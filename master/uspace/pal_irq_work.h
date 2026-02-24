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

#ifndef __EC_USPACE_PAL_IRQ_WORK_H__
#define __EC_USPACE_PAL_IRQ_WORK_H__

/* Forward declaration */
struct pal_irq_work;

/* IRQ work function typedef */
typedef void (*irq_work_func_t)(struct pal_irq_work *work);

/* Work flags */
#define IRQ_WORK_PENDING    (1 << 0)
#define IRQ_WORK_BUSY       (1 << 1)

/**
 * ec_irq_work_t - low-latency deferred work
 */
struct pal_irq_work {
    irq_work_func_t func;           /* Work function */
    atomic_int flags;               /* State flags (lock-free) */
    struct pal_irq_work *next;      /* Next in queue (lock-free list) */
};

typedef struct pal_irq_work ec_irq_work_t;

/**
 * struct pal_irq_work_queue - queue for irq_work items
 */
struct pal_irq_work_queue {
    atomic_uintptr_t head;          /* Lock-free queue head */
    pthread_t worker;               /* High-priority worker thread */
    pthread_mutex_t lock;           /* For condition variable */
    pthread_cond_t cond;            /* Signal new work */
    volatile int shutdown;          /* Shutdown flag */
};

/* Global IRQ work queue */
extern struct pal_irq_work_queue *irq_work_queue_global;

/**
 * ec_irq_work_init - initialize an irq_work item
 */
static inline void ec_irq_work_init(ec_irq_work_t *work,
                                    irq_work_func_t func)
{
    work->func = func;
    atomic_init(&work->flags, 0);
    work->next = NULL;
}

/* Internal: worker thread function */
static inline void *__irq_work_worker(void *arg)
{
    struct pal_irq_work_queue *q = (struct pal_irq_work_queue *)arg;
    ec_irq_work_t *work;
    uintptr_t head;

    pthread_setname_np(pthread_self(), "irq_work");

    while (1) {
        pthread_mutex_lock(&q->lock);

        while (atomic_load(&q->head) == 0 && !q->shutdown) {
            pthread_cond_wait(&q->cond, &q->lock);
        }

        if (q->shutdown && atomic_load(&q->head) == 0) {
            pthread_mutex_unlock(&q->lock);
            break;
        }

        pthread_mutex_unlock(&q->lock);

        while ((head = atomic_exchange(&q->head, 0)) != 0) {
            ec_irq_work_t *reversed = NULL;
            work = (ec_irq_work_t *)head;

            while (work) {
                ec_irq_work_t *next = work->next;
                work->next = reversed;
                reversed = work;
                work = next;
            }

            work = reversed;
            while (work) {
                ec_irq_work_t *next = work->next;
                int flags;

                flags = atomic_fetch_and(&work->flags, ~IRQ_WORK_PENDING);

                if (flags & IRQ_WORK_PENDING) {
                    atomic_fetch_or(&work->flags, IRQ_WORK_BUSY);

                    if (work->func)
                        work->func(work);

                    atomic_fetch_and(&work->flags, ~IRQ_WORK_BUSY);
                }

                work = next;
            }
        }
    }

    return NULL;
}

/**
 * create_irq_work_queue - create the IRQ work queue
 */
static inline int create_irq_work_queue(void)
{
    struct pal_irq_work_queue *q;
    pthread_attr_t attr;
    struct sched_param param;

    q = (struct pal_irq_work_queue *)malloc(sizeof(*q));
    if (!q)
        return -ENOMEM;

    memset(q, 0, sizeof(*q));
    atomic_init(&q->head, 0);
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
    q->shutdown = 0;

    pthread_attr_init(&attr);

    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO) == 0) {
        param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
        pthread_attr_setschedparam(&attr, &param);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

    if (pthread_create(&q->worker, &attr, __irq_work_worker, q) != 0) {
        pthread_attr_destroy(&attr);
        pthread_attr_init(&attr);

        if (pthread_create(&q->worker, &attr, __irq_work_worker, q) != 0) {
            pthread_attr_destroy(&attr);
            pthread_mutex_destroy(&q->lock);
            pthread_cond_destroy(&q->cond);
            free(q);
            return -EAGAIN;
        }
    }

    pthread_attr_destroy(&attr);
    irq_work_queue_global = q;
    return 0;
}

/**
 * destroy_irq_work_queue - destroy the IRQ work queue
 */
static inline void destroy_irq_work_queue(void)
{
    struct pal_irq_work_queue *q = irq_work_queue_global;

    if (!q)
        return;

    pthread_mutex_lock(&q->lock);
    q->shutdown = 1;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);

    pthread_join(q->worker, NULL);

    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->cond);
    free(q);
    irq_work_queue_global = NULL;
}

/**
 * ec_irq_work_queue - queue an irq work item
 */
static inline void ec_irq_work_queue(ec_irq_work_t *work)
{
    struct pal_irq_work_queue *q = irq_work_queue_global;
    uintptr_t old_head;

    if (!q)
        return;

    atomic_fetch_or(&work->flags, IRQ_WORK_PENDING);

    do {
        old_head = atomic_load(&q->head);
        work->next = (ec_irq_work_t *)old_head;
    } while (!atomic_compare_exchange_weak(&q->head,
                                           &old_head,
                                           (uintptr_t)work));

    pthread_mutex_lock(&q->lock);
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
}

/**
 * irq_work_sync - wait for work to complete
 */
static inline void irq_work_sync(ec_irq_work_t *work)
{
    while (atomic_load(&work->flags) & (IRQ_WORK_PENDING | IRQ_WORK_BUSY)) {
        sched_yield();
    }
}


#endif /* __EC_USPACE_PAL_WORK_H__ */

