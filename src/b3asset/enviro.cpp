#include "b3asset/enviro.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace b3asset {

namespace {

bool fail(std::string* why, const std::string& what) { if (why) *why = what; return false; }

void read_block(Bytes f, std::size_t o, EnviroBlock& b) {
    for (int i = 0; i < 4; ++i) b.fog_colour[i] = f.f32(o + 4 * std::size_t(i));
    b.near_scale = f.f32(o + 0x10);
    b.fog_max = f.f32(o + 0x14);
    b.fog_sky = f.f32(o + 0x18);
    b.specular_scale = f.f32(o + 0x1C);
    b.sun_w = f.f32(o + 0x20);
    for (int i = 0; i < 3; ++i) b.unknown[i] = f.f32(o + 0x24 + 4 * std::size_t(i));
}

// Component definitions: maDefinitions__18CB3SkyDomeRenderer 0x4B9CA0, 7 x 0x20 bytes [C rodata]:
// e0, e1, flag(+8), sign(+0xC), u scale(+0x10), v at e0(+0x14), v at e1(+0x18), segments, rings, +0x1E, blend(+0x1F).
// Components 3/4/5 are the car environment-map versions (8 segments, 6 rings), 6 is empty.
struct SkyDef {
    float e0, e1;
    int flag;
    float sign, u_scale, v0, v1;
    int segs, rings, uv_mode, blend;
};
constexpr SkyDef kSkyDefs[6] = {
    {0.0f, 1.0f, 0, -1.0f, 1.0f, 0.765625f, 1.015625f, 32, 12, 0, 0},
    {0.0f, 1.0f, 0, 1.0f, 1.0f, 0.765625f, 0.015625f, 32, 12, 0, 0},
    {0.0f, 0.75f, 1, 1.0f, 2.0f, 1.0f, 0.0f, 64, 12, 0, 1},
    {0.0f, 1.0f, 0, -1.0f, 1.0f, 0.765625f, 1.015625f, 8, 6, 0, 0},
    {0.0f, 1.0f, 0, 1.0f, 1.0f, 0.765625f, 0.015625f, 8, 6, 0, 0},
    {0.0f, 1.0f, 1, 1.0f, 1.0f, 1.0f, 0.0f, 8, 6, 1, 1},
};

struct Rgba { float r, g, b, a; };

// Bilinear sample in texel coordinates (texel centres at integer + 0.5 in GS terms: pass u * w - 0.5), u repeats,
// v clamps. Alpha as decoded (0x80 -> 1.0).
Rgba sample(const Image& im, float x, float y) {
    if (im.w <= 0 || im.h <= 0) return {0, 0, 0, 0};
    const float fx = std::floor(x), fy = std::floor(y);
    const float ax = x - fx, ay = y - fy;
    auto at = [&](int xi, int yi) {
        xi = ((xi % im.w) + im.w) % im.w;
        yi = std::clamp(yi, 0, im.h - 1);
        const std::uint8_t* p = &im.rgba[(std::size_t(yi) * std::size_t(im.w) + std::size_t(xi)) * 4];
        return Rgba{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f};
    };
    const int x0 = int(fx), y0 = int(fy);
    const Rgba a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    auto mix = [&](float p, float q, float r, float s) {
        return (p * (1 - ax) + q * ax) * (1 - ay) + (r * (1 - ax) + s * ax) * ay;
    };
    return {mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b), mix(a.a, b.a, c.a, d.a)};
}

// One Gt2d textured sprite into a w x h target: rect (x0,y0)-(x1,y1), UV (u0,v0)-(u1,v1) (normalised), called per
// covered pixel with the sampled texel.
template <class Fn>
void sprite(int w, int h, const Image& tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
            Fn&& fn) {
    for (int y = std::max(0, int(y0)); y < std::min(h, int(std::ceil(y1))); ++y)
        for (int x = std::max(0, int(x0)); x < std::min(w, int(std::ceil(x1))); ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            if (px < x0 || px >= x1 || py < y0 || py >= y1) continue;
            const float uu = u0 + (u1 - u0) * (px - x0) / (x1 - x0);
            const float vv = v0 + (v1 - v0) * (py - y0) / (y1 - y0);
            fn(x, y, sample(tex, uu * float(tex.w) - 0.5f, vv * float(tex.h) - 0.5f));
        }
}

}  // namespace

bool read_enviro(Bytes f, GsMemory& gs, Enviro& out, std::string* why) {
    out = Enviro{};
    if (!f.has(0, 0xB0)) return fail(why, "ENVIRO.DAT shorter than its 0xB0-byte header");
    read_block(f, 0x00, out.block[0]);
    read_block(f, 0x30, out.block[1]);
    for (int i = 0; i < 4; ++i) {
        out.specular_colour[i] = f.f32(0x60 + 4 * std::size_t(i));
        out.sun_colour[i] = f.f32(0x70 + 4 * std::size_t(i));
    }
    for (int i = 0; i < 3; ++i) {
        out.sun_travel[i] = f.f32(0x80 + 4 * std::size_t(i));
        out.to_sun[i] = -out.sun_travel[i];
    }
    out.corona_scale = f.f32(0x90);
    out.word94 = f.u32(0x94);
    out.word_a8 = f.u32(0xA8);
    out.file_size = f.u32(0xAC);
    DecodedRaster* r[4] = {&out.gradient, &out.clouds, &out.sky_envmap, &out.sun_corona};
    const char* names[4] = {"gradient", "clouds", "sky envmap", "sun corona"};
    for (int k = 0; k < 4; ++k) {
        const std::uint32_t at = f.u32(0x98 + 4 * std::size_t(k));
        if (!at) continue;
        std::string err;
        if (!decode_raster(f, at, gs, *r[k], 0, &err)) return fail(why, std::string("ENVIRO.DAT ") + names[k] + ": " + err);
    }
    return true;
}

bool find_txd_raster(Bytes txd, const std::string& name, GsMemory& gs, DecodedRaster& out) {
    std::vector<TxdEntry> es;
    if (!read_txd(txd, es)) return false;
    auto lower = [](std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; };
    const std::string want = lower(name);
    std::size_t prefix_at = 0;
    int prefix_hits = 0;
    for (const TxdEntry& e : es) {
        RasterHeader h;
        if (!read_raster_header(txd, e.offset, h)) continue;
        const std::string have = lower(h.name);
        if (have == want) return decode_raster(txd, e.offset, gs, out);
        // A name kept in a 16-byte field arrives cut to 15 characters (the particle arrays' "fxexplosionsmok" for GLOBAL.TXD's
        // "fxexplosionsmoke"): the game binds the raster by pointer, so only a name lookup sees the cut. The one raster that
        // name starts is taken when no name matches exactly.
        if (want.size() == 15 && have.size() > 15 && have.compare(0, 15, want) == 0) { prefix_at = e.offset; ++prefix_hits; }
    }
    return prefix_hits == 1 && decode_raster(txd, prefix_at, gs, out);
}

SkyLayer build_sky_layer(int component, float cloud_u_offset) {
    SkyLayer L;
    L.component = component;
    if (component < 0 || component > 5) return L;
    const SkyDef& d = kSkyDefs[component];
    // CB3SkyDomeHemisphereParams::Construct 0x21E490: v = f28 + f2C * h, h = 2e - e^2 [C matched].
    const float fa = 2 * d.e0 - d.e0 * d.e0, fb = 2 * d.e1 - d.e1 * d.e1, k = 1.0f / (fb - fa);
    const float f28 = d.uv_mode == 1 ? 0.0f : k * (d.v0 * fb - d.v1 * fa);
    const float f2c = d.uv_mode == 1 ? 0.0f : k * (d.v1 - d.v0);
    const float u_off = component == 2 ? cloud_u_offset : 0.0f;
    for (int j = 0; j <= d.segs; ++j) {
        const float a = float(j) / float(d.segs);
        for (int i = 0; i <= d.rings; ++i) {
            const float e = d.e0 + (d.e1 - d.e0) * float(i) / float(d.rings);
            const float h = 2 * e - e * e, r = 1 - e * e;
            SkyVertex v;
            v.dir[0] = r * std::cos(6.2831853f * a);
            v.dir[1] = d.sign * h;
            v.dir[2] = r * std::sin(6.2831853f * a);
            v.uv[0] = u_off + d.u_scale * a;
            v.uv[1] = f28 + f2c * h;
            L.verts.push_back(v);
        }
    }
    const std::uint32_t n = std::uint32_t(d.rings + 1);
    for (std::uint32_t j = 0; j < std::uint32_t(d.segs); ++j)
        for (std::uint32_t i = 0; i < std::uint32_t(d.rings); ++i) {
            const std::uint32_t a = j * n + i, b = (j + 1) * n + i;
            L.indices.insert(L.indices.end(), {a, b, a + 1, b, b + 1, a + 1});
        }
    // Construct: blend components get colour 0x40 and alpha 0x80, TEST 0x3140B, ALPHA 0x44, FBMSK 0xFF000000; the
    // others 0x80 with alpha 0x20 (component 0) or 0, TEST 0x30000, ALPHA 0x2A [C matched].
    if (d.blend) {
        L.rgba[0] = L.rgba[1] = L.rgba[2] = 0x40;
        L.rgba[3] = 0x80;
        L.blend = true;
    } else {
        L.rgba[0] = L.rgba[1] = L.rgba[2] = 0x80;
        L.rgba[3] = component == 0 ? 0x20 : 0;
    }
    L.clouds_texture = component == 2;
    return L;
}

float cloud_u_offset(float t) {
    const float f = 2.0f * t;
    return f - (float(int(f)) + 1.0f);
}

Image build_backdrop(const Enviro& env, const float to_sun[3], float column_blend) {
    Image out;
    out.w = 64;
    out.h = 32;
    out.rgba.assign(64 * 32 * 4, 0);
    const Image* g = env.gradient.levels.empty() ? nullptr : &env.gradient.levels[0];
    if (!g) return out;
    std::vector<Rgba> base(64 * 32), bright(64 * 32);
    std::vector<float> mask(64 * 32, 0.0f);
    const float texel = 0.015625f;
    const float u0 = texel + 0.46875f * std::clamp(column_blend, 0.0f, 1.0f);
    // 1. base column, copy (blend mode 3)
    sprite(64, 32, *g, 0, 0, 64, 32, u0, texel, u0, 1.015625f, [&](int x, int y, Rgba c) { base[y * 64 + x] = c; });
    // 2. glow mask into the alpha channel (blend mode 7)
    float bearing, alt;
    {
        const float x = to_sun[0], y = to_sun[1], z = to_sun[2];
        alt = y >= 0 ? 1.0f - std::sqrt(std::max(0.0f, 1.0f - y)) : std::sqrt(std::max(0.0f, 1.0f + y)) - 1.0f;
        bearing = std::atan2(z, x) / 6.2831853f;
        if (bearing < 0) bearing += 1.0f;
    }
    const float fa = 15.5f - bearing, fb2 = fa + 1.0f;
    auto bd_h = [&](int k) {  // RenderBackdropTexture's row function, D_004E0F08 = 1/24 [C rodata]
        const float t = 24.0f * (0.5f - ((1.0f - std::sqrt(float(k) / 24.0f)) - alt));
        return std::min(t, 24.0f);
    };
    for (int i = 0; i < 24; i += 2)
        sprite(64, 32, *g, 0, float(i), 64, float(i + 2), fa, 0.03125f * bd_h(i), fb2, 0.03125f * bd_h(i + 2),
               [&](int x, int y, Rgba c) { mask[y * 64 + x] = c.a; });
    sprite(64, 32, *g, 0, 24, 64, 32, fa, 0.765625f, fb2, 1.015625f, [&](int x, int y, Rgba c) { mask[y * 64 + x] = c.a; });
    // 3. bright column blended by the destination alpha (blend mode 4: (Cs-Cd)*Ad+Cd)
    sprite(64, 32, *g, 0, 0, 64, 32, 0.5f + u0, texel, 0.5f + u0, 1.015625f, [&](int x, int y, Rgba c) { bright[y * 64 + x] = c; });
    for (int p = 0; p < 64 * 32; ++p) {
        const float m = std::clamp(mask[p], 0.0f, 1.0f);
        const float c[3] = {base[p].r + (bright[p].r - base[p].r) * m, base[p].g + (bright[p].g - base[p].g) * m,
                            base[p].b + (bright[p].b - base[p].b) * m};
        for (int k = 0; k < 3; ++k) out.rgba[4 * p + k] = std::uint8_t(std::lround(std::clamp(c[k], 0.0f, 1.0f) * 255.0f));
        out.rgba[4 * p + 3] = std::uint8_t(std::lround(m * 255.0f));
    }
    return out;
}

FogModel fog_model(const Enviro& env) {
    FogModel f;
    const EnviroBlock& b = env.block[0];
    f.near = std::clamp(b.near_scale * 0.0003f, 0.003f, 0.4f);
    f.far = 10000.0f;
    // RW CameraUpdateZShiftScale: 1/far -> zBufferFar (0), 1/near -> zBufferNear (16777215), each pulled in by 0.01 %.
    float zinf = 16777215.0f, zsup = 0.0f;
    const float delta = (zsup - zinf) * 0.0001f;
    zsup -= delta;
    zinf += delta;
    const float in_sup = 1.0f / f.far, in_inf = 1.0f / f.near;
    f.z_scale = (zsup - zinf) / (in_sup - in_inf);
    f.z_shift = 0.5f * ((zsup + zinf) - f.z_scale * (in_sup + in_inf));
    f.fog_max = b.fog_max;
    f.fog_sky = b.fog_sky;
    for (int i = 0; i < 3; ++i) f.colour[i] = b.fog_colour[i];
    for (int i = 0; i < 256; ++i) {
        float v;
        if (i == 0) {
            v = b.fog_sky;
        } else if (i < 12) {
            v = b.fog_max;
        } else {
            float t = (1.4f / (0.4f + float(i - 12) / 243.0f) - 1.0f) / 2.5f;
            if (t < 0.75f) {
                t = t * t * 1.3333334f;
            } else {
                t = 1.0f - t;
                t = 1.0f - 4.0f * t * t;
            }
            v = t * b.fog_max;
        }
        f.alpha[i] = std::uint8_t(std::clamp(int(0.5f + 128.0f * v), 0, 255));
    }
    return f;
}

float fog_amount(const FogModel& f, float w) {
    if (w <= 0.0f) return 0.0f;
    const float z = f.z_shift + f.z_scale / w;
    if (z >= 65535.0f) return 0.0f;  // the fog sprite (Z 0xFFFF, ZTST GREATER) only reaches Z <= 0xFFFE
    const int i = std::clamp(int(z) >> 8, 0, 255);
    return float(f.alpha[i]) / 128.0f;
}

}  // namespace b3asset
