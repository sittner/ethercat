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
#include <linux/timex.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/err.h>

/****************************************************************************/
/* Memory allocation */
/****************************************************************************/

#define ec_pal_malloc(size)         kmalloc(size, GFP_KERNEL)
#define ec_pal_zalloc(size)         kzalloc(size, GFP_KERNEL)
#define ec_pal_free(ptr)            kfree(ptr)
#define ec_pal_malloc_atomic(size)  kmalloc(size, GFP_ATOMIC)
#define ec_pal_strdup(s)            kstrdup(s, GFP_KERNEL)

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
#define ec_pal_sem_down_interruptible(sem) down_interruptible(sem)

/****************************************************************************/
/* Master locking (convenience macros for master semaphores) */
/****************************************************************************/

#define ec_master_lock(m)               down(&(m)->plat.master_sem)
#define ec_master_unlock(m)             up(&(m)->plat.master_sem)
#define ec_master_lock_interruptible(m) down_interruptible(&(m)->plat.master_sem)
#define ec_device_lock(m)               down(&(m)->plat.device_sem)
#define ec_device_unlock(m)             up(&(m)->plat.device_sem)
#define ec_device_lock_interruptible(m) down_interruptible(&(m)->plat.device_sem)
#define ec_scan_lock(m)                 down(&(m)->plat.scan_sem)
#define ec_scan_unlock(m)               up(&(m)->plat.scan_sem)
#define ec_config_lock(m)               down(&(m)->plat.config_sem)
#define ec_config_unlock(m)             up(&(m)->plat.config_sem)
#define ec_ext_queue_lock(m)            down(&(m)->plat.ext_queue_sem)
#define ec_ext_queue_unlock(m)          up(&(m)->plat.ext_queue_sem)
#define ec_ext_queue_trylock(m)         down_trylock(&(m)->plat.ext_queue_sem)

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
#define EC_PAL_PRINT(fmt, args...)  printk(KERN_CONT fmt, ##args)

/* Slave-specific logging macros */
#define EC_SLAVE_INFO(slave, fmt, args...) \
    printk(KERN_INFO "EtherCAT %u-%u: " fmt, slave->master->index, \
            slave->ring_position, ##args)

#define EC_SLAVE_ERR(slave, fmt, args...) \
    printk(KERN_ERR "EtherCAT ERROR %u-%u: " fmt, slave->master->index, \
            slave->ring_position, ##args)

#define EC_SLAVE_WARN(slave, fmt, args...) \
    printk(KERN_WARNING "EtherCAT WARNING %u-%u: " fmt, \
            slave->master->index, slave->ring_position, ##args)

#define EC_SLAVE_DBG(slave, level, fmt, args...) \
    do { \
        if (slave->master->debug_level >= level) { \
            printk(KERN_DEBUG "EtherCAT DEBUG %u-%u: " fmt, \
                    slave->master->index, slave->ring_position, ##args); \
        } \
    } while (0)

/* Slave configuration-specific logging macros */
#define EC_CONFIG_INFO(sc, fmt, args...) \
    printk(KERN_INFO "EtherCAT %u %u:%u: " fmt, sc->master->index, \
            sc->alias, sc->position, ##args)

#define EC_CONFIG_ERR(sc, fmt, args...) \
    printk(KERN_ERR "EtherCAT ERROR %u %u:%u: " fmt, sc->master->index, \
            sc->alias, sc->position, ##args)

#define EC_CONFIG_WARN(sc, fmt, args...) \
    printk(KERN_WARNING "EtherCAT WARNING %u %u:%u: " fmt, \
            sc->master->index, sc->alias, sc->position, ##args)

#define EC_CONFIG_DBG(sc, level, fmt, args...) \
    do { \
        if (sc->master->debug_level >= level) { \
            printk(KERN_DEBUG "EtherCAT DEBUG %u %u:%u: " fmt, \
                    sc->master->index, sc->alias, sc->position, ##args); \
        } \
    } while (0)

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

/* Forward declarations for master platform types */
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/rtmutex.h>
#include <linux/workqueue.h>
#include <linux/irq_work.h>
#include <linux/cdev.h>

/* Forward declaration */
struct ec_master;

/** EtherCAT master character device. */
struct ec_cdev {
    struct ec_master *master; /**< Master owning the device. */
    struct cdev cdev;         /**< Character device. */
};
typedef struct ec_cdev ec_cdev_t;

#ifdef EC_RTDM
struct rtdm_device;

/** EtherCAT RTDM device. */
struct ec_rtdm_dev {
    struct ec_master *master;    /**< Master pointer. */
    struct rtdm_device *dev;     /**< RTDM device. */
};
typedef struct ec_rtdm_dev ec_rtdm_dev_t;
#endif

/** Kernel-specific master fields. */
typedef struct {
    ec_cdev_t cdev;                     /**< Master character device. */
    struct device *class_device;        /**< Master class device. */

#ifdef EC_RTDM
    ec_rtdm_dev_t rtdm_dev;             /**< RTDM device. */
#endif

    struct semaphore master_sem;        /**< Master semaphore. */
    struct semaphore device_sem;        /**< Device semaphore. */
    struct semaphore scan_sem;          /**< Scan semaphore. */
    struct semaphore config_sem;        /**< Configuration semaphore. */
    struct semaphore ext_queue_sem;     /**< External queue semaphore. */

    struct task_struct *thread;         /**< Master thread. */
    struct rt_mutex io_mutex;           /**< Mutex for I/O operations. */

    wait_queue_head_t scan_queue;       /**< Queue for scan state changes. */
    wait_queue_head_t config_queue;     /**< Queue for config state changes. */
    wait_queue_head_t request_queue;    /**< Wait queue for external requests. */

#ifdef EC_EOE
    struct task_struct *eoe_thread;     /**< EoE thread. */
#endif

    struct irq_work sc_reset_work_kicker; /**< IRQ work for slave config reset. */
    struct work_struct sc_reset_work;     /**< Work struct for slave config reset. */
} ec_master_plat_t;

/****************************************************************************/

#endif /* __EC_PAL_KERNEL_H__ */
