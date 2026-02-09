/******************************************************************************
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
 *****************************************************************************/

/**
 * \file
 * Userspace master API implementation.
 */

#include <stdio.h>
#include <errno.h>

#include "include/ecrt_user.h"
#include "transport/ec_transport.h"

/****************************************************************************/

int ecrt_master_init(unsigned int master_index,
                     ec_pal_device_type_t device_type,
                     const char *interface)
{
    ec_transport_type_t transport_type;

    (void)master_index;

    /* Map device type to transport type */
    switch (device_type) {
    case EC_PAL_DEVICE_RAW:
        transport_type = EC_TRANSPORT_RAW;
        break;
    case EC_PAL_DEVICE_XDP:
        transport_type = EC_TRANSPORT_XDP;
        break;
    default:
        return -EINVAL;
    }

    /* Check if transport is available */
    if (!ec_transport_type_available(transport_type)) {
        fprintf(stderr, "Transport '%s' not available\n",
                ec_transport_type_name(transport_type));
        return -ENOTSUP;
    }

    /* TODO: Create transport and open interface (PR 2b) */
    fprintf(stderr, "ecrt_master_init: transport=%s interface=%s (not yet implemented)\n",
            ec_transport_type_name(transport_type), interface);

    return -ENOSYS;
}

/****************************************************************************/

void ecrt_master_cleanup(unsigned int master_index)
{
    (void)master_index;
    /* TODO: Destroy transport (PR 2b) */
}

/****************************************************************************/

void ecrt_master_idle(unsigned int master_index)
{
    (void)master_index;
    /* TODO: Process idle work (PR 2b) */
}

/****************************************************************************/

int ecrt_master_process_control(unsigned int master_index)
{
    (void)master_index;
    /* TODO: Process control interface (PR 2b) */
    return 0;
}

/****************************************************************************/
