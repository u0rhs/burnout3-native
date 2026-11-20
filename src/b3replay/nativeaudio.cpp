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
    else {
        std::printf("[b3audio] no playback device: the game runs silent until one is there (tried again every 3 s)\n");
        reopen_.lost(0);   // (due at the first poll: tried again at once, then every 3 s)
    }
    std::fflush(stdout);
}

void NativeAudio::poll(std::uint64_t now_ns) {
    std::lock_guard<std::mutex> l(mu_);
    if (!tried_ || !reopen_.due(now_ns)) return;
    if (dev_.open()) {
        reopen_.opened();
        std::printf("[b3audio] sound: a playback device is there now, 48 kHz s16 stereo\n");
        std::fflush(stdout);
    } else {
        reopen_.lost(now_ns);
    }
}

void NativeAudio::event(const void* p) {
    const SDL_Event& e = *static_cast<const SDL_Event*>(p);
    if (e.type != SDL_EVENT_AUDIO_DEVICE_ADDED && e.type != SDL_EVENT_AUDIO_DEVICE_REMOVED) return;
    if (e.adevice.recording) return;
    std::lock_guard<std::mutex> l(mu_);
    if (!tried_) return;
    if (e.type == SDL_EVENT_AUDIO_DEVICE_ADDED) {
        if (!dev_.is_open()) reopen_.kick();
    } else if (dev_.is_open() && !dev_.still_playing_after_removal(e.adevice.which)) {   // the device in use is gone (none took over)
        std::printf("[b3audio] the playback device is gone (device %u removed): silent until one is there\n", unsigned(e.adevice.which));
        std::fflush(stdout);
        dev_.close();
        reopen_.lost(0);
        reopen_.kick();   // another may be there already: the next poll tries
    }
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
