// FINISH CAMERA (finishcam1; src/b3replay/finishcam.h): the shots' offsets read from a VDB.XML (a made-up one here:
// the keys with our own values), the set pieces' timeline, a shot's camera (a cut, a tripod, a follow) and the drawn
// camera, and the per-tick gate (the game's finish camera state, the row, one view).
#include "test.h"

#include "finishcam.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace {

using b3replay::Camera;
using b3replay::FinishCam;
using b3replay::FinishShots;
using b3replay::FinishView;
using b3replay::Mat34;
using b3replay::Tick;
using b3replay::Vehicle;

void put32(std::vector<std::uint8_t>& d, std::size_t o, std::uint32_t v) { std::memcpy(d.data() + o, &v, 4); }
void putf(std::vector<std::uint8_t>& d, std::size_t o, float v) { std::memcpy(d.data() + o, &v, 4); }

// A VDB.XML in the file's layout: value records sorted by signed key, the vectors in the blocks after them
std::vector<std::uint8_t> make_vdb(int shots, bool extra = true) {
    std::map<std::int32_t, int> keys;   // key -> shot (-1: a plain value)
    for (int i = 0; i < shots; ++i) keys[std::int32_t(FinishShots::kShotKeys[i])] = i;
    if (extra) {
        keys[0x12345678] = -1;
        keys[std::int32_t(0x87654321u)] = -1;
    }
    const std::uint32_t n = std::uint32_t(keys.size());
    const std::uint32_t blocks = 0x14 + 8 * n, files = blocks + 16 * std::uint32_t(shots);
    std::vector<std::uint8_t> d(files + 8, 0);
    put32(d, 0, 2);
    put32(d, 4, n);
    put32(d, 0xC, 1);
    put32(d, 0x10, files);
    std::uint32_t r = 0, b = blocks;
    for (const auto& [k, shot] : keys) {
        if (shot >= 0) {
            putf(d, b, 0.5f * float(shot) - 3.0f);
            putf(d, b + 4, 2.0f);
            putf(d, b + 8, shot % 2 ? 10.0f : -20.0f);
            put32(d, 0x14 + 8 * r, b);
            b += 16;
        } else {
            putf(d, 0x14 + 8 * r, 1.5f);
        }
        put32(d, 0x14 + 8 * r + 4, std::uint32_t(k));
        ++r;
    }
    return d;
}

float len(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

}  // namespace

TEST(reads_the_offsets) {
    FinishShots s;
    CHECK(s.parse(make_vdb(FinishShots::kShots)) && s.found == FinishShots::kShots);
    for (int i = 0; i < FinishShots::kShots; ++i)
        CHECK(s.offset[i][0] == 0.5f * float(i) - 3.0f && s.offset[i][1] == 2.0f && s.offset[i][2] == (i % 2 ? 10.0f : -20.0f));
    FinishShots t;   // another disc's file (keys missing): not usable
    CHECK(!t.parse(make_vdb(30)) && t.found == 30);
    FinishShots u;
    CHECK(!u.parse(std::vector<std::uint8_t>(8, 0)) && u.found == 0);
    int sum = 0;
    for (int k = 0; k < FinishShots::kSetPieces; ++k) sum += FinishShots::kSetPieceShots[k];
    CHECK(sum == FinishShots::kShots);
}

TEST(set_piece_timeline) {
    int sp = -1, shot = -1;
    float into = -1;
    FinishShots::at(0.0f, sp, shot, into);
    CHECK(sp == 0 && shot == 0 && into == 0.0f);
    FinishShots::at(2.6f, sp, shot, into);   // set piece 1: two shots of 2.5 s
    CHECK(sp == 0 && shot == 1 && std::fabs(into - 0.1f) < 1e-4f);
    FinishShots::at(5.5f, sp, shot, into);   // set piece 2: three shots
    CHECK(sp == 1 && shot == 2);
    FinishShots::at(59.9f, sp, shot, into);   // the 12th's last shot
    CHECK(sp == 11 && shot == 35);
    FinishShots::at(60.1f, sp, shot, into);   // round again
    CHECK(sp == 0 && shot == 0);
}

TEST(shot_camera) {
    FinishShots s;
    CHECK(s.parse(make_vdb(FinishShots::kShots)));
    Mat34 car;   // crossing the line at the origin heading +z
    FinishView v;
    v.step(s, 0.0f, car);   // shot 1: offset (-3, 2, -20) from the line, aimed at the car
    CHECK(v.on && v.shot == 0 && v.eye[0] == -3.0f && v.eye[1] == 2.0f && v.eye[2] == -20.0f);
    CHECK(v.look[1] == FinishView::kAim && v.look[2] == 0.0f && v.zoom < 1.0f && v.zoom >= FinishView::kMinZoom);
    // the car drives on: the camera stays, the aim eases after the car
    car.r[3][2] = 30.0f;
    v.step(s, 1.0f / 30.0f, car);
    CHECK(v.eye[2] == -20.0f && v.look[2] > 0.0f && v.look[2] < 30.0f && v.roam == 30.0f);
    // shot 2 (2.5 s in): offset (-2.5, 2, 10) from the line, not from the car
    for (int k = 0; k < 75; ++k) v.step(s, 1.0f / 30.0f, car);
    CHECK(v.shot == 1 && v.eye[0] == -2.5f && v.eye[2] == 10.0f && std::fabs(v.look[2] - 30.0f) < 0.01f);
    // the car far from the line: the aim goes back to the line
    car.r[3][2] = 400.0f;
    for (int k = 0; k < 30; ++k) v.step(s, 1.0f / 30.0f, car);
    CHECK(v.shot == 1 && std::fabs(v.look[2]) < 1.0f);
    // the pause (no game time): nothing moves on
    const float t = v.t;
    v.step(s, 0.0f, car);
    CHECK(v.t == t);
}

TEST(drawn_camera) {
    Camera c;
    c.valid = true;
    c.half_w = 0.6f;
    c.half_h = 0.45f;
    c.chase = 1.0f;
    const float eye[3] = {3, 2, -10}, look[3] = {0, 0.6f, 0};
    b3replay::finish_camera(c, eye, look, 0.5f);
    float at[3] = {look[0] - eye[0], look[1] - eye[1], look[2] - eye[2]};
    const float l = len(at);
    for (float& x : at) x /= l;
    float d01 = 0, d02 = 0, d12 = 0, dat = 0;
    for (int k = 0; k < 3; ++k) {
        d01 += c.m.r[0][k] * c.m.r[1][k];
        d02 += c.m.r[0][k] * c.m.r[2][k];
        d12 += c.m.r[1][k] * c.m.r[2][k];
        dat += std::fabs(c.m.r[2][k] - at[k]);
    }
    CHECK(std::fabs(d01) < 1e-5f && std::fabs(d02) < 1e-5f && std::fabs(d12) < 1e-5f && dat < 1e-5f);
    CHECK(c.m.r[1][1] > 0.0f && std::fabs(c.m.r[0][1]) < 1e-6f);   // upright, level
    // row 0 is the screen's left: with at = +z it is +x... as the game's chase camera (row 0 = up x at)
    CHECK(c.m.r[3][0] == 3.0f && c.lod_pos[2] == -10.0f && c.half_w == 0.3f && c.half_h == 0.225f && c.chase == 0.0f);
}

TEST(per_tick_gate) {
    FinishCam& f = FinishCam::instance();
    f.load_vdb(make_vdb(FinishShots::kShots));
    CHECK(f.ready());
    std::uint32_t poll = 100;
    float now = 50.0f;
    int finish = 1;
    std::uint32_t logic = 0;
    auto tick = [&](std::uint32_t state, bool split = false) {
        Tick t;
        t.poll = poll++;
        now += 1.0f / 30.0f;
        t.now = now;
        t.cam.valid = true;
        t.cam_state = state;
        t.split = split;
        t.score.valid = true;
        t.score.finish = finish;
        t.score.logic = logic;
        Vehicle v;
        v.id = b3replay::kPlayerRaceCar;
        v.main.r[2][2] = 1.0f;
        t.vehicles = {v};
        f.on_tick(t);
        return t;
    };
    f.set_level(0);   // Game (the default): never
    CHECK(!tick(b3replay::kFinishCameraState).finish_cam.on);
    f.set_level(1);
    CHECK(!tick(0x4DE360u).finish_cam.on);                                // another camera state (the replay's)
    CHECK(!tick(b3replay::kFinishCameraState, true).finish_cam.on);       // the split screen
    finish = 0;
    CHECK(!tick(b3replay::kFinishCameraState).finish_cam.on);             // the state, but no finish (not a race's end)
    finish = 2;
    CHECK(!tick(b3replay::kFinishCameraState).finish_cam.on);             // an out
    finish = 1;
    logic = 0x4DEE00u;
    CHECK(!tick(b3replay::kFinishCameraState).finish_cam.on);             // a Road Rage's end
    logic = 0;
    Tick t = tick(b3replay::kFinishCameraState);
    CHECK(t.finish_cam.on && t.finish_cam.shot == 0);
    for (int k = 0; k < 80; ++k) t = tick(b3replay::kFinishCameraState);
    CHECK(t.finish_cam.on && t.finish_cam.shot == 1);
    t = tick(0x4DE360u);   // the game moves on: its camera, and from the start next time
    CHECK(!t.finish_cam.on);
    t = tick(b3replay::kFinishCameraState);
    CHECK(t.finish_cam.on && t.finish_cam.shot == 0);
    f.set_level(0);
}

// camfixes: point-to-point: the shot follows the car (its offset in the car's level frame at the cut), aimed at it
TEST(point_to_point_follows_the_car) {
    FinishShots s;
    CHECK(s.parse(make_vdb(FinishShots::kShots)));
    Mat34 car;   // at the line heading +z
    FinishView v;
    v.follow = true;
    v.step(s, 0.0f, car);   // shot 1: (-3, 2, -20) from the car
    CHECK(v.eye[0] == -3.0f && v.eye[1] == 2.0f && v.eye[2] == -20.0f);
    car.r[3][2] = 300.0f;   // far down the road: the camera came along
    for (int k = 0; k < 30; ++k) v.step(s, 1.0f / 30.0f, car);
    CHECK(v.shot == 0 && v.eye[2] == 280.0f && std::fabs(v.look[2] - 300.0f) < 10.0f);   // (the aim eases after the car)
    // a turn of the car within the shot: the frame stays the cut's (no swing with the steering)
    car.r[2][0] = 1.0f; car.r[2][2] = 0.0f; car.r[0][0] = 0.0f; car.r[0][2] = -1.0f;
    v.step(s, 1.0f / 30.0f, car);
    CHECK(v.eye[0] == -3.0f && v.eye[2] == 280.0f);
}

// camfixes: the per-tick decision: a finish far from where the car waited for the start is point-to-point
TEST(point_to_point_decision) {
    FinishCam& f = FinishCam::instance();
    f.load_vdb(make_vdb(FinishShots::kShots));
    f.set_level(1);
    std::uint32_t poll = 5000;
    float now = 10.0f;
    auto tick = [&](int score_state, std::uint32_t cam_state, int finish, float z) {
        Tick t;
        t.poll = poll++;
        now += 1.0f / 30.0f;
        t.now = now;
        t.cam.valid = true;
        t.cam_state = cam_state;
        t.score.valid = true;
        t.score.state = score_state;
        t.score.finish = finish;
        Vehicle v;
        v.id = b3replay::kPlayerRaceCar;
        v.main.r[2][2] = 1.0f;
        v.main.r[3][2] = z;
        t.vehicles = {v};
        f.on_tick(t);
        return t;
    };
    for (const float line : {40.0f, 2500.0f}) {   // a circuit (the line by the grid), a point-to-point race
        tick(0, 0x4DE360u, 0, 0.0f);   // the countdown on the grid at z 0
        tick(2, 0x4DE360u, 0, line / 2);
        Tick t = tick(3, b3replay::kFinishCameraState, 1, line);   // the finish
        CHECK(t.finish_cam.on);
        // the circuit's eye stays by the line; the point-to-point's came along with the car (190 m on, then the game's)
        if (line < 100.0f) {
            for (int k = 0; k < 19; ++k) t = tick(3, b3replay::kFinishCameraState, 1, line + 10.0f * float(k + 1));
            CHECK(t.finish_cam.on && std::fabs(t.finish_cam.eye[2] - (line - 20.0f)) < 0.01f);
        } else {
            for (int k = 0; k < 19; ++k) t = tick(3, b3replay::kFinishCameraState, 1, line + 10.0f * float(k + 1));
            CHECK(t.finish_cam.on && std::fabs(t.finish_cam.eye[2] - (line + 190.0f - 20.0f)) < 0.01f);
            t = tick(3, b3replay::kFinishCameraState, 1, line + 220.0f);   // past kFollowEnd: the game's shot, and it stays so
            CHECK(!t.finish_cam.on);
            t = tick(3, b3replay::kFinishCameraState, 1, line + 230.0f);
            CHECK(!t.finish_cam.on);
        }
        tick(3, 0x4DE360u, 1, line + 300.0f);   // the game moves on
    }
    f.set_level(0);
}

TEST_MAIN("finish camera (finishcam1)")
