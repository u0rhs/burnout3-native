# Textures: texture dictionaries (TXD) and PS2 rasters

Every picture the game draws is a PS2 **raster**: the RenderWare raster structure (`RwRaster` and its PS2 extension)
followed by the game's own texture fields (Criterion's `CGtTexture`), stored with file offsets in place of pointers,
plus the exact image transfers the PS2 sends to the graphics chip (the GS). Rasters appear in two kinds of file:

- **Texture dictionaries**: `DATA/GLOBAL.TXD` (194 rasters, 1.1 MB: the HUD, race graphics, shared art) and
  `DATA/FRONTEND.TXD` (221 rasters, 8.5 MB: the menus, car pictures, event art).
- **Embedded in other files**: each track's `STATIC.DAT` (9,240 rasters over the 37 routes) and `ENVIRO.DAT` (148:
  four per route), each car file (`PVEH/*/CAR*.BGV` / `.BTV`: one atlas each, 107 in all) and `DATA/LOADSCRN.BIN` (40
  loading screens, 512 x 256). Those files are documented on their own pages ([track.md](track.md), [car.md](car.md),
  [index.md](index.md)); the raster inside them is the one described here.

The fonts are not in any disc TXD; they come with the program itself [L docs/native-renderer.md, "HeadFont ... on no disc TXD"].

Evidence tags: see [index.md](index.md#evidence-tags).

## Status

| | |
|---|---|
| Decoded by the port | yes, every raster on the disc: 415 of 415 in the TXDs, 9,240 + 148 + 107 + 40 elsewhere [D `b3assetdump census`, all files] |
| Decoder | `src/b3asset/raster.cpp`, `include/b3asset/raster.h` (raster and TXD), `src/b3asset/gs_memory.cpp` (the GS memory model) |
| Tool | `b3modtool txd list / export / import` ([b3modtool.md](../b3modtool.md)); `b3assetdump txd <file> <dir>` (PNG dump) |
| Round trip | **importable** for `GLOBAL.TXD` and `FRONTEND.TXD`: export -> PNG -> import gives every file back byte for byte (2 of 2 files, 415 of 415 rasters, `b3modtool roundtrip`). The rasters inside tracks, cars and `LOADSCRN.BIN` write back identically too (9,535 of 9,535, `roundtrip <disc> rasters`), but those files have no import command yet: export only |
| Alternative | the port also loads PCSX2-style HD texture packs (`src/b3asset/hdpack.cpp`, docs/native-renderer.md §18): replacement images keyed by a hash of the texture, without changing any file |

## Not RenderWare binary streams

The PC RenderWare tools write texture dictionaries as RW binary streams (chunk id, chunk size, library version per
chunk). **These files are not that.** They are memory images of Criterion's own containers: a small dictionary header,
then each raster's in-memory structure, written out with offsets that the game turns into pointers after loading
("fix-up"). RenderWare's stream reader (`RwTexDictionaryStreamRead` 0x1E0448 is in the program) is not used for them
[C: the loader below]. Tools that expect RW chunks will not open them.

## Texture dictionary file (`DATA/*.TXD`)

A `CGtDictionaryBase` image [C `CGtDictionaryBase::FixUp` 0x21AAF0]. All integers little-endian.

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | `0x543C0000` in both files | [D both files] |
| 0x04 | 4 | u32 | `0xBCDEED81` in both files; not read by the loader. A type or format tag [G] | [D], [C FixUp skips it] |
| 0x08 | 4 | u32 | entry count n (194, 221) | [C 0x21AAF0] |
| 0x0C | 4 | u32 | offset of the directory (from the file start); 0x10 in both files | [C 0x21AAF0] |
| dir + 16 i | 4 | u32 | entry key: i + 1 in both files | [D] |
| dir + 16 i + 4 | 4 | u32 | 0 | [D] |
| dir + 16 i + 8 | 4 | u32 | offset of raster i (from the file start); fixed up into a pointer | [C 0x21AAF0] |
| dir + 16 i + 12 | 4 | u32 | 0 | [D] |

After the directory: zero padding to a 64-byte boundary, then the rasters one after another, each followed by its
upload packets and image data.

**Loading.** `GLOBAL.TXD` is loaded once at start-up into its own memory block by the boot sequence
(`func_00133190`, state 3: `QueueLoadRequest("Data/Global.txd")`, then `CGtDictionaryBase::FixUp` and
`CGtTexture::FixUp` 0x2514C0 for every entry through `CGtDictionary<CGtTexture>::GetAt` 0x131AA0) [C nonmatching
func_00133190.cpp]. `FRONTEND.TXD` is loaded by the front end the same way [L]. Code finds a raster either by its
index (`GetAt`) or by name, comparing the name at raster +0xA8 case-insensitively in directory order
(`strcasecmp`, inline in `CB3GraphicsManagerBase::Construct` 0x19EE40) [C].

## The raster (`CGtTexture`, 0xA8 bytes + name)

Offsets are from the raster's start; "rel." offsets are relative to the raster start too (that is how
`CGtTexture::FixUp` 0x2514C0 relocates them [C]). Field names in the RW columns follow the RenderWare 3 PS2 ("sky2")
headers [L]; everything else is from the decomp and the disc.

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | rel. | RwRaster parent (0) | [C FixUp], [D] |
| 0x04 | 4 | rel. | cpPixels: the first image byte inside the upload (always the first transfer's packet + 0x50) | [C FixUp], [D 415 of 415 TXD rasters] |
| 0x08 | 4 | rel. | palette: the CLUT bytes inside the upload (0 for direct colour) | [C FixUp], [D] |
| 0x0C | 4 | u32 | width (pixels) | [P raster.cpp] |
| 0x10 | 4 | u32 | height | [P] |
| 0x14 | 4 | u32 | depth: 4, 8 (paletted), 24, 32 | [D] |
| 0x18 | 4 | u32 | stride | [L RW] |
| 0x1C | 4 | s16 x2 | RW offset x / y (0) | [L RW] |
| 0x20 | 1 | u8 | cType: 4 = texture (the decoder's scan key) | [P] |
| 0x21 | 1 | u8 | cFlags (0) | [D] |
| 0x22 | 1 | u8 | privateFlags (0) | [D] |
| 0x23 | 1 | u8 | cFormat = RW raster format >> 8: 0x05 8888, 0x06 888, 0x25 PAL8+8888, 0x45 PAL4+8888; +0x80 when the raster has mip levels | [D both TXDs] |
| 0x24 | 4 | rel. | the **upload block** (below) | [C FixUp], [P] |
| 0x28..0x33 | 12 | u32 x3 | original width, height, stride (copies of the above) | [L RW], [D] |
| 0x34 | 8 | | sky2 raster extension starts: DMA reference counts | [L RW] |
| 0x3C | 8 | u64 | **TEX0**: the GS texture register (base pointer TBP0, buffer width TBW, pixel format PSM, log2 width / height TW / TH, TCC, CLUT format CPSM, CLUT storage mode CSM, CLUT entry offset CSA). TBP0 and CBP are relative to the texture's slot in the texture cache | [P raster.cpp] |
| 0x44 | 4 | u32 | palOffset: the CLUT's GS block, relative like TBP0 | [P] |
| 0x48 | 2 | u16 | mipmap K / L (LOD parameters) | [L RW] |
| 0x4A | 1 | u8 | max mip level << 2: levels = (byte >> 2) + 1, at most 7 | [P] |
| 0x4C | 8 | u64 | MIPTBP1: base pointer and width of levels 1..3 (20 bits each) | [P] |
| 0x54 | 8 | u64 | MIPTBP2: levels 4..6 | [P] |
| 0x5C | 4 | u32 | sysMemSize (bytes of image data) | [L RW] |
| 0x60 | 4 | u32 | sysMemPalSize (bytes of CLUT data) | [L RW] |
| 0x64 | 4 | u32 | texture cache size in words | [L RW] |
| 0x68 | 1 | u8 | cache packets | [L RW] |
| 0x6A | 1 | u8 | upload flags: 1 new-style packets, 2 PSMT8 sent as CT32, 4 PSMT4 sent as CT16 (seen: 0x1 direct colour, 0x3 8-bit, 0x5 4-bit) | [P], [D all rasters] |
| 0x6C | 4 | rel. | an offset fixed up by FixUp; 0 in every TXD raster | [C FixUp], [D] |
| 0x70..0x8B | 28 | rel. x7 | seven offsets fixed up by FixUp; 0 in every TXD raster | [C FixUp], [D] |
| 0x90 | 16 | u16 x8 | palette table: the CLUT offset (GS blocks past palOffset) of each paint variant (cars, car pictures); 8-paint rasters hold {0, 4, 16, 20, 8, 12, 24, 28} | [C `CB3VehicleRenderer::SetTexture` 0x21FF60, `Gt2dRenderer::SetTexture` 0x219A88] |
| 0xA0 | 1 | u8 | number of palettes (0 direct colour, 1, or up to 8) | [C `CB3RaceCar::Prepare` clamps the colour index with it] |
| 0xA1 | 2 | u8 x2 | 08 02 (4-bit), 10 10 (8-bit), 40 20 (8 palettes): the CLUT's width and height in entries [G] | [D] |
| 0xA4 | 4 | u32 | unknown | |
| 0xA8 | 32 | char[] | name, NUL-terminated ASCII (e.g. `16_curve`); compared case-insensitively | [C 0x19EE40] |

### The upload block (raster + [0x24])

The bytes the PS2 sends to the GS to put the texture into video memory: a list of DMA transfers, each a GS
host-to-local image transfer.

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | transfer count t (1..3 on the disc) | [P], [D] |
| 0x04 | 4 | u32 | a stale pointer from the tool that wrote the file; ignore | [D] |
| 0x08 | 4 | u32 | total transfers: t, or t + 1 when the CLUT rides in the last level's transfer (its extra entry is unused) | [P raster.h] |
| 0x0C | 4 | u32 | 0 | [D] |
| 0x10 + 64 k | 16 | DMA tag | DMAcnt, qwc 2: the next two quadwords go to the GS | [P] |
| + 0x10 | 16 | GIF tag | 1 loop, register A+D | [P] |
| + 0x20 | 16 | A+D | **BITBLTBUF** (register 0x50): destination base pointer (relative), width, format of the transfer | [P] |
| + 0x30 | 16 | DMA tag | DMAref: qwc and source offset (rel. raster, the `+0x34` word FixUp relocates [C]) of the GIF packet below | [P], [C FixUp] |

Each DMAref'd packet is a GIF tag with three A+D writes, **TRXPOS** (0x51), **TRXREG** (0x52: the rectangle's width
and height in the *transfer's* format), **TRXDIR** (0x53, 0 = host to GS), then a GIF IMAGE tag and the pixel bytes
[P raster.cpp `run_gif`, D].

### Formats on the disc

From `tools/modding/asset_raster_census.py <disc>` (counts only):

| File kind | Rasters | Formats (CLUT format) | Mip levels | Palettes |
|---|---|---|---|---|
| TXD (2 files) | 415 | T8 243, T4 163 (CLUT CT32), CT32 6, CT24 3 | 1 for 399; 2..5 for 16 | 0: 9, 1: 349, 8: 57 |
| STATIC.DAT (37) | 9,240 | T4 8,109, T8 1,131 (CT32) | 1..6 (5 is the most common) | 1 |
| ENVIRO.DAT (37) | 148 | T8 111 (CT32), CT32 37 | 1 | 0 or 1 |
| car BGV (67) | 67 | T8 (CT32), 256 x 256 | 1 | 1 or 8 |
| car BTV (40) | 40 | T8 (CT32) | 1 | 1..6 |
| DATA/LOADSCRN.BIN | 40 | T8 (CT32), 512 x 256 | 1 | 1 |

No raster on the disc uses CT16 pixels, a CT16 CLUT or CLUT storage mode CSM2 [D all 9,950 rasters].

## How the image is stored: the "swizzle" is the GS's own memory layout

The GS keeps each pixel format in its own block-and-column arrangement in video memory. To upload a paletted texture
faster, RenderWare sends it as if it were a different format:

- an 8-bit texture (PSMT8) is sent as **32-bit pixels** (PSMCT32): four texels per "pixel", arranged so that, once in
  GS memory, reading the same memory as PSMT8 gives the right texels (upload flag 2);
- a 4-bit texture (PSMT4) is sent as **16-bit pixels** (PSMCT16) the same way (flag 4);
- the CLUT is often part of the same transfer (the last level's).

So the bytes in the file are not in reading order for either format. The port does not unscramble them with
hand-written swizzle tables: it **replays the transfers into a model of the 4 MB GS memory** (`gs_memory.cpp`, the GS
User's Manual page / block / column layouts) and then reads each texel at its (x, y) with the TEX0 / MIPTBP formats,
exactly as the GS samples it [P raster.cpp `decode_raster_clut`]. The import does the reverse with the same model: it
replays the upload, writes the new texels and CLUT where they are sampled from, and reads every image byte of the
upload back from the place the transfer had put it (modtool1 `src/b3modtool/txd.cpp`). File size and every byte
outside the image data stay the same.

**Mip levels.** Level 0 is at TEX0.TBP0 / TBW; levels 1..6 at MIPTBP1 / MIPTBP2, each half the size of the one above
(at least 1 x 1) [P].

## The CLUT (palette)

- 16 entries (T4) or 256 (T8), each a 32-bit colour **R, G, B, A** (byte order in memory: R at the lowest address)
  [P `gs_color_to_rgba8`].
- **CSM1 entry order.** A 256-colour CLUT is stored as a 16 x 16 block in which, within every 32 entries, entries
  8..15 and 16..23 are swapped: the stored position of entry e is e with its bits 3 and 4 exchanged
  (`j = (e & 0xE7) | (e & 0x08) << 1 | (e & 0x10) >> 1`). A 16-colour CLUT is 8 x 2, and TEX0.CSA picks which group
  of 16 is used [P raster.cpp].
- **Several palettes.** Car atlases and some car pictures carry up to 8 CLUTs (paint colours). Palette k is at
  palOffset + table[k] (raster +0x90) GS blocks; a k beyond the count (+0xA0) gives the base CLUT [C 0x21FF60]. The
  table is not linear (8 paints: 0, 4, 16, 20, 8, 12, 24, 28).

## Alpha

PS2 alpha runs from 0 to **0x80 = opaque**. Values above 0x80 exist (blends that brighten) [D: the decoder records
`max_alpha`]. For PNG, the tools map stored alpha a to 2a (255 for 0x80 and above); on import a PNG alpha A becomes
(A + 1) / 2, at most 0x80 ([b3modtool.md](../b3modtool.md)). TEX0.TCC = 0 (three CT24 rasters) means the texture has
no alpha: it draws opaque [P].

## Pitfalls

- **Size, format and palette count are fixed.** An imported picture must have the raster's width and height; the
  format (T4 / T8 / CT32) and the number of mip levels stay. More colours than the format holds are reduced (16 for
  T4, 256 for T8).
- **Rasters with several palettes keep their CLUTs** (the other paints index the same entries), so new colours are
  mapped to the nearest existing entry.
- **Keep the names.** Code looks some rasters up by name; others by their index in the dictionary. Do not reorder or
  rename entries.
- **Two CLUT entries of one colour.** Which of the two a pixel used cannot be seen in a PNG; the import keeps the stored
  index for pixels you did not change. That is why only 320 of 415 TXD rasters come back identical "from pixels
  alone", while all 415 come back identical through import.
- **Textures inside tracks and cars** decode and write back, but there is no import command for those files yet (mod
  kit part 3c). A replaced track file is also read by the renderer's own readers, which still read the disc's copy
  until part 1b ([DESIGN.md](../DESIGN.md) §1.2).
- **Ship a patch, not the TXD.** A whole TXD in a mod is mostly the game's bytes; use `b3modtool diff`
  ([b3patch.md](../b3patch.md), DESIGN.md §8).
