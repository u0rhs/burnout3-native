# Movies: `.M2V` video

The game's movies are bare **MPEG-2 video elementary streams** (`.M2V`): no container, no audio track. Their sound,
when they have any, is a segment of a RenderWare audio stream ([audio.md](audio.md)), mostly `OVID/MOVIE.RWS`. On the
US disc there are 109 movies, 0.7 GB:

- `OVID/*.M2V` (61): the intro and logos (`TITLES30`, `CRI_RW30`, `ENGLIS30`), two tutorial trailers (`TRC_ENG`,
  `TRD_ENG`), the unlock movies (`COMPLOCK`, `COUNLOCK`, `MUUNLOCK`, `SPUNLOCK`, `SUUNLOCK`), the end-of-tour movies
  (`ENDRACE`, `ENDCRSH`), `CREDITS`, and the award movies: `SPCINV01-03`, `SPCPST01-10`, `SIGTKD01-20` (signature
  takedowns), `TKDTRP01-04`, `CRSHHD01-10` (crash headlines);
- `TRACKS/*.M2V` (48): `_FEMAIN.M2V`, the menu background that loops behind the front end, and 47 small route previews
  for the event menus (`ALPINE1` ... `WINTERC6`, plus `STATIC.M2V`).

`OVID/TITLES25.M2V` (the 640 x 512, 25 fps PAL intro) is on the disc but this NTSC program never asks for it (§3).
`DATA/INTRO2.M2V` is **not a movie** (§5).

Evidence tags: see [index.md](index.md#evidence-tags).

## Status

| | |
|---|---|
| Decoded by the port | yes, every movie on the disc: `src/native/mpeg2_decoder.h` (a native MPEG-2 decoder, checked against ffmpeg on every disc movie) behind `src/native/platform_movie.cpp`, which replaces the PS2 libraries the game's `CGtVideoDecoder` drives (Sony libmpeg and the ViBuf ring); the PS2's IPU is not used [P] |
| Reader for modders | `tools/modding/movie_info.py <disc>`: size, frame rate, bit rate, profile per file; `--count` also counts pictures and GOPs and lists any start code that does not belong in a bare video stream (none on the disc: 44,314 pictures, 3,049 GOPs in 109 files) |
| Round trip | not applicable: the files are standard MPEG-2, readable and writable with ordinary video tools (ffmpeg plays them as they are). No b3modtool command |
| Replacing a file | a mod can supply a new `.M2V` at the same path (the overlay serves it like any file); it must meet the limits of §4. New movies, or other names, need a code mod: the names are a fixed table in the program (§3) |

## 1. The stream

A plain ISO 13818-2 video elementary stream [D: starts with a sequence header `00 00 01 B3`, every file]: a sequence
header and a sequence extension before every GOP, GOPs of about 15 pictures (I, P and B), one sequence end code at
the end [D `movie_info.py --count`, all 109]. There is no pack or PES layer.

| Files | Size | Frame rate | Other | Evidence |
|---|---|---|---|---|
| `OVID/TITLES30`, `CRI_RW30`, `ENGLIS30` | 640 x 480 | 29.97 | | [D] |
| other `OVID/*` and `TRACKS/_FEMAIN` | 640 x 352 | 30 (50 files) or 29.97 (`COMPLOCK`, `COUNLOCK`, `CREDITS`, `MUUNLOCK`, `SPUNLOCK`, `SUUNLOCK`, `_FEMAIN`) | `TRD_ENG`: aspect code "square pixels", 7,000,000 bit/s, progressive_sequence 0 | [D] |
| `TRACKS/<route preview>` | 192 x 144 | 29.97 (41), 25 (5), 30 (`STATIC`) | | [D] |
| `OVID/TITLES25` (unused) | 640 x 512 | 25 | | [D] |

Common to all: MPEG-2 Main Profile at Main Level (profile/level 0x48), 4:2:0, a nominal bit rate of 7,372,800 bit/s
and a VBV buffer of 224 KB (except `TRD_ENG`, above), progressive frames [D all 109].

## 2. How the game plays a movie

Every movie is played by one decoder, the front end's `CGtVideoDecoder` at `CB3FrontEnd` + 0x1C8 (address 0x51A870)
[C tools/native/movie_skip.h; decomp `func_00130C70`, `func_00130D50`]:

| Step | Function | What it does | Evidence |
|---|---|---|---|
| prepare | `func_00130D50` | movie index i (the front end's +0x2FC): the file name from the table at 0x4B5300 (`sprintf` with the number 30, so `Titles%d` becomes `TITLES30`), the picture size **640 x the height table at 0x4B5960** (480 for indices 0-9, 352 for 10-81) or **192 x 144** for i >= 82 (0x52), the flags at 0x4B5750 (bit 0: loop); then `CGtVideoDecoder::Prepare` 0x3254F0 | [C 0x130D50, nonmatching draft: L; the tables: C data] |
| sound | `func_00130EE0` | starts the movie's sound on a `CGtSoundStream`: segment `table[i]` (0x4B5540) of `ovid\movie.rws`; value 0x22 means `tracks\_FEMain.rws` instead, 0x24 no sound | [C 0x130EE0, draft: L; table C data] |
| play | `CGtVideoDecoder::Update` 0x325030 | feeds the 1 MB ring 64 KB at a time while the file is idle, takes up to two pictures a frame; at the stream's end loops (flag bit 0) or ends | [C, docs/movies.md; src/native/platform_movie.cpp] |
| draw | `CGt2dObjectVideoImage::_Render` 0x326960 | the current picture as 64 x 64 tiles, uploaded from RAM (RGBA32 in 16-pixel-wide strips) | [C name, docs/native-renderer.md] |
| stop | `CGtVideoDecoder::Abort` 0x324ED0, `IsCompleted` 0x324EE0, `Release` 0x324F40 | the front end's `func_00130C70` aborts, runs Update until completed, releases | [C 0x130C70 draft: L] |

The sound and the picture are started together and are not tied to each other after that [L: no sync call found;
`CGtVideoDecoder::SetExternalSync` 0x324F30 exists, G whether it is used].

## 3. The movie table

129 slots, in this order [C rodata 0x4B42E0-0x4B52FF: fixed 0x20-byte name slots; tables 0x4B5300 / 0x4B5540 /
0x4B5750 / 0x4B5960]:

| Index | Files | Sound (`MOVIE.RWS` segment) | Loop |
|---|---|---|---|
| 0 | `ovid\Titles%d` -> `TITLES30` | 11 | |
| 1-8 | `ovid\dutch%d` ... `swedis%d` -> only `ENGLIS30` (2) is on the US disc | 23 | |
| 9 | `ovid\cri_rw%d` -> `CRI_RW30` | none | |
| 10-17 | `ovid\TrC_<language>` -> only `TRC_ENG` (11) on the disc | the language's "train 2" segment (5 for English) | |
| 18-25 | `ovid\TrD_<language>` -> only `TRD_ENG` (19) | the language's "train 1" segment (4 for English) | |
| 26 | `tracks\_FEMain` | `TRACKS/_FEMAIN.RWS` | yes |
| 27-31 | the five unlock movies | 24, 24, 2, 30, 31 | |
| 32, 33 | `EndRace`, `EndCrsh` | 32 | |
| 34 | `credits` | 11 | yes |
| 35-37 | `SpcInv01-03` | 7 | |
| 38-47 | `SpcPst01-10` | 25 | |
| 48-67 | `SigTkd01-20` | 29 | |
| 68-71 | `TkdTrp01-04` | 32 | |
| 72-81 | `CrshHd01-10` | 16 | |
| 82-128 | the 47 route previews (`tracks\Alpine1` ... `tracks\Static`) | none | yes |

The format number is always 30 in this program [C 0x130DF0], so the `25` files (PAL) are never opened; the other
languages' files are not on the US disc and those slots are not used in the US game [L]. `MOVIE.RWS` segments 1 and 33
are also played by the award page (`func_004547C0`) [C 0x4547C0].

## 4. Replacing a movie: limits

- **Same picture size as the slot**: the decoder is prepared with the size from the table (640 x 480, 640 x 352 or
  192 x 144), not from the file [C 0x130D50]; a different size is not supported [L].
- **The port's decoder handles what the disc uses, and stops with an error naming anything else**: MPEG-2 Main
  Profile, 4:2:0, frame pictures only (picture_structure 3), frame_pred_frame_dct = 1 (no field prediction or field
  DCT), no concealment motion vectors; I / P / B pictures, intra VLC format, alternate scan, q_scale_type, intra DC
  precision 8-10 and loaded quantiser matrices are fine [P src/native/mpeg2_decoder.h]. Interlaced encodes (field
  pictures, field DCT) do not play.
- Keep the frame rate (29.97 or 30, 25 for the five 25 fps previews): the decoder takes pictures at the game's pace,
  not by the file's time stamps [L].
- An encode that should meet these [G: not tried in the game]: `ffmpeg -i in.mp4 -an -vf scale=640:352 -r 30000/1001
  -c:v mpeg2video -pix_fmt yuv420p -b:v 6M -maxrate 7372800 -bufsize 1835008 -g 15 -bf 2 -f mpeg2video OUT.M2V`
  (progressive input; no `-flags +ildct+ilme`).
- **Sound** comes from the `MOVIE.RWS` segment of §3, not from the video: a new movie of another length keeps the old
  sound unless `MOVIE.RWS` is replaced too (no importer yet, [audio.md](audio.md)). With a looping movie the sound and
  picture drift apart if their lengths differ.
- The PS2 build streams the file from the disc into a 1 MB ring; the port reads the same way through its disc layer, so
  the bit rate is not limited by a drive, but the VBV size of 224 KB is what the stream was made for [L].

## 5. `DATA/INTRO2.M2V`: not a movie

Despite its name this file holds the code and data of the online service's DNAS authentication (its strings name
`DirtyDnasRel` and DNAS errors) [D]. It is loaded whole into memory by `func_0026C480` (which also puts strings
0xD50-0xD52 of the string table on screen) and its parts are found by `func_0026C190` [C 0x26C480, 0x26C190], both in the network
manager's code (`B3NetworkManager.cpp`'s range).

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | number of entries (5) | [C 0x26C190; D] |
| 0x04 | 4 | | 0 | [D] |
| 0x08 | 16 x n | | entries: +0x00 u64 the name as a GtID (12 characters, base 40, `GtIDUnCompress` 0x21ADF0), +0x08 u32 size, +0x0C u32 offset from the data start | [C 0x26C190; D] |
| (8 + 16 n, rounded up to 0x800) | | | the data: `VIDUSRUN` (1.6 MB, a MIPS ELF relocatable module) and 64 KB of data for each of `VID_ENG`, `VID_FRE`, `VID_JP`, `VID_US`; the game takes `VIDUSRUN` and `VID_US` | [C 0x26C190 compares "VID_US " and "VIDUSRUN "; D] |

The port does not run the PS2 online service; mods have no reason to touch this file.

## 6. Pitfalls

- `TRACKS/*.M2V` are menu backgrounds and previews: the port's movie skip (Start / Cross) never ends them, only `OVID`
  movies [C tools/native/movie_skip.h].
- The route previews are 192 x 144; replacing one with a 640-wide movie does not make it sharper, the slot's size
  is fixed.
- A movie the decoder rejects stops with an error naming the MPEG-2 feature it met (§4); check a new file with
  `ffprobe -show_streams` (profile Main, field_order progressive) before trying it in the game.
