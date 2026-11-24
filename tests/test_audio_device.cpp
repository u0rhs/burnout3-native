// tests/test_audio_device.cpp (audio1): src/native/audio_device.* on SDL3's dummy audio driver: open, push, the
// queue, the drift guard, and b3audio::pump() moving the real mixer's output. `make test_audio_device` (needs SDL3,
// like `make render`; not part of `make test`).
#include "test.h"

#include "../src/audio/spu2_mixer_impl.h"
#include "../src/native/audio_device.h"
#include "../src/native/rwa_iop.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>

namespace {
struct NullPlatform final : b3native::RwaIopPlatform {
    void read_ee(uint32_t, void* dst, uint32_t n) override { std::fill_n(static_cast<uint8_t*>(dst), n, 0); }
    void write_ee(uint32_t, const void*, uint32_t) override {}
    void reply(const uint32_t*) override {}
    int file_open(const char*) override { return 0; }
    int file_read(int, void*, uint32_t) override { return 0; }
    int file_seek(int, uint32_t) override { return -1; }
    void file_close(int) override {}
};
}  // namespace

TEST(open_push_queue_regulate) {
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    b3native::AudioDevice dev;
    CHECK(dev.open());
    std::vector<int16_t> frame(800 * 2, 100);
    for (int k = 0; k < 4; ++k) dev.push(frame.data(), 800);
    CHECK(dev.pushed() == 3200);
    CHECK(dev.queued_frames() <= 3200);
    b3native::AudioDevice idle;
    CHECK(idle.open());
    CHECK(idle.queued_frames() == 0 && idle.dropped() == 0);
}

// audioq: the target holds at least the device's block and a frame
TEST(target_and_device_block) {
    using D = b3native::AudioDevice;
    CHECK(D::effective_target(2880, 0) == 2880);
    CHECK(D::effective_target(2880, 2048) == 2880);   // 2048 + 800 = 2848: the default target holds a block already
    CHECK(D::effective_target(2880, 4096) == 4896);   // a 4096-frame device: the cushion grows to a block and a frame
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    for (const char* frames : {"2048", "4096"}) {
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frames);
        b3native::AudioDevice dev;
        CHECK(dev.open());
        const size_t block = dev.block_frames();
        std::printf("  dummy device, %s sample frames asked: block %zu (48 kHz frames)\n", frames, block);
        CHECK(block >= 2048 && dev.queue().target() == b3native::OutputQueue::target_for(D::effective_target(2880, block), block));
        dev.close();
    }
    SDL_ResetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
}

// ---- audioq: the output queue (src/native/audio_queue.h), no SDL ----
namespace {
// the game's side: 800 frames a VBlank of a 441 Hz sine (any phase-continuous tone)
struct Tone {
    double phase = 0;
    std::vector<int16_t> next(size_t n = 800, double amp = 12000) {
        std::vector<int16_t> v(2 * n);
        for (size_t k = 0; k < n; ++k) {
            const int16_t s = int16_t(amp * std::sin(phase));
            v[2 * k] = v[2 * k + 1] = s;
            phase += 2 * 3.14159265358979 * 441.0 / 48000.0;
        }
        return v;
    }
};
// the largest sample-to-sample step of a channel (a 441 Hz sine at 12000: about 692; a cut or a gap's edge: thousands)
int max_step(const std::vector<int16_t>& v) {
    int m = 0;
    for (size_t k = 2; k < v.size(); k += 2) m = std::max(m, std::abs(int(v[k]) - int(v[k - 2])));
    return m;
}
}  // namespace

// The steady state: the game's 47952 frames a second (800 at 59.94 Hz) into a 48 kHz device pulling 480 at a time,
// the device reading `ratio` times as fast: the ratio settles near 47952 / 48000, the queue near the target, and it
// never runs dry (the old guard padded 2400 frames of silence about once a minute here)
TEST(queue_rate_follows_the_game) {
    b3native::OutputQueue q(2880, 7200);
    Tone tone;
    std::vector<int16_t> out(480 * 2), all;
    double produce_at = 0, consume_at = 0, credit = 0;
    const double vblank = 1.0 / 59.94, block = 480.0 / 48000.0;
    size_t worst = 0;
    for (double t = 0; t < 180.0; t += 0.001) {   // 3 minutes, a 1 ms clock
        while (produce_at <= t) { const auto v = tone.next(); q.push(v.data(), 800); produce_at += vblank; }
        while (consume_at <= t) {
            credit += 480.0 * q.ratio();   // the stream reads ratio x 480 of the queue's frames for 480 device frames
            const size_t n = size_t(credit);
            credit -= double(n);
            out.resize(2 * n);
            q.pull(out.data(), n);
            if (t > 120.0) all.insert(all.end(), out.begin(), out.end());
            consume_at += block;
        }
        if (t > 30.0) worst = std::max(worst, q.level() > 2880 ? q.level() - 2880 : 2880 - q.level());
    }
    std::printf("  ratio after 3 min %.6f (47952 / 48000 = %.6f), underruns %llu, drops %llu, level off the target by up to %zu frames\n",
                q.ratio(), 47952.0 / 48000.0, static_cast<unsigned long long>(q.underruns()), static_cast<unsigned long long>(q.drops()), worst);
    CHECK(std::fabs(q.ratio() - 47952.0 / 48000.0) < 0.0003);
    CHECK(q.underruns() == 0 && q.drops() == 0 && q.dropped() == 0);
    CHECK(worst < 1600);
    CHECK(max_step(all) < 800);   // a continuous sine: no cut, no gap
}

// The game thread late (a 150 ms stall): the sound fades out over kFade frames, silence, then fades back in once the
// cushion is there; no step anywhere larger than the tone's own
TEST(queue_stall_fades_out_and_in) {
    b3native::OutputQueue q(2880, 7200);
    Tone tone;
    for (int k = 0; k < 4; ++k) { const auto v = tone.next(); q.push(v.data(), 800); }
    std::vector<int16_t> out(480 * 2), all;
    for (int k = 0; k < 40; ++k) {   // 400 ms of pulls, only 4 pushes before and a gap: starving
        q.pull(out.data(), 480);
        all.insert(all.end(), out.begin(), out.end());
        if (k == 30) for (int j = 0; j < 6; ++j) { const auto v = tone.next(); q.push(v.data(), 800); }   // the game is back
    }
    CHECK(q.underruns() == 1 && q.padded() > 0);
    std::printf("  stall: max step %d (the tone's own ~692), %llu frames of silence\n", max_step(all), static_cast<unsigned long long>(q.padded()));
    CHECK(max_step(all) < 800);
    // and it played again after the stall
    bool sound_after = false;
    for (size_t k = 2 * 480 * 35; k < all.size(); ++k) sound_after = sound_after || all[k] != 0;
    CHECK(sound_after);
}

// loadaudio: a dry run while the game is silent on purpose (a fast load's span and its scene wait: set_quiet) is counted
// apart from the game being late, its refill after the span included; the sound out is the same sample for sample
TEST(queue_quiet_span_counted_apart) {
    const auto run = [](bool quiet, std::vector<int16_t>& all, b3native::OutputQueue& q) {
        Tone tone;
        for (int k = 0; k < 4; ++k) { const auto v = tone.next(); q.push(v.data(), 800); }
        if (quiet) q.set_quiet(true);
        std::vector<int16_t> out(480 * 2);
        for (int k = 0; k < 60; ++k) {
            q.pull(out.data(), 480);
            all.insert(all.end(), out.begin(), out.end());
            if (k == 30) {   // the span's end: quiet off, the game's sound again
                if (quiet) q.set_quiet(false);
                for (int j = 0; j < 6; ++j) { const auto v = tone.next(); q.push(v.data(), 800); }
            }
        }
        // then a real stall: counted as late either way
        for (int k = 0; k < 20; ++k) { q.pull(out.data(), 480); all.insert(all.end(), out.begin(), out.end()); }
    };
    std::vector<int16_t> a, b;
    b3native::OutputQueue qa(2880, 7200), qb(2880, 7200);
    run(false, a, qa);
    run(true, b, qb);
    CHECK(a == b);
    CHECK(qa.underruns() == 2 && qa.quiet_underruns() == 0 && qa.quiet_padded() == 0);
    CHECK(qb.underruns() == 1 && qb.quiet_underruns() == 1);
    CHECK(qb.padded() + qb.quiet_padded() == qa.padded() && qb.quiet_padded() > 0 && qb.padded() > 0);
}

// A burst (the pacer catching up): above target + band the oldest frames go down to the target, crossfaded
TEST(queue_burst_is_crossfaded) {
    b3native::OutputQueue q(2880, 7200);
    Tone tone;
    std::vector<int16_t> out(480 * 2), all;
    for (int k = 0; k < 5; ++k) { const auto v = tone.next(); q.push(v.data(), 800); }
    for (int k = 0; k < 4; ++k) { q.pull(out.data(), 480); all.insert(all.end(), out.begin(), out.end()); }   // playing
    for (int k = 0; k < 14; ++k) { const auto v = tone.next(); q.push(v.data(), 800); }   // 11200 frames at once
    CHECK(q.drops() >= 1 && q.dropped() > 0 && q.level() <= 2880 + 7200);
    for (int k = 0; k < 6; ++k) { q.pull(out.data(), 480); all.insert(all.end(), out.begin(), out.end()); }
    std::printf("  burst: %llu frames dropped, max step %d\n", static_cast<unsigned long long>(q.dropped()), max_step(all));
    CHECK(max_step(all) < 1400);   // (the crossfade between two phases of the tone: smooth, no cut)
}

// review: a pull when the queue holds just a little more than the device asks for (n + 1 .. n + kFade - 1): the whole
// block, no starve; then a pull of more than there is: what there is, faded, silence after, nothing written past the
// block (the counters right)
TEST(queue_pull_at_the_edge) {
    using Q = b3native::OutputQueue;
    for (const size_t extra : {size_t(1), Q::kFade - 1}) {
        const size_t n = 480;
        Q q(n + extra, 100000);
        Tone tone;
        while (q.level() < n + extra) { const auto v = tone.next(1); q.push(v.data(), 1); }
        std::vector<int16_t> out(2 * (n + 64), int16_t(0x5A5A));   // guard frames after the block
        q.pull(out.data(), n);
        CHECK(q.underruns() == 0 && q.level() == extra);
        q.pull(out.data(), n);   // less than asked: starving
        CHECK(q.underruns() == 1 && q.padded() == n - extra && q.level() == 0);
        bool guard = true;
        for (size_t k = 2 * n; k < out.size(); ++k) guard = guard && out[k] == int16_t(0x5A5A);
        CHECK(guard);   // nothing past the block
        bool silent_after = true;
        for (size_t k = 2 * extra; k < 2 * n; ++k) silent_after = silent_after && out[k] == 0;
        CHECK(silent_after);
    }
}

// review (robust2's 1.3.0 case): devices pulling large blocks (2048 / 4096 / 8192 frames a pull) with the target
// for their block: no starve, one VBlank 30 ms late included
TEST(queue_large_device_blocks) {
    for (const size_t block : {size_t(2048), size_t(4096), size_t(8192)}) {
        b3native::OutputQueue q(b3native::OutputQueue::target_for(2880, block), 7200);
        Tone tone;
        std::vector<int16_t> out;
        double produce_at = 0, consume_at = 0, credit = 0;
        int vb = 0;
        for (double t = 0; t < 60.0; t += 0.0005) {
            while (produce_at <= t) {
                const auto v = tone.next();
                q.push(v.data(), 800);
                produce_at += 1.0 / 59.94;
                if (++vb == 1800) produce_at += 0.030;   // one VBlank 30 ms late (30 s in)
            }
            while (consume_at <= t) {
                credit += double(block) * q.ratio();
                const size_t n = size_t(credit);
                credit -= double(n);
                out.resize(2 * n);
                q.pull(out.data(), n);
                consume_at += double(block) / 48000.0;
            }
        }
        std::printf("  %zu-frame device: target %zu, underruns %llu, ratio %.5f\n", block, q.target(), static_cast<unsigned long long>(q.underruns()), q.ratio());
        CHECK(q.underruns() == 0);
    }
}

// audioq2 (review): the game stops (no push) while the queue holds a little more than a block: the block that ends
// the sound fades over the full kFade frames (not a cut a few frames into the next pull); a game on time never does
TEST(queue_late_block_end_fades) {
    using Q = b3native::OutputQueue;
    Q q(2000, 7200);
    Tone tone;
    for (int k = 0; k < 3; ++k) { const auto v = tone.next(); q.push(v.data(), 800); }
    { const auto v = tone.next(120); q.push(v.data(), 120); }   // 2520 queued: after 4 pulls 600 left (480 .. 480 + kFade)
    std::vector<int16_t> out(480 * 2), all;
    for (int k = 0; k < 10; ++k) { q.pull(out.data(), 480); all.insert(all.end(), out.begin(), out.end()); }
    CHECK(q.underruns() == 1 && q.dropped() == 120);   // the 5th pull ended the sound (late): its 120 leftover frames dropped
    // the sound's end: the last nonzero frame, and the fade before it over kFade frames (each step no larger than the tone's)
    size_t last = 0;
    for (size_t k = 0; k < all.size() / 2; ++k) if (all[2 * k] != 0) last = k;
    std::printf("  late block end: the sound ends at frame %zu, max step %d\n", last, max_step(all));
    CHECK(max_step(all) < 800);
    double peak_in_fade = 0;
    for (size_t k = last + 1 - Q::kFade / 4; k <= last; ++k) peak_in_fade = std::max(peak_in_fade, std::fabs(double(all[2 * k])));
    CHECK(peak_in_fade < 12000.0 * 0.3);   // the last quarter of the fade is below 30 % of the tone
}

// robust2: no device at the start (or lost) is tried again, not latched silent until a restart
TEST(reopen_schedule) {
    b3native::ReopenSchedule r;
    CHECK(!r.due(0));                 // nothing wanted
    r.lost(1000);
    CHECK(!r.due(1000) && !r.due(1000 + r.period_ns - 1) && r.due(1000 + r.period_ns));
    r.lost(5000);
    r.kick();                         // a device was added: at once
    CHECK(r.due(5000));
    r.opened();
    CHECK(!r.due(1ull << 62));
}

// robust2 review: a removal event of another device leaves the stream playing; its own device's removal does not
TEST(removal_events) {
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    b3native::AudioDevice dev;
    CHECK(dev.open());
    CHECK(dev.device_id() != 0);
    CHECK(dev.still_playing_after_removal(0x7FFF0001u));          // some other device: still there
    CHECK(!dev.still_playing_after_removal(dev.device_id()));     // the stream's own logical device
    dev.close();
    CHECK(!dev.still_playing_after_removal(1));                   // closed: nothing plays
}

TEST(pump_moves_the_mixer_output) {
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    NullPlatform pf;
    b3audio::Spu2MixerImpl mixer;
    b3native::RwaIop iop(pf, mixer, b3native::RwaIopOptions{});
    b3native::AudioDevice dev;
    CHECK(dev.open());
    size_t total = 0;
    for (int f = 0; f < 10; ++f) total += b3audio::pump(iop, mixer, &dev, 800);
    CHECK(total == 8000 && dev.pushed() == 8000);   // the mixer renders every sample, silence included
    CHECK(b3audio::pump(iop, mixer, nullptr, 800) == 800);   // no device: rendered and dropped
}

TEST_MAIN("audio_device (SDL3 output of the native mixer; dummy driver)")
