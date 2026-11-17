// A stub SPU2 behind the Spu2Mixer interface (lane audio1, build step 1 of docs/audio.md §3.1): enough of the
// hardware for the executor's read-backs, no sound.
//   - NAX: every voice runs all the time, as the hardware does (keyed off or not): pitch * samples / 4096 ADPCM
//     samples, 28 per 16-byte block. At the end of a block whose flag byte (+1) has bit 0 (end) it jumps to LSAX,
//     and with bit 1 (repeat) clear its envelope drops to 0 [L psx-spx "SPU ADPCM flags"]; a block with bit 2 (loop
//     start) sets LSAX when it begins. Read back as block + 2 + 2 * (sample in block / 4): the halfword after the
//     block header that holds the next sample [G; the B0 log's idle voices read 0x5002 / 0x5012, i.e. halfword
//     granularity inside the block].
//   - ENVX: 0x7FFF after key-on, 0 after key-off and when a non-repeating end block finishes (no ADSR curve).
//   - VOLX: fixed volumes as the register value (15-bit two's complement, x2); sweeps (0x8000 / 0xA000 | rate,
//     phase bit 0x1000, exponential bit 0x4000) stepped per sample with psx-spx's envelope rule (step 7 - r&3 /
//     -8 + r&3, shifted by 11 - r>>2 or slowed by 2^(r>>2 - 11)).
//   - Everything else (MVOL, EVOL, VMIX, reverb attributes, core attributes) is recorded and readable, not mixed.
//   - The IRQ (run()): only IRQAs in the fixed per-sample rings (sd::ring_irq_pos), as the real mixer has them, so
//     the executor's clock is the same with either mixer.
#ifndef B3_AUDIO_SPU2_STUB_MIXER_H
#define B3_AUDIO_SPU2_STUB_MIXER_H

#include "spu2_mixer.h"

#include <array>
#include <map>
#include <vector>

namespace b3audio {

class Spu2StubMixer final : public Spu2Mixer {
public:
    static constexpr uint32_t kRamBytes = 0x200000;
    Spu2StubMixer();

    void set_param(uint64_t t, uint16_t entry, uint16_t value) override;
    void set_addr(uint64_t t, uint16_t entry, uint32_t addr) override;
    void set_switch(uint64_t t, uint16_t entry, uint32_t value) override;
    void set_core_attr(uint64_t t, uint16_t entry, uint16_t value) override;
    void set_effect_attr(uint64_t t, int core, const Spu2EffectAttr& a) override;
    void voice_trans(uint64_t t, int channel, uint32_t spu_addr, const uint8_t* src, uint32_t n) override;
    uint16_t get_param(uint64_t t, uint16_t entry) override;
    uint32_t get_addr(uint64_t t, uint16_t entry) override;
    uint32_t get_switch(uint64_t t, uint16_t entry) override;
    const uint8_t* sound_ram() const override { return ram_.data(); }
    uint64_t run(uint64_t t, uint32_t* irq_cores) override;   // IRQs from the fixed rings only (sd::ring_irq_pos)

    // Inspection.
    struct Volume { uint16_t reg = 0; int32_t level = 0; bool neg = false; uint32_t cnt = 0; };
    struct Voice {
        uint16_t pitch = 0, adsr1 = 0, adsr2 = 0;
        uint32_t ssa = 0, lsax = 0, block = 0;   // block = current 16-byte block's byte address
        uint32_t sample = 0, frac = 0;           // sample within the block (0..27), 12-bit pitch fraction
        uint16_t envx = 0;
        bool keyed = false;
        Volume vol[2];
        uint64_t key_ons = 0, ends = 0;
    };
    const Voice& voice(int hw) const { return v_[hw]; }
    uint64_t time() const { return t_; }
    uint64_t writes() const { return writes_; }
    const std::map<uint16_t, uint32_t>& registers() const { return regs_; }
    const Spu2EffectAttr& effect(int core) const { return fx_[core & 1]; }

private:
    void step_to(uint64_t t);
    uint64_t first_irq(uint64_t from, uint64_t to, uint32_t* cores) const;   // first ring hit in [from, to)
    void run_voice(Voice& v, uint64_t dt);
    void run_volume(Volume& vol, uint64_t dt);
    void begin_block(Voice& v);
    static int16_t volx(const Volume& vol);

    std::vector<uint8_t> ram_;
    std::array<Voice, 48> v_{};
    std::map<uint16_t, uint32_t> regs_;   // last value of every non-voice entry (and voice entries not modelled)
    std::array<Spu2EffectAttr, 2> fx_{};
    uint64_t t_ = 0, writes_ = 0;
    uint32_t irqa_[2] = {0, 0};
    bool irq_armed_[2] = {false, false};
    uint32_t irq_pending_ = 0;   // cores that fired inside a set_* advance, reported by the next run()
};

}  // namespace b3audio

#endif
