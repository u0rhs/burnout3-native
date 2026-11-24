// MORE RIVALS (tools/native/more_rivals_grid.h): the start spots for cars 7 and 8 (behind the last rank, else ahead of
// the front rank; on a drivable lane, along it, clear of every spot) and the race's record word.
#include "test.h"

#include "more_rivals_grid.h"
#include "../src/b3replay/records.h"

#include <vector>

namespace {

using b3native::GridQuad;
using b3native::GridSpot;
using b3native::GridV3;

// a straight road along +z from z0 to z1, x -8..8, flat at y 0: one quad per 10 m
std::vector<GridQuad> road(float z0, float z1) {
    std::vector<GridQuad> q;
    for (float z = z0; z < z1; z += 10.0f) q.push_back({{-8, 0, z}, {8, 0, z}, {-8, 0, z + 10}, {8, 0, z + 10}});
    return q;
}
// the game's grid: two columns (x -3 / +3), ranks 10 m apart, staggered 5 m, heading +z; the front at z = 100
std::vector<GridSpot> game_grid() {
    std::vector<GridSpot> s;
    for (int i = 0; i < 6; ++i) s.push_back({{i % 2 ? 3.0f : -3.0f, 0, 100.0f - 5.0f * float(i)}, {0, 0, 1}, -1});
    return s;
}

TEST(spots_behind_the_last_rank) {
    const std::vector<GridQuad> r = road(0, 200);
    const auto on = [&](const GridV3& p, const GridV3& h) { return b3native::grid_on_road(r, p, h); };
    for (const GridSpot& s : game_grid()) CHECK(on(s.pos, s.heading));   // the game's own spots pass the test
    const std::vector<GridSpot> e = b3native::grid_extra_spots(game_grid(), 2, on);
    CHECK(e.size() == 2);
    // each column carries on behind: z 75 = 80 - 5... the rearmost spots are z 75 (x 3) and 80 (x -3), each column's step 10
    CHECK(e[0].pos.z == 65.0f && e[0].pos.x == 3.0f && e[0].source == 5);
    CHECK(e[1].pos.z == 70.0f && e[1].pos.x == -3.0f && e[1].source == 4);
}

TEST(a_wall_behind_the_grid) {
    const std::vector<GridQuad> r = road(73, 200);   // the road starts 2 m behind the rearmost spot
    const auto on = [&](const GridV3& p, const GridV3& h) { return b3native::grid_on_road(r, p, h); };
    const std::vector<GridSpot> e = b3native::grid_extra_spots(game_grid(), 2, on);
    CHECK(e.size() == 2);
    CHECK(e[0].pos.z == 110.0f && e[1].pos.z == 105.0f);   // ahead of the front rank instead
}

TEST(off_the_road_and_against_the_lane) {
    const std::vector<GridQuad> r = road(0, 200);
    CHECK(!b3native::grid_on_road(r, {20, 0, 50}, {0, 0, 1}));    // beside the road
    CHECK(!b3native::grid_on_road(r, {0, 5, 50}, {0, 0, 1}));     // above it (a bridge's other level)
    CHECK(!b3native::grid_on_road(r, {0, 0, 50}, {0, 0, -1}));    // facing back down the lane
    CHECK(!b3native::grid_on_road(r, {0, 0, 50}, {1, 0, 0}));     // across it
    CHECK(b3native::grid_on_road(r, {0, 0.5f, 50}, {0.3f, 0, 1}));
}

TEST(no_room_at_all) {
    const std::vector<GridQuad> r = road(73, 102);   // neither behind nor ahead
    const auto on = [&](const GridV3& p, const GridV3& h) { return b3native::grid_on_road(r, p, h); };
    CHECK(b3native::grid_extra_spots(game_grid(), 2, on).empty());
}

TEST(record_word) {
    CHECK(b3native::more_rivals_record_word(0, 5) == 0);   // the game's own race
    CHECK(b3native::more_rivals_record_word(0, 7) == 0x81C2);
    CHECK(b3replay::records::options_folder(b3native::more_rivals_record_word(0, 7)) == "options-rivals-7");
    CHECK(b3replay::records::options_folder(b3native::more_rivals_record_word(0, 6)) == "options-rivals-6");
    // with raceopts' Heavy traffic and 4 laps (bits 0-1 3, bits 2-5 4)
    const std::uint64_t w = 0x8000u | 3u | 4u << 2;
    CHECK(b3replay::records::options_folder(b3native::more_rivals_record_word(w, 7)) == "options-traffic-heavy-laps-4-rivals-7");
    // the bits past 15 (options3's and the others') kept
    const std::uint64_t hi = 0x8002u | (std::uint64_t(1) << 20) | (std::uint64_t(3) << 35);
    CHECK((b3native::more_rivals_record_word(hi, 6) & ~std::uint64_t(7u << 6)) == hi && (b3native::more_rivals_record_word(hi, 6) >> 6 & 7u) == 6u);
}

}  // namespace

TEST_MAIN("MORE RIVALS: the start spots past the game's six, the record word")
