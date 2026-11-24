// settings_back.h -- settingsback (2026-11-22): Back on the game's DRIVER DETAILS > SETTINGS pages keeps the changes.
//
// What the game does [C the decomp; matched functions]: the four pages GAMEPLAY (func_0045BB10 / Update func_0045B920),
// AUDIO (func_003F2FE0), VIDEO (func_003F3E60) and CONTROLLERS (func_003F3670) run under one menu flow handler,
// func_0045C140(self, action, p1, p2, p3):
// - action 3 (a page opens): it copies the settings bytes (D_004F5040 and D_004F506C..D_004F507F: TAKEDOWN CAMERA,
//   CRASH MODE IMPACT TIME, the volumes, the screen position, the controller's settings) into itself;
// - action 5 with p2 1 (the page left with confirm): its scene, nothing else -- the edited bytes stay;
// - action 5 with p2 3 (left with Back) or 0x4A (the event the page's Back raises first): its scene (3), then the
//   saved bytes put back (a cancel), then falls into 0x4B: the settings applied (func_0021B4F0 / func_0021B5E0);
// - action 5 with p2 0x4B: the settings applied (each change raises it as it is made).
// So on the PS2 a change left with Back is discarded. With SETTINGS BACK BUTTON Keep changes (the port's default:
// PC SETTINGS > GAME > INTERFACE) those two calls are rewritten: 0x4A as 0x4B (applied, not put back) and 3 as 1 then
// 0x4B (the same scene, applied, not put back). Menus only: nothing a race reads changes but the settings the player
// chose. Cancel (the game's) leaves every call alone. B3NATIVE_SETTINGS_BACK=0 / 1 overrides the setting (the golden
// runs pin 0: their tours leave pages with Back).
#ifndef B3NATIVE_SETTINGS_BACK_H
#define B3NATIVE_SETTINGS_BACK_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "hook_table.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace b3native {

constexpr b3host::gaddr kSettingsFlow = 0x0045C140u;   // [C] func_0045C140, the SETTINGS pages' flow handler
constexpr std::uint32_t kFlowLeave = 5, kLeaveConfirm = 1, kLeaveBack = 3, kEventCancel = 0x4A, kEventApply = 0x4B;

// The calls the handler gets for one call of the game's (pure: tests/test_settings_back.cpp): `keep` the option,
// `action` / `p2` the call's. Cancel, or any other call: the call itself. Keep: 0x4A -> 0x4B; 3 -> 1, 0x4B.
inline std::vector<std::uint32_t> settings_back_calls(bool keep, std::uint32_t action, std::uint32_t p2) {
    if (!keep || action != kFlowLeave) return {p2};
    if (p2 == kEventCancel) return {kEventApply};
    if (p2 == kLeaveBack) return {kLeaveConfirm, kEventApply};
    return {p2};
}
// A replay's "settings_back" entry ("1" keep); a recording without it: the game's cancel
inline bool settings_back_entry(const char* v) { return v && v[0] == '1' && !v[1]; }
// $B3NATIVE_SETTINGS_BACK: -1 unset, else 0 cancel / 1 keep
inline int settings_back_env() {
    const char* e = std::getenv("B3NATIVE_SETTINGS_BACK");
    if (!e || !(e[0] == '0' || e[0] == '1') || e[1]) return -1;
    return e[0] - '0';
}

class SettingsBack {
public:
    void set_keep_query(std::function<bool()> f) { keep_ = std::move(f); }
    bool install(HookTable& hooks) {
        if (!hooks.has_row(kSettingsFlow)) {
            std::fprintf(stderr, "settings_back: %08X is not native: Back cancels as the game does\n", kSettingsFlow);
            return false;
        }
        std::string error;
        if (!hooks.install("port:settings_back", {{kSettingsFlow, HookLayer::kPort, &entry, &next_}}, &error)) {
            std::fprintf(stderr, "settings_back: %s\n", error.c_str());
            return false;
        }
        instance() = this;
        return true;
    }
    std::uint64_t kept() const { return kept_; }

private:
    static SettingsBack*& instance() { static SettingsBack* p = nullptr; return p; }
    static void entry(const b3host::CallArgs& a, b3host::CallResult& r) {
        SettingsBack* p = instance();
        const std::uint32_t action = a.gpr[1].w[0], p2 = a.gpr[3].w[0];
        const std::vector<std::uint32_t> calls = settings_back_calls(p->keep_ && p->keep_(), action, p2);
        if (calls.size() == 1 && calls[0] == p2) { p->next_(a, r); return; }
        for (const std::uint32_t c : calls) {
            b3host::CallArgs b = a;
            b.gpr[3] = {};
            b.gpr[3].w[0] = c;
            p->next_(b, r);
        }
        if (p2 == kLeaveBack) {
            ++p->kept_;
            std::printf("settings_back: a SETTINGS page left with Back: its changes kept (%llu so far)\n",
                        static_cast<unsigned long long>(p->kept_));
            std::fflush(stdout);
        }
    }
    HookEntry next_ = nullptr;
    std::function<bool()> keep_;
    std::uint64_t kept_ = 0;
};

}  // namespace b3native

#endif
