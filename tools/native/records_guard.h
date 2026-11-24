// records_guard.h -- a race that keeps no game records (raceopts' races with their options, elim1's Single Event
// Eliminator, heavyseries' heavyweight races, gameopts' mirrored / Crashbreaker races): CB3Records::Update called with no record kinds, the records left as a
// race that set no record leaves them.
//
// CB3Records::Update 0x3D1090(records 0x01EA7810, the record kinds, a flag) [C func_003D1090: a1 the kinds (0 returns at
// once; 0x30 / 0x10 pick the crash tables), a2 only gates a second update; func_0042F0F0 passes (&D_01EA7810, +0x1C,
// +0xD5)]. With kinds 0 it sets the last update's type (D_004E2EE0) to 3 ("none") and returns before the track
// (D_004E2EE8), so the results page's RECORD tags (func_00452ED0: GetTrackRecords(D_004E2EE8), the entries' new-record
// marks, which the call cleared) would read a stale track: they are set as a race without a record leaves them, the type "track" (0) and the
// race's track (the current stage record's +0, *0x0051BA88); gameopts: for a crash junction's kinds (0x30: a mirrored
// Crash event) the junction's, type 1 (0x10) or 2 and D_004E2EF0 = the stage's +0x68, as func_003D1090 sets them.
//
// A call the game makes with no kinds itself (integfix: e.g. a results page with none by design) goes straight through,
// D_004E2EE0 / E8 / F0 as the game leaves them (type 3). Two guards on the same call (both hooks on 0x3D1090): the outer
// one sets the values after the inner one passed the call through.
#ifndef B3NATIVE_RECORDS_GUARD_H
#define B3NATIVE_RECORDS_GUARD_H

#include "b3host/call.h"
#include "b3host/host.h"

#include <cstdint>

namespace b3native {

constexpr b3host::gaddr kRecordsUpdate = 0x003D1090u;                          // CB3Records::Update(kinds, flag)
constexpr b3host::gaddr kRecordsLastType = 0x004E2EE0u, kRecordsLastTrack = 0x004E2EE8u, kRecordsLastCrash = 0x004E2EF0u;
constexpr b3host::gaddr kRecordsCurrentStage = 0x0051BA88u;                    // the current stage record's pointer

// Calls `next` (the layer below on 0x3D1090) with no record kinds; returns the kinds the game asked for
inline std::uint64_t records_without_kinds(void (*next)(const b3host::CallArgs&, b3host::CallResult&),
                                           const b3host::CallArgs& a, b3host::CallResult& r) {
    const std::uint64_t kinds = a.gpr[1].w[0] | (std::uint64_t(a.gpr[1].w[1]) << 32);
    if (kinds == 0) {   // integfix: none asked for: the game's own call, untouched
        next(a, r);
        return 0;
    }
    b3host::CallArgs b = a;
    b.gpr[1] = u_long128{};
    next(b, r);
    const b3host::gaddr stage = b3host::load<std::uint32_t>(kRecordsCurrentStage) & 0x01FFFFFFu;
    if (kinds & 0x30u) {   // a crash junction's
        b3host::store<std::uint32_t>(kRecordsLastType, (kinds & 0x10u) ? 1u : 2u);
        if (stage) b3host::store<std::uint64_t>(kRecordsLastCrash, b3host::load<std::uint64_t>(stage + 0x68u));
        return kinds;
    }
    b3host::store<std::uint32_t>(kRecordsLastType, 0);
    if (stage) b3host::store<std::uint64_t>(kRecordsLastTrack, b3host::load<std::uint64_t>(stage));
    return kinds;
}

}  // namespace b3native

#endif
