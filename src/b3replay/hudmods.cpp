// hudmods.cpp -- the race HUD's opt-in additions (hudmods.h).
#include "hudmods.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace b3replay {

namespace {

bool trace_on() {
    static const bool on = [] { const char* e = std::getenv("B3_HUDMODS_TRACE"); return e && *e && *e != '0'; }();
    return on;
}

// The frame's components with this address (a split-screen frame lists each player's HUD: one each)
int component_of(const HudFrame& f, std::uint32_t addr) {
    for (std::size_t c = 0; c < f.components.size(); ++c)
        if (f.components[c].addr == addr) return int(c);
    return -1;
}
bool draw_of(const HudFrame& f, const HudDraw& d, int component) {
    return d.object >= 0 && std::size_t(d.object) < f.objects.size() && f.objects[std::size_t(d.object)].component == component;
}

std::uint32_t mix(std::uint32_t x) {   // a small integer hash (lowbias32)
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    return x;
}

}  // namespace

void hud_shake_offset(std::uint32_t serial, float& dx, float& dy) {
    const std::uint32_t h = mix(serial / kShakeFrames + 0x9E3779B9U);
    const float u = float(h & 0xFFFFU) / 65535.0f, v = float(h >> 16) / 65535.0f;
    dx = std::round((2.0f * u - 1.0f) * kShakeX);   // whole units: the HUD's text stays on its pixel grid
    dy = std::round((2.0f * v - 1.0f) * kShakeY);
    if (dx == 0.0f && dy == 0.0f) dy = (h & 1U) ? kShakeY : -kShakeY;   // never a still frame
}

int hud_rpm_lit(float fraction) {
    if (!(fraction > 0.0f)) return 0;
    return std::clamp(int(fraction * float(kRpmSegments) + 0.5f), 0, kRpmSegments);
}

void HudMods::add_rpm_meter(HudFrame& f) {
    const std::size_t n0 = f.draws.size();
    for (const HudGauge& g : f.gauges) {
        if (g.kind != 1) continue;
        const int comp = component_of(f, g.component);
        if (comp < 0) continue;
        // the speedometer's draws: their box, colour, depth, object and pass
        float box[4] = {1e30f, 1e30f, -1e30f, -1e30f};
        const HudDraw* first = nullptr;
        for (std::size_t d = 0; d < n0; ++d) {
            const HudDraw& hd = f.draws[d];
            if (hd.tri.empty() || !draw_of(f, hd, comp)) continue;
            if (!first) first = &hd;
            for (const HudVertex& v : hd.tri) {
                box[0] = std::min(box[0], v.x); box[1] = std::min(box[1], v.y);
                box[2] = std::max(box[2], v.x); box[3] = std::max(box[3], v.y);
            }
        }
        if (!first || !(box[2] > box[0])) continue;   // no speedometer drawn (hidden, a camera without the HUD)
        const float fill = g.rpm_fraction();
        const int lit = hud_rpm_lit(fill);
        HudDraw bar = *first;
        bar.texture = 0;
        bar.alpha = 0x44;   // (Cs - Cd) As + Cd
        bar.fix = 0x80;
        bar.abe = true;
        bar.alpha_only = bar.rgb_masked = false;
        bar.tfx_decal = false;
        bar.video = -1;
        bar.marker = -1;
        bar.tri.clear();
        const std::uint32_t z = first->tri.front().z;
        const float x0 = box[0], x1 = box[2], y0 = box[3] + kRpmBelow, y1 = y0 + kRpmHeight;
        const float seg = (x1 - x0 - kRpmSlant + kRpmGap) / float(kRpmSegments) - kRpmGap;
        if (!(seg > 0.5f)) continue;
        for (int s = 0; s < kRpmSegments; ++s) {
            const bool on = s < lit, red = s >= kRpmSegments - kRpmRedSegments;
            HudVertex v;
            v.z = z;
            const std::uint32_t c = !on ? kRpmUnlit : red ? kRpmRed : kRpmLit;
            for (int k = 0; k < 4; ++k) v.rgba[k] = std::uint8_t(c >> (8 * k));
            // a parallelogram leaning like the HUD's plates: the top edge kRpmSlant to the right of the bottom
            const float a = x0 + float(s) * (seg + kRpmGap), b = a + seg, t = kRpmSlant;
            const float xs[6] = {a + t, b + t, b, a + t, b, a}, ys[6] = {y0, y0, y1, y0, y1, y1};
            for (int k = 0; k < 6; ++k) { v.x = xs[k]; v.y = ys[k]; bar.tri.push_back(v); }
        }
        f.draws.push_back(std::move(bar));
        if (trace_on())
            std::fprintf(stderr, "hudmods: frame %u speedometer %08x box %.1f,%.1f..%.1f,%.1f revs %.1f low %.1f limit %.1f gear %d%s: %.3f, %d lit\n",
                         f.serial, g.component, double(box[0]), double(box[1]), double(box[2]), double(box[3]), double(g.revs),
                         double(g.low), double(g.limit), g.gear, g.manual ? " manual" : "", double(fill), lit);
    }
}

void HudMods::shake_boost_bars(HudFrame& f) {
    for (const HudGauge& g : f.gauges) {
        if (g.kind != 2 || !g.boosting) continue;
        const int comp = component_of(f, g.component);
        if (comp < 0) continue;
        float dx = 0, dy = 0;
        hud_shake_offset(f.serial, dx, dy);
        int moved = 0;
        for (HudDraw& d : f.draws) {
            if (!draw_of(f, d, comp)) continue;
            for (HudVertex& v : d.tri) { v.x += dx; v.y += dy; }
            ++moved;
        }
        if (trace_on())
            std::fprintf(stderr, "hudmods: frame %u boost bar %08x boosting: %d draws moved by %+.0f, %+.0f\n", f.serial, g.component, moved,
                         double(dx), double(dy));
    }
}

std::shared_ptr<const HudFrame> HudMods::apply(const std::shared_ptr<const HudFrame>& in, const HudModOptions& o) {
    if (!in || (!o.rpm_meter && !o.boost_shake) || in->gauges.empty()) {
        src_.reset();
        out_.reset();
        return in;
    }
    if (in == src_ && o == opts_ && out_) return out_;
    bool any = false;
    for (const HudGauge& g : in->gauges) any = any || (o.rpm_meter && g.kind == 1) || (o.boost_shake && g.kind == 2 && g.boosting);
    if (!any) {
        src_.reset();
        out_.reset();
        return in;
    }
    auto f = std::make_shared<HudFrame>(*in);
    if (o.boost_shake) shake_boost_bars(*f);   // before the meter: the meter never shakes
    if (o.rpm_meter) add_rpm_meter(*f);
    src_ = in;
    opts_ = o;
    out_ = std::move(f);
    return out_;
}

}  // namespace b3replay
