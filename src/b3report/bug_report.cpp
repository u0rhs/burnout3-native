// bug_report.cpp -- see include/b3report/bug_report.h
#include "b3report/bug_report.h"

#include "b3report/run_logs.h"
#include "b3report/version.h"
#include "b3report/zip.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace b3port {

namespace fs = std::filesystem;

std::string bug_report_name(std::time_t now) { return "bug-report-" + runlog::stamp(now) + ".zip"; }

fs::path report_folder(const std::vector<fs::path>& candidates) {
    std::error_code ec;
    for (const fs::path& p : candidates)
        if (!p.empty() && fs::is_directory(p, ec)) return p;
    return {};
}

std::string cap_bytes(const std::string& bytes, std::uintmax_t max) {
    if (bytes.size() <= max || max < 64) return bytes;
    const std::size_t head = std::size_t(max / 4), tail = std::size_t(max - head);
    const std::string note = "\n\n[... " + std::to_string(bytes.size() - head - tail) + " bytes left out of the bug report ...]\n\n";
    return bytes.substr(0, head) + note + bytes.substr(bytes.size() - tail);
}

namespace {

bool is_sep(char c) { return c == '/' || c == '\\'; }

// `s` at `i` equals `key` ignoring ASCII case, and with '/' and '\\' as one
bool match_at(const std::string& s, std::size_t i, const std::string& key) {
    if (i + key.size() > s.size()) return false;
    for (std::size_t k = 0; k < key.size(); ++k) {
        const char a = s[i + k], b = key[k];
        if (is_sep(a) && is_sep(b)) continue;
        if (std::tolower(static_cast<unsigned char>(a)) != std::tolower(static_cast<unsigned char>(b))) return false;
    }
    return true;
}

}  // namespace

std::vector<std::string> home_folders() {
    std::vector<std::string> out;
    for (const char* v : {"HOME", "USERPROFILE"}) {
        const char* h = std::getenv(v);
        if (!h) continue;
        std::string p = h;
        while (p.size() > 1 && is_sep(p.back())) p.pop_back();
        if (p.size() >= 3 && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    }
    return out;
}

std::string redact_paths(const std::string& text, const std::vector<std::string>& homes) {
    std::vector<std::string> hs;
    for (std::string h : homes) {
        while (h.size() > 1 && is_sep(h.back())) h.pop_back();
        if (h.size() >= 3) hs.push_back(h);
    }
    std::sort(hs.begin(), hs.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    static const std::string kParents[] = {"users", "home"};
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        bool done = false;
        for (const std::string& h : hs)   // the home folder itself, where a path part ends after it
            if (match_at(text, i, h) && (i + h.size() == text.size() || !std::isalnum(static_cast<unsigned char>(text[i + h.size()])))) {
                out += '~';
                i += h.size();
                done = true;
                break;
            }
        if (done) continue;
        if (is_sep(text[i]))
            for (const std::string& key : kParents) {
                const std::size_t a = i + 1, b = a + key.size();
                if (b < text.size() && match_at(text, a, key) && is_sep(text[b])) {
                    std::size_t e = b + 1;
                    while (e < text.size() && !is_sep(text[e]) && text[e] != '\r' && text[e] != '\n' && text[e] != '"' && text[e] != '\'' &&
                           text[e] != '<' && text[e] != '>' && text[e] != '|' && text[e] != '\t')
                        ++e;
                    out.append(text, i, b + 1 - i);
                    if (e > b + 1) out += "<user>";
                    i = e;
                    done = true;
                    break;
                }
            }
        if (done) continue;
        out += text[i++];
    }
    return out;
}

namespace {

bool read_file(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

std::time_t mtime_of(const fs::path& p, std::time_t fallback) {
    std::error_code ec;
    const auto ft = fs::last_write_time(p, ec);
    if (ec) return fallback;
    const auto sys = std::chrono::system_clock::now() +
                     std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now());
    return std::chrono::system_clock::to_time_t(sys);
}

}  // namespace

BugReportResult make_bug_report(const BugReportInput& in) {
    BugReportResult r;
    const std::time_t now = in.now ? in.now : std::time(nullptr);
    std::error_code ec;
    if (in.out_dir.empty() || !fs::is_directory(in.out_dir, ec)) {
        r.error = "no folder to write the report to";
        return r;
    }
    fs::path zip_path = in.out_dir / bug_report_name(now);
    for (int n = 2; fs::exists(zip_path, ec) && n < 100; ++n)
        zip_path = in.out_dir / ("bug-report-" + runlog::stamp(now) + "-" + std::to_string(n) + ".zip");
    ZipWriter z;
    if (!z.open(zip_path)) {
        r.error = z.error();
        return r;
    }
    // public issue pages: the reporter's name out of every text's paths (the save file stays as it is)
    const std::vector<std::string> homes = home_folders();
    auto add_text = [&](const std::string& name, const std::string& text) {
        if (z.add(name, redact_paths(text, homes), now)) r.entries.push_back(name);
    };
    auto add_file = [&](const std::string& name, const fs::path& p) {
        std::string bytes;
        if (!read_file(p, bytes)) return;
        bytes = cap_bytes(bytes, in.max_file_bytes);
        const std::string ext = p.extension().string();
        if (ext == ".ini" || ext == ".log" || ext == ".txt") bytes = redact_paths(bytes, homes);
        if (z.add(name, bytes.data(), bytes.size(), mtime_of(p, now))) r.entries.push_back(name);
    };

    const fs::path logs = runlog::logs_dir(in.pref);
    std::vector<std::string> run_names = runlog::runs(logs);
    if (run_names.size() > std::size_t(std::max(in.runs, 0))) run_names.resize(std::size_t(std::max(in.runs, 0)));
    std::string readme = "Burnout 3 native port -- bug report\n\n";
    readme += version_line() + "\n";
    readme += "Made by " + (in.origin.empty() ? std::string("the launcher") : in.origin) + " at " + runlog::stamp(now) + " (local time).\n\n";
    readme += "version.txt       the build\n"
              "system-info.txt   OS, CPU, RAM, GPU and driver, display\n"
              "*.ini             the PC settings, the launcher's choices, the last run's state, the mods' load order\n"
              "logs/             the newest runs: run-<date-time>.log (the game's output) and .pads.txt (its controls,\n"
              "                  replayable: native_play --replay-pad <file>)\n";
    readme += in.save_file.empty() ? "No save file (not asked for).\n" : "save/              the active save file (asked for)\n";
    readme += "No game data is in this archive.\n\nRuns:";
    if (run_names.empty()) readme += " none yet\n";
    else {
        readme += "\n";
        for (const std::string& n : run_names) readme += "  " + n + "\n";
    }
    add_text("README.txt", readme);
    add_text("version.txt", version_line() + "\n");
    add_text("system-info.txt", in.system_info.empty() ? std::string("(no system information)\n") : in.system_info);
    for (const char* f : {"pc_settings.ini", "launcher.ini", "run_state.ini", "mods.ini"})   // modkit1: mods.ini, the load order
        if (fs::is_regular_file(in.pref / f, ec)) add_file(f, in.pref / f);
    for (const std::string& n : run_names)
        for (const fs::path& f : runlog::run_files(logs, n)) add_file("logs/" + f.filename().string(), f);
    if (!in.save_file.empty()) {
        if (fs::is_regular_file(in.save_file, ec)) add_file("save/" + in.save_file.filename().string(), in.save_file);
        else add_text("save/MISSING.txt", "The save file " + in.save_file.string() + " was asked for and is not there.\n");
    }
    if (!z.close()) {
        r.error = z.error();
        r.entries.clear();
        return r;
    }
    r.ok = true;
    r.zip = zip_path;
    return r;
}

}  // namespace b3port
