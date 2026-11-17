# File formats: index

What every file on the Burnout 3 disc (US, SLUS-21050) holds, which page documents it, and how far a modder can go
with it today. The pages are written for people who have never looked inside these files. They give byte layouts as
tables, the code that reads each file, and the traps.

Any file except `SLUS_210.50` and `SYSTEM.CNF` can be replaced whole by a data mod: the loader's overlay serves a
mod's file in place of the disc's ([getting-started.md](../getting-started.md)). The status column says how far you
can go beyond that, i.e. whether you can make a new file of that kind.

Counts and sizes on these pages come from a copy of the US disc. Other versions of the game may differ. No page copies
game data: the few header words quoted are magic numbers and counts.

## Evidence tags

Each fact on these pages carries a tag saying where it comes from:

| Tag | Meaning |
|---|---|
| **[C 0xADDR]** | seen in the game's code, in the decomp (`burnout3-retail-decomp`), at that function address. "matched" units are byte-exact C; "nonmatching" ones are drafts, so a fact resting only on one is at most [L] unless the data agrees |
| **[D]** | observed in the disc's files, in every file of the kind unless the text names fewer ("all 37 files") |
| **[P]** | what the port's own decoder relies on (its source file is named); it decodes every disc file of the kind |
| **[L]** | likely: follows from the above but not checked directly |
| **[G]** | a guess. Treat it as one |

## Mod status

| Status | Meaning |
|---|---|
| **importable** | a tool turns the files into editable files and back, and `b3modtool roundtrip` shows that every such file on the disc comes back byte for byte when nothing is edited |
| **export only** | a port tool exports the content (PNG, OBJ, WAV, text) for viewing; no import yet |
| **decoded** | the port reads the format with its own decoder (for its renderer or audio), but has no export tool |
| **layout known** | documented well enough to edit by hand or with a script; no tool yet |
| **partly known** | the container and some fields are known, the rest is not |
| **not a target** | system files: the port does not use them, or must not be changed |

The asset tool is `b3modtool` (mod kit part 3, [DESIGN.md](../DESIGN.md) §3; its manual is
[b3modtool.md](../b3modtool.md)). `b3assetdump` (`tools/render/b3assetdump.cpp`) is the port's older viewer and
exporter.

## Pages

| Page | Formats |
|---|---|
| [txd.md](txd.md) | texture dictionaries (`.TXD`) and the PS2 rasters inside every other file: GS uploads, swizzles, CLUTs, mipmaps |
| [strings.md](strings.md) | the string tables `GLOBALUS.BIN`, `HEADUS.BIN` |
| [car.md](car.md) | vehicle models `.BGV` (player cars), `.BTV` (traffic) |
| [track.md](track.md) | routes: `STATIC.DAT`, `STREAMED.DAT`, `ENVIRO.DAT` (summary of [docs/track-format.md](../../track-format.md)) |
| [gamedata-bgd.md](gamedata-bgd.md) | `GAMEDATA.BGD`: start grid, triggers, traffic and AI lanes, Crash mode objects |
| [audio.md](audio.md) | RWS streams, AWD wave banks (`.AWD`, `.HWD`, `.LWD`), PS-ADPCM |
| [movies.md](movies.md) | the `.M2V` movies |
| [vdb.md](vdb.md) | `VDB.XML`, the tuning defaults (binary), and the hash that keys them |
| [tuning-registry.md](tuning-registry.md) | every tuning variable the game registers (705 registrations): name, group, type, range |
| [kfs.md](kfs.md) | `INTRO.KFS`, `AFTER.KFS`: Crash mode camera keyframes |
| [bin-data.md](bin-data.md) | `STAGEHED.BIN`, `LOADSCRN.BIN`, `PRGDATA.BIN` |
| [lists.md](lists.md) | `VLIST.BIN`, `TLIST.BIN`: the car and route lists |
| [system.md](system.md) | the program, IOP modules, the network set-up program, the demo, icons |

## Every file on the disc

775 files, 2.7 GB. `<area>` is `US`, `EU` or `AS`; `<route>` is a course and variant such as `C1_V1`; `<class>` is a
car class folder (`COMP`, `CUPE`, `HEVY`, `HSPC`, `MSCL`, `SPRT`, `SUPR`, `TSPC`).

### The disc root

| Path | Count | What it holds | Page | Mod status |
|---|---|---|---|---|
| `SLUS_210.50` | 1 | the PS2 program. The port takes only its data image and runs the game's code natively | [system.md](system.md) | not a target (not replaceable) |
| `SYSTEM.CNF` | 1 | the PS2 boot file | [system.md](system.md) | not a target (not replaceable) |
| `CNF/` | 2 | memory card icons of the network configuration | [system.md](system.md) | not a target |
| `IOP/`, `IOP/NETWORK/` | 22 (18 `.IRX`, 2 `.IMG`, 2 icons), 14 | IOP modules (`.IRX`), IOP images (`.IMG`), icons. All replaced by native code; the port reads only the reverb presets in `LIBSD.IRX` | [system.md](system.md) | not a target |
| `NETGUI/` | 30 | Sony's network set-up program | [system.md](system.md) | not a target |
| `NFSUNDER/` | 9 | a playable demo of another game (290 MB) | [system.md](system.md) | not a target |

### DATA/

| Path | Count | What it holds | Page | Mod status |
|---|---|---|---|---|
| `DATA/GLOBAL.TXD`, `DATA/FRONTEND.TXD` | 2 | texture dictionaries: HUD and race graphics (194 rasters), menus and car pictures (221) | [txd.md](txd.md) | **importable** (`b3modtool txd`: 415 of 415 rasters, both files identical) |
| `DATA/GLOBALUS.BIN`, `DATA/HEADUS.BIN` | 2 | the text: 4,006 and 11 UTF-16 strings | [strings.md](strings.md) | **importable** (`b3modtool strings`: 4,017 of 4,017 strings) |
| `DATA/VDB.XML` | 1 | the tuning defaults: 7,525 values keyed by hash (binary, not XML) | [vdb.md](vdb.md) | **importable** (`b3modtool vdb`: 7,525 of 7,525 values, the file identical); changes gameplay |
| `DATA/STAGEHED.BIN` | 1 | one record per stage (377): route, event, opponents | [bin-data.md](bin-data.md) | partly known; changes gameplay |
| `DATA/PRGDATA.BIN` | 1 | progression: World Tour series and events, Crash mode junctions, unlocks | [bin-data.md](bin-data.md) | partly known; changes gameplay |
| `DATA/LOADSCRN.BIN` | 1 | an archive of 40 loading-screen texture dictionaries | [bin-data.md](bin-data.md), [txd.md](txd.md) | archive known; rasters written back by the census (report only), no import |
| `DATA/INTRO.KFS`, `DATA/AFTER.KFS` | 2 | Crash mode camera keyframes, 100 sets each | [kfs.md](kfs.md) | partly known |
| `DATA/INTRO2.M2V` | 1 | **not a movie**: the online service's authentication bundle (5 parts named by GtID) | [movies.md](movies.md) §5 | not a target |
| `DATA/SAVEICON.ICN`, `DATA/EALOGIN.ICO` | 2 | PS2 3D icons (memory card, EA login) | [system.md](system.md) | replace whole only; cosmetic |

### PVEH/: vehicles

| Path | Count | What it holds | Page | Mod status |
|---|---|---|---|---|
| `PVEH/VLIST.BIN` | 1 | the car list: 107 entries (GtID, driveable flag, boost bar size) | [lists.md](lists.md) | layout known; changes gameplay |
| `PVEH/<class>/CAR<n>.BGV` | 67 | player cars: body, crash parts, wheels, texture atlas and paint palettes, lights, collision hull | [car.md](car.md) | export only (`b3modtool car export`: glTF 2.0 or OBJ, every LOD, part, paint and light; 67 of 67 valid); the hull is gameplay data |
| `PVEH/<class>/CAR<n>.BTV` | 40 | traffic and other non-player vehicles | [car.md](car.md) | export only (`b3modtool car export` by path) |
| `PVEH/<class>/CAR<n>.HWD`, `.LWD` | 67 + 67 | each player car's engine sounds (engine and exhaust at fixed RPMs, gear sounds): ordinary wave banks, high and low detail | [audio.md](audio.md) | **importable** (`b3modtool audio`: all 174 banks identical through WAV) |

### SOUND/

| Path | Count | What it holds | Page | Mod status |
|---|---|---|---|---|
| `SOUND/*.AWD` | 22 | wave banks: shared effects, one per game mode, crashes, the front end, the ten radio `IDENT` banks | [audio.md](audio.md) | **importable** (`b3modtool audio`: all 174 banks identical through WAV) |

### OVID/: movies

| Path | Count | What it holds | Page | Mod status |
|---|---|---|---|---|
| `OVID/*.M2V` | 61 | the movies (MPEG-2 video, no sound): intro and logos, trailers, unlock and end-of-tour movies, credits, award movies. `TITLES25.M2V` (PAL) is never opened | [movies.md](movies.md) | decoded (the port's MPEG-2 decoder); standard MPEG-2, so ordinary video tools make replacements within the limits in movies.md §4 |
| `OVID/MOVIE.RWS` | 1 | the movies' sound: 34 segments, picked per movie by a table in the program | [audio.md](audio.md), [movies.md](movies.md) | **importable** (`b3modtool audio`) |

### TRACKS/

| Path | Count | What it holds | Page | Mod status |
|---|---|---|---|---|
| `TRACKS/TLIST.BIN` | 1 | the route list: 36 entries (GtID, point-to-point and right-hand-drive flags) | [lists.md](lists.md) | layout known; changes gameplay |
| `TRACKS/<area>/<route>/STATIC.DAT` | 37 | the route's resident data: materials, its 9,240 textures in all, backdrops, models, prop placements, the unit table | [track.md](track.md) | export only (`b3modtool track export`: glTF 2.0 or OBJ by section, 37 of 37 routes valid); no import |
| `TRACKS/<area>/<route>/STREAMED.DAT` | 37 | the road and scenery in streamed units, with LOD and collision | [track.md](track.md) | export only (`track export`: the units' meshes; not the collision trees); the collision is gameplay data |
| `TRACKS/<area>/<route>/ENVIRO.DAT` | 37 | sky, sun, fog, specular settings and four sky textures | [track.md](track.md) | decoded (`b3envdump`, `track export`: the sky textures as PNG, the settings in its sidecar); no import |
| `TRACKS/<area>/<route>/GAMEDATA.BGD` | 36 | start grid, start / finish lines and checkpoints, traffic and AI lanes, Crash mode pickups and ramps | [gamedata-bgd.md](gamedata-bgd.md) | partly known (the port reads the Crash mode objects to draw them); changes gameplay |
| `TRACKS/<area>/<route>/SOUND.AWD` | 18 | the route's sound bank (in the `_V1` folders; the reverse routes use their forward route's) | [audio.md](audio.md) | **importable** (`b3modtool audio`) |
| `TRACKS/<area>/<route>/MUSIC.RWS` | 36 | 124 bytes: an empty wave-dictionary chunk the program never opens | [audio.md](audio.md) §5 | unused |
| `TRACKS/<area>/<route>/CRASH1..3.RWS`, `E_DJRACE.RWS` | 18 x 4 | the route's own crash sounds and the DJ's lines about the route | [audio.md](audio.md) | **importable** (`b3modtool audio`) |
| `TRACKS/*.RWS` | 35 | 20 `CRASH<n>.RWS` (crash sounds, normal and slow motion), 12 `E_DJ*.RWS` (the radio DJ), `_EATRAX0/1.RWS` (the EA Trax songs, 22 + 22), `_FEMAIN.RWS` (the menu music) | [audio.md](audio.md) | **importable** (`b3modtool audio`: all 108 streams identical through WAV); for music the port's custom soundtrack folder needs no file change |
| `TRACKS/*.M2V` | 48 | the menu background `_FEMAIN.M2V` and 47 small route previews (`STATIC.M2V` among them) | [movies.md](movies.md) | decoded; replaceable within movies.md §4 |

`TRACKS/US/C5_V1` has only STATIC / STREAMED / ENVIRO.DAT, and its STREAMED.DAT is empty [D].

## Helpers

Small scripts under `tools/modding/` check these layouts on your own disc. They contain no game data and print counts
and structure. Run them with `python3 -I <script> <disc> ...`.

| Script | Checks |
|---|---|
| `vdb_read.py` | VDB.XML: header, sorted records, blocks; with `--decomp`, names the records ([vdb.md](vdb.md)) |
| `tuning_registry.py` | builds the [tuning registry](tuning-registry.md) table from the decomp |
| `asset_raster_census.py` | rasters by file kind: formats, CLUTs, mip levels, palettes ([txd.md](txd.md)) |
| `unk_dump.py` | KFS, STAGEHED, LOADSCRN, PRGDATA, GAMEDATA.BGD, VLIST / TLIST |
| `audio_info.py`, `movie_info.py` | RWS / AWD headers, M2V headers ([audio.md](audio.md), [movies.md](movies.md)) |

Output that lists the game's values (`vdb_read.py --dump`, `tuning_registry.py --values`) is for your own use: never
commit or share it.
