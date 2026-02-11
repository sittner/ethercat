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

/**
   \file
   Platform Abstraction Layer - System and kernel includes.
*/

/****************************************************************************/

#ifndef __EC_PRIV_H__
#define __EC_PRIV_H__

#include "pal.h"

#include "../device.h"

void ec_device_attach(ec_device_t *, struct net_device *, ec_pollfunc_t,
        struct module *);
void ec_device_detach(ec_device_t *);

int ec_device_open(ec_device_t *);
int ec_device_close(ec_device_t *);

#endif // __EC_PRIV_H__
