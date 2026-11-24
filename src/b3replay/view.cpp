#include "view.h"
#include "nativeaudio.h"   // weather: the rain's sound
#include "photomode.h"     // rainpause: the rain off under the pause menu and in photo mode
#include "carmatch.h"   // taxihead: match_car
#include "thumb_release.h"   // paint3
#include "b3port/user_dir.h"   // customseries: the cars folders
#include "../../tools/native/extra_series.h"   // customseries: the folders read as the game reads them
#include "carlook.h"
#include "ghost.h"   // ghost1
#include "taillights.h"   // taillights

#include <fstream>
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
#include <tuple>

#include "b3asset/car.h"
#include "b3asset/enviro.h"
#include "b3asset/png.h"
#include "b3asset/discfs.h"
#include "b3asset/export.h"
#include "b3asset/hdpack.h"
#include "b3report/sysinfo.h"   // hdcache: the process's memory around the HD prepare (the preload line)
#include "b3asset/track.h"

namespace fs = std::filesystem;

namespace b3replay {

// night (1.3.0, owner): the players' own headlights a bit brighter than the other cars' (an arcade racer, not a sim)
constexpr float kPlayerBeamGain = 1.5f;

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
    // customseries (tools/native/extra_cars.h): the player's car mods and the imported prototype cars raced as extra
    // cars, from their folders, as the game's side reads them (extra_series.h extra_scan_all)
    for (const b3native::ExtraCar& c : extra_cars_now().cars) {   // (header-only until drawn: CarFile::full)
        CarFile cf{c.model, {}, true};
        std::ifstream f(b3io::u8path(cf.path), std::ios::binary);
        cf.bytes.resize(0x100);
        if (f.read(reinterpret_cast<char*>(cf.bytes.data()), 0x100)) out.push_back(std::move(cf));
    }
    return out;
}
std::vector<std::uint8_t>& CarFile::full() {
    if (partial) {
        partial = false;
        std::vector<std::uint8_t> b;
        if (b3asset::read_file(path, b) && b.size() > 0xE00) bytes = std::move(b);
        else {   // (Fable's review: empty, so every user skips it -- never the header taken for the whole file)
            bytes.clear();
            std::fprintf(stderr, "car files: %s cannot be read now: not drawn\n", path.c_str());
        }
    }
    return bytes;
}
// customseries: the two extra cars folders (B3_CUSTOM_CARS / B3_BETA_CARS, else <user data>/cars/custom and cars/beta)
b3native::ExtraScan extra_cars_now() {
    fs::path custom, beta;
    const fs::path u = b3io::user_dir();
    if (const char* e = std::getenv("B3_CUSTOM_CARS"); e && *e) custom = b3io::u8path(e);
    else if (!u.empty()) custom = u / "cars" / "custom";
    if (const char* e = std::getenv("B3_BETA_CARS"); e && *e) beta = b3io::u8path(e);
    else if (!u.empty()) beta = u / "cars" / "beta";
    return b3native::extra_scan_all(custom, beta);
}
int match_car(const std::vector<CarFile>& cars, const ModelHead& head, int* candidates) {
    // taxihead (carmatch.h): the files whose bytes 0x00..0x4C are the header's, then of those the ones whose later words
    // are too (relocated by one load offset); none of those: the first of the former (as before)
    int found = -1, n = 0, first = -1;
    for (std::size_t i = 0; i < cars.size(); ++i) {
        if (cars[i].bytes.size() < head.size() || std::memcmp(cars[i].bytes.data(), head.data(), 0x4C) != 0) continue;
        if (first < 0) first = int(i);
        if (!head_tail_fits(cars[i].bytes.data(), cars[i].bytes.size(), head.data(), head.size())) continue;
        if (found < 0) found = int(i);
        ++n;
    }
    if (found < 0 && first >= 0) {   // (a header no file's relocated words fit: the old rule, said once per header)
        found = first;
        n = 1;
        std::fprintf(stderr, "car files: a vehicle header whose relocated words fit none of the files sharing its first 0x4C bytes: "
                             "drawn as %s (the first of them)\n", cars[std::size_t(first)].path.c_str());
    }
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
    // payloads1: released pieces (world matrices) as the wheels, when both ticks hold the same pieces; on board they are
    // in the vehicle's space and follow the interpolated main matrix
    if (a.pay_released && b.pay_released && a.npay == b.npay && std::equal(a.pay_list, a.pay_list + a.npay, b.pay_list))
        for (int k = 0; k < a.npay; ++k) o.pay[k] = lerp(a.pay[k], b.pay[k], t);
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

// taillights: vehicle v's brake level at tick i + alpha, from its brake bit in ticks i - 4 .. i + 1 (a tick without it:
// its bit at i)
static float brake_level_at(const Timeline& tl, std::size_t i, const Vehicle& v, float alpha) {
    const bool own = (v.lit & kLightBrake) != 0;
    bool bits[kBrakeTicks + 1];
    for (int k = 0; k <= kBrakeTicks; ++k) {
        const long j = long(i) - (kBrakeTicks - 1) + k;
        bits[k] = own;
        if (j < long(tl.first()) || j > long(tl.last()) || std::size_t(j) == i) continue;
        for (const Vehicle& u : tl.at(std::size_t(j)).vehicles)
            if (u.id == v.id && u.data == v.data) { bits[k] = (u.lit & kLightBrake) != 0; break; }
    }
    return brake_level(bits, alpha);
}

void View::frame_lights(const Timeline& tl, std::size_t i, double frac, Frame& f) const {
    if (!(night_on(f) || rain_on(f)) || tail_level() < 0.0f) return;   // a dry day: the game's lights, nothing to do
    i = std::clamp(i, tl.first(), tl.last());
    const float alpha = float(std::clamp(frac, 0.0, 1.0));
    for (Vehicle& v : f.vehicles) v.brake = brake_level_at(tl, i, v, alpha);
}

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
    std::memcpy(f.env_unit, a.env_unit, sizeof f.env_unit);   // reflect-tunnel
    f.env_car = a.env_car;
    f.env_track = a.env_track;
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
    f.carousel = a.carousel;   // paint3
    f.fe_car = a.fe_car;   // menu1
    f.fe_viewer = a.fe_viewer;   // gamemods1 (the car viewer)
    f.race_time = a.race_time.race;   // todrace
    f.mirror = (a.race_time.game_options & 1u) != 0;   // gameopts (MIRROR MODE)
    f.fe_player = a.fe_player;   // padpaint
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
    {   // paint3: the thumbnails' pictures on their way finished first (their models' resources go now)
        std::vector<std::uint8_t> drop;
        for (auto& [k, sl] : hud_thumb_textures) if (sl.job.on()) car_picture_take(dev, sl.job, true, drop);
        for (auto& [f, fit] : thumb_fit) if (fit.job.on()) car_picture_take(dev, fit.job, true, drop);
        for (PictureJob& j : thumb_orphans) car_picture_take(dev, j, true, drop);
        thumb_orphans.clear();
        thumb_only_models.clear();
    }
    rays_job.reset();   // orbitcam2: its worker reads the scene cleared below
    day_fittings_reset();   // TUNNEL LIGHTS: the same
    dev.clear_resources();
    car_models.clear();
    for (auto& [file, pm] : paint_maps) pm.tex = b3gfx::kNone;   // carpaint: their textures went with clear_resources (padpaint: the maps stay)
    car_globals = b3view::CarGlobals{};
    hi = b3view::Scene{};
    lo = b3view::Scene{};
    sky = b3view::Scene{};
    hi_unit.clear(); hi_inst.clear(); lo_unit.clear(); backdrop.clear(); barrier.clear(); always.clear(); prop_items.clear(); item_prop.clear();
    effects.clear();
    coronas.clear();
    view2_state.effects.clear();   // split screen: player 2's (swap_view2) went with clear_resources too
    view2_state.coronas.clear();
    hud_textures.clear();
    hud_textures_filtered.clear();   // accessibility
    hud_thumb_textures.clear();      // paint2
    hud_thumb_free.clear();
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
    lo_boxes.clear();   // reflect-tunnel
    hi_local_boxes.clear();   // reflect2: with the scene (a new route of the same item count must not keep them)
    for (EnvKeep& k : env_keep) k = EnvKeep{};   // reflect2: the kept maps went with clear_resources
    movie_tex.clear();
    movie_up.clear();
    movie_placeholder = b3gfx::kNone;
    hud_vb = hud_ib = b3gfx::kNone;
    host_ui_tex.clear();
    host_img_tex = b3gfx::kNone;
    host_img_name.clear();
    for (HostUiBuild& b : host_build) b = HostUiBuild{};
    hud_built.reset();
    hud_items.clear();
    frontend_globals = false;
    room = Showroom{};
    route.clear();
    route_size = 0;
    route_units = 0;
    crash_scene = b3view::CrashScene{};
    sun = b3view::Sun{};   // its corona texture went with clear_resources: a route without ENVIRO.DAT draws no stale sun
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

// SUN SHADOWS (remaster-features §20): the route's open-road light, the level a sunlit receiver's vertex light has (the
// track is prelit, its shadows baked into the vertex colours): the area-weighted 85th percentile of the vertex light
// (luminance, GS bytes / 128) of the up-facing opaque track triangles (|n.y| > 0.85; world-space draws, not the
// instanced props). Below it a receiver is in a baked shadow; 0.75 when the route has none to measure.
static float road_light_level(const b3view::Scene& sc) {
    const auto& m = sc.model;
    constexpr int kBins = 512;
    constexpr float kTop = 2.0f;
    std::vector<double> hist(kBins, 0.0);
    double total = 0.0;
    auto luma = [](std::uint32_t c) {
        return (0.299f * float(c & 0xFF) + 0.587f * float((c >> 8) & 0xFF) + 0.114f * float((c >> 16) & 0xFF)) / 128.0f;
    };
    for (std::size_t k = 0; k < m.draws.size() && k + sc.item_base < sc.items.size(); ++k) {
        const b3asset::DrawRange& d = m.draws[k];
        const b3gfx::DrawItem& it = sc.items[k + sc.item_base];
        if (d.transform >= 0 || it.shading != b3gfx::Shading::kTrack || it.blend == b3gfx::Blend::kAlpha || it.post_fog ||
            it.track_type == 2 || it.track_type == 3)
            continue;
        for (std::uint32_t i = d.first_index; i + 2 < d.first_index + d.index_count && i + 2 < m.indices.size(); i += 3) {
            const std::uint32_t a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
            if (a >= m.vertices.size() || b >= m.vertices.size() || c >= m.vertices.size()) continue;
            const float* p0 = m.vertices[a].pos;
            const float* p1 = m.vertices[b].pos;
            const float* p2 = m.vertices[c].pos;
            const float u[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, v[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
            const float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (!(len > 1e-6f) || std::fabs(n[1]) < 0.85f * len) continue;
            const float l = (luma(m.vertices[a].rgba) + luma(m.vertices[b].rgba) + luma(m.vertices[c].rgba)) / 3.0f;
            hist[std::size_t(std::clamp(int(l / kTop * kBins), 0, kBins - 1))] += 0.5 * len;
            total += 0.5 * len;
        }
    }
    if (!(total > 0.0)) return 0.75f;
    double acc = 0.0;
    for (int b = 0; b < kBins; ++b) {
        acc += hist[std::size_t(b)];
        if (acc >= 0.85 * total) return std::clamp((float(b) + 0.5f) * kTop / kBins, 0.25f, 1.5f);
    }
    return 0.75f;
}

bool View::load_prepared(b3gfx::Device& dev, TrackPrep&& p, std::string* why) {
    rays_job.reset();   // orbitcam2: (a build of the route before reads `hi`, replaced below)
    // perf4: the route's textures and buffers go up in one batch (Device::upload_batch), submitted as it fills and at the end
    struct Batch { b3gfx::Device& d; Batch(b3gfx::Device& x) : d(x) { d.upload_batch(true); } ~Batch() { d.upload_batch(false); } } batch(dev);
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
    road_light = road_light_level(hi);   // SUN SHADOWS: the open road's vertex light on this route
    std::fprintf(stderr, "%s: open road vertex light %.3f (sun shadows' reference)\n", route.c_str(), double(road_light));
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
    // propshadow1 (PROP SHADOWS): the props and the units' instances (lamp posts, signs, gantries, barriers) cast a sun
    // shadow while the row is on (b3gfx's caster pass reads the mark only then)
    const auto mark = [&](int k) {   // (and its bounding radius round the origin, scaled by the model matrix's largest axis)
        if (k < 0 || std::size_t(k) >= hi.items.size()) return;
        b3gfx::DrawItem& it = hi.items[std::size_t(k)];
        it.sun_caster = true;
        const std::size_t d = std::size_t(k) - hi.item_base;
        if (std::size_t(k) < hi.item_base || d >= hi.model.draws.size()) return;
        const auto& r = hi.model.draws[d];
        float r2 = 0;
        for (std::uint32_t i = r.first_index; i < r.first_index + r.index_count && i < hi.model.indices.size(); ++i) {
            const std::uint32_t vi = hi.model.indices[i];
            if (vi >= hi.model.vertices.size()) continue;
            const float* p = hi.model.vertices[vi].pos;
            r2 = std::max(r2, p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        }
        const float* M = it.model;
        float s2 = 0;
        for (int c = 0; c < 3; ++c) s2 = std::max(s2, M[4 * c] * M[4 * c] + M[4 * c + 1] * M[4 * c + 1] + M[4 * c + 2] * M[4 * c + 2]);
        it.sun_radius = std::sqrt(r2 * s2);
    };
    for (const auto& [k, placement] : item_prop) { (void)placement; mark(k); }
    for (const auto& [u, ks] : hi_inst) { (void)u; for (int k : ks) mark(k); }
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
    else {
        effects.preload(dev);   // perf4: the effect and corona textures used before (this run, or saved by an earlier one)
        view2_state.effects.load_shared(dev, effects);   // split screen: player 2's view (its shadows, effect textures, coronas)
    }
    night_fixtures_load();
    if (night_at_load() || night_env_set()) dev.prepare_scene_depth();   // nightdepth: a night race: its pipelines now
    {   // orbitcam2 (trackrays.h): the walls the orbit camera stops at, on a worker. The items' placements and kinds are
        // copied here; the worker reads only the model's vertices and indices, which stay as they are until clear()
        struct Src { float M[16]; std::uint32_t first, count; };
        std::vector<Src> src;
        for (const auto* m : {&hi_unit, &hi_inst})
            for (const auto& [u, v] : *m) {
                if (skip_units.count(u)) continue;
                for (int k : v) {
                    if (k < 0 || std::size_t(k) >= hi.items.size() || std::size_t(k) < hi.item_base) continue;
                    const b3gfx::DrawItem& it = hi.items[std::size_t(k)];
                    const std::size_t d = std::size_t(k) - hi.item_base;
                    if (it.blend != b3gfx::Blend::kOpaque || d >= hi.model.draws.size()) continue;   // see-through: not a wall
                    Src e;
                    std::memcpy(e.M, it.model, sizeof e.M);   // the placement (column-major; identity for the units' meshes)
                    e.first = hi.model.draws[d].first_index;
                    e.count = hi.model.draws[d].index_count;
                    src.push_back(e);
                }
            }
        const b3asset::Model* model = &hi.model;
        rays_job.start([model, src = std::move(src)] {
            std::vector<TrackRays::Tri> out;
            for (const Src& e : src)
                for (std::uint32_t i = e.first; i + 2 < e.first + e.count && i + 2 < model->indices.size(); i += 3) {
                    TrackRays::Tri t;
                    bool ok = true;
                    for (int c = 0; c < 3 && ok; ++c) {
                        const std::uint32_t vi = model->indices[i + std::uint32_t(c)];
                        if (vi >= model->vertices.size()) { ok = false; break; }
                        const float* q = model->vertices[vi].pos;
                        for (int a = 0; a < 3; ++a) t.v[3 * c + a] = e.M[a] * q[0] + e.M[4 + a] * q[1] + e.M[8 + a] * q[2] + e.M[12 + a];
                    }
                    if (ok && TrackRays::keep(t.v)) out.push_back(t);
                }
            return out;
        });
    }
    return true;
}

void View::car_frame(b3gfx::FrameParams& fp) const {
    std::memcpy(fp.sun_colour, car_globals.sun_colour, sizeof fp.sun_colour);
    fp.car_look = carlook::current();   // reflect2: the enhanced look, live-tunable (carlookpanel.h)
}

// surface (docs/remaster-features.md §18; off by default): GRAPHICS > SURFACE DETAIL (relief 3, sheen 0.35) and AMBIENT
// OCCLUSION (0.8 at 1.2 m), or by environment, which then wins over both rows: B3_SURFACE=1 turns relief, sheen and
// ambient occlusion on at those strengths, B3_SURFACE=0 all off; B3_SURFACE_RELIEF / _SHEEN / _AO / _AO_RADIUS / _FADE /
// _SKY / _GRAZING set one each (0 = that piece off), B3_SURFACE_DEBUG=1 the false-colour view. Unset and the rows off:
// every field 0, nothing runs.
static bool surface_env_set() {
    for (const char* n : {"B3_SURFACE", "B3_SURFACE_RELIEF", "B3_SURFACE_SHEEN", "B3_SURFACE_AO", "B3_SURFACE_DEBUG"})
        if (const char* e = std::getenv(n); e && *e) return true;
    return false;
}
static b3gfx::FrameParams::Surface surface_from_env() {
    b3gfx::FrameParams::Surface s;
    auto num = [](const char* n, float& v) { if (const char* e = std::getenv(n); e && *e) v = float(std::atof(e)); };
    if (const char* e = std::getenv("B3_SURFACE"); e && *e && *e != '0') { s.relief = 3.0f; s.sheen = 0.35f; s.ao = 0.8f; }
    num("B3_SURFACE_RELIEF", s.relief);
    num("B3_SURFACE_SHEEN", s.sheen);
    num("B3_SURFACE_AO", s.ao);
    num("B3_SURFACE_AO_RADIUS", s.ao_radius);
    num("B3_SURFACE_FADE", s.fade);
    num("B3_SURFACE_SKY", s.sky);
    num("B3_SURFACE_GRAZING", s.grazing);
    if (const char* e = std::getenv("B3_SURFACE_DEBUG"); e && *e && *e != '0') s.debug = true;
    return s;
}

void moon_direction(const float to_sun[3], float out[3]) {
    const float h = std::sqrt(to_sun[0] * to_sun[0] + to_sun[2] * to_sun[2]);
    const float ax = h > 1e-4f ? to_sun[0] / h : 0.0f, az = h > 1e-4f ? to_sun[2] / h : 1.0f;
    const float lo = 10.0f * 3.14159265f / 180.0f, hi = 20.0f * 3.14159265f / 180.0f;
    const float el = std::clamp(std::atan2(to_sun[1], h), lo, hi);
    out[0] = std::cos(el) * ax;
    out[1] = std::sin(el);
    out[2] = std::cos(el) * az;
}

// nightlights (remaster-features §17.2): a lamp per car at its nose (the shadow box's front, `spot[7]` m above the
// ground the car stands on, along the car's own axes, so a car in the air keeps its beam), the beam along the car's
// forward axis dipped `spot[6]` degrees; the players' cars first, then the others by distance to the camera, within
// 150 m and only when the beam's hull (the lamp and the square around the far end of the cone) is not wholly outside
// one plane of the view's frustum. More than night.lights: the rest keep their coronas only.
void View::night_lamps(const Frame& f, b3gfx::FrameParams& fp) const {
    b3gfx::FrameParams::Night& n = fp.night;
    n.lamps = 0;
    n.spot = night.lights > 0;
    for (int k = 0; k < 3; ++k) n.lamp_colour[k] = night.lamp[k];
    const float d2r = 3.14159265f / 180.0f;
    n.spot_shape[0] = std::cos(std::clamp(night.spot[0], 1.0f, 80.0f) * d2r);
    n.spot_shape[1] = std::cos(std::clamp(night.spot[1], night.spot[0] + 0.5f, 85.0f) * d2r);
    n.spot_shape[2] = std::max(night.spot[2], 1.0f);
    n.spot_shape[3] = std::max(night.spot[3], 0.0f);
    n.spot_range[0] = std::max(night.spot[4], 1.0f);
    n.spot_range[1] = std::max(night.spot[5], n.spot_range[0] + 1.0f);
    n.spot_core[0] = std::cos(std::clamp(night.spot[8], 0.5f, 60.0f) * d2r);
    n.spot_core[1] = std::cos(std::clamp(night.spot[9], night.spot[8] + 0.5f, 70.0f) * d2r);
    n.spot_core[2] = std::max(night.spot[10], 0.0f);
    if (!n.spot) return;
    struct Cand { float pos[3], dir[3], gain, key; };
    std::vector<Cand> cand;
    const float* eye = f.cam.m.r[3];
    const float dip = night.spot[6] * d2r, height = night.spot[7];
    const float reach = n.spot_range[1], wide = reach * std::tan(std::clamp(night.spot[1], 1.0f, 85.0f) * d2r);
    const float* vp = fp.view_proj;
    auto clip = [&](const float p[3], float c[4]) {
        for (int r = 0; r < 4; ++r) c[r] = vp[r] * p[0] + vp[4 + r] * p[1] + vp[8 + r] * p[2] + vp[12 + r];
    };
    for (std::size_t i = 0; i < f.vehicles.size(); ++i) {
        const Vehicle& v = f.vehicles[i];
        if (v.kind == VehicleKind::kCrashed) continue;
        if (v.kind == VehicleKind::kTraffic && !(v.shadow.cast || v.corona.submit)) continue;
        const float gain = v.kind == VehicleKind::kTraffic ? traffic_fade(v, f.fx.now, eye) : 1.0f;
        if (!(gain > 0.01f)) continue;
        const Vehicle::Shadow& s = v.shadow;
        const float* rt = s.ess.r[0];
        const float* up = s.ess.r[1];
        const float* at = s.ess.r[2];
        const float front = s.sup[2] > 0.5f ? s.sup[2] : 2.0f;
        const float lift = height - (s.radius > 0.05f ? s.radius : 0.33f);   // the origin sits about a wheel radius up
        Cand c{};
        float dl = 0;
        for (int k = 0; k < 3; ++k) {
            c.pos[k] = s.ess.r[3][k] + at[k] * front + up[k] * lift;
            c.dir[k] = at[k] * std::cos(dip) - up[k] * std::sin(dip);
            dl += c.dir[k] * c.dir[k];
        }
        if (!(dl > 1e-6f)) continue;
        dl = std::sqrt(dl);
        for (float& x : c.dir) x /= dl;
        const float de = std::sqrt((c.pos[0] - eye[0]) * (c.pos[0] - eye[0]) + (c.pos[1] - eye[1]) * (c.pos[1] - eye[1]) +
                                   (c.pos[2] - eye[2]) * (c.pos[2] - eye[2]));
        if (!(de < 150.0f)) continue;
        // the hull: the lamp and the four corners of the square around the cone's far end
        float pts[5][3];
        for (int k = 0; k < 3; ++k) pts[0][k] = c.pos[k];
        for (int q = 0; q < 4; ++q) {
            const float sx = (q & 1) ? 1.0f : -1.0f, sy = (q & 2) ? 1.0f : -1.0f;
            for (int k = 0; k < 3; ++k) pts[1 + q][k] = c.pos[k] + c.dir[k] * reach + (rt[k] * sx + up[k] * sy) * wide;
        }
        int out_mask = 0x3F;
        for (const auto& p : pts) {
            float cl[4];
            clip(p, cl);
            int m = 0;
            if (cl[0] < -cl[3]) m |= 1;
            if (cl[0] > cl[3]) m |= 2;
            if (cl[1] < -cl[3]) m |= 4;
            if (cl[1] > cl[3]) m |= 8;
            if (cl[2] > cl[3]) m |= 16;   // reverse-Z: nearer than the near plane
            if (cl[3] <= 0) m |= 32;      // behind the eye
            out_mask &= m;
        }
        if (out_mask) continue;
        c.gain = gain * (v.player >= 0 ? kPlayerBeamGain : 1.0f);   // the players' own cars a little brighter (an arcade racer)
        c.key = (v.player >= 0 ? 0.0f : 1000.0f) + de;
        cand.push_back(c);
    }
    std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.key < b.key; });
    const int cap = std::min({night.lights, int(b3gfx::FrameParams::Night::kMaxLamps), int(cand.size())});
    for (int k = 0; k < cap; ++k) {
        b3gfx::FrameParams::Night::Lamp& l = n.lamp[k];
        std::memcpy(l.pos, cand[std::size_t(k)].pos, sizeof l.pos);
        std::memcpy(l.dir, cand[std::size_t(k)].dir, sizeof l.dir);
        l.gain = cand[std::size_t(k)].gain;
    }
    n.lamps = cap;
}

static std::string route_region(const std::string& route) {
    std::string r = route;
    while (!r.empty() && (r.back() == '/' || r.back() == '\\')) r.pop_back();
    const std::size_t a = r.find_last_of("/\\");
    if (a == std::string::npos) return {};
    const std::string parent = r.substr(0, a);
    const std::size_t b = parent.find_last_of("/\\");
    return b == std::string::npos ? parent : parent.substr(b + 1);
}

// street lights (§17.2): the lamp textures per region, from $B3_NIGHT_LAMPSCAN's list, a census of every route's
// textures (nightfix: the tall, thin x / z groups of each texture) and checked by eye in day views of each; a tint per
// entry: 0 sodium orange (streets, motorways, the cobra-head posts), 1 warm white (town and park lamps, gas lamps,
// docks). Two kinds:
//   kPost  the whole lamp post (pole, arm and head) in one texture, or a head with its own texture: the lamp is at the
//          end of the arm (the group's point furthest from its pole among those within 1.5 m of its top)
//   kHead  a shared post atlas (bk_post, CTY_Post, VYD_Post, CST_trafPost: lamp posts and sign posts alike, the cobra
//          head in the atlas' lower left corner; HK_post: the lens strip): only triangles whose UV centre lies in
//          `uv` (u0, v0, u1, v1, wrapped to 0..1) are lamps, the lamp at the head group's centre, under its lowest point
// Every listed triangle (in or out of the rect) also marks the ground: the lowest point within 9 m of a lamp is its
// foot, so the pool can scale with the lamp's height (a 20 m motorway lamp lights a wider pool than a 6 m gas lamp).
// Left out: road and pavement textures named after lights (Chgo_Rd*Light, WF_paveGASLAMP), shop fronts
// (WF_gaslampSHOP*), lit windows (HK_ResLite), tunnel lamps (the tunnel fade lights them), traffic light posts
// (GL_GrnLpst_03, GL_tlightPost, WF_tlightPost), utility and telegraph poles (ATB_UtilPole, GL_telepole, KS_telepole),
// a lighthouse, grass named "light".
struct NightLampName {
    const char* name;
    int tint;
    enum Kind { kPost, kHead } kind = kPost;
    float uv[4] = {0, 0, 1, 1};
};
static const std::vector<NightLampName>& night_lamp_names(const std::string& region) {
    using K = NightLampName;
    static const float kCobra[4] = {0.0f, 0.72f, 0.4f, 1.0f};   // the post atlas' cobra head (bk_post, CTY_Post, VYD_Post, CST_trafPost)
    static const float kHkLens[4] = {0.0f, 0.45f, 0.13f, 0.9f};  // HK_post: the lens strip
    auto head = [](const char* n, int tint, const float* r) { K k{n, tint, K::kHead}; std::memcpy(k.uv, r, sizeof k.uv); return k; };
    // round 3b: the ceiling light fittings of tunnels, covered roads and underpasses (tint 2, cool white), the lit part
    static const float kWhole[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    static const float kHkPanel[4] = {0.25f, 0.68f, 0.75f, 0.93f};   // HK_Tunnel01: the lit panel under the grille
    static const float kTopStrip[4] = {0.0f, 0.0f, 1.0f, 0.12f};     // WF_illumnated1: the strip light (the rest a sign)
    static const float kDStrip[4] = {0.0f, 0.0f, 1.0f, 0.2f};        // ATB_Tunnel_DLight: the strip light
    static const std::map<std::string, std::vector<NightLampName>> names = {
        {"AS", {{"HK_Spotlight", 1}, {"HK_DockLite", 1}, head("HK_post", 0, kHkLens), head("bk_post", 0, kCobra),
                {"bk_riverlamps", 1}, {"KS_lamppost", 0, K::kPost, {0.25f, 0.0f, 0.5f, 1.0f}},
                head("HK_INS_LightRig", 2, kWhole), head("HK_Tunnel01", 2, kHkPanel)}},
        {"EU", {{"light", 1}, {"walllamp", 1}, {"lamppost", 1}, {"riverside_lamppost", 1}, {"ATB_Lamp", 0}, {"ATB_LampostG", 0},
                {"ATB_Lamp_Toll", 0}, {"R_Triplelampost", 0}, {"pole", 0}, {"VYD_FancyLampost", 1}, head("VYD_Post", 0, kCobra),
                head("ATB_Tunnel_Light", 2, kWhole), head("ATB_Tunnel_DLight", 2, kDStrip)}},
        {"US", {{"Chgo_GrnLpst_03", 0}, {"Chgo_GrnLpst_New", 0}, {"Chgo_OrnateLamp", 1}, {"GL_streetlight2", 0},
                {"WF_OrnateLamp1", 1}, {"WF_LAMP_Post", 0},
                // (not WF_gaslamp1-5 / 2b / OLD1, WF_Glamp1 / 1b: despite the names, building facades of the Gaslamp
                // quarter -- their windows are '%' fixtures)
                {"WF_Cparklight2", 0}, {"WF_trampost", 1}, head("CTY_Post", 0, kCobra),
                head("CST_trafPost", 0, kCobra), head("Chgo_TunLght", 2, kWhole), head("WF_illumnated1", 2, kTopStrip)}},
    };
    static const std::vector<NightLampName> none;
    const auto it = names.find(region);
    return it == names.end() ? none : it->second;
}

// The lamps: the triangles of a listed texture (instanced ones through their placement; kHead: those in its UV rect),
// grouped by 1.5 m cells over x / z (neighbouring cells joined); the lamp per group (above), its foot (the lowest
// listed point within 9 m); groups whose lamps lie within 1.5 m of each other merge (a head and its post in two
// textures: the higher kept; 3.5 m before nightfix, which merged the arm tips of posts facing each other over a street)
void View::night_street_load() const {
    night_streets_loaded = true;
    night_board_load();
    night_glow_load();
    night_lamps_build(false, night_streets);
}

// The lamps of the region's table into `night_streets_out` (fittings_only: the tunnel and underpass fittings, tint 2:
// TUNNEL LIGHTS by day, §35.4)
void View::night_lamps_build(bool fittings_only, std::vector<NightStreet>& night_streets_out) const {
    std::vector<NightStreet>& night_streets = night_streets_out;
    night_streets.clear();
    std::vector<NightLampName> list = night_lamp_names(route_region(route));
    if (fittings_only) list.erase(std::remove_if(list.begin(), list.end(), [](const NightLampName& l) { return l.tint != 2; }), list.end());
    if (list.empty()) return;
    const auto t0 = std::chrono::steady_clock::now();
    const b3asset::Model& m = hi.model;
    constexpr float kCell = 1.5f;
    struct Cell { float lo[3], hi[3], top[3]; int tint; int kind; bool used; };
    using Key = std::pair<int, int>;
    std::map<Key, Cell> cells;
    std::map<Key, float> ground;   // the lowest listed point per cell (lamp and post triangles alike)
    std::vector<std::array<float, 3>> pts;   // (for the arm's tip: every lamp vertex, by cell)
    std::map<Key, std::vector<std::uint32_t>> cell_pts;
    for (const b3asset::DrawRange& d : m.draws) {
        if (d.texture < 0 || std::size_t(d.texture) >= m.textures.size()) continue;
        const std::string& name = m.textures[std::size_t(d.texture)].name;
        const NightLampName* l = nullptr;
        for (const NightLampName& e : list) if (name == e.name) l = &e;
        if (!l) continue;
        const float* T = d.transform >= 0 && std::size_t(d.transform) < m.transforms.size() ? m.transforms[std::size_t(d.transform)].data() : nullptr;
        for (std::uint32_t i = d.first_index; i + 2 < d.first_index + d.index_count && i + 2 < m.indices.size(); i += 3) {
            float w[3][3], u = 0, v = 0;
            for (int c = 0; c < 3; ++c) {
                const b3asset::Vertex& vx = m.vertices[m.indices[i + std::uint32_t(c)]];
                const float* p = vx.pos;
                for (int k = 0; k < 3; ++k) w[c][k] = T ? T[k] * p[0] + T[4 + k] * p[1] + T[8 + k] * p[2] + T[12 + k] : p[k];
                u += vx.uv[0] / 3.0f;
                v += vx.uv[1] / 3.0f;
            }
            u -= std::floor(u);
            v -= std::floor(v);
            const bool lamp = u >= l->uv[0] && u <= l->uv[2] && v >= l->uv[1] && v <= l->uv[3];
            for (int c = 0; c < 3; ++c) {
                const Key key{int(std::floor(w[c][0] / kCell)), int(std::floor(w[c][2] / kCell))};
                auto g = ground.find(key);
                if (g == ground.end()) ground[key] = w[c][1];
                else g->second = std::min(g->second, w[c][1]);
                if (!lamp) continue;
                auto it = cells.find(key);
                if (it == cells.end()) {
                    Cell nc{};
                    for (int k = 0; k < 3; ++k) nc.lo[k] = nc.hi[k] = nc.top[k] = w[c][k];
                    nc.tint = l->tint;
                    nc.kind = l->kind;
                    it = cells.emplace(key, nc).first;
                } else {
                    Cell& cc = it->second;
                    for (int k = 0; k < 3; ++k) { cc.lo[k] = std::min(cc.lo[k], w[c][k]); cc.hi[k] = std::max(cc.hi[k], w[c][k]); }
                    if (w[c][1] > cc.top[1]) { std::memcpy(cc.top, w[c], sizeof cc.top); cc.tint = l->tint; cc.kind = l->kind; }
                }
                cell_pts[key].push_back(std::uint32_t(pts.size()));
                pts.push_back({w[c][0], w[c][1], w[c][2]});
            }
        }
    }
    static const float kTint[3][3] = {{1.0f, 0.62f, 0.30f}, {1.0f, 0.86f, 0.66f}, {0.85f, 0.93f, 1.0f}};
    for (auto& [key, c] : cells) {
        if (c.used) continue;
        // join neighbouring cells (flood fill): the group's bounds, its highest point, its cells
        std::vector<Key> stack{key}, group;
        c.used = true;
        Cell g = c;
        while (!stack.empty()) {
            const Key k = stack.back();
            stack.pop_back();
            group.push_back(k);
            const Cell& q = cells[k];
            for (int j = 0; j < 3; ++j) { g.lo[j] = std::min(g.lo[j], q.lo[j]); g.hi[j] = std::max(g.hi[j], q.hi[j]); }
            if (q.top[1] > g.top[1]) { std::memcpy(g.top, q.top, sizeof g.top); g.tint = q.tint; g.kind = q.kind; }
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    auto it = cells.find({k.first + dx, k.second + dz});
                    if (it == cells.end() || it->second.used) continue;
                    it->second.used = true;
                    stack.push_back(it->first);
                }
        }
        float pos[3];
        float base[2] = {0, 0};
        bool arm = false;
        if (g.kind == NightLampName::kHead) {   // the head: its centre over x / z, just under its lowest point (the lens)
            pos[0] = 0.5f * (g.lo[0] + g.hi[0]);
            pos[1] = g.lo[1] - 0.1f;
            pos[2] = 0.5f * (g.lo[2] + g.hi[2]);
            base[0] = pos[0];
            base[1] = pos[2];
        } else {
            // the post (owner, round 3e: the light at the lens): its base is the x / z of its lowest point (the pole's
            // foot; for an arm in a group of its own, where it leaves the pole); its tip the point of its upper half
            // furthest out from the base. Out 1 m or more (an arm): the lens is the bottom of the head there (the lowest
            // point within 0.9 m of the tip, x / z its mean), 0.1 m under it. Else (a straight post, the head on top):
            // its highest point, 0.3 m down.
            float low = 1e30f;
            for (const Key& k : group)
                for (std::uint32_t pi : cell_pts[k])
                    if (pts[pi][1] < low) { low = pts[pi][1]; base[0] = pts[pi][0]; base[1] = pts[pi][2]; }
            const float mid = 0.5f * (g.lo[1] + g.hi[1]);
            float best = -1.0f, tip[3] = {g.top[0], g.top[1], g.top[2]};
            for (const Key& k : group)
                for (std::uint32_t pi : cell_pts[k]) {
                    const auto& p = pts[pi];
                    if (p[1] < mid) continue;
                    const float h = (p[0] - base[0]) * (p[0] - base[0]) + (p[2] - base[1]) * (p[2] - base[1]);
                    if (h > best + 0.01f) { best = h; tip[0] = p[0]; tip[1] = p[1]; tip[2] = p[2]; }
                }
            if (best >= 1.0f) {
                arm = true;
                // the lens's bottom: the lowest point within 1.2 m of the tip; its centre: the mean x / z of the head's
                // underside (the points within 2 m of the tip and 0.4 m of that bottom)
                float lens = 1e30f, sx = 0, sz = 0;
                int nn = 0;
                for (const Key& k : group)
                    for (std::uint32_t pi : cell_pts[k]) {
                        const auto& p = pts[pi];
                        if ((p[0] - tip[0]) * (p[0] - tip[0]) + (p[2] - tip[2]) * (p[2] - tip[2]) <= 1.44f && p[1] >= mid) lens = std::min(lens, p[1]);
                    }
                for (const Key& k : group)
                    for (std::uint32_t pi : cell_pts[k]) {
                        const auto& p = pts[pi];
                        if ((p[0] - tip[0]) * (p[0] - tip[0]) + (p[2] - tip[2]) * (p[2] - tip[2]) > 4.0f || p[1] > lens + 0.4f || p[1] < lens - 0.01f) continue;
                        sx += p[0]; sz += p[2]; ++nn;
                    }
                pos[0] = nn ? sx / float(nn) : tip[0];
                pos[1] = (nn ? lens : tip[1]) - 0.1f;
                pos[2] = nn ? sz / float(nn) : tip[2];
            } else {
                std::memcpy(pos, g.top, sizeof pos);
                pos[1] -= 0.3f;
            }
        }
        // the foot: the lowest listed point within 9 m (a cobra head hangs 4-7 m out from its pole); a lamp with none
        // 2 m or more below it (a head alone, a wall lamp) is taken as 6 m up (round 3: a small pool, gain x 0.65, so the
        // wall right under an arcade's lamps does not light up as a panel)
        float foot = pos[1] - 6.0f, lowest = 1e30f;
        const int cx = int(std::floor(pos[0] / kCell)), cz = int(std::floor(pos[2] / kCell));
        for (int dx = -6; dx <= 6; ++dx)
            for (int dz = -6; dz <= 6; ++dz) {
                auto it = ground.find({cx + dx, cz + dz});
                if (it != ground.end()) lowest = std::min(lowest, it->second);
            }
        if (lowest < pos[1] - 2.0f) foot = lowest;
        NightStreet l{};
        std::memcpy(l.pos, pos, sizeof l.pos);
        std::memcpy(l.colour, kTint[std::clamp(g.tint, 0, 2)], sizeof l.colour);
        l.height = pos[1] - foot;
        l.extent = g.hi[1] - g.lo[1];
        l.base[0] = base[0];
        l.base[1] = base[1];
        l.arm = arm;
        l.kind = std::uint8_t(g.kind);
        bool merged = false;
        for (NightStreet& o : night_streets) {
            const float dx = o.pos[0] - l.pos[0], dy = o.pos[1] - l.pos[1], dz = o.pos[2] - l.pos[2];
            if (dx * dx + dy * dy + dz * dz < 1.5f * 1.5f) {   // (nightfix: 3.5 m merged facing posts' arm tips)
                if (l.pos[1] > o.pos[1]) o = l;
                merged = true;
                break;
            }
        }
        if (!merged) night_streets.push_back(l);
    }
    // (owner, round 3c) an arm lamp whose pole and arm fall in two groups (the arm's cells not next to the pole's) gave
    // two lamps: the pole's at its top (the bend) and the arm's at the head. A tall group (a pole: 3 m or more high)
    // with a short one (the arm or the head) within 12 m across (motorway arms reach 7-9 m) and 2.5 m in height is the same
    // lamp: the short one stays (lamps stand 25-40 m apart)
    {
        std::vector<bool> drop(night_streets.size(), false);
        // (round 3e) a straight-post lamp (a pole top, a bend) where an arm lamp's group starts (its base within 2.5 m
        // across, 4 m in height) is that arm's pole: one light per fitting, at the arm's lens
        for (std::size_t i = 0; i < night_streets.size(); ++i) {
            const NightStreet& a = night_streets[i];
            if (a.arm) continue;
            for (const NightStreet& b : night_streets) {
                if (!b.arm) continue;
                const float dx = a.pos[0] - b.base[0], dz = a.pos[2] - b.base[1];
                if (dx * dx + dz * dz < 6.25f && std::fabs(a.pos[1] - b.pos[1]) < 4.0f) { drop[i] = true; break; }
            }
        }
        // (round 3e) a sodium street post's top (a pole top or the bend of an L-post) with an arm's lens 2-12 m out at its
        // height (3 m): the post's own pole, its light at that lens (street lamps stand 25 m or more apart; the white
        // town lamps, posts with the head on top, are left)
        for (std::size_t i = 0; i < night_streets.size(); ++i) {
            const NightStreet& a = night_streets[i];
            if (a.arm || a.kind != NightLampName::kPost || a.colour[1] >= 0.7f) continue;
            for (const NightStreet& b : night_streets) {
                if (!b.arm) continue;
                const float dx = a.pos[0] - b.pos[0], dz = a.pos[2] - b.pos[2], d2 = dx * dx + dz * dz;
                if (d2 >= 4.0f && d2 <= 144.0f && std::fabs(a.pos[1] - b.pos[1]) < 3.0f) { drop[i] = true; break; }
            }
        }
        for (std::size_t i = 0; i < night_streets.size(); ++i) {
            const NightStreet& a = night_streets[i];
            if (a.extent < 3.0f) continue;
            for (std::size_t j = 0; j < night_streets.size(); ++j) {
                const NightStreet& b = night_streets[j];
                if (j == i || b.extent >= 3.0f) continue;
                const float dx = a.pos[0] - b.pos[0], dz = a.pos[2] - b.pos[2];
                if (dx * dx + dz * dz < 144.0f && std::fabs(a.pos[1] - b.pos[1]) < 2.5f) { drop[i] = true; break; }
            }
        }
        std::size_t k = 0;
        for (std::size_t i = 0; i < night_streets.size(); ++i)
            if (!drop[i]) night_streets[k++] = night_streets[i];
        night_streets.resize(k);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "[b3night] %s %s: %zu lamps (%zu cells), %.1f ms\n", fittings_only ? "tunnel lights" : "street lights", route.c_str(),
                 night_streets.size(), cells.size(), ms);
    if (const char* e = std::getenv("B3_NIGHT_LAMPLIST"); e && *e && *e != '0')   // (diagnostics: every lamp)
        for (const NightStreet& l : night_streets)
            std::fprintf(stderr, "[b3night] lamp %.1f %.1f %.1f height %.1f extent %.1f tint %s base %.1f %.1f %s\n", double(l.pos[0]),
                         double(l.pos[1]), double(l.pos[2]), double(l.height), double(l.extent),
                         l.colour[1] < 0.7f ? "sodium" : l.colour[2] > 0.9f ? "cool" : "white", double(l.base[0]), double(l.base[1]), l.kind == NightLampName::kHead ? "head" : l.arm ? "arm" : "post");
}

// Per frame: the nearest lamps within 120 m whose pool (a sphere of the fade's reach under the lamp) is not wholly
// outside the view, at most `cap`. No lamp pops in or out at the cap: each lamp's gain fades to 0 between 80 % and all
// of the distance of the first lamp left out (or of the 120 m limit), continuous as the camera moves, and (review) by
// rank over the last 8 slots, so when a nearer lamp turns into the view the one pushed out of the cap is already at 0. The pool's size scales with the lamp's height
// (night_street_scale: height / 12 m, 0.75..2): a tall lamp lights a wider pool, about as bright under it.
void View::night_street_frame(const Frame& f, b3gfx::FrameParams& fp) const {
    b3gfx::FrameParams::Night& n = fp.night;
    n.streets = 0;
    const float d2r = 3.14159265f / 180.0f;
    const float* sh = night.street_shape;
    n.street_shape[0] = std::cos(std::clamp(sh[0], 1.0f, 89.0f) * d2r);
    n.street_shape[1] = std::cos(std::clamp(sh[1], sh[0] + 0.5f, 89.5f) * d2r);
    n.street_shape[2] = std::max(sh[2], 0.5f);
    n.street_shape[3] = std::max(sh[3], 0.0f);
    n.street_range[0] = std::max(sh[4], 1.0f);
    n.street_range[1] = std::max(sh[5], n.street_range[0] + 1.0f);
    // (round 3e: split screen draws the pass twice, so each view takes half: 16 on HIGH / ULTRA, 4 on MEDIUM)
    const int full_cap = night.street >= 0 ? night.street : (night.lights >= 8 ? 32 : night.lights >= 4 ? 8 : 0);
    const int cap = std::min(f.split && night.street < 0 ? full_cap / 2 : full_cap, int(b3gfx::FrameParams::Night::kMaxStreet));
    if (cap <= 0 || !n.spot) return;
    if (!night_streets_loaded) night_street_load();
    if (night_streets.empty()) return;
    const float* eye = f.cam.m.r[3];
    const float* vp = fp.view_proj;
    constexpr float kFar = 120.0f;
    constexpr int kRankFade = 8;   // the last 8 of 32 slots fade out by rank (MEDIUM's 8: the last 2)
    std::vector<std::pair<float, int>> cand;
    for (std::size_t i = 0; i < night_streets.size(); ++i) {
        const float* p = night_streets[i].pos;
        const float d2 = (p[0] - eye[0]) * (p[0] - eye[0]) + (p[1] - eye[1]) * (p[1] - eye[1]) + (p[2] - eye[2]) * (p[2] - eye[2]);
        if (d2 > kFar * kFar) continue;
        // the pool's sphere (centre half its reach below the lamp) against the frustum's side planes (clip space)
        const float r = n.street_range[1] * night_street_scale(night_streets[i].height);
        const float c[3] = {p[0], p[1] - 0.5f * r, p[2]};
        float cl[4];
        for (int k = 0; k < 4; ++k) cl[k] = vp[k] * c[0] + vp[4 + k] * c[1] + vp[8 + k] * c[2] + vp[12 + k];
        const float rr = r * 1.2f;
        if (cl[3] < -rr || cl[0] < -cl[3] - rr * 2.0f || cl[0] > cl[3] + rr * 2.0f || cl[1] < -cl[3] - rr * 2.0f || cl[1] > cl[3] + rr * 2.0f) continue;
        cand.emplace_back(d2, int(i));
    }
    std::sort(cand.begin(), cand.end());
    const int k = std::min(cap, int(cand.size()));
    const float cut = int(cand.size()) > cap ? std::sqrt(cand[std::size_t(cap)].first) : kFar;
    for (int i = 0; i < k; ++i) {
        const NightStreet& l = night_streets[std::size_t(cand[std::size_t(i)].second)];
        b3gfx::FrameParams::Night::Street& s = n.street[i];
        std::memcpy(s.pos, l.pos, sizeof l.pos);
        std::memcpy(s.colour, l.colour, sizeof l.colour);
        const float d = std::sqrt(cand[std::size_t(i)].first), u = std::clamp((d - 0.8f * cut) / (0.2f * cut), 0.0f, 1.0f);
        // and by rank over the last kRankFade slots: a lamp turning into the view nearer than the rest shifts every rank
        // by one, so the lamp leaving the cap was already at 0 and the ones before it step by 1 / kRankFade
        const int fade = std::min(kRankFade, cap / 4);
        const float rk = fade > 0 ? std::clamp(float(cap - 1 - i) / float(fade), 0.0f, 1.0f) : 1.0f;
        // (round 3: a low lamp lights its pool less brightly than a tall one, x 0.6..1 by height: gain 4 on 6 m town lamps
        // over snow was a white-out)
        s.gain = (1.0f - u * u * (3.0f - 2.0f * u)) * rk * std::clamp(0.35f + l.height / 20.0f, 0.6f, 1.0f);
        s.scale = night_street_scale(l.height);
    }
    n.streets = k;
}

// round 3b: an LED sign family in the fixture tables: an entry "prefix*!" matches every texture whose name starts with
// the prefix (the sign's wordings and animation frames: bk_warnsigna / b and their frames 3..12, ATB_Sign_Info_Text_A3..
// A15, Chgo_AccidentSign_Words / Words2, Freeway_info_slow / speed)
static bool night_led_family(const std::string& entry, const std::string& name) {
    return entry.size() > 2 && entry[entry.size() - 1] == '!' && entry[entry.size() - 2] == '*' &&
           name.compare(0, entry.size() - 2, entry, 0, entry.size() - 2) == 0;
}

// TUNNEL LIGHTS (§35.4): the textures of tunnel and underpass light fittings per region (the lamp table's tint-2 entries
// and the fittings that only glow: wall and roof lamps without a pool of their own)
static bool night_tunnel_fitting(const std::string& region, const std::string& name) {
    static const std::map<std::string, std::vector<std::string>> extra = {
        {"AS", {"HK_INS_LightRigTop", "HK_Tunnel03"}},   // (xboxglow: HK_Tunnel03's roof strip, by its glow mask)
        {"EU", {"ATB_TunnelB", "ATB_TunnelRnd", "R_tunnel_A", "ATB_LightDecals"}},
        {"US", {"GL_tunnel_light"}},
    };
    for (const NightLampName& l : night_lamp_names(region))
        if (l.tint == 2 && name == l.name) return true;
    const auto it = extra.find(region);
    if (it != extra.end())
        for (const std::string& e : it->second)
            if (e == name) return true;
    return false;
}

static const std::vector<std::string>& night_fixture_names(const std::string& region);

// nightfix: the billboards lit by floodlights (the region's '+' fixture names). Their vertical triangles (instanced ones
// through their placement), those whose boxes touch and whose planes are parallel joined: one board each (the triangles'
// horizontal normals, sign-aligned, area-weighted); its bottom, top and extent along the plane. 1-6 fixtures (one per
// ~6 m of width) 0.3 m under the bottom edge, 2 m out on each side, each aimed at 45 % of the board's height above it (which
// face shows is the camera's business: a fixture behind the face the camera sees gives no light there, the facing term).
void View::night_board_load() const {
    night_boards.clear();
    std::vector<std::string> names;
    for (const std::string& l : night_fixture_names(route_region(route)))
        if (!l.empty() && l.back() == '+') names.push_back(l.substr(0, l.size() - 1));
    if (names.empty()) return;
    const b3asset::Model& m = hi.model;
    struct Tri { float v[3][3]; float n[3]; float area; float lo[3], hi[3]; };
    std::vector<Tri> all;
    for (const b3asset::DrawRange& d : m.draws) {
        if (d.texture < 0 || std::size_t(d.texture) >= m.textures.size()) continue;
        if (std::find(names.begin(), names.end(), m.textures[std::size_t(d.texture)].name) == names.end()) continue;
        const float* T = d.transform >= 0 && std::size_t(d.transform) < m.transforms.size() ? m.transforms[std::size_t(d.transform)].data() : nullptr;
        for (std::uint32_t i = d.first_index; i + 2 < d.first_index + d.index_count && i + 2 < m.indices.size(); i += 3) {
            Tri t{};
            for (int c = 0; c < 3; ++c) {
                const float* p = m.vertices[m.indices[i + std::uint32_t(c)]].pos;
                for (int k = 0; k < 3; ++k) t.v[c][k] = T ? T[k] * p[0] + T[4 + k] * p[1] + T[8 + k] * p[2] + T[12 + k] : p[k];
            }
            const float a[3] = {t.v[1][0] - t.v[0][0], t.v[1][1] - t.v[0][1], t.v[1][2] - t.v[0][2]};
            const float b[3] = {t.v[2][0] - t.v[0][0], t.v[2][1] - t.v[0][1], t.v[2][2] - t.v[0][2]};
            t.n[0] = a[1] * b[2] - a[2] * b[1]; t.n[1] = a[2] * b[0] - a[0] * b[2]; t.n[2] = a[0] * b[1] - a[1] * b[0];
            t.area = 0.5f * std::sqrt(t.n[0] * t.n[0] + t.n[1] * t.n[1] + t.n[2] * t.n[2]);
            const float hn = std::sqrt(t.n[0] * t.n[0] + t.n[2] * t.n[2]);
            if (!(t.area > 1e-2f) || !(hn > 0.9f * 2.0f * t.area)) continue;   // (vertical faces only: the board, not its walkway)
            for (int k = 0; k < 3; ++k) {
                t.lo[k] = std::min({t.v[0][k], t.v[1][k], t.v[2][k]}) - 0.3f;
                t.hi[k] = std::max({t.v[0][k], t.v[1][k], t.v[2][k]}) + 0.3f;
            }
            all.push_back(t);
        }
    }
    // boards: triangles whose boxes touch and whose planes are parallel, joined (union-find; a board is a few large
    // triangles with vertices only at its corners)
    std::vector<int> parent(all.size());
    for (std::size_t i = 0; i < all.size(); ++i) parent[i] = int(i);
    const std::function<int(int)> root = [&](int i) { while (parent[std::size_t(i)] != i) i = parent[std::size_t(i)] = parent[std::size_t(parent[std::size_t(i)])]; return i; };
    for (std::size_t i = 0; i < all.size(); ++i)
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            const Tri& p = all[i];
            const Tri& q = all[j];
            bool touch = true;
            for (int k = 0; k < 3 && touch; ++k) touch = p.lo[k] <= q.hi[k] && q.lo[k] <= p.hi[k];
            if (!touch) continue;
            const float dn = (p.n[0] * q.n[0] + p.n[2] * q.n[2]) / (2.0f * p.area * 2.0f * q.area);
            if (std::fabs(dn) < 0.9f) continue;
            parent[std::size_t(root(int(i)))] = root(int(j));
        }
    std::map<int, std::vector<const Tri*>> groups;
    for (std::size_t i = 0; i < all.size(); ++i) groups[root(int(i))].push_back(&all[i]);
    for (const auto& [gid, tris] : groups) {
        (void)gid;
        // the plane: horizontal normals aligned to the largest triangle's, area-weighted
        const Tri* big = tris[0];
        for (const Tri* t : tris) if (t->area > big->area) big = t;
        float n[3] = {0, 0, 0}, c[3] = {0, 0, 0}, wsum = 0;
        for (const Tri* t : tris) {
            const float sgn = t->n[0] * big->n[0] + t->n[2] * big->n[2] < 0 ? -1.0f : 1.0f;
            n[0] += sgn * t->n[0];
            n[2] += sgn * t->n[2];
            for (int v = 0; v < 3; ++v)
                for (int k = 0; k < 3; ++k) c[k] += t->area * t->v[v][k] / 3.0f;
            wsum += t->area;
        }
        const float nl = std::sqrt(n[0] * n[0] + n[2] * n[2]);
        if (!(nl > 1e-6f) || !(wsum > 0)) continue;
        n[0] /= nl; n[2] /= nl;
        for (float& v : c) v /= wsum;
        const float tg[2] = {-n[2], n[0]};   // along the board (x, z)
        float lo = 1e30f, hi2 = -1e30f, smin = 1e30f, smax = -1e30f;
        for (const Tri* t : tris)
            for (int v = 0; v < 3; ++v) {
                lo = std::min(lo, t->v[v][1]);
                hi2 = std::max(hi2, t->v[v][1]);
                const float sv = (t->v[v][0] - c[0]) * tg[0] + (t->v[v][2] - c[2]) * tg[1];
                smin = std::min(smin, sv);
                smax = std::max(smax, sv);
            }
        const float height = hi2 - lo, width = smax - smin;
        if (height < 1.0f || width < 1.0f || height > 40.0f || width > 60.0f) continue;
        const int count = std::clamp(int(std::lround(width / 6.0f)), 1, 6);   // (one per ~6 m: the fans with dark gaps)
        for (int i = 0; i < count; ++i) {
            const float sv = smin + width * (float(i) + 0.5f) / float(count);
            for (float side : {1.0f, -1.0f}) {
                NightBoard b{};
                b.pos[0] = c[0] + tg[0] * sv + side * n[0] * 2.0f;
                b.pos[1] = lo - 0.3f;
                b.pos[2] = c[2] + tg[1] * sv + side * n[2] * 2.0f;
                const float target[3] = {c[0] + tg[0] * sv, lo + 0.45f * height, c[2] + tg[1] * sv};
                float ax[3] = {target[0] - b.pos[0], target[1] - b.pos[1], target[2] - b.pos[2]};
                const float al = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
                for (int k = 0; k < 3; ++k) b.axis[k] = ax[k] / al;
                b.normal[0] = side * n[0]; b.normal[1] = 0; b.normal[2] = side * n[2];
                b.size = height;
                night_boards.push_back(b);
            }
        }
    }
    std::fprintf(stderr, "[b3night] billboard floodlights %s: %zu fixtures\n", route.c_str(), night_boards.size());
    if (const char* e = std::getenv("B3_NIGHT_LAMPLIST"); e && *e && *e != '0')   // (diagnostics: every fixture)
        for (const NightBoard& b : night_boards)
            std::fprintf(stderr, "[b3night] fixture %.1f %.1f %.1f normal %.2f %.2f board %.1f m\n", double(b.pos[0]), double(b.pos[1]),
                         double(b.pos[2]), double(b.normal[0]), double(b.normal[2]), double(b.size));
}

// round 3b: the neon / LED halos. Each '!' texture's lit texels (the brightest channel above 0.5) averaged into its
// colour (the brightest channel scaled to 1); its triangles' centres in 3 m cells, neighbours joined: one sign each, the
// halo at its centre, half its largest extent (0.6..4 m) across
void View::night_glow_load() const {
    night_glows.clear();
    std::vector<std::string> names;   // the '!' entries (exact names, or "prefix*!" families)
    for (const std::string& l : night_fixture_names(route_region(route)))
        if (!l.empty() && l.back() == '!') names.push_back(l);
    if (names.empty()) return;
    const auto listed = [&](const std::string& n) {
        for (const std::string& l : names)
            if (night_led_family(l, n) || (l.compare(0, l.size() - 1, n) == 0 && l.size() - 1 == n.size())) return true;
        return false;
    };
    const b3asset::Model& m = hi.model;
    std::map<int, std::array<float, 3>> colour;   // per texture index
    using Key = std::pair<int, int>;
    struct Cell { float lo[3], hi[3]; int tex; };
    std::map<Key, Cell> cells;
    for (const b3asset::DrawRange& d : m.draws) {
        if (d.texture < 0 || std::size_t(d.texture) >= m.textures.size()) continue;
        const b3asset::Texture& tx = m.textures[std::size_t(d.texture)];
        if (!listed(tx.name)) continue;
        if (!colour.count(d.texture)) {
            const std::vector<b3asset::Image>& lv = !tx.original.empty() ? tx.original : tx.levels;
            double acc[3] = {0, 0, 0};
            if (!lv.empty())
                for (std::size_t k = 0; k + 3 < lv[0].rgba.size(); k += 4) {
                    const std::uint8_t* p = &lv[0].rgba[k];
                    if (std::max({p[0], p[1], p[2]}) < 128) continue;
                    for (int c = 0; c < 3; ++c) acc[c] += p[c];
                }
            const double mx = std::max({acc[0], acc[1], acc[2], 1.0});
            colour[d.texture] = {float(acc[0] / mx), float(acc[1] / mx), float(acc[2] / mx)};
        }
        const float* T = d.transform >= 0 && std::size_t(d.transform) < m.transforms.size() ? m.transforms[std::size_t(d.transform)].data() : nullptr;
        for (std::uint32_t i = d.first_index; i + 2 < d.first_index + d.index_count && i + 2 < m.indices.size(); i += 3) {
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi2[3] = {-1e30f, -1e30f, -1e30f}, c[3] = {0, 0, 0};
            for (int v = 0; v < 3; ++v) {
                const float* p = m.vertices[m.indices[i + std::uint32_t(v)]].pos;
                for (int k = 0; k < 3; ++k) {
                    const float w = T ? T[k] * p[0] + T[4 + k] * p[1] + T[8 + k] * p[2] + T[12 + k] : p[k];
                    lo[k] = std::min(lo[k], w); hi2[k] = std::max(hi2[k], w); c[k] += w / 3.0f;
                }
            }
            const Key key{int(std::floor(c[0] / 3.0f)), int(std::floor(c[2] / 3.0f))};
            auto it = cells.find(key);
            if (it == cells.end()) cells[key] = Cell{{lo[0], lo[1], lo[2]}, {hi2[0], hi2[1], hi2[2]}, d.texture};
            else for (int k = 0; k < 3; ++k) { it->second.lo[k] = std::min(it->second.lo[k], lo[k]); it->second.hi[k] = std::max(it->second.hi[k], hi2[k]); }
        }
    }
    std::set<Key> used;
    for (const auto& [key, c0] : cells) {
        if (used.count(key)) continue;
        Cell g = c0;
        std::vector<Key> stack{key};
        used.insert(key);
        while (!stack.empty()) {
            const Key k = stack.back();
            stack.pop_back();
            const Cell& c = cells[k];
            for (int j = 0; j < 3; ++j) { g.lo[j] = std::min(g.lo[j], c.lo[j]); g.hi[j] = std::max(g.hi[j], c.hi[j]); }
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    const Key q{k.first + dx, k.second + dz};
                    if (cells.count(q) && !used.count(q)) { used.insert(q); stack.push_back(q); }
                }
        }
        NightGlow n{};
        for (int k = 0; k < 3; ++k) n.pos[k] = 0.5f * (g.lo[k] + g.hi[k]);
        const auto& col = colour[g.tex];
        std::memcpy(n.colour, col.data(), sizeof n.colour);
        n.half = std::clamp(0.4f * std::max({g.hi[0] - g.lo[0], g.hi[1] - g.lo[1], g.hi[2] - g.lo[2]}), 0.5f, 2.5f);
        night_glows.push_back(n);
    }
    std::fprintf(stderr, "[b3night] neon / LED halos %s: %zu\n", route.c_str(), night_glows.size());
}

// Per frame: the nearest 16 fixtures within 300 m on the camera's side of their board whose reach (1.5 x the board's
// height) is not wholly outside the view; faded at the cap as the street lamps are (distance and the last 4 ranks)
void View::night_board_frame(const Frame& f, b3gfx::FrameParams& fp) const {
    b3gfx::FrameParams::Night& n = fp.night;
    n.boards = 0;
    if (!n.spot || night.lights < 4) return;
    static const bool off = [] { const char* e = std::getenv("B3_NIGHT_BOARDS"); return e && *e == '0'; }();   // (diagnostics: none)
    if (off) return;
    if (!night_streets_loaded) night_street_load();   // (the boards load with the lamps)
    if (night_boards.empty()) return;
    const float* eye = f.cam.m.r[3];
    const float* vp = fp.view_proj;
    constexpr float kFar = 300.0f;   // (round 3d: 150 -> 300 m: a big board reads from mid distance)
    const int cap = f.split ? 8 : int(b3gfx::FrameParams::Night::kMaxBoard);   // (split screen: half per view)
    std::vector<std::pair<float, int>> cand;
    for (std::size_t i = 0; i < night_boards.size(); ++i) {
        const NightBoard& b = night_boards[i];
        const float e[3] = {eye[0] - b.pos[0], eye[1] - b.pos[1], eye[2] - b.pos[2]};
        if (e[0] * b.normal[0] + e[2] * b.normal[2] <= 0.0f) continue;
        const float d2 = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
        if (d2 > kFar * kFar) continue;
        const float r = 1.5f * b.size;
        const float c[3] = {b.pos[0], b.pos[1] + 0.5f * b.size, b.pos[2]};
        float cl[4];
        for (int k = 0; k < 4; ++k) cl[k] = vp[k] * c[0] + vp[4 + k] * c[1] + vp[8 + k] * c[2] + vp[12 + k];
        if (cl[3] < -r || cl[0] < -cl[3] - r * 2.0f || cl[0] > cl[3] + r * 2.0f || cl[1] < -cl[3] - r * 2.0f || cl[1] > cl[3] + r * 2.0f) continue;
        cand.emplace_back(d2, int(i));
    }
    std::sort(cand.begin(), cand.end());
    const int k = std::min(cap, int(cand.size()));
    const float cut = int(cand.size()) > cap ? std::sqrt(cand[std::size_t(cap)].first) : kFar;
    for (int i = 0; i < k; ++i) {
        const NightBoard& b = night_boards[std::size_t(cand[std::size_t(i)].second)];
        b3gfx::FrameParams::Night::Board& o = n.board_lamp[i];
        std::memcpy(o.pos, b.pos, sizeof o.pos);
        std::memcpy(o.axis, b.axis, sizeof o.axis);
        o.size = b.size;
        const float d = std::sqrt(cand[std::size_t(i)].first), u = std::clamp((d - 0.8f * cut) / (0.2f * cut), 0.0f, 1.0f);
        const float rk = std::clamp(float(cap - 1 - i) / 4.0f, 0.0f, 1.0f);
        o.gain = (1.0f - u * u * (3.0f - 2.0f * u)) * rk;
    }
    n.boards = k;
}

// nightfix: a corona at every lamp found (not only the pools' 32) in front of the camera within 300 m: a square of
// `night.lamp_corona[1]` m half-size around the head (a head is ~1.5 m), at least `lamp_corona[2]` of the view's half
// width so a far lamp stays a few pixels, pulled 1 m towards the eye for the Z test (the head's own geometry); its
// colour the lamp's tint x `lamp_corona[0]` (GS units, 0x80 = 1), full to 80 m, 35 % at 300 m. The billboards'
// floodlights (night_boards) take the same, smaller. Drawn as the game's corona array 0 (additive, night_glow: after
// the grade, into the bloom).
void View::night_lamp_coronas(const Camera& cam, std::vector<CoronaQuad>& quads, bool day) const {
    // $B3_NIGHT_LAMPMARK=1 (diagnostics): the lamps as small magenta markers, nothing else of the coronas
    static const bool mark = [] { const char* e = std::getenv("B3_NIGHT_LAMPMARK"); return e && *e && *e != '0'; }();
    if (!day && !night_streets_loaded) night_street_load();
    if (!(cam.half_w > 0) || !(cam.half_h > 0)) return;
    const float* P = cam.m.r[3];
    const float rl = std::sqrt(cam.m.r[0][0] * cam.m.r[0][0] + cam.m.r[0][1] * cam.m.r[0][1] + cam.m.r[0][2] * cam.m.r[0][2]);
    const float ul = std::sqrt(cam.m.r[1][0] * cam.m.r[1][0] + cam.m.r[1][1] * cam.m.r[1][1] + cam.m.r[1][2] * cam.m.r[1][2]);
    if (!(rl > 0) || !(ul > 0)) return;
    const float gain = night.lamp_corona[0], base = night.lamp_corona[1], minfrac = night.lamp_corona[2];
    if (!(gain > 0)) return;
    const auto add = [&](const float pos[3], const float col[3], float half, float g) {
        const float d[3] = {pos[0] - P[0], pos[1] - P[1], pos[2] - P[2]};
        const float w = d[0] * cam.m.r[2][0] + d[1] * cam.m.r[2][1] + d[2] * cam.m.r[2][2];
        if (!(w > cam.znear + 1.0f) || w > 300.0f) return;
        const float xs = -(d[0] * cam.m.r[0][0] + d[1] * cam.m.r[0][1] + d[2] * cam.m.r[0][2]) / rl / w / cam.half_w;
        const float ys = (d[0] * cam.m.r[1][0] + d[1] * cam.m.r[1][1] + d[2] * cam.m.r[1][2]) / ul / w / cam.half_h;
        if (std::fabs(xs) > 1.15f || std::fabs(ys) > 1.15f) return;
        const float h = std::max(half, w * cam.half_w * (mark ? 0.0035f : minfrac));
        const float fade = w < 80.0f ? 1.0f : 1.0f - 0.65f * std::min((w - 80.0f) / 220.0f, 1.0f);
        CoronaQuad q;
        for (int l = 0; l < 3; ++l) q.rgb[l] = std::uint8_t(std::clamp(col[l] * gain * g * fade, 0.0f, 255.0f));
        const float pulled = std::max(w - 1.0f, cam.znear * 1.01f), k = pulled / w;
        float ctr[3], R[3], U[3];
        for (int l = 0; l < 3; ++l) {
            ctr[l] = P[l] + d[l] * k;
            R[l] = -cam.m.r[0][l] / rl * h * k;
            U[l] = cam.m.r[1][l] / ul * h * k;
        }
        for (int l = 0; l < 3; ++l) {
            q.corner[0][l] = ctr[l] - R[l] + U[l];
            q.corner[1][l] = ctr[l] + R[l] + U[l];
            q.corner[2][l] = ctr[l] + R[l] - U[l];
            q.corner[3][l] = ctr[l] - R[l] - U[l];
        }
        q.depth = w;
        quads.push_back(q);
    };
    if (day) {   // TUNNEL LIGHTS by day (§35.4): the fittings' halos only, small and at 22 % (the tunnel is lit)
        if (day_fittings_job.valid()) day_fittings = day_fittings_job.get();   // (built at the load: waits only if not done)
        for (const NightStreet& l : day_fittings) add(l.pos, l.colour, 0.3f * base, 0.22f);
        return;
    }
    if (mark) {   // (diagnostics: a small magenta marker at each lamp instead of its corona, to check the place)
        // magenta an arm's lens, cyan a straight post's top, yellow an atlas head
        static const float kMark[3][3] = {{1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}};
        for (const NightStreet& l : night_streets) add(l.pos, kMark[l.kind == NightLampName::kHead ? 2 : l.arm ? 0 : 1], 0.25f, 1.6f);
        return;
    }
    // (a low town or gas lamp: a smaller, dimmer corona than a 17 m motorway head)
    for (const NightStreet& l : night_streets) add(l.pos, l.colour, base * std::clamp(l.height / 14.0f, 0.3f, 1.0f), std::clamp(0.45f + l.height / 20.0f, 0.6f, 1.0f));
    for (const NightGlow& g : night_glows) add(g.pos, g.colour, g.half, 0.22f);   // round 3b: the neon / LED halos (soft)
    static const float kFlood[3] = {1.0f, 0.93f, 0.8f};
    for (const NightBoard& b : night_boards) {   // the fixtures on the camera's side of their board
        const float e[3] = {P[0] - b.pos[0], P[1] - b.pos[1], P[2] - b.pos[2]};
        if (e[0] * b.normal[0] + e[2] * b.normal[2] > 0.0f) add(b.pos, kFlood, 0.55f * base, 0.8f);
    }
}

// nightlights (§17.2, D.2): what gives light at night, by texture name, one table per region (the TRACKS/<region>
// folder of the route): tunnel lamps, neon, lit signs and shop fronts, lit advertising panels, traffic and lane signals,
// lamp heads with their own texture. Found with $B3_NIGHT_TEXLOG (1: textures drawn inside tunnels, 2: every texture
// drawn) and each checked by eye in night shots with $B3_NIGHT_FIXTURES. A plain name glows where its texel is bright
// (the brightest channel above ~0.6, so coloured neon keeps its colour and the housing, the frame or the painted part
// stays dark); a name ending in '*' glows whole (a backlit panel, where a threshold would speckle a photograph), one in '+' is a billboard
// lit by floodlights (nightfix: lit by its fixtures in the beam pass, night_board_load, the board a faint warm base);
// round 3 (nightfix): '^' a shop front (its glass bright and warm, the rest a warm wash), '%' a facade with windows
// (each window -- the texture's grid of dark glass, View::night_window_grid -- lit or not by a hash of where it is, in
// warm, neutral or cool light).
// Painted signs, posters, road signs and road markings are left out.
static const std::vector<std::string>& night_fixture_names(const std::string& region) {
    static const std::map<std::string, std::vector<std::string>> names = {
        // Asia (HK)
        {"AS", {
            "HK_INS_LightRig", "HK_INS_LightRigTop",          // the tunnel's ceiling light rigs (AS/C2_V1)
            "HK_Spotlight", "HK_ResLite",                     // lamps under the terminal roof, lit residential windows
            "HK_Neon!", "HK_Shop01^", "HK_DepStore",          // neon (with the tunnel's lane X / arrow), shop fronts, the store
            "bk_Lit_inter1", "bk_Lit_inter2",                 // lit interiors
            "HK_BigBilly01*", "HK_BigBilly02*", "HK_BigBilly03*", "HK_BigBilly04*",   // billboards (backlit)
            "HK_OfficalADV01*", "HK_OfficalADV02*",           // advertising panels (backlit)
            "KS_lantern",                                     // the lanterns of the island route (AS/C3_V2)
            "bk_billboards2+", "bk_skytrainads+", "KS_adverts+",   // billboards and posters lit by floodlights (nightfix)
            // round 3b (nightfix): neon and LED ('!': the tubes / dots saturated and bright, a coloured halo), the
            // road-closure LED boards, retro-reflective signs ('~': bright when the headlights face them)
            // (an LED family by name prefix, "name*!": every wording and animation frame of the sign, e.g. bk_warnsignb4..12)
            "bk_neonsigns!", "bk_marketneonsigns!", "bk_marketneonsigns2!", "bk_warnsign*!", "Arrows!", "Barrier!", "HK_Tunnel01",
            "bk_diagonalwarningsign~", "bk_lollypoparrow~", "HK_Yellowarrow~", "KS_sign_arrow~", "HK_Armco_warn~",
            "HK_DockLite", "KS_lamppost",                     // lamp heads (street lights, night_lamp_names)
            // round 3 (nightfix): shop fronts ('^') and facades with windows ('%'), checked by eye in day views
            "bk_shopbot2^", "bk_shopbot3^", "bk_shopbot3_New^", "bk_shopbot4^", "bk_shopbot5^", "bk_shopbot6_New^",
            "bk_charkrungshops^", "bk_marketshops2^", "bk_shopcentre1^", "KS_shop01^", "KS_shop02^", "KS_shop03^",
            "HK_BrownWindow%", "HK_CityBuilding%", "HK_OfficeBase%", "HK_BD_Buildings01%", "HK_BD_buildings002%", "HK_TerBuild%",
            "KS_apartments01%", "KS_apartments02%", "KS_apartments03%", "KS_apartments04%", "KS_apartments05%",
            "KS_apartments06%", "KS_apartmentside01%", "KS_apartmentside02%", "KS_buildingside%", "KS_buildingside02%",
            "bk_building1%", "bk_building2%", "bk_building3%", "bk_building4%", "bk_building5%", "bk_building6%",
            "bk_building8%", "bk_building12%", "bk_buildings1%", "bk_charkrungflats1%", "bk_charkrungflats2%",
            "bk_citybuildings1%", "bk_dttowers%", "bk_freewayoffice1%", "bk_freewayoffice2%", "bk_freewayoffice5%",
            "bk_freewayoffice6%", "bk_freewayoffice7%", "bk_hotel3page1%", "bk_marketbuilding1%", "bk_marketbuilding1b%",
            "bk_riverhotel1b%", "bk_riverhotel2bot%", "bk_rivertowers%", "bk_ShopTop4Glass%", "bk_shoptop1%",
            "bk_shoptop2%", "bk_shoptop3%", "bk_shoptop5%", "bk_shoptop6%", "bk_downtown_bd%",
        }},
        // Europe
        {"EU", {
            "light", "walllamp",                              // the street lamps' heads, wall lamps (EU/P1_V2's town)
            "lamppost", "riverside_lamppost", "ATB_Lamp", "ATB_LampostG", "R_Triplelampost",   // lamp posts: their lit heads
            "shop1^", "nushop1^", "nushop2^", "nushop3^", "nushop4^",
            "nushop5^", "nushop6^", "nushop7^", "nushop8^",   // the lit shop fronts (EU/P1_V2; round 3: '^')
            // round 3 (nightfix): shop fronts ('^') and facades with windows ('%')
            "ShopFront_1^", "ShopFront_2^", "ShopFront_4^", "ShopFront_5a^", "ShopFront_5b^", "ShopFront_6^", "ShopFront_7a^",
            "ShopFront_7b^", "ShopFront_8^", "ShopFront_9^", "ShopFront_10^", "ShopFront_11^", "ShopFront_12^",
            "ShopFront_14^", "R_Shop_01^", "R_Shop_02^", "R_Shop_03^", "R_Shop_04^", "R_Shop_05^", "R_Shop_06^",
            "R_Shop_07^", "R_cafeteria^",
            "Win_1%", "Win_2%", "Win_3%", "Win_4%", "Win_5%", "Win_6%", "Win_7%", "Win_8%", "Win_9%", "Win_10%", "Win_11%",
            "Win_12%", "Win_14%", "Win_15%", "Win_16%", "Win_17%", "Win_18%", "R_Flats01%", "R_Flats02%", "R_Flats03%",
            "R_flat_04%", "R_Hotel01%", "R_Hotel02%", "R_Hotel03%", "R_Hotelblocks%", "R_Hotelblocks2%", "R_Hotelblocks3%",
            "R_Building_01%", "R_Building_02%", "R_Building_03%", "R_Building_04%", "R_Building_05%", "R_Tower01%",
            "R_H1_build01%", "R_House01%", "R_House04%", "ATB_Apartements%", "ATB_ApartementsA%", "ATB_BuildsA%",
            "ATB_BuildsB%", "ATB_BuildsC%", "ATB_BuildsD%", "ATB_Tower%", "HouseFront_1%",
            "HouseFront_2%", "HouseFront_3%", "HouseFront_4%", "HouseFront_5%", "HouseFront_6%", "newbuilding1%", "newalleyGRNDlit^",   // (left out: newalleyUP, pale panes on a darker wall; shinybuilding, large
            // panels; newbuilding and ATB_Vien_Builds, collages, not a grid of windows)
            "ATB_SunHouses%", "R_Blocks_03%",
            "R_Blocks_04%", "R_Blocks_05%",
            "trafficbox",                                     // the traffic lights
            "ATB_Sign_Info_Flash*!",                          // the motorway's lit information boards (EU/C1_V1)
            "Freeway_info*!", "ATB_Sign_Info_Text*!", "ATB_Signs_Elec!", "ATB_Grn_Arrow!", "Arrows!", "Barrier!",
            "ATB_Tunnel_Light", "ATB_Tunnel_DLight",         // (round 3b) LED signs, lane signals, tunnel fittings
            "Arrow_1~", "Arrow_1a~", "R_Arrow_1~", "R_Warning_Marker~", "armco_Stripey~",
            "adboard*", "ATB_Billbds1*", "VIEN_Billbds3*",    // billboards (backlit)
            "ATB_Billbds2+", "R_Adv+", "R_Advs02+",           // billboards lit by floodlights (nightfix)
        }},
        // USA
        {"US", {
            "WF_illumnated1",                                 // the strip lights of US/C1_V1's covered road
            "Chgo_TunLght",                                   // the city's tunnel lamps
            "Chgo_ShopSigns!", "Chgo_UpT_Shop^", "Chgo_UpT_Shop_03^",  // shop signs, neon and shop fronts (US/C2_V2)
            "Chgo_AccidentSign*!", "ATB_Signs_Elec!", "Arrows!", "Barrier!", "GL_tunnel_light",   // (round 3b) LED, fittings
            "Chgo_RdClosed~", "GL_RdClosed~", "CP_Yell_Chev~", "P2P_freeway_laneblocker~",
            // round 3 (nightfix): shop fronts ('^') and facades with windows ('%')
            "Chgo_UpT_Shop_02^", "GL_SHOP1^", "GL_SHOP1b^", "GL_SHOP2^", "GL_SHOP2b^", "GL_SHOP3^", "GL_SHOP3b^", "GL_SHOP4^",
            "GL_SHOP4b^", "WF_hoteldoor^",
            "Chgo_UpTWin_04%", "Chgo_UpTWin_05%", "Chgo_UpTWin_06%", "Chgo_UpTWin_07%", "Chgo_UpTWin_08%", "Chgo_UpTWin_09%",
            "Chgo_UpTWin_10%", "Chgo_UpTWin_11%", "Chgo_UpTWin_01_Frame%", "Chgo_Hotel_02Win_Frame%", "CST_hotelnaut%",
            "HotelM1%", "WF_ApartM1%", "WF_apartment1%", "WF_build1%", "WF_build3%", "WF_hotel1%", "WF_hotel1b%",
            "WF_hotel4a%", "WF_hotel4b%", "WF_office1%", "SD_Build1%", "GL_SHOPTOP1%", "GL_SHOPTOP1b%", "GL_SHOPTOP2%",
            "GL_SHOPTOP2b%", "GL_SHOPTOP4%", "GL_SHOPTOP4b%", "GL_housewalton%", "GL_cabinwind%",
            "Chgo_Dwn_Bck%", "Chgo_DwnT_01%", "Chgo_City_Bck%", "Chgo_City_Bck_02%", "Chgo_City_Bck_03%", "Chgo_UpT_04_Frame%",
            "Chgo_DwnTN_01%", "Chgo_DwnTN_02%", "Chgo_DwnTN_03%", "Chgo_DwnTN_04%", "Chgo_DwnTN_05%", "Chgo_UpT_01N_Frame%",
            "Chgo_BldSide_01%", "Chgo_UpT_Feature_02%",
            "Chgo_words*!",                                   // lit lettering (round 3b: neon)
            "Chgo_TrafficBox", "WF_tlightPost",               // the traffic lights
            "WF_gaslampSHOP1^",                               // the waterfront's gas-lamp shop front (US/C1_V2)
            "Chgo_GrnLpst_03", "Chgo_GrnLpst_New", "Chgo_OrnateLamp", "GL_streetlight2", "WF_OrnateLamp1",
            "WF_LAMP_Post",                                   // lamp posts: their lit heads
            "WF_gaslamp1%", "WF_gaslamp2%", "WF_gaslamp2b%", "WF_gaslamp3%", "WF_gaslamp4%", "WF_gaslamp5%", "WF_gaslampOLD1%",
            "WF_Glamp1%", "WF_Glamp1b%",                      // the Gaslamp quarter's facades (not lamps): lit windows
            "Chgo_BigAdverts_03*", "AXE_AD_TSUNAMI*", "AXE_AD_VOODOO*", "AXE_AD_PHOENIX*",   // billboards (backlit)
            "Chgo_BigAdverts+", "Chgo_BigAdverts_02+", "GL_marina_billboard+", "GL_resort_advert+",   // lit by floodlights (nightfix)
        }},
    };
    static const std::vector<std::string> none;
    const auto it = names.find(region);
    return it == names.end() ? none : it->second;
}


static std::uint64_t night_cell_key(int x, int z) { return std::uint64_t(std::uint32_t(x)) << 32 | std::uint32_t(z); }

// weather (§38): a point under a roof (the cover grid's triangles above it, within 40 m), cached per 2 m column and
// 2 m of height (the rain's streaks ask thousands of times a frame; the cache is the route's)
bool View::rain_sheltered(const float p[3]) const {
    NightCover& g = night_cover_grid;
    if (!g.built) night_cover_build();
    if (g.tri.empty()) return false;
    const int cx = int(std::floor(p[0] * 0.5f)), cz = int(std::floor(p[2] * 0.5f)), cy = int(std::floor(p[1] * 0.5f));
    const std::uint64_t key = (std::uint64_t(std::uint32_t(cx) & 0x1FFFFFu) << 43) | (std::uint64_t(std::uint32_t(cz) & 0x1FFFFFu) << 22) |
                              std::uint64_t(std::uint32_t(cy) & 0x3FFFFFu);
    if (const auto it = rain_roof.find(key); it != rain_roof.end()) return it->second;
    const float o[3] = {2.0f * float(cx) + 1.0f, 2.0f * float(cy) + 1.0f, 2.0f * float(cz) + 1.0f};
    constexpr float kCell = 8.0f;
    bool hit = false;
    const std::uint64_t ck = night_cell_key(int(std::floor(o[0] / kCell)), int(std::floor(o[2] / kCell)));
    const auto it = std::lower_bound(g.keys.begin(), g.keys.end(), ck);
    if (it != g.keys.end() && *it == ck) {
        const std::size_t c = std::size_t(it - g.keys.begin());
        for (std::uint32_t qi = g.first[c]; qi < g.first[c + 1] && !hit; ++qi) {   // a vertical ray up: the triangle over (x, z)
            const float* v = &g.tri[std::size_t(g.ids[qi]) * 9];
            const float d1x = v[3] - v[0], d1z = v[5] - v[2], d2x = v[6] - v[0], d2z = v[8] - v[2];
            const float det = d1x * d2z - d2x * d1z;
            if (std::fabs(det) < 1e-6f) continue;   // (a wall: seen edge-on from above)
            const float px = o[0] - v[0], pz = o[2] - v[2];
            const float a = (px * d2z - d2x * pz) / det, b = (d1x * pz - px * d1z) / det;
            if (a < 0.0f || b < 0.0f || a + b > 1.0f) continue;
            const float y = v[1] + a * (v[4] - v[1]) + b * (v[7] - v[1]);
            hit = y > o[1] + 0.5f && y < o[1] + 40.0f;
        }
    }
    if (rain_roof.size() > 400000) rain_roof.clear();   // (a long drive: start again)
    rain_roof.emplace(key, hit);
    return hit;
}

// weather (§38): the ground below `p` (the highest upward-facing triangle of the cover grid within 30 m under it), cached
// per 2 m column and 2 m of height; false: none
bool View::rain_ground(const float p[3], float& y) const {
    NightCover& g = night_cover_grid;
    if (!g.built) night_cover_build();
    if (g.tri.empty()) return false;
    const int cx = int(std::floor(p[0] * 0.5f)), cz = int(std::floor(p[2] * 0.5f)), cy = int(std::floor(p[1] * 0.5f));
    const std::uint64_t key = (std::uint64_t(std::uint32_t(cx) & 0x1FFFFFu) << 43) | (std::uint64_t(std::uint32_t(cz) & 0x1FFFFFu) << 22) |
                              std::uint64_t(std::uint32_t(cy) & 0x3FFFFFu);
    if (const auto it = rain_floor.find(key); it != rain_floor.end()) {
        y = it->second;
        return y > -1e29f;
    }
    const float o[3] = {p[0], 2.0f * float(cy) + 2.0f, p[2]};   // (the cell's top: the same answer for the whole cell)
    constexpr float kCell = 8.0f;
    float best = -1e30f;
    const std::uint64_t ck = night_cell_key(int(std::floor(o[0] / kCell)), int(std::floor(o[2] / kCell)));
    const auto it = std::lower_bound(g.keys.begin(), g.keys.end(), ck);
    if (it != g.keys.end() && *it == ck) {
        const std::size_t c = std::size_t(it - g.keys.begin());
        for (std::uint32_t qi = g.first[c]; qi < g.first[c + 1]; ++qi) {
            const float* v = &g.tri[std::size_t(g.ids[qi]) * 9];
            const float d1x = v[3] - v[0], d1z = v[5] - v[2], d2x = v[6] - v[0], d2z = v[8] - v[2];
            const float det = d1x * d2z - d2x * d1z;
            if (std::fabs(det) < 1e-6f) continue;
            const float px = o[0] - v[0], pz = o[2] - v[2];
            const float a = (px * d2z - d2x * pz) / det, b = (d1x * pz - px * d1z) / det;
            if (a < 0.0f || b < 0.0f || a + b > 1.0f) continue;
            const float ty = v[1] + a * (v[4] - v[1]) + b * (v[7] - v[1]);
            if (ty < o[1] - 0.05f && ty > o[1] - 32.0f) best = std::max(best, ty);
        }
    }
    if (rain_floor.size() > 200000) rain_floor.clear();
    rain_floor.emplace(key, best);
    y = best;
    return best > -1e29f;
}

void View::night_cover_build() const {
    NightCover& g = night_cover_grid;
    g = NightCover{};
    g.built = true;
    constexpr float kCell = 8.0f;
    const auto t0 = std::chrono::steady_clock::now();
    const b3asset::Model& m = hi.model;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> pairs;
    for (const b3asset::DrawRange& d : m.draws) {
        int u = 0;
        if (d.transform >= 0 || d.blend == b3asset::DrawRange::kBlend || std::sscanf(d.part.c_str(), "unit %d", &u) != 1 ||
            d.part.find("instance") != std::string::npos)
            continue;
        for (std::uint32_t i = d.first_index; i + 2 < d.first_index + d.index_count && i + 2 < m.indices.size(); i += 3) {
            float lo[2] = {1e30f, 1e30f}, hi2[2] = {-1e30f, -1e30f};
            for (int c = 0; c < 3; ++c) {
                const float* p = m.vertices[m.indices[i + std::uint32_t(c)]].pos;
                lo[0] = std::min(lo[0], p[0]); hi2[0] = std::max(hi2[0], p[0]);
                lo[1] = std::min(lo[1], p[2]); hi2[1] = std::max(hi2[1], p[2]);
            }
            const int x0 = int(std::floor(lo[0] / kCell)), x1 = int(std::floor(hi2[0] / kCell));
            const int z0 = int(std::floor(lo[1] / kCell)), z1 = int(std::floor(hi2[1] / kCell));
            if ((x1 - x0) > 64 || (z1 - z0) > 64) continue;   // (a degenerate or huge triangle: not a ceiling)
            const std::uint32_t t = std::uint32_t(g.tri.size() / 9);
            for (int c = 0; c < 3; ++c)
                for (int k = 0; k < 3; ++k) g.tri.push_back(m.vertices[m.indices[i + std::uint32_t(c)]].pos[k]);
            for (int x = x0; x <= x1; ++x)
                for (int z = z0; z <= z1; ++z) pairs.emplace_back(night_cell_key(x, z), t);
        }
    }
    std::sort(pairs.begin(), pairs.end());
    g.ids.reserve(pairs.size());
    for (std::size_t k = 0; k < pairs.size(); ++k) {
        if (k == 0 || pairs[k].first != pairs[k - 1].first) {
            g.keys.push_back(pairs[k].first);
            g.first.push_back(std::uint32_t(g.ids.size()));
        }
        g.ids.push_back(pairs[k].second);
    }
    g.first.push_back(std::uint32_t(g.ids.size()));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const double mb = double(g.tri.size() * 4 + g.keys.size() * 8 + g.first.size() * 4 + g.ids.size() * 4) / 1048576.0;
    std::fprintf(stderr, "[b3night] cover grid %s: %zu triangles, %zu cells, %.1f ms, %.1f MB\n", route.c_str(), g.tri.size() / 9,
                 g.keys.size(), ms, mb);
}

float View::night_cover(const float pos[3], float* wide) const {
    NightCover& g = night_cover_grid;
    constexpr float kCell = 8.0f, kRange = 40.0f;
    static const float kWide = [] { const char* e = std::getenv("B3_NIGHT_WIDE"); return e && *e ? float(std::atof(e)) : 14.0f; }();
    if (wide) *wide = 0.0f;
    if (!g.built) night_cover_build();
    if (g.tri.empty()) return 0.0f;
    const float o[3] = {pos[0], pos[1] + 1.5f, pos[2]};   // above the car's roof
    const float s35 = 0.5736f, c35 = 0.8192f;
    const float s60 = 0.8660f, c60 = 0.5f;
    int hits = 0, rays = 0, whits = 0;
    // rays 0-8: up and eight at 35 degrees off the vertical (the roof); 9-16 (nightfix, with `wide`): eight at 60 degrees,
    // which pass an open-sided overpass's edges (a deck 10-20 m wide over a street) where a tunnel's walls and roof stop them
    for (int r = 0; r < (wide ? 17 : 9); ++r) {
        // (the ring only matters where the roof rays say covered: enclosed needs a cover above 0.6, so below it skip it)
        if (r == 9 && float(hits) / 9.0f <= 0.6f) break;
        float d[3] = {0, 1, 0};
        if (r > 8) {
            const float a = float(r - 9) * 0.7853982f + 0.3926991f;
            d[0] = s60 * std::cos(a); d[1] = c60; d[2] = s60 * std::sin(a);
        } else if (r > 0) {
            const float a = float(r - 1) * 0.7853982f;
            d[0] = s35 * std::cos(a); d[1] = c35; d[2] = s35 * std::sin(a);
        }
        bool hit = false;
        float tmin = 1e30f;   // the 60-degree rays: the nearest hit (a hit counts within kWide m)
        std::pair<int, int> last{1 << 30, 0};
        for (float t = 0; t <= (r > 8 ? kWide + kCell : kRange) && !hit; t += kCell * 0.5f) {
            const std::pair<int, int> key{int(std::floor((o[0] + d[0] * t) / kCell)), int(std::floor((o[2] + d[2] * t) / kCell))};
            if (key == last) continue;
            last = key;
            const std::uint64_t ck = night_cell_key(key.first, key.second);
            const auto it = std::lower_bound(g.keys.begin(), g.keys.end(), ck);
            if (it == g.keys.end() || *it != ck) continue;
            const std::size_t c = std::size_t(it - g.keys.begin());
            for (std::uint32_t qi = g.first[c]; qi < g.first[c + 1]; ++qi) {   // Moller-Trumbore, both faces
                const std::uint32_t ti = g.ids[qi];
                const float* v = &g.tri[std::size_t(ti) * 9];
                const float e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]}, e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
                const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
                const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
                if (std::fabs(det) < 1e-8f) continue;
                const float inv = 1.0f / det;
                const float tv[3] = {o[0] - v[0], o[1] - v[1], o[2] - v[2]};
                const float uu = (tv[0] * p[0] + tv[1] * p[1] + tv[2] * p[2]) * inv;
                if (uu < 0.0f || uu > 1.0f) continue;
                const float q[3] = {tv[1] * e1[2] - tv[2] * e1[1], tv[2] * e1[0] - tv[0] * e1[2], tv[0] * e1[1] - tv[1] * e1[0]};
                const float vv = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
                if (vv < 0.0f || uu + vv > 1.0f) continue;
                const float th = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
                if (r > 8) { if (th > 0.05f) tmin = std::min(tmin, th); }
                else if (th > 0.05f && th < kRange) { hit = true; break; }
            }
            if (r > 8 && tmin < kWide) hit = true;
        }
        if (r > 8) whits += hit ? 1 : 0;
        else {
            hits += hit ? 1 : 0;
            ++rays;
        }
    }
    if (wide) *wide = float(whits) / 8.0f;
    return float(hits) / float(rays);
}

// nightfix round 3: a facade texture's mean luminance (x 255) and its grid of windows (columns, rows: the dark runs of
// its column and row luminance profiles below their mean, 1..8 each; 4 x 4 when the texture has no RGBA level, an HD
// pack's compressed one)
std::array<std::uint8_t, 3> View::night_window_grid(const b3asset::Texture& tx) {
    const std::vector<b3asset::Image>& lv = !tx.original.empty() ? tx.original : tx.levels;
    if (lv.empty() || lv[0].rgba.empty() || lv[0].w < 4 || lv[0].h < 4) return {96, 4, 4};
    const b3asset::Image& im = lv[0];
    std::vector<double> col(std::size_t(im.w), 0.0), row(std::size_t(im.h), 0.0);
    double sum = 0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            const std::uint8_t* p = &im.rgba[(std::size_t(y) * std::size_t(im.w) + std::size_t(x)) * 4];
            const double l = (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) / 255.0;
            col[std::size_t(x)] += l;
            row[std::size_t(y)] += l;
            sum += l;
        }
    const auto runs = [](const std::vector<double>& v) {
        double m = 0;
        for (double x : v) m += x;
        m /= double(v.size());
        int n = 0;
        bool below = v.back() < m * 0.97;
        for (double x : v) {
            const bool b = x < m * 0.97;
            if (b && !below) ++n;
            below = b;
        }
        return std::uint8_t(std::clamp(n, 1, 8));
    };
    const double mean = sum / (double(im.w) * double(im.h));
    return {std::uint8_t(std::clamp(mean * 255.0, 8.0, 255.0)), runs(col), runs(row)};
}

void View::day_fittings_reset() {
    if (day_fittings_job.valid()) day_fittings_job.wait();
    day_fittings_job = {};
    day_fittings.clear();
    day_route = false;
}

void View::night_fixtures_load() {
    night_cover_grid = NightCover{};
    // at the load, not on the first night frame (else built when night is turned on); todrace: the race's own choice too
    rain_roof.clear();
    rain_floor.clear();
    rain_dry_built = false;
    if (night_at_load() || rain_at_load()) night_cover_build();   // (weather: the rain's roofs too)
    if (rain_at_load()) rain_dry_build();
    // street lights: the lamps (3-17 ms) when the race is at night or $B3_NIGHT is set; else on the first night frame
    // (night turned on mid-race), so a day race's load pays nothing
    night_streets.clear();
    night_boards.clear();
    night_glows.clear();
    night_streets_loaded = false;
    day_fittings_reset();   // TUNNEL LIGHTS (§35.4): built on a worker below, when the route has fittings
    glow_masks.clear();     // xboxglow
    if (night_at_load() || night_env_set()) night_street_load();
    night_texinfo.clear();
    night_texlogged.clear();
    std::vector<std::string> list = night_fixture_names(route_region(route));
    if (const char* e = std::getenv("B3_NIGHT_FIXTURES"); e && *e) {   // (finding the names: this list instead, any region)
        list.clear();
        for (const char* p = e; *p;) {
            const char* c = std::strchr(p, ',');
            list.emplace_back(p, c ? std::size_t(c - p) : std::strlen(p));
            if (!c) break;
            p = c + 1;
        }
    }
    for (b3view::Scene* sc : {&hi, &lo}) {
        const std::size_t base = sc->item_base;
        for (std::size_t i = 0; i < sc->model.draws.size() && i + base < sc->items.size(); ++i) {
            const int t = sc->model.draws[i].texture;
            if (t < 0 || std::size_t(t) >= sc->model.textures.size()) continue;
            const std::string& name = sc->model.textures[std::size_t(t)].name;
            // a name ending in '*': the whole texture glows (a backlit panel); in '+': a billboard lit by its floodlights
            // (nightfix: the whole board, warm); else its bright texels only
            std::uint8_t mode = 0;
            for (const std::string& l : list)
                if (l == name) mode = 1;
                else if (night_led_family(l, name)) mode = 6;   // round 3b: an LED family ("prefix*!")
                else if (!l.empty() && std::strchr("*+^%!~", l.back()) && l.compare(0, l.size() - 1, name) == 0 && l.size() - 1 == name.size())
                    mode = l.back() == '*' ? 2 : l.back() == '+' ? 3 : l.back() == '^' ? 5 : l.back() == '%' ? 4 : l.back() == '!' ? 6 : 7;
            b3gfx::DrawItem& it = sc->items[i + base];
            it.night_emissive = mode;
            // TUNNEL LIGHTS (§35.4; drawn only when on): 1 a tunnel's fitting, 2 another lamp (xboxglow: its glow mask only;
            // no day glow since Xbox all lamps was withdrawn)
            it.day_glow = night_tunnel_fitting(route_region(route), name) ? 1 : xbox_lamp(name) ? 2 : 0;
            if (it.day_glow || mode == 1) {   // xboxglow (§35.5): the glow mask, once per texture
                const b3gfx::TextureId tid = std::size_t(t) < sc->textures.size() ? sc->textures[std::size_t(t)] : b3gfx::kNone;
                auto gm = glow_masks.find(tid);
                if (gm == glow_masks.end() || tid == b3gfx::kNone)
                    gm = glow_masks.insert_or_assign(tid, glow_mask_of(sc->model.textures[std::size_t(t)])).first;
                it.glow_mask = gm->second;
            }
            if (it.day_glow == 1) day_route = true;
            if (mode == 4 || mode == 5) {   // windows / shop fronts: the texture's mean light and its grid of windows
                const auto g = night_window_grid(sc->model.textures[std::size_t(t)]);
                it.night_mean = g[0];
                it.night_cols = g[1];
                it.night_rows = g[2];
                // (a facade whose texture shows no grid of windows, fewer than 2 x 2, would light whole panels: left dark)
                if (mode == 4 && (g[1] < 2 || g[2] < 2)) it.night_emissive = 0;
                static const bool flog = [] { const char* e = std::getenv("B3_NIGHT_FIXLOG"); return e && *e && *e != '0'; }();
                if (flog && night_texlogged.insert(name).second)   // (diagnostics: each window / shop-front texture once)
                    std::fprintf(stderr, "[b3night] fixlog %s mode %d mean %d grid %d x %d%s\n", name.c_str(), int(mode), int(g[0]), int(g[1]),
                                 int(g[2]), it.night_emissive ? "" : " (left dark)");
            }
        }
    }
    if (day_route)   // TUNNEL LIGHTS (§35.4): the fittings' places for the day halos, on a worker (reads hi.model only)
        day_fittings_job = std::async(std::launch::async, [this] {
            std::vector<NightStreet> v;
            night_lamps_build(true, v);
            return v;
        });
    // (diagnostics: $B3_NIGHT_LAMPSCAN=1 counts the lamps a street-light pass would find: the triangles of every lamp-like
    // texture name, their centres clustered within 2 m, per texture)
    if (const char* ls = std::getenv("B3_NIGHT_LAMPSCAN"); ls && *ls && *ls != '0') {
        const b3asset::Model& m = hi.model;
        std::map<std::string, std::vector<std::array<float, 3>>> pts;
        for (const b3asset::DrawRange& d : m.draws) {
            if (d.texture < 0 || std::size_t(d.texture) >= m.textures.size()) continue;
            std::string name = m.textures[std::size_t(d.texture)].name, low = name;
            for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (low.find("lamp") == std::string::npos && low.find("light") == std::string::npos && low.find("lght") == std::string::npos &&
                low.find("lpst") == std::string::npos && low.find("lite") == std::string::npos && low.find("lantern") == std::string::npos)
                continue;
            const float* T = d.transform >= 0 && std::size_t(d.transform) < m.transforms.size() ? m.transforms[std::size_t(d.transform)].data() : nullptr;
            for (std::uint32_t i = d.first_index; i + 2 < d.first_index + d.index_count && i + 2 < m.indices.size(); i += 3) {
                float c[3] = {0, 0, 0};
                for (int v = 0; v < 3; ++v)
                    for (int k = 0; k < 3; ++k) c[k] += m.vertices[m.indices[i + std::uint32_t(v)]].pos[k] / 3.0f;
                std::array<float, 3> w{c[0], c[1], c[2]};
                if (T) for (int k = 0; k < 3; ++k) w[std::size_t(k)] = T[k] * c[0] + T[4 + k] * c[1] + T[8 + k] * c[2] + T[12 + k];
                pts[name].push_back(w);
            }
        }
        for (const auto& [name, v] : pts) {
            std::vector<std::array<float, 3>> cl;
            for (const auto& p : v) {
                bool found = false;
                for (const auto& c : cl)
                    if ((p[0] - c[0]) * (p[0] - c[0]) + (p[1] - c[1]) * (p[1] - c[1]) + (p[2] - c[2]) * (p[2] - c[2]) < 4.0f) { found = true; break; }
                if (!found) cl.push_back(p);
            }
            std::fprintf(stderr, "[b3night] lampscan %s tex %s triangles %zu lamps %zu\n", route_region(route).c_str(), name.c_str(), v.size(), cl.size());
        }
    }
    const char* e = std::getenv("B3_NIGHT_TEXLOG");
    if (!(e && *e && *e != '0')) return;
    for (const b3view::Scene* sc : {&hi, &lo})
        for (std::size_t t = 0; t < sc->model.textures.size() && t < sc->textures.size(); ++t) {
            const b3asset::Texture& tx = sc->model.textures[t];
            const std::vector<b3asset::Image>& lv = !tx.original.empty() ? tx.original : tx.levels;
            NightTex ni;
            ni.name = tx.name;
            if (!lv.empty() && !lv[0].rgba.empty()) {
                const std::vector<std::uint8_t>& px = lv[0].rgba;
                double lum = 0, sat = 0, bright = 0;
                const std::size_t n = px.size() / 4;
                for (std::size_t k = 0; k < n; ++k) {
                    const float r = px[4 * k] / 255.0f, g = px[4 * k + 1] / 255.0f, b = px[4 * k + 2] / 255.0f;
                    const float l = 0.299f * r + 0.587f * g + 0.114f * b;
                    const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
                    const float st = mx > 1e-3f ? (mx - mn) / mx : 0.0f;
                    lum += l;
                    sat += st;
                    if (l > 0.6f && st < 0.3f) bright += 1;
                }
                if (n) { ni.lum = float(lum / double(n)); ni.sat = float(sat / double(n)); ni.bright = float(bright / double(n)); }
            }
            night_texinfo[sc->textures[t]] = ni;
        }
}

void View::night_texlog(const std::vector<b3gfx::DrawItem>& out, std::size_t first) const {
    static const bool all = [] { const char* e = std::getenv("B3_NIGHT_TEXLOG"); return e && *e == '2'; }();   // 2: every texture drawn
    if (!all && night_tunnel.enclosed <= 0.5f) return;
    for (std::size_t i = first; i < out.size(); ++i) {
        const auto it = night_texinfo.find(out[i].texture);
        if (it == night_texinfo.end() || !night_texlogged.insert(it->second.name).second) continue;
        std::fprintf(stderr, "[b3night] texlog %s tex %s lum %.3f sat %.3f bright %.3f enclosed %.2f\n", route_region(route).c_str(),
                     it->second.name.c_str(), double(it->second.lum), double(it->second.sat), double(it->second.bright), double(night_tunnel.enclosed));
    }
}

// nightlights (§17.2): the tunnel signal. The view's car (the players' car nearest the camera, within 80 m, with its 9
// SH coefficients) and its sky_open (carscene.cpp: the upward share of its irradiance, ~1 open, ~0.2 in a tunnel);
// enclosed = the rays' cover (night_cover: the logs showed sky_open alone also low under a shop's arcade on an open
// street) x the 60-degree ring's (nightfix: an open-sided deck is not a tunnel), cast while sky_open < 0.6, smoothed
// exponentially over tau seconds of the world clock (a jump or
// the first frame takes it at once). $B3_NIGHT_SKYLOG=1 logs it each frame (the numbers were set from such logs).
void View::night_tunnel_frame(const Frame& f, b3gfx::FrameParams& fp) const {
    b3gfx::FrameParams::Night& n = fp.night;
    n.tunnel[0] = std::clamp(night.tunnel[3], 0.0f, 1.0f);
    n.tunnel[1] = std::max(night.tunnel[4], 0.0f);
    n.tunnel[2] = std::max(night.tunnel[5], n.tunnel[1] + 1.0f);
    const float* eye = f.cam.m.r[3];
    const Vehicle* car = nullptr;
    float best = 80.0f * 80.0f;
    for (const Vehicle& v : f.vehicles) {
        if (v.player < 0 || v.sh_count < 9 || v.kind != VehicleKind::kRaceCar) continue;
        float d = 0;
        for (int k = 0; k < 3; ++k) d += (v.main.r[3][k] - eye[k]) * (v.main.r[3][k] - eye[k]);
        if (d < best) { best = d; car = &v; }
    }
    const float sky_open = car && car->sh[0] > 1e-3f ? std::clamp((car->sh[1] / car->sh[0] - 0.15f) / 0.35f, 0.0f, 1.0f) : 1.0f;
    const float lo = night.tunnel[0], hi = std::max(night.tunnel[1], night.tunnel[0] + 1e-3f);
    const float u = std::clamp((sky_open - lo) / (hi - lo), 0.0f, 1.0f);
    // the second signal (the logs: the car's sky_open alone also drops to 0.05 under a shop's arcade on an open street,
    // EU/P1_V2): the share of the rays from the car that hit the track's geometry, at least 7 of 9 for a tunnel.
    // nightfix: sky_open only gates the rays (below 0.6); the value is the geometry's: the roof rays x the 60-degree ring
    // (hits within 14 m, at least 7 of 8 for a tunnel). The logs (AS/C2_V1): in HK's tunnel sky_open jumps 0..0.37 from
    // frame to frame (the tunnel's light pulsed 0.47..0.85 with it) while cover and the ring stay at 1; under the
    // open-sided lattice deck before it the roof rays flicker 0.22..0.67 and the ring holds 0.38..0.62: night, steadily.
    float wide = 0.0f;
    const auto rt0 = std::chrono::steady_clock::now();
    const float cover = car && sky_open < 0.6f ? night_cover(car->main.r[3], &wide) : 0.0f;
    const double rays_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - rt0).count();
    const float cu = std::clamp((cover - 0.6f) / 0.3f, 0.0f, 1.0f);
    const float wu = std::clamp((wide - 0.75f) / 0.2f, 0.0f, 1.0f);
    const float target = cu * cu * (3.0f - 2.0f * cu) * wu * wu * (3.0f - 2.0f * wu);
    (void)u;
    NightTunnel& t = night_tunnel;
    const float dt = f.fx.now - t.now;
    if (!t.valid || dt < 0.0f || dt > 1.0f) t.enclosed = target;
    else if (dt > 0.0f) t.enclosed += (target - t.enclosed) * (1.0f - std::exp(-dt / std::max(night.tunnel[2], 0.01f)));
    t.now = f.fx.now;
    t.valid = true;
    n.enclosed = n.tunnel[0] > 0 ? t.enclosed : 0.0f;
    static const bool log = [] { const char* e = std::getenv("B3_NIGHT_SKYLOG"); return e && *e && *e != '0'; }();
    if (log)
        std::fprintf(stderr, "[b3night] skylog tick %u now %.3f car %s pos %.1f %.1f %.1f sky_open %.3f enclosed %.3f lamps %d cover %.2f wide %.2f rays_us %.1f\n",
                     unsigned(f.tick), double(f.fx.now), car ? (car->player == 0 ? "p1" : "p2") : "-", car ? double(car->main.r[3][0]) : 0.0,
                     car ? double(car->main.r[3][1]) : 0.0, car ? double(car->main.r[3][2]) : 0.0, double(sky_open), double(n.enclosed), n.lamps, double(cover), double(wide), rays_us);
}

// SUN SHADOWS (remaster-features §20): the box's half side and depth (m), how far ahead of the car its centre sits (m),
// the lowest light used (degrees: under 5 nothing is cast; below 12 the light is raised to 12, so the shadows stay a
// few metres long at sunset) and the darkening at full shadow by a bright key light
constexpr float kSunBoxHalf = 30.0f, kSunBoxDepth = 80.0f, kSunBoxAhead = 10.0f, kSunMinElev = 5.0f, kSunLowElev = 12.0f;
constexpr float kSunShadowDark = 0.62f;   // the colour x 0.38 at full shadow: about as dark as the track's baked shadows

// the share of sky a vehicle sees (b3view's car scene: CarParams::sky_open), from its SH or, for traffic and wrecks
// (L00 only), the nearest race car's
static float vehicle_sky_open(const Vehicle& v, const std::vector<Vehicle>& all) {
    const Vehicle* s = &v;
    if (v.sh_count < 9) {
        float best = 1e30f;
        s = nullptr;
        for (const auto& u : all) {
            if (u.sh_count < 9 || !(u.sh[0] > 0)) continue;
            float d2 = 0;
            for (int k = 0; k < 3; ++k) d2 += (u.main.r[3][k] - v.main.r[3][k]) * (u.main.r[3][k] - v.main.r[3][k]);
            if (d2 < best) { best = d2; s = &u; }
        }
        if (!s) return 1.0f;
    }
    return s->sh[0] > 1e-3f ? std::clamp((s->sh[1] / s->sh[0] - 0.15f) / 0.35f, 0.0f, 1.0f) : 1.0f;
}

bool View::sun_shadow_box(const Frame& f, const float to_light[3], b3gfx::FrameParams::SunShadow& out) const {
    if (!sun_shadows || !f.cam.valid) return false;
    float L[3] = {to_light[0], to_light[1], to_light[2]};
    const float ll = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    if (!(ll > 1e-6f)) return false;
    for (float& c : L) c /= ll;
    const float deg = 3.14159265f / 180.0f;
    if (std::asin(std::clamp(L[1], -1.0f, 1.0f)) < kSunMinElev * deg) return false;
    if (std::asin(std::clamp(L[1], -1.0f, 1.0f)) < kSunLowElev * deg) {   // the bearing kept, raised to 12 degrees
        const float h = std::sqrt(L[0] * L[0] + L[2] * L[2]);
        L[0] = std::cos(kSunLowElev * deg) * L[0] / h;
        L[2] = std::cos(kSunLowElev * deg) * L[2] / h;
        L[1] = std::sin(kSunLowElev * deg);
    }
    // the view's car: the env map's (the game follows it), else the race car nearest to a point just ahead of the camera
    const Vehicle* car = nullptr;
    if (f.env_car != 0)
        for (const Vehicle& v : f.vehicles) if (v.kind == VehicleKind::kRaceCar && v.id == f.env_car) car = &v;
    if (!car) {
        float best = 1e30f;
        const float* e = f.cam.m.r[3];
        const float* a = f.cam.m.r[2];
        for (const Vehicle& v : f.vehicles) {
            if (v.kind != VehicleKind::kRaceCar) continue;
            float d2 = 0;
            for (int k = 0; k < 3; ++k) d2 += (v.main.r[3][k] - e[k] - 6.0f * a[k]) * (v.main.r[3][k] - e[k] - 6.0f * a[k]);
            if (d2 < best) { best = d2; car = &v; }
        }
        if (!car || best > 40.0f * 40.0f) return false;
    }
    // the centre a little ahead: along the camera's view (a free camera, photo mode: the car's own heading, so turning the
    // camera does not move the shadows)
    const float* fw = free_view ? car->main.r[2] : f.cam.m.r[2];
    const float fl = std::sqrt(fw[0] * fw[0] + fw[2] * fw[2]);
    float c[3] = {car->main.r[3][0], car->main.r[3][1], car->main.r[3][2]};
    if (fl > 1e-4f) { c[0] += kSunBoxAhead * fw[0] / fl; c[2] += kSunBoxAhead * fw[2] / fl; }
    // the light's frame: R, U across the map, L towards the light
    const bool high = std::fabs(L[1]) > 0.99f;   // (the sun overhead: +Z as the reference instead of +Y)
    const float ref[3] = {0.0f, high ? 0.0f : 1.0f, high ? 1.0f : 0.0f};
    float R[3] = {ref[1] * L[2] - ref[2] * L[1], ref[2] * L[0] - ref[0] * L[2], ref[0] * L[1] - ref[1] * L[0]};
    const float rl = std::sqrt(R[0] * R[0] + R[1] * R[1] + R[2] * R[2]);
    for (float& x : R) x /= rl;
    const float U[3] = {L[1] * R[2] - L[2] * R[1], L[2] * R[0] - L[0] * R[2], L[0] * R[1] - L[1] * R[0]};
    // the centre snapped to whole texels across the map, so the shadows' edges do not crawl as the box moves
    const int side = std::clamp(sun_shadow_size, 256, 4096);
    const float texel = 2.0f * kSunBoxHalf / float(side);
    const float cx = std::floor((R[0] * c[0] + R[1] * c[1] + R[2] * c[2]) / texel + 0.5f) * texel;
    const float cy = std::floor((U[0] * c[0] + U[1] * c[1] + U[2] * c[2]) / texel + 0.5f) * texel;
    const float cz = L[0] * c[0] + L[1] * c[1] + L[2] * c[2];
    const float h = kSunBoxHalf, zr = 0.5f * kSunBoxDepth;
    float* m = out.view_proj;   // column-major: x = (p.R - cx) / h, y = (p.U - cy) / h, z = (cz + zr - p.L) / (2 zr)
    for (int k = 0; k < 16; ++k) m[k] = 0.0f;
    for (int k = 0; k < 3; ++k) {
        m[4 * k + 0] = R[k] / h;
        m[4 * k + 1] = U[k] / h;
        m[4 * k + 2] = -L[k] / (2.0f * zr);
    }
    m[12] = -cx / h;
    m[13] = -cy / h;
    m[14] = (cz + zr) / (2.0f * zr);
    m[15] = 1.0f;
    static const bool trace = std::getenv("B3_SUN_SHADOW_TRACE") != nullptr;   // diagnostics: the view car's sky share by tick
    static std::size_t trace_tick = ~std::size_t(0);
    if (trace && f.tick != trace_tick) {
        trace_tick = f.tick;
        std::printf("[b3sun] tick %zu car %08x at (%.1f %.1f %.1f) sky_open %.2f light (%.2f %.2f %.2f)\n", f.tick, unsigned(car->id),
                    car->main.r[3][0], car->main.r[3][1], car->main.r[3][2], vehicle_sky_open(*car, f.vehicles), L[0], L[1], L[2]);
    }
    out.on = true;
    out.props = prop_shadows;   // propshadow1
    out.depth = kSunBoxDepth;
    out.rise = L[1];
    out.size = side;
    out.filter = sun_shadow_filter;
    return true;
}

// weather (§38): by day in the rain the sky dome and the backdrops (whose pictures carry the painted sky and its clouds)
// pulled towards the overcast's grey before the grade desaturates them (they write depth, so the grade takes them as world)
void View::rain_sky_tint(const Frame& f, b3gfx::DrawItem& it) const {
    if (!rain_on(f) || night_on(f)) return;
    it.overcast = weather_look.sky_grey;
}

// weather (§38): each opaque track draw's share under a roof (DrawItem::dry), from up to 24 of its triangles' centres
// (rain_sheltered: the cover grid, cached), once per route: its road stays that much dry in the rain
void View::rain_dry_build() {
    rain_dry_built = true;
    const auto t0 = std::chrono::steady_clock::now();
    std::size_t n = 0;
    for (b3view::Scene* sc : {&hi, &lo}) {
        const b3asset::Model& m = sc->model;
        for (std::size_t i = 0; i < m.draws.size() && i + sc->item_base < sc->items.size(); ++i) {
            const b3asset::DrawRange& d = m.draws[i];
            if (d.blend == b3asset::DrawRange::kBlend || d.index_count < 3) continue;
            const float* T = d.transform >= 0 && std::size_t(d.transform) < m.transforms.size() ? m.transforms[std::size_t(d.transform)].data() : nullptr;
            const std::uint32_t tris = d.index_count / 3, step = std::max<std::uint32_t>(1, tris / 24);
            int under = 0, seen = 0;
            for (std::uint32_t k = 0; k < tris && seen < 24; k += step) {
                const std::uint32_t i0 = d.first_index + 3 * k;
                if (i0 + 2 >= m.indices.size()) break;
                if (m.indices[i0] >= m.vertices.size() || m.indices[i0 + 1] >= m.vertices.size() || m.indices[i0 + 2] >= m.vertices.size())
                    continue;   // (as the other track readers: an index past the vertices)
                float c[3] = {0, 0, 0};
                for (int v = 0; v < 3; ++v) {
                    const float* p = m.vertices[m.indices[i0 + std::uint32_t(v)]].pos;
                    for (int a = 0; a < 3; ++a) c[a] += (T ? T[a] * p[0] + T[4 + a] * p[1] + T[8 + a] * p[2] + T[12 + a] : p[a]) / 3.0f;
                }
                c[1] += 0.6f;   // (above the surface itself)
                under += rain_sheltered(c) ? 1 : 0;
                ++seen;
            }
            sc->items[i + sc->item_base].dry = seen ? float(under) / float(seen) : 0.0f;
            if (under) ++n;
        }
    }
    std::fprintf(stderr, "[b3rain] dry draws %s: %zu under a roof, %.1f ms\n", route.c_str(), n,
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}

// weather (§38): the followed race car's speed along its heading (m/s; its wheels, VehicleParams +0x4E8 x ~0.33 m) and
// that heading: the nearest race car with its parts captured within 15 m of the camera; 0 none
float View::follow_speed(const Frame& f, float dir[3]) const {
    const float* eye = f.cam.m.r[3];
    float best = 15.0f * 15.0f, speed = 0.0f;
    for (int k = 0; k < 3; ++k) dir[k] = 0.0f;
    for (const Vehicle& v : f.vehicles) {
        if (!v.parts || v.kind != VehicleKind::kRaceCar) continue;
        float d2 = 0, ws = 0;
        for (int k = 0; k < 3; ++k) d2 += (v.main.r[3][k] - eye[k]) * (v.main.r[3][k] - eye[k]);
        if (d2 >= best) continue;
        best = d2;
        for (int j = 0; j < std::min(v.nwheel, 6); ++j) ws = std::max(ws, std::fabs(v.wheel_speed[j]) * 0.33f);
        speed = ws;
        for (int k = 0; k < 3; ++k) dir[k] = v.main.r[2][k];
    }
    return speed;
}

// weather (§38): the lens drops (rain.h lens_drops) on the race's own views, not a free camera (photo mode); fewer and
// none inside a tunnel (`enclosed`)
void View::lens_drops_frame(const Frame& f, b3gfx::FrameParams& fp, float enclosed, float aspect_in) const {
    fp.drops = b3gfx::FrameParams::Drops{};
    if (free_view || !lens_drops_on) return;
    float dir[3];
    const float speed = follow_speed(f, dir);
    const float aspect = aspect_in > 0.1f ? aspect_in : 4.0f / 3.0f;
    fp.drops.aspect = aspect;
    fp.drops.count = lens_drops((double(f.tick) + double(f.alpha)) / 29.97, 1.0f - std::clamp(enclosed, 0.0f, 1.0f), speed, aspect,
                                fp.drops.drop, b3gfx::FrameParams::Drops::kMax);
}

// weather (§38, rain_audio.h): the rain's sound follows the race on screen: on while it rains and the race's clock runs
// (the in-game pause stops it: VBlanks and pushes go on), the followed car's speed, the tunnel's share (rain_frame's
// signal); the volume: RAIN VOLUME (View::rain_volume, 0..2; $B3_RAIN_VOLUME 0-200 wins) x the game's EFFECTS volume
// (NativeAudio, from native_play)
float View::rain_audio_volume() const {   // rainfe: RAIN VOLUME as the sound takes it ($B3_RAIN_VOLUME 0-200 wins)
    static const float env = [] { const char* e = std::getenv("B3_RAIN_VOLUME"); return e && *e ? std::clamp(float(std::atof(e)) / 100.0f, 0.0f, 2.0f) : -1.0f; }();
    return env >= 0.0f ? env : std::clamp(rain_volume, 0.0f, 2.0f);
}

void View::rain_audio_frame(const Frame& f, const b3gfx::FrameParams& fp) const {
    const float volume = rain_audio_volume();
    const auto wall = std::chrono::steady_clock::now();
    if (f.fx.now != rain_clock_now) {
        rain_clock_now = f.fx.now;
        rain_clock_wall = wall;
    }
    const bool running = std::chrono::duration<double>(wall - rain_clock_wall).count() < 0.3;
    float dir[3];
    const float speed = rain_on(f) ? follow_speed(f, dir) : 0.0f;
    // the tunnel's share, or under a bridge / overpass (the camera's cover) most of it: the bed muffled, no drips
    const float enclosed = std::clamp(std::max(fp.night.enclosed, 0.7f * (rain_on(f) ? rain_cover.v : 0.0f)), 0.0f, 1.0f);
    const PhotoMode& pm = PhotoMode::instance();   // rainpause: the pause menu (native_play's, b3r_set_paused), photo mode
    const bool on = RainAudio::sounding(rain_on(f), running, pm.game_paused(), pm.active());
    NativeAudio::instance().set_rain(on, speed, enclosed, volume);
    static const bool log = [] { const char* e = std::getenv("B3_RAIN_AUDIO_LOG"); return e && *e && *e != '0'; }();
    const float state[3] = {on ? 1.0f : 0.0f, rain_cover.v, enclosed};
    if (log && (state[0] != rain_log_last[0] || std::fabs(state[1] - rain_log_last[1]) > 0.1f || std::fabs(state[2] - rain_log_last[2]) > 0.1f ||
                (state[1] == 0.0f) != (rain_log_last[1] == 0.0f))) {
        std::memcpy(rain_log_last, state, sizeof state);
        std::fprintf(stderr, "[b3rain] audio tick %zu: %s, cover %.2f, tunnel %.2f, enclosed %.2f (bed %.0f %%, drips %.0f %%), speed %.0f m/s\n", f.tick,
                     on ? "on" : "off", double(rain_cover.v), double(fp.night.enclosed), double(enclosed), on ? 100.0 * (1.0 - 0.45 * double(enclosed)) : 0.0,
                     on ? 100.0 * (1.0 - double(enclosed)) : 0.0, double(speed));
    }
}

// weather (§38): the camera's cover: five vertical rays (the camera and 3 m round it, 0.5 m above) against the cover grid
// (rain_sheltered, cached per 2 m cell), smoothed: ~0.3 s to cover, ~0.5 s back out
float View::rain_cover_frame(const Frame& f) const {
    const float* eye = f.cam.m.r[3];
    static const float off[5][2] = {{0, 0}, {3, 0}, {-3, 0}, {0, 3}, {0, -3}};
    int hits = 0;
    for (const auto& o : off) {
        const float p[3] = {eye[0] + o[0], eye[1] + 0.5f, eye[2] + o[1]};
        hits += rain_sheltered(p) ? 1 : 0;
    }
    const float target = std::clamp((float(hits) / 5.0f - 0.3f) / 0.5f, 0.0f, 1.0f);
    RainCover& c = rain_cover;
    const float dt = f.fx.now - c.now;
    if (!c.valid || dt < 0.0f || dt > 1.0f) c.v = target;
    else if (dt > 0.0f) c.v += (target - c.v) * (1.0f - std::exp(-dt / (target > c.v ? 0.3f : 0.5f)));
    c.now = f.fx.now;
    c.valid = true;
    return c.v;
}

void View::rain_frame(const Frame& f, float aspect, b3gfx::FrameParams& fp) const {
    if (!rain_on(f)) return;
    const WeatherLook& w = weather_look;
    b3gfx::FrameParams::Night& n = fp.night;
    if (!night_on(f)) {   // by day: the tunnel signal (night_tunnel_frame: the night has it already), for the fades below
        b3gfx::FrameParams::Night day{};
        std::swap(day, n);
        night_tunnel_frame(f, fp);
        day.enclosed = n.enclosed;
        std::memcpy(day.tunnel, n.tunnel, sizeof day.tunnel);
        std::swap(day, n);
    }
    // inside a tunnel the rain's fog and (by day) its grade fade out: the tunnel is lit, its air dry
    const float enc = std::clamp(n.enclosed, 0.0f, 1.0f);
    const float cover = rain_cover_frame(f);   // (the lens drops and the sound: any roof, a bridge's too)
    const float fog_orig[3] = {fp.fog_colour[0], fp.fog_colour[1], fp.fog_colour[2]};
    if (fp.fog_colour[3] > 0) {   // the fog nearer and thicker (the game's curve: z = shift + scale / w, so scale / k)
        fp.fog[0] /= 1.0f + (std::max(w.fog_near, 1.0f) - 1.0f) * (1.0f - enc);
        fp.fog[2] = fp.fog[2] + (std::max(fp.fog[2], w.fog_max) - fp.fog[2]) * (1.0f - enc);
    }
    // the wet road: the full-screen specular mirrors the sky's colour (ROAD SHINE off: none), no sun sheen
    fp.surface.wet = w.wet;
    const bool shine = fp.specular[0] > 0 || fp.specular[1] > 0 || fp.specular[2] > 0;
    for (int k = 0; k < 3; ++k) fp.specular[k] = shine ? (night_on(f) ? w.night_reflect[k] : w.reflect[k]) : 0.0f;
    if (night_on(f)) {   // night rain: the night's grade, the moon and the stars behind the clouds
        n.stars = 0;
        n.cloud = 0;
        n.halo = 0;
        for (int k = 0; k < 3; ++k) {
            n.moon_colour[k] = 0;
            n.moon_light[k] *= w.night_moonlight;
            fp.key_light.colour[k] *= w.night_moonlight;
            n.zenith[k] = w.night_sky[k];
            n.horizon[k] = w.night_horizon[k];
            n.sky[k] *= 0.5f;
        }
        fp.sun_colour[3] = 0;
        if (fp.fog_colour[3] > 0)
            for (int k = 0; k < 3; ++k)
                fp.fog_colour[k] = std::pow(n.horizon[k] / n.pivot, 1.0f / std::max(n.gamma, 0.1f)) * n.pivot / std::max(n.world[k], 1e-3f);
        lens_drops_frame(f, fp, std::max(enc, cover), aspect);
        return;
    }
    const b3gfx::FrameParams::Night tun = n;   // (the tunnel signal, kept)
    n = b3gfx::FrameParams::Night{};   // by day: the night grade's pass with the overcast's values, none of its lights
    n.enclosed = tun.enclosed;   // (the grade fades out over the whole world inside a tunnel: no spot pass by day)
    std::memcpy(n.tunnel, tun.tunnel, sizeof n.tunnel);
    n.on = true;
    n.lit = false;
    std::memcpy(n.world, w.world, sizeof n.world);
    n.sat = w.sat;
    std::memcpy(n.sky, w.sky, sizeof n.sky);
    std::memcpy(n.zenith, w.zenith, sizeof n.zenith);
    std::memcpy(n.horizon, w.horizon, sizeof n.horizon);
    std::memcpy(n.car, w.car, sizeof n.car);
    n.flat = w.flat;
    n.lamps = 0;
    n.streets = 0;
    fp.surface.sheen = 0;   // (SURFACE DETAIL's sun sheen: no sun)
    lens_drops_frame(f, fp, std::max(enc, cover), aspect);
    for (int k = 0; k < 3; ++k) {
        fp.key_light.colour[k] = 0;   // no sun: no shadows from it, no glint
        if (fp.fog_colour[3] > 0) fp.fog_colour[k] = w.horizon[k] / std::max(w.world[k], 1e-3f) * (1.0f - enc) + fog_orig[k] * enc;
    }
    fp.sun_colour[3] = 0;   // the cars' env map: no sun blob
}

void View::post_frame(const Frame& f, float display_aspect, b3gfx::FrameParams& fp) const {
    // TUNNEL LIGHTS (§35.4): the fittings glow by day (xboxglow §35.5: Xbox, its own gain)
    fp.day_glow = tunnel_lights && day_route && !night_on(f) ? (tunnel_lights >= 2 ? xbox_glow_gain() : 1.0f) : 0.0f;
    fp.day_glow_mode = std::clamp(tunnel_lights, 1, 2);           // xboxglow (§35.5): Glow, Xbox
    fp.mirror_x = f.mirror;   // gameopts (MIRROR MODE): b3gfx shows the view's 3D picture reversed
    static const bool surface_env = surface_env_set();
    static const b3gfx::FrameParams::Surface surface = surface_from_env();
    fp.surface = surface;
    if (!surface_env) {   // the settings rows (the environment's prototype values win when set)
        fp.surface = b3gfx::FrameParams::Surface{};
        if (surface_detail) { fp.surface.relief = 3.0f; fp.surface.sheen = 0.35f; }
        if (ambient_occlusion) fp.surface.ao = 0.8f;
    }
    blur_frame(f, fp);
    bloom_frame(f, fp);
    // the key light (FrameParams::KeyLight): the ENVIRO sun by day
    std::memcpy(fp.key_light.dir, fp.to_sun, sizeof fp.key_light.dir);
    for (int k = 0; k < 3; ++k) fp.key_light.colour[k] = fp.sun_colour[3] > 0 ? std::min(fp.sun_colour[k], 1.0f) : 0.5f;
    fp.key_light.night = false;
    fp.night = b3gfx::FrameParams::Night{};   // night mode (remaster-features §17): the race's frames only (not the front end's)
    if (night_on(f)) {
        b3gfx::FrameParams::Night& n = fp.night;
        n.on = true;
        std::memcpy(n.world, night.world, sizeof n.world);
        n.sat = night.sat;
        n.gamma = night.gamma;
        n.pivot = night.pivot;
        std::memcpy(n.sky, night.sky, sizeof n.sky);
        std::memcpy(n.zenith, night.zenith, sizeof n.zenith);
        std::memcpy(n.horizon, night.horizon, sizeof n.horizon);
        n.stars = night.stars;
        n.cloud = night.cloud;
        // the moon at the sun's bearing: every light from the route's sun comes from it (the cars' glint, the track's
        // specular, the relief), so to_sun is the moon's direction from here on
        moon_direction(fp.to_sun, n.moon_dir);
        std::memcpy(fp.to_sun, n.moon_dir, sizeof fp.to_sun);
        std::memcpy(n.car, night.car, sizeof n.car);
        n.flat = std::clamp(night.flat, 0.0f, 0.9f);
        if (night.moon) {
            std::memcpy(n.moon_colour, night.moon_disc, sizeof n.moon_colour);
            n.moon_size = night.moon_size;
            n.halo = night.halo;
            std::memcpy(n.moon_light, night.moonlight, sizeof n.moon_light);
            n.facing = night.facing;
            if (fp.surface.relief <= 0) fp.surface.relief = night.relief;   // the moon lights the track's relief
            if (fp.surface.sheen <= 0) fp.surface.sheen = 0.12f;              // and a glare on the asphalt towards it
        } else {
            n.cloud = 0;
            n.car[2] = 0;
        }
        // the track's full-screen specular (ROAD SHINE): the moon's colour instead of the sun's
        for (int k = 0; k < 3; ++k)
            if (fp.specular[0] > 0 || fp.specular[1] > 0 || fp.specular[2] > 0) fp.specular[k] = night.moon ? 0.3f * night.moonlight[k] : 0.0f;
        // the cars' env map: the moon where the sun's blob was (car.frag's glint follows it); none with the moon off
        for (int k = 0; k < 3; ++k) fp.sun_colour[k] = std::min(1.0f, 0.8f * night.moon_disc[k]);
        fp.sun_colour[3] = night.moon && fp.sun_colour[3] > 0 ? 1.0f : 0.0f;
        // the fog: a colour that the grade turns into the night sky's horizon (distant scenery fades into the sky)
        if (fp.fog_colour[3] > 0)
            for (int k = 0; k < 3; ++k)
                fp.fog_colour[k] = std::pow(night.horizon[k] / night.pivot, 1.0f / std::max(night.gamma, 0.1f)) * night.pivot / std::max(night.world[k], 1e-3f);
        std::memcpy(fp.key_light.dir, n.moon_dir, sizeof fp.key_light.dir);
        std::memcpy(fp.key_light.colour, n.moon_light, sizeof fp.key_light.colour);
        fp.key_light.night = true;
        std::memcpy(n.fixture, night.fixture, sizeof n.fixture);
        night_lamps(f, fp);
        night_street_frame(f, fp);
        night_board_frame(f, fp);   // nightfix: the billboards' floodlights
        night_tunnel_frame(f, fp);
    }
    rain_frame(f, display_aspect, fp);   // weather (§38): the rain's grade, by day and at night
    rain_audio_frame(f, fp);             // and its sound's state (NativeAudio)
    // SUN SHADOWS (§19): the box around the view's car towards the key light, darker under a brighter light (an overcast
    // route a faint shadow); none without the light (night with the moon off)
    fp.sun_shadow = b3gfx::FrameParams::SunShadow{};
    if (sun_shadows && sky.has_enviro && !rain_on(f)) {   // (weather: no sun in the rain)
        const float* kc = fp.key_light.colour;
        const float k = std::max(kc[0], std::max(kc[1], kc[2]));
        static const float dark = [] { const char* e = std::getenv("B3_SUN_SHADOW_STRENGTH"); return e && *e ? float(std::atof(e)) : kSunShadowDark; }();
        b3gfx::FrameParams::SunShadow s;
        if (k > 0.0f && dark > 0.0f && sun_shadow_box(f, fp.key_light.dir, s)) {
            s.strength = std::clamp(dark * std::clamp(k * 1.25f, 0.35f, 1.0f), 0.0f, 1.0f);
            static const float lit = [] { const char* e = std::getenv("B3_SUN_SHADOW_LIT"); return e && *e ? float(std::atof(e)) : 0.0f; }();
            s.lit = lit > 0.0f ? lit : road_light;   // the route's open road (B3_SUN_SHADOW_LIT: a fixed level instead)
            fp.sun_shadow = s;
        }
    }
    if (std::getenv("B3_FRAME_UNITS")) fp.present_scale = 1.0f;   // diagnostics: the output shows the GS frame's bytes, not the display
    fp.sun_sprites.clear();
    if (!show_sun || !sky.has_enviro || night_on(f) || rain_on(f)) return;   // night mode, rain: no sun corona or lens flare
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
    auto& cf = cars[std::size_t(file)];
    if (cf.full().empty()) return nullptr;   // (an extra car's file that cannot be read now)
    const int block = b3view::car_lod_block(cf.full(), lod);
    auto& slot = car_models[{file, paint, block}];
    if (!thumb_model_) thumb_only_models.erase({file, paint, block});   // paint3: the scene's (kept)
    else if (!slot) thumb_only_models.insert({file, paint, block});     // paint3: a thumbnail's only (thumb_models_release)
    if (slot) return slot->scene.items.empty() ? nullptr : slot.get();
    slot = std::make_unique<b3view::CarModel>();
    std::string why;
    const auto t0 = std::chrono::steady_clock::now();
    if (!b3view::load_car_model(dev, cf.bytes, paint, block, *slot, &why)) {
        std::fprintf(stderr, "car %s: %s\n", cf.path.c_str(), why.c_str());
        return nullptr;
    }
    if (thumb_model_ && std::getenv("B3_THUMB_LOG"))   // paint3 diagnostics
        std::fprintf(stderr, "car thumbnails: model %s palette %d loaded (%.1f ms)\n", cf.path.c_str(), paint,
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    return slot.get();
}

View::DrawPaint View::draw_paint(b3gfx::Device& dev, int file, const Vehicle& v) {
    DrawPaint d;
    // carpaint: a local player's own paint. A custom colour is drawn over the reference paint's texture (the brightest: the
    // most shading levels; an HD pack's replacement of it too); decals off with the game's paint keeps the game's texture
    // and fills the livery with that paint's colour
    const int who = v.kind != VehicleKind::kRaceCar ? -1 : v.player >= 0 ? v.player
                  : paint_frontend && v.id == paint_fe_car ? paint_fe_player : -1;   // padpaint: each car its own player's
    d.own = who >= 0 && who < 2 && car_paint[who].on() ? &car_paint[who] : nullptr;
    d.pm = d.own ? paint_map(dev, file) : nullptr;
    d.paint = d.pm && d.own->custom ? d.pm->map.ref : v.paint;
    return d;
}

int View::preload_vehicles(b3gfx::Device& dev, const std::vector<Vehicle>& vehicles, const std::vector<ModelHead>& heads,
                           double budget_ms, const std::function<bool()>& go, std::string& stop) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto spent = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    const std::size_t before = car_models.size();
    stop.clear();
    // hdcache: with an HD pack, every vehicle file the race loaded (the race cars and the route's whole traffic set) has its
    // replacements converted first, all its paints, on threads (HdTextures::prefetch_data; what the disk cache holds is
    // skipped): a traffic vehicle's first draw mid-race then finds its textures ready (it converted them there: 25-40 ms)
    hd_prefetched = 0;
    hd_prefetch_cut = false;
    static const bool prepare_off = [] { const char* e = std::getenv("B3_HD_PREPARE"); return e && e[0] == '0' && !e[1]; }();
    if (b3asset::hd_textures() && !prepare_off) {   // ($B3_HD_PREPARE=0: none, each at its first draw as before)
        std::vector<int> files;
        for (const ModelHead& h : heads) {
            const int f = car_file(h);
            if (f >= 0 && std::find(files.begin(), files.end(), f) == files.end()) files.push_back(f);
        }
        std::vector<std::pair<std::string, b3asset::Bytes>> data;
        for (const int f : files) {
            auto& bytes = cars[std::size_t(f)].full();
            if (!bytes.empty()) data.emplace_back(cars[std::size_t(f)].path, b3asset::Bytes{bytes.data(), bytes.size()});
        }
        if (go()) {   // (Fable) within the wait and its budget: what it does not reach converts at its first draw, cached
            const auto th = std::chrono::steady_clock::now();
            hd_prefetch_mem[0] = b3port::process_memory();
            bool cut = false;
            b3asset::hd_prefetch_data(data, go, std::max(1.0, budget_ms - spent()), &cut);
            hd_prefetch_mem[1] = b3port::process_memory();
            hd_prefetched = int(data.size());
            hd_prefetch_cut = cut;
            hd_prefetch_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - th).count();
        }
    }
    // the race cars first (the player's and the rivals': on screen from the first frame), then the rest (traffic)
    for (const bool race_cars : {true, false})
        for (const Vehicle& v : vehicles) {
            if ((v.kind == VehicleKind::kRaceCar) != race_cars) continue;
            if (v.model < 0 || std::size_t(v.model) >= heads.size()) continue;
            const int file = car_file(heads[std::size_t(v.model)]);
            if (file < 0) continue;
            const int paint = draw_paint(dev, file, v).paint;
            for (int lod = 0; lod <= 4; ++lod) {   // hdcache: 3 and 4 too (the far blocks: a car first seen far off draws them)
                if (!go()) { stop = "the wait ended"; return int(car_models.size() - before); }
                if (spent() > budget_ms) { stop = "the time budget"; return int(car_models.size() - before); }
                car_model(dev, file, paint, lod);
            }
        }
    return int(car_models.size() - before);
}

CarPaint car_paint_env(int p, CarPaint base) {
    if (const char* e = std::getenv(p == 0 ? "B3_PAINT" : "B3_PAINT_P2")) {
        std::uint32_t rgb = 0;
        if (!std::strcmp(e, "game")) base.custom = false;
        else if (b3asset::parse_rgb(e, rgb)) { base.custom = true; base.rgb = rgb; }
    }
    if (const char* e = std::getenv("B3_PAINT_SECOND")) {
        std::uint32_t rgb = 0;
        if (b3asset::parse_rgb(e, rgb)) { base.second = true; base.rgb2 = rgb; }
    }
    if (const char* e = std::getenv("B3_DECALS")) {
        if (!std::strcmp(e, "0")) base.decals = false;
        else if (!std::strcmp(e, "1")) base.decals = true;
    }
    if (const char* e = std::getenv("B3_PAINT_FINISH")) {   // padpaint
        if (!std::strcmp(e, "gloss")) base.finish = 0;
        else if (!std::strcmp(e, "metallic")) base.finish = 1;
        else if (!std::strcmp(e, "pearl")) base.finish = 2;
        else if (!std::strcmp(e, "chrome")) base.finish = 3;   // options3
    }
    return base;
}

const View::PaintMap* View::paint_map(b3gfx::Device& dev, int file) {
    if (file < 0 || std::size_t(file) >= cars.size()) return nullptr;
    auto it = paint_maps.find(file);
    if (it == paint_maps.end()) {   // the first need: the map made (padpaint: on a worker in the window)
        PaintMap& pm = paint_maps[file];
        pm.pending = true;
        auto make = [&pm, bytes = cars[std::size_t(file)].full()]() {
            std::string why;
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = b3asset::car_paint_map(b3asset::Bytes{bytes.data(), bytes.size()}, pm.map, &why);
            pm.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            return std::pair<bool, std::string>(ok, why);
        };
        pm.job = std::async(async_paint ? std::launch::async : std::launch::deferred, std::move(make));
        it = paint_maps.find(file);
    }
    PaintMap& pm = it->second;
    if (pm.pending) {
        if (async_paint && pm.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;   // the game's paint meanwhile
        const auto [ok, why] = pm.job.get();
        pm.pending = false;
        pm.ok = ok;
        if (!ok) std::fprintf(stderr, "car paint: %s: %s (drawn as the game's)\n", cars[std::size_t(file)].path.c_str(), why.c_str());
        else
            std::fprintf(stderr, "car paint: %s: %d paints, reference %d, %d decal islands, %d parts kept (%.0f ms%s)\n",
                         cars[std::size_t(file)].path.c_str(), pm.map.palettes, pm.map.ref, pm.map.decal_islands, pm.map.parts_kept, pm.ms,
                         async_paint ? ", on a worker" : "");
    }
    if (!pm.ok) return nullptr;
    if (pm.tex == b3gfx::kNone) pm.tex = b3view::upload_texture(dev, {pm.map.mask});
    return &pm;
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

namespace { bool file_shadow(const std::vector<std::uint8_t>& b, Vehicle::Shadow& s); }   // below (fe2)

// reflect-tunnel: see view.h. kNone (the sky alone) when off, when CAR REFLECTIONS is Off, without the car or the
// capture's inputs, or when the game's switch D_00665F20 is clear.
std::uint32_t View::env_world_map(b3gfx::Device& dev, const Frame& f, const std::vector<ModelHead>& heads,
                                  const b3view::CarGlobals* globals) {
    if (!car_env_world || car_reflect == 0 || f.env_track == 0 || f.env_car == 0 || !sky.has_enviro) return b3gfx::kNone;
    const Vehicle* car = nullptr;
    for (const auto& v : f.vehicles) if (v.kind == VehicleKind::kRaceCar && v.id == f.env_car) car = &v;
    if (!car || car->model < 0 || std::size_t(car->model) >= heads.size()) return b3gfx::kNone;
    // reflect2: REFLECTION UPDATE every second frame: per view (its car), every other call returns the map drawn the call
    // before (env_slot is not stepped, so the next map goes to the other slot and the kept one stays as it is)
    EnvKeep* keep = nullptr;
    if (env_every_second) {
        for (EnvKeep& k : env_keep) if (!keep && k.car == f.env_car && k.calls) keep = &k;   // this view's
        for (EnvKeep& k : env_keep) if (!keep && !k.calls) keep = &k;                         // else a free one
        if (!keep) keep = &env_keep[0];
        if (keep->car != f.env_car || !keep->calls) *keep = EnvKeep{f.env_car};
        if (keep->calls++ % 2 == 1 && keep->map != b3gfx::kNone) return keep->map;
    }
    // BuildCustomCameraData's lEnvMapCentre: the car's matrix applied to the middle of its box (+0xE80 / +0xE90)
    Vehicle::Shadow box;
    const int file = car_file(heads[std::size_t(car->model)]);
    if (file < 0 || !file_shadow(cars[std::size_t(file)].full(), box)) return b3gfx::kNone;
    b3gfx::EnvPass ep;
    float mid[3];
    for (int k = 0; k < 3; ++k) mid[k] = 0.5f * (box.sup[k] + box.inf[k]);
    for (int k = 0; k < 3; ++k) ep.centre[k] = mid[0] * car->main.r[0][k] + mid[1] * car->main.r[1][k] + mid[2] * car->main.r[2][k] + car->main.r[3][k];
    const float up[3] = {0, 1, 0};
    b3view::frame_params(sky, ep.centre, up, false, false, ep.fp);
    ep.fp.present_scale = 1.0f;
    ep.zfar = env_far;   // reflect2: REFLECTION DISTANCE (100: the game's)
    ep.face = env_face;  // reflect2 D: REFLECTION RESOLUTION (128: the game's)
    // (diagnostics: B3_ENV_FACE_CYCLE=<n> switches 128 / 256 every n maps, as toggling the row live does: render_env's
    // targets are made again at the new size each time)
    if (static const int cyc = [] { const char* e = std::getenv("B3_ENV_FACE_CYCLE"); return e ? std::atoi(e) : 0; }(); cyc > 0) {
        static unsigned calls = 0;
        ep.face = (calls++ / unsigned(cyc)) % 2 ? 256 : 128;
    }
    if (lo_boxes.size() != lo.items.size()) {
        lo_boxes.assign(lo.items.size(), {1, 1, 1, 0, 0, 0});
        for (std::size_t k = 0; k < lo.items.size(); ++k) {
            const std::size_t d = k - lo.item_base;
            if (k < lo.item_base || d >= lo.model.draws.size()) continue;
            const auto& r = lo.model.draws[d];
            float a[3] = {1e30f, 1e30f, 1e30f}, b[3] = {-1e30f, -1e30f, -1e30f};
            bool got = false;
            for (std::uint32_t i = r.first_index; i < r.first_index + r.index_count && i < lo.model.indices.size(); ++i) {
                const std::uint32_t vi = lo.model.indices[i];
                if (vi >= lo.model.vertices.size()) continue;
                const float* p = lo.model.vertices[vi].pos;
                for (int c = 0; c < 3; ++c) { a[c] = std::min(a[c], p[c]); b[c] = std::max(b[c], p[c]); }
                got = true;
            }
            if (!got) continue;
            const float* M = lo.items[k].model;
            std::array<float, 6> w = {1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
            for (int c = 0; c < 8; ++c) {
                const float x = (c & 1) ? b[0] : a[0], y = (c & 2) ? b[1] : a[1], z = (c & 4) ? b[2] : a[2];
                const float q[3] = {M[0] * x + M[4] * y + M[8] * z + M[12], M[1] * x + M[5] * y + M[9] * z + M[13], M[2] * x + M[6] * y + M[10] * z + M[14]};
                for (int i = 0; i < 3; ++i) { w[std::size_t(i)] = std::min(w[std::size_t(i)], q[i]); w[std::size_t(3 + i)] = std::max(w[std::size_t(3 + i)], q[i]); }
            }
            lo_boxes[k] = w;
        }
    }
    const float kw = float(ep.face) / float(ep.face - 2);   // render_env's widened faces
    // the faces a world box (lo xyz, hi xyz; lo > hi: no box, every face) reaches within `reach` of the centre; 0: none
    const auto face_mask = [&](const std::array<float, 6>& w, float reach) -> std::uint8_t {
        if (!(w[0] <= w[3])) return 0x3F;
        float d2 = 0;   // the box's distance from the centre against the env cameras' far plane
        for (int i = 0; i < 3; ++i) {
            const float e = std::max({w[std::size_t(i)] - ep.centre[i], 0.0f, ep.centre[i] - w[std::size_t(3 + i)]});
            d2 += e * e;
        }
        if (d2 > reach * reach) return 0;
        std::uint8_t mask = 0;
        for (int fc = 0; fc < 6; ++fc) {
            float F[3], R[3], U[3];
            b3gfx::env_face(fc, F, R, U);
            int out[5] = {0, 0, 0, 0, 0};
            for (int c = 0; c < 8; ++c) {
                const float p[3] = {((c & 1) ? w[3] : w[0]) - ep.centre[0], ((c & 2) ? w[4] : w[1]) - ep.centre[1], ((c & 4) ? w[5] : w[2]) - ep.centre[2]};
                const float x = R[0] * p[0] + R[1] * p[1] + R[2] * p[2], y = U[0] * p[0] + U[1] * p[1] + U[2] * p[2];
                const float z = (F[0] * p[0] + F[1] * p[1] + F[2] * p[2]) * kw;
                out[0] += x < -z; out[1] += x > z; out[2] += y < -z; out[3] += y > z; out[4] += z <= 0.0f;
            }
            bool cull = false;
            for (int q = 0; q < 5; ++q) cull = cull || out[q] == 8;
            if (!cull) mask |= std::uint8_t(1U << fc);
        }
        return mask;
    };
    std::set<int> done;
    for (int j = 0; j < 17; ++j) {
        const int u = f.env_unit[j];
        // env cameras 1..3: bits 2..7 (reflect2: further than the game's 100 m, every streamed slot, culled by distance)
        if (u < 0 || (env_far <= 100.0f && ((f.vis[j] >> 2) & 0x3F) == 0) || !done.insert(u).second) continue;
        const auto it = lo_unit.find(u);
        if (it == lo_unit.end()) continue;
        for (int k : it->second) {
            if (k < 0 || std::size_t(k) >= lo.items.size()) continue;
            const std::uint8_t mask = face_mask(lo_boxes[std::size_t(k)], ep.zfar);
            if (!mask) continue;
            ep.items.push_back(lo.items[std::size_t(k)]);
            ep.faces.push_back(mask);
        }
    }
    std::size_t prop_seen = 0, prop_boxed = 0, prop_in = 0;   // (B3_ENV_TRACE)
    const std::size_t track_items = ep.items.size();
    if (env_props && f.props_off != 1) {   // reflect2: REFLECTION DETAIL Track and props
        const float reach = std::min(ep.zfar, kEnvPropReach);
        const auto prop_box = [&](int k, const float* M) {   // the hi item's mesh box under the model matrix M (column major)
            std::array<float, 6> w = {1, 1, 1, 0, 0, 0};
            if (k < 0 || std::size_t(k) >= hi_local_boxes.size()) return w;
            auto& b = hi_local_boxes[std::size_t(k)];
            if (b[0] == 2.0f && b[3] == 0.0f) {   // its mesh's box in its own space, once
                b = {1, 1, 1, 0, 0, 0};
                const std::size_t d = std::size_t(k) - hi.item_base;
                if (std::size_t(k) >= hi.item_base && d < hi.model.draws.size()) {
                    const auto& r = hi.model.draws[d];
                    std::array<float, 6> m = {1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
                    for (std::uint32_t i = r.first_index; i < r.first_index + r.index_count && i < hi.model.indices.size(); ++i) {
                        const std::uint32_t vi = hi.model.indices[i];
                        if (vi >= hi.model.vertices.size()) continue;
                        const float* p = hi.model.vertices[vi].pos;
                        for (int c = 0; c < 3; ++c) { m[std::size_t(c)] = std::min(m[std::size_t(c)], p[c]); m[std::size_t(3 + c)] = std::max(m[std::size_t(3 + c)], p[c]); }
                    }
                    if (m[0] <= m[3]) b = m;
                }
            }
            if (!(b[0] <= b[3])) return w;
            w = {1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
            for (int c = 0; c < 8; ++c) {
                const float x = (c & 1) ? b[3] : b[0], y = (c & 2) ? b[4] : b[1], z = (c & 4) ? b[5] : b[2];
                const float q[3] = {M[0] * x + M[4] * y + M[8] * z + M[12], M[1] * x + M[5] * y + M[9] * z + M[13], M[2] * x + M[6] * y + M[10] * z + M[14]};
                for (int i = 0; i < 3; ++i) { w[std::size_t(i)] = std::min(w[std::size_t(i)], q[i]); w[std::size_t(3 + i)] = std::max(w[std::size_t(3 + i)], q[i]); }
            }
            return w;
        };
        if (hi_local_boxes.size() != hi.items.size()) hi_local_boxes.assign(hi.items.size(), {2, 2, 2, 0, 0, 0});   // 2 > 0: not yet
        const auto add = [&](const b3gfx::DrawItem& d, int k) {
            ++prop_seen;
            const auto w = prop_box(k, d.model);
            if (!(w[0] <= w[3])) return;   // (a prop without a box is left out: never every face)
            ++prop_boxed;
            if (const std::uint8_t mask = face_mask(w, reach)) { ep.items.push_back(d); ep.faces.push_back(mask); ++prop_in; }
        };
        for (int k : always) {   // the static props draw_list draws (not the hidden / dynamised ones)
            const auto ip = item_prop.find(k);
            if (ip == item_prop.end() || std::binary_search(f.hidden.begin(), f.hidden.end(), std::uint16_t(ip->second))) continue;
            if (std::size_t(k) < hi.items.size()) add(hi.items[std::size_t(k)], k);
        }
        for (const auto& p : f.props) {   // the dynamic list, at the placements' live matrices
            const auto it = prop_items.find(p.placement);
            if (it == prop_items.end()) continue;
            for (int k : it->second) {
                if (k < 0 || std::size_t(k) >= hi.items.size()) continue;
                b3gfx::DrawItem d = hi.items[std::size_t(k)];
                to_colmajor(p.m, d.model);
                add(d, k);
            }
        }
        // the streamed units' instances (lamp posts, signs, gantries, barriers: each unit's +0x3E list, hi_inst), as
        // draw_list draws them for the streamed slots; the game itself pushes only the ones flagged +0x3E & 2 into its
        // env cameras [L draft 0x21C780], so this is more than the PS2 shows
        std::set<int> inst_done;
        for (int j = 0; j < 17; ++j) {
            const int u = f.slot_unit[j];
            if (u < 0 || !inst_done.insert(u).second || skip_units.count(u)) continue;
            if (const auto it = hi_inst.find(u); it != hi_inst.end())
                for (int k : it->second)
                    if (k >= 0 && std::size_t(k) < hi.items.size()) add(hi.items[std::size_t(k)], k);
        }
    }
    std::size_t car_in = 0;   // (B3_ENV_TRACE)
    if (env_cars && globals) {   // reflect2 C: the other vehicles near the centre, coarse, without their own reflections
        std::vector<Vehicle> near;
        for (const Vehicle& v : f.vehicles) {
            if (v.id == f.env_car) continue;   // (the car the map is round: its own body is not in its reflection)
            float d2 = 0;
            for (int k = 0; k < 3; ++k) d2 += (v.main.r[3][k] - ep.centre[k]) * (v.main.r[3][k] - ep.centre[k]);
            if (d2 < kEnvCarReach * kEnvCarReach) near.push_back(v);
        }
        if (!near.empty()) {
            b3view::CarGlobals g = *globals;
            g.env_world = b3gfx::kNone;
            std::vector<b3gfx::DrawItem> cars_out;
            const int lod_was = car_lod;
            car_lod = 1;
            vehicle_draws(dev, f.cam, near, f.vehicles, heads, false, ep.centre, g, cars_out, false,
                          night_on(f) || rain_on(f) ? tail_level() : -1.0f);
            car_lod = lod_was;
            for (b3gfx::DrawItem& d : cars_out) {
                if (d.shading != b3gfx::Shading::kCar) continue;
                b3gfx::CarParams& c = d.car_mut();
                c.env_mode = 0;   // no env pass on a car in a reflection (and nothing samples the map being drawn)
                c.env_world = b3gfx::kNone;
                c.env_texture = b3gfx::kNone;
                c.reflect = 0.0f;
                const float* M = d.model;   // a car part's box: 3 m round its origin (no mesh boxes for car parts)
                const std::array<float, 6> w = {M[12] - 3.0f, M[13] - 3.0f, M[14] - 3.0f, M[12] + 3.0f, M[13] + 3.0f, M[14] + 3.0f};
                if (const std::uint8_t mask = face_mask(w, ep.zfar)) { ep.items.push_back(d); ep.faces.push_back(mask); ++car_in; }
            }
        }
    }
    // reflect2 G: at night (with the props in the map) the fixtures glow in it: the gain makes up for the car's night env
    // scale and the grade's world multiplier, which the car pixel takes after the map
    if (env_props && night_on(f)) {
        const float wl = 0.2126f * night.world[0] + 0.7152f * night.world[1] + 0.0722f * night.world[2];
        const float k = night.car[1] * wl;
        ep.night_glow = k > 1e-3f ? std::clamp(1.0f / k, 1.0f, 8.0f) : 1.0f;
    }
    if (std::getenv("B3_ENV_TRACE"))   // diagnostics: what the env map holds this frame
        std::fprintf(stderr, "env: tick %zu car %08x centre %.1f %.1f %.1f items %zu (track %zu; props %zu of %zu boxed, %zu seen; car draws %zu; far %.0f; night glow %.2f)\n",
                     f.tick, unsigned(f.env_car), ep.centre[0], ep.centre[1], ep.centre[2], ep.items.size(), track_items, prop_in, prop_boxed,
                     prop_seen, car_in, double(ep.zfar), double(ep.night_glow));
    if (ep.items.empty()) { if (keep) keep->map = b3gfx::kNone; return b3gfx::kNone; }   // nothing within reach: the sky alone
    const int slot = env_slot;
    env_slot ^= 1;
    const std::uint32_t map = dev.render_env(slot, ep);
    if (keep) keep->map = map;
    return map;
}

void View::draw_list(b3gfx::Device& dev, const Frame& f, const std::vector<ModelHead>& heads, bool game_cull,
                     const float eye[3], std::vector<b3gfx::DrawItem>& out) {
    if (rain_on(f) && !rain_dry_built) rain_dry_build();   // weather: rain chosen after the load (the roofs' dry draws)
    hud_mirror = f.mirror;   // gameopts: the markers hud_list places next follow this frame's mirror
    orbit_away[0] = f.orbit_away[0];   // orbitcam1 (the HUD's markers, hud_list)
    orbit_away[1] = f.orbit_away[1];
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
            if (!(m >> k & 1U)) {
                out.push_back(free_view && int(k) == sky.cloud_item ? sky.cloud_free : sky.items[k]);
                rain_sky_tint(f, out.back());
            }
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
        if (auto it = backdrop.find(b); b >= 0 && it != backdrop.end() && !skip_backdrops.count(b))
            for (int k : it->second) {
                out.push_back(hi.items[std::size_t(k)]);
                rain_sky_tint(f, out.back());   // (weather: the backdrops carry the painted sky)
            }
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
    // as is, so the reflection geometry stood unmirrored over the road (race1's bridge exit: it looked like pop-in).
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
    if (night_on(f) && !night_texinfo.empty()) night_texlog(out, 0);
    lap(0);
    // Cars (render5): RenderVehicle's draws per vehicle, at the LOD the game's Draw code picks from its camera
    frame_now = f.fx.now;   // traffic-popin: the clock the traffic fade-in (Vehicle::since) is measured on
    {
        b3view::CarGlobals g = car_globals;
        g.env_world = env_world_map(dev, f, heads, &g);   // reflect-tunnel: the track in the env map (reflect2: and the cars)
        vehicle_draws(dev, f.cam, f.vehicles, f.vehicles, heads, game_cull, eye, g, out, true,
                      night_on(f) || rain_on(f) ? tail_level() : -1.0f);
    }
    vehicle_fades.resize(f.vehicles.size());   // traffic-popin: the shadows and lights fade with their cars
    for (std::size_t k = 0; k < f.vehicles.size(); ++k) vehicle_fades[k] = traffic_fade(f.vehicles[k], frame_now, f.cam.m.r[3]);
    if (ghost) ghost_draws(dev, f, eye, out);   // ghost1: the Time Attack ghost (none unless GHOST is on and a lap is loaded)
    lap(1);
    // Effects stage (fx1): CB3World::Render draws RenderShadows and RenderPreFogEffects after the cars, the specular
    // and the props; b3gfx draws DrawItem::effect after everything else, in this order
    fx_stats = EffectStats{};
    effects.particles = show_particles;
    // SUN SHADOWS (§19): where a car casts a sun shadow its blob fades to 30%, so the two do not stack to black (the box and
    // the gate as post_frame and b3gfx's caster pass make them: towards the ENVIRO sun, the moon at night)
    const std::vector<float>* blob = &vehicle_fades;
    std::vector<float> blob_fades;
    b3gfx::FrameParams::SunShadow box;
    if (sun_shadows && sky.has_enviro && (!night_on(f) || night.moon) && show_effects) {
        float L[3];
        if (night_on(f)) moon_direction(sky.enviro.to_sun, L);
        else std::memcpy(L, sky.enviro.to_sun, sizeof L);
        if (sun_shadow_box(f, L, box)) {
            blob_fades = vehicle_fades;
            const float* m = box.view_proj;
            for (std::size_t k = 0; k < f.vehicles.size(); ++k) {
                const float* p = f.vehicles[k].main.r[3];
                const float x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
                const float in = 1.0f - std::clamp((std::max(std::fabs(x), std::fabs(y)) - 0.85f) / 0.1f, 0.0f, 1.0f);
                blob_fades[k] *= 1.0f - 0.7f * in * b3gfx::sun_shadow_gate(vehicle_sky_open(f.vehicles[k], f.vehicles));
            }
            blob = &blob_fades;
        }
    }
    if (show_effects) effects.draw(dev, f.vehicles, f.fx, out, &fx_stats, blob);
    // night mode (remaster-features §17): the headlight pools, drawn after the frame is darkened (b3gfx's post-fog stage);
    // nightlights (§17.2): only on the presets without the projected beams (night.lights 0; else post_frame's lamps)
    if (night_on(f) && night.lights <= 0 && f.fx.valid) effects.draw_headlights(dev, f.vehicles, night.shape, night.lamp, out, &vehicle_fades);
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
        // paint2: the players' boost glow colours (CarPaint::flame_custom; -1 the game's)
        const std::int64_t flame[2] = {car_paint[0].flame_custom ? std::int64_t(car_paint[0].flame_rgb) : -1,
                                       car_paint[1].flame_custom ? std::int64_t(car_paint[1].flame_rgb) : -1};
        if (night_on(f) || rain_on(f)) {   // night mode, rain: the race cars' and traffic's head and tail lights on (drawn only: a copy of the frame's)
            std::vector<Vehicle> lit = f.vehicles;
            for (Vehicle& v : lit)
                if (v.corona.valid && v.kind != VehicleKind::kCrashed) {
                    v.corona.lights |= 0x0C;
                    v.corona.tail = tail_level();   // taillights: the tail level, rising to the brake light with the brake
                    v.corona.brake = v.brake;
                }
            build_coronas(ci, lit, point, pos, corona_arrays, &vehicle_fades, flame);
        } else {
            build_coronas(ci, f.vehicles, point, pos, corona_arrays, &vehicle_fades, flame);
        }
        b3gfx::TextureId tex[3];
        for (int a = 0; a < 3; ++a) tex[a] = effects.texture(dev, ci.k->texture[a]);
        coronas.draw(dev, cam, corona_arrays, tex, out, &corona_stats, render_aspect);
        // nightfix: the street lamps' heads (and the billboards' floodlights) as coronas, so the lamps read as lit
        std::vector<CoronaQuad>& quads = scratch_lamps;   // (members: no allocation per frame)
        quads.clear();
        if (night_on(f) || (tunnel_lights == 1 && day_route)) {   // (by day: TUNNEL LIGHTS' halos, §35.4; Glow only: the Xbox looks have none)
            night_lamp_coronas(cam, quads, !night_on(f));
            if (!quads.empty()) coronas.draw_quads(dev, quads, tex[0], out);
        }
        // weather (§38, rain.h wet_reflection): every light's streak on the wet road (the coronas of array 0: head, tail
        // and brake lights, signals; the lamps' heads and fittings), on the ground the cover grid finds below it
        if (rain_on(f)) {
            std::vector<CoronaQuad>& wet = scratch_wet;
            wet.clear();
            auto reflect = [&](const float* p, const std::uint8_t* rgb, float size, float gain) {
                float gy = 0;
                CoronaQuad q;
                if (!rain_ground(p, gy)) return;
                const float on_road[3] = {p[0], gy + 0.6f, p[2]};   // (a covered road stays dry: no streak)
                if (!rain_sheltered(on_road) && wet_reflection(cam, p, gy, rgb, size, gain, q)) wet.push_back(q);
            };
            for (const CoronaSlot& s : corona_arrays.slot[0]) {
                CoronaQuad cq;
                if (!corona_quad(cam, s, cq, render_aspect)) continue;
                float hs = 0;
                for (int k = 0; k < 3; ++k) hs += (cq.corner[1][k] - cq.corner[0][k]) * (cq.corner[1][k] - cq.corner[0][k]);
                reflect(s.pos, cq.rgb, 0.5f * std::sqrt(hs), night_on(f) ? 0.85f : 0.7f);
            }
            for (const CoronaQuad& lq : quads) {
                float c[3], hs = 0;
                for (int k = 0; k < 3; ++k) {
                    c[k] = 0.25f * (lq.corner[0][k] + lq.corner[1][k] + lq.corner[2][k] + lq.corner[3][k]);
                    hs += (lq.corner[1][k] - lq.corner[0][k]) * (lq.corner[1][k] - lq.corner[0][k]);
                }
                reflect(c, lq.rgb, 0.5f * std::sqrt(hs), 0.8f);
            }
            wet_layer.draw(dev, wet, tex[0], out);
        }
    }
    // weather (§38, rain.h): the falling rain, after the coronas in the same state (the corona texture's soft ends)
    if (rain_on(f) && show_effects && f.fx.effects_on && ci.valid && ci.k) {
        const Camera& cam = corona_view ? *corona_view : f.cam;
        std::vector<CoronaQuad>& quads = scratch_rain;
        float follow[3];   // (an isolated frame: the followed race car's speed along its heading, from its wheels)
        const float fs = follow_speed(f, follow);
        for (float& c : follow) c *= fs;
        rain_layer.build(cam, (double(f.tick) + double(f.alpha)) / 29.97, 1.0f, night_on(f), [this](const float* p) { return rain_sheltered(p); },
                         quads, rain_look, follow);
        rain_layer.draw(dev, quads, effects.texture(dev, ci.k->texture[0]), out);
        // the spray from the wheels (rain.h Spray): the game's tyre smoke texture as mist, graded with the world
        std::vector<CoronaQuad>& mist = scratch_mist;
        spray_layer.build(f.vehicles, cam, (double(f.tick) + double(f.alpha)) / 29.97, night_on(f), [this](const float* p) { return rain_sheltered(p); },
                          mist, spray_look);
        spray_layer.draw(dev, mist, effects.texture(dev, "fxsmoke"), out);
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

// carpaint (paint2, paint3): the player's paint on a car's draws since `first` (the atlas's: the paint map's modes; the
// wheels' draws their tint), the game's paint `game_paint` where the body is the game's. Shared by the cars in the scene
// (vehicle_draws) and the car pages' thumbnails (render_car_picture), so both draw the same paint.
void View::paint_car_items(std::vector<b3gfx::DrawItem>& out, std::size_t first, const b3view::CarModel& cm, const PaintMap& pm,
                           const CarPaint& own, int game_paint) const {
    if (cm.scene.textures.empty()) return;
    const b3gfx::TextureId atlas = cm.scene.textures.front();
    float paint[3], paint2[3];
    if (own.custom) b3asset::custom_paint_texel(own.rgb, paint);
    else for (int c = 0; c < 3; ++c) paint[c] = pm.map.main[std::clamp(game_paint, 0, pm.map.palettes - 1)][c];
    if (own.custom && own.second) b3asset::custom_paint_texel(own.rgb2, paint2);
    else std::memcpy(paint2, paint, sizeof paint2);
    float decal[3], wheel[3];   // paint2: the decals' and the wheels' colours
    b3asset::custom_paint_texel(own.decal_rgb, decal);
    b3asset::custom_paint_texel(own.wheel_rgb, wheel);
    for (std::size_t k = first; k < out.size(); ++k) {
        if (out[k].texture != atlas) continue;
        b3gfx::CarParams& c = out[k].car_mut();
        c.paint_mode = (own.custom ? 1 : 0) | (own.decals ? 0 : 2) | (own.finish == 1 ? 4 : own.finish == 2 ? 8 : own.finish == 3 ? 12 : 0) |
                       (own.decals && own.decal_custom ? 16 : 0) | (c.wheel && own.wheel_custom ? 32 : 0);
        std::memcpy(c.paint3, decal, sizeof c.paint3);
        std::memcpy(c.paint4, wheel, sizeof c.paint4);
        std::memcpy(c.paint, paint, sizeof c.paint);
        std::memcpy(c.paint2, paint2, sizeof c.paint2);
        c.paint_ref = pm.map.ref_level;
        c.paint_mask = pm.tex;
    }
}

// paint3: one car file's picture as the game's car-page thumbnails show a car -- a 3/4 front view from above, on nothing --
// in the game's paint `paint` and, with `own`, the player's paint layers (the same paint code as the car on the page). The
// car is drawn twice, on black and on white: the two give each pixel's coverage (alpha) and its colour. `s`, `ox`, `oy`:
// the framing's scale and offset (NDC) over the default one (a camera ~10 m away, the car about the middle).
// B3_THUMB_YAW / B3_THUMB_PITCH (degrees) / B3_THUMB_FOV / B3_THUMB_GAIN: tuning. false: not ready (the paint map is still
// being made: try again) or not drawable.
bool View::render_car_picture(b3gfx::Device& dev, int file, int paint, const CarPaint* own, float s, float ox, float oy, int w, int h,
                              std::vector<std::uint8_t>& rgba) {
    PictureJob job;
    return car_picture_begin(dev, file, paint, own, s, ox, oy, w, h, job) && car_picture_take(dev, job, true, rgba) == 1;
}

// paint3: a car picture begun (Device::picture_begin, twice: on black and on white); false: none (the paint map still on
// its way, no model)
bool View::car_picture_begin(b3gfx::Device& dev, int file, int paint, const CarPaint* own, float s, float ox, float oy, int w, int h,
                             PictureJob& job) {
    job = PictureJob{};
    if (file < 0 || std::size_t(file) >= cars.size() || w <= 0 || h <= 0) return false;
    const auto t_all = std::chrono::steady_clock::now();
    const PaintMap* pm = own && own->on() ? paint_map(dev, file) : nullptr;
    const double ms_pm = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_all).count();
    if (own && own->on() && !pm && paint_maps.count(file) && paint_maps[file].pending) return false;   // the map on its way
    thumb_model_ = true;   // (a model loaded here only: thumb_only_models)
    b3view::CarModel* cm = car_model(dev, file, pm && own->custom ? pm->map.ref : paint, 3);   // (the showroom's detail)
    thumb_model_ = false;
    if (!cm) return false;
    if (!frontend_globals) {
        std::string why;
        if (!b3view::load_car_globals(dev, game, car_globals, &why)) std::fprintf(stderr, "cars: %s (slots 1-4 untextured)\n", why.c_str());
        frontend_globals = true;
    }
    const float yaw = thumb_angles[0] * 3.14159265f / 180.0f, pitch = thumb_angles[1] * 3.14159265f / 180.0f,
                fov = thumb_angles[2] * 3.14159265f / 180.0f;
    // the car about the origin, turned by `yaw`; the camera on the far side of +z, raised by `pitch`, looking at the middle
    const float ca = std::cos(yaw), sa = std::sin(yaw);
    b3view::CarPose pose;
    static const float kSh[9] = {0.3893f, 0.3764f, 0.1550f, -0.0201f, -0.0322f, 0.2018f, 0.1293f, -0.0159f, -0.1250f};   // a studio light
    std::memcpy(pose.sh, kSh, sizeof pose.sh);
    pose.main.r[0][0] = ca; pose.main.r[0][2] = -sa;
    pose.main.r[2][0] = sa; pose.main.r[2][2] = ca;
    pose.body = pose.main;
    const float dist = 10.0f, target[3] = {0.0f, 0.5f, 0.0f};
    const float eye[3] = {target[0], target[1] + std::sin(pitch) * dist, target[2] + std::cos(pitch) * dist};
    float fwd[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    for (float& v : fwd) v /= fl;
    b3gfx::FrameParams fp;
    b3view::frame_params(sky, eye, fwd, false, false, fp);
    car_frame(fp);
    float vm[16], pr[16], vp[16];
    const float up[3] = {0, 1, 0};
    b3gfx::mat4_look(eye, fwd, up, vm);
    b3gfx::mat4_perspective_reverse_z(fov, float(w) / float(h), 0.05f, pr);
    b3gfx::mat4_mul(pr, vm, vp);
    const float fit[16] = {s, 0, 0, 0, 0, s, 0, 0, 0, 0, 1, 0, ox, oy, 0, 1};   // NDC: x' = s x + ox (column-major)
    b3gfx::mat4_mul(fit, vp, fp.view_proj);
    fp.present_scale = 255.0f / 128.0f;
    std::memcpy(fp.sun_colour, car_globals.sun_colour, sizeof fp.sun_colour);
    std::vector<b3gfx::DrawItem> items;
    b3view::add_car_draws(*cm, car_globals, pose, eye, eye, items);
    if (pm && own) paint_car_items(items, 0, *cm, *pm, *own, paint);
    job.w = w;
    job.h = h;
    job.file = file;
    job.begun_tick = hud_tick;
    const auto tb = std::chrono::steady_clock::now();
    fp.clear[0] = fp.clear[1] = fp.clear[2] = 0.0f;
    job.ids[0] = dev.picture_begin(w, h, fp, items);
    const auto tm = std::chrono::steady_clock::now();
    fp.clear[0] = fp.clear[1] = fp.clear[2] = 1.0f;
    job.ids[1] = job.ids[0] ? dev.picture_begin(w, h, fp, items) : 0;
    if (std::getenv("B3_THUMB_LOG"))   // diagnostics
        std::fprintf(stderr, "car thumbnails: %s begun in %.1f ms (paint map %.1f; pictures %.1f + %.1f ms)\n", cars[std::size_t(file)].path.c_str(),
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_all).count(), ms_pm,
                     std::chrono::duration<double, std::milli>(tm - tb).count(),
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tm).count());
    if (job.ids[0] && job.ids[1]) return true;
    std::vector<std::uint8_t> drop;
    if (job.ids[0]) dev.picture_take(job.ids[0], drop, true);
    job = PictureJob{};
    return false;
}

// paint3: a begun car picture once the GPU has drawn it (wait: waited for): 1 `rgba` the picture (its coverage from the
// two backgrounds), 0 not yet, -1 failed (the job ends)
int View::car_picture_take(b3gfx::Device& dev, PictureJob& job, bool wait, std::vector<std::uint8_t>& rgba) {
    if (!wait && job.on() && thumb_job_expired(job.begun_tick, hud_tick)) {   // its fence never passed (a lost device): given up
        for (int k = 0; k < 2; ++k) if (job.ids[k]) dev.picture_drop(job.ids[k]);
        std::fprintf(stderr, "car thumbnails: %s: a picture not done in %llu ticks, given up\n",
                     job.file >= 0 && std::size_t(job.file) < cars.size() ? cars[std::size_t(job.file)].path.c_str() : "?",
                     static_cast<unsigned long long>(kThumbJobTicks));
        job = PictureJob{};
        return -1;
    }
    // diagnostics: B3_THUMB_LOST=1: the window's pictures never come (as on a lost device)
    static const bool lost = [] { const char* e = std::getenv("B3_THUMB_LOST"); return e && e[0] == '1'; }();
    if (lost && !wait) return job.on() ? 0 : -1;
    for (int k = 0; k < 2; ++k) {
        if (!job.ids[k]) continue;
        const int r = dev.picture_take(job.ids[k], job.px[k], wait);
        if (r == 0) continue;
        job.ids[k] = 0;
        if (r < 0) job.failed = true;
    }
    if (job.on()) return 0;
    const std::size_t n = std::size_t(job.w) * std::size_t(job.h);
    const bool ok = !job.failed && n > 0 && job.px[0].size() >= n * 4 && job.px[1].size() >= n * 4;
    static const float gain = [] { const char* e = std::getenv("B3_THUMB_GAIN"); return float(e && *e ? std::atof(e) : 1.3); }();
    if (ok) picture_from_backgrounds(job.px[0].data(), job.px[1].data(), n, gain, rgba);
    job = PictureJob{};
    return ok ? 1 : -1;
}

// paint3: the models loaded only for thumbnails given back (their GPU buffers and textures) once no thumbnail of their
// car is drawn in this build or has a picture on its way (scrolled off the carousel, another page)
void View::thumb_models_release(b3gfx::Device& dev) {
    std::set<int> busy = thumb_files_used;
    for (const auto& [k, sl] : hud_thumb_textures) if (sl.job.on()) busy.insert(sl.job.file);
    for (const auto& [f, fit] : thumb_fit) if (fit.job.on()) busy.insert(f);
    for (const PictureJob& j : thumb_orphans) busy.insert(j.file);
    for (auto it = thumb_only_models.begin(); it != thumb_only_models.end();) {
        if (busy.count(std::get<0>(*it))) { ++it; continue; }
        const auto m = car_models.find(*it);
        std::size_t shared = 0;
        if (m != car_models.end() && m->second) {
            std::vector<GpuIds> other_ids;   // every other loaded model, the car globals, the showroom
            {
                for (const auto& [key, cm] : car_models)
                    if (cm && key != *it) {
                        other_ids.emplace_back();
                        other_ids.back().add_items(cm->scene.items);
                        other_ids.back().add_textures(cm->scene.textures);
                    }
                other_ids.emplace_back();
                for (const b3view::CarGlobals* g : {&car_globals, &room.globals}) {
                    for (const b3gfx::TextureId t : g->slot) other_ids.back().add_texture(t);
                    other_ids.back().add_texture(g->env_sky);
                }
                for (const b3view::Scene* sc : {&room.hi, &room.sky}) {
                    other_ids.back().add_items(sc->items);
                    other_ids.back().add_textures(sc->textures);
                }
            }
            GpuIds mine;
            mine.add_items(m->second->scene.items);
            mine.add_textures(m->second->scene.textures);
            std::vector<const GpuIds*> others;
            for (const GpuIds& o : other_ids) others.push_back(&o);
            const GpuIds rel = releasable_ids(mine, others, &shared);
            for (const b3gfx::BufferId b : rel.buffers) dev.release_buffer(b);
            for (const b3gfx::TextureId t : rel.textures) dev.release_texture(t);
            if (shared)   // (never expected: a model's ids are its own; kept rather than released under another's draws)
                std::fprintf(stderr, "car thumbnails: model %s palette %d: %zu GPU ids named elsewhere, kept\n",
                             cars[std::size_t(std::get<0>(*it))].path.c_str(), std::get<1>(*it), shared);
        }
        if (std::getenv("B3_THUMB_LOG"))   // paint3 diagnostics
            std::fprintf(stderr, "car thumbnails: model %s palette %d released (%zu shared ids kept)\n", cars[std::size_t(std::get<0>(*it))].path.c_str(),
                         std::get<1>(*it), shared);
        if (m != car_models.end()) car_models.erase(m);
        it = thumb_only_models.erase(it);
    }
}

// paint3: the cars index of a car-page thumbnail's car ("COMPCAR3" -> PVEH/COMP/CAR3.BGV), -1 none
int View::thumb_file(const std::string& name) {
    const auto it = thumb_file_of.find(name);
    if (it != thumb_file_of.end()) return it->second;
    int found = -1;
    if (hud_thumbnail_name(name) && (name.compare(0, 4, "XCUS") == 0 || name.compare(0, 4, "XBET") == 0)) {
        // customseries: an extra car's model, its folder's file (the slots as the game numbers them); added to `cars`
        // when scan_cars did not read it (the folder changed since)
        for (const b3native::ExtraCar& c : extra_cars_now().cars) {
            if (c.gtid_name() != name) continue;
            for (std::size_t k = 0; k < cars.size() && found < 0; ++k)
                if (cars[k].path == c.model) found = int(k);
            if (found < 0) {
                CarFile cf{c.model, {}, false};
                if (b3asset::read_file(cf.path, cf.bytes) && cf.bytes.size() > 0xE00) {
                    cars.push_back(std::move(cf));
                    found = int(cars.size() - 1);
                }
            }
            break;
        }
    } else if (hud_thumbnail_name(name)) {
        for (const char* ext : {".BGV", ".BTV"}) {   // a race car's, else a traffic vehicle's (carousel_models.h)
            const std::string want = "PVEH/" + name.substr(0, 4) + "/CAR" + name.substr(7) + ext;
            for (std::size_t k = 0; k < cars.size() && found < 0; ++k) {
                std::string p = cars[k].path;
                for (char& c : p) c = c == '\\' ? '/' : char(std::toupper(static_cast<unsigned char>(c)));
                if (p.size() >= want.size() && p.compare(p.size() - want.size(), want.size(), want) == 0) found = int(k);
            }
        }
    }
    return thumb_file_of[name] = found;
}

void View::vehicle_draws(b3gfx::Device& dev, const Camera& cam, const std::vector<Vehicle>& vehicles, const std::vector<Vehicle>& lights,
                         const std::vector<ModelHead>& heads, bool game_cull, const float eye[3], const b3view::CarGlobals& globals,
                         std::vector<b3gfx::DrawItem>& out, bool hide_own, float tail) {
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
        int file = car_file(heads[std::size_t(v.model)]);
        if (file < 0) continue;
        const int lod = car_lod >= 0 ? car_lod : game_lod(cam, v);
        // carpaint: a local player's own paint. A custom colour is drawn over the reference paint's texture (the
        // brightest: the most shading levels; an HD pack's replacement of it too); decals off with the game's paint keeps
        // the game's texture and fills the livery with that paint's colour
        const DrawPaint dp = draw_paint(dev, file, v);
        const CarPaint* own = dp.own;
        const PaintMap* pm = dp.pm;
        b3view::CarModel* cm = car_model(dev, file, dp.paint, lod);
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
        if (tail >= 0.0f && v.kind != VehicleKind::kCrashed) { p.tail = tail; p.brake = v.brake; }   // taillights
        p.glass = glass_damage ? v.glass : 0;   // settings1
        // payloads1 (B3_PAYLOADS=0: not drawn, the renderer before payloads1, for comparisons)
        static const bool kPayloads = [] { const char* e = std::getenv("B3_PAYLOADS"); return !(e && e[0] == '0'); }();
        p.npay = kPayloads ? std::min(v.npay, b3view::CarPose::kMaxPayload) : 0;
        for (int k = 0; k < p.npay; ++k) {   // (on board: the item's matrix x the interpolated main matrix)
            p.pay[k] = row(v.pay_released ? v.pay[k] : mul(v.pay[k], v.main));
            p.pay_list[k] = v.pay_list[k];
        }
        if (p.npay > 0 && payload_logged.insert(std::uint64_t(v.id) * 2u + (v.pay_released ? 1u : 0u)).second)
            std::fprintf(stderr, "payloads1: vehicle %08X (%s, %s) drawn with %d payload objects (C%d first)%s\n", unsigned(v.id),
                         cars[std::size_t(file)].path.c_str(), v.kind == VehicleKind::kTraffic ? "traffic" : v.kind == VehicleKind::kCrashed ? "crashed" : "race car",
                         p.npay, int(p.pay_list[0]), v.pay_released ? ", released: body parts" : "");
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
        if (pm) paint_car_items(out, first, *cm, *pm, *own, int(v.paint));   // carpaint: the atlas's draws take the paint map
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
// hdkey: a HUD texture on the device: its picture (rgba, img_w x img_h), made over by `make` when given (rgba, w, h ->
// the pixels, their ow x oh), and for an HD pack replacement the same of its original (w x h) beside it
// (Device::create_texture_hd: the HD TEXTURES key's other picture)
using HudMake = std::function<std::vector<std::uint8_t>(const std::uint8_t*, int, int, int&, int&)>;
static b3gfx::TextureId hud_upload(b3gfx::Device& dev, const HudTexture& t, const HudMake& make = {}) {
    std::vector<std::uint8_t> keep[2];
    const auto level = [&](const std::uint8_t* px, int w, int h, std::vector<std::uint8_t>& k) {
        if (!make) return b3gfx::TextureLevel{w, h, px};
        int ow = w, oh = h;
        k = make(px, w, h, ow, oh);
        return b3gfx::TextureLevel{ow, oh, k.data()};
    };
    const b3gfx::TextureLevel hd = level(t.rgba.data(), t.img_w, t.img_h, keep[0]);
    std::vector<b3gfx::TextureLevel> original;
    if (!t.original.empty() && t.original.size() == std::size_t(t.w) * std::size_t(t.h) * 4)
        original.push_back(level(t.original.data(), t.w, t.h, keep[1]));
    return dev.create_texture_hd({hd}, original);
}

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

// paint3: the coverage box of a picture's alpha >= min_a pixels, in NDC (x0, x1, y0, y1); false: none
static bool picture_box(const std::vector<std::uint8_t>& im, int w, int h, int min_a, float b[4]) {
    if (w <= 0 || h <= 0 || im.size() < std::size_t(w) * std::size_t(h) * 4) return false;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (im[4 * (std::size_t(y) * std::size_t(w) + std::size_t(x)) + 3] >= min_a) {
                x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
    if (x1 < 0) return false;
    b[0] = -1.0f + 2.0f * float(x0) / float(w); b[1] = -1.0f + 2.0f * float(x1 + 1) / float(w);
    b[2] = 1.0f - 2.0f * float(y1 + 1) / float(h); b[3] = 1.0f - 2.0f * float(y0) / float(h);
    return true;
}

// paint3 diagnostics (B3_THUMB_SEARCH=1, 2: every view logged): the view whose silhouette, fitted as thumb_texture fits
// it, best covers the game picture's (IoU of the alpha >= 64 masks at the picture's size) over a grid of yaw, pitch and
// field of view (B3_THUMB_SEARCH_YAW / _PITCH about which, B3_THUMB_SEARCH_STEP degrees; fov 20, 30, 40). Waits for the
// GPU: for measuring only.
void View::thumb_search(b3gfx::Device& dev, const HudTexture& t, int file) {
    const int tw = t.img_w > 0 ? t.img_w : t.w, th = t.img_h > 0 ? t.img_h : t.h;
    float theirs[4];
    if (!picture_box(t.rgba, tw, th, 64, theirs)) return;
    const float keep[3] = {thumb_angles[0], thumb_angles[1], thumb_angles[2]};
    float best = -1, ba[3] = {0, 0, 0};
    std::vector<std::uint8_t> q, r;
    static const float st = [] { const char* e = std::getenv("B3_THUMB_SEARCH_STEP"); return e ? float(std::atof(e)) : 4.0f; }();
    static const float c0 = [] { const char* e = std::getenv("B3_THUMB_SEARCH_PITCH"); return e ? float(std::atof(e)) : 20.0f; }();
    static const float c1 = [] { const char* e = std::getenv("B3_THUMB_SEARCH_YAW"); return e ? float(std::atof(e)) : -50.0f; }();
    for (float fv = 20; fv <= 40.1f; fv += 10)
        for (float pt = c0 - 3 * st; pt <= c0 + 3 * st + 0.1f; pt += st)
            for (float yw = c1 - 3 * st; yw <= c1 + 3 * st + 0.1f; yw += st) {
                thumb_angles[0] = yw; thumb_angles[1] = pt; thumb_angles[2] = fv;
                float o[4];
                if (!render_car_picture(dev, file, 0, nullptr, 1.0f, 0.0f, 0.0f, tw, th, q) || !picture_box(q, tw, th, 64, o)) continue;
                const float rx = (theirs[1] - theirs[0]) / (o[1] - o[0]), ry = (theirs[3] - theirs[2]) / (o[3] - o[2]);
                const float sc = 0.5f * (rx + ry);
                if (!render_car_picture(dev, file, 0, nullptr, sc, 0.5f * (theirs[0] + theirs[1]) - sc * 0.5f * (o[0] + o[1]),
                                        0.5f * (theirs[2] + theirs[3]) - sc * 0.5f * (o[2] + o[3]), tw, th, r))
                    continue;
                std::size_t in = 0, un = 0;
                for (std::size_t k = 0; k < std::size_t(tw) * std::size_t(th); ++k) {
                    const bool x = r[4 * k + 3] >= 64, y = t.rgba[4 * k + 3] >= 64;
                    in += x && y; un += x || y;
                }
                const float iou = un ? float(in) / float(un) : 0.0f;
                if (iou > best) { best = iou; ba[0] = yw; ba[1] = pt; ba[2] = fv; }
                if (std::getenv("B3_THUMB_SEARCH")[0] == '2')
                    std::fprintf(stderr, "thumb search %s yaw %.0f pitch %.0f fov %.0f iou %.4f\n", t.name.c_str(), double(yw), double(pt), double(fv), double(iou));
            }
    std::fprintf(stderr, "thumb search %s: best yaw %.0f pitch %.0f fov %.0f iou %.4f\n", t.name.c_str(), double(ba[0]), double(ba[1]), double(ba[2]), double(best));
    thumb_angles[0] = keep[0]; thumb_angles[1] = keep[1]; thumb_angles[2] = keep[2];
}

// paint3: a car thumbnail's texture drawn from car `file` (car_picture_begin) in palette `palette` and `own` paint (null:
// its game colours), framed to the game picture `t`, held in slot `key`, refilled when `sig` changes (a stale picture at
// most every kThumbRefreshTicks ticks). The pictures are drawn without waiting for the GPU: a job begun here is taken in
// a later build (the HUD built again while one is on its way: thumb_busy), and at most one is begun a build
// (thumb_budget): until its picture comes, a thumbnail shows the game's. In shots (thumb_wait) every one is drawn and
// waited for at once. kNone while it has no picture of ours.
b3gfx::TextureId View::thumb_texture(b3gfx::Device& dev, const HudTexture& t, int file, int palette, const CarPaint* own, std::uint64_t key,
                                     std::uint64_t sig, std::vector<std::uint64_t>& used) {
    if (file < 0) return b3gfx::kNone;
    struct Timed {   // diagnostics (B3_THUMB_LOG): the time this build spends here
        double& sum;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~Timed() { sum += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
    } timed{thumb_build_ms};
    ThumbSlot& slot = hud_thumb_textures[key];
    used.push_back(key);
    thumb_files_used.insert(file);
    if (slot.id == b3gfx::kNone && !hud_thumb_free.empty()) {   // a freed slot's texture, refilled below
        slot.id = hud_thumb_free.back();
        hud_thumb_free.pop_back();
        slot.sig = 0;
    }
    // (the game picture's own size: an HD pack's replacement is img_w x img_h)
    const int tw = t.img_w > 0 ? t.img_w : t.w, th = t.img_h > 0 ? t.img_h : t.h;
    const int pw = 2 * t.w, ph = 2 * t.h;
    ThumbFit& fit = thumb_fit[file];
    if (!fit.done) {   // the framing fitted once per car: our picture's coverage box onto the game's (its alpha), in NDC
        if (fit.job.ids[0] == 0 && fit.job.ids[1] == 0 && (thumb_wait || thumb_budget > 0)) {
            --thumb_budget;
            if (std::getenv("B3_THUMB_SEARCH") && file == thumb_file(t.name)) thumb_search(dev, t, file);
            car_picture_begin(dev, file, 0, nullptr, 1.0f, 0.0f, 0.0f, pw, ph, fit.job);
        }
        std::vector<std::uint8_t> px;
        const int got = fit.job.ids[0] || fit.job.ids[1] ? car_picture_take(dev, fit.job, thumb_wait, px) : 0;
        float ours[4], theirs[4];
        if (got == 1 && picture_box(px, pw, ph, 64, ours) && picture_box(t.rgba, tw, th, 64, theirs)) {
            // (the picture's own car: the mean of the two ratios, the boxes' shapes differ a little with the models'
            // details; another car on its picture (a traffic vehicle on its donor's): the smaller, so a long or tall
            // vehicle stays whole inside the picture)
            const float rx = (theirs[1] - theirs[0]) / (ours[1] - ours[0]), ry = (theirs[3] - theirs[2]) / (ours[3] - ours[2]);
            fit.s = file == thumb_file(t.name) ? 0.5f * (rx + ry) : std::min(rx, ry);
            fit.ox = 0.5f * (theirs[0] + theirs[1]) - fit.s * 0.5f * (ours[0] + ours[1]);
            fit.oy = 0.5f * (theirs[2] + theirs[3]) - fit.s * 0.5f * (ours[2] + ours[3]);
            std::fprintf(stderr, "car thumbnails: %s framed on %s's picture (scale %.3f, offset %.3f %.3f)\n", cars[std::size_t(file)].path.c_str(),
                         t.name.c_str(), double(fit.s), double(fit.ox), double(fit.oy));
        }
        if (got != 0) fit.done = true;   // (failed: drawn unfitted)
        if (!fit.done) {
            thumb_busy = true;
            return slot.sig != 0 ? slot.id : b3gfx::kNone;
        }
    }
    if (slot.sig != sig && slot.failed_sig != sig && !slot.job.on()) {   // a new picture, when due and in the budget
        const bool due = slot.sig == 0 || thumb_wait || hud_tick < slot.drawn_tick || hud_tick - slot.drawn_tick >= kThumbRefreshTicks;
        if (due && (thumb_wait || thumb_budget > 0) && car_picture_begin(dev, file, palette, own, fit.s, fit.ox, fit.oy, pw, ph, slot.job)) {
            --thumb_budget;
            slot.job_sig = sig;
            slot.drawn_tick = hud_tick;
        }
    }
    if (slot.job.ids[0] || slot.job.ids[1]) {   // a picture on its way
        std::vector<std::uint8_t> px;
        const std::uint64_t job_sig = slot.job_sig;
        const int got = car_picture_take(dev, slot.job, thumb_wait, px);
        if (got < 0) slot.failed_sig = job_sig;   // (given up or failed: not begun again for this paint)
        if (got == 1) {
            dev.update_texture(slot.id, pw, ph, px.data());
            slot.sig = slot.job_sig;
            if (const char* dir = std::getenv("B3_THUMB_DIR")) {   // diagnostics: our picture beside the game's
                std::string n = cars[std::size_t(file)].path;
                n = n.substr(n.find_last_of("/\\") == std::string::npos ? 0 : n.find_last_of("/\\") + 1);
                b3asset::write_png_rgba(std::string(dir) + "/" + t.name + "_" + n + "_ours.png", pw, ph, px.data());
                b3asset::write_png_rgba(std::string(dir) + "/" + t.name + "_game.png", tw, th, t.rgba.data());
                std::vector<std::uint8_t> gc;   // and ours in the game's colours, to set beside the game's
                if (render_car_picture(dev, file, palette, nullptr, fit.s, fit.ox, fit.oy, pw, ph, gc))
                    b3asset::write_png_rgba(std::string(dir) + "/" + t.name + "_" + n + "_gamecolours.png", pw, ph, gc.data());
            }
        }
    }
    if (slot.sig != sig && slot.failed_sig != sig) thumb_busy = true;   // (queued, or on its way: the HUD built again)
    return slot.sig != 0 ? slot.id : b3gfx::kNone;
}

// A HUD texture's own upload (hud_list's usual path; the thumbnails' fallback)
b3gfx::TextureId View::game_texture(b3gfx::Device& dev, std::uint64_t key, const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures) {
    auto tt = hud_textures.find(key);
    if (tt == hud_textures.end()) {
        const auto t = textures.find(key);
        tt = hud_textures.emplace(key, t != textures.end() && t->second->ok ? hud_upload(dev, *t->second) : b3gfx::kNone).first;
    }
    return tt->second;
}

void View::hud_list(b3gfx::Device& dev, const std::shared_ptr<const HudFrame>& hud_in, const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures,
                    float aspect, std::vector<b3gfx::DrawItem>& out, int out_h, int out_w, const HudProjection* proj, bool frontend) {
    if (!show_hud || hud_in == nullptr || hud_in->width <= 0 || hud_in->height <= 0 || !(aspect > 0)) return;
    // hudmods (hudmods.h): the frame with the RPM meter's draws and the boost bar's shake (the same frame with both off)
    const std::shared_ptr<const HudFrame> hud = frontend ? hud_in : hud_mods.apply(hud_in, hud_mod_options);
    // paint2: the car pages' thumbnails in the page's player's paint (the car select's; any other page: player 1's)
    const int thumb_player = !hud->fe_cars.empty() && hud->fe_cars.back().player == 2 ? 1 : 0;
    const CarPaint& thumb_paint = car_paint[thumb_player];
    std::uint64_t thumb_sig = 0;
    if (thumb_paint.on()) {   // paint3: every layer the 3D car shows (the thumbnails are drawn from the cars' models)
        thumb_sig = 0x9E3779B97F4A7C15ull;
        for (const std::uint64_t v : {std::uint64_t(thumb_player), std::uint64_t(thumb_paint.custom), std::uint64_t(thumb_paint.rgb),
                                      std::uint64_t(thumb_paint.second), std::uint64_t(thumb_paint.rgb2), std::uint64_t(thumb_paint.decals),
                                      std::uint64_t(thumb_paint.decal_custom), std::uint64_t(thumb_paint.decal_rgb),
                                      std::uint64_t(thumb_paint.wheel_custom), std::uint64_t(thumb_paint.wheel_rgb), std::uint64_t(thumb_paint.finish)})
            thumb_sig = (thumb_sig ^ v) * 0x100000001B3ull;
    }
    if (thumb_busy || hud_built_thumb != thumb_sig || hud_built_carousel != hud_carousel || hud_built != hud || hud_aspect != aspect || hud_built_wide != hud_wide || hud_built_sharp != hud_sharp || hud_built_h != out_h ||
        hud_built_w != out_w || hud_built_frontend != frontend || hud_built_fe_layout != fe_layout || hud_built_movie_fill != movie_fill ||
        hud_built_prompt != prompt_device || !(hud_built_prompt_keys == prompt_keys) || hud_built_filter != hud_colour_filter ||
        hud_built_flashes != reduce_flashes || hud_built_fonts != hud_fonts || hud_built_size != hud_size || hud_built_series != series_glyphs) {
        std::vector<std::uint64_t> thumb_used;   // paint2: the thumbnails this build draws (the others' slots freed after it)
        hud_built_thumb = thumb_sig;   // paint2
        hud_built_carousel = hud_carousel;   // paint3
        thumb_build_ms = 0;
        thumb_busy = false;                  // paint3: one new thumbnail picture a build (shots: all)
        thumb_budget = 1;
        thumb_wait = !async_paint;
        thumb_files_used.clear();
        for (auto j = thumb_orphans.begin(); j != thumb_orphans.end();) {   // pictures of thumbnails gone, taken when done
            std::vector<std::uint8_t> drop;
            if (car_picture_take(dev, *j, false, drop) != 0) j = thumb_orphans.erase(j);
            else { ++j; thumb_busy = true; }
        }
        hud_items.clear();
        hud_item_marker.clear();
        hud_item_draw.clear();   // splitlayout1
        hud_draw_pieces.assign(hud->draws.size(), {});   // mac5 (menu mouse)
        hud_pieces_frame = hud_in;   // (hudmods: the added draws come after the frame's, so its draws' indexes hold)
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
                        if (t != textures.end() && t->second->ok) id = hud_upload(dev, *t->second);   // hdkey: and its original
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
        // 1.3.1 (HudObject::ticker_port, tools/native/pc_menu.h): a ticker whose text is the port's (the main menu's own
        // rows' help, in the LANGUAGE) is laid out so at every size, over the game's strip (ext 0) when not in the panels,
        // its wrap from its own width as the game's Prepare (0x31CB20) computes it: gap + the advances x the font's cell x 28
        if (hud_fonts && !ticker_43)
            for (std::size_t di = 0; di < hud->draws.size(); ++di) {
                const HudDraw& d = hud->draws[di];
                if (d.texture == 0 || d.video >= 0 || d.tri.empty() || d.object < 0 || std::size_t(d.object) >= hud->objects.size()) continue;
                const HudObject& o0 = hud->objects[std::size_t(d.object)];
                if (o0.ticker_font == 0 || (!panels && !o0.ticker_port) || !hud_in_clip_region(d, right)) continue;
                HudObject o_port;
                if (o0.ticker_port) {
                    o_port = o0;
                    for (const HostFont& hf : hud_fonts->fonts)
                        if (hf.raster == o0.ticker_font && !hf.glyphs.empty()) {
                            float adv = 0.0f;
                            for (const char16_t c : o0.ticker_text) adv += hf.find(std::uint16_t(c))->advance;
                            o_port.ticker_extent = o0.ticker_gap + adv * hf.scale_x * 28.0f;
                        }
                }
                const HudObject& o = o0.ticker_port ? o_port : o0;
                const float ext_t = panels ? ext : 0.0f;
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
                if (!f || !hud_ticker_layout(o, *f, float(t->second->tw) / float(t->second->w), float(t->second->th) / float(t->second->h), ext_t,
                                             d.tri[0].rgba, d.tri[0].z, td.tri)) {
                    ticker_draw.erase(di);   // no record: the game's draw as it is
                    continue;
                }
                td.scissor[0] = int(std::lround(float(d.scissor[0]) - ext_t));
                td.scissor[2] = int(std::lround(float(d.scissor[2]) + ext_t));
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
        if (std::getenv("B3_THUMB_SEARCH") || std::getenv("B3_THUMB_ALL")) {
            // diagnostics: every car picture loaded drawn (B3_THUMB_DIR) or searched (thumb_search) once
            // B3_THUMB_SEARCH_MIN pictures are loaded (the search stalls the renderer: at a tour's last shot)
            std::vector<std::uint64_t> unused;
            std::size_t pics = 0;
            for (const auto& [key, tex] : textures) pics += tex && tex->ok && hud_thumbnail_name(tex->name) && thumb_file(tex->name) >= 0;
            const char* mn = std::getenv("B3_THUMB_SEARCH_MIN");
            std::fprintf(stderr, "thumb search: %zu car pictures loaded\n", pics);
            if (!mn || pics >= std::size_t(std::atoi(mn)))
            for (const auto& [key, tex] : textures)
                if (tex && tex->ok && hud_thumbnail_name(tex->name) && thumb_file(tex->name) >= 0 && !thumb_fit[thumb_file(tex->name)].done)
                    thumb_texture(dev, *tex, thumb_file(tex->name), 0, nullptr, key, 1, unused);
        }
        // paint3 (tools/native/carousel_models.h): the carousel slots whose model is not their picture's, each paired with a
        // quad of its picture: the slots of one picture in their order down the carousel with that picture's quads in
        // theirs (by their middles' y)
        std::map<std::pair<std::size_t, std::size_t>, const HostCarouselSlot*> thumb_override;
        std::set<std::size_t> thumb_override_draws;
        std::vector<b3gfx::TextureId> quad_tex;
        if (hud_carousel && !hud_carousel->slots.empty()) {
            std::map<std::uint32_t, std::vector<const HostCarouselSlot*>> by_raster;
            for (const HostCarouselSlot& sl : hud_carousel->slots) by_raster[sl.raster].push_back(&sl);
            for (auto& [raster, slots] : by_raster) {
                std::sort(slots.begin(), slots.end(), [](const HostCarouselSlot* x, const HostCarouselSlot* y) { return x->slot < y->slot; });
                std::vector<std::tuple<float, std::size_t, std::size_t>> quads;   // (middle y, draw, quad)
                for (std::size_t di = 0; di < hud->draws.size(); ++di) {
                    const HudDraw& d = hud->draws[di];
                    const auto t = d.texture ? textures.find(d.texture) : textures.end();
                    if (t == textures.end() || !t->second->ok || t->second->raster != raster || !hud_thumbnail_name(t->second->name) ||
                        d.tri.size() % 6 != 0)
                        continue;
                    for (std::size_t q = 0; q < d.tri.size() / 6; ++q) {
                        float y = 0;
                        for (std::size_t k = 0; k < 6; ++k) y += d.tri[6 * q + k].y;
                        quads.emplace_back(y / 6.0f, di, q);
                    }
                }
                std::sort(quads.begin(), quads.end());
                for (std::size_t k = 0; k < quads.size() && k < slots.size(); ++k) {
                    thumb_override[{std::get<1>(quads[k]), std::get<2>(quads[k])}] = slots[k];
                    thumb_override_draws.insert(std::get<1>(quads[k]));
                }
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
                    // promptround: the 2D's units in the 4:3 box (an x unit over a y unit: 448 / 480), so a round button stays round
                    // (0 in the split-screen own layout: split_hud then places the 2D with its own scale, where dev's
                    // quads were already round; the review found the 4:3 correction made them 5% too wide there)
                    const float unit_aspect = hud_split_pass_ ? 0.0f : (4.0f / 3.0f) * float(hud->height) / float(hud->width);
                    prompt_sub = prompt_substitute(t->second->name, t->second->w, t->second->h, dp->tri, prompt_atlas, tri, unit_aspect);
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
                    if (t != textures.end() && t->second->ok)
                        id = hud_upload(dev, *t->second, [&](const std::uint8_t* rgba, int w, int h, int&, int&) {   // hdkey: and its original
                            std::vector<std::uint8_t> px(rgba, rgba + std::size_t(w) * std::size_t(h) * 4);
                            a11y::remap_pixels(filter_m, px.data(), px.size() / 4);
                            return px;
                        });
                    tt = hud_textures_filtered.emplace(std::make_pair(key, hud_colour_filter), id).first;
                }
                it.texture = tt->second;
                if (it.texture == b3gfx::kNone) continue;
            } else if (d.texture != 0 && !glyph_step && [&] {   // paint3: a car thumbnail drawn from a car's model
                           const auto t = textures.find(d.texture);
                           if (t == textures.end() || !t->second->ok || !hud_thumbnail_name(t->second->name)) return false;
                           return (thumb_sig != 0 && thumb_file(t->second->name) >= 0) || thumb_override_draws.count(di) != 0;
                       }()) {
                // each quad of the draw (the decoder joins neighbouring thumbnails of one picture): a carousel slot's own
                // model (tools/native/carousel_models.h: a traffic vehicle shown with its donor's picture), else the
                // picture's car in the player's paint, else the game's picture; the draw is split per quad below
                const HudTexture& t = *textures.find(d.texture)->second;
                const std::size_t nq = d.tri.size() % 6 == 0 ? d.tri.size() / 6 : 0;
                quad_tex.assign(std::max<std::size_t>(nq, 1), b3gfx::kNone);
                for (std::size_t q = 0; q < quad_tex.size(); ++q) {
                    const auto ov = thumb_override.find({di, q});
                    b3gfx::TextureId id = b3gfx::kNone;
                    if (ov != thumb_override.end()) {
                        const HostCarouselSlot& sl = *ov->second;
                        const bool painted = sl.paintable && thumb_sig != 0;
                        std::uint64_t key = 0xCBF29CE484222325ull;
                        for (const char c : sl.model) key = (key ^ std::uint8_t(c)) * 0x100000001B3ull;
                        key = ((key ^ std::uint64_t(sl.paint)) * 0x100000001B3ull) & ~1ull;   // (the HUD texture keys are odd)
                        id = thumb_texture(dev, t, thumb_file(sl.model), sl.paint, painted ? &thumb_paint : nullptr, key,
                                           painted ? thumb_sig : 1, thumb_used);
                    } else if (thumb_sig != 0 && thumb_file(t.name) >= 0) {
                        id = thumb_texture(dev, t, thumb_file(t.name), t.palette <= 0 ? 0 : t.palette, &thumb_paint, d.texture, thumb_sig, thumb_used);
                    }
                    quad_tex[q] = id != b3gfx::kNone ? id : game_texture(dev, d.texture, textures);
                }
                it.texture = quad_tex[0];
                if (it.texture == b3gfx::kNone) continue;
                if (nq < 2 || std::all_of(quad_tex.begin(), quad_tex.end(), [&](b3gfx::TextureId x) { return x == quad_tex[0]; })) quad_tex.clear();
            } else if (d.texture != 0) {
                const std::uint64_t key = still_static(d.texture);   // (d.texture unless SCREEN FLASHES Reduced)
                auto tt = hud_textures.find(key);
                if (tt == hud_textures.end()) {
                    const auto t = textures.find(key);
                    b3gfx::TextureId id = b3gfx::kNone;
                    if (t != textures.end() && t->second->ok) {
                        id = hud_upload(dev, *t->second);   // hdkey: and its original
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
            if (!quad_tex.empty() && it.index_count == 6 * quad_tex.size()) {   // paint3: a quad's own picture each
                for (std::size_t q = 0; q < quad_tex.size(); ++q) {
                    b3gfx::DrawItem qi = it;
                    qi.first_index = it.first_index + std::uint32_t(6 * q);
                    qi.index_count = 6;
                    qi.texture = quad_tex[q];
                    hud_items.push_back(qi);
                    hud_item_marker.push_back(d.marker);
                    hud_item_draw.resize(hud_items.size(), -1);
                    hud_item_draw.back() = int(di);
                }
                quad_tex.clear();
            } else {
            quad_tex.clear();
            hud_items.push_back(it);
            hud_item_marker.push_back(d.marker);
            hud_item_draw.resize(hud_items.size(), -1);   // splitlayout1 (split_hud): the draw it came from
            hud_item_draw.back() = int(di);
            }
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
        // paint2: the thumbnails no longer drawn give their slots back (no growth while browsing cars and colours)
        for (auto ts = hud_thumb_textures.begin(); ts != hud_thumb_textures.end();) {
            if (std::find(thumb_used.begin(), thumb_used.end(), ts->first) != thumb_used.end()) { ++ts; continue; }
            if (ts->second.id != b3gfx::kNone) hud_thumb_free.push_back(ts->second.id);
            if (ts->second.job.on()) thumb_orphans.push_back(std::move(ts->second.job));   // paint3
            ts = hud_thumb_textures.erase(ts);
        }
        for (auto& [f, fit] : thumb_fit)   // paint3: a framing's picture for a car no longer shown: taken when done
            if (fit.job.on() && !thumb_files_used.count(f)) thumb_orphans.push_back(std::move(fit.job)), fit.job = PictureJob{};
        if (!thumb_orphans.empty()) thumb_busy = true;
        thumb_models_release(dev);   // paint3
        if (thumb_busy && std::getenv("B3_THUMB_LOG") && std::getenv("B3_THUMB_LOST"))
            std::fprintf(stderr, "car thumbnails: tick %llu: busy\n", static_cast<unsigned long long>(hud_tick));
        if (thumb_build_ms >= 2.0 && std::getenv("B3_THUMB_LOG"))
            std::fprintf(stderr, "car thumbnails: tick %llu: %.1f ms in this build\n", static_cast<unsigned long long>(hud_tick), thumb_build_ms);
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
            if (!hud_reproject || proj == nullptr || !mk.valid) {
                // orbitcam1: at the game's place, which its camera gave: shrunk away while the view is turned from it
                const float keep = 1.0f - std::clamp(orbit_away[mk.view == 1 ? 1 : 0], 0.0f, 1.0f);
                if (keep < 1.0f) {
                    pl.r = keep;
                    pl.shown = keep > 0.02f;
                }
                continue;
            }
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
        float dx = pl.dx;
        if (hud_mirror && b[0] != 0.0f) {
            // gameopts (MIRROR MODE): the marker's point at its mirror about the view's middle (clip x -> -x; the 2D -> clip
            // matrix has no shear: b[4] is 0), the marker itself drawn as it is
            const float x0 = mk.x + pl.dx;
            dx = (-x0 - 2.0f * b[12] / b[0]) - mk.x;
        }
        const float tx = (1.0f - pl.r) * mk.x + dx, ty = (1.0f - pl.r) * mk.y + pl.dy;
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
            if (t != textures.end() && t->second->ok)
                id = hud_upload(dev, *t->second, [&](const std::uint8_t* rgba, int w, int h, int& bw, int& bh) {   // hdkey: and its original
                    std::vector<std::uint8_t> px = fill_blur(rgba, w, h, bw, bh);
                    a11y::remap_pixels(a11y::daltonize_matrix(a11y::colour_filter(hud_colour_filter)), px.data(), px.size() / 4);
                    return px;
                });
            it = fill_still_filtered.emplace(std::make_pair(d.texture, hud_colour_filter), id).first;
        }
        tex = it->second;
    } else {
        auto it = fill_still.find(d.texture);
        if (it == fill_still.end()) {
            b3gfx::TextureId id = b3gfx::kNone;
            const auto t = textures.find(d.texture);
            if (t != textures.end() && t->second->ok)   // hdkey: and its original
                id = hud_upload(dev, *t->second, [](const std::uint8_t* rgba, int w, int h, int& bw, int& bh) { return fill_blur(rgba, w, h, bw, bh); });
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
                        int out_h, int out_w, int slot) {
    if (!show_host_ui || ui == nullptr || ui->quads.empty() || !(aspect > 0)) return;
    HostUiBuild& B = host_build[slot == 1 ? 1 : 0];
    if (B.built != ui || B.aspect != aspect || B.h != out_h || B.w != out_w || B.filter != hud_colour_filter) {
        B.items.clear();
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
                } else if (p.rfind("img:", 0) == 0) {
                    // settingsui: a picture the host made (the page's button glyphs). One texture slot, refilled when the
                    // picture changes (a new device or a rebound key), so a change does not keep the old one until a reset
                    if (p == host_img_name) return host_img_tex;
                    for (const auto& im : ui->images)
                        if (im && "img:" + im->name == p && im->w > 0 && im->h > 0 && im->rgba.size() >= std::size_t(im->w) * std::size_t(im->h) * 4) {
                            dev.update_texture(host_img_tex, im->w, im->h, im->rgba.data());
                            host_img_name = host_img_tex == b3gfx::kNone ? std::string() : p;
                            break;
                        }
                    return p == host_img_name ? host_img_tex : b3gfx::kNone;
                } else if (p.rfind("hudtex:", 0) == 0) {   // mac5: a captured HUD texture by raster (the PC SETTINGS page's fonts)
                    const std::uint32_t raster = std::uint32_t(std::strtoul(p.c_str() + 7, nullptr, 16));
                    if (host_ui_hud_textures)
                        for (const auto& [key, t] : *host_ui_hud_textures)
                            if (t && t->ok && t->raster == raster && !t->rgba.empty()) {
                                id = hud_upload(dev, *t);   // hdkey: and its original
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
            if (B.items.empty() || B.items.back().texture != t || item_cover != cover) {
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
                B.items.push_back(it);
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
            B.items.back().index_count = std::uint32_t(is.size()) - B.items.back().first_index;
        }
        if (!vs.empty()) {
            dev.update_vertex_buffer(B.vb, vs.data(), vs.size());
            dev.update_index_buffer(B.ib, is.data(), is.size());
        }
        for (auto& it : B.items) { it.vertices = B.vb; it.indices = B.ib; }
        B.built = ui;
        B.aspect = aspect;
        B.h = out_h;
        B.w = out_w;
        B.filter = hud_colour_filter;
    }
    out.insert(out.end(), B.items.begin(), B.items.end());
}

// ---- gsoff1 (§29): the 2-player split screen ----------------------------------------------------------------------

void View::split_frame(b3gfx::Device& dev, const Frame& f, const std::vector<ModelHead>& heads,
                       const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& hud_textures, bool game_cull, bool fog,
                       bool spec, int out_w, int out_h, float view_aspect, float hud_aspect,
                       std::vector<b3gfx::Device::ViewPass>& views, b3gfx::FrameParams& ofp, std::vector<b3gfx::DrawItem>& overlay) {
    orbit_away[0] = f.orbit_away[0];   // orbitcam1 (the markers at the game's places: hud_list)
    orbit_away[1] = f.orbit_away[1];
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
            if (!f.view2.track_valid) fk.env_car = 0;   // reflect-tunnel: no player 2 env inputs captured: the sky alone
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
                std::memcpy(fk.env_unit, t2.env_unit, sizeof fk.env_unit);   // reflect-tunnel: player 2's env map
                fk.env_car = t2.env_car;
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
        hud_split_pass_ = true;
        hud_carousel = f.carousel; hud_tick = f.tick;   // paint3
        hud_list(dev, f.hud, hud_textures, 4.0f / 3.0f, overlay, out_h, out_w, nullptr);
        hud_split_pass_ = false;
        if (f.hud) split_hud(hud_mods.laid_out(f.hud), f, views, view_half_w, out_w, out_h, overlay);   // hudmods: the draws laid out
        return;
    }
    // markers at the game's own places (no hud4 reprojection: through a view of this frame they land up to ~3 px off the
    // game's projection although the camera is the marker's own viewport [C B3HUD_MARKER_TRACE, gsoff1mp2]; not settled)
    hud_carousel = f.carousel; hud_tick = f.tick;   // paint3
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
            const float cx = hud_mirror ? -c[0] : c[0];   // gameopts (MIRROR MODE): the point mirrored in its view
            const float px = p.dst[0] + 0.5f * (cx / c[3] + 1.0f) * dw, py = p.dst[1] + 0.5f * (1.0f - c[1] / c[3]) * dh;
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
    if (!frontend_globals) {   // paint3: the car thumbnails' (render_car_picture) loaded with the room, not at the first thumbnail
        std::string why;
        if (!b3view::load_car_globals(dev, game, car_globals, &why)) std::fprintf(stderr, "cars: %s (slots 1-4 untextured)\n", why.c_str());
        frontend_globals = true;
    }
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

// gamemods1: the CAR VIEWER's backgrounds (D-pad left steps through them; tools/native/car_viewer.h), RGB 0..1, given to
// the frame's clear (b3gfx scales it by the present scale's inverse, so it shows as given)
void car_viewer_background(int index, float clear[4]) {
    static const float kBg[15][3] = {{0, 0, 0},          {0.25f, 0.25f, 0.25f}, {0.5f, 0.5f, 0.5f},   {0.8f, 0.8f, 0.8f},
                                     {1, 1, 1},          {0.05f, 0.1f, 0.3f},   {0.35f, 0.6f, 0.9f},  {0.05f, 0.25f, 0.1f},
                                     {0.2f, 0.7f, 0.3f}, {0.35f, 0.05f, 0.05f}, {0.85f, 0.15f, 0.1f}, {0.4f, 0.15f, 0.55f},
                                     {0.95f, 0.5f, 0.1f}, {0.95f, 0.85f, 0.2f}, {0.1f, 0.5f, 0.5f}};
    const float* c = kBg[index >= 0 && index < 15 ? index : 0];
    for (int k = 0; k < 3; ++k) clear[k] = c[k];
    clear[3] = 1.0f;
}

void View::frontend_frame(b3gfx::Device& dev, const Frame& f, const std::vector<ModelHead>& heads,
                          const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures, float aspect, int out_w, int out_h,
                          b3gfx::FrameParams& fp, std::vector<b3gfx::DrawItem>& out) {
    orbit_away[0] = orbit_away[1] = 0.0f;   // orbitcam1 (no race view)
    frontend_params(fp);
    hud_mirror = false;   // gameopts: the front end is never mirrored
    frame_now = f.fx.now;   // traffic-popin (vehicle_draws' traffic_fade)
    fp.sun_sprites.clear();
    fp.blur = b3gfx::FrameParams::RadialBlur{};
    if (f.fe_car != 0 && f.cam.valid && show_frontend_car) {
        // CB3FrontEnd::Render draws the car (CB3World::RenderFrontEnd) before the menu pages' 2D [C asm 0x132FA4 per a
        // reading of it]: the race car path, the game camera (Hor+ at the output's aspect)
        // gamemods1: the CAR VIEWER's car alone, on the viewer's background (no showroom: the garage's leftovers are not its)
        Showroom* sr = show_room && f.fe_viewer < 0 ? showroom(dev, f.route_size, f.route_units) : nullptr;
        if (!sr && !frontend_globals) {
            std::string w;
            if (!b3view::load_car_globals(dev, game, car_globals, &w)) std::fprintf(stderr, "cars: %s (slots 1-4 untextured)\n", w.c_str());
            frontend_globals = true;
        }
        const Vehicle* car = nullptr;
        for (const auto& v : f.vehicles) if (v.id == f.fe_car) car = &v;
        paint_fe_car = f.fe_car;   // padpaint: the car on show takes its world slot's player's paint
        paint_fe_player = car && car->player >= 0 ? car->player : f.fe_player == 1 ? 1 : 0;
        if ((car_paint[0].on() || car_paint[1].on()) && paint_fe_logged != f.fe_player * 4 + paint_fe_player) {
            paint_fe_logged = f.fe_player * 4 + paint_fe_player;
            std::fprintf(stderr, "car paint: the front end's car (%s): player %d's paint\n",
                         car && car->player >= 0 ? "a player's car" : f.fe_player >= 0 ? "the car select's player" : "not the car select",
                         paint_fe_player + 1);
        }
        if (!sr) {   // menu1's frame: the car alone on black
            b3view::frame_params(sky, f.cam.m.r[3], f.cam.m.r[2], false, false, fp);
            frontend_params(fp);   // the game's frame model (half-intensity frame, doubled on present); frame_params left it for an unlit scene
            game_view_proj(f.cam, aspect, fp.view_proj);
            car_frame(fp);
            if (f.fe_viewer >= 0) car_viewer_background(f.fe_viewer, fp.clear);   // gamemods1
            if (std::getenv("B3_CARVIEWER_TRACE"))
                std::fprintf(stderr, "car viewer frame: fe_viewer %d clear %.2f %.2f %.2f\n", f.fe_viewer, double(fp.clear[0]), double(fp.clear[1]), double(fp.clear[2]));
            paint_frontend = true;   // carpaint
            vehicle_draws(dev, f.cam, f.vehicles, f.vehicles, heads, false, fp.eye, car_globals, out);
            paint_frontend = false;
        } else {
            // fe2 (§24): RenderFrontEnd's order [C matched 0x275EF0]: the environment's backdrop texture, sky, sun blob and
            // clouds; RenderAllBackdrops range 0; the car's floor reflection, the car; range 1, the full-screen specular,
            // range 2; RenderFrontEndShadow; the sun corona. b3gfx draws its phases (sky, opaque + cars, specular, blended,
            // effects) in list order within each, so range 1's blended draws land after the specular [G: unseen].
            b3view::CarGlobals globals = sr->globals;
            if (room_env && car && car->model >= 0 && std::size_t(car->model) < heads.size()) {
                // the env map around the car: BuildCustomCameraData(idx)'s centre, the car's box middle (+0xE80 / +0xE90)
                Vehicle::Shadow box;
                if (const int file = car_file(heads[std::size_t(car->model)]); file >= 0 && file_shadow(cars[std::size_t(file)].full(), box)) {
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
            paint_frontend = true;   // carpaint: the showroom's car (and its floor reflection) is player 1's
            if (room_reflection && car) {
                const std::vector<Vehicle> mirrored{floor_reflection(*car)};
                vehicle_draws(dev, f.cam, mirrored, mirrored, heads, false, fp.eye, globals, out);
            }
            vehicle_draws(dev, f.cam, f.vehicles, f.vehicles, heads, false, fp.eye, globals, out);
            paint_frontend = false;
            for (int g = 1; g < 3; ++g) for (int k : sr->range[g]) out.push_back(sr->hi.items[std::size_t(k)]);
            if (room_shadow && car && car->model >= 0 && std::size_t(car->model) < heads.size()) {
                Vehicle::Shadow s;
                s.ess = car->main;
                if (const int file = car_file(heads[std::size_t(car->model)]); file >= 0 && file_shadow(cars[std::size_t(file)].full(), s))
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
    hud_carousel = f.carousel; hud_tick = f.tick;   // paint3
    hud_list(dev, f.hud, textures, aspect, out, out_h, out_w, nullptr, fe_layout != 0);
}

}  // namespace b3replay

namespace b3replay {

// settings1 CAMERA: FIELD OF VIEW / FOV CORRECTION applied to our view of the game's camera (the guest stays retail).
// docs/specs/settings-page.md §4. The view window's tangents scale by fov_scale. With the correction, the eye moves
// along its view direction so that the chased car keeps its size on screen: something at distance d keeps its size when
// the eye moves forward by d (1 - 1 / fov_scale) (backwards for a narrower view). The chased car: the nearest race car
// whose rear (its centre's distance along the view less kRearFraction of its bounding radius) is in front of the eye by
// at least kMinGap, within a chase camera's reach (kChaseReach) and near the view axis -- camfixes (2026-11-22): and
// it is the view's own player car (car_id), so a car passing between the camera and the player's (the finish: the
// rivals flying by) or the car behind in a look-back view never becomes the chased car (the offsets snapped to it, or
// pushed the eye forward into the player's own car). d is its centre's distance along
// the view less kSizeFraction of its radius: with it the race shots at FOV 60 / 120 keep the car's drawn width closest
// to the game's 90 (d to the rear moved too little, d to the centre too far). A forward move stops kClearance short of
// the car's rear. Views with no such car (in the car, on the bumper, trackside replay cameras far from it) do not move.
//
// 1.1 chase camera (chase_distance / chase_height, drawn only): with a chased car, its distance d (as above, after the
// FOV move) becomes d x chase_distance (the eye moved back along the view; a closer view stops kClearance short of the
// car's rear), the eye rises chase_height along the camera's up, and the camera turns about the axis between its old
// and new view directions so that the point it looked at (d ahead) stays at the centre. Without a chased car: unchanged.
// sigcam (1.2): only the game's chase cameras take it (Camera::chase, from its camera manager: capture.cpp chase_share);
// its takedown cameras (a signature takedown's too), crash and other cameras are drawn as the game frames them, and a
// blend between a chase camera and another takes the offset in step with the game's blend.
// camfixes: the chased car of camera `c` among `vehicles` (apply_fov's test): `car_id` (0: any race car, the choice
// before camfixes); its id, `best` its distance along the view less kSizeFraction of its radius, `best_rear` its rear's
// distance (0, best -1: none)
std::uint32_t chase_target(const Camera& c, const std::vector<Vehicle>& vehicles, std::uint32_t car_id, float& best, float& best_rear) {
    constexpr float kChaseReach = 20.0f, kMinGap = 0.75f, kRearFraction = 0.85f, kSizeFraction = 0.4f;
    constexpr float kAxisSlope = 0.3f, kAxisMin = 1.5f, kDefaultRadius = 2.5f;
    best = -1.0f;
    best_rear = 0.0f;
    std::uint32_t id = 0;
    const float* eye = c.m.r[3];
    float at[3] = {c.m.r[2][0], c.m.r[2][1], c.m.r[2][2]};
    const float len = std::sqrt(at[0] * at[0] + at[1] * at[1] + at[2] * at[2]);
    if (!(len > 1e-6f)) return 0;
    for (float& a : at) a /= len;
    for (const Vehicle& v : vehicles) {
        if (v.kind != VehicleKind::kRaceCar || v.own_hidden || (car_id != 0 && v.id != car_id)) continue;
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
            id = v.id;
        }
    }
    return id;
}

void View::apply_fov(Camera& c, const std::vector<Vehicle>& vehicles, std::uint32_t car_id) const {
    const float share = std::clamp(c.chase, 0.0f, 1.0f);
    const bool chase = (chase_distance != 1.0f || chase_height != 0.0f) && share > 0.0f;
    if (fov_scale != 1.0f && c.valid) {
        c.half_w *= fov_scale;
        c.half_h *= fov_scale;
    }
    const bool dolly = fov_scale != 1.0f && fov_correction && fov_scale > 0.0f;
    const bool any_offset = dolly || chase_distance != 1.0f || chase_height != 0.0f;
    if (!any_offset || !c.valid) return;
    constexpr float kClearance = 1.0f;
    float at[3] = {c.m.r[2][0], c.m.r[2][1], c.m.r[2][2]};
    const float len = std::sqrt(at[0] * at[0] + at[1] * at[1] + at[2] * at[2]);
    if (!(len > 1e-6f)) return;
    for (float& a : at) a /= len;
    float up[3] = {c.m.r[1][0], c.m.r[1][1], c.m.r[1][2]};
    const float ul = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (!(ul > 1e-6f)) return;
    for (float& u : up) u /= ul;
    // camfixes2: the weight (chase_clock; the header)
    ChaseEase& e = chase_ease;
    const bool easing = chase_clock >= 0.0;
    const double dt = easing && e.t >= 0.0 ? chase_clock - e.t : -1.0;
    // a jump in time (the first frame, a seek, a shot): no easing, the weight as the frame is; a cut: the view turning
    // faster than 30 degrees per poll (a rate: a low frame rate's frames are polls apart, an ordinary turn is no cut)
    const bool jump = e.t < 0.0 || !(dt >= -1.0 && dt <= 30.0);
    const float cosang = std::clamp(at[0] * e.at[0] + at[1] * e.at[1] + at[2] * e.at[2], -1.0f, 1.0f);
    const bool cut = !jump && std::acos(cosang) > 0.5235988f * float(std::max(1.0, dt));
    e.t = easing ? chase_clock : -1.0;
    for (int k = 0; k < 3; ++k) e.at[k] = at[k];
    float best = -1.0f, best_rear = 0.0f;
    static const bool any_car = [] { const char* v = std::getenv("B3_CHASE_ANY_CAR"); return v && *v == '1'; }();   // (checks)
    if (dolly || chase) chase_target(c, vehicles, any_car ? 0u : car_id, best, best_rear);
    const bool on = best >= 0.0f && (dolly || chase);
    if (!easing || jump) e.w = on ? 1.0f : 0.0f;
    else if (cut) e.w = 0.0f;   // a cut: no old offset carried over; a chased car's eases in from here
    else {
        const float step = float(std::max(0.0, dt) / kChaseEase);
        e.w = on ? std::min(1.0f, e.w + step) : std::max(0.0f, e.w - step);
    }
    if (on) {   // the full offsets for the chased car now
        float move = 0.0f;
        if (dolly) {
            move = best * (1.0f - 1.0f / fov_scale);
            if (move > 0.0f) move = std::min(move, std::max(0.0f, best_rear - kClearance));
        }
        float back = 0.0f;
        if (chase) {
            back = (best - move) * (chase_distance - 1.0f) * share;
            if (back < 0.0f) back = std::max(back, -std::max(0.0f, best_rear - move - kClearance));
        }
        e.move = move;
        e.back = back;
        e.up = chase ? chase_height * share : 0.0f;
        e.d = best;
    }
    const float w = e.w;
    if (!(w > 0.0f)) return;
    const float move = e.move * w, back = e.back * w, rise = e.up * w;
    for (int k = 0; k < 3; ++k) c.m.r[3][k] += at[k] * move;
    if (back == 0.0f && rise == 0.0f) return;
    const float d = e.d - move;    // the chased car's distance now
    float aim[3];                  // the point the camera looks at, d ahead
    for (int k = 0; k < 3; ++k) aim[k] = c.m.r[3][k] + at[k] * d;
    for (int k = 0; k < 3; ++k) c.m.r[3][k] += -at[k] * back + up[k] * rise;
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
