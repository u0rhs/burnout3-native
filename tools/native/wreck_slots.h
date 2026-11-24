// wreck_slots.h -- wreckguard (2026-11-21): a bound for the game's 64 crashed-traffic slots.
//
// What the game has [C the generated units; decomp src/b3/_unclassified/B3PhysicsManager_001B41D0.cpp,
// src/nonmatching/UpdateActiveBodyList__17CB3PhysicsManagerFv.cpp, B3PhysicsManager_001BA280.cpp]:
// - CB3PhysicsManager (0x0055EF70) has 64 CB3TrafficPhysics slots for crashed traffic (+0x32D00, 0x2430 each, in use at
//   +0x210) and the list of those in use (+0xE6100, its count at +0xE620C; room for 67).
// - CrashTrafficBody (0x1B41D0) takes the first free slot with no bound: with all 64 in use it takes "slot" 64 (the
//   memory after them) and the list runs past its room into its own count -- a corrupt list, then a crash in the next
//   physics update (its loop over the list).
// - The game releases a wreck in UpdateActiveBodyList (0x1B7F30, PostEffectsUpdate's whole body) when its active body
//   (+0x70, 0x30 each, the count at +0x1CB70; +0 type 4, +1 the removal mark, +0xC the slot) is marked: the slot's
//   vtable +0x14 (its release), the slot out of the list (the last one moved into its place), the body extracted.
//   Wrecks near the players are kept, so a player stuck in Heavy traffic (SELECT OPTIONS' TRAFFIC) can see the slots fill.
// Ours: CrashTrafficBody with fewer free slots than it needs (one, and one more for each linked cab / trailer of the
// vehicle, +0x10C / +0x110) first releases a wreck as UpdateActiveBodyList would, at once -- one in none of this frame's
// colliding pairs (+0xE6210, 0x30 each, the count at +0xE9210: the collision loop may still reach those), at rest before
// moving, unlinked before linked, the farthest from the race cars --
// its release, its slot out of the list, a linked partner unlinked; its body set to type 8 (a body part: never collided
// nor updated) with no object and the removal mark, so UpdateActiveBodyList extracts it at the end of the frame. Below 64
// it does nothing. Not in the online game (its network table of wrecks is the game's to keep). B3NATIVE_WRECK_GUARD=0: off.
#ifndef B3NATIVE_WRECK_SLOTS_H
#define B3NATIVE_WRECK_SLOTS_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "hook_table.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace b3native {

// The wreck to release (its index; -1: none may go): never one in this frame's colliding pairs (the collision loop is
// still to reach them), then the farthest from its nearest race car (away2: the squared distance), an unlinked one at
// rest before an unlinked moving one, before a linked one at rest, before a linked moving one.
struct WreckCandidate { float away2 = 0.0f; bool linked = false, moving = false, in_pair = false; };
inline int wreck_to_release(const std::vector<WreckCandidate>& c) {
    int best = -1;
    for (int pass = 0; pass < 4 && best < 0; ++pass)
        for (std::size_t i = 0; i < c.size(); ++i) {
            if (c[i].in_pair || c[i].linked != (pass >= 2) || c[i].moving != (pass % 2 == 1)) continue;
            if (best < 0 || c[i].away2 > c[std::size_t(best)].away2) best = int(i);
        }
    return best;
}

class WreckSlots {
public:
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_WRECK_GUARD");
        return !(e && e[0] == '0' && !e[1]);
    }
    bool install(HookTable& hooks);
    std::uint64_t released() const { return released_; }

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static WreckSlots*& instance() { static WreckSlots* p = nullptr; return p; }
    static void crash_traffic(const b3host::CallArgs& a, b3host::CallResult& r);
    bool release_one(b3host::gaddr mgr);
    Entry crash_traffic_ = nullptr;
    std::uint64_t released_ = 0;
};

namespace wreck_slots_detail {
constexpr b3host::gaddr kCrashTrafficBody = 0x001B41D0u, kOnline = 0x01E65499u;   // [C UpdateActiveBodyList] D_01E65499
constexpr std::uint32_t kBodies = 0x70u, kBodyCount = 0x1CB70u, kSlots = 0x32D00u, kSlotBytes = 0x2430u, kSlotCount = 64u;
constexpr std::uint32_t kList = 0xE6100u, kListCount = 0xE620Cu, kMaxBodies = (kBodyCount - kBodies) / 0x30u;
constexpr std::uint32_t kPairs = 0xE6210u, kPairCount = 0xE9210u, kPairBytes = 0x30u, kMaxPairs = 256u;   // this frame's pairs
constexpr std::uint32_t kSpeed = 0xBCu;   // the physics' speed (velocity +0xB0's w)
constexpr float kAtRest = 1.0f;           // m/s
constexpr std::uint32_t kInUse = 0x210u, kMatrix = 0x204u, kLinked = 0x2424u, kRelease = 0x14u;   // (vtable +0x14)
constexpr std::uint32_t kCab = 0x10Cu, kTrailer = 0x110u;   // a traffic vehicle's linked vehicles [C CrashTrafficBody]
inline b3host::gaddr ga(std::uint32_t v) { return v & 0x01FFFFFFu; }
inline unsigned free_slots(b3host::gaddr mgr) {
    unsigned n = 0;
    for (unsigned i = 0; i < kSlotCount; ++i) n += b3host::load<std::uint8_t>(mgr + kSlots + i * kSlotBytes + kInUse) == 0;
    return n;
}
}  // namespace wreck_slots_detail

inline bool WreckSlots::install(HookTable& hooks) {
    using namespace wreck_slots_detail;
    if (!hooks.has_row(kCrashTrafficBody)) {
        std::fprintf(stderr, "wreck_slots: %08X is not native: no wreck slot guard\n", kCrashTrafficBody);
        return false;
    }
    std::string error;
    if (!hooks.install("port:wreck_slots", {{kCrashTrafficBody, HookLayer::kPort, &crash_traffic, &crash_traffic_}}, &error)) {
        std::fprintf(stderr, "wreck_slots: %s: no wreck slot guard\n", error.c_str());
        return false;
    }
    instance() = this;
    return true;
}

// One wreck released as UpdateActiveBodyList releases one, its body left for it to extract
inline bool WreckSlots::release_one(b3host::gaddr mgr) {
    using namespace wreck_slots_detail;
    const unsigned n = b3host::load<std::uint32_t>(mgr + kBodyCount);
    struct Pos { float x, z; };
    std::vector<Pos> players;
    for (unsigned i = 0; i < n && i < kMaxBodies; ++i) {
        const b3host::gaddr b = mgr + kBodies + i * 0x30u;
        if (b3host::load<std::int8_t>(b) < 0 || b3host::load<std::int8_t>(b) > 2) continue;   // the race cars
        const b3host::gaddr m = ga(b3host::load<std::uint32_t>(ga(b3host::load<std::uint32_t>(b + 0xCu)) + kMatrix));
        if (m) players.push_back({b3host::load<float>(m + 0x30u), b3host::load<float>(m + 0x38u)});
    }
    std::vector<b3host::gaddr> paired;   // the bodies in this frame's colliding pairs
    for (unsigned i = 0, np = b3host::load<std::uint32_t>(mgr + kPairCount); i < np && i < kMaxPairs; ++i) {
        paired.push_back(ga(b3host::load<std::uint32_t>(mgr + kPairs + i * kPairBytes + 0x24u)));
        paired.push_back(ga(b3host::load<std::uint32_t>(mgr + kPairs + i * kPairBytes + 0x28u)));
    }
    std::vector<WreckCandidate> c;
    std::vector<b3host::gaddr> bodies;
    for (unsigned i = 0; i < n && i < kMaxBodies; ++i) {
        const b3host::gaddr b = mgr + kBodies + i * 0x30u;
        if (b3host::load<std::int8_t>(b) != 4 || b3host::load<std::uint8_t>(b + 1u) != 0) continue;
        const b3host::gaddr w = ga(b3host::load<std::uint32_t>(b + 0xCu));
        if (w < mgr + kSlots || w >= mgr + kSlots + kSlotCount * kSlotBytes) continue;
        const b3host::gaddr m = ga(b3host::load<std::uint32_t>(w + kMatrix));
        float away2 = 0.0f;
        if (m) {
            away2 = 3.0e38f;
            for (const Pos& p : players) {
                const float dx = b3host::load<float>(m + 0x30u) - p.x, dz = b3host::load<float>(m + 0x38u) - p.z;
                away2 = std::min(away2, dx * dx + dz * dz);
            }
        }
        c.push_back({away2, b3host::load<std::uint32_t>(w + kLinked) != 0, std::fabs(b3host::load<float>(w + kSpeed)) > kAtRest,
                     std::find(paired.begin(), paired.end(), b) != paired.end()});
        bodies.push_back(b);
    }
    const int k = wreck_to_release(c);
    if (k < 0) return false;
    const b3host::gaddr b = bodies[std::size_t(k)], w = ga(b3host::load<std::uint32_t>(b + 0xCu));
    const unsigned before = free_slots(mgr);
    b3host::call_guest<void>(ga(b3host::load<std::uint32_t>(ga(b3host::load<std::uint32_t>(w)) + kRelease)), w);
    if (free_slots(mgr) <= before) std::printf("wreck_slots: wreck %08X released but its slot still reads in use\n", w);
    const std::int32_t count = b3host::load<std::int32_t>(mgr + kListCount);
    for (std::int32_t j = 0; j < count && j < 67; ++j)
        if (ga(b3host::load<std::uint32_t>(mgr + kList + 4u * unsigned(j))) == w) {
            b3host::store<std::uint32_t>(mgr + kList + 4u * unsigned(j), b3host::load<std::uint32_t>(mgr + kList + 4u * unsigned(count - 1)));
            b3host::store<std::int32_t>(mgr + kListCount, count - 1);
            break;
        }
    if (const b3host::gaddr partner = ga(b3host::load<std::uint32_t>(w + kLinked))) {
        b3host::store<std::uint32_t>(partner + kLinked, 0);
        b3host::store<std::uint32_t>(w + kLinked, 0);
    }
    b3host::store<std::int8_t>(b, 8);
    b3host::store<std::uint8_t>(b + 1u, 1);
    b3host::store<std::uint32_t>(b + 0xCu, 0);
    ++released_;
    std::printf("wreck_slots: all crashed-traffic slots in use: wreck %08X released (%.0f m from the nearest race car; %llu so far)\n",
                w, double(std::sqrt(c[std::size_t(k)].away2 < 1e30f ? c[std::size_t(k)].away2 : 0.0f)),
                static_cast<unsigned long long>(released_));
    std::fflush(stdout);
    return true;
}

// CrashTrafficBody(manager, body, ...): enough free slots for the vehicle and its linked ones first
inline void WreckSlots::crash_traffic(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace wreck_slots_detail;
    WreckSlots* p = instance();
    const b3host::gaddr mgr = ga(a.gpr[0].w[0]), body = ga(a.gpr[1].w[0]);
    if (mgr && body && b3host::load<std::uint8_t>(kOnline) == 0) {
        const b3host::gaddr veh = ga(b3host::load<std::uint32_t>(body + 0xCu));
        unsigned need = 1;
        if (veh && b3host::load<std::int8_t>(body) == 3)
            need += (b3host::load<std::uint32_t>(veh + kCab) != 0) + (b3host::load<std::uint32_t>(veh + kTrailer) != 0);
        for (int tries = 0; tries < 3 && free_slots(mgr) < need; ++tries)
            if (!p->release_one(mgr)) break;
    }
    p->crash_traffic_(a, r);
}

}  // namespace b3native

#endif
