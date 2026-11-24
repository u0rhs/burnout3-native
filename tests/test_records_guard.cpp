// integfix: tools/native/records_guard.h records_without_kinds, the records call of a race that keeps no game records.
// A fake CB3Records::Update (0x3D1090) does what func_003D1090 does with the last update's type / track / crash
// (D_004E2EE0 / E8 / F0): type 3 first, then, with kinds, the track's (0) or the crash junction's (1 / 2).
#include "test.h"

#include "b3host/null_backend.h"
#include "records_guard.h"

#include <cstdint>

namespace {

using namespace b3native;
using b3host::NullBackend;
using b3host::BackendScope;

constexpr b3host::gaddr kStage = 0x00600000u;   // [G] a stage record: +0 the track, +0x68 the crash junction
int g_calls = 0;
std::uint64_t g_kinds_seen = ~0ull;

void fake_update(const b3host::CallArgs& a, b3host::CallResult& r) {   // func_003D1090's stores
    ++g_calls;
    g_kinds_seen = a.gpr[1].w[0] | (std::uint64_t(a.gpr[1].w[1]) << 32);
    b3host::store<std::uint32_t>(kRecordsLastType, 3);
    r.v0 = u_long128{};
    if (g_kinds_seen == 0) return;
    const b3host::gaddr g = b3host::load<std::uint32_t>(kRecordsCurrentStage);
    if (g_kinds_seen & 0x30u) {
        b3host::store<std::uint64_t>(kRecordsLastCrash, b3host::load<std::uint64_t>(g + 0x68u));
        b3host::store<std::uint32_t>(kRecordsLastType, (g_kinds_seen & 0x10u) ? 1u : 2u);
    } else {
        b3host::store<std::uint32_t>(kRecordsLastType, 0);
        b3host::store<std::uint64_t>(kRecordsLastTrack, b3host::load<std::uint64_t>(g));
    }
}

b3host::CallArgs call(std::uint64_t kinds) {
    b3host::CallArgs a;
    a.gpr[0].w[0] = 0x01EA7810u;
    a.gpr[1].w[0] = std::uint32_t(kinds);
    a.gpr[1].w[1] = std::uint32_t(kinds >> 32);
    a.n_gpr = 3;
    return a;
}

void setup() {
    g_calls = 0;
    g_kinds_seen = ~0ull;
    b3host::store<std::uint32_t>(kRecordsCurrentStage, kStage);
    b3host::store<std::uint64_t>(kStage, 0x1111222233334444ull);           // this race's track
    b3host::store<std::uint64_t>(kStage + 0x68u, 0x5555666677778888ull);   // its crash junction
    b3host::store<std::uint32_t>(kRecordsLastType, 7);                     // what an earlier update left
    b3host::store<std::uint64_t>(kRecordsLastTrack, 0xAAAAull);
    b3host::store<std::uint64_t>(kRecordsLastCrash, 0xBBBBull);
}

}  // namespace

TEST(kinds_zero_goes_straight_through) {
    // the game's own call with no kinds (e.g. a results page with none by design): the call as it is, type 3 kept, the
    // track and crash words untouched (was: type 0 and this race's track, so the page read this track's records)
    NullBackend be;
    BackendScope s(be);
    setup();
    b3host::CallResult r;
    CHECK(records_without_kinds(&fake_update, call(0), r) == 0);
    CHECK(g_calls == 1 && g_kinds_seen == 0);
    CHECK(b3host::load<std::uint32_t>(kRecordsLastType) == 3);
    CHECK(b3host::load<std::uint64_t>(kRecordsLastTrack) == 0xAAAAull);
    CHECK(b3host::load<std::uint64_t>(kRecordsLastCrash) == 0xBBBBull);
}

TEST(track_kinds_removed_type_and_track_as_a_race_without_a_record) {
    NullBackend be;
    BackendScope s(be);
    setup();
    b3host::CallResult r;
    CHECK(records_without_kinds(&fake_update, call(0x0Fu), r) == 0x0Fu);
    CHECK(g_calls == 1 && g_kinds_seen == 0);   // the game's update ran with none
    CHECK(b3host::load<std::uint32_t>(kRecordsLastType) == 0);
    CHECK(b3host::load<std::uint64_t>(kRecordsLastTrack) == 0x1111222233334444ull);
    CHECK(b3host::load<std::uint64_t>(kRecordsLastCrash) == 0xBBBBull);
}

TEST(crash_kinds_the_junctions) {
    NullBackend be;
    BackendScope s(be);
    for (const std::uint64_t k : {0x10ull, 0x20ull}) {
        setup();
        b3host::CallResult r;
        CHECK(records_without_kinds(&fake_update, call(k), r) == k);
        CHECK(g_kinds_seen == 0);
        CHECK(b3host::load<std::uint32_t>(kRecordsLastType) == (k == 0x10 ? 1u : 2u));
        CHECK(b3host::load<std::uint64_t>(kRecordsLastCrash) == 0x5555666677778888ull);
        CHECK(b3host::load<std::uint64_t>(kRecordsLastTrack) == 0xAAAAull);
    }
}

TEST_MAIN("records_guard: a race without game records (integfix)")
