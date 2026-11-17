// src/native/audio_device.h (audio1, 2026-11-06): the standalone's audio output, the 48 kHz s16 stereo the native
// SPU2-equivalent mixer renders (src/audio/spu2_mixer_impl.h, read_output) to an SDL3 playback stream. The platform's
// main loop (boot2) drives it: per virtual frame, b3audio::pump() advances the IOP executor by that frame's samples
// (from virtual EE time, never host time, so the content is deterministic) and pushes what the mixer rendered. Only
// the device's consumption is host-timed; the loop's frame pacer stays the master (docs/audio.md §3.2).
#ifndef B3_NATIVE_AUDIO_DEVICE_H
#define B3_NATIVE_AUDIO_DEVICE_H

#include <cstddef>
#include <cstdint>

namespace b3native {

class AudioDevice {
public:
    AudioDevice() = default;
    ~AudioDevice() { close(); }
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // The default playback device, S16 stereo 48 kHz. false: no device (the run continues silent; logged once).
    bool open();
    void close();
    bool is_open() const { return stream_ != nullptr; }

    // Queues frames (L, R interleaved). Frames pushed while closed are counted and dropped.
    void push(const int16_t* lr, size_t frames);
    // Frames queued and not yet played.
    size_t queued_frames() const;

    // Drift guard for a host-paced loop: keeps the queue within [target - band, target + band] frames by dropping
    // (too much queued: a fast host or a burst) or padding silence (an underrun: a slow host, a breakpoint), in whole
    // 800-frame frames; counts both. Call once per pushed frame.
    void regulate(size_t target = 2880, size_t band = 4800);   // 60 ms target, +-100 ms band

    uint64_t pushed() const { return pushed_; }
    uint64_t dropped() const { return dropped_; }
    uint64_t padded() const { return padded_; }

private:
    void* stream_ = nullptr;   // SDL_AudioStream*
    uint64_t pushed_ = 0, dropped_ = 0, padded_ = 0;
};

}  // namespace b3native

namespace b3native { class RwaIop; }
namespace b3audio {
class Spu2Mixer;
// One virtual frame of audio: advances the executor (and with it the mixer) by `samples` 48 kHz samples, then moves
// everything the mixer rendered to the device (or drops it when there is none). Returns the frames pushed.
size_t pump(b3native::RwaIop& iop, Spu2Mixer& mixer, b3native::AudioDevice* device, uint32_t samples);
}  // namespace b3audio

#endif
