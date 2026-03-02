# Parallel Slave Configuration — Implementation Guide

## Overview

This document describes how to implement parallel (concurrent) slave configuration
in the IgH EtherCAT Master. The goal is to allow multiple slave FSMs to run
simultaneously, each configuring a different slave, instead of the current
sequential approach where one slave must fully complete configuration before
the next begins.

**Base branch:** `uspace`

## Problem Statement

In the current `uspace` implementation, slave configuration is sequential:
- The master FSM (`fsm_master`) picks up one slave at a time that needs configuration.
- It runs `fsm_slave_config` for that slave using the master FSM's own datagram.
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

The solution already exists in `uspace`: the **external datagram ring**.

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

**This is the mechanism that slave FSMs MUST use.** Slave FSMs must NOT have
embedded `ec_datagram_t` members. They must NOT call `ec_master_queue_datagram()`
directly.

### Current Slave Request Handling (uspace baseline)

In `uspace`, slave requests (SDO, register, FoE, SoE) already use this pattern:

```c
// In ec_master_exec_slave_fsms() — current uspace code
list_for_each_entry(slave, &master->slaves, list) {
    if (slave->fsm_request) {
        ec_datagram_t *datagram = ec_master_get_external_datagram(master);
        if (!datagram) break;  // ring full
        // ... execute the request FSM with this datagram ...
        master->ext_ring_idx_fsm = (master->ext_ring_idx_fsm + 1) % EC_EXT_RING_SIZE;
    }
}
```

The parallel slave config feature extends this same pattern to slave configuration.

## Design

### 1. `ec_fsm_slave_t` Structure Changes

**DO NOT add an embedded `ec_datagram_t datagram` member.**

The structure should hold:
- A **pointer** to the datagram currently being used (received from the ring).
- The sub-FSMs (`fsm_slave_config`, `fsm_slave_scan`).
- State tracking (`state`, `idle_flag`, `config_running`).

```c
// master/fsm_slave.h

struct ec_fsm_slave {
    ec_slave_t *slave;              /**< Slave this FSM runs on. */
    ec_datagram_t *datagram;        /**< Pointer to external ring datagram (NOT owned). */

    void (*state)(ec_fsm_slave_t *, ec_datagram_t *);  /**< Current state function. */
    unsigned int idle_flag;         /**< Set if FSM is idle (not busy). */
    unsigned int config_running;    /**< Set while slave config FSM is active. */

    ec_fsm_slave_config_t fsm_slave_config;  /**< Slave configuration sub-FSM. */
    ec_fsm_slave_scan_t fsm_slave_scan;      /**< Slave scanning sub-FSM. */
    // ... existing request FSM fields ...
};
```

### 2. `ec_fsm_slave_exec()` Signature

The exec function **receives an external datagram as a parameter**:

```c
// master/fsm_slave.h
int ec_fsm_slave_exec(ec_fsm_slave_t *fsm, ec_datagram_t *datagram);
```

Returns: 1 if the datagram was consumed (needs sending), 0 if FSM is idle or
datagram was not used.

### 3. `ec_fsm_slave_exec()` Implementation

```c
// master/fsm_slave.c
int ec_fsm_slave_exec(ec_fsm_slave_t *fsm, ec_datagram_t *datagram)
{
    // If this FSM has a datagram in flight, check if it has been received
    if (fsm->datagram) {
        if (fsm->datagram->state == EC_DATAGRAM_SENT ||
            fsm->datagram->state == EC_DATAGRAM_QUEUED) {
            // Previous datagram not yet returned — skip this cycle
            return 0;
        }
        // Datagram received (RECEIVED, TIMED_OUT, ERROR, etc.)
        // Run the state function with the RECEIVED datagram
        fsm->state(fsm, fsm->datagram);
        fsm->datagram = NULL;  // Release reference to old datagram
    }

    // If FSM is now idle, nothing more to do
    if (fsm->idle_flag) {
        return 0;
    }

    // FSM wants to send — assign the new datagram
    fsm->datagram = datagram;

    // The state function will have set up datagram via sub-FSM
    // (the sub-FSMs write to the datagram passed to them)
    fsm->state(fsm, datagram);

    if (fsm->idle_flag) {
        // State function completed without needing to send
        fsm->datagram = NULL;
        return 0;
    }

    return 1;  // Datagram was consumed, needs sending
}
```

**Important:** The exact flow depends on how the sub-FSMs (`fsm_slave_config`,
`fsm_change`, etc.) interact. The key invariants are:

1. The FSM only holds a datagram pointer while it is in flight.
2. The FSM checks the **received** datagram state before proceeding.
3. The FSM receives a **new** datagram from the ring for each new send.
4. The FSM returns whether it consumed the datagram.

### 4. Sub-FSM Datagram Propagation

The sub-FSMs (`fsm_slave_config`, `fsm_change`, `fsm_coe`, `fsm_pdo`,
`fsm_pdo_entry`) currently store a `ec_datagram_t *datagram` pointer. This
pointer must be **updated each cycle** before calling their exec functions:

```c
// When running fsm_slave_config from within fsm_slave state functions:
fsm->fsm_slave_config.datagram = datagram;
ec_fsm_slave_config_exec(&fsm->fsm_slave_config);

// Similarly for nested FSMs within fsm_slave_config:
fsm_config->fsm_change.datagram = datagram;
fsm_config->fsm_coe.datagram = datagram;
// etc.
```

This ensures the entire sub-FSM tree uses the ring datagram, not a stale pointer.

### 5. Master FSM Changes (`fsm_master.c`)

The master FSM currently handles slave configuration directly via its own states
(`ec_fsm_master_state_configure_slave`, etc.). These must be **removed** from
the master FSM. The master FSM should only:

- Detect that a slave needs configuration (wrong state, config flag set).
- **Kick off** the slave's own FSM: set `slave->fsm.idle_flag = 0`,
  `slave->fsm.config_running = 1`, transition to the config start state.
- **Not wait** for the slave FSM to complete. Move on to the next slave immediately.

```c
// In ec_fsm_master_state_start or similar:
list_for_each_entry(slave, &master->slaves, list) {
    if (slave_needs_config(slave) && !slave->fsm.config_running) {
        ec_fsm_slave_start_config(&slave->fsm);
    }
}
// Continue with other master FSM duties — don't block on any single slave.
```

### 6. `ec_master_exec_slave_fsms()` — The Main Loop

This function is called from the IDLE or OPERATION thread each cycle. It iterates
all slaves and, for each active FSM, obtains an external datagram and runs the FSM:

```c
void ec_master_exec_slave_fsms(ec_master_t *master)
{
    ec_slave_t *slave;

    list_for_each_entry(slave, &master->slaves, list) {
        ec_fsm_slave_t *fsm = &slave->fsm;

        // Skip idle FSMs
        if (fsm->idle_flag) {
            continue;
        }

        // If FSM has a datagram in flight, check it
        if (fsm->datagram) {
            if (fsm->datagram->state == EC_DATAGRAM_SENT ||
                fsm->datagram->state == EC_DATAGRAM_QUEUED) {
                continue;  // Still waiting for response
            }
            // Datagram returned — run FSM to process response
            // (FSM will internally call state function with received datagram)
        }

        // Get a new external datagram for this FSM
        ec_datagram_t *datagram = ec_master_get_external_datagram(master);
        if (!datagram) {
            break;  // Ring full — try remaining slaves next cycle
        }

        if (ec_fsm_slave_exec(fsm, datagram)) {
            // Datagram was consumed — advance ring index
            master->ext_ring_idx_fsm =
                (master->ext_ring_idx_fsm + 1) % EC_EXT_RING_SIZE;
        }
    }
}
```

### 7. External Datagram Ring Size

The current `EC_EXT_RING_SIZE` may be too small for parallel configuration of
many slaves. Each active slave FSM needs one ring slot per cycle. Evaluate
whether the ring size needs to be increased:

```c
// master/globals.h or master/master.h
#define EC_EXT_RING_SIZE 64  // May need to increase from current value
```

A safe value is `max_slaves_configuring_in_parallel + margin_for_sdo_requests`.
For typical systems, 32–64 should suffice.

### 8. IDLE Phase Considerations

In IDLE phase (`ec_master_idle_thread`), the master does its own send/receive,
so the external datagram ring injection happens within the same thread. The
flow is:

```
ec_master_idle_thread() loop:
    1. ec_master_receive_datagrams(master)    — receive responses
    2. ec_fsm_master_exec(&master->fsm_master) — run master FSM
    3. ec_master_exec_slave_fsms(master)       — run all slave FSMs
    4. ec_master_inject_external_datagrams(master) — move ring → queue
    5. ec_master_send_datagrams(master)        — send queued datagrams
```

This works correctly because inject and send happen after all FSMs have run.

### 9. OPERATION Phase Considerations

In OPERATION phase, the FSM thread and RT thread are separate:

```
FSM thread (ec_master_operation_thread):
    1. ec_master_receive_datagrams(master)     — receive (if not RT-driven)
    2. ec_fsm_master_exec(&master->fsm_master)
    3. ec_master_exec_slave_fsms(master)        — writes to ring
    4. (does NOT call send)

RT thread (ecrt_master_send):
    1. ec_master_inject_external_datagrams(master)  — ring → queue (safe)
    2. ec_master_send_datagrams(master)             — send everything
```

The ring provides the thread-safe handoff. No additional locking is needed
because the ring is a single-producer (FSM thread writes `ext_ring_idx_fsm`)
single-consumer (RT thread reads `ext_ring_idx_rt`) structure.

### 10. Handling `config_running` State

When the master FSM scans for slaves needing (re)configuration, it must check
`slave->fsm.config_running` to avoid kicking off a duplicate config:

```c
static int slave_needs_config(ec_slave_t *slave)
{
    // Don't start if already running
    if (slave->fsm.config_running)
        return 0;

    // Slave needs config if current state doesn't match requested state
    return slave->current_state != slave->requested_state
        || slave->force_config;
}
```

When `fsm_slave_config` completes (success or failure), it must clear
`config_running`:

```c
// In fsm_slave_config end states:
void ec_fsm_slave_config_state_end(ec_fsm_slave_config_t *fsm)
{
    fsm->slave->fsm.config_running = 0;
    fsm->slave->fsm.idle_flag = 1;
}
```

### 11. Watchdog / Timeout

Each slave FSM should track how long it has been active to detect stuck
configurations. A simple approach:

```c
struct ec_fsm_slave {
    // ...
    unsigned long config_start_jiffies;  /**< When config started. */
};

// On config start:
fsm->config_start_jiffies = jiffies;

// Each cycle, check:
if (jiffies - fsm->config_start_jiffies > EC_SLAVE_CONFIG_TIMEOUT) {
    EC_SLAVE_WARN(slave, "Configuration timeout, aborting.\n");
    fsm->config_running = 0;
    fsm->idle_flag = 1;
}
```

## Files to Modify

| File | Changes |
|------|---------|
| `master/fsm_slave.h` | Remove embedded datagram. Add `ec_datagram_t *datagram` pointer. Update function signatures. Add `config_running` flag. |
| `master/fsm_slave.c` | Implement new `ec_fsm_slave_exec()` with external datagram parameter. Add config start/end state functions. |
| `master/fsm_slave_config.c` | Ensure datagram pointer is updated each cycle from parent. Remove any direct datagram init/queue calls. |
| `master/fsm_master.c` | Remove sequential slave config states. Add detection loop that kicks off slave FSMs. Don't wait for completion. |
| `master/master.c` | Add/update `ec_master_exec_slave_fsms()` to iterate slaves and use external datagram ring. Integrate into IDLE and OPERATION thread loops. |
| `master/master.h` | Add declaration for `ec_master_exec_slave_fsms()`. Possibly increase `EC_EXT_RING_SIZE`. |
| `master/fsm_change.c` | Ensure datagram pointer is received from parent, not stored permanently. |
| `master/fsm_coe.c` | Same — datagram pointer propagation. |
| `master/fsm_pdo.c` | Same. |
| `master/fsm_pdo_entry.c` | Same. |
| `master/globals.h` | Possibly increase `EC_EXT_RING_SIZE`. |

## Implementation Order

1. **Phase 1 — Datagram plumbing:**
   Modify `fsm_slave.h/c` to use external datagram pointer instead of embedded
   datagram. Update `ec_fsm_slave_exec()` signature and implementation. Ensure
   sub-FSM datagram propagation works.

2. **Phase 2 — Master FSM decoupling:**
   Remove sequential slave config from `fsm_master.c`. Add the kick-off loop
   that sets `config_running` and starts slave FSMs independently.

3. **Phase 3 — Ring integration:**
   Implement `ec_master_exec_slave_fsms()` with external datagram ring. Integrate
   into both IDLE and OPERATION thread loops.

4. **Phase 4 — Testing & hardening:**
   Test with multiple slaves. Verify IDLE and OPERATION phase behavior. Add
   timeouts. Test error recovery (slave disappearing mid-config, etc.).

## Critical Invariants (Checklist)

- [ ] No `ec_datagram_t` as struct member in `ec_fsm_slave_t`
- [ ] `ec_fsm_slave_exec()` takes `ec_datagram_t *` parameter from external ring
- [ ] Sub-FSM datagram pointers updated before each exec call
- [ ] `ext_ring_idx_fsm` only advanced when datagram is actually consumed
- [ ] No direct calls to `ec_master_queue_datagram()` from slave FSMs
- [ ] `config_running` flag prevents duplicate config starts
- [ ] OPERATION phase: slave FSM datagrams go through ring, not direct queue
- [ ] IDLE phase: `ec_master_inject_external_datagrams()` called after FSM exec
- [ ] Ring size sufficient for max parallel configs + other requests
- [ ] Master FSM does not block waiting for any single slave config

## Estimated Scope

- ~500–800 lines of changes across 10–12 files
- Core logic is small; most changes are mechanical datagram pointer propagation
- Highest risk area: getting the sub-FSM datagram propagation chain correct
  through all nesting levels (fsm_slave → fsm_slave_config → fsm_change → fsm_coe → ...)
