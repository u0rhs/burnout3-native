// The speedrun timer (speedrun1, remaster-features §5; src/b3replay/speedrun.h): the game's time text, the .b3split
// file (round trip, damage, the atomic save), the tracker on synthetic tick sequences (start, laps, finish, outs,
// restarts, Time Attack laps, pauses, joining late, leaving) and the panel's rows.
#include "test.h"

#include "speedrun.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace b3replay;
using namespace b3replay::speedrun;

constexpr std::uint32_t kRaceVt = 0x4DE980u, kTaVt = 0x4DEBC0u, kCrashVt = 0x4DF360u;

fs::path scratch() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path d = fs::temp_directory_path() / ("b3speedrun_test_" + std::to_string(stamp));
    fs::create_directories(d);
    return d;
}

// A synthetic race of `laps` laps of lap time `lap` s on the game's 60 Hz tick grid: as CB3Score::Update and
// IncrementLapCount write the fields (state 0 in the countdown, lap 0 at the start line, the race time from GO, the lap
// times at each line, state 3 and finish state 1 at the last line).
struct Race {
    std::uint32_t vt = kRaceVt;
    int laps = 3;
    float lap = 2.0f;
    std::uint32_t poll = 1000;
    ScoreState s;
    float start = 0;   // the game timer at GO
    Race() { s.valid = true; s.logic = vt; s.state = 0; s.lap = -1; }
    TickIn tick() {
        TickIn in;
        in.poll = poll;
        in.race = true;
        in.s = s;
        in.track = "US/C1_V1";
        in.car = "COMP/CAR1.BGV";
        return in;
    }
};

// Drive `r` through the countdown (n ticks), then racing until finished, feeding `t`; the results given
std::vector<Result> run(Tracker& t, Race& r, const Best* best, int countdown = 30, int max_ticks = 100000) {
    std::vector<Result> out;
    r.s.logic = r.vt;
    r.s.laps = r.laps;
    for (int k = 0; k < countdown; ++k, ++r.poll) {
        r.s.state = 0;
        r.s.time = 0;
        r.s.lap = k > 10 ? 0 : -1;   // the start line crossed on the grid
        Result res;
        if (t.step(r.tick(), best, &res)) out.push_back(res);
    }
    r.s.state = 2;
    float lap_start = 0;
    for (int k = 1; k < max_ticks; ++k, ++r.poll) {
        r.s.time = float(k) / 60.0f;
        r.s.lap_time = r.s.time - lap_start;
        if (r.s.lap_time >= r.lap - 1e-4f) {   // the line: IncrementLapCount
            r.s.lap_times[r.s.lap & 7] = r.s.lap_time;
            lap_start = r.s.time;
            r.s.lap = (r.s.lap + 1) % 8;
            ++r.s.laps_done;
            if (r.laps > 0 && r.s.laps_done >= r.laps) { r.s.state = 3; r.s.finish = 1; }
        }
        Result res;
        if (t.step(r.tick(), best, &res)) out.push_back(res);
        if (r.s.state == 3) break;
    }
    ++r.poll;
    return out;
}

TEST(time_text_as_the_game) {
    // func_00155280 then func_00394660's split: "%02d:%02d:%02d" (D_004C98C0)
    CHECK(time_text(0) == "00:00:00");
    CHECK(time_text(kNone) == "00:00:00");
    // the PS2's float arithmetic (the native game's: add / mul truncate): 26 + 0.35 truncates to 26.349998, so 26.35 s
    // prints 00:26:34, as the game's HUD lap timer showed it (race_check timeattack, poll 12500); 1/60 s steps print
    // their own hundredths once truncated twice
    CHECK(time_text(26.35f) == "00:26:34");
    CHECK(time_text(1.3666667f) == "00:01:36");
    CHECK(time_text(8.9833336f) == "00:08:97");
    CHECK(time_text(61.5f) == "01:01:50");
    CHECK(time_text(125.5f) == "02:05:50");
    CHECK(time_text(3599.99f).substr(0, 5) == "59:59");
    // the rounding is the game's: truncation to hundredths, never up
    CHECK(game_round(1.239f) <= 1.24f && game_round(1.239f) > 1.229f);
    CHECK(game_round(0) == 0 && game_round(kNone) == kNone);
    // every hundredth of the first 10 minutes on the 60 Hz grid prints minutes / seconds as the plain split does
    for (int k = 0; k < 36000; k += 7) {
        const float t = float(k) / 60.0f;
        const std::string s = time_text(t);
        const int m = std::stoi(s.substr(0, 2)), sec = std::stoi(s.substr(3, 2)), c = std::stoi(s.substr(6, 2));
        CHECK(m == int(t / 60.0f) || std::fabs(t - 60.0f * float(m + 1)) < 0.02f);
        CHECK(std::fabs(float(m * 60 + sec) + float(c) / 100.0f - t) < 0.0201f);
    }
    CHECK(delta_text(-1.5f) == "-00:01:50" && delta_text(0.25f) == "+00:00:25");
}

TEST(modes) {
    CHECK(mode_of(kRaceVt) == Mode::kRace && mode_of(kTaVt) == Mode::kTimeAttack && mode_of(0x4DEE00u) == Mode::kRoadRage);
    CHECK(mode_of(0x4DECE0u) == Mode::kEliminator && mode_of(kCrashVt) == Mode::kNone && mode_of(0) == Mode::kNone);
    CHECK(std::string(mode_name(Mode::kTimeAttack)) == "TIMEATTACK");
    CHECK(event_key(Mode::kRace, "US/C1_V1", "COMP/CAR1.BGV", 3) == "RACE US/C1_V1 COMP/CAR1.BGV 3");
    CHECK(file_name(Mode::kRace, "US/C1_V1", "COMP/CAR1.BGV", 3) == "RACE-US_C1_V1-COMP_CAR1-3L.b3split");
}

TEST(file_round_trip_and_damage) {
    Best b;
    b.key = "RACE US/C1_V1 COMP/CAR1.BGV 3";
    b.time = 123.456f;
    b.splits = {41.0f, 40.5f, 41.956f};
    b.best_lap = 40.5f;
    const std::vector<std::uint8_t> e = encode(b);
    Best d;
    std::string why;
    CHECK(decode(e.data(), e.size(), d, &why) && d == b);
    Best none;
    none.key = "TIMEATTACK US/C2_V2 COMP/CAR1.BGV 0";
    const std::vector<std::uint8_t> en = encode(none);
    CHECK(decode(en.data(), en.size(), d) && d == none && d.time == kNone && d.splits.empty());
    for (std::size_t k = 0; k < e.size(); ++k) {   // any one byte changed: refused (checksum, magic, ...)
        std::vector<std::uint8_t> x = e;
        x[k] ^= 0x41;
        CHECK(!decode(x.data(), x.size(), d));
    }
    for (std::size_t n = 0; n < e.size(); ++n) CHECK(!decode(e.data(), n, d));   // truncated
    std::vector<std::uint8_t> v = e;   // another version (checksum fixed): refused, said so
    v[4] = 2;
    std::uint32_t h = 2166136261u;
    for (std::size_t k = 0; k + 4 < v.size(); ++k) { h ^= v[k]; h *= 16777619u; }
    for (int k = 0; k < 4; ++k) v[v.size() - 4 + std::size_t(k)] = std::uint8_t(h >> (8 * k));
    CHECK(!decode(v.data(), v.size(), d, &why) && why.find("version 2") != std::string::npos);
    // the atomic save: a .tmp beside it, renamed over an older file; load gives it back
    const fs::path dir = scratch();
    const std::string path = (dir / "sub" / file_name(Mode::kRace, "US/C1_V1", "COMP/CAR1.BGV", 3)).string();
    CHECK(save(path, b));
    CHECK(load(path, d) && d == b && !fs::exists(path + ".tmp"));
    b.time = 120.0f;
    CHECK(save(path, b) && load(path, d) && d.time == 120.0f);
    { std::ofstream f(path, std::ios::binary | std::ios::trunc); f << "B3SR junk"; }
    CHECK(!load(path, d, &why));
    CHECK(!load((dir / "none.b3split").string(), d));
    fs::remove_all(dir);
}

TEST(tracker_race_start_laps_finish) {
    Tracker t;
    Race r;
    const std::vector<Result> res = run(t, r, nullptr);
    CHECK(res.size() == 1);
    CHECK(res[0].kind == Mark::kFinish && res[0].mode == Mode::kRace && res[0].laps == 3 && res[0].track == "US/C1_V1" &&
          res[0].car == "COMP/CAR1.BGV");
    CHECK(res[0].splits.size() == 3);
    float sum = 0;
    for (float s : res[0].splits) { CHECK(std::fabs(s - 2.0f) < 0.02f); sum += s; }
    CHECK(std::fabs(res[0].time - sum) < 1e-3f && res[0].time == r.s.time);   // the game's race time, as it stands at the line
    // the marks: reset (first tick), start at GO, 3 laps, finish
    const std::vector<Mark>& m = t.marks();
    CHECK(m.size() == 6 && m[0].kind == Mark::kReset && m[1].kind == Mark::kStart && m[1].poll == 1030 && m[2].kind == Mark::kLap &&
          m[2].lap == 1 && m[4].lap == 3 && m[5].kind == Mark::kFinish && m[5].time == r.s.time);
    // after the finish the frozen state gives nothing more
    Result x;
    for (int k = 0; k < 100; ++k, ++r.poll) CHECK(!t.step(r.tick(), nullptr, &x));
    CHECK(t.marks().size() == 6);
}

TEST(tracker_restart_and_bests) {
    Tracker t;
    Race r;
    r.lap = 1.0f;
    run(t, r, nullptr);
    Best b;
    b.time = r.s.time;   // the kept best: this run's
    b.splits = {1.0f, 1.0f, 1.0f};
    // a restart (the game's own: the score reset, state 0, lap -1, race time 0) starts a new attempt with its best
    Race r2;
    r2.poll = r.poll + 50;
    r2.lap = 0.9f;
    const std::vector<Result> res = run(t, r2, &b);
    CHECK(res.size() == 1 && res[0].time < b.time);
    const std::vector<Mark> m = t.marks_at(r2.poll);
    CHECK(m.front().kind == Mark::kReset && m.front().ref == b.time && m.back().kind == Mark::kFinish && m.back().ref == b.time);
    // the shown attempt follows the poll: the first race's marks at its own polls
    const std::vector<Mark> old = t.marks_at(r.poll - 1);
    CHECK(old.back().kind == Mark::kFinish && old.back().ref == kNone);
    // a restart mid-race (state 2 -> 0)
    Tracker u;
    Race a;
    a.laps = 5;
    a.lap = 100;
    run(u, a, nullptr, 30, 200);   // 200 ticks of racing, no lap done
    CHECK(u.running());
    a.s = ScoreState{};
    a.s.valid = true;
    a.s.logic = kRaceVt;
    a.s.laps = 5;
    a.s.state = 0;
    a.s.lap = -1;
    Result x;
    CHECK(!u.step(a.tick(), nullptr, &x) && !u.running() && u.marks().back().kind == Mark::kReset);
}

TEST(tracker_outs_pauses_and_leaving) {
    // an out (finish 2: the race time FLT_MAX): an out mark, no result
    Tracker t;
    Race r;
    r.laps = 3;
    r.lap = 100;
    run(t, r, nullptr, 30, 120);
    r.s.state = 3;
    r.s.finish = 2;
    r.s.time = kNone;
    Result x;
    CHECK(!t.step(r.tick(), nullptr, &x) && t.marks().back().kind == Mark::kOut);
    std::vector<Row> rw = rows(1, Mode::kRace, r.s, t.marks_at(r.poll));
    CHECK(!rw.empty() && rw[0].value == "--:--:--");
    ++r.poll;
    // a pause: the timer held for many ticks changes nothing
    Tracker p;
    Race q;
    q.lap = 100;
    run(p, q, nullptr, 30, 60);
    const std::size_t n = p.marks().size();
    for (int k = 0; k < 600; ++k, ++q.poll) CHECK(!p.step(q.tick(), nullptr, &x));
    CHECK(p.marks().size() == n && p.running());
    // leaving the event (a front-end tick), then a crash mode: no timer, the attempt closed
    TickIn fe = q.tick();
    fe.race = false;
    CHECK(!p.step(fe, nullptr, &x) && !p.running() && p.marks().back().kind == Mark::kReset);
    TickIn crash = q.tick();
    crash.s.logic = kCrashVt;
    CHECK(!p.step(crash, nullptr, &x) && p.mode() == Mode::kNone);
    CHECK(rows(2, Mode::kNone, crash.s, {}).empty());
    // joined while racing (the renderer started late): the game's own race time is shown, marks from there
    Tracker j;
    Race late;
    late.s.state = 2;
    late.s.lap = 0;
    late.s.laps = 3;
    late.s.time = 33.0f;
    CHECK(!j.step(late.tick(), nullptr, &x) && j.running());
    rw = rows(1, Mode::kRace, late.s, j.marks_at(late.poll));
    CHECK(rw[0].value == "00:33:00");
    // a tick without the player's car keeps the attempt
    TickIn nocar = late.tick();
    nocar.car.clear();
    ++nocar.poll;
    const std::size_t nj = j.marks().size();
    CHECK(!j.step(nocar, nullptr, &x) && j.marks().size() == nj && j.running());
}

TEST(tracker_time_attack_laps) {
    // Time Attack: laps without an end (lap count 0); each lap is a result; the ring wraps after 8
    Tracker t;
    Race r;
    r.vt = kTaVt;
    r.laps = 0;
    r.lap = 0.5f;
    std::vector<Result> res;
    r.s.logic = kTaVt;
    r.s.laps = 0;
    for (int k = 0; k < 20; ++k, ++r.poll) { r.s.lap = k > 5 ? 0 : -1; Result x; if (t.step(r.tick(), nullptr, &x)) res.push_back(x); }
    r.s.state = 2;
    float lap_start = 0;
    for (int k = 1; k <= 60 * 6; ++k, ++r.poll) {   // 6 s: 12 laps of 0.5 s (wrapping the 8-entry ring)
        r.s.time = float(k) / 60.0f;
        r.s.lap_time = r.s.time - lap_start;
        if (r.s.lap_time >= r.lap - 1e-4f) {
            r.s.lap_times[r.s.lap & 7] = r.s.lap_time;
            lap_start = r.s.time;
            r.s.lap = (r.s.lap + 1) % 8;
            if (r.s.lap == 0) r.s.wrapped = true;
            else if (!r.s.wrapped) ++r.s.laps_done;
        }
        Result x;
        if (t.step(r.tick(), nullptr, &x)) res.push_back(x);
    }
    CHECK(res.size() == 12);
    for (const Result& x : res) CHECK(x.kind == Mark::kLap && x.mode == Mode::kTimeAttack && std::fabs(x.time - 0.5f) < 0.02f);
    const std::vector<Row> rw = rows(2, Mode::kTimeAttack, r.s, t.marks_at(r.poll));
    CHECK(rw.size() == 6 && rw[0].label == "LAP 13" && rw[1].label == "LAP 12" && rw[4].label == "LAP 9" && rw[5].label == "BEST LAP");
    CHECK(rw[5].value == time_text(res[0].time) || rw[5].value.substr(0, 5) == "00:00");
}

TEST(rows_for_the_panel) {
    Tracker t;
    Race r;
    r.lap = 2.0f;
    Best b;
    b.time = 7.0f;
    b.splits = {1.9f, 2.5f, 2.6f};
    run(t, r, &b);
    // Event: the time and the best; Event + splits: the laps between, coloured against the best's splits
    const std::vector<Mark> m = t.marks_at(r.poll);
    std::vector<Row> one = rows(1, Mode::kRace, r.s, m);
    CHECK(one.size() == 2 && one[0].label == "FINISH" && one[0].value == time_text(r.s.time) && one[1].label == "NEW BEST");
    std::vector<Row> two = rows(2, Mode::kRace, r.s, m);
    CHECK(two.size() == 5 && two[1].label == "LAP 1/3" && two[1].colour == Row::kRed && two[2].colour == Row::kGreen);
    CHECK(rows(0, Mode::kRace, r.s, m).empty());
    // in the countdown: 00:00:00 and the kept best
    Tracker c;
    Race w;
    Result x;
    w.s.laps = 3;
    for (int k = 0; k < 5; ++k, ++w.poll) c.step(w.tick(), &b, &x);
    std::vector<Row> cd = rows(2, Mode::kRace, w.s, c.marks_at(w.poll));
    CHECK(cd.size() == 2 && cd[0].value == "00:00:00" && cd[1].label == "BEST" && cd[1].value == time_text(7.0f));
    // racing: the running lap shown after the done ones
    Tracker g;
    Race v;
    v.lap = 1.0f;
    v.laps = 3;
    run(g, v, nullptr, 30, 90);   // 1.5 s: one lap done, the second running
    std::vector<Row> rr = rows(2, Mode::kRace, v.s, g.marks_at(v.poll));
    CHECK(rr.size() == 4 && rr[0].label == "TIME" && rr[1].label == "LAP 1/3" && rr[2].label == "LAP 2/3" && rr[3].value == "--:--:--");
}

}  // namespace

TEST_MAIN("the speedrun timer (src/b3replay/speedrun.h)")
