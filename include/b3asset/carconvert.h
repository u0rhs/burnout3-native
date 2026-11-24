// carconvert.h -- betaimport: a prototype disc's vehicle file made into one the game and the port's readers take.
//
// The June 2004 prototype and demo discs' cars (VEHICLES/<class>/CARn.BGV, version 0x14 or 0x15) have an older layout
// than the game's (0x17, docs/modding/file-formats/car.md): the header ends at 0x16E0 with the first LOD block there
// (the game's has two engine-sound offsets at +0x16DC / +0x16E0 and its first LOD block at 0x16F0), their engine sound
// is in separate .HSM / .LSM files, and in version 0x14 a LOD block's 18 list offsets are from the file's start (from
// the block in 0x15 / 0x17). Everything else is the same: the lists, entries and VIF packets, the hull (its offsets are
// from the hull), the texture (a raster, its offsets from the raster; a 4-paint CLUT block in 0x14, as some game files
// have 1), the frames, the lights.
//
// convert_prototype_car:
//   - finds where the old file's data ends (every structure the game's FixUp walks: the LOD blocks, their lists,
//     entries and packets, the +0x64 / +0x68 lists, the texture and its upload, the lights; the files are padded to 32 KB),
//   - inserts kCarConvertShift bytes at 0x16E0 (the header's 16 bytes and padding, so that every packet and the
//     texture keep their 256-byte alignment) and moves every file-start offset the game relocates
//     (CB3VehicleRenderer::FixUpVehicleData 0x220B70: +0x4C LOD blocks, +0x60 texture, +0x64 / +0x68 lists, +0x1664
//     lights, +0x16C0 table and its entries' +8, +0x16C4) by it; in 0x14 the LOD blocks' list offsets become from the block;
//     a +0x64 / +0x68 list entry that is not a mesh (one prototype file counts stray bytes as a second entry) is left
//     out of its list's count (the game would turn it into a wild pointer),
//   - appends the donor's engine-sound structures (a game car file's +0x16DC / +0x16E0, whose offsets are from
//     themselves: func_002FB240) and points +0x16DC / +0x16E0 at them (the car then plays the donor's engine with the
//     donor's .HWD / .LWD banks),
//   - sets the version to 0x17 and the size word, pads to 2 KB (as the game's files are), and refuses a result over the
//     game's car memory block (0x72000 bytes) or one car_file_error does not take.
// Every offset is checked against the files: a damaged or foreign file is refused with the reason, never read past.
#ifndef B3ASSET_CARCONVERT_H
#define B3ASSET_CARCONVERT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "b3asset/raster.h"   // Bytes

namespace b3asset {

constexpr std::uint32_t kCarOldFirstLodBlock = 0x16E0;   // the prototype layout's first LOD block (and header end)
constexpr std::uint32_t kCarConvertShift = 0x100;        // bytes inserted at 0x16E0
constexpr std::size_t kCarMemoryBlock = 0x72000;         // the game's car memory block 1 [L CB3MemoryManagerPS2::Construct]

struct CarConvertInfo {
    std::uint32_t version = 0;       // the old file's (0x14 / 0x15)
    std::size_t data_end = 0;        // where its data ends (the rest is padding)
    std::size_t esm_at = 0, esm_size = 0;   // the donor's engine-sound structures in the result
    std::size_t size = 0;            // the result's size
    int lod_blocks = 0, lists = 0, packets = 0;
    int dropped_entries = 0;         // +0x64 / +0x68 list entries that were not meshes (stray bytes), left out
};

// true when `file` has the prototype layout (version 0x14 / 0x15, the first LOD block at 0x16E0)
bool car_is_prototype(Bytes file);

// "" on success (out: the converted file); else why not. `donor`: a game (0x17) car file, the engine sound's source.
std::string convert_prototype_car(Bytes old_file, Bytes donor, std::vector<std::uint8_t>& out, CarConvertInfo* info = nullptr);

}  // namespace b3asset

#endif
