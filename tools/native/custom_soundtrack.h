// custom_soundtrack.h -- the user's music folder in the game's EA Trax system (remaster-features §6, first step;
// 2026-11-10). Host side: src/native/custom_music.h (folder, tags, decoders, the player).
//
// How the game plays its music [C decomp boot2w]: the radio object D_01E7A888 (CB3SoundRadio / the music manager [G])
// keeps the current soundtrack at +0x68 (song count +0x6C, the songs +0xB4: 0x18 bytes each, +8 / +0xC / +0x10 the
// title / album / artist GLOBALUS.BIN string ids, +0x14 the playlist flags), its play mode at +0xCC (1 / 5 in order,
// 2 shuffle, 3 / 4 repeat), its radio mode at +0x178 (3: the EA TRAX settings page's previews), and the music stream
// (a CGtSoundStream) at +0xC0.
//   - func_003FBC20(manager, song) starts a song: +0xD8 = song, +0xC4 = its entry, +0x48 = the stream's file name
//     ("tracks\_EATrax1.rws" / 2, 22 songs each, the song a segment); song -1 stops.
//   - func_003FC8C0 (the stream's state machine, every poll): AllocStream, then func_003865A0(stream, name, 0, start
//     sample, segment, ...) until it returns 1 (prepared), Play__14CGtSoundStreamFb, SetVolume (+0x0 radio volume x
//     +0x4 duck), and when func_00385FE0 says the stream is done (status +0x1D0 4 or 0), the next song
//     (func_003FBDB0, func_003FBC20); func_003FC2E0 / func_003FC260 suspend and resume it at a sample position.
//   - func_003FC700, the now-playing popup: once the stream plays (+0x1D0 6), func_00438F10(&D_01ECDE90 (the EA TRAX
//     display), title, artist, album) with the three UTF-16 strings of the song's entry (func_00271A00, the string
//     lookup), and func_00438F00 5 s of stream time later (func_00386080(stream, 1), ms). The display draws the three
//     pointers as given (func_00438FB0 copies them to its text object).
//   - CGtSoundStream: +0x1A4 the per-substream volumes (SetVolume), +0x1CC its RW Audio stream, +0x1D0 its status
//     (1 preparing, 2 prepared, 4 done, 5 stopped, 6 playing, 8 paused), +0x1EC its sound category (13 for music:
//     D_01EA4B00[13] = {pitch, gain}, the category's fades and ducking), +0x1F4 media attached; ApplyVolumesAndPitch
//     gives RW Audio volume x category gain and the category's pitch.
//
// What this adds (playlist mode Custom only / Mixed; EA Trax only, the default, installs the wrappers but they pass
// every call through untouched): when the manager starts a song (not on the EA TRAX page, whose previews stay the
// game's), a custom song is chosen for it (Mixed: every other song); the music stream's prepare then attaches no
// media and answers "prepared", and that stream object -- the "virtual" stream -- is played by the host player instead
// of RW Audio: Play / Pause / Stop / Release / Update / the position / the block size of that one object are answered
// here with the same status changes the game's code makes, the player follows its volume x the category gain each
// poll (RADIO VOLUME, the menus' ducking, the fades; pause stops it as PauseAll pauses the stream), and the song's end is
// the stream's end (status 4: the game picks the next song, the DJ follows as usual). The player is mixed into the
// SPU2 mixer's 48 kHz output (platform_rwa.cpp set_music_overlay), on virtual time. The popup: func_00438F10 is given
// the custom song's title / artist / album, UTF-16 in free guest RAM 0xC0C00..0xC0F00 (the mod arena's port region
// GuestArena::kCustomSoundtrack; checked unused before the first write, as date_format.h does). The wrappers are on
// the hook table (hook_table.h, port layer).
//
// Settings: PC SETTINGS > INTERFACE > MUSIC (b3r_music: the folder and the mode); B3NATIVE_MUSIC_DIR / B3NATIVE_MUSIC_MODE
// (0 / 1 / 2) override them (tests). B3NATIVE_MUSIC_LOG=1 logs the game's own popup texts too.
#ifndef B3NATIVE_CUSTOM_SOUNDTRACK_H
#define B3NATIVE_CUSTOM_SOUNDTRACK_H

#include "custom_music.h"
#include "hook_table.h"
#include "platform.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace b3native {

class CustomSoundtrack {
public:
    enum Mode { kEaTrax = 0, kCustom = 1, kMixed = 2 };
    // config(folder, n): the playlist mode (Mode), the music folder copied to `folder` (UTF-8); read at each song start
    using ConfigFn = int (*)(char* folder, int n);
    bool install(HookTable& hooks, ConfigFn config);
    ~CustomSoundtrack();

    static constexpr b3host::gaddr kStartSong = 0x003FBC20u, kPrepare = 0x003865A0u, kPlay = 0x00386290u,
                                   kStop = 0x00386240u, kRelease = 0x003863C0u, kPause = 0x00385F20u,
                                   kUpdate = 0x00386410u, kPosition = 0x00386080u, kBlock = 0x00386020u,
                                   kShowPopup = 0x00438F10u,
                                   kApplyVolumes = 0x00386130u;   // ApplyVolumesAndPitch__14CGtSoundStreamFv
    // Another music app is playing (PC SETTINGS > INTERFACE > MUSIC > OTHER MUSIC APPS): the game's music category (13) is
    // silent, whatever the playlist mode (EA Trax, custom or mixed); the radio's own state, song changes and the
    // player's setting are untouched, so the music is back as it was when the other app stops. Set once per VBlank by
    // native_play from the latched track (a replay: the recorded one). Host audio only; guest RAM keeps its values.
    static void set_game_music_muted(bool m) { muted_flag().store(m, std::memory_order_relaxed); }
    static bool game_music_muted() { return muted_flag().load(std::memory_order_relaxed); }
    static constexpr b3host::gaddr kCategories = 0x01EA4B00u;   // D_01EA4B00: {pitch, gain} per sound category
    // free guest RAM below the ELF: the mod arena's port region past pc_menu.h's (0xC0000..0xC0400) and
    // date_format.h's (0xC0800..0xC0820)
    static constexpr b3host::gaddr kText = GuestArena::kCustomSoundtrack.address,
                                   kTextEnd = GuestArena::kCustomSoundtrack.address + GuestArena::kCustomSoundtrack.size;
    static_assert(kText == 0x000C0C00u && kTextEnd == 0x000C0F00u, "the popup texts stay where they were");
    static constexpr std::size_t kTextUnits = 0x100 / 2 - 1;     // per line, with its terminator
    static constexpr std::size_t kShownUnits = 48;               // longer lines end in "..." (the game's longest is 40)

private:
    static std::atomic<bool>& muted_flag() { static std::atomic<bool> m{false}; return m; }
    static void apply_volumes(const b3host::CallArgs& a, b3host::CallResult& r);
    static CustomSoundtrack*& instance() { static CustomSoundtrack* p = nullptr; return p; }
    using Fn = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static void start_song(const b3host::CallArgs& a, b3host::CallResult& r);
    static void prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void play(const b3host::CallArgs& a, b3host::CallResult& r);
    static void stop(const b3host::CallArgs& a, b3host::CallResult& r);
    static void release(const b3host::CallArgs& a, b3host::CallResult& r);
    static void pause(const b3host::CallArgs& a, b3host::CallResult& r);
    static void update(const b3host::CallArgs& a, b3host::CallResult& r);
    static void position(const b3host::CallArgs& a, b3host::CallResult& r);
    static void block(const b3host::CallArgs& a, b3host::CallResult& r);
    static void show_popup(const b3host::CallArgs& a, b3host::CallResult& r);
    static void mix(std::int16_t* lr, std::size_t frames) {
        CustomSoundtrack* p = instance();
        if (!p) return;
        p->player_.mix(lr, frames);
        // debugging: B3NATIVE_MUSIC_RMS=<first>-<last> -- the RMS and peak of the whole output (game + custom song) over
        // those VBlanks, to compare a custom song's level with the game's own stream of the same song
        static const std::pair<std::uint64_t, std::uint64_t> range = [] {
            unsigned long long a = 0, b = 0;
            const char* e = std::getenv("B3NATIVE_MUSIC_RMS");
            if (!e || std::sscanf(e, "%llu-%llu", &a, &b) != 2) a = b = 0;
            return std::make_pair(std::uint64_t(a), std::uint64_t(b));
        }();
        if (range.second == 0) return;
        static double sum = 0.0;
        static std::uint64_t n = 0, peak = 0;
        static bool done = false;
        const std::uint64_t v = vb();
        if (v >= range.first && v <= range.second)
            for (std::size_t k = 0; k < 2 * frames; ++k) {
                sum += double(lr[k]) * double(lr[k]);
                peak = std::max<std::uint64_t>(peak, std::uint64_t(lr[k] < 0 ? -lr[k] : lr[k]));
                ++n;
            }
        if (v > range.second && !done && n) {
            done = true;
            std::printf("custom_music: output VBlanks %llu-%llu: RMS %.1f, peak %llu\n", static_cast<unsigned long long>(range.first),
                        static_cast<unsigned long long>(range.second), std::sqrt(sum / double(n)), static_cast<unsigned long long>(peak));
        }
    }

    int mode(std::string* folder) const;
    bool refresh(const std::string& folder);   // the folder's songs (scanned when the folder changes)
    int pick(std::uint32_t manager);           // the next custom song for a song start, -1: the game's
    bool is_virtual(std::uint32_t stream) const { return stream != 0 && stream == virtual_; }
    void end_virtual(const char* why);
    bool write_text(b3host::gaddr at, const std::string& utf8);
    static std::uint64_t vb() { return platform::vblank_count(); }

    Fn start_song_ = nullptr, prepare_ = nullptr, play_ = nullptr, stop_ = nullptr, release_ = nullptr, pause_ = nullptr,
       update_ = nullptr, position_ = nullptr, block_ = nullptr, show_popup_ = nullptr, apply_volumes_ = nullptr;
    ConfigFn config_ = nullptr;
    std::string folder_;                       // the folder scanned
    std::vector<b3music::Track> tracks_;
    std::vector<int> deck_;                    // shuffle order (a fixed seed: the same in every run)
    std::size_t deck_at_ = 0;
    std::uint64_t rng_ = 0x9E3779B97F4A7C15ull;
    int last_ = -1;                            // the last custom song chosen
    std::uint64_t starts_ = 0;                 // song starts seen (Mixed: every other one is custom)
    std::uint32_t manager_ = 0;                // the music manager of the song start
    int current_ = -1;                         // the custom song of the manager's current song, -1: the game's
    std::uint32_t virtual_ = 0;                // the stream object the player stands in for
    int playing_ = -1;                         // the custom song the player has open
    b3music::Player player_;
    bool text_checked_ = false, text_ok_ = false;
    std::uint64_t songs_played_ = 0, popups_ = 0;
};

inline CustomSoundtrack::~CustomSoundtrack() {
    if (instance() != this) return;
    platform::set_music_overlay(nullptr);
    instance() = nullptr;
    if (songs_played_ || !tracks_.empty())
        std::printf("custom_music: %llu custom songs started, %llu popups, %llu frames mixed (%.1f s), peak %llu\n",
                    static_cast<unsigned long long>(songs_played_), static_cast<unsigned long long>(popups_),
                    static_cast<unsigned long long>(player_.frames_mixed()), double(player_.frames_mixed()) / b3music::kOutputRate,
                    static_cast<unsigned long long>(player_.peak()));
}

inline bool CustomSoundtrack::install(HookTable& hooks, ConfigFn config) {
    const std::vector<Hook> set = {
        {kStartSong, HookLayer::kPort, &start_song, &start_song_}, {kPrepare, HookLayer::kPort, &prepare, &prepare_},
        {kPlay, HookLayer::kPort, &play, &play_}, {kStop, HookLayer::kPort, &stop, &stop_},
        {kRelease, HookLayer::kPort, &release, &release_}, {kPause, HookLayer::kPort, &pause, &pause_},
        {kUpdate, HookLayer::kPort, &update, &update_}, {kPosition, HookLayer::kPort, &position, &position_},
        {kBlock, HookLayer::kPort, &block, &block_}, {kShowPopup, HookLayer::kPort, &show_popup, &show_popup_},
        {kApplyVolumes, HookLayer::kPort, &apply_volumes, &apply_volumes_},
    };
    for (const Hook& h : set)
        if (!hooks.has_row(h.address)) {
            std::fprintf(stderr, "custom_music: %08X is not native: no custom soundtrack\n", h.address);
            return false;
        }
    std::string error;
    if (!hooks.install("port:custom_soundtrack", set, &error)) {
        std::fprintf(stderr, "custom_music: %s: no custom soundtrack\n", error.c_str());
        return false;
    }
    config_ = config;
    instance() = this;
    platform::set_music_overlay(&mix);
    std::string folder;
    const int m = mode(&folder);
    std::printf("custom_music: the EA Trax system takes a music folder (mode %s%s%s)\n",
                m == kCustom ? "custom only" : m == kMixed ? "mixed" : "EA Trax only", folder.empty() ? "" : ", folder ", folder.c_str());
    return true;
}

inline int CustomSoundtrack::mode(std::string* folder) const {
    int m = kEaTrax;
    char buf[1024] = {};
    if (config_) m = config_(buf, int(sizeof buf - 1));
    if (folder) *folder = buf;
    if (const char* e = std::getenv("B3NATIVE_MUSIC_MODE"); e && e[0] >= '0' && e[0] <= '2' && !e[1]) m = e[0] - '0';
    if (const char* e = std::getenv("B3NATIVE_MUSIC_DIR"); e && *e && folder) *folder = e;
    return (m >= kEaTrax && m <= kMixed) ? m : kEaTrax;
}

inline bool CustomSoundtrack::refresh(const std::string& folder) {
    if (folder == folder_) return !tracks_.empty();
    folder_ = folder;
    tracks_.clear();
    if (playing_ >= 0) playing_ = -2;   // still playing, but no longer an index of tracks_
    deck_.clear();
    deck_at_ = 0;
    last_ = -1;
    if (folder.empty()) return false;
    const auto t0 = std::chrono::steady_clock::now();
    std::string why;
    tracks_ = b3music::scan_folder(folder, &why);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("custom_music: VBlank %llu: %zu songs in %s (%.0f ms)%s%s\n", static_cast<unsigned long long>(vb()), tracks_.size(),
                folder.c_str(), ms, why.empty() ? "" : ": ", why.c_str());
    return !tracks_.empty();
}

inline int CustomSoundtrack::pick(std::uint32_t manager) {
    const int n = int(tracks_.size());
    const std::uint32_t play_mode = b3host::load<std::uint32_t>(manager + 0xCCu);
    if ((play_mode == 3 || play_mode == 4) && last_ >= 0 && last_ < n) return last_;   // repeat
    if (play_mode == 2) {   // shuffle: a deck, dealt out before a new shuffle
        if (deck_.size() != std::size_t(n) || deck_at_ >= deck_.size()) {
            deck_.resize(std::size_t(n));
            for (int k = 0; k < n; ++k) deck_[std::size_t(k)] = k;
            for (int k = n - 1; k > 0; --k) {
                rng_ ^= rng_ << 13, rng_ ^= rng_ >> 7, rng_ ^= rng_ << 17;
                std::swap(deck_[std::size_t(k)], deck_[std::size_t(rng_ % std::uint64_t(k + 1))]);
            }
            if (n > 1 && deck_[0] == last_) std::swap(deck_[0], deck_[1]);   // no song twice in a row
            deck_at_ = 0;
        }
        return deck_[deck_at_++];
    }
    return (last_ + 1) % n;   // in order
}

inline void CustomSoundtrack::end_virtual(const char* why) {
    if (!virtual_) return;
    if (std::getenv("B3NATIVE_MUSIC_LOG"))
        std::printf("custom_music: VBlank %llu: stream %08X %s at %u ms\n", static_cast<unsigned long long>(vb()), virtual_, why, player_.position_ms());
    virtual_ = 0;
    playing_ = -1;
    player_.close();
}

inline bool CustomSoundtrack::write_text(b3host::gaddr at, const std::string& utf8) {
    if (!text_checked_) {   // free guest RAM, once: nothing there yet
        text_checked_ = true;
        text_ok_ = true;
        for (b3host::gaddr k = kText; k < kTextEnd; k += 4)
            if (b3host::load<std::uint32_t>(k) != 0) {
                std::fprintf(stderr, "custom_music: guest RAM %08X is in use: the popup keeps the game's text\n", k);
                text_ok_ = false;
                break;
            }
    }
    if (!text_ok_) return false;
    const std::u16string u = b3music::popup_text(utf8, kShownUnits);
    b3host::gaddr p = at;
    for (std::size_t k = 0; k < u.size() && k < kTextUnits; ++k, p += 2) b3host::store<std::uint16_t>(p, std::uint16_t(u[k]));
    b3host::store<std::uint16_t>(p, 0);
    return true;
}

// func_003FBC20(manager, song, ...): the game's song start, then this song's source
inline void CustomSoundtrack::start_song(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    p->start_song_(a, r);
    const std::uint32_t manager = a.gpr[0].w[0];
    const int song = static_cast<int>(a.gpr[1].w[0]);
    if (song == -1) { if (manager == p->manager_) p->current_ = -1; return; }
    if (b3host::load<std::uint8_t>(manager + 0xBCu) == 0) return;   // a non-stream soundtrack (the Xbox's own; not on a PS2)
    std::string folder;
    const int m = p->mode(&folder);
    if (m == kEaTrax && p->current_ < 0 && p->virtual_ == 0) return;   // the default: nothing changes
    p->manager_ = manager;
    p->current_ = -1;
    if (m == kEaTrax) return;
    if (b3host::load<std::uint32_t>(manager + 0x178u) == 3) return;   // the EA TRAX page previews the game's songs
    if (!p->refresh(folder)) return;
    const std::uint64_t k = p->starts_++;
    if (m == kMixed && (k & 1)) return;   // Mixed: custom, game, custom, ...
    p->current_ = p->pick(manager);
    p->last_ = p->current_;
    const b3music::Track& t = p->tracks_[std::size_t(p->current_)];
    std::printf("custom_music: VBlank %llu: song %d (game's %d) is custom %d of %zu: %s - %s (%s)\n", static_cast<unsigned long long>(vb()),
                int(p->starts_), song, p->current_ + 1, p->tracks_.size(), t.tags.artist.c_str(), t.tags.title.c_str(), t.path.c_str());
}

// func_003865A0(stream, name, 0, start sample, segment, ...): the manager's stream with a custom song attaches nothing
inline void CustomSoundtrack::prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t stream = a.gpr[0].w[0];
    if (p->current_ < 0 || p->manager_ == 0 || b3host::load<std::uint32_t>(p->manager_ + 0xC0u) != stream) { p->prepare_(a, r); return; }
    if (!p->is_virtual(stream) || !p->player_.open() || p->playing_ != p->current_) {
        const std::uint32_t at = a.gpr[3].w[0];   // the start sample (a resume: the position we answered)
        std::string why;
        if (!p->player_.start(p->tracks_[std::size_t(p->current_)], at, &why)) {
            std::printf("custom_music: VBlank %llu: %s: the game's song plays instead\n", static_cast<unsigned long long>(vb()), why.c_str());
            p->current_ = -1;
            p->virtual_ = 0;
            p->playing_ = -1;
            p->prepare_(a, r);
            return;
        }
        p->virtual_ = stream;
        p->playing_ = p->current_;
        if (at == 0) ++p->songs_played_;
        std::printf("custom_music: VBlank %llu: stream %08X plays %s (%u Hz, from frame %u)\n", static_cast<unsigned long long>(vb()), stream,
                    p->player_.track().path.c_str(), p->player_.source_rate(), at);
    }
    b3host::store<std::uint8_t>(stream + 0x1F5u, 0);   // as func_003865A0's start, then "prepared" (Update's 1 -> 2)
    b3host::store<std::uint8_t>(stream + 0x1F6u, 0);
    b3host::store<float>(stream + 0x1C8u, -1.0f);
    b3host::store<std::uint32_t>(stream + 0x1D0u, 2);
    r.v0 = u_long128{};
    r.v0.w[0] = 1;
}

// Play__14CGtSoundStreamFb(stream, loop)
inline void CustomSoundtrack::play(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t stream = a.gpr[0].w[0];
    if (!p->is_virtual(stream)) { p->play_(a, r); return; }
    if (b3host::load<std::uint32_t>(stream + 0x1D0u) != 6) {
        b3host::store<std::uint32_t>(stream + 0x1D0u, 6);
        p->player_.play((a.gpr[1].w[0] & 0xFFu) != 0);
    }
    b3host::store<std::uint8_t>(stream + 0x1F5u, 0);
}

// Stop__14CGtSoundStreamFv(stream)
inline void CustomSoundtrack::stop(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t stream = a.gpr[0].w[0];
    if (!p->is_virtual(stream)) { p->stop_(a, r); return; }
    const std::uint32_t s = b3host::load<std::uint32_t>(stream + 0x1D0u);
    if (s != 5 && s != 4) {
        b3host::store<std::uint32_t>(stream + 0x1D0u, 5);
        p->player_.stop();
    }
    b3host::store<std::uint8_t>(stream + 0x1F6u, 0);
}

// Release__14CGtSoundStreamFv(stream): the fields as the game's, the player closed
inline void CustomSoundtrack::release(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t stream = a.gpr[0].w[0];
    if (!p->is_virtual(stream)) { p->release_(a, r); return; }
    b3host::store<std::uint32_t>(stream + 0x140u, 0);
    b3host::store<std::uint8_t>(stream + 0x1F4u, 0);
    b3host::store<std::uint32_t>(stream + 0x1D0u, 0);
    b3host::store<std::uint8_t>(stream + 0x1F6u, 0);
    b3host::store<std::uint8_t>(stream + 0x0u, 0);
    p->end_virtual("released");
    r.v0 = u_long128{};
    r.v0.w[0] = 1;
}

// Pause__14CGtSoundStreamFb(stream, on) (PauseAll: the pause menu, the loading pages)
inline void CustomSoundtrack::pause(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t stream = a.gpr[0].w[0];
    if (!p->is_virtual(stream)) { p->pause_(a, r); return; }
    const bool on = (a.gpr[1].w[0] & 0xFFu) != 0;
    const std::uint32_t s = b3host::load<std::uint32_t>(stream + 0x1D0u);
    if (s <= 2) return;
    if (on && b3host::load<std::uint8_t>(stream + 0x1F5u) == 0) {
        if (s == 6) b3host::store<std::uint8_t>(stream + 0x1F6u, 1);
        b3host::store<std::uint32_t>(stream + 0x1D0u, 8);
        b3host::store<std::uint8_t>(stream + 0x1F5u, 1);
        p->player_.pause(true);
    } else if (!on && b3host::load<std::uint8_t>(stream + 0x1F6u) == 1) {
        b3host::store<std::uint32_t>(stream + 0x1D0u, 6);
        b3host::store<std::uint8_t>(stream + 0x1F5u, 0);
        p->player_.pause(false);
    }
}

// Update__14CGtSoundStreamFv(stream), every poll: the song's end, the volume and pitch
inline void CustomSoundtrack::update(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t stream = a.gpr[0].w[0];
    if (!p->is_virtual(stream)) { p->update_(a, r); return; }
    const std::uint32_t s = b3host::load<std::uint32_t>(stream + 0x1D0u);
    if ((s == 6 && p->player_.finished()) || s == 5) {
        b3host::store<std::uint32_t>(stream + 0x1D0u, 4);
        if (s == 6)
            std::printf("custom_music: VBlank %llu: custom song ended (%u ms)\n", static_cast<unsigned long long>(vb()), p->player_.position_ms());
    }
    // ApplyVolumesAndPitch's values: the stream's volume x its category's gain, the category's pitch
    const std::uint32_t cat = b3host::load<std::uint8_t>(stream + 0x1ECu);
    const float vol = b3host::load<float>(stream + 0x1A4u);
    const float cat_pitch = b3host::load<float>(kCategories + 8u * cat), cat_gain = b3host::load<float>(kCategories + 8u * cat + 4u);
    const float g = game_music_muted() && cat == 13 ? 0.0f : vol * cat_gain;
    p->player_.set_gain(g == g ? g : 0.0f);
    p->player_.set_pitch(cat_pitch == cat_pitch && cat_pitch > 0.0f ? cat_pitch : 1.0f);
}

// ApplyVolumesAndPitch(stream): the music category's gain is 0 while another music app plays (the value is put back at once)
inline void CustomSoundtrack::apply_volumes(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t stream = a.gpr[0].w[0];
    if (game_music_muted() && stream != 0 && b3host::load<std::uint8_t>(stream + 0x1ECu) == 13) {
        const b3host::gaddr at = kCategories + 8u * 13u + 4u;
        const float keep = b3host::load<float>(at);
        b3host::store<float>(at, 0.0f);
        p->apply_volumes_(a, r);
        b3host::store<float>(at, keep);
        return;
    }
    p->apply_volumes_(a, r);
}

// func_00386080(stream, ms): 1 the position in ms, 0 in samples (the song's own frames; func_003FC2E0's resume point)
inline void CustomSoundtrack::position(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t stream = a.gpr[0].w[0];
    if (!p->is_virtual(stream)) { p->position_(a, r); return; }
    const std::uint32_t s = b3host::load<std::uint32_t>(stream + 0x1D0u);
    std::uint32_t v = 0;
    if (s == 6 || s == 8) v = a.gpr[1].w[0] ? p->player_.position_ms() : std::uint32_t(p->player_.source_frame());
    r.v0 = u_long128{};
    r.v0.w[0] = v;
}

// func_00386020(stream, ...): the stream's block in samples (the resume point is rounded up to it): 1 frame
inline void CustomSoundtrack::block(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    if (!p->is_virtual(a.gpr[0].w[0])) { p->block_(a, r); return; }
    r.v0 = u_long128{};
    r.v0.w[0] = 1;
}

// func_00438F10(display, title, artist, album): the now-playing popup
inline void CustomSoundtrack::show_popup(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    if (game_music_muted()) { r.v0 = u_long128{}; return; }   // another app's song is shown instead (now_playing_popup.h)
    if (std::getenv("B3NATIVE_MUSIC_LOG")) {   // the game's own texts (as UTF-16 -> ASCII)
        auto text = [](std::uint32_t at) {
            std::string s;
            for (std::uint32_t k = 0; at && k < 80; ++k) {
                const std::uint16_t c = b3host::load<std::uint16_t>(at + 2u * k);
                if (!c) break;
                s.push_back(c < 0x80 ? char(c) : '?');
            }
            return s;
        };
        std::printf("custom_music: VBlank %llu: popup asked: \"%s\" / \"%s\" / \"%s\"\n", static_cast<unsigned long long>(vb()),
                    text(a.gpr[1].w[0]).c_str(), text(a.gpr[2].w[0]).c_str(), text(a.gpr[3].w[0]).c_str());
    }
    if (p->virtual_ == 0 || !p->player_.open() || p->manager_ == 0 || b3host::load<std::uint32_t>(p->manager_ + 0xC0u) != p->virtual_) {
        p->show_popup_(a, r);
        return;
    }
    const b3music::Tags& t = p->player_.track().tags;   // the song playing (a copy: the folder may have been rescanned)
    const b3host::gaddr title = kText, artist = kText + 0x100u, album = kText + 0x200u;
    if (!p->write_text(title, t.title) || !p->write_text(artist, t.artist) || !p->write_text(album, t.album)) {
        p->show_popup_(a, r);
        return;
    }
    b3host::CallArgs b = a;
    b.gpr[1] = u_long128{};
    b.gpr[1].w[0] = title;
    b.gpr[2] = u_long128{};
    b.gpr[2].w[0] = artist;
    b.gpr[3] = u_long128{};
    b.gpr[3].w[0] = album;
    ++p->popups_;
    std::printf("custom_music: VBlank %llu: popup: \"%s\" / \"%s\" / \"%s\"\n", static_cast<unsigned long long>(vb()), t.title.c_str(),
                t.artist.c_str(), t.album.c_str());
    p->show_popup_(b, r);
}

}  // namespace b3native

#endif
