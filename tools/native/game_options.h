// game_options.h -- gameopts (2026-11-21): PC SETTINGS > GAME > GAMEPLAY, MIRROR MODE and CRASHBREAKER IN EVERY MODE, both
// off by default. Which races and the latch: game_option_rules.h. The picture's mirror is the renderer's (the race word's
// bit 9 in the 'HTD1' host record; src/b3gfx FrameParams::mirror_x); the game is told nothing of it but the steering.
//
// What the game has [C the generated units; decomp src/b3/_unclassified/B3PlayerCarPhysics_001BD020.cpp,
// src/nonmatching/GetCrashingDrivingControls__19CB3PlayerCarPhysicsFv.cpp, Update__8CB3ScoreFv.cpp, func_001B2BA0.cpp]:
// - MIRROR: retail keeps the input half of a mirror mode nobody finished. CB3GraphicsManagerBase +0xC (0x00665ECC; the
//   Revenge prototype's stabs name the field mbMirrorMode) is cleared by its Construct (0x19EFA4) and never set; its only
//   readers are CB3PlayerCarPhysics::GetDrivingControls (0x1BD020) and the network car's (0x268790), which negate the
//   steering (+0x1408) when it is set. Nothing draws mirrored (no renderer reads it), and GetCrashingDrivingControls
//   (0x1BB6E0, Aftertouch) does not test it. Ours does not set it: the steering itself, CB3ControllerMapping::GetSteering
//   (0x12F850, every reader of the stick's steering: driving, Aftertouch and the wheel's force feedback), answers negated
//   in a mirrored race, so the screen's left stays left in both; the game's own replay records the controls after it.
//   The renderer's free cameras follow the picture: photo mode's horizontal controls are reversed in a mirrored frame
//   (PhotoMode::set_mirror), and so are the orbit camera's (orbitcam.cpp, from the tick's race word).
// - CRASHBREAKER: the gate is CB3Game::IsCrashMode (0x131E40). CB3Score::Update (0x15B000) arms the breaker
//   (CB3PlayerCarPhysics +0x4586, through 0x1BAD00: the flag, a score event and the "ready" sound) in Crash mode once the
//   junction's vehicle count is reached; GetCrashingDrivingControls fires it, only in Crash mode, when the CRASHBREAKER
//   button (the controls' bit 0, 0x12F610: R2) is pressed: DoCrashBreaker (the vtable's +0x28, 0x1BAAF0: air rams,
//   camera event 6, CB3RaceCarPhysics::DoCrashBreaker 0x1BE990 -> the blast 0x1B2BA0 on the traffic in its radius),
//   then +0x3534 = the game time and the flag cleared (once a crash). The blast wrecks traffic only: rival race cars are
//   not in its list, so it takes nobody down. Crash mode's HUD counter and its camera director (0x356700) stay Crash
//   mode's: a race shows its own crash camera.
//   Ours (outside Crash mode only): CB3PlayerCarPhysics::StartCrashing (0x1BB250, the player cars' crash start) arms
//   it after the game's (the flag and the ready sound, 0x284B20 on 0x01E7A800; no score event), and after the game's
//   GetCrashingDrivingControls the same test and firing as Crash mode's (no crash score: there is none).
// - gamemods1, CRASHBREAKER IN EVERY MODE: Full (the word's bit 4). The blast func_001B2BA0(physics manager, the exploding car's physics, p2, p3) [C draft]
//   crashes the traffic in its radius (InduceTrafficVehicleCrash) and air-rams the crashed traffic's physics in the
//   manager's list (+0xE6100, count +0xE620C: CB3PhysicsManager 0x1B41D0 adds each traffic vehicle that starts crashing),
//   a damage flag and its loose parts each; the race cars are in neither. Full, outside Crash mode, before the game's
//   blast: every other race car (the world's list, gWorld +0x12AD28, count +0x12AD1C) not crashing yet (+0x18EA) within
//   the radius (the game's own: func_003EAD60 of the exploding car's +0x1960, or 20 m for p3) is crashed through
//   CB3CrashActionDirector::StartRaceCarCrashing (0x1AF450 on 0x0055E948, its physics gWorld +0x3C98C + 0x30 x index,
//   the cause the exploding physics' +0x218, no colliding pair) and the exploding car is marked as its last attacker as a
//   ram into it would mark it (car +0x1598 + 4 x index = its score clock +0x10CC, +0x15B0 + index = 1), so the wreck
//   counts as its Aftertouch Takedown; then the crashing race cars' physics are put after the traffic in the manager's
//   list for that call (the game's own air ram, damage and loose parts), and taken out again.
// - gamemods1, the Crashbreaker's prompt on the race HUD (both values of the option): Crash mode's HUD shows it with the
//   crash style component (CB3HUDCrashStyle, _gCrashStyleComponentP1 0x0066AFE0, player 2's 0x0066C0A0), which
//   CB3HUDManager::PrepareCrashComponents (0x1A09A0) prepares only in the crash HUD modes (+0x174 10..15). Its update
//   (func_003A3250, a virtual) shows, while the player's breaker is armed (physics +0x4586) and no crash count is up,
//   the CRASHBREAKER text (string 0x8BA) and its icon, pulsing. Outside Crash mode: after PrepareCrashComponents each
//   HUD's component (one per player view: the manager's +0x170) is prepared (Prepare__16CB3HUDCrashStyle 0x3A3890:
//   the player, align 3, 2), its text built once (+0x44 = 1, one update) and its layers hidden
//   (CB3HUDComponent::SetAllLayerVisible 0x19F420); its update then runs only while that player's car crashes with the
//   breaker armed (the layers shown), else the layers stay hidden (the game would take its objects down for good).
// - records: in a race with an option that changes it (rules: game_options_record_bits) the post-race records call
//   (0x3D1090, CB3Records, from 0x42F0F0) is made with no record kinds (a1: it writes none and reports no new record; the
//   results page's RECORD tags pointed at this race's own tables), and the
//   profile's "this race counts" test (0x136F30: totals, unlocks, the signature takedowns) answers no -- as raceopts /
//   heavyseries do (records_guard.h). Their ghosts and best times: the one records word (game_options_record_word).
// The options are the host's (native_play: the setting, a replay's "game_options"; $B3NATIVE_GAME_OPTIONS overrides).
#ifndef B3NATIVE_GAME_OPTIONS_H
#define B3NATIVE_GAME_OPTIONS_H

#include "b3host/split_array.h"
#include "b3host/call.h"
#include "b3host/host.h"
#include "game_option_rules.h"
#include "hook_table.h"
#include "records_guard.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace b3native {

class GameOptions {
public:
    bool install(HookTable& hooks);
    // gamemods1: true while the Full blast crashes a race car (its StartRaceCarCrashing call, pair 0, info 0): another
    // wrapper on the crash start (nahelam4's party modes) leaves that crash alone
    static bool full_blast_active() { return full_blast_flag(); }
    // the options asked for now (native_play: the setting or a replay's entry), read when a race starts
    void set_query(std::function<unsigned()> f) { query_ = std::move(f); }
    // The latch from guest RAM (the current mode) and the query: native_play per VBlank, the hooks at each call.
    // True when the race's options changed.
    bool refresh();
    unsigned latched() const { return latch_.latched(); }
    // the options keeping this race's results apart (0: the game's own race); read only
    unsigned record_bits() const;
    struct Counts {
        std::uint64_t steering = 0, armed = 0, fired = 0, records_skipped = 0, profile_skipped = 0;
        std::uint64_t cars_crashed = 0, cars_thrown = 0;   // gamemods1: Full's race cars crashed / air-rammed by blasts
    };
    const Counts& counts() const { return counts_; }
    static unsigned env_override();   // $B3NATIVE_GAME_OPTIONS (0..3), or -1u when not set

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static GameOptions*& instance();
    static void steering(const b3host::CallArgs& a, b3host::CallResult& r);
    static void start_crashing(const b3host::CallArgs& a, b3host::CallResult& r);
    static void crashing_controls(const b3host::CallArgs& a, b3host::CallResult& r);
    static void records(const b3host::CallArgs& a, b3host::CallResult& r);
    static void profile_counts(const b3host::CallArgs& a, b3host::CallResult& r);
    static void blast(const b3host::CallArgs& a, b3host::CallResult& r);   // gamemods1: Full
    static bool& full_blast_flag() { return game_options_full_blast_flag(); }
    static void prepare_crash_hud(const b3host::CallArgs& a, b3host::CallResult& r);   // gamemods1: the prompt
    static void crash_style(const b3host::CallArgs& a, b3host::CallResult& r);
    b3host::gaddr prompt_[2] = {0, 0};   // the crash style components prepared for a race (by player), 0 none
    bool prompt_shown_[2] = {false, false};
    bool breaker_on();   // the Crashbreaker added in this race now (the latch, and not Crash mode: a guest call)
    // the player cars' crashes (by physics object): whether this crash's breaker went off already
    struct Crash { b3host::gaddr self = 0; bool used = false; int seen = 0; };
    Crash& crash(b3host::gaddr self);
    void arm(b3host::gaddr self, int vehicles);
    std::array<Crash, 4> crashes_{};

    Entry steering_ = nullptr, start_crashing_ = nullptr, crashing_controls_ = nullptr, records_ = nullptr, profile_counts_ = nullptr,
          blast_ = nullptr, prepare_crash_hud_ = nullptr, crash_style_ = nullptr;
    std::function<unsigned()> query_;
    GameOptionLatch latch_;
    Counts counts_;
};

}  // namespace b3native

// ---- implementation (header-only: native_play.cpp is its one includer) ----
namespace b3native {
namespace game_options_detail {
constexpr b3host::gaddr kGetSteering = 0x0012F850u;       // [C] CB3ControllerMapping::GetSteering (func_0012F850)
constexpr b3host::gaddr kStartCrashing = 0x001BB250u;     // [C] CB3PlayerCarPhysics::StartCrashing (vtable 0x4DDF10)
constexpr b3host::gaddr kCrashingControls = 0x001BB6E0u;  // [C] CB3PlayerCarPhysics::GetCrashingDrivingControls
constexpr b3host::gaddr kRecords = kRecordsUpdate, kProfileCounts = 0x00136F30u;   // (records_guard.h)
constexpr b3host::gaddr kIsCrashMode = 0x00131E40u, kGame = 0x004EE040u;   // [C] CB3Game::IsCrashMode(&D_004EE040)
constexpr b3host::gaddr kReadySound = 0x00284B20u, kSoundManager = 0x01E7A800u;   // [C] func_001BAD00's sound
constexpr b3host::gaddr kGameTime = 0x00522680u;          // [C] D_00522680
// CB3PlayerCarPhysics [C]: +0x13FC the controls' bits (bit 0 the CRASHBREAKER button), +0x1580 the race car (+0x27C8
// nonzero: AI-driven), +0x3534 the breaker's time, +0x4586 the breaker armed
constexpr std::uint32_t kControls = 0x13FCu, kCar = 0x1580u, kCarAiDriven = 0x27C8u, kBreakerTime = 0x3534u, kArmed = 0x4586u;
// CRASHBREAKER AFTER [C CB3CrashCombo::UpdateCollidingPairs 0x363F10, ResetData 0x3640C0]: the physics' +0x13F4 is its car,
// whose CB3CrashCombo (car +0x2470, in every mode: the crash caption's) counts the distinct vehicles the crashing car hits
// (+0x27A0 = combo +0x330, reset when a crash starts; Crash mode's own count, CB3CrashScore's, is kept in Crash mode only)
constexpr std::uint32_t kComboCar = 0x13F4u, kVehiclesInCrash = 0x27A0u;
constexpr std::uint32_t kDoCrashBreakerSlot = 0x28u;      // [C] the vtable's DoCrashBreaker (0x1BAAF0)
// gamemods1, Full [C the blast's draft, src/nonmatching/func_001B2BA0.cpp; the race car's fields: L, checked in runs]
constexpr b3host::gaddr kBlast = 0x001B2BA0u, kStartRaceCarCrashing = 0x001AF450u, kCrashDirector = 0x0055E948u;
constexpr b3host::gaddr kBlastRadius = 0x003EAD60u, kBlastRadiusP3 = 0x004E166Cu;
constexpr b3host::gaddr kWorld = 0x00522660u;
// (MORE RIVALS: up to 8 race cars, the list's 7th and 8th in the split arrays' shadow, b3host/split_array.h; the physics
// table +0x3C98C is the head of the active-body list, read flat for every car)
constexpr std::uint32_t kRaceCarCount = 0x12AD1Cu, kRaceCarList = 0x12AD28u, kRaceCarPhysics = 0x3C98Cu, kRaceCarPhysicsStride = 0x30u;
constexpr std::uint32_t kCarPos = 0x30u, kCarCrashing = 0x18EAu, kCarIndex = 0x19ACu, kCarBlastId = 0x1960u;
constexpr std::uint32_t kCarClock = 0x10CCu, kCarHitTime = 0x1598u, kCarHitBy = 0x15B0u;
constexpr std::uint32_t kPhysBody = 0x204u, kBodyPos = 0x30u, kPhysCause = 0x218u;
constexpr std::uint32_t kPhysKind = 0x215u;   // [C] the vehicle physics' kind (party_modes.h kKind)
constexpr int kPhysKindAi = 2;
constexpr std::uint32_t kListed = 0xE6100u, kListedCount = 0xE620Cu, kListedMax = 67u;
// gamemods1, the prompt [C names: the generated units]
constexpr b3host::gaddr kPrepareCrashHud = 0x001A09A0u, kCrashStylePrepare = 0x003A3890u, kCrashStyleUpdate = 0x003A3250u,
                        kSetLayersVisible = 0x0019F420u;
constexpr b3host::gaddr kCrashStyleP1 = 0x0066AFE0u, kCrashStyleP2 = 0x0066C0A0u, kPlayerPhysics = 0x0055EFECu;
constexpr std::uint32_t kHudViews = 0x170u, kStyleShown = 0x44u, kStyleScore = 0x30u, kScoreCar = 0xC8u, kPhysicsStride = 0x30u;
}  // namespace game_options_detail

inline GameOptions*& GameOptions::instance() { static GameOptions* p = nullptr; return p; }

inline unsigned GameOptions::env_override() {
    const char* e = std::getenv("B3NATIVE_GAME_OPTIONS");
    if (!e || !*e) return ~0u;
    return game_options_entry(e);   // (the word: 2 the Crashbreaker at once, 6 after one vehicle, ...)
}

inline bool GameOptions::refresh() {
    const std::uint32_t cur = b3host::load<std::uint32_t>(kGoCurrentMode) & 0x01FFFFFFu;
    const bool stage = (cur == kGoOnePlayerStage || cur == kGoTwoPlayerStage) &&
                       game_options_stage(cur, b3host::load<std::uint32_t>(cur + 0x1B8u) & 0x01FFFFFFu);
    // (the query only when it is read: a race's first sight)
    const unsigned wanted = stage && !latch_.in_stage() && query_ ? query_() : 0u;
    return latch_.step(stage, wanted);
}

inline unsigned GameOptions::record_bits() const {
    return game_options_record_bits(latch_.latched(), b3host::load<std::int32_t>(kGoMenuChoices + 8u) == kGoCrashMode);
}

inline bool GameOptions::install(HookTable& hooks) {
    using namespace game_options_detail;
    const std::vector<Hook> set = {{kGetSteering, HookLayer::kPort, &steering, &steering_},
                                   {kStartCrashing, HookLayer::kPort, &start_crashing, &start_crashing_},
                                   {kCrashingControls, HookLayer::kPort, &crashing_controls, &crashing_controls_},
                                   {kRecords, HookLayer::kPort, &records, &records_},
                                   {kProfileCounts, HookLayer::kPort, &profile_counts, &profile_counts_},
                                   {kBlast, HookLayer::kPort, &blast, &blast_},
                                   {kPrepareCrashHud, HookLayer::kPort, &prepare_crash_hud, &prepare_crash_hud_},
                                   {kCrashStyleUpdate, HookLayer::kPort, &crash_style, &crash_style_}};
    for (const Hook& h : set)
        if (!hooks.has_row(h.address)) {
            std::fprintf(stderr, "game_options: %08X is not native: MIRROR MODE / CRASHBREAKER IN EVERY MODE off\n", h.address);
            return false;
        }
    std::string error;
    if (!hooks.install("port:game_options", set, &error)) {
        std::fprintf(stderr, "game_options: %s: MIRROR MODE / CRASHBREAKER IN EVERY MODE off\n", error.c_str());
        return false;
    }
    instance() = this;
    return true;
}

inline bool GameOptions::breaker_on() {
    using namespace game_options_detail;
    refresh();
    if (!(latch_.latched() & kGameOptCrashbreaker)) return false;
    return (b3host::call_guest<int>(kIsCrashMode, kGame) & 0xFF) == 0;   // Crash mode: the game's own breaker
}

// GetSteering: negated in a mirrored race (the picture is the world reversed: the screen's left is the world's right)
inline void GameOptions::steering(const b3host::CallArgs& a, b3host::CallResult& r) {
    GameOptions* p = instance();
    p->steering_(a, r);
    p->refresh();
    if (!(p->latch_.latched() & kGameOptMirror)) return;
    if (r.f0 & 0x7FFFFFFFu) r.f0 ^= 0x80000000u;   // (0 stays +0)
    ++p->counts_.steering;
}


// StartCrashing (a player car's crash), outside Crash mode: a new crash, the breaker not armed nor used in it yet; with
// CRASHBREAKER AFTER 0 it is armed at once (the flag and the ready sound), else when its crash has hit enough vehicles
inline void GameOptions::start_crashing(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace game_options_detail;
    GameOptions* p = instance();
    p->start_crashing_(a, r);
    if (!p->breaker_on()) return;
    const b3host::gaddr self = a.gpr[0].w[0];
    b3host::store<std::uint8_t>(self + kArmed, 0);   // (a breaker armed in an earlier crash and not used: not this one's)
    p->crash(self) = Crash{self, false, 0};
    if (game_options_cars(p->latch_.latched()) == 0) p->arm(self, 0);
}

inline GameOptions::Crash& GameOptions::crash(b3host::gaddr self) {
    for (Crash& c : crashes_) if (c.self == self) return c;
    for (Crash& c : crashes_) if (c.self == 0) { c.self = self; return c; }
    return crashes_[0];
}

inline void GameOptions::arm(b3host::gaddr self, int vehicles) {
    using namespace game_options_detail;
    b3host::store<std::uint8_t>(self + kArmed, 1);
    b3host::call_guest<void>(kReadySound, kSoundManager);
    ++counts_.armed;
    std::printf("game_options: Crashbreaker armed outside Crash mode (car physics %08X; %d vehicle%s in the crash, %u asked)\n", self,
                vehicles, vehicles == 1 ? "" : "s", game_options_cars(latch_.latched()));
    std::fflush(stdout);
}

// GetCrashingDrivingControls: outside Crash mode, the breaker armed once the crash has hit CRASHBREAKER AFTER's vehicles
// (the crash's CB3CrashCombo count), then fired on the CRASHBREAKER button as Crash mode's is (once a crash)
inline void GameOptions::crashing_controls(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace game_options_detail;
    GameOptions* p = instance();
    p->crashing_controls_(a, r);
    p->refresh();
    if (!(p->latch_.latched() & kGameOptCrashbreaker)) return;
    const b3host::gaddr self = a.gpr[0].w[0];
    const b3host::gaddr car = b3host::load<std::uint32_t>(self + kCar);
    if (car == 0 || b3host::load<std::uint8_t>(car + kCarAiDriven) != 0) return;
    if (b3host::load<std::uint8_t>(self + kArmed) == 0) {
        Crash& c = p->crash(self);
        const unsigned want = game_options_cars(p->latch_.latched());
        if (c.self != self || c.used || want == 0) return;   // (0: armed at the start, or used)
        const b3host::gaddr combo_car = b3host::load<std::uint32_t>(self + kComboCar);
        const int hit = combo_car ? b3host::load<std::int32_t>(combo_car + kVehiclesInCrash) : 0;
        static const bool trace = std::getenv("B3_GAMEOPT_CRASH_TRACE") != nullptr;   // tests: the count as it rises
        if (trace && hit != c.seen) {
            c.seen = hit;
            std::printf("game_options: the crash has hit %d vehicle%s (%u asked)\n", hit, hit == 1 ? "" : "s", want);
            std::fflush(stdout);
        }
        if (hit < int(want) || !p->breaker_on()) return;
        p->arm(self, hit);
        return;   // (fired from the next call on)
    }
    if ((b3host::load<std::uint32_t>(self + kControls) & 1u) == 0 || !p->breaker_on()) return;
    const b3host::gaddr vt = b3host::load<std::uint32_t>(self);
    b3host::call_guest<void>(b3host::load<std::uint32_t>(vt + kDoCrashBreakerSlot), self, 0);
    b3host::store<float>(self + kBreakerTime, b3host::load<float>(kGameTime));
    b3host::store<std::uint8_t>(self + kArmed, 0);
    p->crash(self).used = true;
    ++p->counts_.fired;
    std::printf("game_options: Crashbreaker fired outside Crash mode (car physics %08X, %llu so far)\n", self,
                static_cast<unsigned long long>(p->counts_.fired));
    std::fflush(stdout);
}

// gamemods1, CRASHBREAKER IN EVERY MODE: Full (above): the race cars caught in the blast, outside Crash mode
inline void GameOptions::blast(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace game_options_detail;
    GameOptions* p = instance();
    p->refresh();
    const unsigned w = p->latch_.latched();
    const b3host::gaddr pm = a.gpr[0].w[0], veh = a.gpr[1].w[0];
    const int p2 = int(a.gpr[2].w[0]), p3 = int(a.gpr[3].w[0]);
    if (!(w & kGameOptCrashbreaker) || !(w & kGameOptCrashFull) || p2 != 0 || !p->breaker_on()) { p->blast_(a, r); return; }
    auto f32 = [](b3host::gaddr x) { return b3host::load<float>(x); };
    const b3host::gaddr own = b3host::load<std::uint32_t>(veh + kComboCar);   // the exploding car (CB3RaceCar)
    const b3host::gaddr body = b3host::load<std::uint32_t>(veh + kPhysBody);
    // only a player's own Crashbreaker (the physics' kind +0x215: 2 an AI car, as party_modes.h reads it): an AI car's
    // blast (CRAZY AI PHYSICS Mayhem's) stays the game's, the traffic only
    if (own == 0 || body == 0 || b3host::load<std::int8_t>(veh + kPhysKind) == kPhysKindAi) { p->blast_(a, r); return; }
    const float bx = f32(body + kBodyPos), by = f32(body + kBodyPos + 4u), bz = f32(body + kBodyPos + 8u);
    float radius = p3 != 0 ? f32(kBlastRadiusP3) : b3host::call_guest<float>(kBlastRadius, b3host::load<std::uint64_t>(own + kCarBlastId));
    static const float test_radius = [] { const char* e = std::getenv("B3NATIVE_CB_RADIUS"); return e && *e ? float(std::atof(e)) : 0.0f; }();
    if (test_radius > 0) radius = test_radius;   // tests only: the race cars' reach (the traffic's stays the game's)
    // the race cars: the game's six, eight with MORE RIVALS (cars 7-8 in the list's shadow, split_array.h)
    const int n = std::min(b3host::load<std::int32_t>(kWorld + kRaceCarCount), 8);
    const std::uint8_t cause = b3host::load<std::uint8_t>(veh + kPhysCause);
    {   // the blast's reach: its radius and the race cars' distances (the log of each Full blast)
        std::printf("game_options: Crashbreaker (Full): blast radius %.1f m (the player's takedowns so far %d); race cars at", double(radius),
                    b3host::load<std::int32_t>(0x0064E4D8u));
        for (int i = 0; i < n; ++i) {
            const b3host::gaddr car = b3host::load<std::uint32_t>(b3host::split::shadow_or_flat(kWorld + kRaceCarList, 4, 6, std::size_t(i)));
            if (car == 0 || car == own) continue;
            const float dx = f32(car + kCarPos) - bx, dy = f32(car + kCarPos + 4u) - by, dz = f32(car + kCarPos + 8u) - bz;
            std::printf(" %.0f%s", double(std::sqrt(dx * dx + dy * dy + dz * dz)), b3host::load<std::uint8_t>(car + kCarCrashing) ? "c" : "");
        }
        std::printf(" m\n");
    }
    for (int i = 0; i < n; ++i) {   // 1: the race cars not crashing yet, crashed; the exploding car their attacker
        const b3host::gaddr car = b3host::load<std::uint32_t>(b3host::split::shadow_or_flat(kWorld + kRaceCarList, 4, 6, std::size_t(i)));
        if (car == 0 || car == own || b3host::load<std::uint8_t>(car + kCarCrashing) != 0) continue;
        const float dx = f32(car + kCarPos) - bx, dy = f32(car + kCarPos + 4u) - by, dz = f32(car + kCarPos + 8u) - bz;
        if (!(std::sqrt(dx * dx + dy * dy + dz * dz) < radius)) continue;
        const std::uint32_t idx = b3host::load<std::uint8_t>(car + kCarIndex);
        const b3host::gaddr phys = b3host::load<std::uint32_t>(kWorld + kRaceCarPhysics + kRaceCarPhysicsStride * idx);
        if (phys == 0) continue;
        full_blast_flag() = true;
        b3host::call_guest<void>(kStartRaceCarCrashing, kCrashDirector, phys, int(cause), 0u, 0u);
        full_blast_flag() = false;
        // the score's takedown tables (CB3Score +0x4D8 / +0x4F0, 6 entries: cars 7-8's in the shadow, split_array.h)
        b3host::store<float>(b3host::split::shadow_or_flat(own + kCarHitTime, 4, 6, idx), f32(own + kCarClock));
        b3host::store<std::uint8_t>(b3host::split::shadow_or_flat(own + kCarHitBy, 1, 6, idx), 1);
        ++p->counts_.cars_crashed;
        std::printf("game_options: Crashbreaker (Full): race car %u caught in the blast (%.1f m of %.1f), the exploding car its attacker\n", idx,
                    double(std::sqrt(dx * dx + dy * dy + dz * dz)), double(radius));
    }
    // 2: the crashing race cars' physics after the traffic in the manager's list, for the game's own air ram (then out)
    const std::int32_t count = b3host::load<std::int32_t>(pm + kListedCount);
    std::int32_t added = 0;
    std::uint32_t saved[8] = {};   // the race cars but the exploding one: 7 at most (MORE RIVALS' 8 cars)
    for (int i = 0; i < n && added < 8 && count + added < std::int32_t(kListedMax); ++i) {
        const b3host::gaddr car = b3host::load<std::uint32_t>(b3host::split::shadow_or_flat(kWorld + kRaceCarList, 4, 6, std::size_t(i)));
        if (car == 0 || car == own || b3host::load<std::uint8_t>(car + kCarCrashing) == 0) continue;
        const std::uint32_t idx = b3host::load<std::uint8_t>(car + kCarIndex);
        const b3host::gaddr phys = b3host::load<std::uint32_t>(kWorld + kRaceCarPhysics + kRaceCarPhysicsStride * idx);
        if (phys == 0) continue;
        const b3host::gaddr slot = pm + kListed + 4u * std::uint32_t(count + added);
        saved[added] = b3host::load<std::uint32_t>(slot);
        b3host::store<std::uint32_t>(slot, phys);
        ++added;
    }
    if (added) b3host::store<std::int32_t>(pm + kListedCount, count + added);
    p->blast_(a, r);
    if (added) {
        b3host::store<std::int32_t>(pm + kListedCount, count);
        for (std::int32_t k = 0; k < added; ++k) b3host::store<std::uint32_t>(pm + kListed + 4u * std::uint32_t(count + k), saved[k]);
        p->counts_.cars_thrown += std::uint64_t(added);
        std::printf("game_options: Crashbreaker (Full): %d crashing race car%s thrown by the blast\n", int(added), added == 1 ? "" : "s");
    }
    std::fflush(stdout);
}

// gamemods1: the Crashbreaker's prompt (above). PrepareCrashComponents (each HUD Prepare): the components for a race
inline void GameOptions::prepare_crash_hud(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace game_options_detail;
    GameOptions* p = instance();
    p->prepare_crash_hud_(a, r);
    p->prompt_[0] = p->prompt_[1] = 0;
    if (!p->breaker_on()) return;
    const int views = std::clamp(b3host::load<std::int32_t>(a.gpr[0].w[0] + kHudViews), 1, 2);
    for (int v = 0; v < views; ++v) {
        const b3host::gaddr comp = v == 0 ? kCrashStyleP1 : kCrashStyleP2;
        b3host::call_guest<void>(kCrashStylePrepare, comp, v, 3, 2);
        b3host::store<std::int32_t>(comp + kStyleShown, 1);           // (a value other than the count's 0: the text is built)
        b3host::call_guest<void>(kCrashStyleUpdate, comp, 0.0f);
        b3host::call_guest<void>(kSetLayersVisible, comp, 0);
        p->prompt_[v] = comp;
        p->prompt_shown_[v] = false;
    }
    std::printf("game_options: the Crashbreaker prompt prepared on %d race HUD%s\n", views, views == 1 ? "" : "s");
    std::fflush(stdout);
}

// The crash style's update: for a race's prompt, shown only while its player's car crashes with the breaker armed
inline void GameOptions::crash_style(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace game_options_detail;
    GameOptions* p = instance();
    const b3host::gaddr self = a.gpr[0].w[0];
    const int v = self == p->prompt_[0] && self != 0 ? 0 : self == p->prompt_[1] && self != 0 ? 1 : -1;
    if (v < 0) { p->crash_style_(a, r); return; }
    const b3host::gaddr score = b3host::load<std::uint32_t>(self + kStyleScore);
    const b3host::gaddr car = score ? b3host::load<std::uint32_t>(score + kScoreCar) : 0;
    if (car == 0) return;
    const int player = b3host::load<std::int8_t>(car + kCarIndex);
    const b3host::gaddr phys = b3host::load<std::uint32_t>(kPlayerPhysics + kPhysicsStride * std::uint32_t(player < 0 ? 0 : player));
    const bool show = phys != 0 && b3host::load<std::uint8_t>(phys + kArmed) != 0 && b3host::load<std::uint8_t>(car + kCarCrashing) != 0;
    if (show != p->prompt_shown_[v]) {
        b3host::call_guest<void>(kSetLayersVisible, self, show ? 1 : 0);
        p->prompt_shown_[v] = show;
        std::printf("game_options: the Crashbreaker prompt %s (player %d)\n", show ? "shown" : "hidden", v + 1);
        std::fflush(stdout);
    }
    if (show) p->crash_style_(a, r);   // (its pulse; hidden, its update would take the objects down)
}

// CB3Records::Update 0x3D1090: in a race with the options no record kinds, the results page's RECORD tags pointed at this
// race's own tables (records_guard.h records_without_kinds, shared with raceopts / elim1 / heavyseries)
inline void GameOptions::records(const b3host::CallArgs& a, b3host::CallResult& r) {
    GameOptions* p = instance();
    p->refresh();
    static const bool trace = std::getenv("B3_GAMEOPT_RECORDS_TRACE") != nullptr;   // tests: every update, its result
    if (p->record_bits() == 0) {
        p->records_(a, r);
        if (trace)
            std::printf("game_options: records update: kinds %llu, a new record %u (type %u)\n",
                        static_cast<unsigned long long>(a.gpr[1].w[0] | (std::uint64_t(a.gpr[1].w[1]) << 32)), r.v0.w[0] & 0xFFu,
                        b3host::load<std::uint32_t>(kRecordsLastType));
        return;
    }
    const std::uint64_t kinds = records_without_kinds(p->records_, a, r);
    ++p->counts_.records_skipped;
    std::printf("game_options: a race with %s: the record kinds %llu not written (no records kept; a new record %u)\n",
                game_options_name(p->record_bits()).c_str(), static_cast<unsigned long long>(kinds), r.v0.w[0] & 0xFFu);
    std::fflush(stdout);
}

// 0x136F30 (whether the race counts for the profile): not with the options
inline void GameOptions::profile_counts(const b3host::CallArgs& a, b3host::CallResult& r) {
    GameOptions* p = instance();
    p->profile_counts_(a, r);
    if ((r.v0.w[0] & 0xFFu) == 0) return;
    p->refresh();
    if (p->record_bits() == 0) return;
    if (p->counts_.profile_skipped++ == 0) {
        std::printf("game_options: a race with %s: the profile's totals and unlocks left as they were\n",
                    game_options_name(p->record_bits()).c_str());
        std::fflush(stdout);
    }
    r.v0 = u_long128{};
}

}  // namespace b3native

#endif
