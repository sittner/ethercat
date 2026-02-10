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
   EtherCAT device methods (userspace implementation).
*/

/****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "pal.h"
#include "device.h"
#include "globals.h"
#include "transport/ec_transport.h"

/****************************************************************************/

/* Forward declarations */
struct ec_master;
typedef struct ec_master ec_master_t;

/* Rate measurement intervals (matching master.c) */
const unsigned int rate_intervals[] = {
    1000,      /* 1 second (1000 ms) */
    1000 * 3,  /* 3 seconds */
    1000 * 60  /* 60 seconds */
};

/* Device attach/detach functions */
void ec_device_attach(ec_device_t *device, ec_transport_t *transport);
void ec_device_detach(ec_device_t *device);

/****************************************************************************/

/** Constructor.
 *
 * \return 0 in case of success, else < 0
 */
int ec_device_init(
        ec_device_t *device, /**< EtherCAT device */
        ec_master_t *master /**< master owning the device */
        )
{
    device->master = master;
    device->plat.transport = NULL;
    device->open = 0;
    device->link_state = 0;
    device->plat.jiffies_poll = 0;

    ec_device_clear_stats(device);

    return 0;
}

/****************************************************************************/

/** Destructor.
 */
void ec_device_clear(
        ec_device_t *device /**< EtherCAT device */
        )
{
    if (device->open) {
        ec_device_close(device);
    }
    
    if (device->plat.transport) {
        ec_transport_close(device->plat.transport);
        ec_transport_destroy(device->plat.transport);
        device->plat.transport = NULL;
    }
}

/****************************************************************************/

/** Attach transport to device.
 * 
 * Associates a transport instance with the device.
 */
void ec_device_attach(
        ec_device_t *device, /**< EtherCAT device */
        ec_transport_t *transport /**< Transport instance */
        )
{
    ec_device_detach(device);
    
    device->plat.transport = transport;
}

/****************************************************************************/

/** Detach transport from device.
 * 
 * Removes the transport association.
 */
void ec_device_detach(
        ec_device_t *device /**< EtherCAT device */
        )
{
    device->plat.transport = NULL;
    device->open = 0;
    device->link_state = 0;
    
    ec_device_clear_stats(device);
}

/****************************************************************************/

/** Opens the EtherCAT device.
 *
 * \return 0 in case of success, else < 0
 */
int ec_device_open(
        ec_device_t *device /**< EtherCAT device */
        )
{
    int link_state;

    if (!device->plat.transport) {
        EC_PAL_ERR("No transport to open!\n");
        return -ENODEV;
    }

    if (device->open) {
        EC_PAL_WARN("Device already opened!\n");
        return 0;
    }

    /* Get link state */
    link_state = ec_transport_get_link_state(device->plat.transport);
    if (link_state < 0) {
        EC_PAL_WARN("Failed to get link state: %s\n", strerror(-link_state));
        device->link_state = 0;
    } else {
        device->link_state = (link_state != 0);
    }

    ec_device_clear_stats(device);

    device->open = 1;

    return 0;
}

/****************************************************************************/

/** Stops the EtherCAT device.
 *
 * \return 0 in case of success, else < 0
 */
int ec_device_close(
        ec_device_t *device /**< EtherCAT device */
        )
{
    if (!device->plat.transport) {
        EC_PAL_ERR("No device to close!\n");
        return -ENODEV;
    }

    if (!device->open) {
        EC_PAL_WARN("Device already closed!\n");
        return 0;
    }

    device->open = 0;
    device->link_state = 0;

    return 0;
}

/****************************************************************************/

/** Returns a pointer to the device's transmit memory.
 *
 * \return pointer to the TX buffer (after Ethernet header)
 */
uint8_t *ec_device_tx_data(
        ec_device_t *device /**< EtherCAT device */
        )
{
    uint8_t *buffer;

    if (!device->plat.transport) {
        return NULL;
    }

    buffer = ec_transport_get_tx_buffer(device->plat.transport);
    if (!buffer) {
        return NULL;
    }

    /* Return pointer after Ethernet header (14 bytes) */
    return buffer + ETH_HLEN;
}

/****************************************************************************/

/** Sends the content of the transmit buffer.
 *
 * Sends the frame via the transport layer.
 */
void ec_device_send(
        ec_device_t *device, /**< EtherCAT device */
        size_t size /**< number of bytes to send */
        )
{
    int ret;
    uint8_t *buffer;
    struct ethhdr {
        uint8_t h_dest[ETH_ALEN];
        uint8_t h_source[ETH_ALEN];
        uint16_t h_proto;
    } __attribute__((packed));
    struct ethhdr *eth;
    uint8_t mac[ETH_ALEN];

    if (!device->plat.transport) {
        device->tx_errors++;
        return;
    }

    buffer = ec_transport_get_tx_buffer(device->plat.transport);
    if (!buffer) {
        device->tx_errors++;
        return;
    }

    /* Prepare Ethernet header */
    eth = (struct ethhdr *)buffer;
    
    /* Set destination to broadcast (EtherCAT uses broadcast) */
    memset(eth->h_dest, 0xff, ETH_ALEN);
    
    /* Set source MAC address */
    if (ec_transport_get_mac(device->plat.transport, mac) == 0) {
        memcpy(eth->h_source, mac, ETH_ALEN);
    } else {
        memset(eth->h_source, 0, ETH_ALEN);
    }
    
    /* Set EtherCAT ethertype (0x88A4) */
    eth->h_proto = htons(0x88A4);

    /* Send full frame including Ethernet header */
    ret = ec_transport_send(device->plat.transport, ETH_HLEN + size);
    if (ret == 0) {
        device->tx_count++;
        device->tx_bytes += ETH_HLEN + size;
    } else {
        device->tx_errors++;
    }
}

/****************************************************************************/

/** Calls the poll function of the transport.
 *
 * Receives frames from the transport and passes them to the master.
 */
void ec_device_poll(
        ec_device_t *device /**< EtherCAT device */
        )
{
    uint8_t rx_buffer[ETH_FRAME_LEN];
    int ret;
    int link_state;
    static uint64_t last_link_check = 0;
    uint64_t now;

    if (!device->plat.transport) {
        return;
    }

    now = ec_pal_jiffies();
    device->plat.jiffies_poll = now;

    /* Update link state periodically (every second) */
    if (ec_pal_time_after(now, last_link_check + ec_pal_hz())) {
        link_state = ec_transport_get_link_state(device->plat.transport);
        if (link_state >= 0) {
            device->link_state = (link_state != 0);
        }
        last_link_check = now;
    }

    /* Non-blocking receive */
    ret = ec_transport_receive(device->plat.transport, rx_buffer, sizeof(rx_buffer));
    if (ret > 0) {
        /* TODO: Pass data to master - transport layer handles Ethernet header
         * This requires the master implementation to be present in userspace.
         * For now, just update statistics.
         */
        /* ec_master_receive_datagrams(device->master, device, rx_buffer, ret); */
        device->rx_count++;
        device->rx_bytes += ret;
    }
}

/****************************************************************************/

/** Clears the frame statistics.
 */
void ec_device_clear_stats(
        ec_device_t *device /**< EtherCAT device */
        )
{
    unsigned int i;

    // zero frame statistics
    device->tx_count = 0;
    device->last_tx_count = 0;
    device->rx_count = 0;
    device->last_rx_count = 0;
    device->tx_bytes = 0;
    device->last_tx_bytes = 0;
    device->rx_bytes = 0;
    device->last_rx_bytes = 0;
    device->tx_errors = 0;

    for (i = 0; i < EC_RATE_COUNT; i++) {
        device->tx_frame_rates[i] = 0;
        device->rx_frame_rates[i] = 0;
        device->tx_byte_rates[i] = 0;
        device->rx_byte_rates[i] = 0;
    }
}

/****************************************************************************/

/** Update device statistics.
 */
void ec_device_update_stats(
        ec_device_t *device /**< EtherCAT device */
        )
{
    unsigned int i;

    int32_t tx_frame_rate = (device->tx_count - device->last_tx_count) * 1000;
    int32_t rx_frame_rate = (device->rx_count - device->last_rx_count) * 1000;
    int32_t tx_byte_rate = (device->tx_bytes - device->last_tx_bytes);
    int32_t rx_byte_rate = (device->rx_bytes - device->last_rx_bytes);

    /* Low-pass filter:
     *      Y_n = y_(n - 1) + T / tau * (x - y_(n - 1))   | T = 1
     *   -> Y_n += (x - y_(n - 1)) / tau
     */
    for (i = 0; i < EC_RATE_COUNT; i++) {
        int32_t n = rate_intervals[i];
        device->tx_frame_rates[i] += 
            (tx_frame_rate - device->tx_frame_rates[i]) / n;
        device->rx_frame_rates[i] += 
            (rx_frame_rate - device->rx_frame_rates[i]) / n;
        device->tx_byte_rates[i] += 
            (tx_byte_rate - device->tx_byte_rates[i]) / n;
        device->rx_byte_rates[i] += 
            (rx_byte_rate - device->rx_byte_rates[i]) / n;
    }

    device->last_tx_count = device->tx_count;
    device->last_rx_count = device->rx_count;
    device->last_tx_bytes = device->tx_bytes;
    device->last_rx_bytes = device->rx_bytes;
}

/****************************************************************************/
