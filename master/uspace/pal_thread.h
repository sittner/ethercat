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

#ifndef __EC_USPACE_PAL_THREAD_H__
#define __EC_USPACE_PAL_THREAD_H__

/* Task states */
#define TASK_RUNNING         0x0000
#define TASK_INTERRUPTIBLE   0x0001
#define TASK_UNINTERRUPTIBLE 0x0002
#define TASK_DEAD            0x0080

/* Error pointer macros */
#define ERR_PTR(err)        ((void *)((long)(err)))
#define PTR_ERR(ptr)        ((long)(ptr))
#define IS_ERR(ptr)         ((unsigned long)(void *)(ptr) >= (unsigned long)-4095) /* MAX_ERRNO = 4095 */

/* Portable gettid */
#if defined(__GLIBC__) && \
    ((__GLIBC__ > 2) || ((__GLIBC__ == 2) && (__GLIBC_MINOR__ >= 30)))
    #define pal_gettid() gettid()
#else
    static inline pid_t pal_gettid(void)
    {
        return (pid_t)syscall(SYS_gettid);
    }
#endif

/**
 * ec_thread_t - userspace thread representation
 */
typedef struct {
    pthread_t thread;           /* POSIX thread handle */
    pid_t pid;                  /* Thread ID */
    volatile int state;         /* Task state */
    volatile int should_stop;   /* Stop requested flag */
    int exit_code;              /* Thread exit code */
    char name[16];              /* Thread name */

    /* Thread function and data */
    int (*thread_fn)(void *data);
    void *thread_data;

    /* Synchronization */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int started;
} ec_thread_t;

/* Key for thread-specific task_struct pointer */
static pthread_key_t current_task_key;
static pthread_once_t current_task_key_once = PTHREAD_ONCE_INIT;

/* Initialize the thread-specific key */
static void __init_current_task_key(void)
{
    pthread_key_create(&current_task_key, NULL);
}

static inline void current_task_init(void)
{
    pthread_once(&current_task_key_once, __init_current_task_key);
}

static inline ec_thread_t *get_current(void)
{
    current_task_init();
    return (ec_thread_t *)pthread_getspecific(current_task_key);
}

static inline void set_current(ec_thread_t *task)
{
    current_task_init();
    pthread_setspecific(current_task_key, task);
}

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
    task->state = TASK_RUNNING;
    pthread_cond_signal(&task->cond);
    pthread_mutex_unlock(&task->lock);

    ret = task->thread_fn(task->thread_data);

    task->exit_code = ret;
    task->state = TASK_DEAD;

    return (void *)(long)ret;
}

/* Internal: create a thread without starting it */
static inline ec_thread_t *__ec_thread_create(
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
    task->state = TASK_UNINTERRUPTIBLE;
    task->should_stop = 0;
    task->started = 0;

    va_start(args, namefmt);
    vsnprintf(task->name, sizeof(task->name), namefmt, args);
    va_end(args);

    pthread_mutex_init(&task->lock, NULL);
    pthread_cond_init(&task->cond, NULL);

    return task;
}

/**
 * ec_thread_wake - start or wake a created thread
 */
static inline int ec_thread_wake(ec_thread_t *task)
{
    int ret;

    pthread_mutex_lock(&task->lock);

    if (task->started) {
        task->state = TASK_RUNNING;
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

/**
 * ec_thread_run - create and start a thread
 */
#define ec_thread_run(threadfn, data, namefmt, ...)                     \
    ({                                                                  \
        ec_thread_t *__k = __ec_thread_create(threadfn, data,          \
                                    namefmt, ##__VA_ARGS__);            \
        if (!IS_ERR(__k))                                               \
            ec_thread_wake(__k);                                        \
        __k;                                                            \
    })

/**
 * ec_thread_stop - stop a thread and wait for exit
 */
static inline int ec_thread_stop(ec_thread_t *task)
{
    int ret;
    void *thread_ret;

    task->should_stop = 1;

    pthread_mutex_lock(&task->lock);
    task->state = TASK_RUNNING;
    pthread_cond_broadcast(&task->cond);
    pthread_mutex_unlock(&task->lock);

    pthread_join(task->thread, &thread_ret);

    ret = task->exit_code;

    pthread_mutex_destroy(&task->lock);
    pthread_cond_destroy(&task->cond);
    free(task);

    return ret;
}

/**
 * ec_thread_should_stop - check if stop was requested
 */
static inline int ec_thread_should_stop(void)
{
    ec_thread_t *task = get_current();
    return task ? task->should_stop : 0;
}

/**
 * ec_thread_bind_cpu - bind thread to CPU (no-op, stored for future use)
 */
static inline void ec_thread_bind_cpu(ec_thread_t *task, unsigned int cpu)
{
    (void)task;
    (void)cpu;
    /* TODO: Store cpu and apply in thread wrapper */
}

/**
 * ec_thread_yield - yield the processor
 */
static inline void ec_thread_yield(void)
{
    sched_yield();
}

/**
 * ec_thread_yield_timeout - yield for a specified number of time units
 * @timeout: timeout value (treated as units of 4 milliseconds in userspace)
 */
static inline void ec_thread_yield_timeout(long timeout)
{
    struct timespec ts;

    if (timeout <= 0) {
        sched_yield();
        return;
    }

    ts.tv_sec = timeout / 250;
    ts.tv_nsec = (timeout % 250) * 4000000L;
    nanosleep(&ts, NULL);
}

/**
 * ec_thread_set_priority - set task to normal scheduling policy
 * @task: task to modify
 * @nice: nice value (-20 to 19)
 */
static inline void ec_thread_set_priority(ec_thread_t *task, int nice)
{
    struct sched_param param = { .sched_priority = 0 };

    pthread_setschedparam(task->thread, SCHED_OTHER, &param);
    (void)nice;
}

#define ec_sched_set_normal(thread, nice) ec_thread_set_priority(thread, nice)

#endif /* __EC_USPACE_PAL_THREAD_H__ */

