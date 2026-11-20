// The garage camera (garagecam1; src/b3replay/garagecam.h): the orbit's hold / resume, its clamps, the camera turned
// round the car with its distance kept, and the per-tick offsets from stamped input (none: 0 0, the game's view).
#include "test.h"

#include "garagecam.h"

#include <algorithm>
#include <cmath>

namespace {

using b3replay::Camera;
using b3replay::GarageCam;
using b3replay::GarageOrbit;
using b3replay::Mat34;
using b3replay::Tick;
using b3replay::Vehicle;

constexpr float kPi = 3.14159265358979f;
constexpr std::uint32_t kCar = 0x01E00000u;

float len(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float wrap(float a) { return std::remainder(a, 2.0f * kPi); }

// A car page's camera: on a circle of radius `r` at height `h` round the origin, at bearing `bearing` (atan2(x, z)),
// looking at (0, `aim`, 0); rows screen left, up, at, position (the game's convention: the right row is the screen's left)
Camera page_camera(float bearing, float r = 6.0f, float h = 1.6f, float aim = 0.6f) {
    Camera c;
    c.valid = true;
    const float e[3] = {r * std::sin(bearing), h, r * std::cos(bearing)};
    float at[3] = {-e[0], aim - e[1], -e[2]};
    const float al = len(at);
    for (float& v : at) v /= al;
    const float up0[3] = {0, 1, 0};
    // screen right = at x up; the row is the screen's left
    float right[3] = {at[1] * up0[2] - at[2] * up0[1], at[2] * up0[0] - at[0] * up0[2], at[0] * up0[1] - at[1] * up0[0]};
    const float rl = len(right);
    for (float& v : right) v /= rl;
    const float up[3] = {right[1] * at[2] - right[2] * at[1], right[2] * at[0] - right[0] * at[2], right[0] * at[1] - right[1] * at[0]};
    for (int k = 0; k < 3; ++k) {
        c.m.r[0][k] = -right[k];
        c.m.r[1][k] = up[k];
        c.m.r[2][k] = at[k];
        c.m.r[3][k] = e[k];
        c.lod_pos[k] = e[k];
    }
    c.half_w = 0.6f;
    c.half_h = 0.45f;
    return c;
}

Tick car_tick(std::uint32_t poll, float bearing) {
    Tick t;
    t.poll = poll;
    t.vblank = 2 * poll;
    t.cam = page_camera(bearing);
    t.fe_car = kCar;
    Vehicle v;
    v.id = kCar;
    t.vehicles.push_back(v);
    return t;
}

}  // namespace

TEST(dead_zone) {
    CHECK(GarageOrbit::dead(0.0f) == 0.0f && GarageOrbit::dead(0.17f) == 0.0f && GarageOrbit::dead(-0.18f) == 0.0f);
    CHECK(GarageOrbit::dead(1.0f) == 1.0f && GarageOrbit::dead(-1.0f) == -1.0f);
    CHECK(GarageOrbit::dead(0.59f) > 0.49f && GarageOrbit::dead(0.59f) < 0.51f);
}

TEST(no_input_no_offset) {
    GarageOrbit o;
    for (int k = 0; k < 600; ++k) o.step(1.0f / 30.0f, 0.1f, -0.15f, 0, 0, wrap(0.01f * float(k)), 0.2f);   // inside the dead zone
    CHECK(!o.engaged && o.yaw == 0.0f && o.pitch == 0.0f);
}

// The view's angle round the car is the game's plus the yaw: held while the stick is used, still for a second after,
// then the game's spin eased back in, with no jump anywhere
TEST(hold_then_resume_without_a_snap) {
    GarageOrbit o;
    const float dt = 1.0f / 30.0f, spin = 0.5f;   // the game's spin, rad/s
    float game = 0.0f, prev_view = 0.0f, max_step = 0.0f;
    auto tick = [&](float sx) {
        game = wrap(game + spin * dt);
        o.step(dt, sx, 0, 0, 0, game, 0.2f);
        const float view = wrap(game + o.yaw);
        max_step = std::max(max_step, std::fabs(wrap(view - prev_view)));
        prev_view = view;
        return view;
    };
    for (int k = 0; k < 30; ++k) tick(0);   // the game's spin as it is
    CHECK(!o.engaged && o.yaw == 0.0f);
    max_step = 0;
    const float v0 = prev_view;
    for (int k = 0; k < 15; ++k) tick(1.0f);   // half a second, full right: the view turns by the stick alone
    CHECK(o.engaged);
    CHECK(std::fabs(wrap(prev_view - v0) - 15 * dt * GarageOrbit::kYawRate) < 1e-3f);
    const float v1 = prev_view;
    for (int k = 0; k < 29; ++k) tick(0);   // < 1 s at rest: still held
    CHECK(std::fabs(wrap(prev_view - v1)) < 1e-4f && o.resume == 0.0f);
    for (int k = 0; k < 3; ++k) tick(0);    // the second passes: the spin begins again, eased
    CHECK(o.resume > 0.0f && o.resume < 1.0f);
    for (int k = 0; k < 40; ++k) tick(0);
    CHECK(o.resume == 1.0f);
    const float y = o.yaw;
    for (int k = 0; k < 30; ++k) tick(0);   // resumed: the offset stays, the view turns with the game
    CHECK(std::fabs(o.yaw - y) < 1e-5f);
    CHECK(max_step <= std::max(spin, GarageOrbit::kYawRate) * dt + 1e-4f);   // never a jump
    // the stick again: held at once
    const float v2 = prev_view;
    tick(0.0f);
    o.step(dt, 0.5f, 0, 0, 0, game, 0.2f);
    CHECK(o.resume == 0.0f);
    (void)v2;
}

// The drift guard: a stick resting still just past the dead zone holds the view for the resume delay only, then the
// game's spin comes back; moving it, or pushing it firmly, counts again
TEST(drift_guard) {
    const float dt = 1.0f / 30.0f, spin = 0.5f;
    GarageOrbit o;
    float game = 0.0f;
    auto tick = [&](float sx, float sy = 0.0f) {
        game = wrap(game + spin * dt);
        o.step(dt, sx, sy, 0, 0, game, 0.2f);
    };
    for (int k = 0; k < 15; ++k) tick(0.25f);   // a resting thumb (or a worn stick) at 0.25
    CHECK(o.engaged && o.resume == 0.0f);
    for (int k = 0; k < 45; ++k) tick(0.25f + 0.01f * float(k & 1));   // two seconds still (jitter under kMoveDelta)
    for (int k = 0; k < 75; ++k) tick(0.26f);
    CHECK(o.resume == 1.0f);   // stopped counting after a second: the spin is back in full
    const float y = o.yaw;
    for (int k = 0; k < 30; ++k) tick(0.25f);
    CHECK(std::fabs(o.yaw - y) < 1e-5f);   // and the drifting stick no longer turns the view
    tick(0.35f);   // it moves: counts again at once
    CHECK(o.resume == 0.0f && o.idle == 0.0f);
    // a firm push (past kFirm) counts however long it is held still
    GarageOrbit f;
    for (int k = 0; k < 150; ++k) f.step(dt, 0.0f, 0.8f, 0, 0, 0.0f, 0.2f);
    CHECK(f.engaged && f.resume == 0.0f && f.idle == 0.0f);
    // a slow, deliberate turn at a small deflection keeps counting while it moves
    GarageOrbit m;
    float y0 = 0.0f;
    for (int k = 0; k < 120; ++k) {
        m.step(dt, 0.25f + 0.002f * float(k), 0.0f, 0, 0, 0.0f, 0.2f);
        if (k == 0) y0 = m.yaw;
    }
    CHECK(m.resume == 0.0f && m.yaw > y0);
    // inside the dead zone nothing, as before
    GarageOrbit z;
    for (int k = 0; k < 60; ++k) z.step(dt, 0.17f, 0.0f, 0, 0, 0.0f, 0.2f);
    CHECK(!z.engaged);
}

TEST(stick_direction_follows_the_screen) {
    GarageOrbit a, b;
    a.step(0.1f, 1.0f, 0, 0, 0, 0, 0.2f, 1.0f);
    b.step(0.1f, 1.0f, 0, 0, 0, 0, 0.2f, -1.0f);
    CHECK(a.yaw > 0.0f && b.yaw < 0.0f && std::fabs(a.yaw + b.yaw) < 1e-6f);
    GarageOrbit d;   // a drag right turns the car with the pointer: the camera the other way
    d.step(0.1f, 0, 0, 0.1f, 0, 0, 0.2f, 1.0f);
    CHECK(d.yaw < 0.0f && std::fabs(d.yaw + 0.1f * GarageOrbit::kDragYaw) < 1e-5f);
}

TEST(a_cut_is_not_held) {
    GarageOrbit o;
    o.step(0.1f, 1.0f, 0, 0, 0, 0.0f, 0.2f);
    const float y = o.yaw;
    o.step(0.1f, 0, 0, 0, 0, 2.0f, 0.2f);   // the game's camera jumps (a new car's): shown as the game makes it
    CHECK(std::fabs(o.yaw - y) < 1e-6f);
}

TEST(pitch_clamped) {
    GarageOrbit o;
    for (int k = 0; k < 200; ++k) o.step(1.0f / 30.0f, 0, 1.0f, 0, 0, 0, 0.2f);   // stick down: up, to the limit
    CHECK(std::fabs(o.pitch - GarageOrbit::kPitchUp) < 1e-5f);
    for (int k = 0; k < 200; ++k) o.step(1.0f / 30.0f, 0, -1.0f, 0, 0, 0, 0.2f);   // up: down, to the game's own height
    CHECK(GarageOrbit::kPitchDown == 0.0f && std::fabs(o.pitch) < 1e-6f);
    GarageOrbit cap;   // the eye's height cap (eye_cap_elevation) below the angle limits
    for (int k = 0; k < 200; ++k) cap.step(1.0f / 30.0f, 0, 1.0f, 0, 0, 0, 0.12f, 1.0f, 0.33f);
    CHECK(std::fabs(0.12f + cap.pitch - 0.33f) < 1e-5f);
    GarageOrbit hi;   // a high one: never over kMaxElevation
    for (int k = 0; k < 200; ++k) hi.step(1.0f / 30.0f, 0, 1.0f, 0, 0, 0, 0.6f);
    CHECK(std::fabs(0.6f + hi.pitch - GarageOrbit::kMaxElevation) < 1e-5f);
    GarageOrbit out;   // a game camera above the range: no offset pushes it higher, down still works
    for (int k = 0; k < 10; ++k) out.step(1.0f / 30.0f, 0, 1.0f, 0, 0, 0, 0.9f);
    CHECK(out.pitch == 0.0f);
}

TEST(orbit_keeps_the_distance_and_the_aim) {
    Mat34 car;
    const Camera c0 = page_camera(0.7f);
    float pivot[3];
    CHECK(b3replay::orbit_pivot(c0, car, pivot));
    CHECK(std::fabs(pivot[0]) < 1e-6f && std::fabs(pivot[2]) < 1e-6f && std::fabs(pivot[1] - 0.6f) < 1e-4f);
    const float e0 = b3replay::camera_elevation(c0, pivot), a0 = b3replay::camera_angle(c0, car);
    CHECK(std::fabs(a0 - 0.7f) < 1e-5f);
    Camera c = c0;
    b3replay::orbit_camera(c, pivot, 0.9f, 0.25f);
    float d0[3], d1[3];
    for (int k = 0; k < 3; ++k) {
        d0[k] = c0.m.r[3][k] - pivot[k];
        d1[k] = c.m.r[3][k] - pivot[k];
    }
    CHECK(std::fabs(len(d0) - len(d1)) < 1e-4f);   // no zoom
    CHECK(std::fabs(b3replay::camera_angle(c, car) - wrap(0.7f + 0.9f)) < 1e-4f);
    CHECK(std::fabs(b3replay::camera_elevation(c, pivot) - (e0 + 0.25f)) < 1e-4f);
    // still aimed at the pivot, the rows orthonormal
    const float to[3] = {-d1[0] / len(d1), -d1[1] / len(d1), -d1[2] / len(d1)};
    CHECK(dot(to, c.m.r[2]) > 0.9999f);
    for (int i = 0; i < 3; ++i) {
        CHECK(std::fabs(len(c.m.r[i]) - 1.0f) < 1e-4f);
        for (int j = i + 1; j < 3; ++j) CHECK(std::fabs(dot(c.m.r[i], c.m.r[j])) < 1e-4f);
    }
    CHECK(std::fabs(c.m.r[0][1]) < 1e-4f);   // level: no roll
    for (int k = 0; k < 3; ++k) CHECK(std::fabs(c.lod_pos[k] - c.m.r[3][k]) < 1e-4f);
    // a positive yaw with yaw_right's sign moves the eye to the screen's right (-r[0])
    const float s = b3replay::yaw_right(c0, pivot);
    Camera m = c0;
    b3replay::orbit_camera(m, pivot, 0.05f * s, 0.0f);
    const float mv[3] = {m.m.r[3][0] - c0.m.r[3][0], 0, m.m.r[3][2] - c0.m.r[3][2]};
    const float right[3] = {-c0.m.r[0][0], -c0.m.r[0][1], -c0.m.r[0][2]};
    CHECK(dot(mv, right) > 0.0f);
}

// The front end's room: the game's camera ~7.6 m from the car at 1.145 m, the car at 0.31 m: the eye never over 2.71 m
TEST(eye_height_cap) {
    Mat34 car;
    car.r[3][1] = 0.312f;
    Camera c = page_camera(1.0f, 7.5f, 1.145f, 0.312f);
    float pivot[3];
    CHECK(b3replay::orbit_pivot(c, car, pivot));
    GarageOrbit o;
    const float e = b3replay::camera_elevation(c, pivot), cap = b3replay::eye_cap_elevation(c, pivot, car.r[3][1]);
    CHECK(cap > e && cap < e + GarageOrbit::kPitchUp);
    for (int k = 0; k < 200; ++k) o.step(1.0f / 30.0f, 0, 1.0f, 0, 0, b3replay::camera_angle(c, car), e, 1.0f, cap);
    Camera v = c;
    b3replay::orbit_camera(v, pivot, o.yaw, o.pitch);
    CHECK(std::fabs(v.m.r[3][1] - (0.312f + GarageOrbit::kMaxEyeAboveCar)) < 1e-3f);
    for (int k = 0; k < 200; ++k) o.step(1.0f / 30.0f, 0, -1.0f, 0, 0, b3replay::camera_angle(c, car), e, 1.0f, cap);
    v = c;
    b3replay::orbit_camera(v, pivot, o.yaw, o.pitch);
    CHECK(std::fabs(v.m.r[3][1] - 1.145f) < 1e-4f);   // never under the game's own
}

TEST(lerp_the_short_way) {
    const float a[2] = {3.1f, 0.1f}, b[2] = {-3.1f, 0.3f};
    float o[2];
    b3replay::orbit_lerp(a, b, 0.5f, o);
    CHECK(std::fabs(std::fabs(o[0]) - kPi) < 1e-3f && std::fabs(o[1] - 0.2f) < 1e-6f);
}

// Stamped input to the ticks' offsets: none before the stick, held after it, ended off the car pages
TEST(ticks_from_stamped_input) {
    GarageCam& g = GarageCam::instance();
    g.set_enabled(true);
    float bearing = 0.0f;
    std::uint32_t poll = 100;
    auto next = [&](float spin = 0.02f) {
        bearing = wrap(bearing + spin);
        Tick t = car_tick(poll, bearing);
        g.note_poll(poll);
        g.on_tick(t);
        ++poll;
        return t;
    };
    for (int k = 0; k < 20; ++k) {
        const Tick t = next();
        CHECK(t.fe_orbit[0] == 0.0f && t.fe_orbit[1] == 0.0f);
    }
    CHECK(g.car_page());
    g.set_test_stick(-1.0f, 0.0f);   // left, 10 polls
    float view0 = 0;
    for (int k = 0; k < 10; ++k) {
        const Tick t = next();
        if (k == 0) view0 = wrap(bearing + t.fe_orbit[0]);
    }
    g.set_test_stick(0.0f, 0.0f);
    Tick t = next();
    const float view1 = wrap(bearing + t.fe_orbit[0]);
    CHECK(t.fe_orbit[0] != 0.0f && std::fabs(wrap(view1 - view0)) > 0.3f);
    for (int k = 0; k < 20; ++k) t = next();   // held: the view still
    CHECK(std::fabs(wrap(bearing + t.fe_orbit[0] - view1)) < 1e-4f);
    g.set_enabled(false);   // GARAGE CAMERA off: nothing taken, the orbit ends
    g.set_test_stick(1.0f, 1.0f);
    t = next();
    CHECK(t.fe_orbit[0] == 0.0f && t.fe_orbit[1] == 0.0f && !g.car_page());
    g.set_test_stick(0.0f, 0.0f);
    g.set_enabled(true);
    t = next();
    CHECK(t.fe_orbit[0] == 0.0f && t.fe_orbit[1] == 0.0f && g.car_page());
    // off the car pages: 0 0, and the next visit starts from the game's view
    g.set_test_stick(1.0f, 0.0f);
    t = next();
    CHECK(t.fe_orbit[0] != 0.0f);
    g.set_test_stick(0.0f, 0.0f);
    Tick off;
    off.poll = poll;
    off.vblank = 2 * poll;
    g.note_poll(poll++);
    g.on_tick(off);
    CHECK(off.fe_orbit[0] == 0.0f && !g.car_page());
    t = next();
    CHECK(t.fe_orbit[0] == 0.0f && t.fe_orbit[1] == 0.0f);
}

TEST_MAIN("garage camera (garagecam1)")
