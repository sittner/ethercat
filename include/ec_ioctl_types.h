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
   Platform-neutral EtherCAT master ioctl data types and command numbers.
   Safe to include from both kernel module code and userspace tool code.
   Does NOT include <linux/ioctl.h>.
*/

/****************************************************************************/

#ifndef __EC_IOCTL_TYPES_H__
#define __EC_IOCTL_TYPES_H__

#include "ecrt.h"
#include "../master/shared.h"

/****************************************************************************/

/** EtherCAT master ioctl() version magic.
 *
 * Increment this when changing the ioctl interface!
 */
#define EC_IOCTL_VERSION_MAGIC 37

/** String size for ioctl data. */
#define EC_IOCTL_STRING_SIZE 64

/** Maximum size for displayed SDO data.
 * \todo Make this dynamic.
 */
#define EC_MAX_SDO_DATA_SIZE 1024

/** Maximum size for displayed IDN data.
 * \todo Make this dynamic.
 */
#define EC_MAX_IDN_DATA_SIZE 1024

/** Maximum size for key.
 */
#define EC_MAX_FLAG_KEY_SIZE 128

/** Default Unix socket path for the userspace master IPC interface. */
#ifndef EC_IPC_DEFAULT_SOCKET_PATH
#define EC_IPC_DEFAULT_SOCKET_PATH "/var/run/ethercat.sock"
#endif

/****************************************************************************/

/** Command numbers for the tool-side interface.
 *
 * These are the plain integers corresponding to the ioctl command numbers
 * (without the _IO/_IOR/_IOW/_IOWR encoding). Used by both the kernel ioctl
 * backend (which maps them to EC_IOCTL_* macros) and the userspace socket
 * backend (which sends them as-is in the IPC request header).
 */
enum ec_tool_cmd {
    EC_CMD_MODULE               = 0x00,
    EC_CMD_MASTER               = 0x01,
    EC_CMD_SLAVE                = 0x02,
    EC_CMD_SLAVE_SYNC           = 0x03,
    EC_CMD_SLAVE_SYNC_PDO       = 0x04,
    EC_CMD_SLAVE_SYNC_PDO_ENTRY = 0x05,
    EC_CMD_DOMAIN               = 0x06,
    EC_CMD_DOMAIN_FMMU          = 0x07,
    EC_CMD_DOMAIN_DATA          = 0x08,
    EC_CMD_MASTER_DEBUG         = 0x09,
    EC_CMD_MASTER_RESCAN        = 0x0a,
    EC_CMD_SLAVE_STATE          = 0x0b,
    EC_CMD_SLAVE_SDO            = 0x0c,
    EC_CMD_SLAVE_SDO_ENTRY      = 0x0d,
    EC_CMD_SLAVE_SDO_UPLOAD     = 0x0e,
    EC_CMD_SLAVE_SDO_DOWNLOAD   = 0x0f,
    EC_CMD_SLAVE_SII_READ       = 0x10,
    EC_CMD_SLAVE_SII_WRITE      = 0x11,
    EC_CMD_SLAVE_REG_READ       = 0x12,
    EC_CMD_SLAVE_REG_WRITE      = 0x13,
    EC_CMD_SLAVE_FOE_READ       = 0x14,
    EC_CMD_SLAVE_FOE_WRITE      = 0x15,
    EC_CMD_SLAVE_SOE_READ       = 0x16,
    EC_CMD_SLAVE_SOE_WRITE      = 0x17,
    EC_CMD_SLAVE_EOE_IP_PARAM   = 0x18,
    EC_CMD_CONFIG               = 0x19,
    EC_CMD_CONFIG_PDO           = 0x1a,
    EC_CMD_CONFIG_PDO_ENTRY     = 0x1b,
    EC_CMD_CONFIG_SDO           = 0x1c,
    EC_CMD_CONFIG_IDN           = 0x1d,
    EC_CMD_CONFIG_FLAG          = 0x1e,
    EC_CMD_CONFIG_EOE_IP_PARAM  = 0x1f,
    EC_CMD_EOE_HANDLER          = 0x20,
};

/****************************************************************************/

typedef struct {
    uint32_t ioctl_version_magic;
    uint32_t master_count;
} ec_ioctl_module_t;

/****************************************************************************/

typedef struct {
    uint32_t slave_count;
    uint32_t scan_index;
    uint32_t config_count;
    uint32_t domain_count;
    uint32_t eoe_handler_count;
    uint8_t phase;
    uint8_t active;
    uint8_t scan_busy;
    struct ec_ioctl_device {
        uint8_t address[6];
        uint8_t attached;
        uint8_t link_state;
        uint64_t tx_count;
        uint64_t rx_count;
        uint64_t tx_bytes;
        uint64_t rx_bytes;
        uint64_t tx_errors;
        int32_t tx_frame_rates[EC_RATE_COUNT];
        int32_t rx_frame_rates[EC_RATE_COUNT];
        int32_t tx_byte_rates[EC_RATE_COUNT];
        int32_t rx_byte_rates[EC_RATE_COUNT];
    } devices[EC_MAX_NUM_DEVICES];
    uint32_t num_devices;
    uint64_t tx_count;
    uint64_t rx_count;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    int32_t tx_frame_rates[EC_RATE_COUNT];
    int32_t rx_frame_rates[EC_RATE_COUNT];
    int32_t tx_byte_rates[EC_RATE_COUNT];
    int32_t rx_byte_rates[EC_RATE_COUNT];
    int32_t loss_rates[EC_RATE_COUNT];
    uint64_t app_time;
    uint64_t dc_ref_time;
    uint16_t ref_clock;
} ec_ioctl_master_t;

/****************************************************************************/

typedef struct {
    // input
    uint16_t position;

    // outputs
    unsigned int device_index;
    uint32_t vendor_id;
    uint32_t product_code;
    uint32_t revision_number;
    uint32_t serial_number;
    uint16_t alias;
    uint16_t boot_rx_mailbox_offset;
    uint16_t boot_rx_mailbox_size;
    uint16_t boot_tx_mailbox_offset;
    uint16_t boot_tx_mailbox_size;
    uint16_t std_rx_mailbox_offset;
    uint16_t std_rx_mailbox_size;
    uint16_t std_tx_mailbox_offset;
    uint16_t std_tx_mailbox_size;
    uint16_t mailbox_protocols;
    uint8_t has_general_category;
    ec_sii_coe_details_t coe_details;
    ec_sii_general_flags_t general_flags;
    int16_t current_on_ebus;
    struct {
        ec_slave_port_desc_t desc;
        ec_slave_port_link_t link;
        uint32_t receive_time;
        uint16_t next_slave;
        uint32_t delay_to_next_dc;
    } ports[EC_MAX_PORTS];
    uint8_t fmmu_bit;
    uint8_t dc_supported;
    ec_slave_dc_range_t dc_range;
    uint8_t has_dc_system_time;
    uint32_t transmission_delay;
    uint8_t al_state;
    uint8_t error_flag;
    uint8_t sync_count;
    uint16_t sdo_count;
    uint32_t sii_nwords;
    char group[EC_IOCTL_STRING_SIZE];
    char image[EC_IOCTL_STRING_SIZE];
    char order[EC_IOCTL_STRING_SIZE];
    char name[EC_IOCTL_STRING_SIZE];
} ec_ioctl_slave_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint32_t sync_index;

    // outputs
    uint16_t physical_start_address;
    uint16_t default_size;
    uint8_t control_register;
    uint8_t enable;
    uint8_t pdo_count;
} ec_ioctl_slave_sync_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint32_t sync_index;
    uint32_t pdo_pos;

    // outputs
    uint16_t index;
    uint8_t entry_count;
    int8_t name[EC_IOCTL_STRING_SIZE];
} ec_ioctl_slave_sync_pdo_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint32_t sync_index;
    uint32_t pdo_pos;
    uint32_t entry_pos;

    // outputs
    uint16_t index;
    uint8_t subindex;
    uint8_t bit_length;
    int8_t name[EC_IOCTL_STRING_SIZE];
} ec_ioctl_slave_sync_pdo_entry_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t index;

    // outputs
    uint32_t data_size;
    uint32_t logical_base_address;
    uint16_t working_counter[EC_MAX_NUM_DEVICES];
    uint16_t expected_working_counter;
    uint32_t fmmu_count;
} ec_ioctl_domain_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t domain_index;
    uint32_t fmmu_index;

    // outputs
    uint16_t slave_config_alias;
    uint16_t slave_config_position;
    uint8_t sync_index;
    ec_direction_t dir;
    uint32_t logical_address;
    uint32_t data_size;
} ec_ioctl_domain_fmmu_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t domain_index;
    uint32_t data_size;
    uint8_t *target;
} ec_ioctl_domain_data_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint8_t al_state;
} ec_ioctl_slave_state_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint16_t sdo_position;

    // outputs
    uint16_t sdo_index;
    uint8_t max_subindex;
    int8_t name[EC_IOCTL_STRING_SIZE];
} ec_ioctl_slave_sdo_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    int sdo_spec; // positive: index, negative: list position
    uint8_t sdo_entry_subindex;

    // outputs
    uint16_t data_type;
    uint16_t bit_length;
    uint8_t read_access[EC_SDO_ENTRY_ACCESS_COUNT];
    uint8_t write_access[EC_SDO_ENTRY_ACCESS_COUNT];
    int8_t description[EC_IOCTL_STRING_SIZE];
} ec_ioctl_slave_sdo_entry_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint16_t sdo_index;
    uint8_t sdo_entry_subindex;
    size_t target_size;
    uint8_t *target;

    // outputs
    size_t data_size;
    uint32_t abort_code;
} ec_ioctl_slave_sdo_upload_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint16_t sdo_index;
    uint8_t sdo_entry_subindex;
    uint8_t complete_access;
    size_t data_size;
    uint8_t *data;

    // outputs
    uint32_t abort_code;
} ec_ioctl_slave_sdo_download_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint16_t offset;
    uint32_t nwords;
    uint16_t *words;
} ec_ioctl_slave_sii_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint8_t emergency;
    uint16_t address;
    size_t size;
    uint8_t *data;
} ec_ioctl_slave_reg_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint16_t offset;
    size_t buffer_size;
    uint8_t *buffer;

    // outputs
    size_t data_size;
    uint32_t result;
    uint32_t error_code;
    char file_name[32];
} ec_ioctl_slave_foe_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint8_t drive_no;
    uint16_t idn;
    size_t mem_size;
    uint8_t *data;

    // outputs
    size_t data_size;
    uint16_t error_code;
} ec_ioctl_slave_soe_read_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint16_t slave_position;
    uint8_t drive_no;
    uint16_t idn;
    size_t data_size;
    uint8_t *data;

    // outputs
    uint16_t error_code;
} ec_ioctl_slave_soe_write_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;

    // outputs
    uint16_t alias;
    uint16_t position;
    uint32_t vendor_id;
    uint32_t product_code;
    struct {
        ec_direction_t dir;
        ec_watchdog_mode_t watchdog_mode;
        uint32_t pdo_count;
        uint8_t config_this;
    } syncs[EC_MAX_SYNC_MANAGERS];
    uint16_t watchdog_divider;
    uint16_t watchdog_intervals;
    uint32_t sdo_count;
    uint32_t idn_count;
    uint32_t flag_count;
    int32_t slave_position;
    uint16_t dc_assign_activate;
    ec_sync_signal_t dc_sync[EC_SYNC_SIGNAL_COUNT];
} ec_ioctl_config_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    uint8_t sync_index;
    uint16_t pdo_pos;

    // outputs
    uint16_t index;
    uint8_t entry_count;
    int8_t name[EC_IOCTL_STRING_SIZE];
} ec_ioctl_config_pdo_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    uint8_t sync_index;
    uint16_t pdo_pos;
    uint8_t entry_pos;

    // outputs
    uint16_t index;
    uint8_t subindex;
    uint8_t bit_length;
    int8_t name[EC_IOCTL_STRING_SIZE];
} ec_ioctl_config_pdo_entry_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    uint32_t sdo_pos;

    // outputs
    uint16_t index;
    uint8_t subindex;
    size_t size;
    uint8_t data[EC_MAX_SDO_DATA_SIZE];
    uint8_t complete_access;
} ec_ioctl_config_sdo_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    uint32_t idn_pos;

    // outputs
    uint8_t drive_no;
    uint16_t idn;
    ec_al_state_t state;
    size_t size;
    uint8_t data[EC_MAX_IDN_DATA_SIZE];
} ec_ioctl_config_idn_t;

/****************************************************************************/

typedef struct {
    // inputs
    uint32_t config_index;
    uint32_t flag_pos;

    // outputs
    char key[EC_MAX_FLAG_KEY_SIZE];
    int32_t value;
} ec_ioctl_config_flag_t;

/****************************************************************************/

#ifdef EC_EOE

typedef struct {
    // input
    uint16_t eoe_index;

    // outputs
    char name[EC_DATAGRAM_NAME_SIZE];
    uint16_t slave_position;
    uint8_t open;
    uint32_t rx_bytes;
    uint32_t rx_rate;
    uint32_t tx_bytes;
    uint32_t tx_rate;
    uint32_t tx_queued_frames;
    uint32_t tx_queue_size;
} ec_ioctl_eoe_handler_t;

#endif

/****************************************************************************/

typedef struct {
    // input
    uint16_t slave_position;
    uint16_t config_index; // alternatively

    uint8_t mac_address_included;
    uint8_t ip_address_included;
    uint8_t subnet_mask_included;
    uint8_t gateway_included;
    uint8_t dns_included;
    uint8_t name_included;

    unsigned char mac_address[EC_ETH_ALEN];
    struct in_addr ip_address;
    struct in_addr subnet_mask;
    struct in_addr gateway;
    struct in_addr dns;
    char name[EC_MAX_HOSTNAME_SIZE];

    // output
    uint16_t result;
} ec_ioctl_eoe_ip_t;

/****************************************************************************/

/** IPC request header (tool → userspace master server). */
typedef struct {
    uint32_t version_magic; /**< EC_IOCTL_VERSION_MAGIC */
    uint32_t cmd;           /**< ec_tool_cmd enum value */
    uint32_t master_index;  /**< Which master to address */
    uint32_t data_size;     /**< Payload size in bytes */
    /* followed by data_size bytes of payload */
} ec_ipc_request_t;

/** IPC response header (userspace master server → tool). */
typedef struct {
    int32_t  ret;       /**< 0 on success, -errno on failure */
    uint32_t data_size; /**< Response payload size in bytes */
    /* followed by data_size bytes of payload */
} ec_ipc_response_t;

/****************************************************************************/

#endif
