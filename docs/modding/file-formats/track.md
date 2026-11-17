# Tracks: STATIC.DAT, STREAMED.DAT, ENVIRO.DAT

Each route is a folder `TRACKS/<area>/<route>/`: 37 folders on the US disc, in the areas `US` (13), `EU` (16) and
`AS` (8) [D]. A route name is the course and its direction or variant (`C1_V1`, `C1_V2`, ...; `C` / `M` / `P` courses
[G meaning of the letters]). Which folder belongs to which event comes from `TRACKS/TLIST.BIN` (see
[index.md](index.md)).

| File | Holds | Size on the disc | Page |
|---|---|---|---|
| `STATIC.DAT` | everything resident for the whole race: materials, all the route's textures, backdrops and water, instance and prop models, prop placements, the table of streamed units | 0.75 to 7.9 MB (37 files) | this page, [docs/track-format.md](../../track-format.md) §1 |
| `STREAMED.DAT` | the road and scenery in **units** streamed in as the car moves: per unit a high-detail block and a LOD + collision block | 0 to 29.6 MB (US/C5_V1's is empty) | this page, [docs/track-format.md](../../track-format.md) §2 |
| `ENVIRO.DAT` | sky, sun, fog and specular settings and four sky textures | 192 or 320 KiB | this page, [docs/sky-enviro.md](../../sky-enviro.md) §S.1 |
| `GAMEDATA.BGD` | the route's game data (start lines, checkpoints, AI and traffic paths [L]) | | [gamedata-bgd.md](gamedata-bgd.md) |
| `SOUND.AWD`, `MUSIC.RWS`, `CRASH*.RWS`, `E_DJRACE.RWS` | the route's sounds and streams | | [audio.md](audio.md) |
| `TRACKS/*.M2V` | the route preview movies of the menus | | [movies.md](movies.md) |

Evidence tags: see [index.md](index.md#evidence-tags). The byte-level spec of STATIC.DAT and STREAMED.DAT, with its own
evidence for every field, is [docs/track-format.md](../../track-format.md); this page is the summary for modders and
does not repeat its tables.

## Status

| | |
|---|---|
| Decoded by the port | yes: 37 of 37 routes, every part (streamed units, LOD, backdrops, water, instances, props, line materials): 82,008 meshes, 181,359 batches, 10.2 million vertices, 9,240 textures [D `b3assetdump census`]. The game's code loads the files natively; the port's renderer also reads STATIC.DAT and STREAMED.DAT itself for the whole-track view |
| Decoders | `src/b3asset/track.cpp`, `include/b3asset/track.h` (STATIC + STREAMED), `src/b3asset/enviro.cpp`, `include/b3asset/enviro.h` (ENVIRO.DAT and the sky model), `src/b3asset/vif.cpp` (mesh packets), `src/b3asset/raster.cpp` (textures) |
| Tools | `b3modtool track list` / `track export` (glTF 2.0 or OBJ by section: the streamed units' high and LOD meshes, instances, props, backdrops, water, materials, textures, the sky textures, a sidecar and a report of what is not exported: [b3modtool.md](../b3modtool.md) "track list / export"); `b3modtool track import` (the edited glTF / GLB and texture PNGs back as patches of STATIC.DAT / STREAMED.DAT: "track import"); `b3assetdump track <route dir> [dir]` (statistics, a top-down picture), `b3envdump <route dir> [dir]` (ENVIRO.DAT parameters, the fog curve, the sky textures) |
| Round trip | **importable, the visual meshes** (mod kit part 3c, trackimport1): export -> import unedited gives STATIC.DAT and STREAMED.DAT byte for byte for 37 of 37 routes, and every one of the 140,896 meshes the files name is written back byte for byte by the packet writer (`build/track_import_check`, below). Export census: 37 of 37 routes (2,838 sections) valid glTF with the renderer's triangle counts (`build/model_export_check`). Not exported or imported: line meshes (type 3), the collision trees and irradiance (layout not decoded; kept as they are), the generated sky dome, ENVIRO.DAT (its sky textures export only). The textures inside STATIC.DAT are imported through their PNGs (the raster write-back, 9,240 of 9,240 identical: `b3modtool roundtrip <disc> rasters`) |

## STATIC.DAT in brief

One image, loaded whole and fixed up by `CB3StaticTrackData::FixUp` 0x15E8E0 from `CB3StaticTrack::Prepare` 0x332E00
[C, docs/track-format.md §1].

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | 0x27 (39) in all 37 files: a version [G] | [D] |
| 0x04 | 4 | u32 | file size | [D all 37] |
| 0x08 / 0x0C | 4 / 2 | off / s16 | materials (0x28 bytes each: type 0..7, texture list, GS TEST / ALPHA, animation) / count | [C 0x15E8E0] |
| 0x16 / 0x18 | 2 / 4 | s16 / off | texture count / table of texture offsets (each a raster, [txd.md](txd.md)) | [C] |
| 0x1C..0x30 | | | four backdrop tables (scenery, the route's signs, two water surfaces) | [C] |
| 0x34..0x3C | | | instance types and prop types (object-space models) | [C] |
| 0x40..0x50 | | | prop placements (4x4 matrices) and per-unit prop lists | [C] |
| 0x54 / 0x58 | 2 / 4 | s16 / off | unit count / the unit table (16 bytes per unit: offsets and sizes of its two blocks in STREAMED.DAT) | [C] |

Full field list: [docs/track-format.md](../../track-format.md) §1.1 (header), §1.2 (materials), §1.3 (textures),
§1.4 (backdrops), §1.5 (instance and prop types).

## STREAMED.DAT in brief

No header: per unit, a **high block** then a **LOD block**, back to back, each a multiple of 32 KB (32 to 160 KB)
[D, docs/track-format.md §2.1]. The game keeps 9 high blocks and 17 LOD blocks around the car resident and loads the
others as it drives (`UpdateStreamHighFSM` 0x3029B0, `UpdateStreamLowFSM` 0x302AE0) [C].

- The high block: the unit's meshes (array at +0x50) and its render table (+0x60), which maps material slots to
  meshes [C §2.3].
- The LOD block (`CB3StreamedTrackLODAndCollisionData`): the low-detail meshes, the placements of instance models,
  and the unit's **collision** (a `CGtCollisionKDTree` at +0x90) and per-vertex lighting [C §2.4].

Meshes are DMA packets of VIF batches (triangle strips with restart flags, up to 80 vertices each) in seven vertex
layouts, one per material type: [docs/track-format.md](../../track-format.md) §3. Coordinates: §4.

### How the packets are written

Every mesh packet of the 37 routes (140,896 meshes: the units' high and LOD meshes, the backdrops and water, the
instance and prop models) has one form, which `track import` writes back byte for byte [D census
`build/track_import_check`; P `src/b3modtool/trackimport.cpp`]:

- chain 1 holds batch 0, chain 2 (when there is one) batches 1..; each chain is one DMA `RET` packet (`0x60000000 |
  qwc`, 0 in its second word, two NOPs in its upper half);
- a batch: for each attribute `STMOD 0`, `STCYCL 3/1` (`1/1` for the type-1 normals), `UNPACK` with FLG (unsigned for
  the colours): positions `V4-32` at 0 (x, y, z, the w lane 0x8000 = ADC or 0); then at 1 `V2-16` texture coordinates
  (4.12), `V2-32` (instance models: floats) or, type 2, `V4-8` signed normals + intensity; at 2 `V4-8` / `V3-8` colours
  or, type 5, a `V4-32` lighting vector; type 1's `V4-8` signed normals + intensity at 204; then NOPs to the quadword,
  `ITOP n`, and in chain 2 `MSCNT`, NOPs to the quadword;
- the three transposed blocks (type-2 and type-1 normals, type-5 lighting: element 4g + c holds component c of
  vertices 4g .. 4g + 3 in its four lanes) have 4 x ceil(n / 4) elements, the lanes past n zero;
- before each `STMOD` 1 to 4 NOPs, so that every `UNPACK`'s data starts on a quadword; the data padded to 4 bytes with
  zeros;
- at most 80 vertices per batch (68 for type 1, whose normals start at qw 204; 76 for the instance models).

The layouts by material type: 0 and 6 (and 3, the lines) positions, [texture coordinates,] RGBA; 1 positions, [texture
coordinates,] RGB, normals at 204; 2 positions, normals at 1, RGB; 4 positions, float texture coordinates; 5 the same
and the lighting vector.

**Where the packets sit, and the limits.** In STREAMED.DAT a mesh's chains lie in its unit's block after the mesh array,
64-byte aligned; a high block ends with its packets, a LOD block with its instance placements after its packets, then
zeros to the block's size (a multiple of 32 KB). A packet that grows moves into space freed by other rewritten packets
or to the block's free end; a block that is full grows by 32 KB, and the blocks after it and the unit table (STATIC.DAT
+0x58: offsets and sizes) follow. The game loads a unit's blocks into fixed slots: 10 high-detail slots of 0x28000
(160 KB) and 18 LOD slots of 0x18000 (96 KB) in the race layout, 19 and 35 in the two-stream one [L nonmatching
memMgrInit; C `GetBlockSize` 0x3E7850: blocks 6 and 7]; the largest blocks on the disc are exactly those sizes.
STATIC.DAT is loaded whole into memory block 5: 0x780000 bytes (7.5 MB) in the race layout, 0x580000 (5.5 MB) in the
two-stream one [C 0x3E7850, 0x332E00]; the import lets it grow (packets appended, the size word +0x04 following, padded
to 2 KB) up to 0x580000, or not at all for the routes already larger.

**Boxes.** Each CB3TrackMesh carries a box (three half axes and a centre, +0x00 .. +0x3F; the w lanes hold the chain
offsets and two constants) that the game's VU0 culling tests; a backdrop object (CB3StaticTrackObject +0x00 .. +0x3F)
has one of the same form around its meshes (on the disc equal to its meshes'), an instance or prop type a local
axis-aligned box (+0x00 the largest x y z, +0x10 the smallest). The import grows them where an edited mesh reaches out
(the same axes, longer, the centre moved).

## ENVIRO.DAT

One serialized `CB3EnvironmentData` image: an 0xB0-byte header and four rasters. Loaded by
`CB3EnvironmentManager::Prepare` 0x2BCA20 (route path + `enviro.dat`) and fixed up by `CB3EnvironmentData::FixUp`
0x2B7110, which relocates only the four texture offsets [C matched func_002BCA20.cpp, func_002B7110.cpp].

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 16 | f32 x4 | fog colour, RGB 0..1 | [C `CB3Fog::Render`] |
| 0x10 | 4 | f32 | near / fog distance scale: near plane = clamp(0.0003 x value, 0.003, 0.4) (200..1500 per route) | [C 0x240EF0], [L] |
| 0x14 | 4 | f32 | fog maximum (0.1..0.6) | [C `CB3Fog::GenerateClut` 0x2413E0] |
| 0x18 | 4 | f32 | fog of the sky pixels (0 in all files) | [C] |
| 0x1C | 4 | f32 | full-screen specular intensity (0.6..1.1) | [C] |
| 0x20 | 16 | f32 x4 | 1, 0, 0, 1 in all files | [D]; [G] unused |
| 0x30 | 48 | | a second block of the same shape; not used by Prepare | [C], [G] |
| 0x60 | 16 | f32 x4 | specular colour | [C] |
| 0x70 | 16 | f32 x4 | sun colour | [C] |
| 0x80 | 12 | f32 x3 | the direction sunlight travels (unit length); the game negates it for the sun position and the track's light | [C] |
| 0x90 | 4 | f32 | sun corona scale (0.15..0.35) | [C/G] |
| 0x94 | 4 | u32 | 0 | [D] |
| 0x98 | 4 | off | sky gradient raster, 32 x 32 CT32 (sky colour by height and the sun-glow mask) | [C FixUp] |
| 0x9C | 4 | off | cloud panorama, 1024 x 256 (US: 1024 x 128) 8-bit | [C] |
| 0xA0 | 4 | off | sky for the cars' environment map, 64 or 128 pixels, 8-bit | [C] |
| 0xA4 | 4 | off | sun corona / flare, 128 x 256 8-bit (0: no sun) | [C Prepare] |
| 0xA8 | 4 | u32 | 0x45440002 in all files: version or tag [G] | [D] |
| 0xAC | 4 | u32 | file size | [D all 37] |

How each value is used (sky dome, backdrop composition, fog curve, sun blob): [docs/sky-enviro.md](../../sky-enviro.md)
and `include/b3asset/enviro.h`.

## Pitfalls

- **Collision lives in STREAMED.DAT.** The LOD blocks hold the collision trees: a changed STREAMED.DAT can change how
  the cars drive and crash, so a mod replacing it is gameplay-changing ([DESIGN.md](../DESIGN.md) §6). Changing only
  textures or ENVIRO.DAT colours is visual. `track import` never changes the trees (their bytes stay where they are in
  their block), so its patches are visual: a road raised in the model is drawn higher while the cars drive on the old
  surface.
- **Offsets everywhere.** Every structure is reached through offsets that the loader relocates, each relative to a
  different base (the file, the material, the block, block + 0x98); docs/track-format.md gives the base per field.
  Inserting bytes anywhere means rewriting all of them; a texture edit of the same size changes nothing else.
- **Block sizes are fixed.** Streamed blocks are loaded into fixed-size slots (160 KB high, 96 KB LOD, "How the packets
  are written"); a larger block would not fit, so `track import` refuses one.
- **V1 / V2 share most data**, but each folder has its own copies; ENVIRO.DAT is byte-identical between V1 and V2 except
  on US/C2, US/P2 and US/M1 [D docs/sky-enviro.md].
- **The renderer's own readers** read STATIC.DAT / STREAMED.DAT from the disc for the whole-track view until mod kit
  part 1b; with a modded track file the game and that view can disagree ([DESIGN.md](../DESIGN.md) §1.2).
- **The sun is switched off** for four track ids by code, whatever ENVIRO.DAT says [C 0x2BCA20].
