/*****************************************************************************
 *
 *  Copyright (C) 2006-2008  Florian Pose, Ingenieurgemeinschaft IgH
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
   EtherCAT master character device (userspace IPC socket server).
*/

/****************************************************************************/

#ifndef __EC_CDEV_H__
#define __EC_CDEV_H__

/****************************************************************************/

/* Forward declaration; full definition provided by pal.h → master.h. */
struct ec_master;

/****************************************************************************/

/** Start the global IPC server on the given socket path.
 *
 *  Creates the listening Unix domain socket and starts the listener thread.
 *  Called by ecrt_lib_init() when socket_path is not NULL.
 *
 *  \return 0 on success, negative error code on failure.
 */
int ec_ipc_server_start(const char *socket_path);

/** Stop the global IPC server.
 *
 *  Closes the listening socket and waits for the listener thread to exit.
 *  Called by ecrt_lib_cleanup().
 */
void ec_ipc_server_stop(void);

/****************************************************************************/

/** Register a master in the global registry.
 *
 *  Called by ecrt_startup_master_common() after successful master init.
 */
void ec_master_registry_add(struct ec_master *master);

/** Unregister a master from the global registry.
 *
 *  Called by ecrt_release_master() before tearing down the master.
 */
void ec_master_registry_remove(struct ec_master *master);

/** Look up a master by index.
 *
 *  \return Pointer to master, or NULL if index is out of range or unregistered.
 */
struct ec_master *ec_master_registry_find(unsigned int index);

/** Return the number of registered masters. */
unsigned int ec_master_registry_count(void);

/** Remove and return one registered master (if any).
 *
 *  The returned master is unregistered from the global registry while under
 *  lock, so callers can safely release it afterwards.
 *
 *  \return Pointer to an unregistered master, or NULL if none are registered.
 */
struct ec_master *ec_master_registry_pop_first(void);

/****************************************************************************/

#endif
