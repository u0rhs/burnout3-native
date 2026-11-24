// driverlabels.h -- options3 (2026-11-21): DRIVER LABELS' layout (racetime.h DriverLabels draws it).
// options3: DRIVER LABELS (PC SETTINGS > GAME > INTERFACE): over each rival race car in a race, its driver's name (the
// race car's +0x1974: capture.h Vehicle::name), the distance to it, or both, in the game's prompt font with a drop shadow.
// The point kDriverLabelLift above the car, projected as the HUD's markers are (hud_project: the 640 x 448 units, the
// wide picture's sides included); not drawn behind the eye, nearer than kDriverLabelNear or farther than kDriverLabelFar
// from the player's car (faded out over its last kDriverLabelFade), above the game's position markers. A label that would
// cover a nearer one is left out; the nearest drawn last. Drawn only.
#ifndef B3REPLAY_DRIVERLABELS_H
#define B3REPLAY_DRIVERLABELS_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "capture.h"

namespace b3replay {

enum : int { kDriverLabelsOff = 0, kDriverLabelsNames = 1, kDriverLabelsBoth = 2, kDriverLabelsDistance = 3 };
constexpr float kDriverLabelH = 13.0f, kDriverLabelLift = 2.8f, kDriverLabelNear = 2.0f, kDriverLabelFar = 150.0f,
                kDriverLabelFade = 50.0f;
struct DriverLabel {
    float x = 0, y = 0;          // the label's foot (640 x 448 units)
    float metres = -1;           // from the player's car (-1: none known)
    float alpha = 1;
    std::u16string name;
};
// A world point in the 640 x 448 units (the wide picture's sides beyond 0..640), as hud_project maps it for an output of
// `aspect`; false behind the eye. `depth`: the clip w.
inline bool driver_label_project(const float vp[16], float aspect, const float w[3], float* x, float* y, float* depth) {
    float c[4];
    for (int i = 0; i < 4; ++i) c[i] = vp[i] * w[0] + vp[4 + i] * w[1] + vp[8 + i] * w[2] + vp[12 + i];
    if (!(c[3] > 1e-4f)) return false;
    const float fw = std::min(1.0f, (4.0f / 3.0f) / aspect), fh = std::min(1.0f, aspect / (4.0f / 3.0f));
    *x = (c[0] / c[3] + fw) / (2.0f * fw / 640.0f);
    *y = (fh - c[1] / c[3]) / (2.0f * fh / 448.0f);
    *depth = c[3];
    return true;
}
// "120 m" (speed_units 1, kilometres) / "394 ft" (0, miles: the game's M / FT)
inline std::string driver_distance_text(float metres, int speed_units) {
    char b[24];
    if (speed_units == 1) std::snprintf(b, sizeof b, "%d m", int(std::lround(metres)));
    else std::snprintf(b, sizeof b, "%d ft", int(std::lround(metres * 3.28084f)));
    return b;
}

// The frame's labels (view 0's player car: Vehicle::player 0, left out), farthest first
inline std::vector<DriverLabel> driver_labels_place(const std::vector<Vehicle>& vehicles, const float view_proj[16], float aspect) {
    std::vector<DriverLabel> out;
    const Vehicle* me = nullptr;
    for (const Vehicle& v : vehicles)
        if (v.kind == VehicleKind::kRaceCar && v.player == 0) { me = &v; break; }
    std::vector<std::pair<float, DriverLabel>> at;   // (depth, label)
    for (const Vehicle& v : vehicles) {
        if (v.kind != VehicleKind::kRaceCar || &v == me) continue;
        const float* p = v.main.r[3];
        float metres = -1;
        if (me) {
            const float dx = p[0] - me->main.r[3][0], dy = p[1] - me->main.r[3][1], dz = p[2] - me->main.r[3][2];
            metres = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (metres < kDriverLabelNear || metres > kDriverLabelFar) continue;
        }
        const float* up = v.main.r[1];
        const float n = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        if (!(n > 0)) continue;
        const float w[3] = {p[0] + up[0] / n * kDriverLabelLift, p[1] + up[1] / n * kDriverLabelLift, p[2] + up[2] / n * kDriverLabelLift};
        float x, y, depth;
        if (!driver_label_project(view_proj, aspect, w, &x, &y, &depth) || !(depth > 1.0f)) continue;
        DriverLabel l;
        l.x = x;
        l.y = y;
        l.metres = metres;
        l.name = v.name;
        const float far = metres >= 0 ? metres : depth;
        l.alpha = std::clamp((kDriverLabelFar - far) / kDriverLabelFade, 0.0f, 1.0f);
        at.emplace_back(depth, std::move(l));
    }
    std::stable_sort(at.begin(), at.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (auto& [d, l] : at) out.push_back(std::move(l));
    return out;
}

}  // namespace b3replay

#endif
