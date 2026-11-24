// hudmods (tools/native/takedown_pitch.h): TAKEDOWN STREAK PITCH's rule -- the streak since the last crash, the target
// 10 over it, the id that makes the game play World Tour's steps (max(n, d) for d 1..4, the bigger sound from 5),
// unchanged with the setting off, in World Tour, before the target and for anything but a takedown.
#include "test.h"

#define B3NATIVE_TAKEDOWN_RULE_ONLY
#include "takedown_pitch.h"

namespace {

using b3native::TakedownStreak;

TEST(game_steps) {
    CHECK(TakedownStreak::step_of(0x93) == 0 && TakedownStreak::step_of(0x9F) == 1 && TakedownStreak::step_of(0xA3) == 1);
    CHECK(TakedownStreak::step_of(0xA2) == 4 && TakedownStreak::step_of(0xA6) == 4 && TakedownStreak::step_of(0xAA) == -1);
}

TEST(climbs_past_ten) {
    TakedownStreak r;
    for (int c = 1; c <= 10; ++c) CHECK(r.step(4, 0x93, c, false, true).id == 0x93);   // up to the target: the game's
    TakedownStreak::Result s = r.step(4, 0x93, 11, false, true);
    CHECK(s.over == 1 && s.step == 1 && s.id == 0xA3 && !s.big);
    CHECK(r.step(4, 0x93, 14, false, true).id == 0xA6);
    s = r.step(4, 0x93, 15, false, true);
    CHECK(s.big && s.step == 0 && s.id == 0xAA);
    CHECK(r.step(4, 0x93, 19, false, true).id == 0xAE);
    CHECK(r.step(4, 0x93, 40, false, true).id == 0xAE);   // at most step 4
    CHECK(r.step(4, 0xA5, 12, false, true).id == 0xA5);   // a chain's own step 3 over d 2: the game's
}

TEST(crash_starts_again) {
    TakedownStreak r;
    CHECK(r.step(4, 0x93, 12, false, true).id == 0xA4);
    CHECK(r.step(3, 0x7F, 12, false, true).crash && r.base() == 12);
    CHECK(r.step(4, 0x93, 22, false, true).id == 0x93);   // 10 since the crash
    CHECK(r.step(4, 0x93, 23, false, true).id == 0xA3);
    CHECK(r.step(3, 0x80, 23, false, true).crash && r.base() == 23);
    CHECK(r.step(4, 0x93, 1, false, true).id == 0x93 && r.base() == 0);   // a new race: the count below the crash's
}

TEST(unchanged_when_off_or_world_tour) {
    TakedownStreak r;
    CHECK(r.step(4, 0x93, 13, false, false).id == 0x93);   // off
    CHECK(r.step(4, 0x93, 13, true, true).id == 0x93);     // World Tour: the game's own rule
    CHECK(r.step(4, 0x50, 13, false, true).id == 0x50);    // not a takedown id
    CHECK(r.step(1, 0x93, 13, false, true).id == 0x93);    // another class
    TakedownStreak q;   // off still follows the crashes (turning it on mid-race counts from the last crash)
    q.step(3, 0x7F, 5, false, false);
    CHECK(q.base() == 5 && q.step(4, 0x93, 16, false, true).id == 0xA3);
}

}  // namespace

TEST_MAIN("TAKEDOWN STREAK PITCH's rule (hudmods)")
