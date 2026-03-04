# Parallel Slave Configuration: Analysis and Implementation Plan

**Branch:** `psc`  
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

Each parallel configuration slot requires its own independent set of sub-FSMs.
Datagrams are obtained from `ext_datagram_ring` at execution time (see §4.5),
so no dedicated datagram is stored in the slot.

| Resource | Current ownership | New ownership |
|----------|-------------------|---------------|
| `ec_datagram_t` | `ec_fsm_master_t::datagram` (single, shared) | borrowed from `ext_datagram_ring` per master cycle — not stored in slot |
| `ec_fsm_change_t` | `ec_fsm_master_t::fsm_change` (shared) | per config-slot instance |
| `ec_fsm_coe_t` | `ec_fsm_master_t::fsm_coe` (shared) | per config-slot instance |
| `ec_fsm_soe_t` | `ec_fsm_master_t::fsm_soe` (shared) | per config-slot instance |
| `ec_fsm_pdo_t` | `ec_fsm_master_t::fsm_pdo` (shared) | per config-slot instance |
| `ec_fsm_eoe_t` | `ec_fsm_master_t::fsm_eoe` (shared) | per config-slot instance |
| `ec_fsm_slave_config_t` | `ec_fsm_master_t::fsm_slave_config` (single) | per config-slot instance |

Define a compound *config slot* struct (defined in `master/fsm_slave_config.h`
to keep configuration-related types together; `master.h` already includes
`fsm_slave_config.h` so the type is available where the pool array is declared):

```c
typedef struct {
    ec_fsm_change_t       fsm_change;
    ec_fsm_coe_t          fsm_coe;
    ec_fsm_soe_t          fsm_soe;
    ec_fsm_pdo_t          fsm_pdo;
    ec_fsm_eoe_t          fsm_eoe;
    ec_fsm_slave_config_t fsm_slave_config;
    int                   in_use;   // slot currently active
    struct list_head      list;     // link in config_exec_list
} ec_config_slot_t;
```

### 4.3 Config slot pool

Add to `ec_master_t` (or `ec_fsm_master_t`):

```c
#define EC_MAX_PARALLEL_CONFIGS  8   // default; overridable via ./configure
                                     // (--with-max-parallel-configs=N passes
                                     //  -DEC_MAX_PARALLEL_CONFIGS=N in CFLAGS)

ec_config_slot_t  config_slots[EC_MAX_PARALLEL_CONFIGS];
struct list_head  config_exec_list;  // slots currently executing
unsigned int      config_exec_count;
```

`EC_MAX_PARALLEL_CONFIGS` defaults to 8 and can be overridden at build time
via the `./configure` option `--with-max-parallel-configs=N`, which injects
`-DEC_MAX_PARALLEL_CONFIGS=N` into `CFLAGS`.  There is no runtime
configurability.

### 4.4 Master FSM orchestration

A new `ec_master_exec_config_slots()` helper (modelled after
`ec_master_exec_slave_fsms()`) performs each master cycle:

1. **Advance active slots**: iterate `config_exec_list`; for each slot call
   `ec_fsm_slave_config_exec()`; if finished, mark slot as free, decrement
   counter, handle errors, signal that one more slave has completed.

2. **Admit new slaves**: while `config_exec_count < EC_MAX_PARALLEL_CONFIGS`
   and there are slaves waiting in `config_pending_list` (a `struct list_head`
   in `ec_master_t`), dequeue the next slave, find a free slot, call
   `ec_fsm_slave_config_start()`, add to `config_exec_list`.

3. **Completion signal**: when `config_exec_list` empties and `config_pending_list`
   is also empty, signal `config_queue` (see §4.6).

When the master FSM discovers a slave needs configuration
(`ec_fsm_master_action_configure()`), it appends the slave to
`config_pending_list` rather than immediately starting the config FSM.
`ec_master_exec_config_slots()` dequeues from this list when a free slot is
available, keeping `master->fsm_slave` exclusively for the runtime FSM
round-robin in `ec_master_exec_slave_fsms()`.

The execution order in the master thread is:

```c
ec_fsm_master_exec(&master->fsm);          // master FSM (scan, etc.)
ec_master_exec_config_slots(master);        // config FSMs first — priority
ec_master_exec_slave_fsms(master);          // runtime FSMs get remaining ring slots
```

Config FSMs execute before runtime FSMs so they receive priority access to
`ext_datagram_ring` slots.

### 4.5 Datagram allocation

Config FSMs share `ext_datagram_ring` — they do **not** use dedicated
datagrams.  Each call to `ec_fsm_slave_config_exec()` obtains a datagram from
the ring via `ec_master_get_external_datagram()`, the same function used by
runtime slave FSMs.

Config FSMs are called first in the master thread execution order (see §4.4),
so they receive priority access to ring slots.  Runtime FSMs consume whatever
ring capacity remains.

This creates a natural three-layer throttle:

1. **Ring capacity** — `EC_EXT_RING_SIZE = 32` total ring slots.
2. **FSM exec cap** — `EC_MAX_PARALLEL_CONFIGS = 8` config FSMs admitted at once.
3. **Frame size limit** — `max_queue_size` check in the injection path defers
   datagrams that would overflow the current Ethernet frame to the next cycle.

Because `ec_config_slot_t` does not contain a dedicated `ec_datagram_t` member,
no extra static memory is required beyond the sub-FSM instances.

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
- Change the five sub-FSM `*` pointer members to value members (the `datagram`
  field remains a pointer — it is borrowed from `ext_datagram_ring` at execution
  time per D4):
  ```c
  ec_datagram_t   *datagram;   // pointer — borrowed from ext_datagram_ring
  ec_fsm_change_t  fsm_change; // owned
  ec_fsm_coe_t     fsm_coe;    // owned
  ec_fsm_soe_t     fsm_soe;    // owned
  ec_fsm_pdo_t     fsm_pdo;    // owned
  ec_fsm_eoe_t     fsm_eoe;    // owned
  ```
- Update `ec_fsm_slave_config_init()` signature: remove the five sub-FSM
  pointer parameters (`fsm_change`, `fsm_coe`, `fsm_soe`, `fsm_pdo`,
  `fsm_eoe`); keep the `datagram` pointer parameter (the datagram is
  borrowed, not owned).  Internally call `ec_fsm_change_init()`,
  `ec_fsm_coe_init()`, `ec_fsm_soe_init()`, `ec_fsm_pdo_init()`,
  `ec_fsm_eoe_init()` to initialize the now-owned sub-FSMs.
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

**Files:** `master/master.h`, `master/master.c`, `configure.ac`

Changes:
- Define `ec_config_slot_t` in `master/fsm_slave_config.h` (see §4.2); no
  `ec_datagram_t` member — slots borrow datagrams from the ring.
- Define `EC_MAX_PARALLEL_CONFIGS` (default 8) near `EC_EXT_RING_SIZE` in
  `master/master.h`.  Add `--with-max-parallel-configs=N` option to
  `configure.ac` so users can override it without editing source.
- Add `config_slots[EC_MAX_PARALLEL_CONFIGS]`, `config_exec_list`,
  `config_exec_count`, and `config_pending_list` to `ec_master_t`.
- In `ec_master_init()` (`master/master.c` ~line 176):
  - initialize each slot's sub-FSMs (call `ec_fsm_slave_config_init()` after
    Phase 1 changes).
  - `INIT_LIST_HEAD(&master->config_exec_list)`.
  - `INIT_LIST_HEAD(&master->config_pending_list)`.
  - `master->config_exec_count = 0`.
- In `ec_master_clear()`: call `ec_fsm_slave_config_clear()` on every slot.

### Phase 3 — Modify master FSM dispatch

**Files:** `master/fsm_master.c`, `master/fsm_master.h`, `master/master.c`

Changes:
- Add `ec_master_exec_config_slots()` helper to `master/master.c` (or
  `master/fsm_master.c`):
  ```
  - iterate config_exec_list, call ec_fsm_slave_config_exec() per slot
    (each call obtains a datagram from ext_datagram_ring via
     ec_master_get_external_datagram())
  - on completion: mark slot free, decrement config_exec_count
  - while config_exec_count < EC_MAX_PARALLEL_CONFIGS and config_pending_list
    is non-empty: dequeue next slave, find a free slot, call
    ec_fsm_slave_config_start(), add to config_exec_list,
    increment config_exec_count
  - if config_exec_list empty and config_pending_list empty: wake config_queue
  ```
- Modify `ec_fsm_master_action_configure()` (line 704):
  - Instead of immediately starting a single config and switching to
    `ec_fsm_master_state_configure_slave`, append the slave to
    `config_pending_list` and let `ec_master_exec_config_slots()` dispatch it.
- Replace `ec_fsm_master_state_configure_slave()` (line 1031) with calls to
  `ec_master_exec_config_slots()` from the master's main execution loop.
- Call `ec_master_exec_config_slots()` from `ec_master_exec()` /
  `ec_master_send_datagrams()` **before** `ec_master_exec_slave_fsms()` so
  config FSMs have priority over runtime FSMs for `ext_datagram_ring` slots
  (see `master/master.c` lines 1395, 1446).

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

- **Integration (small topology):** 4-slave ring (real hardware or
  bus-simulator); verify all four reach OP in parallel and the bus state is
  consistent.
- **Stress (large topology):** 32-slave bus with complex SDO configs; measure
  startup time before and after.
- **Regression:** run the full existing test suite after each phase to detect
  regressions in scan, runtime SDO, FoE, SoE, EoE paths.
- **Unit (if needed):** targeted FSM-layer unit tests should be added only if
  debugging requires them (see D7).  A full FSM test harness is a separate
  effort outside the scope of this feature.

---

## 6. Risks and Side Effects

### 6.1 Datagram queue pressure

Each active config slot adds at least one datagram per cycle to the EtherCAT
frame.  With 8 parallel slots, up to 8 additional datagrams may be queued
simultaneously.  The master already enforces `max_queue_size`; verify that
this limit is not exceeded.  Reduce `EC_MAX_PARALLEL_CONFIGS` if frame
overflow occurs.

### 6.2 EtherCAT frame size limit

Maximum Ethernet payload is ~1496 bytes after the EtherCAT header.  Each
addressed datagram (FPRD/FPWR) has a 10-byte header plus data.  Typical
mailbox transactions carry 6–128 bytes of data.  For 8 parallel mailbox
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
- `ec_fsm_change_t`, `ec_fsm_coe_t`, `ec_fsm_soe_t`, `ec_fsm_pdo_t`,
  `ec_fsm_eoe_t`, `ec_fsm_slave_config_t` (a few hundred bytes total)

No dedicated `ec_datagram_t` is stored in the slot; datagrams are borrowed
from `ext_datagram_ring` at execution time.  For 8 slots the additional
static memory is on the order of a few kilobytes.  Acceptable for both
kernel and userspace builds.

### 6.7 Interaction with runtime slave FSMs for `ext_datagram_ring`

Config FSMs and runtime slave FSMs share `ext_datagram_ring` by design.  This
is safe because config FSMs are called first in the master thread execution
order (see §4.4), giving them priority access to ring slots.  Runtime FSMs
consume the remaining capacity.  The three-layer throttle (ring capacity,
`EC_MAX_PARALLEL_CONFIGS` cap, frame-size limit) prevents ring exhaustion
during the configuration phase.

---

## 7. Files to Modify

| File | Changes |
|------|---------|
| `master/fsm_slave_config.h` | Define `ec_config_slot_t`; change pointer fields to value fields; update `ec_fsm_slave_config_init()` signature |
| `master/fsm_slave_config.c` | Update init/clear; replace `fsm->fsm_coe` with `&fsm->fsm_coe` etc. throughout |
| `master/fsm_slave_scan.h` | Optionally embed `ec_fsm_slave_config_t` instead of holding a pointer |
| `master/fsm_slave_scan.c` | Update init/clear and all `fsm_slave_config` usages |
| `master/fsm_master.h` | Add `config_slots` array, `config_exec_list`, `config_exec_count`; remove or demote single `fsm_slave_config` |
| `master/fsm_master.c` | Rewrite `action_configure` and `state_configure_slave`; add `ec_master_exec_config_slots()` |
| `master/master.h` | Add `EC_MAX_PARALLEL_CONFIGS`; add `config_pending_list`; replace `config_busy` with `config_exec_count` |
| `master/master.c` | Update `ec_master_init()` / `ec_master_clear()`; update `ec_master_enter_operation_phase()`; call `ec_master_exec_config_slots()` from main loop before `ec_master_exec_slave_fsms()` |
| `configure.ac` | Add `--with-max-parallel-configs=N` option; pass `-DEC_MAX_PARALLEL_CONFIGS=N` via `CFLAGS` |

---

## 8. Resolved Decisions

The following decisions resolve the questions that were open during the initial
design phase.

### D1: Where to define `ec_config_slot_t`

**Decision:** Define in `master/fsm_slave_config.h`.  This keeps all
configuration-related types in one file.  `master.h` already includes
`fsm_slave_config.h`, so `ec_config_slot_t` is visible wherever the pool
array is declared in `ec_master_t`.

### D2: `EC_MAX_PARALLEL_CONFIGS` — compile-time or runtime-configurable

**Decision:** Compile-time `#define` with a default value of **8**.  Users can
override it at build time via the `./configure` option
`--with-max-parallel-configs=N`, which injects `-DEC_MAX_PARALLEL_CONFIGS=N`
into `CFLAGS`.  No runtime configurability is provided.

### D3: Shared scan FSM

**Decision:** Keep pointer sharing.  `ec_fsm_slave_scan_t` continues to borrow
a pointer to the single `ec_fsm_slave_config_t` in `ec_fsm_master_t`.
Scanning and configuration never overlap, so sharing is safe.  Phase 1 only
needs to update the `ec_fsm_slave_config_init()` call site for the new
signature; no structural change to the scan FSM is required.

### D4: Slot datagram allocation strategy

**Decision:** Share `ext_datagram_ring` — **do not** use dedicated datagrams
per slot.  Config FSMs obtain datagrams from the ring via
`ec_master_get_external_datagram()`, the same mechanism used by runtime slave
FSMs.  Config FSMs are called first (priority) in the master thread execution
order; runtime FSMs receive the remaining ring capacity.

The `ec_config_slot_t` struct does **not** contain a dedicated `ec_datagram_t`
member.  It owns only the sub-FSMs:

```c
typedef struct {
    ec_fsm_change_t       fsm_change;
    ec_fsm_coe_t          fsm_coe;
    ec_fsm_soe_t          fsm_soe;
    ec_fsm_pdo_t          fsm_pdo;
    ec_fsm_eoe_t          fsm_eoe;
    ec_fsm_slave_config_t fsm_slave_config;
    int                   in_use;
    struct list_head      list;
} ec_config_slot_t;
```

The resulting three-layer throttle is:
1. Ring capacity (`EC_EXT_RING_SIZE` = 32 total slots).
2. FSM exec cap (`EC_MAX_PARALLEL_CONFIGS` = 8 for config).
3. Frame size limit (`max_queue_size` check defers datagrams that do not fit).

### D5: DC interaction during parallel WAIT_SAFEOP

**Decision:** Safe, no special handling required.  The DC-related config states
(`ec_fsm_slave_config_state_dc_sync_check`, `ec_fsm_slave_config_state_dc_start`,
etc.) only **read** `master->app_time` and `master->dc_ref_time`.  These values
are written exclusively by the application's RT thread
(`ecrt_master_application_time()`), not by the config FSM.  Multiple config
FSMs reading these fields concurrently causes no race condition.  Overlapping
DC sync wait periods are a net win: instead of N × 5 000 ms worst case, the
total wait is max(5 000 ms) across all slaves.

### D6: Interaction with `master->fsm_slave` pointer

**Decision:** Use a pending queue.  When the master FSM discovers a slave needs
configuration (`ec_fsm_master_action_configure()`), it appends the slave to a
`config_pending_list` (`struct list_head` in `ec_master_t`).
`ec_master_exec_config_slots()` dequeues slaves from this list whenever a free
slot is available.  `master->fsm_slave` remains exclusively used by
`ec_master_exec_slave_fsms()` for the runtime FSM round-robin; no collision
occurs.

### D7: Testing infrastructure

**Decision:** Start with integration tests only (real hardware or bus-simulator
topologies: 4-slave, 32-slave).  Targeted FSM-layer unit tests should be added
only if debugging requires them.  A full FSM test harness is a separate effort
outside the scope of this feature.
