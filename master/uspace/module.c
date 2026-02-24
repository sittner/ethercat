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
   Userspace EtherCAT master library lifecycle implementation.
*/

/****************************************************************************/

/* ec_transport.h must be included before pal.h (which pulls in ecrt.h via
 * globals.h -> shared.h) so that __EC_TRANSPORT_H__ is defined when ecrt.h
 * runs, preventing redeclaration conflicts with the inline transport
 * definitions in ecrt.h. */
#include "transport/ec_transport.h"

#include "pal.h"

#include "../device.h"
#include "../master.h"

#include <stdlib.h>
#include <string.h>

/****************************************************************************/

int ecrt_lib_init(void)
{
    ec_master_init_static();

    if (ec_pal_work_init() != 0) {
        ec_log(EC_LOG_ERR, "Failed to create system workqueue\n");
        return -1;
    }

    if (ec_pal_irq_work_init() != 0) {
        ec_log(EC_LOG_ERR, "Failed to create IRQ work queue\n");
        ec_pal_work_cleanup();
        return -1;
    }

    return 0;
}

/****************************************************************************/

/** Common startup helper: initializes master from an already-assigned transport.
 *
 * Assumes master->pal.transport, master->pal.transport_owned, and
 * master->pal.interface_name are already set.
 *
 * \return master on success, NULL on error (master is freed on error).
 */
static ec_master_t *ecrt_startup_master_common(ec_master_t *master)
{
    int ret;

    /* Get MAC address from transport */
    ret = ec_transport_get_mac(master->pal.transport, master->pal.main_mac);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to get MAC address: %d\n", ret);
        goto out_free_interface;
    }

    /* Initialize master */
    ret = ec_master_init(master, 0, master->pal.main_mac, master->pal.backup_mac, 1, 0);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to initialize master: %d\n", ret);
        goto out_free_interface;
    }

    /* Store transport reference in device */
    master->devices[EC_DEVICE_MAIN].pal.transport = master->pal.transport;
    master->devices[EC_DEVICE_MAIN].name = master->pal.interface_name;

    /* Open device */
    ret = ec_device_open(&master->devices[EC_DEVICE_MAIN]);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to open device: %d\n", ret);
        goto out_clear_master;
    }

    /* Enter idle phase */
    ret = ec_master_enter_idle_phase(master);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to enter idle phase: %d\n", ret);
        goto out_close_device;
    }

    return master;

out_close_device:
    ec_device_close(&master->devices[EC_DEVICE_MAIN]);
out_clear_master:
    ec_master_clear(master);
out_free_interface:
    if (master->pal.interface_name) {
        free(master->pal.interface_name);
        master->pal.interface_name = NULL;
    }
    free(master);
    return NULL;
}

/****************************************************************************/

ec_master_t *ecrt_startup_master(ec_transport_type_t transport_type,
        const char *interface)
{
    ec_master_t *master;
    int ret;

    master = malloc(sizeof(ec_master_t));
    if (!master) {
        ec_log(EC_LOG_ERR, "Failed to allocate master context\n");
        return NULL;
    }
    memset(master, 0, sizeof(ec_master_t));
    master->pal.transport_owned = 1;

    master->pal.interface_name = strdup(interface);
    if (!master->pal.interface_name) {
        ec_log(EC_LOG_ERR, "Failed to copy interface name\n");
        free(master);
        return NULL;
    }

    master->pal.transport = ec_transport_create(transport_type);
    if (!master->pal.transport) {
        ec_log(EC_LOG_ERR, "Failed to create transport\n");
        free(master->pal.interface_name);
        free(master);
        return NULL;
    }

    ret = ec_transport_open(master->pal.transport, interface);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to open transport on %s: %d\n", interface, ret);
        ec_transport_destroy(master->pal.transport);
        free(master->pal.interface_name);
        free(master);
        return NULL;
    }

    return ecrt_startup_master_common(master);
}

/****************************************************************************/

ec_master_t *ecrt_startup_master_custom(ec_transport_t *transport,
        const char *interface)
{
    ec_master_t *master;

    master = malloc(sizeof(ec_master_t));
    if (!master) {
        ec_log(EC_LOG_ERR, "Failed to allocate master context\n");
        return NULL;
    }
    memset(master, 0, sizeof(ec_master_t));
    master->pal.transport_owned = 0;
    master->pal.transport = transport;

    master->pal.interface_name = strdup(interface);
    if (!master->pal.interface_name) {
        ec_log(EC_LOG_ERR, "Failed to copy interface name\n");
        free(master);
        return NULL;
    }

    return ecrt_startup_master_common(master);
}

/****************************************************************************/

void ecrt_release_master(ec_master_t *master)
{
    if (master->phase != EC_ORPHANED) {
        if (master->active) {
            ec_master_leave_operation_phase(master);
        }
        ec_master_leave_idle_phase(master);
    }

    ec_device_close(&master->devices[EC_DEVICE_MAIN]);
    ec_master_clear(master);

    if (master->pal.transport) {
        ec_transport_close(master->pal.transport);
        if (master->pal.transport_owned) {
            ec_transport_destroy(master->pal.transport);
        }
    }

    if (master->pal.interface_name) {
        free(master->pal.interface_name);
    }

    free(master);
}

/****************************************************************************/

void ecrt_lib_cleanup(void)
{
    ec_pal_irq_work_cleanup();
    ec_pal_work_cleanup();
}

/****************************************************************************/
