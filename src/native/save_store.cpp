// The user's save folder (src/native/save_store.h).
#include "save_store.h"
#include "b3port/fileio.h"
#include "b3port/user_dir.h"

#include <algorithm>
#include <ctime>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>
#include <tuple>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>   // saveflow: proc_pidinfo (process_start)
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#endif
#endif

namespace b3native::save {
namespace {
namespace fs = std::filesystem;

constexpr int kSlots = 5;   // as load_profile_slot / read_active_slot (five slots)

const char* env(const char* name) {
    const char* v = std::getenv(name);
    return v && *v ? v : nullptr;
}

// fsrobust: slot `slot`'s backups (backups/<prefix><YYYYMMDD-HHMMSS>[-<n>].sav, backup_profile), oldest first: by the
// time, then n (none: 1). (Sorting the names put "...-2.sav" before ".sav" of the same second.)
struct Backup {
    std::string stamp;
    long n = 1;
    fs::path path;
};
std::vector<Backup> slot_backups(const fs::path& root, int slot) {
    std::error_code ec;
    const std::string prefix = "profile-" + std::to_string(slot) + "-";
    std::vector<Backup> out;
    // (the error_code increment: a folder error ends the list, never throws)
    for (fs::directory_iterator it(root / "backups", ec), end; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& e = *it;
        const std::string name = b3io::u8string(e.path().filename());
        if (name.rfind(prefix, 0) != 0 || e.path().extension() != ".sav" || name.size() < prefix.size() + 15 + 4) continue;
        const std::string rest = name.substr(prefix.size(), name.size() - prefix.size() - 4);
        Backup b;
        b.stamp = rest.substr(0, 15);
        b.n = rest.size() > 16 ? std::strtol(rest.c_str() + 16, nullptr, 10) : 1;
        b.path = e.path();
        out.push_back(std::move(b));
    }
    std::sort(out.begin(), out.end(), [](const Backup& a, const Backup& b) { return std::tie(a.stamp, a.n) < std::tie(b.stamp, b.n); });
    return out;
}

fs::path newest_backup(const fs::path& root, int slot) {
    const std::vector<Backup> all = slot_backups(root, slot);
    return all.empty() ? fs::path() : all.back().path;
}

bool file_equals(const fs::path& p, const Bytes& bytes) {
    std::error_code ec;
    if (fs::file_size(p, ec) != bytes.size() || ec) return false;
    std::ifstream f(p, std::ios::binary);
    return f && std::equal(bytes.begin(), bytes.end(), std::istreambuf_iterator<char>(f),
                           [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); });
}

std::string local_stamp(std::int64_t now_unix) {
    const std::time_t t = static_cast<std::time_t>(now_unix);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &lt);
    return stamp;
}

bool same_bytes(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (a.empty() || b.empty() || fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    return fa && fb && std::equal(std::istreambuf_iterator<char>(fa), std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(fb));
}

}  // namespace

fs::path user_data_dir() { return b3io::user_dir(); }   // portable mode (1.3.0): b3port/user_dir.h decides

SandboxUse sandbox_use(bool sandbox, bool already_host, bool host_reachable, bool host_writable, bool own_has_saves,
                       bool own_has_settings, bool host_has_data) {
    if (!sandbox) return SandboxUse::kOwn;
    if (already_host) return SandboxUse::kHost;
    if (!host_reachable || !host_writable) return SandboxUse::kOwn;   // (a read-only grant would lose every save)
    if (own_has_saves) return host_has_data ? SandboxUse::kOwnBothHaveData : SandboxUse::kOwn;
    return own_has_settings ? SandboxUse::kHostOwnLeft : SandboxUse::kHost;   // settings alone do not keep the sandbox's
}

bool holds_settings(const fs::path& data_dir) {
    std::error_code ec;
    return fs::is_regular_file(data_dir / "pc_settings.ini", ec) || fs::is_regular_file(data_dir / "launcher.ini", ec);
}

bool holds_data(const fs::path& data_dir) { return holds_settings(data_dir) || has_saves(data_dir / "saves"); }

SandboxData use_host_data_in_sandbox() {
    SandboxData r;
#if !defined(_WIN32) && !defined(__APPLE__)
    std::error_code ec;
    r.sandbox = env("FLATPAK_ID") != nullptr || fs::exists("/.flatpak-info", ec);
    const char* home = env("HOME");
    const char* xdg = env("XDG_DATA_HOME");
    if (r.sandbox && home) {
        const fs::path share = fs::path(home) / ".local" / "share", top = share / "b3port", host_dir = top / "native";
        const bool already = !xdg || fs::weakly_canonical(fs::path(xdg), ec) == fs::weakly_canonical(share, ec);
        const bool reachable = fs::is_directory(top, ec);
        bool writable = false;
        if (reachable) {   // a write probe: a read-only grant (:ro) is reachable too
            const fs::path probe = top / ".b3port-write-test";
            {
                std::ofstream f(probe, std::ios::binary | std::ios::trunc);
                writable = f && (f << "x") && (f.flush(), bool(f));
            }
            fs::remove(probe, ec);
        }
        const fs::path own_dir = xdg ? fs::path(xdg) / "b3port" / "native" : host_dir;
        const bool host_data = reachable && holds_data(host_dir);
        switch (sandbox_use(true, already, reachable, writable, !already && has_saves(own_dir / "saves"),
                            !already && holds_settings(own_dir), host_data)) {
        case SandboxUse::kHost: r.host = already || setenv("XDG_DATA_HOME", share.c_str(), 1) == 0; break;
        case SandboxUse::kHostOwnLeft:
            r.host = setenv("XDG_DATA_HOME", share.c_str(), 1) == 0;
            if (r.host) r.left = own_dir;
            break;
        case SandboxUse::kOwnBothHaveData: r.two = true; r.other = host_dir; break;
        case SandboxUse::kOwn: break;
        }
    }
#endif
    r.dir = user_data_dir();
    return r;
}

fs::path default_saves_root() {
    const fs::path d = user_data_dir();
    return d.empty() ? d : d / "saves";
}

fs::path path_from_utf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

namespace {
std::string utf8_text(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}
}  // namespace

fs::path chosen_saves_root(const fs::path& data_dir) {
    if (data_dir.empty()) return {};
    std::ifstream f(data_dir / "launcher.ini", std::ios::binary);
    std::string line, found;
    auto trim = [](std::string v) {
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) v.pop_back();
        std::size_t a = 0;
        while (a < v.size() && (v[a] == ' ' || v[a] == '\t')) ++a;
        return v.substr(a);
    };
    while (std::getline(f, line)) {   // (the launcher's own reader, core.cpp config_from_ini: the last one wins)
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
        if (trim(line.substr(0, eq)) == "saves_dir") found = trim(line.substr(eq + 1));
    }
    // portable mode (1.3.0): a folder inside the install, kept as "<install>/..." (b3port/user_dir.h)
    return found.empty() ? fs::path() : path_from_utf8(b3io::install_resolved(found, b3io::exe_dir()));
}

bool has_saves(const fs::path& root) {
    std::error_code ec;
    for (int n = 1; n <= kSlots; ++n)
        if (fs::is_regular_file(profile_file(root, n), ec) || fs::is_directory(legacy_profile_folder(root, n), ec)) return true;
    return false;
}

int copy_saves(const fs::path& from, const fs::path& to, std::string* error, int* kept) {
    int copied = 0, left = 0;
    struct Kept { int* out; int& n; ~Kept() { if (out) *out = n; } } report{kept, left};
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec || !fs::is_directory(to, ec)) {
        if (error) *error = "cannot make the folder " + utf8_text(to) + (ec ? ": " + ec.message() : std::string());
        return -1;
    }
    // one file, never over one already there
    auto one = [&](const fs::path& src, const fs::path& dst) -> bool {
        std::error_code e;
        if (fs::exists(dst, e)) {
            ++left;
            return true;
        }
        if (dst.has_parent_path()) fs::create_directories(dst.parent_path(), e);
        if (!fs::copy_file(src, dst, fs::copy_options::none, e) || e) {
            if (error) *error = "cannot copy " + utf8_text(src.filename()) + (e ? ": " + e.message() : std::string());
            return false;
        }
        ++copied;
        return true;
    };
    // a folder's files, below it too
    auto tree = [&](const fs::path& src, const fs::path& dst) -> bool {
        std::error_code e;
        for (fs::recursive_directory_iterator it(src, e), end; !e && it != end; it.increment(e)) {
            std::error_code t;
            if (!it->is_regular_file(t)) continue;
            if (!one(it->path(), dst / fs::relative(it->path(), src, t))) return false;
        }
        if (e) {
            if (error) *error = "cannot read " + utf8_text(src) + ": " + e.message();
            return false;
        }
        return true;
    };
    for (int n = 1; n <= kSlots; ++n) {
        const fs::path sav = profile_file(from, n);
        fs::path bak = sav;
        bak += ".bak";
        for (const fs::path& f : {sav, bak})
            if (fs::is_regular_file(f, ec) && !one(f, to / f.filename())) return -1;
        if (fs::is_directory(legacy_profile_folder(from, n), ec) &&
            !tree(legacy_profile_folder(from, n), legacy_profile_folder(to, n)))
            return -1;
    }
    if (fs::is_regular_file(from / "active-slot.txt", ec) && !one(from / "active-slot.txt", to / "active-slot.txt")) return -1;
    if (fs::is_directory(from / "backups", ec) && !tree(from / "backups", to / "backups")) return -1;
    return copied;
}

fs::path profile_file(const fs::path& root, int slot) { return root / ("profile-" + std::to_string(slot) + ".sav"); }
fs::path legacy_profile_folder(const fs::path& root, int slot) { return root / ("profile-" + std::to_string(slot)); }

std::optional<LoadedProfile> load_profile(const fs::path& root, int slot, std::string* error,
                                          std::optional<std::int64_t> fixed_file_time) {
    if (slot < 1 || slot > kSlots) {
        if (error) *error = "invalid slot " + std::to_string(slot);
        return std::nullopt;
    }
    std::error_code ec;
    LoadedProfile out;
    if (fs::exists(profile_file(root, slot), ec)) {
        auto d = read_sav_file(profile_file(root, slot), error);
        if (!d) return std::nullopt;
        out.dirs = std::move(*d);
        out.source = ProfileSource::kSav;
        return out;
    }
    if (fs::is_directory(legacy_profile_folder(root, slot), ec)) {
        auto d = load_profile_slot(root, slot, error, fixed_file_time);
        if (!d) return std::nullopt;
        out.dirs = std::move(*d);
        out.source = ProfileSource::kFolder;
    }
    return out;
}

bool store_profile(const fs::path& root, int slot, const Directories& dirs, std::string* error) {
    if (slot < 1 || slot > kSlots) {
        if (error) *error = "invalid slot " + std::to_string(slot);
        return false;
    }
    // fsrobust: the new save written and flushed to the disk beside the old one first; only then the old file is kept in
    // backups/ (mac5) and replaced in one step. A write that fails (disk full, read-only, a sync client's lock) leaves
    // profile-N.sav and every backup as they were, and a retry of the same failure makes no new backup (the newest
    // already holds this file): the good backups are never rotated out by retries.
    if (!saves_root_ready(root, false, error)) return false;   // a folder that has gone is not made again here
    const fs::path p = profile_file(root, slot);
    std::error_code ec;
    const Bytes bytes = write_sav(dirs);
    if (fs::is_regular_file(p, ec) && file_equals(p, bytes)) return true;   // the same save: nothing to write or keep
    const fs::path tmp = b3io::tmp_beside(p);
    if (!b3io::write_synced(tmp, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), error)) return false;
    if (fs::is_regular_file(p, ec) && !same_bytes(p, newest_backup(root, slot)))
        backup_profile(root, slot, static_cast<std::int64_t>(std::time(nullptr)));
    return b3io::commit_replace(tmp, p, error);
}

fs::path backup_profile(const fs::path& root, int slot, std::int64_t now_unix, int keep) {
    std::error_code ec;
    const fs::path cur = profile_file(root, slot);
    if (slot < 1 || slot > kSlots || !fs::is_regular_file(cur, ec)) return {};
    const fs::path dir = root / "backups";
    fs::create_directories(dir, ec);
    const std::string stamp = local_stamp(now_unix);
    const std::string prefix = "profile-" + std::to_string(slot) + "-";
    fs::path out = dir / (prefix + stamp + ".sav");
    for (int n = 2; fs::exists(out, ec) && n < 100; ++n) out = dir / (prefix + stamp + "-" + std::to_string(n) + ".sav");
    if (!fs::copy_file(cur, out, fs::copy_options::overwrite_existing, ec)) return {};
    // fsrobust: what stays: the newest `keep`, and the first (oldest) of each of the last kKeepBackupDays days
    if (keep < 0) return out;
    const std::vector<Backup> mine = slot_backups(root, slot);   // oldest first
    std::vector<bool> stays(mine.size(), false);
    for (std::size_t i = mine.size() > std::size_t(keep) ? mine.size() - std::size_t(keep) : 0; i < mine.size(); ++i) stays[i] = true;
    int days = 0;
    for (std::size_t i = mine.size(); i-- > 0 && days < kKeepBackupDays;) {   // newest day first
        const std::string day = mine[i].stamp.substr(0, 8);
        std::size_t first = i;
        while (first > 0 && mine[first - 1].stamp.compare(0, 8, day) == 0) --first;
        stays[first] = true;
        ++days;
        i = first;
    }
    for (std::size_t i = 0; i < mine.size(); ++i)
        if (!stays[i]) fs::remove(mine[i].path, ec);
    return out;
}

bool saves_root_ready(const fs::path& root, bool create, std::string* error) {
    std::error_code ec;
    if (fs::is_directory(root, ec)) return true;
    if (create && !root.empty()) {
        fs::create_directories(root, ec);
        if (fs::is_directory(root, ec)) return true;
        if (error) *error = "cannot make the save folder " + b3io::u8string(root) + (ec ? ": " + ec.message() : std::string());
        return false;
    }
    if (error) *error = "the save folder " + b3io::u8string(root) + " is not there (a drive or network folder not connected?)";
    return false;
}

bool write_active_slot(const fs::path& root, int slot, std::string* error) {
    if (!saves_root_ready(root, false, error)) return false;
    return b3io::replace_file(root / "active-slot.txt", std::to_string(slot) + "\n", error);   // fsrobust
}

std::optional<Directories> import_game_save(const fs::path& from, std::int64_t now_unix, std::string* error) {
    auto all = read_any(from, error);
    if (!all) return std::nullopt;
    auto game = game_save(*all, error);
    if (!game) return std::nullopt;
    const CardTimestamp now = card_timestamp_from_unix(now_unix);
    const CardTimestamp unset{};
    for (auto& d : *game) {
        if (d.created == unset) d.created = now;
        if (d.modified == unset) d.modified = now;
        for (auto& f : d.files) {
            if (f.created == unset) f.created = now;
            if (f.modified == unset) f.modified = now;
        }
    }
    // the card must hold it (build_card_image refuses names / sizes it cannot store)
    std::string why;
    if (build_card_image(*game, &why).empty()) {
        if (error) *error = "the save does not fit a card: " + why;
        return std::nullopt;
    }
    return game;
}

bool import_profile(const fs::path& root, int slot, const fs::path& from, std::int64_t now_unix, std::string* error) {
    auto dirs = import_game_save(from, now_unix, error);
    if (!dirs) return false;
    // saveflow: a save the game would call corrupt (and then ask to delete it with the console's browser) is not taken
    std::string damage;
    if (!game_save_intact(*dirs, &damage)) {
        if (error) *error = "the save is damaged and the game would not load it (" + damage + "): not imported";
        return false;
    }
    if (!saves_root_ready(root, false, error)) return false;
    std::error_code ec;
    const fs::path target = profile_file(root, slot);
    if (fs::is_regular_file(target, ec)) {
        // fsrobust: the old save kept twice, both checked before anything changes: profile-N.sav.bak (as the launcher
        // says) and in backups/ (with the others: the .bak is one generation, the next import replaces it)
        fs::path bak = target;
        bak += ".bak";
        if (!fs::copy_file(target, bak, fs::copy_options::overwrite_existing, ec) || ec) {
            if (error) *error = "cannot keep the old save as " + b3io::u8string(bak.filename()) + (ec ? ": " + ec.message() : std::string());
            return false;
        }
        if (!same_bytes(target, newest_backup(root, slot)) && backup_profile(root, slot, now_unix).empty()) {
            if (error) *error = "cannot keep the old save in " + b3io::u8string(root / "backups");
            return false;
        }
    }
    return store_profile(root, slot, *dirs, error);
}

std::string profile_problem(const fs::path& root, int slot) {
    std::string why;
    const auto p = load_profile(root, slot, &why);
    if (!p) return why.empty() ? std::string("it does not read") : why;
    const auto game = game_save(p->dirs);
    if (game && !game_save_intact(*game, &why)) return why;
    return {};
}

fs::path set_aside_profile(const fs::path& root, int slot, std::int64_t now_unix, std::string* error) {
    auto fail = [error](std::string w) {
        if (error) *error = std::move(w);
        return fs::path();
    };
    if (slot < 1 || slot > kSlots) return fail("invalid slot " + std::to_string(slot));
    // a game playing from this folder holds its profile in memory and would save it back: it ends first
    if (const std::string h = folder_lock_holder(root); !h.empty()) return fail(h + " plays from this save folder: quit it first");
    std::error_code ec;
    const std::string base = "profile-" + std::to_string(slot) + "-set-aside-" + local_stamp(now_unix);
    auto free_name = [&](const std::string& ext) {
        fs::path out = root / (base + ext);
        for (int n = 2; fs::exists(out, ec) && n < 100; ++n) out = root / (base + "-" + std::to_string(n) + ext);
        return out;
    };
    const fs::path sav = profile_file(root, slot), folder = legacy_profile_folder(root, slot);
    const bool has_sav = fs::is_regular_file(sav, ec), has_folder = fs::is_directory(folder, ec);
    if (!has_sav && !has_folder) return fail("file " + std::to_string(slot) + " is empty: nothing to set aside");
    // all or nothing: the older folder first (the .sav would be read without it, the folder without the .sav), and a
    // failed .sav puts the folder back
    fs::path folder_to, sav_to;
    if (has_folder) {
        folder_to = free_name("");
        fs::rename(folder, folder_to, ec);
        if (ec) return fail("cannot rename the folder " + b3io::u8string(folder.filename()) + ": " + ec.message());
    }
    if (has_sav) {
        sav_to = free_name(".sav");
        fs::rename(sav, sav_to, ec);
        if (ec) {
            std::string why = "cannot rename " + b3io::u8string(sav.filename()) + ": " + ec.message();
            if (!folder_to.empty()) {
                std::error_code back;
                fs::rename(folder_to, folder, back);
                if (back) why += "; the folder " + b3io::u8string(folder.filename()) + " could not be put back: it is " + b3io::u8string(folder_to.filename());
            }
            return fail(why);
        }
    }
    return !sav_to.empty() ? sav_to : folder_to;
}

std::vector<fs::path> unsaved_saves(const fs::path& root, int slot) {
    std::vector<fs::path> out;
    const std::string prefix = "profile-" + std::to_string(slot) + "-unsaved-";
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = b3io::u8string(it->path().filename());
        std::error_code rec;
        if (name.rfind(prefix, 0) == 0 && name.size() > prefix.size() + 4 && name.compare(name.size() - 4, 4, ".sav") == 0 &&
            it->is_regular_file(rec))
            out.push_back(it->path());
    }
    // the stamp in the name (local YYYYMMDD-HHMMSS) sorts as the time
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) { return b3io::u8string(a.filename()) > b3io::u8string(b.filename()); });
    return out;
}

bool use_unsaved(const fs::path& root, int slot, const fs::path& from, std::int64_t now_unix, std::string* error) {
    if (const std::string h = folder_lock_holder(root); !h.empty()) {
        if (error) *error = h + " plays from this save folder: quit it first";
        return false;
    }
    return import_profile(root, slot, from, now_unix, error);
}

fs::path keep_unreadable(const fs::path& root, int slot, std::int64_t now_unix) {
    std::error_code ec;
    const fs::path sav = profile_file(root, slot);
    if (slot < 1 || slot > kSlots || !fs::is_regular_file(sav, ec)) return {};
    const std::string prefix = "profile-" + std::to_string(slot) + "-unreadable-";
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = b3io::u8string(it->path().filename());
        if (name.rfind(prefix, 0) == 0 && same_bytes(it->path(), sav)) return it->path();
    }
    fs::path out = root / (prefix + local_stamp(now_unix) + ".sav");
    for (int n = 2; fs::exists(out, ec) && n < 100; ++n) out = root / (prefix + local_stamp(now_unix) + "-" + std::to_string(n) + ".sav");
    if (!fs::copy_file(sav, out, fs::copy_options::none, ec) || ec) return {};
    return out;
}

namespace {
fs::path lock_file(const fs::path& root) { return root / "game.lock"; }
long this_pid() {
#ifdef _WIN32
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(getpid());
#endif
}
std::string this_host() {
#ifdef _WIN32
    if (const auto c = b3io::getenv("COMPUTERNAME")) return *c;
    return "this computer";
#else
    char h[256] = {};
    if (gethostname(h, sizeof h - 1) == 0 && h[0]) return h;
    return "this computer";
#endif
}
// This boot of this computer: Linux's boot id; elsewhere "t<the boot's Unix second>" (the clock minus the uptime: equal
// within kBootSlack seconds for the same boot)
constexpr std::int64_t kBootSlack = 120;
std::string this_boot() {
#if defined(_WIN32)
    const std::int64_t up = static_cast<std::int64_t>(GetTickCount64() / 1000);
    return "t" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() - up);
#elif defined(__APPLE__)
    struct timeval tv{};
    std::size_t n = sizeof tv;
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    if (sysctl(mib, 2, &tv, &n, nullptr, 0) == 0 && tv.tv_sec > 0) return "t" + std::to_string(static_cast<long long>(tv.tv_sec));
    return {};
#else
    std::ifstream f("/proc/sys/kernel/random/boot_id");
    std::string id;
    std::getline(f, id);
    return id;
#endif
}
bool same_boot(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    if (a[0] == 't' && b[0] == 't') {
        const long long x = std::strtoll(a.c_str() + 1, nullptr, 10), y = std::strtoll(b.c_str() + 1, nullptr, 10);
        return x > 0 && y > 0 && (x > y ? x - y : y - x) <= kBootSlack;
    }
    return a == b;
}
// When process `pid` started (an opaque number: Linux /proc/<pid>/stat's start time, Windows' creation time, the Mac's
// proc_pidinfo start time); 0 when unknown. A pid used again by another program after the game ended has another one.
std::uint64_t process_start(long pid) {
#if defined(_WIN32)
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!p) return 0;
    FILETIME c{}, e{}, k{}, u{};
    const bool ok = GetProcessTimes(p, &c, &e, &k, &u);
    CloseHandle(p);
    return ok ? (std::uint64_t(c.dwHighDateTime) << 32 | c.dwLowDateTime) : 0;
#elif defined(__APPLE__)
    // saveflow: the BSD info's start time (seconds, microseconds). Not built on the Linux machine: checked on the Mac.
    struct proc_bsdinfo info {};
    if (proc_pidinfo(static_cast<int>(pid), PROC_PIDTBSDINFO, 0, &info, static_cast<int>(sizeof info)) != static_cast<int>(sizeof info))
        return 0;
    return std::uint64_t(info.pbi_start_tvsec) * 1000000ull + std::uint64_t(info.pbi_start_tvusec);
#else
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string s;
    std::getline(f, s);
    const std::size_t close = s.rfind(')');   // the name may hold spaces and brackets: the fields follow the last ')'
    if (close == std::string::npos) return 0;
    std::size_t at = close + 2;
    for (int field = 3; field < 22 && at < s.size(); ++field) {   // field 22: starttime
        at = s.find(' ', at);
        if (at == std::string::npos) return 0;
        ++at;
    }
    return std::strtoull(s.c_str() + at, nullptr, 10);
#endif
}
bool process_alive(long pid) {
#ifdef _WIN32
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!p) return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(p, &code) && code == STILL_ACTIVE;
    CloseHandle(p);
    return alive;
#else
    return pid > 0 && (kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
#endif
}
std::optional<FolderLock> read_lock_file(const fs::path& root) {
    std::ifstream f(lock_file(root), std::ios::binary);
    if (!f) return std::nullopt;
    FolderLock l;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "pid") l.pid = std::strtol(v.c_str(), nullptr, 10);
        else if (k == "host") l.host = v;
        else if (k == "boot") l.boot = v;
        else if (k == "start") l.start = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "slot") l.slot = static_cast<int>(std::strtol(v.c_str(), nullptr, 10));
        else if (k == "since") l.since = std::strtoll(v.c_str(), nullptr, 10);
    }
    return l;
}
bool mine(const FolderLock& l) { return l.pid == this_pid() && l.host == this_host() && same_boot(l.boot, this_boot()); }
}  // namespace

bool folder_lock_live(const FolderLock& l) {
    if (l.host != this_host() || !same_boot(l.boot, this_boot())) return false;   // another computer, or before this boot
    if (l.pid == this_pid() || !process_alive(l.pid)) return false;
    const std::uint64_t now = l.start ? process_start(l.pid) : 0;
    return !l.start || !now || now == l.start;   // (the same pid, another program: not the game)
}

std::optional<FolderLock> read_folder_lock(const fs::path& root) { return read_lock_file(root); }

std::string folder_lock_holder(const fs::path& root) {
    const auto l = read_lock_file(root);
    if (!l || !folder_lock_live(*l)) return {};
    if (l->slot == kLauncherMoveSlot) return "the launcher, moving your data folder (since " + local_stamp(l->since) + ")";   // portable mode
    return "another game on this computer (file " + std::to_string(l->slot) + ", since " + local_stamp(l->since) + ")";
}

bool acquire_folder_lock(const fs::path& root, int slot, std::int64_t now_unix, std::string* holder) {
    if (const std::string h = folder_lock_holder(root); !h.empty()) {
        if (holder) *holder = h;
        return false;
    }
    const std::string text = "pid=" + std::to_string(this_pid()) + "\nhost=" + this_host() + "\nboot=" + this_boot() +
                             "\nstart=" + std::to_string(process_start(this_pid())) + "\nslot=" + std::to_string(slot) +
                             "\nsince=" + std::to_string(now_unix) + "\n";
    return b3io::replace_file(lock_file(root), text, nullptr, false);
}

void release_folder_lock(const fs::path& root) {
    const auto l = read_lock_file(root);
    if (!l || !mine(*l)) return;   // another game's (it took over a stale one)
    std::error_code ec;
    fs::remove(lock_file(root), ec);
}

// ---- ProfileCard ----
struct ProfileCard::Shared {
    mutable std::mutex m;
    Request request = Request::kNone;
    fs::path path;
    Status status;
};

namespace {
std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}  // namespace

ProfileCard::ProfileCard(fs::path root) : root_(std::move(root)), card_(root_), shared_(new Shared) {
    const char* p = env("B3NATIVE_CARD_PERSIST");   // native_play sets 1; proof tools (native_boot ...) leave it off
    persist_ = p && p[0] == '1' && p[1] == 0;
    const char* t = env("B3NATIVE_CARD_THROWAWAY");   // saveflow: native_play --replay's card
    throwaway_ = t && t[0] == '1' && t[1] == 0;
}

ProfileCard::~ProfileCard() {
    std::string err;
    if (!flush(&err)) std::fprintf(stderr, "[save] %s\n", err.c_str());
    if (locked_) release_folder_lock(root_);
    delete shared_;
}

namespace {
std::string upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}
}  // namespace

ProfileCard::DiskMark ProfileCard::mark() const {
    const fs::path p = profile_file(root_, slot_);
    for (int attempt = 0; attempt < kMarkTries; ++attempt) {
        if (attempt) std::this_thread::sleep_for(std::chrono::milliseconds(75));
        DiskMark m;
        std::error_code ec;
        const bool regular = fs::is_regular_file(p, ec);
        if (ec && ec != std::errc::no_such_file_or_directory) continue;   // (the file's state itself unknown)
        if (!regular) {
            if (fs::exists(p, ec) || ec) continue;   // there, but not a plain file we can stat: unknown
            return m;                                // gone: known, not there
        }
        m.exists = true;
        m.size = fs::file_size(p, ec);
        if (ec) continue;
        std::ifstream f(p, std::ios::binary);
        Bytes b;
        if (f) b.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        if (fail_reads_for_test > 0) {
            --fail_reads_for_test;
            b.clear();
        }
        if (!f || b.size() != m.size) continue;   // a failed or short read: never taken for a change
        // FNV-1a 64 (not crc32: a .sav ends with the crc32 of what precedes it, so every .sav's own crc32 is the same value)
        std::uint64_t h = 0xCBF29CE484222325ull;
        for (const std::uint8_t c : b) h = (h ^ c) * 0x100000001B3ull;
        m.hash = h;
        return m;
    }
    DiskMark unknown;
    unknown.known = false;
    return unknown;
}

bool ProfileCard::unchanged(const DiskMark& now) const {
    if (now.exists != disk_.exists) return false;
    return !now.exists || (now.size == disk_.size && now.hash == disk_.hash);   // (a touch alone: the same bytes, unchanged)
}

void ProfileCard::redirect(const std::string& why) {
    side_ = root_ / ("profile-" + std::to_string(slot_) + "-unsaved-" + local_stamp(unix_now()) + ".sav");
    blocked_ = std::string(kNotSaved) + " - " + why + " - SAVED TO " + upper(b3io::u8string(side_.filename()));
}

bool ProfileCard::attach(int slot, std::string* error) {
    std::string why;
    auto p = load_profile(root_, slot, &why, card_.fixed_file_time());
    if (p) {   // saveflow: a save the game would call corrupt is as unreadable (its "delete it with the console's browser" is a dead end)
        const auto game = game_save(p->dirs);
        if (game && !game_save_intact(*game, &why)) p.reset();
    }
    // saveflow: the save folder's lock (two games on one folder warn); nothing is refused for it
    if (persist_ && !throwaway_ && saves_root_ready(root_, false)) {
        std::string holder;
        locked_ = acquire_folder_lock(root_, slot, unix_now(), &holder);
        if (!locked_ && !holder.empty()) {
            std::printf("[save] the save folder's lock is held by %s\n", holder.c_str());
            const auto l = read_lock_file(root_);
            if (l && l->slot == kLauncherMoveSlot)   // portable mode (1.3.0): the launcher moves the data folder now
                set_status(false, std::string(kSaveWarning) + " - THE LAUNCHER IS MOVING YOUR DATA - QUIT AND START AGAIN WHEN IT IS DONE");
            else
                set_status(false, std::string(kSaveWarning) + " - ANOTHER GAME ON THIS COMPUTER USES THIS SAVE FOLDER - PLAY ONE AT A TIME");
        }
    }
    if (p && card_.attach_directories(slot, p->dirs, &why)) {
        slot_ = slot;
        source_ = p->source;
        seen_writes_ = flushed_writes_ = card_.writes();
        disk_ = mark();   // saveflow: what a save compares the file on the disk with
        return true;
    }
    // an unreadable file: the game gets an empty card (no "no memory card" pages) and nothing is written over the file
    if (error) *error = why;
    slot_ = slot;
    source_ = ProfileSource::kNone;
    const bool kept_wanted = persist_ && !throwaway_;
    persist_ = false;
    card_.attach_directories(slot, {});
    seen_writes_ = flushed_writes_ = card_.writes();
    // saveflow: told in the window (a notice, and again at each save the game makes); the file kept aside as well
    const fs::path kept = kept_wanted ? keep_unreadable(root_, slot, unix_now()) : fs::path();
    const std::string name = upper(b3io::u8string(profile_file(root_, slot).filename()));
    blocked_ = std::string(kNotSaved) + " - CAN'T READ " + name + " - PROGRESS WON'T BE SAVED THIS SESSION - THE LAUNCHER'S SAVES PAGE CAN SET IT ASIDE";
    std::printf("[save] profile-%d.sav can't be read (%s): nothing is saved this session; the file is kept%s\n", slot, why.c_str(),
                kept.empty() ? "" : (" (a copy: " + b3io::u8string(kept.filename()) + ")").c_str());
    set_status(false, blocked_);
    return false;
}

bool ProfileCard::flush(std::string* error) {
    if (!card_.attached() || card_.writes() == flushed_writes_) return true;
    if (!persist_) {
        flushed_writes_ = card_.writes();
        if (!blocked_.empty()) set_status(false, blocked_);   // saveflow: the player told again at each save not written
        return true;
    }
    const auto& img = card_.image();
    std::string why;
    auto dirs = read_card_image(img.data(), img.size(), &why);
    if (!dirs) {
        if (error) *error = "the card's filesystem does not read back (" + why + "): profile-" + std::to_string(slot_) + ".sav not written";
        return false;
    }
    // saveflow: profile-N.sav changed on the disk since the game loaded (or last wrote) it: never written over
    // Only a whole read whose content differs is a change: a file that cannot be read now (held by a scanner or a sync
    // client) fails this save into tick's retry (SAVE FAILED, retried every 10 s); a baseline that could not be read at
    // attach compares nothing (the save is written as before saveflow)
    if (side_.empty()) {
        const DiskMark now = mark();
        if (!now.known) {
            if (error) *error = "profile-" + std::to_string(slot_) + ".sav could not be read to check it (another program may hold it)";
            return false;
        }
        if (disk_.known && !unchanged(now)) {
            redirect("PROFILE-" + std::to_string(slot_) + ".SAV CHANGED OUTSIDE THE GAME");
            std::printf("[save] profile-%d.sav changed on the disk since the game loaded it: not overwritten\n", slot_);
        }
    }
    if (!side_.empty()) {
        const Bytes bytes = write_sav(*dirs);
        if (!b3io::replace_file(side_, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), error)) return false;
        flushed_writes_ = card_.writes();
        std::printf("[save] profile %d saved to %s (not profile-%d.sav)\n", slot_, b3io::u8string(side_).c_str(), slot_);
        set_status(false, blocked_);
        return true;
    }
    if (!store_profile(root_, slot_, *dirs, error)) return false;
    disk_ = mark();
    flushed_writes_ = card_.writes();
    std::printf("[save] profile %d saved to %s\n", slot_, profile_file(root_, slot_).string().c_str());
    std::fflush(stdout);
    if (!failing_ && !throwaway_) set_status(true, kSaved, false);   // saveflow: the window's "SAVED" bubble (after failures: tick's own)
    return true;
}

void ProfileCard::tick(std::uint64_t vblank) {
    now_ = vblank;
    if (card_.writes() != seen_writes_) {
        seen_writes_ = card_.writes();
        last_change_ = vblank;
    }
    if (seen_writes_ != flushed_writes_ && vblank >= last_change_ + kSettleVBlanks && vblank >= retry_at_) {
        std::string err;
        if (!flush(&err)) {
            std::fprintf(stderr, "[save] %s (retrying; profile-%d.sav and its backups are kept as they were)\n", err.c_str(), slot_);
            retry_at_ = vblank + kRetryVBlanks;
            // the notice: at the first failure, then every kRemindRetries-th (30 s) while they go on
            if (failures_ % kRemindRetries == 0) set_status(false, std::string(kSaveFailed) + " - YOUR LAST SAVE IS KEPT - RETRYING");
            ++failures_;
            failing_ = true;
        } else if (failing_) {
            failing_ = false;
            failures_ = 0;
            set_status(true, "SAVED AGAIN TO PROFILE " + std::to_string(slot_));
        }
    }
    Request r;
    fs::path path;
    {
        std::lock_guard<std::mutex> lock(shared_->m);
        r = shared_->request;
        path = shared_->path;
        shared_->request = Request::kNone;
    }
    if (r != Request::kNone) run(r, path);
}

bool ProfileCard::writing() const {
    return persist_ && card_.attached() && card_.writes() != flushed_writes_ &&
           (card_.writes() != seen_writes_ || now_ < last_change_ + kSettleVBlanks);
}

std::string ProfileCard::flush_at_exit(const fs::path& rescue_dir, bool throwaway) {
    struct Release {   // saveflow: the folder's lock given back whatever the end
        ProfileCard* c;
        ~Release() {
            if (c->locked_) release_folder_lock(c->root_);
            c->locked_ = false;
        }
    } release{this};
    if (!persist_ || !card_.attached() || card_.writes() == flushed_writes_) return {};
    if (throwaway) {   // a replay's card: its folder goes right after; nothing to keep, nothing to tell
        std::printf("[save] at exit: the replay's card%s is not written (a throwaway copy)\n", writing() ? " (still being written)" : "");
        std::fflush(stdout);
        flushed_writes_ = seen_writes_ = card_.writes();
        return {};
    }
    const std::string name = "profile-" + std::to_string(slot_) + ".sav";
    const fs::path p = profile_file(root_, slot_);
    const auto& img = card_.image();
    std::string why;
    const auto dirs = read_card_image(img.data(), img.size(), &why);
    const Bytes bytes = dirs ? write_sav(*dirs) : Bytes();
    const std::string_view view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::string err;
    if (writing()) {   // the game had not finished its save: the last whole one stays
        fs::path side = p;
        side += ".unfinished";
        const bool kept = dirs && saves_root_ready(root_, false, &err) && b3io::replace_file(side, view, &err);
        std::printf("[save] NOT SAVED at exit: the game was still writing the card; %s keeps the last complete save%s\n", name.c_str(),
                    kept ? ("; the unfinished one is " + b3io::u8string(side)).c_str() : "");
        std::fflush(stdout);
        set_status(false, std::string(kSaveFailed) + " - NOT SAVED AT EXIT: THE GAME WAS STILL SAVING - THE LAST COMPLETE SAVE IS KEPT");
        return "The game was still saving when it closed. " + name + " keeps your last complete save" +
               (kept ? " (the unfinished one is beside it as " + b3io::u8string(side.filename()) + ")." : ".");
    }
    if (flush(&err)) return {};
    std::printf("[save] NOT SAVED at exit: %s\n", err.c_str());   // (stdout: the run log)
    std::fflush(stdout);
    set_status(false, std::string(kSaveFailed) + " - NOT SAVED AT EXIT");
    fs::path copy;
    if (dirs && !rescue_dir.empty() && rescue_dir != root_) {   // a copy where it can be written (the save folder's failed)
        std::error_code ec;
        fs::create_directories(rescue_dir, ec);
        const fs::path r = rescue_dir / ("profile-" + std::to_string(slot_) + "-" + local_stamp(unix_now()) + ".sav");
        std::string rerr;
        if (b3io::replace_file(r, view, &rerr)) copy = r;
        else std::fprintf(stderr, "[save] at exit: no copy either: %s\n", rerr.c_str());
    }
    if (!copy.empty()) std::printf("[save] at exit: the unsaved save kept in %s\n", b3io::u8string(copy).c_str());
    return "Your last save could not be written to " + b3io::u8string(p) + " (" + err + ")." +
           (copy.empty() ? std::string(" It is lost.")
                         : " A copy is in " + b3io::u8string(copy) + ": put it in the save folder as " + name + " to keep it.");
}

void ProfileCard::post(Request r, fs::path path) {
    std::lock_guard<std::mutex> lock(shared_->m);
    shared_->request = r;
    shared_->path = std::move(path);
}

ProfileCard::Status ProfileCard::status() const {
    std::lock_guard<std::mutex> lock(shared_->m);
    return shared_->status;
}

void ProfileCard::set_status(bool ok, std::string text, bool log) {
    if (log) {
        std::printf("[save] %s\n", text.c_str());
        std::fflush(stdout);
    }
    std::lock_guard<std::mutex> lock(shared_->m);
    ++shared_->status.serial;
    shared_->status.ok = ok;
    shared_->status.text = std::move(text);
}

void ProfileCard::run(Request r, const fs::path& path) {
    std::string err;
    if (r == Request::kExport) {
        // the card as it is now (a save not yet settled included)
        const auto& img = card_.image();
        auto dirs = card_.attached() ? read_card_image(img.data(), img.size(), &err) : std::nullopt;
        if (!dirs) return set_status(false, "EXPORT FAILED: " + (err.empty() ? std::string("no card") : err));
        auto game = game_save(*dirs, &err);
        if (!game) return set_status(false, "EXPORT FAILED: NO PROFILE SAVED YET");
        if (!write_sav_file(path, *game, &err)) return set_status(false, "EXPORT FAILED: " + err);
        return set_status(true, "EXPORTED TO " + path.string());
    }
    if (r == Request::kImport) {
        if (!import_profile(root_, slot_, path, unix_now(), &err)) return set_status(false, "IMPORT FAILED: " + err);
        // the game holds the old profile until it loads again: its later saves would overwrite the import. saveflow:
        // they go beside it (profile-N-unsaved-<stamp>.sav), and each says so
        redirect("A SAVE WAS IMPORTED: RESTART THE GAME TO PLAY IT");
        flushed_writes_ = card_.writes();
        return set_status(true, "IMPORTED. QUIT AND START THE GAME AGAIN TO PLAY IT.");
    }
}

}  // namespace b3native::save
