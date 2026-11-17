// b3latsim (perf1, docs/native-renderer.md §33): the low-latency display clock (src/b3replay/clock.h step_sched) replayed
// on a run's recorded ticks, without the game or a GPU, for several leads / clamps at once, and scored against the true
// camera path. Inputs: the renderer's latency log (--latency-log; LAT_LOG=1: T rows = poll, arrival, latch stamp, drain
// time) and the ticks' camera positions (b3replay --profile ... A:B:1 with B3PROFILE_NODRAW=1: cam_x/y/z at alpha 0).
//   b3latsim <lat-render.csv> <cams.csv> --from P --to P [--fps N (display frames per second, default 300; 0 = the log's
//            own frame times)] [--lead L,L,...] [--reach R,R,...] [--margin MS] [--smooth-delay D]
// Per variant, over the displayed frames of polls P..P (camera cuts and their neighbours left out):
//   speed     the display time's advance per frame against wall time (Δx / Δt / rate): p1 / p5 / p95 / p99, the share
//             of frames off by more than 25 % (judder) and held frames (Δx = 0 while the game runs)
//   error     |shown camera - true camera at the same display time| (extrapolation misses; interpolation is exact): p50 /
//             p99 / max in mm
//   snap      the change of that error from one frame to the next (what the eye sees as a jump when the next tick
//             corrects an extrapolated frame): p99 / max in mm, frames with a snap over 5 / 20 mm
//   lag       now - latch time of the shown state (ms, step_sched's age): p50 / p95
// "smooth" is the old arrival clock (DisplayClock::step, --latency smooth) on the same arrivals, for comparison.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/b3replay/clock.h"

namespace {

struct TickRow { long poll; std::uint64_t arrival, stamp, drained; };
struct Feed {
    const std::vector<TickRow>* t = nullptr;
    std::size_t lo = 0, hi = 0;
    std::size_t first() const { return lo; }
    std::size_t last() const { return hi; }
    std::uint64_t arrival_ns(std::size_t i) const { return (*t)[i].arrival; }
    std::uint64_t sched_ns(std::size_t i) const { return (*t)[i].stamp; }
};
using Clock = b3replay::DisplayClockT<Feed>;

double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, std::size_t(p / 100.0 * double(v.size())))];
}
std::vector<double> list(const char* s) {
    std::vector<double> v;
    std::stringstream ss(s);
    std::string x;
    while (std::getline(ss, x, ',')) v.push_back(std::atof(x.c_str()));
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: b3latsim <lat-render.csv> <cams.csv> --from P --to P [--fps N] [--lead L,...] [--reach R,...] [--margin MS] [--smooth-delay D]\n");
        return 2;
    }
    long from = 0, to = 1L << 30;
    double fps = 300, margin = 0, smooth_delay = 1.5;
    std::vector<double> leads{1.0, 0.75, 0.5, 0.25, 0.0}, reaches{1.25}, decays{0};
    for (int i = 3; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--from") from = std::atol(argv[i + 1]);
        else if (a == "--to") to = std::atol(argv[i + 1]);
        else if (a == "--fps") fps = std::atof(argv[i + 1]);
        else if (a == "--lead") leads = list(argv[i + 1]);
        else if (a == "--reach") reaches = list(argv[i + 1]);
        else if (a == "--decay") decays = list(argv[i + 1]);   // ms: the correction blend (0 = none)
        else if (a == "--margin") margin = std::atof(argv[i + 1]);
        else if (a == "--smooth-delay") smooth_delay = std::atof(argv[i + 1]);
        else if (a == "--session") ++i;
    }
    // the ticks (one per poll) and the log's frame times
    std::vector<TickRow> ticks;
    std::vector<std::uint64_t> log_frames;
    std::map<std::uint64_t, double> log_x;   // the logged display position (poll units) per frame time: the sim's check
    {
        std::ifstream f(argv[1]);
        std::string line;
        while (std::getline(f, line)) {
            if (line.size() < 2) continue;
            std::stringstream ss(line);
            std::string c;
            std::vector<std::string> v;
            while (std::getline(ss, c, ',')) v.push_back(c);
            if (v[0] == "T" && v.size() >= 5) {
                const long p = std::atol(v[1].c_str());
                if (p >= from && p <= to) ticks.push_back({p, std::stoull(v[2]), std::stoull(v[3]), std::stoull(v[4])});
            } else if (v[0] == "F" && v.size() >= 9) {
                const long shown = std::atol(v[5].c_str());
                if (shown >= from && shown <= to) {
                    log_frames.push_back(std::stoull(v[2]));
                    log_x[std::stoull(v[2])] = double(shown) + std::atof(v[6].c_str()) * 1e-6;
                }
            }
        }
    }
    // --session <frames.csv> <pads.log>: a windowed session's own timeline instead of the log's T rows (no LAT_LOG there):
    // with the pacing wait at the poll, frames.csv's line of the vblank a poll ran in is written when its wait ends, right
    // before the latch, so latch(P) ~ t_ns and the record's hand-over (the poll's start) ~ t_ns - wait_us. Arrival(P) =
    // that hand-over; stamp(P) = latch(P - 1), back-to-back polls one tick apart (b3port_tick_stamp).
    for (int i = 3; i + 2 < argc; ++i)
        if (std::string(argv[i]) == "--session") {
            ticks.clear();
            std::map<long, std::pair<std::uint64_t, std::uint64_t>> vb;   // vblank -> (t_ns, wait_ns)
            std::ifstream fv(argv[i + 1]);
            std::string line;
            std::getline(fv, line);
            while (std::getline(fv, line)) {
                std::stringstream ss(line);
                std::string c;
                std::vector<std::string> v;
                while (std::getline(ss, c, ',')) v.push_back(c);
                if (v.size() >= 3) vb[std::atol(v[0].c_str())] = {std::stoull(v[1]), std::stoull(v[2]) * 1000ull};
            }
            std::ifstream fp(argv[i + 2]);
            std::uint64_t last_latch = 0, last_stamp = 0;
            while (std::getline(fp, line)) {
                if (line.size() < 2 || line[0] != 'P') continue;
                std::stringstream ss(line.substr(2));
                long poll = 0, v = 0;
                ss >> poll >> v;
                const auto it = vb.find(v);
                if (it == vb.end()) continue;
                const std::uint64_t latch = it->second.first, arrival = latch - std::min(latch, it->second.second);
                std::uint64_t stamp = last_latch;
                constexpr std::int64_t kP = 16666667;
                if (last_stamp != 0 && std::int64_t(stamp - last_stamp) < kP / 2 && std::int64_t(stamp - last_stamp) > -kP) stamp = last_stamp + kP;
                if (poll >= from && poll <= to && stamp != 0) ticks.push_back({poll, std::max(arrival, stamp), stamp, std::max(arrival, stamp)});
                if (stamp != 0) last_stamp = stamp;
                last_latch = latch;
            }
        }
    std::sort(ticks.begin(), ticks.end(), [](const TickRow& a, const TickRow& b) { return a.poll < b.poll; });
    ticks.erase(std::unique(ticks.begin(), ticks.end(), [](const TickRow& a, const TickRow& b) { return a.poll == b.poll; }), ticks.end());
    std::map<long, std::array<double, 3>> cam;
    {
        std::ifstream f(argv[2]);
        std::string line, head;
        std::vector<std::string> cols;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string c;
            std::vector<std::string> v;
            while (std::getline(ss, c, ',')) v.push_back(c);
            if (cols.empty()) { cols = v; continue; }
            auto col = [&](const char* n) { for (std::size_t k = 0; k < cols.size(); ++k) if (cols[k] == n) return k; return std::size_t(0); };
            static const std::size_t ip = col("poll"), ia = col("alpha"), ix = col("cam_x"), iy = col("cam_y"), iz = col("cam_z");
            if (std::atof(v[ia].c_str()) != 0.0) continue;
            cam[std::atol(v[ip].c_str())] = {std::atof(v[ix].c_str()), std::atof(v[iy].c_str()), std::atof(v[iz].c_str())};
        }
    }
    if (ticks.size() < 10 || cam.empty()) { std::fprintf(stderr, "b3latsim: %zu ticks, %zu cameras in range\n", ticks.size(), cam.size()); return 1; }
    // camera per tick index; cuts (a jump over 3 m between ticks) mark both ticks
    const std::size_t n = ticks.size();
    std::vector<std::array<double, 3>> P(n);
    std::vector<bool> ok(n, false), cut(n, false);
    for (std::size_t i = 0; i < n; ++i) if (auto it = cam.find(ticks[i].poll); it != cam.end()) { P[i] = it->second; ok[i] = true; }
    for (std::size_t i = 1; i < n; ++i) {
        if (!ok[i] || !ok[i - 1] || ticks[i].poll != ticks[i - 1].poll + 1) { cut[i] = cut[i - 1] = true; continue; }
        double d = 0;
        for (int k = 0; k < 3; ++k) d += (P[i][k] - P[i - 1][k]) * (P[i][k] - P[i - 1][k]);
        if (std::sqrt(d) > 3.0) cut[i] = cut[i - 1] = true;
    }
    auto at = [&](double x, std::size_t newest, std::array<double, 3>& out) {   // the renderer's camera at x with ticks up to newest
        if (x <= double(newest)) {
            const std::size_t i = std::min(std::size_t(std::floor(x)), newest);
            const double a = x - double(i);
            const std::size_t j = std::min(i + 1, newest);
            for (int k = 0; k < 3; ++k) out[k] = P[i][k] + a * (P[j][k] - P[i][k]);
        } else {
            const std::size_t i = newest - 1;
            const double a = x - double(i);
            for (int k = 0; k < 3; ++k) out[k] = P[i][k] + a * (P[newest][k] - P[i][k]);
        }
    };
    // the frame times: a steady rate (the window's uncapped loop) or the log's own
    std::vector<std::uint64_t> frames;
    const std::uint64_t t0 = ticks[30].drained, t1 = ticks[n - 31].drained;
    if (fps > 0)
        for (std::uint64_t t = t0; t < t1; t += std::uint64_t(1e9 / fps)) frames.push_back(t);
    else
        for (std::uint64_t t : log_frames) if (t >= t0 && t < t1) frames.push_back(t);
    std::sort(frames.begin(), frames.end());
    std::printf("b3latsim: polls %ld..%ld, %zu ticks, %zu frames (%s)\n", ticks.front().poll, ticks.back().poll, n, frames.size(),
                fps > 0 ? "steady" : "the log's frame times");
    std::printf("%-26s %7s %7s %7s %6s | %7s %7s | %7s %6s %6s | %7s %7s %6s %6s | %6s %6s\n", "variant", "spd p1", "p99", "judder%",
                "held%", "err p50", "p99", "vj p99", ">2m/s", ">10m/s", "snp p99", "max", ">5mm", ">20mm", "lag50", "lag95");
    struct Variant { std::string name; bool smooth; double lead, reach, decay_ms = 0; bool hold = false; };
    std::vector<Variant> vars;
    for (double d : decays) for (double r : reaches) for (double l : leads) {
        char b[64];
        if (d > 0) std::snprintf(b, sizeof b, "low lead %.2f r %.2f blend %.0f", l, r, d);
        else std::snprintf(b, sizeof b, "low lead %.2f reach %.2f", l, r);
        vars.push_back({b, false, l, r, d});
    }
    // lat1 as shipped (before perf1): the clock as above, but past the newest tick the poses held at it (Mat34 lerp
    // returned b for t >= 1)
    vars.push_back({"lat1 shipped (hold)", false, leads.empty() ? 1.0 : 1.0, reaches.empty() ? 1.25 : reaches[0], 0, true});
    {
        char b[64];
        std::snprintf(b, sizeof b, "smooth delay %.1f", smooth_delay);
        vars.push_back({b, true, 0, 0});
    }
    for (const Variant& var : vars) {
        Clock c;
        c.reach_factor = var.reach;
        Feed f{&ticks, 0, 0};
        std::size_t newest = 0;
        std::vector<double> speed, err, snap, lag, vjump;
        long vj2 = 0, vj10 = 0;
        double pv_s[3] = {0, 0, 0}, pv_t[3] = {0, 0, 0}, ps[3] = {0, 0, 0}, ptr[3] = {0, 0, 0};
        bool have_v = false;
        long judder = 0, held = 0, counted = 0, snap5 = 0, snap20 = 0;
        double px = -1, pe[3] = {0, 0, 0};
        std::uint64_t pt = 0;
        bool have_prev = false;
        // the correction blend: when a new tick arrives, the jump between the pose computed with the old ticks and the one
        // with the new tick is kept as an offset that decays exponentially (time constant decay_ms)
        double resid[3] = {0, 0, 0};
        std::size_t prev_newest = 0;
        std::vector<double> check;   // |sim - log| in ticks at the log's own frame times
        for (std::uint64_t t : frames) {
            // a tick is there from its drain in the logged run; at a steady rate (a window drains every frame) from its arrival
            while (newest + 1 < n && (fps > 0 ? ticks[newest + 1].arrival : ticks[newest + 1].drained) <= t) ++newest;
            if (newest < 2) continue;
            f.hi = newest;
            f.lo = newest > 1100 ? newest - 1100 : 0;
            double x = var.smooth ? c.step(f, smooth_delay, t) : c.step_sched(f, margin, smooth_delay, t, var.lead);
            x = std::clamp(x, double(f.lo), double(newest) + (var.smooth ? 0.0 : var.lead * var.reach));
            if (fps <= 0) if (auto lx = log_x.find(t); lx != log_x.end()) check.push_back(std::fabs(double(ticks[0].poll) + x - lx->second));
            const std::size_t i = std::size_t(std::floor(x));
            const bool clean = i >= 1 && i + 2 < n && !cut[i] && !cut[std::min(i + 1, n - 1)] && !cut[newest] && !cut[newest - 1];
            std::array<double, 3> shown{}, truth{};
            at(var.hold ? std::min(x, double(newest)) : x, newest, shown);
            if (var.decay_ms > 0 && have_prev) {
                const double k = std::exp(-double(t - pt) * 1e-6 / var.decay_ms);
                for (int j = 0; j < 3; ++j) resid[j] *= k;
                if (newest != prev_newest && prev_newest >= 1) {
                    std::array<double, 3> old{};
                    at(x, prev_newest, old);
                    for (int j = 0; j < 3; ++j) resid[j] += old[j] - shown[j];
                }
                for (int j = 0; j < 3; ++j) shown[j] += resid[j];
            } else {
                resid[0] = resid[1] = resid[2] = 0;
            }
            prev_newest = newest;
            at(std::min(x, double(n - 1)), n - 1, truth);
            double e[3], el = 0;
            for (int k = 0; k < 3; ++k) { e[k] = shown[k] - truth[k]; el += e[k] * e[k]; }
            el = std::sqrt(el);
            if (clean && have_prev) {
                const double dt = double(t - pt) * 1e-9, dx = x - px;
                const double s = dx / (dt * 60.0);
                speed.push_back(s);
                ++counted;
                if (std::fabs(s - 1.0) > 0.25) ++judder;
                if (dx == 0.0) ++held;
                err.push_back(el * 1000.0);
                double sn = 0;
                for (int k = 0; k < 3; ++k) sn += (e[k] - pe[k]) * (e[k] - pe[k]);
                sn = std::sqrt(sn) * 1000.0;
                snap.push_back(sn);
                if (sn > 5) ++snap5;
                if (sn > 20) ++snap20;
                // the velocity discontinuity: the change of the shown camera's velocity from frame to frame, less the true
                // path's (m/s): what the eye sees as judder (a correction or an overshoot turning back)
                double vs[3], vt[3], vjj = 0;
                for (int k = 0; k < 3; ++k) { vs[k] = (shown[k] - ps[k]) / dt; vt[k] = (truth[k] - ptr[k]) / dt; }
                if (have_v) {
                    for (int k = 0; k < 3; ++k) { const double d = (vs[k] - pv_s[k]) - (vt[k] - pv_t[k]); vjj += d * d; }
                    vjj = std::sqrt(vjj);
                    vjump.push_back(vjj);
                    if (vjj > 2) ++vj2;
                    if (vjj > 10) ++vj10;
                }
                for (int k = 0; k < 3; ++k) { pv_s[k] = vs[k]; pv_t[k] = vt[k]; }
                have_v = true;
                if (!var.smooth && c.sched_age_ms >= 0) lag.push_back(c.sched_age_ms);
                else if (var.smooth) lag.push_back(c.latch_age_ms(f, x, t));
            }
            if (!(clean && have_prev)) have_v = false;
            for (int k = 0; k < 3; ++k) { ps[k] = shown[k]; ptr[k] = truth[k]; }
            px = x;
            pt = t;
            for (int k = 0; k < 3; ++k) pe[k] = e[k];
            have_prev = clean;
        }
        std::printf("%-26s %7.3f %7.3f %7.2f %6.2f | %7.2f %7.1f | %7.2f %6ld %6ld | %7.2f %7.1f %6ld %6ld | %6.1f %6.1f\n", var.name.c_str(),
                    pct(speed, 1), pct(speed, 99), counted ? 100.0 * double(judder) / double(counted) : 0.0,
                    counted ? 100.0 * double(held) / double(counted) : 0.0, pct(err, 50), pct(err, 99), pct(vjump, 99), vj2, vj10, pct(snap, 99), pct(snap, 100),
                    snap5, snap20, pct(lag, 50), pct(lag, 95));
        if (!check.empty())
            std::printf("    against the log's display positions (ticks): p50 %.4f p99 %.4f max %.3f over %zu frames\n", pct(check, 50), pct(check, 99), pct(check, 100), check.size());
    }
    return 0;
}
