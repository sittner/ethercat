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

/* ec_transport.h must be included before pal.h (which pulls in ecrt.h via
 * globals.h -> shared.h) so that __EC_TRANSPORT_H__ is defined when ecrt.h
 * runs, preventing redeclaration conflicts with the inline transport
 * definitions in ecrt.h. */
#include "transport/ec_transport.h"

#include "pal.h"

#include "../master.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>

/****************************************************************************/

/* Forward declarations for library API */
extern int ecrt_lib_init(void);
extern ec_master_t *ecrt_startup_master(ec_transport_type_t transport_type,
        const char *interface);
extern void ecrt_release_master(ec_master_t *master);
extern void ecrt_lib_cleanup(void);

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
    ec_master_t *master = NULL;
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

    ret = ecrt_lib_init();
    if (ret < 0) {
        fprintf(stderr, "Failed to initialize EtherCAT library\n");
        return 1;
    }

    ec_log(EC_LOG_INFO, "Using transport: %s\n", ec_transport_get_name(transport_type));

    master = ecrt_startup_master(transport_type, interface);
    if (!master) {
        fprintf(stderr, "Failed to start EtherCAT master\n");
        ecrt_lib_cleanup();
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    ec_log(EC_LOG_INFO, "EtherCAT master started on %s\n", interface);

    while (g_running) {
        pause();
    }

    ec_log(EC_LOG_INFO, "Shutting down EtherCAT master\n");

    ecrt_release_master(master);
    ecrt_lib_cleanup();

    ec_log(EC_LOG_INFO, "EtherCAT master stopped\n");
    return 0;
}

