// src/native/audio_device.cpp (audio1): see audio_device.h.
#include "audio_device.h"

#ifndef B3_AUDIO_DEVICE_ONLY   // play1: b3render builds the device alone (src/b3replay/nativeaudio.cpp)
#include "rwa_iop.h"
#include "../audio/spu2_mixer.h"
#endif

#include <SDL3/SDL.h>

#include <cstdio>
#include <vector>

namespace b3native {

bool AudioDevice::open() {
    if (stream_) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "audio: SDL audio init failed (%s); running silent\n", SDL_GetError());
        return false;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, 48000};
    SDL_AudioStream* s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!s) {
        std::fprintf(stderr, "audio: no playback device (%s); running silent\n", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(s);
    stream_ = s;
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
    if (q > target + band) {
        // drop whole 800-frame frames from the front by draining the excess (SDL_GetAudioStreamData reads the queue)
        std::vector<int16_t> sink(800 * 2);
        for (size_t left = q - target; left >= 800; left -= 800) {
            if (SDL_GetAudioStreamData(s, sink.data(), 800 * 4) <= 0) break;
            dropped_ += 800;
        }
    } else if (q + band < target) {
        static const std::vector<int16_t> silence(800 * 2, 0);
        for (size_t have = q; have < target; have += 800) {
            SDL_PutAudioStreamData(s, silence.data(), 800 * 4);
            padded_ += 800;
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
