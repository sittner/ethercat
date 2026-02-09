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
   Platform Abstraction Layer (PAL) interface.
   
   This header provides platform-independent macros that map to either
   kernel or userspace implementations, enabling shared code between
   kernel and userspace builds.
*/

/****************************************************************************/

#ifndef __EC_PAL_H__
#define __EC_PAL_H__

/****************************************************************************/

#ifdef __KERNEL__

/****************************************************************************/
/* Kernel Platform Abstraction Layer */
/****************************************************************************/

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/atomic.h>

/* Memory allocation */
#define ec_pal_malloc(size)         kmalloc(size, GFP_KERNEL)
#define ec_pal_zalloc(size)         kzalloc(size, GFP_KERNEL)
#define ec_pal_free(ptr)            kfree(ptr)
#define ec_pal_malloc_atomic(size)  kmalloc(size, GFP_ATOMIC)

/* Spinlocks */
#define ec_pal_spinlock_t           spinlock_t
#define ec_pal_spin_init(lock)      spin_lock_init(lock)
#define ec_pal_spin_lock(lock)      spin_lock(lock)
#define ec_pal_spin_unlock(lock)    spin_unlock(lock)
#define ec_pal_spin_lock_irqsave(lock, flags) \
    spin_lock_irqsave(lock, flags)
#define ec_pal_spin_unlock_irqrestore(lock, flags) \
    spin_unlock_irqrestore(lock, flags)

/* Mutexes */
#define ec_pal_mutex_t              struct mutex
#define ec_pal_mutex_init(mutex)    mutex_init(mutex)
#define ec_pal_mutex_lock(mutex)    mutex_lock(mutex)
#define ec_pal_mutex_unlock(mutex)  mutex_unlock(mutex)
#define ec_pal_mutex_trylock(mutex) mutex_trylock(mutex)

/* Semaphores */
#define ec_pal_semaphore_t          struct semaphore
#define ec_pal_sem_init(sem, val)   sema_init(sem, val)
#define ec_pal_sem_down(sem)        down(sem)
#define ec_pal_sem_up(sem)          up(sem)
#define ec_pal_sem_trydown(sem)     down_trylock(sem)

/* Time functions */
#define ec_pal_time_now()           ktime_get_real_seconds()
#define ec_pal_msleep(ms)           msleep(ms)
#define ec_pal_get_jiffies()        jiffies

/* Logging */
#define EC_PAL_INFO(fmt, args...)   printk(KERN_INFO "EtherCAT: " fmt, ##args)
#define EC_PAL_ERR(fmt, args...)    printk(KERN_ERR "EtherCAT ERROR: " fmt, ##args)
#define EC_PAL_WARN(fmt, args...)   printk(KERN_WARNING "EtherCAT WARNING: " fmt, ##args)
#define EC_PAL_DBG(fmt, args...)    printk(KERN_DEBUG "EtherCAT DEBUG: " fmt, ##args)

/* Atomics */
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

/* Compiler hints */
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

/****************************************************************************/

#else /* !__KERNEL__ */

/****************************************************************************/
/* Userspace Platform Abstraction Layer */
/****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdatomic.h>

/* Memory allocation */
#define ec_pal_malloc(size)         malloc(size)
#define ec_pal_zalloc(size)         calloc(1, size)
#define ec_pal_free(ptr)            free(ptr)
#define ec_pal_malloc_atomic(size)  malloc(size)

/* Spinlocks (mapped to mutexes in userspace) */
#define ec_pal_spinlock_t           pthread_mutex_t
#define ec_pal_spin_init(lock)      pthread_mutex_init(lock, NULL)
#define ec_pal_spin_lock(lock)      pthread_mutex_lock(lock)
#define ec_pal_spin_unlock(lock)    pthread_mutex_unlock(lock)
#define ec_pal_spin_lock_irqsave(lock, flags) \
    do { (void)(flags); pthread_mutex_lock(lock); } while (0)
#define ec_pal_spin_unlock_irqrestore(lock, flags) \
    do { (void)(flags); pthread_mutex_unlock(lock); } while (0)

/* Mutexes */
#define ec_pal_mutex_t              pthread_mutex_t
#define ec_pal_mutex_init(mutex)    pthread_mutex_init(mutex, NULL)
#define ec_pal_mutex_lock(mutex)    pthread_mutex_lock(mutex)
#define ec_pal_mutex_unlock(mutex)  pthread_mutex_unlock(mutex)
#define ec_pal_mutex_trylock(mutex) (pthread_mutex_trylock(mutex) == 0)

/* Semaphores */
#define ec_pal_semaphore_t          sem_t
#define ec_pal_sem_init(sem, val)   sem_init(sem, 0, val)
#define ec_pal_sem_down(sem)        sem_wait(sem)
#define ec_pal_sem_up(sem)          sem_post(sem)
#define ec_pal_sem_trydown(sem)     sem_trywait(sem)

/* Time functions */
#define ec_pal_time_now()           time(NULL)
#define ec_pal_msleep(ms)           usleep((ms) * 1000)

static inline unsigned long ec_pal_get_jiffies(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Logging */
#define EC_PAL_INFO(fmt, args...)   printf("EtherCAT: " fmt, ##args)
#define EC_PAL_ERR(fmt, args...)    fprintf(stderr, "EtherCAT ERROR: " fmt, ##args)
#define EC_PAL_WARN(fmt, args...)   fprintf(stderr, "EtherCAT WARNING: " fmt, ##args)
#define EC_PAL_DBG(fmt, args...)    printf("EtherCAT DEBUG: " fmt, ##args)

/* Atomics (using C11 stdatomic) */
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

/* Compiler hints (userspace) */
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

/****************************************************************************/

#endif /* __KERNEL__ */

/****************************************************************************/

#endif /* __EC_PAL_H__ */
