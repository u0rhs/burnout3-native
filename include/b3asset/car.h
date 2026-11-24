// Vehicle model files PVEH/<class>/CARn.BGV (player cars) and CARn.BTV (traffic): reader into a b3asset::Model.
//
// Layout [C: CB3VehicleRenderer::FixUpVehicleData 0x220B70 in the decomp, src/b3/_unclassified/
// B3VehicleRendererRuntime_00220B70.cpp; data bytes]; the per-slot/list/packet details are checked
// against the files:
//   +0x00 u32 version 0x17 (0x15 / 0x16 read the same: car_version_ok), +0x08 file size, +0x0C u8 panel matrix count (6), +0x0D u8 wheel matrix count (4)
//   +0x4C u32[5] LOD blocks (file offsets; FixUp__21CB3VehicleLODDataBase), +0x60 texture (serialized raster, one
//   256x256 PSMT8 atlas + N palettes = paint variants, N at raster+0xA0), +0x64/+0x68 two more mesh lists
//   +0xB80 wheel frames (CGtMatrix3x4 x count at +0x0D), +0xD00 panel frames (x count at +0x0C)
//     [C: CB3RaceCar::Prepare copies them, src/nonmatching/Prepare__10CB3RaceCarFv.cpp]
//   LOD block: u32[18] list offsets (from the block): L0 body, A0-A5 panels, B0-B2 wheels, C0-C7 payload objects (a
//   truck's boat, logs, cars: drawn only with bake_frames off, in their own space; payloads1)
//   list: s8 count, s8 first glass entry, 2 pad, u32 entries (from the list), u32 0x47800080
//   entry (16 B): u32 mesh (from the entry), u32 continuation mesh (from the entry, 0 = none), u32 material, u32 colour
//   mesh: DMA RET tag (qwc) whose VIF stream (from tag+8) unpacks batches for the car VU1 program:
//     V4-32 @0 (x, y, z, w: 0x8000 = ADC), V4-16 unsigned @1 (UVs in 1/4096 + two influences, interleaved per 4
//     vertices), V3-8 unsigned with STMOD offset @2 (normal = (b - 128) / 127), STCYCL 3/1 (or 6/1: UV rows only for
//     even vertex slots), ITOP n, MSCNT between batches.
#ifndef B3ASSET_CAR_H
#define B3ASSET_CAR_H

#include <cstdint>
#include <string>

#include "b3asset/car_layout.h"   // car_version_ok / car_header_error (header-only: the mod overlay uses them too)
#include "b3asset/model.h"

namespace b3asset {

struct CarOptions {
    int lod = 3;          // 0 lowest .. 3 highest (block slots 0..4 hold up to five; the retail cars use 0..3)
    int palette = 0;      // paint variant
    bool panels = true;   // A0-A5 at their panel frames
    bool wheels = true;   // B0 at each wheel frame
    // false: every list in its own part space, as RenderVehicle draws them (the caller supplies the part matrices):
    // panels without their file frames, the wheel lists B0 / B1 / B2 (RenderVehicle picks one by wheel speed) once
    // each, not per wheel [C RenderVehicle 0x21E6F0 draft]
    bool bake_frames = true;
    bool texture = true;  // carpaint: false = meshes only (no atlas decode, so no HD pack lookup); draws keep texture 0
};

bool read_car(Bytes file, const CarOptions& opt, GsMemory& gs, Model& out, std::string* why = nullptr);

// carmods1: the vehicle file versions read. 0x17 is the game's own; files of version 0x15 / 0x16 (cars from earlier
// builds, as players' car mods carry them) run in the game: its loader never reads the word
// (CB3VehicleRenderer::FixUpVehicleData 0x220B70 relocates the same header offsets whatever it says, and
// CB3RaceCar::Prepare copies the same frames), so such a file has this layout for everything the game reads, and the
// readers take it the same way, every offset still checked against the file.
// The version word does not tell the layout: players' 0x15 mods have this (0x17) layout, but the June-prototype disc's
// own 0x15 files have an older one (car_layout.h): any first LOD block below 0x16F0 is refused, by the readers and by
// the mod overlay (the game is never served one).
bool car_old_layout(Bytes file);   // a first LOD block below 0x16F0 (the retail files' at 0x16F0)
// "" when `file` is a vehicle file the readers take; else why not: too short, its version, or the old layout
// ("a prototype-disc car file (old layout: ...): not supported"); car_header_error on its bytes
std::string car_file_error(Bytes file);

}  // namespace b3asset

#endif
