// Test builds' reporting (include/b3report): the ZIP writer (each archive read back here through its central directory
// and zlib's inflate, as an unzip tool reads it), the run logs' names and rotation, the version string, and a whole
// bug report -- what is in it and what is not (no save unless asked, the newest runs only).
#include "test.h"

#include "b3report/bug_report.h"
#include "b3report/run_logs.h"
#include "b3report/uncaught.h"
#include "b3report/version.h"
#include "b3report/zip.h"

#include <zlib.h>

#ifndef _WIN32
#include "../tools/native/run_log.h"
#include <csignal>
#include <sys/wait.h>
#endif
#include <fstream>
#include <thread>
#include <iterator>
#include <map>

namespace fs = std::filesystem;

namespace {

const fs::path kTmp = fs::path("build") / "test_report_tmp";

fs::path fresh(const std::string& name) {
    const fs::path d = kTmp / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}
void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
std::uint32_t u16(const std::string& b, std::size_t at) { return std::uint8_t(b[at]) | std::uint32_t(std::uint8_t(b[at + 1])) << 8; }
std::uint32_t u32(const std::string& b, std::size_t at) { return u16(b, at) | u16(b, at + 2) << 16; }

struct Item {
    std::string data;
    std::uint16_t method = 0;
    std::uint32_t dos = 0, flags = 0;
};
// A ZIP's entries by name, read as an unzip tool reads them: the end record, the central directory, each local header
// (checked against it), the data inflated and its CRC checked. Empty map + *why on any inconsistency.
std::map<std::string, Item> read_zip(const fs::path& p, std::string* why) {
    std::map<std::string, Item> out;
    const std::string b = slurp(p);
    auto bad = [&](const std::string& w) { *why = w; return std::map<std::string, Item>{}; };
    if (b.size() < 22 || u32(b, b.size() - 22) != 0x06054b50) return bad("no end record at the end");
    const std::size_t e = b.size() - 22;
    const std::uint32_t n = u16(b, e + 10), cd_size = u32(b, e + 12), cd_at = u32(b, e + 16);
    if (u16(b, e + 8) != n || cd_at + cd_size != e) return bad("end record sizes");
    std::size_t at = cd_at;
    for (std::uint32_t i = 0; i < n; ++i) {
        if (u32(b, at) != 0x02014b50) return bad("central header signature");
        Item it;
        it.flags = u16(b, at + 8);
        it.method = std::uint16_t(u16(b, at + 10));
        it.dos = u32(b, at + 12);
        const std::uint32_t crc = u32(b, at + 16), cs = u32(b, at + 20), us = u32(b, at + 24);
        const std::uint32_t nl = u16(b, at + 28), xl = u16(b, at + 30), cl = u16(b, at + 32), off = u32(b, at + 42);
        const std::string name = b.substr(at + 46, nl);
        at += 46 + nl + xl + cl;
        if (u32(b, off) != 0x04034b50) return bad(name + ": local header signature");
        if (u16(b, off + 8) != it.method || u32(b, off + 14) != crc || u32(b, off + 18) != cs || u32(b, off + 22) != us ||
            b.compare(off + 30, u16(b, off + 26), name) != 0)
            return bad(name + ": local header differs from the central one");
        const std::string body = b.substr(off + 30 + u16(b, off + 26) + u16(b, off + 28), cs);
        if (it.method == 0) it.data = body;
        else if (it.method == 8) {
            it.data.assign(us, '\0');
            z_stream z{};
            inflateInit2(&z, -MAX_WBITS);
            z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(body.data()));
            z.avail_in = uInt(body.size());
            z.next_out = reinterpret_cast<Bytef*>(it.data.data());
            z.avail_out = uInt(us);
            const int rc = inflate(&z, Z_FINISH);
            const uLong got = z.total_out;
            inflateEnd(&z);
            if (rc != Z_STREAM_END || got != us) return bad(name + ": inflate");
        } else return bad(name + ": method");
        if (crc32(0, reinterpret_cast<const Bytef*>(it.data.data()), uInt(it.data.size())) != crc) return bad(name + ": crc");
        out[name] = it;
    }
    return out;
}

}  // namespace

// ---- the ZIP writer ----

TEST(crc32_known_value) {
    CHECK_BITS(b3port::crc32_of("123456789", 9), 0xCBF43926u);   // the CRC-32 check value
    CHECK_BITS(b3port::crc32_of("", 0), 0u);
    const std::uint32_t part = b3port::crc32_of("12345", 5);
    CHECK_BITS(b3port::crc32_of("6789", 4, part), 0xCBF43926u);   // continued
}

TEST(dos_time_fields) {
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 10;   // November
    tm.tm_mday = 9;
    tm.tm_hour = 21;
    tm.tm_min = 45;
    tm.tm_sec = 31;
    tm.tm_isdst = -1;
    const std::uint32_t d = b3port::ZipWriter::dos_time(std::mktime(&tm));
    CHECK((d >> 16 >> 9) == 2026 - 1980);
    CHECK(((d >> 16 >> 5) & 15) == 11);
    CHECK(((d >> 16) & 31) == 9);
    CHECK(((d >> 11) & 31) == 21);
    CHECK(((d >> 5) & 63) == 45);
    CHECK((d & 31) == 15);   // two-second units
    CHECK(b3port::ZipWriter::dos_time(0) == ((1u << 5 | 1u) << 16));   // before 1980: its first day
}

TEST(zip_roundtrip_stored_and_deflated) {
    const fs::path d = fresh("zip");
    std::string big;
    for (int i = 0; i < 5000; ++i) big += "native_play: VBlank " + std::to_string(i) + ": a line of log\n";
    std::string noise(4096, '\0');
    std::uint32_t x = 12345;
    for (char& c : noise) { x = x * 1103515245u + 12345u; c = char(x >> 24); }
    const std::time_t now = std::time(nullptr);
    {
        b3port::ZipWriter z;
        CHECK(z.open(d / "a.zip"));
        CHECK(z.add("logs/run-20261109-214501.log", big, now));
        CHECK(z.add("noise.bin", noise.data(), noise.size(), now));
        CHECK(z.add("empty.txt", std::string(), now));
        CHECK(z.add("raw.txt", "stored", 6, now, false));
        CHECK(z.add("名前.txt", std::string("utf-8"), now));
        CHECK(z.entries() == 5);
        CHECK(z.close());
    }
    std::string why;
    const auto m = read_zip(d / "a.zip", &why);
    CHECK(why.empty());
    CHECK(m.size() == 5);
    CHECK(m.count("logs/run-20261109-214501.log") && m.at("logs/run-20261109-214501.log").data == big);
    CHECK(m.count("logs/run-20261109-214501.log") && m.at("logs/run-20261109-214501.log").method == 8);   // text: deflated
    CHECK(m.count("noise.bin") && m.at("noise.bin").data == noise && m.at("noise.bin").method == 0);   // no gain: stored
    CHECK(m.count("empty.txt") && m.at("empty.txt").data.empty());
    CHECK(m.count("raw.txt") && m.at("raw.txt").method == 0 && m.at("raw.txt").data == "stored");
    CHECK(m.count("名前.txt") && (m.at("名前.txt").flags & (1u << 11)));   // UTF-8 names flagged
    CHECK(fs::file_size(d / "a.zip") < big.size() / 4);   // the log compressed
}

TEST(zip_add_file_and_errors) {
    const fs::path d = fresh("zipfile");
    write(d / "in.txt", "from disk\n");
    {
        b3port::ZipWriter z;
        CHECK(z.open(d / "b.zip"));
        CHECK(z.add_file("in.txt", d / "in.txt"));
        CHECK(!z.add_file("missing.txt", d / "missing.txt"));
        CHECK(!z.error().empty());
        CHECK(!z.close());   // an error: the archive is not finished
    }
    CHECK(!fs::exists(d / "b.zip"));   // and removed
    {
        b3port::ZipWriter z;
        CHECK(z.open(d / "c.zip"));
        CHECK(z.add_file("in.txt", d / "in.txt"));
        CHECK(z.close());
    }
    std::string why;
    const auto m = read_zip(d / "c.zip", &why);
    CHECK(m.size() == 1 && m.count("in.txt") && m.at("in.txt").data == "from disk\n");
    {
        b3port::ZipWriter z;   // never closed: removed by the destructor
        CHECK(z.open(d / "d.zip"));
        CHECK(z.add("x", std::string("y"), 0));
    }
    CHECK(!fs::exists(d / "d.zip"));
    b3port::ZipWriter z;
    CHECK(!z.open(d / "no-such-folder" / "e.zip"));
}

// ---- run logs ----

TEST(run_log_names) {
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 0;
    tm.tm_mday = 2;
    tm.tm_hour = 3;
    tm.tm_min = 4;
    tm.tm_sec = 5;
    tm.tm_isdst = -1;
    const std::time_t t = std::mktime(&tm);
    CHECK(b3port::runlog::stamp(t) == "20260102-030405");
    const auto r = b3port::runlog::paths_for_log(fs::path("logs") / "run-20260102-030405.log");
    CHECK(r.pads.filename() == "run-20260102-030405.pads.txt");
    CHECK(b3port::runlog::paths_for_log("x/given").pads.filename() == "given.pads.txt");
    const fs::path d = fresh("names");
    const auto a = b3port::runlog::new_run(d, t);
    CHECK(a.log.filename() == "run-20260102-030405.log");
    write(a.log, "1");
    const auto b = b3port::runlog::new_run(d, t);   // the same second: a second run's name
    CHECK(b.log.filename() == "run-20260102-030405-2.log");
    write(b.pads, "p");
    CHECK(b3port::runlog::new_run(d, t).log.filename() == "run-20260102-030405-3.log");   // a pads file alone counts too
    CHECK(b3port::bug_report_name(t) == "bug-report-20260102-030405.zip");
}

TEST(run_log_rotation) {
    const fs::path d = fresh("rotate");
    // 13 runs over two days, some with a pad file, one "-2" in the same second, and files that are not runs
    std::vector<std::string> names;
    for (int i = 0; i < 12; ++i) {
        char b[40];
        std::snprintf(b, sizeof b, "run-202611%02d-1200%02d", 8 + i / 6, i);
        names.push_back(b);
        write(d / (std::string(b) + ".log"), "log");
        if (i % 2 == 0) write(d / (std::string(b) + ".pads.txt"), "pads");
    }
    names.push_back("run-20261109-120011-2");   // the newest: the same second as the last, second
    write(d / "run-20261109-120011-2.log", "log");
    write(d / "notes.txt", "mine");
    write(d / "run-bad.log", "not a run's name");
    const auto all = b3port::runlog::runs(d);
    CHECK(all.size() == 13);
    CHECK(!all.empty() && all.front() == "run-20261109-120011-2");
    CHECK(all.size() > 1 && all[1] == "run-20261109-120011");
    CHECK(all.back() == "run-20261108-120000");
    CHECK(b3port::runlog::run_files(d, "run-20261108-120000").size() == 2);   // .log and .pads.txt
    const int removed = b3port::runlog::rotate(d, 10);
    CHECK(removed == 5);   // the three oldest runs: 120000 (2 files), 120001 (1), 120002 (2)
    const auto left = b3port::runlog::runs(d);
    CHECK(left.size() == 10);
    CHECK(!fs::exists(d / "run-20261108-120000.log") && !fs::exists(d / "run-20261108-120000.pads.txt"));
    CHECK(!fs::exists(d / "run-20261108-120002.pads.txt"));
    CHECK(fs::exists(d / "run-20261108-120003.log"));
    CHECK(fs::exists(d / "notes.txt") && fs::exists(d / "run-bad.log"));   // not runs: left alone
    CHECK(b3port::runlog::rotate(d, 10) == 0);
    CHECK(b3port::runlog::runs(fresh("empty")).empty());
    CHECK(b3port::runlog::runs(d / "no-such-folder").empty());
}

// ---- the version ----

TEST(version_strings) {
    CHECK(b3port::format_version("0.1.0-test", "1a2b3c4d") == "0.1.0-test+1a2b3c4d");
    CHECK(b3port::format_version("0.1.0-test", "") == "0.1.0-test");
    CHECK(b3port::format_version("", "") == "0.0.0");
    CHECK(b3port::version_line("0.1.0-test+1a2b3c4d", "2026-11-09") == "Burnout 3 native port 0.1.0-test+1a2b3c4d (built 2026-11-09)");
    CHECK(b3port::version_for_file("0.1.0-test+1a2b3c4d-dirty") == "0.1.0-test-1a2b3c4d-dirty");
    // the generated one: VERSION's base, then the commit; a date as YYYY-MM-DD
    const std::string base = [] {
        std::string v = slurp("VERSION");
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
        return v;
    }();
    const std::string v = b3port::version();
    CHECK(!base.empty() && v.rfind(base, 0) == 0);
    CHECK(v.size() == base.size() || (v.size() > base.size() + 1 && v[base.size()] == '+'));
    const std::string d = b3port::build_date();
    CHECK(d.size() == 10 && d[4] == '-' && d[7] == '-');
    CHECK(b3port::version_line().find(v) != std::string::npos);
}

// ---- a whole report ----

TEST(bug_report_contents) {
    const fs::path pref = fresh("pref"), out = fresh("out");
    write(pref / "pc_settings.ini", "msaa = 4\n");
    write(pref / "launcher.ini", "game_dir = D:/Games/B3\n");
    write(pref / "mods.ini", "[order]\nhello-text = on\n");   // modkit1: the mods' load order goes in
    write(pref / "mods" / "hello-text" / "data" / "DATA" / "X.BIN", "MOD");   // a mod's files do not
    write(pref / "saves" / "profile-1.sav", "SAVE1");
    write(pref / "saves" / "profile-2.sav", "SAVE2");
    write(pref / "screenshots" / "Burnout3-1.png", "PNG");
    for (int i = 0; i < 7; ++i) {
        const std::string n = "run-20261109-10000" + std::to_string(i);
        write(pref / "logs" / (n + ".log"), "log " + std::to_string(i) + "\n");
        write(pref / "logs" / (n + ".pads.txt"), "pads " + std::to_string(i) + "\n");
    }
    b3port::BugReportInput in;
    in.pref = pref;
    in.out_dir = out;
    in.runs = 5;
    in.system_info = "os: test\n";
    in.origin = "the test";
    in.now = 1794200000;
    auto r = b3port::make_bug_report(in);
    CHECK(r.ok);
    CHECK(r.zip.filename() == b3port::bug_report_name(in.now));
    std::string why;
    auto m = read_zip(r.zip, &why);
    CHECK(why.empty());
    CHECK(m.count("README.txt") && m.count("version.txt") && m.count("system-info.txt"));
    CHECK(m.count("version.txt") && m.at("version.txt").data == b3port::version_line() + "\n");
    CHECK(m.count("system-info.txt") && m.at("system-info.txt").data == "os: test\n");
    CHECK(m.count("pc_settings.ini") && m.count("launcher.ini") && !m.count("run_state.ini"));   // only what is there
    CHECK(m.count("mods.ini") && m.at("mods.ini").data == "[order]\nhello-text = on\n");
    for (const auto& [name, e] : m) CHECK(name.rfind("mods/", 0) != 0);
    for (int i = 0; i < 7; ++i) {
        const std::string n = "logs/run-20261109-10000" + std::to_string(i);
        CHECK(m.count(n + ".log") == (i >= 2 ? 1u : 0u));   // the newest five runs
        CHECK(m.count(n + ".pads.txt") == (i >= 2 ? 1u : 0u));
    }
    CHECK(m.count("logs/run-20261109-100006.log") && m.at("logs/run-20261109-100006.log").data == "log 6\n");
    for (const auto& [name, item] : m) {   // no save, no screenshot, nothing else
        CHECK(name.find(".sav") == std::string::npos);
        CHECK(name.find("png") == std::string::npos);
        CHECK(item.data.find("SAVE") == std::string::npos);
    }
    CHECK(m.size() == 3 + 3 + 10);   // README / version / system, three ini files, five runs
    CHECK(r.entries.size() == m.size());
    // asked for: the active save, under save/
    in.save_file = pref / "saves" / "profile-2.sav";
    auto r2 = b3port::make_bug_report(in);
    CHECK(r2.ok);
    CHECK(r2.zip != r.zip);   // the same second: a second name
    m = read_zip(r2.zip, &why);
    CHECK(m.count("save/profile-2.sav") && m.at("save/profile-2.sav").data == "SAVE2");
    CHECK(!m.count("save/profile-1.sav"));
    // a missing out folder is an error, not a crash
    in.out_dir = out / "missing";
    CHECK(!b3port::make_bug_report(in).ok);
    CHECK(b3port::report_folder({out / "missing", fs::path(), out}) == out);
    CHECK(b3port::report_folder({out / "missing"}).empty());
}

TEST(bug_report_redacts_paths) {   // public issue pages: no user name in a report's paths
    using b3port::redact_paths;
    CHECK(redact_paths("game_dir = C:\\Users\\Jane Doe\\Games\\B3\n") == "game_dir = C:\\Users\\<user>\\Games\\B3\n");
    CHECK(redact_paths("C:/Users/jane/AppData/Roaming/b3port") == "C:/Users/<user>/AppData/Roaming/b3port");
    CHECK(redact_paths("open /home/deck/Games/x.iso failed") == "open /home/<user>/Games/x.iso failed");
    CHECK(redact_paths("/Users/sam/Library/Application Support/b3port") == "/Users/<user>/Library/Application Support/b3port");
    CHECK(redact_paths("/mnt/c/users/Bob/b3\r\n") == "/mnt/c/users/<user>/b3\r\n");
    CHECK(redact_paths("\"C:\\USERS\\x\"") == "\"C:\\USERS\\<user>\"");
    CHECK(redact_paths("C:\\Users\\") == "C:\\Users\\");   // nothing after it
    // the home folder itself, wherever it is, either slash and any case
    CHECK(redact_paths("D:\\Profiles\\jd\\b3\\run.log", {"D:\\Profiles\\jd"}) == "~\\b3\\run.log");
    CHECK(redact_paths("d:/profiles/JD/b3", {"D:\\Profiles\\jd\\"}) == "~/b3");
    CHECK(redact_paths("D:\\Profiles\\jdx\\b3", {"D:\\Profiles\\jd"}) == "D:\\Profiles\\jdx\\b3");   // another folder
    // left alone: words, the game's own paths, other drives
    for (const char* t : {"users can rebind keys", "home straight", "cdrom0:\\TRACKS\\US\\C2_V2\\STATIC.DAT;1", "D:/Games/B3",
                          "/usr/share/vulkan", "user data folder"})
        CHECK(redact_paths(t) == t);
    // in a report: the ini and the logs redacted, the save byte for byte
    const fs::path pref = fresh("redactpref"), out = fresh("redactout");
    write(pref / "launcher.ini", "game_dir = C:\\Users\\Jane\\B3\n");
    write(pref / "logs" / "run-20261113-120000.log", "disc /home/jane/b3.iso\n");
    write(pref / "logs" / "run-20261113-120000.pads.txt", "pads\n");
    write(pref / "saves" / "s.sav", "/home/jane/raw");
    b3port::BugReportInput in;
    in.pref = pref;
    in.out_dir = out;
    in.now = 1794300000;
    in.save_file = pref / "saves" / "s.sav";
    auto r = b3port::make_bug_report(in);
    CHECK(r.ok);
    std::string why;
    auto m = read_zip(r.zip, &why);
    CHECK(m.count("launcher.ini") && m.at("launcher.ini").data == "game_dir = C:\\Users\\<user>\\B3\n");
    CHECK(m.count("logs/run-20261113-120000.log") && m.at("logs/run-20261113-120000.log").data == "disc /home/<user>/b3.iso\n");
    CHECK(m.count("save/s.sav") && m.at("save/s.sav").data == "/home/jane/raw");
}

TEST(bug_report_caps_big_files) {
    const std::string s(1000, 'a');
    CHECK(b3port::cap_bytes(s, 2000) == s);
    const std::string head = std::string(250, 'h'), tail = std::string(750, 't');
    const std::string capped = b3port::cap_bytes(head + std::string(5000, 'm') + tail, 1000);
    CHECK(capped.rfind(head, 0) == 0);
    CHECK(capped.size() > tail.size() && capped.compare(capped.size() - tail.size(), tail.size(), tail) == 0);
    CHECK(capped.find("5000 bytes left out") != std::string::npos);
    const fs::path pref = fresh("bigpref"), out = fresh("bigout");
    write(pref / "logs" / "run-20261109-100000.log", std::string(200000, 'x'));
    b3port::BugReportInput in;
    in.pref = pref;
    in.out_dir = out;
    in.max_file_bytes = 10000;
    auto r = b3port::make_bug_report(in);
    std::string why;
    auto m = read_zip(r.zip, &why);
    CHECK(m.count("logs/run-20261109-100000.log") && m.at("logs/run-20261109-100000.log").data.size() < 10100);
}

#ifndef _WIN32
// ---- native_play's run log (tools/native/run_log.h), in child processes ----
namespace {
// A child: stdout / stderr pointed at `orig` (a file, or /dev/null), the run log started at `log`, lines written;
// `crash`: then a fault whose handler drains the log as crash_report does (g_crash_flush) and _exits; `uncaught`
// (musicexit): instead a std::system_error out of a noexcept function, under b3report/uncaught.h's handler. Its exit code.
[[gnu::noinline]] void join_fails() { throw std::system_error(EINVAL, std::generic_category()); }   // as a failed join()
void throws_through_noexcept() noexcept { join_fails(); }                                          // as in a destructor
int run_child(const fs::path& orig, const fs::path& log, bool crash, bool uncaught = false) {
    std::fflush(stdout);
    std::fflush(stderr);
    const pid_t pid = fork();
    if (pid == 0) {
        const int fd = ::open(orig.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, 1);
        dup2(fd, 2);
        ::close(fd);
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
        if (!b3native::runlog::start(log)) _exit(9);
        std::printf("out line 1\n");
        std::fprintf(stderr, "err line\n");
        for (int i = 0; i < 2000; ++i) std::printf("bulk %d\n", i);   // more than a pipe's buffer
        if (uncaught) {
            b3port::install_uncaught_report("the test child");
            throws_through_noexcept();
        }
        if (crash) {
            std::signal(SIGSEGV, [](int) {
                std::printf("crash: signal 11 (the report)\n");
                std::fflush(stdout);
                b3native::runlog::crash_flush();
                _exit(4);
            });
            std::raise(SIGSEGV);
        }
        std::printf("the end\n");
        std::exit(0);   // atexit: finish()
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}
}  // namespace

TEST(run_log_tee_to_a_file) {
    const fs::path d = fresh("tee");
    CHECK(run_child(d / "orig.txt", d / "run.log", false) == 0);
    const std::string log = slurp(d / "run.log"), orig = slurp(d / "orig.txt");
    for (const std::string& s : {orig, log}) {   // both get everything: stdout and stderr
        CHECK(s.find("out line 1\n") != std::string::npos);
        CHECK(s.find("err line\n") != std::string::npos);
        CHECK(s.find("bulk 1999\n") != std::string::npos);
        CHECK(s.find("the end\n") != std::string::npos);
    }
}

TEST(run_log_direct_when_nowhere) {
    const fs::path d = fresh("direct");
    CHECK(run_child("/dev/null", d / "run.log", false) == 0);   // the launcher's start
    const std::string log = slurp(d / "run.log");
    CHECK(log.find("out line 1\n") != std::string::npos && log.find("err line\n") != std::string::npos);
    CHECK(log.find("the end\n") != std::string::npos);
}

TEST(run_log_keeps_a_crash_report) {
    const fs::path d = fresh("crash");
    CHECK(run_child(d / "orig.txt", d / "run.log", true) == 4);
    const std::string log = slurp(d / "run.log");
    CHECK(log.find("bulk 1999\n") != std::string::npos);
    CHECK(log.find("crash: signal 11 (the report)\n") != std::string::npos);   // drained before the end
    CHECK(slurp(d / "orig.txt").find("crash: signal 11") != std::string::npos);
    CHECK(run_child("/dev/null", d / "run2.log", true) == 4);
    CHECK(slurp(d / "run2.log").find("crash: signal 11 (the report)\n") != std::string::npos);
}

// musicexit: two threads, one on stdout and one on stderr, 20000 lines each through the run log; every line whole in
// the original (a file both streams were pointed at) and in the log
TEST(run_log_two_threads_whole_lines) {
    const fs::path d = fresh("lines");
    const fs::path orig = d / "orig.txt", log = d / "run.log";
    std::fflush(stdout);
    std::fflush(stderr);
    const pid_t pid = fork();
    if (pid == 0) {
        const int fd = ::open(orig.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, 1);
        dup2(fd, 2);
        ::close(fd);
        if (!b3native::runlog::start(log)) _exit(9);
        std::thread other([] {
            for (int i = 0; i < 20000; ++i) std::fprintf(stderr, "[b3live] renderer line %d with some text after it\n", i);
        });
        for (int i = 0; i < 20000; ++i) std::printf("native_play: VBlank %d: game line\n", i);
        other.join();
        std::exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    for (const fs::path& f : {orig, log}) {
        std::ifstream in(f);
        std::string line;
        int game = 0, render = 0, bad = 0;
        while (std::getline(in, line)) {
            const std::string g = "native_play: VBlank ", r = "[b3live] renderer line ";
            if (line.rfind(g, 0) == 0 && line == g + std::to_string(std::atoi(line.c_str() + g.size())) + ": game line") ++game;
            else if (line.rfind(r, 0) == 0 && line == r + std::to_string(std::atoi(line.c_str() + r.size())) + " with some text after it") ++render;
            else ++bad;
        }
        CHECK(game == 20000);
        CHECK(render == 20000);
        CHECK(bad == 0);
    }
}

TEST(uncaught_exception_reported) {   // musicexit: the exception, the thread, frames, exit 4 -- not a bare abort
    const fs::path d = fresh("uncaught");
    CHECK(run_child("/dev/null", d / "run.log", false, true) == 4);
    const std::string log = slurp(d / "run.log");
    CHECK(log.find("bulk 1999\n") != std::string::npos);
    CHECK(log.find("crash: uncaught C++ exception in the test child: std::system_error: Invalid argument (generic 22)\n") !=
          std::string::npos);
    CHECK(log.find("crash: thread \"") != std::string::npos);
    CHECK(log.find("test_report") != std::string::npos);   // a frame (backtrace_symbols: the program's name)
    CHECK(log.find("the end") == std::string::npos);
}
#endif

#ifndef _WIN32
TEST(run_log_line_cut) {   // musicexit: what a reader passes on (whole lines; all at the end or past kLongLine)
    using b3native::runlog::line_cut;
    CHECK(line_cut("", false) == 0);
    CHECK(line_cut("half a li", false) == 0);
    CHECK(line_cut("half a li", true) == 9);
    CHECK(line_cut("one\n", false) == 4);
    CHECK(line_cut("one\ntwo\nthr", false) == 8);
    CHECK(line_cut(std::string(b3native::runlog::kLongLine, 'x'), false) == b3native::runlog::kLongLine);
}
#endif

TEST(uncaught_exception_text) {   // musicexit (include/b3report/uncaught.h)
    CHECK(b3port::uncaught_exception_text().rfind("no active exception", 0) == 0);
    try {
        throw std::system_error(EINVAL, std::generic_category());
    } catch (...) {
        CHECK(b3port::uncaught_exception_text() == "std::system_error: Invalid argument (generic 22)");
    }
    try {
        throw std::runtime_error("the reason");
    } catch (...) {
        CHECK(b3port::uncaught_exception_text() == "std::runtime_error: the reason");
    }
    struct NotStd {};
    try {
        throw NotStd{};
    } catch (...) {
        const std::string t = b3port::uncaught_exception_text();
        CHECK(t.find("NotStd") != std::string::npos && t.find("(not a std::exception)") != std::string::npos);
    }
}

TEST_MAIN("test_report: ZIP writer, run logs, version, bug report")
