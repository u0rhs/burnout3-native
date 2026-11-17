// version.h -- the build's version stamp (test builds for testers): the release base from the repo's VERSION
// file, the git commit it was built from (short hash, "-dirty" when tracked files had changes) and the build date.
// Printed at the top of every native_play log, shown in the launcher and in the game window's title, written into
// bug reports.
//
// version() / build_date() are defined in a generated source (tools/release/version_info.sh -> build/version_info.cpp,
// rewritten only when its text changes, so a new commit recompiles that one file); the formatting is here.
#ifndef B3REPORT_VERSION_H
#define B3REPORT_VERSION_H

#include <string>
#include <string_view>

namespace b3port {

const char* version();      // "0.1.0-test+1a2b3c4d" (base + "+" + commit; the base alone without git)
const char* build_date();   // "2026-11-09" (UTC)

// base "0.1.0-test", hash "1a2b3c4d" -> "0.1.0-test+1a2b3c4d"; an empty hash: the base alone
inline std::string format_version(std::string_view base, std::string_view hash) {
    std::string v(base.empty() ? std::string_view("0.0.0") : base);
    if (!hash.empty()) v.append("+").append(hash);
    return v;
}
// "Burnout 3 native port 0.1.0-test+1a2b3c4d (built 2026-11-09)"
inline std::string version_line(std::string_view ver, std::string_view date) {
    std::string s = "Burnout 3 native port " + std::string(ver);
    if (!date.empty()) s.append(" (built ").append(date).append(")");
    return s;
}
inline std::string version_line() { return version_line(version(), build_date()); }
// The version as a file name part: anything outside [A-Za-z0-9._-] ('+') becomes '-' ("0.1.0-test-1a2b3c4d")
inline std::string version_for_file(std::string_view ver) {
    std::string s(ver);
    for (char& c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) c = '-';
    return s;
}

}  // namespace b3port

#endif
