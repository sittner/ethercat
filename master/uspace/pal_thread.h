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

#include <stdatomic.h>

/* Task states */
#define EC_TASK_RUNNING         0x0000
#define EC_TASK_INTERRUPTIBLE   0x0001
#define EC_TASK_UNINTERRUPTIBLE 0x0002
#define EC_TASK_DEAD            0x0080

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
    atomic_int state;           /* Task state (atomic) */
    atomic_int should_stop;     /* Stop requested flag (atomic) */
    int exit_code;              /* Thread exit code */
    char name[16];              /* Thread name */

    /* Thread function and data */
    int (*thread_fn)(void *data);
    void *thread_data;

    /* Synchronization */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int started;
    int bind_cpu;               /* CPU to bind to (-1 = unbound) */
    int detached;               /* Non-zero if thread is detached */
} ec_thread_t;

/* Thread-local storage key (defined in pal_thread.c) */
extern void current_task_init(void);
extern ec_thread_t *get_current(void);
extern void set_current(ec_thread_t *task);

/* Thread lifecycle functions (defined in pal_thread.c) */
extern ec_thread_t *__ec_thread_create(
        int (*threadfn)(void *data),
        void *data,
        const char *namefmt, ...);
extern int ec_thread_wake(ec_thread_t *task);
extern int ec_thread_stop(ec_thread_t *task);
extern void ec_thread_detach(ec_thread_t *task);
extern void ec_thread_bind_cpu(ec_thread_t *task, unsigned int cpu);
extern void ec_thread_set_priority(ec_thread_t *task, int nice);

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
 * ec_thread_should_stop - check if stop was requested
 */
static inline int ec_thread_should_stop(void)
{
    ec_thread_t *task = get_current();
    return task ? atomic_load(&task->should_stop) : 0;
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

#endif /* __EC_USPACE_PAL_THREAD_H__ */

