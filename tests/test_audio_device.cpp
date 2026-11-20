// tests/test_audio_device.cpp (audio1): src/native/audio_device.* on SDL3's dummy audio driver: open, push, the
// queue, the drift guard, and b3audio::pump() moving the real mixer's output. `make test_audio_device` (needs SDL3,
// like `make render`; not part of `make test`).
#include "test.h"

#include "../src/audio/spu2_mixer_impl.h"
#include "../src/native/audio_device.h"
#include "../src/native/rwa_iop.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
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
    for (int k = 0; k < 20; ++k) dev.push(frame.data(), 800);   // 19,200 frames: far over target + band
    dev.regulate(2880, 4800);
    CHECK(dev.dropped() > 0 && dev.queued_frames() <= 2880 + 4800 + 800);
    b3native::AudioDevice idle;
    CHECK(idle.open());
    idle.regulate(2880, 1000);   // empty: padded up to the target
    CHECK(idle.padded() >= 2880 - 800);
}

// robust2: the cushion pads after a hitch (it was dead code: q + 4800 < 2880 is never true); robust2 review: only when
// the device ran the queue dry, and around the device's own block, so a device pulling large blocks gets no gaps
TEST(underrun_cushion_rules) {
    using D = b3native::AudioDevice;
    CHECK(D::effective_target(2880, 0) == 2880);
    CHECK(D::effective_target(2880, 2048) == 2880);   // 2048 + 800 = 2848: the default target holds a block already
    CHECK(D::effective_target(2880, 4096) == 4896);   // a 4096-frame device: the cushion grows to a block and a frame
    for (const size_t block : {size_t(2048), size_t(4096)}) {
        const size_t t = D::effective_target(2880, block);
        CHECK(D::pad_frames(0, 800, t) >= t - 800 && D::pad_frames(0, 800, t) % 800 == 0);   // ran dry: padded
        // between two large pulls the queue is low but was never empty: nothing padded (no gap in the sound)
        for (size_t before = 1; before < block + 800; before += 97) CHECK(D::pad_frames(before, before + 800, t) == 0);
        CHECK(D::pad_frames(0, t, t) == 0);   // already at the cushion
    }
}

TEST(underrun_pads_with_the_device_block) {
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    for (const char* frames : {"2048", "4096"}) {
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frames);
        b3native::AudioDevice dev;
        CHECK(dev.open());
        const size_t block = dev.block_frames();
        std::printf("  dummy device, %s sample frames asked: block %zu (48 kHz frames)\n", frames, block);
        CHECK(block >= 2048);
        const size_t t = b3native::AudioDevice::effective_target(2880, block);
        dev.regulate();   // nothing queued yet (the start, or after a hitch): silence up to the cushion
        CHECK(dev.padded() >= t && dev.padded() <= t + 800);
        const uint64_t before = dev.padded();
        std::vector<int16_t> frame(800 * 2, 100);
        for (int k = 0; k < 3; ++k) {   // a healthy queue: nothing added
            dev.push(frame.data(), 800);
            dev.regulate();
        }
        CHECK(dev.padded() == before && dev.dropped() == 0);
        dev.close();
    }
    SDL_ResetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
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
