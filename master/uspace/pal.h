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
   Userspace Platform Abstraction Layer (PAL) interface.
   
   This header provides userspace-specific platform abstractions for the
   EtherCAT master. It defines data types, macros, and inline functions
   that allow shared code to compile for userspace using POSIX APIs.
*/

/****************************************************************************/

#ifndef __EC_PAL_USERSPACE_H__
#define __EC_PAL_USERSPACE_H__

/****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdatomic.h>
#include <errno.h>
#include "uspace/list.h"

/****************************************************************************/
/* Memory allocation */
/****************************************************************************/

#define ec_pal_malloc(size)         malloc(size)
#define ec_pal_zalloc(size)         calloc(1, size)
#define ec_pal_free(ptr)            free(ptr)
#define ec_pal_malloc_atomic(size)  malloc(size)
#define ec_pal_strdup(s)            strdup(s)

/****************************************************************************/
/* Spinlocks (mapped to mutexes in userspace) */
/****************************************************************************/

#define ec_pal_spinlock_t           pthread_mutex_t
#define ec_pal_spin_init(lock)      pthread_mutex_init(lock, NULL)
#define ec_pal_spin_lock(lock)      pthread_mutex_lock(lock)
#define ec_pal_spin_unlock(lock)    pthread_mutex_unlock(lock)
#define ec_pal_spin_lock_irqsave(lock, flags) \
    do { (void)(flags); pthread_mutex_lock(lock); } while (0)
#define ec_pal_spin_unlock_irqrestore(lock, flags) \
    do { (void)(flags); pthread_mutex_unlock(lock); } while (0)

/****************************************************************************/
/* Mutexes */
/****************************************************************************/

#define ec_pal_mutex_t              pthread_mutex_t
#define ec_pal_mutex_init(mutex)    pthread_mutex_init(mutex, NULL)
#define ec_pal_mutex_lock(mutex)    pthread_mutex_lock(mutex)
#define ec_pal_mutex_unlock(mutex)  pthread_mutex_unlock(mutex)
#define ec_pal_mutex_trylock(mutex) (pthread_mutex_trylock(mutex) == 0)

/****************************************************************************/
/* Semaphores */
/****************************************************************************/

#define ec_pal_semaphore_t          sem_t
#define ec_pal_sem_init(sem, val)   sem_init(sem, 0, val)
#define ec_pal_sem_down(sem)        sem_wait(sem)
#define ec_pal_sem_up(sem)          sem_post(sem)
#define ec_pal_sem_trydown(sem)     sem_trywait(sem)
#define ec_pal_sem_down_interruptible(sem) sem_wait(sem)  /* No interrupts in userspace */

/****************************************************************************/
/* Wait queues (using condition variables) */
/****************************************************************************/

typedef struct {
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} ec_pal_wait_queue_t;

static inline void ec_pal_wait_queue_init(ec_pal_wait_queue_t *wq) {
    pthread_cond_init(&wq->cond, NULL);
    pthread_mutex_init(&wq->mutex, NULL);
}

static inline void ec_pal_wake_up(ec_pal_wait_queue_t *wq) {
    pthread_mutex_lock(&wq->mutex);
    pthread_cond_signal(&wq->cond);
    pthread_mutex_unlock(&wq->mutex);
}

static inline void ec_pal_wake_up_all(ec_pal_wait_queue_t *wq) {
    pthread_mutex_lock(&wq->mutex);
    pthread_cond_broadcast(&wq->cond);
    pthread_mutex_unlock(&wq->mutex);
}

/* For wait_event, caller must handle the condition check loop */
#define ec_pal_wait_event(wq, cond) \
    do { \
        pthread_mutex_lock(&(wq)->mutex); \
        while (!(cond)) { \
            pthread_cond_wait(&(wq)->cond, &(wq)->mutex); \
        } \
        pthread_mutex_unlock(&(wq)->mutex); \
    } while (0)

/****************************************************************************/
/* Master locking (convenience macros for master semaphores) */
/****************************************************************************/

#define ec_master_lock(m)               sem_wait(&(m)->plat.master_sem)
#define ec_master_unlock(m)             sem_post(&(m)->plat.master_sem)
#define ec_master_lock_interruptible(m) sem_wait(&(m)->plat.master_sem)  /* No interrupts in userspace */
#define ec_device_lock(m)               sem_wait(&(m)->plat.device_sem)
#define ec_device_unlock(m)             sem_post(&(m)->plat.device_sem)
#define ec_device_lock_interruptible(m) sem_wait(&(m)->plat.device_sem)  /* No interrupts in userspace */
#define ec_scan_lock(m)                 sem_wait(&(m)->plat.scan_sem)
#define ec_scan_unlock(m)               sem_post(&(m)->plat.scan_sem)
#define ec_config_lock(m)               sem_wait(&(m)->plat.config_sem)
#define ec_config_unlock(m)             sem_post(&(m)->plat.config_sem)
#define ec_ext_queue_lock(m)            sem_wait(&(m)->plat.ext_queue_sem)
#define ec_ext_queue_unlock(m)          sem_post(&(m)->plat.ext_queue_sem)
#define ec_ext_queue_trylock(m)         sem_trywait(&(m)->plat.ext_queue_sem)

/****************************************************************************/
/* Time functions */
/****************************************************************************/

#define ec_pal_time_now()           time(NULL)
#define ec_pal_msleep(ms)           usleep((ms) * 1000)

static inline unsigned long ec_pal_get_jiffies(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#define ec_pal_jiffies()            ec_pal_get_jiffies()
#define ec_pal_hz()                 1000  /* 1000 ms = 1 second */
#define ec_pal_time_after(a, b)     ((long)((b) - (a)) < 0)

/* Time conversion macros (identity in userspace since jiffies are already ms) */
#define msecs_to_jiffies(ms)        (ms)
#define jiffies_to_msecs(j)         (j)

/* 64-bit division (userspace can use native division)
 * NOTE: do_div modifies its first argument (n) in place. 
 * The first argument must be an lvalue, not an expression.
 */
#define do_div(n, base) ({ \
    uint32_t __rem; \
    __rem = (n) % (base); \
    (n) = (n) / (base); \
    __rem; \
})

/****************************************************************************/
/* Logging */
/****************************************************************************/

#define EC_PAL_INFO(fmt, args...)   printf("EtherCAT: " fmt, ##args)
#define EC_PAL_ERR(fmt, args...)    fprintf(stderr, "EtherCAT ERROR: " fmt, ##args)
#define EC_PAL_WARN(fmt, args...)   fprintf(stderr, "EtherCAT WARNING: " fmt, ##args)
#define EC_PAL_DBG(fmt, args...)    printf("EtherCAT DEBUG: " fmt, ##args)
#define EC_PAL_PRINT(fmt, args...)  printf(fmt, ##args)

/* Slave-specific logging macros */
#define EC_SLAVE_INFO(slave, fmt, args...) \
    printf("EtherCAT %u-%u: " fmt, slave->master->index, \
            slave->ring_position, ##args)

#define EC_SLAVE_ERR(slave, fmt, args...) \
    fprintf(stderr, "EtherCAT ERROR %u-%u: " fmt, slave->master->index, \
            slave->ring_position, ##args)

#define EC_SLAVE_WARN(slave, fmt, args...) \
    fprintf(stderr, "EtherCAT WARNING %u-%u: " fmt, \
            slave->master->index, slave->ring_position, ##args)

#define EC_SLAVE_DBG(slave, level, fmt, args...) \
    do { \
        if (slave->master->debug_level >= level) { \
            printf("EtherCAT DEBUG %u-%u: " fmt, \
                    slave->master->index, slave->ring_position, ##args); \
        } \
    } while (0)

/* Slave configuration-specific logging macros */
#define EC_CONFIG_INFO(sc, fmt, args...) \
    printf("EtherCAT %u %u:%u: " fmt, sc->master->index, \
            sc->alias, sc->position, ##args)

#define EC_CONFIG_ERR(sc, fmt, args...) \
    fprintf(stderr, "EtherCAT ERROR %u %u:%u: " fmt, sc->master->index, \
            sc->alias, sc->position, ##args)

#define EC_CONFIG_WARN(sc, fmt, args...) \
    fprintf(stderr, "EtherCAT WARNING %u %u:%u: " fmt, \
            sc->master->index, sc->alias, sc->position, ##args)

#define EC_CONFIG_DBG(sc, level, fmt, args...) \
    do { \
        if (sc->master->debug_level >= level) { \
            printf("EtherCAT DEBUG %u %u:%u: " fmt, \
                    sc->master->index, sc->alias, sc->position, ##args); \
        } \
    } while (0)

/****************************************************************************/
/* Atomics (using C11 stdatomic) */
/****************************************************************************/

typedef struct {
    atomic_int counter;
} ec_pal_atomic_t;

#define ec_pal_atomic_read(v)       atomic_load(&(v)->counter)
#define ec_pal_atomic_set(v, i)     atomic_store(&(v)->counter, i)
#define ec_pal_atomic_inc(v)        do { (void)atomic_fetch_add(&(v)->counter, 1); } while (0)
#define ec_pal_atomic_dec(v)        do { (void)atomic_fetch_sub(&(v)->counter, 1); } while (0)
#define ec_pal_atomic_add(i, v)     do { (void)atomic_fetch_add(&(v)->counter, i); } while (0)
#define ec_pal_atomic_sub(i, v)     do { (void)atomic_fetch_sub(&(v)->counter, i); } while (0)
#define ec_pal_atomic_inc_return(v) (atomic_fetch_add(&(v)->counter, 1) + 1)
#define ec_pal_atomic_dec_return(v) (atomic_fetch_sub(&(v)->counter, 1) - 1)
#define ec_pal_atomic_dec_and_test(v) (atomic_fetch_sub(&(v)->counter, 1) == 1)

/****************************************************************************/
/* Compiler hints (userspace) */
/****************************************************************************/

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

/****************************************************************************/
/* Error pointer helpers */
/****************************************************************************/

#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) ((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
#define ERR_PTR(error) ((void *)((long)(error)))
#define PTR_ERR(ptr) ((long)(ptr))
#define IS_ERR(ptr) IS_ERR_VALUE((unsigned long)(ptr))
#define IS_ERR_OR_NULL(ptr) (!(ptr) || IS_ERR(ptr))

/****************************************************************************/
/* Ethernet constants */
/****************************************************************************/

#ifndef ETH_ALEN
#define ETH_ALEN        6    /* Octets in one ethernet address */
#endif

#ifndef ETH_HLEN
#define ETH_HLEN        14   /* Total octets in header */
#endif

#ifndef ETH_DATA_LEN
#define ETH_DATA_LEN    1500 /* Max octets in payload */
#endif

/****************************************************************************/
/* Module macros (no-op in userspace) */
/****************************************************************************/

#define EXPORT_SYMBOL(sym)
#define MODULE_AUTHOR(author)
#define MODULE_DESCRIPTION(desc)
#define MODULE_LICENSE(license)
#define MODULE_VERSION(version)

/****************************************************************************/
/* Min/max macros */
/****************************************************************************/

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

/****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/* Kernel-compatible integer types for userspace */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* Forward declarations */
struct ec_master;

/* Forward declaration for transport */
typedef struct ec_transport ec_transport_t;

/****************************************************************************/
/* Platform-specific types */
/****************************************************************************/

/** Userspace-specific device fields. */
typedef struct {
    ec_transport_t *transport;           /**< Transport layer. */
    uint64_t jiffies_poll;               /**< Time of last poll (ms). */
    uint64_t last_link_check;            /**< Time of last link state check (ms). */
    int last_link_state;                 /**< Last reported link state (-1 = unknown). */
} ec_device_plat_t;

/** Userspace-specific master fields. */
typedef struct {
    sem_t master_sem;                   /**< Master semaphore. */
    sem_t device_sem;                   /**< Device semaphore. */
    sem_t scan_sem;                     /**< Scan semaphore. */
    sem_t config_sem;                   /**< Configuration semaphore. */
    sem_t ext_queue_sem;                /**< External queue semaphore. */

    pthread_t thread;                   /**< Master thread. */
    pthread_mutex_t io_mutex;           /**< Mutex for I/O operations. */

    ec_pal_wait_queue_t scan_queue;     /**< Queue for scan state changes. */
    ec_pal_wait_queue_t config_queue;   /**< Queue for config state changes. */
    ec_pal_wait_queue_t request_queue;  /**< Wait queue for external requests. */

#ifdef EC_EOE
    pthread_t eoe_thread;               /**< EoE thread. */
    ec_pal_wait_queue_t eoe_queue;      /**< Wait queue for EoE. */
#endif
} ec_master_plat_t;

/****************************************************************************/

#endif /* __EC_PAL_USERSPACE_H__ */
