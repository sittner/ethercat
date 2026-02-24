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

#ifndef __EC_USPACE_PAL_IRQ_WORK_H__
#define __EC_USPACE_PAL_IRQ_WORK_H__

/* Forward declaration */
struct pal_irq_work;

/* IRQ work function typedef */
typedef void (*ec_irq_work_func_t)(struct pal_irq_work *work);

/* Work flags (needed for inline ec_irq_work_sync) */
#define EC_IRQ_WORK_PENDING    (1 << 0)
#define EC_IRQ_WORK_BUSY       (1 << 1)

/**
 * ec_irq_work_t - low-latency deferred work
 */
struct pal_irq_work {
    ec_irq_work_func_t func;        /* Work function */
    atomic_int flags;               /* State flags (lock-free) */
    struct pal_irq_work *next;      /* Next in queue (lock-free list) */
};

typedef struct pal_irq_work ec_irq_work_t;

/**
 * ec_irq_work_init - initialize an irq_work item
 */
static inline void ec_irq_work_init(ec_irq_work_t *work,
                                    ec_irq_work_func_t func)
{
    work->func = func;
    atomic_init(&work->flags, 0);
    work->next = NULL;
}

extern void ec_irq_work_queue(ec_irq_work_t *work);

/**
 * ec_irq_work_sync - wait for work to complete
 */
static inline void ec_irq_work_sync(ec_irq_work_t *work)
{
    while (atomic_load(&work->flags) & (EC_IRQ_WORK_PENDING | EC_IRQ_WORK_BUSY)) {
        sched_yield();
    }
}

extern int ec_pal_irq_work_init(void);
extern void ec_pal_irq_work_cleanup(void);

#endif /* __EC_USPACE_PAL_IRQ_WORK_H__ */

