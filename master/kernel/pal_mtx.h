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
   Platform Abstraction Layer - RT mutex wrappers for kernel EtherCAT master.
*/

/****************************************************************************/

#ifndef __EC_KERNEL_PAL_MTX_H__
#define __EC_KERNEL_PAL_MTX_H__

/* ec_rt_mutex_t is typedef'd in pal.h as struct rt_mutex */

static inline void ec_mutex_init(ec_rt_mutex_t *lock)
{
    rt_mutex_init(lock);
}

static inline void ec_mutex_lock(ec_rt_mutex_t *lock)
{
    rt_mutex_lock(lock);
}

static inline void ec_mutex_unlock(ec_rt_mutex_t *lock)
{
    rt_mutex_unlock(lock);
}

static inline void ec_mutex_destroy(ec_rt_mutex_t *lock)
{
    (void)lock;
}

/* ec_rt_lock_interruptible is already defined in pal.h */

#endif /* __EC_KERNEL_PAL_MTX_H__ */
