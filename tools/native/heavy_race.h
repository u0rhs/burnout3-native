// heavy_race.h -- heavyseries (2026-11-21): the HEAVYWEIGHTS series on SINGLE EVENT's SELECT CAR for RACE, TIME ATTACK and
// ROAD RAGE, and a heavyweight race's records kept apart. What the game does and the rules: heavy_series.h.
//
// Ours (wrappers on the hook table's port layer; the carousel, its texts, the cars and the race are the game's):
//   - func_0030CEC0 (SELECT CAR's "heavyweights allowed", read by the car select state only): true as well for SINGLE
//     EVENT's RACE / TIME ATTACK / ROAD RAGE while the series is on and the profile has a heavyweight unlocked (counted
//     from guest RAM as the carousel's 0x42B7C0 and IsCarUnlocked read it: no guest call, which would cost EE time).
//     The carousel then
//     goes one series further right, to the game's own HEAVYWEIGHTS page;
//   - the carousel's Prepare 0x42CFA0 after our answer: a remembered heavyweight opens on the game's first series (class
//     0, row 0), as retail's own reset does when the bound excludes it -- SELECT CAR opens as it always did;
//   - a heavyweight race (heavy_series.h heavy_race: the Single Event stage of those modes, player 0's car class 6):
//     the post-race records call (0x3D1090, CB3Records 0x01EA7810, from 0x42F0F0) is made with no record kinds (a1: it
//     then returns at once and reports no new record), the last update's type and track put back as a race that set
//     none (the results page's RECORD tags read them), and the profile's "this race counts" test (0x136F30: totals,
//     unlocks, the signature takedowns) answers no.
// The series' on / off is the host's (B3NATIVE_HEAVY_SERIES=0: off; a replay: its "heavy_series" entry), never saved.
#ifndef B3NATIVE_HEAVY_RACE_H
#define B3NATIVE_HEAVY_RACE_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "heavy_series.h"
#include "traffic_series.h"   // (a traffic vehicle of the HEVY class is not a heavyweight race car)
#include "hook_table.h"
#include "records_guard.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace b3native {

class HeavyRace {
public:
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_HEAVY_SERIES");
        return !(e && e[0] == '0' && e[1] == 0);
    }
    bool install(HookTable& hooks);
    // whether the series is offered now (native_play: the setting, or a replay's entry), asked at each SELECT CAR
    void set_on_query(std::function<bool()> f) { on_ = std::move(f); }
    // the race on now is a heavyweight race (guest RAM; VBlank side too)
    static bool racing();
    struct Counts {
        std::uint64_t offered = 0, records_skipped = 0, profile_skipped = 0;
        int unlocked = 0, cars = 0;   // at the last offer: heavyweights unlocked, and in the class
    };
    const Counts& counts() const { return counts_; }

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static HeavyRace*& instance() { static HeavyRace* p = nullptr; return p; }
    static void allowed(const b3host::CallArgs& a, b3host::CallResult& r);
    static void records(const b3host::CallArgs& a, b3host::CallResult& r);
    static void profile_counts(const b3host::CallArgs& a, b3host::CallResult& r);
    static void carousel_prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    Entry allowed_ = nullptr, records_ = nullptr, profile_counts_ = nullptr, carousel_prepare_ = nullptr;
    std::function<bool()> on_;
    bool offered_now_ = false;   // the last "heavyweights allowed" was ours (the carousel's Prepare follows it)
    Counts counts_;
};

namespace heavy_race_detail {
constexpr b3host::gaddr kAllowed = 0x0030CEC0u;          // [C] func_0030CEC0
constexpr b3host::gaddr kRecords = kRecordsUpdate, kProfileCounts = 0x00136F30u;
constexpr b3host::gaddr kCarouselPrepare = 0x0042CFA0u;  // [C] the carousel's Prepare(mode, car, class, row, +0x264, +0x265, colour)
constexpr b3host::gaddr kRecordsObject = 0x01EA7810u;  // [C] CB3Records (records_guard.h has the call)
constexpr b3host::gaddr kMenuChoices = 0x01E900B0u;      // [C] CB3MenuChoices: +4 top menu, +8 mode, +0x80 cars
constexpr b3host::gaddr kCurrentMode = 0x0051BA88u;
constexpr b3host::gaddr kVehicleList = 0x005186A0u;      // [C] CB3VehicleList D_005186A0
constexpr b3host::gaddr kRaceVehicleIds = 0x004CCF80u;   // [C] D_004CCF80: the 67 race vehicles' ids (func_003EB060)
constexpr b3host::gaddr kUnlockedCars = 0x0051BFA0u;     // [C] D_0051BFA0: their unlocked bytes (IsCarUnlocked 0x13A660)
}  // namespace heavy_race_detail

inline bool HeavyRace::racing() {
    using namespace heavy_race_detail;
    HeavyStage s;
    s.current_mode = b3host::load<std::uint32_t>(kCurrentMode) & 0x01FFFFFFu;
    s.tag = b3host::load<std::uint32_t>(kHeavyOnePlayerStage + 0x1B8u) & 0x01FFFFFFu;
    s.game_type = b3host::load<std::int32_t>(kMenuChoices + 4u);
    s.mode = b3host::load<std::int32_t>(kMenuChoices + 8u);
    s.car = b3host::load<std::uint64_t>(kMenuChoices + 0x80u);
    return heavy_race(s) && traffic_index(s.car) < 0;   // (trafficseries: the HEVY traffic vehicles are traffic_cars.h's)
}

inline bool HeavyRace::install(HookTable& hooks) {
    using namespace heavy_race_detail;
    const std::vector<Hook> set = {{kAllowed, HookLayer::kPort, &allowed, &allowed_},
                                   {kRecords, HookLayer::kPort, &records, &records_},
                                   {kProfileCounts, HookLayer::kPort, &profile_counts, &profile_counts_},
                                   {kCarouselPrepare, HookLayer::kPort, &carousel_prepare, &carousel_prepare_}};
    for (const Hook& h : set)
        if (!hooks.has_row(h.address)) {
            std::fprintf(stderr, "heavy_series: %08X is not native: SELECT CAR left as retail\n", h.address);
            return false;
        }
    std::string error;
    if (!hooks.install("port:heavy_series", set, &error)) {
        std::fprintf(stderr, "heavy_series: %s: SELECT CAR left as retail\n", error.c_str());
        return false;
    }
    instance() = this;
    std::printf("heavy_series: HEAVYWEIGHTS on SELECT CAR for SINGLE EVENT RACE / TIME ATTACK / ROAD RAGE when unlocked "
                "(B3NATIVE_HEAVY_SERIES=0: retail's list)\n");
    std::fflush(stdout);
    return true;
}

// func_0030CEC0 (the car select state's "heavyweights allowed"): the game's, or the series for those modes
inline void HeavyRace::allowed(const b3host::CallArgs& a, b3host::CallResult& r) {   // (reads only: no guest call)
    using namespace heavy_race_detail;
    HeavyRace* p = instance();
    p->allowed_(a, r);
    const bool game = (r.v0.w[0] & 0xFFu) != 0;
    const std::int32_t type = b3host::load<std::int32_t>(kMenuChoices + 4u), mode = b3host::load<std::int32_t>(kMenuChoices + 8u);
    if (game || !heavy_series_mode(type, mode) || !(p->on_ && p->on_())) return;
    // the profile's heavyweights, read as the carousel and IsCarUnlocked read them (heavy_series.h heavy_count)
    const int list_count = b3host::load<std::int32_t>(kVehicleList + 4u);
    const HeavyCount hc = heavy_count(
        list_count, [](int i) { return b3host::load<std::uint64_t>(kVehicleList + 0x408u + 8u * unsigned(i)); },
        [](int i) { return b3host::load<std::int32_t>(kVehicleList + 8u + 4u * unsigned(i)) != 0; },
        [](int k) { return b3host::load<std::uint64_t>(kRaceVehicleIds + 8u * unsigned(k)); },
        [](int k) { return b3host::load<std::uint8_t>(kUnlockedCars + unsigned(k)) != 0; });
    const int cars = hc.cars, unlocked = hc.unlocked, n = list_count;
    p->counts_.cars = cars;
    p->counts_.unlocked = unlocked;
    if (!heavy_series_allowed(false, true, type, mode, unlocked)) {
        std::printf("heavy_series: SELECT CAR (mode %d): no heavyweight unlocked (%d in the class): no HEAVYWEIGHTS series\n", mode, cars);
        std::fflush(stdout);
        return;
    }
    ++p->counts_.offered;
    std::printf("heavy_series: SELECT CAR (mode %d): HEAVYWEIGHTS offered (%d of %d unlocked; %d vehicles listed)\n", mode, unlocked, cars, n);
    std::fflush(stdout);
    p->offered_now_ = true;
    r.v0 = u_long128{};
    r.v0.w[0] = 1;
}

// func_0042CFA0 (the carousel's Prepare, from SELECT CAR's Prepare right after the answer above): with the series ours, a
// remembered heavyweight (all classes, class 6) starts on the game's first series as retail puts it (its own reset when
// +0x264 is 0: class 0, row 0), so a player's SELECT CAR opens as it always did; HEAVYWEIGHTS is a Right away
inline void HeavyRace::carousel_prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    HeavyRace* p = instance();
    const bool ours = p->offered_now_;
    p->offered_now_ = false;
    if (!ours || a.gpr[1].w[0] != 9u || std::int32_t(a.gpr[2].w[0]) != -1 || a.gpr[3].w[0] != std::uint32_t(kHeavyClass) ||
        (a.gpr[5].w[0] & 0xFFu) == 0) {
        p->carousel_prepare_(a, r);
        return;
    }
    b3host::CallArgs b = a;
    b.gpr[3] = u_long128{};
    b.gpr[4] = u_long128{};
    std::printf("heavy_series: SELECT CAR opens on the first series (the remembered heavyweight's class, as retail)\n");
    std::fflush(stdout);
    p->carousel_prepare_(b, r);
}

// 0x3D1090(records, the record kinds, a second update): no kinds in a heavyweight race; then the last update's type and
// track as a race that set no record leaves them (type 0, this stage's track), so the results page's RECORD tags read
// this track's tables and show none (kinds 0 return before setting them: the tags would read a stale track)
inline void HeavyRace::records(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace heavy_race_detail;
    HeavyRace* p = instance();
    if (!racing()) {
        p->records_(a, r);
        return;
    }
    std::vector<std::uint8_t> before(kRecordsBytes);
    for (unsigned i = 0; i < kRecordsBytes; ++i) before[i] = b3host::load<std::uint8_t>(kRecordsObject + i);
    const std::uint64_t kinds = records_without_kinds(p->records_, a, r);   // (records_guard.h: kinds 0, type / track put back)
    unsigned marks = 0, values = 0;   // (heavy_series.h record_mark_byte: the marks are cleared by every update)
    for (unsigned i = 0; i < kRecordsBytes; ++i)
        if (b3host::load<std::uint8_t>(kRecordsObject + i) != before[i]) ++(record_mark_byte(i) ? marks : values);
    ++p->counts_.records_skipped;
    std::printf("heavy_series: a heavyweight race: no records kept (the kinds %llx not written; the records' tables: %u "
                "record bytes changed, %u new-record marks cleared as every update does)\n",
                static_cast<unsigned long long>(kinds), values, marks);
    std::fflush(stdout);
}

// 0x136F30 (whether the race counts for the profile): not a heavyweight race
inline void HeavyRace::profile_counts(const b3host::CallArgs& a, b3host::CallResult& r) {
    HeavyRace* p = instance();
    p->profile_counts_(a, r);
    if ((r.v0.w[0] & 0xFFu) == 0 || !racing()) return;
    if (p->counts_.profile_skipped++ == 0) {
        std::printf("heavy_series: a heavyweight race: the profile's totals and unlocks left as they were\n");
        std::fflush(stdout);
    }
    r.v0 = u_long128{};
}

}  // namespace b3native

#endif
