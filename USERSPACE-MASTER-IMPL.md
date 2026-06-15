# Userspace Master Library Implementation

## Overview

A userspace master library (`libethercat.so`) that embeds the full EtherCAT
master core into a shared library, allowing user applications to run the master
in-process. This replaces the kernel-based master + ioctl client library
architecture with a single userspace library.

## Architecture

### Kernel Mode (original)

```
Application → lib/libethercat.so (ioctl client) → /dev/EtherCATN → kernel module (master core)
```

### Userspace Master Mode (this implementation)

```
Application → libethercat.so (master core + PAL + transport)
```

- `libethercat.so` contains the full master core (`master/*.c`), PAL shims,
  and transport layer
- `ec_master_t` is the real master struct from `master/master.h` — opaque to
  application via forward declaration in `ecrt.h`
- Master runs in the application's process

## API

### New Functions (guarded by `#ifdef EC_USPACE_MASTER` in `ecrt.h`)

```c
EC_PUBLIC_API int ecrt_lib_init(ec_log_cb_t log_cb);
EC_PUBLIC_API ec_master_t *ecrt_startup_master(
        unsigned int index,
        ec_transport_t *transport,
        ec_transport_t *backup_transport,
        unsigned int debug_level,
        int run_on_cpu);
EC_PUBLIC_API void ecrt_release_master(ec_master_t *master);
EC_PUBLIC_API void ecrt_lib_cleanup(void);
```

### Unchanged API

All operational functions (`ecrt_master_*`, `ecrt_domain_*`,
`ecrt_slave_config_*`, SDO/SoE/VoE/register requests) are identical in both
modes.

### Application Code

```c
int main() {
    ecrt_lib_init(NULL, EC_IPC_DEFAULT_SOCKET_PATH);
    ec_transport_t *t = ec_transport_create(EC_TRANSPORT_RAW, "eth0");
    ec_master_t *master = ecrt_startup_master(0, t, NULL, 1, -1);

    /* From here: identical to kernel mode */
    ec_domain_t *domain = ecrt_master_create_domain(master);
    /* ... configure slaves, activate, cyclic loop ... */

    ecrt_release_master(master);
    ec_transport_destroy(t);
    ecrt_lib_cleanup();
}
```

## File Layout

### PAL (Platform Abstraction Layer)

The master core (`master/*.c`) is shared between kernel and userspace builds.
Platform-specific code is isolated into PAL headers/sources:

| File | Purpose |
|------|---------|
| `master/uspace/pal.h` | Master PAL header (includes all sub-PAL headers) |
| `master/uspace/pal.c` | Logging, misc PAL functions |
| `master/uspace/pal_alloc.h` | `ec_alloc()` → `calloc()` |
| `master/uspace/pal_list.h` | Linux-style linked list (userspace reimplementation) |
| `master/uspace/pal_mtx.h` | Mutex abstraction → `pthread_mutex_t` |
| `master/uspace/pal_sem.h` | Semaphore abstraction → `sem_t` |
| `master/uspace/pal_queue.h` | Completion queue abstraction |
| `master/uspace/pal_misc.h` | Misc utilities (jiffies, time, printk) |
| `master/uspace/pal_thread.c/h` | Thread abstraction → pthreads |
| `master/uspace/pal_work.c/h` | Work queue abstraction |
| `master/uspace/pal_irq_work.c/h` | IRQ work abstraction |
| `master/uspace/pal_eoe.c/h` | EoE PAL: TAP device, skb emulation, TX polling |
| `master/uspace/pal_affinity.h` | NIC IRQ CPU affinity pinning |

### Core Userspace Files

| File | Purpose |
|------|---------|
| `master/uspace/module.c` | Library lifecycle: `ecrt_lib_init()`, `ecrt_startup_master()`, `ecrt_release_master()`, `ecrt_lib_cleanup()` |
| `master/uspace/device_uspace.c` | Device functions: `ec_device_init/clear/tx_data/send/poll/open/close()` |
| `master/uspace/main.c` | `ec_master` standalone binary (CLI, daemonization) |
| `master/uspace/cdev.c/h` | Character device emulation (IPC socket for tool API) |
| `master/uspace/tool_api.c` | Tool API implementation (responds to `ethercat` CLI commands) |

### Transport Layer

| File | Purpose |
|------|---------|
| `include/ectp.h` | Transport abstraction interface |
| `transport/transport.c` | Transport registry and common helpers |
| `transport/transport_raw.c` | Raw socket transport (AF_PACKET) |
| `transport/transport_xdp.c` | XDP/AF_XDP transport (zero-copy) |
| `transport/transport_macb.c` | MACB register-level transport |
| `transport/irq_pin.c` | NIC IRQ affinity pinning implementation |

### Public Headers

| File | Purpose |
|------|---------|
| `include/ecrt.h.in` | Public API header template (configured by autotools) |
| `include/ecrt.h` | Generated public API header |
| `include/ecrt_tool.h` | Tool API header (for CLI/external tools) |
| `include/ectp.h` | Transport plugin interface |

### Kernel PAL (for comparison)

| File | Purpose |
|------|---------|
| `master/kernel/pal.h` | Kernel PAL (thin wrappers around Linux kernel APIs) |
| `master/kernel/pal_eoe.h` | Kernel EoE (net_device, sk_buff) |
| `master/kernel/pal_eoe.c` | Kernel EoE net_device_ops implementation |
| `master/kernel/pal_alloc.h` | `ec_alloc()` → `kzalloc()` |

## Key Design Decisions

### Transport Ownership

```
Caller:   ec_transport_create()
Library:    └─ ecrt_startup_master()  → ec_transport_open()
Library:    └─ ecrt_release_master()  → ec_transport_close()
Caller:   ec_transport_destroy()
```

### Type Opacity

`ecrt.h` forward-declares `ec_master_t` as opaque. Applications only see
pointers. The full struct is in `master/master.h`, never included by app code.

### EoE (Ethernet over EtherCAT)

Kernel uses push model (`ndo_start_xmit`). Userspace uses pull model:
- TAP device created per EoE slave
- `ec_eoe_poll_tx()` polls TAP fd, enqueues frames into `eoe->tx_queue`
- `ec_netif_rx()` writes received frames to TAP fd
- TAP is marked `opened=1` immediately on creation (no ifconfig callback)
- Called from EoE thread before `ec_eoe_run()` each iteration
- Kernel PAL has a no-op inline for `ec_eoe_poll_tx()`

### IRQ Affinity Pinning

`transport/irq_pin.c` detects the NIC's IRQ and pins it to the same CPU as
the RT thread, reducing cross-CPU cache traffic. Records RT CPU from
`ecrt_master_receive()`, applies affinity from the operation thread.

### `EC_USPACE_MASTER` Define

`include/ecrt.h` is generated from `include/ecrt.h.in` by configure.
`@EC_USPACE_MASTER_DEFINE@` inserts `#define EC_USPACE_MASTER 1` when
`--enable-uspace-master` is active. Applications don't need `-D` flags.

## Build System

### Configure

```
--enable-uspace-master    Build userspace master library (default: no)
--enable-eoe              EoE support (default: yes)
```

`--enable-uspace-master` implies `--enable-kernel=no` and `--enable-userlib=no`.

### Build Targets

| Target | Install |
|--------|---------|
| `libethercat.so` | `libdir` |
| `ec_master` | `bindir` |

### Multi-Master CLI

```
ec_master [-d <level>] [-f] [-l]
          -i <iface> [-t <type>] [-b <iface>] [-c <cpu>]
         [-i <iface> ...]
```

Each `-i` starts a new master block with auto-incremented index.

## Status

All implementation tasks complete. The userspace master builds, runs the EoE
and IRQ affinity subsystems, and provides full IPC for the `ethercat` CLI tool.
