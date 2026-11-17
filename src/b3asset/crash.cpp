#include "b3asset/crash.h"

#include <cstring>

namespace b3asset {

const char* const kCrashPickupTextures[9] = {"pickup_boost", "pickup_score01", "pickup_score02", "pickup_score03",
                                             "pickup_multiplier_skull", "pickup_multiplierx2", "pickup_multiplierx4",
                                             "pickup_gamebreaker", "pickup_stealer"};

bool read_game_data(Bytes f, std::vector<GameStage>& out, std::string* why) {
    out.clear();
    auto fail = [&](const std::string& w) { if (why) *why = w; return false; };
    if (!f.has(0, 0x800)) return fail("GAMEDATA.BGD: shorter than its 0x800-byte directory");
    const std::int32_t n = std::int32_t(f.u32(0x260));
    if (n < 0 || n > 50) return fail("GAMEDATA.BGD: stage count " + std::to_string(n) + " outside 0..50");
    for (int i = 0; i < n; ++i) {
        GameStage s;
        s.index = i;
        s.id = f.u64(0x08 + 8 * std::size_t(i));
        const std::size_t h = f.u32(0x198 + 4 * std::size_t(i));
        if (!f.has(h, 0x800)) return fail("GAMEDATA.BGD: stage " + std::to_string(i) + " header outside the file");
        const std::size_t t = f.u32(h + 0x3C0), size = f.u32(h + 0x3BC);
        if (!f.has(t, size) || size < 0x400) return fail("GAMEDATA.BGD: stage " + std::to_string(i) + " track data outside the file");
        for (std::size_t k = 0; k < 6; ++k) {
            std::array<float, 3> p{f.f32(t + 0x50 * k + 0x30), f.f32(t + 0x50 * k + 0x34), f.f32(t + 0x50 * k + 0x38)};
            if (p[0] != 0 || p[1] != 0 || p[2] != 0) s.starts.push_back(p);
        }
        const std::int32_t npick = std::int32_t(f.u32(t + 0x3F8)), nramp = std::int32_t(f.u32(t + 0x3FC));
        const std::size_t pick = t + f.u32(t + 0x3E0), ramp = t + f.u32(t + 0x3E4);
        if (npick < 0 || nramp < 0 || !f.has(pick, 0x20 * std::size_t(npick)) || !f.has(ramp, 0x50 * std::size_t(nramp)) ||
            pick + 0x20 * std::size_t(npick) > t + size || ramp + 0x50 * std::size_t(nramp) > t + size)
            return fail("GAMEDATA.BGD: stage " + std::to_string(i) + " pickups / ramps outside its track data");
        for (std::int32_t k = 0; k < npick; ++k) {
            const std::size_t o = pick + 0x20 * std::size_t(k);
            CrashPickup p;
            for (int c = 0; c < 3; ++c) p.pos[c] = f.f32(o + 4 * std::size_t(c));
            p.type = f.p[o + 0x10];
            p.modes = f.p[o + 0x11];
            s.pickups.push_back(p);
        }
        for (std::int32_t k = 0; k < nramp; ++k) {
            const std::size_t o = ramp + 0x50 * std::size_t(k);
            CrashRamp r;
            for (int row = 0; row < 4; ++row)
                for (int c = 0; c < 3; ++c) r.m[row][c] = f.f32(o + 0x10 * std::size_t(row) + 4 * std::size_t(c));
            r.type = std::int32_t(f.u32(o + 0x40));
            crash_ramp_corners(r.m, r.type, r.corner);
            s.ramps.push_back(r);
        }
        out.push_back(std::move(s));
    }
    return true;
}

void crash_ramp_corners(const float m[4][3], int type, float out[6][3]) {
    float b = 0, a = 0, c = 0, d = 0;   // types outside 0..3 leave Prepare's registers unset; 0 here [G]
    switch (type) {
    case 0: b = 4.0f; a = 3.0f; c = d = 1.2f; break;
    case 1: b = 6.0f; a = 4.0f; c = d = 3.0f; break;
    case 2: b = 7.0f; a = 2.55f; c = 3.2f; d = 1.0f; break;
    case 3: b = 7.0f; a = 2.55f; c = 1.0f; d = 3.2f; break;
    default: break;
    }
    const float* X = m[0];
    const float* Y = m[1];
    const float* Z = m[2];
    const float* P = m[3];
    for (int k = 0; k < 3; ++k) {
        // the operation order of the draft (CGtV3d + / - on the scaled axes)
        const float fwd = P[k] + Z[k] * b, back = P[k] - Z[k] * b;
        out[0][k] = fwd - X[k] * a;
        out[1][k] = fwd + X[k] * a;
        out[2][k] = back - X[k] * a;
        out[3][k] = back + X[k] * a;
        out[4][k] = out[0][k] + Y[k] * c;
        out[5][k] = out[1][k] + Y[k] * d;
    }
}

void crash_ramp_vertices(const float corner[6][3], int type, ImVertex out[kCrashRampVertices]) {
    struct V { int p; float u, v; };
    // CB3CrashRamp::Draw 0x3A1800 [C asm 0x3A1964..0x3A1CA8]: point index and the (u, v) stored at vertex +0x10 / +0x14
    static const V side_back[12] = {
        {5, 1, 0.5f}, {3, 0, 1}, {1, 1, 1},                                                   // RenderTrilist(.., 3)
        {4, 0, 0.5f}, {0, 0, 1}, {2, 1, 1},                                                   // RenderTrilist(.., 3)
        {5, 0, 0.5f}, {0, 1, 1}, {1, 0, 1}, {0, 1, 1}, {4, 1, 0.5f}, {5, 0, 0.5f},            // RenderTrilist(.., 6)
    };
    static const V slope2[6] = {{5, 1, 0}, {2, 0, 0.5f}, {3, 0, 0}, {5, 1, 0}, {4, 1, 0.5f}, {2, 0, 0.5f}};   // type 2
    static const V slope[6] = {{5, 1, 0}, {4, 1, 0.5f}, {3, 0, 0}, {4, 1, 0.5f}, {2, 0, 0.5f}, {3, 0, 0}};    // others
    auto put = [&](int i, const V& v) {
        std::memcpy(out[i].pos, corner[v.p], sizeof out[i].pos);
        out[i].uv[0] = v.u;
        out[i].uv[1] = v.v;
    };
    for (int i = 0; i < 12; ++i) put(i, side_back[i]);
    for (int i = 0; i < 6; ++i) put(12 + i, type == 2 ? slope2[i] : slope[i]);
}

void crash_pickup_vertices(ImVertex out[kCrashPickupVertices]) {
    // top (0, 1), "left" = pos - right (-1, 0), bottom (0, -1), "right" = pos + right (1, 0)
    static const ImVertex v[6] = {{{0, 1, 0}, {0, 0}}, {{-1, 0, 0}, {1, 0}}, {{0, -1, 0}, {1, 1}},
                                  {{0, 1, 0}, {0, 0}}, {{0, -1, 0}, {1, 1}}, {{1, 0, 0}, {0, 1}}};
    for (int i = 0; i < 6; ++i) out[i] = v[i];
}

}  // namespace b3asset
