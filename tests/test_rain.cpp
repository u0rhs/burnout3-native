// The falling rain (weather, remaster-features §38; src/b3replay/rain.h): drawn only and deterministic. The drops keep
// their places in the world as the camera moves, fall with time, are left out under a roof, and streak longer when the
// camera moves fast.
#include "test.h"

#include "rain.h"

#include <cmath>
#include <vector>

namespace {

using b3replay::Camera;
using b3replay::CoronaQuad;
using b3replay::Rain;
using b3replay::RainLook;

Camera camera_at(float x, float y, float z) {   // looking along +z
    Camera c;
    c.valid = true;
    c.m.r[0][0] = -1; c.m.r[0][1] = 0; c.m.r[0][2] = 0;
    c.m.r[1][0] = 0; c.m.r[1][1] = 1; c.m.r[1][2] = 0;
    c.m.r[2][0] = 0; c.m.r[2][1] = 0; c.m.r[2][2] = 1;
    c.m.r[3][0] = x; c.m.r[3][1] = y; c.m.r[3][2] = z;
    return c;
}

float streak_length(const CoronaQuad& q) {
    float d2 = 0;
    for (int k = 0; k < 3; ++k) d2 += (q.corner[3][k] - q.corner[0][k]) * (q.corner[3][k] - q.corner[0][k]);
    return std::sqrt(d2);
}

bool same(const std::vector<CoronaQuad>& a, const std::vector<CoronaQuad>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 3; ++k)
                if (a[i].corner[c][k] != b[i].corner[c][k]) return false;
    return true;
}

}  // namespace

TEST(deterministic_and_visible_ahead) {
    Rain a, b;
    std::vector<CoronaQuad> qa, qb;
    a.build(camera_at(10, 2, 30), 12.5, 1.0f, false, nullptr, qa);
    b.build(camera_at(10, 2, 30), 12.5, 1.0f, false, nullptr, qb);
    CHECK(qa.size() > 300 && qa.size() < 3200 && same(qa, qb));
    for (const CoronaQuad& q : qa) CHECK(q.depth >= 0.4f && q.rgb[0] > 0);
    std::vector<CoronaQuad> none;
    a.build(camera_at(10, 2, 30), 12.6, 0.0f, false, nullptr, none);   // gain 0: no rain
    CHECK(none.empty());
}

TEST(drops_stay_in_the_world_and_fall) {
    // the camera a metre to the side at the same time: the drops it still sees are at the same world places
    Rain a, b;
    std::vector<CoronaQuad> q0, q1;
    a.build(camera_at(0, 2, 0), 5.0, 1.0f, false, nullptr, q0);
    b.build(camera_at(1, 2, 0), 5.0, 1.0f, false, nullptr, q1);
    int shared = 0;
    for (const CoronaQuad& x : q0)
        for (const CoronaQuad& y : q1)
            if (std::fabs(x.corner[0][0] + x.corner[1][0] - y.corner[0][0] - y.corner[1][0]) < 1e-3f &&
                std::fabs(x.corner[0][1] + x.corner[1][1] - y.corner[0][1] - y.corner[1][1]) < 1e-3f && std::fabs(x.corner[0][2] + x.corner[1][2] - y.corner[0][2] - y.corner[1][2]) < 1e-3f) {
                ++shared;
                break;
            }
    CHECK(shared > int(q0.size()) / 2);
    // later, the same camera: the drops lower (each one's head below where it was, or wrapped to the top)
    Rain c;
    std::vector<CoronaQuad> t0, t1;
    c.build(camera_at(0, 2, 0), 5.0, 1.0f, false, nullptr, t0);
    Rain d;
    d.build(camera_at(0, 2, 0), 5.05, 1.0f, false, nullptr, t1);
    CHECK(!same(t0, t1));
}

TEST(no_rain_under_a_roof) {
    Rain a;
    std::vector<CoronaQuad> q;
    a.build(camera_at(0, 2, 0), 3.0, 1.0f, false, [](const float* p) { return p[2] < 8.0f; }, q);   // a roof over z < 8
    CHECK(!q.empty());
    for (const CoronaQuad& x : q) CHECK(x.corner[0][2] >= 7.9f);
    a.build(camera_at(0, 2, 0), 3.1, 1.0f, false, [](const float*) { return true; }, q);   // inside a tunnel
    CHECK(q.empty());
}

TEST(longer_streaks_at_speed) {
    // standing: streaks of about the fall over the exposure (the shortest kept); at 50 m/s forward: much longer
    RainLook look;
    Rain still, fast;
    std::vector<CoronaQuad> qs, qf;
    still.build(camera_at(0, 2, 0), 1.0, 1.0f, false, nullptr, qs, look);
    still.build(camera_at(0, 2, 0), 1.0 + 1.0 / 30.0, 1.0f, false, nullptr, qs, look);
    fast.build(camera_at(0, 2, 0), 1.0, 1.0f, false, nullptr, qf, look);
    for (int i = 1; i <= 4; ++i) fast.build(camera_at(0, 2, 50.0f * float(i) / 30.0f), 1.0 + double(i) / 30.0, 1.0f, false, nullptr, qf, look);
    CHECK(!qs.empty() && !qf.empty());
    double ls = 0, lf = 0;
    for (const CoronaQuad& q : qs) ls += streak_length(q);
    for (const CoronaQuad& q : qf) lf += streak_length(q);
    ls /= double(qs.size());
    lf /= double(qf.size());
    CHECK(ls < 0.45 && lf > 1.0 && lf <= look.max_len + 0.01);
    // night: dimmer
    Rain n;
    std::vector<CoronaQuad> qn;
    n.build(camera_at(0, 2, 0), 1.0 + 1.0 / 30.0, 1.0f, true, nullptr, qn, look);   // (the still camera's last time)
    int sum_day = 0, sum_night = 0;
    for (const CoronaQuad& q : qs) sum_day += q.rgb[0];
    for (const CoronaQuad& q : qn) sum_night += q.rgb[0];
    CHECK(qn.size() == qs.size() && sum_night < sum_day);
}

TEST(lens_drops) {
    // deterministic; none without rain; at speed they run outward (on average farther from the centre)
    float a[24][4], b[24][4];
    const int na = b3replay::lens_drops(12.3, 1.0f, 0.0f, 16.0f / 9.0f, a, 24);
    const int nb = b3replay::lens_drops(12.3, 1.0f, 0.0f, 16.0f / 9.0f, b, 24);
    CHECK(na > 4 && na <= 16 && na == nb);
    for (int i = 0; i < na; ++i) CHECK(a[i][0] == b[i][0] && a[i][3] > 0.0f && a[i][2] >= 0.01f);
    CHECK(b3replay::lens_drops(12.3, 0.0f, 0.0f, 1.5f, a, 24) == 0);
    double still = 0, fast = 0;
    int ns = 0, nf = 0;
    for (int k = 0; k < 200; ++k) {
        const double t = 3.0 + 0.1 * k;
        const int n0 = b3replay::lens_drops(t, 1.0f, 0.0f, 1.5f, a, 24), n1 = b3replay::lens_drops(t, 1.0f, 45.0f, 1.5f, b, 24);
        for (int i = 0; i < n0; ++i) still += std::hypot(a[i][0] - 0.5, a[i][1] - 0.42), ++ns;
        for (int i = 0; i < n1; ++i) fast += std::hypot(b[i][0] - 0.5, b[i][1] - 0.42), ++nf;
    }
    CHECK(ns > 0 && nf > 0 && fast / nf > 1.1 * still / ns && nf < ns);
}

TEST_MAIN("rain (weather)")
