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
#include <stdio.h>
#include <stdatomic.h>

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

/****************************************************************************/
/* Master locking (convenience macros for master semaphores) */
/****************************************************************************/

#define ec_master_lock(m)           sem_wait(&(m)->master_sem)
#define ec_master_unlock(m)         sem_post(&(m)->master_sem)
#define ec_device_lock(m)           sem_wait(&(m)->device_sem)
#define ec_device_unlock(m)         sem_post(&(m)->device_sem)
#define ec_scan_lock(m)             sem_wait(&(m)->scan_sem)
#define ec_scan_unlock(m)           sem_post(&(m)->scan_sem)
#define ec_config_lock(m)           sem_wait(&(m)->config_sem)
#define ec_config_unlock(m)         sem_post(&(m)->config_sem)
#define ec_ext_queue_lock(m)        sem_wait(&(m)->ext_queue_sem)
#define ec_ext_queue_unlock(m)      sem_post(&(m)->ext_queue_sem)

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

/****************************************************************************/
/* Logging */
/****************************************************************************/

#define EC_PAL_INFO(fmt, args...)   printf("EtherCAT: " fmt, ##args)
#define EC_PAL_ERR(fmt, args...)    fprintf(stderr, "EtherCAT ERROR: " fmt, ##args)
#define EC_PAL_WARN(fmt, args...)   fprintf(stderr, "EtherCAT WARNING: " fmt, ##args)
#define EC_PAL_DBG(fmt, args...)    printf("EtherCAT DEBUG: " fmt, ##args)

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

#include <stdint.h>

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
} ec_device_plat_t;

/** Userspace-specific master fields. */
typedef struct {
    /* TODO: Platform-specific master fields will be added in future phases */
    int placeholder;  /* Temporary placeholder to avoid empty struct */
} ec_master_plat_t;

/****************************************************************************/

#endif /* __EC_PAL_USERSPACE_H__ */
