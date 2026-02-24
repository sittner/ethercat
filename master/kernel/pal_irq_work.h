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
   Platform Abstraction Layer - IRQ work wrappers for kernel EtherCAT master.
*/

/****************************************************************************/

#ifndef __EC_KERNEL_PAL_IRQ_WORK_H__
#define __EC_KERNEL_PAL_IRQ_WORK_H__

#include <linux/irq_work.h>

typedef struct irq_work ec_irq_work_t;

static inline void ec_irq_work_init(ec_irq_work_t *work,
                                    void (*func)(ec_irq_work_t *))
{
    init_irq_work(work, func);
}

static inline bool ec_irq_work_queue(ec_irq_work_t *work)
{
    return irq_work_queue(work);
}

static inline void ec_irq_work_sync(ec_irq_work_t *work)
{
    irq_work_sync(work);
}

#endif /* __EC_KERNEL_PAL_IRQ_WORK_H__ */
