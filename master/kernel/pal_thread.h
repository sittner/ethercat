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
   Platform Abstraction Layer - thread wrappers for kernel EtherCAT master.
*/

/****************************************************************************/

#ifndef __EC_KERNEL_PAL_THREAD_H__
#define __EC_KERNEL_PAL_THREAD_H__

#include <linux/kthread.h>
#include <linux/sched.h>

typedef struct task_struct ec_thread_t;

/* ec_thread_run - create and start a kernel thread (variadic namefmt) */
#define ec_thread_run(threadfn, data, namefmt, ...) \
    kthread_run(threadfn, data, namefmt, ##__VA_ARGS__)

static inline int ec_thread_stop(ec_thread_t *task)
{
    return kthread_stop(task);
}

static inline int ec_thread_should_stop(void)
{
    return kthread_should_stop();
}

static inline void ec_thread_yield(void)
{
    set_current_state(TASK_INTERRUPTIBLE);
    schedule();
}

static inline void ec_thread_yield_timeout(long timeout)
{
    set_current_state(TASK_INTERRUPTIBLE);
    schedule_timeout(timeout);
}

static inline int ec_thread_wake(ec_thread_t *task)
{
    return wake_up_process(task);
}

static inline void ec_thread_set_priority(ec_thread_t *task, int nice)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
    sched_set_normal(task, nice);
#else
    {
        struct sched_param param = { .sched_priority = 0 };
        sched_setscheduler(task, SCHED_NORMAL, &param);
        set_user_nice(task, nice);
    }
#endif
}

static inline void ec_thread_bind_cpu(ec_thread_t *task, unsigned int cpu)
{
    kthread_bind(task, cpu);
}

#endif /* __EC_KERNEL_PAL_THREAD_H__ */
