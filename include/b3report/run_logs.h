// run_logs.h -- the game's per-run files in the user data folder (test builds): every window run of native_play
// leaves <pref>/logs/run-<YYYYMMDD-HHMMSS>.log (its output) and run-<...>.pads.txt (its pad recording, replayable with
// native_play --replay-pad), and only the newest kKeepRuns runs stay. Bug reports (bug_report.h) take the newest few.
// Standard library only, header-only (native_play, the launcher, b3render and the tests share it).
#ifndef B3REPORT_RUN_LOGS_H
#define B3REPORT_RUN_LOGS_H

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace b3port::runlog {

namespace fs = std::filesystem;

constexpr int kKeepRuns = 10;

// <pref>/logs
inline fs::path logs_dir(const fs::path& pref) { return pref / "logs"; }

// "20261109-214501" in local time
inline std::string stamp(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    std::snprintf(b, sizeof b, "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

struct RunPaths {
    fs::path log;    // run-<stamp>.log
    fs::path pads;   // run-<stamp>.pads.txt
};
// The pad recording beside a log: <dir>/<name>.pads.txt for <dir>/<name>.log (any other name: + ".pads.txt")
inline RunPaths paths_for_log(const fs::path& log) {
    fs::path p = log;
    if (p.extension() == ".log") p.replace_extension("");
    p += ".pads.txt";
    return {log, p};
}
// A new run's files in `dir` at `now`: run-<stamp>.log, or run-<stamp>-2.log ... when that run exists (two starts in
// one second). The files are not created.
inline RunPaths new_run(const fs::path& dir, std::time_t now) {
    const std::string base = "run-" + stamp(now);
    std::error_code ec;
    for (int n = 1; n < 100; ++n) {
        const std::string name = n == 1 ? base : base + "-" + std::to_string(n);
        const RunPaths r = paths_for_log(dir / (name + ".log"));
        if (!fs::exists(r.log, ec) && !fs::exists(r.pads, ec)) return r;
    }
    return paths_for_log(dir / (base + "-99.log"));
}

namespace detail {
// "run-20261109-214501[-N]" -> sort key (stamp, N); false: not a run's name
inline bool run_key(const std::string& stem, std::string& when, int& n) {
    if (stem.size() < 19 || stem.compare(0, 4, "run-") != 0) return false;
    when = stem.substr(4, 15);
    for (std::size_t i = 0; i < when.size(); ++i)
        if (i == 8 ? when[i] != '-' : (when[i] < '0' || when[i] > '9')) return false;
    n = 1;
    if (stem.size() > 19) {
        if (stem[19] != '-' || stem.size() == 20) return false;
        n = 0;
        for (std::size_t i = 20; i < stem.size(); ++i) {
            if (stem[i] < '0' || stem[i] > '9') return false;
            n = n * 10 + (stem[i] - '0');
        }
    }
    return true;
}
// The run a file belongs to: its name up to the first '.' ("run-...-2" of "run-...-2.pads.txt")
inline std::string run_of(const fs::path& file) {
    const std::string name = file.filename().string();
    return name.substr(0, name.find('.'));
}
}  // namespace detail

// The runs in `dir` (their names: "run-20261109-214501"), newest first.
inline std::vector<std::string> runs(const fs::path& dir) {
    struct R { std::string when; int n; std::string name; };
    std::vector<R> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string stem = detail::run_of(it->path());
        R r{{}, 0, stem};
        if (!detail::run_key(stem, r.when, r.n)) continue;
        if (std::none_of(out.begin(), out.end(), [&](const R& o) { return o.name == stem; })) out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](const R& a, const R& b) { return a.when != b.when ? a.when > b.when : a.n > b.n; });
    std::vector<std::string> names;
    for (const R& r : out) names.push_back(r.name);
    return names;
}
// The files of run `name` in `dir` (its .log, .pads.txt, ...), sorted by name.
inline std::vector<fs::path> run_files(const fs::path& dir, const std::string& name) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && detail::run_of(it->path()) == name) out.push_back(it->path());
    std::sort(out.begin(), out.end());
    return out;
}
// Keeps the newest `keep` runs in `dir` and deletes every file of the others (files that are not a run's are left).
// Returns the number of files deleted.
inline int rotate(const fs::path& dir, int keep = kKeepRuns) {
    const std::vector<std::string> all = runs(dir);
    int removed = 0;
    for (std::size_t i = std::size_t(std::max(keep, 0)); i < all.size(); ++i)
        for (const fs::path& f : run_files(dir, all[i])) {
            std::error_code ec;
            if (fs::remove(f, ec)) ++removed;
        }
    return removed;
}

}  // namespace b3port::runlog

#endif
