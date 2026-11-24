// src/native/audio_queue.h (audioq, 2026-11-22): the sound's output queue between the game's mix and the playback
// device, without SDL (tests/test_audio_device.cpp; tools/native/audio_sim.cpp plays a captured mix through it).
//
// The game mixes on virtual time: 800 frames a VBlank, so 47952 frames a second at the paced 59.94 Hz, plus whatever
// the host's pacing adds or misses (a hitch, a catch-up, the window's focus hold). The device plays 48000 a second on
// its own clock. The queue between them is kept near `target` without touching the sound:
//   - rate: the device's stream is told to read the queue a fraction of a percent slower or faster (ratio(): the
//     frequency ratio of SDL's resampler, at most kMaxAdjust away from 1), from a P + I control of the queue's level
//     (smoothed), so the steady 0.1 % gap between 47952 and 48000 and the host clock's own drift never empty or fill
//     it;
//   - starving (the game thread late: a stall, a load; the queue holds less than the device asks for): the last frames
//     it has (up to kFade) fade out to silence, then silence until the queue holds `target` again, which comes in with
//     a kFade fade in. No cut, no click; the old guard's inserted 800-frame silences and their edges are gone;
//   - too much (a burst: the pacer catching up): above target + band the oldest frames are dropped down to the target
//     with a kFade crossfade from what was next into what now comes next. No 800-frame cut.
// The rate's reach: at most kMaxAdjust (0.5 %, under 9 cents: not heard). A game that runs slower than that for long
// (a PC that cannot hold 59.94 VBlanks a second: the game itself is then slow) still empties the queue from time to
// time: each time the sound fades out and back in (where the old guard padded hard-edged silences). A wider reach when
// the queue is low was weighed and left out: up to 3 % would be half a semitone of pitch drift, heard in the music,
// to hide a game that is already running slow.
// The content of the game's mix, and every log the game writes, are not changed: this is the host's output only.
#ifndef B3_NATIVE_AUDIO_QUEUE_H
#define B3_NATIVE_AUDIO_QUEUE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace b3native {

class OutputQueue {
public:
    static constexpr std::size_t kFade = 128;              // frames (2.7 ms): fades and crossfades
    static constexpr std::size_t kCapacity = 48000 * 2;    // frames (2 s): beyond it the oldest go at once
    static constexpr double kMaxAdjust = 0.005;            // the ratio at most 0.5 % off 1 (under 9 cents)
    static constexpr double kP = 0.1;                      // ratio change per second of level error
    static constexpr double kI = 0.01;                     // per second of error, per second
    static constexpr double kSlew = 0.00005;               // the ratio's change at most this per push (~0.3 % a second)
    static constexpr double kSmooth = 0.05;                // the level's smoothing per push (~20 pushes)
    static constexpr double kIntegrateWithin = 0.02;       // s: the integral moves only within 20 ms of the target
    static constexpr std::size_t kLateFrames = 1200;       // 25 ms of output without a push: the game is late (a VBlank is 16.7 ms)

    explicit OutputQueue(std::size_t target = 2880, std::size_t band = 7200) : target_(target), band_(band) { buf_.resize(2 * kCapacity); }

    void set_target(std::size_t target) { std::lock_guard<std::mutex> l(mu_); target_ = std::max<std::size_t>(target, kFade * 2); }
    std::size_t target() const { std::lock_guard<std::mutex> l(mu_); return target_; }
    // The target for a device pulling `block` frames at a time: at least `target`, and the block plus a fade and two
    // VBlanks (a pull always finds a block while the game is on time, one late VBlank included)
    static std::size_t target_for(std::size_t target, std::size_t block) { return std::max(target, block + kFade + 2 * 800); }

    // Game thread: `n` frames of the mix (L, R interleaved). Then: a drop down to the target when above target +
    // band (crossfaded), and the rate control's next ratio.
    void push(const std::int16_t* lr, std::size_t n) {
        std::lock_guard<std::mutex> l(mu_);
        pushed_ += n;
        since_push_ = 0;
        for (std::size_t k = 0; k < n; ++k) {
            if (size_ == kCapacity) {   // (2 s queued: the oldest frame goes)
                head_ = (head_ + 1) % kCapacity;
                --size_;
                ++dropped_;
            }
            const std::size_t i = (head_ + size_) % kCapacity;
            buf_[2 * i] = lr[2 * k];
            buf_[2 * i + 1] = lr[2 * k + 1];
            ++size_;
        }
        if (size_ > target_ + band_) drop_to(target_);
        control(n);
    }

    // Audio thread: exactly `n` frames into `out` (the device asks; silence where the queue has nothing to give).
    void pull(std::int16_t* out, std::size_t n) {
        std::lock_guard<std::mutex> l(mu_);
        if (!playing_) {
            if (size_ < target_) {   // filling (at the start, after starving): silence until the cushion is there
                std::fill(out, out + 2 * n, std::int16_t(0));
                if (started_) (starve_quiet_ ? quiet_padded_ : padded_) += n;
                return;
            }
            playing_ = true;
            fade_in_ = 0;   // the next kFade frames fade in
        }
        started_ = true;
        // audioq2 (review): the game late (no push for kLateFrames of output before this pull) and this block would
        // leave less than a fade: the starve begins at this block's end, so its last kFade frames fade (not a few
        // frames' cut at the next pull). A device pulling large blocks sees pushes between its pulls: never late here
        const bool late_end = size_ < n + kFade && since_push_ > kLateFrames;
        since_push_ += n;
        if (size_ >= n && !late_end) {   // the usual case (a device pulling large blocks may leave the queue low: not a starve)
            take(out, n);
            return;
        }
        // starving (less than the device asks for, or late at this block's end): what there is up to the block, its
        // last frames faded out to silence, then silence for the rest of the block, and silence until the cushion is back
        const std::size_t m = std::min(size_, n);
        take(out, m);
        if (size_ > 0) {   // (late at the block's end: the few frames left after it would come after the gap: dropped)
            dropped_ += size_;
            head_ = (head_ + size_) % kCapacity;
            size_ = 0;
        }
        const std::size_t f = std::min(kFade, m);
        for (std::size_t k = 0; k < f; ++k) {
            const double g = double(f - 1 - k) / double(f);   // to 0 at the last frame
            scale(out + 2 * (m - f + k), g);
        }
        std::fill(out + 2 * m, out + 2 * n, std::int16_t(0));
        // loadaudio: a starve while the game is silent on purpose (set_quiet) is that silence, not the game late
        starve_quiet_ = quiet_;
        (starve_quiet_ ? quiet_padded_ : padded_) += n - m;
        ++(starve_quiet_ ? quiet_underruns_ : underruns_);
        playing_ = false;
    }
    // loadaudio (game thread): the game's sound is silent on purpose from here (a fast load's or a hidden save screen's
    // muted span, its wait for the renderer's race scene included: tools/native/fast_load.h AudioGate) until
    // set_quiet(false) at the span's first VBlank with sound. A starve that begins meanwhile is counted apart
    // (quiet_underruns, quiet_padded; its refill after the span too): underruns() / padded() are the game late only.
    // The sound is the same either way.
    void set_quiet(bool on) { std::lock_guard<std::mutex> l(mu_); quiet_ = on; }

    std::size_t level() const { std::lock_guard<std::mutex> l(mu_); return size_; }
    double ratio() const { std::lock_guard<std::mutex> l(mu_); return ratio_; }
    bool playing() const { std::lock_guard<std::mutex> l(mu_); return playing_; }
    std::uint64_t pushed() const { std::lock_guard<std::mutex> l(mu_); return pushed_; }
    std::uint64_t dropped() const { std::lock_guard<std::mutex> l(mu_); return dropped_; }       // frames dropped (crossfaded, or past the capacity)
    std::uint64_t padded() const { std::lock_guard<std::mutex> l(mu_); return padded_; }         // frames of silence given while starving (after the start)
    std::uint64_t underruns() const { std::lock_guard<std::mutex> l(mu_); return underruns_; }   // times the queue ran dry while playing (the game late)
    std::uint64_t quiet_underruns() const { std::lock_guard<std::mutex> l(mu_); return quiet_underruns_; }   // loadaudio: ... in a silent span
    std::uint64_t quiet_padded() const { std::lock_guard<std::mutex> l(mu_); return quiet_padded_; }         // ... its frames of silence
    std::uint64_t drops() const { std::lock_guard<std::mutex> l(mu_); return drops_; }           // times frames were dropped down to the target
    void reset() {
        std::lock_guard<std::mutex> l(mu_);
        head_ = size_ = 0;
        since_push_ = 0;
        playing_ = started_ = false;
        starve_quiet_ = false;
        fade_in_ = kFade;
        ratio_ = 1.0;
        ema_ = -1.0;
        integ_ = 0.0;
    }

private:
    static void scale(std::int16_t* s, double g) {
        s[0] = std::int16_t(double(s[0]) * g);
        s[1] = std::int16_t(double(s[1]) * g);
    }
    // the oldest `n` frames into `out` (a pending fade in applied)
    void take(std::int16_t* out, std::size_t n) {
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t i = (head_ + k) % kCapacity;
            out[2 * k] = buf_[2 * i];
            out[2 * k + 1] = buf_[2 * i + 1];
            if (fade_in_ < kFade) {
                scale(out + 2 * k, double(fade_in_ + 1) / double(kFade + 1));
                ++fade_in_;
            }
        }
        head_ = (head_ + n) % kCapacity;
        size_ -= n;
    }
    // drop the oldest frames down to `keep`, crossfading from what was next into what now comes next
    void drop_to(std::size_t keep) {
        if (size_ <= keep) return;
        const std::size_t d = size_ - keep;
        if (d < kFade || !playing_) {   // (filling: nothing playing to cross from)
            head_ = (head_ + d) % kCapacity;
            size_ -= d;
            dropped_ += d;
            ++drops_;
            return;
        }
        const std::size_t f = std::min(kFade, keep);
        for (std::size_t k = 0; k < f; ++k) {
            const std::size_t a = (head_ + k) % kCapacity, b = (head_ + d + k) % kCapacity;
            const double w = double(k + 1) / double(f + 1);
            buf_[2 * b] = std::int16_t(double(buf_[2 * a]) * (1.0 - w) + double(buf_[2 * b]) * w);
            buf_[2 * b + 1] = std::int16_t(double(buf_[2 * a + 1]) * (1.0 - w) + double(buf_[2 * b + 1]) * w);
        }
        head_ = (head_ + d) % kCapacity;
        size_ -= d;
        dropped_ += d;
        ++drops_;
    }
    // the rate: the level (smoothed, right after a push) against the target, P + I, slew-limited
    void control(std::size_t n) {
        if (!playing_) return;
        ema_ = ema_ < 0.0 ? double(size_) : ema_ + (double(size_) - ema_) * kSmooth;
        const double err = (ema_ - double(target_)) / 48000.0;   // seconds; positive: too much queued, read faster
        const double dt = double(n) / 48000.0;
        // the integral learns the steady drift only near the target (a burst or a stall is the P term's: no wind-up)
        if (err > -kIntegrateWithin && err < kIntegrateWithin) integ_ = std::clamp(integ_ + kI * err * dt, -kMaxAdjust, kMaxAdjust);
        const double want = 1.0 + std::clamp(kP * err + integ_, -kMaxAdjust, kMaxAdjust);
        ratio_ += std::clamp(want - ratio_, -kSlew, kSlew);
    }

    mutable std::mutex mu_;
    std::vector<std::int16_t> buf_;
    std::size_t head_ = 0, size_ = 0;
    std::size_t target_, band_;
    bool playing_ = false, started_ = false;
    std::size_t fade_in_ = kFade;   // < kFade: fading in
    double ratio_ = 1.0, ema_ = -1.0, integ_ = 0.0;
    std::uint64_t pushed_ = 0, dropped_ = 0, padded_ = 0, underruns_ = 0, drops_ = 0;
    std::uint64_t quiet_padded_ = 0, quiet_underruns_ = 0;   // loadaudio
    bool quiet_ = false, starve_quiet_ = false;
    std::size_t since_push_ = 0;   // audioq2: frames pulled since the last push
};

}  // namespace b3native

#endif
