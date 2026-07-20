/*****************************************************************************
 *
 *  rt-effects self-test (positive): a cyclic task function annotated
 *  ECRT_RT_ATTR may call the entire rt_safe-documented API subset.
 *  Compiled with -fsyntax-only -Werror=function-effects by
 *  script/rt-effects-check.sh; MUST compile without diagnostics.
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

#include "ecrt.h"

void cyclic_task(ec_master_t *master, ec_domain_t *domain,
        ec_slave_config_t *sc, ec_sdo_request_t *sdo,
        uint8_t *pd, unsigned int off) ECRT_RT_ATTR;

void cyclic_task(ec_master_t *master, ec_domain_t *domain,
        ec_slave_config_t *sc, ec_sdo_request_t *sdo,
        uint8_t *pd, unsigned int off)
{
    ec_master_state_t ms;
    ec_domain_state_t ds;
    ec_slave_config_state_t ss;
    uint32_t ref_time;

    ecrt_master_receive(master);
    ecrt_domain_process(domain);

    ecrt_master_state(master, &ms);
    ecrt_domain_state(domain, &ds);
    ecrt_slave_config_state(sc, &ss);

    /* Asynchronous SDO access via a preallocated request object. */
    if (ecrt_sdo_request_state(sdo) == EC_REQUEST_SUCCESS) {
        ecrt_sdo_request_read(sdo);
    }

    /* PDO data access. */
    EC_WRITE_S16(pd + off, EC_READ_S16(pd + off) + 1);
    ecrt_write_real(pd + off, ecrt_read_real(pd + off) + 1.0f);
    ecrt_write_lreal(pd + off, ecrt_read_lreal(pd + off) + 1.0);

    /* Distributed clocks. */
    ecrt_master_application_time(master, 0);
    ecrt_master_sync_reference_clock_to(master, 0);
    ecrt_master_sync_slave_clocks(master);
    ecrt_master_reference_clock_time(master, &ref_time);
    ecrt_master_sync_monitor_queue(master);

    ecrt_domain_queue(domain);
    ecrt_master_send(master);
}
