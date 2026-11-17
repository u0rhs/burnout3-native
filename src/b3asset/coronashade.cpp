#include "b3asset/coronashade.h"

#include <cmath>
#include <cstring>

#include "b3host/ps2float.h"

namespace b3asset {

namespace {
namespace fr = b3fp::ref;
using u32 = std::uint32_t;
constexpr u32 kZero = 0, kOne = 0x3F800000u, kHalf = 0x3F000000u, k64 = 0x42800000u, k8 = 0x41000000u;

u32 rd32(const std::uint8_t* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
// FTOIn: clamped operand, x 2^n, truncated, saturating (as the VU1 interpreter) [C VU manual].
std::int32_t ftoi(u32 b, int frac) {
    const double v = double(b3fp::flt(fr::clamp(b))) * double(1 << frac);
    if (v >= 2147483647.0) return 0x7FFFFFFF;
    if (v <= -2147483648.0) return std::int32_t(0x80000000u);
    return std::int32_t(std::trunc(v));
}
// CLIP judgement of (x, y, z) against |w|: bit 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z.
u32 clip(u32 x, u32 y, u32 z, u32 w) {
    const float aw = b3fp::flt(fr::clamp(w) & 0x7FFFFFFFu);
    const u32 v[3] = {x, y, z};
    u32 bits = 0;
    for (int l = 0; l < 3; ++l) {
        const float f = b3fp::flt(fr::clamp(v[l]));
        if (f > aw) bits |= 1u << (2 * l);
        if (f < -aw) bits |= 2u << (2 * l);
    }
    return bits;
}
}  // namespace

CoronaCamera corona_camera(const std::uint8_t r[0x230]) {
    CoronaCamera c;
    auto f = [&](int off) { return rd32(r + off); };
    for (int k = 0; k < 4; ++k) {
        for (int l = 0; l < 3; ++l) c.row[k][l] = f(0xE0 + 0x10 * k + 4 * l);
        c.row[k][3] = fr::add(fr::sub(kOne, kOne), c.row[k][2]);   // vsubw.w w, vf0, vf0w; vaddz.w w, w, z
    }
    const u32 k64f = k64;
    c.row[4][0] = fr::add(f(0x218), k64f);   // add.s (FPU)
    c.row[4][1] = fr::add(f(0x21C), k64f);
    c.row[4][2] = f(0x198);
    c.row[4][3] = f(0x188);
    c.row[5][0] = f(0x170);
    c.row[5][1] = f(0x174);
    c.row[5][2] = f(0x178);
    c.row[5][3] = f(0x168);
    c.row[6][0] = f(0x218);
    c.row[6][1] = f(0x21C);
    c.row[6][2] = f(0x200);
    c.row[6][3] = f(0x204);
    c.row[7][0] = k8;
    c.row[7][1] = f(0x21C);
    c.row[7][2] = f(0x208);
    c.row[7][3] = f(0x204);
    c.row[8][0] = 0;
    c.row[8][1] = 0x502B4000u;
    c.row[8][2] = 0x42412u;
    c.row[8][3] = 0;
    return c;
}

std::vector<CoronaSprite> corona_sprites(const CoronaCamera& cam, const std::vector<CoronaSlotWords>& slots) {
    // set-up (0x0000..0x0026)
    const u32* A = cam.row[4];   // 1019
    const u32* S = cam.row[5];   // 1020 (vf27)
    const u32* W = cam.row[6];   // 1021
    u32 M[4][4];                 // vf28, vf29, vf30, vf31 = 1015..1018 (x, y, w loaded; z rebuilt)
    for (int k = 0; k < 4; ++k) std::memcpy(M[k], cam.row[k], 16);
    const u32 sx = fr::mul(fr::add(W[0], W[0]), fr::vu_div(kOne, A[0]));   // vf10.x = 2 width / (width + 64)
    const u32 sy = fr::mul(fr::add(W[1], W[1]), fr::vu_div(kOne, A[1]));
    for (int k = 0; k < 4; ++k) {
        const u32 h = fr::mul(M[k][3], kHalf);
        M[k][0] = fr::mul(fr::sub(M[k][0], h), sx);
        M[k][1] = fr::mul(fr::sub(M[k][1], h), sy);
        M[k][2] = fr::mul(A[2], M[k][3]);
    }
    M[3][2] = fr::add(M[3][2], A[3]);
    const u32 hx = fr::mul(fr::mul(W[0], fr::vu_div(kOne, fr::add(W[2], W[2]))), kHalf);   // vf25.x
    const u32 hy = fr::mul(fr::mul(W[1], fr::vu_div(kOne, fr::add(W[3], W[3]))), kHalf);   // vf25.y
    const u32 half_win[2] = {fr::mul(A[0], kHalf), fr::mul(A[1], kHalf)};                    // vf26.xy
    const u32 K = cam.row[7][2], thr = cam.row[7][0];

    std::vector<CoronaSprite> out;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const u32* p = slots[i].w;
        const u32* c = slots[i].w + 4;
        // cull: ACC = R3 x 1 + R0 x + R1 y; c = ACC + R2 z (mulaw, maddax, madday, maddz)
        u32 cl[4];
        for (int l = 0; l < 4; ++l) {
            u32 acc = fr::mul(M[3][l], kOne);
            acc = fr::madd(acc, M[0][l], p[0]);
            acc = fr::madd(acc, M[1][l], p[1]);
            cl[l] = fr::madd(acc, M[2][l], p[2]);
        }
        const u32 zf = fr::mul(K, p[3]);
        if (clip(cl[0], cl[1], cl[2], cl[3]) != 0 || clip(cl[0], cl[1], zf, cl[3]) != 0x10) continue;
        // fade: ACC.w = w + 0; pulled = ACC.w - size x 0.5; Q = w / z'; colour' = colour - colour x Q
        const u32 w = cl[3];
        const u32 pulled = fr::msub(fr::add(w, kZero), c[3], kHalf);
        const u32 q = fr::vu_div(w, zf);
        u32 col[3];
        for (int l = 0; l < 3; ++l) col[l] = fr::msub(fr::add(kZero, c[l]), c[l], q);
        if ((clip(col[0], col[1], col[2], thr) & 0x15) == 0) continue;   // fcand 0x15: some channel > 8
        // sprite
        const u32 P = fr::vu_div(kOne, w);
        const u32 v[4] = {fr::mul(cl[0], P), fr::mul(cl[1], P), fr::vu_div(S[3], pulled), fr::mul(fr::add(c[3], kZero), P)};
        const u32 hw[3] = {hx, hy, kOne};
        const u32 mul26[3] = {half_win[0], half_win[1], kOne};
        CoronaSprite s;
        s.slot = int(i);
        s.depth = w;
        s.pulled = pulled;
        u32 ctr[3], c1[3], c2[3];
        for (int l = 0; l < 3; ++l) {
            ctr[l] = fr::madd(fr::add(kZero, S[l]), v[l], mul26[l]);
            c1[l] = fr::msub(ctr[l], hw[l], v[3]);
            c2[l] = fr::madd(ctr[l], hw[l], v[3]);
        }
        const u32 qv = fr::mul(hx, v[3]);
        s.st[0][0] = fr::sub(kZero, kZero);
        s.st[0][1] = fr::sub(kZero, kZero);
        s.st[0][2] = qv;
        s.st[1][0] = qv;
        s.st[1][1] = qv;
        s.st[1][2] = qv;
        for (int l = 0; l < 3; ++l) {
            s.col[l] = ftoi(col[l], 0);
            s.rgba[l] = std::uint8_t(s.col[l] & 0xFF);
        }
        s.rgba[3] = 0x80;
        const u32* cs[2] = {c1, c2};
        for (int k = 0; k < 2; ++k) {
            s.x[k] = u32(ftoi(cs[k][0], 4)) & 0xFFFF;
            s.y[k] = u32(ftoi(cs[k][1], 4)) & 0xFFFF;
            s.z[k] = (u32(ftoi(cs[k][2], 4)) >> 4) & 0xFFFFFF;
        }
        out.push_back(s);
    }
    return out;
}

}  // namespace b3asset
