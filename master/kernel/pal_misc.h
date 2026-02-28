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

#define EC_LOG_EMERG   KERN_EMERG
#define EC_LOG_ALERT   KERN_ALERT
#define EC_LOG_CRIT    KERN_CRIT
#define EC_LOG_ERR     KERN_ERR
#define EC_LOG_WARNING KERN_WARNING
#define EC_LOG_NOTICE  KERN_NOTICE
#define EC_LOG_INFO    KERN_INFO
#define EC_LOG_DEBUG   KERN_DEBUG

#define ec_log(level, fmt, args...) \
    printk(level fmt, ##args)

#define ec_log_ratelimit() printk_ratelimit()

#endif /* __EC_KERNEL_PAL_MISC_H__ */
