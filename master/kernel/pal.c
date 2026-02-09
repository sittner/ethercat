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
   Kernel Platform Abstraction Layer implementation.
   
   This file provides the kernel-specific implementation of the Platform
   Abstraction Layer (PAL). Since the PAL primarily uses macros that map
   directly to kernel APIs, this file is minimal and mainly includes
   necessary headers.
*/

/****************************************************************************/

#include "pal.h"

/****************************************************************************/

/* This file is intentionally minimal.
 * The PAL for kernel builds is implemented via macros in pal.h
 * that directly map to kernel APIs. This file exists primarily
 * to be compiled as part of the kernel module build process.
 */

/****************************************************************************/
