# PC settings: the PC SETTINGS page and its rows

The port's own settings page, PC SETTINGS: a 7th row on the main menu above QUIT GAME (decided,
remaster-features §1; nothing removed from the original menus, nothing added inside Driver Details / My Burnout), and
reachable from the race's pause menu. Every row is drawn-only (the renderer) or host-side (window, audio gain, input
before the pad record); none changes physics, gameplay or what the game computes, except the few rows labelled
input-changing, which are opt-in (§7.1, §7.3). The game's own options (volumes, the DJ, the takedown camera, impact time,
vibration) stay where the game has them, Driver Details > SETTINGS; the page points there instead of duplicating them.

Tags: [C] read in the cited source or measured, [L] likely (read from the code, no run yet), [G] guess.

## 1. Decisions

Settings1 (2026-11-05): variable frame rate is refused in every mode; scenery default stays
"Full" (PS2 distances belong only to the PS2 preset); each option has one place; particle / debris density rows stay
hidden until those effects are drawn.

## 2. The renderer's switches (2026-11-05)

All of these are renderer-only: no guest effect.

| Knob | Flag / key (Ctrl+key in the viewer window) | Default (main) | Live | Note |
|---|---|---|---|---|
| Window size, fullscreen | `--size`, `--fullscreen`, F11 / Alt+Enter | 1600 x 900, windowed | yes (F11) | SDL3 desktop fullscreen (borderless) |
| VSync | `--vsync` | off (IMMEDIATE / MAILBOX) | no | |
| Frame cap | `--max-fps N` | 0 = uncapped | no | |
| MSAA | `--msaa 1/2/4/8` | 4 | no (pipelines) | |
| Anisotropic filtering | `b3gfx::Options::anisotropy` (no flag) | 16, trilinear | no | the 2D stage is bilinear, without aniso |
| Interpolation | I | on | yes | off = the game's 60 / 30 Hz steps |
| Display delay / latency | `--delay 1.5`; lat1: `--latency low/smooth/newest`, `--margin`, `--lead`, `--frames-in-flight` | low | no | **lat1's area** |
| Fog | G | on | yes | |
| Full-screen specular | H | on | yes | |
| Shadows + skid marks | F (`--no-effects`) | on | yes | one switch for both today |
| Coronas (car lights, boost glow) | L | on | yes | |
| Sparks, scrapes, popcorn | O (`--no-particles`) | on | yes | one switch for all three (the settings page has levels for sparks and debris: §7.18) |
| Sun corona + flare, its occlusion test | X, B | on | yes | |
| Zoom / directional blur | N (`--no-blur`) | on | yes | |
| Boost radial blur strength | `--radial-blur S` | 0.5 (1 = the decomp's model) | no | |
| Bloom | T | on | yes | |
| Post quality PC / game | Z (`--post`) | PC | yes | |
| Damage deformation | `--no-deform` | on | no | |
| Car LOD | `--car-lod N` | -1, the game's choice | no | |
| Game visibility culling | V | off (every resident unit) | yes | |
| HUD on / off, wide / 4:3, sharp / game filter, markers per frame / tick | U, Y, J, K | on, wide, sharp, frame | yes | |
| Front-end layout | `--fe-layout 43/edges`; fe3: `panels` | 43; fe3: panels | no | **fe3's area** |
| Movie side fill | fe3: `--movie-fill blur/black` | blur (fe3) | no | **fe3's area** |
| Movies, showroom, globe | M, R, `--no-globe` | on | yes | |
| HD texture pack | `$B3_HD_PACK` / `--hd-pack`, P | off unless set | yes (P reloads) | |
| FPS counter | `--fps-counter` or the page's FPS COUNTER (Off / Minimal / Standard / Detailed, ini `fps_view`) and FPS COUNTER POSITION (`fps_corner`); the FPS COUNTER VIEW key (F3) | off | yes | |

The flags and keys themselves are not persisted; the page's rows (§6) are, in the ini.

## 3. The page's design: rows, presets and costs (settings1, 2026-11-05)

Hard rule: no row may change physics, gameplay or the guest's behaviour. Every row below is one of:
- renderer-only;
- host-only (window, audio gain, input before the pad record).

**Default = main today, pixel for pixel**, so with every row at its default the replays and pixel checks are
unchanged. The default is the **High** preset.

Presets:
- **PS2 original**: the PS2 look, kept sharp.
- **Low**: for weak GPUs.
- **Medium**.
- **High**: the default, = main.
- **Ultra**.
- **Custom**: shown as soon as any row differs from a preset.

Cost tags for phase 2:
- **E** exists, needs wiring only;
- **M** a modest addition to the renderer;
- **B** a bigger item, or one that waits on other work.

### 3.1 Display

| Row | Values | Default | PS2 / Low / Med / Ultra | Cost |
|---|---|---|---|---|
| Display mode | Windowed / Borderless fullscreen / Exclusive fullscreen | Windowed | = | E (borderless) / M (exclusive: SDL display mode) |
| Monitor | list of displays | primary | = | M |
| Window size | list + the current size | 1600 x 900 | = | E |
| Render scale (3D) | 50 / 67 / 75 / 100 / 150 / 200 % | 100 | 100 / 75 / 100 / 150 | M (3D target at scale, filtered down or up; HUD stays native) |
| VSync | Off / On | Off | = | E (live: swapchain mode) |
| Frame cap | Unlimited / 30 / 60 / 120 / 144 / 165 / 240 / display refresh | Unlimited | = | E |
| Latency mode | Low / Smooth / Newest (lat1) | Low | = | E, **lat1's flags, coordinated** |
| Frame interpolation | On / Off (game steps, 60 race / 30 crash) | On | Off / On / On / On | E |
| HUD scale | 100 / 115 / 130 / 150 % | 100 | = | **built** as HUD SIZE (§7.1, textsize1): the race HUD only, menus stay 100 % |
| HUD safe area | 0-10 % inset | 0 | = | M |
| HUD layout | Wide (edge anchored) / 4:3 | Wide | 4:3 / = / = / = | E |
| Front-end layout | Panels / 4:3 / Edges (fe3) | fe3's default | 4:3 / ... | E, **fe3's area** |

### 3.2 Quality

| Row | Values | Default | PS2 / Low / Med / Ultra | Cost |
|---|---|---|---|---|
| Anti-aliasing | Off / MSAA 2x / 4x / 8x / SSAA (render scale 200 % + MSAA 4x) | MSAA 4x | Off / Off / 2x / 8x | E (MSAA), M (live change) |
| Texture filtering | Point / Bilinear (PS2) / Trilinear / Aniso 2x / 4x / 8x / 16x | Aniso 16x | Bilinear / Trilinear / Aniso 4x / 16x | M (sampler from settings) |
| HD textures | Off / On + pack folder | as main (`$B3_HD_PACK`) | Off / Off / = / = | E |
| Car detail | Game (PS2 LOD) / Highest (LOD 3 always) | Game | Game / Game / Game / Highest | E (`car_lod`) |
| Track detail | Game (high or LOD per slot) / Highest (high meshes in every slot) | Game | = / = / = / Highest | M |
| Scenery detail (instances) | PS2 (the type records' switch and maximum distances) / Full (main: near model, no limit) | Full | PS2 / PS2 / Full / Full | M-B (needs the far model +0x28 decoded; renderer-side distance test, the guest records only read) |
| Draw distance | Game (resident slots) / Extended (every unit within N m, whole route from disc) | Game | = / = / = / Extended | M; only if the fog hides the edge and no pop-in shows (test on race1's bridge) |
| Shadows | Off / Blob (PS2) | Blob | = | E (split F) |
| Skid marks | Off / On | On | = | E (split F) |
| Car reflections (env pass) | Off / On | On | = | M (a CarParams flag) |
| Full-screen specular | Off / On | On | = | E (H) |
| Fog | not offered (fog off is a debugging view; G stays a viewer key) | On | = | - |
| Water reflections | Off / On | On | = | M |
| Sun corona and lens flare | Off / On | On | = | E (X) |
| Car and traffic lights (coronas) | Off / On | On | = | E (L) |

### 3.3 Effects (all drawn-only)

| Row | Values | Default | PS2 / Low / Med / Ultra | Cost |
|---|---|---|---|---|
| Motion blur (zoom / directional) | 0-100 % strength (0 = off) | 100 % (= on) | 100 / 0 / 100 / 100 | E on / off; M strength |
| Boost radial blur | 0-100 % (main's 0.5 = 50 % of the decomp model, shown as "Default") | Default | = | E |
| Bloom | 0-150 % | 100 % | 100 / 0 / 100 / 100 | E on / off; M strength |
| Post quality | PS2 (the GS's buffers and taps) / PC | PC | PS2 / PS2 / PC / PC | E (Z) |
| Sparks | Off / Low (a quarter) / Medium (a half) / High (all) | High | High / Low / Medium / High | **done** (effects2, §7.18): a stable share of the captured sparks |
| Scrapes | Off / On | On | = | E (split O) |
| Popcorn debris (crash bits) | Off / Low (a quarter) / Medium (a half) / High (all) | High | High / Low / Medium / High | **done** (effects2, §7.18) |
| Smoke, dust, glass shards, fire (particles) | Off / Low / Medium / High | High (half of them) | | **done** (effects3; built on a worker in the window, explosion rasters by full name): the 24 particle arrays are drawn from the game's own VU1 program 0x46D750 (native-renderer.md §19.10); smoke / fire / explosion arrays follow SPARKS, debris / gravel / snow / glass and the crash banks follow CRASH DEBRIS (`particle_kept`, §7.18). No separate row |
| Debris (CB3DebrisArray) | Off / Low / Medium / High | High | | **B**: not drawn yet (item 6); the same levels when it is |
| Damage visuals | Full (the game's deformation) / Off (pristine bodywork, visual only) | Full | = | E (`deform`) |
| Glass damage | Full / Off (intact glass drawn) | Full | = | M (the glass slot choice) |
| Screen flashes (was: crash flash and screen effects) | Full / Reduced (full-screen bright fills of the 2D layer and the TV static capped at 25 % of white) | Full | = | **done** (2026-11-10, §7.1: INTERFACE > ACCESSIBILITY, ini `screen_flashes`). The explosion flash (RenderFullScreenFlash, §7.1) is drawn and Reduced caps it too (its alpha scaled so it lifts the picture by at most 25 % of white) |

**Refused rows.** Physical debris amount, crash damage model, traffic density and particle physics would change the
game state, so they are not offered. Every row above changes only what is drawn from state the game has already
computed.

### 3.4 Colour

| Row | Values | Default | Cost |
|---|---|---|---|
| Brightness | -50 .. +50 | 0 | M (present pass) |
| Gamma | 1.6 .. 2.8 | 2.2 (= main: no correction) | M |
| PS2 TV look | Off / On (a CRT gamma of about 2.5 and a light scanline-free soft filter) | Off | M [G look; to tune against GS frames] |
| PS2 dither | Off / On (the GS 4x4 dither in the present pass) | Off | M |

### 3.5 Interface

| Row | Values | Default | Cost |
|---|---|---|---|
| FPS counter | Off / Minimal / Standard / Detailed (+ position: four corners) | Off | **done** (fpsoverlay2: `fps_view`, `fps_corner`, row ids 255 / 256; remaster-features §11) |
| Latency overlay | Off / On (lat1's numbers on screen) | Off | M, **with lat1** |
| Button prompts | Auto (last device) / PlayStation / PS4 / PS5 / Xbox / Nintendo / Generic / Keyboard | Auto | **done** (mac5, native_play: `button_prompts`, src/b3replay/prompts.h; remaster-features §3) |
| Mouse in menus | Off / On (our page only; the game's menus ignore the mouse while recording) | On | E (the page model takes the pointer) |
| Movie side fill | Blur / Black | fe3's default | E, **fe3's area** |
| Video skip hint | Off / On (the corner "skip" hint with the device's glyph) | On | **done** (mac5, native_play: `skip_hint`, src/b3replay/skiphint.h) |

**Videos (decided, remaster-features §2).**
- Every video plays. Skipping is the player's choice, a button press while the hint shows, and never automatic.
- So there is **no "skip intro movies" setting**. The only row is the hint's visibility.
- The skip itself is a host action driven from the pad the game sees (tools/native/movie_skip.h), so it replays. It is
  outside this page.

### 3.6 Camera (renderer-side)

| Row | Values | Default | Cost |
|---|---|---|---|
| Field of view | Game (90) / 60-120 | Game | M (a tangent map at draw time) |
| FOV correction | On / Off (dolly the eye along `at` so the car keeps its size) | On (inactive at Game) | M |
| Panini projection | Off / On | Off | B (post warp; optional) |
| Look-back hides own car | (cam1, not a setting) | | |

### 3.7 Audio (one tab: the game's rows and the host's)

| Row | Values | Default | Acts on |
|---|---|---|---|
| Effects volume, Radio volume, DJ, Audio output | as retail | the profile's | guest (retail: the game's own options, Driver Details > SETTINGS) |
| Master volume | 0-100 % | 100 | host |
| Mute when unfocused | Off / On | Off | host |

### 3.8 Controls (input side only)

| Row | Values | Default | Changes how the game responds? |
|---|---|---|---|
| Bindings editor (player, device, actions, restore) | **built** (remap1, §7.9: INTERFACE > CONTROLS, the launcher's Controllers page, controls.ini) | the default layout | No: the same logical buttons |
| Stick dead zone | 0-50 % | 24 % (what the game sees today) | **Yes**: a smaller zone steers at smaller stick angles. Labelled |
| Trigger threshold (digital accelerate / brake) | 1-100 % | 25 % | **Yes**. Labelled |
| Vibration | Off / On (the game's own motor bytes to SDL rumble) | Off (built as RUMBLE, rumble1: §7.4) | No (the game sees its requests answered; gameplay does not read them) |

All of these act before the pad record, so a replay of a session is exact whatever they are set to.

**Analog ranges stay PS2-identical** (decided, remaster-features §3).
- No row rescales the stick or pressure values the game sees.
- The dead zone row only moves the existing host-side zone, and its default stays today's 0.24.
- A steering curve or sensitivity would change the values the game sees. They are not settings: they are listed
  under gameplay mods (§5). Gyro steering is built as an opt-in control (pad1, §7.12: off by default, per player in
  controls.ini; the reports are unchanged while it is off), as the boost toggle below.

Also from remaster-features §1 and §5 (host or input only):
- Audio output device.
- Boost toggle instead of hold: an input assist that changes what the game sees per poll. **Built opt-in** (2026-11-10,
  §7.1: INTERFACE > ACCESSIBILITY > BOOST Hold / Toggle, default Hold = the retail input byte for byte), labelled as
  changing the input.
- HUD colour-blind palette: **built** as HUD COLOUR FILTER (§7.1), a daltonize remap of the whole 2D layer.
- Text size: = HUD scale. **Built** as HUD SIZE (§7.1, textsize1): the race HUD's components scaled about their anchors;
  the menus (front end, pause menu) stay at 100 % (their text sits in fixed panels).

## 4. Where it lives

| Part | File | What |
|---|---|---|
| Model | `include/b3settings/{settings,page}.h`, `src/b3settings/{settings,page}.cpp` | the values and defaults, presets, the ini, the HSET record, the page's rows and edits. The menu model (`b3settings::Menu`, `src/b3settings/menu.cpp`; spec: `docs/specs/settings-page.md`): any number of tabs, our own UI items, a text-width callback, time only from the caller. Tests: `tests/test_settings.cpp` |
| Renderer | `src/b3replay/pcsettings.{h,cpp}`, `hud.{h,cpp}` (HSET decode), `live.cpp`, `main.cpp` (`--settings`), `view.{h,cpp}` (`apply_fov`, `glass_damage`), `effects.{h,cpp}` (one flag per effect), `b3gfx` (texture filter) | applies the settings: at start from `--settings <ini>`, then live as they change. An offscreen shot uses the settings of its own tick |
| In the game | `tools/native/pc_menu.h`, `src/b3replay/pcpage.{h,cpp}`, `src/b3replay/inproc.cpp` | the main menu's 7th row, the page drawn and driven in native_play (§7) |

## 5. Room for "GAMEPLAY MODS" (not built now)

Decided (2026-11-05):
- Gameplay mods are wanted, but they are not settings.
- They get a separate, clearly marked opt-in area, off by default.
- Any session with one on is marked as **modded**.

Room left in the design:
- A last tab, "GAMEPLAY MODS", after the settings tabs. It is set apart: a warning colour band and the line "Changes
  how the game plays. Sessions are marked as modded."
- Every row is off by default. A master switch sits on top.
- Stored in their own `[mods]` section of b3port.ini. They are never applied from the settings presets: no preset
  touches them.
- The marking:
  - one header line in the pad recording (`# mods <id>=<value>,...`) and in the scene capture;
  - the run log reports it;
  - online (when it exists) refuses or matches only equal mod sets;
  - the HUD shows a small "MODDED" tag in the front end [G placement].
- Where they would act: guest-side, at a poll boundary from that header, so a modded session still replays exactly
  when its header is honoured.

## 6. The rows

The tabs and rows as built (settings1, 2026-11-05, then mac5 and the feature lanes); the files: §4.

| Tab | Rows | Live |
|---|---|---|
| DISPLAY | QUALITY PRESET (PS2 original / Low / Medium / High / Ultra / Custom), FULLSCREEN, VSYNC, FRAME RATE LIMIT, (mac5) PAUSE WHEN UNFOCUSED, (fastload1, §7.8) FAST LOADING (ini `fast_loading`, default on), FRAME INTERPOLATION, RACE HUD LAYOUT, MENU LAYOUT (fe3: edges / 4:3 / panels), MENU MOVIE SIDES (fe3: black / blur); LATENCY: LATENCY MODE (lat1: auto / low / smooth / newest), PREDICTION (auto / 0-100 % of a tick: `--lead`), FRAMES IN FLIGHT (auto / 1-3) | VSync and the latency rows at the next start (auto = the run's own choice: low in a window, smooth headless); the rest live |
| GRAPHICS | ANTI-ALIASING (MSAA off / 2x / 4x / 8x), TEXTURE FILTERING (point, PS2 bilinear, trilinear, anisotropic 2x-16x), DITHER (off / on, perf1: native-renderer §33.8), HD TEXTURES (only with a pack), CAR DETAIL (game / highest), CAR REFLECTIONS (reflect1: Off / Original / Enhanced, default Enhanced; the PS2 original and Low presets use Original), CAR SHADOWS, SKID MARKS, ROAD SHINE, SUN AND LENS FLARE, CAR AND TRAFFIC LIGHTS | MSAA, filter and HD at the next start; the rest live |
| EFFECTS | MOTION BLUR, BOOST BLUR (0-100 %, default 50 = main's 0.5), BLOOM, POST QUALITY (PS2 / PC), (effects2, §7.18) SPARKS and CRASH DEBRIS (Off / Low / Medium / High, ini `sparks` / `popcorn` 0-3, default 3), SCRAPES, DAMAGE, GLASS DAMAGE; section POWER: (§7.18) BATTERY SAVER (Off / On, ini `battery_saver`, default off, row id 245) | live |
| CAMERA | FIELD OF VIEW (60-120, 90 = the game's), FOV CORRECTION, (splitlayout1, §7.11) SPLIT SCREEN (Top / bottom / Side by side, ini `split_layout` 0-1, default 0, row id 180) | live |
| INTERFACE | FPS COUNTER (fpsoverlay2: Off / Minimal / Standard / Detailed, ini key `fps_view` 0-3, default 0, row id 255; an older `fps_counter = true` reads as 2) and FPS COUNTER POSITION (ini key `fps_corner` 0-3, default 0 = top left, row id 256); BUTTON PROMPTS (mac5: Auto / PlayStation / PS4 / PS5 / Xbox / Nintendo / Generic / Keyboard, ini key `button_prompts` 0-6 (6 = PS4 / PS5, listed second), default 0 = Auto); VIDEO SKIP HINT (mac5: on / off, ini key `skip_hint`, default on); (§7.17, photorow1) PHOTO MODE IN PAUSE MENU (Off / On, ini key `photo_row` true / false, default on, row id 240); (§7.16, units1) SPEED UNITS (MPH / KM/H, ini key `speed_units` 0-1, default 0 = MPH, row id 220); (§7.19, 1.1) LANGUAGE (English / the imported languages, ini key `language`, default English, row id 270) and IMPORT LANGUAGES (launcher only, row id 271); DATE FORMAT (mac5: Month/Day/Year / Day/Month/Year, ini key `date_format` 0-1, default 0); (§7.15, launchskip1) OPEN THE LAUNCHER AT START (Off / On, ini key `launcher_at_start` true / false, default on, row id 210; read by the launcher only); AUDIO AND CONTROLS (a pointer to Driver Details > SETTINGS). Also on the launcher's Settings page | live (OPEN THE LAUNCHER AT START: the launcher's next start; SPEED UNITS: the speedometer from the next race) |
| INTERFACE, section ACCESSIBILITY | (§7.1) SCREEN FLASHES (Full / Reduced, ini `screen_flashes` 0-1, default 0); HUD COLOUR FILTER (Off / Deuteranopia / Protanopia / Tritanopia, ini `hud_colour_filter` 0-3, default 0); HUD SIZE (100% / 115% / 130% / 150%, ini `hud_size` 0-3, default 0: the race HUD only); BOOST (Hold / Toggle, ini `boost_mode` 0-1, default 0; **input-changing**, opt-in). Also on the launcher's Settings page (same rows) | live |
| INTERFACE, section CONTROLLER | (§7.4, rumble1) RUMBLE (Off / On, ini `rumble` 0-1, default 0), RUMBLE STRENGTH (10-100 %, ini `rumble_strength`, default 100), TRIGGER RUMBLE (Off / On, ini `rumble_triggers`, default off); (§7.12, pad1) ADAPTIVE TRIGGERS (Off / On, ini `adaptive_triggers` 0-1, default 0, row id 190; DualSense only, presentation only), TRIGGER STRENGTH (10-100 %, ini `adaptive_triggers_strength`, default 100, row id 191); (§7.13, carsel1) CAR SERIES ON L1 / R1 (Off / On, ini `series_shoulders` 0-1, default 0, row id 200; **input-changing**, opt-in); (§7.20, 1.1) ANALOG TRIGGERS (Off / On, ini `analog_pedals`, default on, row id 272). Also on the launcher's Settings page | live (native_play reads RUMBLE and CAR SERIES each VBlank) |
| INTERFACE, section CONTROLS | (§7.9, remap1) PLAYER, DEVICE, KEYS FOR, the 24 actions, RESET TO DEFAULTS; (§7.12, pad1) with DEVICE Controller: GYRO STEERING (Off / Mix / Replace; **input-changing**, opt-in), GYRO AXIS, GYRO SENSITIVITY, GYRO DEAD ZONE, GYRO RECENTRE (row ids 305-309; saved per player in controls.ini `[playerN.gyro]`, default off). native_play's page only; the launcher's Controllers page has the same | live |
| INTERFACE, section TIME ATTACK | (§7.2) GHOST (Off / Best lap, ini `ghost` 0-1, default 0): Time Attack ghosts, drawn only. Also on the launcher's Settings page | live (a lap from its next start) |
| INTERFACE, section SPEEDRUN | (§7.3) SPEEDRUN TIMER (Off / Event / Event + splits, ini `speedrun_timer` 0-2, default 0): drawn only; INSTANT RESTART (Off / On, ini `instant_restart` 0-1, default 0; **input-injecting**, opt-in). Also on the launcher's Settings page | live |
| INTERFACE, section REPLAYS | (§7.5) RECORD REPLAYS (Off / On, ini `record_replays` 0-1, default 0): each session kept as a replay file. Also on the launcher's Settings and Replays pages | next start |
| INTERFACE, section MUSIC | (native_play; remaster-features §6.1) MUSIC FOLDER (the system's folder dialog, $B3_MUSIC_DIALOG for scripts; ini key `music_folder`), REMOVE MUSIC FOLDER, PLAYLIST (EA Trax only / Custom only / Mixed, ini key `playlist_mode` 0-2, default 0 = the game's music untouched); (§7.14, nowplaying1) OTHER MUSIC APPS (Off / Show now playing, ini `other_music_apps` 0-1, default 0, row id 91; Windows and Linux, disabled on macOS; drawn only). Also on the launcher's Music page | from the next song (OTHER MUSIC APPS: live) |
| INTERFACE, section DEVELOPER | (§7.10, tune4) DEVELOPER MODS (Off / On, ini `developer_mods` 0-1, default 0): the tuning console. Also on the launcher's Settings page | next start |
| DEVELOPER | (§7.10, tune4; only while DEVELOPER MODS is on and the console is in the run, the last tab) SAVE AS PRESET; LIVE OVERLAY (status, F9); RESET ALL (asks: a second Cross); then the tabs of the game's tuning variables (CHANGED: what differs from its default now) one row per element with its value: Left / Right step it, Cross back to its default. **Gameplay-changing**: each edit is recorded | each edit at the next VBlank |
| SAVES | (native_play with a card; save1, remaster-features §8.1) SAVE FILE IN USE; SAVE FILE AT START (1-5, writes active-slot.txt); (autosave1, remaster-features §8) SAVE SCREENS (Hide / Show, ini `hide_save_screens`, default Hide, row id 230: the AUTOSAVE page after a race is not shown; presentation only); SAVE FOLDER (opens it); EXPORT SAVE; IMPORT SAVE (.sav / .psu / .max / PCSX2 card; system file dialog, $B3NATIVE_SAVE_DIALOG for scripts); LAST RESULT | next start (file, import); at once (export, SAVE SCREENS) |

Rows of §3 not built yet, with what each waits for:
- Render scale, monitor choice, exclusive fullscreen, HUD safe area, colour rows, track detail "highest",
  extended draw distance, PS2 scenery distances (the far model at type +0x28 decoded), car reflections and water
  reflections switches: renderer work (M); next.
- Master volume / mute when unfocused, dead zone: host-side rows, not on the page yet.
- Particle and debris density: hidden until those effects are drawn. Glyphs: the atlas.

## 7. Native (mac5, 2026-11-09)

The page in the native game (native_play + the in-process renderer):

| Part | File | What |
|---|---|---|
| Main menu rows | `tools/native/pc_menu.h` | wrappers on the installed native table: Prepare__19CB3VSelectComponent (0x321470) and Prepare__19CB3HelpBarComponent (0x31C350) for the main menu's own components get 7-row copies (free guest RAM 0xC0000, checked zero) with PC SETTINGS and QUIT GAME; the main menu's Update (func_003181E0) takes Confirm on those two rows (the game's code would read its 5-row arrays past the end). `B3NATIVE_PC_ROW=0`: retail's five rows |
| Fonts | `tools/native/gameview_native.cpp` (`publish_fonts`), `src/b3replay/hud.{h,cpp}` (`HostFonts`) | the graphics manager's three CGt2dFont (D_00665EC0 +0x10/+0x14/+0x18) published once as 'HFN1' at 0x41102000 (texture, cell scale, glyph records); text laid out as CGt2dFont::RenderString / GetStringWidth do, with the fonts' captured HUD textures |
| Page | `src/b3replay/pcpage.{h,cpp}` | `b3settings::Menu` on the window's pad (NativePad::last; the game is fed a neutral pad from the VBlank of the request until the page has closed and the pad is released: tools/native/page_hold.h), items as host-UI quads (the game's slanted menu face, docs/specs/settings-page.md §1), edits applied at once (`apply_settings`), saved on close |
| API | `src/b3replay/inproc.cpp` | `b3r_settings_open`, `b3r_settings_active` |
| ini | `src/b3replay/main.cpp` | `--settings <file>`, else (a window with `--native-pad`) `<SDL pref path b3port/native>/pc_settings.ini`; offscreen runs read no user file |

Replays: the pad recording holds what the game saw (neutral while the page was open), so a replay reproduces the game
without reopening the page. QUIT GAME ends native_play like closing the window.

### 7.1 Accessibility (2026-11-10; remaster-features "Accessibility")

INTERFACE tab, section ACCESSIBILITY (a section, not a 7th tab: the tab bar's width). The launcher's Settings page shows
the same rows (it builds them from `b3settings::build_content`). Every default is the game exactly.

| Row | Values (ini key) | Default | What it does | Kind |
|---|---|---|---|---|
| SCREEN FLASHES | Full / Reduced (`screen_flashes` 0-1) | Full | Reduced: (1) a 2D draw over at least half the screen with a white-ish fill (every channel at least 60 % of the 2D's white; never a picture, a coloured panel, the letterbox or the pause shade) lifts the picture by at most 25 % of white (its alpha, or for additive / opaque fills its colour, scaled); (2) the TV static (the camera-change transition: "Camchange" .. "Camchange5", a new noise frame every 2 polls = 15 Hz flicker over most of the screen) is drawn from its first frame only: still noise that fades out as the game fades it | drawn only (`src/b3replay/a11y.{h,cpp}`, `View::hud_list`) |
| HUD COLOUR FILTER | Off / Deuteranopia / Protanopia / Tritanopia (`hud_colour_filter` 0-3) | Off | a daltonize remap of the whole 2D layer (race HUD, menus, prompts' glyphs, our pages): Machado 2009's dichromat simulation subtracted, the lost difference moved into the visible channels (Fidaner 2005's shift). Applied to the vertex colours and to remapped copies of the 2D textures (and the still pictures' side fill); never the 3D scene, never the movies or the globe | drawn only (`a11y.h`, `View::hud_list`, `host_ui_list`) |
| HUD SIZE | 100% / 115% / 130% / 150% (`hud_size` 0-3) | 100% | the race HUD drawn larger, each HUD component scaled about its own anchor (below); the menus stay at 100 % | drawn only (`a11y.h` hud_size_scale / fit_scale / resolve_overlaps, `View::hud_list`) |
| BOOST | Hold / Toggle (`boost_mode` 0-1) | Hold | Toggle: while driving, a press of R1 (any key or pad button bound to boost) latches it; the reports carry R1 held (bit and full pressure) until the next press, the end of the driving context (menus, results) or Start (pause) | **INPUT-CHANGING, opt-in**: the game reads other pad reports than the player's hands made (`src/native/input_keys.h` BoostLatch, applied in NativePad::sample after the keys and pads are combined). The pad recording holds the latched reports, so replays stay exact |

#### HUD SIZE (textsize1, 2026-11-13)

The accessibility text size, built as a scale of the race HUD (the row is named for what it scales: HUD SIZE, not TEXT
SIZE). 100 % runs none of it: `View::hud_list` builds the same items as before, so the frames are the game's byte for
byte (golden below). Drawn only: nothing reaches the game, its pad report or its timeline; the setting is a renderer
field like HUD COLOUR FILTER (saved in pc_settings.ini, applied at once, carried in the settings record the shots and
replays already use).

**What scales.** In a race frame (not the front end, not split screen) each draw is put in a group:
- a listed HUD component (hud2's CB3HUD component list) with an EHUDAlign of 0..7: scaled about its anchor, the point
  (fx, fy) of D_004B7650 in the HUD rect (CB3HUD +0x48 / +0x50, captured: x 30..610, y 22..458), moved with the
  component by the wide layout's shift ((2 fx - 1) x the extra half width) so the pivot sits on the window's edge.
  The pivot is kept inside the visible area (the rect's bottom, y 458, lies below the 448-line raster: the bottom
  anchors scale about y 448). A corner component grows from its corner toward the centre, a top-centre one (SCORE)
  down and to both sides. Seen in races: POS (align 1, top left), LAP (7, top right), SCORE (0, top centre), the
  boost bar (3, bottom left), the speedometer (5, bottom right), CRASHBREAKER in N (3, crash mode);
- a component with align 8 (absolute: the race's messages; in the shots TAILGATING with its sign and counter,
  component 0x66B350; the TAKEDOWN / AFTERTOUCH messages were not caught in a shot): scaled about its own box's centre,
  as a message;
- the free stylised message texts (CB3StylisedText2dObject::_RenderNormal 0x3CFE40 outside the listed components:
  "Get Ready!"): per object, about its own centre, as a message;
- the EA TRAX "now playing" panel (texture EATrax and the song text inside its box): about its own bottom-left corner,
  as a message (it is a passing popup).

A panel's bar laid against the 4:3 edge (the POS / LAP / CRASHBREAKER bars: textured edge pieces, which the wide
layout takes to the window's edge) scales with its component, its edge vertices staying at the window's edge, so the
bar grows with its text. Clip regions and the far-side clip of a moved component scale with it.

**What does not scale (100 % at every size):** the front end (every menu); the overlays made of its panels over a
race (the pause menu, the event intro's heading and mission text, crash results: `hud_overlay_panels`); split-screen
frames; the markers over the rival cars (world-anchored, sized by distance); bands and fills across the screen (the
crash letterbox, full-width bars); untextured fills laid to an edge. The menus are left at 100 % because their text
sits in panels the game lays out for its font size (menu rows in fixed boxes, the prompt bar, dialog boxes): scaling
the glyphs overflows the panels, and scaling whole panels about an anchor runs them into the heading bar, the prompt
bar and each other at 130 % and more -- no form of it was shown safe offscreen, so the menus keep their size.

**Clamping and overlaps** (a11y.h, unit-tested in test_a11y hud_size_*):
- `fit_scale`: the largest scale in [1, the asked one] that keeps the group's box (its draws where they land, inside
  their scissors) inside the visible 2D area (the window in the wide layout, the 4:3 box in the 4:3 layout). A side
  already past the screen at 100 % (a message sliding in) does not limit it.
- `resolve_overlaps` (2 units of gap): two groups that were apart at 100 % and would meet are capped. A message (align
  8, the free texts, EA TRAX) gives way alone, so the steady HUD keeps its size; two steady components share one cap,
  the largest at which they stay apart. Groups that already touch at 100 % are left as the game has them.
- Caps seen in the proofs below (B3_HUD_SIZE_TRACE; every other group took the asked scale in every layout): at 4:3
  only, where the window has no room beside the 4:3 box -- the TAILGATING message at 150 % takes 1.26 (it gives way
  to POS, top left), the EA TRAX popup takes 1.10 at 130 % and 1.00 at 150 % (it gives way to the speedometer, its
  row's right end). At 16:9 and 21:9 nothing is capped. The steady components (POS, LAP, SCORE, boost, speedometer,
  CRASHBREAKER) never meet each other up to 150 % in any of the three layouts.

**Diagnostics:** `B3_HUD_SIZE_TRACE=1` (each group's box, pivot and scale per HUD build); `B3_HUD_SIZE_SHOTS=1,2,3`
(an offscreen shot is also written at those sizes, `live_<poll>_<alpha>_hud<percent>.png`, only the HUD's items built
again: exact side by sides of one frame).

Proofs (2026-11-13, Linux offscreen, STRICT=1 race_check with `RENDER_EXTRA="--size 960x720"`, `1280x720` and
`1680x720` -- 4:3, 16:9, 21:9 -- and `B3_HUD_SIZE_SHOTS=0,1,2,3`; every run PASS):
- Defaults: `tools/golden/baseline.sh` (JOBS=1, RATE_RUNS=0) compared with the recorded golden: 140 PASS (every
  scenario's shots, kernel trace, DMA, SIF, RW Audio and RAM hashes equal), the only FAIL "rate: no rates" (no rate
  runs asked). `make test`: test_a11y hud_size_levels / anchors / clamp / overlaps, test_settings accessibility_rows.
- Race (default scenario, shots 10700 / 11500): the event intro (10700) is byte-identical at every size; the race shot
  (11500: POS, LAP, SCORE, boost, speedometer, TAILGATING) differs at each size, every component on screen and apart.
- Crash (SCENARIO=crash, 12000 .. 12900): the speedometer and CRASHBREAKER in N scale; the crash results menu and the
  loading screen are unchanged.
- Pause (SCENARIO=pause, 10900 "Get Ready!", 11050 the EA TRAX popup, 11620 / 11800 the pause menu): 10900 and 11050
  scale; the pause menu shots and the paused frame without HUD (11500) are byte-identical at every size in all three
  layouts (the variants are drawn with the bloom's kept picture started again, so a 100 % variant and a 150 % one
  differ only where the HUD does).

Where the flashes are [C B3_FLASH_TRACE=1: every tick's 2D draws over 30 % of the screen and the game's post values, on
change; race_check takedown and crash, 2026-11-10]: no race tick of either scenario draws a large light 2D fill; the
bloom's threshold / intensity are constant in races (0.75 / 1.0); the TV static runs in the car select (polls
10072-10079 of the takedown scenario). The game's own white flash at an explosion (CB3ExplosionManager::RenderFullScreenFlash,
a 3D-stage fill) is drawn since 2026-11-16 (`Effects::draw_explosion_flash`): one additive full-view fill (colour
255 / 204 / 127, alpha t^2 x 128.9, t falling 1 -> 0) from 0.025 s to 0.145 s after the crash time stamp; SCREEN FLASHES
Reduced scales its alpha by `flash_cut` (a11y.h `explosion_flash`), so it lifts the picture by at most 25 % of white
(tests/test_a11y.cpp explosion_flash_curve / explosion_flash_reduced).

Proofs (2026-11-10, Linux, `--size 1280x720`; the base = mac5 HEAD f81b3fd6's libb3render.so with the same native_play):
- Defaults, frames: race_check STRICT=1 takedown (13 shots: menus 8000 / 9300, the static 10072 / 10074 / 10076, the
  race 10300 .. 13300) and crash (9 shots): every shot byte-identical to the base's; the pad recordings identical after
  their first line (the first sample's VBlank is host timing).
- Defaults, input: test_input_keys boost_toggle_latch (Hold: every report untouched, driving or not).
- SCREEN FLASHES Reduced (takedown): the three static shots differ, the other 10 are byte-identical to Full; the noise
  of consecutive static shots correlates 0.85 / 0.93 (Full: 0.005 / -0.007, a new frame each time); no fill was capped
  in the run (no false positives: the log's `[a11y] screen flash capped` lines are absent). Unit tests: test_a11y
  flash_cap, static_frames.
- HUD COLOUR FILTER Deuteranopia (takedown): every shot differs, only on the 2D (a diff mask of race shot 11000: the POS /
  LAP / speed panels, GO, the rival labels, the EA TRAX logo; the 3D, its red barrier crosses included, untouched).
  Unit tests: test_a11y (greys and alpha kept; every confused pair of a 6 x 6 x 6 colour grid seen further apart by the
  dichromat, on average > 3x).
- BOOST Toggle: `SCENARIO=boosttoggle tools/native/race_check.sh` (two 10-VBlank taps of Left Shift at 21400 / 22800:
  R1 held from the first to the second in the pad recording, the log's latch / release lines).

### 7.2 Time Attack ghosts (ghost1, 2026-11-10; remaster-features §5 "Ghosts for Burning Laps")

INTERFACE tab, section TIME ATTACK (before ACCESSIBILITY): **GHOST** Off / Best lap (ini `ghost` 0-1, default 0 = Off).
Code: `src/b3replay/ghost.{h,cpp}` (format, time sync, recorder), `LiveScene::ghosts` (live.cpp), `View::ghost_draws`
(view.cpp), `LapState` (capture.h).

- **Off** (default): nothing runs -- no file read or written, nothing drawn; the frames are byte-identical (proofs below).
- **Best lap**: in Time Attack (SINGLE EVENT > TIME ATTACK; the stage logic is CB3OfflineBurningLapLogic, vtable
  0x4DEBC0, seen in every Time Attack tick and in no other mode's), every tick's player race car (the race car block
  gWorld+0x12AD50, Vehicle::id) goes to the recorder with the game's own lap state: CB3Score (block +0x10C0) +0x14 the
  lap (-1 until the start, 0.. after; wraps after 8), +0x2C8 the lap's time [C CB3Score::Update 0x15B000,
  IncrementLapCount 0x15A1F0]. Both are already in the capture image (perf1's 16-byte grabs of +0x10D4 / +0x1384): no
  capture change, no new read of guest memory, no write. Time Attack has no lap count (+0x2C4 = 0): laps run until the
  player quits.
  - A lap = the samples from the tick its index changed (time 0; the start line, or the line ending the lap before) to
    the tick the index moves to the next one (the timer there still holds the finished lap's time: the lap time). A
    restart (back to -1), leaving the mode, the timer going back, a gap over 2 s or a missed start (joined mid-lap)
    drop the lap. Paused ticks (the timer held) add nothing.
  - The best lap per track + car is kept in `<prefs>/ghosts/<REGION_ROUTE>-<CLASS_CAR>.b3ghost` (e.g.
    `US_C2_V2-COMP_CAR1.b3ghost`; prefs = SDL_GetPrefPath("b3port", "native"), `--ghost-dir` for tests; offscreen runs
    use none unless given). The file: "B3GH", version 1, flags, lap time, the track and car as names, per sample (60 Hz)
    the lap time, main / body / wheel matrices (f32 position, i16 rotation) and the wheel speed, then an FNV-1a checksum;
    written to `.tmp` and renamed. ~11 KB per second of lap. Only our own transforms: no game data.
  - Playback: the best lap of the loaded route (any car; a real lap beats a test lap, then the shorter time) is drawn
    after the cars at the pose of the current lap's time (the game's timer between ticks blended; the samples around it
    slerped), with its own car file (loaded from the disc as any car the renderer shows; skipped if the disc lacks it),
    the panels at their file frames on the body, the recorded wheels, lit with the player's car's light, through the
    car shader's screen-door fade (coverage 0.45, less within 6 m of the camera, gone within 2 m): no shadow, coronas,
    skid marks, sound, collision or AI awareness. Past the ghost lap's end it is not drawn. A new best replaces it at
    once (from the next lap).
- Diagnostics: `B3GHOST_DEBUG=1` logs the lap state on change (and every 300 polls) and every ghost draw against the
  player's car. **Test laps**: `B3_GHOST_TEST_LAP=<s>` makes a lap's first <s> seconds count as a (test) lap, flagged
  in the file: a scripted drive (accelerator held, no steering) does not finish a lap of US/C2_V2 in 185 s, so the
  automated check records the first 25 s. A test lap never replaces a real one.

Proofs (2026-11-10, Linux, `--size 1280x720`, STRICT=1; the base = mac5 HEAD 3abf033e's libb3render.so beside the same
native_play):
- GHOST Off (the default), frames: race_check SCENARIO=timeattack (7 shots: the menu 10300, the grid 10700, the lap
  11000 .. 13000) and the default race scenario (6 shots), each with `--ghost-dir` pointing at a folder holding a
  ghost of that track: every shot byte-identical to the base's; the pad recordings identical after their first line
  (host timing).
- The lap state: `B3GHOST_DEBUG=1` on SCENARIO=timeattack: the logic vtable 0x4DEBC0 from the event's loading (poll
  10321) on, lap -1 at the grid, 0 from poll 10785 (the start line, before the countdown ends), the timer from poll
  ~10919 (GO) on: 3.017 s at poll 11100, 9.68 at 11500 = the HUD's 00:09:67 in that shot; laps 0 (no count).
- Record (B3_GHOST_TEST_LAP=25, GHOST Best lap): `[b3ghost] lap 1 completed on US/C2_V2 with COMP/CAR1.BGV: 25.000 s
  (a test lap), 1500 samples`, saved (282,053 bytes) and loaded at once as the track's ghost.
- Playback, the same drive (deterministic: the player drives exactly the recorded lap): at every one of the 175
  ghost draws (lap time 0.95 .. 24.68 s) the ghost is 0.000 m from the player's car (`[b3ghost] drawn at lap time ..:
  ghost (x, y, z), player (x, y, z)`): the time sync and the stored transforms are exact. Past 25 s (shot 12600) no
  ghost is drawn. No new file was written (no test lap set; the file byte-identical after).
- Playback, the accelerator lifted at VBlank 21040 for 2 s: the ghost leads by 1.8 m at 0.95 s, 27.6 m at 3.0 s, 67 m
  at 6.35 s; the shots show it see-through (the 4 x 4 screen door), ahead in the player's lane (shots 11000 / 11100 /
  11300). Two runs of this give byte-identical shots.
- Unit tests: test_ghost (format round trip incl. a mirrored wheel matrix, checksum / truncation / version / order
  rejected, the atomic save replacing a file, the best of a track across cars with a damaged file and a test lap
  present, names, the time sync's sample choice and blend, the recorder: laps, the line tick, pauses, the 8-lap wrap,
  joined mid-lap, restart, leaving the mode, timer jumps, gaps, no car at the line, test laps); test_settings ghost_row.

### 7.3 Speedrun timer and instant restart (speedrun1, 2026-11-12; remaster-features §5 "Speedrun support")

INTERFACE tab, section SPEEDRUN (after TIME ATTACK): **SPEEDRUN TIMER** Off / Event / Event + splits (ini
`speedrun_timer` 0-2, default 0) and **INSTANT RESTART** Off / On (ini `instant_restart` 0-1, default 0). Code:
`src/b3replay/speedrun.{h,cpp}` (time text, the .b3split file, the tracker, the panel's rows), `speedrunpanel.{h,cpp}`
(the drawing), `LiveScene::sr_tick` / `speedrun_rows` (live.cpp), `ScoreState` (capture.h), `RestartMacro`
(src/native/input_keys.h), `NativePad` (nativepad.h).

Where the times come from [C decomp]: view 0's CB3Score (race car block gWorld+0x12AD50, +0x10C0). CB3Score::Update
(0x15B000) sets +0xC, the race time, to the game timer (gWorld+0x20) minus the start time (+0x280) from the start signal
(race state +0x27C: 0 countdown, 1 start, 2 racing, 3 finished) and stops updating it once the state is 3;
IncrementLapCount (0x15A1F0) stores each lap's time (the lap timer +0x2C8 at the line) in +0x1C[8] and, at the last
line, calls CB3RaceCar::SetFinished (0x151780), whose func_00155A40 writes the finish state to +0x8 (1 finished; 2 / 3
out, with +0xC set to FLT_MAX) and the race state 3, and copies the score to the car's +0x16C0. The race results page
takes its entry from that copy: func_003F7AE0 -> func_00272DD0 reads +0x8, +0xC (the race time) and +0x3C (the best
lap). So the time the results show is +0xC at the finish tick, the value the timer freezes on. The HUD's own lap timer
(Time Attack) prints +0x2C8 through func_00394660: func_00155280 truncates to hundredths, then "%02d:%02d:%02d"
(D_004C98C0) of minutes, seconds, hundredths; the panel prints with the same two steps (speedrun::time_text, float
arithmetic statement for statement). No wall clock: a paused game holds the game timer, so the timer holds.

The capture gains two read-only 16-byte-block grabs of that CB3Score: [+0x10C0, +0x1100) and [+0x1330, +0x1350)
(src/gameview/scene_capture.cpp); [+0x1380, +0x1390) (lap count, lap timer) is perf1's. Nothing is written.

- **Off** (default): no tracker, no file read or written, nothing drawn (frames byte-identical: proofs below).
- **Event**: a panel at the right edge under the LAP panel: TIME (the race time, 00:00:00 in the countdown; FINISH and
  gold once finished, "--:--:--" for an out) and BEST (the kept best of this event + car). Time Attack (lap count 0, no
  end): LAP n (the running lap's time) and BEST LAP.
- **Event + splits**: also each lap done (LAP k/N and its time, green when faster than the best run's lap k, red when
  slower; the last 5) and the running lap; Time Attack: the last 4 laps. At the finish: NEW BEST with the gain, or BEST
  and the loss.
- Modes with a timer (the stage logic's vtable): RACE 0x4DE980, TIMEATTACK 0x4DEBC0 (also Burning Lap events),
  ELIMINATOR 0x4DECE0, ROADRAGE 0x4DEE00, RACE2P 0x4DFB80, ROADRAGE2P 0x4DF660 (player 1's timer). Crash modes and online
  logics: none.
- **Best times**: `<prefs>/speedrun/<MODE>-<TRACK>-<CAR>-<LAPS>L.b3split` (e.g. `RACE-US_C2_V2-COMP_CAR1-3L.b3split`;
  `--speedrun-dir` for tests; offscreen runs keep them in memory unless given one). "B3SR", version 1, the event key
  as text, the best event time, its lap splits, the best single lap, FNV-1a; written to `.tmp` and renamed. A finish
  (state 1 or 4) better than the kept time replaces it; any lap better than the best lap replaces that (Time Attack:
  every lap counts).
- The tracker gets every tick as the feed keeps it (LiveFeed::on_push), so no lap is missed however long a frame or an
  offscreen shot takes; the panel of a tick is a function of that tick and the marks up to its poll (a shot of poll N
  shows N). An attempt starts at the event's first race tick and again whenever the game goes back (state below
  racing again, the race time or the lap index back: the game's own restart, or the pause menu's).
- `B3_SPEEDRUN_DEBUG=1` logs the CB3Score fields on change and every 300 polls; the marks (reset, start, lap, finish,
  out) are always logged while the timer is on.

**INSTANT RESTART On** (opt-in, **input-injecting**): F5 (the window's key or a script's), or SELECT + START on player
1's controller, while driving (native_play's driving context: the game read the accelerator in the last 4 VBlanks),
starts a macro on the pad's VBlank grid: Start (the pause menu opens on RESUME), Down at +60 VBlanks (RESTART), Cross at
+90 (the game's dialog "This will restart the current event ... Are you sure you want to restart?", string 0x136,
opening on NO), Up at +140 (YES), Cross at +170; each press held 6 VBlanks, the pad the macro's alone for 200 VBlanks,
then the player's again. The game then restarts the event itself (its loading screen, a new countdown). The macro is
the pause page's own choice path [C func_00342090: row 1 opens dialog 0x136]; it never writes game memory, and the pad
recording holds the injected reports, so a replay is exact. Asked while not driving (menus, results, a crash's own
camera) it is dropped (logged). Off: F5 does nothing (it is bound to no game control) and Select + Start reach the game
as always. On, a gamepad's Start pressed with Select held is the macro's (its own Start opens the pause menu).

Proofs (2026-11-12, Linux, `--size 1280x720`, STRICT=1, `--settings` with `speedrun_timer = 2`, `instant_restart = 1`;
shots and log excerpts in `%B3_WIN%\speedrun1-evidence\`):
- **The digits are the game's** (SCENARIO=timeattack, shots at four consecutive polls 11000-11003): the game's HUD lap
  timer reads 00:01:33, 00:01:36, 00:01:37, 00:01:39 and the panel's LAP 1 reads the same four (lap times 1.3499999,
  1.36666679, 1.38333321, 1.4000001: the PS2 arithmetic skips :34, :35 and :38 there, and the panel with it). An
  earlier build printing in host IEEE floats was off by one hundredth in 2 of 4 shots (26.35 s: the HUD 00:26:34, IEEE
  00:26:35): hence the PS2 float model. The HUD and the panel read CB3Score +0x2C8 at the same tick. The plain
  timeattack scenario with the timer on (shots 10700 / 11000 / 11001 / 11500): race_check PASS, poll 11500 HUD 00:09:67
  = panel LAP 1 00:09:67 (lap time 9.6833334).
- **Race** (the default race scenario): TIME 00:00:00 through the countdown, the start mark at poll 10962 (race time
  0.016667: the first racing tick), then TIME = the tick's +0xC (e.g. poll 11500: 8.983334 -> 00:08:97, LAP 1/3 the
  same), BEST --:--:-- (none kept). A scripted drive does not finish a lap, so the FINISH freeze and the results page
  are verified from the decomp (above), not from a run: they are still to check in a window.
- **Instant restart** (race, F5 at VBlank 22000): the log's "instant restart started (the pause menu's RESTART, YES)",
  the shots show the pause menu with RESTART selected (poll 11975), the game's restart dialog on NO (12010), YES
  selected (12045), the loading screen, a new countdown (12400: TIME 00:00:00), the tracker's reset (poll 12150) and
  start (12484) marks, then TIME from 0 again (13300: 00:13:60 = race time 13.616667). Time Attack likewise (F5 at
  VBlank 22500: reset 12649, start 12983). race_check PASS in that run.
- **Defaults**: `JOBS=1 RATE_RUNS=0 tools/golden/baseline.sh build/native_play build/rec_sr` ("baseline: PASS
  recorded 20 scenarios"), then `tools/golden/compare.sh ../golden/rec_det build/rec_sr`: every item of
  the 20 scenarios PASS (verdicts, shots, time traces, DMA / SIF / RWA logs, RAM hashes at every grid VBlank), "rate"
  FAIL only (no rate runs recorded); `golden_compare_defaults.txt` in the evidence folder.
- Unit tests: test_speedrun (the time text in the PS2 model incl. 26.35 -> 00:26:34; the .b3split round trip, every
  one-byte change and truncation refused, versions, the atomic save; the tracker on synthetic ticks: countdown, start,
  laps, finish, outs, restarts mid-race and after a finish, pauses, leaving, crash modes, joining late, ticks without
  the car, Time Attack laps across the 8-lap wrap; the panel's rows), test_input_keys restart_macro (idle: reports
  untouched; the macro's presses at their VBlanks, alone; the player's reports after), test_settings speedrun_rows.
### 7.4 Controller rumble (rumble1, 2026-11-12; remaster-features §5 "Modern controller features")

INTERFACE tab, section CONTROLLER (before TIME ATTACK; also on the launcher's Settings page):

| Row | Values (ini key) | Default | What it does | Kind |
|---|---|---|---|---|
| RUMBLE | Off / On (`rumble` 0-1) | Off | On: the game's own vibration requests are acknowledged as a PS2's device manager does and their motor values go to the pads: port 1's to player 1's pads, port 2's to player 2's | **GAME-VISIBLE, opt-in** (below) |
| RUMBLE STRENGTH | 10-100 % (`rumble_strength`) | 100 | the motor speeds the pads are given, scaled | host only |
| TRIGGER RUMBLE | Off / On (`rumble_triggers`) | Off | also the triggers' own motors where a pad has them (Xbox One / Series impulse triggers; SDL's trigger-rumble capability): both follow the large motor at half strength, the right one with the small motor's buzz | host only |

The game's own VIBRATION option (DRIVER DETAILS > SETTINGS > CONTROLLERS) still decides whether it asks at all.

**What the game sends** [C decomp; code and details in `src/native/pad_actuator.h`]: CB3InputManager's frame update
(func_00213F00) sets each player's DualShock motors -- the large one (func_00217200) to 255 x clamp(3 x x, 0, 0.9999),
i.e. 0-254, the small one (func_002171D0) on / off -- only while that player's vibration is allowed (func_0022C340).
CGtInputDevicePS2DualShock::UpdateVibration (0x217070) sends them **only when they change** (zeroes while paused,
SetPause 0x217060) through libvib's sceVibSetActParam (profile 3, data = (large << 1) + small), which packs a block for
libdbc's sceDbcSendData2. libdbc sends it as a no-wait RPC to the IOP's device manager unless the previous one is still
in flight (then the request is dropped, return 0). **The acknowledgement** is that RPC's reply: until it comes the
channel is busy. Before rumble1 nothing answered, so the first request (the first crash) stayed in flight for good.
Seen with RUMBLE On: crashes and contacts send the large motor (254 at an impact, decaying over ~0.2 s; small
motor never on in the checked runs), about 1 request every 2 VBlanks while it changes.

**Off** (default): the platform is the port before rumble1 (the first request taken and left pending; no motor
driven). **On**: each request is answered 0.1 ms of virtual EE time after it ([G] a SIF RPC round trip; in the
multiplayer tour the two ports' requests of one frame are ~700,000 cycles apart, so neither is ever dropped) and its
motors kept per port (`sif::pad_motors`); native_play hands them to the renderer every VBlank (`b3r_rumble`), whose
NativePad gives them to the pads (`SDL_RumbleGamepad`: large -> low frequency, small -> high frequency; refreshed every
500 ms while running, each effect 1 s long; stopped when RUMBLE goes Off or a port's values are 250 ms old: the game
held or ended).

**Why gameplay cannot change** [C]: the game never reads the answer. UpdateVibration ignores sceVibSetActParam's
result and records the values as sent either way; nothing else reads the motor fields, and the motor values come from
the input manager's own intensities (written by the game, never read back from the pad). The only difference On makes
to the guest is the result value (left in $v0 of a void function) and the virtual time of the RPCs made: an accepted
request charges libdbc's sceSifCallRpc (~125 EE cycles; 36 in a crash event, 164 in the race scenario). The game's
state would follow it only if a frame were pushed past a VBlank by those cycles. Measured (proofs below): with RUMBLE On
the whole 32 MB of guest RAM equals the Off run's but for the game's raw clock words, each larger by exactly 125
cycles per accepted request (gmuTimeLSP 0x4E2E40, `lasttime$49` 0x4E2B30, and the copy at 0x51BA58 / 0x51BA60):
cars, traffic, crash, score and every other byte the same. A pad recording keeps the RUMBLE switches (`rumble <vblank> 0|1`), so a
replay answers the same requests the same way.

Diagnostics: `B3NATIVE_RUMBLE_LOG=1` logs each acknowledged request (`platform: rumble: VBlank N cycles C port P small S
large L`) and each change a pad is given (`[b3pad] rumble: port P small S large L -> low .. high .. triggers .. ..`);
`B3NATIVE_RUMBLE=0|1` overrides the setting; `race_check.sh RUMBLE=1` runs a scenario with RUMBLE On.

DualSense adaptive triggers: built by pad1 (§7.12; ADAPTIVE TRIGGERS, the next rows). Not built: HD rumble beyond
SDL's two motors.

Proofs (2026-11-12, Linux, offscreen, no pad; branch rumble1 on mac5 21bccd9f):
- **RUMBLE Off (default) = the golden**: `JOBS=1 RATE_RUNS=0 tools/golden/baseline.sh build/native_play build/rec_r`
  (`baseline: PASS recorded 20 scenarios`), `tools/golden/compare.sh ../golden/rec_det build/rec_r`:
  all 140 items PASS (verdict, shots, trace, dma, sif, rwa, ram of the 20 scenarios); only `rate` FAILs (no rate runs).
- **RUMBLE On**: `RUMBLE=1 SCENARIO=crash STRICT=1 tools/native/race_check.sh` PASS, `rumble: 36 actuator requests
  acknowledged, 0 found the channel busy (port 1 large peak 254 ...)`; the default race PASS with 164 requests;
  `SCENARIO=multiplayer` PASS with 830 (both ports: port 2 large peak 254; the two ports' requests of one frame
  ~700,000 cycles apart, none dropped). Shots and VU0 launch counts equal to the golden's. The crash's motors
  (`B3NATIVE_RUMBLE_LOG=1`): VBlank 21969 large 254, 21977 83, 21979 8, ... 21985 0; again 254 at 22171, 191, 18, 1, 0.
- **What On changes in the guest**: baseline.sh with RUMBLE=1 for crash, race and takedown: verdicts and shots PASS,
  call counts equal; trace / dma / sif / rwa / ram differ from the first request on (virtual time moved). RAM dumps
  (`B3NATIVE_RAM_DUMP`) of the crash scenario at VBlanks 22001 and 26901, Off vs On: 4 and 3 words of 32 MB differ,
  all clock words (above), by 1000 and 4375 cycles = 8 and 35 requests x 125.
- Windows: `RENDER=1 tools/native/build_win.sh` builds native_play.exe and b3render.dll.
- Unit tests: test_pad_actuator (decode of the game's block and of partial / bad ones, both channel modes and the mode
  switches, the SDL values, the pad recording's switches), test_settings rumble_rows.
### 7.5 Replay files (replay1, 2026-11-12; remaster-features §5 "Replay files")

INTERFACE tab, section REPLAYS (after TIME ATTACK): **RECORD REPLAYS** Off / On (ini `record_replays` 0-1, default 0 =
Off). Read once, at the start of a session (`b3r_record_replays`). Code: `src/native/replay_file.h` (format, recorder,
player, compatibility, folder), `tools/native/native_play.cpp` (`--record-replay`, `--replay`), the launcher's Replays
page (`src/b3launcher/main.cpp` page_replays), `tools/native/menu_mouse.h` (typed text recorded / fed back).

- **Off** (default): nothing recorded, nothing hashed, no settings latched: the run is the same as before (golden
  baseline equal).
- **On** (a window session, not a replay itself): from the boot to the end, native_play records what it gives the game
  before each VBlank's pad_vblank(): port 1's and port 2's reports (only changes; "none this VBlank" too), port 2
  plugged in / pulled out, the characters the on-screen keyboard took, the date format, speed units and music settings (latched
  per VBlank, so the game reads the recorded values on the recorded VBlanks), and a hash of guest RAM MB 0..30 every 600
  VBlanks. Before the boot: the card as the game gets it (the active slot's profile, as ProfileCard::attach loads it,
  stored as a .sav), the game-side environment (`replay_file.h` kGameEnv: the console clock B3NATIVE_RTC, the card's
  insertion VBlank, ...), the idle skip / movie skip options, the disc program's size and FNV-1a 64, the port version.
  At the end the file is written atomically to `<prefs>/replays/<YYYYMMDD-HHMMSS>.b3replay` (a few hundred KB for a
  session of several minutes) and the newest 20 automatic names are kept.
- **Playback** (`native_play --replay <file>`, the launcher's Replays page > Play): the file is checked first (the same
  disc program and port release, a format this port reads, the CRC, the custom music folder's file list and sizes when
  the recording used one), then its environment and options replace the run's, its card goes into a throwaway folder
  in the system's temp folder (written to as the game saves, deleted at the end: the player's saves are never
  touched), and each VBlank gets the recorded ops. The PC SETTINGS page is not opened (the recording holds the neutral
  pad the game saw meanwhile); the window's controls are ignored except Start (Esc), which pauses the replay (virtual
  time stops). It ends after the recording's last VBlank. Every checkpoint is compared; the end of the log says
  `replay: N of N checkpoints matched; it played as recorded`, else the VBlank where it parted (and a window shows a
  message box at the end).
- Not recorded, by design: anything drawn only (every other row on this page; photo mode, ghosts), pacing, the window's
  focus (the game holds on virtual time).
- Proof: `tools/native/replay_check.sh` -- race and crash recorded, then played from the file: equal time trace, DMA, SIF, RW Audio, RAM and shots (compare.sh PASS), every checkpoint matched; multiplayer (port 2) and profilename (typed text) equal logs too; a replay with one press moved 2 s parts at the next checkpoint.
### 7.6 Photo mode: depth of field (photo2, 2026-11-12; remaster-features §5 "Photo mode")

Photo mode's own controls (src/b3replay/photomode.h `PhotoDof`), not a SETTINGS row; the choice lasts for the run
(photo mode entered again keeps it), nothing is saved.

| What | Keyboard | Pad | Default |
|---|---|---|---|
| Depth of field on / off | F | Back / Select | off |
| Blur strength (the aperture), levels 1-6 | V / B (smaller / larger) | Start (next, cycling) | 4 |
| Focus nearer / farther (held: 1.5 stops a second, faster after a second held, one stop x2 the distance; up to 4 stops each way) | N / M | D-pad left / right | 0 |
| Focus on the player's car / on the screen centre | G | L3 alone | the car |

The help box's first line shows it: `DOF OFF`, `DOF 4   FOCUS CAR`, `DOF 4   FOCUS CENTRE X1.41`.

- **Focus.** Always the GPU's autofocus: the median view distance of five pixels around a point, x 2^stops. On the car:
  the point is the player's race car's middle as the frame shows it (its body matrix's position projected with the
  frame's view-projection; from the frame the renderer draws, no guest read), so the focus is on the car's nearest
  visible panels (its centre's distance would be ~2 m behind the visible tail on a chase view). Behind the camera or
  off screen: the screen centre. On the centre: the screen centre.
- **The blur** (b3gfx `FrameParams::Dof`, `src/b3gfx/shaders/dof.frag`): a depth pre-pass of the frame's Z-writing
  draws (the same draws as the sun's occlusion pass, cars through their own shaders with their deformation) into a
  single-sample, sampled depth texture (the scene's multisampled depth cannot be sampled); then at half size a prep
  pass (colour, the nearest view distance of each 2 x 2) and a gather: a golden-angle spiral over the largest circle,
  each tap counted where its own circle of confusion (R x clamp((z - F) / z, -1, 1), R = the level's radius: 0.3 %,
  0.5 %, 0.8 %, 1.2 %, 1.7 %, 2.4 % of the output height) reaches the pixel; taps behind a sharper pixel are limited to
  twice its circle (no background bleeding over an in-focus subject), taps in front are not (a blurred foreground
  spreads over what is behind it). The result is blended over the frame where the pixel's own circle is over about
  a pixel, or a blurred foreground covers it, after the zoom blur and before the 2D stage. At most ~256 taps a pixel.
- **Off, or photo mode closed**: `Dof::on` is false and no pass of it runs (no pre-pass, the scene pass's store as
  before): the frame is the same as without the feature. The shader and its pipelines are made at the first use; if
  the device cannot make them (or has no sampled depth format), the effect stays off and the log says so once.
- A screenshot (F12) draws the same frame parameters: it includes the blur. Offscreen (race_check), the depth of field's
  keys are stamped in game time like the camera's (photomode.h "Game time"), so a shot of poll N shows the state at N.
- Diagnostics: `b3replay <capture> --offscreen out.png --poll P --dof R[,F]` draws a recorded frame with it (R the
  radius as a fraction of the height, F a focus distance in m; without F the autofocus on the player's car);
  `B3GFX_DOF_DEBUG=1` draws the blur circles instead (red in front of the focus, green behind, black in focus).
- Vulkan and Metal: one GLSL source, SPIR-V and MSL generated (tools/render/build_shaders.sh, spirv-cross); the MSL
  is compile-checked only by spirv-cross here (no Metal compiler on Linux): verify on the Mac.

Proofs (2026-11-12, Linux / Vulkan, STRICT=1):
- `SCENARIO=photodof tools/native/race_check.sh` with `PHOTO_GOLDEN` = a `SCENARIO=photo` run of the same build: PASS
  -- shot 14050 drawn with the depth of field (focus on the car, level 4) differs from 13820 (photo mode, not on yet);
  14080 (off again) = 13820 = the photo scenario's 14050; the shots before and after (13592, 14492, 14842, 15292)
  equal the photo scenario's. `SCENARIO=photo` PASS.
- Defaults: the golden compare (tools/golden/compare.sh, every scenario) passes.
- `b3replay data/scene/race2.b3scene --poll 3000 --size 1280x720 --dof 0.017`: the car sharp, the street behind it
  soft; with `B3GFX_DOF_DEBUG=1` the car black (in focus), the road in front red, the street green.
- test_photomode: the parameters (levels, the aim on the car through a projection, off screen / behind: the centre,
  no projection: the distance, the stops), the keys and the pad (Back, Start, L3 alone, the D-pad held), off when photo
  mode is closed, the setting kept for the next entry, the game-time replay of F / M at their polls, the help lines.

### 7.7 Launcher: SHOWROOM (photo2, 2026-11-12; remaster-features §5 "Showroom / garage")

A launcher page between STATS and CONTROLLERS (src/b3launcher/showroom.h, showroom_view.h): every player car on the
user's disc in 3D, turning on a turntable, named as the game names it.

- **Cars**: PVEH/<COMP, MSCL, CUPE, SPRT, SUPR>/CAR1-10.BGV, HSPC/CAR20-25.BGV, HEVY/CAR36.BGV and CAR1-10.BGV
  (67 on the US disc; the traffic files CARn.BTV are not player cars). Their names: the game's string table
  DATA/GLOBALUS.BIN (4006 UTF-16 strings, an offset table), the block of 67 names starting with "Compact Type 1"
  (title case), in the game's order; the files map to it by class and number, the specials and the fire truck as
  showroom.h says (checked against the drawn cars). Without the table: "<class> <n>".
- **Drawing**: the renderer's car model (src/b3view/carscene.h, as b3view draws a car: the car file's meshes at LOD 3,
  its paint palettes, GLOBAL.TXD's glass and underside, the env map of US/C3_V1's sky, b3view's captured light) on a
  floor disc with a soft shadow, by a b3gfx device without a window (the launcher draws with SDL_Renderer): each frame
  offscreen at the view's pixel size, read back and shown as a picture. The device is made at the first visit; without
  one the page says why.
- **HD textures** (showhd1, 2026-11-14): the car is drawn with the HD textures the game would use: the folders
  `b3asset::hd_folders` gives the game's renderer (the enabled mods' `[textures]` packs in load order, then the player's
  own pack unless HD TEXTURES is off), opened by the view's own `b3asset::HdTextures` and set as the decode hook while
  the car, GLOBAL.TXD and the sky load, so the lookup is the race renderer's (`b3view::load_car_model` ->
  `decode_raster_clut` -> `HdTextures::apply` -> `hd_lookup`, the paint's CLUT in the key). The folders are indexed
  when the list changes or the HD Textures page scans the folder again, not per car; the decoded files are dropped
  after each load. Changing the switch, the folder or the mods reloads the car shown.
- **Controls**: drag the picture (mouse) or the right stick: turn the car and raise / lower the camera; the wheel over
  it: nearer / farther; left / right on the car row, LT / RT or PgUp / PgDn: the previous / next car; PAINT: the car's
  paint jobs; TURNING: the turntable's own spin on / off (a drag pauses it for 3 s).
- **Read only**: the files of the game data folder (or the disc image) only, nothing written; no save file is read, so
  every car shows whether the profile has unlocked it or not, and nothing is unlocked or changed in the game.

Proofs (2026-11-12, Linux / Vulkan): `b3launcher --screenshot <dir>` draws SHOWROOM with three cars (showroom,
showroom_super, showroom_heavy), and `B3LAUNCHER_SHOWROOM_CARS=<FOLDER>/<n>,...` one shot per car named. Looked at:
Compact Type 1, Assassin Sports, Dominator Super, Classic Hotrod (HSPC/CAR23), Fire Truck (HEVY/CAR36), Heavy Pickup
(HEVY/CAR1), City Bus (HEVY/CAR9): each drawn car fits its name, which fixes the specials' and the heavyweights' order.
showhd1 (2026-11-14): the screenshot run's "showroom HD check" makes a test pack from the disc in its own data folder
(Compact Type 1's texture under the keys the renderer looks up for paints 1 and 2, solid magenta / green DDS files)
and a mod with a `[textures]` pack (yellow for paint 1), draws the view with HD TEXTURES off, on, paint 2, the mod on
(with the switch on and off) and off again, and removes them: the US disc gives 69424 of 75255 car pixels changed by
HD, none off the car, the mod's colour over the player's, and the original back pixel for pixel with HD off
(showroom_hd / showroom_hd_off and showroom_hd_view_*.png; skipped without game data or a GPU). b3view
`--hd-pack` with the same two files draws paint 2 green as well (the game's renderer path).
test_showroom: the
string table (UTF-16, surrogates, a bad table), the names block (title case preferred, cut short), every file to one
name, a folder's list and order (traffic .BTV left out), the turntable; with B3_GAME_DIR the real disc: 67 cars, from
Compact Type 1 to Trash Truck.
### 7.8 Fast loading (fastload1, 2026-11-12; remaster-features "Fast loading")

DISPLAY tab, section WINDOW (after PAUSE WHEN UNFOCUSED): **FAST LOADING** Off / On (ini `fast_loading`, default On;
also on the launcher's Settings page). Code: `tools/native/fast_load.h` (the detector, the decision, the pacer, the
sound gate), `tools/native/native_play.cpp` (per VBlank), `src/b3replay/fastforward.h` + `clock.h` (`ff_newest` /
`ff_after`) + `live.cpp` (`display_x`) + `inproc.cpp` (`b3r_fast_loading`, `b3r_fast_forward`), and
`tools/native/gameview_native.cpp` (`set_unstamped`).

How it works:
- The native game runs on virtual time; native_play paces it to real time by sleeping until each virtual VBlank's
  wall-clock deadline. Nothing in the guest reads the wall clock, so pacing changes when a VBlank happens, never
  what happens in it.
- **Loading** is the game's own state: CB3MenuPageManager (D_01E90240) has CB3LoadingScreenPage (D_01E9583C) as
  its current page (+0x1F0 = 0x01E90430). The game makes the same test itself in CB3MenuPageManager::Update
  (0x30D7C0) and SetMenuPage (0x30D5C0); the page comes from the menu flow's loading state (func_003A2100 case 3,
  CB3LoadingScreenPage::Prepare 0x321780) and its Update (0x321690) runs every frame it is up. No timing heuristic.
  D_01E900B0 picks its look: 0 the boot's LOADING band with a bar, 2 the "HAVE YOU TRIED..." picture page into a race,
  1 / 3 the LOADING strip with the two car icons (after a race, between the parts of a crash event).
- With the row On and the run paced (a window; tests: an offscreen run without `--no-pace`), a load's VBlanks are
  not paced from its first quiet VBlank on (the mix's peak below -36 dBFS, at most 30 VBlanks into the load). The
  game reaches the end of the load as fast as the PC can run it; at the first VBlank after it, pacing starts again
  from that moment (no catch-up burst, so a race's first frames are at their normal speed).
- **Sound**: a load is silent except for the tail of what played before it (3-8 VBlanks of the menu music or the crash,
  measured on every load of crashresults with `B3NATIVE_AUDIO_RAW`). That tail plays at real time (the quiet rule
  above); the rest of the load is muted on purpose (the device would otherwise get the mix faster than it plays it,
  and its drift guard would cut it into pieces): one VBlank fades out, the load's frames are dropped, the first VBlank
  after fades in. Its frames are silent anyway.
- **Picture**: the load's ticks carry no latch stamp, and the renderer is told (`b3r_fast_forward`): it shows the
  newest tick at each frame, so the loading screen's animations (the bar, the LOADING fade, the car icons) run as fast
  as the load. After it, the newest tick again until two stamped ticks are there, and the display position never goes
  back behind what was shown (tests/test_clock.cpp `fast_load_newest_then_no_step_back`).
- Overrides: `native_play --fast-loading on|off` and `B3NATIVE_FAST_LOADING=0|1`. Every run logs each loading screen
  (`loading screen up (kind K)`, `loading screen gone after N VBlanks (S s at 59.94 Hz) in W s wall`) and a summary.

Not covered: the boot's movies and the title (they play at their own speed); a load without the loading page (none
seen in the race_check scenarios: the race scenario, crashresults, crashnav).

**Why it can be On by default**: it changes no VBlank of the game (proof below); pad recordings replay identically
with it on or off; the sound it mutes is silence; what the player sees is the same loading screen, shorter.

**Measurements** (Linux, this PC shared with other jobs, 2026-11-12). Every loading screen of the crashresults and
crashnav tours (race_check STRICT=1); "unpaced" = an offscreen check run (`--no-pace`, the offscreen renderer
beside it); "paced" / "FAST LOADING" = `fastload_check.sh`'s paced replays (FAST LOADING's column includes the paced
sound tail, 3-8 VBlanks):

| Loading screen (kind) | VBlanks | at 59.94 Hz | unpaced | paced, FAST LOADING off | paced, FAST LOADING on |
|---|---|---|---|---|---|
| boot, before the intro movies: fe.awd (0) | 163 | 2.72 s | 0.27 s (604 VBlanks/s) | 2.69 s | 0.37 s |
| menus -> race, Single Event (2) | 363 | 6.06 s | 0.96 s (378/s) | 6.04 s | 1.60 s |
| menus -> race, World Tour (2) | 378 | 6.31 s | 1.19 s (318/s) | | |
| crash event: junction intro -> the crash (1) | 136 | 2.27 s | 0.20 s (680/s) | 2.28 s | 0.28 s |
| crash event: the crash -> CRASH RESULTS (3) | 274 | 4.57 s | 0.22 s (1245/s) | 4.57 s | 0.45 s |
| results -> menus, crash event (1) | 166 | 2.77 s | 0.28 s (593/s) | 2.76 s | 0.45 s |
| World Tour race -> its menus (1) | 332 | 5.54 s | 0.44 s (755/s) | | |
| World Tour menus -> the next event (2) | 327 | 5.46 s | 0.88 s (372/s) | | |

The into-race loads are the slowest unpaced (the track's data: the renderer loads the route meanwhile too). The boot
to the title is otherwise movies (englis30 at VBlank 383, cri_rw30 at 647, Titles30 at 1191, the title after it):
they play at their own speed (remaster-features §2: every video plays unless skipped).

**Proof** (`tools/native/fastload_check.sh build/native_play build/fl_check`, SCENARIO=crashresults, 33,000 VBlanks;
the recording made by race_check, then replayed with `--replay-pad` three ways):
- unpaced (`--no-pace`), paced with `--fast-loading off`, paced with `--fast-loading on`: rc 0 each;
- equal in all three: the DMA log (33,000 lines), the SIF log (72,174), the RW Audio log (71,454), guest RAM's
  per-MB FNV hashes at 330 grid VBlanks, the 7 screenshots, the pad recording (39 lines), and every loading screen's
  first and last VBlank;
- wall time 551.4 s paced, 535.9 s with FAST LOADING: the 5 loading screens 18.34 s -> 3.14 s (1,091 of their 1,102
  VBlanks unpaced, 0 resyncs after them).
- The golden (`JOBS=1 RATE_RUNS=0 tools/golden/baseline.sh`, `compare.sh` against rec_det, all 20
  scenarios; those runs are unpaced, so they show that the detector and the new code leave every check run as it
  was): trace, DMA, SIF, RW Audio, RAM and shots PASS everywhere except one shot, `online` live_10400_0.png: the PC
  SETTINGS page itself, which now lists FAST LOADING (the same build with the row left out gives the golden's hash).
- `make test`: tests/test_fast_load.cpp (the detector, spans and edges, the sound tail and its cap, the pacer on a
  fake clock: real-time spacing, the saving, no burst after a load also when the load runs slower than real time,
  --pace-from and resyncs unchanged, the sound gate's fades and drops), tests/test_clock.cpp
  `fast_load_newest_then_no_step_back` (the display: the newest tick in a load, never backwards after it, the schedule
  clock and the 60 Hz rate back; at 240 / 144 / 60 Hz displays and a late flag), tests/test_settings.cpp
  `fast_loading_row`.

**The race's picture at a load's end** (loadlag1, 2026-11-13). A player report (Windows, a window, FAST LOADING on):
"the loading screen is still there but you can hear the race starting underneath it".
- **Why** [C measured: the race_check Single Event load into US/C2_V2, `--pace-from 19700`, the offscreen bench
  drawing every display frame as the window does, `--latency low --latency-log`]: the renderer builds the race's
  route when the game reads its STATIC.DAT header (native-renderer §28.3), 166 ticks before the race's first tick. At
  real time those ticks take 2.8 s and the build (0.4-0.7 s) is done long before the race. With FAST LOADING they
  pass in 0.2-0.4 s, so the build ran into the race: it ran on the render thread, the window kept the loading
  screen's last frame, and after it the render thread still had to decode the ~180 ticks it had not taken meanwhile.
  The game (and its sound) was already in the race. Before the fix, the first race frame came
  - Linux: 699 ms after the game handed the race's first tick over (fast off: 288 ms; the first race frame itself
    costs ~280 ms on this PC's WSL Vulkan, in both cases); the window drew no frame for 757 ms around the race start;
  - Windows (the same PC, the offscreen bench of a Windows build): 376 ms after it, the window drawing nothing for
    327 ms before that, and the first race frame 10 ticks behind the game. Where the time went (a run with timers,
    the PC busier): the build 385 ms on the render thread, then 177 records decoded in 126 ms, then the first race
    frame 150 ms.
  It was not the display clock or the latency mode: after the load the clock shows the newest tick at once
  (fastforward.h, clock.h `ff_newest`), and the first race frame showed a tick 10-19 ticks behind the game only
  because the render thread had been blocked.
- **Fix** (presentation only):
  - The route is read and decoded on a worker (`View::prepare_track`: STATIC / STREAMED, both models, ENVIRO.DAT,
    GLOBAL.TXD once with its blur mask and car slots, GAMEDATA.BGD, the lens-flare table) while the window goes on
    drawing; only the GPU half (`View::load_prepared`) runs on the render thread (`LiveScene::preload`,
    `async_routes`: the window, and the offscreen bench without shots). Offscreen shots and an HD pack keep the
    whole load on the render thread as before.
  - At a fast load's end (its first VBlank that is not loading), before the race's first VBlank, native_play waits
    until the renderer has taken every record and built the race's route (`b3r_scene_wait`, fastforward.h
    `scene_busy` / `scene_epoch`), at most the rest of the load's real-time length and at most 3 s
    (`tools/native/fast_load.h` `scene_hold_ms`). The load's sound is muted up to that VBlank anyway. This is wall
    time only, like the pacer's own sleeps: the game's VBlanks and their state are the same. A load is never slower
    than with FAST LOADING off. The log says `fast loading: N ms waiting for the renderer's race scene`.
  - While a race's route is still being built, the window keeps the last tick before the race (the loading screen)
    instead of the race's 2D on black (`LiveScene::hold_before_race`).
  - The race-start log line also gives the time from the game's hand-over of the race's first tick
    (`first race frame shows poll ... (N ms after the game handed it over)`): the delay as heard.
- **After** (the same runs):
  - Linux, FAST LOADING on: 325 ms of waiting, the first race frame 430 ms after the hand-over (fast off with the
    fix: 389 ms; this PC was compiling at the same time). Both are the first race frame's own cost, which fast off
    has too. The load: 1.16 s -> 1.60 s wall (against 6.22 s at real time).
  - Windows, the four runs one after another (`--pace-from 19700`; "lag": the first race frame after the game handed
    over the race's first tick, as heard; "behind": the ticks between the picture and the game in the race's first
    2 s, at most):

    | Windows, Single Event into US/C2_V2 | load (wall) | waiting | lag | first race frame shows | behind |
    |---|---|---|---|---|---|
    | before, FAST LOADING on | 0.48 s | - | 376 ms | poll 10702 (10 late) | 175 |
    | after, FAST LOADING on | 0.49 s | 162 ms | 114 ms | poll 10692 (the first) | 6 |
    | before, FAST LOADING off | 6.23 s | - | 116 ms | poll 10692 | 6 |
    | after, FAST LOADING off | 6.23 s | - | 124 ms | poll 10692 | 7 |

    With the fix the race starts as with FAST LOADING off; the ~115 ms left is the first race frame itself. FAST
    LOADING off also keeps the loading screen moving while the route is decoded (the window stopped for 286 ms there
    before, 97 ms now).
- Proofs: the golden (`JOBS=1 RATE_RUNS=0 tools/golden/baseline.sh`, `compare.sh` against rec_det): 140 PASS, only
  "rate: FAIL no rates" (those runs are unpaced and their offscreen shots keep the load on the render thread);
  `make test` with tests/test_fast_load.cpp `scene_hold_within_the_real_time_load`; `SCENARIO=race
  tools/native/fastload_check.sh`: PASS (DMA, SIF, RW Audio, RAM, shots and pads equal unpaced, paced, and paced with
  FAST LOADING and its wait: the wait is wall time only).
- Not done: the first race frame's own cost (the race's cars loaded on first sight, the race's GPU pipelines made on
  their first draw: ~115 ms on Windows here). It is the same with FAST LOADING off.

### 7.9 Controls: remapping (remap1, 2026-11-12; remaster-features §3 "Custom mapping")

INTERFACE tab, the last section, **CONTROLS** (only in the game: native_play's page), and the launcher's Controllers
page. Code: `src/native/input_bindings.{h,cpp}` (the model, controls.ini, conflicts, the rebind flow; no SDL),
`src/native/input_keys_sdl.h` (`gamepad_state`, `pad_input_of_button`), `src/b3replay/nativepad.{h,cpp}` (the bindings
applied, the capture), `src/b3replay/pcpage.cpp` (the section), `src/b3replay/prompts.{h,cpp}` (the glyphs),
`src/b3launcher/main.cpp` (`controls_editor`). Tests: `tests/test_input_bindings.cpp`, `test_prompts.cpp`
`remapped_pad_glyphs`, `test_settings.cpp` `controls_section`; padr2 (the stick clicks): `test_input_bindings.cpp`
`default_stick_clicks_are_l2_r2` and `ini_version1_stick_clicks`, `test_prompts.cpp` `default_pad_stick_click_glyphs`
(15 / 13 tests, 10,055 / 1,377 checks), and the golden with the new defaults (keyboard-driven: all 140 items PASS).

What it changes: only which physical input produces which PS2 button. The game reads the same 32-byte DualShock 2
reports with the same value ranges; the bindings act in front of the pad record, so pad recordings and replay files
keep the reports the game read, and a replay plays the same whatever the bindings are later (native_play's replay
paths never read the window's pad). With no controls.ini (or one that only repeats the defaults) every report is
byte for byte the built-in one (`gamepad_sample`, `default_key_map()`).

**The model.** Per player (1, 2) and device (keyboard, gamepad), each of the 24 actions -- Cross, Circle, Square,
Triangle, L1, R1, L2, R2, Start, Select, the D-pad's four directions, the left and right sticks' half axes (left,
right, up, down), L3, R3 -- has a list of physical inputs:
- keyboard: keys (SDL scancodes), each for the driving context, the menus' or both (as the default layout: W
  accelerates while driving and moves up in a menu; `input_keys.h` KeyCtx);
- gamepad: SDL's standard layout: the 21 buttons (South ... Touchpad, paddles), the sticks' half axes (`left_x-`,
  `left_x+`, ...) and the two triggers.

How an input drives an action:

| Input | on a button | on a stick half axis |
|---|---|---|
| key, gamepad button | digital bit + full pressure | full deflection (the keyboard's left stick X and D-pad left / right keep the steering ramp, docs/mac.md §5) |
| trigger | pressure = its travel (also below the threshold), the bit from the trigger threshold (25 %) | its travel |
| stick half axis | pressure = its travel past the dead zone (24 %, rescaled), the bit from the threshold | its travel past the dead zone, rescaled (as the default left stick) |

Several inputs on one action: pressed if any is, the largest pressure or deflection. Nothing is rescaled beyond what
the default layout already does (remaster-features §3: analog ranges stay PS2-identical).

**Defaults** = the built-in mapping: player 1's keyboard is `default_key_map()` (docs/mac.md §5); every gamepad
is `gamepad_sample`'s layout (South Cross and the right trigger as analog Cross, West Square and the left trigger
as analog Square, East Circle, North Triangle, the shoulders L1 / R1, Start, Back = Select, the D-pad, the left
stick, and (padr2, 2026-11-13) the right stick click R2 (the Crashbreaker) and the left stick click L2 (the music
track skip), with full pressure while held; L3, R3 and the right stick's axes have no input); player 2's keyboard has
no keys. Before padr2 a pad had no way to reach L2 / R2: the triggers are the pedals.

**Players.** Player 1: the keyboard and player 1's pads (the first pad connected, and pads past the second: docs/mac.md
"Two controllers"). Player 2: the second pad. The keyboard is shared: keys bound for player 2 drive port 2, which is
then plugged in (also without a second pad); a key bound for both players drives both.

**controls.ini** (the user data folder, beside pc_settings.ini; `--controls <file>` for native_play's renderer, which
offscreen runs never read otherwise):

    version = 2
    [player1.keyboard]
    cross = Space@menus, Return, Keypad Enter, K, Up@driving, W@driving
    [player1.gamepad]
    cross = east
    circle = south
    [player2.keyboard]
    cross = I

One line per action that differs from its default; `none` (or nothing) unbinds it. Keys by name (`Return`, `Left
Shift`, `Comma`, `Keypad 4`; `sc<N>` for a scancode without a name), `@menus` / `@driving` for one context. Unknown
sections, actions, keys, contexts and gamepad inputs are reported (the run log's `[b3controls]` lines, the launcher's
note) and skipped; a line with no valid input keeps the action's default; a newer `version` is read as far as
understood. Version 2 (padr2) made the stick clicks L2 / R2's defaults: in a version 1 file (or one without a version
line) that binds a stick click to another action and leaves L2 / R2 alone, that click stays the other action's only,
as it was when the file was written; L2 / R2 keep their other defaults. A version 2 file is read as written.

**The page.** CONTROLS: PLAYER (1 / 2), DEVICE (Keyboard / Controller), KEYS FOR (Driving / Menus; the keyboard only),
then a row per action with what is bound to it in that view (a controller's inputs named for the pad last used: Cross
/ A / B ...), RESET TO DEFAULTS (this player's device) and LAST CHANGE. On an action row:
- CONFIRM: rebind -- a popup asks for the input (a key; or a controller's button, or a stick / trigger pushed past half
  way from rest) and counts down 6 s; Escape (or a right click; for a key also a controller's B / Circle or Back)
  cancels. The input replaces what the action had in that view (a keyboard binding of both contexts keeps the other
  one). Escape, F5 and F9-F12 and Ctrl / Cmd combinations are the window's and are not taken.
- RIGHT: add one more input; LEFT: remove the last one.
- A conflict (the input drives another action in an overlapping context; the keyboard: of either player) shows what
  else uses it and offers SWAP (they get this action's previous first input), CLEAR (they lose it) or KEEP BOTH
  (LEFT / RIGHT, CONFIRM; BACK cancels and changes nothing).

Each change is applied at once (the next VBlank's report) and saved to controls.ini. The launcher's Controllers page
has the same editor (Enter / click rebinds, right adds, left removes the last; the conflict as a dialog) on the same
file; the game reads it at its start.

**Prompts.** The keyboard's key caps follow the keys (as before: the bound key in the current context). A gamepad's
prompts show the position bound: the first input of a button that has a face / shoulder / trigger / stick-click /
Start / Back position is drawn as the button found there by default (Cross bound to East shows B on an Xbox pad, the
Circle symbol on a PlayStation pad). The PlayStation look stays the game's own pictures for every button not moved.
With the defaults L2 / R2 are moved (to the stick clicks), so their pictures show L3 / R3 (PlayStation, PS4 / PS5,
generic: "L3" / "R3"; Xbox and Nintendo: "LS" / "RS"), in the shoulder button's pill: the HUD's Crashbreaker prompt
(the "R2Button" picture beside CRASHBREAKER in Crash events, CB3HUDCrashStyle 0x3A3250) reads R3 / RS. The HUD's
shoulder pictures ("R2Button", "R1Button" Impact Time, "L2Button") are redrawn like the front end's (padr2: matched
by raster name in any case, the whole 64 x 32 picture in one quad).

**Proof** (2026-11-12, offscreen):
- `make test`: tests/test_input_bindings.cpp (13 tests, 9,985 checks): every scancode alone in both contexts and
  20,000 random key sequences give the same reports as `default_key_map()`; 200,000 random gamepad states (and tunings)
  the same as `gamepad_sample`; rebinding per view, add / remove, conflicts (swap, clear, keep; the keyboard across
  players), the analog <-> digital conversions, controls.ini's round trip and invalid entries, the rebind flow
  (timeout, cancel, reserved keys, conflict choice); test_prompts `remapped_pad_glyphs`, test_settings
  `controls_section`.
- The golden with the defaults (`baseline.sh`, `compare.sh` against rec_det, 20 scenarios): all 140 items PASS (the
  rate: none recorded). The `online` tour's PC SETTINGS shot is unchanged (CONTROLS is the INTERFACE tab's last
  section).
- A test controls.ini moving Cross from Z to K (`--controls`), the race scenario's keys with K for Z: race_check STRICT=1
  PASS, the same two race shots as the default run, the pad recording's reports equal to the default run's (all but the
  first neutral line's VBlank, when the window's pad came up: 455 / 460). The same keys without it: the race is not
  reached (FAIL, as it must). That run's replay file played with other bindings (Cross unbound, the gamepad's Cross /
  Circle swapped): 39 of 39 checkpoints matched, the same shots and reports: replays do not depend on the bindings.
- In the game's page (SCENARIO=pausesettings + keys): INTERFACE > CONTROLS > CROSS, Enter, K: `[b3controls] PLAYER 1
  CROSS: K`, the popup and the row in shots; after resuming, K held drives the race (Cross at full pressure in the pad
  recording, 81 mph in the shot). Scripted page keys need taps away from offscreen shots (the renderer reads the page's
  input between them).
- Launcher `--screenshot`: controls_keyboard / controls_gamepad / controls_conflict.png. Windows build (RENDER=1) rc 0.

### 7.10 The tuning console (tune4, 2026-11-12; mod kit part 4, docs/modding/tuning.md)

INTERFACE tab, section DEVELOPER (after the tab's own rows, so they are where they were; in the game's page before
remap1's CONTROLS, §7.9): **DEVELOPER MODS**
Off / On (ini `developer_mods` 0-1, default 0; also on the launcher's Settings page, and the Mods page's "Developer mods"
button sets the same value). One switch for the mod kit's developer features: the tuning console, and code mods' hooks
on the platform tier (sdk2b, `[code] platform = yes`); `native_play --developer-mods` turns it on for one run. Read at
the start of a window run
by native_play itself (the renderer's settings file: `--settings` in its arguments, else `<prefs>/pc_settings.ini`),
since the console has to be installed before the game's first instruction. Offscreen runs never take it
(`native_play --tuning on|off` decides there). Code: `tools/native/tuning_console.h` (the registry, the writes, the
recording), `src/b3replay/tuneconsole.{h,cpp}` (the tab and the F9 overlay), `include/b3tune/` (the model and the
two sides' C interface, `b3r_set_tune_hooks`).

- **Off** (default): nothing installed; the game is not even read. The golden check is unchanged.
- **On**: native_play wraps the game's tuning registration (CGtCommsDatabase::AddEntry) on the hook table's port
  layer and records every tuning variable as it registers. The page gets a **DEVELOPER** tab (the last tab; the menu
  model's tab bar shrinks its labels to fit): SAVE AS PRESET (this session's changes as a new mod in the mods folder),
  LIVE OVERLAY (a status line), then the variables registered now, by group, one row per element with its value. Left /
  Right step a value (a hundredth of its range, else 1 % of its decade; never outside its range), Cross puts back its
  default (the game's value, after the mods' presets); a changed value is marked `*` with its default, the CHANGED tab
  lists them all and RESET ALL puts them back (after a second Cross; docs/modding/tuning.md "What you changed"). **F9** in the window opens a live overlay
  with the same variables (Up / Down, Left / Right, Page Up / Down by group, Home: back; the game sees a neutral pad
  while it is open, as for this page).
- **Gameplay-changing, recorded**: an edit is written at the next VBlank on the game's thread and goes into the pad
  recording (`tune <vblank> 0x<key>[<element>] 0x<value>`) and the replay file (a SETS `tune` entry), so replays play it
  again exactly. While a recording plays, edits are off.
- Not on the page: tuning presets come with mods (`[tuning] preset`, docs/modding/tuning.md), which the launcher's Mods
  page marks "Changes gameplay".

### 7.11 Split-screen layout (splitlayout1, 2026-11-13; remaster-features "Split-screen layout choice")

CAMERA tab, after FOV CORRECTION (with the views and their projections; a DISPLAY row would change that tab's scroll bar in the golden record's PC SETTINGS shot): **SPLIT SCREEN** Top / bottom / Side by side (ini
`split_layout` 0-1, default 0; row id 180; also on the launcher's Settings page). Drawn only: the game's simulation,
its cameras, the pad reports and the timeline are the same whatever the value (the renderer reads the captured
cameras and places the views; nothing is written to the game). Live: the next frame of a split-screen race.
Code: `src/b3replay/splitlayout.{h,cpp}` (the maths), `View::split_frame` / `View::split_hud` (src/b3replay/view.cpp);
renderer details in docs/native-renderer.md §29.7.

**What the game does** [C decomp; the sources in splitlayout.h]: CB3GraphicsManagerBase::SetViewportsSplitScreen
0x19E760 has two layouts, chosen by the graphics manager's byte +0xF: top / bottom (viewport 1 the upper half,
viewport 2 the lower; aspect 8/3, view distance scale 1.35) and side by side (left / right halves; aspect 2/3, scale
0.55); with the console's 16:9 TV setting (+0xD) 32/9 / 1.62 and 8/9 / 0.75. The view window is half_w = scale x
tan(fov / 2), half_h = half_w / aspect (CGtViewport::SetFOV 0x211CC0 / Update 0x211ED0), and the aspect is exactly a
half of the 4:3 (16:9) TV picture: the PS2 views are **not squashed**. +0xF is only ever written 0 (Construct 0x19EE40),
so the retail game always plays top / bottom; the side-by-side branch is in the code with its own HUD rects
(CB3HUDManager 0x1A0660: [30, 290] / [350, 610] wide, full height, against [30, 610] x [22, 218] / [262, 458]) and
narrower takedown banners (0x3A25F0, 0x3D56D0), unused.

- **Top / bottom** (default): the game's layout, drawn as before splitlayout1 (byte for byte: the renderer's
  path is unchanged). In a window each half keeps the game's vertical view window and widens horizontally with the
  window (Hor+, as one player's view does), so a half is never stretched; the HUD of each player is laid out with the
  wide HUD rules inside its half (§29.2 of the renderer doc).
- **Side by side**: player 1 on the left, player 2 on the right. Each view is projected at its own shape (half the
  window's width, the full height) from the game's own side-by-side view window for the same camera: the vertical
  half extent 0.55 t / (2/3) = 0.825 t (16:9 TV flag: 0.84375 t) with t = tan(fov / 2) recovered from the captured
  top / bottom camera, and the horizontal Hor+ from the view's shape (a 75-degree game camera: about 65 degrees
  vertical, 59 horizontal at 16:9, 74 at 2560 x 1080). The FIELD OF VIEW setting applies as in one view. Each player's HUD
  (the game's HUD band of that player: rows 0-223 / 224-447 of the 2D) is placed in its view: every HUD component at
  its EHUDAlign point of the view (POS top-left, LAP top-right, the boost bar bottom-left, the speedometer
  bottom-right, messages centred), the game's edge pieces against the view's sides, at the game's HUD size or smaller
  so that the band's full width fits the view (1.5 output pixels per 2D unit at 1920 x 1080, 2.0 at 2560 x 1080), and
  clipped to the view. The rival markers are projected through their own view's camera each frame. A pause menu (or
  any overlay of the front end's panels) belongs to both and is laid out over the whole window as in the top / bottom
  layout. The views use the renderer's own culling (the game's visibility results are for its top / bottom frustums).

Checks (Linux, offscreen): SCENARIO=multiplayer STRICT=1 at 1920 x 1080
and 2560 x 1080 with each value; top / bottom shots byte-identical to c90038b9's (15 / 15 at each size), one-player
race shots identical; golden (baseline.sh + compare.sh against rec_det) 140 PASS, rate not recorded; tests/test_split_layout.cpp (the decomp's constants, the views' shapes, the HUD bands inside
their views at 16:9 and 21:9, the clip-space move, the setting's ini / row / edit).

### 7.12 Adaptive triggers and gyro steering (pad1, 2026-11-13; remaster-features §5 "Modern controller features")

Two controller features, both off by default. Code: `src/native/pad_triggers.h` (the DS5 effect block, the effects
over a race; pure), `src/native/pad_gyro.h` (tilt / turn to steering; pure), `src/native/input_bindings.{h,cpp}`
(`GyroControls` per player, `apply_gyro`, the steering mixed into the report, controls.ini's `[playerN.gyro]`,
`trigger_role`), `src/native/input_keys_sdl.h` (`feed_gyro_event`), `src/b3replay/nativepad.cpp` (the sensors, the
effects sent), `src/b3replay/live.cpp` (the game's state for the triggers), `src/b3replay/pcpage.cpp` (the CONTROLS
gyro rows), `src/b3launcher/main.cpp` (the Controllers page). Tests: `tests/test_pad_motion.cpp`,
`test_input_bindings.cpp` (`default_gamepad_byte_identical` with gyro values, `ini_gyro`), `test_settings.cpp`
(`adaptive_trigger_rows`, `gyro_controls_rows`).

**Adaptive triggers** (presentation only). INTERFACE tab, section CONTROLLER, after TRIGGER RUMBLE (also on the
launcher's Settings page):

| Row | Values (ini key) | Default | Row id |
|---|---|---|---|
| ADAPTIVE TRIGGERS | Off / On (`adaptive_triggers` 0-1) | Off | 190 |
| TRIGGER STRENGTH | 10-100 % (`adaptive_triggers_strength`) | 100 | 191 |

On, a DualSense (SDL_GetGamepadType PS5, the Edge too) gets trigger effects through `SDL_SendGamepadEffect`, from
what the window already reads of the game -- the newest tick of the live feed (the capture the HUD and post effects
are drawn from): a race on screen (`race_tick`), the view's boost radial blur amount (graphics manager +0x1D0 / +0x270
block +0x1C, lerp(0, 1, boost)), the view's race car crashed flag (+0x18EA) -- and the window's own pad context
(driving vs menus / paused, b3r_pad_context). Player 1's pads follow view 1, player 2's view 2 in a split-screen race.
Nothing is written to the game and the reports are unchanged (the triggers' travel is read as before).
- Driving: the trigger bound to the brake (Square; the left by default) resists from about 15 % of its pull (simple
  resistance, start 40, force 180); the accelerator's (Cross; the right) a light spring (force 50), which turns into a
  buzz while boosting (simple vibration, 30 Hz, amplitude 60-200 with the boost). A trigger bound to neither is left free.
- A crash (the crashed flag rising while driving): both kick (12 Hz, amplitude 255) for 0.3 s, then are free until the
  car is back.
- Free (the effect "off"): in menus, paused, with the PC page or photo mode open, after the window closes (the pads
  are put back as they were: `release_pads`), and when the row is Off. A pad never given an effect is never sent one
  (Off and never on: no SDL call at all).
- TRIGGER STRENGTH scales every force and amplitude.

The effect block is SDL's `DS5EffectsState_t` (47 bytes; the source and the byte layout are in pad_triggers.h: +0
enable bits 0x04 right / 0x08 left trigger effect, +10 the right trigger's 11 bytes, +21 the left's) with the
controller's simple modes as SDL's testcontroller uses them: 0x05 off, 0x01 resistance (start, force), 0x06 vibration
(frequency, amplitude, start). The SDL we ship has only the public headers; the layout is SDL 3's own struct. The
values are first guesses, to be tuned with a pad. `B3NATIVE_TRIGGER_LOG=1` logs every effect sent (`[b3pad] adaptive
triggers: <pad> (player N): left 01 40 180 0, right 06 30 130 0`); the first one per pad is logged always.

**Gyro steering** (OPT-IN, input-changing, per player). INTERFACE tab, section CONTROLS (native_play's page), DEVICE
Controller: five rows after the action rows (ids 305-309); the launcher's Controllers page has the same five under
the editor's action rows, with a live readout of the selected pad (its sensors are turned on only while the page
shows it with that player's gyro on). Saved in controls.ini, `[player1.gyro]` / `[player2.gyro]`, only what differs
from the defaults; "RESET TO DEFAULTS" of the controller turns it off too.

| Row | Values (controls.ini) | Default |
|---|---|---|
| GYRO STEERING | Off / Mix / Replace (`mode = off / mix / replace`) | Off |
| GYRO AXIS | Tilt / Turn (`axis = roll / yaw`) | Tilt |
| GYRO SENSITIVITY | 25-300 % in steps of 25 (`sensitivity`) | 100 |
| GYRO DEAD ZONE | 0-10 degrees (`dead_zone`) | 2 |
| GYRO RECENTRE | None, Touchpad, Back / Select, L3, R3, Misc (mic / share / capture), paddle L1, paddle R1 (`recentre = none / touchpad / back / left_stick / right_stick / misc1 / left_paddle1 / right_paddle1`) | Touchpad |

On a pad with a gyroscope (SDL_GamepadHasSensor: DualSense, DualShock 4, Switch Pro, the Steam Deck through SDL)
the gyro and the accelerometer are turned on (`SDL_SetGamepadSensorEnabled`) and their events
(`SDL_EVENT_GAMEPAD_SENSOR_UPDATE`, paced by the sensor's own timestamps) give an angle:
- Tilt: the pad's sideways tilt against gravity, atan2(a_x, hypot(a_y, a_z)) (the same held upright or flat in the
  lap), smoothed with the gyro by a complementary filter (0.25 s): no drift. Level is straight; RECENTRE makes the
  present tilt straight.
- Turn: the gyro's rate about gravity, integrated; zeroed when driving starts and by RECENTRE.
The angle past the dead zone, over full lock (40 degrees of tilt, 30 of turn, at 100 %; 200 % halves them), is a
steering value -1..1 (left side down / turned left: left). **While driving only** (the window's driving context),
Mix adds it to the bindings' steering (the actions STEER LEFT / RIGHT: the left stick by default; clamped), Replace
uses it instead; the D-pad and everything else are unchanged. The result is the report's left stick X (byte 4) --
the report the game reads and pad recordings / replay files keep, so a replay plays the steering back exactly.
**Off** (the default), in menus, paused, or with no sensor data: the GamepadState is left as it is and the report is
the one before, byte for byte (`test_input_bindings` `default_gamepad_byte_identical`: 200,000 random pad states with
any gyro values while unused, and again with a player's gyro turned on in the controls; `test_pad_motion`
`gyro_off_or_not_driving_leaves_the_report`). The recentre button still drives what it is bound to (the touchpad
drives nothing by default). `B3NATIVE_GYRO_LOG=1` logs the angle and steering twice a second; the sensors' start and
each recentre are logged always (`[b3pad] gyro steering on: <pad> (player N), with its accelerometer`).

Checks (Linux, offscreen, no pad here: synthetic SDL events): test_pad_motion (9 tests: the tilt upright and flat, the
dead zone and sensitivity over their ranges, the integration by timestamps, the filter's drift with a biased gyro,
recentre, yaw zeroing, the stick values mixed / replaced and only byte 4 changing, off / not driving / no data equal
for 20,000 random states, the effect bytes, the effects over a race incl. a pause during a kick, the trigger roles
from the bindings; 97,998 checks), test_input_bindings (16 tests, 10,077 checks), test_settings (38 tests, 5,905
checks); `make test`: every binary passes. Golden with both off (`JOBS=1 RATE_RUNS=0 tools/golden/baseline.sh`, then
`compare.sh` against rec_det): all 140 items PASS (the PC SETTINGS shots unchanged: the new rows are on INTERFACE),
`rate` not recorded. Launcher `--screenshot`: controls_gyro.png (the five gyro choices under the controller's
bindings). Windows build (RENDER=1) rc 0. Still to check with real pads: the feel, the tuning values, unplugging mid-race, a second pad.

### 7.13 Car series on L1 / R1 (carsel1, 2026-11-13)

LB / RB (L1 / R1) change the car series on the car select, with small button glyphs beside the series arrows. Off by default; opt-in and **input-changing**. Code: `src/native/input_keys.{h,cpp}` (`ShoulderSeries`),
`tools/native/car_select.h` (the pages), `tools/native/native_play.cpp` (per VBlank), `src/b3replay/prompts.{h,cpp}`
(`series_glyphs`), `src/b3replay/view.cpp` (the glyph draws), `src/b3replay/nativepad.h` / `inproc.cpp`
(`b3r_series_shoulders`). Tests: `test_input_keys` (`shoulder_series`), `test_prompts`
(`series_glyphs_beside_the_arrows`), `test_settings` (`series_shoulders_row`); `race_check.sh SCENARIO=carseries`.

| Row | Values (ini key) | Default | Row id |
|---|---|---|---|
| CAR SERIES ON L1 / R1 | Off / On (`series_shoulders` 0-1) | Off | 200 |

INTERFACE tab, section CONTROLLER, after TRIGGER STRENGTH (also on the launcher's Settings page).

What the game does [C: the generated units, the ELF's vtables, its __sinit 0x4D7150; tools/native/car_select.h]:
- The car select is the CB3CarSelectPage object 0x01E94EF0 (vtable 0x4DE3F0, Update 0x318A80, Prepare 0x318F50);
  YOUR GARAGE (Driver Details) the page 0x01E95CB8 (vtable 0x4DFE40, Update 0x3F03D0). Both drive the carousel
  0x01E8EC30 (CB3CarouselComponent): **Left / Right change the series** (the car class: 0x42BD70 / 0x42BEA0, column
  +0x258, the car back to the first, with FEVerticalMove's sound and the slide), Up / Down the car (0x42C020 /
  0x42C090, row +0x244). The car select also reads Back (Triangle), Confirm (Cross) and Buddies (Square: the paint).
  Nothing else: the shoulders do nothing on either page.
- The menus' L1 / R1 [C gaeMenubuttons 0x4B9BC0, the DualShock controls 10 / 12]: PageUp / PageDown (read only by the
  online rankings 0x3778D0) and KeyboardShift / CapsLock (the on-screen keyboard 0x434550).
- The series' name (right-aligned at x 594, y 76) has an arrow each side, two images of the "CarArrow" texture
  (the carousel's Prepare 0x42D2B0, mode 9).

What the row does:
- **Input** (native_play, the game thread): while one of the two pages is the menu's current page (the word at
  0x01E90430), a press of L1 is given to the game as Left and R1 as Right (the D-pad's digital bit and full pressure;
  the shoulder's own bit and pressure cleared), from the press to its release, so the game's own series change runs
  (sound, slide, its auto-repeat while held). Only a press that begins on the page is translated; when the page goes
  away mid-press (a Cross into the race while holding a shoulder) the shoulder passes as it is from then on, so no
  Left / Right is ever held into a race. Port 2 (MULTIPLAYER's G2 SELECT CAR) the same with its own pad. The
  translated report is the one recorded (pad recordings, replay files): a replay plays the same series changes with
  the row off. The log names each translated press (`car series: port 1 R1 given to the game as Right`) and, while
  on, the carousel's series / car (`car series: series 2 car 0`).
- **Glyphs** (the renderer; drawn only): in front-end frames that draw the series' two arrows (the topmost pair of
  CarArrow quads on one row), the device's L1 glyph 4 units left of the left arrow and R1 4 units right of the right
  one, 15 units high (the shoulder cell, 35 x 15), white at the arrows' alpha, as items right after the arrows' draws
  (so the wide layout moves them with the arrows). The device and keys as every prompt: the PlayStation's L1 / R1,
  Xbox LB / RB, Nintendo L / R, the keyboard's bound keys (Q / R by default). Drawn whenever the row is on and the
  arrows are there (the car select, YOUR GARAGE).
- **Off** (the default): the reports are not touched (`ShoulderSeries::apply` returns them as they are, byte for byte,
  `test_input_keys shoulder_series`), nothing is drawn and the prompts' atlas is not built for it.

Why opt-in: a press of L1 / R1 on these pages would otherwise do nothing in the original; with the row on the game
gets Left / Right the player did not press on the D-pad. Harmless for the game's state (Left / Right are what the page
expects), but it changes the input stream, and the port's rule is that the defaults are the original exactly (as BOOST
Toggle and INSTANT RESTART). The golden runs never press the shoulders on the car select; with the row off their
carselect pages are unchanged (no glyphs).

Checks (Linux, offscreen): `race_check.sh SCENARIO=carseries STRICT=1` PASS -- R1 at VBlank 20900 and 21300, L1 at
21700 given to the game as Right, Right, Left; the carousel's series 0 (COMPACT) -> 1 (MUSCLE) -> 2 (COUPE) -> 1 -> 2
(the D-pad's Right at 22100), then the race; shots of the page with the PlayStation's L1 / R1 beside the arrows (and,
with `CARSERIES_INI='button_prompts = 2'` and `--size 1280x720`, Xbox LB / RB in the wide layout). Its pad recording
played back (`REPLAY_PAD=`, the row on: nothing translated again) gives the same series changes and the same 8 shots,
hash for hash. test_input_keys (28 tests), test_prompts (14), test_settings (39): pass. Golden with the row off
(`JOBS=1 RATE_RUNS=0 tools/golden/baseline.sh`, `compare.sh` against rec_det): 140 items PASS (carselect: 11 shots
equal), `rate` not recorded. Windows build (RENDER=1) rc 0. To check in a window: SETTINGS > INTERFACE > CONTROLLER >
CAR SERIES ON L1 / R1 On; SINGLE EVENT > RACE > the car select: LB / RB (L1 / R1, Q / R) move the series as Left /
Right do, with their glyphs beside the arrows, following the device used last; held, they repeat; Off: nothing drawn,
the shoulders do nothing there.

### 7.14 Other music apps (nowplaying1, 2026-11-13; remaster-features §6.2)

INTERFACE tab, section MUSIC, after PLAYLIST (also on the launcher's Music page, with a "Detected" line):

| Row | Values (ini key) | Default | Row id |
|---|---|---|---|
| OTHER MUSIC APPS | Off / Show now playing (`other_music_apps` 0-1) | Off | 91 |

On, the song another app plays (Spotify, a browser, any player in the system's media controls) shows in the game's EA
TRAX popup when it starts playing: title, artist, album (no album: the player's name). The row's description adds
"Now: ..." with what is detected. Readers: Windows media sessions (Windows 10 1809 and later) and Linux MPRIS on the
session D-Bus; macOS has no public interface, so the row is disabled there. Presentation only: the simulation and the
pad report are untouched, the game's music keeps playing (RADIO VOLUME turns it down), and with the row off no reader
is made and no system call happens. Replays record the songs shown and play them back without reading the system.
Code: `src/native/now_playing.{h,cpp}`, `now_playing_win.cpp`, `now_playing_mpris.cpp`, `dbus_dl.h` (host);
`tools/native/now_playing_popup.h` (the popup, the VBlank latch); `src/b3settings/page.cpp`, `src/b3replay/inproc.cpp`
(`b3r_other_music_apps`, `b3r_now_playing`); `src/b3launcher/main.cpp`. Tests: `tests/test_now_playing.cpp`,
`test_settings.cpp` (`other_music_apps_row`), `test_launcher.cpp` (`other_music_apps_on_the_music_page`); in the game:
`SCENARIO=nowplaying tools/native/race_check.sh` (remaster-features §6.2 "Checks").

### 7.15 Open the launcher at start (launchskip1, 2026-11-14)

Players start the game through the launcher (Windows: `Burnout3Launcher.exe`; Linux: `b3launcher` or its menu entry;
macOS: the app, whose program is the launcher). This setting lets the launcher step aside:

| Row | Values (ini key) | Default | Row id |
|---|---|---|---|
| OPEN THE LAUNCHER AT START | Off / On (`launcher_at_start` true / false) | On | 210 |

The row is on the INTERFACE tab after DATE FORMAT (in the game, and on the launcher's Settings page); the launcher's
Play page has the same setting as a switch, **Start the game directly next time (skip this launcher)** (On there = the
row Off). The game only keeps the value: it never starts the launcher itself.

Off: when the launcher starts, it shows a one-second "Starting Burnout 3..." screen and then starts the game exactly as
START GAME does (the same settings, save file, mods, music and HD pack; the launcher closes once the game runs, and
shows itself with the game's last log lines if the game ends at once). The launcher shows its pages instead when:

- it was started with `--launcher`;
- Shift is held (keyboard), or Back / Select on a controller, while it starts or during the "Starting" screen;
- no game data is set, or the data is missing or incomplete, or it will not run (another version of the program);
- the game program is not beside the launcher;
- the last run did not end cleanly (a crash, or the game is still running; safe mode may be next: section 9 of
  PLAYING.md).

The Play page then says which of these opened it. Shift is read from the system where it can be read without a window
(Windows: `GetAsyncKeyState`; macOS: `[NSEvent modifierFlags]`) and from the launcher's own window once it has the
focus (Linux). Code: `src/b3launcher/core.{h,cpp}` (`skip_launcher`), `src/b3launcher/main.cpp` (`skip_poll`, the
Play page), `src/b3launcher/spawn.cpp` (`shift_key_held`); `src/b3settings/page.cpp`, `settings.cpp`. Tests:
`test_settings.cpp` (`launcher_at_start_row`), `test_launcher.cpp` (`skip_the_launcher`); the launcher's
`--screenshot` run (`skip`: each exception, the start path, Shift pressed and a controller's Back held on the
"Starting" screen, the Play page's switch in pc_settings.ini).

### 7.16 Speed units (units1, 2026-11-14)

| Row | Values (ini key) | Default | Row id |
|---|---|---|---|
| SPEED UNITS | MPH / KM/H (`speed_units` 0 / 1) | MPH (the US game's) | 220 |

On the INTERFACE tab right before DATE FORMAT (in the game, and on the launcher's Settings page). KM/H shows the
game's speeds and distances in kilometres: the race HUD's speedometer ("kph", the PAL game's label), the distance
messages (KM / MILES), the race score list (km / mi), the escape distance, item values, records rows and pages, the
stats and Driver Details (M / FT). The words and numbers are the game's own: it already has both sets, chosen by its
language word (0x004E2918; 0 and 1, US / UK English, mean miles, the other languages kilometres), which the US disc
always sets to 1.

How (`tools/native/speed_units.h`): the word also picks the number separators, the '$' place, the online presence
strings, sprintf formats, FSM ids and the language byte a profile saves, so it is never changed for good. With KM/H,
a port-layer hook (`port:speed_units`) on each of the 11 display functions that test it sets it to 2 for that call
only and puts the game's value back when the call returns (2 takes the default arm of every other switch in those
functions and their callees). The m/s-to-mph factor the sound, AI, score logic, camera and crash analyser use never
reads the word, so nothing but the text changes. With MPH the hooks store nothing: the run is byte-identical.

When it applies: the speedometer's label is written at its Prepare, at the race start, so the speedometer follows the
setting from the next race; every other site reads it each time it draws (a change made in the pause menu shows in the
next distance message). While a replay is recorded the value is latched per VBlank (SETS `speed_units`, from its first
KM/H VBlank: a recording at MPH has no entry) and played back from the file. `B3NATIVE_SPEED_UNITS=0 / 1` overrides it
(tests); `B3NATIVE_SPEED_LOG=1` prints the speedometer's number on each VBlank with the speed in m/s.

Checks: `test_settings` (`speed_units_row`), `test_hook_table` (`speed_units_sets_the_word_only_inside_the_display_functions`,
`speed_units_all_sites_or_none`); the golden at the default (byte-identical); in the game, the race scenario with
`B3NATIVE_SPEED_UNITS=1` and `B3NATIVE_SPEED_LOG=1` against the same at MPH: the speed in m/s equal on every one of the
2,455 speedometer VBlanks, the numbers 1.61 times the MPH ones (kph 80 / mph 49 at 22.35 m/s), the race shot different
only inside the speedometer's box. Guest RAM (`B3NATIVE_RAM_DUMP`, word by word) is equal up to the speedometer's first
number and afterwards differs only in presentation state: the speedometer's text and layout (its component and the HUD
objects' positions), the 2D objects' render batch, the RenderWare DMA packet buffers and their pointers (a longer
number or another label is a different packet), and what follows from the virtual EE clock those few extra
instructions move (the hi-res timer's words, the audio voice manager's millisecond stamp and the voice / stream
positions it advances). No car, world, score or AI state differs.

### 7.17 Photo mode in the pause menu (photorow1, 2026-11-15; remaster-features §5 "Photo mode")

| Row | Values (ini key) | Default | Row id |
|---|---|---|---|
| PHOTO MODE IN PAUSE MENU | Off / On (`photo_row` true / false) | On | 240 |

On the INTERFACE tab after VIDEO SKIP HINT. On: the race's pause menu (RESUME, RESTART, AUDIO, CONTROLLERS, SETTINGS,
QUIT) gets a PHOTO MODE row between SETTINGS and QUIT; Confirm on it enters photo mode exactly as P does (the pause menu
is hidden and the game sees a neutral pad), and leaving photo mode (P, Escape, B / Circle) returns to the same paused
menu. The rows above it keep their place. The row is not offered in the 2-player split screen (photo mode is one
view's). The quality presets, PS2 ORIGINAL included, never touch it: they set visuals only. P and L3 + R3 work with the
row on or off.

How (`tools/native/pc_menu.h`, `pause_rows.h`): the game's pause page has seven row slots, full once the port's SETTINGS
row is in, with the action codes and the race flag following them in the object; so the selection component is handed a
private array of up to eight rows (in the port's guest RAM region, `GuestArena::kPcMenu` + 0x300) and the page's count is
one higher. The pause page's Update wrapper takes Confirm on the new row (`b3r_photo_enter` -> `PhotoMode::request_enter`)
and for the rows after it shows the game's Update the index one lower, so the game's own tables are read at their own
indices. The label is written in guest RAM as SETTINGS' is (English only, as the port's other rows). The row's presence
depends on the setting, read when the pause page is built (the next pause after a change). `B3_PHOTO_ROW=0 / 1`
overrides the setting (the golden runs set 0: the row would change those frames). While a replay is recorded the value is
latched (SETS `photo_row`; a recording without the entry plays with no row, as it was made).

Checks: `test_settings` (`photo_row_setting`), `test_pause_rows`; in the game `SCENARIO=photorow tools/native/race_check.sh`
(photo mode entered through the row, left with Escape, the pause menu back as before) and the photo / pause scenarios.

### 7.18 Effect levels, render scale and battery saver (effects2, 2026-11-15)

For weak PCs, integrated graphics, ARM laptops and handhelds. All of it is drawn only: the captured state is read and never
changed, so the game's memory, trace and DMA are identical at every level (the golden record at HIGH is byte-identical;
the levels below High are never in it).

| Row | Values (ini key) | Default | Row id | Tab |
|---|---|---|---|---|
| SPARKS | Off / Low / Medium / High (`sparks` 0-3) | High (3) | 44 (as before) | EFFECTS |
| CRASH DEBRIS | Off / Low / Medium / High (`popcorn` 0-3) | High (3) | 46 (as before) | EFFECTS |
| BATTERY SAVER | Off / On (`battery_saver`) | Off | 245 | EFFECTS, section POWER |
| RENDER SCALE | 100% / 85% / 75% / 67% / 50% (`render_scale` 50-100) | 100% | 246 | GRAPHICS, after TEXTURE FILTERING |

The presets (QUALITY PRESET): PS2 ORIGINAL, HIGH and ULTRA are High and 100%; MEDIUM is Medium and 100%; LOW is Low and 75%.
A settings file from before the levels (`sparks = true` / `false`) reads as High / Off. The rows are on the settings page
only; the launcher shows the same page.

**Levels** (`src/b3replay/sparks.h`). High draws every captured particle, exactly the build before the levels existed. A
particle's identity is its array, bank and slot in the game's ring (the same number for as long as it lives), mixed with a
32-bit finaliser; two bits of it pick the share: Low keeps one residue of four, Medium two (nested: raising a level only
adds particles), Off none. Nothing is chosen per frame, so nothing flickers. For sparks the unchosen ones die in the build's
copy of the ring (the game's empty-slot time), so the spark program model skips them in every one of the history intervals:
the CPU time falls with the share, not only the GPU's. To keep a thinned shower reading as bright, the kept sparks' streaks are
drawn wider (Low x1.6, Medium x1.25) and the kept debris sprites larger (Low x1.3, Medium x1.12); at High nothing is multiplied.
Smoke, dust, glass shards and fire are drawn (effects3) and take these levels (debris, gravel, snow, glass and the crash banks:
CRASH DEBRIS; smoke, fire, explosions: SPARKS), so they have no row of their own, but are drawn one step lighter than the row (High: a half, Medium / Low: a quarter; `B3_PARTICLES=full` for the whole set); the game's other debris array (CB3DebrisArray) is not drawn yet. The skid marks, scrapes, coronas and shadows cost little (see the measurements) and keep their
on / off rows.

**Render scale** (`src/b3gfx/b3gfx_sdl.cpp` `render_scaled`, `Options::render_scale`). The 3D picture (the scene, the effects, the
sun, the bloom and blur passes, the depth of field) is drawn into targets at the scale and copied up into the window-sized frame
with a bilinear filter; the 2D stage (the HUD, the menus, our pages) is drawn over it at the window's size, so text stays sharp.
2D items that test depth against the scene (the rival markers) are drawn with the 3D, at its scale. The 2-player split screen
scales each view the same way. At 100% none of this runs and the frame is as before. It applies at once.

**Battery saver** (`b3settings::on_battery`, `src/b3replay/live.cpp`). With the row on and SDL reporting that the PC runs on
battery (SDL_GetPowerInfo, read every two seconds), the host applies a copy of the settings with the frame rate capped to 60
(a lower cap is kept) and SPARKS / CRASH DEBRIS at most Medium; plugged in, or when the power state is unknown (a desktop), the
saved settings apply. The stored settings are never changed.

**Measured** (2026-11-15; this PC's CPU-only Vulkan, so only the ratios mean anything; a quiet machine and real GPUs were
not available): the heaviest crash (Road Rage, poll 5490: 28,400 spark quads and 2,600 debris sprites) builds its draw list in
about 5.3 / 4.0 / 3.0 / 1.6 ms at High / Medium / Low / Off (frame CPU time, median), and its frames cost about 132 / 121 /
119 / 109 ms to rasterise (the same order). In a normal race on this rasteriser the post passes and MSAA cost more than the
effects: bloom 16%, MSAA 4x -> off 18%, the motion blur 10%, the whole-track draw distance 11% of a frame.

Checks: `test_settings` (`effect_levels_effects2`, `battery_saver_effects2`, `render_scale_effects2`), `test_effect_levels` (the
shares are stable, nested and the right size; per-level spark quad and debris sprite counts on hand-made captures; High twice
gives byte-identical vertices; every Low / Medium quad is one of High's); the golden record at HIGH.

### 7.19 Languages (1.1, 2026-11-18)

The game's text in another language, from the player's own European disc, and the port's own text in the same
language.

| Row | Values (ini) | Default | Id |
|---|---|---|---|
| LANGUAGE | English and every imported language (`language`: a two-letter code, FR GE IT SP DU SW EN; empty = English) | English | 270 |
| IMPORT LANGUAGES | an action: choose a European disc image or folder (launcher only) | | 271 |

- **The game's text.** A European disc carries one pair of string tables per language (`DATA/GLOBAL<code>.BIN`,
  `DATA/HEAD<code>.BIN`, the same format and ids as the US disc's `GLOBALUS.BIN` / `HEADUS.BIN`). IMPORT LANGUAGES copies
  the pairs once into `<preferences>/languages/DATA/` (include/b3asset/language.h); the disc is not needed afterwards. No
  game text ships with the port. native_play gives the chosen pair to the game in place of the US pair
  (src/native/platform_disc.cpp `set_language`); a mod's GLOBALUS.BIN still wins. Applies at the next start. Japanese
  (on the Japanese and Korean discs) is listed but not offered: the US game's fonts have no CJK letters.
- **The port's own text.** The PC SETTINGS page (in the game and on the launcher's Settings page) is shown in the chosen
  language when it is imported (`b3settings::ui_language`); its labels, values and description sentences come from
  `include/b3settings/i18n.h` (our own words, one table per language). Paths, statuses and mods' rows stay as they are.
  The launcher's own pages are translated where they are drawn (src/b3launcher/font.cpp `Text::set_language`: every
  text the launcher draws or measures goes through the same tables; its live status lines, made of parts, stay English).
  The ONLINE dialog's "Online play is coming soon." comes in the language too (tools/native/online_message.h).
- English is the default and unchanged: with no language set the page, the game and the golden record are as before.

Checks: `test_settings` (`language_row_and_edit`, `translations_cover_the_page`: every word of a full page has its
translation in each language, `translate_page`), `test_discfs` (`language_packs_on_a_disc_folder`).

### 7.20 Analog triggers (1.1, 2026-11-18)

| Row | Values (ini) | Default | Id |
|---|---|---|---|
| ANALOG TRIGGERS (INTERFACE > CONTROLLER, after CAR SERIES ON L1 / R1) | Off / On (`analog_pedals` true / false) | On | 272 |

On: a trigger bound to accelerate or brake drives the game's own analog throttle and brake, the right stick's up / down
(`CB3ControllerMapping::GetAccelerator` 0x12FA40 / `GetBrake` 0x12F970 read it besides Cross / Square). The trigger's
travel t is sent as the stick value 0.1 + 0.8 t^0.25, which the game's dead zone (0.1) and curve (the fourth power)
turn back into a throttle of about t: light pressure, light throttle. Both triggers past the threshold together press
both buttons (the powerslide), as before. The face buttons still press fully. Off: 1.0.0's behaviour, the trigger's
pressure on the button. Live (native_play reads it each VBlank); input-changing only for trigger players (a trigger
held fully still gives full throttle).

Checks: `test_input_keys` (`analog_pedals_drive_the_right_stick`, `pedal_stick_makes_the_throttle_follow_the_pedal`),
`test_input_bindings` (`analog_pedals_with_the_bindings`), `test_settings` (`analog_triggers_row`).

### 7.21 Chase camera distance and height (1.1, 2026-11-18)

| Row (CAMERA tab, after FOV CORRECTION) | Values (ini) | Default | Id |
|---|---|---|---|
| CAMERA DISTANCE | -10% / Game / +10% ... +50% (`camera_distance` 90..150 in 10s) | Game (100) | 62 |
| CAMERA HEIGHT | Game / +0.25 m ... +1 m (`camera_height` 0..100 cm in 25s) | Game (0) | 63 |

Drawn only, like FIELD OF VIEW (src/b3replay/view.cpp `View::apply_fov`): on views with a chased car (the same test as
FOV CORRECTION: a race car ahead within chase reach, near the view axis) the eye moves back along the view to the
chosen share of its distance to the car (a closer view stops short of the car's rear), rises along the camera's up,
and turns down about the axis between the old and new view directions so the point it looked at stays centred.
In-car, bumper, replay and crash cameras without a chased car are unchanged. The game's camera, physics and replays
are not touched; with Game / Game the picture is the original (golden unchanged).

### 7.22 Upscaling (1.1, 2026-11-18)

| Row (GRAPHICS > IMAGE, after RENDER SCALE) | Values (ini) | Default | Id |
|---|---|---|---|
| UPSCALING | Bilinear / Sharp (`upscaling` 0 / 1) | Sharp | 247 |

Only with a RENDER SCALE below 100%. Sharp (src/b3gfx/shaders/post.frag `sharp_upscale`, the port's own): a
Catmull-Rom cubic over the 4 x 4 source texels, held within the range of the 2 x 2 it lies between (no halos), then
sharpened by its difference from the bilinear value, less where the local contrast is already high. Bilinear is 1.0's.

### 7.23 Frame rate limit pacing (1.1, 2026-11-18)

FRAME RATE LIMIT (and BATTERY SAVER's 60) now waits on a fixed schedule (each frame's deadline one period after the
last), so the average is the limit exactly. With VSYNC on and a limit at or above the display's refresh, the limiter
stands aside and vsync alone paces the frames: two clocks at the same rate beat against each other and dropped a
frame now and then (55-58 fps reported on 60 Hz screens).

## Appendix: candidate gameplay mods (for later; not built, not designed)

Tags: [G] for what each would touch. None has been looked at in the code yet.

| Mod | What it changes | Where it would act [G] |
|---|---|---|
| Traffic density (more or less traffic, as with pnach codes) | traffic spawn count and spacing | `CB3TrafficSystem` spawn parameters |
| Traffic checking (hit same-direction traffic without crashing, Revenge style) | collision response against traffic | the crash / takedown decision on traffic contacts |
| No traffic | traffic off (practice, Burning Lap feel) | as above |
| Rival AI aggression / rubber band | AI driver tuning | `CB3AIDriver` difficulty (`CalcDifficultyIndex`) |
| Infinite boost, boost fill rate | boost economy | `CB3Score` / boost tuning (82 registered tuning variables, remaster §5) |
| Crashbreaker always available, Impact Time length | Crash-mode tuning | crash-mode tuning variables |
| Crash mode score multiplier, junction retry | scoring | `CB3Score` |
| 60 FPS crashes (same step, not bit-identical to the 30 FPS schedule, frame-rate.md §5) | crash camera tick rate | the crash camera's tick schedule |
| Variable tick | one tick per displayed frame | refused as a setting (§1) |
| Steering curve / sensitivity (gyro steering and the boost toggle are built opt-in: §7.12, §7.1) | input values the game sees | host input before the pad record (replays, but changes play) |
| Play as traffic vehicles, all cars unlocked | car availability | profile / car select |
| Developer tuning console (remaster §5) | any registered tuning variable | `CGtCommsDatabase` registry |
| Fast loading that skips loading screens when the data is ready | front-end timing | loading flow (guest timing). The setting FAST LOADING (§7.8) is not this: it runs the same loading screens unpaced, so the guest timing is unchanged |

Order: wiring the E rows and the page first, then the M rows. The B rows (particles, debris, glyphs, panini,
exclusive fullscreen) are listed, not promised.
