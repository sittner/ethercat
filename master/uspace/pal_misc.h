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
   Platform Abstraction Layer for userspace EtherCAT master.
*/

/****************************************************************************/

#ifndef __EC_USPACE_PAL_MISC_H__
#define __EC_USPACE_PAL_MISC_H__

/****************************************************************************/
/* Kernel-compatible integer types for userspace */
/****************************************************************************/

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* Kernel module exports - not needed in userspace */
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)

#include "list.h"

#define GFP_KERNEL  0

#define kmalloc(size, flags)    malloc(size)
#define kzalloc(size, flags)    calloc(1, size)
#define kfree(ptr)              free(ptr)

#define vmalloc(size)           malloc(size)
#define vfree(ptr)              free(ptr)

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x)   __builtin_expect(!!(x), 1)

//************************************************************************

/****************************************************************************/
/* Logging */
/****************************************************************************/

#define KERN_ERR		"<3>"	/* error conditions			*/
#define KERN_WARNING	"<4>"	/* warning conditions			*/
#define KERN_INFO		"<6>"	/* informational			*/
#define KERN_DEBUG		"<7>"	/* debug-level messages			*/

/*
 * Annotation for a "continued" line of log printout (only done after a
 * line that had no enclosing \n). Only to be used by core/arch code
 * during early bootup (a continued line is not SMP-safe otherwise).
 */
#define KERN_CONT		"<c>"

int printk(const char *fmt, ...);

/****************************************************************************/
/* Kernel utility macros */
/****************************************************************************/

/* min/max macros with strict type checking */
#define min(a, b) \
    ({ \
        typeof(a) _a = (a); \
        typeof(b) _b = (b); \
        _a < _b ? _a : _b; \
    })

#define max(a, b) \
    ({ \
        typeof(a) _a = (a); \
        typeof(b) _b = (b); \
        _a > _b ? _a : _b; \
    })

/****************************************************************************/
/* 64-bit division helpers */
/****************************************************************************/

/**
 * do_div - 64-bit division with remainder
 * @n: dividend (modified in place to become quotient)
 * @base: divisor
 *
 * Returns: remainder
 *
 * In kernel, this handles 64-bit division on 32-bit architectures.
 * In userspace, we can just use regular division.
 */
#define do_div(n, base) \
    ({ \
        uint64_t __n = (n); \
        uint32_t __base = (base); \
        uint32_t __rem = __n % __base; \
        (n) = __n / __base; \
        __rem; \
    })

/****************************************************************************/
/* Ethernet constants */
/****************************************************************************/

#ifndef ETH_ALEN
#define ETH_ALEN        6       /* Octets in one ethernet address */
#endif

#ifndef ETH_HLEN
#define ETH_HLEN        14      /* Total octets in header */
#endif

#ifndef ETH_DATA_LEN
#define ETH_DATA_LEN    1500    /* Max octets in payload */
#endif

#ifndef ETH_ZLEN
#define ETH_ZLEN        60      /* Min octets in frame sans FCS */
#endif

#ifndef ETH_FRAME_LEN
#define ETH_FRAME_LEN   1514    /* Max octets in frame sans FCS */
#endif

/****************************************************************************/
/* String conversion functions */
/****************************************************************************/

#define simple_strtoul(str, endp, base)  strtoul(str, endp, base)

#endif /* __EC_USPACE_PAL_MISC_H__ */

