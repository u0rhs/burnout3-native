// The game's post passes' arithmetic (post1, docs/native-renderer.md §25); see include/b3asset/postfx.h.
#include "b3asset/postfx.h"

#include <cmath>

namespace b3asset {

BloomGains bloom_gains(float threshold) {
    BloomGains g;
    int c = int(threshold * 255.0f + 0.5f);   // cvt.w.s truncates (madd.s 255 x it + 0.5)
    if (unsigned(c) > 254u) c = 254;          // sltiu 0xFF on the sign-extended value: negative becomes 254 too
    g.c = c;
    float s = 32768.0f / float(255 - c);
    s *= 1.05f;                               // D_004E0D88
    auto step = [&](int& k) {
        k = int(s + 0.9999f);                 // D_004E0FAC
        if (unsigned(k) > 255u) k = 255;
        s *= 128.0f / float(k);
    };
    step(g.k1);
    step(g.k2);
    step(g.k3);
    return g;
}

DirBlur directional_motion(const float old_point[3], float last, float now, const float cam[4][3], bool scale, float k) {
    DirBlur r;
    const float dt = now - last;
    if (!(dt > 0.0f)) return r;
    // the orthonormal inverse of the camera matrix applied to the old point: (dot(right, d), dot(up, d), dot(at, d))
    float d[3], p[3];
    for (int i = 0; i < 3; ++i) d[i] = old_point[i] - cam[3][i];
    for (int a = 0; a < 3; ++a) p[a] = cam[a][0] * d[0] + cam[a][1] * d[1] + cam[a][2] * d[2];
    p[2] -= 10.0f;
    float v[3];
    const float inv = 1.0f / dt;
    for (int i = 0; i < 3; ++i) v[i] = p[i] * inv;
    float a = 0.020408162847f * (std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) - 1.0f);
    if (a < 0.007843137719f) return r;
    if (a > 1.0f) a = 1.0f;
    if (v[2] < 0.0f) for (float& x : v) x = -x;
    const float m = 0.998000026f / std::fmax(v[2], 0.009999999776f);
    const float dx = v[0] * m, dy = v[1] * m;
    const float ox = dx > 0.0f ? 0.0f : 1.0f, oy = dy > 0.0f ? 0.0f : 1.0f;
    r.centre[0] = dx + 0.5f;
    r.centre[1] = dy + 0.5f;
    const float ex = r.centre[0] - ox, ey = r.centre[1] - oy;
    r.zoom = 1.0f - 0.0120000001f * (0.5f + 0.5f * a) / std::sqrt(ex * ex + ey * ey);
    r.amount = scale ? a * k : a;
    r.drawn = true;
    return r;
}

}  // namespace b3asset
