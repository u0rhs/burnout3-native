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
    // Opened as SDL's default device, so SDL3 itself follows a change of the system's default (headphones unplugged,
    // Bluetooth switching) without a new open.
    bool open();
    void close();
    bool is_open() const { return stream_ != nullptr; }
    // robust2: the SDL audio device id the stream plays on (0: none): the logical device (SDL's default playback)
    uint32_t device_id() const;
    // robust2: an SDL_EVENT_AUDIO_DEVICE_REMOVED (playback) of device `which`: false when the stream can no longer play
    // (its logical device, or the physical device under it, is gone and SDL did not move it to another). Any removal is
    // checked, whatever its id: the logical device still known to SDL and its physical device (found by name among
    // the playback devices) still listed.
    bool still_playing_after_removal(uint32_t which);

    // Queues frames (L, R interleaved). Frames pushed while closed are counted and dropped.
    void push(const int16_t* lr, size_t frames);
    // Frames queued and not yet played.
    size_t queued_frames() const;

    // Drift guard for a host-paced loop: keeps the queue at most target + band frames by dropping (too much queued: a
    // fast host or a burst), and pads silence up to the target when the device ran the queue dry (an underrun: a slow
    // host, a hitch, a breakpoint), in whole 800-frame frames; counts both. Call once per pushed frame, after push().
    // robust2 (review): the target is at least the device's own block (SDL_GetAudioDeviceFormat's sample frames, at
    // 48 kHz) plus one frame, so a device that pulls large blocks always finds one; and the cushion pads only when the
    // queue was empty before this push (the device was starving), never because it is merely low between two large
    // pulls (that would put gaps into continuous sound). The first pad is logged.
    void regulate(size_t target = 2880, size_t band = 4800);   // 60 ms target, +100 ms band
    // The rules, without SDL (tests/test_audio_device.cpp): the target used with a device block of `block` frames, and
    // the silence to pad when `queued_before_push` frames were queued before the last push and `queued` are now
    static size_t effective_target(size_t target, size_t block) { return target > block + 800 ? target : block + 800; }
    static size_t pad_frames(size_t queued_before_push, size_t queued, size_t target) {
        if (queued_before_push != 0) return 0;
        size_t n = 0;
        for (size_t have = queued; have < target; have += 800) n += 800;
        return n;
    }
    // The device's block in 48 kHz frames (0: unknown)
    size_t block_frames() const { return block_; }

    uint64_t pushed() const { return pushed_; }
    uint64_t dropped() const { return dropped_; }
    uint64_t padded() const { return padded_; }

private:
    bool told_ = false;        // "no playback device" logged once (retries are quiet)
    uint32_t physical_ = 0;    // the physical playback device under the stream (0: not found)
    size_t block_ = 0;         // the device's block, 48 kHz frames (0: unknown)
    size_t before_push_ = 0;   // the queue before the last push (0: ran dry)
    bool told_pad_ = false;    // the first pad logged
    uint32_t find_physical() const;
    void* stream_ = nullptr;   // SDL_AudioStream*
    uint64_t pushed_ = 0, dropped_ = 0, padded_ = 0;
};

// robust2: when to try opening the device again after there was none (or it was lost): every `period_ns`, and at once
// after a device appeared (kick: SDL_EVENT_AUDIO_DEVICE_ADDED). No SDL; tests/test_audio_device.cpp.
struct ReopenSchedule {
    uint64_t period_ns = 3000000000ull;   // 3 s
    uint64_t next_ns = 0;                 // 0: now
    bool wanted = false;                  // a device is wanted and not open
    void lost(uint64_t now_ns) { wanted = true; next_ns = now_ns + period_ns; }   // a failed open / the device gone
    void kick() { next_ns = 0; }                                                   // a device appeared: try at once
    void opened() { wanted = false; }
    bool due(uint64_t now_ns) const { return wanted && now_ns >= next_ns; }
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
