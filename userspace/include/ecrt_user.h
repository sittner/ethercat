/******************************************************************************
 *
 *  Copyright (C) 2006-2025  Florian Pose, Ingenieurgemeinschaft IgH
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
 *****************************************************************************/

/**
 * \file
 * EtherCAT userspace-specific API extensions.
 */

#ifndef __ECRT_USER_H__
#define __ECRT_USER_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * Userspace Master Management
 *****************************************************************************/

/**
 * Device type for userspace transports.
 */
typedef enum {
    EC_PAL_DEVICE_RAW,     /**< Raw socket transport */
    EC_PAL_DEVICE_XDP      /**< XDP (eXpress Data Path) transport */
} ec_pal_device_type_t;

/**
 * Initialize a master instance (replaces kernel module loading).
 *
 * \param master_index Master index (0, 1, ...)
 * \param device_type Built-in device type to use
 * \param interface Network interface name (e.g., "eth0")
 * \return 0 on success, < 0 on error
 */
int ecrt_master_init(
    unsigned int master_index,
    ec_pal_device_type_t device_type,
    const char *interface
);

/**
 * Cleanup a master instance (replaces kernel module unloading).
 *
 * \param master_index Master index
 */
void ecrt_master_cleanup(unsigned int master_index);

/**
 * Run idle processing (call periodically when not in OPERATION).
 * 
 * Equivalent to kernel's ec_master_idle_thread work.
 *
 * \param master_index Master index
 */
void ecrt_master_idle(unsigned int master_index);

/**
 * Process control interface requests (for CLI tool).
 * 
 * Should be called periodically, or run in a separate thread.
 *
 * \param master_index Master index
 * \return 0 on success, < 0 on error
 */
int ecrt_master_process_control(unsigned int master_index);

#ifdef __cplusplus
}
#endif

#endif /* __ECRT_USER_H__ */
