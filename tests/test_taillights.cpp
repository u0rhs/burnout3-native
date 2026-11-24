// taillights (src/b3replay/taillights.h, b3asset carshade): at night and in the rain the tail lights glow at a steady
// level and rise to the full brake light with each car's brake level, smoothed over the last ticks.
#include "test.h"

#include "taillights.h"
#include "b3asset/carshade.h"

#include <cmath>
#include <vector>

using namespace b3replay;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

TEST(brake_level_is_the_mean_of_the_last_ticks) {
    const bool off[kBrakeTicks + 1] = {}, on[kBrakeTicks + 1] = {1, 1, 1, 1, 1, 1};
    CHECK(near(brake_level(off, 0.0f), 0.0f) && near(brake_level(off, 0.7f), 0.0f));
    CHECK(near(brake_level(on, 0.0f), 1.0f) && near(brake_level(on, 0.7f), 1.0f));
    const bool held2[kBrakeTicks + 1] = {0, 0, 0, 1, 1, 1};   // braking since two ticks ago, and the next tick
    CHECK(near(brake_level(held2, 0.0f), 0.4f));
    CHECK(near(brake_level(held2, 0.5f), 0.5f));
    CHECK(near(brake_level(held2, 1.0f), 0.6f));
}

TEST(a_tap_rises_and_falls_over_the_window) {
    // one tick of brake at tick 10: the level over ticks 8..16 (each frame at alpha 0)
    std::vector<bool> bit(30, false);
    bit[10] = true;
    std::vector<float> level;
    for (int i = 8; i <= 16; ++i) {
        bool w[kBrakeTicks + 1];
        for (int k = 0; k <= kBrakeTicks; ++k) w[k] = bit[std::size_t(i - (kBrakeTicks - 1) + k)];
        level.push_back(brake_level(w, 0.0f));
    }
    CHECK(near(level[0], 0.0f) && near(level[1], 0.0f));   // ticks 8, 9
    for (int i = 2; i <= 6; ++i) CHECK(near(level[std::size_t(i)], 0.2f));   // ticks 10..14: lit, dim (a tap)
    CHECK(near(level[7], 0.0f));                           // tick 15: out
    // interpolated frames move smoothly from one tick's value to the next's
    const bool w9[kBrakeTicks + 1] = {0, 0, 0, 0, 0, 1};
    CHECK(near(brake_level(w9, 0.25f), 0.05f));
}

TEST(rear_coronas_split_or_share_the_glow) {
    const RearCoronas one = rear_coronas(true, 0.4f, 0.0f), one_braking = rear_coronas(true, 0.4f, 1.0f);
    CHECK(near(one.tail, 0.0f) && near(one.brake, 0.4f));
    CHECK(near(one_braking.brake, 1.0f));
    CHECK(near(rear_coronas(true, 0.4f, 0.5f).brake, 0.7f));
    const RearCoronas two = rear_coronas(false, 0.4f, 0.0f), two_braking = rear_coronas(false, 0.4f, 1.0f);
    CHECK(near(two.tail, 0.4f) && near(two.brake, 0.0f));
    CHECK(near(two_braking.tail, 0.4f) && near(two_braking.brake, 1.0f));
}

TEST(same_lamps_matches_positions_in_any_order) {
    struct L { float pos[3]; };
    const std::vector<L> a = {{{-0.6f, 0.8f, -2.1f}}, {{0.6f, 0.8f, -2.1f}}};
    const std::vector<L> b = {{{0.6f, 0.81f, -2.1f}}, {{-0.6f, 0.8f, -2.1f}}};
    const std::vector<L> c = {{{0.6f, 0.5f, -2.1f}}, {{-0.6f, 0.5f, -2.1f}}};
    CHECK((same_lamps<L>(a, b)));
    CHECK(!(same_lamps<L>(a, c)));
    CHECK(!(same_lamps<L>(a, std::vector<L>{a[0]})));
}

TEST(lamp_mesh_levels) {
    CHECK(near(b3asset::rear_lamp_level(0x10, 0.4f, 0.0f), 0.4f));   // a brake lamp at the tail level
    CHECK(near(b3asset::rear_lamp_level(0x10, 0.4f, 0.9f), 0.9f));
    CHECK(near(b3asset::rear_lamp_level(0x08, 0.4f, 1.0f), 0.4f));   // a tail lamp stays at the tail level
    CHECK(b3asset::rear_lamp_level(0x04, 0.4f, 1.0f) < 0.0f);        // a head lamp: the game's
    CHECK(b3asset::rear_lamp_level(0x100, 0.4f, 1.0f) < 0.0f);       // glass
}

TEST(lamp_mesh_constants_mix_unlit_to_lit) {
    b3asset::CarScene sc;
    b3asset::CarMeshInput in = b3asset::car_mesh_input(0x00000010u, 0, 0);   // a brake lamp mesh, the game's bits off
    const b3asset::CarMeshConstants off = b3asset::car_mesh_constants(sc, in);
    b3asset::CarMeshInput lit_in = in;
    lit_in.lit_bits = 0x10;
    const b3asset::CarMeshConstants lit = b3asset::car_mesh_constants(sc, lit_in);
    CHECK(lit.no_env != 0 && off.no_env == 0);
    const b3asset::CarMeshConstants l0 = b3asset::car_mesh_constants_level(sc, in, 0.0f);
    const b3asset::CarMeshConstants l1 = b3asset::car_mesh_constants_level(sc, in, 1.0f);
    const b3asset::CarMeshConstants lh = b3asset::car_mesh_constants_level(sc, in, 0.5f);
    for (int i = 0; i < 4; ++i) {
        CHECK(near(l0.base[i], off.base[i]) && near(l0.full[i], off.full[i]) && l0.env[i] == off.env[i]);
        CHECK(near(l1.base[i], lit.base[i]) && near(l1.full[i], lit.full[i]) && l1.env[i] == lit.env[i]);
        CHECK(near(lh.base[i], 0.5f * (off.base[i] + lit.base[i])) && near(lh.full[i], 0.5f * (off.full[i] + lit.full[i])));
    }
    CHECK(l1.no_env == lit.no_env && lh.no_env == off.no_env);
    // the game's bit on (braking by day, or traffic's own tail bit): level 0 still turns it off, as asked
    CHECK(near(b3asset::car_mesh_constants_level(sc, lit_in, 0.0f).base[0], off.base[0]));
    // lit by another of its bits (a mesh in two groups): stays lit
    b3asset::CarMeshInput two = b3asset::car_mesh_input(0x00000014u, 0, 0x04);
    CHECK(near(b3asset::car_mesh_constants_level(sc, two, 0.0f).base[0], 192.0f));
}

TEST(tail_level_default) {
    CHECK(near(kTailLevelDefault, 0.40f));
    CHECK(tail_level() <= 1.0f);   // (-1 with B3_TAILLIGHTS=0)
}

TEST_MAIN("b3replay taillights")
