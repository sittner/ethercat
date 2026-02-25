# Userspace `ethercat` Command-Line Tool Implementation

## Overview

Enable the `ethercat` command-line tool to work with the userspace master
(`EC_USPACE_MASTER` build). The tool communicates with the application process
hosting the master(s) via a Unix domain socket instead of ioctl on
`/dev/EtherCATN`.

The design follows the same PAL pattern used in the master code: **zero
platform-specific `#ifdef` in shared code**. Platform isolation is achieved
entirely through the build system selecting which backend source file gets
compiled and linked.

## Architecture

### Kernel Mode (existing, unchanged)

```
ethercat (tool) → open("/dev/EtherCATN") → ioctl(fd, EC_IOCTL_*, data) → kernel module
                  one device per master        ~30 ioctl commands
```

### Userspace Mode (new)

```
Application
  → ecrt_lib_init(log_cb, "/var/run/ethercat.sock")
      → starts IPC listener thread on unix socket
  → ecrt_startup_master(0, ...)   → master 0, registered in global registry
  → ecrt_startup_master(1, ...)   → master 1, registered in global registry

ethercat (tool)
  → connect("/var/run/ethercat.sock")   (or --socket / EC_SOCKET_PATH)
  → request(cmd, master_idx, data)      → IPC server dispatches to master
```

Key difference from kernel mode: **one socket per user process, multiple
masters behind it**. The tool specifies a master index in each request header,
rather than connecting to a per-master device node.

If `ecrt_lib_init()` is called with `socket_path = NULL`, **no IPC listener
is started**. This allows applications that don't need tool access to avoid
the overhead entirely.

## API Changes

### `ecrt_lib_init()` Signature Change

```c
/* Old: */
EC_PUBLIC_API int ecrt_lib_init(ec_log_cb_t log_cb);

/* New: */
EC_PUBLIC_API int ecrt_lib_init(ec_log_cb_t log_cb, const char *socket_path);
```

- `socket_path != NULL` — start IPC listener on that path
- `socket_path == EC_IPC_DEFAULT_SOCKET_PATH` — use default path
- `socket_path == NULL` — no IPC listener (tool cannot connect)

The uspace API is behind `#ifdef EC_USPACE_MASTER` and not yet released,
so this is not a compatibility break.

### Default Socket Path

```c
/* In include/ec_ioctl_types.h */
#ifndef EC_IPC_DEFAULT_SOCKET_PATH
#define EC_IPC_DEFAULT_SOCKET_PATH "/var/run/ethercat.sock"
#endif
```

### Application Code

```c
#include <ecrt.h>

int main() {
    /* Start IPC listener on default socket */
    ecrt_lib_init(NULL, EC_IPC_DEFAULT_SOCKET_PATH);

    ec_master_t *master = ecrt_startup_master(
            0, EC_TRANSPORT_RAW, "eth0", NULL, 1, 0xffffffff);

    /* ... normal operation ... */

    ecrt_release_master(master);
    ecrt_lib_cleanup();
}
```

```c
    /* No tool access needed */
    ecrt_lib_init(NULL, NULL);
```

### Tool Invocation

```
# Use default socket
ethercat slaves

# Use custom socket path (command line)
ethercat --socket /tmp/my_ethercat.sock slaves

# Use custom socket path (environment)
EC_SOCKET_PATH=/tmp/my_ethercat.sock ethercat slaves
```

Priority: `--socket` > `EC_SOCKET_PATH` > `EC_IPC_DEFAULT_SOCKET_PATH`.

## Master Registry

The IPC server receives `master_index` in each request and must look up the
corresponding `ec_master_t *`. A global registry array is added (similar to
the kernel module's `masters[]` array):

```c
/* In master/uspace/cdev.c (EC_MAX_MASTERS defined in ../globals.h) */
static ec_master_t *master_registry[EC_MAX_MASTERS];
```

- `ecrt_startup_master()` registers the master at `master_registry[index]`
- `ecrt_release_master()` unregisters it (`master_registry[index] = NULL`)
- `ec_master_registry_count()` returns the current count (for `EC_CMD_MODULE` response)

## Wire Protocol

### Request (tool → server)

```c
typedef struct {
    uint32_t version_magic;   /* EC_IOCTL_VERSION_MAGIC */
    uint32_t cmd;             /* ec_tool_cmd enum value */
    uint32_t master_index;    /* which master */
    uint32_t data_size;       /* payload size in bytes (struct + trailing data) */
    /* followed by data_size bytes */
} ec_ipc_request_t;
```

### Response (server → tool)

```c
typedef struct {
    int32_t  ret;             /* 0 on success, -errno on failure */
    uint32_t data_size;       /* response payload size */
    /* followed by data_size bytes */
} ec_ipc_response_t;
```

### Handling Pointer Fields

Several `ec_ioctl_*_t` structs contain pointer fields that reference
separate buffers. These cannot be serialized as-is over a socket. The wire
protocol linearizes them as **struct header + trailing data blob**.

#### Structs with Pointer Fields (tool commands only)

| Struct | Pointer field | Direction | Trailing data |
|--------|--------------|-----------|---------------|
| `ec_ioctl_domain_data_t` | `uint8_t *target` | out | Server appends `data_size` bytes after struct |
| `ec_ioctl_slave_sdo_upload_t` | `uint8_t *target` | out | Server appends `data_size` bytes after struct |
| `ec_ioctl_slave_sdo_download_t` | `uint8_t *data` | in | Tool appends `data_size` bytes after struct |
| `ec_ioctl_slave_sii_t` | `uint16_t *words` | in/out | Tool/server appends `nwords * 2` bytes after struct |
| `ec_ioctl_slave_reg_t` | `uint8_t *data` | in/out | Tool/server appends `size` bytes after struct |
| `ec_ioctl_slave_foe_t` | `uint8_t *buffer` | in/out | Tool/server appends `buffer_size` bytes after struct |
| `ec_ioctl_slave_soe_read_t` | `uint8_t *data` | out | Server appends `data_size` bytes after struct |
| `ec_ioctl_slave_soe_write_t` | `uint8_t *data` | in | Tool appends `data_size` bytes after struct |

#### Wire Layout for Commands with Trailing Data

**Request (tool → server), e.g. SDO download:**

```
[ ec_ipc_request_t header                          ]
[   .data_size = sizeof(ec_ioctl_slave_sdo_download_t) + blob_size ]
[ ec_ioctl_slave_sdo_download_t (pointer set to 0) ]
[ blob_size bytes of SDO data                      ]
```

**Response (server → tool), e.g. SDO upload:**

```
[ ec_ipc_response_t header                         ]
[   .data_size = sizeof(ec_ioctl_slave_sdo_upload_t) + actual_data_size ]
[ ec_ioctl_slave_sdo_upload_t (data_size filled in, pointer set to 0)  ]
[ actual_data_size bytes of SDO data               ]
```

The pointer field is set to 0 (NULL) on the wire. The receiver knows the
trailing data offset from `sizeof(struct)` and the blob length from either
a size field in the struct or `ipc_header.data_size - sizeof(struct)`.

#### Structs Without Pointer Fields

All other `ec_ioctl_*_t` structs (e.g., `ec_ioctl_master_t`,
`ec_ioctl_slave_t`, `ec_ioctl_config_t`, etc.) are flat — they are sent
and received as-is with `data_size = sizeof(struct)`.

## Command Numbers

Command numbers are extracted from the existing ioctl numbering as a
platform-neutral enum in the shared header:

```c
/* include/ec_ioctl_types.h */
enum ec_tool_cmd {
    EC_CMD_MODULE               = 0x00,
    EC_CMD_MASTER               = 0x01,
    EC_CMD_SLAVE                = 0x02,
    EC_CMD_SLAVE_SYNC           = 0x03,
    EC_CMD_SLAVE_SYNC_PDO       = 0x04,
    EC_CMD_SLAVE_SYNC_PDO_ENTRY = 0x05,
    EC_CMD_DOMAIN               = 0x06,
    EC_CMD_DOMAIN_FMMU          = 0x07,
    EC_CMD_DOMAIN_DATA          = 0x08,
    EC_CMD_MASTER_DEBUG         = 0x09,
    EC_CMD_MASTER_RESCAN        = 0x0a,
    EC_CMD_SLAVE_STATE          = 0x0b,
    EC_CMD_SLAVE_SDO            = 0x0c,
    EC_CMD_SLAVE_SDO_ENTRY      = 0x0d,
    EC_CMD_SLAVE_SDO_UPLOAD     = 0x0e,
    EC_CMD_SLAVE_SDO_DOWNLOAD   = 0x0f,
    EC_CMD_SLAVE_SII_READ       = 0x10,
    EC_CMD_SLAVE_SII_WRITE      = 0x11,
    EC_CMD_SLAVE_REG_READ       = 0x12,
    EC_CMD_SLAVE_REG_WRITE      = 0x13,
    EC_CMD_SLAVE_FOE_READ       = 0x14,
    EC_CMD_SLAVE_FOE_WRITE      = 0x15,
    EC_CMD_SLAVE_SOE_READ       = 0x16,
    EC_CMD_SLAVE_SOE_WRITE      = 0x17,
    EC_CMD_SLAVE_EOE_IP_PARAM   = 0x18,
    EC_CMD_CONFIG               = 0x19,
    EC_CMD_CONFIG_PDO           = 0x1a,
    EC_CMD_CONFIG_PDO_ENTRY     = 0x1b,
    EC_CMD_CONFIG_SDO           = 0x1c,
    EC_CMD_CONFIG_IDN           = 0x1d,
    EC_CMD_CONFIG_FLAG          = 0x1e,
    EC_CMD_CONFIG_EOE_IP_PARAM  = 0x1f,
    EC_CMD_EOE_HANDLER          = 0x20,
};
```

The kernel backend maps these to `_IO`/`_IOR`/`_IOW`/`_IOWR` macros via a
lookup table. The socket backend sends them as-is in the request header.

## Shared Header: `include/ec_ioctl_types.h`

All `ec_ioctl_*_t` struct definitions and the `ec_tool_cmd` enum are
extracted from `master/kernel/ioctl.h` into a new shared header that has
**no platform-specific includes** (no `<linux/ioctl.h>`).

`master/kernel/ioctl.h` becomes a thin kernel-specific wrapper that
includes the shared header and adds the `_IO` macros + `#ifdef __KERNEL__`
context.

`tool/Command.h` switches from `#include "../master/kernel/ioctl.h"` to
`#include "ec_ioctl_types.h"`.

## Tool-Side Abstraction Layer

### `MasterDeviceBackend` Interface

```c++
/* tool/MasterDeviceBackend.h */
class MasterDeviceBackend
{
    public:
        virtual ~MasterDeviceBackend() {}
        virtual void open(unsigned int index, bool writable) = 0;
        virtual void close() = 0;
        virtual int request(unsigned int cmd, void *data,
                size_t size, unsigned long arg = 0) = 0;
        static MasterDeviceBackend *create(const std::string &socketPath);
};
```

Note: `requestWithTrailingData()` will be added to the interface in Phase 2.

### Kernel Backend (`tool/kernel/MasterDeviceKernel.cpp`)

- Opens `/dev/EtherCATN`, uses `ioctl()` calls
- Maps `ec_tool_cmd` → `EC_IOCTL_*` via lookup table
- Factory: `MasterDeviceBackend::create("")` returns kernel backend
- Compiled only when `ENABLE_USPACE_MASTER` is **not** set

### Socket Backend (`tool/uspace/MasterDeviceUspace.cpp`)

- Connects to Unix socket (path from `MasterDevice::setSocketPath()`)
- Sends `ec_ipc_request_t` + payload, receives `ec_ipc_response_t` + payload
- Linearizes/delinearizes pointer fields as trailing data
- Factory: `MasterDeviceBackend::create(path)` returns socket backend
- Compiled only when `ENABLE_USPACE_MASTER` **is** set

### `MasterDevice` Delegation

`MasterDevice` becomes a thin wrapper delegating to `MasterDeviceBackend`.
No `#ifdef` anywhere — the backend is selected at link time.

```c++
/* tool/MasterDevice.cpp — no platform-specific code */
MasterDevice::MasterDevice(unsigned int index)
    : index(index), backend(MasterDeviceBackend::create(globalSocketPath))
{ }

void MasterDevice::getMaster(ec_ioctl_master_t *data) {
    int ret = backend->request(EC_CMD_MASTER, data, sizeof(*data));
    if (ret < 0) throw MasterDeviceException(...);
}
/* ... same pattern for all ~30 methods ... */
```

## Tool `main.cpp` Changes

### New Options

```c++
/* In longOptions: */
    {"socket", required_argument, NULL, 'S'},

/* In getopt string: */
    "m:a:p:d:t:o:s:S:efqvh"

/* In switch: */
    case 'S':
        socketPath = optarg;
        break;

/* Before command execution: */
    if (socketPath.empty()) {
        const char *env = getenv("EC_SOCKET_PATH");
        if (env) socketPath = env;
    }
    MasterDevice::setSocketPath(socketPath);
```

### Updated Usage

```
  --socket  -S <path>   Unix socket path for userspace master.
                         Env: EC_SOCKET_PATH
                         Default: /var/run/ethercat.sock
```

## Server Side (in `libethercat.so`)

### IPC Listener Thread

Started by `ecrt_lib_init()` when `socket_path != NULL`:

1. Creates `AF_UNIX` `SOCK_STREAM` socket
2. Binds to `socket_path`, listens
3. Spawns per-connection handler threads (or handles sequentially)
4. Each handler: read request → look up `master_registry[master_index]`
   → dispatch → send response
5. Stopped by `ecrt_lib_cleanup()`

### Locking Strategy

Mirrors the kernel ioctl handler's locking:

- **Read-only commands** (master, slaves, pdos, sdos, config, domains):
  acquire `master->master_sem` (read), release after filling response struct
- **Write commands** (states, debug, rescan, sdo download, reg write, etc.):
  acquire appropriate mutex (`master->master_sem` or `master->io_mutex`)
  as the kernel handler does for the same command
- **Blocking commands** (SDO upload/download, FoE, SoE): hold the
  connection open until the operation completes, same as the kernel
  ioctl which blocks until `copy_to_user`

The kernel ioctl handler in `master/kernel/ioctl.c` uses:
- `down_interruptible(&master->master_sem)` for read operations
- `ec_ioctl_lock_interruptible(&master->io_mutex)` for I/O operations
- `ctx->writable` checks for write operations

The IPC server mirrors this exactly, using the PAL equivalents
(`pthread_mutex_t` / `sem_t`) already provided by the uspace PAL.

### IPC Server Dispatch

The server reuses the ioctl handler functions' **logic**, not the functions
themselves (since those use `copy_to_user`/`copy_from_user`). The dispatch
is a switch on `ec_tool_cmd`, calling internal master functions directly:

```c
/* Conceptual — actual implementation will be a proper dispatch table */
switch (req.cmd) {
    case EC_CMD_MODULE:
        resp.master_count = master_count;
        resp.ioctl_version_magic = EC_IOCTL_VERSION_MAGIC;
        break;
    case EC_CMD_MASTER:
        ec_master_get_info(master, &master_data);  /* fills struct */
        break;
    case EC_CMD_SLAVE:
        ec_master_get_slave_info(master, &slave_data);
        break;
    /* ... */
}
```

## Build System

### `tool/Makefile.am`

```makefile
# Shared sources (all backends)
ethercat_SOURCES = \
    Command.cpp \
    CommandAlias.cpp \
    ... (all existing Command*.cpp unchanged) ...
    MasterDevice.cpp \
    main.cpp \
    sii_crc.cpp

# Backend selection — exactly one gets compiled and linked
if ENABLE_USPACE_MASTER
ethercat_SOURCES += uspace/MasterDeviceUspace.cpp
ethercat_CXXFLAGS = \
    -I$(top_srcdir)/include \
    -Wall -DREV=$(REV) \
    -fno-strict-aliasing
else
ethercat_SOURCES += kernel/MasterDeviceKernel.cpp
ethercat_CXXFLAGS = \
    -I$(top_srcdir)/include \
    -I$(top_srcdir)/master \
    -I$(top_srcdir)/master/kernel \
    -Wall -DREV=$(REV) \
    -fno-strict-aliasing
endif
```

Note: the uspace build only needs `-I$(top_srcdir)/include` (for
`ec_ioctl_types.h` and `ecrt.h`). It never touches `master/kernel/`.

### `configure.ac`

Ensure `BUILD_TOOL` remains functional with `ENABLE_USPACE_MASTER`.
No new configure flags needed — `--enable-tool` (default: yes) works
in both modes.

## File Changes

### New Files

| File | Purpose |
|------|---------|
| `include/ec_ioctl_types.h` | Shared data types + command enum (no platform deps) |
| `tool/MasterDeviceBackend.h` | Abstract backend interface |
| `tool/kernel/MasterDeviceKernel.cpp` | ioctl backend + factory function |
| `tool/uspace/MasterDeviceUspace.cpp` | Unix socket backend + factory function |
| `master/uspace/cdev.c` | IPC listener thread + request dispatch + master registry |
| `master/uspace/cdev.h` | IPC server interface (`ec_ipc_server_start()`, `ec_ipc_server_stop()`) |

### Modified Files

| File | Change |
|------|--------|
| `master/kernel/ioctl.h` | Thin wrapper: includes shared header, adds `_IO` macros |
| `tool/Command.h` | `#include "ec_ioctl_types.h"` (was `../master/kernel/ioctl.h`) |
| `tool/MasterDevice.h` | Remove `int fd`, add `MasterDeviceBackend *backend`, add `setSocketPath()` |
| `tool/MasterDevice.cpp` | Delegate all methods to backend (remove all `ioctl()` calls) |
| `tool/main.cpp` | Add `--socket`/`-S`, `EC_SOCKET_PATH` env, `setSocketPath()` |
| `tool/Makefile.am` | Conditional backend source + include paths |
| `include/ecrt.h.in` | `ecrt_lib_init()` gains `socket_path` parameter, add `EC_IPC_DEFAULT_SOCKET_PATH` |
| `master/uspace/module.c` | Update `ecrt_lib_init()` impl, add master registry, start/stop IPC server |
| `master/uspace/main.c` | Pass socket path to `ecrt_lib_init()` (add `-s`/`--socket` CLI option) |
| `master/uspace/pal.h` | Add IPC server state fields to `ec_master_pal_t` (if needed) |
| `master/uspace/Makefile.am` | Add `ipc_server.c` to library sources |
| `configure.ac` | Ensure `BUILD_TOOL` works with `ENABLE_USPACE_MASTER` |

### Files NOT Modified

- `tool/Command*.cpp` — all 30+ command implementations unchanged
- `tool/Command.h` — only the `#include` path changes
- `master/master.h`, `master/master.c`, `master/*.c` — core unchanged
- `master/kernel/ioctl.c` — kernel ioctl handler unchanged
- `lib/` — untouched (disabled when uspace-master enabled)

## Implementation Phases

### Phase 1: Read-Only Commands

Implement the infrastructure (shared header, backend abstraction, IPC
server skeleton, master registry) and the following read-only commands:

- [x] Extract `ec_ioctl_types.h` from `master/kernel/ioctl.h`
- [x] Make `master/kernel/ioctl.h` a thin wrapper including shared header
- [x] Create `tool/MasterDeviceBackend.h` (abstract interface)
- [x] Create `tool/kernel/MasterDeviceKernel.cpp` (ioctl backend)
- [x] Refactor `tool/MasterDevice.cpp` to delegate to backend
- [x] Change `tool/Command.h` include to `ec_ioctl_types.h`
- [x] Update `tool/Makefile.am` with conditional backend selection
- [x] Verify kernel-mode build still works unchanged
- [x] Change `ecrt_lib_init()` signature: add `socket_path` parameter
- [x] Update `master/uspace/main.c` to pass socket path (add `-s`/`--socket`)
- [x] Implement master registry in `master/uspace/cdev.c`
- [x] Refactor `master/uspace/cdev.c` to global singleton IPC server
- [x] Create `tool/uspace/MasterDeviceUspace.cpp` (socket backend)
- [x] Add `--socket`/`-S` option and `EC_SOCKET_PATH` to `tool/main.cpp`
- [x] IPC: `EC_CMD_MODULE` (version magic + master count)
- [x] IPC: `EC_CMD_MASTER` (master info)
- [x] IPC: `EC_CMD_SLAVE` (slave info)
- [x] IPC: `EC_CMD_SLAVE_SYNC`, `EC_CMD_SLAVE_SYNC_PDO`, `EC_CMD_SLAVE_SYNC_PDO_ENTRY`
- [x] IPC: `EC_CMD_DOMAIN`, `EC_CMD_DOMAIN_FMMU`
- [x] IPC: `EC_CMD_SLAVE_SDO`, `EC_CMD_SLAVE_SDO_ENTRY`
- [x] IPC: `EC_CMD_CONFIG`, `EC_CMD_CONFIG_PDO`, `EC_CMD_CONFIG_PDO_ENTRY`
- [x] IPC: `EC_CMD_CONFIG_SDO`, `EC_CMD_CONFIG_IDN`, `EC_CMD_CONFIG_FLAG`
- [x] IPC: `EC_CMD_MASTER_DEBUG`, `EC_CMD_MASTER_RESCAN`, `EC_CMD_SLAVE_STATE`
- [ ] IPC: `EC_CMD_DOMAIN_DATA` (requires pointer-based trailing data transfer)
- [ ] Test: `ethercat master`, `ethercat slaves`, `ethercat pdos`, `ethercat sdos`
- [ ] Test: `ethercat config`, `ethercat domains`, `ethercat cstruct`
- [ ] Test: `ethercat graph`, `ethercat xml`, `ethercat version`

### Phase 2: Read-Write Commands

- [ ] IPC: `EC_CMD_SLAVE_SDO_UPLOAD` (with trailing data response)
- [ ] IPC: `EC_CMD_SLAVE_SDO_DOWNLOAD` (with trailing data request)
- [ ] IPC: `EC_CMD_SLAVE_SII_READ`, `EC_CMD_SLAVE_SII_WRITE` (trailing data)
- [ ] IPC: `EC_CMD_SLAVE_REG_READ`, `EC_CMD_SLAVE_REG_WRITE` (trailing data)
- [ ] IPC: `EC_CMD_SLAVE_FOE_READ`, `EC_CMD_SLAVE_FOE_WRITE` (trailing data)
- [ ] IPC: `EC_CMD_SLAVE_SOE_READ`, `EC_CMD_SLAVE_SOE_WRITE` (trailing data)
- [ ] Test: `ethercat upload`, `ethercat download`
- [ ] Test: `ethercat states`, `ethercat debug`, `ethercat rescan`
- [ ] Test: `ethercat alias`, `ethercat sii_read`, `ethercat sii_write`
- [ ] Test: `ethercat reg_read`, `ethercat reg_write`
- [ ] Test: `ethercat foe_read`, `ethercat foe_write`
- [ ] Test: `ethercat soe_read`, `ethercat soe_write`

### Phase 3: EoE Commands (if needed)

- [x] IPC: `EC_CMD_EOE_HANDLER` (implemented behind `#ifdef EC_EOE`)
- [ ] IPC: `EC_CMD_SLAVE_EOE_IP_PARAM` (stub, returns -ENOSYS)
- [ ] IPC: `EC_CMD_CONFIG_EOE_IP_PARAM` (stub, returns -ENOSYS)
- [ ] Test: `ethercat eoe`, `ethercat ip`

## Items to Watch

### 1. Access Control on IPC Socket

The Unix socket should be created with permissions matching the kernel
character device convention: owner `root`, group `ethercat`, mode `0660`.
This ensures only authorized users can issue commands to the master.

**Not in scope for this change.** The initial implementation creates the
socket with default permissions. Access control will be added as a
follow-up, consistent with how `/dev/EtherCATN` permissions are managed
by the init scripts and udev rules.

### 2. `ec_master` Daemon Socket Path

The `ec_master` standalone daemon (`master/uspace/main.c`) needs a new
CLI option to pass the socket path through to `ecrt_lib_init()`:

```
ec_master -i eth0 -s /var/run/ethercat.sock
ec_master -i eth0 --socket /var/run/ethercat.sock
```

Default: `EC_IPC_DEFAULT_SOCKET_PATH`. This follows the same pattern as
other `ec_master` options.

### 3. Concurrent Tool Connections

The IPC server should accept multiple simultaneous tool connections (e.g.,
one running `ethercat slaves` while another runs `ethercat upload`). The
kernel cdev supports this naturally via multiple file descriptors. The
IPC server should use per-connection threads or a poll loop.

### 4. `size_t` Portability in Wire Protocol

Several `ec_ioctl_*_t` structs use `size_t` fields, which differ in size
between 32-bit and 64-bit platforms. Since both tool and server run on
the same machine (connected via Unix socket), this is not an immediate
problem. If cross-architecture support is ever needed, the wire protocol
should use fixed-width types (`uint32_t` / `uint64_t`).

### 5. Connection Lifecycle

The current design opens a new connection for each `MasterDevice` instance
(i.e., per command execution). This matches the kernel model where each
`open("/dev/EtherCATN")` is independent. A persistent connection model
could be added later for performance, but is not needed initially since
the tool is a short-lived process.

### 6. Relationship to `master/uspace/cdev.h` Stub

`PAL_IMPLEMENTATION.md` notes that `master/uspace/cdev.h` is a stub with
`//TODO struct cdev`. The IPC server (`ipc_server.c`) effectively replaces
the need for a userspace cdev implementation. The stub can remain as-is
or be removed once the IPC server is complete.
