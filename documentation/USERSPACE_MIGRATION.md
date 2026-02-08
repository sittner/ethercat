# IgH EtherCAT Master - Userspace Migration Guide

## Executive Summary

This document describes the migration of the IgH EtherCAT Master to support both kernel-based and userspace operation using a **shared codebase** approach. The key innovation is a Platform Abstraction Layer (PAL) that allows 90%+ of the code to be shared between kernel and userspace builds.

### Key Benefits

| Aspect | Kernel-Based (Current) | Userspace (Target) |
|--------|------------------------|-------------------|
| **Kernel Modules** | 2+ (master + driver) | **0** |
| **Kernel Dependency** | Must rebuild per kernel | **None** |
| **Debugging** | printk, kgdb | **gdb, valgrind, strace** |
| **Deployment** | DKMS, module signing | **Copy binary** |
| **Secure Boot** | Module signing required | **No issues** |
| **Maintenance** | Track kernel API changes | **Stable userspace APIs** |
| **Portability** | Linux only | **POSIX possible** |
| **Shared Codebase** | N/A | **90%+ code shared with kernel** |
| **Build System** | Kbuild only | **Autotools (same as existing)** |

### Design Principles

1. **Shared codebase**: Kernel and userspace builds share 90%+ of `master/*.c` source files
2. **Existing build system**: Use autotools (configure/automake), NOT CMake - no new dependencies
3. **Optional feature**: `--enable-userspace` configure flag, kernel build unchanged by default
4. **Upstream-friendly**: Minimal changes to existing infrastructure, easy to merge

### Target Performance

| Metric | Target | Notes |
|--------|--------|-------|
| Minimum Cycle Time | 250µs | Conservative target |
| I/O Latency | < 30µs | Combined TX + RX |
| I/O Jitter | < 20µs | 99th percentile |
| CPU Overhead | < 5% | At 1kHz cycle |

---

## Architecture Overview

### Current Architecture (Kernel-Based)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           USERSPACE                                     │
│                                                                         │
│  ┌─────────────────┐          ┌─────────────────────────────────────┐   │
│  │  Your RT App    │─────────▶│  /dev/EtherCAT0 (ioctl interface)   │   │
│  └─────────────────┘          └─────────────────────────────────────┘   │
│                                              │                          │
│  ┌─────────────────┐                         │                          │
│  │ ethercat (CLI)  │─────────────────────────┤                          │
│  └─────────────────┘                         │                          │
│                                              │                          │
└──────────────────────────────────────────────┼──────────────────────────┘
                                               │ ioctl()
┌──────────────────────────────────────────────┼──────────────────────────┐
│                           KERNEL             │                          │
│                                              ▼                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                     ec_master.ko                                  │  │
│  │  - EtherCAT state machines                                        │  │
│  │  - CoE, FoE, SoE, EoE protocols                                   │  │
│  │  - DC synchronization                                             │  │
│  │  - Domain/Process data management                                 │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                    │                                    │
│                                    ▼                                    │
│  ┌────────────────┬────────────────┬────────────────┬────────────────┐  │
│  │ ec_e1000e.ko   │  ec_igb.ko     │ ec_generic.ko  │  ec_ccat.ko    │  │
│  │ (patched)      │  (patched)     │ (SOCK_RAW)     │  (native)      │  │
│  └───────┬────────┴───────┬────────┴───────┬────────┴───────┬────────┘  │
│          │                │                │                │           │
│          ▼                ▼                ▼                ▼           │
│        e1000e           igb           AF_PACKET          CCAT HW        │
└─────────────────────────────────────────────────────────────────────────┘
```

### Target Architecture (Userspace with Shared Codebase)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           USERSPACE                                     │
│                                                                         │
│  ┌─────────────────┐     ┌──────────────────────────────────────────┐   │
│  │  Your RT App    │────▶│            libethercat.so                │   │
│  │  (links lib)    │     │                                          │   │
│  └─────────────────┘     │  ┌────────────────────────────────────┐  │   │
│                          │  │   EtherCAT Master Core (shared)    │  │   │
│  ┌─────────────────┐     │  │   Same master/*.c as kernel!       │  │   │
│  │ ethercat_master │────▶│  │  - State machines (FSM)            │  │   │
│  │ (demonstrator)  │     │  │  - CoE, FoE, SoE protocols         │  │   │
│  └─────────────────┘     │  │  - DC synchronization              │  │   │
│          │               │  │  - Domain management               │  │   │
│    Unix socket           │  └────────────────────────────────────┘  │   │
│          │               │                    │                     │   │
│          ▼               │                    ▼                     │   │
│  ┌─────────────────┐     │  ┌────────────────────────────────────┐  │   │
│  │ ethercat (CLI)  │────▶│  │      Transport Abstraction         │  │   │
│  │ (unchanged)     │     │  │  (userspace-only component)        │  │   │
│  └─────────────────┘     │  │  ┌──────────┬──────────┬────────┐  │  │   │
│                          │  │  │ SOCK_RAW │  AF_XDP  │  CCAT  │  │  │   │
│                          │  │  │ (simple) │  (fast)  │ (HW)   │  │  │   │
│                          │  │  └─────┬────┴─────┬────┴────┬───┘  │  │   │
│                          │  └────────┼──────────┼─────────┼──────┘  │   │
│                          └───────────┼──────────┼─────────┼─────────┘   │
│                                      │          │         │             │
└──────────────────────────────────────┼──────────┼─────────┼─────────────┘
                                       │          │         │
┌──────────────────────────────────────┼──────────┼─────────┼─────────────┐
│              KERNEL (no custom modules!)        │         │             │
│                                      │          │         │             │
│   ┌────────────────┐                 │          │         │             │
│   │  AF_PACKET     │◀────────────────┘          │         │             │
│   │  raw socket    │                            │         │             │
│   └───────┬────────┘                            │         │             │
│           │                    ┌────────────────┘         │             │
│           │                    ▼                          │             │
│           │               ┌─────────────┐                 │             │
│           │               │ XDP BPF     │                 │             │
│           │               │ (loaded at  │                 │             │
│           │               │  runtime)   │                 │             │
│           │               └──────┬──────┘                 │             │
│           │                      │                        │             │
│           ▼                      ▼                 ┌──────┘             │
│    ┌─────────────────────────────────┐             │                    │
│    │  Standard NIC Drivers           │             ▼                    │
│    │  (igb, i40e, e1000e, etc.)      │      ┌─────────────┐             │
│    └──────────────┬──────────────────┘      │ UIO / VFIO  │             │
│                   │                         │ (for CCAT)  │             │
│                   ▼                         └──────┬──────┘             │
│                  NIC                               │                    │
│                                                    ▼                    │
│                                                 CCAT HW                 │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Shared Codebase Architecture

The key innovation is a **Platform Abstraction Layer (PAL)** that allows the same source files to compile for both kernel and userspace:

```
┌─────────────────────────────────────────────────────────────────┐
│                 Shared Core (master/*.c - 90%+)                 │
│  FSMs, Protocol, CoE/FoE/SoE, DC, PDO mapping, datagrams, etc.  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│              Platform Abstraction Layer (master/pal.h)          │
│  ec_pal_malloc(), ec_pal_free(), ec_pal_spinlock_*(),           │
│  ec_pal_time_now(), ec_pal_sleep_ns(), ec_pal_log()             │
└─────────────────────────────────────────────────────────────────┘
                              │
         ┌────────────────────┴────────────────────┐
         ▼                                         ▼
┌─────────────────────┐                 ┌─────────────────────┐
│ master/pal_kernel.c │                 │ master/pal_user.c   │
│  kmalloc, spinlock, │                 │ malloc, pthread,    │
│  jiffies, printk    │                 │ clock_gettime       │
└─────────────────────┘                 └─────────────────────┘
         │                                         │
         ▼                                         ▼
┌─────────────────────┐                 ┌─────────────────────┐
│ Kbuild (existing)   │                 │ Automake (new rules)│
│ make modules        │                 │ make                │
│  → ec_master.ko     │                 │  → libethercat.so   │
└─────────────────────┘                 └─────────────────────┘
```

### Benefits of Shared Codebase

- **Single source of truth**: Bug fixes and features apply to both builds
- **No new build dependencies**: Uses existing autotools infrastructure  
- **Lower maintenance burden**: No code divergence between kernel/userspace
- **Easier upstream acceptance**: Minimal diff, existing patterns
- **Gradual migration**: Can migrate file-by-file while keeping kernel working
- **Easy testing**: Unit tests validate shared code in userspace environment

---

## Directory Structure

```
ethercat/
├── configure.ac              # Modified: add --enable-userspace
├── Makefile.am               # Modified: add userspace subdirectory
├── master/
│   ├── Kbuild                # Unchanged: kernel build
│   ├── Makefile.am           # Modified: add userspace library rules
│   ├── pal.h                 # NEW: Platform Abstraction Layer interface
│   ├── pal_kernel.c          # NEW: Kernel PAL implementation (trivial)
│   ├── pal_user.c            # NEW: Userspace PAL implementation
│   ├── master.c              # Modified: add PAL calls
│   ├── slave.c               # Modified: add PAL calls
│   ├── domain.c              # Modified: add PAL calls
│   ├── datagram.c            # Modified: add PAL calls
│   ├── fsm_master.c          # Modified: add PAL calls
│   └── ...                   # Other existing files
├── userspace/                # NEW: Userspace-specific code
│   ├── Makefile.am
│   ├── transport/
│   │   ├── ec_transport.h    # Transport abstraction interface
│   │   ├── transport.c       # Transport registry and lifecycle
│   │   ├── transport_raw.c   # AF_PACKET (SOCK_RAW) implementation
│   │   └── transport_xdp.c   # AF_XDP implementation (optional)
│   ├── include/
│   │   └── ecrt_user.h       # Userspace-specific API extensions
│   └── examples/
│       └── basic_example.c
├── tests/                    # NEW: Test infrastructure
│   ├── Makefile.am
│   ├── ec_test.h             # Lightweight test framework
│   └── unit/
│       ├── test_pal.c        # PAL function tests
│       ├── test_transport.c  # Transport layer tests
│       └── test_datagram.c   # Datagram handling tests
└── include/
    └── ecrt.h                # Unchanged: public API
```

---

## Build System

### Configure Options

```bash
# Kernel modules only (default, unchanged behavior)
./configure
make modules

# Userspace library only  
./configure --enable-userspace --disable-kernel
make

# Both kernel and userspace
./configure --enable-userspace
make modules
make

# With XDP transport (requires libbpf)
./configure --enable-userspace --enable-xdp
make
```

### configure.ac Additions

```m4
dnl ============================================================
dnl Userspace build option
dnl ============================================================

AC_ARG_ENABLE([userspace],
    AS_HELP_STRING([--enable-userspace], [Build userspace library (libethercat.so)]),
    [enable_userspace=$enableval],
    [enable_userspace=no])

AC_ARG_ENABLE([kernel],
    AS_HELP_STRING([--disable-kernel], [Do not build kernel modules]),
    [enable_kernel=$enableval],
    [enable_kernel=yes])

AM_CONDITIONAL([BUILD_USERSPACE], [test "x$enable_userspace" = "xyes"])
AM_CONDITIONAL([BUILD_KERNEL], [test "x$enable_kernel" = "xyes"])

if test "x$enable_userspace" = "xyes"; then
    dnl Check for pthread
    AC_CHECK_LIB([pthread], [pthread_create], [],
        AC_MSG_ERROR([pthread required for userspace build]))
    
    dnl Check for libbpf (optional, for XDP transport)
    AC_ARG_ENABLE([xdp],
        AS_HELP_STRING([--enable-xdp], [Build AF_XDP transport (requires libbpf)]),
        [enable_xdp=$enableval],
        [enable_xdp=no])
    
    if test "x\$enable_xdp" = "xyes"; then
        PKG_CHECK_MODULES([LIBBPF], [libbpf >= 0.8], [have_libbpf=yes], 
            AC_MSG_ERROR([libbpf >= 0.8 required for XDP transport]))
    fi
    AM_CONDITIONAL([HAVE_LIBBPF], [test "x$enable_xdp" = "xyes"])
    
    dnl Add userspace subdirectory
    AC_CONFIG_FILES([userspace/Makefile])
    AC_CONFIG_FILES([tests/Makefile])
fi
```

### master/Makefile.am Additions

```makefile
# Existing kernel module handling via Kbuild stays unchanged
EXTRA_DIST = Kbuild \$(wildcard *.h)

if BUILD_USERSPACE
# Userspace convenience library (linked into final libethercat.so)
noinst_LTLIBRARIES = libecmaster.la

libecmaster_la_SOURCES = \
    master.c \
    slave.c \
    domain.c \
    datagram.c \
    datagram_pair.c \
    mailbox.c \
    coe_emerg_ring.c \
    sync.c \
    sync_config.c \
    fmmu_config.c \
    pdo.c \
    pdo_entry.c \
    pdo_list.c \
    sdo.c \
    sdo_entry.c \
    sdo_request.c \
    reg_request.c \
    voe_handler.c \
    fsm_master.c \
    fsm_slave.c \
    fsm_slave_config.c \
    fsm_slave_scan.c \
    fsm_coe.c \
    fsm_foe.c \
    fsm_soe.c \
    fsm_pdo.c \
    fsm_pdo_entry.c \
    fsm_sii.c \
    fsm_change.c \
    pal_user.c

libecmaster_la_CFLAGS = \
    -DECRT_USERSPACE \
    -I$(top_srcdir)/include \
    $(PTHREAD_CFLAGS)

libecmaster_la_LIBADD = $(PTHREAD_LIBS)
endif
```

### userspace/Makefile.am

```makefile
if BUILD_USERSPACE

lib_LTLIBRARIES = libethercat.la

libethercat_la_SOURCES = \
    transport/transport.c \
    transport/transport_raw.c

libethercat_la_CFLAGS = \
    -I$(top_srcdir)/include \
    -I$(srcdir)/transport \
    $(PTHREAD_CFLAGS)

libethercat_la_LIBADD = \
    $(top_builddir)/master/libecmaster.la \
    $(PTHREAD_LIBS)

libethercat_la_LDFLAGS = -version-info 1:0:0

# Optional XDP transport
if HAVE_LIBBPF
libethercat_la_SOURCES += transport/transport_xdp.c
libethercat_la_CFLAGS += $(LIBBPF_CFLAGS) -DHAVE_XDP
libethercat_la_LIBADD += $(LIBBPF_LIBS)
endif

# Install transport header
userspacetransportincludedir = $(includedir)/ethercat
userspacetransportinclude_HEADERS = transport/ec_transport.h

endif
```

### tests/Makefile.am

```makefile
if BUILD_USERSPACE

check_PROGRAMS = \
    test_pal \
    test_transport \
    test_datagram

TESTS = $(check_PROGRAMS)

AM_CFLAGS = \
    -I$(top_srcdir)/include \
    -I$(top_srcdir)/master \
    -I$(top_srcdir)/userspace/transport \
    -I$(srcdir)

LDADD = \
    $(top_builddir)/userspace/libethercat.la \
    $(PTHREAD_LIBS)

test_pal_SOURCES = unit/test_pal.c
test_transport_SOURCES = unit/test_transport.c
test_datagram_SOURCES = unit/test_datagram.c

endif
```

---

## Platform Abstraction Layer (PAL)

### master/pal.h

```c
/*****************************************************************************
 *
 *  Platform Abstraction Layer for EtherCAT Master
 *
 *  This header provides a unified interface for kernel and userspace builds.
 *  Include this header instead of directly using kernel APIs.
 *
 ****************************************************************************/

#ifndef EC_PAL_H
#define EC_PAL_H

#ifdef __KERNEL__
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/printk.h>
#else
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#endif

/****************************************************************************
 * Memory Allocation
 ****************************************************************************/

#ifdef __KERNEL__
#define ec_pal_malloc(size)         kmalloc(size, GFP_KERNEL)
#define ec_pal_zalloc(size)         kzalloc(size, GFP_KERNEL)
#define ec_pal_free(ptr)            kfree(ptr)
#define ec_pal_malloc_atomic(size)  kmalloc(size, GFP_ATOMIC)
#else
#define ec_pal_malloc(size)         malloc(size)
#define ec_pal_zalloc(size)         calloc(1, size)
#define ec_pal_free(ptr)            free(ptr)
#define ec_pal_malloc_atomic(size)  malloc(size)  /* No distinction in userspace */
#endif

/****************************************************************************
 * Spinlocks
 ****************************************************************************/

#ifdef __KERNEL__
typedef spinlock_t ec_pal_spinlock_t;
#define ec_pal_spin_init(lock)                  spin_lock_init(lock)
#define ec_pal_spin_destroy(lock)               do { } while(0)
#define ec_pal_spin_lock(lock)                  spin_lock(lock)
#define ec_pal_spin_unlock(lock)                spin_unlock(lock)
#define ec_pal_spin_lock_irqsave(lock, flags)   spin_lock_irqsave(lock, flags)
#define ec_pal_spin_unlock_irqrestore(lock, flags) spin_unlock_irqrestore(lock, flags)
#else
typedef pthread_spinlock_t ec_pal_spinlock_t;
#define ec_pal_spin_init(lock)                  pthread_spin_init(lock, PTHREAD_PROCESS_PRIVATE)
#define ec_pal_spin_destroy(lock)               pthread_spin_destroy(lock)
#define ec_pal_spin_lock(lock)                  pthread_spin_lock(lock)
#define ec_pal_spin_unlock(lock)                pthread_spin_unlock(lock)
#define ec_pal_spin_lock_irqsave(lock, flags)   do { (void)(flags); pthread_spin_lock(lock); } while(0)
#define ec_pal_spin_unlock_irqrestore(lock, flags) do { (void)(flags); pthread_spin_unlock(lock); } while(0)
#endif

/****************************************************************************
 * Mutexes
 ****************************************************************************/

#ifdef __KERNEL__
typedef struct mutex ec_pal_mutex_t;
#define ec_pal_mutex_init(m)        mutex_init(m)
#define ec_pal_mutex_destroy(m)     mutex_destroy(m)
#define ec_pal_mutex_lock(m)        mutex_lock(m)
#define ec_pal_mutex_unlock(m)      mutex_unlock(m)
#define ec_pal_mutex_trylock(m)     mutex_trylock(m)
#else
typedef pthread_mutex_t ec_pal_mutex_t;
#define ec_pal_mutex_init(m)        pthread_mutex_init(m, NULL)
#define ec_pal_mutex_destroy(m)     pthread_mutex_destroy(m)
#define ec_pal_mutex_lock(m)        pthread_mutex_lock(m)
#define ec_pal_mutex_unlock(m)      pthread_mutex_unlock(m)
#define ec_pal_mutex_trylock(m)     (pthread_mutex_trylock(m) == 0)
#endif

/****************************************************************************
 * Semaphores
 ****************************************************************************/

#ifdef __KERNEL__
#include <linux/semaphore.h>
typedef struct semaphore ec_pal_sem_t;
#define ec_pal_sem_init(s, val)     sema_init(s, val)
#define ec_pal_sem_destroy(s)       do { } while(0)
#define ec_pal_sem_down(s)          down(s)
#define ec_pal_sem_up(s)            up(s)
#define ec_pal_sem_down_interruptible(s) down_interruptible(s)
#else
#include <semaphore.h>
typedef sem_t ec_pal_sem_t;
#define ec_pal_sem_init(s, val)     sem_init(s, 0, val)
#define ec_pal_sem_destroy(s)       sem_destroy(s)
#define ec_pal_sem_down(s)          sem_wait(s)
#define ec_pal_sem_up(s)            sem_post(s)
#define ec_pal_sem_down_interruptible(s) sem_wait(s)
#endif

/****************************************************************************
 * Wait Queues / Condition Variables
 ****************************************************************************/

#ifdef __KERNEL__
#include <linux/wait.h>
typedef wait_queue_head_t ec_pal_waitqueue_t;
#define ec_pal_waitqueue_init(wq)   init_waitqueue_head(wq)
#define ec_pal_waitqueue_destroy(wq) do { } while(0)
#define ec_pal_wait_event(wq, cond) wait_event(wq, cond)
#define ec_pal_wait_event_interruptible(wq, cond) wait_event_interruptible(wq, cond)
#define ec_pal_wake_up(wq)          wake_up(wq)
#define ec_pal_wake_up_all(wq)      wake_up_all(wq)
#else
/* Userspace uses condition variables with a mutex */
typedef struct {
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} ec_pal_waitqueue_t;

static inline void ec_pal_waitqueue_init(ec_pal_waitqueue_t *wq) {
    pthread_cond_init(&wq->cond, NULL);
    pthread_mutex_init(&wq->mutex, NULL);
}
static inline void ec_pal_waitqueue_destroy(ec_pal_waitqueue_t *wq) {
    pthread_cond_destroy(&wq->cond);
    pthread_mutex_destroy(&wq->mutex);
}
#define ec_pal_wake_up(wq)          pthread_cond_signal(&(wq)->cond)
#define ec_pal_wake_up_all(wq)      pthread_cond_broadcast(&(wq)->cond)
/* Note: ec_pal_wait_event needs custom implementation per use case */
#endif

/****************************************************************************
 * Time
 ****************************************************************************/

#ifdef __KERNEL__
typedef unsigned long ec_pal_time_t;  /* jiffies */
#define ec_pal_time_now()               jiffies
#define ec_pal_ms_to_time(ms)           msecs_to_jiffies(ms)
#define ec_pal_us_to_time(us)           usecs_to_jiffies(us)
#define ec_pal_time_to_ms(t)            jiffies_to_msecs(t)
#define ec_pal_time_before(a, b)        time_before(a, b)
#define ec_pal_time_after(a, b)         time_after(a, b)
#define ec_pal_msleep(ms)               msleep(ms)
#define ec_pal_usleep(us)               usleep_range(us, us + 100)
#else
typedef uint64_t ec_pal_time_t;  /* nanoseconds */

static inline ec_pal_time_t ec_pal_time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define ec_pal_ms_to_time(ms)           ((ec_pal_time_t)(ms) * 1000000ULL)
#define ec_pal_us_to_time(us)           ((ec_pal_time_t)(us) * 1000ULL)
#define ec_pal_time_to_ms(t)            ((t) / 1000000ULL)
#define ec_pal_time_before(a, b)        ((int64_t)((a) - (b)) < 0)
#define ec_pal_time_after(a, b)         ((int64_t)((a) - (b)) > 0)
#define ec_pal_msleep(ms)               usleep((ms) * 1000)
#define ec_pal_usleep(us)               usleep(us)
#endif

/****************************************************************************
 * Logging
 ****************************************************************************/

#ifdef __KERNEL__
#define EC_PAL_INFO(fmt, ...)    printk(KERN_INFO "EtherCAT: " fmt, ##__VA_ARGS__)
#define EC_PAL_ERR(fmt, ...)     printk(KERN_ERR "EtherCAT ERROR: " fmt, ##__VA_ARGS__)
#define EC_PAL_WARN(fmt, ...)    printk(KERN_WARNING "EtherCAT WARNING: " fmt, ##__VA_ARGS__)
#define EC_PAL_DBG(fmt, ...)     printk(KERN_DEBUG "EtherCAT: " fmt, ##__VA_ARGS__)
#else
#define EC_PAL_INFO(fmt, ...)    fprintf(stderr, "EtherCAT: " fmt "\n", ##__VA_ARGS__)
#define EC_PAL_ERR(fmt, ...)     fprintf(stderr, "EtherCAT ERROR: " fmt "\n", ##__VA_ARGS__)
#define EC_PAL_WARN(fmt, ...)    fprintf(stderr, "EtherCAT WARNING: " fmt "\n", ##__VA_ARGS__)
#define EC_PAL_DBG(fmt, ...)     fprintf(stderr, "EtherCAT DEBUG: " fmt "\n", ##__VA_ARGS__)
#endif

/****************************************************************************
 * Atomics
 ****************************************************************************/

#ifdef __KERNEL__
#include <linux/atomic.h>
typedef atomic_t ec_pal_atomic_t;
#define ec_pal_atomic_read(v)       atomic_read(v)
#define ec_pal_atomic_set(v, i)     atomic_set(v, i)
#define ec_pal_atomic_inc(v)        atomic_inc(v)
#define ec_pal_atomic_dec(v)        atomic_dec(v)
#define ec_pal_atomic_add(i, v)     atomic_add(i, v)
#define ec_pal_atomic_sub(i, v)     atomic_sub(i, v)
#else
#include <stdatomic.h>
typedef atomic_int ec_pal_atomic_t;
#define ec_pal_atomic_read(v)       atomic_load(v)
#define ec_pal_atomic_set(v, i)     atomic_store(v, i)
#define ec_pal_atomic_inc(v)        atomic_fetch_add(v, 1)
#define ec_pal_atomic_dec(v)        atomic_fetch_sub(v, 1)
#define ec_pal_atomic_add(i, v)     atomic_fetch_add(v, i)
#define ec_pal_atomic_sub(i, v)     atomic_fetch_sub(v, i)
#endif

/****************************************************************************
 * List (keep existing list.h - it's already portable)
 ****************************************************************************/

/* The existing list.h macros (INIT_LIST_HEAD, list_add, list_del, etc.)
 * are pure C macros and work in both kernel and userspace unchanged. */

#endif /* EC_PAL_H */
```

### Migration Pattern for Existing Code

For each `master/*.c` file, the migration follows this pattern:

**Before (kernel-only):**
```c
#include <linux/slab.h>

void *ptr = kmalloc(size, GFP_KERNEL);
kfree(ptr);
spin_lock(&lock);
```

**After (shared):**
```c
#include "pal.h"

void *ptr = ec_pal_malloc(size);
ec_pal_free(ptr);
ec_pal_spin_lock(&lock);
```

---

## Transport Abstraction Layer

The transport layer is **userspace-only** (kernel uses existing `ec_device` infrastructure).

### userspace/transport/ec_transport.h

```c
/**
 * @file ec_transport.h
 * @brief Transport abstraction for userspace EtherCAT
 *
 * This interface abstracts the underlying network I/O mechanism.
 * Implementations include:
 * - SOCK_RAW (AF_PACKET) - Simple, works everywhere
 * - AF_XDP - High performance, requires libbpf
 * - CCAT - Beckhoff hardware acceleration
 */

#ifndef EC_TRANSPORT_H
#define EC_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

#define ETH_ALEN 6
#define ETH_P_ETHERCAT 0x88A4

typedef struct ec_transport ec_transport_t;

typedef struct ec_transport_ops {
    const char *name;
    int (*init)(ec_transport_t *transport);
    void (*cleanup)(ec_transport_t *transport);
    int (*open)(ec_transport_t *transport, const char *interface);
    void (*close)(ec_transport_t *transport);
    int (*send)(ec_transport_t *transport, const void *data, size_t len);
    int (*receive)(ec_transport_t *transport, void *buf, size_t maxlen, int timeout_us);
    int (*get_link)(ec_transport_t *transport);
    int (*get_mac)(ec_transport_t *transport, uint8_t mac[ETH_ALEN]);
    int (*get_mtu)(ec_transport_t *transport);
} ec_transport_ops_t;

struct ec_transport {
    const ec_transport_ops_t *ops;
    void *priv;
    char interface[16];
    uint8_t mac[ETH_ALEN];
    int mtu;
    struct {
        uint64_t tx_packets;
        uint64_t tx_bytes;
        uint64_t tx_errors;
        uint64_t rx_packets;
        uint64_t rx_bytes;
        uint64_t rx_errors;
    } stats;
};

/* Transport registry */
extern const ec_transport_ops_t ec_transport_raw;
#ifdef HAVE_XDP
extern const ec_transport_ops_t ec_transport_xdp;
#endif

/* Helper functions */
ec_transport_t *ec_transport_create(const ec_transport_ops_t *ops);
void ec_transport_destroy(ec_transport_t *transport);
const ec_transport_ops_t *ec_transport_get_by_name(const char *name);
int ec_transport_list(const char **names, int max);

#endif /* EC_TRANSPORT_H */
```

---

## Implementation Phases

### Phase 1: PAL Foundation (2 weeks)

| Task | Description | Duration | Status |
|------|-------------|----------|--------|
| 1.1 | Create `master/pal.h` interface | 2 days | ⏳ Pending |
| 1.2 | Create `master/pal_kernel.c` (trivial - just includes) | 1 day | ⏳ Pending |
| 1.3 | Create `master/pal_user.c` | 2 days | ⏳ Pending |
| 1.4 | Update `configure.ac` with `--enable-userspace` | 1 day | ⏳ Pending |
| 1.5 | Update `master/Makefile.am` with userspace rules | 1 day | ⏳ Pending |
| 1.6 | Create `userspace/` directory and `Makefile.am` | 1 day | ⏳ Pending |
| 1.7 | Verify kernel build unchanged (`make modules`) | 1 day | ⏳ Pending |
| 1.8 | Create basic test infrastructure (`tests/`) | 2 days | ⏳ Pending |

**Milestone:** `./configure --enable-userspace && make` builds (empty library skeleton)

### Phase 2: Transport Layer (2 weeks)

| Task | Description | Duration | Status |
|------|-------------|----------|--------|
| 2.1 | Create `userspace/transport/ec_transport.h` | 1 day | ⏳ Pending |
| 2.2 | Implement `transport.c` (registry, lifecycle) | 1 day | ⏳ Pending |
| 2.3 | Implement `transport_raw.c` (AF_PACKET) | 3 days | ⏳ Pending |
| 2.4 | Optional: `transport_xdp.c` skeleton | 2 days | ⏳ Pending |
| 2.5 | Transport unit tests | 2 days | ⏳ Pending |
| 2.6 | Integration test (send/receive EtherCAT frames) | 2 days | ⏳ Pending |

**Milestone:** Can send/receive raw EtherCAT frames in userspace

### Phase 3: Core Migration (4 weeks)

Incrementally refactor `master/*.c` to use PAL macros:

| Task | Description | Duration | Status |
|------|-------------|----------|--------|
| 3.1 | `datagram.c` - add PAL calls | 2 days | ⏳ Pending |
| 3.2 | `domain.c` - add PAL calls | 2 days | ⏳ Pending |
| 3.3 | `slave.c` - add PAL calls | 3 days | ⏳ Pending |
| 3.4 | `master.c` - add PAL calls | 4 days | ⏳ Pending |
| 3.5 | FSM files (`fsm_*.c`) - add PAL calls | 5 days | ⏳ Pending |
| 3.6 | Remaining files | 4 days | ⏳ Pending |

**For each file:**
1. Add `#include "pal.h"`
2. Replace kernel APIs with `ec_pal_*` macros
3. Verify `make modules` still works (kernel build)
4. Verify `make` (userspace) compiles
5. Add/run unit tests

**Milestone:** `libethercat.so` contains full master core

### Phase 4: Control Interface (2 weeks)

| Task | Description | Duration | Status |
|------|-------------|----------|--------|
| 4.1 | Design control socket protocol | 1 day | ⏳ Pending |
| 4.2 | Implement socket server | 2 days | ⏳ Pending |
| 4.3 | Modify `ethercat` CLI for socket support | 2 days | ⏳ Pending |
| 4.4 | Create `ethercat_master` demonstrator | 1 day | ⏳ Pending |
| 4.5 | Test CLI commands via socket | 2 days | ⏳ Pending |

**Milestone:** `ethercat` CLI works with userspace master

### Phase 5: Advanced Features (3 weeks)

| Task | Description | Duration | Status |
|------|-------------|----------|--------|
| 5.1 | Port Distributed Clocks | 3 days | ⏳ Pending |
| 5.2 | Implement DC with `SO_TIMESTAMPING` | 2 days | ⏳ Pending |
| 5.3 | Port EoE using TUN/TAP | 2 days | ⏳ Pending |
| 5.4 | Port FoE, SoE, VoE | 3 days | ⏳ Pending |
| 5.5 | Integration testing | 3 days | ⏳ Pending |

**Milestone:** Feature parity with kernel version

### Phase 6: XDP Transport & Polish (2 weeks)

| Task | Description | Duration | Status |
|------|-------------|----------|--------|
| 6.1 | Implement AF_XDP transport | 4 days | ⏳ Pending |
| 6.2 | Performance testing & optimization | 3 days | ⏳ Pending |
| 6.3 | Documentation | 2 days | ⏳ Pending |
| 6.4 | Final testing & release prep | 2 days | ⏳ Pending |

**Milestone:** Production-ready release

---

## Testing Strategy

### Unit Tests (run with `make check`)

```
tests/
├── unit/
│   ├── test_pal.c          # PAL function tests
│   ├── test_transport.c    # Transport layer tests
│   ├── test_datagram.c     # Datagram handling tests
│   ├── test_list.c         # List operations (shared code)
│   └── test_fsm.c          # State machine logic
```

### Integration Tests

1. **Bus scan test** - Scan bus, verify slave detection
2. **SDO read test** - Read known SDOs from test slave
3. **PDO exchange test** - Exchange process data at 1kHz
4. **DC sync test** - Verify DC synchronization accuracy
5. **CLI test** - All `ethercat` commands work via socket

### Dual-Build Verification

For every change:
```bash
# 1. Verify kernel build still works
./configure
make modules
# Check for compile errors/warnings

# 2. Verify userspace build works
./configure --enable-userspace
make
make check
```

---

## Risk Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Breaking kernel build | Medium | High | Test both builds after every change |
| PAL overhead affects performance | Low | Medium | PAL is mostly macros, near-zero overhead |
| Timing differences in userspace | Medium | Medium | Extensive timing tests, configurable timeouts |
| Upstream rejection | Low | High | Minimal diff, use existing patterns, optional feature |

---

## Advantages of Shared Codebase Approach

1. **Single source of truth** - One codebase for kernel and userspace
2. **No new build dependencies** - Uses existing autotools infrastructure
3. **Upstream-friendly** - Minimal diff, optional feature flag, familiar patterns
4. **Lower maintenance** - Bug fixes apply to both builds automatically
5. **Gradual migration** - Can migrate file-by-file while kernel always works
6. **Easy testing** - Unit tests validate shared code in userspace environment
7. **Community-friendly** - Contributors don't need to learn new tools
8. **No code duplication** - Unlike a pure userspace rewrite

---

## Conclusion

The shared codebase approach with PAL enables userspace EtherCAT operation while:

1. **Preserving the existing kernel build** unchanged by default
2. **Sharing 90%+ of code** between kernel and userspace
3. **Using the existing autotools build system** - no CMake
4. **Providing an easy migration path** - one file at a time
5. **Maximizing upstream acceptance** - minimal, incremental changes

Estimated effort: **14-16 weeks** for one experienced developer.
