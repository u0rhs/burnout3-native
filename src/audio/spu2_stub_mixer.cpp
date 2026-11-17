// The stub SPU2 (spu2_stub_mixer.h).
#include "spu2_stub_mixer.h"

#include <algorithm>
#include <cstring>

namespace b3audio {

Spu2StubMixer::Spu2StubMixer() : ram_(kRamBytes, 0) {}

uint64_t Spu2StubMixer::first_irq(uint64_t from, uint64_t to, uint32_t* cores) const {
    uint64_t best = UINT64_MAX;
    *cores = 0;
    for (int c = 0; c < 2; ++c) {
        const int pos = sd::ring_irq_pos(irqa_[c]);
        if (!irq_armed_[c] || pos < 0) continue;
        const uint64_t s = from + ((static_cast<uint64_t>(pos) - from) & 511);
        if (s >= to || s > best) continue;
        if (s < best) *cores = 0;
        best = s;
        *cores |= 1u << c;
    }
    return best;
}

uint64_t Spu2StubMixer::run(uint64_t t, uint32_t* irq_cores) {
    *irq_cores = 0;
    if (irq_pending_ != 0) {
        *irq_cores = irq_pending_;
        irq_pending_ = 0;
        return t_;
    }
    if (t <= t_) return t_;
    uint32_t cores = 0;
    const uint64_t s = first_irq(t_, t, &cores);
    if (s == UINT64_MAX) {
        step_to(t);
        return t;
    }
    step_to(s + 1);   // step_to latched them as pending
    *irq_cores = irq_pending_;
    irq_pending_ = 0;
    return t_;
}

void Spu2StubMixer::step_to(uint64_t t) {
    if (t <= t_) return;   // stamps never go back; an equal stamp is "the same instant"
    for (;;) {   // IRQs passed on the way are latched (the core disarms) and reported by the next run()
        uint32_t cores = 0;
        const uint64_t s = first_irq(t_, t, &cores);
        if (s == UINT64_MAX) break;
        for (int c = 0; c < 2; ++c)
            if (cores & (1u << c)) irq_armed_[c] = false;
        irq_pending_ |= cores;
    }
    const uint64_t dt = t - t_;
    for (Voice& v : v_) {
        run_voice(v, dt);
        run_volume(v.vol[0], dt);
        run_volume(v.vol[1], dt);
    }
    t_ = t;
}

void Spu2StubMixer::begin_block(Voice& v) {
    if (ram_[(v.block + 1) & (kRamBytes - 1)] & 4) v.lsax = v.block;   // loop start flag
}

void Spu2StubMixer::run_voice(Voice& v, uint64_t dt) {
    if (v.pitch == 0) return;
    const uint64_t acc = static_cast<uint64_t>(v.frac) + static_cast<uint64_t>(v.pitch) * dt;
    v.frac = static_cast<uint32_t>(acc & 0xFFF);
    uint64_t s = v.sample + (acc >> 12);
    while (s >= 28) {
        s -= 28;
        const uint8_t flags = ram_[(v.block + 1) & (kRamBytes - 1)];
        if (flags & 1) {
            v.block = v.lsax & (kRamBytes - 16);
            if (!(flags & 2)) {
                if (v.envx != 0) ++v.ends;
                v.envx = 0;
                v.keyed = false;
            }
        } else {
            v.block = (v.block + 16) & (kRamBytes - 16);
        }
        begin_block(v);
    }
    v.sample = static_cast<uint32_t>(s);
}

// Volume sweep, psx-spx "SPU Volume and ADSR Generator" rule, one envelope update per `cycles` samples.
void Spu2StubMixer::run_volume(Volume& vol, uint64_t dt) {
    if (!(vol.reg & 0x8000)) return;
    const bool expo = vol.reg & 0x4000, dec = vol.reg & 0x2000;
    const int rate = vol.reg & 0x7F, shift = rate >> 2;
    const int base_step = dec ? -8 + (rate & 3) : 7 - (rate & 3);
    for (uint64_t k = 0; k < dt; ++k) {
        if (dec ? vol.level <= 0 : vol.level >= 0x7FFF) break;
        uint32_t cycles = 1u << std::max(0, shift - 11);
        int32_t step = base_step * (1 << std::max(0, 11 - shift));
        if (expo && !dec && vol.level > 0x6000) cycles *= 4;
        if (expo && dec) step = static_cast<int32_t>((static_cast<int64_t>(step) * vol.level) >> 15);
        if (++vol.cnt < cycles) continue;
        vol.cnt = 0;
        vol.level = std::clamp(vol.level + step, 0, 0x7FFF);
    }
}

int16_t Spu2StubMixer::volx(const Volume& vol) {
    return static_cast<int16_t>(vol.neg ? -vol.level : vol.level);
}

void Spu2StubMixer::set_param(uint64_t t, uint16_t entry, uint16_t value) {
    step_to(t);
    ++writes_;
    const uint16_t p = entry & 0xFF00;
    if (p <= sd::VP_VOLXR && !(entry & 0x80)) {
        Voice& v = v_[sd::hw_voice(entry)];
        switch (p) {
        case sd::VP_VOLL:
        case sd::VP_VOLR: {
            Volume& vol = v.vol[p == sd::VP_VOLR];
            vol.reg = value;
            if (value & 0x8000) {   // sweep: from the current level, phase from bit 12
                vol.neg = value & 0x1000;
                vol.cnt = 0;
            } else {                // fixed: 15-bit two's complement, x2
                const int32_t s = static_cast<int32_t>((value & 0x7FFF) ^ 0x4000) - 0x4000;
                vol.neg = s < 0;
                vol.level = std::min(0x7FFF, (s < 0 ? -s : s) * 2);
            }
            return;
        }
        case sd::VP_PITCH: v.pitch = value; return;
        case sd::VP_ADSR1: v.adsr1 = value; return;
        case sd::VP_ADSR2: v.adsr2 = value; return;
        default: break;   // ENVX / VOLX are read-only on the hardware
        }
    }
    regs_[entry] = value;
}

void Spu2StubMixer::set_addr(uint64_t t, uint16_t entry, uint32_t addr) {
    step_to(t);
    ++writes_;
    const uint16_t p = entry & 0xFFC0;
    if ((entry & 0xFF00) == sd::A_IRQA) irqa_[entry & 1] = addr & (kRamBytes - 16);   // libsd floors to 16 bytes
    if (p == sd::VA_SSA || p == sd::VA_LSAX || p == sd::VA_NAX) {
        Voice& v = v_[sd::hw_voice(entry)];
        if (p == sd::VA_SSA) v.ssa = addr & (kRamBytes - 1);
        else if (p == sd::VA_LSAX) v.lsax = addr & (kRamBytes - 1);
        return;
    }
    regs_[entry] = addr;
}

void Spu2StubMixer::set_switch(uint64_t t, uint16_t entry, uint32_t value) {
    step_to(t);
    ++writes_;
    const uint16_t p = entry & 0xFF00;
    const int core = entry & 1;
    if (p == sd::S_KON || p == sd::S_KOFF) {
        for (int n = 0; n < 24; ++n) {
            if (!(value & (1u << n))) continue;
            Voice& v = v_[core * 24 + n];
            if (p == sd::S_KON) {
                v.block = v.ssa & (kRamBytes - 16);
                v.sample = 0;
                v.frac = 0;
                v.envx = 0x7FFF;
                v.keyed = true;
                ++v.key_ons;
                begin_block(v);
            } else {
                v.envx = 0;
                v.keyed = false;
            }
        }
        regs_[entry] = value;   // last written mask, for inspection
        return;
    }
    regs_[entry] = value & 0xFFFFFF;
}

void Spu2StubMixer::set_core_attr(uint64_t t, uint16_t entry, uint16_t value) {
    step_to(t);
    ++writes_;
    regs_[static_cast<uint16_t>(0x8000 | entry)] = value;   // core attributes in their own key range
    if ((entry & 0x3E) == sd::C_IRQ_ENABLE) irq_armed_[entry & 1] = value != 0;
}

void Spu2StubMixer::set_effect_attr(uint64_t t, int core, const Spu2EffectAttr& a) {
    step_to(t);
    ++writes_;
    fx_[core & 1] = a;
    fx_[core & 1].core = core;
}

void Spu2StubMixer::voice_trans(uint64_t t, int, uint32_t spu_addr, const uint8_t* src, uint32_t n) {
    step_to(t);
    ++writes_;
    for (uint32_t k = 0; k < n; ++k) ram_[(spu_addr + k) & (kRamBytes - 1)] = src ? src[k] : 0;
}

uint16_t Spu2StubMixer::get_param(uint64_t t, uint16_t entry) {
    step_to(t);
    const uint16_t p = entry & 0xFF00;
    if (p <= sd::VP_VOLXR && !(entry & 0x80)) {
        const Voice& v = v_[sd::hw_voice(entry)];
        switch (p) {
        case sd::VP_ENVX: return v.envx;
        case sd::VP_VOLXL: return static_cast<uint16_t>(volx(v.vol[0]));
        case sd::VP_VOLXR: return static_cast<uint16_t>(volx(v.vol[1]));
        case sd::VP_VOLL: return v.vol[0].reg;
        case sd::VP_VOLR: return v.vol[1].reg;
        case sd::VP_PITCH: return v.pitch;
        case sd::VP_ADSR1: return v.adsr1;
        case sd::VP_ADSR2: return v.adsr2;
        default: break;
        }
    }
    const auto it = regs_.find(entry);
    return it == regs_.end() ? 0 : static_cast<uint16_t>(it->second);
}

uint32_t Spu2StubMixer::get_addr(uint64_t t, uint16_t entry) {
    step_to(t);
    const uint16_t p = entry & 0xFFC0;
    if (p == sd::VA_SSA || p == sd::VA_LSAX || p == sd::VA_NAX) {
        const Voice& v = v_[sd::hw_voice(entry)];
        if (p == sd::VA_SSA) return v.ssa;
        if (p == sd::VA_LSAX) return v.lsax;
        return v.block + 2 + 2 * (v.sample / 4);
    }
    const auto it = regs_.find(entry);
    return it == regs_.end() ? 0 : it->second;
}

uint32_t Spu2StubMixer::get_switch(uint64_t t, uint16_t entry) {
    step_to(t);
    const auto it = regs_.find(entry);
    return it == regs_.end() ? 0 : it->second;
}

}  // namespace b3audio
