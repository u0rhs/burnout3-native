// trafficcheck (tools/native/traffic_check_rules.h, traffic_check.h): PC SETTINGS > GAME > GAMEPLAY's TRAFFIC CHECKING --
// which races take it (stages, the latch), what counts as a check (the way the traffic goes, its category), the push, the
// chain's scoring and its end, the star levels, the replay's entry and the records kept apart (src/b3replay/records.h).
#include "test.h"

#include "traffic_check_rules.h"
#include "../src/b3replay/records.h"

#include <string>

namespace {

using b3native::TcChain;
using b3native::TcEvent;
using b3native::TrafficCheckLatch;

TEST(stages_are_single_event_and_two_player) {
    CHECK(b3native::traffic_check_stage(0x0051AD00u, 0x0051B920u));    // SINGLE EVENT
    CHECK(b3native::traffic_check_stage(0x0051AEC0u, 0x0051B920u));    // two players
    CHECK(!b3native::traffic_check_stage(0x0051AD00u, 0x0051B928u));   // World Tour
    CHECK(!b3native::traffic_check_stage(0x0051AD00u, 0x0051B988u));   // Crash party
    CHECK(!b3native::traffic_check_stage(0x01E900B0u, 0x0051B920u));   // the front end
    CHECK(!b3native::traffic_check_stage(0, 0));
}

TEST(latched_at_the_race_start) {
    TrafficCheckLatch l;
    CHECK(!l.latched() && l.mode() == b3native::kTcOff);
    CHECK(!l.step(false, b3native::kTcRevenge));            // the front end: never on, whatever is asked
    CHECK(l.step(true, b3native::kTcRevenge) && l.latched() && l.mode() == b3native::kTcRevenge);
    CHECK(!l.step(true, b3native::kTcExtreme) && l.mode() == b3native::kTcRevenge);   // a change applies from the next race
    CHECK(l.step(false, b3native::kTcOff) && !l.latched());  // the race is over
    CHECK(l.step(true, b3native::kTcExtreme) && l.mode() == b3native::kTcExtreme);
    CHECK(l.step(false, 0) && !l.step(true, 0) && !l.latched());   // the next race without it
    CHECK(!l.step(true, b3native::kTcRevenge) && !l.latched());     // (a restart: the same stage, the same latch)
    TrafficCheckLatch m;
    CHECK(!m.step(true, 7) && !m.latched());   // (an unknown value: off)
}

TEST(revenge_and_every_car) {
    using b3native::traffic_checkable;
    // Revenge: a car or van going your way only
    CHECK(traffic_checkable(b3native::kTcRevenge, 1, 1, 0, 0.9f, 0.1f));
    CHECK(traffic_checkable(b3native::kTcRevenge, 2, 1, 0, 1, 0));
    CHECK(!traffic_checkable(b3native::kTcRevenge, 1, 1, 0, -1, 0));   // oncoming
    CHECK(!traffic_checkable(b3native::kTcRevenge, 1, 1, 0, 0, 1));    // crossing
    CHECK(!traffic_checkable(b3native::kTcRevenge, 5, 1, 0, 1, 0));    // a bus
    CHECK(!traffic_checkable(b3native::kTcRevenge, 11, 1, 0, 1, 0));   // a trailer
    // Extreme: any of them
    for (int c : {1, 3, 4, 5, 11}) CHECK(traffic_checkable(b3native::kTcExtreme, c, 1, 0, -1, 0) && traffic_checkable(b3native::kTcExtreme, c, 1, 0, 0, 1));
    CHECK(!traffic_checkable(b3native::kTcOff, 1, 1, 0, 1, 0));
}

TEST(extreme_has_no_speed_floor) {
    using b3native::traffic_check_speed_ok;
    // Revenge from 75 mph (its mrCheckingTrafficMinSpeed); Extreme any fatal hit: the game's own fatal test (closing speed
    // above 75 mph times the car's factor, or a car pinned against a wall at any speed) is the only floor
    CHECK(traffic_check_speed_ok(b3native::kTcRevenge, b3native::kTcMinSpeed));
    CHECK(!traffic_check_speed_ok(b3native::kTcRevenge, b3native::kTcMinSpeed - 0.01f));
    CHECK(!traffic_check_speed_ok(b3native::kTcRevenge, 12.0f));      // 27 mph into an oncoming car: Revenge crashes
    for (float v : {0.0f, 0.8f, 12.0f, 33.0f, 60.0f}) CHECK(traffic_check_speed_ok(b3native::kTcExtreme, v));   // 2 mph pinned too
    CHECK(!traffic_check_speed_ok(b3native::kTcOff, 60.0f));
}

TEST(shunts_check_from_75_mph) {
    using b3native::traffic_check_shunt_ok;
    // a hit the game only shunts (closing speed under its 75 mph) is a check from 75 mph of the car's own speed, both modes
    CHECK(traffic_check_shunt_ok(b3native::kTcRevenge, b3native::kTcMinSpeed));
    CHECK(traffic_check_shunt_ok(b3native::kTcExtreme, 50.0f));
    CHECK(!traffic_check_shunt_ok(b3native::kTcRevenge, b3native::kTcMinSpeed - 0.01f));
    CHECK(!traffic_check_shunt_ok(b3native::kTcExtreme, 20.0f));   // (a slow bump stays the game's shunt)
    CHECK(!traffic_check_shunt_ok(b3native::kTcOff, 60.0f));
}

TEST(wreck_touches) {
    using b3native::TcWreckTouch;
    using b3native::traffic_wreck_touch;
    // the checker's own checked car, of any age: never a crash (both modes)
    CHECK(traffic_wreck_touch(b3native::kTcRevenge, true, false) == TcWreckTouch::kOwn);
    CHECK(traffic_wreck_touch(b3native::kTcExtreme, true, false) == TcWreckTouch::kOwn);
    // a wreck of crashed traffic: Revenge's crash, Extreme's check
    CHECK(traffic_wreck_touch(b3native::kTcRevenge, false, false) == TcWreckTouch::kGame);
    CHECK(traffic_wreck_touch(b3native::kTcExtreme, false, false) == TcWreckTouch::kCheck);
    // another player's checked car now: the game's (it takes you out, the takedown theirs)
    CHECK(traffic_wreck_touch(b3native::kTcExtreme, false, true) == TcWreckTouch::kGame);
    CHECK(traffic_wreck_touch(b3native::kTcRevenge, false, true) == TcWreckTouch::kGame);
    CHECK(traffic_wreck_touch(b3native::kTcOff, true, false) == TcWreckTouch::kGame);
}

TEST(the_traffic_must_go_your_way) {
    CHECK(b3native::traffic_same_way(1, 0, 1, 0));
    CHECK(b3native::traffic_same_way(1, 0, 0.6f, 0.8f));      // 53 degrees
    CHECK(b3native::traffic_same_way(1, 0, 0.35f, 0.9368f));  // 69.5 degrees: within Revenge's 70
    CHECK(!b3native::traffic_same_way(1, 0, 0.33f, 0.944f));  // 70.7 degrees: crossing
    CHECK(!b3native::traffic_same_way(0, 1, 1, 0));           // crossing at a junction
    CHECK(!b3native::traffic_same_way(1, 0, -1, 0));          // oncoming: still a crash, as in Revenge
    CHECK(b3native::traffic_oncoming(1, 0, -1, 0) && b3native::traffic_oncoming(1, 0, -0.6f, 0.8f));
    CHECK(!b3native::traffic_oncoming(1, 0, 0, 1) && !b3native::traffic_oncoming(1, 0, 1, 0));
    CHECK_NEAR(b3native::kTcMinSpeed * 2.2369363, 75.0, 0.01);   // Revenge's 75 mph
}

TEST(revenge_vehicles) {
    for (int c : {1, 2, 3}) CHECK(b3native::traffic_checkable_category(c));   // car, van, lorry
    for (int c : {0, 4, 5, 6, 7, 8, 9, 10, 11}) CHECK(!b3native::traffic_checkable_category(c));
    for (int c : {4, 5, 11}) CHECK(b3native::traffic_heavy_category(c));   // big rig, bus, trailer: Revenge's uncheckable set
    for (int c : {1, 2, 3, 6, 8}) CHECK(!b3native::traffic_heavy_category(c));
}

TEST(the_launch_and_recoil) {
    CHECK_NEAR(b3native::kLaunch, 1.5, 1e-6);   // Revenge's
    CHECK_NEAR(b3native::traffic_recoil_speed(10.0f), 1.0, 1e-6);
    CHECK_NEAR(b3native::traffic_recoil_speed(80.0f), 3.0, 1e-6);
    CHECK_NEAR(b3native::traffic_recoil_speed(-3.0f), 0.0, 1e-6);
}

TEST(points_boost_and_shots) {
    CHECK(b3native::traffic_event_points(TcEvent::kCheck) == 50 && b3native::traffic_event_points(TcEvent::kTrickShot) == 50 &&
          b3native::traffic_event_points(TcEvent::kSkillShot) == 100 && b3native::traffic_event_points(TcEvent::kExtremeShot) == 150);
    CHECK_NEAR(b3native::traffic_event_boost(TcEvent::kCheck, 60.0f), 30.0, 1e-6);   // half B3's near miss: Revenge's 30
    CHECK_NEAR(b3native::traffic_event_boost(TcEvent::kTrickShot, 60.0f), 40.0, 1e-6);
    CHECK_NEAR(b3native::traffic_event_boost(TcEvent::kSkillShot, 60.0f), 60.0, 1e-6);
    CHECK_NEAR(b3native::traffic_event_boost(TcEvent::kExtremeShot, 60.0f), 80.0, 1e-6);
    TcEvent e{};
    CHECK(!b3native::traffic_wreck_event(1, false, e));                                // a car going the same way: nothing
    CHECK(b3native::traffic_wreck_event(2, true, e) && e == TcEvent::kTrickShot);      // an oncoming car
    CHECK(b3native::traffic_wreck_event(5, false, e) && e == TcEvent::kSkillShot);     // a bus
    CHECK(b3native::traffic_wreck_event(4, true, e) && e == TcEvent::kExtremeShot);    // an oncoming big rig
    CHECK(b3native::traffic_shot_sign(TcEvent::kSkillShot) == b3native::TcSign::kSkillShot);
}

TEST(the_chain) {
    TcChain c;
    CHECK(!c.active() && !c.over(10.0f));
    c.add(TcEvent::kCheck, 10.0f);
    CHECK(c.shot_ready(TcEvent::kTrickShot, 11.0f));
    c.add(TcEvent::kTrickShot, 11.0f);
    CHECK(!c.shot_ready(TcEvent::kTrickShot, 12.0f));   // the same shot within 2.5 s: none
    CHECK(c.shot_ready(TcEvent::kSkillShot, 12.0f));    // another shot: yes
    CHECK(c.shot_ready(TcEvent::kTrickShot, 13.6f));
    c.add(TcEvent::kSkillShot, 13.5f);
    CHECK(c.active() && c.events == 3 && c.points == 200);
    CHECK(!c.over(18.5f));                 // 5 s after the last: still on
    CHECK(c.over(18.6f));                  // then over
    CHECK(c.over(5.0f));                   // the clock went back (a restart)
    c.extreme_at = 14.0f;                  // EXTREME CHECKING: over 1 s later
    CHECK(!c.over(14.9f) && c.over(15.1f));
    c.add(TcEvent::kCheck, 2.0f);          // an event after a restart starts afresh
    CHECK(c.events == 1 && c.points == 50 && c.extreme_at < 0.0f);
}

TEST(level_popups) {
    b3native::TcSign s{};
    CHECK(!b3native::traffic_level_sign(-1, 0, s));   // the first star: no popup
    CHECK(b3native::traffic_level_sign(0, 1, s) && s == b3native::TcSign::kGood);
    CHECK(b3native::traffic_level_sign(1, 2, s) && s == b3native::TcSign::kGreat);
    CHECK(b3native::traffic_level_sign(2, 3, s) && s == b3native::TcSign::kExtremeChecking);
    CHECK(b3native::traffic_level_sign(0, 2, s) && s == b3native::TcSign::kGreat);   // (two at once: the higher)
    CHECK(!b3native::traffic_level_sign(2, 2, s));
}

TEST(star_levels_as_near_miss) {
    const float t[4] = {1.0f, 3.0f, 6.0f, 10.0f};
    CHECK(b3native::traffic_star_level(0.0f, t, 4) == -1);
    CHECK(b3native::traffic_star_level(1.0f, t, 4) == 0);
    CHECK(b3native::traffic_star_level(5.0f, t, 4) == 1);
    CHECK(b3native::traffic_star_level(6.0f, t, 4) == 2);
    CHECK(b3native::traffic_star_level(50.0f, t, 4) == 3);
    CHECK(b3native::traffic_star_level(3.0f, t, 0) == -1);   // (no levels known)
}

TEST(replay_entry) {
    CHECK(b3native::traffic_check_entry("1") == b3native::kTcRevenge && b3native::traffic_check_entry("2") == b3native::kTcExtreme);
    CHECK(b3native::traffic_check_entry("0") == 0 && b3native::traffic_check_entry("") == 0 && b3native::traffic_check_entry(nullptr) == 0);
    CHECK(b3native::traffic_check_entry("11") == 0 && b3native::traffic_check_entry("3") == 0 && b3native::traffic_check_entry("yes") == 0);
}

TEST(records_kept_apart) {
    b3replay::records::Run r;
    CHECK(b3replay::records::folder("/g", r) == "/g");
    // the records word's bits 26-27 (traffic_check_record_word: with the flag when nothing else set the word)
    r.race_options = b3native::traffic_check_record_word(0, b3native::kTcExtreme);
    CHECK(b3replay::records::folder("/g", r) == (std::filesystem::path("/g") / "options-trafficcheck-extreme").string());
    r.race_options = b3native::traffic_check_record_word(0, b3native::kTcRevenge);
    CHECK(r.race_options == (0x8002ull | 1ull << 26));
    CHECK(b3replay::records::folder("/g", r) == (std::filesystem::path("/g") / "options-trafficcheck").string());
    CHECK(b3native::traffic_check_record_word(0x8003u, b3native::kTcOff) == 0x8003u && b3native::traffic_check_record_word(0, 0) == 0);
    r.race_options = b3native::traffic_check_record_word(0x8003u, b3native::kTcRevenge);   // raceopts' options too (TRAFFIC Heavy): one folder
    CHECK(b3replay::records::folder("/g", r) == (std::filesystem::path("/g") / "options-traffic-heavy-trafficcheck").string());
    r.gameplay_set = "0123456789abcdef";
    CHECK(b3replay::records::folder("/g", r) ==
          (std::filesystem::path("/g") / "modded-0123456789abcdef" / "options-traffic-heavy-trafficcheck").string());
    CHECK(b3replay::records::folder("", r).empty());
    r.playback = true;
    CHECK(!b3replay::records::writes("/g", r));
}

TEST(words_in_the_games_languages) {
    CHECK(std::string(b3native::traffic_check_words("").caption) == "TRAFFIC CHECK");
    CHECK(std::string(b3native::traffic_check_words("XX").caption) == "TRAFFIC CHECK");   // (an unknown code: English)
    auto units = [](const std::string& u8) { std::size_t n = 0; for (unsigned char ch : u8) n += (ch & 0xC0) != 0x80; return n; };
    for (const std::string code : {"", "FR", "GE", "IT", "SP", "DU", "SW"}) {
        const b3native::TcWords& w = b3native::traffic_check_words(code);
        CHECK(std::string(w.code) == code);
        CHECK(units(w.caption) <= 23);   // (the caption's slot: 0x30 bytes)
        for (const char* t : w.sign) CHECK(t && units(t) > 2 && units(t) <= 25);   // (a sign's slot: 0x34 bytes)
    }
}

}  // namespace

TEST_MAIN("TRAFFIC CHECKING (trafficcheck)")
