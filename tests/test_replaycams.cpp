// The replay viewer's cameras (replaycams; src/b3replay/replaycams.h): each angle's eye and aim from a car's frame, the
// eased heading, Orbit's turn, TV's placement ahead and again once the car is past, the zoom that keeps the car's size.
#include "test.h"

#include "replaycams.h"

#include <cmath>

namespace {

using b3replay::Mat34;
using b3replay::ReplayAngle;
using b3replay::ReplayView;

// a car at `x, z` heading +Z (rows X, Y, Z, position)
Mat34 car_at(float x, float z) {
    Mat34 m;
    m.r[3][0] = x, m.r[3][1] = 10.0f, m.r[3][2] = z;
    return m;
}

float dist(const float* a, const float* b) {
    float s = 0;
    for (int k = 0; k < 3; ++k) s += (a[k] - b[k]) * (a[k] - b[k]);
    return std::sqrt(s);
}

}  // namespace

TEST(chase_and_far_chase_trail_the_car) {
    ReplayView v;
    v.step(ReplayAngle::kChase, 1, 0.0f, car_at(0, 0));
    CHECK(std::fabs(v.eye[2] + ReplayView::kChaseBack) < 1e-4f && std::fabs(v.eye[1] - (10.0f + ReplayView::kChaseUp)) < 1e-4f);
    CHECK(v.look[2] > 0 && v.zoom == 1.0f);   // aimed ahead of the car
    v.step(ReplayAngle::kFarChase, 1, 1.0f / 60, car_at(0, 0.5f));
    CHECK(std::fabs(v.eye[2] - (0.5f - ReplayView::kFarBack)) < 1e-3f && std::fabs(v.eye[1] - (10.0f + ReplayView::kFarUp)) < 1e-4f);
}

TEST(the_heading_eases_on_a_turn) {
    ReplayView v;
    v.step(ReplayAngle::kChase, 1, 0.0f, car_at(0, 0));
    Mat34 turned = car_at(0, 0);   // the car's nose now +X
    turned.r[0][0] = 0, turned.r[0][2] = -1, turned.r[2][0] = 1, turned.r[2][2] = 0;
    v.step(ReplayAngle::kChase, 1, 1.0f / 60, turned);
    CHECK(v.heading[2] > 0.8f && v.heading[0] > 0.0f);   // a sixtieth of a second: still mostly the old heading
    for (int i = 0; i < 120; ++i) v.step(ReplayAngle::kChase, 1, 1.0f / 60, turned);
    CHECK(v.heading[0] > 0.99f);   // two seconds: the new one
}

TEST(bumper_rides_the_car) {
    ReplayView v;
    v.step(ReplayAngle::kBumper, 1, 0.0f, car_at(5, 5));
    CHECK(std::fabs(v.eye[2] - (5.0f + ReplayView::kBumperAhead)) < 1e-4f && v.look[2] > v.eye[2] + 10.0f);
}

TEST(orbit_turns_round_the_car) {
    ReplayView v;
    const Mat34 c = car_at(0, 0);
    v.step(ReplayAngle::kOrbit, 1, 0.0f, c);
    const float a0 = v.orbit;
    for (int i = 0; i < 60; ++i) v.step(ReplayAngle::kOrbit, 1, 1.0f / 60, c);
    CHECK(std::fabs(v.orbit - a0 - ReplayView::kOrbitRate) < 1e-3f);
    const float level = std::hypot(v.eye[0], v.eye[2]);
    CHECK(std::fabs(level - ReplayView::kOrbitRadius) < 1e-3f);
}

TEST(tv_stands_ahead_and_moves_on) {
    ReplayView v;
    float z = 0;
    v.step(ReplayAngle::kTv, 1, 0.0f, car_at(0, z));
    const float first[3] = {v.tv[0], v.tv[1], v.tv[2]};
    CHECK(first[2] > 50.0f && std::fabs(first[0]) > 5.0f);   // ahead, by the road
    float zoom_far = v.zoom;
    // the car drives on at 40 m/s: the TV point holds until the car is kTvPast beyond it, then stands ahead again
    bool moved = false;
    float zoom_near = 1.0f;
    for (int i = 0; i < 200 && !moved; ++i) {
        z += 40.0f / 60;
        v.step(ReplayAngle::kTv, 1, 1.0f / 60, car_at(0, z));
        if (dist(v.tv, first) > 1.0f) moved = true;
        else zoom_near = std::max(zoom_near == 1.0f ? 0.0f : zoom_near, v.zoom);
    }
    CHECK(moved && z > first[2] + ReplayView::kTvPast - 1.0f);
    CHECK(v.tv[2] > z + 50.0f && v.tv[0] * first[0] < 0);   // ahead again, the other side of the road
    CHECK(zoom_far < 1.0f && zoom_far >= 0.18f);
}

// a wall along the road's left (x > 4): TV takes the right side, and Chase stops before a wall behind the car
TEST(tv_and_chase_keep_out_of_walls) {
    ReplayView v;
    const ReplayView::Blocked left_wall = [](const float* a, const float* b, float* t) {
        if ((a[0] > 4.0f) == (b[0] > 4.0f)) return false;   // both on one side of the plane x = 4
        *t = (4.0f - a[0]) / (b[0] - a[0]);
        return true;
    };
    v.step(ReplayAngle::kTv, 1, 0.0f, car_at(0, 0), left_wall);
    CHECK(v.tv[0] < 4.0f);   // the side that sees the car
    const ReplayView::Blocked behind = [](const float* a, const float* b, float* t) {
        if ((a[2] > -3.0f) == (b[2] > -3.0f)) return false;   // a wall across the road 3 m behind
        *t = (-3.0f - a[2]) / (b[2] - a[2]);
        return true;
    };
    ReplayView c;
    c.step(ReplayAngle::kChase, 1, 0.0f, car_at(0, 0), behind);
    CHECK(c.eye[2] > -3.0f && c.eye[2] < -1.5f);
    const ReplayView::Blocked nose = [](const float* a, const float* b, float* t) {
        if ((a[2] > 2.3f) == (b[2] > 2.3f)) return false;   // a pillar 2.3 m ahead of the car's middle (pressed against it)
        *t = (2.3f - a[2]) / (b[2] - a[2]);
        return true;
    };
    ReplayView b;
    b.step(ReplayAngle::kBumper, 1, 0.0f, car_at(0, 0), nose);
    CHECK(b.eye[2] < 2.3f - ReplayView::kBumperMargin + 1e-3f && b.eye[2] > 1.0f && b.look[2] > b.eye[2] + 10.0f);
}

TEST(a_new_car_starts_again) {
    ReplayView v;
    v.step(ReplayAngle::kOrbit, 1, 0.0f, car_at(0, 0));
    for (int i = 0; i < 30; ++i) v.step(ReplayAngle::kOrbit, 1, 1.0f / 60, car_at(0, 0));
    CHECK(v.orbit != 0);
    v.step(ReplayAngle::kOrbit, 2, 1.0f / 60, car_at(100, 0));
    CHECK(v.car == 2 && v.orbit == 0 && v.vel[0] == 0);   // no jump in speed from the other car's place
}

TEST(names) {
    CHECK(std::string(b3replay::replay_angle_name(ReplayAngle::kFarChase)) == "FAR CHASE");
    CHECK(b3replay::kPlayerAngles.size() == 6 && b3replay::kRivalAngles.size() == 3 && b3replay::kRivalAngles[1] == ReplayAngle::kTv);
}

TEST_MAIN("replay viewer cameras (src/b3replay/replaycams.h)")
