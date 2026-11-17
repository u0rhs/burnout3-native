// The native game's sound out of b3render (play1): src/b3replay/nativeaudio.h.
#include "nativeaudio.h"

#include <cstdio>

// audio1's device without its executor pump (that needs the IOP executor, which lives in the game, not in b3render)
#define B3_AUDIO_DEVICE_ONLY
#include "../native/audio_device.cpp"

namespace b3replay {

NativeAudio& NativeAudio::instance() {
    static NativeAudio a;
    return a;
}

void NativeAudio::open() {
    std::lock_guard<std::mutex> l(mu_);
    if (tried_) return;
    tried_ = true;
    if (dev_.open()) std::printf("[b3audio] sound: the default playback device, 48 kHz s16 stereo\n");
    else std::printf("[b3audio] no playback device: the game runs silent\n");
    std::fflush(stdout);
}

void NativeAudio::close() {
    std::lock_guard<std::mutex> l(mu_);
    if (!tried_) return;
    std::printf("[b3audio] %llu frames pushed (%llu before the device was open), %llu dropped / %llu padded by the drift guard\n",
                static_cast<unsigned long long>(pushed_), static_cast<unsigned long long>(dropped_closed_),
                static_cast<unsigned long long>(dev_.dropped()), static_cast<unsigned long long>(dev_.padded()));
    std::fflush(stdout);
    dev_.close();
}

void NativeAudio::push(const std::int16_t* lr, std::size_t frames) {
    std::lock_guard<std::mutex> l(mu_);
    pushed_ += frames;
    if (!dev_.is_open()) { dropped_closed_ += frames; return; }
    dev_.push(lr, frames);
    dev_.regulate();   // 60 ms target, +-100 ms band (audio_device.h): the wall clock's drift against virtual time
}

}  // namespace b3replay
