# GAMEDATA.BGD: per-route game data (start grid, triggers, traffic, AI lanes, Crash mode objects)

`TRACKS/<area>/<route>/GAMEDATA.BGD` (36 files, 1.6 to 2.4 MB; every route folder except `US/C5_V1`) is what the
simulation reads about a route besides its geometry: per stage (an event, or one of five generic mode presets) the
opponents' slots, the start positions, the start / finish lines and split-time checkpoints, the traffic set, and for
Crash mode the pickups and ramps; per route the traffic lanes and the AI lanes. **It is gameplay data**: the AI drives
on the AI lanes, traffic runs on the traffic lanes, the race is timed by the triggers. Collision geometry is not in this
file [C: nothing in `CB3GameData::Prepare` builds collision; G: it lives with the track data, docs/track-format.md].

| | |
|---|---|
| Status | partly known: the directory, the stage header's section table and the track-data block are confirmed; traffic, traffic lanes and AI lanes are known only as blocks with their fix-up functions |
| Decoder in the port | `src/b3asset/crash.cpp` (`read_game_data`, include/b3asset/crash.h): the directory, start positions, Crash mode pickups and ramps, for drawing them (docs/native-renderer.md §12.9); checked on all 36 routes by `tests/test_b3asset.cpp` |
| Round trip | none |
| Helper | `python3 -I tools/modding/unk_dump.py <disc> gamedata`: every stage's sections and counts |

Evidence tags: see [index.md](index.md#evidence-tags). All of the code facts below are from
`CB3GameData::Prepare` 0x14F050 (src/nonmatching/Prepare__11CB3GameDataFv.cpp: a near-miss; its logic is written from
the retail asm, so [C] here means the instruction stream, not a matched unit).

## Directory (0x000, 0x800 bytes)

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x000 | 4 | u32 | 9 | [D all 36] [G: version] |
| 0x004 | 4 | u32 | 0x320178 | [D all 36] [G] |
| 0x008 | 8 x 50 | u64 | stage GtIDs (12 characters packed base 40, `GtIDUnCompress` 0x21ADF0) | [C] |
| 0x198 | 4 x 50 | s32 | file offset of each stage's header | [C] |
| 0x260 | 4 | s32 | stage count (6..19 on the disc; 377 in all) | [C] [D] |
| 0x800 | 0x2800 | block | alternative slot sets: 0x390-byte slot sets from +0x000, s32 counts at +0x2010, traffic sizes at +0x2034 and offsets at +0x2040; used instead of the stage header's own when a flag of the CB3GameData object (+0x58B) is set | [C states 4, 7, 9, 10] [G: which mode sets the flag] |

The game picks the stage whose ID equals CB3GameData +0x558 (the event being started); with no match it seeks to
offset 0 and reads the directory itself as the header [C state 5]. Stage IDs: the five generic sections `OFFSGRCF`, `ONSGRCF`, `OFFBTRCF`, `OFFRRGEF`,
`OFFBRLPF` (one each per route; the names suggest single race, online single race, burning lap, road rage, ... [G]) and
the World Tour / Crash event IDs (`0C040AASM1V2`, crash junctions `1G0000USC3V1` ...) [D].

## Stage header (0x800 bytes)

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x000 | 0x390 | slots | six opponent slots of 0x98 bytes (car class and car GtIDs, floats); the game points a global at it | [C state 7] [D: the same bytes as the stage's STAGEHED.BIN record, [bin-data.md](bin-data.md#stagehedbin)] |
| 0x390 | 0x10 | u64 x 2 | route GtID, event GtID (0 for the generic sections) | [D] |
| 0x3B4 | 4 | s32 | stored in a global next to the slot pointer (0, 1, 2 or 6) | [C state 7] [G: slots in use] |
| 0x3BC | 4 | s32 | track data size | [C state 8] |
| 0x3C0 | 4 | s32 | track data offset (from the file start) | [C state 7] |
| 0x3C4 | 4 | s32 | traffic data size | [C state 10] |
| 0x3C8 | 4 | s32 | traffic data offset | [C state 9] |
| 0x3CC | 4 | s32 | traffic lanes size | [C state 12] |
| 0x3D0 | 4 | s32 | traffic lanes offset | [C state 11] |
| 0x3D4 | 4 | s32 | AI lanes size | [C state 14] |
| 0x3D8 | 4 | s32 | AI lanes offset | [C state 13] |

Over the disc: each file has one traffic lanes block and one AI lanes block, shared by all its stages; track data is
shared in 18 files and per stage in the others (the Crash junctions'); traffic data comes in 3 to 14 variants per file;
the AI lanes block ends at the end of the file in every stage [D].

The four blocks are read one after the other into memory block 9 (directory, the 0x2800 block, the stage header, track
data, traffic data, traffic lanes, AI lanes) and fixed up in place, each relative to its own start:
`CB3TrackDataStruct::Fixup` (track data), `CB3TrafficDataStruct::Fixup`, `CB3TrafficLanesStruct::Fixup`,
`CB3AILanesDataStruct::Fixup` [C states 9, 11, 13, 15].

## Track data (CB3TrackDataStruct)

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x000 | 0x50 x 6 | CB3StartPositionStruct | start positions (position at +0x30) | [C state 0x17: `CB3StartPoint::Prepare`] [L +0x30, port crash.cpp] |
| 0x1E0 | 0x20 x 2 | CB3PlanarTriggerStruct | start / finish lines | [C: `CB3StartFinishLine::Prepare(.., false)`] |
| 0x220 | 0x20 x 2 | CB3PlanarTriggerStruct | second pair of lines | [C: `Prepare(.., true)`] [G: finish lines of point-to-point routes] |
| 0x260 | 0x20 x 12 | CB3PlanarTriggerStruct | split-time checkpoints | [C: `CB3SplitTimeCheckpoint::Prepare`] |
| 0x3E0 | 4 | off | Crash mode pickups, 0x20 each: position +0x00, type +0x10 (texture index), mode mask +0x11 | [C] [P crash.cpp] |
| 0x3E4 | 4 | off | Crash mode ramps, 0x50 each: a 3x4 matrix, type +0x40 | [C] [P crash.cpp] |
| 0x3EC | 4 | s32 | start / finish line count (1 or 2) | [C `GetNumStartLines` 0x14E8A0] [D] |
| 0x3F0 | 4 | s32 | second line count | [C] |
| 0x3F4 | 4 | s32 | split-time checkpoint count (0, 3..6) | [C `GetNumSplitTimeCheckpoints` 0x14E880] [D] |
| 0x3F8 | 4 | s32 | pickup count (up to 16; 1,333 pickups in the 100 crash stages) | [C] [D] |
| 0x3FC | 4 | s32 | ramp count (up to 3; 81 ramps in 55 stages) | [C] [D] |

Pickups and ramps are prepared only in Crash mode; a pickup is used when its mode mask fits the game's options
(bit 0 normal, bits 1 / 2 two options) [C state 0x17]. The pickup textures (`pickup_boost` ... `pickup_stealer`) and the
ramp texture `ramp` are looked up by name in DATA/GLOBAL.TXD [C, port crash.cpp].

Traffic data (CB3TrafficDataStruct), traffic lanes (CB3TrafficLanesStruct: +0x0 off lanes of 0x14 bytes, +0x8 count)
and AI lanes (CB3AILanesDataStruct: +0x4 count of 0x10-byte entries at +0x14, more tables at +0x8 / +0xC / +0x18 /
+0x1C / +0x20) are not decoded [C states 13, 15 for the fields named].

## What a modder can do today

- The overlay can replace a whole GAMEDATA.BGD. A route's file only fits its own route: positions are in that route's
  world coordinates. Any change is gameplay-changing (the AI, traffic and timing read it), and the mod is flagged as
  such (DESIGN.md §6).
- Moving a Crash mode pickup or ramp, or a start position, is an in-place edit of fields confirmed above; the port
  draws pickups and ramps from this file, so such an edit shows in its renderer [P]. Changing counts, adding objects or
  editing lanes needs the blocks re-laid and their fix-ups understood.

## Pitfalls

- Offsets in the stage header are from the file start; offsets inside each block are relative to that block (each
  block is fixed up with its own address).
- The game reads each block with the size stored in the stage header into one memory block: a larger file must still
  fit memory block 9.
- Each stage header's first 0x3A0 bytes are repeated in DATA/STAGEHED.BIN; change both.
- `US/C5_V1` has no GAMEDATA.BGD (it is not in TRACKS/TLIST.BIN either, [lists.md](lists.md)).
