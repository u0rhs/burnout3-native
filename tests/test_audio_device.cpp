// tests/test_audio_device.cpp (audio1): src/native/audio_device.* on SDL3's dummy audio driver: open, push, the
// queue, the drift guard, and b3audio::pump() moving the real mixer's output. `make test_audio_device` (needs SDL3,
// like `make render`; not part of `make test`).
#include "test.h"

#include "../src/audio/spu2_mixer_impl.h"
#include "../src/native/audio_device.h"
#include "../src/native/rwa_iop.h"

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
