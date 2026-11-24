// options3's drawn-only layouts: DRIVER LABELS (src/b3replay/driverlabels.h: which rival cars get a label, where, the
// distance text) and CRASH CAMERA Off's held view (src/b3replay/crashhold.h).
#include "test.h"

#include <cstring>

#include "crashhold.h"
#include "driverlabels.h"

using namespace b3replay;

namespace {
// a view-projection with clip (x, y, z, w = z): the eye at the origin looking down +z, 90 degrees across a 4:3 output
void eye_down_z(float vp[16]) {
    for (int i = 0; i < 16; ++i) vp[i] = 0;
    vp[0] = 1, vp[5] = 1, vp[10] = 1, vp[11] = 1;
}
Vehicle car(float x, float y, float z, int player, const char16_t* name) {
    Vehicle v;
    v.kind = VehicleKind::kRaceCar;
    v.player = std::int8_t(player);
    v.main.r[3][0] = x, v.main.r[3][1] = y, v.main.r[3][2] = z;
    v.name = name;
    return v;
}
}  // namespace

TEST(distance_text_in_the_game_units) {
    CHECK(driver_distance_text(20.0f, 1) == "20 m");
    CHECK(driver_distance_text(20.0f, 0) == "66 ft");
    CHECK(driver_distance_text(0.4f, 1) == "0 m");
}

TEST(rivals_ahead_within_range_get_labels) {
    float vp[16];
    eye_down_z(vp);
    std::vector<Vehicle> vs = {car(0, 0, 5, 0, u"ME"), car(0, 0, 25, -1, u"DRIVER1"), car(0, 0, -20, -1, u"BEHIND"),
                               car(0, 0, 400, -1, u"FAR"), car(1, 0, 80, -1, u"DRIVER2")};
    Vehicle traffic = car(0, 0, 30, -1, u"");
    traffic.kind = VehicleKind::kTraffic;
    vs.push_back(traffic);
    const std::vector<DriverLabel> l = driver_labels_place(vs, vp, 4.0f / 3.0f);
    CHECK(l.size() == 2);   // not the player, the car behind the eye, the one beyond the range or the traffic
    if (l.size() != 2) return;
    CHECK(l[0].name == u"DRIVER2" && l[1].name == u"DRIVER1");   // the farthest first (the nearest drawn last)
    CHECK(std::fabs(l[1].metres - 20.0f) < 1e-3f && std::fabs(l[0].metres - std::sqrt(1.0f + 75.0f * 75.0f)) < 1e-3f);
    // DRIVER1: straight ahead, kDriverLabelLift above it: x the middle, y above the horizon (448 / 2)
    CHECK(std::fabs(l[1].x - 320.0f) < 1e-3f);
    CHECK(std::fabs(l[1].y - (1.0f - kDriverLabelLift / 25.0f) * 224.0f) < 1e-2f);
    CHECK(l[1].alpha == 1.0f && l[0].alpha > 0.99f);   // (75 m: before the fade)
}

TEST(the_far_end_fades_and_wide_pictures_keep_their_sides) {
    float vp[16];
    eye_down_z(vp);
    const std::vector<Vehicle> vs = {car(0, 0, 0.5f, 0, u"ME"), car(0, 0, 0.5f + 0.5f * (kDriverLabelFar + (kDriverLabelFar - kDriverLabelFade)), -1, u"FADING"),
                                     car(-60, 0, 50.5f, -1, u"LEFT")};
    const std::vector<DriverLabel> l = driver_labels_place(vs, vp, 16.0f / 9.0f);
    CHECK(l.size() == 2);
    if (l.size() != 2) return;
    CHECK(l[0].name == u"FADING" && l[0].alpha > 0.45f && l[0].alpha < 0.55f);   // halfway through the fade
    // 16:9: the 4:3 box is 0..640, a point at x / z = -1.2 left of it (the wide picture's side)
    CHECK(l[1].name == u"LEFT" && l[1].x < 0.0f);
}

TEST(no_player_car_no_distance) {
    float vp[16];
    eye_down_z(vp);
    const std::vector<DriverLabel> l = driver_labels_place({car(0, 0, 30, -1, u"A")}, vp, 4.0f / 3.0f);
    CHECK(l.size() == 1 && l[0].metres < 0);
}

namespace {
Camera cam_at(float x, float y, float z, float yaw) {   // looking along +z turned by `yaw` about y
    Camera c;
    c.valid = true;
    const float s = std::sin(yaw), co = std::cos(yaw);
    const float r[4][3] = {{co, 0, -s}, {0, 1, 0}, {s, 0, co}, {x, y, z}};
    for (int i = 0; i < 4; ++i) for (int k = 0; k < 3; ++k) c.m.r[i][k] = r[i][k];
    c.half_w = 1.0f, c.half_h = 0.75f;
    return c;
}
float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
}  // namespace

TEST(crash_hold_keeps_the_chase_view_behind_the_car) {
    const Camera game = cam_at(30, 2, 40, 1.2f);   // the game's crash shot
    Camera held = cam_at(0, 3, -6, 0.0f);          // the chase view before the crash, 6 m behind the car at the origin
    held.half_w = 1.2f;
    const float then[3] = {0, 0, 0}, now[3] = {2, 0.5f, 10};
    // none of it: the game's
    const Camera a = crash_hold_camera(game, held, then, now, 0.0f);
    CHECK(std::memcmp(&a.m, &game.m, sizeof a.m) == 0 && a.half_w == game.half_w);
    // all of it: the held rotation, the held offset from the car where the car is now
    const Camera b = crash_hold_camera(game, held, then, now, 1.0f);
    CHECK(std::memcmp(b.m.r, held.m.r, sizeof(float) * 9) == 0);
    CHECK(b.m.r[3][0] == 2.0f && b.m.r[3][1] == 3.5f && b.m.r[3][2] == 4.0f && b.half_w == 1.2f);
    // a blend: halfway there, the rows orthonormal and turned as the held camera's
    const Camera c = crash_hold_camera(game, held, then, now, 0.5f);
    CHECK(std::fabs(c.m.r[3][0] - 16.0f) < 1e-4f && std::fabs(c.m.r[3][2] - 22.0f) < 1e-4f);
    for (int i = 0; i < 3; ++i) {
        CHECK(std::fabs(dot3(c.m.r[i], c.m.r[i]) - 1.0f) < 1e-4f);
        CHECK(std::fabs(dot3(c.m.r[i], c.m.r[(i + 1) % 3])) < 1e-4f);
    }
    CHECK(dot3(c.m.r[0], held.m.r[0]) > 0.0f && dot3(c.m.r[2], held.m.r[2]) > 0.0f && dot3(c.m.r[2], game.m.r[2]) > 0.0f);
}

TEST_MAIN("options3 drawn-only layouts: DRIVER LABELS, CRASH CAMERA Off")
