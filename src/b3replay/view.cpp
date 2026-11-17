#include "view.h"
#include "carlook.h"
#include "ghost.h"   // ghost1

#include <functional>

#include "globe.h"
#include "splitlayout.h"   // splitlayout1
#include "b3asset/postfx.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>

#include "b3asset/car.h"
#include "b3asset/enviro.h"
#include "b3asset/png.h"
#include "b3asset/discfs.h"
#include "b3asset/export.h"
#include "b3asset/hdpack.h"
#include "b3asset/track.h"

namespace fs = std::filesystem;

namespace b3replay {

namespace {

// ---- asset matching -------------------------------------------------------------------------------------------------

// The route whose STATIC.DAT has the captured file size (+0x04) and unit count (+0x54). Variants of one area can share
// both (EU/C4_V1 and C4_V2: the same size and 51 units), so the candidates are scored by the recorded camera: the
// median distance from the camera to its current unit's centre (the mean of the four points at the unit's high block
// +0x10, as b3asset's track reader takes them), over the segment's ticks. The closest wins; directory order (which
// differs between file systems) never decides.
float route_score(const fs::path& route, const std::vector<std::uint8_t>& st, const std::vector<const Tick*>& samples) {
    std::vector<std::uint8_t> sm;
    if (!b3asset::read_file((route / "STREAMED.DAT").string(), sm)) return 1e30f;
    auto u32 = [](const std::vector<std::uint8_t>& d, std::size_t o) {
        std::uint32_t v = 0;
        if (o + 4 <= d.size()) std::memcpy(&v, d.data() + o, 4);
        return v;
    };
    auto f32 = [&](std::size_t o) { float v = 0; if (o + 4 <= sm.size()) std::memcpy(&v, sm.data() + o, 4); return v; };
    std::int16_t nunits = 0;
    std::memcpy(&nunits, st.data() + 0x54, 2);
    const std::size_t utab = u32(st, 0x58);
    std::vector<float> d;
    for (const Tick* tp : samples) {
        const auto& t = *tp;
        if (!t.cam.valid || t.current_unit < 0 || t.current_unit >= nunits) continue;
        const std::size_t hi = u32(st, utab + 16 * std::size_t(t.current_unit));
        if (hi + 0x50 > sm.size()) continue;
        float c[3] = {};
        for (int q = 0; q < 4; ++q)
            for (int k = 0; k < 3; ++k) c[k] += f32(hi + 0x10 + 16 * std::size_t(q) + 4 * std::size_t(k)) / 4;
        const float dx = t.cam.m.r[3][0] - c[0], dz = t.cam.m.r[3][2] - c[2];
        d.push_back(std::sqrt(dx * dx + dz * dz));
    }
    if (d.empty()) return 1e30f;
    std::nth_element(d.begin(), d.begin() + std::ptrdiff_t(d.size() / 2), d.end());
    return d[d.size() / 2];
}

}  // namespace

std::string find_route(const std::string& game, std::uint32_t size, int units, const std::vector<const Tick*>& samples) {
    std::vector<fs::path> found;
    std::vector<std::uint8_t> st_found;
    std::string best;
    float best_score = 0;
    // iso1: the disc folder or the disc image (b3asset/discfs.h)
    for (const auto& area : b3asset::disc::list_dir(fs::path(game) / "TRACKS")) {
        if (!area.dir) continue;
        for (const auto& route : b3asset::disc::list_dir(area.path)) {
            const fs::path st = route.path / "STATIC.DAT";
            if (b3asset::disc::file_size(st) != static_cast<std::int64_t>(size)) continue;
            std::vector<std::uint8_t> d;
            if (!b3asset::read_file(st.string(), d) || d.size() < 0x60) continue;
            std::int16_t n = 0;
            std::memcpy(&n, d.data() + 0x54, 2);
            if (n != units) continue;
            const float score = route_score(route.path, d, samples);
            if (best.empty() || score < best_score || (score == best_score && route.path.string() < best)) {
                best = route.path.string();
                best_score = score;
            }
        }
    }
    return best;
}

// Car files whose header bytes 0x00..0x4C (version, size, counts, floats: nothing FixUp relocates) equal the captured
// vehicle data's.
std::vector<CarFile> scan_cars(const std::string& game) {
    std::vector<CarFile> out;
    for (const auto& cls : b3asset::disc::list_dir(fs::path(game) / "PVEH")) {   // iso1: folder or image
        if (!cls.dir) continue;
        for (const auto& f : b3asset::disc::list_dir(cls.path)) {
            std::string e = f.path.extension().string();
            for (char& c : e) c = char(std::toupper(static_cast<unsigned char>(c)));
            if (e != ".BGV" && e != ".BTV") continue;
            CarFile cf{f.path.string(), {}};
            if (b3asset::read_file(cf.path, cf.bytes) && cf.bytes.size() > 0xE00) out.push_back(std::move(cf));
        }
    }
    return out;
}
int match_car(const std::vector<CarFile>& cars, const ModelHead& head, int* candidates) {
    int found = -1, n = 0;
    for (std::size_t i = 0; i < cars.size(); ++i)
        if (std::memcmp(cars[i].bytes.data(), head.data(), 0x4C) == 0) { if (found < 0) found = int(i); ++n; }
    if (candidates) *candidates = n;
    return found;
}

// ---- interpolation --------------------------------------------------------------------------------------------------

namespace {

struct Quat { float x, y, z, w; };
Quat to_quat(const float r[3][3]) {   // rows of a rotation (row-vector convention)
    const float tr = r[0][0] + r[1][1] + r[2][2];
    Quat q;
    if (tr > 0) { const float s = std::sqrt(tr + 1.0f) * 2; q = {(r[1][2] - r[2][1]) / s, (r[2][0] - r[0][2]) / s, (r[0][1] - r[1][0]) / s, 0.25f * s}; }
    else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) { const float s = std::sqrt(1 + r[0][0] - r[1][1] - r[2][2]) * 2; q = {0.25f * s, (r[1][0] + r[0][1]) / s, (r[2][0] + r[0][2]) / s, (r[1][2] - r[2][1]) / s}; }
    else if (r[1][1] > r[2][2]) { const float s = std::sqrt(1 + r[1][1] - r[0][0] - r[2][2]) * 2; q = {(r[1][0] + r[0][1]) / s, 0.25f * s, (r[2][1] + r[1][2]) / s, (r[2][0] - r[0][2]) / s}; }
    else { const float s = std::sqrt(1 + r[2][2] - r[0][0] - r[1][1]) * 2; q = {(r[2][0] + r[0][2]) / s, (r[2][1] + r[1][2]) / s, 0.25f * s, (r[0][1] - r[1][0]) / s}; }
    return q;
}
void from_quat(Quat q, float r[3][3]) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n > 0) { q.x /= n; q.y /= n; q.z /= n; q.w /= n; }
    r[0][0] = 1 - 2 * (q.y * q.y + q.z * q.z); r[0][1] = 2 * (q.x * q.y + q.z * q.w);     r[0][2] = 2 * (q.x * q.z - q.y * q.w);
    r[1][0] = 2 * (q.x * q.y - q.z * q.w);     r[1][1] = 1 - 2 * (q.x * q.x + q.z * q.z); r[1][2] = 2 * (q.y * q.z + q.x * q.w);
    r[2][0] = 2 * (q.x * q.z + q.y * q.w);     r[2][1] = 2 * (q.y * q.z - q.x * q.w);     r[2][2] = 1 - 2 * (q.x * q.x + q.y * q.y);
}
}  // namespace

// Rotation by slerp, row lengths (scale) and translation linearly. t in (1, 3] extrapolates along the same path (perf1,
// §33: the live view's frames past the newest tick, lat1's lead; before, t >= 1 returned b, so those frames held the
// newest tick's poses and the motion froze for part of every tick). Offline frames never pass t > 1.
Mat34 lerp(const Mat34& a, const Mat34& b, float t) {
    if (t <= 0) return a;
    if (t == 1) return b;
    t = std::min(t, 3.0f);
    float ra[3][3], rb[3][3], la[3], lb[3];
    for (int i = 0; i < 3; ++i) {
        la[i] = std::sqrt(a.r[i][0] * a.r[i][0] + a.r[i][1] * a.r[i][1] + a.r[i][2] * a.r[i][2]);
        lb[i] = std::sqrt(b.r[i][0] * b.r[i][0] + b.r[i][1] * b.r[i][1] + b.r[i][2] * b.r[i][2]);
        for (int j = 0; j < 3; ++j) { ra[i][j] = la[i] > 0 ? a.r[i][j] / la[i] : 0; rb[i][j] = lb[i] > 0 ? b.r[i][j] / lb[i] : 0; }
    }
    Quat qa = to_quat(ra), qb = to_quat(rb);
    float d = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
    if (d < 0) { qb = {-qb.x, -qb.y, -qb.z, -qb.w}; d = -d; }
    Quat q;
    if (d > 0.9995f) {
        q = {qa.x + (qb.x - qa.x) * t, qa.y + (qb.y - qa.y) * t, qa.z + (qb.z - qa.z) * t, qa.w + (qb.w - qa.w) * t};
    } else {
        const float th = std::acos(d), s = std::sin(th), wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
        q = {qa.x * wa + qb.x * wb, qa.y * wa + qb.y * wb, qa.z * wa + qb.z * wb, qa.w * wa + qb.w * wb};
    }
    Mat34 o;
    float r[3][3];
    from_quat(q, r);
    for (int i = 0; i < 3; ++i) {
        const float l = la[i] + (lb[i] - la[i]) * t;
        for (int j = 0; j < 3; ++j) o.r[i][j] = r[i][j] * l;
    }
    for (int j = 0; j < 3; ++j) o.r[3][j] = a.r[3][j] + (b.r[3][j] - a.r[3][j]) * t;
    return o;
}

Vehicle lerp(const Vehicle& a, const Vehicle& b, float t) {
    Vehicle o = a;
    o.main = lerp(a.main, b.main, t);
    o.body = lerp(a.body, b.body, t);
    for (int w = 0; w < a.nwheel && w < b.nwheel; ++w) o.wheel[w] = lerp(a.wheel[w], b.wheel[w], t);
    for (int k = 0; k < a.nbit && k < b.nbit; ++k) o.bit[k] = lerp(a.bit[k], b.bit[k], t);
    // traffic wheel angles: the shorter way round (spin is an angle the game advances by speed / radius per tick)
    auto angle = [t](float x, float y) {
        float d = std::remainder(y - x, 6.2831853f);
        return x + d * t;
    };
    o.spin = angle(a.spin, b.spin);
    o.steer = a.steer + (b.steer - a.steer) * t;
    o.speed = a.speed + (b.speed - a.speed) * t;
    // zone matrices: element-wise (they are deformations, not rotations); not extrapolated past the newest tick (perf1: a
    // crash's crumple would overshoot and spring back)
    auto mix = [t](const Mat34& x, const Mat34& y) {
        Mat34 m;
        const float tz = std::min(t, 1.0f);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 3; ++j) m.r[i][j] = x.r[i][j] + (y.r[i][j] - x.r[i][j]) * tz;
        return m;
    };
    if (a.zones_valid && b.zones_valid) {
        for (int z = 0; z < 8; ++z) o.zone[z] = mix(a.zone[z], b.zone[z]);
        for (int k = 0; k < a.nbit && k < b.nbit; ++k)
            for (int z = 0; z < 2; ++z) o.bit_zone[k][z] = mix(a.bit_zone[k][z], b.bit_zone[k][z]);
    }
    // shadow inputs (fx1): the frame with the car, the ground normal and distance linearly
    o.shadow.ess = lerp(a.shadow.ess, b.shadow.ess, t);
    for (int k = 0; k < 3; ++k) o.shadow.normal[k] = a.shadow.normal[k] + (b.shadow.normal[k] - a.shadow.normal[k]) * t;
    o.shadow.dist = a.shadow.dist + (b.shadow.dist - a.shadow.dist) * t;
    return o;
}

namespace {

// The tick where the camera value of tick i first appeared: i itself if it changed against i - 1.

// The camera `get` picks of tick i at frac: between the tick where its current value appeared (k0) and the next change
// (k1); 2-tick frames move it every second tick. A value held longer than `reach` ticks either way is shown as is.
template <class Get>
Camera interp_camera(const Timeline& tl, std::size_t i, double frac, std::size_t reach, Get get, bool live = false) {
    const std::size_t first = tl.first(), last = tl.last();
    const auto changed = [&](std::size_t k) {
        if (k <= first) return true;
        const Camera& c = get(tl.at(k));
        const Camera& p = get(tl.at(k - 1));
        return std::memcmp(&c.m, &p.m, sizeof c.m) != 0 || c.half_h != p.half_h;
    };
    Camera out = get(tl.at(i));
    std::size_t k0 = i;
    while (k0 > first && i - k0 < reach && !changed(k0)) --k0;
    std::size_t k1 = i + 1;
    while (k1 <= last && k1 - i <= reach && !changed(k1)) ++k1;
    // perf1 (§33): live, the next key may not have arrived yet (2-tick frames change the camera every second tick, and the
    // low-latency display runs at the newest tick): the motion goes on from the last two keys at their spacing, for at
    // most one key interval, instead of the camera standing still for a tick and then catching up at twice the speed
    // mac5 (Windows 2026-11-10: a FOV blip as the pause menu opens): the game's frame cadence at the newest tick;
    // once the newest tick is `cadence` or more past the key with the camera still unchanged, the game has drawn a frame
    // with the same camera (a stop: the pause, a hold), so the next key is not just late and the motion is not continued
    // (it used to go on for up to a key interval past the stop, ~0.65-1.3 m at race speed, then snap back)
    const std::size_t cadence = std::size_t(std::clamp(tl.at(last).ticks_per_frame, 1, 4));
    if (live && changed(k0) && k1 > last && k0 > first && last - k0 < cadence) {
        std::size_t kp = k0 - 1;
        while (kp > first && k0 - kp < reach && !changed(kp)) --kp;
        if (changed(kp) && k0 - kp <= 2) {
            const Camera& c0 = get(tl.at(kp));
            const Camera& c1 = get(tl.at(k0));
            const double span = double(k0 - kp);
            const float u = float(1.0 + std::min((double(i - k0) + frac) / span, 1.0));
            float d2 = 0, dot = 0;
            for (int j = 0; j < 3; ++j) { d2 += (c1.m.r[3][j] - c0.m.r[3][j]) * (c1.m.r[3][j] - c0.m.r[3][j]); dot += c0.m.r[2][j] * c1.m.r[2][j]; }
            if (d2 < 25.0f * 25.0f && dot > 0.5f && u > 1.0f) {
                out = c1;
                out.m = lerp(c0.m, c1.m, u);
                out.half_w = c0.half_w + (c1.half_w - c0.half_w) * u;
                out.half_h = c0.half_h + (c1.half_h - c0.half_h) * u;
            }
            return out;
        }
    }
    if (changed(k0) && k1 <= last && k1 - i <= reach) {
        // (i - k0) + frac is the original's x - k0 (x = i + frac exactly), so u rounds the same
        const float u = float((double(i - k0) + frac) / double(k1 - k0));
        const Camera& c0 = get(tl.at(k0));
        const Camera& c1 = get(tl.at(k1));
        // a cut (another shot: > 25 m or > 60 degrees between the keys) is not interpolated
        float d2 = 0, dot = 0;
        for (int j = 0; j < 3; ++j) { d2 += (c1.m.r[3][j] - c0.m.r[3][j]) * (c1.m.r[3][j] - c0.m.r[3][j]); dot += c0.m.r[2][j] * c1.m.r[2][j]; }
        out = c0;
        if (d2 < 25.0f * 25.0f && dot > 0.5f) {
            out.m = lerp(c0.m, c1.m, std::clamp(u, 0.0f, 2.0f));   // lat1: u > 1 only past the newest tick (frac > 1)
            out.half_w = c0.half_w + (c1.half_w - c0.half_w) * u;   // zooms (the crash camera's fov changes)
            out.half_h = c0.half_h + (c1.half_h - c0.half_h) * u;
        }
    } else if (live && !changed(k0) && k1 <= last && changed(k1) && k1 - i <= cadence) {
        // mac5 (a report: resuming from the pause menu looked clunky): the first key after a hold longer than `reach` (the
        // pause) was shown at once, a whole frame's motion at race speed in one step; now the held pose blends into it
        // over the one frame interval before it, as between any two keys (cuts excepted)
        const Camera& c0 = get(tl.at(k1 - cadence < first ? first : k1 - cadence));
        const Camera& c1 = get(tl.at(k1));
        const float u = float(std::clamp((double(i) + frac - double(k1 - cadence)) / double(cadence), 0.0, 1.0));
        float d2 = 0, dot = 0;
        for (int j = 0; j < 3; ++j) { d2 += (c1.m.r[3][j] - c0.m.r[3][j]) * (c1.m.r[3][j] - c0.m.r[3][j]); dot += c0.m.r[2][j] * c1.m.r[2][j]; }
        if (d2 < 25.0f * 25.0f && dot > 0.5f) {
            out = c0;
            out.m = lerp(c0.m, c1.m, u);
            out.half_w = c0.half_w + (c1.half_w - c0.half_w) * u;
            out.half_h = c0.half_h + (c1.half_h - c0.half_h) * u;
        }
    }
    return out;
}

}  // namespace

Frame frame_at(const Timeline& tl, std::size_t i, double frac, bool interpolate, std::size_t reach, bool live) {
    const std::size_t first = tl.first(), last = tl.last();
    i = std::clamp(i, first, last);
    Frame f;
    f.tick = i;
    f.alpha = interpolate && i < last ? float(frac) : 0.0f;
    const auto& a = tl.at(i);
    const auto& b = tl.at(std::min(i + 1, last));
    std::memcpy(f.slot_unit, a.slot_unit, sizeof f.slot_unit);
    std::memcpy(f.slot_kind, a.slot_kind, sizeof f.slot_kind);
    std::memcpy(f.vis, a.vis, sizeof f.vis);
    std::memcpy(f.backdrops, a.backdrops, sizeof f.backdrops);
    f.props_off = a.props_off;
    f.current_unit = a.current_unit;
    f.water_on = a.water_on;
    f.hidden = a.hidden;
    f.crash = a.crash;
    f.fx.valid = a.effects_valid;
    f.fx.shadows_on = a.shadows_on;
    f.fx.effects_on = a.effects_on;
    f.fx.shadow_k = a.shadow_k;
    f.fx.now = a.now + (b.now - a.now) * f.alpha;   // the fades at the displayed time
    f.fx.trails = a.trails;
    f.hud = a.hud;
    f.fe_car = a.fe_car;   // menu1
    f.route_size = a.route_size;   // fe2: the front end's showroom
    f.route_units = a.route_units;
    f.sun = a.sun;
    f.blur = a.blur;
    f.post = a.post;
    f.sun_now = a.now;
    if (a.lap.valid) {   // ghost1: the lap's time at the displayed instant (a tick whose lap differs from the one before
                         // starts that lap: time 0; the game's timer there still holds the finished lap's)
        const std::size_t j = std::min(i + 1, last);
        auto lap_t = [&](std::size_t k) { return k > first && tl.at(k - 1).lap.index != tl.at(k).lap.index ? 0.0f : tl.at(k).lap.time; };
        const float ta = lap_t(i), tb = lap_t(j);
        f.time_attack = a.lap.time_attack;
        f.lap_index = a.lap.index;
        f.lap_time = b.lap.valid && b.lap.index == a.lap.index && tb >= ta ? ta + (tb - ta) * f.alpha : ta;
    }
    std::memcpy(f.fx.trail_vu, a.trail_vu, sizeof f.fx.trail_vu);
    std::memcpy(f.fx.trail_cam, a.trail_cam, sizeof f.fx.trail_cam);
    std::memcpy(f.fx.trail_alpha, a.trail_alpha, sizeof f.fx.trail_alpha);
    std::memcpy(f.fx.trail_name, a.trail_name, sizeof f.fx.trail_name);
    f.anim_valid = a.anim_valid;
    f.anim = a.anim;
    {   // the clock the captured state was made with: the poll comes before CB3World::Update in the tick, so tick i holds
        // Update(now of tick i - 1) [C anim1 capture, §15]
        const auto& p = tl.at(i > first ? i - 1 : i);
        f.anim_now = p.now + (a.now - p.now) * f.alpha;
    }
    if (f.alpha > 0 && a.anim_valid && b.anim_valid && b.anim.size() == a.anim.size())
        for (std::size_t k = 0; k < f.anim.size(); ++k) {
            // a continuous scroll (period 0: t = now x speed every tick) blended to the displayed time; not across a
            // clock reset (a race restart) or a frame change
            AnimMaterial& p = f.anim[k];
            const AnimMaterial& q = b.anim[k];
            if (p.material == q.material && p.period == 0.0f && q.period == 0.0f && std::fabs(q.t - p.t) < 1.0f)
                p.t += (q.t - p.t) * f.alpha;
        }
    f.fx.coronas = a.coronas;
    f.fx.contact = a.contact;   // fx2: scrapes, sparks, popcorn (the tick's state, drawn as the tick's render drew them)
    f.fx.spark_cam914[0] = a.trail_cam[5][0];   // camera record 0 +0x170, +0x174, +0x178, +0x168
    f.fx.spark_cam914[1] = a.trail_cam[5][1];
    f.fx.spark_cam914[2] = a.trail_cam[5][2];
    f.fx.spark_cam914[3] = a.trail_cam[4][2];
    // camera: between the tick where its current value appeared (k0) and the next change (k1); 2-tick frames move it
    // every second tick. A value held longer than `reach` ticks either way is shown as is.
    f.cam = a.cam;
    f.tick_cam = a.cam;   // fx2: the tick's own game camera (the popcorn program's set-up)
    if (interpolate) f.cam = interp_camera(tl, i, frac, reach, [](const Tick& t) -> const Camera& { return t.cam; }, live);
    // gsoff1 (§29): player 2's view of a split-screen tick, interpolated the same way
    f.split = a.split && a.view2.valid;
    if (f.split) {
        f.cam2 = a.view2.cam;
        f.tick_cam2 = a.view2.cam;
        f.view2 = a.view2;   // split1
        if (interpolate) f.cam2 = interp_camera(tl, i, frac, reach, [](const Tick& t) -> const Camera& { return t.view2.cam; }, live);
    }
    const std::vector<float>& since = traffic_since_tick(tl, i);   // traffic-popin
    for (const auto& v : a.vehicles) {
        const Vehicle* w = nullptr;
        if (f.alpha > 0)
            for (const auto& u : b.vehicles) if (u.id == v.id && u.data == v.data) { w = &u; break; }
        f.vehicles.push_back(w ? lerp(v, *w, f.alpha) : v);
        if (f.vehicles.size() <= since.size()) f.vehicles.back().since = since[f.vehicles.size() - 1];
    }
    if (f.split) {   // split2 (§29.6): player 2's view fades its traffic against its own radius
        const std::vector<float>& since2 = traffic_since_tick(tl, i, 1);
        for (std::size_t k = 0; k < f.vehicles.size() && k < since2.size(); ++k) f.vehicles[k].since2 = since2[k];
    }
    traffic_vanishing(tl, i, f.fx.now, f.vehicles);   // traffic-popin: removed traffic fading out
    for (const auto& p : a.props) {   // dynamic props: matched by placement
        Prop q = p;
        if (f.alpha > 0)
            for (const auto& u : b.props) if (u.placement == p.placement) { q.m = lerp(p.m, u.m, f.alpha); break; }
        f.props.push_back(q);
    }
    return f;
}

// ---- the route's scenes and the draw list ---------------------------------------------------------------------------

void View::clear(b3gfx::Device& dev) {
    dev.clear_resources();
    car_models.clear();
    car_globals = b3view::CarGlobals{};
    hi = b3view::Scene{};
    lo = b3view::Scene{};
    sky = b3view::Scene{};
    hi_unit.clear(); hi_inst.clear(); lo_unit.clear(); backdrop.clear(); barrier.clear(); always.clear(); prop_items.clear(); item_prop.clear();
    effects.clear();
    coronas.clear();
    hud_textures.clear();
    hud_textures_filtered.clear();   // accessibility
    prompt_tex_filtered = b3gfx::kNone;
    prompt_tex_filter = 0;
    prompt_tex = b3gfx::kNone;   // mac5: released with the device's resources; the atlas is uploaded again
    prompt_atlas = PromptAtlas{};
    // mac5 (Mac 2026-11-08: the mirrored edges gone later in a session): the side fill's blurred textures were
    // kept across clear_resources, so their ids named released textures after the first track load
    fill_movie.clear();
    fill_still.clear();
    fill_still_filtered.clear();
    track_boxes.clear();
    movie_tex.clear();
    movie_up.clear();
    movie_placeholder = b3gfx::kNone;
    hud_vb = hud_ib = b3gfx::kNone;
    host_ui_tex.clear();
    host_vb = host_ib = b3gfx::kNone;
    host_built.reset();
    host_items.clear();
    hud_built.reset();
    hud_items.clear();
    frontend_globals = false;
    room = Showroom{};
    route.clear();
    route_size = 0;
    route_units = 0;
    crash_scene = b3view::CrashScene{};
    fallback_stage = -1;
}

int stage_by_id(const std::vector<b3asset::GameStage>& stages, std::uint64_t id) {
    for (const auto& s : stages) if (s.id == id) return s.index;
    return -1;
}

int start_stage(const std::vector<b3asset::GameStage>& stages, const Tick& first, int* fits) {
    std::vector<const Vehicle*> cars;
    for (const auto& v : first.vehicles) if (v.kind == VehicleKind::kRaceCar) cars.push_back(&v);
    int found = -1, n = 0;
    for (const auto& s : stages) {
        if (cars.empty() || s.starts.size() < cars.size()) continue;
        std::vector<bool> used(s.starts.size(), false);
        bool all = true;
        for (const Vehicle* c : cars) {
            bool hit = false;
            for (std::size_t k = 0; k < s.starts.size() && !hit; ++k) {
                float d2 = 0;
                for (int j = 0; j < 3; ++j) d2 += (c->main.r[3][j] - s.starts[k][std::size_t(j)]) * (c->main.r[3][j] - s.starts[k][std::size_t(j)]);
                if (!used[k] && d2 < 1.0f) { used[k] = true; hit = true; }
            }
            all = all && hit;
        }
        if (all) { found = s.index; ++n; }
    }
    if (fits) *fits = n;
    return n == 1 ? found : -1;
}

void quat_right(const float q[4], float out[3]) {
    const float x = q[0] * 1.4142135f, y = q[1] * 1.4142135f, z = q[2] * 1.4142135f, w = q[3] * 1.4142135f;
    out[0] = 1.0f - y * y - z * z;
    out[1] = x * y + w * z;
    out[2] = x * z - w * y;
}

// loadlag1: the CPU half of load_track (the files read, both models decoded). No device and no View state: any thread.
TrackPrep View::prepare_track(const std::string& route_dir, const std::string& game) {
    TrackPrep p;
    p.route = route_dir;
    const auto t0 = std::chrono::steady_clock::now();
    b3asset::hd_prefetch({route_dir + "/STATIC.DAT"});   // --hd-pack: the route's replacements read in parallel first
    b3asset::read_file(route_dir + "/STATIC.DAT", p.st);
    b3asset::read_file(route_dir + "/STREAMED.DAT", p.sm);
    b3asset::GsMemory gs;
    b3asset::TrackOptions to;
    to.extra_backdrops = true;   // STATIC +0x28: the barriers func_0021C010 draws after the game data (crash2)
    to.lines = true;             // lines1: the type-3 line meshes (wires, cables), high and LOD units
    p.ok = b3asset::read_track({p.st.data(), p.st.size()}, {p.sm.data(), p.sm.size()}, to, gs, p.mh, &p.why);
    if (p.ok) {
        to.lod = true;
        to.backdrops = to.instances = to.props = false;
        b3asset::GsMemory gs2;
        p.ok = b3asset::read_track({p.st.data(), p.st.size()}, {p.sm.data(), p.sm.size()}, to, gs2, p.ml, &p.why);
    }
    if (p.ok) {   // the files load_prepared uploads from (GLOBAL.TXD once: the blur mask, the car slots, the crash objects)
        p.enviro_ok = b3view::read_route_enviro(route_dir, p.enviro);
        p.global_ok = b3asset::read_file(game + "/DATA/GLOBAL.TXD", p.global_txd);
        if (p.global_ok) {
            b3asset::GsMemory gm;
            p.blur_ok = b3asset::find_txd_raster(b3asset::Bytes{p.global_txd.data(), p.global_txd.size()}, "radialblurmask", gm, p.blur_mask) &&
                        !p.blur_mask.levels.empty();
            p.car_slots_n = b3view::decode_car_globals(p.global_txd, p.car_slots, &p.car_why);
        } else {
            p.car_why = "cannot read " + game + "/DATA/GLOBAL.TXD";
        }
        p.bgd_ok = b3asset::read_file(route_dir + "/GAMEDATA.BGD", p.bgd);
        if (p.enviro_ok && !p.enviro.sun_corona.levels.empty() && b3asset::route_has_sun(route_dir)) p.flare = b3view::Sun::read_flare(game);
    }
    p.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return p;
}

bool View::load_track(b3gfx::Device& dev, const std::string& route_dir, std::string* why) {
    return load_prepared(dev, prepare_track(route_dir, game), why);
}

bool View::load_prepared(b3gfx::Device& dev, TrackPrep&& p, std::string* why) {
    route = p.route;
    const std::vector<std::uint8_t>& st = p.st;
    const std::vector<std::uint8_t>& sm = p.sm;
    if (st.size() >= 0x60) { route_size = std::uint32_t(st.size()); std::int16_t n = 0; std::memcpy(&n, st.data() + 0x54, 2); route_units = n; }
    if (!p.ok) {
        if (why) *why = p.why;
        return false;
    }
    if (!b3view::build_scene(dev, std::move(p.mh), true, hi, why) || !b3view::build_scene(dev, std::move(p.ml), true, lo, why, &hi)) return false;
    b3asset::hd_trim();
    auto index = [this](const b3view::Scene& sc, std::map<int, std::vector<int>>* units, std::map<int, std::vector<int>>* inst,
                    std::map<int, std::vector<int>>* bd, std::vector<int>* always) {
        for (std::size_t i = 0; i < sc.model.draws.size(); ++i) {
            const std::string& p = sc.model.draws[i].part;
            int k = 0;
            if (std::sscanf(p.c_str(), "unit %d", &k) == 1) {
                if (p.find("instance") != std::string::npos) { if (inst) (*inst)[k].push_back(int(i)); }
                else if (units) (*units)[k].push_back(int(i));
            } else if (std::sscanf(p.c_str(), "backdrop+28 %d", &k) == 1) { if (bd) barrier[k].push_back(int(i)); }
            else if (std::sscanf(p.c_str(), "backdrop %d", &k) == 1) { if (bd) (*bd)[k].push_back(int(i)); }
            else if (always && p == "prop") always->push_back(int(i));
        }
    };
    // render3's game model: track_item shading is in hi/lo (build_scene with prelit); the sky and the frame
    // parameters come from the route's ENVIRO.DAT, kept in their own scene so the unit indices above stay valid
    sky = b3view::Scene{};
    sky.prelit = true;
    sky.enviro = std::move(p.enviro);
    if (!(p.enviro_ok && b3view::add_enviro(dev, sky))) std::fprintf(stderr, "%s: no ENVIRO.DAT (no sky / fog)\n", route.c_str());
    blur_mask = b3gfx::kNone;
    {   // water1: the radial blur's mask (CB3RadialBlur::Prepare 0x2FAD90 finds "radialblurmask" among the global textures)
        if (p.blur_ok) {   // (read and decoded in prepare_track)
            std::vector<b3gfx::TextureLevel> lv;
            for (const auto& l : p.blur_mask.levels) lv.push_back({l.w, l.h, l.rgba.data()});
            blur_mask = dev.create_texture(lv);
        } else {
            std::fprintf(stderr, "radial blur: no radialblurmask in %s/DATA/GLOBAL.TXD (no boost blur)\n", game.c_str());
        }
    }
    if (!sky.has_enviro) {}
    else if (std::string sw; !sun.load(dev, sky.enviro, route, p.flare, &sw) || !sw.empty())
        std::fprintf(stderr, "%s: %s\n", route.c_str(), sw.empty() ? "no sun corona (Prepare skips this route)" : sw.c_str());
    index(hi, &hi_unit, &hi_inst, &backdrop, &always);
    for (const auto& [g, items] : barrier) for (int k : items) hi.items[std::size_t(k)].post_fog = true;   // drawn after the fog
    std::map<int, int> tr_prop;
    for (const auto& [tr, idx] : hi.model.prop_transforms) tr_prop[tr] = int(idx);
    for (std::size_t i = 0; i < hi.model.draws.size(); ++i)
        if (const auto it = tr_prop.find(hi.model.draws[i].transform); it != tr_prop.end()) {
            prop_items[it->second].push_back(int(i));
            item_prop[int(i)] = it->second;
        }
    index(lo, &lo_unit, nullptr, nullptr, nullptr);
    // the units' water choice (high block +0x6E / +0x6F / +0x70) and the water objects' items (b3view::WaterTable)
    water.build(hi, st, sm);
    car_globals = b3view::CarGlobals{};
    std::string w;
    b3view::upload_car_globals(dev, p.car_slots, p.car_slots_n, car_globals);
    if (!p.global_ok || p.car_slots_n < 4) {
        w = p.car_why;
        std::fprintf(stderr, "cars: %s (slots 1-4 untextured)\n", w.c_str());
    }
    if (sky.has_enviro) b3view::set_car_environment(dev, sky.enviro, car_globals);
    if (!p.bgd_ok) { crash_scene = b3view::CrashScene{}; w.clear(); }   // (load_crash_scene: no GAMEDATA.BGD)
    else if (!b3view::load_crash_scene(dev, p.bgd, p.global_txd, crash_scene, &w) && !w.empty()) std::fprintf(stderr, "%s\n", w.c_str());
    if (!effects.load(dev, game, &w)) std::fprintf(stderr, "effects: %s (none drawn)\n", w.c_str());
    return true;
}

void View::car_frame(b3gfx::FrameParams& fp) const {
    std::memcpy(fp.sun_colour, car_globals.sun_colour, sizeof fp.sun_colour);
    fp.car_look = carlook::current();   // reflect2: the enhanced look, live-tunable (carlookpanel.h)
}

void View::post_frame(const Frame& f, float display_aspect, b3gfx::FrameParams& fp) const {
    blur_frame(f, fp);
    bloom_frame(f, fp);
    if (std::getenv("B3_FRAME_UNITS")) fp.present_scale = 1.0f;   // diagnostics: the output shows the GS frame's bytes, not the display
    fp.sun_sprites.clear();
    if (!show_sun || !sky.has_enviro) return;
    // SetupCamera's mrAspectRatio = record +0x200 x +0x20C x sizeY / sizeX; graphics manager +0 scales the radius
    const SunCam& c = f.sun;
    const float aspect = c.valid && c.size[0] > 0 ? c.f200 * c.f20C * c.size[1] / c.size[0] : 0.0f;
    sun.frame(fp, display_aspect, c.valid ? c.gm0 : 0, aspect, f.sun_now);
    fp.sun_occlusion = sun_occlusion;
    if (std::getenv("B3_SUN_TRACE") && c.valid) {   // SetupCamera's own projection through the captured record vs ours
        const float* d = sky.enviro.to_sun;
        const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        float p[3], q[3];
        for (int k = 0; k < 3; ++k) p[k] = c.pos[k] + 50.0f * d[k] / l;
        for (int k = 0; k < 3; ++k) q[k] = c.m[0][k] * p[0] + c.m[1][k] * p[1] + c.m[2][k] * p[2] + c.m[3][k];
        const auto& s0 = fp.sun_sprites.empty() ? b3gfx::FrameParams::SunSprite{} : fp.sun_sprites[0];
        std::fprintf(stderr, "sun trace: game px (%.1f, %.1f) z %.2f; ours NDC centre (%.3f, %.3f) -> px (%.1f, %.1f); record pos (%.1f %.1f %.1f) eye (%.1f %.1f %.1f)\n",
                     q[0] / q[2] * c.size[0], q[1] / q[2] * c.size[1], q[2], 0.5f * (s0.rect[0] + s0.rect[2]), 0.5f * (s0.rect[1] + s0.rect[3]),
                     (0.25f * (s0.rect[0] + s0.rect[2]) + 0.5f) * 640.0f, (0.5f - 0.25f * (s0.rect[1] + s0.rect[3])) * 448.0f,
                     c.pos[0], c.pos[1], c.pos[2], fp.eye[0], fp.eye[1], fp.eye[2]);
    }
}

void View::blur_frame(const Frame& f, b3gfx::FrameParams& fp) const {
    fp.blur = b3gfx::FrameParams::RadialBlur{};
    fp.dblur = b3gfx::FrameParams::DirectionalBlur{};
    fp.post_game = post_game;
    const RadialBlurIn& b = f.blur;
    const PostIn& p = f.post;
    if (!show_blur) return;
    // CB3World::Render (post1, §25): CB3DirectionalBlur::Render when UpdateCamera 0x4256C0 returns 1 (and not split-screen
    // Crash), which draws while D_00665F2A is set and its amount >= 1/255 (gp D_004E1010) [C Render's test, L draft];
    // else CB3RadialBlur::Render unless the view's car has crashed (+0x18EA). Captures before post1 lack UpdateCamera's
    // inputs: a directional amount >= 1/255 still means it returned 1; the radial blur is then left out (not decidable).
    const bool dir = p.valid && p.dir_on && p.dir_amount >= 0.003921568859f && (p.choice < 0 || p.choice == 1);
    if (dir) {
        // diagnostics (gsoff1, the strength against GS frames): B3_BLUR_AMOUNT_SCALE scales the amount, B3_BLUR_ZOOM_SCALE
        // the streak length (1 - zoom)
        static const float as = std::getenv("B3_BLUR_AMOUNT_SCALE") ? float(std::atof(std::getenv("B3_BLUR_AMOUNT_SCALE"))) : 1.0f;
        static const float zs = std::getenv("B3_BLUR_ZOOM_SCALE") ? float(std::atof(std::getenv("B3_BLUR_ZOOM_SCALE"))) : 1.0f;
        for (int k = 0; k < 2; ++k) {
            fp.dblur.centre[k] = p.dir_centre[k];
            fp.dblur.zoom[k] = zs == 1.0f ? p.dir_zoom[k] : 1.0f - (1.0f - p.dir_zoom[k]) * zs;
        }
        fp.dblur.amount = std::min(p.dir_amount * as, 1.0f);   // GS FIX = int(128 x amount + 0.5)
        return;
    }
    const bool radial = p.choice == 2 || (p.choice < 0 && radial_undecided);
    if (!radial || !b.valid || !b.on || b.crashed || !(b.amount > 0) || blur_mask == b3gfx::kNone) return;
    // diagnostics (gsoff1): B3_RADIAL_AMOUNT_SCALE / B3_RADIAL_ZOOM_SCALE scale the amount and the zoom's (1 - uv_scale)
    static const float ras = std::getenv("B3_RADIAL_AMOUNT_SCALE") ? float(std::atof(std::getenv("B3_RADIAL_AMOUNT_SCALE"))) : 1.0f;
    static const float rzs = std::getenv("B3_RADIAL_ZOOM_SCALE") ? float(std::atof(std::getenv("B3_RADIAL_ZOOM_SCALE"))) : 1.0f;
    for (int k = 0; k < 2; ++k) {
        fp.blur.centre[k] = b.centre[k];
        fp.blur.radius[k] = b.radius[k];
        fp.blur.uv_scale[k] = rzs == 1.0f ? b.uv_scale[k] : 1.0f - (1.0f - b.uv_scale[k]) * rzs;
    }
    fp.blur.colour = b.colour;
    fp.blur.amount = std::min(b.amount * radial_scale * ras, 1.0f);   // GS alpha = int(0.5 + 128 x amount); radial_scale: §25.8
    fp.blur.flip = b.flip;
    fp.blur.mask = blur_mask;
}

void View::bloom_frame(const Frame& f, b3gfx::FrameParams& fp) const {
    fp.bloom = b3gfx::FrameParams::Bloom{};
    const PostIn& p = f.post;
    // CB3Bloom::Render 0x2659C0 returns while D_00665F28 is clear [C matched]
    if (!show_bloom || !p.valid || !p.bloom_on) { bloom_last = -1; return; }
    const b3asset::BloomGains g = b3asset::bloom_gains(p.bloom_threshold);
    fp.bloom.on = true;
    fp.bloom.soft = bloom_level == 1;
    fp.bloom.c = float(g.c) / 255.0f;
    if (const char* e = std::getenv("B3_BLOOM_C")) fp.bloom.c = float(std::atof(e));   // diagnostics: another threshold
    fp.bloom.k1 = float(g.k1) / 128.0f;
    fp.bloom.k2 = float(g.k2) / 128.0f;
    fp.bloom.k3 = float(g.k3) / 128.0f;
    fp.bloom.intensity = float(b3asset::bloom_intensity_rgb(p.bloom_intensity)) / 128.0f;
    fp.bloom.keep = float(b3asset::bloom_feedback_fix(p.bloom_feedback)) / 128.0f;
    fp.bloom.mode = p.bloom_reset ? 1 : p.bloom_freeze ? 2 : 0;   // Blend: p7 (reset) before p6 (freeze) [C matched]
    // renders since the last drawn frame: one per tick reached (game quality), the displayed time in ticks (PC); a jump
    // backwards or of more than 8 ticks starts the kept picture again
    const double now = double(f.tick) + (post_game ? 0.0 : double(f.alpha));
    if (free_view) {   // perf1 (§33.7): a free camera's bloom is its own frame's steady state, not a picture kept from the
        fp.bloom.mode = 0;   // game camera's frames (frozen while paused: ghost glows in the free view): 60 renders of this
        fp.bloom.renders = 60.0f;   // frame's bright part, the old picture x keep^60 (< 0.7 % at the game's keep <= 0.92)
        fp.bloom.restart = false;
        bloom_last = -1;     // back on the game camera: started again
        return;
    }
    const double d = now - bloom_last;
    fp.bloom.restart = bloom_last < 0 || d < 0 || d > 8;
    fp.bloom.renders = fp.bloom.restart ? 1.0f : float(d);
    bloom_last = now;
    if (std::getenv("B3_POST_TRACE"))
        std::fprintf(stderr, "post trace: tick %zu bloom c %.4f k %.3f %.3f %.3f intensity %.3f keep %.3f mode %d renders %.2f restart %d;"
                     " directional %.3f centre (%.3f, %.3f) zoom %.5f; radial %.3f; choice %d\n", f.tick, double(fp.bloom.c), double(fp.bloom.k1),
                     double(fp.bloom.k2), double(fp.bloom.k3), double(fp.bloom.intensity), double(fp.bloom.keep), fp.bloom.mode,
                     double(fp.bloom.renders), int(fp.bloom.restart), double(fp.dblur.amount), double(fp.dblur.centre[0]),
                     double(fp.dblur.centre[1]), double(fp.dblur.zoom[0]), double(fp.blur.amount), p.choice);
    if (std::getenv("B3_POST_TRACE"))
        std::fprintf(stderr, "post trace: captured directional on %d amount %.4f centre (%.3f, %.3f) zoom %.5f time %.3f; radial amount %.3f crashed %d\n",
                     int(p.dir_on), double(p.dir_amount), double(p.dir_centre[0]), double(p.dir_centre[1]), double(p.dir_zoom[0]),
                     double(p.dir_time), double(f.blur.amount), int(f.blur.crashed));
}

int View::car_file(const ModelHead& head) {
    const auto it = head_file.find(head);
    if (it != head_file.end()) return it->second;
    return head_file[head] = match_car(cars, head, nullptr);
}

b3view::CarModel* View::car_model(b3gfx::Device& dev, int file, int paint, int lod) {
    const auto& cf = cars[std::size_t(file)];
    const int block = b3view::car_lod_block(cf.bytes, lod);
    auto& slot = car_models[{file, paint, block}];
    if (slot) return slot->scene.items.empty() ? nullptr : slot.get();
    slot = std::make_unique<b3view::CarModel>();
    std::string why;
    if (!b3view::load_car_model(dev, cf.bytes, paint, block, *slot, &why)) {
        std::fprintf(stderr, "car %s: %s\n", cf.path.c_str(), why.c_str());
        return nullptr;
    }
    return slot.get();
}

namespace {
b3view::RowMat row(const Mat34& m) { b3view::RowMat o; std::memcpy(o.r, m.r, sizeof o.r); return o; }
}  // namespace

std::vector<b3asset::TrackAnimState> View::anim_states(const Frame& f, bool* captured) const {
    const auto& anims = hi.model.track_anims;
    std::vector<b3asset::TrackAnimState> out(anims.size());
    bool all = f.anim_valid && f.anim.size() == anims.size();
    for (std::size_t k = 0; k < anims.size(); ++k) {
        if (all && f.anim[k].material == anims[k].material) {
            const AnimMaterial& a = f.anim[k];
            out[k] = b3asset::TrackAnimState{a.cur, a.p12, a.p13, a.period, a.t};
        } else {
            all = false;
            out[k] = b3asset::track_anim_at(anims[k], f.anim_now);
        }
    }
    if (captured) *captured = all;
    return out;
}

// mac5: each high-detail unit's items (hi_unit, then its instances hi_inst) with the world-space box of their
// vertices (the model's draw ranges: items k map to model.draws[k - item_base])
void View::build_track_boxes() {
    track_boxes.clear();
    std::map<int, std::vector<int>> by_unit;
    for (const auto& [u, ks] : hi_unit) if (!skip_units.count(u)) by_unit[u].insert(by_unit[u].end(), ks.begin(), ks.end());
    for (const auto& [u, ks] : hi_inst) if (!skip_units.count(u)) by_unit[u].insert(by_unit[u].end(), ks.begin(), ks.end());
    for (auto& [u, ks] : by_unit) {
        UnitBox b{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}, ks};
        bool any = false;
        for (int k : ks) {
            const std::size_t d = std::size_t(k) - hi.item_base;
            if (std::size_t(k) < hi.item_base || d >= hi.model.draws.size()) { any = false; break; }   // no range: never cull
            const auto& r = hi.model.draws[d];
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi3[3] = {-1e30f, -1e30f, -1e30f};
            bool got = false;
            for (std::uint32_t i = r.first_index; i < r.first_index + r.index_count && i < hi.model.indices.size(); ++i) {
                const std::uint32_t vi = hi.model.indices[i];
                if (vi >= hi.model.vertices.size()) continue;
                const float* p = hi.model.vertices[vi].pos;
                for (int a = 0; a < 3; ++a) { lo[a] = std::min(lo[a], p[a]); hi3[a] = std::max(hi3[a], p[a]); }
                got = true;
            }
            if (!got) continue;
            const float* M = hi.items[std::size_t(k)].model;   // the item's placement (column-major; identity for the track)
            for (int c = 0; c < 8; ++c) {
                const float x = (c & 1) ? hi3[0] : lo[0], y = (c & 2) ? hi3[1] : lo[1], z = (c & 4) ? hi3[2] : lo[2];
                const float w[3] = {M[0] * x + M[4] * y + M[8] * z + M[12], M[1] * x + M[5] * y + M[9] * z + M[13],
                                    M[2] * x + M[6] * y + M[10] * z + M[14]};
                for (int a = 0; a < 3; ++a) { b.lo[a] = std::min(b.lo[a], w[a]); b.hi[a] = std::max(b.hi[a], w[a]); }
            }
            any = true;
        }
        if (!any) { for (int a = 0; a < 3; ++a) { b.lo[a] = -1e30f; b.hi[a] = 1e30f; } }
        track_boxes.push_back(std::move(b));
    }
}

void View::draw_list(b3gfx::Device& dev, const Frame& f, const std::vector<ModelHead>& heads, bool game_cull,
                     const float eye[3], std::vector<b3gfx::DrawItem>& out) {
    // perf1: per-stage CPU times for b3replay --profile (prof_on)
    auto lap = [this, t = prof_on ? prof_clock() : 0](int k) mutable { if (!prof_on) return; const std::uint64_t n = prof_clock(); prof_ns[std::size_t(k)] += n - t; t = n; };
    if (animate) {   // animated track materials (anim1): frame textures and UV scroll of the route's items for this frame
        const auto st = anim_states(f);
        b3view::animate(hi, st);
        b3view::animate(lo, st);
    }
    {
        // perf1 (§33.7): a free camera gets the cloud layer faded at its open top; B3SKY_SKIP (diagnostics) leaves out sky
        // items by bit (0, 1: the dome, 2: the clouds)
        const char* skip = std::getenv("B3SKY_SKIP");
        const unsigned m = skip ? unsigned(std::strtoul(skip, nullptr, 0)) : 0U;
        for (std::size_t k = 0; k < sky.items.size(); ++k)
            if (!(m >> k & 1U)) out.push_back(free_view && int(k) == sky.cloud_item ? sky.cloud_free : sky.items[k]);
    }
    std::set<int> done;
    if (whole_track && !game_cull) {   // mac5: the whole track (high detail), no streaming pop-in in the distance
        if (track_boxes.empty()) build_track_boxes();
        // frustum cull per unit against the game camera, with a wide horizontal field (aspect 4: nothing a window
        // shows is lost); a free camera draws everything
        float m[16];
        game_view_proj(f.cam, 4.0f, m);
        for (const UnitBox& b : track_boxes) {
            bool cull = false;
            if (!free_view) {
                int out[5] = {0, 0, 0, 0, 0};   // corners outside: x < -w, x > w, y < -w, y > w, w <= 0
                for (int c = 0; c < 8; ++c) {
                    const float x = (c & 1) ? b.hi[0] : b.lo[0], y = (c & 2) ? b.hi[1] : b.lo[1], z = (c & 4) ? b.hi[2] : b.lo[2];
                    const float cx = m[0] * x + m[4] * y + m[8] * z + m[12], cy = m[1] * x + m[5] * y + m[9] * z + m[13];
                    const float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
                    out[0] += cx < -cw; out[1] += cx > cw; out[2] += cy < -cw; out[3] += cy > cw; out[4] += cw <= 0.0f;
                }
                for (int k = 0; k < 5; ++k) cull = cull || out[k] == 8;
            }
            if (!cull) for (int k : b.items) out.push_back(hi.items[std::size_t(k)]);
        }
    }
    for (int j = 0; j < 17 && !(whole_track && !game_cull); ++j) {
        const int u = f.slot_unit[j];
        if (u < 0 || done.count(u) || (game_cull && (f.vis[j] & 3) == 0) || skip_units.count(u)) continue;
        done.insert(u);
        const bool high = f.slot_kind[j] == 0;
        const auto& items = high ? hi_unit : lo_unit;
        const auto& sc = high ? hi : lo;
        if (auto it = items.find(u); it != items.end()) for (int k : it->second) out.push_back(sc.items[std::size_t(k)]);
        if (auto it = hi_inst.find(u); it != hi_inst.end()) for (int k : it->second) out.push_back(hi.items[std::size_t(k)]);
    }
    for (int b : f.backdrops)
        if (auto it = backdrop.find(b); b >= 0 && it != backdrop.end() && !skip_backdrops.count(b)) for (int k : it->second) out.push_back(hi.items[std::size_t(k)]);
    for (int k : always) {
        if (const auto ip = item_prop.find(k); ip != item_prop.end() &&
            (f.props_off == 1 || std::binary_search(f.hidden.begin(), f.hidden.end(), std::uint16_t(ip->second))))
            continue;   // no props in this mode, or dynamised / gone: CB3PropManager::Render skips state 2
        out.push_back(hi.items[std::size_t(k)]);
    }
    // Water (live2 §20): RenderBeforeVehicles draws, when D_00665F1D is set, the STATIC +0x2C object the camera unit's
    // high block +0x6E names (plain static objects) and the +0x30 object +0x6F names through material type 7, whose
    // program 0x47E300 mirrors every vertex about y = the block's +0x70 [C asm 0x0055-0x005C: view matrix y column
    // negated, translation + 2 level y column]: the river's reflections. Before live2 every water object was drawn
    // as is, so the reflection geometry stood unmirrored over the road (race1's bridge exit, the user's "pop-in").
    // water1 (§21): the reflection's type-7 shading (= type 0, no back-face test), as RenderWater does
    // (b3view::WaterTable::append; the same table draws b3view's water).
    if (f.water_on != 0) water.append(hi, f.current_unit, out, skip_water);
    for (const auto& p : f.props) {   // the dynamic list, at the placements' live matrices (props1)
        const auto it = prop_items.find(p.placement);
        if (it == prop_items.end()) continue;
        for (int k : it->second) {
            b3gfx::DrawItem d = hi.items[std::size_t(k)];
            to_colmajor(p.m, d.model);
            out.push_back(d);
        }
    }
    lap(0);
    // Cars (render5): RenderVehicle's draws per vehicle, at the LOD the game's Draw code picks from its camera
    frame_now = f.fx.now;   // traffic-popin: the clock the traffic fade-in (Vehicle::since) is measured on
    vehicle_draws(dev, f.cam, f.vehicles, f.vehicles, heads, game_cull, eye, car_globals, out, true);
    vehicle_fades.resize(f.vehicles.size());   // traffic-popin: the shadows and lights fade with their cars
    for (std::size_t k = 0; k < f.vehicles.size(); ++k) vehicle_fades[k] = traffic_fade(f.vehicles[k], frame_now, f.cam.m.r[3]);
    if (ghost) ghost_draws(dev, f, eye, out);   // ghost1: the Time Attack ghost (none unless GHOST is on and a lap is loaded)
    lap(1);
    // Effects stage (fx1): CB3World::Render draws RenderShadows and RenderPreFogEffects after the cars, the specular
    // and the props; b3gfx draws DrawItem::effect after everything else, in this order
    fx_stats = EffectStats{};
    effects.particles = show_particles;
    if (show_effects) effects.draw(dev, f.vehicles, f.fx, out, &fx_stats, &vehicle_fades);
    lap(2);
    // then CB3GameData::Draw after the fog pass, then func_0021C010's STATIC +0x28 entry (crash2)
    crash_draws(f, out);
    if (crash_objects && f.crash.barriers == 1)
        if (const auto it = barrier.find(f.crash.barrier_group); it != barrier.end())
            for (int k : it->second) out.push_back(hi.items[std::size_t(k)]);
    // Coronas (corona1, §16): CB3EffectsManager::RenderPostFogEffects 0x261E00 draws the three corona arrays after the
    // fog pass and CB3GameData::Draw, when D_00665F2C is set [C matched]. The slots are rebuilt for the displayed
    // vehicles and camera (the camera point +0x1F810 = position - 2 x at: within 0.5 mm of the captured value), so
    // they move with the interpolated cars; light states, boost intensity and its flicker are the tick's.
    // Popcorn (fx2, §19.3): first of RenderPostFogEffects 0x261E00's draws (crash only); the set-up from the tick's game
    // camera, the sprites at the displayed time facing the displayed camera
    lap(3);
    if (show_effects) effects.draw_popcorn(dev, f.fx, f.tick_cam, corona_view ? *corona_view : f.cam, out, &fx_stats);
    if (show_effects) effects.draw_particles(dev, f.fx, out, &fx_stats);   // effects3: the particle arrays (smoke, dust, glass, fire ...)
    lap(4);
    corona_stats = CoronaStats{};
    corona_arrays = CoronaArrays{};
    const CoronaInputs& ci = f.fx.coronas;
    if (show_effects && show_coronas && f.fx.effects_on && ci.valid && ci.k) {
        const Camera& cam = corona_view ? *corona_view : f.cam;
        float pos[3], point[3];
        for (int k = 0; k < 3; ++k) { pos[k] = cam.m.r[3][k]; point[k] = pos[k] - 2.0f * cam.m.r[2][k]; }
        build_coronas(ci, f.vehicles, point, pos, corona_arrays, &vehicle_fades);
        b3gfx::TextureId tex[3];
        for (int a = 0; a < 3; ++a) tex[a] = effects.texture(dev, ci.k->texture[a]);
        coronas.draw(dev, cam, corona_arrays, tex, out, &corona_stats, render_aspect);
    }
    lap(5);
    // Sparks (fx2, §19.2): RenderPostFogEffects 0x261E00 draws the six spark arrays after the coronas
    if (show_effects) effects.draw_sparks(dev, f.fx, out, &fx_stats);
    // The crash explosion's full-screen flash (§19.4): last of the effects drawn before the blue fill and func_00255C90's fades
    if (show_effects) {
        // B3_FLASH_FORCE=<tick> (a proof aid, drawn only): plays the flash as if the crash time had been stamped at that
        // tick (60 ticks a second), whatever the game did: the scripted scenarios never press the Crashbreaker button
        static const long force_tick = std::getenv("B3_FLASH_FORCE") ? std::atol(std::getenv("B3_FLASH_FORCE")) : -1;
        PostIn forced;
        const PostIn* post = &f.post;
        bool crashed = f.blur.crashed;
        if (force_tick >= 0 && long(f.tick) >= force_tick) {
            forced = f.post;
            forced.inputs = forced.has_crash_obj = true;
            forced.flags &= ~0x2000u;
            forced.crash_time = f.fx.now - float(long(f.tick) - force_tick) / 60.0f;
            post = &forced;
            crashed = true;
        }
        fx_stats.explosion_flash = effects.draw_explosion_flash(dev, f.fx, *post, crashed, corona_view ? *corona_view : f.cam, render_aspect,
                                                                 reduce_flashes, out);
    }
    lap(6);
}

// reflect1: PC SETTINGS > CAR REFLECTIONS on the draws added since `first`. 1 Original: the game's env pass untouched; 0 Off:
// the pass dropped (no reflection); 2 Enhanced: the pass replaced by car.frag's Fresnel sky / horizon / sun reflection at
// `strength` (0 where the env map is not the track's sky: the showroom's baked one keeps the game's pass).
void View::apply_car_reflections(std::vector<b3gfx::DrawItem>& out, std::size_t first, float strength) const {
    if (car_reflect == 1) return;
    for (std::size_t k = first; k < out.size(); ++k) {
        b3gfx::CarParams& c = out[k].car_mut();
        if (car_reflect == 0) c.env_mode = 0;
        else c.reflect = c.env_texture == b3gfx::kNone ? 0.0f : strength;
    }
}

void View::vehicle_draws(b3gfx::Device& dev, const Camera& cam, const std::vector<Vehicle>& vehicles, const std::vector<Vehicle>& lights,
                         const std::vector<ModelHead>& heads, bool game_cull, const float eye[3], const b3view::CarGlobals& globals,
                         std::vector<b3gfx::DrawItem>& out, bool hide_own) {
    for (const auto& v : vehicles) {
        if (v.model < 0 || std::size_t(v.model) >= heads.size()) continue;
        // CB3TrafficSystem::Draw's choice (game_draws): beyond 275 m always; the camera-dependent tests with V (render6).
        // A traffic vehicle not in the player car's list is never drawn, V or not: the list is game state, not the camera.
        // It drops the cars that are crashing (CB3TrafficVehicle::Update leaves +0x174 bit 2 out of it). In crash events
        // (+0x363B7 set) StartCrashing 0x167D10 does not Remove the hit car: its slot keeps following its lane, invisible,
        // while the physics wreck takes its place; drawn, it was the "duplicate" driving out of its own wreck.
        if (v.cull == Vehicle::kFar || v.cull == Vehicle::kNotListed || (game_cull && v.cull != Vehicle::kDrawn)) continue;
        // RenderRaceCars 0x199A20: the view's own car is not drawn while IsPlayerVisible is false (the look-back camera,
        // whose eye sits inside the car's body; cam1, §26). Game state, so with or without V.
        if (hide_own && v.own_hidden) continue;
        const int file = car_file(heads[std::size_t(v.model)]);
        if (file < 0) continue;
        const int lod = car_lod >= 0 ? car_lod : game_lod(cam, v);
        b3view::CarModel* cm = car_model(dev, file, v.paint, lod);
        if (!cm) continue;
        b3view::CarPose p;
        p.main = row(v.main);
        p.body = row(v.body);
        p.parts = v.parts;
        p.nbit = v.nbit;
        p.nwheel = v.nwheel;
        for (int k = 0; k < v.nbit; ++k) { p.bit[k] = row(v.bit[k]); p.bit_state[k] = v.bit_state[k]; }
        for (int k = 0; k < v.nwheel; ++k) { p.wheel[k] = row(v.wheel[k]); p.wheel_speed[k] = v.wheel_speed[k]; }
        p.lit = v.lit;
        p.glass = glass_damage ? v.glass : 0;   // settings1
        if (v.kind == VehicleKind::kTraffic)
            b3view::traffic_parts(*cm, v.spin, v.steer, v.spin_axis, v.steer_axis, v.speed, p);
        if (v.zones_valid && deform) {
            p.deformed = v.deformed;
            for (int z = 0; z < 8; ++z) p.zone[z] = row(v.zone[z]);
            for (int k = 0; k < v.nbit; ++k) {
                p.bit_deformed[k] = v.bit_deformed[k];
                for (int z = 0; z < 2; ++z) p.bit_zone[k][z] = row(v.bit_zone[k][z]);
            }
        }
        b3view::car_shading_scales(v.deformed, v.damage, v.exploded, v.explode_time, p.shininess, p.lighting);
        std::memcpy(p.sh, v.sh, sizeof p.sh);
        if (v.sh_count < 9) {
            // [G] the capture holds only L00 of traffic / crashed vehicles ([0, 0x60) of their EssentialParams):
            // the nearest race car's coefficients scaled to this L00
            const Vehicle* ref = nullptr;
            float best = 1e30f;
            for (const auto& u : lights) {
                if (u.sh_count < 9 || !(u.sh[0] > 0)) continue;
                float d2 = 0;
                for (int k = 0; k < 3; ++k) d2 += (u.main.r[3][k] - v.main.r[3][k]) * (u.main.r[3][k] - v.main.r[3][k]);
                if (d2 < best) { best = d2; ref = &u; }
            }
            if (ref && v.sh_count >= 1)
                for (int k = 0; k < 9; ++k) p.sh[k] = ref->sh[k] * (v.sh[0] / ref->sh[0]);
        }
        // traffic-popin (view.h traffic_fade): traffic fades in where it appears, out where it is removed and towards
        // the 275 m cut, as opacity (car.frag: fade < 0); race cars never (mac5's distance fade hid race cars beyond
        // 275 m, which the game draws). Below 1/256 nothing would show, but the car would still write Z: not drawn.
        const float fade = traffic_fade(v, frame_now, cam.m.r[3]);
        if (!(fade >= 1.0f / 256.0f)) continue;
        const std::size_t first = out.size();
        b3view::add_car_draws(*cm, globals, p, eye, cam.lod_pos, out);
        apply_car_reflections(out, first, globals.env_frame_units ? 0.0f : v.kind == VehicleKind::kTraffic ? carlook::current().traffic : 1.0f);
        if (fade < 1.0f)
            for (std::size_t k = first; k < out.size(); ++k) out[k].car_mut().fade = -fade;
    }
}

// ghost1 (ghost.h): the best lap's car at the current lap's time. A race car's parts as RenderVehicle draws an intact
// one: the panels at their file frames on the body, the wheels as recorded; SH lighting from the player's car (else
// the nearest race car); the race-car LOD rule from the game's camera.
void View::ghost_draws(b3gfx::Device& dev, const Frame& f, const float eye[3], std::vector<b3gfx::DrawItem>& out) {
    if (!ghost || ghost_file < 0 || std::size_t(ghost_file) >= cars.size() || !f.time_attack || f.lap_index < 0 || !f.cam.valid) return;
    ghost::At at;
    if (!ghost::locate(*ghost, f.lap_time, at)) return;
    const ghost::Sample& s0 = ghost->samples[at.i];
    const ghost::Sample& s1 = ghost->samples[std::min(at.i + 1, ghost->samples.size() - 1)];
    Vehicle g;
    g.kind = VehicleKind::kRaceCar;
    g.main = lerp(s0.main, s1.main, at.u);
    g.body = lerp(s0.body, s1.body, at.u);
    // within a few metres of the camera the ghost fades out (the chase camera never sits inside it)
    const float dx = g.main.r[3][0] - f.cam.m.r[3][0], dy = g.main.r[3][1] - f.cam.m.r[3][1], dz = g.main.r[3][2] - f.cam.m.r[3][2];
    const float fade = ghost::kFade * std::clamp((std::sqrt(dx * dx + dy * dy + dz * dz) - 2.0f) / 4.0f, 0.0f, 1.0f);
    if (!(fade > 0.0f)) return;
    const int lod = car_lod >= 0 ? car_lod : game_lod(f.cam, g);
    b3view::CarModel* cm = car_model(dev, ghost_file, ghost->paint, lod);
    if (!cm) return;
    b3view::CarPose p;
    p.main = row(g.main);
    p.body = row(g.body);
    p.parts = true;
    p.nbit = std::min(cm->npanel, 7);
    for (int k = 0; k < p.nbit; ++k) p.bit[k] = b3view::row_mul(cm->panel_frame[k], p.body);   // the file frame x the body
    p.nwheel = std::min(ghost->nwheel, 6);
    for (int k = 0; k < p.nwheel; ++k) {
        p.wheel[k] = row(lerp(s0.wheel[k], s1.wheel[k], at.u));
        p.wheel_speed[k] = s0.wheel_speed + (s1.wheel_speed - s0.wheel_speed) * at.u;
    }
    b3view::car_shading_scales(false, 0.0f, false, 0.0f, p.shininess, p.lighting);
    const Vehicle* light = nullptr;
    float best = 1e30f;
    for (const auto& v : f.vehicles) {
        if (v.kind != VehicleKind::kRaceCar || v.sh_count < 9) continue;
        if (v.id == kPlayerRaceCar) { light = &v; break; }
        float d2 = 0;
        for (int k = 0; k < 3; ++k) d2 += (v.main.r[3][k] - g.main.r[3][k]) * (v.main.r[3][k] - g.main.r[3][k]);
        if (d2 < best) { best = d2; light = &v; }
    }
    if (light) std::memcpy(p.sh, light->sh, sizeof p.sh);
    static const bool debug = std::getenv("B3GHOST_DEBUG") != nullptr;
    if (debug) {   // B3GHOST_DEBUG=1: where the ghost is drawn against the player's car (the time sync's check)
        const Vehicle* pl = nullptr;
        for (const auto& v : f.vehicles) if (v.id == kPlayerRaceCar && v.kind == VehicleKind::kRaceCar) pl = &v;
        float d2 = 0;
        for (int k = 0; k < 3 && pl; ++k) d2 += (pl->main.r[3][k] - g.main.r[3][k]) * (pl->main.r[3][k] - g.main.r[3][k]);
        std::printf("[b3ghost] drawn at lap time %.3f (tick %zu): ghost (%.2f, %.2f, %.2f), player (%.2f, %.2f, %.2f): %.3f m apart, fade %.2f\n",
                    double(f.lap_time), f.tick, double(g.main.r[3][0]), double(g.main.r[3][1]), double(g.main.r[3][2]),
                    pl ? double(pl->main.r[3][0]) : 0.0, pl ? double(pl->main.r[3][1]) : 0.0, pl ? double(pl->main.r[3][2]) : 0.0,
                    pl ? std::sqrt(double(d2)) : -1.0, double(fade));
    }
    const std::size_t first = out.size();
    b3view::add_car_draws(*cm, car_globals, p, eye, f.cam.lod_pos, out);
    apply_car_reflections(out, first, 1.0f);
    for (std::size_t k = first; k < out.size(); ++k) out[k].car_mut().fade = std::min(out[k].car().fade, fade);
}

// Crash-mode game data (crash2, §12.9): CB3GameData::Draw 0x14E8C0 draws pickups and ramps only with a stage logic
// (D_0051BA88 +0x1B4), each pickup unless taken (+0x8), each ramp unless D_00665EA1 [C asm]
void View::crash_draws(const Frame& f, std::vector<b3gfx::DrawItem>& out) const {
    const CrashState& c = f.crash;
    // perf1 (§33): the start / finish lines and split checkpoints, first in CB3GameData::Draw [C matched 0x14E8C0]
    if (finish_lines && c.valid && c.finish_known)
        for (const auto& l : c.finish) b3view::add_finish_line(crash_scene, l.pos, l.dir, l.half_h, l.half_w, l.texture, l.rgba, out);
    if (!crash_objects) return;
    const auto& stages = crash_scene.stages;
    const int st = c.valid ? stage_by_id(stages, c.stage) : fallback_stage;
    if (st < 0 || std::size_t(st) >= stages.size() || (c.valid && !c.logic)) return;
    if (!c.valid) {   // an older capture: the stage's file content, facing the drawn camera
        b3view::add_crash_stage(crash_scene, st, f.cam.m.r[0], out);
        return;
    }
    // the pickups face player 1's camera manager camera: GtMath's ConvertToMatrix right row of its quaternion
    float right[3];
    quat_right(c.cam_quat, right);
    for (const auto& p : c.pickups) if (!p.taken) b3view::add_crash_pickup(crash_scene, p.pos, p.type, right, out);
    const auto& ramps = crash_scene.ramps[std::size_t(st)];
    if (!c.ramps_off && c.ramps.size() == ramps.size()) out.insert(out.end(), ramps.begin(), ramps.end());
}

// The game camera as a reverse-Z view-projection: eye = pos, forward = at, up = up (right points to screen left in
// the game's frame, so right-handed look-at gives the game's picture unmirrored); vertical field from the view window
// (+0x4C), horizontal from the output aspect (Hor+).
void game_view_proj(const Camera& c, float aspect, float out[16]) {
    float v[16], p[16];
    b3gfx::mat4_look(c.m.r[3], c.m.r[2], c.m.r[1], v);
    b3gfx::mat4_perspective_reverse_z(2.0f * std::atan(c.half_h), aspect, std::max(0.05f, c.znear), p);
    b3gfx::mat4_mul(p, v, out);
}

}  // namespace b3replay

namespace b3replay {

// fe2 (§24): one GLOBE event's B3Im3d batches (globe_batches) as 2D-stage items with depth. The vertices are in the space
// B3Im3d is handed, which a screen matrix (as viewport +0x00: p = TransformPoint(S, v); 2D x = p.x / p.z x width, y =
// p.y / p.z x height, as DrawPositionMarker projects [L]) takes to the 2D space; the 2D space goes into the
// 4:3 box as hud_list's draws (fw, fh, the half output pixel). Depth: kGlobeDepth / p.z, reverse-Z, so the batches order
// among themselves as the GS's Z does [G constant: only the globe's own Z matters, the front end draws no 3D under it].
// Strips: perspective-correct through `model` (camera space -> clip); lines (PRIM AA1 in the game) as quads one 2D pixel
// wide [G], positioned in 2D with their depth per vertex. Back-face culling is left to the depth test (the rectangle
// DrawInvisiblePolys writes at the centre's depth hides the far half) [G: B3Im3d's BFC rule not read].
void View::globe_items(const HudGlobe& g, const std::function<b3gfx::TextureId(std::uint64_t)>& tex, float fw, float fh, int out_w, int out_h,
                       int width, int height, std::vector<b3gfx::Vertex>& vs, std::vector<std::uint32_t>& is,
                       std::vector<b3gfx::DrawItem>& items) const {
    constexpr float kGlobeDepth = 1.0e-3f;
    const float sx = 2.0f * fw / float(width), sy = 2.0f * fh / float(height);
    const float hx = out_w > 0 ? 1.0f / float(out_w) : 0.5f * sx, hy = out_h > 0 ? 1.0f / float(out_h) : 0.5f * sy;
    // The camera-space -> 2D map, as a screen matrix (rows 0..2 the basis, row 3 the translation; x / z, y / z in 0..1).
    // Viewport 0's +0x00 matrix is zero on the front end's pages [C globe1: every GLOBE event], so it is built from the
    // open viewport's view window (+0x48 / +0x4C): B3Im3d gets camera-space points for the globe (DrawInvisiblePolys
    // puts its bands at (+-half_w, half_h, 1), the screen's corners at depth 1 [L]); +x is the screen's left, as the
    // RwCamera frame's right row (§12.1) [L]; +y up.
    float V[4][4] = {};
    bool zero = true;
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) zero = zero && g.view[r][c] == 0.0f;
    if (!zero) std::memcpy(V, g.view, sizeof V);
    else if (g.k[0] > 0 && g.k[1] > 0) {
        V[0][0] = -0.5f / g.k[0];
        V[1][1] = -0.5f / g.k[1];
        V[2][0] = 0.5f;
        V[2][1] = 0.5f;
        V[2][2] = 1.0f;
    } else return;
    // camera space -> clip: rows over (p0, p1, p2, 1)
    const float R[4][4] = {{sx * float(width), 0, -fw + hx, 0}, {0, -sy * float(height), fh - hy, 0}, {0, 0, 0, kGlobeDepth}, {0, 0, 1, 0}};
    float strip_model[16];
    for (int r = 0; r < 4; ++r) {
        for (int i = 0; i < 3; ++i) strip_model[i * 4 + r] = R[r][0] * V[i][0] + R[r][1] * V[i][1] + R[r][2] * V[i][2];
        strip_model[12 + r] = R[r][0] * V[3][0] + R[r][1] * V[3][1] + R[r][2] * V[3][2] + R[r][3];
    }
    const float line_model[16] = {sx, 0, 0, 0, 0, -sy, 0, 0, 0, 0, 1, 0, -fw + hx, fh - hy, 0, 1};
    auto rgba = [](const std::uint8_t c[4]) {
        return std::uint32_t(c[0]) | std::uint32_t(c[1]) << 8 | std::uint32_t(c[2]) << 16 | std::uint32_t(c[3]) << 24;
    };
    std::vector<GlobeBatch> batches = globe_batches(g);
    const std::size_t merged_from = items.size();   // only the globe's own items merge
    // the wide layout (hud_wide, a display choice [G]): the globe is not clipped at the 4:3 box, so DrawInvisiblePolys'
    // top and bottom bands (its first two strips, at the window's edges x = +-half_w) reach the output's edges too
    if (hud_wide && fw < 1.0f && batches.size() >= 2)
        for (int k = 0; k < 2; ++k) for (GlobeVertex& v : batches[std::size_t(k)].v) v.pos[0] /= fw;
    for (const GlobeBatch& b : batches) {
        b3gfx::DrawItem it;
        it.overlay = true;
        it.overlay_ztest = true;
        it.overlay_zwrite = b.zwrite;
        it.shading = b3gfx::Shading::kTrack;
        it.track_type = 0;
        it.gs_alpha = 0x44;                     // B3Im3d's ALPHA_1 (blend by As) [C crash2 §12.9]
        it.textured = b.texture >= 0;
        it.texture = b3gfx::kNone;
        if (b.texture >= 0) {
            it.texture = g.tex_key[b.texture] ? tex(g.tex_key[b.texture]) : b3gfx::kNone;
            if (it.texture == b3gfx::kNone) continue;
            it.repeat_u = it.repeat_v = true;   // CLAMP_1 0 = REPEAT (B3Im3d) [L]; the surface's u runs past 1
        }
        it.first_index = std::uint32_t(is.size());
        if (b.kind == GlobeBatch::kStrip) {
            std::memcpy(it.model, strip_model, sizeof it.model);
            const std::uint32_t base = std::uint32_t(vs.size());
            for (const GlobeVertex& gv : b.v) {
                b3gfx::Vertex v{};
                std::memcpy(v.pos, gv.pos, sizeof v.pos);
                v.uv[0] = gv.uv[0];
                v.uv[1] = gv.uv[1];
                v.rgba = rgba(gv.rgba);
                vs.push_back(v);
            }
            for (std::uint32_t k = 0; k + 2 < std::uint32_t(b.v.size()); ++k) { is.push_back(base + k); is.push_back(base + k + 1); is.push_back(base + k + 2); }
        } else {
            std::memcpy(it.model, line_model, sizeof it.model);
            for (std::size_t k = 0; k + 1 < b.v.size(); k += 2) {
                float X[2], Y[2], D[2];
                bool ok = true;
                for (int e = 0; e < 2; ++e) {
                    const float* w = b.v[k + std::size_t(e)].pos;
                    float p[3];
                    for (int j = 0; j < 3; ++j) p[j] = w[0] * V[0][j] + w[1] * V[1][j] + w[2] * V[2][j] + V[3][j];
                    if (!(p[2] > 1e-6f)) { ok = false; break; }
                    X[e] = float(width) * p[0] / p[2];
                    Y[e] = float(height) * p[1] / p[2];
                    D[e] = kGlobeDepth / p[2];
                }
                if (!ok) continue;
                float nx = Y[0] - Y[1], ny = X[1] - X[0];
                const float l = std::sqrt(nx * nx + ny * ny);
                if (!(l > 0)) continue;
                nx *= 0.5f / l;
                ny *= 0.5f / l;
                const std::uint32_t base = std::uint32_t(vs.size());
                for (int q = 0; q < 4; ++q) {
                    const int e = q >> 1;
                    const float side = (q & 1) ? 1.0f : -1.0f;
                    b3gfx::Vertex v{};
                    v.pos[0] = X[e] + side * nx;
                    v.pos[1] = Y[e] + side * ny;
                    v.pos[2] = D[e];
                    v.rgba = rgba(b.v[k + std::size_t(e)].rgba);
                    vs.push_back(v);
                }
                for (std::uint32_t t : {0u, 1u, 2u, 1u, 3u, 2u}) is.push_back(base + t);
            }
        }
        it.index_count = std::uint32_t(is.size()) - it.first_index;
        if (!it.index_count) continue;
        // consecutive batches of one state share a draw (the meridians, the surface's 65 strips): their indices follow
        // each other, and within one state their order does not change the picture (lines: no Z write, alpha 0x80)
        if (!items.empty() && merged_from <= items.size() - 1) {
            b3gfx::DrawItem& p = items.back();
            if (p.texture == it.texture && p.textured == it.textured && p.overlay_zwrite == it.overlay_zwrite &&
                std::memcmp(p.model, it.model, sizeof it.model) == 0 && p.first_index + p.index_count == it.first_index) {
                p.index_count += it.index_count;
                continue;
            }
        }
        items.push_back(it);
    }
}

// fe3 (§30): how much a texture changes from one texel column to the next over the columns [u0, u1) and rows [v0, v1)
// it samples (normalised): the 99th percentile of the largest channel change. Stretching sideways shows nothing new when
// it is small: a single column (the prompt bar's strip), bg's soft vignette (5-8 levels); pictures, shapes and glyphs are
// far above (43-254; hud.h kFeSmooth). An HD pack replacement (img_w > w) is measured at the raster's own scale
// (columns and rows img_w / w apart) [G: the pack's front-end coverage not checked].
static float hud_texture_roughness(const HudTexture& t, float u0, float u1, float v0, float v1, float* mean_out = nullptr) {
    if (!t.ok || t.img_w <= 0 || t.img_h <= 0 || t.rgba.size() < std::size_t(t.img_w) * std::size_t(t.img_h) * 4) return 1e9f;
    auto col = [&](float u, bool end) { const float x = u * float(t.img_w); return std::clamp(int(end ? std::ceil(x - 0.01f) - 1 : std::floor(x + 0.01f)), 0, t.img_w - 1); };
    auto row = [&](float v, bool end) { const float y = v * float(t.img_h); return std::clamp(int(end ? std::ceil(y - 0.01f) - 1 : std::floor(y + 0.01f)), 0, t.img_h - 1); };
    const int c0 = col(std::min(u0, u1), false), c1 = std::max(c0, col(std::max(u0, u1), true));
    const int r0 = row(std::min(v0, v1), false), r1 = std::max(r0, row(std::max(v0, v1), true));
    const int sx = t.w > 0 && t.img_w > t.w ? std::max(1, t.img_w / t.w) : 1, sy = t.h > 0 && t.img_h > t.h ? std::max(1, t.img_h / t.h) : 1;
    std::uint32_t hist[256] = {};
    std::size_t n = 0;
    double sum = 0.0;
    for (int y = r0; y <= r1; y += sy)
        for (int x = c0 + sx; x <= c1; x += sx) {
            const std::uint8_t* a = t.rgba.data() + 4 * (std::size_t(y) * std::size_t(t.img_w) + std::size_t(x - sx));
            const std::uint8_t* b = a + 4 * std::size_t(sx);
            int m = 0;
            for (int k = 0; k < 4; ++k) m = std::max(m, std::abs(int(b[k]) - int(a[k])));
            ++hist[m];
            sum += m;
            ++n;
        }
    if (mean_out) *mean_out = n ? float(sum / double(n)) : 0.0f;
    if (n == 0) return 0.0f;
    // the 99th percentile of the largest channel change between neighbouring columns
    std::size_t acc = 0;
    for (int m = 0; m < 256; ++m) { acc += hist[m]; if (acc * 100 >= n * 99) return float(m); }
    return 255.0f;
}
// textsize1: CB3StylisedText2dObject::_RenderNormal (the race's stylised message texts, "Get Ready!" ...) [decomp names]
constexpr std::uint32_t kStylisedTextRender = 0x003CFE40U;
// fe3: the side fill's brightness at the backdrop's edge and at the window's edge (x the backdrop's vertex colour)
constexpr float kFillInner = 0.8f, kFillOuter = 0.35f, kFillDepth = 96.0f;

// fe3: a small, soft copy of an image for the side fill: an 8 x 8 box average, then two [1 2 1] passes each way
static std::vector<std::uint8_t> fill_blur(const std::uint8_t* rgba, int w, int h, int& ow, int& oh) {
    ow = std::max(4, w / 8);
    oh = std::max(4, h / 8);
    std::vector<float> a(std::size_t(ow) * std::size_t(oh) * 4, 0.0f), b(a.size());
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            const int x0 = x * w / ow, x1 = std::max(x0 + 1, (x + 1) * w / ow), y0 = y * h / oh, y1 = std::max(y0 + 1, (y + 1) * h / oh);
            // perf (2026-11-12): integer sums (exact, as the float sums of these few bytes were), converted once
            std::uint32_t acc[4] = {0, 0, 0, 0};
            for (int yy = y0; yy < y1; ++yy) {
                const std::uint8_t* row = rgba + 4 * (std::size_t(yy) * std::size_t(w) + std::size_t(x0));
                for (int xx = x0; xx < x1; ++xx, row += 4)
                    for (int k = 0; k < 4; ++k) acc[k] += row[k];
            }
            const float n = float((x1 - x0) * (y1 - y0));
            for (int k = 0; k < 4; ++k) a[4 * (std::size_t(y) * std::size_t(ow) + std::size_t(x)) + std::size_t(k)] = float(acc[k]) / n;
        }
    for (int pass = 0; pass < 4; ++pass) {
        const bool horiz = (pass & 1) == 0;
        for (int y = 0; y < oh; ++y)
            for (int x = 0; x < ow; ++x) {
                const int xm = horiz ? std::max(0, x - 1) : x, xp = horiz ? std::min(ow - 1, x + 1) : x;
                const int ym = horiz ? y : std::max(0, y - 1), yp = horiz ? y : std::min(oh - 1, y + 1);
                for (int k = 0; k < 4; ++k)
                    b[4 * (std::size_t(y) * std::size_t(ow) + std::size_t(x)) + std::size_t(k)] =
                        0.25f * a[4 * (std::size_t(ym) * std::size_t(ow) + std::size_t(xm)) + std::size_t(k)] +
                        0.5f * a[4 * (std::size_t(y) * std::size_t(ow) + std::size_t(x)) + std::size_t(k)] +
                        0.25f * a[4 * (std::size_t(yp) * std::size_t(ow) + std::size_t(xp)) + std::size_t(k)];
            }
        std::swap(a, b);
    }
    std::vector<std::uint8_t> out(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) out[i] = std::uint8_t(std::clamp(a[i] + 0.5f, 0.0f, 255.0f));
    return out;
}

// accessibility (a11y.h): what SCREEN FLASHES looks at in one 2D draw (its screen share, how light it draws, its blend)
a11y::FlashDraw View::hud_flash_draw(const HudFrame& hud, const HudDraw& d, const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures,
                                     std::string* name) {
    a11y::FlashDraw fd;
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    float vmax = 0, vmin = 0, amax = 0;
    for (const HudVertex& hv : d.tri) {
        x0 = std::min(x0, hv.x); x1 = std::max(x1, hv.x); y0 = std::min(y0, hv.y); y1 = std::max(y1, hv.y);
        const std::uint8_t* vc = d.tfx_decal && d.texture != 0 ? nullptr : hv.rgba;   // DECAL: the texture as it is
        vmax += vc ? float(std::max({vc[0], vc[1], vc[2]})) : 128.0f;
        vmin += vc ? float(std::min({vc[0], vc[1], vc[2]})) : 128.0f;
        amax = std::max(amax, vc ? float(vc[3]) : 128.0f);
    }
    if (!d.tri.empty()) { vmax /= float(d.tri.size()); vmin /= float(d.tri.size()); }
    const float right = float(hud.width), bottom = float(hud.height);
    x0 = std::max({x0, 0.0f, float(d.scissor[0])}); x1 = std::min({x1, right, float(d.scissor[2] + 1)});
    y0 = std::max({y0, 0.0f, float(d.scissor[1])}); y1 = std::min({y1, bottom, float(d.scissor[3] + 1)});
    fd.coverage = x1 > x0 && y1 > y0 && right > 0 && bottom > 0 ? (x1 - x0) * (y1 - y0) / (right * bottom) : 0.0f;
    float tlight = 1.0f;
    if (d.texture != 0) {
        const auto t = textures.find(d.texture);
        if (t != textures.end()) {
            if (name) *name = t->second->name;
            auto lt = hud_texture_light.find(d.texture);
            if (lt == hud_texture_light.end()) {   // its mean colour (the brightest channel), by coverage
                double sum = 0, wsum = 0;
                const std::vector<std::uint8_t>& px = t->second->rgba;
                for (std::size_t k = 0; k + 3 < px.size(); k += 4) {
                    const double w = double(px[k + 3]) / 255.0;
                    sum += w * double(std::max({px[k], px[k + 1], px[k + 2]}));
                    wsum += w;
                }
                lt = hud_texture_light.emplace(d.texture, wsum > 0 ? float(sum / wsum / 128.0) : 0.0f).first;
            }
            tlight = lt->second;
        }
    }
    fd.light = std::min(1.0f, vmax / 128.0f * tlight);   // MODULATE: Cv = Ct Cf >> 7 (untextured: Cf; 0x80 = the 2D's white)
    fd.white = std::min(1.0f, vmin / 128.0f * tlight);
    fd.alpha = amax / 128.0f;
    fd.gs_alpha = d.alpha;
    fd.blend = d.abe;
    fd.textured = d.texture != 0;
    return fd;
}

void View::hud_list(b3gfx::Device& dev, const std::shared_ptr<const HudFrame>& hud, const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures,
                    float aspect, std::vector<b3gfx::DrawItem>& out, int out_h, int out_w, const HudProjection* proj, bool frontend) {
    if (!show_hud || hud == nullptr || hud->width <= 0 || hud->height <= 0 || !(aspect > 0)) return;
    if (hud_built != hud || hud_aspect != aspect || hud_built_wide != hud_wide || hud_built_sharp != hud_sharp || hud_built_h != out_h ||
        hud_built_w != out_w || hud_built_frontend != frontend || hud_built_fe_layout != fe_layout || hud_built_movie_fill != movie_fill ||
        hud_built_prompt != prompt_device || !(hud_built_prompt_keys == prompt_keys) || hud_built_filter != hud_colour_filter ||
        hud_built_flashes != reduce_flashes || hud_built_fonts != hud_fonts || hud_built_size != hud_size || hud_built_series != series_glyphs) {
        hud_items.clear();
        hud_item_marker.clear();
        hud_item_draw.clear();   // splitlayout1
        hud_draw_pieces.assign(hud->draws.size(), {});   // mac5 (menu mouse)
        hud_pieces_frame = hud;
        // the 4:3 box: fractions of the output it covers
        const float fw = std::min(1.0f, (4.0f / 3.0f) / aspect), fh = std::min(1.0f, aspect / (4.0f / 3.0f));
        // mac5 (remaster-features §3): the button prompts' atlas for the device in use, at the 2D's pixel scale
        const bool prompts = prompts_substituted(prompt_device, prompt_keys);   // remap1: also the PlayStation with moved buttons
        const bool series = series_glyphs && frontend;   // carsel1: the car series' L1 / R1 glyphs (the PlayStation's too)
        if (prompts || series) {
            const int px_h = std::max(1, int(std::lround(float(out_h > 0 ? out_h : hud->height) * fh)));
            if (prompt_tex == b3gfx::kNone || prompt_atlas.device() != prompt_device || !(prompt_atlas.keys() == prompt_keys) ||
                prompt_atlas.built_for() != px_h) {
                prompt_atlas.build(prompt_device, prompt_keys, px_h);
                dev.update_texture(prompt_tex, prompt_atlas.width(), prompt_atlas.height(), prompt_atlas.rgba().data());
                prompt_tex_filter = 0;   // accessibility: the filtered copy is made again
                if (std::getenv("B3_PROMPT_TRACE"))
                    std::fprintf(stderr, "prompts: atlas %d x %d for %s at %d lines\n", prompt_atlas.width(), prompt_atlas.height(),
                                 prompt_device_name(prompt_device), px_h);
            }
        }
        // wide layout: the extra half width beyond the box, in 2D units (0: the 4:3 layout)
        const float ext = hud_wide && fw < 1.0f ? (1.0f / fw - 1.0f) * 0.5f * float(hud->width) : 0.0f;
        const float right = float(hud->width);
        // perf1 (§33): the EA TRAX "now playing" panel (texture "EATrax", 2D x 26..426 at the bottom left) goes with the
        // window's left edge in race frames; an overlay over a race made of the front end's panels (the pause menu, the
        // event intro: a heading bar or prompt bar and no HUD component draws) keeps hud2's bands and caps, and its
        // content goes to the side it is on (hud_overlay_shifts)
        const bool lay_fe = frontend;
        auto eatrax = [&](const HudDraw& d) {
            const auto t = d.texture != 0 ? textures.find(d.texture) : textures.end();
            return t != textures.end() && t->second->name == "EATrax";
        };
        // mac5 (Mac 2026-11-08): the panel's song text (glyph draws of the HUD's shared text object, not the EATrax
        // texture) stayed at its 4:3 place in a race. A draw lying wholly inside the EATrax panel's 4:3 box moves with
        // the panel (by object or component would also move the centre messages, TAILGATING, drawn by the same object).
        float trax_box[4] = {1e30f, 1e30f, -1e30f, -1e30f};
        for (const HudDraw& d : hud->draws)
            if (eatrax(d))
                for (const HudVertex& v : d.tri) {
                    trax_box[0] = std::min(trax_box[0], v.x); trax_box[1] = std::min(trax_box[1], v.y);
                    trax_box[2] = std::max(trax_box[2], v.x); trax_box[3] = std::max(trax_box[3], v.y);
                }
        auto trax_group = [&](const HudDraw& d) {
            if (eatrax(d)) return true;
            if (trax_box[0] > trax_box[2] || d.tri.empty()) return false;
            for (const HudVertex& v : d.tri)
                if (v.x < trax_box[0] - 2.0f || v.x > trax_box[2] + 2.0f || v.y < trax_box[1] - 2.0f || v.y > trax_box[3] + 2.0f) return false;
            return true;
        };
        std::vector<float> overlay_shift;
        std::vector<bool> overlay_vertexwise;
        if (!frontend && ext > 0 && hud_overlay_panels(*hud)) overlay_shift = hud_overlay_shifts(*hud, right, ext, &overlay_vertexwise);
        // output pixels per 2D pixel (vertical): the fonts' coverage edges are sharpened from 2x up (hud_sharp)
        const float mag = float(out_h) * fh / float(hud->height);
        const float sx = 2.0f * fw / float(hud->width), sy = 2.0f * fh / float(hud->height);
        std::vector<b3gfx::Vertex> vs;
        std::vector<std::uint32_t> is;
        const bool dbg = std::getenv("B3HUD_DEBUG") != nullptr;
        if (dbg && std::getenv("B3HUD_DEBUG_OBJECTS")) {   // each object event: pc, address, space, its draws
            std::vector<int> nd(hud->objects.size(), 0);
            for (const HudDraw& d : hud->draws) if (d.object >= 0 && std::size_t(d.object) < nd.size()) ++nd[std::size_t(d.object)];
            for (std::size_t i = 0; i < hud->objects.size(); ++i) {
                const HudObject& o = hud->objects[i];
                std::fprintf(stderr, "hud object %zu pc %08x addr %08x pos %.1f,%.1f size %.1f,%.1f draws %d\n", i, o.pc, o.addr,
                             double(o.pos[0]), double(o.pos[1]), double(o.size[0]), double(o.size[1]), nd[i]);
                for (const HudDraw& d : hud->draws) {
                    if (d.object != int(i) || d.tri.empty()) continue;
                    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
                    for (const auto& v : d.tri) { x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); y0 = std::min(y0, v.y); y1 = std::max(y1, v.y); }
                    const auto t = textures.find(d.texture);
                    std::fprintf(stderr, "    draw %zu tris tex %s box %.1f,%.1f-%.1f,%.1f rgba %02x%02x%02x%02x alpha %02X abe %d fbp %u aonly %d rgbmask %d tcc %d decal %d\n",
                                 d.tri.size() / 3, t != textures.end() ? t->second->name.c_str() : "-", x0, y0, x1, y1, d.tri[0].rgba[0], d.tri[0].rgba[1],
                                 d.tri[0].rgba[2], d.tri[0].rgba[3], d.alpha, d.abe, d.fbp, d.alpha_only, d.rgb_masked, d.tcc, d.tfx_decal);
                }
            }
        }
        if (dbg)
            for (const HudComponent& c : hud->components)
                std::fprintf(stderr, "hud component %08x frame %08x root %08x align %d player %d hud %d\n", c.addr, c.frame, c.root, c.align, c.player, c.hud);
        // wide layout: each object's anchor, -1 left / +1 right / 0 none: the side its textured edge pieces touch (a
        // triangle exactly at one edge of the 4:3 box; one at both edges stretches and does not vote), if they agree.
        // The object's other draws move with it, so a band laid against one edge stays in one piece (the event intro's
        // top band: big_curve at x 0, its translucent body and lines from x 76..161 to 640 [C roadrage1 3654]).
        // Draws without an object event (captures before hud2) are their own object.
        std::map<int, int> anchor;   // object (or -2 - draw index) -> side
        if (ext > 0 && !lay_fe) {
            std::map<int, int> votes;   // bit 0 left, bit 1 right
            for (std::size_t di = 0; di < hud->draws.size(); ++di) {
                const HudDraw& d = hud->draws[di];
                if (d.texture == 0) continue;
                // wide1: a draw inside a clip region narrower than the raster does not vote (it does not move with the
                // anchor either, cam1 below): the main menu's ticker shares its object with the d-pad prompt, and a
                // scrolling glyph whose edge lands on x 640 made the d-pad jump to the output's right edge for that
                // frame (33 of 224 frames, menu-menu1 polls 800..1100) [C]
                if (hud_in_clip_region(d, right)) continue;
                const int key = d.object >= 0 ? d.object : -2 - int(di);
                for (std::size_t t0 = 0; t0 + 2 < d.tri.size(); t0 += 3) {
                    float lo = d.tri[t0].x, hi = lo;
                    for (std::size_t k = t0 + 1; k < t0 + 3; ++k) { lo = std::min(lo, d.tri[k].x); hi = std::max(hi, d.tri[k].x); }
                    const bool tl = std::fabs(lo) < 0.5f, tr = std::fabs(hi - right) < 0.5f;
                    if (tl != tr) votes[key] |= tl ? 1 : 2;
                }
            }
            for (const auto& [key, b] : votes) anchor[key] = b == 1 ? -1 : b == 2 ? 1 : 0;
        }
        // fe3 (§30): the front end's panel layout, decided per element class for the whole frame
        const bool panels = lay_fe && ext > 0 && fe_layout == 2;
        HudFrontendPlan plan;
        if (panels) {
            auto roughness = [&](const HudDraw& d) {
                const auto t = textures.find(d.texture);
                if (t == textures.end()) return 1e9f;
                float u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
                for (const HudVertex& hv : d.tri) { u0 = std::min(u0, hv.u); u1 = std::max(u1, hv.u); v0 = std::min(v0, hv.v); v1 = std::max(v1, hv.v); }
                const std::uint64_t key = d.texture * 1000003ULL ^ std::uint64_t(std::lround(u0 * 4096.0f)) * 31ULL ^ std::uint64_t(std::lround(u1 * 4096.0f)) * 131071ULL ^
                                          std::uint64_t(std::lround(v0 * 4096.0f)) * 524287ULL ^ std::uint64_t(std::lround(v1 * 4096.0f)) * 8191ULL;
                auto c = roughness_cache.find(key);
                if (c == roughness_cache.end()) {
                    float mean = 0.0f;
                    const float rough = hud_texture_roughness(*t->second, u0, u1, v0, v1, &mean);
                    if (dbg) std::fprintf(stderr, "fe3 texture %s u %.3f..%.3f v %.3f..%.3f roughness p99 %.0f mean %.2f\n", t->second->name.c_str(), double(u0), double(u1), double(v0), double(v1), double(rough), double(mean));
                    c = roughness_cache.emplace(key, rough).first;
                }
                return c->second;
            };
            auto tv_static = [&](const HudDraw& d) {
                const auto t = textures.find(d.texture);
                return t != textures.end() && t->second->name == "Camchange";   // the game's TV static [C texture name]
            };
            plan = hud_frontend_plan(*hud, right, ext, roughness, tv_static);
        }
        // fe2 (§24): the World Tour globe's B3Im3d batches at their place among the draws
        std::size_t next_globe = 0;
        auto place_globes = [&](std::size_t upto) {
            for (; next_globe < hud->globes.size() && hud->globes[next_globe].draw_index <= upto; ++next_globe) {
                if (!show_globe) continue;
                auto tex = [&](std::uint64_t key) {
                    auto tt = hud_textures.find(key);
                    if (tt == hud_textures.end()) {
                        const auto t = textures.find(key);
                        b3gfx::TextureId id = b3gfx::kNone;
                        if (t != textures.end() && t->second->ok)
                            id = dev.create_texture({b3gfx::TextureLevel{t->second->img_w, t->second->img_h, t->second->rgba.data()}});
                        tt = hud_textures.emplace(key, id).first;
                    }
                    return tt->second;
                };
                globe_items(hud->globes[next_globe], tex, fw, fh, out_w, out_h, hud->width, hud->height, vs, is, hud_items);
                hud_item_marker.resize(hud_items.size(), -1);
            }
        };
        HudDraw prompt_draw;   // mac5: a button draw redrawn from the prompts' atlas (prompt_substitute)
        // accessibility (a11y.h): HUD COLOUR FILTER's matrix (off: nothing below changes a colour) and SCREEN FLASHES Reduced
        const a11y::ColourFilter colour_filter = a11y::colour_filter(hud_colour_filter);
        const bool filter_on = colour_filter != a11y::ColourFilter::kOff;
        const a11y::Matrix3 filter_m = a11y::daltonize_matrix(colour_filter);
        // SCREEN FLASHES Reduced: the TV static's frames 2..5 drawn from its first frame (no 15 Hz flicker; a11y.h)
        std::uint64_t static_first = 0;
        if (reduce_flashes)
            for (const auto& [key, t] : textures)
                if (t && t->name == a11y::kStaticFirstFrame) { static_first = key; break; }
        bool static_logged = false;
        const auto still_static = [&](std::uint64_t key) {
            if (!reduce_flashes || static_first == 0) return key;
            const auto t = textures.find(key);
            if (t == textures.end() || !a11y::static_frame(t->second->name)) return key;
            if (!static_logged && ++static_stills <= 5) std::printf("[a11y] TV static: %s drawn as %s (still)\n", t->second->name.c_str(), a11y::kStaticFirstFrame);
            static_logged = true;
            return static_first;
        };
        if (filter_on && (prompts || series) && prompt_tex != b3gfx::kNone && prompt_tex_filter != hud_colour_filter && !prompt_atlas.rgba().empty()) {
            std::vector<std::uint8_t> px = prompt_atlas.rgba();
            a11y::remap_pixels(filter_m, px.data(), px.size() / 4);
            dev.update_texture(prompt_tex_filtered, prompt_atlas.width(), prompt_atlas.height(), px.data());
            prompt_tex_filter = hud_colour_filter;
        }
        // mac5 (2026-11-10): the title's "Press START button to continue" with START drawn as the device's Start
        // button (prompts.h title_start_swap): its font draw without the word (title_text), then the button from the
        // prompts' atlas as one more item right after it (title_glyph); the PlayStation keeps the game's text
        std::map<std::size_t, HudDraw> title_text, title_glyph;
        if (prompts && hud_fonts && prompt_tex != b3gfx::kNone)
            for (std::size_t di = 0; di < hud->draws.size(); ++di) {
                const HudDraw& d = hud->draws[di];
                if (d.texture == 0 || d.video >= 0 || d.tri.size() < 6 * 26) continue;   // 26: the line's letters
                const auto t = textures.find(d.texture);
                if (t == textures.end() || !t->second) continue;
                for (const HostFont& f : hud_fonts->fonts) {
                    if (f.raster != t->second->raster) continue;
                    std::vector<HudVertex> text, glyph;
                    if (title_start_swap(d.tri, font_quad_chars(f, *t->second, d.tri), prompt_atlas, text, glyph)) {
                        HudDraw& td = title_text[di];
                        td = d;
                        td.tri = std::move(text);
                        HudDraw& gd = title_glyph[di];
                        gd = d;
                        gd.tri = std::move(glyph);
                        gd.wms = gd.wmt = 1;   // clamp
                        gd.linear = true;
                        gd.tfx_decal = false;
                        gd.tcc = true;
                        if (std::getenv("B3_PROMPT_TRACE"))
                            std::fprintf(stderr, "prompts: draw %zu (%s): the title's START drawn as %s's Start\n", di, t->second->name.c_str(),
                                         prompt_device_name(prompt_device));
                    }
                    break;
                }
            }
        // carsel1 (prompts.h series_glyphs): with CAR SERIES ON L1 / R1 on, the device's L1 / R1 beside the car pages'
        // series arrows: one more item right after each arrow's draw (title_glyph's steps), from the prompts' atlas
        if (series && prompt_tex != b3gfx::kNone) {
            std::vector<std::size_t> at;
            std::vector<const std::vector<HudVertex>*> arrows;
            for (std::size_t di = 0; di < hud->draws.size(); ++di) {
                const HudDraw& d = hud->draws[di];
                if (d.texture == 0 || d.video >= 0 || d.tri.size() < 6 || title_glyph.count(di)) continue;
                const auto t = textures.find(d.texture);
                if (t == textures.end() || !t->second || t->second->name != kSeriesArrowTexture) continue;
                at.push_back(di);
                arrows.push_back(&d.tri);
            }
            std::vector<std::vector<HudVertex>> glyphs;
            if (!arrows.empty() && b3replay::series_glyphs(arrows, prompt_atlas, glyphs)) {
                for (std::size_t k = 0; k < at.size(); ++k) {
                    if (glyphs[k].empty()) continue;
                    HudDraw& gd = title_glyph[at[k]];
                    gd = hud->draws[at[k]];
                    gd.tri = std::move(glyphs[k]);
                    gd.wms = gd.wmt = 1;   // clamp
                    gd.linear = true;
                    gd.tfx_decal = false;
                    gd.tcc = true;
                }
                if (series_glyph_frames++ == 0)
                    std::printf("[prompts] car series: %s's L1 / R1 drawn beside the series arrows\n", prompt_device_name(prompt_device));
            }
        }
        // menu-font (2026-11-10, Windows: DRIVER DETAILS > TROPHIES' scrolling hint cut at the 4:3 edge in mid-bar):
        // the ticker (CB3ScrollingText2dObject) lays out only the words inside its clip region (16..624), so in the panel
        // layout, whose bar runs to the window's edges, it is laid out again over the strip widened by ext each side
        // (hud_ticker_layout: the game's own layout, the text entering at the wide strip's right end) with its clip
        // widened as far: the PS2's 16-unit inset from the bar's ends, at the window's edges. The glyphs go in the
        // object's first draw of the font; its other draws of the font are emptied.
        std::map<std::size_t, HudDraw> ticker_draw;
        static const bool ticker_43 = std::getenv("B3_TICKER_43") != nullptr;   // the game's draw as before (comparisons)
        if (panels && hud_fonts && !ticker_43)
            for (std::size_t di = 0; di < hud->draws.size(); ++di) {
                const HudDraw& d = hud->draws[di];
                if (d.texture == 0 || d.video >= 0 || d.tri.empty() || d.object < 0 || std::size_t(d.object) >= hud->objects.size()) continue;
                const HudObject& o = hud->objects[std::size_t(d.object)];
                if (o.ticker_font == 0 || !hud_in_clip_region(d, right)) continue;
                const auto t = textures.find(d.texture);
                if (t == textures.end() || !t->second || t->second->raster != o.ticker_font || t->second->w <= 0 || t->second->h <= 0) continue;
                bool first = true;
                for (const auto& [k, td] : ticker_draw) first = first && td.object != d.object;
                HudDraw& td = ticker_draw[di];
                td = d;
                td.tri.clear();
                if (!first) continue;
                const HostFont* f = nullptr;
                for (const HostFont& hf : hud_fonts->fonts) if (hf.raster == o.ticker_font) f = &hf;
                static const bool check = std::getenv("B3_TICKER_CHECK") != nullptr;
                if (check && f) {   // diagnostics: the layout at ext 0 against the game's own draw(s) of the object
                    std::vector<HudVertex> mine, game;
                    hud_ticker_layout(o, *f, float(t->second->tw) / float(t->second->w), float(t->second->th) / float(t->second->h), 0.0f,
                                      d.tri[0].rgba, d.tri[0].z, mine);
                    game = d.tri;
                    const auto solid = [](std::vector<HudVertex>& v) {   // the triangles with an area (strip joins dropped)
                        std::vector<HudVertex> o2;
                        for (std::size_t k = 0; k + 2 < v.size(); k += 3) {
                            const float ar = (v[k + 1].x - v[k].x) * (v[k + 2].y - v[k].y) - (v[k + 2].x - v[k].x) * (v[k + 1].y - v[k].y);
                            if (std::fabs(ar) > 1e-4f) o2.insert(o2.end(), v.begin() + std::ptrdiff_t(k), v.begin() + std::ptrdiff_t(k + 3));
                        }
                        v.swap(o2);
                    };
                    solid(mine);
                    solid(game);
                    float dx = 0, duv = 0;
                    for (std::size_t k = 0; k < std::min(mine.size(), game.size()); ++k) {
                        dx = std::max({dx, std::fabs(mine[k].x - game[k].x), std::fabs(mine[k].y - game[k].y)});
                        duv = std::max({duv, std::fabs(mine[k].u - game[k].u), std::fabs(mine[k].v - game[k].v)});
                    }
                    std::fprintf(stderr, "ticker check: scroll %.3f: %zu vertices laid out, %zu drawn; max |dxy| %.4f |duv| %.6f\n",
                                 double(o.ticker_scroll), mine.size(), game.size(), double(dx), double(duv));
                }
                if (!f || !hud_ticker_layout(o, *f, float(t->second->tw) / float(t->second->w), float(t->second->th) / float(t->second->h), ext,
                                             d.tri[0].rgba, d.tri[0].z, td.tri)) {
                    ticker_draw.erase(di);   // no record: the game's draw as it is
                    continue;
                }
                td.scissor[0] = int(std::lround(float(d.scissor[0]) - ext));
                td.scissor[2] = int(std::lround(float(d.scissor[2]) + ext));
            }
        // textsize1 (accessibility, HUD SIZE; a11y.h): the race HUD's components scaled about their EHUDAlign anchors. Per
        // draw: its group or -1 (drawn as the game has it). Groups: a listed component (align 0..7 about its anchor; align
        // 8, the race's messages, about its own centre), the EA TRAX panel, and a stylised message text drawn outside the
        // listed components (CB3StylisedText2dObject::_RenderNormal 0x3CFE40: "Get Ready!") per object. Left as they are:
        // the front end and the overlays made of its panels over a race (the pause menu, the event intro: menus stay at
        // 100 %), split-screen frames, the markers over cars, bands across the screen. Per group: its box where its draws
        // land (after the wide layout's shift, inside their scissors), its pivot and the scale it may take (fit_scale:
        // on screen; resolve_overlaps: apart from the others).
        std::vector<int> size_group;
        std::vector<a11y::Part> size_parts;
        const float size_want = a11y::hud_size_scale(hud_size);
        if (size_want > 1.0f && !frontend && !hud->split && !hud_overlay_panels(*hud)) {
            const int nc = int(hud->components.size());
            size_group.assign(hud->draws.size(), -1);
            size_parts.assign(std::size_t(nc) + 1, a11y::Part{});   // the components, then the EA TRAX panel, then the free texts
            std::vector<float> group_shift(size_parts.size(), 0.0f);
            std::map<int, int> text_group;   // object -> group (free stylised texts)
            for (std::size_t di = 0; di < hud->draws.size(); ++di) {
                const HudDraw& d = hud->draws[di];
                if (d.tri.empty() || (d.rgb_masked && !d.alpha_only) || int(d.fbw) * 64 != hud->width) continue;
                if (d.scissor[2] < d.scissor[0] || d.scissor[3] < d.scissor[1] || d.marker >= 0 || d.video >= 0) continue;
                a11y::Box b, inner;   // inner: without the vertices on a 4:3 edge (they stay at the window's edge)
                bool edge = false;
                for (const HudVertex& hv : d.tri) {
                    b.add(hv.x, hv.y);
                    const bool at_edge = std::fabs(hv.x) < 0.5f || std::fabs(hv.x - right) < 0.5f;
                    edge = edge || at_edge;
                    if (!at_edge) inner.add(hv.x, hv.y);
                }
                // (as the draw loop: wholly outside its PS2 scissor, nothing drawn)
                if (b.x1 <= float(d.scissor[0]) || b.x0 >= float(d.scissor[2] + 1) || b.y1 <= float(d.scissor[1]) || b.y0 >= float(d.scissor[3] + 1)) continue;
                // bands and fills across the screen (both 4:3 edges, or most of its width) stay
                if (b.x1 - b.x0 > 0.75f * right || (b.x0 < 0.5f && b.x1 > right - 0.5f)) continue;
                int g = -1;
                float shift = 0.0f;
                if (trax_group(d)) {
                    if (edge || (di < overlay_vertexwise.size() && overlay_vertexwise[di])) continue;
                    g = nc;
                    shift = ext > 0 ? -ext : 0.0f;
                } else {
                    if (di < overlay_shift.size() && overlay_shift[di] != 0.0f) continue;   // the pause menu's panels (menus stay)
                    if (d.object < 0 || std::size_t(d.object) >= hud->objects.size()) continue;
                    const HudObject& ob = hud->objects[std::size_t(d.object)];
                    const int c = ob.component;
                    if ((c < 0 || c >= nc) && ob.pc == kStylisedTextRender && !edge) {   // a free message text: its own group
                        auto [tg, fresh] = text_group.emplace(d.object, int(size_parts.size()));
                        if (fresh) { size_parts.emplace_back(); group_shift.push_back(0.0f); }
                        if (ext > 0) {   // (it moves with no anchor in the wide layout, unless its object has a side)
                            const auto a = anchor.find(d.object);
                            if (a != anchor.end() && a->second != 0 && !hud_in_clip_region(d, right)) continue;
                        }
                        g = tg->second;
                        size_group[di] = g;
                        b.x0 = std::max(b.x0, float(d.scissor[0]));
                        b.x1 = std::min(b.x1, float(d.scissor[2] + 1));
                        b.y0 = std::max(b.y0, float(d.scissor[1]));
                        b.y1 = std::min(b.y1, float(d.scissor[3] + 1));
                        size_parts[std::size_t(g)].box.add(b);
                        continue;
                    }
                    if (c < 0 || c >= nc || hud->components[std::size_t(c)].align < 0 || hud->components[std::size_t(c)].align > 8) continue;
                    // align 8 (absolute): the race's messages (TAKEDOWN, TAILGATING, AFTERTOUCH ...; the markers over cars
                    // are left out above): scaled about their own centre
                    const bool absolute = hud->components[std::size_t(c)].align == 8;
                    if (ext > 0 && !absolute) shift = (2.0f * hud->components[std::size_t(c)].fx - 1.0f) * ext;
                    // the shift the draw loop gives it: its object's side anchor (the wide layout's edge pieces: the POS /
                    // LAP panels' bars) or its component's; a draw that does not move with its component stays
                    float moved = shift;
                    if (ext > 0 && !hud_in_clip_region(d, right)) {
                        const auto a = anchor.find(d.object);
                        if (a != anchor.end() && a->second != 0) moved = float(a->second) * ext;
                    }
                    if (std::fabs(moved - shift) > 0.5f) continue;
                    // an edge piece (a panel's bar laid to the 4:3 edge) only in a side / corner component
                    // (textured: an untextured edge fill stretches in proportion in the wide layout)
                    if (edge && (absolute || hud->components[std::size_t(c)].fx == 0.5f || d.texture == 0)) continue;
                    g = c;
                }
                // its box where it lands, inside its scissor (an edge piece: its inner part, x)
                if (edge && !inner.empty()) { b.x0 = inner.x0; b.x1 = inner.x1; }
                b.x0 = std::max(b.x0, float(d.scissor[0])) + shift;
                b.x1 = std::min(b.x1, float(d.scissor[2] + 1)) + shift;
                b.y0 = std::max(b.y0, float(d.scissor[1]));
                b.y1 = std::min(b.y1, float(d.scissor[3] + 1));
                size_group[di] = g;
                size_parts[std::size_t(g)].box.add(b);
                group_shift[std::size_t(g)] = shift;
            }
            a11y::Box rect;   // the HUD rect (CB3HUD +0x48 / +0x50), else the raster
            if (hud->hud_rect[2] > hud->hud_rect[0] && hud->hud_rect[3] > hud->hud_rect[1]) {
                rect.add(hud->hud_rect[0], hud->hud_rect[1]);
                rect.add(hud->hud_rect[2], hud->hud_rect[3]);
            } else {
                rect.add(0, 0);
                rect.add(right, float(hud->height));
            }
            a11y::Box bounds;   // the visible 2D area: the window (the 4:3 box when the layout is 4:3)
            bounds.add(-ext, 0.0f);
            bounds.add(right + ext, float(hud->height));
            for (std::size_t g = 0; g < size_parts.size(); ++g) {
                a11y::Part& p = size_parts[g];
                if (p.box.empty()) continue;
                if ((g < std::size_t(nc) && hud->components[g].align == 8) || g > std::size_t(nc)) {   // a message: its own centre
                    p.pivot.x = 0.5f * (p.box.x0 + p.box.x1);
                    p.pivot.y = 0.5f * (p.box.y0 + p.box.y1);
                    p.message = true;
                } else if (g < std::size_t(nc)) {   // its anchor in the HUD rect (kept inside the window: the race HUD's
                                                    // bottom anchors lie below the raster, y 458 of 448)
                    const HudComponent& hc = hud->components[g];
                    p.pivot = a11y::anchor_pivot(hc.fx, hc.fy, rect, group_shift[g]);
                    p.pivot.x = std::clamp(p.pivot.x, bounds.x0, bounds.x1);
                    p.pivot.y = std::clamp(p.pivot.y, bounds.y0, bounds.y1);
                    p.message = hc.fx == 0.5f && hc.fy == 0.5f;
                } else {   // the EA TRAX panel: its own bottom-left corner (it sits at the window's left edge); a passing
                           // popup, it gives way to the steady HUD (the speedometer in its row)
                    p.pivot.x = p.box.x0;
                    p.pivot.y = p.box.y1;
                    p.message = true;
                }
                p.scale = a11y::fit_scale(p.box, p.pivot, size_want, bounds);
            }
            a11y::resolve_overlaps(size_parts, 2.0f);
            if (std::getenv("B3_HUD_SIZE_TRACE"))
                for (std::size_t g = 0; g < size_parts.size(); ++g) {
                    const a11y::Part& p = size_parts[g];
                    if (p.box.empty()) continue;
                    const HudComponent* hc = g < std::size_t(nc) ? &hud->components[g] : nullptr;
                    std::fprintf(stderr, "hud size: %s %08x align %d (%.1f, %.1f) box %.1f,%.1f-%.1f,%.1f pivot %.1f,%.1f scale %.3f of %.3f%s\n",
                                 hc ? "component" : g == std::size_t(nc) ? "EA TRAX" : "text", hc ? hc->addr : 0U, hc ? hc->align : -1, hc ? double(hc->fx) : 0.0,
                                 hc ? double(hc->fy) : 0.0, double(p.box.x0), double(p.box.y0), double(p.box.x1), double(p.box.y1),
                                 double(p.pivot.x), double(p.pivot.y), double(p.scale), double(size_want), p.message ? " (message)" : "");
                }
        }
        // the draws in order, a title glyph's step right after its text's
        std::vector<std::pair<std::size_t, bool>> steps;
        steps.reserve(hud->draws.size() + title_glyph.size());
        for (std::size_t di = 0; di < hud->draws.size(); ++di) {
            steps.emplace_back(di, false);
            if (title_glyph.count(di)) steps.emplace_back(di, true);
        }
        for (const auto& [di, glyph_step] : steps) {
            place_globes(di);
            const HudDraw* dp = &hud->draws[di];
            bool prompt_sub = false;
            if (glyph_step) {
                dp = &title_glyph[di];
                prompt_sub = true;   // its UVs point into the prompts' atlas
            } else if (const auto tt = title_text.find(di); tt != title_text.end()) {
                dp = &tt->second;
            } else if (const auto tk = ticker_draw.find(di); tk != ticker_draw.end()) {
                dp = &tk->second;   // menu-font: the ticker laid out over the wide strip
            } else if (prompts && dp->texture != 0 && dp->video < 0) {
                const auto t = textures.find(dp->texture);
                if (t != textures.end() && prompt_texture(t->second->name)) {
                    std::vector<HudVertex> tri;
                    prompt_sub = prompt_substitute(t->second->name, t->second->w, t->second->h, dp->tri, prompt_atlas, tri);
                    if (prompt_sub) {
                        prompt_draw = *dp;
                        prompt_draw.tri = std::move(tri);
                        dp = &prompt_draw;
                    } else if (std::getenv("B3_PROMPT_TRACE")) {
                        std::fprintf(stderr, "prompts: draw %zu of %s (%zu triangles) kept: not a known piece\n", di, t->second->name.c_str(), dp->tri.size() / 3);
                    }
                }
            }
            const HudDraw& d = *dp;
            if (dbg) {
                float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
                for (const auto& v : d.tri) { x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); y0 = std::min(y0, v.y); y1 = std::max(y1, v.y); }
                const auto t = textures.find(d.texture);
                std::fprintf(stderr, "hud draw: %zu tris tex %016llx (%s) clamp %d%d lin %d tcc %d alpha %02X abe %d aonly %d rgbmask %d fbp %u fbw %u scissor %d,%d-%d,%d box %.1f,%.1f-%.1f,%.1f rgba %02x%02x%02x%02x uv %.3f,%.3f\n",
                             d.tri.size() / 3, (unsigned long long)d.texture, t != textures.end() ? t->second->name.c_str() : "-", d.wms, d.wmt, d.linear, d.tcc, d.alpha, d.abe, d.alpha_only,
                             d.rgb_masked, d.fbp, d.fbw, d.scissor[0], d.scissor[1], d.scissor[2], d.scissor[3], x0, y0, x1, y1,
                             d.tri.empty() ? 0 : d.tri[0].rgba[0], d.tri.empty() ? 0 : d.tri[0].rgba[1], d.tri.empty() ? 0 : d.tri[0].rgba[2],
                             d.tri.empty() ? 0 : d.tri[0].rgba[3], d.tri.empty() ? 0.0 : double(d.tri[0].u), d.tri.empty() ? 0.0 : double(d.tri[0].v));
                if (std::getenv("B3HUD_DEBUG_V"))
                    for (std::size_t vi = 0; vi < d.tri.size(); ++vi)
                        std::fprintf(stderr, "    v%zu %.2f,%.2f uv %.4f,%.4f rgba %02x%02x%02x%02x\n", vi, double(d.tri[vi].x), double(d.tri[vi].y), double(d.tri[vi].u),
                                     double(d.tri[vi].v), d.tri[vi].rgba[0], d.tri[vi].rgba[1], d.tri[vi].rgba[2], d.tri[vi].rgba[3]);
                if (d.object >= 0 && std::size_t(d.object) < hud->objects.size()) {
                    const HudObject& o = hud->objects[std::size_t(d.object)];
                    const HudComponent* hc = o.component >= 0 ? &hud->components[std::size_t(o.component)] : nullptr;
                    std::fprintf(stderr, "  object %08x callback %08x component %08x align %d (%.2f, %.2f)\n", o.addr, o.pc,
                                 hc ? hc->addr : 0U, hc ? hc->align : -1, hc ? double(hc->fx) : 0.0, hc ? double(hc->fy) : 0.0);
                }
            }
            if (d.tri.empty() || (d.rgb_masked && !d.alpha_only)) continue;
            if (int(d.fbw) * 64 != hud->width) continue;   // Gt2d's passes into small offscreen targets (effects) [L]
            // an empty GS scissor (SetClipRegion 0x217A20 writes x 0..-1, y -1..0 for a region off the raster [C]) draws nothing
            if (d.scissor[2] < d.scissor[0] || d.scissor[3] < d.scissor[1]) continue;
            {   // mac5 (Mac 2026-11-08): a draw wholly outside its PS2 scissor drew nothing there [C GS]; the wide layout's
                // widened scissor showed it -- the pause menu's options parked left of the 4:3 screen before they slide in
                float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
                for (const HudVertex& hv : d.tri) {
                    x0 = std::min(x0, hv.x); x1 = std::max(x1, hv.x);
                    y0 = std::min(y0, hv.y); y1 = std::max(y1, hv.y);
                }
                if (x1 <= float(d.scissor[0]) || x0 >= float(d.scissor[2] + 1) || y1 <= float(d.scissor[1]) ||
                    y0 >= float(d.scissor[3] + 1))
                    continue;
            }
            b3gfx::DrawItem it;
            it.overlay = true;
            it.shading = b3gfx::Shading::kTrack;
            it.track_type = 0;
            it.textured = d.texture != 0;
            it.texture = b3gfx::kNone;
            if (d.video >= 0 && std::size_t(d.video) < hud->videos.size()) {
                // menu1 (§22): a movie's tiles: its captured picture (a dynamic texture per guest picture buffer), else
                // the placeholder (no picture in the capture: a file captured without B3PORT_SCENE_MOVIE)
                const HudVideo& hv = hud->videos[std::size_t(d.video)];
                it.textured = true;
                if (hv.picture && show_movies) {
                    b3gfx::TextureId& id = movie_tex.emplace(hv.pic, b3gfx::kNone).first->second;
                    auto& up = movie_up[hv.pic];
                    if (up != hv.picture || id == b3gfx::kNone) { dev.update_texture(id, hv.picture->w, hv.picture->h, hv.picture->rgba.data()); up = hv.picture; }
                    it.texture = id;
                } else {
                    if (movie_placeholder == b3gfx::kNone) {
                        // dark slate with diagonal stripes, every 16th texel: plainly not game art (2 x 2 repeats over a movie)
                        std::vector<std::uint8_t> px(64 * 64 * 4);
                        for (int y = 0; y < 64; ++y)
                            for (int x = 0; x < 64; ++x) {
                                const bool stripe = ((x + y) & 15) < 3;
                                std::uint8_t* q = px.data() + 4 * (y * 64 + x);
                                q[0] = stripe ? 70 : 34; q[1] = stripe ? 76 : 38; q[2] = stripe ? 92 : 50; q[3] = 255;
                            }
                        movie_placeholder = dev.create_texture({b3gfx::TextureLevel{64, 64, px.data()}});
                    }
                    it.texture = movie_placeholder;
                    it.repeat_u = it.repeat_v = true;
                }
                if (it.texture == b3gfx::kNone) continue;
            } else if (prompt_sub) {
                it.texture = filter_on && prompt_tex_filter == hud_colour_filter ? prompt_tex_filtered : prompt_tex;   // mac5: our glyphs (the UVs point into the atlas)
                if (it.texture == b3gfx::kNone) continue;
            } else if (d.texture != 0 && filter_on) {   // accessibility: HUD COLOUR FILTER's copy of the texture
                const std::uint64_t key = still_static(d.texture);
                auto tt = hud_textures_filtered.find({key, hud_colour_filter});
                if (tt == hud_textures_filtered.end()) {
                    const auto t = textures.find(key);
                    b3gfx::TextureId id = b3gfx::kNone;
                    if (t != textures.end() && t->second->ok) {
                        std::vector<std::uint8_t> px = t->second->rgba;
                        a11y::remap_pixels(filter_m, px.data(), px.size() / 4);
                        id = dev.create_texture({b3gfx::TextureLevel{t->second->img_w, t->second->img_h, px.data()}});
                    }
                    tt = hud_textures_filtered.emplace(std::make_pair(key, hud_colour_filter), id).first;
                }
                it.texture = tt->second;
                if (it.texture == b3gfx::kNone) continue;
            } else if (d.texture != 0) {
                const std::uint64_t key = still_static(d.texture);   // (d.texture unless SCREEN FLASHES Reduced)
                auto tt = hud_textures.find(key);
                if (tt == hud_textures.end()) {
                    const auto t = textures.find(key);
                    b3gfx::TextureId id = b3gfx::kNone;
                    if (t != textures.end() && t->second->ok) {
                        id = dev.create_texture({b3gfx::TextureLevel{t->second->img_w, t->second->img_h, t->second->rgba.data()}});
                        if (const char* dir = std::getenv("B3HUD_TEXDIR"))   // debugging: the decoded rasters as PNG
                            b3asset::write_png_rgba(std::string(dir) + "/" + t->second->name + "_" + std::to_string(t->second->palette) + ".png",
                                                    t->second->img_w, t->second->img_h, t->second->rgba.data());
                    }
                    tt = hud_textures.emplace(key, id).first;
                }
                it.texture = tt->second;
                if (it.texture == b3gfx::kNone) continue;   // a raster the capture could not decode: left out (--info counts them)
            }
            it.gs_alpha = d.alpha;
            it.overlay_blend = d.abe;
            it.alpha_only = d.alpha_only;
            it.point_sample = !d.linear;
            if (hud_sharp && mag >= 2.0f && d.linear && d.texture != 0 && !glyph_step) {
                const auto t = textures.find(d.texture);
                const std::string& n = t != textures.end() ? t->second->name : std::string();
                it.sharp_alpha = n.size() >= 4 && n.compare(n.size() - 4, 4, "Font") == 0;   // GlobalFont, HeadFont, SmallFont
                // mac5 (Mac 2026-11-08: the profile keyboard's text looked low resolution): SmallFont's strokes are
                // thinner than a texel in places, and a 0.5 cut broke them up (y, v, w, k at 2560 x 1440)
                static const float env_level = [] { const char* e = std::getenv("B3_SMALLFONT_LEVEL"); return e ? float(std::atof(e)) : -1.0f; }();
                // menu-font (2026-11-10, Windows ~2000 x 1110: the TROPHIES checklist "hard to read", hairline strokes):
                // SmallFont is the menus' thin light font (128 x 128, 1-texel strokes, diagonals split over two texels). The
                // PS2 shows it at 448 lines through bilinear filtering, so each stroke carries a soft half-texel fringe that
                // reads as weight; the cut keeps a stroke 2 (1 - cut) texels wide (a full-coverage one). 0.2: 1.6 texels,
                // and never under kSmallFontMinStrokePx output pixels (the cut lowered further near 2 x, at most to 0.1)
                if (n == "SmallFont")
                    it.sharp_level = env_level > 0.0f ? env_level
                                                      : std::clamp(std::min(small_font_level, 1.0f - kSmallFontMinStrokePx / (2.0f * mag)), 0.1f, 0.5f);
            }
            // CLAMP_1 REPEAT (WMS / WMT 0; REGION_REPEAT 3 taken as repeat): the GS wraps at 2^TW, the GPU at the decoded
            // width, the same for the rasters that use it (BoostBits 64 x 64 [C])
            if (d.video < 0) {
                it.repeat_u = d.texture != 0 && (d.wms == 0 || d.wms == 3);
                it.repeat_v = d.texture != 0 && (d.wmt == 0 || d.wmt == 3);
            }
            // the component's anchor shift (wide layout): its frame's place in the HUD rect, fx 0 / 0.5 / 1
            float shift = 0;
            if (ext > 0 && d.object >= 0 && std::size_t(d.object) < hud->objects.size()) {
                const int c = hud->objects[std::size_t(d.object)].component;
                if (c >= 0 && hud->components[std::size_t(c)].align >= 0 && hud->components[std::size_t(c)].align < 8)
                    shift = (2.0f * hud->components[std::size_t(c)].fx - 1.0f) * ext;
            }
            const bool trax = ext > 0 && !lay_fe && (trax_group(d) || (di < overlay_shift.size() && overlay_shift[di] != 0.0f));   // perf1
            if (trax) shift = trax_group(d) ? -ext : overlay_shift[di];
            // mac5 (Mac 2026-11-08): the menus' EA TRAX panel goes to the window's bottom-left corner too, whole
            // (not through the front end's band / panel rules)
            const bool trax_fe = ext > 0 && lay_fe && trax_group(d);
            if (trax_fe) shift = -ext;
            float dlo = 1e30f, dhi = -1e30f;   // the draw's extent (one object's run of one state)
            for (const HudVertex& hv : d.tri) { dlo = std::min(dlo, hv.x); dhi = std::max(dhi, hv.x); }
            // wide1: the front end's rules (hud_frontend_rule): a draw spanning the whole 4:3 width (a band, a panel,
            // the grid) or an untextured fill reaching an edge from inside extends; a movie never stretches
            const HudFrontendRule fe = panels ? plan.rule[di] : lay_fe && ext > 0 ? hud_frontend_rule(d, right) : HudFrontendRule{};
            // in the front end only the draws that extend see past the 4:3 clip: everything else ends at the PS2
            // screen's edge as before (the selection's sweeping ghost text, things parked off screen)
            const bool fe_extends = fe.extends();
            if (fe.tile) it.repeat_u = true;   // mac5: the garage's static tiles over the added width
            float sc0 = float(d.scissor[0]), sc2 = float(d.scissor[2] + 1);
            const bool clip_region = hud_in_clip_region(d, right);
            int side = 0;
            if (ext > 0 && !clip_region && !lay_fe) {
                const auto a = anchor.find(d.object >= 0 ? d.object : -2 - int(di));
                if (a != anchor.end() && !trax) side = a->second;
            }
            if (ext > 0 && (!lay_fe || fe_extends || trax_fe)) {   // the full-width scissor widens with the output; a clip region moves with its component
                sc0 = sc0 <= 0.0f ? -ext : sc0 + shift;
                sc2 = sc2 >= right ? right + ext : sc2 + shift;
                // fe3: a draw that moves whole with a side-aligned component (no edge piece, no object anchor) keeps the
                // PS2's clip on its far side, moved with it: what lay past the 4:3 edge was never on screen. The boost
                // bar's background in the takedown camera runs to x 726.6 (roadrage1 8220), and its 640..727 tail showed
                // in mid-window once the bar moved left. Edge pieces and absolute components (the scrolling combo text)
                // keep the widened scissor (hud2).
                bool edge_piece = false;
                for (const HudVertex& hv : d.tri) edge_piece = edge_piece || std::fabs(hv.x) < 0.5f || std::fabs(hv.x - right) < 0.5f;
                if (!lay_fe && !clip_region && side == 0 && !edge_piece) {
                    if (shift < 0.0f && sc2 > right + shift) sc2 = right + shift;
                    if (shift > 0.0f && sc0 < shift) sc0 = shift;
                }
            }
            float sy0 = float(d.scissor[1]), sy1 = float(d.scissor[3] + 1);
            // textsize1 (HUD SIZE): a scaled draw's clip sides inside the window scale with it (a clip region, the far-side
            // clip above); the window's own edges stay
            const a11y::Part* size_part = di < size_group.size() && size_group[di] >= 0 ? &size_parts[std::size_t(size_group[di])] : nullptr;
            if (size_part && size_part->scale == 1.0f) size_part = nullptr;
            if (size_part) {
                const a11y::Pivot& pv = size_part->pivot;
                const float s = size_part->scale;
                if (sc0 > -ext + 0.5f) sc0 = std::max(-ext, a11y::scale_about(sc0, pv.x, s));
                if (sc2 < right + ext - 0.5f) sc2 = std::min(right + ext, a11y::scale_about(sc2, pv.x, s));
                if (sy0 > 0.5f) sy0 = std::max(0.0f, a11y::scale_about(sy0, pv.y, s));
                if (sy1 < float(hud->height) - 0.5f) sy1 = std::min(float(hud->height), a11y::scale_about(sy1, pv.y, s));
            }
            it.scissor[0] = (1.0f - fw) / 2 + fw * sc0 / float(hud->width);
            it.scissor[1] = (1.0f - fh) / 2 + fh * sy0 / float(hud->height);
            it.scissor[2] = (1.0f - fw) / 2 + fw * sc2 / float(hud->width);
            it.scissor[3] = (1.0f - fh) / 2 + fh * sy1 / float(hud->height);
            // column-major: x' = sx x - fw + 1 / out_w, y' = fh - sy y - 1 / out_h (clip space, y up): the GS samples a
            // pixel at its integer corner, a GPU at its centre, so the 2D moves by half an output pixel [L GS
            // rasterisation rules; checked against the GS frame in §17]. hud1-hud2 moved it by half a 2D pixel, the same
            // at 640 x 448 but 2 output pixels at 1800 lines: the 3D showed above the crash letterbox [C hud3, §17.12].
            const float hx = out_w > 0 ? 1.0f / float(out_w) : 0.5f * sx, hy = out_h > 0 ? 1.0f / float(out_h) : 0.5f * sy;
            float m[16] = {sx, 0, 0, 0, 0, -sy, 0, 0, 0, 0, 1, 0, -fw + hx, fh - hy, 0.5f, 1};
            std::memcpy(it.model, m, sizeof m);
            it.first_index = std::uint32_t(is.size());
            // accessibility: SCREEN FLASHES Reduced -- a full-screen bright fill's lift capped (a11y.h flash_cut)
            a11y::FlashCut flash;
            if (reduce_flashes && d.video < 0) {
                const a11y::FlashDraw fd = hud_flash_draw(*hud, d, textures);
                flash = a11y::flash_cut(fd);
                if (flash.factor < 1.0f && ++flash_cuts <= 20)
                    std::printf("[a11y] screen flash capped: a white fill over %.0f%% of the screen, light %.2f, x %.2f%s\n",
                                double(fd.coverage) * 100.0, double(fd.light), double(flash.factor),
                                flash_cuts == 20 ? " (the rest not logged)" : "");
            }
            // cam1: a draw inside a clip region narrower than the raster (Gt2d's SetClipRegion: the car pages' list of
            // thumbnail cars, scissor 32..159) moves with that region (its component's shift, as sc0 / sc2 above), not
            // with its object's edge anchor: race2's car list object also draws the bottom band, whose right curve made
            // it right-anchored, so the thumbnails left their clip region (cut at 640 x 448, gone at 1920 x 1080)
            // (clip_region and side: above, before the scissor)
            for (std::size_t vi = 0; vi < d.tri.size(); ++vi) {
                const HudVertex& hv = d.tri[vi];
                b3gfx::Vertex v{};
                v.pos[0] = hv.x;
                v.uv[0] = hv.u;
                v.uv[1] = hv.v;
                if (trax_fe) {
                    v.pos[0] = hv.x - ext;
                } else if (ext > 0 && lay_fe) {
                    hud_frontend_vertex(d, fe, vi, right, ext, it.repeat_u || it.repeat_v, v.pos[0], v.uv[0], v.uv[1]);
                } else if (ext > 0) {
                    // the edge pieces the game lays to the 4:3 box's edge (exactly at x 0 / 640) reach the output's edge:
                    // a textured triangle at one edge moves there whole (the crash band's end curve), one at both
                    // stretches (the band); an untextured draw touching an edge stretches: its vertices on an edge go
                    // to the output's edge, the others move with the object's anchor (the band's line, two gouraud
                    // quads, up to its moved end curve) or, without one, in proportion (the letterbox). The object's
                    // other pieces move with its anchor too; the rest (glyphs that merely cross an edge while scrolling
                    // included) moves with its component's anchor.
                    const std::size_t t0 = vi - vi % 3;
                    float lo = dlo, hi = dhi;
                    if (d.texture != 0) {
                        lo = hi = hv.x;
                        for (std::size_t k = t0; k < t0 + 3 && k < d.tri.size(); ++k) { lo = std::min(lo, d.tri[k].x); hi = std::max(hi, d.tri[k].x); }
                    }
                    const bool tl = std::fabs(lo) < 0.5f, tr = std::fabs(hi - right) < 0.5f;
                    float dx = side != 0 ? float(side) * ext : shift;
                    if (trax && di < overlay_vertexwise.size() && overlay_vertexwise[di])   // perf1: a moved row's selection bar
                        dx = std::fabs(hv.x) < 0.5f ? -ext : std::fabs(hv.x - right) < 0.5f ? ext : shift;
                    else if (d.texture == 0 && (tl || tr)) {
                        if (std::fabs(hv.x) < 0.5f) dx = -ext;
                        else if (std::fabs(hv.x - right) < 0.5f) dx = ext;
                        else if (side == 0) dx = (2.0f * hv.x / right - 1.0f) * ext;
                    }
                    else if (tl && tr) dx = hv.x < 0.5f * right ? -ext : ext;
                    else if (tl) dx = -ext;
                    else if (tr) dx = ext;
                    v.pos[0] = hv.x + dx;
                }
                v.pos[1] = hv.y;
                if (size_part) {   // textsize1 (HUD SIZE): about its component's anchor; a vertex on the 4:3 box's edge
                                   // (a panel's bar laid to it) stays at the window's edge, the bar grows from there
                    if (!(std::fabs(hv.x) < 0.5f || std::fabs(hv.x - right) < 0.5f))
                        v.pos[0] = a11y::scale_about(v.pos[0], size_part->pivot.x, size_part->scale);
                    v.pos[1] = a11y::scale_about(v.pos[1], size_part->pivot.y, size_part->scale);
                }
                if (!glyph_step) {   // mac5 (menu mouse): where the vertex lands, in output fractions (x' / y' below, without the half pixel)
                    const std::size_t per = d.tri.size() % 6 == 0 ? 6 : 3;
                    std::vector<ScreenBox>& pc = hud_draw_pieces[di];
                    if (pc.size() <= vi / per) pc.resize(vi / per + 1);
                    pc[vi / per].add(0.5f * (sx * v.pos[0] + 1.0f - fw), 0.5f * (sy * v.pos[1] + 1.0f - fh));
                }
                // an untextured vertex exactly on the raster's edge goes one 2D pixel past it (the scissor / the window
                // clips it): the GS covers the edge pixel, while a GPU pixel centre on the edge moved by half an output
                // pixel is a fill-rule tie (the crash letterbox's y 0 / 448 [C roadrage1 5730])
                if (d.texture == 0) {
                    if (std::fabs(hv.x) < 0.01f) v.pos[0] -= 1.0f;
                    else if (std::fabs(hv.x - right) < 0.01f) v.pos[0] += 1.0f;
                    if (std::fabs(hv.y) < 0.01f) v.pos[1] -= 1.0f;
                    else if (std::fabs(hv.y - float(hud->height)) < 0.01f) v.pos[1] += 1.0f;
                }
                // TFX DECAL = MODULATE by 0x80 (Cv = Ct, Av = At)
                const std::uint8_t* c = hv.rgba;
                const std::uint8_t k[4] = {0x80, 0x80, 0x80, 0x80};
                if (d.tfx_decal && d.texture != 0) c = k;
                std::uint8_t faded[4];
                if (fe.fade) {   // fe3: the ticker fades out at its clip region's ends instead of ending in a hard cut
                    const float lo = float(d.scissor[0]), hi = float(d.scissor[2] + 1);
                    const float f = std::clamp((hv.x - lo) / kFeTickerFade, 0.0f, 1.0f) * std::clamp((hi - hv.x) / kFeTickerFade, 0.0f, 1.0f);
                    std::memcpy(faded, c, 4);
                    faded[3] = std::uint8_t(std::lround(float(c[3]) * f));
                    c = faded;
                }
                v.rgba = std::uint32_t(c[0]) | std::uint32_t(c[1]) << 8 | std::uint32_t(c[2]) << 16 | std::uint32_t(c[3]) << 24;
                if (flash.factor < 1.0f) {   // accessibility: SCREEN FLASHES Reduced
                    const auto scaled = [&](std::uint32_t sh) { return std::uint32_t(std::lround(float(v.rgba >> sh & 0xFFu) * flash.factor)) << sh; };
                    v.rgba = flash.scale_alpha ? (v.rgba & 0x00FFFFFFu) | scaled(24) : scaled(0) | scaled(8) | scaled(16) | (v.rgba & 0xFF000000u);
                }
                if (filter_on && d.video < 0) v.rgba = a11y::remap_rgba(filter_m, v.rgba);   // accessibility: HUD COLOUR FILTER (not the movies)
                is.push_back(std::uint32_t(vs.size()));
                vs.push_back(v);
            }
            it.index_count = std::uint32_t(is.size()) - it.first_index;
            hud_items.push_back(it);
            hud_item_marker.push_back(d.marker);
            hud_item_draw.resize(hud_items.size(), -1);   // splitlayout1 (split_hud): the draw it came from
            hud_item_draw.back() = int(di);
            if (panels && movie_fill && int(di) == plan.backdrop) side_fill(dev, *hud, d, plan.box, it, textures, right, ext, vs, is);
            if (panels && movie_fill && int(di) != plan.backdrop)   // mac5: pictures at a 4:3 edge (the Crash Nav map's tiles)
                for (const HudFrontendPlan::EdgeImage& e : plan.edge_images)
                    if (e.draw == int(di)) side_fill(dev, *hud, d, e.box, it, textures, right, ext, vs, is);
        }
        place_globes(hud->draws.size());
        if (!vs.empty()) {
            dev.update_vertex_buffer(hud_vb, vs.data(), vs.size());
            dev.update_index_buffer(hud_ib, is.data(), is.size());
        }
        for (auto& it : hud_items) { it.vertices = hud_vb; it.indices = hud_ib; }
        hud_item_model.resize(hud_items.size());
        hud_item_count.resize(hud_items.size());
        for (std::size_t k = 0; k < hud_items.size(); ++k) {
            std::memcpy(hud_item_model[k].data(), hud_items[k].model, sizeof hud_items[k].model);
            hud_item_count[k] = hud_items[k].index_count;
        }
        hud_built = hud;
        hud_aspect = aspect;
        hud_built_wide = hud_wide;
        hud_built_sharp = hud_sharp;
        hud_built_h = out_h;
        hud_built_w = out_w;
        hud_built_frontend = frontend;
        hud_built_fe_layout = fe_layout;
        hud_built_movie_fill = movie_fill;
        hud_built_prompt = prompt_device;
        hud_built_prompt_keys = prompt_keys;
        hud_built_fonts = hud_fonts;
        hud_built_filter = hud_colour_filter;
        hud_built_flashes = reduce_flashes;
        hud_built_size = hud_size;
        hud_built_series = series_glyphs;
    }
    // hud4: the world-anchored markers, placed for this frame (the cached items keep their captured vertices; only
    // their 2D -> clip matrix moves: x' = x0 + r (x - x0) + dx about the marker's projected point x0)
    hud_marker_place.assign(hud->markers.size(), MarkerPlace{});
    if (!hud->markers.empty()) {
        for (std::size_t m = 0; m < hud->markers.size(); ++m) {
            const HudMarker& mk = hud->markers[m];
            MarkerPlace& pl = hud_marker_place[m];
            pl.x = mk.x;
            pl.y = mk.y;
            if (!hud_reproject || proj == nullptr || !mk.valid) continue;
            float w[3] = {mk.world[0], mk.world[1], mk.world[2]};
            if (mk.vehicle != 0 && proj->vehicles != nullptr)
                for (const Vehicle& v : *proj->vehicles)
                    if (v.id == mk.vehicle) { for (int k = 0; k < 3; ++k) w[k] = v.main.r[3][k] + mk.offset[k]; break; }
            float x, y, z;
            if (!hud_project(proj->view_proj, aspect, hud->width, hud->height, w, &x, &y, &z) || !(z > mk.znear)) { pl.shown = false; continue; }
            pl.dx = x - mk.x;
            pl.dy = y - mk.y;
            pl.x = x;
            pl.y = y;
            // the game's size rule (capped at 320) at the new depth, relative to the captured size
            if (mk.size > 0 && mk.z > 0) {
                const float uncapped = mk.size >= 320.0f ? mk.size : mk.size * hud_marker_size_rule(z) / hud_marker_size_rule(mk.z);
                pl.r = std::clamp(std::min(320.0f, uncapped) / mk.size, 0.25f, 4.0f);
                if (mk.size >= 320.0f) pl.r = 1.0f;
            }
        }
    }
    for (std::size_t k = 0; k < hud_items.size() && k < hud_item_marker.size(); ++k) {
        const int m = hud_item_marker[k];
        if (m < 0 || std::size_t(m) >= hud_marker_place.size()) continue;
        const MarkerPlace& pl = hud_marker_place[std::size_t(m)];
        const HudMarker& mk = hud->markers[std::size_t(m)];
        const auto& b = hud_item_model[k];
        b3gfx::DrawItem& it = hud_items[k];
        std::memcpy(it.model, b.data(), sizeof it.model);
        it.index_count = pl.shown ? hud_item_count[k] : 0;
        // column-major M; the vertex (x, y) becomes (r x + tx, r y + ty), tx = (1 - r) x0 + dx
        const float tx = (1.0f - pl.r) * mk.x + pl.dx, ty = (1.0f - pl.r) * mk.y + pl.dy;
        for (int i = 0; i < 4; ++i) {
            it.model[12 + i] = b[12 + i] + b[i] * tx + b[4 + i] * ty;
            it.model[i] = b[i] * pl.r;
            it.model[4 + i] = b[4 + i] * pl.r;
        }
    }
    out.insert(out.end(), hud_items.begin(), hud_items.end());
}

// fe3 (§30): the backdrop's side fill: beside the backdrop (a movie, else a full-width picture), out to the window's
// edges, a blurred copy of its own picture's outer band (kFillDepth units) mirrored about its edge, darkened towards the
// window's edge. Drawn right
// after the backdrop, so the game's later draws (the bg strips' fades, the grid) go over it as over the picture.
void View::side_fill(b3gfx::Device& dev, const HudFrame& hud, const HudDraw& d, const float box[4], const b3gfx::DrawItem& base,
                     const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures, float right, float ext,
                     std::vector<b3gfx::Vertex>& vs, std::vector<std::uint32_t>& is) {
    b3gfx::TextureId tex = b3gfx::kNone;
    if (d.video >= 0 && std::size_t(d.video) < hud.videos.size()) {
        const HudVideo& hv = hud.videos[std::size_t(d.video)];
        if (!hv.picture || !show_movies) return;
        auto& slot = fill_movie.emplace(hv.pic, std::make_pair(std::shared_ptr<const HudPicture>(), b3gfx::kNone)).first->second;
        if (slot.first != hv.picture || slot.second == b3gfx::kNone) {
            int bw = 0, bh = 0;
            const std::vector<std::uint8_t> px = fill_blur(hv.picture->rgba.data(), hv.picture->w, hv.picture->h, bw, bh);
            dev.update_texture(slot.second, bw, bh, px.data());
            slot.first = hv.picture;
        }
        tex = slot.second;
    } else if (hud_colour_filter != 0) {   // accessibility: HUD COLOUR FILTER -- the fill of the filtered picture
        auto it = fill_still_filtered.find({d.texture, hud_colour_filter});
        if (it == fill_still_filtered.end()) {
            b3gfx::TextureId id = b3gfx::kNone;
            const auto t = textures.find(d.texture);
            if (t != textures.end() && t->second->ok) {
                int bw = 0, bh = 0;
                std::vector<std::uint8_t> px = fill_blur(t->second->rgba.data(), t->second->img_w, t->second->img_h, bw, bh);
                a11y::remap_pixels(a11y::daltonize_matrix(a11y::colour_filter(hud_colour_filter)), px.data(), px.size() / 4);
                id = dev.create_texture({b3gfx::TextureLevel{bw, bh, px.data()}});
            }
            it = fill_still_filtered.emplace(std::make_pair(d.texture, hud_colour_filter), id).first;
        }
        tex = it->second;
    } else {
        auto it = fill_still.find(d.texture);
        if (it == fill_still.end()) {
            b3gfx::TextureId id = b3gfx::kNone;
            const auto t = textures.find(d.texture);
            if (t != textures.end() && t->second->ok) {
                int bw = 0, bh = 0;
                const std::vector<std::uint8_t> px = fill_blur(t->second->rgba.data(), t->second->img_w, t->second->img_h, bw, bh);
                id = dev.create_texture({b3gfx::TextureLevel{bw, bh, px.data()}});
            }
            it = fill_still.emplace(d.texture, id).first;
        }
        tex = it->second;
    }
    if (tex == b3gfx::kNone || d.tri.size() < 3) return;
    // the backdrop's affine map (x, y) -> (u, v), from its first triangle with an area (a movie's tiles share it)
    float m[6] = {0, 0, 0, 0, 0, 0};
    bool ok = false;
    for (std::size_t t0 = 0; t0 + 2 < d.tri.size() && !ok; t0 += 3) {
        const HudVertex &a = d.tri[t0], &b = d.tri[t0 + 1], &c = d.tri[t0 + 2];
        const float det = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (std::fabs(det) < 1e-3f) continue;
        const float ux = ((b.u - a.u) * (c.y - a.y) - (c.u - a.u) * (b.y - a.y)) / det, uy = ((c.u - a.u) * (b.x - a.x) - (b.u - a.u) * (c.x - a.x)) / det;
        const float vx = ((b.v - a.v) * (c.y - a.y) - (c.v - a.v) * (b.y - a.y)) / det, vy = ((c.v - a.v) * (b.x - a.x) - (b.v - a.v) * (c.x - a.x)) / det;
        m[0] = ux; m[1] = uy; m[2] = a.u - ux * a.x - uy * a.y;
        m[3] = vx; m[4] = vy; m[5] = a.v - vx * a.x - vy * a.y;
        ok = true;
    }
    if (!ok) return;
    const HudVertex& c0 = d.tri[0];
    auto colour = [&](float k) {
        const std::uint8_t* c = (d.tfx_decal && d.texture != 0) ? nullptr : c0.rgba;
        const float r = c ? float(c[0]) : 128.0f, g = c ? float(c[1]) : 128.0f, b = c ? float(c[2]) : 128.0f, a = c ? float(c[3]) : 128.0f;
        return std::uint32_t(std::lround(r * k)) | std::uint32_t(std::lround(g * k)) << 8 | std::uint32_t(std::lround(b * k)) << 16 | std::uint32_t(std::lround(a)) << 24;
    };
    const float y0 = box[1], y1 = box[3];
    for (int side = -1; side <= 1; side += 2) {
        const float edge = side < 0 ? box[0] : box[2];
        if (side < 0 ? edge > 0.5f : edge < right - 0.5f) continue;   // only beside an edge it reaches
        const float outer = side < 0 ? -ext - 2.0f : right + ext + 2.0f;
        const float xs[2] = {edge, outer}, ks[2] = {kFillInner, kFillOuter};
        b3gfx::DrawItem it = base;
        it.texture = tex;
        it.repeat_u = it.repeat_v = false;
        it.point_sample = false;
        it.sharp_alpha = false;
        it.scissor[0] = 0.0f;
        it.scissor[2] = 1.0f;
        it.first_index = std::uint32_t(is.size());
        const std::uint32_t v0i = std::uint32_t(vs.size());
        for (int yi = 0; yi < 2; ++yi)
            for (int xi = 0; xi < 2; ++xi) {
                // mirrored about the backdrop's edge, the whole side from its outer kFillDepth units only: an extension of
                // the edge's colours, not a reflection of what is inside (the intro movie's centred logo)
                // mac5: a still picture (the loading screens) mirrors as deep as the side itself, up to half the picture --
                // its outer 96 units stretched over an ultra-wide side read as streaks of static on a Mac
                const float depth = d.video >= 0 ? kFillDepth : std::max(kFillDepth, std::min(std::fabs(outer - edge), 0.5f * (box[2] - box[0])));
                const float k = std::min(1.0f, depth / std::max(1.0f, std::fabs(outer - edge)));
                const float x = xs[xi], y = yi ? y1 : y0, mx = edge - (x - edge) * k;
                b3gfx::Vertex v{};
                v.pos[0] = x;
                v.pos[1] = y;
                v.uv[0] = std::clamp(m[0] * mx + m[1] * y + m[2], 0.0f, 1.0f);
                v.uv[1] = std::clamp(m[3] * mx + m[4] * y + m[5], 0.0f, 1.0f);
                v.rgba = colour(ks[xi]);
                vs.push_back(v);
            }
        for (std::uint32_t k : {0U, 1U, 3U, 0U, 3U, 2U}) is.push_back(v0i + k);
        it.index_count = 6;
        hud_items.push_back(it);
        hud_item_marker.push_back(-1);
    }
}

// wide1: a host UI layer (§27; the PC settings page): its quads in the 640 x 448 picture mapped as hud_list maps the 2D (the 4:3 box,
// half an output pixel), drawn last with GS alpha 0x44 ((Cs - Cd) As + Cd); colours at the game's 2D intensity
// (vertex 64 x c, alpha 128 x a: Cv = Ct Cf >> 7 into the half-intensity frame the present pass doubles)
void View::host_ui_list(b3gfx::Device& dev, const std::shared_ptr<const HostUi>& ui, float aspect, std::vector<b3gfx::DrawItem>& out,
                        int out_h, int out_w) {
    if (!show_host_ui || ui == nullptr || ui->quads.empty() || !(aspect > 0)) return;
    if (host_built != ui || host_aspect != aspect || host_built_h != out_h || host_built_w != out_w || host_built_filter != hud_colour_filter) {
        host_items.clear();
        const a11y::ColourFilter colour_filter = a11y::colour_filter(hud_colour_filter);   // accessibility (a11y.h)
        const a11y::Matrix3 filter_m = a11y::daltonize_matrix(colour_filter);
        constexpr float W = 640.0f, H = 448.0f;
        const float fw = std::min(1.0f, (4.0f / 3.0f) / aspect), fh = std::min(1.0f, aspect / (4.0f / 3.0f));
        const float sx = 2.0f * fw / W, sy = 2.0f * fh / H;
        const float hx = out_w > 0 ? 1.0f / float(out_w) : 0.5f * sx, hy = out_h > 0 ? 1.0f / float(out_h) : 0.5f * sy;
        const float m[16] = {sx, 0, 0, 0, 0, -sy, 0, 0, 0, 0, 1, 0, -fw + hx, fh - hy, 0.5f, 1};
        const auto tex = [&](const std::string& p) {
            auto it = host_ui_tex.find(p);
            if (it == host_ui_tex.end()) {
                b3gfx::TextureId id = b3gfx::kNone;
                if (p.empty()) {
                    const std::uint8_t white[4] = {255, 255, 255, 255};
                    id = dev.create_texture({b3gfx::TextureLevel{1, 1, white}});
                } else if (p.rfind("hudtex:", 0) == 0) {   // mac5: a captured HUD texture by raster (the PC SETTINGS page's fonts)
                    const std::uint32_t raster = std::uint32_t(std::strtoul(p.c_str() + 7, nullptr, 16));
                    if (host_ui_hud_textures)
                        for (const auto& [key, t] : *host_ui_hud_textures)
                            if (t && t->ok && t->raster == raster && !t->rgba.empty()) {
                                id = dev.create_texture({b3gfx::TextureLevel{t->img_w, t->img_h, t->rgba.data()}});
                                break;
                            }
                    if (id == b3gfx::kNone) return id;   // not captured yet: try again next build (no cache entry)
                } else {
                    int w = 0, h = 0;
                    std::vector<std::uint8_t> px;
                    if (read_png_rgba(p, w, h, px)) id = dev.create_texture({b3gfx::TextureLevel{w, h, px.data()}});
                    else std::fprintf(stderr, "[b3replay] host UI: cannot read %s\n", p.c_str());
                }
                it = host_ui_tex.emplace(p, id).first;
            }
            return it->second;
        };
        std::vector<b3gfx::Vertex> vs;
        std::vector<std::uint32_t> is;
        // widescreen: the picture units the window shows (a cover quad's edge coordinates move out to them, and it is not clipped to the box)
        const HostUiExtent ext = host_ui_extent(aspect);
        const float wl = ext.left, wr = ext.right, wt = ext.top, wb = ext.bottom;
        const bool boxed = fw < 1.0f || fh < 1.0f;
        bool item_cover = false;
        for (const HostUiQuad& q : ui->quads) {
            const b3gfx::TextureId t = tex(ui->paths[q.path]);
            if (t == b3gfx::kNone) continue;
            const bool cover = boxed && q.cover != 0;
            if (host_items.empty() || host_items.back().texture != t || item_cover != cover) {
                item_cover = cover;
                b3gfx::DrawItem it;
                it.overlay = true;
                it.shading = b3gfx::Shading::kTrack;
                it.track_type = 0;
                it.textured = true;
                it.texture = t;
                it.gs_alpha = 0x44;
                it.overlay_blend = true;
                it.point_sample = false;
                it.scissor[0] = cover ? 0.0f : (1.0f - fw) / 2;
                it.scissor[1] = cover ? 0.0f : (1.0f - fh) / 2;
                it.scissor[2] = cover ? 1.0f : (1.0f + fw) / 2;
                it.scissor[3] = cover ? 1.0f : (1.0f + fh) / 2;
                std::memcpy(it.model, m, sizeof m);
                it.first_index = std::uint32_t(is.size());
                host_items.push_back(it);
            }
            const std::uint32_t c = q.rgba;
            const auto half = [](std::uint32_t v) { return (v * 64U + 127U) / 255U; };
            std::uint32_t rgba = half(c & 0xFFU) | half(c >> 8 & 0xFFU) << 8 | half(c >> 16 & 0xFFU) << 16 | ((c >> 24) * 128U + 127U) / 255U << 24;
            if (colour_filter != a11y::ColourFilter::kOff) rgba = a11y::remap_rgba(filter_m, rgba);   // HUD COLOUR FILTER
            const std::uint32_t base = std::uint32_t(vs.size());
            for (int k = 0; k < 4; ++k) {
                b3gfx::Vertex v{};
                v.pos[0] = q.x[k];
                v.pos[1] = q.y[k];
                if (cover && (q.cover & 1)) v.pos[0] = q.x[k] <= 0.001f ? wl : q.x[k] >= W - 0.001f ? wr : q.x[k];
                if (cover && (q.cover & 2)) v.pos[1] = q.y[k] <= 0.001f ? wt : q.y[k] >= H - 0.001f ? wb : q.y[k];
                v.uv[0] = q.u[k];
                v.uv[1] = q.v[k];
                v.rgba = rgba;
                vs.push_back(v);
            }
            for (std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) is.push_back(base + k);
            host_items.back().index_count = std::uint32_t(is.size()) - host_items.back().first_index;
        }
        if (!vs.empty()) {
            dev.update_vertex_buffer(host_vb, vs.data(), vs.size());
            dev.update_index_buffer(host_ib, is.data(), is.size());
        }
        for (auto& it : host_items) { it.vertices = host_vb; it.indices = host_ib; }
        host_built = ui;
        host_aspect = aspect;
        host_built_h = out_h;
        host_built_w = out_w;
        host_built_filter = hud_colour_filter;
    }
    out.insert(out.end(), host_items.begin(), host_items.end());
}

// ---- gsoff1 (§29): the 2-player split screen ----------------------------------------------------------------------

void View::split_frame(b3gfx::Device& dev, const Frame& f, const std::vector<ModelHead>& heads,
                       const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& hud_textures, bool game_cull, bool fog,
                       bool spec, int out_w, int out_h, float view_aspect, float hud_aspect,
                       std::vector<b3gfx::Device::ViewPass>& views, b3gfx::FrameParams& ofp, std::vector<b3gfx::DrawItem>& overlay) {
    views.assign(2, b3gfx::Device::ViewPass{});
    const float shown = 2.0f * hud_aspect;   // a half as displayed: the output's width over half its height
    const float saved_aspect = render_aspect;
    // splitlayout1 (splitlayout.h): the game's top / bottom takes the path below as before; another layout places the
    // views itself, projects each at its own shape and moves each player's 2D into its view
    const SplitLayout layout = split_layout_of(split_layout);
    const bool own_layout = layout != SplitLayout::kGame;
    SplitView sv[2];
    split_views(layout, hud_aspect > 0 ? hud_aspect : 4.0f / 3.0f, sv);
    float view_half_w[2] = {0, 0};
    for (int k = 0; k < 2; ++k) {
        Frame fk = f;
        if (k == 0) {   // split2: a fading-out copy removed within player 2's radius only is not in this view
            for (Vehicle& v : fk.vehicles)
                if (v.gone < 0.0f && v.gone2 >= 0.0f) v.since = fk.fx.now;   // (fade 0: not drawn, nor its shadow or lights)
        }
        if (k == 1) {
            fk.cam = f.cam2;
            fk.tick_cam = f.tick_cam2;
            // split2 (§29.6): RenderRaceCars(1) hides player 2's own car while its camera manager says so (the look-back
            // camera); RenderShadows(1) and the corona submits are view 2's
            for (Vehicle& v : fk.vehicles) {
                v.own_hidden = v.own_hidden2;
                v.shadow.cast = v.shadow.cast2;
                v.corona.submit = v.corona.submit2;
                v.corona.sort = v.corona.sort2;
                v.listings = v.listings2;
            }
            // view 2's post passes (viewport 2's params, player 2's blur inputs; split2); captures without them: none
            if (f.view2.post.valid) {
                fk.post = f.view2.post;
                fk.blur = f.view2.blur;
            } else {
                fk.post.choice = 0;
            }
            if (f.view2.track_valid) {   // split1 (§29.5): CB3World::Render(1) draws from streamed track 1
                const View2In& t2 = f.view2;
                std::memcpy(fk.slot_unit, t2.slot_unit, sizeof fk.slot_unit);
                std::memcpy(fk.slot_kind, t2.slot_kind, sizeof fk.slot_kind);
                std::memcpy(fk.vis, t2.vis, sizeof fk.vis);
                std::memcpy(fk.backdrops, t2.backdrops, sizeof fk.backdrops);
                fk.current_unit = t2.current_unit;
                fk.crash.barriers = t2.barriers;
                fk.crash.barrier_group = t2.barrier_group;
                for (Vehicle& v : fk.vehicles) {
                    v.cull = v.cull2;   // and CB3TrafficSystem::Draw(1)'s choice
                    // traffic-popin's fade in / out against player 2's own 275 m radius (split2: since2 / gone2); a
                    // fading-out copy removed within player 1's radius only is not in this view
                    const bool other = v.gone >= 0.0f && v.gone2 < 0.0f;
                    v.since = other ? fk.fx.now : v.since2;
                    v.gone = other ? -1.0f : v.gone2;
                }
            }
            swap_view2();
        }
        b3gfx::Device::ViewPass& vp = views[std::size_t(k)];
        if (own_layout) {   // splitlayout1: the view's rect and its own view window (Hor+ from the layout's vertical)
            std::memcpy(vp.rect, sv[k].rect, sizeof vp.rect);
            fk.cam.half_h = split_view_half_h(layout, fk.cam.half_w, fk.cam.half_h);
            fk.cam.half_w = fk.cam.half_h * sv[k].aspect;
            view_half_w[k] = fk.cam.half_w;
            b3view::frame_params(sky, fk.cam.m.r[3], fk.cam.m.r[2], fog, spec, vp.fp);
            game_view_proj(fk.cam, sv[k].aspect, vp.fp.view_proj);
            car_frame(vp.fp);
            post_frame(fk, sv[k].aspect, vp.fp);
            render_aspect = sv[k].aspect;
            // the game's visibility results are for its own top / bottom frustum, this view's is another: the
            // renderer's own culling (a window's default)
            draw_list(dev, fk, heads, false, vp.fp.eye, vp.items);
            if (k == 1) swap_view2();
            continue;
        }
        vp.rect[0] = 0.0f;
        vp.rect[1] = 0.5f * float(k);
        vp.rect[2] = 1.0f;
        vp.rect[3] = 0.5f * float(k + 1);
        b3view::frame_params(sky, fk.cam.m.r[3], fk.cam.m.r[2], fog, spec, vp.fp);
        game_view_proj(fk.cam, view_aspect > 0 ? view_aspect : fk.cam.half_w / fk.cam.half_h, vp.fp.view_proj);
        car_frame(vp.fp);
        post_frame(fk, view_aspect > 0 ? view_aspect : shown, vp.fp);
        render_aspect = view_aspect;
        draw_list(dev, fk, heads, game_cull, vp.fp.eye, vp.items);
        if (k == 1) swap_view2();
    }
    render_aspect = saved_aspect;
    ofp = views[0].fp;
    overlay.clear();
    // splitlayout1: the 2D laid out at 4:3 over the whole output, then each draw moved into its view; an overlay of the
    // front end's panels over both views (the pause menu, the event intro: perf1's hud_overlay_panels) is one screen's,
    // laid out over the whole window as the game's layout does
    if (own_layout && !(f.hud && hud_overlay_panels(*f.hud))) {
        hud_list(dev, f.hud, hud_textures, 4.0f / 3.0f, overlay, out_h, out_w, nullptr);
        if (f.hud) split_hud(*f.hud, f, views, view_half_w, out_w, out_h, overlay);
        return;
    }
    // markers at the game's own places (no hud4 reprojection: through a view of this frame they land up to ~3 px off the
    // game's projection although the camera is the marker's own viewport [C B3HUD_MARKER_TRACE, gsoff1mp2]; not settled)
    hud_list(dev, f.hud, hud_textures, hud_aspect, overlay, out_h, out_w, nullptr);
}

// splitlayout1: each item of a split frame's 2D (hud_list at 4:3 over the whole output: x_ndc = sx x - 1 + hx, y_ndc =
// 1 - hy - sy y, sx = 2 / width, sy = 2 / height) moved by its draw's placement (output pixels: x' = a x + b, y' = c y +
// d). The move is applied on the left of the item's own matrix, so whatever hud_list built into it is kept:
// x_clip' = alpha x_clip + beta w_clip, likewise y.
void View::split_hud(const HudFrame& hud, const Frame& f, const std::vector<b3gfx::Device::ViewPass>& views, const float half_w[2],
                     int out_w, int out_h, std::vector<b3gfx::DrawItem>& items) const {
    if (out_w <= 0 || out_h <= 0 || hud.width <= 0 || hud.height <= 0 || views.size() < 2) return;
    const float W = float(hud.width), H = float(hud.height), ow = float(out_w), oh = float(out_h);
    SplitView sv[2];
    for (int k = 0; k < 2; ++k) {
        std::memcpy(sv[k].rect, views[std::size_t(k)].rect, sizeof sv[k].rect);
        sv[k].aspect = (sv[k].rect[2] - sv[k].rect[0]) * ow / std::max(1.0f, (sv[k].rect[3] - sv[k].rect[1]) * oh);
    }
    const HudPlace place[2] = {split_hud_place(0, sv[0], out_w, out_h, hud.width, hud.height),
                               split_hud_place(1, sv[1], out_w, out_h, hud.width, hud.height)};
    const HudPlace whole = frame_hud_place(out_w, out_h, hud.width, hud.height);
    const auto apply = [&](b3gfx::DrawItem& it, const HudAffine& m, const float clip[4]) {
        split_hud_model(m, hud.width, hud.height, out_w, out_h, it.model);
        if (!split_hud_scissor(m, hud.width, hud.height, out_w, out_h, clip, it.scissor)) it.index_count = 0;
    };
    const std::size_t n = std::min(items.size(), hud_items.size());
    for (std::size_t k = 0; k < n; ++k) {
        b3gfx::DrawItem& it = items[k];
        const int di = k < hud_item_draw.size() ? hud_item_draw[k] : -1;
        const int mi = k < hud_item_marker.size() ? hud_item_marker[k] : -1;
        if (mi >= 0 && std::size_t(mi) < hud.markers.size()) {   // a rival marker: its point through its own view's camera
            const HudMarker& mk = hud.markers[std::size_t(mi)];
            const int v = mk.view == 1 ? 1 : 0;
            float w[3] = {mk.world[0], mk.world[1], mk.world[2]};
            if (mk.vehicle != 0)
                for (const Vehicle& veh : f.vehicles)
                    if (veh.id == mk.vehicle) { for (int i = 0; i < 3; ++i) w[i] = veh.main.r[3][i] + mk.offset[i]; break; }
            const float* vp = views[std::size_t(v)].fp.view_proj;
            float c[4];
            for (int i = 0; i < 4; ++i) c[i] = vp[i] * w[0] + vp[4 + i] * w[1] + vp[8 + i] * w[2] + vp[12 + i];
            if (!mk.valid || !(c[3] > std::max(1e-4f, mk.znear)) || !(half_w[v] > 0) || !(mk.k[2] > 0) || !(mk.z > 0)) {
                it.index_count = 0;
                continue;
            }
            const HudPlace& p = place[v];
            const float dw = p.dst[2] - p.dst[0], dh = p.dst[3] - p.dst[1];
            const float px = p.dst[0] + 0.5f * (c[0] / c[3] + 1.0f) * dw, py = p.dst[1] + 0.5f * (1.0f - c[1] / c[3]) * dh;
            // its size: the game's rule at this depth through this view's focal length, in output pixels per 2D unit of
            // the captured marker (the game's focal length: 0.5 sw / half_w = 0.5 sw x +0x50 2D units)
            const float focal = 0.5f * dw / half_w[v], game_focal = 0.5f * float(mk.sw > 0 ? mk.sw : hud.width) * mk.k[2];
            const float sigma = std::clamp(focal * hud_marker_size_rule(c[3]) / (game_focal * hud_marker_size_rule(mk.z)),
                                           0.25f * p.scale, 4.0f * p.scale);
            apply(it, HudAffine{sigma, px - sigma * mk.x, sigma, py - sigma * mk.y}, p.dst);
            continue;
        }
        if (di < 0 || std::size_t(di) >= hud.draws.size() || hud.draws[std::size_t(di)].tri.empty()) {
            apply(it, split_hud_affine(whole, 0.5f, 0.5f, false), whole.dst);
            continue;
        }
        const HudDraw& d = hud.draws[std::size_t(di)];
        float box[4] = {1e30f, 1e30f, -1e30f, -1e30f};
        for (const HudVertex& hv : d.tri) {
            box[0] = std::min(box[0], hv.x); box[1] = std::min(box[1], hv.y);
            box[2] = std::max(box[2], hv.x); box[3] = std::max(box[3], hv.y);
        }
        // its view: the band its box lies in; a draw over both bands (a menu over the race) is the frame's
        const float mid = 0.5f * H;
        const int v = box[1] < mid - 8.0f && box[3] > mid + 8.0f ? -1 : 0.5f * (box[1] + box[3]) >= mid ? 1 : 0;
        const HudPlace& p = v < 0 ? whole : place[v];
        float ax = 0.5f, ay = 0.5f;
        split_hud_anchor(p, box, &ax, &ay);
        if (d.object >= 0 && std::size_t(d.object) < hud.objects.size()) {   // a HUD component's: its EHUDAlign point
            const int ci = hud.objects[std::size_t(d.object)].component;
            if (ci >= 0 && std::size_t(ci) < hud.components.size() && hud.components[std::size_t(ci)].align >= 0 &&
                hud.components[std::size_t(ci)].align < 8) {
                ax = hud.components[std::size_t(ci)].fx;
                ay = hud.components[std::size_t(ci)].fy;
            }
        }
        // the edge pieces the game lays against the raster's sides go to the view's sides (both: stretched over it)
        const bool at_l = box[0] <= 0.5f, at_r = box[2] >= W - 0.5f;
        if (at_l != at_r) ax = at_l ? 0.0f : 1.0f;
        apply(it, split_hud_affine(p, ax, ay, at_l && at_r), p.dst);
    }
}

// ---- menu1 (§22): front-end frames ----------------------------------------------------------------------------------

void frontend_params(b3gfx::FrameParams& fp) {
    for (float& v : fp.view_proj) v = 0;
    fp.view_proj[0] = fp.view_proj[5] = fp.view_proj[10] = fp.view_proj[15] = 1;
    fp.clear[0] = fp.clear[1] = fp.clear[2] = 0;
    fp.clear[3] = 1;
    fp.present_scale = 255.0f / 128.0f;
}

namespace {

// fe2: CB3World::RenderFrontEnd's floor reflection [C matched 0x275EF0]: before its first CB3RaceCar::Draw it negates
// the car's up row (+0x10) and position y (+0x30) and sets lighting set A; before the second it negates them back and
// sets set B (the state the capture grabs at the next poll). The parts RenderVehicle places from the car's matrices
// (wheels from main, panels from the body = tilt x main: capture.cpp's composition) move with it: X' = X inv(main) main';
// detached parts (wheel state 3, panel state >= 3: already world) stay. Set A: +0x5C 0.625 and the 8 floats +0x60.
Vehicle floor_reflection(const Vehicle& v) {
    Vehicle r = v;
    Mat34 m = v.main;
    for (int k = 0; k < 3; ++k) m.r[1][k] = -m.r[1][k];
    m.r[3][1] = -m.r[3][1];
    const Mat34 d = mul(inverse_affine(v.main), m);
    r.main = m;
    r.body = mul(v.body, d);
    for (int w = 0; w < v.nwheel && w < 6; ++w) if (v.wheel_state[w] != 3) r.wheel[w] = mul(v.wheel[w], d);
    for (int b = 0; b < v.nbit && b < 7; ++b) if (v.bit_state[b] < 3) r.bit[b] = mul(v.bit[b], d);
    static const float kSetA[9] = {0.625f, -0.4956515133f, -0.1862764955f, -0.3000000119f, 0.2937499881f, 0.1703812927f,
                                   -0.1570656002f, 0.237499997f, 0.131249994f};
    std::memcpy(r.sh, kSetA, sizeof r.sh);
    r.sh_count = 9;
    return r;
}

// The car file's shadow inputs at the offsets RenderFrontEndShadow reads from the vehicle data (+0x0D wheel count,
// +0xE80 / +0xE90 box, +0xBB0 + 0x40 k wheel records) [C 0x303750 asm]: the vehicle data is the file image (match_car
// compares its header bytes; FixUp relocates pointers only) [L].
bool file_shadow(const std::vector<std::uint8_t>& b, Vehicle::Shadow& s) {
    auto f32 = [&](std::size_t o) { float v = 0; if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4); return v; };
    if (b.size() < 0xEA0) return false;
    const int n = std::int8_t(b[0x0D]);
    if (n < 3 || n > 6) return false;
    s.radius = f32(0x18);
    for (int k = 0; k < 3; ++k) { s.sup[k] = f32(0xE80 + 4 * std::size_t(k)); s.inf[k] = f32(0xE90 + 4 * std::size_t(k)); }
    s.front_z = f32(0xBB0 + 0x40 * 1 + 8);
    s.rear_z = f32(0xBB0 + 0x40 * std::size_t(n - 2) + 8);
    return true;
}

}  // namespace

View::Showroom* View::showroom(b3gfx::Device& dev, std::uint32_t size, int units) {
    if (size == 0 || units <= 0) return nullptr;
    if (room.tried && room.size == size && room.units == units) return room.route.empty() ? nullptr : &room;
    room = Showroom{};   // a different static track (never seen: the front end keeps US/C5_V1); the old GPU ids leak until clear()
    room.tried = true;
    room.size = size;
    room.units = units;
    const std::string r = find_route(game, size, units, {});
    if (r.empty()) { std::fprintf(stderr, "showroom: no TRACKS/*/*/STATIC.DAT with size %u and %d units\n", size, units); return nullptr; }
    std::vector<std::uint8_t> st, sm;
    b3asset::read_file(r + "/STATIC.DAT", st);
    b3asset::read_file(r + "/STREAMED.DAT", sm);
    if (st.size() < 0x60) return nullptr;
    b3asset::Model m;
    b3asset::GsMemory gs;
    b3asset::TrackOptions to;
    to.streamed = to.instances = to.props = false;   // RenderFrontEnd draws the backdrops alone (the units are not streamed)
    to.lines = true;   // lines1 (no route has line meshes outside its units today)
    std::string why;
    if (!b3asset::read_track({st.data(), st.size()}, {sm.data(), sm.size()}, to, gs, m, &why) ||
        !b3view::build_scene(dev, std::move(m), true, room.hi, &why)) {
        std::fprintf(stderr, "showroom %s: %s\n", r.c_str(), why.c_str());
        return nullptr;
    }
    // RenderAllBackdrops' ranges from the static data header [C asm 0x21D2E0]: 0 = [0, s8 +0x14), 1 = [s8 +0x14, s8 +0x15),
    // 2 = [s8 +0x15, s16 +0x1C)
    std::int16_t end2 = 0;
    std::memcpy(&end2, st.data() + 0x1C, 2);
    const int bound[4] = {0, std::int8_t(st[0x14]), std::int8_t(st[0x15]), end2};
    for (std::size_t i = 0; i < room.hi.model.draws.size(); ++i) {
        int k = 0;
        const std::string& p = room.hi.model.draws[i].part;
        if (p.rfind("backdrop+", 0) == 0 || std::sscanf(p.c_str(), "backdrop %d", &k) != 1) continue;
        for (int g = 0; g < 3; ++g) if (k >= bound[g] && k < bound[g + 1]) room.range[g].push_back(int(i));
    }
    room.sky.prelit = true;
    if (!b3view::add_enviro(dev, r, room.sky)) std::fprintf(stderr, "showroom %s: no ENVIRO.DAT (no sky)\n", r.c_str());
    std::string w;
    if (!b3view::load_car_globals(dev, game, room.globals, &w)) std::fprintf(stderr, "cars: %s (slots 1-4 untextured)\n", w.c_str());
    if (room.sky.has_enviro) {
        b3view::set_car_environment(dev, room.sky.enviro, room.globals);
        if (std::string sw; !room.sun.load(dev, room.sky.enviro, r, game, &sw) || !sw.empty())
            std::fprintf(stderr, "showroom %s: %s\n", r.c_str(), sw.empty() ? "no sun corona" : sw.c_str());
    }
    if (!room.fx.load(dev, game, &w)) std::fprintf(stderr, "showroom: %s (no shadow)\n", w.c_str());
    room.route = r;
    std::fprintf(stderr, "showroom %s: backdrop ranges [0, %d) [%d, %d) [%d, %d): %zu / %zu / %zu draws\n", r.c_str(), bound[1], bound[1],
                 bound[2], bound[2], bound[3], room.range[0].size(), room.range[1].size(), room.range[2].size());
    return &room;
}

void View::bake_room_env(b3gfx::Device& dev, Showroom& sr, const float centre[3]) {
    float dd = 0;
    for (int k = 0; k < 3; ++k) dd = std::max(dd, std::fabs(centre[k] - sr.env_centre[k]));
    if (dd < 0.01f && sr.env != b3gfx::kNone) return;
    std::memcpy(sr.env_centre, centre, sizeof sr.env_centre);
    // what RenderFrontEnd draws into its env-map cameras [C matched 0x275EF0]: RenderSkyToEnvironmentMap (the sky's
    // env components, here the main sky's draws [G]), RenderAllBackdrops ranges 0, 1, 2; no full-screen specular
    std::vector<b3gfx::DrawItem> items(sr.sky.items.begin(), sr.sky.items.end());
    for (int g = 0; g < 3; ++g) for (int k : sr.range[g]) items.push_back(sr.hi.items[std::size_t(k)]);
    constexpr int kFace = 128, kW = 256, kH = 128;
    const float dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const float ups[6][3] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
    float vp[6][16];
    std::vector<std::uint8_t> face[6];
    for (int f = 0; f < 6; ++f) {
        b3gfx::FrameParams fp;
        b3view::frame_params(sr.sky, centre, dirs[f], true, false, fp);
        fp.present_scale = 1.0f;   // read back in frame units, the GS env map's range (the lights up to 255)
        float v[16], p[16];
        b3gfx::mat4_look(centre, dirs[f], ups[f], v);
        // a little wider than 90 degrees so every direction lands inside some face with a texel to spare
        b3gfx::mat4_perspective_reverse_z(2.0f * std::atan(1.02f), 1.0f, 0.05f, p);
        b3gfx::mat4_mul(p, v, vp[f]);
        std::memcpy(fp.view_proj, vp[f], sizeof fp.view_proj);
        if (!dev.draw_offscreen(kFace, kFace, fp, items, face[f])) { std::fprintf(stderr, "showroom env: offscreen draw failed\n"); return; }
    }
    // the dome mapping's inverse (car.frag env_sky): u = azimuth turns, v above the horizon 0.765625 - 0.75 h, below
    // 0.765625 + 0.25 h, h = 2e - e^2, |d.y| / |d.xz| = h / (1 - e^2)
    std::vector<std::uint8_t> img(std::size_t(kW) * kH * 4, 255);
    for (int y = 0; y < kH; ++y) {
        const float v = (float(y) + 0.5f) / float(kH);
        const bool up = v < 0.765625f;
        const float h = std::clamp(up ? (0.765625f - v) / 0.75f : (v - 0.765625f) / 0.25f, 0.0f, 0.999f);
        const float e = 1.0f - std::sqrt(1.0f - h);
        const float t = h / std::max(1.0f - e * e, 1e-4f);
        const float hor = 1.0f / std::sqrt(1.0f + t * t), dy = (up ? t : -t) * hor;
        for (int x = 0; x < kW; ++x) {
            const float az = (float(x) + 0.5f) / float(kW) * 6.2831853f;
            const float d[3] = {std::cos(az) * hor, dy, std::sin(az) * hor};
            for (int f = 0; f < 6; ++f) {
                float c[4];
                for (int i = 0; i < 4; ++i) c[i] = vp[f][i] * (centre[0] + d[0]) + vp[f][4 + i] * (centre[1] + d[1]) + vp[f][8 + i] * (centre[2] + d[2]) + vp[f][12 + i];
                if (!(c[3] > 1e-6f)) continue;
                const float nx = c[0] / c[3], ny = c[1] / c[3];
                if (std::fabs(nx) > 1.0f || std::fabs(ny) > 1.0f) continue;
                const float fx = (nx * 0.5f + 0.5f) * kFace - 0.5f, fy = (0.5f - ny * 0.5f) * kFace - 0.5f;
                const int x0 = std::clamp(int(std::floor(fx)), 0, kFace - 2), y0 = std::clamp(int(std::floor(fy)), 0, kFace - 2);
                const float ax = std::clamp(fx - float(x0), 0.0f, 1.0f), ay = std::clamp(fy - float(y0), 0.0f, 1.0f);
                std::uint8_t* o = img.data() + 4 * (std::size_t(y) * kW + std::size_t(x));
                for (int ch = 0; ch < 3; ++ch) {
                    auto px = [&](int xx, int yy) { return float(face[f][4 * (std::size_t(yy) * kFace + std::size_t(xx)) + std::size_t(ch)]); };
                    const float val = (px(x0, y0) * (1 - ax) + px(x0 + 1, y0) * ax) * (1 - ay) + (px(x0, y0 + 1) * (1 - ax) + px(x0 + 1, y0 + 1) * ax) * ay;
                    o[ch] = std::uint8_t(std::clamp(int(val + 0.5f), 0, 255));
                }
                break;
            }
        }
    }
    dev.update_texture(sr.env, kW, kH, img.data());
    if (const char* dir = std::getenv("B3_ROOM_ENV_PNG")) b3asset::write_png_rgba(dir, kW, kH, img.data());
}

void View::frontend_frame(b3gfx::Device& dev, const Frame& f, const std::vector<ModelHead>& heads,
                          const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures, float aspect, int out_w, int out_h,
                          b3gfx::FrameParams& fp, std::vector<b3gfx::DrawItem>& out) {
    frontend_params(fp);
    frame_now = f.fx.now;   // traffic-popin (vehicle_draws' traffic_fade)
    fp.sun_sprites.clear();
    fp.blur = b3gfx::FrameParams::RadialBlur{};
    if (f.fe_car != 0 && f.cam.valid && show_frontend_car) {
        // CB3FrontEnd::Render draws the car (CB3World::RenderFrontEnd) before the menu pages' 2D [C asm 0x132FA4 per a
        // reading of it]: the race car path, the game camera (Hor+ at the output's aspect)
        Showroom* sr = show_room ? showroom(dev, f.route_size, f.route_units) : nullptr;
        if (!sr && !frontend_globals) {
            std::string w;
            if (!b3view::load_car_globals(dev, game, car_globals, &w)) std::fprintf(stderr, "cars: %s (slots 1-4 untextured)\n", w.c_str());
            frontend_globals = true;
        }
        const Vehicle* car = nullptr;
        for (const auto& v : f.vehicles) if (v.id == f.fe_car) car = &v;
        if (!sr) {   // menu1's frame: the car alone on black
            b3view::frame_params(sky, f.cam.m.r[3], f.cam.m.r[2], false, false, fp);
            frontend_params(fp);   // the game's frame model (half-intensity frame, doubled on present); frame_params left it for an unlit scene
            game_view_proj(f.cam, aspect, fp.view_proj);
            car_frame(fp);
            vehicle_draws(dev, f.cam, f.vehicles, f.vehicles, heads, false, fp.eye, car_globals, out);
        } else {
            // fe2 (§24): RenderFrontEnd's order [C matched 0x275EF0]: the environment's backdrop texture, sky, sun blob and
            // clouds; RenderAllBackdrops range 0; the car's floor reflection, the car; range 1, the full-screen specular,
            // range 2; RenderFrontEndShadow; the sun corona. b3gfx draws its phases (sky, opaque + cars, specular, blended,
            // effects) in list order within each, so range 1's blended draws land after the specular [G: unseen].
            b3view::CarGlobals globals = sr->globals;
            if (room_env && car && car->model >= 0 && std::size_t(car->model) < heads.size()) {
                // the env map around the car: BuildCustomCameraData(idx)'s centre, the car's box middle (+0xE80 / +0xE90)
                Vehicle::Shadow box;
                if (const int file = car_file(heads[std::size_t(car->model)]); file >= 0 && file_shadow(cars[std::size_t(file)].bytes, box)) {
                    float mid[3], c[3];
                    for (int k = 0; k < 3; ++k) mid[k] = 0.5f * (box.sup[k] + box.inf[k]);
                    for (int k = 0; k < 3; ++k) c[k] = mid[0] * car->main.r[0][k] + mid[1] * car->main.r[1][k] + mid[2] * car->main.r[2][k] + car->main.r[3][k];
                    bake_room_env(dev, *sr, c);
                }
                if (sr->env != b3gfx::kNone) {
                    globals.env_sky = sr->env;
                    globals.env_frame_units = true;
                    globals.sun_colour[3] = 0;   // the sun blob is in the baked map where the room lets it show (nowhere here)
                }
            }
            b3view::frame_params(sr->sky, f.cam.m.r[3], f.cam.m.r[2], true, true, fp);
            game_view_proj(f.cam, aspect, fp.view_proj);
            std::memcpy(fp.sun_colour, globals.sun_colour, sizeof fp.sun_colour);
            out.insert(out.end(), sr->sky.items.begin(), sr->sky.items.end());
            for (int k : sr->range[0]) out.push_back(sr->hi.items[std::size_t(k)]);
            if (room_reflection && car) {
                const std::vector<Vehicle> mirrored{floor_reflection(*car)};
                vehicle_draws(dev, f.cam, mirrored, mirrored, heads, false, fp.eye, globals, out);
            }
            vehicle_draws(dev, f.cam, f.vehicles, f.vehicles, heads, false, fp.eye, globals, out);
            for (int g = 1; g < 3; ++g) for (int k : sr->range[g]) out.push_back(sr->hi.items[std::size_t(k)]);
            if (room_shadow && car && car->model >= 0 && std::size_t(car->model) < heads.size()) {
                Vehicle::Shadow s;
                s.ess = car->main;
                if (const int file = car_file(heads[std::size_t(car->model)]); file >= 0 && file_shadow(cars[std::size_t(file)].bytes, s))
                    sr->fx.draw_frontend_shadow(dev, s, out);
            }
            if (show_sun && sr->sky.has_enviro) {   // RenderSunCorona (post_frame's sun part, the showroom's sun)
                const SunCam& c = f.sun;
                const float sa = c.valid && c.size[0] > 0 ? c.f200 * c.f20C * c.size[1] / c.size[0] : 0.0f;
                sr->sun.frame(fp, aspect, c.valid ? c.gm0 : 0, sa, f.sun_now);
                fp.sun_occlusion = sun_occlusion;
            }
        }
    }
    hud_list(dev, f.hud, textures, aspect, out, out_h, out_w, nullptr, fe_layout != 0);
}

}  // namespace b3replay

namespace b3replay {

// settings1 CAMERA: FIELD OF VIEW / FOV CORRECTION applied to our view of the game's camera (the guest stays retail).
// docs/specs/settings-page.md §4. The view window's tangents scale by fov_scale. With the correction, the eye moves
// along its view direction so that the chased car keeps its size on screen: something at distance d keeps its size when
// the eye moves forward by d (1 - 1 / fov_scale) (backwards for a narrower view). The chased car: the nearest race car
// whose rear (its centre's distance along the view less kRearFraction of its bounding radius) is in front of the eye by
// at least kMinGap, within a chase camera's reach (kChaseReach) and near the view axis. d is its centre's distance along
// the view less kSizeFraction of its radius: with it the race shots at FOV 60 / 120 keep the car's drawn width closest
// to the game's 90 (d to the rear moved too little, d to the centre too far). A forward move stops kClearance short of
// the car's rear. Views with no such car (in the car, on the bumper, trackside replay cameras far from it) do not move.
//
// 1.1 chase camera (chase_distance / chase_height, drawn only): with a chased car, its distance d (as above, after the
// FOV move) becomes d x chase_distance (the eye moved back along the view; a closer view stops kClearance short of the
// car's rear), the eye rises chase_height along the camera's up, and the camera turns about the axis between its old
// and new view directions so that the point it looked at (d ahead) stays at the centre. Without a chased car: unchanged.
void View::apply_fov(Camera& c, const std::vector<Vehicle>& vehicles) const {
    const bool chase = chase_distance != 1.0f || chase_height != 0.0f;
    if ((fov_scale == 1.0f && !chase) || !c.valid) return;
    if (fov_scale != 1.0f) {
        c.half_w *= fov_scale;
        c.half_h *= fov_scale;
    }
    const bool dolly = fov_scale != 1.0f && fov_correction && fov_scale > 0.0f;
    if (!dolly && !chase) return;
    constexpr float kChaseReach = 20.0f, kMinGap = 0.75f, kClearance = 1.0f, kRearFraction = 0.85f, kSizeFraction = 0.4f;
    constexpr float kAxisSlope = 0.3f, kAxisMin = 1.5f, kDefaultRadius = 2.5f;
    const float* eye = c.m.r[3];
    float at[3] = {c.m.r[2][0], c.m.r[2][1], c.m.r[2][2]};
    const float len = std::sqrt(at[0] * at[0] + at[1] * at[1] + at[2] * at[2]);
    if (!(len > 1e-6f)) return;
    for (float& a : at) a /= len;
    float best = -1.0f, best_rear = 0.0f;
    for (const Vehicle& v : vehicles) {
        if (v.kind != VehicleKind::kRaceCar || v.own_hidden) continue;
        const float rel[3] = {v.main.r[3][0] - eye[0], v.main.r[3][1] - eye[1], v.main.r[3][2] - eye[2]};
        const float along = rel[0] * at[0] + rel[1] * at[1] + rel[2] * at[2];
        if (!(along > 0.0f) || along > kChaseReach) continue;
        const float off[3] = {rel[0] - along * at[0], rel[1] - along * at[1], rel[2] - along * at[2]};
        const float axis = std::sqrt(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]);
        if (axis > std::max(kAxisMin, kAxisSlope * along)) continue;
        const float radius = v.radius > 0.0f ? v.radius : kDefaultRadius;
        const float rear = along - kRearFraction * radius;
        if (rear < kMinGap) continue;
        if (best < 0.0f || along - kSizeFraction * radius < best) {
            best = along - kSizeFraction * radius;
            best_rear = rear;
        }
    }
    if (best < 0.0f) return;
    float move = 0.0f;
    if (dolly) {
        move = best * (1.0f - 1.0f / fov_scale);
        if (move > 0.0f) move = std::min(move, std::max(0.0f, best_rear - kClearance));
        for (int k = 0; k < 3; ++k) c.m.r[3][k] += at[k] * move;
    }
    if (!chase) return;
    const float d = best - move;   // the chased car's distance now
    float aim[3];                  // the point the camera looks at, d ahead
    for (int k = 0; k < 3; ++k) aim[k] = c.m.r[3][k] + at[k] * d;
    float back = d * (chase_distance - 1.0f);
    if (back < 0.0f) back = std::max(back, -std::max(0.0f, best_rear - move - kClearance));
    float up[3] = {c.m.r[1][0], c.m.r[1][1], c.m.r[1][2]};
    const float ul = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (!(ul > 1e-6f)) return;
    for (float& u : up) u /= ul;
    for (int k = 0; k < 3; ++k) c.m.r[3][k] += -at[k] * back + up[k] * chase_height;
    float to[3] = {aim[0] - c.m.r[3][0], aim[1] - c.m.r[3][1], aim[2] - c.m.r[3][2]};
    const float tl = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    if (!(tl > 1e-6f)) return;
    for (float& v : to) v /= tl;
    // the turn from `at` to `to` (Rodrigues), applied to the camera's three axes
    float axis[3] = {at[1] * to[2] - at[2] * to[1], at[2] * to[0] - at[0] * to[2], at[0] * to[1] - at[1] * to[0]};
    const float s = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    const float cs = at[0] * to[0] + at[1] * to[1] + at[2] * to[2];
    if (!(s > 1e-6f)) return;
    for (float& v : axis) v /= s;
    for (int r = 0; r < 3; ++r) {
        float* v = c.m.r[r];
        const float kv[3] = {axis[1] * v[2] - axis[2] * v[1], axis[2] * v[0] - axis[0] * v[2], axis[0] * v[1] - axis[1] * v[0]};
        const float kd = axis[0] * v[0] + axis[1] * v[1] + axis[2] * v[2];
        for (int k = 0; k < 3; ++k) v[k] = v[k] * cs + kv[k] * s + axis[k] * kd * (1.0f - cs);
    }
}

}  // namespace b3replay
