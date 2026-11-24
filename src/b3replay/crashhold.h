// crashhold.h -- options3 (2026-11-21): CRASH CAMERA Off (PC SETTINGS > DISPLAY > CAMERA), drawn only. When the player
// crashes the game switches its camera manager to the own-crash behaviours (slots 7 then 6: capture.cpp crash_share,
// Camera::crash)
// and its crash director frames the crash; with CRASH CAMERA Off the frame is drawn instead from the race's chase view held
// as it was just before the crash: the last tick drawn from a game chase camera (Camera::chase 1, crash 0), its rotation
// kept and its offset from the player's car kept as the car moves, so the view stays behind where the car was and follows
// it as it slides or flies. A blend into or out of the crash camera (the game's slot 10 interpolation) takes the held view
// in step. The game's camera is never written: the crash, its length and everything the game does are the game's (an
// earlier game-side version that skipped the director's Prepare made the race go another way after a few crashes).
#ifndef B3REPLAY_CRASHHOLD_H
#define B3REPLAY_CRASHHOLD_H

#include <algorithm>
#include <cmath>

#include "capture.h"

namespace b3replay {

// The camera drawn: the game's `game` and the held view (`held` at the tick the player's car was at `car_then`, moved with
// the car to `car_now`) blended by `share` (0 the game's, 1 the held view)
inline Camera crash_hold_camera(const Camera& game, const Camera& held, const float car_then[3], const float car_now[3], float share) {
    share = std::clamp(share, 0.0f, 1.0f);
    if (share <= 0.0f) return game;
    Camera out = game;
    float pos[3];
    for (int k = 0; k < 3; ++k) pos[k] = car_now[k] + (held.m.r[3][k] - car_then[k]);
    for (int k = 0; k < 3; ++k) out.m.r[3][k] = game.m.r[3][k] + (pos[k] - game.m.r[3][k]) * share;
    out.half_w = game.half_w + (held.half_w - game.half_w) * share;
    out.half_h = game.half_h + (held.half_h - game.half_h) * share;
    if (share >= 1.0f) {
        for (int r = 0; r < 3; ++r) for (int k = 0; k < 3; ++k) out.m.r[r][k] = held.m.r[r][k];
        return out;
    }
    // the rotation: the view and up directions blended and made orthonormal again, the right axis as the held one turns
    const auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const auto norm = [&](float* a) {
        const float n = std::sqrt(dot(a, a));
        if (n > 1e-6f) for (int k = 0; k < 3; ++k) a[k] /= n;
        return n > 1e-6f;
    };
    float at[3], up[3], right[3];
    for (int k = 0; k < 3; ++k) {
        at[k] = game.m.r[2][k] + (held.m.r[2][k] - game.m.r[2][k]) * share;
        up[k] = game.m.r[1][k] + (held.m.r[1][k] - game.m.r[1][k]) * share;
    }
    if (!norm(at)) return out;
    const float d = dot(up, at);
    for (int k = 0; k < 3; ++k) up[k] -= d * at[k];
    if (!norm(up)) return out;
    right[0] = up[1] * at[2] - up[2] * at[1];
    right[1] = up[2] * at[0] - up[0] * at[2];
    right[2] = up[0] * at[1] - up[1] * at[0];
    if (dot(right, held.m.r[0]) < 0.0f) for (float& c : right) c = -c;   // (the held camera's handedness)
    for (int k = 0; k < 3; ++k) { out.m.r[0][k] = right[k]; out.m.r[1][k] = up[k]; out.m.r[2][k] = at[k]; }
    return out;
}

}  // namespace b3replay

#endif
