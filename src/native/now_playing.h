// now_playing.h -- what another music app is playing, from the system's now-playing interface (remaster-features §6.2,
// "Other music apps"; 2026-11-13). Host side only: the readers and the poller. The game side (the EA TRAX popup that
// shows it, the replay latch) is tools/native/now_playing.h.
//
// One interface (Provider) per system:
//   - Windows (10 1809 and later): the Global System Media Transport Controls sessions
//     (Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager) through the WinRT ABI, combase.dll
//     loaded at run time (now_playing_win.cpp). Spotify, the Media Player, browsers and most players publish one.
//   - Linux: MPRIS over the session D-Bus (org.mpris.MediaPlayer2.* names, PlaybackStatus, Metadata xesam:title /
//     xesam:artist / xesam:album), libdbus-1 loaded at run time (now_playing_mpris.cpp): no D-Bus, no library, the
//     game starts the same and the reader says why it has nothing.
//   - macOS: none. The only way to read another app's now-playing data is the private MediaRemote framework, which
//     macOS 15.4 and later refuse to processes without Apple's entitlement; the reader says so (unsupported).
// A file reader (key=value lines) stands in for the system's in tests and scripts.
//
// Every read can block (a D-Bus round trip, a WinRT async call), so the game never calls a provider: the Poller does,
// on its own thread, about once a second, and the game's thread only copies its last result (a mutex held for the copy).
// A Poller that is not enabled releases its provider and makes no system calls at all.
#ifndef B3NATIVE_NOW_PLAYING_H
#define B3NATIVE_NOW_PLAYING_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace b3nowplaying {

enum class Status : int { kNone = 0, kPlaying = 1, kPaused = 2 };   // kNone: no player, or stopped

struct Track {
    Status status = Status::kNone;
    std::string title, artist, album;   // UTF-8 (artist: several joined with ", ")
    std::string app;                    // the player's name when the system gives one ("Spotify"), else empty
    bool operator==(const Track& o) const {
        return status == o.status && title == o.title && artist == o.artist && album == o.album && app == o.app;
    }
    bool operator!=(const Track& o) const { return !(*this == o); }
};

// One line for logs and the launcher: "Spotify: Artist - Title (Album), playing" / "nothing playing".
std::string describe(const Track& t);
// The replay's value for a track (tools/native/now_playing.h records it per change): status and the four texts,
// separated by U+001F; decode() refuses anything else (and leaves `t` as it was).
std::string encode(const Track& t);
bool decode(const std::string& s, Track& t);

class Provider {
public:
    virtual ~Provider() = default;
    virtual const char* name() const = 0;   // "Windows media sessions", "MPRIS", "file"
    // Reads what plays now. false (and *why): the read failed (tried again at the next poll); true with kNone: nothing.
    virtual bool read(Track& out, std::string* why) = 0;
};

// The system's reader, or null and *why (no such interface here, or it cannot start). Create, use and destroy it on
// one thread (Windows: it initialises WinRT on the thread that creates it).
std::unique_ptr<Provider> system_provider(std::string* why = nullptr);
// What the system reader uses ("Windows media sessions", "MPRIS (D-Bus)"), or null where there is none (macOS).
const char* system_interface();
// A text file of key=value lines (status = playing / paused / none, title, artist, album, app), read at every call;
// a missing file reads as a failure.
std::unique_ptr<Provider> file_provider(const std::string& path);
// The platform readers (null and *why where not built for this system)
std::unique_ptr<Provider> mpris_provider(std::string* why);
std::unique_ptr<Provider> smtc_provider(std::string* why);

// A Windows AppUserModelID or executable as a player's name: "Spotify.exe" -> "Spotify",
// "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify" -> "Spotify", "C:\\...\\vlc.exe" -> "Vlc"; an id that is only a hash
// (Firefox's "308046B0AF4A39CB") -> "".
std::string app_name_from_id(const std::string& id);

// MPRIS: which of the players on the bus to show -- the first (in bus-name order) that plays, else the first paused
// one; -1: none.
struct Player {
    std::string bus_name;
    Track track;
};
int choose_player(const std::vector<Player>& players);

// ---- the poller ----
class Poller {
public:
    using Factory = std::function<std::unique_ptr<Provider>(std::string* why)>;
    struct Snapshot {
        Track track;          // the last good read (kNone before one, while disabled, or after kKeepOnFailure failed reads in a row)
        bool ok = false;      // the provider exists and its last read worked
        std::string why;      // else why not
        std::string provider; // its name
        std::uint64_t reads = 0, failures = 0;
    };
    // threaded = false: no thread (tests call poll_once()); `every`: the time between reads
    explicit Poller(Factory factory, std::chrono::milliseconds every = std::chrono::milliseconds(1000), bool threaded = true);
    ~Poller();   // stops the thread (waits for a read in progress) and releases the provider on it
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    // Off: the provider is released (on the poller's thread) and nothing more is read; the snapshot says nothing plays.
    void set_enabled(bool on);
    bool enabled() const;
    Snapshot snapshot() const;
    // One step of the thread's loop on the caller's thread (threaded = false only)
    void poll_once();
    std::uint64_t providers_created() const;

private:
    void run();
    void step(std::unique_lock<std::mutex>& lock);
    Factory factory_;
    std::chrono::milliseconds every_;
    std::unique_ptr<Provider> provider_;   // the poller thread's only (or poll_once's caller's)
    std::chrono::steady_clock::time_point retry_at_{};
    mutable std::mutex m_;
    std::condition_variable cv_;
    Snapshot snap_;
    bool enabled_ = false, stop_ = false;
    std::uint64_t created_ = 0;
    unsigned failed_in_row_ = 0;
    std::thread thread_;

public:
    static constexpr unsigned kKeepOnFailure = 3;
};

// ---- the popup's decisions (pure: the game side feeds it the latched track each frame) ----
struct PopupLines {
    std::string title, artist, album;   // UTF-8; album: the album, else the player's name
};
class PopupDecider {
public:
    // Each frame: the track as the game sees it now, and whether the EA TRAX display is free (nothing shown or fading).
    // A popup to show now, or none. A track is shown once when it starts playing (or when the option is switched on
    // during it); a pause and resume of the same track shows nothing; nothing playing forgets the last one (the same
    // song started again shows again). While the display is busy the popup waits for it.
    std::optional<PopupLines> frame(const Track& t, bool display_free);
    bool pending() const { return pending_; }
    void reset() { shown_.clear(); pending_ = false; }

private:
    std::string shown_;
    bool pending_ = false;
};

}  // namespace b3nowplaying

#endif
