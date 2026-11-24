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
// The EA TRAX list [C decomp]: the disc's 44 songs are D_004A5600 (0x18 bytes each: +0 the index, +8 / +0xC / +0x10
// the title / album / artist string ids, +0x14 the per-song bits: 7 both, 1 races, 4 menus, 0 off, 8 "not heard yet":
// such a song cannot be set to off), followed by the soundtrack's descriptor D_004A5A20 (+4 the count, +0x4C the
// songs). Everything gets that descriptor through func_003FBB50(radio, 0, out) (a copy) and the radio takes it over
// in func_003FC060 (+0x6C / +0xB4). The EA TRAX page (CB3MyBurnoutSettingsEATraxPage: func_00431D80 prepare,
// func_00431860 / func_00431160 its three rows of artist / title / album lines centred on the selection,
// func_004313E0 its update; left / right through func_00431050 -> func_003FBAA0) reads the radio's count, so it scrolls
// through any number of songs. The profile's copy of the bits is the options block D_004F5040 [0..43] (bytes 44 on are
// other options): the page's confirm and back loops, func_0028B380 at boot and func_003FCC10 go over it for the
// descriptor's count. func_003FC8C0 clears D_004F5040[song] bit 8 when a song ends; the next song (func_003FBDB0) and
// the page's "play from here" (func_003FB8B0) shuffle through a byte array at radio +0xD..+0x3F (51 songs at most).
//
// What this adds (PLAYLIST at Custom only or Mixed, a folder with songs; EA Trax only, the default, installs the
// wrappers but they pass every call through untouched):
//   - the list: the 44 songs copied into a table in free guest RAM (GuestArena::kEaTraxSongs) and the folder's songs
//     after them (at most kMaxCustom, by path); func_003FBB50 hands that table out with the count left at 44 (so the
//     game's own loops over the profile stay within its 44 bytes) and func_003FC060 gives the radio the whole count.
//     While the list is up the table is the only live copy of the 44 songs' bits (func_003FCC10's profile copy is
//     taken into it; they go back to D_004A5600 when the list is taken down).
//   - the rows' text: the custom songs' string ids are kStringBase + 4k (+0 title, +1 album, +2 artist); the string
//     lookup func_00271A00 answers them with the song's tags, UTF-16 in a ring in free guest RAM (kEaTraxText;
//     Latin-1 kept, other characters '?', kRowUnits per line as the page's 128-unit row buffer allows).
//   - the picks: func_003FBDB0 / func_003FB8B0 run here for a list longer than 44 (the same steps, the same sound
//     random generator CGtSoundUtils::mRandom, the shuffle order host-side instead of the 51-byte array). Mixed: every
//     song by its own mode; Custom only: the same, but the disc's songs are never picked (func_003FB7C0, "may the
//     current song play here", says no for them); no custom song allowed here: silence, as the game's with every song off.
//   - D_004F5040[song] is put back after func_003FC8C0 for a custom song (the byte is another option).
//   - the save keeps the disc's songs only: func_00421AF0 (the card's save image) is preceded by the options' last song
//     D_004F5077 put back to the last disc song started when it is a custom index (a save valid on a PS2 / emulator).
//   - rows 100 on: the page's row numbers have room for two digits; func_00431160's rows draw them from kNumbers.
//   - the folder: only files a decoder opens are listed (the first kMaxCustom by path); none: as EA Trax only.
//   - the page: a custom row previews its song as the game's rows do; confirm keeps the custom songs' modes (the
//     host-side ModeStore, beside pc_settings.ini: music_modes.ini; B3NATIVE_MUSIC_MODES_FILE overrides it, "-": none),
//     back puts them back as they were.
// A song start (func_003FBC20) of a custom row's index plays that song: the music stream's prepare then attaches no
// media and answers "prepared", and that stream object -- the "virtual" stream -- is played by the host player instead
// of RW Audio: Play / Pause / Stop / Release / Update / the position / the block size of that one object are answered
// here with the same status changes the game's code makes, the player follows its volume x the category gain each
// poll (RADIO VOLUME, the menus' ducking, the fades; pause stops it as PauseAll pauses the stream), and the song's end is
// the stream's end (status 4: the game picks the next song, the DJ follows as usual). A file that cannot be opened ends
// at once. The player is mixed into the SPU2 mixer's 48 kHz output (platform_rwa.cpp set_music_overlay), on virtual
// time. The popup: func_00438F10 is given the custom song's title / artist / album, UTF-16 in free guest RAM
// 0xC0C00..0xC0F00 (GuestArena::kCustomSoundtrack). Each region is checked unused before its first write, as
// date_format.h does. The wrappers are on the hook table (hook_table.h, port layer).
//
// Replays: native_play records each folder's modes as the replay option "music_modes.<folder>" (one digit per song
// in scan order, modes_digits()) and a replay played back uses them (seed_modes()) and never writes the file.
//
// Settings: PC SETTINGS > AUDIO > MUSIC (b3r_music: the folder and the mode; b3r_music_volume: CUSTOM MUSIC VOLUME);
// B3NATIVE_MUSIC_DIR / B3NATIVE_MUSIC_MODE (0 / 1 / 2) / B3NATIVE_MUSIC_VOLUME (0..200) override them (tests).
// B3NATIVE_MUSIC_LOG=1 logs the game's own popup texts too, and the music stream's gain (the stream's volume, the
// category's gain) whenever it changes, for the game's own songs and the custom ones.
//
// The level (musicvol1, 1.2.1): the player's gain is the game's own for its music stream (the stream's volume, which
// is RADIO VOLUME x the radio's ducking, times the music category's gain), times kStreamLevel, times CUSTOM MUSIC
// VOLUME. kStreamLevel is what RW Audio and the SPU2 add between that gain and the output for the game's own songs
// (ApplyVolumesAndPitch -> _rwaStreamSetStreamGain -> the IOP's voice volumes, written halved to VOLL / VOLR and doubled
// back by the mixer, (s * level) >> 15; MVOL 0x3FFF -> level 0x7FFE, unity): measured, it is unity. The same 30 s of
// "Give It Up" in the same run (the 100 % card, VBlanks 16500-18300, the menus), once as the game's own EA Trax stream
// and once as a player's 320 kbps rip of that stream in a music folder: output RMS -19.09 dBFS and -18.98 dBFS (0.11 dB
// apart, within the rip's second lossy encode); the rip through the player came out at exactly the stream's gain
// (0.4495 there). So a custom song at 100 % plays as loud as a game song mastered as loud (the disc's songs are about
// -8 LUFS, a modern master's level); a quieter file is quieter, which CUSTOM MUSIC VOLUME is for.
//
// CUSTOM MUSIC LOUDNESS (musicloud, default Even out): each custom song's EBU R128 integrated loudness and sample peak
// (b3music::LoudnessMeter) bring it to b3music::kLoudnessTarget (-10 LUFS: the disc's songs at their source level,
// measured in the menus as the output over the stream's gain: "Give It Up" -8.8, the front end's piece -11.3), at most
// 20 dB down and 12 dB up, and up only as far as its peaks go 3 dB over the player's Limiter (a dynamic song is raised
// as far as it stays clean). The folder's songs are measured on a worker thread from the scan on (b3music::
// LoudnessService) and kept in music_loudness.txt beside pc_settings.ini (path, size, time: a changed file is measured
// again, a gone one dropped at the load; B3NATIVE_MUSIC_LOUDNESS_FILE overrides it, "-": none); a song not measured yet
// when it starts is measured then, in the game's prepare call, if small (LoudnessService::kNowBytes: a few hundred ms
// at most); a bigger one plays as the file is until the worker (which takes it next) has it, then eases to its gain. A
// song's gain only depends on its file; only when a big unmeasured song's gain arrives depends on the timing.
// The gain multiplies the stream gain above; Off: 1. B3NATIVE_MUSIC_LOUDNESS=0|1 overrides the setting (tests).
#ifndef B3NATIVE_CUSTOM_SOUNDTRACK_H
#define B3NATIVE_CUSTOM_SOUNDTRACK_H

#include "custom_music.h"
#include "b3port/fileio.h"   // fsrobust
#include "hook_table.h"
#include "platform.h"
#include "save_store.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
    // volume(): CUSTOM MUSIC VOLUME in percent (0..200, 100 = as loud as the game's own songs); read every poll (null: 100)
    using VolumeFn = int (*)();
    void set_volume_source(VolumeFn volume) { volume_ = volume; }
    // loudness(): CUSTOM MUSIC LOUDNESS (musicloud): 1 Even out (each song's measured level brought to the game's own
    // songs' level, b3music::loudness_gain), 0 Off (the file's own level); read every poll (null: 1)
    void set_loudness_source(VolumeFn loudness) { loudness_ = loudness; }
    // RW Audio + the SPU2 between the music stream's gain and the output, for the game's own songs: unity (the header)
    static constexpr float kStreamLevel = 1.0f;
    static float custom_volume(int percent) { return float(std::clamp(percent, 0, 200)) / 100.0f; }
    ~CustomSoundtrack();

    static constexpr b3host::gaddr kStartSong = 0x003FBC20u, kPrepare = 0x003865A0u, kPlay = 0x00386290u,
                                   kStop = 0x00386240u, kRelease = 0x003863C0u, kPause = 0x00385F20u,
                                   kUpdate = 0x00386410u, kPosition = 0x00386080u, kBlock = 0x00386020u,
                                   kShowPopup = 0x00438F10u,
                                   kApplyVolumes = 0x00386130u;   // ApplyVolumesAndPitch__14CGtSoundStreamFv
    // the EA TRAX list (the header above)
    static constexpr b3host::gaddr kTrax = 0x003FBB50u,          // func_003FBB50(radio, soundtrack, out): the descriptor
                                   kSetSoundtrack = 0x003FC060u, // func_003FC060(radio, descriptor, song, play mode)
                                   kProfileSongs = 0x003FCC10u,  // func_003FCC10(radio): the profile's bits -> D_004A5600
                                   kNextSong = 0x003FBDB0u,      // func_003FBDB0(radio) -> the next song, -1 none
                                   kPlayFrom = 0x003FB8B0u,      // func_003FB8B0(radio, song): the order from that song
                                   kSongAllowed = 0x003FB7C0u,   // func_003FB7C0(radio): the current song may play here
                                   kRunStream = 0x003FC8C0u,     // func_003FC8C0(radio): the music stream, every poll
                                   kPageUpdate = 0x004313E0u,    // the EA TRAX page's Update
                                   kLookup = 0x00271A00u,        // func_00271A00(string table, id) -> UTF-16 text
                                   kSaveImage = 0x00421AF0u,     // func_00421AF0: the card's save image (options, profiles, checksum)
                                   kPageRows = 0x00431160u;      // func_00431160(page): the page's three rows filled in
    static constexpr b3host::gaddr kLastSong = 0x004F5077u;      // D_004F5077: the options' last song (saved; header +0x37)
    static constexpr b3host::gaddr kGameSongs = 0x004A5600u, kOptions = 0x004F5040u, kStrings = 0x0051BA44u,
                                   kInput = 0x004F50C0u, kMenuState = 0x01E90424u, kGetMenuConfirm = 0x0022CFE0u,
                                   kGetMenuBack = 0x0022CF60u, kSoundRandom = 0x004E2E78u, kInRace = 0x0064D370u,
                                   kGame = 0x004EE040u, kCrashCheck = 0x00131E40u;
    static constexpr std::uint32_t kGameSongCount = 44, kEntry = 0x18, kMaxCustom = 200;
    static constexpr std::uint32_t kStringBase = 0xF000u;   // custom song k: +4k title, +1 album, +2 artist (the disc has 0xFA5 ids)
    static constexpr b3host::gaddr kTable = GuestArena::kEaTraxSongs.address, kRing = GuestArena::kEaTraxText.address;
    static_assert((kGameSongCount + kMaxCustom) * kEntry <= GuestArena::kEaTraxSongs.size, "the song table fits its region");
    static constexpr std::uint32_t kRingSlot = 0x80, kRingSlots = GuestArena::kEaTraxText.size / kRingSlot - 1;
    // the rows' numbers: the page keeps them in unsigned short [3][3] (+0x4: two digits); from row 100 on they are
    // drawn from here instead (three 4-unit texts in the region's last slot)
    static constexpr b3host::gaddr kNumbers = kRing + kRingSlots * kRingSlot;
    // per line of a row: the page's row buffer is 128 units for artist "\n" title "\n" album and its terminator
    static constexpr std::size_t kRowUnits = 40;
    static_assert(3 * kRowUnits + 2 + 1 <= 128 && kRowUnits < kRingSlot / 2, "a row's three lines fit the page's buffer");

    // Another music app is playing (PC SETTINGS > AUDIO > MUSIC > OTHER MUSIC APPS): the game's music category (13) is
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

    // replays (native_play): a folder's modes as digits in scan order (the store's, loaded when first asked), and a
    // replay's digits put in place of the store's for that folder (the file is then never written in this run)
    static std::string modes_digits(const std::string& folder);
    static void seed_modes(const std::string& folder, const std::string& digits);
    // the store's file: B3NATIVE_MUSIC_MODES_FILE ("-": none, the modes kept for this run only), else
    // <user data>/music_modes.ini
    static std::string modes_file();

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
    static void trax(const b3host::CallArgs& a, b3host::CallResult& r);
    static void set_soundtrack(const b3host::CallArgs& a, b3host::CallResult& r);
    static void profile_songs(const b3host::CallArgs& a, b3host::CallResult& r);
    static void next_song(const b3host::CallArgs& a, b3host::CallResult& r);
    static void play_from(const b3host::CallArgs& a, b3host::CallResult& r);
    static void song_allowed(const b3host::CallArgs& a, b3host::CallResult& r);
    static void run_stream(const b3host::CallArgs& a, b3host::CallResult& r);
    static void page_update(const b3host::CallArgs& a, b3host::CallResult& r);
    static void lookup(const b3host::CallArgs& a, b3host::CallResult& r);
    static void save_image(const b3host::CallArgs& a, b3host::CallResult& r);
    static void page_rows(const b3host::CallArgs& a, b3host::CallResult& r);
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

    // the modes store (one per process: native_play's replay code reaches it before the game runs)
    struct Store {
        b3music::ModeStore modes;
        bool loaded = false, read_only = false;
    };
    static Store& store() {
        static Store s;
        if (!s.loaded) {
            s.loaded = true;
            const std::string f = modes_file();
            s.modes.load(f);
            s.read_only = f.empty();
        }
        return s;
    }

    int mode(std::string* folder) const;
    bool refresh(const std::string& folder);   // the folder's songs (scanned when the folder changes)
    // the list as the settings want it now: up (built or rebuilt for the folder) or down; true: up
    bool sync_list();
    void take_down();
    bool listed_radio(std::uint32_t radio) const {   // the radio plays from the list
        return listed_ && b3host::load<std::uint32_t>(radio + 0xB4u) == kTable;
    }
    bool regions_free();
    std::uint8_t context_mode() const;          // func_003FB7C0_inl0: 4 menus, 1 races, 2 (func_00131E40)
    bool allowed(std::uint32_t song, std::uint8_t m) const;
    static std::uint32_t sound_random();        // CGtSoundUtils' generator (func_003FBDB0's Rnd)
    void keep_modes();                          // the custom rows' bits -> the store (the page's confirm)
    void restore_modes();                       // the store -> the custom rows' bits (the page's back)
    bool is_virtual(std::uint32_t stream) const { return stream != 0 && stream == virtual_; }
    void end_virtual(const char* why);
    bool write_text(b3host::gaddr at, const std::string& utf8);
    void log_gain(bool custom, float vol, float cat, float user) {   // B3NATIVE_MUSIC_LOG: the music stream's gain, on a change
        if (custom == logged_virtual_ && std::fabs(vol - logged_vol_) < 0.005f && std::fabs(cat - logged_cat_) < 0.005f && user == logged_user_) return;
        logged_virtual_ = custom;
        logged_vol_ = vol;
        logged_cat_ = cat;
        logged_user_ = user;
        std::printf("custom_music: VBlank %llu: %s music stream gain: volume %.3f x category %.3f%s = %.3f\n",
                    static_cast<unsigned long long>(vb()), custom ? "custom" : "game's", vol, cat,
                    custom ? (" x custom volume " + std::to_string(int(std::lround(user * 100.0f))) + "%").c_str() : "",
                    vol * cat * (custom ? kStreamLevel * user : 1.0f));
    }
    static std::uint64_t vb() { return platform::vblank_count(); }

    Fn start_song_ = nullptr, prepare_ = nullptr, play_ = nullptr, stop_ = nullptr, release_ = nullptr, pause_ = nullptr,
       update_ = nullptr, position_ = nullptr, block_ = nullptr, show_popup_ = nullptr, apply_volumes_ = nullptr,
       trax_ = nullptr, set_soundtrack_ = nullptr, profile_songs_ = nullptr, next_song_ = nullptr, play_from_ = nullptr,
       song_allowed_ = nullptr, run_stream_ = nullptr, page_update_ = nullptr, lookup_ = nullptr, save_image_ = nullptr, page_rows_ = nullptr;
    ConfigFn config_ = nullptr;
    VolumeFn volume_ = nullptr;
    VolumeFn loudness_ = nullptr;              // musicloud
    b3music::LoudnessService loud_;            // musicloud: the folder's songs measured (a worker thread, a cache file)
    std::string loud_folder_;                  // the folder the worker was started for
    float song_gain_ = 1.0f;                   // the playing song's CUSTOM MUSIC LOUDNESS gain
    bool song_measured_ = false;               // song_gain_ is the song's (else 1 until it is)
    unsigned loud_wait_ = 0;                   // polls until the next lookup (switched on during an unmeasured song)
    void loudness_start();                     // the worker on the folder's songs (Even out; once per folder)
    void loudness_song(bool may_measure);      // song_gain_ for the player's song
    static std::string loudness_file();
    float logged_vol_ = -1.0f, logged_cat_ = -1.0f, logged_user_ = -1.0f;   // B3NATIVE_MUSIC_LOG: the gain last logged
    bool logged_virtual_ = false;
    std::string folder_;                       // the folder scanned
    std::vector<b3music::Track> tracks_;       // its songs (at most kMaxCustom)
    std::size_t found_ = 0;                    // the files found there (tracks_ is the first kMaxCustom)
    std::uint32_t listed_ = 0;                 // the custom songs in the table (0: the list is down)
    std::string listed_folder_;                // the folder they are of
    int mode_ = kEaTrax;                       // the playlist mode at the last sync
    std::uint8_t last_game_song_ = 0;          // the last of the disc's songs started (what a save keeps instead of a custom one)
    std::vector<int> deck_;                    // the shuffle order of a long list (the game's: radio +0xD, 51 bytes)
    int deck_at_ = 0;                          // radio +0x40's part
    std::uint32_t deck_n_ = 0;                 // radio +0x44's part
    bool regions_checked_ = false, regions_ok_ = false;
    std::uint32_t ring_at_ = 0;
    std::uint32_t manager_ = 0;                // the radio of the song start
    static constexpr int kSkip = -3;           // current_: a custom row that is no song now (it ends at once)
    int current_ = -1;                         // the custom song of the radio's current song, -1: the game's
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
    if (player_.limiter().limited())   // musicvol1: the limiter at work (a loud song at a high gain)
        std::printf("custom_music: limiter: %llu frames turned down (%.1f s), lowest gain %.3f\n",
                    static_cast<unsigned long long>(player_.limiter().limited()), double(player_.limiter().limited()) / b3music::kOutputRate,
                    double(player_.limiter().min_gain()));
}

// musicloud: the cache of measured songs: B3NATIVE_MUSIC_LOUDNESS_FILE ("-": none, measured for this run only), else
// beside pc_settings.ini: music_loudness.txt
inline std::string CustomSoundtrack::loudness_file() {
    if (const char* e = std::getenv("B3NATIVE_MUSIC_LOUDNESS_FILE"); e && *e) return std::string(e) == "-" ? std::string() : std::string(e);
    const std::filesystem::path d = save::user_data_dir();
    if (d.empty()) return {};
    const std::u8string u = (d / "music_loudness.txt").u8string();
    return std::string(u.begin(), u.end());
}

inline void CustomSoundtrack::loudness_start() {
    if (tracks_.empty() || loud_folder_ == folder_) return;
    loud_folder_ = folder_;
    loud_.set_cache_file(loudness_file());
    std::vector<std::string> paths;
    paths.reserve(tracks_.size());
    for (const b3music::Track& t : tracks_) paths.push_back(t.path);
    loud_.start(paths);
}

inline void CustomSoundtrack::loudness_song(bool may_measure) {
    if (!player_.open()) return;
    const std::string& path = player_.track().path;
    float g = 1.0f;
    bool cached = loud_.lookup(path, g);
    if (!cached && !may_measure) return;
    const auto t0 = std::chrono::steady_clock::now();
    if (!cached && !loud_.need(path, g)) {   // a big song: the worker's next; gain 1 until its result (update())
        std::printf("custom_music: VBlank %llu: loudness: a big file, measured in the background (as the file is until then)\n",
                    static_cast<unsigned long long>(vb()));
        return;
    }
    song_gain_ = g;
    song_measured_ = true;
    b3music::LoudnessCache::Entry e;
    const bool have = loud_.cache().get(path, e);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (have && std::isfinite(e.lufs))
        std::printf("custom_music: VBlank %llu: loudness %.2f LUFS, peak %.1f dBFS: gain %+.1f dB (%s)\n", static_cast<unsigned long long>(vb()),
                    e.lufs, 20.0 * std::log10(std::max(e.peak, 1e-9)), 20.0 * std::log10(double(g)),
                    cached ? (may_measure ? "measured before" : "measured in the background: from here")
                           : ("measured now, " + std::to_string(int(ms)) + " ms").c_str());
    else
        std::printf("custom_music: VBlank %llu: loudness: none measured (silent or unreadable): as the file is\n", static_cast<unsigned long long>(vb()));
}

inline std::string CustomSoundtrack::modes_file() {
    if (const char* e = std::getenv("B3NATIVE_MUSIC_MODES_FILE"); e && *e) return std::string(e) == "-" ? std::string() : std::string(e);
    const std::filesystem::path d = save::user_data_dir();
    if (d.empty()) return {};
    const std::u8string u = (d / "music_modes.ini").u8string();
    return std::string(u.begin(), u.end());
}

inline std::string CustomSoundtrack::modes_digits(const std::string& folder) {
    Store& s = store();
    std::string d;
    for (const std::string& f : b3music::list_audio_files(folder)) {
        if (d.size() >= kMaxCustom) break;
        d.push_back(char('0' + (s.modes.get(folder, b3music::relative_key(folder, f)) & 7u)));
    }
    return d;
}

inline void CustomSoundtrack::seed_modes(const std::string& folder, const std::string& digits) {
    Store& s = store();
    if (!s.read_only) s.modes.clear();   // a replay's modes only: the file's are not read or written in this run
    s.read_only = true;
    std::size_t k = 0;
    for (const std::string& f : b3music::list_audio_files(folder)) {
        if (k >= digits.size() || k >= kMaxCustom) break;
        const char c = digits[k++];
        s.modes.set(folder, b3music::relative_key(folder, f), std::uint8_t(c >= '0' && c <= '7' ? c - '0' : b3music::kModeBoth));
    }
}

inline bool CustomSoundtrack::install(HookTable& hooks, ConfigFn config) {
    const std::vector<Hook> set = {
        {kStartSong, HookLayer::kPort, &start_song, &start_song_}, {kPrepare, HookLayer::kPort, &prepare, &prepare_},
        {kPlay, HookLayer::kPort, &play, &play_}, {kStop, HookLayer::kPort, &stop, &stop_},
        {kRelease, HookLayer::kPort, &release, &release_}, {kPause, HookLayer::kPort, &pause, &pause_},
        {kUpdate, HookLayer::kPort, &update, &update_}, {kPosition, HookLayer::kPort, &position, &position_},
        {kBlock, HookLayer::kPort, &block, &block_}, {kShowPopup, HookLayer::kPort, &show_popup, &show_popup_},
        {kApplyVolumes, HookLayer::kPort, &apply_volumes, &apply_volumes_},
        {kTrax, HookLayer::kPort, &trax, &trax_}, {kSetSoundtrack, HookLayer::kPort, &set_soundtrack, &set_soundtrack_},
        {kProfileSongs, HookLayer::kPort, &profile_songs, &profile_songs_}, {kNextSong, HookLayer::kPort, &next_song, &next_song_},
        {kPlayFrom, HookLayer::kPort, &play_from, &play_from_}, {kSongAllowed, HookLayer::kPort, &song_allowed, &song_allowed_},
        {kRunStream, HookLayer::kPort, &run_stream, &run_stream_}, {kPageUpdate, HookLayer::kPort, &page_update, &page_update_},
        {kLookup, HookLayer::kPort, &lookup, &lookup_}, {kSaveImage, HookLayer::kPort, &save_image, &save_image_},
        {kPageRows, HookLayer::kPort, &page_rows, &page_rows_},
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
    if (const auto e = b3io::getenv("B3NATIVE_MUSIC_DIR"); e && folder) *folder = *e;   // fsrobust: UTF-8
    return (m >= kEaTrax && m <= kMixed) ? m : kEaTrax;
}

inline bool CustomSoundtrack::refresh(const std::string& folder) {
    if (folder == folder_) return !tracks_.empty();
    folder_ = folder;
    tracks_.clear();
    found_ = 0;
    if (playing_ >= 0) playing_ = -2;   // still playing, but no longer an index of tracks_
    if (current_ >= 0) current_ = kSkip;   // robust2: nor is the radio's custom song (it ends at once, the game goes on)
    if (folder.empty()) return false;
    const auto t0 = std::chrono::steady_clock::now();
    std::string why;
    std::vector<std::string> files = b3music::list_audio_files(folder, &why);
    found_ = files.size();
    if (files.size() > kMaxCustom) files.resize(kMaxCustom);   // the first kMaxCustom by path
    tracks_.reserve(files.size());
    std::size_t bad = 0;
    for (const std::string& f : files) {   // only files a decoder opens are listed (none: as EA Trax only)
        std::string no;
        if (!b3music::Decoder::open(f, &no)) {
            if (++bad <= 5) std::printf("custom_music: %s: %s: not listed\n", f.c_str(), no.c_str());
            continue;
        }
        tracks_.push_back(b3music::Track{f, b3music::read_tags(f)});
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("custom_music: VBlank %llu: %zu songs in %s, %zu playable (%.0f ms)%s%s%s\n", static_cast<unsigned long long>(vb()), found_,
                folder.c_str(), tracks_.size(), ms, found_ > kMaxCustom ? ": the first 200 are listed" : "", why.empty() ? "" : ": ", why.c_str());
    if (!loudness_ || loudness_()) loudness_start();   // musicloud
    return !tracks_.empty();
}

inline bool CustomSoundtrack::regions_free() {
    if (regions_checked_) return regions_ok_;
    regions_checked_ = true;
    regions_ok_ = true;
    for (const ArenaRegion& g : {GuestArena::kEaTraxSongs, GuestArena::kEaTraxText})
        for (b3host::gaddr k = g.address; k < g.address + g.size; k += 4)
            if (b3host::load<std::uint32_t>(k) != 0) {
                std::fprintf(stderr, "custom_music: guest RAM %08X is in use: the EA TRAX list keeps the game's 44 songs\n", k);
                regions_ok_ = false;
                return false;
            }
    return true;
}

// the list as the settings want it: the custom songs after the 44 (a folder with songs, PLAYLIST not EA Trax only)
inline bool CustomSoundtrack::sync_list() {
    std::string folder;
    const int m = mode(&folder);
    mode_ = m;
    if (m == kEaTrax || folder.empty()) {
        if (listed_) take_down();
        return false;
    }
    if (!refresh(folder) || !regions_free()) {
        if (listed_) take_down();
        return false;
    }
    const std::uint32_t n = std::uint32_t(tracks_.size());
    if (listed_ == n && listed_folder_ == folder_) return true;
    if (!listed_)   // the disc's 44 as they are now (from here on the table is their live copy)
        for (std::uint32_t k = 0; k < kGameSongCount * kEntry; k += 4)
            b3host::store<std::uint32_t>(kTable + k, b3host::load<std::uint32_t>(kGameSongs + k));
    Store& s = store();
    for (std::uint32_t k = 0; k < n; ++k) {
        const b3host::gaddr e = kTable + (kGameSongCount + k) * kEntry;
        b3host::store<std::uint32_t>(e + 0x0u, kGameSongCount + k);
        b3host::store<std::uint32_t>(e + 0x4u, 0);
        b3host::store<std::uint32_t>(e + 0x8u, kStringBase + 4 * k + 0);    // title
        b3host::store<std::uint32_t>(e + 0xCu, kStringBase + 4 * k + 1);    // album
        b3host::store<std::uint32_t>(e + 0x10u, kStringBase + 4 * k + 2);   // artist
        b3host::store<std::uint32_t>(e + 0x14u, s.modes.get(folder_, b3music::relative_key(folder_, tracks_[k].path)));
    }
    listed_ = n;
    listed_folder_ = folder_;
    deck_n_ = 0;   // a new shuffle
    // the radio from the next song (its soundtrack is the EA TRAX one and plays from either table)
    const b3host::gaddr radio = 0x01E7A888u;
    const std::uint32_t b4 = b3host::load<std::uint32_t>(radio + 0xB4u);
    if (b3host::load<std::uint8_t>(radio + 0xBCu) != 0 && (b4 == kGameSongs || b4 == kTable)) {
        const std::uint32_t d8 = b3host::load<std::uint32_t>(radio + 0xD8u);
        b3host::store<std::uint32_t>(radio + 0xB4u, kTable);
        b3host::store<std::uint32_t>(radio + 0x6Cu, kGameSongCount + n);
        if (d8 != 0xFFFFFFFFu && d8 < kGameSongCount + n) b3host::store<std::uint32_t>(radio + 0xC4u, kTable + d8 * kEntry);
    }
    std::printf("custom_music: VBlank %llu: EA TRAX lists %u songs (44 + %u of %s)\n", static_cast<unsigned long long>(vb()),
                kGameSongCount + n, n, folder_.c_str());
    return true;
}

// the list down: the 44 songs' bits back to D_004A5600, the radio back on it
inline void CustomSoundtrack::take_down() {
    for (std::uint32_t k = 0; k < kGameSongCount; ++k)
        b3host::store<std::uint32_t>(kGameSongs + k * kEntry + 0x14u, b3host::load<std::uint32_t>(kTable + k * kEntry + 0x14u));
    const b3host::gaddr radio = 0x01E7A888u;
    if (b3host::load<std::uint32_t>(radio + 0xB4u) == kTable) {
        const std::uint32_t d8 = b3host::load<std::uint32_t>(radio + 0xD8u);
        b3host::store<std::uint32_t>(radio + 0xB4u, kGameSongs);
        b3host::store<std::uint32_t>(radio + 0x6Cu, kGameSongCount);
        if (d8 < kGameSongCount) b3host::store<std::uint32_t>(radio + 0xC4u, kGameSongs + d8 * kEntry);
    }
    std::printf("custom_music: VBlank %llu: EA TRAX lists the game's 44 songs\n", static_cast<unsigned long long>(vb()));
    listed_ = 0;
    listed_folder_.clear();
}

inline std::uint8_t CustomSoundtrack::context_mode() const {
    if (b3host::load<std::uint64_t>(kInRace) != 0)
        return (b3host::call_guest<int>(kCrashCheck, kGame) & 0xFF) != 0 ? 2 : 1;
    return 4;
}

inline bool CustomSoundtrack::allowed(std::uint32_t song, std::uint8_t m) const {
    if (mode_ == kCustom && song < kGameSongCount) return false;   // Custom only: the disc's songs are not picked
    return (m & b3host::load<std::uint32_t>(kTable + song * kEntry + 0x14u)) != 0;
}

inline std::uint32_t CustomSoundtrack::sound_random() {   // as func_003FBDB0's Rnd (CGtSoundUtils::mRandom)
    const std::int32_t a = b3host::load<std::int32_t>(kSoundRandom);
    std::int32_t x = std::int32_t(std::uint32_t(a) << 16) + (a >> 16);
    x = std::int32_t(std::uint32_t(x) + b3host::load<std::uint32_t>(kSoundRandom + 4u));
    b3host::store<std::uint32_t>(kSoundRandom + 4u, b3host::load<std::uint32_t>(kSoundRandom + 4u) + std::uint32_t(x));
    b3host::store<std::int32_t>(kSoundRandom, x);
    return std::uint32_t(x);
}

inline void CustomSoundtrack::keep_modes() {
    Store& s = store();
    bool changed = false;
    for (std::uint32_t k = 0; k < listed_ && k < tracks_.size(); ++k) {
        const std::uint8_t m = std::uint8_t(b3host::load<std::uint32_t>(kTable + (kGameSongCount + k) * kEntry + 0x14u) & 7u);
        const std::string key = b3music::relative_key(listed_folder_, tracks_[k].path);
        if (s.modes.get(listed_folder_, key) != m) {
            s.modes.set(listed_folder_, key, m);
            changed = true;
        }
    }
    if (!changed) return;
    std::string why;
    if (s.read_only) std::printf("custom_music: VBlank %llu: the custom songs' modes kept for this run\n", static_cast<unsigned long long>(vb()));
    else if (s.modes.save(&why)) std::printf("custom_music: VBlank %llu: the custom songs' modes saved in %s\n", static_cast<unsigned long long>(vb()), s.modes.file().c_str());
    else std::fprintf(stderr, "custom_music: the custom songs' modes are not saved: %s\n", why.c_str());
}

inline void CustomSoundtrack::restore_modes() {
    Store& s = store();
    std::uint32_t changed = 0;
    for (std::uint32_t k = 0; k < listed_ && k < tracks_.size(); ++k) {
        const b3host::gaddr at = kTable + (kGameSongCount + k) * kEntry + 0x14u;
        const std::uint32_t m = s.modes.get(listed_folder_, b3music::relative_key(listed_folder_, tracks_[k].path));
        if (b3host::load<std::uint32_t>(at) != m) b3host::store<std::uint32_t>(at, m), ++changed;
    }
    if (changed) std::printf("custom_music: VBlank %llu: the custom songs' modes put back (%u)\n", static_cast<unsigned long long>(vb()), changed);
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

// func_003FBB50(radio, soundtrack, out): the EA TRAX descriptor with the list's table (its count stays 44)
inline void CustomSoundtrack::trax(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    p->trax_(a, r);
    if (a.gpr[1].w[0] != 0) return;
    if (!p->sync_list()) return;
    b3host::store<std::uint32_t>(a.gpr[2].w[0] + 0x4Cu, kTable);
}

// func_003FC060(radio, descriptor, song, play mode): the radio takes the list's whole count
inline void CustomSoundtrack::set_soundtrack(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    p->set_soundtrack_(a, r);
    const std::uint32_t radio = a.gpr[0].w[0];
    if (p->listed_radio(radio)) b3host::store<std::uint32_t>(radio + 0x6Cu, kGameSongCount + p->listed_);
}

// func_003FCC10(radio): the profile's bits into D_004A5600, and so into the list's copy of the 44
inline void CustomSoundtrack::profile_songs(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    p->profile_songs_(a, r);
    if (!p->listed_) return;
    for (std::uint32_t k = 0; k < kGameSongCount; ++k)
        b3host::store<std::uint32_t>(kTable + k * kEntry + 0x14u, b3host::load<std::uint32_t>(kGameSongs + k * kEntry + 0x14u));
}

// func_003FBDB0(radio) -> the next song: the game's steps for a list longer than its 51-byte shuffle array
inline void CustomSoundtrack::next_song(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t radio = a.gpr[0].w[0];
    p->sync_list();   // PLAYLIST and the folder apply from the next song
    const std::uint32_t n = b3host::load<std::uint32_t>(radio + 0x6Cu);
    if (!p->listed_radio(radio) || n <= kGameSongCount || b3host::load<std::uint8_t>(radio + 0xBCu) == 0) {
        p->next_song_(a, r);
        return;
    }
    const std::uint8_t m = p->context_mode();
    b3host::store<std::uint8_t>(radio + 0xCu, 0);
    int song = -1;
    switch (b3host::load<std::int32_t>(radio + 0xCCu)) {
    case 1:
    case 5: {   // in order from the current song
        const std::uint32_t d8 = b3host::load<std::uint32_t>(radio + 0xD8u);
        std::uint32_t j = 0;
        for (; j < n; ++j) {
            song = int((d8 + j + 1) % n);
            if (p->allowed(std::uint32_t(song), m)) break;
        }
        if (j == n) b3host::store<std::uint8_t>(radio + 0xCu, 1), song = -1;
        break;
    }
    case 3:
    case 4:   // repeat
        song = b3host::load<std::int32_t>(radio + 0xD8u);
        break;
    case 2: {   // shuffle: a new order once the count changes, then the next of it
        std::uint32_t i = 0;
        for (; i < n; ++i) {
            if (p->deck_n_ != n) {
                p->deck_n_ = n;
                p->deck_.resize(n);
                for (std::uint32_t k = 0; k < n; ++k) p->deck_[k] = int(k);
                for (std::uint32_t k = 0; k < n; ++k) std::swap(p->deck_[k], p->deck_[k + sound_random() % (n - k)]);
                p->deck_at_ = int(sound_random() % n);
            }
            song = p->deck_[std::size_t(p->deck_at_)];
            p->deck_at_ = (p->deck_at_ + 1) % int(p->deck_n_);
            if (p->allowed(std::uint32_t(song), m)) break;
        }
        if (i == n) song = -1, b3host::store<std::uint8_t>(radio + 0xCu, 1);
        break;
    }
    default:
        break;
    }
    r.v0 = u_long128{};
    r.v0.w[0] = std::uint32_t(song);
    r.v0.w[1] = song < 0 ? 0xFFFFFFFFu : 0u;
}

// func_003FB8B0(radio, song): the page's confirm, the order going on from its song
inline void CustomSoundtrack::play_from(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t radio = a.gpr[0].w[0];
    const std::uint32_t n = b3host::load<std::uint32_t>(radio + 0x6Cu);
    if (!p->listed_radio(radio) || n <= kGameSongCount) {
        p->play_from_(a, r);
        return;
    }
    const int song = static_cast<int>(a.gpr[1].w[0]);
    if (p->deck_n_ != n) {
        p->deck_n_ = n;
        p->deck_.resize(n);
        for (std::uint32_t k = 0; k < n; ++k) p->deck_[k] = int(k);
        for (std::uint32_t k = 0; k < n; ++k) std::swap(p->deck_[k], p->deck_[k + sound_random() % (n - k)]);
        p->deck_at_ = int(sound_random() % n);
    }
    if (b3host::load<std::int32_t>(radio + 0xCCu) == 1) {
        b3host::store<std::int32_t>(radio + 0xD8u, song - 1 < 0 ? int(n) - 1 : song - 1);
    } else {
        for (std::uint32_t k = 0; k < p->deck_n_; ++k)
            if (p->deck_[k] == song) p->deck_at_ = int(k);
    }
}

// func_003FB7C0(radio): may the current song play here (Custom only: not a disc song)
inline void CustomSoundtrack::song_allowed(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    p->song_allowed_(a, r);
    const std::uint32_t radio = a.gpr[0].w[0];
    if (p->mode_ != kCustom || !p->listed_radio(radio) || b3host::load<std::uint8_t>(radio + 0xBCu) == 0) return;
    if (b3host::load<std::uint32_t>(radio + 0xD8u) < kGameSongCount) r.v0 = u_long128{};
}

// func_003FC8C0(radio): at a custom song's end the game clears D_004F5040[song] bit 8 -- another option's byte: kept
inline void CustomSoundtrack::run_stream(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t radio = a.gpr[0].w[0];
    const std::uint32_t d8 = b3host::load<std::uint32_t>(radio + 0xD8u);
    if (d8 < kGameSongCount || d8 >= kGameSongCount + kMaxCustom) {
        p->run_stream_(a, r);
        return;
    }
    const std::uint8_t keep = b3host::load<std::uint8_t>(kOptions + d8);
    p->run_stream_(a, r);
    b3host::store<std::uint8_t>(kOptions + d8, keep);
}

// the EA TRAX page's Update: confirm keeps the custom songs' modes, back puts them back (the page asks the input as here)
inline void CustomSoundtrack::page_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    if (!p->listed_ || b3host::load<std::int32_t>(kMenuState) != 5) {
        p->page_update_(a, r);
        return;
    }
    const bool confirm = (b3host::call_guest<int>(kGetMenuConfirm, kInput, 0) & 0xFF) != 0;
    const bool back = !confirm && (b3host::call_guest<int>(kGetMenuBack, kInput, 0) & 0xFF) != 0;
    p->page_update_(a, r);
    if (confirm) p->keep_modes();
    else if (back) p->restore_modes();
}

// func_00271A00(strings, id): a custom row's title / album / artist
inline void CustomSoundtrack::lookup(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t id = a.gpr[1].w[0];
    if (!p->listed_ || id < kStringBase || id >= kStringBase + 4 * p->listed_ || (id & 3u) == 3u || a.gpr[0].w[0] != kStrings) {
        p->lookup_(a, r);
        return;
    }
    const b3music::Tags& t = p->tracks_[(id - kStringBase) / 4].tags;
    const std::string& s = (id & 3u) == 0 ? t.title : (id & 3u) == 1 ? t.album : t.artist;
    const std::u16string u = b3music::popup_text(s, kRowUnits);
    const b3host::gaddr at = kRing + (p->ring_at_++ % kRingSlots) * kRingSlot;
    std::size_t k = 0;
    for (; k < u.size() && k < kRingSlot / 2 - 1; ++k) b3host::store<std::uint16_t>(at + 2 * k, std::uint16_t(u[k]));
    b3host::store<std::uint16_t>(at + 2 * k, 0);
    r.v0 = u_long128{};
    r.v0.w[0] = at;
}

// func_00421AF0(card image, ...): the save keeps the disc's songs only -- a custom song's index in the options' last song
// (D_004F5077, written as the radio picks a song) becomes the last of the disc's songs started (else 0) before the
// image and its checksum are made, so the save stays valid on a PS2 or in an emulator
inline void CustomSoundtrack::save_image(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint8_t v = b3host::load<std::uint8_t>(kLastSong);
    if (v >= kGameSongCount && v != 0xFFu) {
        b3host::store<std::uint8_t>(kLastSong, p->last_game_song_);
        std::printf("custom_music: VBlank %llu: the save's last song is custom (%u): saved as the game's %u\n", static_cast<unsigned long long>(vb()),
                    unsigned(v), unsigned(p->last_game_song_));
    }
    p->save_image_(a, r);
}

// func_00431160(page): the rows' numbers. The page prints each into unsigned short [3][3] at +0x4 (PrintUInt): a
// number from 100 on (a long list) takes four units, its terminator landing in the next row's text or, for the last
// row, in the two bytes after the array (+0x16, put back here). Each row's number text (the text objects at +0x20,
// 0x50 bytes each, the string at +0x48) is then drawn from kNumbers with the whole number.
inline void CustomSoundtrack::page_rows(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t page = a.gpr[0].w[0];
    if (!p->listed_) {
        p->page_rows_(a, r);
        return;
    }
    const std::uint16_t keep = b3host::load<std::uint16_t>(page + 0x16u);
    p->page_rows_(a, r);
    b3host::store<std::uint16_t>(page + 0x16u, keep);
    const std::int32_t top = b3host::load<std::int32_t>(page + 0x938u) - 1;
    const std::uint32_t count = b3host::load<std::uint32_t>(b3host::load<std::uint32_t>(page + 0x9BCu) + 4u);
    for (std::uint32_t i = 0; i < 3; ++i) {
        const std::int32_t idx = top + std::int32_t(i);
        const b3host::gaddr at = kNumbers + 8u * i;
        char d[8] = {};
        if (idx >= 0 && std::uint32_t(idx) < count) std::snprintf(d, sizeof d, "%u", unsigned(idx + 1) % 1000u);
        std::uint32_t k = 0;
        for (; d[k] && k < 3; ++k) b3host::store<std::uint16_t>(at + 2 * k, std::uint16_t(d[k]));
        b3host::store<std::uint16_t>(at + 2 * k, 0);
        b3host::store<std::uint32_t>(page + 0x20u + 0x50u * i + 0x48u, at);
    }
}

// func_003FBC20(radio, song, ...): the game's song start; a custom row's index is that custom song
inline void CustomSoundtrack::start_song(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    p->start_song_(a, r);
    const std::uint32_t manager = a.gpr[0].w[0];
    const int song = static_cast<int>(a.gpr[1].w[0]);
    if (song == -1) { if (manager == p->manager_) p->current_ = -1; return; }
    if (b3host::load<std::uint8_t>(manager + 0xBCu) == 0) return;   // a non-stream soundtrack (the Xbox's own; not on a PS2)
    if (song < int(kGameSongCount)) {   // the game's song
        p->current_ = -1;
        p->last_game_song_ = std::uint8_t(song);
        return;
    }
    p->manager_ = manager;
    const int k = song - int(kGameSongCount);
    if (!p->listed_ || k >= int(p->tracks_.size())) {   // not a song of the folder now (the list changed): it ends at once
        p->current_ = kSkip;
        return;
    }
    p->current_ = k;
    const b3music::Track& t = p->tracks_[std::size_t(k)];
    std::printf("custom_music: VBlank %llu: song %d is custom %d of %zu: %s - %s (%s)\n", static_cast<unsigned long long>(vb()), song,
                k + 1, p->tracks_.size(), t.tags.artist.c_str(), t.tags.title.c_str(), t.path.c_str());
}

// func_003865A0(stream, name, 0, start sample, segment, ...): the radio's stream with a custom song attaches nothing
inline void CustomSoundtrack::prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    CustomSoundtrack* p = instance();
    const std::uint32_t stream = a.gpr[0].w[0];
    if (p->current_ == -1 || p->manager_ == 0 || b3host::load<std::uint32_t>(p->manager_ + 0xC0u) != stream) { p->prepare_(a, r); return; }
    if (!p->is_virtual(stream) || p->playing_ != p->current_ || (p->current_ >= 0 && !p->player_.open())) {
        const std::uint32_t at = a.gpr[3].w[0];   // the start sample (a resume: the position we answered)
        std::string why = "not a song of the music folder now";
        p->player_.close();
        if (p->current_ >= int(p->tracks_.size())) p->current_ = kSkip;   // robust2: never past the list (a folder change)
        if (p->current_ >= 0 && !p->player_.start(p->tracks_[std::size_t(p->current_)], at, &why)) p->player_.close();
        p->virtual_ = stream;
        p->playing_ = p->current_;
        if (!p->player_.open()) {   // the song ends at once (Update: status 4), the game goes on to its next
            std::printf("custom_music: VBlank %llu: %s: the song is skipped\n", static_cast<unsigned long long>(vb()), why.c_str());
        } else {
            if (at == 0) ++p->songs_played_;
            std::printf("custom_music: VBlank %llu: stream %08X plays %s (%u Hz, from frame %u)\n", static_cast<unsigned long long>(vb()), stream,
                        p->player_.track().path.c_str(), p->player_.source_rate(), at);
            p->song_measured_ = false;   // musicloud: the song's gain (measured now if the worker has not yet)
            p->song_gain_ = 1.0f;
            if (!p->loudness_ || p->loudness_()) p->loudness_song(true);
        }
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
    static const bool log = std::getenv("B3NATIVE_MUSIC_LOG") != nullptr;
    if (!p->is_virtual(stream)) {
        if (log && stream != 0 && b3host::load<std::uint8_t>(stream + 0x1ECu) == 13)   // the game's own music stream
            p->log_gain(false, b3host::load<float>(stream + 0x1A4u), b3host::load<float>(kCategories + 8u * 13u + 4u), 1.0f);
        p->update_(a, r);
        return;
    }
    const std::uint32_t s = b3host::load<std::uint32_t>(stream + 0x1D0u);
    if ((s == 6 && (p->player_.finished() || !p->player_.open())) || s == 5) {
        b3host::store<std::uint32_t>(stream + 0x1D0u, 4);
        if (s == 6)
            std::printf("custom_music: VBlank %llu: custom song ended (%u ms)\n", static_cast<unsigned long long>(vb()), p->player_.position_ms());
    }
    // ApplyVolumesAndPitch's values: the stream's volume x its category's gain, the category's pitch; then the SPU2
    // path's level (kStreamLevel) and CUSTOM MUSIC VOLUME (the header)
    const std::uint32_t cat = b3host::load<std::uint8_t>(stream + 0x1ECu);
    const float vol = b3host::load<float>(stream + 0x1A4u);
    const float cat_pitch = b3host::load<float>(kCategories + 8u * cat), cat_gain = b3host::load<float>(kCategories + 8u * cat + 4u);
    const float user = custom_volume(p->volume_ ? p->volume_() : 100);
    if (log) p->log_gain(true, vol, cat_gain, user);
    // musicloud: CUSTOM MUSIC LOUDNESS (switched on during a song not measured yet: looked up now and then, the worker
    // started; the gain moves there smoothly, Player::mix)
    const bool even = !p->loudness_ || p->loudness_();
    if (even && !p->song_measured_ && p->player_.open() && p->loud_wait_-- == 0) {
        p->loud_wait_ = 30;
        p->loudness_start();
        p->loudness_song(false);
    }
    const float loud = even && p->song_measured_ ? p->song_gain_ : 1.0f;
    const float g = game_music_muted() && cat == 13 ? 0.0f : vol * cat_gain * kStreamLevel * user * loud;
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
