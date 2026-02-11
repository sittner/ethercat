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
   Platform Abstraction Layer - System and kernel includes.
*/

/****************************************************************************/

#ifndef __EC_PAL_H__
#define __EC_PAL_H__

#include <asm/div64.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/if_ether.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/irq_work.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/rtmutex.h>
#include <linux/semaphore.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/timer.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/types.h>
#include <uapi/linux/sched/types.h>
#endif

#if defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
#include <linux/suse_version.h>
#else
#  ifndef SUSE_VERSION
#    define SUSE_VERSION 0
#  endif
#  ifndef SUSE_PATCHLEVEL
#    define SUSE_PATCHLEVEL 0
#  endif
#endif

#include "../../devices/ecdev.h"

#include "cdev.h"

#ifdef EC_RTDM
#include "rtdm.h"
#endif

typedef struct semaphore ec_semaphore_t;
typedef struct rt_mutex ec_rt_mutex_t;
typedef wait_queue_head_t ec_wait_queue_t;
typedef struct task_struct ec_thread_t;
typedef struct work_struct ec_work_t;
typedef struct irq_work ec_irq_work_t;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0) || \
    (defined(CONFIG_PREEMPT_RT_FULL) && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
#  define ec_rt_lock_interruptible(lock) \
          rt_mutex_lock_interruptible(lock)
#else
#  define ec_rt_lock_interruptible(lock) \
          rt_mutex_lock_interruptible(lock, 0)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
#    define ec_sched_set_normal(thread, nice) sched_set_normal(thread, nice)
#else
#    define ec_sched_set_normal(thread, nice)                   \
        do {                                                    \
            struct sched_param param = { .sched_priority = 0 }; \
            sched_setscheduler(p, SCHED_NORMAL, &param);        \
            set_user_nice(p, nice);                             \
        } while (0)
#endif

/** Kernel-specific master fields. */
typedef struct {
    ec_cdev_t cdev;                     /**< Master character device. */
    struct device *class_device;        /**< Master class device. */

#ifdef EC_RTDM
    ec_rtdm_dev_t rtdm_dev;             /**< RTDM device. */
#endif
} ec_master_pal_t;

/**
 * Size of the transmit ring.
 * This memory ring is used to transmit frames. It is necessary to use
 * different memory regions, because otherwise the network device DMA could
 * send the same data twice, if it is called twice.
 */
#define EC_TX_RING_SIZE 2

/** Kernel-specific device fields. */
typedef struct {
    ec_pollfunc_t poll; /**< pointer to the device's poll function */
    struct net_device *dev; /**< pointer to the assigned net_device */
    struct module *module; /**< pointer to the device's owning module */
    struct sk_buff *tx_skb[EC_TX_RING_SIZE]; /**< transmit skb ring */
    unsigned int tx_ring_index; /**< last ring entry used to transmit */
} ec_device_pal_t;

/****************************************************************************/

#endif // __EC_PAL_H__
