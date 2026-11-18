// core.cpp -- see core.h
#include "core.h"

#include "../native/custom_music.h"
#include "../native/save_card.h"
#include "b3asset/discfs.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // osk1: GetLogicalDrives (the browser's drives)
#endif

namespace b3launcher {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::int64_t unix_time(const fs::file_time_type& t) {
    // file_clock -> system_clock (C++20 clock_cast is missing in some standard libraries: the offset at now)
    const auto sys = std::chrono::system_clock::now() +
                     std::chrono::duration_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

}  // namespace

fs::path path_from_utf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string utf8_of(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// ---- launcher.ini ----

std::string config_to_ini(const Config& c) {
    std::ostringstream o;
    o << "# Burnout 3 launcher: its own choices (the game's settings are in pc_settings.ini)\n";
    o << "game_dir = " << c.game_dir << "\n";
    if (!c.saves_dir.empty()) o << "saves_dir = " << c.saves_dir << "\n";   // 1.1 (a default folder: no line, as before)
    update::write_ini(o, c.update);   // update1
    return o.str();
}

Config config_from_ini(std::string_view text) {
    Config c;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = trim(text.substr(at, end - at));
        at = end + 1;
        const std::size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string_view::npos) continue;
        const std::string_view key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        if (key == "game_dir") c.game_dir = std::string(val);
        else if (key == "saves_dir") c.saves_dir = std::string(val);   // 1.1
        else update::read_ini_key(c.update, key, val);   // update1
    }
    return c;
}

bool load_config(const fs::path& path, Config& c) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    c = config_from_ini(ss.str());
    return true;
}

bool save_config(const fs::path& path, const Config& c, std::string* why) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (f) f << config_to_ini(c);
        if (!f) {
            if (why) *why = "cannot write " + tmp.string();
            return false;
        }
    }
    fs::rename(tmp, path, ec);   // replaces the old file (std::filesystem does on Windows too)
    if (ec) {
        if (why) *why = "cannot replace " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

fs::path settings_file(const fs::path& pref) { return pref / "pc_settings.ini"; }
fs::path run_state_file(const fs::path& pref) { return pref / "run_state.ini"; }
fs::path config_file(const fs::path& pref) { return pref / "launcher.ini"; }
fs::path logs_folder(const fs::path& pref) { return pref / "logs"; }
fs::path null_device() {
#ifdef _WIN32
    return fs::path("NUL");
#else
    return fs::path("/dev/null");
#endif
}
fs::path saves_folder(const fs::path& pref) { return pref / "saves"; }
fs::path saves_folder(const fs::path& pref, const std::string& chosen) { return chosen.empty() ? saves_folder(pref) : path_from_utf8(chosen); }

// ---- the game data folder ----

namespace {

std::vector<GameItem> game_items() {
    return {
        {"SLUS_210.50", "the game program", false, true, false},
        {"SYSTEM.CNF", "the disc's start file", false, true, false},
        {"DATA", "the game's data", true, true, false},
        {"TRACKS", "the tracks", true, true, false},
        {"PVEH", "the cars", true, true, false},
        {"SOUND", "sound and music", true, true, false},
        {"OVID", "the videos", true, false, false},
    };
}

// The items of `dir` (no search below it); false when the folder is not there.
// iso1: `dir` may be a disc image (.iso / .bin / chd1: .chd): its root is checked the same way (b3asset/discfs.h)
bool check_items(const fs::path& dir, std::vector<GameItem>& items, std::vector<fs::path>* children = nullptr) {
    if (dir.empty() || !b3asset::disc::is_dir(dir)) return false;
    std::vector<std::pair<std::string, bool>> names;   // lower case name, is a folder
    for (const b3asset::disc::Entry& e : b3asset::disc::list_dir(dir)) {
        names.emplace_back(lower(e.name), e.dir);
        if (e.dir && children) children->push_back(e.path);
    }
    for (GameItem& i : items) {
        const std::string want = lower(i.name);
        i.found = std::any_of(names.begin(), names.end(), [&](const auto& n) { return n.first == want && n.second == i.folder; });
    }
    return true;
}

bool all_required(const std::vector<GameItem>& items) {
    return std::all_of(items.begin(), items.end(), [](const GameItem& i) { return !i.required || i.found; });
}

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& d) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::uint8_t b : d) { h ^= b; h *= 0x100000001b3ull; }
    return h;
}

// A file in the image whose last byte cannot be read (the image ends before it): the first one found, else empty.
std::string truncated_file(const fs::path& dir, int depth = 0) {
    if (depth > 16) return {};
    for (const b3asset::disc::Entry& e : b3asset::disc::list_dir(dir)) {
        if (e.dir) {
            if (std::string t = truncated_file(e.path, depth + 1); !t.empty()) return t;
            continue;
        }
        if (e.size == 0) continue;
        b3asset::disc::File f;
        std::uint8_t last = 0;
        if (!f.open(e.path) || f.read_at(e.size - 1, &last, 1) != 1) return e.path.lexically_relative(dir).generic_string();
    }
    return {};
}

// A disc image file whose primary volume descriptor says the volume is longer than the file (a cut-short copy or
// dump): "<have> of <need> MB", else empty. Cooked (.iso, 2048-byte sectors) and raw (.bin, 2352: mode 1 / mode 2 form 1).
std::string image_short_by(const fs::path& image) {
    if (b3asset::disc::is_chd(image)) return {};   // chd1: its data is compressed (a cut-short CHD: truncated_file, chd_problem)
    std::ifstream f(image, std::ios::binary);
    if (!f) return {};
    std::error_code ec;
    const std::uint64_t have = fs::file_size(image, ec);
    if (ec) return {};
    for (const auto& [stride, off] : {std::pair<std::uint64_t, std::uint64_t>{2048, 0}, {2352, 16}, {2352, 24}}) {
        std::uint8_t pvd[88] = {};
        f.clear();
        f.seekg(std::streamoff(16 * stride + off));
        if (!f.read(reinterpret_cast<char*>(pvd), sizeof pvd) || pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0) continue;
        const std::uint64_t blocks = std::uint64_t(pvd[80]) | std::uint64_t(pvd[81]) << 8 | std::uint64_t(pvd[82]) << 16 | std::uint64_t(pvd[83]) << 24;
        const std::uint64_t need = blocks * stride;
        if (have >= need) return {};
        char b[96];
        std::snprintf(b, sizeof b, "%.0f of %.0f MB", double(have) / 1048576.0, double(need) / 1048576.0);
        return b;
    }
    return {};
}

std::string incomplete_image(const std::string& what) {
    return "The disc image is incomplete (" + what + "): an interrupted copy or dump. Make the image again.";
}

std::string game_data_warning(const fs::path& dir, bool image) {
    if (image) {
        if (const std::string s = image_short_by(dir); !s.empty()) return incomplete_image(s);
        const std::string t = truncated_file(dir);
        if (!t.empty()) return incomplete_image("it ends inside " + t);
    }
    const fs::path prog = b3asset::disc::resolve_ci(dir, "SLUS_210.50");
    const std::int64_t size = prog.empty() ? -1 : b3asset::disc::file_size(prog);
    std::vector<std::uint8_t> d;
    if (size != std::int64_t(kProgramSize) || !b3asset::disc::read_file(prog, d) || d.size() != kProgramSize || fnv1a64(d) != kProgramFnv)
        return "SLUS_210.50 is not the US Burnout 3: Takedown program (another version or region, or a damaged copy): the "
               "port runs only the NTSC-U disc (SLUS-21050).";
    return {};
}

}  // namespace

std::string chd_problem(const fs::path& path) {
    if (b3asset::disc::image_error(path).empty()) return {};
    return "The CHD image cannot be read (damaged, cut short, or a CHD that needs its parent): " + path.string();
}

GameCheck check_game_folder(const fs::path& dir) {
    GameCheck g;
    g.items = game_items();
    std::vector<fs::path> children;
    std::error_code ec;
    const bool file = !dir.empty() && fs::is_regular_file(dir, ec);   // iso1: a disc image
    if (!check_items(dir, g.items, &children)) {
        const std::string short_by = file ? image_short_by(dir) : std::string();   // mac5: a cut-short image
        const std::string chd = file ? chd_problem(dir) : std::string();   // chd1
        g.summary = dir.empty()        ? "No game data folder chosen."
                    : !short_by.empty() ? incomplete_image(short_by)
                    : !chd.empty()      ? chd
                    : file              ? "Not a disc image (ISO 9660) or a folder: " + dir.string()
                                        : "The folder is not there: " + dir.string();
        return g;
    }
    g.exists = true;
    g.ok = all_required(g.items);
    if (g.ok) {
        g.summary = file ? "Every game file the port needs is in this disc image." : "Every game file the port needs is there.";
        g.warning = game_data_warning(dir, file);
        return g;
    }
    // the folder above the game's (an install with game/ inside, the repo's data/game, a disc copy in a subfolder)
    std::sort(children.begin(), children.end());
    for (const fs::path& c : children) {
        for (const fs::path& cand : {c, c / "game"}) {
            std::vector<GameItem> inner = game_items();
            if (check_items(cand, inner) && all_required(inner)) { g.suggestion = cand; break; }
        }
        if (!g.suggestion.empty()) break;
    }
    // iso1: the folder that holds the disc image (.iso / .bin / chd1: .chd)
    if (g.suggestion.empty() && !file) {
        std::vector<fs::path> images;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string ext = lower(it->path().extension().string());
            if ((ext == ".iso" || ext == ".bin" || ext == ".chd") && it->is_regular_file(ec)) images.push_back(it->path());
        }
        std::sort(images.begin(), images.end());
        for (const fs::path& c : images) {
            std::vector<GameItem> inner = game_items();
            if (check_items(c, inner) && all_required(inner)) { g.suggestion = c; break; }
        }
    }
    std::string list;
    for (const GameItem& i : g.items)
        if (i.required && !i.found) list += (list.empty() ? "" : ", ") + i.name;
    g.summary = (file ? "Not the game's disc image: no " : "Not the game's folder: no ") + list + ".";
    if (!g.suggestion.empty()) g.summary += " The game is in " + g.suggestion.lexically_relative(dir).generic_string() + " inside it.";
    return g;
}

std::vector<fs::path> game_folder_candidates(const std::string& remembered, const fs::path& exe_dir, const fs::path& cwd) {
    std::vector<fs::path> c;
    auto add = [&](const fs::path& p) {
        if (p.empty()) return;
        const fs::path n = p.lexically_normal();
        if (std::find(c.begin(), c.end(), n) == c.end()) c.push_back(n);
    };
    if (!remembered.empty()) add(path_from_utf8(remembered));
    if (!exe_dir.empty()) {
        add(exe_dir / "game");
        add(exe_dir / "data" / "game");
        add(exe_dir / ".." / "data" / "game");
    }
    if (!cwd.empty()) add(cwd / "data" / "game");
    return c;
}

fs::path find_game_folder(const std::string& remembered, const fs::path& exe_dir, const fs::path& cwd) {
    for (const fs::path& p : game_folder_candidates(remembered, exe_dir, cwd)) {
        const GameCheck g = check_game_folder(p);
        if (g.ok) return p;
        if (!g.suggestion.empty()) return g.suggestion;
    }
    return {};
}

// ---- starting the game ----

fs::path game_program(const fs::path& exe_dir) {
#ifdef _WIN32
    return exe_dir / "native_play.exe";
#else
    return exe_dir / "native_play";
#endif
}

std::vector<std::string> game_arguments(const std::string& game_dir, bool safe_mode) {
    std::vector<std::string> a = {"--game", game_dir};
    if (safe_mode) a.push_back("--safe-mode");
    return a;
}

std::string windows_command_line(const std::vector<std::string>& argv) {
    std::string out;
    for (std::size_t k = 0; k < argv.size(); ++k) {
        const std::string& a = argv[k];
        if (k) out += ' ';
        if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string::npos) {
            out += a;
            continue;
        }
        out += '"';
        std::size_t slashes = 0;
        for (char ch : a) {
            if (ch == '\\') {
                ++slashes;
                continue;
            }
            if (ch == '"') out.append(slashes * 2 + 1, '\\');   // the backslashes and the quote escaped
            else out.append(slashes, '\\');                    // backslashes not before a quote stay as they are
            slashes = 0;
            out += ch;
        }
        out.append(slashes * 2, '\\');   // before the closing quote
        out += '"';
    }
    return out;
}

// ---- the HD pack folder ----

HdCheck check_hd_folder(const fs::path& dir, std::size_t limit) {
    HdCheck h;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return h;
    h.exists = true;
    std::size_t seen = 0;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (++seen > 4 * limit) {
            h.more = true;
            break;
        }
        const std::string ext = lower(it->path().extension().string());
        if (ext == ".dds" || ext == ".png") {
            if (h.images >= limit) {
                h.more = true;
                break;
            }
            ++h.images;
        }
    }
    return h;
}

// ---- the music folder ----

MusicCheck check_music_folder(const fs::path& dir, std::size_t list) {
    MusicCheck m;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return m;
    m.exists = true;
    const std::vector<std::string> files = b3music::list_audio_files(utf8_of(dir), &m.error);
    m.songs = files.size();
    for (std::size_t k = 0; k < files.size() && k < list; ++k) {
        const b3music::Tags t = b3music::read_tags(files[k]);
        m.first.push_back(MusicSong{files[k], t.title, t.artist, t.album});
    }
    return m;
}

std::vector<b3settings::Row> music_rows(const b3settings::Settings& s, const std::string& status) {
    b3settings::Env env;
    env.music = true;   // native_play's page: the MUSIC section
    env.music_status = status;
    env.profile = 0;
    std::vector<b3settings::Row> out;
    for (const auto& t : b3settings::build_content(s, env).tabs) {
        bool in = false;
        for (const auto& r : t.rows) {
            if (r.kind == b3settings::RowKind::kSection) in = r.label == "MUSIC";
            else if (in) out.push_back(r);
        }
    }
    return out;
}

b3settings::Row playlist_row(const b3settings::Settings& s) {
    for (const b3settings::Row& r : music_rows(s))
        if (r.label == "PLAYLIST") return r;
    return {};
}

b3settings::Row other_music_apps_row(const b3settings::Settings& s, const std::string& now) {
    b3settings::Env env;
    env.music = true;
    env.now_playing = now;
    env.profile = 0;
    for (const auto& t : b3settings::build_content(s, env).tabs)
        for (const auto& r : t.rows)
            if (r.label == "OTHER MUSIC APPS") return r;
    return {};
}

std::string now_playing_status(bool on, bool available, bool reading, bool ok, const std::string& why, const std::string& what, int* level) {
    int l = 0;
    std::string s;
    if (!available) {
        l = 2;
        s = "Not available on this system: macOS has no public way for a game to read what another app is playing.";
    } else if (!on) {
        s = "Off: nothing is read. Choose Show now playing below to see the song another app plays.";
    } else if (!reading) {
        s = "Looking for a music app...";
    } else if (!ok) {
        l = 2;
        s = "Cannot read the system's media controls: " + why + ".";
    } else {
        l = 1;
        s = what == "nothing playing" ? std::string("No music app is playing (start a song in Spotify, a browser or any player).")
                                      : "Now: " + what + ".";
    }
    if (level) *level = l;
    return s;
}

// ---- the save files ----

std::vector<SaveSlot> list_save_slots(const fs::path& root) {
    std::vector<SaveSlot> out;
    const int active = b3native::save::read_active_slot(root);
    for (int n = 1; n <= kSaveSlots; ++n) {
        SaveSlot s;
        s.slot = n;
        s.active = n == active;
        std::error_code ec;
        const fs::path sav = root / ("profile-" + std::to_string(n) + ".sav");
        const fs::path folder = root / ("profile-" + std::to_string(n));
        s.sav = fs::is_regular_file(sav, ec);
        s.legacy = fs::is_directory(folder, ec);
        if (s.sav) {
            s.bytes = fs::file_size(sav, ec);
            if (ec) s.bytes = 0;
            const auto t = fs::last_write_time(sav, ec);
            if (!ec) s.modified = unix_time(t);
        } else if (s.legacy) {
            const auto t = fs::last_write_time(folder, ec);
            if (!ec) s.modified = unix_time(t);
        }
        out.push_back(s);
    }
    return out;
}

std::string format_bytes(std::uintmax_t b) {
    char buf[32];
    if (b < 1024) std::snprintf(buf, sizeof buf, "%u bytes", unsigned(b));
    else if (b < 1024 * 1024) std::snprintf(buf, sizeof buf, "%u KB", unsigned((b + 512) / 1024));
    else std::snprintf(buf, sizeof buf, "%.1f MB", double(b) / (1024.0 * 1024.0));
    return buf;
}

std::string format_time(std::int64_t unix_seconds) {
    const std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

// ---- the last run ----

RunSummary summarize_run(const b3settings::RunState& r, bool have_file) {
    RunSummary s;
    if (!have_file) {
        s.kind = LastRun::kNone;
        s.text = "No run yet.";
        return s;
    }
    if (!r.running) {
        s.kind = LastRun::kClean;
        s.text = "The last run ended cleanly.";
        return s;
    }
    const b3settings::StartDecision d = b3settings::start_run(r, false);
    if (d.safe) {
        s.kind = LastRun::kSafeNext;
        s.text = "The last " + std::to_string(d.now.unclean) + " runs did not end cleanly: the next start uses safe mode.";
    } else {
        s.kind = LastRun::kUnclean;
        s.text = "The last run did not end cleanly (or the game is still running).";
    }
    return s;
}

// ---- launchskip1: the launcher skipped ----

SkipBlock skip_launcher(const SkipInput& in) {
    if (in.launcher_at_start) return SkipBlock::kSetting;
    if (in.launcher_argument) return SkipBlock::kArgument;
    if (in.held) return SkipBlock::kHeld;
    if (!in.game_ok) return SkipBlock::kNoGameData;
    if (in.game_warning) return SkipBlock::kBadGameData;
    if (!in.have_program) return SkipBlock::kNoProgram;
    if (in.last_run == LastRun::kUnclean || in.last_run == LastRun::kSafeNext) return SkipBlock::kLastRun;
    return SkipBlock::kNone;
}

std::string skip_block_text(SkipBlock b) {
    switch (b) {
    case SkipBlock::kNone:
    case SkipBlock::kSetting: return {};
    case SkipBlock::kArgument: return "The launcher opened because it was started with --launcher.";
    case SkipBlock::kHeld: return "The launcher opened because Shift or a controller's Back / Select was held.";
    case SkipBlock::kNoGameData: return "The launcher opened because the game data is not set or is missing.";
    case SkipBlock::kBadGameData: return "The launcher opened because the game data will not run (see Game data below).";
    case SkipBlock::kNoProgram: return "The launcher opened because the game program is not beside it.";
    case SkipBlock::kLastRun: return "The launcher opened because the last run did not end cleanly.";
    }
    return {};
}

// ---- the settings rows ----

std::size_t value_count(const b3settings::Row& r) {
    switch (r.kind) {
    case b3settings::RowKind::kToggle:
    case b3settings::RowKind::kChoice: return r.values.size();
    case b3settings::RowKind::kSlider: return r.levels;
    default: return 0;
    }
}

std::optional<std::size_t> step_value(const b3settings::Row& r, int dir, bool wrap) {
    const std::size_t n = value_count(r);
    if (r.disabled || n < 2 || dir == 0) return std::nullopt;
    const std::size_t v = std::min(r.value, n - 1);
    if (dir < 0) {
        if (v > 0) return v - 1;
        return wrap ? std::optional<std::size_t>(n - 1) : std::nullopt;
    }
    if (v + 1 < n) return v + 1;
    return wrap ? std::optional<std::size_t>(0) : std::nullopt;
}

// ---- the keyboard ----

const char* menu_name(b3native::input::Ds2 c) {
    using D = b3native::input::Ds2;
    switch (c) {
    case D::Select: return "Select";
    case D::L3: return "L3";
    case D::R3: return "R3";
    case D::Start: return "Start";
    case D::Up: return "Up";
    case D::Right: return "Right";
    case D::Down: return "Down";
    case D::Left: return "Left";
    case D::L2: return "L2";
    case D::R2: return "R2";
    case D::L1: return "L1";
    case D::R1: return "R1";
    case D::Triangle: return "Triangle";
    case D::Circle: return "Circle";
    case D::Cross: return "Cross";
    case D::Square: return "Square";
    default: return "";   // the stick: driving only
    }
}

const char* drive_name(b3native::input::Ds2 c) {
    using D = b3native::input::Ds2;
    // input_keys.h: what the game reads while driving
    switch (c) {
    case D::Cross: return "Accelerate";
    case D::Square: return "Brake / reverse";
    case D::R1: return "Boost";
    case D::L1: return "Look back";
    case D::Triangle: return "Camera";
    case D::Circle: return "Horn";
    case D::R2: return "Crashbreaker";
    case D::L2: return "Next music track";
    case D::Start: return "Pause";
    case D::Select: return "Select";
    case D::Left:
    case D::SteerLeft: return "Steer left";
    case D::Right:
    case D::SteerRight: return "Steer right";
    case D::StickUp:
    case D::StickDown:
    case D::Up:
    case D::Down: return "Aftertouch";
    default: return "";
    }
}

std::vector<KeyRow> key_rows(const std::vector<b3native::input::KeyBinding>& map) {
    using b3native::input::KeyCtx;
    std::vector<KeyRow> rows;
    auto add = [](std::string& col, const char* name) {
        if (!*name) return;
        const std::string n = name;
        // a name once per column ("Steer left" from both the D-pad and the stick)
        std::size_t at = 0;
        while (at <= col.size()) {
            std::size_t end = col.find(", ", at);
            if (end == std::string::npos) end = col.size();
            if (col.compare(at, end - at, n) == 0) return;
            if (end == col.size()) break;
            at = end + 2;
        }
        col += (col.empty() ? "" : ", ") + n;
    };
    for (const auto& b : map) {
        auto it = std::find_if(rows.begin(), rows.end(), [&](const KeyRow& r) { return r.scancode == b.scancode; });
        if (it == rows.end()) {
            rows.push_back(KeyRow{b.scancode, {}, {}});
            it = rows.end() - 1;
        }
        if (b.ctx != KeyCtx::Drive) add(it->menus, menu_name(b.control));
        if (b.ctx != KeyCtx::Menu) add(it->driving, drive_name(b.control));
    }
    return rows;
}

b3settings::Content settings_content(const b3settings::Settings& s, const std::string& hd_status, bool hd_has_images,
                                     const std::vector<std::pair<std::string, std::string>>& languages, const std::string& language_status) {
    b3settings::Env env;
    env.languages = languages;                 // 1.0.1: LANGUAGE and IMPORT LANGUAGES
    env.language_import = true;
    env.language_status = language_status;
    env.hd_pack = !s.hd_pack_dir.empty();      // the HD TEXTURES row with a folder chosen
    env.hd_pack_loaded = hd_has_images;        // "(in use)": used from the next start
    env.hd_pack_status = hd_status;
    env.profile = 0;                           // no SAVES tab: the launcher's SAVES page
    b3settings::Content c = b3settings::build_content(s, env);
    b3settings::translate(c, b3settings::ui_language(s, env));   // 1.1: our own text in the player's language
    return c;
}

// ---- osk1: a text field's text and cursor ----

std::size_t utf8_prev(std::string_view s, std::size_t at) {
    at = std::min(at, s.size());
    if (at == 0) return 0;
    --at;
    while (at > 0 && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) --at;
    return at;
}

std::size_t utf8_next(std::string_view s, std::size_t at) {
    if (at >= s.size()) return s.size();
    ++at;
    while (at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) ++at;
    return at;
}

void TextEdit::insert(std::string_view s) {
    cursor = std::min(cursor, text.size());
    text.insert(cursor, s);
    cursor += s.size();
}

bool TextEdit::erase_back() {
    cursor = std::min(cursor, text.size());
    if (cursor == 0) return false;
    const std::size_t from = utf8_prev(text, cursor);
    text.erase(from, cursor - from);
    cursor = from;
    return true;
}

bool TextEdit::erase_forward() {
    cursor = std::min(cursor, text.size());
    if (cursor >= text.size()) return false;
    text.erase(cursor, utf8_next(text, cursor) - cursor);
    return true;
}

void TextEdit::left() { cursor = utf8_prev(text, cursor); }
void TextEdit::right() { cursor = utf8_next(text, cursor); }

std::string clean_pasted_path(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

// ---- osk1: the on-screen keyboard ----

namespace {

// a row of character keys, one UTF-8 character per entry on each layer
void char_row(std::vector<OskKey>& out, int row, float x, const std::vector<std::string>& lo, const std::vector<std::string>& up,
              const std::vector<std::string>& sym) {
    for (std::size_t k = 0; k < lo.size(); ++k) {
        OskKey key;
        key.kind = OskKind::kChar;
        key.chars[0] = lo[k];
        key.chars[1] = up[k];
        key.chars[2] = sym[k];
        key.row = row;
        key.x = x + float(k);
        key.w = 1;
        out.push_back(key);
    }
}

void special(std::vector<OskKey>& out, OskKind kind, int row, float& x, float w) {
    OskKey key;
    key.kind = kind;
    key.row = row;
    key.x = x;
    key.w = w;
    out.push_back(key);
    x += w;
}

std::vector<std::string> chars_of(std::string_view s) {   // ASCII text, one key each
    std::vector<std::string> v;
    for (char c : s) v.emplace_back(1, c);
    return v;
}

}  // namespace

std::vector<OskKey> osk_layout(bool system_key) {
    std::vector<OskKey> k;
    float x = 11;
    char_row(k, 0, 0, chars_of("1234567890-"), chars_of("1234567890-"), chars_of("!@#$%&()=+,"));
    special(k, OskKind::kBackspace, 0, x, 2);
    x = 11;
    char_row(k, 1, 0, chars_of("qwertyuiop_"), chars_of("QWERTYUIOP_"),
             {"[", "]", "{", "}", "'", "\"", "`", ";", "^", "\xC3\xAA", "\xC3\xB4"});
    special(k, OskKind::kClear, 1, x, 2);
    x = 11;
    char_row(k, 2, 0, chars_of("asdfghjkl:\\"), chars_of("ASDFGHJKL:\\"),
             {"\xC3\xA1", "\xC3\xA9", "\xC3\xAD", "\xC3\xB3", "\xC3\xBA", "\xC3\xA0", "\xC3\xA8", "\xC3\xB1", "\xC3\xA7", ":", "\\"});
    special(k, OskKind::kDone, 2, x, 2);
    x = 0;
    special(k, OskKind::kShift, 3, x, 2);
    char_row(k, 3, 2, chars_of("zxcvbnm,./~"), chars_of("ZXCVBNM,./~"),
             {"\xC3\xA4", "\xC3\xB6", "\xC3\xBC", "\xC3\x9F", "\xC3\xB8", "\xC3\xA5", "?", "*", ".", "/", "~"});
    x = 0;
    special(k, OskKind::kSymbols, 4, x, 2);
    special(k, OskKind::kSpace, 4, x, system_key ? 3.0f : 4.5f);
    special(k, OskKind::kLeft, 4, x, 1.5f);
    special(k, OskKind::kRight, 4, x, 1.5f);
    special(k, OskKind::kPaste, 4, x, 1.5f);
    if (system_key) special(k, OskKind::kSystem, 4, x, 2);
    special(k, OskKind::kCancel, 4, x, system_key ? 1.5f : 2.0f);
    return k;
}

std::string osk_label(const OskKey& k, OskLayer layer) {
    switch (k.kind) {
    case OskKind::kChar: return k.chars[std::size_t(layer)];
    case OskKind::kShift: return "Shift";
    case OskKind::kSymbols: return layer == OskLayer::kSymbols ? "abc" : "#+=";
    case OskKind::kSpace: return "Space";
    case OskKind::kBackspace: return "Delete";
    case OskKind::kClear: return "Clear";
    case OskKind::kLeft: return "<";
    case OskKind::kRight: return ">";
    case OskKind::kPaste: return "Paste";
    case OskKind::kSystem: return "System keyboard";
    case OskKind::kCancel: return "Cancel";
    case OskKind::kDone: return "Done";
    }
    return {};
}

int osk_move(const std::vector<OskKey>& keys, int from, int dx, int dy) {
    if (keys.empty()) return -1;
    if (from < 0 || from >= int(keys.size())) return 0;
    const OskKey& f = keys[std::size_t(from)];
    if (dx != 0) {
        std::vector<int> row;
        for (int i = 0; i < int(keys.size()); ++i)
            if (keys[std::size_t(i)].row == f.row) row.push_back(i);
        std::sort(row.begin(), row.end(), [&](int a, int b) { return keys[std::size_t(a)].x < keys[std::size_t(b)].x; });
        const int at = int(std::find(row.begin(), row.end(), from) - row.begin());
        const int n = int(row.size());
        return row[std::size_t(((at + (dx > 0 ? 1 : -1)) % n + n) % n)];
    }
    if (dy == 0) return from;
    int rows = 1;
    for (const OskKey& k : keys) rows = std::max(rows, k.row + 1);
    const int to_row = ((f.row + (dy > 0 ? 1 : -1)) % rows + rows) % rows;
    const float mid = f.x + f.w * 0.5f;
    int best = from;
    float best_d = 1e30f;
    for (int i = 0; i < int(keys.size()); ++i) {
        const OskKey& k = keys[std::size_t(i)];
        if (k.row != to_row) continue;
        // the key under the middle wins; else the nearest middle
        const float d = (mid >= k.x && mid < k.x + k.w) ? -1.0f : std::fabs(k.x + k.w * 0.5f - mid);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

int osk_find(const std::vector<OskKey>& keys, OskKind kind) {
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (keys[i].kind == kind) return int(i);
    return -1;
}

bool osk_find_char(const std::vector<OskKey>& keys, std::string_view ch, int& key, OskLayer& layer) {
    for (OskLayer l : {OskLayer::kLower, OskLayer::kUpper, OskLayer::kSymbols})
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i].kind == OskKind::kChar && keys[i].chars[std::size_t(l)] == ch) {
                key = int(i);
                layer = l;
                return true;
            }
    if (ch == " ") {
        key = osk_find(keys, OskKind::kSpace);
        layer = OskLayer::kLower;
        return key >= 0;
    }
    return false;
}

OskResult osk_press(OskState& st, const std::vector<OskKey>& keys, int key, TextEdit& edit) {
    if (key < 0 || key >= int(keys.size())) return OskResult::kNone;
    const OskKey& k = keys[std::size_t(key)];
    switch (k.kind) {
    case OskKind::kChar:
        edit.insert(k.chars[std::size_t(st.layer)]);
        if (st.layer == OskLayer::kUpper && !st.caps) st.layer = OskLayer::kLower;   // Shift: one character
        break;
    case OskKind::kShift:
        if (st.layer != OskLayer::kUpper) {
            st.layer = OskLayer::kUpper;
            st.caps = false;
        } else if (!st.caps) {
            st.caps = true;   // again: caps lock
        } else {
            st.layer = OskLayer::kLower;
            st.caps = false;
        }
        break;
    case OskKind::kSymbols:
        st.layer = st.layer == OskLayer::kSymbols ? OskLayer::kLower : OskLayer::kSymbols;
        st.caps = false;
        break;
    case OskKind::kSpace: edit.insert(" "); break;
    case OskKind::kBackspace: edit.erase_back(); break;
    case OskKind::kClear: edit.set(""); break;
    case OskKind::kLeft: edit.left(); break;
    case OskKind::kRight: edit.right(); break;
    case OskKind::kPaste: return OskResult::kPaste;
    case OskKind::kSystem: return OskResult::kSystem;
    case OskKind::kCancel: return OskResult::kCancel;
    case OskKind::kDone: return OskResult::kDone;
    }
    return OskResult::kNone;
}

void osk_cycle_layer(OskState& st) {
    st.layer = st.layer == OskLayer::kLower ? OskLayer::kUpper : st.layer == OskLayer::kUpper ? OskLayer::kSymbols : OskLayer::kLower;
    st.caps = false;
}

// ---- osk1: the folder browser ----

namespace {

fs::path tidy(const fs::path& p) {
    fs::path n = p.lexically_normal();
    if (!n.has_filename() && n.has_relative_path()) n = n.parent_path();   // "a/b/" -> "a/b"
    return n;
}

}  // namespace

bool has_extension(const fs::path& p, const std::vector<std::string>& exts) {
    std::string e = lower(utf8_of(p.extension()));
    if (e.size() < 2) return false;
    e.erase(0, 1);
    return std::find(exts.begin(), exts.end(), e) != exts.end();
}

BrowseList list_folder(const fs::path& dir, const std::vector<std::string>& exts, std::size_t limit) {
    BrowseList r;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        r.error = ec.message();
        return r;
    }
    r.ok = true;
    std::vector<BrowseEntry> folders, files;
    for (fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::path p = it->path();
        std::string name = utf8_of(p.filename());
        if (name.empty() || name[0] == '.' || lower(name) == "$recycle.bin" || lower(name) == "system volume information") continue;
        std::error_code e2;
        const bool folder = fs::is_directory(p, e2);   // (a link to a folder counts as one)
        if (e2) continue;
        if (!folder && !(has_extension(p, exts) && fs::is_regular_file(p, e2))) {
            ++r.other_files;
            continue;
        }
        if (folders.size() + files.size() >= limit) {
            r.more = true;
            break;
        }
        (folder ? folders : files).push_back(BrowseEntry{std::move(name), p, folder});
    }
    const auto by_name = [](const BrowseEntry& a, const BrowseEntry& b) {
        const std::string la = lower(a.name), lb = lower(b.name);
        return la != lb ? la < lb : a.name < b.name;
    };
    std::sort(folders.begin(), folders.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);
    r.entries = std::move(folders);
    r.entries.insert(r.entries.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
    return r;
}

fs::path parent_folder(const fs::path& p) {
    const fs::path n = tidy(p);
    return n.has_relative_path() ? n.parent_path() : n;
}

bool is_root_folder(const fs::path& p) { return !tidy(p).has_relative_path(); }

std::vector<BrowsePlace> mounted_media(const std::vector<fs::path>& roots, const std::string& user) {
    std::vector<BrowsePlace> out;
    auto dirs = [](const fs::path& d) {
        std::vector<fs::path> v;
        std::error_code ec;
        for (fs::directory_iterator it(d, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            const std::string n = utf8_of(it->path().filename());
            if (!n.empty() && n[0] != '.' && fs::is_directory(it->path(), e2)) v.push_back(it->path());
        }
        std::sort(v.begin(), v.end());
        return v;
    };
    for (const fs::path& root : roots)
        for (const fs::path& d : dirs(root)) {
            const std::string name = utf8_of(d.filename());
            if (!user.empty() && name == user) {   // /run/media/<user>/<label>
                for (const fs::path& m : dirs(d)) out.push_back(BrowsePlace{utf8_of(m.filename()), m, true});
                continue;
            }
            std::error_code ec;
            fs::directory_iterator probe(d, ec);   // another user's folder cannot be read: left out
            if (!ec) out.push_back(BrowsePlace{name, d, true});
        }
    return out;
}

std::vector<BrowsePlace> browse_places(const std::vector<BrowsePlace>& folders) {
    std::vector<BrowsePlace> out;
    auto add = [&](const BrowsePlace& p) {
        if (p.path.empty()) return;
        std::error_code ec;
        if (!fs::is_directory(p.path, ec)) return;
        const fs::path n = tidy(p.path);
        for (const BrowsePlace& o : out)
            if (tidy(o.path) == n) return;
        out.push_back(BrowsePlace{p.label, n, p.drive});
    };
    for (const BrowsePlace& p : folders) add(p);
#ifdef _WIN32
    const DWORD drives = GetLogicalDrives();   // (not touched: an empty DVD drive is listed too, and says so when opened)
    for (int d = 0; d < 26; ++d)
        if (drives & (DWORD(1) << d)) {
            const std::string root = std::string(1, char('A' + d)) + ":\\";
            out.push_back(BrowsePlace{root.substr(0, 2), fs::path(root), true});
        }
#else
    add(BrowsePlace{"Computer (/)", fs::path("/"), true});
    std::string user;
    if (const char* u = std::getenv("USER"); u && *u) user = u;
    else if (const char* l = std::getenv("LOGNAME"); l && *l) user = l;
    for (const BrowsePlace& m : mounted_media({"/run/media", "/media", "/mnt", "/Volumes"}, user)) add(m);
#endif
    return out;
}

}  // namespace b3launcher
