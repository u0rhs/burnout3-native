// padskip (tools/native/pad_context.h, issue #14): the pad's context -- driving only while the game reads the player's
// accelerator outside its replays and films; a replay keeps the orbit camera; and a film on the front end's decoder
// (MovieSkip::film_playing) whatever the skip's setting.
#include "test.h"

#include "movie_skip.h"
#include "pad_context.h"

#include <map>

using namespace b3native;

TEST(driving_only_when_the_player_drives) {
    CHECK(pad_context(true, false, false) == PadContext::kDriving);
    CHECK(pad_context(false, false, false) == PadContext::kMenus);    // menus, paused, loading
    CHECK(pad_context(false, true, false) == PadContext::kMenus);     // a replay's page before its race runs
    CHECK(pad_context(true, false, true) == PadContext::kMenus);      // a film
    CHECK(pad_context(true, true, true) == PadContext::kMenus);
}

TEST(a_replay_is_not_driving_but_keeps_the_orbit_camera) {
    // CRASH RESULTS over the crash's replay: the replay re-runs the car through GetAccelerator
    const PadContext c = pad_context(true, true, false);
    CHECK(c == PadContext::kPlayback);
    CHECK(int(c) != 1);   // b3r_pad_context: the keys' driving bindings only for 1
    CHECK(int(c) != 0);   // the orbit camera's input for any non-zero
    CHECK(int(PadContext::kMenus) == 0 && int(PadContext::kDriving) == 1 && int(PadContext::kPlayback) == 2);
    CHECK(std::string(pad_context_name(c)) == "menus (replay playing)");
    CHECK(std::string(pad_context_name(PadContext::kDriving)) == "driving");   // (race_check greps "keys: driving")
}

TEST(film_playing_follows_the_decoder_not_the_skip_setting) {
    std::map<std::uint32_t, std::uint8_t> ram;
    MovieSkip s;
    s.load8 = [&](std::uint32_t a) { return ram[a]; };
    s.store8 = [&](std::uint32_t a, std::uint8_t v) { ram[a] = v; };
    const char* name = "ovid\\EndRace";
    for (std::uint32_t i = 0; name[i]; ++i) ram[0x1000 + i] = std::uint8_t(name[i]);
    s.prepared(0x1000);
    CHECK(!s.film_playing());                                 // not playing yet
    ram[MovieSkip::kDecoder + MovieSkip::kPlaying] = 1;
    CHECK(s.film_playing());
    s.enabled = false;                                        // --no-movie-skip: still a film on screen
    CHECK(s.film_playing() && !s.skippable());
    ram[MovieSkip::kDecoder + MovieSkip::kLoop] = 1;          // a looping one (the credits): a menu's
    CHECK(!s.film_playing());
    ram[MovieSkip::kDecoder + MovieSkip::kLoop] = 0;
    const char* bg = "tracks\\_FEMain";
    for (std::uint32_t i = 0; bg[i]; ++i) ram[0x2000 + i] = std::uint8_t(bg[i]);
    s.prepared(0x2000);                                       // a menu background
    CHECK(!s.film_playing());
}

TEST_MAIN("native pad context (padskip)")
