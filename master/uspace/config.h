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
   Userspace configuration header stub.
*/

#ifndef __EC_USPACE_CONFIG_H__
#define __EC_USPACE_CONFIG_H__

/* Version macros for globals.h */
#define VERSION "1.6.0"
#define REV "devel"

/* Library version */
#define EC_LIB_VERSION "1.6.0-devel"

/* Maximum number of EtherCAT devices per master */
#define EC_MAX_NUM_DEVICES 1

/* Number of state machines used for configuration */
#define EC_NUM_FSM 16

/* EoE support - disabled for userspace */
#undef EC_EOE

/* Enable debug output */
#undef EC_DEBUG_IF
#undef EC_DEBUG_RING

/* Enable profiling */
#undef EC_HAVE_CYCLES

/* RT syslog - disabled for userspace */
#undef EC_RT_SYSLOG

#endif /* __EC_USPACE_CONFIG_H__ */
