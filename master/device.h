/*****************************************************************************
 *
 *  Copyright (C) 2006-2008  Florian Pose, Ingenieurgemeinschaft IgH
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
   EtherCAT device structure.
*/

/****************************************************************************/

#ifndef __EC_DEVICE_H__
#define __EC_DEVICE_H__


#include "pal.h"

#include "master_globals.h"

#ifdef EC_DEBUG_IF
#include "debug.h"
#endif

#ifdef EC_DEBUG_RING
#define EC_DEBUG_RING_SIZE 10

typedef enum {
    TX, RX
} ec_debug_frame_dir_t;

typedef struct {
    ec_debug_frame_dir_t dir;
    struct timeval t;
    uint8_t data[EC_MAX_DATA_SIZE];
    unsigned int data_size;
} ec_debug_frame_t;

#endif

/****************************************************************************/

/**
   EtherCAT device.
   An EtherCAT device is a network interface card, that is owned by an
   EtherCAT master to send and receive EtherCAT frames with.
*/

struct ec_device
{
    ec_master_t *master; /**< EtherCAT master */
    const char *name; /**< device name */
    uint8_t open; /**< true, if the net_device has been opened */
    EC_PAL_SHARED uint8_t link_state; /**< device link state */
#ifdef EC_DEBUG_RING
    struct timeval timeval_poll;
#endif
    ec_time_t time_poll; /**< Timestamp of last poll */

    // Frame statistics
    EC_PAL_SHARED uint64_t tx_count; /**< Number of frames sent. */
    uint64_t last_tx_count; /**< Number of frames sent of last statistics cycle. */
    EC_PAL_SHARED uint64_t rx_count; /**< Number of frames received. */
    uint64_t last_rx_count; /**< Number of frames received of last statistics
                         cycle. */
    EC_PAL_SHARED uint64_t tx_bytes; /**< Number of bytes sent. */
    uint64_t last_tx_bytes; /**< Number of bytes sent of last statistics cycle. */
    EC_PAL_SHARED uint64_t rx_bytes; /**< Number of bytes received. */
    uint64_t last_rx_bytes; /**< Number of bytes received of last statistics cycle.
                        */
    EC_PAL_SHARED uint64_t tx_errors; /**< Number of transmit errors. */
    EC_PAL_SHARED int32_t tx_frame_rates[EC_RATE_COUNT]; /**< Transmit rates in frames/s for
                                         different statistics cycle periods.
                                        */
    EC_PAL_SHARED int32_t rx_frame_rates[EC_RATE_COUNT]; /**< Receive rates in frames/s for
                                         different statistics cycle periods.
                                        */
    EC_PAL_SHARED int32_t tx_byte_rates[EC_RATE_COUNT]; /**< Transmit rates in byte/s for
                                        different statistics cycle periods. */
    EC_PAL_SHARED int32_t rx_byte_rates[EC_RATE_COUNT]; /**< Receive rates in byte/s for
                                        different statistics cycle periods. */

#ifdef EC_DEBUG_IF
    ec_debug_t dbg; /**< debug device */
#endif
#ifdef EC_DEBUG_RING
    ec_debug_frame_t debug_frames[EC_DEBUG_RING_SIZE];
    unsigned int debug_frame_index;
    unsigned int debug_frame_count;
#endif
    ec_device_pal_t pal;
};

/****************************************************************************/

int ec_device_init(ec_device_t *, ec_master_t *);
void ec_device_clear(ec_device_t *);
int ec_device_open(ec_device_t *);
int ec_device_close(ec_device_t *);

void ec_device_poll(ec_device_t *) EC_RT_ATTR;
uint8_t *ec_device_tx_data(ec_device_t *) EC_RT_ATTR;
void ec_device_send(ec_device_t *, size_t) EC_RT_ATTR;
void ec_device_clear_stats(ec_device_t *) EC_RT_ATTR;
void ec_device_update_stats(ec_device_t *) EC_RT_ATTR;
void ec_device_init_common(ec_device_t *, ec_master_t *);
void ec_device_clear_common(ec_device_t *);
void ec_device_account_tx(ec_device_t *, size_t) EC_RT_ATTR;
void ec_device_account_tx_error(ec_device_t *) EC_RT_ATTR;
void ec_device_account_rx(ec_device_t *, size_t) EC_RT_ATTR;

#ifdef EC_DEBUG_RING
void ec_device_debug_ring_append(ec_device_t *, ec_debug_frame_dir_t,
        const void *, size_t);
void ec_device_debug_ring_print(const ec_device_t *);
#endif

/****************************************************************************/

#endif
