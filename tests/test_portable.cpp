// Portable mode (1.3.0): the user data folder's rule (include/b3port/user_dir.h) over a table of cases, and the move
// between this computer's folder and <install>/appdata (src/b3launcher/portable.h) both ways in scratch folders: the
// copy checked, failures at each step (the source as it was, nothing left behind), the dated old folder and its removal,
// this computer's folder already holding data (Keep), a move cut short. No game data. Writes under
// build/test_portable.scratch.
#include "test.h"

#include "../src/b3launcher/portable.h"
#include "b3port/user_dir.h"

#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace pt = b3launcher::portable;

namespace {

const fs::path kRoot = fs::absolute(fs::path("build") / "test_portable.scratch");

void write(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

// every file (its bytes) and folder below `root`
std::map<std::string, std::string> snapshot(const fs::path& root) {
    std::map<std::string, std::string> m;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return m;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        const std::string rel = fs::relative(it->path(), root).generic_string();
        if (it->is_directory()) m["D " + rel] = "";
        else m["F " + rel] = read(it->path());
    }
    return m;
}

// A scratch computer: <case>/home/b3port/native (this computer's data folder) and <case>/install (the launcher's folder)
struct Box {
    fs::path top, platform, install;
    explicit Box(const std::string& name) : top(kRoot / name), platform(top / "home" / "b3port" / "native"), install(top / "install") {
        fs::remove_all(top);
        fs::create_directories(install);
        write(install / "b3launcher", "launcher");
        write(install / "native_play", "game");
    }
    void player_data(const fs::path& d, const std::string& tag) const {
        write(d / "pc_settings.ini", "vsync = 1\n# " + tag + "\n");
        write(d / "launcher.ini", "game_dir = /games/b3\n");
        write(d / "saves" / "profile-1.sav", std::string("SAVE\0\1\2\3", 8) + tag);
        write(d / "saves" / "backups" / "profile-1-20261119-101010.sav", "older " + tag);
        write(d / "mods" / "a-mod" / "mod.ini", "id = a-mod\n");
        write(d / "logs" / "run-20261120-100000.log", "a log");
        std::string big(3u << 20, 'x');   // more than one copy chunk
        for (std::size_t k = 0; k < big.size(); k += 4099) big[k] = char(k & 0x7F);
        write(d / "xbox-textures" / "pack.bin", big + tag);
        fs::create_directories(d / "replays");   // an empty folder
    }
    bool portable() const { return fs::is_directory(b3io::portable_root(install)); }
    b3io::UserDirChoice choice() const {
        b3io::UserDirCase c;
        c.exe_dir = install;
        c.marker = portable();
        c.platform = platform;
        return b3io::resolve_user_dir(c);
    }
    bool partials() const {
        return fs::exists(install / "appdata.partial") || fs::exists(platform.parent_path() / "native.partial");
    }
};

const std::string kStamp = "20261120-120000";

}  // namespace

TEST(user_dir_rule_table) {
    const fs::path exe = "/games/b3", home = "/home/me/.local/share/b3port/native";
    struct Row {
        const char* what;
        std::optional<fs::path> override_dir;
        bool marker, apple, appimage, flatpak;
        fs::path want;
        b3io::UserDirKind kind;
    };
    const Row rows[] = {
        {"no marker", std::nullopt, false, false, false, false, home, b3io::UserDirKind::kPlatform},
        {"marker", std::nullopt, true, false, false, false, exe / "appdata" / "native", b3io::UserDirKind::kPortable},
        {"marker, override", fs::path("/tmp/given"), true, false, false, false, "/tmp/given", b3io::UserDirKind::kOverride},
        {"no marker, override", fs::path("/tmp/given"), false, false, false, false, "/tmp/given", b3io::UserDirKind::kOverride},
        {"empty override", fs::path(), true, false, false, false, exe / "appdata" / "native", b3io::UserDirKind::kPortable},
        {"marker on macOS", std::nullopt, true, true, false, false, home, b3io::UserDirKind::kPlatform},
        {"marker in an AppImage", std::nullopt, true, false, true, false, home, b3io::UserDirKind::kPlatform},
        {"marker in a Flatpak", std::nullopt, true, false, false, true, home, b3io::UserDirKind::kPlatform},
    };
    for (const Row& r : rows) {
        b3io::UserDirCase c;
        c.override_dir = r.override_dir;
        c.exe_dir = exe;
        c.marker = r.marker;
        c.apple = r.apple;
        c.appimage = r.appimage;
        c.flatpak = r.flatpak;
        c.platform = home;
        const b3io::UserDirChoice got = b3io::resolve_user_dir(c);
        const bool ok = got.dir == r.want && got.kind == r.kind;
        if (!ok) std::printf("    %s: %s\n", r.what, got.dir.string().c_str());
        CHECK(ok);
    }
    b3io::UserDirCase none;   // the program's folder unknown: never portable
    none.marker = true;
    none.platform = home;
    CHECK(b3io::resolve_user_dir(none).kind == b3io::UserDirKind::kPlatform);
    CHECK(b3io::portable_supported(false, false, false));
    CHECK(!b3io::portable_supported(true, false, false) && !b3io::portable_supported(false, true, false) && !b3io::portable_supported(false, false, true));
}

TEST(two_installs_do_not_mix) {
    Box a("two_a"), b("two_b"), c("two_c");
    fs::create_directories(b3io::portable_dir(a.install));
    fs::create_directories(b3io::portable_dir(b.install));
    b.platform = a.platform;   // the same computer
    c.platform = a.platform;
    CHECK(a.choice().dir == b3io::portable_dir(a.install));
    CHECK(b.choice().dir == b3io::portable_dir(b.install));
    CHECK(a.choice().dir != b.choice().dir);
    CHECK(c.choice().dir == a.platform && c.choice().kind == b3io::UserDirKind::kPlatform);   // not portable: the normal folder
}

TEST(this_process_override_and_platform) {
#if !defined(_WIN32) && !defined(__APPLE__)
    const auto keep_x = b3io::getenv("XDG_DATA_HOME"), keep_o = b3io::getenv(b3io::kUserDirEnv);
    ::setenv("XDG_DATA_HOME", (kRoot / "xdg").c_str(), 1);
    ::unsetenv(b3io::kUserDirEnv);
    CHECK(b3io::platform_user_dir() == kRoot / "xdg" / "b3port" / "native");
    CHECK(b3io::user_dir_choice().kind == b3io::UserDirKind::kPlatform);   // (build/ has no appdata folder)
    CHECK(b3io::user_dir() == kRoot / "xdg" / "b3port" / "native");
    ::setenv(b3io::kUserDirEnv, (kRoot / "given").c_str(), 1);
    CHECK(b3io::user_dir_choice().kind == b3io::UserDirKind::kOverride && b3io::user_dir() == kRoot / "given");
    const std::string slash = b3io::user_dir_slash();   // made, as SDL_GetPrefPath did
    CHECK(!slash.empty() && slash.back() == '/' && fs::is_directory(kRoot / "given"));
    if (keep_x) ::setenv("XDG_DATA_HOME", keep_x->c_str(), 1); else ::unsetenv("XDG_DATA_HOME");
    if (keep_o) ::setenv(b3io::kUserDirEnv, keep_o->c_str(), 1); else ::unsetenv(b3io::kUserDirEnv);
    CHECK(!b3io::exe_dir().empty());
#else
    t::skip("Linux's environment rule");
#endif
}

TEST(row_status) {
    Box b("status");
    pt::Status s = pt::status(b.install, false, false, false, false, true, false);
    CHECK(s.shown && s.usable && !s.on && s.why.empty());
    CHECK(!pt::status(b.install, true, false, false, false, true, false).shown);    // macOS
    CHECK(!pt::status(b.install, false, true, false, false, true, false).shown);    // AppImage
    CHECK(!pt::status(b.install, false, false, true, false, true, false).shown);    // Flatpak
    s = pt::status(b.install, false, false, false, false, false, false);           // Program Files, /opt
    CHECK(s.shown && !s.usable && s.why.find("cannot be written") != std::string::npos);
    s = pt::status(b.install, false, false, false, false, true, true);
    CHECK(s.shown && !s.usable && s.why.find("running") != std::string::npos);
    s = pt::status(b.install, false, false, false, true, true, false);
    CHECK(s.shown && !s.usable && s.why.find("--pref") != std::string::npos);
    s = pt::status(b.install, false, false, false, 2, true, false);   // the player's own $B3PORT_USER_DIR: said so
    CHECK(s.shown && !s.usable && s.why.find("B3PORT_USER_DIR") != std::string::npos && s.why.find("--pref") == std::string::npos);
    fs::create_directories(b3io::portable_dir(b.install));
    CHECK(pt::status(b.install, false, false, false, false, true, false).on);
    CHECK(b3io::dir_writable(b.install));
#ifndef _WIN32
    if (::geteuid() != 0) {   // (root writes anywhere)
        fs::permissions(b.install, fs::perms(0555));
        CHECK(!b3io::dir_writable(b.install));
        fs::permissions(b.install, fs::perms(0755));
    }
#endif
}

TEST(the_games_request) {
    Box b("request");
    CHECK(!b3io::read_portable_request(b.platform));
    CHECK(b3io::write_portable_request(b.platform, true));
    CHECK(b3io::read_portable_request(b.platform) == std::optional<bool>(true));
    CHECK(b3io::write_portable_request(b.platform, false));
    CHECK(b3io::read_portable_request(b.platform) == std::optional<bool>(false));
    write(b.platform / b3io::kPortableRequestFile, "maybe\n");
    CHECK(!b3io::read_portable_request(b.platform));
    b3io::clear_portable_request(b.platform);
    CHECK(!fs::exists(b.platform / b3io::kPortableRequestFile));
}

TEST(move_on_then_off) {
    Box b("both_ways");
    b.player_data(b.platform, "mine");
    const auto data = snapshot(b.platform);
    // on
    std::string why;
    auto p = pt::plan(true, b.install, b.platform, &why);
    CHECK(p.has_value());
    if (!p) { std::printf("    why: %s\n", why.c_str()); return; }
    CHECK(p->files == 7 && p->bytes > (3u << 20) && p->enough_space && !p->target_has_data);
    pt::Progress pr;
    pt::Result r = pt::move(*p, pt::Keep::kMoved, kStamp, &pr);
    CHECK(r.ok);
    if (!r.ok) std::printf("    why: %s\n", r.why.c_str());
    CHECK(r.files == 7);
    CHECK(pr.done == pr.total && pr.total == std::uint64_t(p->bytes) * 2);
    CHECK(b.portable() && b.choice().kind == b3io::UserDirKind::kPortable);
    CHECK(snapshot(b3io::portable_dir(b.install)) == data);
    CHECK(!fs::exists(b.platform));                                    // renamed, not deleted:
    CHECK(r.old_kept == b.platform.parent_path() / ("native.moved-" + kStamp));
    CHECK(snapshot(r.old_kept) == data);
    CHECK(!b.partials());
    CHECK(fs::last_write_time(b3io::portable_dir(b.install) / "saves" / "profile-1.sav") ==
          fs::last_write_time(r.old_kept / "saves" / "profile-1.sav"));   // (kept: "last saved")
    CHECK(!pt::plan(true, b.install, b.platform, &why) && why.find("on already") != std::string::npos);
    // the game plays and saves into appdata/
    write(b3io::portable_dir(b.install) / "saves" / "profile-1.sav", "a new save");
    const auto played = snapshot(b3io::portable_dir(b.install));
    // off: this computer's folder is empty (the old one is under its dated name)
    p = pt::plan(false, b.install, b.platform, &why);
    CHECK(p.has_value() && !p->target_has_data);
    if (!p) return;
    r = pt::move(*p, pt::Keep::kMoved, "20261120-130000");
    CHECK(r.ok);
    CHECK(!b.portable() && b.choice().dir == b.platform);
    CHECK(snapshot(b.platform) == played);
    CHECK(r.old_kept == b.install / "appdata.moved-20261120-130000");
    CHECK(snapshot(r.old_kept / "native") == played);
    CHECK(r.aside.empty());
    CHECK(!b.partials());
    CHECK(read(b.install / "b3launcher") == "launcher");   // the install's own files untouched
}

TEST(no_data_yet) {
    Box b("empty");
    auto p = pt::plan(true, b.install, b.platform);
    CHECK(p && p->files == 0);
    const pt::Result r = pt::move(*p, pt::Keep::kMoved, kStamp);
    CHECK(r.ok && b.portable() && fs::is_directory(b3io::portable_dir(b.install)) && r.old_kept.empty());
}

TEST(off_with_data_already_here) {
    for (int keep = 0; keep < 2; ++keep) {
        Box b(keep ? "keep_existing" : "keep_moved");
        b.player_data(b3io::portable_dir(b.install), "portable");
        b.player_data(b.platform, "this computer");
        const auto port = snapshot(b3io::portable_dir(b.install)), here = snapshot(b.platform);
        auto p = pt::plan(false, b.install, b.platform);
        CHECK(p && p->target_has_data);
        if (!p) continue;
        const pt::Result r = pt::move(*p, keep ? pt::Keep::kExisting : pt::Keep::kMoved, kStamp);
        CHECK(r.ok);
        CHECK(!b.portable());
        if (keep) {   // this computer's kept; the portable data set aside in the install folder, nothing copied
            CHECK(snapshot(b.platform) == here);
            CHECK(r.old_kept == b.install / ("appdata.moved-" + kStamp));
            CHECK(snapshot(r.old_kept / "native") == port);
        } else {      // the portable data moved; this computer's own set aside, never merged
            CHECK(snapshot(b.platform) == port);
            CHECK(r.aside == b.platform.parent_path() / ("native.aside-" + kStamp));
            CHECK(snapshot(r.aside) == here);
            CHECK(snapshot(r.old_kept / "native") == port);
        }
        CHECK(!b.partials());
    }
}

TEST(a_failure_leaves_the_source_as_it_was) {
    const pt::Fault faults[] = {pt::Fault::kCopy, pt::Fault::kVerify, pt::Fault::kRenameIn};
    for (int dir = 0; dir < 2; ++dir)
        for (const pt::Fault f : faults) {
            Box b("fail_" + std::to_string(dir) + "_" + std::to_string(int(f)));
            const fs::path src = dir == 0 ? b.platform : b3io::portable_dir(b.install);
            b.player_data(src, "source");
            if (dir == 1) b.player_data(b.platform, "here");   // off: this computer's own too (set aside, then put back)
            const auto before = snapshot(b.top);
            auto p = pt::plan(dir == 0, b.install, b.platform);
            CHECK(p.has_value());
            if (!p) continue;
            int n = 0;
            const pt::Result r = pt::move(*p, pt::Keep::kMoved, kStamp, nullptr, [&](pt::Fault at, const fs::path&) {
                return at == f && ++n == (f == pt::Fault::kRenameIn ? 1 : 3);   // the third file: mid-copy / mid-check
            });
            CHECK(!r.ok && !r.why.empty());
            CHECK(snapshot(b.top) == before);   // the source, the install and this computer's folder as they were
            CHECK(!b.partials());
        }
}

// Something writes to the source while it is copied (a game started from Steam, a second launcher, a sync client): the
// files are listed again after the check, and any change (a file changed, added or its time moved) stops the move with
// the source as it now is and nothing left behind
TEST(the_source_changed_during_the_move) {
    for (int kind = 0; kind < 3; ++kind) {
        Box b("changed_" + std::to_string(kind));
        b.player_data(b.platform, "mine");
        auto p = pt::plan(true, b.install, b.platform);
        CHECK(p.has_value());
        if (!p) continue;
        bool done = false;
        const pt::Result r = pt::move(*p, pt::Keep::kMoved, kStamp, nullptr, [&](pt::Fault f, const fs::path&) {
            if (f != pt::Fault::kVerify || done) return false;
            done = true;
            if (kind == 0) write(b.platform / "saves" / "profile-1.sav", "saved meanwhile, a different size");
            if (kind == 1) write(b.platform / "logs" / "run-20261120-120500.log", "a new run");
            if (kind == 2) {   // the same size, a new time
                const fs::path f2 = b.platform / "pc_settings.ini";
                fs::last_write_time(f2, fs::last_write_time(f2) + std::chrono::seconds(5));
            }
            return false;
        });
        CHECK(!r.ok && r.why.find("changed while they were copied") != std::string::npos);
        CHECK(!b.portable() && !b.partials() && fs::exists(b.platform / "pc_settings.ini"));
        if (kind == 0) CHECK(read(b.platform / "saves" / "profile-1.sav") == "saved meanwhile, a different size");
    }
    Box q("changed_hook");   // the test hook alone
    q.player_data(q.platform, "mine");
    auto p = pt::plan(true, q.install, q.platform);
    const pt::Result r = pt::move(*p, pt::Keep::kMoved, kStamp, nullptr, [](pt::Fault f, const fs::path&) { return f == pt::Fault::kChanged; });
    CHECK(!r.ok && !q.portable() && !q.partials());
}

// Windows' path limit: a move whose longest new path would pass it is refused before any copy, naming the path
TEST(a_path_too_long_is_named) {
    Box b("long");
    b.player_data(b.platform, "mine");
    write(b.platform / "mods" / "a-mod-with-a-rather-long-name" / "textures" / "a-texture-with-a-long-name-too.dds", "x");
    std::string why;
    const std::size_t limit = (b.install / "appdata.partial" / "native").native().size() + 30;
    CHECK(!pt::plan(true, b.install, b.platform, &why, limit));
    CHECK(why.find("too long for Windows") != std::string::npos && why.find("a-texture-with-a-long-name-too.dds") != std::string::npos);
    CHECK(!b.portable() && !b.partials());
    CHECK(pt::plan(true, b.install, b.platform, &why, 0).has_value());   // no limit (Linux, macOS)
    CHECK(b3io::path_limit() == 0);
}

// Folders inside the install kept as "<install>/..." in the ini files: a stick on E: here and F: there
TEST(folders_inside_the_install_are_kept_relative) {
#ifdef _WIN32
    const fs::path e = "E:\\Games\\B3", f = "F:\\B3";
#else
    const fs::path e = "/media/stick/B3", f = "/run/media/other/B3";
#endif
    const std::string game = b3io::u8string(e / "game"), hd = b3io::u8string(e / "packs" / "SLUS-21050");
    CHECK(b3io::install_relative(game, e) == "<install>/game");
    CHECK(b3io::install_relative(hd, e) == "<install>/packs/SLUS-21050");
    CHECK(b3io::install_relative(b3io::u8string(e), e) == "<install>");
    CHECK(fs::path(b3io::install_resolved("<install>/game", f)) == f / "game");     // another drive letter / mount
    CHECK(fs::path(b3io::install_resolved("<install>/packs/SLUS-21050", f)) == f / "packs" / "SLUS-21050");
    CHECK(fs::path(b3io::install_resolved("<install>", f)) == f);
    const std::string outside = b3io::u8string(e.parent_path() / "Other" / "game");   // outside: absolute as it is
    CHECK(b3io::install_relative(outside, e) == outside && b3io::install_resolved(outside, f) == outside);
    CHECK(b3io::install_relative(b3io::u8string(e.parent_path() / "B3x" / "game"), e) == b3io::u8string(e.parent_path() / "B3x" / "game"));
    CHECK(b3io::install_relative("game", e) == "game");                              // a relative path: as it is
    CHECK(b3io::install_relative("", e).empty() && b3io::install_resolved("", f).empty());
    CHECK(b3io::install_relative("<install>/game", e) == "<install>/game");          // stored already
#ifdef _WIN32
    CHECK(b3io::install_relative("e:\\games\\b3\\game", e) == "<install>/game");   // the case of a drive letter
#endif
}

TEST(the_rename_away_fails) {
    {   // on: portable with the checked copy; the old folder left in place (said), never deleted
        Box b("away_on");
        b.player_data(b.platform, "mine");
        const auto data = snapshot(b.platform);
        auto p = pt::plan(true, b.install, b.platform);
        const pt::Result r = pt::move(*p, pt::Keep::kMoved, kStamp, nullptr, [](pt::Fault f, const fs::path&) { return f == pt::Fault::kRenameAway; });
        CHECK(r.ok && !r.note.empty() && r.old_kept.empty());
        CHECK(b.portable() && snapshot(b3io::portable_dir(b.install)) == data && snapshot(b.platform) == data);
    }
    {   // off: appdata/ cannot go, so the install would stay portable: undone, this computer's data back
        Box b("away_off");
        b.player_data(b3io::portable_dir(b.install), "portable");
        b.player_data(b.platform, "here");
        const auto before = snapshot(b.top);
        auto p = pt::plan(false, b.install, b.platform);
        const pt::Result r = pt::move(*p, pt::Keep::kMoved, kStamp, nullptr, [](pt::Fault f, const fs::path&) { return f == pt::Fault::kRenameAway; });
        CHECK(!r.ok);
        CHECK(snapshot(b.top) == before);
        CHECK(!b.partials());
        p = pt::plan(false, b.install, b.platform);
        const pt::Result k = pt::move(*p, pt::Keep::kExisting, kStamp, nullptr, [](pt::Fault f, const fs::path&) { return f == pt::Fault::kRenameAway; });
        CHECK(!k.ok && snapshot(b.top) == before);
    }
}

TEST(cancel_and_no_space) {
    Box b("cancel");
    b.player_data(b.platform, "mine");
    const auto before = snapshot(b.top);
    auto p = pt::plan(true, b.install, b.platform);
    pt::Progress pr;
    pr.cancel = true;
    pt::Result r = pt::move(*p, pt::Keep::kMoved, kStamp, &pr);
    CHECK(!r.ok && r.why == "cancelled" && snapshot(b.top) == before && !b.partials());
    pt::Plan full = *p;
    full.enough_space = false;
    full.free = 1u << 20;
    r = pt::move(full, pt::Keep::kMoved, kStamp);
    CHECK(!r.ok && r.why.find("free space") != std::string::npos && snapshot(b.top) == before);
    Box k("no_space_keep");   // off, keeping this computer's data: nothing copied, so no space needed
    k.player_data(b3io::portable_dir(k.install), "portable");
    k.player_data(k.platform, "here");
    auto q = pt::plan(false, k.install, k.platform);
    q->enough_space = false;
    CHECK(pt::move(*q, pt::Keep::kExisting, kStamp).ok && !k.portable());
}

TEST(a_move_cut_short_is_cleaned_at_the_next_start) {
    Box b("cut_short");
    b.player_data(b.platform, "mine");
    write(b.install / "appdata.partial" / "native" / "pc_settings.ini", "half");
    write(b.platform.parent_path() / "native.partial" / "saves" / "profile-1.sav", "half");
    const auto data = snapshot(b.platform);
    CHECK(b.choice().kind == b3io::UserDirKind::kPlatform);   // a .partial folder is not the marker
    CHECK(pt::clean_partials(b.install, b.platform) == 2);
    CHECK(!b.partials() && snapshot(b.platform) == data && !b.portable());
    CHECK(pt::clean_partials(b.install, b.platform) == 0);
}

TEST(the_old_copy_kept_then_removed_after_a_good_start) {
    Box b("old_copy");
    b.player_data(b.platform, "mine");
    auto p = pt::plan(true, b.install, b.platform);
    const pt::Result r = pt::move(*p, pt::Keep::kMoved, kStamp);
    CHECK(r.ok && fs::is_directory(r.old_kept));
    const fs::path now = b3io::portable_dir(b.install);
    pt::Record rec;
    rec.old_kept = r.old_kept;
    CHECK(pt::write_record(now, rec));
    auto back = pt::read_record(now);
    CHECK(back && back->old_kept == r.old_kept && !back->started && !back->declined);
    back->started = true;   // the game started from the new folder (and ended cleanly: the launcher offers the removal)
    CHECK(pt::write_record(now, *back));
    back = pt::read_record(now);
    CHECK(back && back->started);
    std::string why;
    CHECK(!pt::remove_old(b.platform.parent_path(), &why) && !why.empty());   // never a folder the move did not name
    CHECK(!pt::remove_old(now, &why));
    CHECK(pt::remove_old(back->old_kept, &why));
    CHECK(!fs::exists(r.old_kept) && fs::is_directory(now) && fs::exists(now / "saves" / "profile-1.sav"));
    pt::clear_record(now);
    CHECK(!pt::read_record(now));
}

TEST(a_save_folder_chosen_elsewhere) {
    const fs::path from = "/home/me/.local/share/b3port/native", to = "/games/b3/appdata/native";
    CHECK(pt::rebase_saves_dir("", from, to).empty());
    CHECK(pt::rebase_saves_dir("/cloud/Burnout saves", from, to) == "/cloud/Burnout saves");   // not moved
    CHECK(fs::path(pt::rebase_saves_dir((from / "saves").string(), from, to)) == to / "saves");
    CHECK(fs::path(pt::rebase_saves_dir(from.string(), from, to)) == to);
    CHECK(pt::rebase_saves_dir("/home/me/.local/share/b3port/native-other", from, to) == "/home/me/.local/share/b3port/native-other");
}

// One function decides the folder: no program of the port asks SDL for it on its own (a call outside user_dir.h would
// miss portable mode). Comments may name it.
TEST(no_stray_pref_path_calls) {
    int found = 0, files = 0;
    for (const char* top : {"src", "tools", "include"}) {
        std::error_code ec;
        if (!fs::is_directory(top, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(top, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const std::string ext = it->path().extension().string();
            if (!it->is_regular_file() || (ext != ".cpp" && ext != ".h" && ext != ".c" && ext != ".mm")) continue;
            if (it->path().string().find("third_party") != std::string::npos) continue;
            ++files;
            std::ifstream f(it->path());
            std::string line;
            for (int n = 1; std::getline(f, line); ++n) {
                const std::size_t at = line.find("SDL_GetPrefPath(");
                if (at == std::string::npos) continue;
                const std::size_t comment = line.find("//");
                if (comment != std::string::npos && comment < at) continue;
                std::printf("    %s:%d: %s\n", it->path().string().c_str(), n, line.c_str());
                ++found;
            }
        }
    }
    if (files == 0) { t::skip("run from the repository's folder"); return; }
    CHECK(found == 0);
}

int main() {
    fs::remove_all(kRoot);
    const int rc = t::run_all("portable mode: the user data folder's rule and the move (scratch folders)");
    fs::remove_all(kRoot);
    return rc;
}
