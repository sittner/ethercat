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
   PAL: IRQ affinity helpers for userspace master.
   Included after ec_master struct and macros are defined.
*/

#ifndef __EC_USPACE_PAL_AFFINITY_H__
#define __EC_USPACE_PAL_AFFINITY_H__

#include "ectp.h"

/** Record RT caller's CPU for IRQ affinity tracking.
 *  Guarded by master->active (set only after ecrt_master_activate). */
static inline void ec_pal_record_rt_cpu(ec_master_t *master)
{
    if (!master->active)
        return;
    atomic_store_explicit(&master->pal.rt_cpu, sched_getcpu(),
                          memory_order_relaxed);
}

/** Check if RT CPU changed and re-pin transport IRQs accordingly. */
static inline void ec_pal_check_irq_affinity(ec_master_t *master)
{
    int rt_cpu = atomic_load_explicit(&master->pal.rt_cpu,
                                      memory_order_relaxed);
    if (rt_cpu >= 0 && rt_cpu != master->pal.affinity_cpu) {
        if (ec_transport_set_cpu_affinity(master->pal.transport,
                                          rt_cpu) == 0) {
            EC_MASTER_INFO(master,
                "Pinned transport IRQ to CPU %d\n", rt_cpu);
        } else {
            EC_MASTER_WARN(master,
                "Failed to pin transport IRQ to CPU %d\n", rt_cpu);
        }
        master->pal.affinity_cpu = rt_cpu;
    }
}

#endif /* __EC_USPACE_PAL_AFFINITY_H__ */
