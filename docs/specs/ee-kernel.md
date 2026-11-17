# Spec: the EE kernel model (handlers, semaphores, threads, alarms, virtual time, DMAC delivery)

Two parts: the kernel objects and threads, and virtual time and interrupt delivery. Interface and surrounding code:
`src/native/platform.h` (the declarations and the row table), `src/native/platform_sif.h`,
`include/b3host/native_backend.h`, `include/b3host/call.h`. Sources: the public PS2SDK headers
(`ee/kernel/include/kernel.h`: `ee_sema_t`, `ee_thread_t`, `ee_thread_status_t`, the `THS_*` / `TSW_*` values),
ps2tek (EE timing, INTC / DMAC registers), and the decomp's `libkernl.s` for the syscall stubs.

Everything here is observable by the game: return values land in guest registers and RAM (ids are stored in game
objects), the order of handler calls and thread switches decides which code runs first, and the virtual cycle at
which each event is delivered is compared line by line by `B3NATIVE_TIME_TRACE` against the golden runs. Many rules
below are **not** what a real PS2 kernel does; they are the model the golden runs were recorded with. Keep them.

## 0. Where it lives

- Kernel objects and threads: `src/native/kernel_model.h` (`struct KernelModel`: handler lists, `add`, `remove`,
  `create_sema`, `create_thread`, `start_thread`), `src/native/platform_kernel.cpp` (the kernel syscalls: handler
  syscalls, EnableIntc / Dmac, CreateSema, SetGsCrt, GsPutIMR, the OSD word, DIntr / EIntr, the restore hooks),
  `src/native/platform_threads.cpp`, and `tools/native/boot_oracle.h` `MiniPlatform::syscall` (the dev oracle's copy
  of the kernel rules) with its handler dispatch.
- Virtual time and interrupt delivery: `src/native/platform_time.cpp` (the frame scheduler, virtual time, alarms,
  VBlank / DMAC delivery, idle) and `src/native/dmac_delivery.h` (HwModel's deferred DMAC completion queue, a member
  of `class HwModel` in `src/native/hw_model.h`: `clock`, `has_dmac_handler`, `kDmacCompletionDelay`,
  `completion_delay`, `finish_dma`'s queueing, `check_dmac_interrupts`, `check_deferred`, the pending list and its
  restore hooks), with the GS reset values, `set_dmac_mask` and `spr_dma`.

Around it (not specified here): `platform.h` (declarations, the row table), `platform_sif.*`, `platform_charge.h`,
`platform_iop.cpp`'s SIF start-up stubs, `lgKbdGetLedState`, `sceVibSetActParam`,
`Release__24CB3RaceCarPhysicsAttribs`, `sceScfGetAspect`, `SyncDCache`, `platform.cpp`'s `_InitSys` (decomp-based;
it calls CreateSema / CreateThread / StartThread / ChangeThreadPriority through the rows), `platform_malloc.cpp`
(EndOfHeap's value is specified in §9), HwModel's other registers, DMA kick / PATH3 / VIF0 logic, `cpcond0`,
`vblank_start`.

## 1. Conventions

- A row is `void f(const b3host::CallArgs& a, b3host::CallResult& r)`: arguments in `a.gpr[0..3].w[0]` (a0..a3, the
  EE ABI), the result in `r.v0` as the 32-bit value sign-extended to 64 bits (`r = CallResult{}` first). Rows listed
  in platform.h's table by retail address (e.g. AddIntcHandler 0x10BB00, iSignalSema 0x10BE50, SetAlarm 0x10BBA0).
  The `i`-prefixed (interrupt-context) variants share the row of their plain form.
- "fatal" = `stub_unexpected(name, why)` (throws; the run ends with the reason).
- The game's gp, recorded with handlers and alarms: 0x004E8670.
- "preemption check" = `maybe_preempt()` (§5.4). "isr depth" = the counter `isr_enter / isr_exit / isr_depth`.
- "interrupts enabled" = the native backend's `interrupts_enabled()` (COP0 Status IE as the guest set it).

## 2. Handler lists (INTC and DMAC)

Two independent ordered lists of records {id, cause (INTC cause or DMAC channel), handler address, argument, gp}.
- **Id**: the smallest integer in 1..128 not used by a record of *that* list; none free → the call returns −1 and
  nothing is added. Ids are reused after removal.
- **Placement** by the `next` argument (a2): 0 → immediately before the first record with the same cause (at the end
  if there is none); 0xFFFFFFFF → immediately after the last record with the same cause (at the end if none); any
  other value → immediately before the record whose id equals it, whatever its cause (at the end if none).
- AddIntcHandler (a0 cause, a1 handler, a2 next, a3 argument) → the id. RemoveIntcHandler (a0 cause, a1 id): the record
  matching both is removed → 0; else −1.
- AddDmacHandler / AddDmacHandler2: as INTC on the DMAC list; channel > 9 is fatal. RemoveDmacHandler: channel > 9 →
  −1, else as INTC.
- `note_libkernl_timer_handler()`: when the INTC list is empty, adds {cause 11 (TIM3), handler 0x00115D00, next 0,
  argument 0} — retail's `_InitSys → InitTimer` registration, so the game's VBlank handler later gets id 2.
- `intc_handlers()` returns a copy as `IntcHandler{id, cause, handler, argument}`; `dmac_handler_exists(ch)`.
- `KernelModel` keeps public fields used elsewhere (keep them or update the users: native_boot.cpp, boot_oracle.h,
  platform_restore.cpp, platform_movie.cpp, platform_rwa.cpp, tests): `intc`, `dmac` (records with fields `id cause
  handler argument gp`), `intc_mask`, `dmac_mask`, `wakeups`, `vblanks`, `main_priority`, `semas` (`id count max attr
  option`), `threads` (`id entry stack stack_size gp priority started argument`), and the functions `add(list, cause,
  handler, next, argument, gp)`, `remove(list, cause, id)`, `create_sema(count, max, attr, option)`,
  `create_thread(entry, stack, size, gp, priority)`, `start_thread(id, argument)`.

## 3. Masks

- EnableIntc(cause) / DisableIntc(cause): cause > 31 fatal. Returns 1 when the cause's enabled state changes (Enable
  of a disabled cause, Disable of an enabled one), else 0; then sets / clears it in `intc_mask`.
- EnableDmac(ch) / DisableDmac(ch): ch > 9 fatal. Same 1 / 0 rule on `dmac_mask`; then the hardware model's D_STAT
  mask half (bits 16..25) is set to exactly the enabled channels (`set_dmac_mask`), which may deliver a pending DMAC
  interrupt at once (§8).
- DIntr (retail func_00114E60) / EIntr (0x114EB8): return 1 if interrupts were enabled before the call, else 0; then
  disable / enable them.

## 4. Semaphores

Kept in the platform (not in guest memory). Parameter block = `ee_sema_t` [PS2SDK kernel.h]: +4 max_count,
+8 init_count, +16 attr, +20 option (+0 count and +12 wait_threads are not read).
- CreateSema(p): p not 4-aligned → fatal. Id = the smallest unused integer in 0..255 (−1 if none); count = init_count.
  Returns the id.
- WaitSema(id): unknown → −1. count > 0 → count − 1, returns 0. Else the caller blocks: fatal when isr depth > 0 or
  interrupts are disabled; the thread is appended to the semaphore's FIFO of waiters and the scheduler runs another
  thread (§5.3); the call returns the wake result (0 when woken by a signal, −1 when the semaphore was deleted).
- PollSema / iPollSema(id): if the semaphore exists with count ≤ 0, interrupts are enabled and isr depth is 0, the
  pending SIF completions are serviced first (`sif::sif_service()`) and the semaphore looked up again. Then: unknown or
  count ≤ 0 → −1; else count − 1 and the **id** is returned.
- SignalSema / iSignalSema(id): unknown → −1. If a thread waits on it (the FIFO's first entry that is still waiting
  on this id; stale entries skipped and dropped), that thread becomes ready (back of its queue), its wake result 0,
  the count unchanged, returns 0, preemption check. Otherwise: count ≥ max → −1; else count + 1, returns 0.
- DeleteSema(id): unknown → −1. Else it is removed; every thread still waiting on it, in FIFO order, becomes ready
  with wake result −1; returns 0; preemption check.
- `kernel_signal_sema(id)`: SignalSema for platform code (file-system completions); true when it returned 0.

## 5. Threads and the scheduler

### 5.1 Thread records
Status values as `ee_thread_status_t` [PS2SDK]: running 1, ready 2, waiting 4, dormant 0x10. Wait types: sleep 1,
semaphore 2. Per thread: id, current and initial priority, status, wait type and semaphore id, wake result, wakeup
count, entry, stack, stack size, gp, argument.
- The main thread, id 1, exists implicitly from the first query: running, entry 0x00100220, stack 0x01FF0000, size
  0x10000, gp 0x004E8670 (crt0's SetupThread arguments, decomp crt0.s), current priority = `KernelModel::main_priority`
  (initially 0), initial priority 0. Its priority and wakeup count are mirrored into `KernelModel::main_priority` /
  `wakeups` whenever they change (the boot oracle reads them).
- CreateThread(p): p not 4-aligned → fatal. `ee_thread_t`: +4 func, +8 stack, +12 stack_size, +16 gp_reg,
  +20 initial_priority. Id = the smallest unused integer in 2..255 (−1 if none); dormant; current = initial priority.
  The `KernelModel::threads` record is added too (`started` false).
- StartThread (`_StartThread`, a0 id, a1 argument): −1 when unknown, not dormant, priority outside 0..127, or the
  thread's host context is still alive (§5.5). A thread with stack 0 is fatal. Stores the argument, marks the
  KernelModel record started, makes it ready (back of its queue), returns the id, preemption check.
- GetThreadId (and the game's func_002372F0): the running thread's id.
- ChangeThreadPriority(id, prio): id 0 = the running thread. −1 when unknown, dormant, or prio (unsigned) > 127.
  Returns the previous priority. A ready thread whose priority changes moves to the back of the new priority's queue.
  Preemption check (so a running thread that lowers itself below a ready one yields).
- SleepThread: a pending wakeup is consumed (count − 1) and 0 returned at once. Otherwise fatal inside a handler;
  the thread waits (sleep) and the scheduler runs another; returns 0 when woken.
- WakeupThread / iWakeupThread(id): −1 when unknown or dormant. A sleeping thread becomes ready (back); any other
  thread (running, ready, waiting on a semaphore) gets its wakeup count + 1 (saturating at 0xFFFFFFFF). Returns 0,
  preemption check.
- TerminateThread / iTerminateThread(id): −1 when unknown, the caller itself, or dormant. The main thread is fatal.
  Otherwise the thread becomes dormant (§5.2) and, if its host context is alive, it is unwound before the call
  returns (§5.5). Returns 0.
- DeleteThread(id): −1 when unknown, the caller, not dormant, or its host context still alive. Removes the thread
  (and its KernelModel record); returns 0.
- ExitThread / ExitDeleteThread (the same row): fatal in the main thread; otherwise the caller ends: dormant (§5.2),
  the next ready thread runs. (ExitDeleteThread does not delete the record in this model.)
- ReferThreadStatus / iReferThreadStatus(id, p): id 0 = the running thread; −1 when unknown or p not 4-aligned.
  Writes 12 words at p (the `ee_thread_status_t` layout): status, entry, stack, stack size, gp, initial priority,
  current priority, attr 0, option 0, wait type (0 when not waiting), the semaphore id when waiting on one (else 0),
  the wakeup count. Returns 0.

### 5.2 Becoming dormant
Removed from every ready queue and every semaphore's waiters; wait cleared; priority reset to the initial one;
wakeup count 0. (Applies to TerminateThread, ExitThread and an entry function returning.)

### 5.3 Scheduling rules
- 128 ready queues, priority 0 the highest; FIFO within a priority. A thread made ready by StartThread, WakeupThread,
  SignalSema or DeleteSema goes to the **back**; a running thread preempted by a higher one goes to the **front**.
- The running thread is replaced only when a ready thread has a **strictly** higher priority (lower number); equal
  priorities never rotate.
- When the running thread blocks (WaitSema, SleepThread) or ends, the highest-priority ready thread (front of the
  first non-empty queue) runs. If none is ready: fatal when interrupts are disabled; otherwise repeat {service pending
  SIF completions; if a thread became ready, run it; else one idle step (§6.6)}; 600 rounds without a ready thread is
  a deadlock: fatal with the waiting thread's id.
- A thread whose entry returns becomes dormant; the next ready thread runs; if none, VBlanks are delivered until one
  is (600 → fatal).

### 5.4 Preemption check (`maybe_preempt`)
Does nothing when isr depth > 0, when only the main thread exists, when the current thread is not running, or when
interrupts are disabled. Otherwise, if the best ready thread has a strictly higher priority, the current one goes to
the front of its queue and the other runs. Called after the kernel calls named above and at the end of interrupt
delivery (`time_check`, the VBlank wait loops, the idle loop). Never inside a handler.

### 5.5 Host execution model (implementation requirements)
- Each guest thread runs on its own host thread; exactly one guest thread runs at any time (a baton passed under a
  mutex / condition variable), so the run is as deterministic as with one thread.
- At a switch the backend's guest stack pointer is saved / restored per thread; a new thread starts with
  sp = (stack + stack size) rounded down to 16 and calls its entry with a0 = the argument.
- b3asm's register file (`b3asm::state()`, thread_local) is per thread. The interrupt-enable state is shared.
- POSIX hosts: guest host threads get an 8 MB stack (macOS defaults to 512 KB for secondary threads); Windows keeps
  `std::thread`'s.
- A terminated thread parked in its own code is unwound by an exception on its host thread before the terminator
  continues; an error thrown in a non-main thread is carried to the main thread and rethrown there.
- `restore_thread(RestoredThread, resume)` / `thread_wake_result(id)` (dev restore of a snapshot): the thread table,
  queues (in call order) and waiters set to the given states; a restored thread runs `resume` instead of its entry
  when it first gets the baton.

## 6. Virtual time and VBlanks

### 6.1 Clock
`now` = the native backend's cost counter (each dispatched native call adds the static cost of the original) +
`skipped` (time the platform jumped over while waiting). `virtual_cycles()` returns it.
- VBlank n (n = 1, 2, ...) starts at n × 4,915,200 EE cycles (294.912 MHz / 60 exactly). `vblank_count()` = VBlanks
  delivered; `next_vblank_start(t)` = the first VBlank start ≥ t on the current grid.
- `arm_virtual_time(nb)` / `rearm_virtual_time()` / internal re-arming: the backend's next time event is
  min(next VBlank start, the extra source's next event, the earliest alarm deadline, the gate wake-up if armed, the
  SIF layer's next ready time, the hardware model's next completion ready time), or 0 (= at the next dispatched call)
  when an IOP→EE reply is pending; stored relative to `skipped`; the backend calls `time_check()` when it is reached.
- `set_extra_time_source(next, due)`: one extra event source (RW Audio's IOP executor).
- `restore_virtual_time(ee_cycles, next_vblank)`: sets `skipped` so that now = ee_cycles (fatal if below the cost
  counter) and the next VBlank; `restore_alarm(...)` appends an alarm as given.

### 6.2 VBlank delivery (one VBlank)
In this order: if `B3NATIVE_MAX_VBLANKS` = L > 0 and L VBlanks were already delivered, throw `VBlankLimit`; count it;
advance the next VBlank start by one period; re-arm; `hw_model().vblank_start()`; `sif::sif_service()`; INTC
dispatch of cause 2 (VBlank start), then cause 3 (VBlank end); every VBlank hook in registration order
(`add_vblank_hook`); `sif::pad_vblank()`; `sif::sif_service()`.

### 6.3 INTC dispatch of one cause
Only when the cause is enabled and at least one INTC record has that cause. The hardware model's INTC status bit is
acknowledged first (`ack_intc`). Then, on a copy of the list taken before the first call (handlers may add / remove),
with interrupts disabled and isr depth + 1, each record of that cause is called in list order as
`handler(cause, argument)` (int return ignored). Afterwards the interrupt state is restored and, if it was enabled,
`hw_model().poll_interrupts()` runs (DMAC interrupts raised meanwhile).

### 6.4 Alarms
- SetAlarm / iSetAlarm(a0 ticks (low 16 bits), a1 handler, a2 common): id = the smallest unused integer in 1..64;
  −1 when the handler is 0 or none is free. Deadline = now + max(ticks, 1) × 18,750 cycles (saturating at 2^64 − 1).
  Returns the id; re-arm.
- ReleaseAlarm / iReleaseAlarm(id): a pending alarm with that id is removed → 0; else −1.
- Delivery of one alarm: only when isr depth is 0 and some alarm's deadline ≤ now. The alarm with the smallest
  deadline is taken (the earliest set among equal deadlines), **removed first and the time event re-armed** (before
  the handler runs: re-arming after it misses an event that falls due inside the handler; not re-arming delivers a
  SIF reply a VBlank early), then called as
  `handler(id, ticks, common)` with interrupts disabled and isr depth + 1; the interrupt state is restored after.
  An alarm does **not** wait for interrupts to be enabled.

### 6.5 `time_check()` (the backend's time event; not re-entrant: a nested call returns at once)
1. If an IOP→EE reply is pending and interrupts are enabled: `sif::sif_service()`.
2. If the gate wake-up time has passed: clear it, re-arm.
3. If the extra source's next event ≤ now: run its `due`, re-arm.
4. Loop, re-reading now each round:
   a. an alarm is due (deadline ≤ now) and its deadline ≤ the next VBlank start → deliver it, repeat;
   b. the hardware model's front completion is ready (≤ now) and ≤ the next VBlank start → `poll_interrupts()`,
      re-arm; if the queue length changed, repeat;
   c. now ≥ the next VBlank start and interrupts enabled → deliver the VBlank (§6.2), repeat;
   d. an alarm is due → deliver it, repeat;
   e. stop.
5. Preemption check, **after** the re-entry guard is cleared. "Nested" means a call on the same host thread inside
   this one: a thread switch at step 5 parks this host thread, and the next thread's time events must still run.
   Neither a clock jump nor the end of `time_check` re-arms.

`gate_latched()`: the gate wake-up = now + 64 cycles; re-arm (a time event only; it releases nothing).

### 6.6 Waiting for time
- `vblank()` (a thread waits for the next VBlank: SleepThread idle, VSync polls): fatal when interrupts are disabled.
  Until one more VBlank than at entry has been delivered (by anyone): if isr depth is 0 and the earliest alarm is
  before the next VBlank start → jump the clock to its deadline (when in the future), deliver it, preemption check,
  continue; else if isr depth is 0 and the front DMAC completion is ready before the next VBlank start and not after
  the earliest alarm → jump the clock to its ready time (when in the future), `poll_interrupts()`, re-arm; if the
  queue moved, preemption check and continue; otherwise jump the clock to the next VBlank start (when in the future),
  deliver it, preemption check.
- `idle_step()` (no thread ready): if isr depth is 0 and the earliest alarm is before the next VBlank start and not
  after the front completion's ready time → jump to it and deliver it, preemption check; else the due-completion step
  above (preemption check if it moved); else `vblank()`.
- `skip_to_next_event()` (native_play's busy-wait hook; never used in proof runs): false when isr depth > 0 or
  interrupts are disabled. Otherwise jump the clock to the earliest of {next VBlank start, gate wake-up, earliest
  alarm, front completion ready, SIF next ready} when in the future, run `time_check()`, return true.

## 7. DMAC interrupt delivery to handlers
`dmac_interrupt(ch)` (the hardware model's `on_dmac_interrupt`): returns false (not delivered) when interrupts are
disabled or isr depth > 0. More than 200,000 deliveries since the last VBlank is fatal (a handler restarting
transfers forever). Otherwise: acknowledge the channel's D_STAT status bit, log `'i'` (`dma_log`), then on a copy of
the DMAC list, with interrupts disabled and isr depth + 1, call each record of that channel in list order as
`handler(channel, argument)`; afterwards interrupts enabled; returns true.

## 8. The hardware model's DMAC completion queue

- A finished transfer on channel c: CHCR.STR cleared, QWC 0, D_STAT status bit c set, `on_complete(c)`; when the model
  has a clock, a completion {c, ready = clock() + delay(c)} is appended to a FIFO queue, delay 32 cycles for channels
  0..7 and 0 for the scratchpad channels 8 and 9; then the interrupt check.
- Interrupt check without a clock (unit tests): repeatedly take the lowest channel whose D_STAT status and mask bits
  are both set and hand it to `on_dmac_interrupt`; stop when none is left or one is refused. Not re-entrant.
- Interrupt check with a clock (the platform): repeat { move every completion at the queue front whose ready time ≤
  clock() into the pending list, appending its channel only if not already in it; scan the pending list in order for
  the first channel that is enabled in D_STAT's mask half and (when `has_dmac_handler` is set) has a handler; remove it
  and hand it to `on_dmac_interrupt`; if refused, put it back at the same position and stop; if nothing was delivered,
  stop }. Not re-entrant (a nested check returns at once).
- `next_completion_ready()` = the queue front's ready time (2^64 − 1 when empty); `queued_completions()`;
  `rebase_completions(from_now, to_now)` keeps each entry's remaining delay on another clock;
  `restore_pending_dmac`, `restore_completion` set state without side effects.
- `set_dmac_mask(channels)`: D_STAT bits 16..25 = channels, low half unchanged; then the interrupt check.
- Scratchpad DMA, normal mode: fromSPR (channel 8) / toSPR (9) move QWC quadwords between RAM at MADR (low 4 bits
  ignored, below 0x10000000 or fatal) and the scratchpad at SADR & 0x3FF0 (wrapping within the 16 KB scratchpad),
  quadword by quadword; afterwards MADR and SADR advance by 16 × QWC (SADR wrapping). Chain / interleave modes are
  fatal [ps2tek DMAC: D8 / D9 registers].
- GS after reset: CSR = 0x551B0000 (revision / id field), IMR = 0x7F00. GsPutIMR(a0): stores a0 (low 32 bits) as the
  IMR and returns the previous IMR as a 64-bit value.

## 9. Other kernel calls
- SetGsCrt(interlace, mode, ffmode): stored (as 16-bit values) for the display; no guest memory written; logged.
- GetOsdConfigParam(p): p not 4-aligned → fatal; writes the OSD configuration word to p; v0 = 0. The word is 0x00012010
  unless `B3NATIVE_OSD_CONFIG=<hex>` gives another (read once). GetOsdConfigParam2(p, n, offset): n > 256 → fatal;
  n zero bytes at p; returns 0.
- EndOfHeap (platform_malloc.cpp, own code, value required): 0x01FFFD60 — the main thread's initial stack pointer
  (stack top 0x02000000 less 0x2A0). The guest heap layout of every golden run depends on it.

## 10. The boot oracle's syscall model (`tools/native/boot_oracle.h`, dev only, compiled into native_play)
`MiniPlatform::syscall` answers the syscalls of guest code run in the mini interpreter with the same rules, for its
subset: GetThreadId → 1; EndOfHeap → 0x01FFFD60; WaitSema / SignalSema → never contended (return the id); the handler
lists (§2); _EnableIntc / _DisableIntc / _EnableDmac / _DisableDmac (§3, return values as there; Dmac also updates
its HwModel's mask); CreateSema (§4); CreateThread (§5.1, no scheduling); StartThread (`KernelModel::start_thread`:
the id when found, not started and priority 0..127, else −1); ChangeThreadPriority of the main thread only (others
fatal); FlushCache 0; SetGsCrt nothing; GsPutIMR (§8); WakeupThread of thread 1 only (counts a wakeup). The handler
dispatch of its interrupts mirrors §6.3 / §7 (list order, acknowledge first).

## 11. Performance constraints
- The rows run on every kernel call (thousands per frame for WaitSema / SignalSema / iSignalSema in file and audio
  paths): no allocation on the common paths (the lists may allocate when they grow), no logging unless a diagnostic
  variable is set, no `dynamic_cast` per call beyond what the baseline does.
- `time_check` and `arm_time` run at every time event; keep them O(alarms + queued completions) (both are small).
- A thread switch stays one mutex / condition-variable handoff.
- Gate: unpaced race rate within 2 % of the baseline (median of 3).

## 12. Acceptance checks

1. `make test`: `test_native_platform` (crt0 / IOP start-up stubs, `vblank_sleep_wakeup`, `hw_model_rules`,
   `hw_model_dmac_completion_delay`, `hw_model_spr_dma`, `hw_model_path3_gate`, `hw_model_cpcond0`,
   `malloc_family_against_retail`), `test_native_backend`, `test_calls`.
2. New unit tests for every rule of §2–§9 with hand-written expectations (ids after add / remove / reuse, placement
   for next = 0 / −1 / id, every −1 case, FIFO waiters, signal at max, delete with waiters, priority preemption only
   when strictly higher, front / back queueing, alarm order for equal deadlines, alarm before VBlank at equal time,
   DMAC pending-list order and refusal).

Against the previous build (the baseline; tools/golden):
3. Every scenario and tour (CONTRIBUTING.md, "Checks a change must pass") with `STRICT=1`, `B3NATIVE_TIME_TRACE` over every kernel row address
   in platform.h (0x10BB00 0x10BB20 0x10BB30 0x10BB40 0x10BB50 0x10CA38 0x10C9D0 0x10C968 0x10C900 0x10BA20 0x10C160
   0x10BE20 0x10BE30 0x10BE40 0x10BE50 0x10BE60 0x10BE70 0x10BE80 0x10BC20 0x10BC30 0x10BC40 0x10BC50 0x10BC60
   0x10BC70 0x10BC80 0x10BCB0 0x10BD10 0x10BD20 0x10BD30 0x10BD40 0x10BD50 0x10CCD0 0x10BBA0 0x10BBB0 0x10BC00
   0x10BC10 0x114E60 0x114EB8 0x10BED0 0x10C130 0x2372F0) and `B3NATIVE_DMA_LOG`, `B3NATIVE_SIF_LOG`,
   `B3NATIVE_RWA_LOG` on: every run PASS, every trace and log equal to the baseline's line for line, every shot
   equal (photo's free-camera shots excepted).
4. `B3NATIVE_RAM_HASH=1-23400:100` equal in race, crash, multiplayer and nfsdemo (no ignore ranges needed: the
   kernel model keeps no state in guest RAM).
5. `save_check.sh` PASS; the dev tools that use the oracle's model (`native_boot`, `mpeg_play`, a non-strict
   native_play run with the fallback) behave as with the baseline on their usual inputs.
6. The Windows build (`tools/native/build_win.sh`) and the macOS / Linux arm64 builds compile (the host-thread code
   is platform-specific).
