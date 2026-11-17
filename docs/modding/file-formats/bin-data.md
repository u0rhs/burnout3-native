# DATA/*.BIN: stage headers, loading screens, progression

Three of the `.BIN` files in `DATA/` are game tables with no decoder yet: `STAGEHED.BIN` (374,784 bytes, a record per
stage: route, event, the six opponents' cars and per-car numbers), `LOADSCRN.BIN` (5,326,656 bytes, an archive of 40
loading-screen pictures) and `PRGDATA.BIN` (18,432 bytes, the World Tour and Crash mode progression: series, events,
unlocks). The other two are string tables: `GLOBALUS.BIN` and `HEADUS.BIN`, see [strings.md](strings.md). The three
tables refer to each other and to [GAMEDATA.BGD](gamedata-bgd.md) by GtID (12 characters packed base 40 into a u64,
`GtIDUnCompress` 0x21ADF0 / `GtIDCompress` 0x21AEF0).

| File | Status | Decoder in the port | Round trip |
|---|---|---|---|
| STAGEHED.BIN | partly known (record size, count, IDs) | none | none |
| LOADSCRN.BIN | archive known; each entry is a texture dictionary | the rasters are found by the census scan (`b3assetdump census`, `b3modtool roundtrip <disc> rasters`: 40 of 40 written back identical) | none for the archive |
| PRGDATA.BIN | tree known (the fix-up), records in part | none | none |

`python3 -I tools/modding/unk_dump.py <disc> stagehed|loadscrn|prgdata` prints each table. Evidence tags: see
[index.md](index.md#evidence-tags).

## STAGEHED.BIN

Loaded whole at front-end start-up into memory block 0x12 (`func_00131480` state 5, B3FrontEnd.cpp) [C 0x131480].
Read by `CB3MenuChoices::GetGameStageData(CB3StageDataStruct*)` 0x30CE20 and `func_0030C560`, which walk the records
looking for a (route ID, event ID) pair [C]; the count and stride they use match the file [L: the buffer they read is
this file's; the loader and the readers reach it through different globals].

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x000 | 4 | u32 | 8 | [D] [G meaning] |
| 0x004 | 0x25C | -- | zero | [D] |
| 0x260 | 4 | s32 | record count: 377 | [C 0x30CE20] [D] |
| 0x264 | 0x14 | -- | zero | [D] |
| 0x278 | 0x3E0 x count | record | stage records | [C 0x30CE20: `e++` over 0x3E0-byte records from +0x278] |
| end | | | zero padding to a multiple of 0x800 | [D] |

A record (0x3E0 bytes):

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x000 | 0x98 x 6 | slot | six opponent slots: +0x00 u64 car class GtID (`SUPR`, `HSPC` ...), +0x08 u64 car GtID (`CAR24`) or 0, +0x10..+0x93 floats (two runs of 16, then four bytes such as 0x64 = 100), +0x94 s8 | [C `func_0030C560` struct: 6 x 0x98 with +0x00, +0x08 u64 and +0x94 s8] [D: IDs] [G: what the floats are; AI tuning curves?] |
| 0x390 | 8 | u64 | route GtID (`US_C1_V1`) | [C 0x30CE20 compares it with CB3StageDataStruct +0x0] [D] |
| 0x398 | 8 | u64 | event GtID (`0C040AASM1V2`), 0 in 181 records | [C 0x30CE20 compares it with CB3StageDataStruct +0x8] [D] |
| 0x3A0 | 0x14 | | three floats, then 15, 20 | [D] [G] |
| 0x3B4 | 4 | s32 | 0, 1, 2 or 6; in GAMEDATA.BGD's copy the game stores it in a global next to the slot pointer | [C 0x14F050 state 7] [D] [G: the number of slots used] |
| 0x3B8 | 4 | s32 | a small number (0..3) | [D] |
| 0x3BC | 4 | u32 | 0x800 | [D] |
| 0x3C0 | 4 | u32 | the record's own index | [D: 0..376] |
| 0x3C4 | 0x18 | u32 x 6 | byte counts, multiples of 0x800 (the last four the same for every record of a route) | [D] [G: memory budgets] |
| 0x3DC | 4 | -- | 0 | [D] |

**STAGEHED.BIN is an index of the stage headers in [GAMEDATA.BGD](gamedata-bgd.md).** There are 377 records and 377
stage sections in the 36 GAMEDATA.BGD files; the first 0x3A0 bytes of every record equal the first 0x3A0 bytes of one
stage section's 0x800-byte header (0x3C0 bytes for 375 of them); from +0x3C0 the two differ (GAMEDATA.BGD keeps file
offsets there) [D]. The 196 distinct event IDs are exactly the event sections of GAMEDATA.BGD, which adds five generic
sections per route [D]. So the front end can show a stage's opponents without opening the route, and a change to a
stage needs the same change in both files [L].

## LOADSCRN.BIN

Read by `func_00371940` (the directory, on start-up, into the CB3MenuImages object) and `func_00371420` (one picture
each time a loading screen is shown) [C]. The directory has the same shape as the [KFS](kfs.md) archives.

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x000 | 4 | s32 | entry count: 40 (kept in a signed byte: at most 127) | [C 0x371940] [D] |
| 0x004 | 4 | -- | 0 | [D] |
| 0x008 | 16 x n | row | {u64 key, u32 size, u32 offset} | [C 0x371940 copies the rows; 0x371420 reads `size` bytes at `offset + 0x800`] |
| 0x800 + offset | size | dictionary | one texture dictionary (the same header as `DATA/GLOBAL.TXD`, one texture each) | [C 0x371420: `CGtDictionaryBase::FixUp`, then `CGtTexture::FixUp` on each texture; texture 0 is shown] [D] |

Keys: `GNRL0001`..`GNRL0020`, `CARS0001`..`CARS0012`, `TRAK0001`..`TRAK0008`; every size 0x20800 [D]. Which picture is
shown: the first time a random entry (`CGtRandom`), then the next one in the list each time, wrapping [C 0x371420].
The texture format inside is the one [txd.md](txd.md) documents.

Pitfalls: the last entry runs 0xC0 bytes past the end of the file [D]. A replacement picture must keep its dictionary
within the block the game loads it into (sizes on the disc are all 0x20800).

## PRGDATA.BIN

Loaded at boot into memory block 0xC by `func_00133190` state 6, then fixed up in place by
`CB3ProgressionDataStruct::Fixup(unsigned int)` 0x367E70 (matched), which turns stored offsets (relative to the file
start) into pointers [C].

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | 4 | [D] [G: version] |
| 0x04 | 4 | off | group array: {off series[], s32 count} x (+0x08) | [C 0x367E70] |
| 0x08 | 4 | s32 | group count: 2 | [C 0x367E70] [D] |
| 0x0C | 4 | off | a second table: 0x18-byte rows {u64 GtID of a car or car class, u32, u32, u32, u32} | [C 0x367E70 fixes the offset] [D rows] [G: unlock conditions and rewards] |
| 0x10 | 4 | s32 | that table's row count: 12 | [D] |
| 0x14 | 4 | u32 | 0x4770: the end of the data | [D] |
| 0x18 | 8 | -- | 0 | [D] |

Series (0x18 bytes): +0x00 u64 car class GtID (`COMP`, `MSCL`, `CUPE`, `SPRT`, `SUPR`, or 0), +0x08 u32 1 or 0,
+0x10 off events[], +0x14 s32 event count [C 0x367E70 for +0x10 / +0x14; D for the rest]. Event (0x40 bytes): +0x00
u64 class GtID, +0x34 off a stage reference, the rest numbers not yet read [C 0x367E70 for +0x34]. A stage reference
(0x20 bytes) is a CB3StageDataStruct: +0x00 u64 route GtID, +0x08 u64 event GtID [L: the same pair
`GetGameStageData` looks up in STAGEHED.BIN], then three small integers [D].

Group 0 has 7 series of 8, 10, 11, 14, 14, 16 and 5 events (the World Tour races); group 1 has 5 series of 20 (the 100
crash junctions, IDs `1G....`) [D]. At load the game lowers group 0's series count by one when it is 7 or more, so the
seventh series is never offered [C 0x133190 state 7].

## What a modder can do today

- The overlay can replace each file whole. Copies of the game's own file are the only valid input so far; editing a
  value in place (a number in a STAGEHED record, an opponent's car ID) is plausible for fields whose meaning is
  confirmed above, but nothing checks such an edit yet, and STAGEHED / PRGDATA feed the race set-up: treat any such
  change as gameplay-changing.
- A loading-screen picture can be replaced by editing the entry's texture with the TXD tools once they read a
  dictionary at an offset; until then the census scan finds and writes back those rasters (report only).

## Pitfalls

- PRGDATA.BIN is fixed up in place: every offset must stay inside the file, and the game reads at most its memory
  block's size.
- IDs are compared as whole u64 values: a GtID must be packed exactly as `GtIDCompress` does (upper case letters,
  digits, `-`, `/`, `_`, space).
