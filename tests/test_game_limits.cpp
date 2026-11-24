// gamemods1 (tools/native/game_limits.h): END VERY LONG EVENTS' two checks as the game makes them -- the world update's
// end after 1800 s (2400 s with the longer limit), not in a replay; the near end outside World Tour after 1500 s.
#include "test.h"

#define B3NATIVE_LIMITS_RULE_ONLY
#include "game_limits.h"

namespace {

using b3native::GameLimitsRule;

TEST(world_end) {
    CHECK(!GameLimitsRule::world_end_due(1800.0f, false, false));
    CHECK(GameLimitsRule::world_end_due(1800.5f, false, false));
    CHECK(!GameLimitsRule::world_end_due(2000.0f, true, false) && GameLimitsRule::world_end_due(2400.5f, true, false));
    CHECK(!GameLimitsRule::world_end_due(5000.0f, false, true));   // a replay: the game never ends it either
}

TEST(near_end) {
    CHECK(!GameLimitsRule::near_end_due(1500.0f, false));
    CHECK(GameLimitsRule::near_end_due(1500.5f, false));
    CHECK(!GameLimitsRule::near_end_due(1700.0f, true));   // World Tour: never
}

}  // namespace

TEST_MAIN("END VERY LONG EVENTS' checks (gamemods1)")
