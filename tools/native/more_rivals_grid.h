// more_rivals_grid.h -- MORE RIVALS: start spots for the cars past the game's 6, the race's record word (pure:
// tests/test_more_rivals.cpp).
//
// Every stage's track data holds 6 hand-placed CB3StartPositionStruct (0x50 bytes; +0x00 a 4 x 4 float matrix, rows
// q0..q3 as CB3PhysicsManager::AddRaceCar copies them, the position in q3 at +0x30; then the stream unit and AI lane
// fields): a staggered two-column grid, ranks 7-14 m apart, the player's spot mid-grid. A spot for car 7, 8 is one of
// the candidates below, the first that is on the road and clear of every other spot:
//   1. behind the last rank, each column carrying on its own step (the rear spot moved back by the step to the spot
//      ahead of it in its column), then two steps back;
//   2. ahead of the front rank the same way (a point-to-point route can start against a barrier).
// "On the road": inside a drivable AI lane segment's quad (the lane's left / right points of the segment and the next,
// seen from above), within kRoadHeight of its height, the lane running within ~45 degrees of the spot's own heading.
// "Clear": kClear from every other spot (a car is ~4.5 m by 2 m).
// The new spot is a copy of its source spot (its matrix, stream unit and lane fields) with the position moved.
#ifndef B3NATIVE_MORE_RIVALS_GRID_H
#define B3NATIVE_MORE_RIVALS_GRID_H

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace b3native {

struct GridV3 {
    float x = 0, y = 0, z = 0;
    GridV3 operator+(const GridV3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    GridV3 operator-(const GridV3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    GridV3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const GridV3& o) const { return x * o.x + y * o.y + z * o.z; }
    float flat_len() const { return std::sqrt(x * x + z * z); }
};

constexpr float kGridClear = 4.0f;      // metres between spots (ground plane)
constexpr float kRoadHeight = 2.5f;     // metres above / below the lane's quad
constexpr float kRoadHeading = 0.7f;    // cos of the largest angle between the spot's heading and the lane

// One drivable lane segment seen from above: its four points (left / right of the segment, then of the next)
struct GridQuad {
    GridV3 l0, r0, l1, r1;
    GridV3 dir() const { return ((l1 + r1) - (l0 + r0)) * 0.5f; }
};

// Point in the quad's ground-plane outline (two triangles), and its height there (the four points' mean)
inline bool grid_in_triangle(float px, float pz, const GridV3& a, const GridV3& b, const GridV3& c) {
    const float d1 = (px - b.x) * (a.z - b.z) - (a.x - b.x) * (pz - b.z);
    const float d2 = (px - c.x) * (b.z - c.z) - (b.x - c.x) * (pz - c.z);
    const float d3 = (px - a.x) * (c.z - a.z) - (c.x - a.x) * (pz - a.z);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}
inline bool grid_on_quad(const GridQuad& q, const GridV3& p, const GridV3& heading) {
    if (!grid_in_triangle(p.x, p.z, q.l0, q.r0, q.r1) && !grid_in_triangle(p.x, p.z, q.l0, q.r1, q.l1)) return false;
    const float h = (q.l0.y + q.r0.y + q.l1.y + q.r1.y) * 0.25f;
    if (std::fabs(p.y - h) > kRoadHeight) return false;
    const GridV3 d = q.dir();
    const float n = d.flat_len() * std::sqrt(heading.x * heading.x + heading.z * heading.z);
    return n > 0 && (d.x * heading.x + d.z * heading.z) / n >= kRoadHeading;
}
inline bool grid_on_road(const std::vector<GridQuad>& road, const GridV3& p, const GridV3& heading) {
    for (const GridQuad& q : road)
        if (grid_on_quad(q, p, heading)) return true;
    return false;
}

// A spot: its position and heading (the matrix row the spots spread along, signed as the lane runs)
struct GridSpot {
    GridV3 pos, heading;
    int source = -1;   // a new spot: the game's spot it copies
};

// The spot ahead of / behind `i` in its column: the nearest along the heading on that side, the same column first
inline int grid_neighbour(const std::vector<GridSpot>& s, int i, bool ahead) {
    int best = -1;
    float bestd = 1e30f;
    const GridV3 f = s[i].heading;
    for (int j = 0; j < int(s.size()); ++j) {
        if (j == i) continue;
        const GridV3 d = s[j].pos - s[i].pos;
        const float t = d.dot(f);
        if (ahead ? t <= 1.0f : t >= -1.0f) continue;
        const GridV3 lat = d - f * t;
        const float score = lat.dot(lat) * 100.0f + t * t;
        if (score < bestd) { bestd = score; best = j; }
    }
    return best;
}

// The candidates in order (behind the last rank, two steps behind, ahead of the front rank, two steps ahead)
inline std::vector<GridSpot> grid_candidates(const std::vector<GridSpot>& s) {
    std::vector<int> order(s.size());
    for (int i = 0; i < int(s.size()); ++i) order[i] = i;
    auto along = [&](int i) { return s[i].pos.dot(s[0].heading); };
    for (int i = 0; i < int(order.size()); ++i)
        for (int j = i + 1; j < int(order.size()); ++j)
            if (along(order[j]) < along(order[i])) std::swap(order[i], order[j]);
    std::vector<GridSpot> out;
    for (int steps = 1; steps <= 2; ++steps)
        for (int k = 0; k < 2 && k < int(order.size()); ++k) {   // the rearmost two: one per column
            const int r = order[k], a = grid_neighbour(s, r, true);
            if (a < 0) continue;
            out.push_back({s[r].pos + (s[r].pos - s[a].pos) * float(steps), s[r].heading, r});
        }
    for (int steps = 1; steps <= 2; ++steps)
        for (int k = 0; k < 2 && k < int(order.size()); ++k) {   // the front two
            const int f = order[order.size() - 1 - k], b = grid_neighbour(s, f, false);
            if (b < 0) continue;
            out.push_back({s[f].pos + (s[f].pos - s[b].pos) * float(steps), s[f].heading, f});
        }
    return out;
}

// The spots for `extra` more cars: the first candidates on the road and clear of every spot so far. Fewer than asked
// when the candidates run out (the caller then has fewer cars).
inline std::vector<GridSpot> grid_extra_spots(const std::vector<GridSpot>& game, int extra,
                                              const std::function<bool(const GridV3&, const GridV3&)>& on_road) {
    std::vector<GridSpot> all = game, out;
    for (const GridSpot& c : grid_candidates(game)) {
        if (int(out.size()) >= extra) break;
        bool clear = true;
        for (const GridSpot& o : all)
            if ((o.pos - c.pos).flat_len() < kGridClear) clear = false;
        if (!clear || !on_road(c.pos, c.heading)) continue;
        out.push_back(c);
        all.push_back(c);
    }
    return out;
}

// The renderer's record word for a race with `rivals` (> 5): raceopts' word (race_choice.h RaceChoice::record_word, 0:
// the game's options; 64 bits since options3) with the rivals in its RIVALS field, bits 6-8 (Road Rage's 1..5 there, a
// Race's 6 / 7 here: one owner, tests/test_bit_owners.cpp; src/b3replay/records.h options_folder "options[-...]-rivals-7")
inline std::uint64_t more_rivals_record_word(std::uint64_t w, int rivals) {
    if (rivals <= 5 || rivals > 7) return w;
    return (((w & 0x8000u) ? w : std::uint64_t(0x8002u)) & ~std::uint64_t(7u << 6)) | std::uint64_t(unsigned(rivals) << 6);   // (0x8002: Normal traffic)
}

}  // namespace b3native

#endif
