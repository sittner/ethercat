# Parallel Slave Configuration: Analysis and Implementation Plan

**Branch:** `uspace`  
**Directory in scope:** `master/`

---

## 1. Executive Summary

On large EtherCAT networks the startup time is dominated by slave *configuration*:
bringing every slave from its current AL state to the requested state (typically
OP) while writing SDO/SoE parameters, configuring PDO mappings, setting up sync
managers, DC clocks and FMMUs. The current implementation configures slaves
**one at a time** through a single shared `ec_fsm_slave_config_t` instance that
borrows a single datagram and a set of sub-FSMs from the master FSM.

Because each SDO write is a multi-frame mailbox round-trip (one frame to write
the request into the slave's mailbox, one or more frames polling for the response
in the slave's output mailbox) and because the bus is otherwise idle while
waiting, the total startup latency grows roughly linearly with the number of
slaves that have SDO parameters.

The proposed solution re-uses the existing *external datagram ring* and
*slave-FSM execution-list* infrastructure (already in use for runtime SDO/SoE/FoE
requests) to run up to N slave configuration FSMs simultaneously.  Each concurrent
FSM instance owns its own sub-FSMs and datagram so that they do not interfere with
each other.

---

## 2. Current Architecture Analysis

### 2.1 The `ec_fsm_master_t` struct and its single config FSM

**File:** `master/fsm_master.h`, lines 60–92

```c
struct ec_fsm_master {
    ec_master_t *master;          // back-pointer to master
    ec_datagram_t *datagram;      // single shared datagram
    ...
    ec_fsm_coe_t    fsm_coe;      // CoE sub-FSM  (owned)
    ec_fsm_soe_t    fsm_soe;      // SoE sub-FSM  (owned)
    ec_fsm_pdo_t    fsm_pdo;      // PDO sub-FSM  (owned)
    ec_fsm_eoe_t    fsm_eoe;      // EoE sub-FSM  (owned)
    ec_fsm_change_t fsm_change;   // AL-state-change sub-FSM (owned)
    ec_fsm_slave_config_t fsm_slave_config;  // slave config FSM (line 90)
    ec_fsm_slave_scan_t   fsm_slave_scan;    // slave scan FSM
    ec_fsm_sii_t    fsm_sii;
};
```

There is **exactly one** `ec_fsm_slave_config_t` embedded in the master FSM.
It is also shared with the slave-scan FSM (see the `fsm_slave_scan_init` call
in `master/fsm_master.c`, lines 115–116).

Initialization (`master/fsm_master.c`, lines 111–116):

```c
ec_fsm_slave_config_init(&fsm->fsm_slave_config, fsm->datagram,
        &fsm->fsm_change, &fsm->fsm_coe, &fsm->fsm_soe, &fsm->fsm_pdo,
        &fsm->fsm_eoe);
ec_fsm_slave_scan_init(&fsm->fsm_slave_scan, fsm->datagram,
        &fsm->fsm_slave_config, &fsm->fsm_pdo);
```

(lines 114–116 of `master/fsm_master.c`)

### 2.2 `ec_fsm_slave_config_t` borrows all sub-FSMs via pointers

**File:** `master/fsm_slave_config.h`, lines 46–65

```c
struct ec_fsm_slave_config {
    ec_datagram_t   *datagram;    // pointer — NOT owned
    ec_fsm_change_t *fsm_change;  // pointer — NOT owned
    ec_fsm_coe_t    *fsm_coe;     // pointer — NOT owned
    ec_fsm_soe_t    *fsm_soe;     // pointer — NOT owned
    ec_fsm_pdo_t    *fsm_pdo;     // pointer — NOT owned
    ec_fsm_eoe_t    *fsm_eoe;     // pointer — NOT owned
    ec_slave_t      *slave;
    void (*state)(ec_fsm_slave_config_t *);
    unsigned int     retries;
    ec_sdo_request_t *request;
    ec_sdo_request_t  request_copy;
    ec_soe_request_t *soe_request;
    ec_soe_request_t  soe_request_copy;
    ec_time_t         time_start;
    unsigned int      take_time;
    unsigned long     wait_ms;
};
```

All six resource pointers (`datagram`, `fsm_change`, `fsm_coe`, `fsm_soe`,
`fsm_pdo`, `fsm_eoe`) are borrowed from `ec_fsm_master_t`.  This means that
creating a second `ec_fsm_slave_config_t` and pointing it at the *same* resources
would corrupt both state machines.  Parallel operation therefore requires **N
independent copies** of all six resources.

### 2.3 Sequential slave iteration in the master FSM

The master FSM walks slaves one at a time:

**`ec_fsm_master_action_next_slave_state()`** (`master/fsm_master.c`, line 676):  
Increments `fsm->slave++` and issues an `FPRD` datagram to read the next slave's
AL state register (0x0130).

**`ec_fsm_master_action_configure()`** (`master/fsm_master.c`, line 704):  
When a slave needs configuration it:
1. Acquires `master->config_sem` and sets `master->config_busy = 1`.
2. Transitions master FSM state to `ec_fsm_master_state_configure_slave`.
3. Calls `ec_fsm_slave_config_start()` and executes the config FSM immediately.

**`ec_fsm_master_state_configure_slave()`** (`master/fsm_master.c`, line 1031):  
Polls `ec_fsm_slave_config_exec()` each master cycle.  When it returns 0
(finished):
1. Sets `master->config_busy = 0`.
2. Wakes `master->config_queue` (unblocking `ec_master_enter_operation_phase()`).
3. Calls `ec_fsm_master_action_next_slave_state()` to continue scanning.

Only a **single slave** is ever being configured at any moment.

### 2.4 The single-datagram bottleneck

`ec_fsm_master_t::datagram` points to `master->fsm_datagram` (a single
`ec_datagram_t`).  The config FSM and all its sub-FSMs operate on this one
datagram.  While a datagram round-trip is in flight (`QUEUED → SENT → RECEIVED`)
no other command can use that datagram.  The master cycle cannot advance the FSM
until the datagram completes.

### 2.5 `config_busy` / `config_sem` / `config_queue` synchronization

**File:** `master/master.h`, lines 234–237  
**Initialised in:** `master/master.c`, lines 176–178

```c
unsigned int    config_busy;   // 1 = some slave being configured
ec_semaphore_t  config_sem;    // protects config_busy
ec_wait_queue_t config_queue;  // blocks ec_master_enter_operation_phase()
```

`ec_master_enter_operation_phase()` (`master/master.c`, line 621) waits on
`config_queue` until `config_busy` reaches 0.  The current design assumes
**exactly one** configuration can be in progress: `config_busy` is a boolean
(0/1), not a counter.

### 2.6 Existing parallel slave-FSM infrastructure

EtherCAT already supports running multiple *runtime* slave FSMs (SDO
reads/writes, FoE, SoE, EoE) in parallel.  The relevant infrastructure:

| Item | Location | Role |
|------|----------|------|
| `ec_fsm_slave_t` (per-slave) | embedded in `struct ec_slave` (`master/slave.h`, line 227) | runtime request FSM per slave |
| `master->ext_datagram_ring[EC_EXT_RING_SIZE]` | `master/master.h`, line 248; `#define EC_EXT_RING_SIZE 32` (line 102) | pool of 32 datagrams for concurrent FSMs |
| `master->ext_ring_idx_rt` / `ext_ring_idx_fsm` | `master/master.h`, lines 250–252 | ring-buffer indices (RT path and FSM path) |
| `master->fsm_exec_list` | `master/master.h`, line 259 | linked list of currently executing slave FSMs |
| `master->fsm_exec_count` | `master/master.h`, line 260 | current number in execution list; capped at `EC_EXT_RING_SIZE / 2 = 16` |
| `ec_master_exec_slave_fsms()` | `master/master.c`, line 1276 | iterates exec list; advances each FSM with its own datagram |

`ec_master_exec_slave_fsms()` uses the following pattern:

```c
// (1) advance existing executing FSMs
list_for_each_entry_safe(fsm, next, &master->fsm_exec_list, list) {
    datagram = ec_master_get_external_datagram(master); // next free slot
    if (ec_fsm_slave_exec(fsm, datagram)) {
        master->ext_ring_idx_fsm = (master->ext_ring_idx_fsm + 1) % EC_EXT_RING_SIZE;
    } else {
        list_del_init(&fsm->list);
        master->fsm_exec_count--;
    }
}
// (2) admit new FSMs up to the limit
while (master->fsm_exec_count < EC_EXT_RING_SIZE / 2 && ...) {
    if (ec_fsm_slave_is_ready(&master->fsm_slave->fsm)) {
        ec_fsm_slave_exec(&master->fsm_slave->fsm, datagram);
        list_add_tail(&master->fsm_slave->fsm.list, &master->fsm_exec_list);
        master->fsm_exec_count++;
    }
    master->fsm_slave++;
}
```

This pattern is the direct blueprint for the parallel configuration pool.

---

## 3. Why Sequential Configuration Is Slow

### 3.1 Mailbox round-trips dominate

The SDO configuration step (`ec_fsm_slave_config_state_sdo_conf`,
`master/fsm_slave_config.c` ~line 849) iterates `slave->config->sdo_configs`.
For each SDO the CoE mailbox protocol requires at minimum two datagram
round-trips:

1. **Write request**: FPWR into the slave's output mailbox sync manager
   (SM0/SM2).
2. **Poll response**: repeated FPRD of the slave's input mailbox (SM1/SM3)
   until the slave places a response.

A typical complex servo drive may have 50–200 SDO parameters.  At a master cycle
time of 1 ms this is 100–400 ms *per slave* just for SDO configuration, before
the next slave can start.

### 3.2 Wire idle time

While waiting for one slave's mailbox response the EtherCAT bus carries only
that one FPRD/FPWR datagram per cycle.  The bus can carry up to ~1500 bytes of
payload per Ethernet frame, easily accommodating mailbox transactions to several
slaves simultaneously.  This capacity is entirely wasted in the sequential model.

### 3.3 Additional per-slave configuration time

Besides SDO configuration, each slave goes through:

```
START → INIT → CLEAR_FMMU → CLEAR_SYNC → DC_CLEAR_ASSIGN
→ MBOX_SYNC → [ASSIGN_PDI] → BOOT_PREOP → [ASSIGN_ETHERCAT]
→ SDO_CONF → SOE_CONF_PREOP → EOE_IP_PARAM → PDO_CONF
→ WATCHDOG_DIVIDER → WATCHDOG → PDO_SYNC → FMMU
→ DC_CYCLE → DC_START → DC_ASSIGN
→ WAIT_SAFEOP → SAFEOP → SOE_CONF_SAFEOP → OP → END
```

(State-entry points from `master/fsm_slave_config.c`, lines 167–1829.)

Each AL state change (`INIT→PREOP`, `PREOP→SAFEOP`, `SAFEOP→OP`) requires
sending an AL control register write followed by polling the AL status register
until the slave completes the transition.  On a 20-slave bus, even without SDO
parameters, 60 state-change waits execute sequentially.

### 3.4 Linear scaling

Total startup latency scales roughly as `N * T_slave` where `T_slave` is the
per-slave configuration time.  With parallel configuration the dominant term
becomes `max(T_slave_i)` for the slowest slave, plus scheduling overhead.

---

## 4. Proposed Architecture: Parallel Slave Configuration

### 4.1 Design principle

Extend the existing `ext_datagram_ring` + `fsm_exec_list` pattern — already
proven for runtime requests — to cover the *initial configuration* phase.

### 4.2 Self-contained config FSM instances

Each parallel configuration slot requires its own independent set of resources:

| Resource | Current ownership | New ownership |
|----------|-------------------|---------------|
| `ec_datagram_t` | `ec_fsm_master_t::datagram` (single, shared) | one slot from `ext_datagram_ring` per active config |
| `ec_fsm_change_t` | `ec_fsm_master_t::fsm_change` (shared) | per config-slot instance |
| `ec_fsm_coe_t` | `ec_fsm_master_t::fsm_coe` (shared) | per config-slot instance |
| `ec_fsm_soe_t` | `ec_fsm_master_t::fsm_soe` (shared) | per config-slot instance |
| `ec_fsm_pdo_t` | `ec_fsm_master_t::fsm_pdo` (shared) | per config-slot instance |
| `ec_fsm_eoe_t` | `ec_fsm_master_t::fsm_eoe` (shared) | per config-slot instance |
| `ec_fsm_slave_config_t` | `ec_fsm_master_t::fsm_slave_config` (single) | per config-slot instance |

Define a compound *config slot* struct:

```c
typedef struct {
    ec_datagram_t        datagram;
    ec_fsm_change_t      fsm_change;
    ec_fsm_coe_t         fsm_coe;
    ec_fsm_soe_t         fsm_soe;
    ec_fsm_pdo_t         fsm_pdo;
    ec_fsm_eoe_t         fsm_eoe;
    ec_fsm_slave_config_t fsm_slave_config;
    int                  in_use;   // slot currently active
    struct list_head     list;     // link in config_exec_list
} ec_config_slot_t;
```

### 4.3 Config slot pool

Add to `ec_master_t` (or `ec_fsm_master_t`):

```c
#define EC_MAX_PARALLEL_CONFIGS  16   // configurable; ≤ EC_EXT_RING_SIZE / 2

ec_config_slot_t  config_slots[EC_MAX_PARALLEL_CONFIGS];
struct list_head  config_exec_list;  // slots currently executing
unsigned int      config_exec_count;
```

`EC_MAX_PARALLEL_CONFIGS` should default to 16, matching the existing
`EC_EXT_RING_SIZE / 2` limit used by `ec_master_exec_slave_fsms()`.

### 4.4 Master FSM orchestration

The master FSM (or a new `ec_master_exec_config_slots()` helper modelled after
`ec_master_exec_slave_fsms()`) performs each master cycle:

1. **Advance active slots**: iterate `config_exec_list`; for each slot call
   `ec_fsm_slave_config_exec()`; if finished, mark slot as free, decrement
   counter, handle errors, signal that one more slave has completed.

2. **Admit new slaves**: while `config_exec_count < EC_MAX_PARALLEL_CONFIGS`
   and there are slaves waiting, find a free slot, call
   `ec_fsm_slave_config_start()`, add to list.

3. **Completion signal**: when `config_exec_list` empties and no more slaves
   need configuration, signal `config_queue` (see §4.6).

### 4.5 Datagram allocation

Each config slot owns a dedicated `ec_datagram_t` (allocated as part of the
slot struct, not borrowed from `ext_datagram_ring`).  This avoids contention
with the runtime slave FSMs that already use `ext_datagram_ring`.

Alternatively, slots may use `ext_datagram_ring` datagrams if memory is
constrained, but this reduces the slots available for runtime requests during
configuration.

### 4.6 Synchronization changes

Replace the boolean `config_busy` with a counter:

```c
// master/master.h
unsigned int    config_exec_count;   // replaces config_busy (0 = idle)
ec_semaphore_t  config_sem;          // protects config_exec_count
ec_wait_queue_t config_queue;        // signalled when count reaches 0
```

`ec_master_enter_operation_phase()` waits on `config_queue` until
`config_exec_count == 0`.  The existing wait-queue idiom (`ec_wq_wait_interruptible`)
is unchanged; only the condition predicate changes from `!config_busy` to
`config_exec_count == 0`.

---

## 5. Detailed Implementation Plan

### Phase 1 — Make `ec_fsm_slave_config_t` self-contained

**Goal:** eliminate the six borrowed-pointer fields; the struct owns its
sub-FSMs directly.

**Files:** `master/fsm_slave_config.h`, `master/fsm_slave_config.c`

Changes:
- Change the six `*` pointer members to value members:
  ```c
  ec_datagram_t    datagram;   // owned, not a pointer
  ec_fsm_change_t  fsm_change;
  ec_fsm_coe_t     fsm_coe;
  ec_fsm_soe_t     fsm_soe;
  ec_fsm_pdo_t     fsm_pdo;
  ec_fsm_eoe_t     fsm_eoe;
  ```
- Update `ec_fsm_slave_config_init()` signature: remove the six pointer
  parameters; instead call `ec_fsm_change_init()`, `ec_fsm_coe_init()`,
  `ec_fsm_soe_init()`, `ec_fsm_pdo_init()`, `ec_fsm_eoe_init()` internally.
- Update `ec_fsm_slave_config_clear()` to call the corresponding `_clear()`
  functions on the now-owned sub-FSMs.
- Replace all `fsm->fsm_coe` pointer dereferences with `&fsm->fsm_coe`
  address-of expressions within `fsm_slave_config.c`.
- Update callers in `master/fsm_master.c` and `master/fsm_slave_scan.c`:
  remove the sub-FSM arguments from `ec_fsm_slave_config_init()` calls.

**Note:** `ec_fsm_slave_scan_t` also receives a pointer to the shared
`ec_fsm_slave_config_t`.  After Phase 1, the scan FSM should have its
own embedded `ec_fsm_slave_config_t` (or continue borrowing a pointer —
the scan and config FSMs are never active simultaneously so sharing is
safe, but embedding removes the dependency).

### Phase 2 — Create the config slot pool

**Files:** `master/master.h`, `master/master.c`

Changes:
- Define `ec_config_slot_t` (see §4.2).
- Define `EC_MAX_PARALLEL_CONFIGS` (suggest `master/master.h`, near
  `EC_EXT_RING_SIZE`).
- Add `config_slots[EC_MAX_PARALLEL_CONFIGS]`, `config_exec_list`,
  `config_exec_count` to `ec_master_t`.
- In `ec_master_init()` (`master/master.c` ~line 176):
  - initialize each slot's sub-FSMs (call `ec_fsm_slave_config_init()` after
    Phase 1 changes).
  - `INIT_LIST_HEAD(&master->config_exec_list)`.
  - `master->config_exec_count = 0`.
- In `ec_master_clear()`: call `ec_fsm_slave_config_clear()` on every slot.

### Phase 3 — Modify master FSM dispatch

**Files:** `master/fsm_master.c`, `master/fsm_master.h`, `master/master.c`

Changes:
- Add `ec_master_exec_config_slots()` helper to `master/master.c` (or
  `master/fsm_master.c`):
  ```
  - iterate config_exec_list, call ec_fsm_slave_config_exec() per slot
  - on completion: mark slot free, decrement config_exec_count
  - while config_exec_count < EC_MAX_PARALLEL_CONFIGS and pending slaves exist:
      find a free slot, call ec_fsm_slave_config_start(), add to list,
      increment config_exec_count
  - if list empty and no more pending slaves: wake config_queue
  ```
- Modify `ec_fsm_master_action_configure()` (line 704):
  - Instead of immediately starting a single config and switching to
    `ec_fsm_master_state_configure_slave`, enqueue the slave in a
    "pending config" list and let `ec_master_exec_config_slots()` dispatch it.
- Replace `ec_fsm_master_state_configure_slave()` (line 1031) with calls to
  `ec_master_exec_config_slots()` from the master's main execution loop.
- Call `ec_master_exec_config_slots()` from `ec_master_exec()` /
  `ec_master_send_datagrams()` at the same point where
  `ec_master_exec_slave_fsms()` is called (see `master/master.c` lines 1395,
  1446).

### Phase 4 — Adapt synchronization

**Files:** `master/master.h`, `master/master.c`, `master/fsm_master.c`

Changes:
- Replace `config_busy` (boolean) with `config_exec_count` (unsigned int) or
  keep `config_busy` as an alias `(config_exec_count > 0)`.
- Update `ec_master_enter_operation_phase()` wait condition:
  ```c
  // Before:
  ret = ec_wq_wait_interruptible(master->config_queue, !master->config_busy);
  // After:
  ret = ec_wq_wait_interruptible(master->config_queue,
          master->config_exec_count == 0);
  ```
- Update `ec_sem_down/up(config_sem)` call sites to protect the new counter
  instead of the boolean.
- Remove the `master->config_busy = 1` / `= 0` assignments from
  `ec_fsm_master_action_configure()` and `ec_fsm_master_state_configure_slave()`;
  replace with `config_exec_count` increment/decrement under `config_sem`.

### Phase 5 — Testing

- **Unit:** Write targeted tests that instantiate two `ec_config_slot_t`
  instances with mock slaves and verify they advance independently.
- **Integration (small topology):** 4-slave ring; verify all four reach OP
  in parallel and the bus state is consistent.
- **Stress (large topology):** 32-slave bus with complex SDO configs; measure
  startup time before and after.
- **Regression:** run the full existing test suite after each phase to detect
  regressions in scan, runtime SDO, FoE, SoE, EoE paths.

---

## 6. Risks and Side Effects

### 6.1 Datagram queue pressure

Each active config slot adds at least one datagram per cycle to the EtherCAT
frame.  With 16 parallel slots, up to 16 additional datagrams may be queued
simultaneously.  The master already enforces `max_queue_size`; verify that
this limit is not exceeded.  Reduce `EC_MAX_PARALLEL_CONFIGS` if frame
overflow occurs.

### 6.2 EtherCAT frame size limit

Maximum Ethernet payload is ~1496 bytes after the EtherCAT header.  Each
addressed datagram (FPRD/FPWR) has a 10-byte header plus data.  Typical
mailbox transactions carry 6–128 bytes of data.  For 16 parallel mailbox
polls the datagrams fit comfortably within a single frame; however monitor
actual frame sizes in hardware tests.

### 6.3 DC system time compensation

`ec_fsm_master_enter_write_system_times()` runs before configuration and
operates on all slaves sequentially.  This function should remain unchanged;
parallel config starts only after DC compensation completes.

### 6.4 `config_changed` during parallel configuration

The current code checks `master->config_changed` at the top of
`ec_fsm_master_action_configure()` and aborts the current slave scan if set
(lines 713–723 of `fsm_master.c`).  With multiple slaves in flight, a
`config_changed` event must:
1. Stop admitting new slaves to the config pool.
2. Wait for already-active slots to complete (or abort them).
3. Re-start the DC compensation pass and then re-scan from slave 0.

The abort path is complex; a safe first implementation can simply wait for
all active slots to finish before reacting to `config_changed`.

### 6.5 Error handling

If one slave's configuration fails (`ec_fsm_slave_config_success()` returns
false) the current code only logs a TODO comment (line 1048 of `fsm_master.c`).
With parallel config, a failing slave must not prevent other slaves from
completing.  The slot should be marked failed and freed normally; the error
is associated with that slave, not the entire configuration pass.

### 6.6 Memory usage

Each `ec_config_slot_t` embeds:
- `ec_datagram_t` (~100 bytes + data buffer)
- `ec_fsm_change_t`, `ec_fsm_coe_t`, `ec_fsm_soe_t`, `ec_fsm_pdo_t`,
  `ec_fsm_eoe_t`, `ec_fsm_slave_config_t` (a few hundred bytes total)

For 16 slots the additional static memory is on the order of a few kilobytes.
Acceptable for both kernel and userspace builds.

### 6.7 Contention with runtime slave FSMs for `ext_datagram_ring`

If config slots use `ext_datagram_ring` datagrams, they compete with
`ec_master_exec_slave_fsms()` for ring slots.  The preferred approach (§4.5)
is to give each config slot a *dedicated* `ec_datagram_t` so that the ring
remains fully available for runtime requests.  The dedicated datagrams must
be queued through the same `ec_master_queue_datagram()` path so they are
included in sent frames.

---

## 7. Files to Modify

| File | Changes |
|------|---------|
| `master/fsm_slave_config.h` | Change pointer fields to value fields; update `ec_fsm_slave_config_init()` signature |
| `master/fsm_slave_config.c` | Update init/clear; replace `fsm->fsm_coe` with `&fsm->fsm_coe` etc. throughout |
| `master/fsm_slave_scan.h` | Optionally embed `ec_fsm_slave_config_t` instead of holding a pointer |
| `master/fsm_slave_scan.c` | Update init/clear and all `fsm_slave_config` usages |
| `master/fsm_master.h` | Add `config_slots` array, `config_exec_list`, `config_exec_count`; remove or demote single `fsm_slave_config` |
| `master/fsm_master.c` | Rewrite `action_configure` and `state_configure_slave`; add `ec_master_exec_config_slots()` |
| `master/master.h` | Add `ec_config_slot_t` typedef; add `EC_MAX_PARALLEL_CONFIGS`; replace `config_busy` with `config_exec_count` |
| `master/master.c` | Update `ec_master_init()` / `ec_master_clear()`; update `ec_master_enter_operation_phase()`; call `ec_master_exec_config_slots()` from main loop |

---

## 8. Open Questions

1. **Where to define `ec_config_slot_t`?** In `master/master.h` alongside the
   master struct, or in a new `master/fsm_slave_config.h` extension?  The
   latter keeps configuration-related types together.

2. **Should `EC_MAX_PARALLEL_CONFIGS` be a compile-time constant or a
   runtime-configurable parameter** (e.g. sysfs/ioctl attribute)?  A runtime
   parameter allows tuning without recompile.

3. **Shared scan FSM**: `ec_fsm_slave_scan_t` currently borrows the single
   shared `ec_fsm_slave_config_t`.  Since scanning and parallel config do not
   overlap (scanning finishes before configuration begins), the existing single
   instance in `ec_fsm_master_t` can continue to be used for scanning.  Phase 1
   must not break the scan FSM's use of the config FSM.

4. **Slot datagram allocation strategy**: dedicated static `ec_datagram_t` per
   slot (simpler, more memory) vs. sharing `ext_datagram_ring` (less memory,
   more synchronization).  Recommend dedicated datagrams for Phase 2 and
   revisit if memory is constrained.

5. **DC interaction during parallel WAIT_SAFEOP**: the DC sync wait state
   (`ec_fsm_slave_config_state_dc_sync_check`) waits up to
   `EC_DC_SYNC_WAIT_MS` (5000 ms) for synchronization.  With N slaves in this
   state simultaneously the wait periods overlap, which is desirable.  Verify
   that the master's app_time accounting is not disturbed by multiple concurrent
   accesses.

6. **Interaction with `master->fsm_slave` pointer**: `ec_master_exec_slave_fsms()`
   advances `master->fsm_slave` through the slave array each call.  The new
   config dispatch loop must iterate the same slave array but use a separate
   pointer (or a pending-queue) to avoid collision.

7. **Testing infrastructure**: there are no automated unit tests for the FSM
   layer in the current repository.  Should the parallel config work include
   adding a test harness, or should it rely on integration tests only?
