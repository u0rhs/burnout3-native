# The Mac (Apple Silicon)

## Playing: the Mac app (download)

The ready-made Mac download is a disk image, `burnout3-native-<version>-macos-arm64.dmg`, with **Burnout 3
Native.app** in it: the launcher, the game and its renderer (Metal through SDL 3), SDL 3 itself and the mod tool, all
inside the app. It needs a Mac with Apple Silicon (M1 or later) and the macOS version its `README-TESTERS.txt` names.
No Homebrew and no developer tools are needed to play.

1. Open the `.dmg` and drag **Burnout 3 Native** onto the **Applications** folder beside it.
2. **The first start.** The app is ad-hoc signed but not notarized by Apple, so macOS blocks it the first time:
   - **macOS 15 (Sequoia) and newer:** open the app once (macOS says it cannot check it: click **Done**), then
     **System Settings > Privacy & Security**, scroll down to *"Burnout 3 Native.app" was blocked*, **Open Anyway**,
     and confirm.
   - **macOS 14 and earlier:** the same, or right-click (Control-click) the app > **Open** > **Open**.
   - **Any version:** once in Terminal, `xattr -dr com.apple.quarantine "/Applications/Burnout 3 Native.app"`.
3. The launcher opens: its **Game Data** page takes the folder with your disc's files, then **START GAME**.

Your saves, settings and logs are in `~/Library/Application Support/b3port/native/` (Finder: Go > Go to Folder...),
the same folder the build-it-yourself steps below use. Inside the app (right-click > Show Package Contents):
`Contents/MacOS` holds `b3launcher` (the app's own program), `native_play`, `libb3render.so`, `libSDL3.0.dylib` and
`b3modtool`; `Contents/Resources` holds `README-TESTERS.txt`, `KNOWN-ISSUES.txt`, `LICENSES/` and the Blender add-on
(`b3_car_tools.zip`). The player guide, [PLAYING.md](PLAYING.md), covers the rest. Section 10 below builds the same
app yourself.

## Testing from source

Two things to check on a Mac: that **b3view draws a track with Metal**, and that the **PS2-exactness unit tests
pass on arm64**. You only need the repo and your disc files. Nothing here writes to the disc folder, and nothing
made from it goes into git (`build/` is git-ignored).

## 1. One-time setup

1. Xcode command line tools: `xcode-select --install` (skip it if `clang++ --version` already works).
2. Homebrew (https://brew.sh), then: `brew install sdl3 pkg-config make`
   (`make` gives a current GNU make, `gmake`; macOS's own is from 2006.)
3. The repository: `git clone https://github.com/u0rhs/burnout3-native ../burnout3-native`
   (a clone you already have: see section 9, "Get the newest code"). Use a path without spaces.

## 2. Build, test, view

From the repo folder (`cd ../burnout3-native`):

```
tools/mac/build.sh  <disc folder>    # e.g. ../game -- the folder with SLUS_210.50, DATA, TRACKS, PVEH ...
tools/mac/test.sh                    # unit tests + one headless b3view picture; writes build/mac/report.txt
tools/mac/b3view.sh AS/C1_V1         # the window: free camera on a route
```

- `build.sh` checks the tools and prints what it found. It remembers the disc folder (`build/mac/disc`) and builds
  everything into `build/mac/` (log: `build/mac/build.log`). If you already copied the disc to `data/game`, the
  argument can be left out.
- `test.sh` runs 32 test programs (two to five minutes), renders `AS/C1_V1` once without a window, and prints the
  report.
- `b3view.sh` takes a route (`AS/C1_V1`, `EU/C4_V1`, ...), a whole region (`EU`), or a car (`PVEH/COMP/CAR1.BGV`).
  Controls: hold the right mouse button and move to look; W/A/S/D move; Q/E down/up; Shift is faster; the wheel
  changes speed; N/B next/previous stream unit; F fog; F11 fullscreen; Esc quits. The title bar shows fps, size and
  driver.

## 3. What to send back

1. `build/mac/report.txt`. It has the machine, compiler, SDL version, every test's result and the retail-comparison
   lines; no game data.
2. The picture named in the report (`build/mac/b3view-AS_C1_V1.png`), to compare with the same view rendered on
   Vulkan.
3. A screenshot of the b3view window, title bar included.
4. If something failed: `build/mac/build.log`, or the failing test's log `build/mac/mac-tests/<test>.log`.

## 4. What a result means

| Result | Meaning |
|---|---|
| All `exactness` rows PASS, with retail lines saying `bit-exact` | The PS2 arithmetic model gives the same bits on Apple Silicon as on the PC: physics stays PS2-identical on ARM. That covers the EE float rules (`b3fp`), VU0 (GtMath), the game's libm compared bit for bit with the retail code, and the FPCR round-toward-zero mode. |
| `test_fenv` FAIL | The CPU's float mode (FPCR round-toward-zero + flush-to-zero) does not behave as on x86 / qemu. Plain C float code would then drift from the PS2. The table-driven model (`b3fp::ref`, used by the faithful build) is not affected. |
| `test_ps2float` / `test_gtmath_*` / `test_libm*` FAIL | The model itself computes differently under Apple's clang (for example a fused multiply-add slipping in). That would be a real porting bug. Send the log. |
| `SKIP` on `test_libm_cost`, or `test_libm` with only ~29 checks | No `SLUS_210.50` was found in the disc folder, so the comparisons with the retail code did not run. Re-run `build.sh` with the right folder. |
| `other` rows FAIL | Not about physics: renderer decoders, HUD readers, audio DSP, the native backend and platform. |
| `NOT BUILT` | A compile or link error under Apple's toolchain: `build/mac/build.log` has it. |
| b3view picture fails, but the window works | Only the windowless Metal path is affected (`--offscreen`, never run on a Mac before). |
| The window says `metal` and shows the track | The Metal claim holds (shaders: SPIR-V -> MSL, `src/b3gfx/shaders_gen.h`). |

## 5. Keys for the native game (no controller needed)

The native game reads the keyboard as a PS2 DualShock 2 (`src/native/input_keys.*`). A connected pad still works
alongside it. The keys depend on whether you are **driving** (the race simulation runs) or in a **menu** (front end,
pause, results): native_play watches whether the game reads the player's accelerator (`GetAccelerator` 0x12FA40, called
only by the player car's driving / crashing controls) and logs each switch (`keys: driving` / `keys: menus`). So the arrows move through menus and Up never confirms there.

| Key | Driving | Menus | Game evidence |
|---|---|---|---|
| W / Up | accelerate (Cross; also stick up = Aftertouch while crashing) | up (D-pad) | GetAccelerator 0x12FA40 reads Cross [C]; GetAftertouch 0x12F720 the stick Y [C] |
| S / Down | brake / reverse (Square; also stick down) | down (D-pad) | GetBrake 0x12F970 reads Square [C] |
| A / D, Left / Right | steer (stick X + D-pad pressures, ramped to full in 0.12 s) | left / right | GetSteering 0x12F850 [C] |
| Space | boost (R1); hold it as you crash: Impact Time + Aftertouch | confirm (Cross) | 0x12F160 -> StartBurn; GetCrashingDrivingControls [C] |
| Enter (or keypad Enter) | Cross | confirm (Cross) | |
| Escape | pause (Start) | Start ("Press Start", resume) | 0x12FB10 Start [C] |
| Backspace | change camera (Triangle) | back (Triangle) | 0x12F330 [C call, L meaning] |
| C | change camera (Triangle) | back | |
| Q | look back (L1) | L1 | 0x12F240 [C call, L meaning] |
| E (or 3) | Crashbreaker (R2, Crash mode) | R2 | 0x12F610 -> DoCrashBreaker [C] |
| Left Shift, R | boost (R1) | R1 | |
| H, X | horn (Circle) | Circle | 0x12F6B0 [C] |
| 1 | music track skip (L2) [L] | L2 | 0x12F460 [L] |
| Z | Cross | Cross | |
| Tab | Select | Select | |
| V | -- | Square (car select Color, MULTIPLAYER Change Names) | the pages' Square prompts [C shots, fe1] |

Movies: Enter, Space or Escape (pad: Cross or Start) skips the movie that is playing (docs/remaster-features.md §2);
a corner hint shows the button for the device in use (SETTINGS > INTERFACE > VIDEO SKIP HINT turns it off).
The table is in `default_key_map()`: the default layout. Remapping (remap1, docs/pc-settings.md §7.9): SETTINGS >
INTERFACE > CONTROLS in the game, or the launcher's Controllers page, rebinds any PS2 button or stick direction per
player, for the keyboard (separately while driving and in menus) and for controllers; the choice is saved in
controls.ini in the user data folder and the prompts show the keys and buttons bound. Player 2 can have keys too (none
by default): they then drive a second controller port.

Button prompts (mac5, remaster-features §3): the game's button glyphs show the device used last -- key caps with
these keys after a key press, Xbox / Nintendo / generic buttons after a pad's button, the game's PlayStation glyphs for
a PlayStation pad and before any input. SETTINGS > INTERFACE > BUTTON PROMPTS fixes one; `B3_PROMPT_DEVICE=keyboard|
playstation|ps5|xbox|nintendo|generic` (ps5 = ps4: the PS4 / PS5 style) fixes it for a run (offscreen shots).

Two controllers (pad2, 2026-11-11; local 2-player: ROAD RAGE, TEAM CRUSH, DOUBLE IMPACT, PARTY CRASH, the
multiplayer races in split screen): the first gamepad connected is player 1 (port 1, together with the keyboard and the
mouse), the second is player 2 (port 2). Port 2 is plugged in while the second pad is connected: pulling it out is a
PS2 pad pulled out of port 2 (the game shows its own controller-removed handling), connecting it again plugs it back
in. A pad keeps its player while it stays connected: if player 1's pad goes, player 2's stays player 2 and the next pad
connected becomes player 1 (to make a lone remaining pad player 1, disconnect and reconnect it). A third pad and more
act as player 1 (as every pad did before) and take an empty seat when one frees up. With one gamepad (or none)
nothing changes: port 2 stays empty. The button prompts follow player 1's devices (the keyboard and player 1's pad);
photo mode and the PC SETTINGS page take player 1's pad; BOOST: Toggle applies to both players; with SETTINGS > INTERFACE > RUMBLE On
both pads rumble, each for its own car (off by default). The log says `[b3pad] gamepad: <name>, player 2 (port 2 connected)` and
`native_play: VBlank N: port 2: a gamepad plugged in (player 2)` / `pulled out`. `--keys2` (scripted port 2) and
`--replay-pad` take precedence: the window's second pad is then ignored, and a pad recording holds port 1 only.

Mouse (mac5, remaster-features §8): in the game's menus, pointing at a row selects it, a left click selects and
confirms it, a click on a prompt at the bottom (Select, Back, START Resume ...) presses that button, a right click is
Back (Triangle), the wheel moves up / down; on a page without a list (a message, the car or track pages) a left click
continues (Cross). The SETTINGS page takes the mouse too (rows, tabs, sliders, the wheel scrolls, right click closes).
The cursor shows when the mouse moves and hides after 3 s or when a key or a pad is used; the prompts keep the key caps
while the mouse is used. In a race the mouse does nothing (the pause menu takes it).

Typing a name (mac5): on the profile name keyboard (NEW PROFILE) letters, digits, space and punctuation type into the
name, Backspace deletes, Enter is Done; the arrows still move the on-screen keyboard's cursor and Escape / Tab keep their
buttons.

## 6. The native game (native_boot) on arm64: status (mac2, 2026-11-07)

Not a user step yet: boot2's native game does not draw a picture yet. Checked here, without a Mac:

- **Compiles for aarch64 with clang 21.** All of the platform tier (`src/native`, `tools/native/native_boot.cpp` /
  `native_check.cpp`, gameview, b3host). The generated game code (boot2's generation, 4,853 units): 4,844 compile.
  The other 9 rely on GCC's `-fpermissive` leniency, which clang does not have:
  - 5 have a `goto` that jumps past an initialisation;
  - 1 assigns through a `const&` (a T31 escape);
  - 1 assigns to a `volatile` ps2f field;
  - 1 takes the address of an rvalue;
  - `sce_libmc2`: `char* = strrchr(const char*, ...)`.
  These need generator or source fixes (gen1 / boot2's code). Until then, Homebrew's GCC (`brew install gcc`) is the
  fallback for the native build on the Mac.
- **Portability fixes:**
  - `gp<T> == T*` made unambiguous: a template, the operator GCC already picks;
  - clang's flags for the generated units;
  - an explicit 8 MB stack for the guest threads' host threads (macOS gives 512 KB);
  - zstd through pkg-config on macOS.
- **Runs under qemu-aarch64, identical to x86.** `native_boot --tickhash ... --polls 200` (now `--poll-point`; the tick hash was retired on 2026-11-11) with B3NATIVE_MAX_VBLANKS=600
  on boot2's code (3056978) and generation:
  - aarch64 (clang 21, qemu) and x86-64 (g++), both without the 8 units clang rejects, stop at the same missing
    function (`Prepare__20CB3FlashText2dObject...`, boot2's current frontier) after the same 68,772,071 dispatch calls;
  - per-function call counts and the stub log are identical.
  - Guest RAM differs only in two guest stacks (493 words), by host stack bytes that `ptr_arg` bounce windows copied
    in (host addresses: these differ between any two runs too).
- **The first run diverged, from a real bug.** In RW Audio's `func_002AD480` (src/libs/rwa/rwastreamnodety.cpp), retail
  passes a two-word stack object to a getter that writes both words (sp+0x38 / +0x3C). The C had one `RwUInt32`, so
  the second word lived in whatever host stack followed it. That worked on x86 by layout; on aarch64 the stream
  callback went to a wrong address and the boot called a null pointer. Found by bisecting call counts and guest RAM
  between the two builds, then boot2's `--oracle` (x86 SAME, aarch64 DIFF). Fixed (`slot[2]`, mac2).
  `-fsigned-char` was tested first and ruled out.
- `native_boot` reads the ELF from `B3_ELF=<disc>/SLUS_210.50` (its default path is this PC's).

## 7. Status before the first run (2026-11-07, branch mac1)

Done on Linux without a Mac:
- **arm64.** The 28 tests that need no zlib, built for aarch64 by clang 21 and run under qemu-aarch64, all pass,
  including the retail-ELF oracle: libm bit-exact, cost exact, and `test_fenv` with FPCR RZ+FZ, 0 of 5.2M operations
  differing from `b3fp::ref`. qemu emulates the AArch64 float rules in software, so the M1 can still differ: that is
  what `test.sh` settles.
- **libc++.** All 32 tests build and pass against LLVM libc++ (x86-64), the C++ library family Apple ships. The
  viewers build. Every Mac-built source compiles against libc++ 17 headers.
- **Fixes this needed:** `-msse2` only on x86 (Makefile); a NEON fast path and an FPCR `Ps2FloatEnv` (`include/b3host/`);
  macOS ucontext (`_XOPEN_SOURCE`) and `dladdr` for the image base (`src/b3host/`); `std::chrono::clock_cast`
  (not in libc++) replaced in `save_card`; the `PtrArg` snapshot order (`include/b3host/call.h`), a latent bug that
  only libc++ exposed.

Not checkable here: Apple clang / ld64 / Mach-O, macOS's ucontext on arm64, SDL3 with Cocoa + Metal, and the
windowless Metal path.

On Linux: `tools/mac/cross_check.sh [a64|lcx|lcx17|all]` repeats these checks (toolchain:
`tools/mac/toolchain-linux.sha256`; `RETAIL_ELF=<disc>/SLUS_210.50` for the oracle).

## 8. The M1 runs (2026-11-07) and their fixes (branch mac3)

On an M1 Max (Apple clang 21, SDL 3.4.18):
- every exactness test passed; the libm retail comparisons were bit-exact with exact costs; Ps2FloatEnv: 0 of 5.2M
  operations differed;
- b3view rendered on Metal: offscreen, and windowed at 3328x1800, about 298 fps.

Three failures, all fixed on mac3:
- **test_livelap.** The ring readers sized the shared memory with `lseek(SEEK_END)`, which macOS does not support on a
  POSIX shm object, so no reader could open. Now `fstat`. [C] It passed on the second M1 run (2,360 checks).
- **test_clock, NOT BUILT.** Its recipe (and test_gameinput's, b3clocksim's, b3latsim's) had no `-std`; Apple clang's
  default has no `std::clamp`. Reproduced with libc++ at gnu++14, including the same errors at clock.h 69:22 / 120:26.
  Now they compile at C++20. `tools/mac/test.sh` writes a NOT BUILT test's compiler errors into report.txt.
- **test_nonatomic, segfault (rc 139) in windows_and_pass_through_per_fiber** (test_nonatomic and the fibers were retired
  2026-11-11; the stack clamp below stays, now on the thread's own stack).
  - Cause: a `ptr_arg` window (336 bytes around a local near the top of a fiber stack) read past the stack's malloc'd
    block into an unmapped gap. That happened with both the old ucontext path and the AAPCS64 switch.
  - Fix: windows are clamped to the current stack. A running fiber publishes its range; other code uses the thread's
    own stack (pthread_getattr_np / pthread_get_stackaddr_np / the Windows TIB).
  - POSIX fiber stacks are now mmap'd between guard pages, so Linux faults the same way. Reproduced under qemu-aarch64
    before the fix; new test `window_at_the_ends_of_a_guarded_stack`.
  - AArch64 also switches fibers with its own AAPCS64 routine (x19-x30, d8-d15, FPCR) instead of the deprecated
    ucontext.

To rerun after the merge: `git pull`, `tools/mac/build.sh <disc folder>`, `tools/mac/test.sh`; send back
`build/mac/report.txt`.

## 9. The native game on the Mac: native_play (mac4, 2026-11-07)

The game's own code compiled for the Mac:
- the decomp's C++ (generated, committed in `gen/native`, so no decomp checkout is needed);
- the platform tier, the in-process renderer (Metal through SDL3) and the audio mixer.

It reads only the disc folder you give it.

**Get the newest code.** The repository's history was replaced by a fresh start more than once, so in a clone made
before that, `git pull` refuses ("unrelated histories"). Update it with:
```
git fetch origin
git reset --hard origin/main      # throws away local changes in this clone; build/ (not tracked) stays
```
or make a fresh clone (`git clone https://github.com/u0rhs/burnout3-native`).

**Run the game:**
```
tools/mac/native.sh <disc folder>
```
- The first build compiles about 4,900 files: roughly 10-20 minutes on an M1. Later runs only relink what changed.
- One-time setup is that of section 1 (`brew install sdl3 pkg-config make`).
- Options after the folder go to native_play: `--no-audio`, `--quit-after 300` (seconds), `--offscreen` (no window,
  for a log only).
- `SAVES=<folder> tools/mac/native.sh <disc folder>` inserts a memory card. The folder holds the game's saves; use a
  copy. Without it, there is no card, and the game says so at the start.
- `BUILD_ONLY=1` builds without running.

**Or with the launcher:** `LAUNCHER=1 tools/mac/native.sh` builds the launcher too (`build/mac/b3launcher`, beside
native_play) and opens it: its Game Data page takes the disc, and START GAME runs the game from the same folder. Its
other pages (settings, saves, mods, music, the showroom, replays, controllers) work as on Windows and Linux. The
launcher builds on macOS in CI, and a build of the .dmg has been installed and run on a real Mac.

**What you should see:** a window, the intro movies (skippable), the title screen, the menus, and every race mode,
World Tour included, with sound, as on the PC. Report anything that differs.

**Keys:** section 5 (WASD / arrows drive and move through menus, Space boost, Enter confirm, Escape pause / Start,
Backspace back). A connected controller also works.

**Send back:**
- `build/mac/native_play.log` and `build/mac/pads.txt` (the pad recording: the button / stick states per frame, no game
  data; with it the same run is replayed exactly on the PC, `REPLAY_PAD=build/mac/pads.txt` / `--replay-pad`), and
  `build/mac/native-build.log` if the build failed;
- a screenshot (Cmd+Shift+4) of each new screen, and of the last one if it stops;
- whether the sound played, and whether it felt smooth.

The log names any game function that runs as the original PS2 code (the fallback list at the end), and gives a
report if the game stops.

## 10. Building the Mac app yourself: tools/release/package_mac.sh

`tools/release/package_mac.sh` builds the download above from the source, on an Apple Silicon Mac with the one-time
setup of section 1 (the Xcode command line tools; `brew install sdl3 pkg-config make`):

```
tools/release/package_mac.sh                     # build + package: build/release/burnout3-native-<version>-macos-arm64.dmg
SMOKE=1 tools/release/package_mac.sh             # ... then the smoke test of the packaged app (no game data needed)
SMOKE=1 B3_GAME_DIR=<disc folder> SAVES=<card folder> tools/release/package_mac.sh
                                                 # ... plus the strict race from the app's own native_play
```

- **The build** (`build/mac-release/`, log `release-build.log`): native_play, libb3render.so, the launcher and
  b3modtool with Apple clang from the committed native bundle (`NATIVE_FROM=bundle`, as `tools/mac/native.sh`), for
  the minimum macOS of the SDL 3 it bundles (Homebrew's is built for the macOS its bottle is for; `MACOS_MIN` and
  `SDL3_DIR=<prefix>` choose others). The first build compiles about 4,900 units. `OUT=<folder>` packages a finished
  build instead.
- **The package:** the programs stripped (their dSYMs go to `build/release/debug/`, for a crash report's offsets),
  SDL 3 copied into the app and every reference to it rewritten to `@rpath/libSDL3.0.dylib` with `@executable_path` /
  `@loader_path` rpaths (`install_name_tool`), the `Info.plist` (bundle id, version, minimum macOS, high resolution,
  game controllers), the icon (`b3.icns`, the prebuilt `assets/icon/b3-icon.icns`), the readme, the known
  issues and the licenses, then an ad-hoc signature (`codesign -s -`: Apple Silicon runs only signed code) and a
  compressed disk image (`hdiutil`, UDZO) with the app beside a link to `/Applications`. There is no notarization
  step.
- **The checks it always makes:** every program arm64; `otool -L` shows only macOS's own libraries and the app's own
  (no Homebrew path: the app runs on a Mac without Homebrew); no absolute rpath; all three programs carry the same
  version stamp (a build with uncommitted changes is refused; `ALLOW_DIRTY=1` makes a test package); the signature
  verifies; the privacy scan (no user name or home folder of the build machine in the app).
- **`SMOKE=1`:** opens the image, copies the app out of it as a player would, verifies its signature, runs the
  launcher's screenshot mode offscreen (every page drawn, every walk check PASS, SDL 3 loaded from inside the app)
  and, with `B3_GAME_DIR` and `SAVES`, `tools/native/race_check.sh` with `STRICT=1` on the app's own native_play (no
  fallback to retail code, VU0 all generated). Its files are in `build/release/smoke-mac/`.
- **`DRY_RUN=1`** runs anywhere (also on Linux): placeholder programs, the bundle's folders, `Info.plist` and texts in
  `build/release/stage-mac/`, the privacy scan, and the Mac-only commands printed instead of run.

**In CI** the GitHub Actions workflow `release-mac.yml` runs `SMOKE=1 tools/release/package_mac.sh` on a
`macos-latest` (Apple Silicon) runner, without game data, and keeps the `.dmg` as the `macos-dmg` artifact (the dSYMs
as `macos-dsym`, the logs and the launcher's pages as `macos-package-logs`). It runs by hand (`gh workflow run
release-mac.yml`, or the Actions tab) and on `v*` tags, not on every push.
