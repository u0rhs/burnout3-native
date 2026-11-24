// heavyseries (tools/native/heavy_series.h): the HEAVYWEIGHTS series on SINGLE EVENT's SELECT CAR -- which events get it
// (RACE / TIME ATTACK / ROAD RAGE only, the series on and a heavyweight unlocked; the game's own answer kept), which race
// is a heavyweight race (the Single Event stage record, the mode, player 0's car), the replay's entry, and where such a
// race's ghosts and best times go (src/b3replay/records.h: apart from the player's own).
#include "test.h"

#include "heavy_series.h"
#include "../src/b3replay/records.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace {

using namespace b3native;

// the retail cars' GtIDs (the vehicle list's): the 11 heavyweights and one of every other class
constexpr std::uint64_t kHeavies[] = {0x7704283866EB4000ull, 0x7704283866EE2E00ull, 0x7704283867125000ull, 0x7704283867396000ull,
                                      0x7704283867422A00ull, 0x7704283867607000ull, 0x7704283867878000ull, 0x7704283867AE9000ull,
                                      0x7704283867D5A000ull, 0x7704283867FCB000ull, 0x770428386823C000ull};
constexpr std::uint64_t kOthers[] = {0x5B55839B95EB4000ull /* Compact */, 0x790798FBA1182C00ull /* Special */,
                                     0x790798FBA1192600ull /* Special */, 0ull};

TEST(heavyweights_by_their_id) {
    for (std::uint64_t id : kHeavies) CHECK(heavy_car_id(id));
    for (std::uint64_t id : kOthers) CHECK(!heavy_car_id(id));
}

TEST(offered_for_single_event_races_only) {
    for (int mode = 0; mode <= 2; ++mode) CHECK(heavy_series_allowed(false, true, kSingleEvent, mode, 1));   // RACE / TA / RR
    CHECK(!heavy_series_allowed(false, true, kSingleEvent, 3, 1));   // CRASH: the game's answer (true there) is kept
    CHECK(heavy_series_allowed(true, true, kSingleEvent, 3, 0));
    CHECK(heavy_series_allowed(true, false, kSingleEvent, 3, 0));    // (off: still the game's)
    CHECK(!heavy_series_allowed(false, true, 0, 0, 5));              // World Tour
    CHECK(!heavy_series_allowed(false, true, 2, 0, 5));              // Multiplayer
    CHECK(!heavy_series_allowed(false, true, 3, 0, 5));              // online
    CHECK(!heavy_series_allowed(false, true, kSingleEvent, -1, 5));
    CHECK(!heavy_series_allowed(false, true, kSingleEvent, 4, 5));
}

TEST(only_with_a_heavyweight_unlocked_and_the_series_on) {
    CHECK(!heavy_series_allowed(false, true, kSingleEvent, 0, 0));
    CHECK(!heavy_series_allowed(false, false, kSingleEvent, 0, 11));
    CHECK(heavy_series_allowed(false, true, kSingleEvent, 0, 11));
}

HeavyStage stage(std::int32_t mode, std::uint64_t car) {
    HeavyStage s;
    s.current_mode = kHeavyOnePlayerStage;
    s.tag = kHeavySingleEventTag;
    s.game_type = kSingleEvent;
    s.mode = mode;
    s.car = car;
    return s;
}

TEST(a_heavyweight_race) {
    for (int mode = 0; mode <= 2; ++mode) CHECK(heavy_race(stage(mode, kHeavies[10])));
    CHECK(!heavy_race(stage(0, kOthers[0])));         // a normal car: the player's own race
    CHECK(!heavy_race(stage(3, kHeavies[0])));        // CRASH with a heavyweight: the game's own event
    HeavyStage s = stage(0, kHeavies[0]);
    s.tag = 0x0051B928u;                              // World Tour's stage record
    CHECK(!heavy_race(s));
    s = stage(0, kHeavies[0]);
    s.current_mode = 0;                               // the front end (no stage)
    CHECK(!heavy_race(s));
    s = stage(0, kHeavies[0]);
    s.game_type = 2;                                  // Multiplayer
    CHECK(!heavy_race(s));
}

TEST(heavyweights_counted_as_the_carousel_lists_them) {
    // the vehicle list: a compact, two driveable heavyweights, a heavy traffic vehicle (not driveable), one more driveable
    // heavyweight missing from the race table (IsCarUnlocked: unlocked)
    const std::uint64_t list[] = {kOthers[0], kHeavies[0], kHeavies[10], 0x77042838671E0800ull, 0x7704283860000000ull};
    const bool drive[] = {true, true, true, false, true};
    std::uint64_t race[kRaceVehicles] = {};
    bool unlocked[kRaceVehicles] = {};
    race[3] = kOthers[0];
    race[40] = kHeavies[0];
    race[50] = kHeavies[10];
    unlocked[3] = true;
    auto count = [&] {
        return heavy_count(5, [&](int i) { return list[i]; }, [&](int i) { return drive[i]; }, [&](int k) { return race[k]; },
                           [&](int k) { return unlocked[k]; });
    };
    HeavyCount c = count();
    CHECK(c.cars == 3 && c.unlocked == 1);   // only the one outside the table
    unlocked[50] = true;
    c = count();
    CHECK(c.cars == 3 && c.unlocked == 2);
    race[60] = 0x7704283860000000ull;        // now in the table, locked
    c = count();
    CHECK(c.cars == 3 && c.unlocked == 1);
    CHECK(heavy_count(0, [&](int i) { return list[i]; }, [&](int i) { return drive[i]; }, [&](int k) { return race[k]; },
                      [&](int k) { return unlocked[k]; }).cars == 0);
}

TEST(record_marks) {
    CHECK(kRecordsBytes == 0xE330u);                       // [C] the tables' size (0x4EC0 + 100 x 0x17C)
    CHECK(record_mark_byte(0x14) && record_mark_byte(0x1C + 0x14) && !record_mark_byte(0x13) && !record_mark_byte(0x15));
    CHECK(record_mark_byte(kRecordTrackBytes - 0x1C + 0x14));
    CHECK(record_mark_byte(kRecordTrackBytes + 0x14) && record_mark_byte(kRecordTrackBytes + 4 * 0x1C + 0x14));
    CHECK(record_mark_byte(kRecordTrackBytes + 0x8C + 0x14) && record_mark_byte(kRecordTrackBytes + 0x8C + 4 * 0x30 + 0x14));
    CHECK(!record_mark_byte(kRecordTrackBytes + 0x8C + 0x1C + 0x14));   // (inside a 0x30 entry, not a mark)
    CHECK(record_mark_byte(kRecordTrackBytes + 99 * kRecordCrashBytes + 0x14));
    unsigned n = 0;
    for (unsigned o = 0; o < kRecordsBytes; ++o) n += record_mark_byte(o);
    CHECK(n == 36u * 4u * 5u + 100u * 10u);                // one per entry (func_003D0C80's loops)
}

TEST(replay_entry) {
    CHECK(heavy_series_entry("1"));
    CHECK(!heavy_series_entry("0"));
    CHECK(!heavy_series_entry(""));
    CHECK(!heavy_series_entry(nullptr));
    CHECK(!heavy_series_entry("10"));
}

TEST(records_kept_apart) {
    using b3replay::records::Run;
    using b3replay::records::folder;
    using b3replay::records::options_folder;
    CHECK(options_folder(heavy_record_word()) == "options-heavyweights");
    CHECK(options_folder(heavy_record_word(0x8000u)) == "options-heavyweights-traffic-off");   // with TRAFFIC Off: one folder
    CHECK(options_folder(heavy_record_word(0xC002u)) == "options-eliminator-heavyweights");    // an Eliminator with one
    CHECK(options_folder(heavy_record_word(0x8002u | 2u << 6)) == "options-heavyweights-rivals-2");
    Run plain, heavy;
    heavy.race_options = heavy_record_word();
    CHECK(folder("/g", plain) == "/g");
    CHECK(folder("/g", heavy) == (std::filesystem::path("/g") / "options-heavyweights").string());
    Run mod = heavy;
    mod.gameplay_set = "0123456789abcdef";
    CHECK(folder("/g", mod) == (std::filesystem::path("/g") / "modded-0123456789abcdef" / "options-heavyweights").string());
    CHECK(folder("", heavy).empty());                 // no folder: none
    heavy.playback = true;
    CHECK(!b3replay::records::writes("/g", heavy));   // a replay of one: nothing written
}

}  // namespace

TEST_MAIN("heavy_series")
