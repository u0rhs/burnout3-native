# Cars: vehicle files (BGV, BTV)

Every vehicle's model is one file in `PVEH/<class>/`:

- `CARn.BGV`: the cars you drive (67 files, 370 to 467 KB), in the classes `COMP` (compact), `CUPE` (coupe), `MSCL`
  (muscle), `SPRT` (sport), `SUPR` (super), `HSPC` and `HEVY` [D; the expansions in brackets are a guess, G].
- `CARn.BTV`: traffic and other non-player vehicles (40 files, 92 to 260 KB), in `COMP`, `HEVY` and `TSPC` [D].

Beside each `.BGV` are `CARn.HWD` and `CARn.LWD`: the car's engine sounds (wave banks, high and low detail), documented
in [audio.md](audio.md). Which file is which car comes from `PVEH/VLIST.BIN` (see [index.md](index.md)). One file
holds the body, the parts that come off in a crash, the wheels, the texture atlas with its paint colours, the lights,
and the collision hull, so a car file is read by the physics as well as the renderer.

Evidence tags: see [index.md](index.md#evidence-tags).

## Status

| | |
|---|---|
| Decoded by the port | yes: 107 of 107 files at every LOD [D `b3assetdump census`]; the game's own code loads them natively, and the port's renderer also reads them (`scan_cars`) |
| Decoder | `src/b3asset/car.cpp`, `include/b3asset/car.h`; the mesh packets through `src/b3asset/vif.cpp`; shading model `src/b3asset/carshade.cpp` (docs/car-shading.md) |
| Tools | `b3modtool car list` (the cars by the game's names), `b3modtool car export` (glTF 2.0 or OBJ, every LOD and part, the paints, the lights, a sidecar with every field an import needs) and `b3modtool car import` (an edited glTF / GLB and paint PNGs back into the file: [b3modtool.md](../b3modtool.md) "car import", [tutorial 08](../tutorials/08-edit-a-car.md)); `b3assetdump car <file> <dir> [lod] [palette]` (OBJ + preview pictures, for the renderer's developers) |
| Writer | `src/b3modtool/carimport.cpp` (the mesh packet writer, the strip builder, the layout), the paints through `encode_raster_paints` (`src/b3modtool/txd.cpp`) |
| Round trip | **importable** (mod kit part 3c). Export census: 67 of 67 player cars, 268 of 268 LODs valid glTF with the renderer's vertex and triangle counts and bounds (`build/model_export_check`). Import census (`build/model_import_check`): an unedited export imports to the disc's file byte for byte for 67 of 67 player cars (also with every mesh's vertices and triangles shuffled, as a modelling tool saves them); with every mesh's strips rebuilt, 67 of 67 decode-identical (7,022 of 7,022 entries, 268 of 268 LODs) |

## File layout

One memory image, fixed up in place after loading: offsets become pointers. All integers little-endian; offsets are
from the file start unless the table says otherwise.

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | version 0x17 (all 107 files) | [D], [P car.cpp checks it] |
| 0x04 | 4 | u32 | 0 | [D all] |
| 0x08 | 4 | u32 | file size (equal to the file's length in all 107) | [D] |
| 0x0C | 1 | u8 | number of panel frames (0..6) | [C `CB3RaceCar::Prepare` 0x152AF0 copies that many from +0xD00] |
| 0x0D | 1 | u8 | number of wheel frames (0, 4 or 6) | [C same, from +0xB80] |
| 0x10 | 4 | u32 | 3 in every BGV, 2 in every BTV | [D]; meaning unknown |
| 0x14 | 4 | f32 | unknown (2.676 in COMP/CAR1) | |
| 0x18 | 4 | f32 | a per-car scale; each wheel's value is +0x18 x +0x1C[i] (wheel radius [G]) | [C 0x152AF0] |
| 0x1C | 24 | f32 x6 | per-wheel factors (1.0 in COMP/CAR1) | [C 0x152AF0] |
| 0x4C | 20 | u32 x5 | offsets of up to five **LOD blocks** (0 = none); every BGV has 4, every BTV 3 | [C `CB3VehicleRenderer::FixUpVehicleData` 0x220B70, `CB3VehicleLODDataBase::FixUp` 0x220DB0], [D] |
| 0x60 | 4 | u32 | offset of the **texture** (a raster, [txd.md](txd.md)) | [C 0x220B70 -> `CGtTexture::FixUp`] |
| 0x64 | 4 | u32 | offset of a mesh list (format as the LOD lists below) | [C 0x220B70]; purpose [G] |
| 0x68 | 4 | u32 | offset of a second mesh list | [C 0x220B70] |
| 0x6C..0xB7F | | | unknown (non-zero data) | |
| 0xB80 | 64 x n | CGtMatrix3x4 | wheel frames (n = +0x0D): rows right, up, at, position | [C 0x152AF0], [P] |
| 0xD00 | 64 x n | CGtMatrix3x4 | panel frames (n = +0x0C) for the lists A0..A5 | [C 0x152AF0], [P] |
| 0x1060 | 0x604 | CB3ConvexHull | the collision hull (physics) | [C 0x220B70 -> `CB3ConvexHull::Fixup`] |
| 0x1664 | 72 | u32 x18 | offsets of light groups (headlights, brake lights, indicators ...), 0x30 bytes per light: position, facing | [C `CB3EffectsManager::RenderLightCoronas` 0x256690, FixUp 0x220B70] |
| 0x16AC | 18 | u8 x18 | number of lights in each group | [C 0x256690] |
| 0x16C0 | 4 | u32 | offset of a table of 12-byte entries (its +8 word is an offset too); one BTV has it | [C 0x220B70], [D] |
| 0x16C4 | 4 | u32 | an offset (same BTV) | [C 0x220B70] |
| 0x16C8 | 1 | s8 | entry count of +0x16C0 (8 in that BTV) | [C 0x220B70] |
| 0x16DC | 8 | u32 x2 | two engine-sound structures (BGV only), prepared by a sound function (0x2FB240, in the `CB3SoundESM` code) | [C 0x220B70], [D 67 BGV / 0 BTV] [L meaning] |
| (from 0x16F0) | | | LOD blocks, mesh lists, meshes, the texture | [D] |

### LOD block

A LOD block (`CB3VehicleLODDataBase`) is 18 offsets (from the block) to mesh lists, one per part [P car.h]:

| Slot | Name | What it is |
|---|---|---|
| 0 | L0 | the body |
| 1..6 | A0..A5 | panels (bonnet, doors, boot ... that deform and come off), each drawn at its panel frame |
| 7 | B0 | the wheel at rest, drawn at every wheel frame |
| 8, 9 | B1, B2 | the wheel blurred, used above wheel speed 25 / 50 [C `RenderVehicle` 0x21E6F0 draft] |
| 10..17 | C0..C7 | not drawn by the port; unused on the disc [L] |

LOD 0 is the lowest detail, the last one the highest (block slots 0..3 on the BGV files) [P].

### Mesh list and entries

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 1 | s8 | entry count | [C 0x220B70] |
| 0x01 | 1 | s8 | index of the first glass entry (-1: none); entries from there on are drawn blended | [P] |
| 0x02 | 2 | | padding | |
| 0x04 | 4 | u32 | offset of the entries (from the list) | [C 0x220B70] |
| 0x08 | 4 | u32 | 0x47800080 | [D] |

Each entry is 16 bytes:

| Offset | Size | Type | Meaning | Evidence |
|---|---|---|---|---|
| 0x00 | 4 | u32 | offset of the mesh packet (from the entry) | [C 0x220B70] |
| 0x04 | 4 | u32 | offset of a continuation packet (from the entry), 0 = none | [C 0x220B70] |
| 0x08 | 4 | u32 | material word (bits 0x300: glass; byte 3: the renderer texture slot) | [P], docs/car-shading.md §1 |
| 0x0C | 4 | u32 | colour word: one 0x80-based byte twice (0x8080 opaque; glass 0x3434) | [D], [L meaning] |

### Mesh packets

A mesh is a DMA **RET** tag (qwc in the low 16 bits) and a VIF stream for the car's VU1 program, in batches drawn as triangle strips [P car.cpp, vif.cpp]:

- `UNPACK V4-32` at VU address 0: x, y, z, w; w bit 0x8000 = "ADC": do not close a triangle at this vertex (strip
  restart);
- `UNPACK V4-16` unsigned at 1: texture coordinates in 1/4096, and two influence indices, interleaved per group of
  four vertices;
- `UNPACK V3-8` unsigned with an offset at 2: the normal, (b - 128) / 128;
- `STCYCL` 3/1 (or 6/1), `ITOP` = the vertex count, `MSCNT` between batches.

#### How the packets are written

Every packet of the 67 player cars has one form, which `car import` writes back byte for byte [D census
`build/model_import_check`; P `src/b3modtool/carimport.cpp`]:

- the first packet holds batch 0: `NOP`, `STROW` 65536.0 x 4, the batch, `ITOP n`, NOPs to the quadword; no `MSCNT`
  (the renderer kicks it after the DMA call). The continuation holds batches 1.., each ending `ITOP n`, `MSCNT`;
- a batch: `STMOD 0`, `STCYCL 3/1`, `UNPACK V4-32` (n, at 0, FLG); `STMOD 0x11` (offset), `STCYCL 3/1`, `UNPACK V3-8`
  unsigned (n, at 2); `STMOD 0`, `STCYCL 3/1`, `UNPACK V4-16` unsigned (n rounded up to 4, at 1) -- or for the wheel
  lists B0..B2 `STCYCL 6/1` and half that count: texture coordinates only, no influences;
- before each `STMOD` 1 to 4 NOPs, so that every `UNPACK`'s (and `STROW`'s) data starts on a quadword; the bytes of
  vertices past n in the last group of four are 0; the w lane is 0x8000 (ADC) or 0;
- at most 52 vertices per batch; the DMA tag is `0x60000000 | qwc` (RET) with 0 in its second word.

Packets sit after their entries, 16-byte aligned (on the disc mostly at 0x100); a packet that grows is moved into
space freed by other rewritten packets or to the end of the file.

Positions are in the car's own space (panels in their panel frame's, wheels in the wheel frame's). The damage
deformation is computed in the game at run time (`CB3VehicleDeform`, `RenderDeformedObject` 0x21F790, four
quadwords per damage zone sent to VU1), not stored as extra geometry [C docs/car-shading.md §1].

### Texture and paint colours

The texture at +0x60 is one 256 x 256 8-bit atlas (two BTVs have a 512-pixel side) with 1 to 8 CLUTs [D]. Each CLUT is one paint
colour; the paint index of the race car is wrapped by the count at raster +0xA0 [C 0x152AF0], and the renderer picks
CLUT k by the raster's +0x90 table [C `CB3VehicleRenderer::SetTexture` 0x21FF60]. Details: [txd.md](txd.md).

## How the game loads a car

`CB3RaceCar::Prepare` 0x152AF0 (state 2): acquire memory block 1, build the path with
`CB3VehicleList::CreateVehiclePathFromIndex` 0x251F40 (from VLIST.BIN), append `.bgv`, and queue the load with the
block's size; when it is in, `CB3VehicleRenderer::FixUpVehicleData` 0x220B70 relocates the offsets (texture, LOD
blocks, the +0x64/+0x68 lists, lights, the hull, the sound structures), then the wheel and panel frames are copied and
`CB3VehicleDeform::Prepare` runs [C nonmatching Prepare__10CB3RaceCarFv.cpp]. Traffic files (`.btv`) are loaded by the
traffic system (`B3TrafficSystemLoadAndRender` 0x3093E0) [C].

## Pitfalls

- **Size limit.** A player car is loaded into memory block 1, which is 0x72000 bytes (466,944) in every memory layout
  [L `CB3MemoryManagerPS2::Construct` 0x222650]; the largest BGV on the disc is exactly that size. A bigger file
  does not fit.
- **Not just a model.** The collision hull (+0x1060) and the per-wheel values feed the physics: a changed car file can
  change gameplay ([DESIGN.md](../DESIGN.md) §6).
- **What the import keeps.** `car import` rewrites only mesh packets (and, when one grows, its entry's offset and the
  size word +0x08) and the texture atlas's texels and CLUTs. The LOD blocks, lists, entries, frames, lights, hull and
  sound structures are never changed, so the counts the game's FixUp walks stay valid.
- **Damage is not in the file as shapes.** The export carries each vertex's two deformation influences (`_B3_INFL_0`, `_B3_INFL_1`);
  there are no damaged meshes to export as morph targets.
- **The renderer's readers.** The port's renderer scans the car files itself; until part 1b it reads the disc's
  copies, not a mod's ([DESIGN.md](../DESIGN.md) §1.2).
- **Unknown areas** (+0x14, +0x6C..+0xB7F, +0x16C0) are kept byte for byte by any future writer; their meaning is not
  traced.
