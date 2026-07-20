/*****************************************************************************
 *
 *  rt-effects self-test (negative): an ECRT_RT_ATTR cyclic function that
 *  calls non-RT-safe API and blocking libc.  Compiled with -fsyntax-only
 *  -Werror=function-effects by script/rt-effects-check.sh; every marked
 *  call MUST be diagnosed (the script asserts the compile FAILS and that
 *  each expected diagnostic appears).
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

#include <stdlib.h>

#include "ecrt.h"

void bad_cyclic_task(ec_master_t *master, ec_slave_config_t *sc) ECRT_RT_ATTR;

void bad_cyclic_task(ec_master_t *master, ec_slave_config_t *sc)
{
    uint8_t buf[4] = {0};
    size_t result_size;
    uint32_t abort_code;

    /* Blocking mailbox transfer — not rt_safe, must be diagnosed. */
    ecrt_master_sdo_download(master, 0, 0x1000, 0, buf, sizeof(buf),
            &abort_code);

    /* Blocking mailbox transfer — not rt_safe, must be diagnosed. */
    ecrt_master_sdo_upload(master, 0, 0x1000, 0, buf, sizeof(buf),
            &result_size, &abort_code);

    /* Configuration call — not rt_safe, must be diagnosed. */
    ecrt_slave_config_sdo8(sc, 0x1000, 0, 0);

    /* Heap allocation — must be diagnosed. */
    free(malloc(16));
}
