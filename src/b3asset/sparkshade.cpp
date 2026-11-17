#include "b3asset/sparkshade.h"

#include <cstring>

#include "b3host/ps2float.h"

namespace b3asset {

namespace {
namespace fr = b3fp::ref;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
constexpr u32 kZero = 0, kOne = 0x3F800000u, kHalf = 0x3F000000u;
constexpr u32 kLifeScale = 0x447FFFF0u;  // D_004E115C = 1023.999 [C ELF]
constexpr u32 kHistMin = 0x3C449BA6u;    // D_004E0FC4 = 0.012 (Set's near clamp) [C ELF]

u32 rd32(const std::uint8_t* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
u64 rd64(const std::uint8_t* p) { return u64(rd32(p)) | u64(rd32(p + 4)) << 32; }
void put32(std::vector<std::uint8_t>& b, u32 v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
float bitsf(u32 b) { float f; std::memcpy(&f, &b, 4); return f; }
// FTOI4 as the VU does it [C VU manual; same as trailshade.cpp].
u32 ftoi4(u32 b) {
    const double v = double(b3fp::flt(fr::clamp(b))) * 16.0;
    if (v >= 2147483647.0) return 0x7FFFFFFFu;
    if (v <= -2147483648.0) return 0x80000000u;
    return u32(std::int32_t(v));
}
// "used": the MAC flags of the difference have neither Z nor S for the lane [C program 0x0020..0x0024, vudis lane masks
// 0x88/0x44/0x22/0x11 = Sx|Zx ... Sw|Zw].
bool positive(u32 d) { return (d & 0x7FFFFFFFu) != 0 && !(d & 0x80000000u); }
// CLIP judgement of one vector against its w [C vu1.cpp CLIP].
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

float SparkParams::f(int off) const { return bitsf(w[off / 4]); }

SparkParams spark_params_from_guest(const std::uint8_t p[0x7C]) {
    SparkParams s;
    for (int i = 0; i < 31; ++i) s.w[i] = rd32(p + 4 * i);
    return s;
}

SparkFrame spark_frame_from_guest(const std::uint8_t p[0x60]) {
    SparkFrame f;
    for (int r = 0; r < 6; ++r)
        for (int l = 0; l < 4; ++l) f.row[r][l] = rd32(p + 16 * r + 4 * l);
    return f;
}

// unit_00263120.s, in its order [L: nonmatching function; the order is the asm's].
SparkFrame spark_frame_set(const std::uint8_t cam[0x230], float t, const SparkFrame& prev) {
    auto f = [&](int off) { return rd32(cam + off); };
    SparkFrame o = prev;
    const u32 n = f(0x18C), fnear = fr::min(f(0x19C), kHistMin);              // min.s
    const u32 m2n = fr::mul(0xC0000000u, n);                                    // -2 x n
    const u32 m2nf = fr::mul(m2n, fnear);
    const u32 k = fr::fpu_div(kOne, fr::sub(n, fnear));
    const u32 npf = fr::add(n, fnear);
    const u32 sw = fr::mul(k, m2nf), sz = fr::mul(k, npf);
    const u32 sx = fr::fpu_div(f(0x218), fr::add(0x43800000u, f(0x218)));
    const u32 sy = fr::fpu_div(f(0x21C), fr::add(0x43800000u, f(0x21C)));
    o.row[3][3] = fr::sub(kOne, kOne);                                          // vsubw.w row3, vf0, vf0w
    u32 a[4][3];
    for (int r = 0; r < 4; ++r)
        for (int l = 0; l < 3; ++l) a[r][l] = f(0xE0 + 0x10 * r + 4 * l);
    for (int r = 0; r < 4; ++r)
        for (int l = 0; l < 2; ++l) o.row[r][l] = fr::add(a[r][l], a[r][l]);   // vadd.xy row, E0, E0
    for (int r = 0; r < 4; ++r) o.row[r][3] = fr::add(o.row[3][3], a[r][2]);   // vaddz.w row, row3, E0z (row3.w = 0)
    const u32 hz = fr::mul(kHalf, fr::mul(f(0x218), f(0x208)));
    const u32 hw = fr::mul(kHalf, fr::mul(f(0x21C), f(0x20C)));
    for (int r = 0; r < 4; ++r)
        for (int l = 0; l < 2; ++l) o.row[r][l] = fr::sub(o.row[r][l], o.row[r][3]);   // vsubw.xy
    const u32 x = fr::fpu_div(f(0x200), sx), y = fr::fpu_div(f(0x204), sy);
    for (int r = 0; r < 4; ++r) {
        o.row[r][0] = fr::mul(o.row[r][0], sx);   // vmul.xy row, row, s
        o.row[r][1] = fr::mul(o.row[r][1], sy);
    }
    for (int r : {3, 0, 1, 2}) o.row[r][2] = fr::mul(sz, o.row[r][3]);       // vmulw.z row, s, roww
    o.row[3][2] = fr::add(o.row[3][2], sw);                                    // vaddw.z row3, row3, sw
    o.row[4][0] = fr::add(kZero, x);
    o.row[4][1] = fr::add(kZero, y);
    o.row[4][2] = fr::add(kZero, hz);
    o.row[4][3] = fr::mul(kOne, hw);                                           // vmulx.w
    o.row[5][0] = fr::add(kZero, b3fp::bits(t));
    return o;
}

int spark_intervals(const SparkFrame h[18], u32 blur) {
    int i = 1;
    while (i < 17) {
        if (!fr::lt(fr::sub(h[i].row[5][0], h[0].row[5][0]), blur)) break;
        if (fr::eq(h[i].row[5][0], h[i + 1].row[5][0])) break;
        ++i;
    }
    return i;
}

// UploadStaticDataAndCallCode 0x263420 [C asm unit_00263420.s: 912 via vaddx/vmulx from $vf0, 913 integer stores].
SparkVuStatic spark_vu_static(const SparkParams& p, u32 a0, int intervals, const u32 cam914[4], const u32 tag[4],
                              const SparkFrame hist[18]) {
    SparkVuStatic s;
    const u32 x = p.w[0x6C / 4];
    s.q[0][0] = fr::add(kZero, x);
    s.q[0][1] = fr::add(kZero, p.w[0x70 / 4]);
    s.q[0][2] = fr::add(kZero, p.w[0x78 / 4]);
    s.q[0][3] = fr::mul(kOne, fr::fpu_div(fr::sub(p.w[0x74 / 4], x), p.w[0x5C / 4]));
    s.q[1][0] = u32(intervals);
    s.q[1][1] = a0;
    s.q[1][2] = p.w[0x58 / 4];
    s.q[1][3] = p.w[0x50 / 4];
    for (int l = 0; l < 4; ++l) { s.q[2][l] = cam914[l]; s.q[3][l] = tag[l]; }
    for (int k = 0; k < 18; ++k)
        for (int r = 0; r < 6; ++r)
            for (int l = 0; l < 4; ++l) s.q[4 + 6 * k + r][l] = hist[k].row[r][l];
    return s;
}

void spark_cam914_from_record(const std::uint8_t cam[0x230], u32 out[4]) {
    out[0] = rd32(cam + 0x170);
    out[1] = rd32(cam + 0x174);
    out[2] = rd32(cam + 0x178);
    out[3] = rd32(cam + 0x168);
}

// _SparkSetRGBAQ [C asm 0x264284..0x264338: ACC = 0 + 0.5; madd.s ACC + 64 x c (128 for alpha); cvt.w.s].
u64 spark_rgbaq(const SparkParams& p, int ring) {
    const u32 h = fr::add(kZero, kHalf);
    u64 v = u64(kOne) << 32;
    for (int c = 0; c < 4; ++c) {
        const u32 scale = c == 3 ? 0x43000000u : 0x42800000u;
        const std::int32_t q = fr::cvt_w_s(fr::madd(h, scale, p.w[4 * ring + c]));
        v |= u64(std::int64_t(q)) << (8 * c);   // dsll32 / dsra32: sign-extended, shifted, OR'd
    }
    return v;
}

// CSparkBank::Construct 0x264170 [L nonmatching source; C asm order of the scalars 0x2641C0..0x264280].
std::vector<SparkBatch> spark_bank_batches(const SparkParams& p, bool crash_bank, const int* ring_capacity) {
    std::vector<SparkBatch> out;
    const std::int32_t n = std::int32_t(p.w[(crash_bank ? 0x64 : 0x60) / 4]);
    if (n == 0) return out;
    const u32* l = &p.w[0x40 / 4];
    const u32 mx = fr::max(l[3], fr::max(l[2], fr::max(l[0], l[1])));
    const u32 k = fr::fpu_div(fr::cvt_s_w(n), fr::add(l[3], fr::add(l[2], fr::add(l[0], l[1]))));
    const u32 scale = fr::fpu_div(kLifeScale, mx);
    for (int r = 0; r < 4; ++r) {
        const int cap = ring_capacity ? ring_capacity[r] : ((fr::cvt_w_s(fr::mul(l[r], k)) + 3) & ~3);
        const int nb = (cap / 4 + 10) / 11;
        const int packed = fr::cvt_w_s(fr::mul(scale, l[r])) & ~0xF;
        for (int b = 0; b < nb; ++b) {
            SparkBatch s;
            s.ring = r;
            s.first = 11 * b;
            s.blocks = b < nb - 1 ? 11 : cap / 4 - (nb - 1) * 11;
            s.itop = u32(s.blocks | packed) & 0x3FF;
            s.mscalf = r == 0 && b == 0 && !crash_bank;
            s.rgbaq = spark_rgbaq(p, r);
            out.push_back(s);
        }
    }
    return out;
}

bool spark_bank_batches_from_chain(const std::uint8_t* c, std::size_t bytes, const u32 base[4], std::vector<SparkBatch>& out) {
    out.clear();
    const std::size_t nq = bytes / 16;
    std::size_t q = 0;
    auto lo = [&](std::size_t i) { return rd64(c + 16 * i); };
    auto hi = [&](std::size_t i) { return rd64(c + 16 * i + 8); };
    if (nq < 1 || lo(0) != 0x10000000ull || hi(0) != 0x0500000000000000ull) return false;
    q = 1;
    u64 rgbaq = 0;
    while (q < nq) {
        const u64 tag = lo(q), vif = hi(q);
        const u32 id = u32(tag >> 28) & 7;
        if (id == 1 && (tag & 0xFFFF) == 2 && vif == 0x5000000210000000ull && q + 2 < nq) {   // FLUSHE; DIRECT 2: RGBAQ
            if (lo(q + 1) != 0x1000000000008001ull || hi(q + 1) != 0xE || hi(q + 2) != 1) return false;
            rgbaq = lo(q + 2);
            q += 3;
            continue;
        }
        if (id != 3 || q + 1 >= nq) return false;   // REF: the batch's blocks
        const u32 qwc = u32(tag & 0xFFFF), addr = u32(tag >> 32);
        if (qwc % 9 || u32(vif) != 0x01000101u || u32(vif >> 32) != (0x6C008000u | (qwc << 16))) return false;
        SparkBatch s;
        s.blocks = int(qwc / 9);
        s.ring = -1;
        for (int r = 0; r < 4; ++r) {
            const u32 b = base[r] & ~0x30000000u, a = addr & ~0x30000000u;
            if (a >= b && (a - b) % 0x90 == 0 && (r == 3 || a < (base[r + 1] & ~0x30000000u))) { s.ring = r; s.first = int((a - b) / 0x90); }
        }
        if (s.ring < 0) return false;
        const u64 t2 = lo(q + 1), v2 = hi(q + 1);
        const u32 itop = u32(v2), kick = u32(v2 >> 32);
        if ((t2 != 0x10000000ull && t2 != 0x60000000ull) || (itop >> 24) != 0x04 || (kick != 0x15000000u && kick != 0x17000000u))
            return false;
        s.itop = itop & 0x3FF;
        s.mscalf = kick == 0x15000000u;
        s.rgbaq = rgbaq;
        out.push_back(s);
        q += 2;
        if (t2 == 0x60000000ull) break;   // ret
    }
    return true;
}

bool spark_array_renderable(const SparkParams& p, u32 now, u32 last_normal, u32 last_crash, bool is_crash, bool* crash_bank) {
    const u32* l = &p.w[0x40 / 4];
    const u32 r = fr::max(l[3], fr::max(l[2], fr::max(l[0], l[1])));
    const u32 limit = fr::sub(now, r);
    const bool a = fr::lt(limit, last_normal);
    const bool c = is_crash && fr::lt(limit, last_crash);
    if (crash_bank) *crash_bank = c;
    return a || c;
}

void spark_segments(const SparkVuStatic& st, int k, u32 itop, const u32* blocks, int nblocks, std::vector<SparkSegment>& out) {
    out.clear();
    const u32 tk = st.frame_row(k, 5)[0], tk1 = st.frame_row(k + 1, 5)[0];
    const u32 a0u = st.q[1][1], g = st.q[1][3];
    const u32 t29 = fr::add(kZero, tk);                       // 0x0011 addx vf29, vf01(0), vf15x
    const u32 dt = fr::sub(tk, tk1);                          // 0x0012 subx.x vf26, vf15, vf16x
    const u32 life = fr::mul(fr::chop(double(std::int32_t(std::int16_t(itop)))), a0u);   // 0x000C mfir, 0x0010 itof0, 0x0014
    const u32 t27 = fr::add(kZero, tk1);                      // 0x0015
    const u32 t30 = fr::sub(t29, dt);                         // 0x0016
    const u32 t31 = fr::sub(t29, life);                       // 0x0018
    const u32 half[4] = {kZero, fr::mul(fr::add(kZero, g), kHalf), kZero, fr::mul(g, kHalf)};   // 0x0013/0x0014/0x0017/0x0019
    for (int b = 0; b < nblocks; ++b) {
        const u32* B = blocks + 36 * b;
        for (int j = 0; j < 4; ++j) {
            const u32 ts = B[j];
            // 0x001C: ts - (t_k - L) (dead at t_k unless > 0); 0x001D: t_k+1 - ts (born after t_k+1 unless > 0)
            if (!positive(fr::sub(ts, t31)) || !positive(fr::sub(t27, ts))) continue;
            const u32 a0 = fr::sub(t29, ts), a1 = fr::sub(t30, ts);
            const u32 sq0 = fr::mul(a0, a0), sq1 = fr::mul(a1, a1);
            const u32* P = B + 4 + 8 * j;
            const u32* V = P + 4;
            SparkSegment s;
            s.spark = 4 * b + j;
            for (int l = 0; l < 4; ++l) {
                u32 acc = fr::add(P[l], kZero);       // addax ACC, p, vf00x
                acc = fr::madd(acc, V[l], a0);        // maddax ACC, v, a0
                s.head[l] = fr::madd(acc, half[l], sq0);
                acc = fr::msub(acc, V[l], dt);        // msubax ACC, v, dt
                s.tail[l] = fr::madd(acc, half[l], sq1);
            }
            s.head[1] = fr::max(s.head[1], s.head[3]);   // 0x0026 maxw.y (head, before the store)
            s.tail[1] = fr::max(s.tail[1], s.tail[3]);   // 0x004F maxw.y (tail, after the reload)
            out.push_back(s);
        }
    }
}

void spark_st(const SparkVuStatic& st, int k, u32 s[2], u32 t[2]) {
    const u32 t0 = st.frame_row(0, 5)[0];
    const u32 acc = fr::add(kZero, st.q[0][0]);                            // 0x008F addax.x ACC, vf00, vf24x
    s[0] = fr::madd(acc, fr::sub(t0, st.frame_row(k, 5)[0]), st.q[0][3]);       // 0x0088, 0x0090
    s[1] = fr::madd(acc, fr::sub(t0, st.frame_row(k + 1, 5)[0]), st.q[0][3]);   // 0x0089, 0x0093
    t[0] = st.q[0][1];
    t[1] = st.q[0][2];
}

void spark_project(const SparkVuStatic& st, int k, const std::vector<SparkSegment>& segs, std::vector<SparkQuad>& out,
                   std::vector<int>* culled, bool screen) {
    out.clear();
    if (culled) culled->clear();
    const u32 *R = st.frame_row(k, 0), *R1 = st.frame_row(k + 1, 0);   // rows: R + 4 r
    auto xf = [](const u32* M, const u32 p[4], u32 c[4]) {
        for (int l = 0; l < 4; ++l) {
            u32 acc = fr::add(M[12 + l], kZero);   // addax ACC, row3, vf00x
            acc = fr::madd(acc, M[l], p[0]);
            acc = fr::madd(acc, M[4 + l], p[1]);
            c[l] = fr::madd(acc, M[8 + l], p[2]);
        }
    };
    const u32 *f4 = st.frame_row(k, 4), *g4 = st.frame_row(k + 1, 4);
    const u32 sk[2] = {fr::add(kZero, f4[2]), fr::add(kZero, f4[3])};   // 0x0077/0x0078 vf30.xy
    const u32 sk1[2] = {fr::add(kZero, g4[2]), fr::add(kZero, g4[3])};  // 0x007B/0x007C vf31.xy
    const u32 width = st.q[1][2];                                        // 0x007C vf30.z = 913.z
    const u32* cam = st.q[2];                                            // 0x007D vf23 = 914
    u32 s[2], t[2];
    spark_st(st, k, s, t);
    for (const SparkSegment& g : segs) {
        SparkQuad q;
        q.spark = g.spark;
        xf(R, g.head, q.clip_head);
        xf(R1, g.tail, q.clip_tail);
        if (clip_bits(q.clip_head) | clip_bits(q.clip_tail)) {   // 0x005C fcand vi01, 0xFFF
            if (culled) culled->push_back(g.spark);
            continue;
        }
        const u32 A[3] = {fr::mul(q.clip_head[0], f4[0]), fr::mul(q.clip_head[1], f4[1]), q.clip_head[3]};   // 0x0079/0x007A
        const u32 B[3] = {fr::mul(q.clip_tail[0], g4[0]), fr::mul(q.clip_tail[1], g4[1]), q.clip_tail[3]};
        const u32 d[3] = {fr::sub(B[0], A[0]), fr::sub(B[1], A[1]), fr::sub(B[2], A[2])};               // 0x007E
        const u32 c[3] = {fr::msub(fr::mul(d[1], B[2]), B[1], d[2]),                                     // 0x0082/0x0083
                          fr::msub(fr::mul(d[2], B[0]), B[2], d[0]),
                          fr::msub(fr::mul(d[0], B[1]), B[0], d[1])};
        const u32 sq[3] = {fr::mul(c[0], c[0]), fr::mul(c[1], c[1]), fr::mul(c[2], c[2])};               // 0x0087
        const u32 n2 = fr::madd(fr::add(sq[2], sq[0]), kOne, sq[1]);                                     // 0x008B/0x008C
        const u32 Q = fr::vu_rsqrt(width, n2);                                                           // 0x0090 rsqrt
        u32 n[3];
        for (int l = 0; l < 3; ++l) n[l] = fr::mul(fr::add(c[l], kZero), Q);                            // 0x008A, 0x009D
        for (int l = 0; l < 3; ++l) {
            q.corner[0][l] = fr::add(A[l], n[l]);
            q.corner[1][l] = fr::sub(A[l], n[l]);
            q.corner[2][l] = fr::add(B[l], n[l]);
            q.corner[3][l] = fr::sub(B[l], n[l]);
        }
        for (int v = 0; v < 4; ++v) {
            if (screen) {
                const u32* sc = v < 2 ? sk : sk1;
                const u32 r = fr::vu_div(kOne, q.corner[v][2]);   // ERCPR (corners 0, 3) / DIV 1.0 (1, 2): the same quotient
                const u32 scr[3] = {fr::mul(q.corner[v][0], sc[0]), fr::mul(q.corner[v][1], sc[1]), cam[3]};   // vf19..22
                for (int l = 0; l < 3; ++l) q.v[v].xyz[l] = ftoi4(fr::madd(fr::add(cam[l], kZero), scr[l], r));
            }
            q.v[v].s = s[v >> 1];
            q.v[v].t = t[v & 1];
            q.v[v].q = kOne;
            q.v[v].adc = v < 2;
        }
        out.push_back(q);
    }
}

std::vector<std::uint8_t> spark_vif_static(const SparkVuStatic& st) {
    std::vector<std::uint8_t> b;
    put32(b, 0x01000101);   // STCYCL 1,1
    put32(b, 0x6C700390);   // UNPACK V4-32 x 112 -> 0x390
    for (const auto& q : st.q)
        for (u32 w : q) put32(b, w);
    return b;
}

std::vector<std::uint8_t> spark_vif_batch(const SparkBatch& s, const u32* blocks) {
    std::vector<std::uint8_t> b;
    const u32 n = u32(9 * s.blocks);
    put32(b, 0x01000101);
    put32(b, 0x6C008000u | (n << 16));
    for (u32 i = 0; i < 4 * n; ++i) put32(b, blocks[i]);
    put32(b, 0x04000000u | s.itop);
    put32(b, s.mscalf ? 0x15000000u : 0x17000000u);
    return b;
}

}  // namespace b3asset
