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
   Platform Abstraction Layer - wait queue wrappers for kernel EtherCAT master.
*/

/****************************************************************************/

#ifndef __EC_KERNEL_PAL_QUEUE_H__
#define __EC_KERNEL_PAL_QUEUE_H__

#include <linux/wait.h>

typedef wait_queue_head_t ec_wait_queue_t;

static inline void ec_wq_init(ec_wait_queue_t *wq)
{
    init_waitqueue_head(wq);
}

static inline void ec_wq_wake(ec_wait_queue_t *wq)
{
    wake_up(wq);
}

static inline void ec_wq_wake_interruptible(ec_wait_queue_t *wq)
{
    wake_up_interruptible(wq);
}

static inline void ec_wq_wake_all(ec_wait_queue_t *wq)
{
    wake_up_all(wq);
}

/* ec_wq_wait and ec_wq_wait_interruptible must be macros because they
 * capture a condition expression */
#define ec_wq_wait(wq, condition) \
    wait_event(wq, condition)

#define ec_wq_wait_interruptible(wq, condition) \
    wait_event_interruptible(wq, condition)

#endif /* __EC_KERNEL_PAL_QUEUE_H__ */
