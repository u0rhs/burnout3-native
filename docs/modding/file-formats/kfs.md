# KFS: camera keyframe archives

`DATA/INTRO.KFS` (205,136 bytes) and `DATA/AFTER.KFS` (205,040 bytes) hold the scripted camera paths of Crash mode:
each is a small archive of 100 keyframe sets, one per crash junction. INTRO.KFS is keyed by the junction's event ID
(the fly-by before the run), AFTER.KFS by the junction's number 1..100 (the camera after the crash) [G: which camera
each one drives, from the keys and the loader's types]. The program also names `Data\Replay.kfs`, which is not on the
disc; the loader then finds no entry and the game goes on without keyframes [C 0x374550].

| | |
|---|---|
| Status | partly known: the archive layout is confirmed, the keyframe record only in part |
| Decoder in the port | none (the game's own loader runs natively) |
| Round trip | none |
| Helper | `python3 -I tools/modding/unk_dump.py <disc> kfs` lists both archives |

Evidence tags: see [index.md](index.md#evidence-tags).

## Layout

The first 0x800 bytes are a directory; entry data follows it.

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x000 | 4 | s32 | entry count (100 in both files) | [C 0x374550] [D both files] |
| 0x004 | 4 | -- | 0 | [D] |
| 0x008 | 16 x n | row | {u64 key, u32 size, u32 offset} | [C 0x374550] |
| ... 0x7FF | | | zero | [D] |
| 0x800 + offset | size | entry | one keyframe set | [C: the loader seeks `offset` from the end of the directory] |

Row keys are GtIDs (12 characters packed base 40 into a u64, `GtIDUnCompress` 0x21ADF0). INTRO.KFS: the crash event
IDs (`1G0000USC3V1` ...), the same IDs as the crash events' sections in [GAMEDATA.BGD](gamedata-bgd.md); AFTER.KFS:
`000000000001` ... `000000000100`, built by `sprintf` from the crash stage's index + 1
(`FindCrashStage__26CB3HardCodedCrashStageListFUl`) and `GtIDCompress` [C 0x374550]. Rows are sorted by key and the
entries are packed in row order, every size 0x800 [D].

One entry (0x800 bytes as stored):

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | 1 in all 200 entries (a version?) | [D] [G meaning] |
| 0x04 | 4 | u32 | keyframe count (INTRO up to 14, AFTER up to 29) | [D: count x 0x20 = the next word in all 200] |
| 0x08 | 4 | u32 | keyframe bytes (count x 0x20) | [D] |
| 0x0C | 4 | -- | 0, or a float in one AFTER entry | [D] |
| 0x10 | 0x20 x count | CB3CameraKeyFrame | the keyframes | [L: see below] |
| ... | | | zero to 0x800 | [D] |

A keyframe (0x20 bytes) as far as the retail accessors show it: +0x14 is a bit field (bits 0..3 behaviour type
`GetTypeBehaviour` 0x446FC0, 4..7 transition type 0x446FB0, 8..9 event pause 0x446FA0, 10..11 event record 0x446F90,
bit 31 "is set up" 0x446FE0), +0x18 a byte, the race car index (`GetRaceCarIndex` 0x446FD0) [C]. Bit 31 is set in every
one of the 2,239 keyframes on the disc, which places the record at entry +0x10 [D]. The first 0x10 bytes look like a
float followed by a position (x, y, z in world units) [G].

## How the game loads it

`CB3CameraEditor::LoadMungedKeyFrames(void*, EB3KeyFrameType)` 0x374550 [C]: type 1 opens INTRO.KFS and looks up the
current event ID, type 3 opens AFTER.KFS and looks up the crash stage number, any other type opens Replay.kfs. It reads
the 0x800-byte directory, finds the key, seeks to the entry and reads `size` bytes into the caller's buffer; a missing
key clears the buffer and returns 0. The keyframes are then used by `CB3BehaviourKeyFrame::Prepare` and its siblings
(src/nonmatching/Prepare__20CB3BehaviourKeyFrameFPv.cpp).

## What a modder can do today

- Replace a whole KFS through the loader's overlay. A copy with entries swapped between junctions is plausible (every
  entry has the same size), but a camera path is in world coordinates of its own route, so an entry only makes sense
  on the junction it was made for.
- Editing keyframes needs the record decoded first: positions and timing are the parts left to confirm.

## Pitfalls

- The last entry runs past the end of the file (INTRO by 0x6B0 bytes, AFTER by 0x710): the row says 0x800 but the
  file stops early [D]; the read is short and the end of the buffer keeps what was there before [G]. A rebuilt file
  can keep every entry whole.
- The loader reads `size` bytes into a buffer its caller owns; every entry on the disc is 0x800 bytes, so keep a
  keyframe set within 0x800 - 0x10 bytes (63 keyframes) until the caller's buffer size is known [G].
