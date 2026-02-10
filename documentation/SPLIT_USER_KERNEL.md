# IgH EtherCAT Master - Kernel/Userspace Code Separation

## Overview

This document describes the architectural approach for separating kernel-specific and userspace-specific code while maximizing shared code between both build targets.

## Design Goals

1. **Maximize shared code** - FSM, protocol handling, datagram management shared between kernel and userspace
2. **No `#ifdef __KERNEL__` in shared code** - Clean separation via include paths
3. **Same names everywhere** - `ec_device_t`, `ec_master_t`, `ec_device_send()` work in both contexts
4. **Minimal changes to existing code** - Restructure directories, not rewrite code
5. **Build system handles switching** - Include path selects kernel or userspace implementation

## Design Decisions

The following decisions were made during the design discussion:

### 1. Structure Design: Platform Abstraction Pattern

Both `ec_master_t` and `ec_device_t` use a **shared structure with platform-specific embedded struct**:

- Shared fields remain directly in the main struct
- Platform-specific fields are grouped in a `.plat` member of type `ec_master_plat_t` / `ec_device_plat_t`
- Access macros provide platform-independent operations (e.g., `ec_master_lock()`)

**Rationale:** Single shared definition, clear separation of platform-specific code, no `#ifdef` in struct definitions.

### 2. Linked Lists: Port kernel list.h

Use the kernel's `list.h` implementation for userspace - it's pure C macros with no kernel dependencies.

**Rationale:** Already tested, well-understood, zero changes to shared code using lists.

### 3. Logging: Same macros, different expansion

Same macro names (`EC_MASTER_INFO`, `EC_MASTER_ERR`, etc.) defined in each platform's `pal.h`:
- Kernel: expands to `printk()`
- Userspace: expands to `fprintf(stderr, ...)

**Rationale:** Zero changes needed in shared code.

### 4. Time Representation: Abstract via PAL

Use `ec_time_t` (milliseconds) with platform-specific implementations:
- `ec_time_now()` - get current time
- `ec_time_after(a, b)` / `ec_time_before(a, b)` - time comparison
- `ec_msleep()` / `ec_usleep()` - sleep functions

**Rationale:** Unified interface, milliseconds sufficient for EtherCAT timeouts.

### 5. Error Codes: Keep existing convention

Use kernel-style negative errno values (`-EINVAL`, `-ENOMEM`, etc.) everywhere. POSIX `<errno.h>` defines the same constants.

**Rationale:** Already works in both contexts, no changes needed.

---

## Directory Structure

### Before (Current)

```
git/ethercat/
├── master/                    # Mixed kernel code + some shared
│   ├── pal.h                  # PAL switch header
│   ├── pal_kernel.c           # Kernel PAL implementation
│   ├── device.c               # Kernel device (sk_buff)
│   ├── device.h               # Kernel device types
│   ├── master.c               # Mostly shareable
│   ├── master.h               # Kernel master types
│   ├── cdev.c                 # Kernel only
│   ├── module.c               # Kernel only
│   └── ...
├── userspace/                 # Userspace code
│   ├── pal_user.c
│   ├── ec_master.c
│   └── transport/
└── ...
```

### After (Target)

```
git/ethercat/
├── master/                    # SHARED code only
│   ├── master.c               # Shared - uses ec_device_send() etc.
│   ├── master.h               # Shared - includes pal.h, uses ec_master_plat_t
│   ├── slave.c                # Shared
│   ├── slave.h                # Shared
│   ├── datagram.c             # Shared
│   ├── datagram.h             # Shared
│   ├── domain.c               # Shared
│   ├── fsm_master.c           # Shared
│   ├── fsm_slave.c            # Shared
│   ├── fsm_coe.c              # Shared
│   ├── mailbox.c              # Shared
│   ├── pdo.c, sdo.c, ...      # Shared
│   │
│   ├── kernel/                # KERNEL-SPECIFIC
│   │   ├── pal.h              # Kernel types, ec_master_plat_t, ec_device_plat_t
│   │   ├── device.c           # ec_device_* using sk_buff
│   │   ├── cdev.c             # Character device
│   │   ├── cdev.h
│   │   ├── module.c           # Module init/exit
│   │   ├── debug.c            # Debug network interface
│   │   ├── debug.h
│   │   ├── rtdm.c             # RTDM support (optional)
│   │   └── rtdm.h
│   │
│   └── uspace/                # USERSPACE-SPECIFIC
│       ├── pal.h              # Userspace types, ec_master_plat_t, ec_device_plat_t
│       ├── device.c           # ec_device_* using transport
│       ├── ecrt_user.c        # Userspace API extensions
│       ├── ecrt_user.h
│       ├── ec_master.c          # Standalone daemon
│       └── transport/         # Transport layer
│           ├── ec_transport.h
│           ├── transport.c
│           ├── transport_raw.c
│           └── transport_xdp.c (future)
└── ...
```

## The Include Path Trick

Both `master/kernel/pal.h` and `master/uspace/pal.h` exist with **identical filenames**.

### Kernel Build (Kbuild)

```makefile
# master/Kbuild.in
ccflags-y += -I$(src)/kernel
```

When shared code does `#include "pal.h"`, the compiler finds `master/kernel/pal.h`.

### Userspace Build (Automake)

```makefile
# master/uspace/Makefile.am
AM_CFLAGS = -I$(srcdir)
```

Or from top-level:
```makefile
AM_CFLAGS = -I$(top_srcdir)/master/uspace
```

When shared code does `#include "pal.h"`, the compiler finds `master/uspace/pal.h`.

**Result:** Same source file compiles differently based on include path. No `#ifdef` needed!

---

## Platform Abstraction Types

### ec_device_t with ec_device_plat_t

The device structure has shared fields and a platform-specific `.plat` member:

```c
/* master/device.h (shared) */
#ifndef EC_DEVICE_H
#define EC_DEVICE_H

#include "pal.h"  /* Gets ec_device_plat_t from kernel or uspace */

struct ec_device {
    struct ec_master *master;
    uint8_t open;
    uint8_t link_state;
    
    /* Statistics (shared) */
    uint64_t tx_count;
    uint64_t rx_count;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_errors;
    
    ec_device_plat_t plat;  /* Platform-specific */
};

typedef struct ec_device ec_device_t;

/* Function declarations - same signatures on both platforms */
int ec_device_init(ec_device_t *device, struct ec_master *master);
void ec_device_clear(ec_device_t *device);
int ec_device_open(ec_device_t *device);
int ec_device_close(ec_device_t *device);
uint8_t *ec_device_tx_data(ec_device_t *device);
void ec_device_send(ec_device_t *device, size_t size);
void ec_device_poll(ec_device_t *device);
uint8_t ec_device_get_link(ec_device_t *device);
void ec_device_clear_stats(ec_device_t *device);

#endif /* EC_DEVICE_H */
```

### Kernel ec_device_plat_t

```c
/* master/kernel/pal.h (excerpt) */

#define EC_TX_RING_SIZE 2

typedef struct {
    struct net_device *dev;
    ec_pollfunc_t poll;
    struct module *module;
    struct sk_buff *tx_skb[EC_TX_RING_SIZE];
    unsigned int tx_ring_index;
    unsigned long jiffies_poll;
#ifdef EC_DEBUG_IF
    ec_debug_t dbg;
#endif
#ifdef EC_DEBUG_RING
    ec_debug_frame_t debug_frames[EC_DEBUG_RING_SIZE];
    unsigned int debug_frame_index;
    unsigned int debug_frame_count;
#endif
} ec_device_plat_t;
```

### Userspace ec_device_plat_t

```c
/* master/uspace/pal.h (excerpt) */

typedef struct {
    ec_transport_t *transport;
    uint64_t jiffies_poll;
} ec_device_plat_t;
```

---

### ec_master_t with ec_master_plat_t

```c
/* master/master.h (shared) */
#ifndef EC_MASTER_H
#define EC_MASTER_H

#include "pal.h"  /* Gets ec_master_plat_t */

struct ec_master {
    unsigned int index;
    unsigned int reserved;
    
ec_device_t devices[EC_MAX_NUM_DEVICES];
    ec_master_phase_t phase;
    unsigned int active;
    unsigned int config_changed;
    
ec_fsm_master_t fsm;
    ec_datagram_t fsm_datagram;
    
    struct list_head datagram_queue;
    uint8_t datagram_index;
    struct list_head ext_datagram_queue;
    
ec_slave_t *slaves;
    unsigned int slave_count;
    struct list_head configs;
    struct list_head domains;
    
    unsigned int debug_level;
    ec_stats_t stats;
    
    /* ... other shared fields ... */
    
ec_master_plat_t plat;  /* Platform-specific */
};

typedef struct ec_master ec_master_t;

#endif /* EC_MASTER_H */
```

### Kernel ec_master_plat_t

```c
/* master/kernel/pal.h (excerpt) */

typedef struct {
    ec_cdev_t cdev;
    struct device *class_device;
    struct semaphore master_sem;
    struct semaphore device_sem;
    struct semaphore scan_sem;
    struct semaphore config_sem;
    struct semaphore ext_queue_sem;
    struct task_struct *thread;
    wait_queue_head_t scan_queue;
    wait_queue_head_t config_queue;
    wait_queue_head_t request_queue;
    struct work_struct sc_reset_work;
    struct irq_work sc_reset_work_kicker;
    struct rt_mutex io_mutex;
#ifdef EC_RTDM
    ec_rtdm_dev_t rtdm_dev;
#endif
#ifdef EC_EOE
    struct task_struct *eoe_thread;
#endif
} ec_master_plat_t;
```

### Userspace ec_master_plat_t

```c
/* master/uspace/pal.h (excerpt) */
typedef struct {
    pthread_mutex_t master_mutex;
    pthread_mutex_t device_mutex;
    pthread_mutex_t scan_mutex;
    pthread_mutex_t config_mutex;
    pthread_mutex_t ext_queue_mutex;
    pthread_t thread;
    pthread_cond_t scan_cond;
    pthread_cond_t config_cond;
    pthread_cond_t request_cond;
    pthread_mutex_t io_mutex;
} ec_master_plat_t;
```

---

## Access Macros

Platform-independent macros for common operations:

### Kernel pal.h

```c
/* master/kernel/pal.h (excerpt) */

/* Master locking */
#define ec_master_lock(m)       down(&(m)->plat.master_sem)
#define ec_master_unlock(m)     up(&(m)->plat.master_sem)
#define ec_device_lock(m)       down(&(m)->plat.device_sem)
#define ec_device_unlock(m)     up(&(m)->plat.device_sem)
#define ec_ext_queue_lock(m)    down(&(m)->plat.ext_queue_sem)
#define ec_ext_queue_unlock(m)  up(&(m)->plat.ext_queue_sem)

/* Logging */
#define EC_MASTER_INFO(master, fmt, args...) \
    printk(KERN_INFO "EtherCAT %u: " fmt, (master)->index, ##args)

#define EC_MASTER_ERR(master, fmt, args...) \
    printk(KERN_ERR "EtherCAT ERROR %u: " fmt, (master)->index, ##args)

#define EC_MASTER_WARN(master, fmt, args...) \
    printk(KERN_WARNING "EtherCAT WARNING %u: " fmt, (master)->index, ##args)

#define EC_MASTER_DBG(master, level, fmt, args...) \
    do { \
        if ((master)->debug_level >= level) { \
            printk(KERN_DEBUG "EtherCAT DEBUG %u: " fmt, \
                    (master)->index, ##args); \
        } \
    } while (0)

/* Time */
typedef uint64_t ec_time_t;
#define ec_time_now()           jiffies_to_msecs(jiffies)
#define ec_time_after(a, b)     ((int64_t)((a) - (b)) > 0)
#define ec_time_before(a, b)    ((int64_t)((a) - (b)) < 0)
#define ec_msleep(ms)           msleep(ms)
#define ec_usleep(us)           usleep_range(us, (us) + 100)

/* Memory */
#define ec_malloc(size)         kmalloc(size, GFP_KERNEL)
#define ec_zalloc(size)         kzalloc(size, GFP_KERNEL)
#define ec_free(ptr)            kfree(ptr)
```

### Userspace pal.h

```c
/* master/uspace/pal.h (excerpt) */

/* Master locking */
#define ec_master_lock(m)       pthread_mutex_lock(&(m)->plat.master_mutex)
#define ec_master_unlock(m)     pthread_mutex_unlock(&(m)->plat.master_mutex)
#define ec_device_lock(m)       pthread_mutex_lock(&(m)->plat.device_mutex)
#define ec_device_unlock(m)     pthread_mutex_unlock(&(m)->plat.device_mutex)
#define ec_ext_queue_lock(m)    pthread_mutex_lock(&(m)->plat.ext_queue_mutex)
#define ec_ext_queue_unlock(m)  pthread_mutex_unlock(&(m)->plat.ext_queue_mutex)

/* Logging */
#define EC_MASTER_INFO(master, fmt, args...) \
    fprintf(stderr, "EtherCAT %u: " fmt, (master)->index, ##args)

#define EC_MASTER_ERR(master, fmt, args...) \
    fprintf(stderr, "EtherCAT ERROR %u: " fmt, (master)->index, ##args)

#define EC_MASTER_WARN(master, fmt, args...) \
    fprintf(stderr, "EtherCAT WARNING %u: " fmt, (master)->index, ##args)

#define EC_MASTER_DBG(master, level, fmt, args...) \
    do { \
        if ((master)->debug_level >= level) { \
            fprintf(stderr, "EtherCAT DEBUG %u: " fmt, \
                    (master)->index, ##args); \
        } \
    } while (0)

/* Time */
typedef uint64_t ec_time_t;

static inline ec_time_t ec_time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ec_time_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#define ec_time_after(a, b)     ((int64_t)((a) - (b)) > 0)
#define ec_time_before(a, b)    ((int64_t)((a) - (b)) < 0)
#define ec_msleep(ms)           usleep((ms) * 1000)
#define ec_usleep(us)           usleep(us)

/* Memory */
#define ec_malloc(size)         malloc(size)
#define ec_zalloc(size)         calloc(1, size)
#define ec_free(ptr)            free(ptr)
```

---

## Shared Code Pattern

Shared code in `master/` includes `pal.h` and uses abstracted types and macros:

```c
/* master/master.c */
#include "pal.h"      /* Gets kernel/pal.h or uspace/pal.h */
#include "master.h"

void ec_master_send_datagrams(ec_master_t *master, ec_device_index_t device_index)
{
    ec_device_t *device = &master->devices[device_index];
    uint8_t *frame_data;
    
    /* Platform-independent locking */
    ec_device_lock(master);
    
    /* These calls work in both kernel and userspace! */
    frame_data = ec_device_tx_data(device);
    
    /* ... build frame ... */
    
    ec_device_send(device, frame_size);
    
    ec_device_unlock(master);
}

void ec_master_idle_thread(ec_master_t *master)
{
    ec_time_t start = ec_time_now();
    
    /* Platform-independent logging */
    EC_MASTER_DBG(master, 1, "Starting idle thread\n");
    
    while (!master->should_stop) {
        ec_master_lock(master);
        /* ... do work ... */
        ec_master_unlock(master);
        
        ec_msleep(10);
    }
}
```

---

## Implementation Strategy

### Phase 1: Create Directory Structure

1. Create `master/kernel/` directory
2. Create `master/uspace/` directory
3. Move `userspace/transport/` to `master/uspace/transport/`
4. Move `userspace/ec_master.c` to `master/uspace/`
5. Move `userspace/ecrt_user.*` to `master/uspace/`

### Phase 2: Kernel PAL

1. Create `master/kernel/pal.h` with:
   - Kernel includes
   - `ec_device_plat_t` definition
   - `ec_master_plat_t` definition
   - Access macros (locking, logging, time, memory)
2. Move `master/device.c` to `master/kernel/device.c`
3. Move other kernel-only files:
   - `cdev.c`, `cdev.h`
   - `module.c`
   - `debug.c`, `debug.h`
   - `rtdm.c`, `rtdm.h`
4. Update `master/Kbuild.in`:
   - Add `-I$(src)/kernel` to include path
   - Update object file paths

### Phase 3: Userspace PAL

1. Create `master/uspace/pal.h` with:
   - Userspace includes
   - `ec_device_plat_t` definition (with `ec_transport_t *`)
   - `ec_master_plat_t` definition (with pthread types)
   - Access macros (locking, logging, time, memory)
2. Create `master/uspace/device.c` implementing `ec_device_*` using transport
3. Update/create `master/uspace/Makefile.am`

### Phase 4: Shared Header Refactoring

1. Create shared `master/device.h`:
   - Include `pal.h` for `ec_device_plat_t`
   - Define `ec_device_t` with shared fields + `.plat`
   - Declare device functions
2. Update `master/master.h`:
   - Include `pal.h` for `ec_master_plat_t`
   - Move platform fields to `ec_master_plat_t`
   - Keep shared fields in `ec_master_t`
3. Delete old `master/pal.h` (no longer needed - include path handles switching)
4. Verify kernel build still works
5. Verify userspace build works

### Phase 5: Incremental Shared Code Migration

For each file to be shared:

1. Add `#include "pal.h"` at top
2. Replace kernel-specific calls with PAL equivalents
3. Replace direct platform field access with access macros where applicable
4. Test kernel build
5. Add to userspace build
6. Test userspace build

Priority order:
1. `datagram.c` (already partially done)
2. `domain.c`
3. `slave.c`
4. `master.c` (complex, depends on many others)
5. FSM files

---

## Build System Changes

### Kbuild.in (Kernel)

```makefile
# master/Kbuild.in
ccflags-y := -I$(src)/kernel

ec_master-objs := \
    master.o \
    slave.o \
    datagram.o \
    domain.o \
    fsm_master.o \
    fsm_slave.o \
    # ... shared objects
    kernel/device.o \
    kernel/cdev.o \
    kernel/module.o \
    # ... kernel-only objects
```

### Makefile.am (Userspace)

```makefile
# master/uspace/Makefile.am

AM_CFLAGS = \
    -I$(srcdir) \
    -I$(top_srcdir)/master \
    -I$(top_srcdir)/include

lib_LTLIBRARIES = libethercat_master.la

# Shared sources (from parent directory)
SHARED_SOURCES = \
    $(top_srcdir)/master/datagram.c \
    $(top_srcdir)/master/domain.c \
    $(top_srcdir)/master/slave.c \
    $(top_srcdir)/master/master.c \
    $(top_srcdir)/master/fsm_master.c
    # ... etc

# Userspace-specific sources
USPACE_SOURCES = \
    device.c \
    ecrt_user.c \
    transport/transport.c \
    transport/transport_raw.c

libethercat_master_la_SOURCES = \
    $(SHARED_SOURCES) \
    $(USPACE_SOURCES)

# Daemon
bin_PROGRAMS = ec_master
ec_master_SOURCES = ethercat_master.c
ec_master_LDADD = libethercat_master.la
```

---

## Verification Checklist

### After Each Phase

- [ ] Kernel module compiles without warnings
- [ ] Kernel module loads successfully
- [ ] Existing kernel functionality works
- [ ] Userspace library compiles
- [ ] `ec_master` daemon runs
- [ ] No `#ifdef __KERNEL__` in shared code (goal)

### Final Verification

- [ ] Run kernel master with real slaves
- [ ] Run userspace master, verify frame TX/RX
- [ ] Both can be built from same source tree
- [ ] Clean `make distclean && ./configure && make` works

---

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Break kernel build | Test kernel build after every change |
| Circular includes | Careful header organization, forward declarations |
| Type size mismatches | Use fixed-size types (`uint32_t`) in shared structures |
| Missing PAL abstractions | Add to PAL as discovered, keep list updated |
| Build system complexity | Document clearly, test both builds in CI |

---

## References

- Current PAL implementation: `master/pal.h`
- Transport layer: `userspace/transport/`
- Migration guide: `documentation/USERSPACE_MIGRATION.md`