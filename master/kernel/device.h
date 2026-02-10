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
   Kernel-specific EtherCAT device extensions.
*/

/****************************************************************************/

#ifndef __EC_KERNEL_DEVICE_H__
#define __EC_KERNEL_DEVICE_H__

#include "../device.h"
#include "../../devices/ecdev.h"

/****************************************************************************/

/* Kernel-specific device functions */
void ec_device_attach(ec_device_t *, struct net_device *, ec_pollfunc_t,
        struct module *);
void ec_device_detach(ec_device_t *);

#ifdef EC_DEBUG_RING
void ec_device_debug_ring_append(ec_device_t *, ec_debug_frame_dir_t,
        const void *, size_t);
void ec_device_debug_ring_print(const ec_device_t *);
#endif

/****************************************************************************/

#endif /* __EC_KERNEL_DEVICE_H__ */

