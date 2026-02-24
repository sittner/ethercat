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
   Userspace EtherCAT master standalone executable main.
*/

/****************************************************************************/

#include "pal.h"

#include "../device.h"
#include "../master.h"
#include "../fsm_master.h"

#include "transport/ec_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>

/****************************************************************************/

/** Global variable to control main loop */
static volatile int g_running = 1;

/****************************************************************************/

/** Signal handler for clean shutdown */
static void signal_handler(int signum)
{
    (void)signum;
    g_running = 0;
}

/****************************************************************************/

int main(int argc, char *argv[])
{
    const char *interface = NULL;
    ec_transport_type_t transport_type = EC_TRANSPORT_RAW;  /* DEFAULT: raw */
    ec_transport_t *transport = NULL;
    ec_master_t master;
    int ret = 0;
    int c;

    /* Parse command line arguments */
    static struct option long_options[] = {
        {"interface", required_argument, 0, 'i'},
        {"transport", required_argument, 0, 't'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "i:t:h", long_options, NULL)) != -1) {
        switch (c) {
            case 'i':
                interface = optarg;
                break;
            case 't':
                ret = ec_transport_find_by_name(optarg);
                if (ret < 0) {
                    fprintf(stderr, "Unknown transport type: %s\n", optarg);
                    fprintf(stderr, "Available transports: ");
                    ec_transport_print_available();
                    fprintf(stderr, "\n");
                    return 1;
                }
                transport_type = ret;
                ret = 0;
                break;
            case 'h':
                fprintf(stdout, "Usage: %s [OPTIONS]\n", argv[0]);
                fprintf(stdout, "Options:\n");
                fprintf(stdout, "  -i, --interface <name>    Network interface (required)\n");
                fprintf(stdout, "  -t, --transport <type>    Transport type (default: raw)\n");
                fprintf(stdout, "                            Available: ");
                fflush(stdout);  /* Flush before stderr to ensure correct output order */
                ec_transport_print_available();
                fprintf(stdout, "\n");
                fprintf(stdout, "  -h, --help                Show this help\n");
                return 0;
            default:
                fprintf(stderr, "Usage: %s -i <interface> [-t <transport>]\n", argv[0]);
                return 1;
        }
    }

    if (!interface) {
        fprintf(stderr, "Error: Network interface required (-i option)\n");
        fprintf(stderr, "Usage: %s -i <interface> [-t <transport>]\n", argv[0]);
        return 1;
    }

    ec_log(EC_LOG_INFO, "Starting EtherCAT master on interface %s\n", interface);

    ec_master_init_static();

    /* Initialize workqueues */
    if (ec_pal_work_init() != 0) {
        ec_log(EC_LOG_ERR, "Failed to create system workqueue\n");
        return 1;
    }

    if (ec_pal_irq_work_init() != 0) {
        ec_log(EC_LOG_ERR, "Failed to create IRQ work queue\n");
        ec_pal_work_cleanup();
        return 1;
    }

    ec_log(EC_LOG_INFO, "Using transport: %s\n", ec_transport_get_name(transport_type));

    /* Create and open transport */
    transport = ec_transport_create(transport_type);
    if (!transport) {
        ec_log(EC_LOG_ERR, "Failed to create transport\n");
        ret = 1;
        goto out_cleanup_queues;
    }

    ret = ec_transport_open(transport, interface);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to open transport on %s: %d\n", interface, ret);
        ret = 1;
        goto out_destroy_transport;
    }

    ec_log(EC_LOG_INFO, "Transport opened successfully\n");

    // TODO: check if this is the correct way
    uint8_t main_mac[ETH_ALEN] = {0};
    uint8_t backup_mac[ETH_ALEN] = {0};

    /* Get MAC address from transport */
    ec_transport_get_mac(transport, main_mac);

    /* Initialize master */
    ret = ec_master_init(&master, 0, main_mac, backup_mac, 1, 0);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to initialize master: %d\n", ret);
        ret = 1;
        goto out_close_transport;
    }

    /* Store transport reference in device */
    master.devices[EC_DEVICE_MAIN].pal.transport = transport;
    master.devices[EC_DEVICE_MAIN].name = interface;  /* Safe: interface from argv remains valid */

    /* Open device */
    ret = ec_device_open(&master.devices[EC_DEVICE_MAIN]);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to open device: %d\n", ret);
        ret = 1;
        goto out_clear_master;
    }

    /* Set up signal handlers for clean shutdown */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    ec_log(EC_LOG_INFO, "EtherCAT master started, entering main loop\n");

    /* Enter idle phase */
    ret = ec_master_enter_idle_phase(&master);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to enter idle phase: %d\n", ret);
        ret = 1;
        goto out_close_device;
    }

    /* Main loop - run until signal received */
    while (g_running) {
        pause();  /* Sleep until signal - uses ~0% CPU */
    }

    ec_log(EC_LOG_INFO, "Shutting down EtherCAT master\n");

    /* Leave idle phase */
    ec_master_leave_idle_phase(&master);

out_close_device:
    ec_device_close(&master.devices[EC_DEVICE_MAIN]);

out_clear_master:
    ec_master_clear(&master);

out_close_transport:
    ec_transport_close(transport);

out_destroy_transport:
    ec_transport_destroy(transport);

out_cleanup_queues:
    ec_pal_irq_work_cleanup();
    ec_pal_work_cleanup();

    ec_log(EC_LOG_INFO, "EtherCAT master stopped\n");
    return ret;
}

/****************************************************************************/
/* Device functions - userspace implementation */
/****************************************************************************/

/** Initialize device structure. */
int ec_device_init(ec_device_t *device, ec_master_t *master)
{
    ec_device_init_common(device, master);

    /* Initialize PAL-specific fields */
    device->pal.transport = NULL;
    device->pal.last_link_check = 0;
    device->pal.last_link_state = -1;

    return 0;
}

/** Clear device structure. */
void ec_device_clear(ec_device_t *device)
{
    ec_device_clear_common(device);
    /* Transport is managed by main(), just clear the reference */
    device->pal.transport = NULL;
}

/** Get pointer to transmit buffer. */
uint8_t *ec_device_tx_data(ec_device_t *device)
{
    if (!device->pal.transport) {
        return NULL;
    }
    
    /* Get TX buffer from transport, skip Ethernet header */
    return ec_transport_get_tx_buffer(device->pal.transport) + ETH_HLEN;
}

/** Send frame. */
void ec_device_send(ec_device_t *device, size_t size)
{
    int ret;

    if (!device->pal.transport) {
        return;
    }

    /* Send frame via transport layer */
    ret = ec_transport_send(device->pal.transport, size + ETH_HLEN);
    if (ret < 0) {
        ec_device_account_tx_error(device);
        return;
    }

    /* Update statistics */
    ec_device_account_tx(device, ETH_HLEN + size);
}

/** Poll for received frames. */
void ec_device_poll(ec_device_t *device)
{
    uint8_t rx_buffer[ETH_FRAME_LEN];
    int received;

    device->time_poll = ec_current_time();

    if (!device->pal.transport) {
        return;
    }

    /* Poll transport layer for received frames */
    while ((received = ec_transport_receive(device->pal.transport, rx_buffer, sizeof(rx_buffer))) > 0) {
        /* Update RX statistics */
        ec_device_account_rx(device, received);

        /* Process received frame - skip Ethernet header */
        if (received > ETH_HLEN) {
            ec_master_receive_datagrams(
                device->master,
                device,
                rx_buffer + ETH_HLEN,
                received - ETH_HLEN
            );
        }
    }

    /* Periodically update link state */
    if (device->time_poll > device->pal.last_link_check + ec_ms_to_time(1000)) {
        int link_state = ec_transport_get_link_state(device->pal.transport);
        if (link_state >= 0 && link_state != device->pal.last_link_state) {
            device->link_state = (uint8_t)link_state;
            device->pal.last_link_state = link_state;
            if (link_state) {
                ec_log(EC_LOG_INFO, "Device %s: Link is up\n", device->name ? device->name : "?");
            } else {
                ec_log(EC_LOG_WARNING, "Device %s: Link is down\n", device->name ? device->name : "?");
            }
        }
        device->pal.last_link_check = device->time_poll;
    }
}

/** Open device. */
int ec_device_open(ec_device_t *device)
{
    uint8_t *tx_buffer;

    /* Transport is already opened in main(), just set state */
    device->open = 1;
    device->link_state = 0;
    ec_device_clear_stats(device);
    
    /* Initialize Ethernet header in TX buffer */
    tx_buffer = ec_transport_get_tx_buffer(device->pal.transport);
    if (tx_buffer) {
        /* Fill Ethernet header (matches kernel ec_device_init + ec_device_attach) */
        memset(tx_buffer, 0xFF, ETH_ALEN);                                    /* h_dest: broadcast */
        memcpy(tx_buffer + ETH_ALEN, device->master->macs[EC_DEVICE_MAIN], ETH_ALEN);  /* h_source: our MAC */
        tx_buffer[12] = 0x88;                                                 /* h_proto: EtherCAT (0x88A4) */
        tx_buffer[13] = 0xA4;
    }

    ec_log(EC_LOG_INFO, "Device %s opened\n", device->name ? device->name : "?");
    return 0;
}

/** Close device. */
int ec_device_close(ec_device_t *device)
{
    /* Transport is closed in main(), just set state */
    device->open = 0;
    
    ec_log(EC_LOG_INFO, "Device %s closed\n", device->name ? device->name : "?");
    return 0;
}

