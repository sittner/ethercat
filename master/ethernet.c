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
  Ethernet over EtherCAT (EoE).
*/

/****************************************************************************/


#include "pal.h"

#include "master_globals.h"
#include "master.h"
#include "slave.h"
#include "mailbox.h"
#include "ethernet.h"

/****************************************************************************/

/** Defines the debug level of EoE processing.
 *
 * 0 = No debug messages.
 * 1 = Output warnings.
 * 2 = Output actions.
 * 3 = Output actions and frame data.
 */
#define EOE_DEBUG_LEVEL 1

/** Size of the EoE tx queue.
 */
#define EC_EOE_TX_QUEUE_SIZE 100

/** Number of tries.
 */
#define EC_EOE_TRIES 100

/****************************************************************************/

// prototypes for private methods
void ec_eoe_flush(ec_eoe_t *);
int ec_eoe_send(ec_eoe_t *);

/****************************************************************************/

// state functions
void ec_eoe_state_rx_start(ec_eoe_t *);
void ec_eoe_state_rx_check(ec_eoe_t *);
void ec_eoe_state_rx_fetch(ec_eoe_t *);
void ec_eoe_state_tx_start(ec_eoe_t *);
void ec_eoe_state_tx_sent(ec_eoe_t *);

/****************************************************************************/

/** EoE constructor.
 *
 * Initializes the EoE handler, creates a net_device and registers it.
 *
 * \return Zero on success, otherwise a negative error code.
 */
int ec_eoe_init(
        ec_eoe_t *eoe, /**< EoE handler */
        ec_slave_t *slave /**< EtherCAT slave */
        )
{
    int ret = 0;
    char name[EC_DATAGRAM_NAME_SIZE];

    eoe->slave = slave;

    ec_datagram_init(&eoe->datagram);
    eoe->queue_datagram = 0;
    eoe->state = ec_eoe_state_rx_start;
    eoe->opened = 0;
    eoe->rx_skb = NULL;
    eoe->rx_expected_fragment = 0;
    INIT_LIST_HEAD(&eoe->tx_queue);
    eoe->tx_frame = NULL;
    eoe->tx_queue_active = 0;
    eoe->tx_queue_size = EC_EOE_TX_QUEUE_SIZE;
    eoe->tx_queued_frames = 0;

    eoe->tx_frame_number = 0xFF;
    memset(&eoe->stats, 0, sizeof(ec_eoe_stats_t));

    eoe->rx_counter = 0;
    eoe->tx_counter = 0;
    eoe->rx_rate = 0;
    eoe->tx_rate = 0;
    eoe->rate_time = 0;
    eoe->rx_idle = 1;
    eoe->tx_idle = 1;

    /* device name eoe<MASTER>[as]<SLAVE>, because networking scripts don't
     * like hyphens etc. in interface names. */
    if (slave->effective_alias) {
        snprintf(name, EC_DATAGRAM_NAME_SIZE,
                "eoe%ua%u", slave->master->index, slave->effective_alias);
    } else {
        snprintf(name, EC_DATAGRAM_NAME_SIZE,
                "eoe%us%u", slave->master->index, slave->ring_position);
    }

    snprintf(eoe->datagram.name, EC_DATAGRAM_NAME_SIZE, name);

    ret = ec_eoe_netdev_create(eoe, name);
    if (ret) {
        EC_SLAVE_ERR(slave, "Failed to create EoE net_device %s: error %i\n",
                name, ret);
        return ret;
    }

    return 0;
}

/****************************************************************************/

/** EoE destructor.
 *
 * Unregisteres the net_device and frees allocated memory.
 */
void ec_eoe_clear(ec_eoe_t *eoe /**< EoE handler */)
{
    // empty transmit queue
    ec_eoe_flush(eoe);

    if (eoe->tx_frame) {
        ec_eoe_buf_free(eoe->tx_frame->skb);
        ec_eoe_frame_free(eoe->tx_frame);
    }

    if (eoe->rx_skb)
        ec_eoe_buf_free(eoe->rx_skb);

    ec_eoe_netdev_destroy(eoe);

    ec_datagram_clear(&eoe->datagram);
}

/****************************************************************************/

/** Empties the transmit queue.
 */
void ec_eoe_flush(ec_eoe_t *eoe /**< EoE handler */)
{
    ec_eoe_frame_t *frame, *next;
    struct list_head tx_queue;

    ec_eoe_netdev_tx_lock(eoe->dev);

    list_replace_init(&eoe->tx_queue, &tx_queue);
    eoe->tx_queued_frames = 0;

    ec_eoe_netdev_tx_unlock(eoe->dev);

    list_for_each_entry_safe(frame, next, &tx_queue, queue) {
        list_del(&frame->queue);
        ec_eoe_buf_free(frame->skb);
        ec_eoe_frame_free(frame);
    }
}

/****************************************************************************/

/** Sends a frame or the next fragment.
 *
 * \return Zero on success, otherwise a negative error code.
 */
int ec_eoe_send(ec_eoe_t *eoe /**< EoE handler */)
{
    size_t remaining_size, current_size, complete_offset;
    unsigned int last_fragment;
    uint8_t *data;
#if EOE_DEBUG_LEVEL >= 3
    unsigned int i;
#endif

    remaining_size = eoe->tx_frame->skb->len - eoe->tx_offset;

    if (remaining_size <= eoe->slave->configured_tx_mailbox_size - 10) {
        current_size = remaining_size;
        last_fragment = 1;
    } else {
        current_size = ((eoe->slave->configured_tx_mailbox_size - 10) / 32) * 32;
        last_fragment = 0;
    }

    if (eoe->tx_fragment_number) {
        complete_offset = eoe->tx_offset / 32;
    }
    else {
        // complete size in 32 bit blocks, rounded up.
        complete_offset = remaining_size / 32 + 1;
    }

#if EOE_DEBUG_LEVEL >= 2
    EC_SLAVE_DBG(eoe->slave, 0, "EoE %s TX sending fragment %u%s"
            " with %zu octets (%zu). %u frames queued.\n",
            ec_eoe_netdev_name(eoe->dev), eoe->tx_fragment_number,
            last_fragment ? "" : "+", current_size, complete_offset,
            eoe->tx_queued_frames);
#endif

#if EOE_DEBUG_LEVEL >= 3
    {
        char _line[3 * 16 + 1];
        int _off = 0;
        _line[0] = '\0';
        for (i = 0; i < current_size; i++) {
            _off += snprintf(_line + _off, sizeof(_line) - _off,
                    "%02X ", eoe->tx_frame->skb->data[eoe->tx_offset + i]);
            if ((i + 1) % 16 == 0 || i + 1 == current_size) {
                EC_SLAVE_DBG(eoe->slave, 0, "%s\n", _line);
                _off = 0;
                _line[0] = '\0';
            }
        }
    }
#endif

    data = ec_slave_mbox_prepare_send(eoe->slave, &eoe->datagram,
            EC_MBOX_TYPE_EOE, current_size + 4);
    if (IS_ERR(data))
        return PTR_ERR(data);

    EC_WRITE_U8 (data, EC_EOE_FRAMETYPE_INIT_REQ); // Initiate EoE Request
    EC_WRITE_U8 (data + 1, last_fragment);
    EC_WRITE_U16(data + 2, ((eoe->tx_fragment_number & 0x3F) |
                            (complete_offset & 0x3F) << 6 |
                            (eoe->tx_frame_number & 0x0F) << 12));

    memcpy(data + 4, eoe->tx_frame->skb->data + eoe->tx_offset, current_size);
    eoe->queue_datagram = 1;

    eoe->tx_offset += current_size;
    eoe->tx_fragment_number++;
    return 0;
}

/****************************************************************************/

/** Runs the EoE state machine.
 */
void ec_eoe_run(ec_eoe_t *eoe /**< EoE handler */)
{
    if (!eoe->opened)
        return;

    // if the datagram was not sent, or is not yet received, skip this cycle
    if (eoe->queue_datagram || eoe->datagram.state == EC_DATAGRAM_SENT)
        return;

    // call state function
    eoe->state(eoe);

    // update statistics
    ec_time_t now = ec_current_time();
    if (now - eoe->rate_time > ec_ms_to_time(1000)) {
        eoe->rx_rate = eoe->rx_counter;
        eoe->tx_rate = eoe->tx_counter;
        eoe->rx_counter = 0;
        eoe->tx_counter = 0;
        eoe->rate_time = now;
    }

    ec_datagram_output_stats(&eoe->datagram);
}

/****************************************************************************/

/** Queues the datagram, if necessary.
 */
void ec_eoe_queue(ec_eoe_t *eoe /**< EoE handler */)
{
    if (eoe->queue_datagram) {
        ec_master_queue_datagram_ext(eoe->slave->master, &eoe->datagram);
        eoe->queue_datagram = 0;
    }
}

/****************************************************************************/

/** Returns the state of the device.
 *
 * \return 1 if the device is "up", 0 if it is "down"
 */
int ec_eoe_is_open(const ec_eoe_t *eoe /**< EoE handler */)
{
    return eoe->opened;
}

/****************************************************************************/

/** Returns the idle state.
 *
 * \retval 1 The device is idle.
 * \retval 0 The device is busy.
 */
int ec_eoe_is_idle(const ec_eoe_t *eoe /**< EoE handler */)
{
    return eoe->rx_idle && eoe->tx_idle;
}

/*****************************************************************************
 *  STATE PROCESSING FUNCTIONS
 ****************************************************************************/

/** State: RX_START.
 *
 * Starts a new receiving sequence by queueing a datagram that checks the
 * slave's mailbox for a new EoE datagram.
 *
 * \todo Use both devices.
 */
void ec_eoe_state_rx_start(ec_eoe_t *eoe /**< EoE handler */)
{
    if (eoe->slave->error_flag ||
            !eoe->slave->master->devices[EC_DEVICE_MAIN].link_state) {
        eoe->rx_idle = 1;
        eoe->tx_idle = 1;
        return;
    }

    ec_slave_mbox_prepare_check(eoe->slave, &eoe->datagram);
    eoe->queue_datagram = 1;
    eoe->state = ec_eoe_state_rx_check;
}

/****************************************************************************/

/** State: RX_CHECK.
 *
 * Processes the checking datagram sent in RX_START and issues a receive
 * datagram, if new data is available.
 */
void ec_eoe_state_rx_check(ec_eoe_t *eoe /**< EoE handler */)
{
    if (eoe->datagram.state != EC_DATAGRAM_RECEIVED) {
        eoe->stats.rx_errors++;
#if EOE_DEBUG_LEVEL >= 1
        EC_SLAVE_WARN(eoe->slave, "Failed to receive mbox"
                " check datagram for %s.\n", ec_eoe_netdev_name(eoe->dev));
#endif
        eoe->state = ec_eoe_state_tx_start;
        return;
    }

    if (!ec_slave_mbox_check(&eoe->datagram)) {
        eoe->rx_idle = 1;
        eoe->state = ec_eoe_state_tx_start;
        return;
    }

    eoe->rx_idle = 0;
    ec_slave_mbox_prepare_fetch(eoe->slave, &eoe->datagram);
    eoe->queue_datagram = 1;
    eoe->state = ec_eoe_state_rx_fetch;
}

/****************************************************************************/

/** State: RX_FETCH.
 *
 * Checks if the requested data of RX_CHECK was received and processes the EoE
 * datagram.
 */
void ec_eoe_state_rx_fetch(ec_eoe_t *eoe /**< EoE handler */)
{
    size_t rec_size, data_size;
    uint8_t *data, frame_type, last_fragment, time_appended, mbox_prot;
    uint8_t fragment_offset, fragment_number;
#if EOE_DEBUG_LEVEL >= 2
    uint8_t frame_number;
#endif
    off_t offset;
#if EOE_DEBUG_LEVEL >= 3
    unsigned int i;
#endif

    if (eoe->datagram.state != EC_DATAGRAM_RECEIVED) {
        eoe->stats.rx_errors++;
#if EOE_DEBUG_LEVEL >= 1
        EC_SLAVE_WARN(eoe->slave, "Failed to receive mbox"
                " fetch datagram for %s.\n", ec_eoe_netdev_name(eoe->dev));
#endif
        eoe->state = ec_eoe_state_tx_start;
        return;
    }

    data = ec_slave_mbox_fetch(eoe->slave, &eoe->datagram,
            &mbox_prot, &rec_size);
    if (IS_ERR(data)) {
        eoe->stats.rx_errors++;
#if EOE_DEBUG_LEVEL >= 1
        EC_SLAVE_WARN(eoe->slave, "Invalid mailbox response for %s.\n",
                ec_eoe_netdev_name(eoe->dev));
#endif
        eoe->state = ec_eoe_state_tx_start;
        return;
    }

    if (mbox_prot != EC_MBOX_TYPE_EOE) { // FIXME mailbox handler necessary
        eoe->stats.rx_errors++;
#if EOE_DEBUG_LEVEL >= 1
        EC_SLAVE_WARN(eoe->slave, "Other mailbox protocol response for %s.\n",
                ec_eoe_netdev_name(eoe->dev));
#endif
        eoe->state = ec_eoe_state_tx_start;
        return;
    }

    frame_type = EC_READ_U16(data) & 0x000F;

    if (frame_type != EC_EOE_FRAMETYPE_INIT_REQ) { // EoE Fragment Data
#if EOE_DEBUG_LEVEL >= 1
        EC_SLAVE_WARN(eoe->slave, "%s: Other frame received."
                " Dropping.\n", ec_eoe_netdev_name(eoe->dev));
#endif
        eoe->stats.rx_dropped++;
        eoe->state = ec_eoe_state_tx_start;
        return;
    }

    // EoE Fragment Request received

    last_fragment = (EC_READ_U16(data) >> 8) & 0x0001;
    time_appended = (EC_READ_U16(data) >> 9) & 0x0001;
    fragment_number = EC_READ_U16(data + 2) & 0x003F;
    fragment_offset = (EC_READ_U16(data + 2) >> 6) & 0x003F;
#if EOE_DEBUG_LEVEL >= 2
    frame_number = (EC_READ_U16(data + 2) >> 12) & 0x000F;
#endif

#if EOE_DEBUG_LEVEL >= 2
    EC_SLAVE_DBG(eoe->slave, 0, "EoE %s RX fragment %u%s, offset %u,"
            " frame %u%s, %zu octets\n", ec_eoe_netdev_name(eoe->dev), fragment_number,
           last_fragment ? "" : "+", fragment_offset, frame_number,
           time_appended ? ", + timestamp" : "",
           time_appended ? rec_size - 8 : rec_size - 4);
#endif

#if EOE_DEBUG_LEVEL >= 3
    {
        char _line[3 * 16 + 1];
        int _off = 0;
        size_t _rx_size = rec_size - 4;
        _line[0] = '\0';
        for (i = 0; i < _rx_size; i++) {
            _off += snprintf(_line + _off, sizeof(_line) - _off,
                    "%02X ", data[i + 4]);
            if ((i + 1) % 16 == 0 || i + 1 == _rx_size) {
                EC_SLAVE_DBG(eoe->slave, 0, "%s\n", _line);
                _off = 0;
                _line[0] = '\0';
            }
        }
    }
#endif

    data_size = time_appended ? rec_size - 8 : rec_size - 4;

    if (!fragment_number) {
        if (eoe->rx_skb) {
            EC_SLAVE_WARN(eoe->slave, "EoE RX freeing old socket buffer.\n");
            ec_eoe_buf_free(eoe->rx_skb);
        }

        // new socket buffer
        if (!(eoe->rx_skb = ec_eoe_buf_alloc(fragment_offset * 32))) {
            if (ec_log_ratelimit())
                EC_SLAVE_WARN(eoe->slave, "EoE RX low on mem,"
                        " frame dropped.\n");
            eoe->stats.rx_dropped++;
            eoe->state = ec_eoe_state_tx_start;
            return;
        }

        eoe->rx_skb_offset = 0;
        eoe->rx_skb_size = fragment_offset * 32;
        eoe->rx_expected_fragment = 0;
    }
    else {
        if (!eoe->rx_skb) {
            eoe->stats.rx_dropped++;
            eoe->state = ec_eoe_state_tx_start;
            return;
        }

        offset = fragment_offset * 32;
        if (offset != eoe->rx_skb_offset ||
            offset + data_size > eoe->rx_skb_size ||
            fragment_number != eoe->rx_expected_fragment) {
            ec_eoe_buf_free(eoe->rx_skb);
            eoe->rx_skb = NULL;
            eoe->stats.rx_errors++;
#if EOE_DEBUG_LEVEL >= 1
            EC_SLAVE_WARN(eoe->slave, "Fragmenting error at %s.\n",
                    ec_eoe_netdev_name(eoe->dev));
#endif
            eoe->state = ec_eoe_state_tx_start;
            return;
        }
    }

    // copy fragment into socket buffer
    memcpy(ec_eoe_buf_put(eoe->rx_skb, data_size), data + 4, data_size);
    eoe->rx_skb_offset += data_size;

    if (last_fragment) {
        // update statistics
        eoe->stats.rx_packets++;
        eoe->stats.rx_bytes += eoe->rx_skb->len;
        eoe->rx_counter += eoe->rx_skb->len;

#if EOE_DEBUG_LEVEL >= 2
        EC_SLAVE_DBG(eoe->slave, 0, "EoE %s RX frame completed"
                " with %u octets.\n", ec_eoe_netdev_name(eoe->dev), eoe->rx_skb->len);
#endif

        // pass socket buffer to network stack
        eoe->rx_skb->dev = eoe->dev;
        eoe->rx_skb->protocol = ec_eoe_buf_eth_type_trans(eoe->rx_skb, eoe->dev);
        eoe->rx_skb->ip_summed = EC_EOE_CHECKSUM_UNNECESSARY;
        if (ec_eoe_buf_deliver(eoe->rx_skb)) {
            EC_SLAVE_WARN(eoe->slave, "EoE RX netif_rx failed.\n");
        }
        eoe->rx_skb = NULL;

        eoe->state = ec_eoe_state_tx_start;
    }
    else {
        eoe->rx_expected_fragment++;
#if EOE_DEBUG_LEVEL >= 2
        EC_SLAVE_DBG(eoe->slave, 0, "EoE %s RX expecting fragment %u\n",
               ec_eoe_netdev_name(eoe->dev), eoe->rx_expected_fragment);
#endif
        eoe->state = ec_eoe_state_rx_start;
    }
}

/****************************************************************************/

/** State: TX START.
 *
 * Starts a new transmit sequence. If no data is available, a new receive
 * sequence is started instead.
 *
 * \todo Use both devices.
 */
void ec_eoe_state_tx_start(ec_eoe_t *eoe /**< EoE handler */)
{
#if EOE_DEBUG_LEVEL >= 2
    unsigned int wakeup = 0;
#endif

    if (eoe->slave->error_flag ||
            !eoe->slave->master->devices[EC_DEVICE_MAIN].link_state) {
        eoe->rx_idle = 1;
        eoe->tx_idle = 1;
        return;
    }

    ec_eoe_netdev_tx_lock(eoe->dev);

    if (!eoe->tx_queued_frames || list_empty(&eoe->tx_queue)) {
        ec_eoe_netdev_tx_unlock(eoe->dev);
        eoe->tx_idle = 1;
        // no data available.
        // start a new receive immediately.
        ec_eoe_state_rx_start(eoe);
        return;
    }

    // take the first frame out of the queue
    eoe->tx_frame = list_entry(eoe->tx_queue.next, ec_eoe_frame_t, queue);
    list_del(&eoe->tx_frame->queue);
    if (!eoe->tx_queue_active &&
        eoe->tx_queued_frames == eoe->tx_queue_size / 2) {
        ec_eoe_netdev_wake_queue(eoe->dev);
        eoe->tx_queue_active = 1;
#if EOE_DEBUG_LEVEL >= 2
        wakeup = 1;
#endif
    }

    eoe->tx_queued_frames--;
    ec_eoe_netdev_tx_unlock(eoe->dev);

    eoe->tx_idle = 0;

    eoe->tx_frame_number++;
    eoe->tx_frame_number %= 16;
    eoe->tx_fragment_number = 0;
    eoe->tx_offset = 0;

    if (ec_eoe_send(eoe)) {
        ec_eoe_buf_free(eoe->tx_frame->skb);
        ec_eoe_frame_free(eoe->tx_frame);
        eoe->tx_frame = NULL;
        eoe->stats.tx_errors++;
        eoe->state = ec_eoe_state_rx_start;
#if EOE_DEBUG_LEVEL >= 1
        EC_SLAVE_WARN(eoe->slave, "Send error at %s.\n", ec_eoe_netdev_name(eoe->dev));
#endif
        return;
    }

#if EOE_DEBUG_LEVEL >= 2
    if (wakeup)
        EC_SLAVE_DBG(eoe->slave, 0, "EoE %s waking up TX queue...\n",
                ec_eoe_netdev_name(eoe->dev));
#endif

    eoe->tries = EC_EOE_TRIES;
    eoe->state = ec_eoe_state_tx_sent;
}

/****************************************************************************/

/** State: TX SENT.
 *
 * Checks is the previous transmit datagram succeded and sends the next
 * fragment, if necessary.
 */
void ec_eoe_state_tx_sent(ec_eoe_t *eoe /**< EoE handler */)
{
    if (eoe->datagram.state != EC_DATAGRAM_RECEIVED) {
        if (eoe->tries) {
            eoe->tries--; // try again
            eoe->queue_datagram = 1;
        } else {
            eoe->stats.tx_errors++;
#if EOE_DEBUG_LEVEL >= 1
            EC_SLAVE_WARN(eoe->slave, "Failed to receive send"
                    " datagram for %s after %u tries.\n",
                    ec_eoe_netdev_name(eoe->dev), EC_EOE_TRIES);
#endif
            eoe->state = ec_eoe_state_rx_start;
        }
        return;
    }

    if (eoe->datagram.working_counter != 1) {
        if (eoe->tries) {
            eoe->tries--; // try again
            eoe->queue_datagram = 1;
        } else {
            eoe->stats.tx_errors++;
#if EOE_DEBUG_LEVEL >= 1
            EC_SLAVE_WARN(eoe->slave, "No sending response"
                    " for %s after %u tries.\n",
                    ec_eoe_netdev_name(eoe->dev), EC_EOE_TRIES);
#endif
            eoe->state = ec_eoe_state_rx_start;
        }
        return;
    }

    // frame completely sent
    if (eoe->tx_offset >= eoe->tx_frame->skb->len) {
        eoe->stats.tx_packets++;
        eoe->stats.tx_bytes += eoe->tx_frame->skb->len;
        eoe->tx_counter += eoe->tx_frame->skb->len;
        ec_eoe_buf_free(eoe->tx_frame->skb);
        ec_eoe_frame_free(eoe->tx_frame);
        eoe->tx_frame = NULL;
        eoe->state = ec_eoe_state_rx_start;
    }
    else { // send next fragment
        if (ec_eoe_send(eoe)) {
            ec_eoe_buf_free(eoe->tx_frame->skb);
            ec_eoe_frame_free(eoe->tx_frame);
            eoe->tx_frame = NULL;
            eoe->stats.tx_errors++;
#if EOE_DEBUG_LEVEL >= 1
            EC_SLAVE_WARN(eoe->slave, "Send error at %s.\n", ec_eoe_netdev_name(eoe->dev));
#endif
            eoe->state = ec_eoe_state_rx_start;
        }
    }
}

/****************************************************************************/
