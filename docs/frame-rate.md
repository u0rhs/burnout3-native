# Frame rate: how Burnout 3 steps time, and how to get 60 / uncapped without touching physics

Investigation fps1, 2026-11-02. Requirement (firm): same gameplay and physics as the PS2, frame rate never
limited: uncapped, or at least 60 FPS in races and crashes. Tags: **[C]** confirmed from a cited artefact, **[L]**
likely, **[G]** guess. Addresses are retail SLUS-21050; function names are the decomp's (`research/retail_names.csv`,
E-map tiers as noted); `0x4EE040` is the `CB3Game` object, so `CB3Game+0x2DA65` = `0x51BAA5` etc.

## Recommendation (short; updated after the proof, §4)

1. **"60 in crashes" works but is not bit-identical.** Forcing 60 in crashes (the PCSX2 crash patch's semantics as a
   hook) gives 60 FPS through every crash with the same 1/60 s ticks. But the per-tick hash proof **fails** on `races`
   and `crash1`. Once a crash has contacts and Aftertouch,
   the player car's state drifts apart at float-rounding level (7th digit) a few dozen ticks in, then grows, as crash
   physics does. navproof (two crashes, no Aftertouch) stays identical. The channel is state carried between ticks
   that depends on what runs between them [L]: contact vectors whose xyz agree but whose w lane differs. The PCSX2 patch
   on a real PS2 must do the same. So it is "same physics rules and step, different crash trajectories". It stays
   opt-in; whether to offer it (tagged gameplay-changing) is still open.
2. **Bit-identical 60 (and uncapped) needs the native renderer:** keep retail's tick schedule (2 ticks then 1 render
   in crashes) and present interpolated or extra frames render-side (§2 c). Any change to what the guest runs between
   ticks changes the crash physics.
3. **Races are already 60** in retail's schedule: one tick per frame outside crashes, split screen and menus (§1).
4. **Retail's catch-up on guest EE cycles** is not needed: overruns stayed ~0 even with 60 forced in crashes. It also
   fails the proof (it adds ticks after loading hitches, and retail's fixed-dt per-frame updates then fall out of step).
5. Changing the physics step itself (a tick per displayed frame with the host's frame time as dt) is not an option for
   parity.

## 1. How B3 steps time [decomp]

### The main loop: fixed 60 Hz ticks, N ticks per rendered frame

`func_00132600` (called forever from boot, `assembly/asm/d5/boot.s` 0x12ECE0, `a0 = 0x4EE040`; unnamed, it is the game's
per-frame update) in the in-game state (`CB3Game+0x2DA90 == 5`) does, per displayed frame [C asm
`assembly/asm/d5/unit_00131D20.s` lines 687-1464, Ghidra draft]:

```
UpdatePreSimulation()                         // 0x132090: pause; latches the half-rate request (below)
for (i = 0; i < N; i++) {                     // N = CB3Game+0x2DA84, decided at the end of the previous frame
    func_001D3D20();                          // CGtTimer::NextFrame [L E-map]: mguFrames++ (the global tick)
    CGtTimer::Update(game timer 0x4EF080); CGtTimer::Update(world timer 0x522660);
    input manager update (0x214050), stage->vtable+0x10 (the simulation: CB3OnePlayerStage::Update 0x135180 ...)
}
UpdatePostSimulation()                        // 0x131F90: sound; camera with dt = N * world tick dt
N = CGtFramerateManager::UpdatePostRenderWait(fm, min, max)   // 0x376570; (1,4), or (2,8) while half rate is active
render, PresentDrawBuffer
```

- **Tick length is fixed**: `PrepareTimerServices(60)` (NTSC; 50 on PAL) at construction, `mgrBaseTimestepSeconds = 1/60`
  [C `CB3Game::Construct` asm, `GtTimer.cpp` 0x1D3D30]. `CGtTimer::Update` (0x1D3A60) counts whole ticks and
  sub-ticks; `SetSlowMo(k)` (0x1D3A50, `field_24`) divides the tick into k sub-steps (world dt = 1/(60k)): Impact Time
  is a per-tick time scale, not a frame-rate change [C asm].
- **N is the only thing the frame rate changes.** The simulation runs N whole 1/60 s ticks per frame; a 30 FPS frame
  runs 2 of the same ticks. Physics never sees a different dt because of the frame rate [C loop structure; L that no
  per-tick code reads frame-rate state, settled by §3].
- Per *frame* (not per tick) are: the CB3Game RNG stir at the top of the loop (`CB3Game+0x2A640/0x2A644`, i.e.
  `0x518680`), `UpdatePostSimulation` (sound, cameras with `dt = N * tick`), the stage's vtable+0x14, render. Readers
  of that RNG named in retail's tables are frontend (`CB3MenuChoices::GetDriverCarID`,
  `RequestChampionshipGameMode`); 13 more readers are unnamed [C grep; G whether any is in-race]. These are the
  only paths by which "ticks per frame" could reach gameplay; §3 tests them.

### Who decides N: CGtFramerateManager

`UpdatePostRenderWait(fm, min, max)` (0x376570, `[L]` name from retail_names "direct+chain agree"), disassembled
from the ELF [C]:

```
now = CGtHiResSystemTimer::GetTime()           // 0x375280: 64-bit COP0 Count (EE clock, 294.912 MHz)
if (!fm->flag34) {                             // 0 offline
    fm->ticksDone += fm->lastN;
    if (fm->ticksDone <= 0) { ...; return min; }
    fm->elapsed += now - fm->last;
    late = fm->elapsed - fm->ticksDone * period  // period = 16.667 ms (D_004E1118) in EE cycles
} else late = -1;
N = min; if (late > 0) N = min + ceil(late / period);
if (N > max) { N = max; if (mode != 2) drop the excess time }   // beyond 4 (or 8) ticks: slow motion
if (!flag34) fm->lastN = N;
if (mode == 0) N = min;                        // mode 0: fixed, no catch-up
```

So retail is **adaptive with catch-up** (mode 1): a frame that overran gets extra 1/60 ticks next frame, keeping game
time on the wall clock; that is how the PS2 shows "60 with dips" without slowing the race. Mode (`CB3Game+0x2DA68`,
used because `+0x2DA6C` = 3) [C asm]:

| Mode | Set by | Meaning |
|---|---|---|
| 1 | `CB3Game::Construct`; `CB3OnePlayerStage::Update` when the player stops crashing; play mode / frontend flow | adaptive catch-up |
| 0 | `CB3OnePlayerStage::Prepare` 0x135280 (every single-player event starts in mode 0); `CB3OnePlayerStage::Update` when a crash starts | fixed N = min |
| 2 | online race logics (0x38E0F0, 0x38EAD0, 0x38AB10) | catch-up, excess never dropped |

[C asm; watch: mode went 1 -> 0 at the race start of race2, poll 2061]

Watch of race2's recording [C]:

| Poll (vblank) | Change | Meaning |
|---|---|---|
| 1 (259) | Count time 0, request/active 0/0, mode 1, N 1, crash flag 0, min vsync 2 | frontend |
| 2061 (4388) | mode 0, min vsync 1 | `CB3OnePlayerStage::Prepare`: the event starts, 60 |
| 3199 (5526) | crash flag 1, request/active 1/1, min vsync 2 | the Crash-mode junction impact |
| 3200 (5528) | N 2 | 2 ticks per frame from here |
| 4623 (6960) | crash flag 0 | `StopCrashing` |
| 4747-4750 (7206-7209) | request 0, mode 1, active 0, min vsync 1, N 1 | the stage's next update with the flag clear (124 polls later: the stage did not run its crash check in between [L: post-crash scoring]) |
| 4967 (7575) | min vsync 2 | back to the frontend |

### What forces 30

- **Crashes** (any mode, single player): `CB3OnePlayerStage::Update` (0x135180, retail_names high) reads byte
  `0x64EC9A` = `CB3RaceCar+0x18EA` of the car at `0x64D3B0`, which `CB3RaceCar::StartCrashing` (0x1519F0) sets to 1 and
  `CB3RaceCar::StopCrashing` (0x1519E0) clears [C asm]. While it is set and half rate is not active: mode = 0 and the
  half-rate request `CB3Game+0x2DA65` = 1; when it clears: mode = 1, request = 0. `UpdatePreSimulation` copies a
  changed request into `CB3Game+0x2DA66` (active) and calls `_rwDMAMinVsyncCntSet(2 or 1)` (0x1F4290, RenderWare's
  minimum vblanks per flip); active half rate makes the frame-skip call `(2,8)`. With mode 0 that is **exactly 2 ticks
  per frame, presented every 2 vblanks** [C asm]. The car at 0x64D3B0 is the player's [C race2: the watch-confirmed
  crash at poll 3200 is followed by the first 2-tick frame at poll 3201].
  So **Crash mode is 30 from the junction impact to the end of the crash, Impact Time included** (the approach is 60),
  and every player crash/wreck in a race is 30 for its duration. Takedowns of rivals are not (only the player's car is
  read) [C].
- **Split screen**: `CB3TwoPlayerSplitScreenStage::Prepare` (0x13CC70) sets the request to 1 for the whole event [C].
- **Menus / frontend**: `CB3Game::SetFrontendFrameRate(true)` (0x1D3EE0) sets the *tick rate* to 30
  (`PrepareTimerServices(30)`, 1/30 s ticks) and minimum vsync 2 [C]. Here the tick length itself changes, unlike in
  crashes.

### Time source: COP0 Count

`GetTime` reads COP0 Count, and `CGtHiResSystemTimer::Update(vblanks)` only adds whole Count wraps (vblank delta x
0.00114 >= 1). With Count standing still, the game's clock stands still, `late` is never positive, and **N is always
`min`**: the catch-up never fires; an overrun frame becomes slow motion instead [C code]. The only other `GetTime`
caller is `func_00441D70` (unnamed, two calls) [C grep].

### The PCSX2 patches (`PCSX2/pcsx2_patches` `patches/SLUS-21050_BEBF8793.pnach`, author Nehalem; read only)

| Patch | Writes | What it does in the decomp's terms |
|---|---|---|
| 60 FPS for Crashes | `0x1320D8 = 0x1000004B` | in `UpdatePreSimulation`, the `bne active, request` becomes `b 0x132208` (`_rwDMAMinVsyncCntSet(1)` every frame): half rate never becomes active, so the frame-skip call stays `(1,4)`. The stage still sets mode 0 at the crash, so N = **1 tick per frame**, every vblank. Tick length unchanged |
| 60 FPS for Menus | `0x1D3F2C = 0x1000000A` + FMV fix at 0x130DD8.. | in `SetFrontendFrameRate`, `beqz a1` becomes a branch to the 60 path: frontend ticks 1/60 s (not 1/30) and vsync 1; the FMV fix doubles a movie rate float |

[C disassembly of both sites]. Neither changes the simulation step; the crash patch changes ticks per frame (2 -> 1).
Side effects of the crash patch on a PS2: mode stays 0 after the first crash (the "crash ended" branch needs active
half rate), so the catch-up is off for the rest of the event, and a crash frame longer than 16.7 ms slows the game
down (§2 b).

## 2. Options, ranked

| # | Option | What it does to physics | How | Proof |
|---|---|---|---|---|
| a | **Force 60 in crashes** (PCSX2 crash-patch semantics) | Same ticks (1/60 s), same count, same per-tick inputs; only N per frame 2 -> 1. Per-frame code (RNG stir, cameras dt, stage +0x14) runs twice as often [C structure; G whether any feeds the simulation] | A hook at `UpdatePreSimulation` entry (0x132090) that clears the request byte 0x51BAA5; equivalent to the pnach without patching code. Cost: crash frames at 60 need ~1 tick + render per vblank; overruns become slow motion until (b) | §3 hash, stock vs hook, crash1 and races |
| b | **Retail catch-up restored** (remove overrun slowdowns) | Same ticks; N follows guest time like the PS2 (mode 1) instead of slowing down. With (a): replace the frame-skip controller 0x376570 by a host function: `N = 1 + floor(late / period)` from guest EE cycles (deterministic), max 4, plus swap interval 1 at 0x1F4290. Not by making Count run: mode 0 in crashes would still refuse catch-up | Small | §3 hash; plus game time per guest second = 60 ticks |
| c | **Uncapped via interpolation** | None: simulation stays 60 Hz fixed; the renderer draws at display rate between the last two ticks (transforms, camera), one tick (16.7 ms) of extra latency | The native renderer: native-renderer plan item 8 (draw list per tick, identity matching). Note: the simulation is **60 Hz**, not 30 as `native-renderer.md` §6.4 says; with (a)+(b) there is one draw list per tick | §3 hash unchanged by construction; visual check |

Order: a + b first (they are one change in practice), c with the native renderer.

## 3. Proving physics unchanged

Inputs are latched per poll and polls are per tick, so two replays of one `pads.log` give the simulation the same
input at every tick whatever the frame rate [tick = poll above]. Test:

1. At every poll, FNV-1a over listed guest ranges, one `T <poll> <hash>` line. Ranges: the race cars
   (`CB3RaceCar` from 0x64D3B0, size from the decomp), `CB3PlayerCarPhysics` 0x57BAF0, the world timer 0x522660 (44
   bytes), the traffic and AI managers, the CB3Game RNG 0x518680 (8 bytes, reported separately: it is stirred per
   frame by design and is the one expected difference; what matters is whether the simulation ranges differ).
2. Replay `crash1` and `races` twice: stock, and with the hook. The replay checker will report
   other vblanks per poll (expected); compare the T lines: **identical hash at every poll = identical simulation state
   at every tick**. A first differing poll names where per-frame work leaks into gameplay (then: which range, which
   function, from the watch).
3. Same for (b) with an artificial overrun (e.g. a host-side delay of the guest's vblank on chosen frames) to check
   that catch-up ticks are the same ticks.

## 4. Proof results (2026-11-02)

**Implementation (opt-in):** forcing 60 in crashes (CRASH60 below) clears the half-rate request `0x51BAA5` at `CB3Game::UpdatePreSimulation`
entry (0x132090). The latch never changes, so there is 1 tick per frame and the minimum vsync stays 1. A swap-interval
override is not needed and would be wrong: the menus' 30 Hz ticks would then run at double speed. The catch-up (CATCHUP) replaces
`UpdatePostRenderWait` (0x376570) with retail's algorithm, ported from the ELF, on guest EE cycles, without the mode-0
override. The hash: per poll (= per tick), a hash per page of guest RAM.

**Runs** (replays of the recorded sessions) [C]:

| Check | Result |
|---|---|
| Options off | navproof 5,517/5,517 polls exact, GS stream IDENTICAL over 6,681 frames; races 21,011/21,011, crash1 35,635/35,635 exact; two stock runs give identical tick hashes at every page and poll (the hash is deterministic) |
| CRASH60 frame rate | every crash at 60: half-rate frames 7,303 -> 0 (crash1), 2,156 -> 1 (races), 269 -> 0 (navproof); overrun frames next to 60-FPS play 17 -> 11, 12 -> 6, 3 -> 1 |
| CRASH60, navproof (two crashes, no Aftertouch input) | `CB3PlayerCarPhysics` (0x57BAF0, 8-byte words): **identical** except 6 words: contact scratch (+0x1A8..+0x1BF, +0x568, +0x5A8) that differs only at polls that are a stock 2-tick frame's second tick and reconverges; a counter word at +0x1410; a heap pointer at +0x11C0. Traffic state (0x53Exxx: 13.4112 m/s = 30 mph) integrated once per frame with dt = N ticks agrees at every shared frame boundary and drifts in the 4th digit afterwards. Render, RenderWare, sound, HUD, camera and CB3Game frame state differ, as expected |
| CRASH60, races (crash at poll 6265, stick hard right + Aftertouch) | **differs**: the w lane of contact vectors in another car's physics (slot 0x584610 +0x1A0) differs from poll 6273 (xyz equal); the player's position differs from poll 6304 in the 7th digit (2367.1270 vs 2367.1272), then broadly (984 of 3,166 32-byte blocks) |
| CRASH60, crash1 (Crash-mode junction at ~2900, Aftertouch) | **differs** from poll 3313; the session then leaves the recorded path (32,537 of 35,635 polls reached) |
| CRASH60 + CATCHUP, navproof | catch-up fires at boot (one tick), after loading hitches (2-4 ticks) and at the race intro; the player's physics differs from poll 1206 (race start) |

**Why [L/G].** Not the timers: the world timer (incl. Impact Time slow motion) is identical at every poll [C watch].
Not the deformation: `CB3CrashActionDirector::Update` (which drives `CB3VehicleDeform`) runs from
`CB3PhysicsManager::Update`, i.e. per tick [C asm]. What differs first are w lanes of contact vectors whose xyz agree,
then rounding-level drift. That matches state carried from one tick into the next which the code in between
overwrites: VU0 register lanes, scratchpad or stack leftovers. With 2 ticks per frame, tick 2 inherits tick 1's
leftovers; with 1, it inherits the renderer's [G mechanism]. If so, no host-side change that runs the guest renderer
between ticks can be bit-identical. A real PS2 with the PCSX2 patch behaves the same. The narrower fix (find the read of
leftover state and pin it) would change stock behaviour too.

**Menus.** The pnach menus patch makes `SetFrontendFrameRate` take the 60 path: frontend ticks
of 1/60 s instead of 1/30 s, vsync 1. Frontend states run no race simulation: the world is released and the stage
update runs only in game state 5 [C `func_00132600` draft]. Timers count ticks x tick length, so their seconds stay the
same [C `CGtTimer::Update`]. What can change: anything counted in frames instead of time (the pnach's FMV-rate fix
shows the movie player is one [C pnach]), and the CB3Game RNG stirred once per frame, which `CB3MenuChoices` reads
(`GetDriverCarID`, `RequestChampionshipGameMode` [C names]), so random picks differ, as they already do with any
change in menu timing. Not implemented. Easy as a hook at 0x1D3EE0 (`a1 = 0`), but it needs the FMV check.

## Evidence and tools

- Decomp: `assembly/asm/d5/unit_00131D20.s` (main loop, `UpdatePreSimulation`, `Construct`), `play_transition.s`
  (`CB3OnePlayerStage::Update/Prepare`), `d4/gamemode.s` (split screen), `B3RaceCar_001519F0.s`, ELF disassembly of
  0x376570 and 0x375280 (objdump `-m mips:5900`), Ghidra drafts (reading aid).
- Sessions: `{races,race2,crash1,navproof,menu}/pads.log`.
- Watch: race2's recording, on 0x4E2E40:8, 0x51BAA5:2, 0x51BAA8, 0x51BAC4:4, 0x64EC9A, 0x4E17DC.
