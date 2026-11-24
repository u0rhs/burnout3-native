#include "b3asset/bc.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

#include "b3asset/raster.h"

namespace b3asset {

namespace {
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

void rgb565(u16 c, int o[3]) {
    const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    o[0] = (r << 3) | (r >> 2);
    o[1] = (g << 2) | (g >> 4);
    o[2] = (b << 3) | (b >> 2);
}

// The palette as the decoder below builds it (4-colour: thirds; 3-colour: the half and transparent black).
void colour_palette(u16 c0, u16 c1, bool four, int pal[4][3]) {
    rgb565(c0, pal[0]);
    rgb565(c1, pal[1]);
    for (int k = 0; k < 3; ++k) {
        if (four) {
            pal[2][k] = (2 * pal[0][k] + pal[1][k] + 1) / 3;
            pal[3][k] = (pal[0][k] + 2 * pal[1][k] + 1) / 3;
        } else {
            pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
            pal[3][k] = 0;
        }
    }
}

void colour_block(const u8* b, u8 px[16][4], bool bc1_alpha) {
    const u16 c0 = u16(b[0] | b[1] << 8), c1 = u16(b[2] | b[3] << 8);
    const bool four = c0 > c1 || !bc1_alpha;
    int pal[4][3];
    colour_palette(c0, c1, four, pal);
    const u32 idx = u32(b[4]) | u32(b[5]) << 8 | u32(b[6]) << 16 | u32(b[7]) << 24;
    for (int i = 0; i < 16; ++i) {
        const int k = (idx >> (2 * i)) & 3;
        for (int c = 0; c < 3; ++c) px[i][c] = u8(pal[k][c]);
        px[i][3] = (!four && k == 3) ? 0 : 255;
    }
}

void alpha_palette(int a0, int a1, int a[8]) {
    a[0] = a0;
    a[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i < 7; ++i) a[1 + i] = ((7 - i) * a0 + i * a1 + 3) / 7;
    } else {
        for (int i = 1; i < 5; ++i) a[1 + i] = ((5 - i) * a0 + i * a1 + 2) / 5;
        a[6] = 0;
        a[7] = 255;
    }
}

void alpha_block(const u8* b, u8 px[16][4]) {
    int a[8];
    alpha_palette(b[0], b[1], a);
    u64 bits = 0;
    for (int i = 0; i < 6; ++i) bits |= u64(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) px[i][3] = u8(a[(bits >> (3 * i)) & 7]);
}

int q5(float v) { return std::clamp(int(std::lround(v * 31.0f / 255.0f)), 0, 31); }
int q6(float v) { return std::clamp(int(std::lround(v * 63.0f / 255.0f)), 0, 63); }
u16 pack565(const float c[3]) { return u16(q5(c[0]) << 11 | q6(c[1]) << 5 | q5(c[2])); }

int dist2(const u8* p, const int* q) {
    const int dr = int(p[0]) - q[0], dg = int(p[1]) - q[1], db = int(p[2]) - q[2];
    return dr * dr + dg * dg + db * db;
}

// Indices and error of endpoints (c0, c1) in the given mode over the texels in `use`; transparent texels (`punch`) on 3.
long fit_indices(const u8 px[16][4], const bool use[16], const bool clear[16], u16 c0, u16 c1, bool four, u8 idx[16]) {
    int pal[4][3];
    colour_palette(c0, c1, four, pal);
    const int n = four ? 4 : 3;
    long err = 0;
    for (int i = 0; i < 16; ++i) {
        if (clear[i]) { idx[i] = 3; continue; }
        int best = 0, bd = 1 << 30;
        for (int k = 0; k < n; ++k) {
            const int d = dist2(px[i], pal[k]);
            if (d < bd) { bd = d; best = k; }
        }
        idx[i] = u8(best);
        if (use[i]) err += bd;
    }
    return err;
}

// Least squares endpoints for fixed indices (weights of endpoint 0 per index).
bool refine(const u8 px[16][4], const bool use[16], const u8 idx[16], bool four, float e0[3], float e1[3]) {
    static const float w4[4] = {1.0f, 0.0f, 2.0f / 3.0f, 1.0f / 3.0f};
    static const float w3[4] = {1.0f, 0.0f, 0.5f, 0.0f};
    const float* w = four ? w4 : w3;
    float aa = 0, bb = 0, ab = 0, ax[3] = {}, bx[3] = {};
    for (int i = 0; i < 16; ++i) {
        if (!use[i]) continue;
        const float a = w[idx[i]], b = 1.0f - a;
        aa += a * a;
        bb += b * b;
        ab += a * b;
        for (int c = 0; c < 3; ++c) { ax[c] += a * px[i][c]; bx[c] += b * px[i][c]; }
    }
    const float det = aa * bb - ab * ab;
    if (std::fabs(det) < 1e-6f) return false;
    for (int c = 0; c < 3; ++c) {
        e0[c] = std::clamp((ax[c] * bb - bx[c] * ab) / det, 0.0f, 255.0f);
        e1[c] = std::clamp((bx[c] * aa - ax[c] * ab) / det, 0.0f, 255.0f);
    }
    return true;
}

void write_colour(u16 c0, u16 c1, const u8 idx[16], u8 out[8]) {
    out[0] = u8(c0);
    out[1] = u8(c0 >> 8);
    out[2] = u8(c1);
    out[3] = u8(c1 >> 8);
    u32 bits = 0;
    for (int i = 0; i < 16; ++i) bits |= u32(idx[i] & 3) << (2 * i);
    for (int i = 0; i < 4; ++i) out[4 + i] = u8(bits >> (8 * i));
}
}  // namespace


// ---- BC7 (texpack), from ARB_texture_compression_bptc (Khronos): Table.M, Table.P2 / P3, Table.A2 / A3a / A3b ----
namespace {
struct Bc7Mode { int ns, pb, rb, isb, cb, ab, epb, spb, ib, ib2; };
constexpr Bc7Mode kBc7Modes[8] = {
    {3, 4, 0, 0, 4, 0, 1, 0, 3, 0}, {2, 6, 0, 0, 6, 0, 0, 1, 3, 0}, {3, 6, 0, 0, 5, 0, 0, 0, 2, 0}, {2, 6, 0, 0, 7, 0, 1, 0, 2, 0},
    {1, 0, 2, 1, 5, 6, 0, 0, 2, 3}, {1, 0, 2, 0, 7, 8, 0, 0, 2, 2}, {1, 0, 0, 0, 7, 7, 1, 0, 4, 0}, {2, 6, 0, 0, 5, 5, 1, 0, 2, 0}};
// Table.P2 / Table.P3: the subset of each texel (raster order) per partition, 2 bits a texel, texel 0 lowest
constexpr u8 kP2[64][16] = {
    {0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1},{0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1},{0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1},{0,0,0,1,0,0,1,1,0,0,1,1,0,1,1,1},
    {0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1},{0,0,1,1,0,1,1,1,0,1,1,1,1,1,1,1},{0,0,0,1,0,0,1,1,0,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,1},
    {0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1},{0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1},
    {0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1},{0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1},
    {0,0,0,0,1,0,0,0,1,1,1,0,1,1,1,1},{0,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0},{0,1,1,1,0,0,1,1,0,0,0,1,0,0,0,0},
    {0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0},{0,0,0,0,1,0,0,0,1,1,0,0,1,1,1,0},{0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0},{0,1,1,1,0,0,1,1,0,0,1,1,0,0,0,1},
    {0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0},{0,0,0,0,1,0,0,0,1,0,0,0,1,1,0,0},{0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0},{0,0,1,1,0,1,1,0,0,1,1,0,1,1,0,0},
    {0,0,0,1,0,1,1,1,1,1,1,0,1,0,0,0},{0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0},{0,1,1,1,0,0,0,1,1,0,0,0,1,1,1,0},{0,0,1,1,1,0,0,1,1,0,0,1,1,1,0,0},
    {0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1},{0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},{0,1,0,1,1,0,1,0,0,1,0,1,1,0,1,0},{0,0,1,1,0,0,1,1,1,1,0,0,1,1,0,0},
    {0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0},{0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0},{0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1},{0,1,0,1,1,0,1,0,1,0,1,0,0,1,0,1},
    {0,1,1,1,0,0,1,1,1,1,0,0,1,1,1,0},{0,0,0,1,0,0,1,1,1,1,0,0,1,0,0,0},{0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0},{0,0,1,1,1,0,1,1,1,1,0,1,1,1,0,0},
    {0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0},{0,0,1,1,1,1,0,0,1,1,0,0,0,0,1,1},{0,1,1,0,0,1,1,0,1,0,0,1,1,0,0,1},{0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0},
    {0,1,0,0,1,1,1,0,0,1,0,0,0,0,0,0},{0,0,1,0,0,1,1,1,0,0,1,0,0,0,0,0},{0,0,0,0,0,0,1,0,0,1,1,1,0,0,1,0},{0,0,0,0,0,1,0,0,1,1,1,0,0,1,0,0},
    {0,1,1,0,1,1,0,0,1,0,0,1,0,0,1,1},{0,0,1,1,0,1,1,0,1,1,0,0,1,0,0,1},{0,1,1,0,0,0,1,1,1,0,0,1,1,1,0,0},{0,0,1,1,1,0,0,1,1,1,0,0,0,1,1,0},
    {0,1,1,0,1,1,0,0,1,1,0,0,1,0,0,1},{0,1,1,0,0,0,1,1,0,0,1,1,1,0,0,1},{0,1,1,1,1,1,1,0,1,0,0,0,0,0,0,1},{0,0,0,1,1,0,0,0,1,1,1,0,0,1,1,1},
    {0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1},{0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0},{0,0,1,0,0,0,1,0,1,1,1,0,1,1,1,0},{0,1,0,0,0,1,0,0,0,1,1,1,0,1,1,1}};
constexpr u8 kP3[64][16] = {
    {0,0,1,1,0,0,1,1,0,2,2,1,2,2,2,2},{0,0,0,1,0,0,1,1,2,2,1,1,2,2,2,1},{0,0,0,0,2,0,0,1,2,2,1,1,2,2,1,1},{0,2,2,2,0,0,2,2,0,0,1,1,0,1,1,1},
    {0,0,0,0,0,0,0,0,1,1,2,2,1,1,2,2},{0,0,1,1,0,0,1,1,0,0,2,2,0,0,2,2},{0,0,2,2,0,0,2,2,1,1,1,1,1,1,1,1},{0,0,1,1,0,0,1,1,2,2,1,1,2,2,1,1},
    {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2},{0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2},{0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2},{0,0,1,2,0,0,1,2,0,0,1,2,0,0,1,2},
    {0,1,1,2,0,1,1,2,0,1,1,2,0,1,1,2},{0,1,2,2,0,1,2,2,0,1,2,2,0,1,2,2},{0,0,1,1,0,1,1,2,1,1,2,2,1,2,2,2},{0,0,1,1,2,0,0,1,2,2,0,0,2,2,2,0},
    {0,0,0,1,0,0,1,1,0,1,1,2,1,1,2,2},{0,1,1,1,0,0,1,1,2,0,0,1,2,2,0,0},{0,0,0,0,1,1,2,2,1,1,2,2,1,1,2,2},{0,0,2,2,0,0,2,2,0,0,2,2,1,1,1,1},
    {0,1,1,1,0,1,1,1,0,2,2,2,0,2,2,2},{0,0,0,1,0,0,0,1,2,2,2,1,2,2,2,1},{0,0,0,0,0,0,1,1,0,1,2,2,0,1,2,2},{0,0,0,0,1,1,0,0,2,2,1,0,2,2,1,0},
    {0,1,2,2,0,1,2,2,0,0,1,1,0,0,0,0},{0,0,1,2,0,0,1,2,1,1,2,2,2,2,2,2},{0,1,1,0,1,2,2,1,1,2,2,1,0,1,1,0},{0,0,0,0,0,1,1,0,1,2,2,1,1,2,2,1},
    {0,0,2,2,1,1,0,2,1,1,0,2,0,0,2,2},{0,1,1,0,0,1,1,0,2,0,0,2,2,2,2,2},{0,0,1,1,0,1,2,2,0,1,2,2,0,0,1,1},{0,0,0,0,2,0,0,0,2,2,1,1,2,2,2,1},
    {0,0,0,0,0,0,0,2,1,1,2,2,1,2,2,2},{0,2,2,2,0,0,2,2,0,0,1,2,0,0,1,1},{0,0,1,1,0,0,1,2,0,0,2,2,0,2,2,2},{0,1,2,0,0,1,2,0,0,1,2,0,0,1,2,0},
    {0,0,0,0,1,1,1,1,2,2,2,2,0,0,0,0},{0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0},{0,1,2,0,2,0,1,2,1,2,0,1,0,1,2,0},{0,0,1,1,2,2,0,0,1,1,2,2,0,0,1,1},
    {0,0,1,1,1,1,2,2,2,2,0,0,0,0,1,1},{0,1,0,1,0,1,0,1,2,2,2,2,2,2,2,2},{0,0,0,0,0,0,0,0,2,1,2,1,2,1,2,1},{0,0,2,2,1,1,2,2,0,0,2,2,1,1,2,2},
    {0,0,2,2,0,0,1,1,0,0,2,2,0,0,1,1},{0,2,2,0,1,2,2,1,0,2,2,0,1,2,2,1},{0,1,0,1,2,2,2,2,2,2,2,2,0,1,0,1},{0,0,0,0,2,1,2,1,2,1,2,1,2,1,2,1},
    {0,1,0,1,0,1,0,1,0,1,0,1,2,2,2,2},{0,2,2,2,0,1,1,1,0,2,2,2,0,1,1,1},{0,0,0,2,1,1,1,2,0,0,0,2,1,1,1,2},{0,0,0,0,2,1,1,2,2,1,1,2,2,1,1,2},
    {0,2,2,2,0,1,1,1,0,1,1,1,0,2,2,2},{0,0,0,2,1,1,1,2,1,1,1,2,0,0,0,2},{0,1,1,0,0,1,1,0,0,1,1,0,2,2,2,2},{0,0,0,0,0,0,0,0,2,1,1,2,2,1,1,2},
    {0,1,1,0,0,1,1,0,2,2,2,2,2,2,2,2},{0,0,2,2,0,0,1,1,0,0,1,1,0,0,2,2},{0,0,2,2,1,1,2,2,1,1,2,2,0,0,2,2},{0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,2},
    {0,0,0,2,0,0,0,1,0,0,0,2,0,0,0,1},{0,2,2,2,1,2,2,2,0,2,2,2,1,2,2,2},{0,1,0,1,2,2,2,2,2,2,2,2,2,2,2,2},{0,1,1,1,2,0,1,1,2,2,0,1,2,2,2,0}};
constexpr u8 kA2[64] = {15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15, 15, 2, 8, 2, 2, 8, 8,15,  2, 8, 2, 2, 8, 8, 2, 2,
                        15,15, 6, 8, 2, 8,15,15,  2, 8, 2, 2, 2,15,15, 6,  6, 2, 6, 8,15,15, 2, 2, 15,15,15,15,15, 2, 2,15};
constexpr u8 kA3a[64] = { 3, 3,15,15, 8, 3,15,15,  8, 8, 6, 6, 6, 5, 3, 3,  3, 3, 8,15, 3, 3, 6,10,  5, 8, 8, 6, 8, 5,15,15,
                          8,15, 3, 5, 6,10, 8,15, 15, 3,15, 5,15,15,15,15,  3,15, 5, 5, 5, 8, 5,10,  5,10, 8,13,15,12, 3, 3};
constexpr u8 kA3b[64] = {15, 8, 8, 3,15,15, 3, 8, 15,15,15,15,15,15,15, 8, 15, 8,15, 3,15, 8,15, 8,  3,15, 6,10,15,15,10, 8,
                         15, 3,15,10,10, 8, 9,10,  6,15, 8,15, 3, 6, 6, 8, 15, 3,15,15,15,15,15,15, 15,15,15,15, 3,15,15, 8};
constexpr u8 kW2[4] = {0, 21, 43, 64}, kW3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr u8 kW4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
struct Bits {   // the block's 128 bits read lowest first
    const u8* b;
    int at = 0;
    unsigned take(int n) {
        unsigned v = 0;
        for (int i = 0; i < n; ++i, ++at) v |= unsigned((b[at >> 3] >> (at & 7)) & 1) << i;
        return v;
    }
};
int bc7_weight(int bits, unsigned i) { return bits == 2 ? kW2[i] : bits == 3 ? kW3[i] : kW4[i]; }
u8 bc7_lerp(int e0, int e1, int w) { return u8(((64 - w) * e0 + w * e1 + 32) >> 6); }
}  // namespace

bool bc7_tables_check() {   // (tests) the tables agree: texel 0 in subset 0, each anchor in its own subset
    for (int p = 0; p < 64; ++p) {
        if (kP2[p][0] || kP3[p][0] || kP2[p][kA2[p]] != 1 || kP3[p][kA3a[p]] != 1 || kP3[p][kA3b[p]] != 2) return false;
        for (int i = 0; i < 16; ++i) if (kP2[p][i] > 1 || kP3[p][i] > 2) return false;
    }
    return true;
}

void bc7_decode_block(const std::uint8_t* b, std::uint8_t px[16][4]) {
    int mode = 0;
    while (mode < 8 && !((b[0] >> mode) & 1)) ++mode;
    if (mode == 8) {   // reserved: transparent black
        std::memset(px, 0, 16 * 4);
        return;
    }
    const Bc7Mode& m = kBc7Modes[mode];
    Bits r{b, mode + 1};
    const unsigned part = r.take(m.pb), rot = r.take(m.rb), isel = r.take(m.isb);
    int ep[3][2][4] = {};   // [subset][endpoint][channel], the stored bits first
    for (int c = 0; c < 3; ++c)
        for (int s = 0; s < m.ns; ++s)
            for (int e = 0; e < 2; ++e) ep[s][e][c] = int(r.take(m.cb));
    for (int s = 0; s < m.ns; ++s)
        for (int e = 0; e < 2; ++e) ep[s][e][3] = m.ab ? int(r.take(m.ab)) : 255;
    int pbit[3][2] = {};
    if (m.epb)
        for (int s = 0; s < m.ns; ++s)
            for (int e = 0; e < 2; ++e) pbit[s][e] = int(r.take(1));
    if (m.spb)
        for (int s = 0; s < m.ns; ++s) pbit[s][0] = pbit[s][1] = int(r.take(1));
    const bool has_p = m.epb || m.spb;
    // to 8 bits: the P-bit below the stored bits, then the top bits repeated into the rest
    for (int s = 0; s < m.ns; ++s)
        for (int e = 0; e < 2; ++e)
            for (int c = 0; c < 4; ++c) {
                const int bits = c < 3 ? m.cb : m.ab;
                if (c == 3 && !m.ab) continue;
                int v = ep[s][e][c], n = bits;
                if (has_p) { v = v << 1 | pbit[s][e]; ++n; }
                v <<= 8 - n;
                ep[s][e][c] = v | (v >> n);
            }
    // subsets and anchors
    u8 subset[16] = {};
    if (m.ns == 2) std::memcpy(subset, kP2[part], 16);
    if (m.ns == 3) std::memcpy(subset, kP3[part], 16);
    auto anchor = [&](int i) {
        if (i == 0) return true;
        if (m.ns == 2) return i == kA2[part];
        if (m.ns == 3) return i == kA3a[part] || i == kA3b[part];
        return false;
    };
    unsigned idx[16], idx2[16] = {};
    for (int i = 0; i < 16; ++i) idx[i] = r.take(m.ib - (anchor(i) ? 1 : 0));
    if (m.ib2)
        for (int i = 0; i < 16; ++i) idx2[i] = r.take(m.ib2 - (i == 0 ? 1 : 0));
    for (int i = 0; i < 16; ++i) {
        const int s = subset[i];
        // colour from the secondary indices when the selection bit says so; alpha from the secondary ones when there are
        // any and no selection bit says otherwise
        const bool col2 = m.ib2 && isel, alp2 = m.ib2 && !isel;
        const int cbits = col2 ? m.ib2 : m.ib, abits = alp2 ? m.ib2 : m.ib;
        const int cw = bc7_weight(cbits, col2 ? idx2[i] : idx[i]), aw = bc7_weight(abits, alp2 ? idx2[i] : idx[i]);
        u8 o[4];
        for (int c = 0; c < 3; ++c) o[c] = bc7_lerp(ep[s][0][c], ep[s][1][c], cw);
        o[3] = m.ab ? bc7_lerp(ep[s][0][3], ep[s][1][3], aw) : 255;
        if (rot) std::swap(o[3], o[rot - 1]);
        std::memcpy(px[i], o, 4);
    }
}

void bc_decode_block(int fmt, const std::uint8_t* b, std::uint8_t px[16][4]) {
    if (fmt == kBC7) {
        bc7_decode_block(b, px);
        return;
    }
    if (fmt == kBC1) {
        colour_block(b, px, true);
        return;
    }
    colour_block(b + 8, px, false);
    if (fmt == kBC3) {
        alpha_block(b, px);
    } else {
        for (int i = 0; i < 16; ++i) px[i][3] = u8(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
    }
}

void bc_decode(int fmt, const std::uint8_t* blocks, int w, int h, std::uint8_t* rgba) {
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    const std::size_t bsz = bc_block_bytes(fmt);
    u8 px[16][4];
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            bc_decode_block(fmt, blocks + (std::size_t(by) * bw + bx) * bsz, px);
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < w && y < h) std::memcpy(rgba + (std::size_t(y) * w + x) * 4, px[i], 4);
            }
        }
}

void bc_encode_color(const std::uint8_t px[16][4], std::uint8_t out[8], bool punch) {
    bool clear[16], use[16];
    bool any_clear = false;
    int n = 0;
    for (int i = 0; i < 16; ++i) {
        clear[i] = punch && px[i][3] < 128;
        use[i] = !clear[i];
        any_clear |= clear[i];
        n += use[i];
    }
    const bool four = !any_clear;
    u8 idx[16];
    if (!n) {   // all transparent: 3-colour mode, every texel on index 3
        std::memset(idx, 3, sizeof idx);
        write_colour(0, 0, idx, out);
        return;
    }
    // principal axis of the texels used (power iteration from the bounding box diagonal)
    float mean[3] = {}, lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i)
        if (use[i])
            for (int c = 0; c < 3; ++c) {
                mean[c] += px[i][c];
                lo[c] = std::min(lo[c], float(px[i][c]));
                hi[c] = std::max(hi[c], float(px[i][c]));
            }
    for (float& m : mean) m /= float(n);
    float cov[6] = {};
    for (int i = 0; i < 16; ++i) {
        if (!use[i]) continue;
        const float d[3] = {px[i][0] - mean[0], px[i][1] - mean[1], px[i][2] - mean[2]};
        cov[0] += d[0] * d[0]; cov[1] += d[0] * d[1]; cov[2] += d[0] * d[2];
        cov[3] += d[1] * d[1]; cov[4] += d[1] * d[2]; cov[5] += d[2] * d[2];
    }
    float axis[3] = {hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
    for (int it = 0; it < 6; ++it) {
        const float v[3] = {cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2], cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2],
                            cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2]};
        const float m = std::max({std::fabs(v[0]), std::fabs(v[1]), std::fabs(v[2])});
        if (m < 1e-6f) break;
        for (int c = 0; c < 3; ++c) axis[c] = v[c] / m;
    }
    const float len2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
    float e0[3], e1[3];
    if (len2 < 1e-6f) {
        for (int c = 0; c < 3; ++c) e0[c] = e1[c] = mean[c];
    } else {
        float tmin = 1e30f, tmax = -1e30f;
        for (int i = 0; i < 16; ++i) {
            if (!use[i]) continue;
            const float t = ((px[i][0] - mean[0]) * axis[0] + (px[i][1] - mean[1]) * axis[1] + (px[i][2] - mean[2]) * axis[2]) / len2;
            tmin = std::min(tmin, t);
            tmax = std::max(tmax, t);
        }
        for (int c = 0; c < 3; ++c) {
            e0[c] = std::clamp(mean[c] + tmax * axis[c], 0.0f, 255.0f);
            e1[c] = std::clamp(mean[c] + tmin * axis[c], 0.0f, 255.0f);
        }
    }
    u16 b0 = pack565(e0), b1 = pack565(e1);
    u8 bidx[16];
    long best = fit_indices(px, use, clear, b0, b1, four, bidx);
    for (int pass = 0; pass < 2 && best > 0; ++pass) {
        float r0[3], r1[3];
        if (!refine(px, use, bidx, four, r0, r1)) break;
        const u16 c0 = pack565(r0), c1 = pack565(r1);
        if (c0 == b0 && c1 == b1) break;
        u8 t[16];
        const long e = fit_indices(px, use, clear, c0, c1, four, t);
        if (e >= best) break;
        best = e;
        b0 = c0;
        b1 = c1;
        std::memcpy(bidx, t, 16);
    }
    // the mode is in the endpoint order: 4-colour c0 > c1, 3-colour c0 <= c1
    if (four) {
        if (b0 < b1) {
            std::swap(b0, b1);
            static const u8 swap4[4] = {1, 0, 3, 2};
            for (u8& k : bidx) k = swap4[k];
        } else if (b0 == b1) {
            std::memset(bidx, 0, sizeof bidx);   // one colour (index 0 is c0 in either mode)
        }
    } else if (b0 > b1) {
        std::swap(b0, b1);
        static const u8 swap3[4] = {1, 0, 2, 3};
        for (u8& k : bidx) k = swap3[k];
    }
    write_colour(b0, b1, bidx, out);
}

void bc_encode_alpha(const std::uint8_t a[16], std::uint8_t out[8]) {
    int lo = 255, hi = 0, lo6 = 255, hi6 = 0;
    for (int i = 0; i < 16; ++i) {
        lo = std::min(lo, int(a[i]));
        hi = std::max(hi, int(a[i]));
        if (a[i] != 0 && a[i] != 255) { lo6 = std::min(lo6, int(a[i])); hi6 = std::max(hi6, int(a[i])); }
    }
    auto put = [&](int a0, int a1, const u8 idx[16]) {
        out[0] = u8(a0);
        out[1] = u8(a1);
        u64 bits = 0;
        for (int i = 0; i < 16; ++i) bits |= u64(idx[i] & 7) << (3 * i);
        for (int k = 0; k < 6; ++k) out[2 + k] = u8(bits >> (8 * k));
    };
    u8 i8[16], i6[16];
    if (lo == hi) {   // one value: index 0 (equal endpoints decode in the 6-value mode, index 0 = a0)
        std::memset(i8, 0, sizeof i8);
        put(lo, lo, i8);
        return;
    }
    // 8 interpolants (a0 = hi > a1 = lo): the step nearest to each value, then the exact palette's error
    int p8[8], p6[8];
    alpha_palette(hi, lo, p8);
    long e8 = 0;
    const int span = hi - lo;
    for (int i = 0; i < 16; ++i) {
        const int k = ((int(a[i]) - lo) * 7 + span / 2) / span;   // 0 = lo .. 7 = hi
        u8 idx = u8(k == 7 ? 0 : k == 0 ? 1 : 8 - k);
        int d = std::abs(int(a[i]) - p8[idx]);
        for (const int nb : {k - 1, k + 1}) {   // the palette's rounding: a neighbour may be nearer
            if (nb < 0 || nb > 7) continue;
            const u8 j = u8(nb == 7 ? 0 : nb == 0 ? 1 : 8 - nb);
            const int dj = std::abs(int(a[i]) - p8[j]);
            if (dj < d) { d = dj; idx = j; }
        }
        i8[i] = idx;
        e8 += long(d) * d;
    }
    // 6 interpolants between the values other than 0 / 255, which the mode holds exactly (6 and 7)
    if (lo6 > hi6) lo6 = hi6 = 0;
    alpha_palette(lo6, hi6, p6);
    long e6 = 0;
    const int span6 = hi6 - lo6;
    for (int i = 0; i < 16; ++i) {
        const int v = a[i];
        int best = v - p6[6] < p6[7] - v ? 6 : 7, bd = std::min(v - p6[6], p6[7] - v);
        if (span6 > 0) {
            const int k = std::clamp(((v - lo6) * 5 + span6 / 2) / span6, 0, 5);   // 0 = lo6 .. 5 = hi6
            for (const int nb : {k - 1, k, k + 1}) {
                if (nb < 0 || nb > 5) continue;
                const int j = nb == 0 ? 0 : nb == 5 ? 1 : 1 + nb;
                const int dj = std::abs(v - p6[j]);
                if (dj < bd) { bd = dj; best = j; }
            }
        } else {
            const int dj = std::abs(v - p6[0]);
            if (dj < bd) { bd = dj; best = 0; }
        }
        i6[i] = u8(best);
        e6 += long(bd) * bd;
    }
    if (e6 < e8) put(lo6, hi6, i6);
    else put(hi, lo, i8);
}

void bc_encode(int fmt, const std::uint8_t* rgba, int w, int h, std::vector<std::uint8_t>& out, int threads) {
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    const std::size_t bsz = bc_block_bytes(fmt);
    out.assign(std::size_t(bw) * bh * bsz, 0);
    auto rows = [&](int r0, int r1) {
        u8 px[16][4];
        for (int by = r0; by < r1; ++by)
            for (int bx = 0; bx < bw; ++bx) {
                for (int i = 0; i < 16; ++i) {   // partial blocks repeat the edge texels
                    const int x = std::min(bx * 4 + (i & 3), w - 1), y = std::min(by * 4 + (i >> 2), h - 1);
                    std::memcpy(px[i], rgba + (std::size_t(y) * w + x) * 4, 4);
                }
                u8* b = out.data() + (std::size_t(by) * bw + bx) * bsz;
                if (fmt == kBC1) {
                    bc_encode_color(px, b, true);
                    continue;
                }
                bc_encode_color(px, b + 8, false);
                if (fmt == kBC3) {
                    u8 a[16];
                    for (int i = 0; i < 16; ++i) a[i] = px[i][3];
                    bc_encode_alpha(a, b);
                } else {
                    for (int i = 0; i < 16; ++i) {
                        const int a4 = (int(px[i][3]) * 15 + 127) / 255;
                        b[i / 2] = u8(b[i / 2] | a4 << (4 * (i & 1)));
                    }
                }
            }
    };
    const int n = std::clamp(threads, 1, std::max(1, bh / 8));
    if (n <= 1) { rows(0, bh); return; }
    std::vector<std::thread> pool;
    for (int k = 1; k < n; ++k) pool.emplace_back(rows, bh * k / n, bh * (k + 1) / n);
    rows(0, bh / n);
    for (auto& t : pool) t.join();
}

void mip_down(const Image& src, Image& dst) {
    const int sw = src.w, sh = src.h;
    dst.w = std::max(sw / 2, 1);
    dst.h = std::max(sh / 2, 1);
    dst.rgba.assign(std::size_t(dst.w) * dst.h * 4, 0);
    const bool exact = sw == dst.w * 2 && sh == dst.h * 2;
    for (int y = 0; y < dst.h; ++y) {
        if (exact) {
            const u8* r0 = &src.rgba[std::size_t(2 * y) * sw * 4];
            const u8* r1 = r0 + std::size_t(sw) * 4;
            u8* o = &dst.rgba[std::size_t(y) * dst.w * 4];
            for (int x = 0; x < dst.w; ++x)
                for (int c = 0; c < 4; ++c) {
                    const int s = r0[8 * x + c] + r0[8 * x + 4 + c] + r1[8 * x + c] + r1[8 * x + 4 + c];
                    o[4 * x + c] = u8((s + 2) >> 2);
                }
            continue;
        }
        const float fy = (float(y) + 0.5f) * float(sh) / float(dst.h) - 0.5f;
        const int y0 = int(std::floor(fy));
        const float wy = fy - float(y0);
        const int ya = std::clamp(y0, 0, sh - 1), yb = std::clamp(y0 + 1, 0, sh - 1);
        for (int x = 0; x < dst.w; ++x) {
            const float fx = (float(x) + 0.5f) * float(sw) / float(dst.w) - 0.5f;
            const int x0 = int(std::floor(fx));
            const float wx = fx - float(x0);
            const int xa = std::clamp(x0, 0, sw - 1), xb = std::clamp(x0 + 1, 0, sw - 1);
            auto at = [&](int xx, int yy, int c) { return float(src.rgba[(std::size_t(yy) * sw + xx) * 4 + c]); };
            for (int c = 0; c < 4; ++c) {
                const float v = (at(xa, ya, c) * (1 - wx) + at(xb, ya, c) * wx) * (1 - wy) + (at(xa, yb, c) * (1 - wx) + at(xb, yb, c) * wx) * wy;
                dst.rgba[(std::size_t(y) * dst.w + x) * 4 + c] = u8(std::clamp(int(std::lround(v)), 0, 255));
            }
        }
    }
}

// hdmips (bc.h)
std::size_t bc_consistent_levels(const std::vector<Image>& chain, bool alpha) {
    if (chain.empty()) return 0;
    double prev[4] = {0, 0, 0, 0};
    for (std::size_t l = 0; l < chain.size(); ++l) {
        const Image& lv = chain[l];
        const int bw = (lv.w + 3) / 4, bh = (lv.h + 3) / 4;
        const std::size_t n = std::size_t(bw) * bh, bsz = bc_block_bytes(lv.bc);
        // a level this check cannot read (not BC1-3, no blocks, too few bytes): nothing to judge, the chain kept as it is
        if (!lv.blocks || lv.bc < kBC1 || lv.bc > kBC3 || lv.w < 1 || lv.h < 1 || lv.blocks->size() < n * bsz) return chain.size();
        // the blocks sampled: all of them up to 4096, else 4096 at golden-ratio positions over the block index (spread over
        // every column whatever the row width: a fixed stride that divides the row would see only a few columns)
        const std::size_t samples = std::min<std::size_t>(n, 4096);
        double sum[4] = {0, 0, 0, 0}, texels = 0;
        u8 px[16][4];
        for (std::size_t j = 0; j < samples; ++j) {
            const std::size_t i = samples == n ? j : std::min(n - 1, std::size_t(std::fmod(double(j) * 0.6180339887498949, 1.0) * double(n)));
            bc_decode_block(lv.bc, lv.blocks->data() + i * bsz, px);
            const int bx = int(i % std::size_t(bw)), by = int(i / std::size_t(bw));
            for (int t = 0; t < 16; ++t) {
                if (bx * 4 + (t & 3) >= lv.w || by * 4 + (t >> 2) >= lv.h) continue;   // only the texels inside w x h (1 x 1, 2 x 2)
                for (int c = 0; c < 4; ++c) sum[c] += px[t][c];
                texels += 1;
            }
        }
        double m[4];
        for (int c = 0; c < 4; ++c) m[c] = texels > 0 ? sum[c] / texels : 0.0;
        if (l > 0) {
            // tolerance: BC's own error and sampling, plus a quarter of the parent's level (dark textures keep a floor)
            for (int c = 0; c < 3; ++c)
                if (std::fabs(m[c] - prev[c]) > 24.0 + 0.25 * prev[c]) return l;
            if (alpha && std::fabs(m[3] - prev[3]) > 40.0) return l;
        }
        for (int c = 0; c < 4; ++c) prev[c] = m[c];
    }
    return chain.size();
}

}  // namespace b3asset
