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

/* Kernel semaphore compatibility */
typedef sem_t ec_semaphore_t;

/**
 * sema_init - initialize a semaphore
 * @sem: semaphore to initialize
 * @val: initial value
 */
static inline void sema_init(ec_semaphore_t *sem, int val)
{
    sem_init(sem, 0, val);  /* 0 = not shared between processes */
}

/**
 * down - acquire semaphore (may sleep)
 * @sem: semaphore to acquire
 *
 * Kernel down() cannot be interrupted. Use sem_wait().
 */
static inline void down(ec_semaphore_t *sem)
{
    sem_wait(sem);
}

/**
 * down_interruptible - acquire semaphore, interruptible
 * @sem: semaphore to acquire
 *
 * Returns 0 on success, -EINTR if interrupted by signal
 */
static inline int down_interruptible(ec_semaphore_t *sem)
{
    if (sem_wait(sem) == -1 && errno == EINTR)
        return -EINTR;
    return 0;
}

/**
 * down_trylock - try to acquire without blocking
 * @sem: semaphore to acquire
 *
 * Returns 0 if acquired, 1 if not (note: opposite of sem_trywait!)
 */
static inline int down_trylock(ec_semaphore_t *sem)
{
    return (sem_trywait(sem) == 0) ? 0 : 1;
}

/**
 * up - release semaphore
 * @sem: semaphore to release
 */
static inline void up(ec_semaphore_t *sem)
{
    sem_post(sem);
}

#endif /* __EC_USPACE_PAL_SEM_H__ */

