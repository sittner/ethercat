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

#include "pal.h"

#include "../device.h"
#include "../master.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************/

static atomic_flag lib_initialized = ATOMIC_FLAG_INIT;

/****************************************************************************/

int ecrt_lib_init(ec_log_cb_t log_cb, const char *socket_path)
{
    if (atomic_flag_test_and_set(&lib_initialized)) {
        ec_log(EC_LOG_WARNING, "ecrt_lib_init() called more than once; ignoring\n");
        return 0;
    }

    ec_log_set_callback(log_cb);
    ec_master_init_static();

    if (ec_pal_work_init() != 0) {
        ec_log(EC_LOG_ERR, "Failed to create system workqueue\n");
        atomic_flag_clear(&lib_initialized);
        return -1;
    }

    if (ec_pal_irq_work_init() != 0) {
        ec_log(EC_LOG_ERR, "Failed to create IRQ work queue\n");
        ec_pal_work_cleanup();
        atomic_flag_clear(&lib_initialized);
        return -1;
    }

    /* Start IPC server only if socket_path is provided. */
    if (socket_path) {
        int ret = ec_ipc_server_start(socket_path);
        if (ret < 0) {
            ec_log(EC_LOG_WARNING,
                    "Failed to start IPC server on %s: %d; "
                    "ethercat tool will not be able to connect\n",
                    socket_path, ret);
            /* Non-fatal — master still usable via API */
        }
    }

    return 0;
}

/****************************************************************************/

/** Common startup helper: initializes master from an already-assigned transport.
 *
 * Assumes master->pal.transport, master->pal.transport_owned,
 * master->pal.interface_name, and optionally master->pal.backup_transport
 * and master->pal.backup_interface_name are already set.
 *
 * \return master on success, NULL on error (master is freed on error).
 */
static ec_master_t *ecrt_startup_master_common(ec_master_t *master,
        unsigned int index, unsigned int debug_level, unsigned int run_on_cpu)
{
    int ret;

    /* Get MAC address from transport */
    ret = ec_transport_get_mac(master->pal.transport, master->pal.main_mac);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to get MAC address: %d\n", ret);
        goto out_free_interface;
    }

    /* Get backup MAC address from backup transport (if any) */
    if (master->pal.backup_transport) {
        ret = ec_transport_get_mac(master->pal.backup_transport,
                master->pal.backup_mac);
        if (ret < 0) {
            ec_log(EC_LOG_ERR, "Failed to get backup MAC address: %d\n", ret);
            goto out_free_interface;
        }
    }

    /* Initialize master */
    ret = ec_master_init(master, index, master->pal.main_mac,
            master->pal.backup_mac, debug_level, run_on_cpu);
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

    /* Set up backup device if backup transport is present */
    if (master->pal.backup_transport) {
        master->devices[EC_DEVICE_BACKUP].pal.transport =
                master->pal.backup_transport;
        master->devices[EC_DEVICE_BACKUP].name =
                master->pal.backup_interface_name;
        ret = ec_device_open(&master->devices[EC_DEVICE_BACKUP]);
        if (ret < 0) {
            ec_log(EC_LOG_ERR, "Failed to open backup device: %d\n", ret);
            goto out_close_main_device;
        }
    }

    /* Enter idle phase */
    ret = ec_master_enter_idle_phase(master);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to enter idle phase: %d\n", ret);
        goto out_close_backup_device;
    }

    /* Wait for the initial bus scan to complete before returning to the
     * caller. This ensures that when the application calls
     * ecrt_master_slave_config(), slaves have been scanned and their SII
     * data (including default PDO mappings) is available.
     * ec_wq_wait_interruptible() always returns 0 in the userspace PAL. */
    ec_wq_wait_interruptible(master->scan_queue, master->initial_scan_done);
    EC_MASTER_DBG(master, 1, "Initial bus scan complete, %u slave(s) found.\n",
            master->slave_count);

    /* Register master in global registry for IPC dispatch. */
    ec_master_registry_add(master);

    return master;

out_close_backup_device:
    if (master->pal.backup_transport) {
        ec_device_close(&master->devices[EC_DEVICE_BACKUP]);
    }
out_close_main_device:
    ec_device_close(&master->devices[EC_DEVICE_MAIN]);
out_clear_master:
    ec_master_clear(master);
out_free_interface:
    if (master->pal.backup_interface_name) {
        free(master->pal.backup_interface_name);
        master->pal.backup_interface_name = NULL;
    }
    if (master->pal.interface_name) {
        free(master->pal.interface_name);
        master->pal.interface_name = NULL;
    }
    free(master);
    return NULL;
}

/****************************************************************************/

ec_master_t *ecrt_startup_master(unsigned int index,
        ec_transport_type_t transport_type,
        const char *interface,
        const char *backup_interface,
        unsigned int debug_level,
        unsigned int run_on_cpu)
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

    /* Set up backup transport if backup_interface is specified */
    if (backup_interface) {
        master->pal.backup_interface_name = strdup(backup_interface);
        if (!master->pal.backup_interface_name) {
            ec_log(EC_LOG_ERR, "Failed to copy backup interface name\n");
            ec_transport_close(master->pal.transport);
            ec_transport_destroy(master->pal.transport);
            free(master->pal.interface_name);
            free(master);
            return NULL;
        }

        master->pal.backup_transport = ec_transport_create(transport_type);
        if (!master->pal.backup_transport) {
            ec_log(EC_LOG_ERR, "Failed to create backup transport\n");
            free(master->pal.backup_interface_name);
            ec_transport_close(master->pal.transport);
            ec_transport_destroy(master->pal.transport);
            free(master->pal.interface_name);
            free(master);
            return NULL;
        }

        ret = ec_transport_open(master->pal.backup_transport, backup_interface);
        if (ret < 0) {
            ec_log(EC_LOG_ERR, "Failed to open backup transport on %s: %d\n",
                    backup_interface, ret);
            ec_transport_destroy(master->pal.backup_transport);
            free(master->pal.backup_interface_name);
            ec_transport_close(master->pal.transport);
            ec_transport_destroy(master->pal.transport);
            free(master->pal.interface_name);
            free(master);
            return NULL;
        }
    }

    return ecrt_startup_master_common(master, index, debug_level, run_on_cpu);
}

/****************************************************************************/

ec_master_t *ecrt_startup_master_custom(unsigned int index,
        ec_transport_t *transport,
        const char *interface,
        const char *backup_interface,
        unsigned int debug_level,
        unsigned int run_on_cpu)
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

    /* TODO: backup_interface support for custom transport requires
     * ec_transport_get_type() to determine the transport type for creating
     * a second transport instance. Not yet implemented. */
    if (backup_interface) {
        ec_log(EC_LOG_WARNING,
                "Backup interface not supported in custom transport mode\n");
    }

    return ecrt_startup_master_common(master, index, debug_level, run_on_cpu);
}

/****************************************************************************/

void ecrt_release_master(ec_master_t *master)
{
    if (!master) return;

    /* Unregister master from global registry before shutting down. */
    ec_master_registry_remove(master);

    if (master->phase != EC_ORPHANED) {
        if (master->active) {
            ec_master_leave_operation_phase(master);
        }
        ec_master_leave_idle_phase(master);
    }

    if (master->pal.backup_transport) {
        ec_device_close(&master->devices[EC_DEVICE_BACKUP]);
    }
    ec_device_close(&master->devices[EC_DEVICE_MAIN]);
    ec_master_clear(master);

    if (master->pal.backup_transport) {
        ec_transport_close(master->pal.backup_transport);
        ec_transport_destroy(master->pal.backup_transport);
    }

    if (master->pal.transport) {
        ec_transport_close(master->pal.transport);
        if (master->pal.transport_owned) {
            ec_transport_destroy(master->pal.transport);
        }
    }

    if (master->pal.backup_interface_name) {
        free(master->pal.backup_interface_name);
    }

    if (master->pal.interface_name) {
        free(master->pal.interface_name);
    }

    free(master);
}

/****************************************************************************/

void ecrt_lib_cleanup(void)
{
    ec_ipc_server_stop();
    ec_pal_irq_work_cleanup();
    ec_pal_work_cleanup();
    atomic_flag_clear(&lib_initialized);
}

/****************************************************************************/
