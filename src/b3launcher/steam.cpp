// steam.cpp -- ADD TO STEAM (steam.h): shortcuts.vdf, the grid's pictures, where Steam is and whether it runs.
#include "steam.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <sys/types.h>
#endif

namespace b3launcher::steam {

namespace {
std::string u8(const fs::path& p) {   // a path as UTF-8 (the VDF's text, the launcher's messages)
    const auto s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.c_str()), s.size());
}
}  // namespace

// ---- the shortcut's id ----

std::uint32_t crc32(std::string_view s) {
    static const auto table = [] {
        std::uint32_t t[256];
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return std::vector<std::uint32_t>(t, t + 256);
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (const char ch : s) c = table[(c ^ static_cast<std::uint8_t>(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::uint32_t shortcut_appid(std::string_view exe, std::string_view name) {
    std::string s(exe);
    s += name;
    return crc32(s) | 0x80000000u;
}

std::int32_t vdf_int(std::uint32_t appid) {
    std::int32_t v;
    std::memcpy(&v, &appid, 4);
    return v;
}

std::string vdf_quoted(std::string_view path) { return "\"" + std::string(path) + "\""; }

std::uint32_t appid_of(const NewShortcut& s) { return shortcut_appid(vdf_quoted(s.exe), s.name); }

// ---- the grid's file names ----

std::string grid_name(std::uint32_t appid, Art art, std::string_view ext) {
    std::string n = std::to_string(appid);
    switch (art) {
    case Art::kCover: n += "p"; break;
    case Art::kWide: break;
    case Art::kHero: n += "_hero"; break;
    case Art::kLogo: n += "_logo"; break;
    case Art::kIcon: n += "_icon"; break;
    }
    return n + "." + std::string(ext);
}

std::string image_kind(const Bytes& h) {
    static const std::uint8_t png[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (h.size() >= 8 && std::memcmp(h.data(), png, 8) == 0) return "png";
    if (h.size() >= 3 && h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) return "jpg";
    return {};
}

// ---- shortcuts.vdf ----

namespace {

enum : std::uint8_t { kMap = 0x00, kString = 0x01, kInt32 = 0x02, kFloat = 0x03, kPointer = 0x04, kWide = 0x05,
                      kColor = 0x06, kUint64 = 0x07, kEnd = 0x08, kInt64 = 0x0A };

struct Node {
    std::uint8_t type = 0;
    std::string name, str;
    std::uint32_t u32 = 0;
    std::vector<Node> kids;
    std::size_t begin = 0, end = 0, end_marker = 0;   // a map's end byte (0x08)
};

bool read_cstr(const Bytes& b, std::size_t& p, std::string* out) {
    std::size_t e = p;
    while (e < b.size() && b[e] != 0) ++e;
    if (e >= b.size()) return false;
    if (out) out->assign(reinterpret_cast<const char*>(b.data()) + p, e - p);
    p = e + 1;
    return true;
}

// A map's children from `p` to its end byte; `p` ends after that byte
bool parse_children(const Bytes& b, std::size_t& p, int depth, std::vector<Node>& out, std::size_t& end_marker, std::string& why) {
    while (true) {
        if (p >= b.size()) { why = "it ends inside a list"; return false; }
        const std::uint8_t t = b[p];
        if (t == kEnd) {
            end_marker = p++;
            return true;
        }
        Node n;
        n.begin = p++;
        n.type = t;
        if (!read_cstr(b, p, &n.name)) { why = "a name runs past the end"; return false; }
        auto need = [&](std::size_t k) { return b.size() - p >= k; };
        switch (t) {
        case kMap:
            if (depth >= 32) { why = "it nests too deep"; return false; }
            if (!parse_children(b, p, depth + 1, n.kids, n.end_marker, why)) return false;
            break;
        case kString:
            if (!read_cstr(b, p, &n.str)) { why = "a text runs past the end"; return false; }
            break;
        case kInt32: case kFloat: case kPointer: case kColor:
            if (!need(4)) { why = "a number runs past the end"; return false; }
            n.u32 = std::uint32_t(b[p]) | std::uint32_t(b[p + 1]) << 8 | std::uint32_t(b[p + 2]) << 16 | std::uint32_t(b[p + 3]) << 24;
            p += 4;
            break;
        case kUint64: case kInt64:
            if (!need(8)) { why = "a number runs past the end"; return false; }
            p += 8;
            break;
        case kWide:   // UTF-16 text up to a 0x0000
            while (true) {
                if (!need(2)) { why = "a text runs past the end"; return false; }
                const bool z = b[p] == 0 && b[p + 1] == 0;
                p += 2;
                if (z) break;
            }
            break;
        default: {
            char m[64];
            std::snprintf(m, sizeof m, "an unknown field type 0x%02X at byte %zu", unsigned(t), n.begin);
            why = m;
            return false;
        }
        }
        n.end = p;
        out.push_back(std::move(n));
    }
}

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

const Bytes& empty_list() {
    static const Bytes e = [] {
        Bytes b{kMap};
        for (const char c : std::string_view("shortcuts")) b.push_back(std::uint8_t(c));
        b.push_back(0);
        b.push_back(kEnd);   // the "shortcuts" map
        b.push_back(kEnd);   // the file
        return b;
    }();
    return e;
}

void put_name(Bytes& b, std::uint8_t type, std::string_view name) {
    b.push_back(type);
    b.insert(b.end(), name.begin(), name.end());
    b.push_back(0);
}
void put_str(Bytes& b, std::string_view name, std::string_view v) {
    put_name(b, kString, name);
    b.insert(b.end(), v.begin(), v.end());
    b.push_back(0);
}
void put_int(Bytes& b, std::string_view name, std::uint32_t v) {
    put_name(b, kInt32, name);
    for (int k = 0; k < 4; ++k) b.push_back(std::uint8_t(v >> (8 * k)));
}

std::string_view unquote(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

}  // namespace

ShortcutsFile parse_shortcuts(const Bytes& bytes) {
    ShortcutsFile f;
    f.bytes = bytes.empty() ? empty_list() : bytes;
    std::vector<Node> root;
    std::size_t p = 0, root_end = 0;
    if (!parse_children(f.bytes, p, 0, root, root_end, f.error)) return f;
    const Node* list = nullptr;
    for (const Node& n : root)
        if (n.type == kMap && iequal(n.name, "shortcuts")) list = &n;
    if (!list) {
        f.error = "it has no \"shortcuts\" list";
        return f;
    }
    for (const Node& e : list->kids) {
        if (e.type != kMap) continue;
        Shortcut s;
        s.key = e.name;
        s.begin = e.begin;
        s.end = e.end;
        for (const Node& k : e.kids) {
            if (k.type == kInt32 && iequal(k.name, "appid")) s.appid = k.u32;
            else if (k.type == kString && iequal(k.name, "appname")) s.name = k.str;
            else if (k.type == kString && iequal(k.name, "exe")) s.exe = k.str;
            else if (k.type == kString && iequal(k.name, "startdir")) s.start_dir = k.str;
            else if (k.type == kString && iequal(k.name, "icon")) s.icon = k.str;
        }
        f.entries.push_back(std::move(s));
    }
    f.insert_at = list->end_marker;
    f.ok = true;
    return f;
}

Bytes encode_entry(const std::string& key, const NewShortcut& s) {
    Bytes b;
    put_name(b, kMap, key);
    put_int(b, "appid", appid_of(s));
    put_str(b, "AppName", s.name);
    put_str(b, "Exe", vdf_quoted(s.exe));
    put_str(b, "StartDir", vdf_quoted(s.start_dir));
    put_str(b, "icon", s.icon);
    put_str(b, "ShortcutPath", "");
    put_str(b, "LaunchOptions", s.launch_options);
    put_int(b, "IsHidden", 0);
    put_int(b, "AllowDesktopConfig", 1);
    put_int(b, "AllowOverlay", 1);
    put_int(b, "OpenVR", 0);
    put_int(b, "Devkit", 0);
    put_str(b, "DevkitGameID", "");
    put_int(b, "DevkitOverrideAppID", 0);
    put_int(b, "LastPlayTime", 0);
    put_str(b, "FlatpakAppID", "");
    put_str(b, "sortas", "");   // (current Steam versions write it; an empty one sorts by AppName)
    put_name(b, kMap, "tags");
    b.push_back(kEnd);
    b.push_back(kEnd);
    return b;
}

Bytes with_entry(const ShortcutsFile& f, const NewShortcut& s) {
    std::set<std::string> keys;
    long long next = 0;
    for (const Shortcut& e : f.entries) {
        keys.insert(e.key);
        if (!e.key.empty() && e.key.size() < 10 && std::all_of(e.key.begin(), e.key.end(), [](char c) { return c >= '0' && c <= '9'; }))
            next = std::max(next, std::stoll(e.key) + 1);
    }
    while (keys.count(std::to_string(next))) ++next;
    const Bytes entry = encode_entry(std::to_string(next), s);
    Bytes out(f.bytes.begin(), f.bytes.begin() + std::ptrdiff_t(f.insert_at));
    out.insert(out.end(), entry.begin(), entry.end());
    out.insert(out.end(), f.bytes.begin() + std::ptrdiff_t(f.insert_at), f.bytes.end());
    return out;
}

bool translocated(std::string_view exe_path) { return exe_path.find("/AppTranslocation/") != std::string_view::npos; }

bool same_exe(std::string_view a, std::string_view b, bool fold) {
    a = unquote(a);
    b = unquote(b);
    if (a.empty() || a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (fold) {
            if (x == '/') x = '\\';
            if (y == '/') y = '\\';
            x = char(std::tolower(static_cast<unsigned char>(x)));
            y = char(std::tolower(static_cast<unsigned char>(y)));
        }
        if (x != y) return false;
    }
    return true;
}

bool fold_paths() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

const Shortcut* find_by_exe(const ShortcutsFile& f, std::string_view exe, bool fold) {
    for (const Shortcut& e : f.entries)
        if (same_exe(e.exe, exe, fold)) return &e;
    return nullptr;
}

std::uint32_t entry_appid(const Shortcut& e) {
    if (e.appid && *e.appid != 0) return *e.appid;
    return shortcut_appid(e.exe, e.name);
}

// ---- files ----

bool read_file(const fs::path& p, Bytes& out, std::size_t limit) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.clear();
    char buf[65536];
    while (f.read(buf, sizeof buf) || f.gcount() > 0) {
        out.insert(out.end(), buf, buf + f.gcount());
        if (out.size() > limit) return false;
    }
    return !f.bad();
}

bool write_atomic(const fs::path& to, const Bytes& data, std::string* why) {
    fs::path tmp = to;
    tmp += ".b3tmp";
    std::error_code ec;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (f) f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (f) f.flush();
        if (!f) {
            f.close();
            fs::remove(tmp, ec);
            if (why) *why = u8(tmp);
            return false;
        }
    }
    fs::rename(tmp, to, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (why) *why = u8(to);
        return false;
    }
    return true;
}

fs::path backup_path(const fs::path& vdf) {
    fs::path b = vdf;
    b += ".b3backup";
    return b;
}

bool backup_once(const fs::path& vdf, std::string* why) {
    std::error_code ec;
    const fs::path b = backup_path(vdf);
    if (!fs::exists(vdf, ec) || fs::exists(b, ec)) return true;
    if (!fs::copy_file(vdf, b, fs::copy_options::none, ec) || ec) {
        if (why) *why = u8(b);
        return false;
    }
    return true;
}

// ---- where Steam is ----

Os this_os() {
#if defined(_WIN32)
    return Os::kWindows;
#elif defined(__APPLE__)
    return Os::kMac;
#else
    return Os::kLinux;
#endif
}

#ifdef _WIN32
namespace {
fs::path registry_path(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buf[4096];
    DWORD n = sizeof buf;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &n) != ERROR_SUCCESS) return {};
    buf[sizeof buf / sizeof buf[0] - 1] = 0;
    return buf[0] ? fs::path(std::wstring(buf)) : fs::path();
}
fs::path env_path(const wchar_t* name) {
    const wchar_t* v = _wgetenv(name);
    return v && *v ? fs::path(std::wstring(v)) : fs::path();
}
}  // namespace
#endif

Places this_places() {
    Places p;
#ifdef _WIN32
    p.home = env_path(L"USERPROFILE");
    for (const fs::path& r : {registry_path(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"),
                              registry_path(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"),
                              registry_path(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath")})
        if (!r.empty()) p.registry.push_back(r);
    for (const wchar_t* v : {L"ProgramFiles(x86)", L"ProgramFiles"})
        if (fs::path f = env_path(v); !f.empty()) p.program_files.push_back(f);
    if (p.program_files.empty()) p.program_files.push_back(fs::path(L"C:\\Program Files (x86)"));
#else
    if (const char* h = std::getenv("HOME"); h && *h) p.home = h;
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) p.xdg_data_home = x;
#endif
    return p;
}

std::vector<fs::path> candidate_roots(const Places& p, Os os) {
    std::vector<fs::path> c;
    switch (os) {
    case Os::kWindows:
        c = p.registry;
        for (const fs::path& f : p.program_files) c.push_back(f / "Steam");
        break;
    case Os::kLinux:
        if (p.home.empty()) break;
        c.push_back(p.home / ".steam" / "steam");
        c.push_back(p.home / ".local" / "share" / "Steam");
        if (!p.xdg_data_home.empty()) c.push_back(p.xdg_data_home / "Steam");
        c.push_back(p.home / ".steam" / "root");
        c.push_back(p.home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam");   // Flatpak
        c.push_back(p.home / ".var" / "app" / "com.valvesoftware.Steam" / ".steam" / "steam");
        c.push_back(p.home / "snap" / "steam" / "common" / ".local" / "share" / "Steam");                  // Snap
        break;
    case Os::kMac:
        if (!p.home.empty()) c.push_back(p.home / "Library" / "Application Support" / "Steam");
        break;
    }
    return c;
}

std::vector<fs::path> find_roots(const Places& p, Os os) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const fs::path& c : candidate_roots(p, os)) {
        if (!fs::is_directory(c, ec)) continue;
        if (!fs::is_directory(c / "userdata", ec) && !fs::is_directory(c / "config", ec)) continue;
        // (Linux: ~/.steam/steam is a link to ~/.local/share/Steam; Windows: no canonical(), whose \\?\ form could reach
        // the icon path written into shortcuts.vdf)
        fs::path r = os == Os::kWindows ? c.lexically_normal() : fs::canonical(c, ec);
        if (ec) r = c;
        bool seen = false;
        for (const fs::path& o : out) seen = seen || fs::equivalent(o, r, ec) || o == r;
        if (!seen) out.push_back(r);
    }
    return out;
}

std::vector<std::string> account_ids(const fs::path& root) {
    std::vector<std::string> ids;
    std::error_code ec;
    fs::directory_iterator it(root / "userdata", ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.empty() || n.size() > 12 || !std::all_of(n.begin(), n.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        if (std::all_of(n.begin(), n.end(), [](char c) { return c == '0'; })) continue;   // 0: no account
        std::error_code e2;
        if (it->is_directory(e2)) ids.push_back(n);
    }
    std::sort(ids.begin(), ids.end(), [](const std::string& a, const std::string& b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    return ids;
}

// ---- is Steam running ----

bool steam_running_in_proc(const fs::path& proc) {
    std::error_code ec;
    fs::directory_iterator it(proc, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.empty() || !std::all_of(n.begin(), n.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        std::ifstream f(it->path() / "comm");
        std::string comm;
        if (f && std::getline(f, comm) && comm == "steam") return true;
    }
    return false;
}

bool steam_running() {
#if defined(_WIN32)
    bool found = false;
    if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W e{};
        e.dwSize = sizeof e;
        for (BOOL more = Process32FirstW(snap, &e); more && !found; more = Process32NextW(snap, &e))
            found = _wcsicmp(e.szExeFile, L"steam.exe") == 0;
        CloseHandle(snap);
    }
    if (!found) {   // the pid Steam keeps in the registry while it runs (0 once it has closed)
        DWORD pid = 0, n = sizeof pid;
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid", RRF_RT_REG_DWORD, nullptr, &pid, &n) == ERROR_SUCCESS &&
            pid != 0) {
            if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
                wchar_t img[MAX_PATH * 4];
                DWORD len = DWORD(sizeof img / sizeof img[0]);
                DWORD code = 0;
                if (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE && QueryFullProcessImageNameW(h, 0, img, &len)) {
                    const std::wstring s(img, len);
                    const std::size_t slash = s.find_last_of(L"\\/");
                    found = _wcsicmp(s.c_str() + (slash == std::wstring::npos ? 0 : slash + 1), L"steam.exe") == 0;
                }
                CloseHandle(h);
            }
        }
    }
    return found;
#elif defined(__APPLE__)
    const int n = proc_listallpids(nullptr, 0);
    if (n <= 0) return false;
    std::vector<pid_t> pids(std::size_t(n) + 64);
    const int got = proc_listallpids(pids.data(), int(pids.size() * sizeof(pid_t)));
    for (int k = 0; k < got && k < int(pids.size()); ++k) {
        char name[256] = {};
        if (pids[std::size_t(k)] > 0 && proc_name(pids[std::size_t(k)], name, sizeof name) > 0 && std::strcmp(name, "steam_osx") == 0) return true;
    }
    return false;
#else
    return steam_running_in_proc("/proc");
#endif
}

// ---- adding ----

std::size_t Result::ok_count() const {
    return std::size_t(std::count_if(accounts.begin(), accounts.end(), [](const AccountResult& a) { return a.ok; }));
}
std::size_t Result::added_count() const {
    return std::size_t(std::count_if(accounts.begin(), accounts.end(), [](const AccountResult& a) { return a.ok && a.added; }));
}

namespace {
// The pictures into <config>/grid; a slot's picture in the other format (png / jpg) removed, so that Steam shows
// the new one. Returns the first file that could not be written (empty: all written).
std::string write_art(const fs::path& config, std::uint32_t appid, const std::vector<ArtFile>& art) {
    std::error_code ec;
    const fs::path grid = config / "grid";
    fs::create_directories(grid, ec);
    std::string first_bad;
    for (const ArtFile& a : art) {
        for (const char* other : {"png", "jpg", "jpeg"})
            if (a.ext != other) fs::remove(grid / grid_name(appid, a.art, other), ec);
        std::string why;
        if (!write_atomic(grid / grid_name(appid, a.art, a.ext), a.bytes, &why) && first_bad.empty()) first_bad = why;
    }
    return first_bad;
}
}  // namespace

Result add(const Request& r) {
    Result res;
    res.steam_found = !r.roots.empty();
    std::error_code ec;
    const ArtFile* icon = nullptr;
    for (const ArtFile& a : r.art)
        if (a.art == Art::kIcon) icon = &a;
    for (const fs::path& root : r.roots)
        for (const std::string& id : account_ids(root)) {
            AccountResult acc;
            acc.id = id;
            acc.config = root / "userdata" / id / "config";
            const fs::path vdf = acc.config / "shortcuts.vdf";
            Bytes bytes;
            if (fs::exists(vdf, ec) && !read_file(vdf, bytes, 64u << 20)) {
                acc.why = "Could not read " + u8(vdf) + ".";
                res.accounts.push_back(acc);
                continue;
            }
            const ShortcutsFile f = parse_shortcuts(bytes);
            if (!f.ok) {
                std::fprintf(stderr, "b3launcher: Add to Steam: %s left as it is (%s)\n", u8(vdf).c_str(), f.error.c_str());
                acc.why = "A shortcuts.vdf of Steam's could not be read, so it was left as it is.";
                res.accounts.push_back(acc);
                continue;
            }
            NewShortcut s = r.shortcut;
            if (const Shortcut* old = find_by_exe(f, vdf_quoted(s.exe), fold_paths())) {
                acc.appid = entry_appid(*old);   // there already: its pictures only
            } else {
                acc.appid = appid_of(s);
                if (icon) s.icon = u8(acc.config / "grid" / grid_name(acc.appid, Art::kIcon, icon->ext));
                fs::create_directories(acc.config, ec);
                std::string why;
                if (!backup_once(vdf, &why)) {
                    acc.why = "Could not make the backup " + why + ".";
                    res.accounts.push_back(acc);
                    continue;
                }
                if (!write_atomic(vdf, with_entry(f, s), &why)) {
                    acc.why = "Could not write " + why + ".";
                    res.accounts.push_back(acc);
                    continue;
                }
                acc.added = true;
            }
            acc.ok = true;
            if (const std::string bad = write_art(acc.config, acc.appid, r.art); !bad.empty()) acc.art_why = "Could not write " + bad + ".";
            std::printf("b3launcher: Add to Steam: account %s: %s (appid %u)%s%s\n", id.c_str(), acc.added ? "added" : "there already, pictures updated",
                        unsigned(acc.appid), acc.art_why.empty() ? "" : "; ", acc.art_why.c_str());
            res.accounts.push_back(acc);
        }
    return res;
}

std::string summary(const Result& r) {
    if (!r.steam_found) return "Steam was not found on this computer. Install Steam and sign in once, then press Add to Steam again.";
    if (r.accounts.empty())
        return "Steam has no accounts on this computer yet. Start Steam, sign in, close Steam, then press Add to Steam again.";
    const std::size_t ok = r.ok_count(), added = r.added_count();
    std::string why, art_why;
    for (const AccountResult& a : r.accounts) {
        if (!a.ok && why.empty()) why = a.why;
        if (a.ok && art_why.empty()) art_why = a.art_why;
    }
    if (ok == 0) return "Not added to Steam. " + why;
    std::string s;
    if (added > 0) s = added == 1 ? "Added to Steam (1 account)." : "Added to Steam (" + std::to_string(added) + " accounts).";
    if (ok > added) {
        const std::size_t there = ok - added;
        if (!s.empty()) s += " ";
        s += there == 1 ? "Already in Steam (1 account): its pictures were updated." : "Already in Steam (" + std::to_string(there) + " accounts): its pictures were updated.";
    }
    s += " Start Steam to see it.";
    if (!why.empty()) s += " " + why;
    if (!art_why.empty()) s += " " + art_why;
    return s;
}

}  // namespace b3launcher::steam
