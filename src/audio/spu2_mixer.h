// The SPU2-equivalent mixer's interface as the IOP executor sees it (lane audio1; docs/audio.md §3.1 "Mixer
// interface"): libsd-level writes, each stamped with its virtual sample time (48 kHz, monotonic per mixer), and the
// read-backs RWA.IRX makes (ENVX, VOLX, NAX, the VMIX switches). Entries are libsd's (SDK common/include/sdmacro.h):
// voice parameters `param | voice << 1 | core`, core parameters `param | core`. Sound RAM is byte addressed (libsd
// byte addresses, 2 MB). Two implementations: the stub (spu2_stub_mixer.h: positions and the clock, no sound) and
// the SPU2-equivalent mixer (spu2_mixer_impl.h: ADPCM, Gaussian, ADSR, sweeps, reverb, 48 kHz output).
#ifndef B3_AUDIO_SPU2_MIXER_H
#define B3_AUDIO_SPU2_MIXER_H

#include <cstddef>
#include <cstdint>

namespace b3audio {

// sdmacro.h entries [C SDK 3.0.3 common/include/sdmacro.h].
namespace sd {
constexpr uint16_t VP_VOLL = 0x0000, VP_VOLR = 0x0100, VP_PITCH = 0x0200, VP_ADSR1 = 0x0300, VP_ADSR2 = 0x0400,
                   VP_ENVX = 0x0500, VP_VOLXL = 0x0600, VP_VOLXR = 0x0700;
constexpr uint16_t P_MMIX = 0x0800, P_MVOLL = 0x0980, P_MVOLR = 0x0A80, P_EVOLL = 0x0B80, P_EVOLR = 0x0C80,
                   P_AVOLL = 0x0D80, P_AVOLR = 0x0E80, P_BVOLL = 0x0F80, P_BVOLR = 0x1080;
constexpr uint16_t S_PMON = 0x1300, S_NON = 0x1400, S_KON = 0x1500, S_KOFF = 0x1600, S_ENDX = 0x1700,
                   S_VMIXL = 0x1800, S_VMIXEL = 0x1900, S_VMIXR = 0x1A00, S_VMIXER = 0x1B00;
constexpr uint16_t A_ESA = 0x1C00, A_EEA = 0x1D00, A_TSA = 0x1E00, A_IRQA = 0x1F00;
constexpr uint16_t VA_SSA = 0x2040, VA_LSAX = 0x2140, VA_NAX = 0x2240;
constexpr uint16_t C_EFFECT_ENABLE = 1 << 1, C_IRQ_ENABLE = 2 << 1, C_MUTE_ENABLE = 3 << 1, C_NOISE_CLK = 4 << 1,
                   C_SPDIF_MODE = 5 << 1;
// libsd modes (mode = RWA ReverbType - 1).
constexpr int REV_MODE_OFF = 0;
inline constexpr uint16_t voice(int hw) { return static_cast<uint16_t>((hw >= 24 ? 1 : 0) | (hw % 24) << 1); }   // hardware voice 0..47
inline constexpr int hw_voice(uint16_t entry) { return (entry & 1) * 24 + ((entry >> 1) & 0x1F); }
// The fixed per-sample rings [C SPU2 Overview 2.2.1 / 2.3, word addresses 0x400-0x27FF]: the output areas (core 0
// voice 1 / 3 at 0x400 / 0x600, core 1 SIN L / R at 0x800 / 0xA00, voice 1 / 3 at 0xC00 / 0xE00, MEMOUT 0x1000-
// 0x1FFF) are written and the MEMIN areas (0x2000-0x27FF) read at ring position (sample & 511) every sample. An
// IRQA (byte address) inside them is hit at that position: returns it, or -1.
inline constexpr int ring_irq_pos(uint32_t irqa) {
    const uint32_t w = (irqa >> 1) & 0xFFFFF;
    return (w >= 0x400 && w < 0x2800) ? static_cast<int>(w & 0x1FF) : -1;
}
}  // namespace sd

struct Spu2EffectAttr {          // sceSdEffectAttr [L sdmacro.h / libsd.h]
    int32_t core = 0, mode = 0;
    int16_t depth_l = 0, depth_r = 0;
    int32_t delay = 0, feedback = 0;
};

class Spu2Mixer {
public:
    virtual ~Spu2Mixer() = default;
    // Writes, in the order RWA.IRX makes them; t = virtual sample time (never decreases).
    virtual void set_param(uint64_t t, uint16_t entry, uint16_t value) = 0;       // sceSdSetParam
    virtual void set_addr(uint64_t t, uint16_t entry, uint32_t addr) = 0;         // sceSdSetAddr (bytes)
    virtual void set_switch(uint64_t t, uint16_t entry, uint32_t value) = 0;      // sceSdSetSwitch (KON/KOFF/VMIX..)
    virtual void set_core_attr(uint64_t t, uint16_t entry, uint16_t value) = 0;   // sceSdSetCoreAttr
    virtual void set_effect_attr(uint64_t t, int core, const Spu2EffectAttr& a) = 0;   // sceSdSetEffectAttr
    virtual void voice_trans(uint64_t t, int channel, uint32_t spu_addr, const uint8_t* src, uint32_t n) = 0;   // write to sound RAM
    // Read-backs.
    virtual uint16_t get_param(uint64_t t, uint16_t entry) = 0;   // ENVX, VOLXL/R
    virtual uint32_t get_addr(uint64_t t, uint16_t entry) = 0;    // NAX (bytes)
    virtual uint32_t get_switch(uint64_t t, uint16_t entry) = 0;  // VMIX read-modify-write
    // Sound RAM (2 MB), for tests and the B trace.
    virtual const uint8_t* sound_ram() const = 0;

    // Time and the SPU2 IRQ (the executor's clock, docs/audio-irx.md §3.1). Runs the SPU2 from its current time up to
    // t, but stops right after the first sample in which a core's armed IRQA was accessed (IRQ_ENABLE set; any
    // core's access counts, SPU2 Overview 2.7): returns that time (the sample's index + 1) with the firing cores in
    // *irq_cores (bit c), or t with *irq_cores = 0. A core that fired stays disarmed until C_IRQ_ENABLE is written
    // 1 again (libsd's acknowledge; RWA's handler writes IRQA, then IRQ_ENABLE 0, 1). The set_* calls also advance
    // time; an IRQ raised while they do is reported by the next run() at once.
    virtual uint64_t run(uint64_t t, uint32_t* irq_cores) = 0;
    // The 48 kHz output (s16 L, R interleaved) rendered and not read yet; returns the frames copied (0 = none, e.g.
    // the stub).
    virtual size_t read_output(int16_t* lr, size_t max_frames) {
        (void)lr;
        (void)max_frames;
        return 0;
    }
};

}  // namespace b3audio

#endif
