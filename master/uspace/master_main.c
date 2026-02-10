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
 * Userspace master management functions.
 */

/****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "pal.h"
#include "master.h"
#include "device.h"
#include "ecrt_user.h"

/****************************************************************************/

/* Global master array (like kernel module.c) */
static ec_master_t *masters[EC_MAX_MASTERS];
static unsigned int master_count = 0;

/* Forward declarations from master.c */
int ec_master_enter_idle_phase(ec_master_t *);
void ec_master_leave_idle_phase(ec_master_t *);
void ec_master_clear_slaves(ec_master_t *);
void ec_master_set_send_interval(ec_master_t *, unsigned int);
void ec_master_internal_send_cb(void *);
void ec_master_internal_receive_cb(void *);
void ec_fsm_master_init(ec_fsm_master_t *, ec_master_t *, ec_datagram_t *);
void ec_fsm_master_clear(ec_fsm_master_t *);
int ec_fsm_master_exec(ec_fsm_master_t *);
void ec_master_exec_slave_fsms(ec_master_t *);
void ec_master_send_datagrams(ec_master_t *, ec_device_index_t);
void ec_master_queue_datagram(ec_master_t *, ec_datagram_t *);
void ec_master_output_stats(ec_master_t *);

/* Device function declaration */
void ec_device_set_interface(ec_device_t *, const char *);

/****************************************************************************/

/**
 * Initialize a master instance (userspace).
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
        return -EBUSY;  /* Already initialized */
    }

    /* Allocate master */
    master = calloc(1, sizeof(ec_master_t));
    if (!master) {
        return -ENOMEM;
    }

    /* Initialize master structure */
    master->index = master_index;
    master->phase = EC_ORPHANED;
    master->debug_level = 1;  /* Default debug level */
    
    /* Initialize lists */
    INIT_LIST_HEAD(&master->configs);
    INIT_LIST_HEAD(&master->domains);
    INIT_LIST_HEAD(&master->datagram_queue);
    INIT_LIST_HEAD(&master->ext_datagram_queue);
    INIT_LIST_HEAD(&master->sii_requests);
    INIT_LIST_HEAD(&master->emerg_reg_requests);
    INIT_LIST_HEAD(&master->fsm_exec_list);
#ifdef EC_EOE
    INIT_LIST_HEAD(&master->eoe_handlers);
#endif

    /* Initialize platform-specific fields */
    sem_init(&master->plat.master_sem, 0, 1);
    sem_init(&master->plat.device_sem, 0, 1);
    sem_init(&master->plat.scan_sem, 0, 1);
    sem_init(&master->plat.config_sem, 0, 1);
    sem_init(&master->plat.ext_queue_sem, 0, 1);
    pthread_mutex_init(&master->plat.io_mutex, NULL);
    pthread_cond_init(&master->plat.scan_cond, NULL);
    pthread_cond_init(&master->plat.config_cond, NULL);
    pthread_cond_init(&master->plat.request_cond, NULL);

    /* Initialize device */
    ret = ec_device_init(&master->devices[EC_DEVICE_MAIN], master);
    if (ret < 0) {
        goto err_free_master;
    }

    /* Set interface name on transport */
    ec_device_set_interface(&master->devices[EC_DEVICE_MAIN], interface);

    /* Initialize FSM datagram */
    ec_datagram_init(&master->fsm_datagram);
    snprintf(master->fsm_datagram.name, EC_DATAGRAM_NAME_SIZE, "master-fsm");
    ret = ec_datagram_prealloc(&master->fsm_datagram, EC_MAX_DATA_SIZE);
    if (ret < 0) {
        goto err_clear_device;
    }

    /* Initialize external datagram ring */
    for (i = 0; i < EC_EXT_RING_SIZE; i++) {
        ec_datagram_init(&master->ext_datagram_ring[i]);
        snprintf(master->ext_datagram_ring[i].name, EC_DATAGRAM_NAME_SIZE, 
                 "ext-%u", i);
    }

    /* Initialize master FSM */
    ec_fsm_master_init(&master->fsm, master, &master->fsm_datagram);

    /* Set default send interval */
    ec_master_set_send_interval(master, 1000);  /* 1ms default */

    /* Set internal callbacks */
    master->send_cb = ec_master_internal_send_cb;
    master->receive_cb = ec_master_internal_receive_cb;
    master->cb_data = master;

    /* Open device */
    ret = ec_device_open(&master->devices[EC_DEVICE_MAIN]);
    if (ret < 0) {
        goto err_clear_fsm;
    }

    /* Enter idle phase */
    ret = ec_master_enter_idle_phase(master);
    if (ret < 0) {
        goto err_close_device;
    }

    masters[master_index] = master;
    if (master_index >= master_count) {
        master_count = master_index + 1;
    }

    EC_MASTER_INFO(master, "Master initialized on %s\n", interface);
    return 0;

err_close_device:
    ec_device_close(&master->devices[EC_DEVICE_MAIN]);
err_clear_fsm:
    ec_fsm_master_clear(&master->fsm);
    ec_datagram_clear(&master->fsm_datagram);
    for (i = 0; i < EC_EXT_RING_SIZE; i++) {
        ec_datagram_clear(&master->ext_datagram_ring[i]);
    }
err_clear_device:
    ec_device_clear(&master->devices[EC_DEVICE_MAIN]);
err_free_master:
    free(master);
    return ret;
}

/****************************************************************************/

/**
 * Cleanup a master instance (userspace).
 */
void ecrt_master_cleanup(unsigned int master_index)
{
    ec_master_t *master;
    unsigned int i;

    if (master_index >= EC_MAX_MASTERS || !masters[master_index]) {
        return;
    }

    master = masters[master_index];

    EC_MASTER_INFO(master, "Shutting down...\n");

    /* Leave idle phase */
    ec_master_leave_idle_phase(master);

    /* Close device */
    ec_device_close(&master->devices[EC_DEVICE_MAIN]);

    /* Clear FSM */
    ec_fsm_master_clear(&master->fsm);
    ec_datagram_clear(&master->fsm_datagram);
    for (i = 0; i < EC_EXT_RING_SIZE; i++) {
        ec_datagram_clear(&master->ext_datagram_ring[i]);
    }

    /* Clear device */
    ec_device_clear(&master->devices[EC_DEVICE_MAIN]);

    /* Clear slaves */
    ec_master_clear_slaves(master);

    /* Destroy platform-specific fields */
    sem_destroy(&master->plat.master_sem);
    sem_destroy(&master->plat.device_sem);
    sem_destroy(&master->plat.scan_sem);
    sem_destroy(&master->plat.config_sem);
    sem_destroy(&master->plat.ext_queue_sem);
    pthread_mutex_destroy(&master->plat.io_mutex);
    pthread_cond_destroy(&master->plat.scan_cond);
    pthread_cond_destroy(&master->plat.config_cond);
    pthread_cond_destroy(&master->plat.request_cond);

    free(master);
    masters[master_index] = NULL;

    EC_MASTER_INFO(master, "Master released.\n");
}

/****************************************************************************/

/**
 * Run idle processing (call periodically when not in OPERATION).
 */
void ecrt_master_idle(unsigned int master_index)
{
    ec_master_t *master;

    if (master_index >= EC_MAX_MASTERS || !masters[master_index]) {
        return;
    }

    master = masters[master_index];

    if (master->phase != EC_IDLE) {
        return;
    }

    /* This is the userspace equivalent of ec_master_idle_thread() */
    
    /* Receive datagrams */
    ec_device_poll(&master->devices[EC_DEVICE_MAIN]);

    /* Execute master FSM */
    if (ec_fsm_master_exec(&master->fsm)) {
        /* FSM is active, send datagram */
        ec_master_queue_datagram(master, &master->fsm_datagram);
    }

    /* Process slave FSMs */
    ec_master_exec_slave_fsms(master);

    /* Send queued datagrams */
    ec_master_send_datagrams(master, EC_DEVICE_MAIN);

    /* Output statistics periodically */
    ec_master_output_stats(master);
}

/****************************************************************************/

/**
 * Process control interface requests (for CLI tool).
 */
int ecrt_master_process_control(unsigned int master_index)
{
    /* TODO: Implement control socket for CLI tool communication */
    /* For now, this is a stub */
    (void)master_index;
    return 0;
}

/****************************************************************************/
