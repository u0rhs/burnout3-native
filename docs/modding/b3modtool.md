# b3modtool

The mod kit's asset tool (mod kit parts 3a, 3b and 3c, [DESIGN.md](DESIGN.md) §3): it lists and
copies the game's files, turns textures into PNG and back, string tables and the tuning defaults (`VDB.XML`) into text
and back, sounds and music into WAV and back, exports cars and routes as glTF 2.0 or OBJ models and imports the edited
glTF back, makes and applies
`b3patch` deltas, and packs and checks mods. It reads your own disc (a folder of its files or the image itself) and writes only the output paths you give
it.

Build: `make modtool` (Linux, macOS) gives `build/b3modtool`; the Windows build (`tools/render/build_win.sh`, also run
by `tools/native/build_win.sh RENDER=1`) gives `b3modtool.exe`. Code: `tools/modding/b3modtool.cpp` (the commands),
`src/b3modtool/` and `include/b3modtool/modtool.h` (the formats; the model export `include/b3modtool/export3d.h`,
JSON and glTF `include/b3modtool/gltf.h`); tests: `tests/test_modtool.cpp` (`make test`).

In the examples `<disc>` is your disc: a folder (`../game`) or an image (`../burnout3.iso`; a `.bin` or `.chd` too). A
file argument may also be a path inside an image: `../burnout3.iso/DATA/GLOBAL.TXD`.

## What can be changed today

A format is **importable** only when `b3modtool roundtrip` shows that export followed by import, with nothing edited,
gives back every such file on the disc byte for byte. Until then it is export only.

| Files | Command | State | Census on the US disc (SLUS-21050) |
|---|---|---|---|
| Texture dictionaries: `DATA/GLOBAL.TXD`, `DATA/FRONTEND.TXD` | `txd` | **importable** | 2 of 2 files, 415 of 415 rasters identical |
| String tables: `DATA/GLOBALUS.BIN`, `DATA/HEADUS.BIN` | `strings` | **importable** | 2 of 2 files, 4017 of 4017 strings identical |
| Any file, as a delta against the disc's | `diff` / `apply` | yes | (apply(diff) is checked against the target's hash) |
| Textures inside tracks (`STATIC.DAT`, `ENVIRO.DAT`) | `track export` / `track import` (STATIC.DAT's; ENVIRO.DAT's export only); the cars' paint atlases: `car import` (below) | **importable** through the route's PNGs | the write-back: 9535 of 9535 rasters (`roundtrip <disc> rasters`); 37 of 37 routes' largest texture recoloured back exactly (`build/track_import_check`) |
| Tuning defaults: `DATA/VDB.XML` | `vdb` | **importable** | 1 of 1 file, 7525 of 7525 values identical |
| Audio streams: `TRACKS/*.RWS`, `TRACKS/<area>/<route>_V1/*.RWS`, `OVID/MOVIE.RWS` | `audio` | **importable** | 108 of 108 files, 1016 of 1016 segment x sub-stream items identical through WAV |
| Wave banks: `SOUND/*.AWD`, the routes' `SOUND.AWD`, the cars' `.HWD` / `.LWD` | `audio` | **importable** | 174 of 174 files, 1575 of 1575 waves identical through WAV |
| Car models: `PVEH/<class>/CARn.BGV` (and the traffic `.BTV`) | `car export` / `car import` | **importable** (glTF 2.0 / GLB in; OBJ is export only): meshes, texture coordinates, triangles, the paint pictures | export: 67 of 67 player cars, 268 of 268 LODs valid with the renderer's counts; import: 67 of 67 cars byte-identical through export -> import (`build/model_import_check`) |
| Routes: `STATIC.DAT`, `STREAMED.DAT`, `ENVIRO.DAT` | `track export` / `track import` | **importable, the visual meshes** (glTF 2.0 / GLB in; OBJ is export only): the sections, backdrops, water and instance models (vertices, UVs, colours, triangles), textures, material swaps; the collision, the AI paths (`GAMEDATA.BGD`), the placements and `ENVIRO.DAT` stay the disc's | export: 37 of 37 routes (2,838 sections) valid with the renderer's triangle counts; import: 37 of 37 routes byte-identical through export -> import, 140,896 of 140,896 meshes' packets written back byte for byte (`build/track_import_check`) |

Not decoded at all yet: KFS, `STAGEHED.BIN`, `LOADSCRN.BIN`, `PRGDATA.BIN`, `GAMEDATA.BGD`, the movies (`.M2V`).

## Commands

### list

```
b3modtool list <disc> [pattern]
```

Every file on the disc with its size and what it is (by name and first bytes: `txd`, `strings`, `track`, `car`,
`rws audio stream`, ...). A pattern narrows the list; patterns ignore case, `*` matches within one folder, `**` across
folders, `?` one character; a pattern without `/` is matched against the file name in any folder.

```
$ b3modtool list ../game '*.TXD'
     8517568  txd                DATA/FRONTEND.TXD
     1146112  txd                DATA/GLOBAL.TXD
2 files, 9663680 bytes
$ b3modtool list ../game 'TRACKS/**/STATIC.DAT'
```

### extract

```
b3modtool extract <disc> <pattern> <dir>
```

Copies the matching files to `<dir>` at their disc paths (from a folder or from inside an image). The copies are the
game's files: for editing on your PC, never for sharing.

```
$ b3modtool extract ../burnout3.iso 'DATA/*.TXD' work
extract: 2 files, 9663680 bytes into work (the game's files: for editing on this PC, never for sharing)
```

### txd list / export / import

```
b3modtool txd list <file.TXD>
b3modtool txd export <file.TXD> <dir>
b3modtool txd import <original.TXD> <dir> <new.TXD> [--keep-palette] [--keep-mips]
```

`list` shows each raster: index, name, size, storage format (`T8` / `T4`: 256 / 16 colours with a palette, the CLUT;
`CT32` / `CT24` / `CT16`: direct colour), mip levels, palettes.

`export` writes one PNG per raster and level:

- `NNN_<name>.png`: level 0 of raster NNN (its place in the dictionary);
- `NNN_<name>.mip1.png`, `.mip2.png` ...: the stored smaller levels, when the raster has them;
- `txd.txt`: what each file is (size, format, offset).

`import` takes the original dictionary and the folder, and writes a new dictionary of the **same size and layout**:
only the rasters' image bytes change. The rules:

- A PNG must have its raster's size. Only the number at the start of the name matters (`012_anything.png` is raster
  12); a raster without a PNG in the folder stays as it is. Any PNG works (RGB, RGBA, grey, paletted; not interlaced).
- Pixels you did not change keep their stored values exactly.
- **Palette rasters (T8 / T4).** If every colour of the new level 0 is in the raster's CLUT, the CLUT stays and the
  pixels take those entries. If not, a new CLUT is built from level 0's colours: exact when there are at most 256
  (T8) / 16 (T4) of them, else reduced to that many (median cut). `--keep-palette` keeps the CLUT and gives new colours
  the nearest entry. Rasters with several palettes (the car-colour variants, e.g. `COMPCAR1` in FRONTEND.TXD) always
  keep their CLUTs: the other palettes are indexed the same way, so a new CLUT would break them. Such a raster is
  exported with its first palette.
- **Mip levels.** When level 0 changed and a level's PNG is still as exported (or missing), that level is rebuilt
  from the one above it (2x2 average); a level whose PNG you edited is taken as it is. `--keep-mips` leaves unedited
  levels as stored.
- **Alpha.** The PS2 stores alpha 0..0x80 (0x80 = opaque); the PNG has 0..255 (alpha 2 x stored, 255 for 0x80). A
  PNG alpha is stored as (a + 1) / 2, at most 0x80. CT16 has a 1-bit alpha: 128 and above is opaque.
- After writing, the raster is decoded again and checked against what was meant to be written; an import that cannot
  be written exactly stops with a message, and nothing is written.

```
$ b3modtool txd export ../game/DATA/FRONTEND.TXD work/fe
txd export: 222 files into work/fe (edit the PNGs at their size; then txd import)
  ... paint a new picture into work/fe/017_champion.png (256 x 256), in any image editor ...
$ b3modtool txd import ../game/DATA/FRONTEND.TXD work/fe work/FRONTEND.TXD
  note: champion: 65536 colours reduced to 256
txd import: 221 PNGs, 1 rasters changed, 1 with a new palette -> work/FRONTEND.TXD (8517568 bytes, the original's size)
$ b3modtool diff ../game/DATA/FRONTEND.TXD work/FRONTEND.TXD my-mod/patches/DATA/FRONTEND.TXD.b3patch
diff: ... 2703 ops, 8492084 bytes copied from the original, 25484 new; patch 37262 bytes
```

A whole TXD in a mod is mostly the game's bytes: share the change as a patch (`diff`, below), not the file (`pack`
refuses such a file without `--force`). To try it in the game on your own PC, put the new file into a mod's `data`
folder (`my-mod/data/DATA/FRONTEND.TXD`) and turn the mod on ([getting-started.md](getting-started.md)); the patch in
`my-mod/patches/` works the same way, applied by the game.

### strings export / import

```
b3modtool strings export <file.BIN> <out.txt>
b3modtool strings import <original.BIN> <in.txt> <new.BIN>
```

The game's string tables (`DATA/GLOBALUS.BIN`, the menus and messages; `DATA/HEADUS.BIN`) as UTF-8 text, one string
per line:

```
# b3modtool strings: GLOBALUS.BIN, 4006 strings
...
0 = Press START button to continue
2 = WHEN CRASHING HOLD THE R1 BUTTON FOR AFTERTOUCH
```

- `<id> = <text>`. Escapes: `\n` a new line, `\r`, `\t`, `\\` a backslash, `\uXXXX` any character by its UTF-16 code.
  Spaces at the end of a string are written ` ` (editors remove trailing spaces). `#` lines are comments.
- Import reads the original and replaces the strings of the lines the text has: **a text with only your changed
  lines is enough**; ids you leave out keep the game's text. Strings may get longer or shorter (the offsets are
  rebuilt). New ids may be added right after the last one (the game only shows ids its code asks for).
- Errors name the line: an unknown escape, text that is not UTF-8, an id given twice, a gap.

```
$ cat my-strings.txt
# my change
230 = MY EVENT
$ b3modtool strings import ../game/DATA/GLOBALUS.BIN my-strings.txt work/GLOBALUS.BIN
strings import: 1 strings changed -> work/GLOBALUS.BIN (214842 bytes)
```

### vdb export / import

```
b3modtool vdb export <VDB.XML> <out.txt> [--names <table>]
b3modtool vdb import <in.txt> <new VDB.XML> [--base <VDB.XML>] [--names <table>]
```

`DATA/VDB.XML` holds the defaults of the game's tuning variables (boost and score rules, AI, car physics, cameras,
sound mixing, rumble, bloom). It is binary despite its name ([file-formats/vdb.md](file-formats/vdb.md)). `export`
writes every record as a line of text; `import` builds the binary from the text.

```
# b3modtool vdb: VDB.XML: 7525 values (6601 named), 348 config files (343 named)
...
@version = 2
@pad = fdfdfdfd

# config files (the file records): 1 enabled, 0 disabled
@file ../Export/ValueDB/AI/defaults.cfg = 1
@file 0x30077DDD = 1
...
[../Export/ValueDB/Score.cfg]
Score/Boost/Air | Minimum Air Distance (Metres) = <value>
Boost Bar | Boost Rate (Boost Units per Second) = [<a>, <b>, <c>, <d>]
...
[]
0x<key> = 0x<raw word>
```

- A value is `<group> | <name> = <value>` under the `[<config file>]` it belongs to, with the names the game
  registers it under (the [tuning registry](file-formats/tuning-registry.md)). The record's key is the hash of name,
  group and file (vdb.md "The key"), so the three must be exactly as written: case, spaces and all.
- A value whose key no registered name matches is written `0x<key> = <value>` (924 on the US disc: names the decomp
  scan does not see; two of them are named arrays whose length in the file differs from the registration's, so they
  are written by key too).
- Values: a float has a `.` or an exponent (`25.0`, `1e-07`), an int has none (`-5`), a vector is `(x, y, z, w)`, an
  array `[a, b, ...]`, and `0x` with up to 8 hex digits is the raw 32-bit word (NaN, or a value whose type is not
  known: every unnamed word is written that way). For a named value the registry's type decides: `30` for a float
  is `30.0`; a vector must be given as a vector, an array with its registered length.
- A vector's fourth value (w) is 0xCDCDCDCD in every vector on the disc, written as the float `-431602080.0`: a fill
  value from Criterion's tool, copied by the game but not a real coordinate. Leave it as it is.
- `@version` (header +0x00), `@pad` (the bytes that align the first block, `fd` on the disc) and `@file` (a config
  file's enable record) keep the rest of the file; `@vectors = <n>` would set header +0x08 to something other than
  the number of vectors (never needed for the disc's file).
- Import sorts the records by signed key and lays out the blocks as the disc does (vdb.md "Layout"): the result is the
  disc's file byte for byte when nothing was edited. A text without `--base` must hold every record; with
  `--base <VDB.XML>` it changes or adds only the records it names (a text of your changed lines is enough). A record
  cannot be removed with `--base`.
- A key may be new (a name not in the file, or a new `0x<key>`): import adds it. The game's buffer holds 73,728 bytes
  (0x12000); import refuses a file larger than that (vdb.md "Size limit"; room for about 380 more records).
- `--names <table>` adds names from a table in the format of `src/b3modtool/vdb_names.inc` (`v <type> <count> <group>
  <name> <file>` and `f <file>` lines, tab-separated; `<vehicle>` / `<route>` in a file name are filled with every car
  and route id). The built-in table is generated by `tools/modding/tuning_registry.py --names-table --cxx` and holds
  names only, no game values.
- Errors name the line: a value of the wrong kind or length for its name, a value given twice, a named value outside
  a `[...]` section, a name with a non-ASCII character (the game's hash cannot take one).

```
$ cat my-boost.txt
[../Export/ValueDB/Score.cfg]
Boost Bar | Boost Rate (Boost Units per Second) = [48.0, 48.0, 48.0, 48.0]
$ b3modtool vdb import my-boost.txt work/VDB.XML --base ~/Games/Burnout3/DATA/VDB.XML
vdb import: 7525 values (1 set by the text, 0 added to the base), 348 config files -> work/VDB.XML (70648 bytes of at most 73728)
$ b3modtool diff ~/Games/Burnout3/DATA/VDB.XML work/VDB.XML my-mod/patches/DATA/VDB.XML.b3patch
diff: ... 3 ops, 70635 bytes copied from the original, 13 new; patch 75 bytes
```

A changed VDB.XML changes gameplay ([DESIGN.md](DESIGN.md) §6). A value only reaches a variable when the game
registers it (some at start, most per race), so the change shows from the next race.

`tools/modding/vdb_mod_check.sh [native_play]` checks this in the game: it exports your VDB.XML, puts every car's
follow camera twice as far behind (`Camera/Follow/ | Camera Offset`, z x 2: 67 values), imports it with `--base` into a
test mod under `build/` and runs an offscreen race_check with and without the mod. PASS: the mod's VDB.XML is served,
the event intro's shot (its own camera) is equal and the race shot 11500 shows the car from further back, with the
same score, position and speed on the HUD.

### audio list / export / import

```
b3modtool audio list <file>
b3modtool audio export <file> <dir>
b3modtool audio import <original> <dir> <new file>
```

The game's sound files ([file-formats/audio.md](file-formats/audio.md)): **streams** (`.RWS`: the EA Trax songs, the
radio DJ, the crash sounds, the movies' sound), cut into **segments** (one song, one DJ line) and **sub-streams** (the
crash files have two: normal speed and slow motion), and **wave banks** (`.AWD`, the cars' `.HWD` / `.LWD`) of short
**waves**. Everything is PS-ADPCM, the PS2's 4-bit format.

`list` shows each item; `export` writes one WAV per item (16-bit PCM at the item's rate: 32 kHz stereo for the
streams, the wave's own rate and mono for banks) and `audio.txt`:

- `NNN_<name>.wav`: segment or wave NNN (its number in the file, which is how the game asks for it);
- `NNN_<name>.sub<k>.wav`: sub-stream k of segment NNN, in files with more than one.

`import` takes the original file and the folder and writes a new file of the same structure:

- Only the number at the start of a WAV's name (and its `.sub<k>`) matters. An item without a WAV stays as it is.
- Any PCM WAV works (8, 16, 24 or 32-bit, or 32-bit float). Another rate is resampled (linear) to the item's rate; a
  mono WAV for a stereo item is copied to both channels, a stereo one for a mono wave mixed down. Use the item's own
  rate and channels for the best result.
- **Unedited audio keeps its bytes.** Each 16-byte PS-ADPCM frame of the original that decodes to exactly the WAV's
  samples is kept as it is, so a WAV you did not change gives back the original frames; frames that differ are
  encoded anew (the filter and shift with the least error for each frame of 28 samples, quantised against the
  decoder's own output).
- **What can change:** an item's audio and its **length**. A wave of another length moves the waves after it in the
  bank (each still starts on a 0x80-byte boundary, the bytes the original had after it are kept) and the bank's data
  size; a segment of another length becomes a whole number of blocks again and moves the segments after it; its size
  table, the data chunk and the file size follow. The rate, the channel count, the number and order of the items,
  their names, the block size and channel interleave, and the header stay as they are.
- **Loops and flags:** a wave keeps its frame flags: the loop start on its first frame where the original has it
  (the 774 looping waves on the disc loop from their start), the end flag (0x3) on its last frame; the wave's flags
  word and target format are kept. Stream frames keep their flag 0x2; the end of a segment's last block is filled with
  the silent frame streams pad with. A WAV's own loop points (`smpl`) are not read.
- **Sound memory** is 2 MB for every bank and stream ring loaded at once ([file-formats/audio.md](file-formats/audio.md)
  §7): import notes when a bank's wave data grows. Keep replacements about the original's length.

```
$ b3modtool audio export ~/Games/Burnout3/SOUND/FE.AWD work/fe
audio export: 51 WAV files and audio.txt into work/fe (the game's audio: for editing on this PC, never for sharing)
  ... edit work/fe/001_GTurn.wav in any audio editor ...
$ b3modtool audio import ~/Games/Burnout3/SOUND/FE.AWD work/fe work/FE.AWD
  note: the bank's wave data grows from 909312 to 913920 bytes: sound memory (2 MB for every bank and stream ring at once) may not hold it
audio import: 51 WAVs for 51 items: 50 unchanged (the original PS-ADPCM kept), 1 re-encoded (1 with a new length); 55974 frames kept, 885 encoded -> work/FE.AWD (922112 bytes)
```

For your own music in place of the EA Trax songs no file needs changing: PC SETTINGS > INTERFACE > MUSIC plays a folder
of your MP3 / FLAC / Ogg / WAV files.

### car list / export

```
b3modtool car list <disc>
b3modtool car export <disc> <car | all> <dir> [--lod N|all] [--format gltf|obj] [--paint K]
```

`car list` prints the 67 player cars in the game's order with the game's names (read from `DATA/GLOBALUS.BIN`, as
the launcher's showroom names them), then the traffic and other vehicles (`.BTV`) by path:

```
$ b3modtool car list ../game
  0  PVEH/COMP/CAR1.BGV   Compact      Compact Type 1
  ...
 54  PVEH/HEVY/CAR36.BGV  Special      Fire Truck
 ...
 66  PVEH/HEVY/CAR10.BGV  Heavyweight  Trash Truck
traffic and other vehicles (export them by path):
     PVEH/COMP/CAR11.BTV
     ...
67 player cars, 40 traffic vehicles
```

`car export` takes a car by its name (`"Compact Type 1"`), its number in that list, or its path (`PVEH/COMP/CAR1.BGV`,
`COMP/CAR1`, also a `.BTV`); `all` exports every player car, each into its own folder. It writes, for
`PVEH/COMP/CAR1.BGV`:

| File | What |
|---|---|
| `COMP_CAR1.gltf`, `COMP_CAR1.bin` | the model (glTF 2.0, JSON + one binary buffer) |
| `COMP_CAR1_paint<k>.png` | the car's texture atlas once per paint colour (one per CLUT, 1 to 8 per car) |
| `COMP_CAR1.b3car.json` | the sidecar: everything an import needs to rebuild the file (see "The export's files" below) |

```
$ b3modtool car export ../game "Compact Type 1" out/car1
PVEH/COMP/CAR1.BGV   Compact Type 1         11 files, 114 meshes, 310 nodes, 11824 vertices, 7320 triangles, 8 paints; bounds (-1.016 -0.314 -2.087) .. (1.016 1.122 2.064); glTF check ok
  note: not exported as geometry (kept in the file; an import keeps them byte for byte): the collision hull (+0x1060), the engine-sound structures (+0x16DC), the unknown areas (+0x14, +0x6C..+0xB7F, +0x16C0)
  note: damage: the game deforms the car at run time from the per-vertex influences (_B3_INFL_0 / _1: weight << 8 | zone << 2); the file has no damaged shapes, so there are no morph targets
  note: texture: level 0 of the atlas per paint; its 1 stored level(s) are rebuilt by an import
car export: 1 of 1 cars written into out/car1, 1 glTF checked valid (the game's data: for your own editing, never for sharing)
```

The glTF scene, named the game's way:

```
Compact Type 1                      the root (extras: the disc path, the sidecar's name)
  LOD0 .. LOD3                      the four levels of detail (LOD3 the highest; --lod N exports one)
    LOD3_L0                         the body
      LOD3_L0_e00 ...               one node and mesh per mesh entry of the list (its material and colour words in extras)
    LOD3_A0 .. LOD3_A5              the panels (bonnet, doors, boot ...), each at its panel frame
    LOD3_B0_w0 .. LOD3_B0_w3        the wheel, once per wheel frame (one mesh, shared)
    LOD3_B1_w0 ..., LOD3_B2_w0 ...  the blurred wheels the game draws above wheel speed 25 / 50: hide them to see the car as it stands
  extra_lists                       the two lists at +0x64 / +0x68 when they are in the list format (with all LODs only;
                                    their use is not known, the port's renderer does not draw them; on the US disc 64 and 65 of the 67 cars have them, and
                                    one +0x64 list is not a list: left out, with a note)
  lights                            empties at the light points: light_head_0, light_tail_0, light_brake_0, light_reverse_0,
                                    light_indicator_a_0 / _b_0, light_exhaust_0, and the other groups as light_g<k>_<i>
```

Materials: per paint `paint<k>_body` (opaque), `paint<k>_light` (the light meshes) and `paint<k>_glass_<FIX>` (blended,
opacity = the entry's colour byte x 2 / 255). The default is paint 0 (`--paint K` for another); a car with more than one
paint carries the others as **material variants** (glTF `KHR_materials_variants`, "paint 0" .. "paint 7"), which a
viewer or Blender can switch between. **Liveries**: the atlas is one 8-bit picture with one palette (CLUT) per paint, so
the paints are the same picture in other colours; editing a livery means editing the picture (`car import` re-indexes
it against every palette; a pure recolour of one paint changes only its palette).

`--format obj` writes `COMP_CAR1_lod<k>.obj` per LOD (one OBJ when one LOD is asked for) and `COMP_CAR1.mtl` with the
paint's PNG, every part placed at its frame (OBJ has no hierarchy), and the same sidecar. No variants, no custom
attributes: OBJ is for a quick look in any tool.

### car import

```
b3modtool car import <disc> <model.gltf | model.glb | export dir> <mod dir | new.BGV> [--sidecar <file.b3car.json>]
b3modtool car check <disc> <model.gltf | model.glb | export dir> [--sidecar <file.b3car.json>]
```

Puts an exported car, edited in Blender or any glTF tool, back into the game's file. It reads:

- the **model**: the export's `.gltf` (with its `.bin`), or a `.gltf` / `.glb` your modelling tool saved from it (an
  export folder as the argument means its own `<stem>.gltf`);
- the **sidecar** `<stem>.b3car.json` beside the model (or `--sidecar`): which file of your disc the export came from,
  and where the paint PNGs are;
- the **paint PNGs** `<stem>_paint<k>.png` beside the sidecar;
- your disc's own car file, checked against the sidecar's size and hash (an export made from another version of the
  file is refused: export again).

It writes, when the last argument is a folder, `<mod dir>/patches/PVEH/<class>/CARn.BGV.b3patch` (a b3patch against
your disc's file, ready to share: it holds only what you changed) and a `mod.ini` if the folder has none; when the last
argument ends in `.BGV` / `.BTV`, the whole new file (for trying it on your own PC: `data/PVEH/...` in a mod).
`car check` does the same import and its checks and writes nothing: its last line is the same summary, ending in
`ok, nothing written` (the Blender add-on's **Check** button, [tutorial 09](tutorials/09-car-mods-in-blender.md)).

```
$ b3modtool car import ../game out/car1/COMP_CAR1_big.gltf mods/big-compact
  note: 25 object(s) moved / turned / scaled (LOD3_L0_e00 .. LOD3_A5_e02): applied to their vertices (the parts' frames stay the file's)
  note: compact1: a recolour (each palette entry one colour): only the edited paints' CLUTs change
  note: mods/big-compact/mod.ini written (id big-compact): change the name, author and description before you share the mod
car import: PVEH/COMP/CAR1.BGV: 114 meshes (89 unchanged, 25 edited in place, 0 with new triangles, 0 not in the model: kept), 0 packets moved, 1 paints changed; 436224 -> 436224 bytes -> mods/big-compact/patches/PVEH/COMP/CAR1.BGV.b3patch
```

**What can change, and how it is written:**

| Edit | How the import writes it |
|---|---|
| An object moved, rotated or scaled as a whole (its node's matrix) | the file's own strips and batches with the matrix applied to the vertices; the file keeps its size |
| Vertices moved in edit mode, the triangles' order and vertex order kept (a script, or a tool that keeps them) | the same strips and batches, the new positions; the file keeps its size |
| Nothing changed but the order (a modelling tool's re-save reorders and splits vertices) | the file's own strips: the same bytes |
| An object of several meshes of one material (backdrops, signs, water) as Blender's glTF exporter writes it: one primitive per material, without the primitives' `extras` | cut back into the file's meshes by its triangles (each one a triangle of one of them, by its three positions in the mesh's own space), with a note: repainted or moved as a whole, it imports as any mesh does; with vertices moved or faces added its triangles are not the file's any more and it is refused as before (edit such an object in a tool that keeps the primitives' extras, or leave it out of the model) |
| Texture coordinates (UVs) | the same; stored as u, v x 4096 (0 .. 16: keep the UVs on the texture) |
| Normals | the stored bytes while the model's `NORMAL` still points the same way, else `NORMAL` quantised; none in the model: made from the faces |
| Vertices moved in edit mode in a tool that reorders them (Blender), triangles added, deleted, a mesh remodelled (vertex count, topology) | new strips (greedy, at most 52 vertices per VU batch, the winding kept); a packet that no longer fits its place moves into freed packet space or to the end of the file (the entry's offset and the size word follow) |
| A mesh's faces all deleted | one batch that draws nothing |
| A paint picture recoloured (any per-colour change: hue, levels, swapping channels) | only that paint's palette (CLUT) changes |
| A paint picture painted on (a stripe, a number, another picture of the same size) | all paints are re-quantised together: one 8-bit picture whose 256 entries hold a colour per paint |
| An object deleted from the model | that mesh stays as it is in the file (delete its faces instead) |

**Refused, with a message, nothing written:** a mesh that is not one of the car's (`LOD3_L0_e00` names: the import
changes the meshes the file has and cannot add objects; join a new part into one of them); a vertex position that is
not a number or beyond 1 km; texture coordinates outside 0 .. 16; a primitive that is not triangles or has no texture
coordinates; a paint picture of another size; a new
file over **466,944 bytes** (memory block 1, which a player car is loaded into; the largest car on the disc is exactly
that size, so a heavier model needs vertices removed elsewhere, e.g. in the lower LODs) or, for a traffic `.BTV`, larger
than the original. After writing, the import decodes every mesh of the new file again and checks it is what was meant.

**What stays the file's:** the LOD blocks, lists and entries (their count, materials, glass and colour words), the part
frames (panels and wheels pivot where they did: an object moved in Blender has the move applied to its vertices, around
the old pivot), the collision hull (the car still collides with the old shape: change it and gameplay changes), the
lights (moving the `light_*` empties does nothing), the engine-sound structures and the unknown areas. A wheel mesh
is shared by all wheels: edit it once (the first wheel's object is read). Damage: a new vertex without
`_B3_INFL_0` / `_B3_INFL_1` takes the influences of the nearest original vertex, so it bends with what was there
(nearest by the mesh's own positions, before its object's move; of equally near ones the one with the same texture
coordinates). So does a vertex whose influences are not ones the file can hold (not whole numbers, or a damage zone the
file does not use: a tool that mixes attributes, e.g. on a subdivided face); the import says how many.
`_B3_NRM_X` / `_Y` / `_Z` are used while they hold whole bytes that point where `NORMAL` does; when most of a mesh's do
not (a tool changed the attributes), they are left out for that mesh and the normals come from `NORMAL`.

**Blender.** The add-on B3 Car Tools (`tools/blender/b3_car_tools`, `make blender-addon`; [tutorial
09](tutorials/09-car-mods-in-blender.md)) does all of this from Blender's menus. By hand: import the export's `.gltf`
(tutorial 07), edit, then **File > Export > glTF 2.0**: either format (`.glb` or `.gltf + .bin`), **Include > Custom
Properties** and **Data > Mesh > Attributes** on (they carry `extras` and the `_B3_*` attributes back; without them the
names still match the meshes, and normals and influences are rebuilt as the table says), **Apply Modifiers** as you
like, Draco compression off, and keep the object names. Save it beside the sidecar (in the export folder), or name the
sidecar with `--sidecar`. Tutorial 08 goes through it.

What Blender's glTF importer and exporter do to a car (read from their source, glTF-Blender-IO for Blender 4.2 LTS,
and run in Blender 4.2.23 LTS by `tools/modding/blender_check.sh`; `model_import_check` models it, below) and how the
import copes:

| Blender | The import |
|---|---|
| imports only the vertices the triangles use (the strips' restart vertices are dropped) | the same triangles give the file's own stream, whatever the vertex count |
| exports the vertices in its own order (sorted by the bytes of a per-corner record) and may split a vertex whose corners differ | the triangle set is compared, not the order |
| keeps `NORMAL` as custom split normals; exports them rounded to 4 decimals | the stored normal bytes stay while `NORMAL` points within 2.5 degrees of them |
| texture coordinates as v' = 1 - v and back | exact for every stored value (k / 4096 below 16) |
| node matrices as location / rotation / scale (decomposed in single precision, twice) | a part whose transform is its frame to 1e-5 counts as not moved |
| keeps a `_` attribute only in a form it has: float SCALAR / VEC2 / VEC3 / VEC4 kept; an unsigned byte or short VEC4 becomes a byte colour (its values clamped to 0 / 255); an unsigned short VEC2 is dropped | the export writes every stored field as floats, so they come back exactly |
| pairs a mesh's `_` attributes with their data in the order of a Python set, which the string hashing of each Blender run decides (`io_scene_gltf2/blender/imp/gltf2_blender_mesh.py`): attributes of different widths in one mesh make the import fail at random (`ValueError: all the input array dimensions ... must match exactly`, in `np.concatenate`) | the export writes one float SCALAR per component (`_B3_NRM_X`, `_B3_INFL_0`, ...: all of one form, so every order gives each attribute its own values); exports made before that (`_B3_NRM` VEC4, `_B3_INFL` VEC2) still import with b3modtool, but Blender's importer fails on them in most runs: export the car again |
| writes Draco-compressed meshes only when asked | Draco is not read: leave it off |

Before the floats (exports of b3modtool before the add-on), the influences did not survive Blender and the influences
of a whole object scaled in object mode were looked up by the moved positions: such a car was restripped (its file
grew) with the wrong damage zones. Both are fixed (the census's "through Blender" below).

**The census** (`make build/model_import_check`, then `build/model_import_check <disc> [out dir] [car ...]`;
`tools/modding/model_import_check.cpp`), every player car in memory:

- **identity**: export -> import with nothing edited gives the disc's file byte for byte: **67 of 67 cars**;
- **re-save**: every mesh's vertices and triangles shuffled (what a modelling tool's save does to an unedited model):
  the disc's file byte for byte, 67 of 67;
- **restrip** (every mesh's strips rebuilt from its triangles, the stress case of new topology everywhere):
  decode-identical, 67 of 67 cars (7,022 of 7,022 entries the same triangle sets with every stored field, 268 of 268
  LODs of the renderer's decode the same drawn triangles and bounds); the strips hold 4.7 % more vertices than the
  disc's (815,368 against 778,656), the files grow by at most 4.3 %, and 57 of the 67 stay within the size limit;
- **topology**: a quad added to LOD 3's first body mesh: decode-identical (that mesh the new triangle set, every other
  entry unchanged) for 67 of 67, within the limit for 66 (Coupe DX is the disc's largest file, at the limit already);
- **edit**: LOD 3's body scaled 1.3x gives exactly those positions in the same size, and paint 0 with red and blue
  swapped decodes to that picture with the other paints unchanged: 67 of 67 and 67 of 67;
- **through Blender** (the export put through what Blender's glTF importer and exporter do, as the table above says:
  the unused vertices dropped, Blender's vertex order, normals rounded, the node transforms decomposed, the `_`
  attributes kept or lost by their type; written as a GLB): unedited, the disc's file byte for byte, **67 of 67**;
  LOD 3's body scaled 1.3x in object mode, those positions (to 5e-7 m) in the same size, every mesh edited in place,
  67 of 67; an export in the layout before the per-component attributes: the disc's file by the import, and refused
  by the model of Blender's importer (attributes of different widths), 67 of 67. In a real Blender (4.2.23 LTS,
  headless): `tools/modding/blender_check.sh`, below.

In the game: `tools/modding/car_mod_check.sh` (every player car's LOD 3 body and panels 1.3x about the car's origin
and its paints red <-> blue, imported as a patch mod; an offscreen race with and without it): **PASS** (race_check PASS with and without the mod; the game read the race's five cars, `PVEH/COMP/CAR1, 4, 7, 8, 10.BGV`, from the mod's patches, the renderer built the same set; race shots 10700 and 11500 changed in 51 % and 17 % of their pixels: the player's car and the rivals 1.3x and blue).

In a real Blender (4.2.23 LTS, headless; `BLENDER=<path to blender> tools/modding/blender_check.sh`): **PASS**. The
add-on's zip validates as an extension, installs and enables; Compact Type 1, Classic Hotrod and Fire Truck imported
with File > Import > Burnout 3 Car into one scene, checked, and exported unedited to car files: the disc's bytes, 3 of
3; Compact Type 1's LOD 3 body and panels scaled 1.3x in object mode and exported as a mod: 25 meshes edited in place,
the same size, verify OK; race_check PASS with it, the game read `PVEH/COMP/CAR1.BGV` from the mod, the race shots
changed. Run six times (`PYTHONHASHSEED` 1 .. 5 and unset), every run the same. With the exports before the
per-component attributes the same importer failed in 4 of 6 runs (seeds 2 .. 5).

### track list / export

```
b3modtool track list <disc>
b3modtool track export <disc> <route | all> <dir> [--section N|all] [--format gltf|obj]
```

`track list` prints the 37 routes (`TRACKS/<area>/<route>`) and their number of sections. A **section** is one streamed
unit of the route (the game streams them in as the car drives; docs/track-format.md §2):

```
$ b3modtool track list ../game
TRACKS/AS/C1_V1      49 sections
...
TRACKS/US/C5_V1       0 sections (STREAMED.DAT is empty)
...
37 routes
```

(`US/C5_V1` has an empty `STREAMED.DAT`: its export holds its backdrops, water and props only.)

`track export` takes a route as `US/C1_V1` (or `TRACKS/US/C1_V1`, or `all`). By default the whole route; `--section N`
only that unit, with its instances and props (and without the route-wide backdrops and water). It writes `US_C1_V1.gltf`
/ `.bin` (or `US_C1_V1_s010.*` for a section), one PNG per texture of `STATIC.DAT` (`<NNN>_<name>.png`, NNN its index in
the texture table, as `txd export` names them), the four sky textures of `ENVIRO.DAT` (`US_C1_V1_sky_*.png`), the sidecar
`US_C1_V1.b3track.json` and `US_C1_V1_report.txt`:

```
$ b3modtool track export ../game US/C1_V1 out/us_c1
TRACKS/US/C1_V1    205 files, 176 meshes, 1959 nodes, 3183 primitives, 301051 vertices, 180881 triangles, 197 textures; bounds (-7217.199 -165.144 -7801.129) .. (10080.747 1496.996 13648.699); 3.4 s; glTF check ok
track export: 1 of 1 routes written into out/us_c1, 1 glTF checked valid; each folder's *_report.txt says what is not exported (the game's data: for your own editing, never for sharing)
```

The scene:

```
US_C1_V1
  section_000 .. section_058         the unit's high-detail meshes: one glTF mesh, one primitive per source mesh
    section_010_instance12_0 ...      the instance models placed in the unit (a shared mesh, the placement matrix on the node)
    section_010_prop3_0 ...           the props listed by the unit (STATIC.DAT +0x48 placements)
  section_000_lod ..                  the unit's LOD meshes (the far view)
  backdrop_0 .., backdrop+28_0 ..     the backdrops (+0x24) and the route's signs (+0x28)
  water_0 .., water+30_0 ..           the water surfaces (+0x2C, +0x30)
```

Materials: one per `STATIC.DAT` material (`m<index>_<texture>`; `_2s` when it is drawn two-sided), alpha mode from the
material's GS state (opaque, alpha test with its reference as `alphaCutoff`, blended with its fixed alpha); extras: the
material's type, flags, GS TEST / ALPHA words. Vertex colours (`COLOR_0`) are the stored GS colours as they look on
screen (x 4 for RGB: 0x80 is 1.0 to the GS and the frame is doubled when it is shown; x 2 for alpha), clamped to 1.0;
the stored bytes are kept in `_B3_COLOR_R` .. `_B3_COLOR_A`. Instance models have no vertex colour: their colour is the placement's
(node extras `tint_w`). Animated materials show their first frame; the frames are listed in the sidecar.

What is **not exported** (each export's report says so with its counts): the line meshes (material type 3: decoded,
but glTF has no line width), the collision trees and the irradiance per collision vertex (STREAMED.DAT LOD blocks
+0x90 / +0x94: the port does not decode the tree's layout), the sky dome (generated by code: only its textures),
the water reflections (mirrored at run time), `GAMEDATA.BGD` (start lines, AI and traffic paths).

`--format obj` writes one `US_C1_V1.obj` / `.mtl` with every mesh placed (OBJ vertex colours as 0x80 = 1.0).

### track import

```
b3modtool track import <disc> <model.gltf | model.glb | export dir> <mod dir | out.b3mod> [--sidecar <file.b3track.json>] [--restrip]
b3modtool track check <disc> <model.gltf | model.glb | export dir> [--sidecar <file.b3track.json>]
```

Puts an exported route, edited in Blender or any glTF tool, back into the route's files. It reads:

- the **model**: the export's `.gltf` (with its `.bin`), or a `.gltf` / `.glb` a modelling tool saved from it (an
  export folder as the argument means its own `<stem>.gltf`);
- the **sidecar** `<stem>.b3track.json` beside the model (or `--sidecar`): which route of your disc the export came
  from, the materials and the texture PNGs' names;
- the **texture PNGs** `<NNN>_<name>.png` beside the sidecar (one per texture of `STATIC.DAT`; an edited one goes back
  into its raster, a missing one stays as it is);
- your disc's own `STATIC.DAT` and `STREAMED.DAT` of that route, checked against the sidecar's sizes and hashes (an
  export made from other files is refused: export again).

It writes, when the last argument is a folder, `<mod dir>/patches/TRACKS/<area>/<route>/STREAMED.DAT.b3patch` and / or
`STATIC.DAT.b3patch` (b3patches against your disc's files: only what you changed, ready to share) and a `mod.ini` if the
folder has none; when it ends in `.b3mod`, that folder (the name without `.b3mod`) and then the packed mod (`verify`
and `pack` as below). `track check` does the same import and its checks and writes nothing. An import that changes
nothing writes no patch.

```
$ b3modtool track export ../game US/C1_V1 c1
  (section_029 moved 2 m up in a modelling tool, the export saved again as c1/US_C1_V1.gltf)
$ b3modtool track import ../game c1 mods/high-road
  note: 27 object(s) moved / turned / scaled (section_029 primitive 0 .. section_029 primitive 26): applied to their vertices
  note: 23 placed instances / props under a moved node: they stay where the file places them (the import moves a section's meshes, not its placements)
  note: mods/high-road/mod.ini written (id high-road): change the name, author and description before you share the mod
track import: TRACKS/US/C1_V1: 3183 meshes in the model (3156 unchanged, 27 edited in place, 0 with new triangles; 0 not in the model: kept), 0 packets moved, 0 blocks grown, 27 boxes grown, 0 materials and 0 textures swapped, 0 texture pictures changed; STATIC.DAT 5341184 -> 5341184, STREAMED.DAT 12124160 -> 12124160 bytes -> mods/high-road/patches/TRACKS/US/C1_V1/STREAMED.DAT.b3patch
```

(The patch is 29 KB; with `mods/high-road.b3mod` as the last argument the same folder is made and packed, 7.5 KB.)

**What is imported: the visual meshes**, as the export gives them: the sections' high-detail and LOD meshes
(`STREAMED.DAT`), the backdrops, signs and water, and the instance and prop models (`STATIC.DAT`); the textures of
`STATIC.DAT`; which texture a material draws, and which material a backdrop or instance model is drawn with.

| Edit | How the import writes it |
|---|---|
| Nothing changed but the order (a modelling tool's re-save reorders and splits vertices) | the file's own strips: the same bytes |
| An object of several meshes of one material (backdrops, signs, water) as Blender's glTF exporter writes it: one primitive per material, without the primitives' `extras` | cut back into the file's meshes by its triangles (each one a triangle of one of them, by its three positions in the mesh's own space), with a note: repainted or moved as a whole, it imports as any mesh does; with vertices moved or faces added its triangles are not the file's any more and it is refused as before (edit such an object in a tool that keeps the primitives' extras, or leave it out of the model) |
| A section, backdrop or water object moved, rotated or scaled as a whole (its node's matrix) | the file's own strips with the matrix applied to the vertices (and the normals turned); the files keep their sizes. The instances and props placed under a section's node stay where the file places them |
| Vertices moved, the triangles' order and vertex order kept (a script) | the same strips and batches, the new positions |
| Texture coordinates (UVs) | stored as 4.12 fixed point (u, v x 4096, -8 .. 8) for the route's meshes, as floats for the instance models |
| Vertex colours painted (`COLOR_0`, in a modelling tool) | the stored GS bytes follow: `COLOR_0` shows them x 4 (alpha x 2), so a channel the tool changed is stored as its value / 4; one it did not change keeps its byte (the export's `_B3_COLOR_R` .. `_A` say which: an edit of those attributes themselves is taken as it is). The file's own strips with the new colours, also when the tool re-ordered the vertices (Blender does): the model's triangles are matched with the file's by their other fields; a stored vertex whose corners were painted apart gets new strips |
| Normals (the specular materials, types 1 and 2) | the stored bytes while `NORMAL` still points the same way, else `NORMAL` quantised; none in the model: made from the faces |
| Triangles added, deleted, a mesh remodelled (vertex count, topology; Blender's edit mode reorders the vertices) | new strips (greedy, at most 80 vertices per VU batch, 68 for type 1, 76 for the instance models; the winding kept); a packet that no longer fits its place moves into freed packet space, to its block's free end or, in `STATIC.DAT`, to the end of the file (the mesh's offsets follow); a `STREAMED.DAT` block that is full grows by 32 KB steps up to the game's slot for it (160 KB for a high-detail block, 96 KB for a LOD block), the later blocks and the unit table in `STATIC.DAT` following |
| A mesh's faces all deleted | one batch that draws nothing |
| A texture PNG edited (the same size) | written into its raster in place as `txd import` does (a recolour keeps the palette's layout; a new picture gets a new palette; the mip levels rebuilt) |
| A material's texture changed to another of the route's PNGs (its glTF material's base colour image) | the material's texture entry names the other raster (not an animated material's: edit its frames' PNGs) |
| A backdrop, water or instance model given another material of the same type (its primitive's glTF material) | the mesh's material index; a material of another type draws another vertex layout: refused |
| An edited mesh reaching out of its box | the boxes the game's culling tests grow to hold it: the mesh's own (and a backdrop object's), an instance or prop type's |
| An object deleted from the model | that mesh stays as it is (delete its faces instead) |

**Blender** (tutorial 10 step 4). Import the export with **File > Import > glTF 2.0** and its defaults; export with
**File > Export > glTF 2.0**, either format, **Include > Custom Properties** on, **Data > Mesh > Attributes** on,
**Use Vertex Color** Active (or Material), **Materials** Export (the default: the import finds a section's meshes by
their materials, so not Placeholder or None), Draco off. What Blender's glTF importer and exporter (Blender 4.2.23
LTS) do to a route and how the import copes:

| Blender | The import |
|---|---|
| one primitive per material slot, without the primitives' `extras` | a section has one mesh per material, so its primitives say which; an object with several meshes of one material (backdrops, signs, water) is cut back into them by its triangles |
| the vertices in its own order, split per corner | the triangle set is compared, not the order; painted colours are matched to the file's vertices triangle by triangle |
| float texture coordinates (the instance models') through v' = 1 - v and back: rounded to the precision of 1 + v, and -0 written as +0 | a coordinate that close to the file's vertex at the same position keeps the file's bits |
| node matrices as location / rotation / scale, the nodes in its own order | each placement is matched to the closest of the file's (to 1 mm and 1e-3) |
| the images in the `.glb` (named after their PNGs, no file names); in a `.gltf + .bin` beside the export, the PNGs written again (the same bytes when unedited) | a material's texture is found by the image's name |
| `NORMAL` for every mesh, also those that store no normals | used only where the mesh stores them |

**Checked** in Blender 4.2.23 LTS (headless): every route of the disc exported whole, imported and exported by
Blender's glTF add-on with the options above and imported back unedited gives the route's `STATIC.DAT` and
`STREAMED.DAT` byte for byte (**37 of 37 routes**, each run with another string-hash seed, 738 joined primitives cut
back); `tools/modding/blender_check.sh` does it for the race's route and imports an edit made in Blender (the green
out of every high-detail section's colours, one section lifted 0.5 m in object mode) with every mesh unchanged or
edited in place, the files' sizes kept; race_check PASS with that mod, the game reading the route's `STREAMED.DAT`
from it, the race shots changed.

`--restrip` rebuilds every mesh's strips, even an unchanged one (the stress test of the writer: the same triangles,
new packets everywhere); `tools/modding/track_edit_check.sh` uses it in the game.

**Refused, with a message, nothing written:** a mesh that is not one of the route's (no `b3` extras, not a section's
material, not an instance model's name: the import changes the meshes the route has and cannot add objects; join new
geometry into a section's mesh); a section mesh given another material (a unit's meshes are one per material, named by
the render tables of the 17 units around it: the import cannot add one; move the triangles into the section's mesh of
the other material, or change the material's texture); a vertex position that is not a number or beyond 100 km (1 km
for an instance model, in its own space); texture coordinates outside -8 .. 8; a primitive that is not triangles or has
no texture coordinates where its mesh has them; an instance or prop moved, added or deleted (its node's matrix not the
file's placement: the placements are the route's layout); a texture picture of another size; a material swap to a
material of another type, of an untextured or animated material's texture, or to a texture that is not the route's; a
`STREAMED.DAT` block over its slot; `STATIC.DAT` over 5.5 MB (`0x580000`, the memory block the game loads it into in
its two-stream memory layout, file-formats/track.md; a route already larger may not grow). After writing, the import reads every
mesh of the new files again, checks each names the packets it should and that the rewritten ones decode to what was
meant, and runs the renderer's own reader over the files.

**What stays the files': visual only.** The collision (each unit's `CGtCollisionKDTree` in its `STREAMED.DAT` LOD
block, kept byte for byte at its place: a raised road is drawn higher, the cars still drive on the old one), the AI,
traffic and race paths and the start lines (`GAMEDATA.BGD`, not read or written), the instance and prop placements, the
render tables (which mesh of which unit is drawn with which material, in which pass), the materials' GS state (alpha,
blending, flags), the line meshes (material type 3, not exported), the instances' lower-detail models (+0x28 / +0x30
of a type, not exported), the units' streaming quads and boundary lines, the irradiance, `ENVIRO.DAT` (its sky
pictures and parameters: export only). A changed road surface without a changed collision is a visual mod; the game's
physics and AI are untouched by it.

**The census** (`make build/track_import_check`, then `build/track_import_check <disc> [out dir] [route ...]`;
`tools/modding/track_import_check.cpp`), every route in memory:

- **writer**: every mesh the files name (the sections' high and LOD meshes, backdrops, water, instance and prop
  models), decoded with every stored lane and written again: **140,896 of 140,896** the disc's bytes;
- **identity**: export -> import with nothing edited gives the disc's `STATIC.DAT` and `STREAMED.DAT` byte for byte:
  **37 of 37 routes**;
- **re-save**: every mesh's vertices and triangles shuffled: the disc's files byte for byte, 37 of 37;
- **restrip** (every mesh's strips rebuilt): decode-identical, 37 of 37 (140,896 of 140,896 meshes the same triangle
  sets with every stored field; the renderer's decode, high detail and LOD, the same drawn triangles and bounds); the
  strips hold 2.5 % more vertices than the disc's; 13 of 37 routes stay within the slots and STATIC.DAT's block
  (the others have full blocks: a whole route restripped is the stress case, an edit restrips only its meshes);
- **edit**: the middle section raised 2 m and its colours red <-> blue: exactly that there, every other mesh as before,
  the same sizes: 36 of 36 (US/C5_V1 has no sections);
- **quad**: two triangles added to that section's first mesh: decode-identical, 36 of 36;
- **textures**: the largest texture's PNG recoloured written back (37 of 37, every texel exact), a material given
  another of the route's textures (37 of 37).

In the game: `tools/modding/track_edit_check.sh` (the race's route with the green taken out of every section's vertex
colours in `COLOR_0`, imported with `--restrip` where every block still fits its slot, else the colour edit alone; an
offscreen race with and without it): **PASS** (race_check PASS with and without the mod, both with the same 11,679,530 VU0 program launches; the race's
route, US/C2_V2, read by the game and the renderer from the mod's `STREAMED.DAT.b3patch`, one set hash; `--restrip`
was refused on that route (unit 7's high-detail block 1.5 KB over its 160 KB slot), so 2,252 meshes were edited in
place; race shots 10700 and 11500 changed in 73.7 % and 73.1 % of their pixels, every changed pixel less green).

### preview

```
b3modtool preview <file.gltf> <out.png> [--yaw D] [--pitch D] [--skip NAME,NAME]
```

A picture of an exported model, drawn on the CPU (no window, no GPU): the camera looks at the model's bounds from yaw /
pitch degrees (default 35 / 20). `--skip` leaves out the nodes whose name contains one of the words, with their
children: `--skip _B1_,_B2_,extra_lists` shows a car as it stands, `--skip _lod` a route without its far view. For a
quick check of an export (or of your edited glTF) without a modelling tool.

### The export's files (what the import reads)

**Axes and units.** The game's model and world space is right-handed with Y up, in metres (docs/track-format.md §4;
the renderer draws it unmirrored with a right-handed look-at) — the same as glTF's, so positions are written as they
are, with no conversion. A car's front is +Z (its headlights are at z = +1.77 m on `Compact Type 1`), which is glTF's
"front" too; Blender's glTF importer turns Y up into its Z up by itself. Texture coordinates are the stored ones
(u, v = stored value / 4096 for cars; tracks store floats or 4.12 fixed point), with (0, 0) at the texture's first texel
as in glTF; OBJ counts v from the bottom (v' = 1 - v). The game's part frames (`CGtMatrix3x4`: rows right, up, at,
position; world = x right + y up + z at + position) become node matrices with those rows as columns (their w lanes
dropped). `tests/test_modtool.cpp` checks these rules.

**Custom vertex attributes** (glTF allows application attributes that start with `_`; viewers ignore them):

Every stored field is written as **one float SCALAR attribute per component** (`_B3_NRM_X`, `_B3_NRM_Y`, ...): Blender's
glTF importer keeps only float attributes as they are (an unsigned short VEC2 is dropped, an unsigned byte VEC4 clamped
as a colour), and it pairs a mesh's `_` attributes with their data in an order that changes from run to run, which
only works when all of them have one form ("car import", "Blender"). The imports also read the forms of older exports:
one VEC4 / VEC2 per field (`_B3_NRM`, `_B3_INFL`, `_B3_COLOR`, `_B3_LIGHT`; as floats, or u8 x 4 and u16 x 2 for cars,
u8 x 4 colours and s8 x 4 normals for routes) and `_B3_W` as u32.

| Attribute | Type | Holds |
|---|---|---|
| `_B3_W` | float (0 or 32768) | the position's stored w lane (0x8000: the strip restart flag) |
| `_B3_NRM_X`, `_B3_NRM_Y`, `_B3_NRM_Z` | float each (whole numbers: cars the bytes 0 .. 255; tracks -128 .. 127) | the stored normal bytes (cars: n = (b - 128) / 128, not unit length; tracks types 1 / 2: n = s / 127) — `NORMAL` holds them normalised |
| `_B3_NRM_W` | float (whole numbers -128 .. 127) | tracks: the normal's 4th byte, the specular intensity of types 1 / 2 |
| `_B3_INFL_0`, `_B3_INFL_1` | float each (whole numbers 0 .. 65535) | cars: the two deformation influences of the vertex (weight << 8, zone << 2): what the game's damage uses |
| `_B3_COLOR_R`, `_G`, `_B`, `_A` | float each (whole numbers 0 .. 255) | tracks: the stored GS colour bytes (`COLOR_0` shows them x 4, alpha x 2) |
| `_B3_LIGHT_X`, `_Y`, `_Z`, `_W` | float each | tracks, material type 5 (the dynamically lit instance models): the stored per-vertex lighting vector |

**The sidecar** (`.b3car.json` / `.b3track.json`): the source file's path, size and FNV-1a 64; for cars every header
field, the wheel and panel frames as stored, the texture (name, size, format, levels, palettes), every light (its 12
floats), and per mesh entry (of every LOD, exported or not) its LOD, slot, entry, the offsets of its list, packet and
continuation packet, its material and colour words, its vertex and triangle counts, the vertex count of each VU batch,
and two hashes: `raw_hash` (the stored vertex fields in order) and `tri_hash` (the set of non-degenerate triangles by
position, UV and influences, in any order: it survives a modelling tool that reorders or splits vertices). For tracks:
the header bytes, every material (its 0x28 bytes), the animated materials' frames, the ENVIRO.DAT parameters, and per
exported mesh the file, its two chain offsets, its material, unit, instance / prop type, batches and `tri_hash`. The
glTF's nodes, meshes and materials carry the same keys in their `extras` (`b3`).

`car import` and `track import` read these files back: the sidecar says which file(s) of the disc the export came
from and where the paint / texture PNGs are; the meshes' names and `extras` say which entry (car) or which packets
(route: file and chain offsets, material) each mesh is; the `_B3_*` attributes give back the stored fields a modelling
tool cannot show.

**The census** (`make build/model_export_check`, then `build/model_export_check <disc> <out dir> [route ...]`; `tools/modding/model_export_check.cpp`): exports every
player car and route, checks each glTF, reads it back and compares it with the renderer's own decode
(`b3asset::read_car` / `read_track`). On the US disc: 67 of 67 cars valid, 268 of 268 LODs with the decoder's vertex
and triangle counts and bounds (to the bit), the LOD 3 pictures of both within 2.2 % of pixels (the glass, drawn
blended in another order); 37 of 37 routes (2,838 sections) valid with the decoder's triangle counts, high detail and
LOD (17.1 million triangles placed in all). `tools/modding/gltf_check.py` (Python, standard library) checks the files
again independently: 104 of 104 valid.

### diff / apply

```
b3modtool diff <original> <modified> <out.b3patch>
b3modtool apply <original> <patch.b3patch> <out>
```

A binary delta of one file against the disc's: the patch names the original's size and hash, then copy runs (from the
original) and insert runs (your bytes). `apply` refuses a file that is not the one the patch was made from. Format:
[b3patch.md](b3patch.md).

```
$ b3modtool diff ../game/DATA/GLOBALUS.BIN work/GLOBALUS.BIN my-mod/patches/DATA/GLOBALUS.BIN.b3patch
diff: ... 19 ops, 199903 bytes copied from the original, 14939 new; patch 15055 bytes
$ b3modtool apply ../game/DATA/GLOBALUS.BIN my-mod/patches/DATA/GLOBALUS.BIN.b3patch work/check.BIN
apply: work/check.BIN (214842 bytes, the patch's target hash checked)
```

(The 14939 new bytes of that example are mostly the string table's offsets, which all move when one string's length
changes; the text itself is 16 bytes.)

A patch in a mod's `patches` folder (`my-mod/patches/<disc path>.b3patch`) is applied by the game itself when it
starts, to the player's own file, after checking that it is the file the patch was made from
([mod-format.md](mod-format.md) "Patches"). `apply` does the same by hand: to look at the result, or to check a patch.

### verify

```
b3modtool verify <mod folder | file.b3mod> [--game <disc>] [--allow-exports]
```

Checks a mod as the loader will read it, and as it may be shared:

- **errors** (the mod should not be shared like this): no `mod.ini` at the top, a manifest error (the loader's own
  rules: id, api, root), `SLUS_210.50` / `SYSTEM.CNF` in the overlay, two files for one disc path (names differing
  only in case), a path that is not a disc path, a link out of the mod, an archive entry that is absolute or leaves the
  mod (zip slip), a damaged `.b3patch`, a patch and a file in `data/` for the same disc path (the loader refuses
  that mod); **an export of b3modtool's inside the mod** (below); with `--game`: **a file that is a byte-for-byte
  copy of any disc file**, a patch made from another version of its file than the disc's;
- **warnings**: with `--game`, a replaced file of which half or more is the disc's file ("ship a b3patch"; `pack`
  refuses it without `--force`); a `.b3patch` outside the mod's patches folder (the loader does not apply it), a file
  in the patches folder that is not a `.b3patch`; a `[textures] pack` folder with no replacement textures; manifest
  notes (no `api`, ...); a mod that changes nothing;
- **notes**: files the loader leaves out (dot files, `Thumbs.db`, `desktop.ini`), new disc paths, files in the
  `[textures]` folder that are not replacement names, no README / LICENSE.

**Exports inside a mod.** What `txd export`, `audio export`, `strings export` and `vdb export` write is the game's own
data, so it must never be shipped. `verify` recognises it by the first line these commands write, and flags it as
an error:

- a folder with a `txd.txt` starting `# b3modtool txd export:` (the PNGs of a whole texture dictionary), or an
  `audio.txt` starting `# b3modtool audio:` (the WAVs of a whole stream or bank);
- a text file starting `# b3modtool strings: <file>, <n> strings` that still has more than half of the `<n>` lines,
  or one starting `# b3modtool vdb: <file>: <n> values` with more than half of its value lines. A text with only the
  lines you changed (tutorial 02) is your own work and passes, header line or not.

Keep export folders (`work/`) outside the mod folder. `--allow-exports` turns these errors into notes, for a mod
that really is meant to hold such a folder (your own pictures in an export's layout, say).

The last line says `verify: OK` or `verify: FAIL`; the exit code is 0 for OK, 1 for FAIL.

```
$ b3modtool verify my-mod --game ../game
verify: OK my-mod: menu-words 1.0, 4 files (0 disc paths, 1 patches), 0 errors, 0 warnings
```

### pack

```
b3modtool pack <mod folder> <out.b3mod> [--game <disc>] [--allow-exports] [--force]
```

Runs `verify` (with `--game` if given, and `--allow-exports`) and, when there are no errors, writes a `.b3mod`: a
ZIP with `mod.ini` at the top and every file of the folder except dot files, `Thumbs.db` and `desktop.ini`. Give
`--game` so that copies of the game's files are caught before you share the archive: with it, `pack` also refuses a
file in `data/` that is mostly the disc's file at that path (half or more of it, as `verify` warns: a whole TXD with
one picture changed is the game's dictionary), and says how to ship the change as a patch instead:

```
$ b3modtool pack blue-logo blue-logo.b3mod --game ../game
  warning  data/DATA/FRONTEND.TXD: 99% of it is the disc's DATA/FRONTEND.TXD (ship a b3patch, or only what you made: b3modtool diff; pack refuses it without --force)
verify: OK blue-logo: blue-logo 1.0, 2 files (1 disc paths, 0 patches), 0 errors, 1 warnings
b3modtool: not packed: data/DATA/FRONTEND.TXD is mostly the disc's own file. Share your change as a patch: b3modtool diff <the disc's file> <your file> <mod>/patches/<its disc path>.b3patch, then remove your file from the mod (players' games apply the patch to their own disc). To pack it anyway: --force
```

`--force` packs it anyway (for a mod you only move between your own PCs).

```
$ b3modtool pack my-mod menu-words-1.0.b3mod --game ../game
pack: menu-words-1.0.b3mod (4 files)
```

### roundtrip

```
b3modtool roundtrip <disc> [txd | strings | vdb | audio | all]
```

The census behind the table at the top: every texture dictionary, string table, VDB.XML, stream and wave bank on the
disc goes through export and import (PNG and WAV bytes encoded and decoded again; the text written and read back with
nothing taken from the original but, for strings, its header; VDB.XML rebuilt from its text alone) and must come back
identical. For textures it also reports:

- per raster, export -> PNG -> import written through the GS memory model even where nothing changed (so the
  write-back itself is checked), and the whole file through `txd import`;
- **strict**: the rasters that come back identical from their pixels alone, with no stored value kept; those that
  do not differ only where two CLUT entries have one colour (which entry a pixel used cannot be seen in a PNG; import
  keeps it for pixels you do not change);
- **edit check**: each raster's level 0 with its colours inverted (mirrored for rasters with several palettes),
  written and read back: the result must be exactly the edited image.

On the US disc (2026-11-12):

```
  DATA/FRONTEND.TXD       221 rasters:  221 identical after export -> PNG -> import; file identical; from pixels alone (strict) 141
  DATA/GLOBAL.TXD         194 rasters:  194 identical after export -> PNG -> import; file identical; from pixels alone (strict) 179
  DATA/GLOBALUS.BIN       4006 strings:  4006 identical after export -> text -> import; file identical
  DATA/HEADUS.BIN           11 strings:    11 identical after export -> text -> import; file identical
roundtrip txd: 2 of 2 files identical, 415 of 415 rasters (strict, from pixels alone: 320)
  strict: of the 95 rasters that differ, 95 have CLUT entries of one colour, 0 stored alpha above 0x80, 0 other
  edit check (level 0 RGB inverted, or mirrored with several palettes; mips rebuilt): 415 of 415 rasters read back exactly
roundtrip strings: 2 of 2 files identical, 4017 of 4017 strings
roundtrip: PASS
```

For VDB.XML and audio:

- **vdb**: the file rebuilt from its text alone, value by value and as a whole; and an **edit check**: one named
  float + 1.0 and one new record, through a text over the original: only those two records differ when read back.
- **audio**: each segment x sub-stream of every stream and each wave of every bank decoded, written as a WAV, read
  back and imported: every original frame must be kept, the file identical. An **edit check** per file: its first
  item a quarter second longer; the rebuilt file must read back with that item as the encoder wrote it and every other
  item's PS-ADPCM, rate and flags unchanged. And the **encoder's quality**: each item's decoded audio encoded and
  decoded again (PS-ADPCM is lossy; audio that came from PS-ADPCM mostly re-encodes exactly), and the same at gain 0.9,
  which puts the samples off the original frames' grid as new audio is. Streams: the first 10 s of each item; banks:
  every wave whole. (`MUSIC.RWS` files are empty wave dictionaries the game never opens, not streams: skipped.)

On the US disc (2026-11-12):

```
$ b3modtool roundtrip ~/Games/Burnout3 vdb
  DATA/VDB.XML            7525 values (6601 named, 924 by key), 348 config files (343 named): 7525 identical after export -> text -> import; file identical
    edit check ("AI/Aggressive Driving | Extra distance in front to boost relative to aggression (meters)" + 1.0, and one record added): only those changed, 70664 bytes
roundtrip vdb: 1 of 1 files identical, 7525 of 7525 values; edit check 1 of 1
roundtrip: PASS
$ b3modtool roundtrip ~/Games/Burnout3 audio
roundtrip audio streams (.RWS): 108 of 108 files identical, 1016 of 1016 segment x sub-stream items kept byte for byte through WAV (36 MUSIC.RWS dummies, not streams, skipped); edit check (item 0 longer) 108 of 108
  encoder quality over 543.2 M samples (the first 10 s of each item):
    decode -> encode -> decode: 74.3 dB SNR; 558 items come back exactly; of the others median 75.6 dB, lowest 59.1 dB
    the same at gain 0.9 (off the original frames' grid, like new audio): 32.4 dB SNR; per item median 35.0 dB, lowest 24.2 dB
roundtrip audio banks (.AWD .HWD .LWD): 174 of 174 files identical, 1575 of 1575 waves kept byte for byte through WAV; edit check (item 0 longer) 174 of 174
  encoder quality over 40.9 M samples (every wave whole):
    decode -> encode -> decode: no error at all; 1569 items come back exactly; none differ
    the same at gain 0.9 (off the original frames' grid, like new audio): 29.7 dB SNR; per item median 31.2 dB, lowest 20.2 dB
roundtrip: PASS
```

(The other 6 waves are silent.) The audio census reads about 1 GB and takes a few minutes.

The exit code is 0 only for PASS.

`b3modtool roundtrip <disc> rasters` runs the same write-back (forced, and strict) over the rasters inside other
files, found by a header scan as `b3assetdump census` finds them. It is a report for the next part of the kit (these
files have no import command yet), not an acceptance check. On the US disc:

```
  DATA/*.BIN      5 files:     40 rasters,     40 identical written back,     40 from pixels alone (strict)
  ENVIRO.DAT     37 files:    148 rasters,    148 identical written back,    135 from pixels alone (strict)
  STATIC.DAT     37 files:   9240 rasters,   9240 identical written back,   9200 from pixels alone (strict)
  car BGV        67 files:     67 rasters,     67 identical written back,     18 from pixels alone (strict)
  car BTV        40 files:     40 rasters,     40 identical written back,     22 from pixels alone (strict)
roundtrip rasters (report only: no import for these files yet): 9535 of 9535 identical (strict 9415)
```

## How the texture import works

The game's rasters are stored as the GS upload they are sent with: image transfers, often in another storage format
than the one the texture is drawn in (a 256-colour texture sent as 32-bit pixels, a 16-colour one as 16-bit, the CLUT
in the same transfer), the RenderWare PS2 "swizzled" forms. The decoder (b3asset, `src/b3asset/raster.cpp`) replays
those transfers into a model of the GS memory and samples the texture from it as the GS does. The import does the
reverse with the same model: it replays the transfers, writes the new texel values and CLUT where the texture is
sampled from, and reads every image byte of the upload back from the memory at the place the transfer had put it. So
no swizzle is coded by hand, and the file keeps its size and every byte outside the image data. A raster whose
uploads write the same GS memory twice could not be written back this way: `txd list` marks it EXPORT ONLY and import
refuses to change it (none on the US disc).
