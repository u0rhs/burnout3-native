# Remaster feature backlog

Features planned for the port (listed 2026-11-01). They are recorded here so each one is designed in when
the port reaches that area, not bolted on afterwards. **Hard rule for all of them: no change to physics, AI or
gameplay results.** A feature may only touch presentation, input, menus and settings, and the native checks
(`race_check.sh` with `STRICT=1` and its scenarios, the golden runs) must stay identical with every feature in its
default state.

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
- **Car series on the shoulders (carsel1, 2026-11-13; LB / RB change the series, with glyphs beside the arrows):** opt-in, input-changing (docs/pc-settings.md §7.13). The car select changes the
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
would need a target and a make-up gain of its own.

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
`$B3_NIGHT_STREET_SHAPE` = "inner deg, outer deg, d0 m, gain, fade start m, fade end m". Not lit: lamps on routes
without a listed texture (HK's streets have none beside its spotlights and dock lamps). Cost on the RTX 4080 (as below,
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
the outside takes the fade's light too (the fade is by distance, not by what covers each pixel).

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
| 3. Ambient occlusion | **Prototype done.** `shaders/ao.frag`: the depth pre-pass photo mode's depth of field already draws (every Z-writing draw, cars included), a half-size pass (12 taps on a disc of 1.2 m, the normal from the depth, rotated per pixel), then a 3 x 3 depth-aware upsample multiplied into the opaque scene before the full-screen specular. It gives the cars contact shading on the road, darkens the kerb and wall bases, corners and under barriers; the sky and the prelit big shapes are left as they are. The real cost is the second geometry pass: writing depth into a second colour target of the scene pass itself (MRT on every scene pipeline) would remove it: +2-3 d. GTAO (horizon-based, steadier) on the same inputs: +2 d | done (~1 d); MRT depth +2-3 d; GTAO +2 d | pre-pass ~0.3-0.5 ms GPU (+~1 ms CPU recording ~1000 draws again), AO ~0.15 ms, composite ~0.1 ms |
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

## Order

1. Input layer with mapping, keyboard and automatic switching: needed by every platform anyway (phase 6, platform
   layer), and the glyph switching follows from it.
2. Player-controlled video skip: small and possible early, as an override.
3. Settings menu: after the front-end page classes run natively; graphics options after the native renderer.
4. Section 5 items as their prerequisites land (save import and the tuning console are early candidates; replays,
   ghosts and online need the input layer; photo mode and HUD options need the native renderer).
