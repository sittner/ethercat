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
 * Raw socket transport - STUB implementation
 *
 * This is a placeholder. The real AF_PACKET implementation is in PR 2b.
 */

#include <errno.h>
#include "ec_transport.h"

/****************************************************************************/

static int raw_open(ec_transport_t *transport, const char *interface)
{
    (void)transport;
    (void)interface;
    return -ENOSYS;  /* Not yet implemented */
}

static void raw_close(ec_transport_t *transport)
{
    (void)transport;
}

static int raw_send(ec_transport_t *transport, size_t size)
{
    (void)transport;
    (void)size;
    return -ENOSYS;
}

static int raw_receive(ec_transport_t *transport, uint8_t *buffer, size_t max_size)
{
    (void)transport;
    (void)buffer;
    (void)max_size;
    return -ENOSYS;
}

/****************************************************************************/

const ec_transport_ops_t ec_transport_raw_ops = {
    .name = "raw",
    .open = raw_open,
    .close = raw_close,
    .get_tx_buffer = NULL,  /* Use default */
    .send = raw_send,
    .receive = raw_receive,
    .get_link_state = NULL,
    .get_mac = NULL,
    .get_fd = NULL,
};

/****************************************************************************/
