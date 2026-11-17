// save_stats.h -- the stats the game keeps in its own save, read back for the launcher's STATS page and
// `save_tool stats` (remaster-features §5 "Local achievements and stats"), and a few local achievements worked out from
// them. Read only: nothing here writes a save. Standard library only (tests: tests/test_save_stats.cpp).
//
// The game's save file (BASLUS-21050/BASLUS-21050, 61,772 bytes, little endian) [C decomp func_00421700, the load-time
// check, and Construct__20CB3MemCardControllerFv (func_003FDF90): the 61772-byte buffer]:
//   +0x0000 u32 version, 0x10 (the check returns "corrupt" for any other)
//   ...     the record tables and options (not decoded here)
//   +0xE374 CB3Profile[3], 0x49C bytes each (the three profiles of one save; the load check walks i * 0x49C from here)
//   +0xF148 u32 checksum: the 32-bit sum of the 0x3C52 words before it
// One profile (offsets in the profile; the decomp's placeholder field names; the game's own STATS page,
// func_003F1490, shows each value through the getter named) [C unless marked]:
//   +0x000 f32   save stamp (GetTimeStamp / SetTimeStamp, func_0013A560 / func_0013A570)
//   +0x008 u16[9] the name, UTF-16, NUL-terminated (func_0013B730 / func_0013B740: CopyN 9)
//   +0x040 u32   Burnout points (STATS row 1: func_0013B660; also event unlock thresholds, func_00138530)
//   +0x044 u32   crash damage total, clamped to 1,999,999,999 (row 2: func_0013B570 / func_0013B580)
//   +0x048 u32[67] race events per car (IncrementVehicleUsage__10CB3ProfileFUl, called beside the race count in
//                 func_0042F0F0; EvaluateFavourites: the favourite car)
//   +0x154 u32[7] takedowns per car series, offline + online (func_0013B260)
//   +0x170 u32[36] takedowns per track (func_0013B260)
//   +0x200 u32   takedowns online (row 4: func_0013B200(p, 0, 1)); offline = sum(+0x154) - this (row 3, EvaluateGameCounts)
//   +0x204 u32   slams offline, +0x208 slams online (rows 5, 6: func_0013B1B0, IncreaseSlamTotal__10CB3ProfileFUib)
//   +0x20C f32[36] longest oncoming per track, metres (row 20: func_0013B090; the page shows M, or FT x a factor)
//   +0x29C i32   car of the longest oncoming (row 22)
//   +0x2A0 f32[36] longest drift per track, metres (row 23: func_0013AEF0)
//   +0x330 i32   car of the longest drift (row 25)
//   +0x334 u32   race events online,  +0x33C race events offline  (rows 8, 7: func_0013AE20)
//   +0x338 u32   crash events online, +0x340 crash events offline (rows 10, 9: func_0013ADD0)
//   +0x344 u32[15] best single-event values, the takedown targets' scores (func_00137C40; thresholds are the game's)
//   +0x386 i8[73] race event medals, +0x3CF i8[100] crash event medals: 3 gold, 2 silver, 1 bronze, 0 or -1 none
//                 (func_001372B0 counts m > 0 as a medal, m == 3 as gold; a new profile has -1 throughout; the
//                 game's STATS page counts 173 events, row 12)
//   +0x433 u8[20] signature takedowns done (row 27, n/20: func_0013A7F0, EvaluateGameCounts)
//   +0x447 u8[10] crash headlines done (row 28, n/10: func_0013A7D0, func_0013B580)
//   +0x451 u8[67] cars unlocked outright (most cars unlock from progress instead, worked out by the game at load:
//                 WillPlayerCarUnlock__10CB3ProfileFi, from its car table -- so not a car count)
//   +0x49B u8    1: this profile slot is empty (func_00421700; func_0013A570 clears it when the profile is stamped)
// Not decoded (they need the game's tables): game complete %, events unlocked, regional medals, special-event golds,
// cars collected, takedown targets met, favourite car / track names.
#ifndef B3NATIVE_SAVE_STATS_H
#define B3NATIVE_SAVE_STATS_H

#include "save_formats.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace b3native::save {

inline constexpr std::size_t kGameSaveBytes = 61772;
inline constexpr std::uint32_t kGameSaveVersion = 0x10;
inline constexpr std::size_t kProfilesOffset = 0xE374, kProfileBytes = 0x49C, kChecksumOffset = 0xF148;
inline constexpr int kProfilesPerSave = 3;
inline constexpr int kRaceEvents = 73, kCrashEvents = 100, kEvents = kRaceEvents + kCrashEvents;
inline constexpr int kSignatureTakedowns = 20, kHeadlines = 10, kCars = 67, kTracks = 36;

struct MedalCounts {
    int gold = 0, silver = 0, bronze = 0;
    int medals() const { return gold + silver + bronze; }
};

struct ProfileStats {
    int index = 0;                 // 0..2 in the save
    std::string name;              // UTF-8
    std::uint32_t burnout_points = 0, crash_damage = 0;
    std::uint32_t takedowns_offline = 0, takedowns_online = 0;
    std::uint32_t slams_offline = 0, slams_online = 0;
    std::uint32_t race_events_offline = 0, race_events_online = 0;
    std::uint32_t crash_events_offline = 0, crash_events_online = 0;
    float longest_oncoming_m = 0, longest_drift_m = 0;
    MedalCounts race, crash;       // over the 73 race and 100 crash events
    int signature_takedowns = 0, headlines = 0;
    int cars_driven = 0;           // cars with at least one race event
    std::uint64_t car_events = 0;  // race events summed over the cars

    std::uint64_t takedowns() const { return std::uint64_t{takedowns_offline} + takedowns_online; }
    std::uint64_t events_played() const {
        return std::uint64_t{race_events_offline} + race_events_online + crash_events_offline + crash_events_online;
    }
    MedalCounts all() const { return {race.gold + crash.gold, race.silver + crash.silver, race.bronze + crash.bronze}; }
};

// The profiles in use in the game's save file bytes (empty slots left out). nullopt (and *error) for a file that is not
// the game's save: wrong size, version or checksum (the game itself would not load it).
std::optional<std::vector<ProfileStats>> decode_game_save(const std::uint8_t* bytes, std::size_t size, std::string* error = nullptr);
// The same from card directories (the game's save among them: save_formats.h game_save).
std::optional<std::vector<ProfileStats>> decode_game_save(const Directories& dirs, std::string* error = nullptr);
// From any file or folder save_formats.h read_any takes (.sav, .psu, .max, a card image, a folder card).
std::optional<std::vector<ProfileStats>> read_profile_stats(const std::filesystem::path& p, std::string* error = nullptr);

// ---- local achievements: worked out from the stats alone ----
struct Achievement {
    const char* id;
    const char* title;
    const char* description;
    std::uint64_t progress = 0, target = 1;
    bool unlocked() const { return progress >= target; }
};
std::vector<Achievement> achievements(const ProfileStats& s);

// "1,234,567"
std::string group_digits(std::uint64_t v);
// A text report of a save's profiles and their achievements (save_tool stats).
std::string format_stats(const std::vector<ProfileStats>& profiles);

}  // namespace b3native::save

#endif
