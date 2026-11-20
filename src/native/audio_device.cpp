// src/native/audio_device.cpp (audio1): see audio_device.h.
#include "audio_device.h"

#ifndef B3_AUDIO_DEVICE_ONLY   // play1: b3render builds the device alone (src/b3replay/nativeaudio.cpp)
#include "rwa_iop.h"
#include "../audio/spu2_mixer.h"
#endif

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

namespace b3native {

bool AudioDevice::open() {
    if (stream_) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        if (!told_) std::fprintf(stderr, "audio: SDL audio init failed (%s); running silent\n", SDL_GetError());
        told_ = true;
        return false;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, 48000};
    SDL_AudioStream* s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!s) {
        if (!told_) std::fprintf(stderr, "audio: no playback device (%s); running silent\n", SDL_GetError());
        told_ = true;
        return false;
    }
    SDL_ResumeAudioStreamDevice(s);
    stream_ = s;
    told_ = false;
    physical_ = find_physical();
    {   // robust2 (review): the device's block, in this stream's 48 kHz frames (regulate's cushion holds at least one)
        SDL_AudioSpec dspec{};
        int frames = 0;
        block_ = SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(s), &dspec, &frames) && frames > 0 && dspec.freq > 0
                     ? (size_t(frames) * 48000 + size_t(dspec.freq) - 1) / size_t(dspec.freq) : 0;
    }
    before_push_ = 0;
    return true;
}

uint32_t AudioDevice::device_id() const {
    return stream_ ? uint32_t(SDL_GetAudioStreamDevice(static_cast<SDL_AudioStream*>(stream_))) : 0u;
}

uint32_t AudioDevice::find_physical() const {
    const uint32_t logical = device_id();
    if (!logical) return 0;
    const char* name = SDL_GetAudioDeviceName(SDL_AudioDeviceID(logical));   // the physical device's name under it
    if (!name) return 0;
    const std::string want = name;
    uint32_t found = 0;
    int n = 0;
    if (SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&n)) {
        for (int k = 0; k < n && !found; ++k)
            if (const char* nm = SDL_GetAudioDeviceName(ids[k]); nm && want == nm) found = uint32_t(ids[k]);
        SDL_free(ids);
    }
    return found;
}

bool AudioDevice::still_playing_after_removal(uint32_t which) {
    const uint32_t logical = device_id();
    if (!logical) return false;
    if (which == logical) return false;   // the stream's own (logical) device
    SDL_AudioSpec spec{};
    int frames = 0;
    if (!SDL_GetAudioDeviceFormat(SDL_AudioDeviceID(logical), &spec, &frames)) return false;   // no longer known
    const uint32_t now = find_physical();   // SDL moves the default device to the new default when it can
    if (!now && physical_) return false;    // it was listed at the open, and nothing is under the stream any more
    physical_ = now;                        // (never found by name on this system: the logical device decides)
    return true;
}

void AudioDevice::close() {
    if (!stream_) return;
    SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(stream_));
    stream_ = nullptr;
}

void AudioDevice::push(const int16_t* lr, size_t frames) {
    pushed_ += frames;
    if (!stream_ || frames == 0) return;
    before_push_ = queued_frames();   // robust2: 0 = the device ran the queue dry before this push
    SDL_PutAudioStreamData(static_cast<SDL_AudioStream*>(stream_), lr, static_cast<int>(frames * 4));
}

size_t AudioDevice::queued_frames() const {
    if (!stream_) return 0;
    const int q = SDL_GetAudioStreamQueued(static_cast<SDL_AudioStream*>(stream_));
    return q > 0 ? static_cast<size_t>(q) / 4 : 0;
}

void AudioDevice::regulate(size_t target, size_t band) {
    if (!stream_) return;
    auto* s = static_cast<SDL_AudioStream*>(stream_);
    const size_t q = queued_frames();
    target = effective_target(target, block_);
    if (q > target + band) {
        // drop whole 800-frame frames from the front by draining the excess (SDL_GetAudioStreamData reads the queue)
        std::vector<int16_t> sink(800 * 2);
        for (size_t left = q - target; left >= 800; left -= 800) {
            if (SDL_GetAudioStreamData(s, sink.data(), 800 * 4) <= 0) break;
            dropped_ += 800;
        }
    } else if (const size_t pad = pad_frames(before_push_, q, target); pad > 0) {
        static const std::vector<int16_t> silence(800 * 2, 0);
        for (size_t k = 0; k < pad; k += 800) SDL_PutAudioStreamData(s, silence.data(), 800 * 4);
        padded_ += pad;
        before_push_ = q + pad;   // (regulate twice without a push: no second pad)
        if (!told_pad_) {
            std::fprintf(stderr, "audio: the queue ran dry: %zu frames of silence padded to a %zu-frame cushion (device block %zu; logged once)\n",
                         pad, target, block_);
            told_pad_ = true;
        }
    }
}

}  // namespace b3native

#ifndef B3_AUDIO_DEVICE_ONLY
namespace b3audio {

size_t pump(b3native::RwaIop& iop, Spu2Mixer& mixer, b3native::AudioDevice* device, uint32_t samples) {
    iop.advance(samples);
    int16_t buf[2 * 2048];
    size_t total = 0;
    for (size_t n; (n = mixer.read_output(buf, 2048)) > 0;) {
        if (device) device->push(buf, n);
        total += n;
    }
    return total;
}

}  // namespace b3audio
#endif
