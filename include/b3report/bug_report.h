// bug_report.h -- a tester's bug report (test builds): one bug-report-<YYYYMMDD-HHMMSS>.zip with what a developer needs
// to see and replay a problem, and nothing private beyond it:
//   README.txt            what the archive holds, the version, where it was made (the launcher, the game's F10)
//   version.txt           the build's version line
//   system-info.txt       OS, CPU, RAM, GPU and driver, display mode (the caller's text: sysinfo.h)
//   pc_settings.ini, launcher.ini, run_state.ini, mods.ini (the mods' load order)   from the user data folder, when there
//   logs/run-*.log, logs/run-*.pads.txt              the newest `runs` runs (run_logs.h): output and pad recordings
//   save/profile-N.sav    only when the user asked for it (`save_file`)
// No other save, no game data. A file larger than `max_file_bytes` goes in as its head and tail with a note between.
// Standard library + zip.h; the launcher (its button) and b3render (F10 in the game) make them.
#ifndef B3REPORT_BUG_REPORT_H
#define B3REPORT_BUG_REPORT_H

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace b3port {

struct BugReportInput {
    std::filesystem::path pref;      // the user data folder (b3io::user_dir(), include/b3port/user_dir.h)
    std::filesystem::path out_dir;   // where the zip is written (report_folder)
    int runs = 5;                    // the newest runs' logs and pad recordings
    std::filesystem::path save_file; // empty: no save in the report
    std::string system_info;         // system-info.txt
    std::string origin;              // "the launcher", "the game (F10)"
    std::time_t now = 0;             // the report's time (its name); 0: now
    std::uintmax_t max_file_bytes = 24u << 20;
};
struct BugReportResult {
    bool ok = false;
    std::filesystem::path zip;
    std::string error;
    std::vector<std::string> entries;   // the archive's names, in order
};
BugReportResult make_bug_report(const BugReportInput& in);

// "bug-report-20261109-214501.zip"
std::string bug_report_name(std::time_t now);
// The first of `candidates` that is an existing folder (Desktop, Documents, the user data folder ...); else empty.
std::filesystem::path report_folder(const std::vector<std::filesystem::path>& candidates);
// `bytes` cut to at most `max` bytes: the first quarter and the last three quarters, a note line between (unchanged
// when it fits)
std::string cap_bytes(const std::string& bytes, std::uintmax_t max);
// The text with the reporter's identity out of its paths (bug reports go on public issue pages): each of `homes` (the
// home folder, either slash, any case) -> "~", and the folder after a `Users` / `home` path part (C:\Users\<name>,
// /Users/<name>, /home/<name>, /mnt/c/Users/<name>) -> "<user>". Anything else is unchanged.
std::string redact_paths(const std::string& text, const std::vector<std::string>& homes = {});
// The home folders redact_paths takes out: HOME and USERPROFILE (those that are set, 3 or more characters).
std::vector<std::string> home_folders();

}  // namespace b3port

#endif
