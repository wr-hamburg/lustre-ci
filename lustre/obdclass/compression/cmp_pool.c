/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Major changes by:
 * Leonhard Reichenbach <4reichen@informatik.uni-hamburg.de>
 */

#include <obd_support.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/rhashtable.h>
#include <linux/jhash.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include "cmp_pool.h"

#define MAX_MEM_WAIT_TIME 2048

DEFINE_SPINLOCK(cmp_lock_init);

DEFINE_SPINLOCK(cmp_lock);
LIST_HEAD(cmp_list);

DEFINE_SPINLOCK(empty_containers_lock);
LIST_HEAD(empty_cnt_list);

unsigned int cmp_list_total_length;
unsigned int cmp_list_current_length;

unsigned int buf_size;
unsigned int page_order;

unsigned int inited;

struct many_buf_cont {
	struct page *key;
	struct rhash_head node;
};

struct rhashtable tr_map;

struct rhashtable_params tr_map_params = {
	.head_offset = offsetof(struct many_buf_cont, node),
	.key_offset = offsetof(struct many_buf_cont, key),
	.key_len = sizeof(struct page *),
};

unsigned int cmp_pool_get_buf_size(void)
{
	return buf_size;
}
EXPORT_SYMBOL(cmp_pool_get_buf_size);

int cmp_pool_alloc_page_bundles(unsigned int count)
{
	int res;
	unsigned int i;
	struct page_bundle *tmp = NULL;

	for (i = 0; i < count; i++) {

		/* TODO: treshold */

		OBD_ALLOC_PTR(tmp);

		if (tmp == NULL) {
			res = -ENOMEM;
			goto _finish_alloc;
		}

		tmp->page = alloc_pages(GFP_NOFS | __GFP_ZERO | __GFP_COMP,
					page_order);

		if (tmp->page == NULL) {
			OBD_FREE_PTR(tmp);
			res = -ENOMEM;
			goto _finish_alloc;
		}

	INIT_LIST_HEAD(&tmp->list);
	spin_lock(&cmp_lock);
	list_add_tail(&tmp->list, &cmp_list);
	spin_unlock(&cmp_lock);

	}
	/* end of loop -> (i == count) */
	cmp_list_total_length += i;
	cmp_list_current_length += i;
	res = 0;

	CDEBUG(D_INFO, "Successfully finished compression pool allocation\n");

_finish_alloc:
	return res;
}

struct page **_downsize_page_bundle(struct page *old_page, unsigned int count,
				    unsigned int new_page_count)
{
	unsigned int i;
	struct page **res;

	OBD_ALLOC(res, sizeof(*res) * count);
	if (res == NULL) {
		CERROR("_downsize failed\n");
		goto _finish__downsize;
	}

	for (i = 0; i < count; i++)
		res[i] = old_page + i * new_page_count;

/* TODO: cleanup */
_finish__downsize:
	return res;
}

static inline int wait_for_mem(unsigned int count)
{
	unsigned int i, j;

	j = 0;
	i = 1;

	while (cmp_list_current_length < count && j < MAX_MEM_WAIT_TIME) {
		spin_unlock(&cmp_lock);
		msleep(i);
		j += i;
		i = (i == 128) ? 128 : i << 1;
		spin_lock(&cmp_lock);
	}

	return MAX_MEM_WAIT_TIME - j;
}

int cmp_pool_get_many_buffers(unsigned int count, unsigned int size,
			      void **destination)
{
	struct page_bundle *tmp;
	struct list_head **empty_cnt_array;
	struct many_buf_cont **container;
	struct page **pg_array;
	unsigned int i;
	int res;
	int err = 0;

	container = NULL;

	if (unlikely(size != buf_size)) {
		if (size < buf_size) {
			CWARN("Requested bufsize %d too small, returned %d.\n",
					size, buf_size);
		} else {
			CWARN("Requested bufsize %d too large, max size %d!\n",
					size, buf_size);
			return -EDOM;
		}
	}

	OBD_ALLOC(empty_cnt_array, sizeof(*empty_cnt_array) * count);
	if (empty_cnt_array == NULL) {
		res = -ENOMEM;
		goto _many_buf_exit;
	}

	OBD_ALLOC(container, count * sizeof(container));
	if (container == NULL) {
		res = -ENOMEM;
		goto _many_buf_exit;
	}

	for (i = 0; i < count; i++) {
		OBD_ALLOC_PTR(container[i]);
		if (container[i] == NULL) {
			res = -ENOMEM;
			goto _many_buf_exit;
		}
	}

	OBD_ALLOC(pg_array, sizeof(*pg_array) * (count + 1));
	if (pg_array == NULL) {
		res = -ENOMEM;
		for (i = 0; i < count; i++)
			OBD_FREE_PTR(container[i]);
		goto _many_buf_exit;
	}

	spin_lock(&cmp_lock);

	if (unlikely(wait_for_mem(count) < 1)) {
		spin_unlock(&cmp_lock);
		CERROR("Timeout while waiting for memory\n");
		res = -ETIME;
		for (i = 0; i < count; i++)
			OBD_FREE_PTR(container[i]);
		OBD_FREE(pg_array, sizeof(*pg_array) * (count + 1));
		goto _many_buf_exit;
	}

	for (i = 0; i < count; i++) {
		tmp = list_first_entry(&cmp_list, struct page_bundle, list);

		list_del_init(&tmp->list);
		cmp_list_current_length--;

		pg_array[i] = tmp->page;
		empty_cnt_array[i] = &tmp->list;

		tmp->page = NULL;
	}

	spin_unlock(&cmp_lock);

	spin_lock(&empty_containers_lock);
	for (i = 0; i < count; i++)
		list_add_tail((empty_cnt_array[i]), &empty_cnt_list);

	spin_unlock(&empty_containers_lock);

	for (i = 0; i < count; i++) {
		destination[i] = kmap(pg_array[i]);

		container[i]->key = pg_array[i];
		err = rhashtable_insert_fast(&tr_map, &container[i]->node,
						tr_map_params);
		if (err != 0) {
			OBD_FREE(pg_array, sizeof(*pg_array) * (count + 1));
			OBD_FREE_PTR(container);
			CERROR("Failed insert into hashmap\n");
			res = -EAGAIN;
			goto _many_buf_exit;
		}
	}

	res = 0;

_many_buf_exit:
	if (container != NULL)
		OBD_FREE(container, count * sizeof(*container));
	OBD_FREE(empty_cnt_array, sizeof(*empty_cnt_array) * count);
	return res;
}
EXPORT_SYMBOL(cmp_pool_get_many_buffers);

void *cmp_pool_get_page_buffer(unsigned int size)
{
	int err;
	void *destination = NULL;

	err = cmp_pool_get_many_buffers(1, size, &destination);
	/* TODO: error handling */
	if (err != 0)
		CERROR("get_many_buffers returned %d\n", err);
	if (destination == NULL && err != 0)
		CERROR("This really shouldn't happen :(\n");

	return destination;
}
EXPORT_SYMBOL(cmp_pool_get_page_buffer);

/**
 * Helper function that returns a page and its bundle to the pool
 */
int _return_page(struct page *page)
{
	struct page_bundle *tmp;

	spin_lock(&empty_containers_lock);
	tmp = list_first_entry(&empty_cnt_list, struct page_bundle, list);
	list_del_init(&tmp->list);
	spin_unlock(&empty_containers_lock);

	tmp->page = page;

	spin_lock(&cmp_lock);
	list_add_tail(&tmp->list, &cmp_list);
	cmp_list_current_length++;
	spin_unlock(&cmp_lock);

	return 0;
}

int cmp_pool_return_page_buffer(void *buffer)
{
	struct page *page;
	struct many_buf_cont *container;
	int res = 0;
	int err = 0;

	if (buffer == NULL) {
		CERROR("Tried to return null buffer\n");
		return -EINVAL;
	}

	page = virt_to_page(buffer);

	container = rhashtable_lookup_fast(&tr_map, &page, tr_map_params);
	if (container == NULL) {
		CERROR("Buffer not found in map\n");
		return -EFAULT;
	}

	err = rhashtable_remove_fast(&tr_map, &container->node, tr_map_params);
	if (err != 0) {
		CERROR("Failed to remove from hashtable\n");
		/* TODO: handle this */
	}

	kunmap(page);
	res = _return_page(page);

	if (res != 0)
		CERROR("Could not return all pages\n");

	OBD_FREE_PTR(container);

	return res;
}
EXPORT_SYMBOL(cmp_pool_return_page_buffer);

/**
 * Converts a struct page *array to a struct brw_page *array
 */
struct brw_page **pg_to_brw(struct page **pages, unsigned int length)
{
	unsigned int i;
	struct brw_page **res = NULL;
	struct brw_page *tmp = NULL;

	OBD_ALLOC(res, sizeof(*res) * (length + 1));
	if (res == NULL)
		return res; /* out of memory or worse */

	for (i = 1; i < length; i++) {
		OBD_ALLOC_PTR(tmp);
		if (tmp == NULL)
			return NULL;

		tmp->pg = pages[i];
		res[i] = tmp;
	}
	res[i] = NULL;

	return res;
}

struct brw_page **cmp_pool_get_page_array(unsigned int count)
{
	struct page **pages;
	struct page_bundle *tmp;
	struct brw_page **res = NULL;
	unsigned int pages_per_bundle = buf_size >> 12;

	unsigned int page_count = 1 << page_order;

	if (unlikely(count != page_count)) {
		if (count < page_count) {
			CWARN("Requested %d pages, returned %d pages\n",
					count, page_count);
		} else {
			CWARN("Requested %d pages, max page count is %d\n",
					count, page_count);
			return NULL;
		}
	}

	spin_lock(&cmp_lock);

	if (unlikely(wait_for_mem(1) < 1)) {
		spin_unlock(&cmp_lock);
		return NULL;
	}

	tmp = list_first_entry(&cmp_list, struct page_bundle, list);
	list_del_init(&tmp->list);
	cmp_list_current_length--;
	spin_unlock(&cmp_lock);

	pages = _downsize_page_bundle(tmp->page, pages_per_bundle, 1);
	res = pg_to_brw(pages, pages_per_bundle);

	tmp->page = NULL;

	spin_lock(&empty_containers_lock);
	list_add_tail(&tmp->list, &empty_cnt_list);
	spin_unlock(&empty_containers_lock);

	OBD_FREE(pages, sizeof(*pages) * pages_per_bundle);

	return res;
}
EXPORT_SYMBOL(cmp_pool_get_page_array);

int cmp_pool_return_page_array(struct brw_page **pages)
{
	unsigned int i, length;
	struct page *first_page;

	first_page = (*pages)->pg;

	for (i = 0; pages[i] != NULL; i++)
		OBD_FREE_PTR(pages[i]);

	/* i is now the number of pages in the array */
	length = i;

	if (length != (buf_size >> 12))
		return -EFAULT;

	return _return_page(first_page);
}
EXPORT_SYMBOL(cmp_pool_return_page_array);

int cmp_pool_init(unsigned int n_bundles, unsigned int buffer_size)
{
	int a = 0;
	int b = 0;

	spin_lock(&cmp_lock_init);
	if (!inited) {
		inited++;
	} else {
		CERROR("cmp_pool exsits already.\n");
		spin_unlock(&cmp_lock_init);
		goto out;
	}
	spin_unlock(&cmp_lock_init);

	page_order = order_base_2(buffer_size) - ilog2(PAGE_SIZE);
	/* 12 because log_2(4096) = 12, TODO: use PAGE_SIZE */
	buf_size = (1 << page_order) * PAGE_SIZE;

	if (unlikely(buf_size < buffer_size))
		CERROR("Requested buffer_size %d was rounded up to %d\n",
			buffer_size, buf_size);

	if (unlikely(buf_size > buffer_size))
		CWARN("Requested buffer_size %d was rounded up to %d\n",
			buffer_size, buf_size);

	a = cmp_pool_alloc_page_bundles(n_bundles);
	if (a == -ENOMEM)
		CERROR("Not enough memory for init\n");

	tr_map_params.max_size = n_bundles;
	tr_map_params.nelem_hint = n_bundles / 4 * 3;

	b = rhashtable_init(&tr_map, &tr_map_params);
	if (b == -ENOMEM)
		CERROR("Not enough memory to init hashtable\n");

out:
	return a; /* TODO: error handling */
}
EXPORT_SYMBOL(cmp_pool_init);

int _free_forgotten_pages(void)
{
	struct rhashtable_iter hti;
	struct many_buf_cont *container;
	int err = 0;

	rhashtable_walk_enter(&tr_map, &hti);

	rhashtable_walk_start(&hti);

	while (cmp_list_current_length < cmp_list_total_length) {
		container = rhashtable_walk_next(&hti);

		if (PTR_ERR(container) == -EAGAIN) {
			continue;
		} else if (IS_ERR(container)) {
			err = PTR_ERR(container);
			CERROR("Encountered error %d\n", err);
			break;
		} else if (container == NULL) {
			break;
		}

		CDEBUG(D_INFO, "Returning page %p\n", container->key);

		__free_pages(container->key, page_order);
	}

	rhashtable_walk_stop(&hti);
	rhashtable_walk_exit(&hti);

	return err;
}

int cmp_pool_free(void)
{
	struct page_bundle *current_bundle;
	struct page_bundle *next_bundle;
	unsigned int forgotten_pages;
	int err = 0;

	spin_lock(&cmp_lock_init);
	if (!inited) {
		CERROR("Tried to free uninitialized cmp_pool - prevented.\n");
		spin_unlock(&cmp_lock_init);
		goto out;
	} else {
		inited--;
	}
	spin_unlock(&cmp_lock_init);

	spin_lock(&cmp_lock);

	forgotten_pages = cmp_list_total_length - cmp_list_current_length;

	if (forgotten_pages)
		CERROR("%d buffers were not returned to the pool\n",
			forgotten_pages);

	list_for_each_entry_safe(current_bundle, next_bundle, &cmp_list, list) {
		list_del(&current_bundle->list);
		__free_pages(current_bundle->page, page_order);
		OBD_FREE_PTR(current_bundle);
		cmp_list_total_length--;
		cmp_list_current_length--;
	}

	spin_unlock(&cmp_lock);

	/* this could go horribly wrong */
	if (cmp_list_total_length > cmp_list_current_length)
		err = _free_forgotten_pages();

	forgotten_pages = cmp_list_total_length - cmp_list_current_length;
	if (err)
		CERROR("bytes LEAKED: %d\n", forgotten_pages * buf_size);

out:
	return 0;
}
EXPORT_SYMBOL(cmp_pool_free);

MODULE_LICENSE("GPL");

