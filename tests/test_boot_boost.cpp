// gamemods1 (tools/native/boot_boost.h): START AT's rules -- the films ended only until the title is up and only when a
// start other than the game's is chosen; Start pressed for the profile targets once the title takes it, released when
// the title answers or after the longest hold; the profile loaded once, at the first flow event 3 with the boot flag.
#include "test.h"

#define B3NATIVE_BOOT_RULE_ONLY
#include "boot_boost.h"

namespace {

using b3native::BootBoostRule;

TEST(targets) {
    CHECK(BootBoostRule::clamp(-1) == 0 && BootBoostRule::clamp(5) == 0 && BootBoostRule::clamp(3) == 3);
    CHECK(!BootBoostRule::profile(BootBoostRule::kTitle) && BootBoostRule::profile(BootBoostRule::kProfile3));
    CHECK(BootBoostRule::slot(BootBoostRule::kProfile1) == 0 && BootBoostRule::slot(BootBoostRule::kProfile3) == 2);
}

TEST(films_until_the_title) {
    BootBoostRule g;
    CHECK(!g.skip_films(BootBoostRule::kGame));   // the game's start: every film plays
    BootBoostRule r;
    CHECK(r.skip_films(BootBoostRule::kTitle));
    r.title_up(BootBoostRule::kTitle);
    CHECK(!r.skip_films(BootBoostRule::kTitle) && !r.start_pending());   // the title: no press for the title target
}

TEST(press_then_load) {
    BootBoostRule r;
    const int t = BootBoostRule::kProfile2;
    r.title_up(t);
    CHECK(r.start_pending() && !r.press());       // the title not ready yet
    r.title_ready(5, 1.0f, 0.0f);
    CHECK(!r.press());                            // its 2 s wait
    r.title_ready(4, 3.0f, 0.0f);
    CHECK(!r.press());                            // the page manager not taking input
    r.title_ready(5, 2.5f, 0.0f);
    CHECK(r.press() && r.press());                // held
    CHECK(!r.autoload(t, 2, true));               // another event
    CHECK(r.autoload(t, 3, true) && !r.start_pending() && !r.press());   // the title answered: released, loaded
    CHECK(!r.autoload(t, 3, true) && r.loaded());  // once
}

TEST(no_flag_no_load_and_hold_limit) {
    BootBoostRule r;
    const int t = BootBoostRule::kProfile1;
    r.title_up(t);
    r.title_ready(5, 9.0f, 0.0f);
    int held = 0;
    while (r.press()) ++held;
    CHECK(held == BootBoostRule::kPressVBlanks && !r.start_pending());   // never answered: let go
    CHECK(!r.autoload(t, 3, false));               // no boot flag: the game's way
    CHECK(!r.autoload(BootBoostRule::kTitle, 3, true));
}

}  // namespace

TEST_MAIN("START AT's rules (gamemods1)")
