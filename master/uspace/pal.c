/******************************************************************************
 *
 *  Copyright (C) 2006-2025  Florian Pose, Ingenieurgemeinschaft IgH
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
 *****************************************************************************/

/**
 * \file
 * Platform Abstraction Layer - Userspace Implementation
 *
 * This file implements the userspace side of the Platform Abstraction Layer.
 * It provides POSIX-based implementations of the PAL functions defined in
 * master/pal.h for userspace builds.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#include "pal.h"

/******************************************************************************
 * Constants
 *****************************************************************************/

/* Constants moved to appropriate locations */

/******************************************************************************
 * Memory Allocation
 *****************************************************************************/

/* Memory allocation functions are implemented as macros in pal.h */

/******************************************************************************
 * Time Functions
 *****************************************************************************/

/* Time functions are implemented as macros in pal.h */

/******************************************************************************
 * Logging
 *****************************************************************************/

/* Logging functions are implemented as macros in pal.h */

/******************************************************************************
 * Global Running Flag
 ******************************************************************************/

/** Global running flag - set by signal handler to indicate shutdown. */
volatile int ec_pal_running = 1;

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

/* Helper functions for master initialization have been moved to master_main.c */

/*****************************************************************************/
