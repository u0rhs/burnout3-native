# VLIST.BIN and TLIST.BIN: the car and route lists

`PVEH/VLIST.BIN` and `TRACKS/TLIST.BIN` (4,096 bytes each) list the cars and the routes the game knows, with one or two
flags per entry. The game reads exactly 0x1000 bytes of each at boot (`func_00133190` states 4..6) into the
CB3VehicleList and CB3TrackList objects and answers every "which cars / routes exist" question from them
(B3DataLists.cpp, 0x251870..0x252000) [C]. VLIST.BIN has 107 entries, TLIST.BIN 36 [D].

| | |
|---|---|
| Status | known (every field the code reads) |
| Decoder in the port | none (the game's own code runs natively) |
| Round trip | none (a fixed-size table: an editor only has to keep the layout below) |
| Helper | `python3 -I tools/modding/unk_dump.py <disc> lists` prints both lists |

Evidence tags: see [index.md](index.md#evidence-tags).

## Layout (both files)

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x000 | 4 | u32 | VLIST 6, TLIST 4 | [D] [G: a version; no reader found] |
| 0x004 | 4 | s32 | entry count n (at most 128) | [C 0x2518E0 / 0x251E60 loops] [D] |
| 0x008 | 4 x 128 | s32 | flag A per entry | [C 0x2519B0 / 0x251EF0] |
| 0x208 | 4 x 128 | s32 | value B per entry | [C 0x251920 / 0x251EA0] |
| 0x408 | 8 x n | u64 | entry GtID (12 characters packed base 40, `GtIDUnCompress` 0x21ADF0) | [C] |
| 0x408 + 8n | | | not read | [C: every loop stops at n] [D: not zero, leftover bytes] |

Slots past n in the two s32 arrays are not read either and hold leftover bytes too [D]; they can be zeroed.

| File | ID | Flag A (+0x008) | Value B (+0x208) |
|---|---|---|---|
| VLIST.BIN | car: `COMPCAR1` = class `COMP` + `CAR1` | driveable (`IsVehicleDriveable` 0x251EF0; 0 = traffic-only models such as `COMPCAR11`..`19`) | boost bar size, 0..4 (`GetVehicleBoostBarSize` 0x251EA0) |
| TLIST.BIN | route: `US_C1_V1` | point to point (`IsTrackPointToPoint` 0x2519B0; set for the P1 / P2 routes of US and EU) | right-hand drive (`IsTrackRightHandDrive` 0x251920; 1 for US and EU, 0 for AS) |

How the IDs are used [C]:

- **Car class** = the ID's first four characters, compared with `COMP`, `MSCL`, `CUPE`, `SPRT`, `SUPR`, `HSPC`, `HEVY`,
  `TRAF`, `TSPC` in that order (`GetClassFromID` 0x251CB0, class 0..8, 9 = none). `GetCarCount` / `GetCarID` count and
  pick the cars of a class in list order (the car select screens).
- **Car path** = `pveh/` + class + `/` + the rest up to the first space: `pveh/COMP/CAR1` (`CreateVehiclePathFromIndex`
  0x251F40); the caller adds the file extension. The HSPC cars are `CAR20`..`CAR25`, as their files are named.
- **Route region** = the ID's first character, `U` 0, `E` 1, `A` 2, else 3 (`GetTrackRegion` 0x251870); **marathon**
  = the fourth character is `M` (`IsTrackMarathon` 0x251A00).
- **Route path** = `tracks/` + chars 0..1 + `/` + chars 3..4 + `_` + chars 6..7 + `/`: `tracks/US/C1_V1/`
  (`CreateTrackPathFromID` 0x251AE0).
- An unknown car ID: driveable, boost bar 1; an unknown route: point to point, right-hand drive [C: the loops'
  fall-through returns].

`TRACKS/US/C5_V1` is on the disc but not in TLIST.BIN (and has no GAMEDATA.BGD) [D].

## What a modder can do today

- Flip a flag or change a boost bar size: an in-place edit of a confirmed field, through the overlay with the whole
  4,096-byte file (or a b3patch). Driveable / boost bar size change what the player can pick and how the car plays:
  gameplay-changing.
- A new car or route entry (n + 1, a new ID) is valid for this file, but everything else the game keys by that ID
  (the car files, PRGDATA.BIN, STAGEHED.BIN, GAMEDATA.BGD, the menus' string and texture names) has to exist too: not
  yet a working route for new content (DESIGN.md §12).

## Pitfalls

- The game reads 0x1000 bytes: a longer file is cut, a shorter one leaves the rest of the buffer as it was.
- At most 128 entries (the two arrays have 128 slots; the ID array then reaches 0x808).
- IDs are compared as whole u64 values: pack them exactly as `GtIDCompress` 0x21AEF0 does (A-Z, 0-9, `-`, `/`, `_`,
  spaces at the end).
