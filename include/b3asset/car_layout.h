// car_layout.h -- carmods1: which vehicle files (PVEH/*.BGV / .BTV) the port's readers and the game take, from the header
// bytes alone (header-only, so the mod overlay, src/b3mod, can refuse a car file without linking the asset readers).
//   - The version word (+0x00) 0x15 .. 0x17: 0x17 is the game's own; players' car mods from earlier builds carry 0x15 /
//     0x16 in the same layout, and the game runs them (CB3VehicleRenderer::FixUpVehicleData 0x220B70 never reads it).
//   - The layout: every retail file's first LOD block (the smallest non-zero of the 5 offsets at +0x4C) is at 0x16F0.
//     The June-prototype disc's own files (also 0x15) have an older layout, 16 bytes shorter before it (0x16E0; sound in
//     .HSM / .LSM): the game would read their fields 16 bytes off. Any first LOD block below 0x16F0 is refused.
#ifndef B3ASSET_CAR_LAYOUT_H
#define B3ASSET_CAR_LAYOUT_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace b3asset {

constexpr std::size_t kCarHeaderBytes = 0x64;          // what car_header_error reads
constexpr std::uint32_t kCarFirstLodBlock = 0x16F0;    // the retail layout's first LOD block

inline bool car_version_ok(std::uint32_t v) { return v >= 0x15 && v <= 0x17; }
inline std::string car_version_error(std::uint32_t v) {   // "not a vehicle file (version 0x..; 0x15 .. 0x17 are read)"
    char b[96];
    std::snprintf(b, sizeof b, "not a vehicle file (version 0x%X; 0x15 .. 0x17 are read)", v);
    return b;
}
// The first LOD block's offset (the smallest non-zero of +0x4C .. +0x5F), 0 when none or the header is short
inline std::uint32_t car_first_lod_block(const std::uint8_t* p, std::size_t n) {
    std::uint32_t first = 0;
    for (std::size_t k = 0; k < 5 && 0x4C + 4 * k + 4 <= n; ++k) {
        const std::uint8_t* q = p + 0x4C + 4 * k;
        const std::uint32_t o = std::uint32_t(q[0] | q[1] << 8 | q[2] << 16) | std::uint32_t(q[3]) << 24;
        if (o && (!first || o < first)) first = o;
    }
    return first;
}
// "" when the header is one the readers and the game take; else why not (too short, the version, the old layout)
inline std::string car_header_error(const std::uint8_t* p, std::size_t n) {
    if (!p || n < kCarHeaderBytes) return "too short for a vehicle file header";
    const std::uint32_t v = std::uint32_t(p[0] | p[1] << 8 | p[2] << 16) | std::uint32_t(p[3]) << 24;
    if (!car_version_ok(v)) return car_version_error(v);
    if (const std::uint32_t first = car_first_lod_block(p, n); first && first < kCarFirstLodBlock) {
        char b[160];
        std::snprintf(b, sizeof b, "a prototype-disc car file (old layout: its first LOD block at 0x%X, the game's at 0x%X): not supported",
                      first, kCarFirstLodBlock);
        return b;
    }
    return "";
}

}  // namespace b3asset

#endif
