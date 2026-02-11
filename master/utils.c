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

char *ec_master_version_str = EC_MASTER_VERSION; /**< Version string. */

/** Device names.
 */
const char *ec_device_names[2] = {
    "main",
    "backup"
};

/****************************************************************************/

/** Global request state type translation table.
 *
 * Translates an internal request state to an external one.
 */
const ec_request_state_t ec_request_state_translation_table[] = {
    EC_REQUEST_UNUSED,  // EC_INT_REQUEST_INIT,
    EC_REQUEST_BUSY,    // EC_INT_REQUEST_QUEUED,
    EC_REQUEST_BUSY,    // EC_INT_REQUEST_BUSY,
    EC_REQUEST_SUCCESS, // EC_INT_REQUEST_SUCCESS,
    EC_REQUEST_ERROR    // EC_INT_REQUEST_FAILURE
};

/*****************************************************************************
 * MAC address functions
 ****************************************************************************/

/**
 * \return true, if two MAC addresses are equal.
 */
int ec_mac_equal(
        const uint8_t *mac1, /**< First MAC address. */
        const uint8_t *mac2 /**< Second MAC address. */
        )
{
    unsigned int i;

    for (i = 0; i < ETH_ALEN; i++)
        if (mac1[i] != mac2[i])
            return 0;

    return 1;
}

/****************************************************************************/

/** Maximum MAC string size.
 */
#define EC_MAX_MAC_STRING_SIZE (3 * ETH_ALEN)

/** Print a MAC address to a buffer.
 *
 * The buffer size must be at least EC_MAX_MAC_STRING_SIZE.
 *
 * \return number of bytes written.
 */
ssize_t ec_mac_print(
        const uint8_t *mac, /**< MAC address */
        char *buffer /**< Target buffer. */
        )
{
    off_t off = 0;
    unsigned int i;

    for (i = 0; i < ETH_ALEN; i++) {
        off += sprintf(buffer + off, "%02X", mac[i]);
        if (i < ETH_ALEN - 1) off += sprintf(buffer + off, ":");
    }

    return off;
}

/****************************************************************************/

/**
 * \return true, if the MAC address is all-zero.
 */
int ec_mac_is_zero(
        const uint8_t *mac /**< MAC address. */
        )
{
    unsigned int i;

    for (i = 0; i < ETH_ALEN; i++)
        if (mac[i])
            return 0;

    return 1;
}

/****************************************************************************/

/**
 * \return true, if the given MAC address is the broadcast address.
 */
int ec_mac_is_broadcast(
        const uint8_t *mac /**< MAC address. */
        )
{
    unsigned int i;

    for (i = 0; i < ETH_ALEN; i++)
        if (mac[i] != 0xff)
            return 0;

    return 1;
}

/****************************************************************************/

/** Parse a MAC address from a string.
 *
 * The MAC address must match the regular expression
 * "([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}".
 *
 * \return 0 on success, else < 0
 */
static int ec_mac_parse(uint8_t *mac, const char *src, int allow_empty)
{
    unsigned int i, value;
    const char *orig = src;
    char *rem;

    if (!strlen(src)) {
        if (allow_empty){
            return 0;
        } else {
            EC_ERR("MAC address may not be empty.\n");
            return -EINVAL;
        }
    }

    for (i = 0; i < ETH_ALEN; i++) {
        value = simple_strtoul(src, &rem, 16);
        if (rem != src + 2
                || value > 0xFF
                || (i < ETH_ALEN - 1 && *rem != ':')) {
            EC_ERR("Invalid MAC address \"%s\".\n", orig);
            return -EINVAL;
        }
        mac[i] = value;
        if (i < ETH_ALEN - 1) {
            src = rem + 1; // skip colon
        }
    }

    return 0;
}

/****************************************************************************/

/** Outputs frame contents for debugging purposes.
 * If the data block is larger than 256 bytes, only the first 128
 * and the last 128 bytes will be shown
 */
void ec_print_data(const uint8_t *data, /**< pointer to data */
                   size_t size /**< number of bytes to output */
                   )
{
    unsigned int i;

    EC_DBG("");
    for (i = 0; i < size; i++) {
        printk(KERN_CONT "%02X ", data[i]);

        if ((i + 1) % 16 == 0 && i < size - 1) {
            printk(KERN_CONT "\n");
            EC_DBG("");
        }

        if (i + 1 == 128 && size > 256) {
            printk(KERN_CONT "dropped %zu bytes\n", size - 128 - i);
            i = size - 128;
            EC_DBG("");
        }
    }
    printk(KERN_CONT "\n");
}

/****************************************************************************/

/** Outputs frame contents and differences for debugging purposes.
 */
void ec_print_data_diff(const uint8_t *d1, /**< first data */
                        const uint8_t *d2, /**< second data */
                        size_t size /** number of bytes to output */
                        )
{
    unsigned int i;

    EC_DBG("");
    for (i = 0; i < size; i++) {
        if (d1[i] == d2[i]) {
            printk(KERN_CONT ".. ");
        }
        else {
            printk(KERN_CONT "%02X ", d2[i]);
        }
        if ((i + 1) % 16 == 0) {
            printk(KERN_CONT "\n");
            EC_DBG("");
        }
    }
    printk(KERN_CONT "\n");
}

/****************************************************************************/

/** Prints slave states in clear text.
 *
 * \return Size of the created string.
 */
size_t ec_state_string(uint8_t states, /**< slave states */
                       char *buffer, /**< target buffer
                                       (min. EC_STATE_STRING_SIZE bytes) */
                       uint8_t multi /**< Show multi-state mask. */
                       )
{
    off_t off = 0;
    unsigned int first = 1;

    if (!states) {
        off += sprintf(buffer + off, "(unknown)");
        return off;
    }

    if (multi) { // multiple slaves
        if (states & EC_SLAVE_STATE_INIT) {
            off += sprintf(buffer + off, "INIT");
            first = 0;
        }
        if (states & EC_SLAVE_STATE_PREOP) {
            if (!first) off += sprintf(buffer + off, ", ");
            off += sprintf(buffer + off, "PREOP");
            first = 0;
        }
        if (states & EC_SLAVE_STATE_SAFEOP) {
            if (!first) off += sprintf(buffer + off, ", ");
            off += sprintf(buffer + off, "SAFEOP");
            first = 0;
        }
        if (states & EC_SLAVE_STATE_OP) {
            if (!first) off += sprintf(buffer + off, ", ");
            off += sprintf(buffer + off, "OP");
        }
    } else { // single slave
        if ((states & EC_SLAVE_STATE_MASK) == EC_SLAVE_STATE_INIT) {
            off += sprintf(buffer + off, "INIT");
        } else if ((states & EC_SLAVE_STATE_MASK) == EC_SLAVE_STATE_PREOP) {
            off += sprintf(buffer + off, "PREOP");
        } else if ((states & EC_SLAVE_STATE_MASK) == EC_SLAVE_STATE_BOOT) {
            off += sprintf(buffer + off, "BOOT");
        } else if ((states & EC_SLAVE_STATE_MASK) == EC_SLAVE_STATE_SAFEOP) {
            off += sprintf(buffer + off, "SAFEOP");
        } else if ((states & EC_SLAVE_STATE_MASK) == EC_SLAVE_STATE_OP) {
            off += sprintf(buffer + off, "OP");
        } else {
            off += sprintf(buffer + off, "(invalid)");
        }
        first = 0;
    }

    if (states & EC_SLAVE_STATE_ACK_ERR) {
        if (!first) off += sprintf(buffer + off, " + ");
        off += sprintf(buffer + off, "ERROR");
    }

    return off;
}

