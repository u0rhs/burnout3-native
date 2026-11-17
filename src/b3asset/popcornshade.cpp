#include "b3asset/popcornshade.h"

#include <cmath>
#include <cstring>

#include "b3host/ps2float.h"

namespace b3asset {

namespace {
namespace fr = b3fp::ref;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
constexpr u32 kZero = 0, kOne = 0x3F800000u, kHalf = 0x3F000000u;
constexpr u32 k128 = 0x43000000u, kMinus2 = 0xC0000000u;
constexpr u32 kPoint2 = 0x3E4CCCCDu;   // D_004E0E00 = 0.2 [C ELF]

u32 rd32(const std::uint8_t* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
u64 rd64(const std::uint8_t* p) { return u64(rd32(p)) | u64(rd32(p + 4)) << 32; }
void put32(std::vector<std::uint8_t>& b, u32 v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
float bitsf(u32 b) { float f; std::memcpy(&f, &b, 4); return f; }
u32 add0(u32 x) { return fr::add(kZero, x); }   // vaddx.<lane> vf1, vf0, vf2x (CGtV4d::SetX/Y/Z)
u32 mul1(u32 x) { return fr::mul(kOne, x); }    // vmulx.w vf1, vf0, vf2x (SetA)
std::int32_t ftoi(u32 b, int frac) {
    const double v = double(b3fp::flt(fr::clamp(b))) * double(1 << frac);
    if (v >= 2147483647.0) return 0x7FFFFFFF;
    if (v <= -2147483648.0) return std::int32_t(0x80000000u);
    return std::int32_t(v);
}
u32 clip_bits(const u32 c[4]) {
    const float aw = bitsf(fr::clamp(c[3]) & 0x7FFFFFFFu);
    u32 bits = 0;
    for (int l = 0; l < 3; ++l) {
        const float x = bitsf(fr::clamp(c[l]));
        if (x > aw) bits |= 1u << (2 * l);
        if (x < -aw) bits |= 2u << (2 * l);
    }
    return bits;
}
}  // namespace

float PopcornParams::f(int off) const { return bitsf(w[off / 4]); }

PopcornParams popcorn_params_from_guest(const std::uint8_t p[0x50]) {
    PopcornParams s;
    for (int i = 0; i < 20; ++i) s.w[i] = rd32(p + 4 * i);
    return s;
}

PopcornCamera popcorn_camera_from_guest(const std::uint8_t c[0x88]) {
    PopcornCamera k;
    for (int r = 0; r < 4; ++r)
        for (int l = 0; l < 3; ++l) k.view[r][l] = rd32(c + 0x20 + 0x10 * r + 4 * l);
    k.window_x = rd32(c + 0x68);
    k.recip_x = rd32(c + 0x70);
    k.recip_y = rd32(c + 0x74);
    k.near_ = rd32(c + 0x80);
    k.far_ = rd32(c + 0x84);
    return k;
}

// CB3PopcornArray::Render 0x365920 [C asm 0x365A58..0x365DF0].
PopcornVuStatic popcorn_vu_static(const PopcornParams& p, const PopcornCamera& c, u32 g, const u32 scale[4],
                                  const u32 shift[4], const u32 offset[4], u32 now, u32 step) {
    PopcornVuStatic s;
    const u32 far_ = fr::min(c.far_, p.w[0x44 / 4]);                   // min.s
    const u32 dn = fr::sub(far_, c.near_);
    const u32 m2nf = fr::mul(fr::mul(kMinus2, c.near_), far_);
    const u32 npf = fr::add(c.near_, far_);
    const u32 k = fr::fpu_div(kOne, dn);
    const u32 zb = fr::mul(k, m2nf), za = fr::mul(k, npf);
    const u32 zl[4] = {zb, za, far_, far_};                            // row 3's z lane keeps row 2's (one CGtV4d)
    for (int r = 0; r < 4; ++r) {
        s.q[r][0] = add0(c.view[r][0]);
        s.q[r][1] = add0(c.view[r][1]);
        s.q[r][2] = add0(zl[r]);
        s.q[r][3] = mul1(c.view[r][2]);
    }
    for (int l = 0; l < 4; ++l) { s.q[4][l] = scale[l]; s.q[5][l] = shift[l]; s.q[6][l] = offset[l]; }
    s.q[7][0] = add0(kZero);
    s.q[7][1] = add0(g);
    s.q[7][2] = add0(kZero);
    s.q[7][3] = mul1(now);
    s.q[8][0] = add0(c.recip_x);
    s.q[8][1] = add0(c.recip_y);
    s.q[8][2] = add0(fr::mul(p.w[0x10 / 4], fr::add(kPoint2, c.window_x)));
    s.q[8][3] = mul1(p.w[0xC / 4]);
    for (int l = 0; l < 4; ++l) {
        const u32 a = fr::mul(k128, p.w[0x14 / 4 + l]), b = fr::mul(k128, p.w[0x24 / 4 + l]);
        s.q[9][l] = l < 3 ? add0(a) : mul1(a);
        s.q[10][l] = l < 3 ? add0(b) : mul1(b);
        s.q[11][l] = s.q[12][l] = l < 3 ? add0(p.w[0x34 / 4 + l]) : mul1(p.w[0x34 / 4 + l]);
    }
    s.q[13][0] = 0; s.q[13][1] = 0x402B4000u; s.q[13][2] = 0x4124u; s.q[13][3] = 0;
    s.q[14][0] = add0(kZero); s.q[14][1] = add0(kZero); s.q[14][2] = add0(kOne); s.q[14][3] = mul1(kOne);
    s.q[15][0] = s.q[14][0]; s.q[15][1] = add0(kZero); s.q[15][2] = add0(step); s.q[15][3] = s.q[14][3];
    return s;
}

std::vector<PopcornBatch> popcorn_batches(int capacity) {
    std::vector<PopcornBatch> out;
    const int n4 = capacity / 4, nb = (n4 + 19) / 20;
    for (int i = 0; i < nb; ++i) out.push_back({20 * i, i < nb - 1 ? 20 : n4 - (nb - 1) * 20, i == 0});
    return out;
}

bool popcorn_batches_from_chain(const std::uint8_t* c, std::size_t bytes, u32 rec_base, std::vector<PopcornBatch>& out) {
    out.clear();
    const std::size_t nq = bytes / 16;
    if (nq < 1 || rd64(c) != 0x10000000ull || rd64(c + 8) != 0x0500000000000000ull) return false;
    for (std::size_t q = 1; q + 1 < nq; q += 2) {
        const u64 tag = rd64(c + 16 * q), vif = rd64(c + 16 * q + 8);
        const u32 qwc = u32(tag & 0xFFFF), addr = u32(tag >> 32) & ~0x30000000u, base = rec_base & ~0x30000000u;
        if (((tag >> 28) & 7) != 3 || qwc % 9 || u32(vif) != 0x01000101u || u32(vif >> 32) != (0x6C008000u | (qwc << 16)))
            return false;
        if (addr < base || (addr - base) % 0x90) return false;
        const u64 t2 = rd64(c + 16 * (q + 1)), v2 = rd64(c + 16 * (q + 1) + 8);
        const u32 itop = u32(v2), kick = u32(v2 >> 32);
        if ((itop >> 24) != 0x04 || (itop & 0x3FF) != qwc / 9 || (kick != 0x15000000u && kick != 0x17000000u)) return false;
        out.push_back({int((addr - base) / 0x90), int(qwc / 9), kick == 0x15000000u});
        if (t2 == 0x60000000ull) return true;   // ret
        if (t2 != 0x10000000ull) return false;
    }
    return false;
}

u64 popcorn_alpha(const u64 modes[], const PopcornParams& p) { return modes[p.w[0x8 / 4]]; }

// Pairs 0x0000..0x0034 and 0x0035..0x003D [C listing, checked by b3vucheck_popcorn].
PopcornSetup popcorn_setup(const PopcornVuStatic& st) {
    PopcornSetup s;
    const auto& S = st.q;
    for (int r = 0; r < 4; ++r) {
        const u32 h = fr::mul(S[r][3], kHalf);                                       // 0x0005..0x0008 muli.w
        for (int l = 0; l < 2; ++l) {
            const u32 v = fr::sub(S[r][l], h);                                       // 0x000A..0x000D subw.xy
            s.row[r][l] = fr::add(v, v);                                             // 0x0010..0x0013 add.xy
        }
        s.row[r][3] = S[r][3];
    }
    const u32 za = S[1][2];
    s.row[3][2] = fr::add(fr::mul(za, S[3][3]), S[0][2]);                            // 0x000F, 0x0014
    s.row[2][2] = fr::mul(za, S[2][3]);                                              // 0x0015
    s.row[0][2] = fr::mul(za, S[0][3]);                                              // 0x0016
    s.row[1][2] = fr::mul(za, S[1][3]);                                              // 0x0017
    s.half_scale[0] = fr::mul(S[4][0], kHalf);                                       // 0x0009
    s.half_scale[1] = fr::mul(S[4][1], kHalf);
    s.zscale = S[4][2];
    s.shift[0] = fr::add(S[5][0], S[6][0]);                                          // 0x000E
    s.shift[1] = fr::add(S[5][1], S[6][1]);
    s.shift[2] = S[5][2];
    const u32 gw = fr::mul(kOne, S[7][1]);                                           // 0x001E muly.w
    s.grav[0] = fr::mul(S[7][0], kHalf);                                             // 0x0022 muli
    s.grav[1] = fr::mul(S[7][1], kHalf);
    s.grav[2] = fr::mul(S[7][2], kHalf);
    s.grav[3] = fr::mul(gw, kHalf);
    s.now = S[7][3];                                                                 // 0x0035/0x0039 mr32.z
    s.dead = fr::sub(S[7][3], S[8][3]);                                              // 0x003A
    for (int l = 0; l < 2; ++l) s.half_px[l] = fr::mul(fr::mul(S[8][l], S[8][2]), s.half_scale[l]);   // 0x001C, 0x0020
    s.size = S[8][2];
    const u32 q = fr::vu_div(kOne, S[8][3]);                                         // 0x001C div
    for (int l = 0; l < 4; ++l) {
        s.c0[l] = S[9][l];
        s.dc[l] = fr::mul(fr::mul(fr::sub(S[10][l], S[9][l]), q), q);                // 0x001F, 0x0023, 0x0027
    }
    const u32 p = fr::vu_div(kOne, S[2][2]);                                         // 0x0008 ercpr (far')
    for (int l = 0; l < 4; ++l) s.fade[l] = fr::mul(fr::sub(S[11][l], kOne), p);    // 0x0030, 0x0034
    s.st0[0] = S[14][0];
    s.st0[1] = S[14][1];
    s.st_base[0] = fr::sub(S[14][2], S[14][0]);                                      // 0x0013/0x0017/0x001B
    s.st_base[1] = fr::sub(S[14][3], S[14][1]);
    return s;
}

void popcorn_particles(const PopcornSetup& s, const u32* blocks, int nblocks, const u32* stale_p1, std::vector<PopcornParticle>& out) {
    out.clear();
    for (int b = 0; b < nblocks; ++b) {
        const u32* B = blocks + 36 * b;
        for (int j = 0; j < 4; ++j) {
            const u32 ts = B[j];
            const u32 a = fr::sub(ts, s.now);                    // 0x0043 subz: S flag = born before now
            const u32 d = fr::sub(ts, s.dead);                   // 0x0046 subw: S flag = dead
            if (!(a & 0x80000000u) || (d & 0x80000000u)) continue;
            const u32 sq = fr::mul(a, a);
            const u32* P = (b == 0 && j == 1 && stale_p1) ? stale_p1 : B + 4 + 8 * j;
            const u32* V = B + 8 + 8 * j;
            PopcornParticle q;
            q.index = 4 * b + j;
            for (int l = 0; l < 4; ++l) {
                u32 acc = fr::mul(P[l], kOne);                   // mulaw ACC, p, vf00w
                acc = fr::msub(acc, V[l], a);                    // msubax ACC, v, a
                q.pos[l] = fr::madd(acc, s.grav[l], sq);
            }
            q.pos[1] = fr::max(q.pos[1], q.pos[3]);              // 0x004F maxw.y
            q.age2 = fr::add(kZero, sq);                         // 0x005C addx.z (the stored z lane)
            for (int l = 0; l < 4; ++l) q.colour[l] = fr::madd(fr::add(s.c0[l], kZero), s.dc[l], q.age2);   // 0x0095/0x0096
            out.push_back(q);
        }
    }
}

void popcorn_fade(const PopcornSetup& s, u32 depth, u32 out[4]) {
    for (int l = 0; l < 4; ++l) out[l] = fr::madd(fr::add(kOne, kZero), s.fade[l], depth);   // 0x0093/0x0094
}

PopcornStState popcorn_st_start(const PopcornSetup& s) {
    PopcornStState st;
    st.vf21[0] = s.st0[0];
    st.vf21[1] = s.st0[1];
    return st;   // gs = (0, 0): Render's A+D ST
}

float popcorn_view_half_extent(const PopcornSetup& s, float depth) { return bitsf(s.size) * std::sqrt(std::fabs(depth)); }

void popcorn_project(const PopcornSetup& s, const std::vector<PopcornParticle>& parts, PopcornStState& st,
                     std::vector<PopcornSprite>& out, std::vector<int>* culled) {
    out.clear();
    if (culled) culled->clear();
    bool first = true;
    for (const PopcornParticle& q : parts) {
        PopcornSprite sp;
        sp.index = q.index;
        for (int l = 0; l < 4; ++l) {
            u32 acc = fr::mul(s.row[3][l], kOne);                // 0x0054 mulaw ACC, vf31, vf00w
            acc = fr::madd(acc, s.row[0][l], q.pos[0]);
            acc = fr::madd(acc, s.row[1][l], q.pos[1]);
            sp.clip[l] = fr::madd(acc, s.row[2][l], q.pos[2]);
        }
        if (clip_bits(sp.clip)) {                                // 0x005B clipw, 0x005F fcand 0x3F
            if (culled) culled->push_back(q.index);
            continue;
        }
        const u32 d = sp.clip[3];
        const u32 P = fr::vu_div(kOne, d), Q = fr::vu_sqrt(d);  // 0x008E ercpr, sqrt
        u32 fk[4];
        popcorn_fade(s, d, fk);
        for (int l = 0; l < 4; ++l) sp.rgba[l] = ftoi(fr::mul(fk[l], q.colour[l]), 0);   // 0x009A mul, 0x00A3 ftoi0
        u32 c[2][3];
        for (int l = 0; l < 2; ++l) {
            const u32 acc = fr::mul(s.half_scale[l], sp.clip[l]);   // 0x0097 mula.xy
            c[0][l] = fr::msub(acc, s.half_px[l], Q);               // 0x0098 msubq
            c[1][l] = fr::madd(acc, s.half_px[l], Q);               // 0x0099 maddq
        }
        c[0][2] = c[1][2] = fr::add(kZero, s.zscale);               // 0x008A/0x008B addz.z
        for (int k = 0; k < 2; ++k)
            for (int l = 0; l < 3; ++l) sp.xyz[k][l] = u32(ftoi(fr::madd(fr::add(s.shift[l], kZero), c[k][l], P), 4));   // 0x009E..0x00A5
        if (first) {                                             // 0x009B: the prologue's flip
            for (int l = 0; l < 2; ++l) st.vf21[l] = fr::sub(s.st_base[l], st.vf21[l]);
            first = false;
        }
        for (int l = 0; l < 2; ++l) {
            sp.st[0][l] = st.gs[l];                              // corner 0: the GS ST in force (the previous sprite's)
            sp.st[1][l] = st.gs[l] = st.vf21[l];                 // 0x00A3 sq.xyz vf21 (ST before the second XYZF2)
            st.vf21[l] = fr::sub(s.st_base[l], st.vf21[l]);      // 0x00AA / 0x00B9 / 0x00C8, after every store
        }
        out.push_back(sp);
    }
}

std::vector<std::uint8_t> popcorn_vif_static(const PopcornVuStatic& st) {
    std::vector<std::uint8_t> b;
    put32(b, 0x01000101);   // STCYCL 1,1
    put32(b, 0x6C1003F0);   // UNPACK V4-32 x 16 -> 1008
    for (const auto& q : st.q)
        for (u32 w : q) put32(b, w);
    put32(b, 0x03000000);   // BASE 0
    put32(b, 0x020000B4);   // OFFSET 0xB4
    return b;
}

std::vector<std::uint8_t> popcorn_vif_batch(const PopcornBatch& s, const u32* blocks) {
    std::vector<std::uint8_t> b;
    const u32 n = u32(9 * s.blocks);
    put32(b, 0x01000101);
    put32(b, 0x6C008000u | (n << 16));
    for (u32 i = 0; i < 4 * n; ++i) put32(b, blocks[i]);
    put32(b, 0x04000000u | u32(s.blocks));
    put32(b, s.mscalf ? 0x15000000u : 0x17000000u);
    return b;
}

}  // namespace b3asset
