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

/** \file
 * Kernel symbol exports for the EtherCAT master public API.
 *
 * Symbols are defined in the shared master source files and exported here
 * so that the shared code does not need to call EXPORT_SYMBOL directly.
 */

/****************************************************************************/

#include "pal.h"
#include "../globals.h"

/****************************************************************************/

/** \cond */

/* domain.c */
EXPORT_SYMBOL(ecrt_domain_reg_pdo_entry_list);
EXPORT_SYMBOL(ecrt_domain_size);
EXPORT_SYMBOL(ecrt_domain_external_memory);
EXPORT_SYMBOL(ecrt_domain_data);
EXPORT_SYMBOL(ecrt_domain_process);
EXPORT_SYMBOL(ecrt_domain_queue);
EXPORT_SYMBOL(ecrt_domain_state);

/* master.c */
EXPORT_SYMBOL(ecrt_master_create_domain);
EXPORT_SYMBOL(ecrt_master_activate);
EXPORT_SYMBOL(ecrt_master_deactivate);
EXPORT_SYMBOL(ecrt_master_send);
EXPORT_SYMBOL(ecrt_master_send_ext);
EXPORT_SYMBOL(ecrt_master_receive);
EXPORT_SYMBOL(ecrt_master_callbacks);
EXPORT_SYMBOL(ecrt_master);
EXPORT_SYMBOL(ecrt_master_scan_progress);
EXPORT_SYMBOL(ecrt_master_get_slave);
EXPORT_SYMBOL(ecrt_master_slave_config);
EXPORT_SYMBOL(ecrt_master_select_reference_clock);
EXPORT_SYMBOL(ecrt_master_state);
EXPORT_SYMBOL(ecrt_master_link_state);
EXPORT_SYMBOL(ecrt_master_application_time);
EXPORT_SYMBOL(ecrt_master_sync_reference_clock);
EXPORT_SYMBOL(ecrt_master_sync_reference_clock_to);
EXPORT_SYMBOL(ecrt_master_sync_slave_clocks);
EXPORT_SYMBOL(ecrt_master_reference_clock_time);
EXPORT_SYMBOL(ecrt_master_sync_monitor_queue);
EXPORT_SYMBOL(ecrt_master_sync_monitor_process);
EXPORT_SYMBOL(ecrt_master_sdo_download);
EXPORT_SYMBOL(ecrt_master_sdo_download_complete);
EXPORT_SYMBOL(ecrt_master_sdo_upload);
EXPORT_SYMBOL(ecrt_master_write_idn);
EXPORT_SYMBOL(ecrt_master_read_idn);
EXPORT_SYMBOL(ecrt_master_reset);

/* reg_request.c */
EXPORT_SYMBOL(ecrt_reg_request_data);
EXPORT_SYMBOL(ecrt_reg_request_state);
EXPORT_SYMBOL(ecrt_reg_request_write);
EXPORT_SYMBOL(ecrt_reg_request_read);

/* sdo_request.c */
EXPORT_SYMBOL(ecrt_sdo_request_index);
EXPORT_SYMBOL(ecrt_sdo_request_timeout);
EXPORT_SYMBOL(ecrt_sdo_request_data);
EXPORT_SYMBOL(ecrt_sdo_request_data_size);
EXPORT_SYMBOL(ecrt_sdo_request_state);
EXPORT_SYMBOL(ecrt_sdo_request_read);
EXPORT_SYMBOL(ecrt_sdo_request_write);

/* slave_config.c */
EXPORT_SYMBOL(ecrt_slave_config_sync_manager);
EXPORT_SYMBOL(ecrt_slave_config_watchdog);
EXPORT_SYMBOL(ecrt_slave_config_pdo_assign_add);
EXPORT_SYMBOL(ecrt_slave_config_pdo_assign_clear);
EXPORT_SYMBOL(ecrt_slave_config_pdo_mapping_add);
EXPORT_SYMBOL(ecrt_slave_config_pdo_mapping_clear);
EXPORT_SYMBOL(ecrt_slave_config_pdos);
EXPORT_SYMBOL(ecrt_slave_config_reg_pdo_entry);
EXPORT_SYMBOL(ecrt_slave_config_reg_pdo_entry_pos);
EXPORT_SYMBOL(ecrt_slave_config_dc);
EXPORT_SYMBOL(ecrt_slave_config_sdo);
EXPORT_SYMBOL(ecrt_slave_config_sdo8);
EXPORT_SYMBOL(ecrt_slave_config_sdo16);
EXPORT_SYMBOL(ecrt_slave_config_sdo32);
EXPORT_SYMBOL(ecrt_slave_config_complete_sdo);
EXPORT_SYMBOL(ecrt_slave_config_emerg_size);
EXPORT_SYMBOL(ecrt_slave_config_emerg_pop);
EXPORT_SYMBOL(ecrt_slave_config_emerg_clear);
EXPORT_SYMBOL(ecrt_slave_config_emerg_overruns);
EXPORT_SYMBOL(ecrt_slave_config_create_sdo_request);
EXPORT_SYMBOL(ecrt_slave_config_create_soe_request);
EXPORT_SYMBOL(ecrt_slave_config_create_voe_handler);
EXPORT_SYMBOL(ecrt_slave_config_create_reg_request);
EXPORT_SYMBOL(ecrt_slave_config_state);
EXPORT_SYMBOL(ecrt_slave_config_idn);
EXPORT_SYMBOL(ecrt_slave_config_flag);
#ifdef EC_EOE
EXPORT_SYMBOL(ecrt_slave_config_eoe_mac_address);
EXPORT_SYMBOL(ecrt_slave_config_eoe_ip_address);
EXPORT_SYMBOL(ecrt_slave_config_eoe_subnet_mask);
EXPORT_SYMBOL(ecrt_slave_config_eoe_default_gateway);
EXPORT_SYMBOL(ecrt_slave_config_eoe_dns_address);
EXPORT_SYMBOL(ecrt_slave_config_eoe_hostname);
#endif
EXPORT_SYMBOL(ecrt_slave_config_state_timeout);

/* soe_request.c */
EXPORT_SYMBOL(ecrt_soe_request_idn);
EXPORT_SYMBOL(ecrt_soe_request_timeout);
EXPORT_SYMBOL(ecrt_soe_request_data);
EXPORT_SYMBOL(ecrt_soe_request_data_size);
EXPORT_SYMBOL(ecrt_soe_request_state);
EXPORT_SYMBOL(ecrt_soe_request_read);
EXPORT_SYMBOL(ecrt_soe_request_write);

/* voe_handler.c */
EXPORT_SYMBOL(ecrt_voe_handler_send_header);
EXPORT_SYMBOL(ecrt_voe_handler_received_header);
EXPORT_SYMBOL(ecrt_voe_handler_data);
EXPORT_SYMBOL(ecrt_voe_handler_data_size);
EXPORT_SYMBOL(ecrt_voe_handler_read);
EXPORT_SYMBOL(ecrt_voe_handler_write);
EXPORT_SYMBOL(ecrt_voe_handler_execute);

/** \endcond */

/****************************************************************************/
