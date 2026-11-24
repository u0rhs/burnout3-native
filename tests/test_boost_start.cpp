// booststart (tools/native/boost_start.h): the wheel spin flag a human car held on a standing start gets
#include "test.h"

#include "boost_start.h"

namespace {

using b3native::boost_start_spin;

TEST(held_human_car_revving) {
    CHECK(boost_start_spin(true, 1, false, 0, 1.0f) == 1);    // accelerator floored: spinning
    CHECK(boost_start_spin(true, 1, false, 0, 0.6f) == 1);
    CHECK(boost_start_spin(true, 1, false, 0, 0.5f) == 0);    // half or less: not
    CHECK(boost_start_spin(true, 1, false, 0, 0.0f) == 0);
}

TEST(left_alone) {
    CHECK(boost_start_spin(false, 1, false, 0, 1.0f) == -1);  // a rolling start (the game's): never
    CHECK(boost_start_spin(true, 2, false, 0, 1.0f) == -1);   // an AI race car
    CHECK(boost_start_spin(true, 3, false, 0, 1.0f) == -1);   // a replay's car
    CHECK(boost_start_spin(true, 1, true, 0, 1.0f) == -1);    // the player's car AI-driven (attract demo, AI DRIVER)
    CHECK(boost_start_spin(true, 1, false, 1, 1.0f) == -1);   // racing (from the start through the finish)
    CHECK(boost_start_spin(true, 1, false, 2, 1.0f) == -1);   // any other state: not held
}

TEST(award_icon) {
    using b3native::boost_start_texture;
    CHECK(boost_start_texture(true, true, 0) == 7);     // BOOST START! on a standing start: the lightning-bolt sign
    CHECK(boost_start_texture(false, true, 0) == -1);   // (never otherwise: retail has no standing start)
    CHECK(boost_start_texture(true, false, 0) == -1);   // another message of type 0 (PERFECT LAP!, ...)
    CHECK(boost_start_texture(true, true, 6) == -1);    // another type
    CHECK(boost_start_texture(true, true, 0, 13) == 13 && boost_start_texture(true, true, 0, 16) == -1);
}

}  // namespace

TEST_MAIN("boost start (tools/native/boost_start.h)")
