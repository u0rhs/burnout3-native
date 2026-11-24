// wreckguard (tools/native/wreck_slots.h): with all 64 crashed-traffic slots in use, the wreck released is the one
// farthest from the nearest race car, never one in this frame's colliding pairs, at rest and unlinked first.
#include "test.h"

#include "wreck_slots.h"

namespace {

using b3native::WreckCandidate;

TEST(farthest_unlinked_first) {
    CHECK(b3native::wreck_to_release({}) == -1);
    CHECK(b3native::wreck_to_release({{100.0f}, {900.0f}, {400.0f}}) == 1);
    CHECK(b3native::wreck_to_release({{100.0f}, {900.0f, true}, {400.0f}}) == 2);   // a linked one waits
    CHECK(b3native::wreck_to_release({{100.0f, true}, {900.0f, true}}) == 1);       // only linked ones left
    CHECK(b3native::wreck_to_release({{5.0f}}) == 0);
}

TEST(never_one_in_a_pair_and_at_rest_first) {
    CHECK(b3native::wreck_to_release({{900.0f, false, false, true}, {100.0f}}) == 1);   // the farthest is being collided
    CHECK(b3native::wreck_to_release({{900.0f, false, false, true}}) == -1);           // none may go
    CHECK(b3native::wreck_to_release({{900.0f, false, true}, {100.0f}}) == 1);          // a moving one after those at rest
    CHECK(b3native::wreck_to_release({{900.0f, false, true}, {100.0f, true}}) == 0);    // moving unlinked before linked
}

}  // namespace

TEST_MAIN("crashed-traffic slots bounded (wreckguard)")
