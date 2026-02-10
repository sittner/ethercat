/*****************************************************************************
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
 ****************************************************************************/

/**
   \file
   Userspace master initialization and management.
*/

/****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "pal.h"
#include "../master.h"
#include "../device.h"
#include "../fsm_master.h"
#include "../datagram.h"
#include "../globals.h"
#include "ecrt_user.h"

/****************************************************************************/

/** Array of master instances. */
static ec_master_t *masters[EC_MAX_MASTERS];

/****************************************************************************/

/** Forward declaration for device interface setting. */
void ec_device_set_interface(ec_device_t *device, const char *interface);

/****************************************************************************/

/**
 * Initialize a master instance.
 *
 * This is the userspace equivalent of loading the kernel module and creating
 * a master. It initializes the master structure, creates the device, and
 * enters idle phase.
 *
 * \param master_index Master index (0, 1, ...).
 * \param device_type Device transport type.
 * \param interface Network interface name (e.g., "eth0").
 * \return 0 on success, negative error code on failure.
 */
int ecrt_master_init(
    unsigned int master_index,
    ec_pal_device_type_t device_type,
    const char *interface
)
{
    ec_master_t *master;
    int ret;
    unsigned int i;

    if (master_index >= EC_MAX_MASTERS) {
        return -EINVAL;
    }

    if (masters[master_index]) {
        return -EBUSY;
    }

    /* Allocate and zero master structure */
    master = ec_pal_zalloc(sizeof(ec_master_t));
    if (!master) {
        return -ENOMEM;
    }

    /* Basic initialization */
    master->index = master_index;
    master->phase = EC_ORPHANED;
    master->debug_level = 1;
    master->reserved = 0;
    master->active = 0;
    master->config_changed = 0;
    master->injection_seq_fsm = 0;
    master->injection_seq_rt = 0;

    /* Initialize lists */
    INIT_LIST_HEAD(&master->configs);
    INIT_LIST_HEAD(&master->domains);
    INIT_LIST_HEAD(&master->datagram_queue);
    INIT_LIST_HEAD(&master->ext_datagram_queue);
    INIT_LIST_HEAD(&master->sii_requests);
    INIT_LIST_HEAD(&master->emerg_reg_requests);
    INIT_LIST_HEAD(&master->fsm_exec_list);

    /* Initialize slave array */
    master->slaves = NULL;
    master->slave_count = 0;

    /* Initialize scanning */
    master->scan_busy = 0;
    master->scan_index = 0;
    master->allow_scan = 1;

    /* Initialize configuration */
    master->config_busy = 0;

    /* Initialize datagram index */
    master->datagram_index = 0;

    /* Initialize FSM execution */
    master->fsm_slave = NULL;
    master->fsm_exec_count = 0;

    /* Initialize DC */
    master->app_time = 0;
    master->dc_ref_time = 0;
    master->dc_ref_config = NULL;
    master->dc_ref_clock = NULL;

    /* Initialize platform fields using PAL */
    ec_pal_sem_init(&master->plat.master_sem, 1);
    ec_pal_sem_init(&master->plat.device_sem, 1);
    ec_pal_sem_init(&master->plat.scan_sem, 1);
    ec_pal_sem_init(&master->plat.config_sem, 1);
    ec_pal_sem_init(&master->plat.ext_queue_sem, 1);
    ec_pal_mutex_init(&master->plat.io_mutex);
    ec_pal_wait_queue_init(&master->plat.request_queue);
    ec_pal_wait_queue_init(&master->plat.scan_queue);
    ec_pal_wait_queue_init(&master->plat.config_queue);

#ifdef EC_EOE
    INIT_LIST_HEAD(&master->eoe_handlers);
    ec_pal_wait_queue_init(&master->plat.eoe_queue);
#endif

    /* Initialize device */
    ret = ec_device_init(&master->devices[EC_DEVICE_MAIN], master);
    if (ret < 0) {
        goto err_free;
    }

    /* Set interface name */
    ec_device_set_interface(&master->devices[EC_DEVICE_MAIN], interface);

    /* Initialize FSM datagram */
    ec_datagram_init(&master->fsm_datagram);
    snprintf(master->fsm_datagram.name, EC_DATAGRAM_NAME_SIZE, "master-fsm");
    ret = ec_datagram_prealloc(&master->fsm_datagram, EC_MAX_DATA_SIZE);
    if (ret < 0) {
        goto err_device;
    }

    /* Initialize external datagram ring */
    for (i = 0; i < EC_EXT_RING_SIZE; i++) {
        ec_datagram_init(&master->ext_datagram_ring[i]);
        snprintf(master->ext_datagram_ring[i].name, EC_DATAGRAM_NAME_SIZE,
                 "ext-%u", i);
    }
    master->ext_ring_idx_rt = 0;
    master->ext_ring_idx_fsm = 0;

    /* Initialize master FSM */
    ec_fsm_master_init(&master->fsm, master, &master->fsm_datagram);

    /* Set defaults */
    ec_master_set_send_interval(master, 1000);
    master->send_cb = ec_master_internal_send_cb;
    master->receive_cb = ec_master_internal_receive_cb;
    master->cb_data = master;
    master->app_send_cb = NULL;
    master->app_receive_cb = NULL;
    master->app_cb_data = NULL;

    /* Open device */
    ret = ec_device_open(&master->devices[EC_DEVICE_MAIN]);
    if (ret < 0) {
        goto err_fsm;
    }

    /* Enter idle phase */
    ret = ec_master_enter_idle_phase(master);
    if (ret < 0) {
        goto err_close;
    }

    masters[master_index] = master;
    EC_MASTER_INFO(master, "Initialized on %s\n", interface);
    return 0;

err_close:
    ec_device_close(&master->devices[EC_DEVICE_MAIN]);
err_fsm:
    ec_fsm_master_clear(&master->fsm);
    ec_datagram_clear(&master->fsm_datagram);
    for (i = 0; i < EC_EXT_RING_SIZE; i++) {
        ec_datagram_clear(&master->ext_datagram_ring[i]);
    }
err_device:
    ec_device_clear(&master->devices[EC_DEVICE_MAIN]);
err_free:
    ec_pal_free(master);
    return ret;
}

/****************************************************************************/

/**
 * Run idle processing.
 *
 * This function should be called periodically when the master is in IDLE phase.
 * It polls for received frames, executes the master FSM, and sends datagrams.
 *
 * \param master_index Master index.
 */
void ecrt_master_idle(unsigned int master_index)
{
    ec_master_t *master;

    if (master_index >= EC_MAX_MASTERS) {
        return;
    }

    master = masters[master_index];
    if (!master || master->phase != EC_IDLE) {
        return;
    }

    /* Poll for received frames */
    ec_device_poll(&master->devices[EC_DEVICE_MAIN]);

    /* Execute master FSM */
    if (ec_fsm_master_exec(&master->fsm)) {
        ec_master_queue_datagram(master, &master->fsm_datagram);
    }

    /* Send queued datagrams */
    ec_master_send_datagrams(master, EC_DEVICE_MAIN);

    /* Output statistics periodically */
    ec_master_output_stats(master);
}

/****************************************************************************/

/**
 * Cleanup a master instance.
 *
 * This is the userspace equivalent of unloading the kernel module.
 * It leaves idle phase, closes the device, and frees all resources.
 *
 * \param master_index Master index.
 */
void ecrt_master_cleanup(unsigned int master_index)
{
    ec_master_t *master;
    unsigned int i;

    if (master_index >= EC_MAX_MASTERS) {
        return;
    }

    master = masters[master_index];
    if (!master) {
        return;
    }

    EC_MASTER_INFO(master, "Cleaning up...\n");

    /* Leave idle phase */
    if (master->phase == EC_IDLE) {
        ec_master_leave_idle_phase(master);
    }

    /* Close device */
    ec_device_close(&master->devices[EC_DEVICE_MAIN]);

    /* Clear FSM */
    ec_fsm_master_clear(&master->fsm);

    /* Clear datagrams */
    ec_datagram_clear(&master->fsm_datagram);
    for (i = 0; i < EC_EXT_RING_SIZE; i++) {
        ec_datagram_clear(&master->ext_datagram_ring[i]);
    }

    /* Clear device */
    ec_device_clear(&master->devices[EC_DEVICE_MAIN]);

    /* Clear slaves */
    ec_master_clear_slaves(master);

    /* Free master */
    ec_pal_free(master);
    masters[master_index] = NULL;
}

/****************************************************************************/
