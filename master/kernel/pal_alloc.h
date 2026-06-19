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
   Platform Abstraction Layer - Memory allocation for kernel.
*/

/****************************************************************************/

#ifndef __EC_KERNEL_PAL_ALLOC_H__
#define __EC_KERNEL_PAL_ALLOC_H__

#include <linux/slab.h>
#include <linux/vmalloc.h>

/****************************************************************************/

static inline void *ec_alloc(size_t size)
{
    return kmalloc(size, GFP_KERNEL);
}

static inline void *ec_alloc_atomic(size_t size)
{
    return kmalloc(size, GFP_ATOMIC);
}

static inline void *ec_zalloc(size_t size)
{
    return kzalloc(size, GFP_KERNEL);
}

static inline void ec_free(void *ptr)
{
    kfree(ptr);
}

static inline void *ec_valloc(size_t size)
{
    return vmalloc(size);
}

static inline void ec_vfree(void *ptr)
{
    vfree(ptr);
}

/****************************************************************************/
/* RT-hardened allocation: on kernel, all kmalloc memory is pinned.         */
/****************************************************************************/

static inline void *ec_rt_alloc(size_t size)
{
    return kmalloc(size, GFP_KERNEL);
}

static inline void *ec_rt_zalloc(size_t size)
{
    return kzalloc(size, GFP_KERNEL);
}

static inline void ec_rt_free(void *ptr, size_t size)
{
    (void)size;
    kfree(ptr);
}

static inline void ec_rt_lock_mem(void *p, size_t size)
{
    (void)p;
    (void)size;
}

static inline void ec_rt_unlock_mem(void *p, size_t size)
{
    (void)p;
    (void)size;
}

/****************************************************************************/

#endif /* __EC_KERNEL_PAL_ALLOC_H__ */
