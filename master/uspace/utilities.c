/******************************************************************************
 *
 *  Copyright (C) 2006-2025  Florian Pose, Ingenieurgemeinschaft IgH
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
 * Userspace utility functions for the master.
 * These functions are shared with the kernel module but implemented differently
 * for userspace.
 */

/****************************************************************************/

#include <stdio.h>
#include <string.h>
#include "pal.h"
#include "globals.h"
#include "master.h"
#include "domain.h"
#include "slave_config.h"
#include "fmmu_config.h"
#include "flag.h"

/****************************************************************************/

/** Request state translation table. */
const ec_request_state_t ec_request_state_translation_table[] = {
    EC_REQUEST_UNUSED,  // EC_INT_REQUEST_INIT,
    EC_REQUEST_BUSY,    // EC_INT_REQUEST_QUEUED,
    EC_REQUEST_BUSY,    // EC_INT_REQUEST_BUSY,
    EC_REQUEST_SUCCESS, // EC_INT_REQUEST_SUCCESS,
    EC_REQUEST_ERROR    // EC_INT_REQUEST_FAILURE
};

/****************************************************************************/

/** Device names */
const char *ec_device_names[2] = {
    "main",
    "backup"
};

/****************************************************************************/

/** Print data in hex format.
 *
 * \param data Pointer to data.
 * \param size Number of bytes to output.
 */
void ec_print_data(const uint8_t *data, size_t size)
{
    unsigned int i;

    printf("    ");
    for (i = 0; i < size; i++) {
        printf("%02X ", data[i]);

        if ((i + 1) % 16 == 0 && i < size - 1) {
            printf("\n");
            printf("    ");
        }

        if (i + 1 == 128 && size > 256) {
            printf("dropped %zu bytes\n", size - 128 - i);
            i = size - 128;
            printf("    ");
        }
    }
    printf("\n");
}

/****************************************************************************/

/** Print slave states in clear text.
 *
 * \param states Slave states.
 * \param buffer Target buffer (min. EC_STATE_STRING_SIZE bytes).
 * \param multi Show multi-state mask.
 * \return Number of bytes written.
 */
size_t ec_state_string(uint8_t states, char *buffer, uint8_t multi)
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
        off += sprintf(buffer + off, " + ERROR");
    }

    return off;
}

/****************************************************************************/

/* Stub functions for kernel-only operations that are called from shared code
 * but not needed in userspace */

/** Calculate DC timing (stub for userspace). */
void ec_master_calc_dc(ec_master_t *master) {
    (void)master;
    /* DC timing calculation is kernel-only */
}

/** Request OP state (stub for userspace). */
void ec_master_request_op(ec_master_t *master) {
    (void)master;
    /* OP state request is kernel-only */
}

/** Attach slave configs (stub for userspace). */
void ec_master_attach_slave_configs(ec_master_t *master) {
    (void)master;
    /* Slave config attachment is kernel-only */
}

/** Detach slave config (stub for userspace). */
void ec_slave_config_detach(ec_slave_config_t *sc) {
    (void)sc;
    /* Slave config detachment is kernel-only */
}

/** Get AL timeout for slave config (stub for userspace). */
unsigned int ec_slave_config_al_timeout(const ec_slave_config_t *sc,
        ec_slave_state_t old_state, ec_slave_state_t new_state) {
    (void)sc;
    (void)old_state;
    (void)new_state;
    /* Return default timeout in ms */
    return 1000;
}

/** Find flag (stub for userspace). */
ec_flag_t *ec_slave_config_find_flag(ec_slave_config_t *sc, const char *key) {
    (void)sc;
    (void)key;
    /* Flag finding is kernel-only */
    return NULL;
}

/** Add FMMU config to domain (stub for userspace). */
void ec_domain_add_fmmu_config(ec_domain_t *domain, ec_fmmu_config_t *fmmu) {
    (void)domain;
    (void)fmmu;
    /* Domain FMMU management is kernel-only */
}

/****************************************************************************/
