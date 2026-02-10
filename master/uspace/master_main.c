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
 * Includes the main() entry point and CLI for the ec_master daemon.
 */

/****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

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

/** Datagram timeout in microseconds (userspace). */
#define EC_DATAGRAM_TIMEOUT_US 10000

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

/** Receive and process datagrams (userspace simplified version).
 *
 * Parses received EtherCAT frames and updates matching datagrams.
 */
void ec_master_receive_datagrams(
        ec_master_t *master,
        ec_device_t *device,
        const uint8_t *frame_data,
        size_t size
        )
{
    size_t frame_size, data_size;
    uint8_t datagram_type, datagram_index;
    unsigned int cmd_follows, matched;
    const uint8_t *cur_data;
    ec_datagram_t *datagram;

    /* Check minimum frame size */
    if (size < EC_FRAME_HEADER_SIZE) {
        return;
    }

    cur_data = frame_data;

    /* Check length of entire frame */
    frame_size = EC_READ_U16(cur_data) & 0x07FF;
    cur_data += EC_FRAME_HEADER_SIZE;

    if (frame_size > size) {
        return;
    }

    cmd_follows = 1;
    while (cmd_follows) {
        /* Check if we have enough data for datagram header */
        if (cur_data - frame_data + EC_DATAGRAM_HEADER_SIZE > size) {
            break;
        }

        /* Process datagram header */
        datagram_type  = EC_READ_U8(cur_data);
        datagram_index = EC_READ_U8(cur_data + 1);
        data_size      = EC_READ_U16(cur_data + 6) & 0x07FF;
        cmd_follows    = EC_READ_U16(cur_data + 6) & 0x8000;
        cur_data += EC_DATAGRAM_HEADER_SIZE;

        /* Check if we have enough data for payload and footer */
        if (cur_data - frame_data + data_size + EC_DATAGRAM_FOOTER_SIZE > size) {
            break;
        }

        /* Search for matching datagram in the queue */
        matched = 0;
        list_for_each_entry(datagram, &master->datagram_queue, queue) {
            if (datagram->index == datagram_index
                && datagram->state == EC_DATAGRAM_SENT
                && datagram->type == datagram_type
                && datagram->data_size == data_size) {
                matched = 1;
                break;
            }
        }

        /* No matching datagram was found */
        if (!matched) {
            cur_data += data_size + EC_DATAGRAM_FOOTER_SIZE;
            continue;
        }

        /* Copy received data into the datagram memory for read operations */
        if (datagram->type != EC_DATAGRAM_APWR &&
            datagram->type != EC_DATAGRAM_FPWR &&
            datagram->type != EC_DATAGRAM_BWR &&
            datagram->type != EC_DATAGRAM_LWR) {
            memcpy(datagram->data, cur_data, data_size);
        }
        cur_data += data_size;

        /* Set the datagram's working counter */
        datagram->working_counter = EC_READ_U16(cur_data);
        cur_data += EC_DATAGRAM_FOOTER_SIZE;

        /* Dequeue the received datagram */
        datagram->state = EC_DATAGRAM_RECEIVED;
        datagram->jiffies_received = device->plat.jiffies_poll;
        list_del_init(&datagram->queue);
    }
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
        if (datagram->state == EC_DATAGRAM_INIT) {
            datagram->state = EC_DATAGRAM_QUEUED;
        }
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
        /* Only send datagrams that are queued but not yet sent */
        if (datagram->state != EC_DATAGRAM_QUEUED && 
            datagram->state != EC_DATAGRAM_INIT) {
            continue;
        }
        
        /* Get transmit buffer */
        frame_data = ec_device_tx_data(&master->devices[EC_DEVICE_MAIN]);
        if (!frame_data) {
            datagram->state = EC_DATAGRAM_ERROR;
            list_del_init(&datagram->queue);
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
        /* Keep datagram in queue until received or timed out */
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

/** Check for datagram timeouts.
 *
 * Checks all queued datagrams and marks timed-out ones.
 */
void ec_master_check_timeouts(
        ec_master_t *master
        )
{
    ec_datagram_t *datagram;
    unsigned long now = ec_pal_jiffies();
    unsigned long timeout_jiffies = (EC_DATAGRAM_TIMEOUT_US * ec_pal_hz()) / 1000000UL;
    
    /* Ensure minimum timeout of 1 jiffy */
    if (timeout_jiffies < 1) {
        timeout_jiffies = 1;
    }
    
    list_for_each_entry(datagram, &master->datagram_queue, queue) {
        if (datagram->state == EC_DATAGRAM_SENT) {
            if (ec_pal_time_after(now, datagram->jiffies_sent + timeout_jiffies)) {
                datagram->state = EC_DATAGRAM_TIMED_OUT;
                list_del_init(&datagram->queue);
            }
        }
    }
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

    /* Check for datagram timeouts */
    ec_master_check_timeouts(master);

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

/* CLI and Main Function */

/****************************************************************************/

static volatile int running = 1;

static struct option long_options[] = {
    {"interface",  required_argument, 0, 'i'},
    {"transport",  required_argument, 0, 't'},
    {"debug",      required_argument, 0, 'd'},
    {"verbose",    no_argument,       0, 'v'},
    {"help",       no_argument,       0, 'h'},
    {0, 0, 0, 0}
};

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("EtherCAT Master Userspace Daemon\n");
    printf("\n");
    printf("Runs an EtherCAT master in userspace, equivalent to loading\n");
    printf("the kernel module. The ethercat CLI tool can connect to query\n");
    printf("slave information.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -i, --interface IFACE   Network interface (required, e.g., eth0)\n");
    printf("  -t, --transport TYPE    Transport type: raw, xdp (default: raw)\n");
    printf("  -d, --debug LEVEL       Debug level (0-2, default: 1)\n");
    printf("  -v, --verbose           Verbose output\n");
    printf("  -h, --help              Show this help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s -i eth0\n", prog);
    printf("  %s -i enp0s31f6 -t raw -v\n", prog);
    printf("\n");
}

int main(int argc, char *argv[])
{
    const char *interface = NULL;
    const char *transport = "raw";
    int verbose = 0;
    int debug_level = 1;
    int opt;
    ec_pal_device_type_t device_type;
    int ret;

    /* Parse command line arguments */
    while ((opt = getopt_long(argc, argv, "i:t:d:vh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'i':
            interface = optarg;
            break;
        case 't':
            transport = optarg;
            break;
        case 'd':
            debug_level = atoi(optarg);
            break;
        case 'v':
            verbose = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!interface) {
        fprintf(stderr, "Error: Network interface required (-i option)\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Determine device/transport type */
    if (strcmp(transport, "raw") == 0) {
        device_type = EC_PAL_DEVICE_RAW;
    } else if (strcmp(transport, "xdp") == 0) {
        device_type = EC_PAL_DEVICE_XDP;
    } else {
        fprintf(stderr, "Error: Unknown transport type '%s'\n", transport);
        fprintf(stderr, "Supported types: raw, xdp\n");
        return 1;
    }

    /* Install signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (verbose) {
        printf("EtherCAT Master Userspace Daemon\n");
        printf("Interface: %s\n", interface);
        printf("Transport: %s\n", transport);
        printf("Debug level: %d\n", debug_level);
        printf("\n");
        printf("Initializing master...\n");
    }

    /* Initialize master */
    ret = ecrt_master_init(0, device_type, interface);
    if (ret < 0) {
        fprintf(stderr, "Failed to initialize master: %s\n", strerror(-ret));
        return 1;
    }

    /* Set debug level */
    if (masters[0]) {
        masters[0]->debug_level = debug_level;
    }

    if (verbose) {
        printf("Master initialized successfully.\n");
        printf("Running. Press Ctrl+C to stop.\n");
        printf("\n");
    }

    /* Main loop: run idle processing */
    while (running) {
        /* Process idle state machine (bus scanning, etc.) */
        ecrt_master_idle(0);

        /* Sleep briefly to avoid busy-waiting */
        usleep(10000);  /* 10ms idle cycle */
    }

    if (verbose) {
        printf("\nShutting down...\n");
    }

    /* Cleanup */
    ecrt_master_cleanup(0);

    if (verbose) {
        printf("Master stopped.\n");
    }

    return 0;
}

/****************************************************************************/
