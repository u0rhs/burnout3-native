#include "b3asset/trailshade.h"

#include <cstring>

#include "b3host/ps2float.h"

namespace b3asset {

namespace {
namespace fr = b3fp::ref;
using u32 = std::uint32_t;
constexpr u32 kZero = 0, kOne = 0x3F800000u, kHalf = 0x3F000000u, k255 = 0x437F0000u;
constexpr u32 kInv127 = 0x3C010204u;  // the program's `loi 0.0078740157` [C 0x0032]

u32 fbits(float f) { u32 b; std::memcpy(&b, &f, 4); return b; }
float bitsf(u32 b) { float f; std::memcpy(&f, &b, 4); return f; }
u32 rd32(const std::uint8_t* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
// FTOIn: clamped operand (exponent 0 -> 0, exponent 255 -> +-Fmax), x 2^n, truncated, saturating [C VU manual].
std::int32_t ftoi(u32 b, int frac) {
    const double v = double(b3fp::flt(fr::clamp(b))) * double(1 << frac);
    if (v >= 2147483647.0) return 0x7FFFFFFF;
    if (v <= -2147483648.0) return std::int32_t(0x80000000u);
    return std::int32_t(v);
}
u32 itof(std::int32_t i, int frac) { return fr::chop(double(i) / double(1 << frac)); }
}  // namespace

// CB3TrailRenderer::Construct 0x2F3A60 [C asm: div.s by 2.0, ((w1 - w0) / life) / 2, 1.0 / life; +0x0C untouched (bss 0)].
void trail_vu_params(const std::uint8_t rec[0x50], u32 o[12]) {
    auto f = [&](int off) { return rd32(rec + off); };
    const u32 two = 0x40000000u;
    o[0] = fr::fpu_div(f(0x24), two);
    o[1] = fr::fpu_div(fr::fpu_div(fr::sub(f(0x28), f(0x24)), f(0x4C)), two);
    o[2] = fr::fpu_div(kOne, f(0x4C));
    o[3] = 0;
    for (int i = 0; i < 4; ++i) {
        o[4 + i] = f(0x2C + 4 * i);
        o[8 + i] = fr::sub(f(0x3C + 4 * i), f(0x2C + 4 * i));
    }
}

TrailEmitterInput trail_emitter_from_guest(std::int8_t count, const std::uint8_t data[0x130]) {
    TrailEmitterInput e;
    e.count = count;
    for (int k = 0; k < 16; ++k) {
        for (int l = 0; l < 4; ++l) e.point[k][l] = rd32(data + 16 * k + 4 * l);
        for (int c = 0; c < 3; ++c) e.side[k][c] = std::int8_t(data[0x100 + 3 * k + c]);
    }
    return e;
}

std::uint64_t trail_alpha_reg(u32 w) { return (std::uint64_t(w & 0xFF000000u) << 8) | (w & 0xFFu); }

// VU1 0x4771B0 from 0x002A (each MSCNT; 0x020E jumps back there) [C listing, checked by b3vucheck_trail].
TrailRibbon trail_ribbon(const u32 p[12], float now_f, const TrailEmitterInput& e) {
    TrailRibbon r;
    const u32 now = fbits(now_f);
    int n = e.count;
    // 0x0030..0x003C, 0x0210: while the last point is not newer than point 0 (MAC Zw|Sw of t[n-1] - t[0]), drop it and
    // start over; nothing is drawn below two points (0x0037 iblez n - 1) [C].
    for (;;) {
        if (n - 1 <= 0) return r;
        const u32 d = fr::sub(e.point[n - 1][3], e.point[0][3]);
        if ((d & 0x7FFFFFFFu) != 0 && !(d & 0x80000000u)) break;
        --n;
    }
    r.points = n;
    const u32 inv = p[2];
    const u32 wx = fr::mul(p[0], kInv127), wy = fr::mul(p[1], kInv127);  // 0x0033 muli.xy vf07 (I = 1/127)
    const u32 acc_now = fr::mul(now, inv);                                 // mulaz.w ACC = now x (1 / life)
    for (int k = 0; k < n; ++k) {
        // age fraction a = now/life - t/life, clamped above at 1 (not below) [C 0x0031/0x0032, 0x0039 miniw.w vf00w]
        const u32 a = fr::min(fr::msub(acc_now, e.point[k][3], inv), kOne);
        // half width = w0/2/127 + rate/127 x a (the side vector carries x127) [C 0x003D addax.y, 0x003E maddw.y]
        const u32 hw = fr::madd(fr::add(kZero, wx), wy, a);
        // colour = start + (end - start) x a, FTOI0 [C 0x003F addax, 0x0040 maddw, 0x0044 ftoi0]
        std::int32_t col[4];
        for (int l = 0; l < 4; ++l) col[l] = ftoi(fr::madd(fr::add(p[4 + l], kZero), p[8 + l], a), 0);
        // Side vector: ITOF0 of the s8 integers -- except point 0, which the program converts with FTOI0 (0x0034, an
        // upper-slot `ftoi0` where the loop uses `itof0` 0x003B/0x0049): small integers are exponent-0 "floats" -> 0,
        // negative ones exponent 255 -> -Fmax -> 0x80000000 = -0.0. So point 0 always has zero width [C, run].
        u32 sd[3];
        for (int c = 0; c < 3; ++c) {
            const u32 raw = u32(std::int32_t(e.side[k][c]));
            sd[c] = k == 0 ? u32(ftoi(raw, 0)) : itof(std::int32_t(raw), 0);
        }
        for (int edge = 0; edge < 2; ++edge) {
            TrailVertex v;
            for (int c = 0; c < 3; ++c) {
                const u32 base = fr::add(kZero, e.point[k][c]);  // adda.xyz ACC, vf00, vf10
                v.pos[c] = bitsf(edge == 0 ? fr::madd(base, sd[c], hw) : fr::msub(base, sd[c], hw));  // 0x0042 / 0x0043
            }
            v.s = float(k & 1);   // vf22/vf23 = (0,0)/(0,1) on even points, vf24/vf25 = (1,0)/(1,1) on odd [C 0x002B..0x0038]
            v.t = float(edge);
            for (int l = 0; l < 4; ++l) { v.colour[l] = col[l]; v.rgba[l] = std::uint8_t(col[l] & 0xFF); }
            v.point = k;
            v.edge = edge;
            v.kick = r.verts.size() >= 2;
            r.verts.push_back(v);
        }
    }
    return r;
}

// Set-up 0x0006..0x0016 (once per BeginRender) and 0x0063..0x00CB (per emitter) [C].
TrailDraw trail_project(const TrailCamera& cam, const TrailRibbon& r, std::vector<TrailGsVertex>& out) {
    out.clear();
    if (r.points < 2) return TrailDraw::kNothing;
    const auto& R = cam.row;  // R[0..3] view rows (1014..1017), R[4] scale, R[5] shift, R[6]/R[7] clip frame
    // Rows with the window centring folded in: x' = (x - w/2) x width, y' = (y - w/2) x height; z lane = w; the
    // translation row's z lane = its w (0x0065/0x0066/0x0068..0x006B).
    u32 m[4][4];
    for (int i = 0; i < 4; ++i) {
        const u32 hw = fr::mul(R[i][3], kHalf);
        for (int c = 0; c < 2; ++c) m[i][c] = fr::mul(fr::sub(R[i][c], hw), R[4][c]);
        m[i][3] = R[i][3];
    }
    for (int i = 0; i < 3; ++i) m[i][2] = fr::add(kZero, m[i][3]);
    for (int c : {0, 1, 3}) m[3][c] = fr::add(m[3][c], kZero);
    m[3][2] = fr::add(kZero, R[3][3]);
    const u32 zscale = fr::mul(kOne, R[4][2]);  // vf31.z = 1 x scale.z (0x0009, 0x0016)
    // Clip-frame test vector: (x / 1020.y, y / 1021.y, 1020.z + 1021.z x w, w) judged against |w| (0x006A..0x006C,
    // 0x0077..0x007D). With the game's constants: |x/w| <= 325 px, |y/w| <= 229 px from the window centre and
    // 0.4998 <= w <= 1000 [L constants, ShadeScene defaults].
    const u32 kx = fr::add(kZero, R[6][1]), ky = fr::add(kZero, R[7][1]), kz = fr::add(kZero, R[7][2]);
    const u32 z0 = fr::add(kZero, R[6][2]);
    u32 all = 0x3F, any = 0;
    for (const TrailVertex& v : r.verts) {
        TrailGsVertex g;
        u32 c[4];
        for (int l = 0; l < 4; ++l) {
            u32 acc = fr::mul(m[3][l], kOne);
            for (int i = 0; i < 3; ++i) acc = fr::madd(acc, m[i][l], fbits(v.pos[i]));
            c[l] = acc;
            g.clip[l] = bitsf(acc);
        }
        const u32 tv[3] = {fr::mul(c[0], kx), fr::mul(c[1], ky), fr::madd(z0, kz, c[3])};
        const float aw = bitsf(fr::clamp(fr::mul(c[3], kOne)) & 0x7FFFFFFFu);
        for (int l = 0; l < 3; ++l) {
            const float x = bitsf(fr::clamp(tv[l]));
            if (x > aw) g.clip_flags |= 1u << (2 * l);
            if (x < -aw) g.clip_flags |= 2u << (2 * l);
        }
        all &= g.clip_flags;
        any |= g.clip_flags;
        // Fast-path output (0x00A8..0x00C9).
        const u32 q = fr::vu_div(kOne, c[3]);
        g.q = bitsf(fr::mul(kOne, q));
        g.s = bitsf(fr::mul(fbits(v.s), q));
        g.t = bitsf(fr::mul(fbits(v.t), q));
        g.x = u32(ftoi(fr::madd(fr::add(R[5][0], kZero), c[0], q), 4)) & 0xFFFF;
        g.y = u32(ftoi(fr::madd(fr::add(R[5][1], kZero), c[1], q), 4)) & 0xFFFF;
        g.z = (u32(ftoi(fr::madd(fr::add(R[5][2], kZero), zscale, q), 4)) >> 4) & 0xFFFFFF;
        // fog = clamp(scale.w x w - scale.w x shift.w, 0, 255) (FGE is off: unused) [C 0x00AA..0x00B4]
        u32 fog = fr::madd(fr::msub(kZero, R[5][3], R[4][3]), c[3], R[4][3]);
        fog = fr::min(fr::max(fog, kZero), k255);
        const std::int32_t fw = ftoi(fr::add(fog, kZero), 4);
        g.f = std::uint8_t((u32(fw) >> 4) & 0xFF);
        g.adc = (u32(fw) >> 15) & 1;
        for (int l = 0; l < 4; ++l) g.rgba[l] = v.rgba[l];
        out.push_back(g);
    }
    if (all) return TrailDraw::kCulled;   // 0x00A4 ibne vi02 (AND of every vertex's flags)
    if (any) return TrailDraw::kClipped;  // 0x00A6 ibne vi03 (OR) -> clipper at 0x00CC
    return TrailDraw::kDirect;
}

}  // namespace b3asset
