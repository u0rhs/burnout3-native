// Time Attack ghosts (ghost1, remaster-features §5; src/b3replay/ghost.h): the .b3ghost file (round trip, damage,
// versions, the atomic save, the best lap of a track), the names, the time sync and the recorder's laps.
#include "test.h"

#include "ghost.h"
#include "records.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace b3replay;
using namespace b3replay::ghost;

// A pose at distance d along +z, yawed by `yaw`, wheels 1 m around it
Sample pose(float d, float yaw = 0) {
    Sample s;
    const float c = std::cos(yaw), n = std::sin(yaw);
    s.main.r[0][0] = c; s.main.r[0][2] = -n;
    s.main.r[2][0] = n; s.main.r[2][2] = c;
    s.main.r[3][0] = 100.5f; s.main.r[3][1] = 2.25f; s.main.r[3][2] = d;
    s.body = s.main;
    s.body.r[3][1] += 0.1f;
    for (int k = 0; k < 4; ++k) {
        s.wheel[k] = s.main;
        s.wheel[k].r[3][0] += (k & 1) ? 0.8f : -0.8f;
        s.wheel[k].r[3][2] += (k & 2) ? 1.3f : -1.3f;
        if (k & 1) for (int j = 0; j < 3; ++j) s.wheel[k].r[0][j] = -s.wheel[k].r[0][j];   // a mirrored wheel
    }
    s.wheel_speed = d * 0.5f;
    return s;
}

Lap make_lap(float time, int n, const std::string& car = "COMP/CAR1.BGV") {
    Lap l;
    l.track = "US/C1_V1";
    l.car = car;
    l.paint = 3;
    l.nwheel = 4;
    l.time = time;
    for (int k = 0; k < n; ++k) {
        Sample s = pose(float(k) * 0.7f, float(k) * 0.01f);
        s.t = time * float(k) / float(n - 1);
        l.samples.push_back(s);
    }
    return l;
}

fs::path scratch() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path d = fs::temp_directory_path() / ("b3ghost_test_" + std::to_string(stamp));
    fs::create_directories(d);
    return d;
}

bool near_mat(const Mat34& a, const Mat34& b, float tol) {
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j) if (std::fabs(a.r[i][j] - b.r[i][j]) > tol) return false;
    return true;
}

TEST(format_round_trip) {
    const Lap l = make_lap(61.25f, 50);
    const std::vector<std::uint8_t> b = encode(l);
    CHECK(b.size() == 4 * 5 + 1 + 1 + 2 + 2 + 2 + l.track.size() + l.car.size() + 50 * (4 + 30 * 6 + 4) + 4);
    CHECK(std::memcmp(b.data(), "B3GH", 4) == 0 && b[4] == kVersion && b[5] == 0);
    Lap d;
    std::string why;
    CHECK(decode(b.data(), b.size(), d, &why));
    CHECK(d.track == l.track && d.car == l.car && d.paint == 3 && d.nwheel == 4 && d.time == l.time && !d.test);
    CHECK(d.samples.size() == l.samples.size());
    bool same = true;
    for (std::size_t k = 0; k < d.samples.size() && k < l.samples.size(); ++k) {
        same = same && d.samples[k].t == l.samples[k].t && d.samples[k].wheel_speed == l.samples[k].wheel_speed;
        // positions exact (f32), rotations to 1 / 16384 (i16)
        same = same && near_mat(d.samples[k].main, l.samples[k].main, 1.0f / 16000.0f) && near_mat(d.samples[k].body, l.samples[k].body, 1.0f / 16000.0f);
        for (int w = 0; w < 4; ++w) same = same && near_mat(d.samples[k].wheel[w], l.samples[k].wheel[w], 1.0f / 16000.0f);
        for (int j = 0; j < 3; ++j) same = same && d.samples[k].main.r[3][j] == l.samples[k].main.r[3][j];
    }
    CHECK(same);
    CHECK(d.samples[1].wheel[1].r[0][0] < 0);   // a mirrored wheel stays mirrored
    // a test lap's flag
    Lap t = l;
    t.test = true;
    Lap dt;
    const auto bt = encode(t);
    CHECK(decode(bt.data(), bt.size(), dt) && dt.test);
}

TEST(format_rejects_damage) {
    const Lap l = make_lap(30, 10);
    std::vector<std::uint8_t> b = encode(l);
    Lap d;
    std::string why;
    std::vector<std::uint8_t> x = b;
    x[x.size() / 2] ^= 0x40;   // one bit in a sample
    CHECK(!decode(x.data(), x.size(), d, &why) && why.find("checksum") != std::string::npos);
    CHECK(!decode(b.data(), b.size() - 7, d));   // truncated
    CHECK(!decode(b.data(), 10, d));
    x = b;
    x[0] = 'X';
    CHECK(!decode(x.data(), x.size(), d, &why) && why == "not a ghost file");
    // another version (its checksum made right): refused by name
    x = b;
    x[4] = 2;
    std::uint32_t h = 2166136261u;
    for (std::size_t k = 0; k + 4 < x.size(); ++k) { h ^= x[k]; h *= 16777619u; }
    for (int k = 0; k < 4; ++k) x[x.size() - 4 + std::size_t(k)] = std::uint8_t(h >> (8 * k));
    CHECK(!decode(x.data(), x.size(), d, &why) && why.find("version 2") != std::string::npos);
    // samples out of order
    Lap bad = l;
    bad.samples[5].t = bad.samples[4].t;
    const auto bb = encode(bad);
    CHECK(!decode(bb.data(), bb.size(), d));
}

TEST(names) {
    CHECK(last_two("data/game/TRACKS/US/C1_V1") == "US/C1_V1");
    CHECK(last_two("C:\\Games\\B3\\TRACKS\\EU\\M2_V2\\") == "EU/M2_V2");
    CHECK(last_two("data/game/PVEH/COMP/CAR1.BGV") == "COMP/CAR1.BGV");
    CHECK(last_two("CAR1.BGV") == "CAR1.BGV");
    CHECK(slug("US/C1_V1", false) == "US_C1_V1");
    CHECK(slug("comp/car1.bgv", true) == "COMP_CAR1");
    CHECK(file_name("US/C1_V1", "COMP/CAR1.BGV") == "US_C1_V1-COMP_CAR1.b3ghost");
}

TEST(save_atomic_and_best_of_track) {
    const fs::path dir = scratch();
    const std::string a = (dir / file_name("US/C1_V1", "COMP/CAR1.BGV")).string();
    std::string why;
    CHECK(save(a, make_lap(70, 20), &why));
    CHECK(!fs::exists(a + ".tmp"));
    CHECK(save(a, make_lap(65, 20), &why));   // replaces the file
    Lap l;
    CHECK(load(a, l) && l.time == 65);
    // another car on the same track: faster; another track: faster still (not this track's)
    CHECK(save((dir / file_name("US/C1_V1", "MUSC/CAR2.BGV")).string(), make_lap(63, 20, "MUSC/CAR2.BGV")));
    Lap other = make_lap(10, 20);
    other.track = "US/C2_V1";
    CHECK(save((dir / file_name("US/C2_V1", "COMP/CAR1.BGV")).string(), other));
    // a test lap is never the best while a real one exists
    Lap test = make_lap(5, 20, "COMP/CAR3.BGV");
    test.test = true;
    CHECK(save((dir / file_name("US/C1_V1", "COMP/CAR3.BGV")).string(), test));
    // a damaged file is skipped
    { std::ofstream f(dir / "US_C1_V1-JUNK.b3ghost", std::ios::binary); f << "B3GH garbage"; }
    Lap best;
    std::string path;
    CHECK(best_for_track(dir.string(), "US/C1_V1", best, &path));
    CHECK(best.time == 63 && best.car == "MUSC/CAR2.BGV" && path.find("US_C1_V1-MUSC_CAR2.b3ghost") != std::string::npos);
    CHECK(best_for_track(dir.string(), "US/C2_V1", best) && best.time == 10);
    CHECK(!best_for_track(dir.string(), "EU/M1_V1", best));
    CHECK(!best_for_track((dir / "none").string(), "US/C1_V1", best));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(time_sync) {
    Lap l;
    for (int k = 0; k <= 4; ++k) { Sample s = pose(float(k)); s.t = float(k) * 0.5f; l.samples.push_back(s); }   // 0, .5, 1, 1.5, 2
    At at;
    CHECK(locate(l, 0, at) && at.i == 0 && at.u == 0);
    CHECK(locate(l, 0.25f, at) && at.i == 0 && std::fabs(at.u - 0.5f) < 1e-6f);
    CHECK(locate(l, 0.5f, at) && at.i == 1 && at.u == 0);
    CHECK(locate(l, 1.9f, at) && at.i == 3 && std::fabs(at.u - 0.8f) < 1e-5f);
    CHECK(locate(l, 2.0f, at) && at.i == 3 && at.u == 1);
    CHECK(!locate(l, 2.01f, at));   // past the lap's end: not drawn
    CHECK(!locate(l, -0.1f, at));
    CHECK(!locate(l, std::nanf(""), at));
    CHECK(!locate(Lap{}, 0, at));
    // uneven spacing (a 30 Hz stretch)
    l.samples[2].t = 0.6f;
    CHECK(locate(l, 1.0f, at) && at.i == 2 && std::fabs(at.u - 0.4f / 0.9f) < 1e-5f);
}

TickInput tick(int index, float time, float d, bool ta = true) {
    TickInput in;
    in.time_attack = ta;
    in.index = index;
    in.time = time;
    in.have_car = true;
    in.pose = pose(d);
    in.paint = 2;
    in.nwheel = 4;
    in.car = "COMP/CAR1.BGV";
    return in;
}

TEST(recorder_laps) {
    Recorder r;
    Lap done;
    int laps = 0;
    // the countdown (lap -1), the start (0 from -1), 100 ticks of lap 0, the line (lap 1, the timer still the lap's)
    for (int k = 0; k < 10; ++k) laps += r.step(tick(-1, 0, 0), &done);
    laps += r.step(tick(0, 0, 0), &done);   // the lap begins here (the game's timer not moving yet)
    for (int k = 1; k <= 100; ++k) laps += r.step(tick(0, float(k) / 60.0f, float(k)), &done);
    CHECK(laps == 0 && r.recording());
    laps += r.step(tick(1, 101.0f / 60.0f, 101), &done);
    CHECK(laps == 1);
    CHECK(done.time == 101.0f / 60.0f && done.car == "COMP/CAR1.BGV" && done.paint == 2 && done.nwheel == 4 && !done.test);
    CHECK(done.samples.size() == 102 && done.samples.front().t == 0 && done.samples.back().t == done.time);
    CHECK(done.samples.back().main.r[3][2] == 101.0f && done.samples.front().main.r[3][2] == 0.0f);
    bool rising = true;
    for (std::size_t k = 1; k < done.samples.size(); ++k) rising = rising && done.samples[k].t > done.samples[k - 1].t;
    CHECK(rising);
    // lap 1: the boundary tick began it (t 0 at the line); a pause (the timer held) adds nothing
    for (int k = 1; k <= 50; ++k) laps += r.step(tick(1, float(k) / 60.0f, 101.0f + float(k)), &done);
    for (int k = 0; k < 30; ++k) laps += r.step(tick(1, 50.0f / 60.0f, 151), &done);
    for (int k = 51; k <= 90; ++k) laps += r.step(tick(1, float(k) / 60.0f, 101.0f + float(k)), &done);
    laps += r.step(tick(2, 91.0f / 60.0f, 192), &done);
    CHECK(laps == 2 && done.samples.size() == 92 && done.samples.front().main.r[3][2] == 101.0f && done.time == 91.0f / 60.0f);
    // after 8 laps the index wraps to 0: still the next lap
    Recorder w;
    w.step(tick(6, 0, 0), &done);
    w.step(tick(7, 0, 0), &done);
    for (int k = 1; k <= 10; ++k) w.step(tick(7, float(k) / 60.0f, float(k)), &done);
    CHECK(w.step(tick(0, 11.0f / 60.0f, 11), &done) && done.samples.size() == 12);
}

TEST(recorder_drops_unclean_laps) {
    Lap done;
    {   // joined mid-lap: that lap's start was not seen, the next one counts
        Recorder r;
        for (int k = 30; k <= 60; ++k) CHECK(!r.step(tick(0, float(k) / 60.0f, float(k)), &done));
        CHECK(!r.step(tick(1, 61.0f / 60.0f, 61), &done));
        for (int k = 1; k <= 20; ++k) r.step(tick(1, float(k) / 60.0f, float(k)), &done);
        CHECK(r.step(tick(2, 21.0f / 60.0f, 21), &done) && done.samples.size() == 22);
    }
    {   // a restart (back to -1) drops the lap; so does leaving Time Attack
        Recorder r;
        r.step(tick(-1, 0, 0), &done);
        r.step(tick(0, 0, 0), &done);
        for (int k = 1; k <= 20; ++k) r.step(tick(0, float(k) / 60.0f, float(k)), &done);
        CHECK(!r.step(tick(-1, 0, 0), &done) && !r.recording());
        CHECK(!r.step(tick(0, 0, 0), &done) && r.recording());
        for (int k = 1; k <= 20; ++k) r.step(tick(0, float(k) / 60.0f, float(k)), &done);
        CHECK(!r.step(tick(0, 21.0f / 60.0f, 21, false), &done) && !r.recording());
    }
    {   // the timer jumping back mid-lap, or a gap of more than 2 s, or no car at the line: no lap
        Recorder r;
        r.step(tick(-1, 0, 0), &done);
        r.step(tick(0, 0, 0), &done);
        for (int k = 1; k <= 20; ++k) r.step(tick(0, float(k) / 60.0f, float(k)), &done);
        r.step(tick(0, 0.01f, 1), &done);
        CHECK(!r.recording() && !r.step(tick(1, 1, 1), &done));
        for (int k = 1; k <= 20; ++k) r.step(tick(1, float(k) / 60.0f, float(k)), &done);
        r.step(tick(1, 5.0f, 5), &done);
        CHECK(!r.recording() && !r.step(tick(2, 5.1f, 5), &done));
        for (int k = 1; k <= 20; ++k) r.step(tick(2, float(k) / 60.0f, float(k)), &done);
        TickInput no = tick(3, 21.0f / 60.0f, 21);
        no.have_car = false;
        CHECK(!r.step(no, &done));
    }
}

TEST(recorder_test_lap) {
    Recorder r(1.0f);   // B3_GHOST_TEST_LAP=1: the first second of a lap is a (test) lap
    Lap done;
    int laps = 0;
    r.step(tick(-1, 0, 0), &done);
    r.step(tick(0, 0, 0), &done);
    for (int k = 1; k <= 90; ++k) laps += r.step(tick(0, float(k) / 60.0f, float(k)), &done);
    CHECK(laps == 1 && done.test && done.time == 1.0f && done.samples.size() == 61 && !r.recording());
}

}  // namespace

// media1 (records.h): a replay playing writes no record; the folder is read as given
TEST(records_replay_playback) {
    records::Run play;
    play.playback = true;
    CHECK(!records::writes("ghosts", play));
    CHECK(records::folder("ghosts", play) == "ghosts");   // the player's own ghost is still shown
    const records::Run own;
    CHECK(records::writes("ghosts", own));
    CHECK(!records::writes("", own));
    CHECK(records::folder("ghosts", own) == "ghosts");
}

// media1 (records.h): a gameplay-changing mod set reads and writes its own folder; the plain game's lists only its own
// files, so it never sees that set's ghosts
TEST(records_gameplay_mods_apart) {
    const fs::path dir = fs::temp_directory_path() / ("b3ghost-records-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    records::Run mod;
    mod.gameplay_set = "0123456789abcdef";
    const std::string folder = records::folder(dir.string(), mod);
    CHECK(fs::path(folder) == dir / "modded-0123456789abcdef");
    CHECK(records::writes(dir.string(), mod));
    CHECK(records::folder("", mod).empty());
    const Lap l = make_lap(50, 20);
    CHECK(save((fs::path(folder) / file_name(l.track, l.car)).string(), l));
    Lap got;
    CHECK(!best_for_track(dir.string(), l.track, got));   // the plain game: none
    CHECK(best_for_track(folder, l.track, got));          // the set: its lap
    CHECK(got.time == 50);
    records::Run play = mod;
    play.playback = true;
    CHECK(!records::writes(dir.string(), play));
    CHECK(records::folder(dir.string(), play) == folder);   // a replay with that set shows that set's ghost
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_MAIN("Time Attack ghosts (src/b3replay/ghost.h)")
