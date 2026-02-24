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
   Platform Abstraction Layer - semaphore wrappers for kernel EtherCAT master.
*/

/****************************************************************************/

#ifndef __EC_KERNEL_PAL_SEM_H__
#define __EC_KERNEL_PAL_SEM_H__

/* ec_semaphore_t is typedef'd in pal.h as struct semaphore */

static inline void ec_sem_init(ec_semaphore_t *sem, int val)
{
    sema_init(sem, val);
}

static inline void ec_sem_down(ec_semaphore_t *sem)
{
    down(sem);
}

static inline int ec_sem_down_trylock(ec_semaphore_t *sem)
{
    return down_trylock(sem);
}

static inline void ec_sem_up(ec_semaphore_t *sem)
{
    up(sem);
}

static inline int ec_sem_down_interruptible(ec_semaphore_t *sem)
{
    return down_interruptible(sem);
}

#endif /* __EC_KERNEL_PAL_SEM_H__ */
