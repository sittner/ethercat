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

/** \file
 * Realtime function-effect annotation macros, shared by ecrt.h and
 * ectp.h (single definition — do not copy these blocks elsewhere).
 */

#ifndef __ECRT_RT_H__
#define __ECRT_RT_H__

/** Realtime function-effect annotation.
 *
 * Every function whose documentation carries the \a rt_safe usage tag is
 * declared with this attribute. Backed by clang's function-effects
 * analysis (clang >= 20, opt-in via -Wfunction-effects): an application
 * that marks its own cyclic task function ECRT_RT_ATTR gets a compile-time
 * diagnostic if that function calls any part of this API that is not
 * realtime-safe (or any other blocking/allocating code). Expands to
 * nothing on GCC and older clang, so ABI and production builds are
 * unaffected. See script/rt-effects-check.sh.
 *
 * Overrideable: define ECRT_RT_ATTR before including this header to hand
 * the annotation over to a framework's own RT-check macro (e.g.
 * RTAPI_NONBLOCKING / GOMC_NONBLOCKING in LinuxCNC).
 */
#ifndef ECRT_RT_ATTR
# if defined(__clang__) && (__clang_major__ >= 20) && defined(__has_attribute)
#  if __has_attribute(nonblocking)
#   define ECRT_RT_ATTR __attribute__((nonblocking))
#  endif
# endif
# ifndef ECRT_RT_ATTR
#  define ECRT_RT_ATTR
# endif
#endif

/** Trust-boundary escape for the function-effects analysis.
 *
 * Wraps a function definition that is declared ECRT_RT_ATTR but cannot be
 * verified by the compiler (e.g. it performs a syscall that is nonblocking
 * by flag, which the analysis cannot see). Every use is a trust boundary
 * of the RT path and must carry a justification comment.
 */
#ifndef ECRT_RT_TRUSTED_BEGIN
# if defined(__clang__) && (__clang_major__ >= 20) && defined(__has_attribute)
#  if __has_attribute(nonblocking)
#   define ECRT_RT_TRUSTED_BEGIN \
    _Pragma("clang diagnostic push") \
    _Pragma("clang diagnostic ignored \"-Wfunction-effects\"")
#   define ECRT_RT_TRUSTED_END \
    _Pragma("clang diagnostic pop")
#  endif
# endif
# ifndef ECRT_RT_TRUSTED_BEGIN
#  define ECRT_RT_TRUSTED_BEGIN
#  define ECRT_RT_TRUSTED_END
# endif
#endif

#endif /* __ECRT_RT_H__ */
