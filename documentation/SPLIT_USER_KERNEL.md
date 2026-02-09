# IgH EtherCAT Master - Kernel/Userspace Code Separation

## Overview

This document describes the architectural approach for separating kernel-specific and userspace-specific code while maximizing shared code between both build targets.

## Design Goals

1. **Maximize shared code** - FSM, protocol handling, datagram management shared between kernel and userspace
2. **No `#ifdef __KERNEL__` in shared code** - Clean separation via include paths
3. **Same names everywhere** - `ec_device_t`, `ec_master_t`, `ec_device_send()` work in both contexts
4. **Minimal changes to existing code** - Restructure directories, not rewrite code
5. **Build system handles switching** - Include path selects kernel or userspace implementation

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
│   ├── ethercat_master.c
│   └── transport/
└── ...
```

### After (Target)

```
git/ethercat/
├── master/                    # SHARED code only
│   ├── master.c               # Shared - uses ec_device_send() etc.
│   ├── master.h               # Shared - includes pal.h for base types
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
│   │   ├── pal.h              # Kernel types + includes
│   │   ├── device.c           # ec_device_* using sk_buff
│   │   ├── device.h           # Kernel ec_device_t definition
│   │   ├── cdev.c             # Character device
│   │   ├── cdev.h
│   │   ├── module.c           # Module init/exit
│   │   ├── debug.c            # Debug network interface
│   │   ├── debug.h
│   │   ├── rtdm.c             # RTDM support (optional)
│   │   └── rtdm.h
│   │
│   └── uspace/                # USERSPACE-SPECIFIC
│       ├── pal.h              # Userspace types + includes
│       ├── device.c           # ec_device_* using transport
│       ├── device.h           # Userspace ec_device_t definition
│       ├── ecrt_user.c        # Userspace API extensions
│       ├── ecrt_user.h
│       ├── ethercat_master.c  # Standalone daemon
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
AM_CFLAGS = -I$(srcdir)/../kernel  # NO! Wrong one
AM_CFLAGS = -I$(srcdir)            # Finds uspace/pal.h
```

Or from top-level:
```makefile
AM_CFLAGS = -I$(top_srcdir)/master/uspace
```

When shared code does `#include "pal.h"`, the compiler finds `master/uspace/pal.h`.

**Result:** Same source file compiles differently based on include path. No `#ifdef` needed!

## Type Definitions

### Kernel pal.h

```c
/* master/kernel/pal.h */
#ifndef EC_PAL_H
#define EC_PAL_H

/* Kernel includes */
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/jiffies.h>

/* Kernel type mappings */
typedef spinlock_t ec_spinlock_t;
typedef struct semaphore ec_sem_t;
typedef struct mutex ec_mutex_t;

/* Time */
#define ec_jiffies()          jiffies
#define ec_ms_to_jiffies(ms)  msecs_to_jiffies(ms)

/* Memory */
#define ec_malloc(size)       kmalloc(size, GFP_KERNEL)
#define ec_zalloc(size)       kzalloc(size, GFP_KERNEL)
#define ec_free(ptr)          kfree(ptr)

/* Include kernel-specific headers */
#include "device.h"   /* Kernel ec_device_t */

#endif /* EC_PAL_H */
```

### Userspace pal.h

```c
/* master/uspace/pal.h */
#ifndef EC_PAL_H
#define EC_PAL_H

/* Userspace includes */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

/* Userspace type mappings */
typedef pthread_spinlock_t ec_spinlock_t;
typedef sem_t ec_sem_t;
typedef pthread_mutex_t ec_mutex_t;

/* Time - use nanoseconds internally */
static inline uint64_t ec_jiffies(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;  /* milliseconds */
}
#define ec_ms_to_jiffies(ms)  (ms)

/* Memory */
#define ec_malloc(size)       malloc(size)
#define ec_zalloc(size)       calloc(1, size)
#define ec_free(ptr)          free(ptr)

/* Include userspace-specific headers */
#include "device.h"   /* Userspace ec_device_t */

#endif /* EC_PAL_H */
```

## Device Abstraction

### Kernel device.h

```c
/* master/kernel/device.h */
#ifndef EC_DEVICE_H
#define EC_DEVICE_H

#include <linux/netdevice.h>
#include <linux/skbuff.h>

#define EC_TX_RING_SIZE 2

struct ec_device {
    struct ec_master *master;
    struct net_device *dev;
    ec_pollfunc_t poll;
    struct module *module;
    uint8_t open;
    uint8_t link_state;
    struct sk_buff *tx_skb[EC_TX_RING_SIZE];
    unsigned int tx_ring_index;
    unsigned long jiffies_poll;
    
    /* Statistics */
    uint64_t tx_count;
    uint64_t rx_count;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_errors;
    /* ... rates, debug ring, etc. */
};

typedef struct ec_device ec_device_t;

/* Function declarations - same signatures as userspace */
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

### Userspace device.h

```c
/* master/uspace/device.h */
#ifndef EC_DEVICE_H
#define EC_DEVICE_H

#include <stdint.h>
#include <stddef.h>
#include "transport/ec_transport.h"

struct ec_device {
    struct ec_master *master;
    ec_transport_t *transport;
    uint8_t open;
    uint8_t link_state;
    uint64_t jiffies_poll;
    
    /* Statistics */
    uint64_t tx_count;
    uint64_t rx_count;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_errors;
};

typedef struct ec_device ec_device_t;

/* Function declarations - same signatures as kernel */
int ec_device_init(ec_device_t *device, struct ec_master *master);
void ec_device_clear(ec_device_t *device);
int ec_device_open(ec_device_t *device);
int ec_device_close(ec_device_t *device);
uint8_t *ec_device_tx_data(ec_device_t *device);
void ec_device_send(ec_device_t *device, size_t size);
void ec_device_poll(ec_device_t *device);
uint8_t ec_device_get_link(ec_device_t *device);
void ec_device_clear_stats(ec_device_t *device);

/* Userspace-specific: set transport type and interface */
int ec_device_setup(ec_device_t *device, int transport_type, const char *interface);

#endif /* EC_DEVICE_H */
```

## Shared Code Pattern

Shared code in `master/` includes `pal.h` and uses abstracted types:

```c
/* master/master.c */
#include "pal.h"      /* Gets kernel/pal.h or uspace/pal.h */
#include "master.h"
#include "datagram.h"

void ec_master_send_datagrams(ec_master_t *master, ec_device_index_t device_index)
{
    ec_device_t *device = &master->devices[device_index];
    uint8_t *frame_data;
    
    /* These calls work in both kernel and userspace! */
    frame_data = ec_device_tx_data(device);
    
    /* ... build frame ... */
    
ec_device_send(device, frame_size);
}

void ec_master_receive_datagrams(ec_master_t *master, ec_device_t *device,
        const uint8_t *frame_data, size_t size)
{
    /* Same code works in both contexts */
    /* ... process received frame ... */
}
```

## Implementation Strategy

### Phase 1: Create Directory Structure

1. Create `master/kernel/` directory
2. Create `master/uspace/` directory
3. Move `userspace/transport/` to `master/uspace/transport/`

### Phase 2: Kernel PAL

1. Create `master/kernel/pal.h` with kernel type definitions
2. Create `master/kernel/device.h` (extract from current `master/device.h`)
3. Move `master/device.c` to `master/kernel/device.c`
4. Move other kernel-only files:
   - `cdev.c`, `cdev.h`
   - `module.c`
   - `debug.c`, `debug.h`
   - `rtdm.c`, `rtdm.h`
5. Update `master/Kbuild.in`:
   - Add `-I$(src)/kernel` to include path
   - Update object file paths

### Phase 3: Userspace PAL

1. Create `master/uspace/pal.h` with userspace type definitions
2. Create `master/uspace/device.h` with userspace `ec_device_t`
3. Create `master/uspace/device.c` implementing `ec_device_*` using transport
4. Move userspace files:
   - `userspace/ethercat_master.c` → `master/uspace/ethercat_master.c`
   - `userspace/pal_user.c` → `master/uspace/pal.c` (or merge into device.c)
   - `userspace/ecrt_user.*` → `master/uspace/`
5. Update/create `master/uspace/Makefile.am`

### Phase 4: Shared Header Cleanup

1. Update `master/master.h`:
   - Add `#include "pal.h"` at top
   - Remove direct kernel includes
   - Use PAL types (`ec_spinlock_t`, etc.)
2. Same for other shared headers as needed
3. Verify kernel build still works
4. Verify userspace build works

### Phase 5: Incremental Shared Code Migration

For each file to be shared:

1. Add `#include "pal.h"` at top
2. Replace kernel-specific calls with PAL equivalents
3. Test kernel build
4. Add to userspace build
5. Test userspace build

Priority order:
1. `datagram.c` (already partially done)
2. `domain.c`
3. `slave.c`
4. `master.c` (complex, depends on many others)
5. FSM files

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
    pal.c \
    ecrt_user.c \
    transport/transport.c \
    transport/transport_raw.c

libethercat_master_la_SOURCES = \
    $(SHARED_SOURCES) \
    $(USPACE_SOURCES)

# Daemon
bin_PROGRAMS = ethercat_master
erthercat_master_SOURCES = ethercat_master.c
ethercat_master_LDADD = libethercat_master.la
```

## Verification Checklist

### After Each Phase

- [ ] Kernel module compiles without warnings
- [ ] Kernel module loads successfully
- [ ] Existing kernel functionality works
- [ ] Userspace library compiles
- [ ] `ethercat_master` daemon runs
- [ ] No `#ifdef __KERNEL__` in shared code (goal)

### Final Verification

- [ ] Run kernel master with real slaves
- [ ] Run userspace master, verify frame TX/RX
- [ ] Both can be built from same source tree
- [ ] Clean `make distclean && ./configure && make` works

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Break kernel build | Test kernel build after every change |
| Circular includes | Careful header organization, forward declarations |
| Type size mismatches | Use fixed-size types (`uint32_t`) in shared structures |
| Missing PAL abstractions | Add to PAL as discovered, keep list updated |
| Build system complexity | Document clearly, test both builds in CI |

## Open Items

- [ ] Decide on `ec_master_t` shared vs separate definition
- [ ] Handle `list_head` - kernel uses `struct list_head`, userspace needs equivalent
- [ ] Handle logging macros (`EC_MASTER_INFO`, etc.)
- [ ] Handle time representation (jiffies vs nanoseconds)
- [ ] Error code conventions (`-EINVAL` vs `-1`)  
## References

- Current PAL implementation: `master/pal.h`
- Transport layer: `userspace/transport/`
- Migration guide: `documentation/USERSPACE_MIGRATION.md`