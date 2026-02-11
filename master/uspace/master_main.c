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
   Userspace EtherCAT master main.
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
    ec_transport_t *transport = NULL;
    ec_master_t master;
    int ret = 0;
    int c;

    /* Parse command line arguments */
    static struct option long_options[] = {
        {"interface", required_argument, 0, 'i'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "i:h", long_options, NULL)) != -1) {
        switch (c) {
            case 'i':
                interface = optarg;
                break;
            case 'h':
                printf("Usage: %s -i <interface>\n", argv[0]);
                printf("  -i, --interface <name>   Network interface (required)\n");
                printf("  -h, --help               Show this help\n");
                return 0;
            default:
                fprintf(stderr, "Usage: %s -i <interface>\n", argv[0]);
                return 1;
        }
    }

    if (!interface) {
        fprintf(stderr, "Error: Network interface required (-i option)\n");
        fprintf(stderr, "Usage: %s -i <interface>\n", argv[0]);
        return 1;
    }

    printk(KERN_INFO "Starting EtherCAT master on interface %s\n", interface);

    /* Initialize workqueues */
    system_wq = create_workqueue("system_wq");
    if (!system_wq) {
        printk(KERN_ERR "Failed to create system workqueue\n");
        return 1;
    }

    if (create_irq_work_queue() != 0) {
        printk(KERN_ERR "Failed to create IRQ work queue\n");
        destroy_workqueue(system_wq);
        return 1;
    }

    /* Create and open transport */
    transport = ec_transport_create(EC_TRANSPORT_RAW);
    if (!transport) {
        printk(KERN_ERR "Failed to create transport\n");
        ret = 1;
        goto out_cleanup_queues;
    }

    ret = ec_transport_open(transport, interface);
    if (ret < 0) {
        printk(KERN_ERR "Failed to open transport on %s: %d\n", interface, ret);
        ret = 1;
        goto out_destroy_transport;
    }

    printk(KERN_INFO "Transport opened successfully\n");

    /* Initialize master */
    ret = ec_master_init(&master, 0, NULL, NULL, 0, 0);
    if (ret < 0) {
        printk(KERN_ERR "Failed to initialize master: %d\n", ret);
        ret = 1;
        goto out_close_transport;
    }

    /* Store transport reference in device */
    master.devices[EC_DEVICE_MAIN].pal.transport = transport;
    master.devices[EC_DEVICE_MAIN].name = interface;  /* Safe: interface from argv remains valid */

    /* Open device */
    ret = ec_device_open(&master.devices[EC_DEVICE_MAIN]);
    if (ret < 0) {
        printk(KERN_ERR "Failed to open device: %d\n", ret);
        ret = 1;
        goto out_clear_master;
    }

    /* Set up signal handlers for clean shutdown */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printk(KERN_INFO "EtherCAT master started, entering main loop\n");

    /* Enter idle phase */
    ret = ec_master_enter_idle_phase(&master);
    if (ret < 0) {
        printk(KERN_ERR "Failed to enter idle phase: %d\n", ret);
        ret = 1;
        goto out_close_device;
    }

    /* Main loop - run until signal received */
    while (g_running) {
        /* Poll device for received frames */
        ec_device_poll(&master.devices[EC_DEVICE_MAIN]);

        /* Execute master FSM unconditionally */
        ec_fsm_master_exec(&master.fsm);

        /* Small sleep to prevent busy-waiting */
        usleep(1000);  /* 1ms */
    }

    printk(KERN_INFO "Shutting down EtherCAT master\n");

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
    destroy_irq_work_queue();
    destroy_workqueue(system_wq);

    printk(KERN_INFO "EtherCAT master stopped\n");
    return ret;
}

/****************************************************************************/
/* Device functions - userspace implementation */
/****************************************************************************/

/** Initialize device structure. */
int ec_device_init(ec_device_t *device, ec_master_t *master)
{
    device->master = master;
    device->name = NULL;
    device->open = 0;
    device->link_state = 0;
    device->jiffies_poll = 0;

    /* Initialize PAL-specific fields */
    device->pal.transport = NULL;
    device->pal.jiffies_poll = 0;
    device->pal.last_link_check = 0;
    device->pal.last_link_state = -1;

    ec_device_clear_stats(device);
    
    return 0;
}

/** Clear device structure. */
void ec_device_clear(ec_device_t *device)
{
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

    /* Send frame via transport layer (size includes EtherCAT data, not Ethernet header) */
    ret = ec_transport_send(device->pal.transport, size + ETH_HLEN);
    if (ret < 0) {
        device->tx_errors++;
        return;
    }

    /* Update statistics */
    device->tx_count++;
    device->master->device_stats.tx_count++;
    device->tx_bytes += ETH_HLEN + size;
    device->master->device_stats.tx_bytes += ETH_HLEN + size;
}

/** Poll for received frames. */
void ec_device_poll(ec_device_t *device)
{
    uint8_t rx_buffer[ETH_FRAME_LEN];
    int received;

    device->jiffies_poll = get_jiffies();
    device->pal.jiffies_poll = device->jiffies_poll;

    if (!device->pal.transport) {
        return;
    }

    /* Poll transport layer for received frames */
    while ((received = ec_transport_receive(device->pal.transport, rx_buffer, sizeof(rx_buffer))) > 0) {
        /* Update RX statistics */
        device->rx_count++;
        device->master->device_stats.rx_count++;
        device->rx_bytes += received;
        device->master->device_stats.rx_bytes += received;

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
    if (time_after(device->pal.jiffies_poll, device->pal.last_link_check + 1000)) {
        int link_state = ec_transport_get_link_state(device->pal.transport);
        if (link_state >= 0 && link_state != device->pal.last_link_state) {
            device->link_state = (uint8_t)link_state;
            device->pal.last_link_state = link_state;
            if (link_state) {
                printk(KERN_INFO "Device %s: Link is up\n", device->name ? device->name : "?");
            } else {
                printk(KERN_WARNING "Device %s: Link is down\n", device->name ? device->name : "?");
            }
        }
        device->pal.last_link_check = device->pal.jiffies_poll;
    }
}

/** Open device. */
int ec_device_open(ec_device_t *device)
{
    /* Transport is already opened in main(), just set state */
    device->open = 1;
    device->link_state = 0;
    ec_device_clear_stats(device);
    
    printk(KERN_INFO "Device %s opened\n", device->name ? device->name : "?");
    return 0;
}

/** Close device. */
int ec_device_close(ec_device_t *device)
{
    /* Transport is closed in main(), just set state */
    device->open = 0;
    
    printk(KERN_INFO "Device %s closed\n", device->name ? device->name : "?");
    return 0;
}

