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
ownership. The flag `transport_owned` in `ec_master_uspace_ctx_t` tracks this:

- `ecrt_startup_master()` → library-owned (`transport_owned = 1`) → `ecrt_release_master()` destroys transport
- `ecrt_startup_master_custom()` → caller-owned (`transport_owned = 0`) → `ecrt_release_master()` does NOT destroy transport

### String and MAC Lifetime

MAC addresses and the interface name string are owned by `ec_master_uspace_ctx_t`
(the wrapper struct allocated in `module.c`):

- `ctx->main_mac[ETH_ALEN]` — MAC address copied from transport at startup
- `ctx->backup_mac[ETH_ALEN]` — backup MAC address (zeroed by default)
- `ctx->interface_name` — interface name `strdup`'d in `ecrt_startup_master()` / `ecrt_startup_master_custom()`, freed in `ecrt_release_master()`

This fixes a latent bug in the current `main.c` where stack-local `main_mac`
happens to outlive the master only because it's in `main()`'s stack frame.

### Master Allocation

The master is heap-allocated as part of `ec_master_uspace_ctx_t`
(`malloc(sizeof(ec_master_uspace_ctx_t))`) since the application cannot know
`sizeof(ec_master_t)` — the type is opaque. The `ec_master_t master` field is
the first member of the context struct, so `(ec_master_t *)ctx == &ctx->master`.
This is consistent with how the existing ioctl-based `lib/common.c` allocates
masters.

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
- [ ] Test: build with `--enable-uspace-master`
- [ ] Test: build without (default kernel mode unchanged)
- [ ] Test: `ec_master` standalone binary works
- [ ] Test: application linked against `libethercat.so` works

## Items to Watch

The following are not bugs or blockers, but areas worth keeping in mind for
future hardening and documentation.

### 1. Multiple Master Instances

`module.c` does not enforce a single-instance constraint. Calling
`ecrt_startup_master()` multiple times will create independent master
instances, each with its own `ec_master_uspace_ctx_t`. This is intentional
for the library model (unlike the kernel module which has a global master
array), but should be documented for users who expect kernel-like behavior.

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
