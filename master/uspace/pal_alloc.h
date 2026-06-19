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
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

/****************************************************************************/

static inline void *ec_alloc(size_t size)
{
    return malloc(size);
}

static inline void *ec_alloc_atomic(size_t size)
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
/* RT-hardened allocation: prefault all pages + mlock to prevent swapping.  */
/* Use for memory accessed from the real-time cyclic path.                  */
/****************************************************************************/

static inline void ec_rt_lock_mem(void *p, size_t size)
{
    long pagesize = sysconf(_SC_PAGESIZE);
    volatile char *c = (volatile char *)p;
    volatile char dummy;

    /* Pre-fault all pages (read + write) */
    for (size_t i = 0; i < size; i += (size_t)pagesize) {
        dummy = c[i];
        c[i] = dummy;
    }
    if (size > 0 && (size % (size_t)pagesize) != 0) {
        dummy = c[size - 1];
        c[size - 1] = dummy;
    }
    (void)dummy;

    mlock(p, size);
}

static inline void ec_rt_unlock_mem(void *p, size_t size)
{
    if (p)
        munlock(p, size);
}

static inline void *ec_rt_alloc(size_t size)
{
    void *p = malloc(size);
    if (!p)
        return NULL;
    ec_rt_lock_mem(p, size);
    return p;
}

static inline void *ec_rt_zalloc(size_t size)
{
    void *p = ec_rt_alloc(size);
    if (!p)
        return NULL;
    memset(p, 0, size);
    return p;
}

static inline void ec_rt_free(void *ptr, size_t size)
{
    if (!ptr)
        return;
    ec_rt_unlock_mem(ptr, size);
    free(ptr);
}

/****************************************************************************/

#endif /* __EC_USPACE_PAL_ALLOC_H__ */
