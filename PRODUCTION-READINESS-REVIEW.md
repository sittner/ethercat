# Production-Readiness Review — Userspace Master Port

Date: 2026-07-20 · Branch: `uspace` @ `f5ed03a1` · Scope: userspace port
(`--enable-uspace-master`), PAL, transports, tool/IPC, with kernel mode retained.

Verification basis: full source audit of `master/`, `master/uspace/`, `transport/`,
`lib/`, `tool/`; clean-tree build with `-Wall -Wextra` (gcc, Debian 13); review of the
LinuxCNC/GOMC integration (`src/hal/drivers/ethercat/`) as the reference embedder and
of its RT-hardening methodology (`RT_HARDENING_CHECKLIST.md`, `rt-effects-check.sh`).

## Verdict summary

| Area | State | Blocking issues |
|---|---|---|
| Cyclic-path RT design | Good foundation | logging fallback in RT path; no mlockall story |
| Correctness | 1 real UB bug, ~112 warnings | `lib/shared.c` float type-punning |
| Locking | Mostly sound | plain `sem_t` (no PI) for FSM-side locks |
| Threads/scheduling | Deliberate but undocumented | FSM/EoE forced `SCHED_OTHER`, no app control |
| Security | 1 gap | IPC socket has default permissions |
| ABI/packaging | Pre-release | `-version-info 0:0:0`, no `.pc`/CMake for uspace lib |
| Testing | **None** | no unit tests, no `make check` |
| CI | Upstream-only | no job builds `--enable-uspace-master` |
| Docs | Drifted | stale checklists, FEATURES/TODO not updated for uspace |

---

## 1. Findings

### 1.1 Correctness (P0)

**F1 — Undefined behavior in the public real/lreal accessors.**
`lib/shared.c:36-62` (`ecrt_read_real`, `ecrt_read_lreal`, `ecrt_write_real`,
`ecrt_write_lreal` — compiled into the uspace lib via `$(top_srcdir)/lib/shared.c`)
type-pun float↔integer through pointer casts. GCC at `-O2` already reports
`'raw' is used uninitialized` plus `-Wstrict-aliasing` at all four sites — i.e. this
is not theoretical; the optimizer is entitled to (and may) drop the store. The write
side additionally performs direct, potentially unaligned `uint32_t`/`uint64_t` stores
into PDO memory instead of using `EC_WRITE_U32/U64`. Fix: `memcpy`-based punning (or a
union) + the `EC_READ_*`/`EC_WRITE_*` macros for the bus access. These functions are
in the RT PDO path of every consumer that uses REAL/LREAL PDO entries (LinuxCNC does:
they are in its `ecrt_rt_api.h` RT subset).

**F2 — Warning debt.** Clean build with `-Wall -Wextra`: 112 warnings.
Breakdown: 52 `-Wunused-parameter` (mostly PAL stubs — cosmetic), 19 `-Wtype-limits`
(e.g. `slave.h:100`, `coe_emerg_ring.c:78` — dead comparisons hiding intent), 15
`-Wstringop-truncation` (several real: `cdev.c:1291` truncates 123→107 bytes with no
guaranteed NUL; `tool_api.c:65` `strncpy` bound == destination size), 14
`-Wsign-compare`, 4 `-Wstrict-aliasing` / 2 `-Wuninitialized` (= F1), 2
`-Wpointer-sign` (`tool_api.c:925,995`), 2 `-Warray-bounds` (`module.c:177,179`
indexing `ec_device_t[1]` at subscript 1 — check `EC_MAX_NUM_DEVICES` handling in
uspace mode). Goal: zero-warning build, then `-Werror` in CI.

### 1.2 RT readiness of the cyclic path

What is already right — worth stating because it is the core claim of the port:

- `ecrt_master_receive` / `ecrt_domain_process` / `ecrt_domain_queue` /
  `ecrt_master_send` take **no locks** and perform **no per-cycle heap allocation**
  (`master/master.c:2243,2290`, `master/domain.c:458,666`). RT↔FSM handover is via
  sequence counters and a single-producer/consumer ring
  (`master.c:728-805,2249`); `ecrt_master_send_ext` uses `sem_trywait`
  (`master.c:2338`) so the RT side never blocks.
- RT-critical buffers (datagram payloads, domain data, datagram pairs, redundancy
  buffer, XDP UMEM) are prefaulted + `mlock`ed via the `ec_rt_alloc*` PAL
  (`master/uspace/pal_alloc.h:69-140`, commit `e8d4f900`).
- Transport syscalls are non-blocking (`MSG_DONTWAIT` / `O_NONBLOCK`;
  `transport_raw.c:130,196,230`, `transport_xdp.c:428,531`).

Gaps:

**F3 — Logging reachable from the RT path (P1).** `EC_RT_SYSLOG` is on by default
(`configure.ac:1391`). On datagram timeout, corrupted/unmatched frames, working-counter
changes and link transitions, the receive path calls `ec_log`
(`master.c:2314-2315,1044-1073`, `domain.c:635-655`, `device_uspace.c:127-129`). With
no app log callback installed, the fallback is `fprintf(stderr)+fflush` under a
process-global mutex (`master/uspace/pal.c:28,55-60`) — an unbounded blocking call in
the RT thread exactly when the bus is misbehaving. Mitigations (pick one or layer):
(a) document that RT apps MUST install a nonblocking callback via
`ecrt_lib_init(log_cb, …)`; (b) make the fallback a lock-free ring drained by a
low-prio thread; (c) an overrideable `EC_RT_LOG()` macro (see §2).

**F4 — No process-level memory locking (P1, by design — must be documented as API
contract).** The library `mlock`s its own buffers but does not `mlockall`; thread
stacks and any later library `malloc` (e.g. `ec_datagram_prealloc` re-allocation on
datagram resize, `datagram.c:152`, FSM-context) are not covered. Every example app
calls `mlockall(MCL_CURRENT|MCL_FUTURE)` + stack prefault itself
(`examples/user/main.c:359`, `examples/dc_user/main.c:258`). This split is correct
(the app owns process policy) but is currently only discoverable by reading examples.
Action: state the requirement in the `ecrt_startup_master` doc block and in
USERSPACE-MASTER-IMPL.md; optionally provide a helper (`ecrt_rt_harden()`), modeled on
LinuxCNC's `harden_rt()` (mlockall, `RLIMIT_MEMLOCK`, `/dev/cpu_dma_latency`).

**F5 — Library thread scheduling is hardwired (P1).** All library threads (master
FSM idle/operation, EoE, IPC listener, workqueues) are created with default inherited
attributes; `ec_thread_set_priority` deliberately forces `SCHED_OTHER` prio 0
(`pal_thread.c:240-248`). Consequences: (a) if the app creates the master from an RT
thread, library threads inherit RT scheduling silently; (b) under CPU pressure a
starved FSM thread stalls slave FSMs, SDO traffic and watchdog-relevant rescans with
no diagnostic. Action: expose scheduling per thread class (policy/priority/affinity)
through the PAL/API, default safe (`SCHED_OTHER`, not inherited — create with
`PTHREAD_EXPLICIT_SCHED`), and document the starvation failure mode.

**F6 — Non-PI semaphores (P2).** Only `io_mutex` is `PTHREAD_PRIO_INHERIT`
(`pal_mtx.h:46`); `master_sem`, `ext_queue_sem`, etc. are plain `sem_t`
(`pal_sem.h:33`). The strict cyclic path never blocks on them (trylock only), so this
is not an inversion in the app RT thread today — but any future code taking
`master_sem` from RT context inherits an unbounded inversion. Either convert the
sem-as-mutex users to PI mutexes where semantics allow, or add a PAL comment + the
§2 static check to keep them out of RT TUs.

**F7 — Teardown vs. running RT loop (P2).** `ecrt_lib_cleanup` force-releases active
masters (`module.c:300-319`, commit `14f56b57`) and `release_master_internal` frees
the master struct while a still-running app RT thread could be inside
`ecrt_master_send`. Registry access is locked but the master object itself is not
lifetime-protected against concurrent cyclic calls. Document the contract ("stop your
cyclic task before release/cleanup") in `ecrt.h` and consider a debug-mode guard
(atomic in-cycle flag checked in release).

**F8 — Per-cycle syscall inventory (informational).** Per cycle the RT thread issues:
1 `sendto` per TX frame (raw) or ring-kick (XDP), `recvfrom` per RX poll,
`clock_gettime` (vDSO), `sched_getcpu` (vDSO, `pal_affinity.h:39`), and once per
second an `ioctl(SIOCGIFFLAGS)` link check inside `ec_device_poll`
(`device_uspace.c:121`, `transport_raw.c:266`). The 1 Hz ioctl is the only
non-obvious one — it is cheap but is a real syscall in the receive path; consider
moving link supervision to the FSM thread.

### 1.3 Security (P0)

**F9 — IPC socket permissions.** The tool socket (`/var/run/ethercat.sock`) is
created with default permissions; access control is explicitly "not in scope" in
USERSPACE-TOOL-IMPL.md. The tool API includes write operations (SDO download, state
changes, SII write, register write) — i.e. any local user can command drives. Minimum:
`umask`/`fchmod` 0660 + configurable group (mirroring the kernel module's udev
`MODE="0664", GROUP=...` convention), socket path configurable (already is), document.

### 1.4 ABI & packaging (P1)

- The uspace `libethercat` is `-version-info 0:0:0` with its own version-node scheme
  (`LIBETHERCAT_USPACE_1.0`, unstable `LIBETHERCAT_USPACE_TOOL_1.0`) — good structure
  (commit `f2252257`), but the soname must be versioned before any release, and the
  intentional ABI break vs. the kernel-mode `lib/` (3:0:2) should be stated in
  INSTALL.md.
- No `libethercat.pc` / CMake config is installed in uspace mode (only kernel-mode
  `lib/` installs them). LinuxCNC hardcodes `-lethercat` + prefix paths as a result.
  Add a uspace `.pc` (and reuse `ethercat-config.cmake.in`).
- `ecrt.h` is generated with `#define EC_USPACE_MASTER 1` baked in at configure time —
  correct for single-mode installs, but worth a header comment explaining that one
  installed header serves exactly one mode.

### 1.5 Documentation drift (P2)

- `PAL_IMPLEMENTATION.md`: two items still `[ ]` (P3 `ec_master_pal_t`, P6
  `module.c`) are actually done — stale.
- `USERSPACE-TOOL-IMPL.md`: FoE/SoE/EoE commands and `config`/`domains` remain
  unverified (hardware-gated) — keep, but they belong in a test matrix (§3).
- `FEATURES.md` and `TODO` predate the port (TODO still lists "move master to a user
  space daemon" as future work). Refresh both.
- Only one code TODO exists (`tool_api.c:926`, hardcoded 10000-byte FoE size).

---

## 2. RT-check hand-over to the embedding application

Reference pattern (LinuxCNC/GOMC): a function-attribute macro defined in a header,
expanding to `[[clang::nonblocking]]` when the toolchain supports Clang ≥20 Function
Effect Analysis and to nothing otherwise (`src/rtapi/rtapi_rt_check.h`,
`gomc_rt_check.h`); RT entry points and — critically — the cyclic dispatch
*function-pointer types* carry the attribute, and a CI script compiles the RT TUs with
`-fsyntax-only -Wfunction-effects -Werror=function-effects`
(`scripts/rt-effects-check.sh`). Because this library does not annotate its API,
LinuxCNC today maintains a shim (`src/hal/drivers/ethercat/ecrt_rt_api.h`) that
re-declares the RT-safe `ecrt_*` subset with the attribute and must be kept in sync
by hand — the library is a *trusted, unverified* leaf of their audit
(`RT_HARDENING_CHECKLIST.md` §0 gap list).

Proposal — annotate natively in `include/ecrt.h.in`, with the exact overrideable
`#define` hand-over requested:

```c
/* RT-safety annotation for functions callable from cyclic RT context.
 * Overrideable: define ECRT_RT_ATTR before including ecrt.h to substitute
 * your framework's attribute (e.g. RTAPI_NONBLOCKING, GOMC_NONBLOCKING). */
#ifndef ECRT_RT_ATTR
# if defined(__clang__) && defined(__has_attribute)
#  if __clang_major__ >= 20 && __has_attribute(nonblocking)
#   define ECRT_RT_ATTR __attribute__((nonblocking))
#  endif
# endif
# ifndef ECRT_RT_ATTR
#  define ECRT_RT_ATTR /* empty: no-op on GCC / older clang, ABI unchanged */
# endif
#endif
```

Then tag the documented RT subset (trailing decl attribute):
`ecrt_master_send`, `ecrt_master_receive`, `ecrt_master_send_ext`,
`ecrt_master_state`, `ecrt_domain_process`, `ecrt_domain_queue`, `ecrt_domain_state`,
`ecrt_master_application_time`, `ecrt_master_sync_reference_clock(_to)`,
`ecrt_master_sync_slave_clocks`, `ecrt_master_reference_clock_time`,
`ecrt_master_sync_monitor_queue/process`, `ecrt_slave_config_state`,
`ecrt_read/write_real/lreal` (after F1 is fixed — as written they are not
RT-clean-compilable anyway).

Effects:
- Embedders get compile-time RT verification of their *own* code calling this API for
  free; calling a non-RT function (`ecrt_master_sdo_download`, …) from an annotated
  cyclic function becomes a diagnostic.
- The LinuxCNC shim collapses to `#define ECRT_RT_ATTR GOMC_NONBLOCKING` before
  `#include <ecrt.h>` — the "seamless hand-over".
- Same header works for kernel mode: the macro is empty under `__KERNEL__`/GCC.

Verifying the *implementation* (not just callers) — two CI-able mechanisms, both
already proven in the LinuxCNC tree:
1. A `scripts/rt-effects-check.sh` analog: clang-20 `-fsyntax-only
   -Wfunction-effects -Werror=function-effects` over the RT TUs
   (`master/master.c`, `master/domain.c`, `master/datagram.c`,
   `master/uspace/device_uspace.c`, `transport/transport_raw.c`,
   `transport/transport_xdp.c`), with `TRUSTED` pragma escapes for the deliberate
   syscalls (`sendto`/`recvfrom`/`clock_gettime` are nonblocking by flag, which the
   analysis cannot see). This would have flagged F3 mechanically.
2. RealtimeSanitizer (`-fsanitize=realtime`, clang ≥20) on the RT smoke test (§3.4):
   attribute the test's cyclic function `[[clang::nonblocking]]` and RTSan aborts on
   any malloc/blocking syscall at runtime.

Runtime hand-over hooks (complement, not substitute):
- `ecrt_lib_init(log_cb, …)` already routes all library logging to the app — keep,
  document as the RT-required configuration (F3).
- Optional `EC_RT_CHECK(cond)` / `EC_RT_LOG(...)` overrideable macros in the PAL
  (`pal_misc.h`), default no-op/ec_log, so an embedder can map them to its own
  assertion/tracing (LinuxCNC would map to `rtapi_print_msg`). Keep the surface tiny:
  two macros, both `#ifndef`-guarded.

---

## 3. Testing framework

Today: zero automated tests (no `check_PROGRAMS`/`TESTS` anywhere), and LinuxCNC's
tree has zero tests for the driver either — its production-readiness doc flags the
combination "commands real drives, zero tests" as its Tier-1 hotspot #3. Verification
of this port is currently manual checkboxes in USERSPACE-*-IMPL.md, several
hardware-gated (FoE/SoE/EoE untested).

The userspace port is itself the enabler: the shared master core now compiles into a
plain process, so the core (which kernel mode also uses) becomes unit-testable without
kernel infrastructure. Proposed layers, cheapest first:

**T1 — Pure unit tests (start here).** automake `make check` (`check_PROGRAMS` +
`TESTS`), plain C + a vendored single-file assert framework (Unity) to keep zero
external deps. First targets, chosen because each had a recent real bug or is
security-relevant:
- PDO entry/list string formatting (regression for the `f5ed03a1` buffer overflow),
- datagram construction/parsing (`datagram.c`), working-counter handling,
- `lib/shared.c` real/lreal round-trips incl. unaligned buffers (locks in the F1 fix),
- IPC wire (de)serialization round-trips (`ec_ipc_types.h` linearization — this is a
  local attack surface, F9),
- PAL primitives (`pal_list.h`, `ec_rt_alloc` prefault/mlock behavior, work queues).

**T2 — Simulated-bus tests (the big lever).** `ec_transport_ops_t` is public and
link-time pluggable (`ectp.h:66-104`); the registry pattern makes a
`transport_sim` trivial to add (test-only, not installed). It implements a virtual
bus: parse outgoing frames, emulate N slaves at datagram level (position/configured
addressing, BRD slave count, AL control/status, SII EEPROM images, then CoE mailbox).
This unlocks hardware-free tests of the actual product logic: bus scan, slave
configuration, PREOP→SAFEOP→OP transitions, PDO mapping/assignment writes (regression
for `f381059c`), domain exchange + WC states, link-down behavior (`5dab28a0`),
scan watchdog (`530d0403`), PSC (`ecf9dac8`). Build the simulator incrementally —
even BRD+AL+SII covers the scan/FSM paths where the last four bugs were fixed.
Because the core is shared, every T2 test also exercises the kernel-mode logic.

**T3 — Integration tests.** (a) `ec_master` daemon + `ethercat` tool over the Unix
socket against `transport_sim` — covers the whole USERSPACE-TOOL-IMPL matrix
including the currently hardware-gated commands (simulate an FoE/SoE mailbox);
also test socket permissions (F9). (b) veth-pair test in an unshared netns
(`unshare -rn`) exercising the real raw transport end-to-end (CI-safe, no root on
runners that allow user namespaces).

**T4 — RT smoke / latency gate.** A `make check`-external test (needs privileges):
cyclic loop at `SCHED_FIFO` over `transport_sim` or veth, mlockall'd, measuring
per-cycle jitter histogram and asserting (a) p99.9 under threshold, (b) **zero
minflt/majflt delta after warmup** (read `/proc/self/stat` — the page-fault check
LinuxCNC's checklist calls for but doesn't automate), (c) optional RTSan build (§2).
Run on a self-hosted runner nightly; skip gracefully elsewhere. This is the runtime
"rt check" counterpart to the §2 static checks.

**T5 — Hardware-in-the-loop (later).** Self-hosted runner with a real slave chain for
the FoE/SoE/EoE checkboxes and DC accuracy; manual trigger + nightly.

**Kernel mode guard.** No kernel unit tests needed: T1/T2 cover the shared core; the
kernel side is guarded by compile (CI below) since `master/kernel/` PAL headers are
thin inline wrappers.

---

## 4. CI

Current state: `.gitlab-ci.yml` is the inherited upstream pipeline (kernel modules vs.
RTAI/Xenomai matrices, docs, release) — **no job builds `--enable-uspace-master`**,
and the repo lives on GitHub (`sittner/ethercat`), where that pipeline doesn't run.
No `.github/workflows` exists.

Proposed `.github/workflows/ci.yml` (all container-friendly, no hardware):

| Job | Matrix | Purpose |
|---|---|---|
| build-uspace | gcc, clang × {libxdp, no-libxdp} | `--enable-uspace-master`, `-Wall -Wextra` (→ `-Werror` after F2) |
| build-kernel | 2–3 distro kernel-header sets (Debian 12/13, Ubuntu LTS) | guard the retained kernel mode + PAL split |
| check | gcc | `make check` (T1, later T2/T3a) |
| sanitize | clang ASan+UBSan | `make check` under sanitizers (would have caught F1) |
| rt-effects | clang-20 | §2 static RT check (once annotations land) |
| integration-net | gcc | veth/netns raw-transport test (T3b) |
| dist | gcc | `make distcheck` — proves VPATH/tarball builds, catches the missing-file class of packaging bugs |

Nightly (self-hosted, when available): T4 latency gate, RTSan, later T5.
Keep `.gitlab-ci.yml` untouched for upstream merges, or prune it — decide once; it
currently documents a pipeline that never runs here.

---

## 5. Prioritized roadmap

**P0 — correctness/safety, before any production deployment**
1. ~~Fix F1 (`lib/shared.c` UB)~~ — **done** (memcpy punning + `EC_WRITE_*`;
   round-trip and LE wire format verified at `-O2 -fstrict-aliasing`).
2. ~~F9: IPC socket permissions~~ — **done** (0660 fail-closed between
   `bind()`/`listen()`; `ec_master --socket-group`; docs updated).
3. ~~Triage the non-cosmetic warnings from F2~~ — **done**. The
   `-Warray-bounds` at `module.c:177` was a real out-of-bounds write:
   `ecrt_startup_master()` accessed `devices[EC_DEVICE_BACKUP]` without the
   `EC_MAX_NUM_DEVICES` guard, corrupting memory when a backup transport was
   passed to a default (`--with-devices=1`) build — now rejected/guarded.
   Also fixed: dead `size < 0` in `coe_emerg_ring.c` (replaced by a real
   allocation-overflow guard), `-Wpointer-sign` in `tool_api.c`, unused
   `ipc_strcpy`. `-Wuninitialized`/`-Wstrict-aliasing` were F1.
   Remaining warnings (100) are the cosmetic classes only
   (`-Wunused-parameter` 52, `-Wtype-limits` 18, `-Wstringop-truncation` 15
   — all NUL-safe on inspection, `-Wsign-compare` 14) → F2/P1.

**P1 — production hygiene**
4. ~~CI~~ — **done**: `.github/workflows/ci.yml` with build-uspace
   (gcc/clang × ±libxdp, `-Wall -Wextra`, unit tests), sanitize (ASan+UBSan),
   build-kernel (distro headers, guards the retained kernel mode), and dist
   (`make distcheck`). All four jobs validated locally first; distcheck
   immediately caught that the uspace *and* kernel `pal_*.h` headers were
   missing from tarballs (fixed — kernel tarball builds were broken since the
   PAL split).
5. ~~`make check` skeleton + T1 tests~~ — **done**: `tests/` with a plain-C
   harness and four programs (PDO print overflow regression, REAL/LREAL +
   `EC_READ/WRITE_*` semantics, datagram construction, CoE emergency ring).
   Mutation-checked: reverting `f5ed03a1` makes `test_pdo_print` fail under
   ASan with the historical stack-buffer-overflow.
6. F3: document the RT log-callback requirement; ring-buffer fallback.
7. F4/F5: document mlockall contract + failure modes; `PTHREAD_EXPLICIT_SCHED` for
   library threads; expose thread scheduling knobs.
8. ABI: set real `-version-info`, install uspace `.pc`/CMake config.

**P2 — depth**
9. ~~§2 `ECRT_RT_ATTR` annotations + rt-effects CI job~~ — **done** (embedder
   side): all 52 `rt_safe`-documented functions in `ecrt.h.in` carry
   `ECRT_RT_ATTR` (`__attribute__((nonblocking))` on clang ≥ 20, empty
   otherwise, `#ifndef`-overrideable for the framework hand-over);
   `ECRT_RT_TRUSTED_BEGIN/END` escapes provided. `script/rt-effects-check.sh`
   (pinned clang 22.1.8 via `script/rt-clang.sh`, `RT_CLANG` override) proves
   the contract with positive/negative/override self-test TUs
   (`tests/rt-effects/`), wired as the `rt-effects` CI job with a toolchain
   cache. The analysis is opt-in (`-Wfunction-effects`), verified inert in
   default clang builds. **Still open from §2**: transitive verification of
   the *implementation* — a probe (`clang -include include/ecrt.h
   -Wfunction-effects master/master.c`) shows 19 first-level diagnostics
   (`ec_master_queue_datagram`, `ec_log`, `ec_device_poll`, …), i.e. the
   mechanism works but needs the internal cyclic call tree annotated and the
   deliberate nonblocking syscalls trusted-wrapped. Also still open: propose
   `#define ECRT_RT_ATTR GOMC_NONBLOCKING` to LinuxCNC to retire its
   `ecrt_rt_api.h` shim.
10. T2 `transport_sim` + FSM/scan/PDO regression tests; T3 tool-IPC matrix.
    — **first stage done**: `tests/transport_sim.{c,h}` emulates the bus at
    datagram level (position/configured/broadcast addressing with correct
    working counters and BRD OR-semantics, per-slave register space, AL
    state machine ack, SII EEPROM read interface, DL-status port topology)
    behind the public `ec_transport_ops_t`; `test_sim_scan` boots a real
    master against 3 simulated slaves and verifies the complete scan
    (identities from SII, `slaves_responding`, link state) plus the
    startup/release/cleanup lifecycle — clean under ASan/UBSan/LSan.
    — **second stage done (domain exchange)**: the sim now serves an SII
    sync-manager category (per-slave SM2/SM3 process-data windows) and
    executes LRD/LWR/LRW through the FMMU pages the master writes to
    0x0600, with spec working counters (read +1, write +1, LRW write +2).
    `test_sim_domain` runs the full application life cycle — explicit PDO
    config, activate, real cyclic path — to OP with a complete working
    counter (LRW wc=3) and verifies process data round-trips both
    directions through the FMMU mapping; clean under ASan/UBSan/LSan.
    — **third stage done (mailbox/CoE)**: slaves can declare a CoE
    mailbox (SII words 0x0014-0x001C + SM0/SM1 category entries); requests
    written into the receive mailbox are answered from a per-slave object
    dictionary through the real mechanics (SM1 full bit at 0x080D polled
    via FPRD 0x808, fetch clears it). Expedited and single-segment normal
    SDO transfers are served; segmented transfers abort. `test_sim_coe`
    exercises the blocking SDO API end-to-end: expedited and normal
    up/downloads, OD round-trip verification, and the abort path
    (0x06020000 for unknown objects); the CoE-capable slave also takes
    the mailbox-scan and PDO-reading (0x1C1x) paths during startup.
    — **fourth stage done (CoE-based PDO assignment)**: the SII gains a
    General category advertising enable_pdo_assign/-configuration, so a
    combined mailbox+PD slave takes the fsm_pdo SDO write path during
    PREOP→SAFEOP. `test_sim_pdo_assign` verifies the exact mapping
    (0x1600/0x1A00) and assignment (0x1C12/0x1C13) object values the
    master writes, reaches OP with working process data, and — mutation-
    verified regression for `f381059c` — asserts that re-activating with
    an unchanged configuration issues zero further SDO downloads
    (reverting the fix yields 12 extra downloads and exactly that
    assertion fails).
    — **fifth stage done (link-down/rescan)**: `test_sim_link` covers
    startup with the link down (mutation-verified regression for
    `5dab28a0` — reverting it returns a false empty bus), idle-phase link
    loss/recovery with rescan and intact identities, and a full cable
    yank while activated: OP → link down → WC zero → link up → automatic
    rescan + reconfiguration back to OP with process data flowing.
    Next increments: DC (ARMW/FRMW), multi-slave domains, SDO Info
    service for `ethercat sdos`.
11. T4 RT smoke/latency gate (self-hosted).
12. F6/F7 lock/teardown hardening; F8 link-check relocation.
13. Docs refresh (FEATURES, TODO, stale checklist items); T5 hardware rig.
