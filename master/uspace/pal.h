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

#ifndef __EC_USPACE_PAL_H__
#define __EC_USPACE_PAL_H__

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdatomic.h>
#include <syslog.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/syscall.h>

#include <stdint.h>
#include <stdbool.h>

#include "../globals.h"

/****************************************************************************/
/* Kernel-compatible integer types for userspace */
/****************************************************************************/

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* Kernel module exports - not needed in userspace */
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_VERSION(x)
#define MODULE_PARM_DESC(x, y)
#define module_param(name, type, perm)
#define module_param_named(name, var, type, perm)
#define module_param_array(name, type, nump, perm)
#define module_init(fn)
#define module_exit(fn)

/****************************************************************************/
/* Ethernet constants */
/****************************************************************************/

#ifndef ETH_ALEN
#define ETH_ALEN        6    /* Octets in one ethernet address */
#endif

#ifndef ETH_HLEN
#define ETH_HLEN        14   /* Total octets in header */
#endif

#ifndef ETH_DATA_LEN
#define ETH_DATA_LEN    1500 /* Max octets in payload */
#endif

#include "uspace/list.h"


#define GFP_KERNEL  0
#define GFP_ATOMIC  0
#define __GFP_ZERO  0

#define kmalloc(size, flags)    malloc(size)
#define kzalloc(size, flags)    calloc(1, size)
#define kfree(ptr)              free(ptr)

#define vmalloc(size)           malloc(size)
#define vzalloc(size)           calloc(1, size)
#define vfree(ptr)              free(ptr)

#define krealloc(ptr, size, flags)  realloc(ptr, size)

/* kmemdup - allocate and copy */
static inline void *kmemdup(const void *src, size_t len, unsigned gfp)
{
    void *p = malloc(len);
    if (p)
        memcpy(p, src, len);
    return p;
}

/* kstrdup - duplicate a string */
static inline char *kstrdup(const char *s, unsigned gfp)
{
    return strdup(s);
}

/* kstrndup - duplicate a string with max length */
static inline char *kstrndup(const char *s, size_t max, unsigned gfp)
{
    return strndup(s, max);
}


#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x)   __builtin_expect(!!(x), 1)

/****************************************************************************/
/* Logging */
/****************************************************************************/

#define KERN_EMERG		"<0>"	/* system is unusable			*/
#define KERN_ALERT		"<1>"	/* action must be taken immediately	*/
#define KERN_CRIT		"<2>"	/* critical conditions			*/
#define KERN_ERR		"<3>"	/* error conditions			*/
#define KERN_WARNING	"<4>"	/* warning conditions			*/
#define KERN_NOTICE		"<5>"	/* normal but significant condition	*/
#define KERN_INFO		"<6>"	/* informational			*/
#define KERN_DEBUG		"<7>"	/* debug-level messages			*/

/*
 * Annotation for a "continued" line of log printout (only done after a
 * line that had no enclosing \n). Only to be used by core/arch code
 * during early bootup (a continued line is not SMP-safe otherwise).
 */
#define KERN_CONT		"<c>"

int printk(const char *fmt, ...);


//************************************************************************

/* Kernel semaphore compatibility */
typedef sem_t ec_semaphore_t;

/**
 * sema_init - initialize a semaphore
 * @sem: semaphore to initialize
 * @val: initial value
 */
static inline void sema_init(ec_semaphore_t *sem, int val)
{
    sem_init(sem, 0, val);  /* 0 = not shared between processes */
}

/**
 * down - acquire semaphore (may sleep)
 * @sem: semaphore to acquire
 *
 * Kernel down() cannot be interrupted. Use sem_wait().
 */
static inline void down(ec_semaphore_t *sem)
{
    sem_wait(sem);
}

/**
 * down_interruptible - acquire semaphore, interruptible
 * @sem: semaphore to acquire
 *
 * Returns 0 on success, -EINTR if interrupted by signal
 */
static inline int down_interruptible(ec_semaphore_t *sem)
{
    if (sem_wait(sem) == -1 && errno == EINTR)
        return -EINTR;
    return 0;
}

/**
 * down_trylock - try to acquire without blocking
 * @sem: semaphore to acquire
 *
 * Returns 0 if acquired, 1 if not (note: opposite of sem_trywait!)
 */
static inline int down_trylock(ec_semaphore_t *sem)
{
    return (sem_trywait(sem) == 0) ? 0 : 1;
}

/**
 * up - release semaphore
 * @sem: semaphore to release
 */
static inline void up(ec_semaphore_t *sem)
{
    sem_post(sem);
}

//************************************************************************
//typedef struct rt_mutex ec_rt_mutex_t;

/**
 * struct rt_mutex - real-time mutex for userspace
 *
 * Uses PTHREAD_PRIO_INHERIT to enable priority inheritance
 */
typedef struct {
    pthread_mutex_t mutex;
} ec_rt_mutex_t;

/**
 * rt_mutex_init - initialize an rt_mutex
 * @lock: the mutex to initialize
 *
 * Configures the mutex with priority inheritance protocol
 */
static inline void rt_mutex_init(ec_rt_mutex_t *lock)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    
    /* Enable priority inheritance to prevent priority inversion */
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    
    pthread_mutex_init(&lock->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

/**
 * rt_mutex_destroy - clean up rt_mutex resources
 * @lock: the mutex to destroy
 *
 * Note: No kernel equivalent, but needed in userspace
 */
static inline void rt_mutex_destroy(ec_rt_mutex_t *lock)
{
    pthread_mutex_destroy(&lock->mutex);
}

/**
 * rt_mutex_lock - acquire the mutex
 * @lock: the mutex to acquire
 *
 * Sleeps until the mutex is acquired. Cannot be interrupted.
 */
static inline void rt_mutex_lock(ec_rt_mutex_t *lock)
{
    pthread_mutex_lock(&lock->mutex);
}

/**
 * rt_mutex_lock_interruptible - acquire mutex, interruptible
 * @lock: the mutex to acquire
 *
 * Returns 0 on success, -EINTR if interrupted
 *
 * Note: True interruptibility requires pthread_cancel or signals.
 * This is a simplified implementation.
 */
static inline int rt_mutex_lock_interruptible(ec_rt_mutex_t *lock)
{
    int ret = pthread_mutex_lock(&lock->mutex);
    if (ret == EINTR)
        return -EINTR;
    return ret ? -ret : 0;
}

/**
 * rt_mutex_trylock - try to acquire without blocking
 * @lock: the mutex to acquire
 *
 * Returns 1 if acquired, 0 if not (opposite of pthread!)
 */
static inline int rt_mutex_trylock(ec_rt_mutex_t *lock)
{
    return (pthread_mutex_trylock(&lock->mutex) == 0) ? 1 : 0;
}

/**
 * rt_mutex_unlock - release the mutex
 * @lock: the mutex to release
 */
static inline void rt_mutex_unlock(ec_rt_mutex_t *lock)
{
    pthread_mutex_unlock(&lock->mutex);
}

/**
 * rt_mutex_is_locked - check if mutex is locked
 * @lock: the mutex to check
 *
 * Returns 1 if locked, 0 if not
 */
static inline int rt_mutex_is_locked(ec_rt_mutex_t *lock)
{
    if (pthread_mutex_trylock(&lock->mutex) == 0) {
        pthread_mutex_unlock(&lock->mutex);
        return 0;  /* Was not locked */
    }
    return 1;  /* Is locked */
}

//************************************************************************
//typedef wait_queue_head_t ec_wait_queue_t;

/* Wait queue structure for userspace */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
} ec_wait_queue_t;

/**
 * init_waitqueue_head - initialize a wait queue
 * @wq: wait queue to initialize
 */
static inline void init_waitqueue_head(ec_wait_queue_t *wq)
{
    pthread_mutex_init(&wq->lock, NULL);
    pthread_cond_init(&wq->cond, NULL);
}

/**
 * destroy_waitqueue_head - clean up wait queue resources
 * @wq: wait queue to destroy
 *
 * Note: No kernel equivalent, but needed in userspace
 */
static inline void destroy_waitqueue_head(ec_wait_queue_t *wq)
{
    pthread_cond_destroy(&wq->cond);
    pthread_mutex_destroy(&wq->lock);
}

/**
 * wake_up - wake one waiting thread
 * @wq: wait queue
 */
static inline void wake_up(ec_wait_queue_t *wq)
{
    pthread_mutex_lock(&wq->lock);
    pthread_cond_signal(&wq->cond);
    pthread_mutex_unlock(&wq->lock);
}

/**
 * wake_up_all - wake all waiting threads
 * @wq: wait queue
 */
static inline void wake_up_all(ec_wait_queue_t *wq)
{
    pthread_mutex_lock(&wq->lock);
    pthread_cond_broadcast(&wq->cond);
    pthread_mutex_unlock(&wq->lock);
}

/**
 * wake_up_interruptible - wake threads (same as wake_up in userspace)
 * @wq: wait queue
 */
#define wake_up_interruptible(wq) wake_up(wq)

/**
 * wait_event - sleep until condition is true
 * @wq: wait queue
 * @condition: condition to wait for
 *
 * Note: Condition is checked with lock held to avoid races
 */
#define wait_event(wq, condition)                       \
    do {                                                \
        pthread_mutex_lock(&(wq)->lock);                \
        while (!(condition)) {                          \
            pthread_cond_wait(&(wq)->cond, &(wq)->lock);\
        }                                               \
        pthread_mutex_unlock(&(wq)->lock);              \
    } while (0)

/**
 * wait_event_interruptible - sleep until condition (interruptible)
 * @wq: wait queue
 * @condition: condition to wait for
 *
 * Returns 0 if condition became true, -ERESTARTSYS on signal
 *
 * Note: True signal interruption requires more complex handling
 */
#define wait_event_interruptible(wq, condition)         \
    ({                                                  \
        int __ret = 0;                                  \
        pthread_mutex_lock(&(wq)->lock);                \
        while (!(condition)) {                          \
            pthread_cond_wait(&(wq)->cond, &(wq)->lock);\
        }                                               \
        pthread_mutex_unlock(&(wq)->lock);              \
        __ret;                                          \
    })

/**
 * wait_event_timeout - sleep until condition or timeout
 * @wq: wait queue
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
        pthread_mutex_lock(&(wq)->lock);                                \
        while (!(condition)) {                                          \
            if (pthread_cond_timedwait(&(wq)->cond, &(wq)->lock, &__ts) \
                    == ETIMEDOUT) {                                     \
                __ret = 0;                                              \
                break;                                                  \
            }                                                           \
        }                                                               \
        pthread_mutex_unlock(&(wq)->lock);                              \
        __ret;                                                          \
    })

//************************************************************************
//typedef struct task_struct ec_thread_t;

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

#ifndef HZ
#define HZ 1000
#endif

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

//************************************************************************
//typedef struct work_struct ec_work_t;

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

/**
 * work_pending - check if work is queued
 * @work: work item to check
 */
static inline int work_pending(ec_work_t *work)
{
    return work->flags & WORK_STRUCT_PENDING;
}

/**
 * work_busy - check if work is pending or running
 * @work: work item to check
 */
static inline int work_busy(ec_work_t *work)
{
    return work->flags & (WORK_STRUCT_PENDING | WORK_STRUCT_RUNNING);
}

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
 * flush_work - wait for work item to complete
 * @work: work item to wait for
 *
 * Returns 1 if work was pending, 0 if not
 */
static inline int flush_work(ec_work_t *work)
{
    int was_pending = 0;

    /* Spin until work is no longer pending or running */
    while (work->flags & (WORK_STRUCT_PENDING | WORK_STRUCT_RUNNING)) {
        was_pending = 1;
        sched_yield();
    }

    return was_pending;
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

/**
 * flush_workqueue - wait for all pending work to complete
 * @wq: workqueue to flush
 */
static inline void flush_workqueue(struct workqueue_struct *wq)
{
    pthread_mutex_lock(&wq->lock);
    while (wq->head) {
        pthread_mutex_unlock(&wq->lock);
        sched_yield();
        pthread_mutex_lock(&wq->lock);
    }
    pthread_mutex_unlock(&wq->lock);
}

//************************************************************************
//typedef struct irq_work ec_irq_work_t;

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

/****************************************************************************/
/* Kernel utility macros */
/****************************************************************************/

/* min/max macros with strict type checking */
#define min(a, b) \
    ({ \
        typeof(a) _a = (a); \
        typeof(b) _b = (b); \
        _a < _b ? _a : _b; \
    })

#define max(a, b) \
    ({ \
        typeof(a) _a = (a); \
        typeof(b) _b = (b); \
        _a > _b ? _a : _b; \
    })

/* Type-specific versions */
#define min_t(type, a, b) \
    ({ \
        type _a = (a); \
        type _b = (b); \
        _a < _b ? _a : _b; \
    })

#define max_t(type, a, b) \
    ({ \
        type _a = (a); \
        type _b = (b); \
        _a > _b ? _a : _b; \
    })

/* Clamp a value to a range */
#define clamp(val, lo, hi) min(max(val, lo), hi)

#define clamp_t(type, val, lo, hi) min_t(type, max_t(type, val, lo), hi)

//************************************************************************

#define HZ 1000  /* 1ms tick */

typedef unsigned long jiffies_t;

static inline jiffies_t get_jiffies(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

#define jiffies get_jiffies()

/* HZ equivalent - we're using milliseconds directly */
#define HZ 1000

/* Conversion macros (trivial since we use ms directly) */
#define jiffies_to_msecs(j)  (j)
#define msecs_to_jiffies(m)  (m)

/* Time comparison macros (handle wraparound) */
#define time_after(a, b)     ((long)((b) - (a)) < 0)
#define time_before(a, b)    time_after(b, a)
#define time_after_eq(a, b)  ((long)((a) - (b)) >= 0)
#define time_before_eq(a, b) time_after_eq(b, a)

/****************************************************************************/
/* 64-bit division helpers */
/****************************************************************************/

/**
 * do_div - 64-bit division with remainder
 * @n: dividend (modified in place to become quotient)
 * @base: divisor
 *
 * Returns: remainder
 *
 * In kernel, this handles 64-bit division on 32-bit architectures.
 * In userspace, we can just use regular division.
 */
#define do_div(n, base) \
    ({ \
        uint64_t __n = (n); \
        uint32_t __base = (base); \
        uint32_t __rem = __n % __base; \
        (n) = __n / __base; \
        __rem; \
    })

/**
 * div_u64 - unsigned 64-bit divide with 32-bit divisor
 * @dividend: 64-bit dividend
 * @divisor: 32-bit divisor
 *
 * Returns: quotient
 */
static inline uint64_t div_u64(uint64_t dividend, uint32_t divisor)
{
    return dividend / divisor;
}

/**
 * div_u64_rem - unsigned 64-bit divide with remainder
 * @dividend: 64-bit dividend
 * @divisor: 32-bit divisor
 * @remainder: pointer to store remainder
 *
 * Returns: quotient
 */
static inline uint64_t div_u64_rem(uint64_t dividend, uint32_t divisor,
                                   uint32_t *remainder)
{
    *remainder = dividend % divisor;
    return dividend / divisor;
}

/**
 * div_s64 - signed 64-bit divide
 * @dividend: 64-bit dividend
 * @divisor: 32-bit divisor
 *
 * Returns: quotient
 */
static inline int64_t div_s64(int64_t dividend, int32_t divisor)
{
    return dividend / divisor;
}

/**
 * div64_u64 - unsigned 64/64 division
 * @dividend: 64-bit dividend
 * @divisor: 64-bit divisor
 *
 * Returns: quotient
 */
static inline uint64_t div64_u64(uint64_t dividend, uint64_t divisor)
{
    return dividend / divisor;
}

//************************************************************************

struct net_device_stats {
  int dummy;
};

struct ec_device;
typedef struct ec_device ec_device_t;

typedef struct {
//    ec_transport_t *transport;           /**< Transport layer. */
    uint64_t jiffies_poll;               /**< Time of last poll (ms). */
    uint64_t last_link_check;            /**< Time of last link state check (ms). */
    int last_link_state;                 /**< Last reported link state (-1 = unknown). */
} ec_device_pal_t;

struct ec_master;
typedef struct ec_master ec_master_t;

/** Kernel-specific master fields. */
typedef struct {
  // TODO
} ec_master_pal_t;


/* TODO: Add userspace implementations of kernel APIs */

#endif /* __EC_USPACE_PAL_H__ */
