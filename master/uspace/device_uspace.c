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
   Userspace EtherCAT device implementation.
*/

/****************************************************************************/

#include <string.h>

#include "pal.h"

#include "../device.h"
#include "../master.h"

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
    /* Transport is managed by module, just clear the reference */
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

}

/** Refresh the link state from the transport (rate-limited to 1 Hz).
 *
 * Runs in the master (FSM) threads, NOT in the receive path: the
 * transport link query is a syscall (ioctl(SIOCGIFFLAGS) on the raw
 * transport) and may take locks in custom transports, so it must not
 * run in the application's cyclic thread.
 */
static void ec_device_check_link(ec_device_t *device)
{
    ec_time_t now;
    int link_state;

    if (!device->open || !device->pal.transport) {
        return;
    }

    now = ec_current_time();
    if (now <= device->pal.last_link_check + ec_ms_to_time(1000)) {
        return;
    }
    device->pal.last_link_check = now;

    link_state = ec_transport_get_link_state(device->pal.transport);
    if (link_state >= 0 && link_state != device->pal.last_link_state) {
        device->link_state = (uint8_t)link_state;
        device->pal.last_link_state = link_state;
        if (link_state) {
            ec_log(EC_LOG_INFO, "Device %s: Link is up\n",
                    device->name ? device->name : "?");
        } else {
            ec_log(EC_LOG_WARNING, "Device %s: Link is down\n",
                    device->name ? device->name : "?");
        }
    }
}

/** Check the link state of all devices (PAL hook, called from the
 * master threads). */
void ec_pal_check_link_states(ec_master_t *master)
{
    ec_device_index_t dev_idx;

    for (dev_idx = EC_DEVICE_MAIN; dev_idx < ec_master_num_devices(master);
            dev_idx++) {
        ec_device_check_link(&master->devices[dev_idx]);
    }
}

/** Open device. */
int ec_device_open(ec_device_t *device)
{
    uint8_t *tx_buffer;

    /* Transport is already opened in module, just set state */
    device->open = 1;
    device->link_state = 0;
    device->pal.last_link_check = ec_current_time();
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
    /* Transport is closed in module, just set state */
    device->open = 0;
    
    ec_log(EC_LOG_INFO, "Device %s closed\n", device->name ? device->name : "?");
    return 0;
}
