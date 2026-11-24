// stutter: how evenly the displayed game time moves, per drawn race frame (the window and the bench), for the run log.
//
// Interpolation draws the game at position x (ticks, fractional) at each display frame. With a steady game (one tick per
// 1/59.94 s) x should advance by (the frame's wall time) x (the tick rate): "uneven motion" with clean frame times is
// x stepping too far, too little, standing still or going back. Per frame: its step against the ideal one, as a share of
// the ideal; held frames (x unchanged for more than 2 ms of wall time in a row while racing: at 600 fps a frame is
// under 2 ms, so a run of still frames counts by its time, motionend); backward steps. And the ticks'
// latch stamps' intervals (the schedule the display clock follows: steady stamps are steady motion).
#ifndef B3REPLAY_MOTION_STATS_H
#define B3REPLAY_MOTION_STATS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace b3replay {

struct MotionStats {
    static constexpr std::size_t kMax = 1u << 22;
    std::vector<float> err;        // per frame: |step - ideal| / ideal
    std::vector<float> stamp_ms;   // per new tick: its latch stamp minus the previous tick's (ms)
    std::uint64_t frames = 0, held = 0, back = 0;
    double held_s = 0;
    double last_x = -1;
    std::uint64_t last_ns = 0;
    // a drawn race frame at wall time `now_ns`, game position `x` (ticks), the tick rate `rate` (ticks / s; 0: 59.94)
    void frame(std::uint64_t now_ns, double x, double rate) {
        if (rate <= 0) rate = 59.94;
        if (last_ns && now_ns > last_ns && last_x >= 0) {
            const double dt = double(now_ns - last_ns) * 1e-9;
            if (dt < 0.1) {   // (a longer gap: a pause, a load; not motion)
                const double dx = x - last_x, ideal = dt * rate;
                ++frames;
                if (dx < 0) ++back;
                if (dx >= 0 && dx < 1e-9) {
                    still_s_ += dt;
                    if (still_s_ > 0.002) { ++held; held_s += dt; }
                } else {
                    still_s_ = 0;
                }
                if (err.size() < kMax) err.push_back(float(std::fabs(dx - ideal) / ideal));
            }
        }
        last_ns = now_ns;
        last_x = x;
    }
    void reset_run() { last_ns = 0; last_x = -1; still_s_ = 0; }
    double still_s_ = 0;   // the current run of still frames' wall time (motionend)   // a load / segment change: the next frame starts afresh
    void stamp(double interval_ms) {
        if (interval_ms > 0 && interval_ms < 200 && stamp_ms.size() < kMax) stamp_ms.push_back(float(interval_ms));
    }
    // the ticks received since the last call: each stamped one's interval from the tick before it (Feed: first(),
    // last(), sched_ns(i))
    template <class Feed> void ticks(const Feed& f) {
        const std::size_t last = f.last();
        if (seen_)
            for (std::size_t k = std::max(seen_ + 1, f.first() + 1); k <= last; ++k)
                if (f.sched_ns(k) && f.sched_ns(k - 1)) stamp((double(f.sched_ns(k)) - double(f.sched_ns(k - 1))) * 1e-6);
        seen_ = last;
    }
    std::size_t seen_ = 0;
    static double pct(std::vector<float> v, double q) {
        if (v.empty()) return 0;
        const std::size_t i = std::min(v.size() - 1, std::size_t(double(v.size()) * q / 100.0));
        std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(i), v.end());
        return double(v[i]);
    }
    std::string summary(const char* what) const {
        char b[512];
        std::snprintf(b, sizeof b,
                      "[b3live] %s race motion: %llu frames; the game-time step against the ideal p50 %.1f %% p95 %.1f %% p99 %.1f %%; "
                      "held %llu frames (%.2f s), backward steps %llu; tick stamps %zu, interval ms p1 %.2f p50 %.2f p99 %.2f",
                      what, static_cast<unsigned long long>(frames), 100.0 * pct(err, 50), 100.0 * pct(err, 95), 100.0 * pct(err, 99),
                      static_cast<unsigned long long>(held), held_s, static_cast<unsigned long long>(back), stamp_ms.size(),
                      pct(stamp_ms, 1), pct(stamp_ms, 50), pct(stamp_ms, 99));
        return b;
    }
};

}  // namespace b3replay

#endif
