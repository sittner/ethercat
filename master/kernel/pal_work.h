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
   Platform Abstraction Layer - work queue wrappers for kernel EtherCAT master.
*/

/****************************************************************************/

#ifndef __EC_KERNEL_PAL_WORK_H__
#define __EC_KERNEL_PAL_WORK_H__

#include <linux/workqueue.h>

typedef struct work_struct ec_work_t;

/* ec_work_init must be a macro because INIT_WORK is a macro */
#define ec_work_init(_work, _func) INIT_WORK(_work, _func)

static inline int ec_work_schedule(ec_work_t *work)
{
    return schedule_work(work);
}

static inline bool ec_work_cancel(ec_work_t *work)
{
    return cancel_work_sync(work);
}

#endif /* __EC_KERNEL_PAL_WORK_H__ */
