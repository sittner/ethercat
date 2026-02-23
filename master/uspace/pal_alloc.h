/*****************************************************************************
 *
 *  Copyright (C) 2006-2024  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 *  The IgH EtherCAT Master is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 *  Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the IgH EtherCAT Master; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 ****************************************************************************/

/**
   \file
   Platform Abstraction Layer - Memory allocation for userspace.
*/

/****************************************************************************/

#ifndef __EC_USPACE_PAL_ALLOC_H__
#define __EC_USPACE_PAL_ALLOC_H__

#include <stdlib.h>

/****************************************************************************/

static inline void *ec_alloc(size_t size)
{
    return malloc(size);
}

static inline void *ec_zalloc(size_t size)
{
    return calloc(1, size);
}

static inline void ec_free(void *ptr)
{
    free(ptr);
}

static inline void *ec_valloc(size_t size)
{
    return malloc(size);
}

static inline void ec_vfree(void *ptr)
{
    free(ptr);
}

/****************************************************************************/

#endif /* __EC_USPACE_PAL_ALLOC_H__ */
