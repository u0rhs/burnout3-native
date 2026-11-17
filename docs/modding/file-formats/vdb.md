# DATA/VDB.XML: the tuning defaults

`DATA/VDB.XML` (one file, 70,648 bytes on the US disc) holds the values the game gives its tuning variables at start:
boost and score rules, AI driving, every car's physics, the cameras, sound mixing, pad rumble, the bloom per route. It
is **binary despite its name**: there is no XML and no text in it, only numbers. Each value is filed under a 32-bit hash
of the variable's name, its group and the config file it came from in Criterion's tools. The names themselves are not
in the file. They are in the game's code, which registers each variable at run time (the
[tuning registry](tuning-registry.md)).

| | |
|---|---|
| Status | **layout known and rebuilt**: `b3modtool vdb export / import` (text and back), `tools/modding/vdb_read.py` (a checker). 6,603 of the 7,525 values are named. |
| Port | no decoder of its own: native_play runs the game's own loader (generated from the decomp), so a VDB.XML a mod puts in the overlay is read like the disc's [L] |
| Round trip | **importable**: `b3modtool roundtrip <disc> vdb` rebuilds the US disc's file byte for byte from its text alone (7,525 of 7,525 values, the file identical), and an edited and an added record read back as written ([b3modtool.md](../b3modtool.md) "vdb") [D] |
| Changes gameplay | **yes**: most values are physics, AI, boost and score rules |

Tags: [evidence tags](index.md#evidence-tags).

## Layout

All little-endian. The game reads the whole file into one buffer and uses it in place; every offset below is from the
start of the file [C 0x0024FE90].

### Header (0x14 bytes)

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | 2. Never read by the game. A format version, probably [G] | [D] |
| 0x04 | 4 | u32 | number of value records (7,525) | [C 0x0024FE90] [D] |
| 0x08 | 4 | u32 | number of value records that are vectors (452): the first 452 blocks, in file order, are the vectors. Never read by the game | [D: 367 named vectors + 85 unnamed 16-byte blocks; the file rebuilt with this rule is identical] |
| 0x0C | 4 | u32 | number of file records (348) | [C 0x0024FE90] [D] |
| 0x10 | 4 | u32 | offset of the file records | [C 0x0024FE90] [D] |

### Value records (at 0x14, 8 bytes each)

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| +0 | 4 | u32 | the value: the variable's raw 32 bits (a float or an int), or, for a vector or an array, the offset of its block | [C 0x0024FFC0] |
| +4 | 4 | s32 | key = `CGtHash(name + group + "/" + file)` (below) | [C 0x0021BB60, 0x0021BD80, 0x0021B9B0] [D] |

The records are **sorted by key as a signed 32-bit number**. The game finds a key by binary search, so an unsorted file
loses values without any error [C 0x0024FFC0] [D: all 7,525 strictly increasing].

### Value blocks (after the value records, up to the file records)

The data of values that do not fit in a word, with no gaps, up to the file records [D]:

1. padding from the end of the records to the next 16-byte boundary of the file: 4 bytes of 0xFD on the disc (the
   first block is at 0xEB40);
2. the **vectors' blocks**, 16 bytes each (so all 16-aligned), in the order of their records' keys (signed);
3. the **arrays' blocks**, n x 4 bytes each, packed (only 4-byte aligned: the disc has arrays of 2, 3, 4, 5 and 8
   words), in the order of their records' keys; the file records follow the last one directly (at 0x10918 on the disc,
   not 16-aligned).

| Kind | Size | Copied how | Evidence |
|---|---|---|---|
| vector (`CGtV3d`) | 16 bytes: x, y, z, w floats | one 16-byte copy | [C 0x0024FFC0] [D: all 452 vector blocks 16-aligned] |
| array (a variable registered with count n >= 2) | n x 4 bytes | `memcpy` of n x 4 bytes, whatever the type | [C 0x0024FFC0] [D: 26 arrays] |

The w word of all 452 vectors is 0xCDCDCDCD (-431602080.0 as a float, the authoring tool's fill for memory it never
set) [D]; the game copies it with the vector.

Two arrays hold one word more than their registration's count: `Tailgate Category BP` (int) and `Tailgate Category
Minima (Seconds)` (float) in `Score.cfg` are registered with count 3 (as the decomp's draft reads the call) and have
4-word blocks [D]; the game copies count x 4 bytes, so the fourth word is never read [L]. `b3modtool vdb` writes
these two by key (`0x<key> = [...]`).

### File records (at the offset in 0x10, 8 bytes each, to the end of the file)

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| +0 | 4 | u32 | enabled: 0 drops every variable of that config file from the registry | [C 0x0024FDE0] |
| +4 | 4 | s32 | key = `CGtHash(file)`, e.g. of `../Export/ValueDB/Score.cfg` | [C 0x0024FDE0] [D] |

They are sorted by key as signed numbers too [D]. On the disc all 348 are 1 [D]. A file with no record counts as
enabled [C 0x0024FDE0].

## The key: CGtHash::CalculateHash (0x0021BFA0)

```
crc = -1                                   (signed 32-bit)
for each byte c of the string:
    crc = (crc >> 8) ^ table[(c ^ crc) & 0xFF]    >> is an ARITHMETIC shift (sra)
return crc                                 (no final inversion)
```

`table` (`manHashTable__7CGtHash`, 0x0049B110) is the ordinary CRC-32 table (polynomial 0xEDB88320) [C, checked
entry by entry against the executable]. Two differences from the usual CRC-32 [C asm 0x0021BFC8..]: the shift is
arithmetic, and the result is not inverted. So `zlib.crc32` gives other numbers. The byte is loaded sign-extended
(`lb`), so a character of 0x80 or above indexes outside the table: names must be plain ASCII.
`tools/modding/vdb_read.py` has the function in Python (`gthash`).

The string the key is made of is built by `AddEntry` [C 0x0021BB60]:

```
key string = name + group + ("/" unless group already ends with "/") + file
```

The three parts are the three string arguments of the registration, in this order. Examples from the code (names, not
data): name `Minimum Air Distance (Metres)`, group `Score/Boost/Air`, file `../Export/ValueDB/Score.cfg` gives
`Minimum Air Distance (Metres)Score/Boost/Air/../Export/ValueDB/Score.cfg`, key 0x58C54868; the file record's key,
`CGtHash("../Export/ValueDB/Score.cfg")`, is 0x9169203E; the empty string gives 0xFFFFFFFF (checked in
`tests/test_modtool.cpp`). There is no separator between name and group.

Per-vehicle and per-route values use a file name built at run time from the object's id. The id is a GtID: a u64 that
packs 12 characters in base 40 (`GtIDConvertToString` 0x0021AB40, trailing spaces trimmed) [C]. Vehicles are named
`<class>CAR<n>`, after their files `PVEH/<class>/CAR<n>.*`, and routes `<area>_<route>_V<1|2>`, after
`TRACKS/<area>/<route>_V<n>`:

| File name pattern | From | Files on the disc |
|---|---|---|
| `../Export/ValueDB/VehiclePhysics/<vehicle>.cfg` | `CB3VehiclePhysicsAttribs::Register` 0x00252100, `CB3RaceCarPhysicsAttribs::Register` 0x00252C20 | 100 [D] |
| `../Export/ValueDB/VehicleAudio/<vehicle>.cfg` | `CB3SoundPlayerCar::Register` 0x0028C780 | 67 [D] |
| `../Export/ValueDB/VehicleAudioAI/<vehicle>.cfg` | `CB3SoundAICar::Register` 0x0028BA20 | 56 [D] |
| `../export/ValueDB/VehicleMaterial/<vehicle>.cfg` | `CB3RaceCar::Register` 0x00150F20 | 22 [D] |
| `../Export/ValueDB/Camera/<vehicle>.cfg` | `CB3CameraAttribsCar::RegisterVariables` 0x002F8870 | 67 [D] |
| `../export/ValueDB/Sound/Tracks/<route>.cfg` | `CB3SoundCrashManager::Prepare` 0x00281C30 | 9 [D] |
| `../export/ValueDB/TrackBloom/<route>.cfg` | `CB3StaticTrack::Prepare` 0x00332E00 | 8 [D] |

Note `Export` and `export`: the case differs between files, and the hash sees it [D].

## How the game loads and applies it

1. Start-up (`func_00133190`, the staged boot) calls `CGtCommsToolBase::Prepare("Data/vdb.xml", ...)` 0x0021B830.
   It opens the file, reads its size **rounded up to 0x800** into the buffer at tool base + 0xE540 (= database +
   0xE530) and calls `InitialiseDefaultValues` [C 0x0021B830].
2. `CGtCommsDatabaseGame::InitialiseDefaultValues` 0x0024FE90 points the two record tables into the buffer, then
   applies the defaults to every variable already registered and drops those whose file is disabled [C].
3. The same boot step then registers the AI, score, burn and rumble variables (`Register__12CB3AIAttribsFv`,
   `RegisterStaticVariables__8CB3ScoreFv`, `RegisterStaticVariables__7CB3BurnFv`, `func_00344310`) [C 0x00133190].
   Cars, sounds, cameras and routes register theirs when they are set up, during a race's preparation.
4. Every `AddEntry` [C 0x0021BB60 / 0x0021BD80 / 0x0021B9B0] hashes the key string, fills a free 32-byte registry entry
   (variable pointer, min, max, the three strings, key, count, flags, type), then calls the database's virtual
   `SetDefaultElementValue` (vtable 0x004DE0F0 + 0x10), which **writes the VDB.XML value over the variable at once**
   if the key is in the file. It then calls `CheckFileValid` (+0x14, 0x0024FDE0), which unregisters the entry if its
   file record says 0. The value written stays in the variable either way.
5. `SetDefaultElementValue` 0x0024FFC0 by entry type [C]: count >= 2: copy count x 4 bytes from the block; type 1
   (int) / 2 (float): store the word; type 3 (bool): store `value != 0` as a byte; type 5 (vector): copy 16 bytes from
   the block. No key: the variable keeps the value the code gave it.

The registry holds up to 0x6C8 = 1,736 entries at a time [C `Construct__Q27GtComms20CGtCommsDatabaseGameFv`
0x00250120]. The file has 7,525 values because it carries every vehicle and route at once; only the ones in use are
registered at a time.

## Names: what the check found

`python3 -I tools/modding/vdb_read.py <disc>/DATA/VDB.XML --decomp <decomp>` hashes every registration the decomp has
(the [tuning registry](tuning-registry.md)) with every config file name it can build, and looks each one up. On the US
disc (SLUS-21050):

| | Found | Of |
|---|---|---|
| file records named (literal names in the code, and the patterns above filled with the vehicle and route ids) | 343 | 348 |
| value records named | 6,603 | 7,525 |
| registrations (call sites) that have a value in the file | 685 | 705 |
| ... of them with a file name fixed in the code | 528 | 543 |
| ... of them with a file name built at run time (per vehicle / per route) | 157 | 162 |

The 20 registrations without a value [D]:
- `Sound/Movie.cfg` and `Physics/cad.cfg` (10 entries) have no file record at all: their variables keep their code
  values.
- the 5 `Material Properties` arrays (`VehicleMaterial/<vehicle>.cfg`): the 22 files are there, but no key matched.
  Why is not known yet [G: another key string for these].
- 5 single entries: two in `Score.cfg` (the near-miss category arrays), two in `Sound/HUD.cfg` (the boost segment
  gains) and one in `Sound/Misc.cfg`.

The 922 unnamed values belong to registrations the scan does not see. One case is known: the HUD message volumes
(`Sound/Hud/General`, `../export/ValueDB/Sound/HUD.cfg`, e.g. `Lap Message Volume`) are in the file under names
the decomp has as strings, but no call site the scan reads registers them [D: found by trying every decomp string as
the name]. The five unnamed file records are probably files of such registrations [G].

## Editing it

- **With b3modtool**: `b3modtool vdb export` gives every value as `<group> | <name> = <value>` text, `vdb import`
  (with `--base` the original, for a text of only your changes) builds the file by the layout above, adds new keys
  in sorted position and checks the size limit ([b3modtool.md](../b3modtool.md) "vdb"). By hand:
- **Changing a value**: find its key (hash the key string), binary-search the records, write the new word. For a
  float that is the IEEE single. For a vector or an array, overwrite its block, not the record. Nothing else moves.
  This is the safe edit.
- **Adding a value** for a registered variable that has none: insert the record **in sorted position** (signed order),
  bump the count at 0x04, and move the blocks and the file records along; every block offset in the records then
  changes by 8, and the offset at 0x10 too. Keep the vectors' blocks 16-byte aligned, from the start of the file
  (the padding before them grows or shrinks), and count a new vector at 0x08.
- **Size limit**: the buffer is inside the game object, before the next member at +0x28DB0. That leaves
  0x12010 bytes from +0x16DA0, and the read rounds up to 0x800, so a file over 73,728 bytes (0x12000) would
  overrun it [L: from the CB3Game layout in `Construct__7CB3GameFv`, nonmatching]. The disc's file is 70,648 bytes,
  room for about 380 more records.
- **Disabling a config file** (enabled 0) only unregisters its variables, which matters for a live tuning tool. They
  still receive their values [C 0x0021BB60].
- Share a changed VDB.XML as a `b3patch` (`b3modtool diff`), never the file ([DESIGN.md](../DESIGN.md) §8). A mod that
  changes it changes gameplay ([DESIGN.md](../DESIGN.md) §6).

## Pitfalls

- It is not XML. Text editors and XML tools break it.
- Not CRC-32: the arithmetic shift and the missing inversion give other keys. Check your hash on a known name first:
  `vdb_read.py --decomp` should name the records.
- The name, group and file are joined with **no** separator between name and group. A group that already ends in `/`
  (`Sound/L Train/`, `Camera/Follow/`) gets no second `/`.
- The records are sorted by **signed** key: keys from 0x80000000 up come first.
- The case of the file name matters: `../Export/...` and `../export/...` are different files.
- A value only reaches a variable that is registered, and only when it is registered. Some are registered once at
  start (score, AI, rumble), some per race (cars, sounds, cameras, routes). Changing VDB.XML while the game runs does
  nothing until the next registration.
- The game skips the first word and the vector count. Keep them as they are; other tools may read them.

## Sources

- Decomp (`../burnout3-retail-decomp`): `src/b3/_unclassified/GtHash_0021BFA0.cpp` (the hash, matched),
  `src/nonmatching/vdbRegisterFloat.cpp` / `vdbRegisterInt.cpp` / `vdbRegisterVector.cpp` (AddEntry),
  `vdbDbLoad.cpp` (InitialiseDefaultValues), `vdbDbApply.cpp` (SetDefaultElementValue), `vdbDbIsEnabled.cpp`
  (CheckFileValid), `src/b3/_unclassified/func_0021B830.cpp` (the load), `src/nonmatching/func_00133190.cpp` (boot);
  the vtable `__vt__Q27GtComms20CGtCommsDatabaseGame` 0x004DE0F0.
- [docs/names-layer.md](../../names-layer.md) §vdb: the same registrations decoded from the executable, and the same
  check on VDB.XML (528 of 543 fixed-file registrations).
- Tools: `tools/modding/vdb_read.py` (layout check, naming; `--dump` lists values, for your own use only),
  `tools/modding/tuning_registry.py` (the registry table; `--names-table --cxx` writes b3modtool's names table
  `src/b3modtool/vdb_names.inc`), `b3modtool vdb` (`src/b3modtool/vdb.cpp`: the hash, the reader and writer, the text
  form).
