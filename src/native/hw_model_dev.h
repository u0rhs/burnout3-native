// hw_model_dev.h -- the hardware model of the dev tools and the retail oracle (the device-state work, 2026-11-10): the
// shipped game's HwModel (hw_model.h) plus what only retail code in the mini (tests/r5900_mini.h: native_boot's
// oracle, mpeg_play, test_ipu_model) reaches. Never included by the shipped game's sources (src/native/platform*.cpp,
// native_play): there these devices do not exist and an access is fatal.
//
// The IPU (ipu_model.h, movie1): its registers 0x10002000-0x1000203F, IPU_out_FIFO / IPU_in_FIFO and the fromIPU (3) /
// toIPU (4) DMA channels, with D_ENABLEW's DMA hold. toSPR in chain mode (retail libmpeg's dmaRefImage, CHCR 0x105):
// the source chain's payload, qword by qword, to the scratchpad from SADR on (the shared source-chain walker, SADR
// advanced); QWC left by the CPU first [L: the transfer starts from MADR / QWC]. MADR / TADR after the chain are not
// updated [G: libmpeg reads only CHCR back, C libmpeg.s]; TTE is fatal (walk_source_chain gives a VIF's 64 tag bits;
// libmpeg never sets it). Also the DMAC source-chain walker (walk_source_chain) and, built on it, the check of the
// product's PATH3 rules against a VIF code walk (Path3Check) and the DMA stream hash of the diagnostics.
#ifndef B3NATIVE_HW_MODEL_DEV_H
#define B3NATIVE_HW_MODEL_DEV_H

#include "hw_model.h"
#include "ipu_model.h"

namespace b3native {

// The DMAC's source-chain walk (dev tools only: the shipped game reads no DMA chain): the words one transfer reads, in
// order, each with its guest address -- normal mode (CHCR.MOD 0: QWC qwords at MADR) or chain mode (MOD 1: DMAtags from
// TADR; with TTE the tag's upper 64 bits first) [C EE manual DMAC source-chain tags: refe 0, cnt 1, next 2, ref 3,
// refs 4, call 5, ret 6, end 7; QWC bits 0-15, ID 28-30, ADDR the second word; an IRQ tag with CHCR.TIE stops].
// `regs` = CHCR, MADR, QWC, TADR; rd32(guest address) reads a word; errors throw HwUnmodelled.
inline std::uint32_t dma_address(std::uint32_t x) { return (x & 0x80000000u) ? (0x70000000u | (x & 0x3FF0u)) : (x & 0x01FFFFF0u); }
template <class R, class F>
void walk_source_chain(const std::uint32_t* regs, const char* name, R&& rd32, F&& word) {
    const std::uint32_t chcr = regs[0];
    const unsigned mod = (chcr >> 2) & 3u;
    const bool tte = (chcr >> 6) & 1u;
    auto fail = [name](const char* what) { throw HwUnmodelled(std::string("hardware model: ") + name + " DMA " + what); };
    auto words = [&](std::uint32_t a, std::uint32_t n) { for (std::uint32_t k = 0; k < n; ++k) word(a + 4 * k, rd32(a + 4 * k)); };
    if (mod == 0) {
        words(dma_address(regs[1]), (regs[2] & 0xFFFFu) * 4u);
        return;
    }
    if (mod != 1) fail("in interleave mode");
    std::uint32_t tadr = dma_address(regs[3]), asr[2] = {0, 0};
    unsigned depth = 0;
    for (unsigned guard = 0; guard < 100000; ++guard) {
        const std::uint32_t t0 = rd32(tadr), t1 = rd32(tadr + 4);
        const unsigned qwc = t0 & 0xFFFFu, id = (t0 >> 28) & 7u;
        const std::uint32_t addr = dma_address(t1);
        if (tte) { word(tadr + 8, rd32(tadr + 8)); word(tadr + 12, rd32(tadr + 12)); }
        const std::uint32_t data = tadr + 16;
        switch (id) {
        case 0: words(addr, qwc * 4u); return;                                    // refe
        case 1: words(data, qwc * 4u); tadr = data + qwc * 16u; break;            // cnt
        case 2: words(data, qwc * 4u); tadr = addr; break;                        // next
        case 3: case 4: words(addr, qwc * 4u); tadr += 16; break;                 // ref / refs
        case 5:                                                                    // call
            words(data, qwc * 4u);
            if (depth == 2) fail("call depth over 2");
            asr[depth++] = data + qwc * 16u;
            tadr = addr;
            break;
        case 6:                                                                    // ret
            words(data, qwc * 4u);
            if (depth == 0) return;
            tadr = asr[--depth];
            break;
        case 7: words(data, qwc * 4u); return;                                    // end
        }
        if ((t0 >> 31) && ((chcr >> 7) & 1u)) return;   // IRQ with TIE: the transfer stops after this tag
    }
    fail("chain without an end");
}

// The PATH3 masking of a VIF1 stream as a full VIF code walk reads it (dev: what the product's rules, hw_model.h
// start_dma, replaced): each MSKPATH3 code, every other code's payload skipped by its size [C EE manual VIF codes:
// UNPACK by vn / vl / num, STMASK 1 word, STROW / STCOL 4, MPG num x 2, DIRECT / DIRECTHL imm qwords].
struct Vif1MaskWalk {
    unsigned payload = 0;
    int last = -1;   // the last MSKPATH3's mask bit (-1: none seen)
    void word(std::uint32_t x) {
        if (payload) { --payload; return; }
        const std::uint32_t cmd = (x >> 24) & 0x7Fu, imm = x & 0xFFFFu, num = (x >> 16) & 0xFFu;
        if ((cmd & 0x60u) == 0x60u) {   // UNPACK: num vectors of vn+1 elements of 32 >> vl bits, padded to words
            const unsigned vl = cmd & 3u, vn = (cmd >> 2) & 3u, n = num ? num : 256u;
            payload = (n * (vn + 1u) * (32u >> vl) + 31u) / 32u;
            return;
        }
        switch (cmd) {
        case 0x06: last = (x & 0x8000u) ? 1 : 0; return;
        case 0x20: payload = 1; return;
        case 0x30: case 0x31: payload = 4; return;
        case 0x4A: payload = 2u * (num ? num : 256u); return;
        case 0x50: case 0x51: payload = 4u * (imm ? imm : 65536u); return;
        default: return;
        }
    }
};

// The dev check of hw_model.h's PATH3 rules (the device-state work, H3) against the VIF code walk, at each kick before
// the model handles it: a VIF1 chain must leave PATH3 unmasked (the product clears M3P at every VIF1 kick), and a GIF
// transfer kicked under the mask must be released by the next VIF1 chain, through its MSKPATH3(0), at the same clock
// (the product completes it at its own kick). A violation throws HwUnmodelled, naming it. Used by HwModelDev (the
// oracle's minis) and, through HwModel::on_dma, by a dev native_play (make HW_DEV=1).
struct Path3Check {
    std::uint64_t vif1 = 0, gates = 0;
    bool gif_masked = false;
    std::uint64_t gif_clock = 0;
    template <class R>
    void kick(unsigned c, const std::uint32_t* regs, bool m3p, std::uint64_t now, R&& rd32) {
        if (c == 2) {
            if (m3p) { gif_masked = true; gif_clock = now; }
            return;
        }
        if (c != 1) return;
        ++vif1;
        Vif1MaskWalk w;
        walk_source_chain(regs, "VIF1", rd32, [&w](std::uint32_t, std::uint32_t x) { w.word(x); });
        if (w.last < 0 ? m3p : w.last == 1)
            throw HwUnmodelled("PATH3 check: a VIF1 chain (TADR " + std::to_string(regs[3]) + ") leaves PATH3 masked; hw_model.h's "
                               "rule (every VIF1 chain ends unmasked) no longer holds");
        if (gif_masked) {
            ++gates;
            gif_masked = false;
            if (w.last != 0) throw HwUnmodelled("PATH3 check: a masked GIF transfer's VIF1 chain carries no MSKPATH3(0)");
            if (now != gif_clock)
                throw HwUnmodelled("PATH3 check: a masked GIF transfer released at a later clock than its kick (hw_model.h "
                                   "completes it at the kick)");
        }
    }
    void report(std::FILE* out, const char* prefix) const {
        std::fprintf(out, "%sPATH3 check: %llu VIF1 chains walked, %llu gates (a masked GIF transfer released by its chain at "
                     "its kick's clock), every chain ended unmasked\n", prefix, static_cast<unsigned long long>(vif1),
                     static_cast<unsigned long long>(gates));
    }
};

// The words a VIF1 / GIF transfer reads from guest memory, folded into an FNV-1a 64 and handed to `sink` (dev
// diagnostics: native_boot's maskhash "dma" field and B3NATIVE_DMA_DUMP, a dev native_play's B3NATIVE_VIF1_DUMP), walked
// at the kick (HwModel::on_dma) from the registers' values then.
template <class R, class F>
void hash_dma_words(unsigned c, const std::uint32_t* regs, std::uint64_t& h, R&& rd32, F&& sink) {
    if (c != 1 && c != 2) return;
    walk_source_chain(regs, c == 1 ? "VIF1" : "GIF", rd32, [&](std::uint32_t a, std::uint32_t w) {
        h = (h ^ w) * 0x100000001b3ull;
        sink(a, w, c);
    });
}

class HwModelDev : public HwModel, private IpuHost {
public:
    HwModelDev() = default;
    HwModelDev(const HwModelDev&) = default;
    HwModelDev& operator=(const HwModelDev&) = default;
    // a copy of a product model (the native platform's, when a comparison starts from a live native state): its
    // registers, DMA and interrupt state; the dev devices start from reset (the product has none)
    explicit HwModelDev(const HwModel& m) : HwModel(m) {}
    HwModelDev& operator=(const HwModel& m) {
        HwModel::operator=(m);
        ipu_.reset_all();
        path3_ = Path3Check{};
        return *this;
    }
    void reset() {
        HwModel::reset();
        ipu_.reset_all();
        path3_ = Path3Check{};
    }
    // movie1: the IPU (registers 0x10002000-0x1000203F, IPU_in_FIFO / IPU_out_FIFO, DMA channels 3 / 4)
    IpuModel& ipu() { return ipu_; }
    const IpuModel& ipu() const { return ipu_; }
    const Path3Check& path3_check() const { return path3_; }

protected:
    bool device_write(std::uint32_t a, unsigned width, std::uint64_t v, std::uint64_t hi) override {
        if (IpuModel::owns(a)) { ipu_.write(*this, a, width, v, hi); return true; }
        if (a == 0x1000F590u && width == 4) {   // D_ENABLEW: the DMA hold released (or set)
            set_reg32(a, static_cast<std::uint32_t>(v));
            ipu_.dma_kick(*this);
            return true;
        }
        if (a == 0x10005000u && width == 16) {   // the VIF1 FIFO: the product's MSKPATH3 rule against the walk
            Vif1MaskWalk w;
            const std::uint32_t x[4] = {static_cast<std::uint32_t>(v), static_cast<std::uint32_t>(v >> 32),
                                        static_cast<std::uint32_t>(hi), static_cast<std::uint32_t>(hi >> 32)};
            int first = -1;
            for (std::uint32_t k : x) {
                w.word(k);
                if (first < 0 && ((k >> 24) & 0x7Fu) == 0x06u) first = (k & 0x8000u) ? 1 : 0;
            }
            if (first != w.last) throw HwUnmodelled("PATH3 check: a VIF1 FIFO qword whose first MSKPATH3 is not its last");
        }
        return false;
    }
    bool device_read(std::uint32_t a, unsigned width, std::uint64_t& v) override {
        if (!IpuModel::owns(a)) return false;
        if (width == 16) fail("load from", a, width);
        v = ipu_.read(*this, a, width);
        return true;
    }
    bool has_ipu_channels() const override { return true; }
    bool device_dma(unsigned c, std::uint32_t* r) override {
        if (c == 3 || c == 4) {   // movie1: the IPU's channels move data as the IPU takes / gives it
            ipu_.dma_start(*this, c);
            return true;
        }
        if (c == 9 && ((r[0] >> 2) & 3u) == 1u && scratchpad && write_ram) {   // movie2: toSPR in chain mode
            to_spr_chain(r);
            finish_dma(c);   // completes as a product transfer does (hw_model.h)
            return true;
        }
        if ((c == 1 || c == 2) && read_ram)   // the PATH3 rules against the walk; the product then handles the kick
            path3_.kick(c, r, mskpath3(), clock ? clock() : 0, [this](std::uint32_t a) { return rd32(a); });
        return false;
    }

private:
    std::uint32_t rd32(std::uint32_t a) {
        std::uint32_t v = 0;
        if (!read_ram(a, &v, 4)) fail("DMA source outside RAM at", a, 4);
        return v;
    }
    void to_spr_chain(std::uint32_t* r) {
        const std::uint32_t base = 0x1000D400u;
        if (r[0] & 0x40u) fail("toSPR DMA chain with TTE at", base, 4);
        std::uint8_t* spr = scratchpad();
        const std::uint32_t sa = r[8] & 0x3FF0u;
        std::uint32_t at = 0;
        auto word = [&](std::uint32_t, std::uint32_t w) {
            std::memcpy(spr + ((sa + at) & 0x3FFCu), &w, 4);
            at += 4;
        };
        std::uint32_t lead[4];
        for (std::uint32_t k = 0; k < (r[2] & 0xFFFFu); ++k) {
            const std::uint32_t ma = dma_address(r[1]) + 16 * k;
            if (!read_ram(ma, lead, 16)) fail("toSPR DMA source outside RAM at", ma, 16);
            for (std::uint32_t w : lead) word(0, w);
        }
        walk_source_chain(r, "toSPR", [this](std::uint32_t a) { return rd32(a); }, word);
        r[8] = (sa + at) & 0x3FF0u;
    }
    // movie1: IpuHost (ipu_model.h)
    std::uint32_t* ipu_channel(unsigned c) override { return channel_regs(c); }
    bool ipu_read_mem(std::uint32_t x, void* out, unsigned n) override { return read_ram && read_ram(dma_address(x), out, n); }
    bool ipu_write_mem(std::uint32_t x, const void* in, unsigned n) override { return write_ram && write_ram(dma_address(x), in, n); }
    void ipu_dma_finished(unsigned c) override { finish_dma(c); }
    bool ipu_dma_held() override {
        std::uint32_t v = 0;
        return reg32(0x1000F590u, v) && (v & 0x10000u);
    }
    IpuModel ipu_;
    Path3Check path3_;
};

}  // namespace b3native

#endif
