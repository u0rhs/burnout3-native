// nativeaudio.h -- the native game's sound out of b3render (play1, native_play). The platform mixes RW Audio on virtual
// IOP time and pushes each VBlank's 800 frames (48 kHz s16 stereo) to native_play's AudioSink (src/native/audio_sink.h),
// which hands them to b3r_audio_push (src/b3replay/inproc.cpp): only bytes cross, and the one SDL in the process (the
// renderer's) plays them through audio1's AudioDevice (src/native/audio_device.h, with its drift guard).
//
// Threads: the window thread opens the device when the live window starts (--native-audio; SDL's own thread); the game
// thread pushes. Frames pushed before the device is open, or with none (offscreen, no audio device), are counted and
// dropped: the mixer state the game sees is the same either way (the platform mixes regardless).
#ifndef B3REPLAY_NATIVEAUDIO_H
#define B3REPLAY_NATIVEAUDIO_H

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <cmath>
#include <chrono>
#include <mutex>
#include <vector>

#include "rain_audio.h"

#include "../native/audio_device.h"

namespace b3replay {

class NativeAudio {
public:
    static NativeAudio& instance();
    void open();    // window thread (--native-audio)
    void close();   // window thread, at the end
    // robust2: window thread, each loop: no device at the start (or it was lost) is tried again every 3 s, and at once
    // when SDL reports a playback device added; SDL_EVENT_AUDIO_DEVICE_REMOVED of the one in use closes it (silence,
    // then the retries). A change of the system's default device SDL3 follows by itself (audio_device.h).
    void poll(std::uint64_t now_ns);
    void event(const void* sdl_event);   // an SDL_Event*: the audio device events, anything else ignored
    void push(const std::int16_t* lr, std::size_t frames);   // game thread
    // stutter: the output queue's dry runs so far (the device played silence until it refilled; b3r_audio_late)
    std::uint64_t late() { std::lock_guard<std::mutex> l(mu_); return dev_.is_open() ? dev_.underruns() : 0; }
    // loadaudio: the game's sound silent on purpose (a fast load's muted span, its scene wait; b3r_audio_quiet): the
    // queue counts a dry run then apart from late() (the sound is the same)
    void set_quiet(bool on) { std::lock_guard<std::mutex> l(mu_); dev_.set_quiet(on); }
    std::uint64_t pushed() const { return pushed_; }
    std::uint64_t dropped_closed() const { return dropped_closed_; }
    // weather (§38): the rain's sound from the renderer (any thread): on (a race in the rain, running, not paused),
    // the followed car's speed (m/s), the tunnel's share (0..1), the volume (0..1)
    void set_rain(bool on, float speed, float enclosed, float volume) {
        auto fin = [](float v) { return std::isfinite(v) ? v : 0.0f; };   // (a non-finite input: 0)
        rain_on_.store(on, std::memory_order_relaxed);
        rain_speed_.store(fin(speed), std::memory_order_relaxed);
        rain_enclosed_.store(fin(enclosed), std::memory_order_relaxed);
        rain_volume_.store(fin(volume), std::memory_order_relaxed);
        rain_set_ns_.store(now_ns(), std::memory_order_relaxed);   // rainfe: kept only while race frames set it
    }
    // rainfe: RAIN VOLUME at once when the settings change (any page: the pause menu's PC SETTINGS too), not only from
    // the next race frame
    void set_rain_volume(float volume) { rain_volume_.store(std::isfinite(volume) ? volume : 0.0f, std::memory_order_relaxed); }
    static std::uint64_t now_ns() {
        return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    // the game's EFFECTS volume, 0..1 (native_play, b3r_effects_volume; 1 until it says): the rain follows it
    void set_effects_volume(float v) { effects_volume_.store(std::isfinite(v) ? v : 0.0f, std::memory_order_relaxed); }

private:
    std::mutex mu_;
    b3native::AudioDevice dev_;
    bool tried_ = false;
    b3native::ReopenSchedule reopen_;
    std::uint64_t pushed_ = 0, dropped_closed_ = 0;
    RainAudio rain_;                    // (game thread, under mu_)
    std::vector<std::int16_t> mix_;
    std::atomic<bool> rain_on_{false};
    std::atomic<float> rain_speed_{0.0f}, rain_enclosed_{0.0f}, rain_volume_{1.0f}, effects_volume_{1.0f};
    std::atomic<std::uint64_t> rain_set_ns_{0};   // rainfe: when the renderer last set the rain's state
};

}  // namespace b3replay

#endif
