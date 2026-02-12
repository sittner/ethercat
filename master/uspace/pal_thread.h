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
#define TASK_STOPPED         0x0004
#define TASK_DEAD            0x0080

/* Error pointer macros */
#define MAX_ERRNO           4095
#define IS_ERR_VALUE(x)     ((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
#define ERR_PTR(err)        ((void *)((long)(err)))
#define PTR_ERR(ptr)        ((long)(ptr))
#define IS_ERR(ptr)         IS_ERR_VALUE((unsigned long)(ptr))
#define IS_ERR_OR_NULL(ptr) (!(ptr) || IS_ERR(ptr))

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
 * struct task_struct - userspace thread representation
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

/**
 * current_task_init - initialize current task subsystem
 *
 * Call once at program startup
 */
static inline void current_task_init(void)
{
    pthread_once(&current_task_key_once, __init_current_task_key);
}

/**
 * get_current - get current task_struct for calling thread
 *
 * Returns NULL if not a managed kthread
 */
static inline ec_thread_t *get_current(void)
{
    current_task_init();  /* Ensure key exists */
    return (ec_thread_t *)pthread_getspecific(current_task_key);
}

/**
 * set_current - set current task_struct for calling thread
 * @task: task to set as current
 */
static inline void set_current(ec_thread_t *task)
{
    current_task_init();  /* Ensure key exists */
    pthread_setspecific(current_task_key, task);
}

/* Macro to access current (like kernel) */
#define current get_current()

/* Internal thread wrapper */
static void *__task_thread_wrapper(void *arg)
{
    ec_thread_t *task = (ec_thread_t *)arg;
    int ret;

    /* Store task pointer in thread-specific data */
    set_current(task);

    /* Get real thread ID */
    task->pid = pal_gettid();

    /* Set thread name */
    pthread_setname_np(task->thread, task->name);

    /* Signal that we've started */
    pthread_mutex_lock(&task->lock);
    task->started = 1;
    task->state = TASK_RUNNING;
    pthread_cond_signal(&task->cond);
    pthread_mutex_unlock(&task->lock);

    /* Run thread function - signature matches kernel */
    ret = task->thread_fn(task->thread_data);

    task->exit_code = ret;
    task->state = TASK_DEAD;

    return (void *)(long)ret;
}

/**
 * kthread_create - create a new kernel-style thread
 * @threadfn: thread function (same signature as kernel)
 * @data: data passed to thread function
 * @namefmt: thread name format
 */
static inline ec_thread_t *kthread_create(
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
 * wake_up_process - start a created thread
 */
static inline int wake_up_process(ec_thread_t *task)
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
 * kthread_run - create and start a thread
 */
#define kthread_run(threadfn, data, namefmt, ...)                   \
    ({                                                              \
        ec_thread_t *__k = kthread_create(threadfn, data,    \
                                    namefmt, ##__VA_ARGS__);        \
        if (!IS_ERR(__k))                                           \
            wake_up_process(__k);                                   \
        __k;                                                        \
    })

/**
 * kthread_stop - stop a thread and wait for exit
 */
static inline int kthread_stop(ec_thread_t *task)
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
 * kthread_should_stop - check if stop was requested
 *
 * Uses thread-specific 'current' - same API as kernel!
 */
static inline int kthread_should_stop(void)
{
    ec_thread_t *task = current;
    return task ? task->should_stop : 0;
}

/**
 * kthread_bind - bind thread to CPU
 */
static inline void kthread_bind(ec_thread_t *task, unsigned int cpu)
{
	return; // TODO: Store cpu and apply in kthread_wrapper
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(task->thread, sizeof(cpuset), &cpuset);
}

/**
 * set_current_state - set current task's state
 */
#define set_current_state(state_value)          \
    do {                                        \
        ec_thread_t *__t = current;      \
        if (__t)                                \
            __t->state = (state_value);         \
    } while (0)

#define __set_current_state(state_value) set_current_state(state_value)

/**
 * schedule - yield the processor
 */
static inline void schedule(void)
{
    sched_yield();
}

/**
 * schedule_timeout - sleep for jiffies
 */
static inline long schedule_timeout(long timeout)
{
    struct timespec ts;

    if (timeout <= 0)
        return 0;

    ts.tv_sec = timeout / HZ;
    ts.tv_nsec = (timeout % HZ) * (1000000000L / HZ);

    nanosleep(&ts, NULL);
    return 0;
}

/**
 * sched_set_normal - set task to normal scheduling policy
 * @task: task to modify
 * @nice: nice value (-20 to 19)
 *
 * In kernel, this sets SCHED_NORMAL policy.
 * In userspace, we use SCHED_OTHER (same thing).
 */
static inline void sched_set_normal(ec_thread_t *task, int nice)
{
    struct sched_param param = { .sched_priority = 0 };

    /* SCHED_OTHER (normal) doesn't use priority, uses nice instead */
    pthread_setschedparam(task->thread, SCHED_OTHER, &param);

    /* Set nice value - requires appropriate privileges */
    /* Note: setpriority() affects the whole thread group in some cases,
     * but for pthreads this is typically fine */
#ifdef _GNU_SOURCE
    /* Could use pthread_setschedprio or setpriority here */
    (void)nice;  /* Nice value handling is limited in pthreads */
#endif
}

#define ec_sched_set_normal(thread, nice) sched_set_normal(thread, nice)

#endif /* __EC_USPACE_PAL_THREAD_H__ */

