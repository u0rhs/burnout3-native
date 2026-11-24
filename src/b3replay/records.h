// records.h -- media1: where a run's Time Attack ghosts (ghost.h) and speedrun best times (speedrun.h) are read and
// written. Two kinds of run do not make the player's own records:
//   - a replay playing (native_play --replay passes --replay-playback): its laps and finishes are the recording's (a
//     friend's, perhaps), so nothing is written; the player's ghost is still read and shown;
//   - a run whose mod set changes gameplay (b3mod::changes_gameplay_any: a tuning preset, a code mod marked
//     gameplay = yes, a script mod that may write the game): its ghosts and times belong to that set, kept in a folder
//     of its own, "<ghosts or speedrun folder>/modded-<hash>" (docs/PLAYING.md §8; the hash: b3mod::gameplay_set_hash,
//     the gameplay-changing mods alone, so a texture mod added or moved keeps the folder). The plain game reads only
//     its folder's own files, so it never sees them; another gameplay mod or version starts afresh;
//   - raceopts: a SINGLE EVENT race with the SELECT OPTIONS page's TRAFFIC / LAPS / RIVALS not the game's
//     (tools/native/race_options.h; the record word in the 'HTD1' host record, hud.h HostRaceTime::options): its ghosts and
//     times go to a folder of those options, "<folder>/options-traffic-off-laps-5" (inside a mod set's folder when both);
//   - elim1: a SINGLE EVENT ELIMINATOR (tools/native/single_eliminator.h; the game sees a race, but its laps are the cars
//     less one): bit 14 of the same word, "<folder>/options-eliminator" (TRAFFIC / RIVALS after it when not the game's);
//   - heavyseries: a SINGLE EVENT race driven with a heavyweight (tools/native/heavy_race.h): bit 13 of the same word,
//     "<folder>/options-heavyweights" (one folder with the others, e.g. "options-heavyweights-traffic-off");
//   - gameopts: a race with PC SETTINGS > GAME > GAMEPLAY's MIRROR MODE (bit 11) or CRASHBREAKER IN EVERY MODE outside Crash
//     mode (bit 12) (tools/native/game_option_rules.h game_options_record_word): "<folder>/options-mirror",
//     "options-crashbreaker", one folder with the others (e.g. "options-heavyweights-mirror-traffic-off");
//   - rrparts1: a SINGLE EVENT Road Rage with ROAD RAGE PARTS (tools/native/road_rage_parts.h): bit 25, "-loose-parts".
//   - party1: a race with any of GAME > PARTY MODES (tools/native/party_modes.h): bit 24, "options-party".
//   - trafficcheck: a race with TRAFFIC CHECK (SELECT OPTIONS' row in Single Event, PC SETTINGS > GAME > GAMEPLAY's TRAFFIC
//     CHECK (2 PLAYERS) in two-player races; tools/native/traffic_check_rules.h traffic_check_record_word): bits 26-27 of
//     the same word, "-trafficcheck" (Revenge) or "-trafficcheck-extreme" (Extreme) after the other options in one folder
//     ("options-trafficcheck", "options-traffic-heavy-trafficcheck-extreme").
//   - trafficseries: a SINGLE EVENT race with a traffic vehicle as the player's car (tools/native/traffic_cars.h): bit 28,
//     "<folder>/options-trafficcars" (one folder with the others).
//   - customseries: a SINGLE EVENT race with an extra car (a player's car mod, the CUSTOM series) as the player's car
//     (tools/native/extra_cars.h): bit 29, "<folder>/options-extracars" (one folder with the others).
#ifndef B3REPLAY_RECORDS_H
#define B3REPLAY_RECORDS_H

#include <cstdint>
#include <filesystem>
#include <string>

namespace b3replay::records {

struct Run {
    bool playback = false;      // a replay playing
    std::string gameplay_set;   // b3mod::gameplay_set_hash (16 hex); "": no mod changes gameplay
    std::uint64_t race_options = 0;   // raceopts: the race's record word (0: the game's own race; 64 bits since options3)
};

// raceopts: the folder of a race's options, from the race's records word (64 bits; bit 15 set: options; every owner of
// every bit: tests/test_bit_owners.cpp). raceopts (tools/native/race_choice.h RaceChoice::record_word): bits 0-1 TRAFFIC
// 0 Off / 1 Light / 2 Normal / 3 Heavy / 4 Busy / 5 Extreme (its third bit in bit 16), 2-5 LAPS (0 the track's), 6-8 ROAD
// RAGE's RIVALS (0 the game's); elim1: bit 9 TIMED ELIMINATION, bit 14 ELIMINATOR; gearbox: bit 10 MANUAL; gameopts: bit
// 11 MIRROR MODE, bit 12 CRASHBREAKER IN EVERY MODE; heavyseries: bit 13 HEAVYWEIGHTS; options3: 17 START standing, 18
// RIVAL STYLE Road Rage, 19 BURNOUT POINTS all, 20-22 ROAD RAGE's TIME LIMIT (1..4: 1, 2, 3, 5 minutes), 23 TRAFFIC
// PATTERN varied, 32-34 DEFORMATION (1 none, 2 50 %, 3 150 %, 4 200 %: tools/native/game_mods.h), 35-37 RIVALS' CARS
// (1..6 a class, 7 the player's car), 38 RIVALS' CARS Mixed (mixedrivals); party1: 24 PARTY MODES; rrparts1: 25 ROAD RAGE PARTS (loose parts); trafficcheck:
// 26-27 TRAFFIC CHECK (1 Revenge, 2 Extreme: traffic_check_rules.h); trafficseries: 28 TRAFFIC CARS; "" for 0
inline std::string options_folder(std::uint64_t w) {
    if (!(w & 0x8000u)) return "";
    const unsigned t = unsigned(w & 3u) | unsigned(w >> 16 & 1u) << 2, l = unsigned(w >> 2 & 15u), r = unsigned(w >> 6 & 7u),
                   m = unsigned(w >> 20 & 7u);
    static const char* const kTraffic[6] = {"off", "light", "normal", "heavy", "busy", "extreme"};
    std::string s = "options";
    if (w & 0x4000u) s += "-eliminator";   // elim1
    if (w & 0x0200u) s += "-timed";        // elim1: TIMED ELIMINATION (raceopts' word bit 9)
    if (w & 0x2000u) s += "-heavyweights";   // heavyseries
    if (w & 0x0800u) s += "-mirror";          // gameopts
    if (w & 0x1000u) s += "-crashbreaker";
    if (w & 0x01000000ull) s += "-party";      // party1: any PARTY MODE (tools/native/party_modes.h; records word bit 24)
    if (w & 0x0400u) s += "-manual";       // gearbox: TRANSMISSION Manual (raceopts' word bit 10)
    if (w & 0x10000000u) s += "-trafficcars";   // trafficseries
    if (w & 0x20000000u) s += "-extracars";     // customseries
    if (t != 2 && t < 6) s += std::string("-traffic-") + kTraffic[t];
    if (l) s += "-laps-" + std::to_string(l);
    if (r) s += "-rivals-" + std::to_string(r);
    if (w & 0x20000u) s += "-standing-start";   // options3
    if (w & 0x40000u) s += "-roadrage-ai";
    if (w & 0x80000u) s += "-all-points";
    if (m >= 1 && m <= 4) { static const int kMin[5] = {0, 1, 2, 3, 5}; s += "-time-limit-" + std::to_string(kMin[m]); }
    if (w & 0x800000u) s += "-varied-traffic";
    if (w & 0x2000000u) s += "-loose-parts";   // rrparts1: ROAD RAGE PARTS (bit 25)
    if (const unsigned c = unsigned(w >> 35 & 7u); c >= 1 && c <= 7) {   // options3: RIVALS' CARS
        static const char* const kCars[8] = {"", "compact", "muscle", "coupe", "sports", "super", "heavyweights", "same"};
        s += std::string("-rival-cars-") + kCars[c];
    }
    if (w & 0x4000000000ull) s += "-rival-cars-mixed";   // mixedrivals: RIVALS' CARS Mixed (bit 38)
    if (const unsigned d = unsigned(w >> 32 & 7u); d >= 1 && d <= 4) {   // options3: DEFORMATION
        static const char* const kDeform[5] = {"", "none", "50", "150", "200"};
        s += std::string("-deformation-") + kDeform[d];
    }
    if (const unsigned k = unsigned(w >> 26 & 3u); k == 1 || k == 2)   // trafficcheck: TRAFFIC CHECK Revenge / Extreme
        s += k == 1 ? "-trafficcheck" : "-trafficcheck-extreme";
    return s;
}

// The folder the run reads from (and writes to, when it writes): `base` itself, or its set's folder. "" stays "".
inline std::string folder(const std::string& base, const Run& r) {
    if (base.empty()) return base;
    std::filesystem::path p(base);
    if (!r.gameplay_set.empty()) p /= "modded-" + r.gameplay_set;
    if (const std::string o = options_folder(r.race_options); !o.empty()) p /= o;
    return p.string();
}
// Whether the run writes its records at all (a folder given, and not a replay playing)
inline bool writes(const std::string& base, const Run& r) { return !base.empty() && !r.playback; }

}  // namespace b3replay::records

#endif
