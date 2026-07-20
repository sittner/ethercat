/*****************************************************************************
 *
 *  Copyright (C) 2006-2012  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT master userspace library.
 *
 *  The IgH EtherCAT master userspace library is free software; you can
 *  redistribute it and/or modify it under the terms of the GNU Lesser General
 *  Public License as published by the Free Software Foundation; version 2.1
 *  of the License.
 *
 *  The IgH EtherCAT master userspace library is distributed in the hope that
 *  it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with the IgH EtherCAT master userspace library. If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 ****************************************************************************/

#include <string.h>

#include "include/ecrt.h"

/****************************************************************************/

unsigned int ecrt_version_magic(void)
{
    return ECRT_VERSION_MAGIC;
}

/****************************************************************************/

#ifndef __KERNEL__

float ecrt_read_real(const void *data)
{
    uint32_t raw = EC_READ_U32(data);
    float value;

    memcpy(&value, &raw, sizeof(value));
    return value;
}

/****************************************************************************/

double ecrt_read_lreal(const void *data)
{
    uint64_t raw = EC_READ_U64(data);
    double value;

    memcpy(&value, &raw, sizeof(value));
    return value;
}

/****************************************************************************/

void ecrt_write_real(void *data, float value)
{
    uint32_t raw;

    memcpy(&raw, &value, sizeof(raw));
    EC_WRITE_U32(data, raw);
}

/****************************************************************************/

void ecrt_write_lreal(void *data, double value)
{
    uint64_t raw;

    memcpy(&raw, &value, sizeof(raw));
    EC_WRITE_U64(data, raw);
}

#endif // ifndef __KERNEL__

/****************************************************************************/
