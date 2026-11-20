// todrace (tools/native/race_time.h): DAY / NIGHT per race on SINGLE EVENT's track page -- the choice's lifetime: stepped
// by presses only while the page takes input, taken by the race at the loading screen (each load, so a restart keeps
// it), back to Default on the main menu (before any World Tour race); the label's fade; playback's set_choice.
#include "test.h"

#include "race_time.h"

#include <cstdint>
#include <string>

namespace {

using b3native::RaceTime;
constexpr std::uint32_t kPage = b3native::kSelectOptionsPage, kMain = b3native::kRaceTimeMainMenu, kLoad = 0x01E9583Cu;
constexpr std::uint32_t kCarSelect = 0x01E94EF0u, kModes = 0x01E94E4Cu, kNone = 0;
constexpr std::uint32_t kInput = b3native::kPageStateInput, kLeadOut = 3;

// n VBlanks on `page` (state, loading as given), no presses
void idle(RaceTime& r, std::uint32_t page, int n, std::uint32_t state = kInput, bool loading = false) {
    for (int k = 0; k < n; ++k) r.step(page, state, loading, 0);
}
// a race: the loading screen for a while, then the race (no menu page)
void race(RaceTime& r) {
    idle(r, kLoad, 30, 0, true);
    idle(r, kNone, 60, 0);
}

TEST(steps_only_on_the_page_taking_input) {
    RaceTime r;
    CHECK(r.choice() == b3native::kRaceTimeDefault && r.race() == b3native::kRaceTimeDefault && r.opacity() == 0);
    r.step(kModes, kInput, false, 1);                 // another page: ignored
    CHECK(r.choice() == b3native::kRaceTimeDefault);
    r.step(kPage, kLeadOut, false, 1);                // the page leading in / out (state not 5): ignored
    CHECK(r.choice() == b3native::kRaceTimeDefault);
    auto s = r.step(kPage, kInput, false, 1);
    CHECK(s.choice_changed && r.choice() == b3native::kRaceTimeDay);
    s = r.step(kPage, kInput, false, 1);
    CHECK(s.choice_changed && r.choice() == b3native::kRaceTimeNight);
    s = r.step(kPage, kInput, false, 1);              // round to Default
    CHECK(s.choice_changed && r.choice() == b3native::kRaceTimeDefault);
    s = r.step(kPage, kInput, false, 2);              // two presses in one VBlank: two steps
    CHECK(r.choice() == b3native::kRaceTimeNight && r.presses() == 5);
    s = r.step(kPage, kInput, false, 0);
    CHECK(!s.choice_changed && !s.race_changed);
    CHECK(r.race() == b3native::kRaceTimeDefault);    // not before the next race starts
}

TEST(the_race_takes_it_at_the_load_and_a_restart_keeps_it) {
    RaceTime r;
    idle(r, kPage, 10);
    r.step(kPage, kInput, false, 2);                  // Night
    idle(r, kCarSelect, 20);
    CHECK(r.race() == b3native::kRaceTimeDefault);
    auto s = r.step(kLoad, 0, true, 0);               // the loading screen comes up
    CHECK(s.race_changed && r.race() == b3native::kRaceTimeNight);
    s = r.step(kLoad, 0, true, 0);
    CHECK(!s.race_changed);
    idle(r, kNone, 100, 0);
    race(r);                                          // a restart (a load again): still Night
    CHECK(r.race() == b3native::kRaceTimeNight && r.choice() == b3native::kRaceTimeNight);
    idle(r, kModes, 10);                              // back to SELECT MODE / the track page: kept
    idle(r, kPage, 10);
    CHECK(r.choice() == b3native::kRaceTimeNight);
    r.step(kPage, kInput, false, 0);
    race(r);
    CHECK(r.race() == b3native::kRaceTimeNight);
}

TEST(the_main_menu_resets_it_before_any_world_tour_race) {
    RaceTime r;
    idle(r, kPage, 5);
    r.step(kPage, kInput, false, 2);                  // Night
    race(r);
    CHECK(r.race() == b3native::kRaceTimeNight);
    auto s = r.step(kMain, kInput, false, 0);         // the main menu
    CHECK(s.choice_changed && r.choice() == b3native::kRaceTimeDefault);
    CHECK(r.race() == b3native::kRaceTimeNight);      // (the front end draws no night: the race keeps it until the next load)
    idle(r, kMain, 10);
    r.step(kMain, kInput, false, 3);                  // presses elsewhere do nothing
    CHECK(r.choice() == b3native::kRaceTimeDefault);
    race(r);                                          // a World Tour race (no track page on its way)
    CHECK(r.race() == b3native::kRaceTimeDefault);
}

TEST(the_label_fades_with_the_page) {
    RaceTime r;
    auto s = r.step(kPage, kLeadOut, false, 0);       // leading in: not yet
    CHECK(r.opacity() == 0 && !s.label_changed);
    int n = 0;
    while (r.opacity() < 255 && n < 100) { r.step(kPage, kInput, false, 0); ++n; }
    CHECK(n == RaceTime::kFadeSteps);
    s = r.step(kPage, kInput, false, 0);
    CHECK(!s.label_changed);
    s = r.step(kPage, kInput, false, 1);              // a press: the label's text changes
    CHECK(s.label_changed && s.choice_changed);
    n = 0;
    while (r.opacity() > 0 && n < 100) { r.step(kPage, kLeadOut, false, 0); ++n; }   // the page's lead-out
    CHECK(n == RaceTime::kFadeSteps);
}

TEST(playback_sets_the_recorded_choice) {
    RaceTime live, play;
    // live: two presses on the page, the race, the main menu; playback: the same pages, the recorded ops instead of presses
    const struct { std::uint32_t page, state; bool loading; int presses; } script[] = {
        {kModes, kInput, false, 0}, {kPage, kLeadOut, false, 0}, {kPage, kInput, false, 1}, {kPage, kInput, false, 0},
        {kPage, kInput, false, 1}, {kCarSelect, kInput, false, 0}, {kLoad, 0, true, 0}, {kNone, 0, false, 0},
        {kMain, kInput, false, 0}, {kModes, kInput, false, 0}, {kPage, kInput, false, 0}, {kLoad, 0, true, 0}};
    int recorded = -1;   // the "race_time" op, as native_play records it (on a change; kept once it was not Default)
    bool any = false;
    for (const auto& v : script) {
        const auto s = live.step(v.page, v.state, v.loading, v.presses);
        if (s.choice_changed && (any || live.choice() != b3native::kRaceTimeDefault)) { recorded = live.choice(); any = true; }
        if (recorded >= 0) play.set_choice(recorded);
        play.step(v.page, v.state, v.loading, 0);
        CHECK(play.choice() == live.choice() && play.race() == live.race() && play.opacity() == live.opacity());
    }
    CHECK(live.race() == b3native::kRaceTimeDefault && recorded == b3native::kRaceTimeDefault);
    play.set_choice(7);   // a value no build writes: Default
    CHECK(play.choice() == b3native::kRaceTimeDefault);
}

TEST(the_other_track_pages) {
    // CRASH's junction page and MULTIPLAYER's track page take presses too (one choice for both split-screen views)
    CHECK(b3native::race_time_page(b3native::kJunctionPage) && b3native::race_time_page(b3native::kMultiTrackPage | 0x20000000u));
    CHECK(!b3native::race_time_page(kCarSelect) && !b3native::race_time_page(kMain) && !b3native::race_time_page(kModes));
    RaceTime r;
    r.step(b3native::kJunctionPage, kInput, false, 2);
    CHECK(r.choice() == b3native::kRaceTimeNight);
    r.step(b3native::kMultiTrackPage, kInput, false, 1);
    CHECK(r.choice() == b3native::kRaceTimeDefault);
    r.step(b3native::kMultiTrackPage, kLeadOut, false, 1);
    CHECK(r.choice() == b3native::kRaceTimeDefault);
}

TEST(names) {
    CHECK(std::string(b3native::race_time_name(0)) == "Default");
    CHECK(std::string(b3native::race_time_name(1)) == "Day");
    CHECK(std::string(b3native::race_time_name(2)) == "Night");
    CHECK(b3native::kSelectOptionsPage == 0x01E96F78u && b3native::kRaceTimeMainMenu == 0x01E94C40u);
}

}  // namespace

TEST_MAIN("DAY / NIGHT per race on the track page (todrace)")
