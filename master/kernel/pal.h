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

/****************************************************************************/

#endif // __EC_PAL_H__
