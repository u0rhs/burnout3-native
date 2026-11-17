# Modding the PC port

The port runs the game natively on your PC from your own copy of the disc. Mods change what the game loads from that
disc without touching the disc itself. This page says what you can change today, where to start, and where every
modding page is.

## What you can mod today

**Data mods**: a mod is a folder with a `mod.ini` and a `data` folder of files at their disc paths. When the game
opens one of those paths it gets the mod's file instead of the disc's. With the asset tool,
[`b3modtool`](b3modtool.md), you can make new versions of these files:

| What | Files | How |
|---|---|---|
| Menu and race graphics (menus, car and event pictures, the HUD, shared race art) | `DATA/FRONTEND.TXD`, `DATA/GLOBAL.TXD` | `b3modtool txd export` to PNG, edit, `txd import` ([tutorial 01](tutorials/01-replace-a-texture.md)) |
| The game's text (menus, messages, hints, event and car names, credits; also a whole translation) | `DATA/GLOBALUS.BIN`, `DATA/HEADUS.BIN` | `b3modtool strings export` to a text file, edit, `strings import` ([tutorial 02](tutorials/02-change-menu-text.md)) |
| Any other file, replaced whole | any disc path but `SLUS_210.50` and `SYSTEM.CNF` | a file you made with your own tools, put at its disc path in the mod |
| The cars' models and paints (shape, UVs, triangles, colours) | `PVEH/<class>/CARn.BGV` | `b3modtool car export` to glTF, edit in Blender, `car import` into a mod as a patch ([tutorial 08](tutorials/08-edit-a-car.md)); or all of it from Blender's menus with the add-on B3 Car Tools (new: [tutorial 09](tutorials/09-car-mods-in-blender.md)) |
| The routes' look (road and scenery shape, vertex colours, UVs, triangles, the route's textures) | `TRACKS/<area>/<route>/STATIC.DAT`, `STREAMED.DAT` | `b3modtool track export` to glTF (a section or the whole route), edit, `track import` into a mod as patches (new: [tutorial 10](tutorials/10-edit-a-track.md)); visual only: the collision and the AI paths stay the disc's |
| A change to any of these files, for sharing | the same paths | `b3modtool diff` makes a `.b3patch` (only your change); the game applies it to each player's own file when it starts ([tutorial 01](tutorials/01-replace-a-texture.md), step 9) |
| HD textures (drawn by the renderer, over the player's own HD pack) | none: a `[textures]` folder of PCSX2-format replacements | [mod-format.md](mod-format.md) "[textures]" |

**Code mods**: a mod may also carry a small C library (built with the [SDK](../../sdk/README.md)) that the game loads
before it starts. It can change what the game's functions do (hooks on their retail addresses), add rows to PC
SETTINGS (a MODS tab), draw over the game, and read files. The launcher asks before a mod's code may run
([tutorial 03](tutorials/03-a-settings-row.md), [tutorial 05](tutorials/05-an-overlay.md),
[the SDK reference](sdk-reference.md)). The port does not run the disc's program, so PS2 patches (pnach codes, a
patched ELF) do nothing here: a code mod does the same change in C.

**Tuning presets**: a mod's `[tuning] preset` sets the game's tuning variables (boost and score rules, AI, car physics,
cameras, sound mixing) as the game registers them, and the **tuning console** (PC SETTINGS > INTERFACE > DEVELOPER
MODS) shows and changes them live: [tuning.md](tuning.md), [tutorial 04](tutorials/04-a-tuning-preset.md); what each
value does: [tuning-options.md](tuning-options.md). Both
change gameplay and are marked so.

**Script mods**: a mod may carry a Lua 5.4 script instead of a library. No compiler, the same files on every system:
the script draws over the game, adds settings rows, hears game events (race start, lap, crash, takedown, finish), reads
the race, the player's car and the score by name, and, unless it says `gameplay = no`, can change the game through the
same calls a code mod has. It runs sandboxed (no files outside the mod and the disc, no clock, no network) with an
instruction budget and a memory cap ([tutorial 06](tutorials/06-a-lua-script.md),
[the Lua reference](lua-reference.md)).

**Not yet** (each is a later part of the mod kit, [DESIGN.md](DESIGN.md) §9):

- the routes' collision, their AI and traffic paths (`GAMEDATA.BGD`) and their sky (`ENVIRO.DAT`): the track import
  (above) changes only what is drawn;
- sounds and music (`.RWS`, `.AWD`) and the tuning table (`VDB.XML`): part 3b.

## Where to start

1. [getting-started.md](getting-started.md): installing mods, the load order, the shape of a mod and its `mod.ini`.
2. [Tutorial 01: replace a texture](tutorials/01-replace-a-texture.md): a picture on a menu, from export to the game.
3. [Tutorial 02: change menu text](tutorials/02-change-menu-text.md): rename a menu item; translations.
4. Code mods: [tutorial 03: a settings row](tutorials/03-a-settings-row.md) and [tutorial 05: an overlay](tutorials/05-an-overlay.md), with the [SDK](../../sdk/README.md).
   Script mods: [tutorial 06: a Lua script](tutorials/06-a-lua-script.md).
5. [Tutorial 04: a tuning preset](tutorials/04-a-tuning-preset.md): the tuning console, a preset, sharing it.
6. [Tutorial 07: export a car](tutorials/07-export-a-car.md): a car as glTF, opened in Blender; then
   [tutorial 08: edit a car in Blender and put it in the game](tutorials/08-edit-a-car.md). With the Blender add-on
   instead of the terminal: [tutorial 09: car mods with the Blender add-on](tutorials/09-car-mods-in-blender.md) (the
   add-on is new: feedback welcome).
   Routes: [tutorial 10: edit a track and put it in the game](tutorials/10-edit-a-track.md).
7. [b3modtool.md](b3modtool.md) when you need the details of a command.

## Every page

| Page | What it covers |
|---|---|
| [getting-started.md](getting-started.md) | installing and ordering mods, the mods folder, making a data mod, `mod.ini`, sharing mods the right way |
| [tutorials/01-replace-a-texture.md](tutorials/01-replace-a-texture.md) | `txd list / export / import`, a mod folder, `verify`, seeing it in the game, shipping a patch |
| [tutorials/02-change-menu-text.md](tutorials/02-change-menu-text.md) | `strings export / import`, a partial text file, a mod folder, translations |
| [tutorials/03-a-settings-row.md](tutorials/03-a-settings-row.md) | a code mod: a PC SETTINGS row, a hook on the game's string lookup, building, allowing its code |
| [tutorials/04-a-tuning-preset.md](tutorials/04-a-tuning-preset.md) | the tuning console, a change in a race, a preset saved or written by hand, gameplay-changing mods and replays |
| [tuning.md](tuning.md) | the tuning console (DEVELOPER tab, F9), presets (`[tuning]`), edits and their recording, determinism |
| [tuning-options.md](tuning-options.md) | every tuning value in the console's order (tab, section, subsection): what it does, its type and range, the YOUR CAR page |
| [tutorials/05-an-overlay.md](tutorials/05-an-overlay.md) | a code mod drawing over the game: boxes, text, a texture |
| [tutorials/06-a-lua-script.md](tutorials/06-a-lua-script.md) | a script mod: a speedometer box from the read-only views, a settings row, lap events, seeing it, hot reload |
| [lua-reference.md](lua-reference.md) | script mods: `[script]`, the sandbox, the budget and memory cap, determinism, every `b3` call, the views and events, hot reload |
| [sdk-reference.md](sdk-reference.md) | the code-mod API (sdk/include/b3mod_api.h): loading, trust, hooks, memory, settings, overlays, events, files |
| [hooks.md](hooks.md) | the hook table: its layers, what a hook sees, the mod arena |
| [tutorials/07-export-a-car.md](tutorials/07-export-a-car.md) | `car list / export`, `preview`, opening a car in Blender: parts, paints, lights |
| [tutorials/08-edit-a-car.md](tutorials/08-edit-a-car.md) | `car import`: an edited car (shape, UVs, a recoloured paint) from Blender into a mod as a b3patch, seeing it in the game, the limits |
| [tutorials/09-car-mods-in-blender.md](tutorials/09-car-mods-in-blender.md) | the Blender add-on B3 Car Tools: a car imported from your disc, checked and exported as a mod from Blender's menus; its messages |
| [tutorials/10-edit-a-track.md](tutorials/10-edit-a-track.md) | `track export --section` / `track check` / `track import`: a route section's vertex colours, shape and a texture into a mod as b3patches, seeing it in the game, what stays the disc's (collision, AI paths) |
| [b3modtool.md](b3modtool.md) | every b3modtool command, what can be imported today, the round-trip census |
| [b3patch.md](b3patch.md) | the `.b3patch` delta format: how a mod ships a change without the game's file |
| [file-formats/index.md](file-formats/index.md) | every file on the disc, what it holds and how far it can be modded |
| [file-formats/txd.md](file-formats/txd.md) | texture dictionaries and the PS2 rasters inside every file |
| [file-formats/strings.md](file-formats/strings.md) | the string tables, their limits (ids, size, characters the font has) |
| [file-formats/car.md](file-formats/car.md), [track.md](file-formats/track.md), [gamedata-bgd.md](file-formats/gamedata-bgd.md) | cars, routes and their game data |
| [file-formats/audio.md](file-formats/audio.md), [movies.md](file-formats/movies.md) | sound streams and banks, the movies |
| [file-formats/vdb.md](file-formats/vdb.md), [tuning-registry.md](file-formats/tuning-registry.md) | the tuning defaults and every tuning variable |
| [file-formats/kfs.md](file-formats/kfs.md), [bin-data.md](file-formats/bin-data.md), [lists.md](file-formats/lists.md), [system.md](file-formats/system.md) | the files not decoded yet, and the system files |
| [DESIGN.md](DESIGN.md) | the mod kit's plan: the loader, the SDK, the tools, the tuning console, phases and checks |

Example mods: a data mod (no game data in it; you make its file from your disc),
[examples/mods/hello-texture](../../examples/mods/hello-texture/README.md); two code mods,
[examples/mods/hello-overlay](../../examples/mods/hello-overlay) and [examples/mods/menu-label-setting](../../examples/mods/menu-label-setting).

## Rules for sharing

Make mods from your own copy of the game, and share only what you made: a `.b3patch`, the PNGs you painted, the
lines of text you wrote; never the game's own files. `b3modtool verify --game <disc>` checks a mod for copies of game
files before you share it ([getting-started.md](getting-started.md#please-share-mods-the-right-way)).
