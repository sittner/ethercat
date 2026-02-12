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

/* Kernel compatibility name */
#define wait_queue_head_t ec_wait_queue_t

/**
 * init_waitqueue_head - initialize a wait queue
 * @wq: pointer to wait queue to initialize
 */
static inline void init_waitqueue_head(ec_wait_queue_t *wq)
{
    pthread_mutex_init(&wq->lock, NULL);
    pthread_cond_init(&wq->cond, NULL);
}

/**
 * wake_up - wake one waiting thread
 * @wq: pointer to wait queue
 */
static inline void wake_up(ec_wait_queue_t *wq)
{
    pthread_mutex_lock(&wq->lock);
    pthread_cond_signal(&wq->cond);
    pthread_mutex_unlock(&wq->lock);
}

/**
 * wake_up_all - wake all waiting threads
 * @wq: pointer to wait queue
 */
static inline void wake_up_all(ec_wait_queue_t *wq)
{
    pthread_mutex_lock(&wq->lock);
    pthread_cond_broadcast(&wq->cond);
    pthread_mutex_unlock(&wq->lock);
}

#define wake_up_interruptible(wq) wake_up(wq)

/**
 * wait_event - sleep until condition is true
 * @wq: wait queue (passed by VALUE, not pointer - kernel API!)
 * @condition: condition to wait for
 */
#define wait_event(wq, condition)                       \
    do {                                                \
        pthread_mutex_lock(&(wq).lock);                 \
        while (!(condition)) {                          \
            pthread_cond_wait(&(wq).cond, &(wq).lock);  \
        }                                               \
        pthread_mutex_unlock(&(wq).lock);               \
    } while (0)

/**
 * wait_event_interruptible - sleep until condition (interruptible)
 * @wq: wait queue (passed by VALUE)
 * @condition: condition to wait for
 *
 * Returns 0 if condition became true, -ERESTARTSYS on signal
 */
#define wait_event_interruptible(wq, condition)         \
    ({                                                  \
        int __ret = 0;                                  \
        pthread_mutex_lock(&(wq).lock);                 \
        while (!(condition)) {                          \
            pthread_cond_wait(&(wq).cond, &(wq).lock);  \
        }                                               \
        pthread_mutex_unlock(&(wq).lock);               \
        __ret;                                          \
    })

/**
 * wait_event_timeout - sleep until condition or timeout
 * @wq: wait queue (passed by VALUE)
 * @condition: condition to wait for
 * @timeout_jiffies: timeout in jiffies
 *
 * Returns remaining jiffies (>0) if condition met, 0 on timeout
 */
#define wait_event_timeout(wq, condition, timeout_jiffies)              \
    ({                                                                  \
        long __ret = (timeout_jiffies);                                 \
        struct timespec __ts;                                           \
        clock_gettime(CLOCK_REALTIME, &__ts);                           \
        __ts.tv_sec += (__ret) / HZ;                                    \
        __ts.tv_nsec += ((__ret) % HZ) * (1000000000L / HZ);            \
        if (__ts.tv_nsec >= 1000000000L) {                              \
            __ts.tv_sec++;                                              \
            __ts.tv_nsec -= 1000000000L;                                \
        }                                                               \
        pthread_mutex_lock(&(wq).lock);                                 \
        while (!(condition)) {                                          \
            if (pthread_cond_timedwait(&(wq).cond, &(wq).lock, &__ts)   \
                    == ETIMEDOUT) {                                     \
                __ret = 0;                                              \
                break;                                                  \
            }                                                           \
        }                                                               \
        pthread_mutex_unlock(&(wq).lock);                               \
        __ret;                                                          \
    })

#endif /* __EC_USPACE_PAL_QUEUE_H__ */

