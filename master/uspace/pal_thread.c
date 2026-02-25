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
   Platform Abstraction Layer - thread implementation for userspace.
*/

/****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdatomic.h>

#include "pal.h"

/****************************************************************************/

/* Thread-local storage key for current ec_thread_t pointer */
static pthread_key_t current_task_key;
static pthread_once_t current_task_key_once = PTHREAD_ONCE_INIT;

/****************************************************************************/

static void __init_current_task_key(void)
{
    pthread_key_create(&current_task_key, NULL);
}

void current_task_init(void)
{
    pthread_once(&current_task_key_once, __init_current_task_key);
}

ec_thread_t *get_current(void)
{
    current_task_init();
    return (ec_thread_t *)pthread_getspecific(current_task_key);
}

void set_current(ec_thread_t *task)
{
    current_task_init();
    pthread_setspecific(current_task_key, task);
}

/****************************************************************************/

/* Internal thread wrapper */
static void *__task_thread_wrapper(void *arg)
{
    ec_thread_t *task = (ec_thread_t *)arg;
    int ret;

    set_current(task);
    task->pid = pal_gettid();
    pthread_setname_np(task->thread, task->name);

    pthread_mutex_lock(&task->lock);
    task->started = 1;
    atomic_store(&task->state, EC_TASK_RUNNING);
    pthread_cond_signal(&task->cond);
    pthread_mutex_unlock(&task->lock);

    if (task->bind_cpu >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(task->bind_cpu, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    }

    ret = task->thread_fn(task->thread_data);

    task->exit_code = ret;
    atomic_store(&task->state, EC_TASK_DEAD);

    if (task->detached) {
        pthread_mutex_destroy(&task->lock);
        pthread_cond_destroy(&task->cond);
        free(task);
        return (void *)(long)ret;
    }

    return (void *)(long)ret;
}

/****************************************************************************/

/**
 * __ec_thread_create - create a thread without starting it
 */
ec_thread_t *__ec_thread_create(
        int (*threadfn)(void *data),
        void *data,
        const char *namefmt, ...)
{
    ec_thread_t *task;
    va_list args;

    task = (ec_thread_t *)malloc(sizeof(*task));
    if (!task)
        return ERR_PTR(-ENOMEM);

    memset(task, 0, sizeof(*task));

    task->thread_fn = threadfn;
    task->thread_data = data;
    atomic_init(&task->state, EC_TASK_UNINTERRUPTIBLE);
    atomic_init(&task->should_stop, 0);
    task->started = 0;
    task->bind_cpu = -1;
    task->detached = 0;

    va_start(args, namefmt);
    vsnprintf(task->name, sizeof(task->name), namefmt, args);
    va_end(args);

    pthread_mutex_init(&task->lock, NULL);
    pthread_cond_init(&task->cond, NULL);

    return task;
}

/****************************************************************************/

/**
 * ec_thread_wake - start or wake a created thread
 */
int ec_thread_wake(ec_thread_t *task)
{
    int ret;

    pthread_mutex_lock(&task->lock);

    if (task->started) {
        atomic_store(&task->state, EC_TASK_RUNNING);
        pthread_cond_signal(&task->cond);
        pthread_mutex_unlock(&task->lock);
        return 0;
    }

    ret = pthread_create(&task->thread, NULL, __task_thread_wrapper, task);
    if (ret != 0) {
        pthread_mutex_unlock(&task->lock);
        return 0;
    }

    while (!task->started) {
        pthread_cond_wait(&task->cond, &task->lock);
    }

    pthread_mutex_unlock(&task->lock);
    return 1;
}

/****************************************************************************/

/**
 * ec_thread_stop - stop a thread and wait for exit
 */
int ec_thread_stop(ec_thread_t *task)
{
    int ret;
    void *thread_ret;

    atomic_store(&task->should_stop, 1);

    pthread_mutex_lock(&task->lock);
    atomic_store(&task->state, EC_TASK_RUNNING);
    pthread_cond_broadcast(&task->cond);
    pthread_mutex_unlock(&task->lock);

    pthread_join(task->thread, &thread_ret);

    ret = task->exit_code;

    pthread_mutex_destroy(&task->lock);
    pthread_cond_destroy(&task->cond);
    free(task);

    return ret;
}

/****************************************************************************/

/**
 * ec_thread_detach - detach a thread so it frees itself on exit
 */
void ec_thread_detach(ec_thread_t *task)
{
    pthread_detach(task->thread);
    task->detached = 1;
}

/****************************************************************************/

/**
 * ec_thread_bind_cpu - bind thread to CPU
 */
void ec_thread_bind_cpu(ec_thread_t *task, unsigned int cpu)
{
    task->bind_cpu = (int)cpu;

    if (task->started) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(task->bind_cpu, &cpuset);
        pthread_setaffinity_np(task->thread, sizeof(cpuset), &cpuset);
    }
}

/****************************************************************************/

/**
 * ec_thread_set_priority - set task to normal scheduling policy
 * @task: task to modify
 * @nice: nice value (-20 to 19, ignored - setting nice from a thread context
 *        requires CAP_SYS_NICE, so we just ensure the policy is SCHED_OTHER)
 */
void ec_thread_set_priority(ec_thread_t *task, int nice)
{
    struct sched_param param = { .sched_priority = 0 };

    pthread_setschedparam(task->thread, SCHED_OTHER, &param);
    (void)nice;
}
