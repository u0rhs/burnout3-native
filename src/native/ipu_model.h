// ipu_model.h -- the PS2 IPU (MPEG decoder) at the command level, for the retail oracle only (movie1, 2026-11-06;
// dev-only since the device-state work, 2026-11-10: the shipped game's movies are native, platform_movie.cpp, and its
// hardware layer has no IPU -- an IPU access there is fatal). Owned by HwModelDev (hw_model_dev.h: mpeg_play,
// test_ipu_model, the oracle's mini), which hands it the IPU registers, its two FIFOs and the fromIPU (ch 3) / toIPU
// (ch 4) DMA channels; it is a value type, so the oracle's copies of the hardware model copy it. The IDCT and the CSC
// are mpeg_dsp.h's, shared with the native decoder.
//
// Evidence: EE User's Manual 6.0 ch. 8 (research/ext/PS2Docs/EE_Users_Manual.pdf: registers 8.3, commands 8.4,
// symbols 8.5, CSC 8.6) [C], ch. 5 for the DMA source-chain tags [C], 13818-2 Annex B for the VLC tables
// (ipu_vlc.h) [C].
//
// What is exact (what game-visible state depends on): the bitstream position after every command (IPU_BP.BP, the
// qwords in the IPU: IFC + FP), the VDEC / FDEC results, IPU_TOP, the busy / error flags, CBP, and the DMA progress
// of both channels (MADR / QWC / TADR / CHCR, completion). Commands run at once when their data is there; a command
// whose input is not all there stays busy (IPU_CTRL.BUSY) and continues when DMA brings more, as on hardware.
// Pixel output (mac4, 2026-11-08; before: zeros, so every movie showed solid green = Y / Cb / Cr 0): BDEC writes RAW16
// macroblocks -- inverse scan (zigzag / alternate, IPU_CTRL.AS), inverse quantisation (13818-2 7.4.2: the matrices of
// SETIQ indexed by scan position as the bitstream sends them [L PCSX2's IPU], QSC linear / non-linear by IPU_CTRL.QST;
// 11172-2's oddification for MPEG-1), saturation, mismatch control (7.4.4), an integer IDCT (Chen-Wang, IEEE 1180
// accuracy, test_ipu_model), the output saturated to [-256, 255], frame or field order by BDEC's DT [C manual 8.4,
// 8.2.5]. The pixel values feed only the picture, never control flow (frames are outside the RAM hashes) [G]; the
// IPU's own IDCT rounding is not documented, so the values may differ from the console's by IDCT rounding. CSC converts
// exactly (manual 8.6.1). IDEC and PACK are not used by retail's libmpeg [C: no 0x1/0x8 command in libmpeg.s] and
// are fatal (Unmodelled).
//
// Modelling choices [L / G, each named where it is made]: the internal buffer (IPU_BP.FP, at most 2 qwords) is
// filled from IPU_in_FIFO on demand, when a command reads bits beyond it [L: PCSX2 does the same; the manual only
// bounds FP]; a command that needs more input than is there waits without consuming anything [G: hardware decodes
// as data arrives; only the state at completion is visible to libmpeg, which polls BUSY].
#ifndef B3NATIVE_IPU_MODEL_H
#define B3NATIVE_IPU_MODEL_H

#include "hw_model.h"   // HwUnmodelled
#include "ipu_vlc.h"
#include "mpeg_dsp.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

namespace b3native {

// What the IPU needs from its owner (HwModelDev): the two DMA channels' register blocks (CHCR +0, MADR +0x10, QWC +0x20,
// TADR +0x30, ASR0 +0x40, ASR1 +0x50: index offset >> 4), memory for DMA (a DMA address: bit 31 = scratchpad), the
// end of a transfer (the DMAC's D_STAT bit and interrupt) and DMA hold (D_ENABLEW bit 16).
struct IpuHost {
    virtual std::uint32_t* ipu_channel(unsigned c) = 0;
    virtual bool ipu_read_mem(std::uint32_t dma_addr, void* out, unsigned n) = 0;
    virtual bool ipu_write_mem(std::uint32_t dma_addr, const void* in, unsigned n) = 0;
    virtual void ipu_dma_finished(unsigned c) = 0;
    virtual bool ipu_dma_held() = 0;
protected:
    ~IpuHost() = default;
};

class IpuModel {
public:
    using Unmodelled = HwUnmodelled;
    using Q = std::array<std::uint8_t, 16>;   // one quadword in memory order (the bitstream's byte order)

    // A trace record per command (for the trace comparison and tests): the command word, then BP / IFC / FP and the
    // result after it completed.
    struct Trace { std::uint32_t cmd; std::uint32_t bp; std::uint64_t result; std::uint32_t ctrl; };
    std::vector<Trace>* trace = nullptr;
    // rt1: FNV-1a over every quadword the IPU emits (IPU_out_FIFO, in order) and over each command's completion record
    // (the Trace fields): the identity check of a speed change (tools/native/mpeg_play prints both per movie)
    bool hashing = false;   // the hashes cost a pass over every output byte: on only when asked (mpeg_play)
    std::uint64_t out_hash() const { return out_hash_; }
    std::uint64_t cmd_hash() const { return cmd_hash_; }

    static constexpr std::uint32_t kCmd = 0x10002000u, kCtrl = 0x10002010u, kBp = 0x10002020u, kTop = 0x10002030u;
    static constexpr std::uint32_t kOutFifo = 0x10007000u, kInFifo = 0x10007010u;

    static bool owns(std::uint32_t a) {
        return (a >= 0x10002000u && a < 0x10002040u) || a == kOutFifo || a == kInFifo;
    }

    IpuModel() { reset_all(); }

    void reset_all() {
        reset();
        ctrl_rw_ = 0;
        iq_intra_.fill(0);
        iq_nonintra_.fill(0);
        vq_.fill(0);
        th0_ = th1_ = 0;
        dc_pred_[0] = dc_pred_[1] = dc_pred_[2] = 0;
        commands_ = 0;
    }

    // IPU_CTRL.RST [C manual 8.1.2 Reset, 8.3 IPU_CTRL]: the command ends, BUSY / IFC / OFC clear, data in the IPU is
    // abandoned.
    void reset() {
        q_.clear();
        fp_ = 0;
        bp_ = 0;
        out_.clear();
        busy_ = false;
        pending_ = false;
        cmd_ = 0;
        result_ = 0;
        ecd_ = scd_ = false;
        cbp_ = 0;
        bd_ = Bdec{};
        for (auto& b : coef_) b.fill(0);
        csc_raw_.clear();
        csc_done_ = 0;
    }

    // ---- registers ----
    void write(IpuHost& h, std::uint32_t a, unsigned width, std::uint64_t lo, std::uint64_t hi) {
        if (a == kCmd && (width == 4 || width == 8)) {
            command(h, static_cast<std::uint32_t>(lo));
            return;
        }
        if (a == kCtrl && width == 4) {
            const std::uint32_t v = static_cast<std::uint32_t>(lo);
            // r/w fields: IDP 17:16, AS 20, IVF 21, QST 22, MP1 23, PCT 26:24 [C manual 8.3]
            ctrl_rw_ = v & 0x07F30000u;
            if (v & 0x40000000u) reset();
            service(h);
            return;
        }
        if (a == kInFifo && width == 16) {
            if (ifc() >= 8) fail("IPU_in_FIFO written while full", a, width);
            Q q;
            std::memcpy(q.data(), &lo, 8);
            std::memcpy(q.data() + 8, &hi, 8);
            q_.push_back(q);
            service(h);
            return;
        }
        fail("store to", a, width);
    }

    std::uint64_t read(IpuHost& h, std::uint32_t a, unsigned width) {
        service(h);
        if (log_every_ && ++reads_ % log_every_ == 0) log_state(h, a);
        if (a == kCmd && (width == 8 || width == 4)) {
            // DATA = the last VDEC / FDEC result; bit 63 while VDEC / FDEC runs [C manual 8.3]
            const bool b = busy_ && ((cmd_ >> 28) == 3u || (cmd_ >> 28) == 4u);
            return (b ? (1ull << 63) : 0) | result_;
        }
        if (a == kCmd + 4 && width == 4) {
            const bool b = busy_ && ((cmd_ >> 28) == 3u || (cmd_ >> 28) == 4u);
            return b ? 0x80000000u : 0;
        }
        if (a == kCtrl && width == 4) return ctrl();
        if (a == kBp && width == 4) return bp_ | (ifc() << 8) | (static_cast<std::uint32_t>(fp_) << 16);
        if ((a == kTop && (width == 8 || width == 4)) || (a == kTop + 4 && width == 4)) {
            // BSTOP: the next 32 bits; bit 63 while a command runs or fewer than 32 bits are in the IPU [C manual 8.3;
            // the "not enough data" half: ps2tek, and libmpeg's _waitBdecOut relies on it (0x1062F0)]
            std::uint64_t v;
            if (busy_ || avail() < 32) {
                const std::uint32_t n = busy_ ? 0 : avail();
                fill_to(n);
                v = (1ull << 63) | (n ? static_cast<std::uint64_t>(peek(n) << (32 - n)) : 0);   // [G] partial bits
            } else {
                fill_to(32);
                v = peek(32);
            }
            return a == kTop + 4 ? (v >> 32) : (width == 4 ? (v & 0xFFFFFFFFu) : v);
        }
        if (a == kOutFifo && width == 16) fail("IPU_out_FIFO read by the CPU (not used by libmpeg)", a, width);
        fail("load from", a, width);
    }
    std::uint64_t read_hi(std::uint32_t) const { return 0; }

    // A DMA channel of the IPU's was started (CHCR.STR written 1) or released from hold: move data.
    void dma_kick(IpuHost& h) { service(h); }

    std::uint32_t ctrl() const {
        return ifc() | (std::min<std::uint32_t>(static_cast<std::uint32_t>(out_.size()), 8u) << 4) |
               (static_cast<std::uint32_t>(cbp_) << 8) | (ecd_ ? 0x4000u : 0) | (scd_ ? 0x8000u : 0) | ctrl_rw_ |
               (busy_ ? 0x80000000u : 0);
    }
    std::uint32_t bp() const { return bp_; }
    std::uint32_t ifc() const { return static_cast<std::uint32_t>(q_.size()) - fp_; }
    std::uint32_t fp() const { return fp_; }
    bool busy() const { return busy_; }
    std::uint64_t commands() const { return commands_; }
    std::uint32_t last_command() const { return cmd_; }
    const std::array<std::uint8_t, 64>& iq_intra() const { return iq_intra_; }
    const std::array<std::uint8_t, 64>& iq_nonintra() const { return iq_nonintra_; }

    // movie2: B3NATIVE_IPU_LOG=N prints the IPU's state every N register reads (a stuck poll shows what it waits for).
    void log_state(IpuHost& h, std::uint32_t a) const {
        const std::uint32_t* t = h.ipu_channel(4);
        const std::uint32_t* f = h.ipu_channel(3);
        std::fprintf(stderr,
                     "ipu: %llu reads (now %08X), %llu commands, cmd %08X busy %d pending %d, CTRL %08X BP %u IFC %u FP %u "
                     "out %zu; toIPU CHCR %08X MADR %08X QWC %u TADR %08X; fromIPU CHCR %08X MADR %08X QWC %u; held %d\n",
                     static_cast<unsigned long long>(reads_), a, static_cast<unsigned long long>(commands_), cmd_, busy_,
                     pending_, ctrl(), bp_, ifc(), fp_, out_.size(), t[0], t[1], t[2], t[3], f[0], f[1], f[2],
                     h.ipu_dma_held());
        std::uint8_t b[32] = {};
        if (h.ipu_read_mem(t[1] & ~15u, b, 32)) {
            std::fprintf(stderr, "ipu:   toIPU data at %08X:", t[1] & ~15u);
            for (unsigned i = 0; i < 32; ++i) std::fprintf(stderr, " %02X", b[i]);
            std::fprintf(stderr, "\n");
        }
    }

private:
    std::uint64_t reads_ = 0;
    std::uint64_t log_every_ = [] {
        const char* e = std::getenv("B3NATIVE_IPU_LOG");
        return e ? std::strtoull(e, nullptr, 10) : 0ull;
    }();
    [[noreturn]] static void fail(const char* what, std::uint32_t a, unsigned width) {
        char b[160];
        std::snprintf(b, sizeof b, "IPU model: %s 0x%08X (%u bytes) not modelled", what, a, width);
        throw Unmodelled(b);
    }
    [[noreturn]] static void fail_cmd(const char* what, std::uint32_t cmd) {
        char b[160];
        std::snprintf(b, sizeof b, "IPU model: %s (command 0x%08X) not modelled", what, cmd);
        throw Unmodelled(b);
    }

    // ---- the bitstream in the IPU: q_ = the internal buffer (front fp_ qwords) + IPU_in_FIFO; bp_ = the bit
    // position in q_[0] ----
    std::uint32_t avail() const { return q_.empty() ? 0 : static_cast<std::uint32_t>(q_.size()) * 128u - bp_; }
    std::uint8_t byte_at(std::uint32_t bit) const { return q_[bit >> 7][(bit >> 3) & 15]; }
    // The n (<= 32) bits at the current position plus off, MSB first; the caller checked avail.
    // rt1 (speed): the five bytes from the (at most) two quadwords they span, each looked up once in the deque instead
    // of once per byte; bytes past the buffer read 0 as before
    std::uint32_t peek(unsigned n, std::uint32_t off = 0) const {
        if (n == 0) return 0;
        const std::uint32_t first = bp_ + off;
        const std::size_t qi = first >> 7;
        const unsigned bo = (first >> 3) & 15u;
        const std::uint8_t* a = qi < q_.size() ? q_[qi].data() : nullptr;
        const std::uint8_t* b = bo > 11 && qi + 1 < q_.size() ? q_[qi + 1].data() : nullptr;
        std::uint64_t w = 0;
        for (unsigned i = 0; i < 5; ++i) {
            const unsigned k = bo + i;
            w = (w << 8) | (k < 16 ? (a ? a[k] : 0u) : (b ? b[k - 16] : 0u));
        }
        return static_cast<std::uint32_t>((w >> (40 - (first & 7) - n)) & ((n == 32) ? 0xFFFFFFFFull : ((1ull << n) - 1)));
    }
    // The internal buffer takes qwords from IPU_in_FIFO until it covers the next n bits (at most 2 qwords) [L].
    void fill_to(std::uint32_t n) {
        const std::uint32_t need = (bp_ + n + 127) / 128;
        while (fp_ < need && fp_ < 2 && fp_ < q_.size()) ++fp_;
    }
    void consume(std::uint32_t n) {
        fill_to(n);
        consumed_ += n;
        bp_ += n;
        while (bp_ >= 128 && !q_.empty()) {
            q_.pop_front();
            if (fp_) --fp_;
            bp_ -= 128;
            fill_to(0);
        }
    }

    // ---- commands ----
    void command(IpuHost& h, std::uint32_t v) {
        if (busy_) fail_cmd("a command written while the IPU is busy", v);
        ++commands_;
        cmd_ = v;
        ecd_ = scd_ = false;   // [C manual 8.3: cleared when a command is issued]
        busy_ = true;
        pending_ = true;
        service(h);
    }

    // Runs the pending command when its data is there; true when it completed.
    bool try_command() {
        const std::uint32_t c = cmd_ >> 28, fb = cmd_ & 0x3Fu;
        switch (c) {
        case 0x0:   // BCLR: the input FIFO and the internal buffer cleared; BP for the first new qword [C manual 8.4]
            q_.clear();
            fp_ = 0;
            bp_ = cmd_ & 0x7Fu;
            return true;
        case 0x4: {   // FDEC: skip FB, the next 32 bits as the result, not consumed [C manual 8.4]
            if (avail() < fb + 32) return false;
            consume(fb);
            fill_to(32);
            result_ = peek(32);
            check_start_code();
            return true;
        }
        case 0x3:   // VDEC
            return vdec(fb, (cmd_ >> 26) & 3u);
        case 0x5: {   // SETIQ: skip FB, 64 bytes into the intra (IQM 0) or non-intra (IQM 1) matrix [C manual 8.4]
            if (avail() < fb + 512) return false;
            consume(fb);
            auto& m = (cmd_ & 0x08000000u) ? iq_nonintra_ : iq_intra_;
            for (unsigned i = 0; i < 64; ++i) {
                fill_to(8);
                m[i] = static_cast<std::uint8_t>(peek(8));
                consume(8);
            }
            return true;
        }
        case 0x6: {   // SETVQ: 32 bytes (16 RGB16 entries) [C manual 8.4]
            if (avail() < 256) return false;
            for (unsigned i = 0; i < 32; ++i) {
                fill_to(8);
                vq_[i] = static_cast<std::uint8_t>(peek(8));
                consume(8);
            }
            return true;
        }
        case 0x9:   // SETTH: TH0 8:0, TH1 24:16 [C manual 8.4]
            th0_ = cmd_ & 0x1FFu;
            th1_ = (cmd_ >> 16) & 0x1FFu;
            return true;
        case 0x2:
            return bdec();
        case 0x7:
            return csc();
        default:
            fail_cmd(c == 1 ? "IDEC" : c == 8 ? "PACK" : "an undefined command", cmd_);
        }
    }

    // SCD [C manual 8.3: set when a start code is met]: the next 24 bits are 0x000001 at a byte boundary [L: the
    // byte-aligned form; libmpeg never reads SCD (no test of 0x8000 in libmpeg.s), so this is not game-visible].
    void check_start_code() {
        if ((bp_ & 7) == 0 && avail() >= 24 && peek(24) == 1) scd_ = true;
    }

    bool vdec(std::uint32_t fb, std::uint32_t tbl) {
        // the table is searched over 32 bits [C manual 8.4 VDEC note: 32 bits must be there after BP + FB]
        if (avail() < fb + 32) return false;
        consume(fb);
        fill_to(32);
        const std::uint32_t next16 = peek(16);
        int len = 0, value = 0;
        if (tbl == 0) {
            const auto& e = tables().mbai.at(next16);
            if (e.len) { len = e.len; value = e.value; }
            if (len == 0 && (next16 >> 5) == 0x00Fu && (ctrl_rw_ & 0x00800000u)) { len = 11; value = 0x22; }
        } else if (tbl == 1) {
            const std::uint32_t pct = (ctrl_rw_ >> 24) & 7u;
            const IpuVlcLookup* t = pct == 1 ? &tables().mbt_i : pct == 2 ? &tables().mbt_p : pct == 3 ? &tables().mbt_b
                                  : pct == 4 ? &tables().mbt_d : nullptr;
            if (t) {
                const auto& e = t->at(next16);
                if (e.len) { len = e.len; value = e.value; }
            }
        } else if (tbl == 2) {
            if (next16 & 0x8000u) { len = 1; value = 0; }
            else {
                const auto& e = tables().motion.at(next16);
                if (e.len && e.len < 16) {
                    len = e.len + 1;
                    value = ((next16 >> (15 - e.len)) & 1u) ? -e.value : e.value;
                }
            }
        } else {
            const auto& e = tables().dmv.at(next16);
            if (e.len) { len = e.len; value = e.value; }
        }
        if (len == 0) {   // an invalid code: DATA 0 [C manual 8.5.3 "ERROR"], ECD [L], nothing consumed [G]
            result_ = 0;
            ecd_ = true;
            return true;
        }
        result_ = (static_cast<std::uint32_t>(len) << 16) | (static_cast<std::uint32_t>(value) & 0xFFFFu);
        consume(static_cast<std::uint32_t>(len));
        check_start_code();
        return true;
    }

    // ---- BDEC [C manual 8.4, 8.5.2; 13818-2 7.2 / Annex B] ----
    // Resumable: one symbol at a time, each consumed only when all its bits are in the IPU, so a macroblock larger
    // than the IPU's 10 qwords (FIFO + internal buffer) decodes as DMA brings it [L: the hardware decodes a stream].
    // A VLC needs only its own bits: the lookup over the next 16 bits zero-pads past the data and a code of length L
    // depends on its first L bits alone; a code not found with fewer than 16 bits there waits for more [G].
    enum : std::uint8_t { kBStart, kBCbp, kBDcSize, kBDcDiff, kBCoef, kBEnd };
    struct Bdec {
        std::uint8_t phase = kBStart, blk = 0, comp = 0, dc_size = 0, cbp = 0;
        std::int8_t idx = 0;
        bool first = false, error = false;
        int sum = 0;   // mac4: the block's coefficient sum (mismatch control)
    } bd_;
    // mac4: the macroblock's six blocks, natural order, dequantised (then IDCT'd in place at the macroblock's end)
    std::array<std::array<std::int16_t, 64>, 6> coef_{};

    // ---- mac4: inverse scan, inverse quantisation, IDCT ----
    static constexpr std::uint8_t kZigzag[64] = {   // scan position -> natural index [C 13818-2 Figure 7-2]
        0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
    static constexpr std::uint8_t kAlternate[64] = {   // [C 13818-2 Figure 7-3]
        0, 8, 16, 24, 1, 9, 2, 10, 17, 25, 32, 40, 48, 56, 57, 49, 41, 33, 26, 18, 3, 11, 4, 12, 19, 27, 34, 42, 50, 58, 35, 43,
        51, 59, 20, 28, 5, 13, 6, 14, 21, 29, 36, 44, 52, 60, 37, 45, 53, 61, 22, 30, 7, 15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63};
    static int quantiser_scale(std::uint32_t qsc, bool nonlinear, bool mpeg1) {   // [C 13818-2 Table 7-6]
        static constexpr std::uint8_t kNonLinear[32] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36,
                                                        40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112};
        if (mpeg1) return static_cast<int>(qsc);
        return nonlinear ? kNonLinear[qsc & 31u] : static_cast<int>(qsc & 31u) * 2;
    }
    // One AC (or non-intra) coefficient at scan position `pos`: QF = level (signed), 13818-2 7.4.2.3 / 11172-2 2.4.4
    void put_coef(int pos, int level, bool intra) {
        const bool mpeg1 = ctrl_rw_ & 0x00800000u;
        const int qs = quantiser_scale((cmd_ >> 16) & 31u, ctrl_rw_ & 0x00400000u, mpeg1);
        const int w = (intra ? iq_intra_ : iq_nonintra_)[static_cast<unsigned>(pos)];
        const int sgn = level < 0 ? -1 : 1;
        int f = intra ? (2 * level * w * qs) / (mpeg1 ? 16 : 32)
                      : ((2 * level + sgn) * w * qs) / (mpeg1 ? 16 : 32);
        if (mpeg1 && f != 0 && (f & 1) == 0) f -= sgn;   // oddification [C 11172-2 2.4.4.1 / 2.4.4.2]
        f = std::clamp(f, -2048, 2047);
        bd_.sum += f;
        const std::uint8_t* scan = (ctrl_rw_ & 0x00100000u) ? kAlternate : kZigzag;
        coef_[bd_.blk][scan[pos]] = static_cast<std::int16_t>(f);
    }
    void end_block() {   // mismatch control [C 13818-2 7.4.4] (MPEG-2 only)
        if (!(ctrl_rw_ & 0x00800000u) && (bd_.sum & 1) == 0) coef_[bd_.blk][63] = static_cast<std::int16_t>(coef_[bd_.blk][63] ^ 1);
    }
    // the integer IDCT (mpeg_dsp.h, the native decoder's too)
    static void idct(std::int16_t* blk) { mpeg_dsp::idct(blk); }

    // The next 16 bits, zero-padded past the data, and how many are real.
    std::uint32_t look16(std::uint32_t& have) const {
        have = avail();
        if (have >= 16) return peek(16);
        return have ? peek(have) << (16 - have) : 0;
    }
    // A VLC lookup: 1 = found (e set), 0 = wait, -1 = an error code.
    int vlc(const IpuVlcLookup& t, const IpuVlcLookup::Entry*& e) {
        std::uint32_t have;
        const std::uint32_t n16 = look16(have);
        e = &t.at(n16);
        if (e->len && e->len <= have) return 1;
        if (have >= 16) return -1;
        return 0;
    }
    int next_block(bool mbi) {   // the next coded block's start phase; kBEnd after the last
        while (bd_.blk < 6 && !(bd_.cbp & (0x20 >> bd_.blk))) ++bd_.blk;
        if (bd_.blk >= 6) return kBEnd;
        bd_.comp = bd_.blk < 4 ? 0 : static_cast<std::uint8_t>(bd_.blk - 3);
        bd_.idx = 0;
        bd_.sum = 0;
        bd_.first = !mbi;
        return mbi ? kBDcSize : kBCoef;
    }
    // RAW16: Y 16 x 16, then Cb 8 x 8, Cr 8 x 8, 16-bit little-endian = 48 qwords [C manual 8.2.5]. The luma blocks
    // 0-3 are the quarters (frame DCT) or the even / odd lines of each half (field DCT: BDEC's DT, bit 25) [C 13818-2
    // 6.1.3 / Figure 6-13]. An error's macroblock is zeros.
    void out_macroblock(bool error = false) {
        if (error) {
            for (int i = 0; i < 48; ++i) emit(Q{});
            for (auto& b : coef_) b.fill(0);
            return;
        }
        std::int16_t mb[384];
        const bool field = (cmd_ >> 25) & 1u;
        for (int k = 0; k < 6; ++k) {
            const bool coded = bd_.cbp & (0x20 >> k);
            if (coded) idct(coef_[k].data());
            const std::int16_t* b = coef_[k].data();
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    const std::int16_t v = coded ? b[8 * y + x] : 0;
                    if (k < 4) {
                        const int row = field ? 2 * y + (k >> 1) : 8 * (k >> 1) + y;
                        mb[16 * row + 8 * (k & 1) + x] = v;
                    } else {
                        mb[256 + 64 * (k - 4) + 8 * y + x] = v;
                    }
                }
            if (coded) coef_[k].fill(0);
        }
        for (int i = 0; i < 48; ++i) {
            Q o;
            for (int j = 0; j < 8; ++j) {
                const auto u = static_cast<std::uint16_t>(mb[8 * i + j]);
                o[2 * j] = static_cast<std::uint8_t>(u & 0xFFu);
                o[2 * j + 1] = static_cast<std::uint8_t>(u >> 8);
            }
            emit(o);
        }
    }

    bool bdec() {
        const std::uint32_t fb = cmd_ & 0x3Fu;
        const bool mbi = (cmd_ >> 27) & 1u, dcr = (cmd_ >> 26) & 1u;
        const bool mpeg1 = ctrl_rw_ & 0x00800000u, table_one = ctrl_rw_ & 0x00200000u;
        const IpuVlcLookup::Entry* e = nullptr;
        for (;;) {
            switch (bd_.phase) {
            case kBStart:
                if (avail() < fb) return false;
                consume(fb);
                if (dcr) {   // [C manual: DCR resets the DC prediction; 13818-2 7.2.1: 128 << intra_dc_precision]
                    const int p = 128 << ((ctrl_rw_ >> 16) & 3u);
                    dc_pred_[0] = dc_pred_[1] = dc_pred_[2] = p;
                }
                bd_.error = false;
                bd_.blk = 0;
                if (mbi) { bd_.cbp = 0x3F; bd_.phase = static_cast<std::uint8_t>(next_block(mbi)); }
                else bd_.phase = kBCbp;
                break;
            case kBCbp: {
                const int k = vlc(tables().cbp, e);
                if (k == 0) return false;
                if (k < 0 || (e->value == 0 && mpeg1)) return bdec_error();
                consume(e->len);
                bd_.cbp = static_cast<std::uint8_t>(e->value);
                bd_.phase = static_cast<std::uint8_t>(next_block(mbi));
                break;
            }
            case kBDcSize: {
                const int k = vlc(bd_.comp == 0 ? tables().dc_luma : tables().dc_chroma, e);
                if (k == 0) return false;
                if (k < 0) return bdec_error();
                consume(e->len);
                bd_.dc_size = static_cast<std::uint8_t>(e->value);
                bd_.phase = kBDcDiff;
                break;
            }
            case kBDcDiff: {
                const unsigned size = bd_.dc_size;
                if (avail() < size) return false;
                int diff = 0;
                if (size) {
                    fill_to(size);
                    diff = static_cast<int>(peek(size));
                    consume(size);
                    if (diff < (1 << (size - 1))) diff -= (1 << size) - 1;
                }
                dc_pred_[bd_.comp] += diff;
                {   // mac4: the intra DC coefficient [C 13818-2 7.4.1: intra_dc_mult = 8 >> intra_dc_precision; 11172-2: x 8]
                    const unsigned prec = (ctrl_rw_ & 0x00800000u) ? 0u : (ctrl_rw_ >> 16) & 3u;
                    const int f = dc_pred_[bd_.comp] * (8 >> prec);
                    bd_.sum = f;
                    coef_[bd_.blk][0] = static_cast<std::int16_t>(std::clamp(f, -2048, 2047));
                }
                bd_.idx = 1;
                bd_.phase = kBCoef;
                break;
            }
            case kBCoef: {
                int run, level = 0;
                if (bd_.first) {   // a non-intra block's first coefficient: "1s" = (0, +-1) (B-14 note)
                    std::uint32_t have;
                    const std::uint32_t n16 = look16(have);
                    if (have >= 2 && (n16 & 0x8000u)) {
                        consume(2);
                        bd_.first = false;
                        run = 0;
                        put_coef(bd_.idx, (n16 & 0x4000u) ? -1 : 1, false);   // mac4: the sign is the second bit
                        if (++bd_.idx > 64) return bdec_error();
                        break;
                    }
                    if (have < 2) return false;
                }
                const IpuVlcLookup& t = (mbi && table_one && !mpeg1) ? tables().dct_one : tables().dct_zero;
                const int k = vlc(t, e);
                if (k == 0) return false;
                if (k < 0) return bdec_error();
                if (e->value == -1) {   // end of block
                    if (bd_.first) return bdec_error();   // "10" cannot start a non-intra block (it reads as 1s)
                    consume(e->len);
                    end_block();
                    ++bd_.blk;
                    bd_.phase = static_cast<std::uint8_t>(next_block(mbi));
                    break;
                }
                if (e->value == -2) {   // escape: 6-bit code + 6-bit run + the level
                    if (mpeg1) {   // 11172-2: 8-bit level; 0x00 / 0x80 add 8 more bits
                        if (avail() < 20) return false;
                        fill_to(20);
                        const std::uint32_t l8 = peek(8, 12);
                        const unsigned n = (l8 == 0 || l8 == 0x80) ? 28u : 20u;
                        if (avail() < n) return false;
                        fill_to(n);
                        run = static_cast<int>(peek(6, 6));
                        // mac4: the level: 8 bits signed; 0x00 + 8 bits = 128..255, 0x80 + 8 bits = -256..-129
                        level = l8 == 0 ? static_cast<int>(peek(8, 20)) : l8 == 0x80 ? static_cast<int>(peek(8, 20)) - 256
                                        : static_cast<int>(static_cast<std::int8_t>(l8));
                        consume(n);
                    } else {
                        if (avail() < 24) return false;
                        fill_to(24);
                        run = static_cast<int>(peek(6, 6));
                        const int l = static_cast<int>(peek(12, 12));
                        if (l == 0 || l == 2048) return bdec_error();   // forbidden levels [C 13818-2 Table B-16]
                        level = l >= 2048 ? l - 4096 : l;   // mac4: 12 bits signed
                        consume(24);
                    }
                } else {
                    const std::uint32_t have = avail();   // rt1: look16's `have` (its peek was unused)
                    if (have < static_cast<std::uint32_t>(e->len) + 1) return false;   // the sign bit
                    run = e->value;
                    level = peek(1, e->len) ? -e->value2 : e->value2;   // mac4: the sign bit after the code
                    consume(e->len + 1u);
                }
                bd_.first = false;
                bd_.idx = static_cast<std::int8_t>(bd_.idx + run);
                if (bd_.idx > 63) return bdec_error();
                put_coef(bd_.idx, level, mbi);
                ++bd_.idx;
                break;
            }
            case kBEnd:
            default: {
                // 8 zero bits ahead = a start code follows (SCD; BP at the start code: the next byte boundary [L manual
                // "the beginning of the start code"]); else the next macroblock follows (BP after the EOB) [C manual
                // BDEC end conditions a) / b)]
                if (avail() < 8) return false;   // [G] the end test needs its 8 bits
                fill_to(8);
                if (peek(8) == 0) {
                    scd_ = true;
                    consume((8 - (bp_ & 7)) & 7);
                }
                cbp_ = bd_.cbp;
                out_macroblock();
                bd_.phase = kBStart;
                return true;
            }
            }
        }
    }
    // An error code [C manual BDEC c)]: ECD, BP at the error code, the command ends [L: with a full macroblock of output
    // (libmpeg resets the IPU and its DMA after an error, 0x106338-0x1063DC, so the output's size is not observed)].
    bool bdec_error() {
        ecd_ = true;
        bd_.phase = kBStart;
        out_macroblock(true);
        return true;
    }

    // ---- CSC [C manual 8.4, 8.6.1, 8.6.2]: MBC macroblocks of RAW8 (24 qwords each) to RGB32 (64) / RGB16 (32) ----
    // Resumable: the RAW8 bytes are taken as they arrive (a macroblock is larger than the IPU holds).
    std::vector<std::uint8_t> csc_raw_;
    std::uint32_t csc_done_ = 0;

    bool csc() {
        const std::uint32_t mbc = cmd_ & 0x7FFu;
        const bool rgb16 = (cmd_ >> 27) & 1u, dte = (cmd_ >> 26) & 1u;
        while (csc_done_ < mbc) {
            while (csc_raw_.size() < 384 && avail() >= 8) {
                fill_to(8);
                if ((bp_ & 7u) == 0) {
                    // rt1 (speed): the rest of the front quadword at once. Byte-aligned, fill_to(8) needs one quadword
                    // for every byte of it and consume(8) changes nothing else until the quadword ends, where it pops
                    // it as below -- so BP / FP / IFC go through the per-byte loop's states
                    const unsigned bo = bp_ >> 3;
                    const unsigned take = std::min<unsigned>(16u - bo, 384u - static_cast<unsigned>(csc_raw_.size()));
                    const std::uint8_t* src = q_.front().data() + bo;
                    csc_raw_.insert(csc_raw_.end(), src, src + take);
                    consumed_ += 8u * take;
                    bp_ += 8u * take;
                    if (bp_ >= 128) {
                        q_.pop_front();
                        if (fp_) --fp_;
                        bp_ -= 128;
                        fill_to(0);
                    }
                    continue;
                }
                csc_raw_.push_back(static_cast<std::uint8_t>(peek(8)));
                consume(8);
            }
            if (csc_raw_.size() < 384) return false;
            csc_macroblock(csc_raw_.data(), rgb16, dte);
            csc_raw_.clear();
            ++csc_done_;
        }
        csc_done_ = 0;
        return true;
    }

    void csc_macroblock(const std::uint8_t* raw, bool rgb16, bool dte) {
        std::uint8_t px[256][4];
        // the CSC (mpeg_dsp.h, the native decoder's too): the chroma terms once per chroma sample (rt1: each serves
        // four pixels)
        mpeg_dsp::CscChroma ch[64];
        for (unsigned c = 0; c < 64; ++c) ch[c] = mpeg_dsp::csc_chroma(raw[256 + c], raw[320 + c]);
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 16; ++x)
                mpeg_dsp::csc_pixel(raw[y * 16 + x], ch[(y / 2) * 8 + x / 2], static_cast<int>(th0_), static_cast<int>(th1_), px[y * 16 + x]);
        if (!rgb16) {
            for (unsigned q = 0; q < 64; ++q) {
                Q o;
                for (unsigned k = 0; k < 4; ++k) std::memcpy(o.data() + 4 * k, px[q * 4 + k], 4);
                emit(o);
            }
            return;
        }
        // the dither matrix D (s4.1) [C manual 8.6.2: rows -4 0 -3 1 / 2 -2 3 -1 / -2.5 1.5 -3.5 0.5 / 3.5 -0.5 2.5 -1.5],
        // in halves
        static const int dith[4][4] = {{-8, 0, -6, 2}, {4, -4, 6, -2}, {-5, 3, -7, 1}, {7, -1, 5, -3}};
        for (unsigned q = 0; q < 32; ++q) {
            Q o;
            for (unsigned k = 0; k < 8; ++k) {
                const unsigned p = q * 8 + k, x = p & 15, y = p >> 4;
                int c[3];
                for (unsigned ch = 0; ch < 3; ++ch) {
                    int v = px[p][ch] * 2 + (dte ? dith[y & 3][x & 3] : 0);   // s10.1
                    v >>= 1;
                    c[ch] = (v < 0 ? 0 : v > 255 ? 255 : v) >> 3;
                }
                const std::uint16_t w = static_cast<std::uint16_t>(c[0] | (c[1] << 5) | (c[2] << 10) |
                                                                   ((px[p][3] == 0x40) ? 0x8000 : 0));
                o[2 * k] = static_cast<std::uint8_t>(w);
                o[2 * k + 1] = static_cast<std::uint8_t>(w >> 8);
            }
            emit(o);
        }
    }
    // ---- DMA ----
    // toIPU (ch 4, source; normal or chain mode) fills IPU_in_FIFO while it has room [C manual 8.1.2; EE manual ch. 5
    // source-chain tags: refe 0, cnt 1, next 2, ref 3, refs 4, call 5, ret 6, end 7; QWC 15:0, PCE 27:26, ID 30:28,
    // IRQ 31, ADDR the second word; CHCR.TAG = the tag's bits 31:16]; fromIPU (ch 3, destination, normal mode) empties
    // IPU_out_FIFO to memory.
    bool pump_to_ipu(IpuHost& h) {
        std::uint32_t* r = h.ipu_channel(4);
        bool moved = false;
        while ((r[0] & 0x100u) && !h.ipu_dma_held() && ifc() < 8) {
            if (r[2] & 0xFFFFu) {
                Q q;
                if (!h.ipu_read_mem(r[1], q.data(), 16)) fail("toIPU DMA from a non-memory address", r[1], 16);
                q_.push_back(q);
                r[1] += 16;
                r[2] = (r[2] - 1) & 0xFFFFu;
                moved = true;
                continue;
            }
            const unsigned mod = (r[0] >> 2) & 3u;
            if (mod == 0) { h.ipu_dma_finished(4); break; }
            if (mod != 1) fail("toIPU DMA in interleave mode", 0x1000B400u, 4);
            // chain: the transfer ends after the data of an end-type tag (refe / end), of a ret with nothing on the
            // address stack, or of a tag with IRQ set when CHCR.TIE is on [C EE manual DMA chain]
            const unsigned last = (r[0] >> 28) & 7u;
            const bool irq_stop = (r[0] & 0x80000000u) && (r[0] & 0x80u);
            if (started_ && (last == 0 || last == 7 || irq_stop || ret_end_)) { h.ipu_dma_finished(4); break; }
            started_ = true;
            ret_end_ = false;
            std::uint32_t tag[2];
            if (!h.ipu_read_mem(r[3], tag, 8)) fail("toIPU DMA tag at a non-memory address", r[3], 8);
            const unsigned id = (tag[0] >> 28) & 7u;
            const std::uint32_t qwc = tag[0] & 0xFFFFu, addr = tag[1];
            r[0] = (r[0] & 0xFFFFu) | (tag[0] & 0xFFFF0000u);
            r[2] = qwc;
            switch (id) {
            case 0: case 3: case 4: r[1] = addr; r[3] += 16; break;              // refe / ref / refs
            case 1: r[1] = r[3] + 16; r[3] = r[1] + qwc * 16; break;              // cnt
            case 2: r[1] = r[3] + 16; r[3] = addr; break;                         // next
            case 5: {                                                             // call
                const unsigned asp = (r[0] >> 4) & 3u;
                if (asp >= 2) fail("toIPU DMA call with a full address stack", 0x1000B400u, 4);
                r[1] = r[3] + 16;
                r[4 + asp] = r[1] + qwc * 16;
                r[0] = (r[0] & ~0x30u) | ((asp + 1) << 4);
                r[3] = addr;
                break;
            }
            case 6: {                                                             // ret
                const unsigned asp = (r[0] >> 4) & 3u;
                r[1] = r[3] + 16;
                if (asp) { r[3] = r[4 + asp - 1]; r[0] = (r[0] & ~0x30u) | ((asp - 1) << 4); }
                else ret_end_ = true;
                break;
            }
            default: r[1] = r[3] + 16; break;                                     // end
            }
            moved = true;
        }
        return moved;
    }
    bool pump_from_ipu(IpuHost& h) {
        std::uint32_t* r = h.ipu_channel(3);
        bool moved = false;
        while ((r[0] & 0x100u) && !h.ipu_dma_held()) {
            if ((r[2] & 0xFFFFu) == 0) { h.ipu_dma_finished(3); break; }
            if (out_.empty()) break;
            if (!h.ipu_write_mem(r[1], out_.front().data(), 16)) fail("fromIPU DMA to a non-memory address", r[1], 16);
            out_.pop_front();
            r[1] += 16;
            r[2] = (r[2] - 1) & 0xFFFFu;
            moved = true;
            if ((r[2] & 0xFFFFu) == 0) { h.ipu_dma_finished(3); break; }
        }
        return moved;
    }

public:
    // Called after every register access of the IPU or its channels: data moves, a waiting command continues. A
    // command's end: BUSY clears once its input is decoded and its output fits the 8-qword output FIFO [L].
    void service(IpuHost& h) {
        for (int guard = 0; guard < 1000000; ++guard) {
            bool progress = pump_to_ipu(h);
            const std::uint64_t before = consumed_;
            if (pending_ && try_command()) {
                pending_ = false;
                progress = true;
            }
            progress |= consumed_ != before;   // a waiting command took data: the FIFO has room again
            progress |= pump_from_ipu(h);
            if (busy_ && !pending_ && out_.size() <= 8) {
                busy_ = false;
                if (trace) trace->push_back(Trace{cmd_, bp_ | (ifc() << 8) | (fp_ << 16), result_, ctrl()});
                if (hashing) {
                    const std::uint64_t rec[3] = {cmd_, static_cast<std::uint64_t>(bp_ | (ifc() << 8) | (fp_ << 16)) |
                                                            (static_cast<std::uint64_t>(ctrl()) << 32), result_};
                    cmd_hash_ = fnv(cmd_hash_, reinterpret_cast<const std::uint8_t*>(rec), sizeof rec);
                }
                progress = true;
            }
            if (!progress) return;
        }
        throw Unmodelled("IPU model: no progress bound exceeded");
    }
    // A new toIPU transfer (CHCR.STR 0 -> 1): chain mode starts by transferring MADR / QWC when QWC is set, then
    // follows TADR [C EE manual]; started_ = a tag has been read in this transfer.
    void dma_start(IpuHost& h, unsigned c) {
        // A fromIPU transfer ends when the IPU has produced QWC qwords (the hardware's order).
        if (c == 4) {
            std::uint32_t* r = h.ipu_channel(4);
            // a transfer resumed with QWC left continues the tag CHCR.TAG names; a fresh chain reads a tag first
            started_ = (r[2] & 0xFFFFu) != 0;
            ret_end_ = false;
        }
        service(h);
    }

private:
    struct Tables {
        IpuVlcLookup mbai, mbt_i, mbt_p, mbt_b, mbt_d, motion, dmv, cbp, dc_luma, dc_chroma, dct_zero, dct_one;
        Tables() {
            mbai = ipu_vlc_build(kIpuMbai, ipu_count(kIpuMbai));
            mbt_i = ipu_vlc_build(kIpuMbTypeI, ipu_count(kIpuMbTypeI));
            mbt_p = ipu_vlc_build(kIpuMbTypeP, ipu_count(kIpuMbTypeP));
            mbt_b = ipu_vlc_build(kIpuMbTypeB, ipu_count(kIpuMbTypeB));
            mbt_d = ipu_vlc_build(kIpuMbTypeD, ipu_count(kIpuMbTypeD));
            motion = ipu_vlc_build(kIpuMotionMagnitude, ipu_count(kIpuMotionMagnitude));
            dmv = ipu_vlc_build(kIpuDmv, ipu_count(kIpuDmv));
            cbp = ipu_vlc_build(kIpuCbp, ipu_count(kIpuCbp));
            dc_luma = ipu_vlc_build(kIpuDcSizeLuma, ipu_count(kIpuDcSizeLuma));
            dc_chroma = ipu_vlc_build(kIpuDcSizeChroma, ipu_count(kIpuDcSizeChroma));
            std::vector<IpuVlcCode> z(kIpuDctZero, kIpuDctZero + ipu_count(kIpuDctZero)), o(kIpuDctOne, kIpuDctOne + ipu_count(kIpuDctOne));
            z.insert(z.end(), kIpuDctLong, kIpuDctLong + ipu_count(kIpuDctLong));
            o.insert(o.end(), kIpuDctLong, kIpuDctLong + ipu_count(kIpuDctLong));
            dct_zero = ipu_vlc_build(z.data(), static_cast<unsigned>(z.size()));
            dct_one = ipu_vlc_build(o.data(), static_cast<unsigned>(o.size()));
        }
    };
    static const Tables& tables() {
        static const Tables t;
        return t;
    }

    std::deque<Q> q_;
    std::uint32_t fp_ = 0, bp_ = 0;
    std::deque<Q> out_;
    std::uint64_t out_hash_ = 0xcbf29ce484222325ull, cmd_hash_ = 0xcbf29ce484222325ull;
    static std::uint64_t fnv(std::uint64_t h, const std::uint8_t* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
        return h;
    }
    void emit(const Q& o) {
        if (hashing) out_hash_ = fnv(out_hash_, o.data(), o.size());
        out_.push_back(o);
    }
    bool busy_ = false, pending_ = false, ecd_ = false, scd_ = false;
    bool started_ = false, ret_end_ = false;
    std::uint32_t cmd_ = 0, ctrl_rw_ = 0;
    std::uint64_t result_ = 0;
    std::uint8_t cbp_ = 0;
    std::array<std::uint8_t, 64> iq_intra_{}, iq_nonintra_{};
    std::array<std::uint8_t, 32> vq_{};
    std::uint32_t th0_ = 0, th1_ = 0;
    int dc_pred_[3] = {0, 0, 0};
    std::uint64_t commands_ = 0;
    std::uint64_t consumed_ = 0;   // bits consumed since construction (progress detection only)
};

}   // namespace b3native

#endif
