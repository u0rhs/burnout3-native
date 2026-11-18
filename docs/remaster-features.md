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
  the PC build adds a 6th, QUIT GAME, so the list takes extra rows), so the plan is a 7th row "PC SETTINGS"
  above QUIT GAME, laid out in the original style; if the main menu page's layout can't fit a 7th row cleanly, the
  fallback is a footer prompt (a button hint like the existing "back"/"select" ones). Its FSM event goes to our page. Strings: our own string table next to
  the game's language files, so translations work the same way.
- Settings are stored in a host config file (not in the PS2 profile), so original saves stay untouched and portable.
- Sub-pages: Graphics (resolution, window mode, framerate cap / uncapped with interpolation, ultrawide, draw
  distance, motion blur, anti-aliasing, texture filtering), Audio (volumes, output device), Controls (section 3),
  Port (the video skip prompt, section 2; PS2-faithful vs fast presentation options).
- Prerequisites: front-end page classes running natively (decomp game code, phase 4); the native renderer for the
  graphics options (phase 6).

- **Status (settings1, 2026-11-05):** built for the GS-off window. "PC SETTINGS" is the main menu's 7th row (above QUIT
  GAME); our own ini, not the PS2 profile; 5 tabs (DISPLAY, GRAPHICS, EFFECTS, CAMERA, INTERFACE), presets; every row
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
  fonts have no button glyphs. PC SETTINGS > INTERFACE > VIDEO SKIP HINT turns it off. The skip itself is unchanged
  (Start or Cross, as above).

## 3. Controls: remapping, button prompts, keyboard, automatic switching

- **Custom mapping** for controllers and keyboard, per player, saved in the host config. Mapped at the input layer
  (where pad state enters the game), so game code sees the same logical buttons it always did; physics and handling
  stay identical because the inputs are the same values. **Done (remap1, 2026-11-12)**, docs/pc-settings.md §7.9:
  `src/native/input_bindings.h` binds each of the 24 PS2 actions (the 16 buttons, both sticks' half axes) to keys
  (per context: driving / menus) or gamepad inputs (buttons, stick half axes, triggers with their pressure), per
  player; controls.ini in the user data folder (versioned, invalid entries reported and skipped); edited in SETTINGS >
  INTERFACE > CONTROLS and on the launcher's Controllers page (press the input, a timeout and Escape to cancel, a
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
  INTERFACE > BUTTON PROMPTS: Auto / PlayStation / PS4 / PS5 / Xbox / Nintendo / Generic / Keyboard (ini `button_prompts`). Covered:
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
  INTERFACE > CAR SERIES ON L1 / R1 On, a press of L1 / R1 on the car select or YOUR GARAGE page reaches the game as
  Left / Right (the game's own series change), and the device's L1 / R1 glyphs are drawn beside the series arrows.

## 4. Also requested earlier (see README goal)

Uncapped framerate with fixed-step simulation and interpolation, ultrawide, draw distance, motion blur, modern
rendering, custom cars/liveries/tracks, online play.

## 5. More features (proposed 2026-11-01)

Gameplay-changing items are **opt-in** and never part of the default game.

**Built on what is already in the game**
- **Developer tuning console for modders.** Retail registers its tuning variables with a variable debugger (the
  `CGtCommsDatabase::AddEntry` calls; RegisterStaticVariables__8CB3ScoreFv alone registers 82 [C decomp, draftW]). Expose that
  registry in-game: Criterion's own knobs, read/write, with presets saved per mod.
  *Done (v1, tune4 2026-11-12, mod kit part 4; docs/modding/tuning.md, docs/pc-settings.md §7.10):* PC SETTINGS >
  INTERFACE > DEVELOPER > DEVELOPER MODS Off / On (default Off: nothing installed, the run byte-identical; from the next
  start). On, native_play wraps the three AddEntry functions (float 0x0021BB60, int 0x0021BD80, vector 0x0021B9B0) on the
  hook table's port layer and records every variable as it registers (address, name, group, config file, range, count,
  key; read only). PC SETTINGS gets a DEVELOPER tab (the variables registered now, by group, with their values; Left /
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
  *Done (v1, replay1 2026-11-12, src/native/replay_file.h; docs/pc-settings.md §7.5):* PC SETTINGS > INTERFACE > REPLAYS >
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
  *Done (own ghosts, v1, ghost1 2026-11-10, src/b3replay/ghost.h; docs/pc-settings.md §7.2):* PC SETTINGS > INTERFACE >
  TIME ATTACK > GHOST Off / Best lap (default Off: nothing recorded or drawn, frames byte-identical). In Time Attack
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
  SETTINGS > INTERFACE > SPEEDRUN. SPEEDRUN TIMER Off / Event / Event + splits (default Off: nothing read, kept or drawn;
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
  *Pause menu row (photorow1, 2026-11-15, docs/pc-settings.md §7.17):* a PHOTO MODE row after SETTINGS in the race's
  pause menu (not in the 2-player split screen) enters photo mode as P does; PC SETTINGS > INTERFACE > PHOTO MODE IN
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
  *Done (fastload1, 2026-11-12; docs/pc-settings.md §7.8, tools/native/fast_load.h):* SETTINGS > DISPLAY > FAST
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
  **Rumble done (rumble1, 2026-11-12, docs/pc-settings.md §7.4):** SETTINGS > INTERFACE > CONTROLLER: RUMBLE Off / On
  (default Off), RUMBLE STRENGTH 10-100 %, TRIGGER RUMBLE (Xbox impulse triggers). The game's own vibration requests
  (libvib through sceDbcSendData2: the small motor on / off, the large motor 0-254) are acknowledged as the PS2's
  device manager does (the RPC's reply) and given to player 1's and player 2's pads through SDL_RumbleGamepad /
  SDL_RumbleGamepadTriggers. Off leaves them pending as before (the run identical). On is opt-in because the game sees
  the answers (a result it ignores, and ~125 EE cycles per accepted request); physics, AI and gameplay read nothing of
  it. Not done: HD rumble beyond SDL's two motors.
  **Adaptive triggers and gyro steering built (pad1, 2026-11-13, docs/pc-settings.md §7.12; verify with real pads):**
  SETTINGS > INTERFACE > CONTROLLER: ADAPTIVE TRIGGERS Off / On (default Off; presentation only) and TRIGGER STRENGTH:
  a DualSense's brake trigger resists, the accelerator's is a light spring that buzzes while boosting, both kick at a
  crash, free in menus / paused / at exit (SDL_SendGamepadEffect with SDL's DS5 effect block; the effects from the
  game's state the window reads: a race on screen, the view's boost blur amount, its car's crashed flag). Gyro
  steering, per player (INTERFACE > CONTROLS with the controller device, and the launcher's Controllers page; saved in
  controls.ini): GYRO STEERING Off / Mix / Replace (**input-changing**, opt-in, default Off), tilt (a steering wheel,
  drift-free with the accelerometer) or turn, sensitivity, dead zone, a recentre button; while driving it is the
  report's left stick X, so replays keep it; Off: the reports byte for byte as before.
- **Accessibility:** colour-blind-safe HUD colours, text size, boost toggle vs hold, DJ subtitles.
  **Done (2026-11-10, docs/pc-settings.md §7.1):** SETTINGS > INTERFACE > ACCESSIBILITY: SCREEN FLASHES Full / Reduced
  (full-screen white fills of the 2D capped, the TV static still, the Crashbreaker's full-screen flash capped at 25 % of white), HUD COLOUR FILTER Off / Deuteranopia / Protanopia /
  Tritanopia (a daltonize remap of the 2D layer only), BOOST Hold / Toggle (opt-in, input-changing). Defaults = the game
  byte for byte. **Text size done as HUD SIZE (textsize1, 2026-11-13, pc-settings.md §7.1 "HUD SIZE"):** 100 / 115 /
  130 / 150 %, the race HUD's components each scaled about their own EHUDAlign anchor (POS, LAP, SCORE, boost bar,
  speedometer, CRASHBREAKER, the race's messages, "Get Ready!", the EA TRAX popup), capped so that nothing leaves the
  screen or runs into another component (a message gives way to the steady HUD); 100 % = the game byte for byte. The
  menus (front end, pause menu, event intro, results) stay at 100 %: their text sits in fixed panels. Not done: DJ
  subtitles.
- **Split-screen layout choice** (vertical / horizontal) with correct aspect ratio.
  **Done (splitlayout1, 2026-11-13, docs/pc-settings.md §7.11, docs/native-renderer.md §29.7):** SETTINGS > CAMERA >
  SPLIT SCREEN: Top / bottom (default) / Side by side. The game's own layout is top / bottom, and it is not squashed
  [C SetViewportsSplitScreen 0x19E760: each viewport's aspect is its half of the TV picture, 8/3 at 4:3]; the port
  already draws each half Hor+ at its window shape, so Top / bottom is the game's layout byte for byte. Side by side
  (a branch the retail game has but never turns on) puts player 1 left and player 2 right, each projected at its own
  shape from the game's side-by-side view window (vertical 0.825 tan(fov / 2), Hor+), with each player's HUD
  anchored inside its own view (16:9 and 21:9 checked) and the rival markers projected per view. Drawn only: no game
  state, camera, pad report or timeline changes (golden equal with the default).
- **Speed units:** MPH or KM/H for the speedometer and the game's distances.
  **Done (units1, 2026-11-14, docs/pc-settings.md §7.16, tools/native/speed_units.h):** SETTINGS > INTERFACE > SPEED
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

**Settings.** PC SETTINGS > INTERFACE > MUSIC (a section of the INTERFACE tab, native_play only: `Env::music`; not a 7th tab, whose label would not fit): MUSIC FOLDER (the system's folder dialog,
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
it give the same output RMS (2959 vs 2921 over 900 VBlanks; B3NATIVE_MUSIC_RMS=<first>-<last>).

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

**Setting.** PC SETTINGS > INTERFACE > MUSIC > OTHER MUSIC APPS: Off / Show now playing (ini `other_music_apps` 0-1,
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
  soundtrack, the same music_folder / playlist_mode as SETTINGS > INTERFACE > MUSIC: choose / type / open / remove the
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
  **Start the game directly next time (skip this launcher)** on the Play page (= SETTINGS > INTERFACE > OPEN THE
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
REFLECTIONS** (PC SETTINGS > GRAPHICS; ini `car_reflections` 0 Off / 1 Original / 2 Enhanced; renderer
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
tens of thousands of sparks is the heaviest picture the game draws, so **SPARKS** and **CRASH DEBRIS** (PC SETTINGS >
EFFECTS) have levels, Off / Low / Medium / High: High is the game's whole set (the default, byte-identical), Medium draws
half of the particles and Low a quarter, a little larger so a crash still sparks brightly. The same particles are kept from
frame to frame (nothing flickers). **RENDER SCALE** (GRAPHICS; 100 / 85 / 75 / 67 / 50 %) draws the 3D picture at a share of the
window and scales it up, with the HUD and menus at full size. **BATTERY SAVER** (EFFECTS; off by default) holds the frame
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
amber accent bar of the notices and photo mode's panels, a pixel font and slanted cells, all solid rectangles: it costs next to nothing per frame). **FPS COUNTER** (PC SETTINGS >
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
picture), so a big window is not the cause; the strength is. **BLOOM** (PC SETTINGS > EFFECTS; ini `bloom_level` 1 Soft /
2 Standard, with `bloom = false` for Off; row id 260) has three choices: Off, **Soft** (the default) and **Standard**. Standard
is the game's own sum (the frame plus the glow). Soft adds the glow at 60% of its intensity as a screen blend,
`1 - (1 - frame)(1 - glow)`, so the sum never passes white and bright signs keep their detail. The PS2 ORIGINAL preset
is Standard. Drawn only: nothing reaches game memory. The golden record is Standard: `tools/golden/baseline.sh` exports
`B3_BLOOM_LEVEL=standard` (`soft` is the other value); the offline `b3replay --offscreen` also starts at Standard
unless that variable says otherwise.

Cost: with MSAA on and POST PC the bloom draws in one lean form. The scene pass before the bloom resolves into a
single-sample texture and does not store its samples; the glow is then added in one full-screen draw that starts the
resumed pass (the resolved picture plus the B-spline glow, no blend). The old form stored the MSAA scene (4 samples at
RGBA16F, 184 MB at 3200 x 1800), reloaded it, and blended the glow into every sample. The picture is the same to within
rounding (mean difference 0.00001 of 255, at most 3 of 255 on a few pixels); the PS2-sized path (POST PS2, the golden
record) and MSAA off keep the original form byte for byte. Measured in a real window on an RTX 4080 SUPER (5120 x 1440, MSAA 8, the same race replay), the lean form was slower than the original split (218 fps against 238), so the original split is the default again; `B3_BLOOM_LEAN=1` opts into the lean form for comparisons.

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

## Order

1. Input layer with mapping, keyboard and automatic switching: needed by every platform anyway (phase 6, platform
   layer), and the glyph switching follows from it.
2. Player-controlled video skip: small and possible early, as an override.
3. Settings menu: after the front-end page classes run natively; graphics options after the native renderer.
4. Section 5 items as their prerequisites land (save import and the tuning console are early candidates; replays,
   ghosts and online need the input layer; photo mode and HUD options need the native renderer).
