// music_decoders.cpp -- the decoders of the custom soundtrack (custom_music.h): dr_mp3, dr_flac, dr_wav and stb_vorbis
// (src/native/third_party, public domain / MIT-0 / MIT; README.md there), behind one Decoder interface.
#include "custom_music.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO_W_WARNING
#include "third_party/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG   // Ogg is stb_vorbis's
#include "third_party/dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "third_party/dr_wav.h"
#define STB_VORBIS_NO_PUSHDATA_API
#include "third_party/stb_vorbis.c"
// stb_vorbis leaves short macro names defined (L, C, R ...): none is used below
#undef L
#undef C
#undef R
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace b3music {
namespace {

#ifdef _WIN32
std::filesystem::path upath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
#endif

std::string lower_ext(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t sep = path.find_last_of("/\\");
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return {};
    std::string e = path.substr(dot);
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

class Mp3 final : public Decoder {
public:
    bool init(const std::string& path) {
#ifdef _WIN32
        if (!drmp3_init_file_w(&d_, upath(path).c_str(), nullptr)) return false;
#else
        if (!drmp3_init_file(&d_, path.c_str(), nullptr)) return false;
#endif
        ok_ = true;
        rate_ = d_.sampleRate;
        channels_ = d_.channels;
        return rate_ > 0 && channels_ > 0;
    }
    ~Mp3() override { if (ok_) drmp3_uninit(&d_); }
    bool seek(std::uint64_t f) override { return drmp3_seek_to_pcm_frame(&d_, f) != 0; }

protected:
    std::size_t read_native(float* out, std::size_t n) override { return std::size_t(drmp3_read_pcm_frames_f32(&d_, n, out)); }

private:
    drmp3 d_{};
    bool ok_ = false;
};

class Flac final : public Decoder {
public:
    bool init(const std::string& path) {
#ifdef _WIN32
        d_ = drflac_open_file_w(upath(path).c_str(), nullptr);
#else
        d_ = drflac_open_file(path.c_str(), nullptr);
#endif
        if (!d_) return false;
        rate_ = d_->sampleRate;
        channels_ = d_->channels;
        return rate_ > 0 && channels_ > 0;
    }
    ~Flac() override { if (d_) drflac_close(d_); }
    bool seek(std::uint64_t f) override { return drflac_seek_to_pcm_frame(d_, f) != 0; }

protected:
    std::size_t read_native(float* out, std::size_t n) override { return std::size_t(drflac_read_pcm_frames_f32(d_, n, out)); }

private:
    drflac* d_ = nullptr;
};

class Wav final : public Decoder {
public:
    bool init(const std::string& path) {
#ifdef _WIN32
        if (!drwav_init_file_w(&d_, upath(path).c_str(), nullptr)) return false;
#else
        if (!drwav_init_file(&d_, path.c_str(), nullptr)) return false;
#endif
        ok_ = true;
        rate_ = d_.sampleRate;
        channels_ = d_.channels;
        return rate_ > 0 && channels_ > 0;
    }
    ~Wav() override { if (ok_) drwav_uninit(&d_); }
    bool seek(std::uint64_t f) override { return drwav_seek_to_pcm_frame(&d_, f) != 0; }

protected:
    std::size_t read_native(float* out, std::size_t n) override { return std::size_t(drwav_read_pcm_frames_f32(&d_, n, out)); }

private:
    drwav d_{};
    bool ok_ = false;
};

class Vorbis final : public Decoder {
public:
    bool init(const std::string& path) {
        std::FILE* f = open_file(path);
        if (!f) return false;
        int err = 0;
        v_ = stb_vorbis_open_file(f, 1, &err, nullptr);   // closes f when done (or on failure [C stb_vorbis.c 5073])
        if (!v_) return false;
        const stb_vorbis_info i = stb_vorbis_get_info(v_);
        rate_ = i.sample_rate;
        channels_ = unsigned(i.channels);
        return rate_ > 0 && channels_ > 0;
    }
    ~Vorbis() override { if (v_) stb_vorbis_close(v_); }
    bool seek(std::uint64_t f) override { return stb_vorbis_seek(v_, unsigned(f)) != 0; }

protected:
    std::size_t read_native(float* out, std::size_t n) override {
        std::size_t got = 0;
        while (got < n) {   // stb returns at most one packet's frames per call
            const int r = stb_vorbis_get_samples_float_interleaved(v_, int(channels_), out + got * channels_, int((n - got) * channels_));
            if (r <= 0) break;
            got += std::size_t(r);
        }
        return got;
    }

private:
    stb_vorbis* v_ = nullptr;
};

template <class D>
std::unique_ptr<Decoder> try_open(const std::string& path) {
    auto d = std::make_unique<D>();
    if (!d->init(path)) return nullptr;
    return d;
}

}  // namespace

std::FILE* open_file(const std::string& utf8_path) {
#ifdef _WIN32
    return _wfopen(upath(utf8_path).c_str(), L"rb");
#else
    return std::fopen(utf8_path.c_str(), "rb");
#endif
}

bool read_ogg_comments(const std::string& path, Tags& t) {
    std::FILE* f = open_file(path);
    if (!f) return false;
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_file(f, 1, &err, nullptr);
    if (!v) return false;
    const stb_vorbis_comment c = stb_vorbis_get_comment(v);
    for (int k = 0; k < c.comment_list_length; ++k)
        if (c.comment_list[k]) apply_comment(c.comment_list[k], t);
    stb_vorbis_close(v);
    return true;
}

std::unique_ptr<Decoder> Decoder::open(const std::string& path, std::string* why) {
    const std::string e = lower_ext(path);
    std::unique_ptr<Decoder> d;
    if (e == ".mp3") d = try_open<Mp3>(path);
    else if (e == ".flac") d = try_open<Flac>(path);
    else if (e == ".ogg" || e == ".oga") d = try_open<Vorbis>(path);
    else if (e == ".wav") d = try_open<Wav>(path);
    if (!d) {   // a wrong extension: by content
        if (e != ".flac") d = try_open<Flac>(path);
        if (!d && e != ".ogg" && e != ".oga") d = try_open<Vorbis>(path);
        if (!d && e != ".wav") d = try_open<Wav>(path);
        if (!d && e != ".mp3") d = try_open<Mp3>(path);
    }
    if (!d && why) *why = "cannot decode " + path;
    return d;
}

std::size_t Decoder::read(float* lr, std::size_t frames) {
    if (channels_ == 2) return read_native(lr, frames);
    scratch_.resize(frames * channels_);
    const std::size_t got = read_native(scratch_.data(), frames);
    for (std::size_t k = 0; k < got; ++k) {
        const float* s = scratch_.data() + k * channels_;
        lr[2 * k] = s[0];
        lr[2 * k + 1] = channels_ == 1 ? s[0] : s[1];
    }
    return got;
}

}  // namespace b3music
