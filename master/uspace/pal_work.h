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
typedef void (*work_func_t)(struct pal_work_struct *work);

/* Work states */
#define WORK_STRUCT_PENDING     (1 << 0)
#define WORK_STRUCT_RUNNING     (1 << 1)

/**
 * struct pal_work_struct - deferred work item
 */
struct pal_work_struct {
    work_func_t func;               /* Work function */
    volatile unsigned long flags;   /* State flags */
    struct pal_work_struct *next;       /* Next in queue (linked list) */
};

typedef struct pal_work_struct ec_work_t;

/**
 * struct workqueue_struct - workqueue with worker thread
 */
struct workqueue_struct {
    pthread_t worker;               /* Worker thread */
    pthread_mutex_t lock;           /* Protects queue */
    pthread_cond_t cond;            /* Signal new work */
    ec_work_t *head;       /* Queue head */
    ec_work_t *tail;       /* Queue tail */
    volatile int shutdown;          /* Shutdown flag */
    char name[32];                  /* Workqueue name */
};

/* Global system workqueue */
extern struct workqueue_struct *system_wq;

/**
 * INIT_WORK - initialize a work item
 * @_work: work struct to initialize
 * @_func: function to execute
 */
#define INIT_WORK(_work, _func)                     \
    do {                                            \
        (_work)->func = (_func);                    \
        (_work)->flags = 0;                         \
        (_work)->next = NULL;                       \
    } while (0)



/* Internal: worker thread function */
static inline void *__workqueue_worker(void *arg)
{
    struct workqueue_struct *wq = (struct workqueue_struct *)arg;
    ec_work_t *work;

    pthread_setname_np(pthread_self(), wq->name);

    while (1) {
        pthread_mutex_lock(&wq->lock);

        /* Wait for work or shutdown */
        while (!wq->head && !wq->shutdown) {
            pthread_cond_wait(&wq->cond, &wq->lock);
        }

        if (wq->shutdown && !wq->head) {
            pthread_mutex_unlock(&wq->lock);
            break;
        }

        /* Dequeue work item */
        work = wq->head;
        if (work) {
            wq->head = work->next;
            if (!wq->head)
                wq->tail = NULL;
            work->next = NULL;
            work->flags &= ~WORK_STRUCT_PENDING;
            work->flags |= WORK_STRUCT_RUNNING;
        }

        pthread_mutex_unlock(&wq->lock);

        /* Execute work outside lock */
        if (work && work->func) {
            work->func(work);
            work->flags &= ~WORK_STRUCT_RUNNING;
        }
    }

    return NULL;
}

/**
 * create_workqueue - create a new workqueue
 * @name: name for the workqueue
 *
 * Returns workqueue pointer or NULL on failure
 */
static inline struct workqueue_struct *create_workqueue(const char *name)
{
    struct workqueue_struct *wq;

    wq = (struct workqueue_struct *)malloc(sizeof(*wq));
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
 * destroy_workqueue - destroy a workqueue
 * @wq: workqueue to destroy
 *
 * Waits for pending work to complete
 */
static inline void destroy_workqueue(struct workqueue_struct *wq)
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
 * queue_work - queue work to a workqueue
 * @wq: target workqueue
 * @work: work item to queue
 *
 * Returns 1 if work was queued, 0 if already pending
 */
static inline int queue_work(struct workqueue_struct *wq,
                             ec_work_t *work)
{
    int ret = 0;

    pthread_mutex_lock(&wq->lock);

    /* Don't queue if already pending */
    if (!(work->flags & WORK_STRUCT_PENDING)) {
        work->flags |= WORK_STRUCT_PENDING;
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
 * schedule_work - queue work to system workqueue
 * @work: work item to queue
 *
 * Returns 1 if work was queued, 0 if already pending
 */
static inline int schedule_work(ec_work_t *work)
{
    return queue_work(system_wq, work);
}



/**
 * cancel_work_sync - cancel work and wait for completion
 * @work: work item to cancel
 *
 * Returns 1 if work was pending, 0 if not
 */
static inline int cancel_work_sync(ec_work_t *work)
{
    int was_pending;

    /* Mark as not pending (worker will skip if not dequeued yet) */
    was_pending = (work->flags & WORK_STRUCT_PENDING) ? 1 : 0;

    /* Wait for completion if running */
    while (work->flags & WORK_STRUCT_RUNNING) {
        sched_yield();
    }

    /* Clear pending flag */
    work->flags &= ~WORK_STRUCT_PENDING;

    return was_pending;
}



#endif /* __EC_USPACE_PAL_WORK_H__ */

