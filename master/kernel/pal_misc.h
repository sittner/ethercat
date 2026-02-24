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
   Platform Abstraction Layer - misc/logging wrappers for kernel EtherCAT master.
*/

/****************************************************************************/

#ifndef __EC_KERNEL_PAL_MISC_H__
#define __EC_KERNEL_PAL_MISC_H__

#include <linux/kernel.h>

/** Token-pasting helpers to map EC_LOG_* integer levels to KERN_* strings.
 *
 * Two-level expansion is required so that macro arguments (e.g. EC_LOG_ERR)
 * are fully expanded to their integer value before the token paste occurs.
 * _EC_KERN_LVL_PASTE performs the actual paste; _EC_KERN_LVL forces
 * expansion of its argument first, yielding e.g. _EC_KERN_LVL_3 -> KERN_ERR.
 */
#define _EC_KERN_LVL_0 KERN_EMERG
#define _EC_KERN_LVL_1 KERN_ALERT
#define _EC_KERN_LVL_2 KERN_CRIT
#define _EC_KERN_LVL_3 KERN_ERR
#define _EC_KERN_LVL_4 KERN_WARNING
#define _EC_KERN_LVL_5 KERN_NOTICE
#define _EC_KERN_LVL_6 KERN_INFO
#define _EC_KERN_LVL_7 KERN_DEBUG

#define _EC_KERN_LVL_PASTE(level) _EC_KERN_LVL_##level
#define _EC_KERN_LVL(level)       _EC_KERN_LVL_PASTE(level)

#define ec_log(level, fmt, args...) \
    printk(_EC_KERN_LVL(level) fmt, ##args)

#define ec_log_ratelimit() printk_ratelimit()

#endif /* __EC_KERNEL_PAL_MISC_H__ */
