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
 * Platform Abstraction Layer - Userspace Implementation
 *
 * This file implements the userspace side of the Platform Abstraction Layer.
 * It provides POSIX-based implementations of the PAL functions defined in
 * master/pal.h for userspace builds.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#include "include/ecrt_user.h"
#include "transport/ec_transport.h"

/******************************************************************************
 * Constants
 *****************************************************************************/

/** Maximum number of master instances */
#define EC_MAX_MASTERS 16

/******************************************************************************
 * Master State
 *****************************************************************************/

/** Master instance state */
typedef struct {
    int initialized;                    /**< Non-zero if initialized */
    ec_transport_t *transport;          /**< Transport instance */
    ec_pal_device_type_t device_type;   /**< Device type (raw/xdp) */
    char interface[64];                 /**< Interface name */
} ec_master_state_t;

/** Array of master states */
static ec_master_state_t master_states[EC_MAX_MASTERS];

/******************************************************************************
 * Memory Allocation
 *****************************************************************************/

/* These are typically macros in pal.h, but provide function versions if needed */

void *ec_pal_malloc_func(size_t size)
{
    return malloc(size);
}

void *ec_pal_zalloc_func(size_t size)
{
    return calloc(1, size);
}

void ec_pal_free_func(void *ptr)
{
    free(ptr);
}

/******************************************************************************
 * Time Functions
 *****************************************************************************/

/* ec_pal_get_jiffies() is implemented as a static inline in pal.h */

void ec_pal_usleep(unsigned long usecs)
{
    usleep(usecs);
}

void ec_pal_msleep(unsigned long msecs)
{
    usleep(msecs * 1000);
}

/******************************************************************************
 * Logging
 *****************************************************************************/

void ec_pal_print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

/******************************************************************************
 * Helper Functions
 *****************************************************************************/

/**
 * Map PAL device type to transport type.
 */
static ec_transport_type_t map_device_to_transport(ec_pal_device_type_t device_type)
{
    switch (device_type) {
    case EC_PAL_DEVICE_RAW:
        return EC_TRANSPORT_RAW;
    case EC_PAL_DEVICE_XDP:
        return EC_TRANSPORT_XDP;
    default:
        return EC_TRANSPORT_RAW;
    }
}

/**
 * Get transport type name for logging.
 */
static const char *get_device_type_name(ec_pal_device_type_t device_type)
{
    switch (device_type) {
    case EC_PAL_DEVICE_RAW:
        return "raw";
    case EC_PAL_DEVICE_XDP:
        return "xdp";
    default:
        return "unknown";
    }
}

/******************************************************************************
 * Userspace Master Management
 *****************************************************************************/

int ecrt_master_init(
    unsigned int master_index,
    ec_pal_device_type_t device_type,
    const char *interface
) {
    ec_master_state_t *state;
    ec_transport_type_t transport_type;
    ec_transport_t *transport;
    uint8_t mac[6];
    int link_state;
    int ret;

    /* Validate master index */
    if (master_index >= EC_MAX_MASTERS) {
        fprintf(stderr, "Master index %u exceeds maximum (%d)\n",
                master_index, EC_MAX_MASTERS);
        return -EINVAL;
    }

    state = &master_states[master_index];

    /* Check if already initialized */
    if (state->initialized) {
        fprintf(stderr, "Master %u already initialized\n", master_index);
        return -EBUSY;
    }

    /* Validate interface */
    if (!interface || strlen(interface) == 0) {
        fprintf(stderr, "No interface specified\n");
        return -EINVAL;
    }

    if (strlen(interface) >= sizeof(state->interface)) {
        fprintf(stderr, "Interface name too long\n");
        return -EINVAL;
    }

    /* Map device type to transport type */
    transport_type = map_device_to_transport(device_type);

    /* Check if transport type is available */
    if (!ec_transport_available(transport_type)) {
        fprintf(stderr, "Transport '%s' not available\n",
                ec_transport_type_name(transport_type));
        return -ENOTSUP;
    }

    /* Create transport */
    transport = ec_transport_create(transport_type);
    if (!transport) {
        fprintf(stderr, "Failed to create transport\n");
        return -ENOMEM;
    }

    /* Open transport on interface */
    ret = ec_transport_open(transport, interface);
    if (ret < 0) {
        fprintf(stderr, "Failed to open transport on %s: %s\n",
                interface, strerror(-ret));
        ec_transport_destroy(transport);
        return ret;
    }

    /* Get and display link state */
    link_state = ec_transport_get_link_state(transport);
    if (link_state < 0) {
        fprintf(stderr, "Warning: Failed to get link state: %s\n",
                strerror(-link_state));
    } else {
        fprintf(stderr, "Link: %s\n", link_state ? "UP" : "DOWN");
    }

    /* Get and display MAC address */
    ret = ec_transport_get_mac(transport, mac);
    if (ret < 0) {
        fprintf(stderr, "Warning: Failed to get MAC address: %s\n",
                strerror(-ret));
    } else {
        fprintf(stderr, "MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    /* Store state */
    state->transport = transport;
    state->device_type = device_type;
    snprintf(state->interface, sizeof(state->interface), "%s", interface);
    state->initialized = 1;

    fprintf(stderr, "Master %u initialized on %s (transport: %s)\n",
            master_index, interface, get_device_type_name(device_type));

    return 0;
}

void ecrt_master_cleanup(unsigned int master_index)
{
    ec_master_state_t *state;

    if (master_index >= EC_MAX_MASTERS) {
        return;
    }

    state = &master_states[master_index];

    if (!state->initialized) {
        return;
    }

    /* Close and destroy transport */
    if (state->transport) {
        ec_transport_close(state->transport);
        ec_transport_destroy(state->transport);
        state->transport = NULL;
    }

    state->initialized = 0;

    fprintf(stderr, "Master %u cleaned up\n", master_index);
}

void ecrt_master_idle(unsigned int master_index)
{
    ec_master_state_t *state;

    if (master_index >= EC_MAX_MASTERS) {
        return;
    }

    state = &master_states[master_index];

    if (!state->initialized || !state->transport) {
        return;
    }

    /* TODO: Implement idle processing
     * - Check for received frames
     * - Process any pending work
     * - Update statistics
     */
}

int ecrt_master_process_control(unsigned int master_index)
{
    ec_master_state_t *state;

    if (master_index >= EC_MAX_MASTERS) {
        return -EINVAL;
    }

    state = &master_states[master_index];

    if (!state->initialized) {
        return -EINVAL;
    }

    /* TODO: Implement control interface processing
     * - Handle Unix socket commands
     * - Process ethercat tool requests
     */

    return 0;
}

/*****************************************************************************/
