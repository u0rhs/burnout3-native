// debug_health.h -- a TEST-ONLY poke for native_play (mac5, 2026-11-09; the road rage "DAMAGE CRITICAL!" report):
// B3NATIVE_DEBUG_HEALTH=<vblank>:<value> sets the player's CB3Score health (+0x5F4; 1.0 full, the HUD warns below 0.1)
// once, at the first CB3HUDScore::UpdateSmallMessage (0x1A2700) call at or after that VBlank: the HUD score's +0xCC
// is its CB3Score [C decomp src/nonmatching/UpdateSmallMessage__11CB3HUDScoreFf.cpp: field_CC->field_5F4]. It changes
// the game's state, so it exists only for checks; nothing sets it by default.
#ifndef B3NATIVE_DEBUG_HEALTH_H
#define B3NATIVE_DEBUG_HEALTH_H

#include "hook_table.h"
#include "platform.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace b3native {

class DebugHealth {
public:
    static bool requested() { const char* e = std::getenv("B3NATIVE_DEBUG_HEALTH"); return e && *e; }
    bool install(HookTable& hooks) {
        const char* e = std::getenv("B3NATIVE_DEBUG_HEALTH");
        if (!e || std::sscanf(e, "%llu:%f", &at_, &value_) != 2) return false;
        if (!hooks.install("port:debug_health", {{kUpdate, HookLayer::kPort, &update, &update_}})) return false;
        instance() = this;
        std::printf("debug_health: TEST ONLY -- the player's health set to %.3f at VBlank %llu\n", double(value_), at_);
        return true;
    }

private:
    static constexpr b3host::gaddr kUpdate = 0x001A2700u;
    static DebugHealth*& instance() { static DebugHealth* p = nullptr; return p; }
    static void update(const b3host::CallArgs& a, b3host::CallResult& r) {
        DebugHealth* p = instance();
        if (!p->done_ && platform::vblank_count() >= p->at_) {
            const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
            const b3host::gaddr score = b3host::load<std::uint32_t>(self + 0xCCu) & 0x01FFFFFFu;
            if (score) {
                std::uint32_t bits;
                std::memcpy(&bits, &p->value_, 4);
                const std::uint32_t old = b3host::load<std::uint32_t>(score + 0x5F4u);
                float before;
                std::memcpy(&before, &old, 4);
                b3host::store<std::uint32_t>(score + 0x5F4u, bits);
                std::printf("debug_health: VBlank %llu: score %08X health %.3f -> %.3f\n",
                            static_cast<unsigned long long>(platform::vblank_count()), score, double(before), double(p->value_));
                p->done_ = true;
            }
        }
        p->update_(a, r);
    }
    void (*update_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    unsigned long long at_ = 0;
    float value_ = 1.0f;
    bool done_ = false;
};

// debugging: B3NATIVE_CALL_COUNT=<addr>:<from vblank>:<to vblank> logs each call of that native function in the window
// (VBlank, a0, a1): which objects a render callback draws
class CallCount {
public:
    bool install(HookTable& hooks) {
        const char* e = std::getenv("B3NATIVE_CALL_COUNT");
        unsigned addr = 0;
        if (!e || std::sscanf(e, "%x:%llu:%llu", &addr, &from_, &to_) != 3) return false;
        addr_ = addr;
        if (!hooks.install("port:call_count", {{addr_, HookLayer::kPort, &call, &fn_}})) return false;
        instance() = this;
        std::printf("call_count: %08X logged from VBlank %llu to %llu\n", addr_, from_, to_);
        return true;
    }

private:
    static CallCount*& instance() { static CallCount* p = nullptr; return p; }
    static void call(const b3host::CallArgs& a, b3host::CallResult& r) {
        CallCount* p = instance();
        const unsigned long long v = platform::vblank_count();
        if (v >= p->from_ && v <= p->to_)
            std::printf("call_count: VBlank %llu %08X a0 %08X a1 %08X\n", v, p->addr_, a.gpr[0].w[0], a.gpr[1].w[0]);
        p->fn_(a, r);
    }
    void (*fn_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    b3host::gaddr addr_ = 0;
    unsigned long long from_ = 0, to_ = 0;
};

}  // namespace b3native

#endif
