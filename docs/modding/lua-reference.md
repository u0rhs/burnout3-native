# Lua script reference

Script mods: a mod that carries a Lua 5.4 script instead of a library. The script gets a `b3` table with the same groups
as the C API of code mods ([sdk-reference.md](sdk-reference.md)): the log, callbacks, game events, guest memory, calls
and hooks on the game's functions, settings rows, drawing over the game, files, and read-only views of the race, the
player's car and the score. The plan behind it: [DESIGN.md](DESIGN.md) §10. A walk-through:
[tutorial 06](tutorials/06-a-lua-script.md); an example: [examples/mods/lua-speedo](../../examples/mods/lua-speedo).

## A script mod

```ini
[mod]
id = my-script
version = 1.0
api = 3                 # script mods need api 3

[script]
main = scripts/main.lua
gameplay = no           # no: it only reads and draws (writes refused); yes (or not given): it may change the game
budget = 1000000        # optional: Lua instructions per VBlank (10000 to 50000000; default 1000000)
memory = 64             # optional: megabytes of Lua memory (1 to 512; default 64)
```

| Key | Meaning |
|---|---|
| `main` | the script the game runs, a `.lua` file in a folder of the mod (`scripts/main.lua`; a script at the top of the mod is refused). Its folder is the script folder: `require` finds the other scripts there, and every file in it is in the mod hash. |
| `gameplay` | `no`: the script only reads and draws; every call that could change the game is refused (below). `yes`, or not given (with a note in the log): it may write the game's memory, call its functions and replace them, and the Mods page shows the mod as changing gameplay. |
| `budget` | how many Lua instructions the script may run per VBlank (below). |
| `memory` | how much memory its Lua state may use (below). |

**api 3 beside api 2.** The port's mod API is 3: data mods may say 1, 2 or 3; a mod with `[code]` needs 2 or more; a mod
with `[script]` needs 3. A mod has `[code]` or `[script]`, not both (make two mods; one may `[requires]` the other). The C
ABI (`B3MOD_API_VERSION` 1) did not change: a script mod runs on it inside the game.

Scripts are data: there is no build step, and the same files work on Windows, Linux and macOS. The launcher does not ask
before a script runs (it asks for code mods), because a script can only do what the `b3` table offers.

## Loading

native_play loads script mods with the code mods, in the load order, after the port's own hooks and before the game's
first instruction ([sdk-reference.md](sdk-reference.md) "Loading"). For each script mod:

1. a Lua state of its own is made, with the libraries below and the `b3` table;
2. `main` runs (the **main chunk**); this is where the script registers: `b3.settings.add`, `b3.hook.wrap` /
   `replace` and `b3.on` for the wrapped game events work only here (the hook table is fixed when the game starts);
3. the functions given to `b3.on_load` run, with `false`;
4. its hooks and rows go in, all or nothing, as a code mod's (`mod:<id>` on the hook table).

The main chunk and `on_load` have 50 times the per-VBlank budget. An error there, or a refusal, leaves the mod out with its
files, as a code mod that does not load:

```
mod my-script: error: the script did not load: an error
mod my-script: error:   scripts/main.lua:12: attempt to index a nil value (global 'x')
native_play: mods: my-script: not loaded: its script did not load (the lines above say why)
native_play: mods: my-script: script loaded (0 replaced, 2 wrapped, 1 settings, 3 callbacks)      (when it loads)
```

## What a script has (the sandbox)

| Available | Not there |
|---|---|
| the base functions (`assert`, `error`, `pairs`, `ipairs`, `next`, `pcall`, `xpcall`, `select`, `type`, `tostring`, `tonumber`, `rawget` / `rawset` / `rawequal` / `rawlen`, `setmetatable`, `getmetatable`, `collectgarbage`, `warn`), `coroutine`, `math`, `string`, `table`, `utf8` | `io`, `os`, `debug`, `package`, `dofile`, `loadfile`, `string.dump` (no files, no clock, no processes, no network, no precompiled code) |
| `print(...)`: the same as `b3.log` | |
| `load(text [, name [, mode [, env]]])`: text chunks only (the mode is always `"t"`; a precompiled chunk is refused) | |
| `require(name)`: `<script folder>/<name>.lua`, a `.` in the name being a folder (`require("ui.box")`: `scripts/ui/box.lua`); run once, its value kept; names of letters, digits, `_`, `-` and `.` only | `require` of anything outside the script folder; C modules |
| `b3.data`: the disc as the game reads it (through the active mods' files), and the mod's own folder, read-only | any other file |

`warn(...)` messages go to the log as warnings.

## Limits: the budget and the memory cap

- **Budget.** Every Lua instruction the script runs between two VBlanks counts: its `on_vblank`, `on_frame` and event
  functions and its hooks. Past `budget` (default 1,000,000; looked at every 1,000 instructions) the script is
  **stopped**: an error that a `pcall` in the script cannot catch (every instruction after it fails too), then:
  ```
  mod my-script: error: the script was stopped at VBlank 9301: it ran past 1000000 Lua instructions in one VBlank (its budget: a loop that does not end?); the game goes on without it
  mod my-script: error:   scripts/main.lua:20: the script was stopped (its instruction budget, or an error before)
  ```
- **Memory.** Its Lua state may hold up to `memory` megabytes; past that, the allocation fails, and the script is
  stopped with `it used more than 64 MB (its memory cap)`.
- **Errors.** An error that reaches the game (not caught by the script's own `pcall`) stops the script too, with the
  message and the traceback in the log.

A stopped script is stopped for the rest of the run: its callbacks are no longer called, its hooks pass every call to the
game's function with the game's arguments (a hook that stopped half way: the call the game made, as without the mod),
its drawing ends, its Lua state is closed. The game goes on. With Developer mods on, a changed script loads again
(hot reload, below).

What the budget does not see: a single call into Lua's C library runs to its end before the budget is looked at again
(`string.rep`, `table.concat` and the like are bounded by the memory cap; a pattern search on a long string with several
`.-` can take long).

## Determinism

A script mod plays back exactly from a pad recording or a replay file made with the same mod set, as the game does:

- Everything a script does runs on the game's thread at fixed points: the main chunk before the game starts, then
  `on_vblank` at each virtual VBlank, `on_frame` at each game frame, the hooks and wrapped events inside the game's own
  calls, `on_shutdown` at the end.
- There is no clock but the game's: `b3.vblank()` (60 per second of game time; it stops while the game is paused for the
  window). `os.time` / `os.clock` are not there.
- `math.random` starts seeded from the mod's id (FNV-1a 64 of it), so it gives the same numbers in every run;
  `math.randomseed(x)` with a seed works as in Lua, `math.randomseed()` without one is refused.
- Lua's string hashing has a fixed seed in the port (a normal Lua seeds it from the clock), so `pairs` over a table
  with string or number keys gives the same order in every run. **Tables keyed by tables or functions** iterate in an
  order that depends on memory addresses: do not let that order decide what a script does to the game (keep an array
  for order). The same holds for `tostring` of a table or a function and `%p` in `string.format`.
- Floats are host doubles inside the script. Arithmetic is the same on every machine; `math.sin`, `math.cos`,
  `math.exp`, `math.log` and `math.pow`-like `^` come from the system's C library, which may differ in the last bit
  between Windows, Linux and macOS: a replay recorded on another system can drift if such a value is written into the
  game.
- Writes into the game go through the same bounds-checked calls as a code mod's.
- The mod set hash covers every file of the script folder (`script <path> <size> <fnv>` lines in the mod hash,
  [mod-format.md](mod-format.md)), so a replay made with another version of a script says so.

## The `b3` table

### Info and log

| Name | What |
|---|---|
| `b3.id`, `b3.version` | the mod's id and version (mod.ini) |
| `b3.port_version` | the port's version line |
| `b3.gameplay` | `false` when mod.ini says `gameplay = no` |
| `b3.developer` | `true` with Developer mods on (hot reload) |
| `b3.vblank()` | the virtual VBlank count, an integer: the clock to use |
| `b3.log(...)` | one line in the game's log: `mod <id>: <the values, as tostring gives them, separated by spaces>` |
| `b3.warn(...)` | the same as a warning (`mod <id>: warning: ...`) |
| `b3.rgba(r, g, b [, a])` | a colour for `b3.draw` (0 to 255 each; `a` 255 when not given) |
| `b3.has_function(addr)` | `true` when the game function at the guest address is native (hookable and callable) |

### Callbacks

| Name | Called |
|---|---|
| `b3.on_load(fn)` | `fn(reloaded)` after the main chunk: `false` at the start, `true` after a hot reload |
| `b3.on_vblank(fn)` | `fn(vblank)` at every virtual VBlank, after the port's VBlank work and the mods' settings changes for that VBlank |
| `b3.on_frame(fn)` | `fn()` at every game frame (the game's pad poll) before the frame goes to the renderer: draw here |
| `b3.on_shutdown(fn)` | `fn()` when the run ends normally |

Any number of functions per callback, called in the order given; several mods in the load order.

### Game events

`b3.on(name, fn)`: `fn(e)` with a table `e` when the event happens. Three events are wraps the port puts on the game's
own functions (only for a script that listens to them, and only from its main chunk): their functions run **inside the
game's call**, after the game's function returned. Two are read from the player's score at each VBlank, before
`on_vblank`.

| Event | When | `e` | Source |
|---|---|---|---|
| `race_start` | the player's score's race state becomes 2 (racing) | `vblank` | read at the VBlank: CB3Score +0x27C of the player's race car ([C] src/b3replay/capture.h ScoreState: 0 countdown, 1 start, 2 racing, 3 finished) |
| `lap` | the player's laps done grows (a lap line crossed) | `lap` (laps done, 1 ..), `time` (that lap's time, seconds) | read at the VBlank: CB3Score +0x18 laps done, +0x1C the lap times ([C] capture.h ScoreState) |
| `crash` | any race car starts crashing | `car` (its guest address), `player` | wrap of `CB3RaceCar::StartCrashing` 0x001519F0 ([C] decomp config/symbol_addrs_b3names.txt), `car` its `this` |
| `takedown` | a race car takes down another one (counted by the game) | `car` (who did it), `victim`, `player` (the player did it), `victim_player`, `count` (the score's takedowns now), `score` | wrap of `CB3Score::OnTakeDown` 0x001553F0 ([C] symbol file): it fires when the score's takedown count (+0x68) grew in the call [L decomp B3Score_001553F0.cpp: `field_68 + 1` for a victim not taken down yet]; `car` is the score's car (+0xC8) [L same file; C in a race run 2026-11-12: it was the score's address - 0x10C0 and one of the six race cars], `score` the score's address |
| `finish` | a race car's finish state is set | `car`, `player`, `state` (the game's EB3ScoreFinishState: 1 finished, 2 / 3 out, 4 the other finish; capture.h ScoreState +0x8) | wrap of `CB3RaceCar::SetFinished` 0x00151780 ([C] symbol file), `state` its argument |

Seen in the game (race_check's race and takedown scenarios with a script logging every event, 2026-11-12): `race_start`
at VBlank 21070, after which the score's race time counts from 0 (3.85 s at VBlank 21300); `crash` for rivals and for
the player (`player` true, `car` 0x0064D3B0), the same VBlanks in two runs; one `takedown` of a rival by a rival.
`lap` and `finish` were not reached by those runs (the scripted drive stops before the first lap line); the unit test
checks both on synthetic memory.

Not offered: events whose function the port cannot name from evidence (a lap function by its own name: the decomp tags
its name a guess, so `lap` is read from the score instead). Other events: `b3.hook.wrap` on any function a script knows.

### Guest memory: `b3.mem`

| Name | What |
|---|---|
| `b3.mem.read(kind, addr)` | the value at a guest address: `kind` `"u8"`, `"s8"`, `"u16"`, `"s16"`, `"u32"`, `"s32"` (integers) or `"f32"` (a float); `nil` outside EE RAM (32 MB) and the scratchpad |
| `b3.mem.write(kind, addr, value)` | write it; `true` done, `false` outside them or in the program's code (0x00100000-0x00483F00: that code never runs natively, use a hook). A value that does not fit the kind is an error. **`gameplay = no`: refused (an error).** |
| `b3.mem.read_bytes(addr, n)` | `n` bytes (up to 1 MB) as a string, or `nil` |
| `b3.mem.write_bytes(addr, s)` | the string's bytes; as `write` |

The guest is little-endian. Writes from the main chunk are applied once the mod has loaded (all or nothing).

### Calls and hooks

| Name | What |
|---|---|
| `b3.call(addr, ...)` | call the game's function at `addr` through every hook on it, from a callback or a hook (not from the main chunk, nor from inside a coroutine). Arguments in order: integers and booleans into a0, a1, a2, a3, t0 .. t3; floats (Lua numbers with a fraction, or `1.0`) into f12 .. f19. Returns `v0` (an integer: the low 64 bits, a 32-bit result sign-extended), `f0` (a float), `v1`. An error when there is no native function there. **`gameplay = no`: refused.** |
| `b3.hook.wrap(addr, fn)` | main chunk only: `fn(call)` runs around every call of the function at `addr` (on top of the port's own wrappers, as a code mod's wrap). If `fn` does not call `call:next()`, the game's function runs after it: a wrap never drops the call. |
| `b3.hook.replace(addr, fn)` | main chunk only: `fn(call)` runs instead; the game's function runs only if `fn` calls `call:next()`. One mod per address. **`gameplay = no`: refused.** |

The `call` object (valid only during its hook):

| Method | What |
|---|---|
| `call:arg(i)` | integer argument `i` (1 .. 8: a0 .. t3), 64 bits |
| `call:farg(i)` | float argument `i` (1 .. 8: f12 .. f19) |
| `call:sp()` | the guest stack pointer (stack arguments from it) |
| `call:next()` | run the layer below (the game's function, or another mod's replace) with the arguments as they are now; returns `v0, f0, v1`. Once per call. |
| `call:result()` | `v0, f0, v1` as they are now |
| `call:set_arg(i, v)`, `call:set_farg(i, v)` | change an argument before `next` (**`gameplay = no`: refused**) |
| `call:set_result(v0 [, f0])`, `call:set_fresult(f0)` | what the caller gets (**`gameplay = no`: refused**) |

Which addresses can be hooked and what a hook sees (calls between two generated units may not go through it):
[hooks.md](hooks.md). Addresses: the decomp's `config/symbol_addrs.txt`, `gen/native/manifest.tsv`.

### Settings: `b3.settings`

| Name | What |
|---|---|
| `b3.settings.add{ key = ..., label = ..., kind = ..., ... }` | main chunk only: a row on PC SETTINGS > MODS and on the launcher's Mods page. `kind` `"toggle"` (the default; `default` `true` / `false`), `"choice"` (`values = { "A", "B", ... }`, 2 to 16 names of up to 24 characters; `default` one of the names) or `"slider"` (`count` levels, 2 to 101; `default` a level). `key`: a-z, 0-9, `_ - .`, up to 32; `label` up to 40 characters; `description` optional |
| `b3.settings.get(key)` | the value now: a toggle `true` / `false`, a choice its name, a slider its level; `nil` for a key the mod did not add |
| `b3.settings.index(key)` | the value as an index (0 ..) |

Values are kept, changed and recorded as a code mod's ([sdk-reference.md](sdk-reference.md) "Settings"): a change made
while the game runs reaches the script at the next VBlank; `native_play --mod-set` sets them for a test.

### Drawing: `b3.draw`

| Name | What |
|---|---|
| `b3.draw.quad(x, y, w, h [, rgba [, texture]])` | a rectangle in the game's 640 x 448 screen (top left 0, 0), filled with the colour (white when not given), or the texture tinted by it |
| `b3.draw.text(x, y, size, rgba, text)` | text from its top left, capital letters `size` high, in the port's stroke font (A-Z, 0-9, space, `+ - / . : ;`; lower case drawn as upper case) |
| `b3.draw.texture(w, h, pixels)` | a texture from a string of `w * h * 4` bytes (red, green, blue, alpha per pixel, rows from the top; up to 1024 x 1024); its id |

Draw in `on_frame`, every frame: what is drawn during a frame is shown until the next one. Up to 256 items per mod and
frame. Drawing never changes the game.

### Files: `b3.data`

| Name | What |
|---|---|
| `b3.data.size(path)` | the file's size, or `nil` |
| `b3.data.read(path [, offset [, n]])` | its bytes (from `offset`, `n` of them; to the end when `n` is not given) as a string, or `nil` |
| `b3.data.open(path)` | a file object (`nil, message` when there is none) with `:read([n])` (`nil` at the end), `:seek(pos)`, `:tell()`, `:size()` |

`path`: `"disc:DATA/GLOBALUS.BIN"` -- a disc file as the game gets it (through the active mods' files); anything else
-- a file in the mod's own folder (`"data/table.bin"`; no `..`, no absolute path). Nothing can be written.

### Read-only views: `b3.view`

Each call reads the game's memory now and returns a fresh table (or `nil` outside a race). The addresses are the ones the
port's renderer and speedrun timer already read; the source of each is cited.

**`b3.view.race()`** -- always a table:

| Field | What | Source |
|---|---|---|
| `active` | a race is on: 1 to 6 race cars, the first being the player's (0x0064D3B0) | gWorld 0x00522660 +0x12AD1C, +0x12AD28 ([C] src/b3replay/capture.cpp decode_tick) |
| `timer` | the game timer, seconds | gWorld +0x20 ([C] capture.h Tick::now) |
| `ticks_per_frame` | game ticks per frame | 0x0051BAC4 ([C] capture.cpp) |
| `cars` | race cars | gWorld +0x12AD1C |
| `split_screen` | two-player split screen | *0x0051BA88 == 0x0051AEC0 ([C] capture.h Tick::split) |
| `logic` | the stage logic's vtable (the game mode) | (*0x0051BA88)+0x1B4 ([C] capture.cpp decode_lap) |
| `time_attack` | the Time Attack / Burning Lap logic | `logic` == 0x004DEBC0 ([C] capture.h) |
| `vblank` | `b3.vblank()` | |

**`b3.view.player()`** -- the player's race car (view 0), `nil` when no race is on:

| Field | What | Source |
|---|---|---|
| `car` | its guest address, 0x0064D3B0 | gWorld +0x12AD50 ([C] capture.h kPlayerRaceCar) |
| `x`, `y`, `z` | its position (the translation row of its matrix) | car +0x30 ([C] capture.cpp decode_vehicle: the matrix at +0x00) |
| `speed` | its speed in the game's units, as the speedometer reads it | the W lane at 0x0055EF70 + 0x1CB80 + 0xB0 ([L] decomp src/nonmatching/func_001A3B30.cpp, CB3HUDSpeedo::Update, logic exact; func_001A3C70 sets that pointer for player 0) |
| `speed_shown` | the number the speedometer shows: `speed` times the unit factor, cut to an integer | the factor D_004E0798 when *0x004E2918 <= 1, else D_004E0960 ([L] same function; C in a race shot 2026-11-12: 87 beside the game's own `87 mph`, the factor 2.23694, metres per second to miles per hour, so `speed` is in m/s) |
| `speed_factor`, `speed_units` | that factor and *0x004E2918 | |

**`b3.view.score()`** -- the player's CB3Score (race car +0x10C0 = 0x0064E470), `nil` when no race is on. Fields from
src/b3replay/capture.h ScoreState ([C], the speedrun timer reads them):

| Field | Offset | What |
|---|---|---|
| `state` | +0x27C | the race state: 0 countdown, 1 start, 2 racing, 3 finished |
| `finish` | +0x8 | the finish state: 0 racing, 1 finished, 2 / 3 out, 4 the other finish |
| `time` | +0xC | the race time, seconds (0 until the start; a very large number after being out) |
| `lap` | +0x14 | the lap (-1 until the start line) |
| `laps_done` | +0x18 | laps completed |
| `lap_times` | +0x1C | an array of 8 lap times, seconds |
| `best_lap` | +0x3C | the best lap |
| `laps` | +0x2C4 | the race's laps (0: no end, Time Attack) |
| `lap_time` | +0x2C8 | the time of the lap being driven |
| `takedowns` | +0x68 | takedowns [L decomp B3Score_001553F0.cpp: OnTakeDown counts them there] |

## Gameplay

A script that writes the game's memory, calls its functions or changes a call (a replace, `set_arg`, `set_result`) can
change the game, so a script mod is gameplay-changing unless its mod.ini says `gameplay = no` ([DESIGN.md](DESIGN.md)
§6: times and replays made with it are kept apart). With `gameplay = no` the port refuses those calls: each is an error
naming the refusal, which stops the script (a `pcall` around it lets the script go on). Reading, drawing, the log,
settings, files, events and `b3.hook.wrap` (watching calls, not changing them) stay allowed.

## Hot reload (Developer mods)

With **Developer mods** on (the launcher's Mods page or PC SETTINGS > INTERFACE > DEVELOPER MODS: one switch, which
also turns on the tuning console; `--developer-mods` for one run), the port looks at each script folder every 30 VBlanks
(half a second); when a file changed, the script is loaded again at that VBlank, in a new Lua state: the main chunk, then
`on_load(true)`. The hooks, rows and wrapped events of the first load stay (the new script's `b3.hook.wrap` / `replace`
on the same addresses take them over; new ones, new rows and new wrapped events are used from the next start, with a
warning). A script stopped by an error or its budget loads again on its next change. The log says so:

```
mod my-script: its script changed: loaded again at VBlank 4210 (hot reload, Developer mods: from here this run does not play back as recorded)
```

Without Developer mods (every normal run, every test) a script is read once, at the start: a run is always one version of
each script.

## Checks

`tests/test_script_mods.cpp` (in `make test`): the `[script]` manifest and its files in the mod hash; the sandbox's
refusals (libraries, `load` of a precompiled chunk, `require` outside the folder, files outside the mod); a runaway loop
(also inside `pcall`) stopped by the budget with the game's call going on; the memory cap; `math.random` seeded per id
and the same string-key order in two states; the API on synthetic guest memory and a synthetic hook table (memory kinds,
calls, replace and wrap, settings, drawing, views, the five events); `gameplay = no`; hot reload with and without
Developer mods. In the game: [tutorial 06](tutorials/06-a-lua-script.md) runs the example with `race_check.sh` and its
replay with `replay_check.sh`.
