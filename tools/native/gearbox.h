// gearbox.h -- gearbox (2026-11-22):
// TRANSMISSION Manual, the game's own manual gearbox, for a SINGLE EVENT race (raceopts' row, race_choice.h).
//
// What the game has [C the decomp; retail]:
// - CB3RaceCarPhysics::GetDrivingControls 0x1C0F00 (the player's car each physics tick): with the transmission's manual
//   flag (physics +0x14B8 = CB3Transmission +0x70) set, bit 1 of the driving buttons (+0x13FC) shifts up (latch +0x14C0;
//   up to the top gear +0x14CC = transmission +0x84) and bit 2 shifts down (latch +0x14C4; not below 1st), each with the
//   0.35 s clutch (+0x14A4 / +0x14A0); first and reverse are taken from neutral by the throttle and the brake as always.
//   The transmission's Update func_001C6F90 then shifts nothing by itself, uses the manual rev limit (D_004E0D88) and
//   drops to neutral at low revs off the throttle. CB3Transmission::Prepare 0x1C7880 never sets +0x70: retail never
//   enables it.
// - The bits come from the controller mapping in CB3PlayerCarPhysics::GetDrivingControls 0x1BD020: bit 1 from
//   func_0012F610 (the DualShock's R2, control 13), bit 2 from func_0012F4E0 (L2, control 11, just pressed); the game's
//   replay records +0x13FC (CB3ReplaySystem::NextFrame, in the same function).
// What the port does, only while the race on now has TRANSMISSION Manual (RaceOptions::manual_gears):
// - CB3Transmission::Prepare 0x1C7880 (a1 the car's physics; the player's a CB3PlayerCarPhysics, vtable 0x4DDF00): its
//   +0x14B8 set as the race is prepared, before the start's countdown (the transmission's Update runs from then on, and
//   without it shifts by itself on the line); cleared again on a later race that is not manual (the physics object is
//   the race's next car too); 0x1BD020 (the player's controls) checks it each tick as well;
// - the SHIFT UP / SHIFT DOWN port actions (input_bindings.h; U / Y, the paddles), queued by native_play on the VBlank
//   grid (a replay: its "shift" entries) and taken only while the player's controls (0x1BD020) ran since the last VBlank
//   (a press during a pause, the countdown or a results page is dropped, as is one not taken by the next VBlank); the
//   game's gear readers (0x12F610 / 0x12F4E0) answer from them alone -- 1.0 for one queued press, else 0 -- when called
//   from the port's own 0x1BD020 wrapper for the player's controller mapping, so the game's latch, its clutch and its
//   replay take them, and the game's own R2 / L2 (the Xbox layout's B / View: the Crashbreaker, the music skip) do not
//   shift; every other call (the crash's controls 0x1BB6E0: the Crashbreaker) is the game's;
// - the gear (+0x14C8: -1 reverse, 0 neutral, 1..) for the HUD's indicator (gear()).
// B3NATIVE_GEARBOX=0: none of it (the row stays raceopts').
#ifndef B3NATIVE_GEARBOX_H
#define B3NATIVE_GEARBOX_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "hook_table.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace b3native {

class Gearbox {
public:
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_GEARBOX");
        return !(e && e[0] == '0' && e[1] == 0);
    }
    bool install(HookTable& hooks);
    // the race on now has TRANSMISSION Manual (raceopts' choice)
    void set_manual_query(std::function<bool()> f) { manual_query_ = std::move(f); }
    bool manual() const { return manual_query_ && manual_query_(); }
    // SHIFT UP / SHIFT DOWN presses for the game (native_play, once per VBlank; dropped unless manual)
    void press(int up, int down) {
        if (!manual() || !controls_seen_) return;   // (nothing reads them now: dropped)
        pending_up_ += up;
        pending_down_ += down;
    }
    // native_play, after press() each VBlank: presses not taken while the controls were not read are dropped
    void end_vblank() {
        if (!controls_seen_) pending_up_ = pending_down_ = 0;
        controls_seen_ = false;
    }
    // the player's gear while manual (-1 reverse, 0 neutral, 1..), else -2
    int gear() const;
    std::uint64_t shifts() const { return shifts_; }
    // the player's manual flag now (-1: no player car yet)
    int flag_now() const { return player_ ? b3host::load<std::int32_t>(player_ + 0x14B8u) : -1; }

private:
    static Gearbox*& instance() { static Gearbox* p = nullptr; return p; }
    static void controls(const b3host::CallArgs& a, b3host::CallResult& r);
    static void prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    void flag(b3host::gaddr physics, bool manual, const char* where);
    static void gear_up(const b3host::CallArgs& a, b3host::CallResult& r);
    static void gear_down(const b3host::CallArgs& a, b3host::CallResult& r);
    static void pulse(void (*next)(const b3host::CallArgs&, b3host::CallResult&), int& pending, const char* what,
                      const b3host::CallArgs& a, b3host::CallResult& r);

    void (*controls_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*prepare_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*gear_up_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*gear_down_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    std::function<bool()> manual_query_;
    int pending_up_ = 0, pending_down_ = 0;
    std::vector<b3host::gaddr> flagged_;   // the physics objects whose +0x14B8 the port set
    b3host::gaddr player_ = 0;             // the player's physics (the last 0x1BD020 while manual)
    std::uint64_t shifts_ = 0;
    bool logged_ = false;
    bool in_controls_ = false, controls_seen_ = false;   // inside the port's 0x1BD020 wrapper; the controls ran this VBlank
    b3host::gaddr mapping_ = 0;                          // the player's controller mapping (the wrapper's)
};

namespace gearbox_detail {
constexpr b3host::gaddr kPlayerControls = 0x001BD020u;   // CB3PlayerCarPhysics::GetDrivingControls
constexpr b3host::gaddr kGearUp = 0x0012F610u, kGearDown = 0x0012F4E0u;   // the controller mapping's gear buttons
constexpr b3host::gaddr kTransPrepare = 0x001C7880u;     // CB3Transmission::Prepare(transmission, physics)
constexpr b3host::gaddr kPlayerPhysicsVtable = 0x004DDF00u;   // __vt__19CB3PlayerCarPhysics
constexpr std::uint32_t kManual = 0x14B8u, kGear = 0x14C8u;   // CB3RaceCarPhysics: the transmission (+0x1448) +0x70, +0x80
}  // namespace gearbox_detail

inline bool Gearbox::install(HookTable& hooks) {
    using namespace gearbox_detail;
    for (b3host::gaddr a : {kPlayerControls, kGearUp, kGearDown, kTransPrepare})
        if (!hooks.has_row(a)) {
            std::fprintf(stderr, "gearbox: %08X is not native: no manual gearbox\n", a);
            return false;
        }
    std::string error;
    if (!hooks.install("port:gearbox", {{kPlayerControls, HookLayer::kPort, &controls, &controls_},
                                        {kGearUp, HookLayer::kPort, &gear_up, &gear_up_},
                                        {kGearDown, HookLayer::kPort, &gear_down, &gear_down_},
                                        {kTransPrepare, HookLayer::kPort, &prepare, &prepare_}}, &error)) {
        std::fprintf(stderr, "gearbox: %s: no manual gearbox\n", error.c_str());
        return false;
    }
    instance() = this;
    std::printf("gearbox: TRANSMISSION Manual available (SELECT OPTIONS; B3NATIVE_GEARBOX=0: none)\n");
    return true;
}

inline int Gearbox::gear() const {
    if (!player_ || !manual()) return -2;
    return b3host::load<std::int32_t>(player_ + gearbox_detail::kGear);
}

// The manual flag of a car's physics: set for the player's in a manual race, cleared where the port set it otherwise
inline void Gearbox::flag(b3host::gaddr self, bool manual, const char* where) {
    using namespace gearbox_detail;
    if (manual) {
        if (b3host::load<std::int32_t>(self + kManual) == 0) {
            b3host::store<std::int32_t>(self + kManual, 1);
            bool known = false;
            for (b3host::gaddr f : flagged_) known = known || f == self;
            if (!known) flagged_.push_back(self);
            std::printf("gearbox: TRANSMISSION Manual: the player's car %08X in manual (%s, gear %d)\n", self, where,
                        b3host::load<std::int32_t>(self + kGear));
            std::fflush(stdout);
        }
        player_ = self;
    } else {
        for (b3host::gaddr f : flagged_)
            if (f == self && b3host::load<std::int32_t>(self + kManual) != 0) b3host::store<std::int32_t>(self + kManual, 0);
        pending_up_ = pending_down_ = 0;
    }
}

// CB3Transmission::Prepare (a0 the transmission, a1 the car's physics): the game's, then the player's manual flag
inline void Gearbox::prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    Gearbox* p = instance();
    p->prepare_(a, r);
    const b3host::gaddr phys = a.gpr[1].w[0] & 0x01FFFFFFu;
    if (!phys) return;
    // the player's car: a CB3PlayerCarPhysics (its vtable at +0; the AI's are CB3AICarPhysics, 0x4DDF70)
    if ((b3host::load<std::uint32_t>(phys) & 0x01FFFFFFu) == gearbox_detail::kPlayerPhysicsVtable) {
        const bool m = p->manual();
        std::printf("gearbox: the player's transmission prepared (%08X): the race's options %s, TRANSMISSION %s\n", phys,
                    p->manual_query_ ? "read" : "none", m ? "Manual" : "Automatic");
        std::fflush(stdout);
        p->flag(phys, m, "the race's preparation");
    }
}

// CB3PlayerCarPhysics::GetDrivingControls (a0 the player's physics): the manual flag, then the game's controls
inline void Gearbox::controls(const b3host::CallArgs& a, b3host::CallResult& r) {
    Gearbox* p = instance();
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
    if (!self || !p->manual()) {
        p->controls_(a, r);
        return;
    }
    p->flag(self, true, "the player's controls");   // (cleared only by a preparation)
    // the player's controller mapping, as the game reads it: *(car +0x2460), the car the physics' +0x1580 [C 0x1BD020]
    const b3host::gaddr car = b3host::load<std::uint32_t>(self + 0x1580u) & 0x01FFFFFFu;
    p->mapping_ = car ? b3host::load<std::uint32_t>(b3host::load<std::uint32_t>(car + 0x2460u) & 0x01FFFFFFu) & 0x01FFFFFFu : 0;
    p->controls_seen_ = true;
    p->in_controls_ = true;
    p->controls_(a, r);
    p->in_controls_ = false;
}

inline void Gearbox::pulse(void (*next)(const b3host::CallArgs&, b3host::CallResult&), int& pending, const char* what,
                           const b3host::CallArgs& a, b3host::CallResult& r) {
    Gearbox* p = instance();
    next(a, r);
    // only the player's controls in a manual race, from the port's wrapper: the port's presses alone
    if (!p->in_controls_ || !p->mapping_ || (a.gpr[0].w[0] & 0x01FFFFFFu) != p->mapping_ || !p->manual()) return;
    const bool press = pending > 0;
    const float v = press ? 1.0f : 0.0f;
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    r.f0 = bits;
    if (!press) return;
    --pending;
    ++p->shifts_;
    if (p->shifts_ <= 20) {
        std::printf("gearbox: %s (gear %d before the game's shift)\n", what, p->player_ ? b3host::load<std::int32_t>(p->player_ + gearbox_detail::kGear) : 0);
        std::fflush(stdout);
    }
}
// func_0012F610 / func_0012F4E0 (the controller mapping's gear up / down): one queued press answers one call with 1.0
inline void Gearbox::gear_up(const b3host::CallArgs& a, b3host::CallResult& r) { pulse(instance()->gear_up_, instance()->pending_up_, "shift up", a, r); }
inline void Gearbox::gear_down(const b3host::CallArgs& a, b3host::CallResult& r) { pulse(instance()->gear_down_, instance()->pending_down_, "shift down", a, r); }

}  // namespace b3native

#endif
