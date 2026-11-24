// carprint.h -- carpacks (2026-11-22): a car model's geometry fingerprint, the same for two files of the same car whatever
// converted them (a prototype car made into the game's layout by the BETA import, or by a pack's author), and different
// for a remodelled car. The body as the game draws it at its highest detail (read_car: LOD 3, no panels, no wheels, its
// frames applied, no textures), each vertex position snapped to a 2 cm grid, the set of those cells (sorted, each once)
// hashed with FNV-1a 64. 0: the file cannot be read as a car (an old-layout prototype file: convert it first).
#ifndef B3ASSET_CARPRINT_H
#define B3ASSET_CARPRINT_H

#include "b3asset/car.h"
#include "b3asset/gs_memory.h"
#include "b3asset/model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace b3asset {

inline std::uint64_t car_fingerprint(Bytes file, std::size_t* cells = nullptr) {
    CarOptions o;
    o.lod = 3;
    o.panels = false;
    o.wheels = false;
    o.texture = false;
    GsMemory gs;
    Model m;
    if (!read_car(file, o, gs, m, nullptr) || m.vertices.empty()) return 0;
    std::vector<std::array<std::int32_t, 3>> v;
    v.reserve(m.vertices.size());
    for (const Vertex& p : m.vertices) {
        if (!(std::fabs(p.pos[0]) < 50.0f && std::fabs(p.pos[1]) < 50.0f && std::fabs(p.pos[2]) < 50.0f)) continue;
        v.push_back({std::int32_t(std::lround(p.pos[0] * 50.0f)), std::int32_t(std::lround(p.pos[1] * 50.0f)),
                     std::int32_t(std::lround(p.pos[2] * 50.0f))});
    }
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    if (cells) *cells = v.size();
    if (v.empty()) return 0;
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (const auto& c : v)
        for (std::int32_t x : c)
            for (int k = 0; k < 4; ++k) h = (h ^ std::uint8_t(std::uint32_t(x) >> (8 * k))) * 0x100000001B3ull;
    return h;
}

}  // namespace b3asset

#endif
