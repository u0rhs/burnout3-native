// The launcher's logic without SDL (src/b3launcher/core.h, spawn.h; remaster-features §8): its ini, the game data
// folder's check and search, the game's command line, the HD pack check, the save files, the last run's words, the
// settings rows' steps, the Music page (its settings and the game's folder scan), the keyboard table, (POSIX)
// starting a program with its output in a log, and (osk1) a text field's cursor, the on-screen keyboard and the folder
// list.
#include "test.h"

#include "../src/b3launcher/core.h"
#include "../src/b3launcher/gameart.h"
#include "../src/b3launcher/spawn.h"
#include "iso_writer.h"
#include "chd_writer.h"   // chd1
#include "b3settings/page.h"   // chd1: translate_text

#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>

using namespace b3launcher;

namespace {

const fs::path kTmp = fs::path("build") / "test_launcher_tmp";

fs::path fresh(const std::string& name) {
    const fs::path d = kTmp / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

void touch(const fs::path& p, std::size_t bytes = 1) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << std::string(bytes, 'x');
}

// a disc folder with the files the check looks for
void make_disc(const fs::path& d, bool lower = false) {
    auto n = [&](std::string s) {
        if (lower)
            for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    touch(d / n("SLUS_210.50"));
    touch(d / n("SYSTEM.CNF"));
    for (const char* f : {"DATA", "TRACKS", "PVEH", "SOUND", "OVID"}) fs::create_directories(d / n(f));
}

const b3settings::Row* find_row(const b3settings::Content& c, const std::string& label) {
    for (const auto& t : c.tabs)
        for (const auto& r : t.rows)
            if (r.label == label) return &r;
    return nullptr;
}

}  // namespace

TEST(config_ini_round_trip) {
    Config c;
    c.game_dir = "/home/me/Burnout 3/game";
    CHECK(config_from_ini(config_to_ini(c)) == c);
    const Config d = config_from_ini("# x\n  game_dir =  C:\\Games\\B3 \r\nunknown = 1\nbad line\n");
    CHECK(d.game_dir == "C:\\Games\\B3");
    CHECK(config_from_ini("").game_dir.empty());
    // 1.1: the chosen save folder; none: no line (the file as before)
    CHECK(config_to_ini(c).find("saves_dir") == std::string::npos && config_from_ini(config_to_ini(c)).saves_dir.empty());
    Config sd = c;
    sd.saves_dir = "/cloud/My Drive/B3";
    CHECK(config_from_ini(config_to_ini(sd)) == sd);
    // update1: the update check's keys beside game_dir (update.h), on by default
    CHECK(config_from_ini("").update.enabled && config_from_ini("update_check = 0\n").update.enabled == false);
    Config u = c;
    u.update.checked_at = 1800000000;
    u.update.latest.tag = "v0.2.0";
    u.update.latest.name = "Takedown at 60";
    u.update.latest.url = "https://github.com/u0rhs/burnout3-native/releases/tag/v0.2.0";
    u.update.latest.notes = {"What's new", "Split screen"};
    u.update.error = "timed out";
    CHECK(config_from_ini(config_to_ini(u)) == u);
    // issue #8: ADD TO STEAM's own pictures; none chosen: no lines
    CHECK(config_to_ini(c).find("steam_") == std::string::npos);
    Config st = c;
    st.steam_cover = "/home/me/Pictures/my cover.jpg";
    st.steam_hero = "C:\\Users\\me\\Pictures\\hero.png";
    CHECK(config_from_ini(config_to_ini(st)) == st && config_from_ini(config_to_ini(st)).steam_logo.empty());
    st.steam_logo = "/l.png";
    CHECK(config_from_ini(config_to_ini(st)) == st);
    const fs::path dir = fresh("config");
    Config e;
    CHECK(!load_config(dir / "launcher.ini", e));
    CHECK(save_config(dir / "launcher.ini", c));
    CHECK(save_config(dir / "launcher.ini", c));   // replaces
    CHECK(load_config(dir / "launcher.ini", e) && e == c);
    CHECK(!fs::exists(dir / "launcher.ini.tmp"));
}

TEST(files_beside_the_settings) {
    const fs::path p = "/u/b3port/native";
    CHECK(settings_file(p) == p / "pc_settings.ini");
    CHECK(run_state_file(p) == p / "run_state.ini");
    CHECK(config_file(p) == p / "launcher.ini");
    CHECK(saves_folder(p) == p / "saves");
    CHECK(saves_folder(p, "") == p / "saves" && saves_folder(p, "/cloud/B3") == fs::path("/cloud/B3"));   // 1.1
    CHECK(logs_folder(p) == p / "logs");
#ifndef _WIN32
    CHECK(null_device() == "/dev/null");
#endif
    CHECK(utf8_of(path_from_utf8("caf\xC3\xA9/x")) == "caf\xC3\xA9/x");
}

TEST(game_folder_check) {
    const fs::path d = fresh("disc");
    GameCheck g = check_game_folder(d);
    CHECK(g.exists && !g.ok);
    CHECK(g.summary.find("SLUS_210.50") != std::string::npos);
    make_disc(d);
    g = check_game_folder(d);
    CHECK(g.ok);
    for (const auto& i : g.items) CHECK(i.found);
    // OVID is optional
    fs::remove_all(d / "OVID");
    g = check_game_folder(d);
    CHECK(g.ok);
    // a file where a folder should be is not it
    fs::remove_all(d / "TRACKS");
    touch(d / "TRACKS");
    g = check_game_folder(d);
    CHECK(!g.ok && g.summary.find("TRACKS") != std::string::npos);
    // lower case names (a disc copied on Linux)
    const fs::path l = fresh("disc_lower");
    make_disc(l, true);
    CHECK(check_game_folder(l).ok);
    // not there / none
    CHECK(!check_game_folder(kTmp / "nope").exists);
    CHECK(!check_game_folder(fs::path()).exists);
}

TEST(game_folder_above_the_disc_is_suggested) {
    const fs::path top = fresh("install");
    make_disc(top / "game");
    GameCheck g = check_game_folder(top);
    CHECK(!g.ok && g.suggestion == top / "game");
    const fs::path repo = fresh("repo");
    make_disc(repo / "data" / "game");
    g = check_game_folder(repo);
    CHECK(!g.ok && g.suggestion == repo / "data" / "game");
}

// iso1: the disc image itself (an ISO 9660 volume, include/b3asset/discfs.h) is checked as the folder is
TEST(game_disc_image_check) {
    const fs::path d = fresh("image");
    isotest::Writer w;
    w.file("SLUS_210.50", {1, 2, 3});
    w.file("SYSTEM.CNF", {'B', 'O', 'O', 'T', '2'});
    for (const char* f : {"DATA/GLOBAL.TXD", "TRACKS/EU/C1_V1/STATIC.DAT", "PVEH/COMP/CAR1.BGV", "SOUND/X.BIN"}) w.file(f, {9});
    CHECK(w.write((d / "Burnout 3 (USA).iso").string()));
    GameCheck g = check_game_folder(d / "Burnout 3 (USA).iso");
    CHECK(g.exists && g.ok);
    CHECK(g.summary.find("disc image") != std::string::npos);
    for (const auto& i : g.items) CHECK(i.found == (i.name != "OVID"));
    CHECK(find_ci(d / "Burnout 3 (USA).iso", "data/global.txd") == d / "Burnout 3 (USA).iso" / "DATA" / "GLOBAL.TXD");
    // the folder that holds the image: suggested
    g = check_game_folder(d);
    CHECK(!g.ok && g.suggestion == d / "Burnout 3 (USA).iso");
    // another disc's image: not it
    isotest::Writer other;
    other.file("SLES_123.45", {1});
    other.dir("DATA");
    CHECK(other.write((d / "other.iso").string()));
    g = check_game_folder(d / "other.iso");
    CHECK(g.exists && !g.ok && g.summary.find("Not the game's disc image") != std::string::npos);
    // a file that is no image
    touch(d / "notes.txt", 100);
    g = check_game_folder(d / "notes.txt");
    CHECK(!g.exists && g.summary.find("Not a disc image") != std::string::npos);
    CHECK(game_arguments((d / "x.iso").string(), false) == (std::vector<std::string>{"--game", (d / "x.iso").string()}));
}

// chd1: a CHD of the disc image (tests/chd_writer.h) is checked as the .iso is; a damaged one says so
TEST(game_disc_chd_check) {
    const fs::path d = fresh("chd");
    isotest::Writer w;
    w.file("SLUS_210.50", {1, 2, 3});
    w.file("SYSTEM.CNF", {'B', 'O', 'O', 'T', '2'});
    for (const char* f : {"DATA/GLOBAL.TXD", "TRACKS/EU/C1_V1/STATIC.DAT", "PVEH/COMP/CAR1.BGV", "SOUND/X.BIN"}) w.file(f, {9});
    CHECK(w.write((d / "disc.iso.tmp").string()));
    std::vector<std::uint8_t> iso;
    {
        std::ifstream f(d / "disc.iso.tmp", std::ios::binary);
        iso.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    fs::remove(d / "disc.iso.tmp");
    chdtest::Options o;
    o.codec = chdtest::Codec::kZlib;
    const chdtest::Image img = chdtest::write(iso, o);
    auto put = [](const fs::path& p, const std::vector<std::uint8_t>& b) {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    };
    put(d / "Burnout 3 (USA).chd", img.bytes);
    GameCheck g = check_game_folder(d / "Burnout 3 (USA).chd");
    CHECK(g.exists && g.ok && g.summary.find("disc image") != std::string::npos);
    CHECK(g.warning.find("incomplete") == std::string::npos);   // (SLUS_210.50 is not the retail one: that warning only)
    // the folder that holds it: suggested
    g = check_game_folder(d);
    CHECK(!g.ok && g.suggestion == d / "Burnout 3 (USA).chd");
    // damaged (the volume descriptor's hunk): named as a CHD that cannot be read, in the player's language too
    std::vector<std::uint8_t> bad = img.bytes;
    CHECK(img.hunk_at.size() > 8 && img.hunk_at[8] != 0);
    bad[std::size_t(img.hunk_at[8]) + 2] ^= 0x20;   // 4096-byte hunks: sector 16 is in hunk 8
    put(d / "bad.chd", bad);
    g = check_game_folder(d / "bad.chd");
    CHECK(!g.exists && g.summary.find("The CHD image cannot be read") == 0 && g.summary.find((d / "bad.chd").string()) != std::string::npos);
    CHECK(chd_problem(d / "Burnout 3 (USA).chd").empty() && !chd_problem(d / "bad.chd").empty());
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"}) {
        const std::string t = b3settings::translate_text(g.summary, code);
        CHECK(t.find("cannot be read") == std::string::npos && t.find((d / "bad.chd").string()) != std::string::npos);
        CHECK(b3settings::translate_text("A .chd image of the disc works too.", code) != "A .chd image of the disc works too.");
        CHECK(b3settings::translate_text("Disc images (*.iso, *.bin, *.chd)", code).find("*.chd") != std::string::npos);
    }
}

// mac5 (integrity): a program that is not the retail one, an image cut short
TEST(game_data_integrity_warning) {
    const fs::path d = fresh("integrity");
    make_disc(d);
    GameCheck g = check_game_folder(d);
    CHECK(g.ok && g.warning.find("not the US Burnout 3") != std::string::npos);
    isotest::Writer w;
    w.file("SLUS_210.50", {1, 2, 3});
    w.file("SYSTEM.CNF", {'B', 'O', 'O', 'T', '2'});
    for (const char* f : {"DATA/GLOBAL.TXD", "TRACKS/EU/C1_V1/STATIC.DAT", "PVEH/COMP/CAR1.BGV"}) w.file(f, {9});
    w.file("SOUND/ZZ.BIN", std::vector<std::uint8_t>(20000, 7));   // the image's last file
    const fs::path iso = d / "cut.iso";
    CHECK(w.write(iso.string()));
    g = check_game_folder(iso);
    CHECK(g.ok && g.warning.find("not the US Burnout 3") != std::string::npos);
    // cut 8 KB off the end: SOUND/ZZ.BIN's last sector is gone (a fresh copy: the image is cached per path)
    const fs::path cut = d / "cut2.iso";
    fs::copy_file(iso, cut);
    fs::resize_file(cut, fs::file_size(cut) - 8192);
    g = check_game_folder(cut);
    // either way it is named for what it is (here the directory records went with the end: no tree to check)
    CHECK(g.summary.find("incomplete") != std::string::npos || g.warning.find("incomplete") != std::string::npos);
    CHECK(g.summary.find("MB") != std::string::npos || g.warning.find("MB") != std::string::npos);
    const char* real = std::getenv("B3_TEST_ISO");
    if (real && *real) CHECK(check_game_folder(real).ok && check_game_folder(real).warning.empty());
}

// B3_TEST_ISO=<the retail .iso>: the launcher's art from the disc image, as from the folder
TEST(game_art_from_the_disc_image) {
    const char* iso = std::getenv("B3_TEST_ISO");
    if (!iso || !*iso) { std::printf("  (B3_TEST_ISO not set: skipped)\n"); return; }
    CHECK(check_game_folder(iso).ok);
    const GameArt a = load_game_art(iso);
    CHECK(a.ok());
    CHECK(a.font[kGlobalFont].glyphs.size() == 166 && a.font[kHeadFont].glyphs.size() == 30 && a.font[kSmallFont].glyphs.size() == 166);
    for (const char* n : {"B3Logo", "grid", "titlescreen_logo", "Halo"}) CHECK(a.image(n) != nullptr);
}

TEST(game_folder_candidates_and_search) {
    const fs::path exe = fresh("exe");
    const fs::path cwd = fresh("cwd");
    const auto c = game_folder_candidates("/x/remembered", exe, cwd);
    CHECK(c.size() >= 5);
    CHECK(c[0] == fs::path("/x/remembered"));
    CHECK(c[1] == (exe / "game").lexically_normal());
    CHECK(c[2] == (exe / "data" / "game").lexically_normal());
    CHECK(c[3] == (exe / ".." / "data" / "game").lexically_normal());
    CHECK(c[4] == (cwd / "data" / "game").lexically_normal());
    CHECK(find_game_folder("", exe, cwd).empty());
    make_disc(cwd / "data" / "game");
    CHECK(find_game_folder("", exe, cwd) == (cwd / "data" / "game").lexically_normal());
    make_disc(exe / "game");
    CHECK(find_game_folder("", exe, cwd) == (exe / "game").lexically_normal());   // beside the launcher first
    // the remembered folder above a disc: the disc inside it
    const fs::path top = fresh("remembered");
    make_disc(top / "game");
    CHECK(find_game_folder(top.string(), fs::path(), fs::path()) == top / "game");
}

TEST(game_command_line) {
#ifdef _WIN32
    CHECK(game_program("C:/b3").filename() == "native_play.exe");
#else
    CHECK(game_program("/opt/b3") == fs::path("/opt/b3/native_play"));
#endif
    CHECK((game_arguments("/g", false) == std::vector<std::string>{"--game", "/g"}));
    CHECK((game_arguments("/g", true) == std::vector<std::string>{"--game", "/g", "--safe-mode"}));
    CHECK(windows_command_line({"a.exe", "--game", "C:\\Games\\B3"}) == "a.exe --game C:\\Games\\B3");
    CHECK(windows_command_line({"C:\\Program Files\\b3\\native_play.exe", "--game", "D:\\My Games\\B3\\"}) ==
          "\"C:\\Program Files\\b3\\native_play.exe\" --game \"D:\\My Games\\B3\\\\\"");
    CHECK(windows_command_line({"x", ""}) == "x \"\"");
    CHECK(windows_command_line({"x", "say \"hi\""}) == "x \"say \\\"hi\\\"\"");
    CHECK(windows_command_line({"x", "a\\\"b"}) == "x \"a\\\\\\\"b\"");
}

TEST(hd_folder_check) {
    const fs::path d = fresh("hd");
    CHECK(!check_hd_folder(kTmp / "nope").exists);
    HdCheck h = check_hd_folder(d);
    CHECK(h.exists && h.images == 0);
    touch(d / "SLUS-21050" / "replacements" / "a.DDS");
    touch(d / "SLUS-21050" / "replacements" / "b.png");
    touch(d / "SLUS-21050" / "replacements" / "c.txt");
    h = check_hd_folder(d);
    CHECK(h.images == 2 && !h.more);
    for (int k = 0; k < 5; ++k) touch(d / ("more" + std::to_string(k) + ".png"));
    h = check_hd_folder(d, 3);
    CHECK(h.images == 3 && h.more);
}

TEST(save_slots) {
    const fs::path root = fresh("saves");
    auto s = list_save_slots(root);
    CHECK(s.size() == 5);
    CHECK(s[0].active && !s[1].active);   // no active-slot.txt: file 1
    for (const auto& x : s) CHECK(!x.sav && !x.legacy && !x.modified);
    touch(root / "profile-2.sav", 61900);
    fs::create_directories(root / "profile-4" / "BASLUS-21050");
    { std::ofstream f(root / "active-slot.txt"); f << "2\n"; }
    s = list_save_slots(root);
    CHECK(s[1].active && s[1].sav && s[1].bytes == 61900 && s[1].modified);
    CHECK(s[3].legacy && !s[3].sav && s[3].modified);
    CHECK(!s[0].active);
    // the age of a fresh file: now
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    CHECK(*s[1].modified > now - 120 && *s[1].modified < now + 120);
    CHECK(format_bytes(500) == "500 bytes");
    CHECK(format_bytes(61900) == "60 KB");
    CHECK(format_bytes(3 * 1024 * 1024 / 2) == "1.5 MB");
    CHECK(format_time(0).size() == 16);
}

TEST(last_run_words) {
    b3settings::RunState r;
    CHECK(summarize_run(r, false).kind == LastRun::kNone);
    CHECK(summarize_run(r, true).kind == LastRun::kClean);
    r.running = true;
    r.unclean = 0;
    CHECK(summarize_run(r, true).kind == LastRun::kUnclean);
    r.unclean = 1;   // this one plus the one before: the next start is safe (safemode.h start_run)
    const RunSummary s = summarize_run(r, true);
    CHECK(s.kind == LastRun::kSafeNext && s.text.find("2 runs") != std::string::npos);
    CHECK(b3settings::start_run(r, false).safe);
}

TEST(skip_the_launcher) {
    // launchskip1: OPEN THE LAUNCHER AT START off starts the game at once, unless an exception holds (the first one
    // found is the reason); on (the default), the launcher shows
    SkipInput go;
    go.launcher_at_start = false;
    go.game_ok = true;
    go.have_program = true;
    go.last_run = LastRun::kClean;
    CHECK(skip_launcher(go) == SkipBlock::kNone);
    CHECK(skip_block_text(SkipBlock::kNone).empty() && skip_block_text(SkipBlock::kSetting).empty());
    SkipInput none = go;
    none.last_run = LastRun::kNone;   // no run yet: nothing went wrong
    CHECK(skip_launcher(none) == SkipBlock::kNone);
    CHECK(skip_launcher(SkipInput{}) == SkipBlock::kSetting);   // the default: the launcher shows
    auto with = [&](auto change) {
        SkipInput in = go;
        change(in);
        return skip_launcher(in);
    };
    CHECK(with([](SkipInput& in) { in.launcher_at_start = true; }) == SkipBlock::kSetting);
    CHECK(with([](SkipInput& in) { in.launcher_argument = true; }) == SkipBlock::kArgument);
    CHECK(with([](SkipInput& in) { in.held = true; }) == SkipBlock::kHeld);
    CHECK(with([](SkipInput& in) { in.game_ok = false; }) == SkipBlock::kNoGameData);
    CHECK(with([](SkipInput& in) { in.game_warning = true; }) == SkipBlock::kBadGameData);
    CHECK(with([](SkipInput& in) { in.have_program = false; }) == SkipBlock::kNoProgram);
    CHECK(with([](SkipInput& in) { in.last_run = LastRun::kUnclean; }) == SkipBlock::kLastRun);
    CHECK(with([](SkipInput& in) { in.last_run = LastRun::kSafeNext; }) == SkipBlock::kLastRun);
    for (SkipBlock b : {SkipBlock::kArgument, SkipBlock::kHeld, SkipBlock::kNoGameData, SkipBlock::kBadGameData, SkipBlock::kNoProgram,
                        SkipBlock::kLastRun})
        CHECK(skip_block_text(b).find("The launcher opened because") == 0);
    // the setting is the game's (pc_settings.ini launcher_at_start), on by default
    CHECK(b3settings::Settings{}.launcher_at_start);
}

TEST(row_steps) {
    b3settings::Row c;
    c.kind = b3settings::RowKind::kChoice;
    c.values = {"a", "b", "c"};
    c.value = 0;
    CHECK(!step_value(c, -1, false));
    CHECK(*step_value(c, -1, true) == 2);
    CHECK(*step_value(c, +1, false) == 1);
    c.value = 2;
    CHECK(!step_value(c, +1, false) && *step_value(c, +1, true) == 0);
    c.disabled = true;
    CHECK(!step_value(c, +1, true));
    b3settings::Row s;
    s.kind = b3settings::RowKind::kSlider;
    s.levels = 11;
    s.value = 5;
    CHECK(value_count(s) == 11 && *step_value(s, +1, false) == 6 && *step_value(s, -1, false) == 4);
    b3settings::Row sec;
    sec.kind = b3settings::RowKind::kSection;
    CHECK(!step_value(sec, 1, true));
}

TEST(settings_rows_are_the_games) {
    b3settings::Settings st;
    b3settings::Content c = settings_content(st, "", false);
    // the in-game tabs without SAVES (the launcher's page)
    CHECK(c.tabs.size() == 5);
    for (const auto& t : c.tabs) CHECK(t.label != "SAVES");
    CHECK(find_row(c, "HD TEXTURE PACK") && !find_row(c, "REMOVE HD PACK") && !find_row(c, "HD TEXTURES"));
    st.hd_pack_dir = "/packs/b3";
    c = settings_content(st, "Folder chosen.", true);
    CHECK(find_row(c, "REMOVE HD PACK") && find_row(c, "HD TEXTURES"));
    CHECK(find_row(c, "HD TEXTURE PACK")->description.find("Folder chosen.") != std::string::npos);
    // an edit through the row's step: the in-game rule, and the file the game reads
    const b3settings::Row* vs = find_row(c, "VSYNC");
    CHECK(vs && vs->value == 0);
    b3settings::Edit e;
    e.id = vs->id;
    e.value = *step_value(*vs, +1, true);
    CHECK(b3settings::apply_edit(st, e) && st.vsync);
    const fs::path dir = fresh("settings");
    CHECK(b3settings::save_file((dir / "pc_settings.ini").string(), st));
    b3settings::Settings back;
    CHECK(b3settings::load_file((dir / "pc_settings.ini").string(), back) && back == st && back.hd_pack_dir == "/packs/b3");
}

// ---- the Music page (the custom soundtrack: Settings::music_folder / playlist_mode, src/native/custom_music.h) ----

namespace {

// a tiny 16-bit PCM WAV (mono, 8 kHz, a few ms of a ramp)
void write_wav(const fs::path& p, unsigned frames = 80) {
    fs::create_directories(p.parent_path());
    std::string b;
    auto le = [&](std::uint32_t v, int n) { for (int k = 0; k < n; ++k) b += char((v >> (8 * k)) & 0xFF); };
    b += "RIFF";
    le(36 + frames * 2, 4);
    b += "WAVEfmt ";
    le(16, 4); le(1, 2); le(1, 2); le(8000, 4); le(16000, 4); le(2, 2); le(16, 2);
    b += "data";
    le(frames * 2, 4);
    for (unsigned k = 0; k < frames; ++k) le(std::uint32_t(std::int16_t(k * 100)) & 0xFFFF, 2);
    std::ofstream f(p, std::ios::binary);
    f << b;
}

const b3settings::Row* music_row(const std::vector<b3settings::Row>& rows, const std::string& label) {
    for (const auto& r : rows)
        if (r.label == label) return &r;
    return nullptr;
}

}  // namespace

TEST(music_rows_are_the_games) {
    b3settings::Settings st;
    std::vector<b3settings::Row> rows = music_rows(st);
    CHECK(music_row(rows, "MUSIC FOLDER") && !music_row(rows, "REMOVE MUSIC FOLDER"));
    const b3settings::Row pl = playlist_row(st);
    CHECK(pl.label == "PLAYLIST" && pl.kind == b3settings::RowKind::kChoice && !pl.disabled);
    CHECK((pl.values == std::vector<std::string>{"EA Trax only", "Custom only", "Mixed"}));
    CHECK(pl.value == 0);   // the default: the game's music untouched (the feature off)
    const b3settings::Row cv = custom_music_volume_row(st);   // musicvol1: the game's row, the same ini key
    CHECK(cv.label == "CUSTOM MUSIC VOLUME" && cv.kind == b3settings::RowKind::kSlider && cv.levels == 21 && cv.value == 10);
    st.music_folder = "/music/mine";
    rows = music_rows(st, "3 songs found.");
    CHECK(music_row(rows, "REMOVE MUSIC FOLDER") && music_row(rows, "PLAYLIST"));
    CHECK(music_row(rows, "MUSIC FOLDER")->values[0] == "mine");
    CHECK(music_row(rows, "MUSIC FOLDER")->description.find("3 songs found.") != std::string::npos);
    // the launcher's SETTINGS tabs leave the section to the Music page
    CHECK(!find_row(settings_content(st, "", false), "PLAYLIST"));
}

TEST(music_settings_round_trip) {
    const fs::path pref = fresh("music_pref");
    const std::string folder = utf8_of(pref / u8"My Músic = Punk");
    // the launcher's edits: the folder (a dialog's path), then PLAYLIST stepped on twice as its page steps a choice row,
    // saved to the file the game reads
    b3settings::Settings ls;
    ls.music_folder = folder;
    for (int k = 0; k < 2; ++k) {
        const b3settings::Row r = playlist_row(ls);
        b3settings::Edit e;
        e.id = r.id;
        e.value = *step_value(r, +1, true);
        CHECK(b3settings::apply_edit(ls, e));
    }
    CHECK(ls.playlist_mode == 2);
    CHECK(b3settings::save_file(utf8_of(settings_file(pref)), ls));
    // the game's reader: pc_settings.ini through b3settings (native_play's page), its MUSIC rows
    b3settings::Settings gs;
    CHECK(b3settings::load_file(utf8_of(settings_file(pref)), gs));
    CHECK(gs.music_folder == folder && gs.playlist_mode == 2 && gs == ls);
    b3settings::Env env;
    env.music = true;
    const b3settings::Content c = b3settings::build_content(gs, env);
    CHECK(find_row(c, "PLAYLIST") && find_row(c, "PLAYLIST")->value == 2);
    CHECK(find_row(c, "MUSIC FOLDER") && find_row(c, "MUSIC FOLDER")->values[0] == "My Músic = Punk");
    // and back: the game's page steps PLAYLIST to Custom only and saves; the launcher reads it
    b3settings::Edit e;
    e.id = find_row(c, "PLAYLIST")->id;
    e.value = 1;
    CHECK(b3settings::apply_edit(gs, e) && gs.playlist_mode == 1);
    CHECK(b3settings::save_file(utf8_of(settings_file(pref)), gs));
    b3settings::Settings back;
    CHECK(b3settings::load_file(utf8_of(settings_file(pref)), back) && back == gs);
    CHECK(playlist_row(back).value == 1 && back.music_folder == folder);
    // REMOVE MUSIC FOLDER: the empty folder, saved
    back.music_folder.clear();
    CHECK(b3settings::save_file(utf8_of(settings_file(pref)), back));
    b3settings::Settings cleared;
    CHECK(b3settings::load_file(utf8_of(settings_file(pref)), cleared) && cleared.music_folder.empty() && cleared.playlist_mode == 1);
}

TEST(other_music_apps_on_the_music_page) {
    // nowplaying1: the in-game OTHER MUSIC APPS row (the same values and words), its edits saved for the game, and the
    // page's "Detected" line
    const fs::path pref = fresh("now_playing_pref");
    b3settings::Settings ls;
    b3settings::Row r = other_music_apps_row(ls);
    CHECK(r.label == "OTHER MUSIC APPS" && r.id == 91 && r.value == 0);
#if !defined(__APPLE__)
    CHECK(!r.disabled);
    b3settings::Edit e;
    e.id = r.id;
    e.value = *step_value(r, +1, true);
    CHECK(b3settings::apply_edit(ls, e) && ls.other_music_apps == 1);
    CHECK(b3settings::save_file(utf8_of(settings_file(pref)), ls));
    b3settings::Settings gs;
    CHECK(b3settings::load_file(utf8_of(settings_file(pref)), gs) && gs.other_music_apps == 1 && gs == ls);
    r = other_music_apps_row(gs, "Spotify: Band - Song, playing");
    CHECK(r.value == 1 && r.description.find("Now: Spotify: Band - Song, playing.") != std::string::npos);
#else
    CHECK(r.disabled);
#endif
    int level = -1;
    CHECK(now_playing_status(false, true, false, false, "", "", &level).rfind("Off: nothing is read", 0) == 0 && level == 0);
    CHECK(now_playing_status(true, true, false, false, "off", "", &level).rfind("Looking", 0) == 0 && level == 0);
    CHECK(now_playing_status(true, true, true, false, "no session D-Bus", "", &level).find("no session D-Bus") != std::string::npos && level == 2);
    CHECK(now_playing_status(true, true, true, true, "", "nothing playing", &level).rfind("No music app is playing", 0) == 0 && level == 1);
    CHECK(now_playing_status(true, true, true, true, "", "Spotify: Band - Song, playing", &level) == "Now: Spotify: Band - Song, playing." && level == 1);
    CHECK(now_playing_status(true, false, false, false, "", "", &level).find("macOS") != std::string::npos && level == 2);
}

TEST(music_folder_scan) {
    const fs::path d = fresh("music");
    CHECK(!check_music_folder(kTmp / "nope").exists);
    MusicCheck m = check_music_folder(d);
    CHECK(m.exists && m.songs == 0 && m.first.empty());
    write_wav(d / "Album A" / "01 Band One - First.wav");
    write_wav(d / "Sub" / u8"Déeper" / "Second.WAV");   // a subfolder's, the extension in any case
    touch(d / "notes.txt");
    touch(d / "Album A" / "cover.jpg");
    m = check_music_folder(d);
    CHECK(m.exists && m.songs == 2 && m.first.size() == 2 && m.error.empty());
    if (m.first.size() == 2) {   // the game's order (by path) and its names (the file name's, the folder as the album)
        CHECK(m.first[0].artist == "Band One" && m.first[0].title == "First" && m.first[0].album == "Album A");
        CHECK(m.first[1].title == "Second" && m.first[1].album == "Déeper");
    }
    m = check_music_folder(d, 1);
    CHECK(m.songs == 2 && m.first.size() == 1);
}

TEST(keyboard_table) {
    using namespace b3native::input;
    const auto rows = key_rows(default_key_map());
    auto row = [&](std::uint16_t sc) -> const KeyRow* {
        for (const auto& r : rows)
            if (r.scancode == sc) return &r;
        return nullptr;
    };
    CHECK(rows.size() == 25);   // fe1: V (Square in menus)
    CHECK(row(sc::V) && row(sc::V)->menus == "Square" && row(sc::V)->driving.empty());
    CHECK(row(sc::Left) && row(sc::Left)->menus == "Left" && row(sc::Left)->driving == "Steer left");
    CHECK(row(sc::W) && row(sc::W)->menus == "Up" && row(sc::W)->driving == "Accelerate, Aftertouch");
    CHECK(row(sc::Space) && row(sc::Space)->menus == "Cross" && row(sc::Space)->driving == "Boost");
    CHECK(row(sc::Escape) && row(sc::Escape)->menus == "Start" && row(sc::Escape)->driving == "Pause");
    CHECK(row(sc::Return) && row(sc::Return)->driving == "Accelerate");
    CHECK(rows[0].scancode == sc::Left);   // the map's order
}

#ifndef _WIN32
TEST(spawn_with_a_log) {
    const fs::path dir = fresh("spawn");
    std::string err;
    auto c = spawn("/bin/sh", {"-c", "echo out; echo err 1>&2; pwd; exit 3"}, dir, dir / "run.log", &err);
    CHECK(c.has_value());
    std::optional<int> code;
    for (int k = 0; k < 500 && !code; ++k) {
        code = poll_exit(*c);
        if (!code) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(code && *code == 3);
    std::ifstream f(dir / "run.log");
    std::string all((std::istreambuf_iterator<char>(f)), {});
    CHECK(all.find("out\n") != std::string::npos && all.find("err\n") != std::string::npos);
    CHECK(all.find(fs::absolute(dir).filename().string()) != std::string::npos);   // ran in `dir`
    CHECK(fs::current_path().filename() != dir.filename());                         // the launcher's own folder kept
    // a missing program: an error at once (glibc), or a child that ends with 127 (other C libraries)
    err.clear();
    auto bad = spawn(dir / "no-such-program", {}, dir, dir / "x.log", &err);
    if (bad) {
        std::optional<int> b;
        for (int k = 0; k < 500 && !b; ++k) {
            b = poll_exit(*bad);
            if (!b) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(b && *b == 127);
    } else {
        CHECK(err.find("no-such-program") != std::string::npos);
    }
}
#endif

// ---- the game's look (gameart.h) ----

namespace {

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    if (b.size() < at + 4) b.resize(at + 4);
    for (int k = 0; k < 4; ++k) b[at + std::size_t(k)] = std::uint8_t(v >> (8 * k));
}
void putf(std::vector<std::uint8_t>& b, std::size_t at, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(b, at, v);
}

// A font definition as the game program stores it: "3rev", scales, the buckets, glyphs for `codes`
std::vector<std::uint8_t> font_def(const std::u16string& codes) {
    std::vector<std::uint8_t> b(0x220 + 0x20 * codes.size(), 0);
    put32(b, 0, 0x76657233u);
    putf(b, 8, 4.0f);
    putf(b, 12, 4.0f);
    putf(b, 16, 0.8f);
    put32(b, 0x1C, std::uint32_t(0x220 + 0x20 * (codes.size() - 1)));
    for (unsigned i = 0; i < 0x80u; ++i) put32(b, 0x20 + 4 * i, 0x220);
    for (std::size_t k = 0; k < codes.size(); ++k) {
        const std::size_t at = 0x220 + 0x20 * k;
        putf(b, at, 0.1f * float(k));   // u
        putf(b, at + 8, 0.05f);         // w
        putf(b, at + 12, 0.1f);         // h
        putf(b, at + 16, 0.01f);        // ox
        putf(b, at + 20, 0.02f);        // oy
        putf(b, at + 24, 0.06f);        // advance
        b[at + 28] = std::uint8_t(codes[k]);
        b[at + 29] = std::uint8_t(codes[k] >> 8);
    }
    return b;
}

}  // namespace

TEST(font_definitions) {
    const auto b = font_def(u"AB ");
    GameFont f;
    CHECK(parse_font_def(b.data(), b.size(), f));
    CHECK(f.glyphs.size() == 3 && f.scale_x == 4.0f && f.glyphs[1].code == u'B');
    CHECK(f.has(u'A') && !f.has(u'Z'));
    CHECK(f.find(u'Z') == &f.glyphs.back());   // the game's lookup: the last glyph
    CHECK_NEAR(f.line(), 0.12, 1e-6);
    // GetStringWidth: -ox, the advances, then the last glyph's ox + w; x scale x size
    CHECK_NEAR(f.width(u"AB", 2.0f), (-0.01 + 0.06 + 0.01 + 0.05) * 4.0 * 2.0, 1e-5);
    CHECK_NEAR(f.size_for(48.0f) * f.line() * f.scale_y, 48.0, 1e-4);
    std::vector<std::uint8_t> bad = b;
    put32(bad, 0, 0);
    CHECK(!parse_font_def(bad.data(), bad.size(), f));
    CHECK(!parse_font_def(b.data(), 0x100, f));   // shorter than the bucket table
    bad = b;
    put32(bad, 0x1C, 0x10000);                     // the last glyph past the end
    CHECK(!parse_font_def(bad.data(), bad.size(), f));
}

TEST(elf_segments_and_fonts_fallback) {
    // a 32-bit ELF with one PT_LOAD of 0x100 bytes at 0x00100000 (file offset 0x100)
    ElfFile e;
    e.bytes.assign(0x200, 0);
    std::memcpy(e.bytes.data(), "\x7F" "ELF\x01\x01", 6);
    put32(e.bytes, 0x1C, 0x34);
    e.bytes[0x2A] = 32;
    e.bytes[0x2C] = 1;
    put32(e.bytes, 0x34, 1);            // PT_LOAD
    put32(e.bytes, 0x34 + 4, 0x100);    // offset
    put32(e.bytes, 0x34 + 8, 0x00100000);
    put32(e.bytes, 0x34 + 16, 0x100);   // file size
    put32(e.bytes, 0x110, 0xCAFEF00D);
    CHECK(e.parse());
    CHECK(e.offset_of(0x00100010) && *e.offset_of(0x00100010) == 0x110);
    CHECK(e.u32(0x00100010) && *e.u32(0x00100010) == 0xCAFEF00Du);
    CHECK(!e.offset_of(0x001000FE, 4));   // past the segment
    CHECK(!e.offset_of(0x00500000));
    // not the retail program: no fonts (the launcher keeps its plain look)
    GameFont fonts[3];
    std::string why;
    CHECK(!load_game_fonts(e, fonts, &why) && !why.empty());
    ElfFile junk;
    junk.bytes.assign(64, 0);
    CHECK(!junk.parse());
    // no texture dictionary
    const std::uint8_t zeros[64] = {};
    CHECK(!txd_texture(zeros, sizeof zeros, "Halo"));
}

TEST(game_art_lookup) {
    const fs::path d = fresh("artcase");
    touch(d / "Data" / "Global.txd");
    // the same file (on a case-insensitive disk, macOS / Windows, the path keeps the asked spelling)
    const auto same = [](const fs::path& a, const fs::path& b) {
        std::error_code ec;
        return !a.empty() && fs::equivalent(a, b, ec);
    };
    CHECK(same(find_ci(d, "DATA/GLOBAL.TXD"), d / "Data" / "Global.txd"));
    CHECK(same(find_ci(d, "data\\global.txd"), d / "Data" / "Global.txd"));
    CHECK(find_ci(d, "DATA/FRONTEND.TXD").empty());
    CHECK(to_u16("Caf\xC3\xA9 \xE2\x80\xA6") == u"Café …");
    CHECK(to_u16("\xFF") == u"?");
    // no game data: the plain look, saying why
    const GameArt none = load_game_art(fs::path());
    CHECK(!none.ok() && !none.why.empty());
    const GameArt empty = load_game_art(d);
    CHECK(!empty.ok() && empty.images.empty());
}

#ifdef B3_GAME_DIR
TEST(game_art_from_the_disc) {
    const GameArt a = load_game_art(B3_GAME_DIR);
    CHECK(a.ok());
    CHECK(a.font[kGlobalFont].glyphs.size() == 166 && a.font[kHeadFont].glyphs.size() == 30 && a.font[kSmallFont].glyphs.size() == 166);
    CHECK(a.font[kGlobalFont].tex.w == 256 && a.font[kHeadFont].tex.w == 128 && a.font[kHeadFont].tex.h == 256);
    CHECK(a.font[kGlobalFont].has(u'a') && a.font[kGlobalFont].has(u'~') && !a.font[kHeadFont].has(u'W'));
    for (const char* n : {"B3Logo", "grid", "titlescreen_logo", "Halo"}) CHECK(a.image(n) != nullptr);
    if (const ArtImage* h = a.image("Halo")) {   // the arc: transparent away from its line
        CHECK(h->w == 256 && h->rgba[3] == 0 && h->rgba[(255 * 256 + 255) * 4 + 3] == 0);
    }
}
#endif

// osk1: a text field's cursor (UTF-8 whole characters), Backspace / Delete, pasted paths
TEST(text_field_cursor) {
    TextEdit e("ab\xC3\xA9");   // "abé": the cursor at the end
    CHECK(e.cursor == 4);
    e.left();
    CHECK(e.cursor == 2);   // before the two-byte é
    e.insert("X");
    CHECK(e.text == "abX\xC3\xA9" && e.cursor == 3);
    CHECK(e.erase_forward());
    CHECK(e.text == "abX" && e.cursor == 3);
    CHECK(!e.erase_forward());
    e.home();
    CHECK(!e.erase_back());
    e.end();
    e.insert("\xC3\xBC");   // ü
    CHECK(e.erase_back() && e.text == "abX");
    e.set("");
    CHECK(e.empty() && e.cursor == 0);
    CHECK(utf8_prev("a\xC3\xA9", 3) == 1 && utf8_next("a\xC3\xA9", 1) == 3 && utf8_next("a", 1) == 1);
    CHECK(clean_pasted_path("\"C:\\Games\\Burnout 3\"\r\n") == "C:\\Games\\Burnout 3");
}

// osk1: the on-screen keyboard: every key reachable with the D-pad, every character a path needs on a key, the
// layers, Shift for one character / caps lock, the result keys
TEST(on_screen_keyboard) {
    for (bool sys : {false, true}) {
        const std::vector<OskKey> keys = osk_layout(sys);
        CHECK((osk_find(keys, OskKind::kSystem) >= 0) == sys);
        for (int r = 0; r < kOskRows; ++r) {   // each row fills its 13 units, keys side by side
            float w = 0;
            for (const OskKey& k : keys)
                if (k.row == r) w += k.w;
            CHECK_NEAR(w, kOskRowUnits, 1e-4);
        }
        // from the first key every key is reached by the four moves
        std::vector<char> seen(keys.size(), 0);
        std::vector<int> q{0};
        seen[0] = 1;
        for (std::size_t i = 0; i < q.size(); ++i)
            for (auto [dx, dy] : {std::pair{0, -1}, std::pair{0, 1}, std::pair{-1, 0}, std::pair{1, 0}}) {
                const int n = osk_move(keys, q[i], dx, dy);
                CHECK(n >= 0 && n < int(keys.size()));
                if (!seen[std::size_t(n)]) { seen[std::size_t(n)] = 1; q.push_back(n); }
            }
        CHECK(q.size() == keys.size());
    }
    const std::vector<OskKey> keys = osk_layout(false);
    for (char c : std::string("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_:\\./~ ,()[]{}'!@#$%&=+;")) {
        int key = -1;
        OskLayer l{};
        CHECK(osk_find_char(keys, std::string(1, c), key, l));
    }
    // Up / Down keep the column: from "q" down to "a", up to "1"; along a row Right wraps round
    int q = -1, a = -1, one = -1, dash = -1;
    OskLayer l{};
    osk_find_char(keys, "q", q, l);
    osk_find_char(keys, "a", a, l);
    osk_find_char(keys, "1", one, l);
    osk_find_char(keys, "-", dash, l);
    CHECK(osk_move(keys, q, 0, 1) == a && osk_move(keys, q, 0, -1) == one);
    CHECK(osk_move(keys, osk_find(keys, OskKind::kBackspace), 1, 0) == one && osk_move(keys, one, -1, 0) == osk_find(keys, OskKind::kBackspace));
    CHECK(osk_move(keys, dash, 1, 0) == osk_find(keys, OskKind::kBackspace));
    // typing: Shift is for one character, twice caps lock; the symbols layer
    OskState st;
    TextEdit e;
    osk_press(st, keys, osk_find(keys, OskKind::kShift), e);
    CHECK(st.layer == OskLayer::kUpper && !st.caps);
    osk_press(st, keys, q, e);
    CHECK(e.text == "Q" && st.layer == OskLayer::kLower);
    osk_press(st, keys, q, e);
    CHECK(e.text == "Qq");
    osk_press(st, keys, osk_find(keys, OskKind::kShift), e);
    osk_press(st, keys, osk_find(keys, OskKind::kShift), e);
    CHECK(st.caps && osk_label(keys[std::size_t(osk_find(keys, OskKind::kShift))], st.layer) == "Shift");
    osk_press(st, keys, a, e);
    osk_press(st, keys, a, e);
    CHECK(e.text == "QqAA" && st.layer == OskLayer::kUpper);
    osk_press(st, keys, osk_find(keys, OskKind::kSymbols), e);
    CHECK(st.layer == OskLayer::kSymbols && !st.caps);
    osk_press(st, keys, one, e);
    CHECK(e.text == "QqAA!");
    osk_press(st, keys, osk_find(keys, OskKind::kLeft), e);
    osk_press(st, keys, osk_find(keys, OskKind::kSpace), e);
    CHECK(e.text == "QqAA !" && e.cursor == 5);
    osk_press(st, keys, osk_find(keys, OskKind::kBackspace), e);
    CHECK(e.text == "QqAA!");
    CHECK(osk_press(st, keys, osk_find(keys, OskKind::kPaste), e) == OskResult::kPaste);
    CHECK(osk_press(st, keys, osk_find(keys, OskKind::kDone), e) == OskResult::kDone);
    CHECK(osk_press(st, keys, osk_find(keys, OskKind::kCancel), e) == OskResult::kCancel);
    osk_press(st, keys, osk_find(keys, OskKind::kClear), e);
    CHECK(e.empty());
    // the pad's Y: lower -> upper -> symbols -> lower
    OskState y;
    osk_cycle_layer(y);
    CHECK(y.layer == OskLayer::kUpper);
    osk_cycle_layer(y);
    CHECK(y.layer == OskLayer::kSymbols);
    osk_cycle_layer(y);
    CHECK(y.layer == OskLayer::kLower);
}

// osk1: the folder list: folders first, then the files asked for, ignoring case; hidden names left out; the folder
// above; mounted media under /run/media/<user>
TEST(folder_list) {
    const fs::path d = fresh("browse");
    for (const char* f : {"beta", "Alpha", ".hidden", "Gamma/inner"}) fs::create_directories(d / f);
    touch(d / "disc.ISO");
    touch(d / "b.bin");
    touch(d / "notes.txt");
    touch(d / ".secret.iso");
    const BrowseList l = list_folder(d, {"iso", "bin"});
    CHECK(l.ok);
    CHECK(l.entries.size() == 5);
    if (l.entries.size() == 5) {
        CHECK(l.entries[0].name == "Alpha" && l.entries[0].folder);
        CHECK(l.entries[1].name == "beta" && l.entries[2].name == "Gamma");
        CHECK(l.entries[3].name == "b.bin" && !l.entries[3].folder && l.entries[4].name == "disc.ISO");
    }
    CHECK(l.other_files == 1);   // notes.txt (the hidden ones are not counted)
    const BrowseList only = list_folder(d, {});
    CHECK(only.entries.size() == 3 && only.other_files == 3);
    const BrowseList limited = list_folder(d, {"iso", "bin"}, 2);
    CHECK(limited.more && limited.entries.size() == 2);
    CHECK(!list_folder(d / "missing", {}).ok);
    CHECK(has_extension("x/y.Zip", {"zip"}) && !has_extension("x/zip", {"zip"}) && !has_extension("x/y.", {""}));
    CHECK(parent_folder(d / "Gamma") == d.lexically_normal());
    CHECK(parent_folder(d / "Gamma" / "") == d.lexically_normal());
    CHECK(is_root_folder(fs::path("/")) && !is_root_folder(d));
    CHECK(parent_folder(fs::path("/")) == fs::path("/"));
    // /run/media/<user>/<label> (the user's own folder: its media), /run/media/<label>, another user's folder too when readable
    const fs::path media = fresh("media");
    fs::create_directories(media / "deck" / "SD card");
    fs::create_directories(media / "mmcblk0p1");
    const std::vector<BrowsePlace> m = mounted_media({media, media / "missing"}, "deck");
    CHECK(m.size() == 2);
    if (m.size() == 2) {
        CHECK(m[0].label == "SD card" && m[0].path == media / "deck" / "SD card");
        CHECK(m[1].label == "mmcblk0p1");
    }
    // the places: each once, only folders that are there
    const std::vector<BrowsePlace> p = browse_places({{"Here", d}, {"Again", d / ""}, {"Gone", d / "missing"}, {"Gamma", d / "Gamma"}});
    CHECK(p.size() >= 2 && p[0].label == "Here" && p[1].label == "Gamma");
}

TEST_MAIN("test_launcher: the launcher's logic (src/b3launcher/core.h, spawn.h, gameart.h)")
