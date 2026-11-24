// party_modes.h -- party1 (2026-11-21): GAME > PARTY MODES, off by default (hooks on the hook table; no game code is
// changed). The races they apply to, the latch, the replay entry and the records kept apart are GAMEPLAY's
// (game_option_rules.h: the word's bits 4..11; game_options.h: the latch, records_guard.h, the profile).
//
// The game's parts they use [C the generated units; decomp asm units 0x1AC470, 0x1C0F00, 0x1CC900, 0x161B50, 0x1BD020]:
// - CB3RaceCarPhysics +0x215 is the vehicle's kind (1 the player's car, 2 an AI race car, 3 a replay's), +0x210 its
//   crashing flag, +0x204 its matrix (the position at +0x30), +0x1580 its race car (+0x27C0 the player's index, +0x27C8
//   AI-driven, +0x19AC the car's slot).
// - CRAZY AI PHYSICS: CB3VehiclePhysics::CollideWheels 0x1CDB70 clears each wheel's ground contact (+0x8D3 + wheel x 0xC0)
//   before it tests the wheel; for AI cars ours keeps a contact that was set (all four wheels), so as far as the game knows
//   their wheels never leave the road. kGravity (0x004C9CC0, (0, -20, 0) at run time) is read by ApplyDrivingForces
//   0x1C1B20 and CB3CollidingBody::Update 0x1AC470; for AI cars (Update: not crashing) ours zeroes it for the call and puts
//   the game's back after it. Local Wildlife: the crash
//   Update__17CB3RaceCarPhysics starts when a car ends up on its roof (StartRaceCarCrashing 0x1AF450, called at 0x1C4D00
//   with no colliding pair or start info) is left out for AI cars. Mayhem: it starts, and CB3RaceCarPhysics::DoCrashBreaker
//   0x1BE990 follows (on the AI car's own physics: its air rams and DetachAll, the blast 0x1B2BA0 from that car's own radius
//   and strength (+0x13F4 +0x1960); the one player-indexed call, 0x3CCC50, is empty).
// - TUK-TUKS (word bit 6): every race car of a SINGLE EVENT race the tuk-tuk (COMP/CAR18), driven as a race car through
//   trafficseries (traffic_cars.h: its model file with its donor's engine sound models, the donor's tuning; a .BTV alone
//   would stop the game in the race cars' engine sound, CB3SoundESMGraphInputs::ApplyLag 0x2FAFB0). native_play gives
//   TrafficCars::set_race_cars the tuk-tuk for the player and the rivals while the bit is on; the race keeps its records
//   apart as any traffic vehicle's (records word bit 28). RIVALS' CARS' own pick (other than Your class / Same as yours)
//   still picks the rivals.
// - MOON JUMP: after func_001BD6A0 (the player car physics' frame, 0x1BD6A0) player 1's car is lifted by the level's
//   amount a frame (its matrix's y; 0x0064D3E4 for player 1) while the port action MOON JUMP is held, not while crashing.
//   Nothing stops it at a bridge or a tunnel's roof: the car can be lifted through one (the game then resets it to the road).
// - CRASH ON DEMAND: after the game's GetDrivingControls 0x1BD020 for player 1's car, a press of the port action CRASH
//   starts the crash the game starts for a boost-held start (kind 9): StartRaceCarCrashing(&D_0055E948, physics, slot, 0,
//   0), then func_001CC520(physics, 9, 0, D_004E0CC8).
// - AI DRIVER BUTTON: a press of the port action AI DRIVER calls func_00150280(car, the AI flag's opposite) from the same
//   place: the game's AI drives player 1's car, and the player again on the next press -- never a car that has finished
//   (+0x13F4 +0x133C 3): the AI drives it to the end.
// - KAMIKAZE TRAFFIC: CB3TrafficParam::CalcSpeed 0x161B50 reacts only to the race cars ahead whose +0x18EA is set (the
//   table at 0x0064D388, its count at 0x0064D37C: a race car close ahead of a traffic vehicle makes it crash itself,
//   InduceTrafficVehicleCrash 0x1B82F0); ours sets that byte on every race car (the first 6) for the call and puts each back,
//   so traffic throws itself at every car. Many wrecks: the game's 64 crashed-traffic slots are wreck_slots.h's to bound.
// The button modes' state comes per VBlank from native_play (the window's port actions; a replay's "party_moon" setting
// and "party_press" events).
#ifndef B3NATIVE_PARTY_MODES_H
#define B3NATIVE_PARTY_MODES_H

#include "b3host/split_array.h"
#include "b3host/call.h"
#include "b3host/host.h"
#include "game_option_rules.h"
#include "hook_table.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace b3native {

class PartyModes {
public:
    bool install(HookTable& hooks);
    // native_play, per VBlank before the game's frame: the race's latched options word (game_options.h GameOptions::latched,
    // refreshed; the hooks use it for the VBlank) and the port actions' state (MOON JUMP held; CRASH / AI DRIVER presses on
    // this VBlank). Outside a race with the modes the presses are dropped: none waits for the next race.
    void set_input(unsigned word, bool moon_held, int crash_presses, int ai_presses) {
        word_ = word;
        const unsigned w = word;
        moon_held_ = moon_held && party_moon(w);
        crash_pending_ = (w & kPartyCrash) ? crash_pending_ + crash_presses : 0;
        ai_pending_ = (w & kPartyAiDriver) ? ai_pending_ + ai_presses : 0;
    }
    struct Counts {
        std::uint64_t gravity_swaps = 0, wheels_kept = 0, flips_spared = 0, flips_blown = 0, moon_frames = 0,
                      crashes = 0, ai_toggles = 0, kamikaze_calls = 0, traffic_induced = 0, kamikaze_induced = 0;
    };
    const Counts& counts() const { return counts_; }

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static PartyModes*& instance();
    unsigned word() const { return word_; }
    static void colliding_update(const b3host::CallArgs& a, b3host::CallResult& r);
    static void driving_forces(const b3host::CallArgs& a, b3host::CallResult& r);
    static void collide_wheels(const b3host::CallArgs& a, b3host::CallResult& r);
    static void start_crashing(const b3host::CallArgs& a, b3host::CallResult& r);
    static void player_frame(const b3host::CallArgs& a, b3host::CallResult& r);
    static void driving_controls(const b3host::CallArgs& a, b3host::CallResult& r);
    static void calc_speed(const b3host::CallArgs& a, b3host::CallResult& r);
    static void induce_crash(const b3host::CallArgs& a, b3host::CallResult& r);
    void with_crazy_gravity(Entry next, const b3host::CallArgs& a, b3host::CallResult& r);

    Entry colliding_update_ = nullptr, driving_forces_ = nullptr, collide_wheels_ = nullptr, start_crashing_ = nullptr,
          player_frame_ = nullptr, induce_crash_ = nullptr, driving_controls_ = nullptr, calc_speed_ = nullptr;
    unsigned word_ = 0;
    bool moon_held_ = false, in_kamikaze_ = false;
    int crash_pending_ = 0, ai_pending_ = 0;
    std::set<b3host::gaddr> ai_physics_;   // the AI race cars' physics seen by ApplyDrivingForces (CollidingBody::Update's test)
    bool told_gravity_ = false;
    Counts counts_;
};

}  // namespace b3native

// ---- implementation (header-only: native_play.cpp is its one includer) ----
namespace b3native {
namespace party_modes_detail {
constexpr b3host::gaddr kCollidingUpdate = 0x001AC470u, kDrivingForces = 0x001C1B20u, kCollideWheels = 0x001CDB70u;
constexpr b3host::gaddr kStartRaceCarCrashing = 0x001AF450u, kCrashDirector = 0x0055E948u, kDoCrashBreaker = 0x001BE990u;
constexpr b3host::gaddr kPlayerFrame = 0x001BD6A0u, kDrivingControls = 0x001BD020u;
constexpr b3host::gaddr kCrashKind = 0x001CC520u, kCrashKindTime = 0x004E0CC8u, kSetAiDriven = 0x00150280u;
constexpr b3host::gaddr kCalcSpeed = 0x00161B50u, kRaceCars = 0x0064D388u, kRaceCarCount = 0x0064D37Cu;
constexpr b3host::gaddr kInduceTrafficCrash = 0x001B82F0u;   // [C] CB3PhysicsManager::InduceTrafficVehicleCrash (counted)
constexpr b3host::gaddr kGravity = 0x004C9CC0u;   // [C] kGravity, a CGtV3d ((0, -20, 0) at run time)
// CB3RaceCarPhysics / CB3VehiclePhysics
constexpr std::uint32_t kKind = 0x215u, kCrashing = 0x210u, kMatrix = 0x204u, kCar = 0x1580u, kWheelContact = 0x8D3u, kWheelStride = 0xC0u;
// CB3RaceCar
constexpr std::uint32_t kCarPlayer = 0x27C0u, kCarAiDriven = 0x27C8u, kCarSlot = 0x19ACu, kCarCrashFlag = 0x18EAu;
// the physics' +0x13F4 (the race car's state): +0x133C 3 once the car has finished [C GetDrivingControls__17CB3RaceCarPhysics]
constexpr std::uint32_t kCarState = 0x13F4u, kFinished = 0x133Cu;
// the race car list's entries: the game's 6 flat, a MORE RIVALS race's 7th and 8th in the split arrays' shadow
// (b3host/split_array.h split::shadow_or_flat)
constexpr int kKindAi = 2, kMaxRaceCars = 8, kGameRaceCars = 6;
}  // namespace party_modes_detail

inline PartyModes*& PartyModes::instance() { static PartyModes* p = nullptr; return p; }

inline bool PartyModes::install(HookTable& hooks) {
    using namespace party_modes_detail;
    const std::vector<Hook> set = {{kCollidingUpdate, HookLayer::kPort, &colliding_update, &colliding_update_},
                                   {kDrivingForces, HookLayer::kPort, &driving_forces, &driving_forces_},
                                   {kCollideWheels, HookLayer::kPort, &collide_wheels, &collide_wheels_},
                                   {kStartRaceCarCrashing, HookLayer::kPort, &start_crashing, &start_crashing_},
                                   {kPlayerFrame, HookLayer::kPort, &player_frame, &player_frame_},
                                   {kDrivingControls, HookLayer::kPort, &driving_controls, &driving_controls_},
                                   {kCalcSpeed, HookLayer::kPort, &calc_speed, &calc_speed_},
                                   {kInduceTrafficCrash, HookLayer::kPort, &induce_crash, &induce_crash_}};
    for (const Hook& h : set)
        if (!hooks.has_row(h.address)) {
            std::fprintf(stderr, "party_modes: %08X is not native: PARTY MODES off\n", h.address);
            return false;
        }
    std::string error;
    if (!hooks.install("port:party_modes", set, &error)) {
        std::fprintf(stderr, "party_modes: %s: PARTY MODES off\n", error.c_str());
        return false;
    }
    instance() = this;
    return true;
}

// CRAZY AI PHYSICS: the call with kGravity zero (the game's put back after it, by a guard: whatever the call does)
inline void PartyModes::with_crazy_gravity(Entry next, const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace party_modes_detail;
    struct Restore {
        std::uint32_t saved[4];
        Restore() {
            for (unsigned k = 0; k < 4; ++k) {
                saved[k] = b3host::load<std::uint32_t>(kGravity + 4u * k);
                b3host::store<std::uint32_t>(kGravity + 4u * k, 0u);
            }
        }
        ~Restore() {
            for (unsigned k = 0; k < 4; ++k) b3host::store<std::uint32_t>(kGravity + 4u * k, saved[k]);
        }
    };
    if (!told_gravity_) {
        told_gravity_ = true;
        std::printf("party_modes: CRAZY AI PHYSICS: AI cars without gravity while they drive (the game's %.3f)\n",
                    double(b3host::load<float>(kGravity + 4u)));
        std::fflush(stdout);
    }
    {
        const Restore guard;
        next(a, r);
    }
    ++counts_.gravity_swaps;
}

inline void PartyModes::driving_forces(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace party_modes_detail;
    PartyModes* p = instance();
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
    if (!party_crazy(p->word()) || b3host::load<std::int8_t>(self + kKind) != kKindAi) {
        p->driving_forces_(a, r);
        return;
    }
    p->ai_physics_.insert(self);
    p->with_crazy_gravity(p->driving_forces_, a, r);
}

inline void PartyModes::colliding_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace party_modes_detail;
    PartyModes* p = instance();
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
    if (!party_crazy(p->word()) || !p->ai_physics_.count(self) || b3host::load<std::int8_t>(self + kKind) != kKindAi ||
        b3host::load<std::uint8_t>(self + kCrashing) != 0) {
        p->colliding_update_(a, r);
        return;
    }
    p->with_crazy_gravity(p->colliding_update_, a, r);
}

inline void PartyModes::collide_wheels(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace party_modes_detail;
    PartyModes* p = instance();
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
    if (!party_crazy(p->word()) || b3host::load<std::int8_t>(self + kKind) != kKindAi) {
        p->collide_wheels_(a, r);
        return;
    }
    std::uint8_t had[4];
    for (unsigned w = 0; w < 4; ++w) had[w] = b3host::load<std::uint8_t>(self + kWheelContact + w * kWheelStride);
    p->collide_wheels_(a, r);
    for (unsigned w = 0; w < 4; ++w)
        if (had[w] && !b3host::load<std::uint8_t>(self + kWheelContact + w * kWheelStride)) {
            b3host::store<std::uint8_t>(self + kWheelContact + w * kWheelStride, had[w]);
            ++p->counts_.wheels_kept;
        }
}

// StartRaceCarCrashing(director, physics, slot, pair, info): an AI car's crash with no pair or start info (a flip)
inline void PartyModes::start_crashing(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace party_modes_detail;
    PartyModes* p = instance();
    const unsigned crazy = party_crazy(p->word());
    const b3host::gaddr phys = a.gpr[1].w[0] & 0x01FFFFFFu;
    // (not the Full Crashbreaker's own crash of a car in its blast: game_option_rules.h game_options_full_blast)
    const bool flip = crazy >= 2 && phys && a.gpr[3].w[0] == 0 && a.gpr[4].w[0] == 0 && !game_options_full_blast() &&
                      b3host::load<std::int8_t>(phys + kKind) == kKindAi;
    if (flip && crazy == 2) {   // Local Wildlife: the AI car rolls on
        if (p->counts_.flips_spared++ == 0) {
            std::printf("party_modes: CRAZY AI PHYSICS Local Wildlife: a flipped AI car does not crash\n");
            std::fflush(stdout);
        }
        return;
    }
    p->start_crashing_(a, r);
    if (flip && crazy == 3) {   // Mayhem: and it blows up
        b3host::call_guest<void>(kDoCrashBreaker, phys, 0);
        ++p->counts_.flips_blown;
        std::printf("party_modes: CRAZY AI PHYSICS Mayhem: a flipped AI car blew up (%llu so far)\n",
                    static_cast<unsigned long long>(p->counts_.flips_blown));
        std::fflush(stdout);
    }
}

// the player car physics' frame: MOON JUMP lifts player 1's car while its action is held (not while crashing)
inline void PartyModes::player_frame(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace party_modes_detail;
    PartyModes* p = instance();
    p->player_frame_(a, r);
    const unsigned moon = party_moon(p->word());
    if (!moon || !p->moon_held_) return;
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
    const b3host::gaddr car = b3host::load<std::uint32_t>(self + kCar) & 0x01FFFFFFu;
    if (!car || b3host::load<std::int32_t>(car + kCarPlayer) != 0 || b3host::load<std::uint8_t>(self + kCrashing) != 0) return;
    const b3host::gaddr m = b3host::load<std::uint32_t>(self + kMatrix) & 0x01FFFFFFu;
    if (!m) return;
    b3host::store<float>(m + 0x34u, b3host::load<float>(m + 0x34u) + party_moon_lift(moon));
    if (p->counts_.moon_frames++ == 0) {
        std::printf("party_modes: MOON JUMP: player 1's car lifted (matrix %08X)\n", m);
        std::fflush(stdout);
    }
}

// player 1's driving controls: CRASH ON DEMAND and the AI DRIVER BUTTON's presses
inline void PartyModes::driving_controls(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace party_modes_detail;
    PartyModes* p = instance();
    p->driving_controls_(a, r);
    if (!p->crash_pending_ && !p->ai_pending_) return;
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
    const b3host::gaddr car = b3host::load<std::uint32_t>(self + kCar) & 0x01FFFFFFu;
    if (!car || b3host::load<std::int32_t>(car + kCarPlayer) != 0) return;
    const unsigned w = p->word();
    if (p->ai_pending_) {
        const int n = p->ai_pending_;
        p->ai_pending_ = 0;
        const b3host::gaddr state = b3host::load<std::uint32_t>(self + kCarState) & 0x01FFFFFFu;
        const bool finished = state && b3host::load<std::int32_t>(state + kFinished) == 3;
        if ((w & kPartyAiDriver) && (n & 1) && finished && b3host::load<std::uint8_t>(car + kCarAiDriven) != 0) {
            std::printf("party_modes: AI DRIVER: player 1's car has finished: it stays the AI's\n");
            std::fflush(stdout);
        } else if ((w & kPartyAiDriver) && (n & 1) && !finished) {
            const bool on = b3host::load<std::uint8_t>(car + kCarAiDriven) == 0;
            b3host::call_guest<void>(kSetAiDriven, car, on ? 1 : 0);
            ++p->counts_.ai_toggles;
            std::printf("party_modes: AI DRIVER: player 1's car %s\n", on ? "driven by the AI" : "back to the player");
            std::fflush(stdout);
        }
    }
    if (p->crash_pending_) {
        p->crash_pending_ = 0;
        if ((w & kPartyCrash) && b3host::load<std::uint8_t>(self + kCrashing) == 0 && b3host::load<std::uint8_t>(car + kCarAiDriven) == 0) {
            b3host::call_guest<void>(kStartRaceCarCrashing, kCrashDirector, self, int(b3host::load<std::int8_t>(car + kCarSlot)), 0, 0);
            b3host::call_guest<void>(kCrashKind, self, 9, 0, b3host::load<float>(kCrashKindTime));
            ++p->counts_.crashes;
            std::printf("party_modes: CRASH ON DEMAND: player 1's car crashed (%llu so far)\n", static_cast<unsigned long long>(p->counts_.crashes));
            std::fflush(stdout);
        }
    }
}

// CB3TrafficParam::CalcSpeed: KAMIKAZE TRAFFIC -- every race car counted as one traffic reacts to (+0x18EA), for the call
inline void PartyModes::calc_speed(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace party_modes_detail;
    PartyModes* p = instance();
    if (!(p->word() & kPartyKamikaze)) {
        p->calc_speed_(a, r);
        return;
    }
    const int n = std::min(b3host::load<std::int32_t>(kRaceCarCount), kMaxRaceCars);
    b3host::gaddr set[kMaxRaceCars] = {};
    for (int i = 0; i < n; ++i) {
        const b3host::gaddr car = b3host::load<std::uint32_t>(b3host::split::shadow_or_flat(kRaceCars, 4, kGameRaceCars, std::size_t(i))) & 0x01FFFFFFu;
        if (car && b3host::load<std::uint8_t>(car + kCarCrashFlag) == 0) {
            b3host::store<std::uint8_t>(car + kCarCrashFlag, 1);
            set[i] = car;
        }
    }
    p->in_kamikaze_ = true;
    p->calc_speed_(a, r);
    p->in_kamikaze_ = false;
    for (int i = 0; i < n; ++i)
        if (set[i]) b3host::store<std::uint8_t>(set[i] + kCarCrashFlag, 0);
    ++p->counts_.kamikaze_calls;
}

// InduceTrafficVehicleCrash: counted (all of them; those KAMIKAZE TRAFFIC's CalcSpeed made apart), the game's as it is
inline void PartyModes::induce_crash(const b3host::CallArgs& a, b3host::CallResult& r) {
    PartyModes* p = instance();
    ++p->counts_.traffic_induced;
    if (p->in_kamikaze_) ++p->counts_.kamikaze_induced;
    p->induce_crash_(a, r);
}

}  // namespace b3native

#endif
