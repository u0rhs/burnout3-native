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
#include <mutex>

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
    std::uint64_t pushed() const { return pushed_; }
    std::uint64_t dropped_closed() const { return dropped_closed_; }

private:
    std::mutex mu_;
    b3native::AudioDevice dev_;
    bool tried_ = false;
    b3native::ReopenSchedule reopen_;
    std::uint64_t pushed_ = 0, dropped_closed_ = 0;
};

}  // namespace b3replay

#endif
