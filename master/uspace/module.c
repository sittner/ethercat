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

/****************************************************************************/

ec_master_t *ecrt_startup_master(unsigned int index,
        ec_transport_t *transport,
        ec_transport_t *backup_transport,
        unsigned int debug_level,
        int run_on_cpu)
{
    ec_master_t *master;
    int ret;

    if (!transport) {
        ec_log(EC_LOG_ERR, "Main transport must not be NULL\n");
        return NULL;
    }

    if (!transport->interface[0]) {
        ec_log(EC_LOG_ERR, "Main transport interface name must not be empty\n");
        return NULL;
    }

    master = malloc(sizeof(ec_master_t));
    if (!master) {
        ec_log(EC_LOG_ERR, "Failed to allocate master context\n");
        return NULL;
    }
    memset(master, 0, sizeof(ec_master_t));

    /* Borrow transport pointers — caller owns them, library opens/closes */
    master->pal.transport = transport;
    master->pal.backup_transport = backup_transport;
    atomic_init(&master->pal.rt_cpu, -1);
    master->pal.affinity_cpu = -1;

    /* Open main transport (interface stored in transport->interface) */
    ret = ec_transport_open(master->pal.transport);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to open transport on %s: %d\n",
                master->pal.transport->interface, ret);
        goto out_free;
    }

    /* Get MAC address from transport */
    ret = ec_transport_get_mac(master->pal.transport, master->pal.main_mac);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to get MAC address: %d\n", ret);
        goto out_close_main;
    }

    /* Open backup transport and get its MAC (if present) */
    if (master->pal.backup_transport) {
        ret = ec_transport_open(master->pal.backup_transport);
        if (ret < 0) {
            ec_log(EC_LOG_ERR, "Failed to open backup transport on %s: %d\n",
                    master->pal.backup_transport->interface, ret);
            goto out_close_main;
        }

        ret = ec_transport_get_mac(master->pal.backup_transport,
                master->pal.backup_mac);
        if (ret < 0) {
            ec_log(EC_LOG_ERR, "Failed to get backup MAC address: %d\n", ret);
            goto out_close_backup;
        }
    }

    /* Initialize master */
    ret = ec_master_init(master, index, master->pal.main_mac,
            master->pal.backup_mac, debug_level,
            run_on_cpu < 0 ? 0xffffffff : (unsigned int)run_on_cpu);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to initialize master: %d\n", ret);
        goto out_close_backup;
    }

    /* Store transport reference in device; interface name borrowed from transport */
    master->devices[EC_DEVICE_MAIN].pal.transport = master->pal.transport;
    master->devices[EC_DEVICE_MAIN].name = master->pal.transport->interface;

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
                master->pal.backup_transport->interface;
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
     *
     * Activity-based scan timeout: reset on every new slave discovered.
     * This handles both empty buses (timeout after ~5s) and large buses
     * (keeps waiting as long as slaves are being found). */
#define EC_SCAN_PROGRESS_TIMEOUT 5  /* seconds */
    {
        unsigned int prev_count = 0;
        while (!master->initial_scan_done) {
            prev_count = master->slave_count;
            ec_wq_wait_timeout(master->scan_queue, master->initial_scan_done,
                    EC_SCAN_PROGRESS_TIMEOUT);
            if (master->initial_scan_done)
                break;
            if (master->slave_count == prev_count) {
                /* No progress — bus empty or stalled */
                EC_MASTER_WARN(master,
                        "Initial bus scan timed out after %u seconds"
                        " with no new slaves. %u slave(s) found so far."
                        " Scan continues in background.\n",
                        EC_SCAN_PROGRESS_TIMEOUT, master->slave_count);
                break;
            }
            /* Progress was made (new slaves found), keep waiting */
            EC_MASTER_DBG(master, 1,
                    "Scan progress: %u slave(s) found so far,"
                    " resetting timeout.\n", master->slave_count);
        }
    }
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
out_close_backup:
    if (master->pal.backup_transport) {
        ec_transport_close(master->pal.backup_transport);
    }
out_close_main:
    ec_transport_close(master->pal.transport);
out_free:
    free(master);
    return NULL;
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

    /* Close transports — caller still owns them and must call
     * ec_transport_destroy() afterwards. */
    if (master->pal.backup_transport) {
        ec_transport_close(master->pal.backup_transport);
    }
    if (master->pal.transport) {
        ec_transport_close(master->pal.transport);
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
