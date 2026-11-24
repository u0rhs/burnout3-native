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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

// ---- the custom songs' EA TRAX modes (remaster-features §6.3) ----
// The game's per-song bits (the profile's D_004F5040 [0..43], the song table's +0x14 [C decomp func_00431050 /
// func_00431160]): 7 both (races and menus), 1 races, 4 menus, 0 off. A custom song's mode is kept host-side, keyed by
// its folder and its path in that folder (the save holds the 44 songs of the disc only).
enum : std::uint8_t { kModeOff = 0, kModeRaces = 1, kModeMenus = 4, kModeBoth = 7 };
std::uint8_t mode_from_word(const std::string& w);   // "both" / "races" / "menus" / "off"; anything else: kModeBoth
const char* mode_word(std::uint8_t m);               // the low three bits: 7 both, 1 races, 4 menus, else off
// `path` (a file under `folder`, as scan_folder gives it) relative to the folder, '/' separators: the key of its mode
std::string relative_key(const std::string& folder, const std::string& path);
// The modes file: "folder=<folder>" starts a folder's lines, then "<mode word>=<path in the folder>" (UTF-8, one per
// line; '#' lines are comments). A song not in it is kModeBoth.
class ModeStore {
public:
    bool load(const std::string& file);   // false: no such file (the store is then empty) or unreadable
    bool save(std::string* why = nullptr) const;   // the whole file, through a temporary and a rename
    void clear() { modes_.clear(); }
    std::uint8_t get(const std::string& folder, const std::string& key) const;
    void set(const std::string& folder, const std::string& key, std::uint8_t mode);
    void set_file(const std::string& file) { file_ = file; }
    const std::string& file() const { return file_; }
    std::string text() const;   // the file's text
    bool parse(const std::string& text);

private:
    std::string file_;
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::uint8_t>>>> modes_;   // folder order kept
};

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

// A look-ahead peak limiter (stereo, both channels turned down together) for a song after its gain: a loud master at
// a high gain (CUSTOM MUSIC VOLUME above 100 %, RADIO VOLUME at the top) is turned down smoothly instead of clipping.
// Each frame's need (ceiling / peak) is held for kLookahead frames, released slowly towards 1 and then averaged over
// kLookahead frames, which the signal is delayed by: so the gain is already down when a peak comes out, the output's
// peaks stay at the ceiling, and the gain never steps (no clicks, no clipped waves). Below the ceiling it is an exact
// delay of kLookahead - 1 frames (the gain exactly 1). Plain float arithmetic only: the same output on every system.
class Limiter {
public:
    static constexpr std::size_t kLookahead = 64;          // frames: 1.3 ms at 48 kHz (the attack and the delay)
    static constexpr float kCeiling = 0.891f;              // -1 dBFS: room for the game's own sounds on top
    static constexpr float kRelease = 1.0f / 4800.0f;      // per frame, towards a gain of 1 (~0.1 s time constant)
    Limiter() { reset(); }
    void reset();
    // In place: `frames` interleaved stereo frames (full scale 1.0) in, the same number out, kLookahead - 1 later.
    void run(float* lr, std::size_t frames);
    std::uint64_t limited() const { return limited_; }     // frames that came out turned down
    float min_gain() const { return min_gain_; }           // the lowest gain used

private:
    float x_[2 * kLookahead];   // the last kLookahead frames in (a ring at at_)
    float need_[kLookahead];    // their needs (ceiling / peak, at most 1)
    float env_[kLookahead];     // the released envelope (the average's ring)
    std::size_t at_ = 0;
    float env_now_ = 1.0f;
    std::uint64_t limited_ = 0;
    float min_gain_ = 1.0f;
};

// ---- loudness (musicloud: CUSTOM MUSIC LOUDNESS, Even out) ----
// EBU R128 / ITU-R BS.1770-4 integrated loudness of a stereo stream: K-weighting (the standard's high shelf and high
// pass, their coefficients worked out for the stream's own rate), mean squares over 400 ms blocks every 100 ms, the
// absolute gate (-70 LUFS) and the relative gate (10 LU under the gated mean). Plus the sample peak. Double precision.
class LoudnessMeter {
public:
    explicit LoudnessMeter(unsigned rate);
    void add(const float* lr, std::size_t frames);   // interleaved stereo (full scale 1.0)
    // The integrated loudness in LUFS; -inf when no block passes the gates (silence, or less than 400 ms)
    double integrated() const;
    double peak() const { return peak_; }            // the largest |sample| (1.0 = full scale)
private:
    struct Biquad {
        double b0, b1, b2, a1, a2;
        double z1[2] = {0.0, 0.0}, z2[2] = {0.0, 0.0};
        double run(double x, int c);
    };
    Biquad shelf_{}, pass_{};
    std::size_t sub_len_ = 0, sub_at_ = 0;   // frames per 100 ms, frames in the current 100 ms
    double sub_sum_ = 0.0, sub_[4] = {};     // the current sub-block's energy, the last four's
    std::size_t subs_ = 0;                   // sub-blocks done
    std::vector<double> blocks_;             // each 400 ms block's mean square (the channels summed)
    double peak_ = 0.0;
};
// One file measured from start to end through Decoder (its own rate, no resampling). False: unreadable, or `stop`
// set while it ran (checked every 4096 frames; *stopped then true).
bool measure_loudness(const std::string& path, double& lufs, double& peak, std::string* why = nullptr,
                      const std::atomic<bool>* stop = nullptr, bool* stopped = nullptr);

// CUSTOM MUSIC LOUDNESS's gain for a song of `lufs` with sample peak `peak`: towards kLoudnessTarget, at most
// kMaxCut down and kMaxBoost up, and a boost only as far as the song's peak goes over the player's Limiter by
// kLimitAllowance dB (a dynamic, quiet song is raised as far as it stays clean, not squashed to the target). A song
// with no measured loudness (silence): 1.
// LUFS: the game's own EA TRAX songs at their source level (the menus' output over the music stream's gain, 0.450 on
// the 100 % card: "Give It Up" -8.8, the front end's own piece -11.3; musicloud, 2026-11-22)
constexpr double kLoudnessTarget = -10.0;
constexpr double kMaxBoost = 12.0, kMaxCut = 20.0, kLimitAllowance = 3.0;   // dB
float loudness_gain(double lufs, double peak);

// The measured songs, keyed by path, size and modification time (a changed file is measured again); kept in a text
// file (one song per line, tab separated: LUFS, peak, size, time, path; '#' lines are comments). Thread-safe.
// The values are kept at the file's precision even when just measured, so a song plays at the same gain whether it
// came from the file or was measured in this run.
class LoudnessCache {
public:
    struct Entry {
        double lufs = 0.0, peak = 0.0;
    };
    bool load(const std::string& file);   // false: no such file (the cache then empty) or unreadable
    bool save(std::string* why = nullptr);   // through a temporary and a rename; nothing when unchanged since
    void set_file(const std::string& file);
    bool get(const std::string& path, Entry& e) const;   // false: not measured (or the file changed since)
    void put(const std::string& path, double lufs, double peak);   // the file's size and time read now
    std::string text() const;
    bool parse(const std::string& text);
    static bool stat(const std::string& path, std::uint64_t& size, std::int64_t& time);
    static double quantize(double v, int decimals);
private:
    struct Rec {
        std::uint64_t size = 0;
        std::int64_t time = 0;
        Entry e;
    };
    mutable std::mutex m_;
    std::mutex save_m_;   // one save at a time (the worker's and need()'s share the .tmp file)
    std::string file_;
    std::map<std::string, Rec> recs_;
    bool dirty_ = false;
};

// The folder's songs measured on a worker thread (in list order, the cached ones skipped), the cache saved as it goes;
// stop() cancels a measure under way (a quit or a new folder never waits for a long file). need() measures a small
// song (kNowBytes) on the caller's thread when the worker has not reached it yet; a bigger one goes to the front of the
// worker's list and need() returns false (the caller plays it at gain 1 and takes the worker's result when lookup()
// has it: the game's thread never stalls for seconds). The gains only depend on the files; only when a big song's
// gain arrives depends on the timing.
class LoudnessService {
public:
    ~LoudnessService() { stop(); }
    void set_cache_file(const std::string& file);   // loads it (once per file)
    void start(const std::vector<std::string>& paths);   // replaces the worker's list
    void stop();     // the worker stopped where it is (the cache saved)
    void finish();   // waits for the worker to go through its list
    bool lookup(const std::string& path, float& gain);   // the gain if measured (never waits)
    // The gain (1: unreadable), measured now if need be and small; false: a big song, left to the worker (first)
    bool need(const std::string& path, float& gain);
    static constexpr std::uint64_t kNowBytes = 12u << 20;   // MP3 / Ogg (the lossless formats: x5): ~5 min at 320 kbps
    std::uint64_t now_bytes = kNowBytes;                     // (tests)
    std::size_t measured() const { return measured_.load(); }   // songs measured in this run (both threads)
    LoudnessCache& cache() { return cache_; }
private:
    void run();
    LoudnessCache cache_;
    std::string cache_file_;
    std::mutex m_;
    std::vector<std::string> todo_;
    std::string first_;          // need()'s big song: the worker's next (m_)
    bool running_ = false;       // the worker is in its loop (m_)
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<std::size_t> measured_{0};
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
    // Adds this many output frames of the song (at the current gain, through the Limiter) into `lr` (s16 stereo,
    // saturated) and advances; silent and still while not playing.
    void mix(std::int16_t* lr, std::size_t frames);
    const Limiter& limiter() const { return lim_; }
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
    Limiter lim_;
};

}  // namespace b3music

#endif
