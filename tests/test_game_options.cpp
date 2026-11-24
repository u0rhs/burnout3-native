// gameopts (tools/native/game_option_rules.h, game_options.h): PC SETTINGS > GAME > GAMEPLAY's MIRROR MODE and CRASHBREAKER
// IN EVERY MODE -- which races take them (Single Event and the two-player game, never World Tour or Crash party), the latch
// (taken at the race's start, kept through it, gone after), the record word (the Crashbreaker does not set Crash mode
// apart), the replay entry, and the records' folders (src/b3replay/records.h).
#include "test.h"

#include "game_option_rules.h"
#include "../../src/b3replay/records.h"

#include <cstdint>
#include <string>

namespace {

using namespace b3native;
constexpr std::uint32_t kWorldTourTag = 0x0051B928u, kPartyTag = 0x0051B988u, kFrontEnd = 0x0051A000u;

TEST(stages_single_event_and_two_player_only) {
    CHECK(game_options_stage(kGoOnePlayerStage, kGoSingleEventTag));
    CHECK(game_options_stage(kGoTwoPlayerStage, kGoSingleEventTag));
    CHECK(!game_options_stage(kGoOnePlayerStage, kWorldTourTag));   // World Tour counts for the tour: the game's own
    CHECK(!game_options_stage(kGoOnePlayerStage, kPartyTag));       // Crash party
    CHECK(!game_options_stage(kFrontEnd, kGoSingleEventTag));
    CHECK(!game_options_stage(0, 0));
}

TEST(latched_at_the_race_start_kept_through_it) {
    GameOptionLatch l;
    CHECK(!l.step(false, kGameOptAll) && l.latched() == 0);          // the menus: nothing, whatever is asked
    CHECK(l.step(true, kGameOptMirror) && l.latched() == kGameOptMirror);
    CHECK(!l.step(true, kGameOptCrashbreaker) && l.latched() == kGameOptMirror);   // a change mid-race waits
    CHECK(!l.step(true, 0) && l.latched() == kGameOptMirror);        // (a restart: the stage stays current)
    CHECK(l.step(false, kGameOptCrashbreaker) && l.latched() == 0);  // the race over
    CHECK(l.step(true, kGameOptCrashbreaker | 0x2000u) && l.latched() == kGameOptCrashbreaker);   // the next race; bits beyond the word masked
    CHECK(l.step(false, 0) && !l.step(true, 0) && l.latched() == 0); // off: the game's own race
}

TEST(record_bits) {
    CHECK(game_options_record_bits(0, false) == 0 && game_options_record_bits(0, true) == 0);
    CHECK(game_options_record_bits(kGameOptMirror, false) == kGameOptMirror);
    CHECK(game_options_record_bits(kGameOptMirror, true) == kGameOptMirror);   // a mirrored junction is apart too
    CHECK(game_options_record_bits(kGameOptCrashbreaker, false) == kGameOptCrashbreaker);
    CHECK(game_options_record_bits(kGameOptCrashbreaker, true) == 0);          // Crash mode has its own breaker
    CHECK(game_options_record_bits(kGameOptAll, true) == kGameOptMirror);
}

TEST(replay_entry_and_the_word) {
    CHECK(game_options_entry("0") == 0 && game_options_entry("1") == 1 && game_options_entry("2") == 2 && game_options_entry("3") == 3);
    CHECK(game_options_entry("8192") == 0 && game_options_entry("-1") == 0 && game_options_entry("1x") == 0);
    CHECK(game_options_entry("4096") == 0 && game_options_entry("4098") == 4098);   // gamemods1: Full (bit 12) only with the Crashbreaker
    CHECK(game_options_entry("") == 0 && game_options_entry(nullptr) == 0);
    // CRASHBREAKER AFTER in bits 2..3, only with the Crashbreaker (a word with neither option is 0: no replay entry)
    CHECK(game_options_entry("14") == 14 && game_options_cars(14) == 3 && game_options_entry("13") == 1 && game_options_entry("12") == 0);
    CHECK(game_options_word(false, false, 3) == 0 && game_options_word(true, false, 2) == 1);
    CHECK(game_options_word(false, true, 1) == 6 && game_options_word(true, true, 9) == 15 && game_options_word(false, true, -1) == 2);
    CHECK(game_options_name(0) == "none" && game_options_name(1) == "MIRROR MODE");
    CHECK(game_options_name(3) == "MIRROR MODE + CRASHBREAKER IN EVERY MODE (traffic, after 0 vehicles)");
    CHECK(game_options_name(6) == "CRASHBREAKER IN EVERY MODE (traffic, after 1 vehicle)");
    CHECK(game_options_name(0x1006) == "CRASHBREAKER IN EVERY MODE (Full, after 1 vehicle)");   // gamemods1: bit 12 Full
    CHECK(game_options_normal(0x1001) == 1 && game_options_normal(0x1002) == 0x1002 && game_options_word(false, true, 1, true) == 0x1006);
    CHECK(game_options_normal(0x1010) == 0x10);   // (Full dropped without the Crashbreaker; the party bits kept)
    GameOptionLatch l;
    CHECK(l.step(true, 12 | kGameOptMirror) && l.latched() == kGameOptMirror);   // (a count without the breaker dropped)
    l.step(false, 0);
    CHECK(l.step(true, 10) && game_options_cars(l.latched()) == 2);
}

TEST(party_modes_word) {
    // party1: PARTY MODES in the word's bits 4..11, latched and replayed with the rest; any but TUK-TUKS (a traffic vehicle's race: trafficseries' bit 28)
    // keeps the race apart: records word bit 10, "options-party"
    namespace r = b3replay::records;
    const unsigned w = party_word(3, true, 2, true, true, true);
    CHECK(w == (3u << 4 | 1u << 6 | 2u << 7 | 1u << 9 | 1u << 10 | 1u << 11) && (w & ~kPartyMask) == 0);
    CHECK(party_crazy(w) == 3 && party_moon(w) == 2 && party_word(9, false, -1, false, false, false) == 3u << 4);
    CHECK(game_options_normal(w | kGameOptMirror) == (w | kGameOptMirror));
    CHECK(game_options_entry(std::to_string(w).c_str()) == w && game_options_entry("4095") == 4095u);
    CHECK(game_options_record_bits(kPartyTuk, false) == 0);   // (trafficseries' bit 28)
    CHECK(game_options_record_bits(kPartyKamikaze, true) == kGameOptParty);   // Crash mode too
    CHECK(r::options_folder(game_options_record_word(0, kGameOptParty)) == "options-party");
    CHECK(r::options_folder(game_options_record_word(0, kGameOptMirror | kGameOptParty)) == "options-mirror-party");
    CHECK(party_moon_lift(0) == 0.0f && party_moon_lift(1) == 0.25f && party_moon_lift(3) == 1.0f);
    CHECK(game_options_name(party_word(2, false, 0, true, false, false)) == "PARTY MODES (CRAZY AI PHYSICS Local Wildlife, CRASH ON DEMAND)");
    GameOptionLatch l;
    CHECK(l.step(true, w) && l.latched() == w);
}

TEST(records_word_and_folders) {
    // dev's one records word (src/b3replay/records.h options_folder): bit 11 MIRROR MODE, bit 12 the Crashbreaker, one
    // folder with raceopts' / elim1's / heavyseries' options
    namespace r = b3replay::records;
    CHECK(game_options_record_word(0, 0) == 0 && game_options_record_word(0x8012u, 0) == 0x8012u);
    CHECK(game_options_record_word(0, kGameOptMirror) == 0x8802u && r::options_folder(0x8802u) == "options-mirror");
    CHECK(r::options_folder(game_options_record_word(0, kGameOptCrashbreaker)) == "options-crashbreaker");
    CHECK(r::options_folder(game_options_record_word(0, kGameOptAll)) == "options-mirror-crashbreaker");
    // a mirrored heavyweight race with TRAFFIC Off: one combined folder (heavy_series.h's bit 13 on raceopts' word)
    const std::uint16_t heavy_traffic_off = 0x8000u | 0x2000u;   // (TRAFFIC bits 0: Off)
    CHECK(r::options_folder(game_options_record_word(heavy_traffic_off, kGameOptMirror)) == "options-heavyweights-mirror-traffic-off");
    r::Run run;
    run.race_options = game_options_record_word(0, kGameOptMirror);
    CHECK(r::folder("/g", run) == (std::filesystem::path("/g") / "options-mirror").string());
    run.gameplay_set = "0123456789abcdef";   // inside a mod set's folder
    CHECK(r::folder("/g", run) == (std::filesystem::path("/g") / "modded-0123456789abcdef" / "options-mirror").string());
    run.playback = true;
    CHECK(!r::writes("/g", run));
}

}  // namespace

TEST_MAIN("MIRROR MODE and CRASHBREAKER IN EVERY MODE (gameopts)")
