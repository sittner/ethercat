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
 * Userspace master management implementation.
 */

/****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "pal.h"
#include "globals.h"
#include "master.h"
#include "device.h"
#include "fsm_master.h"
#include "datagram.h"
#include "ecrt_user.h"
#include "ecrt.h"  /* For EC_WRITE_U8, EC_WRITE_U16 macros */
#include "transport/ec_transport.h"

/****************************************************************************/

static ec_master_t *masters[EC_MAX_MASTERS];

/****************************************************************************/

/* Forward declarations for userspace-specific implementations */
void ec_device_set_interface(ec_device_t *device, const char *interface);
void ec_device_attach(ec_device_t *device, ec_transport_t *transport);

/* Userspace implementations of kernel master functions */

/* Stub FSM functions for userspace - will be expanded later */
void ec_fsm_master_init(
        ec_fsm_master_t *fsm,
        ec_master_t *master,
        ec_datagram_t *datagram
        )
{
    fsm->master = master;
    fsm->datagram = datagram;
    fsm->idle = 1;
    /* Full FSM implementation will be added in future PR */
}

void ec_fsm_master_clear(
        ec_fsm_master_t *fsm
        )
{
    (void)fsm; /* Stub */
}

int ec_fsm_master_exec(
        ec_fsm_master_t *fsm
        )
{
    (void)fsm;
    /* Stub - returns 0 = no datagram to send */
    return 0;
}

/** Internal sending callback (userspace).
 */
void ec_master_internal_send_cb(
        void *cb_data
        )
{
    ec_master_t *master = (ec_master_t *) cb_data;
    (void)master; /* Currently unused in userspace */
}

/** Internal receiving callback (userspace).
 */
void ec_master_internal_receive_cb(
        void *cb_data
        )
{
    ec_master_t *master = (ec_master_t *) cb_data;
    (void)master; /* Currently unused in userspace */
}

/** Queue datagram for sending.
 */
void ec_master_queue_datagram(
        ec_master_t *master,
        ec_datagram_t *datagram
        )
{
    /* Simple implementation - add to queue */
    if (list_empty(&datagram->queue)) {
        list_add_tail(&datagram->queue, &master->datagram_queue);
    }
}

/** Send all queued datagrams (userspace).
 */
int ecrt_master_send(
        ec_master_t *master
        )
{
    ec_datagram_t *datagram, *next;
    uint8_t *frame_data, *cur_data;
    size_t datagram_size;
    
    /* Simple implementation - send each datagram in its own frame */
    list_for_each_entry_safe(datagram, next, &master->datagram_queue, queue) {
        /* Remove from queue */
        list_del_init(&datagram->queue);
        
        /* Get transmit buffer */
        frame_data = ec_device_tx_data(&master->devices[EC_DEVICE_MAIN]);
        if (!frame_data) {
            datagram->state = EC_DATAGRAM_ERROR;
            continue;
        }
        
        cur_data = frame_data;
        
        /* EtherCAT frame header (2 bytes) */
        datagram_size = EC_DATAGRAM_HEADER_SIZE + datagram->data_size + EC_DATAGRAM_FOOTER_SIZE;
        EC_WRITE_U16(cur_data, (datagram_size & 0x7FF) | 0x1000);
        cur_data += EC_FRAME_HEADER_SIZE;
        
        /* EtherCAT datagram header (10 bytes) */
        EC_WRITE_U8(cur_data, datagram->type);
        EC_WRITE_U8(cur_data + 1, datagram->index);
        memcpy(cur_data + 2, datagram->address, EC_ADDR_LEN);
        EC_WRITE_U16(cur_data + 6, datagram->data_size & 0x7FF);
        EC_WRITE_U16(cur_data + 8, 0x0000);
        cur_data += EC_DATAGRAM_HEADER_SIZE;
        
        /* EtherCAT datagram data */
        memcpy(cur_data, datagram->data, datagram->data_size);
        cur_data += datagram->data_size;
        
        /* EtherCAT datagram footer (2 bytes) */
        EC_WRITE_U16(cur_data, 0x0000);  /* reset working counter */
        cur_data += EC_DATAGRAM_FOOTER_SIZE;
        
        /* Send frame */
        ec_device_send(&master->devices[EC_DEVICE_MAIN], cur_data - frame_data);
        
        datagram->state = EC_DATAGRAM_SENT;
        datagram->jiffies_sent = ec_pal_jiffies();
    }
    
    return 0;
}

/** Output statistics (userspace stub).
 */
void ec_master_output_stats(
        ec_master_t *master
        )
{
    /* Statistics output - simplified for userspace */
    /* Could be expanded later for periodic logging */
    (void)master;
}

/** Clear all slaves.
 */
void ec_master_clear_slaves(
        ec_master_t *master
        )
{
    if (master->slaves) {
        ec_pal_free(master->slaves);
        master->slaves = NULL;
        master->slave_count = 0;
    }
}

/****************************************************************************/

/** Initialize a master instance.
 *
 * \param master_index Master index (0, 1, ...)
 * \param device_type Transport type (raw, xdp)
 * \param interface Network interface name
 * \return 0 on success, < 0 on error
 */
int ecrt_master_init(unsigned int master_index,
                     ec_pal_device_type_t device_type,
                     const char *interface)
{
    ec_master_t *master;
    ec_transport_t *transport;
    ec_transport_type_t transport_type;
    int ret;
    unsigned int i;

    if (master_index >= EC_MAX_MASTERS) {
        EC_PAL_ERR("Master index %u out of range (max %u)\n", 
                   master_index, EC_MAX_MASTERS);
        return -EINVAL;
    }

    if (masters[master_index]) {
        EC_PAL_ERR("Master %u already initialized\n", master_index);
        return -EBUSY;
    }

    /* Map device type to transport type */
    switch (device_type) {
    case EC_PAL_DEVICE_RAW:
        transport_type = EC_TRANSPORT_RAW;
        break;
    case EC_PAL_DEVICE_XDP:
        transport_type = EC_TRANSPORT_XDP;
        break;
    default:
        transport_type = EC_TRANSPORT_RAW;
        break;
    }

    /* Create transport */
    transport = ec_transport_create(transport_type);
    if (!transport) {
        EC_PAL_ERR("Failed to create transport\n");
        return -ENOMEM;
    }

    /* Open transport on interface */
    ret = ec_transport_open(transport, interface);
    if (ret < 0) {
        EC_PAL_ERR("Failed to open transport on %s: %d\n", interface, ret);
        ec_transport_destroy(transport);
        return ret;
    }

    master = ec_pal_zalloc(sizeof(ec_master_t));
    if (!master) {
        EC_PAL_ERR("Failed to allocate master\n");
        ec_transport_close(transport);
        ec_transport_destroy(transport);
        return -ENOMEM;
    }

    master->index = master_index;
    master->phase = EC_ORPHANED;
    master->debug_level = 1;
    master->send_interval = 1000000;  /* 1ms in ns */

    /* Initialize lists */
    INIT_LIST_HEAD(&master->configs);
    INIT_LIST_HEAD(&master->domains);
    INIT_LIST_HEAD(&master->datagram_queue);
    INIT_LIST_HEAD(&master->ext_datagram_queue);
    INIT_LIST_HEAD(&master->sii_requests);
    INIT_LIST_HEAD(&master->emerg_reg_requests);
    INIT_LIST_HEAD(&master->fsm_exec_list);

    /* Initialize platform semaphores and wait queues */
    ec_pal_sem_init(&master->plat.master_sem, 1);
    ec_pal_sem_init(&master->plat.device_sem, 1);
    ec_pal_sem_init(&master->plat.scan_sem, 1);
    ec_pal_sem_init(&master->plat.config_sem, 1);
    ec_pal_sem_init(&master->plat.ext_queue_sem, 1);
    ec_pal_mutex_init(&master->plat.io_mutex);
    ec_pal_wait_queue_init(&master->plat.scan_queue);
    ec_pal_wait_queue_init(&master->plat.config_queue);
    ec_pal_wait_queue_init(&master->plat.request_queue);

    /* Initialize main device */
    ret = ec_device_init(&master->devices[EC_DEVICE_MAIN], master);
    if (ret < 0) {
        EC_PAL_ERR("Failed to init device: %d\n", ret);
        goto err_free_transport;
    }

    /* Attach transport to device */
    ec_device_attach(&master->devices[EC_DEVICE_MAIN], transport);

    /* Set interface name */
    ec_device_set_interface(&master->devices[EC_DEVICE_MAIN], interface);

    /* Initialize FSM datagram */
    ec_datagram_init(&master->fsm_datagram);
    snprintf(master->fsm_datagram.name, EC_DATAGRAM_NAME_SIZE, "master-fsm");
    ret = ec_datagram_prealloc(&master->fsm_datagram, EC_MAX_DATA_SIZE);
    if (ret < 0) {
        EC_PAL_ERR("Failed to alloc FSM datagram\n");
        goto err_device;
    }

    /* Initialize external datagram ring */
    for (i = 0; i < EC_EXT_RING_SIZE; i++) {
        ec_datagram_init(&master->ext_datagram_ring[i]);
        snprintf(master->ext_datagram_ring[i].name, 
                 EC_DATAGRAM_NAME_SIZE, "ext-%u", i);
    }

    /* Initialize master FSM */
    ec_fsm_master_init(&master->fsm, master, &master->fsm_datagram);

    /* Set callbacks */
    master->send_cb = ec_master_internal_send_cb;
    master->receive_cb = ec_master_internal_receive_cb;
    master->cb_data = master;

    /* Open device (creates socket, binds to interface) */
    ret = ec_device_open(&master->devices[EC_DEVICE_MAIN]);
    if (ret < 0) {
        EC_PAL_ERR("Failed to open device on %s: %d\n", interface, ret);
        goto err_fsm;
    }

    /* Enter idle phase - enables scanning */
    master->phase = EC_IDLE;
    master->allow_scan = 1;
    master->scan_busy = 0;
    master->config_busy = 0;

    masters[master_index] = master;
    EC_MASTER_INFO(master, "Initialized on %s\n", interface);
    
    return 0;

err_fsm:
    ec_fsm_master_clear(&master->fsm);
    ec_datagram_clear(&master->fsm_datagram);
    for (i = 0; i < EC_EXT_RING_SIZE; i++)
        ec_datagram_clear(&master->ext_datagram_ring[i]);
err_device:
    ec_device_clear(&master->devices[EC_DEVICE_MAIN]);
err_free_transport:
    ec_transport_close(transport);
    ec_transport_destroy(transport);
    ec_pal_free(master);
    return ret;
}

/****************************************************************************/

/** Run idle processing.
 *
 * \param master_index Master index
 */
void ecrt_master_idle(unsigned int master_index)
{
    ec_master_t *master;

    if (master_index >= EC_MAX_MASTERS)
        return;
    
    master = masters[master_index];
    if (!master || master->phase != EC_IDLE)
        return;

    /* Receive any pending frames */
    ec_device_poll(&master->devices[EC_DEVICE_MAIN]);

    /* Execute master FSM (handles scanning, etc.) */
    if (ec_fsm_master_exec(&master->fsm)) {
        ec_master_queue_datagram(master, &master->fsm_datagram);
    }

    /* Send queued datagrams */
    ecrt_master_send(master);

    /* Update and output statistics periodically */
    ec_master_output_stats(master);
}

/****************************************************************************/

/** Cleanup a master instance.
 *
 * \param master_index Master index
 */
void ecrt_master_cleanup(unsigned int master_index)
{
    ec_master_t *master;
    unsigned int i;

    if (master_index >= EC_MAX_MASTERS)
        return;

    master = masters[master_index];
    if (!master)
        return;

    EC_MASTER_INFO(master, "Shutting down...\n");

    master->phase = EC_ORPHANED;

    /* Close device */
    ec_device_close(&master->devices[EC_DEVICE_MAIN]);

    /* Clear FSM */
    ec_fsm_master_clear(&master->fsm);
    ec_datagram_clear(&master->fsm_datagram);
    for (i = 0; i < EC_EXT_RING_SIZE; i++)
        ec_datagram_clear(&master->ext_datagram_ring[i]);

    /* Clear device */
    ec_device_clear(&master->devices[EC_DEVICE_MAIN]);

    /* Clear any slaves that were found */
    ec_master_clear_slaves(master);

    ec_pal_free(master);
    masters[master_index] = NULL;
}

/****************************************************************************/

/** Process control interface requests.
 *
 * Stub for future CLI tool communication.
 *
 * \param master_index Master index
 * \return 0 on success, < 0 on error
 */
int ecrt_master_process_control(unsigned int master_index)
{
    /* Stub for future CLI tool communication */
    (void)master_index;
    return 0;
}

/****************************************************************************/
