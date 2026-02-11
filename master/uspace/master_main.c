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

#include <stdio.h>

int main(int argc, char *argv[])
{
    printf("Hello World\n");
    return 0;
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

    ec_device_clear_stats(device);

    // TODO: Initialize userspace transport (raw socket, etc.)
    
    return 0;
}

/** Clear device structure. */
void ec_device_clear(ec_device_t *device)
{
    //if (device->open) {
    //    ec_device_close(device);
    //}
    // TODO: Clean up userspace transport
}

/** Get pointer to transmit buffer. */
uint8_t *ec_device_tx_data(ec_device_t *device)
{
    // TODO: Return pointer to TX buffer from transport layer
    static uint8_t tx_buffer[ETH_FRAME_LEN];
    return tx_buffer + ETH_HLEN;  // Skip Ethernet header
}

/** Send frame. */
void ec_device_send(ec_device_t *device, size_t size)
{
    // TODO: Send via transport layer (raw socket)
    device->tx_count++;
    device->master->device_stats.tx_count++;
    device->tx_bytes += ETH_HLEN + size;
    device->master->device_stats.tx_bytes += ETH_HLEN + size;
}

/** Poll for received frames. */
void ec_device_poll(ec_device_t *device)
{
    device->jiffies_poll = get_jiffies();
    // TODO: Poll transport layer for received frames
}

/** Open device. */
int ec_device_open(ec_device_t *device)
{
    // TODO: Open raw socket or transport
    device->open = 1;
    device->link_state = 0;
    ec_device_clear_stats(device);
    return 0;
}

/** Close device. */
int ec_device_close(ec_device_t *device)
{
    // TODO: Close raw socket or transport
    device->open = 0;
    return 0;
}

