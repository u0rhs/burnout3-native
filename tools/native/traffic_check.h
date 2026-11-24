// traffic_check.h -- trafficcheck (2026-11-21): TRAFFIC CHECK Off / Revenge / Extreme (SINGLE EVENT: SELECT OPTIONS' row,
// race_choice.h; two-player races: PC SETTINGS > GAME > GAMEPLAY > TRAFFIC CHECK (2 PLAYERS)), Off by default: traffic is
// launched ahead instead of crashing you, as in Burnout Revenge (Revenge: its rules and values; Extreme: any traffic).
// Which races, the latch, what counts as a check and the scoring: traffic_check_rules.h. Built on the game's own
// functions.
//
// What the game has [C the generated units; decomp src/nonmatching/VehicleTrafficResponse__17CB3PhysicsManager...cpp,
// CollideAndRespondPair..., BodyBodyResponse..., StartRaceCarCrashing..., src/b3/_unclassified/B3PhysicsManager_001B41D0.cpp,
// UpdateActiveBodyList..., B3Score_001598F0.cpp, func_001578E0.cpp, UpdateTakeDownStats..., B3HUDBoostBar_001A45F0.cpp,
// MessagesUpdate..., B3ScoreBonus.cpp, UpdateMessage__20CB3HUDPrimaryMessageFf.cpp]:
// - CB3PhysicsManager (0x0055EF70) CollideAndRespondPair sends a vehicle against a traffic body (type 3) to
//   VehicleTrafficResponse (0x1B5AA0, the pair: +0 contact, +0x10 normal from the traffic to the vehicle, +0x20 impulse,
//   +0x24 / +0x28 the two active bodies: +0 type 0 AI / 1 network / 2 player / 3 traffic / 4 crashing traffic, +1
//   removal mark, +4 matrix, +0xC object; +0x2C colliding). When the hull test and the closing speed make the hit fatal,
//   a race car gets StartRaceCarCrashing (0x1AF450, the director 0x0055E948), the traffic CrashTrafficBody (0x1B41D0: the
//   body becomes type 4 with one of the manager's 64 CB3TrafficPhysics wreck slots, +0x32D00 0x2430 each, in use at
//   +0x210; the slot search has no bound) and the pair BodyBodyResponse (0x1B4BF0).
// - Two vehicles (a race car and a wreck among them) go to BodyBodyResponse, whose fatal test starts the race car's crash
//   through StartRaceCarCrashing too. UpdateActiveBodyList (the whole of PostEffectsUpdate 0x1B8EF0, once a physics
//   frame) takes out the bodies marked +1 = 1: a wreck's slot released (its vtable +0x14) and the wreck list compacted.
// - Takedowns: a crashing car's CB3Score::UpdateTakeDownStats writes the aggressor's table (CB3RaceCar +0x1598 float[6]
//   the time by the victim's index +0x19AC, +0x15B0 / +0x15B6 flags); the aggressor's CalculateScoreForTakedowns
//   (0x1578E0) awards the takedown after gmrTakedownWaitTime if it is not crashing itself (+0x18EA).
// - The boost bar (CB3HUDBoostBar, its score at +0x68C) shows its captions with stars by UpdateBoostMessage (0x1A45F0:
//   the message slot, a CB3ScoreCategory {value, time, previous, thresholds*, active, level, previous level, levels}, dt,
//   the value to show from); MessagesUpdate (0x1A41B0) updates slots 0-2 and 4-6. Slot 3 (+0x5E8; its text +0x5EC,
//   string 0x850 from Prepare) is never updated in retail. A chain's end shows a sign through CB3ScoreBonus::OnAward
//   (score +0x124, 0x368E90: the event, its value, the category) -- NEAR MISS's chain end does so -- which
//   CB3HUDPrimaryMessage::UpdateMessage (0x3D8280) draws with the event's string (GetString 0x271A00 of _kaEventMap's id;
//   event 0x77 PERFECT LAP, string 0x804).
// Ours (wrappers on the hook table's port layer), only in a race with the option latched and not Crash mode:
//   - VehicleTrafficResponse: a check candidate (rules) is marked; StartRaceCarCrashing for that car and pair is then not
//     made, and the BodyBodyResponse of that pair is our push instead (the game's CrashTrafficBody makes the traffic a
//     wreck credited to the player, as for a crash): the wreck's velocity becomes the car's plus the push along the
//     contact's normal, a little spin, the car a small recoil (the physics' impulse accumulators +0x110 / +0x120). Its
//     +0x211 is put back. The checked wreck is the checker's for kOwnedSeconds. A wreck of ours wrecking traffic in
//     VehicleTrafficResponse scores TRICK / SKILL / EXTREME SHOT;
//   - BodyBodyResponse: the checker touching its own checked wreck (of any age) is not crashed by it (its
//     StartRaceCarCrashing not made; the wreck its own again); Extreme: a human player's touch of any other wreck that is
//     not another player's checked car now is spared too, the wreck the player's (a check). Another race car touching a
//     checked wreck is crashed (not within its reset grace, +0x152C >= 0) and the takedown written to the checker's table
//     as UpdateTakeDownStats writes an aggressor's (the game then awards it);
//   - CrashTrafficBody (after): a slot taken again is no longer a checked wreck. A check needs two free wreck slots;
//   - PostEffectsUpdate (before): the chains' ends; with 40 or more wreck slots in use, up to two wrecks not linked to a
//     trailer, behind every player and 60 m away (or 150 m away anywhere; with 56 or more, 40 m away anywhere), marked
//     for removal as the game marks one;
//   - MessagesUpdate (before): slot 3 is TRAFFIC CHECK, updated from the chain's category (the game's near-miss star
//     levels: the score's own near-miss category's thresholds); its popup sound as the game's;
//   - the chain's end: OnAward(PERFECT LAP, the chain's points), the sign's string ours while UpdateMessage draws it (and
//     its ShowScore 0x1A2030 not made: the sign's number is the chain's, not the mode's score);
//   - each event: its boost (CB3Burn::AddBurn 0x14E2D0; a check half gmrNearMissBurnValue 0x4E1498, the shots 40 / 60 / 80);
//   - records: the post-race records call (0x3D1090) through records_guard.h (no record kinds), the profile's "this race
//     counts" (0x136F30) no;
//   - the signs: ours only while the pending award is the one we made (its value and time), so a PERFECT LAP the game
//     awards meanwhile shows as the game's.
// The option is the host's (native_play: the row's choice, the setting, a replay's; $B3NATIVE_TRAFFIC_CHECK=0/1/2 overrides).
#ifndef B3NATIVE_TRAFFIC_CHECK_H
#define B3NATIVE_TRAFFIC_CHECK_H

#include "b3host/split_array.h"
#include "b3host/call.h"
#include "b3host/host.h"
#include "hook_table.h"
#include "records_guard.h"
#include "traffic_check_rules.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace b3native {

class TrafficCheck {
public:
    bool install(HookTable& hooks);
    // the mode asked for now (TcMode; native_play: the setting or a replay's entry), read when a race starts
    void set_query(std::function<unsigned()> f) { query_ = std::move(f); }
    // the game's text language (race_choice.h race_words' codes; native_play: platform_disc.h language_text())
    void set_language(std::function<std::string()> f) { language_ = std::move(f); }
    // The latch from guest RAM (the current mode) and the query: native_play per VBlank, the hooks at each call.
    // True when the race's option changed.
    bool refresh();
    bool latched() const { return latch_.latched(); }
    unsigned mode() const { return latch_.mode(); }   // TcMode of the race on now
    struct Counts {
        std::uint64_t checks = 0, trick_shots = 0, skill_shots = 0, extreme_shots = 0, takedowns = 0, own_touches = 0;
        std::uint64_t not_checked_pool = 0, wrecks_cleared = 0, chains = 0, signs = 0, records_skipped = 0, profile_skipped = 0;
        std::uint64_t wreck_checks = 0;   // (of checks: Extreme's checks of wrecks)
        std::uint64_t shunt_checks = 0;   // (of checks: hits the game only shunted, from 75 mph)
        std::uint64_t chain_owned = 0;    // wrecks a checked car's chain knocked, its checker's
        std::uint64_t player_traffic_crashes = 0, player_wreck_crashes = 0;   // a human player's crashes on traffic / wrecks
    };
    const Counts& counts() const { return counts_; }
    static int env_override();   // $B3NATIVE_TRAFFIC_CHECK 0 / 1 (Revenge) / 2 (Extreme), or -1 when not set

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static TrafficCheck*& instance();
    static void vehicle_traffic(const b3host::CallArgs& a, b3host::CallResult& r);
    static void start_crashing(const b3host::CallArgs& a, b3host::CallResult& r);
    static void body_body(const b3host::CallArgs& a, b3host::CallResult& r);
    static void crash_traffic(const b3host::CallArgs& a, b3host::CallResult& r);
    static void post_effects(const b3host::CallArgs& a, b3host::CallResult& r);
    static void messages_update(const b3host::CallArgs& a, b3host::CallResult& r);
    static void update_message(const b3host::CallArgs& a, b3host::CallResult& r);
    static void get_string(const b3host::CallArgs& a, b3host::CallResult& r);
    static void show_score(const b3host::CallArgs& a, b3host::CallResult& r);
    static void records(const b3host::CallArgs& a, b3host::CallResult& r);
    static void profile_counts(const b3host::CallArgs& a, b3host::CallResult& r);

    bool active();   // the option latched in this race now, and not Crash mode (a guest call)
    bool region_ok();
    void write_texts();
    // a checked wreck (by its CB3TrafficPhysics) and the chain of a checker (by its CB3RaceCar)
    // depth: 0 the checked car itself, n a wreck the chain's car of depth n - 1 knocked (protection only: never fatal to its
    // checker; no takedowns or shots of its own)
    struct Owned { b3host::gaddr wreck = 0, checker = 0; float at = 0.0f; int depth = 0; };
    Owned* owned(b3host::gaddr wreck);   // a checked wreck still its checker's now (null: none)
    Owned* checked(b3host::gaddr wreck);   // a checked wreck of any age, its slot not taken again (null: none)
    void set_owner(b3host::gaddr wreck, b3host::gaddr checker, int depth = 0);   // the wreck the checker's from now (one entry a wreck)
    void chain_own(b3host::gaddr wreck, const Owned& by, const char* how);   // a wreck the chain's car knocked: the checker's too
    void check_shunt(b3host::gaddr pair, b3host::gaddr car, b3host::gaddr body);   // a shunt made a check (CrashTrafficBody, push)
    // TEST ONLY ($B3_TRAFFIC_CHECK_TEST_OWNED=1, checks only): a wreck no one checked counts as checked by the first
    // human player's car at its first touch by a race car, so a scripted run reaches the checked-car takedown
    Owned* test_owned(b3host::gaddr wreck);
    // a player's chain and its signs: awarded one at a time when the game's sign is free (UpdateMessage), the one awarded
    // and not yet taken (pending) and those waiting
    struct Queued { TcSign sign; int value; };
    struct Chain {
        b3host::gaddr car = 0;
        TcChain c;
        bool sign_pending = false;
        TcSign sign = TcSign::kTotal;
        std::int32_t sign_value = 0;   // the award of ours pending: its value and the bonus' time it set (+0x138), so a
        float sign_time = 0.0f;        // PERFECT LAP the game awards meanwhile is the game's, not ours
        std::vector<Queued> queue;
    };
    Chain* chain(b3host::gaddr car, bool make);
    b3host::gaddr category(const Chain& c) const;
    void event(b3host::gaddr car, TcEvent e);
    void end_chain(Chain& c, float now);
    void show_sign(Chain& c, TcSign s, int value);   // one of ours on the PERFECT LAP sign, when it is free
    bool award_sign(Chain& c, TcSign s, int value);  // its award now (false: the game refused it)
    b3host::gaddr sign_slot(const Chain& c) const;
    void push(b3host::gaddr pair);
    void clear_wrecks(b3host::gaddr manager);
    void reset_race();

    Entry vehicle_traffic_ = nullptr, start_crashing_ = nullptr, body_body_ = nullptr, crash_traffic_ = nullptr;
    Entry post_effects_ = nullptr, messages_update_ = nullptr, update_message_ = nullptr, get_string_ = nullptr, show_score_ = nullptr;
    Entry records_ = nullptr, profile_counts_ = nullptr;
    std::function<unsigned()> query_;
    std::function<std::string()> language_;
    TrafficCheckLatch latch_;
    Counts counts_;
    bool region_checked_ = false, region_bad_ = false;
    std::string texts_for_ = "?";
    // the check under way in VehicleTrafficResponse (its pair, car, the traffic's body) and whether its crash was refused
    struct Pending { b3host::gaddr pair = 0, car = 0, body = 0; std::uint8_t c211 = 0; bool checked = false; };
    Pending pending_;
    // B3_TRAFFIC_CHECK_TRACE: the path a human player's crash came by (VehicleTrafficResponse 1, BodyBodyResponse 2) and
    // the rule that kept it from being a check
    struct TraceCtx { int path = 0; const char* why = nullptr; };
    TraceCtx ctx_;
    void trace_crash(b3host::gaddr car, b3host::gaddr pair);
    b3host::gaddr spare_car_ = 0;   // BodyBodyResponse: this car's StartRaceCarCrashing is not made (its own wreck)
    bool spared_ = false;           // ... and the game asked for it (the touch was fatal)
    std::uint32_t text_override_ = 0;   // UpdateMessage: the sign's string in place of PERFECT LAP's
    std::vector<Owned> owned_;
    std::array<Chain, 2> chains_{};
};

}  // namespace b3native

// ---- implementation (header-only: native_play.cpp is its one includer) ----
namespace b3native {
namespace traffic_check_detail {
constexpr b3host::gaddr kVehicleTraffic = 0x001B5AA0u, kStartRaceCarCrashing = 0x001AF450u, kBodyBody = 0x001B4BF0u;
constexpr b3host::gaddr kCrashTrafficBody = 0x001B41D0u, kPostEffects = 0x001B8EF0u;
constexpr b3host::gaddr kMessagesUpdate = 0x001A41B0u, kUpdateBoostMessage = 0x001A45F0u, kGeneralPopup = 0x00285BC0u;
constexpr b3host::gaddr kHudSound = 0x01E7A800u;
constexpr b3host::gaddr kUpdateMessage = 0x003D8280u, kGetString = 0x00271A00u, kOnAward = 0x00368E90u;
constexpr b3host::gaddr kShowScore = 0x001A2030u;   // CB3HUDScore::ShowScore(hud score, event, value)
constexpr b3host::gaddr kAddBurn = 0x0014E2D0u, kNearMissBurn = 0x004E1498u;   // gmrNearMissBurnValue
constexpr b3host::gaddr kIsCrashMode = 0x00131E40u, kGame = 0x004EE040u;
constexpr b3host::gaddr kRecords = 0x003D1090u, kProfileCounts = 0x00136F30u;
constexpr b3host::gaddr kDirector = 0x0055E948u;
constexpr std::uint32_t kPerfectLapEvent = 0x77u, kPerfectLapString = 0x804u, kCaptionSlot = 3u;
// CB3PhysicsManager: the active bodies (0x30 each) and their count; the wreck slots
constexpr std::uint32_t kBodies = 0x70u, kBodyCount = 0x1CB70u, kWrecks = 0x32D00u, kWreckBytes = 0x2430u, kWreckSlots = 64u;
constexpr std::uint32_t kMaxBodies = (kBodyCount - kBodies) / 0x30u;   // the body array's room (2448)
// a vehicle's physics (CB3VehiclePhysics: race cars and wrecks)
constexpr std::uint32_t kVel = 0xB0u, kSpeed = 0xBCu, kLinear = 0x110u, kAngular = 0x120u, kMass = 0x1F0u, kMatrix = 0x204u;
constexpr std::uint32_t kCrashing = 0x210u, kHit = 0x211u, kRaceCar = 0x13F4u, kGrace = 0x152Cu;
constexpr std::uint32_t kDriver = 0x1580u, kAiDriven = 0x27C8u;   // CB3PlayerCarPhysics' race car, AI-driven [C gameopts]
constexpr std::uint32_t kWreckCategory = 0x2420u, kWreckLinked = 0x2424u;
constexpr std::uint32_t kMatAt = 0x20u, kMatPos = 0x30u;
// the traffic vehicle (a type 3 body's object): its unit heading, its category
constexpr std::uint32_t kTrafficHeading = 0x90u, kTrafficCategory = 0x173u;
// CB3RaceCar: its score (+0xC the clock, +0xCC the burn, +0x124 the bonus, +0x418 the near-miss category), its takedown
// table, its state
constexpr std::uint32_t kScore = 0x10C0u, kClock = 0x10CCu, kBurn = 0x118Cu, kFinish = 0x133Cu;
constexpr std::uint32_t kTakedownTime = 0x1598u, kTakedownNow = 0x15B0u, kTakedownSlam = 0x15B6u;
constexpr std::uint32_t kCarCrashing = 0x18EAu, kCarIndex = 0x19ACu;
constexpr std::uint32_t kScoreBonus = 0x124u, kScoreNearMiss = 0x418u, kBonusEvent = 0x254u, kBonusValue = 0x258u, kBonusPending = 0x134u;
constexpr std::uint32_t kBonusTime = 0x25Cu;   // CB3ScoreBonus +0x138: the clock at its last award [C OnEvent 0x368EF0]
constexpr std::uint32_t kBarScore = 0x68Cu, kBarSlotText = 0x570u + kCaptionSlot * 0x28u + 4u;
// the region: two categories (0x14 each), the caption (UTF-16, 0x30 bytes), a sign per player (0x34 bytes each)
constexpr b3host::gaddr kRegion = GuestArena::kTrafficCheck.address, kRegionEnd = kRegion + GuestArena::kTrafficCheck.size;
constexpr b3host::gaddr kCategories = kRegion, kCaption = kRegion + 0x28u, kSigns = kRegion + 0x58u, kOwnEnd = kRegion + 0xC0u;
constexpr unsigned kCaptionBytes = 0x30u, kSignBytes = 0x34u;
// (the rest of the region, 0xC0 on, is raceopts' TRAFFIC CHECK row: race_options.h kKind5)
static_assert(kCategories + 2u * 0x14u <= kCaption && kCaption + kCaptionBytes <= kSigns && kSigns + 2u * kSignBytes <= kOwnEnd,
              "the categories and the texts fit the region's own part");

inline b3host::gaddr ga(std::uint32_t v) { return v & 0x01FFFFFFu; }
inline b3host::gaddr ptr(b3host::gaddr at) { return ga(b3host::load<std::uint32_t>(at)); }
inline float f32(b3host::gaddr at) { return b3host::load<float>(at); }
inline void add_f32(b3host::gaddr at, float v) { b3host::store<float>(at, b3host::load<float>(at) + v); }
inline std::int8_t body_type(b3host::gaddr body) { return b3host::load<std::int8_t>(body); }
inline b3host::gaddr body_object(b3host::gaddr body) { return ptr(body + 0xCu); }
// UTF-8 text as the game's UTF-16 (the BMP; at most bytes / 2 - 1 units, then 0)
inline void write_utf16(b3host::gaddr at, const std::string& text, unsigned bytes) {
    unsigned n = 0;
    for (std::size_t i = 0; i < text.size() && n + 1 < bytes / 2; ++n) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint32_t u = c;
        std::size_t len = 1;
        if (c >= 0xE0 && i + 2 < text.size()) { u = (c & 0x0Fu) << 12 | (text[i + 1] & 0x3Fu) << 6 | (text[i + 2] & 0x3Fu); len = 3; }
        else if (c >= 0xC0 && i + 1 < text.size()) { u = (c & 0x1Fu) << 6 | (text[i + 1] & 0x3Fu); len = 2; }
        b3host::store<std::uint16_t>(at + 2u * n, std::uint16_t(u));
        i += len;
    }
    b3host::store<std::uint16_t>(at + 2u * n, 0);
}
// A human player's car able to check now: body type 2, its physics not crashing, its race car not AI-driven nor finished
inline bool checker_ok(b3host::gaddr body) {
    if (body_type(body) != 2) return false;
    const b3host::gaddr phys = body_object(body);
    if (!phys || b3host::load<std::uint8_t>(phys + kCrashing) != 0) return false;
    const b3host::gaddr car = ptr(phys + kRaceCar), driver = ptr(phys + kDriver);
    // TEST ONLY ($B3_TRAFFIC_CHECK_TEST_AI=1): a car the AI drives (party modes' AI DRIVER) checks too, so a scripted run
    // drives the route through the traffic
    static const bool test_ai = [] { const char* e = std::getenv("B3_TRAFFIC_CHECK_TEST_AI"); return e && e[0] == '1' && !e[1]; }();
    if (!car || (driver && !test_ai && b3host::load<std::uint8_t>(driver + kAiDriven) != 0)) return false;
    return b3host::load<std::int32_t>(car + kFinish) != 3;
}
inline unsigned free_wreck_slots() {
    constexpr b3host::gaddr mgr = 0x0055EF70u;
    unsigned n = 0;
    for (unsigned i = 0; i < kWreckSlots; ++i) n += b3host::load<std::uint8_t>(mgr + kWrecks + i * kWreckBytes + kCrashing) == 0;
    return n;
}
}  // namespace traffic_check_detail

inline TrafficCheck*& TrafficCheck::instance() { static TrafficCheck* p = nullptr; return p; }

inline int TrafficCheck::env_override() {
    const char* e = std::getenv("B3NATIVE_TRAFFIC_CHECK");
    if (!e || !*e) return -1;
    return int(traffic_check_entry(e));
}

inline bool TrafficCheck::refresh() {
    const std::uint32_t cur = b3host::load<std::uint32_t>(kTcCurrentMode) & 0x01FFFFFFu;
    const bool stage = (cur == kTcOnePlayerStage || cur == kTcTwoPlayerStage) &&
                       traffic_check_stage(cur, b3host::load<std::uint32_t>(cur + 0x1B8u) & 0x01FFFFFFu);
    // (asked only at a race's first sight; never in Crash mode: its junctions, scoring and records stay the game's)
    unsigned wanted = stage && !latch_.in_stage() && query_ ? traffic_check_normal(query_()) : kTcOff;
    if (wanted != kTcOff && (b3host::call_guest<int>(traffic_check_detail::kIsCrashMode, traffic_check_detail::kGame) & 0xFF) != 0) wanted = kTcOff;
    const bool changed = latch_.step(stage, wanted);
    if (changed) reset_race();
    return changed;
}

inline void TrafficCheck::reset_race() {
    owned_.clear();
    chains_ = {};
    pending_ = Pending{};
    spare_car_ = 0;
    text_override_ = 0;
}

inline bool TrafficCheck::install(HookTable& hooks) {
    using namespace traffic_check_detail;
    const std::vector<Hook> set = {{kVehicleTraffic, HookLayer::kPort, &vehicle_traffic, &vehicle_traffic_},
                                   {kStartRaceCarCrashing, HookLayer::kPort, &start_crashing, &start_crashing_},
                                   {kBodyBody, HookLayer::kPort, &body_body, &body_body_},
                                   {kCrashTrafficBody, HookLayer::kPort, &crash_traffic, &crash_traffic_},
                                   {kPostEffects, HookLayer::kPort, &post_effects, &post_effects_},
                                   {kMessagesUpdate, HookLayer::kPort, &messages_update, &messages_update_},
                                   {kUpdateMessage, HookLayer::kPort, &update_message, &update_message_},
                                   {kGetString, HookLayer::kPort, &get_string, &get_string_},
                                   {kShowScore, HookLayer::kPort, &show_score, &show_score_},
                                   {kRecords, HookLayer::kPort, &records, &records_},
                                   {kProfileCounts, HookLayer::kPort, &profile_counts, &profile_counts_}};
    for (const Hook& h : set)
        if (!hooks.has_row(h.address)) {
            std::fprintf(stderr, "traffic_check: %08X is not native: TRAFFIC CHECKING off\n", h.address);
            return false;
        }
    std::string error;
    if (!hooks.install("port:traffic_check", set, &error)) {
        std::fprintf(stderr, "traffic_check: %s: TRAFFIC CHECKING off\n", error.c_str());
        return false;
    }
    instance() = this;
    return true;
}

inline bool TrafficCheck::region_ok() {
    using namespace traffic_check_detail;
    if (region_checked_) return !region_bad_;
    region_checked_ = true;
    for (b3host::gaddr a = kRegion; a < kOwnEnd; a += 4)
        if (b3host::load<std::uint32_t>(a) != 0) {
            std::fprintf(stderr, "traffic_check: guest RAM %08X..%08X is in use (%08X): TRAFFIC CHECKING off\n", kRegion, kOwnEnd, a);
            region_bad_ = true;
            return false;
        }
    return true;
}

inline void TrafficCheck::write_texts() {
    using namespace traffic_check_detail;
    const std::string code = language_ ? language_() : std::string();
    if (code == texts_for_) return;
    texts_for_ = code;
    const TcWords& w = traffic_check_words(code);
    write_utf16(kCaption, w.caption, kCaptionBytes);
}

inline bool TrafficCheck::active() {
    using namespace traffic_check_detail;
    refresh();
    if (!latch_.latched() || !region_ok()) return false;
    return (b3host::call_guest<int>(kIsCrashMode, kGame) & 0xFF) == 0;
}

inline TrafficCheck::Owned* TrafficCheck::owned(b3host::gaddr wreck) {
    using namespace traffic_check_detail;
    for (Owned& o : owned_) {
        if (o.wreck != wreck) continue;
        if (b3host::load<std::uint8_t>(wreck + kCrashing) == 0) return nullptr;   // (the slot released)
        const float now = f32(o.checker + kClock), age = now - o.at;
        return age >= 0.0f && age <= kOwnedSeconds ? &o : nullptr;
    }
    return nullptr;
}

inline TrafficCheck::Owned* TrafficCheck::checked(b3host::gaddr wreck) {
    using namespace traffic_check_detail;
    for (Owned& o : owned_)
        if (o.wreck == wreck) return b3host::load<std::uint8_t>(wreck + kCrashing) != 0 ? &o : nullptr;
    return nullptr;
}

inline void TrafficCheck::set_owner(b3host::gaddr wreck, b3host::gaddr checker, int depth) {
    using namespace traffic_check_detail;
    for (std::size_t i = 0; i < owned_.size();)
        if (owned_[i].wreck == wreck) owned_.erase(owned_.begin() + std::ptrdiff_t(i));
        else ++i;
    owned_.push_back(Owned{wreck, checker, f32(checker + kClock), depth});
}

inline void TrafficCheck::chain_own(b3host::gaddr wreck, const Owned& by, const char* how) {
    if (!wreck || wreck == by.wreck) return;
    if (const Owned* o = checked(wreck); o && (o->checker == by.checker || o->depth == 0)) return;   // (its own, or another's checked car)
    const Owned keep = by;   // (set_owner may move the entries)
    set_owner(wreck, keep.checker, keep.depth + 1);
    ++counts_.chain_owned;
    static const bool trace = std::getenv("B3_TRAFFIC_CHECK_TRACE") != nullptr;
    if (trace) {
        std::printf("traffic_check: TRACE chain: wreck %08X (%s by wreck %08X, depth %d) is car %08X's: never fatal to it\n", wreck, how,
                    keep.wreck, keep.depth + 1, keep.checker);
        std::fflush(stdout);
    }
}

// E: a hit the game only shunted (VehicleTrafficResponse's non-fatal response done: the car pushed out and bounced, the
// logic told) that the rules make a check: the traffic crashed as the fatal path crashes it [C VehicleTrafficResponse:
// CrashTrafficBody(body, D_0064ED5C's player, the car's +0x218, 1, 1, 1), the wreck's +0x211 set] and pushed as a check
inline void TrafficCheck::check_shunt(b3host::gaddr pair, b3host::gaddr car, b3host::gaddr body) {
    using namespace traffic_check_detail;
    constexpr b3host::gaddr mgr = 0x0055EF70u, kPlayerIndex = 0x0064ED5Cu;
    b3host::call_guest<void>(kCrashTrafficBody, mgr, body, int(b3host::load<std::int8_t>(kPlayerIndex)),
                             int(b3host::load<std::int8_t>(car + 0x218u)), 1, 1, 1);
    if (body_type(body) != 4 || !body_object(body)) return;
    b3host::store<std::uint8_t>(body_object(body) + kHit, 1);
    push(pair);
    const b3host::gaddr rc = ptr(car + kRaceCar);
    // (the credit: the car's own id +0x218 goes to StartTrafficCrashing, the machine's network player index to the wreck's
    // +0x242B [C CrashTrafficBody], as the game's fatal path passes them; the chain is the hitting car's)
    std::printf("traffic_check: shunt check (car %08X, race car %08X, player %d: CrashTrafficBody with network player %d, car id %d)\n", car,
                rc, rc ? int(b3host::load<std::int32_t>(rc + 0x27C0u)) : -1, int(b3host::load<std::int8_t>(kPlayerIndex)),
                int(b3host::load<std::int8_t>(car + 0x218u)));
    std::fflush(stdout);
    set_owner(body_object(body), rc);
    ++counts_.checks;
    ++counts_.shunt_checks;
    event(rc, TcEvent::kCheck);
}

inline TrafficCheck::Chain* TrafficCheck::chain(b3host::gaddr car, bool make) {
    for (Chain& c : chains_) if (c.car == car) return &c;
    if (!make) return nullptr;
    for (Chain& c : chains_) if (c.car == 0) { c = Chain{}; c.car = car; return &c; }
    return nullptr;
}

inline b3host::gaddr TrafficCheck::category(const Chain& c) const {
    return traffic_check_detail::kCategories + 0x14u * unsigned(&c - chains_.data());
}

inline b3host::gaddr TrafficCheck::sign_slot(const Chain& c) const {
    return traffic_check_detail::kSigns + traffic_check_detail::kSignBytes * unsigned(&c - chains_.data());
}

// A sign of ours: OnAward of PERFECT LAP (the game's priority and its refusal while the checker crashes), its number ours
// whatever the mode's scoring keeps, its text the chain's (UpdateMessage)
inline void TrafficCheck::show_sign(Chain& c, TcSign s, int value) {
    if (c.queue.size() >= 4) c.queue.erase(c.queue.begin());   // (a burst: the oldest waiting one dropped)
    c.queue.push_back(Queued{s, value});
}

inline bool TrafficCheck::award_sign(Chain& c, TcSign s, int value) {
    using namespace traffic_check_detail;
    const b3host::gaddr score = c.car + kScore;
    const int award = b3host::call_guest<int>(kOnAward, score + kScoreBonus, int(kPerfectLapEvent), value, category(c));
    if (!(award & 0xFF)) return false;
    if (b3host::load<std::uint32_t>(score + kBonusEvent) == kPerfectLapEvent) b3host::store<std::int32_t>(score + kBonusValue, value);
    c.sign_pending = true;
    c.sign = s;
    c.sign_value = value;
    c.sign_time = f32(score + kBonusTime);
    return true;
}

// An event of `car`'s chain: its points and boost (a shot only kShotGrace after the same one), the category's value and
// star level (the game's near-miss levels), GOOD / GREAT / EXTREME CHECKING as it reaches them, a shot's sign
inline void TrafficCheck::event(b3host::gaddr car, TcEvent e) {
    using namespace traffic_check_detail;
    Chain* c = chain(car, true);
    if (!c) return;
    const float now = f32(car + kClock);
    if (c->c.over(now)) end_chain(*c, now);
    if (!c->c.shot_ready(e, now)) return;   // (the same shot again within its grace)
    c->c.add(e, now);
    b3host::call_guest<void>(kAddBurn, car + kBurn, traffic_event_boost(e, f32(kNearMissBurn)));
    const b3host::gaddr cat = category(*c), near_miss = car + kScore + kScoreNearMiss;
    const b3host::gaddr thr = ptr(near_miss + 0xCu);
    const int levels = b3host::load<std::int8_t>(near_miss + 0x13u);
    float t[8] = {};
    const int n = thr && levels > 0 && levels <= 8 ? levels : 0;
    for (int i = 0; i < n; ++i) t[i] = f32(thr + 4u * unsigned(i));
    const float value = float(c->c.events);
    const std::int8_t level = std::int8_t(traffic_star_level(value, t, n));
    b3host::store<float>(cat + 0x8u, f32(cat));                       // the previous value
    b3host::store<float>(cat + 0x0u, value);
    b3host::store<float>(cat + 0x4u, now);
    b3host::store<std::uint32_t>(cat + 0xCu, thr);
    b3host::store<std::uint8_t>(cat + 0x10u, 1);
    const std::int8_t was = b3host::load<std::int8_t>(cat + 0x11u);
    if (level != was) b3host::store<std::int8_t>(cat + 0x12u, was);
    b3host::store<std::int8_t>(cat + 0x11u, level);
    b3host::store<std::int8_t>(cat + 0x13u, std::int8_t(n));
    TcSign sign;
    const bool shot = e != TcEvent::kCheck;
    const bool up = traffic_level_sign(c->c.level, level, sign);
    c->c.level = std::max<int>(c->c.level, level);
    if (up && sign == TcSign::kExtremeChecking) c->c.extreme_at = now;
    if (shot) show_sign(*c, traffic_shot_sign(e), traffic_event_points(e));
    if (up) show_sign(*c, sign, c->c.points);
    std::printf("traffic_check: %s +%d (car %08X: chain %d, %d points, level %d%s)\n", traffic_event_name(e), traffic_event_points(e), car,
                c->c.events, c->c.points, int(level), up ? (sign == TcSign::kGood ? ": GOOD CHECKING" : sign == TcSign::kGreat ? ": GREAT CHECKING" : ": EXTREME CHECKING") : "");
    std::fflush(stdout);
}

// The chain's end: the sign with its total, the category back to none as the game's near-miss chain leaves its own
inline void TrafficCheck::end_chain(Chain& c, float now) {
    using namespace traffic_check_detail;
    const b3host::gaddr cat = category(c), score = c.car + kScore;
    const std::uint32_t pending = b3host::load<std::uint32_t>(score + kBonusEvent);
    const int crashing = b3host::load<std::uint8_t>(c.car + kCarCrashing);
    show_sign(c, TcSign::kTotal, c.c.points);
    ++counts_.chains;
    std::printf("traffic_check: chain over (car %08X: %d events, %d points; pending event %u, crashing %d)\n", c.car,
                c.c.events, c.c.points, pending, crashing);
    std::fflush(stdout);
    b3host::store<float>(cat + 0x8u, f32(cat));
    b3host::store<std::int8_t>(cat + 0x12u, b3host::load<std::int8_t>(cat + 0x11u));
    b3host::store<float>(cat + 0x0u, 0.0f);
    b3host::store<float>(cat + 0x4u, now);
    b3host::store<std::uint8_t>(cat + 0x10u, 0);
    b3host::store<std::int8_t>(cat + 0x11u, -1);
    c.c = TcChain{};
}

// VehicleTrafficResponse(manager, pair): a check candidate marked for the calls it makes; a checked wreck's hit on traffic
inline void TrafficCheck::vehicle_traffic(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_check_detail;
    TrafficCheck* p = instance();
    p->refresh();
    const b3host::gaddr pair = p->latch_.latched() ? ga(a.gpr[1].w[0]) : 0u;   // (no race of ours: the game's, at once)
    const b3host::gaddr ba = pair ? ptr(pair + 0x24u) : 0u, bb = pair ? ptr(pair + 0x28u) : 0u;
    if (!ba || !bb || body_type(bb) != 3 || (body_type(ba) != 2 && body_type(ba) != 4) || !p->active()) {
        p->vehicle_traffic_(a, r);
        return;
    }
    const b3host::gaddr tv = body_object(bb);
    if (body_type(ba) == 2 && !checker_ok(ba)) {   // (the trace: not able to check now)
        const TraceCtx saved = p->ctx_;
        p->ctx_ = TraceCtx{1, "checker not able (crashing, AI-driven or finished)"};
        p->vehicle_traffic_(a, r);
        p->ctx_ = saved;
        return;
    }
    if (body_type(ba) == 2 && checker_ok(ba) && tv) {   // a check candidate
        const b3host::gaddr car = body_object(ba), m = ptr(car + kMatrix);
        const int cat = b3host::load<std::int8_t>(tv + kTrafficCategory);
        const bool same = m && traffic_checkable(p->latch_.mode(), cat, f32(m + kMatAt), f32(m + kMatAt + 8u), f32(tv + kTrafficHeading),
                                                 f32(tv + kTrafficHeading + 8u));
        // (the trace's reason: the rule that keeps this hit from being a check, should the game make it fatal)
        const char* why = !same ? (p->latch_.mode() == kTcRevenge && !traffic_checkable_category(cat) ? "vehicle (heavy / special)" : "direction")
                          : !traffic_check_speed_ok(p->latch_.mode(), f32(car + kSpeed)) ? "speed below 75 mph"
                          : free_wreck_slots() < 2 ? "wreck slots full" : "none (a check candidate)";
        const TraceCtx saved = p->ctx_;
        p->ctx_ = TraceCtx{1, why};
        struct Restore { TrafficCheck* p; TraceCtx c; ~Restore() { p->ctx_ = c; } } restore{p, saved};
        if (same && traffic_check_speed_ok(p->latch_.mode(), f32(car + kSpeed))) {
            if (free_wreck_slots() < 2) {
                ++p->counts_.not_checked_pool;
                p->vehicle_traffic_(a, r);
                return;
            }
            p->pending_ = Pending{pair, car, bb, b3host::load<std::uint8_t>(car + kHit), false};
            p->vehicle_traffic_(a, r);
            const Pending d = p->pending_;
            p->pending_ = Pending{};
            if (!d.checked) {   // the game's shunt: a check from 75 mph (E)
                if (traffic_check_shunt_ok(p->latch_.mode(), f32(car + kSpeed)) && b3host::load<std::uint8_t>(pair + 0x2Cu) != 0 &&
                    body_type(bb) == 3 && b3host::load<std::uint8_t>(car + kCrashing) == 0 && free_wreck_slots() >= 2)
                    p->check_shunt(pair, car, bb);
                return;
            }
            b3host::store<std::uint8_t>(car + kHit, d.c211);
            const b3host::gaddr wreck = body_type(bb) == 4 ? body_object(bb) : 0;
            const b3host::gaddr rc = ptr(car + kRaceCar);
            if (wreck) p->set_owner(wreck, rc);
            ++p->counts_.checks;
            p->event(rc, TcEvent::kCheck);
            return;
        }
        p->vehicle_traffic_(a, r);
        return;
    }
    // a checked wreck (or its chain's) against traffic: what it wrecks is its checker's too (never fatal to it); a checked
    // car's own (depth 0, its 10 s) scores for its checker
    const Owned* any = body_type(ba) == 4 ? p->checked(body_object(ba)) : nullptr;
    if (!any || !tv) {
        p->vehicle_traffic_(a, r);
        return;
    }
    const Owned by = *any;
    const bool scores = by.depth == 0 && p->owned(body_object(ba)) != nullptr;
    const int cat = b3host::load<std::int8_t>(tv + kTrafficCategory);
    const float hx = f32(tv + kTrafficHeading), hz = f32(tv + kTrafficHeading + 8u);
    const b3host::gaddr checker = by.checker;
    p->vehicle_traffic_(a, r);
    if (body_type(bb) != 4) return;   // (not wrecked)
    p->chain_own(body_object(bb), by, "wrecked");
    if (!scores) return;
    bool oncoming = false;
    for (unsigned i = 0, n = b3host::load<std::uint32_t>(0x0055EF70u + kBodyCount); i < n && i < kMaxBodies; ++i) {   // the checker's heading
        const b3host::gaddr b = 0x0055EF70u + kBodies + i * 0x30u;
        if (body_type(b) < 3 && ptr(body_object(b) + kRaceCar) == checker) {
            const b3host::gaddr m = ptr(body_object(b) + kMatrix);
            oncoming = m && traffic_oncoming(f32(m + kMatAt), f32(m + kMatAt + 8u), hx, hz);
            break;
        }
    }
    TcEvent e;
    if (!traffic_wreck_event(cat, oncoming, e)) return;
    ++(e == TcEvent::kExtremeShot ? p->counts_.extreme_shots : e == TcEvent::kSkillShot ? p->counts_.skill_shots : p->counts_.trick_shots);
    p->event(checker, e);
}

// StartRaceCarCrashing(director, car, id, pair, info): not made for the check under way, nor for a car touching its own
// checked wreck
inline void TrafficCheck::start_crashing(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCheck* p = instance();
    const b3host::gaddr car = traffic_check_detail::ga(a.gpr[1].w[0]), pair = traffic_check_detail::ga(a.gpr[3].w[0]);
    if (p->pending_.pair && !p->pending_.checked && car == p->pending_.car && pair == p->pending_.pair) {
        p->pending_.checked = true;
        return;
    }
    if (p->spare_car_ && car == p->spare_car_) {
        p->spared_ = true;
        return;
    }
    const bool watch = p->latch_.latched() && car;
    const bool was = watch && b3host::load<std::uint8_t>(car + traffic_check_detail::kCrashing) != 0;
    p->start_crashing_(a, r);
    if (watch && !was && b3host::load<std::uint8_t>(car + traffic_check_detail::kCrashing) != 0) p->trace_crash(car, pair);
}

// A human player's car crashed in a race with the option: counted when on traffic or a wreck; the trace
// ($B3_TRAFFIC_CHECK_TRACE) logs every one, by which path and why it was not a check
inline void TrafficCheck::trace_crash(b3host::gaddr car, b3host::gaddr pair) {
    using namespace traffic_check_detail;
    constexpr b3host::gaddr mgr = 0x0055EF70u;
    bool human = false;
    for (unsigned i = 0, n = b3host::load<std::uint32_t>(mgr + kBodyCount); i < n && i < kMaxBodies; ++i) {
        const b3host::gaddr b = mgr + kBodies + i * 0x30u;
        if (body_type(b) == 2 && body_object(b) == car) { human = true; break; }
    }
    if (!human) return;
    const b3host::gaddr ba = pair ? ptr(pair + 0x24u) : 0u, bb = pair ? ptr(pair + 0x28u) : 0u;
    const b3host::gaddr other = ba && body_object(ba) == car ? bb : ba;
    const int ot = other ? body_type(other) : -1;
    if (ot == 3) ++counts_.player_traffic_crashes;
    if (ot == 4) ++counts_.player_wreck_crashes;
    static const bool trace = std::getenv("B3_TRAFFIC_CHECK_TRACE") != nullptr;
    if (!trace) return;
    const b3host::gaddr oo = other ? body_object(other) : 0u, rc = ptr(car + kRaceCar);
    const float mph = 2.236936f, speed = f32(car + kSpeed) * mph, factor = f32(car + 0x1534u);
    int cat = -1;
    float dot = 0.0f, closing = -1.0f;
    const b3host::gaddr m = ptr(car + kMatrix);
    if (ot == 3 && oo) {
        cat = b3host::load<std::int8_t>(oo + kTrafficCategory);
        if (m) dot = f32(m + kMatAt) * f32(oo + kTrafficHeading) + f32(m + kMatAt + 8u) * f32(oo + kTrafficHeading + 8u);
        if (m) {   // the game's closing speed [C VehicleTrafficResponse: traffic heading * its speed - the car's at * its speed, on the normal]
            const float ts = f32(oo + 0xC4u), cs = f32(car + kSpeed);
            const float rx = f32(oo + kTrafficHeading) * ts - f32(m + kMatAt) * cs, rz = f32(oo + kTrafficHeading + 8u) * ts - f32(m + kMatAt + 8u) * cs;
            closing = std::fabs(mph * (rx * f32(pair + 0x10u) + rz * f32(pair + 0x18u)));
        }
    } else if (ot == 4 && oo) {
        cat = b3host::load<std::int32_t>(oo + kWreckCategory);
        const b3host::gaddr mw = ptr(oo + kMatrix);
        if (m && mw) dot = f32(m + kMatAt) * f32(mw + kMatAt) + f32(m + kMatAt + 8u) * f32(mw + kMatAt + 8u);
    }
    const char* path = ctx_.path == 1 ? "VehicleTrafficResponse" : ctx_.path == 2 ? "BodyBodyResponse" : "another path";
    const char* why = ctx_.why;
    std::string wreck_note;
    if (ot == 4 && oo) {
        bool listed = false;
        for (const Owned& o : owned_)
            if (o.wreck == oo) {
                listed = true;
                char t[96];
                std::snprintf(t, sizeof t, "a checked car (by %08X, %.1f s ago%s)", o.checker, double(f32(o.checker + kClock) - o.at),
                              o.checker == rc ? ", its own: past the 10 s" : "");
                wreck_note = t;
            }
        if (!listed) wreck_note = "a wreck no check of ours made (crashed traffic)";
    }
    const char* kind = ot == 3 ? "traffic" : ot == 4 ? "wreck" : ot >= 0 && ot < 3 ? "race car" : ot < 0 ? "the world (no pair: a wall, the ground)" : "a body";
    std::printf("traffic_check: TRACE player crash (car %08X, clock %.2f, %s): by %s on %s (body type %d, category %d%s%s), speed %.1f mph,"
                " heading dot %.2f, closing %.1f mph (fatal above %.1f), wreck slots free %u: %s\n",
                car, double(rc ? f32(rc + kClock) : 0.0f), traffic_check_name(latch_.mode()), path, kind, ot, cat,
                wreck_note.empty() ? "" : ", ", wreck_note.c_str(), double(speed), double(dot), double(closing), double(75.0f * factor),
                free_wreck_slots(), why ? why : ot == 4 ? (latch_.mode() == kTcExtreme ? "another player's checked car, or the car not able to check"
                                                                                  : "crashed traffic (Revenge checks live traffic only)")
                                         : "not a traffic hit");
    std::fflush(stdout);
}

// The push of the checked wreck (the pair: the checker's car at +0x24, the wreck at +0x28; its normal from the wreck to
// the car): the wreck's velocity becomes the car's plus the push along the normal from the car, a little spin from the
// contact; the car a small recoil
inline void TrafficCheck::push(b3host::gaddr pair) {
    using namespace traffic_check_detail;
    const b3host::gaddr car = body_object(ptr(pair + 0x24u)), wreck = body_object(ptr(pair + 0x28u));
    const b3host::gaddr mc = ptr(car + kMatrix), mw = ptr(wreck + kMatrix);
    if (!car || !wreck || !mc || !mw) return;
    float nx = -f32(pair + 0x10u), nz = -f32(pair + 0x18u);
    const float len = std::sqrt(nx * nx + nz * nz);
    if (len > 1e-4f) { nx /= len; nz /= len; }
    else { nx = f32(mc + kMatAt); nz = f32(mc + kMatAt + 8u); }   // (no normal: along the car)
    const float rvx = f32(car + kVel) - f32(wreck + kVel), rvz = f32(car + kVel + 8u) - f32(wreck + kVel + 8u);
    const float closing = rvx * nx + rvz * nz;
    const float mw_ = f32(wreck + kMass), mc_ = f32(car + kMass);
    // its velocity becomes kLaunch times the car's (Revenge's launch) plus kSide along the normal
    const float vx = kLaunch * f32(car + kVel) + kSide * nx, vz = kLaunch * f32(car + kVel + 8u) + kSide * nz;
    const float jx = mw_ * (vx - f32(wreck + kVel)), jz = mw_ * (vz - f32(wreck + kVel + 8u));
    const float s = std::sqrt(vx * vx + vz * vz);
    add_f32(wreck + kLinear, jx);
    add_f32(wreck + kLinear + 8u, jz);
    const float cx = 0.5f * (f32(mc + kMatPos) + f32(mw + kMatPos)), cz = 0.5f * (f32(mc + kMatPos + 8u) + f32(mw + kMatPos + 8u));
    const float rx = cx - f32(mw + kMatPos), rz = cz - f32(mw + kMatPos + 8u);
    add_f32(wreck + kAngular + 4u, kSpinGain * (rz * jx - rx * jz));
    const float rec = mc_ * traffic_recoil_speed(closing);
    add_f32(car + kLinear, -nx * rec);
    add_f32(car + kLinear + 8u, -nz * rec);
    b3host::store<float>(pair + 0x0u, cx);
    b3host::store<float>(pair + 0x4u, f32(mc + kMatPos + 4u) + 0.1f);
    b3host::store<float>(pair + 0x8u, cz);
    b3host::store<std::uint8_t>(pair + 0x2Cu, 1);
    std::printf("traffic_check: check (car %08X, wreck %08X: closing %.1f m/s, launched at %.1f m/s)\n", car, wreck, double(closing), double(s));
    std::fflush(stdout);
}

// BodyBodyResponse(manager, pair): the check's own pair pushed; a checked wreck touched by its checker (never fatal to it)
// or by another race car (crashed, the takedown the checker's)
inline void TrafficCheck::body_body(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_check_detail;
    TrafficCheck* p = instance();
    const b3host::gaddr pair = ga(a.gpr[1].w[0]);
    if (p->pending_.checked && pair == p->pending_.pair) {
        p->push(pair);
        return;
    }
    static const bool test = std::getenv("B3_TRAFFIC_CHECK_TEST_OWNED") != nullptr;
    const TraceCtx saved = p->ctx_;
    p->ctx_ = TraceCtx{2, nullptr};
    struct Restore { TrafficCheck* p; TraceCtx c; ~Restore() { p->ctx_ = c; } } restore{p, saved};
    if (!p->latch_.latched() || !pair) {
        p->body_body_(a, r);
        return;
    }
    const b3host::gaddr ba = ptr(pair + 0x24u), bb = ptr(pair + 0x28u);
    if (ba && bb && body_type(ba) == 4 && body_type(bb) == 4 && !p->owned_.empty()) {   // two wrecks: the chain goes on
        const b3host::gaddr wa = body_object(ba), wb = body_object(bb);
        p->body_body_(a, r);
        if (b3host::load<std::uint8_t>(pair + 0x2Cu) == 0) return;
        const Owned* oa = p->checked(wa);
        const Owned* ob = p->checked(wb);
        if (oa && !ob) p->chain_own(wb, *oa, "knocked");
        else if (ob && !oa) p->chain_own(wa, *ob, "knocked");
        return;
    }
    b3host::gaddr race = 0, wreck_body = 0;
    if (ba && bb && body_type(ba) >= 0 && body_type(ba) < 3 && body_type(bb) == 4) { race = ba; wreck_body = bb; }
    else if (ba && bb && body_type(bb) >= 0 && body_type(bb) < 3 && body_type(ba) == 4) { race = bb; wreck_body = ba; }
    if (!race || (p->owned_.empty() && !test && p->latch_.mode() != kTcExtreme)) {
        p->body_body_(a, r);
        return;
    }
    const b3host::gaddr wreck = body_object(wreck_body);
    Owned* o = p->owned(wreck);
    // a human player's car able to check: its own checked car (any age) or, in Extreme, a wreck not another player's
    // checked car now: the touch spared (the game's own response: BodyBodyResponse moves and pushes only the wreck while
    // the race car is not crashing [C its ta = 2]), the wreck the player's; a check when the touch was fatal and the
    // wreck was not the player's already
    if (body_type(race) == 2 && checker_ok(race) && p->active()) {
        const b3host::gaddr rphys = body_object(race), rcar = ptr(rphys + kRaceCar);
        const Owned* any = p->checked(wreck);
        const TcWreckTouch t = traffic_wreck_touch(p->latch_.mode(), any && any->checker == rcar, o && o->checker != rcar && o->depth == 0);
        const int depth = any ? any->depth : 0;
        if (t != TcWreckTouch::kGame) {
            p->spare_car_ = rphys;
            p->spared_ = false;
            p->body_body_(a, r);
            p->spare_car_ = 0;
            if (!p->spared_) return;   // (not fatal: nothing to spare)
            p->spared_ = false;
            if (t == TcWreckTouch::kOwn) {
                p->set_owner(wreck, rcar, depth);
                ++p->counts_.own_touches;
                static const bool trace = std::getenv("B3_TRAFFIC_CHECK_TRACE") != nullptr;
                if (trace) {
                    std::printf("traffic_check: TRACE spared: car %08X touched its own %s (wreck %08X, depth %d): no crash\n", rphys,
                                depth == 0 ? "checked car" : "checked car's chain", wreck, depth);
                    std::fflush(stdout);
                }
                return;
            }
            p->set_owner(wreck, rcar);
            ++p->counts_.checks;
            ++p->counts_.wreck_checks;
            std::printf("traffic_check: check of a wreck (car %08X, wreck %08X: spared, the wreck the player's)\n", rphys, wreck);
            std::fflush(stdout);
            p->event(rcar, TcEvent::kCheck);
            return;
        }
    }
    if (o && o->depth != 0) o = nullptr;   // (a chain's wreck: no takedowns of its own)
    if (!o) o = p->test_owned(wreck);
    if (!o) {
        p->body_body_(a, r);
        return;
    }
    const b3host::gaddr rphys = body_object(race), rcar = ptr(rphys + kRaceCar);
    if (rcar == o->checker) {   // its own checked wreck (the checker not able to check now): never crashed by it
        p->spare_car_ = rphys;
        p->spared_ = false;
        p->body_body_(a, r);
        p->spare_car_ = 0;
        if (p->spared_) ++p->counts_.own_touches;
        p->spared_ = false;
        return;
    }
    const bool was_crashing = b3host::load<std::uint8_t>(rphys + kCrashing) != 0;
    const b3host::gaddr checker = o->checker;
    p->body_body_(a, r);
    if (was_crashing || !rcar || b3host::load<std::uint8_t>(pair + 0x2Cu) == 0) return;
    if (b3host::load<std::uint8_t>(rphys + kCrashing) == 0) {
        if (f32(rphys + kGrace) >= 0.0f || b3host::load<std::int32_t>(rcar + kFinish) == 3) return;   // (just reset, or finished)
        // its crash as the game starts one, the pair with the race car first (as StartRaceCarCrashing reads it)
        const bool swap = race != ba;
        if (swap) { b3host::store<std::uint32_t>(pair + 0x24u, bb); b3host::store<std::uint32_t>(pair + 0x28u, ba); }
        // (no crash-start info: BodyBodyResponse makes one only when the other body is a race car [C its fatal path], so a
        // race car's crash on a wreck has none)
        b3host::call_guest<void>(kStartRaceCarCrashing, kDirector, rphys, int(b3host::load<std::int8_t>(rcar + kCarIndex)), pair, 0u);
        if (swap) { b3host::store<std::uint32_t>(pair + 0x24u, ba); b3host::store<std::uint32_t>(pair + 0x28u, bb); }
        if (b3host::load<std::uint8_t>(rphys + kCrashing) == 0) return;
    }
    // the takedown the checker's: its table as UpdateTakeDownStats writes an aggressor's (the game awards it after its wait,
    // if the checker is not crashing then)
    const int k = b3host::load<std::int8_t>(rcar + kCarIndex);
    // (MORE RIVALS: a victim past the game's 6 has its entries in the split arrays' shadow, b3host/split_array.h)
    if (k < 0 || k >= 8 || b3host::load<std::int32_t>(checker + kFinish) == 3 || b3host::load<std::uint8_t>(checker + kCarCrashing) != 0) return;
    const std::size_t v = std::size_t(k);
    b3host::store<float>(b3host::split::shadow_or_flat(checker + kTakedownTime, 4, 6, v), f32(checker + kClock));
    b3host::store<std::uint8_t>(b3host::split::shadow_or_flat(checker + kTakedownNow, 1, 6, v), 0);
    b3host::store<std::uint8_t>(b3host::split::shadow_or_flat(checker + kTakedownSlam, 1, 6, v), 0);
    ++p->counts_.takedowns;
    std::printf("traffic_check: a checked car took out race car %d (the takedown credited to car %08X)\n", k, checker);
    std::fflush(stdout);
}

inline TrafficCheck::Owned* TrafficCheck::test_owned(b3host::gaddr wreck) {
    using namespace traffic_check_detail;
    static const bool on = [] { const char* e = std::getenv("B3_TRAFFIC_CHECK_TEST_OWNED"); return e && e[0] == '1' && !e[1]; }();
    if (!on || !wreck || b3host::load<std::uint8_t>(wreck + kCrashing) == 0) return nullptr;
    for (const Owned& o : owned_) if (o.wreck == wreck) return nullptr;   // (owned once: its time ran out)
    constexpr b3host::gaddr mgr = 0x0055EF70u;
    for (unsigned i = 0, n = b3host::load<std::uint32_t>(mgr + kBodyCount); i < n && i < kMaxBodies; ++i) {
        const b3host::gaddr b = mgr + kBodies + i * 0x30u;
        if (body_type(b) != 2) continue;
        const b3host::gaddr car = ptr(body_object(b) + kRaceCar);
        if (!car) continue;
        owned_.push_back(Owned{wreck, car, f32(car + kClock)});
        std::printf("traffic_check: TEST: wreck %08X taken as checked by car %08X\n", wreck, car);
        std::fflush(stdout);
        return &owned_.back();
    }
    return nullptr;
}

// CrashTrafficBody(manager, body, ...): the wreck slot it takes is no longer a checked wreck of before
inline void TrafficCheck::crash_traffic(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_check_detail;
    TrafficCheck* p = instance();
    p->crash_traffic_(a, r);
    if (p->owned_.empty()) return;
    const b3host::gaddr body = ga(a.gpr[1].w[0]);
    const b3host::gaddr wreck = body ? body_object(body) : 0;
    for (std::size_t i = 0; i < p->owned_.size();)
        if (p->owned_[i].wreck == wreck) p->owned_.erase(p->owned_.begin() + std::ptrdiff_t(i));
        else ++i;
}

// The wrecks to clear when the slots run short: not linked, behind every player car and 60 m away, or 150 m away
inline void TrafficCheck::clear_wrecks(b3host::gaddr mgr) {
    using namespace traffic_check_detail;
    static const bool no_clear = std::getenv("B3_TRAFFIC_CHECK_NO_CLEAR") != nullptr;   // TEST ONLY: none cleared
    const unsigned used = kWreckSlots - free_wreck_slots();
    if (used < 40 || no_clear) return;
    const bool tight = used >= 56;   // (nearly full: any wreck 40 m away)
    struct Player { float x, z, fx, fz; };
    std::vector<Player> players;
    const unsigned n = b3host::load<std::uint32_t>(mgr + kBodyCount);
    for (unsigned i = 0; i < n && i < kMaxBodies; ++i) {
        const b3host::gaddr b = mgr + kBodies + i * 0x30u;
        if (body_type(b) != 2) continue;
        const b3host::gaddr m = ptr(body_object(b) + kMatrix);
        if (m) players.push_back({f32(m + kMatPos), f32(m + kMatPos + 8u), f32(m + kMatAt), f32(m + kMatAt + 8u)});
    }
    if (players.empty()) return;
    unsigned cleared = 0;
    for (unsigned i = 0; i < n && i < kMaxBodies && cleared < 2; ++i) {
        const b3host::gaddr b = mgr + kBodies + i * 0x30u;
        if (body_type(b) != 4 || b3host::load<std::uint8_t>(b + 1u) != 0) continue;
        const b3host::gaddr w = body_object(b), m = w ? ptr(w + kMatrix) : 0;
        if (!m || ptr(w + kWreckLinked) != 0) continue;
        bool away = true;
        for (const Player& pl : players) {
            const float dx = f32(m + kMatPos) - pl.x, dz = f32(m + kMatPos + 8u) - pl.z, d2 = dx * dx + dz * dz;
            const bool behind = dx * pl.fx + dz * pl.fz < 0.0f;
            if (!(d2 > 150.0f * 150.0f || (behind && d2 > 60.0f * 60.0f) || (tight && d2 > 40.0f * 40.0f))) { away = false; break; }
        }
        if (!away) continue;
        b3host::store<std::uint8_t>(b + 1u, 1);   // (UpdateActiveBodyList takes it out, as a wreck the game marks)
        ++cleared;
        ++counts_.wrecks_cleared;
    }
}

// PostEffectsUpdate(manager) = UpdateActiveBodyList, once a physics frame: the chains' ends, the wrecks to clear
inline void TrafficCheck::post_effects(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_check_detail;
    TrafficCheck* p = instance();
    p->refresh();
    static const bool trace = std::getenv("B3_TRAFFIC_CHECK_TRACE") != nullptr;   // tests: the wreck list's state
    if (trace) {   // (with the option off too: the wreck slots' high-water mark)
        const b3host::gaddr mgr = ga(a.gpr[0].w[0]);
        static int last_n = -1, last_used = -1, last_t4 = -1, high = 0;
        static bool bad = false;
        const int n = b3host::load<std::int32_t>(mgr + 0xE620Cu), used = int(kWreckSlots - free_wreck_slots());
        int t4 = 0;
        const unsigned nb = b3host::load<std::uint32_t>(mgr + kBodyCount);
        for (unsigned i = 0; i < nb && i < kMaxBodies; ++i) t4 += body_type(mgr + kBodies + i * 0x30u) == 4;
        if (n != last_n || used != last_used || t4 != last_t4) {
            std::printf("traffic_check: TRACE wrecks: list %d, slots in use %d, type-4 bodies %d, cleared %llu\n", n, used, t4,
                        static_cast<unsigned long long>(p->counts_.wrecks_cleared));
            last_n = n; last_used = used; last_t4 = t4;
        }
        if (used > high) {
            high = used;
            std::printf("traffic_check: TRACE wreck slots high-water %d (list %d, type-4 bodies %d of %u)\n", high, n, t4, nb);
        }
        for (int i = 0; !bad && i < n && i < 67; ++i) {
            const b3host::gaddr w = ga(b3host::load<std::uint32_t>(mgr + 0xE6100u + 4u * unsigned(i)));
            if (w < mgr + kWrecks || w >= mgr + kWrecks + kWreckSlots * kWreckBytes || (w - mgr - kWrecks) % kWreckBytes) {
                std::printf("traffic_check: TRACE wreck list entry %d of %d is %08X (not a slot)\n", i, n, w);
                bad = true;
            }
        }
        if (!bad && n > int(kWreckSlots)) { std::printf("traffic_check: TRACE wreck list count %d\n", n); bad = true; }
        std::fflush(stdout);
    }
    if (p->latch_.latched() && p->active()) {
        for (Chain& c : p->chains_)
            if (c.car && c.c.over(f32(c.car + kClock))) p->end_chain(c, f32(c.car + kClock));
        p->clear_wrecks(ga(a.gpr[0].w[0]));
    }
    p->post_effects_(a, r);
    // a wreck slot UpdateActiveBodyList released ends its entry, so the slot taken again inherits no check (nor its
    // protection): CrashTrafficBody's own drop (crash_traffic) is for a slot taken again within a frame
    for (std::size_t i = 0; i < p->owned_.size();)
        if (b3host::load<std::uint8_t>(p->owned_[i].wreck + kCrashing) == 0) p->owned_.erase(p->owned_.begin() + std::ptrdiff_t(i));
        else ++i;
}

// MessagesUpdate(bar, dt): slot 3 is the bar's player's TRAFFIC CHECK chain
inline void TrafficCheck::messages_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_check_detail;
    TrafficCheck* p = instance();
    if (p->latch_.latched() && p->region_ok()) {
        const b3host::gaddr bar = ga(a.gpr[0].w[0]), score = ptr(bar + kBarScore);
        Chain* c = score ? p->chain(score - kScore, false) : nullptr;
        if (c) {
            p->write_texts();
            b3host::store<std::uint32_t>(bar + kBarSlotText, kCaption);
            const float dt = std::bit_cast<float>(a.fpr[0]);
            if (b3host::call_guest<int>(kUpdateBoostMessage, bar, int(kCaptionSlot), p->category(*c), dt, 1.0f) & 0xFF)
                b3host::call_guest<void>(kGeneralPopup, kHudSound);
        }
    }
    p->messages_update_(a, r);
}

// UpdateMessage(primary message, dt): a pending sign of ours drawn with our string
inline void TrafficCheck::update_message(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_check_detail;
    TrafficCheck* p = instance();
    Chain* c = nullptr;
    const b3host::gaddr self = ga(a.gpr[0].w[0]);
    if (p->latch_.latched()) {
        const b3host::gaddr score = ptr(self + 0x24u);
        c = score ? p->chain(score - kScore, false) : nullptr;
        // ours only while the pending award is the one we made: another event (a real PERFECT LAP: OnEvent sets the
        // time and keeps the larger value) or none pending makes it the game's
        if (c && c->sign_pending && (b3host::load<std::uint32_t>(score + kBonusEvent) != kPerfectLapEvent ||
                                     b3host::load<std::uint8_t>(score + kBonusPending) == 0 ||
                                     b3host::load<std::int32_t>(score + kBonusValue) != c->sign_value ||
                                     std::bit_cast<std::uint32_t>(f32(score + kBonusTime)) != std::bit_cast<std::uint32_t>(c->sign_time)))
            c->sign_pending = false;
        // the next waiting sign, when the game's sign is free by its own rule [C UpdateMessage: no event pending, and no
        // message up (+0x4C) or the one up shown its least time (+0x190 >= +0x194)]
        if (c && !c->sign_pending && !c->queue.empty() && b3host::load<std::uint8_t>(score + kBonusPending) == 0 &&
            (b3host::load<std::uint32_t>(self + 0x4Cu) == 0 || f32(self + 0x190u) >= f32(self + 0x194u))) {
            const Queued q = c->queue.front();
            c->queue.erase(c->queue.begin());
            if (p->award_sign(*c, q.sign, q.value)) {
                const TcWords& w = traffic_check_words(p->language_ ? p->language_() : std::string());
                write_utf16(p->sign_slot(*c), w.sign[unsigned(q.sign)], kSignBytes);
            }
        }
        if (c && !c->sign_pending) c = nullptr;
    }
    if (!c) {
        p->update_message_(a, r);
        return;
    }
    p->write_texts();
    p->text_override_ = p->sign_slot(*c);
    p->update_message_(a, r);
    p->text_override_ = 0;
    const b3host::gaddr score = c->car + kScore;
    if (b3host::load<std::uint8_t>(score + kBonusPending) == 0) {
        c->sign_pending = false;
        ++p->counts_.signs;
        std::printf("traffic_check: sign %u shown (car %08X)\n", unsigned(c->sign), c->car);
        std::fflush(stdout);
    }
}

// GetString(table, id): PERFECT LAP's string is the sign's while UpdateMessage draws a sign of ours
inline void TrafficCheck::get_string(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCheck* p = instance();
    if (p->text_override_ && a.gpr[1].w[0] == traffic_check_detail::kPerfectLapString) {
        r.v0 = u_long128{};
        r.v0.w[0] = p->text_override_;
        return;
    }
    p->get_string_(a, r);
}

// ShowScore(hud score, event, value): not for our sign (its number is the chain's; the mode's own SCORE counter, which a
// race never shows, stays hidden)
inline void TrafficCheck::show_score(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCheck* p = instance();
    if (p->text_override_) return;
    p->show_score_(a, r);
}

// CB3Records::Update 0x3D1090(records, kinds, p1) [C func_003D1090; as game_options.h / race_options.h]: in a race with
// the option the kinds (a1) are 0, then the last update set as an update that set no record leaves it
inline void TrafficCheck::records(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_check_detail;
    TrafficCheck* p = instance();
    p->refresh();
    if (!p->latch_.latched()) {
        p->records_(a, r);
        return;
    }
    const std::uint64_t kinds = records_without_kinds(p->records_, a, r);   // (records_guard.h)
    ++p->counts_.records_skipped;
    std::printf("traffic_check: a race with TRAFFIC CHECKING: the record kinds %llu not written (no records kept)\n",
                static_cast<unsigned long long>(kinds));
    std::fflush(stdout);
}

// 0x136F30 (whether the race counts for the profile): not with the option
inline void TrafficCheck::profile_counts(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCheck* p = instance();
    p->profile_counts_(a, r);
    if ((r.v0.w[0] & 0xFFu) == 0) return;
    p->refresh();
    if (!p->latch_.latched()) return;
    if (p->counts_.profile_skipped++ == 0) {
        std::printf("traffic_check: a race with TRAFFIC CHECKING: the profile's totals and unlocks left as they were\n");
        std::fflush(stdout);
    }
    r.v0 = u_long128{};
}

}  // namespace b3native

#endif
