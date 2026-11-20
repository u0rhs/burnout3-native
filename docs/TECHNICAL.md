# How the native port works

This page explains how Burnout 3: Takedown (PS2, NTSC-U SLUS-21050) runs as a native program on Windows, Linux
and macOS, and how each claim can be checked. Every number here comes from a file in this repository or from a
check you can run yourself with your own disc. The headline numbers live in [`docs/status.json`](status.json),
which the README renders. The figures below are as of 2026-11-14; where they differ, `status.json` is the
current one.

## Summary

- **The game's code is C++ compiled ahead of time** for x86-64 and arm64. It is generated from a matching
  decompilation of the game: 5,652 game functions, of which 4,655 are rebuilt byte for byte and 997 are complete C
  that compiles to different bytes. There is no emulator and no JIT. A strict mode halts if any function would need
  the original PS2 code, and all 20 scripted scenarios pass with it. The game binary contains no interpreter for
  PS2 code ([section 1](#what-is-in-the-binaries)): a function missing from the native build stops the game
  with a message naming it, instead of running the original code. The renderer library alone has a small VU1
  interpreter that runs the game's particle graphics program for the presentation of smoke, dust, glass and fire.
- **The game keeps its own memory layout.** Its data lives in a 32 MB "guest RAM" at the PS2's addresses, and
  pointers inside game structures stay 32-bit guest addresses. Structures, assets and saves therefore stay exactly
  as the game knows them.
- **PS2 floating point is reproduced bit for bit** by a software model of the Emotion Engine's non-IEEE arithmetic
  (round toward zero, no denormals, no infinities or NaNs, clamping), together with the game's own maths library
  ported to give the same bits.
- **Below the game code is a platform layer of our own** (kernel, timers, files, pads, memory card, sound, movies),
  written from our specifications. No IOP code runs, no graphics chip is emulated and no DMA chain is walked. A
  **native renderer** on SDL3 GPU (Vulkan, Metal) draws each frame from the game's scene state.
- **Behaviour is checked against a recording.** For 20 scenarios covering every race mode and every front-end path,
  each run's kernel timeline, DMA, SIF and audio logs, guest RAM hashes and screenshots match a golden recording
  item by item: 140 items in all.

## Contents

1. [What "native" means here](#1-what-native-means-here)
2. [How the game code was reconstructed](#2-how-the-game-code-was-reconstructed)
3. [PS2 floating-point behaviour](#3-ps2-floating-point-behaviour)
4. [The platform layer and the renderer](#4-the-platform-layer-and-the-renderer)
5. [The disc and its assets](#5-the-disc-and-its-assets)
6. [How identical behaviour is verified](#6-how-identical-behaviour-is-verified)
7. [Compared with other approaches](#7-compared-with-other-approaches)
8. [Limitations](#8-limitations)
9. [Credits](#9-credits)

```
   your disc (folder or .iso / .bin / .chd, read only)
        |  game files                    |  SLUS_210.50: checked, its data image loaded
        v                                v
 +------------------------------------------------------------------+
 |  native_play (one process)                                       |
 |                                                                  |
 |   game code: C++ generated from the decomp (gen/native)          |
 |   + RenderWare / RW Audio / Sony / Logitech libraries as C       |
 |        |  calls by guest address        |  data in guest RAM    |
 |        v                                v                        |
 |   platform layer (src/native)      32 MB guest RAM + scratchpad  |
 |   kernel, time, files, pads,            |  read only, per tick   |
 |   card, sound mixer, movies             v                        |
 |        |                           renderer (b3render)           |
 |        v                           SDL3 GPU: Vulkan / Metal      |
 |   SDL3 audio / input                                             |
 +------------------------------------------------------------------+
```

## 1. What "native" means here

### What runs

The game's functions are C++ source files in [`gen/native`](../gen/native) (4,961 generated units), compiled by
GCC or Clang for the host CPU like any other program. The four VU0 micro programs the game uploads to its vector
unit (collision, track renderer, vehicle renderer and traffic) are generated C++ too
([`gen/native/vu0/vu0_aot_gen.cpp`](../gen/native/vu0/vu0_aot_gen.cpp), [docs/vu0-micro.md](vu0-micro.md)).
Everything is compiled ahead of time: nothing is generated at run time and there is no first-run compile.

The native build holds 6,915 functions ([`gen/native/manifest.tsv`](../gen/native/manifest.tsv), one row each):
the game's functions plus about 1,260 functions of the libraries linked into its executable (RenderWare 3.6,
RenderWare Audio, Sony's EE libraries, a Logitech library), written as C in `src/libs`. Every call goes through a
table keyed by the function's original (guest) address. The target is a generated function, a library function in
C, or a *platform row*: platform-layer code that answers a system or library call at its own entry point.

### The strict mode, and how to check it

`native_play --strict` (`STRICT=1` in the checks) makes any call without a native target a fatal stop that names
the address and the call chain, and does the same for a VU0 launch without a generated program. All 20 scripted
scenarios pass with it ([section 6](#6-how-identical-behaviour-is-verified)). To check it yourself, with your disc
and a memory card folder holding a profile ([CONTRIBUTING.md](../CONTRIBUTING.md)):

```sh
make play                                                    # build/native_play + build/libb3render.so
STRICT=1 tools/native/race_check.sh build/native_play build/rc_race
SCENARIO=crash STRICT=1 tools/native/race_check.sh build/native_play build/rc_crash   # likewise for the others
```

The last line must read like this (the numbers vary by scenario):

```
race_check: PASS race US/C2_V2 reached and driven to VBlank 23400; ...; vu0: 11679530 launches: 11679530 generated,
0 interpreted, 0 without a generated program; aot on, interpreter compiled out
```

A strict run fails on a missing native function, on a VU0 launch that was interpreted or had no generated
program, or when the `vu0:` line is missing.

### What is in the binaries

"No interpreter" can mean several things, so here is exactly what the shipped programs contain:

| Component | Where | What uses it |
|---|---|---|
| VU interpreter (the shared VU0 / VU1 core, `src/b3asset/vu1.cpp`) | **Not in `native_play`, the launcher or the mod tool** (compiled out of `native_play` by default: `make VU0_INTERP=1` adds it for development; `nm -C build/native_play \| grep -c 'b3asset::Vu1::'` prints 0). **In the renderer library** (`libb3render.so` / `b3render.dll`) since the particle layer (`src/b3asset/particleshade.cpp`) | The renderer runs the game's particle VU1 program 0x46D750 on captured copies of the particle arrays to draw smoke, dust, glass shards and fire (presentation only: nothing is written to game memory; `B3_PARTICLES=off` turns the layer off). Also the development cross-checks and shading check tools (`tools/render/b3vucheck*`). The game itself never calls it |
| R5900 subset interpreter (`tests/r5900_mini.h`), the *fallback* | **Not in any shipped program**: compiled out by default and in every release build (`make NATIVE_FALLBACK=1` adds it to a developer build; `nm build/native_play \| grep -ci r5900` prints 0, and the package scripts refuse a binary that has it) | Developer builds and the per-call differential check (`tools/native/diff_check.h`), which use it as a reference. Without it, a call to a function that has no native version stops the game with a message naming the function and a bug-report prompt; it never runs the original code |

So the claim is precise: the game never runs PS2 code, on any path. The game binary has nothing that could run one,
and strict mode shows the 20 scenarios never even reach for it. The one VU1 program that does run as PS2 microcode is the
particle graphics program, inside the renderer's presentation layer (see the table). Whether to replace that with a
hand-written model (as for sparks) is an open decision.

### The guest RAM model

The game's code expects its own memory map: structures laid out by its compiler (MWCC for the EE), 4-byte
pointers inside them, a 64-bit `long`, data at fixed addresses. The port keeps all of it:

- **A 32 MB arena is the PS2's main RAM**, beside a 16 KB scratchpad. A game address is an offset into it.
- **Pointers stored in game memory stay 32-bit guest addresses.** Pointer members become `b3host::gp<T>`
  ([`include/b3host/gp.h`](../include/b3host/gp.h)), 4 bytes that convert to a host pointer when used, and function
  pointers become `gfn<>`. Locals and parameters are ordinary host pointers into the arena.
- **The PS2's ABI on every host**: MWCC's 64-bit `long`, class layout, vtable position and bitfield packing
  ([docs/abi-mwcc-ee.md](abi-mwcc-ee.md)).
- **The disc's executable is read as data.** `native_play` accepts only the retail `SLUS_210.50` (checked by size
  and FNV-1a 64 hash) and copies its loadable segments into the arena at their addresses: initial data, tables,
  vtables and constants (`load_elf_bytes`, [`src/b3host/native_backend.cpp`](../src/b3host/native_backend.cpp)).
  The code bytes sit in memory too, as on a PS2, but nothing executes them.

The reason is exactness. With the same layout, every structure, asset and save the game handles is byte-identical
to the PS2's, code that walks memory by offsets or casts between types behaves the same, a PS2 save imports as-is,
and the RAM of two runs can be compared directly ([section 6](#6-how-identical-behaviour-is-verified)).

## 2. How the game code was reconstructed

### The matching decompilation

The game code comes from a separate decompilation project, `burnout3-retail-decomp`, which rebuilds the retail
executable's functions as C/C++:

- **Byte-matched**: compiled with the original compiler (MWCC 3.0.1 for the EE) and flags, the function gives
  exactly the retail machine code, proven by relinking the whole game against the retail executable.
- **Complete C**: the rest. Written from the retail code to be behaviour-equivalent, it compiles to different
  bytes. The decompilation audits it with a static comparison against retail (calls and their argument sources,
  branches, stores, constants, return values). The audit flags suspicions and is not a proof, so the port adds
  per-call differential checks and the scenario runs ([section 6](#6-how-identical-behaviour-is-verified)).

| State | Functions | Share | Code | Share |
|---|---:|---:|---:|---:|
| Byte-matched | 4,655 | 82.4% | 1.33 MB | 49.2% |
| Complete C | 997 | 17.6% | 1.38 MB | 50.8% |
| Partial C | 0 | 0% | 0 | 0% |
| **Game code** | **5,652** | | **2.71 MB** | |

Source: `docs/status.json` (`decomp_fallback`: 2,709,480 bytes in all, 1,331,924 matched, 1,377,556 complete C).
The complete-C functions are the larger ones, and much of the physics, camera and AI is among them. They are where
an undiscovered difference is most likely, which is why section 6 tests behaviour and not only code.

Names follow the decompilation's evidence rule (Criterion's names where there is evidence, `func_XXXXXXXX`
otherwise): 3,673 of the 6,915 native functions are named (`status.json`). Descriptive names the port adds live in
a separate, tagged layer ([docs/names-layer.md](names-layer.md)).

### From decomp source to host C++: decomp2host

The decompilation's source must stay byte-identical under MWCC, so it is never edited for the PC.
[`tools/decomp2host`](../tools/decomp2host) transforms it mechanically and reproducibly into host C++
([docs/decomp-on-host.md](decomp-on-host.md) lists every rule with its reason):

| Problem on a 64-bit host | Transform |
|---|---|
| 8-byte host pointers would move every later field | Pointer members become `gp<T>`; `T**` becomes `gp<T>*` |
| `extern` data at fixed guest addresses | A reference to the guest global at that address |
| Host `float` rounds to nearest and keeps denormals, Inf and NaN | `float` becomes `ps2f`, whose every operator is one EE FPU instruction of the PS2 float model ([section 3](#3-ps2-floating-point-behaviour)) |
| MWCC's `long` is 64-bit, Windows' is 32-bit | `long` becomes `ps2_long` |
| MWCC idioms that ISO C++ rejects or leaves undefined (jumps over initialisations, `[1]` arrays indexed past 1, type punning) | Equivalent forms; `-fno-strict-aliasing`, `-fstrict-flex-arrays=0`, no loop-bound assumptions |
| Inline EE / VU0 assembly | Translated statement by statement into C++ on the PS2 models (`tools/decomp2host/eeasm.py`, [docs/specs/eeasm-ops.md](specs/eeasm-ops.md)) |

Anything the generator does not understand stops the generation with its location; it never guesses.

**Calls.** A call from generated code to another game function goes through a stub that marshals the arguments in
MWCC's convention and looks the target up by its guest address. The convention was established from retail code
and from probe programs built with the original compiler ([docs/abi-mwcc-ee.md](abi-mwcc-ee.md)). Virtual calls
read the guest vtable, and game functions used as values (callbacks) are registered by their guest code address.
The table, [`gen/native/table/functions.h`](../gen/native/table/functions.h), lists all 9,105 functions of the
executable by address and size. Any of them without generated code or library C must be a platform row or never be
called, which strict mode enforces.

### What is committed, and what is not

- **Committed**: `gen/native` (the generated C++, headers, manifest and function table) from a pinned snapshot of
  the decompilation ([`gen/native/SNAPSHOT`](../gen/native/SNAPSHOT) names the commit), so the port builds without a
  decompilation checkout. It is regenerated, never edited by hand; fixes to game code go to the decompilation.
- **Read only**: the decompilation. The port reads a snapshot of its committed tree and writes nothing back.
- **No disc bytes**: `python3 -I tools/native/bundle_check.py --elf <disc>/SLUS_210.50` checks that `gen/native` is
  text and holds no data copied out of the executable.

**Licence.** The generated code in `gen/` and the libraries in `src/libs/` are the game's own code in another form.
They belong to the game's owners and are **not** covered by this project's MIT licence, which applies to the port's
own code ([LICENSING.md](../LICENSING.md)).

## 3. PS2 floating-point behaviour

### The rules being reproduced

The Emotion Engine's float unit (COP1) and its vector unit VU0 do not follow IEEE 754
([docs/specs/ps2-float.md](specs/ps2-float.md) §3 cites the manuals):

- **No denormals**: an exponent of 0 is zero, whatever the fraction. Tiny results become signed zero.
- **No infinities or NaNs**: an exponent of 255 is an ordinary large number. The model clamps it to the largest
  finite value (±Fmax).
- **Round toward zero**, with no guard bits. Conversions truncate.
- **Exceptional results clamp**: x/0 and overflow give ±Fmax, and the square root of a negative number is taken of
  its magnitude.
- **Multiply-add is not fused**: the product is rounded first.
- min / max use a total order on the raw bit patterns, and VU0's divide unit has its own rules.

Plain C++ `float` on a PC gives different results in all of these cases, and physics accumulates the differences.

### How the port reproduces them

- **`b3fp::ref`** ([`include/b3host/ps2float_ref.h`](../include/b3host/ps2float_ref.h), specified in
  [docs/specs/ps2-float.md](specs/ps2-float.md)) models every operation bit for bit on raw 32-bit encodings: add,
  sub, mul, madd, msub, the EE and VU0 divides and square roots, conversions, compares.
- **Generated game code** uses it for every float operation: the `ps2f` type's operators are the COP1 instructions
  under the model (the shipped build has `B3_PS2_FLOAT=1`).
- **VU0 code** (GtMath through the port's shadow headers in `include/gtmath`, translated inline assembly, the four
  micro programs) uses the same model. The micro programs run their hot operations on the host's float unit set to
  round toward zero and flush-to-zero (MXCSR RZ + FTZ + DAZ on x86-64, FPCR RMode RZ + FZ on arm64:
  [`include/b3host/fenv.h`](../include/b3host/fenv.h), [`ps2float_hw.h`](../include/b3host/ps2float_hw.h)). Where
  those modes cannot give the model's bits (an add of operands more than 28 exponents apart, the divide unit), the
  model itself runs. The code around the mode switch is built with `-frounding-math`, so no float operation moves
  across it.
- **The maths library is ported bit-exactly** ([`include/b3libm`](../include/b3libm), [docs/ps2-libm.md](ps2-libm.md)).
  The game links newlib's single-precision fdlibm, built without denormal and Inf/NaN handling, with constants
  that differ from newlib's source in the last bit. sinf, cosf, tanf, asinf, acosf, atanf, atan2f, powf, logf,
  floorf, ceilf and fabsf follow the retail instruction sequence's branches and association order, with the bit
  patterns the retail code loads.
- **No contraction**: everything is compiled with `-ffp-contract=off -fno-fast-math` (Makefile `REQFLAGS`), so no
  compiler fuses a multiply and an add.

### The tests that prove it

- **`make test`** runs the exactness tests (`test_ps2float`, `test_fenv`, `test_vufloat`, `test_gtmath_ps2` /
  `_fast`, `test_libm`, `test_libm_more`, `test_overrides`, `test_libm_cost`, `test_eeasm_ps2` / `_fast`,
  `test_b3host`, `test_abi`: the `exactness` group of `make mac-tests`).
- **libm against the retail code**: given your executable (`RETAIL_ELF=<disc>/SLUS_210.50`), `test_libm` and
  `test_libm_more` run the retail maths functions in the test-only R5900 subset interpreter and compare them bit for
  bit with the port, over every exponent and both signs and dense ranges around each branch. A one-ulp fault
  injected into one constant gives 637 mismatches in 21,631 inputs: the comparison has teeth
  ([docs/ps2-libm.md](ps2-libm.md)).
- **Hardware float modes against the model**: `test_fenv` and `test_vufloat`, op by op. On an Apple M1 every
  exactness test passed, the libm comparisons were bit-exact, and 0 of 5.2 million operations differed
  ([docs/mac.md](mac.md) §8).
- **Micro programs against the interpreter**: over nine race scenarios, 132,703,109 VU0 launches ran both ways with
  0 differences ([docs/vu0-micro.md](vu0-micro.md), Linux).

### Where the model is not the hardware

The model is checked against the retail *code*, not against a console. [docs/specs/ps2-float.md](specs/ps2-float.md)
§4 documents four places (D1-D4) where it departs from the hardware manuals: exponent-255 operands, the FPU's x/0
value, add/sub of far-apart operands (no guard bits), and FPU divide / square root rounding. The recordings were
made with the model, so the model is what the port keeps. Where the hardware leaves a value undefined (lanes a
masked VU0 instruction does not write) the port uses one fixed rule, while retail has whatever the register held
([docs/decomp-on-host.md](decomp-on-host.md), P6).

## 4. The platform layer and the renderer

### Services below the game

On a PS2 the game's libraries talk to a kernel, to a second processor (the IOP) running driver modules, and to
hardware. The port replaces all of it with native code in [`src/native`](../src/native) that answers the game at
the library entry points it calls, specified in [docs/specs/](specs/):

| PS2 service | In the port |
|---|---|
| EE kernel: threads, semaphores, interrupt handlers, alarms | A kernel model with the PS2's thread and semaphore rules on a virtual clock ([docs/specs/ee-kernel.md](specs/ee-kernel.md)) |
| Timers and VBlank | Virtual EE time (one VBlank = 4,915,200 EE cycles). Each native function charges a fixed cost from the size of its retail code and each platform row the retail path it replaces, so interrupts and thread switches fall at the same points on every run, whatever the host's speed. Window runs pace VBlanks to 59.94 Hz |
| IOP services over SIF RPC (disc, file I/O, devices, the game's file-system module) | Answered at the EE library's own functions: no RPC is made, no IOP module runs, and the RPC entry points are fatal (`platform_sif.cpp`, `platform_gtfs.cpp`, `platform_devices.cpp`) |
| CD/DVD drive | The host reads the disc's files, from a folder or an image ([section 5](#5-the-disc-and-its-assets)) |
| Pads | Keyboard and SDL gamepads become the DualShock 2 report the game reads ([docs/specs/input.md](specs/input.md)); the mouse drives the game's menus |
| Memory card | The game's own card library runs natively on a 16,384-page card image, built from and written back to the port's save files (`save_card.cpp`) |
| Sound (RW Audio on the IOP, the SPU2 chip) | RenderWare Audio's EE library as C; its packets go to a native executor of its IOP protocol and an SPU2-equivalent mixer (PS-ADPCM, Gaussian resampling, ADSR, reverb; 48 kHz, 800 samples per VBlank). No IOP code, no sound-chip registers ([docs/audio.md](audio.md)) |
| Movies (Sony libmpeg on the IPU) | A native MPEG-2 decoder fed from the game's own ring buffer, with the PS2's colour conversion (`mpeg2_decoder.h`, `platform_movie.cpp`). No IPU in the shipped game |
| VU0 micro mode | The generated programs, chosen by the SHA-256 of the image the game uploads |
| Hardware registers the libraries write | A device-state layer (`hw_model.h`, `platform_hw.cpp`) keeping only what the game reads back: register values, DMA completion and its interrupt on the virtual clock, the VBlank grid. No DMA chain walked, no VIF / GIF / GS command executed; an unmodelled register is fatal |

**Where the line is drawn.** Some of the PS2's behaviour *is* the game's behaviour: float arithmetic, the order and
timing of interrupts and thread switches, the sound chip's sample arithmetic. The port reproduces those as native
code working on objects (threads, alarms, voices), not as an emulated machine. The game still builds its PS2 draw
packets in guest memory, since that is what its code does, but nothing consumes them as GPU commands.

### The renderer

The renderer ([docs/native-renderer.md](native-renderer.md), long; `src/b3replay`, `src/b3gfx`, `src/b3asset`,
`src/gameview`) does not emulate the PS2's Graphics Synthesizer. It draws the game's *scene*:

- **Capture.** Once per simulation tick, at the game's controller poll, a read-only hook reads the scene from guest
  RAM: camera and viewports, cars, traffic, the visible track units, props, effects (shadows, skid marks, sparks,
  coronas, water, post passes) and the game's own render decisions. The 2D layer (HUD, menus, text, movies) is
  captured at the game's 2D renderer calls, with their float coordinates. The hook never writes guest memory.
- **Assets** (textures, track and car geometry) come from your disc; the pre-built geometry packets in its files are
  decoded once into vertex buffers.
- **Shading.** The maths of the game's VU1 micro programs (transforms, lighting, fog, environment maps) is
  reimplemented as shaders and checked against a VU1 reference model by developer tools.
- **GPU.** SDL3 GPU: Vulkan on Windows and Linux, Metal on macOS, on its own thread in the game's process. The game
  never waits for it.
- **Frame rate.** The simulation keeps the game's fixed 60 Hz steps. The renderer draws at the display's rate,
  interpolating camera and object transforms between ticks (its low-latency mode briefly extrapolates past the
  newest one) ([docs/frame-rate.md](frame-rate.md)).
- **Any aspect ratio.** 3D at the window's shape with an adjustable field of view; HUD and menus anchored to the
  window's edges; 4:3 movies with black or blurred sides.

Because the renderer draws the scene rather than GS commands, MSAA, filtering, detail, whole-track draw distance,
HD textures, photo mode and split-screen layouts are presentation only: none of them changes what the game
computes, and the checks in section 6 run with every feature at its default.

## 5. The disc and its assets

- **Your own disc, read in place.** The game needs Burnout 3: Takedown NTSC-U (SLUS-21050). Give it a folder of the
  disc's files or a disc image: a cooked `.iso` (2,048-byte sectors), a raw `.bin` (2,352-byte sectors) or a `.chd`
  of either (through libchdr, its hunks decompressed as they are read), read as an ISO 9660 volume without extracting
  it ([`include/b3asset/discfs.h`](../include/b3asset/discfs.h)). Every reader
  (the game's file system, RW Audio's streams, the executable, the renderer, the launcher) goes through the same
  read-only functions. Nothing writes to the disc folder or image.
- **No game data in the repository or the packages.** No disc images, game files, executable, BIOS or SDKs are
  included. The packages contain the port's own programs, documentation and licences (`tools/release/package_*.sh`).
- **Mods overlay the disc.** A mod's files answer the game's file opens in place of the disc's. A patch mod ships a
  binary delta (`.b3patch`), which is applied to your own file when the game starts, after checking the source
  file's size and hash. The result is kept in a cache file in the mods folder, never written to the disc. With no
  mods enabled there is no overlay at all, and the checks give the same results byte for byte
  ([docs/modding/DESIGN.md](modding/DESIGN.md)).
- **Saves.** The game's own save code writes to the card image's pages, and the port keeps them in up to five
  `.sav` files of its own. Each holds the game's three profiles, with backups. PS2 saves can be imported (`.psu`,
  `.max` and emulator memory-card images) and exported ([docs/PLAYING.md](PLAYING.md)).

## 6. How identical behaviour is verified

### The layers of evidence

| Level | What is shown | How |
|---|---|---|
| Code | 4,655 functions compile to the retail machine code | The decompilation's relink against retail |
| Code | The other 997 are complete and audited | The decompilation's static audit; the port's per-call differential check (retail and native from the same saved state, RAM and results compared: `tools/native/diff_check.h`); the scenarios |
| Arithmetic | PS2 floats, VU0, libm bit-exact under the model | `make test`, exactness group ([section 3](#3-ps2-floating-point-behaviour)) |
| Code | No generated non-void function falls off its end (undefined behaviour in C++, where retail's code leaves v0 as it was) | `make check-returns` (`tools/native/check_returns.py`; the generation and `make native-bundle` run it too) |
| Coverage | No function needs the original code | `STRICT=1` scenario runs ([section 1](#1-what-native-means-here)) |
| Behaviour | A build does exactly what the recorded build did | The golden recording and compare, below |
| Input | A recorded session plays back exactly | Replay files and pad recordings, below |

### The golden recording

[`tools/golden/baseline.sh`](../tools/golden/baseline.sh) runs a `native_play` build through 20 scripted scenarios,
headless and unpaced, each with `STRICT=1`:

- **Race modes**: race, pause, photo, music (the custom-music path, with generated test tones), takedown, time
  attack, road rage, crash, World Tour, movie (a race with the intro movie skipped).
- **Front-end tours**: multiplayer (with a second controller and the split-screen start), party crash, online (the
  game's own no-adapter message, then PC SETTINGS), the Driver Details pages (rewards; records and profile load /
  save; settings, training and extras), Crash Nav, car select, crash results, and the extras menu's demo hand-over.

For each scenario it records seven items, which [`tools/golden/compare.sh`](../tools/golden/compare.sh) compares
between two recordings:

| Item | What must be equal |
|---|---|
| verdict | `race_check.sh` passes (the scenario reached its goal and every expected screen) |
| shots | The SHA-256 of every screenshot, except photo-mode free-camera shots (that camera moves by host time) |
| trace | The time trace of 41 EE kernel rows (threads, semaphores, handlers, alarms, interrupts): per 100-VBlank window, the call count and a hash over the calls (each with its VBlank, virtual cycles at entry and exit, arguments and result), then the count per row |
| dma | Per VBlank, the DMA completions and DMAC callbacks with their clock |
| sif | The EE-IOP command traffic, stamped with VBlank and EE cycles |
| rwa | RenderWare Audio's commands and replies |
| ram | An FNV-1a hash of each MB of guest RAM every 100 VBlanks. A cell that differs once and is equal again within four grid points, with everything else equal, is reported as transient host noise (host bytes below the guest stack pointer), not a failure; `RAM_STRICT=1` fails on it too |

**"140 PASS"** means all seven items equal in all 20 scenarios. A separate `rate` line compares speed (unpaced
VBlanks per second). It is not a behaviour item: when a recording has no rate runs (`RATE_RUNS=0`) it is skipped
or fails, which says nothing about behaviour. Two runs of one build give the same 140 items: the simulation is deterministic. Every change to the platform
layer, the renderer or a default is checked this way, and a refactor or a feature at its default must give
identical results ([CONTRIBUTING.md](../CONTRIBUTING.md)). The comparison behind `status.json` is dated 2026-11-12.

A recording keeps each scenario's whole `native_play` log (`native_play.log.gz`: its wall-clock rate every 600
VBlanks shows how loaded the machine was), and a scenario whose verdict fails keeps its screenshots. When
`compare.sh` fails a scenario, it copies that scenario's recording to `<new recording>.failed/<scenario>.<UTC time>`
(`KEEP_FAILED=<folder>` to put it elsewhere, `KEEP_FAILED=none` for no copy), so a re-run into the same folder does
not erase the evidence.

### Replays and pad recordings

A deterministic simulation can be stored as its inputs. A **replay file** (`.b3replay`, about 80 KB for a race)
holds both pads, typed text, the card as it was at the boot, the game-side options and RAM checkpoints.
[`tools/native/replay_check.sh`](../tools/native/replay_check.sh) records a scenario, plays the file back, compares
the two runs with `compare.sh` (every item equal) and checks the playback's line `replay: N of N checkpoints
matched`. Window runs also keep a **pad recording** beside their log, and `native_play --replay-pad` plays one
back: that is how a bug report replays a player's session.

### Unit tests and CI

`make test` builds and runs 70 test programs (65 without a decompilation snapshot, as in CI), plus the generator's
self-tests, a compile-fail suite and a declaration check (Makefile `TESTS`). Tests that need the disc or the retail
executable skip those parts without it and say so. GitHub Actions runs `make test` on Linux and `make mac-tests` on
macOS for every push and pull request ([`.github/workflows/ci.yml`](../.github/workflows/ci.yml)); the in-game
checks need a disc, so CI does not run them.

### Running the checks yourself

You need a build (`make play`), your own disc (folder or image: `B3_GAME_DIR`, or `data/game`) and, for the
scenarios, a memory card folder with a profile (`SAVES`):

```sh
make test
make check-returns                                            # last line: check_returns: PASS ...
STRICT=1 tools/native/race_check.sh build/native_play build/rc_race
tools/golden/baseline.sh build/native_play build/golden-a     # record a build
tools/golden/baseline.sh build/native_play build/golden-b     # record it again (or another build)
tools/golden/compare.sh build/golden-a build/golden-b         # last line: compare: PASS ...
tools/native/replay_check.sh build/native_play build/replay_check
```

The outputs (screenshots, logs, RAM hashes) are derived from your disc. Share verdict lines and hashes, never the
files.

### What these checks do not show

- The golden compare shows that two builds behave alike, and determinism that a build behaves alike on every run.
  Neither is a measurement of a real PS2: the comparisons with retail are of the retail *code*, run under the
  port's own float model (section 3).
- The recordings are made on Linux x86-64. The float model is tested on x86-64 and arm64, but a golden comparison
  *between* platforms is not part of the published checks.
- The scenarios follow scripted inputs through every race mode and front-end path; they cannot reach every state
  of the game.

## 7. Compared with other approaches

There are several ways to bring a console game to a PC. This compares the general kinds by their usual
definitions; it describes no particular project.

- **(a) Full-system emulator**: runs the original executable unchanged, interpreting or recompiling its machine
  code at run time (JIT) and emulating the graphics chip, vector units, sound chip and second processor.
- **(b) Emulator-based port or wrapper**: an emulator core packaged with one game, with per-game settings, patches
  or hooks.
- **(c) Static binary recompilation**: the executable's machine code translated mechanically, instruction by
  instruction, into C or C++ ahead of time and compiled natively, running on a model of the machine (CPU registers,
  the memory map, emulated or replaced hardware).
- **(d) Source-level reconstruction (this project)**: a matching decompilation turned back into source code and
  compiled natively, on a native platform layer, with a native renderer.

| | (a) Emulator | (b) Emulator-based port | (c) Static recompilation | (d) Decompilation, compiled natively |
|---|---|---|---|---|
| **What is preserved** | The original binary, exactly | The original binary, plus patches | The original instruction semantics, translated | The game's source-level operations (byte-matched where proven), its data layout and float rules |
| **What runs** | Interpreter or JIT, plus emulated hardware | The same, packaged | Translated instructions as native code, on a CPU-state and hardware model | Source compiled natively, on a platform layer and a renderer; registers exist only at call boundaries and in VU0 assembly |
| **Needed at run time** | The game's executable and data, often console firmware | The same | The game's data; the executable at build or run time | The game's data, and the executable as data |
| **Graphics** | Emulated graphics chip, usually upscaled | Emulated, sometimes hooked | Emulated or replaced at the hardware interface | Drawn from the game's scene by a new renderer |
| **How behaviour is shown** | The emulator's accuracy against real hardware, across many games | As (a) | The translator's correctness, instruction by instruction | Byte matching for 82% of functions (49% of the code); audits, differential checks and recordings for the rest |
| **Strengths** | One program for a whole library of games; high fidelity when accurate; no per-game reconstruction | Quick to ship; per-game polish | Native speed without a JIT; a game-agnostic translator; far quicker to produce than a decompilation | Readable, moddable, portable source; rendering, input and features free to change; no CPU or graphics-chip model |
| **Costs** | Run-time overhead; changes beyond resolution and patches are hard; JIT not allowed on some platforms | As (a); improvements limited to what hooks reach | Machine-shaped output, hard to read or modify; indirect jumps and hardware still need handling | Very slow to build (every function rebuilt and checked); one game only; the non-matching part rests on testing; platform layer and renderer written and proven per service |

Each approach has its place. Emulation preserves the most for the least per-game work; recompilation reaches
native speed without reconstructing source; a decompilation costs the most to build and gives code that people
can read, change and port.

## 8. Limitations

- **Online play** is not in this build. The port reports no PS2 network adapter to the game (the original servers
  are gone), and the ONLINE dialog reads "Online play is coming soon." (the port's own words in place of the game's
  no-adapter message, `tools/native/online_message.h`). Online play is coming in a future release.
- **macOS** (Apple Silicon): a `.dmg` with Burnout 3 Native.app, built and smoke-tested by CI on an Apple Silicon
  runner (ad-hoc signed, not notarized; [docs/mac.md](mac.md)). The download itself is still to be run on a real Mac.
- **Linux** packages are built for glibc 2.35 and newer and pass their smoke test. Runs on other distributions and
  the Steam Deck are still to come.
- **Checks still to do in a window** (the automated checks run headless): rumble, remapping with real pads, a second
  controller, the look and sound of fast loading, depth of field, the showroom, replays, frame pacing, HUD size,
  side-by-side split screen with two pads, and adaptive triggers and gyro steering with real pads
  ([docs/release/fact-sheet.md](release/fact-sheet.md)).
- **The float model is not proven equal to the hardware** in the four documented cases D1-D4 (section 3).
- **The 997 complete-C functions** are audited and tested but not byte-proven. A difference that none of the
  scenarios reaches could remain.
- **A function the native build lacks stops the game** (with a message and a bug report), rather than falling back
  to the original code: the game binary has no interpreter to fall back on (section 1). None of the 20 scenarios
  reaches one.
- Known problems in the current build: [docs/known-issues.md](known-issues.md).

## 9. Credits

The projects and communities this port stands on are thanked in the README's
[Acknowledgements](../README.md#acknowledgements).
