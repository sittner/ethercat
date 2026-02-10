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
 * Userspace stub implementations for FSM dependencies.
 * 
 * The FSM code calls many functions from master.c, slave.c, slave_config.c, etc.
 * that have extensive kernel dependencies. This file provides minimal stub
 * implementations for userspace to allow FSM to compile and perform basic
 * operations like bus scanning.
 */

/****************************************************************************/

#include "pal.h"
#include "globals.h"
#include "master.h"
#include "slave.h"
#include "slave_config.h"
#include "sync.h"
#include "fmmu_config.h"
#include "sdo_request.h"
#include "soe_request.h"
#include "coe_emerg_ring.h"

/****************************************************************************/

/* Slave functions */

void ec_slave_init(
        ec_slave_t *slave,
        ec_master_t *master,
        ec_device_index_t dev_idx,
        uint16_t ring_position,
        uint16_t station_address
        )
{
    /* Minimal initialization for userspace */
    memset(slave, 0, sizeof(ec_slave_t));
    slave->master = master;
    slave->device_index = dev_idx;
    slave->ring_position = ring_position;
    slave->station_address = station_address;
    slave->effective_alias = 0;
    
    INIT_LIST_HEAD(&slave->sdo_dictionary);
    INIT_LIST_HEAD(&slave->sdo_requests);
    INIT_LIST_HEAD(&slave->soe_requests);
    INIT_LIST_HEAD(&slave->reg_requests);
    INIT_LIST_HEAD(&slave->foe_requests);
}

void ec_slave_clear(
        ec_slave_t *slave
        )
{
    /* Minimal cleanup */
    (void)slave;
}

void ec_slave_set_state(
        ec_slave_t *slave,
        ec_slave_state_t new_state
        )
{
    slave->current_state = new_state;
}

void ec_slave_request_state(
        ec_slave_t *slave,
        ec_slave_state_t state
        )
{
    slave->requested_state = state;
}

int ec_slave_fetch_sii_general(
        ec_slave_t *slave,
        const uint8_t *data,
        size_t data_size
        )
{
    /* Stub - would parse SII EEPROM general category */
    (void)slave;
    (void)data;
    (void)data_size;
    return 0;
}

int ec_slave_fetch_sii_strings(
        ec_slave_t *slave,
        const uint8_t *data,
        size_t data_size
        )
{
    /* Stub - would parse SII EEPROM strings */
    (void)slave;
    (void)data;
    (void)data_size;
    return 0;
}

int ec_slave_fetch_sii_syncs(
        ec_slave_t *slave,
        const uint8_t *data,
        size_t data_size
        )
{
    /* Stub - would parse SII EEPROM sync manager configuration */
    (void)slave;
    (void)data;
    (void)data_size;
    return 0;
}

int ec_slave_fetch_sii_pdos(
        ec_slave_t *slave,
        const uint8_t *data,
        size_t data_size,
        ec_direction_t dir
        )
{
    /* Stub - would parse SII EEPROM PDO configuration */
    (void)slave;
    (void)data;
    (void)data_size;
    (void)dir;
    return 0;
}

const ec_pdo_t *ec_slave_find_pdo(
        const ec_slave_t *slave,
        uint16_t index
        )
{
    /* Stub - would search for PDO by index */
    (void)slave;
    (void)index;
    return NULL;
}

void ec_slave_attach_pdo_names(
        ec_slave_t *slave
        )
{
    /* Stub - would attach PDO entry names from SII */
    (void)slave;
}

void ec_slave_sdo_dict_info(
        const ec_slave_t *slave,
        unsigned int *sdo_count,
        unsigned int *tx_count
        )
{
    /* Stub - return no SDOs */
    *sdo_count = 0;
    *tx_count = 0;
    (void)slave;
}

void ec_slave_clear_sync_managers(
        ec_slave_t *slave
        )
{
    /* Stub */
    (void)slave;
}

ec_sync_t *ec_slave_get_sync(
        ec_slave_t *slave,
        uint8_t sync_index
        )
{
    /* Stub - return NULL since we don't have sync managers in minimal implementation */
    (void)slave;
    (void)sync_index;
    return NULL;
}

/****************************************************************************/

/* Sync manager functions */

void ec_sync_init(
        ec_sync_t *sync,
        ec_slave_t *slave
        )
{
    memset(sync, 0, sizeof(ec_sync_t));
    sync->slave = slave;
}

void ec_sync_clear(
        ec_sync_t *sync
        )
{
    (void)sync;
}

void ec_sync_page(
        const ec_sync_t *sync,
        uint8_t sync_index,
        uint16_t phys_start_address,
        const ec_sync_config_t *sync_config,
        uint8_t enable,
        uint8_t *data
        )
{
    (void)sync;
    (void)sync_index;
    (void)phys_start_address;
    (void)sync_config;
    (void)enable;
    (void)data;
}

/****************************************************************************/

/* FMMU functions */

void ec_fmmu_config_page(
        const ec_fmmu_config_t *fmmu,
        const ec_sync_t *sync,
        uint8_t *data
        )
{
    (void)fmmu;
    (void)sync;
    (void)data;
}

/****************************************************************************/

/* Slave config functions */

ec_flag_t *ec_slave_config_find_flag(
        ec_slave_config_t *sc,
        const char *key
        )
{
    /* Stub - would search for configuration flag */
    (void)sc;
    (void)key;
    return NULL;
}

unsigned int ec_slave_config_al_timeout(
        const ec_slave_config_t *sc,
        ec_slave_state_t requested_state,
        ec_slave_state_t current_state
        )
{
    /* Return default timeout */
    (void)sc;
    (void)requested_state;
    (void)current_state;
    return 1000; /* 1 second default */
}

/****************************************************************************/

/* SDO request functions */

void ec_sdo_request_init(
        ec_sdo_request_t *req
        )
{
    memset(req, 0, sizeof(ec_sdo_request_t));
    INIT_LIST_HEAD(&req->list);
}

void ec_sdo_request_clear(
        ec_sdo_request_t *req
        )
{
    (void)req;
}

int ec_sdo_request_alloc(
        ec_sdo_request_t *req,
        size_t size
        )
{
    /* Stub - would allocate memory for SDO data */
    (void)req;
    (void)size;
    return 0;
}

int ec_sdo_request_copy(
        ec_sdo_request_t *dst,
        const ec_sdo_request_t *src
        )
{
    (void)dst;
    (void)src;
    return 0;
}

int ec_sdo_request_copy_data(
        ec_sdo_request_t *req,
        const uint8_t *data,
        size_t size
        )
{
    (void)req;
    (void)data;
    (void)size;
    return 0;
}

int ec_sdo_request_timed_out(
        const ec_sdo_request_t *req
        )
{
    (void)req;
    return 0;
}

int ecrt_sdo_request_index(
        ec_sdo_request_t *req,
        uint16_t index,
        uint8_t subindex
        )
{
    req->index = index;
    req->subindex = subindex;
    return 0;
}

int ecrt_sdo_request_read(
        ec_sdo_request_t *req
        )
{
    req->dir = EC_DIR_INPUT;
    return 0;
}

int ecrt_sdo_request_write(
        ec_sdo_request_t *req
        )
{
    req->dir = EC_DIR_OUTPUT;
    return 0;
}

/****************************************************************************/

/* SoE request functions */

void ec_soe_request_init(
        ec_soe_request_t *req
        )
{
    memset(req, 0, sizeof(ec_soe_request_t));
    INIT_LIST_HEAD(&req->list);
}

void ec_soe_request_clear(
        ec_soe_request_t *req
        )
{
    (void)req;
}

int ec_soe_request_copy(
        ec_soe_request_t *dst,
        const ec_soe_request_t *src
        )
{
    (void)dst;
    (void)src;
    return 0;
}

int ec_soe_request_write(
        ec_soe_request_t *req
        )
{
    req->dir = EC_DIR_OUTPUT;
    return 0;
}

int ec_soe_request_append_data(
        ec_soe_request_t *req,
        const uint8_t *data,
        size_t size
        )
{
    (void)req;
    (void)data;
    (void)size;
    return 0;
}

int ec_soe_request_timed_out(
        const ec_soe_request_t *req
        )
{
    (void)req;
    return 0;
}

/****************************************************************************/

/* Master functions */

void ec_master_calc_dc(
        ec_master_t *master
        )
{
    /* Stub - would calculate distributed clocks */
    (void)master;
}

void ec_master_request_op(
        ec_master_t *master
        )
{
    /* Stub - would request operational state */
    (void)master;
}

void ec_master_attach_slave_configs(
        ec_master_t *master
        )
{
    /* Stub - would attach slave configurations to discovered slaves */
    (void)master;
}

/****************************************************************************/

/* CoE emergency ring */

void ec_coe_emerg_ring_push(
        ec_coe_emerg_ring_t *ring,
        const uint8_t *data
        )
{
    (void)ring;
    (void)data;
}

/****************************************************************************/

/* Utility functions */

void ec_print_data(
        const uint8_t *data,
        size_t size
        )
{
    size_t i;
    for (i = 0; i < size; i++) {
        printf("%02X%c", data[i], (i + 1) % 16 ? ' ' : '\n');
    }
    if (size % 16) {
        printf("\n");
    }
}

size_t ec_state_string(
        uint8_t state,
        char *buffer,
        uint8_t size
        )
{
    const char *str;
    
    switch (state) {
        case EC_SLAVE_STATE_INIT:    str = "INIT"; break;
        case EC_SLAVE_STATE_PREOP:   str = "PREOP"; break;
        case EC_SLAVE_STATE_SAFEOP:  str = "SAFEOP"; break;
        case EC_SLAVE_STATE_OP:      str = "OP"; break;
        default:                     str = "UNKNOWN"; break;
    }
    
    size_t len = strlen(str);
    if (buffer && size > 0) {
        strncpy(buffer, str, size - 1);
        buffer[size - 1] = '\0';
    }
    return len;
}

const char *ec_device_names[2] = {
    "main",
    "backup"
};

/****************************************************************************/
