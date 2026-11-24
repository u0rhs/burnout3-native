// road_rage_parts.h -- ROAD RAGE PARTS (rrparts1, 2026-11-21): in an offline Road Rage, every kind of loose body part can
// come off a damaged car, not only the kinds retail lets go (remaster-features §31).
//
// What the game does [C the generated unit func_00395A40, CB3OfflineRoadRageLogic's per-frame damage pass, called by its
// Update func_0038CF50 and by the online Road Rage's func_0038F830]: for each race car with a body (+0x210 == 0), from its
// Road Rage health h (car +0x13F4 -> +0x16B4): n the vehicle data's flying bits (+0xCC0 -> +0x40 -> +0xC), the parts
// still on (state +0xCC4 -> +0x4B2[j] not 3 / 4) of the kinds that can come off (the vehicle data's +0xAC4[j] not 0, 1, 3,
// 5 or 6) counted, and while that count is over min(h + 0.1, 1) x n, the flying bits in the order of D_004C9900 are
// loosened (state 0: ImpactFlyingBit for a player's car, ImpactRoadRage, func_001CBE50) and the first hanging one (state
// 2) of a kind that can come off is let go (func_001CBD80: state 3, a body from the physics manager's pool 0x55EF70,
// activated, its sound). Then, while the parts not yet loosened (state 0 / 1) are over the same share, more are
// loosened. The kinds 0, 1, 3, 5, 6 only ever hang (retail's gate at 0x395CD8: v1 = 1 for them, then "bnez v1").
//
// Ours: a port wrapper on func_00395A40 that, with the row on and the logic the offline Road Rage's (vtable 0x4DEE00 at
// the logic object, the function's a0), runs the same pass without that gate in its let-go step (the count before it
// still counts the kinds retail lets go, so more parts come off as the health falls); otherwise the
// game's own function. Only a SINGLE EVENT Road Rage (the current mode the one-player stage record with the Single Event
// tag, game_option_rules.h game_options_stage, as the other gameplay options): World Tour's keeps the game's parts, so its
// results and medals count; the online Road Rage is never changed (the other players would not see the same).
//
// The body-part pool: a part let go takes one of the physics manager's 64 body-part slots (CB3PhysicsManager::AddBodyPart
// 0x1B86A0 [C decomp unit]: +0xD2900, 0x540 each, a use mask at +0xE9218); with all 64 in use it recycles the oldest
// (that part marked gone, state 4) rather than fail, so more parts let go cannot run it out. The let-goes and the most
// slots seen in use are logged when the race ends.
//
// Gameplay: a part let go is a physics body (func_001B86A0 on the physics manager: it can hit and be hit), so the race
// is not the game's: its records are kept apart as raceopts' (records_guard.h: the post-race records call with no
// record kinds, the profile's "this race counts" answered no) and the renderer's record word takes kRecordBit (bit 25,
// "-loose-parts"; tests/test_bit_owners.cpp). Latched
// per race: once a part came off this way in a Road Rage, that race keeps no records even if the row is turned off.
// Replays: the row is a live setting ("road_rage_parts") latched per VBlank, so a playback makes the same calls.
// B3NATIVE_ROAD_RAGE_PARTS=0: not installed (the game's function always).
#ifndef B3NATIVE_ROAD_RAGE_PARTS_H
#define B3NATIVE_ROAD_RAGE_PARTS_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "b3host/ps2f.h"
#include "hook_table.h"
#include "game_option_rules.h"
#include "records_guard.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace b3native {

class RoadRageParts {
public:
    static constexpr std::uint64_t kRecordBit = 0x0000000002000000ull;   // the records word's bit 25 (src/b3replay/records.h)
    static std::uint64_t record_word(std::uint64_t options = 0) { return (options ? options : 0x8002u) | kRecordBit; }
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_ROAD_RAGE_PARTS");
        return !(e && e[0] == '0' && e[1] == 0);
    }
    // `on`: the row now (live settings, replays: the latched value)
    bool install(HookTable& hooks, bool (*on)());
    // A Road Rage whose parts came off this way is the game's race now (its records kept apart)
    static bool racing() { RoadRageParts* p = instance(); return p && p->used_; }
    // native_play, every VBlank: the latch ends when no Single Event stage is the current mode (back in the menus), so a
    // later Road Rage without the row keeps its records
    void refresh();
    std::uint64_t let_go() const { return let_go_; }

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static constexpr b3host::gaddr kDamagePass = 0x00395A40u;      // func_00395A40(logic)
    static constexpr b3host::gaddr kStageUpdate = 0x00395490u;     // CB3OfflineStageLogic::Update(logic): every offline race
    static constexpr b3host::gaddr kRecords = kRecordsUpdate, kProfileCounts = 0x00136F30u;
    static constexpr b3host::gaddr kOfflineRoadRage = 0x004DEE00u;   // __vt__23CB3OfflineRoadRageLogic
    static constexpr b3host::gaddr kCarCount = 0x0064D37Cu, kCars = 0x00522660u + 0x3C960u;   // (0x30 each, car at +0x2C)
    static constexpr b3host::gaddr kFlyingBits = 0x004C9900u;       // the 7 EVehicleFlyingBit in the pass's order
    static constexpr b3host::gaddr kFlyingIndex = 0x003E7600u;      // CB3VehicleDeform::GetFlyingObjectIndex(data, bit)
    static constexpr b3host::gaddr kImpactFlyingBit = 0x003E2970u;  // CB3VehicleDeform::ImpactFlyingBit(deform, car, i, f, f)
    static constexpr b3host::gaddr kImpactRoadRage = 0x003E2BF0u;   // CB3VehicleDeform::ImpactRoadRage(deform, car, id, i, f, f)
    static constexpr b3host::gaddr kLoosen = 0x001CBE50u, kLetGo = 0x001CBD80u;
    static constexpr b3host::gaddr kBodyPartMask = 0x0055EF70u + 0xE9218u;   // the physics manager's body-part slots in use
    static constexpr std::uint32_t kMask = 0x01FFFFFFu;   // guest pointers to the EE's 32 MB
    static bool single_event() {
        const std::uint32_t cur = b3host::load<std::uint32_t>(kGoCurrentMode) & kMask;
        return cur != 0 && game_options_stage(cur, b3host::load<std::uint32_t>(cur + 0x1B8u) & kMask);
    }

    static RoadRageParts*& instance() { static RoadRageParts* p = nullptr; return p; }
    static bool hangs_only(int kind) { return kind == 0 || kind == 1 || kind == 3 || kind == 5 || kind == 6; }
    static void damage_pass(const b3host::CallArgs& a, b3host::CallResult& r);
    static void stage_update(const b3host::CallArgs& a, b3host::CallResult& r);
    static void records(const b3host::CallArgs& a, b3host::CallResult& r);
    static void profile_counts(const b3host::CallArgs& a, b3host::CallResult& r);
    void pass();   // func_00395A40 without the let-go gate

    bool (*on_)() = nullptr;
    Entry damage_ = nullptr, stage_ = nullptr, records_ = nullptr, profile_ = nullptr;
    bool used_ = false;
    std::uint64_t let_go_ = 0, kept_go_ = 0, records_skipped_ = 0;
    std::uint64_t race_let_go_ = 0;   // this race's
    int slots_most_ = 0;              // the most body-part slots seen in use after a let-go this race
};

inline bool RoadRageParts::install(HookTable& hooks, bool (*on)()) {
    on_ = on;
    const std::vector<Hook> set = {{kDamagePass, HookLayer::kPort, &damage_pass, &damage_},
                                   {kStageUpdate, HookLayer::kPort, &stage_update, &stage_},
                                   {kRecords, HookLayer::kPort, &records, &records_},
                                   {kProfileCounts, HookLayer::kPort, &profile_counts, &profile_}};
    for (const Hook& h : set)
        if (!hooks.has_row(h.address)) {
            std::fprintf(stderr, "road_rage_parts: %08X is not native: ROAD RAGE PARTS left as retail\n", h.address);
            return false;
        }
    std::string error;
    if (!hooks.install("port:road_rage_parts", set, &error)) {
        std::fprintf(stderr, "road_rage_parts: %s: ROAD RAGE PARTS left as retail\n", error.c_str());
        return false;
    }
    instance() = this;
    return true;
}

// func_00395A40(logic): ours with the row on in an offline Road Rage, else the game's
inline void RoadRageParts::damage_pass(const b3host::CallArgs& a, b3host::CallResult& r) {
    RoadRageParts* p = instance();
    const b3host::gaddr logic = a.gpr[0].w[0] & 0x01FFFFFFu;
    if (!p->on_ || !p->on_() || logic == 0 || b3host::load<std::uint32_t>(logic) != kOfflineRoadRage || !single_event()) {
        p->damage_(a, r);
        return;
    }
    if (!p->used_) {
        p->used_ = true;
        std::printf("road_rage_parts: an offline Road Rage with every loose part let go: its records kept apart\n");
        std::fflush(stdout);
    }
    p->pass();
}

// CB3OfflineStageLogic::Update(logic): another kind of offline race ends the latch
inline void RoadRageParts::stage_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    RoadRageParts* p = instance();
    const b3host::gaddr logic = a.gpr[0].w[0] & 0x01FFFFFFu;
    if (p->used_ && logic != 0 && b3host::load<std::uint32_t>(logic) != kOfflineRoadRage) p->used_ = false;
    p->stage_(a, r);
}

inline void RoadRageParts::refresh() {
    if (!used_ || single_event()) return;
    used_ = false;
    std::printf("road_rage_parts: the Road Rage ended: %llu parts let go, the body-part pool at most %d of 64 in use\n",
                static_cast<unsigned long long>(race_let_go_), slots_most_);
    std::fflush(stdout);
    race_let_go_ = 0;
    slots_most_ = 0;
}

inline void RoadRageParts::records(const b3host::CallArgs& a, b3host::CallResult& r) {
    RoadRageParts* p = instance();
    if (!p->used_) {
        p->records_(a, r);
        return;
    }
    const std::uint64_t kinds = records_without_kinds(p->records_, a, r);
    ++p->records_skipped_;
    std::printf("road_rage_parts: no records kept for this Road Rage (the kinds %llx not written)\n", static_cast<unsigned long long>(kinds));
    std::fflush(stdout);
}

inline void RoadRageParts::profile_counts(const b3host::CallArgs& a, b3host::CallResult& r) {
    RoadRageParts* p = instance();
    p->profile_(a, r);
    if ((r.v0.w[0] & 0xFFu) != 0 && p->used_) r.v0 = u_long128{};
}

inline void RoadRageParts::pass() {
    using b3host::call_guest;
    using b3host::load;
    const int cars = load<std::int32_t>(kCarCount);
    for (int i = 0; i < cars; ++i) {
        const b3host::gaddr car = load<std::uint32_t>(kCars + 0x30u * std::uint32_t(i) + 0x2Cu) & kMask;
        if (car == 0) continue;
        if (load<std::uint8_t>(car + 0x210u) != 0) continue;
        const b3host::gaddr cc4 = load<std::uint32_t>(car + 0xCC4u) & kMask, h = load<std::uint32_t>(car + 0x13F4u) & kMask;
        const b3host::gaddr data = load<std::uint32_t>((load<std::uint32_t>(car + 0xCC0u) & kMask) + 0x40u) & kMask;
        const float health = load<float>(h + 0x16B4u);
        if (health < 0.7f) b3host::store<std::uint8_t>(cc4 + 0x1014u, 2);
        else if (health < 0.95f) b3host::store<std::uint8_t>(cc4 + 0x1014u, 1);
        if (cc4 == 0 || h == 0 || data == 0) continue;
        const int n = load<std::int8_t>(data + 0xCu);
        auto state = [&](int j) { return load<std::int8_t>(cc4 + 0x4B2u + std::uint32_t(j)); };
        auto kind = [&](int j) { return load<std::int32_t>(data + 0xAC4u + 4u * std::uint32_t(j)); };
        int on = 0;
        for (int j = 0; j < n; ++j)
            if (state(j) != 3 && state(j) != 4 && !hangs_only(kind(j))) ++on;
        // the share as the game's unit computes it, in the PS2's float (b3host::ps2f: its add / mul, cvt.s.w, cvt.w.s)
        b3host::ps2f share = 0.1f;
        share += b3host::ps2f(health);
        if (1.0f < float(share)) share = 1.0f;   // GtMath::Min(0.1 + h, 1)
        share *= b3host::ps2f(n);
        const int keep = static_cast<int>(share);
        auto loosen = [&](int idx) {
            if (load<std::uint8_t>(car + 0x215u) == 1)
                call_guest<void>(kImpactFlyingBit, cc4 + 0x300u, car, idx, 1.0f, 0.5f);
            call_guest<void>(kImpactRoadRage, cc4 + 0x300u, car, load<std::int64_t>(h + 0x1960u), 0, 1.0f, 0.5f);
            call_guest<void>(kLoosen, car, idx);
        };
        // the let-go step: as the game's, without its gate on the part's kind (a sweep that finds nothing ends it:
        // the game's loop would sweep again; with the gate gone there is always a hanging part once one is loosened)
        for (int over = on - keep; over > 0;) {
            bool any = false;
            for (int m = 0; m < 7; ++m) {
                const int idx = call_guest<int>(kFlyingIndex, data, load<std::int32_t>(kFlyingBits + 4u * std::uint32_t(m)));
                if (idx < 0) continue;
                if (state(idx) == 0) loosen(idx);
                if (state(idx) != 2) continue;
                call_guest<void>(kLetGo, car, idx);
                if (state(idx) == 3) {
                    if (hangs_only(kind(idx)) && kept_go_++ == 0) {
                        std::printf("road_rage_parts: a part retail keeps hanging came off (car %d, part %d of kind %d)\n", i, idx, kind(idx));
                        std::fflush(stdout);
                    }
                    ++let_go_;
                    ++race_let_go_;
                    slots_most_ = std::max(slots_most_, __builtin_popcountll(load<std::uint64_t>(kBodyPartMask)));
                    if (race_let_go_ % 25 == 0) {
                        std::printf("road_rage_parts: %llu parts let go this race, the body-part pool at most %d of 64 in use\n",
                                    static_cast<unsigned long long>(race_let_go_), slots_most_);
                        std::fflush(stdout);
                    }
                }
                --over;
                any = true;
                break;
            }
            if (!any) break;
        }
        int loose = 0;
        for (int j = 0; j < n; ++j)
            if (std::uint8_t(state(j)) < 2) ++loose;
        for (int over = loose - keep; over > 0;) {
            bool any = false;
            for (int m = 0; m < 7; ++m) {
                const int idx = call_guest<int>(kFlyingIndex, data, load<std::int32_t>(kFlyingBits + 4u * std::uint32_t(m)));
                if (idx < 0 || std::uint8_t(state(idx)) >= 2) continue;
                loosen(idx);
                --over;
                any = true;
                break;
            }
            if (!any) break;
        }
    }
}

}  // namespace b3native

#endif
