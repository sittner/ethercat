# Parallel Slave Configuration Implementation Guide

## Overview

This document describes how to implement **parallel slave configuration** for the IgH EtherCAT Master (stable-1.6) with **minimal changes** to upstream code. This is based on analysis of the `features/parallel-slave` patches from the uecasm patchset, adapted for the modern stable-1.6 codebase.

### Goal

Reduce slave configuration time on large networks by configuring multiple slaves simultaneously instead of sequentially.

**Expected Performance Improvement:**
- Before: ~80 seconds for 100 slaves (sequential, one at a time)
- After: ~20 seconds for 100 slaves (parallel, 16 at a time)
- **4x speedup**

### Design Principle

The key insight is that slave configuration (PREOP → SAFEOP → OP transitions) is largely independent between slaves. By allowing multiple `fsm_slave_config` state machines to run concurrently using separate datagrams, we can include configuration commands for multiple slaves in the same Ethernet frame.

---

## Architecture Changes

### Current Architecture (Sequential)

```
fsm_master
    │
    ├── Scan slave 0
    ├── Scan slave 1
    ├── ... (all scans complete)
    │
    ├── Configure slave 0 ──────► fsm_slave_config (embedded datagram)
    │   └── Wait for completion
    ├── Configure slave 1 ──────► fsm_slave_config (embedded datagram)
    │   └── Wait for completion
    └── ... (sequential)
```

### Target Architecture (Parallel)

```
fsm_master                           fsm_slave (per slave)
    │                                     │
    ├── Scan all slaves                   │
    │                                     │
    ├── Request config for slave 0 ──────►├── fsm_slave_config (external datagram)
    ├── Request config for slave 1 ──────►├── fsm_slave_config (external datagram)
    ├── ... (up to EC_EXT_RING_SIZE/2)   │
    │                                     │
    └── ec_master_exec_slave_fsms() ─────►└── Execute all in parallel
        (already exists, reuse it)
```

---

## Implementation Steps

### Step 1: Modify `fsm_slave_config` to Use External Datagram

**Files:** `master/fsm_slave_config.h`, `master/fsm_slave_config.c`

#### 1.1 Update Header (`fsm_slave_config.h`)

```c
// BEFORE: Embedded datagram
struct ec_fsm_slave_config {
    ec_datagram_t datagram;           // Embedded datagram
    ec_fsm_change_t fsm_change;
    ec_fsm_coe_t fsm_coe;
    ec_fsm_pdo_t fsm_pdo;
    // ...
};

// AFTER: External datagram pointer
struct ec_fsm_slave_config {
    ec_datagram_t *datagram;          // Pointer to external datagram
    ec_fsm_change_t *fsm_change;      // Pointer to external FSM
    ec_fsm_coe_t *fsm_coe;            // Pointer to external FSM
    ec_fsm_pdo_t *fsm_pdo;            // Pointer to external FSM
    // ...
};

// Update function signatures
void ec_fsm_slave_config_init(
    ec_fsm_slave_config_t *fsm,
    ec_datagram_t *datagram,          // Now required parameter
    ec_fsm_change_t *fsm_change,
    ec_fsm_coe_t *fsm_coe,
    ec_fsm_pdo_t *fsm_pdo
);
```

#### 1.2 Update Implementation (`fsm_slave_config.c`)

```c
void ec_fsm_slave_config_init(
    ec_fsm_slave_config_t *fsm,
    ec_datagram_t *datagram,
    ec_fsm_change_t *fsm_change,
    ec_fsm_coe_t *fsm_coe,
    ec_fsm_pdo_t *fsm_pdo
)
{
    fsm->datagram = datagram;
    fsm->fsm_change = fsm_change;
    fsm->fsm_coe = fsm_coe;
    fsm->fsm_pdo = fsm_pdo;
    fsm->state = NULL;
    fsm->slave = NULL;
    // ... other initialization
}

// Update all state functions to use fsm->datagram instead of &fsm->datagram
// Example:
void ec_fsm_slave_config_state_start(ec_fsm_slave_config_t *fsm)
{
    ec_datagram_t *datagram = fsm->datagram;  // Use pointer
    ec_slave_t *slave = fsm->slave;
    
    // ... rest of function unchanged, just use 'datagram' variable
}
```

---

### Step 2: Add Configuration State Machine to `fsm_slave`

**Files:** `master/fsm_slave.h`, `master/fsm_slave.c`

#### 2.1 Update Header (`fsm_slave.h`)

```c
#include "fsm_slave_config.h"
#include "fsm_change.h"
#include "fsm_pdo.h"

struct ec_fsm_slave {
    ec_slave_t *slave;
    struct list_head list;
    
    void (*state)(ec_fsm_slave_t *, ec_datagram_t *);
    ec_datagram_t *datagram;
    
    // Existing request handling
    ec_sdo_request_t *sdo_request;
    ec_reg_request_t *reg_request;
    ec_foe_request_t *foe_request;
    ec_soe_request_t *soe_request;
    
    // ADD: Configuration state machine and sub-FSMs
    ec_fsm_slave_config_t fsm_slave_config;
    ec_fsm_change_t fsm_change;
    ec_fsm_coe_t fsm_coe_config;      // Separate from request handling
    ec_fsm_pdo_t fsm_pdo;
    
    // ADD: Configuration request flag
    unsigned int config_requested:1;
    unsigned int config_running:1;
    
    // Existing sub-state machines for requests
    ec_fsm_coe_t fsm_coe;
    ec_fsm_foe_t fsm_foe;
    ec_fsm_soe_t fsm_soe;
#ifdef EC_EOE
    ec_fsm_eoe_t fsm_eoe;
#endif
};
```

#### 2.2 Update Implementation (`fsm_slave.c`)

```c
void ec_fsm_slave_init(
    ec_fsm_slave_t *fsm,
    ec_slave_t *slave
)
{
    fsm->slave = slave;
    INIT_LIST_HEAD(&fsm->list);
    
    fsm->state = ec_fsm_slave_state_idle;
    fsm->datagram = NULL;
    fsm->sdo_request = NULL;
    fsm->reg_request = NULL;
    fsm->foe_request = NULL;
    fsm->soe_request = NULL;
    
    // ADD: Initialize configuration flags
    fsm->config_requested = 0;
    fsm->config_running = 0;
    
    // ADD: Initialize configuration sub-FSMs
    ec_fsm_change_init(&fsm->fsm_change);
    ec_fsm_coe_init(&fsm->fsm_coe_config);
    ec_fsm_pdo_init(&fsm->fsm_pdo, &fsm->fsm_coe_config);
    ec_fsm_slave_config_init(&fsm->fsm_slave_config, NULL,
                              &fsm->fsm_change, &fsm->fsm_coe_config,
                              &fsm->fsm_pdo);
    
    // Existing initialization
    ec_fsm_coe_init(&fsm->fsm_coe);
    ec_fsm_foe_init(&fsm->fsm_foe);
    ec_fsm_soe_init(&fsm->fsm_soe);
#ifdef EC_EOE
    ec_fsm_eoe_init(&fsm->fsm_eoe);
#endif
}

void ec_fsm_slave_clear(ec_fsm_slave_t *fsm)
{
    // ADD: Clear configuration sub-FSMs
    ec_fsm_slave_config_clear(&fsm->fsm_slave_config);
    ec_fsm_change_clear(&fsm->fsm_change);
    ec_fsm_coe_clear(&fsm->fsm_coe_config);
    ec_fsm_pdo_clear(&fsm->fsm_pdo);
    
    // Existing cleanup
    ec_fsm_coe_clear(&fsm->fsm_coe);
    ec_fsm_foe_clear(&fsm->fsm_foe);
    ec_fsm_soe_clear(&fsm->fsm_soe);
#ifdef EC_EOE
    ec_fsm_eoe_clear(&fsm->fsm_eoe);
#endif
}

// ADD: New function to request configuration
void ec_fsm_slave_request_config(ec_fsm_slave_t *fsm)
{
    fsm->config_requested = 1;
}

// ADD: Check if configuration is complete
int ec_fsm_slave_config_success(const ec_fsm_slave_t *fsm)
{
    return !fsm->config_running && !fsm->config_requested;
}

// MODIFY: Update exec function to handle configuration
int ec_fsm_slave_exec(
    ec_fsm_slave_t *fsm,
    ec_datagram_t *datagram
)
{
    int datagram_used = 0;
    
    // ADD: Handle configuration if requested
    if (fsm->config_requested && !fsm->config_running) {
        // Start configuration
        fsm->config_requested = 0;
        fsm->config_running = 1;
        fsm->fsm_slave_config.datagram = datagram;
        ec_fsm_slave_config_start(&fsm->fsm_slave_config, fsm->slave);
    }
    
    if (fsm->config_running) {
        fsm->fsm_slave_config.datagram = datagram;
        if (ec_fsm_slave_config_exec(&fsm->fsm_slave_config)) {
            datagram_used = 1;
        }
        if (!ec_fsm_slave_config_running(&fsm->fsm_slave_config)) {
            fsm->config_running = 0;
            // Configuration complete
            EC_SLAVE_DBG(fsm->slave, 1, "Configuration finished.\n");
        }
        if (datagram_used) {
            fsm->datagram = datagram;
            return 1;
        }
    }
    
    // Existing state machine execution for requests
    fsm->state(fsm, datagram);
    
    datagram_used = fsm->state != ec_fsm_slave_state_idle &&
        fsm->state != ec_fsm_slave_state_ready;
    
    if (datagram_used) {
        fsm->datagram = datagram;
    } else {
        fsm->datagram = NULL;
    }
    
    return datagram_used;
}
```

---

### Step 3: Modify `fsm_master` to Delegate Configuration

**Files:** `master/fsm_master.c`, `master/fsm_master.h`

#### 3.1 Update State Machine Logic

```c
// In ec_fsm_master_state_scan_slave() or similar, after scanning completes:

void ec_fsm_master_action_configure(ec_fsm_master_t *fsm)
{
    ec_master_t *master = fsm->master;
    ec_slave_t *slave;
    
    // Find slaves that need configuration
    for (slave = master->slaves;
         slave < master->slaves + master->slave_count;
         slave++) {
        
        if (!slave->config) continue;
        
        // Check if slave needs configuration
        if (ec_fsm_slave_config_needed(slave)) {
            // CHANGED: Instead of running fsm_slave_config directly,
            // request the slave's own FSM to handle it
            ec_fsm_slave_request_config(&slave->fsm);
            
            EC_SLAVE_DBG(slave, 1, "Requested parallel configuration.\n");
        }
    }
    
    // Let ec_master_exec_slave_fsms() handle the parallel execution
    fsm->state = ec_fsm_master_state_wait_config;
}

// ADD: New state to wait for parallel configuration
void ec_fsm_master_state_wait_config(ec_fsm_master_t *fsm)
{
    ec_master_t *master = fsm->master;
    ec_slave_t *slave;
    int all_complete = 1;
    
    // Check if all slaves have completed configuration
    for (slave = master->slaves;
         slave < master->slaves + master->slave_count;
         slave++) {
        
        if (slave->fsm.config_requested || slave->fsm.config_running) {
            all_complete = 0;
            break;
        }
    }
    
    if (all_complete) {
        EC_MASTER_DBG(master, 1, "All slaves configured.\n");
        fsm->state = ec_fsm_master_state_end;
    }
    // Otherwise, stay in this state - slave FSMs are being executed
    // by ec_master_exec_slave_fsms() in the main loop
}
```

---

### Step 4: Ensure `master.c` Executes Slave FSMs

**File:** `master/master.c`

The existing `ec_master_exec_slave_fsms()` function already has the infrastructure for parallel execution. Verify it handles the new configuration states:

```c
void ec_master_exec_slave_fsms(ec_master_t *master)
{
    ec_slave_t *slave;
    ec_datagram_t *datagram;
    unsigned int count = 0;
    
    // This function already iterates through slaves and executes their FSMs
    // using available datagrams from the ext_ring
    
    while (count < master->slave_count) {
        slave = master->fsm_slave;
        
        // Check if this slave's FSM needs execution
        // ADD: Include config_requested and config_running in the check
        if (slave->fsm.config_requested || slave->fsm.config_running ||
            ec_fsm_slave_is_ready(&slave->fsm)) {
            
            // Get an available datagram
            datagram = ec_master_get_ext_datagram(master);
            if (!datagram) {
                break;  // No more datagrams available this cycle
            }
            
            // Execute the slave FSM
            if (ec_fsm_slave_exec(&slave->fsm, datagram)) {
                // FSM used the datagram, add to execution list
                list_add_tail(&slave->fsm.list, &master->fsm_exec_list);
                master->fsm_exec_count++;
            } else {
                // FSM didn't need the datagram, return it
                ec_master_return_ext_datagram(master, datagram);
            }
        }
        
        // Move to next slave (circular)
        master->fsm_slave++;
        if (master->fsm_slave >= master->slaves + master->slave_count) {
            master->fsm_slave = master->slaves;
        }
        count++;
    }
}
```

---

## Configuration Options

### Tuning Parallelism

The number of slaves configured simultaneously is controlled by `EC_EXT_RING_SIZE` in `master/globals.h`:

```c
/** Size of the external datagram ring.
 * This determines how many slave FSMs can run in parallel.
 * Effective parallel configs = EC_EXT_RING_SIZE / 2
 * (because some datagrams are used for scanning, etc.)
 */
#define EC_EXT_RING_SIZE 32   // Default: 16 parallel configs
```

Increase this value for larger networks, but be aware of memory usage.

---

## Testing Checklist

### Functional Tests

- [ ] Single slave configuration works correctly
- [ ] Multiple slaves (2-4) configure correctly
- [ ] Large network (50+ slaves) configures correctly
- [ ] Slave removal during configuration is handled
- [ ] Slave reconnection triggers reconfiguration
- [ ] DC synchronization works after parallel config
- [ ] SDO configuration (CoE) works correctly
- [ ] PDO mapping works correctly

### Performance Tests

- [ ] Measure configuration time with 10 slaves
- [ ] Measure configuration time with 50 slaves
- [ ] Measure configuration time with 100 slaves
- [ ] Compare with sequential configuration baseline

### Stress Tests

- [ ] Rapid slave connect/disconnect cycles
- [ ] Network cable unplug during configuration
- [ ] Mixed slave types with different config times

---

## Rollback Plan

If issues are encountered, the changes can be reverted by:

1. Reverting `fsm_slave_config` to embedded datagram
2. Removing configuration handling from `fsm_slave`
3. Restoring sequential configuration in `fsm_master`

The changes are isolated enough that a clean rollback is possible.

---

## Files Modified Summary

| File | Type of Change |
|------|----------------|
| `master/fsm_slave_config.h` | Struct modification, function signatures |
| `master/fsm_slave_config.c` | Use external datagram pointer |
| `master/fsm_slave.h` | Add config FSM and flags |
| `master/fsm_slave.c` | Initialize config FSM, handle in exec |
| `master/fsm_master.c` | Delegate config to slave FSM |
| `master/master.c` | Verify parallel execution (minimal changes) |

**Estimated Lines Changed:** 500-800 lines

---

## References

- Original parallel-slave patches: `features/parallel-slave/0001-0007`
- uecasm patchset README: Describes the 80s → 20s improvement
- IgH EtherCAT Master documentation: FSM architecture

---

## Author Notes

This implementation guide focuses on the **minimal viable parallel configuration** feature. The full uecasm parallel-slave patchset also includes:

- Parallel slave scanning (not implemented here)
- Moving SDO dictionary fetching to slave FSM
- Various optimizations and cleanups

These can be added incrementally after the core parallel configuration is working and tested.
