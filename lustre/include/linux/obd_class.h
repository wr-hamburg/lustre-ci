/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  Copyright (C) 2001-2003 Cluster File Systems, Inc.
 *
 *   This file is part of Lustre, http://www.lustre.org.
 *
 *   Lustre is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Lustre is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Lustre; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __LINUX_CLASS_OBD_H
#define __LINUX_CLASS_OBD_H

#ifndef __KERNEL__
#include <sys/types.h>
#include <portals/list.h>
#else
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/time.h>
#endif

#include <linux/obd_support.h>
#include <linux/lustre_import.h>
#include <linux/lustre_net.h>
#include <linux/obd.h>
#include <linux/lustre_lib.h>
#include <linux/lustre_idl.h>
#include <linux/lustre_mds.h>
#include <linux/lustre_dlm.h>
#include <linux/lprocfs_status.h>


/* OBD Device Declarations */
#define MAX_OBD_DEVICES 128
extern struct obd_device obd_dev[MAX_OBD_DEVICES];

/* OBD Operations Declarations */
extern struct obd_device *class_conn2obd(struct lustre_handle *);

/* genops.c */
struct obd_export *class_conn2export(struct lustre_handle *);
int class_register_type(struct obd_ops *ops, struct lprocfs_vars *, char *nm);
int class_unregister_type(char *nm);
int class_name2dev(char *name);
struct obd_device *class_name2obd(char *name);
int class_uuid2dev(struct obd_uuid *uuid);
struct obd_device *class_uuid2obd(struct obd_uuid *uuid);

struct obd_export *class_export_get(struct obd_export *);
void class_export_put(struct obd_export *);
struct obd_export *class_new_export(struct obd_device *obddev);
void class_unlink_export(struct obd_export *exp);

struct obd_import *class_import_get(struct obd_import *);
void class_import_put(struct obd_import *);
struct obd_import *class_new_import(void);
void class_destroy_import(struct obd_import *exp);

struct obd_type *class_get_type(char *name);
void class_put_type(struct obd_type *type);
int class_connect(struct lustre_handle *conn, struct obd_device *obd,
                  struct obd_uuid *cluuid);
int class_disconnect(struct lustre_handle *conn, int failover);
void class_disconnect_exports(struct obd_device *obddev, int failover);
/* generic operations shared by various OBD types */
int class_multi_setup(struct obd_device *obddev, uint32_t len, void *data);
int class_multi_cleanup(struct obd_device *obddev);

static inline int obd_check_conn(struct lustre_handle *conn)
{
        struct obd_device *obd;
        if (!conn) {
                CERROR("NULL conn\n");
                RETURN(-ENOTCONN);
        }

        obd = class_conn2obd(conn);
        if (!obd) {
                CERROR("NULL obd\n");
                RETURN(-ENODEV);
        }

        if (!obd->obd_attached) {
                CERROR("obd %d not attached\n", obd->obd_minor);
                RETURN(-ENODEV);
        }

        if (!obd->obd_set_up) {
                CERROR("obd %d not setup\n", obd->obd_minor);
                RETURN(-ENODEV);
        }

        if (!obd->obd_type) {
                CERROR("obd %d not typed\n", obd->obd_minor);
                RETURN(-ENODEV);
        }

        if (!obd->obd_type->typ_ops) {
                CERROR("obd_check_conn: obd %d no operations\n",
                       obd->obd_minor);
                RETURN(-EOPNOTSUPP);
        }
        return 0;
}


#define OBT(dev)        (dev)->obd_type
#define OBP(dev, op)    (dev)->obd_type->typ_ops->o_ ## op

/* Ensure obd_setup: used for disconnect which might be called while
   an obd is stopping. */
#define OBD_CHECK_SETUP(conn, exp)                                      \
do {                                                                    \
        if (!(conn)) {                                                  \
                CERROR("NULL connection\n");                            \
                RETURN(-EINVAL);                                        \
        }                                                               \
                                                                        \
        exp = class_conn2export(conn);                                  \
        if (!(exp)) {                                                   \
                CERROR("No export for conn "LPX64"\n", (conn)->cookie); \
                RETURN(-EINVAL);                                        \
        }                                                               \
                                                                        \
        if (!(exp)->exp_obd->obd_set_up) {                              \
                CERROR("Device %d not setup\n",                         \
                       (exp)->exp_obd->obd_minor);                      \
                class_export_put(exp);                                  \
                RETURN(-EINVAL);                                        \
        }                                                               \
} while (0)

/* Ensure obd_setup and !obd_stopping. */
#define OBD_CHECK_ACTIVE(conn, exp)                                     \
do {                                                                    \
        if (!(conn)) {                                                  \
                CERROR("NULL connection\n");                            \
                RETURN(-EINVAL);                                        \
        }                                                               \
                                                                        \
        exp = class_conn2export(conn);                                  \
        if (!(exp)) {                                                   \
                CERROR("No export for conn "LPX64"\n", (conn)->cookie); \
                RETURN(-EINVAL);                                        \
        }                                                               \
                                                                        \
        if (!(exp)->exp_obd->obd_set_up || (exp)->exp_obd->obd_stopping) { \
                CERROR("Device %d not setup\n",                         \
                       (exp)->exp_obd->obd_minor);                      \
                class_export_put(exp);                                  \
                RETURN(-EINVAL);                                        \
        }                                                               \
} while (0)

/* Ensure obd_setup: used for cleanup which must be called
   while obd is stopping */
#define OBD_CHECK_DEV_STOPPING(obd)                             \
do {                                                            \
        if (!(obd)) {                                           \
                CERROR("NULL device\n");                        \
                RETURN(-ENODEV);                                \
        }                                                       \
                                                                \
        if (!(obd)->obd_set_up) {                               \
                CERROR("Device %d not setup\n",                 \
                       (obd)->obd_minor);                       \
                RETURN(-ENODEV);                                \
        }                                                       \
                                                                \
        if (!(obd)->obd_stopping) {                             \
                CERROR("Device %d not stopping\n",              \
                       (obd)->obd_minor);                       \
                RETURN(-ENODEV);                                \
        }                                                       \
} while (0)

/* ensure obd_setup and !obd_stopping */
#define OBD_CHECK_DEV_ACTIVE(obd)                               \
do {                                                            \
        if (!(obd)) {                                           \
                CERROR("NULL device\n");                        \
                RETURN(-ENODEV);                                \
        }                                                       \
                                                                \
        if (!(obd)->obd_set_up || (obd)->obd_stopping) {        \
                CERROR("Device %d not setup\n",                 \
                       (obd)->obd_minor);                       \
                RETURN(-ENODEV);                                \
        }                                                       \
} while (0)


#ifdef LPROCFS
#define OBD_COUNTER_OFFSET(op)                                  \
        ((offsetof(struct obd_ops, o_ ## op) -                  \
          offsetof(struct obd_ops, o_iocontrol))                \
         / sizeof(((struct obd_ops *)(0))->o_iocontrol))

#define OBD_COUNTER_INCREMENT(obd, op)                          \
        if ((obd)->obd_stats != NULL) {                         \
                unsigned int coffset;                           \
                coffset = (unsigned int)(obd)->obd_cntr_base +  \
                        OBD_COUNTER_OFFSET(op);                 \
                LASSERT(coffset < obd->obd_stats->ls_num);      \
                lprocfs_counter_incr(obd->obd_stats, coffset);  \
        }
#else
#define OBD_COUNTER_OFFSET(op)
#define OBD_COUNTER_INCREMENT(obd, op)
#endif

#define OBD_CHECK_OP(obd, op)                                   \
do {                                                            \
        if (!OBP((obd), op)) {                                  \
                CERROR("obd_" #op ": dev %d no operation\n",    \
                       obd->obd_minor);                         \
                RETURN(-EOPNOTSUPP);                            \
        }                                                       \
} while (0)

static inline int obd_get_info(struct lustre_handle *conn, __u32 keylen,
                               void *key, __u32 *vallen, void *val)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, get_info);
        OBD_COUNTER_INCREMENT(exp->exp_obd, get_info);

        rc = OBP(exp->exp_obd, get_info)(conn, keylen, key, vallen, val);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_set_info(struct lustre_handle *conn, obd_count keylen,
                               void *key, obd_count vallen, void *val)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, set_info);
        OBD_COUNTER_INCREMENT(exp->exp_obd, set_info);

        rc = OBP(exp->exp_obd, set_info)(conn, keylen, key, vallen, val);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_setup(struct obd_device *obd, int datalen, void *data)
{
        int rc;
        ENTRY;

        OBD_CHECK_OP(obd, setup);
        OBD_COUNTER_INCREMENT(obd, setup);

        rc = OBP(obd, setup)(obd, datalen, data);
        RETURN(rc);
}

static inline int obd_cleanup(struct obd_device *obd, int force, int failover)
{
        int rc;
        ENTRY;

        OBD_CHECK_DEV_STOPPING(obd);
        OBD_CHECK_OP(obd, cleanup);
        OBD_COUNTER_INCREMENT(obd, cleanup);

        rc = OBP(obd, cleanup)(obd, force, failover);
        RETURN(rc);
}

/* Pack an in-memory MD struct for storage on disk.
 * Returns +ve size of packed MD (0 for free), or -ve error.
 *
 * If @disk_tgt == NULL, MD size is returned (max size if @mem_src == NULL).
 * If @*disk_tgt != NULL and @mem_src == NULL, @*disk_tgt will be freed.
 * If @*disk_tgt == NULL, it will be allocated
 */
static inline int obd_packmd(struct lustre_handle *conn,
                             struct lov_mds_md **disk_tgt,
                             struct lov_stripe_md *mem_src)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, packmd);
        OBD_COUNTER_INCREMENT(exp->exp_obd, packmd);

        rc = OBP(exp->exp_obd, packmd)(conn, disk_tgt, mem_src);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_size_diskmd(struct lustre_handle *conn,
                                  struct lov_stripe_md *mem_src)
{
        return obd_packmd(conn, NULL, mem_src);
}

/* helper functions */
static inline int obd_alloc_diskmd(struct lustre_handle *conn,
                                   struct lov_mds_md **disk_tgt)
{
        LASSERT(disk_tgt);
        LASSERT(*disk_tgt == NULL);
        return obd_packmd(conn, disk_tgt, NULL);
}

static inline int obd_free_diskmd(struct lustre_handle *conn,
                                  struct lov_mds_md **disk_tgt)
{
        LASSERT(disk_tgt);
        LASSERT(*disk_tgt);
        return obd_packmd(conn, disk_tgt, NULL);
}

/* Unpack an MD struct from disk to in-memory format.
 * Returns +ve size of unpacked MD (0 for free), or -ve error.
 *
 * If @mem_tgt == NULL, MD size is returned (max size if @disk_src == NULL).
 * If @*mem_tgt != NULL and @disk_src == NULL, @*mem_tgt will be freed.
 * If @*mem_tgt == NULL, it will be allocated
 */
static inline int obd_unpackmd(struct lustre_handle *conn,
                               struct lov_stripe_md **mem_tgt,
                               struct lov_mds_md *disk_src,
                               int disk_len)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, unpackmd);
        OBD_COUNTER_INCREMENT(exp->exp_obd, unpackmd);

        rc = OBP(exp->exp_obd, unpackmd)(conn, mem_tgt, disk_src, disk_len);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_size_memmd(struct lustre_handle *conn,
                                 struct lov_mds_md *disk_src,
                                 int disk_len)
{
        return obd_unpackmd(conn, NULL, disk_src, disk_len);
}

/* helper functions */
static inline int obd_alloc_memmd(struct lustre_handle *conn,
                                  struct lov_stripe_md **mem_tgt)
{
        LASSERT(mem_tgt);
        LASSERT(*mem_tgt == NULL);
        return obd_unpackmd(conn, mem_tgt, NULL, 0);
}

static inline int obd_free_memmd(struct lustre_handle *conn,
                                 struct lov_stripe_md **mem_tgt)
{
        LASSERT(mem_tgt);
        LASSERT(*mem_tgt);
        return obd_unpackmd(conn, mem_tgt, NULL, 0);
}

static inline int obd_create(struct lustre_handle *conn, struct obdo *obdo,
                             struct lov_stripe_md **ea,
                             struct obd_trans_info *oti)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, create);
        OBD_COUNTER_INCREMENT(exp->exp_obd, create);

        rc = OBP(exp->exp_obd, create)(conn, obdo, ea, oti);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_destroy(struct lustre_handle *conn, struct obdo *obdo,
                              struct lov_stripe_md *ea,
                              struct obd_trans_info *oti)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, destroy);
        OBD_COUNTER_INCREMENT(exp->exp_obd, destroy);

        rc = OBP(exp->exp_obd, destroy)(conn, obdo, ea, oti);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_getattr(struct lustre_handle *conn, struct obdo *obdo,
                              struct lov_stripe_md *ea)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, getattr);
        OBD_COUNTER_INCREMENT(exp->exp_obd, getattr);

        rc = OBP(exp->exp_obd, getattr)(conn, obdo, ea);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_getattr_async(struct lustre_handle *conn,
                                    struct obdo *obdo, struct lov_stripe_md *ea,
                                    struct ptlrpc_request_set *set)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_SETUP(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, getattr);
        OBD_COUNTER_INCREMENT(exp->exp_obd, getattr);

        rc = OBP(exp->exp_obd, getattr_async)(conn, obdo, ea, set);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_close(struct lustre_handle *conn, struct obdo *obdo,
                            struct lov_stripe_md *ea,
                            struct obd_trans_info *oti)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, close);
        OBD_COUNTER_INCREMENT(exp->exp_obd, close);

        rc = OBP(exp->exp_obd, close)(conn, obdo, ea, oti);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_open(struct lustre_handle *conn, struct obdo *obdo,
                           struct lov_stripe_md *ea, struct obd_trans_info *oti,
                           struct obd_client_handle *och)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, open);
        OBD_COUNTER_INCREMENT(exp->exp_obd, open);

        rc = OBP(exp->exp_obd, open)(conn, obdo, ea, oti, och);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_setattr(struct lustre_handle *conn, struct obdo *obdo,
                              struct lov_stripe_md *ea,
                              struct obd_trans_info *oti)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, setattr);
        OBD_COUNTER_INCREMENT(exp->exp_obd, setattr);

        rc = OBP(exp->exp_obd, setattr)(conn, obdo, ea, oti);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_connect(struct lustre_handle *conn,
                              struct obd_device *obd, struct obd_uuid *cluuid)
{
        int rc;
        ENTRY;

        OBD_CHECK_DEV_ACTIVE(obd);
        OBD_CHECK_OP(obd, connect);
        OBD_COUNTER_INCREMENT(obd, connect);

        rc = OBP(obd, connect)(conn, obd, cluuid);
        RETURN(rc);
}

static inline int obd_disconnect(struct lustre_handle *conn, int failover)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_SETUP(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, disconnect);
        OBD_COUNTER_INCREMENT(exp->exp_obd, disconnect);

        rc = OBP(exp->exp_obd, disconnect)(conn, failover);
        class_export_put(exp);
        RETURN(rc);
}

static inline void obd_destroy_export(struct obd_export *exp)
{
        ENTRY;
        if (OBP(exp->exp_obd, destroy_export))
                OBP(exp->exp_obd, destroy_export)(exp);
        EXIT;
}

static inline int obd_statfs(struct obd_export *exp, struct obd_statfs *osfs)
{
        int rc;
        ENTRY;

        OBD_CHECK_OP(exp->exp_obd, statfs);
        OBD_COUNTER_INCREMENT(exp->exp_obd, statfs);

        rc = OBP(exp->exp_obd, statfs)(exp, osfs);
        RETURN(rc);
}

static inline int obd_syncfs(struct obd_export *exp)
{
        int rc;
        ENTRY;

        OBD_CHECK_OP(exp->exp_obd, syncfs);
        OBD_COUNTER_INCREMENT(exp->exp_obd, syncfs);

        rc = OBP(exp->exp_obd, syncfs)(exp);
        RETURN(rc);
}

static inline int obd_punch(struct lustre_handle *conn, struct obdo *oa,
                            struct lov_stripe_md *ea, obd_size start,
                            obd_size end, struct obd_trans_info *oti)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, punch);
        OBD_COUNTER_INCREMENT(exp->exp_obd, punch);

        rc = OBP(exp->exp_obd, punch)(conn, oa, ea, start, end, oti);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_brw(int cmd, struct lustre_handle *conn,
                          struct lov_stripe_md *ea, obd_count oa_bufs,
                          struct brw_page *pg, struct obd_trans_info *oti)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, brw);
        OBD_COUNTER_INCREMENT(exp->exp_obd, brw);

        if (!(cmd & (OBD_BRW_RWMASK | OBD_BRW_CHECK))) {
                CERROR("obd_brw: cmd must be OBD_BRW_READ, OBD_BRW_WRITE, "
                       "or OBD_BRW_CHECK\n");
                LBUG();
        }

        rc = OBP(exp->exp_obd, brw)(cmd, conn, ea, oa_bufs, pg, oti);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_brw_async(int cmd, struct lustre_handle *conn,
                                struct lov_stripe_md *ea, obd_count oa_bufs,
                                struct brw_page *pg,
                                struct ptlrpc_request_set *set,
                                struct obd_trans_info *oti)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, brw_async);
        OBD_COUNTER_INCREMENT(exp->exp_obd, brw_async);

        if (!(cmd & OBD_BRW_RWMASK)) {
                CERROR("obd_brw: cmd must be OBD_BRW_READ or OBD_BRW_WRITE\n");
                LBUG();
        }

        rc = OBP(exp->exp_obd, brw_async)(cmd, conn, ea, oa_bufs, pg, set, oti);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_preprw(int cmd, struct obd_export *exp, struct obdo *obdo,
                             int objcount, struct obd_ioobj *obj,
                             int niocount, struct niobuf_remote *remote,
                             struct niobuf_local *local, void **desc_private,
                             struct obd_trans_info *oti)
{
        int rc;
        ENTRY;

        OBD_CHECK_OP(exp->exp_obd, preprw);
        OBD_COUNTER_INCREMENT(exp->exp_obd, preprw);

        rc = OBP(exp->exp_obd, preprw)(cmd, exp, obdo, objcount, obj, niocount,
                                       remote, local, desc_private, oti);
        RETURN(rc);
}

static inline int obd_commitrw(int cmd, struct obd_export *exp,
                               int objcount, struct obd_ioobj *obj,
                               int niocount, struct niobuf_local *local,
                               void *desc_private, struct obd_trans_info *oti)
{
        int rc;
        ENTRY;

        OBD_CHECK_OP(exp->exp_obd, commitrw);
        OBD_COUNTER_INCREMENT(exp->exp_obd, commitrw);

        rc = OBP(exp->exp_obd, commitrw)(cmd, exp, objcount, obj, niocount,
                                         local, desc_private, oti);
        RETURN(rc);
}

static inline int obd_iocontrol(unsigned int cmd, struct lustre_handle *conn,
                                int len, void *karg, void *uarg)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, iocontrol);
        OBD_COUNTER_INCREMENT(exp->exp_obd, iocontrol);

        rc = OBP(exp->exp_obd, iocontrol)(cmd, conn, len, karg, uarg);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_enqueue(struct lustre_handle *conn,
                              struct lov_stripe_md *ea,
                              struct lustre_handle *parent_lock,
                              __u32 type, void *cookie, int cookielen,
                              __u32 mode, int *flags, void *cb, void *data,
                              struct lustre_handle *lockh)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, enqueue);
        OBD_COUNTER_INCREMENT(exp->exp_obd, enqueue);

        rc = OBP(exp->exp_obd, enqueue)(conn, ea, parent_lock, type,
                                        cookie, cookielen, mode, flags, cb,
                                        data, lockh);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_match(struct lustre_handle *conn,
                            struct lov_stripe_md *ea, __u32 type, void *cookie,
                            int cookielen, __u32 mode, int *flags, void *data,
                            struct lustre_handle *lockh)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, match);
        OBD_COUNTER_INCREMENT(exp->exp_obd, match);

        rc = OBP(exp->exp_obd, match)(conn, ea, type, cookie, cookielen, mode,
                                      flags, data, lockh);
        class_export_put(exp);
        RETURN(rc);
}


static inline int obd_cancel(struct lustre_handle *conn,
                             struct lov_stripe_md *ea, __u32 mode,
                             struct lustre_handle *lockh)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, cancel);
        OBD_COUNTER_INCREMENT(exp->exp_obd, cancel);

        rc = OBP(exp->exp_obd, cancel)(conn, ea, mode, lockh);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_cancel_unused(struct lustre_handle *conn,
                                    struct lov_stripe_md *ea, int flags,
                                    void *opaque)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, cancel_unused);
        OBD_COUNTER_INCREMENT(exp->exp_obd, cancel_unused);

        rc = OBP(exp->exp_obd, cancel_unused)(conn, ea, flags, opaque);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_san_preprw(int cmd, struct lustre_handle *conn,
                                 int objcount, struct obd_ioobj *obj,
                                 int niocount, struct niobuf_remote *remote)
{
        struct obd_export *exp;
        int rc;

        OBD_CHECK_ACTIVE(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, preprw);
        OBD_COUNTER_INCREMENT(exp->exp_obd, preprw);

        rc = OBP(exp->exp_obd, san_preprw)(cmd, conn, objcount, obj,
                                           niocount, remote);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_mark_page_dirty(struct lustre_handle *conn,
                                      struct lov_stripe_md *lsm,  
                                      unsigned long offset)
{
        struct obd_export *exp;
        int rc;

        OBD_CHECK_SETUP(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, mark_page_dirty);

        rc = OBP(exp->exp_obd, mark_page_dirty)(conn, lsm, offset);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_clear_dirty_pages(struct lustre_handle *conn,
                                        struct lov_stripe_md *lsm,  
                                        unsigned long start,
                                        unsigned long end,
                                        unsigned long *cleared)
{
        struct obd_export *exp;
        int rc;

        OBD_CHECK_SETUP(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, clear_dirty_pages);

        rc = OBP(exp->exp_obd, clear_dirty_pages)(conn, lsm, start, end,
                                                  cleared);
        class_export_put(exp);
        RETURN(rc);
}

static inline int obd_last_dirty_offset(struct lustre_handle *conn,
                                      struct lov_stripe_md *lsm,
                                      unsigned long *offset)
{
        struct obd_export *exp;
        int rc;

        OBD_CHECK_SETUP(conn, exp);
        OBD_CHECK_OP(exp->exp_obd, last_dirty_offset);

        rc = OBP(exp->exp_obd, last_dirty_offset)(conn, lsm, offset);
        class_export_put(exp);
        RETURN(rc);
}

/* OBD Metadata Support */

extern int obd_init_caches(void);
extern void obd_cleanup_caches(void);

static inline struct lustre_handle *obdo_handle(struct obdo *oa)
{
        return (struct lustre_handle *)&oa->o_inline;
}

/* support routines */
extern kmem_cache_t *obdo_cachep;
static inline struct obdo *obdo_alloc(void)
{
        struct obdo *oa;

        oa = kmem_cache_alloc(obdo_cachep, SLAB_KERNEL);
        if (oa == NULL)
                LBUG();
        memset(oa, 0, sizeof (*oa));

        return oa;
}

static inline void obdo_free(struct obdo *oa)
{
        if (!oa)
                return;
        kmem_cache_free(obdo_cachep, oa);
}

#if !defined(__KERNEL__) || (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
#define to_kdev_t(dev) dev
#define kdev_t_to_nr(dev) dev
#endif

#ifdef __KERNEL__
static inline void obdo_from_iattr(struct obdo *oa, struct iattr *attr)
{
        unsigned int ia_valid = attr->ia_valid;

        if (ia_valid & ATTR_ATIME) {
                oa->o_atime = LTIME_S(attr->ia_atime);
                oa->o_valid |= OBD_MD_FLATIME;
        }
        if (ia_valid & ATTR_MTIME) {
                oa->o_mtime = LTIME_S(attr->ia_mtime);
                oa->o_valid |= OBD_MD_FLMTIME;
        }
        if (ia_valid & ATTR_CTIME) {
                oa->o_ctime = LTIME_S(attr->ia_ctime);
                oa->o_valid |= OBD_MD_FLCTIME;
        }
        if (ia_valid & ATTR_SIZE) {
                oa->o_size = attr->ia_size;
                oa->o_valid |= OBD_MD_FLSIZE;
        }
        if (ia_valid & ATTR_MODE) {
                oa->o_mode = attr->ia_mode;
                oa->o_valid |= OBD_MD_FLTYPE | OBD_MD_FLMODE;
                if (!in_group_p(oa->o_gid) && !capable(CAP_FSETID))
                        oa->o_mode &= ~S_ISGID;
        }
        if (ia_valid & ATTR_UID) {
                oa->o_uid = attr->ia_uid;
                oa->o_valid |= OBD_MD_FLUID;
        }
        if (ia_valid & ATTR_GID) {
                oa->o_gid = attr->ia_gid;
                oa->o_valid |= OBD_MD_FLGID;
        }
}


static inline void iattr_from_obdo(struct iattr *attr, struct obdo *oa,
                                   obd_flag valid)
{
        memset(attr, 0, sizeof(*attr));
        if (valid & OBD_MD_FLATIME) {
                LTIME_S(attr->ia_atime) = oa->o_atime;
                attr->ia_valid |= ATTR_ATIME;
        }
        if (valid & OBD_MD_FLMTIME) {
                LTIME_S(attr->ia_mtime) = oa->o_mtime;
                attr->ia_valid |= ATTR_MTIME;
        }
        if (valid & OBD_MD_FLCTIME) {
                LTIME_S(attr->ia_ctime) = oa->o_ctime;
                attr->ia_valid |= ATTR_CTIME;
        }
        if (valid & OBD_MD_FLSIZE) {
                attr->ia_size = oa->o_size;
                attr->ia_valid |= ATTR_SIZE;
        }
        if (valid & OBD_MD_FLTYPE) {
                attr->ia_mode = (attr->ia_mode & ~S_IFMT)|(oa->o_mode & S_IFMT);
                attr->ia_valid |= ATTR_MODE;
        }
        if (valid & OBD_MD_FLMODE) {
                attr->ia_mode = (attr->ia_mode & S_IFMT)|(oa->o_mode & ~S_IFMT);
                attr->ia_valid |= ATTR_MODE;
                if (!in_group_p(oa->o_gid) && !capable(CAP_FSETID))
                        attr->ia_mode &= ~S_ISGID;
        }
        if (valid & OBD_MD_FLUID)
        {
                attr->ia_uid = oa->o_uid;
                attr->ia_valid |= ATTR_UID;
        }
        if (valid & OBD_MD_FLGID) {
                attr->ia_gid = oa->o_gid;
                attr->ia_valid |= ATTR_GID;
        }
}


/* WARNING: the file systems must take care not to tinker with
   attributes they don't manage (such as blocks). */


static inline void obdo_from_inode(struct obdo *dst, struct inode *src,
                                   obd_flag valid)
{
        if (valid & OBD_MD_FLATIME)
                dst->o_atime = LTIME_S(src->i_atime);
        if (valid & OBD_MD_FLMTIME)
                dst->o_mtime = LTIME_S(src->i_mtime);
        if (valid & OBD_MD_FLCTIME)
                dst->o_ctime = LTIME_S(src->i_ctime);
        if (valid & OBD_MD_FLSIZE)
                dst->o_size = src->i_size;
        if (valid & OBD_MD_FLBLOCKS)   /* allocation of space */
                dst->o_blocks = src->i_blocks;
        if (valid & OBD_MD_FLBLKSZ)
                dst->o_blksize = src->i_blksize;
        if (valid & OBD_MD_FLTYPE)
                dst->o_mode = (dst->o_mode & ~S_IFMT) | (src->i_mode & S_IFMT);
        if (valid & OBD_MD_FLMODE)
                dst->o_mode = (dst->o_mode & S_IFMT) | (src->i_mode & ~S_IFMT);
        if (valid & OBD_MD_FLUID)
                dst->o_uid = src->i_uid;
        if (valid & OBD_MD_FLGID)
                dst->o_gid = src->i_gid;
        if (valid & OBD_MD_FLFLAGS)
                dst->o_flags = src->i_flags;
        if (valid & OBD_MD_FLNLINK)
                dst->o_nlink = src->i_nlink;
        if (valid & OBD_MD_FLGENER)
                dst->o_generation = src->i_generation;
        if (valid & OBD_MD_FLRDEV)
                dst->o_rdev = (__u32)kdev_t_to_nr(src->i_rdev);

        dst->o_valid |= (valid & ~OBD_MD_FLID);
}

static inline void obdo_refresh_inode(struct inode *dst, struct obdo *src,
                                      obd_flag valid)
{
        valid &= src->o_valid;

        if (valid & OBD_MD_FLATIME && src->o_atime > LTIME_S(dst->i_atime))
                LTIME_S(dst->i_atime) = src->o_atime;
        if (valid & OBD_MD_FLMTIME && src->o_mtime > LTIME_S(dst->i_mtime))
                LTIME_S(dst->i_mtime) = src->o_mtime;
        if (valid & OBD_MD_FLCTIME && src->o_ctime > LTIME_S(dst->i_ctime))
                LTIME_S(dst->i_ctime) = src->o_ctime;
        if (valid & OBD_MD_FLSIZE && src->o_size > dst->i_size)
                dst->i_size = src->o_size;
        /* allocation of space */
        if (valid & OBD_MD_FLBLOCKS && src->o_blocks > dst->i_blocks)
                dst->i_blocks = src->o_blocks;
}

static inline void obdo_to_inode(struct inode *dst, struct obdo *src,
                                 obd_flag valid)
{
        valid &= src->o_valid;

        if (valid & OBD_MD_FLATIME)
                LTIME_S(dst->i_atime) = src->o_atime;
        if (valid & OBD_MD_FLMTIME)
                LTIME_S(dst->i_mtime) = src->o_mtime;
        if (valid & OBD_MD_FLCTIME && src->o_ctime > LTIME_S(dst->i_ctime))
                LTIME_S(dst->i_ctime) = src->o_ctime;
        if (valid & OBD_MD_FLSIZE)
                dst->i_size = src->o_size;
        if (valid & OBD_MD_FLBLOCKS) /* allocation of space */
                dst->i_blocks = src->o_blocks;
        if (valid & OBD_MD_FLBLKSZ)
                dst->i_blksize = src->o_blksize;
        if (valid & OBD_MD_FLTYPE)
                dst->i_mode = (dst->i_mode & ~S_IFMT) | (src->o_mode & S_IFMT);
        if (valid & OBD_MD_FLMODE)
                dst->i_mode = (dst->i_mode & S_IFMT) | (src->o_mode & ~S_IFMT);
        if (valid & OBD_MD_FLUID)
                dst->i_uid = src->o_uid;
        if (valid & OBD_MD_FLGID)
                dst->i_gid = src->o_gid;
        if (valid & OBD_MD_FLFLAGS)
                dst->i_flags = src->o_flags;
        if (valid & OBD_MD_FLNLINK)
                dst->i_nlink = src->o_nlink;
        if (valid & OBD_MD_FLGENER)
                dst->i_generation = src->o_generation;
        if (valid & OBD_MD_FLRDEV)
                dst->i_rdev = to_kdev_t(src->o_rdev);
}
#endif

static inline void obdo_cpy_md(struct obdo *dst, struct obdo *src,
                               obd_flag valid)
{
#ifdef __KERNEL__
        CDEBUG(D_INODE, "src obdo %Ld valid 0x%x, dst obdo %Ld\n",
               (unsigned long long)src->o_id, src->o_valid,
               (unsigned long long)dst->o_id);
#endif
        if (valid & OBD_MD_FLATIME)
                dst->o_atime = src->o_atime;
        if (valid & OBD_MD_FLMTIME)
                dst->o_mtime = src->o_mtime;
        if (valid & OBD_MD_FLCTIME)
                dst->o_ctime = src->o_ctime;
        if (valid & OBD_MD_FLSIZE)
                dst->o_size = src->o_size;
        if (valid & OBD_MD_FLBLOCKS) /* allocation of space */
                dst->o_blocks = src->o_blocks;
        if (valid & OBD_MD_FLBLKSZ)
                dst->o_blksize = src->o_blksize;
        if (valid & OBD_MD_FLTYPE)
                dst->o_mode = (dst->o_mode & ~S_IFMT) | (src->o_mode & S_IFMT);
        if (valid & OBD_MD_FLMODE)
                dst->o_mode = (dst->o_mode & S_IFMT) | (src->o_mode & ~S_IFMT);
        if (valid & OBD_MD_FLUID)
                dst->o_uid = src->o_uid;
        if (valid & OBD_MD_FLGID)
                dst->o_gid = src->o_gid;
        if (valid & OBD_MD_FLFLAGS)
                dst->o_flags = src->o_flags;
        /*
        if (valid & OBD_MD_FLOBDFLG)
                dst->o_obdflags = src->o_obdflags;
        */
        if (valid & OBD_MD_FLNLINK)
                dst->o_nlink = src->o_nlink;
        if (valid & OBD_MD_FLGENER)
                dst->o_generation = src->o_generation;
        if (valid & OBD_MD_FLRDEV)
                dst->o_rdev = src->o_rdev;
        if (valid & OBD_MD_FLINLINE &&
             src->o_obdflags & OBD_FL_INLINEDATA) {
                memcpy(dst->o_inline, src->o_inline, sizeof(src->o_inline));
                dst->o_obdflags |= OBD_FL_INLINEDATA;
        }

        dst->o_valid |= valid;
}


/* returns FALSE if comparison (by flags) is same, TRUE if changed */
static inline int obdo_cmp_md(struct obdo *dst, struct obdo *src,
                              obd_flag compare)
{
        int res = 0;

        if ( compare & OBD_MD_FLATIME )
                res = (res || (dst->o_atime != src->o_atime));
        if ( compare & OBD_MD_FLMTIME )
                res = (res || (dst->o_mtime != src->o_mtime));
        if ( compare & OBD_MD_FLCTIME )
                res = (res || (dst->o_ctime != src->o_ctime));
        if ( compare & OBD_MD_FLSIZE )
                res = (res || (dst->o_size != src->o_size));
        if ( compare & OBD_MD_FLBLOCKS ) /* allocation of space */
                res = (res || (dst->o_blocks != src->o_blocks));
        if ( compare & OBD_MD_FLBLKSZ )
                res = (res || (dst->o_blksize != src->o_blksize));
        if ( compare & OBD_MD_FLTYPE )
                res = (res || (((dst->o_mode ^ src->o_mode) & S_IFMT) != 0));
        if ( compare & OBD_MD_FLMODE )
                res = (res || (((dst->o_mode ^ src->o_mode) & ~S_IFMT) != 0));
        if ( compare & OBD_MD_FLUID )
                res = (res || (dst->o_uid != src->o_uid));
        if ( compare & OBD_MD_FLGID )
                res = (res || (dst->o_gid != src->o_gid));
        if ( compare & OBD_MD_FLFLAGS )
                res = (res || (dst->o_flags != src->o_flags));
        if ( compare & OBD_MD_FLNLINK )
                res = (res || (dst->o_nlink != src->o_nlink));
        if ( compare & OBD_MD_FLGENER )
                res = (res || (dst->o_generation != src->o_generation));
        /* XXX Don't know if thses should be included here - wasn't previously
        if ( compare & OBD_MD_FLINLINE )
                res = (res || memcmp(dst->o_inline, src->o_inline));
        */
        return res;
}

/* I'm as embarrassed about this as you are.
 *
 * <shaver> // XXX do not look into _superhack with remaining eye
 * <shaver> // XXX if this were any uglier, I'd get my own show on MTV */
extern int (*ptlrpc_put_connection_superhack)(struct ptlrpc_connection *c);
extern void (*ptlrpc_abort_inflight_superhack)(struct obd_import *imp);

struct obd_statfs;
struct statfs;
void statfs_pack(struct obd_statfs *osfs, struct statfs *sfs);
void statfs_unpack(struct statfs *sfs, struct obd_statfs *osfs);

struct obd_class_user_state {
        struct obd_device     *ocus_current_obd;
        struct list_head       ocus_conns;
};

struct obd_class_user_conn {
        struct list_head       ocuc_chain;
        struct lustre_handle   ocuc_conn;
};


/* sysctl.c */
extern void obd_sysctl_init (void);
extern void obd_sysctl_clean (void);

/* uuid.c  */
typedef __u8 class_uuid_t[16];
//int class_uuid_parse(struct obd_uuid in, class_uuid_t out);
void class_uuid_unparse(class_uuid_t in, struct obd_uuid *out);

/* lustre_peer.c    */
int lustre_uuid_to_peer(char *uuid, struct lustre_peer *peer);
int class_add_uuid(char *uuid, __u64 nid, __u32 nal);
int class_del_uuid (char *uuid);
void class_init_uuidlist(void);
void class_exit_uuidlist(void);

#endif /* __LINUX_OBD_CLASS_H */
