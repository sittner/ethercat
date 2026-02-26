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

/**
   \file
   EtherCAT master character device IOCTL commands.
*/

/****************************************************************************/

#ifndef __EC_IOCTL_H__
#define __EC_IOCTL_H__

#include <linux/ioctl.h>

#include "../../include/ec_ioctl_types.h"

/****************************************************************************/

/** \cond */

#define EC_IOCTL_TYPE 0xa4

#define EC_IO(nr)           _IO(EC_IOCTL_TYPE, nr)
#define EC_IOR(nr, type)   _IOR(EC_IOCTL_TYPE, nr, type)
#define EC_IOW(nr, type)   _IOW(EC_IOCTL_TYPE, nr, type)
#define EC_IOWR(nr, type) _IOWR(EC_IOCTL_TYPE, nr, type)

// Command-line tool
#define EC_IOCTL_MODULE                EC_IOR(0x00, ec_ioctl_module_t)
#define EC_IOCTL_MASTER                EC_IOR(0x01, ec_ioctl_master_t)
#define EC_IOCTL_SLAVE                EC_IOWR(0x02, ec_ioctl_slave_t)
#define EC_IOCTL_SLAVE_SYNC           EC_IOWR(0x03, ec_ioctl_slave_sync_t)
#define EC_IOCTL_SLAVE_SYNC_PDO       EC_IOWR(0x04, ec_ioctl_slave_sync_pdo_t)
#define EC_IOCTL_SLAVE_SYNC_PDO_ENTRY EC_IOWR(0x05, ec_ioctl_slave_sync_pdo_entry_t)
#define EC_IOCTL_DOMAIN               EC_IOWR(0x06, ec_ioctl_domain_t)
#define EC_IOCTL_DOMAIN_FMMU          EC_IOWR(0x07, ec_ioctl_domain_fmmu_t)
#define EC_IOCTL_DOMAIN_DATA          EC_IOWR(0x08, ec_ioctl_domain_data_t)
#define EC_IOCTL_MASTER_DEBUG           EC_IO(0x09)
#define EC_IOCTL_MASTER_RESCAN          EC_IO(0x0a)
#define EC_IOCTL_SLAVE_STATE           EC_IOW(0x0b, ec_ioctl_slave_state_t)
#define EC_IOCTL_SLAVE_SDO            EC_IOWR(0x0c, ec_ioctl_slave_sdo_t)
#define EC_IOCTL_SLAVE_SDO_ENTRY      EC_IOWR(0x0d, ec_ioctl_slave_sdo_entry_t)
#define EC_IOCTL_SLAVE_SDO_UPLOAD     EC_IOWR(0x0e, ec_ioctl_slave_sdo_upload_t)
#define EC_IOCTL_SLAVE_SDO_DOWNLOAD   EC_IOWR(0x0f, ec_ioctl_slave_sdo_download_t)
#define EC_IOCTL_SLAVE_SII_READ       EC_IOWR(0x10, ec_ioctl_slave_sii_t)
#define EC_IOCTL_SLAVE_SII_WRITE       EC_IOW(0x11, ec_ioctl_slave_sii_t)
#define EC_IOCTL_SLAVE_REG_READ       EC_IOWR(0x12, ec_ioctl_slave_reg_t)
#define EC_IOCTL_SLAVE_REG_WRITE       EC_IOW(0x13, ec_ioctl_slave_reg_t)
#define EC_IOCTL_SLAVE_FOE_READ       EC_IOWR(0x14, ec_ioctl_slave_foe_t)
#define EC_IOCTL_SLAVE_FOE_WRITE       EC_IOW(0x15, ec_ioctl_slave_foe_t)
#define EC_IOCTL_SLAVE_SOE_READ       EC_IOWR(0x16, ec_ioctl_slave_soe_read_t)
#define EC_IOCTL_SLAVE_SOE_WRITE      EC_IOWR(0x17, ec_ioctl_slave_soe_write_t)
#define EC_IOCTL_SLAVE_EOE_IP_PARAM    EC_IOW(0x18, ec_ioctl_eoe_ip_t)
#define EC_IOCTL_CONFIG               EC_IOWR(0x19, ec_ioctl_config_t)
#define EC_IOCTL_CONFIG_PDO           EC_IOWR(0x1a, ec_ioctl_config_pdo_t)
#define EC_IOCTL_CONFIG_PDO_ENTRY     EC_IOWR(0x1b, ec_ioctl_config_pdo_entry_t)
#define EC_IOCTL_CONFIG_SDO           EC_IOWR(0x1c, ec_ioctl_config_sdo_t)
#define EC_IOCTL_CONFIG_IDN           EC_IOWR(0x1d, ec_ioctl_config_idn_t)
#define EC_IOCTL_CONFIG_FLAG          EC_IOWR(0x1e, ec_ioctl_config_flag_t)
#define EC_IOCTL_CONFIG_EOE_IP_PARAM  EC_IOWR(0x1f, ec_ioctl_eoe_ip_t)
#define EC_IOCTL_EOE_HANDLER          EC_IOWR(0x20, ec_ioctl_eoe_handler_t)

// Application interface
#define EC_IOCTL_REQUEST                EC_IO(0x21)
#define EC_IOCTL_CREATE_DOMAIN          EC_IO(0x22)
#define EC_IOCTL_CREATE_SLAVE_CONFIG  EC_IOWR(0x23, ec_ioctl_config_t)
#define EC_IOCTL_SELECT_REF_CLOCK      EC_IOW(0x24, uint32_t)
#define EC_IOCTL_ACTIVATE              EC_IOR(0x25, ec_ioctl_master_activate_t)
#define EC_IOCTL_DEACTIVATE             EC_IO(0x26)
#define EC_IOCTL_SEND                   EC_IO(0x27)
#define EC_IOCTL_RECEIVE                EC_IO(0x28)
#define EC_IOCTL_MASTER_STATE          EC_IOR(0x29, ec_master_state_t)
#define EC_IOCTL_MASTER_LINK_STATE    EC_IOWR(0x2a, ec_ioctl_link_state_t)
#define EC_IOCTL_APP_TIME              EC_IOW(0x2b, uint64_t)
#define EC_IOCTL_SYNC_REF               EC_IO(0x2c)
#define EC_IOCTL_SYNC_REF_TO           EC_IOW(0x2d, uint64_t)
#define EC_IOCTL_SYNC_SLAVES            EC_IO(0x2e)
#define EC_IOCTL_REF_CLOCK_TIME        EC_IOR(0x2f, uint32_t)
#define EC_IOCTL_SYNC_MON_QUEUE         EC_IO(0x30)
#define EC_IOCTL_SYNC_MON_PROCESS      EC_IOR(0x31, uint32_t)
#define EC_IOCTL_RESET                  EC_IO(0x32)
#define EC_IOCTL_SC_SYNC               EC_IOW(0x33, ec_ioctl_config_t)
#define EC_IOCTL_SC_WATCHDOG           EC_IOW(0x34, ec_ioctl_config_t)
#define EC_IOCTL_SC_ADD_PDO            EC_IOW(0x35, ec_ioctl_config_pdo_t)
#define EC_IOCTL_SC_CLEAR_PDOS         EC_IOW(0x36, ec_ioctl_config_pdo_t)
#define EC_IOCTL_SC_ADD_ENTRY          EC_IOW(0x37, ec_ioctl_add_pdo_entry_t)
#define EC_IOCTL_SC_CLEAR_ENTRIES      EC_IOW(0x38, ec_ioctl_config_pdo_t)
#define EC_IOCTL_SC_REG_PDO_ENTRY     EC_IOWR(0x39, ec_ioctl_reg_pdo_entry_t)
#define EC_IOCTL_SC_REG_PDO_POS       EC_IOWR(0x3a, ec_ioctl_reg_pdo_pos_t)
#define EC_IOCTL_SC_DC                 EC_IOW(0x3b, ec_ioctl_config_t)
#define EC_IOCTL_SC_SDO                EC_IOW(0x3c, ec_ioctl_sc_sdo_t)
#define EC_IOCTL_SC_EMERG_SIZE         EC_IOW(0x3d, ec_ioctl_sc_emerg_t)
#define EC_IOCTL_SC_EMERG_POP         EC_IOWR(0x3e, ec_ioctl_sc_emerg_t)
#define EC_IOCTL_SC_EMERG_CLEAR        EC_IOW(0x3f, ec_ioctl_sc_emerg_t)
#define EC_IOCTL_SC_EMERG_OVERRUNS    EC_IOWR(0x40, ec_ioctl_sc_emerg_t)
#define EC_IOCTL_SC_SDO_REQUEST       EC_IOWR(0x41, ec_ioctl_sdo_request_t)
#define EC_IOCTL_SC_SOE_REQUEST       EC_IOWR(0x42, ec_ioctl_soe_request_t)
#define EC_IOCTL_SC_REG_REQUEST       EC_IOWR(0x43, ec_ioctl_reg_request_t)
#define EC_IOCTL_SC_VOE               EC_IOWR(0x44, ec_ioctl_voe_t)
#define EC_IOCTL_SC_STATE             EC_IOWR(0x45, ec_ioctl_sc_state_t)
#define EC_IOCTL_SC_IDN                EC_IOW(0x46, ec_ioctl_sc_idn_t)
#define EC_IOCTL_SC_FLAG               EC_IOW(0x47, ec_ioctl_sc_flag_t)
#ifdef EC_EOE
#define EC_IOCTL_SC_EOE_IP_PARAM       EC_IOW(0x48, ec_ioctl_eoe_ip_t)
#endif
#define EC_IOCTL_SC_STATE_TIMEOUT      EC_IOW(0x49, ec_ioctl_sc_state_timeout_t)
#define EC_IOCTL_DOMAIN_SIZE            EC_IO(0x4a)
#define EC_IOCTL_DOMAIN_OFFSET          EC_IO(0x4b)
#define EC_IOCTL_DOMAIN_PROCESS         EC_IO(0x4c)
#define EC_IOCTL_DOMAIN_QUEUE           EC_IO(0x4d)
#define EC_IOCTL_DOMAIN_STATE         EC_IOWR(0x4e, ec_ioctl_domain_state_t)
#define EC_IOCTL_SDO_REQUEST_INDEX    EC_IOWR(0x4f, ec_ioctl_sdo_request_t)
#define EC_IOCTL_SDO_REQUEST_TIMEOUT  EC_IOWR(0x50, ec_ioctl_sdo_request_t)
#define EC_IOCTL_SDO_REQUEST_STATE    EC_IOWR(0x51, ec_ioctl_sdo_request_t)
#define EC_IOCTL_SDO_REQUEST_READ     EC_IOWR(0x52, ec_ioctl_sdo_request_t)
#define EC_IOCTL_SDO_REQUEST_WRITE    EC_IOWR(0x53, ec_ioctl_sdo_request_t)
#define EC_IOCTL_SDO_REQUEST_DATA     EC_IOWR(0x54, ec_ioctl_sdo_request_t)
#define EC_IOCTL_SOE_REQUEST_IDN      EC_IOWR(0x55, ec_ioctl_soe_request_t)
#define EC_IOCTL_SOE_REQUEST_TIMEOUT  EC_IOWR(0x56, ec_ioctl_soe_request_t)
#define EC_IOCTL_SOE_REQUEST_STATE    EC_IOWR(0x57, ec_ioctl_soe_request_t)
#define EC_IOCTL_SOE_REQUEST_READ     EC_IOWR(0x58, ec_ioctl_soe_request_t)
#define EC_IOCTL_SOE_REQUEST_WRITE    EC_IOWR(0x59, ec_ioctl_soe_request_t)
#define EC_IOCTL_SOE_REQUEST_DATA     EC_IOWR(0x5a, ec_ioctl_soe_request_t)
#define EC_IOCTL_REG_REQUEST_DATA     EC_IOWR(0x5b, ec_ioctl_reg_request_t)
#define EC_IOCTL_REG_REQUEST_STATE    EC_IOWR(0x5c, ec_ioctl_reg_request_t)
#define EC_IOCTL_REG_REQUEST_WRITE    EC_IOWR(0x5d, ec_ioctl_reg_request_t)
#define EC_IOCTL_REG_REQUEST_READ     EC_IOWR(0x5e, ec_ioctl_reg_request_t)
#define EC_IOCTL_VOE_SEND_HEADER       EC_IOW(0x5f, ec_ioctl_voe_t)
#define EC_IOCTL_VOE_REC_HEADER       EC_IOWR(0x60, ec_ioctl_voe_t)
#define EC_IOCTL_VOE_READ              EC_IOW(0x61, ec_ioctl_voe_t)
#define EC_IOCTL_VOE_READ_NOSYNC       EC_IOW(0x62, ec_ioctl_voe_t)
#define EC_IOCTL_VOE_WRITE            EC_IOWR(0x63, ec_ioctl_voe_t)
#define EC_IOCTL_VOE_EXEC             EC_IOWR(0x64, ec_ioctl_voe_t)
#define EC_IOCTL_VOE_DATA             EC_IOWR(0x65, ec_ioctl_voe_t)
#define EC_IOCTL_SET_SEND_INTERVAL     EC_IOW(0x66, size_t)

/****************************************************************************/


/*****************************************************************************/

typedef struct {
    // outputs
    void *process_data;
    size_t process_data_size;
} ec_ioctl_master_activate_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    uint16_t pdo_index;
    uint16_t entry_index;
    uint8_t entry_subindex;
    uint8_t entry_bit_length;
} ec_ioctl_add_pdo_entry_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    uint16_t entry_index;
    uint8_t entry_subindex;
    uint32_t domain_index;

    // outputs
    unsigned int bit_position;
} ec_ioctl_reg_pdo_entry_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    uint32_t sync_index;
    uint32_t pdo_pos;
    uint32_t entry_pos;
    uint32_t domain_index;

    // outputs
    unsigned int bit_position;
} ec_ioctl_reg_pdo_pos_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    uint16_t index;
    uint8_t subindex;
    const uint8_t *data;
    size_t size;
    uint8_t complete_access;
} ec_ioctl_sc_sdo_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    size_t size;
    uint8_t *target;

    // outputs
    int32_t overruns;
} ec_ioctl_sc_emerg_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;

    // outputs
    ec_slave_config_state_t *state;
} ec_ioctl_sc_state_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    uint8_t drive_no;
    uint16_t idn;
    ec_al_state_t al_state;
    const uint8_t *data;
    size_t size;
} ec_ioctl_sc_idn_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    size_t key_size;
    char *key;
    int32_t value;
} ec_ioctl_sc_flag_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    ec_al_state_t from_state;
    ec_al_state_t to_state;
    uint32_t timeout_ms;
} ec_ioctl_sc_state_timeout_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t domain_index;

    // outputs
    ec_domain_state_t *state;
} ec_ioctl_domain_state_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;

    // inputs/outputs
    uint32_t request_index;
    uint16_t sdo_index;
    uint8_t sdo_subindex;
    size_t size;
    uint8_t *data;
    uint32_t timeout;
    ec_request_state_t state;
} ec_ioctl_sdo_request_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;

    // inputs/outputs
    uint32_t request_index;
    uint8_t drive_no;
    uint16_t idn;
    size_t size;
    uint8_t *data;
    uint32_t timeout;
    ec_request_state_t state;
} ec_ioctl_soe_request_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    size_t mem_size;

    // inputs/outputs
    uint32_t request_index;
    uint8_t *data;
    ec_request_state_t state;
    uint8_t new_data;
    uint16_t address;
    size_t transfer_size;
} ec_ioctl_reg_request_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;

    // inputs/outputs
    uint32_t voe_index;
    uint32_t *vendor_id;
    uint16_t *vendor_type;
    size_t size;
    uint8_t *data;
    ec_request_state_t state;
} ec_ioctl_voe_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t dev_idx;

    // outputs
    ec_master_link_state_t *state;
} ec_ioctl_link_state_t;

/****************************************************************************/

#ifdef __KERNEL__

/** Context data structure for file handles.
 */
typedef struct {
    unsigned int writable; /**< Device was opened with write permission. */
    unsigned int requested; /**< Master was requested via this file handle. */
    uint8_t *process_data; /**< Total process data area. */
    size_t process_data_size; /**< Size of the \a process_data. */
} ec_ioctl_context_t;

long ec_ioctl(ec_master_t *, ec_ioctl_context_t *, unsigned int,
        void __user *);

#ifdef EC_RTDM

long ec_ioctl_rtdm_rt(ec_master_t *, ec_ioctl_context_t *, unsigned int,
        void __user *);
long ec_ioctl_rtdm_nrt(ec_master_t *, ec_ioctl_context_t *, unsigned int,
        void __user *);

#ifndef EC_RTDM_XENOMAI_V3
int ec_rtdm_mmap(ec_ioctl_context_t *, void **);
#endif

#endif

#endif

/****************************************************************************/

/** \endcond */

#endif
