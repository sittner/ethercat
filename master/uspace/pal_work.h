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

#ifndef __EC_USPACE_PAL_WORK_H__
#define __EC_USPACE_PAL_WORK_H__

/* Forward declaration */
struct pal_work_struct;

/* Work function typedef */
typedef void (*ec_work_func_t)(struct pal_work_struct *work);

/**
 * ec_work_t - deferred work item
 */
struct pal_work_struct {
    ec_work_func_t func;            /* Work function */
    volatile unsigned long flags;   /* State flags */
    struct pal_work_struct *next;   /* Next in queue (linked list) */
};

typedef struct pal_work_struct ec_work_t;

/**
 * ec_work_init - initialize a work item
 * @_work: work struct to initialize
 * @_func: function to execute
 */
#define ec_work_init(_work, _func)                  \
    do {                                            \
        (_work)->func = (_func);                    \
        (_work)->flags = 0;                         \
        (_work)->next = NULL;                       \
    } while (0)

extern int ec_work_schedule(ec_work_t *work);
extern int ec_work_cancel(ec_work_t *work);

extern int ec_pal_work_init(void);
extern void ec_pal_work_cleanup(void);

#endif /* __EC_USPACE_PAL_WORK_H__ */

