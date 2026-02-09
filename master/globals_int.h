/*****************************************************************************
 *
 *  Copyright (C) 2006-2024  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT master.
 *
 *  The file is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by the
 *  Free Software Foundation; version 2.1 of the License.
 *
 *  This file is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 *  License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this file. If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************************/

/** \file
 * Internal global definitions and macros for master code.
 * 
 * This header includes PAL and provides logging macros.
 * Only for use by master/*.c files, not for tool or external code.
 */

/****************************************************************************/

#ifndef __EC_MASTER_GLOBALS_INT_H__
#define __EC_MASTER_GLOBALS_INT_H__

#include "globals.h"
#include "pal.h"

/****************************************************************************/

/** Convenience macro for printing EtherCAT-specific information to syslog.
 *
 * This will print the message in \a fmt with a prefixed "EtherCAT: ".
 *
 * \param fmt format string (like in printf())
 * \param args arguments (optional)
 */
#define EC_INFO(fmt, args...) \
    EC_PAL_INFO(fmt, ##args)

/** Convenience macro for printing EtherCAT-specific errors to syslog.
 *
 * This will print the message in \a fmt with a prefixed "EtherCAT ERROR: ".
 *
 * \param fmt format string (like in printf())
 * \param args arguments (optional)
 */
#define EC_ERR(fmt, args...) \
    EC_PAL_ERR(fmt, ##args)

/** Convenience macro for printing EtherCAT-specific warnings to syslog.
 *
 * This will print the message in \a fmt with a prefixed "EtherCAT WARNING: ".
 *
 * \param fmt format string (like in printf())
 * \param args arguments (optional)
 */
#define EC_WARN(fmt, args...) \
    EC_PAL_WARN(fmt, ##args)

/** Convenience macro for printing EtherCAT debug messages to syslog.
 *
 * This will print the message in \a fmt with a prefixed "EtherCAT DEBUG: ".
 *
 * \param fmt format string (like in printf())
 * \param args arguments (optional)
 */
#define EC_DBG(fmt, args...) \
    EC_PAL_DBG(fmt, ##args)

/****************************************************************************/

#endif /* __EC_MASTER_GLOBALS_INT_H__ */
