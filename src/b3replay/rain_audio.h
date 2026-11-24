// rain_audio.h -- weather (remaster-features §38): the rain's sound, made by the port (the game has none; no outside
// audio). Added to the game's mixed output (48 kHz s16 stereo), never to the game's mixer, so nothing the game sees
// changes. Three layers, each from the generator's own noise (a fixed seed: the same samples every run):
// - the bed: rain on everything around, white noise high-passed at ~300 Hz and low-passed at ~6 kHz (~1.2 kHz in a
//   tunnel: the rain outside, muffled), decorrelated left / right;
// - the drips: sparse short pings (2-4.5 kHz, ~6 ms), panned at random, ~18 a second in the open, none in a tunnel;
// - the hiss: the tyres on the wet road, a lower band (LP ~2.5 kHz) rising with the speed (none standing, full at 50 m/s).
// Every gain moves smoothly (~0.35 s), so the rain fades in and out and a tunnel's entry is not a click. Off (and
// faded out): the samples pass untouched. The game's samples are never changed: the rain is held so that the sum stays
// within the platform limiter's knee (Spu2MixerImpl::kSoftKnee, 29000: SOUND OUTPUT Clean's ceiling), and adds nothing
// where the game is already above it (Original's loudest moments), at any volume.
#ifndef B3REPLAY_RAIN_AUDIO_H
#define B3REPLAY_RAIN_AUDIO_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace b3replay {

struct RainAudioState {
    bool on = false;          // the race on screen is in the rain
    float speed = 0.0f;       // m/s, the followed car's
    float enclosed = 0.0f;    // 0 open .. 1 inside a tunnel
    float volume = 1.0f;      // 0..1, the effects' volume
};

class RainAudio {
public:
    static constexpr float kRate = 48000.0f;
    // Adds the rain to `lr` (`frames` stereo frames, interleaved) in place, saturating at the s16 range
    void mix(std::int16_t* lr, std::size_t frames, const RainAudioState& in) {
        RainAudioState s = in;   // (a non-finite input counts as 0)
        if (!std::isfinite(s.speed)) s.speed = 0.0f;
        if (!std::isfinite(s.enclosed)) s.enclosed = 0.0f;
        if (!std::isfinite(s.volume)) s.volume = 0.0f;
        const float vol = s.volume * kLevel;   // rainvol
        const float tgt_bed = s.on ? kBed * vol * (1.0f - 0.45f * clamp01(s.enclosed)) : 0.0f;
        const float tgt_drip = s.on ? kDrip * vol * (1.0f - clamp01(s.enclosed)) : 0.0f;
        const float tgt_hiss = s.on ? kHiss * vol * clamp01(s.speed / 50.0f) : 0.0f;
        const float tgt_lp = s.on ? 6000.0f + (1200.0f - 6000.0f) * clamp01(s.enclosed) : 6000.0f;
        if (!std::isfinite(bed_) || !std::isfinite(drip_) || !std::isfinite(hiss_) || !std::isfinite(lp_hz_) ||
            !std::isfinite(l2_[0]) || !std::isfinite(l2_[1]) || !std::isfinite(hs_[0]) || !std::isfinite(hs_[1]) || !std::isfinite(ping_env_))
            reset();   // (a non-finite value would stay in the filters for good)
        if (tgt_bed <= 0.0f && tgt_hiss <= 0.0f && bed_ < 1e-4f && hiss_ < 1e-4f && drip_ < 1e-4f) {
            bed_ = hiss_ = drip_ = 0.0f;
            return;   // silent: the game's samples as they are
        }
        const float a = 1.0f - std::exp(-1.0f / (0.35f * kRate));   // the gains' slew per sample
        const float hp = std::exp(-2.0f * 3.14159265f * 300.0f / kRate);
        const float lph = 1.0f - std::exp(-2.0f * 3.14159265f * 2500.0f / kRate);
        for (std::size_t i = 0; i < frames; ++i) {
            bed_ += (tgt_bed - bed_) * a;
            drip_ += (tgt_drip - drip_) * a;
            hiss_ += (tgt_hiss - hiss_) * a;
            lp_hz_ += (tgt_lp - lp_hz_) * a;
            const float lpb = 1.0f - std::exp(-2.0f * 3.14159265f * lp_hz_ / kRate);
            // a drip now and then: a short ping, panned
            if (drip_ > 1e-4f && rnd() < kDripRate / kRate) {
                ping_f_ = 2000.0f + 2500.0f * rnd();
                ping_amp_ = 0.4f + 0.6f * rnd();
                ping_pan_ = rnd();
                ping_ph_ = 0.0f;
                ping_env_ = 1.0f;
            }
            float ping = 0.0f;
            if (ping_env_ > 1e-3f) {
                ping_ph_ += 2.0f * 3.14159265f * ping_f_ / kRate;
                ping = std::sin(ping_ph_) * ping_env_ * ping_amp_;
                ping_env_ *= kPingDecay;
            }
            for (int c = 0; c < 2; ++c) {
                const float w = 2.0f * rnd() - 1.0f;
                // the bed: high-pass (one pole, DC-blocking form) then low-pass (two one-poles)
                const float h = w - x1_[c] + hp * y1_[c];
                x1_[c] = w;
                y1_[c] = h;
                l1_[c] += (h - l1_[c]) * lpb;
                l2_[c] += (l1_[c] - l2_[c]) * lpb;
                // the hiss: its own noise, one low-pass
                const float w2 = 2.0f * rnd() - 1.0f;
                hs_[c] += (w2 - hs_[c]) * lph;
                const float pan = c == 0 ? 1.0f - ping_pan_ : ping_pan_;
                float add = 32767.0f * (bed_ * l2_[c] * 2.2f + hiss_ * hs_[c] * 1.6f + drip_ * ping * pan);
                const float g = float(lr[2 * i + std::size_t(c)]);
                // the sum within the limiter's knee; nothing where the game is past it (its sample kept as it is)
                add = std::isfinite(add) && std::fabs(g) <= kKnee ? std::clamp(add, -kKnee - g, kKnee - g) : 0.0f;
                lr[2 * i + std::size_t(c)] = std::int16_t(std::lround(g + add));
            }
        }
    }
    float bed_gain() const { return bed_; }
    // rainvol (1.4.0, a tester: "the rain is loud"): the whole rain sound's level at RAIN VOLUME 100 % (bed, drops,
    // hiss alike), 0.5 = -6 dB under the first one; the slider and its default (100) unchanged
    static constexpr float kLevel = 0.5f;
    // rainfe (a tester: the rain kept playing in the menus after a rain race): the renderer sets the rain's state at
    // each race frame it draws (View::rain_audio_frame); a state not set again within kHoldNs is off (the front end, a
    // loading screen, results without the race: no race frame), so the rain fades out over its slew (~0.35 s). A second
    // (Fable): a render hitch does not dip the rain; the menus still go quiet promptly. A set time newer than now (the
    // renderer's and the game thread's reads of the clock crossing) is live.
    static constexpr std::uint64_t kHoldNs = 1000000000;   // 1 s
    // rainpause (Fable): the rain follows the game's own sound: on while it rains and the race's clock runs, off (faded)
    // while the pause menu is up (its PC SETTINGS included: the pause stays up under it) and in photo mode; results over
    // the race and the game's replay keep it (neither is a pause)
    static bool sounding(bool raining, bool clock_running, bool game_paused, bool photo_mode) {
        return raining && clock_running && !game_paused && !photo_mode;
    }
    static bool state_live(std::uint64_t now_ns, std::uint64_t set_ns) { return set_ns != 0 && (now_ns < set_ns || now_ns - set_ns <= kHoldNs); }
    void reset() { *this = RainAudio{}; }

private:
    static constexpr float kBed = 0.071f, kDrip = 0.062f, kHiss = 0.116f;   // of full scale (the bed ~ -30 dBFS RMS at EFFECTS 100 %)
    static constexpr float kDripRate = 18.0f;                             // a second
    static constexpr float kKnee = 29000.0f;                              // Spu2MixerImpl::kSoftKnee
    static constexpr float kPingDecay = 0.9965f;                          // ~6 ms to -20 dB
    static float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
    float rnd() {   // xorshift32, 0..1
        seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
        return float(seed_ >> 8) * (1.0f / 16777216.0f);
    }
    std::uint32_t seed_ = 0x9E3779B9u;
    float bed_ = 0, drip_ = 0, hiss_ = 0, lp_hz_ = 6000.0f;
    float x1_[2] = {0, 0}, y1_[2] = {0, 0}, l1_[2] = {0, 0}, l2_[2] = {0, 0}, hs_[2] = {0, 0};
    float ping_f_ = 3000.0f, ping_amp_ = 0, ping_pan_ = 0.5f, ping_ph_ = 0, ping_env_ = 0;
};

}  // namespace b3replay

#endif
