// lane_pair_fix.h -- CB3AITarget::UpdateCurrentSegment (0x14CA20): the lane's point pairs read as retail does.
//
// The generated function (the decomp's draft, son9) has one wrong line in a rare branch: the target has no segment
// (+0x1D8 0xFFFF: after CB3AITarget::ResetTarget 0x149140), its car's current lane segment is not drivable
// (LaneIsDriveable 0x148C20 false) and both neighbouring lanes the segment record names (+4 / +6, +5 / +8) are
// drivable. Retail then picks the neighbour on the car's side of the segment's middle [C retail 0x14CB34..0x14CBD0]:
//   q = lane->field_4->field_0 + seg * 4 (u16 point indices: this segment's pair, then the next one's), the average of
//   the four points (D_0064D354, 16 bytes each), d = car +0x30 - average, the leftwards vector
//   (GetAvgLeftwardsVector 0x147E90), dot(d, left) > 0: the first neighbour, else the second
//   (+0x1D4 = &world +0x2C [id] (8 bytes each), +0x1D8 = its segment).
// The draft loads the pair `field_0[seg]` as a pointer (gp<gp<u16>>): two point indices as an address, a fatal
// "guest pointer outside RAM" when the second index is 0x200 or more, else a silent read of low RAM. Reached by the
// more rivals trial's 8-car race (the player's car pushed onto a closed lane piece at segment 3 of US/C2_V2); not in
// any default race_check scenario or the golden runs, but nothing in the condition depends on the car count.
//
// The fix (port layer, until the decomp's draft takes the one-line fix: `&...->field_0[seg]` cast to the u16 pairs):
// a wrapper that, in exactly that case, resolves the target's lane and segment the retail way, then runs the game's
// function, which finds the segment set and goes on as after that branch. In every other case it only reads memory
// (the drivable test without a call) and calls the game's function unchanged: the default game's runs are untouched
// (race_check shots identical to the build without it). B3NATIVE_LANE_PAIR_FIX=0: off.
#ifndef B3NATIVE_LANE_PAIR_FIX_H
#define B3NATIVE_LANE_PAIR_FIX_H

#include "b3/GtMathPs2.h"
#include "b3host/call.h"
#include "b3host/host.h"
#include "hook_table.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace b3native {

class LanePairFix {
public:
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_LANE_PAIR_FIX");
        return !(e && e[0] == '0' && e[1] == 0);
    }
    bool install(HookTable& hooks) {
        if (!hooks.has_row(kUpdateCurrentSegment)) return false;
        std::string error;
        std::vector<Hook> specs = {{kUpdateCurrentSegment, HookLayer::kPort, &entry, &next_}};
        if (!hooks.install("port:lane_pair_fix", specs, &error)) {
            std::fprintf(stderr, "lane_pair_fix: %s\n", error.c_str());
            return false;
        }
        instance() = this;
        return true;
    }
    std::uint64_t fixed() const { return fixed_; }

private:
    static constexpr b3host::gaddr kUpdateCurrentSegment = 0x0014CA20u;
    static constexpr b3host::gaddr kLaneDriveableStruct = 0x00148C20u;   // LaneIsDriveable(const CB3AILaneStruct*, u16, u16)
    static constexpr b3host::gaddr kLaneDriveableId = 0x00148CA0u;       // LaneIsDriveable(u8, u16, u16)
    static constexpr b3host::gaddr kLeftwards = 0x00147E90u;             // CB3AILane::GetAvgLeftwardsVector(u16, CGtV3d*)
    static constexpr b3host::gaddr kPoints = 0x0064D354u, kLanes = 0x00522660u + 0x2Cu;
    static LanePairFix*& instance() { static LanePairFix* p = nullptr; return p; }

    static void entry(const b3host::CallArgs& c, b3host::CallResult& r) {
        LanePairFix& m = *instance();
        m.resolve(c.gpr[0].w[0]);
        m.next_(c, r);
    }
    void resolve(b3host::gaddr target) {
        using GtMathPs2::CGtV3d;
        if (b3host::load<std::uint16_t>(target + 0x1D8u) != 0xFFFFu) return;
        const b3host::gaddr car = b3host::load<std::uint32_t>(target + 4u);
        const b3host::gaddr lane = b3host::load<std::uint32_t>(car + 0x18B4u);
        if (lane == 0) return;
        const std::uint16_t seg = b3host::load<std::uint16_t>(car + 0x18B8u);
        const b3host::gaddr ls = b3host::load<std::uint32_t>(lane + 4u);
        // the test by memory reads only (no dispatched call: the common case adds no reported guest cost, so the
        // game's timing is untouched): LaneIsDriveable(r, seg, 1) [C B3AILane.cpp] is the segment record's (10 bytes
        // at r +8) +3 & 7 != 5; the lane id form is false for 255, else the same on lane[id] (world +0x2C + 8 id, +4)
        auto driveable = [](b3host::gaddr r, std::uint16_t sg) {
            return (b3host::load<std::uint8_t>(b3host::load<std::uint32_t>(r + 8u) + 10u * sg + 3u) & 7u) != 5u;
        };
        auto driveable_id = [&](std::uint8_t id, std::uint16_t sg) {
            return id != 255u && driveable(b3host::load<std::uint32_t>(kLanes + 8u * id + 4u), sg);
        };
        if (driveable(ls, seg)) return;
        const b3host::gaddr rec = b3host::load<std::uint32_t>(ls + 8u) + 10u * seg;
        const std::uint8_t id0 = b3host::load<std::uint8_t>(rec + 4u), id1 = b3host::load<std::uint8_t>(rec + 5u);
        const std::uint16_t s0 = b3host::load<std::uint16_t>(rec + 6u), s1 = b3host::load<std::uint16_t>(rec + 8u);
        if (!driveable_id(id0, s0) || !driveable_id(id1, s1)) return;   // the game's own branches (right in the draft)
        // the case: the game's three queries as its function makes them (the same calls, the same reported cost)
        (void)b3host::call_guest<bool>(kLaneDriveableStruct, ls, seg, std::uint16_t(1));
        (void)b3host::call_guest<bool>(kLaneDriveableId, id0, s0, std::uint16_t(1));
        (void)b3host::call_guest<bool>(kLaneDriveableId, id1, s1, std::uint16_t(1));
        // the guest stack as the game's frame would leave it: its first local (the car physics' position, copied) and
        // the leftwards vector in the next slot, so later reads of stale guest stack see the same bytes
        const b3host::gaddr body = b3host::load<std::uint32_t>(b3host::load<std::uint32_t>(car + 0x2430u) + 0x204u);
        b3host::GuestLocal<CGtV3d> pos_slot(b3host::copy_init, *static_cast<const CGtV3d*>(b3host::host_pointer(body + 0x30u, 16)));
        const b3host::gaddr q = b3host::load<std::uint32_t>(ls) + 4u * seg;
        const auto* pts = static_cast<const CGtV3d*>(b3host::host_pointer(b3host::load<std::uint32_t>(kPoints), 16));
        const CGtV3d avg = (pts[b3host::load<std::uint16_t>(q)] + pts[b3host::load<std::uint16_t>(q + 2u)] +
                            pts[b3host::load<std::uint16_t>(q + 4u)] + pts[b3host::load<std::uint16_t>(q + 6u)]) * 0.25f;
        b3host::GuestLocal<CGtV3d> left;
        b3host::call_guest<void>(kLeftwards, lane, seg, left.addr());
        const CGtV3d pos = *static_cast<const CGtV3d*>(b3host::host_pointer(car + 0x30u, 16));
        const bool first = GtMathPs2::DotProduct(pos - avg, *left) > 0.0f;
        b3host::store<std::uint32_t>(target + 0x1D4u, kLanes + 8u * (first ? id0 : id1));
        b3host::store<std::uint16_t>(target + 0x1D8u, first ? s0 : s1);
        if (fixed_++ == 0) {
            std::printf("lane_pair_fix: CB3AITarget::UpdateCurrentSegment's lane pair branch, the retail way (car %d, segment %u)\n",
                        b3host::load<std::int8_t>(car + 0x19ACu), unsigned(seg));
            std::fflush(stdout);
        }
    }

    HookEntry next_ = nullptr;
    std::uint64_t fixed_ = 0;
};

}  // namespace b3native

#endif
