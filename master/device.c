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
   EtherCAT device methods.
*/

/****************************************************************************/


#include "pal.h"

#include "device.h"
#include "master.h"

#ifdef EC_DEBUG_RING
#define timersub(a, b, result) \
    do { \
        (result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
        (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
        if ((result)->tv_usec < 0) { \
            --(result)->tv_sec; \
            (result)->tv_usec += 1000000; \
        } \
    } while (0)
#endif


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

#ifdef EC_DEBUG_RING
/** Appends frame data to the debug ring.
 */
void ec_device_debug_ring_append(
        ec_device_t *device, /**< EtherCAT device */
        ec_debug_frame_dir_t dir, /**< direction */
        const void *data, /**< frame data */
        size_t size /**< data size */
        )
{
    ec_debug_frame_t *df = &device->debug_frames[device->debug_frame_index];

    df->dir = dir;
    if (dir == TX) {
        do_gettimeofday(&df->t);
    }
    else {
        df->t = device->timeval_poll;
    }
    memcpy(df->data, data, size);
    df->data_size = size;

    device->debug_frame_index++;
    device->debug_frame_index %= EC_DEBUG_RING_SIZE;
    if (unlikely(device->debug_frame_count < EC_DEBUG_RING_SIZE))
        device->debug_frame_count++;
}

/****************************************************************************/

/** Outputs the debug ring.
 */
void ec_device_debug_ring_print(
        const ec_device_t *device /**< EtherCAT device */
        )
{
    int i;
    unsigned int ring_index;
    const ec_debug_frame_t *df;
    struct timeval t0, diff;

    // calculate index of the newest frame in the ring to get its time
    ring_index = (device->debug_frame_index + EC_DEBUG_RING_SIZE - 1)
        % EC_DEBUG_RING_SIZE;
    t0 = device->debug_frames[ring_index].t;

    EC_MASTER_DBG(device->master, 1, "Debug ring %u:\n", ring_index);

    // calculate index of the oldest frame in the ring
    ring_index = (device->debug_frame_index + EC_DEBUG_RING_SIZE
            - device->debug_frame_count) % EC_DEBUG_RING_SIZE;

    for (i = 0; i < device->debug_frame_count; i++) {
        df = &device->debug_frames[ring_index];
        timersub(&t0, &df->t, &diff);

        EC_MASTER_DBG(device->master, 1, "Frame %u, dt=%u.%06u s, %s:\n",
                i + 1 - device->debug_frame_count,
                (unsigned int) diff.tv_sec,
                (unsigned int) diff.tv_usec,
                (df->dir == TX) ? "TX" : "RX");
        ec_print_data(df->data, df->data_size);

        ring_index++;
        ring_index %= EC_DEBUG_RING_SIZE;
    }
}
#endif

/****************************************************************************/

/** Update device statistics.
 */
void ec_device_update_stats(
        ec_device_t *device /**< EtherCAT device */
        )
{
    unsigned int i;

    s32 tx_frame_rate = (device->tx_count - device->last_tx_count) * 1000;
    s32 rx_frame_rate = (device->rx_count - device->last_rx_count) * 1000;
    s32 tx_byte_rate = (device->tx_bytes - device->last_tx_bytes);
    s32 rx_byte_rate = (device->rx_bytes - device->last_rx_bytes);

    /* Low-pass filter:
     *      Y_n = y_(n - 1) + T / tau * (x - y_(n - 1))   | T = 1
     *   -> Y_n += (x - y_(n - 1)) / tau
     */
    for (i = 0; i < EC_RATE_COUNT; i++) {
        s32 n = rate_intervals[i];
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
