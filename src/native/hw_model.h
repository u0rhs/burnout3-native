// hw_model.h -- the device state the native platform keeps for the game's own hardware code (boot2; include/b3hw/hw.h,
// the hardware-hook decision of 2026-11-05; reduced to device state by the device-state work, 2026-11-10).
//
// Nothing here draws: the renderer reads the game's scene from guest RAM at function entries (gameview_native.cpp
// watch(): scene_capture.cpp / hud_capture.cpp), never from this model. What is here is what the ported RenderWare /
// Sony library C (badma.cpp, libgraph, libdma; their b3hw_* calls) and the decomp's inline asm read back, as an
// operating system's platform layer would keep it:
//   - a register file with the write rules the code relies on: D_STAT's write-1-to-clear / toggle halves, INTC_STAT's
//     write-1-to-clear, GS CSR's reset; every other register keeps what is written; an unknown one is fatal, naming it
//     (HwModel::Unmodelled);
//   - DMA as completion only: a transfer completes at its kick (CHCR.STR clears, QWC 0, D_STAT's channel bit), its
//     DMAC callback queued FIFO to be ready kDmacCompletionDelay (32) virtual EE cycles later (SPR: 0), delivered by
//     the platform's clock (platform_iop.cpp); the one transfer whose data the game reads back is the scratchpad's
//     (fromSPR / toSPR normal mode: a copy);
//   - PATH3 masking as state (GIF_STAT.M3P and FQC, three rules below start_dma), what RW's DMA gate polls;
//   - the VBlank grid seen through INTC_STAT (a poll advances virtual time to the next VBlank, on_vblank) and the GS
//     CSR FIELD toggle;
//   - VIF0 transfers as "load this VU0 image" (on_vif0_kick: Vu0Unit picks one of retail's four by address).
// No DMA chain is walked, no VIF / GIF / IPU code is interpreted. The dev tools' model (hw_model_dev.h: the retail
// oracle's mini, mpeg_play, native_boot's diagnostics) adds the IPU, toSPR chains, the DMAC chain walker and the
// check of the PATH3 rules against a VIF code walk; the shipped game never includes it. The kernel's handler lists,
// semaphores and threads (KernelModel) live here too.
//
// Evidence: the EE / GS manuals (research/ext/PS2Docs, ps2tek) for the register layouts [C]; docs/specs/
// ee-kernel.md §3, §6.3 and §8 for the kernel-visible behaviour (GS CSR 0x551B0000, IMR 0x7F00 after reset; GsPutIMR
// stores the mask and returns the previous; EnableDmac / DisableDmac set D_STAT's mask half; the kernel acknowledges
// an INTC cause before its handlers run; the DMAC completion queue); the H2 census (the device-state work) for the
// PATH3 rules. Tags: [C] confirmed, [L] likely, [G] guess.
#ifndef B3NATIVE_HW_MODEL_H
#define B3NATIVE_HW_MODEL_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "kernel_model.h"

namespace b3native {

// An access the hardware model does not model (HwModel::Unmodelled; the dev IPU model's, ipu_model.h, is this type too,
// so the platform's handler catches both).
struct HwUnmodelled : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// struct KernelModel (the kernel's handler lists, masks, semaphores, thread records): kernel_model.h.

class HwModel {
public:
    using Unmodelled = HwUnmodelled;   // movie2: one type with IpuModel's (ipu_model.h)
    HwModel(const HwModel&) = default;
    HwModel& operator=(const HwModel&) = default;
    virtual ~HwModel() = default;
    // Called when a poll needs the next VBlank: the owner advances virtual time and delivers the interrupt (the kernel
    // acknowledges INTC_STAT's bit before the handlers run, ee-kernel.md §6.3); the poll then
    // sees the bit set [L: on hardware the poll can observe VBON before the interrupt is taken, which is how VSync
    // returns with a VBlank handler installed]. Without it the poll is fatal (no time source).
    std::function<void()> on_vblank;
    // A DMA transfer started (channel, CHCR, MADR, QWC, TADR at the start): an observer for diagnostics (native_boot's
    // DMA trace and stream hash, a dev build's PATH3 check: hw_model_dev.h); the model completes the transfer.
    std::function<void(unsigned, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)> on_dma;
    // A DMAC channel interrupt is pending (its D_STAT status and mask bits both set): the owner delivers it now and
    // returns true (acknowledging the status bit, as the kernel does before the handler,
    // ee-kernel.md §7), or returns false while interrupts are off; it stays pending, and the owner calls
    // poll_interrupts() when they come back on.
    std::function<bool(unsigned)> on_dmac_interrupt;
    // H4: a VIF0 transfer started (CHCR, TADR): the owner loads the VU0 image it uploads (the platform: Vu0Unit, by
    // the chain's address -- CGtVu0CodeManager's only transfers, one of retail's four images); unset: VIF0 transfers
    // complete at once with no effect (boot2's behaviour).
    std::function<void(std::uint32_t, std::uint32_t)> on_vif0_kick;
    // Guest memory for toSPR (and the dev model's DMA walker): false when the range is not RAM.
    std::function<bool(std::uint32_t, void*, unsigned)> read_ram;
    // guest memory for fromSPR DMA (and the dev IPU's fromIPU, hw_model_dev.h): false when the range is not RAM.
    std::function<bool(std::uint32_t, const void*, unsigned)> write_ram;
    // vu0 (boot2-agreed, 2026-11-07): the DMAC completion delay (docs/specs/ee-kernel.md §8).
    // With `clock` set (the platform: virtual EE cycles), a completed transfer's register effects and D_STAT status
    // bit are immediate as before, but its DMAC callback is queued FIFO, ready kDmacCompletionDelay cycles after the
    // kick (static-cost cycles); ready entries move, front first, to the pending list (one entry per channel), from
    // which a channel is delivered when it is enabled (D_STAT mask), has a handler (has_dmac_handler, when set) and
    // on_dmac_interrupt accepts (interrupts on, no handler running). Unset (tests): delivery as before, at once on
    // status and mask.
    std::function<std::uint64_t()> clock;
    std::function<bool(unsigned)> has_dmac_handler;
    static constexpr std::uint64_t kDmacCompletionDelay = 32;
    // the delay per channel: SPR transfers (8 / 9) queue with 0, the others 32 (ee-kernel.md §8)
    static std::uint64_t completion_delay(unsigned c);
    // vu0 (SPR DMA, boot2-agreed): the scratchpad buffer for fromSPR / toSPR (with write_ram above); unset: no data moves
    std::function<std::uint8_t*()> scratchpad;
    // layer_check: a transfer completed (its channel); the platform's B3NATIVE_DMA_LOG stamps it with the clock
    std::function<void(unsigned)> on_complete;
    // H3: a GIF transfer started under the PATH3 mask (the FQC latch set): the platform's gate wake-up
    // (platform_iop.cpp gate_latched: a time event 64 cycles out, which brings the gate's DMAC callbacks in on time)
    std::function<void()> on_path3_latch;
    // mac5 diagnostics (native_play's stall watchdog): the DMA channels' CHCR, D_STAT, PATH3 and the queued completions
    void debug_state(std::FILE* out) const {
        std::fprintf(out, "hw: CHCR");
        for (unsigned c = 0; c < 10; ++c) std::fprintf(out, " %u:%08X", c, ch_[c][0]);
        std::fprintf(out, "; D_STAT %08X; M3P %d, FQC latched %d; clock %llu; queued", d_stat_, int(mskpath3_), int(fqc_latch_),
                     static_cast<unsigned long long>(clock ? clock() : 0));
        for (const auto& c : completions_) std::fprintf(out, " %u@%llu", c.channel, static_cast<unsigned long long>(c.ready));
        std::fprintf(out, "; pending");
        for (unsigned c : pending_dmac_) std::fprintf(out, " %u", c);
        std::fprintf(out, "; in check %d; DMAs %llu\n", int(in_check_), static_cast<unsigned long long>(dmas_));
    }
    // the earliest ready time of a queued completion (~0 when none): the owner's next time event
    std::uint64_t next_completion_ready() const;
    // boot2: a copy moved to another clock (boot_oracle's MiniPlatform, whose time is the mini's retired count): every
    // queued completion keeps its remaining delay (old clock `from_now` -> new clock `to_now`)
    void rebase_completions(std::uint64_t from_now, std::uint64_t to_now);
    std::size_t queued_completions() const;
    const std::vector<unsigned>& pending_dmac() const;

    HwModel() { reset(); }
    void reset() {
        gs_.clear();
        gs_csr_ = kCsrReset;
        gs_imr_ = kImrReset;
        regs32_.clear();
        for (unsigned c = 0; c < 10; ++c) for (unsigned r = 0; r < 9; ++r) ch_[c][r] = 0;
        d_stat_ = 0;
        intc_stat_ = 0;
        vu_fbrst_ = 0;
        dmas_ = 0;
        mskpath3_ = false;
        fqc_latch_ = false;
        completions_.clear();
        pending_dmac_.clear();
    }
    // GS CSR REV / ID after a reset: 0x551B0000; IMR 0x7F00 (ee-kernel.md §8).
    static constexpr std::uint64_t kCsrReset = 0x551B0000u;
    static constexpr std::uint64_t kImrReset = 0x7F00u;
    static constexpr std::uint32_t kCop2Ctrl = 0xC2000000u;   // b3hw.h B3HW_COP2_CTRL

    std::uint64_t gs_csr() const { return gs_csr_; }
    std::uint64_t gs_imr() const { return gs_imr_; }
    void set_gs_imr(std::uint64_t v) { gs_imr_ = v; }
    std::uint32_t d_stat() const { return d_stat_; }
    // vu0: COP0 condition 0 (BC0F / BC0T): set when every channel D_PCR.CPC selects has D_STAT.CIS set,
    // ((~CPC | CIS) & 0x3FF) == 0x3FF [C ps2tek index.html, DMAC D_PCR]
    bool cpcond0() const {
        const auto it = regs32_.find(0x1000E020u);
        const std::uint32_t pcr = it == regs32_.end() ? 0u : it->second;
        return ((~pcr | d_stat_) & 0x3FFu) == 0x3FFu;
    }
    // EnableDmac / DisableDmac: the kernel keeps D_STAT's mask half equal to its enabled channels (ee-kernel.md §3)
    void set_dmac_mask(std::uint32_t channels);
    // A VBlank (the owner's on_vblank): VBON set (the interrupt is the owner's), the GS CSR FIELD bit toggles [L].
    void vblank_start() {
        intc_stat_ |= 4u;
        gs_csr_ ^= 0x2000u;
        gs_csr_ |= 0x8u;   // VSINT [L]
    }
    void ack_intc(std::uint32_t cause) { intc_stat_ &= ~(1u << cause); }
    void ack_dmac(unsigned channel) { d_stat_ &= ~(1u << channel); }
    void poll_interrupts() { check_dmac_interrupts(); }
    std::uint32_t intc_stat() const { return intc_stat_; }
    std::uint64_t dma_count() const { return dmas_; }
    bool mskpath3() const { return mskpath3_; }
    bool fqc_latched() const { return fqc_latch_; }
    std::uint32_t chan(unsigned c, unsigned r) const { return ch_[c][r]; }
    // GS privileged display registers as written (PMODE 0x12000000 .. BGCOLOR 0x120000E0), as the game reads them back
    std::uint64_t gs_reg(std::uint32_t addr) const {
        auto it = gs_.find(addr);
        return it == gs_.end() ? 0 : it->second;
    }

    void write(std::uint32_t a, unsigned width, const std::uint8_t* d) {
        std::uint64_t v = 0, hi = 0;
        std::memcpy(&v, d, width >= 8 ? 8 : width);
        if (width == 16) std::memcpy(&hi, d + 8, 8);
        if (device_write(a, width, v, hi)) return;   // a dev model's extra devices (hw_model_dev.h: the IPU)
        if (a >= 0x12000000u && a <= 0x120000E0u && (a & 0xF) == 0 && width == 8) { gs_[a] = v; return; }
        if (a == 0x12001000u && (width == 8 || width == 4)) {   // CSR: RESET (bit 9) resets the GS; low bits 0-4 W1C
            if (v & 0x200u) { gs_.clear(); gs_csr_ = kCsrReset; gs_imr_ = kImrReset; return; }
            gs_csr_ &= ~(v & 0x1Fu);
            return;
        }
        if (ipu_address(a)) fail(kNoIpu, a, width);
        if (a == kCop2Ctrl + 28 && width == 4) { vu_fbrst_ = 0; return; }   // VU FBRST: resets complete at once [L]
        if (a == 0x1000E010u && width == 4) {   // D_STAT: CIS / SIS / MEIS / BEIS write 1 to clear, masks write 1 to toggle
            const std::uint32_t w = static_cast<std::uint32_t>(v);
            d_stat_ = (d_stat_ & ~(w & 0xFFFFu)) ^ (w & 0xFFFF0000u);
            check_dmac_interrupts();
            return;
        }
        if (a == 0x1000F000u && width == 4) { intc_stat_ &= ~static_cast<std::uint32_t>(v); return; }
        if (int c = channel_of(a); c >= 0 && width == 4) {
            const unsigned r = (a & 0xFF) >> 4;
            // the IPU's channels keep their registers (sceDmaReset clears every channel's); a transfer is fatal
            if ((c == 3 || c == 4) && r == 0 && (v & 0x100u) && !has_ipu_channels()) fail(kNoIpu, a, width);
            ch_[c][r] = static_cast<std::uint32_t>(v);
            if (r == 0 && (v & 0x100u)) start_dma(static_cast<unsigned>(c));
            return;
        }
        if (width == 4 && plain32(a)) {
            regs32_[a] = static_cast<std::uint32_t>(v);
            return;
        }
        if (a == 0x10005000u && width == 16) {
            // VIF1 FIFO (the CPU's PATH2 stores: badma's gate qword {MSKPATH3(1), 0, 0, 0}, sceGsResetPath's two qwords of
            // init codes): of what VIF1 does with them, only MSKPATH3 (code 0x06, bit 15 the mask) changes state the game
            // reads back (GIF_STAT.M3P). The qword's first MSKPATH3 sets it [H2 census over the STRICT scenarios: every
            // store agrees with a full VIF code walk].
            const std::uint32_t w[4] = {static_cast<std::uint32_t>(v), static_cast<std::uint32_t>(v >> 32),
                                        static_cast<std::uint32_t>(hi), static_cast<std::uint32_t>(hi >> 32)};
            for (std::uint32_t x : w)
                if (((x >> 24) & 0x7Fu) == 0x06u) {
                    set_m3p((x & 0x8000u) != 0);
                    break;
                }
            return;
        }
        fail("store to", a, width);
    }
    void read(std::uint32_t a, unsigned width, std::uint8_t* d) {
        std::uint64_t v = 0;
        if (device_read(a, width, v)) {   // a dev model's extra devices (hw_model_dev.h: the IPU)
            std::memcpy(d, &v, width >= 8 ? 8 : width);
            return;
        }
        if (ipu_address(a)) fail(kNoIpu, a, width);
        if (a >= 0x12000000u && a <= 0x120000E0u && (a & 0xF) == 0 && width == 8) v = gs_reg(a);
        else if (a == 0x12001000u && (width == 8 || width == 4)) v = gs_csr_;
        else if (a == kCop2Ctrl + 28 && width == 4) v = vu_fbrst_;
        else if (a == kCop2Ctrl + 29 && width == 4) v = 0;   // VPU-STAT: VU0 / VU1 idle (programs run at once) [L]
        else if (a == 0x1000E010u && width == 4) v = d_stat_;
        else if (a == 0x1000F000u && width == 4) {
            if (!(intc_stat_ & 4u)) {   // a VBlank poll: time advances to the next VBlank
                if (!on_vblank) fail("VBlank poll (no time source) at", a, width);
                on_vblank();
                intc_stat_ |= 4u;   // what the poll observes [L], see on_vblank
            }
            v = intc_stat_;
        } else if (int c = channel_of(a); c >= 0 && width == 4) {
            v = ch_[c][(a & 0xFF) >> 4];
        }
        else if (width == 4 && plain32(a)) {
            auto it = regs32_.find(a);
            v = it == regs32_.end() ? 0 : it->second;
            if (a == 0x10003C00u) v &= ~0x1F000003u;   // VIF1_STAT: VPS idle, FIFO empty (transfers complete at once)
            if (a == 0x10003020u) {                     // GIF_STAT: APATH idle; M3P (bit 1) = VIF1's MSKPATH3
                v &= ~0xC02u;
                if (mskpath3_) v |= 2u;
                if (fqc_latch_) v |= 16u << 24;   // FQC: the PATH3 transfer behind the mask fills the GIF FIFO [L]
            }
        } else fail("load from", a, width);
        std::memcpy(d, &v, width >= 8 ? 8 : width);
    }

protected:
    // The extension points of a dev model (hw_model_dev.h: the IPU, retail libmpeg's toSPR chains): an access or a
    // transfer it serves itself (true), else the product's rules apply. The shipped game's model has none.
    virtual bool device_write(std::uint32_t, unsigned, std::uint64_t, std::uint64_t) { return false; }
    virtual bool device_read(std::uint32_t, unsigned, std::uint64_t&) { return false; }
    virtual bool device_dma(unsigned, std::uint32_t*) { return false; }
    virtual bool has_ipu_channels() const { return false; }
    // dev-model access to the state
    std::uint32_t* channel_regs(unsigned c) { return ch_[c]; }
    bool reg32(std::uint32_t a, std::uint32_t& v) const {
        auto it = regs32_.find(a);
        if (it == regs32_.end()) return false;
        v = it->second;
        return true;
    }
    void set_reg32(std::uint32_t a, std::uint32_t v) { regs32_[a] = v; }

    // The IPU (registers 0x10002000-0x1000203F, IPU_out_FIFO 0x10007000, IPU_in_FIFO 0x10007010, a transfer on DMA
    // channels 3 / 4) is not in the native platform: the movies are decoded natively (platform_movie.cpp, movie3) and
    // nothing in the shipped game reaches it, so an access is fatal (the channels' registers stay plain: sceDmaReset
    // clears every channel's). The dev IPU model (ipu_model.h, retail libmpeg's oracle) lives
    // in hw_model_dev.h.
    static constexpr const char* kNoIpu = "access to the IPU (not in the native platform: the movies are native, platform_movie.cpp) at";
    static bool ipu_address(std::uint32_t a) { return (a >= 0x10002000u && a < 0x10002040u) || a == 0x10007000u || a == 0x10007010u; }

    static std::string hex(std::uint32_t v) {
        char b[16];
        std::snprintf(b, sizeof b, "0x%08X", v);
        return b;
    }
    [[noreturn]] static void fail(const char* what, std::uint32_t a, unsigned width) {
        throw Unmodelled(std::string("hardware model: ") + what + " " + hex(a) + " (" + std::to_string(width) +
                         " bytes) not modelled");
    }
    // DMA channel register blocks [C EE manual]: VIF0 0x10008000, VIF1 0x10009000, GIF 0x1000A000, fromIPU 0x1000B000,
    // toIPU 0x1000B400, SIF0 0x1000C000, SIF1 0x1000C400, SIF2 0x1000C800, fromSPR 0x1000D000, toSPR 0x1000D400;
    // registers CHCR +0, MADR +0x10, QWC +0x20, TADR +0x30, ASR0 +0x40, ASR1 +0x50, SADR +0x80.
    static int channel_of(std::uint32_t a) {
        static const std::uint32_t base[10] = {0x10008000u, 0x10009000u, 0x1000A000u, 0x1000B000u, 0x1000B400u,
                                               0x1000C000u, 0x1000C400u, 0x1000C800u, 0x1000D000u, 0x1000D400u};
        for (int c = 0; c < 10; ++c) {
            if (a < base[c] || a >= base[c] + 0x90u) continue;
            const std::uint32_t r = a - base[c];
            if ((r & 0xF) == 0 && (r <= 0x50 || r == 0x80)) return c;
        }
        return -1;
    }
    // 32-bit registers that keep what is written: D_CTRL / D_PCR / D_SQWC / D_RBSR / D_RBOR / D_STADR, D_ENABLEW,
    // VIF0 / VIF1 STAT, FBRST, ERR, MARK, CYCLE, MODE, NUM, MASK, CODE, ITOPS, BASE, OFST, TOPS, ITOP, TOP, ROWs, COLs;
    // GIF CTRL / MODE / STAT; INTC_MASK is the kernel's (EnableIntc), never stored by library code [L].
    static bool plain32(std::uint32_t a) {
        if (a >= 0x1000E000u && a <= 0x1000E060u && (a & 0xF) == 0) return true;
        if (a == 0x1000F520u || a == 0x1000F590u) return true;
        if ((a >= 0x10003800u && a < 0x10003A00u) || (a >= 0x10003C00u && a < 0x10003E00u)) return (a & 0xF) == 0;
        if (a == 0x10003000u || a == 0x10003010u || a == 0x10003020u) return true;
        return false;
    }
    // A transfer starts (CHCR.STR written 1) and completes at once: CHCR.STR clears, QWC 0, D_STAT's channel bit, the
    // DMAC callback queued (finish_dma). PATH3 masking (the device-state work, H3; evidence: the H2 census over the
    // STRICT scenarios) is three rules, no VIF code walk: a GIF transfer started while GIF_STAT.M3P is set completes as
    // any other and latches GIF_STAT.FQC (its data waits in the GIF FIFO [C EE manual, GIF PATH3 masking]), which is
    // what RW's 0x21 gate polls for before it starts VIF1; a VIF1 transfer clears M3P and the latch (every VIF1 chain
    // the game sends ends unmasked: the gate's ends in MSKPATH3(0) after its last MSKPATH3(1), the others carry none
    // or MSKPATH3(0)); a VIF1 FIFO store sets M3P by its MSKPATH3 (write above). In the gate the GIF transfer so
    // completes before the VIF1 one, at the same clock (the census: no time passes between the two kicks), as when it
    // waited for the chain's MSKPATH3(0).
    void start_dma(unsigned c) {
        ++dmas_;
        std::uint32_t* r = ch_[c];
        if (on_dma) on_dma(c, r[0], r[1], r[2], r[3]);
        if (device_dma(c, r)) return;   // a dev model's transfer (hw_model_dev.h: the IPU's channels, toSPR chains)
        if ((c == 8 || c == 9) && scratchpad && write_ram) spr_dma(c, r);   // vu0: the scratchpad's data moves
        if (c == 1) set_m3p(false);   // the chain unmasks PATH3 (above)
        if (c == 0 && on_vif0_kick) on_vif0_kick(r[0], r[3]);   // H4: a VU0 code upload (above)
        if (c == 2 && mskpath3_) {
            fqc_latch_ = true;
            if (on_path3_latch) on_path3_latch();
        }
        finish_dma(c);
    }
    void finish_dma(unsigned c);
    void set_m3p(bool m) {
        mskpath3_ = m;
        if (!m) fqc_latch_ = false;
    }
    // vu0: fromSPR (8) / toSPR (9) in normal mode: QWC qwords between RAM at MADR and the scratchpad at SADR & 0x3FF0,
    // then MADR / SADR advanced by the bytes moved (ee-kernel.md §8); chain and interleave modes and a VU-memory MADR
    // are not modelled (toSPR chains came only from retail libmpeg's dmaRefImage: dev-only since movie3,
    // hw_model_dev.h)
    void spr_dma(unsigned c, std::uint32_t* r);
    // Pending DMAC interrupts in channel order, one at a time (a handler may start transfers: those are seen by this
    // loop, not by a nested one).
    void check_dmac_interrupts();

    // vu0: the deferred delivery (clock set): ready completions to the pending list in queue order (one entry per
    // channel), then the first deliverable pending channel, one at a time; a refused delivery
    // (interrupts off, inside a handler) leaves everything for the next check
    void check_deferred();
    struct Completion { unsigned channel; std::uint64_t ready; };
    std::vector<Completion> completions_;
    std::vector<unsigned> pending_dmac_;

    std::map<std::uint32_t, std::uint64_t> gs_;
    std::uint64_t gs_csr_ = kCsrReset, gs_imr_ = kImrReset;
    std::map<std::uint32_t, std::uint32_t> regs32_;
    std::uint32_t ch_[10][9] = {};
    std::uint32_t d_stat_ = 0, intc_stat_ = 0, vu_fbrst_ = 0;
    std::uint64_t dmas_ = 0;
    bool in_check_ = false;
    bool mskpath3_ = false;   // GIF_STAT.M3P: PATH3 masked by VIF1's MSKPATH3
    bool fqc_latch_ = false;  // a GIF transfer started under the mask: GIF_STAT.FQC shows its data until M3P clears
};

}  // namespace b3native

// HwModel's DMAC completion queue and delivery, scratchpad DMA, the D_STAT mask (docs/specs/ee-kernel.md §8): dmac_delivery.h
#include "dmac_delivery.h"

#endif
