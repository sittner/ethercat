/*****************************************************************************
 *
 *  rt-effects self-test (override): defining ECRT_RT_ATTR before
 *  including ecrt.h replaces the built-in annotation — the documented
 *  hand-over hook for frameworks with their own RT-check macro (e.g.
 *  LinuxCNC's RTAPI_NONBLOCKING).  Here it is overridden to empty, so
 *  the rt_bad.c pattern MUST compile cleanly even under
 *  -Werror=function-effects (proving the override wins).
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

#define ECRT_RT_ATTR /* framework hand-over: annotation disabled */

#include <stdlib.h>

#include "ecrt.h"

void cyclic_task(ec_master_t *master) ECRT_RT_ATTR;

void cyclic_task(ec_master_t *master)
{
    uint8_t buf[4] = {0};
    uint32_t abort_code;

    /* Without the annotation nothing is verified. */
    ecrt_master_sdo_download(master, 0, 0x1000, 0, buf, sizeof(buf),
            &abort_code);
    free(malloc(16));
}
