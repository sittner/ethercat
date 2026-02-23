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
 * EtherCAT transport layer registry and lifecycle management.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "ec_transport.h"

/****************************************************************************/

/** Transport registry - maps enum to ops */
static const struct {
    ec_transport_type_t type;
    const ec_transport_ops_t *ops;
} transport_registry[] = {
    { EC_TRANSPORT_RAW,        &ec_transport_raw_ops },
#ifdef HAVE_XDP
    { EC_TRANSPORT_XDP_SKB,    &ec_transport_xdp_skb_ops },
    { EC_TRANSPORT_XDP_NATIVE, &ec_transport_xdp_native_ops },
#endif
    { 0, NULL }  /* End of table marker */
};

/****************************************************************************/

/**
 * Create a transport instance.
 */
ec_transport_t *ec_transport_create(ec_transport_type_t type)
{
    ec_transport_t *transport;
    const ec_transport_ops_t *ops;
    unsigned int i;

    /* Find transport ops in registry */
    ops = NULL;
    for (i = 0; transport_registry[i].ops != NULL; i++) {
        if (transport_registry[i].type == type) {
            ops = transport_registry[i].ops;
            break;
        }
    }

    if (!ops) {
        return NULL;
    }

    transport = calloc(1, sizeof(ec_transport_t));
    if (!transport) {
        return NULL;
    }

    transport->ops = ops;
    transport->priv = NULL;

    return transport;
}

/****************************************************************************/

/**
 * Destroy a transport instance.
 */
void ec_transport_destroy(ec_transport_t *transport)
{
    if (!transport) {
        return;
    }

    free(transport);
}

/****************************************************************************/

/**
 * Open transport on network interface.
 */
int ec_transport_open(ec_transport_t *transport, const char *interface)
{
    if (!transport || !transport->ops || !transport->ops->open) {
        return -EINVAL;
    }

    if (!interface) {
        return -EINVAL;
    }

    /* Store interface name */
    strncpy(transport->interface, interface, sizeof(transport->interface) - 1);
    transport->interface[sizeof(transport->interface) - 1] = '\0';

    return transport->ops->open(transport, interface);
}

/****************************************************************************/

/**
 * Close transport.
 */
void ec_transport_close(ec_transport_t *transport)
{
    if (!transport || !transport->ops || !transport->ops->close) {
        return;
    }

    transport->ops->close(transport);
}

/****************************************************************************/

/**
 * Get TX buffer pointer.
 */
uint8_t *ec_transport_get_tx_buffer(ec_transport_t *transport)
{
    if (!transport || !transport->ops || !transport->ops->get_tx_buffer) {
        return NULL;
    }

    return transport->ops->get_tx_buffer(transport);
}

/****************************************************************************/

/**
 * Send frame.
 */
int ec_transport_send(ec_transport_t *transport, size_t size)
{
    if (!transport || !transport->ops || !transport->ops->send) {
        return -EINVAL;
    }

    return transport->ops->send(transport, size);
}

/****************************************************************************/

/**
 * Receive frame (non-blocking).
 */
int ec_transport_receive(ec_transport_t *transport, uint8_t *buffer, size_t max_size)
{
    if (!transport || !transport->ops || !transport->ops->receive) {
        return -EINVAL;
    }

    if (!buffer) {
        return -EINVAL;
    }

    return transport->ops->receive(transport, buffer, max_size);
}

/****************************************************************************/

/**
 * Get link state.
 */
int ec_transport_get_link_state(ec_transport_t *transport)
{
    if (!transport || !transport->ops || !transport->ops->get_link_state) {
        return -EINVAL;
    }

    return transport->ops->get_link_state(transport);
}

/****************************************************************************/

/**
 * Get MAC address.
 */
int ec_transport_get_mac(ec_transport_t *transport, uint8_t mac[6])
{
    if (!transport || !transport->ops || !transport->ops->get_mac) {
        return -EINVAL;
    }

    if (!mac) {
        return -EINVAL;
    }

    return transport->ops->get_mac(transport, mac);
}

/****************************************************************************/

/**
 * Get file descriptor for polling.
 */
int ec_transport_get_fd(ec_transport_t *transport)
{
    if (!transport || !transport->ops) {
        return -1;
    }

    if (!transport->ops->get_fd) {
        return -1;
    }

    return transport->ops->get_fd(transport);
}

/****************************************************************************/

/**
 * Check if a transport type is available.
 */
int ec_transport_available(ec_transport_type_t type)
{
    unsigned int i;

    for (i = 0; transport_registry[i].ops != NULL; i++) {
        if (transport_registry[i].type == type) {
            return 1;
        }
    }

    return 0;
}

/****************************************************************************/

/**
 * Get the name of a transport type.
 */
const char *ec_transport_type_name(ec_transport_type_t type)
{
    const char *name = ec_transport_get_name(type);
    return name ? name : "unknown";
}

/****************************************************************************/

/**
 * Find transport type by name.
 */
int ec_transport_find_by_name(const char *name)
{
    unsigned int i;

    if (!name) {
        return -EINVAL;
    }

    for (i = 0; transport_registry[i].ops != NULL; i++) {
        const ec_transport_ops_t *ops = transport_registry[i].ops;
        if (ops->name && strcmp(ops->name, name) == 0) {
            return transport_registry[i].type;
        }
    }

    return -ENOENT;
}

/****************************************************************************/

/**
 * Get transport name by type.
 */
const char *ec_transport_get_name(ec_transport_type_t type)
{
    unsigned int i;

    for (i = 0; transport_registry[i].ops != NULL; i++) {
        if (transport_registry[i].type == type) {
            return transport_registry[i].ops->name;
        }
    }

    return NULL;
}

/****************************************************************************/

/**
 * Get transport ops by type.
 */
const ec_transport_ops_t *ec_transport_get_ops(ec_transport_type_t type)
{
    unsigned int i;

    for (i = 0; transport_registry[i].ops != NULL; i++) {
        if (transport_registry[i].type == type) {
            return transport_registry[i].ops;
        }
    }

    return NULL;
}

/****************************************************************************/

/**
 * Print available transports to stderr.
 */
void ec_transport_print_available(void)
{
    unsigned int i;
    int needs_separator = 0;

    for (i = 0; transport_registry[i].ops != NULL; i++) {
        const ec_transport_ops_t *ops = transport_registry[i].ops;
        if (ops->name) {
            if (needs_separator) {
                fprintf(stderr, " ");
            }
            fprintf(stderr, "%s", ops->name);
            needs_separator = 1;
        }
    }
}

/****************************************************************************/
