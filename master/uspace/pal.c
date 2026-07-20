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

#include "pal.h"

#include "../master.h"
#include "../fsm_master.h"

static ec_log_cb_t ec_log_callback = NULL;
static pthread_mutex_t ec_log_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *loglevel_names[] = {
    "EMERG",
    "ALERT",
    "CRIT",
    "ERR",
    "WARN",
    "NOTICE",
    "INFO",
    "DEBUG",
};

void ec_log_set_callback(ec_log_cb_t cb)
{
    ec_log_callback = cb;
}

/****************************************************************************/
/* Lock-free log ring                                                       */
/*                                                                          */
/* ec_log() is reachable from the application's realtime thread (datagram  */
/* timeouts, working counter changes, link state), so the fallback without */
/* an application callback must not block. While the drainer thread runs,  */
/* messages are formatted into a lock-free MPSC ring and written to stderr */
/* by the drainer; a full ring drops messages and reports the count.       */
/* Outside the drainer's lifetime (before ecrt_lib_init() / after          */
/* cleanup) the direct mutex-protected stderr path is used — those        */
/* messages cannot originate from a realtime context.                       */
/****************************************************************************/

#define EC_LOG_RING_SIZE 64 /* power of two */
#define EC_LOG_MSG_SIZE 224

typedef struct {
    atomic_int ready; /**< 0 = free, 1 = ready to print. */
    int level;
    char msg[EC_LOG_MSG_SIZE];
} ec_log_slot_t;

static ec_log_slot_t log_ring[EC_LOG_RING_SIZE];
static atomic_uint log_head; /**< Next slot to claim (producers). */
static atomic_uint log_tail; /**< Next slot to drain (drainer). */
static atomic_ulong log_dropped;
static sem_t log_sem;
static pthread_t log_drainer;
static atomic_int log_ring_active; /**< 0 = off, 1 = running, 2 = stopping */

static void ec_log_print_direct(int level, const char *msg)
{
    if (level >= 0 && level <= 7) {
        fprintf(stderr, "[%s] %s", loglevel_names[level], msg);
    } else {
        fputs(msg, stderr);
    }
    fflush(stderr);
}

static void *ec_log_drainer_fn(void *arg)
{
    (void) arg;

    for (;;) {
        sem_wait(&log_sem);

        for (;;) {
            unsigned int t = atomic_load_explicit(&log_tail,
                    memory_order_relaxed);
            ec_log_slot_t *slot = &log_ring[t & (EC_LOG_RING_SIZE - 1)];

            if (atomic_load_explicit(&slot->ready, memory_order_acquire)
                    != 1) {
                break; /* next slot not (yet) ready */
            }
            ec_log_print_direct(slot->level, slot->msg);
            atomic_store_explicit(&slot->ready, 0, memory_order_release);
            atomic_store_explicit(&log_tail, t + 1, memory_order_release);
        }

        {
            unsigned long dropped =
                atomic_exchange_explicit(&log_dropped, 0,
                        memory_order_relaxed);
            if (dropped) {
                fprintf(stderr, "[WARN] EtherCAT: %lu log message(s)"
                        " dropped (ring full)\n", dropped);
                fflush(stderr);
            }
        }

        if (atomic_load_explicit(&log_ring_active, memory_order_acquire)
                == 2) {
            return NULL;
        }
    }
}

int ec_pal_log_start(void)
{
    int ret;

    if (atomic_load(&log_ring_active) == 1) {
        return 0;
    }

    memset(log_ring, 0, sizeof(log_ring));
    atomic_store(&log_head, 0);
    atomic_store(&log_tail, 0);
    atomic_store(&log_dropped, 0);
    if (sem_init(&log_sem, 0, 0)) {
        return -errno;
    }

    atomic_store(&log_ring_active, 1);
    ret = pthread_create(&log_drainer, NULL, ec_log_drainer_fn, NULL);
    if (ret) {
        atomic_store(&log_ring_active, 0);
        sem_destroy(&log_sem);
        return -ret;
    }
    return 0;
}

void ec_pal_log_stop(void)
{
    if (atomic_load(&log_ring_active) != 1) {
        return;
    }
    atomic_store_explicit(&log_ring_active, 2, memory_order_release);
    sem_post(&log_sem); /* final drain + exit */
    pthread_join(log_drainer, NULL);
    atomic_store(&log_ring_active, 0);
    sem_destroy(&log_sem);
}

/** Nonblocking log path: claim a slot, format, mark ready. */
static void ec_log_ring_put(int level, const char *fmt, va_list args)
{
    unsigned int head;
    ec_log_slot_t *slot;

    for (;;) {
        head = atomic_load_explicit(&log_head, memory_order_relaxed);
        if (head - atomic_load_explicit(&log_tail, memory_order_acquire)
                >= EC_LOG_RING_SIZE) {
            atomic_fetch_add_explicit(&log_dropped, 1,
                    memory_order_relaxed);
            sem_post(&log_sem); /* make sure the drop gets reported */
            return;
        }
        if (atomic_compare_exchange_weak_explicit(&log_head, &head,
                    head + 1, memory_order_acq_rel, memory_order_relaxed)) {
            break;
        }
    }

    slot = &log_ring[head & (EC_LOG_RING_SIZE - 1)];
    slot->level = level;
    vsnprintf(slot->msg, sizeof(slot->msg), fmt, args);
    atomic_store_explicit(&slot->ready, 1, memory_order_release);
    sem_post(&log_sem);
}

/* TRUSTED: with an application callback the nonblocking contract is
 * delegated to the application (documented at ecrt_lib_init()); without
 * one, RT-context messages go through the lock-free ring above
 * (vsnprintf + atomics + sem_post, all nonblocking); the mutex-guarded
 * direct path is only reachable outside the drainer's lifetime, where
 * no RT context exists. */
EC_RT_TRUSTED_BEGIN
void ec_log(int level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    if (ec_log_callback) {
        ec_log_callback(level, fmt, args);
    } else if (atomic_load_explicit(&log_ring_active, memory_order_acquire)
            == 1) {
        ec_log_ring_put(level, fmt, args);
    } else {
        /* Before ecrt_lib_init() / after cleanup: direct stderr. */
        char msg[EC_LOG_MSG_SIZE];
        vsnprintf(msg, sizeof(msg), fmt, args);
        pthread_mutex_lock(&ec_log_lock);
        ec_log_print_direct(level, msg);
        pthread_mutex_unlock(&ec_log_lock);
    }

    va_end(args);
}
EC_RT_TRUSTED_END

static void ec_master_nanosleep(const unsigned long nsecs) {
    struct timespec ts = {
        .tv_sec = nsecs / 1000000000UL,
        .tv_nsec = nsecs % 1000000000UL
    };

    clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

void ec_master_idle_thread_schedule(ec_master_t *master, int sent_bytes) {
    if (ec_fsm_master_idle(&master->fsm)) {
        ec_master_nanosleep(master->send_interval * 1000);
    } else {
        ec_master_nanosleep(sent_bytes * EC_BYTE_TRANSMISSION_TIME_NS);
    }
}

void ec_master_operation_thread_schedule(ec_master_t *master) {
    // the op thread should not work faster than the sending RT thread
    ec_master_nanosleep(master->send_interval * 1000);
}

/* TRUSTED: clock_gettime(CLOCK_MONOTONIC) is a nonblocking vDSO call;
 * the function-effects analysis cannot see that. */
EC_RT_TRUSTED_BEGIN
ec_time_t ec_current_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);  // or CLOCK_REALTIME for wall-clock time
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
EC_RT_TRUSTED_END

/* TRUSTED: the rate-limiter state is a deliberate static local and
 * time() is a nonblocking vDSO call; the function-effects analysis
 * cannot see either. */
EC_RT_TRUSTED_BEGIN
int ec_log_ratelimit(void)
{
    static time_t last_time = 0;
    time_t now = time(NULL);

    if (now - last_time >= 1) {
        last_time = now;
        return 1;  /* Allow message */
    }
    return 0;  /* Rate limited */
}
EC_RT_TRUSTED_END
