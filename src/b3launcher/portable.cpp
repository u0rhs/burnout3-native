// portable.cpp -- portable mode's moves (portable.h)
#include "portable.h"

#include "b3port/fileio.h"
#include "b3port/user_dir.h"
#include "b3mod/archive.h"   // remove_tree: never through a link or a junction

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <system_error>
#include <vector>

namespace b3launcher::portable {

namespace {

std::string utf8(const fs::path& p) { return b3io::u8string(p); }

bool fail(Result& r, const std::string& why) {
    r.ok = false;
    r.why = why;
    return false;
}

// a name free beside `base`: base, base-2, base-3 ...
fs::path free_name(const fs::path& base) {
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(base, ec))) return base;
    for (int n = 2; n < 1000; ++n) {
        fs::path p = base;
        p += "-" + std::to_string(n);
        if (!fs::exists(fs::symlink_status(p, ec))) return p;
    }
    return {};
}

// rename with a few tries (a virus scanner or a sync client holding a file for a moment, as fileio.h replace_file)
bool rename_dir(const fs::path& a, const fs::path& b, std::string* why) {
    std::error_code ec;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (fs::exists(fs::symlink_status(b, ec))) {
            if (why) *why = utf8(b) + " is there already";
            return false;
        }
        fs::rename(a, b, ec);
        if (!ec) return true;
#ifdef _WIN32
        ::Sleep(200);
#endif
    }
    if (why) *why = "cannot rename " + utf8(a) + " to " + utf8(b.filename()) + ": " + ec.message();
    return false;
}

void remove_all_quiet(const fs::path& p) {
    if (!p.empty()) b3mod::remove_tree(p);
}

constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ull, kFnvPrime = 0x100000001B3ull;
std::uint64_t fnv(std::uint64_t h, const char* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) h = (h ^ static_cast<unsigned char>(p[i])) * kFnvPrime;
    return h;
}
constexpr std::size_t kChunk = 1u << 20;

// a file's hash, read in chunks; nullopt when it cannot be read
std::optional<std::uint64_t> file_hash(const fs::path& p, Progress* progress, std::uintmax_t* size) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    std::vector<char> buf(kChunk);
    std::uint64_t h = kFnvOffset;
    std::uintmax_t n = 0;
    while (f) {
        f.read(buf.data(), std::streamsize(buf.size()));
        const std::size_t got = std::size_t(f.gcount());
        h = fnv(h, buf.data(), got);
        n += got;
        if (progress) progress->done += got;
    }
    if (f.bad()) return std::nullopt;
    if (size) *size = n;
    return h;
}

// one file copied, its source's hash on the way
bool copy_file_hashed(const fs::path& from, const fs::path& to, std::uint64_t& hash, std::uintmax_t& size, Progress* progress,
                      std::string* why) {
    std::ifstream in(from, std::ios::binary);
    if (!in) return (why && (*why = "cannot read " + utf8(from), true)), false;
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) return (why && (*why = "cannot write " + utf8(to), true)), false;
    std::vector<char> buf(kChunk);
    hash = kFnvOffset;
    size = 0;
    while (in) {
        in.read(buf.data(), std::streamsize(buf.size()));
        const std::size_t got = std::size_t(in.gcount());
        if (got == 0) break;
        hash = fnv(hash, buf.data(), got);
        size += got;
        if (!out.write(buf.data(), std::streamsize(got))) return (why && (*why = "cannot write " + utf8(to) + " (the disk full?)", true)), false;
        if (progress) progress->done += got;
    }
    if (in.bad()) return (why && (*why = "cannot read " + utf8(from), true)), false;
    out.flush();
    if (!out) return (why && (*why = "cannot write " + utf8(to) + " (the disk full?)", true)), false;
    out.close();
    if (!out) return (why && (*why = "cannot write " + utf8(to), true)), false;
    std::error_code ec;
    const auto t = fs::last_write_time(from, ec);   // (the saves' "last saved" and the logs' order)
    if (!ec) fs::last_write_time(to, t, ec);
    return true;
}

bool non_empty_dir(const fs::path& d) {
    std::error_code ec;
    if (!fs::is_directory(d, ec)) return false;
    fs::directory_iterator it(d, ec);
    return !ec && it != fs::directory_iterator();
}

// a dated name: <name>.<what>-<stamp> beside `p`
fs::path dated(const fs::path& p, const char* what, const std::string& stamp) {
    return free_name(p.parent_path() / (utf8(p.filename()) + "." + what + "-" + stamp));
}

struct Copied {
    fs::path rel;
    std::uint64_t hash = 0;
    std::uintmax_t size = 0;
    fs::file_time_type time{};
};

// The source's regular files now: relative path -> (size, modification time)
std::map<fs::path, std::pair<std::uintmax_t, fs::file_time_type>> listing(const fs::path& dir, std::string* why) {
    std::map<fs::path, std::pair<std::uintmax_t, fs::file_time_type>> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code e;
        if (!fs::is_regular_file(it->symlink_status(e))) continue;
        const std::uintmax_t n = it->file_size(e);
        const fs::file_time_type t = it->last_write_time(e);
        out[it->path().lexically_relative(dir)] = {n, t};
    }
    if (ec && why) *why = ec.message();
    return out;
}

}  // namespace

Status status(const fs::path& exe_dir, bool apple, bool appimage, bool flatpak, int override_dir, bool writable, bool running) {
    Status s;
    if (!b3io::portable_supported(apple, appimage, flatpak)) return s;   // hidden
    s.shown = true;
    std::error_code ec;
    s.on = !exe_dir.empty() && fs::is_directory(b3io::portable_root(exe_dir), ec);
    if (exe_dir.empty()) s.why = "The launcher's own folder is not known.";
    else if (override_dir == 2) s.why = "B3PORT_USER_DIR is set to a data folder of your choice, so this does not apply.";
    else if (override_dir) s.why = "A data folder was given when the launcher was started (--pref), so this does not apply.";
    else if (!writable) s.why = "The install folder cannot be written by you: move the game to a folder of your own to use portable mode.";
    else if (running) s.why = "The game is running: close it first.";
    s.usable = s.why.empty();
    return s;
}

void measure(const fs::path& dir, std::size_t& files, std::uintmax_t& bytes) {
    files = 0;
    bytes = 0;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e;
        const fs::file_status st = it->symlink_status(e);
        if (fs::is_regular_file(st)) {
            ++files;
            const std::uintmax_t n = it->file_size(e);
            if (!e) bytes += n;
        }
    }
}

std::optional<Plan> plan(bool to_portable, const fs::path& exe_dir, const fs::path& platform, std::string* why, std::size_t path_limit) {
    auto no = [&](const std::string& w) -> std::optional<Plan> {
        if (why) *why = w;
        return std::nullopt;
    };
    if (exe_dir.empty()) return no("the launcher's own folder is not known");
    if (platform.empty()) return no("this computer gives no user data folder (no APPDATA / HOME)");
    std::error_code ec;
    const bool on_now = fs::is_directory(b3io::portable_root(exe_dir), ec);
    if (to_portable == on_now) return no(on_now ? "portable mode is on already" : "portable mode is off already");
    Plan p;
    p.to_portable = to_portable;
    if (to_portable) {
        if (fs::exists(fs::symlink_status(b3io::portable_root(exe_dir), ec))) return no(utf8(b3io::portable_root(exe_dir)) + " is a file, not a folder");
        p.from = platform;
        p.to = b3io::portable_dir(exe_dir);
        p.from_top = platform;
        p.to_top = b3io::portable_root(exe_dir);
        p.partial_top = exe_dir / (std::string(b3io::kPortableFolder) + ".partial");
        p.partial = p.partial_top / p.to.filename();
    } else {
        p.from = b3io::portable_dir(exe_dir);
        p.to = platform;
        p.from_top = b3io::portable_root(exe_dir);
        p.to_top = platform;
        p.partial_top = platform.parent_path() / (utf8(platform.filename()) + ".partial");
        p.partial = p.partial_top;
        p.target_has_data = non_empty_dir(platform);
    }
    measure(p.from, p.files, p.bytes);
    // Windows' path limit: a file whose new path (or its .partial one) would be longer is named now, before any copy
    if (path_limit > 0 && fs::is_directory(p.from, ec)) {
        fs::path longest;
        for (fs::recursive_directory_iterator it(p.from, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path rel = it->path().lexically_relative(p.from);
            for (const fs::path& d : {p.to / rel, p.partial / rel})
                if (d.native().size() > longest.native().size()) longest = d;
        }
        if (longest.native().size() > path_limit)
            return no("a path would be too long for Windows (" + std::to_string(longest.native().size()) + " characters, the limit is " +
                      std::to_string(path_limit) + "): " + utf8(longest) + ". Move the game to a folder with a shorter path, such as C:\\Games\\Burnout 3");
    }
    const fs::path where = p.partial_top.parent_path();
    fs::path probe = where;
    while (!probe.empty() && !fs::exists(probe, ec) && probe.has_parent_path() && probe.parent_path() != probe) probe = probe.parent_path();
    const fs::space_info sp = fs::space(probe, ec);
    p.free = ec ? 0 : sp.available;
    p.enough_space = ec || p.free >= p.bytes + kMargin + p.bytes / 20;
    return p;
}

Result move(const Plan& p, Keep keep, const std::string& stamp, Progress* progress, const FaultHook& fault) {
    Result r;
    auto faulty = [&](Fault f, const fs::path& at) { return fault && fault(f, at); };
    std::error_code ec;
    if (progress) {
        progress->total = std::uint64_t(p.bytes) * 2;
        progress->done = 0;
        progress->step = 0;
    }
    // off, keeping this computer's own data: nothing copied, the portable data set aside in the install folder
    if (!p.to_portable && keep == Keep::kExisting) {
        const fs::path aside = dated(p.from_top, "moved", stamp);
        std::string w;
        if (aside.empty() || faulty(Fault::kRenameAway, p.from_top) || !rename_dir(p.from_top, aside, &w)) {
            fail(r, w.empty() ? "cannot rename " + utf8(p.from_top) : w);
            return r;
        }
        r.ok = true;
        r.old_kept = aside;
        return r;
    }
    if (!p.enough_space) {   // (nothing is copied when this computer's own data is kept: no space needed)
        fail(r, "not enough free space where the data goes (" + std::to_string((p.bytes + kMargin) >> 20) + " MB needed, " +
                    std::to_string(p.free >> 20) + " MB free)");
        return r;
    }
    // 1. the copy (a .partial folder left by an earlier try goes first)
    remove_all_quiet(p.partial_top);
    if (fs::exists(fs::symlink_status(p.partial_top, ec))) {
        fail(r, "cannot remove " + utf8(p.partial_top) + " (left by an earlier try)");
        return r;
    }
    auto abort = [&](const std::string& why) {
        remove_all_quiet(p.partial_top);
        fail(r, why);
        return r;
    };
    fs::create_directories(p.partial, ec);
    if (ec || !fs::is_directory(p.partial, ec)) return abort("cannot make " + utf8(p.partial) + (ec ? ": " + ec.message() : std::string()));
    std::vector<Copied> copied;
    if (fs::is_directory(p.from, ec)) {
        for (fs::recursive_directory_iterator it(p.from, ec), end; it != end; it.increment(ec)) {
            if (ec) return abort("cannot list " + utf8(p.from) + ": " + ec.message());
            if (progress && progress->cancel) return abort("cancelled");
            const fs::path rel = it->path().lexically_relative(p.from);
            const fs::path dst = p.partial / rel;
            std::error_code e;
            const fs::file_status st = it->symlink_status(e);
            if (fs::is_directory(st)) {
                fs::create_directories(dst, e);
                if (e) return abort("cannot make " + utf8(dst) + ": " + e.message());
            } else if (fs::is_regular_file(st)) {
                Copied c{rel, 0, 0, {}};
                std::error_code te;
                c.time = fs::last_write_time(it->path(), te);   // (before the copy: a write during it shows below)
                std::string w;
                if (faulty(Fault::kCopy, rel)) return abort("cannot write " + utf8(dst) + " (a test's failure)");
                if (!copy_file_hashed(it->path(), dst, c.hash, c.size, progress, &w)) return abort(w);
                copied.push_back(c);
            } else if (fs::is_symlink(st)) {
                fs::copy_symlink(it->path(), dst, e);   // (as it is: never followed out of the folder)
                if (e) return abort("cannot copy the link " + utf8(it->path()) + ": " + e.message());
            }
        }
        if (ec) return abort("cannot list " + utf8(p.from) + ": " + ec.message());
    }
    // 2. every file read back: its size and hash as the source's
    if (progress) progress->step = 1;
    for (const Copied& c : copied) {
        if (progress && progress->cancel) return abort("cancelled");
        std::uintmax_t n = 0;
        const fs::path dst = p.partial / c.rel;
        const auto h = file_hash(dst, progress, &n);
        if (faulty(Fault::kVerify, c.rel) || !h || *h != c.hash || n != c.size)
            return abort("the copy of " + utf8(c.rel) + " does not match the original (" + utf8(dst.parent_path()) + "): nothing was moved");
    }
    // 2b. the source as it was when copied: the same files, sizes and times (a game started from Steam meanwhile, a
    // second launcher, a sync client: anything that wrote there aborts the move, the source as it now is)
    {
        std::string lw;
        const auto now = listing(p.from, &lw);
        bool same = lw.empty() && now.size() == copied.size();
        fs::path changed;
        for (const Copied& c : copied) {
            const auto it = now.find(c.rel);
            if (it == now.end() || it->second.first != c.size || it->second.second != c.time) {
                same = false;
                changed = c.rel;
                break;
            }
        }
        if (faulty(Fault::kChanged, p.from)) same = false;
        if (!same)
            return abort("files in " + utf8(p.from) + " changed while they were copied" + (changed.empty() ? std::string() : " (" + utf8(changed) + ")") +
                         ": nothing was moved; close the game and try again");
    }
    r.files = copied.size();
    for (const Copied& c : copied) r.bytes += c.size;
    if (progress && progress->cancel) return abort("cancelled");
    // 3. into place
    if (progress) progress->step = 2;
    fs::path aside;
    if (!p.to_portable && fs::is_directory(p.to_top, ec) && !non_empty_dir(p.to_top)) fs::remove(p.to_top, ec);   // (empty)
    if (!p.to_portable && fs::exists(fs::symlink_status(p.to_top, ec))) {   // off: this computer's own data, set aside
        aside = dated(p.to_top, "aside", stamp);
        std::string w;
        if (aside.empty() || !rename_dir(p.to_top, aside, &w)) return abort(w.empty() ? "cannot set " + utf8(p.to_top) + " aside" : w);
    }
    {
        std::string w;
        if (faulty(Fault::kRenameIn, p.partial_top) || !rename_dir(p.partial_top, p.to_top, &w)) {
            if (!aside.empty()) rename_dir(aside, p.to_top, nullptr);   // put back
            return abort(w.empty() ? "cannot rename " + utf8(p.partial_top) + " to " + utf8(p.to_top.filename()) : w);
        }
    }
    // 4. the folder moved from, under a dated name
    std::error_code e2;
    if (fs::exists(fs::symlink_status(p.from_top, e2))) {
        const fs::path old = dated(p.from_top, "moved", stamp);
        std::string w;
        if (old.empty() || faulty(Fault::kRenameAway, p.from_top) || !rename_dir(p.from_top, old, &w)) {
            if (p.to_portable) {
                // on: the install is portable with a checked copy; the old folder stays where it was (said)
                r.ok = true;
                r.aside = aside;
                r.note = "The old folder " + utf8(p.from_top) + " could not be renamed (" + (w.empty() ? std::string("a test's failure") : w) +
                         "); it is left as it was and no longer used.";
                return r;
            }
            // off: appdata/ still there would keep the install portable: the copy undone, this computer's data back
            remove_all_quiet(p.to_top);
            if (!aside.empty()) rename_dir(aside, p.to_top, nullptr);
            fail(r, (w.empty() ? "cannot rename " + utf8(p.from_top) : w) + " (is a file in it open?): nothing was moved");
            return r;
        }
        r.old_kept = old;
    }
    r.aside = aside;
    r.ok = true;
    return r;
}

int clean_partials(const fs::path& exe_dir, const fs::path& platform) {
    int n = 0;
    std::error_code ec;
    for (const fs::path& p : {exe_dir.empty() ? fs::path() : exe_dir / (std::string(b3io::kPortableFolder) + ".partial"),
                              platform.empty() ? fs::path() : platform.parent_path() / (utf8(platform.filename()) + ".partial")}) {
        if (p.empty() || !fs::exists(fs::symlink_status(p, ec))) continue;
        if (b3mod::remove_tree(p)) ++n;
    }
    return n;
}

std::optional<Record> read_record(const fs::path& data_dir) {
    if (data_dir.empty()) return std::nullopt;
    std::ifstream f(data_dir / kRecordFile, std::ios::binary);
    if (!f) return std::nullopt;
    Record r;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t eq = line.find(" = ");
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 3);
        if (k == "old") r.old_kept = b3io::u8path(v);
        else if (k == "aside") r.aside = b3io::u8path(v);
        else if (k == "started") r.started = v == "1";
        else if (k == "declined") r.declined = v == "1";
    }
    if (r.old_kept.empty() && r.aside.empty()) return std::nullopt;
    return r;
}

bool write_record(const fs::path& data_dir, const Record& r) {
    std::string t = "# portable mode (1.3.0): the data folder's copy kept after a move (b3launcher portable.h)\n";
    if (!r.old_kept.empty()) t += "old = " + utf8(r.old_kept) + "\n";
    if (!r.aside.empty()) t += "aside = " + utf8(r.aside) + "\n";
    t += std::string("started = ") + (r.started ? "1" : "0") + "\n";
    t += std::string("declined = ") + (r.declined ? "1" : "0") + "\n";
    return b3io::replace_file(data_dir / kRecordFile, t);
}

void clear_record(const fs::path& data_dir) {
    std::error_code ec;
    if (!data_dir.empty()) fs::remove(data_dir / kRecordFile, ec);
}

bool remove_old(const fs::path& old_kept, std::string* why) {
    const std::string name = utf8(old_kept.filename());
    const bool ours = name.find(".moved-") != std::string::npos || name.find(".aside-") != std::string::npos;
    if (old_kept.empty() || !ours) return (why && (*why = "not a folder the move set aside: " + utf8(old_kept), true)), false;
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(old_kept, ec))) return true;   // gone already
    if (!b3mod::remove_tree(old_kept)) return (why && (*why = "cannot remove all of " + utf8(old_kept) + " (a file in it open?)", true)), false;
    return true;
}

std::string rebase_saves_dir(const std::string& saves_dir_utf8, const fs::path& from, const fs::path& to) {
    if (saves_dir_utf8.empty()) return saves_dir_utf8;
    const fs::path s = b3io::u8path(saves_dir_utf8).lexically_normal();
    const fs::path f = from.lexically_normal();
    const fs::path rel = s.lexically_relative(f);
    if (rel.empty() || utf8(rel).rfind("..", 0) == 0) return saves_dir_utf8;   // elsewhere: as it is
    return utf8(rel == "." ? to : to / rel);
}

std::string stamp_now() {
    const std::time_t t = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char s[32];
    std::strftime(s, sizeof s, "%Y%m%d-%H%M%S", &lt);
    return s;
}

}  // namespace b3launcher::portable
