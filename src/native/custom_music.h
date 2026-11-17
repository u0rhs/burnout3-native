// custom_music.h -- the user's own music folder in the game's EA Trax system (remaster-features §6, first step: a
// custom folder and the now-playing popup; 2026-11-10). Host side only: the folder scan, the tags, the decoders (MP3,
// FLAC, Ogg Vorbis, WAV; src/native/third_party), a cubic resampler to the mixer's 48 kHz and the player that the game's
// music stream drives. The game side (which stream plays a custom song, the popup's text) is tools/native/custom_music.h.
//
// Everything runs on the game's thread, on virtual time: the player advances only by the frames the platform's mixer
// renders (platform_rwa.cpp's VBlank hook calls mix()), so a song's end and its position are the same in every run of
// the same folder (a replay of a custom-music session stays exact).
#ifndef B3NATIVE_CUSTOM_MUSIC_H
#define B3NATIVE_CUSTOM_MUSIC_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace b3music {

constexpr unsigned kOutputRate = 48000;   // the SPU2 mixer's output (platform_rwa.cpp)

// ---- tags (UTF-8 text; empty when absent) ----
struct Tags {
    std::string title, artist, album;
};
// ID3v2.2 / 2.3 / 2.4 at the start of `d` (TIT2 / TPE1 / TALB, v2.2 TT2 / TP1 / TAL; the four text encodings,
// unsynchronisation, extended headers). Fields already set in `t` are kept. *tag_bytes: the whole tag's size.
bool parse_id3v2(const std::uint8_t* d, std::size_t n, Tags& t, std::size_t* tag_bytes = nullptr);
// ID3v1 ("TAG" + 125 bytes, Latin-1) -- only fields still empty are set.
bool parse_id3v1(const std::uint8_t* d, std::size_t n, Tags& t);
// A Vorbis comment block (vendor string, count, "KEY=value" entries, little-endian lengths): TITLE, ARTIST (or
// ALBUMARTIST), ALBUM; the first value of each key wins.
bool parse_vorbis_comment(const std::uint8_t* d, std::size_t n, Tags& t);
// One "KEY=value" entry (any case) into `t` (used for Ogg Vorbis files, whose comments stb_vorbis returns as text).
void apply_comment(const std::string& entry, Tags& t);
// A FLAC file's start ("fLaC" and its metadata blocks): the VORBIS_COMMENT block.
bool parse_flac_tags(const std::uint8_t* d, std::size_t n, Tags& t);
// The fallback from a file name: "07 Artist - Title.mp3" -> artist "Artist", title "Title" (a leading track number and
// the extension dropped; no " - ": the whole name is the title). Fields already set are kept.
void tags_from_filename(const std::string& path, Tags& t);
// Latin-1 / UTF-16 to UTF-8.
std::string latin1_to_utf8(const std::uint8_t* d, std::size_t n);
std::string utf16_to_utf8(const std::uint8_t* d, std::size_t n, bool big_endian);
// UTF-8 to the game's popup text: UTF-16 code units, Latin-1 kept (the game's fonts are Latin-1 [L]), common
// typographic marks folded to ASCII, anything else '?'; at most `max_units` (a long text ends in "...").
std::u16string popup_text(const std::string& utf8, std::size_t max_units);

// ---- the folder ----
bool is_audio_file(const std::string& path);   // .mp3 .flac .ogg .oga .wav (any case)
struct Track {
    std::string path;   // UTF-8
    Tags tags;          // title / artist always set (the file name); album: the tag, else the folder's name
};
Tags read_tags(const std::string& path);
// Every audio file under `dir` (recursive), sorted by path (byte order: the same list on every run). *why: an error.
std::vector<Track> scan_folder(const std::string& dir, std::string* why = nullptr);
// scan_folder's paths without their tags (the same files, the same order; the launcher's Music page counts them).
std::vector<std::string> list_audio_files(const std::string& dir, std::string* why = nullptr);

// ---- decoding (src/native/music_decoders.cpp) ----
std::FILE* open_file(const std::string& utf8_path);   // "rb", UTF-8 path on every system
bool read_ogg_comments(const std::string& path, Tags& t);
class Decoder {
public:
    virtual ~Decoder() = default;
    // MP3 / FLAC / Ogg Vorbis / WAV by extension, else by trying each; null and *why on failure.
    static std::unique_ptr<Decoder> open(const std::string& path, std::string* why = nullptr);
    unsigned rate() const { return rate_; }
    unsigned channels() const { return channels_; }
    // Interleaved stereo float (mono doubled, more channels: the first two); returns the frames read, 0 at the end.
    std::size_t read(float* lr, std::size_t frames);
    virtual bool seek(std::uint64_t frame) = 0;

protected:
    virtual std::size_t read_native(float* out, std::size_t frames) = 0;   // channels_ interleaved
    unsigned rate_ = 0, channels_ = 0;
    std::vector<float> scratch_;
};

// Cubic (Catmull-Rom) resampling of a stereo stream at any ratio (source frames per output frame).
class Resampler {
public:
    // From source frame `base` (the decoder already there).
    void reset(std::uint64_t base = 0);
    // Produces up to `frames` output frames into `out` (interleaved stereo), pulling source frames from `src` as
    // needed; returns the frames produced (fewer only at the source's end).
    std::size_t run(Decoder& src, double ratio, float* out, std::size_t frames);
    double source_position() const { return double(base_) + t_; }   // the read point, in source frames
    bool ended() const { return ended_ && t_ >= double(end_); }

private:
    float at(std::int64_t k, int c) const;
    std::vector<float> win_;           // stereo source frames from win_start_ (relative to base_)
    std::int64_t win_start_ = 0, end_ = 0;
    double t_ = 0.0;                   // the read point relative to base_
    std::uint64_t base_ = 0;
    bool ended_ = false;
};

// One song at the output rate, driven by the game's music stream (prepare, play, pause, stop, the volume each poll).
class Player {
public:
    bool start(const Track& t, std::uint64_t at_frame, std::string* why = nullptr);   // opened, positioned, not playing
    void play(bool loop) { playing_ = true; paused_ = false; loop_ = loop; }
    void pause(bool on) { paused_ = on; }
    void stop() { playing_ = false; }
    void close();
    bool open() const { return dec_ != nullptr; }
    bool playing() const { return playing_ && !paused_; }
    bool finished() const { return finished_; }
    void set_gain(float g) { gain_target_ = g < 0.0f ? 0.0f : (g > 4.0f ? 4.0f : g); }
    void set_pitch(float p) { pitch_ = p < 0.25f ? 0.25f : (p > 4.0f ? 4.0f : p); }
    float gain() const { return gain_; }
    // Adds this many output frames of the song (at the current gain) into `lr` (s16 stereo, saturated) and advances;
    // silent and still while not playing.
    void mix(std::int16_t* lr, std::size_t frames);
    std::uint64_t source_frame() const;            // the song's position in its own frames
    std::uint32_t position_ms() const;
    unsigned source_rate() const { return dec_ ? dec_->rate() : 0; }
    const Track& track() const { return track_; }
    std::uint64_t frames_mixed() const { return mixed_; }   // output frames of song mixed in (all songs)
    std::uint64_t peak() const { return peak_; }            // the largest |sample| added (s16 units, before saturation)

private:
    std::unique_ptr<Decoder> dec_;
    Resampler rs_;
    Track track_;
    bool playing_ = false, paused_ = false, loop_ = false, finished_ = false;
    float gain_ = -1.0f, gain_target_ = 0.0f, pitch_ = 1.0f;   // gain_ < 0: none yet (the first target taken as is)
    std::uint64_t mixed_ = 0, peak_ = 0;
    std::vector<float> tmp_;
};

}  // namespace b3music

#endif
