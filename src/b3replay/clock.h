// The live viewer's display clock (live1 §13.1, db55faa, live2 §20), header-only so that tools/render/b3clocksim.cpp
// can run it on a session's recorded tick arrivals.
#ifndef B3REPLAY_CLOCK_H
#define B3REPLAY_CLOCK_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace b3replay {

// The display position in ticks. Ticks arrive at the game's own rate (60 a second in races, 30 in crashes), so the
// clock measures it instead of assuming 60 (db55faa: with 60 assumed, crashes ran ahead, hit the newest tick and were
// pulled back: a hitch, and twice the delay in time; seen in the first live session).
// live2: ticks also arrive in groups. A frame of N ticks (CB3Game +0x2DA84 = 2: race1 had 60 % of its race ticks in
// 2-tick frames) runs its N polls back to back and then waits N/60 s, so the newest tick jumps by N at once. db55faa's
// clock extrapolated at most 1 tick past the newest arrival, so in 2-tick frames it reached the newest tick before the
// next pair came and stood still (race2: 16.8 % of display frames at the newest tick, speed p1 0.0). Now arrivals less
// than 4 ms apart form one group; the rate comes from the group starts (no aliasing of a fixed tick window over
// pairs), the position extrapolates up to the group size N past the newest arrival, and the delay is the margin beyond
// the group gap (N - 1 + delay ticks behind; a jump-free target in steady state: right after a group it equals the
// value just before it). --clock-db55faa keeps the previous rule (comparisons).
template <class Feed>   // Feed: first(), last(), arrival_ns(i) (LiveFeed; b3clocksim's recorded arrivals)
struct DisplayClockT {
    bool init = false;
    double x = 0;
    std::uint64_t last_ns = 0;
    double rate = 60.0;   // ticks per second, measured
    bool old_rule = false;
    // group tracking (live2)
    std::size_t seen = 0;                                            // last tick index processed
    std::deque<std::pair<std::size_t, std::uint64_t>> starts;        // group start tick, its arrival
    double burst = 1;                                                // ticks per group: the largest of the recent groups
    void track(const Feed& f) {
        const std::size_t lo = f.first(), hi = f.last();
        if (seen < lo || seen > hi) { starts.clear(); seen = lo; starts.push_back({lo, f.arrival_ns(lo)}); }
        for (std::size_t k = seen + 1; k <= hi; ++k)
            if (f.arrival_ns(k) - f.arrival_ns(k - 1) > 4000000ull) {
                starts.push_back({k, f.arrival_ns(k)});
                if (starts.size() > 5) starts.pop_front();
            }
        seen = hi;
        if (starts.size() >= 3) {
            const double span = double(starts.back().second - starts.front().second) * 1e-9;
            if (span > 0) rate = std::clamp(double(starts.back().first - starts.front().first) / span, 15.0, 120.0);
        }
        std::size_t b = 1;
        for (std::size_t i = 0; i + 1 < starts.size(); ++i) b = std::max(b, starts[i + 1].first - starts[i].first);
        burst = double(std::min<std::size_t>(b, 4));   // the game's frames are 1 or 2 ticks; a gap from a load is not a group
    }
    double step(const Feed& f, double delay, std::uint64_t now) {
        const std::size_t latest = f.last();
        double reach = 1.0, behind = delay;
        if (old_rule) {
            if (latest >= f.first() + 8) {
                const double span = double(f.arrival_ns(latest) - f.arrival_ns(latest - 8)) * 1e-9;
                if (span > 0) rate = std::clamp(8.0 / span, 15.0, 120.0);
            }
        } else {
            track(f);
            reach = burst;
            behind = delay + burst - 1.0;
        }
        const double since = (double(now) - double(f.arrival_ns(latest))) * 1e-9 * rate;
        const double target = double(latest) + std::clamp(since, 0.0, reach) - behind;
        const double dt = init ? double(now - last_ns) * 1e-9 : 0.0;
        last_ns = now;
        if (!init || std::fabs(target - x) > 30.0) { x = target; init = true; }
        else { x += dt * rate; x += (target - x) * std::min(1.0, dt * 2.0); }
        starved = x >= double(latest);   // at the newest tick: the next frame must wait for the next arrival
        x = std::clamp(x, double(f.first()), double(latest));
        // age of what is shown: now minus the arrival time of tick x (interpolated between its two ticks' arrivals)
        const std::size_t i = std::size_t(std::floor(x));
        const double fr = x - double(i);
        const double at = i < latest ? double(f.arrival_ns(i)) + fr * (double(f.arrival_ns(i + 1)) - double(f.arrival_ns(i))) : double(f.arrival_ns(i));
        age_ms = (double(now) - at) * 1e-6;
        return x;
    }
    bool starved = false;
    double age_ms = 0;

    // ---- lat1: the schedule clock ----
    // Each tick of the in-process feed carries the host time its input was latched (Feed::sched_ns, b3r_submit_at). The
    // game releases those latches on a steady 1/60 s clock, while the ticks' arrivals jitter with the guest's own work
    // after each latch (and, with the pacing wait at the poll, arrive within a few ms of it). This clock shows tick r's
    // state blending in from S_r + m and fully at S_r + m + T: x = L - 1 + (now - S_L - m) / T for the newest tick L. The
    // margin m is the 95th percentile of the recent arrival lags (arrival - stamp; with a lead, down to the 80th at lead 1)
    // plus 1 ms, at least `floor_ms`, so the
    // next tick is almost always there before the blend needs it: no prediction, steady motion, and the shortest delay the
    // arrivals allow. `lead` (0..1 ticks) shifts the display forward: the newest tick is shown fully from S_L + m and the
    // motion is extrapolated from the last two ticks up to 1.25 x `lead` ticks past it until the next one arrives (LiveScene::frame,
    // frame_at with frac > 1). Without stamps it is step() with `fallback_delay`.
    double margin_ms = 0;          // m in use
    double sched_age_ms = -1;      // now minus the latch time of the shown state (-1: no stamps)
    double reach_factor = 1.25;    // the extrapolation clamp past the newest tick, in units of the lead (perf1: b3latsim)
    bool sched_on = false;         // the last step used the stamps
    std::size_t lag_seen = 0;      // last tick whose lag was taken
    std::deque<double> lags;       // recent arrival lags, ms
    bool have_stamps(const Feed& f, std::size_t i) const { return i > f.first() && f.sched_ns(i) != 0 && f.sched_ns(i - 1) != 0; }
    // split2p (a report: split screen choppy at 150-200 FPS): the stamp tick i is shown by. A game frame of N ticks (split
    // screen for the whole race, a player's crash: N = 2) latches its N polls back to back and then waits N VBlanks, so its
    // ticks came with one stamp and the next frame's N ticks a whole frame later. Shown by those stamps, x reached the newest
    // tick half way through each frame, stood still there for a tick and then jumped (30 steps a second). A tick latched less
    // than half a VBlank after the one before is one frame's later tick: it is shown one VBlank after it (at most 3 ticks
    // back to the frame's first). 1-tick frames are unchanged (their stamps are a VBlank apart).
    static constexpr double kVblankNs = 1e9 / 59.94;   // native_play's pacing (NTSC)
    std::uint64_t vstamp(const Feed& f, std::size_t i) const {
        const std::uint64_t s = f.sched_ns(i);
        if (s == 0) return 0;
        std::size_t j = i;
        while (j > f.first() && i - j < 3 && f.sched_ns(j - 1) != 0 &&
               double(f.sched_ns(j)) - double(f.sched_ns(j - 1)) < 0.5 * kVblankNs)
            --j;
        return std::max<std::uint64_t>(s, f.sched_ns(j) + std::uint64_t(double(i - j) * kVblankNs));
    }
    double step_sched(const Feed& f, double floor_ms, double fallback_delay, std::uint64_t now, double lead = 0) {
        const std::size_t latest = f.last();
        if (!have_stamps(f, latest)) { sched_on = false; sched_age_ms = -1; return step(f, fallback_delay, now); }
        if (lag_seen < f.first() || lag_seen > latest) lag_seen = f.first();
        bool fresh = false;
        for (std::size_t k = lag_seen + 1; k <= latest; ++k)
            if (f.sched_ns(k) != 0) {
                // split2p: against the stamp the tick is shown by; a frame's later tick is there long before (not counted)
                const double lag = (double(f.arrival_ns(k)) - double(vstamp(f, k))) * 1e-6;
                if (lag >= 0 || vstamp(f, k) == f.sched_ns(k)) {
                    lags.push_back(lag);
                    if (lags.size() > 120) lags.pop_front();
                }
                fresh = true;
            }
        lag_seen = latest;
        if (fresh || !sched_on) {   // the margin moves 5 % of the way per new tick (no visible steps)
            std::vector<double> v(lags.begin(), lags.end());
            // the 95th percentile without a lead; with one, the 80th at lead 1, the later ticks covered by up to a quarter tick
            // more of extrapolation (the clamp below)
            const double pq = 0.95 - 0.15 * std::clamp(lead, 0.0, 1.0);
            const std::size_t q = v.empty() ? 0 : std::min(v.size() - 1, std::size_t(double(v.size()) * pq));
            if (!v.empty()) std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(q), v.end());
            const double want = std::max(floor_ms, (v.empty() ? 16.7 : v[q]) + 1.0);
            margin_ms = !sched_on || margin_ms <= 0 ? want : margin_ms + 0.05 * (want - margin_ms);
        }
        // the tick rate from the stamps (they advance one tick of game time per tick, crash frames included)
        std::size_t k = latest;
        while (k > f.first() && latest - k < 30 && f.sched_ns(k - 1) != 0) --k;
        if (latest > k) {
            const double span = (double(vstamp(f, latest)) - double(vstamp(f, k))) * 1e-9;
            if (span > 0) rate = std::clamp(double(latest - k) / span, 15.0, 120.0);
        }
        // the stamps are steady (the pacer's deadlines), so the clock is the target itself, clamped to the ticks there are
        x = double(latest) - 1.0 + lead + ((double(now) - double(vstamp(f, latest))) * 1e-9 - margin_ms * 1e-3) * rate;
        last_ns = now;
        init = false;   // a switch back to step() starts from its own target
        sched_on = true;
        const double reach_ahead = lead * reach_factor;   // a tick later than the margin: extrapolated a little further, not held
        starved = x >= double(latest) + reach_ahead;
        x = std::clamp(x, double(f.first()), double(latest) + reach_ahead);
        age_ms = latch_age_ms(f, x, now);
        sched_age_ms = age_ms;
        return x;
    }
    // ---- fastload1: FAST LOADING (src/b3replay/fastforward.h, tools/native/fast_load.h) ----
    // While a load runs unpaced, its ticks arrive many times faster than real time with no latch stamp: the display shows
    // the newest tick at each frame (the loading screen's animations run as fast as the load). After it, the newest
    // again until two stamped ticks are there (`want_stamps`: the schedule clock needs them; the arrival clock does not),
    // and the clock's own position is held at the last one shown until it passes it: no step back at the load's end.
    bool ff_hold = false;
    double ff_floor = -1;
    // true: x is set (the newest tick); false: the caller steps its own rule, then ff_after() on its result
    bool ff_newest(const Feed& f, bool fast, bool want_stamps) {
        if (fast) ff_hold = true;
        else if (ff_hold && (!want_stamps || have_stamps(f, f.last()))) ff_hold = false;
        if (!ff_hold) return false;
        x = double(f.last());
        init = false;
        starved = true;
        starts.clear();   // the load's arrivals are no group rate
        ff_floor = x;
        return true;
    }
    double ff_after(const Feed& f, double xs) {
        if (ff_floor > double(f.last()) || ff_floor < double(f.first())) ff_floor = -1;   // a new segment
        if (ff_floor >= 0) {
            if (xs < ff_floor) xs = ff_floor;
            else ff_floor = -1;
            x = xs;
        }
        return xs;
    }
    // now minus the latch time of the input behind the state at x (stamps interpolated); -1 without stamps
    // (past the newest tick, the extrapolated part counts as ticks at the measured rate)
    double latch_age_ms(const Feed& f, double xs, std::uint64_t now) const {
        const std::size_t i = std::min(std::size_t(std::floor(std::max(xs, double(f.first())))), f.last());
        if (f.sched_ns(i) == 0) return -1;
        const double fr = xs - double(i);
        double at = double(f.sched_ns(i));
        if (i < f.last() && f.sched_ns(i + 1) != 0) at += fr * (double(f.sched_ns(i + 1)) - double(f.sched_ns(i)));
        else at += fr * 1e9 / rate;
        return (double(now) - at) * 1e-6;
    }
};

}  // namespace b3replay

#endif
