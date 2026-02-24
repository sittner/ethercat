# Userspace Master Library Implementation Plan

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
 *  Creates transport, opens interface, initializes master,
 *  and enters idle phase (slave scanning starts immediately).
 *
 *  Internally calls:
 *    malloc(sizeof(ec_master_t))
 *    ec_transport_create(transport_type)
 *    ec_transport_open(transport, interface)
 *    ec_transport_get_mac(transport, main_mac)  — MAC is copied
 *    ec_master_init(master, ...)                — interface string is copied
 *    assign transport to device PAL
 *    ec_device_open(&master->devices[EC_DEVICE_MAIN])
 *    ec_master_enter_idle_phase(master)
 *
 *  Transport is library-owned and will be destroyed by ecrt_release_master().
 *
 *  \return Pointer to master, or NULL on error.
 */
EC_PUBLIC_API ec_master_t *ecrt_startup_master(
        ec_transport_type_t transport_type,
        const char *interface
        );

/** Start a userspace master with caller-provided transport.
 *  Same as ecrt_startup_master() but uses an externally created transport.
 *
 *  Transport is caller-owned — ecrt_release_master() will NOT destroy it.
 *
 *  \return Pointer to master, or NULL on error.
 */
EC_PUBLIC_API ec_master_t *ecrt_startup_master_custom(
        ec_transport_t *transport,
        const char *interface
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
 *    ec_master_clear(master)
 *    ec_transport_close(transport)         — always
 *    ec_transport_destroy(transport)       — only if library-owned
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
    ec_master_t *master = ecrt_startup_master(EC_TRANSPORT_RAW, "eth0");

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
ownership. A flag stored in the allocated master context tracks this:

- `ecrt_startup_master()` → library-owned → `ecrt_release_master()` destroys transport
- `ecrt_startup_master_custom()` → caller-owned → `ecrt_release_master()` does NOT destroy transport

### String and MAC Lifetime

`ec_master_init()` stores only a **pointer** to the MAC address, not a copy.
The library must:

- Copy the MAC address into a buffer owned by the master context
- Copy the interface name string into a buffer owned by the master context
- Free these in `ecrt_release_master()`

This fixes a latent bug in the current `main.c` where stack-local `main_mac`
happens to outlive the master only because it's in `main()`'s stack frame.

### Master Allocation

The master must be heap-allocated (`malloc`) since the application cannot know
`sizeof(ec_master_t)` — the type is opaque. This is consistent with how the
existing ioctl-based `lib/common.c` allocates masters.

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

### New Files

| File | Purpose |
|------|---------|
| `master/uspace/module.c` | Library lifecycle: `ecrt_lib_init()`, `ecrt_startup_master()`, `ecrt_startup_master_custom()`, `ecrt_release_master()`, `ecrt_lib_cleanup()` |
| `master/uspace/device_uspace.c` | Device functions extracted from `main.c`: `ec_device_init()`, `ec_device_clear()`, `ec_device_tx_data()`, `ec_device_send()`, `ec_device_poll()`, `ec_device_open()`, `ec_device_close()` |
| `master/uspace/Makefile.am` | Autotools build rules for library + binary |

### Modified Files

| File | Change |
|------|--------|
| `include/ecrt.h` | Add new API functions under `#ifdef EC_USPACE_MASTER` |
| `master/uspace/main.c` | Strip to thin consumer of library API (arg parsing + signal handling + calls to ecrt_lib_init/ecrt_startup_master/ecrt_release_master/ecrt_lib_cleanup) |
| `configure.ac` | Add `--enable-uspace-master` option, conditionals, implied options |
| `master/Makefile.am` | Conditional uspace subdirectory |

### Files NOT Modified

- `master/master.h` — no type renames
- `master/master.c` — no changes to core
- `master/*.c` — master core unchanged
- `lib/` — untouched (disabled when uspace-master enabled)

## Task Tracking

- [ ] Extract device functions from `main.c` → `device_uspace.c`
- [ ] Implement `master/uspace/module.c`
- [ ] Fix MAC address pointer lifetime (copy in module.c)
- [ ] Fix interface name string lifetime (copy in module.c)
- [ ] Add transport ownership flag
- [ ] Modify `include/ecrt.h` with new API
- [ ] Refactor `master/uspace/main.c` to use library API
- [ ] Create `master/uspace/Makefile.am`
- [ ] Modify `configure.ac` (`--enable-uspace-master` + implied options)
- [ ] Modify `master/Makefile.am` (conditional subdirectory)
- [ ] Update `AC_CONFIG_FILES` list
- [ ] XDP/BPF library detection in configure
- [ ] Test: build with `--enable-uspace-master`
- [ ] Test: build without (default kernel mode unchanged)
- [ ] Test: `ec_master` standalone binary works
- [ ] Test: application linked against `libethercat.so` works

