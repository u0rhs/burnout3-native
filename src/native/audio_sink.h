// The native platform's audio output seam (boot2 with play1, 2026-11-07): the platform owns RW Audio's IOP executor
// and its SPU2 mixer and runs them on virtual IOP time (platform_rwa.cpp: 800 samples per virtual VBlank); a front
// end that plays sound (tools/native/native_play: an SDL AudioDevice with its own wall-clock buffering) gives a sink,
// and each VBlank's rendered frames are pushed there. Headless runs mix the same way and drop the output, so the
// game sees identical mixer state with or without a sink.
#ifndef B3_NATIVE_AUDIO_SINK_H
#define B3_NATIVE_AUDIO_SINK_H
#include <cstddef>
#include <cstdint>
namespace b3audio {
struct AudioSink {
    virtual ~AudioSink() = default;
    // interleaved stereo s16 (L, R), 48 kHz; called from the platform's VBlank hook with that VBlank's frames
    virtual void push(const std::int16_t* lr, std::size_t frames) = 0;
};
}  // namespace b3audio
#endif
