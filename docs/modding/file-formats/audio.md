# Audio: RWS streams, AWD wave banks, PS-ADPCM

All of the game's sound is **RenderWare Audio** (Criterion's RW Audio 3.6 / 3.7 for the PS2) data in **PS-ADPCM**, the
PS2 sound chip's own 4-bit compressed format. There are two kinds of file:

- **Streams (`.RWS`)**, played from the disc while they play: the EA Trax music (`TRACKS/_EATRAX0.RWS`,
  `_EATRAX1.RWS`, 22 songs each), the front-end menu loop's music (`TRACKS/_FEMAIN.RWS`), the radio DJ
  (`TRACKS/E_DJ*.RWS`, 12 files, and the 18 forward routes' `E_DJRACE.RWS`), crash sounds (`TRACKS/CRASH1-20.RWS`, each
  forward route's `CRASH1-3.RWS`) and the movies' sound (`OVID/MOVIE.RWS`). 108 stream files, 960 segments, about 0.9 GB.
- **Wave dictionaries ("banks": `.AWD`, `.HWD`, `.LWD`)**, loaded whole into the sound chip's 2 MB of memory: effects
  (`SOUND/*.AWD`, 22 files; the 18 forward routes' `SOUND.AWD`) and the cars' engines (`PVEH/<class>/CAR<n>.HWD` and
  `.LWD`, 67 of each). 174 files, 1,575 waves.

Each route folder also has a 124-byte `MUSIC.RWS` (36 files) that is neither: an empty RenderWare wave-dictionary
chunk that the game never opens (§5).

Evidence tags: see [index.md](index.md#evidence-tags). All integers are little-endian.

## Status

| | |
|---|---|
| Decoded by the port | yes, but by the game's own code: the port runs RW Audio's EE library as C (`src/libs/rwa/`), which reads these files unchanged; the IOP side (`src/audio/rwa_iop.cpp`) serves its file reads and sound-memory uploads, and the SPU2 mixer (`src/audio/spu2_mixer_impl.cpp`) decodes the PS-ADPCM. There is no separate host-side decoder of the file formats |
| Reader for modders | `tools/modding/audio_info.py`: prints every stream's segments and sub-streams and every bank's waves; `--census <disc>` checks every file (all 144 `.RWS` and 174 banks read, every PS-ADPCM frame header valid on the US disc); `--wav` decodes one segment or wave to a WAV file for listening |
| Round trip | **importable**: `b3modtool audio list / export / import` (each segment or wave to WAV and back, PS-ADPCM encoded; [b3modtool.md](../b3modtool.md) "audio"). `b3modtool roundtrip <disc> audio` on the US disc: 108 of 108 streams (1,016 segment x sub-stream items) and 174 of 174 banks (1,575 waves) identical after export -> WAV -> import, and each file with its first item made longer reads back with every other item unchanged [D] |
| Replacing a file | the mod overlay works for these files: RW Audio opens them by name through the port's disc layer (`src/native/platform_rwa.cpp` `disc::open` -> `platform_disc.cpp`, which asks the overlay first) [P] |
| Alternative for music | the port's own custom soundtrack (PC SETTINGS > INTERFACE > MUSIC: a folder of your MP3 / FLAC / Ogg / WAV files plays in place of, or mixed with, the EA Trax songs; `src/native/custom_music.cpp`, docs/remaster-features.md §6.1) needs no file changes at all |

## 1. The files and who opens them

| Files | Count | Holds | Opened by |
|---|---|---|---|
| `TRACKS/_EATRAX0.RWS`, `_EATRAX1.RWS` | 2 | the EA Trax songs, one segment a song (22 + 22) | the music manager (object `D_01E7A888`): `func_003FBC20` starts song n of file "tracks\\_EATrax" + digit, `func_003FC8C0` runs the stream [C 0x3FBC20, 0x3FC8C0; docs/remaster-features.md §6.1] |
| `TRACKS/_FEMAIN.RWS` | 1 | the front-end menu loop's music (one segment) | the front end's movie audio, `func_00130EE0`, for the `_FEMain.m2v` movie (its table value 0x22 = this file) [C 0x130EE0, data 0x4B5520] |
| `OVID/MOVIE.RWS` | 1 | the movies' sound, 34 segments (trailers in six languages, unlock, award and headline movies, logos) | `func_00130EE0` (segment = a per-movie table, [movies.md](movies.md)); the award page `func_004547C0` (segments 1 and 33) [C 0x130EE0, 0x4547C0] |
| `TRACKS/E_DJ*.RWS` | 12 | the radio DJ: per region (`E_DJUS`, `E_DJEU`, `E_DJAS`), general (`E_DJGEN`, `E_DJWWW`) and per event type (`E_DJM*`: seven files) | the music / DJ code (`func_003F99F0`, `func_003F9F70`, `func_003F8C30`): names built from "tracks\\" + a language prefix ("E_"; also "J_", "G_", "F_", "S_", "I_" for other languages, not on this disc) + "dj..." + ".rws" [C rodata 0x4CE8D8-0x4CEAC0] |
| `TRACKS/<area>/<route>_V1/E_DJRACE.RWS` | 18 | the DJ's lines about that route | the same code ("djrace") [C rodata 0x4CE900] |
| `TRACKS/CRASH1.RWS` .. `CRASH20.RWS` | 20 | crash sounds: two sub-streams, normal speed and the slow-motion version | `CB3SoundCrashManager::UpdateCrashStream` 0x27B820: "tracks\\crash%d.rws", the next of the 20 in turn [C] |
| `TRACKS/<area>/<route>_V1/CRASH1.RWS`, `CRASH2.RWS`, `CRASH3.RWS` | 3 x 18 | the route's own crash sounds | `UpdateCrashStream`: every fifth crash (with conditions) takes the route's `crash1` / `crash2` in turn; `CB3SoundCrashManager::Prepare` 0x281C30 prepares the route's `crash3.rws` [C, nonmatching drafts: L] |
| `SOUND/GENERIC.AWD`, `SINGLE.AWD`, `ROADRAGE.AWD`, `CRASH.AWD`, `ELIM.AWD` | 5 | shared effects; one bank per game mode | `CGtSoundBankManager::LoadBank` 0x3840C0, called by `CB3SoundGameModeManager::Prepare` 0x289EE0 (the mode bank from `GetModeBankPath` 0x288F70), `CB3SoundAICar` 0x28BDD0 and `CB3SoundCrashedTraffic` (generic) [C] |
| `SOUND/CRASHMOD.AWD`, `G_CRSH04/14/24/34/44.AWD` | 6 | crash effects: Crash mode's bank, and five variants for the other modes | `CB3SoundCrashManager::Prepare` 0x281C30: crashmod in Crash mode, else `CGtSoundUtils::GenerateVariantPath("sound\\", "g_crsh04", ".awd", mode 2)` [C, draft: L] |
| `SOUND/IDENT0.AWD` .. `IDENT9.AWD` | 10 | short station idents for the radio | the music code: "sound\\" + "ident" + n + ".awd" (`"%s%s%d.awd"`) [C rodata 0x4CE9D0] |
| `SOUND/FE.AWD` | 1 | front-end (menu) sounds | loaded at start-up [D: sync1's run log shows it]; the name is not a literal in the program [C: no "fe.awd" string], it is built at run time [L] |
| `TRACKS/<area>/<route>_V1/SOUND.AWD` | 18 | the route's ambient and local sounds | `CB3SoundGameModeManager::Prepare` 0x289EE0: "tracks\\" + area + "\\" + route + "_" + **"v1"** + "\\" + "sound.awd". The `_V1` folder is hard-coded, so reverse routes (`_V2`, which have no `SOUND.AWD`) use their forward route's bank [C] |
| `PVEH/<class>/CAR<n>.HWD`, `.LWD` | 67 + 67 | a car's engine sound model, high and low detail: engine and exhaust recordings at a few fixed RPMs (`eng_<rpm>`, `ex_<rpm>`), gear sounds | `CB3SoundESMHigh::Prepare` 0x2882D0 (`.hwd`) and `CB3SoundESMLow::Prepare` 0x287890 (`.lwd`), both through `func_00288960`: the car's path (`CB3VehicleList::CreateVehiclePathFromIndex`) + ".h" / ".l" + "wd" [C] |
| `TRACKS/<area>/<route>/MUSIC.RWS` | 36 | an empty wave dictionary ("dummy sound bank") | nothing: no "music" string exists in the program [C rodata] |

Which car uses the high and which the low engine model is not worked out here [G: the high one for the cars you hear
close up, the low one for the others].

## 2. RenderWare chunk headers

The `.RWS` files (and `MUSIC.RWS`) are RenderWare binary streams: a tree of chunks, each with a 12-byte header.

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | chunk type: 0x80D stream, 0x80E stream header, 0x80F stream data; 0x809 wave dictionary, 0x80A / 0x80C its parts | [C 0x2A9CF0 `_RwaStreamSourceFileOpCallback`; D] |
| 0x04 | 4 | u32 | size of the chunk's contents, not counting this header | [D] |
| 0x08 | 4 | u32 | RenderWare library stamp: 0x1C020009 (RW 3.7.0.2, build 9) in 92 stream files, 0x1803FFFF (RW 3.6.0.3) in 16 (the crash streams of eight routes) and in every `MUSIC.RWS` | [D all 144 files] |

The `.AWD` banks are **not** chunked (§4), although their first word is 0x809 too.

## 3. RWS streams

### 3.1 The chunk tree

Every stream file on the disc is one 0x80D chunk covering the whole file, holding a 0x80E header chunk and a 0x80F
data chunk [D all 108]. The game's reader finds 0x80D, then 0x80E inside it, reads the stream description, seeks past
the rest of 0x80E, and expects the next chunk to be 0x80F [C 0x2A9CF0]. Several 0x80D chunks in one file are
allowed by the reader (it can look a stream up by name or id) but none of the game's files has more than one.

| Offset | Size | Meaning | Evidence |
|---|---|---|---|
| 0x000 | 12 | chunk 0x80D, size = file size - 12 | [D] |
| 0x00C | 12 | chunk 0x80E | [D] |
| 0x018 | 0x14 | the stream header (§3.2) | [C 0x2A9CF0 reads 0x14 bytes; D] |
| 0x02C | (header +0x00) | the stream description (§3.3) | [C, D] |
| ... | | up to the end of 0x80E: unused bytes (whatever was in the authoring tool's memory: fragments of strings and pointers). The game skips them | [D, C 0x2A9CF0: seeks past] |
| desc +0x24 - 12 | 12 | chunk 0x80F; its contents start at a multiple of 0x800 (0x800 or 0x1000 on the disc) | [D all 108] |
| desc +0x24 | 0x80F size | the audio data (§3.6) | [C stream seek: segment +0x1C + desc +0x24 + block x block size, docs/audio.md §2.2; D] |

### 3.2 Stream header (0x14 bytes, at file 0x18)

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | size of the description that follows (the bytes the walk of §3.3 uses, in all 108 files) | [C 0x2A9CF0; D] |
| 0x04 | 4 | u32 | 0x14 in every file (this header's size) | [D; meaning G] |
| 0x08 | 4 | u32 | 0x10 in every file: the size of the stream's UUID | [C 0x2A9CF0 uses it as the id's size; D] |
| 0x0C | 4 | u32 | 0x24 in every file | [D; meaning unknown] |
| 0x10 | 4 | u32 | length of the stream's name (without its terminating zero) | [C 0x2A9CF0; D] |

### 3.3 Stream description

A packed `RwaStreamFormat`: a fixed 0x2C-byte part whose pointer fields are only "present" flags in the file, then
variable parts in a fixed order. `_rwaStreamFormatCreateFromPack` (0x2AD8E0) walks it and turns the flags into
pointers [C]. Names are zero-terminated and padded to 16 bytes; every variable part starts on the 16-byte grid the
names leave (RW Audio's block alignment, 0x10 [C `RwaCoreOpen`, src/libs/rwa/rwabase.cpp]).

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | nonzero: the stream has a UUID (in the packed data below) | [C 0x2AD8E0] |
| 0x04 | 4 | u32 | nonzero: the stream has a name | [C 0x2AD8E0] |
| 0x08 | 4 | u32 | 0 in every file | [D] |
| 0x0C | 4 | u32 | number of **segments** | [C 0x2AD8E0; D] |
| 0x10 | 4 | ptr | (filled in when loaded: the segment table) | [C 0x2AD8E0] |
| 0x14 | 4 | u32 | number of **sub-streams** (1, or 2 in the 20 `TRACKS/CRASH*.RWS` and the routes' crash files) | [C 0x2AD8E0; D] |
| 0x18 | 4 | ptr | (filled in: the sub-stream table) | [C 0x2AD8E0] |
| 0x1C | 4 | u32 | 0x800 in every file | [D; meaning G: the sector size] |
| 0x20 | 4 | u32 | **block size**: 0x2000 with one sub-stream, 0x4000 with two | [C 0x2A9CF0 uses it as the read unit; D] |
| 0x24 | 4 | u32 | file offset of the audio data (the 0x80F chunk's contents) | [C seek; D all 108] |
| 0x28 | 4 | u32 | 0 | [D] |
| 0x2C | ... | | packed parts, in this order: | [C 0x2AD8E0] |
| | 16 | | the stream's UUID (if +0x00) | |
| | n x 16 | | its name (if +0x04), padded | |
| | 0x20 each | | the segment table: +0x0C segments (§3.4) | |
| | 4 x sub-streams each | | for each segment whose +0x0C is set: its per-sub-stream data sizes | |
| | 16 each | | each segment's UUID (if its +0x00) | |
| | n x 16 each | | each segment's name (if its +0x04) | |
| | 0x14 each | | each segment's markers (if its +0x14; +0x10 of them), then their UUIDs and names. No segment on the disc has markers | [D] |
| | 0x28 each | | the sub-stream table: +0x14 sub-streams (§3.5) | |
| | 0x30 each | | each sub-stream's wave format (if its +0x14): §3.5.1, padded to 0x30 | |
| | 16 each, n x 16 each | | each sub-stream's UUID and name | |

### 3.4 Segment (0x20 bytes)

A segment is one piece of sound the game asks for **by its index** (a song, a DJ line, a movie's sound).

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | nonzero: has a UUID | [C 0x2AD8E0] |
| 0x04 | 4 | u32 | nonzero: has a name ("00".."21" in the EA Trax files, the DJ line's name, "Segment0") | [C; D] |
| 0x08 | 4 | u32 | 0 | [D] |
| 0x0C | 4 | u32 | nonzero: has a per-sub-stream size table (set in every file) | [C 0x2AD8E0; D] |
| 0x10 | 4 | u32 | number of markers (0 everywhere) | [C 0x2AD8E0; D] |
| 0x14 | 4 | u32 | nonzero: has markers | [C 0x2AD8E0] |
| 0x18 | 4 | u32 | the segment's size in the data, a whole number of blocks | [D all 960] |
| 0x1C | 4 | u32 | the segment's offset in the data (from desc +0x24), a multiple of the block size; segments follow each other with no gaps | [C stream seek; D all 960] |

The per-sub-stream size is the PS-ADPCM bytes of that sub-stream in the segment (both channels); the segment's last
block is padded.

### 3.5 Sub-stream (0x28 bytes)

A sub-stream is one track of sound that runs through every segment; the crash files have two (the normal sound and
the slow-motion one, as their names say) [D].

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | nonzero: has a UUID | [C 0x2AD8E0] |
| 0x04 | 4 | u32 | nonzero: has a name | [C 0x2AD8E0] |
| 0x08 | 4 | u32 | 0 | [D] |
| 0x0C | 4 | u32 | samples per coded frame: 28 (PS-ADPCM) | [C `RwaWaveFormatGetSamplesPerAudioFrame` 0x29F520; D] |
| 0x10 | 4 | u32 | bytes of this sub-stream in each block: 0x2000 | [D; L meaning] |
| 0x14 | 4 | u32 | nonzero: has a wave format (§3.5.1) | [C 0x2AD8E0] |
| 0x18 | 2 | u16 | **channel interleave**: 0x1000. Within its slice of a block, each channel's data is one run of this many bytes, left then right | [D: decoding with it gives correlated left and right channels; L] |
| 0x1A | 2 | u16 | coded frame size: 0x10 | [C `RwaWaveFormatGetFrameSize` 0x2A0220; D] |
| 0x1C | 4 | u32 | 1 in every file | [D; meaning unknown] |
| 0x20 | 4 | u32 | equal to +0x10 in every file | [D; meaning unknown] |
| 0x24 | 4 | u32 | offset of this sub-stream's slice inside each block (0, or 0x2000 for the second crash sub-stream) | [D] |

#### 3.5.1 Wave format (0x1C bytes + the data type's UUID)

The same `RwaWaveFormat` as in the banks (§4.4), followed by the 16-byte UUID of its data type.

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | sample rate: 32,000 Hz in every stream | [C struct; D all 164 sub-streams] |
| 0x04 | 4 | | (a pointer when loaded: the data type) | [C] |
| 0x08 | 4 | u32 | length in bytes | [C] |
| 0x0C | 1 | u8 | bits per sample: 4 | [D] |
| 0x0D | 1 | u8 | channels: 2 | [D] |
| 0x10 | 8 | | misc data pointer / size (0) | [C struct] |
| 0x18 | 1 | u8 | flags (0) | [D] |
| 0x1C | 16 | UUID | the data type: `rwaSPU2WAVEID`, the PS2's ADPCM (first word 0xD9EA9798) | [C data 0x4C1570; D every sub-stream] |

### 3.6 The audio data

The data is a sequence of **blocks** (desc +0x20 bytes). Each block holds a slice of every sub-stream (at its +0x24,
+0x10 bytes long), and each slice holds its channels one after the other, +0x18 bytes each (left, then right). So
one sub-stream, one channel = the 0x1000-byte runs at the same place in every block of the segment, in order; that is
PS-ADPCM (§6), 0x1000 bytes = 256 frames = 7,168 samples per channel per block [D, L]. The game reads streams in whole
blocks into a 16-block ring in IOP memory and gives each channel to a voice of its own (the "2 feeds" of docs/audio.md
§2.2, 7,168 samples per ring block) [C ported RW Audio C, docs/audio.md §2.2].

`tools/modding/audio_info.py <file.RWS>` prints this structure; `--wav <file.RWS> <segment> <sub-stream> <out.wav>`
decodes one segment.

## 4. Wave dictionaries: `.AWD`, `.HWD`, `.LWD`

A bank is a **memory image**: the header and the `RwaWaveDict` structures exactly as RW Audio's tool had them in
memory, with offsets from the start of the file in place of pointers, then the wave data. `RwaWaveDictReadAsync`
(0x29EB70) reads the first 0x800 bytes, `_rwaWaveDictLoaderLoadWaves` (0x29EF00) reads the rest of the header if it is
longer and relocates it (`RwaWaveDictFixup` 0x29E740 adds the load address to each nonzero offset), then the wave data
goes into sound memory in pieces (`_rwaWaveDictLoaderLoadData` 0x29ED90) [C; port: src/libs/rwa/rwawavedict.cpp].

### 4.1 File

| Offset | Size | Meaning | Evidence |
|---|---|---|---|
| 0 | header +0x08 | header: §4.2, the dictionary (§4.3), its list nodes, waves (§4.4), names and UUIDs, then padding | [C 0x29EF00] |
| header +0x08 | header +0x14 | the wave data: every wave's PS-ADPCM, each starting at a multiple of 0x80 bytes (the sound-memory unit) | [D all 174 files, 1,575 waves] |

The file is exactly header size + data size in every bank [D all 174].

### 4.2 Header (the first 0x2C bytes)

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | 0x809 in every bank (the RW wave-dictionary chunk id, but no chunk follows) | [D] |
| 0x04 | 1 | u8 | nonzero: the file is big-endian (byte-swapped on load); 0 on the disc | [C 0x29EF00; D] |
| 0x05 | 3 | | not used (left-over bytes) | [D] |
| 0x08 | 4 | u32 | header size: 0x800, 0x1000 or 0x2000; where the wave data starts | [C 0x29EF00; D] |
| 0x0C | 4 | u32 | offset of the `RwaWaveDict` in the file: 0x2C | [C 0x29EF00; D] |
| 0x10 | 4 | | not used (left-over bytes, differs between files) | [D] |
| 0x14 | 4 | u32 | size of the wave data | [C 0x29EF00; D] |
| 0x18 | 16 | UUID | the platform the bank was built for: the PS2's id (first word 0xAAEAC9AC, the 16 bytes at 0x4C1560 in the program). The loader compares it and then ignores the result | [C 0x29F08C, data 0x4C1560; D] |

### 4.3 `RwaWaveDict` (0x30 bytes, at +0x2C)

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | | UUID pointer (not used in the file: left-over value) | [C struct rwacore.h; D] |
| 0x04 | 4 | off | the bank's name ("fe", "high", "low", the route's id ...) | [C 0x29E740; D] |
| 0x08 | 4 | u32 | flags of the id | [C] |
| 0x0C | 4 | off | the wave list's **last** node | [C 0x29E740] |
| 0x10 | 4 | off | the wave list's **first** node | [C 0x29E740] |
| 0x14 | 4 | off | list data (0) | [C] |
| 0x18-0x2F | | | run-time fields (flags, list link, the sound-memory handle and size), rewritten on load | [C struct] |

**List node** (0xC bytes): +0x00 previous node, +0x04 next node, +0x08 the wave, all file offsets. Walk from the
first node by "next" until the last [C 0x29E740].

### 4.4 `RwaWave` (0x5C bytes)

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | off | UUID (16 bytes) | [C 0x29E740] |
| 0x04 | 4 | off | the wave's name (`eng_4317`, `Beep` ...) | [C; D] |
| 0x08 | 4 | u32 | id flags | [C] |
| 0x0C | 4 | | wave type callbacks (set on load) | [C] |
| 0x10 | 0x1C | | **format**, `RwaWaveFormat`: +0x00 sample rate (2,000 to 44,100 Hz on the disc), +0x04 data type as an index (0 = the PS2 ADPCM in every wave; turned into a UUID pointer on load), +0x08 length in bytes (whole 16-byte frames), +0x0C bits (4), +0x0D channels (1 in every wave), +0x10 / +0x14 misc data, +0x18 flags | [C struct, 0x29E740; D all 1,575] |
| 0x2C | 0x1C | | **target format** (the played format): the same values | [C; D] |
| 0x48 | 4 | u32 | uncompressed length (0) | [C struct] |
| 0x4C | 4 | u32 | the wave's offset in the wave data (in sound memory after loading) | [C `_rwaWaveDictFixupWaveCallback`; D] |
| 0x50 | 4 | | state (run time) | [C] |
| 0x54 | 4 | u32 | flags: 2 = positional (3D); 4 or 6 on the disc | [C 0x294EAC for 2; D] |
| 0x58 | 4 | | owner (run time) | [C] |

`tools/modding/audio_info.py <bank>` lists the waves; `--wav <bank> <index> <out.wav>` decodes one.

## 5. `MUSIC.RWS`

A 0x809 chunk (RW 3.6.0.3 stamp) holding a 0x80A part (0x54 bytes) and a 0x80C part (4 bytes), named as a dummy sound
bank [D all 36]. The program has no string naming it [C rodata], so the game never opens it [L]. Changing it does
nothing.

## 6. PS-ADPCM

The PS2 sound chip's format, used by every wave and stream here. Data is a run of 16-byte frames, each giving 28
samples of 16 bits:

| Offset | Size | Meaning | Evidence |
|---|---|---|---|
| 0 | 1 | low nibble: shift 0-12; high nibble: filter 0-4 | [P src/audio/spu2_mixer_impl.cpp; D every frame header on the disc is in range] |
| 1 | 1 | flags: bit 0 end of the sample (jump to the loop address), bit 1 repeat (keep playing from the loop address), bit 2 loop start (set the loop address here) | [P spu2_mixer_impl.cpp] |
| 2 | 14 | 28 samples of 4 bits, low nibble first | [P] |

Decoding: each nibble is sign-extended, shifted left 12, shifted right by `shift`, plus the filter's prediction from
the last two samples: `s = n + (s1 * f0 + s2 * f1 + 32) >> 6`, clamped to 16 bits, with (f0, f1) = (0, 0), (60, 0),
(115, -52), (98, -55), (122, -60) for filters 0-4 [P spu2_mixer_impl.cpp; the same in `audio_info.py`].

On the disc, the last frame of every bank wave has flags 0x3 (end + repeat) [D all 1,575]: the voice then jumps to its
loop address. RW Audio points a one-shot wave's loop address at a silent block (sound memory 0x5010) and a looping
wave's at its loop start [C docs/audio-irx.md, the IOP's opcode 0x14], and 774 waves carry a loop-start flag inside
[D]. Every stream frame has flags 0x2 and none an end flag [D all 108 files]: the stream's voices loop over their
ring in sound memory, which the IOP refills and patches (opcode 0x46) [C docs/audio.md §3.1].

## 6.1 What a rebuilt file may change (b3modtool)

Checked against every file on the US disc [D], and the rules `b3modtool audio import` writes by:

- **Banks.** The waves' data follow each other in list order, each on a 0x80-byte boundary; the bytes between a
  wave's end and the next wave are left-overs (zeros, or other bytes in some banks: `SINGLE.AWD`, `ROADRAGE.AWD`, the routes' `SOUND.AWD`, ...) and some gaps, and the space after
  the last wave, are larger than the alignment needs. A wave of a new length: its two format lengths (+0x18 and the
  target format's +0x34) change, the waves after it move by whole 0x80 units (their +0x4C offsets change), each keeps
  the bytes it had after it beyond its alignment, and the header's data size (+0x14) changes. The header itself (its
  size, the names, the list) does not move. Flags (+0x54), rate and target format stay.
- **Wave frames.** 774 waves loop: their first frame has flags 0x6 (loop start + repeat), the others 0x2, the last
  0x3. The other 801 have 0x2 and a last 0x3. No wave sets a loop start anywhere but its first frame. A new wave keeps
  that pattern (first frame's flags from the original, 0x3 last).
- **Streams.** The 0x80F data is the segments' blocks back to back; each sub-stream's channel run in a block is full
  PS-ADPCM up to the segment's size table, then **padding frames** `00 02 00 ... 00` (silence, flags 0x2) to the end
  of the block [D: every padded run]. A segment of a new length: its size (+0x18, whole blocks), its size table and
  the offsets (+0x1C) of the segments after it change, and so do the 0x80F and 0x80D chunk sizes. The description
  chunk (0x80E) keeps its size; its unused bytes are kept.
- **The sub-stream's wave format length** (+0x08) is not the stream's length: in 153 of the 164 sub-streams it is
  segment 0's size-table entry for that sub-stream (all 33 files with several segments, and most single-segment
  files), in the other 11 (`_FEMAIN.RWS` and ten routes' `CRASH3.RWS`) segment 0's whole size in blocks. A rebuild
  keeps that relation when segment 0 changes length [D].

## 7. Pitfalls

- **The game asks for segments and waves by number.** A song is segment n of `_EATRAX0/1` (22 each), a movie's sound
  is a fixed segment index of `MOVIE.RWS` ([movies.md](movies.md)), crash streams are numbered files. A new stream must
  keep the segment count and order; a new bank should keep its waves, their order and names [L; how the game picks
  bank waves is not traced here, G].
- **Stream data layout.** Keep 32 kHz stereo, 0x1000-byte channel runs, the block size and the sub-stream count of the
  file you replace: the stream code sizes its IOP ring from the block size and plays each channel on its own voice
  [C docs/audio.md §2.2]. Other rates are untested [G].
- **The header area is a memory image.** In an `.AWD`, everything is at an offset from the file start and the loader
  only adds the load address; there is no slack for a longer name or more waves except up to the header size, and
  callers pass a fixed header buffer to `LoadBank` (0x800 for engine banks, 0x1000 for route banks, 0x2000 for the
  crash bank); a header larger than the caller's buffer fails the load (`INSUFFICIENTWAVEMEMORY`) [C 0x29EF00; the
  buffer sizes from the `LoadBank` calls, L].
- **Sound memory is 2 MB** for everything loaded at once (the shared, mode, route, crash, car banks and the stream
  rings); the first 0x5400 bytes are reserved [C docs/audio.md §2.1]. A larger bank may not fit.
- **Keep the frame flags.** End every bank wave with a 0x3 frame as the disc does; a missing end flag plays past the
  wave into the next one. `b3modtool audio import` does this for you.
- **Left-over bytes are not padding.** Both formats carry uninitialised bytes from the authoring tool (the end of the
  0x80E chunk, AWD +0x05 / +0x10, pointer fields). They are ignored by the game and differ between files; a tool that
  writes zeros there makes a file that works but is not byte-identical.
- **Reverse routes have no own sounds**: `_V2` folders lack `SOUND.AWD`, `E_DJRACE.RWS` and `CRASH*.RWS`; the game uses
  the `_V1` folder's [C 0x289EE0, 0x27B820].
