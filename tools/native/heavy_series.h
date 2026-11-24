// heavy_series.h -- heavyseries (2026-11-21): the HEAVYWEIGHTS car series on SINGLE EVENT's SELECT CAR for RACE, TIME
// ATTACK and ROAD RAGE (and ELIMINATOR, which the game runs as RACE), not only CRASH. The rules as values (pure:
// tests/test_heavy_series.cpp); the hooks are heavy_race.h.
//
// What the game does [C the decomp; addresses retail]:
// - SELECT CAR's series are the carousel's car classes (CB3CarouselComponent 0x01E8EC30, mode +0x25C 9 = every class,
//   the class on show +0x258): EB3MenuCarClasses 0 Compact, 1 Muscle, 2 Coupe, 3 Sport, 4 Super, 5 Special,
//   6 Heavyweight (CB3VehicleList::GetClassFromID 0x251CB0: the car's GtID name's first four letters; GetClassString
//   0x42DA70: string 0xBF4 for 6, the series' name in every language's text table). Left / Right (0x42BD70 /
//   0x42BEA0) step the class between 0 and a bound that two carousel bytes set: +0x265 0 (online's restricted list)
//   -> 0..4; +0x264 0 -> 0..5; both 1 -> 0..6, the heavyweights last. Each class's cars are built by 0x42B7C0 with the
//   profile's IsCarUnlocked 0x13A660 per car (a locked car shows locked and cannot be taken, as in every series).
// - The two bytes are the car select state's (0x310200, action 3) arguments to the page's Prepare 0x318F50 ->
//   0x42CFA0: +0x264 = func_0030CEC0 (CB3MenuChoices 0x01E900B0: +4 the top menu 0 World Tour / 1 Single Event / 2
//   Multiplayer ..., +8 the mode 0 race, 1 time attack, 2 road rage, 3 crash; true for the crash modes only), +0x265 =
//   func_0030CE80. 0x30CEC0 has no other caller. 0x42CFA0 also puts a remembered heavyweight back to class 0 when the
//   bound excludes it, so a later event never starts on a heavyweight it does not allow.
// - The race takes the car as any other: its own handling, deformation and chase camera (per-car data; HEVYCARn.cfg),
//   its boost bar (GetVehicleBoostBarSize). On Confirm (0x310200 action 5, Single Event) the rivals are picked by
//   0x30B9D0 -> 0x30B6E0 from the player's class: random cars of class 6 from the race vehicle list (67 cars, the 11
//   heavyweights among them), unlocked ones first, each at most 3 times -- so heavyweights race heavyweights.
// - Records: CB3Records::Update 0x3D1090 (from the post-race setup 0x42F0F0) writes the track's tables, and 0x136F30
//   says whether the race counts for the profile (totals, unlocks, signature takedowns). A heavyweight race gets no
//   record kinds and does not count (heavy_race.h), like a race with SELECT OPTIONS' rows changed; the renderer's ghosts
//   and best times go to an "options-heavyweights" folder (src/b3replay/records.h; heavy_record_word below).
// Only while the series is on (B3NATIVE_HEAVY_SERIES=0: retail's list; a replay: its recording's entry) and at least
// one heavyweight is unlocked in the profile. World Tour, CRASH (already the game's), MULTIPLAYER and online: the game's.
#ifndef B3NATIVE_HEAVY_SERIES_H
#define B3NATIVE_HEAVY_SERIES_H

#include <cstdint>

namespace b3native {

constexpr int kHeavyClass = 6;                           // [C] EB3MenuCarClasses' heavyweights
constexpr std::uint32_t kHeavyIdHigh = 0x77042838u;      // [C] the high word of every "HEVYCAR.." GtID (the 11 cars)
constexpr int kSingleEvent = 1;
constexpr int kRaceVehicles = 67;                        // [C] D_004CCF80's entries (func_003EB060)                          // [C] CB3MenuChoices +4
// The car is a heavyweight: its GtID's high word (the name's first letters, "HEVYCA"). The hooks check it against the
// game's GetClassFromID where they can (heavy_race.h).
inline bool heavy_car_id(std::uint64_t id) { return std::uint32_t(id >> 32) == kHeavyIdHigh; }
// SINGLE EVENT's modes the series is added to: RACE 0 (ELIMINATOR runs as it), TIME ATTACK 1, ROAD RAGE 2 (CRASH 3 has it)
inline bool heavy_series_mode(int game_type, int mode) { return game_type == kSingleEvent && mode >= 0 && mode <= 2; }
// What SELECT CAR's "heavyweights allowed" answer (func_0030CEC0) becomes: the game's, or true for those modes with the
// series on and a heavyweight unlocked
inline bool heavy_series_allowed(bool game, bool on, int game_type, int mode, int unlocked) {
    return game || (on && unlocked > 0 && heavy_series_mode(game_type, mode));
}
// The race on now is a heavyweight race of those modes: the game's current mode is the one-player stage record built for
// SINGLE EVENT (race_choice.h's, [C RequestOnePlayerGameMode 0x30C2C0]: 0x0051AD00 with +0x1B8 0x0051B920; World
// Tour's carries 0x0051B928), the menu's choice one of the modes, and player 0's car (CB3MenuChoices +0x80) a heavyweight
struct HeavyStage {
    std::uint32_t current_mode = 0;   // *(0x0051BA88)
    std::uint32_t tag = 0;            // the record's +0x1B8
    std::int32_t game_type = 0;       // CB3MenuChoices +4
    std::int32_t mode = -1;           // CB3MenuChoices +8
    std::uint64_t car = 0;            // CB3MenuChoices +0x80
};
constexpr std::uint32_t kHeavyOnePlayerStage = 0x0051AD00u, kHeavySingleEventTag = 0x0051B920u;
inline bool heavy_race(const HeavyStage& s) {
    return s.current_mode == kHeavyOnePlayerStage && s.tag == kHeavySingleEventTag && heavy_series_mode(s.game_type, s.mode) &&
           heavy_car_id(s.car);
}
// The profile's heavyweights as SELECT CAR lists them, from guest RAM only (no guest call: a call costs EE time and the
// golden record pins the timing). [C] CB3VehicleList D_005186A0 {+4 count, +8 int driveable[128], +0x408 u64 id[]}:
// the carousel 0x42B7C0 lists class 6's driveable entries; CB3Profile::IsCarUnlocked 0x13A660 = the id in the list,
// then its index in the race vehicle table D_004CCF80 (67 u64, func_003EB060) -> D_0051BFA0[index] (byte), or 1 when
// the id is not in that table. `load_*(i)` read the guest's entries.
struct HeavyCount { int cars = 0, unlocked = 0; };
template <class Id, class Driveable, class RaceId, class Unlocked>
HeavyCount heavy_count(int list_count, Id list_id, Driveable driveable, RaceId race_id, Unlocked unlocked_byte) {
    HeavyCount c;
    for (int i = 0; i < list_count && i < 128; ++i) {
        const std::uint64_t id = list_id(i);
        if (!heavy_car_id(id) || !driveable(i)) continue;
        ++c.cars;
        int k = 0;
        while (k < kRaceVehicles && race_id(k) != id) ++k;
        if (k == kRaceVehicles || unlocked_byte(k)) ++c.unlocked;
    }
    return c;
}
// CB3Records 0x01EA7810's tables [C func_003D0C80, func_003D1090]: 36 tracks x 4 tables x 5 entries of 0x1C bytes, then
// 100 crash tables of 5 x 0x1C + 5 x 0x30 bytes. Each entry's byte +0x14 is its "new record" mark, which every update
// clears first (0x3D0C80) and a new record sets; the rest is the record itself (times, names, cars).
constexpr unsigned kRecordTrackBytes = 36u * 4u * 5u * 0x1Cu, kRecordCrashBytes = 0x17Cu;
constexpr unsigned kRecordsBytes = kRecordTrackBytes + 100u * kRecordCrashBytes;
inline bool record_mark_byte(unsigned offset) {
    if (offset < kRecordTrackBytes) return offset % 0x1Cu == 0x14u;
    const unsigned q = (offset - kRecordTrackBytes) % kRecordCrashBytes;
    return q < 0x8Cu ? q % 0x1Cu == 0x14u : (q - 0x8Cu) % 0x30u == 0x14u;
}
// The renderer's record word (src/b3replay/records.h options_folder; raceopts' race_choice.h record_word, elim1's
// EliminatorRows::record_word): bit 13 for a heavyweight race, with the race's options when it has some ("" folder
// "options-heavyweights", or one folder with them, e.g. "options-heavyweights-traffic-off")
constexpr std::uint16_t kHeavyRecordBit = 0x2000u;
inline std::uint64_t heavy_record_word(std::uint64_t options = 0) {
    return (options ? options : 0x8002u) | kHeavyRecordBit;   // (0x8002: the flag, TRAFFIC Normal)
}
// The replay's entry ("heavy_series" 0 / 1; a recording without it: off, the list it was made with)
inline bool heavy_series_entry(const char* v) { return v && v[0] == '1' && !v[1]; }

}  // namespace b3native

#endif
