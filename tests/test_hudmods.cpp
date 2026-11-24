// hudmods (src/b3replay/hudmods.h): RPM METER and BOOST BAR SHAKE on synthetic HUD frames -- the meter's fill from the
// transmission's values, its segments under the speedometer's draws (its object, so the layouts move it with them), the
// shake only on the boosting bar's draws, the frame itself when nothing applies.
#include "test.h"

#include "hudmods.h"

#include <cmath>
#include <memory>

namespace {

using namespace b3replay;
constexpr std::uint32_t kSpeedo = 0x0066AE10u, kBoost = 0x0066A610u, kOther = 0x0066A550u;

HudDraw quad(int object, float x0, float y0, float x1, float y1, std::uint8_t r = 0x60) {
    HudDraw d;
    d.object = object;
    d.texture = 7;
    const float xs[6] = {x0, x1, x1, x0, x1, x0}, ys[6] = {y0, y0, y1, y0, y1, y1};
    for (int k = 0; k < 6; ++k) {
        HudVertex v;
        v.x = xs[k]; v.y = ys[k]; v.z = 5;
        v.rgba[0] = r; v.rgba[1] = 0x50; v.rgba[2] = 0x40; v.rgba[3] = 0x80;
        d.tri.push_back(v);
    }
    return d;
}
// a frame: the speedometer (component 0, object 0) at 500..580 x 400..420, the boost bar (component 1, object 1), the
// position (component 2, object 2)
HudFrame frame(float revs, bool boosting) {
    HudFrame f;
    f.serial = 30;
    f.width = 640; f.height = 448;
    for (std::uint32_t a : {kSpeedo, kBoost, kOther}) { HudComponent c; c.addr = a; f.components.push_back(c); }
    for (int k = 0; k < 3; ++k) { HudObject o; o.component = k; f.objects.push_back(o); }
    f.draws.push_back(quad(0, 500, 400, 580, 420));
    f.draws.push_back(quad(1, 100, 410, 300, 425));
    f.draws.push_back(quad(2, 20, 20, 60, 40));
    HudGauge s; s.component = kSpeedo; s.kind = 1; s.revs = revs; s.low = 1000; s.limit = 8000;
    HudGauge b; b.component = kBoost; b.kind = 2; b.boosting = boosting;
    f.gauges = {s, b};
    return f;
}

TEST(rpm_fraction) {
    HudGauge g; g.kind = 1; g.low = 1000; g.limit = 8000;
    g.revs = 100; CHECK(g.rpm_fraction() == 0.0f);      // idle: (10 x 100 - 1000) / 7000
    g.revs = 450; CHECK(std::fabs(g.rpm_fraction() - 0.5f) < 1e-6f);
    g.revs = 800; CHECK(g.rpm_fraction() == 1.0f);
    g.revs = 900; CHECK(g.rpm_fraction() == 1.0f);      // over the limit (a boost's 10 %): full
    g.revs = 0; CHECK(g.rpm_fraction() == 0.0f);
    g.limit = g.low; CHECK(g.rpm_fraction() == 0.0f);   // no range (not a car yet): empty
    CHECK(hud_rpm_lit(0) == 0 && hud_rpm_lit(1) == kRpmSegments && hud_rpm_lit(0.5f) == 10 && hud_rpm_lit(0.024f) == 0 && hud_rpm_lit(0.026f) == 1);
}

TEST(meter_under_the_speedometer) {
    HudFrame f = frame(450, false);
    HudMods::add_rpm_meter(f);
    CHECK(f.draws.size() == 4);
    const HudDraw& bar = f.draws.back();
    CHECK(bar.object == 0 && bar.texture == 0 && bar.abe);   // the speedometer's object: the layouts move it with it
    CHECK(bar.tri.size() == std::size_t(6 * kRpmSegments));
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    int lit = 0, red = 0;
    for (std::size_t k = 0; k < bar.tri.size(); k += 6) {
        if (bar.tri[k].rgba[3] == 0x80) ++lit;
        if (bar.tri[k].rgba[0] == 0x70 && bar.tri[k].rgba[1] == 0x16) ++red;
    }
    for (const HudVertex& v : bar.tri) { x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); y0 = std::min(y0, v.y); y1 = std::max(y1, v.y); }
    CHECK(std::fabs(x0 - 500) < 1e-3f && std::fabs(x1 - 580) < 1e-3f);   // the digits' width
    CHECK(y0 == 420 + kRpmBelow && y1 == 420 + kRpmBelow + kRpmHeight);
    CHECK(lit == 10 && red == 0);   // half way: no red yet
    CHECK(bar.tri[0].rgba[0] == (kRpmLit & 0xFF) && bar.tri[0].rgba[2] == ((kRpmLit >> 16) & 0xFF) && bar.tri[0].z == 5);   // its depth
    CHECK(bar.tri[0].x == 500 + kRpmSlant && bar.tri[5].x == 500);   // leaning like the plates: the top edge to the right
    HudFrame full = frame(800, false);
    HudMods::add_rpm_meter(full);
    int red_full = 0;
    for (std::size_t k = 0; k < full.draws.back().tri.size(); k += 6) red_full += full.draws.back().tri[k].rgba[0] == (kRpmRed & 0xFF);
    CHECK(red_full == kRpmRedSegments);   // at the limit: the last three red
}

TEST(no_speedometer_no_meter) {
    HudFrame f = frame(450, false);
    f.draws.erase(f.draws.begin());   // the speedometer not drawn (a camera without the HUD)
    HudMods::add_rpm_meter(f);
    CHECK(f.draws.size() == 2);
}

TEST(shake_only_the_boosting_bar) {
    HudFrame f = frame(450, true);
    float dx = 0, dy = 0;
    hud_shake_offset(f.serial, dx, dy);
    CHECK((dx != 0 || dy != 0) && std::fabs(dx) <= kShakeX && std::fabs(dy) <= kShakeY);
    HudMods::shake_boost_bars(f);
    CHECK(f.draws[1].tri[0].x == 100 + dx && f.draws[1].tri[0].y == 410 + dy);
    CHECK(f.draws[0].tri[0].x == 500 && f.draws[2].tri[0].x == 20);   // the rest stays
    HudFrame g = frame(450, false);
    HudMods::shake_boost_bars(g);
    CHECK(g.draws[1].tri[0].x == 100 && g.draws[1].tri[0].y == 410);
    // a new place every kShakeFrames frames, the same for the same serial (a replay shakes the same)
    float ax, ay, bx, by;
    hud_shake_offset(30, ax, ay);
    hud_shake_offset(31, bx, by);
    CHECK(ax == bx && ay == by);
    int moves = 0;
    for (std::uint32_t s = 0; s < 60; s += kShakeFrames) {
        hud_shake_offset(s, ax, ay);
        hud_shake_offset(s + kShakeFrames, bx, by);
        if (ax != bx || ay != by) ++moves;
    }
    CHECK(moves >= 15);
}

TEST(apply_keeps_the_frame_when_off) {
    auto in = std::make_shared<const HudFrame>(frame(450, true));
    HudMods m;
    CHECK(m.apply(in, HudModOptions{}) == in);
    CHECK(&m.laid_out(in) == in.get());
    auto out = m.apply(in, HudModOptions{true, true});
    CHECK(out != in && out->draws.size() == in->draws.size() + 1 && in->draws.size() == 3);
    CHECK(m.apply(in, HudModOptions{true, true}) == out);   // kept while the frame is the same
    CHECK(&m.laid_out(in) == out.get());
    auto none = std::make_shared<HudFrame>(frame(450, false));
    none->gauges.clear();   // an older capture: no gauges
    std::shared_ptr<const HudFrame> c = none;
    CHECK(m.apply(c, HudModOptions{true, true}) == c);
    auto calm = std::make_shared<const HudFrame>(frame(450, false));
    CHECK(m.apply(calm, HudModOptions{false, true}) == calm);   // shake on, not boosting: the frame itself
}

}  // namespace

TEST_MAIN("the race HUD's RPM meter and boost bar shake (hudmods)")
