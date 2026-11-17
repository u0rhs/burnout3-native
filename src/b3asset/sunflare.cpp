#include "b3asset/sunflare.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

#include "b3asset/discfs.h"
#include "b3host/ps2float.h"

namespace b3asset {

namespace {

bool fail(std::string* why, const std::string& m) {
    if (why) *why = m;
    return false;
}

// The bytes at virtual address `va` of an ELF32 (little endian) file image, from its PT_LOAD program headers.
const std::uint8_t* elf_at(const std::vector<std::uint8_t>& d, std::uint32_t va, std::uint32_t n) {
    auto u32 = [&](std::size_t o) { std::uint32_t v = 0; if (o + 4 <= d.size()) std::memcpy(&v, d.data() + o, 4); return v; };
    auto u16 = [&](std::size_t o) { std::uint16_t v = 0; if (o + 2 <= d.size()) std::memcpy(&v, d.data() + o, 2); return v; };
    if (d.size() < 0x34 || std::memcmp(d.data(), "\x7F" "ELF", 4) != 0 || d[4] != 1 || d[5] != 1) return nullptr;
    const std::uint32_t phoff = u32(0x1C);
    const std::uint16_t phes = u16(0x2A), phn = u16(0x2C);
    for (std::uint32_t i = 0; i < phn; ++i) {
        const std::size_t p = phoff + std::size_t(i) * phes;
        if (u32(p) != 1) continue;   // PT_LOAD
        const std::uint32_t off = u32(p + 4), vaddr = u32(p + 8), filesz = u32(p + 16);
        if (va >= vaddr && std::uint64_t(va) + n <= std::uint64_t(vaddr) + filesz && std::size_t(off) + (va - vaddr) + n <= d.size())
            return d.data() + off + (va - vaddr);
    }
    return nullptr;
}

float f32at(const std::uint8_t* p) { float f; std::memcpy(&f, p, 4); return f; }

}  // namespace

bool read_sun_flare_table(const std::string& elf_path, SunFlareTable& out, std::string* why) {
    out = SunFlareTable{};
    std::vector<std::uint8_t> d;   // iso1: a disc folder's or a disc image's (b3asset/discfs.h)
    if (!disc::read_file(elf_path, d)) return fail(why, "cannot read " + elf_path);
    const std::uint8_t* t = elf_at(d, 0x4BB610, 12 * 0x18);   // D_004BB610 [C RenderCorona's table]
    const std::uint8_t* b = elf_at(d, 0x4E1978, 1);           // D_004E1978 (lbSecondaries)
    if (!t || !b) return fail(why, elf_path + ": not the retail ELF (no D_004BB610 / D_004E1978)");
    for (int i = 0; i < 12; ++i) {
        const std::uint8_t* e = t + 0x18 * i;
        SunSecondary& s = out.sec[i];
        s.pos = f32at(e);
        s.size = f32at(e + 4);
        for (int c = 0; c < 3; ++c) s.rgb[c] = f32at(e + 8 + 4 * c);
        std::memcpy(&s.quadrant, e + 0x14, 4);
    }
    out.secondaries = *b != 0;
    out.valid = true;
    return true;
}

bool route_has_sun(const std::string& route_dir) {
    std::string r = route_dir;
    while (!r.empty() && (r.back() == '/' || r.back() == '\\')) r.pop_back();
    std::string tail = r.size() >= 8 ? r.substr(r.size() - 8) : r;
    for (char& c : tail) c = char(std::toupper(static_cast<unsigned char>(c))), c = c == '\\' ? '/' : c;
    for (const char* no : {"US/P2_V1", "US/P2_V2", "EU/M2_V1", "EU/M2_V2"})
        if (tail == no) return false;
    return true;
}

SunFlare sun_flare(const SunFlareInput& in, const SunFlareTable& table, int tex_w, int tex_h) {
    SunFlare out;
    const float x = in.sun_px[0], y = in.sun_px[1];
    const int sx = int(in.screen[0]), sy = int(in.screen[1]);   // mnScreenSizeX / Y
    // OcclusionTest: within 6.5 px of the screen, else mbBehind [C asm c.lt / c.le pairs]
    if (!(x > -6.5f && y > -6.5f && x < 6.5f + float(sx) && y < 6.5f + float(sy))) return out;
    out.drawn = true;
    const int max_x = sx << 4, max_y = sy << 4;
    const int cx16 = int(16.0f * x), cy16 = int(16.0f * y);   // mnCoronaX / Y, 1/16 px
    int left = cx16 - 0x70, right = cx16 + 0x70, top = cy16 - 0x70, bottom = cy16 + 0x70;
    left = std::max(left, 0);
    right = std::min(right, max_x);
    top = std::max(top, 0);
    bottom = std::min(bottom, max_y);
    out.fade = float(((right - left) * (bottom - top)) / 256) / 196.0f;
    // the GS sprite that marks the sky covers whole pixels: left & ~15 .. (right + 15) & ~15
    out.box[0] = float(left & ~15) / 16.0f;
    out.box[1] = float(top & ~15) / 16.0f;
    out.box[2] = float((right + 15) & ~15) / 16.0f;
    out.box[3] = float((bottom + 15) & ~15) / 16.0f;
    int col[3];
    for (int c = 0; c < 3; ++c) col[c] = int(in.colour[c] * out.fade);   // mnCoronaRed / Green / Blue
    // RenderCorona
    const float r16 = 16.0f * in.radius;
    const int rx = int(r16), ry = int(r16 * in.aspect);   // lnRadiusX / Y
    const float W = float(tex_w), H = float(tex_h);
    SunSprite c;
    c.x0 = float(cx16 - rx) / 16.0f; c.y0 = float(cy16 - ry) / 16.0f;
    c.x1 = float(cx16 + rx) / 16.0f; c.y1 = float(cy16 + ry) / 16.0f;
    // UV (1/16 texel): (8, 8) .. (16 W - 8, 8 H - 8) with the secondaries (the upper half), else 16 H - 8
    c.u0 = 0.5f / W; c.v0 = 0.5f / H;
    c.u1 = (W - 0.5f) / W;
    c.v1 = ((table.secondaries ? H / 2 : H) - 0.5f) / H;
    for (int k = 0; k < 3; ++k) c.rgb[k] = col[k];
    out.sprites.push_back(c);
    if (!table.valid || !table.secondaries) return out;
    // the flicker: the world clock through float bits, the EE FPU's arithmetic (b3fp::ref: truncating)
    using b3fp::bits;
    using b3fp::flt;
    const auto mul = [](float a, float b) { return flt(b3fp::ref::mul(bits(a), bits(b))); };
    const auto sub = [](float a, float b) { return flt(b3fp::ref::sub(bits(a), bits(b))); };
    const float K = 0.93456787f;
    float r = mul(in.now, mul(K, in.now));
    r = sub(r, float(int(r)));
    const int ccx = max_x / 2, ccy = max_y / 2, dx = cx16 - ccx, dy = cy16 - ccy;
    for (int i = 0; i < 12; ++i) {
        const SunSecondary& e = table.sec[i];
        const float t = sub(r, K);
        r = mul(t, t);
        const std::uint32_t b = bits(r);
        const int rnd = 0xFF00 - int((((b & 0xFF0000u) >> 16) ^ ((b & 0xFFu) ^ ((b & 0xFF00u) >> 8))) << 6);
        const int px = ccx + int(e.pos * float(dx)), py = ccy + int(e.pos * float(dy));
        const int srx = int(e.size * float(rx)), sry = int(e.size * float(ry));
        SunSprite s;
        s.x0 = float(px - srx) / 16.0f; s.y0 = float(py - sry) / 16.0f;
        s.x1 = float(px + srx) / 16.0f; s.y1 = float(py + sry) / 16.0f;
        for (int k = 0; k < 3; ++k) s.rgb[k] = int(e.rgb[k] * float((col[k] * rnd) / 0xFF00));
        // UV (1/16 texel): u = (q & 1) 8 W + 8, v = (q & 2) 2 H + 8 + 8 H; size 8 W - 8 by 4 H - 8
        const float u = float(e.quadrant & 1) * W / 2 + 0.5f, v = float(e.quadrant & 2) * H / 8 + 0.5f + H / 2;
        s.u0 = u / W; s.v0 = v / H;
        s.u1 = (u + W / 2 - 0.5f) / W; s.v1 = (v + H / 4 - 0.5f) / H;
        out.sprites.push_back(s);
    }
    return out;
}

}  // namespace b3asset
