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

#ifndef __EC_USPACE_PAL_WORK_H__
#define __EC_USPACE_PAL_WORK_H__

/* Forward declaration */
struct pal_work_struct;

/* Work function typedef */
typedef void (*ec_work_func_t)(struct pal_work_struct *work);

/* Work states */
#define EC_WORK_PENDING     (1 << 0)
#define EC_WORK_RUNNING     (1 << 1)

/**
 * ec_work_t - deferred work item
 */
struct pal_work_struct {
    ec_work_func_t func;            /* Work function */
    volatile unsigned long flags;   /* State flags */
    struct pal_work_struct *next;   /* Next in queue (linked list) */
};

typedef struct pal_work_struct ec_work_t;

/**
 * struct ec_workqueue - workqueue with worker thread
 */
struct ec_workqueue {
    pthread_t worker;               /* Worker thread */
    pthread_mutex_t lock;           /* Protects queue */
    pthread_cond_t cond;            /* Signal new work */
    ec_work_t *head;                /* Queue head */
    ec_work_t *tail;                /* Queue tail */
    volatile int shutdown;          /* Shutdown flag */
    char name[32];                  /* Workqueue name */
};

typedef struct ec_workqueue ec_workqueue_t;

/* Global system workqueue */
extern ec_workqueue_t *ec_system_wq;

/**
 * ec_work_init - initialize a work item
 * @_work: work struct to initialize
 * @_func: function to execute
 */
#define ec_work_init(_work, _func)                  \
    do {                                            \
        (_work)->func = (_func);                    \
        (_work)->flags = 0;                         \
        (_work)->next = NULL;                       \
    } while (0)

/* Internal: worker thread function */
static inline void *__workqueue_worker(void *arg)
{
    ec_workqueue_t *wq = (ec_workqueue_t *)arg;
    ec_work_t *work;

    pthread_setname_np(pthread_self(), wq->name);

    while (1) {
        pthread_mutex_lock(&wq->lock);

        while (!wq->head && !wq->shutdown) {
            pthread_cond_wait(&wq->cond, &wq->lock);
        }

        if (wq->shutdown && !wq->head) {
            pthread_mutex_unlock(&wq->lock);
            break;
        }

        work = wq->head;
        if (work) {
            wq->head = work->next;
            if (!wq->head)
                wq->tail = NULL;
            work->next = NULL;
            work->flags &= ~EC_WORK_PENDING;
            work->flags |= EC_WORK_RUNNING;
        }

        pthread_mutex_unlock(&wq->lock);

        if (work && work->func) {
            work->func(work);
            work->flags &= ~EC_WORK_RUNNING;
        }
    }

    return NULL;
}

/**
 * ec_wq_create - create a new workqueue
 */
static inline ec_workqueue_t *ec_wq_create(const char *name)
{
    ec_workqueue_t *wq;

    wq = (ec_workqueue_t *)malloc(sizeof(*wq));
    if (!wq)
        return NULL;

    memset(wq, 0, sizeof(*wq));
    strncpy(wq->name, name, sizeof(wq->name) - 1);

    pthread_mutex_init(&wq->lock, NULL);
    pthread_cond_init(&wq->cond, NULL);
    wq->head = NULL;
    wq->tail = NULL;
    wq->shutdown = 0;

    if (pthread_create(&wq->worker, NULL, __workqueue_worker, wq) != 0) {
        pthread_mutex_destroy(&wq->lock);
        pthread_cond_destroy(&wq->cond);
        free(wq);
        return NULL;
    }

    return wq;
}

/**
 * ec_wq_destroy - destroy a workqueue
 */
static inline void ec_wq_destroy(ec_workqueue_t *wq)
{
    if (!wq)
        return;

    pthread_mutex_lock(&wq->lock);
    wq->shutdown = 1;
    pthread_cond_signal(&wq->cond);
    pthread_mutex_unlock(&wq->lock);

    pthread_join(wq->worker, NULL);

    pthread_mutex_destroy(&wq->lock);
    pthread_cond_destroy(&wq->cond);
    free(wq);
}

/**
 * ec_work_queue - queue work to a workqueue
 */
static inline int ec_work_queue(ec_workqueue_t *wq, ec_work_t *work)
{
    int ret = 0;

    pthread_mutex_lock(&wq->lock);

    if (!(work->flags & EC_WORK_PENDING)) {
        work->flags |= EC_WORK_PENDING;
        work->next = NULL;

        if (wq->tail) {
            wq->tail->next = work;
            wq->tail = work;
        } else {
            wq->head = wq->tail = work;
        }

        pthread_cond_signal(&wq->cond);
        ret = 1;
    }

    pthread_mutex_unlock(&wq->lock);
    return ret;
}

/**
 * ec_work_schedule - queue work to system workqueue
 */
static inline int ec_work_schedule(ec_work_t *work)
{
    return ec_work_queue(ec_system_wq, work);
}

/**
 * ec_work_cancel - cancel work and wait for completion
 */
static inline int ec_work_cancel(ec_work_t *work)
{
    int was_pending;

    was_pending = (work->flags & EC_WORK_PENDING) ? 1 : 0;

    while (work->flags & EC_WORK_RUNNING) {
        sched_yield();
    }

    work->flags &= ~EC_WORK_PENDING;

    return was_pending;
}

/**
 * ec_wq_flush - flush all pending work in a workqueue
 */
static inline void ec_wq_flush(ec_workqueue_t *wq)
{
    /* Signal and wait for queue to drain */
    pthread_mutex_lock(&wq->lock);
    while (wq->head) {
        pthread_mutex_unlock(&wq->lock);
        sched_yield();
        pthread_mutex_lock(&wq->lock);
    }
    pthread_mutex_unlock(&wq->lock);
}
#endif /* __EC_USPACE_PAL_WORK_H__ */

