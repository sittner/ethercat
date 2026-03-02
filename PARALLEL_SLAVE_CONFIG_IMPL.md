# Parallel Slave Configuration — Implementation Guide

## Overview

This document describes how to implement parallel (concurrent) slave configuration
in the IgH EtherCAT Master. The goal is to allow multiple slave FSMs to run
simultaneously, each configuring a different slave, instead of the current
sequential approach where one slave must fully complete configuration before
the next begins.

**Base branch:** `parconf`

## Problem Statement

In the current implementation, slave configuration is sequential:
- The master FSM (`fsm_master`) picks up one slave at a time that needs configuration.
- It runs `fsm_slave_config` for that slave using the master FSM's own datagram
  and its own sub-FSM instances (`fsm_change`, `fsm_coe`, `fsm_soe`, `fsm_pdo`, `fsm_eoe`).
- Only after that slave is fully configured (or fails) does it move to the next.
- With N slaves, this means configuration time scales as O(N × per-slave-time).

For systems with many slaves (e.g., 20+), this leads to unacceptable startup times.

## Architecture

### Key Constraint: The External Datagram Ring

The EtherCAT master has two execution contexts:

1. **FSM/IDLE thread** (`ec_master_idle_thread` / `ec_master_operation_thread`):
   Executes state machines, processes mailbox communication.

2. **RT thread** (e.g., LinuxCNC's servo thread): Calls `ecrt_master_send()` and
   `ecrt_master_receive()` to do actual EtherCAT I/O.

In OPERATION phase, these are **different threads**. The datagram queue
(`master->datagram_queue`) is iterated by the RT thread during send/receive.
FSM thread code **must not** directly queue datagrams into `master->datagram_queue`
because there is no mutual exclusion on that list.

The solution already exists: the **external datagram ring**.

```
master->ext_datagram_ring[EC_EXT_RING_SIZE]
master->ext_ring_idx_rt    — consumed by RT thread (inject into datagram_queue)
master->ext_ring_idx_fsm   — produced by FSM thread (write datagrams here)
```

The flow:
1. FSM thread calls `ec_master_get_external_datagram(master)` to get a pointer
   to `&master->ext_datagram_ring[master->ext_ring_idx_fsm]`.
2. FSM thread sets up the datagram (address, data, etc.) via sub-FSM execution.
3. FSM thread advances `ext_ring_idx_fsm`.
4. RT thread calls `ec_master_inject_external_datagrams(master)` inside
   `ecrt_master_send()`, which moves datagrams from the ring into the
   `datagram_queue` — safely, because this runs in the RT thread context.

**This is the mechanism that slave FSMs MUST use.** Slave config FSMs must NOT
have embedded `ec_datagram_t` members. They receive a datagram pointer from the
external ring each cycle.

### Slave Data Structure Note

Slaves are stored as a **C array**, not a linked list:

```c
master->slaves      // pointer to array
master->slave_count // number of elements
// Iteration: for (i = 0; i < master->slave_count; i++) { slave = &master->slaves[i]; ... }
```

All iteration in this document uses array-style loops, matching the actual codebase.

### Current Sub-FSM Ownership

Currently, `ec_fsm_master_t` owns these sub-FSMs that are used during slave
configuration:

| Sub-FSM | Master FSM field | Used by `fsm_slave_config` | Also used by master for other purposes |
|---------|-----------------|---------------------------|---------------------------------------|
| `fsm_change` | `fsm->fsm_change` | Yes (state changes) | No |
| `fsm_coe` | `fsm->fsm_coe` | Yes (SDO config) | Yes (SDO dictionary, internal SDO requests) |
| `fsm_soe` | `fsm->fsm_soe` | Yes (SoE config) | Yes (internal SoE requests) |
| `fsm_pdo` | `fsm->fsm_pdo` | Yes (PDO config) | No |
| `fsm_eoe` | `fsm->fsm_eoe` | Yes (EoE IP params) | No |

The `ec_fsm_slave_config_t` struct holds **pointers** to these sub-FSMs (not
embedded copies). The `ec_fsm_slave_config_init()` takes all of them as parameters.

For parallel config, each slave needs its **own set** of these sub-FSMs. The
master FSM must keep its own `fsm_coe` and `fsm_soe` for SDO dictionary fetching
and internal request handling.

### Current Slave FSM (`ec_fsm_slave_t`)

The existing `ec_fsm_slave_t` in `fsm_slave.h` already has the right basic
structure for request handling:

```c
struct ec_fsm_slave {
    ec_slave_t *slave;
    struct list_head list;           // Used for execution list
    void (*state)(ec_fsm_slave_t *, ec_datagram_t *);  // State function
    ec_datagram_t *datagram;         // Previous state datagram (pointer, not embedded)
    // ... request fields (sdo_request, reg_request, etc.) ...
    ec_fsm_coe_t fsm_coe;           // Already has its own CoE FSM for requests
    ec_fsm_foe_t fsm_foe;
    ec_fsm_soe_t fsm_soe;
    ec_fsm_eoe_t fsm_eoe;           // (ifdef EC_EOE)
};
```

This FSM already handles SDO/FoE/SoE/EoE/register **requests** using per-slave
sub-FSM instances and external ring datagrams. The parallel config feature
extends this same structure to also handle slave **configuration**.

## Design

### 1. Add Config Sub-FSMs to `ec_fsm_slave_t`

Add these fields to `ec_fsm_slave_t` in `master/fsm_slave.h`:

```c
struct ec_fsm_slave {
    // ... existing fields ...

    unsigned int config_running;          /**< Set while slave config FSM is active. */

    ec_fsm_slave_config_t fsm_slave_config; /**< Slave configuration sub-FSM. */
    ec_fsm_change_t fsm_change;             /**< State change FSM (for config). */
    ec_fsm_pdo_t fsm_pdo;                   /**< PDO configuration FSM (for config). */
    // Note: fsm_coe already exists for requests, reuse it for config too.
    // Note: fsm_soe already exists for requests, reuse it for config too.
    // Note: fsm_eoe already exists for requests, reuse it for config too.
};
```

**DO NOT add an embedded `ec_datagram_t`.** The datagram pointer comes from
the external ring, same as for requests.

The `fsm_slave_config` will be initialized to point to the per-slave sub-FSMs:
- `fsm_change` → new, per-slave
- `fsm_coe` → the existing `fsm->fsm_coe` (already per-slave)
- `fsm_soe` → the existing `fsm->fsm_soe` (already per-slave)
- `fsm_pdo` → new, per-slave (needs its own because `fsm_pdo` holds a pointer
  to `fsm_coe` internally)
- `fsm_eoe` → the existing `fsm->fsm_eoe` (already per-slave, ifdef EC_EOE)

### 2. Init/Clear Changes in `ec_fsm_slave_init()` / `ec_fsm_slave_clear()`

In `master/fsm_slave.c`:

```c
void ec_fsm_slave_init(ec_fsm_slave_t *fsm, ec_slave_t *slave)
{
    // ... existing init code ...

    fsm->config_running = 0;

    // Init config sub-FSMs
    ec_fsm_change_init(&fsm->fsm_change, NULL);  // datagram set each cycle
    ec_fsm_pdo_init(&fsm->fsm_pdo, &fsm->fsm_coe);
    ec_fsm_slave_config_init(&fsm->fsm_slave_config, NULL,
            &fsm->fsm_change, &fsm->fsm_coe, &fsm->fsm_soe,
            &fsm->fsm_pdo, &fsm->fsm_eoe);
}

void ec_fsm_slave_clear(ec_fsm_slave_t *fsm)
{
    // ... existing clear code ...

    ec_fsm_slave_config_clear(&fsm->fsm_slave_config);
    ec_fsm_pdo_clear(&fsm->fsm_pdo);
    ec_fsm_change_clear(&fsm->fsm_change);
}
```

Note: `ec_fsm_change_init()` takes a `datagram` parameter that it stores.
Pass NULL initially; the datagram pointer will be updated before each exec cycle.
Check if `ec_fsm_change_init` stores the datagram or just uses it — if it stores
it, ensure it gets updated before each use.

### 3. Starting Slave Configuration

Add a new function in `master/fsm_slave.c`:

```c
void ec_fsm_slave_start_config(ec_fsm_slave_t *fsm)
{
    fsm->config_running = 1;
    ec_fsm_slave_config_start(&fsm->fsm_slave_config, fsm->slave);
    fsm->state = ec_fsm_slave_state_config;
}
```

Declare in `master/fsm_slave.h`:
```c
void ec_fsm_slave_start_config(ec_fsm_slave_t *);
```

### 4. Config State Handler in `ec_fsm_slave_t`

Add a new state function in `master/fsm_slave.c`:

```c
void ec_fsm_slave_state_config(
        ec_fsm_slave_t *fsm,
        ec_datagram_t *datagram)
{
    // Propagate datagram to all sub-FSMs before exec
    fsm->fsm_slave_config.datagram = datagram;
    fsm->fsm_change.datagram = datagram;
    // fsm_coe, fsm_soe, fsm_pdo, fsm_eoe receive datagram through their
    // own exec calls (they take datagram as parameter), so no need to set
    // a stored pointer — but verify this for fsm_change which stores it.

    if (ec_fsm_slave_config_exec(&fsm->fsm_slave_config)) {
        return;  // still running, datagram was used
    }

    // Configuration finished
    fsm->slave->force_config = 0;
    fsm->config_running = 0;

    if (!ec_fsm_slave_config_success(&fsm->fsm_slave_config)) {
        // Config failed
        fsm->slave->error_flag = 1;
    }

    // Return to ready state (or idle, depending on whether requests are enabled)
    fsm->state = ec_fsm_slave_state_ready;
}
```

### 5. Master FSM Changes (`fsm_master.c`)

#### 5a. Remove Sequential Config from Master FSM

The master FSM currently handles slave config in `ec_fsm_master_action_configure()`
which enters `ec_fsm_master_state_configure_slave()` and blocks there.

**Change `ec_fsm_master_action_configure()`** to kick off the per-slave FSM
instead of blocking:

```c
void ec_fsm_master_action_configure(ec_fsm_master_t *fsm)
{
    ec_master_t *master = fsm->master;
    ec_slave_t *slave = fsm->slave;

    if (master->config_changed) {
        master->config_changed = 0;
        EC_MASTER_DBG(master, 1, "Configuration changed"
                " (aborting state check).\n");
        fsm->slave = master->slaves;
        ec_fsm_master_enter_write_system_times(fsm);
        return;
    }

    // Does the slave have to be configured?
    if ((slave->current_state != slave->requested_state
                || slave->force_config)
            && !slave->error_flag
            && !slave->fsm.config_running) {

        // Kick off per-slave config FSM (non-blocking)
        if (master->debug_level) {
            char old_state[EC_STATE_STRING_SIZE],
                 new_state[EC_STATE_STRING_SIZE];
            ec_state_string(slave->current_state, old_state, 0);
            ec_state_string(slave->requested_state, new_state, 0);
            EC_SLAVE_DBG(slave, 1, "Changing state from %s to %s%s.\n",
                    old_state, new_state,
                    slave->force_config ? " (forced)" : "");
        }

        ec_fsm_slave_start_config(&slave->fsm);
    }

    // Continue to next slave immediately — don't block
    ec_fsm_master_action_next_slave_state(fsm);
}
```

#### 5b. Remove `ec_fsm_master_state_configure_slave()`

Delete or stub out this function. It is no longer needed since slave config
is driven by `ec_master_exec_slave_fsms()`.

#### 5c. Handle `config_busy` for Parallel Configs

Currently `config_busy` is set to 1 when one slave starts configuring and
cleared when it finishes. With parallel config, multiple slaves may be
configuring simultaneously.

Option A (simple): Don't use `config_busy` for per-slave configs at all. Just
remove the `config_busy` set/clear from `ec_fsm_master_action_configure` and
`ec_fsm_master_state_configure_slave`. If userspace code waits on `config_queue`,
it now returns immediately (slaves configure asynchronously).

Option B (tracking): Use a counter instead of a flag. Increment when starting
a config, decrement when finishing. Wake `config_queue` when it reaches 0.
This requires `ec_fsm_slave_state_config()` to signal the master when done.

**Recommended: Option A** for initial implementation. The config_busy mechanism
is primarily used during `ecrt_master_activate()` which waits for all slaves
to reach their target state. The activate path can check all slaves directly
instead of relying on config_busy.

#### 5d. Keep Master FSM's Own Sub-FSMs for Non-Config Tasks

The master FSM still needs its own `fsm_coe` and `fsm_soe` for:
- SDO dictionary fetching (`ec_fsm_master_state_sdo_dictionary`)
- Internal SDO requests (`ec_fsm_master_state_sdo_request`)
- Internal SoE requests (`ec_fsm_master_state_soe_request`)

These remain unchanged in `ec_fsm_master_t`. Only `fsm_slave_config` (and
`fsm_change` if it was only used for config) can be removed from the master FSM.

**What to remove from `ec_fsm_master_t`:**
- `fsm_slave_config` — moved to per-slave
- `fsm_change` — moved to per-slave (verify it's not used elsewhere in master FSM)

**What stays in `ec_fsm_master_t`:**
- `fsm_coe` — used for SDO dictionary + internal SDO requests
- `fsm_soe` — used for internal SoE requests
- `fsm_pdo` — check if used outside config; if only used by config, remove
- `fsm_eoe` — check if used outside config; if only used by config, remove
- `fsm_slave_scan` — still needed for scanning (uses master datagram)
- `fsm_sii` — still needed for SII operations

#### 5e. `ec_fsm_slave_scan_t` Impact

`ec_fsm_slave_scan_t` currently takes pointers to `fsm_slave_config` and
`fsm_pdo` from the master FSM. Scanning is sequential (one slave at a time)
and uses the master's own datagram. During scanning, the scan FSM may invoke
`fsm_slave_config` for the slave being scanned (to bring it to INIT).

**For now, keep scanning sequential.** The master FSM's `fsm_slave_scan` can
point to a dedicated `fsm_slave_config` instance that remains in the master FSM
for scanning purposes only. Or, since scanning is sequential and each slave now
has its own `fsm_slave_config`, the scan FSM can use `slave->fsm.fsm_slave_config`
for the slave being scanned.

**Recommended approach:** During scanning, use `slave->fsm.fsm_slave_config` and
`slave->fsm.fsm_pdo` (since they're per-slave now). Update
`ec_fsm_slave_scan_init()` to no longer require these as constructor parameters.
Instead, set them at scan start time from the slave being scanned:

```c
void ec_fsm_slave_scan_start(ec_fsm_slave_scan_t *fsm, ec_slave_t *slave)
{
    fsm->slave = slave;
    fsm->fsm_slave_config = &slave->fsm.fsm_slave_config;
    fsm->fsm_pdo = &slave->fsm.fsm_pdo;
    fsm->state = ec_fsm_slave_scan_state_start;
}
```

### 6. `ec_master_exec_slave_fsms()` — The Main Loop

This function already exists in `master/master.c` and iterates slaves for FSM
execution. It must be updated to handle config FSMs alongside request FSMs.

The existing code uses a round-robin `master->fsm_slave` pointer and an
execution list (`master->fsm_exec_list`). Config FSMs integrate into this
same mechanism — a slave with `config_running` set will have a non-idle/non-ready
state function that consumes datagrams just like request processing does.

**No major structural change is needed in `ec_master_exec_slave_fsms()`** because
the slave FSM's `ec_fsm_slave_exec()` already handles dispatching to the current
state function, which can be either a request state or the new config state.
The existing round-robin logic and execution list should work as-is.

Verify that `ec_fsm_slave_is_ready()` returns 0 when `config_running` is set
(it will, because the state function will be `ec_fsm_slave_state_config`, not
`ec_fsm_slave_state_ready`).

### 7. External Datagram Ring Size

The current `EC_EXT_RING_SIZE` is 32 (defined in `master/master.h`). Each
active slave FSM needs one ring slot per cycle. With parallel configuration
of many slaves, this may be insufficient.

Increase to 64:

```c
// master/master.h
#define EC_EXT_RING_SIZE 64
```

Also check the guard in `ec_master_exec_slave_fsms()`:
```c
while (master->fsm_exec_count < EC_EXT_RING_SIZE / 2 ...)
```
This limits concurrent FSMs to half the ring size. With 64, that's 32
simultaneous FSMs which should be sufficient.

### 8. Sub-FSM Datagram Propagation

The critical detail: `ec_fsm_slave_config_t` stores a `datagram` pointer
and passes it to sub-FSMs. The sub-FSMs that take datagram as exec parameter
(`ec_fsm_coe_exec(fsm, datagram)`, `ec_fsm_soe_exec(fsm, datagram)`,
`ec_fsm_eoe_exec(fsm, datagram)`) are fine — they use the passed datagram.

However, `ec_fsm_change_t` stores `datagram` as a member set during `init()`.
This must be updated each cycle before use:

```c
// In ec_fsm_slave_state_config(), before calling fsm_slave_config_exec:
fsm->fsm_change.datagram = datagram;
fsm->fsm_slave_config.datagram = datagram;
```

Similarly, `ec_fsm_slave_config_exec()` is currently a void-return-checking
pattern — it returns 1 if running, 0 if done. Inside, it calls
`fsm->state(fsm)` which accesses `fsm->datagram`. We must ensure
`fsm->datagram` is set before each call.

`ec_fsm_pdo_t` takes datagram via its `fsm_coe` sub-FSM which receives it as
exec parameter. Verify there are no stored datagram references in `fsm_pdo`
that need updating.

### 9. IDLE Phase Considerations

In IDLE phase (`ec_master_idle_thread`), the master does its own send/receive,
so the external datagram ring injection happens within the same thread. The
flow is:

```
ec_master_idle_thread() loop:
    1. ec_master_receive_datagrams(master)
    2. ec_fsm_master_exec(&master->fsm)
    3. ec_master_exec_slave_fsms(master)       — run all slave FSMs
    4. ec_master_inject_external_datagrams(master)
    5. ec_master_send_datagrams(master)
```

This works correctly because inject and send happen after all FSMs have run.

### 10. OPERATION Phase Considerations

In OPERATION phase, the FSM thread and RT thread are separate:

```
FSM thread (ec_master_operation_thread):
    1. ec_fsm_master_exec(&master->fsm)
    2. ec_master_exec_slave_fsms(master)       — writes to ring
    3. master->injection_seq_fsm++

RT thread (ecrt_master_send):
    1. ec_master_inject_external_datagrams(master)  — ring → queue
    2. ec_master_send_datagrams(master)
```

The ring provides the thread-safe handoff. No additional locking is needed
because the ring is a single-producer (FSM thread) single-consumer (RT thread).

## Files to Modify

| File | Changes |
|------|---------|
| `master/fsm_slave.h` | Add `config_running` field. Add `fsm_slave_config`, `fsm_change`, `fsm_pdo` sub-FSM instances. Add `ec_fsm_slave_start_config()` declaration. Add `#include "fsm_slave_config.h"`. |
| `master/fsm_slave.c` | Init/clear new sub-FSMs. Add `ec_fsm_slave_start_config()`. Add `ec_fsm_slave_state_config()` state function. |
| `master/fsm_master.h` | Remove `fsm_slave_config` and `fsm_change` from `ec_fsm_master_t` (keep `fsm_coe`, `fsm_soe`, `fsm_pdo`, `fsm_eoe` if used for non-config tasks; remove if only used by config). |
| `master/fsm_master.c` | Change `ec_fsm_master_action_configure()` to kick off per-slave FSM non-blocking. Remove `ec_fsm_master_state_configure_slave()`. Update `ec_fsm_master_init()`/`ec_fsm_master_clear()` to remove sub-FSMs that moved to per-slave. Remove `config_busy` set/clear from config path. |
| `master/fsm_slave_config.h` | No changes needed (already uses pointers to sub-FSMs). |
| `master/fsm_slave_config.c` | No changes needed (already uses pointer-based sub-FSMs). Verify datagram propagation is correct. |
| `master/fsm_slave_scan.h` | Possibly remove `fsm_slave_config` and `fsm_pdo` from constructor params. |
| `master/fsm_slave_scan.c` | Set `fsm_slave_config` and `fsm_pdo` from slave being scanned in `_start()`. |
| `master/master.h` | Increase `EC_EXT_RING_SIZE` from 32 to 64. |
| `master/master.c` | Verify `ec_master_exec_slave_fsms()` handles config FSMs correctly (should work with existing round-robin logic). |
| `master/fsm_change.c` | Verify datagram pointer is updated before each use (it stores datagram in struct). |

## Implementation Order

1. **Phase 1 — Per-slave sub-FSM instances:**
   Add `fsm_slave_config`, `fsm_change`, `fsm_pdo` to `ec_fsm_slave_t`.
   Init/clear them in `ec_fsm_slave_init()`/`ec_fsm_slave_clear()`.
   Add `config_running` flag.
   Add `ec_fsm_slave_start_config()` and `ec_fsm_slave_state_config()`.

2. **Phase 2 — Master FSM decoupling:**
   Change `ec_fsm_master_action_configure()` to kick off slave FSMs non-blocking.
   Remove `ec_fsm_master_state_configure_slave()`.
   Remove `fsm_slave_config` and `fsm_change` from `ec_fsm_master_t`.
   Update `ec_fsm_master_init()`/`ec_fsm_master_clear()`.
   Handle `config_busy` (simplest: remove it from config path).

3. **Phase 3 — Scan FSM update:**
   Update `ec_fsm_slave_scan_t` to use per-slave `fsm_slave_config` and `fsm_pdo`.
   Remove these from scan constructor params.

4. **Phase 4 — Ring size and testing:**
   Increase `EC_EXT_RING_SIZE` to 64.
   Test with multiple slaves. Verify IDLE and OPERATION phase behavior.

## Critical Invariants (Checklist)

- [ ] No `ec_datagram_t` as struct member in `ec_fsm_slave_t` (only pointer)
- [ ] Each slave has its own `fsm_slave_config`, `fsm_change`, `fsm_pdo` instances
- [ ] Datagram pointer propagated to `fsm_slave_config.datagram` and `fsm_change.datagram` before each exec
- [ ] `ext_ring_idx_fsm` only advanced when datagram is actually consumed
- [ ] No direct calls to `ec_master_queue_datagram()` from slave config FSMs
- [ ] `config_running` flag prevents duplicate config starts
- [ ] Master FSM does not block waiting for any single slave config
- [ ] Master FSM keeps its own `fsm_coe`/`fsm_soe` for SDO dictionary and internal requests
- [ ] Scanning still works (uses per-slave `fsm_slave_config` instead of master's)
- [ ] Ring size sufficient for max parallel configs + other requests (64)

## Estimated Scope

- ~400–600 lines of changes across 8–10 files
- Core logic is small: add sub-FSMs to `ec_fsm_slave_t`, add one new state function,
  change master FSM to kick-off instead of block
- Most risk: getting datagram propagation right through `fsm_slave_config` →
  `fsm_change` (which stores datagram pointer)
- Secondary risk: scan FSM transition to per-slave sub-FSMs
