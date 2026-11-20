// lat1: the display clock's schedule mode (src/b3replay/clock.h step_sched) on synthetic feeds: steady latch stamps with
// jittered arrivals (the pacing wait at the poll), 2-tick frames (crashes), no stamps (the arrival clock), and the age of
// the shown state.
#include <cstdint>
#include <random>
#include <vector>

#include "../src/b3replay/clock.h"
#include "test.h"

namespace {

constexpr std::uint64_t kT = 16666667;   // one tick, ns

struct Feed {   // DisplayClockT's Feed: first(), last(), arrival_ns(i), sched_ns(i)
    std::vector<std::uint64_t> arrive, sched;
    std::size_t n = 0;   // ticks received so far
    std::size_t first() const { return 0; }
    std::size_t last() const { return n - 1; }
    std::uint64_t arrival_ns(std::size_t i) const { return arrive[i]; }
    std::uint64_t sched_ns(std::size_t i) const { return sched[i]; }
    void receive_until(std::uint64_t now) { while (n < arrive.size() && arrive[n] <= now) ++n; }
};

// Ticks latched every T from t0; each arrives `lag` (2..6 ms, seeded) after its stamp.
Feed steady(std::size_t ticks, std::uint64_t t0, bool stamps, int pair = 1) {
    Feed f;
    std::mt19937 r(7);
    std::uniform_int_distribution<int> lag(2000000, 6000000);
    for (std::size_t i = 0; i < ticks; ++i) {
        const std::uint64_t s = t0 + i * kT;
        // pair = 2: frames of two ticks, both arriving after the pair's second latch
        const std::size_t head = i - i % std::size_t(pair);
        const std::uint64_t a = t0 + (head + std::size_t(pair) - 1) * kT + std::uint64_t(lag(r));
        f.sched.push_back(stamps ? s : 0);
        f.arrive.push_back(std::max(a, s));
    }
    return f;
}

}  // namespace

TEST(schedule_clock_follows_the_stamps_without_starving) {
    Feed f = steady(600, 1000000000ull, true);
    b3replay::DisplayClockT<Feed> c;
    std::uint64_t starved = 0, frames = 0, backwards = 0;
    double prev = -1, worst_off = 0;
    for (std::uint64_t now = f.arrive[3]; now < f.arrive.back(); now += 2100000) {   // ~476 fps
        f.receive_until(now);
        const double x = c.step_sched(f, 1.0, 1.5, now);
        CHECK(c.sched_on);
        if (now > f.arrive[120]) {   // after the margin has settled
            ++frames;
            starved += c.starved;
            if (x < prev) ++backwards;
            // where the schedule says it should be: tick r fully shown at S_r + m + T
            const double ideal = (double(now) - double(f.sched[0]) - c.margin_ms * 1e6) / double(kT) - 1.0;
            worst_off = std::max(worst_off, std::fabs(x - ideal));
        }
        prev = x;
    }
    CHECK(frames > 1000);
    CHECK(starved * 100 < frames * 2);   // the margin covers ~95 % of the lags; the rest only touches the newest tick
    CHECK(backwards == 0);
    CHECK(worst_off < 0.05);
    CHECK(c.margin_ms > 5.0 && c.margin_ms < 7.5);   // p95 of 2..6 ms, + 1 ms
}

TEST(schedule_clock_two_tick_frames) {
    Feed f = steady(600, 1000000000ull, true, 2);
    b3replay::DisplayClockT<Feed> c;
    std::uint64_t starved = 0, frames = 0, backwards = 0;
    double prev = -1;
    for (std::uint64_t now = f.arrive[3]; now < f.arrive.back(); now += 2100000) {
        f.receive_until(now);
        const double x = c.step_sched(f, 1.0, 1.5, now);
        if (now > f.arrive[120]) { ++frames; starved += c.starved; if (x < prev) ++backwards; }
        prev = x;
    }
    CHECK(frames > 1000);
    CHECK(starved * 100 < frames * 3);
    CHECK(backwards == 0);
    CHECK_NEAR(c.rate, 60.0, 0.1);
    CHECK(c.margin_ms > 16.7 && c.margin_ms < 25.0);   // the pair's first tick arrives a tick after its stamp
}

// split2p (a report: split screen choppy at 150-200 FPS): native_play's 2-tick frames (split screen, crashes) latch their
// two polls back to back (stamps 0.3 ms apart) and then wait two VBlanks. The display must still advance a tick per VBlank
// at every frame: no held frames, no jumps (it used to reach the newest tick half way through each frame, hold, then jump).
TEST(schedule_clock_two_tick_frames_one_latch) {
    const double vb = 1e9 / 59.94;
    Feed f;
    std::mt19937 r(11);
    std::uniform_int_distribution<int> lag(2000000, 6000000);
    for (std::size_t i = 0; i < 600; ++i) {
        const std::uint64_t s = 1000000000ull + std::uint64_t(double(i / 2) * 2.0 * vb) + (i % 2) * 300000ull;
        f.sched.push_back(s);
        f.arrive.push_back(s + std::uint64_t(lag(r)));
    }
    b3replay::DisplayClockT<Feed> c;
    std::uint64_t frames = 0, held = 0, judder = 0, backwards = 0, starved = 0;
    double prev = -1;
    std::uint64_t prev_ns = 0;
    for (std::uint64_t now = f.arrive[3]; now < f.arrive.back(); now += 5300000) {   // ~190 fps
        f.receive_until(now);
        const double x = c.step_sched(f, 1.0, 1.5, now);
        if (now > f.arrive[120]) {
            ++frames;
            starved += c.starved;
            const double want = double(now - prev_ns) * 1e-9 * 59.94;
            if (x == prev) ++held;
            if (x < prev) ++backwards;
            if (std::fabs((x - prev) / want - 1.0) > 0.25) ++judder;
        }
        prev = x;
        prev_ns = now;
    }
    CHECK(frames > 1000);
    CHECK(held == 0);
    CHECK(backwards == 0);
    CHECK(judder * 100 < frames * 2);
    CHECK(starved * 100 < frames * 3);
    CHECK_NEAR(c.rate, 59.94, 0.1);
    CHECK(c.margin_ms > 5.0 && c.margin_ms < 8.0);   // p95 of the frames' first ticks' 2..6 ms lags, + 1 ms
}

TEST(no_stamps_falls_back_to_the_arrival_clock) {
    Feed f = steady(200, 1000000000ull, false);
    b3replay::DisplayClockT<Feed> c, ref;
    for (std::uint64_t now = f.arrive[3]; now < f.arrive.back(); now += 3000000) {
        f.receive_until(now);
        const double x = c.step_sched(f, 1.0, 1.5, now);
        const double y = ref.step(f, 1.5, now);
        CHECK(!c.sched_on);
        CHECK(x == y);
        CHECK(c.sched_age_ms < 0);
    }
}

TEST(latch_age_of_the_shown_state) {
    Feed f = steady(10, 1000000000ull, true);
    f.n = 10;
    b3replay::DisplayClockT<Feed> c;
    c.rate = 60.0;
    // x = 4.5: half way between ticks 4 and 5, whose inputs were latched at t0 + 4 T and t0 + 5 T
    const std::uint64_t now = 1000000000ull + 7 * kT;
    CHECK_NEAR(c.latch_age_ms(f, 4.5, now), (7.0 - 4.5) * double(kT) * 1e-6, 1e-6);
    CHECK_NEAR(c.latch_age_ms(f, 9.0, now), (7.0 - 9.0) * double(kT) * 1e-6, 1e-6);
}

namespace {
// fastload1: 120 paced ticks, `fast` ticks of an unpaced load (1 ms apart, no stamps), then paced ticks again from 30 ms
// after the load's last one; the display at `frame_ns` steps, with b3r_fast_forward on from the load's first arrival
// to its last + `late_ns` (the flag crosses threads: the renderer sees it change a little after the game).
struct FfRun { std::uint64_t frames = 0, backwards = 0, not_newest_in_load = 0; bool sched_after = false; double rate_after = 0; };
FfRun fast_load_run(bool low, std::uint64_t frame_ns, std::uint64_t late_ns) {
    Feed f;
    std::uint64_t t = 1000000000ull;
    for (int i = 0; i < 120; ++i, t += kT) { f.sched.push_back(t); f.arrive.push_back(t + 3000000); }
    const std::size_t lo = f.arrive.size();
    for (int i = 0; i < 400; ++i, t += 1000000) { f.sched.push_back(0); f.arrive.push_back(t); }
    const std::size_t hi = f.arrive.size();   // the first paced tick after the load
    const std::uint64_t load_end = t;
    t += 30000000;
    for (int i = 0; i < 300; ++i, t += kT) { f.sched.push_back(t); f.arrive.push_back(t + 3000000); }
    b3replay::DisplayClockT<Feed> c;
    FfRun r;
    double prev = -1;
    for (std::uint64_t now = f.arrive[3]; now < f.arrive.back(); now += frame_ns) {
        f.receive_until(now);
        const bool fast = f.last() >= lo && (f.last() < hi || now < load_end + late_ns) && now >= f.arrive[lo];
        double x;
        if (c.ff_newest(f, fast, low)) x = c.x;
        else x = c.ff_after(f, low ? c.step_sched(f, 1.0, 1.5, now) : c.step(f, 1.5, now));
        ++r.frames;
        if (x < prev - 1e-9) ++r.backwards;
        if (fast && x != double(f.last())) ++r.not_newest_in_load;
        prev = x;
    }
    r.sched_after = c.sched_on;
    r.rate_after = c.rate;
    return r;
}
}  // namespace

TEST(fast_load_newest_then_no_step_back) {
    for (bool low : {true, false})
        for (std::uint64_t frame : {4166667ull, 16666667ull, 6944444ull})
            for (std::uint64_t late : {0ull, 5000000ull, 40000000ull}) {
                const FfRun r = fast_load_run(low, frame, late);
                CHECK(r.frames > 100);
                CHECK(r.not_newest_in_load == 0);
                CHECK(r.backwards == 0);
                if (low) CHECK(r.sched_after);
                CHECK(r.rate_after > 55.0 && r.rate_after < 65.0);
            }
}

TEST_MAIN("display clock (lat1 schedule mode)")
