// synthetic_car.h -- a synthetic vehicle file for the car tests (no game data): the header, one LOD block (LOD 3 at
// 0x16F0) with one L0 entry whose packet unpacks three vertices, and a 16 x 16 PSMT8 texture with two palettes at
// 0x1900 (test_car_truncation, test_carconvert); the same car in the June 2004 prototypes' layout (old_car) and a game
// car with engine-sound structures (donor_car) for the prototype converter (test_carconvert, test_betaimport).
#ifndef B3_TESTS_SYNTHETIC_CAR_H
#define B3_TESTS_SYNTHETIC_CAR_H

#include <cstdint>
#include <cstring>
#include <vector>

#include "b3asset/gs_memory.h"
#include "b3asset/raster.h"

namespace synth {

using Buf = std::vector<std::uint8_t>;

inline void p32(Buf& b, std::size_t at, std::uint32_t v) {
    if (b.size() < at + 4) b.resize(at + 4);
    std::memcpy(&b[at], &v, 4);
}
inline void p64(Buf& b, std::size_t at, std::uint64_t v) {
    if (b.size() < at + 8) b.resize(at + 8);
    std::memcpy(&b[at], &v, 8);
}
inline std::uint32_t fbits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
inline void qw(Buf& v, std::uint64_t lo, std::uint64_t hi) {
    std::uint8_t b[16];
    std::memcpy(b, &lo, 8);
    std::memcpy(b + 8, &hi, 8);
    v.insert(v.end(), b, b + 16);
}
inline void gif_upload(Buf& v, std::uint32_t rrw, std::uint32_t rrh, const Buf& bytes) {
    qw(v, 3 | (1ull << 60), 0xE);   // NLOOP 3, PACKED, NREG 1 (A+D)
    qw(v, 0, 0x51);
    qw(v, rrw | std::uint64_t(rrh) << 32, 0x52);
    qw(v, 0, 0x53);
    const std::size_t nq = (bytes.size() + 15) / 16;
    qw(v, nq | (2ull << 58), 0);   // IMAGE
    Buf pad = bytes;
    pad.resize(nq * 16, 0);
    v.insert(v.end(), pad.begin(), pad.end());
}

// the texture: a raster header, its upload chain, the pixels and a 256-entry CLUT; two palettes (the same CLUT block)
inline Buf synthetic_raster() {
    Buf f(0x200, 0);
    const std::uint32_t w = 16, h = 16;
    Buf pix_bytes(w * h), clut(256 * 4);
    for (std::size_t i = 0; i < pix_bytes.size(); ++i) pix_bytes[i] = std::uint8_t(i);
    for (std::size_t i = 0; i < clut.size(); ++i) clut[i] = std::uint8_t(i * 7);
    p32(f, 0x0C, w);
    p32(f, 0x10, h);
    p32(f, 0x14, 8);
    f[0x20] = 4;
    f[0x23] = 0x25;
    p64(f, 0x3C, (std::uint64_t(2) << 14) | (std::uint64_t(b3asset::PSMT8) << 20) | (4ull << 26) | (4ull << 30) | (1ull << 34) | (1ull << 61));
    p32(f, 0x44, 4);
    f[0xA0] = 2;   // two palettes (paint 1's CBP add at +0x92: 0, the same block)
    std::memcpy(&f[0xA8], "carpaint", 8);
    p32(f, 0x24, 0x100);
    p32(f, 0x100, 2);
    p32(f, 0x108, 2);
    Buf pix, pal;
    gif_upload(pix, w / 2, h / 2, pix_bytes);
    gif_upload(pal, 16, 16, clut);
    const std::size_t pix_at = 0x200, pal_at = pix_at + pix.size();
    f.resize(pal_at + pal.size());
    std::memcpy(&f[pix_at], pix.data(), pix.size());
    std::memcpy(&f[pal_at], pal.data(), pal.size());
    auto chain = [&](std::size_t e, std::uint64_t bitblt, std::size_t src, std::size_t q) {
        p32(f, e, 0x10000002);
        p32(f, e + 12, 0x50000002);
        p64(f, e + 16, 1 | (1ull << 60));
        p64(f, e + 24, 0xE);
        p64(f, e + 32, bitblt);
        p64(f, e + 40, 0x50);
        p32(f, e + 48, 0x30000000u | std::uint32_t(q));
        p32(f, e + 52, std::uint32_t(src));
        p32(f, e + 60, 0x50000000u | std::uint32_t(q));
    };
    chain(0x110, std::uint64_t(1) << 48 | std::uint64_t(b3asset::PSMCT32) << 56, pix_at, pix.size() / 16);
    chain(0x150, std::uint64_t(4) << 32 | std::uint64_t(1) << 48 | std::uint64_t(b3asset::PSMCT32) << 56, pal_at, pal.size() / 16);
    return f;
}

// the car (test_modtool's synthetic car) with the texture at 0x1900
inline Buf synthetic_car() {
    Buf f(0x1900, 0);
    p32(f, 0x00, 0x17);
    p32(f, 0x10, 3);
    p32(f, 0x4C + 4 * 3, 0x16F0);   // LOD 3
    p32(f, 0x16F0 + 0, 0x48);       // slot L0 -> list at 0x1738
    const std::size_t lst = 0x1738;
    f[lst] = 1;
    f[lst + 1] = 0xFF;
    p32(f, lst + 4, 0x10);
    p32(f, lst + 8, 0x47800080);
    const std::size_t e = 0x1748;
    p32(f, e + 0, 0x10);
    p32(f, e + 8, 0x00640001);
    p32(f, e + 12, 0x8080);
    std::vector<std::uint32_t> w = {0, 0, 0x30000000};
    for (int i = 0; i < 4; ++i) w.push_back(0x47800000);
    w.push_back(0x01000103);
    w.push_back(0x6C030000);
    const float P[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0.5f}};
    for (int i = 0; i < 3; ++i) {
        for (int c = 0; c < 3; ++c) w.push_back(fbits(P[i][c]));
        w.push_back(i == 0 ? 0x8000 : 0);
    }
    w.push_back(0x6D034001);
    const std::uint16_t U[3][4] = {{4096, 0, 2048, 4096}, {0x0104, 0x0208, 0x030C, 0}, {0, 4096, 0, 0}};
    for (int i = 0; i < 3; ++i) {
        w.push_back(U[i][0] | std::uint32_t(U[i][1]) << 16);
        w.push_back(U[i][2] | std::uint32_t(U[i][3]) << 16);
    }
    w.push_back(0x05000001);
    w.push_back(0x6A034002);
    const std::uint8_t N[12] = {128, 255, 128, 128, 128, 255, 0, 128, 128, 0, 0, 0};
    for (int i = 0; i < 3; ++i)
        w.push_back(std::uint32_t(N[4 * i]) | std::uint32_t(N[4 * i + 1]) << 8 | std::uint32_t(N[4 * i + 2]) << 16 | std::uint32_t(N[4 * i + 3]) << 24);
    w.push_back(0x04000003);
    while ((w.size() * 4 + 8) % 16) w.push_back(0);
    const std::size_t at = 0x1758;
    p32(f, at, 0x60000000u | std::uint32_t((w.size() * 4 - 8) / 16));
    for (std::size_t i = 0; i < w.size(); ++i) p32(f, at + 8 + 4 * i, w[i]);
    const Buf tex = synthetic_raster();
    p32(f, 0x60, 0x1900);
    f.resize(0x1900);
    f.insert(f.end(), tex.begin(), tex.end());
    p32(f, 0x08, std::uint32_t(f.size()));
    return f;
}

inline std::uint32_t g32(const Buf& b, std::size_t at) { std::uint32_t v; std::memcpy(&v, &b[at], 4); return v; }

// the synthetic game car in the prototype layout
inline Buf old_car(std::uint32_t version) {
    const Buf f = synthetic_car();
    Buf o(f.begin(), f.begin() + 0x16E0);
    o.insert(o.end(), f.begin() + 0x16F0, f.end());
    p32(o, 0x00, version);
    p32(o, 0x4C + 4 * 3, 0x16E0);
    p32(o, 0x60, g32(f, 0x60) - 0x10);
    p32(o, 0x08, std::uint32_t(o.size()));
    if (version == 0x14)
        for (std::size_t s = 0; s < 18; ++s)
            if (const std::uint32_t r = g32(o, 0x16E0 + 4 * s)) p32(o, 0x16E0 + 4 * s, r + 0x16E0);   // from the file's start
    o.resize(0x8000, 0);   // the prototype files are padded to 32 KB
    return o;
}

// the synthetic car with two engine-sound structures at its end (0x44 bytes each; one entry with two sub-entries before it)
inline Buf donor_car() {
    Buf d = synthetic_car();
    d.resize((d.size() + 15) / 16 * 16, 0);
    const std::size_t from = d.size();
    d.resize(from + 0x100, 0);
    // structure A at from (the sound data's start, as in the game's files): its entries at from + 0x90, 1 of them; the
    // sub-entries at from + 0x50, before the entry
    const std::size_t a = from, ents = from + 0x90, subs = from + 0x50;
    p32(d, a + 0x3C, std::uint32_t(ents - a));
    d[a + 0x40] = 1;
    p32(d, ents + 8, std::uint32_t(std::int32_t(subs) - std::int32_t(ents)));   // negative: before the entry
    d[ents + 0xC] = 2;
    p32(d, subs, 0x20);       // sub-entry 0 -> subs + 0x20
    p32(d, subs + 8, 0x18);   // sub-entry 1 -> subs + 0x20
    for (std::size_t k = 0; k < 0x10; ++k) d[from + 0xC0 + k] = std::uint8_t(0xA0 + k);   // some sound bytes
    // structure B at from + 0xD0: no entries
    p32(d, 0x16DC, std::uint32_t(a));
    p32(d, 0x16E0, std::uint32_t(from + 0xD0));
    d.resize(from + 0x200, 0);
    p32(d, 0x08, std::uint32_t(d.size()));
    return d;
}

}  // namespace synth

#endif
