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

#ifndef __EC_USPACE_PAL_QUEUE_H__
#define __EC_USPACE_PAL_QUEUE_H__

/* Wait queue structure for userspace */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
} ec_wait_queue_t;

static inline void ec_wq_init(ec_wait_queue_t *wq)
{
    pthread_mutex_init(&wq->lock, NULL);
    pthread_cond_init(&wq->cond, NULL);
}

static inline void ec_wq_wake(ec_wait_queue_t *wq)
{
    pthread_mutex_lock(&wq->lock);
    pthread_cond_signal(&wq->cond);
    pthread_mutex_unlock(&wq->lock);
}

static inline void ec_wq_wake_interruptible(ec_wait_queue_t *wq)
{
    ec_wq_wake(wq);
}

static inline void ec_wq_wake_all(ec_wait_queue_t *wq)
{
    pthread_mutex_lock(&wq->lock);
    pthread_cond_broadcast(&wq->cond);
    pthread_mutex_unlock(&wq->lock);
}

/**
 * ec_wq_wait - sleep until condition is true
 * @wq: wait queue (passed by VALUE, not pointer - matches kernel API)
 * @condition: condition to wait for
 */
#define ec_wq_wait(wq, condition)                        \
    do {                                                 \
        pthread_mutex_lock(&(wq).lock);                  \
        while (!(condition)) {                           \
            pthread_cond_wait(&(wq).cond, &(wq).lock);  \
        }                                                \
        pthread_mutex_unlock(&(wq).lock);                \
    } while (0)

/**
 * ec_wq_wait_interruptible - sleep until condition (interruptible)
 * @wq: wait queue (passed by VALUE)
 * @condition: condition to wait for
 *
 * Returns 0 if condition became true, -ERESTARTSYS on signal
 */
#define ec_wq_wait_interruptible(wq, condition)          \
    ({                                                   \
        int __ret = 0;                                   \
        pthread_mutex_lock(&(wq).lock);                  \
        while (!(condition)) {                           \
            pthread_cond_wait(&(wq).cond, &(wq).lock);  \
        }                                                \
        pthread_mutex_unlock(&(wq).lock);                \
        __ret;                                           \
    })

#endif /* __EC_USPACE_PAL_QUEUE_H__ */

