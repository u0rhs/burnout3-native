// taillights.h -- taillights (2026-11-22): where night mode or rain turns the cars' lights on (view.cpp), the tail lights
// glow at a steady level and rise to the full brake light while the car brakes, as on a real car. Drawn only; a dry day
// keeps the game's lights as they are.
//
// What the game has [C RenderLightCoronas 0x256690, VU0 0x47F6D0 at 0x16A; the bits traced per tick over a race]:
// EssentialParams +0x58 holds the lights: 0x02 / 0x04 the head lights, 0x08 the tail lights, 0x10 the brake lights,
// 0x20 / 0x40 the indicators, 0x80 the reverse lights. Race cars set 0x10 while braking (the player and the AI alike)
// and never 0x08; traffic holds 0x0C (head and tail) and adds 0x10 while braking. The coronas draw the brake group (2)
// in (1.4, 0, 0) x 0.75, else the tail group (1) in (1.1, 0, 0) x 0.5; a car mesh entry whose material mask has a lit
// bit draws flat 192 (no env pass), all or nothing.
// Ours, where the lights are on (night, rain):
// - a brake level per car: the mean of its brake bit over the last kBrakeTicks ticks of the feed (the tick's alpha
//   moving the window), so a light rises and falls over ~83 ms and is the same in a window, a shot and a replay;
// - the rear coronas: the brake group's colour x (tail + (1 - tail) x brake) where the tail lamps are the brake lamps
//   (or the car has no tail group), else the tail group at tail x the brake colour and the brake group at brake x it;
// - the lamp meshes: a tail mesh (mask 0x08) at the tail level, a brake mesh (0x10) at max(tail, brake), each a mix of
//   the body's constants and the lit ones (b3asset::rear_lamp_level, car_mesh_constants_level).
#ifndef B3REPLAY_TAILLIGHTS_H
#define B3REPLAY_TAILLIGHTS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace b3replay {

constexpr std::uint8_t kLightTail = 0x08, kLightBrake = 0x10;
constexpr int kBrakeTicks = 5;              // the brake light's rise and fall: 5 ticks (83 ms at 60 Hz)
constexpr float kTailLevelDefault = 0.40f;  // the tail lights' share of the brake lights' glow

// The tail level (B3_TAIL_LEVEL=<0..1>: another, for checks; B3_TAILLIGHTS=0: -1, the lights before taillights, for
// comparisons)
inline float tail_level() {
    static const float t = [] {
        const char* off = std::getenv("B3_TAILLIGHTS");
        if (off && off[0] == '0') return -1.0f;
        const char* e = std::getenv("B3_TAIL_LEVEL");
        const float v = e && *e ? float(std::atof(e)) : kTailLevelDefault;
        return std::clamp(v, 0.0f, 1.0f);
    }();
    return t;
}

// The brake level at tick i + alpha from the brake bits of ticks i - (kBrakeTicks - 1) .. i + 1 (`bits[0]` the oldest,
// bits[kBrakeTicks] the next tick's): the window's mean at i, moved towards the next window's by alpha.
inline float brake_level(const bool (&bits)[kBrakeTicks + 1], float alpha) {
    int now = 0, next = 0;
    for (int k = 0; k < kBrakeTicks; ++k) { now += bits[k]; next += bits[k + 1]; }
    const float a = std::clamp(alpha, 0.0f, 1.0f);
    return (float(now) + a * float(next - now)) / float(kBrakeTicks);
}

// The rear coronas' scales of the brake colour: `same` the tail lamps are the brake lamps (or there is no tail group)
struct RearCoronas { float tail = 0, brake = 0; };
inline RearCoronas rear_coronas(bool same, float tail, float brake) {
    if (same) return {0.0f, tail + (1.0f - tail) * brake};
    return {tail, brake};
}

// Two corona groups the same lamps: as many lights, each within 5 cm of one in the other
template <class Light, class Group>
bool same_lamps(const Group& a, const Group& b) {
    if (a.size() != b.size()) return false;
    for (const Light& l : a) {
        bool found = false;
        for (const Light& m : b) {
            const float d = std::fabs(l.pos[0] - m.pos[0]) + std::fabs(l.pos[1] - m.pos[1]) + std::fabs(l.pos[2] - m.pos[2]);
            if (d < 0.05f) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

}  // namespace b3replay

#endif
