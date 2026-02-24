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

- [ ] Define `ec_sem_init`, `ec_sem_down`, `ec_sem_up`, `ec_sem_down_interruptible` in shared header
- [ ] Update `master/uspace/pal_sem.h` to implement `ec_sem_*` (remove `sema_init`, `down`, `up`, `down_interruptible` mimics)
- [ ] Create `master/kernel/pal_sem.h` with thin wrappers: `ec_sem_init` → `sema_init`, `ec_sem_down` → `down`, `ec_sem_up` → `up`, `ec_sem_down_interruptible` → `down_interruptible`
- [ ] Update shared `master/*.c` files: replace `sema_init` → `ec_sem_init`, `down` → `ec_sem_down`, `up` → `ec_sem_up`, `down_interruptible` → `ec_sem_down_interruptible`

### RT Mutex (`pal_mtx.h`)

- [ ] Define `ec_mutex_init`, `ec_mutex_lock`, `ec_mutex_unlock` in shared header (`ec_rt_lock_interruptible` already exists)
- [ ] Update `master/uspace/pal_mtx.h` to implement `ec_mutex_*` (remove `rt_mutex_init`, `rt_mutex_lock`, `rt_mutex_unlock` mimics)
- [ ] Create `master/kernel/pal_mtx.h` with thin wrappers: `ec_mutex_init` → `rt_mutex_init`, `ec_mutex_lock` → `rt_mutex_lock`, `ec_mutex_unlock` → `rt_mutex_unlock`
- [ ] Update shared `master/*.c` files: replace `rt_mutex_init` → `ec_mutex_init`, `rt_mutex_lock` → `ec_mutex_lock`, `rt_mutex_unlock` → `ec_mutex_unlock`

### Wait Queue (`pal_queue.h`)

- [ ] Define `ec_wq_init`, `ec_wq_wake`, `ec_wq_wake_interruptible`, `ec_wq_wait`, `ec_wq_wait_interruptible` in shared header
- [ ] Update `master/uspace/pal_queue.h` to implement `ec_wq_*` (remove `init_waitqueue_head`, `wake_up`, `wake_up_interruptible`, `wait_event`, `wait_event_interruptible` mimics)
- [ ] Create `master/kernel/pal_queue.h` with thin wrappers: `ec_wq_init` → `init_waitqueue_head`, `ec_wq_wake` → `wake_up`, etc.
- [ ] Update shared `master/*.c` files: replace all wait queue kernel API calls with `ec_wq_*`

### Thread (`pal_thread.h`)

- [ ] Define `ec_thread_run`, `ec_thread_stop`, `ec_thread_should_stop`, `ec_thread_yield`, `ec_thread_yield_timeout`, `ec_thread_wake`, `ec_thread_set_priority`, `ec_thread_bind_cpu` in shared header
- [ ] Update `master/uspace/pal_thread.h` to implement `ec_thread_*` (remove `kthread_run`, `kthread_stop`, `kthread_should_stop`, `schedule`, `schedule_timeout`, `set_current_state`, `wake_up_process`, `sched_set_normal`, `kthread_bind` mimics)
- [ ] Create `master/kernel/pal_thread.h` with thin wrappers mapping to kernel kthread API
- [ ] Abstract or remove `set_current_state` from shared code
- [ ] Update shared `master/*.c` files: replace all kthread/schedule kernel API calls with `ec_thread_*`

### Work Queue (`pal_work.h`)

- [ ] Define `ec_work_init`, `ec_work_schedule`, `ec_work_queue`, `ec_wq_create`, `ec_wq_destroy`, `ec_work_cancel`, `ec_wq_flush` in shared header
- [ ] Update `master/uspace/pal_work.h` to implement `ec_work_*` / `ec_wq_*` (remove `INIT_WORK`, `schedule_work`, `queue_work`, `create_workqueue`, `destroy_workqueue`, `cancel_work_sync`, `flush_workqueue` mimics)
- [ ] Create `master/kernel/pal_work.h` with thin wrappers: `ec_work_init` → `INIT_WORK`, `ec_work_schedule` → `schedule_work`, etc.
- [ ] Update shared `master/*.c` files: replace all workqueue kernel API calls with `ec_work_*` / `ec_wq_*`

### IRQ Work (`pal_irq_work.h`)

- [ ] Define `ec_irq_work_init`, `ec_irq_work_queue` in shared header
- [ ] Update `master/uspace/pal_irq_work.h` to implement `ec_irq_work_*` (remove `init_irq_work`, `irq_work_queue` mimics)
- [ ] Create `master/kernel/pal_irq_work.h` with thin wrappers: `ec_irq_work_init` → `init_irq_work`, `ec_irq_work_queue` → `irq_work_queue`
- [ ] Update shared `master/*.c` files: replace `init_irq_work` → `ec_irq_work_init`, `irq_work_queue` → `ec_irq_work_queue`

### Misc (`pal_misc.h`)

- [ ] Remove `EXPORT_SYMBOL` from shared `master/*.c` files (move to kernel-specific wrappers or remove)
- [ ] Ensure `likely`/`unlikely`/`do_div`/`simple_strtoul`/`min`/`max` are handled via `pal_misc.h` on both sides

### List (`pal_list.h`)

- [ ] Rename `master/uspace/list.h` to `master/uspace/pal_list.h`
- [ ] Update all includes of `list.h` in `master/uspace/` to use `pal_list.h`
- [ ] List API can stay as-is since it is a data structure, not a kernel API

---

## Priority 2: EoE (Ethernet over EtherCAT) Split

`master/ethernet.c` is shared code but is currently 100% kernel code.
`master/uspace/pal_eoe_compat.h` uses `#define net_device ec_netdev` globally — this is
namespace pollution that gets included for ALL shared files via `pal.h`.

- [ ] Split `master/ethernet.c` into shared protocol logic and platform-specific net_device/skb handling
- [ ] Extract EoE state machine (`rx_start`, `rx_check`, `rx_fetch`, `tx_start`, `tx_sent`) and protocol logic into shared code using abstract PAL types
- [ ] Move `ec_eoedev_open`, `ec_eoedev_stop`, `ec_eoedev_tx`, `ec_eoedev_stats` and net_device init/clear to `master/kernel/pal_eoe.c`
- [ ] Adapt `master/uspace/pal_eoe.c` (TAP device) to match the new PAL interface
- [ ] Replace `struct net_device *dev` and `struct sk_buff *rx_skb` in `ethernet.h` with PAL types (`ec_eoe_netdev_t`, `ec_eoe_buf_t`)
- [ ] Remove `master/uspace/pal_eoe_compat.h` entirely (the `#define net_device` approach)
- [ ] Stop including `pal_eoe_compat.h` from `master/uspace/pal.h`

---

## Priority 3: Bugs and Incomplete Implementations

- [ ] `system_wq` is NULL, never initialized → crash on first `schedule_work()` call (`uspace/pal.c:87`)
- [ ] `irq_work_queue_global` is NULL, never initialized (`uspace/pal.c:88`)
- [ ] `ec_master_pal_t` is empty struct with `// TODO` (`uspace/pal.h:111-113`)
- [ ] `kthread_bind()` has dead code after `return` statement (`uspace/pal_thread.h:270`)
- [ ] Implement `pthread_mutex_destroy()` calls for rt_mutex cleanup
- [ ] `uspace/cdev.h` is a stub (`//TODO struct cdev`) — needs userspace implementation or proper stub

---

## Priority 4: Device Init/Clear Split

- [ ] Extract common device init (stats zeroing, `device->master`, name assignment) from `kernel/pal.c` `ec_device_init()` into shared `master/device.c`
- [ ] Keep platform-specific buffer setup (`sk_buff`/`alloc_skb`/`ethhdr` in kernel, transport-based in uspace) in respective `pal.c`
- [ ] Address `// TODO: shared init` comment in `kernel/pal.c`

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
