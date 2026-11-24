# Remaster feature backlog

Features planned for the port (listed 2026-11-01). They are recorded here so each one is designed in when
the port reaches that area, not bolted on afterwards. **Hard rule for all of them: no change to physics, AI or
gameplay results.** A feature may only touch presentation, input, menus and settings, and the native checks
(`race_check.sh` with `STRICT=1` and its scenarios, the golden runs) must stay identical with every feature in its
default state.

Sections 1-5 are the first list; each later section was added when its study or feature landed, in the order they
were merged, so their dates are not always in sequence. Each section describes its feature as merged; since 1.4.0
(§45, extras) every addition to the game itself is behind its own row on SETTINGS > EXTRAS, off by default but
ELIMINATOR, and night and rain ship there as EXPERIMENTAL.

Class names below come from the decomp's symbol tables (E-map / Revenge evidence); what each class does is read
from its name and from the decomp source, tagged [L] where read from code and [G] where only from the name.

## 1. Our own settings menu inside the game's front end

A native in-game section for PC settings (graphics, audio, controls, port options), **added alongside** the
existing menus, without replacing any of Criterion's menu items.

- Hooks: the front end is a page state machine: `CB3MenuFlowFSM` (flow and events), `CB3MenuPageManager` (page
  stack), `CB3MenuPageBase` and the page classes (`CB3MainMenuPage`, `CB3MyBurnoutPage`,
  `CB3MyBurnoutSettingsVideoPage`, `CB3GenericVSelectPage`, ...), built from `CB3MenuComponentBase` widgets
  (`CB3VSelectComponent`, scroll bars, text components) with `CB3MenuComponentSelectionData` choice lists [G names].
- Approach: new page classes written in the port, in Criterion's own widget style and fonts, so they look native.
  Entry point (2026-11-02): **on the main menu itself**, not tucked inside My Burnout, and without removing any of the original entries. The main menu is a fixed vertical list (5 rows retail;
  the PC build adds a 6th, QUIT GAME, so the list takes extra rows), so the plan is a 7th row "PC SETTINGS" (in the game today: "SETTINGS")
  above QUIT GAME, laid out in the original style; if the main menu page's layout can't fit a 7th row cleanly, the
  fallback is a footer prompt (a button hint like the existing "back"/"select" ones). Its FSM event goes to our page. Strings: our own string table next to
  the game's language files, so translations work the same way.
- Settings are stored in a host config file (not in the PS2 profile), so original saves stay untouched and portable.
- Sub-pages: Graphics (resolution, window mode, framerate cap / uncapped with interpolation, ultrawide, draw
  distance, motion blur, anti-aliasing, texture filtering), Audio (volumes, output device), Controls (section 3),
  Port (the video skip prompt, section 2; PS2-faithful vs fast presentation options).
- Prerequisites: front-end page classes running natively (decomp game code, phase 4); the native renderer for the
  graphics options (phase 6).

- **Status (settings1, 2026-11-05):** built for the GS-off window. "SETTINGS" (first named "PC SETTINGS") is the main menu's 7th row (above QUIT
  GAME); our own ini, not the PS2 profile; tabs with presets (today seven: DISPLAY, GRAPHICS, EFFECTS, CAMERA,
  INTERFACE, SAVES, MODS); every row
  drawn-only or host-only. A "GAMEPLAY MODS" tab has room reserved (off by default, marks sessions as modded): not built.
  Details and what is still to come: `docs/pc-settings.md` §6.

## 2. Skip intro videos

- The boot logo/attract videos, and the video that plays after creating a new profile before the first race.
- Hooks: `CB3VideoManager` and `CB3PlayVideoPage` [G names]; the disc's videos are the `.m2v` (MPEG-2) streams.
- **The player decides, nothing is skipped automatically (2026-11-02).** Every video plays as the original
  does, and the player can skip it with a button press. The new-profile video before the first race (about 3
  minutes, unskippable on PS2) is the main case.
- Prompt: a small "skip" hint in a screen corner with the button glyph for the device in use (section 3's glyph
  switching: PlayStation / Xbox / Nintendo / key cap), shown while a skippable video plays; it may fade in after a
  moment or on the first input so it doesn't sit over the video from frame one. Pressing that button skips; other
  buttons do nothing, so a stray press doesn't skip by accident.
- Mechanism: the skip makes the video page finish and post the same completion event to the menu FSM as a finished
  video, so the flow after it is unchanged.
- Prerequisite: the video page running natively, or a narrow override of its play/finish functions.
- **Done in native_play (mac4, 2026-11-08)**, `tools/native/movie_skip.h`: Start or Cross newly pressed while a movie
  plays ends it (keyboard: Enter / Space = Cross, Escape = Start, docs/mac.md §5) [G: a choice of buttons;
  a single dedicated button, as above, is a one-line change]. Mechanism [C decomp / asm]: every movie runs on the
  front end's `CGtVideoDecoder` (CB3FrontEnd 0x51A6A8 + 0x1C8 = 0x51A870); a skip is the decoder's own `Abort`
  (0x324ED0, `+0x4A = 1`), the flag `Update` (0x325030) sets itself when a non-looping stream ends, after which Update
  closes the file and sets `+0x49` (IsCompleted 0x324EE0) that the pages wait for -- the same path as a natural end.
  Looping movies (`+0x4B`, the menus' background) are never skipped; the skipping press is masked until released so
  the next page does not see it. `--no-movie-skip` turns it off.
- **The hint, done in native_play (mac5, 2026-11-09)**: a small dark pill in the bottom right corner of the window's
  centred 16:9 part (5 % of the height in from its edges) with the skip button of the device in use and "SKIP": the
  PlayStation Cross (blue), the Xbox A (green), the Nintendo B (the bottom face button, which the pad maps to Cross), or
  an ENTER key cap for the keyboard. The device is the last one used (a key, a gamepad button or stick, a pad
  connected; src/b3replay/nativepad.h `device()`, SDL's label of the pad's bottom face button). It fades in 0.75 s
  after a skippable movie starts, stays 5 s, fades out, and comes back for 5 s on any other press; its opacity is
  computed in game time (tools/native/movie_skip.h SkipHintFade) and travels with the poll's record ('HSK1',
  src/b3replay/hud.h kHostSkipBase), so offscreen shots and replays show it at the same polls. Our own picture
  (src/b3replay/skiphint.h: shapes and a small stroke font, rasterised at the window's pixel size), since the game's
  fonts have no button glyphs. SETTINGS > GAME > INTERFACE > VIDEO SKIP HINT turns it off. The skip itself is unchanged
  (Start or Cross, as above).

## 3. Controls: remapping, button prompts, keyboard, automatic switching

- **Custom mapping** for controllers and keyboard, per player, saved in the host config. Mapped at the input layer
  (where pad state enters the game), so game code sees the same logical buttons it always did; physics and handling
  stay identical because the inputs are the same values. **Done (remap1, 2026-11-12)**, docs/pc-settings.md §7.9:
  `src/native/input_bindings.h` binds each of the 24 PS2 actions (the 16 buttons, both sticks' half axes) to keys
  (per context: driving / menus) or gamepad inputs (buttons, stick half axes, triggers with their pressure), per
  player; controls.ini in the user data folder (versioned, invalid entries reported and skipped); edited in SETTINGS >
  CONTROLS > BINDINGS and on the launcher's Controllers page (press the input, a timeout and Escape to cancel, a
  conflict offers swap / clear / keep both, reset to defaults); the button prompts show the bound key or button. The
  defaults are the mapping before it, byte for byte; pad recordings and replay files keep the reports (post-mapping).
  **Shortcuts (rebind2)**: the port's own actions are bindable too (`PortAction`: restart, fullscreen, screenshot, bug
  report, the tuning overlay, the settings sections, photo mode's entry and its camera controls), on any key and a
  gamepad chord, in the same pages (Player: Shortcuts), the same conflict flow, a reset per control and for everything;
  only Escape (cancels a capture) cannot be taken, and Ctrl / Shift / Alt / GUI can be bound.
- **Button prompts that match the device in use:** PlayStation, Xbox, Nintendo or generic glyphs for controllers;
  key caps for the keyboard (showing the actual bound key). Hooks: the HUD/menu prompt drawing and the button glyph
  textures/font characters it uses (`CB3Button`, `CB3HUDDPad`, `CB3DPad` [G names]); our glyph atlas replaces the
  PS2 glyphs at draw time.
- **Keyboard support** as a first-class device, plus mouse in our own menus.
- **Automatic switching:** the last device that produced input becomes the active one; prompts switch with it,
  without a menu toggle (with a short hysteresis so analog noise doesn't flip it).
- Hooks: `CB3InputManager`, `CB3ControllerMapping` (the game's own mapping table, which may already allow layouts)
  and `CGtInputDevicePS2DualShock` [L: its GetControlValue / DeadzoneButton already run natively in the port as
  matched decomp code]. The port's platform layer (SDL3 gamepad + keyboard) feeds a DualShock-shaped device, so the
  game's input code stays unchanged.
- Analog values (steering, throttle) must reach the game with the PS2's value range and dead zones, or handling would
  change; remapping must never rescale them unless the user chooses a curve option, which is off by default.
- **Button prompts and automatic switching: done in native_play (mac5, 2026-11-09)**, src/b3replay/prompts.h. Drawn
  only: the renderer redraws the game's button draws from our own atlas (shapes and a stroke font, glyphdraw.h,
  rasterised at the window's 2D scale), keeping each draw's place, size, colour and order. How a prompt reaches the
  draw list [C]: a prompt is a GlobalFont text object plus a button object (render callback 0x382950, prepared by
  0x382C00 with a button index 0..10) that draws two sprites from the front end's "Buttons" texture (128 x 64): the
  base (the white ring, tinted with the menu's text colour) and the symbol inset in it; the texel rectangles are the
  static tables the __sinit 0x4D9B10 fills (index 0 Triangle, 1 Cross, 2 Start (33 x 21), 3/6/7/10 Square, 4/9
  Circle; 5 / 8 the 64 x 32 shoulder pictures, textures D_01EA472C / D_01EA4728, "l2button" / "l1button" [G: by the
  name table's order; the renderer goes by the raster name]). The d-pad hint (CB3DPad2dObject; callbacks 0x31C990 /
  0x371BE0) draws four arms from "dpad" (32 x 16), each arm's colour saying whether that direction acts on the page.
  The renderer recognises those pieces by raster name and texel rectangle and maps each vertex into our cell (flips
  and mirrors carry over; a symbol's quad grows to its button's box so a key's name has room); a draw with any piece
  it does not know stays the game's. Devices: PlayStation = the game's own pictures (no substitution; PS3 pads, and
  while no device is known yet), PS4 / PS5 (2026-11-09: those pads have no Start / Select; SDL_GetGamepadType PS4
  / PS5): the game's own pictures but Start, drawn as the OPTIONS button (a small pill with three bars over the word);
  the game never draws a Select prompt (none of the button object's 11 indices is Select), so CREATE / SHARE are
  labels only), Xbox (A green / B red / X blue / Y yellow letters in the ring, LB RB LT RT pills, Menu / View),
  Nintendo (B / A / Y / X by SDL's positions: Cross is the South button, labelled B), generic (the four positions with
  the one meant filled, L1 / R1 / L2 / R2, START / SELECT) and the keyboard (key caps with the key bound to that button
  in the current context: Enter, Backspace, Escape, X, Q, R ... from the key map, src/native/input_keys.h; the d-pad as
  four arrow-key caps). The device is the last one used (NativePad: a key, a pad button, a pad connected switch at
  once; a stick or trigger must stay past half way 0.15 s and not within 0.5 s of the last switch); SETTINGS >
  GAME > INTERFACE > BUTTON PROMPTS: Auto / PlayStation / PS4 / PS5 / Xbox / Nintendo / Generic / Keyboard (ini `button_prompts`). Covered:
  the front end's prompt bar on every page and dialog (Select / Back / Continue / Change), the d-pad hint, the race
  intro's and the pause menu's prompts (START Resume), the shoulder pictures (by name, not seen in the checked flows),
  the SETTINGS page's tab buttons, the video skip hint, and (padr2, 2026-11-13) the HUD's own shoulder pictures, looked
  up by name: "R2Button" beside CRASHBREAKER in Crash events (CB3HUDCrashStyle 0x3A3250: when the Crashbreaker is
  ready, string 0x8BA and the picture, 45 x 25, the whole 64 x 32 raster in one quad), "R1Button" (Impact Time),
  "L2Button" (0x435AB0, beside "Talk"); the program finds every shoulder picture by a case-insensitive name, so the
  draws carry the GLOBAL.TXD raster names ("L1Button" ... ) and the renderer matches any case. The captured image quad
  samples texels 1,1..64,32 (race_check SCENARIO=crash, Impact Time at poll 12100: drawn "RB" with the Xbox look). With the default pad
  bindings (L2 / R2 on the stick clicks) the Crashbreaker picture reads R3 / RS. Not covered yet: the Aftertouch d-pad
  (CB3HUDDPad2dObject, its own texture), text strings naming buttons (the mouse counts as the keyboard: its key caps stay while it is used, §8).
- **Rumble (rumble1, 2026-11-12):** the game's own motor requests to the pads, opt-in (§5 "Modern controller features",
  docs/pc-settings.md §7.4).
- **Car series on the shoulders (carsel1, 2026-11-13; LB / RB change the series, with glyphs beside the arrows):** on by default since 2026-11-23 (off before), input-changing (docs/pc-settings.md §7.13). The car select changes the
  series with Left / Right and reads no shoulder button [C CB3CarSelectPage::Update 0x318A80]; with SETTINGS >
  CAR > GARAGE > CAR SERIES ON L1 / R1 On, a press of L1 / R1 on the car select or YOUR GARAGE page reaches the game as
  Left / Right (the game's own series change), and the device's L1 / R1 glyphs are drawn beside the series arrows.

## 4. More features (see the README's goal)

Uncapped framerate with fixed-step simulation and interpolation, ultrawide, draw distance, motion blur, modern
rendering, custom cars/liveries/tracks, online play.

## 5. More features (proposed 2026-11-01)

Gameplay-changing items are **opt-in** and never part of the default game.

**Built on what is already in the game**
- **Developer tuning console for modders.** Retail registers its tuning variables with a variable debugger (the
  `CGtCommsDatabase::AddEntry` calls; RegisterStaticVariables__8CB3ScoreFv alone registers 82 [C decomp, draftW]). Expose that
  registry in-game: Criterion's own knobs, read/write, with presets saved per mod.
  *Done (v1, tune4 2026-11-12, mod kit part 4; docs/modding/tuning.md, docs/pc-settings.md §7.10):* SETTINGS >
  MODS > DEVELOPER > DEVELOPER MODS Off / On (default Off: nothing installed, the run byte-identical; from the next
  start). On, native_play wraps the three AddEntry functions (float 0x0021BB60, int 0x0021BD80, vector 0x0021B9B0) on the
  hook table's port layer and records every variable as it registers (address, name, group, config file, range, count,
  key; read only). SETTINGS gets a DEVELOPER tab (the variables registered now, by group, with their values; Left /
  Right within the range, Cross back to its default, a CHANGED tab and RESET ALL; SAVE AS PRESET writes the session's changes as a new mod) and
  F9 a live overlay in the window. **Gameplay-changing, opt-in, recorded**: an edit is written at the next VBlank and
  goes into the pad recording (`tune` lines) and the replay file (SETS `tune` entries), so replays play it exactly.
  Mods carry presets (`[tuning] preset = tuning/x.ini`: `<group> | <name> = <value>` lines, applied as each variable
  registers, in load order), always marked as changing gameplay (Mods page, log, recordings). Scripted edits:
  `native_play --tune`. *tunenav1 (2026-11-14):* the overlay in tabs, sections and pages with breadcrumbs; a YOUR CAR page
  (boost, freeze damage, repair; recorded like any edit); pinned favourites and the last 12 changed values
  (`tuning_console.ini`); search (`/` or Ctrl+F). Not yet: the overlay seen in a window run, editing variables that
  are not registered at the moment.
- **Showroom / garage.** `CB3CarViewerGameMode` exists in retail [L name; its Update is drafted]. Unlock it as a
  showroom with a free camera; later a livery editor on top.
  *Done as a launcher page (v1, photo2 2026-11-12, src/b3launcher/showroom.h, docs/pc-settings.md §7.7):* SHOWROOM shows
  every player car on the user's disc in 3D on a turntable (mouse drag / right stick turn it, the wheel comes nearer,
  left / right or LT / RT the next car, the paint jobs), named from the game's own string table, drawn with the
  renderer's car model offscreen, with the HD textures the game would use (the player's pack when HD TEXTURES is on
  and the enabled mods' texture packs; showhd1 2026-11-14). Read only: nothing unlocked, no save read or written. Not the game's own car viewer
  mode (that would run inside the game); a livery editor not started.
- **Crash-junction editor.** Crash mode junctions are data setups (spawns, traffic, scoring): a junction editor is
  the smallest step towards custom content.
- **PS2 save import.** One-click import of a PS2 memory-card save (`.psu`, PCSX2 `.ps2`; conversion already proven
  with mymcplus) into the port's profile.

**Possible because the simulation is deterministic (proven by the golden runs)**
- **Replay files** as input recordings that replay exactly; save and share races and crashes.
  *Done (v1, replay1 2026-11-12, src/native/replay_file.h; docs/pc-settings.md §7.5):* SETTINGS > GAME > MODES >
  RECORD REPLAYS Off / On (default Off: nothing recorded, the run byte-identical). On, native_play keeps each window
  session from its boot to its end as `<prefs>/replays/<YYYYMMDD-HHMMSS>.b3replay` (written on quit, the newest 20
  automatic ones kept; a renamed file is never removed). A replay is the session's inputs only: both ports' pad ops
  per VBlank (the reports the game got, plug-ins / pull-outs), the characters typed into the on-screen keyboard, the
  profile .sav as it was at the boot, the console clock and every other game-side option / environment value, the live
  settings the game reads (date format, speed units, music: latched per VBlank while recording), RAM checkpoints every 600 VBlanks,
  the disc program's size + FNV-1a 64 and the port version; versioned sections, CRC-32. The launcher's **Replays** page
  lists them (date, length, size, version), plays one (`native_play --replay`: the recorded inputs from the boot, a
  throwaway copy of the card so the player's saves are untouched, the window's controls ignored but Start = pause) and
  deletes / shows them. Refused with a message: another disc program, another port release (the VERSION base), a newer
  format, a damaged file, custom music whose files are not there as recorded. Another commit of the same release plays
  with a warning; the checkpoints then say whether, and from when, it parted (a message box at the end). Proof:
  `tools/native/replay_check.sh` (a race_check scenario recorded, then played from its file: every baseline.sh log
  equal). A replay always starts at the boot (the whole state comes from boot + card), so it cannot begin mid-session;
  not yet: a seek / fast-forward, the track's name in the list, sharing from inside the game.
- **Ghosts** for Burning Laps (own and friends').
  *Done (own ghosts, v1, ghost1 2026-11-10, src/b3replay/ghost.h; docs/pc-settings.md §7.2):* SETTINGS > GAME >
  MODES > GHOST Off / Best lap (default Off: nothing recorded or drawn, frames byte-identical). In Time Attack
  (SINGLE EVENT > TIME ATTACK: the stage logic CB3OfflineBurningLapLogic, vtable 0x4DEBC0), the renderer records the
  player's car per tick (main, body and wheel matrices) between the lap line crossings the game's own CB3Score shows
  (lap +0x14, lap timer +0x2C8, read from the capture image: no capture change, no guest write); the best lap per
  track + car is saved to `<prefs>/ghosts/<track>-<car>.b3ghost` (versioned, checksummed, written atomically; our
  transforms only). Later runs on the track draw its best lap (any car the disc has) at the pose of the current lap's
  time, screen-door faded, no shadow / lights / sound / collision; the game never knows. Not yet: friends' ghosts
  (sharing / importing a .b3ghost: the format is ready, a launcher page to pick one is not), other modes (races
  against the clock elsewhere), a ghost of the run's own previous lap. Not a replay file: the ghost is transforms, so
  it needs no determinism (an input-recording ghost would need the whole race re-simulated).
- **Speedrun support:** in-game timer, instant event restart, crash-junction retry; times comparable to PS2.
  *Done (timer and instant restart, speedrun1 2026-11-12, src/b3replay/speedrun.h; docs/pc-settings.md §7.3):* PC
  SETTINGS > GAME > MODES > SPEEDRUN TIMER Off / Event / Event + splits (default Off: nothing read, kept or drawn;
  frames byte-identical): a panel with the event's time in game time -- the game's own race time, CB3Score +0xC (the
  game timer minus the start time, from the start signal, frozen at the finish: the value the race results page takes
  from the score copy SetFinished makes) -- printed as the game prints its lap timer (func_00155280 + "%02d:%02d:%02d"),
  the lap splits (+0x1C[8]) against the kept best, and the best per event + car in `<prefs>/speedrun/*.b3split`
  (versioned, checksummed, atomic). Time Attack: the running lap and the best lap. Read only (two capture grabs of
  CB3Score), no wall clock. INSTANT RESTART Off / On (default Off; **input-injecting**, opt-in): F5 or Select + Start
  while driving plays the pause menu's own RESTART, YES as pad presses on the VBlank grid; the game restarts the event
  itself. Times are the game's own timer, so they match the PS2's for the same run (the timer counts game ticks, never
  host time). Not yet: crash-junction retry, a timer for crash modes,
  a whole-run (multi-event) timer.
- **Online play** (coming in a future release).

**Presentation and quality of life**
- **Custom soundtrack:** user music folder alongside or instead of the EA Trax playlist; DJ on/off.
- **Photo mode:** freeze in Impact Time or a Takedown cam; free camera, depth of field, HUD off.
  *Done (v1, 2026-11-10, src/b3replay/photomode.h):* from the race's pause menu (pause during a crash, Impact Time or a
  Takedown cam to freeze there): the PHOTO MODE row of the pause menu, or P or L3 + R3; free camera (mouse / WASD / pad), field of view, roll, HUD and menu off,
  F12 shots without the help box; renderer only, the game sees a neutral pad meanwhile.
  *Depth of field (photo2, 2026-11-12, docs/pc-settings.md §7.6):* F or Back / Select turns it on (off by default), V / B
  or Start the blur strength (6 levels), N / M or the D-pad left / right the focus nearer / farther, G or L3 the focus
  on the player's car or on the screen centre's depth; a bokeh-style gather blur on a depth pre-pass, in screenshots
  too. Photo mode closed: nothing of it runs.
  *Motion blur (photoblur, 1.3.0, a player request; FrameParams::Motion, shaders/motion.frag):* J or a click of R3 alone
  (released within 0.3 s, the right stick not moved: R3 is the look stick's button) cycles
  Off (default) / Tracking / Panning / Static; K / L or R3 + D-pad left / right the shutter, 1/500 to 1/15 s (1/60 at
  first). One screen-space pass at the start of the 2D stage's phase (after the night grade and the headlight pools,
  before the bloom, the zoom blur, the depth of field and the HUD): each pixel's world position from the depth
  pre-pass the depth of field draws, moved over the shutter and reprojected, gives its streak. A pixel inside a car's
  box (the shadow box, a little larger, down to 5 cm over the road; up to 8, the player's car first, then the nearest
  drawn cars within 300 m) moves with that car. Tracking moves the camera with the player's car, Panning turns it about
  its position to keep the car's middle in place, Static holds it; the player's car's relative motion is then zero
  (sharp) and other cars streak by their speed relative to it. The cars' velocities: the renderer keeps no velocities
  and the paused game repeats the same matrices every tick (the feed's last two ticks at photo mode's entry are equal),
  so the feed records each car's velocity from the last ticks in which it moved, over the game's own timer (a 50 Hz
  disc's ticks are 1/50 s; src/b3replay/live.h VehicleMotion, into Vehicle::motion), held while nothing in the world
  moves; no game state is read. The gather: a tile max (32
  pixels) and neighbour max of the streaks, then 8 time slices per frame, each showing a nearer, differently moving
  pixel whose streak reaches this one (a car smears past its outline, leaves over the sky between them) or else the
  pixel's own surface along its own streak (a pixel off every car skips places on a car, so the sharp subject does not
  bleed into the background). The frame is frozen, so each window frame adds 8 slices to an accumulation, 16 frames
  (128 slices, a quarter of a second at 60 fps); any change of the camera, the motion, the frame's look (night, fog,
  car look, surface), the 3D draw list or what it samples (a texture's content, the HD pack on / off, a car's paint)
  restarts it; once converged the window draws no pre-pass or streaks, only the copy. Photo mode never runs in the
  two-player split screen.
  An offscreen draw (F12, a scripted shot) takes all 16 at once, so screenshots are always the finished picture.
  MSAA: it reads the resolved picture; RENDER SCALE: it runs at the scaled size. Things without depth (smoke, sparks,
  glass) streak with what is behind them. B3GFX_MOTION_DEBUG=1 shows the streaks in false colour. Off (the default) or
  photo mode closed: nothing of it runs, and the frames are as before.
  *Pause menu row (photorow1, 2026-11-15, docs/pc-settings.md §7.17):* a PHOTO MODE row after SETTINGS in the race's
  pause menu (not in the 2-player split screen) enters photo mode as P does; SETTINGS > GAME > INTERFACE > PHOTO MODE IN
  PAUSE MENU (on by default) turns the row off.
  *On-screen display (photohud1, 2026-11-15, src/b3replay/photohud.h):* the plain help box became cut-corner plates in
  the notices' amber-on-dark look: readouts at the top right (mode, field of view, tilt, depth of field level and focus
  as segmented gauges) and a controls strip at the bottom (key caps from one key table shared with the camera, or the
  button prompts' glyphs of the pad in use). H / Y cycles everything, readouts only, hidden; plates slide and fade.
  Placed from the window's edges at every aspect ratio and HUD SIZE (a strip that does not fit loses its least
  important groups, then shrinks). Drawn after the screenshot is taken: pictures never hold it.
  *Held options (devnav1, 2026-11-13, a player report: zooming slow and stuttering on a Mac):* the field of
  view zooms by a ratio (x2 or x0.5 in 0.6 s, the same feel narrow and wide; it was 0.6 rad/s, 1.7 s for 60 to 120
  degrees), the roll turns 1.2 rad/s (was 0.8), the focus 1.5 stops a second (was 1); each goes up to twice as fast
  after a second held, Shift / A x2, Alt x0.25. The lag was the help box: it shows the field of view and the focus,
  so it was drawn again every frame they changed, and that drawing took ~1.2 s at a Retina window's size (each frame
  of a held key took that long and moved 0.25 s at most); now ~2 ms (only the changed line, a faster stroke painter).
  Game time (offscreen shots, replays) steps the same rules a poll at a time.
- **Widescreen HUD anchoring and an FOV slider.**
- **Fast loading:** skip loading screens when the data is ready.
  *Done (fastload1, 2026-11-12; docs/pc-settings.md §7.8, tools/native/fast_load.h):* SETTINGS > GAME > STARTING AND LOADING > FAST
  LOADING, On by default. While the game's loading screen is up (CB3MenuPageManager's current page is
  CB3LoadingScreenPage, the game's own test), native_play stops pacing the game to real time: the load runs as fast as
  the PC can. Nothing is skipped: the game goes through exactly the same VBlanks with the same state, only sooner
  (`tools/native/fastload_check.sh`: logs, RAM, shots and pads equal unpaced, paced, and paced with it). The loading
  screen's animations run fast with it; the tail of the sound before the load plays in real time, the load's silence
  is muted; pacing resumes at the first VBlank after it without a catch-up. A crash event's 5 loading screens: 18.3 s
  -> 3.1 s. The boot's loading screen is covered too; its movies and the title play at their own speed.
  *Fixed (loadlag1, 2026-11-13; pc-settings §7.8):* a player report heard the race start under the loading screen:
  the renderer's track build (0.4-0.7 s) no longer fitted in the fast load. The track is now decoded on a worker and
  the race's first VBlank waits (wall time only, at most the time FAST LOADING saved) until the renderer has it.
- **Modern controller features:** DualSense adaptive triggers (boost, brake), HD rumble (Switch Pro controller, Steam Deck),
  opt-in gyro steering (opt-in because it changes input values).
  **Rumble done (rumble1, 2026-11-12, docs/pc-settings.md §7.4):** SETTINGS > CONTROLS > FEEDBACK: RUMBLE Off / On
  (default Off), RUMBLE STRENGTH 10-100 %, TRIGGER RUMBLE (Xbox impulse triggers). The game's own vibration requests
  (libvib through sceDbcSendData2: the small motor on / off, the large motor 0-254) are acknowledged as the PS2's
  device manager does (the RPC's reply) and given to player 1's and player 2's pads through SDL_RumbleGamepad /
  SDL_RumbleGamepadTriggers. Off leaves them pending as before (the run identical). On is opt-in because the game sees
  the answers (a result it ignores, and ~125 EE cycles per accepted request); physics, AI and gameplay read nothing of
  it. Not done: HD rumble beyond SDL's two motors.
  **Adaptive triggers and gyro steering built (pad1, 2026-11-13, docs/pc-settings.md §7.12; verify with real pads):**
  SETTINGS > CONTROLS > FEEDBACK: ADAPTIVE TRIGGERS Off / On (default Off; presentation only) and TRIGGER STRENGTH:
  a DualSense's brake trigger resists, the accelerator's is a light spring that buzzes while boosting, both kick at a
  crash, free in menus / paused / at exit (SDL_SendGamepadEffect with SDL's DS5 effect block; the effects from the
  game's state the window reads: a race on screen, the view's boost blur amount, its car's crashed flag). Gyro
  steering, per player (CONTROLS > BINDINGS with the controller device, and the launcher's Controllers page; saved in
  controls.ini): GYRO STEERING Off / Mix / Replace (**input-changing**, opt-in, default Off), tilt (a steering wheel,
  drift-free with the accelerometer) or turn, sensitivity, dead zone, a recentre button; while driving it is the
  report's left stick X, so replays keep it; Off: the reports byte for byte as before.
- **Accessibility:** colour-blind-safe HUD colours, text size, boost toggle vs hold, DJ subtitles.
  **Done (2026-11-10, docs/pc-settings.md §7.1):** SETTINGS > GAME > ACCESSIBILITY: SCREEN FLASHES Full / Reduced
  (full-screen white fills of the 2D capped, the TV static still, the Crashbreaker's full-screen flash capped at 25 % of white), HUD COLOUR FILTER Off / Deuteranopia / Protanopia /
  Tritanopia (a daltonize remap of the 2D layer only), BOOST Hold / Toggle (opt-in, input-changing). Defaults = the game
  byte for byte. **Text size done as HUD SIZE (textsize1, 2026-11-13, pc-settings.md §7.1 "HUD SIZE"):** 100 / 115 /
  130 / 150 %, the race HUD's components each scaled about their own EHUDAlign anchor (POS, LAP, SCORE, boost bar,
  speedometer, CRASHBREAKER, the race's messages, "Get Ready!", the EA TRAX popup), capped so that nothing leaves the
  screen or runs into another component (a message gives way to the steady HUD); 100 % = the game byte for byte. The
  menus (front end, pause menu, event intro, results) stay at 100 %: their text sits in fixed panels. Not done: DJ
  subtitles.
- **Split-screen layout choice** (vertical / horizontal) with correct aspect ratio.
  **Done (splitlayout1, 2026-11-13, docs/pc-settings.md §7.11, docs/native-renderer.md §29.7):** SETTINGS > DISPLAY > LAYOUT >
  SPLIT SCREEN: Top / bottom (default) / Side by side. The game's own layout is top / bottom, and it is not squashed
  [C SetViewportsSplitScreen 0x19E760: each viewport's aspect is its half of the TV picture, 8/3 at 4:3]; the port
  already draws each half Hor+ at its window shape, so Top / bottom is the game's layout byte for byte. Side by side
  (a branch the retail game has but never turns on) puts player 1 left and player 2 right, each projected at its own
  shape from the game's side-by-side view window (vertical 0.825 tan(fov / 2), Hor+), with each player's HUD
  anchored inside its own view (16:9 and 21:9 checked) and the rival markers projected per view. Drawn only: no game
  state, camera, pad report or timeline changes (golden equal with the default).
- **Speed units:** MPH or KM/H for the speedometer and the game's distances.
  **Done (units1, 2026-11-14, docs/pc-settings.md §7.16, tools/native/speed_units.h):** SETTINGS > GAME > LANGUAGE AND UNITS > SPEED
  UNITS: MPH (default, the US game's) / KM/H. The game already has both sets of words and factors, chosen by its
  language word (0x004E2918, always 1 on the US disc); with KM/H a port-layer hook on each of the 11 display functions
  that test it (the speedometer's Prepare and Update, the distance messages, the score list, the escape distance, item
  values, distance texts, records rows and page, stats, Driver Details) sets it to 2 for that call only, so the other
  things it chooses (separators, '$', online strings, the profile's saved language byte) never change. Text only: the
  m/s-to-mph factor the simulation uses never reads the word (golden equal with the default). The speedometer
  follows a change from the next race; the other sites at once.
- **Local achievements and stats** from the stats the game already tracks.
  **Done (stats1, 2026-11-10):** src/native/save_stats.h reads them, read only, from the game's own save (the
  BASLUS-21050 file in each profile-N.sav: version 0x10, three CB3Profile slots of 0x49C bytes at 0xE374, the 32-bit
  word-sum checksum at 0xF148 [C func_00421700, the game's load check]); the per-field layout with its decomp sources
  (the game's STATS page func_003F1490 and the CB3Profile getters) is in that header. Decoded: name, Burnout points,
  crash damage, takedowns and slams (offline / online), race and crash events played, longest oncoming / drift
  (metres), the medal of each of the 73 race and 100 crash events (3 gold .. 1 bronze), the 20 signature takedowns,
  the 10 crash headlines, race events per car. Not decoded, because the game works them out from its own tables (car
  list, World Tour layout, takedown targets): game complete %, events unlocked, regional / special-event medals, cars
  collected, takedown targets met, favourite car / track names. 13 local achievements from those values alone
  (`achievements()`: first gold, a medal / gold in all 173 events, all 73 race golds, all 100 crash golds, all
  signature takedowns, all headlines, 100 / 1,000 takedowns, 100,000,000 crash damage, 1,000 m oncoming, 250 m drift,
  25 cars raced). The launcher's **Stats** page shows every profile in use across the save files (chosen at the top),
  its totals and the achievements with progress; `save_tool stats [file]` prints the same. Tests: test_save_stats
  (synthetic saves built in the test, plus the kit's 100% profile when present: 13 / 13).

**Bigger, later**
- **Mod loader:** an overlay folder so mods never modify the original data; car and livery manager.
  *Part 1 done (modkit1, 2026-11-12; the whole mod kit's plan: docs/modding/DESIGN.md, user guide
  docs/modding/getting-started.md):* `<user data folder>/mods/<folder>/mod.ini` (id, name, version, author, description,
  api, priority; `[files] root`, default `data`), the load order and on / off in `<user data folder>/mods.ini` (the
  launcher's new **Mods** page: list, turn on / off, move up / down, open the folder, the manifest's errors and notes).
  The overlay sits in the platform's disc layer (platform_disc.cpp `resolve`, the one path of the GTFS drive and RW
  Audio's file reads): the first enabled mod with a path supplies it (size and bytes), names case-insensitive as the
  disc's; paths leaving a mod (`..`, absolute, links), case twins, the ELF and SYSTEM.CNF are refused. native_play
  `--mods <ids>` / `--mods-dir`; a window run takes mods.ini, an offscreen run none. The active set and its hash
  (FNV-1a 64 over each mod's id, version and files with their sizes and contents) are printed at the start (so run
  logs and bug reports carry it; bug reports also take mods.ini), pad recordings get a `mods` line and a replay with
  another set warns. No mods: no overlay, the game byte for byte (golden compare). Checks: tests/test_mods.cpp,
  tools/modding/text_mod_check.sh (a test mod built at run time from a copy of the user's GLOBALUS.BIN: "SINGLE EVENT"
  reads "MODDED EVENT" on the main menu, the race shots unchanged). Next (DESIGN.md §9): the hook table, the asset
  tools (b3modtool), the renderer's own disc readers through the overlay, the code-mod SDK, the tuning console.
- **Cut content** from the prototypes (Hidden Palace builds, TCRF research) as an opt-in extras menu.

## 6. Custom soundtrack in the EA Trax system, and Spotify (2026-11-01)

**Custom songs as first-class EA Trax entries.** The game's own music system is in the symbol tables [G names]:
`CB3SoundMusicManager`, `CB3SoundMusicSoundtrack`, `CB3SoundMusicSong`, the now-playing popup `CB3EATraxDisplay` /
`CB3EATrax`, the settings pages `CB3MyBurnoutSettingsEATraxPage` and `CB3MyBurnoutSettingsPlaylistEditPage` (the
in-game playlist editor), and the DJ `CB3SoundRadio`.
- User tracks (a music folder; MP3/FLAC/OGG/AAC via a host decoder) are added to the soundtrack list next to the
  EA Trax songs, so they show up in the game's own playlist editor and can be switched on/off there like the
  originals.
- The **EA Trax now-playing popup shows the custom song's title, artist and album** from its tags (fallback: the file
  name), drawn by the game's own display class, so it looks identical to the original notification. Optional: album
  art in the popup.
- Playback goes through the port's audio layer at the same mixer position as the streamed RWS music, so in-game music
  volume, ducking in menus and the radio DJ behave as before. Option: DJ lines on/off between custom songs.
- Playlist modes: EA Trax only, custom only, mixed; shuffle per the game's own setting.

**Spotify and other music apps.**
- **OS now-playing integration (all music apps)** (*built for Windows and Linux, 2026-11-13: §6.2*): read the current track from the platform's media session
  (Windows SMTC, macOS Now Playing, Linux MPRIS) and show it in the EA Trax popup when it
  changes; game music mutes while an external player is active; skip/previous via the game's own music controls.
  Works with Spotify, Apple Music, YouTube Music, local players, without any account linking.
- **Spotify account link (optional):** Spotify's Web API (OAuth with PKCE) lets the game control the user's own
  Spotify playback (play a chosen playlist, skip, pause) and read the current track for the popup. Audio is played by
  the Spotify app itself, not routed into the game [L: Spotify does not offer raw audio streams to third-party
  apps; playback control needs Premium]. Check Spotify's developer terms and API availability (app quota modes,
  endpoint changes) when this is built; the OS integration above needs no approval and is the default.

**Reference rip (a player's own, 2026-11-02; outside the repo, never copied in):** 46 MP3s: `00 Intro` (a 57 s cut of the F-Ups song), `01`..`44` the EA Trax songs, `99 Menu Music` (names fixed by hand).
All are 320 kbps at **32 kHz**, so they are rips of the game's own music streams (`tracks\_EATrax1.rws` etc.), not
CD masters: no better than what the game already plays, and a second lossy encode. Uses: (1) the test set for the
custom-folder feature (tags, popup text, playlist editor) without needing other music; (2) a listening reference for
the port's music decoding (our RWS stream output should sound the same); (3) the numbering likely follows the game's
track order [G], to check against `CB3SoundMusicSoundtrack`.
A higher-quality "HD soundtrack" option would need 44.1 kHz sources (the player's own CDs/purchases) mapped per song.

Order: custom folder + popup first (needs the music manager running natively or overridden, plus a host decoder);
OS now-playing next; Spotify link last.

### 6.1 Custom folder and popup as built (v1, 2026-11-10)

**Settings.** SETTINGS > AUDIO > MUSIC (a section of the INTERFACE tab, native_play only: `Env::music`; not a 7th tab, whose label would not fit): MUSIC FOLDER (the system's folder dialog,
`$B3_MUSIC_DIALOG=<path>` answers it in scripts), REMOVE MUSIC FOLDER, PLAYLIST (EA Trax only / Custom only / Mixed;
ini `playlist_mode` 0-2, default 0; RADIO VOLUME and the DJ stay in Driver Details). Ini key `music_folder`
(a path line, as `hd_pack_dir`). Both apply from the next song. The renderer publishes them to the game's thread
(`b3r_music`, PcPage::music); `B3NATIVE_MUSIC_DIR` / `B3NATIVE_MUSIC_MODE` override them (tests),
`B3NATIVE_MUSIC_OFF=1` does not install the wrappers at all, `B3NATIVE_MUSIC_LOG=1` logs the game's own popup texts and
the stream's releases.

**Host side** (`src/native/custom_music.{h,cpp}`, `music_decoders.cpp`): the folder scanned recursively (sorted by
path, the same list every run); tags from ID3v2.2/2.3/2.4 (TIT2 / TPE1 / TALB, all four text encodings,
unsynchronisation), ID3v1, FLAC and Ogg Vorbis comments (TITLE / ARTIST or ALBUMARTIST / ALBUM); fallback from the file
name ("07 Artist - Title"), album: the folder's name. Decoders vendored in `src/native/third_party` (dr_mp3, dr_flac,
dr_wav: public domain or MIT-0; stb_vorbis: MIT or public domain; README and license files there). AAC is not decoded.
Catmull-Rom resampling from the file's rate (times the music category's pitch) to the mixer's 48 kHz. Unit test:
`tests/test_custom_music.cpp` (synthetic tag bytes, a WAV tone written by the test).

**Game side** (`tools/native/custom_soundtrack.h`, wrappers on the native table, chained like date_format.h). The music
manager is the radio object D_01E7A888 [C decomp boot2w]: func_003FBC20 starts a song (the stream file
`tracks\_EATrax1.rws` / 2, 22 songs each, the song a segment), func_003FC8C0 runs the stream (prepare func_003865A0,
Play, SetVolume = RADIO VOLUME x duck, the end via the stream's status), func_003FC700 the popup (func_00438F10(display
D_01ECDE90, title, artist, album), hidden 5 s of stream time later). Wrapped:

| Address | Function | With a custom song |
|---|---|---|
| 0x003FBC20 | start a song (manager, song) | after the game's own start: a custom song is chosen (Custom: every song; Mixed: every other); the game's play mode at +0xCC: in order, shuffle (a fixed-seed deck), repeat. Not on the EA TRAX page (radio mode +0x178 = 3): its previews stay the game's |
| 0x003865A0 | CGtSoundStream prepare | the manager's stream (+0xC0) attaches no media: the song is opened (at the resume sample), status 2, returns 1 |
| 0x00386290 / 0x00385F20 / 0x00386240 / 0x003863C0 | Play / Pause / Stop / Release | the same status fields as the game's code, no RW Audio calls; the player plays, pauses, stops, closes |
| 0x00386410 | CGtSoundStream::Update | the song's end -> status 4 (the game goes on to its next song); the player's gain = the stream's volume x the music category's gain (D_01EA4B00[13]: fades, ducking), its pitch the category's |
| 0x00386080 / 0x00386020 | position (ms / samples), block size | the player's position; block 1 (the suspend / resume point, func_003FC2E0 / func_003FC260) |
| 0x00438F10 | the EA TRAX display's show | the custom song's title / artist / album, UTF-16 in guest RAM 0xC0C00..0xC0F00 (checked zero before the first write; Latin-1 kept, other characters '?', 48 characters at most) |

The player is mixed into the SPU2 mixer's output each VBlank (`platform::set_music_overlay`, platform_rwa.cpp), on
virtual time, so a song's length and position are the same in every run; the game's own EA Trax songs are unchanged in
Mixed mode. With PLAYLIST at EA Trax only (the default) every wrapper passes the call through untouched (guest RAM and
the run identical: the golden compare). With a custom song the game's own state follows it (the song's length, the
stream's status), and with no RWS stream reads the menus' frame pacing differs a little, so per-frame state such as
the menus' random generator D_00518680 (a "Random" track) can differ from an EA Trax run at the same VBlank; physics,
AI and gameplay code are not touched. Level check: the rip's "Give It Up" as a custom song and the game's own stream of
it give the same output RMS (2959 vs 2921 over 900 VBlanks; B3NATIVE_MUSIC_RMS=<first>-<last>). Re-measured for 1.2.1
(musicvol1: the same 30 s in the menus, the 100 % card): -18.98 vs -19.09 dBFS, and the rip through the player comes out
at exactly the stream's gain (0.4495 there): RW Audio and the SPU2 add nothing to the game's own music stream's gain,
so the player's level constant (custom_soundtrack.h kStreamLevel) is 1. CUSTOM MUSIC VOLUME (0-200 %, default 100)
multiplies the custom songs only, and a look-ahead peak limiter (custom_music.h Limiter: 64 frames, ceiling -1 dBFS,
~0.1 s release) keeps a loud song at a high gain from clipping (below the ceiling it is a 1.3 ms delay, nothing else).
B3NATIVE_MUSIC_LOG=1 logs the music stream's gain (volume x category x custom volume) on each change. ReplayGain tags
are not read: their reference (about -18 LUFS) sits some 10 dB under the disc's songs (about -8 LUFS), so using them
would need a target and a make-up gain of its own. Instead (musicloud, CUSTOM MUSIC LOUDNESS, default Even out) each
song is measured by the port: EBU R128 integrated loudness and sample peak (custom_music.h LoudnessMeter, checked
against EBU Tech 3341's cases), on a worker thread from the folder scan, kept in music_loudness.txt (path, size, time),
and its gain brings it to -10 LUFS: the disc's songs at their source level, measured in the menus as the output over
the music stream's gain (0.450): "Give It Up" -8.8, the front end's own piece -11.3. At most 20 dB down and 12 dB up,
and up only as far as the song's peaks go 3 dB over the player's limiter, so a dynamic song is not squashed to the
target. A song the worker has not reached when it starts is measured then (in the game's prepare call); the gain only
depends on the file, so a run plays every song at the same level whatever the timing. Off: each file's own level.

**Not in v1:** custom songs as rows of the in-game playlist editor (built in 1.1: §6.3); the DJ's lines between custom
songs (the DJ talks as for the game's songs; no on/off option yet); album art; AAC.

### 6.3 Custom songs in the game's EA TRAX list (1.1, 2026-11-18, GitHub issue #5)

With PLAYLIST at Custom only or Mixed and a folder with songs, the songs of the folder are rows 45 on of the game's
own EA TRAX page (DRIVER DETAILS > SETTINGS > EA GAMES TRAX), after the disc's 44, at most 200 (the first by path;
the launcher's Music page says so above that). Each row is set like the game's (left / right: the game's per-song
modes ALL / RACE ONLY / MENU ONLY / OFF), previews its song when selected as the game's rows do, and the game's own playlist logic (in order, shuffle,
repeat; the race / menu contexts; the L2 skip) then picks them. EA Trax only (the default) lists the game's 44 and
changes nothing (the golden compare).

**PLAYLIST.** Mixed: every song, the game's and yours, by its own mode. Custom only: the same list, but the game's songs
are never picked (their modes are kept as saved); with every custom song off for a context the radio is silent there,
as the game is with every song off. The v1 rule (Custom: every song replaced; Mixed: every other one) is gone.

**How the game builds the list** [C gen/native, boot2w snapshot]: the 44 songs are D_004A5600 (0x18 bytes: +0 index,
+8 / +0xC / +0x10 the title / album / artist GLOBALUS.BIN string ids, +0x14 the per-song bits: 7 both, 1 races, 4
menus, 0 off, 8 "not heard yet", which cannot be set to off), then the soundtrack descriptor D_004A5A20 (+4 count,
+0x4C songs). Every user takes a copy of the descriptor from func_003FBB50(radio, 0, out); func_003FC060 gives it to
the radio D_01E7A888 (+0x6C count, +0xB4 songs). The page (CB3MyBurnoutSettingsEATraxPage: prepare func_00431D80, rows
func_00431860 / func_00431160 -- three rows of artist / title / album lines, joined by "\n" in a 128-unit buffer,
centred on the selection; update func_004313E0; left / right func_00431050 -> func_003FBAA0) reads the radio's count,
so it scrolls through any length. The profile's copy is the options block D_004F5040 [0..43]; its bytes from 44 on are
other options (func_0021B770's defaults: 0x0F for each song, then 1, 1, 1, 1).

**What overflows past 44, and the wrappers** (tools/native/custom_soundtrack.h, port layer, all pass-through while the
list is down):

| Address | Function | With the list up |
|---|---|---|
| 0x003FBB50 | the soundtrack descriptor (copy) | its songs = the port's table (guest RAM 0xC1400, `port:eatrax_songs`: the 44 copied, then the custom entries), its count left at 44, so the page's confirm / back loops, func_0028B380 at boot and CB3SoundManager's loop stay within the profile's 44 bytes |
| 0x003FC060 | the radio takes a soundtrack | the radio's count = 44 + the custom songs |
| 0x003FCC10 | the profile's bits -> D_004A5600 | also into the table (it is the live copy while the list is up; the bits go back to D_004A5600 when it comes down) |
| 0x003FBDB0 / 0x003FB8B0 | the next song / the order from the page's song | the game's steps (in order, shuffle, repeat; the context's bit) with the same sound generator CGtSoundUtils::mRandom (0x004E2E78), the shuffle order kept host-side: the game's is a byte array at radio +0xD..+0x3F (51 songs) |
| 0x003FB7C0 | may the current song play here | Custom only: no for the game's songs |
| 0x003FC8C0 | the music stream's poll | D_004F5040[song] (bit 8 cleared at a song's end) put back for a custom song: that byte is another option |
| 0x004313E0 | the EA TRAX page's update | confirm (asked as the page does, before it) keeps the custom rows' modes; back puts them back |
| 0x00431160 | the page's rows | the row numbers: the page prints them into unsigned short [3][3] (two digits; the game never had row 100); from 100 on the overflowing terminator's two bytes after the array are put back and each row's number text object (+0x20 + 0x50 i, string at +0x48) draws from 0xC3380 (the text region's last slot) |
| 0x00271A00 | the string lookup | the custom ids 0xF000 + 4k (+0 title, +1 album, +2 artist) answered with the song's tags, UTF-16 in a 15-slot ring at 0xC2C00 (`port:eatrax_text`; Latin-1 kept, others '?', 40 units a line) |
| 0x003FBC20 | a song start | a custom row's index plays that song (the page's previews too); the stream wrappers of §6.1 play it |
| 0x00421AF0 | the card's save image (options, profiles, checksum) | the options' last song D_004F5077 (header +0x37), written by func_003FA600 and its siblings as the radio picks a song, holding a custom index becomes the last disc song started (else 0) before the image and its checksum are made |

**Where the modes are kept.** The save holds the 44 songs' bits only (unchanged: the save card format and layout are
the same). A custom song's mode lives host-side, keyed by its folder and its path in that folder, in
`music_modes.ini` beside pc_settings.ini (`folder=<path>` then `both|races|menus|off=<path in the folder>` lines;
B3NATIVE_MUSIC_MODES_FILE overrides it, `-` keeps them for the run only); a song not there is "both", and the "not
heard yet" bit does not apply to custom songs. Written on the page's confirm only. Entries of files that are not in
the folder now (moved, renamed, a folder not connected) are kept in the file and ignored.

**The save stays the PS2's.** No custom index (44 on) reaches what the profile saves: the 44 songs' bits are written for
the disc's songs only (the descriptor's count stays 44; D_004F5040[song] is put back for a custom song), and the last
song byte is mapped back to a disc song when the save image is made (above), so a save exported to a PS2 or an emulator
reads as one made there. `tools/native/save_check.sh` run 3 saves a new profile while a custom song plays and checks
the save's song fields (`save_tool music <file>`: the last song and the 44 bits).

**Which files are listed.** The folder's audio files by path, the first 200; each is opened by its decoder when the folder
is scanned and one that does not open is left out (the log names the first five). A folder with no file that opens
(or none at all, or not there) is as EA Trax only.

**Replays and determinism.** Everything stays on the game's thread and on virtual time. A recording keeps each
folder's modes as the option `music_modes.<folder>` (a digit per song in scan order) next to `music_fp.<folder>`; a
replay played back uses them and never writes the file. Replays recorded with a music folder before 1.1 followed the
v1 rule and do not play the same now.

**Checks.** `SCENARIO=eatrax tools/native/race_check.sh` (the page with four synthetic songs: row 45 previewed, two
modes changed and saved, Back's restore; `EATRAX_FILL=<n>` adds n short songs: the 200 cap); `SCENARIO=music`
(Custom only: the custom songs play in a race).

### 6.2 Other music apps: the system's now-playing song in the popup (v1, 2026-11-13)

**Setting.** SETTINGS > AUDIO > MUSIC > OTHER MUSIC APPS: Off / Show now playing (ini `other_music_apps` 0-1,
default 0, row id 91; also on the launcher's Music page with a "Detected" line). On, the song another app plays shows
in the game's own EA TRAX popup (title, artist, album; no album: the player's name) when it starts playing. While that
app plays, the game's own music is silent (whatever the PLAYLIST setting) and the game's own song popups are not shown;
about 3 s after it stops (short gaps between songs do not count) the game's music is back as the PLAYLIST setting has
it. The sound itself stays in that app. Off, no reader exists and
no system call is made (none is ever made while the option stays off).

| System | Reader | Status |
|---|---|---|
| Windows 10 1809+ / 11 | Global System Media Transport Controls (`Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager`: the sessions the volume flyout's media controls show) through the WinRT ABI, combase.dll loaded at run time; the interfaces declared in `src/native/now_playing_win.cpp` with their IIDs from the SDK's metadata | built; on a Windows desktop the reader starts and reads the sessions (test_now_playing.exe: 15 tests pass, `--read`: "nothing playing"); a playing song's properties still to be checked |
| Linux | MPRIS 2 on the session D-Bus (`org.mpris.MediaPlayer2.*`: PlaybackStatus, Metadata `xesam:title` / `xesam:artist` / `xesam:album`, Identity), libdbus-1 loaded at run time (`src/native/dbus_dl.h`): no library or no bus, and the game starts as before | works (end to end against fake players on a private bus) |
| macOS | none: the only way to read another app's now-playing data is the private MediaRemote framework, which macOS 15.4 and later refuse to apps without Apple's entitlement | unsupported (the row is disabled there) |

Which player: Windows, the system's current session when it plays, else the first that plays, else the current one
paused; Linux, the first player (by bus name) that plays, else the first paused one. Spotify, browsers (YouTube and
other sites through the browser's media session), the Windows Media Player, VLC and most Linux players publish one.

**Host side** (`src/native/now_playing.{h,cpp}`, `now_playing_win.cpp`, `now_playing_mpris.cpp`): one `Provider`
interface per system, a `Poller` that reads it on its own thread once a second (a D-Bus round trip waits at most
500 ms, a WinRT async call 2 s; the game's thread only copies the last result under a mutex, so a slow player never
stalls a frame), a failed read keeps the last song for three reads (no second popup of the same song), and
`PopupDecider` (pure): a song is shown once when it plays; a pause and resume of it shows nothing; nothing playing
forgets it; while the display is busy (the game's own popup up or fading) the popup waits. A file reader
(`B3NATIVE_NOW_PLAYING_FILE=<file>`, key=value lines) stands in for the system's in tests and scripts.

**Game side** (`tools/native/now_playing_popup.h`): func_00438F30, the EA TRAX display's render (D_01ECDE90, once
per frame in every state from func_00132600) wrapped on the port layer. On a new song the title / artist / album go as
UTF-16 into guest RAM 0xC0F00..0xC1200 (the mod arena's port region `port:now_playing`, checked zero before the first
write; Latin-1 kept, other characters '?', 48 characters at most as the custom songs') and func_00438F10's own row is
called with them: the game draws its popup, its 5 s and its fades. The display is free when its +0x40 is zero and its
text object's (+0x34) +0x28 is zero. With nothing latched the wrapper only calls the render (guest RAM untouched).

**Determinism.** Presentation only: nothing the simulation reads changes, and the pad report is untouched. The
poller's result reaches the game only at a VBlank (native_play latches it each VBlank); a replay records each change
of it as a `now_playing` setting op, and playing a replay shows the recorded songs on the same frames (the system is
not read). A session that never had a song records nothing. `B3NATIVE_NOW_PLAYING=0/1` overrides the setting,
`B3NATIVE_NOW_PLAYING_OFF=1` does not install the wrapper.

**Checks.** `tests/test_now_playing.cpp` (16 tests): the replay form, player names from Windows ids, the player
choice, the popup on track changes / pause and resume / nothing playing / a busy display, unicode and long titles, the
poller with a fake provider (off: no provider made and nothing read; failures; released when switched off; no reader),
the file reader, and on Linux the MPRIS reader end to end: a private `dbus-daemon` with two fake players made by the
test (a paused one, a playing one with two artists and a unicode title; then the playing one quits), through the reader
and the poller (skipped where there is no dbus-daemon). `SCENARIO=nowplaying tools/native/race_check.sh`: a file
reader, the first song latched at VBlank 16900 (the main menu: shot 8600 shows the game's popup with Some Band / First
Song / Some Album), the second (unicode, no album) once the first was shown, waiting for the display to be free. A replay
recorded with it plays back with all 29 checkpoints matched, the same popups on the same VBlanks and the same shot. In a
race the panel and its text draw as in the menus (the text fades in once the panel has slid in); the game's radio keeps
playing there, so a game song that starts replaces another app's popup, as it replaces its own. The Windows reader was first built with its ABI interfaces in an
anonymous namespace: GCC then saw no class that could implement them and replaced every call through them with a
pure-virtual call (no Sleep or WideCharToMultiByte left in the object); they now have external linkage (`winabi`).

**Not in v1:** muting the game's music while another app plays (the RADIO VOLUME setting does it today), skip /
previous through the game's music controls, album art, the Spotify account link.

## 7. Open regions, custom routes, very long tracks (2026-11-03)

**Parked (2026-11-03): first wire the native renderer in.** Pick up from here later.

Goal: open each region up as one world, so custom routes and very long tracks can be made on it.
- Facts so far [C measurement, docs/native-renderer.md "Region mode"]: each region's routes share one coordinate
  space; the M/P routes reuse 76-92 % of the circuits' triangles; V1/V2 are the same roads. b3view loads a whole region
  (EU: 1.3-1.5M triangles at ~160 fps on an M1 at 3200x1800).
- Open in the world: places two routes model differently (a road one route closes off as pavement, its barriers);
  probably a curated "which route's version" table per place.
- Road blockers: routes close unused roads with fake building facades and barriers inside their unit meshes (EU city
  at (2200, -330): the same facade in M1 and P1). Next step: a pass that flags walls whose footprint crosses a road some
  route drives on, reviewed, then removed in the open-world build.
- Needed for driving: the collision data per route (not decoded yet) merged the same way; a route's own data (unit
  order, AI racing lines, traffic paths, checkpoints, crash junctions, side-road barriers) built on the merged world,
  mostly derivable from the roads, with a route editor on top of b3view.
- Game limits (17 streamed unit slots, tables sized for the shipped routes) lift once that code runs natively; the PC
  keeps a region resident.
- Physics and handling unchanged: custom routes change where you drive, not how.

## 8. Saves, profiles, input in the game's menus, HD pack setup, launcher (2026-11-09)

Decided (the port goes for full remaster-style features):
- **No memory card**: the card is already our own (src/native/save_card: a folder served as a card); hide it -- no card
  checks or messages, silent autosave, no card wording in the menus.
- **Our own save file**: one file per profile (`.sav`) in the user's save folder, copied as-is to another computer
  running the port; Export / Import in SETTINGS.
- **PS2 / PCSX2 save import** (§5): `.psu`, PCSX2 `.ps2` / folder cards, into a profile.
- **More than 3 profiles** (the game's profile list has three slots: needs its menu code extended).
- **Keyboard typing on the profile-name screen** (the game's on-screen keyboard driven by typed characters). **Done
  (mac5, 2026-11-09)**, tools/native/menu_mouse.h: the page CB3MyBurnoutProfileCreateNewPage (Prepare 0x3F55E0) shows
  the keyboard component D_01E899C0 and registers its PS2 USB keyboard callbacks at the keyboard device D_004F5518
  (+0x8 KeyboardCharacterCallback 0x34E2A0, +0xC the component; RegisterKeyboardCallbacks 0x34D8D0); Show (0x350F80)
  clears D_01E7AA38 while it is up. native_play hands each typed character to that callback, one per keyboard Update
  (0x351410, wrapped on the native table), i.e. the game's own USB-keyboard path: Enter presses its Done key, Backspace
  its Backspace key, space its Space key, any other character the key that shows it (PressKey, with its highlight
  and sound; the PS2 puts the highlight out on the key's release, KeyboardEventCallback 0x34E3D0 calling
  UnHighlightKeys 0x34D3A0, and the window gives characters, not releases, so native_play calls UnHighlightKeys
  6 VBlanks after each typed character or before the next one: a brief flash) or AddCharToString, both after the game's check against the page's allowable characters (string 659;
  read once through func_00271A00 so a typed "a" goes in as "A" on a capitals-only page, text_char_for). The name's
  buffer, length, cursor and limit stay the game's. The window: while the keyboard is up (b3r_text_mode) SDL's text
  input is on and the keys that type (letters, digits, space, punctuation, Shift, Backspace, Enter) no longer press
  pad buttons; the arrows, Escape and Tab still do.
- **Mouse in the game's own menus** (hover highlights, click selects, wheel scrolls), beyond our SETTINGS page.
  **Done (mac5, 2026-11-09)**, src/b3replay/menumouse.h + tools/native/menu_mouse.h + include/b3live/menu_input.h.
  Where the rows are: native_play wraps CB3VSelectComponent::Update (0x31FF80), the vertical list every front-end page
  and the pause menu use (+0xC its selection data: u16 current row, u16 rows; +0x14 the 9 row text objects; +0xC4
  < 0: no lead-in / lead-out), and reads the front end's dialog box D_01E8F360 (shown while +0xA is set; +0xC / +0xE
  current / count, +0x38 / +0x3C the two choices: LOAD / NEW PROFILE, YES / NO), and publishes them per poll as a host
  record ('HMN1'). The renderer finds each row's text draws by object address (the capture's object address is 8 bytes
  before the component's pointer) in the frame as drawn -- View::hud_list keeps every draw's quads in window fractions
  after the wide layout -- and makes a hit band per row across the menu column. Prompts: the "Buttons" pieces of the
  prompt bar (the symbol names the button) plus the label word beside it, either side ("Select (X)", "(T) Back").
  What the mouse does: moving onto a row selects it, a left click selects it and presses Cross, a click on a prompt
  presses its button, a right click presses Triangle, the wheel presses Up / Down; a left click on a menu page without
  a list presses Cross (not on the name keyboard). It moves the game's own selection with pad presses in the report
  the game reads (MenuPresser: one Up / Down at a time, each checked against the game's selection, the actions in
  order), so the game's logic, sounds and animations run; never while the race is driven (the key map's driving
  context), so a race only takes it in the pause menu. Cursor: shown on a move, hidden after 3 s or on a key / pad
  button; a click or the wheel counts as the keyboard for the prompts. The SETTINGS page takes the pointer through
  b3settings' own pointer rules (rows, tabs, sliders, wheel; right click = Back). Not covered: lists that are not a
  CB3VSelectComponent or the dialog box (the profile slot list, car / track carousels, the World Tour map: there the
  wheel, prompts and click-to-continue work, hover does not), the on-screen keyboard's keys by mouse.
- **HD texture pack**: works today (`--hd-pack` / `$B3_HD_PACK`, a PCSX2 replacement folder); add choosing the folder
  (SETTINGS and the launcher) and remembering it.
  **Done (mac5, 2026-11-09):** SETTINGS > QUALITY > HD TEXTURE PACK opens the system's folder dialog (SDL3; scripted:
  `$B3_HD_PACK_DIALOG`), REMOVE HD PACK clears it; the folder is kept as `hd_pack_dir` in pc_settings.ini and used from
  the next start (`--hd-pack` still wins; the setting beats `$B3_HD_PACK`). Checked: a settings file with an HD
  pack loads it at start (7859 replacements indexed).
- **Launcher / setup program** beside the game: display, graphics, HD pack folder, save folder, controller test, reset
  settings -- usable without starting the game.
  **Done (mac5, 2026-11-09):** `b3launcher` (`make launcher`; Windows: `Burnout3Launcher.exe` from
  tools/render/build_win.sh, beside native_play.exe with `tools/native/build_win.sh RENDER=1`), src/b3launcher. One SDL3
  window (SDL_Renderer, dark theme, the system's TrueType font through stb_truetype; SDL's debug font if none), mouse,
  keyboard and gamepad, 1280x720 and up (Steam Deck 1280x800). Pages: **Play** (START GAME, START IN SAFE MODE =
  `native_play --game <folder> [--safe-mode] --log <logs>/run-<date-time>.log` beside the launcher (test builds: the
  game writes its run log and pad recording itself, below); the launcher closes once the game runs, or shows the run
  log's end when it ended at once; status: game data, game program, save file in use,
  the last run from run_state.ini with "clear the crash count", the settings in short); **Settings** (the in-game page's
  own rows, labels and descriptions: b3settings::build_content / apply_edit on the same pc_settings.ini, saved at each
  change; reset to defaults with a confirmation); **HD Textures** (choose with SDL's folder dialog or a typed path /
  open / remove; the .dds / .png check of the in-game page; the HD TEXTURES switch); **Music** (§6.1's custom
  soundtrack, the same music_folder / playlist_mode as SETTINGS > AUDIO > MUSIC: choose / type / open / remove the
  folder, the song count and the first songs' title / artist / album from the game's own scan and tags
  (custom_music.h list_audio_files / read_tags), the in-game PLAYLIST row as it is; §6.2's OTHER MUSIC APPS row with a
  "Detected" line, read only while the page shows and the option is on); **Game Data** (the disc folder,
  remembered in launcher.ini beside pc_settings.ini; a checklist of SLUS_210.50, SYSTEM.CNF, DATA, TRACKS, PVEH, SOUND,
  OVID, case-insensitive; the folder above the disc is taken as its game/ folder; searched beside the launcher, in
  data/game and the start folder); **Saves** (the saves folder and its five files, the file used at start =
  active-slot.txt, export / import through save_store.h: .sav, .psu, .max, .ps2 cards, folder cards); **Stats** (each
  profile's own stats and local achievements, read only: §5 "Local achievements and stats"); **Controllers**
  (connected pads with name and type, the selected one live, a button test that does not navigate, rumble when SDL can,
  the DS2 buttons the game gets from every pad and the keyboard, and the keyboard table of input_keys.h's map). A typed
  path stands in when the system has no file dialog. **A controller alone (osk1, 2026-11-14):** a pad (or a touch on
  the screen) on a folder / file button opens the launcher's own folder list instead of the system's dialog (also in
  Steam's Gaming Mode / Big Picture for every input, and when the system has no dialog): places on the left (the
  current choice, home, the user folders, the launcher's folder, the drives, `/`, media under /run/media, /media,
  /mnt, /Volumes), A opens, B up, Start chooses, Y places, X type a path, Select cancels; a text field opened with a
  pad gets the launcher's on-screen keyboard (core.h osk_layout: lower / upper / symbols with accented letters,
  Shift for one character, caps lock, the cursor; A types, B deletes, X space, Y layers, LB / RB cursor, Start done),
  plus SDL's system keyboard where SDL_HasScreenKeyboardSupport (Steam's on a Steam Deck). The text field has a
  cursor for every input now. Players always start through the launcher; launchskip1's
  **Start the game directly next time (skip this launcher)** on the Play page (= SETTINGS > GAME > STARTING AND LOADING > OPEN THE
  LAUNCHER AT START off, `launcher_at_start` in pc_settings.ini) makes it start the game at once, unless Shift / a
  pad's Back is held, `--launcher` is given, the game data or program is missing or the last run ended badly
  (docs/pc-settings.md §7.15). `b3launcher --screenshot DIR [--pref DIR]` draws every page (the check; osk1: an `osk`
  and a `browse` check type a path and pick a folder and a disc image with a virtual pad alone). Tests: test_launcher (ini, game folder check / search, command line and Windows quoting, HD check, save
  slots, last-run words, row steps, the music settings' round trip and folder scan, keyboard table, spawn with a log).
  **The game's look (mac5: the launcher styled like the game instead of a generic look):**
  once the game data folder is found, the launcher reads the front end's own art from it at run time
  (src/b3launcher/gameart.h; nothing of the game's is in git or in the binary) and draws like the menus: the game's
  three fonts from SLUS_210.50 (GlobalFont / HeadFont / SmallFont are linked into the program, not in a TXD: the
  CGt2dFont definitions and their one-texture dictionaries behind the sdata pointers 0x4E1584..0x4E1598
  [C CB3GraphicsManagerBase::Construct]; laid out as RenderString, italic and outlined as the menus' text), GLOBAL.TXD
  "B3Logo" and "grid", FRONTEND.TXD "titlescreen_logo" and "Halo" (the blue arc). The frame: the navy grid backdrop
  with the arc, the heading band with the logo and the page title in HeadFont's italic capitals (as "SELECT OPTION"),
  the pages as the menus' list (light-blue italic, the selected one white in |- -| brackets), setting rows highlighted
  the same way, the help bar and the prompt bar with src/b3replay/prompts.h's button glyphs for the device in use (the
  keyboard: ENTER / ESC / TAB / PGUP / PGDN caps). Without game data (or `B3LAUNCHER_PLAIN=1`) the plain dark look stays.
- **Safe mode**: after two unclean exits in a row the game starts with low settings and says so.
  **Done (mac5, 2026-11-09):** include/b3settings/safemode.h. `run_state.ini` beside pc_settings.ini marks a window run
  as running until its clean end; two runs in a row that never got there (crash, killed hang, driver reset) start the
  next one with safe settings (the Low preset, no MSAA, bilinear, no HD pack, a window, vsync, auto latency) for that run
  only (the player's file is kept unless they change something on the page); a banner and the window title say so.
  `native_play --safe-mode` / `B3_SAFE_MODE=1` asks for it (the launcher's "start in safe mode"). Tests: test_settings.
  Safe mode asked for (`--safe-mode`, `B3_SAFE_MODE=1`) also allocates guest RAM without its guard pages (hostfix);
  `B3_GUEST_GUARDS=0` does that alone, the workaround for a "guest memory overrun" crash. The automatic safe mode keeps
  the guards on unless the previous run's log shows such an overrun (its crash line, or its own guards-off line for
  that reason): b3settings::safe_mode_guards_off. Tests: test_native_backend guards_off_switch, test_settings
  safe_mode_guard_policy.

### 8.1 Saves as built (save1, 2026-11-09)

- **The card** is the active save file (src/native/save_store.h): `profile-N.sav` in the saves folder (native_play's
  default in a window: `<user data>/saves`, beside pc_settings.ini -- %APPDATA%\b3port\native\saves,
  ~/Library/Application Support/b3port/native/saves, ~/.local/share/b3port/native/saves; `--saves DIR` /
  B3NATIVE_SAVES_ROOT replace it; `active-slot.txt` picks N, 1..5). It is presented to the game as a freshly formatted
  8 MB card holding that file's directories (SaveCard: the same image as before), present from VBlank 0. When the game
  has written to the card and the card has been quiet for 60 VBlanks, the image is read back through its own
  filesystem (save_formats.h read_card_image: superblock, indirect FAT, FAT chains, directory entries) and the game's
  directories are written to profile-N.sav (write to .tmp, rename). The older folder layout (`profile-N/BASLUS-21050/...`)
  is read when profile-N.sav is absent; the first save then writes the .sav beside it (the folder is left alone).
  native_play sets B3NATIVE_CARD_PERSIST=1; without it (native_boot / native_check proofs on a fixed save folder) the
  writes stay in the image.
- **The .sav** (save_formats.h): "B3PSAVE1", u32 version 1, u32 payload size, payload (u32 directory count; per
  directory: name, mode, created / modified as the card's 8-byte times, u32 file count; per file: name, mode, times,
  u32 size, bytes), u32 CRC-32 of everything before. It holds the card's BASLUS-21050 directory as the game wrote it
  (icon.sys, view.ico, the 61,772-byte BASLUS-21050), so a copy on another computer loads to the same game state.
- **No card messages** (tools/native/card_silence.h, two wrappers on the native table, B3NATIVE_CARD_MESSAGES=1
  restores them): every card page shows its messages through the message component D_01E8F360 (func_00449F10 prepares,
  func_00449150 answers). Progress boxes with card texts (GLOBALUS.BIN 120, 168, 169, 171, 175, 2260-2262: "Checking /
  Loading / Saving / Formatting memory card (PS2) ... Do not remove") are not prepared (the card controller's completion
  moves the page on, not the box); "Load successful" (205), "Save successful" (2256), "Autosave successful" (2263) are
  not shown and answered OK; "Would you like to save your profile?" (2257, after a new profile's name) is answered YES.
  Kept as the game has them: the profile questions (LOAD PROFILE / NEW PROFILE, the PROFILE 1-3 list, overwrite,
  autosave on / off) and the card errors, which the port's card cannot produce (always present, formatted, 8 MB).
  The menus themselves have no card wording (GLOBALUS.BIN's card strings are all messages).
- **Save screens** (autosave1, tools/native/save_screens.h; SETTINGS > SAVES > SAVE SCREENS: Hide (default) / Show, ini
  `hide_save_screens`, `--save-screens hide|show`, B3NATIVE_SAVE_SCREENS=show): after a race and after the crash
  results the game enters its AUTOSAVE page (the card flow's page object D_01E97DAC, func_00420A50), whose title slides
  in, whose card controller checks the card, saves and confirms (the messages above, hidden) and which leads out to the
  next page: ~200 VBlanks in all [C measured, race_check crashresults; the log's "AUTOSAVE page up / gone" lines].
  With Hide the page is not shown: its poll records carry a hold flag and the renderer shows the picture of the page
  before it (src/b3replay/livefeed.cpp), and a paced run goes through its VBlanks as through a fast load (no wall-clock
  wait, its sound muted; FAST LOADING need not be on). Presentation only: the same VBlanks, the same guest memory at each of
  them, the same save bytes (tools/native/save_check.sh). Show is the PS2's page, and the golden recording's. DRIVER
  DETAILS > PROFILE > SAVE PROFILE / LOAD PROFILE are the player's own actions and stay as they are.
- **Import / export**: SETTINGS > SAVES (src/b3settings, src/b3replay/pcpage.cpp): SAVE FILE IN USE, SAVE FILE AT
  START (writes active-slot.txt), SAVE FOLDER (opens it), EXPORT SAVE (a .sav of the game's save), IMPORT SAVE (any of:
  .sav, EMS / uLaunchELF .psu, Action Replay MAX .max (LZARI), a PCSX2 .ps2 card image with or without its ECC
  bytes, a PCSX2 folder memory card -- pick its _pcsx2_superblock -- or one save folder; the BASLUS-21050 save in it
  replaces the active file, the old one kept as .bak; it applies at the next start, and the running game's later saves
  are no longer written, so they cannot overwrite it). The system file dialog (SDL3); without one (Linux without a
  portal) exports go to saves/exports and imports come from saves/import. `save_tool info | import | export | where`
  does the same from a shell. Checks: tools/native/save_check.sh, tests/test_save_formats.cpp.
- **Profiles**: the game keeps three profiles in its one save file (BASLUS-21050, 61,772 bytes, the size fixed in
  Construct__20CB3MemCardController 0x3FDF90 +0x44 / +0x54): the block D_01EBE3B0 holds three 1,180-byte (0x49C)
  profile records at +0xE390 (func_00420FE0: + i * 1180; +0x49B the "in use" byte, which StartLoadProfileFlow /
  StartSaveProfileFlow 0x421190 / 0x421380 reach as +0xE678 + 0x1B3 for i < 3) beside data the three share [C decomp;
  the bytes a new profile saved to PROFILE 1 or to PROFILE 2 changes in the file lie 0x49C apart, the rest equal]. The PROFILE 1-3 list is the message component's type 6 (func_00448BB0 0x448BB0: two
  `i < 3` loops naming the items PROFILE %1 from the records; func_00449150: rows 0-2 answer 9 / 0xA / 0xB, and 0xC
  is BACK, 0xD "nothing"; the component has five item slots, +0x38). More than three in the game's own list would need
  the block's record array, the file size, every `i < 3` loop, the list's answer codes and the pages' switches on them
  changed: not possible without editing the decomp. What the port gives instead: five save files (SAVE FILE AT START),
  each with the game's three profiles -- fifteen, switched at the next start.

### 8.2 Test builds for testers (2026-11-09)

- **Version stamp** (include/b3report/version.h): `<VERSION file>+<git short hash>[-dirty]` and the build date, from a
  generated source (tools/release/version_info.sh -> build/version_info.cpp; the Windows build scripts generate their
  own copy; rewritten only when its text changes, so a new commit recompiles that file alone). The first line of every
  native_play log, the launcher's sidebar, Play page and window title, the game window's title, every bug report.
- **Run logs, always on, bounded** (include/b3report/run_logs.h, tools/native/run_log.h): a window run of native_play
  (not --offscreen / --replay-pad; `--no-run-log` or B3NATIVE_RUN_LOG=0 turn it off) writes
  `<user data>/logs/run-<YYYYMMDD-HHMMSS>.log` -- stdout and stderr, the renderer's lines and a crash report included --
  and `run-<...>.pads.txt` (the `--record-pad` recording with its `rtc` line: `native_play --replay-pad` replays the
  run); only the newest 10 runs stay. A stream that pointed at a terminal, a file or a pipe keeps getting the output
  (a reader thread per stream copies a pipe to it and to the log, unbuffered); one that pointed nowhere or at the null
  device (the launcher's start) is the log file itself. A crash waits up to 1 s for the copies (crash_report.h
  g_crash_flush). The launcher passes `--log <file>` and gives the game the null device (no console window on Windows);
  last-run.log is gone. Started directly without `--game`, a window run takes launcher.ini's game_dir, and with none
  says what is missing in a message box (Windows' own; elsewhere SDL's through the renderer library); so do a missing
  renderer library and a game folder without the retail SLUS_210.50.
- **Bug reports** (include/b3report/bug_report.h, zip.h, sysinfo.h): `bug-report-<YYYYMMDD-HHMMSS>.zip` on the Desktop
  (else Documents, else the user data folder) with README.txt, version.txt, system-info.txt (OS and version, Wine,
  CPU, threads, RAM, the GPU and its driver from SDL GPU's device properties, the GPU drivers, each display's mode,
  SDL's version), pc_settings.ini, launcher.ini, run_state.ini, and the newest 5 runs' logs and pad recordings (a file
  over 24 MB as its head and tail). No game data, no save unless asked (the launcher's "Include my save": the active
  profile-N.sav under save/). The launcher's Play page: "Make a bug report" (then its folder opens); in the game F10
  (made on a thread; the banner says where; the game's GPU described from its own device; `$B3_REPORT_AFTER=<s>`: a check's F10). The ZIP writer is our own
  (deflate through zlib, or stored when that is not smaller; UTF-8 names; no ZIP64). Tests: test_report (the writer
  read back through zlib, the run logs' names and rotation, the version string, a whole report's contents).
- **Release zip** (tools/release/package_win.sh, WSL): builds with `tools/native/build_win.sh RENDER=1 STRIP=1` (or
  takes `OUT=` a finished build and strips copies with the toolchain's strip.exe) and writes
  `$WIN/release/burnout3-native-<version>-win64.zip`: Burnout3Launcher.exe, native_play.exe, b3render.dll, SDL3.dll,
  b3-icon.png (the .exe files carry the multi-size icon resource), README-TESTERS.txt, KNOWN-ISSUES.txt, VERSION.txt, LICENSES/ (SDL3, zlib, stb_truetype, MinGW-w64
  runtime, winpthreads, the GCC runtime exception). No developer tools. The texts are tools/release/*.txt
  (KNOWN-ISSUES.txt kept by hand, in step with docs/known-issues.md).
- **Linux tarball** (tools/release/package_linux.sh, 2026-11-10): `build/release/burnout3-native-<version>-linux-x86_64.tar.gz`:
  b3launcher, native_play, libb3render.so, libSDL3.so.0, b3-icon.png, icons/hicolor/ (installed by add-to-menu.sh), burnout3-native.desktop (starts the launcher
  beside it: `%k`), add-to-menu.sh (the menu entry `~/.local/share/applications/b3port.launcher.desktop` with this
  folder's paths; also what Steam's "Add a Non-Steam Game" lists), README-TESTERS.txt (README-TESTERS-linux.txt: XDG
  paths `~/.local/share/b3port/native`, Vulkan loader per distro, Steam Deck), KNOWN-ISSUES.txt, VERSION.txt, LICENSES/
  (THIRD-PARTY-NOTICES-linux.txt: SDL3, stb_truetype, dr_libs, minimp3, stb_vorbis, the GCC runtime exception). Built
  by `make BUILD=build/linux-release LINUX_RELEASE=1 play launcher`: every source force-includes
  include/b3compat/glibc_compat.h (no C23 strtol redirection, the libm functions glibc 2.38-2.43 re-versioned bound to
  their old versions, __isoc23_strtoul / arc4random for the static libstdc++), `-static-libstdc++ -static-libgcc
  -Wl,--exclude-libs,ALL`, and an `$ORIGIN` RPATH (DT_RPATH: wins over Steam's LD_LIBRARY_PATH) for libSDL3.so.0. So
  the programs need glibc 2.35 (Ubuntu 22.04) and no system libstdc++; from the system only libc / libm / libz and
  (through SDL, dlopen) X11 or Wayland, PipeWire / PulseAudio / ALSA and the Vulkan loader. The script checks each
  program's GLIBC_ versions, GLIBCXX, NEEDED and RPATH and refuses a non-portable one (NONPORTABLE=1: a -nonportable
  package for checks on the build machine), refuses a -dirty or mixed version stamp (ALLOW_DIRTY=1), strips the copies
  (symbols in build/release/debug/<name>/*.debug, gnu_debuglink), and with SMOKE=1 unpacks the tarball into
  build/release/smoke and runs the launcher's screenshot mode and race_check.sh with the packaged native_play (the
  dynamic loader's LD_DEBUG record must show libb3render.so / libSDL3.so.0 from the package folder). SDL3: Ubuntu's
  libsdl3-0 needs glibc 2.43 and links X11 / Wayland / PulseAudio / libdecor / fribidi / gbm directly, so the package
  takes SDL 3.4.2 built by tools/release/build_sdl3_linux.sh (the release tarball, sha256 = GitHub's digest; headers
  from apt-get download'ed -dev packages; every backend dlopen'ed; checked: only libc / libm, glibc <= 2.35, X11 +
  Wayland + PipeWire + PulseAudio + ALSA + Vulkan in).

## 9. Car reflections (reflect1, 2026-11-15)

The paint and glass of a car in a race show only a faint reflection in the game itself: the car shader's second pass
samples the environment map (the sky) at a weight that is low on the flat paint (docs/car-shading.md). **CAR
REFLECTIONS** (SETTINGS > GRAPHICS; ini `car_reflections` 0 Off / 1 Original / 2 Enhanced; renderer
`--car-reflections off|original|enhanced`) replaces that pass:

- **Enhanced** (the default): a clearcoat over the game's lit paint that keeps its colour: the reflection scales the
  paint's own colour (a blue car stays blue, a black one a neutral dark grey) and adds a white sun glint, weighted by Fresnel
  (a low gloss face-on, the sheen toward the body's edges). The same environment map (the track's ENVIRO.DAT sky) is looked up
  along the reflected view ray with contrast: clouds, a bright haze along a hard horizon line, and a dark ground that
  darkens the lower body. Glass is darker with a strong sky in it and a bright rim. It is dimmed where the car's own
  irradiance says it sees little sky (tunnels, cover) and at night the sky is dark by itself. The player's car and the other
  racers at full strength, traffic at about half.
- **Original**: the game's pass exactly (what the PS2 draws). **Off**: no environment pass.
- In both, the environment map holds the track around the followed car (within 100 m), as the game draws it into its
  env-map cameras: in a tunnel the cars mirror the tunnel and its lights, not the sky (docs/car-shading.md §9). Enhanced's
  gloss mirrors the same where the track covers the sky. The LOW preset uses the sky-only map (for weak hardware): the
  track in the map goes with ROAD SHINE (the full-screen specular), which LOW turns off.
- The QUALITY PRESET PS2 ORIGINAL and LOW set Original; the other presets Enhanced.
- **REFLECTION DISTANCE** (Game 100 m / 200 m / 300 m), **REFLECTION DETAIL** (Track / Track and props: the roadside
  props, lamp posts, signs and gantries within 60 m) and **REFLECTION UPDATE** (Every frame / Every 2nd frame, for slower
  cards) (reflect2, 2026-11-22; docs/car-shading.md §10): the map's track part reaches further and holds the roadside;
  REFLECTION DETAIL's third step, Track, props and cars, adds the other cars within 50 m; **REFLECTION RESOLUTION**
  (Standard 128 / High 256 px faces) sharpens them. The game's map by default; ULTRA sets 200 m with the props and the
  cars at High. At night (with the props in the map) the lamps glow in the reflections.

Drawn only (`b3gfx` car shader, `View::apply_car_reflections`): nothing reaches game memory, so a replay is exact. This is
the one remaster feature that is on by default; with Original the frames are byte-identical to the game's own (the
golden record: `tools/golden/baseline.sh` exports `B3_CAR_REFLECTIONS=original`, the same as `car_reflections = 1` in the settings ini). The
scene renderer takes the same value (`b3replay --car-reflections original`, or `--settings`).

The look's values are runtime values with a live tuner (docs/car-look.md): a panel on F8 while DEVELOPER MODS is on,
`car_look.ini` in the user data folder, and `b3replay --car-look <ini>` for screenshots.

## 10. Effect levels, render scale and battery saver (effects2, 2026-11-15)

For weak PCs, integrated graphics, ARM laptops and handhelds (a Steam Deck at 1280 x 800, a Mac on battery). A crash with
tens of thousands of sparks is the heaviest picture the game draws, so **SPARKS** and **CRASH DEBRIS** (SETTINGS >
EFFECTS) have levels, Off / Low / Medium / High: High is the game's whole set (the default, byte-identical), Medium draws
half of the particles and Low a quarter, a little larger so a crash still sparks brightly. The same particles are kept from
frame to frame (nothing flickers). **RENDER SCALE** (GRAPHICS; 100 / 85 / 75 / 67 / 50 %) draws the 3D picture at a share of the
window and scales it up, with the HUD and menus at full size; 125 / 150 / 200 % (perf4, supersampling) draw it larger and
bring it down with an area filter (smoother edges, less shimmer), held to the GPU's limits in very large windows. **BATTERY SAVER** (EFFECTS; off by default) holds the frame
rate to 60 and the particle levels to Medium at most while the PC runs on battery. The QUALITY PRESET drives them: PS2
ORIGINAL, HIGH and ULTRA are High at 100 %, MEDIUM is Medium, LOW is Low at 75 %.

`B3_PARTICLES=off` (environment) turns the particle layer (smoke, dust, glass, fire, below) off exactly (the golden record's reference look; `tools/golden/baseline.sh` exports it); `full` draws the whole set. The layer's build (the game's particle program, about 5.5 ms per tick with smoke) runs on a worker in the window, so it never lands on a frame (a real-window replay of a crash on an RTX 4080 SUPER at 1600 x 900: the slowest 1 % of frames 2.7 ms with it, 2.9 ms without; on the render thread it was 5.9 ms). The explosion smoke and flash rasters are found by their full names (the arrays keep 15 characters), so explosions draw as soft fire and smoke, not untextured squares.

Drawn only (`src/b3replay/sparks.h`, `Options::render_scale`): nothing reaches game memory. The game's other particles (tyre and exhaust
smoke, dust, gravel, snow, glass shards, fire, explosion smoke and fire) are drawn too, from the game's own particle program,
and take the same levels: CRASH DEBRIS scales debris, gravel, snow, glass and every crash-mode particle, SPARKS the smoke,
fire and explosion arrays. The particle layer is drawn one step lighter than the row's level so the default stays cheap: High draws half of the game's particles, Medium and Low a quarter (`particle_layer_level`, `src/b3replay/sparks.h`); `B3_PARTICLES=full` (environment) draws the whole set at High. Each tick the layer is built once (the game's program run on the captured arrays, packets without their own ALPHA / TEST keep the GS state of the packet before them, nearly invisible quads dropped) and uploaded as one vertex buffer. Not drawn yet: the debris pieces (CB3DebrisArray), the explosion's full-screen flash, and the
smoke of the second split-screen view.
Details, measurements and checks: docs/pc-settings.md §7.18.

## 11. FPS counter (fpsoverlay2, 2026-11-15)

The frame-rate readout is a plate in the port's own display style (the dark cut-corner plate with an amber edge and a short
amber accent bar of the notices and photo mode's panels, a pixel font and slanted cells, all solid rectangles: it costs next to nothing per frame). **FPS COUNTER** (SETTINGS >
INTERFACE; ini `fps_view` 0 Off / 1 Minimal / 2 Standard / 3 Detailed; an older `fps_counter = true` reads as Standard) and
**FPS COUNTER POSITION** (ini `fps_corner`: top left, top right, bottom left, bottom right) set it; the shortcut **FPS COUNTER
VIEW** (F3, remappable) cycles the views for the session.

- **Minimal**: a small plate: the number, FPS, and three small signal cells (three lit at the target, two below, one far below).
- **Standard**: the same small plate with the 1 % low under the number (no graph). Minimal and Standard stay within about
  8 % of the window's width on any window and are redrawn twice a second.
- **Detailed**: the big plate: also the frame-time graph in milliseconds (a filled graph of the last 8 s, the slowest
  frame of each 0.2 s, a dashed line at the target frame time (the FRAME RATE LIMIT's, else 16.7 ms), slow stretches in
  amber and spike columns in red), the average, current and worst frame time, the latency, the render size, MSAA and
  preset, the GPU and its backend; at most about 12 % of the window's width on a big window (never under its 720-line size); redrawn twice a second.
- Colour never carries the state alone: the lit cells, the graph's shape and the target line say it too.
- At the margin of the safe area (4.5 % of the height) in the chosen corner at every aspect, scaled by HUD SIZE; in a race it stays
  below / above the game's own HUD plates; it moves clear of a notice, takes the top left while photo mode's display is up, and is never in a screenshot.

Drawn only: nothing reaches game memory, so a replay is exact. Off by default. Design and layout rules:
docs/native-renderer.md §28.1; tests/test_fpspanel.cpp.

## 12. Bloom level (bloom2, 2026-11-16)

The game's bloom is strong by design: the bright part of the frame (above the track's threshold, 0.65 to 0.75) is blurred
at a quarter of the PS2's frame size and kept from frame to frame, with a feedback up to 0.9, so on a bright track (a white sky,
a lit sign) the glow can wash out the picture. It is the same at every window size (the port builds it at the PS2's
buffer sizes and scales it with the window's height: 640 x 448, 1920 x 1080 and 3200 x 1800 add the same share of the
picture), so a big window is not the cause; the strength is. **BLOOM** (SETTINGS > EFFECTS; ini `bloom_level` 1 Soft /
2 Standard, with `bloom = false` for Off; row id 260) has three choices: Off, **Soft** (the default) and **Standard**. Standard
is the game's own sum (the frame plus the glow). Soft adds the glow at 60% of its intensity as a screen blend,
`1 - (1 - frame)(1 - glow)`, so the sum never passes white and bright signs keep their detail. The PS2 ORIGINAL preset
is Standard. Drawn only: nothing reaches game memory. The golden record is Standard: `tools/golden/baseline.sh` exports
`B3_BLOOM_LEVEL=standard` (`soft` is the other value); the offline `b3replay --offscreen` also starts at Standard
unless that variable says otherwise.

Cost: with MSAA on, the scene pass before the bloom stores its samples (4 samples at RGBA16F: 184 MB at 3200 x 1800),
resolves, and reloads them after the bloom chain; the glow is then blended into every sample. This "split" form is the
default, and the PS2-sized path (POST PS2, the golden record) and MSAA off use it byte for byte. A lean form exists
for comparisons (`B3_BLOOM_LEAN=1`, POST PC with MSAA only): the pass before the bloom resolves into a single-sample
texture without storing its samples, and one full-screen draw starts the resumed pass with the resolved picture plus
the glow. Its picture is the same to within rounding (mean difference 0.00001 of 255, at most 3 of 255 on a few
pixels). Measured in a real window on a high-end desktop GPU (5120 x 1440, MSAA 8, the same race replay), the lean form
was slower than the split (218 fps against 238), so it is not the default.

## 13. Languages and analog triggers (1.1, 2026-11-18)

- **Languages.** The game's text in French, German, Italian, Spanish, Dutch, Swedish or UK English, imported once from
  the player's own European disc (no game text ships with the port), and the launcher and the port's own SETTINGS page
  translated into the same languages (our own words). English stays the default. docs/pc-settings.md §7.19.
- **Analog triggers.** A trigger on accelerate or brake drives the game's own analog throttle and brake (the right
  stick's up / down that the PS2 game already reads), so the pedals work gradually, as on the Xbox release. On by
  default; Off is 1.0.0's trigger-as-a-button. docs/pc-settings.md §7.20.

## 14. Chase camera, sharp upscaling, steadier frame limit (1.1, 2026-11-18)

- **Chase camera distance and height** (CAMERA tab): the race camera further back and higher, tilted to keep the car
  in view; drawn only. docs/pc-settings.md §7.21.
- **UPSCALING Sharp** for RENDER SCALE below 100%: a sharper picture on weak GPUs and handhelds. §7.22.
- **Frame rate limit pacing**: a fixed schedule, and vsync alone when it already holds the rate. §7.23.

## 15. Car paint: any colour, decals off (carpaint, 2026-11-19)

The feature: the player's car in any colour, not only the paints the car select offers, and the car without its livery
(stripes, logos, numbers) in plain paint, with lights, windows, grilles and wheels kept. Both are drawn only, for the
local players' cars, and off by default.

### 15.1 How the game chooses and draws a paint (study)

- **The index.** The car select's colour (Square: Color) ends up as the race car's paint byte, EssentialParams +0x59.
  `CB3RaceCar::Prepare` 0x152AF0 wraps it by the atlas's palette count (raster +0xA0) [C nonmatching Prepare]. Replays,
  ghosts and the save keep the index, so the port never changes it.
- **The CLUT.** `CB3VehicleRenderer::SetTexture` 0x21FF60 points TEX0's CBP at palette k of the car's atlas: palOffset +
  the raster's u16 table at +0x90 [C] (docs/modding/file-formats/txd.md). One 256 x 256 8-bit atlas per car file, up to
  8 CLUTs, each one paint. The texels are shared by all the paints; only the CLUT changes.
- **The materials.** Each mesh entry's material word holds a 16-bit mask, the ALPHA_1 byte and a texture slot
  (docs/car-shading.md §3). Slot 0 is the atlas: the body, the panels, the lights (entries whose mask has light bits
  0x04..0x80) and the wheels (lists B0..B2) all sample it. Slot 1 is the underside and slots 2..4 the glass (intact,
  cracked, broken). All four are the global textures in DATA/GLOBAL.TXD, the same for every car (docs/native-renderer.md
  "Texture slots"). So the glass never carries paint or livery, and a change to the atlas never reaches it.
- **The livery is in the same atlas as the paint.** Logos, numbers and sponsor stripes are texels inside the paint area,
  whose CLUT entries are the same in every palette. The paint's entries are a ramp of shades of each palette's body
  colour. Some cars also have a second colour that changes with the paint but not in step with it: two-tone stripes,
  and the boxes behind some logos. Lights, grilles, wheels and the interior sit in the atlas's other blocks, in fixed
  entries.
- **The reflections.** The game's second pass (context 2) blends the environment sphere map over the lit atlas,
  weighted by the Fresnel alpha times the atlas alpha. The port's Enhanced reflections (§9) are a clearcoat that scales
  the paint's own colour. Both work on whatever colour the first pass gives, so a custom paint gets the same gloss.

Census of the US disc (`b3asset::car_paint_map` over every PVEH/*/CAR*.BGV; tests/test_carpaint.cpp has the synthetic
cases):

| | |
|---|---|
| player car files | 67 |
| with 8 paints (a mask can be told) | 57: paint covers 22..51 % of the atlas; 1,832 decal islands in all, 61 islands kept as parts |
| with **one** paint (no mask: drawn as the game's, custom paint and decals off do nothing) | 10: the five **Dominator** cars (Compact, Muscle, Coupe, Sports, Super), **Custom Coupe Ultimate**, **Classic Hotrod**, **City Bus**, **Trash Truck**, **Fire Truck** |
| palettes that decode equal | most cars: the 8 variants collapse to about 4 distinct CLUTs (e.g. Compact Type 1: 0 = 3, 1 = 4). The map needs two distinct ones, which every 8-paint car has |

### 15.2 How the port tells paint, livery and parts apart

`include/b3asset/carpaint.h`, once per car file, from the PS2 decodes only (no HD pack). It takes about 30 ms:

1. **Paint**: a texel whose colour differs between the palettes (more than 8 of 255 in some channel).
2. **Main paint vs second colour**: each paint texel's colours in all palettes form one "signature" vector. The main paint is the
   signature most paint texels share (a mode, refined, cosine > 0.97). The other paint texels are a second colour.
3. **Decals**: the fixed (non-paint) texels in islands enclosed by paint (at least 75 % of the border is paint, the
   atlas edge counting half; under 10 % of the atlas). Main paint texels within 2 of a decal or a second colour join it
   (anti-aliased edges: Coupe Type 3's boot lid logos are a darker tone of the paint, and their edges read as main
   paint at a darker shade, a ghost of the logo once it is filled). Small paint islands inside a decal (the paint
   showing through a logo's letters) join it too.
4. **The meshes' check**: an island is kept as a part when a light entry or a wheel list samples it, or when the body
   triangles that sample it cover less than 20 % paint (their own polygons: a licence plate, a lamp, an exhaust). The
   Delivery Truck's atlas packs its lamps, wheels, exhaust and plate on a paint-coloured background: without this check
   they were all taken as decals; with it 7 are kept and 5 small islands go, among them its two shield logos.
5. **Shade**: each main paint texel's brightest channel over the mean of the brightest paint (the reference palette),
   inpainted (diffused from the paint around) under the decals and second colours.

The result is an RGBA mask the size of the atlas (R main paint, G second colour, B decal, A shade / 2).

### 15.3 How it is drawn (prototype)

In the car shader (`car.frag`, a fourth sampler and two uniforms; `b3gfx::CarParams::paint_*`), applied to the atlas
texel before the game's lighting and the env pass:

- **Custom paint**: the car's model is decoded with the reference palette (its brightest paint: the most shading
  levels). Main paint becomes colour x the texel's own shade, and the second colour becomes the second colour (or a
  shade of the paint) x its shade. The shade is read from the texel actually bound, so **an HD pack's replacement keeps
  its detail**: the pack's texture for the reference palette is loaded as usual (its key is the PS2 texture + that
  CLUT, unchanged). White (FFFFFF) is drawn at texel 120, the game's light paints' level.
- **Decals off**: decals and second colours are filled with the paint (the custom colour, or the game paint's mean
  colour) at the inpainted shade. Lights, wheels, grilles and the plate stay.
- Only the local players' race cars (a CB3PlayerCar: +0x1914 = 0, +0x27C0 = the player, captured as
  `Vehicle::player`) and the front end's car on show. AI, network cars, traffic and the Time Attack ghost keep the
  game's paint. In a split screen each player can have their own paint (player 2 from `$B3_PAINT_P2` for now).
- Off, nothing changes: no paint map is made, the draw's mode is 0 and the shader's branch is skipped. The golden record
  (defaults) stays byte-identical.

Environment (prototype): `B3_PAINT=RRGGBB` (player 1; `game` = the game's), `B3_PAINT_P2=RRGGBB`,
`B3_PAINT_SECOND=RRGGBB` (the livery's second colour), `B3_DECALS=0`, `B3_PAINT_FINISH=gloss|metallic|pearl`. Settings
(CAR > CAR PAINT; the launcher shows the same rows): CAR PAINT Game / Custom, PAINT RED / GREEN / BLUE (0..255 in
steps of 15; the ini `paint_red` etc. take any value; the default 195 / 30 / 30 is on the steps, so every value the
rows give can be reached again), DECALS On / Off, FINISH (below); PLAYER 2 CAR PAINT: P2 CAR PAINT, P2 PAINT RED /
GREEN / BLUE (default 30 / 90 / 210), P2 DECALS (ini `p2_car_paint`, `p2_paint_red` ..., `p2_car_decals`). Row ids 280-284
(player 1), 285 FINISH, 286-289 and 291 (player 2). The colour rows show a swatch of the colour they make, in the game's
page and the launcher's. Translated in the six languages. The environment wins over the ini.

### 15.5 Finish, player 2, the showroom (padpaint, 2026-11-19)

- **FINISH** (ini `paint_finish` 0..2, default 0 Gloss: the game's): the local players' cars' paint (the map's main
  paint and second colour; with decals off the filled decals too), whatever paint they wear, in car.frag after the
  reflections (paint mode bits 2 / 3). **Metallic**: the sky mirrored in the paint's own colour, 35 % face-on to 100 % at
  the edges, and flakes (a hash per atlas cell, 900 a side, changing with the angle to the eye) that catch the sun.
  **Pearl**: the paint's hue turned round the grey axis by up to 1 rad towards the edges ((1 - n.v)^2), with a soft
  white sheen there.
  Gloss and CAR PAINT Game with decals on (the defaults): no paint map, mode 0, the cars as before (golden unchanged).
- **Player 2**: each car takes its own player's paint: the race cars by `Vehicle::player`; on a front-end page only the
  car on show (Tick::fe_car), by the car select page's player while that page is up (CB3CarSelectPage 0x01E94EF0 +0x18,
  the menus' current page word 0x01E90430; the FECAR event's fourth word, Tick::fe_player), else player 1's; logged as
  `car paint: the front end's car (...): player P's paint`. Every other race car on a front-end page keeps the game's
  paint (before, they all took player 1's). (RenderFrontEnd's car index is no guide: the single player car select
  alternates world cars 0 and 1 as the carousel turns.)
- **No hitch**: the paint map (~30 ms) is made on a worker in the window (View::async_paint): the car shows the game's
  paint for the frames until it is ready. The maps are kept across routes (only their textures are made again).
- **The launcher's showroom** (src/b3launcher/showroom_view.h `set_paint`) draws player 1's custom paint, decals and
  finish when CAR PAINT is Custom, as the game does (the model in the reference paint, the map, the same shader).

**What stays as the game draws it**: the sun strips across some windscreens (a fixed bar that touches the atlas's
fixed half, so not an enclosed island) and stickers on parts with an atlas block of their own (Oval Racer Special's rear
wing); numbers printed as a darker shade of the very same paint (Oval Racer Special's "11": the same colour signature as
the body, so they read as the paint's own shading and stay as a faint tone-on-tone shape); the 10 one-livery cars. A sticker printed on a
paint-coloured part of its own (a badge with no paint around it) can be kept as a part.

### 15.6 Paint layers, colour picker, painted thumbnails (paint2, 2026-11-22)

Inspired by community car colour mods (a colour picker on the car select, layers for the body, decals, wheels and
nitro, a fine mode and a factory reset; painted carousel thumbnails). Written anew in the port's paint panel
(pc-settings.md §7.31.1): LAYER (Body / Decals / Wheels / Boost
glow: the exhaust glow while boosting, the effect the port draws there), COLOUR with Game first, COLOUR PICKER (pad, keyboard, mouse), FACTORY COLOURS; one set per player for every car,
drawn only. The thumbnails are drawn from the cars' own models (paint3, below), so they show every layer as the car does.

paint3 (2026-11-22): paint2 recoloured the thumbnails from a paint map of their own palettes. On those pictures the
windows, the shading and the wheels never change between palettes and sit inside the paint, so the map took them for
decals: large decal-coloured areas the car itself does not have. Now, while the page player's paint is not the game's,
each thumbnail is drawn from its car (PVEH/<CLASS>/CAR<n>.BGV, the picture's name) with the same paint code as the car on
the page: a 3/4 front view as the game's pictures (yaw -44, pitch 18 degrees, measured against all 67 race cars' game
pictures by their silhouettes), the framing fitted once per car to the game picture's coverage, drawn twice (on black, on
white) for the coverage. The pictures are drawn without waiting for the GPU, one new one a frame (the game's picture shows
until ours comes), and the models loaded only for them are given back once their cars leave the carousel. With the game's paint the game's
pictures are drawn as before. A car the carousel shows on another car's picture (a series of traffic vehicles, which
have no pictures of their own) is drawn the same way from its own model (PVEH/<CLASS>/CAR<n>.BTV), in the player's paint as the car on show and the
race draw it: native_play reads which car each visible carousel slot stands for and tells the renderer.

### 15.4 Effort

| Piece | State | Effort |
|---|---|---|
| Study (this section) | done | 0.5 day |
| Paint map (b3asset/carpaint), tests | done | 1 day |
| Shader + renderer hook, env vars, player identity | done | 0.5 day |
| Settings rows (GRAPHICS > CAR PAINT), ini, translations, settings test | done | 0.5 day |
| A hand-made exception list for misread islands (per car: keep / remove), from a pass over all 57 cars in the showroom | to do | 1 day |
| Paint finish (metallic / pearl; §15.5) | done (padpaint) | 0.5..1 day |
| A swatch beside the colour rows; the launcher showroom drawing the custom paint; player 2's rows | done (padpaint) | 0.5 day each |
| A "CUSTOM" entry in the car select's colour row | not recommended | 2+ days: the row's count comes from the atlas (+0xA0) and the index goes into game state, replays and saves; it could only be a drawn-only overlay with a port key |
| The 10 one-livery cars | not possible this way | hand-made masks, or the Xbox release's palettes when the player has imported that disc: its car atlases carry 8 palettes whose per-entry alpha is about 227 on body paint and 0 on parts (a ready-made paint mask; xboxtex study) |

## 16. Garage camera (garagecam1, 2026-11-19)

On the car pages (SELECT CAR, YOUR GARAGE) the right stick, or a mouse drag on the car, turns the view around the car;
the car's own spin stops while it is used and comes back, eased, a second after the stick is let go, from where the view
is (no snap). No zoom, and the height is clamped, so the camera cannot clip into the car or the room. Drawn only (the
renderer turns the camera it draws with; the game never knows), on by default, SETTINGS > CAR > GARAGE > GARAGE CAMERA to
turn it off. docs/pc-settings.md §7.26.

padpaint (2026-11-19): a **drift guard**, so a worn stick resting just past the dead zone cannot hold the view for
ever: a deflection counts while it is firm (past 0.6) or while the stick moves (a change over 0.05) and for a second
after; a stick sitting still at a smaller deflection stops counting then and the spin comes back (GarageOrbit
`stick_counts`; test_garagecam `drift_guard`). A mouse press on the car belongs to its page: when the page goes away
(another page, or PC SETTINGS opened) before the release, the release presses nothing (menumouse.cpp).

## 17. Night mode and weather: study and first prototype (nightwx, 2026-11-19)

Asked for by players. The game has no night races: every route is lit once, in daylight, and that light is baked.

(1.4.0: night ships as an experimental option, SETTINGS > EXTRAS > EXPERIMENTAL > NIGHT, per race and in World Tour:
§45.)

**How a frame is lit today** (docs/native-renderer.md §11, docs/sky-enviro.md, docs/car-shading.md):

- **Track**: prelit. The vertex colour is the light (0x00-0x3F in GS units, the frame drawn at half intensity and doubled
  on present); `mesh.vert` passes it on unchanged (types 0/6), type 4 instances carry a per-instance colour, types 1/2 add
  a Phong term into the vertex alpha that the **full-screen specular** pass turns into sheen (specular colour x frame
  alpha). No light sources, no normals used for lighting: night cannot come from relighting, only from grading.
- **Sky**: ENVIRO.DAT's gradient + clouds on a camera-centred dome (`Shading::kSky`), the sun blob, its corona and lens
  flare (`View::post_frame` -> `b3view::Sun`), fog colour / curve from the same file (`b3view::frame_params`).
- **Cars**: 9-coefficient SH irradiance per car from the track's collision vertices (daylight values), the env map
  (sky, or the track around the car with CAR ENV) in the reflection pass (`car.frag`).
- **Post**: the effects stage (shadows, skids), the post-fog stage (crash objects, particles, **coronas**: every car's
  head / tail / brake / indicator lights and the traffic signals, additive), then bloom, sun corona, zoom blur, 2D.
- Hooks every race frame passes through: `View::post_frame` (all paths: live, offline, photo mode, split screen; not the
  front end) and `View::draw_list`; in `b3gfx` the scene pass's phases (`render_scene`).

**Where night fits, and the effort** (one developer, days of focused work):

| Piece | How | Effort |
|---|---|---|
| (a) Global grade | A multiply of the frame at the start of the post-fog stage, sky and world apart by the depth test (sky at the far plane); the additive post-fog effects (coronas, sparks, fire) drawn after it, so they stay bright and feed the bloom; the sun corona off. **Prototype done (below).** A better grade (desaturation, a curve) needs the frame read back: one more full-screen pass with a copy, ~1 d | done; polish 1-2 d |
| (b) Headlights | A pool of light per car on the ground ahead (the shadow's ground plane and box), blended Cd (1 + L) so it scales the road's own colour. **Prototype done.** Limits: a plane, so on a crest or a dip the far end floats or sinks; walls and other cars are not lit. A real projected spot light needs the scene's depth (a depth pre-pass exists for photo mode's DOF): light volumes or a screen-space pass, 3-5 d. Car lights forced on (coronas) is done; the lamp glass of the car model itself (material light bits, `lx16LitBits`) lit too: ~1 d | done; proper 3-5 d |
| (c) Street lights, lit windows | The tracks have **no light-marker or emissive data** (no light sources in STATIC / STREAMED; the overhead wires carry lamps as geometry). Options: a hand-placed light list per route (37 routes, 1-2 h each, plus a corona + pool per light: ~6-8 d), or texture-name heuristics for windows / lamp heads (names are in the TXDs; a guess list, ~3 d, uneven). Traffic signals already have coronas. | 3-8 d |
| Sky | Now the dome x a dark tint. A night sky (stars, a moon, a dark gradient) as a replacement ENVIRO gradient / cloud raster made by the port: ~2 d; the car env map's sky the same: +1 d | 2-3 d |
| Car lighting | Cars are graded with the frame; their SH is daylight: scaling the SH and the env strength per car by a night factor is a uniform change in `car.frag`, ~1 d (and cars passing through head-light pools of others are not lit) | 1 d |
| Settings | PC SETTINGS > GRAPHICS row (TIME OF DAY: Day / Night), launcher, ini, presets untouched (default Day): ~1 d plus translations | 1-2 d |

**Weather** (rain, wet roads):

| Piece | How | Effort |
|---|---|---|
| Rain in screen space | Streaks as a 2D layer (before the HUD), moved by the camera's speed; drops on the "lens" in the chase view: cheap, the port's own pass | 2-3 d |
| Wet road | Darker albedo + stronger reflection on road materials. Road materials can probably be told apart, not checked yet: the type-1 materials (Phong specular into the frame alpha) are the likely road candidates, and the texture names in the track TXDs (`b3hdkeys` lists them) can confirm; a per-route allow-list where they cannot. The sheen today is the full-screen specular (sun-only, no reflection of the world): a wet road wants a screen-space reflection of the scene (the frame is there; depth from the pre-pass) masked by the frame alpha the road already writes: 4-6 d. A cheap first step (more full-screen specular on type-1 materials + darker type-1 albedo): ~1-2 d | 2-6 d |
| Spray from wheels | The game's particle program draws tyre smoke and dust already (`effects3`): a spray emitter would be our own particles behind each wheel (speed-scaled, drawn with the particle layer's state): 2-3 d | 2-3 d |
| Puddles, wet car paint, wipers in the in-car view | later | - |

Weather and night stay drawn only: nothing reaches game memory, physics and AI are the game's (the grip of a wet road is
not simulated: that would change gameplay and replays).

**Prototype (nightwx, environment only).** `B3_NIGHT=1`: the frame multiplied by a blue-tinted factor after the post-fog
geometry, the sky by a darker one, a headlight pool per car, the head and tail light coronas lit after the darkening. Seen
in offscreen race shots (US/C2_V2, US/M1_V1, AS/M1_V2): a convincing night from the chase camera. It became the feature
below.

### 17.1 Night mode as shipped (night, 2026-11-19)

GRAPHICS > LIGHTING (docs/pc-settings.md §7.28): **TIME OF DAY** Day / Night (row 370), **MOON** Off / On (371, on by
default, used at night only). Drawn only, off by default; the front end is never darkened (night applies to the race's
frames: live, photo mode, split screen, replays). `$B3_NIGHT=1` / `0` and `$B3_MOON` win over the rows.

The frame (`FrameParams::Night`, `b3gfx` render_scene's post-fog stage, `shaders/night.frag`):

1. The post-fog draws that do not give light (smoke, dust, gravel, glass, debris, the crash objects) draw first, so they
   are graded with the frame. Tyre smoke is additive in the game (GS ALPHA 0x48), so "additive" cannot tell light from
   smoke: the producers tag what gives light instead (`DrawItem::night_glow`: the coronas, sparks, the crash's sparks,
   fire and the explosion flash particles by texture name, the explosion flash).
2. The scene pass ends; with MSAA its first part resolves into a copy (`Targets::nc`), without MSAA the frame is copied
   there; the pass resumes with its samples and its depth (stored only at night).
3. **The grade** (where the depth is nearer than the far plane): desaturated to 42 %, multiplied by a cool tint
   (0.31, 0.36, 0.55), then the curve pow(c / 0.5, 1.18) x 0.5 (darks deeper, highlights kept). The fog colour is set so
   that the grade turns it into the night sky's horizon: distant scenery fades into the sky.
4. **Headlights**: since nightlights (§17.2) a projected spot light per car on HIGH, ULTRA and MEDIUM; LOW and PS2
   ORIGINAL keep the first form here (`shaders/light.frag`, one quad per race car and drawn traffic car on the shadow's
   ground plane): a pool just ahead of the car and the beam's throw out to 80 m (narrower, brightest on the centre
   line, fading out), now blended as the spot lights are (the pre-grade colour brought back, §17.2). The throw is what
   the chase camera sees past its own car; in a bend the beam goes where the car points, off the road's centre.
5. **The night sky** (where the far plane shows; drawn after the pools, so none of their light lands on the sky): a
   gradient from the horizon to the zenith, the original sky x (0.035, 0.045, 0.09) (its clouds and the backdrop's
   mountains and skylines stay readable), the clouds (white, bright texels) lit by the moon and brightest near it,
   procedural **stars** (one candidate per ~0.3 degree cell of the view ray, kept off cell edges so none is cut, sized to
   a pixel; only in clear blue sky: none on clouds, haze, the backdrop's mountains, near the horizon or inside the moon's
   halo), and the **moon**: a disc of 1.2 degrees radius with a soft limb and faint maria (fixed noise), a two-part halo,
   bright enough (0.72 in frame units) that the bloom makes it glow; terrain and buildings cover it (depth).
6. The light-giving post-fog draws (coronas with every race car's and traffic car's head and tail lights on, from a copy
   of the frame's light byte; sparks; fire), so they stay bright and the bloom picks them up. No sun corona or lens flare.
7. The tail lights (taillights, 2026-11-22; night and rain): every car's tail lights glow at 40 % of its brake light
   and rise to the full brake light while it brakes, as on a real car: the player, the rivals and the traffic, from
   the game's brake bit (+0x58 0x10; the game sets it for all three). The brake level is the bit's mean over the last
   5 ticks of the feed (83 ms; the tick's alpha moves the window), so a light rises and falls instead of flickering and
   is the same in the window, a shot and a replay. The rear coronas draw the brake colour and size at that level (one
   glow where a car's tail and brake lamps are the same, else the tail lamps at the tail level and the brake lamps at the
   brake level); the lamp meshes mix their unlit and lit constants by it (a brake lamp max(tail, brake), a tail lamp the
   tail level). A dry day keeps the game's lights. `B3_TAIL_LEVEL=<0..1>` sets another tail level (for checks), `B3_TAILLIGHTS=0` draws the lights as before (for comparisons).

**The moon's place**: at the route's ENVIRO sun bearing, its height clamped to 10..20 degrees (`moon_direction`). The sun's
bearing because every baked shadow of the route falls away from it, so the moonlit faces and the shadows agree; the low
height so it shows in the chase camera's view (the routes' suns stand 20..60 degrees high). Opposite the sun was the
alternative: the same chance of being in view over a lap, but the light would come from the shadowed side.

**Moonlight** (MOON on): `FrameParams::to_sun` is the moon's direction for the whole frame, so everything lit by the
route's sun follows the moon:
- cars (`car.frag`): the daylight SH x 0.95, the env map x 0.45, plus a silver-blue key light (0.55, 0.66, 0.88) x 0.8 x
  N.L; the env map's sun blob becomes the moon, so the gloss shows a moon glint on paint and glass;
- the track: the surface relief path (§18) runs with the moon as its light (relief 1.6 when SURFACE DETAIL is off, a
  sheen of 0.12 on the asphalt towards the moon) and a facing term: faces turned to the moon x up to 1.3, faces turned
  away x 0.75, at any distance (walls, kerbs and buildings read in the moonlight);
- ROAD SHINE (the full-screen specular): 0.3 x the moonlight's colour instead of the sun's.
With MOON off: no disc, halo or moonlit clouds, no key light, no relief or facing from it; the grade, stars and lights
stay.

**For a shadow pass**: `FrameParams::key_light` (dir towards the light, colour, night) is filled for every race frame:
the ENVIRO sun by day, the moon at night (colour 0 with the moon off). Nothing reads it yet.

**Tuning by environment**: `B3_NIGHT_WORLD` (the tint), `B3_NIGHT_SKY`, `B3_NIGHT_LAMP`, `B3_NIGHT_MOON` (the moonlight;
"r,g,b"), `B3_NIGHT_GRADE` ("saturation, gamma"), `B3_NIGHT_BEAM` ("tan of the half angle, range m, pool gain, throw
gain"), `B3_NIGHT_STARS`. Diagnostics: `B3_NIGHT_ABTIME=N` (offscreen shots: the frame redrawn N times with the night
stage off and on, interleaved; medians), `B3_NIGHT_MOONSCAN=1` (each race tick whose view axis is within 35 degrees of
the moon: how to find the shots that show it).

**Off**: no copy, no grade, no pass, no item; the depth is not stored; the frames are byte-identical (the golden record's
140 shots PASS).

**Street lights** (`View::night_street_load` / `night_street_frame`, the same pass): the tracks' lamps are unlit
geometry, so they are found at the route's load from a per-region list of lamp textures (`night_lamp_names`: lamp
heads and whole lamp posts; the false friends left out: `Chgo_Rd*Light` and `WF_paveGASLAMP` are road and pavement,
`WF_gaslampSHOP*` shop fronts, `HK_ResLite` windows, tunnel and traffic-light textures, `R_lighthouse01`). Every vertex
of a listed texture (instanced ones through their placement) falls in a 1.5 m cell over x / z; neighbouring cells join
into one post and its highest point (0.3 m down) is the lamp; lamps within 3.5 m merge (an arm and its pole, a head
and its post in two textures). Found: US/C2_V2's region 746 lamps, EU/P1 717, AS/C2_V1 292 (3-8 ms at load;
`$B3_NIGHT_LAMPSCAN=1` lists the candidates per texture). Each frame the nearest 24 within 120 m whose pool reaches the
view (MEDIUM 8, LOW / PS2 ORIGINAL none; `$B3_NIGHT_STREET`) add a pool under the lamp in the beam pass: a downward
cone (full within 45 degrees of straight down, none beyond 72), the falloff 1 / (1 + (d / 7)^2) faded out 14-24 m from
the lamp, gain 2.2, the facing term as for the beams; the restore is tinted per lamp, sodium orange (1, 0.62, 0.30:
city streets, motorways) or warm white (1, 0.86, 0.66: town, park and gas lamps, docks), the beams and lamps mixed by
their share of the light. The lamp heads glow through the signage tables (the lens is the texture's bright part).
`$B3_NIGHT_STREET_SHAPE` = "inner deg, outer deg, d0 m, gain, fade start m, fade end m". nightfix (§35.1): more
lamp textures (the Asia city and motorway, HK's dock road, US and EU post atlases), pools scaled by the lamp's height, 32
lamps, no pop at the cap. Cost on the RTX 4080 (as below,
24 lamps against `$B3_NIGHT_STREET=0`, the same polls): +0 to +0.3 ms at 1920 x 1080, +0.3 to +0.6 ms at 3840 x 2160.

**Cost**, on the owner's RTX 4080 SUPER (native_play.exe offscreen from WSL, race_check's race, MSAA 4,
`B3_NIGHT_ABTIME=15`: the same frame drawn 15 times per form, interleaved, median draw + read-back; other jobs on the
machine, so +-0.2 ms of noise), polls 10900-11300 with 5-8 lamps:
- 1920 x 1080: day 2.5-3.4 ms; night +0.3 to +0.9 ms, of which the beams (pre-pass and light pass) +0.4 to +0.9 ms;
- 3840 x 2160: day 7.6-8.3 ms; night +1.1 to +1.4 ms, of which the beams +0.7 to +0.9 ms.
The light pass computes the normal only where a lamp reaches and discards the rest, and moving that work out did not
change the 4K figure: the cost is the depth pre-pass (the track drawn a second time). It is over the 0.5 ms budget at
4K; the follow-up is to drop the pre-pass (the scene pass writing its depth into a single-sample colour target beside
the frame, as §18 says for the AO), after which the beams are one full-screen draw. In the software rasteriser (WSL)
the same pre-pass is +64 to +104 ms per 1280 x 720 frame (most of a second geometry pass on the CPU), dev's night +8 to
+17 ms.

**Limits**: the beam's facing term comes from the depth's derivatives, so low-poly road shows its facets faintly at
the beam's edge; a car's lamp sits at the nose's centre (one source per car, not two); within 60 m of a tunnel's exit
the outside takes the fade's light too (the fade is by distance, not by what covers each pixel). The tunnel test: §35.1.

### 17.3 Day or night per race (todrace, 1.3.0, 2026-11-20)

The track pages -- Single Event's SELECT OPTIONS (REGION / TRACK / RIVALS for RACE, TIME ATTACK and ROAD RAGE;
0x01E96F78, Update 0x432A50), its CRASH's JUNCTION SELECTION (0x01E97F38, Update 0x454BF0; Multiplayer's crash modes
show the same page) and Multiplayer's REGION / TRACK page (0x01E97640, Update 0x433C20) -- get one more prompt above
their prompt bar, right-aligned with Continue: "Time of
day: Default (Day)" / "Day" / "Night" in the game's GlobalFont and the bar's blue, with the TIME OF DAY PER RACE
action's button as the prompts draw it for the device in use (the PlayStation's L1, the Xbox LB, the Nintendo L, the
keyboard's T key cap). It fades in and out with the page (the page manager's state 5, the game's own test for input).
The pages read Confirm, Back, Up / Down and Left / Right only [C: 0x4328C0 / 0x432560, 0x454BF0, 0x433A70 / 0x433720],
so L1 is free there; the window withholds the action's inputs from the game's report while such a page is up, so the
game sees no press. In Multiplayer the action is player 1's (as every port action), and both split-screen views use the
choice.

The choice is held by native_play (`tools/native/race_time.h` RaceTime): stepped by each press while the page takes
input, taken by the race when the loading screen comes up (so a restart keeps it), back to Default on the main menu. World
Tour shows none of these pages, and the main menu comes before any World Tour race, so World Tour always follows TIME OF
DAY. The renderer
takes the race's value per tick (host record 'HTD1' -> `Tick::race_time` -> `Frame::race_time`; `View::night_on`), so the
race's frames, its photo mode and both split-screen views use it; replays record it (setting `race_time`) and play it back
on the same VBlanks. Default leaves every frame as before.

## 18. Surfaces that react to light: study and first prototype (surface, 2026-11-19)

The aim: a modern-remaster look on the PS2 art (relief that moves with the camera and the sun, sheen, contact shading)
without losing the baked look. (§17 is the night / weather study.)

**What the game gives us** (docs/native-renderer.md §11, docs/car-shading.md, docs/effects-survey.md §3.1):

- The track is **prelit**: the vertex colour is the light. Vertex normals exist only on the type 1 / 2 materials (the
  Phong-into-alpha and reflection-texgen programs); every other material carries a constant up vector. There are no
  tangents. A per-pixel frame therefore has to come from the screen-space derivatives of the world position and the UV
  (or per triangle at load); the prototype does the former, which needs nothing from the data.
- The sun direction and the sun specular colour come from ENVIRO.DAT (`FrameParams::to_sun`, `FrameParams::specular`);
  the **full-screen specular** pass adds `specular x frame alpha` to every pixel, so the frame alpha is the game's own
  "how shiny" channel (only that pass reads it: the effects blend by their own alpha).
- Car shadows are the game's blob shadows: an 8-vertex strip per car with GLOBAL.TXD "blobbyshadow", blended (Cs - Cd) As
  + Cd in the effects stage, darkness from krShadowDarkness and the height above the ground. No shadow maps anywhere.
- Car paint: the Enhanced CAR REFLECTIONS (§9) are already a clear-coat (Fresnel, the env map with the track around the
  car, a sun glint) over the game's SH-lit paint.
- Texture filtering: the default is already anisotropic 16x with trilinear mips (`Filter::kAniso16`).

**The pieces, effort and cost** (one developer, days of focused work; GPU cost estimated for a mid-range desktop GPU at
1920 x 1080, measured cost on the software rasteriser below):

| Piece | How | Effort | GPU cost |
|---|---|---|---|
| 1. Relief from the colour texture | **Prototype done.** In `mesh.frag` on the opaque track draws: the luminance gradient of the colour texture (4 extra taps at the mip one level coarser than the footprint's, so it is the same for the original textures and an HD pack), a normal on the derivative frame, lit by the ENVIRO sun as a factor around 1.0: `1 + (N'.L - N.L)` on sunlit faces plus a share of light from above, clamped to 0.6..1.4. Flat texels keep the game's colour exactly. Fades out from 24 to 60 m (no shimmer at a distance). A load-time normal map per texture (cached, mip-filtered normals, a second sampler per draw, paired with the HD swap in `set_texture_original`) would be steadier on far, minified surfaces: +2 d | done (~1 d); cached maps +2 d; a per-texture strength list (bricks strong, signs and decals off) +1-2 d | ~4 taps + ~40 ALU per opaque track pixel: ~0.1-0.2 ms |
| 2. Sheen from texel heuristics | **Prototype done.** A Blinn-Phong lobe on the bumped normal, weighted by a gloss guessed from the texel (grey, mid-dark, upward-facing = asphalt; grey, bright, vertical = metal at half), added to the frame alpha, so the game's own full-screen specular turns it into the route's sun specular colour (no new pass, fogged like the rest). It shows only looking towards the sun. Wet roads (a reflection of the scene) need a screen-space reflection on the depth (nightwx §17: 4-6 d). Glass in the track (shop windows) cannot be told from the texel: needs a name list from the TXDs (`b3hdkeys`), 1-2 d | done (~0.5 d); name lists 1-2 d; SSR 4-6 d | in piece 1's taps: ~0.02 ms |
| 3. Ambient occlusion | **Prototype done.** `shaders/ao.frag`: the depth pre-pass photo mode's depth of field already draws (every Z-writing draw, cars included), a half-size pass (12 taps on a disc of 1.2 m, the normal from the depth, rotated per pixel), then (nightfix, §35.1: the rotation a 4 x 4 ordered pattern) a half-size 4 x 4 depth-aware blur and a bilinear depth-aware upsample multiplied into the opaque scene before the full-screen specular. It gives the cars contact shading on the road, darkens the kerb and wall bases, corners and under barriers; the sky and the prelit big shapes are left as they are. The real cost is the second geometry pass: writing depth into a second colour target of the scene pass itself (MRT on every scene pipeline) would remove it: +2-3 d. GTAO (horizon-based, steadier) on the same inputs: +2 d | done (~1 d); MRT depth +2-3 d; GTAO +2 d | pre-pass ~0.3-0.5 ms GPU (+~1 ms CPU recording ~1000 draws again), AO ~0.15 ms, composite ~0.1 ms |
| 4. Car clear-coat | Already there in Enhanced CAR REFLECTIONS (§9): Fresnel, the env map, a hard sun glint. What is missing is a broad second lobe (a GGX sun highlight of roughness ~0.3 under the glint) and an orange-peel normal wobble; both are a few lines in `car.frag` behind `CarLook` values, default off. Not prototyped: another branch (carpaint) works on the car paint, to avoid two changes to `car.frag` at once | ~1 d | negligible (car pixels only) |
| 5a. Sun shadows of the cars | Feasible: the car items (kCar, ~10-30 per frame) drawn depth-only from the ENVIRO sun into a 2048^2 orthographic map that follows the camera (~80 m), sampled with 4-9 taps of PCF in `mesh.frag` on the opaque track and darkening only the sunlit prelit colour (a road already in the baked shadow of a building stays as it is). The blob shadows stay as the soft contact under the car. The shadow's colour: the prelit colour times a share from ENVIRO's ambient (~0.55). Self-shadowing of the cars and the track onto the cars (bridges) is a further step (the SH already darkens cars under cover) | 2-3 d | map ~0.1 ms, PCF ~0.1-0.2 ms |
| 5b. Contact shadows | Covered by the ambient occlusion (piece 3) | - | - |
| 5c. Sharper textures | The default is already anisotropic 16x. Left: a small negative LOD bias (-0.5) with RENDER SCALE below 100 %, a sharpening in the UPSCALING path (exists: Sharp, §14) | 0.5 d | none |

**Prototype (environment only, off by default; drawn only).** `B3_SURFACE=1` turns on relief 3, sheen 0.35 and ambient
occlusion 0.8 (radius 1.2 m). `B3_SURFACE_RELIEF`, `_SHEEN`, `_AO`, `_AO_RADIUS`, `_FADE`, `_SKY`, `_GRAZING` set one
each (0 = that piece off); `B3_SURFACE_DEBUG=1` shows the relief factor (red around grey) and the sheen (blue) on the track,
or the AO alone when it is on. `FrameParams::Surface` carries the values (filled in `View::post_frame`, so every race path,
live, offline, photo mode and split screen, gets them), so a settings row later is a few lines. Nothing reaches game memory.
Unset: every field is 0, no extra draw, pass or texture runs and the frame is unchanged (the golden record's 140 shots
PASS with the prototype built in). Not applied: the sky, blended track draws, lines, reflection-texgen (type 2) materials,
the 2D stage, the car env map's faces, the post-fog objects; cars only get the AO.
`B3_SURFACE_ABTIME=N` (diagnostics) redraws each offscreen shot's frame N times in each of four forms (off, relief + sheen,
AO, all), interleaved, and prints the median draw + read-back times.

**Seen in offscreen shots** (US/C2_V2, US/M1_V1, AS/M1_V2 at 1280 x 720, and US/C2_V2 with the HD Textures PS2 pack):
the asphalt gets a crisp grain that moves with the light, the barriers, kerbs and walls their texture's relief, with the
HD pack the road's cracks and the manhole lids read as relief. The AO puts a soft contact shadow under and beside the cars
and along wall and barrier bases; open road and the sky are untouched. The sheen shows as a soft glare on the road looking
towards the sun (US/M1_V1 near the end of the run), nothing with the sun behind the camera. Limits: luminance is not height
everywhere (painted lines and a white sign read as raised; dark joints as grooves, which is mostly right); at relief 4+ the
road looks lumpy, 3 is the prototype's value; the sheen is a dry glare, not wet.

**Measured cost** (software rasteriser in WSL, so a relative share of pixel work, not GPU times; US/C2_V2, 1280 x 720,
MSAA 4, the same frame drawn 9 times per form, interleaved, median draw + read-back): off 161-190 ms; relief + sheen +1.3 to
+7.8 ms (1-4 %); AO +74 to +89 ms (~45 %, most of it the second geometry pass and the full-size composite's depth
reconstruction); all +79 to +95 ms. On a real GPU the relief is a few taps per pixel; the AO's pre-pass is the item to
remove (MRT depth, above) before it ships. A real-window measurement on the owner's GPU is still to do.

### 18.1 Settings rows (night, 2026-11-19)

GRAPHICS > LIGHTING: **SURFACE DETAIL** Off / On (row 372: relief 3, sheen 0.35, lit by the ENVIRO sun by day and by the
moon at night) and **AMBIENT OCCLUSION** Off / On (row 373: 0.8 at 1.2 m), both off by default, the prototype's pieces
unchanged. Any `B3_SURFACE*` variable set wins over both rows (the values above, or the variables' own). At night with
MOON on the relief runs with the moon even with SURFACE DETAIL off (§17.1); off and by day nothing of it runs.

## 19. Import Xbox textures (xboximport, 1.3.0, 2026-11-20)

The Xbox version keeps the PS2 version's files, texture names and UV layouts, and about a quarter of its track textures
are 2x-4x larger, with some extra detail; the car atlases are twice as wide. The launcher's HD Textures page builds a
pack from the player's own Xbox disc image, on their PC: nothing from the Xbox disc ships. **It is a small
improvement**: some building fronts, signs and car paint are somewhat sharper; an HD texture pack looks far better. It is
there for players who asked to use their Xbox disc.

- **Reading the disc** (`b3asset/xdvdfs.h`): XDVDFS, trimmed (xiso) and full dumps; the image is untrusted input (every
  offset checked, each folder read once, caps on the tree). Separate from `discfs`, the game's own disc.
- **The textures** (`b3asset/xboxtex.h`): the D3D texture header and the game's raster tail; DXT1 / DXT3 / DXT5 and P8
  (Morton-swizzled, 256-entry palettes; the cars have one per paint).
- **The pack** (`b3asset/xboximport.h`): each Xbox texture is matched by name to the PS2 raster in the same file and
  written as a DDS named by that raster's HD key (`hd_each_raster`: the enumeration `b3hdkeys` checks with). Only Xbox
  textures larger than the PS2 one, more than 32 texels a side, not the 16x16 water placeholders, and passing a layout
  and colour check against the PS2 picture. The Xbox textures are stored darker, so tracks and the front end get a
  detail transfer: the PS2 colours with the Xbox detail on top (the visible colour spread into transparent texels first,
  the detail only where the Xbox texel is opaque: no dark fringes on cut-outs; an opaque PS2 texture stays opaque, and
  the Xbox alpha detail is used only where the Xbox alpha agrees with the PS2 one: 18 textures on the USA disc take the
  PS2 alpha alone). Cars: the Xbox index image with a palette built from
  each PS2 paint (the PS2 texel kept where the Xbox livery differs), one file per paint. BC1 when opaque, else BC3, the
  top level only. No upscaling: the textures are the Xbox art at its own size.
- **Loading**: `<user data>/xbox-textures/` (with `import.ini`: the image, the date, the counts, the importer version)
  below the mods' packs and the player's own HD pack; XBOX TEXTURES (GRAPHICS, off until an import finishes) and HD
  TEXTURES (and F4) switch it. Nothing loads unless imported, so defaults and the checks are unchanged.
- **Launcher**: Import Xbox textures... (a file dialog), a progress bar with Cancel (the job on its own thread), a
  free-space check, the result, Open and Remove. A file that is not an Xbox image, or another game's disc, is a plain
  message; cancel or an error leaves the earlier import as it was.

On the USA disc: about 1,040 files (590 track and front-end textures, 450 car paints), about 90 MB, in about 10 s.
PAL and other regions' discs are not tested (the names should match; the sizes may not).

## 20. Sun shadows (sunshadow, 1.3.0, 2026-11-20)

GRAPHICS > LIGHTING (docs/pc-settings.md §7.29): **SUN SHADOWS** Off / On (row 390, ini `sun_shadows`), off by default,
drawn only. The cars cast a real shadow on the road from the ENVIRO sun by day and from the moon at night
(`FrameParams::key_light`). The existing CAR SHADOWS row (the game's blob, `blobbyshadow`) stays.

**The frame** (`FrameParams::SunShadow`, filled by `View::post_frame`; `b3gfx` render_scene):

1. **The box** (`View::sun_shadow_box`): one orthographic map along the key light, 60 m across and 80 m deep, around the
   view's car (`Frame::env_car`, else the race car the camera follows) with its centre 10 m ahead along the camera's view
   (in photo mode along the car's heading, so turning the free camera does not move the shadows). The centre is snapped to
   whole texels so the edges do not crawl. No pass when the light is under 5 degrees or has no colour (night with the
   moon off); below 12 degrees the light is raised to 12 (the shadows stay a few metres long at sunset). Split screen: one
   box per view, each around its own car, drawn into the same map just before that view's scene pass.
2. **The caster pass** (before the scene pass): the frame's car draws (racers, traffic, wrecks; not the screen-door
   Time Attack ghost) whose position lies in the box, through `car.vert` (deformation included) and `shaders/shadow.frag`,
   into an R32F map (1024 or 2048 texels) with its own depth target, LESS_OR_EQUAL, a slope-scaled bias. Each texel keeps
   the nearest caster as its light depth + 2 x its **gate** (0..255): the caster's `CarParams::sky_open` mapped through
   smoothstep(0.18, 0.32) (a car in a tunnel, ~0.2, casts nothing, and the shadow fades at the mouths with the car's own
   sky share; a street between tall buildings reads ~0.3 and still casts), times a fading-in traffic car's opacity.
3. **The receivers**: the track draws of the scene pass (opaque, alpha-tested and blended; not the sky, the line meshes,
   reflection-texgen materials, effects, the post-fog stage, the env map or the pre-passes) take mesh.frag's
   `SUN_SHADOW` variant (`mesh_shadow.frag`, compiled from mesh.frag with the define; the default shader is unchanged
   byte for byte). Only faces turned up receive (the position's derivatives: road, verges, kerbs; not walls, not a
   bridge's underside). The lookup (`shaders/sun_shadow.glsl`, `sun_shadow_amount`) is a 3 x 3 box filter weighted
   bilinearly at its edges (4 gathers; ULTRA: 5 x 5, 9 gathers), each texel its caster's gate where the receiver lies more
   than 3 cm below it, fading out 3 to 8 m below it (heights: the distance along the light x its sine of elevation; a car
   on a bridge does not shade the road under the bridge, a car in the air leaves the ground slowly), and the box's outer
   10 % fades.
4. **No double darkening.** The track is prelit: its shadows (buildings, gantries, bridges, tunnels) are baked into the
   vertex colours (seen with `B3_SUN_SHADOW_DEBUG=2`: on EU/C3_V1 the open road's vertex light is 0.61-0.77, a gantry's
   baked shadow 0.25, and the final colours 0.33-0.40 x the lit road). So a receiver ends at min(its colour, the open
   road's x (1 - strength)): with b its vertex light (luminance) and R the route's open-road light, the colour is
   multiplied by mix(1, clamp((1 - strength) R / b, 1 - strength, 1), shadow). Inside a baked shadow nothing changes, at
   its edge it blends, and a receiver brighter than R (bright concrete) takes the open road's factor, never more. R is
   measured per route at load (`road_light_level`): the area-weighted 85th percentile of the vertex light of the
   up-facing opaque track triangles; it ranges from 0.35 (US/M1_V2) to 0.71 (EU/C3_V1) over the 12 routes seen (US/C2_V2
   0.44, US/P2_V2 0.59, AS/C1_V1 0.60, US/C1_V1 0.62), so one fixed level would leave the dark routes' shadows faint
   (0.75 on US/M1_V2: the open road only to 0.81 x) or darken the bright routes' baked shadows. Strength
   0.62 (the open road at 0.38 x, as dark as the baked shadows), scaled by the key light's brightness (an overcast route a
   fainter shadow), before the fog; the sun's specular (the frame alpha the full-screen specular reads, opaque draws) and
   the sheen take the same factor. This is also why nothing but the cars casts and only the ground receives.
5. **The blob**: where a car casts (in the box, by its gate), its blob shadow fades to 30 % (`View::draw_list`), so the
   two do not stack to black.
6. **PROP SHADOWS** (propshadow1, 2026-11-22; row 613 after SUN SHADOWS, ini `prop_shadows`, off by default, no preset
   touches it): with SUN SHADOWS on, the track draws marked `DrawItem::sun_caster` cast too: the props (static and
   dynamic, at their live matrices: a knocked cone's shadow moves) and the streamed units' instances (lamp posts, signs,
   gantries, the elevated railway's girders, barriers), marked once when the track is indexed. A second loop of the
   caster pass draws those whose bounds reach the box (each item's mesh radius round its origin, `DrawItem::sun_radius`:
   a long barrier or gantry whose origin lies outside still casts where it crosses) through mesh.vert's simple path and `shaders/shadow_mesh.frag`
   (cut by the texture's alpha when the draw is alpha-tested or blended: fences, foliage cards), gate 255. The receivers
   and the no-double-darkening rule (4) are unchanged: where the track already bakes a prop's shadow nothing stacks.
   Race US/C2_V2: 198 car caster draws and 70 prop casters in the first frame; the visible change is the instances' shade
   on the road (the elevated railway's girders), lamp posts thin lines. `B3_PROP_SHADOWS=0 / 1` (with a settings file)
   overrides the row.

**Night.** At night the key light is the moon (silver-blue, 10-20 degrees up) and the same receiver term runs in
mesh.frag, before the grade. The planned night headlight pass (brief D) takes the term over at night (a beam fills a
moon shadow): `sun_shadow.glsl` is written to be included there, with the map bound beside the pass's own inputs.

**Presets.** The map follows ANTI-ALIASING (1024 texels below 4x: MEDIUM; 2048 at 4x: HIGH; 2048 with the 5 x 5 kernel at
8x: ULTRA); the PS2 ORIGINAL and LOW presets turn the row off. Off: no target, no pass, no pipeline variant (the golden is
unchanged). Diagnostics: `B3_SUN_SHADOW_DEBUG=1` (false colour: red the shadow, green the box, blue the faces that
receive), `=2` (the vertex light, blended layers blue), `B3_SUN_SHADOW_TRACE=1` (the view car's sky_open per frame),
`B3_SUN_SHADOW_SIZE`, `_FILTER`, `_STRENGTH` (0.62), `_LIT` (a fixed open-road level instead of the route's),
`B3_SUN_SHADOWS` (all with a settings file). The route's level is logged at load ("open road vertex light").

**Measured cost** (RTX 4080 SUPER, the Windows build's native_play.exe offscreen from WSL, `--bench 1920x1080`, the
race_check race on US/C2_V2, 2 interleaved rounds per form; the bench's "GPU draw + read-back" per racing frame, p50 ms):
MSAA 2: off 3.79 / 3.88, MEDIUM (1024, 3 x 3) 4.24 / 3.96; MSAA 4: off 3.62 / 3.65, HIGH (2048, 3 x 3) 4.16 / 4.29; MSAA 8:
off 3.92 / 4.03, ULTRA (2048, 5 x 5) 3.96 / 4.12. So +0.1 to +0.6 ms, within the run-to-run spread of a read-back-bound
bench (fps while racing 153-167 off, 142-155 on); +130 to +190 caster draws per frame (the cars' parts), render-thread CPU
(cmd_scene) +0.1 ms. A real-window measurement (no read-back) on the owner's GPU is still to do.

**Seen in offscreen shots** (1280 x 720, MSAA 4, lavapipe; off / on / debug of the same scripted race): AS/C1_V1 start
(sun 30 degrees from the side) beside the player's car 144 -> 69, beside an opponent 148 -> 87; EU/C3_V1 (25 degrees,
light from ahead) a car-length shadow behind each car; US/C2_V2 under the rail bridge (baked dark) unchanged; US/C1_V1's
coast tunnel: the car's sky_open 1.0 -> 0.40 at the mouth, 0.20 inside (nothing cast), 1.0 out; night (moon at 20
degrees) a long moon shadow, 108 -> 45 beside the car; the split screen shades each view around its own car; photo mode
keeps the box on the car. Measured in US/P2_V2's streets: sky_open 0.28-0.53 in the open between tall buildings (why the
gate opens at 0.32, not higher).

## 21. Orbit camera in races (orbitcam1, 2026-11-21)

In a race the right stick (or the mouse with its right button held) swings the chase camera around the car: look left,
right, behind, a little up or down; 0.75 s after it is let go the view eases back to the game's chase view. Each
split-screen player turns their own view with their own pad. Drawn only, like the garage camera (§16) whose turn about
the car it reuses: only the game's chase cameras, with the player's car ahead (sigcam's chase share), never the
takedown, crash, bumper or in-car views; the look-back button keeps working. On by default, because it only takes a
right stick the race does not read: on the PlayStation layout (and a Custom one that binds it) the right stick is the
game's analog throttle and brake, so it stays the game's there and only the mouse turns the view. Replays carry the
game's pad, not the orbit; while one plays the viewer can look around. SETTINGS > DISPLAY > CAMERA > ORBIT CAMERA.
(orbitcam1 had no collision with the scenery; orbitcam2 below stops at walls.) docs/pc-settings.md §7.32.

orbitcam2 (2026-11-21): a right-stick camera more like Burnout Revenge's. ORBIT STYLE Look (the default): the stick's
direction is where the view looks (down: behind), eased 10 % of the way every 1/60 s, held 0.4 s after the stick is let
go, the field of view up to 20 degrees narrower as the eye goes round to the car's front; Swing keeps the first
version (the stick turns the view while held, and tilts it). Both stop the eye 0.4 m before the walls between it and a
point 1 m above the car, from the drawn track's solid, near-vertical triangles (the renderer's; not the game's own
collision, so a low barrier can still be looked over). The stick stays the game's where it is bound (the PlayStation
layout's throttle and brake). docs/pc-settings.md §7.32.

## 22. Per-race options on SELECT OPTIONS (raceopts, 2026-11-21)

TRAFFIC / LAPS / RIVALS as the game's own rows on SINGLE EVENT's SELECT OPTIONS page (tools/native/race_options.h, the
values and rules in race_choice.h; TIME OF DAY is built there too, held back with night mode, and replaces 17.3's
label). Where the game keeps each value [C the decomp]:

- **The page** (0x01E96F78): Prepare func_00432AB0(page, mode) fills up to 3 rows at +0x28 (0x14 bytes each) and hands
  them with the selection {row, count} at +0x24 to the options component D_01E8961C (func_003BE5A0 -> func_003BE2A0,
  up to 9 rows, centred at 32 a row); Left / Right go to func_00432560, which maps rows through per-mode tables. The
  port copies the rows into a private array (GuestArena::kRaceOptions) with its own after them (kind 3: string lists)
  and takes Left / Right on those; everything else (drawing, highlight, arrows, Up / Down, Confirm) is the game's.
- **Rivals**: the page's +0x6C0 (0..5) goes to CB3MenuChoices 0x01E900B0 +0x18 on Continue; RequestOnePlayerGameMode
  0x30C2C0 gives the stage record 0x0051AD00 +0xA4 AI cars: RACE the rivals, TIME ATTACK 0, ROAD RAGE min(rivals, 3).
  Road Rage's row sets the page's own value (1..5); for 4 / 5 the port raises the record's +0xA4 after 0x30C2C0's clamp
  and copies the 4th / 5th rival's car id (+0x18 u64) and colour (+0x78) from the choices' +0x80 / +0xC0, the slots
  RACE uses. The Road Rage logic walks the AI count with per-car tables of 6 (OnWorldPrepared 0x38C3F0, the respawns
  0x38CBB0: rivals released every 5 s), the stage data has 6 opponent slots, the world 5 AI cars: 4 / 5 stay inside the
  game's 6-car frame (5 rivals: 6 race cars in the capture, the results page as the game's).
- **Laps**: each car's CB3RaceCar::Prepare (0x152C1C) asks the game mode (vtable +0x20; CB3OnePlayerStage 0x134930 ->
  CB3GameData::GetNumLaps 0x14E860: the stage header's +0x3B8 in the track's GAMEDATA.BGD; Single Event's stages
  "OffSgRcF" / "OffBrLpF" / "OffRRgeF"). The AI and HUD ask the same call, so the port answers it. 1..8: the lap
  index wraps after 8 (IncrementLapCount 0x15A1F0), so 9 or more would never finish. Time Attack sets 0 (no end);
  Road Rage is timed by its takedowns: no LAPS row for either.
- **Traffic**: per stage in GAMEDATA.BGD (the trigger data); each valve lane has 7 spawn rates (vehicles a minute per
  category, CB3TrafficValve +0x10) read by PrepareGenerationTimes 0x268270, UpdateGeneration 0x267800 (60 / rate ±
  30 %) and FillLaneSection 0x266AE0. The port scales them for each of those calls and puts them back (Off 0, Light
  0.5, Heavy 2); the game's own caps stay (254 vehicles, two queued per lane). US/C2_V2, the same 30 s: 6 vehicles
  (Off: the race cars), 29-32 (Normal), 49-66 (Heavy).
- **Records**: the post-race setup 0x42F0F0 calls 0x3D1090 (CB3Records 0x01EA7810) with the record kinds, and 0x136F30
  decides whether the race counts for the profile (totals, unlocks, signature takedowns; also asked by
  CB3StageLogic::OnTakedown). A race with options not the game's gets no record kinds and "does not count". The
  renderer's ghosts and best times go to an options-... folder (src/b3replay/records.h).
- Only while the game's current mode is the one-player stage built for Single Event (+0x1B8 0x0051B920; World Tour's
  is 0x0051B928) in the page's mode; the options reset with the page's own (SELECT MODE) and on the main menu.
- Replays: a recording made with the rows says so in its header (option race_opts = 1) and holds the choice from its
  first VBlank; one without it (made before the rows, or with B3NATIVE_RACE_OPTS=0) plays on the game's own page, so
  its pads land on the same rows. The words follow the game's text language (EN, FR, GE, IT, SP, DU, SW; LAPS, RIVALS
  and TRAFFIC are the game's own words where its table has them).

## 23. ELIMINATOR in Single Event (elim1, 2026-11-21)

A fifth SINGLE EVENT mode, on by default (its EXTRAS row, §45) (tools/native/single_eliminator.h; the row rules in eliminator_rows.h, tested by
tests/test_eliminator_rows.cpp; `B3NATIVE_ELIMINATOR=0` gives retail's four modes). What the game has [C the decomp]:

- **SELECT MODE** is a CB3GenericVSelectPage (0x01E94E4C, room for 10 rows; built by func_003C9210, Prepare 0x3C98D0)
  with RACE / TIME ATTACK / ROAD RAGE / CRASH; the row index is CB3MenuChoices' mode (0x01E900B0 +8). ELIMINATOR (string
  273, World Tour's event name, in every language's table) is row 4; the game sees the ELIMINATOR row as race (mode 0)
  and CRASH's row as 3 (the Single Event state's Action 0x3C9340). The page has no per-mode help line.
- **The race**: RequestOnePlayerGameMode 0x30C2C0 builds the stage record (0x0051AD00) for the race; the port puts World
  Tour's CB3OfflineLapEliminatorLogic (0x0051B2F0, vtable 0x4DECE0) in it. That logic is the whole mode: each car's laps
  are the cars less one (0x38BE40), the one car with the fewest laps is out when the others have started the next lap
  (0x38BD30 -> 0x38BB70), its HUD (LAP n/5) and results page (ELIMINATOR RESULTS, func_0042FE90) come from its vtable.
  The stage stays "OffSgRcF": the ELF names "OffLpElF" (CB3NormalGameModes), but no track's GAMEDATA.BGD has it.
  RIVALS is the race's row (0..5); 0 is raised to 1 on SELECT OPTIONS' Continue (CB3MenuChoices +0x18, before the game
  picks the rivals' cars for the count: raised later, the added slot had no car).
- **Records**: the results page asks CB3Records::Update 0x3D1090 for kinds 13 (World Tour's Eliminators write them into
  the track's tables, the ones RACE uses); there is no Eliminator table, so a Single Event Eliminator writes none (the
  kinds given as 0, and the records left as after a race with no new record, so no RECORD tags).
- **Profile**: counted, an Eliminator's results add Burnout points (World Tour's event unlock thresholds, func_00138530),
  race / slam / per-car totals and the unlocks' evaluation (a card diff: Burnout points +230, slams +1, race events +1,
  the car's events +1; no medal or unlock byte). func_00136F30 (does the race count for the profile; the game's own says
  no for some modes) answers no for it, as raceopts does for its races: the profile in RAM is unchanged and the game
  does not autosave.
- **The port's records**: the game sees a race, so native_play gives the renderer bit 14 of raceopts' 'HTD1' options
  word (records.h; EliminatorRows::record_word: with raceopts' own word when both, else 0xC002): its ghosts and best
  times go to "options-eliminator".
- **The choice** ends on the main menu (its Prepare 0x318340) and on SELECT MODE's Back, as well as on another row.
- **TIMED ELIMINATION**: raceopts' row kind 5 on ELIMINATOR's SELECT OPTIONS (in LAPS' place; Off / On, translated; its strings in the
  arena after the four kinds' layout), the choice code " e1" (older codes parse as Off) and record word bit 9
  (options-eliminator-timed). On: the Eliminator logic's Update 0x38BEB0 runs a timer on the race time (the game timer
  gWorld 0x522660 +0x20 less the player's CB3Score +0x280, from its race state +0x27C 2) and every 24 s puts out the car
  with the highest race position (CB3Score +0x0, s16, CB3RacePosition::Update 0x154770) among those still in (+0x1998, the
  score's finish +0x8 clear), through the call the lap rule uses (0x38BB70); the lap rule's own calls (from 0x38BD30)
  are skipped; the cars through the logic's pointer array 0x64D388. The game ends the race as before (one car left).
  The countdown: native_play's 'HEL1' record (0x41112400: whole seconds, published on change) and the renderer's
  ElimCountdown (racetime.h): in the lettering of the race's own captions ("6th PLACE ELIMINATED!": the HUD's primary
  message func_003D56D0 -> CB3StylisedText2dObject 0x3D0710, layout 0x3CDB80, render 0x3CFE40 / ramp 0x3CE290): the
  graphics manager's font, 1.5 wide, sheared 0.4, the outline ring and drop shadow, the gold ramp of style 1 (0x004CA500..,
  carried in the record), a small lead-in "ELIMINATION IN" and big seconds; in the last 5 s the seconds in the warning
  captions' style 4 (the HUD's +0x50 / +0x60, as "CRASHED!"); under the HUD's top row, above where the captions show;
  only while the player is still in and not crashing (+0x18EA: the game hides its HUD then).
  An Eliminator keeps the game's lap rule (laps = the cars less one) in both modes: with the timer, the eliminations
  (5 x 24 s) end the race long before the laps would, so no LAPS row is offered (a lap count per elimination would be a
  rule of the port's own, not the game's).
- **Titles**: SELECT OPTIONS' and SELECT CAR's red title (heading table DATA/HEADxx.BIN: 11 strings, none for
  Eliminator) reads string 273 while those headings are prepared for the ELIMINATOR row.
- Replays: the mode follows from the menu inputs, so a replay plays it back (replay_check SCENARIOS=eliminator). Every
  recording has the option `single_eliminator` (1: the row there); a replay without it (1.3.0 and before) plays with
  retail's four rows, so its Downs on SELECT MODE land where they did.

## 24. Heavyweights in Single Event races (heavyseries, 2026-11-21)

SELECT CAR offers the HEAVYWEIGHTS series in SINGLE EVENT's RACE, TIME ATTACK, ROAD RAGE and ELIMINATOR as well as CRASH
(tools/native/heavy_race.h; the rules in heavy_series.h, tests/test_heavy_series.cpp). Where the game decides [C the
decomp]:

- **The series**: SELECT CAR's series are the carousel's car classes (CB3CarouselComponent 0x01E8EC30, +0x258), 0..6 with
  Heavyweight last (GetClassFromID 0x251CB0; its name is string 0xBF4, GetClassString 0x42DA70: every language's own
  text). Left / Right stop at a bound that the carousel bytes +0x264 / +0x265 set. The car select state 0x310200 passes
  them to the page's Prepare 0x318F50 -> 0x42CFA0. +0x264 is func_0030CEC0 (true only for the crash modes of each top
  menu), which has no other caller. The port answers true there too for Single Event modes 0..2 when the series is on
  (B3NATIVE_HEAVY_SERIES=0: off; replays: their "heavy_series" entry) and the profile has a heavyweight unlocked
  (read from guest RAM as the carousel 0x42B7C0 and IsCarUnlocked 0x13A660 read it: the vehicle list's driveable class 6
  entries, the race vehicle table D_004CCF80 and its unlocked bytes D_0051BFA0; no guest call, since one costs EE time
  and would move every later VBlank). Locked heavyweights show
  locked, as in every series. 0x42CFA0 moves a remembered heavyweight back to class 0 for any event whose bound
  excludes it, so World Tour and Multiplayer never start on one. The port keeps that reset for the Single Event modes
  too (a wrapper on 0x42CFA0 after its own answer: class 0, row 0), so SELECT CAR always opens on the game's first
  series, as retail. HEAVYWEIGHTS is one Right past SPECIAL. Eliminator in Single Event (§23) runs as RACE (mode 0)
  and gets the series too.
- **The race**: the car comes with its own handling, deformation, chase camera (HEVYCARn.cfg) and boost bar
  (GetVehicleBoostBarSize). Single Event's Confirm picks the rivals from the player's class (0x30B9D0 -> 0x30B6E0: the
  race vehicle list's 67 cars, which include the 11 heavyweights; unlocked first, each at most 3 times), so
  heavyweights race heavyweights. The AI, HUD and results page are the game's.
- **Records**: in a heavyweight race (the Single Event stage record 0x0051AD00 / +0x1B8 0x0051B920, modes 0..2, player
  0's car of class 6) the post-race CB3Records::Update 0x3D1090 gets no record kinds (a1; a2 only asks for a second
  update). Kinds 0 return at once with the last update's type set to 3, so the port puts back what a race that set no
  record leaves (D_004E2EE0 type 0, D_004E2EE8 this stage's track), as the Eliminator does, and the results page's
  RECORD tags read this track's tables and show none. 0x136F30 (whether the race counts for the profile's totals,
  unlocks and signature takedowns) answers no. The update goes through the shared records_guard.h (records_without_kinds), as raceopts' (§22) and the
  Eliminator's (§23) do, and the guards chain. The renderer gets the race through the same record word: bit 13
  (heavy_series.h heavy_record_word), so the ghosts and best times go to "options-heavyweights", one folder with the
  other options when they apply (e.g. "options-heavyweights-traffic-off", "options-eliminator-heavyweights").
- **The golden record** (1.3.0's, made without the series): the Single Event scenarios that reach SELECT CAR (race,
  timeattack, roadrage, takedown, pause, photo, music, movie, carselect) keep their shots, DMA, SIF and audio logs and
  their timing. Two things differ in each, both the series itself: the RAM hash of MB 30 from VBlank 20001 on (the
  carousel's +0x264 byte is 1), and the trace window v19900-19999, where two kernel calls (0x10BE60 / 0x10BE40 at
  v19926) see that byte left in a3. With B3NATIVE_HEAVY_SERIES=0 the trace is the record's.

## 25. Mirror mode and the Crashbreaker in every mode (gameopts, 2026-11-21)

Two opt-in GAMEPLAY options (PC SETTINGS > GAME > GAMEPLAY, section 41, rows 430 / 431; ini `mirror_mode`,
`crashbreaker_all_modes`), both off by default (tools/native/game_options.h).

**Which races.** SINGLE EVENT (all four modes) and the two-player races only: the game's current mode is the one-player
stage record 0x0051AD00 with +0x1B8 = 0x0051B920 (World Tour's carries 0x0051B928, Crash party's 0x0051B988), or the
two-player one, 0x0051AEC0 [C RequestOnePlayerGameMode 0x30C2C0, RequestTwoPlayerGameMode 0x30BFF0,
RequestCrashPartyGameMode 0x30BE20]. World Tour stays the game's: a tour event played mirrored or with the breaker could
not count for the tour. The options are taken when such a race becomes the current mode and kept until it is not (a
restart keeps them; a change on the page applies from the next race).

**MIRROR MODE: what retail has.** The input half of a mirror mode nobody finished: CB3GraphicsManagerBase +0xC (0x00665ECC;
the Revenge prototype's stabs name the field `mbMirrorMode`) is cleared by its Construct (0x19EFA4) and never set; its only
readers are CB3PlayerCarPhysics::GetDrivingControls (0x1BD020) and the network car's (0x268790), which negate the steering
when it is set. No renderer reads it, and GetCrashingDrivingControls (Aftertouch) does not test it: setting it alone only
swaps the steering.

**MIRROR MODE: ours.** The game's state is not mirrored, only shown so: (1) CB3ControllerMapping::GetSteering (0x12F850, the
stick's steering for driving, Aftertouch and the wheel's force feedback) answers negated, so the screen's left steers
left; the game's replay system records the controls after it; (2) the renderer gets the race's options in the 'HTD1'
host record (the race word's bits 9..10; bits 12..13 the options that keep the race apart) and sets
FrameParams::mirror_x; b3gfx then draws the 3D picture (and the depth-tested 2D) apart from the 2D stage as RENDER SCALE
does, blits it reversed left to right (SDL_BlitGPUTexture, SDL_FLIP_HORIZONTAL) and composites it before the HUD and menus,
which stay readable. A split screen flips each view about its own middle. (3) The world-anchored HUD markers (the rank
over a rival, a points value over a car) are placed at their mirrored points (clip x negated), the marker itself drawn as
it is. (4) Photo mode's horizontal controls (look, move sideways, roll; keys, mouse and pad) act reversed in the world over
a mirrored race (PhotoMode::set_mirror), so they stay right on screen; any other free camera (orbitcam) does the same from
Frame::mirror. The writing on signs and cars reads backwards (accepted). The mirrored copy (RGBA16F, the view's size) is
counted in the supersampling budget (render_scale.h scaled_bytes_per_pixel); at 100 % the mirror takes RENDER SCALE's
separate target set too, kept while mirrored frames are drawn (a settings edit no longer frees and remakes it) and freed
once they end. Off: no blit, no extra targets, the frame as before. Not done: the sound's stereo is not swapped.

Steering checked by the game's state (race_check SCENARIO=steer, the RAM hashed every 200 VBlanks through and after a
300-VBlank steer, STEER_DEV key / dpad / stick: the keyboard's arrows press the D-pad and the stick together, the scripted
pad one at a time): mirrored + Right leaves the car, the rivals and the traffic (MB 6-20, 28-31) exactly as plain + Left;
plain Left and Right differ in 20 MBs. What still differs is the input manager's raw state (MB 4: the D-pad pressures at
0x004F5E24) and, with the D-pad or the stick alone, EE time: holding the other button (or a positive stick, where
GetSteering skips an instruction) costs a few cycles more or less, so CGtHiResSystemTimer's 0x004E2E40, its copy at
0x0051BA58 and RW Audio's voice timing (0x004E2B30, the sound manager's buffers in MB 30) move with it. No other reader
of the steering.

**CRASHBREAKER IN EVERY MODE.** The gate is CB3Game::IsCrashMode (0x131E40). In Crash mode CB3Score::Update (0x15B000) arms
the breaker (CB3PlayerCarPhysics +0x4586, via 0x1BAD00: the flag, a score event, the "ready" sound) once the junction's
vehicle count is reached, and GetCrashingDrivingControls (0x1BB6E0) fires it on the CRASHBREAKER button (the controls'
bit 0, 0x12F610: R2): DoCrashBreaker (vtable +0x28, 0x1BAAF0: air rams, camera event 6, the blast 0x1B2BA0 on the
traffic in its radius), +0x3534 the time, the flag cleared (once a crash). Ours, outside Crash mode: StartCrashing
(0x1BB250, the player cars' crash start) arms it (the flag and the ready sound, no score event), and after the game's
GetCrashingDrivingControls the same test fires it. Every crash of a race can end in one. What the race then shows is the
game's own: its crash camera, the flash and the fireball, the crash caption ("Into Taxi + Exploded"). Unlike the Crash
mode HUD there is no counter (Burnout 3's breaker is not a boost-bar meter: that is Revenge's), and the blast wrecks
traffic only: rival race cars are not in its list, so it takes nobody down (not changed here). **CRASHBREAKER AFTER** (row 432, under CRASHBREAKER IN EVERY MODE and only with it on; ini `crashbreaker_cars`, 0..3,
default 1; the options word's bits 2..3, latched and in the replay's "game_options" with the rest): how many vehicles the
player's crash must hit before the breaker is ready. Crash mode's own count (CrashBreakerVehiclesRemaining 0x3CC910: the
junction's number less the vehicles damaged in CB3CrashScore's row) is kept in Crash mode only: every writer of that row
(HandleCrashTrafficCar 0x3CD430 via 0x1B41D0, HandleCrashRaceCar 0x3CD4D0, 0x3CD380 from CollideAndRespondPair) is behind
IsCrashMode. The race's count is the crash caption's: the player car's CB3CrashCombo (car +0x2470; the physics' +0x13F4 is
the car) lists the distinct vehicles the crashing car hits (CB3CrashCombo::UpdateCollidingPairs 0x363F10, every frame, any
mode; reset at the crash's start by ResetData 0x3640C0) and counts them at car +0x27A0. The vehicle the player crashes into
counts (the caption's "Into Taxi" is 1), walls and props do not. With 0 the breaker is armed when the crash starts; else
after the game's GetCrashingDrivingControls once the count reaches the setting (the ready sound then), once a crash. In
the scripted race (US/C2_V2, three crashes) 0 armed all three, 1 the two that hit a vehicle (the third was a wall), 3
none (each crash hit one). Default 1: a crash into traffic is ready at once, a wall crash once it tumbles into a vehicle;
races have too little traffic for Crash mode's numbers. `B3_GAMEOPT_CRASH_TRACE=1` logs the count as it rises.

After the blast the event goes on as after any crash: in TIME ATTACK and ROAD RAGE the car is
back on the road at the same moment as without the blast, with the same timer, score and takedowns (race_check
SCENARIO=crashbreaker CB_MODE=timeattack|roadrage, CB_PLAIN=1 the same drive without it). Boost held through a crash
is Impact Time's slow motion and keeps the crash going, blast or not (the game's).

**Records.** A race with MIRROR MODE, or with the breaker outside Crash mode, keeps its results apart as raceopts /
heavyseries do: the post-race records call (CB3Records::Update 0x3D1090) gets no record kinds (a1; a2 only gates a second
table) and its last update's type and track are set as a race with no record leaves them, so the results page's RECORD tags
read this race's own tables and show none (as single_eliminator.h does); the profile's "this race counts" test
(0x136F30) answers no (no records, profile totals or unlocks); the call goes through records_guard.h (shared with raceopts,
elim1 and heavyseries; for a mirrored Crash event it points the tags at the junction's tables). Its ghosts and best times
use the one records word (bit 11 MIRROR MODE, bit 12 the Crashbreaker; records.h options_folder): `options-mirror/`,
`options-crashbreaker/`, one folder with the other races' options (a mirrored heavyweight race with TRAFFIC Off:
`options-heavyweights-mirror-traffic-off/`). Replays record the options ("game_options", the word, from the first VBlank
one is on) and play them back. `$B3NATIVE_GAME_OPTIONS` (the word, 0..15) overrides the setting; the golden's
baseline.sh pins 0. The orbit camera's horizontal input is reversed in a mirrored frame as photo mode's is.

**Checks.** test_settings (gameplay_rows, the translations), test_game_options (stages, latch, record word, replay entry,
folders); race_check SCENARIO=mirror (the weave mirrored; the counts line "steering negated") and SCENARIO=crashbreaker
(the takedown drive with E tapped: three crashes, three breakers on US/C2_V2); replay_check of both. Records: the
crashresults tour's crash sets a new record plain (kinds 16, the "Record" tag on CRASH $, the profile autosaved) and none
mirrored (kinds 16 not written, no tag, no autosave: the card unchanged); $B3_GAMEOPT_RECORDS_TRACE=1 logs each update.

## 26. The title's PRESS START at once (titlefast, 2026-11-21)

After the intro movies (and whenever the title comes back) the title's "Press START button to continue" used to show
about a second after the page was up, and Start was ignored until then (tools/native/title_fast.h). [C the decomp]:
the title page D_01E97E1C (Prepare 0x438910 stores the game clock D_004F50A0 at +0x14) takes Start in its Update
0x438840 only when the clock is past that stamp + 2.0 s; the title component D_01E8D550 (CB3TitleScreenComponent,
Prepare 0x343A50: the clock at +0x2C) draws the prompt with alpha 0 until its stamp + 2.0 s (Update 0x343800), then
blinks it. The page's lead-in takes 0.93 s, so the page took input at VBlank 4680 of the boot and the prompt and Start
came at 4746: 66 VBlanks (1.1 s) later. Now both Updates see their stamps two seconds further back for the call (put
back after it) while the page takes input: the prompt shows and Start is taken from VBlank 4680. The lead-in, the
90 s attract and the blink's phase are the game's. The game drawing the prompt in those 66 frames costs EE time (the
SIF log's timestamps from VBlank 4679 on are ~133 cycles later), so every later VBlank's work moves a little: the
golden record (made with the wait) differs from the title on in every scenario (with the feature off it is the
record's). A re-record with the feature on is the owner's call; until then a golden run would pin
B3NATIVE_TITLE_FAST=0.

On by default (B3NATIVE_TITLE_FAST=0: the game's wait). Not part of FAST LOADING: that setting changes no VBlank of the
game, and this one does once Start is pressed early. A replay records "title_fast" (1); a recording without it (made
before) plays with the game's wait, so its Start lands as recorded. race_check's TITLE_START_AT=<VBlank> moves the
title's Start (4690: taken 1.133 s after the Prepare; ignored with B3NATIVE_TITLE_FAST=0).

## 27. Camera roll and the finish camera (betacam, 2026-11-21)

**CAMERA ROLL** (Off / Subtle / Strong, default Off; SETTINGS > DISPLAY > CAMERA, after ORBIT STYLE). The race chase
camera leans into corners, drawn only (src/b3replay/orbitcam.h `ChaseRoll`). Per tick, from the game's own camera of
the view, in game time (the game's timer, so the pause holds it): the turn rate is the change of its at row projected
on its right row; the roll eases (1 / s) towards -rate x gain degrees, at most a limit (Subtle: 10 degrees per rad/s,
at most 4; Strong: 20 and 8); a sideways change over 0.1 in one tick (a camera cut) levels it at once. It is kept in
`Tick::race_roll`, interpolated, and applied to the camera the renderer draws (its right and up rows turned about its at
row) after the drawn FOV, distance and orbit, times the chase share (sigcam's `Camera::chase`, as CAMERA DISTANCE), only
with the player's car ahead within reach (as the orbit: not the bumper or in-car views). The game's camera is never
written. The rival markers the game projected with its own camera follow the rolled view where the renderer
re-projects them (hud4); where it draws them at the game's places (the split screen's top / bottom) they shrink away
while the view is rolled, as for the orbit. Check: `race_check.sh SCENARIO=camroll` (US/C2_V2's bends: about +7 and
-5 degrees; `ROLL_INI="camera_roll = 0"`: none); tests `test_orbitcam` (`roll_leans_into_corners`,
`roll_turns_the_drawn_camera`, `roll_per_tick`), `test_settings` (`camera_roll_row`); `B3_CAMERA_ROLL=0|1|2` overrides
the row (checks).

**FINISH CAMERA** (Game / Set pieces, default Game; SETTINGS > DISPLAY > CAMERA, after CAMERA ROLL). From the finish
line on, the race view cycles through 12 set pieces of 2-4 camera angles round the line, 5 s each, round again after
the 12th, instead of the game's single trackside shot; drawn only (src/b3replay/finishcam.h). The shots' camera offsets
come from the disc's DATA/VDB.XML: 36 vector values under keys no retail code registers (`FinishShots::kShotKeys`, our
list), read at run time from the player's own file; another disc without them keeps the game's shot. The rest is ours:
a set piece's shots share its 5 s evenly; each shot is a still camera at its offset from where the car crossed the line
(in the car's frame at that moment, levelled), aimed at the car while it is within 100 m of the line, then eased back
(4 / s) to the line, where the rivals come through, zooming in past 12 m (to 0.3 of the view at most). The cameras stay
round the line because the PS2 keeps the track streamed (textured) only round its own finish shot: after the finish the
game drives the car on at 50-80 m/s for a kilometre and more. When: view 0's camera manager is in the state the game
enters at the finish (its CGtFSM's current state +0x4, that state's vtable +0xC = 0x4E01F0, whose Action is
`func_00437DC0`: the trackside "tripod" behaviour, type 1, and `SetActiveBehaviour(8)`; captured as `Tick::cam_state`)
and the race's score has finished (`Tick::score.finish` 1 or 4: not an out, 2 / 3, as a World Tour ELIMINATOR's
elimination, which enters the same state; not a Road Rage's end, by its logic's vtable 0x4DEE00 / 0x4DEAA0), in one view (not the split screen), from the first such
tick, in the game's time (the pause holds it); any other camera state (the game's replays, crash and takedown cameras,
the results' next race) ends it. Checked with `B3_FINISHCAM_LOG=1`: a race's intro, Time Attack's lap ends, the Crash
event and the idle title never enter the finish state; a World Tour ELIMINATOR's elimination enters it with an out
(finish 2), so the set pieces do not start there. Photo mode cannot be opened while they run (the results page does
not pause the game, and photo mode opens from the pause), and draws from its own camera. The game's finish state, its results and their
timing are untouched; the frames are drawn from this camera (`Tick::finish_cam`, interpolated within a shot, cut between
shots), and the rival markers the game projected with its own camera are not drawn then. No collision with the
scenery: an offset to the side of a narrow street can put a camera in a wall for a moment. Checks: `race_check.sh
SCENARIO=finishcam` (a one-lap race to the line); tests `test_finishcam`, `test_settings` (`finish_camera_row`);
`B3_FINISH_CAMERA=0|1` overrides the row, `B3_FINISHCAM_LOG=1` logs the camera manager's state as it changes.

## 28. TRANSMISSION Manual (gearbox, 2026-11-22)

The game's own manual gearbox, switched on by the port for a Single Event race (tools/native/gearbox.h). What retail
has [C the decomp]:

- CB3RaceCarPhysics::GetDrivingControls 0x1C0F00: with the transmission's manual flag (physics +0x14B8 = CB3Transmission
  +0x70) the driving buttons' bit 1 shifts up (latch +0x14C0, up to the top gear +0x14CC) and bit 2 down (latch +0x14C4,
  not below 1st), each with the 0.35 s clutch (+0x14A4 / +0x14A0); first and reverse come from neutral by the pedals.
  The transmission's Update func_001C6F90 then shifts nothing itself, uses the manual rev limit and drops to neutral at
  low revs off the throttle. CB3Transmission::Prepare 0x1C7880 never sets the flag: retail never enables it.
- The bits come from CB3PlayerCarPhysics::GetDrivingControls 0x1BD020: bit 1 from func_0012F610 (the DualShock's R2,
  control 13 = pressure index 0x1F), bit 2 from func_0012F4E0 (L2, control 11 = 0x1E, just pressed); the pressure indices
  0x14..0x1F are Right, Left, Up, Down, Triangle, Circle, Cross, Square, L1, R1, L2, R2 (accelerate control 2 = 0x1A =
  Cross, brake 1 = 0x1B = Square). The game's replay records the bits (CB3ReplaySystem::NextFrame).
- The gear on a reset is the game's GetDesiredGear (func_001C6F30: the race's start placement func_001BED00 and
  ResetOnTrack): a manual race starts in the gear the game picks for the car.

The port: raceopts' row kind 6 TRANSMISSION (Automatic / Manual, translated) on SELECT OPTIONS for RACE, TIME ATTACK,
ROAD RAGE and ELIMINATOR; the choice code " m1" and record word bit 10 (options-...-manual; no game records, not counted,
as the other race options). While the race on now is manual: the player's flag set as CB3Transmission::Prepare runs (the
player's car: a CB3PlayerCarPhysics, vtable 0x4DDF00; before the start, whose countdown already runs the transmission)
and cleared at a later race's preparation; the port actions SHIFT UP / SHIFT DOWN (U / Y, the right / left paddle: no
layout binds the paddles) queued on the VBlank grid (a replay's "shift" events), taken only while the player's controls
(0x1BD020) ran since the last VBlank (a press in a pause, the countdown or on a results page is dropped); in the port's
0x1BD020 wrapper, for the player's controller mapping, the game's gear readers answer from them alone (1.0 for one press,
else 0), so the game's latch, clutch and replay take them and the game's own R2 / L2 (Xbox B / View, PlayStation R3 / L3:
the Crashbreaker and the music skip) do not shift; any other call (the crash's controls 0x1BB6E0) is the game's. The
gear in the 'HGB1' host
record for the HUD's GEAR indicator (racetime.h GearIndicator, drawn as CB3HUDPos draws POS, above the speedometer; the
RPM meter of hudmods to sit beside it). B3NATIVE_GEARBOX=0: none of it.

## 29. RPM meter, boost bar shake, takedown streak pitch (hudmods, 2026-11-21)

Three small opt-in additions. What the game has that they build on [C the decomp]:

- **The engine revs**: the speedometer CB3HUDSpeedo (_gSpeedoComponentP1 / P2, 0x0066AE10 / 0x0066BFB0) shows the speed
  of the car at its +0x2C (CB3HUDSpeedo::Update 0x1A3B30). That car's transmission is at +0x1448 (func_001C6F90): +0x54
  the engine revs (in tenths of the rev figures: the function scales the limits by D_004E0D80 before comparing), +0x38
  the rev limit in rpm (tested against 6000), +0x3C the low rev figure, +0x80 the gear, +0x70 set for no automatic
  shifts. The game draws no rev counter. SPEED UNITS (mph / km/h) is already the port's (§7.16 of docs/pc-settings.md).
- **Boosting**: each boost bar CB3HUDBoostBar (_gBoostBarComponentP1 / P2, 0x0066A610 / 0x0066B7B0) reads the CB3Score at
  its +0x68C (the car's +0x10C0); that score's CB3Burn (+0xCC) has the boosting flag at +0x52 (BarUpdate 0x1A4900).
- **The takedown sound**: func_00285C00 (the HUD popups' sounds) plays a takedown (class 4) at one of five pitch steps
  (1, 2^(2/12), 2^(3/12), 2^(4/12), 2^(6/12), D_004E092C..0x938) by the takedown's kind, and in World Tour's
  takedown-target events by how far the player's takedowns are past the event's target (then, 5 and more past, the
  bigger takedown sound, func_00285770). Its caller CB3HUDPrimaryMessage::UpdateMessage (0x3D8280) holds the player's
  CB3Score at +0x24, whose +0x68 is that player's takedown count; class 3 ids 0x7F / 0x80 are the player's crashes.

What the port does:

- **RPM METER** (DISPLAY > RACE HUD, ini `rpm_meter`, row id 450; src/b3replay/hudmods.h): 20 thin slanted segments
  under each view's speedometer plate, the plate's blue, the last three red near the limiter. The values come with
  each HUD frame (the HUD capture's GAUGE event, read where the speedometer reads its car: src/gameview/hud_capture.cpp
  observe_gauge), and the bar is added to that frame as draws of the speedometer's own object, so the wide layout,
  HUD SIZE and the split screen place it with the speedometer. Checked on a race: 1st gear against a wall at full
  throttle reads 7729 against a limit of 7600 (boost raises the limit 10 %): full.
- **BOOST BAR SHAKE** (the same section, ini `boost_shake`, row id 451): while the bar's car boosts, every draw of that
  bar moves by up to 2 units across and 3 up or down, a new place every 3 HUD frames (a hash of the frame's serial: a
  replay shakes the same).
- **TAKEDOWN STREAK PITCH** (AUDIO > RACE SOUNDS, ini `takedown_pitch`, row id 452; tools/native/takedown_pitch.h): a
  port hook on func_00285C00 follows each player's takedown count (the calling message's score) at each takedown and
  crash popup; outside World Tour, when that player's streak since their last crash passes 10 it hands the game the
  takedown id that makes it play World Tour's step (the game's own sounds and pitches). Split screen: each player has
  their own streak. World Tour keeps the game's own rule. Replays record the setting.

All three are off by default. The first two are drawn only; the third changes only which of the game's takedown
sounds play. With them off the frames, the sounds and the game are the game's (the golden).

## 30. More race options, crash camera, driver labels, deformation, chrome (options3, 2026-11-21)

More rows on SELECT OPTIONS (§22's mechanism: tools/native/race_options.h, the values in race_choice.h) and five PC
SETTINGS rows. Gameplay-changing ones are off by default and keep their races' records apart (no record kinds at
0x3D1090, 0x136F30 "does not count", the renderer's ghosts and best times in an options-... folder); the drawn-only ones
never touch the game. Every owner of the records word, the HTD1 fields and the arena regions is listed in
tests/test_bit_owners.cpp.

**SELECT OPTIONS rows** [C the decomp]:

- **TRAFFIC** has six values: Off, Light, Normal (the game's), Busy, Heavy, Extreme: the valve lanes' spawn rates x0,
  x0.5, x1, x1.5, x2, x3 (the game's 60 s spawn interval as 40 / 30 / 20 s for the three busy ones). Stored values keep
  §22's (3 is Heavy x2); Busy and Extreme take the records word's bit 16 (traffic's third bit).
- **TRAFFIC PATTERN** (RACE, TIME ATTACK, ROAD RAGE): Fixed (the game's) or Varied. CB3TrafficSystem::ResetMemberVariables
  0x1632C0 seeds the traffic's random numbers with two fixed words (system +0x36348 / +0x3634C = 0x0055D788 / 0x0055D78C),
  so every race has the same traffic; Varied writes a seed from the race start's VBlank after it (splitmix64).
- **START** (RACE, TIME ATTACK, ROAD RAGE): Rolling (the game's) or Standing. The stage logics' vtable slot +0x88
  (func_00135170) answers 1, a rolling start; Standing answers 0. The standing intro waits for a fresh Continue press.
  The game's own boost start (booststart, tools/native/boost_start.h) [C CB3RaceCarPhysics::GetDrivingControls 0x1C0F00,
  while a human car is held at the start]: the brake past half (+0x1528, kept while the accelerator does not drop),
  between 1.0 and 0.5 s before the start that and the wheels spinning (+0x1529, UpdateWheelStates: a wheel's spin x
  radius past 2 m/s) arm it (+0x152A); in the last 0.5 s the accelerator lifted below 0.2 (+0x152B), then past 0.8:
  CB3Score::HandleBoostStart 0x156E20 (the boost and the BOOST START award, 50) and +0x1446 (full throttle for 3 s).
  Retail never starts standing, and the held car's wheels never turn (their spin first changes at the release), so it
  could never arm: on a standing start +0x1529 is the accelerator past half before each GetDrivingControls of a held
  human car. The award's sign: the game draws BOOST START! with its generic event icon (_kaEventMap: event 121 kind 1 ->
  message type 0 -> D_004CA6F0's texture 0, a car-collision sign, as INSTANT BOOST's type 8 too); on a standing start
  the port draws it with texture 7 of mapIconTextures__15CB3HUDComponent, a lightning bolt in the same yellow diamond
  that the game ships but never draws (the type's layout, colours and sound kept; drawn only; B3NATIVE_BOOST_ICON=<n>
  checks another). Checked: brake and accelerator from V22020, lift at V22305, floor at V22320 (the race on at V22331): BOOST
  START! 50, +0x1446 for 3 s, 12.0 m/s at V22350 against 0.7 without it. Test: test_boost_start.
- **RIVAL STYLE** (RACE): Race (the game's) or Road Rage. CB3StageLogic::GetAIMode 0x134340 answers 0 for a race (the
  Road Rage logic has its own); 1 makes the rivals drive as Road Rage's.
- **RIVALS' CARS** (RACE, ROAD RAGE): Your class (the game's), Compact, Muscle, Coupe, Sports, Super, Heavyweights, Same
  as yours, Mixed. SELECT CAR's Confirm (func_00310200) picks the rivals with func_0030B9D0(choices, class): per rival a car of
  that class from the race vehicle list (func_0030B6E0: unlocked ones first, each at most 3 times, random) into
  CB3MenuChoices +0x80 + 8 i, a colour 0..2 into +0xC0 + 4 i; RequestOnePlayerGameMode 0x30C2C0 copies them into the
  stage record. With the row set the pick is made again just before that copy, with the chosen class (+0x30 1 and
  +0x18 5 for the call: the player's slot kept, five filled), or the player's car in every slot (colours 0..2 in turn).
  The explicit choice wins over any other wrapper on func_0030B9D0 (RaceOptions::explicit_rival_pick is true during the
  call, for such a wrapper to pass it through); with RIVALS' CARS at the game's value the port makes no pick of its own.
  Mixed (1.4.0): each rival slot gets a class of its own, drawn from the game's random numbers (D_00518680, the CGtRandom
  the picker itself draws from) among the classes with an unlocked car in the race vehicle list (Compact to Super;
  Heavyweights once one is unlocked; never the two special cars, extra or imported cars), then the game's pick for that
  class for that slot alone (unlocked cars first, each at most 3 times, another colour for a car already taken). Guest
  state and the game's own random numbers only, so a replay makes the same picks. With RIVALS 6 or 7 the extra cars copy
  rivals 2 and 3's as for any other value. The row's nine value pointers sit past the region's blank row (the other
  kinds keep 8 each).
- **The page holds 9 rows**: CB3VOptionsSelectComponent::Prepare 0x3BE2A0 keeps per-row state in arrays of 9 inside the
  component, so 9 is the game's limit (not raised: more would write past them). The RACE page has 3 rows of its own,
  TIME ATTACK and ROAD RAGE 2. The rows from before this batch (TRAFFIC, LAPS / TIMED ELIMINATION, RIVALS, TRANSMISSION)
  come first, as they were, so a recording's presses reach the same rows. optpages (2026-11-22): RACE and ROAD RAGE show
  theirs in two pages with a last row MORE (race_choice.h race_pages_for; RACE: TRAFFIC, LAPS, TRANSMISSION, START,
  RIVALS' CARS | TRAFFIC CHECK, RIVAL STYLE, TRAFFIC PATTERN; ROAD RAGE: RIVALS, TRAFFIC, TRANSMISSION, TIME LIMIT,
  BURNOUT POINTS, START | RIVALS' CARS, TRAFFIC PATTERN, TRAFFIC CHECK; TIME OF DAY, while held back, on the last page;
  TIME ATTACK on one page). Left / Right on MORE rewrite our rows in place: the entries, the count, the selection on
  MORE, each row re-made by the component's own calls (func_003BC360 the label and its enabled state, SetOptionText
  the value), the rows past a shorter page's end blanked and put off the screen (func_003BE040 / func_003BD760 at x
  2000; again after the component's next Updates, which re-make the row the highlight leaves). Nothing is released or
  prepared again; every Prepare opens page 1. A value on the page not shown stays chosen (the old rule reset a row
  that did not fit to the game's value). Recordings made on dev builds with the earlier layout differ past TRAFFIC
  (tester notes).
- **TIME LIMIT** (ROAD RAGE): Off (the game's), 1, 2, 3 or 5 minutes. CB3Score +0x2D0 (the race car's +0x1390) ends the
  race (SetFinished 2) when the race time reaches it [C CB3Score::Update 0x15B000]; World Tour's timed Road Rage sets
  180 s in CB3OfflineRoadRageLogic::OnWorldPrepared 0x38C3F0 and shows the timer (CB3HUDManager::PrepareNormalComponents
  0x1A0AF0 prepares the timer component when CB3Game::IsInChampionship 0x131CE0). With a limit the player's cars get it
  after OnWorldPrepared, and IsInChampionship answers yes inside PrepareNormalComponents only.
- **BURNOUT POINTS** (ROAD RAGE): Takedowns (the game's) or All. CB3OfflineRoadRageLogic::GetScoreModeFlags 0x38C020
  returns 2; bit 0 is "non-takedown Burnout points" (its readers test that bit only); All sets it.
- The records word is 64 bits now (src/b3replay/records.h options_folder): this batch's 16 traffic's third bit, 17
  standing start, 18 Road Rage AI, 19 all points, 20-22 time limit, 23 varied traffic, 32-34 DEFORMATION (below), 35-37
  rivals' cars, 38 rivals' cars Mixed; 24-28 are allocated to other branches (reserved in tests/test_bit_owners.cpp). The renderer's HTD1 record
  carries bits 0-15 in its label word, 16-31 in its race word and 32-63 in a fifth word (24 bytes; a 16-byte one reads
  0 there). Choice codes stay "t2 l0 r0 d0" with a tag for each value not the game's, in a fixed order: " e1" (TIMED
  ELIMINATION), " m1" (TRANSMISSION), " s1" START, " a1" RIVAL STYLE, " p1" BURNOUT POINTS, " k<n>" TIME LIMIT, " v1"
  TRAFFIC PATTERN, " c<n>" RIVALS' CARS (c8 Mixed); codes recorded before these rows read as they were. The arena region grew to
  0x1C00 (0xC3400-0xC5000).

**PC SETTINGS rows** (ids 500-502 are this batch's):

- **CRASH CAMERA** (DISPLAY > CAMERA, id 500, ini `crash_camera`, default On; drawn only: src/b3replay/crashhold.h).
  When the player crashes the camera manager cuts to its own-crash behaviours (slot 7, then a blend into slot 6; mask
  +0xB80 0x80 / 0x40, the blend 0x400 from +0x9F0 to +0x9F4: capture.cpp crash_share, Camera::crash). Off draws the
  frame from the chase view of the last tick before it (Camera::chase 1), its rotation kept and its offset from the
  player's car kept as the car moves, blended in and out with the game's blends. A game-side version (skipping the crash
  director's Prepare 0x357520 on CB3PlayerCameraStateCrashMode::Action 0x357600's entry) changed the race after a few
  crashes: the crash director drives the crash's end. The game's TAKEDOWN CAM (Driver Details) is its own option.
- **DRIVER LABELS** (GAME > INTERFACE, id 501, ini `driver_labels` 0-3, default Off; drawn only:
  src/b3replay/driverlabels.h). Over each rival race car: its driver's name (the race car's +0x1974, 16 UTF-16 units the
  world's Prepare fills: the profile's, the AI's from string 0x311 + its index, a network player's [C B3World 0x19A950];
  inside the capture's race car grab), the straight-line distance (ft with MPH, m with KM/H), or both, in the game's
  prompt font with a drop shadow, 2.8 m above the car (clear of the game's position markers), 2-150 m from the player
  with a 50 m fade, while the race runs on the chase cameras, not in split screen. A label over a nearer one is left out.
  The host UI got a second build slot (View::host_ui_list slot 1) so two host layers can draw in one frame, and cover 4
  (not clipped to the 4:3 box) for the wide picture's sides.
- **DEFORMATION** (GAME > GAMEPLAY, id 502, ini `deformation` 0-4: 0 100 % the game's, 1 none, 2 50 %, 3 150 %, 4 200 %;
  tools/native/game_mods.h). CB3VehicleDeform::Impact 0x3DB450 gets its impact size (f12) times the scale. Impact draws
  on the game's random numbers above a size (the lower panels' random damage), so another scale changes the race from
  the first hard hit: **gameplay-changing**, latched per race at 0x1632C0 only in the races the GAMEPLAY options take
  (game_option_rules.h game_options_stage: Single Event's one- and two-player stages; World Tour, whose results and medals
  count, and online keep the game's 100 %), cleared when the stage is released, records kept apart (records word bits 32-34),
  recorded in replays (the "game_mods" entry).
- **FINISH Chrome** (CAR > CAR PAINT; drawn only): paint mode 12 in car.frag: the environment at nearly full strength
  with a little of the paint's hue and a hard sun glint.

**Already covered** by existing rows: a lap timer in Race and Road Rage (SPEEDRUN TIMER, Event + splits: §5), and a
forced highest car LOD (CAR DETAIL Highest). In TIME LIMIT, CB3Game::IsInChampionship answers yes only inside
PrepareNormalComponents (see its scope note in race_options.h).
## 31. Car stats, turn signals and Road Rage parts (carstats1, blinkers1, rrparts1, 2026-11-21)

Three port options, each a game-side port wrapper (tools/native/car_stats.h, turn_signals.h, road_rage_parts.h; the pure
parts in stats_signals_logic.h) with its own row, handed to the game through the renderer (`b3r_car_stats` /
`b3r_turn_signals` / `b3r_road_rage_parts`) and latched for replays (SETS "car_stats" / "turn_signals" /
"road_rage_parts", from the first VBlank a row is on).

**CAR STATS** (Game / More, default More; SETTINGS > CAR > GARAGE). The game's text has two labels no retail page shows:
0x184 GRIP (between 0x183 TOP SPEED and 0x185 WEIGHT) and 0xA1C ACCELERATION, in every language file. SELECT CAR's stats
component (`func_00435100` Prepare, `func_00434FF0` targets, `func_00434DD0` animation, `func_00434BF0` Release) shows TOP
SPEED (Max Speed In Boost) and WEIGHT (Mass (Kg)) of the car on show. Ours wraps all four: GRIP and ACCELERATION built
the same way (the game's text object, bar and fill, "CarArrow", its colours) on the same layers, in one row with the
game's two, which move right a little to fit (columns -0.02, 0.25, 0.52, 0.80 of the stats' width, from 0.481 / 0.789).
Their values come from the VDB.XML copy the game loaded (the file's key hash, the car's name from its GtID; memory
reads). ACCELERATION = (Torque x 1st Gear x Final Gear / Mass (Kg) - 5) / 9 (the cars span 5.8 .. 13.7). GRIP = 0.15 +
0.85 x (0.6 x Downforce Coef / 3 + 0.4 x (Max Angle - 10) / 17), from the steering's Max Angle in degrees. Both use the
game's floor and 1 as limits. The six objects come from the front-end scene's one object pool (100 objects, a
CGtFreeListPool for all its layers): ours are built only with at least 22 free (6 + 16 headroom); browsing every series,
the car pages and the paint panel leaves at least 56 free with them up. On by default because it is information only
(front end, no race reads it). The golden
record pins it off (B3NATIVE_CAR_STATS=0 in tools/golden/baseline.sh), because the SELECT CAR shots and RAM differ with
it on.

**TURN SIGNALS** (Off / On, default Off; SETTINGS > CAR > CAR LOOK). Each race car keeps an indicator byte +0x18EF,
which `CB3RaceCar::Update` clears every frame (no retail code sets it) and `CB3RaceCarPhysics::Update` ORs into the
light bits. The lights still draw it: 0x20 the left indicators (light group 5), 0x40 the right (group 6), seen in the
shots. Ours wraps `CB3RaceCar::Update`: for each AI racer (+0x1914 == 1, body on), after the game's update, it reads the
car's own AI lane (+0x18B4 / +0x18B8, its point pairs). When the lane 15-70 m ahead turns more than about 26 degrees
from the car's segment, that side blinks (0.7 s period, half on, a phase per car) until the bend is passed. Memory
reads only, no guest call: the game's timing is the game's, and the byte feeds the lights alone, so the race is the
game's and records are kept. Checks: `race_check.sh SCENARIO=blinkers` (B3_TURN_SIGNALS_TEST_PLAYER=1 gives player 1's
car the indicators too, to see them up close).

**ROAD RAGE PARTS** (Game / All loose, default Game; SETTINGS > GAME > GAMEPLAY). In the offline Road Rage's damage pass
(`func_00395A40`, from its Update), a hanging part (state 2) is let go (`func_001CBD80`) only if its kind is not 0, 1, 3, 5
or 6 (the gate at 0x395CD8 .. 0x395CDC). Ours is a port wrapper on `func_00395A40` that runs the same pass without that
gate, for a SINGLE EVENT Road Rage only (the logic's vtable 0x4DEE00 and the current mode the Single Event stage, as the
other gameplay options: World Tour's Road Rage keeps the game's parts, so its results and medals count; the online Road
Rage stays the game's): the count before it still leaves those kinds out, so more parts come off as a car's health
falls. The same arithmetic as the game's unit (its PS2 floats). A part let go takes one of the physics manager's 64
body-part slots (`CB3PhysicsManager::AddBodyPart` 0x1B86A0: with all 64 in use it recycles the oldest, that part gone),
so more let-goes cannot run the pool out (a 7-minute Road Rage: under 25 let go). A part let go is a physics body
(a body from the physics manager's pool, activated, with its sound), so it is gameplay. The race's records are kept
apart as raceopts' (records_guard.h: the records call with no kinds, the profile's "this race counts" answered no), and
the renderer's record word takes bit 25 ("-loose-parts" in the records folder), latched until no Single Event stage is the
current mode. Checks: `race_check.sh SCENARIO=rrparts`.

### 31.1 Crashed-traffic slots bounded (wreckguard, 2026-11-21)

CrashTrafficBody's slot search has no bound: with all 64 CB3TrafficPhysics slots in use it takes the memory after them
and the wreck list (room for 67) runs into its own count; the next physics update's loop over it crashes. Retail never
fills them in its own races, but a player stuck in Heavy traffic (TRAFFIC's row) can: wrecks near the players are never
released. tools/native/wreck_slots.h: CrashTrafficBody with fewer free slots than it needs (one, more for a linked cab or
trailer) first releases one wreck as UpdateActiveBodyList releases one (its vtable +0x14, out of the wreck list, a linked
partner unlinked, its body type 8 with the removal mark: extracted at the end of the frame) -- one in none of the frame's
colliding pairs, at rest and unlinked first, the farthest from the race cars. Below 64 it does nothing (golden
byte-identical); not online; B3NATIVE_WRECK_GUARD=0 turns it off. A run that filled the slots and crashed at VBlank 28200
now releases 25 wrecks and runs on.

## 32. PARTY MODES (party1, 2026-11-21)

GAME > PARTY MODES (section 42, rows 530-535; ini `party_*`; tools/native/party_modes.h,
hooks on the hook table, no game code changed). All off by default; they take GAMEPLAY's races, latch and replay entry
(§25: Single Event and the two-player races, the options word's bits 4..11, "game_options"), and keep the race apart
(records_guard.h, the profile, the records word's bit 10: `options-party/`).

**CRAZY AI PHYSICS** (Off / Basic / Local Wildlife / Mayhem). CB3RaceCarPhysics +0x215 is the vehicle's kind (2: an AI
race car), +0x210 its crashing flag. CB3VehiclePhysics::CollideWheels 0x1CDB70 clears each wheel's ground contact
(+0x8D3 + wheel x 0xC0) before testing it: for AI cars a wrapper keeps a contact that was set (all four wheels), so the game
always takes their wheels for on the road. kGravity (0x004C9CC0, (0, -20, 0) at run time) is zero for ApplyDrivingForces
0x1C1B20 and CB3CollidingBody::Update 0x1AC470 of AI cars
(Update: not crashing; its bodies checked against the AI physics ApplyDrivingForces saw). Local Wildlife: the crash
Update__17CB3RaceCarPhysics starts when a car ends up on its roof (StartRaceCarCrashing 0x1AF450 with no colliding pair or
start info, called at 0x1C4D00) is left out for AI cars. Mayhem: it starts, and CB3RaceCarPhysics::DoCrashBreaker 0x1BE990
follows (one scripted race: five rivals blown up); it reads only that car's own data (its air rams, DetachAll, the blast
0x1B2BA0 from the car's radius and strength; the one player-indexed call, 0x3CCC50, is empty). The player's car is
untouched.

**TUK-TUKS** (row 531, ini `party_tuk_tuks`, the word's bit 6). In a SINGLE EVENT race, and in a SINGLE EVENT Crash event
(its junction too: the same one-player request), the player and every rival drive the tuk-tuk (COMP/CAR18), a real race car through trafficseries (§34): a traffic model (.BTV) alone, loaded for a race car by
CB3RaceCar::Prepare 0x152AF0, stops the game in the race cars' engine sound (CB3SoundESMGraphInputs::ApplyLag 0x2FAFB0
reads past what it holds), so the race loads trafficseries' model file (the .BTV with its donor's engine sound models)
with its donor's tuning (Compact Type 1). native_play gives TrafficCars::set_race_cars the tuk-tuk for the player and the
rivals while the bit is on, before RequestOnePlayerGameMode reads it, and puts the player's own choice back in the menu
choices after the request (SELECT CAR keeps it; TrafficCars::racing takes the race's own car); RIVALS' CARS' own pick (other than Your class /
Same as yours) still picks the rivals. The race is a traffic vehicle's, so trafficseries keeps it apart (records word bit
28, `options-trafficcars`); the party bit is not added for it. Two-player races are not changed (trafficseries is one
player only). The earlier drawn-only development path (the renderer drawing race cars as the .BTV, race word bit 11) is
gone.

**MOON JUMP** (Off / Low / Medium / High: 0.25 / 0.5 / 1.0 a frame), **CRASH ON DEMAND**, **AI DRIVER BUTTON**: port
actions (input_bindings.h PartyMoonJump / PartyCrash / PartyAiDriver, keys 4 / 5 / 6, no controller binding by default, so
nothing clashes with the game's buttons; CONTROLS rows 417-419). MOON JUMP: after func_001BD6A0 (the player car physics'
frame) player 1's car's matrix y (+0x204, then +0x34: 0x0064D3E4 for player 1) rises while the action is held, not while
crashing. CRASH ON DEMAND: after GetDrivingControls 0x1BD020 a press starts the crash the game starts for a boost-held start,
StartRaceCarCrashing(&D_0055E948, physics, slot, 0, 0) and func_001CC520(physics, 9, 0, D_004E0CC8). AI DRIVER:
func_00150280(car, the AI flag's opposite) (+0x27C8). The presses reach native_play as b3r_party_keys (bit 0 MOON JUMP held,
the counts of the others), per VBlank: MOON JUMP held recorded as the "party_moon" setting (each change), each CRASH / AI
DRIVER press as a "party_press" event of its VBlank, played back from them; outside a race with the mode they are dropped.
The latched word is taken once a VBlank. A car that has finished is never handed back to the player (+0x13F4 +0x133C 3).
MOON JUMP is not stopped by a bridge or a tunnel's roof: the car can be lifted through one (the game then resets it).
CRAZY AI PHYSICS writes zeros into kGravity for the call (put back by a guard); KAMIKAZE TRAFFIC reads the race car list's
first 6 entries.

**KAMIKAZE TRAFFIC**. CB3TrafficParam::CalcSpeed 0x161B50 reacts only to the race cars (the table at 0x0064D388, count
0x0064D37C) whose +0x18EA is set: a race car close ahead makes the traffic vehicle crash itself (InduceTrafficVehicleCrash
0x1B82F0). Ours sets +0x18EA on every race car for the call and puts each back after it. The same 32 000-VBlank scripted
race: 70 traffic crashes induced against 8 without. The game's 64 crashed-traffic slots are bounded by wreckguard
(wreck_slots.h: the farthest wreck released when all are in use).

**Checks.** test_settings (party_mode_rows, the translations), test_game_options (party_modes_word), test_input_bindings
(party_mode_actions); race_check SCENARIO=party (MOON JUMP held, CRASH, AI DRIVER on and off; PARTY_OPTS adds modes).

## 33. Traffic checking (trafficcheck, 2026-11-21)

TRAFFIC CHECK Off / Revenge / Extreme: a row on SINGLE EVENT's SELECT OPTIONS for RACE, TIME ATTACK, ROAD RAGE and
ELIMINATOR (raceopts' row machinery: race_choice.h RaceRow::kCheck, the thirteenth row kind; RACE's and ROAD RAGE's
second page, TIME ATTACK's last row; in the choice code as " x1" / " x2"; not CRASH),
and PC SETTINGS > GAME > GAMEPLAY > TRAFFIC CHECK (2 PLAYERS) for the two-player races, which have no option page
(tools/native/traffic_check.h, the rules in traffic_check_rules.h). Off by default. REVENGE plays as Burnout Revenge's
traffic check does, with B3's equivalents; EXTREME checks any traffic. Where the game decides and what the port changes
[C the decomp]:

- **The crash**: CollideAndRespondPair 0x1B7220 sends a vehicle against a traffic body to VehicleTrafficResponse
  0x1B5AA0. A slow bump is the game's shunt; a fatal hit (the hull test, kaabCanFatal, the closing speed) starts the
  race car's crash (StartRaceCarCrashing 0x1AF450), makes the traffic a wreck (CrashTrafficBody 0x1B41D0: body type 4,
  one of 64 CB3TrafficPhysics slots) and hands the pair to BodyBodyResponse 0x1B4BF0. A check is that fatal hit by a
  human player's car (body type 2) at 75 mph or more on traffic going its way
  (headings within 70 degrees) that is not a big rig, a bus or a trailer (CB3TrafficVehicle
  +0x173 categories 4 / 5 / 11) nor one of B3's special vehicles; EXTREME drops the speed, direction
  and vehicle tests: any hit the game makes fatal is a check. (The game's fatal test is its own speed floor: the closing
  speed along the contact's normal above 75 mph, krRaceCarCollisionHeight, times the car's +0x1534: 1, 0.5 in some
  modes, 0.25 in a Burnout's boost, less just after a slam; and a car touching a wall that frame (+0x212, set by
  DoDrivingWorldCollision) is crashed by any traffic hit. So under 75 mph of the car's own speed, a fatal hit is an
  oncoming or crossing car, a boost or a wall: a crash in REVENGE, a check in EXTREME.) A hit the game only shunts
  (closing speed under its limit: VehicleTrafficResponse's non-fatal response, the car pushed out and bounced) is a check
  too from 75 mph of the car's own speed when the rules allow one, as Revenge checks traffic going its way at its minimum
  speed whatever the impact (most hits on traffic going your way are shunts in B3): after the game's response the traffic
  is crashed as the fatal path crashes it (CrashTrafficBody with the player's index and the car's +0x218, the wreck's
  +0x211 set) and pushed as below. The car's crash is not started; the pair's BodyBodyResponse is the port's launch: the wreck's
  velocity becomes 1.5 times the car's plus 5 m/s along the contact normal, a little spin, the car a small
  recoil (the physics' impulse accumulators +0x110 / +0x120). The traffic still becomes the game's wreck, credited to
  the player as a crash would.
- **Wrecks**: a race car against a wreck (crashed traffic, body type 4) is BodyBodyResponse's, whose fatal test (the
  impulse) starts the car's crash. While the car is not crashing, BodyBodyResponse moves and pushes only the wreck
  [C its ta = 2], so a touch whose crash is not started leaves the car's speed alone and shoves the wreck away. A checker's
  own checked car never crashes it, past its 10 s too (the touch makes it the checker's again, no points), nor does its
  chain: a car the checked car wrecks (VehicleTrafficResponse) or a wreck it knocks (BodyBodyResponse between two wrecks),
  and on down the chain, is the checker's too, so a checked car shoved into the car ahead and either of them bouncing back
  into the checker spares it (both modes). The chain's cars only protect: no takedowns or shots of their own. EXTREME: any
  other wreck that is not another player's checked car now (traffic a checked car, a rival or a pile-up wrecked) is
  spared the same way and becomes the player's checked car, a check (once per wreck). REVENGE crashes you on those.
- **The checked car**: its checker's for 10 s (the score's clock). The checker touching it again is never crashed by
  it; any other race car (an AI rival, the other player) touching it is crashed (not within its reset grace, +0x152C)
  and the takedown written to the checker's takedown table (CB3RaceCar +0x1598 / +0x15B0 / +0x15B6, as
  UpdateTakeDownStats writes an aggressor's), so the game itself awards it (CalculateScoreForTakedowns 0x1578E0).
- **Scoring**: a check 50 points and 30 boost (half B3's near miss, gmrNearMissBurnValue 60;
  AddBurn 0x14E2D0). A checked car that wrecks other traffic (in VehicleTrafficResponse): an oncoming car TRICK SHOT (50,
  40 boost), a truck or bus SKILL SHOT (100, 60), an oncoming truck or bus EXTREME SHOT (150, 80); the same shot again
  only after 2.5 s; a car going the same way nothing. The chain is a CB3ScoreCategory of the port's
  (GuestArena::kTrafficCheck) with the game's near-miss star levels: the boost bar's message slot 3, which retail never
  updates (MessagesUpdate 0x1A41B0 does 0-2 and 4-6), shows TRAFFIC CHECK with its stars (UpdateBoostMessage 0x1A45F0).
  GOOD / GREAT / EXTREME CHECKING pop up as it reaches the levels; it ends 5 s after its last event (as B3's
  near-miss chain time) or 1 s after EXTREME CHECKING, with its total. The popups and the total are the PERFECT LAP sign
  (CB3ScoreBonus::OnAward 0x368E90, event 0x77, as NEAR MISS's chain end awards), its text ours while UpdateMessage
  0x3D8280 draws it (GetString 0x271A00 of id 0x804) and the mode's SCORE counter not shown for it (ShowScore 0x1A2030).
  A crash loses the chain (OnEvent refuses the award while the player crashes).
- **Wrecks**: with 40 or more of the 64 wreck slots in use, PostEffectsUpdate 0x1B8EF0 (= UpdateActiveBodyList) gets up
  to two wrecks a frame marked for removal (the body's +1, as the game marks one): not linked to a trailer, behind every
  player and 60 m away, or 150 m away anywhere. A check needs two free slots. (wreckguard, wreck_slots.h, bounds the
  slots for every race: below.)
- **Records**: as §22: no record kinds for 0x3D1090, "does not count" from 0x136F30; ghosts and best times with the
  race's other options in one folder, "options-trafficcheck" / "options-...-trafficcheck-extreme" (the records word's
  bits 26-27: traffic_check_rules.h traffic_check_record_word, from the race's latched mode). Crash mode, World Tour, Crash party and online are the game's. A replay holds the row in
  its race_options code and the two-player setting as `traffic_check` 1 / 2.
- Checks: checks, the caption, a trick shot and the sign (a Heavy-traffic race); Extreme checking a crossing car and a
  shot on a heavy vehicle; race_check SCENARIO=trafficcheck (TRAFFIC Extreme, the player's car driven by the AI through the
  traffic with the test-only $B3_TRAFFIC_CHECK_TEST_AI=1; TC_MODE 2 / 1): Extreme checks, wrecks among them, and no
  crash of the player's on traffic or a wreck (walls still crash it); $B3_TRAFFIC_CHECK_TRACE=1 logs every crash of a
  human player's car in such a race, the path it came by and the rule that kept it from being a check, each wreck a
  chain takes ("TRACE chain") and each touch of the player's own checked car or its chain spared ("TRACE spared": the
  rebound case); the run's summary counts the checks (wrecks, shunts), the chains' wrecks and the crashes on traffic and on
  wrecks; a checked car's takedown of a rival awarded by the game (TAKEDOWN!, 2-IN-A-ROW) with the
  test-only $B3_TRAFFIC_CHECK_TEST_OWNED=1 (any wreck counts as player 1's checked car).

## 34. Traffic vehicles as race cars, the TRAFFIC series (trafficseries, 2026-11-22)

(discread, 1.4.0: the vehicle's two disc files are read through the disc layer, `b3asset::disc::read_file`, so the
series drives from a disc image as from a folder; race_check runs `race` and `trafficseries` from an image too.)

SINGLE EVENT's SELECT CAR offers TRAFFIC (20 vehicles) and TRAFFIC SPECIAL (7) after the last series the event allows,
and a traffic vehicle drives as a race car, the player's or the rivals' (tools/native/traffic_cars.h; the tables and
rules in traffic_series.h, tests/test_traffic_series.cpp). What the game has [C the decomp]:

- **The vehicles**: the vehicle list holds the 67 race cars and the 40 traffic vehicles (COMP CAR11-19, HEVY CAR11-35,
  TSPC CAR1-6: one .BTV each). The program's traffic table D_004CDC00 lists the 40 GtIDs (FindTrafficVehicle 0x3EAD20)
  and D_004CDD40 their name strings (1894-1928: Car, Taxi, Tuk Tuk, Pickup, Van, Bus, Big Rig, the trailers, Minibus,
  Gritting Truck, Motorhome, Tram), as func_003EACC0 gives them; the series names are strings 380 "TRAFFIC" and 381
  "TRAFFIC SPECIAL". GetClassFromID classes them by their names (COMP 0, HEVY 6, TSPC 8); class 7 has no vehicle.
- **What a race car needs that a .BTV lacks**: a .BTV has the race car file format (version 0x17: LODs, panel and wheel
  frames, damage influences: it draws, deforms and breaks glass as a race car), but not the engine sound models the
  sound reads from the car's model file (+0x16DC / +0x16E0, func_00288960; a null one is the crash in ApplyLag
  0x2FAFB0), no .HWD / .LWD sound banks, and at most 9 VDB values (mass, suspension) where a race car has 64, with no
  Camera, VehicleAudio or AI file. Its front-end picture does not exist either (func_00371670 copies a null texture).
- **Ours**: each traffic vehicle has a donor race car by type and size. CB3RaceCar::Prepare's `<path>.bgv` is served
  (a disc path alias) from a file made of the .BTV, zero padding, and the donor .BGV's tail from its first engine sound
  model on (unchanged, so its own offsets hold), with the two offsets set; the header's size stays the .BTV's, so the
  renderer recognises the model as the .BTV. It is made once from the files as the disc serves them (a mod's
  included) and cached under the user's data folder (`cache/traffic`, the portable folder in portable mode), named by
  a hash of both inputs and written to a temporary name, then renamed. A vehicle whose file cannot be made is not
  offered (logged); a series with none is skipped. The alias never shadows a file of that name the disc or a mod has
  (none on the retail disc). The .HWD / .LWD are the donor's. The race car's own tuning registrations (AddEntry within
  CB3RaceCarPhysicsAttribs::Register 0x252C20 for VehiclePhysics; Camera, VehicleAudio, VehicleAudioAI, AI) register
  under the donor's file, except the suspension geometry (attach height, spring length) of a vehicle with its own
  VehiclePhysics file. The traffic on the road keeps its own (CB3VehiclePhysicsAttribs::Register 0x252100 and
  VehicleMaterial are the traffic system's). func_00371670 gets the donor's id.
- **SELECT CAR**: the carousel's Right / Left steps (func_0042BEA0 / 0x42BD70), "can go right" (0x42C1A0) and arrows
  (0x42CD60) go on past the last series allowed to classes 7 and 8 (the steps as the game's own: the rows rebuilt with
  func_0042B7C0, the shown objects released, 0x42DAE0); GetCarCount / GetCarID / IsVehicleDriveable answer for those
  classes (20 rows a series: the carousel's tables); the name comes from the traffic table (func_003EAE40 after
  func_003EB060's -1); the small picture is the donor's (GetCarTexture 0x4277C0 and func_00427080's own lookup: the
  front end has none of traffic vehicles); the 3D model is the same served file. A remembered traffic vehicle opens on
  the first series (as the game does with a heavyweight an event does not allow). Rivals of a traffic vehicle are race
  cars of its donor's class (func_0030B9D0's class: func_0030B6E0 would pick from an empty list).
- **Only Single Event**: the race's traffic vehicles are set by RequestOnePlayerGameMode and cleared by every mode's
  RequestNewGameMode 0x30D1B0, so World Tour, two players and online never get redirected registrations.
- **The save**: it holds no car GtIDs; its car indices (race events per car, the cars of the longest drift and
  oncoming) take func_003EB060's index and skip -1, the traffic vehicles' index, and are written only when 0x136F30
  says the race counts (func_0042F0F0). A Crash event with a traffic vehicle to its results leaves the save unchanged
  (the game does not even autosave: nothing changed).
- **Crashbreaker and the AI's series**: the Crashbreaker's radius and strength (func_003EAD60 / func_003EADD0, from
  CB3PhysicsManager::DoCrashBreaker) come from the 67 race cars' table: 0.0 for a traffic vehicle (a blast that does
  nothing). They are the donor's: a bus's is the City Bus's 45 / 3.5. CB3RaceCar::SetCarID's series (car+0x1970, the AI's
  catch-up row) is also the donor's, where the game gives an unknown id Compact's.
- **Records**: with a traffic vehicle as the player's car, records_guard.h's CB3Records::Update with no kinds and
  0x136F30 answering no; bit 28 of the record word ("options-trafficcars", one folder with the race options).
- **Left out**: the seven trailers (no cab) and the trams. The small pictures are the donors' for now (rendered car
  thumbnails are being built for the painted cars; they are to cover the traffic vehicles too).
- Gameopts' rows set the race's cars directly (TrafficCars::set_race_cars: the player's and every rival's); tests use
  B3_TRAFFIC_CARS="player=COMP/CAR18,rivals=COMP/CAR18". B3NATIVE_TRAFFIC_SERIES=0 takes the series off; replays record
  "traffic_series" (none: off).

## 35. Night lighting: street lamps, signs, windows, tunnels (nightfix, 2026-11-21 / 22)

Night mode's lighting pass after 1.3.0 (night and AMBIENT OCCLUSION stay hidden until the owner decides; TUNNEL LIGHTS,
§35.4, is a day feature with its own row). §17 is the night mode itself.

### 35.1 Night lighting pass (nightfix, 2026-11-21)

Night and AMBIENT OCCLUSION stay hidden (1.3.0); this pass fixes what the owner found playing night on a real GPU.

**Street lamps on every region.** 1.3.0 found no lamp on the Asia city and motorway routes (AS/C1, AS/M1: "street
lights ... 0 lamps") and the lamps it found on the island route (`KS_lamppost`, ~20 m) lit nothing. Two causes:
- the posts' textures: a census of every route's textures (each texture's triangles grouped by 1.5 m x / z cells; tall,
  thin groups listed with their height) and a day view of each candidate (b3view at the group) found the missing lamp
  posts: `bk_post` (the Asia city and motorway's 17 m cobra heads), `HK_post` (HK's dock road), `CTY_Post` and
  `CST_trafPost` (US), `VYD_Post` (EU) are one post atlas shared with sign posts, so only the triangles whose UV centre
  lies in the cobra head's corner of the atlas are lamps (kHead; `HK_post`: its lens strip); `WF_trampost` (US
  waterfront), `pole` (EU motorway), `VYD_FancyLampost` (EU bridge lamps), `bk_riverlamps` are whole posts (kPost: the
  lamp at the end of the arm, the group's point furthest from its pole's foot within 1.5 m of its top). Left out by eye:
  traffic-light posts (`GL_GrnLpst_03`, `GL_tlightPost`), utility and telegraph poles. `KS_lamppost` (an atlas with
  traffic signals) takes its pole strip only (u 0.25-0.5).
- the pool's size: it was fixed (falloff d0 7 m, faded out 14-24 m from the lamp), tuned on 6 m lamps; under a 17-20 m
  lamp the road is already at the fade. Each lamp now has a foot (the lowest point of any listed texture within 9 m: a
  cobra head hangs 4-7 m out from its pole; none 2 m below it: 6 m up, a wall lamp's small pool) and its pool's d0 and fade scale with its height
  over a reference of 12 m (0.75..2; `$B3_NIGHT_STREET_SHAPE`'s seventh value), so tall lamps light a wider pool, a
  little dimmer under it, and the pools along a motorway stay pools (8 m reference: one orange wash, like dusk).
Found now (1.3.0 -> nightfix): AS/C1_V2 0 -> 362 lamps, US/C2_V2 746 -> 884, and more on every route with a new
texture (AS/C2_V1, AS/M1_V2, US/C1_V1, EU/C1_V1, EU/P1_V2 by roughly 1.4-2 x). Lamps whose positions lie within 1.5 m
merge (a head and its post in two textures; 3.5 m before, which with the lamp at the arm's tip merged posts facing
each other over a street). The count per frame is 32
(MEDIUM 8), and no lamp pops at the cap: each lamp's gain fades to 0 between 80 % and all of the distance of the first
lamp left out (or 120 m), a distance that is continuous as the camera moves (the k-th nearest of continuous distances),
and by rank over the last 8 slots (MEDIUM: 2), so when a nearer lamp turns into the view in a fast turn the lamp pushed
out of the cap is already at 0 (the others step by 1/8). The lamps are found at the route's load only when its race is
at night or `$B3_NIGHT` is set; by day nothing is loaded, and night turned on mid-race loads them on its first frame. `$B3_NIGHT_LAMPLIST=1` prints every lamp with its height and tint. Seen in
offscreen night shots of AS/C1_V2, AS/C2_V1, AS/M1_V2, US/C2_V2, US/C1_V1, EU/P1_V2, EU/C1_V1 and split screen.

**Billboards.** The boards lit by their own floodlights had no entry in the glow tables (only backlit panels had), so
they went dark with the grade. A third fixture kind (a table name ending in '+', `DrawItem::night_emissive` 3): the whole
board at its pre-grade colour x a warm floodlight tint (1, 0.9, 0.76) over 90 % of the grade (`FrameParams::Night::board`):
`bk_billboards2`, `bk_skytrainads`, `KS_adverts` (Asia), `Chgo_BigAdverts`, `Chgo_BigAdverts_02`, `GL_marina_billboard`,
`GL_resort_advert` (US), `ATB_Billbds2`, `R_Adv`, `R_Advs02` (EU); `AXE_AD_PHOENIX` joins its backlit siblings. Their
lamp heads are frame geometry without a texture of their own (the frames share `Chgo_BigAdverts_Backs` and scaffold
textures with unlit structures), so a spot per lamp could not be placed reliably; an even wash is how a floodlit board
reads at night.

**Covered roads.** Two things made the road under HK's long lattice deck (AS/C2_V1, before its tunnel) and under the
Asia skytrain patchy:
- the baked light is the day's: the sun through the lattice and between the skytrain's decks left bright stripes next
  to hard deck and pillar shadows in the vertex colour, and the grade kept that pattern, darker. At night the track's
  vertex light now has its contrast flattened, luminance l -> 0.5 (l / 0.5)^(1 - 0.5), hue kept (`FrameParams::Night::
  flat`, mesh.frag, `$B3_NIGHT_FLAT`; the lamp pools restore the same flattened colour). The road under a deck reads as
  one surface.
- the tunnel signal: the logs (`$B3_NIGHT_SKYLOG`) in HK's tunnel showed the car's sky_open jumping 0-0.37 from frame to
  frame while every ray hit, so the tunnel's light pulsed between 0.47 and 0.85 of its strength; under the open-sided
  deck the roof rays flickered 0.22-0.67. The enclosure is now the geometry's: the nine roof rays x a second ring of
  eight rays 60 degrees off the vertical that count a hit within 14 m (a tunnel's walls and roof stop them; under an
  open-sided deck they leave past its edge: 0.38-0.62 there, 1.0 in the tunnel), with sky_open only gating the rays
  (below 0.6), the ring cast only where the roof rays are above 0.6. Under the deck it stays night (lit by its lamps and
the beams); in the tunnel the fade holds at full. The rays' cost (`$B3_NIGHT_SKYLOG`'s rays_us, WSL): 17-40 us mean,
94 us at most per frame in HK's deck and tunnel and the Asia city's streets; 0 where sky_open is 0.6 or more. SUN
SHADOWS' receiver in mesh.frag measures the baked light it clamps against with the same (flattened at night) vertex
light the surface draws.

**Headlights**: the players' beams (1.5 x, 34 degree spill since a61d6bb) checked from the chase camera on US/P2_V1's
dark mountain road: the road ahead and the cars beside lit warm, no glare; unchanged.

**Ambient occlusion** (§18 piece 3): the noise lines were the sampling pattern. Each pixel's twelve-tap disc was rotated
by interleaved gradient noise (a pattern of diagonal streaks by design) and only a 3 x 3 depth-aware filter followed,
which cannot cancel it: the streaks and grain showed over every surface with any occlusion, and moved with the image.
Now the rotation (and the taps' radial offset) is a 4 x 4 ordered pattern, a half-size depth-aware 4 x 4 box blur
(`ao.frag` mode 3, a second half-size buffer `Targets::ao2`) takes each of the sixteen rotations exactly once, so the
pattern cancels completely and the result is the same from frame to frame for a still camera, and the composite
upsamples the four nearest half-size texels with bilinear weights x a depth test (the edge keeps its own side).

**Cost** on the owner's RTX 4080 SUPER (native_play.exe offscreen from WSL, MSAA 4, the same frame drawn 15 times per
form, interleaved, medians; dev 47a833f's renderer against this one on the same polls; other jobs on the machine, so
+-0.2 ms at 1080p and +-0.4 ms at 4K of noise):
- night (`B3_NIGHT_ABTIME`), US/C2_V2 polls 10900-11300: 1920 x 1080 night +0.62..+0.69 ms before, +0.79..+0.90 ms now;
  3840 x 2160 +1.46..+1.86 before, +1.70..+2.25 now (32 lamps instead of 24, larger pools: about +0.2 ms at 1080p,
  +0.3 ms at 4K). AS/C1_V2 under the skytrain (0 lamps before): 4K +0.94..+1.50 before, +1.51..+1.65 now.
- AMBIENT OCCLUSION by day (`B3_SURFACE_ABTIME`, x31 at 1080p): 1920 x 1080 +0.27..+0.49 ms before, +0.30..+0.46 now;
  3840 x 2160 +0.86..+1.28 before, +0.89..+1.18 now: the blur pass (16 fetches at half size) is within the noise; the
  cost is still the depth pre-pass (§17.2's follow-up: the scene pass writing its depth beside the frame).
The flattened baked light and the billboards' wash are a few ALU per track pixel and one draw per board: not measurable.
Off (night off, AO off) nothing of this runs: the golden record's items are all equal.

### 35.2 Lamps, billboards and windows lit (nightfix round 3, 2026-11-21)

From the owner's look at §35.1's shots: the lamps did not read as on, the boards looked evenly lit rather than lit from
their fixtures, and the buildings were dark.

**Lamp heads.** Every lamp found (§35.1; not only the 32 that light pools) gets a corona at its head in front of the
camera within 300 m (`View::night_lamp_coronas`, drawn as the game's corona array 0 is, "coronaglow", additive after
the grade so the bloom takes it): a square of 2.2 m half-size x (the lamp's height / 14 m, 0.3..1), at least 1.2 % of
the view's half width so a far lamp stays a few pixels, pulled 1 m towards the eye for the Z test, the lamp's tint x
230 (GS units) x (0.45 + height / 20 m, 0.6..1), full to 80 m, 35 % at 300 m (`$B3_NIGHT_LAMP_CORONA` = "gain, half
size, least share"). A row of lamps along a motorway now reads as a row of sodium heads; the town's low lamps stay
small. The pools: gain 2.2 -> 4.0, x (0.35 + height / 20 m, 0.6..1) per lamp, so the tall lamps' pools read on the road
and the 6 m town lamps do not white out a snowy street.

**Billboards** (the '+' boards): `View::night_board_load` finds each board: its vertical triangles, those whose boxes
touch with parallel planes joined (a board is a few large triangles with vertices only at its corners), the plane, its
bottom edge and width; 1-6 fixtures (one per ~4 m) 0.3 m under the bottom edge and 1 m out on each side, aimed at the
board's middle (AS/C1_V2: 298). Each frame the nearest 16 on the camera's side within 150 m (faded at the cap as the
lamps are) light the board in the beam pass: a cone (full within 30 degrees, none past 60), the falloff 1 / (1 + (d /
(0.6 x the board's height))^2), faded out at 1.5 x its height, warm white, facing the fixture: bright at the bottom
edge, falling off upwards and between fixtures. The fixtures glow (a corona of 1.2 m half-size); the board keeps 20 %
of its old even wash as a base. Not done: a visible cone in the air (a volumetric sprite per fixture: not cheap enough
to be worth it at these sizes).

**Shop fronts and windows.** Two more fixture kinds in the per-region tables (texture names, chosen from a census of
facade textures and day views of them): '^' a shop front (its glass -- the texels darker than their surroundings, the
texture's local average at a mip about one window cell wide, and than its mean light -- a warm interior light with a little of the texture's detail, the rest a warm wash 25 % of the way back to its
pre-grade colour), '%' a facade with windows: at load each such
texture's mean light and its grid of windows (the dark runs of its column and row light profiles, 1..8 each,
`View::night_window_grid`) go on its draws (`DrawItem::night_mean`, `night_cols`, `night_rows`; a texture with fewer than 2 x 2 is left dark: it
would light whole panels); in the fixtures pass
each window cell of the texture's grid, per repeat and per 25 m block of the world, is lit or not by a hash (about 42 %
lit), its glass texels (as for the shop fronts: darker than their surroundings) in warm, neutral or cool light (0.26..0.48 in frame units, a little of the texel's detail kept).
The window is the same from frame to frame and from every angle (the hash is of its place). Lists: Asia's city and
island facades and shops (bk_building*, bk_freewayoffice*, KS_apartments*, HK office and city blocks, bk_shopbot*,
KS_shop*), Europe's (Win_*, ShopFront_*, R_Shop_*, R_Flats*, R_Hotel*, R_Building_*, ATB_Builds*, HouseFront_*,
R_Blocks_*), the USA's (Chgo_UpTWin_*, Chgo_DwnTN_*, Chgo_City_Bck*, GL_SHOP*, GL_SHOPTOP*, WF_hotel*,
WF_apartment1, WF_office1). Limits: where a texture's windows do not fall on a regular grid, or its glass is lighter
than its wall (pale blue panes on grey stone), a cell can light part of a window or the wall's darker parts.

**Cost** (RTX 4080 SUPER offscreen, as §35.1: the night stage off / on x15 interleaved, medians; dev 0b6b4c6's renderer
against this one on the same polls; the machine busy, so the day frame itself moved by up to 1 ms between runs):
US/C2_V2 1920 x 1080 night +0.90..+1.46 ms before, +0.96..+1.80 now; 3840 x 2160 +2.14..+2.17 before, +2.64..+2.99 now.
AS/C1_V2 (the freeway billboard, the skytrain, the avenue) 1920 x 1080 +0.45..+1.00 before, +0.80..+2.18 now; 3840 x
2160 +1.56..+2.26 before, +2.14..+2.76 now. About +0.1..0.5 ms at 1080p and +0.5..0.8 ms at 4K: the floodlights' loop in
the full-screen beam pass, the window and shop-front draws (each such facade drawn once more), the coronas (one
sprite batch). With night off none of it runs (the golden record's items are all equal).

### 35.3 Signs, neon and tunnel lights (nightfix round 3b, 2026-11-22)

More of what the owner found dim at night, each night-only (with night off nothing runs; the golden record is equal).

**Neon and LED signs** (a fixture kind '!' in the per-region tables, `DrawItem::night_emissive` 6): the lit texels (the
brightest channel above ~0.35) at their pre-grade colour, saturation x 1.35 and brightness x 1.3, over the graded sign,
so the tubes and dots read as lit and the bloom makes their coloured glow; and per sign a soft corona of its lit colour
(`View::night_glow_load`: each '!' texture's lit texels averaged into its colour; its triangles in 3 m cells, joined;
the halo at the sign's centre, 0.4 x its size, 0.5..2.5 m half across, 0.22 x the lamps' corona gain). Listed: the Asia
LED message boards (bk_warnsigna / bk_warnsignb and their animation frames: "SLOW DOWN", "SPEED LIMIT", "DANGER
AHEAD"), HK_Neon (also the freeway gantries' lane-control arrows and crosses), the Asia neon sheets
(bk_neonsigns, bk_marketneonsigns*), the city's neon (Chgo_ShopSigns, Chgo_words*), the EU / US electronic signs
(Freeway_info_slow, ATB_Sign_Info_*, ATB_Signs_Elec: the speed limits, the lane control X and arrows), the US message
boards (Chgo_AccidentSign_Words: missed at first, the owner's unlit "SLOW DOWN" on US/C1_V1's covered road), and the road-closure
LED boards every route draws (Arrows, the amber arrow board, and Barrier, the red cross: STATIC +0x28's group, which
the earlier census had skipped). The message boards are matched as families by name prefix ("prefix*!" in the tables:
every wording and animation frame, e.g. bk_warnsignb4..12, ATB_Sign_Info_Text_A3..A15, Chgo_AccidentSign_Words2,
Freeway_info_speed), and a scan of every route's textures for an LED dot pattern (a dark board, a regular grid of small
bright dots) found no other family. Routes with such boards: Asia C1, C2, M1 (bk_warnsign*), Europe C1, C2, M1, P1, P2
(ATB_Sign_Info_*), C4, M2 (Freeway_info*), C2, M1, P1, P2 and the USA's M1, P2 (ATB_Signs_Elec), the USA's every route
(Chgo_AccidentSign*), HK (HK_Neon's lane signals). Not done: a flicker (photosensitivity, SCREEN FLASHES) and a coloured spill onto the
wall and road (one more light kind in the beam pass: later, if the halos are not enough).

**Retro-reflective signs** ('~', mode 7): the road-closure and chevron boards that are not lit (ROAD CLOSED AHEAD,
the yellow-and-black chevrons, the arrow boards, the striped warning rails): their pre-grade colour x (0.5 + 0.9 x
facing^3 x the headlights' reach), the facing taken towards the camera (the chase camera sits just behind the car's
lamps: a board the car points at shines back), faded out 35..90 m.

**Arm lamps** (owner, a glow on the bend of the EU motorway's curved posts as well as at the head): where a post's arm
and its pole fall in two groups (the arm's cells not next to the pole's), each gave a lamp, the pole's at its top (the
bend). A tall group (3 m or more high: a pole) with a short one (the arm or the head) within 12 m across (the motorway
arms reach 7-9 m) and 2.5 m in height is now one lamp, the head's. Audited with `$B3_NIGHT_LAMPLIST` (each lamp's
height, group extent and tint) on AS/C1_V2, C2_V1, C3_V2, M1_V2, US/C1_V1, C2_V2, EU/P1_V2, C1_V1, M1_V1: no pole top left
beside a head; the heads 1.5-3 m apart that remain (US/C1_V1 31, EU/P1_V2 18) are double fixtures (twin gas lanterns,
paired wall lamps). The grid of soft orange discs over the trees on EU/M1_V1 is the game's own effect (there by day too,
and at night with every night light pass off).

**Tunnel and underpass light fittings**: the ceiling fittings join the street-lamp tables as cool-white lamps (tint
(0.85, 0.93, 1.0), the lit part of each atlas only: HK_INS_LightRig, HK_Tunnel01's panel, Chgo_TunLght,
WF_illumnated1's strip, ATB_Tunnel_Light, ATB_Tunnel_DLight's strip): each throws a pool on the road and lower walls
below it (a row of them a strip of light), and gets a small halo; their emissive glow is the §17.2 fixtures' (the
bright texels at the pre-grade colour), now also for ATB_Tunnel_Light / DLight, GL_tunnel_light and HK_Tunnel01.

**Cost** (RTX 4080 SUPER offscreen, night off / on x15 interleaved, medians; dev 0b6b4c6's renderer against round 3 +
3b on the same polls; the machine busy, so +-0.5 ms): US/C2_V2 1920 x 1080 night +0.71..+1.81 ms before, +0.58..+1.30
now (in the noise); 3840 x 2160 +1.79..+2.07 before, +2.73..+2.86 now. AS/C1_V2 (the gantry, the skytrain's neon, the
avenue) 1920 x 1080 +0.48..+1.30 before, +0.71..+1.94 now; 3840 x 2160 +1.68..+2.02 before, +2.50..+4.01 now. About
+1 ms at 4K, +2 ms on the busiest avenue frame: most of it the full-screen beam pass (32 lamps and 16 floodlights tested
per pixel); the fixtures (signs, facades: each such draw once more) and the coronas the rest. The follow-up (§17.2's)
is a tiled light list, so a pixel tests only the lights that reach its tile.

### 35.4 Floodlit boards, arm lamps, TUNNEL LIGHTS by day (nightfix round 3c, 2026-11-22)

**Floodlit billboards** (owner: the board read evenly lit at mid distance). Three causes: the fixtures' light was
selected within 150 m with a fade from 120 m, so a 25 m board seen from 120-140 m (the Asia freeway's "Battlefield") got
almost none; the 20 % even wash under them; and the light only brought the board back to its daytime colour, so overlapped
wide cones looked like the wash. Now: the floodlights are chosen within 300 m; no wash (the board is graded dark where
no fixture reaches); one fixture per ~6 m, 2 m out, aimed at 45 % of the board's height; a narrow fan each (full within 7
degrees, none past 17), the falloff over 0.25 x the board's height (2..6 m), so the bottom edge is bright and the top
corners and the gaps dim; warm and up to 1.3 x the daytime colour near the fixture; the facing term eased for the
grazing angle. Shots: round3c/AS_C1_V2_city_12900_boardwash.png (the owner's frame), _12950.

**One light per fitting, at the lens** (owner, round 3e: glows near but not on the lens, and a glow at the 90-degree
bend of L-posts on EU/M1_V1). The post's lamp is now its lens: the base is the x / z of the group's lowest point, the
tip the point of its upper half furthest out from it; an arm (the tip 1 m or more out) has its lamp at the head's
underside -- the lowest point within 1.2 m of the tip, x / z the mean of the head's underside (within 2 m of the tip and
0.4 m of that bottom) -- 0.1 m below it; a straight post keeps its top, 0.3 m down. A straight-post lamp is dropped where
it is an arm's pole: where an arm group starts (its base within 2.5 m) or, for the sodium street posts, where an arm's
lens lies 2-12 m out at its height (3 m; lamps stand 25 m or more apart; the white town lamps, posts with the head on
top, are left). `$B3_NIGHT_LAMPMARK=1` draws each lamp as a small marker instead of its corona (magenta an arm's lens,
cyan a straight post's top, yellow an atlas head) and `$B3_NIGHT_LAMPLIST` prints each with its base and kind; checked
frame by frame on EU/M1_V1 (11100-11300), US/C2_V2 (10900-11300), AS/C1_V2 (12900, 15300, 15800): round3e/*_marks.png
beside the same frames lit (*_lens.png). EU/M1_V1 1233 lamps (was 1309: the poles and bends gone).

**TUNNEL LIGHTS** (GRAPHICS > LIGHTING, Game / Glow, row 374, ini `tunnel_lights`, default Glow; PS2 ORIGINAL sets Game;
`$B3_TUNNEL_LIGHTS` = game / glow wins; drawn only). A player's request: on the Xbox the tunnel lights glow by day (Coastal
Dream, Alpine), on the PS2 they are plain textures. With Glow, by day: the tunnel and underpass light fittings
(`DrawItem::day_glow`: the night lamp table's tint-2 fittings -- HK_INS_LightRig, HK_Tunnel01, Chgo_TunLght,
WF_illumnated1, ATB_Tunnel_Light, ATB_Tunnel_DLight -- and the glow-only HK_INS_LightRigTop, ATB_TunnelB, ATB_TunnelRnd,
R_tunnel_A, ATB_LightDecals, GL_tunnel_light) draw once more after the opaque world: their bright texels at 0.85 x the
texel in frame units (1.7 x on screen: above white, so the bloom glows), 80 % over the prelit draw (emissive.frag mode
8, no frame copy needed); and a small halo per fitting (the night lamp loader, fittings only, on a worker at the
route's load: 1.6 ms on HK). A route without fittings runs no day glow pass. No pools by day (the tunnel's own light is baked). Routes: the Asia HK tunnel (C2), the US waterfront's
covered road and city tunnels (C1, C2, M1, P2), the EU Alpine tunnels (ATB_Tunnel*: C2 Alpine, M1 Alpine Expressway, P1,
P2) and the Riviera / coastal tunnels (R_tunnel_A: C3 Riviera, M2, P2); the route folder of Coastal Dream is not named in
the game data the port reads (its R_ / VYD_ textures place it in M2 or P2). Game draws nothing more: the golden pins
`B3_TUNNEL_LIGHTS=game` (tools/golden/baseline.sh), its record predates the Glow default.

### 35.5 TUNNEL LIGHTS' Xbox looks (xboxglow, 2026-11-22)

TUNNEL LIGHTS gains **Xbox** (2) beside Game and Glow (ini `tunnel_lights` 0-2, `$B3_TUNNEL_LIGHTS` = game / glow /
xbox / 0-2; drawn only; the default stays Glow, PS2 ORIGINAL Game, the golden pinned to Game).

**Withdrawn (xboxlamps, 2026-11-23):** a fourth value, Xbox all lamps (3), lit the lamps outside the tunnels by day too
(below). It looked a bit broken and was taken off the row (the owner's decision); the value Xbox tunnels was renamed
Xbox. An older ini's `tunnel_lights = 3` reads as 2 (`from_ini`), `$B3_TUNNEL_LIGHTS` = xboxall / 3 as xbox, and the
renderer's all-lamps path (the route's lamps for it, `day_glow_mode` 3) was removed. The lamps' list (`xbox_lamp`,
DrawItem::day_glow 2) stays: it still decides their glow masks, which night mode's fixtures use.

What the Xbox draws, from its own files (the player's disc, read locally): its track textures carry the PS2's names and
UV layouts, and the light fittings' alpha is a **glow mask** painted over the lamp's lit part -- the tube of ATB_TunnelB,
R_tunnel_A, HK_Tunnel01 / 03 at alpha >= 128, the wall around it ~0 (mean 3-13), the masked texels 238-253 bright against
98-165; Chgo_TunLght's and ATB_LightDecals' alpha follows the tube's light (correlation +1.00 / +0.99). **The PS2 files
carry the same masks** (the same alpha, texture for texture), but the PS2 draws these surfaces opaque and its bloom is a
colour threshold (native-renderer.md §25.4), so they go unused there. The Xbox's bloom has the same two
parameters as the PS2's (Threshold, default 0.75, and Intensity, per track) and is a threshold too, so the masks act in the
Xbox's track shading: the masked texels lit at the texture's own colour, not darkened by the tunnel's prelight, which the
bloom then catches -- the tunnel lights that glow by day on the Xbox.

The Xbox looks draw the masked texels once more after the opaque world (emissive.frag mode 8 with the mask:
smoothstep(0.3, 0.7, alpha)) at 1.5 x the texel's colour (`kXboxGlowGain`, `$B3_XBOX_GLOW_GAIN` to tune it) (a white tube 1.4 in frame units, well over the route's bloom
threshold 0.65-0.75), opaque, without the Glow look's halos: the route's own bloom makes the glow, so its strength follows
each route's bloom as the Xbox's per-track values do. A texture has a glow mask (`glow_mask_of`, src/b3replay/xboxglow.h;
`DrawItem::glow_mask`, decided once per drawn texture: two textures of one name on a route are told apart) when its texels at alpha >= 128 cover 0.2-50 % of it, are 160+ bright (the brightest channel's
mean) and 30 brighter than the rest: not a cut-out (the HK lamp rig's wires: the masked part darker), not opaque, not a
broad mask over most of the texture. Only the listed fittings and lamps are drawn (a broad gloss mask such as
ATB_BridgeStone's is on no list). A fitting without a mask keeps the brightness ramp, and so does one drawn from an HD pack's replacement (whose alpha the GPU samples
need not be the mask).
- **Xbox** (named Xbox tunnels at first): the tunnel and underpass fittings (nightfix's list, `night_tunnel_fitting`,
  plus HK_Tunnel03).
- **Xbox all lamps** (withdrawn, above): those and the lamps outside the tunnels whose textures carry a glow mask (`xbox_lamp`; DrawItem::
  day_glow 2): the street lamps ATB_Lamp, ATB_LampostG, the toll plaza's ATB_Lamp_Toll and ATB_TollBooth, HK_post,
  HK_ResLite, the car park's HK_Midparking2 / 5, a lit window (Alleway7), the Riviera's street furniture (R_furnit3: its
  street lamps' heads and a traffic light's green lens -- the mask covers that one lens at half strength, and the 35 triangles
  that sample it are untinted, so a red or amber lens, another texture, never lights) and an LED speed
  sign (Freeway_info_speed). Picked from the 27 textures the PS2 draws opaque with a lamp-like mask (lit on at most 30 %,
  200+ bright), each looked at; left out: snow (ATB_Snow_Heaped, Snow_RockBlend, Snow_Grass_Blend: a mask of another use,
  no evidence the Xbox blooms it), pavement2b, the valley backdrop, building windows (WF_gaslamp4), and posters and posts
  with no clear lamp in the mask.
- **Glow** (nightfix's) and night mode's fixtures (mode 1) take the mask too where a texture has one, in place of the
  brightness threshold: ATB_TunnelB's "PARK" lettering beside its tube no longer glows.

No light pools by day: the port has no reader for the Xbox geometry to compare the baked light under the fittings, so
nothing is added there. The Xbox's per-track bloom values were not decoded; the routes' own bloom
values (the PS2's, the same parameters) do that job. Tests: `test_xboxglow` (the setting's value, the lamps, the mask
test on a lamp, a cut-out, an opaque, a broad and a dim mask). Shots: xboxglow-shots/.

## 36. Players' car mods and the prototype cars as extra cars, the CUSTOM and BETA series (customseries, 2026-11-22)

SINGLE EVENT's SELECT CAR offers a **CUSTOM** series after the traffic series: the car mods in the player's folder
`<user data>/cars/custom/`, each as a car of its own beside the retail ones, never in place of one; then a **BETA**
series: the prototype cars the launcher's Beta / Prototype cars import converted into `<user data>/cars/beta/` (its importer:
b3asset/betaimport, §39). (tools/native/extra_cars.h; the rules, tables and the folders' reading in extra_series.h,
tests/test_extra_series.cpp.)
What the game has [C the decomp]:

- **The vehicle list** is PVEH/VLIST.BIN as loaded (CB3Game +0x2A660 = D_005186A0, 0x1000 bytes; func_00133190's
  start-up): +4 the count (107), +8 s32 driveable[128], +0x208 s32 boost bar size[128], +0x408 u64 GtID[128]. A race
  car's model path is made from its GtID name (CreateVehiclePathFromIndex 0x251F40: "PVEH/" + its first four letters +
  "/" + the rest). GetClassFromID 0x251CB0 classes a car by those four letters (0..8; any other 9, which SELECT CAR's
  code takes for "no car": func_00318F50, func_0042CFA0). Its callers outside the list's own unit go through dispatch;
  GetCarID calls it within its unit.
- **A race car's file** (.BGV, 0x17): its LOD blocks at 0x16F0 (header +0x4C), its size at +0x08, its engine sound models
  at +0x16DC / +0x16E0. Car mods made from earlier builds carry 0x15 / 0x16 with this layout (the game never reads the
  word). The prototype discs' own files (0x14 / 0x15) have the LOD blocks at 0x16E0 and no sound models: another layout.
- **Ours**: each usable car gets a vehicle list entry (after the game has loaded the list, CB3Game +0x2C660): its GtID
  XCUS/CARn (n from 1 in the folder's order; room for 21), driveable, its donor's boost bar size. The entries are there
  only while needed: SELECT CAR on CUSTOM / BETA (stepped onto, or opening on a remembered extra car) and a race with an
  extra car in it; stepping back out of the series, and a request for a race without one, put the disc's list back byte
  for byte. (A list one entry longer costs every GetCarCount / GetCarID walk EE time: a normal race with a car installed
  but its series never opened was measured equal to one without, trace, DMA, SIF, sound and RAM.) Its donor is the
  retail race car it was made to replace: the folder it is in and its file name (`COMP/CAR7.BGV`), or a folder of its
  own with a `car.ini` (`donor = COMP/CAR7`, `name =`, `model =`, `hwd =` / `lwd =` for its own sound banks: plain file
  names in that folder). The folders are the player's, so untrusted: every file's size is checked before it is read (a
  car file at most 1 MB and the game's room for it, car.ini 64 KB); a car's own sound banks must be banks (at most
  512 KB, the header all 134 on the disc have) before the game is given them.
  - BETA's cars: one folder each (`NN-<key>/car.bgv` + `car.ini`, as the importer writes them; a folder being written is
    hidden, `.NN-<key>.partial`, and skipped), GtIDs XBET/CARn, series 11, the same mechanics as CUSTOM's.
  - GetClassFromID answers 10 (11) for an extra car; GetCarCount / GetCarID answer series 10 with the extra cars while
    SELECT CAR offers them (SINGLE EVENT) and class 9 with none (as the disc's list has); the carousel's steps, "can go
    right" and arrows go on to series 10 after the last series to its left (TrafficCars::series_end: the traffic
    series' last, or the carousel's own); its name and each car's name are string ids the game's lookup
    func_00271A00 never has (0xE000 on), answered in the game's text language ("CUSTOM"; a car without a name of its
    own: its donor's name with " MOD"); the race's pictures are the donor's; the carousel's small pictures are drawn
    from the car's own model (paint3's carousel_models.h resolver, chained; the renderer's View::thumb_file finds an
    XCUS / XBET name's file in the folders). The Crashbreaker's radius / strength and the AI catch-up series are the
    donor's (trafficseries' TrafficCars::set_extra_donors).
  - The disc path alias (platform_disc.h, now a list: trafficseries' and this one) serves PVEH/XCUS/CARn.BGV from the
    car's file (with its donor's engine sound models added when it has none, as trafficseries' traffic_model, cached in
    `cache/extra`), .HWD / .LWD from its own banks or its donor's. The renderer finds the model by its header bytes in
    the same folder (view.cpp scan_cars).
  - Its handling is its donor's: every per-vehicle file it registers (AddEntry) registers under its donor's
    (VehiclePhysics, Camera, VehicleAudio, VehicleMaterial, AI); its hull, wheel and panel frames are its own. Rivals of
    an extra car are race cars of its donor's class (RIVALS' CARS' own pick is untouched: "Same as yours" gives them
    the extra car).
  - Refused, with the reason logged (and shown by the launcher, which reads the folders the same way): a file not named
    after a race car, another version or an old-layout prototype file (carmods' b3asset/car_layout.h; the latter
    pointed to the launcher's Beta / Prototype cars), a file larger than the game's room for a car (0x72000) or whose header's size is
    not its own.
- **The launcher**: Game Data's **Custom cars** (after Prototype cars): each car with Remove, the files left out with
  their reason, **Import from folder...** (a car mod's folder: its car files named after the car they replace in a
  folder named after its class, or its folders with a car.ini: only their car's files, checked and copied into
  cars/custom; symbolic links never followed; a car already there refused: extra_import),
  Open, Look again. The **Showroom** lists, after the disc's player cars, TRAFFIC and TRAFFIC SPECIAL (the traffic
  vehicles' .BTV, named by the program's traffic table D_004CDD40 read from the player's SLUS_210.50), then CUSTOM and
  BETA (their paints as the files have them), and notes each extra car file it leaves out.
- **Only Single Event**: every other mode's request (RequestNewGameMode 0x30D1B0) races an extra car left in a choice
  slot as its donor. **The save** holds no car GtIDs; an extra car's index is -1, as a traffic vehicle's (trafficseries'
  notes), and the race does not count for the profile (0x136F30). Measured: a race in a custom car driven to its finish
  and through the results leaves the card byte for byte as it was (the game has nothing to autosave), where the same
  race in a retail car autosaves. The list's only readers are its own methods (B3DataLists.cpp); IsCarUnlocked takes an
  id outside the 67 race cars as unlocked.
- **Records**: none with an extra car as the player's car (records_guard.h); bit 29 of the record word
  ("options-extracars", one folder with the race options).
- **Replays** record the cars in their header (`extra_cars`: each slot's donor and a hash of its file) and play only
  with the same cars in the folder; a recording without the entry plays without them. With no usable car in the folder
  nothing is installed: the game and its vehicle list are as before. B3NATIVE_CUSTOM_SERIES=0 takes the series off;
  tests use B3_CUSTOM_CARS / B3_BETA_CARS (the folders) and B3_EXTRA_CARS="player=XCUS/CAR1,rivals=XCUS/CAR1"; race_check
  SCENARIO=customseries (CUSTOM_RIGHTS=10: on to BETA).

### 36.1 Car packs, and CUSTOM in pages (carpacks, 2026-11-22)

**Pages.** A series shows at most 20 cars [C func_0042B7C0: 20 rows], and the vehicle list has room for 21 more than the
disc's 107. CUSTOM and BETA come in pages of 19, each a series of its own on SELECT CAR (CUSTOM, CUSTOM 2, ...: series
codes 10, 12, 14, ...; BETA 11, 13, ...: codes only the port's wrappers see), up to 200 CUSTOM cars (11 pages). The
vehicle list holds only the cars of the page on screen (stepping onto it, or SELECT CAR opening on a remembered extra car)
or of the race (the request's slots), and the remembered extra cars (SELECT CAR's own D_01E94EF8 and the choices'
players 1 and 2, +0x80 / +0x88: a page of 19 and the player's two fit the 21, so no menu looks up a remembered car the
list lacks; rebuilt when one changes, as when a save is restored), and the disc's list again otherwise, byte for byte.
Outside SINGLE EVENT the request demotes every slot and SELECT CAR's own to the donor, so such a race has the disc's list. Measured with a 66-car pack (4 pages):
at each change the game's own DoesVehicleExist 0x252000 / GetVehicleIndexFromID 0x251E60 find every joined car at its
entry and none of the others (B3_EXTRA_CHECK=1 logs it); a race in a page-3 car; with the cars installed and never
opened, a race and SELECT CAR equal to the game's own (trace, DMA, SIF, sound, RAM). For 200 cars the folders are read
from each car file's header and size (validation), the file hashes kept between starts (cache/extra/hashes.txt), and the
renderer keeps an extra car's file header-only until it is drawn (CarFile::full).

**Import car pack...** (the launcher's Game Data, Custom cars; src/b3launcher/carpack.h, tests/test_carpack.cpp) takes a
pack made to overwrite the disc's car files -- `<pack>/PVEH/<CLS>/CARn.{BGV,BTV,HWD,LWD}`, as a .zip or a folder -- and
makes the cars it changes cars of their own. Nothing in the game is duplicated (the owner's rule):
- a file identical to the player's disc file at its path: left out ("already in the game");
- a car that is one of the BETA series' cut cars, by its geometry (include/b3asset/carprint.h car_fingerprint: the body
  at LOD 3, its vertices on a 2 cm grid, hashed; the same for the BETA import's conversion of the disc car and a pack
  author's own conversion: measured on a pack's nine): into BETA as that car (betaimport's folder, name and donor,
  car.ini with a `pack =` line), once ("already a BETA car" when it is there; a disc import later replaces the pack's);
- a file already imported (the same bytes, from any pack or folder): left out ("already imported");
- an old-layout prototype file: Beta / Prototype cars' converter (betaimport);
- any other changed car: CUSTOM, `cars/custom/<pack>/<CLS>-CARn/` (car.bgv, car.ini: its name, the slot's car as its
  donor, the pack's engine banks when they differ from the disc's).
The pack's vehicle list and VDB.XML (handling, cameras) are never applied (listed as left out). Before anything is
written the launcher shows the pack's cars by group, each group a toggle: cut cars into BETA, cut cars, new models (on),
early versions and new paint (off). Names: a table of car names keyed by the file's FNV-1a 64 (one known pack, 59
cars; a car of the game in new paint takes " (<pack>)"), else "<the slot's car name> (<pack>)"; <pack> is the player's
own zip or folder name, so no pack is named by the port; up to 32 characters. A repaint of a cut car is that BETA car
(the same shape), not a car of its own. A race's extra cars are hashed from their files when a replay is recorded;
playback with other files is reported as parted from the recording. Installed packs are listed one
line each, with Remove pack.

## 37. START AT, IDLE PAUSE, END VERY LONG EVENTS, the Crashbreaker's Full blast, the CAR VIEWER (gamemods1, 2026-11-21)

What the game has that these build on [C the decomp]:

- **The boot**: the loading page's Prepare func_00321780 hands the front end its first menu with func_00130C20
  (CB3FrontEnd 0x0051A6A8, the intro sequence 0x94413E2EEC056247: the films englis30, cri_rw30, Titles30, then the
  title 0x60D6B07214065F27). The title's Start (func_00438840, 2 s after it comes up) reaches the flow event 3 of
  func_0030E930: with the boot flag D_0051A998 set it leads to the profile choice; the profile page loads a profile
  with func_00421000 on the memory card controller 0x01ECD520.
- **The idle pause**: CB3OfflineStageLogic::Update 0x395490 asks for the pause (the request CB3Game +0x2DA58, taken by
  CB3Game::UpdatePreSimulation) when both players' pads have been idle more than 300 s (GetDeadInputTime 0x22BAE0:
  the least of a player's controllers' times since their last input, 10^8 for a player with none). An empty port still
  has its controller: its stamp is the menus' last reset (CB3InputManager::ResetDeadInputTime(-1)), taken on the game's
  clock D_004F50A0, which restarts with the race; so with one pad the pause opens once player 1 has been idle 300 s
  and the race clock has passed that stamp by 300 s (on the PS2 as in the port).
- **The long events**: func_0019A550 (the world's update) finishes every car still running once the world time passes
  1800 s (2400 s with D_01E65499), unless the world is a replay; func_0038B160 does the same outside World Tour once
  1500 s have passed.
- **The Crashbreaker's blast** func_001B2BA0 crashes the traffic in its radius and air-rams the crashed traffic
  (the physics manager's list +0xE6100); race cars are in neither. A ram marks its attacker on the rammed car's
  attacker's side (car +0x1598 + 4 x index = the clock, +0x15B0 + index = 1), which is what makes a wreck an Aftertouch
  Takedown.
- **The car viewer**: the retail game keeps a debug game mode, CB3CarViewerGameMode (one-player choice 4; Prepare
  func_0024FC20, Update 0x24DAB0, Release func_0024D4D0, Render 0x24D520), that no menu reaches.

What the port does:

- **START AT** (GAME > STARTING AND LOADING, ini `boot_to`, row 470; tools/native/boot_boost.h): Intro films (the
  game's) / Title screen / Main menu, profile 1-3. Handing the boot the title's id shows the title at once, but in the
  port the title's Start then leads nowhere, so the port keeps the game's intro sequence and ends each of its films as
  it starts (the movie skip's own CGtVideoDecoder Abort): the title is up at VBlank ~530 instead of ~4650. For a profile,
  it then presses Start on the title for the player (in player 1's report, so a replay holds it) once the title takes
  it, and at flow event 3 loads that profile as the profile page does: the main menu at VBlank ~650 instead of ~16350.
  A profile the save does not hold (read from the save file: src/native/save_stats.h) is not loaded: the game's own
  profile choice shows (the game would load a blank profile and save it; a damaged save reads as none). Replays record
  the setting and whether the save held that profile at the boot (`boot_profile`), so playback never reads the local
  save.
- **IDLE PAUSE** (GAME > GAMEPLAY, ini `idle_pause`, row 471, default on = the game's; tools/native/game_limits.h): off,
  the dead input time reads 0 inside CB3OfflineStageLogic::Update only (every other reader is the game's).
- **END VERY LONG EVENTS** (GAME > GAMEPLAY, ini `long_event_end`, row 472, default on): off, func_0038B160 does nothing and
  func_0019A550 runs with the replay flag set for that call only once past the limit (its end check is the flag's one
  reader in the update). Only in Single Event and the two-player game (game_options_stage, as GAMEPLAY's other
  options): World Tour and online keep the game's limits. An event kept going past the limit keeps no game records
  (records_guard.h) and does not count for the profile (0x136F30 answered no).
- **CRASHBREAKER IN EVERY MODE: Full** (row 431's third value; game_options.h blast): outside Crash mode the race cars in
  the blast too (each one not crashing yet crashed through StartRaceCarCrashing, the exploding car marked as its
  attacker, so the wreck counts as an Aftertouch Takedown; the crashing ones thrown by the blast's own air ram, their
  physics put in the manager's list for that call). GameOptions::full_blast_active() is true during those crash calls, so
  another wrapper on the crash start (the party modes') leaves them alone; only a player's own Crashbreaker blasts this
  way (an AI car's, Mayhem's, stays the game's). It reads the game's six race cars.
- **The Crashbreaker's prompt on the race HUD** (with the option on, either value): Crash mode's crash style component
  (CB3HUDCrashStyle 0x0066AFE0 / 0x0066C0A0) prepared on each race HUD after PrepareCrashComponents and shown while that
  player's car crashes with the breaker ready (its update hooked; the layers hidden otherwise rather than its objects
  taken down).
- **CAR VIEWER** (tools/native/car_viewer.h; no setting): on YOUR GARAGE (its Update func_003F03D0 reads Back and the
  D-pad only) Select, when let go, or L2 + R2 + Square opens the game's own mode: the menu state to come back to into
  CB3FrontEnd +0x300, the page released, the carousel's car as CB3MenuChoices +0x80 and choice 4, RequestNewGameMode
  0x30D1B0. Its Prepare is followed by the set-up the mode expects of the player's race car 0x0064D3B0 (its physics body
  pointer +0x2430 = 0x0057BAF0 and back, its lighting terms +0x5C..+0x7F, our own soft light; the boost flame off) and
  shadows / bloom / blur off; its Update runs with the sound game mode manager's prepare state (0x01E77540 +4) held at
  0 (its whole value as the first Prepare found it put back at Release, with the graphics flags and CB3MenuChoices +4 /
  +0x80). Leaving runs no records, profile counts or autosave (the save card is byte-identical to a run without the
  visit). A request to open it is an event of a replay (`car_viewer`); playback opens it on those only. The game's own controls are the viewer's (lights, wheels,
  hinged parts, deformation, colour, Select back); D-pad left (the background colour) and right (the deformation reset,
  CB3VehicleDeform::Reset 0x3DAEE0) are the port's, on two buttons the mode leaves free. Drawing: its Render is not the
  front end's, so each frame its Update hook hands the car to the HUD capture as a FECAR event of its own (index 0x100 +
  the background, src/gameview/hud_capture.cpp b3gv::car_viewer_frame); the renderer draws that car alone with the
  frame's camera, on the background colour (src/b3replay/view.cpp).

With the defaults every call goes through unchanged (the golden).

## 38. Weather: rain (weather, 2026-11-22)

(1.4.0: rain ships as an experimental option, SETTINGS > EXTRAS > EXPERIMENTAL > RAIN, per race and in World Tour:
§45. What follows is the state as merged.)

Rain, drawn only, held back with night mode until the owner releases it: the WEATHER row (GRAPHICS > LIGHTING, Dry /
Rain, ini `weather`, row 650) is not shown and the setting is not read; `B3_WEATHER=rain` (or `1`; `dry` / `0` off) turns
it on for every race, `B3_WEATHER_ROW=1` gives SELECT OPTIONS' TIME OF DAY row its rain values (tests). Nothing reaches
game memory: physics, AI, records and replays are the game's (a replay records the race's choice and draws it again).

**What the game has.** No weather in the retail Burnout 3: no rain or snow code, particles, wet textures or rain sound.
The prototype's environment manager keeps a leftover weather intensity (0..1, asserted) that the retail Update
(0x2BC460) no longer has; Burnout 2 had rain and snow in its effects manager and "conditions" on its track select, and
the Revenge prototype's weather managers are empty stubs. The physics has no wet path: the collision surfaces add only a
speed-scaled drag off the road (CB3RaceCarPhysics::DoPhysicalEffects 0x1BF030), no grip per surface. So there is no
wet-grip option (it would be a gameplay change of our own).

(condrows, 2026-11-23, the owner's: the one CONDITIONS row below became two, TIME OF DAY (Day / Night, with EXTRAS'
NIGHT) and WEATHER (Clear / Rain, with RAIN), over the same value 0-4: each row sets its half (race_choice.h
RaceRowModel::set_conditions; RaceRow::kWeather after MORE; its strings in a block of their own after RIVALS' CARS'). Both
fit: TIME ATTACK's one page has exactly the 9 rows, RACE's and ROAD RAGE's last pages room for both. The paragraph below
is the state as merged.)

**CONDITIONS per race.** SELECT OPTIONS holds 9 rows (the game's component), so rain is not a row of its own: with the
rain values TIME OF DAY becomes CONDITIONS, Day / Night / Rain / Night rain (tools/native/race_choice.h, the race's
value 1-4; race_time.h kRaceTimeRain 3, kRaceTimeNightRain 4, after the old values, so a recording's 0-2 play back as
before; the host record's race byte carries 0-4). "(Default)" marks the settings' own pair (TIME OF DAY and WEATHER); a
value too long for the page's 23-unit string slot with it takes the language's short tag. The renderer's
`View::rain_on(f)`: the race's 3 / 4, else WEATHER.

**The look** (`View::rain_frame`, `WeatherLook`; rain.h; mesh.frag):

- **Overcast by day**: the night grade's pass (b3gfx Night with `lit` false: none of the night's lights) with day
  values: the world desaturated (0.62) and a little darker and cooler, the baked sun's contrast softened (flat 0.35), the
  sky dome and backdrops (whose pictures carry the painted sky) pulled 75 % to their own grey (DrawItem::overcast); no sun
  corona, lens flare, sun shadows or car glint; a nearer, thicker grey fog (the game's curve at 1.8 x the distance, at
  least 0.8 far away). TUNNEL LIGHTS' day glow draws after the grade.
- **Night rain**: the night's grade without moon or stars, a dull sky lit a little by the town, weak moonlight.
- **Tunnels**: inside, the rain's fog and its day grade fade out by the night's tunnel signal (the lit tunnel is dry).
- **Falling rain** (rain.h `Rain`): 3,200 streaks in a 16 m round volume around the camera, anchored in the world (a
  drop keeps its place as the camera moves) and falling at 9 m/s with a little wind; each streak is the drop's path
  relative to the camera over 24 ms (short and near-vertical standing, long and slanted at speed), a thin quad with the
  corona texture in the corona state (additive, Z tested, after the grade). None under a roof: the night's cover grid
  (built at the load in the rain), a vertical ray per 2 m column and 2 m of height, cached. The camera's speed is its
  movement since the last frame; in an isolated frame (an offscreen shot) the nearest race car's, from its wheels.
- **The wet road** (mesh.frag, FrameParams::Surface::wet): the upward, grey ground of the opaque track (asphalt,
  pavement, kerbs; not grass or paint) darker (x 0.55) and mirroring the sky by a Fresnel term, (0.03 + 0.97 (1 - n.v)^4)
  into the frame alpha, which the game's own full-screen specular turns into the overcast's colour (ROAD SHINE off: no
  sheen). A draw's share under a roof (DrawItem::dry: up to 24 of its triangles tested against the cover grid, at the
  load) keeps it that much dry.
- **Lights on the wet road** (rain.h `wet_reflection`): every light's streak lying on the road below it (the corona
  array 0: head, tail and brake lights, signals; the lamps' heads and fittings), from behind its foot towards the
  camera, longer the higher the light, brightest at grazing angles; none on a covered road. The ground comes from the
  cover grid (the highest triangle below). The cars drive with their lights on in the rain. Their tail lights glow and rise with the brake as at night (§35, taillights).
- **Spray** (rain.h `Spray`): puffs streaming back from each car's rear wheels (a race car's two rearmost captured
  wheels; traffic: two points from its matrix), rising and spreading, denser with speed (8 m/s none, 40 full), the
  game's tyre-smoke texture (GLOBAL.TXD fxsmoke) as mist ((Cs - Cd) As + Cd, graded with the world). None under a roof.
  A race car's speed is its captured wheel speed x 0.33 m (VehicleParams +0x4E8 is the speed over the wheel's radius).

- **Glints in the light pass** (beam.frag, `tun.w` the wetness): each street lamp's and headlight's mirror lobe on the
  upward ground it lights, towards the camera; none when dry (night without rain is unchanged).
- **Drops on the lens** (shaders/drops.frag, FrameParams::Drops, rain.h `lens_drops`): up to 16 water beads over the 3D
  picture before the 2D stage, each the scene behind it flipped and magnified, a darker rim and a highlight; stateless
  (each slot its own 1.5-4 s cycle from the frame's time: a replay shows the same drops), running outward at speed, none
  inside a tunnel or with a free camera (photo mode); `B3_RAIN_DROPS=0` none. They read the frame copied at the grade's
  split (the rain always has it) and grade it as night.frag grades the world: no break of the scene pass. So a drop shows
  the world without what is drawn after the grade (the lamps' pools, coronas, sparks).
- **The sound** (rain_audio.h; nativeaudio.cpp): the port's own, no outside audio: a stereo noise bed (300 Hz - 6 kHz;
  1.2 kHz and quieter inside a tunnel), sparse drips (none in a tunnel), tyre hiss rising with the followed car's speed;
  every gain moves over ~0.35 s; the bed about -30 dBFS RMS at EFFECTS 100 %. Mixed into the frames b3render plays
  (NativeAudio::push: after the game's mix, no frame added), never into the game's mixer, so nothing the game sees
  changes and offscreen runs stay silent. The game's samples are never changed: the rain is held so the sum stays within
  the platform limiter's knee (29000, SOUND OUTPUT Clean's ceiling) and adds nothing where the game is past it (Original's
  loudest moments), at any RAIN VOLUME; a non-finite input counts as 0 and resets the generator.
  On while the race's clock runs (the in-game pause mutes it). Its level: RAIN VOLUME (held back: 100 %) x the game's own
  EFFECTS volume, which native_play reads each VBlank from the game's settings bytes [C func_0028B380: D_004F5074 x 0.01
  to the effects' mix; the AUDIO page's row 0] and hands to b3render (b3r_effects_volume). `B3_RAIN_VOLUME` (0-200)
  overrides RAIN VOLUME; `B3_RAIN_AUDIO_DUMP=<file>` (checks) writes the mixed frames even offscreen.

- **Under cover** (tunnels, bridges, overpasses, roofs): no streaks fall there (each drop's vertical test), no spray,
  the road dry by each draw's share under a roof; and with the camera under a roof (`View::rain_cover_frame`: five
  vertical rays round the camera against the cover grid, smoothed ~0.3 s in and ~0.5 s out, so a bridge's shadow or a
  lattice does not flicker it) no new lens drops (those on the lens fade over their own life) and the sound muffled: the
  bed at 55-100 % and the drips off by the tunnel's share or 0.7 x the cover, back on leaving. `B3_RAIN_AUDIO_LOG=1`
  prints the sound's state as it changes (on, cover, tunnel, the bed's and the drips' levels).

Split screen: each view keeps its own rain, light streaks, spray and cover (View2State). Defaults: `B3_WEATHER` unset, every new
value is 0 and no extra draw or mix runs: the golden record is byte-identical.

- **Puddles** (mesh.frag's wet block): about 15 % of the flat wet ground (world-anchored value noise at ~3 m and ~0.8 m),
  over the lane markings too (a puddle covers the paint), not on grass, none under a roof (the draw's dry share): darker
  (x 0.3) and a near-total mirror of the sky by its own Fresnel, so they read as bright sky patches by day and dark
  mirrors at night; the light streaks and glints land on them. No shimmer at a distance: as a pixel's footprint grows the
  fine octave gives way to its mean, the edges widen and far away a pixel takes the average cover.
- **A wet car** (car.frag, `moon.w` the wetness): the clearcoat mirrors more of the sky (a Fresnel gloss over the paint),
  and beads of water lie on the paint and the glass: 4 cm cells in the part's own space (car.vert's part position and
  normal, on the plane the face is turned to: they stay on the body as it moves, whatever its texture mapping), more on
  faces turned up, each a soft glint and a darker rim, fading where a bead would be smaller than a pixel. Dry, the
  results are unchanged (the car shader still takes the beads' coordinates, two derivatives and two more varyings: a
  tiny cost, not none).
- (The lens drops at night: mostly clear (alpha x 0.3) with their highlight kept, since the copy they read lacks the
  night's lights.)

**Still to do**: screen-space reflections on the wet road (after the scene depth target, scenedepth).

## 39. Prototype cars: the BETA series import (betaimport, 2026-11-22)

Cars cut from the game, from the player's own disc image of the June 8, 2004 PS2 prototype or the June 21, 2004 PS2 demo,
converted into car files the game takes; the BETA series on SELECT CAR (the extra-cars code) races them.

- **The converter** (`b3asset::convert_prototype_car`, `include/b3asset/carconvert.h`; the layout in
  docs/modding/file-formats/car.md "The June-prototype disc's own files"):
  - it finds the old file's data end by walking every structure the game's FixUpVehicleData 0x220B70 reaches (the
    files are padded to 32 KB);
  - it inserts 0x100 bytes at 0x16E0 (the header's two engine-sound offsets and padding: every packet and the
    texture keep their 256-byte alignment) and moves every file-start offset FixUp relocates;
  - version 0x14's LOD blocks hold their list offsets from the file's start: they are rebased onto the block;
  - it appends the donor's engine-sound structures (func_002FB240: offsets from themselves, signed), sets the version
    to 0x17, pads to 2 KB, refuses a result over the car memory block (0x72000) and checks it with car_file_error.
  The hull (offsets from the hull) and the texture (from the raster; version 0x14's 4-paint CLUT block is a raster
  form the game reads) need nothing. One June 21 file's +0x64 list counts a second entry of stray bytes: it is left
  out of the count (the game would turn it into a wild pointer). Of the two discs' 132 car files, 130 convert and read
  at every LOD; COMP/CAR7 and CUPE/CAR9 of the June 21 demo are ~1 KB over the car memory block with the engine sound.
- **The import** (`b3asset::beta`, `include/b3asset/betaimport.h`): the disc is known by VEHICLES/VLIST.BIN's FNV-1a 64
  and its cut cars' files (the April and May 2004 demos share the June 21 car list without them). The 11 cars are a
  table: each by disc, path and file hash, with a donor (the game car with the same place in the car list; the
  delivery van the B-Team Van, as the TRAFFIC series), a series and a name (the discs have placeholders only). Each is
  converted into its own folder, `<user data>/cars/beta/NN-<key>/` (car.bgv and car.ini: name, donor, model, and the
  import's key, source, from, size, hash), the layout the extra-cars code reads for CUSTOM and BETA, in folder-name
  order; a folder is written whole (a hidden .partial folder, then renamed). The June 8 muscle car is the June 21 one's
  earlier version: the later one replaces it.
- **Car files and folders** (the owner): `b3asset::beta::import_files` / `import_folder` (a folder walked recursively,
  at most 4,000 entries, the output folder skipped; .BGV / .BTV only, other files counted). A file whose FNV-1a 64 is a
  table car's is that car; another old-layout file takes the donor its path names (`.../HEVY/CAR4.BGV` -> HEVY/CAR4,
  `donor_from_path`), is named "<class> <n> (prototype)" and goes to `50-<class>-<carN>-<hash>`; a path that does not
  say, or a donor the game has no file of, comes back in `needs_donor` for the player to choose (`FileDonor`,
  `donor_choices`: the class's PVEH/<cls>/CARn.BGV files). A file of the game's layout is refused as a mod car (the
  CUSTOM series); every read has the size ceilings above.
- **Car files from a car pack** (betabytes, for gameopts' "Import car pack..."): `import_bytes(rel, bytes, game,
  out_dir, donor)` takes one file's bytes and its path in the pack (`HEVY/CAR35.BTV`) and does what `import_files` does
  with a file (`needs_donor` then holds the pack path). A traffic vehicle's .BTV at a slot the TRAFFIC series offers
  (COMP CAR11-19, HEVY CAR11-35: not the trailers or trams), there or in a file import, takes trafficseries' donor race
  car (tools/native/traffic_series.h; HEVY/CAR35 -> HEVY/CAR5) and is named by its slot ("Heavy 35 (prototype)") instead of
  asking; a donor the player chose is never replaced by it.
- **The launcher** (the owner's layout): Game Data > Beta / Prototype cars: Import from disc... (an .iso, raw .bin or
  .chd, read by b3asset/discfs.h) / Import from folder... / Import files... (several at once) / Open / Remove, one line
  per imported car (its name, its source); the Custom cars section follows in the same form. The import runs on a
  thread, its step in a modal, the result in words; for a file whose car is not known, a question: its class, then its
  game car (the modal's buttons wrap into rows), then those files are imported with the answers.
- **In the game**: a converted file served in place of the game's (the mod overlay) was loaded, fixed up, drawn on
  SELECT CAR and driven (race_check heavyseries). The BETA series itself (its cars as extra cars, their names,
  pictures, records apart) is the extra-cars code's, reading the car folders.
- Tests: test_carconvert (synthetic old-layout files of both versions read back as the game-layout car; every cut
  input refused; AddressSanitizer), test_betaimport (synthetic ISO images, test tables), the launcher's game_beta shot.

## 40. Back keeps the game's settings changes (settingsback, 2026-11-22)

**SETTINGS BACK BUTTON** (PC SETTINGS > GAME > INTERFACE): Keep changes (default) / Cancel (the game's). The game's
DRIVER DETAILS > SETTINGS pages -- GAMEPLAY (func_0045BB10 / func_0045B920), AUDIO (func_003F2FE0), VIDEO
(func_003F3E60), CONTROLLERS (func_003F3670) -- run under one menu flow handler, func_0045C140 [C, matched]: when a page
opens (action 3) it copies the settings bytes (D_004F5040, D_004F506C..D_004F507F: TAKEDOWN CAMERA, CRASH MODE IMPACT
TIME, the volumes, the screen position, the controller's settings); left with confirm (action 5, code 1) nothing more;
left with Back (code 3, and event 0x4A that the page's Back raises first) it puts the copy back -- a cancel -- and then
applies the settings (0x4B, func_0021B4F0 / func_0021B5E0). Players took a change left with Back for one that does
not work (CRASH MODE IMPACT TIME: Automatic). tools/native/settings_back.h: with Keep changes the two cancelling calls
are made as the keeping ones -- 0x4A as 0x4B (applied, not put back), 3 as 1 then 0x4B (the same scene, applied).
Cancel leaves every call alone. Menus only: no race code changes, only which settings the player ends up with.
B3NATIVE_SETTINGS_BACK=0 / 1 overrides the row; the golden runs pin 0 (their tours leave the pages with Back); a replay
records "settings_back" (a recording without it plays with the game's cancel). Tests: test_settings_back (the calls),
test_settings `settings_back_row`.

## 41. The scene's own depth instead of the depth pre-pass (scenedepth, 2026-11-22)

Night's lights (§17.2, §35), the AMBIENT OCCLUSION (§18) and photo mode's depth of field and motion blur read a
single-sample depth of the frame. SDL GPU can neither sample nor resolve the scene pass's multisampled depth, so until
now they drew the frame's Z-writing draws a second time, into a depth pre-pass of their own. With the scene depth on, the
scene pass keeps its depth itself, in a second colour target, and draws no pre-pass.

- **Switch**: by default (1.4.0, nightdepth) on for night's frames and off by day (the pre-pass, as before:
  `scene_depth_mode` kNight; the frame's choice, so a night race after a day race needs nothing re-made: the device
  prepares the two-target pipelines at start), in `pc_settings.ini` `scene_depth = 1` for every frame (ini only, no page
  row; 0 off, -1 the default) or `$B3_SCENE_DEPTH=1` (which wins; `0` forces the pre-pass). Measured at night on an RTX
  4080 SUPER: 288 fps with the pre-pass, 389 with the scene depth (+35 %). The default is a per-backend choice in code (`include/b3gfx/scene_depth.h` `scene_depth_default`), so a
  backend where it measures faster (a tile-based GPU, Metal on Apple's: a second colour target is nearly free there and a
  second geometry pass is not) can have it on. When it is on, the two-target pipelines are made with the rest at start
  (and with any pipeline made later), not at the first night, AO or photo frame. An R32F colour target the device cannot
  make at the scene's sample count turns it off (the log says so). The AO's break blends R32F (MAX, below), which SDL
  3.4 cannot query: by backend (`scene_depth_blend`: Direct3D 12 and desktop Vulkan GPUs yes; mobile-class Vulkan GPUs
  and Metal, whose Apple GPUs do not blend 32-bit float targets, no), and without it a frame with the AO keeps the
  pre-pass while night, the depth of field and the motion blur still take the scene depth. A two-target pipeline that
  cannot be made is never replaced by its one-target form inside the pass: its draws are left out of that frame and the
  later frames take the pre-pass (the log says so). The state is the device's own (`Device::sd_`; a process can hold two
  devices, the launcher's previews and showroom), found by the SDL device when a pipeline is made and through the
  drawing thread's frame (`t_sd`) in the scene pass; pipelines are released only with their device, their two-target
  forms with them.

- **The target**: R32F at the scene's sample count, cleared to 0 (reverse-Z far). The draws the pre-pass drew (not the
  2D, the effects, the sky or a blended track draw other than the post-fog stage's; every car draw, glass too) write
  gl_FragCoord.z into it through SCENE_DEPTH forms of the track, sun-shadow-receiver and car fragment shaders
  (`mesh_depth.frag`, `mesh_shadow_depth.frag`, `car_depth.frag`; mesh.frag's flag: FragmentUniforms light_dir.w = -1).
  Every other draw has it masked off. Those draws also write Z, so the last fragment written is the nearest.
- **No change to the scene code**: on a frame that reads the depth, every pass begun on the scene's colour target gets
  the second target (cleared at the first begin, loaded after; resolved by SDL's colour resolve at each end with MSAA),
  and every pipeline bound in it is swapped for its two-target form, made on first use from the create info kept for
  each pipeline (b3gfx_sdl.cpp `SceneDepthState`, `perf_create_pipeline` / `perf_begin_pass` / `perf_bind`). Off, or on a frame
  without a reader, nothing of it runs: no target, no other pipeline bound, the shaders as before.
- **When it is read**: night's lights at the grade's break (phase 5), the depth of field and the motion blur at the 2D
  stage's breaks (phase 6), as before. The AO used to run before the scene pass; it now runs at a new break after the
  opaque phase (the pass's colour and depth stored and loaded again). The post-fog draws are drawn into the depth target
  only (no colour, no depth test; MAX blend) at that break first, as the pre-pass had them. After its last reader the
  target is no longer stored or attached.
- **MSAA**: the pre-pass is single-sampled, so at a silhouette pixel it holds the surface at the pixel's centre (in effect
  the majority surface); the scene depth's resolve averages the pixel's samples, a mix of the two surfaces' depths
  weighted by their coverage. The readers see that as about one pixel of difference along silhouettes. A fix pass
  (`sdfix.frag`: where the 3 x 3 spans a discontinuity, the nearest or the closer of nearest and farthest) was measured
  against the pre-pass's pictures and made every form worse (the readers' own filtering handles an averaged edge
  better than a snapped one): it stays opt-in, `$B3_SCENE_DEPTH_EDGE=<share>`.
- **Pictures** (offscreen, US/C2_V2, night + AO, against the pre-pass): MSAA 1 identical (0 pixels at four polls); MSAA 4
  1-pixel differences at silhouettes, 0.4-2.3 % of the pixels by more than 2/255, mean 0.11-0.44/255.
- **Cost**: in the software renderer (1280 x 720, MSAA 4) night costs +29..+33 ms instead of +75..+105 ms (the light
  pass +5..+8 instead of +56..+83: the second geometry pass was most of it). On an RTX 4080 SUPER (native_play.exe
  offscreen, MSAA 4, the same frame 15 x per form, medians) the pre-pass was cheap and the two are about even: night
  +0.30..+0.48 ms instead of +0.46..+0.59 at 1080p, +1.20..+1.55 instead of +1.08..+1.56 at 4K; AO +0.22..+0.25 instead
  of +0.36..+0.56 at 1080p, +0.92..+1.35 instead of +0.95..+1.16 at 4K (+-0.2 / +-0.4 ms of noise). In split screen
  (night, the mean of 24 frames per shot, noisier) 9.6-12.9 ms a frame instead of 10.4-11.9 at 1080p and 19.0-23.1
  instead of 15.9-19.5 at 4K: the reason it is off by default until measured in a real window per platform.
- Round 3e's opt-in MSAA-1 depth copy and the night pre-pass's distance cull are removed (the scene depth replaces both).

## 42. MORE RIVALS: up to 8 cars in a RACE (morerivals, 2026-11-21)

SINGLE EVENT RACE's own RIVALS row goes up to 7: 8 cars (tools/native/more_rivals.h; its EXTRAS row off by default since §45; inert unless a race
has more than 5 rivals; `B3NATIVE_MORE_RIVALS=0` is the console's game). The game's car count (6) is designed into fixed
per-car arrays inline in its objects, so there is no one number to raise [C the decomp]:

- **The arrays**: tools/decomp2host/split_census.py finds every access in the generated code (about 200 units: struct
  fields named by offset, guest array globals, raw byte offsets; the classes from the mangled self, the guest global's
  address, seeded members). config/split_arrays.tsv names them and what each needs: `split` (the score's per-opponent
  near miss / takedown state, each car's race distances, the world's car list, the physics manager's near-miss rows, the
  traffic system's per-car buckets and distances, each traffic vehicle's distance per car, the start slots ...),
  `pool` / `flat` (memory after the array that the extra cars extend: the AI car and physics body pools run on into the
  network ones, unused offline; the physics manager's per-car table is the head of its active-body list). decomp2host's
  last pass (tools/decomp2host/split_arrays.py, DECOMP2HOST_SPLIT_ARRAYS) gives the first `split_array<T, 6>`
  (include/b3host/split_array.h): element 6 + k lives in a shadow of the array's own element k 32 MB up (33 MB reserved
  after guest RAM at boot, no-access like a guard, committed at the first race with more than 5 rivals: until then the
  console's 32 MB exactly), so it has a real guest address and two views of one array meet; with no race of more than 6 cars every index
  is the flat memory it was. The others get `flat_array` (an index the compiler cannot bound: clang has no
  -fno-aggressive-loop-optimizations). The compiler checks the census: no decay to a pointer, every other use fixed
  by a listed rule.
- **The race**: the stage record 0x0051AD00 carries the AI count (+0xA4) from the page's +0x6C0 (the game's own row, its
  maximum +0xCA raised to 7 by raceopts' page hook). func_001346C0 (the record to the world) is where the extra AI cars
  are made: their ids and colours (rivals 3 and 4's cars, another paint: at most 6 models, the physics attribute table's
  and the engine sound banks' limit), the AI car and physics objects in the network pools (the first setup's bytes: the
  vtables come from static init), the car models (memory block 1, 456 KB each) in the arena's last MB, which only
  layout 2 uses; the start spots (more_rivals_grid.h, tests/test_more_rivals.cpp): behind the last rank, else ahead of
  the front rank, inside a drivable AI lane segment, along it, 4 m clear (tools/native/more_rivals_grid_check.sh: the
  36 routes); engine sound: two more CB3SoundAICar / CB3SoundESMLow when the game's 5 are taken; the traffic system's
  per-car infos (scratchpad) moved to guest RAM; CalcDifficultyIndex's 6-car stack array replaced; RenderRaceCars' draw
  list on the guest stack 8 long; the position bonus past 5th none (its table has five).
- **Records**: as raceopts' races (records_guard.h; the profile not counted); ghosts and best times in
  options-...-rivals-N. Replays say so in their header (more_rivals); one without it plays with the game's 5.
- Single Event RACE only (World Tour, Road Rage, Eliminator, two players and online keep the game's 6-car frame).
## 43. Race replay (replayview, 2026-11-22)

RACE REPLAY (GAME > MODES, off by default; pc-settings §7.42): the game's own replay of a finished race in every offline
mode, from its results page (tools/native/replay_view.h). What the game has [C the decomp]:

- **Recording**: CB3ReplaySystem (gWorld + 0x3C888 = 0x0055EEE8) keeps the human players' driving controls: the player
  car's GetDrivingControls 0x1BD020 / GetCrashingDrivingControls call NextFrame 0x155020(system, controls (physics
  +0x13FC, 32 bytes), crash), which packs them into a 3-byte frame (8 in a crash: aftertouch and the camera) in a 16 KB
  byte stream (memory block 8) and gives the controls back as that frame decodes, so the live race runs on them. Full
  (~90 s), it returns 0 and leaves the raw controls. AI cars and traffic are not recorded: a replay re-simulates them.
- **Playing**: the stage's Update (CB3OnePlayerStage 0x135180) asks the logic (vt+0x28; a race logic answers its +4) and
  sets the game state D_0051BACC 12; CB3Game::Update then calls the mode's StartReplay (D_00665E52 "requested") and
  prepares the world again, which copies it to D_00665E53 "in playback" and calls Prepare 0x1551B0(system, 1). Retail
  asks only in Crash mode (after the crash) and online; the race, Road Rage, Burning Lap and Eliminator logics have the
  same chain but nothing sets their +4.
- **The start**: CB3OfflineStageLogic::Prepare 0x3956A0 sets the logic's +5, and the race logic's state 1 (Update
  0x389EE0) then runs the countdown (the countdown scene 0x94413E497BA7576B, the world timer, each car's start).
  StartReplay leaves the logic in state 1 without +5: the replay's race never started.
- **The end**: func_003960A0 (the logic's state 2 when every car is done) is the only path offline to OnEndOfChallenge
  (World Tour's progress) and the post-race setup 0x42F0F0 (records, profile); in playback it does nothing, not even a
  results scene. The results flow clears D_00665E52 as it leads out; the world's reset 0x19B380 clears both flags.
- **During playback** the race and crash logics read no pause, rumble is off and CB3OnePlayerStage::Render hides the
  HUD. A race replay uses the race's camera (StartReplay's camera event 16 / 17), not the Replay state's TV camera.

Ours (wrappers on the hook table's port layer, installed only with RACE REPLAY on at the start):

- **NextFrame**: recording, the game's own call, then the controls it leaves and its result copied to the host per call,
  so the live race is the game's byte for byte (its 16 KB and the raw controls past them as retail); playing a replay we
  asked for, the host copy gives them back in order, for any length. Prepare: recording starts a new copy, playing
  rewinds it. The port's inputs that reach the game through the driving controls (TRANSMISSION's SHIFT presses, the
  Crashbreaker button) are in those 32 bytes; TRAFFIC PATTERN Varied's seed is taken again for the replay
  (race_options.h). PARTY MODES' buttons act outside the controls: a race that used them offers no replay (logged), and
  they are ignored while a replay plays.
- **A standing start** (START Standing: the logic's rolling-start query, vt+0xC0, answers 0) waits in state 1 on the
  race's intro page (0x01E9584C, its +4 "wait for Continue") [C func_00389EE0, the intro scene's func_0043C760]. The
  page's Update (func_0043C470) takes the player's confirm from the input manager, which the playback does not read,
  and starts the page manager's lead-out with event 2, at whose end the scene calls the logic's start (vt+0x24). The
  replay continues it the same way: the page's own func_0043C570 (that lead-out without the input), once the page is up
  (the page manager's state D_01E90424 5). The world's clock is stopped meanwhile, so the moment does not change the
  race. (Calling the logic's start directly left the page's 2D frames behind: the results page failed to build at the
  replay's finish, a crash.) The player's car stays a CB3PlayerCarPhysics (kind 1) in the game's offline replay, so
  the held start, its gear and the BOOST START (booststart's wheel spin included) run from the recorded controls:
  checked "the same" (all 6 cars) with and without a BOOST START, and with the player waiting 26 s on the live intro
  (the replay's intro 72 VBlanks), back on the results page with the profile, records and tally as the race left them.
  A race still in state 1 five seconds after the lead-out is logged.
- **Request** (the REPLAY action on a one-player results page (CB3OnePlayerStage: its Update is the one hooked; split
  screen is not offered) of one of the offline race logics (gGame's race, Burning Lap, Eliminator and Road Rage objects,
  0x0051B258 / 2A4 / 2F0 / 340: a plain compare, no guest call; the crash logics keep their own replay): after the
  post-race setup ran, a recording held, the
  game in its race state D_0051BACC 5, and no Cross / Start pressed on that results scene: a choice there, Continue or
  RETRY, plays its out-animation in state 5 before it sets 7 / 8, which the stage's Update would overwrite with 12, so
  REPLAY waits for the scene to change (measured: 26 VBlanks); a press that changes neither the scene nor the game state
  within 2 s changed nothing and REPLAY is offered again; the press is tested again where the request is made, so REPLAY
  and Cross in the same VBlank leave the choice to Cross): the menu's scene noted, the takedown tally kept (below), the
  logic's +4 set in the stage's next Update. **func_0030EB10** (the world ready): in our
  playback the logic's +5 set, as its Prepare does, so the countdown runs and the replay's race starts as the live one
  did. **Leave** (REPLAY again, or 3 s after the replay's race is over or its recording run out): in the stage's Update
  the menu set back on the noted scene (the results as they were: the post-race setup is not run again) and the request
  cleared; the playback flag stays until the world's reset, so the race's end stays inert. REPLAY again from there works
  (its end counted only from its own prepare); the recording is kept until the next race (Continue, RETRY).
- **The takedown tally** (D_0051C610, 0x50 bytes: the 20 signature-takedown flags and the counts the Continue's reward
  pages and the profile take) and **the records** (0x01EA7810, 0xF000 bytes; a lap the replay completes writes there):
  OnTakedown__13CB3StageLogic and the lap code have no playback test, so around every Update of our replay's race the
  bytes that Update changed are put back (only those: anything else writing them is left alone); the leave logs whether
  both are as the request found them.
- **The world after the replay** is held behind the results page, as the game holds it on a standing start's intro
  (D_00665E50 set, gWorld's CGtTimer stopped): the replay's race does not race on there, and a held world is silent
  (measured: the mix at 0 to 1 against 6,000-8,000 for the same page without a replay). Left before its end, the race
  offers no further replay (logged): held mid-race, the next replay of it started differently (DIFFERENT after a leave
  at 606 of 1,632 frames), and what survives that re-prepare is not known (research after 1.4.0); played to its end it
  can be watched again. The hold is given back (the flag cleared, the clock started) as the game leaves its race
  state, at any world's prepare or readiness (RETRY, the next event, a Crash event, a 2P race) and when a replay is asked
  for again, so nothing after starts held. The held time is not counted as play time (the save card's one counter
  that differs from the same race without a replay), as the game's own pause does not count it. TRAFFIC PATTERN Varied's seed is taken again only
  while the replay is asked for (D_00665E52): a RETRY after a replay is a new race with a new seed.
- The renderer gets the state through the host record 'HRV1' (0x41112800: 0 none, 1 REPLAY offered, 2 the replay
  playing): the REPLAY prompt (racetime.h ReplayPrompt), and the ghost and best-time recorders ignore replay ticks.

Checked (2026-11-22, US/C2_V2 and World Tour US/C3_V1): the player's car at the same position at every 300th frame, live
and replay, 25 of 25 through a 124 s race (7,431 frames, past the old 16 KB); the profile (0x49C bytes) and the records
(0x01EA7810, 0xF000 bytes) hash the same before and after watching a replay, in a Single Event and a World Tour race;
REPLAY, a leave mid-replay, REPLAY again and the end back on the results page, by scripted presses; Road Rage and the
Eliminator (its ~100 s race, from either results page) the same; REPLAY refused after Continue on the same page; Time
Attack (CB3OfflineBurningLapLogic, endless laps) from its results page after END SESSION: the session replayed to its
last frame ("the same"), then 3 s of neutral controls (the session has no finish) and back. A manual-gearbox race
with TRAFFIC PATTERN Varied (three SHIFT UPs): the gear changes at the same intervals and "the same" at the last
frame (the self-check: the player's car at the replay's last frame against the race's, logged). "The same" too, one
race each: TRAFFIC CHECK Extreme, TUK-TUKS, CRAZY AI PHYSICS Mayhem (the AI cars blown up in both), KAMIKAZE TRAFFIC,
the Crashbreaker in every mode, a custom car as the player's (customseries), a tuk-tuk as the player's (trafficseries),
the takedown scenario's 2,700-frame race and Road Rage's 2,400; MORE RIVALS with RIVALS 7 (8 cars, 900 and 2,700
frames: every race car where the race had it -- the self-check compares all of them, D_0064D37C race cars from gWorld
+0x12AD28, each +0x30). AI DRIVER BUTTON (PARTY MODES' buttons) is refused.
After Continue to the front end, the profile, the records and the tally are the same with no replay, a whole replay and
one left at 606 of 1,800 frames; the results page after an early leave shows the race's place, times and records.

## 44. Replay cameras (replaycams, 2026-11-23)

The race replay's viewer (§43's phase 2; pc-settings §7.43): while the game's replay plays, cameras of the port's own on
any race car, the watched car switched, a free camera, all drawn only (src/b3replay/replaycams.h). What the game has [C
the decomp]: a race replay keeps the race's chase camera on view 0's player car and cuts to the crash camera on a crash
(§43); CB3PlayerCameraStateReplay (the trackside director, vt 0x4DE360) is Crash mode's. Nothing in the game watches a
rival, so the angles are ours:

- **The angles**, stepped per tick in game time (shots of a poll show the same frame however far behind the renderer
  is) and blended per frame: Chase and Far chase trail the car's level heading (eased, so a spin does not whip the
  view) and stop short of a wall between the car and the eye (the route's walls: the orbit camera's track rays);
  Bumper rides the car's own frame, held back from a wall at its nose; Orbit turns round the car; TV stands by the road
  ahead, picks a side that sees the car, is placed again once the car is past or out of sight for a third of a second,
  and zooms so the car keeps its size. All keep the race chase camera's view window (not the crash camera's narrow one).
- **The cars**: the race cars from the capture, the player's first; each angle list remembers its choice.
- **Presses** carry the game poll they came with and apply from that tick; the camera and driver line is kept by poll
  too, so an offscreen shot shows the line as it was then. Offscreen shots of the replay's own views draw the whole
  track (as the window does), not only the part the game streamed for its own camera.
- **The free camera** is photo mode's, allowed live while the replay plays (the game is not paused); the FREE CAMERA key
  also leaves it, and it closes when the replay stops.
- **REPLAY CAMERA CUTS**: Game (default) leaves the game's crash and takedown cuts as they are; Stay keeps the chosen
  camera through them.

Checked (2026-11-23, US/C2_V2): race_check `SCENARIO=replaycams` (the race finished by the test, REPLAY, then each
angle, the next car, its TV, the free camera on and off by scripted keys; the cameras off as the replay ends), shots of
each; a rival watched far from the player's car (470 m) with the track drawn round it; with RACE REPLAY off or the
replay not playing, the frame is the game's (the golden scenarios unchanged but for the CONTROLS pages' five new rows).

## 45. EXTRAS: every addition off by default (extras, 2026-11-23)

The owner's call (2026-11-23): a new install plays as the game shipped. Every addition the port makes to the game itself
(SELECT MODE's ELIMINATOR, MORE RIVALS, each extra SELECT OPTIONS row, the HEAVYWEIGHTS / TRAFFIC / CUSTOM / BETA series,
the CAR VIEWER, the GAMEPLAY options, the game's two limits as NO IDLE PAUSE / NO EVENT TIME LIMIT, PARTY MODES, RACE
REPLAY) has its own row on PC SETTINGS > EXTRAS, all off by default but ELIMINATOR (docs/pc-settings.md §7.46). Night and
rain (§17, §38) ship there as EXPERIMENTAL (NIGHT: SELECT OPTIONS' TIME OF DAY row, RAIN: its WEATHER row; NIGHT / RAIN IN
WORLD TOUR: Off / Always / Random), off by default; drawn only, so their races count as any. Visual upgrades stay
with the presets; the conveniences stay on. The rows reach the game side as one word (b3settings::extras_bits,
b3r_extras, latched and recorded for replays); SELECT OPTIONS packs the rows that are on (race_choice.h
race_pages_masked). An older install keeps its imported cars on (pc_settings.ini version 3). The start reads these words
from the settings file until the renderer has loaded it (extrastart, gameoptsfix: before, a window run's start took the
defaults; docs/pc-settings.md §7.46 "At the start").

## 46. HD texture cache: convert once, prepare at the loading screen (hdcache, 2026-11-24)

With a big HD pack, a vehicle drawn for the first time mid-race converted its replacement on the render thread: a
traffic model's far LOD in each new paint, or a race car's LOD 3, 25-40 ms each (the BC chain 15-19 ms, the file read
and decode 0.5-7 ms, the crop and alpha rule the rest): about 20-25 single-frame stutters a race on US/C1_V1 and
AS/C1_V1 with a complete community HD pack at 5120x1440, one every 10 s or so. The streamed track is not involved: every
track texture is converted and uploaded when the route loads (on a worker, 0.7-0.8 s).

- **The cache** (`include/b3asset/hdcache.h`; `src/b3asset/hdpack.cpp` HdTextures::disk_key / disk_hit / disk_store):
  what `HdTextures::apply` hands the renderer (the cropped, alpha-ruled RGBA8 image, or the BC chain with its mips) is
  kept in the user data folder (`cache/hd/`, the portable folder in portable mode), one file per result, and read back
  instead of converted again. The key: the replacement file's path, size and time (a changed pack file gets new keys),
  the hash of its first and last 4 KB (the same size and time is not always the same file), the raster's
  request (crop, TEX0 size, TCC, the decode's size), the alpha rule's input (the original's alpha and its
  maximum) and the conversion's mode (BC device, the compress scope, HD TEXTURE MEMORY, raw alpha), with a format and a
  conversion version (bumped when the converters change). A hit reads no replacement file at all. Only what was drawn
  is written ("convert only what is used").
- **Safety:** one worker writes each entry whole to a temporary name and renames it (a crash or a second game leaves no
  half entry; a temporary file a crash left is removed after an hour); an entry is read back with its size and XXH3
  checked (a damaged one is deleted and converted again). The folder is held under 2 GB ($B3_HD_CACHE_MB): the entries
  used longest ago go first (a read updates an entry's time). $B3_HD_CACHE=0: no cache; $B3_HD_CACHE_DIR: another
  folder. An entry is byte-identical to a fresh conversion (tests/test_hdpack.cpp `disk_cache_*`), so nothing on screen
  changes; with no HD pack the cache is never touched (the golden runs have none).
- **The loading screen** (vpreload's scene wait, `View::preload_vehicles`): with a pack, every vehicle file the race
  loaded (the race cars and the route's whole traffic set) has its replacements converted first, every paint, on threads
  (`HdTextures::prefetch_data`; what the cache holds is skipped), and the vehicles on the grid load their LOD 3 and 4 as
  well as 0-2. A vehicle's first draw mid-race then finds its textures ready. The prepare takes no new raster once the
  wait ends or its 2 s budget is spent (what it did not reach converts at its first draw, and is cached);
  $B3_HD_PREPARE=0: none. The log line gives its time and the process's memory before and after it.
- **The scene wait** (scenewait, 2026-11-24): the first race on a track with an empty cache can take longer to build its
  route than the loading screen's wait allowed (3 s: EU/M1_V1, 509 conversions, 3.5 s), so the race showed half a second
  before its scene and the vehicle preload never ran. The wait now goes on past its cap, up to 10 s, only while the scene
  is actually being made: the route's decode or GPU half running, the preload running, or the route pending with the
  render loop going round; never with the window minimised or hidden, and a failed route ends it at once
  (`src/b3replay/fastforward.h` scene_wait_more; the log says "waited on"; checks: $B3_TEST_ROUTE_DELAY_MS,
  $B3_TEST_ROUTE_FAIL). A warm cache ends well inside it.
- **The launcher:** the HD Textures page shows the cache's size (HD TEXTURE CACHE) with **Clear cache**.
- The log: `[b3live] vehicles preloaded: ... HD prepared for N vehicle files in X ms; HD cache H read, C converted so
  far`, and at the end `hd pack: disk cache ...` (reads, conversions, writes, damaged, evicted, the folder's size).

## Order (the first plan, 2026-11-01; kept as history)

The order the features were first planned in. Steps 1-3 are done and most of step 4 has landed (online play is still
to come); the later sections record what landed since.

1. Input layer with mapping, keyboard and automatic switching: needed by every platform anyway (phase 6, platform
   layer), and the glyph switching follows from it.
2. Player-controlled video skip: small and possible early, as an override.
3. Settings menu: after the front-end page classes run natively; graphics options after the native renderer.
4. Section 5 items as their prerequisites land (save import and the tuning console are early candidates; replays,
   ghosts and online need the input layer; photo mode and HUD options need the native renderer).
