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
   EtherCAT device structure (shared between kernel and userspace).
*/

/****************************************************************************/

#ifndef __EC_DEVICE_H__
#define __EC_DEVICE_H__

#include "pal.h"  /* Gets ec_device_plat_t from kernel/ or uspace/ */

/****************************************************************************/

/* Forward declaration */
struct ec_master;

/****************************************************************************/

/** Rate measurement intervals */
#define EC_RATE_COUNT 3

/****************************************************************************/

/** EtherCAT device.
 *
 * An EtherCAT device is a network interface card, that is owned by an
 * EtherCAT master to send and receive EtherCAT frames with.
 */
struct ec_device {
    struct ec_master *master;  /**< Master owning the device. */
    uint8_t open;              /**< true, if the device has been opened. */
    uint8_t link_state;        /**< Device link state. */

    /* Statistics (shared between kernel and userspace) */
    uint64_t tx_count;         /**< Number of frames sent. */
    uint64_t last_tx_count;    /**< Number of frames sent of last statistics cycle. */
    uint64_t rx_count;         /**< Number of frames received. */
    uint64_t last_rx_count;    /**< Number of frames received of last statistics cycle. */
    uint64_t tx_bytes;         /**< Number of bytes sent. */
    uint64_t last_tx_bytes;    /**< Number of bytes sent of last statistics cycle. */
    uint64_t rx_bytes;         /**< Number of bytes received. */
    uint64_t last_rx_bytes;    /**< Number of bytes received of last statistics cycle. */
    uint64_t tx_errors;        /**< Number of transmit errors. */
    int32_t tx_frame_rates[EC_RATE_COUNT]; /**< Transmit rates in frames/s. */
    int32_t rx_frame_rates[EC_RATE_COUNT]; /**< Receive rates in frames/s. */
    int32_t tx_byte_rates[EC_RATE_COUNT];  /**< Transmit rates in bytes/s. */
    int32_t rx_byte_rates[EC_RATE_COUNT];  /**< Receive rates in bytes/s. */

    ec_device_plat_t plat;     /**< Platform-specific fields. */
};

typedef struct ec_device ec_device_t;

/****************************************************************************/

/* Function declarations - same signatures on both platforms */
int ec_device_init(ec_device_t *device, struct ec_master *master);
void ec_device_clear(ec_device_t *device);
int ec_device_open(ec_device_t *device);
int ec_device_close(ec_device_t *device);
uint8_t *ec_device_tx_data(ec_device_t *device);
void ec_device_send(ec_device_t *device, size_t size);
void ec_device_poll(ec_device_t *device);
void ec_device_clear_stats(ec_device_t *device);
void ec_device_update_stats(ec_device_t *device);

/****************************************************************************/

#endif /* __EC_DEVICE_H__ */
