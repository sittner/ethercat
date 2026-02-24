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
   Platform Abstraction Layer - work queue implementation for userspace.
*/

/****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>

#include "pal_work.h"

/****************************************************************************/

/* Work states (internal) */
#define EC_WORK_PENDING     (1 << 0)
#define EC_WORK_RUNNING     (1 << 1)

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

/* Global system workqueue (file-scope) */
static struct ec_workqueue *ec_system_wq;

/****************************************************************************/

static void *__workqueue_worker(void *arg)
{
    struct ec_workqueue *wq = (struct ec_workqueue *)arg;
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

/****************************************************************************/

static struct ec_workqueue *ec_wq_create(const char *name)
{
    struct ec_workqueue *wq;

    wq = (struct ec_workqueue *)malloc(sizeof(*wq));
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

/****************************************************************************/

static void ec_wq_destroy(struct ec_workqueue *wq)
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

/****************************************************************************/

static int ec_work_queue(struct ec_workqueue *wq, ec_work_t *work)
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

/****************************************************************************/

int ec_work_schedule(ec_work_t *work)
{
    return ec_work_queue(ec_system_wq, work);
}

/****************************************************************************/

int ec_work_cancel(ec_work_t *work)
{
    int was_pending;

    was_pending = (work->flags & EC_WORK_PENDING) ? 1 : 0;

    while (work->flags & EC_WORK_RUNNING) {
        sched_yield();
    }

    work->flags &= ~EC_WORK_PENDING;

    return was_pending;
}

/****************************************************************************/

int ec_pal_work_init(void)
{
    ec_system_wq = ec_wq_create("system_wq");
    if (!ec_system_wq)
        return -ENOMEM;
    return 0;
}

/****************************************************************************/

void ec_pal_work_cleanup(void)
{
    ec_wq_destroy(ec_system_wq);
    ec_system_wq = NULL;
}
