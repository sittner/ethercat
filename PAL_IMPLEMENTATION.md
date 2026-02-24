# PAL Implementation Progress

Platform Abstraction Layer (PAL) implementation tracker for the `uspace` branch.

The `uspace` branch implements a userspace/kernel split for the EtherCAT master:

- `master/` — shared code (goal: no direct kernel API usage)
- `master/kernel/` — kernel-mode platform implementation (kernel API ok)
- `master/uspace/` — userspace platform implementation

The current problem: `master/uspace/` reimplements kernel API names (e.g. `sema_init()`,
`kthread_run()`, `wake_up()`, `alloc_netdev()`) so shared code can call kernel names directly.
This is wrong — shared code should use neutral `ec_`-prefixed PAL names, with both `kernel/`
and `uspace/` sides providing implementations.

---

## Priority 1: Rename Kernel API Mimics to Neutral `ec_` PAL API

For each subsystem the work is:
1. Define neutral `ec_`-prefixed API in shared code
2. Update `master/uspace/pal_<name>.h` to implement the `ec_` API (remove kernel name mimics)
3. Create matching `master/kernel/pal_<name>.h` with thin inline wrappers calling real kernel API
4. Update all shared `master/*.c` files that use the old kernel names

### Semaphore (`pal_sem.h`)

- [x] Define `ec_sem_init`, `ec_sem_down`, `ec_sem_up`, `ec_sem_down_interruptible` in shared header
- [x] Update `master/uspace/pal_sem.h` to implement `ec_sem_*` (remove `sema_init`, `down`, `up`, `down_interruptible` mimics)
- [x] Create `master/kernel/pal_sem.h` with thin wrappers: `ec_sem_init` → `sema_init`, `ec_sem_down` → `down`, `ec_sem_up` → `up`, `ec_sem_down_interruptible` → `down_interruptible`
- [x] Update shared `master/*.c` files: replace `sema_init` → `ec_sem_init`, `down` → `ec_sem_down`, `up` → `ec_sem_up`, `down_interruptible` → `ec_sem_down_interruptible`

### RT Mutex (`pal_mtx.h`)

- [x] Define `ec_mutex_init`, `ec_mutex_lock`, `ec_mutex_unlock` in shared header (`ec_rt_lock_interruptible` already exists)
- [x] Update `master/uspace/pal_mtx.h` to implement `ec_mutex_*` (remove `rt_mutex_init`, `rt_mutex_lock`, `rt_mutex_unlock` mimics)
- [x] Create `master/kernel/pal_mtx.h` with thin wrappers: `ec_mutex_init` → `rt_mutex_init`, `ec_mutex_lock` → `rt_mutex_lock`, `ec_mutex_unlock` → `rt_mutex_unlock`
- [x] Update shared `master/*.c` files: replace `rt_mutex_init` → `ec_mutex_init`, `rt_mutex_lock` → `ec_mutex_lock`, `rt_mutex_unlock` → `ec_mutex_unlock`

### Wait Queue (`pal_queue.h`)

- [x] Define `ec_wq_init`, `ec_wq_wake`, `ec_wq_wake_interruptible`, `ec_wq_wait`, `ec_wq_wait_interruptible` in shared header
- [x] Update `master/uspace/pal_queue.h` to implement `ec_wq_*` (remove `init_waitqueue_head`, `wake_up`, `wake_up_interruptible`, `wait_event`, `wait_event_interruptible` mimics)
- [x] Create `master/kernel/pal_queue.h` with thin wrappers: `ec_wq_init` → `init_waitqueue_head`, `ec_wq_wake` → `wake_up`, etc.
- [x] Update shared `master/*.c` files: replace all wait queue kernel API calls with `ec_wq_*`

### Thread (`pal_thread.h`)

- [x] Define `ec_thread_run`, `ec_thread_stop`, `ec_thread_should_stop`, `ec_thread_yield`, `ec_thread_yield_timeout`, `ec_thread_wake`, `ec_thread_set_priority`, `ec_thread_bind_cpu` in shared header
- [x] Update `master/uspace/pal_thread.h` to implement `ec_thread_*` (remove `kthread_run`, `kthread_stop`, `kthread_should_stop`, `schedule`, `schedule_timeout`, `set_current_state`, `wake_up_process`, `sched_set_normal`, `kthread_bind` mimics)
- [x] Create `master/kernel/pal_thread.h` with thin wrappers mapping to kernel kthread API
- [x] Abstract or remove `set_current_state` from shared code
- [x] Update shared `master/*.c` files: replace all kthread/schedule kernel API calls with `ec_thread_*`

### Work Queue (`pal_work.h`)

- [x] Define `ec_work_init`, `ec_work_schedule`, `ec_work_queue`, `ec_wq_create`, `ec_wq_destroy`, `ec_work_cancel`, `ec_wq_flush` in shared header
- [x] Update `master/uspace/pal_work.h` to implement `ec_work_*` / `ec_wq_*` (remove `INIT_WORK`, `schedule_work`, `queue_work`, `create_workqueue`, `destroy_workqueue`, `cancel_work_sync`, `flush_workqueue` mimics)
- [x] Create `master/kernel/pal_work.h` with thin wrappers: `ec_work_init` → `INIT_WORK`, `ec_work_schedule` → `schedule_work`, etc.
- [x] Update shared `master/*.c` files: replace all workqueue kernel API calls with `ec_work_*` / `ec_wq_*`

### IRQ Work (`pal_irq_work.h`)

- [x] Define `ec_irq_work_init`, `ec_irq_work_queue` in shared header
- [x] Update `master/uspace/pal_irq_work.h` to implement `ec_irq_work_*` (remove `init_irq_work`, `irq_work_queue` mimics)
- [x] Create `master/kernel/pal_irq_work.h` with thin wrappers: `ec_irq_work_init` → `init_irq_work`, `ec_irq_work_queue` → `irq_work_queue`
- [x] Update shared `master/*.c` files: replace `init_irq_work` → `ec_irq_work_init`, `irq_work_queue` → `ec_irq_work_queue`

### Misc (`pal_misc.h`)

- [x] Remove `EXPORT_SYMBOL` from shared `master/*.c` files (move to `master/kernel/exports.c`)
- [x] Ensure `likely`/`unlikely`/`do_div`/`simple_strtoul`/`min`/`max` are handled via `pal_misc.h` on both sides

### List (`pal_list.h`)

- [x] Rename `master/uspace/list.h` to `master/uspace/pal_list.h`
- [x] Update all includes of `list.h` in `master/uspace/` to use `pal_list.h`
- [x] List API can stay as-is since it is a data structure, not a kernel API

---

## Priority 2: EoE (Ethernet over EtherCAT) Split

`master/ethernet.c` is shared code but is currently 100% kernel code.
`master/uspace/pal_eoe_compat.h` uses `#define net_device ec_netdev` globally — this is
namespace pollution that gets included for ALL shared files via `pal.h`.

### PR 1: Introduce PAL types in `ethernet.h` (no behavior change)

Define opaque PAL types so `ethernet.h` no longer uses raw kernel type names.
No logic changes — pure type aliasing.

- [x] Create `master/kernel/pal_eoe.h` with kernel-side typedefs:
  `typedef struct net_device * ec_eoe_netdev_t`,
  `typedef struct sk_buff * ec_eoe_buf_t`,
  `typedef struct net_device_stats ec_eoe_stats_t`
- [x] Update `master/uspace/pal_eoe.h` with matching userspace typedefs:
  `typedef ec_netdev_t * ec_eoe_netdev_t`,
  `typedef ec_skb_t * ec_eoe_buf_t`,
  `typedef struct { unsigned long rx_packets; ... } ec_eoe_stats_t`
- [x] Replace `struct net_device *dev` → `ec_eoe_netdev_t dev` in `ec_eoe_t` (`ethernet.h:81`)
- [x] Replace `struct net_device_stats stats` → `ec_eoe_stats_t stats` in `ec_eoe_t` (`ethernet.h:82`)
- [x] Replace `struct sk_buff *rx_skb` → `ec_eoe_buf_t rx_skb` in `ec_eoe_t` (`ethernet.h:86`)
- [x] Replace `struct sk_buff *skb` → `ec_eoe_buf_t skb` in `ec_eoe_frame_t` (`ethernet.h:60`)
- [x] Add accessor `ec_eoe_netdev_name(ec_eoe_netdev_t dev)` — returns `const char *` (both sides have `name` field but accessor is cleaner for abstraction)
- [x] Add accessor `ec_eoe_netdev_ifindex(ec_eoe_netdev_t dev)` — returns `int` (used once in `ethernet.c:166`)
- [x] Include `kernel/pal_eoe.h` from `kernel/pal.h`
- [x] Verify kernel and userspace builds compile cleanly with no logic changes

### PR 2: Extract `net_device` operations to PAL

Move net_device lifecycle and callback functions out of shared `ethernet.c` into
platform-specific `kernel/pal_eoe.c` and adapt `uspace/pal_eoe.c`.

- [x] Define PAL API for net_device lifecycle:
  `ec_eoe_netdev_create(ec_eoe_t *eoe, const char *name)` — wraps `alloc_netdev` + `ether_setup` + `register_netdev` + MAC init,
  `ec_eoe_netdev_destroy(ec_eoe_t *eoe)` — wraps `unregister_netdev` + `free_netdev`
- [x] Define PAL API for net_device queue operations:
  `ec_eoe_netdev_tx_lock(ec_eoe_netdev_t dev)` — wraps `netif_tx_lock_bh`,
  `ec_eoe_netdev_tx_unlock(ec_eoe_netdev_t dev)` — wraps `netif_tx_unlock_bh`,
  `ec_eoe_netdev_start_queue(ec_eoe_netdev_t dev)` — wraps `netif_start_queue`,
  `ec_eoe_netdev_stop_queue(ec_eoe_netdev_t dev)` — wraps `netif_stop_queue`,
  `ec_eoe_netdev_wake_queue(ec_eoe_netdev_t dev)` — wraps `netif_wake_queue`
- [x] Create `master/kernel/pal_eoe.c`:
  move `ec_eoedev_open`, `ec_eoedev_stop`, `ec_eoedev_tx`, `ec_eoedev_stats` from `ethernet.c`,
  implement `ec_eoe_netdev_create()` (absorbs `alloc_netdev`/`register_netdev`/`eth_hw_addr_set`/`ether_setup`/`netdev_priv` init from `ec_eoe_init`),
  implement `ec_eoe_netdev_destroy()` (absorbs `unregister_netdev`/`free_netdev` from `ec_eoe_clear`),
  implement queue operation wrappers as thin inlines or functions,
  register `net_device_ops` callbacks (verify how `ndo_open`/`ndo_stop`/`ndo_start_xmit`/`ndo_get_stats` are currently registered)
- [x] Refactor `ec_eoe_init()` in shared `ethernet.c`:
  keep protocol state init (queues, counters, datagram, state machine),
  replace `alloc_netdev`/`register_netdev`/`eth_hw_addr_set` block with single `ec_eoe_netdev_create(eoe, name)` call
- [x] Refactor `ec_eoe_clear()` in shared `ethernet.c`:
  replace `unregister_netdev`/`free_netdev` with `ec_eoe_netdev_destroy(eoe)` call
- [x] Refactor `ec_eoe_flush()` in shared `ethernet.c`:
  replace `netif_tx_lock_bh`/`netif_tx_unlock_bh` with `ec_eoe_netdev_tx_lock`/`ec_eoe_netdev_tx_unlock`
- [x] Refactor `ec_eoe_state_tx_start()` in shared `ethernet.c`:
  replace `netif_tx_lock_bh`/`netif_tx_unlock_bh`/`netif_wake_queue` with PAL wrappers
- [x] Adapt `master/uspace/pal_eoe.c` to implement the new PAL API (`ec_eoe_netdev_create` wraps TAP `ec_netdev_alloc` + `ec_netdev_register`, etc.)
- [x] Update kernel and userspace Makefiles to add `kernel/pal_eoe.c`
- [x] Remove forward declarations of `ec_eoedev_open`/`ec_eoedev_stop`/`ec_eoedev_tx`/`ec_eoedev_stats` from `ethernet.c` (now in `kernel/pal_eoe.c`)

### PR 3: Abstract `sk_buff` operations

Replace all direct `skb_*` / `struct sk_buff` member access in the shared EoE state
machine with `ec_eoe_buf_*` PAL wrappers.

- [x] Define PAL buffer API in `kernel/pal_eoe.h` and `uspace/pal_eoe.h`:
  `ec_eoe_buf_alloc(size_t size)` → `dev_alloc_skb` / `ec_skb_alloc`,
  `ec_eoe_buf_free(ec_eoe_buf_t buf)` → `dev_kfree_skb` / `ec_skb_free`,
  `ec_eoe_buf_put(ec_eoe_buf_t buf, size_t len)` → `skb_put` / `ec_skb_put`,
  `ec_eoe_buf_len(ec_eoe_buf_t buf)` → `buf->len`,
  `ec_eoe_buf_data(ec_eoe_buf_t buf)` → `buf->data`
- [x] Define PAL RX-completion API:
  `ec_eoe_buf_set_dev(ec_eoe_buf_t buf, ec_eoe_netdev_t dev)` → `buf->dev = dev`,
  `ec_eoe_buf_set_protocol(ec_eoe_buf_t buf, ec_eoe_netdev_t dev)` → wraps `eth_type_trans` / `ec_eth_type_trans`,
  `ec_eoe_buf_set_checksum(ec_eoe_buf_t buf)` → `buf->ip_summed = CHECKSUM_UNNECESSARY`,
  `ec_eoe_buf_deliver(ec_eoe_buf_t buf)` → `netif_rx` / `ec_netif_rx`
- [x] Kernel side (`kernel/pal_eoe.h`): implement as thin inline wrappers around real `skb_*` / `netif_*` API
- [x] Userspace side (`uspace/pal_eoe.h`): implement as thin inline wrappers around existing `ec_skb_*` / `ec_netif_*` functions
- [x] Replace all `dev_alloc_skb()` calls in `ethernet.c` state machine with `ec_eoe_buf_alloc()`
- [x] Replace all `dev_kfree_skb()` calls in `ethernet.c` with `ec_eoe_buf_free()` (~9 call sites)
- [x] Replace all `skb_put()` calls in `ethernet.c` with `ec_eoe_buf_put()`
- [x] Replace all `skb->len` / `skb->data` member access with `ec_eoe_buf_len()` / `ec_eoe_buf_data()` (~15 call sites in `ec_eoe_send`, `ec_eoe_state_rx_fetch`, `ec_eoe_state_tx_start`, `ec_eoe_state_tx_sent`)
- [x] Replace RX completion sequence in `ec_eoe_state_rx_fetch()` (`skb->dev =`, `eth_type_trans`, `skb->ip_summed =`, `netif_rx`) with PAL wrappers
- [x] Remove `WARN_ON_ONCE` / `lockdep_assert_held` / `skb_get_queue_mapping` / `netdev_get_tx_queue` stubs from shared code (already moved to `kernel/pal_eoe.c` in PR 2)

### PR 4: Remove `pal_eoe_compat.h`

Once shared code uses only `ec_eoe_*` PAL names, remove the kernel-name-mimicking compat layer.

- [x] Verify no shared `master/*.c` or `master/*.h` file uses raw kernel names (`net_device`, `sk_buff`, `alloc_netdev`, `dev_alloc_skb`, `skb_put`, `netif_rx`, etc.)
- [x] Remove `#include "pal_eoe_compat.h"` from `master/uspace/pal.h` (line 59)
- [x] Delete `master/uspace/pal_eoe_compat.h` entirely
- [x] Move any remaining shared definitions (e.g. `NET_NAME_UNKNOWN` stub if needed) to `uspace/pal_eoe.h`
- [x] Fix `ec_log_ratelimit()` conflict: rename `ec_printk_ratelimit` → `ec_log_ratelimit` in `pal_eoe.c`, remove `#define` alias from `pal_eoe.h`, move declaration to `pal_misc.h` (matching kernel `pal.h` placement), remove stub from `pal_misc.h`
- [x] Verify kernel and userspace builds compile cleanly

### Notes / known complications

- `eoe->dev->name` is accessed ~20 times in shared `ethernet.c` for debug/log messages — `ec_eoe_netdev_name()` accessor handles this (PR 1)
- `eoe->dev->ifindex` is accessed once (`ethernet.c:166`) for MAC uniqueness — `ec_eoe_netdev_ifindex()` accessor handles this (PR 1)
- `net_device_ops` registration (`ndo_open`, `ndo_stop`, `ndo_start_xmit`, `ndo_get_stats`) must be verified before PR 2 — the current `ethernet.c` declares callbacks but the `net_device_ops` struct assignment is not visible in shared code
- `ec_eoe_frame_t.skb` field also needs the `ec_eoe_buf_t` typedef (PR 1)
- `struct net_device_stats` is used ~20 times via `eoe->stats.*` — `ec_eoe_stats_t` typedef keeps both sides compatible (PR 1)
- PR 3 is the heaviest — consider splitting further (alloc/free first, then RX-completion wrappers) if needed

---

## Priority 3: Bugs and Incomplete Implementations

- [x] `system_wq` is NULL, never initialized → ~~crash on first `schedule_work()` call~~ fixed: initialized in `master_main.c`, stale `//TODO` removed from `pal.c`
- [x] `irq_work_queue_global` is NULL, never initialized → ~~crash~~ fixed: initialized in `master_main.c`, stale `//TODO` removed from `pal.c`
- [ ] `ec_master_pal_t` is empty struct with `// TODO` (`uspace/pal.h:111-113`)
- [x] `kthread_bind()` dead code → fixed: `ec_thread_bind_cpu` now implements actual CPU affinity via `pthread_setaffinity_np()`
- [x] Implement `pthread_mutex_destroy()` calls for rt_mutex cleanup → `ec_mutex_destroy()` added to both PAL sides
- [ ] `uspace/cdev.h` is a stub (`//TODO struct cdev`) — needs userspace implementation or proper stub

---

## Priority 4: Device Init/Clear Split

- [x] Extract common device init (stats zeroing, `device->master`, name assignment) from `kernel/pal.c` `ec_device_init()` into shared `master/device.c`
- [x] Keep platform-specific buffer setup (`sk_buff`/`alloc_skb`/`ethhdr` in kernel, transport-based in uspace) in respective `pal.c`
- [x] Address `// TODO: shared init` comment in `kernel/pal.c`

---

## Priority 5: Kernel Side PAL Balance

Once shared code uses `ec_` names, balance the kernel side to match uspace structure.

- [ ] Split monolithic `kernel/pal.h` typedefs into per-concept files matching uspace structure (`kernel/pal_sem.h`, `kernel/pal_mtx.h`, `kernel/pal_queue.h`, `kernel/pal_thread.h`, `kernel/pal_work.h`, `kernel/pal_irq_work.h`)
- [ ] Add thin inline wrappers in each kernel `pal_*.h` file
- [ ] Ensure symmetric file structure between `kernel/` and `uspace/`

---

## Priority 6: Cleanup

- [ ] Delete dead `master/uspace/config.h` (real config is `/config.h` from autotools) or document why it exists
- [ ] Rename `master_main.c` and `module.c` both to `main.c`
- [ ] Remove `master/globals.h.gch` (accidentally committed precompiled header, 4MB)

---

## Completed

- [x] Memory allocation PAL (`pal_alloc.h`) — balanced on both sides
- [x] Scheduling/time PAL (`pal.c`) — `ec_master_idle_thread_schedule`, `ec_master_operation_thread_schedule`, `ec_current_time` implemented on both sides
- [x] Logging PAL — `ec_log()` abstraction in shared code, kernel uses `printk`, uspace uses `vfprintf`
- [x] Time type abstraction — `ec_time_t` with `ec_time_to_ns`/`ec_us_to_time`/etc. macros
- [x] Transport layer for userspace (`uspace/transport/`) — raw socket + XDP
- [x] `ec_device_pal_t` — properly split (kernel: `net_device`/`module`, uspace: `ec_transport`/link check)
- [x] TAP-based EoE implementation (`uspace/pal_eoe.c`) — new userspace code (needs adaptation for PAL interface)
