/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  Copyright (C) 2001-2003 Cluster File Systems, Inc.
 *   Author: Peter J. Braam <braam@clusterfs.com>
 *   Author: Phil Schwan <phil@clusterfs.com>
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
 *  Storage Target Handling functions
 *  Lustre Object Server Module (OST)
 *
 *  This server is single threaded at present (but can easily be multi
 *  threaded). For testing and management it is treated as an
 *  obd_device, although it does not export a full OBD method table
 *  (the requests are coming in over the wire, so object target
 *  modules do not have a full method table.)
 */

#define EXPORT_SYMTAB
#define DEBUG_SUBSYSTEM S_OST

#include <linux/module.h>
#include <linux/obd_ost.h>
#include <linux/lustre_net.h>
#include <linux/lustre_dlm.h>
#include <linux/lustre_export.h>
#include <linux/init.h>
#include <linux/lprocfs_status.h>

inline void oti_to_request(struct obd_trans_info *oti,
                           struct ptlrpc_request *req)
{
        int i;
        struct oti_req_ack_lock *ack_lock;

        if(oti == NULL)
                return;

        if (req->rq_repmsg)
                req->rq_repmsg->transno = oti->oti_transno;

        /* XXX 4 == entries in oti_ack_locks??? */
        for (ack_lock = oti->oti_ack_locks, i = 0; i < 4; i++, ack_lock++) {
                if (!ack_lock->mode)
                        break;
                memcpy(&req->rq_ack_locks[i].lock, &ack_lock->lock,
                       sizeof(req->rq_ack_locks[i].lock));
                req->rq_ack_locks[i].mode = ack_lock->mode;
        }
        EXIT;
}

static int ost_destroy(struct ptlrpc_request *req, struct obd_trans_info *oti)
{
        struct lustre_handle *conn = &req->rq_reqmsg->handle;
        struct ost_body *body;
        int rc, size = sizeof(*body);
        ENTRY;

        body = lustre_swab_reqbuf (req, 0, sizeof (*body),
                                   lustre_swab_ost_body);
        if (body == NULL)
                RETURN (-EFAULT);

        rc = lustre_pack_msg(1, &size, NULL, &req->rq_replen, &req->rq_repmsg);
        if (rc)
                RETURN(rc);

        req->rq_status = obd_destroy(conn, &body->oa, NULL, oti);
        RETURN(0);
}

static int ost_getattr(struct ptlrpc_request *req)
{
        struct lustre_handle *conn = (struct lustre_handle *)req->rq_reqmsg;
        struct ost_body *body, *repbody;
        int rc, size = sizeof(*body);
        ENTRY;

        body = lustre_swab_reqbuf (req, 0, sizeof (*body),
                                   lustre_swab_ost_body);
        if (body == NULL)
                RETURN (-EFAULT);

        rc = lustre_pack_msg(1, &size, NULL, &req->rq_replen, &req->rq_repmsg);
        if (rc)
                RETURN(rc);

        repbody = lustre_msg_buf (req->rq_repmsg, 0, sizeof (*repbody));
        memcpy(&repbody->oa, &body->oa, sizeof(body->oa));
        req->rq_status = obd_getattr(conn, &repbody->oa, NULL);
        RETURN(0);
}

static int ost_statfs(struct ptlrpc_request *req)
{
        struct lustre_handle *conn = (struct lustre_handle *)req->rq_reqmsg;
        struct obd_statfs *osfs;
        int rc, size = sizeof(*osfs);
        ENTRY;

        rc = lustre_pack_msg(1, &size, NULL, &req->rq_replen, &req->rq_repmsg);
        if (rc)
                RETURN(rc);

        osfs = lustre_msg_buf(req->rq_repmsg, 0, sizeof (*osfs));
        memset(osfs, 0, size);

        req->rq_status = obd_statfs(conn, osfs);
        if (req->rq_status != 0)
                CERROR("ost: statfs failed: rc %d\n", req->rq_status);

        RETURN(0);
}

static int ost_syncfs(struct ptlrpc_request *req)
{
        struct obd_statfs *osfs;
        int rc, size = sizeof(*osfs);
        ENTRY;

        rc = lustre_pack_msg(0, &size, NULL, &req->rq_replen, &req->rq_repmsg);
        if (rc)
                RETURN(rc);

        rc = obd_syncfs(req->rq_export);
        if (rc) {
                CERROR("ost: syncfs failed: rc %d\n", rc);
                req->rq_status = rc;
                RETURN(rc);
        }

        RETURN(0);
}

static int ost_open(struct ptlrpc_request *req, struct obd_trans_info *oti)
{
        struct lustre_handle *conn = (struct lustre_handle *)req->rq_reqmsg;
        struct ost_body *body, *repbody;
        int rc, size = sizeof(*repbody);
        ENTRY;

        body = lustre_swab_reqbuf (req, 0, sizeof (*body),
                                   lustre_swab_ost_body);
        if (body == NULL)
                return (-EFAULT);

        rc = lustre_pack_msg(1, &size, NULL, &req->rq_replen, &req->rq_repmsg);
        if (rc)
                RETURN(rc);

        repbody = lustre_msg_buf (req->rq_repmsg, 0, sizeof (*repbody));
        memcpy(&repbody->oa, &body->oa, sizeof(body->oa));
        req->rq_status = obd_open(conn, &repbody->oa, NULL, oti, NULL);
        RETURN(0);
}

static int ost_close(struct ptlrpc_request *req, struct obd_trans_info *oti)
{
        struct lustre_handle *conn = (struct lustre_handle *)req->rq_reqmsg;
        struct ost_body *body, *repbody;
        int rc, size = sizeof(*repbody);
        ENTRY;

        body = lustre_swab_reqbuf (req, 0, sizeof (*body),
                                   lustre_swab_ost_body);
        if (body == NULL)
                RETURN (-EFAULT);

        rc = lustre_pack_msg(1, &size, NULL, &req->rq_replen, &req->rq_repmsg);
        if (rc)
                RETURN(rc);

        repbody = lustre_msg_buf(req->rq_repmsg, 0, sizeof (*repbody));
        memcpy(&repbody->oa, &body->oa, sizeof(body->oa));
        req->rq_status = obd_close(conn, &repbody->oa, NULL, oti);
        RETURN(0);
}

static int ost_create(struct ptlrpc_request *req, struct obd_trans_info *oti)
{
        struct lustre_handle *conn = (struct lustre_handle *)req->rq_reqmsg;
        struct ost_body *body, *repbody;
        int rc, size = sizeof(*repbody);
        ENTRY;

        body = lustre_swab_reqbuf (req, 0, sizeof (*body),
                                   lustre_swab_ost_body);
        if (body == NULL)
                RETURN (-EFAULT);

        rc = lustre_pack_msg(1, &size, NULL, &req->rq_replen, &req->rq_repmsg);
        if (rc)
                RETURN(rc);

        repbody = lustre_msg_buf (req->rq_repmsg, 0, sizeof (*repbody));
        memcpy(&repbody->oa, &body->oa, sizeof(body->oa));
        req->rq_status = obd_create(conn, &repbody->oa, NULL, oti);
        RETURN(0);
}

static int ost_punch(struct ptlrpc_request *req, struct obd_trans_info *oti)
{
        struct lustre_handle *conn = (struct lustre_handle *)req->rq_reqmsg;
        struct ost_body *body, *repbody;
        int rc, size = sizeof(*repbody);
        ENTRY;

        body = lustre_swab_reqbuf (req, 0, sizeof (*body),
                                   lustre_swab_ost_body);
        if (body == NULL)
                RETURN (-EFAULT);

        if ((body->oa.o_valid & (OBD_MD_FLSIZE | OBD_MD_FLBLOCKS)) !=
            (OBD_MD_FLSIZE | OBD_MD_FLBLOCKS))
                RETURN(-EINVAL);

        rc = lustre_pack_msg(1, &size, NULL, &req->rq_replen, &req->rq_repmsg);
        if (rc)
                RETURN(rc);

        repbody = lustre_msg_buf(req->rq_repmsg, 0, sizeof (*repbody));
        memcpy(&repbody->oa, &body->oa, sizeof(body->oa));
        req->rq_status = obd_punch(conn, &repbody->oa, NULL, repbody->oa.o_size,
                                   repbody->oa.o_blocks, oti);
        RETURN(0);
}

static int ost_setattr(struct ptlrpc_request *req, struct obd_trans_info *oti)
{
        struct lustre_handle *conn = &req->rq_reqmsg->handle;
        struct ost_body *body, *repbody;
        int rc, size = sizeof(*repbody);
        ENTRY;

        body = lustre_swab_reqbuf (req, 0, sizeof (*body),
                                   lustre_swab_ost_body);
        if (body == NULL)
                RETURN (-EFAULT);

        rc = lustre_pack_msg(1, &size, NULL, &req->rq_replen, &req->rq_repmsg);
        if (rc)
                RETURN(rc);

        repbody = lustre_msg_buf(req->rq_repmsg, 0, sizeof (*repbody));
        memcpy(&repbody->oa, &body->oa, sizeof(body->oa));

        req->rq_status = obd_setattr(conn, &repbody->oa, NULL, oti);
        RETURN(0);
}

static int ost_bulk_timeout(void *data)
{
        ENTRY;
        /* We don't fail the connection here, because having the export
         * killed makes the (vital) call to commitrw very sad.
         */
        RETURN(1);
}

static int get_per_page_niobufs (struct obd_ioobj *ioo, int nioo,
                                 struct niobuf_remote *rnb, int nrnb,
                                 struct niobuf_remote **pp_rnbp)
{
        /* Copy a remote niobuf, splitting it into page-sized chunks
         * and setting ioo[i].ioo_bufcnt accordingly */
        struct niobuf_remote *pp_rnb;
        int   i;
        int   j;
        int   page;
        int   rnbidx = 0;
        int   npages = 0;

        /* first count and check the number of pages required */
        for (i = 0; i < nioo; i++)
                for (j = 0; j < ioo->ioo_bufcnt; j++, rnbidx++) {
                        obd_off offset = rnb[rnbidx].offset;
                        obd_off p0 = offset >> PAGE_SHIFT;
                        obd_off pn = (offset + rnb[rnbidx].len - 1)>>PAGE_SHIFT;

                        LASSERT (rnbidx < nrnb);

                        npages += (pn + 1 - p0);

                        if (rnb[rnbidx].len == 0) {
                                CERROR("zero len BRW: obj %d objid "LPX64
                                       " buf %u\n", i, ioo[i].ioo_id, j);
                                return (-EINVAL);
                        }
                        if (j > 0 &&
                            rnb[rnbidx].offset <= rnb[rnbidx-1].offset) {
                                CERROR("unordered BRW: obj %d objid "LPX64
                                       " buf %u offset "LPX64" <= "LPX64"\n",
                                       i, ioo[i].ioo_id, j, rnb[rnbidx].offset,
                                       rnb[rnbidx].offset);
                                return (-EINVAL);
                        }
                }

        LASSERT (rnbidx == nrnb);

        if (npages == nrnb) {       /* all niobufs are for single pages */
                *pp_rnbp = rnb;
                return (npages);
        }

        OBD_ALLOC (pp_rnb, sizeof (*pp_rnb) * npages);
        if (pp_rnb == NULL)
                return (-ENOMEM);

        /* now do the actual split */
        page = rnbidx = 0;
        for (i = 0; i < nioo; i++) {
                int  obj_pages = 0;

                for (j = 0; j < ioo[i].ioo_bufcnt; j++, rnbidx++) {
                        obd_off off = rnb[rnbidx].offset;
                        int     nob = rnb[rnbidx].len;

                        LASSERT (rnbidx < nrnb);
                        do {
                                obd_off  poff = off & (PAGE_SIZE - 1);
                                int      pnob = (poff + nob > PAGE_SIZE) ?
                                                PAGE_SIZE - poff : nob;

                                LASSERT (page < npages);
                                pp_rnb[page].len = pnob;
                                pp_rnb[page].offset = off;
                                pp_rnb[page].flags = rnb->flags;

                                CDEBUG (D_PAGE, "   obj %d id "LPX64
                                        "page %d(%d) "LPX64" for %d\n",
                                        i, ioo[i].ioo_id, obj_pages, page,
                                        pp_rnb[page].offset, pp_rnb[page].len);
                                page++;
                                obj_pages++;

                                off += pnob;
                                nob -= pnob;
                        } while (nob > 0);
                        LASSERT (nob == 0);
                }
                ioo[i].ioo_bufcnt = obj_pages;
        }
        LASSERT (page == npages);

        *pp_rnbp = pp_rnb;
        return (npages);
}

static void free_per_page_niobufs (int npages, struct niobuf_remote *pp_rnb,
                                   struct niobuf_remote *rnb)
{
        if (pp_rnb == rnb)                      /* didn't allocate above */
                return;

        OBD_FREE (pp_rnb, sizeof (*pp_rnb) * npages);
}

#if CHECKSUM_BULK
__u64 ost_checksum_bulk (struct ptlrpc_bulk_desc *desc)
{
        __u64             cksum = 0;
        struct list_head *tmp;
        char             *ptr;

        list_for_each (tmp, &desc->bd_page_list) {
                struct ptlrpc_bulk_page *bp;

                bp = list_entry (tmp, struct ptlrpc_bulk_page, bp_link);
                ptr = kmap (bp->bp_page);
                ost_checksum (&cksum, ptr + bp->bp_pageoffset, bp->bp_buflen);
                kunmap (bp->bp_page);
        }
}
#endif

static int ost_brw_read(struct ptlrpc_request *req)
{
        struct ptlrpc_bulk_desc *desc;
        struct niobuf_remote    *remote_nb;
        struct niobuf_remote    *pp_rnb;
        struct niobuf_local     *local_nb;
        struct obd_ioobj        *ioo;
        struct ost_body         *body;
        struct l_wait_info       lwi;
        void                    *desc_priv = NULL;
        int                      size[1] = { sizeof(*body) };
        int                      comms_error = 0;
        int                      niocount;
        int                      npages;
        int                      nob = 0;
        int                      rc;
        int                      i;
        ENTRY;

        if (OBD_FAIL_CHECK(OBD_FAIL_OST_BRW_READ_BULK))
                GOTO(out, rc = -EIO);

        body = lustre_swab_reqbuf(req, 0, sizeof(*body), lustre_swab_ost_body);
        if (body == NULL) {
                CERROR ("Missing/short ost_body\n");
                GOTO (out, rc = -EFAULT);
        }

        ioo = lustre_swab_reqbuf (req, 1, sizeof (*ioo),
                                  lustre_swab_obd_ioobj);
        if (ioo == NULL) {
                CERROR ("Missing/short ioobj\n");
                GOTO (out, rc = -EFAULT);
        }

        niocount = ioo->ioo_bufcnt;
        remote_nb = lustre_swab_reqbuf(req, 2, niocount * sizeof (*remote_nb),
                                       lustre_swab_niobuf_remote);
        if (remote_nb == NULL) {
                CERROR ("Missing/short niobuf\n");
                GOTO (out, rc = -EFAULT);
        }
        if (lustre_msg_swabbed (req->rq_reqmsg)) { /* swab remaining niobufs */
                for (i = 1; i < niocount; i++)
                        lustre_swab_niobuf_remote (&remote_nb[i]);
        }

        rc = lustre_pack_msg(1, size, NULL, &req->rq_replen, &req->rq_repmsg);
        if (rc)
                GOTO(out, rc);

        /* CAVEAT EMPTOR this sets ioo->ioo_bufcnt to # pages */
        npages = get_per_page_niobufs (ioo, 1, remote_nb, niocount, &pp_rnb);
        if (npages < 0)
                GOTO(out, rc = npages);

        OBD_ALLOC(local_nb, sizeof(*local_nb) * npages);
        if (local_nb == NULL)
                GOTO(out_pp_rnb, rc = -ENOMEM);

        desc = ptlrpc_prep_bulk_exp (req, BULK_PUT_SOURCE, OST_BULK_PORTAL);
        if (desc == NULL)
                GOTO(out_local, rc = -ENOMEM);

        rc = obd_preprw(OBD_BRW_READ, req->rq_export, 1, ioo, npages,
                        pp_rnb, local_nb, &desc_priv, NULL);
        if (rc != 0)
                GOTO(out_bulk, rc);

        nob = 0;
        for (i = 0; i < npages; i++) {
                int page_rc = local_nb[i].rc;

                if (page_rc < 0) {              /* error */
                        rc = page_rc;
                        break;
                }

                LASSERT (page_rc <= pp_rnb[i].len);
                nob += page_rc;
                if (page_rc != 0) {             /* some data! */
                        LASSERT (local_nb[i].page != NULL);
                        rc = ptlrpc_prep_bulk_page(desc, local_nb[i].page,
                                                   pp_rnb[i].offset& ~PAGE_MASK,
                                                   page_rc);
                        if (rc != 0)
                                break;
                }

                if (page_rc != pp_rnb[i].len) { /* short read */
                        /* All subsequent pages should be 0 */
                        while (++i < npages)
                                LASSERT (local_nb[i].rc == 0);
                        break;
                }
        }

        if (rc == 0) {
                rc = ptlrpc_bulk_put(desc);
                if (rc == 0) {
                        lwi = LWI_TIMEOUT(obd_timeout * HZ, ost_bulk_timeout,
                                          desc);
                        rc = l_wait_event(desc->bd_waitq,
                                          ptlrpc_bulk_complete(desc), &lwi);
                        if (rc) {
                                LASSERT(rc == -ETIMEDOUT);
                                CERROR ("timeout waiting for bulk PUT\n");
                                ptlrpc_abort_bulk (desc);
                        }
                } else {
                        CERROR("ptlrpc_bulk_put failed RC: %d\n", rc);
		}
		comms_error = rc != 0;
        }

        /* Must commit after prep above in all cases */
        rc = obd_commitrw(OBD_BRW_READ, req->rq_export, 1, ioo, npages,
                          local_nb, desc_priv, NULL);

#if CHECKSUM_BULK
        if (rc == 0) {
                body = lustre_msg_buf(req->rq_repmsg, 0, sizeof (*body));
                body->oa.o_rdev = ost_checksum_bulk (desc);
                body->oa.o_valid |= OBD_MD_FLCKSUM;
        }
#endif

 out_bulk:
        ptlrpc_free_bulk (desc);
 out_local:
        OBD_FREE(local_nb, sizeof(*local_nb) * npages);
 out_pp_rnb:
        free_per_page_niobufs (npages, pp_rnb, remote_nb);
 out:
        LASSERT (rc <= 0);
        if (rc == 0) {
                req->rq_status = nob;
                ptlrpc_reply(req);
        } else if (!comms_error) {
                /* only reply if comms OK */
                req->rq_status = rc;
                ptlrpc_error(req);
        } else {
                if (req->rq_repmsg != NULL) {
                        /* reply out callback would free */
                        OBD_FREE (req->rq_repmsg, req->rq_replen);
                }
                CERROR("bulk IO comms error: evicting %s@%s nid "LPU64"\n",
                       req->rq_export->exp_client_uuid.uuid,
                       req->rq_connection->c_remote_uuid.uuid,
                       req->rq_connection->c_peer.peer_nid);
                ptlrpc_fail_export(req->rq_export);
        }

        RETURN(rc);
}

static int ost_brw_write(struct ptlrpc_request *req, struct obd_trans_info *oti)
{
        struct ptlrpc_bulk_desc *desc;
        struct niobuf_remote    *remote_nb;
        struct niobuf_remote    *pp_rnb;
        struct niobuf_local     *local_nb;
        struct obd_ioobj        *ioo;
        struct ost_body         *body;
        struct l_wait_info       lwi;
        void                    *desc_priv = NULL;
        __u32                   *rcs;
        int                      size[2] = { sizeof (*body) };
        int                      objcount, niocount, npages;
        int                      comms_error = 0;
        int                      rc, rc2, swab, i, j;
        ENTRY;

        if (OBD_FAIL_CHECK(OBD_FAIL_OST_BRW_WRITE_BULK))
                GOTO(out, rc = -EIO);

        /* pause before transaction has been started */
        OBD_FAIL_TIMEOUT(OBD_FAIL_OST_BRW_PAUSE_BULK | OBD_FAIL_ONCE, 
                         obd_timeout +1);

        swab = lustre_msg_swabbed (req->rq_reqmsg);
        body = lustre_swab_reqbuf (req, 0, sizeof (*body),
                                   lustre_swab_ost_body);
        if (body == NULL) {
                CERROR ("Missing/short ost_body\n");
                GOTO(out, rc = -EFAULT);
        }

        LASSERT_REQSWAB (req, 1);
        objcount = req->rq_reqmsg->buflens[1] / sizeof(*ioo);
        if (objcount == 0) {
                CERROR ("Missing/short ioobj\n");
                GOTO (out, rc = -EFAULT);
        }
        ioo = lustre_msg_buf (req->rq_reqmsg, 1, objcount * sizeof (*ioo));
        LASSERT (ioo != NULL);
        for (niocount = i = 0; i < objcount; i++) {
                if (swab)
                        lustre_swab_obd_ioobj (&ioo[i]);
                if (ioo[i].ioo_bufcnt == 0) {
                        CERROR ("ioo[%d] has zero bufcnt\n", i);
                        GOTO (out, rc = -EFAULT);
                }
                niocount += ioo[i].ioo_bufcnt;
        }

        remote_nb = lustre_swab_reqbuf(req, 2, niocount * sizeof (*remote_nb),
                                       lustre_swab_niobuf_remote);
        if (remote_nb == NULL) {
                CERROR ("Missing/short niobuf\n");
                GOTO(out, rc = -EFAULT);
        }
        if (swab) {                             /* swab the remaining niobufs */
                for (i = 1; i < niocount; i++)
                        lustre_swab_niobuf_remote (&remote_nb[i]);
        }

        size[1] = niocount * sizeof (*rcs);
        rc = lustre_pack_msg(2, size, NULL, &req->rq_replen,
                             &req->rq_repmsg);
        if (rc != 0)
                GOTO (out, rc);
        rcs = lustre_msg_buf (req->rq_repmsg, 1, niocount * sizeof (*rcs));

        /* CAVEAT EMPTOR this sets ioo->ioo_bufcnt to # pages */
        npages = get_per_page_niobufs(ioo, objcount,remote_nb,niocount,&pp_rnb);
        if (npages < 0)
                GOTO (out, rc = npages);

        OBD_ALLOC(local_nb, sizeof(*local_nb) * npages);
        if (local_nb == NULL)
                GOTO(out_pp_rnb, rc = -ENOMEM);

        desc = ptlrpc_prep_bulk_exp (req, BULK_GET_SINK, OST_BULK_PORTAL);
        if (desc == NULL)
                GOTO(out_local, rc = -ENOMEM);

        rc = obd_preprw(OBD_BRW_WRITE, req->rq_export, objcount, ioo,
                        npages, pp_rnb, local_nb, &desc_priv, oti);
        if (rc != 0)
                GOTO (out_bulk, rc);

        /* NB Having prepped, we must commit... */

        for (i = 0; i < npages; i++) {
                rc = ptlrpc_prep_bulk_page(desc, local_nb[i].page,
                                           pp_rnb[i].offset & (PAGE_SIZE - 1),
                                           pp_rnb[i].len);
                if (rc != 0)
                        break;
        }

        if (rc == 0) {
                rc = ptlrpc_bulk_get(desc);
                if (rc == 0) {
                        lwi = LWI_TIMEOUT(obd_timeout * HZ, ost_bulk_timeout,
                                          desc);
                        rc = l_wait_event(desc->bd_waitq,
                                          ptlrpc_bulk_complete(desc), &lwi);
                        if (rc) {
                                LASSERT(rc == -ETIMEDOUT);
                                CERROR ("timeout waiting for bulk GET\n");
                                ptlrpc_abort_bulk (desc);
                        }
                } else {
			CERROR("ptlrpc_bulk_get failed RC: %d\n", rc);
		}
		comms_error = rc != 0;
        }

#if CHECKSUM_BULK
        if (rc == 0 && (body->oa.o_valid & OBD_MD_FLCKSUM) != 0) {
                static int cksum_counter;
                __u64 client_cksum = body->oa.o_rdev;
                __u64 cksum = ost_checksum_bulk (desc);

                if (client_cksum != cksum) {
                        CERROR("Bad checksum: client "LPX64", server "LPX64
                               ", client NID "LPX64"\n", client_cksum, cksum,
                               req->rq_connection->c_peer.peer_nid);
                        cksum_counter = 1;
                } else {
                        cksum_counter++;
                        if ((cksum_counter & (-cksum_counter)) == cksum_counter)
                                CERROR("Checksum %d from "LPX64": "LPX64" OK\n",
                                        cksum_counter,
                                        req->rq_connection->c_peer.peer_nid,
                                        cksum);
                }
        }
#endif
        /* Must commit after prep above in all cases */
        rc2 = obd_commitrw(OBD_BRW_WRITE, req->rq_export, objcount, ioo,
                           npages, local_nb, desc_priv, oti);

        if (rc == 0) {
                /* set per-requested niobuf return codes */
                for (i = j = 0; i < niocount; i++) {
                        int nob = remote_nb[i].len;

                        rcs[i] = 0;
                        do {
                                LASSERT (j < npages);
                                if (local_nb[j].rc < 0)
                                        rcs[i] = local_nb[j].rc;
                                nob -= pp_rnb[j].len;
                                j++;
                        } while (nob > 0);
                        LASSERT (nob == 0);
                }
                LASSERT (j == npages);
        }
        if (rc == 0)
                rc = rc2;

 out_bulk:
        ptlrpc_free_bulk (desc);
 out_local:
        OBD_FREE(local_nb, sizeof(*local_nb) * npages);
 out_pp_rnb:
        free_per_page_niobufs (npages, pp_rnb, remote_nb);
 out:
        if (rc == 0) {
                oti_to_request(oti, req);
                rc = ptlrpc_reply(req);
        } else if (!comms_error) {
                /* Only reply if there was no comms problem with bulk */
                req->rq_status = rc;
                ptlrpc_error(req);
        } else {
                if (req->rq_repmsg != NULL) {
                        /* reply out callback would free */
                        OBD_FREE (req->rq_repmsg, req->rq_replen);
                }
                CERROR("bulk IO comms error: evicting %s@%s nid "LPU64"\n",
                       req->rq_export->exp_client_uuid.uuid,
                       req->rq_connection->c_remote_uuid.uuid,
                       req->rq_connection->c_peer.peer_nid);
                ptlrpc_fail_export(req->rq_export);
        }
        RETURN(rc);
}

static int ost_san_brw(struct ptlrpc_request *req, int cmd)
{
        struct lustre_handle *conn = &req->rq_reqmsg->handle;
        struct niobuf_remote *remote_nb, *res_nb;
        struct obd_ioobj *ioo;
        struct ost_body *body;
        int rc, i, j, objcount, niocount, size[2] = {sizeof(*body)};
        int n;
        int swab;
        ENTRY;

        /* XXX not set to use latest protocol */

        swab = lustre_msg_swabbed (req->rq_reqmsg);
        body = lustre_swab_reqbuf (req, 0, sizeof (*body),
                                   lustre_swab_ost_body);
        if (body == NULL) {
                CERROR ("Missing/short ost_body\n");
                GOTO (out, rc = -EFAULT);
        }

        ioo = lustre_swab_reqbuf(req, 1, sizeof (*ioo),
                                 lustre_swab_obd_ioobj);
        if (ioo == NULL) {
                CERROR ("Missing/short ioobj\n");
                GOTO (out, rc = -EFAULT);
        }
        objcount = req->rq_reqmsg->buflens[1] / sizeof(*ioo);
        niocount = ioo[0].ioo_bufcnt;
        for (i = 1; i < objcount; i++) {
                if (swab)
                        lustre_swab_obd_ioobj (&ioo[i]);
                niocount += ioo[i].ioo_bufcnt;
        }

        remote_nb = lustre_swab_reqbuf(req, 2, niocount * sizeof (*remote_nb),
                                       lustre_swab_niobuf_remote);
        if (remote_nb == NULL) {
                CERROR ("Missing/short niobuf\n");
                GOTO (out, rc = -EFAULT);
        }
        if (swab) {                             /* swab the remaining niobufs */
                for (i = 1; i < niocount; i++)
                        lustre_swab_niobuf_remote (&remote_nb[i]);
        }

        for (i = n = 0; i < objcount; i++) {
                for (j = 0; j < ioo[i].ioo_bufcnt; j++, n++) {
                        if (remote_nb[n].len == 0) {
                                CERROR("zero len BRW: objid "LPX64" buf %u\n",
                                       ioo[i].ioo_id, j);
                                GOTO(out, rc = -EINVAL);
                        }
                        if (j && remote_nb[n].offset <= remote_nb[n-1].offset) {
                                CERROR("unordered BRW: objid "LPX64
                                       " buf %u offset "LPX64" <= "LPX64"\n",
                                       ioo[i].ioo_id, j, remote_nb[n].offset,
                                       remote_nb[n-1].offset);
                                GOTO(out, rc = -EINVAL);
                        }
                }
        }

        size[1] = niocount * sizeof(*remote_nb);
        rc = lustre_pack_msg(2, size, NULL, &req->rq_replen, &req->rq_repmsg);
        if (rc)
                GOTO(out, rc);

        req->rq_status = obd_san_preprw(cmd, conn, objcount, ioo,
                                        niocount, remote_nb);

        if (req->rq_status)
                GOTO (out, rc = 0);

        res_nb = lustre_msg_buf(req->rq_repmsg, 1, size[1]);
        memcpy (res_nb, remote_nb, size[1]);
        rc = 0;
out:
        if (rc) {
                OBD_FREE(req->rq_repmsg, req->rq_replen);
                req->rq_repmsg = NULL;
                req->rq_status = rc;
                ptlrpc_error(req);
        } else
                ptlrpc_reply(req);

        return rc;
}

static int filter_recovery_request(struct ptlrpc_request *req,
                                   struct obd_device *obd, int *process)
{
        switch (req->rq_reqmsg->opc) {
        case OST_CONNECT: /* This will never get here, but for completeness. */
        case OST_DISCONNECT:
               *process = 1;
               RETURN(0);

        case OBD_PING:
        case OST_CLOSE:
        case OST_CREATE:
        case OST_DESTROY:
        case OST_OPEN:
        case OST_PUNCH:
        case OST_SETATTR: 
        case OST_SYNCFS:
        case OST_WRITE:
        case LDLM_ENQUEUE:
                *process = target_queue_recovery_request(req, obd);
                RETURN(0);

        default:
                DEBUG_REQ(D_ERROR, req, "not permitted during recovery");
                *process = 0;
                /* XXX what should we set rq_status to here? */
                req->rq_status = -EAGAIN;
                RETURN(ptlrpc_error(req));
        }
}



static int ost_handle(struct ptlrpc_request *req)
{
        struct obd_trans_info trans_info = { 0, }, *oti = &trans_info;
        int should_process, fail = OBD_FAIL_OST_ALL_REPLY_NET, rc = 0;
        ENTRY;

        /* XXX identical to MDS */
        if (req->rq_reqmsg->opc != OST_CONNECT) {
                struct obd_device *obd;
                int abort_recovery, recovering;

                if (req->rq_export == NULL) {
                        CERROR("lustre_ost: operation %d on unconnected OST\n",
                               req->rq_reqmsg->opc);
                        req->rq_status = -ENOTCONN;
                        GOTO(out, rc = -ENOTCONN);
                }

                obd = req->rq_export->exp_obd;

                /* Check for aborted recovery. */
                spin_lock_bh(&obd->obd_processing_task_lock);
                abort_recovery = obd->obd_abort_recovery;
                recovering = obd->obd_recovering;
                spin_unlock_bh(&obd->obd_processing_task_lock);
                if (abort_recovery) {
                        target_abort_recovery(obd);
                } else if (recovering) {
                        rc = filter_recovery_request(req, obd, &should_process);
                        if (rc || !should_process)
                                RETURN(rc);
                }
        } 

        if (strcmp(req->rq_obd->obd_type->typ_name, "ost") != 0)
                GOTO(out, rc = -EINVAL);

        switch (req->rq_reqmsg->opc) {
        case OST_CONNECT:
                CDEBUG(D_INODE, "connect\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_CONNECT_NET, 0);
                rc = target_handle_connect(req, ost_handle);
                break;
        case OST_DISCONNECT:
                CDEBUG(D_INODE, "disconnect\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_DISCONNECT_NET, 0);
                rc = target_handle_disconnect(req);
                break;
        case OST_CREATE:
                CDEBUG(D_INODE, "create\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_CREATE_NET, 0);
                rc = ost_create(req, oti);
                break;
        case OST_DESTROY:
                CDEBUG(D_INODE, "destroy\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_DESTROY_NET, 0);
                rc = ost_destroy(req, oti);
                break;
        case OST_GETATTR:
                CDEBUG(D_INODE, "getattr\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_GETATTR_NET, 0);
                rc = ost_getattr(req);
                break;
        case OST_SETATTR:
                CDEBUG(D_INODE, "setattr\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_SETATTR_NET, 0);
                rc = ost_setattr(req, oti);
                break;
        case OST_OPEN:
                CDEBUG(D_INODE, "open\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_OPEN_NET, 0);
                rc = ost_open(req, oti);
                break;
        case OST_CLOSE:
                CDEBUG(D_INODE, "close\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_CLOSE_NET, 0);
                rc = ost_close(req, oti);
                break;
        case OST_WRITE:
                CDEBUG(D_INODE, "write\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_BRW_NET, 0);
                rc = ost_brw_write(req, oti);
                /* ost_brw sends its own replies */
                RETURN(rc);
        case OST_READ:
                CDEBUG(D_INODE, "read\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_BRW_NET, 0);
                rc = ost_brw_read(req);
                /* ost_brw sends its own replies */
                RETURN(rc);
        case OST_SAN_READ:
                CDEBUG(D_INODE, "san read\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_BRW_NET, 0);
                rc = ost_san_brw(req, OBD_BRW_READ);
                /* ost_san_brw sends its own replies */
                RETURN(rc);
        case OST_SAN_WRITE:
                CDEBUG(D_INODE, "san write\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_BRW_NET, 0);
                rc = ost_san_brw(req, OBD_BRW_WRITE);
                /* ost_san_brw sends its own replies */
                RETURN(rc);
        case OST_PUNCH:
                CDEBUG(D_INODE, "punch\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_PUNCH_NET, 0);
                rc = ost_punch(req, oti);
                break;
        case OST_STATFS:
                CDEBUG(D_INODE, "statfs\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_STATFS_NET, 0);
                rc = ost_statfs(req);
                break;
        case OST_SYNCFS:
                CDEBUG(D_INODE, "sync\n");
                OBD_FAIL_RETURN(OBD_FAIL_OST_SYNCFS_NET, 0);
                rc = ost_syncfs(req);
                break;
        case OBD_PING:
                DEBUG_REQ(D_INODE, req, "ping");
                rc = target_handle_ping(req);
                break;
        case LDLM_ENQUEUE:
                CDEBUG(D_INODE, "enqueue\n");
                OBD_FAIL_RETURN(OBD_FAIL_LDLM_ENQUEUE, 0);
                rc = ldlm_handle_enqueue(req, ldlm_server_completion_ast,
                                         ldlm_server_blocking_ast);
                fail = OBD_FAIL_OST_LDLM_REPLY_NET;
                break;
        case LDLM_CONVERT:
                CDEBUG(D_INODE, "convert\n");
                OBD_FAIL_RETURN(OBD_FAIL_LDLM_CONVERT, 0);
                rc = ldlm_handle_convert(req);
                break;
        case LDLM_CANCEL:
                CDEBUG(D_INODE, "cancel\n");
                OBD_FAIL_RETURN(OBD_FAIL_LDLM_CANCEL, 0);
                rc = ldlm_handle_cancel(req);
                break;
        case LDLM_BL_CALLBACK:
        case LDLM_CP_CALLBACK:
                CDEBUG(D_INODE, "callback\n");
                CERROR("callbacks should not happen on OST\n");
                /* fall through */
        default:
                CERROR("Unexpected opcode %d\n", req->rq_reqmsg->opc);
                req->rq_status = -ENOTSUPP;
                rc = ptlrpc_error(req);
                RETURN(rc);
        }

        EXIT;
        /* If we're DISCONNECTing, the export_data is already freed */
        if (!rc && req->rq_reqmsg->opc != OST_DISCONNECT) {
                struct obd_device *obd  = req->rq_export->exp_obd;
                if (!obd->obd_no_transno) {
                        req->rq_repmsg->last_committed =
                                obd->obd_last_committed;
                } else {
                        DEBUG_REQ(D_IOCTL, req,
                                  "not sending last_committed update");
                }
                CDEBUG(D_INFO, "last_committed "LPU64", xid "LPX64"\n",
                       obd->obd_last_committed, req->rq_xid);
        }

out:
        if (lustre_msg_get_flags(req->rq_reqmsg) & MSG_LAST_REPLAY) {
                struct obd_device *obd = req->rq_export->exp_obd;

                if (obd && obd->obd_recovering) {
                        DEBUG_REQ(D_HA, req, "LAST_REPLAY, queuing reply");
                        return target_queue_final_reply(req, rc);
                }
                /* Lost a race with recovery; let the error path DTRT. */
                rc = req->rq_status = -ENOTCONN;
        }

        if (!rc)
                oti_to_request(oti, req);

        target_send_reply(req, rc, fail);
        return 0;
}

static int ost_setup(struct obd_device *obddev, obd_count len, void *buf)
{
        struct ost_obd *ost = &obddev->u.ost;
        int err;
        int i;
        ENTRY;

        ost->ost_service = ptlrpc_init_svc(OST_NEVENTS, OST_NBUFS,
                                           OST_BUFSIZE, OST_MAXREQSIZE,
                                           OST_REQUEST_PORTAL, OSC_REPLY_PORTAL,
                                           ost_handle, "ost", obddev);
        if (!ost->ost_service) {
                CERROR("failed to start service\n");
                GOTO(error_disc, err = -ENOMEM);
        }

        for (i = 0; i < OST_NUM_THREADS; i++) {
                char name[32];
                sprintf(name, "ll_ost_%02d", i);
                err = ptlrpc_start_thread(obddev, ost->ost_service, name);
                if (err) {
                        CERROR("error starting thread #%d: rc %d\n", i, err);
                        GOTO(error_disc, err = -EINVAL);
                }
        }

        RETURN(0);

error_disc:
        RETURN(err);
}

static int ost_cleanup(struct obd_device *obddev, int force, int failover)
{
        struct ost_obd *ost = &obddev->u.ost;
        int err = 0;
        ENTRY;

        if (obddev->obd_recovering)
                target_cancel_recovery_timer(obddev);

        ptlrpc_stop_all_threads(ost->ost_service);
        ptlrpc_unregister_service(ost->ost_service);

        RETURN(err);
}

int ost_attach(struct obd_device *dev, obd_count len, void *data)
{
        struct lprocfs_static_vars lvars;

        lprocfs_init_vars(&lvars);
        return lprocfs_obd_attach(dev, lvars.obd_vars);
}

int ost_detach(struct obd_device *dev)
{
        return lprocfs_obd_detach(dev);
}

/* I don't think this function is ever used, since nothing 
 * connects directly to this module.
 */
static int ost_connect(struct lustre_handle *conn,
                       struct obd_device *obd, struct obd_uuid *cluuid)
{
        struct obd_export *exp;
        int rc;
        ENTRY;

        if (!conn || !obd || !cluuid)
                RETURN(-EINVAL);

        rc = class_connect(conn, obd, cluuid);
        if (rc)
                RETURN(rc);
        exp = class_conn2export(conn);
        LASSERT(exp);
        class_export_put(exp);

        RETURN(0);
}

/* use obd ops to offer management infrastructure */
static struct obd_ops ost_obd_ops = {
        o_owner:        THIS_MODULE,
        o_attach:       ost_attach,
        o_detach:       ost_detach,
        o_setup:        ost_setup,
        o_cleanup:      ost_cleanup,
        o_connect:      ost_connect,
};

static int __init ost_init(void)
{
        struct lprocfs_static_vars lvars;
        ENTRY;

        lprocfs_init_vars(&lvars);
        RETURN(class_register_type(&ost_obd_ops, lvars.module_vars,
                                   LUSTRE_OST_NAME));
}

static void __exit ost_exit(void)
{
        class_unregister_type(LUSTRE_OST_NAME);
}

MODULE_AUTHOR("Cluster File Systems, Inc. <info@clusterfs.com>");
MODULE_DESCRIPTION("Lustre Object Storage Target (OST) v0.01");
MODULE_LICENSE("GPL");

module_init(ost_init);
module_exit(ost_exit);
