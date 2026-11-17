// overlay_record.h -- the code mods' overlay record (mod kit Phase 2b, docs/modding/DESIGN.md §2.2 overlays): the 2D a
// code mod draws (B3ModApi quad / text), handed from the game's thread to the renderer with a frame. Header only (the
// renderer's capture decoder and native_play's loader both use it).
//
// A host record of the scene feed (as the skip hint's), at kOverlayBase, published at a poll when it changed:
//   u32 'HMO1', u32 serial, u32 bytes (after this head), u32 items; then per item:
//   u8 kind (1 quad, 2 text), u8 0, u16 text bytes, u32 rgba (R low), f32 x, y, w, h (text: h = its size, w = 0),
//   u32 texture id (quad; 0: none), the text's bytes padded to 4
// Coordinates in the game's 640 x 448 screen. Little endian (every host the port runs on). Never read by the game.
#ifndef B3MOD_OVERLAY_RECORD_H
#define B3MOD_OVERLAY_RECORD_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace b3mod {

constexpr std::uint32_t kOverlayBase = 0x41114000u, kOverlayMax = 0xC000u, kOverlayMagic = 0x314F4D48u;   // 'HMO1'
constexpr std::size_t kOverlayItemsPerMod = 256;
struct OverlayItem {
    std::uint8_t kind = 1;   // 1 quad, 2 text
    std::uint32_t rgba = 0xFFFFFFFFu;
    float x = 0, y = 0, w = 0, h = 0;
    std::uint32_t texture = 0;
    std::string text;
    bool operator==(const OverlayItem&) const = default;
};
struct OverlayList {
    std::uint32_t serial = 0;
    std::vector<OverlayItem> items;
};

// The record's bytes (head included); the items that would pass kOverlayMax are left out
inline std::vector<std::uint8_t> overlay_record(std::uint32_t serial, const std::vector<OverlayItem>& items) {
    std::vector<std::uint8_t> b(16);
    auto put32 = [&b](std::uint32_t v) {
        for (int k = 0; k < 4; ++k) b.push_back(std::uint8_t(v >> (8 * k)));
    };
    auto putf = [&](float f) {
        std::uint32_t v;
        std::memcpy(&v, &f, 4);
        put32(v);
    };
    std::uint32_t n = 0;
    for (const OverlayItem& it : items) {
        const std::size_t tl = std::min<std::size_t>(it.text.size(), 1024);
        if (b.size() + 28 + ((tl + 3) & ~std::size_t(3)) > kOverlayMax) break;
        b.push_back(it.kind);
        b.push_back(0);
        b.push_back(std::uint8_t(tl));
        b.push_back(std::uint8_t(tl >> 8));
        put32(it.rgba);
        putf(it.x);
        putf(it.y);
        putf(it.w);
        putf(it.h);
        put32(it.texture);
        b.insert(b.end(), it.text.begin(), it.text.begin() + std::ptrdiff_t(tl));
        while (b.size() % 4) b.push_back(0);
        ++n;
    }
    const std::uint32_t head[4] = {kOverlayMagic, serial, std::uint32_t(b.size() - 16), n};
    std::memcpy(b.data(), head, 16);
    return b;
}

inline bool overlay_parse(const std::uint8_t* p, std::size_t n, OverlayList& out) {
    auto get32 = [](const std::uint8_t* q) {
        return std::uint32_t(q[0]) | std::uint32_t(q[1]) << 8 | std::uint32_t(q[2]) << 16 | std::uint32_t(q[3]) << 24;
    };
    auto getf = [&](const std::uint8_t* q) {
        const std::uint32_t v = get32(q);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    };
    if (!p || n < 16 || get32(p) != kOverlayMagic) return false;
    const std::uint32_t bytes = get32(p + 8), count = get32(p + 12);
    if (bytes > kOverlayMax || 16 + std::size_t(bytes) > n) return false;
    OverlayList l;
    l.serial = get32(p + 4);
    std::size_t at = 16;
    const std::size_t end = 16 + std::size_t(bytes);
    for (std::uint32_t k = 0; k < count; ++k) {
        if (at + 28 > end) return false;
        OverlayItem it;
        it.kind = p[at];
        const std::size_t tl = std::size_t(p[at + 2]) | std::size_t(p[at + 3]) << 8;
        it.rgba = get32(p + at + 4);
        it.x = getf(p + at + 8);
        it.y = getf(p + at + 12);
        it.w = getf(p + at + 16);
        it.h = getf(p + at + 20);
        it.texture = get32(p + at + 24);
        at += 28;
        if (at + tl > end || (it.kind != 1 && it.kind != 2)) return false;
        it.text.assign(reinterpret_cast<const char*>(p + at), tl);
        at += (tl + 3) & ~std::size_t(3);
        l.items.push_back(std::move(it));
    }
    out = std::move(l);
    return true;
}

}  // namespace b3mod

#endif
