// rain.h -- weather (remaster-features §38): the falling rain around the camera, drawn only.
//
// Streaks in a box around the camera, anchored in the world (a drop keeps its place as the camera moves, so the rain
// does not swim with it): each drop's x / z is its seed's place in a repeating `2 half` m cell, its height the seed's
// place in a `height` m column falling at `fall` m/s. A streak is the drop's path over a short exposure relative to the
// camera (the drop's velocity, `wind` included, less the camera's), so it is a short near-vertical line when the
// camera stands and a long slanted one at speed. Each streak is a thin quad facing the camera, drawn with the corona
// texture (soft ends) in the corona state (b3replay/coronas.cpp: additive, Z tested, no Z write, after the fog and, at
// night, after the grade, so the lamps keep it visible). None under a roof (`sheltered`: View's cover grid, cached per
// cell). Deterministic: the drops follow the frame's time, so a replay or a shot draws the same rain.
#ifndef B3REPLAY_RAIN_H
#define B3REPLAY_RAIN_H

#include "b3gfx/b3gfx.h"
#include "capture.h"
#include "coronas.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace b3replay {

struct RainLook {
    int count = 3200;            // drops in the box
    float half = 16.0f;          // the box's half width (m) around the camera
    float height = 12.0f;        // its height (m), from 3 m below the camera
    float fall = 9.0f;           // m/s
    float wind[2] = {0.9f, 0.4f};   // m/s (x, z)
    float exposure = 0.024f;     // s: a streak's length is the drop's relative path over this
    float min_len = 0.35f, max_len = 3.0f;   // m
    float width = 0.014f;        // m (the half width at 1 m from the camera grows a little with distance: see build)
    float level_day = 48.0f;     // the streak's colour (GS bytes, x the corona texture; additive)
    float level_night = 13.0f;
};

// Quads drawn as one item in the corona state (b3replay/coronas.cpp: additive, Z tested GEQUAL, no Z write, after the
// fog and, at night, after the grade), with buffers of their own
class QuadLayer {
public:
    void clear() { vb_ = ib_ = b3gfx::kNone; }
    // additive (the rain, the reflections: lights, after the night's grade) or, `mist`, blended by the vertex alpha
    // ((Cs - Cd) As + Cd, graded with the world at night: the spray)
    void draw(b3gfx::Device& dev, const std::vector<CoronaQuad>& quads, b3gfx::TextureId tex, std::vector<b3gfx::DrawItem>& out,
              bool mist = false) {
        if (dev.resource_epoch() != epoch_) { vb_ = ib_ = b3gfx::kNone; epoch_ = dev.resource_epoch(); }
        if (quads.empty() || tex == b3gfx::kNone) return;
        verts_.clear();
        idx_.clear();
        static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (const CoronaQuad& q : quads) {
            const std::uint32_t base = std::uint32_t(verts_.size());
            for (int c = 0; c < 4; ++c) {
                b3gfx::Vertex o;
                std::memset(&o, 0, sizeof o);
                std::memcpy(o.pos, q.corner[c], sizeof o.pos);
                o.normal[1] = 1;
                o.uv[0] = uv[c][0];
                o.uv[1] = uv[c][1];
                o.rgba = std::uint32_t(q.rgb[0]) | std::uint32_t(q.rgb[1]) << 8 | std::uint32_t(q.rgb[2]) << 16 |
                         std::uint32_t(mist ? q.alpha : 0x80u) << 24;
                verts_.push_back(o);
            }
            for (std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) idx_.push_back(base + k);
        }
        dev.update_vertex_buffer(vb_, verts_.data(), verts_.size());
        dev.update_index_buffer(ib_, idx_.data(), idx_.size());
        b3gfx::DrawItem d;
        d.vertices = vb_;
        d.indices = ib_;
        d.index_count = std::uint32_t(idx_.size());
        d.texture = tex;
        d.blend = b3gfx::Blend::kAlpha;
        d.shading = b3gfx::Shading::kTrack;
        d.track_type = 0;
        d.gs_test = 0x53001;   // as the coronas: RGB written, Z tested (GEQUAL), not written
        d.lighting = 0;
        d.effect = true;
        d.post_fog = true;
        d.gs_alpha = mist ? 0x44 : 0x68;   // (Cs - Cd) As + Cd: the mist; (Cs - 0) FIX + Cd: additive
        d.blend_fix = mist ? -1.0f : 1.0f;   // (the mist by As: the texel's x the vertex's; the lights by FIX 0x80)
        d.clamp_uv = true;
        d.night_glow = !mist;   // lights after the grade (the night's and the rain's); the mist graded with the world
        out.push_back(d);
    }


private:
    b3gfx::BufferId vb_ = b3gfx::kNone, ib_ = b3gfx::kNone;
    unsigned epoch_ = 0;
    std::vector<b3gfx::Vertex> verts_;
    std::vector<std::uint32_t> idx_;
};

// weather (§38): a light's reflection on the wet road: a soft streak lying on the ground `ground_y` below the light at
// `p`, from just behind its foot towards the camera (on screen: a streak below the light), longer the higher the light
// and the farther the camera, brightest at grazing angles. `rgb` the light's colour as its corona draws it, `size` its
// corona's half size (m). False when the camera is below the ground or the light under it.
inline bool wet_reflection(const Camera& cam, const float p[3], float ground_y, const std::uint8_t rgb[3], float size, float gain,
                           CoronaQuad& q) {
    const float* eye = cam.m.r[3];
    const float h = p[1] - ground_y;
    if (h < 0.1f || h > 30.0f || eye[1] < ground_y + 0.2f) return false;
    float dc[2] = {eye[0] - p[0], eye[2] - p[2]};
    const float dist = std::sqrt(dc[0] * dc[0] + dc[1] * dc[1]);
    if (dist < 1.0f || dist > 250.0f) return false;
    dc[0] /= dist;
    dc[1] /= dist;
    const float back = 0.25f * h, len = std::min(dist * 0.6f, std::min(4.0f + 3.0f * h, 40.0f));
    const float w = std::clamp(size * 0.45f, 0.08f, 1.2f);
    const float side[2] = {-dc[1] * w, dc[0] * w};
    const float y = ground_y + 0.03f;
    const float a[2] = {p[0] - dc[0] * back, p[2] - dc[1] * back}, b[2] = {p[0] + dc[0] * len, p[2] + dc[1] * len};
    // grazing: the camera's height over the ground against the distance (a low, far view mirrors most)
    const float graze = 1.0f - std::clamp((eye[1] - ground_y) / (dist + 1.0f), 0.0f, 1.0f);
    const float k = std::clamp(gain * (0.25f + 0.75f * graze * graze), 0.0f, 1.0f);
    q.corner[0][0] = a[0] - side[0]; q.corner[0][1] = y; q.corner[0][2] = a[1] - side[1];
    q.corner[1][0] = a[0] + side[0]; q.corner[1][1] = y; q.corner[1][2] = a[1] + side[1];
    q.corner[2][0] = b[0] + side[0]; q.corner[2][1] = y; q.corner[2][2] = b[1] + side[1];
    q.corner[3][0] = b[0] - side[0]; q.corner[3][1] = y; q.corner[3][2] = b[1] - side[1];
    for (int c = 0; c < 3; ++c) q.rgb[c] = std::uint8_t(std::clamp(float(rgb[c]) * k, 0.0f, 255.0f));
    q.depth = dist;
    return q.rgb[0] || q.rgb[1] || q.rgb[2];
}

class Rain {
public:
    void clear() { layer_.clear(); has_last_ = false; }
    // The streaks for camera `cam` at time `t` (s); `gain` 0..1 (0: none), `night` the night's level; `sheltered` true for a
    // point under a roof. Returns the quads (CoronaQuad: top left / right at the drop, bottom right / left at its tail).
    // `fallback_vel` (optional): the camera's velocity when the last frame gives none (an isolated frame: an offscreen
    // shot, the first frame after a cut), e.g. the followed car's
    void build(const Camera& cam, double t, float gain, bool night, const std::function<bool(const float*)>& sheltered,
               std::vector<CoronaQuad>& out, const RainLook& look = RainLook{}, const float* fallback_vel = nullptr) {
        out.clear();
        const float* eye = cam.m.r[3];
        // the camera's velocity from the last frame, smoothed (none after a cut, a jump or a long pause; the same time
        // drawn again -- a paused frame, photo mode -- keeps it)
        if (has_last_ && t != last_t_) {
            const double dt = t - last_t_;
            bool ok = dt > 0 && dt < 0.25;
            float v[3] = {0, 0, 0};
            if (ok) {
                float v2 = 0;
                for (int k = 0; k < 3; ++k) { v[k] = float((eye[k] - last_eye_[k]) / dt); v2 += v[k] * v[k]; }
                ok = v2 < 120.0f * 120.0f;
            }
            if (ok) for (int k = 0; k < 3; ++k) vel_[k] = vel_valid_ ? 0.6f * vel_[k] + 0.4f * v[k] : v[k];
            vel_valid_ = ok;
        }
        if (!has_last_ || t != last_t_) {
            std::memcpy(last_eye_, eye, sizeof last_eye_);
            last_t_ = t;
            has_last_ = true;
        }
        if (!vel_valid_) {
            if (fallback_vel) std::memcpy(vel_, fallback_vel, sizeof vel_);
            else std::memset(vel_, 0, sizeof vel_);
        }
        if (gain <= 0.0f || !cam.valid) return;
        const float S = 2.0f * look.half, H = look.height;
        const float level = (night ? look.level_night : look.level_day) * std::clamp(gain, 0.0f, 1.0f);
        const float rel[3] = {look.wind[0] - vel_[0], -look.fall - vel_[1], look.wind[1] - vel_[2]};
        const float* at = cam.m.r[2];
        out.reserve(std::size_t(look.count));
        for (int i = 0; i < look.count; ++i) {
            const std::uint32_t h0 = hash(std::uint32_t(i) * 3u + 1u), h1 = hash(std::uint32_t(i) * 3u + 2u), h2 = hash(std::uint32_t(i) * 3u + 3u);
            const float sx = unit(h0), sz = unit(h1), sy = unit(h2);
            const float fall_i = look.fall * (0.85f + 0.3f * unit(hash(h2 ^ 0x9E3779B9u)));
            float p[3];
            p[0] = eye[0] + (wrap((sx * S - eye[0]) / S) - 0.5f) * S;
            p[2] = eye[2] + (wrap((sz * S - eye[2]) / S) - 0.5f) * S;
            p[1] = eye[1] - 3.0f + wrap((sy * H - fall_i * float(std::fmod(t, 3600.0)) - (eye[1] - 3.0f)) / H) * H;
            const float d[3] = {p[0] - eye[0], p[1] - eye[1], p[2] - eye[2]};
            const float z = d[0] * at[0] + d[1] * at[1] + d[2] * at[2];
            if (z < 0.4f) continue;   // behind or at the lens
            const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (dist > look.half) continue;   // a round volume: the box's corners would show as a square edge at speed
            if (sheltered && sheltered(p)) continue;
            float e[3] = {rel[0] * look.exposure, rel[1] * look.exposure * (fall_i / look.fall), rel[2] * look.exposure};
            float len = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            if (len < 1e-4f) continue;
            const float want = std::clamp(len, look.min_len, look.max_len);
            for (int k = 0; k < 3; ++k) e[k] *= want / len;
            // the quad's side: across the streak and the view ray
            float s[3] = {e[1] * d[2] - e[2] * d[1], e[2] * d[0] - e[0] * d[2], e[0] * d[1] - e[1] * d[0]};
            const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
            if (sl < 1e-6f) continue;
            const float w = look.width * (1.0f + 0.04f * dist);
            for (int k = 0; k < 3; ++k) s[k] *= w / sl;
            CoronaQuad q;
            for (int k = 0; k < 3; ++k) {
                q.corner[0][k] = p[k] - s[k];
                q.corner[1][k] = p[k] + s[k];
                q.corner[2][k] = p[k] + e[k] + s[k];
                q.corner[3][k] = p[k] + e[k] - s[k];
            }
            // nearer drops brighter, the far ones fading into the haze; a little variety per drop
            const float fade = (1.0f - dist / look.half) * (0.7f + 0.3f * unit(hash(h0 ^ h1)));
            const std::uint8_t c = std::uint8_t(std::clamp(level * (0.35f + 0.65f * fade), 0.0f, 255.0f));
            q.rgb[0] = q.rgb[1] = c;
            q.rgb[2] = std::uint8_t(std::min(255, int(c) + int(c) / 8));
            q.depth = z;
            out.push_back(q);
        }
    }
    // The streaks as one draw in the corona state (their own buffers)
    void draw(b3gfx::Device& dev, const std::vector<CoronaQuad>& quads, b3gfx::TextureId tex, std::vector<b3gfx::DrawItem>& out) {
        layer_.draw(dev, quads, tex, out);
    }

    static std::uint32_t hash(std::uint32_t x) {
        x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
        return x;
    }
    static float unit(std::uint32_t h) { return float(h >> 8) * (1.0f / 16777216.0f); }
    static float wrap(float v) { return v - std::floor(v); }

private:
    QuadLayer layer_;
    bool has_last_ = false, vel_valid_ = false;
    double last_t_ = 0;
    float last_eye_[3] = {0, 0, 0}, vel_[3] = {0, 0, 0};
};

// weather (§38): rain drops on the lens (b3gfx FrameParams::Drops, shaders/drops.frag). Stateless and deterministic: 16
// slots, each living its own cycle (1.5-4 s): a drop lands at a place and size of its cycle's seed, holds, and fades;
// at speed the drops run outward from the centre (the air flow over the lens) and leave the picture sooner. `gain` 0..1
// (the rain, less inside a tunnel), `speed` m/s (the followed car's), `aspect` the view's width / height.
inline int lens_drops(double t, float gain, float speed, float aspect, float out[][4], int max_out) {
    if (gain <= 0.0f) return 0;
    int n = 0;
    const float run = std::clamp(speed, 0.0f, 60.0f) * 0.011f;   // uv per second, outward
    for (std::uint32_t i = 0; i < 16 && n < max_out; ++i) {
        const float life = 1.5f + 2.5f * Rain::unit(Rain::hash(i * 7u + 1u));
        const double x = t / double(life) + double(Rain::unit(Rain::hash(i * 7u + 2u)));
        const std::uint32_t cyc = std::uint32_t(std::floor(x));
        const float age = float(x - std::floor(x));
        const std::uint32_t h = Rain::hash(i * 1013u + cyc * 7919u + 3u);
        float c[2] = {0.04f + 0.92f * Rain::unit(h), 0.06f + 0.86f * Rain::unit(Rain::hash(h ^ 0x5bd1e995u))};
        const float r = 0.010f + 0.020f * Rain::unit(Rain::hash(h + 11u));
        // outward from the picture's centre (a little up), faster the farther out
        float d[2] = {(c[0] - 0.5f) * aspect, c[1] - 0.42f};
        const float dl = std::max(std::sqrt(d[0] * d[0] + d[1] * d[1]), 0.05f);
        const float travel = run * age * life;
        c[0] += d[0] / dl / aspect * travel;
        c[1] += d[1] / dl * travel;
        if (c[0] < -r || c[0] > 1.0f + r || c[1] < -r || c[1] > 1.0f + r) continue;
        const float fade = std::clamp(age / 0.04f, 0.0f, 1.0f) * (1.0f - std::clamp((age - 0.6f) / 0.4f, 0.0f, 1.0f));
        const float a = gain * fade * 0.65f;
        if (a <= 0.01f) continue;
        out[n][0] = c[0];
        out[n][1] = c[1];
        out[n][2] = r;
        out[n][3] = a;
        ++n;
    }
    return n;
}

// weather (§38): the spray from the wheels on a wet road. Stateless puffs streaming back from each rear wheel (a race
// car's captured wheels; traffic: two points from its matrix and radius) along the car's path, rising and spreading
// with their distance, denser with speed (none below `v0` m/s, full at `v1`), drawn as mist (QuadLayer `mist`). The
// speed: the vehicle's movement since the last frame (by its id; none after a cut). None under a roof (`sheltered`).
struct SprayLook {
    float v0 = 8.0f, v1 = 40.0f;   // m/s
    int puffs = 7;                 // per wheel
    float spacing = 1.1f;          // m between puffs at full speed (the trail's length: puffs x spacing)
    float size0 = 0.3f, grow = 0.14f;   // a puff's half size (m) at the wheel, its growth per m back
    float rise = 0.08f;            // m up per m back
    float alpha = 26.0f;           // the nearest puff's As (of 0x80) at full speed
    std::uint8_t grey_day = 150, grey_night = 70;
};

class Spray {
public:
    void clear() { layer_.clear(); last_.clear(); }
    void build(const std::vector<Vehicle>& vehicles, const Camera& cam, double t, bool night,
               const std::function<bool(const float*)>& sheltered, std::vector<CoronaQuad>& out, const SprayLook& look = SprayLook{}) {
        out.clear();
        std::vector<Seen>& now = now_;   // (kept: no allocation per frame)
        now.clear();
        const float* eye = cam.m.r[3];
        const float* right = cam.m.r[0];
        const float* up = cam.m.r[1];
        for (const Vehicle& v : vehicles) {
            if (v.kind == VehicleKind::kCrashed) continue;
            const float* p = v.main.r[3];
            Seen s{v.id, {p[0], p[1], p[2]}, t, 0.0f};
            float ws = 0;   // a race car's wheels: +0x4E8 is the speed over the wheel's radius (rad/s; ~0.33 m)
            if (v.parts)
                for (int j = 0; j < std::min(v.nwheel, 6); ++j) ws = std::max(ws, std::fabs(v.wheel_speed[j]) * 0.33f);
            for (const Seen& o : last_)
                if (o.id == v.id && t > o.t && t - o.t < 0.25) {
                    float d2 = 0;
                    for (int k = 0; k < 3; ++k) d2 += (p[k] - o.pos[k]) * (p[k] - o.pos[k]);
                    s.speed = float(std::sqrt(d2) / (t - o.t));
                    if (s.speed > 120.0f) s.speed = 0.0f;   // (a cut, a reset)
                    s.speed = 0.5f * s.speed + 0.5f * o.speed;
                } else if (o.id == v.id && t == o.t) {
                    s.speed = o.speed;
                }
            if (ws > 0.0f) s.speed = ws;
            now.push_back(s);
            const float k = std::clamp((s.speed - look.v0) / (look.v1 - look.v0), 0.0f, 1.0f);
            if (k <= 0.0f || !cam.valid) continue;
            float dx = p[0] - eye[0], dz = p[2] - eye[2];
            if (dx * dx + dz * dz > 90.0f * 90.0f) continue;
            if (sheltered && sheltered(p)) continue;
            // the rear wheels' contact points (race cars: the captured wheels 2 and 3, the rear pair; traffic: from the
            // matrix, a radius-sized car)
            const float* back = v.main.r[2];   // the car's forward axis; the spray streams the other way
            float w[2][3];
            int nw = 0;
            if (v.parts && v.nwheel >= 4) {   // the two wheels farthest back along the car's forward axis
                int a = -1, b = -1;
                float fa = 1e30f, fb = 1e30f;
                for (int j = 0; j < std::min(v.nwheel, 6); ++j) {
                    float f = 0;
                    for (int c = 0; c < 3; ++c) f += (v.wheel[j].r[3][c] - p[c]) * back[c];
                    if (f < fa) { b = a; fb = fa; a = j; fa = f; }
                    else if (f < fb) { b = j; fb = f; }
                }
                for (int j : {a, b}) {
                    if (j < 0) continue;
                    for (int c = 0; c < 3; ++c) w[nw][c] = v.wheel[j].r[3][c] - (c == 1 ? 0.3f : 0.0f);
                    ++nw;
                }
            } else {
                const float half = std::clamp(v.radius * 0.33f, 0.6f, 1.1f), len = std::clamp(v.radius * 0.55f, 1.0f, 3.5f);
                for (int j = 0; j < 2; ++j, ++nw)
                    for (int c = 0; c < 3; ++c) w[nw][c] = p[c] + (j ? half : -half) * v.main.r[0][c] - len * back[c] + (c == 1 ? -0.2f : 0.0f);
            }
            const float spacing = look.spacing * (0.6f + 0.4f * k);
            const float phase = float(std::fmod(t * double(s.speed) / double(spacing), 1.0));
            for (int j = 0; j < nw; ++j)
                for (int i = 0; i < look.puffs; ++i) {
                    const float d = (float(i) + phase) * spacing;
                    const std::uint32_t h = Rain::hash(v.id * 31u + std::uint32_t(j) * 7u + std::uint32_t(i + int(std::floor(t * double(s.speed) / double(spacing)))));
                    float c[3];
                    for (int q = 0; q < 3; ++q) c[q] = w[j][q] - back[q] * d;
                    c[1] += 0.12f + look.rise * d;
                    c[0] += (Rain::unit(h) - 0.5f) * 0.3f * d * 0.3f;
                    c[2] += (Rain::unit(Rain::hash(h)) - 0.5f) * 0.3f * d * 0.3f;
                    const float hs = look.size0 + look.grow * d;
                    CoronaQuad q;
                    for (int a = 0; a < 3; ++a) {
                        q.corner[0][a] = c[a] + right[a] * hs + up[a] * hs;
                        q.corner[1][a] = c[a] - right[a] * hs + up[a] * hs;
                        q.corner[2][a] = c[a] - right[a] * hs - up[a] * hs;
                        q.corner[3][a] = c[a] + right[a] * hs - up[a] * hs;
                    }
                    const float fade = (1.0f - float(i) / float(look.puffs)) * (i == 0 ? phase : 1.0f);   // (the first fades in)
                    q.alpha = std::uint8_t(std::clamp(look.alpha * k * fade, 0.0f, 127.0f));
                    const std::uint8_t g = night ? look.grey_night : look.grey_day;
                    q.rgb[0] = q.rgb[1] = g;
                    q.rgb[2] = std::uint8_t(std::min(255, int(g) + 6));
                    q.depth = std::sqrt((c[0] - eye[0]) * (c[0] - eye[0]) + (c[1] - eye[1]) * (c[1] - eye[1]) + (c[2] - eye[2]) * (c[2] - eye[2]));
                    if (q.alpha) out.push_back(q);
                }
        }
        // back to front (the mist blends)
        std::sort(out.begin(), out.end(), [](const CoronaQuad& a, const CoronaQuad& b) { return a.depth > b.depth; });
        if (!vehicles.empty()) std::swap(last_, now_);
    }
    void draw(b3gfx::Device& dev, const std::vector<CoronaQuad>& quads, b3gfx::TextureId tex, std::vector<b3gfx::DrawItem>& out) {
        layer_.draw(dev, quads, tex, out, true);
    }

private:
    struct Seen { std::uint32_t id; float pos[3]; double t; float speed; };
    QuadLayer layer_;
    std::vector<Seen> last_, now_;
};

}  // namespace b3replay

#endif
