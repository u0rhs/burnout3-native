// src/native/dmac_delivery.h -- HwModel's DMAC completion queue and its delivery, scratchpad DMA, the D_STAT mask
// (docs/specs/ee-kernel.md §8).
//
// A finished transfer sets its D_STAT status bit at once; with a clock (the platform) its callback is queued FIFO to be
// ready completion_delay(c) cycles later and handed to the owner (on_dmac_interrupt) once the clock reaches it, through a
// pending list that keeps the channels taken from the queue but not yet delivered (masked, no handler, or refused).
// Without a clock (unit tests) the pending status bits are offered at once in channel order.
#ifndef B3NATIVE_DMAC_DELIVERY_H
#define B3NATIVE_DMAC_DELIVERY_H

#include "hw_model.h"


#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace b3native {

inline std::uint64_t HwModel::completion_delay(unsigned c) { return c < 8 ? kDmacCompletionDelay : 0; }

inline std::uint64_t HwModel::next_completion_ready() const {
    return completions_.empty() ? ~std::uint64_t{0} : completions_.front().ready;
}

inline void HwModel::rebase_completions(std::uint64_t from_now, std::uint64_t to_now) {
    for (Completion& c : completions_) c.ready = to_now + (c.ready > from_now ? c.ready - from_now : 0);
}

inline std::size_t HwModel::queued_completions() const { return completions_.size(); }

inline const std::vector<unsigned>& HwModel::pending_dmac() const { return pending_dmac_; }

// D_STAT's mask half (bits 16..25) = the enabled channels; the status half stays
inline void HwModel::set_dmac_mask(std::uint32_t channels) {
    d_stat_ = (d_stat_ & 0xFFFFu) | ((channels & 0x3FFu) << 16);
    check_dmac_interrupts();
}

inline void HwModel::finish_dma(unsigned c) {
    ch_[c][0] &= ~0x100u;   // CHCR.STR
    ch_[c][2] = 0;          // QWC
    d_stat_ |= 1u << c;
    if (on_complete) on_complete(c);
    if (clock) completions_.push_back(Completion{c, clock() + completion_delay(c)});
    check_dmac_interrupts();
}

// fromSPR (8) / toSPR (9) in normal mode: QWC quadwords between RAM at MADR and the scratchpad at SADR [ps2tek DMAC]
inline void HwModel::spr_dma(unsigned c, std::uint32_t* r) {
    const std::uint32_t base = c == 8 ? 0x1000D000u : 0x1000D400u;
    const unsigned mode = (r[0] >> 2) & 3u;
    if (mode == 1) fail(c == 8 ? "fromSPR DMA in chain mode at" : "toSPR DMA in chain mode at", base, 4);
    if (mode != 0) fail(c == 8 ? "fromSPR DMA in interleave mode at" : "toSPR DMA in interleave mode at", base, 4);
    const std::uint32_t madr = r[1];
    if (madr >= 0x10000000u) fail(c == 8 ? "fromSPR DMA to a non-RAM MADR at" : "toSPR DMA from a non-RAM MADR at", base + 0x10, 4);
    const std::uint32_t ram = madr & ~0xFu;
    const std::uint32_t qwc = r[2] & 0xFFFFu;
    const std::uint32_t sa = r[8] & 0x3FF0u;
    std::uint8_t* spr = scratchpad();
    for (std::uint32_t k = 0; k < qwc; ++k) {
        std::uint8_t* s = spr + ((sa + 16u * k) & 0x3FF0u);
        const std::uint32_t m = ram + 16u * k;
        const bool ok = c == 9 ? (read_ram && read_ram(m, s, 16)) : write_ram(m, s, 16);
        if (!ok) fail(c == 9 ? "toSPR DMA source outside RAM at" : "fromSPR DMA destination outside RAM at", m, 16);
    }
    r[1] = madr + 16u * qwc;
    r[8] = (sa + 16u * qwc) & 0x3FF0u;
}

inline void HwModel::check_dmac_interrupts() {
    if (clock) {
        check_deferred();
        return;
    }
    if (in_check_) return;
    in_check_ = true;
    struct Done { bool& f; ~Done() { f = false; } } done{in_check_};
    while (on_dmac_interrupt) {
        const std::uint32_t ready = d_stat_ & (d_stat_ >> 16) & 0x3FFu;
        if (!ready) break;
        unsigned c = 0;
        while (!(ready & (1u << c))) ++c;
        if (!on_dmac_interrupt(c)) break;
    }
}

// With a clock: the completions whose time has come join the pending list (once per channel); the first pending channel
// that is enabled (and has a handler, when the owner says) is delivered, one at a time, until none can be.
inline void HwModel::check_deferred() {
    if (in_check_) return;
    in_check_ = true;
    struct Done { bool& f; ~Done() { f = false; } } done{in_check_};
    for (;;) {
        const std::uint64_t now = clock();
        std::size_t taken = 0;
        while (taken < completions_.size() && completions_[taken].ready <= now) {
            const unsigned c = completions_[taken].channel;
            if (std::find(pending_dmac_.begin(), pending_dmac_.end(), c) == pending_dmac_.end()) pending_dmac_.push_back(c);
            ++taken;
        }
        if (taken) completions_.erase(completions_.begin(), completions_.begin() + static_cast<std::ptrdiff_t>(taken));
        if (!on_dmac_interrupt) return;
        std::size_t i = 0;
        for (; i < pending_dmac_.size(); ++i) {
            const unsigned c = pending_dmac_[i];
            if (!(d_stat_ & (1u << (16 + c)))) continue;
            if (has_dmac_handler && !has_dmac_handler(c)) continue;
            break;
        }
        if (i == pending_dmac_.size()) return;
        const unsigned c = pending_dmac_[i];
        pending_dmac_.erase(pending_dmac_.begin() + static_cast<std::ptrdiff_t>(i));
        if (!on_dmac_interrupt(c)) {
            pending_dmac_.insert(pending_dmac_.begin() + static_cast<std::ptrdiff_t>(i), c);
            return;
        }
    }
}

}  // namespace b3native

#endif
