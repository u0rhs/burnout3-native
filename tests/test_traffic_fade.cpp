// traffic-popin (src/b3replay/view.h, trafficfade.cpp): traffic cars fade in over kTrafficAppearSeconds where they come
// within the 275 m draw radius (spawned in view or from beyond it), fade out where the game removes them (not when they
// crash), and towards the radius; race cars and crashing traffic are never cut or faded by distance. The fade is the
// car's opacity (car.frag), its shadow's alpha and its lights' colour scale (view.cpp, effects.cpp, coronas.cpp).
#include "test.h"

#include "view.h"

#include <cmath>
#include <vector>

namespace {

using namespace b3replay;

constexpr float kDt = 1.0f / 60.0f;   // the game's tick (Tick::now advances by this)

struct Ticks : Timeline {
    std::vector<Tick> t;
    std::size_t first() const override { return 0; }
    std::size_t last() const override { return t.size() - 1; }
    const Tick& at(std::size_t i) const override { return t[i]; }
};

// n ticks, the camera at the origin, the clock at k / 60 (+ 10 s); polls numbered on across the tests, as a session's
// are (trafficfade.cpp keeps per-tick results by the tick's address and content)
Ticks ticks(std::size_t n) {
    static std::uint32_t poll = 1;
    Ticks s;
    s.t.resize(n);
    for (std::size_t k = 0; k < n; ++k) { s.t[k].now = 10.0f + float(k) * kDt; s.t[k].cam.valid = true; s.t[k].poll = poll++; }
    return s;
}

Vehicle car(std::uint32_t id, float z, VehicleKind kind = VehicleKind::kTraffic) {
    Vehicle v;
    v.id = id;
    v.data = 0x900000;
    v.kind = kind;
    v.main.r[3][2] = z;
    v.body = v.main;
    for (int k = 0; k < 3; ++k) v.cull_pos[k] = v.main.r[3][k];
    v.cull = kind == VehicleKind::kTraffic && !(z <= kTrafficDrawRadius) ? Vehicle::kFar : Vehicle::kDrawn;
    v.shadow.cast = true;
    v.corona.submit = true;
    return v;
}

const float kCam[3] = {0, 0, 0};

// the fade a frame at tick i (frac 0) draws vehicle k of that tick with
float fade_at(const Ticks& s, std::size_t i, std::size_t k) {
    Vehicle v = s.t[i].vehicles[k];
    v.since = traffic_since(s, i, k);
    return traffic_fade(v, s.t[i].now, kCam);
}

TEST(spawned_in_view_fades_in) {
    Ticks s = ticks(80);
    for (std::size_t k = 10; k < 80; ++k) s.t[k].vehicles.push_back(car(0x100, 150.0f - 0.5f * float(k - 10)));
    std::size_t at = 0;
    CHECK(traffic_since(s, 10, 0, &at) == s.t[10].now);
    CHECK(at == 10);
    CHECK(fade_at(s, 10, 0) == 0.0f);                                   // the tick it appears: not drawn yet
    CHECK_NEAR(fade_at(s, 28, 0), 18.0f * kDt / kTrafficAppearSeconds, 1e-4);   // 0.3 s in: half
    CHECK(fade_at(s, 27, 0) < fade_at(s, 28, 0));
    CHECK(traffic_since(s, 10 + 37, 0) < 0.0f);                         // past 0.6 s: solid, no look-back result
    CHECK(fade_at(s, 10 + 37, 0) == 1.0f);
    // between ticks: the fade follows the displayed clock
    Vehicle v = s.t[19].vehicles[0];
    v.since = traffic_since(s, 19, 0);
    CHECK(traffic_fade(v, s.t[19].now + 0.5f * kDt, kCam) > traffic_fade(v, s.t[19].now, kCam));
}

TEST(present_from_the_first_tick_is_solid) {
    Ticks s = ticks(20);
    for (auto& t : s.t) t.vehicles.push_back(car(0x100, 120.0f));
    for (std::size_t i = 0; i < 20; ++i) CHECK(fade_at(s, i, 0) == 1.0f);
}

TEST(from_beyond_the_radius_fades_in_and_by_distance) {
    Ticks s = ticks(60);
    for (std::size_t k = 0; k < 60; ++k) s.t[k].vehicles.push_back(car(0x100, 285.0f - float(k)));   // 275 m at tick 10
    CHECK(s.t[9].vehicles[0].cull == Vehicle::kFar);
    CHECK(traffic_since(s, 11, 0) == s.t[10].now);   // 275 m (Draw's <=): the first tick within the radius
    CHECK(fade_at(s, 10, 0) == 0.0f);
    // the distance factor alone (a car long in the radius): 0 at 275 m, 1 inside 275 - band, smooth between
    Vehicle v = car(0x100, kTrafficDrawRadius);
    CHECK(traffic_fade(v, 0.0f, kCam) == 0.0f);
    v = car(0x100, kTrafficDrawRadius - kTrafficFadeBand);
    CHECK(traffic_fade(v, 0.0f, kCam) == 1.0f);
    v = car(0x100, kTrafficDrawRadius - 0.5f * kTrafficFadeBand);
    CHECK_NEAR(traffic_fade(v, 0.0f, kCam), 0.5, 1e-6);
}

TEST(race_cars_and_crashing_traffic_not_cut) {
    // the game draws race cars at any distance (mac5's fade hid them beyond 275 m) and crashing traffic (+0x174 bit 2)
    // beyond the radius
    Vehicle r = car(0x100, 400.0f, VehicleKind::kRaceCar);
    CHECK(traffic_fade(r, 0.0f, kCam) == 1.0f);
    Vehicle c = car(0x100, 300.0f);
    c.flags = 4;
    c.cull = Vehicle::kDrawn;
    CHECK(traffic_fade(c, 0.0f, kCam) == 1.0f);
    // crashed vehicles: cut at 275 m by Draw, so faded towards it
    Vehicle w = car(0x100, 272.0f, VehicleKind::kCrashed);
    CHECK(traffic_fade(w, 0.0f, kCam) < 0.01f);
    // a race car appearing is never faded in
    Ticks s = ticks(20);
    for (std::size_t k = 10; k < 20; ++k) s.t[k].vehicles.push_back(car(0x100, 50.0f, VehicleKind::kRaceCar));
    CHECK(traffic_since(s, 10, 0) < 0.0f);
}

TEST(removed_in_view_fades_out_moving_on) {
    Ticks s = ticks(80);
    for (std::size_t k = 0; k <= 30; ++k) s.t[k].vehicles.push_back(car(0x100, 120.0f + 0.5f * float(k)));   // 30 m/s
    std::vector<Vehicle> out;
    traffic_vanishing(s, 30, s.t[30].now, out);
    CHECK(out.empty());                              // still there
    traffic_vanishing(s, 31, s.t[31].now, out);
    CHECK(out.size() == 1);
    if (out.size() == 1) {
        const Vehicle& g = out[0];
        CHECK(g.gone == s.t[30].now);
        CHECK(g.shadow.cast && g.corona.submit);     // its shadow and lights go with it (faded by the caller)
        CHECK_NEAR(g.shadow.ess.r[3][2] - g.main.r[3][2], s.t[30].vehicles[0].shadow.ess.r[3][2] - s.t[30].vehicles[0].main.r[3][2], 1e-4);
        CHECK_NEAR(g.main.r[3][2], 135.5, 1e-3);     // on at 30 m/s for one tick
        CHECK_NEAR(traffic_fade(g, s.t[31].now, kCam), 1.0 - kDt / kTrafficVanishSeconds, 1e-4);
        CHECK(traffic_fade(g, s.t[30].now + kTrafficVanishSeconds, kCam) == 0.0f);
    }
    out.clear();
    traffic_vanishing(s, 30 + 37, s.t[30 + 37].now, out);   // past 0.6 s: gone
    CHECK(out.empty());
}

TEST(crashed_into_a_wreck_is_not_kept) {
    // a traffic car that crashes goes on as a crashed vehicle at the same place: no fading copy beside it
    Ticks s = ticks(40);
    for (std::size_t k = 0; k <= 20; ++k) s.t[k].vehicles.push_back(car(0x100, 30.0f));
    for (std::size_t k = 21; k < 40; ++k) s.t[k].vehicles.push_back(car(0x300, 30.2f, VehicleKind::kCrashed));
    std::vector<Vehicle> out;
    traffic_vanishing(s, 22, s.t[22].now, out);
    CHECK(out.empty());
    CHECK(fade_at(s, 22, 0) == 1.0f);   // the wreck: no fade-in either
}

TEST(slot_reused_for_another_car) {
    // the same slot 100 m away from one tick to the next: another car -- the old one fades out, the new one in
    Ticks s = ticks(40);
    for (std::size_t k = 0; k < 40; ++k) s.t[k].vehicles.push_back(car(0x100, k < 20 ? 60.0f : 160.0f));
    CHECK(traffic_since(s, 20, 0) == s.t[20].now);
    std::vector<Vehicle> out;
    traffic_vanishing(s, 20, s.t[20].now, out);
    CHECK(out.size() == 1 && out[0].main.r[3][2] == 60.0f);
}

TEST(per_tick_results_equal_the_look_back) {
    // frames use the per-tick results (traffic_since_tick, kept per tick); the probe looks back: both agree, asked in
    // any order, for cars that spawn, cross the radius back and forth, jump slots and leave
    Ticks s = ticks(150);
    for (std::size_t k = 0; k < 150; ++k) {
        auto& v = s.t[k].vehicles;
        v.push_back(car(0x50, 10.0f, VehicleKind::kRaceCar));
        if (k >= 5) v.push_back(car(0x100, 120.0f - 0.4f * float(k)));                           // spawns at tick 5
        v.push_back(car(0x200, (k / 3) % 2 ? 276.0f : 274.0f));                                  // toggles at the radius
        if (k < 60 || k > 64) v.push_back(car(0x300, k < 90 ? 80.0f + 0.3f * float(k) : 200.0f));   // leaves, returns, jumps
    }
    int fading = 0;
    for (std::size_t pass = 0; pass < 2; ++pass)
        for (std::size_t n = 0; n < 150; ++n) {
            const std::size_t i = pass == 0 ? 149 - n : n;   // backwards first: nothing kept yet for the earlier ticks
            for (std::size_t k = 0; k < s.t[i].vehicles.size(); ++k) {
                std::size_t at = 0;
                const float look = traffic_since(s, i, k, &at), kept = traffic_since(s, i, k);
                CHECK(look == kept);
                fading += kept >= 0.0f;
            }
        }
    CHECK(fading > 0);
}

TEST(clock_reset_or_stop_never_leaves_a_car_half_faded) {
    Ticks s = ticks(400);
    for (std::size_t k = 10; k < 400; ++k) s.t[k].vehicles.push_back(car(0x100, 100.0f));
    for (std::size_t k = 15; k < 400; ++k) s.t[k].now = 0.0f + float(k - 15) * kDt;   // a restart: the clock from 0
    CHECK(traffic_since(s, 16, 0) < 0.0f);
    for (std::size_t k = 15; k < 400; ++k) s.t[k].now = 20.0f;                          // a stopped clock (held ticks)
    CHECK(traffic_since(s, 399, 0) < 0.0f);
}

}  // namespace

// split2 (docs/native-renderer.md §29.6): in a split screen each view fades against its own radius (cull / cull2); a
// car removed within one view's radius only fades out there and is hidden in the other (gone / gone2 = -1)
TEST(split_screen_fades_per_view) {
    Ticks s = ticks(80);
    for (auto& t : s.t) t.split = true;
    // far from player 1 (kFar in view 1), coming within player 2's radius at tick 10, removed after tick 50
    for (std::size_t k = 0; k <= 50; ++k) {
        Vehicle v = car(0x100, 400.0f);
        v.cull2 = k >= 10 ? Vehicle::kDrawn : Vehicle::kFar;
        s.t[k].vehicles.push_back(v);
    }
    CHECK(traffic_since_tick(s, 10, 0)[0] < 0.0f);           // view 1: never in its radius
    CHECK(traffic_since_tick(s, 10, 1)[0] == s.t[10].now);   // view 2: comes in at tick 10
    CHECK(traffic_since_tick(s, 20, 1)[0] == s.t[10].now);
    CHECK(traffic_since_tick(s, 10 + 37, 1)[0] < 0.0f);     // solid after 0.6 s
    std::vector<Vehicle> out;
    traffic_vanishing(s, 51, s.t[51].now, out);
    CHECK(out.size() == 1);
    if (out.size() == 1) {
        CHECK(out[0].gone < 0.0f);                           // not in view 1's radius: hidden there
        CHECK(out[0].gone2 == s.t[50].now);                  // fades out in view 2
    }
    // one-player ticks: view 0's alone (no copy for a car out of its radius)
    for (auto& t : s.t) t.split = false;
    Ticks o = s;
    for (auto& t : o.t) t.poll += 100000;
    out.clear();
    traffic_vanishing(o, 51, o.t[51].now, out);
    CHECK(out.empty());
}

TEST_MAIN("traffic fade-in / fade-out (src/b3replay/view.h, trafficfade.cpp)")
