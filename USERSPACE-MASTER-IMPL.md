# Userspace Master Library Implementation

## Overview

Implement a userspace master library (`libethercat.so`) that embeds the full
EtherCAT master core into a shared library, allowing user applications to
run the master in-process. This replaces the kernel-based master + ioctl
client library architecture with a single userspace library.

## Architecture

### Current Architecture (Kernel Mode)

```
Application → lib/libethercat.so (ioctl client) → /dev/EtherCATN → kernel module (master core)
```

- `lib/` implements `ecrt.h` API as ioctl proxy
- `ec_master_t` in `lib/master.h` is `{ int fd; ... }` — a file descriptor wrapper
- Master core runs in kernel space

### New Architecture (Userspace Master Mode)

```
Application → libethercat.so (master core + PAL + transport)
```

- `libethercat.so` contains the full master core (`master/*.c`), PAL shims, and transport layer
- `ec_master_t` is the real master struct from `master/master.h` — opaque to application via forward declaration in `ecrt.h`
- Master runs in the application's process

## API Design

### New Functions (guarded by `#ifdef EC_USPACE_MASTER` in `ecrt.h`)

```c
/** Initialize the userspace master library.
 *  Must be called once before any other ecrt_* function.
 *
 *  Internally calls:
 *    ec_master_init_static()
 *    ec_pal_work_init()
 *    ec_pal_irq_work_init()
 *
 *  \return 0 on success, < 0 on error.
 */
EC_PUBLIC_API int ecrt_lib_init(void);

/** Start a userspace master with built-in transport.
 *  Creates transport(s), opens interface(s), initializes master,
 *  and enters idle phase (slave scanning starts immediately).
 *
 *  Internally calls:
 *    malloc(sizeof(ec_master_t))
 *    ec_transport_create(transport_type)          — for main interface
 *    ec_transport_open(transport, interface)
 *    ec_transport_get_mac(transport, main_mac)    — MAC is copied
 *    if backup_interface != NULL:
 *      ec_transport_create(transport_type)        — for backup interface (same type)
 *      ec_transport_open(backup_transport, backup_interface)
 *      ec_transport_get_mac(backup_transport, backup_mac)
 *    ec_master_init(master, index, ..., debug_level, run_on_cpu)
 *    assign transports to device PALs
 *    ec_device_open(&master->devices[EC_DEVICE_MAIN])
 *    if backup_interface != NULL:
 *      ec_device_open(&master->devices[EC_DEVICE_BACKUP])
 *    ec_master_enter_idle_phase(master)
 *
 *  Both transports are library-owned and will be destroyed by
 *  ecrt_release_master().
 *
 *  \param index        Master index (0-based). Stored in ec_master_t by
 *                      ec_master_init().
 *  \param transport_type  Transport type for both main and backup interfaces.
 *  \param interface    Main network interface name.
 *  \param backup_interface  Backup network interface name, or NULL for none.
 *  \param debug_level  Debug verbosity level. Stored in ec_master_t by
 *                      ec_master_init().
 *  \param run_on_cpu   CPU affinity for master threads, or 0xffffffff for
 *                      no binding. Stored in ec_master_t by ec_master_init().
 *  \return Pointer to master, or NULL on error.
 */
EC_PUBLIC_API ec_master_t *ecrt_startup_master(
        unsigned int index,
        ec_transport_type_t transport_type,
        const char *interface,
        const char *backup_interface,   /* NULL = no backup */
        unsigned int debug_level,
        unsigned int run_on_cpu         /* 0xffffffff = no binding */
        );

/** Start a userspace master with caller-provided transport.
 *  Same as ecrt_startup_master() but the main transport is supplied by the
 *  caller. The backup interface (if non-NULL) triggers an internally created
 *  transport of the same type as the caller's transport for the backup device.
 *
 *  Main transport is caller-owned — ecrt_release_master() will NOT destroy it.
 *  Backup transport (if created) is library-owned and will be destroyed.
 *
 *  \param index        Master index (0-based).
 *  \param transport    Caller-provided main transport (already opened).
 *  \param interface    Main network interface name.
 *  \param backup_interface  Backup network interface name, or NULL for none.
 *  \param debug_level  Debug verbosity level.
 *  \param run_on_cpu   CPU affinity for master threads, or 0xffffffff for
 *                      no binding.
 *  \return Pointer to master, or NULL on error.
 */
EC_PUBLIC_API ec_master_t *ecrt_startup_master_custom(
        unsigned int index,
        ec_transport_t *transport,
        const char *interface,
        const char *backup_interface,   /* NULL = no backup */
        unsigned int debug_level,
        unsigned int run_on_cpu         /* 0xffffffff = no binding */
        );

/** Cleanup the userspace master library.
 *  Must be called after all masters have been released.
 *
 *  Internally calls:
 *    ec_pal_irq_work_cleanup()
 *    ec_pal_work_cleanup()
 *
 */
EC_PUBLIC_API void ecrt_lib_cleanup(void);
```

### Existing Functions — Behavior Changes

```c
/** Release master — signature unchanged (void).
 *
 *  In uspace-master mode, additionally:
 *    ec_master_leave_idle_phase(master)
 *    ec_device_close(&master->devices[EC_DEVICE_MAIN])
 *    if backup device was opened:
 *      ec_device_close(&master->devices[EC_DEVICE_BACKUP])
 *    ec_master_clear(master)
 *    ec_transport_close(transport)                — always (main)
 *    ec_transport_destroy(transport)              — only if library-owned
 *    if backup_transport != NULL:
 *      ec_transport_close(backup_transport)       — always
 *      ec_transport_destroy(backup_transport)     — always (library-owned)
 *    free(master)
 */
EC_PUBLIC_API void ecrt_release_master(ec_master_t *master);
```

### Unchanged API (identical in both modes)

All operational functions remain identical:

- `ecrt_master_create_domain()`
- `ecrt_master_activate()` / `ecrt_master_deactivate()`
- `ecrt_master_send()` / `ecrt_master_receive()`
- `ecrt_master_application_time()`
- `ecrt_master_sync_reference_clock()` / `ecrt_master_sync_slave_clocks()`
- `ecrt_domain_*()` functions
- `ecrt_slave_config_*()` functions
- SDO, SoE, VoE, register request functions
- `ecrt_master_state()`, `ecrt_master_get_slave()`
- All other `ecrt_*` functions

### Application Code Comparison

**Kernel mode:**
```c
int main() {
    ec_master_t *master = ecrt_request_master(0);
    ec_domain_t *domain = ecrt_master_create_domain(master);
    /* ... configure slaves, activate, cyclic loop ... */
    ecrt_release_master(master);
}
```

**Userspace master mode:**
```c
int main() {
    ecrt_lib_init();
    ec_master_t *master = ecrt_startup_master(
            0,               /* index */
            EC_TRANSPORT_RAW,
            "eth0",          /* main interface */
            NULL,            /* no backup */
            1,               /* debug_level */
            0xffffffff       /* no CPU binding */
            );

    /* FROM HERE: identical to kernel mode */
    ec_domain_t *domain = ecrt_master_create_domain(master);
    /* ... configure slaves, activate, cyclic loop ... */
    ecrt_release_master(master);

    ecrt_lib_cleanup();
}
```

## Type Opacity

`ecrt.h` already forward-declares `ec_master_t` as opaque:

```c
struct ec_master;
typedef struct ec_master ec_master_t;
```

Applications only see pointers. The full struct definition in `master/master.h`
is never included by application code. **No type renames needed.**

## Implementation Details

### Transport Ownership

`ecrt_startup_master()` and `ecrt_startup_master_custom()` differ in transport
ownership. The flag `transport_owned` in `ec_master_pal_t` tracks this:

- `ecrt_startup_master()` → library-owned (`transport_owned = 1`) → `ecrt_release_master()` destroys transport
- `ecrt_startup_master_custom()` → caller-owned (`transport_owned = 0`) → `ecrt_release_master()` does NOT destroy transport

The backup transport (stored in `ec_master_pal_t.backup_transport`) is always
library-owned when present — `ecrt_release_master()` always destroys it.

### Backup Device Support

When `backup_interface != NULL`, `ecrt_startup_master()` creates a second
transport instance of the **same type** as the main transport. Both transports
use the same `transport_type` to ensure consistency and redundancy quality.

The `ec_master_pal_t` struct tracks both transports:

```c
typedef struct {
    struct ec_transport *transport;        /**< Main transport instance. */
    struct ec_transport *backup_transport; /**< Backup transport instance (NULL if none). */
    int transport_owned;
    uint8_t main_mac[ETH_ALEN];
    uint8_t backup_mac[ETH_ALEN];
    char *interface_name;
    char *backup_interface_name;          /**< Backup interface name (NULL if none). */
} ec_master_pal_t;
```

Note: `index`, `debug_level`, and `run_on_cpu` are passed directly to
`ec_master_init()` and do not need to be stored in `ec_master_pal_t` — they
are stored in `ec_master_t` itself by `ec_master_init()`.

The backup transport is assigned to `master->devices[EC_DEVICE_BACKUP].pal.transport`.

### String and MAC Lifetime

MAC addresses and interface name strings are owned by `ec_master_pal_t`
(the PAL fields embedded in `ec_master_t`):

- `pal.main_mac[ETH_ALEN]` — MAC address copied from transport at startup
- `pal.backup_mac[ETH_ALEN]` — backup MAC address copied from backup transport (zeroed if no backup)
- `pal.interface_name` — interface name `strdup`'d in `ecrt_startup_master()` / `ecrt_startup_master_custom()`, freed in `ecrt_release_master()`
- `pal.backup_interface_name` — backup interface name `strdup`'d when `backup_interface != NULL`, freed in `ecrt_release_master()`

This fixes a latent bug in the current `main.c` where stack-local `main_mac`
happens to outlive the master only because it's in `main()`'s stack frame.

### Master Allocation

The master is heap-allocated via `malloc(sizeof(ec_master_t))` since the
application cannot know `sizeof(ec_master_t)` — the type is opaque.
`ec_master_pal_t pal` is an embedded field of `ec_master_t` (declared in
`master/master.h`), so the PAL struct is allocated as part of the master.
This is consistent with how the existing ioctl-based `lib/common.c` allocates
masters.

### `ecrt_startup_master_common()` Helper

The internal `ecrt_startup_master_common()` function completes master
initialization after the transport(s) and interface names are set up by the
caller (`ecrt_startup_master()` or `ecrt_startup_master_custom()`). It uses
the caller-provided `index`, `debug_level`, and `run_on_cpu` values when
calling `ec_master_init()` — these are no longer hardcoded to `0`, `1`, `0`.

## `ec_master` Standalone Tool

### Multi-Master CLI Design

The `ec_master` tool supports multiple simultaneous masters via repeated `-i`
flags. Each `-i` starts a new master block and auto-increments the master index.

```
ec_master [-d <level>] [-f] [-l]
          -i <iface> [-t <type>] [-b <iface>] [-c <cpu>]
         [-i <iface> [-t <type>] [-b <iface>] [-c <cpu>]]
         ...
          [-h]
```

**Parameters:**

| Flag | Long option | Description |
|------|-------------|-------------|
| `-i <name>` | `--interface <name>` | Network interface — required; starts new master block, increments index |
| `-t <type>` | `--transport <type>` | Transport type for current block (default: raw) |
| `-b <name>` | `--backup <name>` | Backup interface for current block |
| `-c <id>` | `--cpu <id>` | Bind master threads to CPU for current block |
| `-d <level>` | `--debug <level>` | Debug level (global, applies to all masters) |
| `-f` | `--foreground` | Do not daemonize |
| `-l` | `--log-stdout` | Log to stdout instead of syslog (requires `--foreground`) |
| `-h` | `--help` | Show help |

**Parsing strategy:** single-pass stateful parser using `getopt_long`. Each
`-i` finalizes the previous master block and starts a new one. Block-local
options (`-t`, `-b`, `-c`) apply to the most recent `-i`. Global options
(`-d`, `-f`, `-l`) may appear anywhere.

**Example:**

```
ec_master -d 1 -i eth0 -t raw -b eth1 -c 2 -i eth2 -t xdp
```

→ master 0: main=eth0, transport=raw, backup=eth1, cpu=2, debug=1  
→ master 1: main=eth2, transport=xdp, no backup, no cpu binding, debug=1

### Daemonization

`ec_master` forks to background by default using the standard double-fork +
setsid pattern:

1. First fork: parent exits, child calls `setsid()` to become session leader
2. Second fork: session leader exits, grandchild can never acquire a terminal
3. Standard I/O redirected to `/dev/null`

`--foreground` skips all forking and keeps the process in the foreground.

A PID file is written to `/var/run/ec_master.pid` when daemonizing, and
removed on clean shutdown. This is required for init system integration and
double-start prevention.

### Syslog Support

| Mode | Logging destination |
|------|---------------------|
| Daemon (default) | `openlog("ec_master", LOG_PID, LOG_DAEMON)` then `vsyslog()` |
| `--foreground` (without `--log-stdout`) | syslog (same as daemon mode) |
| `--foreground --log-stdout` | stdout/stderr (`vfprintf(stderr, ...)`) |

`ec_log()` in `pal.c` uses a global flag to switch between `vsyslog()` and
`vfprintf(stderr, ...)`. The flag is set at startup before the main loop.

`--log-stdout` requires `--foreground`: when daemonized, stdin/stdout/stderr
are redirected to `/dev/null`, so logging to stdout would silently discard
all messages.

## Build System

### Configure Option

```
--enable-uspace-master    Build userspace master library (default: no)
```

When enabled, implies:
- `--enable-kernel=no` (no kernel modules)
- `--enable-userlib=no` (no ioctl-based client library — would conflict)

### Build Targets

| Target | Source | Install |
|--------|--------|---------|
| `libethercat.so` | master core (`master/*.c`) + PAL (`master/uspace/pal*.c`) + transport (`master/uspace/transport/*.c`) + `master/uspace/module.c` + `master/uspace/device_uspace.c` | `libdir` |
| `ec_master` | `master/uspace/main.c` linked against `libethercat.so` | `bindir` |

### Autotools Integration

- New `master/uspace/Makefile.am` with libtool shared library rules
- Conditional `ENABLE_USPACE_MASTER` in `master/Makefile.am`
- `AC_CONFIG_FILES` entry for `master/uspace/Makefile`
- XDP detection (`AC_CHECK_LIB` for libxdp, libbpf) when enabled

## File Changes

### New Files (implemented)

| File | Purpose |
|------|---------|
| `master/uspace/module.c` | Library lifecycle: `ecrt_lib_init()`, `ecrt_startup_master()`, `ecrt_startup_master_custom()`, `ecrt_release_master()`, `ecrt_lib_cleanup()` |
| `master/uspace/device_uspace.c` | Device functions extracted from `main.c`: `ec_device_init()`, `ec_device_clear()`, `ec_device_tx_data()`, `ec_device_send()`, `ec_device_poll()`, `ec_device_open()`, `ec_device_close()` |
| `master/uspace/transport/ec_transport.h` | Transport abstraction interface |
| `master/uspace/transport/transport.c` | Transport registry and common helpers |
| `master/uspace/transport/transport_raw.c` | Raw socket transport implementation |
| `master/uspace/transport/transport_xdp.c` | XDP/AF_XDP transport implementation |
| `master/uspace/Makefile.am` | Autotools build rules for library + binary |

### Modified Files (implemented)

| File | Change |
|------|--------|
| `include/ecrt.h` | New API functions and transport type/struct definitions under `#ifdef EC_USPACE_MASTER` |
| `master/uspace/main.c` | Thin consumer of library API: arg parsing + signal handling + calls to `ecrt_lib_init`/`ecrt_startup_master`/`ecrt_release_master`/`ecrt_lib_cleanup` |
| `configure.ac` | Add `--enable-uspace-master` option, conditionals, implied options, XDP detection |
| `master/Makefile.am` | Conditional uspace subdirectory |

### Files NOT Modified

- `master/master.h` — no type renames
- `master/master.c` — no changes to core
- `master/*.c` — master core unchanged
- `lib/` — untouched (disabled when uspace-master enabled)

## Task Tracking

- [x] Extract device functions from `main.c` → `device_uspace.c`
- [x] Implement `master/uspace/module.c`
- [x] Fix MAC address pointer lifetime (copy in module.c)
- [x] Fix interface name string lifetime (copy in module.c)
- [x] Add transport ownership flag
- [x] Modify `include/ecrt.h` with new API
- [x] Refactor `master/uspace/main.c` to use library API
- [x] Create `master/uspace/Makefile.am`
- [x] Modify `configure.ac` (`--enable-uspace-master` + implied options)
- [x] Modify `master/Makefile.am` (conditional subdirectory)
- [x] Update `AC_CONFIG_FILES` list
- [x] XDP/BPF library detection in configure
- [x] Extend `ecrt_startup_master()` signature with index, backup_interface, debug_level, run_on_cpu
- [x] Extend `ecrt_startup_master_custom()` signature similarly
- [x] Add backup transport support to `ec_master_pal_t` and `ecrt_startup_master_common()`
- [x] Add backup device setup/teardown in module.c
- [x] Implement multi-master CLI parsing in main.c
- [x] Implement daemonization (double-fork, setsid, PID file)
- [x] Implement syslog support (global flag in ec_log)
- [x] Implement --foreground / --log-stdout flags
- [x] Update help text
- [ ] Test: build with `--enable-uspace-master`
- [ ] Test: build without (default kernel mode unchanged)
- [ ] Test: `ec_master` standalone binary works
- [ ] Test: application linked against `libethercat.so` works

## Items to Watch

The following are not bugs or blockers, but areas worth keeping in mind for
future hardening and documentation.

### 1. Multiple Master Instances

The `ec_master` standalone tool is designed for multiple simultaneous masters.
Calling `ecrt_startup_master()` multiple times creates independent master
instances, each with its own `ec_master_pal_t` fields. This is intentional for
the library model (unlike the kernel module which has a global master array).

In `ec_master`, multiple masters are configured via repeated `-i` flags (see
the Multi-Master CLI Design section). Each `-i` starts a new master block with
an auto-incremented index. All masters run concurrently and are shut down
together on SIGINT/SIGTERM.

### 2. Thread Safety of `ecrt_lib_init()` / `ecrt_lib_cleanup()`

The global workqueue initialization functions (`ec_pal_work_init()`,
`ec_pal_irq_work_init()`) are called once during `ecrt_lib_init()`. If two
threads race on `ecrt_lib_init()`, double initialization could occur. For
realtime applications this is typically a non-issue (single-threaded init on
startup), but could be hardened later with an `atomic_flag` or `pthread_once`
guard.

### 3. `ecrt_release_master()` Phase Safety

The current code checks `master->phase != EC_ORPHANED` before leaving
idle/operation phase, and checks `master->active` before calling
`ec_master_leave_operation_phase()`. This is correct for all normal
sequences. Edge cases to be aware of:

- If `ecrt_master_activate()` was never called, `master->active` is 0 and the
  operation-phase teardown is correctly skipped.
- If the master is in an error state where `phase` was not updated, the
  cleanup may skip necessary teardown. This matches kernel behavior.

### 4. `EC_USPACE_MASTER` Define

The new `ecrt.h` API is guarded by `#ifdef EC_USPACE_MASTER`. Ensure the
build system defines this flag (`-DEC_USPACE_MASTER`) when building with
`--enable-uspace-master`. Verify via `configure.ac` that `AC_DEFINE` or
`AM_CPPFLAGS` propagates this to both the library build and installed
headers.

### 5. Shared Library Versioning

The library currently uses `-version-info 0:0:0` (libtool). Before a stable
release, the version-info triple should be updated following libtool's
current:revision:age scheme to maintain ABI compatibility tracking.
