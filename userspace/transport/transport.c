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
 * Transport layer registry and lifecycle management.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "ec_transport.h"

/****************************************************************************/

/* Forward declarations for transport backends */
extern const ec_transport_ops_t ec_transport_raw_ops;

#ifdef HAVE_XDP
extern const ec_transport_ops_t ec_transport_xdp_ops;
#endif

/****************************************************************************/

/** Transport registry */
static const struct {
    ec_transport_type_t type;
    const ec_transport_ops_t *ops;
    int available;
} transport_registry[] = {
    {
        .type = EC_TRANSPORT_RAW,
        .ops = &ec_transport_raw_ops,
        .available = 1,
    },
#ifdef HAVE_XDP
    {
        .type = EC_TRANSPORT_XDP,
        .ops = &ec_transport_xdp_ops,
        .available = 1,
    },
#else
    {
        .type = EC_TRANSPORT_XDP,
        .ops = NULL,
        .available = 0,
    },
#endif
};

#define TRANSPORT_REGISTRY_SIZE \
    (sizeof(transport_registry) / sizeof(transport_registry[0]))

/****************************************************************************/

ec_transport_t *ec_transport_create(ec_transport_type_t type)
{
    ec_transport_t *transport;
    size_t i;

    /* Find transport in registry */
    for (i = 0; i < TRANSPORT_REGISTRY_SIZE; i++) {
        if (transport_registry[i].type == type) {
            if (!transport_registry[i].available) {
                return NULL;
            }

            transport = calloc(1, sizeof(ec_transport_t));
            if (!transport) {
                return NULL;
            }

            transport->type = type;
            transport->ops = transport_registry[i].ops;
            return transport;
        }
    }

    return NULL;
}

/****************************************************************************/

void ec_transport_destroy(ec_transport_t *transport)
{
    if (transport) {
        ec_transport_close(transport);
        free(transport);
    }
}

/****************************************************************************/

int ec_transport_open(ec_transport_t *transport, const char *interface)
{
    int ret;

    if (!transport || !transport->ops || !transport->ops->open) {
        return -EINVAL;
    }

    if (!interface || strlen(interface) >= sizeof(transport->interface)) {
        return -EINVAL;
    }

    strncpy(transport->interface, interface, sizeof(transport->interface) - 1);
    transport->interface[sizeof(transport->interface) - 1] = '\0';

    ret = transport->ops->open(transport, interface);
    if (ret < 0) {
        transport->interface[0] = '\0';
    }

    return ret;
}

/****************************************************************************/

void ec_transport_close(ec_transport_t *transport)
{
    if (transport && transport->ops && transport->ops->close) {
        transport->ops->close(transport);
        transport->interface[0] = '\0';
    }
}

/****************************************************************************/

int ec_transport_type_available(ec_transport_type_t type)
{
    size_t i;

    for (i = 0; i < TRANSPORT_REGISTRY_SIZE; i++) {
        if (transport_registry[i].type == type) {
            return transport_registry[i].available;
        }
    }

    return 0;
}

/****************************************************************************/

const char *ec_transport_type_name(ec_transport_type_t type)
{
    switch (type) {
    case EC_TRANSPORT_RAW:
        return "raw";
    case EC_TRANSPORT_XDP:
        return "xdp";
    default:
        return "unknown";
    }
}

/****************************************************************************/
