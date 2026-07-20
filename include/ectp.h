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
 * EtherCAT transport layer interface for userspace implementations.
 */

#ifndef __ECTP_H__
#define __ECTP_H__

#include <stddef.h>
#include <stdint.h>

/****************************************************************************/

/** EtherCAT ethertype */
#define EC_TRANSPORT_ETHERTYPE 0x88A4

/** Maximum frame size (Ethernet MTU) */
#define EC_TRANSPORT_MAX_FRAME_SIZE 1518

/****************************************************************************/

/** Transport type enumeration values */
enum ec_transport_type {
    EC_TRANSPORT_RAW = 0,   /**< AF_PACKET raw socket */
    EC_TRANSPORT_XDP_SKB,   /**< AF_XDP Generic SKB mode (universal compatibility) */
    EC_TRANSPORT_XDP_NATIVE,   /**< AF_XDP Native driver mode with copy */
    EC_TRANSPORT_CCAT,      /**< Beckhoff CCAT EIM direct PCI access (no kernel module) */
};

typedef enum ec_transport_type ec_transport_type_t;

/****************************************************************************/

/* Realtime function-effect annotation macros, shared with ecrt.h (see
 * ecrt_rt.h). The cyclic transport operations (get_tx_buffer/send/
 * receive) carry ECRT_RT_ATTR as part of their function-pointer types:
 * custom transport implementations are thereby subject to clang's
 * function-effects analysis (clang >= 20, -Wfunction-effects) when it
 * is enabled. */
#include "ecrt_rt.h"

/****************************************************************************/

/* Forward declarations */
typedef struct ec_transport ec_transport_t;
typedef struct ec_transport_ops ec_transport_ops_t;

/****************************************************************************/

/**
 * Transport operations structure.
 * 
 * Defines the interface that each transport implementation must provide.
 */
struct ec_transport_ops {
    const char *name;  /**< Transport name for logging */
    
    /** Open transport on interface */
    int (*open)(ec_transport_t *transport, const char *interface);
    
    /** Close transport */
    void (*close)(ec_transport_t *transport);
    
    /** Get TX buffer pointer (realtime context) */
    uint8_t *(*get_tx_buffer)(ec_transport_t *transport) ECRT_RT_ATTR;

    /** Send frame (realtime context, must not block) */
    int (*send)(ec_transport_t *transport, size_t size) ECRT_RT_ATTR;

    /** Receive frame (realtime context, must not block) */
    int (*receive)(ec_transport_t *transport, uint8_t *buffer,
            size_t max_size) ECRT_RT_ATTR;
    
    /** Get link state (1 = up, 0 = down) */
    int (*get_link_state)(ec_transport_t *transport);
    
    /** Get MAC address */
    int (*get_mac)(ec_transport_t *transport, uint8_t mac[6]);
    
    /** Get file descriptor for polling (optional, returns -1 if not supported) */
    int (*get_fd)(ec_transport_t *transport);

    /** Set CPU affinity for transport IRQs (optional, NULL if not supported).
     *
     * Pins the NIC IRQ(s) associated with this transport to the specified CPU.
     * This keeps the IRQ handler and the RT thread on the same core for
     * cache-local I/O, reducing latency and jitter in the EtherCAT cycle.
     *
     * @param transport  Transport instance.
     * @param cpu        Target CPU number (0-based).
     * @return 0 on success, negative error code on failure.
     */
    int (*set_cpu_affinity)(ec_transport_t *transport, int cpu);
};

/****************************************************************************/

/**
 * Transport instance structure.
 */
struct ec_transport {
    const ec_transport_ops_t *ops;  /**< Operations table */
    void *priv;                     /**< Private transport data */
    char interface[16];             /**< Interface name */
    uint8_t tx_buffer[EC_TRANSPORT_MAX_FRAME_SIZE];  /**< TX buffer */
};

/****************************************************************************/

/**
 * Create a transport instance by name.
 *
 * Convenience wrapper that calls ec_transport_find_by_name() then
 * ec_transport_create().
 *
 * @param name Transport name (e.g., "raw", "xdp-skb", "xdp-native")
 * @param interface Network interface name (e.g., "eth0"), or NULL
 * @return Transport instance or NULL on error
 */
ec_transport_t *ec_transport_create_by_name(const char *name,
        const char *interface);

/**
 * Create a transport instance.
 * 
 * @param type Transport type
 * @param interface Network interface name (e.g., "eth0"), or NULL
 * @return Transport instance or NULL on error
 */
ec_transport_t *ec_transport_create(ec_transport_type_t type,
        const char *interface);

/**
 * Destroy a transport instance.
 * 
 * @param transport Transport instance
 */
void ec_transport_destroy(ec_transport_t *transport);

/**
 * Open transport on the interface stored in transport->interface.
 * 
 * @param transport Transport instance (must have interface set at create time)
 * @return 0 on success, negative error code on failure
 */
int ec_transport_open(ec_transport_t *transport);

/**
 * Close transport.
 * 
 * @param transport Transport instance
 */
void ec_transport_close(ec_transport_t *transport);

/**
 * Get TX buffer pointer.
 * 
 * @param transport Transport instance
 * @return Pointer to TX buffer
 */
uint8_t *ec_transport_get_tx_buffer(ec_transport_t *transport)
        ECRT_RT_ATTR;

/**
 * Send frame.
 * 
 * @param transport Transport instance
 * @param size Frame size in bytes
 * @return 0 on success, negative error code on failure
 */
int ec_transport_send(ec_transport_t *transport, size_t size)
        ECRT_RT_ATTR;

/**
 * Receive frame (non-blocking).
 * 
 * @param transport Transport instance
 * @param buffer Buffer to receive frame into
 * @param max_size Maximum buffer size
 * @return Number of bytes received, 0 if no data available, negative error code on failure
 */
int ec_transport_receive(ec_transport_t *transport, uint8_t *buffer,
        size_t max_size) ECRT_RT_ATTR;

/**
 * Get link state.
 * 
 * @param transport Transport instance
 * @return 1 if link is up, 0 if down, negative error code on failure
 */
int ec_transport_get_link_state(ec_transport_t *transport);

/**
 * Get MAC address.
 * 
 * @param transport Transport instance
 * @param mac Buffer to receive MAC address (6 bytes)
 * @return 0 on success, negative error code on failure
 */
int ec_transport_get_mac(ec_transport_t *transport, uint8_t mac[6]);

/**
 * Get file descriptor for polling.
 * 
 * @param transport Transport instance
 * @return File descriptor or -1 if not supported
 */
int ec_transport_get_fd(ec_transport_t *transport);

/**
 * Check if a transport type is available.
 * 
 * @param type Transport type
 * @return 1 if available, 0 otherwise
 */
int ec_transport_available(ec_transport_type_t type);

/**
 * Get the name of a transport type.
 * 
 * @param type Transport type
 * @return Transport type name string, or "unknown" if invalid
 */
const char *ec_transport_type_name(ec_transport_type_t type);

/**
 * Find transport type by name.
 *
 * @param name Transport name (e.g., "raw", "xdp-skb", "xdp-native")
 * @return Transport type on success, negative error code on failure
 */
int ec_transport_find_by_name(const char *name);

/**
 * Get transport name by type.
 *
 * @param type Transport type
 * @return Transport name string, or NULL if invalid
 */
const char *ec_transport_get_name(ec_transport_type_t type);

/**
 * Get transport ops by type.
 *
 * @param type Transport type
 * @return Pointer to transport ops, or NULL if invalid
 */
const ec_transport_ops_t *ec_transport_get_ops(ec_transport_type_t type);

/**
 * Print available transports to stderr.
 */
void ec_transport_print_available(void);

/**
 * Set CPU affinity for the transport's NIC IRQs.
 *
 * Calls the transport's set_cpu_affinity op if available.
 *
 * @param transport Transport instance
 * @param cpu Target CPU number (0-based)
 * @return 0 on success, -ENOSYS if not supported, other negative on error
 */
int ec_transport_set_cpu_affinity(ec_transport_t *transport, int cpu);

/****************************************************************************/

/****************************************************************************/

#endif /* __ECTP_H__ */
