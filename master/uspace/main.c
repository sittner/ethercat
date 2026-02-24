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

