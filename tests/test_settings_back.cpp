// settingsback (tools/native/settings_back.h): the calls the SETTINGS pages' flow handler gets with SETTINGS BACK BUTTON
// Keep changes (the default) or Cancel (the game's), and the replay entry.
#include "test.h"

#include "settings_back.h"

#include <vector>

namespace {

using b3native::settings_back_calls;
using V = std::vector<std::uint32_t>;

TEST(cancel_is_the_games) {
    for (const std::uint32_t p2 : {1u, 3u, 0x4Au, 0x4Bu, 6u})
        CHECK(settings_back_calls(false, 5, p2) == V{p2});   // every call as it was
}

TEST(keep_rewrites_the_two_cancels) {
    CHECK(settings_back_calls(true, 5, 0x4A) == (V{0x4B}));        // the page's Back event: applied, not put back
    CHECK(settings_back_calls(true, 5, 3) == (V{1, 0x4B}));         // left with Back: confirm's scene, then applied
    CHECK(settings_back_calls(true, 5, 1) == V{1});                 // confirm, apply, other leave codes: untouched
    CHECK(settings_back_calls(true, 5, 0x4B) == V{0x4B});
    CHECK(settings_back_calls(true, 5, 6) == V{6});
    CHECK(settings_back_calls(true, 3, 3) == V{3});                 // a page opening (action 3) with p2 3: untouched
    CHECK(settings_back_calls(true, 3, 0x4A) == V{0x4A});
}

TEST(replay_entry) {
    CHECK(b3native::settings_back_entry("1"));
    CHECK(!b3native::settings_back_entry("0") && !b3native::settings_back_entry("") && !b3native::settings_back_entry(nullptr));
    CHECK(!b3native::settings_back_entry("11"));
}

}  // namespace

TEST_MAIN("settings back (tools/native/settings_back.h)")
