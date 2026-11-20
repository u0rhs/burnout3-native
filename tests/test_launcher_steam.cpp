// ADD TO STEAM (src/b3launcher/steam.h, steam_art.h): the shortcut's id, shortcuts.vdf read and written (existing
// entries byte for byte), no second entry for the same program, the grid's file names, where Steam is (a made-up home
// folder), whether it runs (a made-up /proc), the backup and the atomic write, and the pictures. Everything happens in
// build/test_launcher_steam.scratch; no Steam folder of this computer is read or written.
//
// The id vectors were computed for this test from the formula with Python's zlib.crc32:
//   (zlib.crc32((exe + name).encode()) | 0x80000000) & 0xFFFFFFFF
#include "test.h"
#include "../src/b3launcher/steam.h"
#include "../src/b3launcher/steam_art.h"
#include "b3settings/i18n.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace b3launcher;
using steam::Bytes;
namespace fs = std::filesystem;

namespace {

fs::path temp_root() { return "build/test_launcher_steam.scratch"; }   // removed at the end (the last test)
fs::path fresh(const std::string& name) {
    const fs::path p = temp_root() / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}
Bytes read(const fs::path& p) {
    Bytes b;
    std::ifstream f(p, std::ios::binary);
    char c;
    while (f.get(c)) b.push_back(std::uint8_t(c));
    return b;
}
void write(const fs::path& p, const Bytes& b) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}
void write_text(const fs::path& p, const std::string& s) { write(p, Bytes(s.begin(), s.end())); }
bool contains(const Bytes& hay, const Bytes& needle) {
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}

// A binary VDF writer of the test's own (not the launcher's), for files as other tools and older Steam versions write
struct Vdf {
    Bytes b;
    Vdf& map(const std::string& n) { b.push_back(0); name(n); return *this; }
    Vdf& str(const std::string& n, const std::string& v) { b.push_back(1); name(n); name(v); return *this; }
    Vdf& i32(const std::string& n, std::uint32_t v) { b.push_back(2); name(n); for (int k = 0; k < 4; ++k) b.push_back(std::uint8_t(v >> (8 * k))); return *this; }
    Vdf& f32(const std::string& n) { b.push_back(3); name(n); for (std::uint8_t x : {0x00, 0x00, 0x80, 0x3F}) b.push_back(x); return *this; }
    Vdf& u64(const std::string& n, std::uint64_t v) { b.push_back(7); name(n); for (int k = 0; k < 8; ++k) b.push_back(std::uint8_t(v >> (8 * k))); return *this; }
    Vdf& end() { b.push_back(8); return *this; }
    void name(const std::string& s) { b.insert(b.end(), s.begin(), s.end()); b.push_back(0); }
};

// Two entries as other tools write them: lower-case names, no appid (older Steam), a float, a 64-bit field, tags
Bytes two_entries() {
    Vdf v;
    v.map("shortcuts");
    v.map("0").str("appname", "Some Emulator").str("exe", "\"/opt/emu/emu\"").str("StartDir", "\"/opt/emu/\"").str("icon", "")
        .i32("IsHidden", 0).f32("Scale").u64("LastPlayTime64", 0x0102030405060708ull)
        .map("tags").str("0", "favorite").end().end();
    v.map("1").i32("appid", 0x80001234u).str("AppName", "Other Game").str("Exe", "\"C:\\Games\\Other\\other.exe\"")
        .str("StartDir", "\"C:\\Games\\Other\\\"").map("tags").end().end();
    v.end();   // shortcuts
    v.end();   // the file
    return v.b;
}

steam::NewShortcut ours(const std::string& exe = "/games/b3/b3launcher") {
    steam::NewShortcut s;
    s.exe = exe;
    s.start_dir = fs::path(exe).parent_path().string();
    return s;
}

}  // namespace

TEST(crc32_and_appid_vectors) {
    CHECK_BITS(steam::crc32("123456789"), 0xCBF43926u);   // the CRC-32 check value
    CHECK_BITS(steam::crc32(""), 0u);
    CHECK_BITS(steam::crc32("The quick brown fox jumps over the lazy dog"), 0x414FA339u);
    CHECK_BITS(steam::shortcut_appid("\"C:\\Games\\Burnout 3\\Burnout3Launcher.exe\"", "Burnout 3: Takedown"), 0x8D4EE77Bu);
    CHECK_BITS(steam::shortcut_appid("\"/home/deck/Games/burnout3-native/b3launcher\"", "Burnout 3: Takedown"), 0xA4C33B13u);
    CHECK_BITS(steam::shortcut_appid("\"/Applications/Burnout 3.app/Contents/MacOS/b3launcher\"", "Burnout 3: Takedown"), 0x89B3C5BBu);
    CHECK_BITS(steam::shortcut_appid("\"a\"", "b"), 0xE6645FEAu);
    // the VDF's int32: the same bits, signed
    CHECK(steam::vdf_int(0x8D4EE77Bu) == -1924208773);
    CHECK(steam::vdf_int(0xE6645FEAu) == -429629462);
    // a new entry's id: crc32 of the quoted Exe and the name
    steam::NewShortcut s = ours("/home/deck/Games/burnout3-native/b3launcher");
    CHECK(s.name == "Burnout 3: Takedown");
    CHECK_BITS(steam::appid_of(s), 0xA4C33B13u);
    CHECK(steam::vdf_quoted("/a b/c") == "\"/a b/c\"");
}

TEST(grid_file_names) {
    const std::uint32_t id = 2370758523u;
    CHECK(steam::grid_name(id, steam::Art::kCover) == "2370758523p.png");
    CHECK(steam::grid_name(id, steam::Art::kWide) == "2370758523.png");
    CHECK(steam::grid_name(id, steam::Art::kHero) == "2370758523_hero.png");
    CHECK(steam::grid_name(id, steam::Art::kLogo) == "2370758523_logo.png");
    CHECK(steam::grid_name(id, steam::Art::kIcon) == "2370758523_icon.png");
    CHECK(steam::grid_name(id, steam::Art::kCover, "jpg") == "2370758523p.jpg");
    CHECK(steam::grid_name(id, steam::Art::kHero, "jpg") == "2370758523_hero.jpg");
    CHECK(steam::image_kind({0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0}) == "png");
    CHECK(steam::image_kind({0xFF, 0xD8, 0xFF, 0xE0}) == "jpg");
    CHECK(steam::image_kind({'G', 'I', 'F', '8'}).empty() && steam::image_kind({}).empty() && steam::image_kind({0xFF, 0xD8}).empty());
}

TEST(vdf_empty_file_and_one_entry) {
    const steam::ShortcutsFile e = steam::parse_shortcuts({});
    CHECK(e.ok && e.entries.empty());
    const Bytes out = steam::with_entry(e, ours());
    const steam::ShortcutsFile f = steam::parse_shortcuts(out);
    CHECK(f.ok && f.entries.size() == 1);
    if (f.entries.size() != 1) return;
    const steam::Shortcut& s = f.entries[0];
    CHECK(s.key == "0" && s.name == "Burnout 3: Takedown" && s.exe == "\"/games/b3/b3launcher\"" && s.start_dir == "\"/games/b3\"");
    CHECK(s.appid && *s.appid == steam::appid_of(ours()));
    // the layout: the file's list, the entry's fields in the order Steam itself writes them (as in a shortcuts.vdf that
    // Steam wrote in 2026), two end bytes
    Vdf v;
    v.map("shortcuts").map("0").i32("appid", steam::appid_of(ours())).str("AppName", "Burnout 3: Takedown")
        .str("Exe", "\"/games/b3/b3launcher\"").str("StartDir", "\"/games/b3\"").str("icon", "").str("ShortcutPath", "")
        .str("LaunchOptions", "").i32("IsHidden", 0).i32("AllowDesktopConfig", 1).i32("AllowOverlay", 1).i32("OpenVR", 0)
        .i32("Devkit", 0).str("DevkitGameID", "").i32("DevkitOverrideAppID", 0).i32("LastPlayTime", 0).str("FlatpakAppID", "")
        .str("sortas", "").map("tags").end().end().end().end();
    CHECK(out == v.b);
}

TEST(vdf_existing_entries_kept_byte_for_byte) {
    Bytes in = two_entries();
    in.push_back(0x00);   // bytes after the file's end (kept as they are)
    in.push_back(0x42);
    const steam::ShortcutsFile f = steam::parse_shortcuts(in);
    CHECK(f.ok && f.entries.size() == 2);
    if (f.entries.size() != 2) return;
    CHECK(f.entries[0].name == "Some Emulator" && f.entries[0].exe == "\"/opt/emu/emu\"" && !f.entries[0].appid);
    CHECK(f.entries[1].name == "Other Game" && f.entries[1].appid && *f.entries[1].appid == 0x80001234u);
    CHECK(f.bytes == in);
    const Bytes out = steam::with_entry(f, ours());
    // everything before the list's end byte and everything from it on: unchanged; the new entry between
    CHECK(out.size() > in.size());
    CHECK(Bytes(out.begin(), out.begin() + std::ptrdiff_t(f.insert_at)) == Bytes(in.begin(), in.begin() + std::ptrdiff_t(f.insert_at)));
    CHECK(Bytes(out.end() - std::ptrdiff_t(in.size() - f.insert_at), out.end()) == Bytes(in.begin() + std::ptrdiff_t(f.insert_at), in.end()));
    const steam::ShortcutsFile g = steam::parse_shortcuts(out);
    CHECK(g.ok && g.entries.size() == 3);
    if (g.entries.size() != 3) return;
    for (int k = 0; k < 2; ++k)   // the old entries at the same bytes
        CHECK(Bytes(out.begin() + std::ptrdiff_t(g.entries[k].begin), out.begin() + std::ptrdiff_t(g.entries[k].end)) ==
              Bytes(in.begin() + std::ptrdiff_t(f.entries[k].begin), in.begin() + std::ptrdiff_t(f.entries[k].end)));
    CHECK(g.entries[2].key == "2" && g.entries[2].name == "Burnout 3: Takedown");
    // the same file read again and written again: the same bytes (nothing re-encoded)
    CHECK(steam::with_entry(steam::parse_shortcuts(in), ours()) == out);
}

TEST(vdf_next_key_and_case_of_names) {
    Vdf v;
    v.map("Shortcuts").map("0").str("AppName", "A").str("Exe", "\"/a\"").end().map("7").str("AppName", "B").str("Exe", "\"/b\"").end().end().end();
    const steam::ShortcutsFile f = steam::parse_shortcuts(v.b);
    CHECK(f.ok && f.entries.size() == 2);
    const steam::ShortcutsFile g = steam::parse_shortcuts(steam::with_entry(f, ours()));
    CHECK(g.ok && g.entries.size() == 3 && g.entries.back().key == "8");
}

TEST(vdf_bad_files_are_refused) {
    Bytes full = two_entries();
    for (std::size_t cut : {std::size_t(1), std::size_t(5), std::size_t(20), full.size() - 1}) {
        const steam::ShortcutsFile f = steam::parse_shortcuts(Bytes(full.begin(), full.begin() + std::ptrdiff_t(cut)));
        CHECK(!f.ok && !f.error.empty());
    }
    Bytes unknown = full;
    unknown[unknown.size() - 3] = 0x09;   // an end byte made into a type that does not exist
    CHECK(!steam::parse_shortcuts(unknown).ok);
    Vdf other;
    other.map("something").end().end();
    CHECK(!steam::parse_shortcuts(other.b).ok);   // no "shortcuts" list
    Bytes deep;
    for (int k = 0; k < 40; ++k) { deep.push_back(0); deep.push_back('a'); deep.push_back(0); }
    for (int k = 0; k < 41; ++k) deep.push_back(8);
    CHECK(!steam::parse_shortcuts(deep).ok);   // nesting past 32
}

TEST(same_program_found) {
    const steam::ShortcutsFile f = steam::parse_shortcuts(two_entries());
    CHECK(steam::find_by_exe(f, "\"/opt/emu/emu\"", false) == &f.entries[0]);
    CHECK(steam::find_by_exe(f, "/opt/emu/emu", false) == &f.entries[0]);
    CHECK(steam::find_by_exe(f, "\"/opt/EMU/emu\"", false) == nullptr);
    CHECK(steam::find_by_exe(f, "\"c:/games/other/OTHER.EXE\"", true) == &f.entries[1]);
    CHECK(steam::find_by_exe(f, "\"c:/games/other/OTHER.EXE\"", false) == nullptr);
    CHECK(!steam::same_exe("", "\"\"", true));
    // an entry's id: its appid field, else what Steam computes from its Exe and name
    CHECK(steam::entry_appid(f.entries[1]) == 0x80001234u);
    CHECK(steam::entry_appid(f.entries[0]) == steam::shortcut_appid("\"/opt/emu/emu\"", "Some Emulator"));
}

TEST(backup_and_atomic_write) {
    const fs::path d = fresh("files");
    const fs::path v = d / "shortcuts.vdf";
    CHECK(steam::backup_once(v, nullptr));   // no file: nothing to keep
    CHECK(!fs::exists(steam::backup_path(v)));
    write_text(v, "first");
    CHECK(steam::backup_once(v, nullptr) && read(steam::backup_path(v)) == Bytes({'f', 'i', 'r', 's', 't'}));
    CHECK(steam::write_atomic(v, {'s', 'e', 'c'}, nullptr) && read(v) == Bytes({'s', 'e', 'c'}));
    CHECK(steam::backup_once(v, nullptr) && read(steam::backup_path(v)) == Bytes({'f', 'i', 'r', 's', 't'}));   // the first kept
    CHECK(steam::backup_path(v).filename() == "shortcuts.vdf.b3backup");
    for (const auto& e : fs::directory_iterator(d)) CHECK(e.path().extension() != ".b3tmp");
    std::string why;
    CHECK(!steam::write_atomic(d / "missing" / "x.vdf", {'x'}, &why) && !why.empty());
    CHECK(!fs::exists(d / "missing"));
}

TEST(where_steam_is) {
    steam::Places p;
    p.home = "/home/u";
    const auto lin = steam::candidate_roots(p, steam::Os::kLinux);
    auto has = [](const std::vector<fs::path>& v, const fs::path& x) { return std::find(v.begin(), v.end(), x) != v.end(); };
    CHECK(has(lin, "/home/u/.steam/steam") && has(lin, "/home/u/.local/share/Steam") &&
          has(lin, "/home/u/.var/app/com.valvesoftware.Steam/.local/share/Steam"));
    const auto mac = steam::candidate_roots(p, steam::Os::kMac);
    CHECK(mac.size() == 1 && mac[0] == "/home/u/Library/Application Support/Steam");
    steam::Places w;
    w.registry = {"c:/program files (x86)/steam"};
    w.program_files = {"C:\\Program Files (x86)"};
    const auto win = steam::candidate_roots(w, steam::Os::kWindows);
    CHECK(win.size() == 2 && win[0] == "c:/program files (x86)/steam" && win[1] == fs::path("C:\\Program Files (x86)") / "Steam");
    CHECK(steam::candidate_roots(steam::Places{}, steam::Os::kLinux).empty());

    // a made-up home: Steam in ~/.local/share/Steam with the ~/.steam/steam link to it (found once), Flatpak's too
    const fs::path home = fresh("home");
    p.home = home;
    CHECK(steam::find_roots(p, steam::Os::kLinux).empty());
    fs::create_directories(home / ".local/share/Steam/userdata/123/config");
    fs::create_directories(home / ".steam");
    fs::create_directory_symlink(home / ".local/share/Steam", home / ".steam/steam");
    fs::create_directory_symlink(home / ".local/share/Steam", home / ".steam/root");
    auto roots = steam::find_roots(p, steam::Os::kLinux);
    CHECK(roots.size() == 1 && !roots.empty() && fs::equivalent(roots[0], home / ".local/share/Steam"));
    fs::create_directories(home / ".var/app/com.valvesoftware.Steam/.local/share/Steam/userdata");
    fs::create_directories(home / "notsteam");
    roots = steam::find_roots(p, steam::Os::kLinux);
    CHECK(roots.size() == 2);
    // a folder named Steam without Steam's folders in it: not Steam
    fs::create_directories(home / "Library/Application Support/Steam");
    CHECK(steam::find_roots(p, steam::Os::kMac).empty());
    fs::create_directories(home / "Library/Application Support/Steam/userdata");
    CHECK(steam::find_roots(p, steam::Os::kMac).size() == 1);

    // the accounts: numeric folders, not 0
    const fs::path s = home / ".local/share/Steam";
    for (const char* n : {"0", "45", "anonymous", "12a"}) fs::create_directories(s / "userdata" / n);
    write_text(s / "userdata/77", "a file, not an account");
    CHECK(steam::account_ids(s) == std::vector<std::string>({"45", "123"}));
    CHECK(steam::account_ids(home / "nowhere").empty());
}

TEST(steam_running_by_proc) {
    const fs::path proc = fresh("proc");
    write_text(proc / "1/comm", "systemd\n");
    write_text(proc / "200/comm", "steamwebhelper\n");
    write_text(proc / "300/comm", "steam.sh\n");
    write_text(proc / "self/comm", "steam\n");   // not a pid
    CHECK(!steam::steam_running_in_proc(proc));
    write_text(proc / "4242/comm", "steam\n");
    CHECK(steam::steam_running_in_proc(proc));
    CHECK(!steam::steam_running_in_proc(proc / "missing"));
}

TEST(add_to_accounts_and_no_duplicate) {
    const fs::path home = fresh("addhome");
    const fs::path root = home / ".local/share/Steam";
    // account 111: a list with two games already; account 222: no config folder yet
    write(root / "userdata/111/config/shortcuts.vdf", two_entries());
    fs::create_directories(root / "userdata/222");
    steam::Places p;
    p.home = home;
    steam::Request r;
    r.shortcut = ours("/games/b3/b3launcher");
    r.roots = steam::find_roots(p, steam::Os::kLinux);
    CHECK(r.roots.size() == 1);
    r.art = {{steam::Art::kCover, "png", {1, 2, 3}}, {steam::Art::kWide, "png", {4}}, {steam::Art::kHero, "jpg", {5}},
             {steam::Art::kLogo, "png", {6}}, {steam::Art::kIcon, "png", {7}}};
    const fs::path hero_png = root / "userdata/111/config/grid" / steam::grid_name(steam::appid_of(r.shortcut), steam::Art::kHero);
    write_text(hero_png, "an older png hero");   // replaced by the .jpg one
    const steam::Result a = steam::add(r);
    CHECK(a.steam_found && a.accounts.size() == 2 && a.ok_count() == 2 && a.added_count() == 2);
    CHECK(steam::summary(a) == "Added to Steam (2 accounts). Start Steam to see it.");
    const std::uint32_t id = steam::appid_of(r.shortcut);
    for (const char* acc : {"111", "222"}) {
        const fs::path c = root / "userdata" / acc / "config";
        const steam::ShortcutsFile f = steam::parse_shortcuts(read(c / "shortcuts.vdf"));
        CHECK(f.ok && !f.entries.empty() && f.entries.back().name == "Burnout 3: Takedown");
        if (f.entries.empty()) continue;
        CHECK(f.entries.back().icon == (fs::canonical(root) / "userdata" / acc / "config/grid" / steam::grid_name(id, steam::Art::kIcon)).string());
        CHECK(read(c / "grid" / steam::grid_name(id, steam::Art::kCover)) == Bytes({1, 2, 3}));
        CHECK(read(c / "grid" / steam::grid_name(id, steam::Art::kWide)) == Bytes({4}));
        CHECK(read(c / "grid" / steam::grid_name(id, steam::Art::kHero, "jpg")) == Bytes({5}));
        CHECK(!fs::exists(c / "grid" / steam::grid_name(id, steam::Art::kHero)));
        CHECK(read(c / "grid" / steam::grid_name(id, steam::Art::kLogo)) == Bytes({6}));
        CHECK(read(c / "grid" / steam::grid_name(id, steam::Art::kIcon)) == Bytes({7}));
    }
    // the backup only where there was a list; the old entries kept
    CHECK(read(root / "userdata/111/config/shortcuts.vdf.b3backup") == two_entries());
    CHECK(!fs::exists(root / "userdata/222/config/shortcuts.vdf.b3backup"));
    const Bytes after = read(root / "userdata/111/config/shortcuts.vdf");
    CHECK(steam::parse_shortcuts(after).entries.size() == 3);
    {
        const Bytes in = two_entries();
        const steam::ShortcutsFile f = steam::parse_shortcuts(in);
        CHECK(f.entries.size() == 2 && contains(after, Bytes(in.begin() + std::ptrdiff_t(f.entries[0].begin), in.begin() + std::ptrdiff_t(f.entries[1].end))));
    }

    // again: no second entry, the file unchanged, the pictures written again (a cover chosen by the player now)
    r.art[0] = {steam::Art::kCover, "jpg", {9, 9}};
    const steam::Result b = steam::add(r);
    CHECK(b.ok_count() == 2 && b.added_count() == 0);
    CHECK(steam::summary(b) == "Already in Steam (2 accounts): its pictures were updated. Start Steam to see it.");
    CHECK(read(root / "userdata/111/config/shortcuts.vdf") == after);
    CHECK(read(root / "userdata/111/config/grid" / steam::grid_name(id, steam::Art::kCover, "jpg")) == Bytes({9, 9}));
    CHECK(!fs::exists(root / "userdata/111/config/grid" / steam::grid_name(id, steam::Art::kCover)));
    CHECK(read(root / "userdata/111/config/shortcuts.vdf.b3backup") == two_entries());   // the first backup kept
}

TEST(add_refuses_a_list_it_cannot_read) {
    const fs::path home = fresh("badhome");
    const fs::path root = home / ".local/share/Steam";
    const Bytes junk = {0x00, 's', 'h', 'o', 'r', 't', 0x00, 0x13, 0x37};
    write(root / "userdata/5/config/shortcuts.vdf", junk);
    write(root / "userdata/6/config/shortcuts.vdf", {});   // empty: a new list
    steam::Places p;
    p.home = home;
    steam::Request r;
    r.shortcut = ours();
    r.roots = steam::find_roots(p, steam::Os::kLinux);
    const steam::Result res = steam::add(r);
    CHECK(res.accounts.size() == 2 && res.ok_count() == 1);
    CHECK(read(root / "userdata/5/config/shortcuts.vdf") == junk);   // untouched
    CHECK(!fs::exists(root / "userdata/5/config/shortcuts.vdf.b3backup"));
    CHECK(steam::parse_shortcuts(read(root / "userdata/6/config/shortcuts.vdf")).entries.size() == 1);
    CHECK(steam::summary(res) ==
          "Added to Steam (1 account). Start Steam to see it. A shortcuts.vdf of Steam's could not be read, so it was left as it is.");
    // only that one: nothing added
    std::error_code ec;
    fs::remove_all(root / "userdata/6", ec);
    const steam::Result none = steam::add(r);
    CHECK(none.ok_count() == 0 && steam::summary(none) == "Not added to Steam. A shortcuts.vdf of Steam's could not be read, so it was left as it is.");
}

TEST(summaries) {
    steam::Result r;
    CHECK(steam::summary(r) == "Steam was not found on this computer. Install Steam and sign in once, then press Add to Steam again.");
    r.steam_found = true;
    CHECK(steam::summary(r) == "Steam has no accounts on this computer yet. Start Steam, sign in, close Steam, then press Add to Steam again.");
    steam::AccountResult a;
    a.ok = a.added = true;
    r.accounts = {a};
    CHECK(steam::summary(r) == "Added to Steam (1 account). Start Steam to see it.");
    a.added = false;
    a.art_why = "Could not write /x/grid/1p.png.";
    r.accounts = {a};
    CHECK(steam::summary(r) == "Already in Steam (1 account): its pictures were updated. Start Steam to see it. Could not write /x/grid/1p.png.");
}

TEST(png_round_trip) {
    steam_art::Image im;
    im.w = 37;
    im.h = 11;
    for (int i = 0; i < im.w * im.h; ++i)
        for (int k = 0; k < 4; ++k) im.rgba.push_back(std::uint8_t((i * 7 + k * 61 + (i / 37) * 13) & 0xFF));
    const Bytes rgba = steam_art::encode_png(im, true);
    const auto back = steam_art::decode_png(rgba.data(), rgba.size());
    CHECK(back && back->w == 37 && back->h == 11 && back->rgba == im.rgba);
    CHECK(steam::image_kind(rgba) == "png");
    const Bytes rgb = steam_art::encode_png(im, false);
    const auto back3 = steam_art::decode_png(rgb.data(), rgb.size());
    CHECK(back3 && back3->w == 37);
    bool same = back3.has_value();
    for (std::size_t i = 0; back3 && i < im.rgba.size(); ++i) same = same && back3->rgba[i] == (i % 4 == 3 ? 255 : im.rgba[i]);
    CHECK(same);
    std::string why;
    CHECK(!steam_art::decode_png(rgb.data(), 20, &why) && !why.empty());
    const Bytes junk = {1, 2, 3};
    CHECK(!steam_art::decode_png(junk.data(), junk.size()));
}

TEST(the_ports_pictures) {
    const steam_art::Image& lg = steam_art::logo();
    CHECK(lg.w == 920 && lg.h == 459);
    CHECK(!lg.rgba.empty() && lg.rgba[3] == 0);   // transparent around the letters
    std::size_t n = 0;
    const std::uint8_t* png = steam_art::logo_png(&n);
    CHECK(n > 1000 && steam::image_kind(Bytes(png, png + 8)) == "png");
    struct Want { steam_art::Image im; int w, h; };
    for (const Want& w : {Want{steam_art::cover(), 600, 900}, Want{steam_art::wide(), 920, 430}, Want{steam_art::hero(), 1920, 620}}) {
        CHECK(w.im.w == w.w && w.im.h == w.h && w.im.rgba.size() == std::size_t(w.w) * std::size_t(w.h) * 4);
        bool opaque = true;
        long sum = 0;
        for (std::size_t i = 0; i < w.im.rgba.size(); i += 4) {
            opaque = opaque && w.im.rgba[i + 3] == 255;
            sum += w.im.rgba[i];
        }
        CHECK(opaque);
        CHECK(sum > 0);   // drawn, not black
        const Bytes enc = steam_art::encode_png(w.im, false);
        const auto back = steam_art::decode_png(enc.data(), enc.size());
        CHECK(back && back->w == w.w && back->h == w.h);
    }
    // the same pictures on every computer
    CHECK(steam_art::encode_png(steam_art::wide(), false) == steam_art::encode_png(steam_art::wide(), false));
}

// 1.1's rule for the launcher's own words: each in the six languages (include/b3settings/i18n.h; `{}`: a number or path)
TEST(every_string_translated) {
    static const char* const kWords[] = {
        "Steam", "Add to Steam", "Adding...", "Adding to Steam...", "Big Picture and the Steam Deck then show it with its own pictures.",
        "Cover image...", "Background image...", "Logo image...", "Use the port's art", "The port's own",
        "Adds Burnout 3: Takedown to the Steam library of every account on this computer, with its own cover, background and logo.",
        "Close Steam first: it rewrites its list of games when it closes.",
        "Choose your own picture for the library's cover (PNG or JPEG; 600 x 900 fits best).",
        "It goes into Steam at the next Add to Steam.",
        "Choose your own picture for the game page's background (PNG or JPEG; 1920 x 620 or 3840 x 1240 fits best).",
        "Choose your own logo shown over the background (a PNG with a transparent background fits best).",
        "Forgets the pictures you chose: the next Add to Steam uses the port's own cover, background and logo.",
        "Started from Steam, the game has Steam Input and the overlay, and Steam's desktop controller layout no longer clicks on the desktop while you play.",
        "Not used: choose a PNG or JPEG image (up to 32 MB).", "Chosen: it goes into Steam at the next Add to Steam.",
        "Cannot read your image {}: choose it again, or Use the port's art.", "Could not make the pictures.",
        "A check run writes only to a made-up Steam folder.", "Cannot find the launcher's own program file, so it cannot be added.",
        "Steam is running.", "Close Steam, then press Add to Steam again: Steam rewrites its list of games when it closes.",
        "Right-click the Steam icon by the clock and choose Exit.",
        "In the menu bar: Steam > Quit Steam.",
        "On a Steam Deck: right-click the Steam icon in the taskbar (bottom right) and choose Exit Steam, then press Add to Steam again.",
        "Flatpak's Steam needs to see this folder and your data folder: flatpak override --user --filesystem={} --filesystem=~/.local/share/b3port com.valvesoftware.Steam",
        // robust2: macOS App Translocation, Steam's Flatpak sandbox, the update's cancel and its disk image
        "macOS runs this app from a temporary copy, because it was opened where it was downloaded.",
        "Move the app to Applications (drag it there in Finder), open it from there, then press Add to Steam again.",
        "Started from Steam's Flatpak",
        "Steam's Flatpak runs the game in its own sandbox, so the launcher and the game keep their settings, saves and mods in a folder of their own: {}.",
        "To use the same data as outside Steam, run this once in a terminal, then start the game from Steam again: flatpak override --user --filesystem=~/.local/share/b3port com.valvesoftware.Steam",
        "Two data folders",
        "Steam's Flatpak can reach the data folder you use outside Steam, but the sandbox's own folder holds saves too, so it stays in use: {}.",
        "To use the other one from Steam, copy what you want to keep from the sandbox's folder into ~/.local/share/b3port/native, then delete the sandbox's folder.",
        "Sandbox settings left behind",
        "Steam's Flatpak can now reach the data folder you use outside Steam, so the launcher and the game use it: {}.",
        "The settings written earlier in the sandbox's own folder stay there, unused (the sandbox's folder holds no save): {}.",
        "Stops the download.", "Nothing is installed; your current version stays as it is.", "Update cancelled.",
        "An update is being installed: wait for it to finish (or cancel it).",
        "The update {} is downloaded and checked, but its disk image did not open.",
        "Open it yourself ({}), then quit and drag the app to Applications.",
        "The next Add to Steam uses the port's own pictures.",
        "Choose a cover picture for Steam (PNG or JPEG)", "Choose a background picture for Steam (PNG or JPEG)",
        "Choose a logo picture for Steam (PNG or JPEG)",
        // steam::summary's sentences
        "Steam was not found on this computer.", "Install Steam and sign in once, then press Add to Steam again.",
        "Steam has no accounts on this computer yet.", "Start Steam, sign in, close Steam, then press Add to Steam again.",
        "Not added to Steam.", "Added to Steam (1 account).", "Added to Steam ({} accounts).",
        "Already in Steam (1 account): its pictures were updated.", "Already in Steam ({} accounts): its pictures were updated.",
        "Start Steam to see it.", "A shortcuts.vdf of Steam's could not be read, so it was left as it is.", "Could not read {}.",
        "Could not write {}.", "Could not make the backup {}.",
        // item 11: the Mods page's Add folder and a pack of the game's files without a mod.ini
        "Install as a file-replacement mod?", "This pack has no mod.ini.", "Install it as a file-replacement mod named {}?",
        "Its files replace the game's files of the same names; it has no code, so nothing in it runs.",
        "A mod.ini is written for it, and it starts off.", "Install", "Not a mod", "{} was not installed; nothing was written.",
        "It has no mod.ini, and no folders of the game's own (DATA, PVEH, TRACKS, SOUND, OVID) to install as replacement files.",
        "A mod.ini is the small text file at the top of a mod that names it: its id, name and version.",
        "How to make one: docs/modding in the port's source.", "Add folder...",
        "Installs a mod from a folder: copied into its own folder in the mods folder.",
        "A pack of the game's own files without a mod.ini (its DATA, PVEH or TRACKS folders) can be installed as a file-replacement mod.",
        "Add a mod: choose its folder (a mod, or a pack of the game's files)",
        "Open the mod's folder, then Start: it is copied into the mods folder.",
        // settingsui: the launcher's tidy-up (the prompt bar, the Play page, the LAUNCHER and TEST tabs, the lines made of pieces)
        "Go back", "Bug report...", "Make a bug report?", "Make report", "With my save", "Click a row twice to open it", "LAUNCHER",
        "TEST", "Left stick", "built {}", "Burnout 3 native port, a test build", "{} preset", "fullscreen", "window", "no MSAA",
        "{} fps limit", "no HD pack", "HD pack", "HD pack off", "saved {}",
        "(an older profile-{} folder: read, and saved as a .sav at the first save)", "(new: the game makes it at its first save)",
        "An older profile-{} folder (read; saved as a .sav at the first save)", "Released {}", "Last checked {}", "Version {}",
        "Not checked for updates yet", "Checking for updates...", "Update check off (Settings > Launcher)", "Update available: {}",
        "Up to date ({} is the latest release)", "Up to date (no release published yet)", "Couldn't check for updates ({})",
        "Installed: {}", "on: {}", "gold", "silver", "bronze", "Paint {}", "NOT SET", "Not saved: {}",
        // xboximport (1.3.0): Import Xbox textures (the HD Textures page, its progress, results and Remove)
        "Xbox textures", "Builds a texture pack on this computer from your own Xbox disc image of the game (.iso or .xiso).",
        "It is a small improvement: some building fronts, signs and car paint are somewhat sharper.",
        "An HD texture pack looks far better; this is for players who want to use their Xbox disc.",
        "Nothing is downloaded or shared, and your own HD pack and the mods' packs still come first.", "Not imported.",
        "{} textures imported.", "Size: {} MB.", "From: {}.", "Made by an older version: import again for the newest result.",
        "XBOX TEXTURES is off.", "HD TEXTURES is off.", "Import Xbox textures...",
        "Choose your Xbox disc image of the game (.iso or .xiso).", "Choose your Xbox disc image of the game (.iso or .xiso)",
        "The import runs on this computer in about a minute.", "Deletes the imported textures (your disc image is not touched).",
        "Disc images here: A on one imports its textures.", "No disc images here: open the folder that holds your Xbox disc image.",
        "Xbox disc images (*.iso, *.xiso)", "Importing Xbox textures", "Reading the game's files: {}.", "Building the textures: {}.",
        "Textures written: {}.", "Cancelling...", "Used from the next start.", "Import cancelled: nothing was changed.",
        "The import failed.", "Remove the Xbox textures?", "The imported textures are deleted.",
        "You can import them again from your disc at any time.", "The Xbox textures were removed.", "Cannot open the disc image.",
        "Not an Xbox disc image (no XDVDFS volume in it).", "This Xbox disc image is not Burnout 3: Takedown.",
        "The game's files were not found (choose the game's folder first).", "Not enough free space (about 200 MB needed).",
        "Could not read the disc or write the textures (is the drive full?).",
        "No Xbox texture matched the game's (a different version of the game?).",
        "Could not replace the earlier import (a file in it is open?).", "Could not move the textures into place.",
        "Out of memory while reading the disc image (a damaged image?).", "The import stopped: {}.",
        "The import stopped: an unknown error.",
    };
    for (const b3settings::i18n::Table& t : b3settings::i18n::kTables) {
        for (const char* w : kWords) {
            bool found = false;
            for (std::size_t k = 0; k < t.count && !found; ++k) found = std::strcmp(t.entries[k].en, w) == 0 && t.entries[k].text[0] != 0;
            if (!found) std::printf("  %s lacks: %s\n", t.code, w);
            CHECK(found);
        }
    }
}

TEST(mac_app_translocation) {   // robust2
    CHECK(steam::translocated("/private/var/folders/x1/abc/T/AppTranslocation/0F1E-22/d/Burnout 3.app/Contents/MacOS/b3launcher"));
    CHECK(!steam::translocated("/Applications/Burnout 3.app/Contents/MacOS/b3launcher"));
    CHECK(!steam::translocated("/home/me/Games/burnout3-native/b3launcher"));
}

TEST(scratch_removed) {
    std::error_code ec;
    fs::remove_all(temp_root(), ec);
    CHECK(!fs::exists(temp_root()));
}

TEST_MAIN("test_launcher_steam: Add to Steam (shortcuts.vdf, ids, grid pictures, where Steam is)")
