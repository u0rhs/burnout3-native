// mpeg2_decoder.cpp -- see mpeg2_decoder.h. Section numbers are ISO 13818-2's [C].
#include "mpeg2_decoder.h"

#include "mpeg_dsp.h"   // the IEEE 1180 integer IDCT and the IPU's CSC (shared with the dev IPU model)
#include "ipu_vlc.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace b3native {

namespace {

constexpr std::uint8_t kZigzag[64] = {   // scan position -> natural index [Figure 7-2]
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
constexpr std::uint8_t kAlternate[64] = {   // [Figure 7-3]
    0, 8, 16, 24, 1, 9, 2, 10, 17, 25, 32, 40, 48, 56, 57, 49, 41, 33, 26, 18, 3, 11, 4, 12, 19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28, 5, 13, 6, 14, 21, 29, 36, 44, 52, 60, 37, 45, 53, 61, 22, 30, 7, 15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63};
constexpr std::uint8_t kDefaultIntra[64] = {   // natural order [6.3.11]
    8, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83};
constexpr std::uint8_t kNonLinear[32] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36,
                                         40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112};   // [Table 7-6]

template <class T, unsigned N> unsigned count(const T (&)[N]) { return N; }

struct Tables {
    IpuVlcLookup mbai, mbt_i, mbt_p, mbt_b, motion, cbp, dc_luma, dc_chroma, dct_zero, dct_one;
    Tables() {
        mbai = ipu_vlc_build(kIpuMbai, count(kIpuMbai));
        mbt_i = ipu_vlc_build(kIpuMbTypeI, count(kIpuMbTypeI));
        mbt_p = ipu_vlc_build(kIpuMbTypeP, count(kIpuMbTypeP));
        mbt_b = ipu_vlc_build(kIpuMbTypeB, count(kIpuMbTypeB));
        motion = ipu_vlc_build(kIpuMotionMagnitude, count(kIpuMotionMagnitude));
        cbp = ipu_vlc_build(kIpuCbp, count(kIpuCbp));
        dc_luma = ipu_vlc_build(kIpuDcSizeLuma, count(kIpuDcSizeLuma));
        dc_chroma = ipu_vlc_build(kIpuDcSizeChroma, count(kIpuDcSizeChroma));
        std::vector<IpuVlcCode> z(kIpuDctZero, kIpuDctZero + count(kIpuDctZero)), o(kIpuDctOne, kIpuDctOne + count(kIpuDctOne));
        z.insert(z.end(), kIpuDctLong, kIpuDctLong + count(kIpuDctLong));
        o.insert(o.end(), kIpuDctLong, kIpuDctLong + count(kIpuDctLong));
        dct_zero = ipu_vlc_build(z.data(), static_cast<unsigned>(z.size()));
        dct_one = ipu_vlc_build(o.data(), static_cast<unsigned>(o.size()));
    }
};
const Tables& tables() { static const Tables t; return t; }

// MSB-first bit reader over one picture's bytes (start code to start code), zero bits past the end
struct Bits {
    const std::uint8_t* p = nullptr;
    std::size_t n = 0, pos = 0;   // pos in bits
    std::uint32_t peek(unsigned k) const {
        std::uint32_t v = 0;
        for (unsigned i = 0; i < k; ++i) {
            const std::size_t b = pos + i;
            v = (v << 1) | ((b >> 3) < n ? (p[b >> 3] >> (7 - (b & 7))) & 1u : 0u);
        }
        return v;
    }
    std::uint32_t get(unsigned k) { const std::uint32_t v = peek(k); pos += k; return v; }
    void skip(unsigned k) { pos += k; }
    bool past_end() const { return (pos >> 3) > n; }
    void align() { pos = (pos + 7) & ~std::size_t(7); }
    // fast path for VLC lookups: 16 bits at once
    std::uint32_t peek16() const {
        const std::size_t byte = pos >> 3;
        std::uint32_t w = 0;
        for (int i = 0; i < 3; ++i) w = (w << 8) | (byte + std::size_t(i) < n ? p[byte + std::size_t(i)] : 0u);
        return (w >> (8 - (pos & 7))) & 0xFFFFu;
    }
};

struct Frame {
    int w = 0, h = 0;   // coded (macroblock multiples)
    std::vector<std::uint8_t> y, cb, cr;
    int type = 0, tref = 0;
    std::uint64_t index = 0;
    void alloc(int W, int H) {
        w = W; h = H;
        y.assign(std::size_t(W) * std::size_t(H), 0);
        cb.assign(std::size_t(W / 2) * std::size_t(H / 2), 128);
        cr.assign(std::size_t(W / 2) * std::size_t(H / 2), 128);
    }
};

}  // namespace

struct Mpeg2Decoder::Impl {
    std::vector<std::uint8_t> buf;   // unconsumed stream bytes
    bool eos = false;
    bool ended = false;   // a sequence_end_code was met: later bytes (padding, a stale buffer) are ignored
    std::deque<Mpeg2Picture> out;
    std::string* err = nullptr;
    std::uint64_t* decoded = nullptr;

    // sequence
    bool have_seq = false, mpeg2 = false;
    int hsize = 0, vsize = 0, mbw = 0, mbh = 0;
    std::uint8_t wi[64], wn[64];   // natural order
    std::uint8_t wi_default[64], wn_default[64];
    // picture
    int pct = 0, tref = 0, f_code[2][2] = {{15, 15}, {15, 15}}, dc_prec = 0, pic_struct = 3, fpfd = 1, cmv = 0;
    int qst = 0, ivf = 0, alt = 0;
    // references
    std::shared_ptr<Frame> fwd, bwd, cur;   // fwd: older reference, bwd: newer (B pictures predict from both)
    std::shared_ptr<Frame> held;            // the newest I / P, output when the next I / P arrives
    std::uint64_t index = 0;

    bool fail(const std::string& m) { if (err->empty()) *err = m; return false; }

    // ---- headers ----
    void load_matrix(Bits& b, std::uint8_t* m) {
        for (int i = 0; i < 64; ++i) m[kZigzag[i]] = static_cast<std::uint8_t>(b.get(8));
    }
    bool sequence_header(Bits& b) {
        hsize = int(b.get(12)); vsize = int(b.get(12));
        b.skip(4 + 4 + 18 + 1 + 10 + 1);   // aspect, frame rate, bit rate, marker, vbv, constrained
        if (b.get(1)) load_matrix(b, wi_default); else std::memcpy(wi_default, kDefaultIntra, 64);
        if (b.get(1)) load_matrix(b, wn_default); else std::memset(wn_default, 16, 64);
        std::memcpy(wi, wi_default, 64); std::memcpy(wn, wn_default, 64);
        mbw = (hsize + 15) / 16; mbh = (vsize + 15) / 16;
        have_seq = true;
        return true;
    }
    bool extension(Bits& b) {
        const int id = int(b.get(4));
        if (id == 1) {   // sequence extension
            mpeg2 = true;
            b.skip(8);   // profile and level
            const int progressive = int(b.get(1)), chroma = int(b.get(2));
            hsize |= int(b.get(2)) << 12; vsize |= int(b.get(2)) << 12;
            mbw = (hsize + 15) / 16; mbh = (vsize + 15) / 16;
            (void)progressive;
            if (chroma != 1) return fail("mpeg2: chroma format " + std::to_string(chroma) + " (only 4:2:0)");
        } else if (id == 8) {   // picture coding extension
            f_code[0][0] = int(b.get(4)); f_code[0][1] = int(b.get(4));
            f_code[1][0] = int(b.get(4)); f_code[1][1] = int(b.get(4));
            dc_prec = int(b.get(2)); pic_struct = int(b.get(2));
            b.skip(1);   // top_field_first
            fpfd = int(b.get(1)); cmv = int(b.get(1)); qst = int(b.get(1)); ivf = int(b.get(1)); alt = int(b.get(1));
            if (pic_struct != 3) return fail("mpeg2: field pictures (picture_structure " + std::to_string(pic_struct) + ")");
            if (!fpfd) return fail("mpeg2: frame_pred_frame_dct 0 (field prediction / field DCT)");
            if (cmv) return fail("mpeg2: concealment motion vectors");
        } else if (id == 3) {   // quant matrix extension
            if (b.get(1)) load_matrix(b, wi);
            if (b.get(1)) load_matrix(b, wn);
        }
        return true;
    }

    // ---- motion ----
    int motion_delta(Bits& b, int fcode) {
        const std::uint32_t nx = b.peek16();
        int code;
        if (nx & 0x8000u) { b.skip(1); code = 0; }
        else {
            const auto& e = tables().motion.at(nx);
            if (!e.len) { fail("mpeg2: bad motion code"); return 0; }
            b.skip(e.len);
            code = b.get(1) ? -e.value : e.value;
        }
        const int r = fcode - 1;
        if (r == 0 || code == 0) return code;
        const int residual = int(b.get(unsigned(r)));
        const int mag = ((std::abs(code) - 1) << r) + residual + 1;
        return code < 0 ? -mag : mag;
    }
    static int wrap(int v, int fcode) {   // [7.6.3.1]
        const int r = fcode - 1, f = 1 << r, low = -16 * f, high = 16 * f - 1, range = 32 * f;
        if (v < low) v += range;
        else if (v > high) v -= range;
        return v;
    }

    // ---- prediction ----
    static void predict_block(const std::uint8_t* ref, int rw, int rh, int x, int y, int mvx, int mvy, int bw, int bh,
                              std::uint8_t* dst, int dw, bool average) {
        const int ix = x + (mvx >> 1), iy = y + (mvy >> 1), hx = mvx & 1, hy = mvy & 1;
        for (int j = 0; j < bh; ++j)
            for (int i = 0; i < bw; ++i) {
                auto px = [&](int xx, int yy) {
                    xx = std::clamp(xx, 0, rw - 1); yy = std::clamp(yy, 0, rh - 1);
                    return int(ref[std::size_t(yy) * std::size_t(rw) + std::size_t(xx)]);
                };
                int v;
                if (!hx && !hy) v = px(ix + i, iy + j);
                else if (hx && !hy) v = (px(ix + i, iy + j) + px(ix + i + 1, iy + j) + 1) >> 1;
                else if (!hx && hy) v = (px(ix + i, iy + j) + px(ix + i, iy + j + 1) + 1) >> 1;
                else v = (px(ix + i, iy + j) + px(ix + i + 1, iy + j) + px(ix + i, iy + j + 1) + px(ix + i + 1, iy + j + 1) + 2) >> 2;
                std::uint8_t& d = dst[j * dw + i];
                d = average ? std::uint8_t((int(d) + v + 1) >> 1) : std::uint8_t(v);
            }
    }
    // the macroblock's prediction (16x16 luma, two 8x8 chroma) from one reference, or averaged into an existing one
    void predict_mb(const Frame& ref, int mbx, int mby, int mvx, int mvy, std::uint8_t* py, std::uint8_t* pcb, std::uint8_t* pcr, bool average) {
        predict_block(ref.y.data(), ref.w, ref.h, mbx * 16, mby * 16, mvx, mvy, 16, 16, py, 16, average);
        // chroma vectors: the luma vector / 2, truncated towards zero [7.6.3.7]
        const int cx = mvx / 2, cy = mvy / 2;
        predict_block(ref.cb.data(), ref.w / 2, ref.h / 2, mbx * 8, mby * 8, cx, cy, 8, 8, pcb, 8, average);
        predict_block(ref.cr.data(), ref.w / 2, ref.h / 2, mbx * 8, mby * 8, cx, cy, 8, 8, pcr, 8, average);
    }

    // ---- blocks ----
    bool block(Bits& b, int k, bool intra, int qs, int* dc_pred, std::int16_t* blk) {
        std::memset(blk, 0, 64 * sizeof(std::int16_t));
        const std::uint8_t* scan = alt ? kAlternate : kZigzag;
        const std::uint8_t* w = intra ? wi : wn;
        int sum = 0, pos = 0;
        if (intra) {
            const bool luma = k < 4;
            const auto& e = (luma ? tables().dc_luma : tables().dc_chroma).at(b.peek16());
            if (!e.len) return fail("mpeg2: bad dc size");
            b.skip(e.len);
            const int size = e.value;
            int diff = 0;
            if (size) {
                diff = int(b.get(unsigned(size)));
                if (!(diff >> (size - 1))) diff = diff - (1 << size) + 1;
            }
            int& pred = dc_pred[luma ? 0 : k - 3];
            pred += diff;
            const int f = pred * (8 >> dc_prec);
            blk[0] = std::int16_t(f);
            sum = f;
            pos = 1;
        }
        const IpuVlcLookup& t = (intra && ivf) ? tables().dct_one : tables().dct_zero;
        bool first = !intra;
        for (;;) {
            int run, level;
            const std::uint32_t nx = b.peek16();
            if (first && (nx & 0x8000u)) {   // non-intra first coefficient: "1s" = (0, +-1) [B-14 note]
                b.skip(1);
                run = 0; level = b.get(1) ? -1 : 1;
            } else {
                const auto& e = t.at(nx);
                if (!e.len) return fail("mpeg2: bad dct code");
                b.skip(e.len);
                if (e.value == -1) break;   // end of block
                if (e.value == -2) {        // escape: 6-bit run, 12-bit signed level [7.2.2.3]
                    run = int(b.get(6));
                    level = int(b.get(12));
                    if (level >= 2048) level -= 4096;
                    if (level == 0 || level == -2048) return fail("mpeg2: forbidden escape level");
                } else {
                    run = e.value;
                    level = b.get(1) ? -e.value2 : e.value2;
                }
            }
            first = false;
            pos += run;
            if (pos > 63) return fail("mpeg2: coefficient past 63");
            const int n = scan[pos];
            const int sgn = level < 0 ? -1 : 1;
            int f = intra ? (2 * level * int(w[n]) * qs) / 32 : ((2 * level + sgn) * int(w[n]) * qs) / 32;
            f = std::clamp(f, -2048, 2047);
            blk[n] = std::int16_t(f);
            sum += f;
            ++pos;
        }
        if ((sum & 1) == 0) blk[63] = std::int16_t(blk[63] ^ 1);   // mismatch control [7.4.4]
        mpeg_dsp::idct(blk);
        return true;
    }

    // ---- one picture ----
    bool picture(Bits& b, std::shared_ptr<Frame>& f) {
        f = std::make_shared<Frame>();
        f->alloc(mbw * 16, mbh * 16);
        f->type = pct; f->tref = tref; f->index = index++;
        int pmv[2][2] = {{0, 0}, {0, 0}};
        int last_type = 0, last_mv[2][2] = {{0, 0}, {0, 0}};   // a skipped B macroblock repeats the previous one
        std::int16_t blk[6][64];
        std::uint8_t py[256], pcb[64], pcr[64];
        // slices: the caller positioned b at the first slice start code's payload; walk the start codes
        for (;;) {
            // find the next start code
            b.align();
            std::size_t byte = b.pos >> 3;
            while (byte + 3 < b.n && !(b.p[byte] == 0 && b.p[byte + 1] == 0 && b.p[byte + 2] == 1)) ++byte;
            if (byte + 3 >= b.n) break;
            const int code = b.p[byte + 3];
            if (code < 0x01 || code > 0xAF) break;   // not a slice: the picture ends
            b.pos = (byte + 4) * 8;
            const int mby = code - 1;
            if (mby >= mbh) return fail("mpeg2: slice row past the picture");
            const int qcode = int(b.get(5));
            int qs = qst ? kNonLinear[qcode] : qcode * 2;
            if (b.get(1)) {   // extra slice information
                b.skip(8);
                while (b.get(1)) b.skip(8);
            }
            int dc_pred[3];
            const int dc_reset = 1 << (7 + dc_prec);
            dc_pred[0] = dc_pred[1] = dc_pred[2] = dc_reset;
            std::memset(pmv, 0, sizeof pmv);
            int mbaddr = mby * mbw - 1;
            bool first_mb = true;
            for (;;) {
                // end of slice: 23 zero bits (the next start code)
                if (b.peek(23) == 0 || b.past_end()) break;
                int inc = 0;
                for (;;) {
                    const auto& e = tables().mbai.at(b.peek16());
                    if (!e.len) { if (b.peek(11) == 0x00F) { b.skip(11); continue; } return fail("mpeg2: bad macroblock address increment"); }
                    b.skip(e.len);
                    if (e.value == 0x23) { inc += 33; continue; }   // macroblock_escape
                    inc += e.value;
                    break;
                }
                // skipped macroblocks between the previous one and this one (not before a slice's first)
                if (!first_mb && inc > 1) {
                    for (int s = 1; s < inc; ++s) {
                        const int a = mbaddr + s, sx = a % mbw, sy = a / mbw;
                        if (pct == 2) {   // P: copied from the newest reference, vector 0; predictors reset [7.6.6]
                            if (!bwd) return fail("mpeg2: P picture without a reference");
                            predict_mb(*bwd, sx, sy, 0, 0, py, pcb, pcr, false);
                            std::memset(pmv, 0, sizeof pmv);
                        } else if (pct == 3) {   // B: the previous macroblock's prediction, its vectors
                            if (!fwd || !bwd) return fail("mpeg2: B picture without references");
                            bool have = false;
                            if (last_type & 8) { predict_mb(*fwd, sx, sy, last_mv[0][0], last_mv[0][1], py, pcb, pcr, false); have = true; }
                            if (last_type & 4) { predict_mb(*bwd, sx, sy, last_mv[1][0], last_mv[1][1], py, pcb, pcr, have); have = true; }
                            if (!have) return fail("mpeg2: skipped B macroblock after an intra one");
                        } else return fail("mpeg2: skipped macroblock in an I picture");
                        store(*f, sx, sy, py, pcb, pcr, nullptr, 0);
                        dc_pred[0] = dc_pred[1] = dc_pred[2] = dc_reset;
                    }
                }
                mbaddr += inc;
                first_mb = false;
                if (mbaddr >= mbw * mbh) return fail("mpeg2: macroblock past the picture");
                const int mbx = mbaddr % mbw, mbyy = mbaddr / mbw;
                const IpuVlcLookup& mt = pct == 1 ? tables().mbt_i : pct == 2 ? tables().mbt_p : tables().mbt_b;
                const auto& e = mt.at(b.peek16());
                if (!e.len) return fail("mpeg2: bad macroblock type");
                b.skip(e.len);
                const int type = e.value;
                const bool intra = type & 1, pattern = type & 2, bwdmv = type & 4, fwdmv = type & 8, quant = type & 0x10;
                if (quant) { const int q = int(b.get(5)); qs = qst ? kNonLinear[q] : q * 2; }
                int mv[2][2] = {{0, 0}, {0, 0}};
                for (int s = 0; s < 2; ++s) {   // 0 forward, 1 backward
                    if (!(s == 0 ? fwdmv : bwdmv)) continue;
                    for (int t = 0; t < 2; ++t) {
                        const int fc = f_code[s][t];
                        const int d = motion_delta(b, fc);
                        if (failed()) return false;
                        const int v = wrap(pmv[s][t] + d, fc);
                        pmv[s][t] = v;
                        mv[s][t] = v;
                    }
                }
                if (intra) {
                    std::memset(pmv, 0, sizeof pmv);   // [7.6.3.4]
                    for (int k = 0; k < 6; ++k) if (!block(b, k, true, qs, dc_pred, blk[k])) return false;
                    store(*f, mbx, mbyy, nullptr, nullptr, nullptr, blk, 0x3F);
                    last_type = type;
                    continue;
                }
                dc_pred[0] = dc_pred[1] = dc_pred[2] = dc_reset;
                if (pct == 2 && !fwdmv) std::memset(pmv, 0, sizeof pmv);   // P without motion: vector 0, reset [7.6.3.4]
                const int cbp = pattern ? [&] {
                    const auto& c = tables().cbp.at(b.peek16());
                    if (!c.len) { fail("mpeg2: bad coded block pattern"); return 0; }
                    b.skip(c.len);
                    return int(c.value);
                }() : 0;
                if (failed()) return false;
                // prediction
                if (pct == 2) {   // P: from the newest I / P (bwd); B pictures use both (fwd older, bwd newer)
                    if (!bwd) return fail("mpeg2: P picture without a reference");
                    predict_mb(*bwd, mbx, mbyy, mv[0][0], mv[0][1], py, pcb, pcr, false);
                } else {
                    if (!fwd || !bwd) return fail("mpeg2: B picture without references");
                    bool have = false;
                    if (fwdmv) { predict_mb(*fwd, mbx, mbyy, mv[0][0], mv[0][1], py, pcb, pcr, false); have = true; }
                    if (bwdmv) { predict_mb(*bwd, mbx, mbyy, mv[1][0], mv[1][1], py, pcb, pcr, have); have = true; }
                    if (!have) return fail("mpeg2: B macroblock without a direction");
                }
                for (int k = 0; k < 6; ++k)
                    if (cbp & (32 >> k)) { if (!block(b, k, false, qs, dc_pred, blk[k])) return false; }
                store(*f, mbx, mbyy, py, pcb, pcr, blk, cbp);
                last_type = type;
                std::memcpy(last_mv, mv, sizeof mv);
            }
        }
        return true;
    }
    bool failed() const { return !err->empty(); }

    // a macroblock into the frame: prediction (or 0 for intra) plus the coded blocks' residuals, clamped
    static void store(Frame& f, int mbx, int mby, const std::uint8_t* py, const std::uint8_t* pcb, const std::uint8_t* pcr,
                      std::int16_t (*blk)[64], int cbp) {
        for (int k = 0; k < 4; ++k) {
            const int bx = mbx * 16 + (k & 1) * 8, by = mby * 16 + (k >> 1) * 8;
            const bool coded = blk && (cbp & (32 >> k));
            for (int j = 0; j < 8; ++j)
                for (int i = 0; i < 8; ++i) {
                    int v = py ? py[((k >> 1) * 8 + j) * 16 + (k & 1) * 8 + i] : 0;
                    if (coded) v += blk[k][j * 8 + i];
                    f.y[std::size_t(by + j) * std::size_t(f.w) + std::size_t(bx + i)] = std::uint8_t(std::clamp(v, 0, 255));
                }
        }
        for (int k = 4; k < 6; ++k) {
            std::vector<std::uint8_t>& plane = k == 4 ? f.cb : f.cr;
            const std::uint8_t* pp = k == 4 ? pcb : pcr;
            const bool coded = blk && (cbp & (32 >> k));
            const int cw = f.w / 2;
            for (int j = 0; j < 8; ++j)
                for (int i = 0; i < 8; ++i) {
                    int v = pp ? pp[j * 8 + i] : 0;
                    if (coded) v += blk[k][j * 8 + i];
                    plane[std::size_t(mby * 8 + j) * std::size_t(cw) + std::size_t(mbx * 8 + i)] = std::uint8_t(std::clamp(v, 0, 255));
                }
        }
    }

    void emit(const Frame& f) {
        Mpeg2Picture p;
        p.width = f.w; p.height = f.h; p.display_w = hsize; p.display_h = vsize;
        p.type = f.type; p.temporal_reference = f.tref; p.decode_index = f.index;
        p.y = f.y; p.cb = f.cb; p.cr = f.cr;
        out.push_back(std::move(p));
    }

    // decode every complete picture in buf (a picture is complete once the next picture / sequence / GOP start code or
    // the stream's end follows it)
    void pump() {
        if (failed()) return;
        if (ended) { buf.clear(); return; }
        std::size_t i = 0;
        auto next_sc = [&](std::size_t from) {
            for (std::size_t k = from; k + 3 < buf.size(); ++k)
                if (buf[k] == 0 && buf[k + 1] == 0 && buf[k + 2] == 1) return k;
            return std::size_t(-1);
        };
        for (;;) {
            const std::size_t s = next_sc(i);
            if (s == std::size_t(-1)) break;
            const int code = buf[s + 3];
            if (code == 0x00) {   // picture: needs everything up to the next non-slice start code
                std::size_t e = s + 4;
                for (;;) {
                    e = next_sc(e);
                    if (e == std::size_t(-1)) break;
                    const int c = buf[e + 3];
                    if (c == 0x00 || c == 0xB3 || c == 0xB8 || c == 0xB7) break;
                    e += 4;
                }
                if (e == std::size_t(-1) && !eos) break;   // wait for more data
                const std::size_t end = e == std::size_t(-1) ? buf.size() : e;
                Bits b{buf.data() + s + 4, end - s - 4, 0};
                tref = int(b.get(10)); pct = int(b.get(3));
                b.skip(16);   // vbv delay
                if (pct == 2 || pct == 3) { b.skip(4); if (pct == 3) b.skip(4); }   // MPEG-1 full_pel / f_code (unused in MPEG-2)
                while (b.get(1)) b.skip(8);   // extra information
                // the picture's extensions before the first slice
                for (;;) {
                    b.align();
                    std::size_t byte = b.pos >> 3;
                    while (byte + 3 < b.n && !(b.p[byte] == 0 && b.p[byte + 1] == 0 && b.p[byte + 2] == 1)) ++byte;
                    if (byte + 3 >= b.n) break;
                    const int c = b.p[byte + 3];
                    if (c == 0xB5) { b.pos = (byte + 4) * 8; if (!extension(b)) return; continue; }
                    if (c == 0xB2) { b.pos = (byte + 4) * 8; continue; }   // user data
                    b.pos = byte * 8;
                    break;
                }
                if (!have_seq) { fail("mpeg2: picture before a sequence header"); return; }
                if (!mpeg2) { fail("mpeg2: MPEG-1 stream (no sequence extension)"); return; }
                if (pct < 1 || pct > 3) { fail("mpeg2: picture coding type " + std::to_string(pct)); return; }
                std::shared_ptr<Frame> f;
                if (pct != 3 || (fwd && bwd)) {
                    if (!picture(b, f)) return;
                    ++*decoded;
                    if (pct == 3) emit(*f);   // B: out at once
                    else {                    // I / P: the previous reference goes out; this one is held
                        if (held) emit(*held);
                        held = f;
                        fwd = bwd; bwd = f;
                        if (!fwd) fwd = f;
                    }
                }   // else: a B picture before two references exist (an open GOP's leading B): dropped, as players do
                i = end;
                continue;
            }
            if (code == 0xB7) {   // sequence end: no payload, so it needs no following start code
                if (held) { emit(*held); held.reset(); }
                ended = true;
                buf.clear();
                return;
            }
            if (code == 0xB3 || code == 0xB5 || code == 0xB8 || code == 0xB2) {
                // sequence header / extension / GOP / end / user data: parse the ones that matter
                std::size_t e = next_sc(s + 4);
                if (e == std::size_t(-1) && !eos) break;
                const std::size_t end = e == std::size_t(-1) ? buf.size() : e;
                Bits b{buf.data() + s + 4, end - s - 4, 0};
                if (code == 0xB3) { if (!sequence_header(b)) return; }
                else if (code == 0xB5) { if (!extension(b)) return; }
                i = end;
                continue;
            }
            i = s + 4;   // anything else (stray slices): skip
        }
        if (i) buf.erase(buf.begin(), buf.begin() + std::ptrdiff_t(std::min(i, buf.size())));
    }
};

Mpeg2Decoder::Mpeg2Decoder() : m_(std::make_unique<Impl>()) { m_->err = &error_; m_->decoded = &decoded_; }
Mpeg2Decoder::~Mpeg2Decoder() = default;

void Mpeg2Decoder::feed(const std::uint8_t* data, std::size_t n) {
    m_->buf.insert(m_->buf.end(), data, data + n);
    m_->pump();
}
void Mpeg2Decoder::finish() {
    m_->eos = true;
    m_->pump();
    if (m_->held) { m_->emit(*m_->held); m_->held.reset(); }
}
bool Mpeg2Decoder::next_picture(Mpeg2Picture& out) {
    if (m_->out.empty()) return false;
    out = std::move(m_->out.front());
    m_->out.pop_front();
    return true;
}
bool Mpeg2Decoder::ended() const { return m_->ended; }
void Mpeg2Decoder::reset() {
    auto err = m_->err; auto dec = m_->decoded;
    m_ = std::make_unique<Impl>();
    m_->err = err; m_->decoded = dec;
    error_.clear();
}

void Mpeg2Decoder::to_rgba(const Mpeg2Picture& p, std::vector<std::uint8_t>& rgba, int th0, int th1) {
    rgba.assign(std::size_t(p.display_w) * std::size_t(p.display_h) * 4, 0);
    const int cw = p.width / 2;
    for (int y = 0; y < p.display_h; ++y)
        for (int x = 0; x < p.display_w; ++x) {
            const std::size_t c = std::size_t(y / 2) * std::size_t(cw) + std::size_t(x / 2);
            mpeg_dsp::csc_pixel(p.y[std::size_t(y) * std::size_t(p.width) + std::size_t(x)], mpeg_dsp::csc_chroma(p.cb[c], p.cr[c]), th0, th1,
                                &rgba[(std::size_t(y) * std::size_t(p.display_w) + std::size_t(x)) * 4]);
        }
}

}  // namespace b3native
