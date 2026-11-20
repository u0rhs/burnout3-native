// The one-click updater's install step (src/b3launcher/installer.h): release packages (.tar.gz and .zip) written here,
// byte by byte, installed over a fake install folder; previous-version/, roll back, refused packages (nothing changed),
// an interrupted swap undone at the next start, a swap that fails half way. Writes under
// build/test_launcher_installer.scratch.
#include "test.h"

#include "../src/b3launcher/installer.h"

#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace b3launcher::installer;

namespace {

const fs::path kRoot = fs::path("build") / "test_launcher_installer.scratch";
const std::string kTop = "burnout3-native-1.2.0+abc-linux-x86_64";

struct Item {
    std::string name;            // in the archive
    std::string data;            // a file's bytes; a link's target
    char type = '0';             // tar type flag: '0' file, '5' folder, '1' hard link, '2' symlink, '3' device, '6' FIFO
    unsigned mode = 0644;
    bool pax = false;            // tar: the name in a pax 'x' record
};

// ---- writing packages ----

void octal(std::string& h, std::size_t at, std::size_t n, unsigned long long v) {
    char b[24];
    std::snprintf(b, sizeof b, "%0*llo", int(n - 1), v);
    std::memcpy(&h[at], b, n - 1);
    h[at + n - 1] = '\0';
}

std::string tar_header(const std::string& name, const std::string& prefix, std::uint64_t size, char type, unsigned mode,
                       const std::string& link, bool gnu) {
    std::string h(512, '\0');
    std::memcpy(&h[0], name.data(), std::min<std::size_t>(name.size(), 100));
    octal(h, 100, 8, mode);
    octal(h, 108, 8, 0);
    octal(h, 116, 8, 0);
    octal(h, 124, 12, size);
    octal(h, 136, 12, 0);
    std::memset(&h[148], ' ', 8);
    h[156] = type;
    std::memcpy(&h[157], link.data(), std::min<std::size_t>(link.size(), 100));
    if (gnu) std::memcpy(&h[257], "ustar  \0", 8);
    else std::memcpy(&h[257], "ustar\0" "00", 8);
    std::memcpy(&h[345], prefix.data(), std::min<std::size_t>(prefix.size(), 155));
    unsigned sum = 0;
    for (const char c : h) sum += static_cast<unsigned char>(c);
    char b[8];
    std::snprintf(b, sizeof b, "%06o", sum);
    std::memcpy(&h[148], b, 6);
    h[154] = '\0';
    h[155] = ' ';
    return h;
}

void pad512(std::string& s) { s.append((512 - s.size() % 512) % 512, '\0'); }

std::string make_tar(const std::vector<Item>& items, bool gnu) {
    std::string t;
    for (const Item& it : items) {
        const bool file = it.type == '0';
        const std::string body = file ? it.data : std::string();
        const std::string link = file ? std::string() : it.data;
        std::string name = it.name, prefix;
        if (it.pax) {
            std::string rec = " path=" + it.name + "\n";
            std::size_t len = rec.size() + 1;
            while (std::to_string(len).size() + rec.size() != len) ++len;
            rec = std::to_string(len) + rec;
            t += tar_header("PaxHeaders/x", "", rec.size(), 'x', 0644, "", false);
            t += rec;
            pad512(t);
            name = "pax-placeholder";
        } else if (name.size() > 100 && gnu) {
            t += tar_header("././@LongLink", "", name.size() + 1, 'L', 0644, "", true);
            t += name + '\0';
            pad512(t);
            name = name.substr(0, 100);
        } else if (name.size() > 100) {
            const std::size_t s = name.rfind('/', name.size() - 2);
            prefix = name.substr(0, s);
            name = name.substr(s + 1);
        }
        t += tar_header(name, prefix, body.size(), it.type, it.mode, link, gnu);
        t += body;
        pad512(t);
    }
    t.append(1024, '\0');
    t.append((10240 - t.size() % 10240) % 10240, '\0');
    return t;
}

std::string gzip(const std::string& raw) {
    z_stream z{};
    deflateInit2(&z, 9, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    std::string out(deflateBound(&z, uLong(raw.size())) + 64, '\0');
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.data()));
    z.avail_in = uInt(raw.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = uInt(out.size());
    deflate(&z, Z_FINISH);
    out.resize(z.total_out);
    deflateEnd(&z);
    return out;
}

void put16(std::string& s, unsigned v) { s += char(v & 0xFF); s += char((v >> 8) & 0xFF); }
void put32(std::string& s, std::uint32_t v) { put16(s, v & 0xFFFF); put16(s, v >> 16); }

// a zip with stored entries, made on "Unix" (st_mode in the external attributes), as Python's zipfile writes on Linux
std::string make_zip(const std::vector<Item>& items) {
    std::string out, cd;
    for (const Item& it : items) {
        const std::string& data = it.type == '5' ? std::string() : it.data;
        const std::uint32_t crc = std::uint32_t(crc32(0, reinterpret_cast<const Bytef*>(data.data()), uInt(data.size())));
        const unsigned type = it.type == '5' ? 0040000u : it.type == '2' ? 0120000u : it.type == '6' ? 0010000u : 0100000u;
        const std::uint32_t external = ((type | it.mode) << 16) | (it.type == '5' ? 0x10u : 0u);
        const std::uint32_t offset = std::uint32_t(out.size());
        put32(out, 0x04034b50u); put16(out, 20); put16(out, 0); put16(out, 0); put16(out, 0); put16(out, 0x21);
        put32(out, crc); put32(out, std::uint32_t(data.size())); put32(out, std::uint32_t(data.size()));
        put16(out, unsigned(it.name.size())); put16(out, 0);
        out += it.name;
        out += data;
        put32(cd, 0x02014b50u); put16(cd, (3u << 8) | 20); put16(cd, 20); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0x21);
        put32(cd, crc); put32(cd, std::uint32_t(data.size())); put32(cd, std::uint32_t(data.size()));
        put16(cd, unsigned(it.name.size())); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0);
        put32(cd, external); put32(cd, offset);
        cd += it.name;
    }
    const std::uint32_t cd_at = std::uint32_t(out.size());
    out += cd;
    put32(out, 0x06054b50u); put16(out, 0); put16(out, 0); put16(out, unsigned(items.size())); put16(out, unsigned(items.size()));
    put32(out, std::uint32_t(cd.size())); put32(out, cd_at); put16(out, 0);
    return out;
}

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

fs::path tgz(const std::string& file, const std::vector<Item>& items, bool gnu = true) {
    const fs::path p = kRoot / "pkg" / file;
    write(p, gzip(make_tar(items, gnu)));
    return p;
}
fs::path zip(const std::string& file, const std::vector<Item>& items) {
    const fs::path p = kRoot / "pkg" / file;
    write(p, make_zip(items));
    return p;
}

Item f(const std::string& rel, const std::string& data, unsigned mode = 0644) { return {kTop + "/" + rel, data, '0', mode}; }
Item d(const std::string& rel) { return {kTop + "/" + rel + (rel.empty() ? "" : "/"), {}, '5', 0755}; }

// ---- the install folder ----

bool exec_bit(const fs::path& p) {
#ifndef _WIN32
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && (st.st_mode & S_IXUSR);
#else
    (void)p;
    return false;
#endif
}

// every file (contents, exec bit) and folder below `root`
std::map<std::string, std::string> snapshot(const fs::path& root) {
    std::map<std::string, std::string> m;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        const std::string rel = fs::relative(it->path(), root).generic_string();
        if (it->is_directory()) m["D " + rel] = "";
        else m["F " + rel] = read(it->path()) + (exec_bit(it->path()) ? " [x]" : "");
    }
    return m;
}

bool leftovers(const fs::path& root) { return fs::exists(root / ".update-staging") || fs::exists(root / ".update-trash"); }

// 1.1.0, as a player has it: the release's files and their own
fs::path old_install(const std::string& name) {
    const fs::path root = kRoot / name;
    fs::remove_all(root);
    write(root / "native_play", "play 1.1.0");
    write(root / "b3launcher", "launcher 1.1.0");
    write(root / "VERSION.txt", "Burnout 3 native port 1.1.0\ntest build\n");
    write(root / "README-TESTERS.txt", "readme 1.1.0");
    write(root / "mods" / "my-mod" / "mod.ini", "id = my-mod\n");   // the player's
    write(root / "notes.txt", "my notes");
#ifndef _WIN32
    fs::permissions(root / "native_play", fs::perms(0755));
    fs::permissions(root / "b3launcher", fs::perms(0755));
#endif
    return root;
}

std::vector<Item> release(const std::string& v, bool with_extra = true) {
    std::vector<Item> items = {d(""), f("native_play", "play " + v, 0755), f("b3launcher", "launcher " + v, 0755),
                               f("VERSION.txt", "Burnout 3 native port " + v + "\ntest build\n"), f("README-TESTERS.txt", "readme " + v)};
    if (with_extra) {
        items.push_back(f("libb3render.so", "render " + v));   // new in 1.2.0
        items.push_back(d("icons"));
        items.push_back(f("icons/hicolor/256x256/apps/burnout3-native.png", "icon " + v));
    }
    return items;
}

}  // namespace

TEST(tgz_install_swaps_and_keeps_the_old_files) {
    const fs::path root = old_install("t1");
    const Result r = install(tgz("p1.tar.gz", release("1.2.0")), root);
    CHECK(r.ok);
    if (!r.ok) std::printf("    why: %s\n", r.why.c_str());
    CHECK(r.files == 6);
    CHECK(read(root / "native_play") == "play 1.2.0");
    CHECK(read(root / "b3launcher") == "launcher 1.2.0");
    CHECK(read(root / "libb3render.so") == "render 1.2.0");
    CHECK(read(root / "icons/hicolor/256x256/apps/burnout3-native.png") == "icon 1.2.0");
    // previous-version: what was replaced, nothing else
    CHECK(read(root / "previous-version/native_play") == "play 1.1.0");
    CHECK(read(root / "previous-version/VERSION.txt").rfind("Burnout 3 native port 1.1.0", 0) == 0);
    CHECK(!fs::exists(root / "previous-version/libb3render.so"));
    CHECK(!fs::exists(root / "previous-version/notes.txt"));
    CHECK(fs::exists(root / "previous-version/.update-manifest"));
    // the player's files
    CHECK(read(root / "notes.txt") == "my notes");
    CHECK(read(root / "mods/my-mod/mod.ini") == "id = my-mod\n");
#ifndef _WIN32
    CHECK(exec_bit(root / "native_play"));
    CHECK(exec_bit(root / "b3launcher"));
    CHECK(!exec_bit(root / "libb3render.so"));
    CHECK(exec_bit(root / "previous-version/native_play"));
#endif
    CHECK(!leftovers(root));
    CHECK(can_roll_back(root));
    CHECK(previous_version(root) == "1.1.0");
}

TEST(previous_version_holds_only_the_version_just_replaced) {
    const fs::path root = old_install("t2");
    CHECK(install(tgz("p2a.tar.gz", release("1.2.0")), root).ok);
    write(root / "previous-version" / "stray.txt", "left by someone");
    std::vector<Item> v13 = release("1.3.0", false);
    v13.push_back(f("libb3render.so", "render 1.3.0"));
    const Result r = install(tgz("p2b.tar.gz", v13), root);
    CHECK(r.ok);
    CHECK(r.files == 5);
    CHECK(read(root / "native_play") == "play 1.3.0");
    CHECK(read(root / "previous-version/native_play") == "play 1.2.0");
    CHECK(read(root / "previous-version/libb3render.so") == "render 1.2.0");
    CHECK(!fs::exists(root / "previous-version/stray.txt"));
    CHECK(!fs::exists(root / "previous-version/icons"));
    CHECK(read(root / "icons/hicolor/256x256/apps/burnout3-native.png") == "icon 1.2.0");   // not in 1.3.0: left
    CHECK(previous_version(root) == "1.2.0");
    CHECK(!leftovers(root));
}

TEST(zip_install_windows_layout) {
    const fs::path root = kRoot / "t3";
    fs::remove_all(root);
    write(root / "Burnout3Launcher.exe", "launcher 1.1.0");
    write(root / "native_play.exe", "play 1.1.0");
    write(root / "LICENSES" / "SDL3-LICENSE.txt", "sdl old");
    const std::string top = "burnout3-native-1.2.0-win64";
    const Result r = install(zip("p3.zip", {{top + "/Burnout3Launcher.exe", "launcher 1.2.0", '0', 0755},
                                            {top + "/native_play.exe", "play 1.2.0", '0', 0755},
                                            {top + "/b3render.dll", "render 1.2.0", '0', 0644},
                                            {top + "/LICENSES/SDL3-LICENSE.txt", "sdl new", '0', 0644},
                                            {top + "/VERSION.txt", "Burnout 3 native port 1.2.0+abc\n", '0', 0644}}),
                             root);
    CHECK(r.ok);
    if (!r.ok) std::printf("    why: %s\n", r.why.c_str());
    CHECK(r.files == 5);
    CHECK(read(root / "Burnout3Launcher.exe") == "launcher 1.2.0");
    CHECK(read(root / "LICENSES/SDL3-LICENSE.txt") == "sdl new");
    CHECK(read(root / "previous-version/LICENSES/SDL3-LICENSE.txt") == "sdl old");
    CHECK(read(root / "previous-version/native_play.exe") == "play 1.1.0");
    CHECK(previous_version(root) == "unknown");   // the old install had no VERSION.txt
#ifndef _WIN32
    CHECK(exec_bit(root / "native_play.exe"));
    CHECK(!exec_bit(root / "b3render.dll"));
#endif
    CHECK(!leftovers(root));
}

TEST(long_names_gnu_ustar_prefix_and_pax) {
    const std::string deep = "icons/hicolor/256x256/apps/a-rather-long-folder-name-to-pass-one-hundred-characters/burnout3-native.png";
    CHECK((kTop + "/" + deep).size() > 100);
    for (int form = 0; form < 3; ++form) {
        const fs::path root = old_install("t4");
        std::vector<Item> items = release("1.2.0", false);
        items.push_back(f(deep, "deep"));
        if (form == 2) items.back().pax = true;
        const Result r = install(tgz("p4.tar.gz", items, form == 0), root);
        CHECK(r.ok);
        if (!r.ok) std::printf("    why (form %d): %s\n", form, r.why.c_str());
        CHECK(read(root / deep) == "deep");
    }
}

TEST(a_package_made_by_gnu_tar) {
    // the system's tar as tools/release/package_linux.sh runs it (skipped where that tar is not GNU tar)
#ifndef _WIN32
    const fs::path src = kRoot / "tarsrc";
    fs::remove_all(src);
    const std::string deep = "icons/hicolor/256x256/apps/a-rather-long-folder-name-to-pass-one-hundred-characters/burnout3-native.png";
    write(src / kTop / "native_play", "play 1.2.0");
    write(src / kTop / "libb3render.so", "render 1.2.0");
    write(src / kTop / deep, "deep");
    fs::permissions(src / kTop / "native_play", fs::perms(0755));
    fs::permissions(src / kTop / "libb3render.so", fs::perms(0644));
    const fs::path pkg = fs::absolute(kRoot / "pkg" / "gnutar.tar.gz");
    fs::create_directories(pkg.parent_path());
    const std::string cmd = "tar -C '" + src.string() + "' --sort=name --owner=0 --group=0 --numeric-owner -czf '" + pkg.string() + "' '" + kTop +
                            "' 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) {
        std::printf("    (no GNU tar: skipped)\n");
        return;
    }
    const fs::path root = old_install("t5");
    const Result r = install(pkg, root);
    CHECK(r.ok);
    if (!r.ok) std::printf("    why: %s\n", r.why.c_str());
    CHECK(read(root / "native_play") == "play 1.2.0");
    CHECK(read(root / deep) == "deep");
    CHECK(exec_bit(root / "native_play"));
    CHECK(!exec_bit(root / "libb3render.so"));
    CHECK(read(root / "previous-version/native_play") == "play 1.1.0");
#endif
}

TEST(refused_packages_change_nothing) {
    const fs::path root = old_install("t6");
    CHECK(install(tgz("p6.tar.gz", release("1.2.0")), root).ok);   // a previous-version/ to keep as well
    const auto before = snapshot(root);
    const std::string top = kTop + "/";
    struct Bad {
        const char* what;
        std::vector<Item> items;
        bool tar_only;
    };
    const std::vector<Bad> bad = {
        {"..", {f("a.txt", "a"), {top + "../evil.txt", "x"}}, false},
        {"... at the start", {{"../evil.txt", "x"}}, false},
        {"absolute", {f("a.txt", "a"), {"/tmp/evil.txt", "x"}}, false},
        {"backslash absolute", {f("a.txt", "a"), {"\\evil.txt", "x"}}, false},
        {"drive letter", {f("a.txt", "a"), {top + "C:/evil.txt", "x"}}, false},
        {"symlink", {f("a.txt", "a"), {top + "lib.so", "/etc/passwd", '2', 0777}}, false},
        {"hard link", {f("a.txt", "a"), {top + "b.txt", top + "a.txt", '1', 0644}}, true},
        {"device", {f("a.txt", "a"), {top + "dev", "", '3', 0644}}, true},
        {"fifo", {f("a.txt", "a"), {top + "fifo", "", '6', 0644}}, false},
        {"second top folder", {f("a.txt", "a"), {"burnout3-native-other/b.txt", "x"}}, false},
        {"file beside the top folder", {f("a.txt", "a"), {"b.txt", "x"}}, false},
        {"not our package", {{"something/a.txt", "a"}}, false},
        {"reserved name", {f("a.txt", "a"), f("previous-version/native_play", "x")}, false},
        {"reserved name 2", {f("a.txt", "a"), f(".update-staging/x", "x")}, false},
        {"reserved name 3 (portable data)", {f("a.txt", "a"), f("appdata/native/pc_settings.ini", "x")}, false},
        {"reserved name 4 (portable data)", {f("a.txt", "a"), f("appdata.moved-20261120-120000/native/x", "x")}, false},
        {"case twins", {f("a.txt", "a"), f("A.txt", "b")}, false},
        {"file and folder", {f("a", "a"), f("a/b.txt", "b")}, false},
        {"device name", {f("a.txt", "a"), f("nul.txt", "x")}, false},
        {"empty", {d("")}, false},
    };
    for (const Bad& b : bad)
        for (int z = 0; z < 2; ++z) {
            if (z == 1 && b.tar_only) continue;
            const Result r = z ? install(zip("bad.zip", b.items), root) : install(tgz("bad.tar.gz", b.items), root);
            CHECK(!r.ok);
            if (r.ok) std::printf("    installed: %s (%s)\n", b.what, z ? "zip" : "tar.gz");
            CHECK(snapshot(root) == before);
        }
    // limits
    Limits small;
    small.max_file = 5;
    CHECK(!install(tgz("big.tar.gz", {f("a.txt", "123456")}), root, small).ok);
    CHECK(!install(zip("big.zip", {f("a.txt", "123456")}), root, small).ok);
    small = {};
    small.max_entries = 2;
    CHECK(!install(tgz("many.tar.gz", {f("a", "1"), f("b", "2"), f("c", "3")}), root, small).ok);
    CHECK(!install(zip("many.zip", {f("a", "1"), f("b", "2"), f("c", "3")}), root, small).ok);
    small = {};
    small.max_total = 5;
    CHECK(!install(tgz("total.tar.gz", {f("a", "123"), f("b", "456")}), root, small).ok);
    CHECK(snapshot(root) == before);
    // damaged: cut short, a bad header checksum, not an archive, missing
    const std::string good = gzip(make_tar(release("1.3.0"), true));
    write(kRoot / "pkg" / "cut.tar.gz", good.substr(0, good.size() / 2));
    Result r = install(kRoot / "pkg" / "cut.tar.gz", root);
    CHECK(!r.ok && r.why.find("ends early") != std::string::npos);
    std::string tar = make_tar(release("1.3.0"), true);
    tar[0] ^= 1;
    write(kRoot / "pkg" / "sum.tar.gz", gzip(tar));
    CHECK(!install(kRoot / "pkg" / "sum.tar.gz", root).ok);
    write(kRoot / "pkg" / "junk.tar.gz", "\x1f\x8bjunk junk junk");
    CHECK(!install(kRoot / "pkg" / "junk.tar.gz", root).ok);
    write(kRoot / "pkg" / "text.zip", "hello");
    CHECK(!install(kRoot / "pkg" / "text.zip", root).ok);
    CHECK(!install(kRoot / "pkg" / "missing.zip", root).ok);
    std::string zbytes = make_zip(release("1.3.0"));
    zbytes[zbytes.find("play 1.3.0")] ^= 1;   // inside native_play's data: its CRC fails
    write(kRoot / "pkg" / "crc.zip", zbytes);
    CHECK(!install(kRoot / "pkg" / "crc.zip", root).ok);
    CHECK(snapshot(root) == before);
    // a folder in the install where the package has a file; a file where it needs a folder
    fs::create_directories(root / "libb3render.so.d");
    const auto before2 = snapshot(root);
    r = install(tgz("dir.tar.gz", {f("native_play", "x"), f("libb3render.so.d", "x")}), root);
    CHECK(!r.ok);
    r = install(tgz("dir2.tar.gz", {f("native_play", "x"), f("notes.txt/x", "x")}), root);
    CHECK(!r.ok);
    CHECK(snapshot(root) == before2);
}

TEST(roll_back_and_redo) {
    const fs::path root = old_install("t7");
    const auto v110 = snapshot(root);
    CHECK(!can_roll_back(root));
    CHECK(previous_version(root).empty());
    CHECK(!roll_back(root).ok);
    CHECK(snapshot(root) == v110);
    CHECK(install(tgz("p7.tar.gz", release("1.2.0")), root).ok);
    auto strip_pv = [](std::map<std::string, std::string> m) {
        for (auto it = m.begin(); it != m.end();)
            it = it->first.find("previous-version") != std::string::npos ? m.erase(it) : std::next(it);
        return m;
    };
    const auto v120 = snapshot(root);
    Result r = roll_back(root);
    CHECK(r.ok);
    if (!r.ok) std::printf("    why: %s\n", r.why.c_str());
    CHECK(r.files == 6);
    CHECK(read(root / "native_play") == "play 1.1.0");
    CHECK(!fs::exists(root / "libb3render.so"));
    CHECK(read(root / "previous-version/native_play") == "play 1.2.0");
    CHECK(read(root / "previous-version/libb3render.so") == "render 1.2.0");
    CHECK(read(root / "notes.txt") == "my notes");
#ifndef _WIN32
    CHECK(exec_bit(root / "native_play"));
    CHECK(exec_bit(root / "previous-version/native_play"));
#endif
    // the 1.1.0 files as they were (the update's new folders gone again)
    CHECK(strip_pv(snapshot(root)) == v110);
    CHECK(can_roll_back(root));
    CHECK(previous_version(root) == "1.2.0");
    CHECK(!leftovers(root));
    // again: the update back
    r = roll_back(root);
    CHECK(r.ok);
    CHECK(snapshot(root) == v120);
    CHECK(previous_version(root) == "1.1.0");
    CHECK(!leftovers(root));
}

// portable mode (1.3.0): <install>/appdata (the player's settings, saves, mods ...) byte for byte through an update,
// its roll back and the leftovers' cleaning
TEST(update_and_roll_back_keep_the_portable_data) {
    const fs::path root = old_install("t7p");
    write(root / "appdata" / "native" / "pc_settings.ini", "vsync = 1\n");
    write(root / "appdata" / "native" / "saves" / "profile-1.sav", std::string("SAVE\0\1\2", 7));
    write(root / "appdata" / "native" / "native_play", "not the program: a file of the player's with a package's name");
    write(root / "appdata" / "native" / "mods" / "my-mod" / "mod.ini", "id = my-mod\n");
    write(root / "appdata.moved-20261120-101010" / "native" / "launcher.ini", "old\n");
    const auto data = snapshot(root / "appdata");
    const auto old = snapshot(root / "appdata.moved-20261120-101010");
    Result r = install(tgz("p7p.tar.gz", release("1.2.0")), root);
    CHECK(r.ok);
    CHECK(read(root / "native_play") == "play 1.2.0");
    CHECK(snapshot(root / "appdata") == data);
    CHECK(snapshot(root / "appdata.moved-20261120-101010") == old);
    CHECK(!fs::exists(root / "previous-version" / "appdata"));
    r = roll_back(root);
    CHECK(r.ok);
    CHECK(read(root / "native_play") == "play 1.1.0");
    CHECK(snapshot(root / "appdata") == data);
    CHECK(snapshot(root / "appdata.moved-20261120-101010") == old);
    clean_leftovers(root);
    CHECK(snapshot(root / "appdata") == data);
    // a package with appdata/ in it is refused and changes nothing
    const auto before = snapshot(root);
    CHECK(!install(tgz("p7q.tar.gz", {f("native_play", "x"), f("appdata/native/saves/profile-1.sav", "theirs")}), root).ok);
    CHECK(snapshot(root) == before);
}

TEST(clean_leftovers_removes_and_undoes) {
    const fs::path root = old_install("t8");
    write(root / ".update-staging" / "files" / "x.bin", "half unpacked");
    write(root / ".update-trash" / "staging" / "y.bin", "old");
    clean_leftovers(root);
    CHECK(!leftovers(root));
    CHECK(read(root / "native_play") == "play 1.1.0");

    // an install cut short half way through its swap (the power went): undone at the next start
    CHECK(install(tgz("p8.tar.gz", release("1.2.0")), root).ok);   // previous-version/: 1.1.0
    const auto before = snapshot(root);
    write(root / ".update-staging" / "files" / "native_play", "play 1.3.0");
    write(root / ".update-staging" / "files" / "zzz.txt", "new in 1.3.0");
    write(root / ".update-staging" / "update-manifest", "burnout3-native update manifest 1\ninstalled 1.3.0\nprevious 1.2.0\n11 native_play\n01 zzz.txt\n");
    write(root / ".update-staging" / "swap-plan",
          "burnout3-native swap plan 1\n"
          "D\tprevious-version\t.update-trash/previous-version\n"
          "N\tprevious-version\n"
          "R\tnative_play\tprevious-version/native_play\n"
          "R\t.update-staging/files/native_play\tnative_play\n"
          "R\t.update-staging/files/zzz.txt\tzzz.txt\n"
          "R\t.update-staging/update-manifest\tprevious-version/.update-manifest\n");
    // what ran: D, N, both renames of native_play; swap-done has the first three (the power went before the fourth's line)
    write(root / ".update-staging" / "swap-done", "0\n1\n2\n");
    fs::create_directories(root / ".update-trash");
    fs::rename(root / "previous-version", root / ".update-trash" / "previous-version");
    fs::create_directory(root / "previous-version");
    fs::rename(root / "native_play", root / "previous-version" / "native_play");
    fs::rename(root / ".update-staging" / "files" / "native_play", root / "native_play");
    CHECK(read(root / "native_play") == "play 1.3.0");
    clean_leftovers(root);
    CHECK(snapshot(root) == before);
    CHECK(read(root / "native_play") == "play 1.2.0");
    CHECK(read(root / "previous-version/native_play") == "play 1.1.0");
    CHECK(previous_version(root) == "1.1.0");

    // cut short before anything ran: previous-version/ (the old one) is kept
    write(root / ".update-staging" / "files" / "native_play", "play 1.3.0");
    write(root / ".update-staging" / "update-manifest", "x");
    write(root / ".update-staging" / "swap-plan",
          "burnout3-native swap plan 1\n"
          "D\tprevious-version\t.update-trash/previous-version\n"
          "N\tprevious-version\n"
          "R\tnative_play\tprevious-version/native_play\n"
          "R\t.update-staging/files/native_play\tnative_play\n"
          "R\t.update-staging/update-manifest\tprevious-version/.update-manifest\n");
    clean_leftovers(root);
    CHECK(snapshot(root) == before);
    // a roll back's swap through .update-staging/files, cut short after its three renames (swap-done has them all) ...
    const std::string cycle = "burnout3-native swap plan 1\n"
                              "R\tnative_play\t.update-staging/files/native_play\n"
                              "R\tprevious-version/native_play\tnative_play\n"
                              "R\t.update-staging/files/native_play\tprevious-version/native_play\n";
    write(root / ".update-staging" / "swap-plan", cycle);
    write(root / ".update-staging" / "swap-done", "0\n1\n2\n");
    fs::create_directories(root / ".update-staging" / "files");
    fs::rename(root / "native_play", root / ".update-staging" / "files" / "native_play");
    fs::rename(root / "previous-version" / "native_play", root / "native_play");
    fs::rename(root / ".update-staging" / "files" / "native_play", root / "previous-version" / "native_play");
    clean_leftovers(root);
    CHECK(snapshot(root) == before);
    // ... and one whose undo was cut short after its last step (but before the plan was removed): not undone twice
    write(root / ".update-staging" / "swap-plan", cycle);
    write(root / ".update-staging" / "swap-done", "0\n1\n2\nu 2\nu 1\n");   // "u 0" not written: told from the disk
    fs::create_directories(root / ".update-staging" / "files");
    clean_leftovers(root);
    CHECK(snapshot(root) == before);
    CHECK(read(root / "native_play") == "play 1.2.0");
    // a damaged plan: nothing is removed (no guessing which files are needed)
    write(root / ".update-staging" / "swap-plan", "burnout3-native swap plan 1\nR\t../x\ty\n");
    clean_leftovers(root);
    CHECK(fs::exists(root / ".update-staging" / "swap-plan"));
    CHECK(!install(tgz("p8b.tar.gz", release("1.3.0")), root).ok);   // refused while it is there
    fs::remove_all(root / ".update-staging");
    CHECK(snapshot(root) == before);
}

// lock/b.txt made impossible to move: its folder read-only (POSIX), or the file held open without delete sharing, as
// another program would (Windows)
struct Lock {
    explicit Lock(const fs::path& file) : file_(file) {
#ifndef _WIN32
        fs::permissions(file_.parent_path(), fs::perms(0555));
#else
        h_ = CreateFileW(file_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
#endif
    }
    ~Lock() {
#ifndef _WIN32
        fs::permissions(file_.parent_path(), fs::perms(0755));
#else
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
#endif
    }
    fs::path file_;
#ifdef _WIN32
    HANDLE h_ = INVALID_HANDLE_VALUE;
#endif
};

TEST(a_failing_swap_leaves_the_install_as_it_was) {
#ifndef _WIN32
    if (::geteuid() == 0) {
        std::printf("    (root ignores folder permissions: skipped)\n");
        return;
    }
#endif
    const fs::path root = old_install("t9");
    write(root / "lock" / "b.txt", "b 1.1.0");
    write(root / "z.txt", "z 1.1.0");
    CHECK(install(tgz("p9a.tar.gz", {f("a.txt", "a 1.1.5"), f("native_play", "play 1.1.5", 0755)}), root).ok);   // a previous-version/
    const auto before = snapshot(root);
    Result r;
    {
        Lock lock(root / "lock" / "b.txt");
        // in the swap's order: a.txt, lock/b.txt (fails), native_play, new.txt, z.txt
        r = install(tgz("p9b.tar.gz", {f("a.txt", "a 1.2.0"), f("lock/b.txt", "b 1.2.0"), f("native_play", "play 1.2.0", 0755),
                                       f("new/new.txt", "new"), f("z.txt", "z 1.2.0")}),
                    root);
    }
    CHECK(!r.ok);
    CHECK(r.why.find("lock/b.txt") != std::string::npos);
    CHECK(r.why.find("nothing was changed") != std::string::npos);
    std::printf("    why: %s\n", r.why.c_str());
    CHECK(snapshot(root) == before);
    CHECK(!leftovers(root));
    CHECK(previous_version(root) == "1.1.0");

    // a roll back that fails half way: the same
    CHECK(install(tgz("p9c.tar.gz", {f("a.txt", "a 1.2.0"), f("lock/b.txt", "b 1.2.0"), f("z.txt", "z 1.2.0")}), root).ok);
    const auto before2 = snapshot(root);
    Result r2;
    {
        Lock lock(root / "lock" / "b.txt");
        r2 = roll_back(root);
    }
    CHECK(!r2.ok);
    std::printf("    why: %s\n", r2.why.c_str());
    CHECK(snapshot(root) == before2);
    CHECK(!leftovers(root));
    CHECK(roll_back(root).ok);
    CHECK(read(root / "lock/b.txt") == "b 1.1.0");
    CHECK(read(root / "a.txt") == "a 1.1.5");
}

#ifdef _WIN32
TEST(windows_the_running_program_can_be_renamed) {
    // what the swap relies on: the launcher replaces its own .exe (and loaded DLLs) by renaming them on NTFS
    wchar_t self[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
    CHECK(n > 0 && n < MAX_PATH);
    const fs::path p(self);
    fs::path q = p;
    q += L".renamed";
    std::error_code ec;
    fs::rename(p, q, ec);
    CHECK(!ec);
    if (!ec) {
        fs::rename(q, p, ec);
        CHECK(!ec);
    }
}
#endif

// robust2: the installed version and a folder the update cannot be written to, found before any download
TEST(installed_version_and_install_blocker) {
    const fs::path root = old_install("blocker");
    CHECK(installed_version(root) == "1.1.0");
    CHECK(installed_version(kRoot / "nothing-here").empty());
    {   // macOS: Contents/MacOS's VERSION.txt is in Contents/Resources
        const fs::path app = kRoot / "mac" / "Burnout 3.app" / "Contents";
        fs::remove_all(kRoot / "mac");
        write(app / "Resources" / "VERSION.txt", "Burnout 3 native port 1.2.1+1a2b3c4d\n");
        fs::create_directories(app / "MacOS");
        CHECK(installed_version(app / "MacOS") == "1.2.1+1a2b3c4d");
    }
    CHECK(install_blocker(root, false, false).empty());
    CHECK(!fs::exists(root / ".update-write-test"));
    CHECK(install_blocker(root, true, false).find("AppImage") != std::string::npos);
    CHECK(install_blocker(root, false, true).find("Flatpak") != std::string::npos);
    CHECK(!install_blocker(kRoot / "nothing-here", false, false).empty());
#ifndef _WIN32
    if (::geteuid() != 0) {   // a folder this user cannot write (/opt as a player has it)
        fs::permissions(root, fs::perms(0555));
        CHECK(install_blocker(root, false, false).find("cannot be written") != std::string::npos);
        fs::permissions(root, fs::perms(0755));
    }
#endif
}

TEST_MAIN("test_launcher_installer: the updater's install step (packages, swap, previous-version, roll back)")
