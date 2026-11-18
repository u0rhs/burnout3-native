# Release fact sheet

The facts for the release announcement, each with where it was verified. Keep this current as features land:
nothing goes into the post that is not on this page. Status: ✅ in the build · 🚧 built, being checked · 📋 planned.
The README's feature list uses the same marks and groups; keep the two in step.

Last updated: 2026-11-18 (version 1.2.0).

## What it is

- Burnout 3: Takedown (PS2, NTSC-U SLUS-21050) running natively on **Windows, Linux and macOS (Apple Silicon)**.
- Criterion's own game code, rebuilt from a matching decompilation and compiled to native C++: 5,652 game
  functions, 4,655 rebuilt byte for byte and 997 as complete C audited against retail; 6,915 functions in the native
  build (the README's badges and stat strip, `docs/status.json`).
- **No emulation and no PS2 code at run time**: every race mode and every front-end path (20 scripted scenarios)
  passes with `STRICT=1`, which halts if any function would run as original PS2 code; the VU0 micro programs are
  native too (`tools/native/race_check.sh`, the smoke tests of both packages). The game itself never runs PS2
  code: neither the EE (R5900) fallback that a developer build can run an unported function in (`make NATIVE_FALLBACK=1`)
  nor the VU0 / VU1 interpreter core is compiled into the game binary (`native_play`) or the launcher. One exception, in the
  presentation layer only: the renderer library (`libb3render` / `b3render.dll`) contains a small VU1 interpreter and runs
  the game's particle program (VU1 0x46D750: smoke, dust, glass, fire) on the game's particle data to draw those effects.
  It reads captured copies, never touches game memory, and cannot affect physics, AI or saves. A function missing from the native build stops
  the game with a message naming it and offers a bug report; it never runs the original code.
- **Physics and gameplay identical to the PS2**: the PS2's float behaviour is reproduced; the 20 scenarios give the
  same timeline, DMA / SIF / audio logs, guest RAM and screenshots on every run (`tools/golden/`), and every change
  is checked against that recording item by item.
- **Ready to play**: compiled ahead of time; unzip, point the launcher at your disc, play. No runtime download, no
  first-run compile.
- **You need your own copy of the game** (a disc image `.iso` / `.bin` / `.chd`, or the disc's files in a folder). No
  game data is included, and nothing writes to your disc files.
- **How it works**, with where each claim above is verified and the limits stated: [docs/TECHNICAL.md](../TECHNICAL.md).
  Quote technical claims from there.

## Platforms

| Platform | Graphics | Package | Verified |
|---|---|---|---|
| Windows 10 / 11 | Vulkan | zip with the launcher | the package passes its smoke test; played on a high-end desktop |
| Linux x86-64 | Vulkan | tarball, own SDL3 (X11, Wayland, PipeWire, PulseAudio, ALSA), glibc 2.35+ | the package passes its smoke test and was played on one desktop; other distributions and the Steam Deck: untested |
| macOS, Apple Silicon | Metal | a .dmg with Burnout 3 Native.app (the launcher; own SDL3; ad-hoc signed, not notarized: the first start needs Open Anyway), built by CI; or build from source | played on an M1 when built from source; the .dmg is built by CI (the launcher's checks pass, SDL3 from inside the app), and a build of it has been installed and run on a real Mac |

Speed (one high-end Windows desktop: a 16-core Ryzen, an RTX 4080-class GPU, 64 GB; in a window, perf1): menus ~460 fps,
races ~760 fps. Other machines will differ; these are not quoted as a headline figure. Unpaced (no frame limit, headless):
~280 game frames per second on Windows, i.e. well above the game's 60.

## Features (defaults never change gameplay; ⚙️ = off by default because it changes the game's input or what the game sees, or adds to the race)

### Graphics
- ✅ Uncapped frame rate: a frame limit of your choice, frames interpolated between the game's fixed steps.
- ✅ Widescreen and ultrawide: HUD and menus anchored to the edges; 4:3 movies with black or blurred sides.
- ✅ Field of view 60-120 (90 = the game's), with correction.
- ✅ Whole-track draw distance (no streaming pop-in); traffic fades in before the game's draw limit.
- ✅ MSAA up to 8x, anisotropic filtering up to 16x, car detail, shadows, motion blur, boost blur, bloom.
- ✅ HD texture packs (PCSX2 format) chosen from a folder dialog. The launcher's HD Textures page credits and links
  Panda_Venom's Burnout 3: Takedown USA HD Remaster (their forum post, their MediaFire download, their Ko-fi); the
  pack is never bundled or mirrored.
- ✅ The tracks' overhead wires and cables (line meshes, 34 of 37 routes): thin anti-aliased dark lines with the
  game's darkening blend and fog (lines1; before, the traffic lights hanging from them seemed to float). Unlike the
  PS2's blend order, the sky's highlight is not added on top of them, so they read as dark cables on every route.
- ✅ Quality presets: PS2 original, Low, Medium, High, Ultra.
- ✅ Photo mode: free camera, HUD off, screenshots, also mid-crash; **depth of field** (focus on the car or the
  centre, six strengths, in the screenshots too).
- ✅ Split-screen layout: the game's own top / bottom (the default) or side by side (SETTINGS > CAMERA > SPLIT SCREEN),
  each view at its own shape with no stretching, each player's HUD, messages and markers inside its view; presentation
  only (splitlayout1: offscreen shots at 16:9 and 21:9 in the 2-player tour; top / bottom byte for byte as before;
  golden). Side by side was in the PS2 code but never switched on.

### Front end and launcher
- ✅ PC SETTINGS inside the game's own main menu (plus QUIT GAME), in the game's fonts, applied at once.
- ✅ Launcher pages: Play, Settings, HD Textures, Music, Mods, Game Data, Saves, Stats, Showroom, Replays,
  Controllers, Credits.
- ✅ The launcher fully usable with the keyboard alone, a gamepad alone or the mouse alone (launcherux1: a walk check
  over every page).
- ✅ Skip intro movies (never automatic) with a hint.
- ✅ Saves of your own: up to five `.sav` files, each holding the game's three profiles; import PS2 saves (`.psu`, `.max`,
  PCSX2 cards); export; backups of the last 10 saves per file.
- ✅ Stats from your save and 13 local achievements worked out from it (launcher Stats page;
  src/native/save_stats.cpp); safe mode after crashes; bug reports (one zip with logs and a replayable recording; the
  reporter's user name taken out of every path in it).
- ✅ **Fast loading**: loading screens run at full speed with the game's timeline unchanged — a crash event's loading
  screens 18.3 s -> 3.1 s (fastload1, measured with identical logs on / off). The race's picture and sound start
  together: the track builds on a worker thread and a short silent hold covers the rest (loadlag1: first race frame
  114 ms after the race starts on Windows, as with fast loading off).
- ✅ **Car showroom**: every car on your disc (67) in 3D on a turntable, with the game's own names; drawn with the
  HD textures the game would use (the player's pack + mods' `[textures]` packs; showhd1: a test pack and a mod pack
  recolour the car in the launcher check, HD off gives the original pixels).
- ✅ **Skip the launcher**: "Start the game directly next time" on the Play page (= SETTINGS > INTERFACE > OPEN THE
  LAUNCHER AT START off); Shift / a pad's Back held, `--launcher`, missing game data or a bad last run bring the
  launcher back (launchskip1; Shift on Windows / macOS still to try on a real machine).
- ✅ **Update check**: once a day the launcher asks GitHub Releases for the latest release and shows a newer one with a
  link (nothing downloaded or installed; Settings > Launcher > CHECK FOR UPDATES; update1, canned answers in the
  launcher check; not yet tried against a real release).
- 📋 More than three profiles per save.

### Controls and accessibility
- ✅ Keyboard, mouse in the menus, any SDL gamepad; typing on the profile-name screen.
- ✅ Button prompts that match your device (PlayStation, Xbox, Nintendo, generic, keyboard) and follow the last one used.
- ✅ Speed units: MPH (the US game's, the default) or KM/H (SETTINGS > INTERFACE > SPEED UNITS) on the speedometer,
  distance messages, score list, records and stats, with the game's own PAL words; text only (units1: KM/H race
  shot, the speed in m/s equal on every VBlank and the numbers 1.61 times the MPH run's, guest RAM different only in
  HUD text, draw packets and clock-derived audio stamps; golden byte-identical at MPH).
- ✅ Local 2-player: a second gamepad is player 2.
- ✅ Rumble ⚙️ (the game's own vibration, strength slider, Xbox trigger rumble).
- ✅ DualSense adaptive triggers (off by default; presentation only: the brake resists, the accelerator buzzes while
  boosting, both kick at a crash, free in menus and paused) and gyro steering ⚙️ (opt-in per player: tilt or turn,
  mixed with the stick or instead of it, sensitivity, dead zone, recentre button; the report the game reads, so
  replays keep it; off: every report byte for byte as before) (pad1: unit tests on synthetic sensor events; golden).
  Not yet felt with a real pad.
- ✅ Accessibility: reduced screen flashes, colour-blind HUD filters (deuteranopia, protanopia, tritanopia), boost
  toggle ⚙️ or hold (the default).
- ✅ HUD size: the race HUD at 100 / 115 / 130 / 150 % (SETTINGS > INTERFACE > ACCESSIBILITY), each part grown from
  its own anchor, kept on screen and apart from the others; menus stay at 100 %; 100 % is the game's picture byte for
  byte (textsize1: offscreen shots at 4:3, 16:9 and 21:9; golden). Not yet looked at in a window.
- ✅ Remapping per player (1 and 2), keyboard and gamepad: in PC SETTINGS > INTERFACE > CONTROLS and the launcher's
  Controllers page; conflicts offer swap / clear / keep both; prompts follow the bindings; defaults identical to the
  original mapping (every key and 200,000 random pad states tested); replays unaffected by later rebinding (remap1).
- ✅ Car series on LB / RB ⚙️ (opt-in): on the car select and YOUR GARAGE, L1 / R1 change the series exactly as left /
  right do, with the device's glyphs beside the series arrows (carsel1: a strict carselect run through three series,
  replayed exactly; golden identical with it off).

### Music
- ✅ Your own music in EA Trax: EA Trax only, your songs only, or mixed; the game's radio plays them.
- ✅ Now-playing popup with your song's title, artist and album in the game's own display.
- ✅ Other music apps (Linux; 🚧 Windows: built, desktop check pending) ⚙️: the song Spotify, a browser or any player is playing shows in the same
  popup, read from the system's now-playing interface (Windows media sessions, Linux MPRIS); off by default, nothing
  read while off; not on macOS (no public interface). Linux checked end to end against test players on a private
  D-Bus (`tests/test_now_playing.cpp`); the Windows reader builds and its logic is unit-tested through the interface.

### Play
- ✅ **Replay files**: whole sessions as small files (a 6.5-minute race: ~80 KB) that play back exactly; Replays page.
- ✅ Time Attack ghosts ⚙️.
- ✅ Speedrun timer: game time, the HUD's own digits, splits and bests per event and car.
- ✅ Instant restart ⚙️: F5 or Select + Start, through the game's own pause menu.

## Modding (docs/modding/)

- ✅ Mod loader: mod folders or `.b3mod` zips, load order and on / off on the launcher's Mods page, conflicts and
  requirements shown; mods overlay the disc's files for both the game and the renderer; your disc is never changed.
- ✅ Patch mods: a mod can ship `.b3patch` deltas instead of game files; applied to the player's own disc at start,
  checked against the source file's hash.
- ✅ Per-mod HD texture packs.
- ✅ `b3modtool`: textures <-> PNG, the game's text <-> text files, music / sounds / voices <-> WAV, the tuning file
  <-> text, patches, pack and verify. Round trips verified byte-identical on the whole disc: 415 / 415 textures,
  4,017 / 4,017 strings, 108 / 108 audio streams, 174 / 174 sound banks, the tuning file (7,525 values).
- ✅ File-format reference for every file on the disc (775 files); the game's 705 tuning-variable registrations listed.
- ✅ Ten tutorials (docs/modding/tutorials/): replace a texture, change menu text, a settings row, a tuning preset,
  an overlay, a Lua script, export a car, edit a car, car mods in Blender, edit a track. 01 / 02 followed end to end in the game
  (tutorial_check); 03 / 05 by sdk_check; 04's kind of preset by tuning_check; 06's example
  in a strict race; 07 / 08 by gltf_check / car_mod_check; 10's
  terminal steps run for real (track_edit_check); 09 and 10's Blender steps run in Blender 4.2.23 LTS by a script
  (blender_check).
- ✅ Code-mod SDK: a versioned C API (hook or replace any game function, call game functions, safe memory access,
  settings rows, on-screen overlays, per-frame events), templates for Windows / Linux / macOS, two example mods
  verified in the game and in replays; code mods need the player's OK on the Mods page (sdk_check).
- ✅ Tuning console: the game's own tuning variables live (DEVELOPER tab, F9 overlay), presets as mods, edits replay
  exactly (tuning_check: 1,088 live registrations in a race). The F9 overlay in tabs, sections and pages with
  breadcrumbs, a YOUR CAR page (boost, freeze damage, repair), pinned favourites, recently changed values and search;
  keyboard or pad (tunenav1; test_tune_browse, tuning_check's overlay session replays equal).
  A reference of what every one of the 705 values does, in the console's order (docs/modding/tuning-options.md; 624
  read in the code, the rest marked likely or no effect), kept complete by test_tune_browse.
- ✅ Lua scripting: sandboxed (instruction budget, memory cap, no file or OS access) and
  deterministic (replays exactly); the example speedometer matches the game's own speed reading in a race.
- ✅ Car and track export (glTF / OBJ, with textures, paints as material variants, a sidecar for later import): all
  67 player cars and all 37 routes (2,838 sections) export and validate; every car's 268 LODs match the renderer's own
  decode exactly; a tutorial for opening a car in Blender (gltf_check, model_export_check).
- ✅ Car import: an edited glTF (from Blender or any editor) back into the game's car file as a patch mod;
  unedited, all 67 cars come back byte for byte; moved / scaled vertices, UVs, paints and new triangles; checked
  in a race with a bigger, recoloured car (model_import_check, car_mod_check; tutorial 08). Collision keeps the
  original hull.
- ✅ Blender add-on (B3 Car Tools, Blender 4.2+): File > Import > Burnout 3 Car from your disc, File > Export >
  Burnout 3 Car Mod (folder or .b3mod), a Check button, b3modtool's refusals as Blender errors; tutorial 09. Run in
  Blender 4.2.23 LTS (headless): three cars round-trip byte for byte, a body scaled in Blender as a mod in a strict
  race (blender_check). Car exports keep their damage-zone data through Blender's glTF import / export (67 / 67
  modelled from Blender's importer and exporter).
- ✅ Routes through Blender's own glTF add-on: unedited, all 37 routes come back byte for byte; a recolour and a
  moved section made in Blender import in place and show in a strict race (blender_check; tutorial 10).
- ✅ Track import: an edited route's glTF back into the game's STATIC.DAT / STREAMED.DAT as a patch mod (b3modtool
  `track import`): moved vertices and objects, colours, UVs, normals, added / removed triangles, textures, material
  swaps; refusals with a message for what the format can't hold. Unedited, all 37 routes come back byte for byte
  (also shuffled; restripped decode-identical, 140,896 meshes); checked in a strict race with a recoloured route
  (track_import_check, track_edit_check; tutorial 10). Visual only: collision and AI keep the original track.
- 📋 New car slots, events and crash junctions; sky (ENVIRO.DAT) import.

## Online

- 🚧 Online play (EA's servers are gone): coming in a future release.

## Open source

- The repository goes public on release. The port's own code (platform layer, renderer, launcher, tools, mod kit,
  SDK) is MIT. Game data is never included; the code generated from the decompilation is the game's own code and is
  not covered by that licence.
- Contributions welcome: CONTRIBUTING, issue forms for bugs (with the bug-report zip), features and mod questions;
  every push and pull request builds and runs the unit tests on Linux and macOS (GitHub Actions, both green).

## Still to do before the post

- Checks in a window: rumble, remapping (rebind, conflicts, a real pad), a second controller, fast loading's look and sound, depth of field,
  showroom, replays, frame pacing, HUD size, split screen side by side
  with two pads, adaptive triggers and gyro steering with real pads.
- A small tester round (Windows, a real Linux distro / Steam Deck, Mac).
- Player README: written (docs/PLAYING.md: what you need, making a disc image, install, launcher, controls, saves,
  settings, mods, troubleshooting, known issues); download hosting.
- Mac: re-test the newest build (the depth-of-field shader has not been compiled on a Mac yet).
