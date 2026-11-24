// The native game's sound out of b3render (play1): src/b3replay/nativeaudio.h.
#include "nativeaudio.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

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
    std::printf("[b3audio] %llu frames pushed (%llu before the device was open); output queue: %llu late (the game's sound late: faded out and "
                "in, %llu frames of silence), %llu through fast loads (the game silent on purpose, %llu frames), %llu bursts dropped (%llu "
                "frames, crossfaded); the rate %.4f %% off the game's at the end\n",
                static_cast<unsigned long long>(pushed_), static_cast<unsigned long long>(dropped_closed_),
                static_cast<unsigned long long>(dev_.underruns()), static_cast<unsigned long long>(dev_.padded()),
                static_cast<unsigned long long>(dev_.quiet_underruns()), static_cast<unsigned long long>(dev_.quiet_padded()),
                static_cast<unsigned long long>(dev_.drops()), static_cast<unsigned long long>(dev_.dropped()), (dev_.ratio() - 1.0) * 100.0);
    std::fflush(stdout);
    dev_.close();
}

void NativeAudio::push(const std::int16_t* lr, std::size_t frames) {
    std::lock_guard<std::mutex> l(mu_);
    pushed_ += frames;
    // weather (checks only): $B3_RAIN_AUDIO_DUMP=<file> writes the frames with the rain mixed in (raw s16 stereo 48 kHz),
    // with or without a device, so the hook and the renderer's state can be checked offscreen
    static std::FILE* dump = [] { const char* e = std::getenv("B3_RAIN_AUDIO_DUMP"); return e && *e ? std::fopen(e, "wb") : nullptr; }();
    if (!dev_.is_open() && !dump) { dropped_closed_ += frames; return; }
    // weather (§38, rain_audio.h): the rain's sound mixed into these frames (never more frames: the count is the
    // queue's clock), from the renderer's state; silent and untouched when there is no rain
    RainAudioState rs;
    // rainfe: on only while the renderer's race frames keep setting it (RainAudio::state_live)
    rs.on = rain_on_.load(std::memory_order_relaxed) && RainAudio::state_live(now_ns(), rain_set_ns_.load(std::memory_order_relaxed));
    rs.speed = rain_speed_.load(std::memory_order_relaxed);
    rs.enclosed = rain_enclosed_.load(std::memory_order_relaxed);
    rs.volume = rain_volume_.load(std::memory_order_relaxed) * effects_volume_.load(std::memory_order_relaxed);
    if (rs.on || rain_.bed_gain() > 0.0f) {
        if (mix_.size() < 2 * frames) mix_.resize(2 * frames);   // (grows once to the largest push, then kept)
        std::copy(lr, lr + 2 * frames, mix_.begin());
        rain_.mix(mix_.data(), frames, rs);
        if (dump) std::fwrite(mix_.data(), sizeof(std::int16_t), 2 * frames, dump);
        if (dev_.is_open()) dev_.push(mix_.data(), frames);
        else dropped_closed_ += frames;
        return;
    }
    if (dump) std::fwrite(lr, sizeof(std::int16_t), 2 * frames, dump);
    if (!dev_.is_open()) { dropped_closed_ += frames; return; }
    dev_.push(lr, frames);   // audioq: the output queue keeps the rate, fades a stall, crossfades a burst (audio_queue.h)
}

}  // namespace b3replay
