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
 * EtherCAT Transport Layer Interface
 *
 * This header defines the transport abstraction layer for userspace.
 * Different backends (raw socket, XDP, etc.) implement this interface.
 */

#ifndef EC_TRANSPORT_H
#define EC_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

/****************************************************************************/

/** Maximum Ethernet frame size */
#define EC_TRANSPORT_MAX_FRAME_SIZE 1518

/** EtherCAT ethertype */
#define EC_TRANSPORT_ETHERTYPE 0x88A4

/****************************************************************************/

/** Transport types */
typedef enum {
    EC_TRANSPORT_RAW = 0,    /**< AF_PACKET raw socket */
    EC_TRANSPORT_XDP,        /**< AF_XDP (if compiled with --enable-xdp) */
    EC_TRANSPORT_COUNT       /**< Number of transport types */
} ec_transport_type_t;

/** Forward declaration */
typedef struct ec_transport ec_transport_t;

/**
 * Transport operations interface
 *
 * Each transport backend implements these callbacks.
 */
typedef struct ec_transport_ops {
    /** Human-readable name */
    const char *name;

    /**
     * Open transport on interface
     * @param transport Transport instance
     * @param interface Network interface name (e.g., "eth0")
     * @return 0 on success, negative errno on error
     */
    int (*open)(ec_transport_t *transport, const char *interface);

    /**
     * Close transport
     * @param transport Transport instance
     */
    void (*close)(ec_transport_t *transport);

    /**
     * Get pointer to TX buffer
     * @param transport Transport instance
     * @return Pointer to TX buffer (at least EC_TRANSPORT_MAX_FRAME_SIZE bytes)
     */
    uint8_t *(*get_tx_buffer)(ec_transport_t *transport);

    /**
     * Send frame
     * @param transport Transport instance
     * @param size Frame size in bytes
     * @return 0 on success, negative errno on error
     */
    int (*send)(ec_transport_t *transport, size_t size);

    /**
     * Receive frame (non-blocking)
     * @param transport Transport instance
     * @param buffer Buffer to receive into
     * @param max_size Maximum bytes to receive
     * @return Number of bytes received, 0 if no frame available, negative errno on error
     */
    int (*receive)(ec_transport_t *transport, uint8_t *buffer, size_t max_size);

    /**
     * Get link status
     * @param transport Transport instance
     * @return 1 if link up, 0 if link down, negative errno on error
     */
    int (*get_link_state)(ec_transport_t *transport);

    /**
     * Get MAC address
     * @param transport Transport instance
     * @param mac Buffer for MAC address (6 bytes)
     * @return 0 on success, negative errno on error
     */
    int (*get_mac)(ec_transport_t *transport, uint8_t mac[6]);

    /**
     * Get file descriptor for polling (optional)
     * @param transport Transport instance
     * @return File descriptor, or -1 if not applicable
     */
    int (*get_fd)(ec_transport_t *transport);

} ec_transport_ops_t;

/**
 * Transport instance
 */
struct ec_transport {
    /** Operations for this transport */
    const ec_transport_ops_t *ops;

    /** Transport type */
    ec_transport_type_t type;

    /** Interface name */
    char interface[64];

    /** Private data for backend */
    void *priv;

    /** TX buffer */
    uint8_t tx_buffer[EC_TRANSPORT_MAX_FRAME_SIZE];

    /** Statistics */
    struct {
        uint64_t tx_frames;
        uint64_t tx_bytes;
        uint64_t rx_frames;
        uint64_t rx_bytes;
        uint64_t tx_errors;
        uint64_t rx_errors;
    } stats;
};

/****************************************************************************/

/**
 * Create a transport instance
 * @param type Transport type
 * @return Transport instance, or NULL on error
 */
ec_transport_t *ec_transport_create(ec_transport_type_t type);

/**
 * Destroy a transport instance
 * @param transport Transport instance
 */
void ec_transport_destroy(ec_transport_t *transport);

/**
 * Open transport on interface
 * @param transport Transport instance
 * @param interface Network interface name
 * @return 0 on success, negative errno on error
 */
int ec_transport_open(ec_transport_t *transport, const char *interface);

/**
 * Close transport
 * @param transport Transport instance
 */
void ec_transport_close(ec_transport_t *transport);

/**
 * Get pointer to TX buffer
 * @param transport Transport instance
 * @return Pointer to TX buffer
 */
static inline uint8_t *ec_transport_get_tx_buffer(ec_transport_t *transport)
{
    return transport->ops->get_tx_buffer ?
           transport->ops->get_tx_buffer(transport) : transport->tx_buffer;
}

/**
 * Send frame
 * @param transport Transport instance
 * @param size Frame size
 * @return 0 on success, negative errno on error
 */
static inline int ec_transport_send(ec_transport_t *transport, size_t size)
{
    int ret = transport->ops->send(transport, size);
    if (ret == 0) {
        transport->stats.tx_frames++;
        transport->stats.tx_bytes += size;
    } else {
        transport->stats.tx_errors++;
    }
    return ret;
}

/**
 * Receive frame
 * @param transport Transport instance
 * @param buffer Receive buffer
 * @param max_size Maximum size
 * @return Bytes received, 0 if none, negative errno on error
 */
static inline int ec_transport_receive(ec_transport_t *transport,
                                       uint8_t *buffer, size_t max_size)
{
    int ret = transport->ops->receive(transport, buffer, max_size);
    if (ret > 0) {
        transport->stats.rx_frames++;
        transport->stats.rx_bytes += ret;
    } else if (ret < 0) {
        transport->stats.rx_errors++;
    }
    return ret;
}

/**
 * Get link state
 * @param transport Transport instance
 * @return 1 if up, 0 if down
 */
static inline int ec_transport_get_link_state(ec_transport_t *transport)
{
    return transport->ops->get_link_state ?
           transport->ops->get_link_state(transport) : 1;
}

/**
 * Get MAC address
 * @param transport Transport instance
 * @param mac Buffer for MAC (6 bytes)
 * @return 0 on success
 */
static inline int ec_transport_get_mac(ec_transport_t *transport, uint8_t mac[6])
{
    return transport->ops->get_mac ?
           transport->ops->get_mac(transport, mac) : -ENOTSUP;
}

/**
 * Get file descriptor for polling
 * @param transport Transport instance
 * @return fd or -1
 */
static inline int ec_transport_get_fd(ec_transport_t *transport)
{
    return transport->ops->get_fd ?
           transport->ops->get_fd(transport) : -1;
}

/**
 * Check if transport type is available
 * @param type Transport type
 * @return 1 if available, 0 if not
 */
int ec_transport_type_available(ec_transport_type_t type);

/**
 * Get transport type name
 * @param type Transport type
 * @return Name string
 */
const char *ec_transport_type_name(ec_transport_type_t type);

/****************************************************************************/

#endif /* EC_TRANSPORT_H */
