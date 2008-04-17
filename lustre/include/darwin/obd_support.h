/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  Copyright (C) 2001, 2002 Cluster File Systems, Inc.
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

#ifndef _DARWIN_OBD_SUPPORT
#define _DARWIN_OBD_SUPPORT

#ifndef _OBD_SUPPORT
#error Do not #include this file directly. #include <obd_support.h> instead
#endif

#include <darwin/lustre_compat.h>

#define OBD_SLEEP_ON(wq)        sleep_on(wq)

/* for obd_class.h */
# ifndef ERR_PTR
#  define ERR_PTR(a) ((void *)(a))
# endif

#endif
