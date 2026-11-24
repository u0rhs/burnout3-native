// Every owner of the shared fixed spaces, in one static list each, so a merge of two branches that both take the same bits
// or bytes fails here (options3, 2026-11-21):
//   - the guest arena's port regions (src/native/hook_table.h GuestArena::kPort; its compile-time check covers one table,
//     this one also the regions' owners by name);
//   - the records word (src/b3replay/records.h options_folder): raceopts / options3's race_choice.h record_word, elim1's
//     eliminator_rows.h, heavyseries' heavy_series.h, gameopts' game_option_rules.h, trafficcheck's traffic_check_rules.h;
//   - the host records' bases (kHostRecords);
//   - the 'HTD1' host record's race word and label word (tools/native/gameview_native.cpp publish_race_time, src/b3replay
//     hud.cpp host_race_time_decode).
// A new owner adds its line here (and its producer's check below).
#include "test.h"

#include "hook_table.h"
#include "race_choice.h"
#include "eliminator_rows.h"
#include "heavy_series.h"
#include "traffic_series.h"
#include "extra_series.h"
#include "game_option_rules.h"
#include "game_mods.h"
#include "road_rage_parts.h"
#include "traffic_check_rules.h"
#include "../src/b3replay/records.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace {

struct BitOwner {
    std::uint32_t mask;
    const char* owner;
};

// The records word (64 bits since options3): every owner, the reserved ones (allocated to branches not merged yet) too.
// A branch that takes a bit turns its "reserved" line real or adds one; the producers' own masks (below) must equal the
// real lines, so a producer that takes a bit the table does not list fails here.
struct WordOwner {
    std::uint64_t mask;
    const char* owner;
    bool reserved;
};
const std::vector<WordOwner> kRecordWord = {
    {0x0000000000000003ull, "raceopts TRAFFIC (low bits)", false},
    {0x000000000000003Cull, "raceopts LAPS", false},
    {0x00000000000001C0ull, "raceopts ROAD RAGE RIVALS; morerivals a Race's 6 / 7 rivals (more_rivals_grid.h)", false},
    {0x0000000000000200ull, "elim1 TIMED ELIMINATION", false},
    {0x0000000000000400ull, "gearbox TRANSMISSION Manual", false},
    {0x0000000000000800ull, "gameopts MIRROR MODE", false},
    {0x0000000000001000ull, "gameopts CRASHBREAKER", false},
    {0x0000000000002000ull, "heavyseries HEAVYWEIGHTS", false},
    {0x0000000000004000ull, "elim1 ELIMINATOR", false},
    {0x0000000000008000ull, "the flag (a word is set)", false},
    {0x0000000000010000ull, "options3 TRAFFIC (third bit: Busy / Extreme)", false},
    {0x0000000000020000ull, "options3 START standing", false},
    {0x0000000000040000ull, "options3 RIVAL STYLE Road Rage", false},
    {0x0000000000080000ull, "options3 BURNOUT POINTS all", false},
    {0x0000000000700000ull, "options3 ROAD RAGE TIME LIMIT", false},
    {0x0000000000800000ull, "options3 TRAFFIC PATTERN varied", false},
    {0x0000000001000000ull, "party1 PARTY MODES (game_option_rules.h kPartyRecordBit)", false},
    {0x0000000002000000ull, "rrparts1 ROAD RAGE PARTS", false},
    {0x000000000C000000ull, "trafficcheck TRAFFIC CHECK (traffic_check_rules.h)", false},
    {0x0000000010000000ull, "trafficseries TRAFFIC CARS", false},
    {0x0000000020000000ull, "customseries EXTRA CARS (extra_series.h kExtraRecordBit)", false},
    {0x0000000700000000ull, "options3 DEFORMATION (game_mods.h)", false},
    {0x0000003800000000ull, "options3 RIVALS' CARS", false},
    {0x0000004000000000ull, "mixedrivals RIVALS' CARS Mixed", false},
};
// 'HTD1' (24 bytes: magic, serial, race word, label word, the records word's bits 32..63, 0)
const std::vector<BitOwner> kHtd1RaceWord = {
    {0x000000FFu, "todrace the race's time of day (0..2)"},
    {0x00000600u, "gameopts GAMEPLAY options (mirror, crashbreaker)"},
    {0xFFFF0000u, "raceopts the records word's bits 16..31 (options3)"},
};
const std::vector<BitOwner> kHtd1LabelWord = {
    {0x000000FFu, "todrace the label's choice"},
    {0x0000FF00u, "todrace the label's opacity"},
    {0xFFFF0000u, "raceopts the records word's bits 0..15"},
};
const std::vector<BitOwner> kHtd1Word4 = {
    {0xFFFFFFFFu, "options3 the records word's bits 32..63"},
};

// The host-only records native_play publishes for the renderer (src/b3replay/hud.h, include/b3live/menu_input.h): each
// owner's base and the bytes it may use (a record's own size or the room before the next), in the HUD's host region
struct HostRecord {
    std::uint32_t base, room;
    const char* owner;
};
const std::vector<HostRecord> kHostRecords = {
    {0x41101000u, 0x1000u, "mac5 HSET the settings"},
    {0x41102000u, 0x10000u, "mac5 HFN1 the game's fonts"},
    {0x41112000u, 0x100u, "mac5 HSK1 the video skip hint"},
    {0x41112100u, 0x100u, "autosave1 HHD1 the display hold"},
    {0x41112200u, 0x100u, "todrace / raceopts HTD1 the race word, the label, the records word"},
    {0x41112300u, 0x100u, "paintsel HPS1 the PAINT prompt"},
    {0x41112400u, 0x100u, "elim1 HEL1 the TIMED ELIMINATION countdown"},
    {0x41112500u, 0x100u, "gearbox HGB1 the gear"},
    {0x41112600u, 0x100u, "paint3 HCM1 the carousel slots (16 + 4 x 32 bytes)"},
    {0x41112800u, 0x100u, "replayview HRV1 the race replay's state"},
    {0x41113000u, 0x400u, "mac5 HMN1 the menus on screen"},
};

bool disjoint(const std::vector<BitOwner>& v, std::string* clash) {
    for (std::size_t i = 0; i < v.size(); ++i)
        for (std::size_t j = i + 1; j < v.size(); ++j)
            if (v[i].mask & v[j].mask) {
                *clash = std::string(v[i].owner) + " / " + v[j].owner;
                return false;
            }
    return true;
}
std::uint32_t all_of(const std::vector<BitOwner>& v) {
    std::uint32_t m = 0;
    for (const BitOwner& b : v) m |= b.mask;
    return m;
}

TEST(arena_regions_disjoint) {
    using b3native::GuestArena;
    std::set<std::string> owners;
    b3host::gaddr at = GuestArena::kBase;
    for (const b3native::ArenaRegion& r : GuestArena::kPort) {
        CHECK(r.address >= at);   // in order, no overlap
        CHECK(r.size > 0 && r.address + r.size <= GuestArena::kPortEnd);
        CHECK(owners.insert(r.what).second);
        at = r.address + r.size;
    }
    CHECK(b3native::arena_port_regions_ok());
}

TEST(record_word_bits_disjoint) {
    for (std::size_t i = 0; i < kRecordWord.size(); ++i)
        for (std::size_t j = i + 1; j < kRecordWord.size(); ++j)
            if (kRecordWord[i].mask & kRecordWord[j].mask) {
                std::printf("  records word: %s / %s\n", kRecordWord[i].owner, kRecordWord[j].owner);
                CHECK(false);
            }
    std::uint64_t real = 0;
    for (const WordOwner& o : kRecordWord) if (!o.reserved) real |= o.mask;
    // every producer's own mask, as the code exports it
    const std::uint64_t gameopts = b3native::game_options_record_word(0, b3native::kGameOptAll | b3native::kGameOptParty) & ~std::uint64_t(0x8002u);   // (party1: bit 24)
    const std::uint64_t produced = b3native::kRaceWordMask | b3native::EliminatorRows::kRecordBit | b3native::kHeavyRecordBit | b3native::kTrafficRecordBit | b3native::kExtraRecordBit |
                                   gameopts | b3native::GameMods::kRecordMask | b3native::RoadRageParts::kRecordBit | b3native::kTcRecordMask;
    if (produced != real) std::printf("  records word: the producers' bits %016llX, the table's %016llX\n",
                                      static_cast<unsigned long long>(produced), static_cast<unsigned long long>(real));
    CHECK(produced == real);
    // raceopts / options3: every field at its largest stays inside its mask
    b3native::RaceChoice c;
    c.traffic = 5, c.laps = b3native::kRaceMaxLaps, c.rivals = b3native::kRoadRageMaxRivals, c.timed = 1, c.manual = 1, c.start = 1,
    c.style = 1, c.points = 1, c.limit = b3native::kRaceTimeLimits - 1, c.seed = 1, c.cars = b3native::kRaceRivalCars - 1;
    CHECK((c.record_word() & ~b3native::kRaceWordMask) == 0);
    CHECK((b3native::GameMods::record_word(0, 4) & ~(b3native::GameMods::kRecordMask | 0x8002u)) == 0);
    CHECK((b3native::traffic_check_record_word(0, b3native::kTcExtreme) & ~(b3native::kTcRecordMask | 0x8002u)) == 0);
    // one field alone each: a folder of its own (no two owners name the same folder)
    std::set<std::string> folders;
    for (const WordOwner& o : kRecordWord) {
        if (o.reserved || o.mask == 0x8000u || o.mask == 0x3u) continue;   // (TRAFFIC's low bits are its value, not a flag)
        const std::uint64_t one = o.mask & (~o.mask + 1u);   // its lowest bit
        CHECK(folders.insert(b3replay::records::options_folder(0x8000u | 0x0002u /* TRAFFIC Normal */ | one)).second);
    }
}

TEST(host_records_disjoint) {
    CHECK(!kHostRecords.empty());
    for (std::size_t i = 0; i < kHostRecords.size(); ++i)
        for (std::size_t j = i + 1; j < kHostRecords.size(); ++j) {
            const HostRecord& a = kHostRecords[i];
            const HostRecord& b = kHostRecords[j];
            if (a.base < b.base + b.room && b.base < a.base + a.room) {
                std::printf("  host records: %s / %s\n", a.owner, b.owner);
                CHECK(false);
            }
        }
}

TEST(htd1_fields_disjoint) {
    std::string clash;
    CHECK(disjoint(kHtd1RaceWord, &clash));
    CHECK(disjoint(kHtd1LabelWord, &clash));
    CHECK(disjoint(kHtd1Word4, &clash));
    if (!clash.empty()) std::printf("  HTD1: %s\n", clash.c_str());
    // the records word's 64 bits travel in HTD1's three fields: 0..15 the label word's high half, 16..31 the race word's,
    // 32..63 word 4
    CHECK((all_of(kHtd1LabelWord) & 0xFFFF0000u) == 0xFFFF0000u && (all_of(kHtd1RaceWord) & 0xFFFF0000u) == 0xFFFF0000u &&
          all_of(kHtd1Word4) == 0xFFFFFFFFu);
}

}  // namespace

TEST_MAIN("Owners of the shared fixed spaces: arena regions, records word bits, HTD1 fields, host records")
