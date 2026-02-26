/*****************************************************************************
 *
 *  Copyright (C) 2006-2024  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT master.
 *
 *  The file is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by the
 *  Free Software Foundation; version 2.1 of the License.
 *
 *  This file is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 *  License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this file. If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************************/

/** \file
 * Shared protocol-level constants and types used by both the ioctl interface
 * and the shared master source code.  Safe to include from kernel module code
 * and from userspace tool code.
 */

/****************************************************************************/

#ifndef __EC_MASTER_SHARED_H__
#define __EC_MASTER_SHARED_H__

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#include "../globals.h"
#include "../include/ecrt.h"

/****************************************************************************/

/** Number of statistic rate intervals to maintain. */
#define EC_RATE_COUNT 3

/** Number of DC sync signals. */
#define EC_SYNC_SIGNAL_COUNT 2

/** Size of the datagram description string.
 *
 * This is also used as the maximum length of EoE device names.
 **/
#define EC_DATAGRAM_NAME_SIZE 20

/** Maximum hostname size.
 *
 * Used inside the EoE set IP parameter request.
 */
#define EC_MAX_HOSTNAME_SIZE 32

/** Slave state mask.
 *
 * Apply this mask to a slave state byte to get the slave state without
 * the error flag.
 */
#define EC_SLAVE_STATE_MASK 0x0F

/****************************************************************************/

/** Ethernet address length.
 *
 * Defined here to avoid a dependency on kernel headers in the ioctl
 * interface header, which is also used by userspace tools.
 */
#define EC_ETH_ALEN 6
#ifdef ETH_ALEN
#if ETH_ALEN != EC_ETH_ALEN
#error Ethernet address length mismatch
#endif
#endif

/****************************************************************************/

/** State of an EtherCAT slave.
 */
typedef enum {
    EC_SLAVE_STATE_UNKNOWN = 0x00,
    /**< unknown state */
    EC_SLAVE_STATE_INIT = 0x01,
    /**< INIT state (no mailbox communication, no IO) */
    EC_SLAVE_STATE_PREOP = 0x02,
    /**< PREOP state (mailbox communication, no IO) */
    EC_SLAVE_STATE_BOOT = 0x03,
    /**< Bootstrap state (mailbox communication, firmware update) */
    EC_SLAVE_STATE_SAFEOP = 0x04,
    /**< SAFEOP (mailbox communication and input update) */
    EC_SLAVE_STATE_OP = 0x08,
    /**< OP (mailbox communication and input/output update) */
    EC_SLAVE_STATE_ACK_ERR = 0x10
    /**< Acknowledge/Error bit (no actual state) */
} ec_slave_state_t;

/** Supported mailbox protocols.
 *
 * Not to mix up with the mailbox type field in the mailbox header defined in
 * master/mailbox.h.
 */
enum {
    EC_MBOX_AOE = 0x01, /**< ADS over EtherCAT */
    EC_MBOX_EOE = 0x02, /**< Ethernet over EtherCAT */
    EC_MBOX_COE = 0x04, /**< CANopen over EtherCAT */
    EC_MBOX_FOE = 0x08, /**< File-Access over EtherCAT */
    EC_MBOX_SOE = 0x10, /**< Servo-Profile over EtherCAT */
    EC_MBOX_VOE = 0x20  /**< Vendor specific */
};

/** Slave information interface CANopen over EtherCAT details flags.
 */
typedef struct {
    uint8_t enable_sdo : 1; /**< Enable SDO access. */
    uint8_t enable_sdo_info : 1; /**< SDO information service available. */
    uint8_t enable_pdo_assign : 1; /**< PDO mapping configurable. */
    uint8_t enable_pdo_configuration : 1; /**< PDO configuration possible. */
    uint8_t enable_upload_at_startup : 1; /**< ?. */
    uint8_t enable_sdo_complete_access : 1; /**< Complete access possible. */
} ec_sii_coe_details_t;

/** Slave information interface general flags.
 */
typedef struct {
    uint8_t enable_safeop : 1; /**< ?. */
    uint8_t enable_not_lrw : 1; /**< Slave does not support LRW. */
} ec_sii_general_flags_t;

/** EtherCAT slave distributed clocks range.
 */
typedef enum {
    EC_DC_32, /**< 32 bit. */
    EC_DC_64 /**< 64 bit for system time, system time offset and
               port 0 receive time. */
} ec_slave_dc_range_t;

/** EtherCAT slave sync signal configuration.
 */
typedef struct {
    uint32_t cycle_time; /**< Cycle time [ns]. */
    int32_t shift_time; /**< Shift time [ns]. */
} ec_sync_signal_t;

/** Access states for SDO entries.
 *
 * The access rights are managed per AL state.
 */
enum {
    EC_SDO_ENTRY_ACCESS_PREOP, /**< Access rights in PREOP. */
    EC_SDO_ENTRY_ACCESS_SAFEOP, /**< Access rights in SAFEOP. */
    EC_SDO_ENTRY_ACCESS_OP, /**< Access rights in OP. */
    EC_SDO_ENTRY_ACCESS_COUNT /**< Number of states. */
};

/****************************************************************************/

/** Master devices.
 */
typedef enum {
    EC_DEVICE_MAIN, /**< Main device. */
    EC_DEVICE_BACKUP /**< Backup device. */
} ec_device_index_t;

extern const char *ec_device_names[2]; // only main and backup!

/****************************************************************************/

#endif
