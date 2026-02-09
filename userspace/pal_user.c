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

#include "include/ecrt_user.h"

/******************************************************************************
 * Memory Allocation
 *****************************************************************************/

/* These are typically macros in pal.h, but provide function versions if needed */

void *ec_pal_malloc_func(size_t size)
{
    return malloc(size);
}

void *ec_pal_zalloc_func(size_t size)
{
    return calloc(1, size);
}

void ec_pal_free_func(void *ptr)
{
    free(ptr);
}

/******************************************************************************
 * Time Functions
 *****************************************************************************/

uint64_t ec_pal_jiffies(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;  /* milliseconds */
}

void ec_pal_usleep(unsigned long usecs)
{
    usleep(usecs);
}

void ec_pal_msleep(unsigned long msecs)
{
    usleep(msecs * 1000);
}

/******************************************************************************
 * Logging
 *****************************************************************************/

void ec_pal_print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

/******************************************************************************
 * Userspace Master Management - Stub Implementations
 *****************************************************************************/

int ecrt_master_init(
    unsigned int master_index,
    ec_pal_device_type_t device_type,
    const char *interface
) {
    /* TODO: Implement master initialization */
    return -ENOSYS;
}

void ecrt_master_cleanup(unsigned int master_index)
{
    /* TODO: Implement master cleanup */
}

void ecrt_master_idle(unsigned int master_index)
{
    /* TODO: Implement idle processing */
}

int ecrt_master_process_control(unsigned int master_index)
{
    /* TODO: Implement control interface processing */
    return -ENOSYS;
}

/*****************************************************************************/
