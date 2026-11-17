// traffic-popin (2026-11-10: AI / traffic cars still pop in): traffic fading in where it appears, out where the
// game removes it, and towards the 275 m draw radius (declarations and the reasoning: view.h). Renderer only.
#include "view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace b3replay {

namespace {

constexpr float kSlotJump2 = 20.0f * 20.0f;   // m^2 between two ticks: further means the slot holds another car
constexpr std::size_t kSinceMaxTicks = 240;   // how far back a run is followed (a stopped clock: the pause's held ticks)

float dist2(const float a[3], const float b[3]) {
    float s = 0;
    for (int k = 0; k < 3; ++k) s += (a[k] - b[k]) * (a[k] - b[k]);
    return s;
}

// The vehicle `v` (of a later tick) in `p`'s list: same guest object and model; `hint` is its index in the later tick
// (the order is stable: race cars, the traffic slots ascending, crashed vehicles)
const Vehicle* find_vehicle(const Tick& p, const Vehicle& v, std::size_t hint) {
    if (hint < p.vehicles.size() && p.vehicles[hint].id == v.id && p.vehicles[hint].data == v.data) return &p.vehicles[hint];
    for (const auto& u : p.vehicles) if (u.id == v.id && u.data == v.data) return &u;
    return nullptr;
}

// split2 (§29.6): view 1 = player 2's in a split screen (its own camera's 275 m: Vehicle::cull2)
bool within_radius(const Vehicle& v, int view = 0) {
    return v.kind == VehicleKind::kTraffic && (view == 0 ? v.cull : v.cull2) != Vehicle::kFar;
}

// Where the unbroken in-radius run of vehicle k of tick i begins, by looking back (the probe and the tests; frames use
// the per-tick results below): 0 = it has run for at least the fade time or since the timeline's first tick (no fade),
// 1 = it begins at tick *start (and `how` says why it was not there at *start - 1: 0 spawn, 1 out of range, 2 a jump)
int run_start(const Timeline& tl, std::size_t i, std::size_t k, std::size_t* start, int* how = nullptr) {
    const Tick& t = tl.at(i);
    if (k >= t.vehicles.size() || !within_radius(t.vehicles[k])) return 0;
    const Vehicle* cur = &t.vehicles[k];
    std::size_t j = i, hint = k;
    while (j > tl.first()) {
        const Tick& p = tl.at(j - 1);
        const Vehicle* u = find_vehicle(p, *cur, hint);
        const int why = !u ? 0 : !within_radius(*u) ? 1 : dist2(u->main.r[3], cur->main.r[3]) > kSlotJump2 ? 2 : -1;
        if (why >= 0) {
            if (how) *how = why;
            *start = j;
            return 1;
        }
        // the clock: a reset (a restart) or a long stop ends the search as "old" (never left half faded)
        if (p.now > tl.at(j).now || i - (j - 1) > kSinceMaxTicks) return 0;
        cur = u;
        hint = std::size_t(u - p.vehicles.data());
        --j;
        if (t.now - p.now >= kTrafficAppearSeconds) return 0;
    }
    return 0;
}

// Vehicle k of tick j is in the radius there and gone at tick j + 1 (removed or its slot reused), not by a crash (a
// crashed vehicle within 5 m then: the car goes on as that)
bool removed_after(const Timeline& tl, std::size_t j, std::size_t k, int view = 0) {
    const Tick& p = tl.at(j);
    const Vehicle& u = p.vehicles[k];
    if (!within_radius(u, view) || j >= tl.last()) return false;
    const Tick& q = tl.at(j + 1);
    if (const Vehicle* w = find_vehicle(q, u, k); w && dist2(w->main.r[3], u.main.r[3]) <= kSlotJump2) return false;
    for (const auto& x : q.vehicles)
        if (x.kind == VehicleKind::kCrashed && dist2(x.main.r[3], u.main.r[3]) < 25.0f) return false;
    return true;
}

// Per-tick results, worked out once per tick: a window at hundreds of frames a second asks about the same ~40 ticks
// again and again (looking back per frame cost ~0.1 ms a frame). Keyed by the tick's address and checked against a
// signature of its content (a freed tick's address can come back holding another tick).
// split2: per view ([1]: player 2's of a split screen; only worked out for split ticks)
struct Memo {
    std::uint64_t sig = 0;
    bool since_ok[2] = {false, false};
    std::vector<float> since[2];                 // Vehicle::since per vehicle of the tick
    bool removed_ok[2] = {false, false};         // known once the next tick is there
    struct Removed { std::size_t k; float vel[3]; };
    std::vector<Removed> removed[2];             // removed_after this tick, with the speed it had
};
thread_local std::unordered_map<const Tick*, Memo> g_memo;

// (a session's ticks differ in their poll; the clock and the vehicle list's ends as well, cheaply)
std::uint64_t signature(const Tick& t) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::uint64_t x) { h = (h ^ x) * 1099511628211ull; };
    std::uint32_t nb;
    std::memcpy(&nb, &t.now, 4);
    mix(t.poll); mix(t.vblank); mix(nb); mix(t.vehicles.size());
    if (!t.vehicles.empty()) { mix(t.vehicles.front().id); mix(t.vehicles.back().id); }
    return h;
}

Memo& memo(const Tick& t) {
    Memo& m = g_memo[&t];   // (references to unordered_map elements survive rehashing)
    const std::uint64_t s = signature(t);
    if (m.sig != s) { m = Memo{}; m.sig = s; }
    return m;
}

void trim_memo() { if (g_memo.size() > 8192) g_memo.clear(); }

// Vehicle::since for every vehicle of tick j from tick j - 1's (base: none fading); view 1: player 2's radius, and
// a tick before the split screen counts as base
void compute_since(const Timeline& tl, std::size_t j, bool base, int view = 0) {
    const Tick& t = tl.at(j);
    Memo& m = memo(t);
    m.since[view].assign(t.vehicles.size(), -1.0f);
    m.since_ok[view] = true;
    if (base || (view == 1 && !(t.split && tl.at(j - 1).split))) return;
    const Tick& p = tl.at(j - 1);
    const Memo& pm = memo(p);
    const bool clock_ok = !(p.now > t.now);   // a reset (a restart): what was there counts as long present
    for (std::size_t k = 0; k < t.vehicles.size(); ++k) {
        const Vehicle& v = t.vehicles[k];
        if (!within_radius(v, view)) continue;
        const Vehicle* u = find_vehicle(p, v, k);
        if (!u || !within_radius(*u, view) || dist2(u->main.r[3], v.main.r[3]) > kSlotJump2) { m.since[view][k] = t.now; continue; }
        if (!clock_ok) continue;
        const std::size_t n = std::size_t(u - p.vehicles.data());
        const float s = pm.since_ok[view] && n < pm.since[view].size() ? pm.since[view][n] : -1.0f;
        if (s >= 0.0f && t.now - s < kTrafficAppearSeconds) m.since[view][k] = s;
    }
}

const Memo& removed_of(const Timeline& tl, std::size_t j, int view = 0) {   // j < tl.last()
    const Tick& p = tl.at(j);
    Memo& m = memo(p);
    if (m.removed_ok[view]) return m;
    m.removed[view].clear();
    for (std::size_t k = 0; k < p.vehicles.size(); ++k) {
        if (!removed_after(tl, j, k, view)) continue;
        Memo::Removed r{k, {0, 0, 0}};
        if (j > tl.first()) {   // its last speed (ticks j - 1 -> j)
            const Tick& o = tl.at(j - 1);
            const Vehicle& g = p.vehicles[k];
            const Vehicle* z = find_vehicle(o, g, k);
            if (z && p.now > o.now && dist2(z->main.r[3], g.main.r[3]) <= kSlotJump2)
                for (int c = 0; c < 3; ++c) r.vel[c] = (g.main.r[3][c] - z->main.r[3][c]) / (p.now - o.now);
        }
        m.removed[view].push_back(r);
    }
    m.removed_ok[view] = true;
    return m;
}

}  // namespace

const std::vector<float>& traffic_since_tick(const Timeline& tl, std::size_t i, int view) {
    trim_memo();
    // back to the newest tick already worked out (or the timeline's first, or kSinceMaxTicks back: what was there then
    // counts as long present), then forward to i
    std::size_t j = i;
    while (j > tl.first() && i - j < kSinceMaxTicks && !memo(tl.at(j)).since_ok[view]) --j;
    if (!memo(tl.at(j)).since_ok[view]) compute_since(tl, j, true, view);
    for (std::size_t n = j + 1; n <= i; ++n) compute_since(tl, n, false, view);
    return memo(tl.at(i)).since[view];
}

float traffic_since(const Timeline& tl, std::size_t i, std::size_t k, std::size_t* appear_tick) {
    if (appear_tick) {   // the probe and the tests: the look-back, which also says where the run began
        std::size_t s = 0;
        if (!run_start(tl, i, k, &s)) return -1.0f;
        *appear_tick = s;
        return tl.at(s).now;
    }
    const std::vector<float>& since = traffic_since_tick(tl, i);
    return k < since.size() ? since[k] : -1.0f;
}

void traffic_vanishing(const Timeline& tl, std::size_t i, float now, std::vector<Vehicle>& out) {
    trim_memo();
    const Tick& a = tl.at(i);
    for (std::size_t j = i; j-- > tl.first();) {   // removals between tick j and j + 1, newest first
        const Tick& p = tl.at(j);
        if (a.now - p.now >= kTrafficVanishSeconds || p.now > tl.at(j + 1).now || i - j > kSinceMaxTicks) break;
        // split2 (§29.6): in a split screen, the removals within either view's radius (one copy each; gone / gone2 say
        // in which view it fades out). One-player ticks: view 0's alone, as before.
        const bool split = a.split && p.split;
        const auto& r0 = removed_of(tl, j, 0).removed[0];
        const std::vector<Memo::Removed>* r1 = split ? &removed_of(tl, j, 1).removed[1] : nullptr;
        auto listed = [](const std::vector<Memo::Removed>* l, std::size_t k) {
            if (l) for (const auto& r : *l) if (r.k == k) return true;
            return false;
        };
        auto emit = [&](const Memo::Removed& r, bool in0, bool in1) {
            Vehicle g = p.vehicles[r.k];
            // on at its last speed, at most 70 m/s
            const float v2 = r.vel[0] * r.vel[0] + r.vel[1] * r.vel[1] + r.vel[2] * r.vel[2];
            const float dt = std::max(0.0f, now - p.now) * (v2 > 70.0f * 70.0f ? 0.0f : 1.0f);
            for (int c = 0; c < 3; ++c) {
                g.main.r[3][c] += r.vel[c] * dt;
                g.body.r[3][c] += r.vel[c] * dt;
                g.cull_pos[c] += r.vel[c] * dt;
                g.shadow.ess.r[3][c] += r.vel[c] * dt;   // its shadow and lights go with it, fading as it does
            }
            g.since = -1.0f;
            g.gone = in0 ? p.now : -1.0f;
            g.since2 = -1.0f;
            g.gone2 = in1 ? p.now : -1.0f;
            out.push_back(g);
        };
        for (const auto& r : r0) emit(r, true, listed(r1, r.k));
        if (r1)
            for (const auto& r : *r1)
                if (!listed(&r0, r.k)) emit(r, false, true);
    }
}

float traffic_fade(const Vehicle& v, float now, const float cam_pos[3]) {
    float fade = 1.0f;
    if (v.since >= 0.0f) fade = std::clamp((now - v.since) / kTrafficAppearSeconds, 0.0f, 1.0f);
    if (v.gone >= 0.0f) fade = std::clamp(1.0f - (now - v.gone) / kTrafficVanishSeconds, 0.0f, 1.0f);
    // what Draw cuts at 275 m: traffic unless crashing, crashed vehicles (game_draws)
    const bool cut = (v.kind == VehicleKind::kTraffic && !(v.flags & 4)) || v.kind == VehicleKind::kCrashed;
    if (cut) {
        const float d = std::sqrt(dist2(v.main.r[3], cam_pos));
        const float u = std::clamp((kTrafficDrawRadius - d) / kTrafficFadeBand, 0.0f, 1.0f);
        fade *= u * u * (3.0f - 2.0f * u);   // smoothstep: no visible start of the band
    }
    return fade;
}

void traffic_probe(const Timeline& tl, std::size_t i, TrafficProbeStats* stats) {
    const Tick& t = tl.at(i);
    const Camera& c = t.cam;
    auto where = [&](const Vehicle& v, float* sx, float* sy, float* z) {
        float p[3];
        for (int r = 0; r < 3; ++r)
            p[r] = v.main.r[3][0] * c.screen[0][r] + v.main.r[3][1] * c.screen[1][r] + v.main.r[3][2] * c.screen[2][r] + c.screen[3][r];
        *z = p[2];
        *sx = p[2] > 0 ? p[0] / p[2] : -9.0f;
        *sy = p[2] > 0 ? p[1] / p[2] : -9.0f;
        // on screen: in front, within the 4:3 picture's height and up to a 32:9 picture's width (0..1 is 4:3)
        return p[2] > 0 && *sy >= 0.0f && *sy <= 1.0f && *sx >= -0.84f && *sx <= 1.84f;
    };
    static const char* kHow[3] = {"spawn", "range", "jump"};
    for (std::size_t k = 0; k < t.vehicles.size(); ++k) {
        const Vehicle& v = t.vehicles[k];
        if (!within_radius(v)) continue;
        std::size_t s = 0;
        int how = 0;
        if (!run_start(tl, i, k, &s, &how)) continue;
        Vehicle w = v;
        w.since = tl.at(s).now;
        const float fade = traffic_fade(w, t.now, c.m.r[3]);
        float sx, sy, z;
        const bool on = where(v, &sx, &sy, &z);
        const float d = std::sqrt(dist2(v.main.r[3], c.m.r[3])), dl = std::sqrt(dist2(v.cull_pos, c.lod_pos));
        if (s == i) {
            std::printf("[traffic-fade] poll %u tick %zu: traffic %08x appears (%s) at %.1f m (game camera %.1f m), screen %.2f %.2f%s; fade %.2f\n",
                        t.poll, i, v.id, kHow[how], double(d), double(dl), double(sx), double(sy), on ? " (on screen)" : "", double(fade));
            if (stats) {
                ++stats->appear[how];
                if (on) {
                    ++stats->appear_on_screen[how];
                    ++stats->dist_hist[std::min(5, int(dl / 50.0f))];
                }
            }
        } else {
            std::printf("[traffic-fade] poll %u tick %zu:   %08x %.1f m, %.2f s in, fade %.2f\n", t.poll, i, v.id, double(d),
                        double(t.now - tl.at(s).now), double(fade));
        }
    }
    if (i <= tl.first()) return;
    const Tick& p = tl.at(i - 1);
    for (std::size_t k = 0; k < p.vehicles.size(); ++k) {   // in the radius at the tick before, not now
        const Vehicle& u = p.vehicles[k];
        if (!within_radius(u)) continue;
        const Vehicle* v = find_vehicle(t, u, k);
        if (v && within_radius(*v) && dist2(u.main.r[3], v->main.r[3]) <= kSlotJump2) continue;
        float sx, sy, z;
        const bool on = where(u, &sx, &sy, &z);
        const float dl = std::sqrt(dist2(u.cull_pos, p.cam.lod_pos));
        const char* why = !v ? "removed" : !within_radius(*v) ? "out of range" : "slot reused";
        const bool vanish = removed_after(tl, i - 1, k);
        float wreck = 1e9f;   // the nearest crashed vehicle now (a traffic car that crashed goes on as one)
        for (const auto& x : t.vehicles) if (x.kind == VehicleKind::kCrashed) wreck = std::min(wreck, dist2(x.main.r[3], u.main.r[3]));
        std::printf("[traffic-fade] poll %u tick %zu: traffic %08x leaves (%s) at %.1f m (game camera), screen %.2f %.2f%s; nearest wreck %.1f m%s\n",
                    t.poll, i, u.id, why, double(dl), double(sx), double(sy), on ? " (on screen)" : "", double(std::sqrt(wreck)),
                    vanish ? "; fades out" : "");
        if (stats) {
            if (v && !within_radius(*v)) ++stats->gone_range;
            else { ++stats->gone_despawn; stats->gone_despawn_on_screen += on; }
        }
    }
}

}  // namespace b3replay
