// b3clocksim (live2, docs/native-renderer.md §20): the live viewer's display clock (src/b3replay/clock.h) run on a play
// session's recorded tick arrivals, without the game or a GPU. Arrival of poll k = the host release time of the guest
// vblank it ran in (pads.log "P <poll> <vblank> ..." with the session's frames.csv "vblank,t_ns,..." from pace1's
// frame log), i.e. what the live ring delivered in that session up to the emulation time inside one vblank.
//   b3clocksim <session dir> [--fps N] [--delay D] [--from P --to P]...   (ranges: race segments; default all polls)
// For each rule (db55faa and live2's groups) and tick-rate class: display frames, the share at the newest tick, steps
// backwards, speed / tick-rate percentiles, the age of the shown state (now - arrival of tick x).
#include <algorithm>
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
struct Feed {
    const std::vector<std::uint64_t>* t = nullptr;   // arrival per tick index
    std::size_t lo = 0, hi = 0;
    std::size_t first() const { return lo; }
    std::size_t last() const { return hi; }
    std::uint64_t arrival_ns(std::size_t i) const { return (*t)[i]; }
};
double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, std::size_t(p / 100.0 * double(v.size())))];
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: b3clocksim <session dir> [--fps N] [--delay D] [--from P --to P]...\n"); return 2; }
    const std::string dir = argv[1];
    double fps = 500, delay = 1.5;
    std::vector<std::pair<long, long>> ranges;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--fps") fps = std::atof(argv[i + 1]);
        else if (a == "--delay") delay = std::atof(argv[i + 1]);
        else if (a == "--from") ranges.push_back({std::atol(argv[i + 1]), -1});
        else if (a == "--to" && !ranges.empty()) ranges.back().second = std::atol(argv[i + 1]);
    }
    std::map<long, std::uint64_t> vb_t;
    {
        std::ifstream f(dir + "/frames.csv");
        std::string line;
        std::getline(f, line);
        while (std::getline(f, line)) {
            long vb; unsigned long long t;
            if (std::sscanf(line.c_str(), "%ld,%llu", &vb, &t) == 2) vb_t[vb] = t;
        }
    }
    std::vector<std::pair<long, long>> polls;   // (poll, vblank)
    {
        std::ifstream f(dir + "/pads.log");
        std::string line;
        while (std::getline(f, line)) {
            long p, v;
            if (line.size() > 2 && line[0] == 'P' && std::sscanf(line.c_str() + 1, "%ld %ld", &p, &v) == 2) polls.push_back({p, v});
        }
    }
    if (vb_t.empty() || polls.empty()) { std::fprintf(stderr, "no frames.csv / pads.log in %s\n", dir.c_str()); return 1; }
    if (ranges.empty()) ranges.push_back({polls.front().first, polls.back().first});
    std::printf("session %s: %zu polls, %zu vblanks; display %.0f fps, delay %.1f ticks\n", dir.c_str(), polls.size(), vb_t.size(), fps, delay);
    for (const auto& [from, to] : ranges) {
        std::vector<std::uint64_t> t;
        for (const auto& [p, v] : polls) {
            if (p < from || p > to) continue;
            auto it = vb_t.find(v);
            if (it == vb_t.end()) continue;
            t.push_back(std::max(it->second, t.empty() ? 0 : t.back()));
        }
        if (t.size() < 100) continue;
        // groups as the clock sees them, for the report
        std::map<std::size_t, long> group_sizes;
        std::size_t g = 1;
        for (std::size_t k = 1; k < t.size(); ++k) { if (t[k] - t[k - 1] > 4000000ull) { ++group_sizes[g]; g = 1; } else ++g; }
        std::printf("polls %ld..%ld: %zu ticks over %.1f s; arrival groups by size:", from, to, t.size(), double(t.back() - t.front()) * 1e-9);
        for (const auto& [sz, n] : group_sizes) std::printf(" %zu x%ld", sz, n);
        std::printf("\n");
        for (int rule = 0; rule < 2; ++rule) {
            b3replay::DisplayClockT<Feed> clock;
            clock.old_rule = rule == 0;
            struct Stats { std::vector<double> age, speed; long frames = 0, starved = 0, back = 0; } st[2];
            std::vector<double> holds;   // hold events: consecutive display frames at the newest tick, their duration in ms
            long run = 0;
            Feed f;
            f.t = &t;
            const std::uint64_t period = std::uint64_t(1e9 / fps);
            double px = -1;
            std::uint64_t pnow = 0;
            std::size_t hi = 0;
            for (std::uint64_t now = t.front() + 30 * period; now < t.back(); now += period) {
                while (hi + 1 < t.size() && t[hi + 1] <= now) ++hi;
                f.hi = hi;
                f.lo = hi > 1200 ? hi - 1200 : 0;
                const double x = clock.step(f, delay, now);
                Stats& s = st[clock.rate < 45.0 ? 1 : 0];
                ++s.frames;
                s.starved += clock.starved;
                if (clock.starved) ++run;
                else if (run) { holds.push_back(double(run) * 1000.0 / fps); run = 0; }
                s.age.push_back(clock.age_ms);
                if (pnow) { if (x < px) ++s.back; s.speed.push_back((x - px) / (double(now - pnow) * 1e-9 * clock.rate)); }
                px = x;
                pnow = now;
            }
            {
                long over8 = 0;
                for (double h : holds) over8 += h >= 8.0;
                const double minutes = double(t.back() - t.front()) * 1e-9 / 60.0;
                std::printf("  %-13s hold events (display stopped at the newest tick): %zu, %.1f per minute, %ld of >= 8 ms (%.1f per minute); duration ms p50/p99/max %.1f/%.1f/%.1f\n",
                            rule ? "live2 groups" : "db55faa", holds.size(), double(holds.size()) / minutes, over8, double(over8) / minutes, pct(holds, 50), pct(holds, 99), pct(holds, 100));
            }
            for (int k = 0; k < 2; ++k) {
                const Stats& s = st[k];
                if (!s.frames) continue;
                std::printf("  %-13s %-17s %7ld frames: at the newest tick %5.2f %%, backwards %ld; speed p0.1/p1/p50/p99/p99.9 %.2f/%.2f/%.3f/%.2f/%.2f;"
                            " age ms p1/p50/p99/max %.1f/%.1f/%.1f/%.1f\n",
                            rule ? "live2 groups" : "db55faa", k ? "30 Hz ticks" : "60 Hz ticks", s.frames, 100.0 * double(s.starved) / double(s.frames), s.back,
                            pct(s.speed, 0.1), pct(s.speed, 1), pct(s.speed, 50), pct(s.speed, 99), pct(s.speed, 99.9), pct(s.age, 1), pct(s.age, 50),
                            pct(s.age, 99), pct(s.age, 100));
            }
        }
    }
    return 0;
}
