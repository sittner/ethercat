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
 * struct pal_irq_work - low-latency deferred work
 */
struct pal_irq_work {
    irq_work_func_t func;           /* Work function */
    atomic_int flags;               /* State flags (lock-free) */
    struct pal_irq_work *next;          /* Next in queue (lock-free list) */
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
 * init_irq_work - initialize an irq_work item
 * @work: work item to initialize
 * @func: function to execute
 */
static inline void init_irq_work(ec_irq_work_t *work, irq_work_func_t func)
{
    work->func = func;
    atomic_init(&work->flags, 0);
    work->next = NULL;
}

/**
 * irq_work_busy - check if work is pending or running
 * @work: work item to check
 */
static inline int irq_work_busy(ec_irq_work_t *work)
{
    return atomic_load(&work->flags) & (IRQ_WORK_PENDING | IRQ_WORK_BUSY);
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

        /* Wait for work or shutdown */
        while (atomic_load(&q->head) == 0 && !q->shutdown) {
            pthread_cond_wait(&q->cond, &q->lock);
        }

        if (q->shutdown && atomic_load(&q->head) == 0) {
            pthread_mutex_unlock(&q->lock);
            break;
        }

        pthread_mutex_unlock(&q->lock);

        /* Process all queued work (lock-free dequeue) */
        while ((head = atomic_exchange(&q->head, 0)) != 0) {
            /* Reverse the list to get FIFO order */
            ec_irq_work_t *reversed = NULL;
            work = (ec_irq_work_t *)head;
       
            while (work) {
                ec_irq_work_t *next = work->next;
                work->next = reversed;
                reversed = work;
                work = next;
            }

            /* Execute in FIFO order */
            work = reversed;
            while (work) {
                ec_irq_work_t *next = work->next;
                int flags;

                /* Mark as running, get previous flags */
                flags = atomic_fetch_and(&work->flags, ~IRQ_WORK_PENDING);

                /* Only execute if work was actually pending */
                if (flags & IRQ_WORK_PENDING) {
                    atomic_fetch_or(&work->flags, IRQ_WORK_BUSY);

                    /* Execute work function */
                    if (work->func)
                        work->func(work);

                    /* Mark as complete */
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
 *
 * Returns 0 on success, negative error code on failure
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

    /* Create high-priority worker thread */
    pthread_attr_init(&attr);

    /* Try to set real-time priority (may fail without privileges) */
    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO) == 0) {
        param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
        pthread_attr_setschedparam(&attr, &param);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

    if (pthread_create(&q->worker, &attr, __irq_work_worker, q) != 0) {
        /* Retry without real-time priority */
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
 * irq_work_queue - queue work for execution
 * @work: work item to queue
 *
 * Lock-free, safe to call from signal handlers
 * Returns 1 if queued, 0 if already pending
 */
static inline int irq_work_queue(ec_irq_work_t *work)
{
    struct pal_irq_work_queue *q = irq_work_queue_global;
    uintptr_t old_head;
    int old_flags;

    if (!q)
        return 0;

    /* Check if already pending (atomic) */
    old_flags = atomic_fetch_or(&work->flags, IRQ_WORK_PENDING);
    if (old_flags & IRQ_WORK_PENDING)
        return 0;  /* Already queued */

    /* Lock-free push to front of list */
    do {
        old_head = atomic_load(&q->head);
        work->next = (ec_irq_work_t *)old_head;
    } while (!atomic_compare_exchange_weak(&q->head, &old_head,
                                           (uintptr_t)work));

    /* Signal worker thread */
    pthread_mutex_lock(&q->lock);
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);

    return 1;
}

/**
 * irq_work_sync - wait for work to complete
 * @work: work item to wait for
 */
static inline void irq_work_sync(ec_irq_work_t *work)
{
    /* Spin until work is complete */
    while (atomic_load(&work->flags) & (IRQ_WORK_PENDING | IRQ_WORK_BUSY)) {
        sched_yield();
    }
}


#endif /* __EC_USPACE_PAL_WORK_H__ */

