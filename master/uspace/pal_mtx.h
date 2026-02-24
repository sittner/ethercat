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
   Platform Abstraction Layer for userspace EtherCAT master.
*/

/****************************************************************************/

#ifndef __EC_USPACE_PAL_MTX_H__
#define __EC_USPACE_PAL_MTX_H__

/**
 * ec_rt_mutex_t - real-time mutex for userspace
 *
 * Uses PTHREAD_PRIO_INHERIT to enable priority inheritance
 */
typedef struct {
    pthread_mutex_t mutex;
} ec_rt_mutex_t;

static inline void ec_mutex_init(ec_rt_mutex_t *lock)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&lock->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

static inline void ec_mutex_lock(ec_rt_mutex_t *lock)
{
    pthread_mutex_lock(&lock->mutex);
}

static inline void ec_mutex_unlock(ec_rt_mutex_t *lock)
{
    pthread_mutex_unlock(&lock->mutex);
}

static inline void ec_mutex_destroy(ec_rt_mutex_t *lock)
{
    pthread_mutex_destroy(&lock->mutex);
}

/**
 * ec_rt_lock_interruptible - acquire mutex, interruptible
 *
 * Returns 0 on success, -EINTR if interrupted
 */
static inline int rt_mutex_lock_interruptible(ec_rt_mutex_t *lock)
{
    int ret = pthread_mutex_lock(&lock->mutex);
    if (ret == EINTR)
        return -EINTR;
    return ret ? -ret : 0;
}

#define ec_rt_lock_interruptible(lock) rt_mutex_lock_interruptible(lock)

#endif /* __EC_USPACE_PAL_MTX_H__ */
