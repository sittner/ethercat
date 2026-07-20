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

#ifndef __EC_USPACE_PAL_SEM_H__
#define __EC_USPACE_PAL_SEM_H__

#include <assert.h>

/* Semaphore type.
 *
 * The master core uses its semaphores exclusively in mutex style: every
 * ec_sem_down() is paired with an ec_sem_up() by the same thread in the
 * same function (audited across all call sites of master_sem,
 * device_sem, scan_sem, config_sem and ext_queue_sem; all are
 * initialized to 1). The userspace implementation is therefore a
 * pthread mutex with PTHREAD_PRIO_INHERIT — a plain sem_t has no owner
 * and thus no priority inheritance, so a low-priority holder (e.g. the
 * SCHED_OTHER master FSM thread) could block a realtime-priority waiter
 * unboundedly under a classic three-thread priority inversion.
 *
 * PTHREAD_MUTEX_ERRORCHECK is the safety net for the mutex-style
 * contract: an unlock from a non-owning thread (i.e. semaphore-style
 * signaling that the audit missed) fails with EPERM and trips the
 * assert instead of silently corrupting the lock state.
 */
typedef pthread_mutex_t ec_semaphore_t;

static inline void ec_sem_init(ec_semaphore_t *sem, int val)
{
    pthread_mutexattr_t attr;

    assert(val == 1); /* mutex-style only: initialized unlocked */
    (void) val;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(sem, &attr);
    pthread_mutexattr_destroy(&attr);
}

static inline void ec_sem_down(ec_semaphore_t *sem)
{
    int ret = pthread_mutex_lock(sem);

    assert(ret == 0); /* EDEADLK = relock attempt by the owner */
    (void) ret;
}

/**
 * ec_sem_down_trylock - try to acquire without blocking
 *
 * Returns 0 if acquired, 1 if not (note: opposite of sem_trywait!)
 *
 * TRUSTED: pthread_mutex_trylock() never blocks by contract; the
 * function-effects analysis cannot see that. Used from the cyclic path
 * (ecrt_master_send_ext()).
 */
static inline int ec_sem_down_trylock(ec_semaphore_t *sem) EC_RT_ATTR;
EC_RT_TRUSTED_BEGIN
static inline int ec_sem_down_trylock(ec_semaphore_t *sem)
{
    return (pthread_mutex_trylock(sem) == 0) ? 0 : 1;
}
EC_RT_TRUSTED_END

/**
 * ec_sem_down_interruptible - acquire semaphore, interruptible
 *
 * Returns 0 on success, -EINTR if interrupted by signal. The userspace
 * library performs no signal-based interruption, so this maps to a
 * plain lock.
 */
static inline int ec_sem_down_interruptible(ec_semaphore_t *sem)
{
    ec_sem_down(sem);
    return 0;
}

/* TRUSTED: unlocking a PI mutex is nonblocking (bounded futex wake);
 * the function-effects analysis cannot see that. Reachable from the
 * cyclic path only after a successful trylock. */
static inline void ec_sem_up(ec_semaphore_t *sem) EC_RT_ATTR;
EC_RT_TRUSTED_BEGIN
static inline void ec_sem_up(ec_semaphore_t *sem)
{
    int ret = pthread_mutex_unlock(sem);

    assert(ret == 0); /* EPERM = unlock by a non-owning thread */
    (void) ret;
}
EC_RT_TRUSTED_END

#endif /* __EC_USPACE_PAL_SEM_H__ */

