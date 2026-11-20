// OTHER MUSIC APPS' host side (src/native/now_playing.h): the replay form, the player names, the popup's decisions
// (track changes, pause, nothing playing, a busy display), the popup text (unicode, long names), the poller with a fake
// provider (off: no provider made; failures; release), the file reader, and on Linux the MPRIS reader end to end: a
// private D-Bus (dbus-daemon, when installed) with fake players made by this program (--fake-player, the same libdbus
// loaded at run time). `test_now_playing --read` prints one read of this system's reader (a manual check).
#include "test.h"

#include "custom_music.h"
#include "now_playing.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include "dbus_dl.h"
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using namespace b3nowplaying;

namespace {

Track track(Status s, std::string title, std::string artist = "", std::string album = "", std::string app = "") {
    Track t;
    t.status = s;
    t.title = std::move(title);
    t.artist = std::move(artist);
    t.album = std::move(album);
    t.app = std::move(app);
    return t;
}

// A provider driven by the test: what it reads, whether it fails, and how many exist
struct Fake {
    Track next;
    bool fail = false;
    int reads = 0;
    int alive = 0;
};
class FakeProvider : public Provider {
public:
    explicit FakeProvider(Fake& f) : f_(f) { ++f_.alive; }
    ~FakeProvider() override { --f_.alive; }
    const char* name() const override { return "fake"; }
    bool read(Track& out, std::string* why) override {
        ++f_.reads;
        if (f_.fail) {
            if (why) *why = "fake failure";
            return false;
        }
        out = f_.next;
        return true;
    }

private:
    Fake& f_;
};

std::string units(const std::u16string& u) {   // the popup text as ASCII ('#' for anything above 0x7F)
    std::string s;
    for (char16_t c : u) s.push_back(c < 0x80 ? char(c) : '#');
    return s;
}

}  // namespace

TEST(encode_decode_round_trip) {
    const Track t = track(Status::kPlaying, "Song = 1", "Artist, Other", "Album | B", "Spotify");
    Track back;
    CHECK(decode(encode(t), back) && back == t);
    const Track paused = track(Status::kPaused, "", "", "", "");
    CHECK(decode(encode(paused), back) && back == paused);
    CHECK(decode(encode(Track{}), back) && back == Track{});
    Track keep = t;
    CHECK(!decode("", keep) && keep == t);
    CHECK(!decode("7\x1F" "a\x1F" "b\x1F" "c\x1F" "d", keep) && keep == t);   // no such status
    CHECK(!decode("1\x1F" "a\x1F" "b", keep) && keep == t);                    // fields missing
    CHECK(!decode("1x\x1F" "a\x1F" "b\x1F" "c\x1F" "d", keep));
}

TEST(describe_lines) {
    CHECK(describe(Track{}) == "nothing playing");
    CHECK(describe(track(Status::kPlaying, "Title", "Artist", "Album", "Spotify")) == "Spotify: Artist - Title (Album), playing");
    CHECK(describe(track(Status::kPaused, "Title")) == "Title, paused");
}

TEST(app_names_from_windows_ids) {
    CHECK(app_name_from_id("Spotify.exe") == "Spotify");
    CHECK(app_name_from_id("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify") == "Spotify");
    CHECK(app_name_from_id("Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic") == "ZuneMusic");
    CHECK(app_name_from_id("C:\\Program Files\\VideoLAN\\VLC\\vlc.EXE") == "Vlc");
    CHECK(app_name_from_id("chrome") == "Chrome");
    CHECK(app_name_from_id("308046B0AF4A39CB") == "");   // Firefox's id: a hash, no name
    CHECK(app_name_from_id("") == "");
}

TEST(choose_player_prefers_playing) {
    std::vector<Player> p = {{"org.mpris.MediaPlayer2.a", track(Status::kPaused, "A")},
                             {"org.mpris.MediaPlayer2.b", track(Status::kNone, "B")},
                             {"org.mpris.MediaPlayer2.c", track(Status::kPlaying, "C")}};
    CHECK(choose_player(p) == 2);
    p[2].track.status = Status::kPaused;
    CHECK(choose_player(p) == 0);
    p[0].track.status = p[2].track.status = Status::kNone;
    CHECK(choose_player(p) == -1);
    CHECK(choose_player({}) == -1);
}

TEST(popup_on_track_changes) {
    PopupDecider d;
    CHECK(!d.frame(Track{}, true));   // nothing playing
    const Track a = track(Status::kPlaying, "First", "Band", "Record", "Spotify");
    auto l = d.frame(a, true);
    CHECK(l && l->title == "First" && l->artist == "Band" && l->album == "Record");
    CHECK(!d.frame(a, true));   // shown once
    CHECK(!d.frame(a, true));
    const Track b = track(Status::kPlaying, "Second", "Band", "", "Spotify");
    l = d.frame(b, true);
    CHECK(l && l->title == "Second" && l->album == "Spotify");   // no album: the player's name
    CHECK(!d.frame(b, true));
}

TEST(popup_paused_and_resumed) {
    PopupDecider d;
    const Track a = track(Status::kPlaying, "Song", "Artist", "Album");
    CHECK(d.frame(a, true));
    Track p = a;
    p.status = Status::kPaused;
    CHECK(!d.frame(p, true));   // a pause shows nothing
    CHECK(!d.frame(a, true));   // and the same song resumed neither
    // a new song chosen while paused shows when it plays
    Track n = track(Status::kPaused, "Next", "Artist", "Album");
    CHECK(!d.frame(n, true));
    n.status = Status::kPlaying;
    CHECK(d.frame(n, true));
}

TEST(popup_nothing_playing_forgets) {
    PopupDecider d;
    const Track a = track(Status::kPlaying, "Song");
    CHECK(d.frame(a, true));
    CHECK(!d.frame(Track{}, true));   // the player quit
    CHECK(d.frame(a, true));          // the same song started again: shown again
    CHECK(!d.frame(track(Status::kPlaying, ""), true));   // no title: nothing to show
}

TEST(popup_waits_for_a_busy_display) {
    PopupDecider d;
    const Track a = track(Status::kPlaying, "Song");
    CHECK(!d.frame(a, false) && d.pending());   // the game's own popup is up
    CHECK(!d.frame(a, false) && d.pending());
    CHECK(d.frame(a, true) && !d.pending());
    CHECK(!d.frame(a, true) && !d.pending());
    // a song that changes while waiting: the newest is shown
    CHECK(!d.frame(track(Status::kPlaying, "B"), false));
    auto l = d.frame(track(Status::kPlaying, "C"), true);
    CHECK(l && l->title == "C");
}

TEST(popup_text_unicode_and_long_names) {
    // Latin-1 kept (the game's fonts), typographic marks folded, the rest '?'
    CHECK(b3music::popup_text("Beyonc\xC3\xA9", 48) == u"Beyonc\u00E9");
    CHECK(units(b3music::popup_text("\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\xA2\xE3\x83\xB3", 48)) == "????");   // katakana
    CHECK(b3music::popup_text("Don\xE2\x80\x99t Stop \xE2\x80\x94 Live", 48) == u"Don't Stop - Live");
    CHECK(units(b3music::popup_text("\xF0\x9F\x8E\xB5 Song", 48)) == "? Song");   // an emoji (outside the BMP)
    // long names cut to the display's 48 characters, ending in "..."
    const std::string long_title(120, 'x');
    const std::u16string cut = b3music::popup_text(long_title, 48);
    CHECK(cut.size() == 48 && cut.substr(45) == u"...");
    CHECK(b3music::popup_text(std::string(48, 'y'), 48).size() == 48);   // exactly the width: kept whole
}

TEST(poller_off_makes_no_provider) {
    Fake f;
    int made = 0;
    {
        Poller p([&](std::string*) { ++made; return std::make_unique<FakeProvider>(f); }, std::chrono::milliseconds(10), false);
        for (int k = 0; k < 5; ++k) p.poll_once();
        CHECK(made == 0 && f.reads == 0 && p.providers_created() == 0);
        CHECK(p.snapshot().track == Track{} && !p.snapshot().ok);
    }
    {   // a threaded poller left off: the thread runs, nothing is made
        Poller p([&](std::string*) { ++made; return std::make_unique<FakeProvider>(f); }, std::chrono::milliseconds(5));
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        CHECK(made == 0 && f.reads == 0);
    }
}

TEST(poller_reads_fails_and_releases) {
    Fake f;
    f.next = track(Status::kPlaying, "One", "A", "B", "Fake");
    Poller p([&](std::string*) { return std::make_unique<FakeProvider>(f); }, std::chrono::milliseconds(10), false);
    p.set_enabled(true);
    p.poll_once();
    CHECK(f.alive == 1 && f.reads == 1 && p.providers_created() == 1);
    Poller::Snapshot s = p.snapshot();
    CHECK(s.ok && s.track == f.next && s.provider == "fake");
    f.next = track(Status::kPlaying, "Two");
    p.poll_once();
    CHECK(p.snapshot().track.title == "Two" && p.providers_created() == 1);   // the same provider
    // failures: the last track kept for a few reads (no second popup of the same song), then nothing
    f.fail = true;
    for (unsigned k = 1; k < Poller::kKeepOnFailure; ++k) {
        p.poll_once();
        s = p.snapshot();
        CHECK(!s.ok && s.why == "fake failure" && s.track.title == "Two");
    }
    p.poll_once();
    CHECK(p.snapshot().track == Track{} && p.snapshot().failures == Poller::kKeepOnFailure);
    f.fail = false;
    p.poll_once();
    CHECK(p.snapshot().ok && p.snapshot().track.title == "Two");
    // off: released, nothing more read, the snapshot says nothing
    p.set_enabled(false);
    const int reads = f.reads;
    p.poll_once();
    CHECK(f.alive == 0 && f.reads == reads && p.snapshot().track == Track{});
    p.set_enabled(true);
    p.poll_once();
    CHECK(f.alive == 1 && p.providers_created() == 2);
}

TEST(poller_no_reader) {
    Poller p([](std::string* why) -> std::unique_ptr<Provider> { *why = "no reader here"; return nullptr; },
             std::chrono::milliseconds(10), false);
    p.set_enabled(true);
    p.poll_once();
    const Poller::Snapshot s = p.snapshot();
    CHECK(!s.ok && s.why == "no reader here" && s.track == Track{});
    p.poll_once();   // not tried again at once (a reader may come later: 10 s)
    CHECK(p.providers_created() == 1);
}

TEST(poller_thread_reads_off_the_caller) {
    std::atomic<int> reads{0};
    struct Counting : Provider {
        std::atomic<int>& n;
        explicit Counting(std::atomic<int>& c) : n(c) {}
        const char* name() const override { return "counting"; }
        bool read(Track& out, std::string*) override {
            ++n;
            out = track(Status::kPlaying, "Threaded");
            return true;
        }
    };
    Poller p([&](std::string*) { return std::make_unique<Counting>(reads); }, std::chrono::milliseconds(5));
    p.set_enabled(true);
    for (int k = 0; k < 400 && p.snapshot().track.title != "Threaded"; ++k) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(p.snapshot().track.title == "Threaded" && reads.load() >= 1);
}

TEST(file_provider_reads_each_time) {
    const std::filesystem::path f = std::filesystem::temp_directory_path() / "b3_now_playing_test.txt";
    auto put = [&](const std::string& s) { std::ofstream(f, std::ios::binary) << s; };
    std::unique_ptr<Provider> p = file_provider(f.string());
    Track t;
    std::filesystem::remove(f);
    std::string why;
    CHECK(!p->read(t, &why) && !why.empty());
    put("status=playing\r\ntitle=Caf\xC3\xA9\nartist=Someone\nalbum=LP\napp=Player\n# comment\n");
    CHECK(p->read(t, nullptr) && t == track(Status::kPlaying, "Caf\xC3\xA9", "Someone", "LP", "Player"));
    put("status=paused\ntitle=X\n");
    CHECK(p->read(t, nullptr) && t == track(Status::kPaused, "X"));
    put("fail=1\n");
    CHECK(!p->read(t, &why));
    std::filesystem::remove(f);
}

TEST(system_reader_exists_where_built) {
    std::string why;
#if defined(__APPLE__)
    CHECK(system_interface() == nullptr);
    CHECK(!system_provider(&why) && why.find("macOS") != std::string::npos);
#else
    CHECK(system_interface() != nullptr);
#endif
}

#if defined(__linux__)
namespace {

// ---- the MPRIS end-to-end check: a private bus and fake players ----
pid_t spawn(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) return 0;
    return pid;
}
void stop(pid_t pid) {
    if (pid <= 0) return;
    kill(pid, SIGTERM);
    int st = 0;
    waitpid(pid, &st, 0);
}

// a private session bus: dbus-daemon --session, its address from a pipe; 0 when there is no dbus-daemon
pid_t private_bus(std::string& address) {
    int fd[2];
    if (pipe(fd) != 0) return 0;
    const std::string print = "--print-address=" + std::to_string(fd[1]);
    const pid_t pid = spawn({"dbus-daemon", "--session", "--nofork", "--nopidfile", print});
    close(fd[1]);
    if (!pid) { close(fd[0]); return 0; }
    char buf[512];
    std::string line;
    for (ssize_t n; line.find('\n') == std::string::npos && (n = read(fd[0], buf, sizeof buf)) > 0;) line.append(buf, std::size_t(n));
    close(fd[0]);
    if (const std::size_t nl = line.find('\n'); nl != std::string::npos) line.resize(nl);
    if (line.empty()) { stop(pid); return 0; }
    address = line;
    return pid;
}

}  // namespace

// --fake-player <name> <status> <title> <artists, '|' between> <album> <identity>: an MPRIS player on the session bus
// that answers Properties.Get of PlaybackStatus, Metadata and Identity until it is killed
int fake_player(int argc, char** argv) {
    if (argc < 8) return 2;
    using namespace b3dbus;
    static Lib d;
    std::string why;
    if (!d.load(&why)) { std::fprintf(stderr, "fake player: %s\n", why.c_str()); return 1; }
    DBusConnection* c = d.session(&why);
    if (!c) { std::fprintf(stderr, "fake player: %s\n", why.c_str()); return 1; }
    const std::string name = std::string("org.mpris.MediaPlayer2.") + argv[2], status = argv[3], title = argv[4], album = argv[6], ident = argv[7];
    std::vector<std::string> artists;
    for (std::string a = argv[5]; !a.empty();) {
        const std::size_t bar = a.find('|');
        artists.push_back(a.substr(0, bar));
        a = bar == std::string::npos ? std::string() : a.substr(bar + 1);
    }
    DBusError e;
    d.error_init(&e);
    if (d.bus_request_name(c, name.c_str(), kNameFlagDoNotQueue, &e) != kRequestNamePrimaryOwner) {
        std::fprintf(stderr, "fake player: the name %s is taken\n", name.c_str());
        return 1;
    }
    d.error_free(&e);
    auto str = [&](DBusMessageIter* it, const std::string& s) { const char* p = s.c_str(); d.message_iter_append_basic(it, kTypeString, &p); };
    while (d.connection_read_write(c, 200)) {
        for (DBusMessage* m; (m = d.connection_pop_message(c));) {
            if (d.message_is_method_call(m, "org.freedesktop.DBus.Properties", "Get")) {
                const char* iface = nullptr;
                const char* prop = nullptr;
                d.error_init(&e);
                d.message_get_args(m, &e, kTypeString, &iface, kTypeString, &prop, kTypeInvalid);
                d.error_free(&e);
                const std::string p = prop ? prop : "";
                DBusMessage* r = nullptr;
                if (p == "PlaybackStatus" || p == "Identity" || p == "Metadata") {
                    r = d.message_new_method_return(m);
                    DBusMessageIter it, var;
                    d.message_iter_init_append(r, &it);
                    if (p == "Metadata") {
                        DBusMessageIter arr;
                        d.message_iter_open_container(&it, kTypeVariant, "a{sv}", &var);
                        d.message_iter_open_container(&var, kTypeArray, "{sv}", &arr);
                        auto entry = [&](const char* key, const std::vector<std::string>& vals, bool list) {
                            DBusMessageIter ent, v, a;
                            d.message_iter_open_container(&arr, kTypeDictEntry, nullptr, &ent);
                            str(&ent, key);
                            d.message_iter_open_container(&ent, kTypeVariant, list ? "as" : "s", &v);
                            if (list) {
                                d.message_iter_open_container(&v, kTypeArray, "s", &a);
                                for (const std::string& s : vals) str(&a, s);
                                d.message_iter_close_container(&v, &a);
                            } else {
                                str(&v, vals[0]);
                            }
                            d.message_iter_close_container(&ent, &v);
                            d.message_iter_close_container(&arr, &ent);
                        };
                        entry("mpris:trackid", {"/org/mpris/MediaPlayer2/Track/1"}, false);
                        entry("xesam:title", {title}, false);
                        entry("xesam:artist", artists, true);
                        entry("xesam:album", {album}, false);
                        d.message_iter_close_container(&var, &arr);
                    } else {
                        d.message_iter_open_container(&it, kTypeVariant, "s", &var);
                        str(&var, p == "Identity" ? ident : status);
                    }
                    d.message_iter_close_container(&it, &var);
                } else {
                    r = d.message_new_error(m, "org.freedesktop.DBus.Error.UnknownProperty", "no such property");
                }
                d.connection_send(c, r, nullptr);
                d.connection_flush(c);
                d.message_unref(r);
            }
            d.message_unref(m);
        }
    }
    return 0;
}

TEST(mpris_end_to_end_on_a_private_bus) {
    std::string why;
    {
        b3dbus::Lib probe;
        if (!probe.load(&why)) { std::printf("    skipped: %s\n", why.c_str()); return; }
    }
    std::string address;
    const pid_t bus = private_bus(address);
    if (!bus) { std::printf("    skipped: no dbus-daemon\n"); return; }
    const char* old = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = old ? old : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", address.c_str(), 1);
    const std::string self = std::filesystem::read_symlink("/proc/self/exe").string();
    std::unique_ptr<Provider> p = mpris_provider(&why);
    CHECK(p != nullptr);
    if (p) {
        Track t;
        CHECK(p->read(t, &why) && t.status == Status::kNone);   // no players on the bus
        // a paused player, then one that plays: the playing one is shown
        const pid_t a = spawn({self, "--fake-player", "aaa", "Paused", "Quiet", "Nobody", "Rest", "Paused Player"});
        const pid_t b = spawn({self, "--fake-player", "zzz.instance42", "Playing", "Caf\xC3\xA9 \xE2\x80\x94 Live", "First|Second",
                               "The Album", ""});
        bool seen = false;
        for (int k = 0; k < 100 && !seen; ++k) {
            seen = p->read(t, &why) && t.status == Status::kPlaying;
            if (!seen) std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        CHECK(seen);
        CHECK(t.title == "Caf\xC3\xA9 \xE2\x80\x94 Live" && t.artist == "First, Second" && t.album == "The Album");
        CHECK(t.app == "Zzz");   // no Identity: the bus name's first part
        stop(b);
        seen = false;
        for (int k = 0; k < 100 && !seen; ++k) {
            seen = p->read(t, &why) && t.status == Status::kPaused;
            if (!seen) std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        CHECK(seen && t.title == "Quiet" && t.artist == "Nobody" && t.album == "Rest" && t.app == "Paused Player");
        // through the poller as the game uses it
        Poller poll([](std::string* w) { return mpris_provider(w); }, std::chrono::milliseconds(20));
        poll.set_enabled(true);
        for (int k = 0; k < 200 && poll.snapshot().track.title != "Quiet"; ++k) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(poll.snapshot().ok && poll.snapshot().track.title == "Quiet" && poll.snapshot().provider == "MPRIS");
        stop(a);
    }
    p.reset();
    stop(bus);
    if (old) setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
    else unsetenv("DBUS_SESSION_BUS_ADDRESS");
    // the bus gone: a reader made now says why, and nothing ends the process
    std::unique_ptr<Provider> gone;
    setenv("DBUS_SESSION_BUS_ADDRESS", address.c_str(), 1);
    gone = mpris_provider(&why);
    CHECK(!gone && !why.empty());
    if (old) setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
    else unsetenv("DBUS_SESSION_BUS_ADDRESS");
}
#endif

int main(int argc, char** argv) {
#if defined(__linux__)
    if (argc >= 2 && std::string(argv[1]) == "--fake-player") return fake_player(argc, argv);
#endif
    if (argc >= 2 && std::string(argv[1]) == "--read") {   // one read of this system's reader
        std::string why;
        std::unique_ptr<Provider> p = system_provider(&why);
        if (!p) { std::printf("no reader: %s\n", why.c_str()); return 1; }
        Track t;
        if (!p->read(t, &why)) { std::printf("%s: the read failed: %s\n", p->name(), why.c_str()); return 1; }
        std::printf("%s: %s\n", p->name(), describe(t).c_str());
        return 0;
    }
    return t::run_all("now playing (other music apps)");
}
