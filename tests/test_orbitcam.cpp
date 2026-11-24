// The orbit camera in a race (orbitcam1; src/b3replay/orbitcam.h): the look's swing and its ease back, the chase camera
// turned round the car with its distance kept (left, behind; only the game's chase cameras, only with the car ahead),
// and the per-tick offsets from stamped input (none, off, blocked or not driving: 0 0, the game's view).
#include "test.h"

#include "orbitcam.h"
#include "garagecam.h"
#include "trackrays.h"

#include <cmath>
#include <array>
#include <cstring>

namespace {

using b3replay::Camera;
using b3replay::ChaseOrbit;
using b3replay::OrbitCam;
using b3replay::Tick;
using b3replay::Vehicle;

constexpr float kPi = 3.14159265358979f;
constexpr float kDt = 1.0f / 29.97f;   // a race tick (two VBlanks)

float len(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// A chase camera behind a car at the origin heading +z: the eye `back` behind and `h` above, looking at (0, aim, ahead)
Camera chase_camera(float back = 6.0f, float h = 2.0f, float aim = 0.8f, float ahead = 4.0f) {
    Camera c;
    c.valid = true;
    const float e[3] = {0.0f, h, -back};
    float at[3] = {0.0f, aim - h, ahead + back};
    const float al = len(at);
    for (float& v : at) v /= al;
    const float up0[3] = {0, 1, 0};
    float right[3] = {at[1] * up0[2] - at[2] * up0[1], at[2] * up0[0] - at[0] * up0[2], at[0] * up0[1] - at[1] * up0[0]};
    const float rl = len(right);
    for (float& v : right) v /= rl;
    const float up[3] = {right[1] * at[2] - right[2] * at[1], right[2] * at[0] - right[0] * at[2], right[0] * at[1] - right[1] * at[0]};
    for (int k = 0; k < 3; ++k) {
        c.m.r[0][k] = -right[k];   // the game's row 0 is the screen's left
        c.m.r[1][k] = up[k];
        c.m.r[2][k] = at[k];
        c.m.r[3][k] = e[k];
        c.lod_pos[k] = e[k];
    }
    c.half_w = 0.6f;
    c.half_h = 0.45f;
    return c;
}

std::vector<Vehicle> player_car() {
    Vehicle v;
    v.id = b3replay::kPlayerRaceCar;
    v.main.r[2][2] = 1.0f;   // heading +z
    return {v};
}

Tick race_tick(std::uint32_t poll) {
    Tick t;
    t.poll = poll;
    t.vblank = 2 * poll;
    t.cam = chase_camera();
    t.vehicles = player_car();
    return t;
}

}  // namespace

TEST(no_input_no_offset) {
    ChaseOrbit o;
    o.style = ChaseOrbit::kSwing;   // (orbitcam1's style)
    for (int k = 0; k < 300; ++k) CHECK(!o.step(kDt, 0.1f, -0.15f, 0, 0));   // inside the dead zone
    CHECK(!o.active() && o.yaw == 0.0f && o.pitch == 0.0f);
}

// Held right, the view turns at kYawRate; at rest it holds kIdleSeconds, then eases back to 0 over kReturnSeconds with
// no jump; held again meanwhile, it takes the view from where it is
TEST(swing_and_return) {
    ChaseOrbit o;
    o.style = ChaseOrbit::kSwing;   // (orbitcam1's style)
    int ticks = 0;
    for (float t = 0; t < 0.5f; t += kDt, ++ticks) CHECK(o.step(kDt, 1.0f, 0.0f, 0, 0));
    CHECK(std::fabs(o.yaw - float(ticks) * kDt * ChaseOrbit::kYawRate) < 1e-3f && o.yaw > 1.4f);
    const float held = o.yaw;
    for (float t = 0; t + kDt < ChaseOrbit::kIdleSeconds; t += kDt) o.step(kDt, 0.0f, 0.0f, 0, 0);
    CHECK(o.yaw == held && !o.returning);
    float prev = o.yaw;
    bool smooth = true;
    for (int k = 0; k < 40 && o.active(); ++k) {
        o.step(kDt, 0.0f, 0.0f, 0, 0);
        smooth = smooth && o.yaw <= prev && prev - o.yaw < 0.25f;
        prev = o.yaw;
    }
    CHECK(smooth && !o.active() && o.yaw == 0.0f);
    // behind: about a second of full stick left (the yaw wraps at pi; the way back the shorter one)
    for (float t = 0; t < 1.0f; t += kDt) o.step(kDt, -1.0f, 0.0f, 0, 0);
    CHECK(std::fabs(std::fabs(o.yaw) - kPi) < 0.25f);
    // taken again while easing back: no jump
    for (float t = 0; t < ChaseOrbit::kIdleSeconds + 0.2f; t += kDt) o.step(kDt, 0.0f, 0.0f, 0, 0);
    CHECK(o.returning);
    const float mid = o.yaw;
    o.step(kDt, 0.5f, 0.0f, 0, 0);
    CHECK(!o.returning && std::fabs(o.yaw - mid) < 0.1f);
}

TEST(pitch_clamped) {
    ChaseOrbit o;
    o.style = ChaseOrbit::kSwing;   // (orbitcam1's style)
    for (int k = 0; k < 120; ++k) o.step(kDt, 0.0f, 1.0f, 0, 0);
    CHECK(o.pitch == ChaseOrbit::kPitchUp);
    for (int k = 0; k < 120; ++k) o.step(kDt, 0.0f, -1.0f, 0, 0);
    CHECK(o.pitch == -ChaseOrbit::kPitchDown);
    o.reset();
    o.step(kDt, 0, 0, 0.25f, 0.0f);   // a quarter of the window dragged right: the view turns right
    CHECK(std::fabs(o.yaw - 0.25f * ChaseOrbit::kDragYaw) < 1e-5f);
}

// The drift guard: a worn stick resting just past the dead zone stops counting after kIdleSeconds
TEST(drift_guard) {
    ChaseOrbit o;
    o.style = ChaseOrbit::kSwing;   // (orbitcam1's style)
    int counted = 0;
    for (int k = 0; k < 150; ++k) counted += o.step(kDt, 0.3f, 0.0f, 0, 0) ? 1 : 0;
    CHECK(counted > 0 && counted < 30);
    CHECK(!o.active());   // and the view came back
}

// The camera turned round the car: the distance to the pivot kept, the eye to the car's right for a look left (the
// view turned left), in front of it for a look behind; the car stays in the middle of the view
TEST(turns_the_chase_camera) {
    const std::vector<Vehicle> cars = player_car();
    const Camera game = chase_camera();
    float pivot[3];
    CHECK(b3replay::orbit_pivot(game, cars[0].main, pivot));
    const float d0[3] = {game.m.r[3][0] - pivot[0], game.m.r[3][1] - pivot[1], game.m.r[3][2] - pivot[2]};
    {
        Camera c = game;
        const float look[2] = {-kPi / 2, 0.0f};   // left
        CHECK(b3replay::apply_chase_orbit(c, cars, b3replay::kPlayerRaceCar, look));
        const float d[3] = {c.m.r[3][0] - pivot[0], c.m.r[3][1] - pivot[1], c.m.r[3][2] - pivot[2]};
        CHECK(std::fabs(len(d) - len(d0)) < 1e-3f);
        // the eye on the car's right (-x for a car heading +z with the game's left row at -right: right is -x), looking
        // across it to the left (+x)
        CHECK(c.m.r[3][0] < -5.0f && std::fabs(c.m.r[3][2]) < 0.2f && c.m.r[2][0] > 0.9f);
    }
    {
        Camera c = game;
        const float look[2] = {kPi, 0.0f};   // behind
        CHECK(b3replay::apply_chase_orbit(c, cars, b3replay::kPlayerRaceCar, look));
        CHECK(c.m.r[3][2] > 5.0f && c.m.r[2][2] < -0.9f);
        CHECK(std::fabs(c.m.r[3][1] - game.m.r[3][1]) < 1e-3f);   // the same height
    }
    {
        Camera c = game;
        const float look[2] = {0.0f, 0.3f};   // up: the eye higher, still at its distance
        CHECK(b3replay::apply_chase_orbit(c, cars, b3replay::kPlayerRaceCar, look));
        const float d[3] = {c.m.r[3][0] - pivot[0], c.m.r[3][1] - pivot[1], c.m.r[3][2] - pivot[2]};
        CHECK(c.m.r[3][1] > game.m.r[3][1] + 0.5f && std::fabs(len(d) - len(d0)) < 1e-3f);
    }
    {
        Camera c = game;
        const float look[2] = {0.0f, -1.0f};   // far down: the eye no lower than the pivot
        CHECK(b3replay::apply_chase_orbit(c, cars, b3replay::kPlayerRaceCar, look));
        CHECK(c.m.r[3][1] >= pivot[1] - 1e-3f);
    }
}

// Only the game's chase cameras (Camera::chase), only with the car ahead and within reach; the other player's car or
// none: the game's view
TEST(only_the_chase_view) {
    const std::vector<Vehicle> cars = player_car();
    const float look[2] = {1.0f, 0.0f};
    Camera c = chase_camera();
    c.chase = 0.0f;   // a takedown camera
    const Camera before = c;
    CHECK(!b3replay::apply_chase_orbit(c, cars, b3replay::kPlayerRaceCar, look) && std::memcmp(&c, &before, sizeof c) == 0);
    c.chase = 0.5f;   // half way through the game's blend: half the turn
    Camera full = chase_camera();
    CHECK(b3replay::apply_chase_orbit(c, cars, b3replay::kPlayerRaceCar, look) && b3replay::apply_chase_orbit(full, cars, b3replay::kPlayerRaceCar, look));
    CHECK(std::fabs(c.m.r[3][0]) < std::fabs(full.m.r[3][0]));
    Camera bumper = chase_camera(-1.5f, 0.6f, 0.6f, 20.0f);   // the eye at the front bumper: the car behind it
    CHECK(!b3replay::apply_chase_orbit(bumper, cars, b3replay::kPlayerRaceCar, look));
    Camera other = chase_camera();
    CHECK(!b3replay::apply_chase_orbit(other, cars, b3replay::kPlayer2RaceCar, look));
    const float none[2] = {0.0f, 0.0f};
    CHECK(!b3replay::apply_chase_orbit(other, cars, b3replay::kPlayerRaceCar, none));
}

// Stamped input to per-tick offsets: none -> 0 0; the stick only while driving, enabled and not blocked; player 2's
// own stick turns view 2 only in a split-screen tick; a car page is not a race view
TEST(per_tick_offsets) {
    OrbitCam& g = OrbitCam::instance();
    g.set_style(ChaseOrbit::kSwing);   // (orbitcam1's style)
    std::uint32_t poll = 100;
    auto next = [&](Tick t) {
        g.note_poll(poll);
        t.poll = poll;
        t.vblank = 2 * poll;
        ++poll;
        g.on_tick(t);
        return t;
    };
    g.set_enabled(true);
    g.set_driving(true);
    Tick t = next(race_tick(0));
    CHECK(t.race_orbit[0][0] == 0.0f && t.race_orbit[0][1] == 0.0f);
    g.set_test_stick(1.0f, 0.0f);
    t = next(race_tick(0));
    t = next(race_tick(0));
    CHECK(t.race_orbit[0][0] > 0.0f && t.race_orbit[1][0] == 0.0f);
    g.set_test_stick(0.0f, 0.0f);
    // the pause menu (not driving): the stick does nothing, the view eases back
    g.set_driving(false);
    g.set_window_stick(0, -1.0f, 0.0f);
    for (int k = 0; k < 60; ++k) t = next(race_tick(0));
    CHECK(t.race_orbit[0][0] == 0.0f);
    g.set_driving(true);
    // blocked (PC SETTINGS / photo mode), or off: nothing
    g.set_blocked(true);
    t = next(race_tick(0));
    t = next(race_tick(0));
    CHECK(t.race_orbit[0][0] == 0.0f);
    g.set_blocked(false);
    g.set_enabled(false);
    t = next(race_tick(0));
    t = next(race_tick(0));
    CHECK(t.race_orbit[0][0] == 0.0f);
    g.set_enabled(true);
    g.set_window_stick(0, 0.0f, 0.0f);
    // player 2's stick: view 2 of a split-screen tick
    Tick split = race_tick(0);
    split.split = true;
    split.view2.valid = true;
    split.view2.cam = chase_camera();
    Vehicle p2 = player_car()[0];
    p2.id = b3replay::kPlayer2RaceCar;
    split.vehicles.push_back(p2);
    g.set_window_stick(1, 0.0f, 1.0f);
    t = next(split);
    t = next(split);
    CHECK(t.race_orbit[1][1] > 0.0f && t.race_orbit[0][1] == 0.0f);
    g.set_window_stick(1, 0.0f, 0.0f);
    // a car page: no race view, the orbit ends
    Tick page = race_tick(0);
    page.fe_car = 0x01E00000u;
    t = next(page);
    CHECK(t.race_orbit[0][0] == 0.0f && t.race_orbit[1][1] == 0.0f);
    // the mouse: a drag turns player 1's view
    g.add_drag(0.1f, 0.0f);
    t = next(race_tick(0));
    CHECK(std::fabs(t.race_orbit[0][0] - 0.1f * ChaseOrbit::kDragYaw) < 1e-4f);
}

// The field rate from the game's timer: a PAL disc's ticks (1/50 s a VBlank) turn the view at kYawRate in its seconds
TEST(field_rate_from_the_game_timer) {
    OrbitCam& g = OrbitCam::instance();
    g.set_style(ChaseOrbit::kSwing);
    g.set_enabled(true);
    g.set_driving(true);
    g.set_blocked(false);
    g.set_window_stick(0, 0.0f, 0.0f);
    g.set_window_stick(1, 0.0f, 0.0f);
    g.set_test_stick(0.0f, 0.0f);
    std::uint32_t poll = 5000, vb = 20000;
    float now = 10.0f;
    auto tick = [&](float hz) {
        Tick t = race_tick(0);
        t.poll = poll;
        t.vblank = vb;
        t.now = now;
        g.note_poll(poll);
        g.on_tick(t);
        ++poll;
        ++vb;
        now += 1.0f / hz;
        return t;
    };
    for (int k = 0; k < 40; ++k) tick(50.0f);   // at rest; the rate seen
    g.set_test_stick(1.0f, 0.0f);
    const Tick a = tick(50.0f);
    const Tick b = tick(50.0f);
    CHECK(std::fabs((b.race_orbit[0][0] - a.race_orbit[0][0]) - ChaseOrbit::kYawRate / 50.0f) < 1e-4f);
    g.set_test_stick(0.0f, 0.0f);
    for (int k = 0; k < 120; ++k) tick(59.94f);
    g.set_test_stick(1.0f, 0.0f);
    const Tick c = tick(59.94f);
    const Tick d = tick(59.94f);
    CHECK(std::fabs((d.race_orbit[0][0] - c.race_orbit[0][0]) - ChaseOrbit::kYawRate / 59.94f) < 1e-4f);
    g.set_test_stick(0.0f, 0.0f);
}

// The markers the game projected with its own camera shrink away as the view turns
TEST(marker_away) {
    const float none[2] = {0, 0}, a[2] = {0.075f, 0}, b[2] = {0, -0.5f};
    CHECK(b3replay::orbit_marker_away(none) == 0.0f);
    CHECK(std::fabs(b3replay::orbit_marker_away(a) - 0.5f) < 1e-5f && b3replay::orbit_marker_away(b) == 1.0f);
}

// orbitcam2: LOOK (the default): the stick's direction is the target (right: the view right, down: behind), reached by
// kLookEase of the rest every 1/60 s whatever the tick; held kLookHold after the stick is let go, then eased back to 0
TEST(look_style) {
    ChaseOrbit o;
    CHECK(o.style == ChaseOrbit::kLook);
    for (int k = 0; k < 10; ++k) CHECK(!o.step(1.0f / 60.0f, 0.19f, 0.0f, 0, 0));   // inside the dead zone (0.2 long)
    CHECK(!o.active());
    CHECK(o.step(1.0f / 60.0f, 1.0f, 0.0f, 0, 0));   // right: target +pi/2, 10 % of it in one 1/60 s
    CHECK(std::fabs(o.target_yaw - kPi / 2) < 1e-5f && std::fabs(o.yaw - 0.1f * kPi / 2) < 1e-4f);
    ChaseOrbit h;   // the same time in 1/30 s ticks: the same place (time-based)
    h.step(1.0f / 30.0f, 1.0f, 0.0f, 0, 0);
    ChaseOrbit q;
    q.step(1.0f / 60.0f, 1.0f, 0.0f, 0, 0);
    q.step(1.0f / 60.0f, 1.0f, 0.0f, 0, 0);
    CHECK(std::fabs(h.yaw - q.yaw) < 1e-5f);
    for (int k = 0; k < 120; ++k) o.step(1.0f / 60.0f, 0.0f, 1.0f, 0, 0);   // down: behind
    CHECK(std::fabs(std::fabs(o.yaw) - kPi) < 0.01f && o.pitch == 0.0f);
    for (int k = 0; k < 120; ++k) o.step(1.0f / 60.0f, -1.0f, 0.0f, 0, 0);   // left
    CHECK(std::fabs(o.yaw + kPi / 2) < 0.01f);
    // let go: held 0.4 s, then back
    int held = 0;
    while (!o.returning && held < 100) { o.step(1.0f / 60.0f, 0.0f, 0.0f, 0, 0); ++held; }
    CHECK(held >= 23 && held <= 25 && std::fabs(o.yaw + 0.9f * kPi / 2) < 0.01f);   // (the first step back taken)
    for (int k = 0; k < 200 && o.active(); ++k) o.step(1.0f / 60.0f, 0.0f, 0.0f, 0, 0);
    CHECK(!o.active() && o.yaw == 0.0f && !o.returning);
    // a drag: the view moves with the pointer and holds there
    o.step(1.0f / 60.0f, 0, 0, 0.1f, 0.0f);
    CHECK(std::fabs(o.yaw - 0.1f * ChaseOrbit::kDragYaw) < 1e-4f);
    o.step(1.0f / 60.0f, 0, 0, 0, 0);
    CHECK(std::fabs(o.yaw - 0.1f * ChaseOrbit::kDragYaw) < 1e-4f);
}

// The route's walls: a segment's nearest hit; floors and ceilings left out; across the grid's squares
TEST(track_rays) {
    b3replay::TrackRays r;
    std::vector<b3replay::TrackRays::Tri> tris;
    auto wall = [&](float x, float z0, float z1) {   // a wall across x = const, 0..3 m high
        tris.push_back({{x, 0, z0, x, 3, z0, x, 0, z1}});
        tris.push_back({{x, 3, z0, x, 3, z1, x, 0, z1}});
    };
    wall(5.0f, -10.0f, 10.0f);
    wall(9.0f, -10.0f, 10.0f);
    const b3replay::TrackRays::Tri floor{{-20, 0.5f, -20, 20, 0.5f, -20, 0, 0.5f, 20}};
    CHECK(!b3replay::TrackRays::keep(floor.v) && b3replay::TrackRays::keep(tris[0].v));
    std::vector<b3replay::TrackRays::Tri> kept;
    for (const auto& t : tris) if (b3replay::TrackRays::keep(t.v)) kept.push_back(t);
    if (b3replay::TrackRays::keep(floor.v)) kept.push_back(floor);
    r.build(kept);
    const float a[3] = {0.0f, 1.0f, 0.5f}, b[3] = {20.0f, 1.0f, 0.5f}, c[3] = {4.0f, 1.0f, 0.5f};
    float t = -1;
    CHECK(r.segment(a, b, &t) && std::fabs(t - 0.25f) < 1e-4f);   // the nearer wall (x 5)
    CHECK(r.segment(b, a, &t) && std::fabs(t - 0.55f) < 1e-4f);   // from the other side: x 9
    CHECK(!r.segment(a, c, &t));                                   // short of it
    const float up[3] = {2.0f, 5.0f, 0.5f}, down[3] = {2.0f, -2.0f, 0.5f};
    CHECK(!r.segment(up, down, &t));                               // the floor is not a wall
}

// The eye stops kWallMargin before the wall between it and the point kRayHeight above the car (a look left with a wall
// 3 m to the car's right); none without walls; at the game's view nothing moves
TEST(stops_before_walls) {
    const std::vector<Vehicle> cars = player_car();
    b3replay::TrackRays r;
    std::vector<b3replay::TrackRays::Tri> tris;
    const float x = -3.0f;   // the car's right (heading +z: right is -x)
    tris.push_back({{x, -1, -10, x, 5, -10, x, -1, 10}});
    tris.push_back({{x, 5, -10, x, 5, 10, x, -1, 10}});
    r.build(tris);
    const float left[2] = {-kPi / 2, 0.0f};
    Camera through = chase_camera(), stopped = chase_camera();
    CHECK(b3replay::apply_chase_orbit(through, cars, b3replay::kPlayerRaceCar, left));
    CHECK(through.m.r[3][0] < -5.0f);   // without walls: beyond the wall
    const float from[3] = {0.0f, b3replay::kRayHeight, 0.0f};
    const float wall = b3replay::wall_distance(r, from, through.m.r[3]);
    const float d[3] = {through.m.r[3][0] - from[0], through.m.r[3][1] - from[1], through.m.r[3][2] - from[2]};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    CHECK(wall > 1.0f && wall < len);
    b3replay::OrbitDraw dr;
    dr.wall = wall;
    CHECK(b3replay::apply_chase_orbit(stopped, cars, b3replay::kPlayerRaceCar, left, dr));
    CHECK(stopped.m.r[3][0] > x + 0.3f && stopped.m.r[3][0] < x + 0.5f);   // 0.4 m short of it
    CHECK(std::memcmp(stopped.m.r[2], through.m.r[2], sizeof through.m.r[2]) == 0);   // the same direction
    // a small look (inside kOrbitMarkerFade): only that share of the pull, so the game's view comes back smoothly
    Camera small = chase_camera(), small_free = chase_camera();
    const float little[2] = {-0.075f, 0.0f};
    b3replay::OrbitDraw near;
    near.wall = 2.0f;
    b3replay::apply_chase_orbit(small, cars, b3replay::kPlayerRaceCar, little, near);
    b3replay::apply_chase_orbit(small_free, cars, b3replay::kPlayerRaceCar, little);
    const float dz = small.m.r[3][2] - small_free.m.r[3][2];
    CHECK(dz > 0.1f);   // pulled in part of the way (half: 0.075 of kOrbitMarkerFade 0.15)
    // nothing in the way: the whole way; a wall hugging the car: no nearer than kMinEyeDistance
    const float open[3] = {6.0f, 1.0f, 0.0f};
    CHECK(std::fabs(b3replay::wall_distance(r, from, open) - std::sqrt(36.0f)) < 1e-4f);
    const float close[3] = {-2.7f, 1.0f, 0.0f}, beyond[3] = {-6.0f, 1.0f, 0.0f};   // the wall 0.3 m from the start
    CHECK(b3replay::wall_distance(r, close, beyond) == b3replay::kMinEyeDistance);
}

// The wall distance's easing: a nearer wall at once, a farther one over kWallOutSeconds (no pumping past posts)
TEST(wall_easing) {
    CHECK(b3replay::ease_wall(-1.0f, 5.0f, 0.016f) == 5.0f);   // the first: as it is
    CHECK(b3replay::ease_wall(5.0f, 2.0f, 0.016f) == 2.0f);    // nearer: at once
    float d = 2.0f;
    for (int k = 0; k < 6; ++k) d = b3replay::ease_wall(d, 5.0f, 1.0f / 60.0f);   // a post passed: out slowly
    CHECK(d > 2.0f && d < 3.0f);
    for (int k = 0; k < 240; ++k) d = b3replay::ease_wall(d, 5.0f, 1.0f / 60.0f);
    CHECK(std::fabs(d - 5.0f) < 0.01f);
}

// A long wall (a triangle across many grid squares) stops the ray anywhere along it; a dense square tests each
// triangle once a query (the stamps)
TEST(long_walls) {
    b3replay::TrackRays r;
    std::vector<b3replay::TrackRays::Tri> tris;
    tris.push_back({{-400, 0, 3, 400, 0, 3, -400, 6, 3}});   // a 800 m wall along z = 3, 6 m high (one triangle)
    tris.push_back({{400, 0, 3, 400, 6, 3, -400, 6, 3}});
    for (int k = 0; k < 2000; ++k) tris.push_back({{0.5f, 0.0f, 0.5f + 0.001f * float(k), 1.5f, 0.0f, 0.5f, 0.5f, 3.0f, 0.5f}});
    r.build(tris);
    float t;
    for (const float x : {-350.0f, -3.0f, 0.0f, 120.0f, 390.0f}) {
        const float a[3] = {x, 1, 0}, b[3] = {x, 1, 6};
        CHECK(r.segment(a, b, &t) && std::fabs(t - 0.5f) < 1e-4f);
    }
    const float a[3] = {0.8f, 1, 0}, b[3] = {0.8f, 1, 1};
    CHECK(r.segment(a, b, &t) && std::fabs(t - 0.5f) < 1e-3f);   // through the dense square (its triangles each tested once)
}

// MIRROR MODE (gameopts): the stick's x is the picture's, the world's the other way; LOOK takes it too
TEST(look_mirrored) {
    OrbitCam& g = OrbitCam::instance();
    g.set_style(ChaseOrbit::kLook);
    g.set_enabled(true);
    g.set_driving(true);
    g.set_blocked(false);
    g.set_window_stick(0, 0.0f, 0.0f);
    g.set_window_stick(1, 0.0f, 0.0f);
    std::uint32_t poll = 9000;
    float last = 0;
    for (int mirror = 0; mirror < 2; ++mirror) {
        g.set_test_stick(0.0f, 0.0f);
        for (int k = 0; k < 200; ++k) {   // back to the game's view
            Tick t = race_tick(0);
            t.poll = poll; t.vblank = 2 * poll; t.race_time.game_options = std::uint16_t(mirror);
            g.note_poll(poll++); g.on_tick(t);
        }
        g.set_test_stick(1.0f, 0.0f);   // right on screen
        for (int k = 0; k < 30; ++k) {
            Tick t = race_tick(0);
            t.poll = poll; t.vblank = 2 * poll; t.race_time.game_options = std::uint16_t(mirror);
            g.note_poll(poll++); g.on_tick(t);
            last = t.race_orbit[0][0];
        }
        CHECK(mirror ? last < -0.5f : last > 0.5f);   // the world's right, or (mirrored) its left
    }
    g.set_test_stick(0.0f, 0.0f);
}

// The walls per tick: the eased distance in Tick::race_wall while the view is turned, -1 at the game's view
TEST(walls_per_tick) {
    OrbitCam& g = OrbitCam::instance();
    auto rays = std::make_shared<b3replay::TrackRays>();
    std::vector<b3replay::TrackRays::Tri> tris;
    tris.push_back({{-3, -1, -10, -3, 5, -10, -3, -1, 10}});
    tris.push_back({{-3, 5, -10, -3, 5, 10, -3, -1, 10}});
    rays->build(tris);
    g.set_rays_source([rays] { return std::shared_ptr<const b3replay::TrackRays>(rays); });
    g.set_style(ChaseOrbit::kLook);
    g.set_test_stick(0.0f, 0.0f);
    std::uint32_t poll = 20000;
    auto tick = [&] {
        Tick t = race_tick(0);
        t.poll = poll; t.vblank = 2 * poll;
        g.note_poll(poll++); g.on_tick(t);
        return t;
    };
    for (int k = 0; k < 200; ++k) tick();
    Tick t = tick();
    CHECK(t.race_wall[0] == -1.0f);   // the game's view: none
    g.set_test_stick(-1.0f, 0.0f);    // look left: the eye goes to the car's right, where the wall is
    for (int k = 0; k < 120; ++k) t = tick();
    CHECK(t.race_wall[0] > 1.0f && t.race_wall[0] < 4.0f);
    g.set_test_stick(0.0f, 0.0f);
    for (int k = 0; k < 300; ++k) t = tick();
    CHECK(t.race_wall[0] == -1.0f);
    g.set_rays_source(nullptr);
}

// CAMERA ROLL's hook (betacam): the lean kept as the view turns
TEST(roll_keep) {
    CHECK(b3replay::orbit_roll_keep(0.0f) == 1.0f && b3replay::orbit_roll_keep(1.0f) == 0.0f && b3replay::orbit_roll_keep(0.25f) == 0.75f);
    CHECK(b3replay::orbit_roll_keep(-1.0f) == 1.0f && b3replay::orbit_roll_keep(2.0f) == 0.0f);
}

// LOOK's front zoom: the field of view up to kFrontZoomDeg narrower as the eye goes round to the front
TEST(front_zoom) {
    const std::vector<Vehicle> cars = player_car();
    b3replay::OrbitDraw d;
    d.front_zoom = true;
    const float behind[2] = {kPi, 0.0f}, side[2] = {kPi / 2, 0.0f};
    Camera c = chase_camera(), s = chase_camera();
    const float fov0 = 2.0f * std::atan(c.half_w);
    CHECK(b3replay::apply_chase_orbit(c, cars, b3replay::kPlayerRaceCar, behind, d));
    CHECK(std::fabs((fov0 - 2.0f * std::atan(c.half_w)) - b3replay::kFrontZoomDeg * kPi / 180.0f) < 1e-3f);
    CHECK(std::fabs(c.half_h / c.half_w - 0.45f / 0.6f) < 1e-5f);
    CHECK(b3replay::apply_chase_orbit(s, cars, b3replay::kPlayerRaceCar, side, d));
    CHECK(std::fabs(s.half_w - 0.6f) < 1e-5f);   // at the side: none
}

// camroll1 (orbitcam.h ChaseRoll): the chase camera leans into a corner (a right turn: the screen-left row towards up),
// eased, at most max_deg; a cut levels it; no game time holds it
namespace {
void yaw_rows(Camera& c, float a) {   // the camera turned by a about the vertical (positive: to the screen's right)
    const float cs = std::cos(a), sn = std::sin(a);
    for (int r = 0; r < 4; ++r) {
        const float x = c.m.r[r][0], z = c.m.r[r][2];
        c.m.r[r][0] = x * cs + z * sn;
        c.m.r[r][2] = -x * sn + z * cs;
    }
}
}  // namespace

TEST(roll_leans_into_corners) {
    using b3replay::ChaseRoll;
    for (int dir : {1, -1}) {
        ChaseRoll r;
        Camera c = chase_camera();
        // the screen's right is -r[0]; turning the at row towards it is a right turn
        const float sx = -c.m.r[0][0], sz = -c.m.r[0][2];
        const float a = 0.6f * kDt * float(dir) * (sx * c.m.r[2][2] - sz * c.m.r[2][0] >= 0.0f ? 1.0f : -1.0f);
        float last = 0.0f;
        for (int k = 0; k < 400; ++k) {
            r.step(kDt, c.m.r[2], c.m.r[0], ChaseRoll::kGain[2], ChaseRoll::kMaxDeg[2]);
            yaw_rows(c, a);
            if (k == 10) CHECK(std::fabs(r.roll) > 0.0f && std::fabs(r.roll) < ChaseRoll::kMaxDeg[2]);   // eased, not at once
            last = r.roll;
        }
        CHECK(std::fabs(std::fabs(last) - ChaseRoll::kMaxDeg[2]) < 0.05f);   // 0.6 rad/s (of the at row sideways about 0.55) x 20 > 8: at the most
        CHECK((dir > 0) == (last > 0.0f));                                    // a right turn: positive
        // no game time (the pause): held
        const float held = r.roll;
        yaw_rows(c, a);
        r.step(0.0f, c.m.r[2], c.m.r[0], ChaseRoll::kGain[2], ChaseRoll::kMaxDeg[2]);
        CHECK(r.roll == held);
        // a cut: level again
        yaw_rows(c, 1.5f);
        r.step(kDt, c.m.r[2], c.m.r[0], ChaseRoll::kGain[2], ChaseRoll::kMaxDeg[2]);
        CHECK(r.roll == 0.0f);
        // straight on: eases back to level
        for (int k = 0; k < 600; ++k) r.step(kDt, c.m.r[2], c.m.r[0], ChaseRoll::kGain[1], ChaseRoll::kMaxDeg[1]);
        CHECK(std::fabs(r.roll) < 1e-3f);
    }
}

TEST(roll_turns_the_drawn_camera) {
    Camera c = chase_camera();
    const Camera g = c;
    b3replay::roll_camera(c, 8.0f);
    float dot_ru = 0, dot_ra = 0, dot_ua = 0, d_at = 0, d_pos = 0;
    for (int k = 0; k < 3; ++k) {
        dot_ru += c.m.r[0][k] * c.m.r[1][k];
        dot_ra += c.m.r[0][k] * c.m.r[2][k];
        dot_ua += c.m.r[1][k] * c.m.r[2][k];
        d_at += std::fabs(c.m.r[2][k] - g.m.r[2][k]);
        d_pos += std::fabs(c.m.r[3][k] - g.m.r[3][k]);
    }
    CHECK(std::fabs(dot_ru) < 1e-5f && std::fabs(dot_ra) < 1e-5f && std::fabs(dot_ua) < 1e-5f && d_at == 0.0f && d_pos == 0.0f);
    CHECK(std::fabs(len(c.m.r[0]) - 1.0f) < 1e-5f && std::fabs(len(c.m.r[1]) - 1.0f) < 1e-5f);
    // right (screen left) towards up by 8 degrees
    float ru = 0;
    for (int k = 0; k < 3; ++k) ru += c.m.r[0][k] * g.m.r[1][k];
    CHECK(std::fabs(ru - std::sin(8.0f * kPi / 180.0f)) < 1e-5f);
    Camera z = g;
    b3replay::roll_camera(z, 0.0f);
    CHECK(std::memcmp(&z.m, &g.m, sizeof z.m) == 0);
}

TEST(roll_marker_away) {
    CHECK(b3replay::roll_marker_away(0.0f) == 0.0f && std::fabs(b3replay::roll_marker_away(-0.5f) - 0.5f) < 1e-6f);
    CHECK(b3replay::roll_marker_away(8.0f) == 1.0f && b3replay::roll_marker_away(-1.0f) == 1.0f);
}

TEST(roll_per_tick) {
    OrbitCam& g = OrbitCam::instance();
    std::uint32_t poll = 5000;
    float now = 10.0f;
    Camera c = chase_camera();
    auto next = [&](float share, bool turn) {
        Tick t = race_tick(poll++);
        now += kDt;
        t.now = now;
        if (turn) yaw_rows(c, 0.4f * kDt);
        t.cam = c;
        t.cam.chase = share;
        g.on_tick(t);
        return t;
    };
    g.set_roll(0);   // Off (the default): none
    Tick t;
    for (int k = 0; k < 30; ++k) t = next(1.0f, true);
    CHECK(t.race_roll[0] == 0.0f && t.race_roll[1] == 0.0f);
    g.set_roll(1);
    for (int k = 0; k < 300; ++k) t = next(1.0f, true);
    CHECK(std::fabs(std::fabs(t.race_roll[0]) - 4.0f) < 0.05f && t.race_roll[1] == 0.0f);   // Subtle: at most 4
    t = next(0.5f, true);
    CHECK(std::fabs(std::fabs(t.race_roll[0]) - 2.0f) < 0.1f);   // a blend to another camera: in step
    t = next(0.0f, true);
    CHECK(t.race_roll[0] == 0.0f);                                // not the chase camera: the game's
    t = next(1.0f, true);
    CHECK(std::fabs(t.race_roll[0]) < 0.5f);                      // back: from level again
    g.set_roll(0);
}

TEST_MAIN("orbit camera (orbitcam1, orbitcam2, camroll1)")
