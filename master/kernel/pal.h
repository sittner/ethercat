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
   Kernel Platform Abstraction Layer (PAL) interface.
   
   This header provides kernel-specific platform abstractions for the
   EtherCAT master. It defines data types, macros, and inline functions
   that allow shared code to compile for the Linux kernel.
*/

/****************************************************************************/

#ifndef __EC_PAL_KERNEL_H__
#define __EC_PAL_KERNEL_H__

/****************************************************************************/

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/atomic.h>

/****************************************************************************/
/* Memory allocation */
/****************************************************************************/

#define ec_pal_malloc(size)         kmalloc(size, GFP_KERNEL)
#define ec_pal_zalloc(size)         kzalloc(size, GFP_KERNEL)
#define ec_pal_free(ptr)            kfree(ptr)
#define ec_pal_malloc_atomic(size)  kmalloc(size, GFP_ATOMIC)

/****************************************************************************/
/* Spinlocks */
/****************************************************************************/

#define ec_pal_spinlock_t           spinlock_t
#define ec_pal_spin_init(lock)      spin_lock_init(lock)
#define ec_pal_spin_lock(lock)      spin_lock(lock)
#define ec_pal_spin_unlock(lock)    spin_unlock(lock)
#define ec_pal_spin_lock_irqsave(lock, flags) \
    spin_lock_irqsave(lock, flags)
#define ec_pal_spin_unlock_irqrestore(lock, flags) \
    spin_unlock_irqrestore(lock, flags)

/****************************************************************************/
/* Mutexes */
/****************************************************************************/

#define ec_pal_mutex_t              struct mutex
#define ec_pal_mutex_init(mutex)    mutex_init(mutex)
#define ec_pal_mutex_lock(mutex)    mutex_lock(mutex)
#define ec_pal_mutex_unlock(mutex)  mutex_unlock(mutex)
#define ec_pal_mutex_trylock(mutex) mutex_trylock(mutex)

/****************************************************************************/
/* Semaphores */
/****************************************************************************/

#define ec_pal_semaphore_t          struct semaphore
#define ec_pal_sem_init(sem, val)   sema_init(sem, val)
#define ec_pal_sem_down(sem)        down(sem)
#define ec_pal_sem_up(sem)          up(sem)
#define ec_pal_sem_trydown(sem)     down_trylock(sem)

/****************************************************************************/
/* Time functions */
/****************************************************************************/

#define ec_pal_time_now()           ktime_get_real_seconds()
#define ec_pal_msleep(ms)           msleep(ms)
#define ec_pal_get_jiffies()        jiffies
#define ec_pal_jiffies()            jiffies
#define ec_pal_hz()                 HZ
#define ec_pal_time_after(a, b)     time_after(a, b)

/****************************************************************************/
/* Logging */
/****************************************************************************/

#define EC_PAL_INFO(fmt, args...)   printk(KERN_INFO "EtherCAT: " fmt, ##args)
#define EC_PAL_ERR(fmt, args...)    printk(KERN_ERR "EtherCAT ERROR: " fmt, ##args)
#define EC_PAL_WARN(fmt, args...)   printk(KERN_WARNING "EtherCAT WARNING: " fmt, ##args)
#define EC_PAL_DBG(fmt, args...)    printk(KERN_DEBUG "EtherCAT DEBUG: " fmt, ##args)

/****************************************************************************/
/* Atomics */
/****************************************************************************/

#define ec_pal_atomic_t             atomic_t
#define ec_pal_atomic_read(v)       atomic_read(v)
#define ec_pal_atomic_set(v, i)     atomic_set(v, i)
#define ec_pal_atomic_inc(v)        do { atomic_inc(v); } while (0)
#define ec_pal_atomic_dec(v)        do { atomic_dec(v); } while (0)
#define ec_pal_atomic_add(i, v)     do { atomic_add(i, v); } while (0)
#define ec_pal_atomic_sub(i, v)     do { atomic_sub(i, v); } while (0)
#define ec_pal_atomic_inc_return(v) atomic_inc_return(v)
#define ec_pal_atomic_dec_return(v) atomic_dec_return(v)
#define ec_pal_atomic_dec_and_test(v) atomic_dec_and_test(v)

/****************************************************************************/
/* Compiler hints */
/****************************************************************************/

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

/****************************************************************************/

#include <linux/netdevice.h>
#include <linux/skbuff.h>

/* Forward declarations */
struct ec_master;

/* Device poll function type from ecdev.h */
typedef void (*ec_pollfunc_t)(struct net_device *);

/****************************************************************************/
/* Platform-specific types */
/****************************************************************************/

#define EC_TX_RING_SIZE 2

#ifdef EC_DEBUG_IF
/* Forward declaration */
typedef struct ec_debug ec_debug_t;
#endif

#ifdef EC_DEBUG_RING
#define EC_DEBUG_RING_SIZE 10

typedef enum {
    TX, RX
} ec_debug_frame_dir_t;

typedef struct {
    ec_debug_frame_dir_t dir;
    struct timeval t;
    uint8_t data[1518];  /* EC_MAX_DATA_SIZE equivalent */
    unsigned int data_size;
} ec_debug_frame_t;
#endif

/** Kernel-specific device fields. */
typedef struct {
    struct net_device *dev;              /**< Pointer to the network device. */
    ec_pollfunc_t poll;                  /**< Pointer to poll function. */
    struct module *module;               /**< Pointer to device's module. */
    struct sk_buff *tx_skb[EC_TX_RING_SIZE]; /**< Transmit socket buffers. */
    unsigned int tx_ring_index;          /**< Index into tx_skb ring. */
#ifdef EC_HAVE_CYCLES
    cycles_t cycles_poll;                /**< Cycles of last poll. */
#endif
#ifdef EC_DEBUG_RING
    struct timeval timeval_poll;         /**< Timeval of last poll. */
#endif
    unsigned long jiffies_poll;          /**< Jiffies of last poll. */
#ifdef EC_DEBUG_IF
    ec_debug_t dbg;                      /**< Debug device. */
#endif
#ifdef EC_DEBUG_RING
    ec_debug_frame_t debug_frames[EC_DEBUG_RING_SIZE];
    unsigned int debug_frame_index;
    unsigned int debug_frame_count;
#endif
} ec_device_plat_t;

/** Kernel-specific master fields. */
typedef struct {
    /* TODO: Platform-specific master fields will be added in future phases */
    int placeholder;  /* Temporary placeholder to avoid empty struct */
} ec_master_plat_t;

/****************************************************************************/

#endif /* __EC_PAL_KERNEL_H__ */
