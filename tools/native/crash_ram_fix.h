// crash_ram_fix.h -- CB3CrashActionDirector::UpdateTraffic (0x1B1240): the ram's flags set as retail sets them.
//
// UpdateTraffic is Crash mode's traffic AI, called each frame for each crashed traffic vehicle (a CB3TrafficPhysics
// that is not a trailer) with its CB3CADTrafficData (a1: +0 the time all its wheels have been down, +4 the vehicle it
// is told to ram, +8 the roll kick given, +9 the ram given) [C decomp src/nonmatching/UpdateTraffic__22CB3CrashAction
// Director...cpp, draftAE]:
// - told to ram, it steers at its target; within 15 m (or more than 75 degrees off) it is kicked by FireAirRam
//   (0x1CC520, flag 0x40): a cab towing a trailer kicks the trailer and sets +9 and +8; a vehicle on its own kicks
//   itself and sets +9 only [C retail 0x1B1984..0x1B1994: `sb v1,9(s1)` in the branch's delay slot to 0x1B182C, past
//   the linked branch's `sb v1,8(s1)` at 0x1B1828];
// - while +8 is clear, a wreck whose wheels have all been down for more than 0.5 s and which slides sideways faster than
//   5 m/s, at 60..120 degrees from its heading, gets a roll kick (FireAirRam flag 4 or 8) and +8 is set: buses, lorries
//   and the other vehicles that rammed the player tip over instead of sliding to a stop.
// The generated function (the decomp's draft) sets +8 in both ram branches, so in the port a vehicle without a trailer
// that has rammed never gets its roll kick (B3NATIVE_DIFF: 2 of 2201 calls on a Crash junction, +8 1 against retail's
// 0). Seen as heavy traffic sliding "stiff" in Crash mode.
//
// The fix (port layer, until the decomp's draft drops the line): a wrapper that, after a call in which +9 went from 0
// to 1 for a vehicle with no linked vehicle (+0x2424), puts +8 back to its value before the call. Every other call is
// untouched. B3NATIVE_CRASH_RAM_FIX=0: off. A replay records "crash_ram_fix" 1; one without the entry (every replay made
// before the fix, 1.3.0's too) plays with the draft's flags, as it was recorded (set_on_query); while a replay plays
// the entry decides, whatever B3NATIVE_CRASH_RAM_FIX says.
#ifndef B3NATIVE_CRASH_RAM_FIX_H
#define B3NATIVE_CRASH_RAM_FIX_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "hook_table.h"
#include "platform.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace b3native {

class CrashRamFix {
public:
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_CRASH_RAM_FIX");
        return !(e && e[0] == '0' && e[1] == 0);
    }
    bool install(HookTable& hooks) {
        if (!hooks.has_row(kUpdateTraffic)) return false;
        std::string error;
        std::vector<Hook> specs = {{kUpdateTraffic, HookLayer::kPort, &entry, &next_}};
        if (!hooks.install("port:crash_ram_fix", specs, &error)) {
            std::fprintf(stderr, "crash_ram_fix: %s\n", error.c_str());
            return false;
        }
        instance() = this;
        return true;
    }
    std::uint64_t fixed() const { return fixed_; }
    // whether the fix applies now (native_play: a replay's "crash_ram_fix" entry while one plays; else on)
    void set_on_query(std::function<bool()> f) { on_ = std::move(f); }

private:
    static constexpr b3host::gaddr kUpdateTraffic = 0x001B1240u;
    static constexpr std::uint32_t kRolled = 0x8u, kRammed = 0x9u, kLinked = 0x2424u;
    static CrashRamFix*& instance() { static CrashRamFix* p = nullptr; return p; }
    static b3host::gaddr ga(std::uint32_t v) { return v & 0x01FFFFFFu; }

    static void entry(const b3host::CallArgs& c, b3host::CallResult& r) {
        CrashRamFix* p = instance();
        const b3host::gaddr cad = ga(c.gpr[1].w[0]), phys = ga(c.gpr[2].w[0]);
        if (!cad || !phys || (p->on_ && !p->on_())) { p->next_(c, r); return; }
        const std::uint8_t rolled = b3host::load<std::uint8_t>(cad + kRolled);
        const bool rammed = b3host::load<std::uint8_t>(cad + kRammed) != 0;
        const bool linked = b3host::load<std::uint32_t>(phys + kLinked) != 0;
        p->next_(c, r);
        if (rammed || linked || b3host::load<std::uint8_t>(cad + kRammed) == 0) return;
        if (b3host::load<std::uint8_t>(cad + kRolled) == rolled) return;
        b3host::store<std::uint8_t>(cad + kRolled, rolled);
        ++p->fixed_;
        std::printf("crash_ram_fix: VBlank %llu: vehicle %08X (%.0f kg) rammed on its own: its roll kick left armed as retail (%llu so far)\n",
                    static_cast<unsigned long long>(platform::vblank_count()), phys, double(b3host::load<float>(phys + 0x1F0u)),
                    static_cast<unsigned long long>(p->fixed_));
    }

    HookEntry next_ = nullptr;
    std::function<bool()> on_;
    std::uint64_t fixed_ = 0;
};

}  // namespace b3native

#endif
