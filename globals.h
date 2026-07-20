/******************************************************************************
 *
 *  Copyright (C) 2006-2021  Florian Pose, Ingenieurgemeinschaft IgH
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
 *****************************************************************************/

/**
   \file
   Global definitions and macros.
*/

/*****************************************************************************/

#ifndef __EC_GLOBALS_H__
#define __EC_GLOBALS_H__

#include "config.h"

/******************************************************************************
 *  Overall macros
 *****************************************************************************/

/** Helper macro for EC_STR(), literates a macro argument.
 *
 * \param X argument to literate.
 */
#define EC_LIT(X) #X

/** Converts a macro argument to a string.
 *
 * \param X argument to stringify.
 */
#define EC_STR(X) EC_LIT(X)

/** Master version string
 */
#define EC_MASTER_VERSION VERSION " " EC_STR(REV)

/*****************************************************************************/

/** Realtime function-effect annotation for the INTERNAL cyclic call
 * tree (the public API twin is ECRT_RT_ATTR in ecrt.h — keep the
 * version gates in sync). Backed by clang's function-effects analysis
 * (clang >= 20, opt-in via -Wfunction-effects, see
 * script/rt-effects-check.sh); expands to nothing on GCC and older
 * clang. EC_RT_TRUSTED_BEGIN/END wrap a definition that is declared
 * EC_RT_ATTR but cannot be verified by the compiler (e.g. a
 * nonblocking-by-flag syscall); every use is a trust boundary of the
 * RT path and must carry a justification comment. */
#if defined(__clang__) && (__clang_major__ >= 20) && defined(__has_attribute)
#if __has_attribute(nonblocking)
#define EC_RT_ATTR __attribute__((nonblocking))
#define EC_RT_TRUSTED_BEGIN \
    _Pragma("clang diagnostic push") \
    _Pragma("clang diagnostic ignored \"-Wfunction-effects\"")
#define EC_RT_TRUSTED_END \
    _Pragma("clang diagnostic pop")
#endif
#endif
#ifndef EC_RT_ATTR
#define EC_RT_ATTR
#define EC_RT_TRUSTED_BEGIN
#define EC_RT_TRUSTED_END
#endif

/*****************************************************************************/

#endif
