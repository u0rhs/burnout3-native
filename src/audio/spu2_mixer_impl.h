// The SPU2-equivalent mixer behind Spu2Mixer (lane audio1; docs/audio.md §1 / §3.1): 2 cores x 24 voices, rendered
// sample by sample at 48 kHz in the executor's virtual time, s16 stereo out. Host C++, written from the hardware
// documents; PCSX2 was read for comparison only (no code taken). Sources, cited per rule:
//   [SPU2]  Sony "SPU2 Overview Manual" 6.0 (SDK 3.0.3 Docs&Training/HardwareManuals): voice / envelope / volume
//           registers, output and MEMIN areas, mixing, effect area, interrupts.
//   [spx]   psx-spx (psx-spx.consoledev.net "Sound Processing Unit (SPU)", fetched 2026-11-06): ADPCM decoding and
//           flags, the pitch counter, the Gaussian table, the envelope step rule, noise, the reverb formula and its
//           resampling FIR. The SPU2's voice core is the PSX SPU's (SPU2 Overview 1.3 / 2.1 match it field by field).
//   [libsd] the retail LIBSD.IRX ("PsIIlibsd 2800", game/IOP): sceSdSetAddr floors addresses to 16 bytes (0x378C),
//           sceSdSetEffectAttr (0xC2C) and its preset table (.data 0x4A68 / 0x4A90).
//   [L] / [G] mark choices where the documents are silent or disagree; the list is in spu2_mixer_impl.cpp.
//
// Model, per sample s (ring position s & 511):
//   - voices in order: ADPCM decoded one halfword (4 samples) at a time ahead of the pitch counter, NAX = the next
//     halfword to decode; Gaussian 4-tap interpolation; ADSR; VOLL / VOLR with sweeps; VMIX switches into dry / wet;
//   - per core: MEMIN (BVOL), core 0's output into core 1 (AVOL), MMIX, the reverb (24 kHz, ESA..EEA work area) with
//     EVOL, MVOL with sweeps; core 1 is the output;
//   - the output / MEMIN rings are written / read at the ring position; IRQA is checked against ring accesses, voice
//     ADPCM fetches, reverb accesses (effect enabled) and transfers.
#ifndef B3_AUDIO_SPU2_MIXER_IMPL_H
#define B3_AUDIO_SPU2_MIXER_IMPL_H

#include "spu2_mixer.h"

#include <array>
#include <cstddef>
#include <map>
#include <vector>

namespace b3audio {

// mac5 diagnostics (B3_SPU_STALE_LOG): a streamed wave's sound-RAM ring (RWA op 0x46: base, target length) -- the
// stale-replay report is limited to these rings (sound-effect caches replay their samples legitimately)
void spu2_mark_stream_ring(uint32_t base, uint32_t len);


// libsd's reverb presets, read from the game's own IOP/LIBSD.IRX (Sony data: never in the repo). Checked by an FNV-1a
// 64 of the bytes used; false if the file is missing or differs. Until loaded, every reverb mode keeps a silent wet
// path (one warning). The standalone loads it at start-up from the disc; tests skip their reverb checks without it.
bool load_libsd_reverb_presets(const char* libsd_irx_path);
bool load_libsd_reverb_presets_bytes(const std::vector<uint8_t>& libsd_irx);   // iso1: the file's bytes
bool libsd_reverb_presets_loaded();


class Spu2MixerImpl final : public Spu2Mixer {
public:
    static constexpr uint32_t kRamBytes = 0x200000;
    // Decoded samples kept ahead of the pitch counter: a halfword is decoded when at most this many are queued
    // beyond the 4-sample window's oldest [L: NAX observed at 2-byte granularity in the B0 log; the lead matches the
    // recorded NAX].
    static constexpr uint32_t kDecodeAhead = 12;

    explicit Spu2MixerImpl(size_t output_frames = 1u << 16);   // output ring capacity (oldest frames dropped)

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
    uint64_t run(uint64_t t, uint32_t* irq_cores) override;
    size_t read_output(int16_t* lr, size_t max_frames) override;

    // ---- building blocks (public for the unit tests) ----
    // One envelope / sweep generator step per sample [spx "Envelope Operation"]: rate = shift << 2 | step index.
    static void env_step(int32_t& level, uint32_t& cnt, int rate, bool dec, bool expo);
    struct Adsr {
        enum Phase : uint8_t { Attack, Decay, Sustain, Release, Off };
        uint16_t adsr1 = 0, adsr2 = 0;
        Phase phase = Off;
        int32_t level = 0;
        uint32_t cnt = 0;
        void key_on() { phase = Attack; level = 0; cnt = 0; }
        void key_off() { if (phase != Off) { phase = Release; cnt = 0; } }
        void mute() { phase = Off; level = 0; cnt = 0; }
        void tick();
    };
    struct Volume {   // VOLL / VOLR / MVOL: register and the current signed level
        uint16_t reg = 0;
        int32_t level = 0;
        uint32_t cnt = 0;
        void write(uint16_t v);
        void tick();
        bool sweeping() const { return (reg & 0x8000) != 0; }
    };
    // PS-ADPCM [spx]: decode the 4 samples of data halfword `hw` (0..6) of a 16-byte block.
    static void decode_halfword(const uint8_t* block, int hw, int32_t& h1, int32_t& h2, int16_t out[4]);
    static void decode_block(const uint8_t* block, int32_t& h1, int32_t& h2, int16_t out[28]);
    static const int16_t* gauss_table();   // 512 entries [spx]
    static int32_t gauss(uint32_t i, int32_t oldest, int32_t older, int32_t old, int32_t newest);
    static const int16_t* reverb_fir();    // 39 taps [spx]
    // audioq: the host's final output from the unclamped mix (Core::host_l / host_r) through a peak limiter instead of
    // the hardware's clamp (a race's loud moments sum far past full scale, which the clamp held at 32767 for up to a
    // millisecond, heard as crackle): the output is kLookahead + 1 frames (1 ms) behind the mix, and both channels' gain
    // ramps down within that time to keep the louder at kSoftKnee when its peak comes (no flat top, not even the first
    // crest's), then comes back over kReleaseSeconds. Never over the knee it is the identity: the hardware's values,
    // 1 ms later. soft_limit is the last safety (the identity up to the knee, a tanh curve above). With
    // B3_AUDIO_HARD_CLIP=1 (or set_hard_clip) the output is the hardware's clamped value, as before, with no delay.
    static constexpr int32_t kSoftKnee = 29000;
    static constexpr double kReleaseSeconds = 0.08;
    static int16_t soft_limit(int64_t v);
    struct Limiter {
        static constexpr int kLookahead = 48;
        int64_t ring[kLookahead + 1][2] = {};
        double need[kLookahead + 1] = {};
        int pos = 0;
        double g = 1.0;
        Limiter() { for (double& n : need) n = 1.0; }
        // one frame of the mix in; the frame kLookahead + 1 earlier out, limited
        void step(int64_t l, int64_t r, int16_t& out_l, int16_t& out_r);
    };
    void set_hard_clip(bool on) {   // (B3_AUDIO_HARD_CLIP=1 keeps it on)
        const bool h = on || hard_env_;
        if (h != hard_clip_) limiter_ = Limiter{};   // audioq2: a switch starts the limiter afresh (no old frames played)
        hard_clip_ = h;
    }

    // ---- inspection ----
    uint64_t time() const { return t_; }
    uint64_t dropped_frames() const { return dropped_; }
    uint64_t irqs() const { return irq_count_; }
    const Adsr& envelope(int hw) const { return core_[hw / 24].v[hw % 24].env; }
    uint32_t nax(int hw) const { return core_[hw / 24].v[hw % 24].nax; }
    uint32_t lsax(int hw) const { return core_[hw / 24].v[hw % 24].lsax; }
    uint32_t endx(int core) const { return core_[core & 1].endx; }
    uint32_t esa(int core) const { return core_[core & 1].esa; }
    uint32_t eea(int core) const { return core_[core & 1].eea; }
    const std::array<uint16_t, 32>& reverb_regs(int core) const { return core_[core & 1].rv; }
    int32_t mvol_level(int core, int ch) const { return core_[core & 1].mvol[ch & 1].level; }

private:
    struct Voice {
        uint16_t pitch = 0;
        Adsr env;
        Volume vol[2];
        uint32_t ssa = 0, lsax = 0;
        bool lsax_written = false;   // LSAX written since key-on: the waveform's loop-start flags are ignored [SPU2 1.3.3]
        uint64_t kon_t = 0;
        bool keyed = false;          // ever keyed on (for the 4-Ts LSAX rule)
        uint32_t block = 0, nax = 2; // current block, next data halfword (byte addresses)
        int32_t h1 = 0, h2 = 0;      // ADPCM predictor history
        std::array<int16_t, 32> q{}; // decoded samples
        uint32_t rd = 0, wr = 0;     // q[rd..rd+3] = oldest..new
        uint32_t counter = 0;        // pitch counter, 12-bit fraction
        int32_t outx = 0;            // last output after the envelope (OUTX)
    };
    struct Core {
        std::array<Voice, 24> v{};
        uint32_t vmix[4] = {0, 0, 0, 0};   // VMIXL, VMIXR, VMIXEL, VMIXER = dry L, dry R, wet L, wet R
        uint32_t pmon = 0, non = 0, endx = 0, kon = 0, koff = 0;
        uint16_t mmix = 0;
        Volume mvol[2];
        int32_t evol[2] = {0, 0}, avol[2] = {0, 0}, bvol[2] = {0, 0};
        bool fx_enable = false, irq_enable = false, mute = false;
        uint16_t noise_clk = 0, spdif = 0;
        uint32_t irqa = 0, tsa = 0, esa = 0, eea = 0x1FFFF, rev_cur = 0;   // byte addresses; rev_cur in words
        std::array<uint16_t, 32> rv{};     // reverb registers, psx-spx order, address fields in 8-byte units
        Spu2EffectAttr fx{};
        // 24 kHz reverb: FIR histories at 48 kHz (input wet L/R, zero-stuffed output L/R).
        std::array<int16_t, 64> fin_l{}, fin_r{}, fout_l{}, fout_r{};
        // noise generator [spx "SPU Noise Generator"]
        int32_t noise_timer = 0;
        uint16_t noise_level = 0;
        int32_t out_l = 0, out_r = 0;      // this sample's core output (after MVOL)
        // audioq: the same without the clamps of the dry sums and the output (host only: the sound the player hears;
        // no game-visible state comes from it), for the soft limit of the final output (soft_limit)
        int64_t host_l = 0, host_r = 0;
    };

    void advance(uint64_t t);   // render samples up to t; IRQs on the way are latched into pending_
    void render_one();
    void voice_sample(Core& k, int vi, int32_t prev_outx, int32_t mix[4]);
    void enter_block(Voice& v, uint32_t addr);
    void decode_next(Core& k, int vi);
    static void noise_tick(Core& k);
    void reverb_step(Core& k, int32_t in_l, int32_t in_r, int32_t& out_l, int32_t& out_r);
    void touch(uint32_t byte_addr);   // an SPU2 access: IRQA match
    uint16_t rd16(uint32_t word) const { return static_cast<uint16_t>(ram_[(word * 2) & (kRamBytes - 1)] | ram_[(word * 2 + 1) & (kRamBytes - 1)] << 8); }
    void wr16(uint32_t word, int32_t v) {
        const uint32_t a = (word * 2) & (kRamBytes - 1);
        ram_[a] = static_cast<uint8_t>(v);
        ram_[a + 1] = static_cast<uint8_t>(v >> 8);
    }
    Voice& voice(uint16_t entry) { return core_[entry & 1].v[(entry >> 1) & 0x1F]; }
    void key_on(int c, uint32_t mask);
    void key_off(int c, uint32_t mask);

    std::vector<uint8_t> ram_;
    bool hard_clip_ = false;   // audioq (limit)
    bool hard_env_ = false;    // audioq: B3_AUDIO_HARD_CLIP=1
    Limiter limiter_;          // audioq
    // mac5 diagnostics (B3_SPU_STALE_LOG=1): a voice entering a 16-byte block it (or another voice) already played since
    // that block was last uploaded, in a region uploaded more than once (a stream's ring, not a sound bank): the music
    // looping over stale ring data. Per block: uploads, and the upload count at its last play (+1; 0 never played).
    bool stale_log_ = false;
    std::vector<uint32_t> wgen_, pgen_;
    uint64_t stale_count_ = 0, stale_last_t_ = 0;
    std::array<Core, 2> core_{};
    uint64_t t_ = 0;
    uint32_t pending_ = 0;   // cores whose IRQ fired and has not been reported by run() yet
    uint64_t irq_count_ = 0;
    std::map<uint16_t, uint32_t> regs_;   // anything not modelled, for read-back
    // output ring
    std::vector<int16_t> out_;
    size_t out_cap_, out_head_ = 0, out_size_ = 0;
    uint64_t dropped_ = 0;
};

}  // namespace b3audio

#endif
