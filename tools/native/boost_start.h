// boost_start.h -- booststart (2026-11-22): the game's own BOOST START on a standing start (raceopts' START Standing).
//
// What the game has [C the decomp]: CB3RaceCarPhysics::GetDrivingControls 0x1C0F00 (draft), while a human car is held
// at the start (its race car's +0x178C not 1 yet), arms a boost start [Revenge's mControlStuff.mbBoostStart]:
// - +0x1528 set when the brake (+0x1404) is past half, cleared when the accelerator (+0x1400) drops below half;
// - between 1.0 and 0.5 s before the start (race car +0x1340, against the game clock D_00522680) +0x152A = +0x1528 and
//   +0x1529 -- the wheels spinning (CB3RaceCarPhysics::UpdateWheelStates 0x1BF170 sets it when a wheel's spin x radius
//   passes 2 m/s);
// - in the last 0.5 s the accelerator lifted below 0.2 (+0x152B), then floored past 0.8: CB3Score::HandleBoostStart
//   0x156E20 (the boost burn and the BOOST START award) and +0x1446 (full throttle for the first 3 s).
// Retail never starts standing (func_00135170, the stage's rolling start, answers 1 for every race), and the car held
// at the start has its wheels still (no spin is simulated before the start: measured, wheel spin +0x87C first changes
// at the release), so +0x1529 never comes and the boost start can never be armed.
// Ours: on a standing start, before each GetDrivingControls of a human car still held, +0x1529 is the accelerator past
// half (the revving the wheel spin stands for). The rest is the game's: brake and accelerator held through the
// countdown, the accelerator lifted in the last half second, then floored. Nothing changes without START Standing
// (rolling starts never run that branch).
//
// The award's icon (boosticon): the HUD's primary message (CB3HUDPrimaryMessage::UpdateMessage 0x3D8280) finds the event
// in _kaEventMap 0x4CA950 ({u8 event, u8 kind, u16 string}, 206 entries) and draws it with func_003D56D0, its 5th
// argument the message type (D_004CA6F0's entry: colours, layout, +8 the icon texture), chosen by the kind: BOOST START! (event 121, string 2135) is kind 1 -- icon 0, the generic event
// icon LAP LEADER, PERFECT LAP, BURNING LAP and the positions share -- the game's own (no event-specific icon). The
// icons by kind: 0 events (kind 1, 5-8), 1 slams (9), 2 drift / air / oncoming / near miss / GREAT BOOSTING (13), 3-5
// gold / silver / bronze (12, 11, 10), 6 takedowns (4), 7 crashed (3), 8 INSTANT BOOST (20), 9-11 CASH BONUS (15-17),
// 12-13 CASH MULTIPLIER (18, 19), 14 CRASHBREAKER (21), 15 HEARTBREAKER (22), 16 STEALER (23) -- and the textures:
// type 0 and 8 texture 0, 1 and 7 texture 1, 2 texture 2, 6 texture 3, 3-5 4-6, 9-13 8-12, 14-16 13-15 (7 unused).
// On a standing start BOOST START! is drawn with texture 7, the unused lightning-bolt sign (the boost; texture 2 is a plain
// star, 13 CRASHBREAKER's explosion), its type's layout and colours kept. Drawn only: the event, its kind (which also picks its sound), the score and the boost are
// the game's. B3NATIVE_BOOST_ICON=<0..15> (checks) draws another texture.
#ifndef B3NATIVE_BOOST_START_H
#define B3NATIVE_BOOST_START_H

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

constexpr b3host::gaddr kGetDrivingControls = 0x001C0F00u;   // [C] CB3RaceCarPhysics::GetDrivingControls
// The physics' fields [C GetDrivingControls; party_modes.h]: the race car (+0x13F4; the player's car also at +0x1580),
// the car's kind (+0x215: 1 the player's car, 2 an AI race car, 3 a replay's), the accelerator (+0x1400), the wheel
// spin flag (+0x1529); the race car's racing state (+0x178C: 0 held at the start, 1 from the start on, through the
// finish -- logged over a whole race) and AI-driven (+0x27C8: the attract demo, the AI DRIVER button)
constexpr std::uint32_t kPhysRaceCar = 0x13F4u, kPhysPlayerCar = 0x1580u, kPhysKind = 0x215u, kPhysAccel = 0x1400u,
                        kPhysSpin = 0x1529u;
constexpr std::uint32_t kCarRacing = 0x178Cu, kCarAiDriven = 0x27C8u;
constexpr b3host::gaddr kHudMessageDraw = 0x003D56D0u;   // [C] func_003D56D0(..., text, icon, ...): the primary message
constexpr b3host::gaddr kBoostMsgGetString = 0x00271A00u, kBoostMsgStrings = 0x0051BA44u;   // [C] func_00271A00(&D_0051BA44, id): the text
constexpr std::uint32_t kBoostStartString = 2135u;      // "BOOST START!" (_kaEventMap: event 121, kind 1)
// The message type table D_004CA6F0 (0x10 each: +8 the icon texture, mapIconTextures__15CB3HUDComponent 0x667280 [16]):
// type 0 (events, BOOST START!) and type 8 (INSTANT BOOST) both draw texture 0, the car-collision sign; texture 7, a
// lightning bolt in the same yellow diamond, is on the disc but no message type nor any other HUD code draws it (the
// medals read 4-6 [func_00394870], two icons 1 [func_003B90E0, func_003CAAB0])
constexpr b3host::gaddr kMessageTypes = 0x004CA6F0u;
constexpr std::uint32_t kGenericIcon = 0u, kBoostTexture = 7u;
// The texture BOOST START! is drawn with (pure: tests/test_boost_start.cpp): on a standing start the boost one; -1 the game's
inline int boost_start_texture(bool standing, bool boost_start_text, std::uint32_t type, int want = int(kBoostTexture)) {
    return standing && boost_start_text && type == kGenericIcon && want >= 0 && want < 16 ? want : -1;
}
constexpr int kKindPlayer = 1, kHeld = 0;

// The wheel spin flag a held human car gets (pure: tests/test_boost_start.cpp); -1 leaves it alone: only a player's car
// (kind 1) a player drives (not AI-driven), held at the start (racing 0), on a standing start
inline int boost_start_spin(bool standing, int kind, bool ai_driven, int racing, float accel) {
    if (!standing || kind != kKindPlayer || ai_driven || racing != kHeld) return -1;
    return accel > 0.5f ? 1 : 0;
}

class BoostStart {
public:
    void set_standing_query(std::function<bool()> f) { standing_ = std::move(f); }
    bool install(HookTable& hooks) {
        if (!hooks.has_row(kGetDrivingControls)) return false;
        std::string error;
        std::vector<Hook> specs = {{kGetDrivingControls, HookLayer::kPort, &entry, &next_}};
        if (hooks.has_row(kHudMessageDraw)) specs.push_back({kHudMessageDraw, HookLayer::kPort, &message, &message_next_});
        if (!hooks.install("port:boost_start", specs, &error)) {
            std::fprintf(stderr, "boost_start: %s\n", error.c_str());
            return false;
        }
        instance() = this;
        return true;
    }

private:
    static BoostStart*& instance() { static BoostStart* p = nullptr; return p; }
    static void entry(const b3host::CallArgs& a, b3host::CallResult& r) {
        BoostStart* p = instance();
        const b3host::gaddr phys = a.gpr[0].w[0] & 0x01FFFFFFu;
        if (phys && p->standing_ && p->standing_()) {
            const b3host::gaddr car = b3host::load<std::uint32_t>(phys + kPhysRaceCar) & 0x01FFFFFFu;
            if (car) {
                const int kind = b3host::load<std::int8_t>(phys + kPhysKind);
                const b3host::gaddr pcar = kind == kKindPlayer ? b3host::load<std::uint32_t>(phys + kPhysPlayerCar) & 0x01FFFFFFu : 0u;
                const bool ai = pcar && b3host::load<std::uint8_t>(pcar + kCarAiDriven) != 0;
                const int racing = b3host::load<std::int32_t>(car + kCarRacing);
                const int spin = boost_start_spin(true, kind, ai, racing, b3host::load<float>(phys + kPhysAccel));
                if (spin >= 0) b3host::store<std::uint8_t>(phys + kPhysSpin, std::uint8_t(spin));
                p->note(phys, kind, ai, racing, spin >= 0);
            }
        }
        p->next_(a, r);
    }
    // the HUD's primary message: BOOST START!'s icon on a standing start (drawn only)
    static void message(const b3host::CallArgs& a, b3host::CallResult& r) {
        BoostStart* p = instance();
        const std::uint32_t icon = a.gpr[4].w[0];
        if (icon == kGenericIcon && p->standing_ && p->standing_()) {
            const std::uint32_t text = a.gpr[3].w[0] & 0x01FFFFFFu;
            const std::uint32_t want = b3host::call_guest<std::uint32_t>(kBoostMsgGetString, kBoostMsgStrings, kBoostStartString) & 0x01FFFFFFu;
            static const int pick = [] { const char* e = std::getenv("B3NATIVE_BOOST_ICON"); return e && *e ? std::atoi(e) : int(kBoostTexture); }();
            const int tex = boost_start_texture(true, text != 0 && text == want, icon, pick);
            if (tex >= 0) {
                // only the texture: the type-0 entry's for this one draw (its layout and colours stay), put back after
                const b3host::gaddr slot = kMessageTypes + 16u * kGenericIcon + 8u;
                const std::uint32_t old = b3host::load<std::uint32_t>(slot);
                b3host::store<std::uint32_t>(slot, std::uint32_t(tex));
                if (!p->icon_told_) { p->icon_told_ = true; std::printf("boost_start: BOOST START! drawn with the boost icon (texture %d)\n", tex); std::fflush(stdout); }
                p->message_next_(a, r);
                b3host::store<std::uint32_t>(slot, old);
                return;
            }
        }
        p->message_next_(a, r);
    }
    // once per car and race state: which cars a standing start arms (the log: every car at the start)
    void note(b3host::gaddr phys, int kind, bool ai, int racing, bool armed) {
        const std::uint64_t key = std::uint64_t(phys) << 8 | std::uint64_t(racing & 0xFF);
        for (const std::uint64_t k : noted_) if (k == key) return;
        if (noted_.size() >= 64) return;
        noted_.push_back(key);
        std::printf("boost_start: car physics %08X kind %d%s racing %d: %s\n", phys, kind, ai ? " (AI-driven)" : "", racing,
                    armed ? "its wheel spin from the accelerator" : "left alone");
        std::fflush(stdout);
    }
    HookEntry next_ = nullptr, message_next_ = nullptr;
    bool icon_told_ = false;
    std::function<bool()> standing_;
    std::vector<std::uint64_t> noted_;
};

}  // namespace b3native

#endif
