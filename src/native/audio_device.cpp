// src/native/audio_device.cpp (audio1): see audio_device.h.
#include "audio_device.h"

#ifndef B3_AUDIO_DEVICE_ONLY   // play1: b3render builds the device alone (src/b3replay/nativeaudio.cpp)
#include "rwa_iop.h"
#include "../audio/spu2_mixer.h"
#endif

#include <SDL3/SDL.h>

#include <algorithm>
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
    queue_.reset();
    ratio_set_ = 1.0;
    pull_buf_.assign(2 * 16384, 0);   // the largest piece the callback fills at once (it loops for more)
    // audioq: the device's stream pulls from the output queue (audio_queue.h) when it needs more
    SDL_AudioStream* s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                                   [](void* u, SDL_AudioStream* st, int add, int total) { AudioDevice::pull(u, st, add, total); }, this);
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
    queue_.set_target(OutputQueue::target_for(effective_target(2880, block_), block_));
    return true;
}

void AudioDevice::pull(void* self, void* stream, int additional, int) {
    auto* d = static_cast<AudioDevice*>(self);
    if (additional <= 0) return;
    // in pieces of the buffer sized at the open (no allocation on the device's thread)
    const size_t cap = d->pull_buf_.size() / 2;
    for (size_t left = size_t(additional + 3) / 4; left > 0 && cap > 0;) {
        const size_t n = std::min(left, cap);
        d->queue_.pull(d->pull_buf_.data(), n);
        SDL_PutAudioStreamData(static_cast<SDL_AudioStream*>(stream), d->pull_buf_.data(), int(n * 4));
        left -= n;
    }
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
    SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(stream_));   // (the callback is not called after this returns)
    stream_ = nullptr;
}

void AudioDevice::push(const int16_t* lr, size_t frames) {
    pushed_ += frames;
    if (!stream_ || frames == 0) return;
    const uint64_t under = queue_.underruns();
    queue_.push(lr, frames);
    // the rate control's ratio to the stream: it reads the queue that much faster (> 1) or slower; set when it moved
    // by more than a part in a million (SDL's resampler follows it smoothly)
    const double r = queue_.ratio();
    if (r != ratio_set_ && (r - ratio_set_ > 1e-6 || ratio_set_ - r > 1e-6)) {
        SDL_SetAudioStreamFrequencyRatio(static_cast<SDL_AudioStream*>(stream_), float(r));
        ratio_set_ = r;
    }
    if (!told_pad_ && under > 0) {
        std::fprintf(stderr, "audio: the game's sound was late (the device ran the queue dry): faded out, then back in after a %zu-frame cushion "
                     "(device block %zu; logged once)\n", queue_.target(), block_);
        told_pad_ = true;
    }
}

size_t AudioDevice::queued_frames() const {
    return stream_ ? queue_.level() : 0;
}

void AudioDevice::regulate(size_t target, size_t) {
    queue_.set_target(effective_target(target, block_));   // (the drift guard is the output queue's: audio_queue.h)
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
