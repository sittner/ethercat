/******************************************************************************
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
 *****************************************************************************/

/**
 * \file
 * EtherCAT Master Userspace Daemon
 *
 * Standalone executable that runs an EtherCAT master in userspace,
 * equivalent to loading the kernel module.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

#include "ecrt.h"
#include "ecrt_user.h"

static volatile int running = 1;

static struct option long_options[] = {
    {"interface",  required_argument, 0, 'i'},
    {"transport",  required_argument, 0, 't'},
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
    int opt;
    ec_pal_device_type_t device_type;
    int ret;

    /* Parse command line arguments */
    while ((opt = getopt_long(argc, argv, "i:t:vh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'i':
            interface = optarg;
            break;
        case 't':
            transport = optarg;
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
        printf("\n");
        printf("Initializing master...\n");
    }

    /* Initialize master */
    ret = ecrt_master_init(0, device_type, interface);
    if (ret < 0) {
        fprintf(stderr, "Failed to initialize master: %s\n", strerror(-ret));
        return 1;
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
        usleep(10000);  /* 10ms */
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
