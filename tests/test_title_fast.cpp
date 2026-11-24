// titlefast (tools/native/title_fast.h): the title's wait -- the stamp a call sees (moved only with the feature on, while
// the page takes input and the game's two seconds are not over; then past the game's own test), the replay's entry.
#include "test.h"

#include "title_fast.h"

namespace {

using namespace b3native;

// the game's tests [C func_00438840 / func_00343800]: Start (and the prompt) only when !(clock <= stamp + 2.0)
bool game_allows(float clock, float stamp) { return !(clock <= stamp + kTitleWait); }

TEST(moved_within_the_wait_only) {
    const float stamp = 77.25f;
    for (float clock : {77.25f, 77.5f, 78.2f, 79.25f}) {   // the page's first input frame (~0.93 s) .. the wait's end
        const TitleStamp t = title_stamp(true, true, clock, stamp);
        CHECK(t.moved);
        CHECK(game_allows(clock, t.stamp));               // the game's test passes with the moved stamp
        CHECK(!game_allows(clock, stamp));                // and not with its own
    }
    const TitleStamp after = title_stamp(true, true, 79.5f, stamp);   // the wait over: the game's own stamp
    CHECK(!after.moved && after.stamp == stamp && game_allows(79.5f, stamp));
}

TEST(only_when_on_and_taking_input) {
    CHECK(!title_stamp(false, true, 78.0f, 77.0f).moved);   // off (B3NATIVE_TITLE_FAST=0, an old replay): the game's
    CHECK(!title_stamp(true, false, 78.0f, 77.0f).moved);   // the page leading in or out: the game's
    CHECK(title_stamp(true, false, 78.0f, 77.0f).stamp == 77.0f);
}

TEST(replay_entry) {
    CHECK(title_fast_entry("1"));
    CHECK(!title_fast_entry("0"));
    CHECK(!title_fast_entry(""));
    CHECK(!title_fast_entry(nullptr));
}

}  // namespace

TEST_MAIN("the title's prompt and Start without the wait (titlefast)")
