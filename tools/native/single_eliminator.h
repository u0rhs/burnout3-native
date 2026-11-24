// single_eliminator.h -- elim1 (2026-11-21): ELIMINATOR as a SINGLE EVENT mode, World Tour's Eliminator on any race
// track with the game's own rules. Always on (B3NATIVE_ELIMINATOR=0: retail's four modes, for comparisons).
//
// What the game has [C the decomp; addresses retail]:
// - SELECT MODE: the CB3GenericVSelectPage 0x01E94E4C (up to 10 rows: count +4, selection +8, title +0xC, string ids
//   +0x10, two disabled-flag bytes per row at +0x38 / +0x42), built by func_003C9210 and drawn by its Prepare
//   func_003C98D0; the Single Event state's Action func_003C9340 turns the confirmed row into CB3MenuChoices' mode
//   (0x01E900B0 +8: 0 race, 1 time attack, 2 road rage, 3 crash; +4 is the top menu, 1 for Single Event) and opens
//   SELECT OPTIONS for that mode (func_00432AB0). Rows and modes: eliminator_rows.h.
// - The race: CB3MenuChoices::RequestOnePlayerGameMode 0x30C2C0 fills the stage record 0x0051AD00 for the mode: +0x1B4
//   the logic object inside CB3Game (race: CB3OfflineSingleRaceLogic 0x0051B258), +0xA4 the AI cars, +0x68 the track's
//   GAMEDATA.BGD stage ("OffSgRcF" for a race), +0x1B8 0x0051B920 (World Tour's stages: 0x0051B928, func_0030C560).
//   World Tour's Eliminator events use the same record with +0x1B4 = CB3OfflineLapEliminatorLogic 0x0051B2F0 (vtable
//   0x4DECE0) and 5 rivals. That logic is the whole mode: OnWorldPrepared 0x38BE40 sets each car's laps to the cars
//   less one; OnRaceCarStartNewLap 0x38BD30 puts out the one car with the fewest laps (0x38BB70: SetFinished, its
//   crash, the others' bonus); its HUD (4), results page (4: ELIMINATOR RESULTS, func_0042FE90) and score mode flags
//   (the single race's) come from its vtable. The track data has no Eliminator stage ("OffLpElF" is named in the ELF's
//   CB3NormalGameModes table, but no GAMEDATA.BGD holds it): the race keeps "OffSgRcF".
// - So ELIMINATOR is the race mode (the RIVALS row as RACE's; 0 is raised to 1 on SELECT OPTIONS' Continue, before the
//   game picks the rivals' cars for the count) with the logic swapped after the
//   game built the record.
// - Profile: func_00136F30 answers "does not count" for it (below), so its results add no Burnout points (World Tour's
//   event unlock thresholds), totals or unlocks (a profile diff of a counted one: Burnout points +230, slams, race events,
//   the car's events; no medal or unlock byte).
// - Records: after the race func_0042F0F0 gives CB3Records::Update 0x3D1090 the results page's record kinds; the
//   Eliminator's page asks for 13 (best race time, best lap, ...), written into the TRACK's tables (36 tracks x 4
//   tables), the ones RACE uses: the game has no Eliminator table. Eliminators run 1..5 laps (not RACE's), so a Single
//   Event Eliminator writes no records (kinds 0, as a race the game does not record); the results page, profile totals
//   and unlocks are the game's.
// The row's text: string 273 (World Tour's "ELIMINATOR"), from the game's own table: every language has it. The game's
// SELECT MODE has no help line for its modes (string 282, World Tour's "LAST PLACE IN EACH LAP IS ELIMINATED", is left
// unused). The red title of SELECT OPTIONS and SELECT CAR (type 14 headings) is the heading table's (DATA/HEADxx.BIN,
// D_0051BA48: 11 strings, no Eliminator) string that func_0031ABC0 picks from the page and the mode (Single Event's
// race: 5, RACE); while such a heading is prepared for the ELIMINATOR row (CB3HeadingComponent::Prepare(EGlobalStrings,
// type) 0x319C80 or Prepare(text, type) 0x319A90; their Prepare(type) 0x319E80, which reads the title, is a direct native
// call) the lookup func_00271A00 of heading 5 is answered with string 273.
// The choice ends on the main menu (its page's Prepare 0x318340) and on SELECT MODE's Back; another row's Confirm ends it
// too. The port's own records: the game sees a race, so native_play gives the renderer the record word
// EliminatorRows::record_word (bit 14 of raceopts' 'HTD1' options word, src/b3replay/records.h; with raceopts' own word
// when its options are not the game's, one set_race_options call) while an Eliminator stage is on: its ghosts and best
// times go to "options-eliminator..." (e.g. options-eliminator-traffic-off). raceopts' SELECT OPTIONS shows no LAPS row
// for it and leaves its laps to the game (RaceOptions::set_eliminator_query = selected()); TRAFFIC applies. Replays: every recording has the option
// "single_eliminator" (1: the row is there); one without it (recorded before the row) plays with retail's four rows.
// TIMED ELIMINATION (raceopts' row for an Eliminator, race_choice.h; set_timed_query): the Eliminator logic's Update
// 0x38BEB0 (each logic update) runs a timer on the race time (the game timer gWorld 0x522660 +0x20 less the player's
// CB3Score +0x280, the start; from its race state +0x27C 2) and every kTimedEliminationSeconds puts out the car with the
// highest race position (CB3Score +0x0 s16, CB3RacePosition::Update 0x154770) among those still in (+0x1998 set, the score's
// finish +0x8 clear), through the same call the lap rule uses (0x38BB70); the lap rule's own calls (from 0x38BD30) are
// skipped. The game ends the race as before (Update: one car left). The countdown goes to the renderer (timer_seconds).
#ifndef B3NATIVE_SINGLE_ELIMINATOR_H
#define B3NATIVE_SINGLE_ELIMINATOR_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "eliminator_rows.h"
#include <algorithm>
#include "hook_table.h"
#include "records_guard.h"   // the records call without kinds (shared with raceopts)

#include <cstdint>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <string>
#include <vector>

namespace b3native {

class SingleEliminator {
public:
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_ELIMINATOR");
        return !(e && e[0] == '0' && e[1] == 0);
    }
    bool install(HookTable& hooks);
    // SELECT MODE's ELIMINATOR row was the last one confirmed (the game's mode is then race)
    bool selected() const { return selected_; }
    // the race now running is a Single Event Eliminator (the stage record says so)
    static bool racing();
    // TIMED ELIMINATION: whether it is on for the race now (raceopts' choice); the countdown (whole seconds, -1: none)
    void set_timed_query(std::function<bool()> f) { timed_query_ = std::move(f); }
    // extras (PC SETTINGS > EXTRAS > ELIMINATOR): whether SELECT MODE gets the row, asked at each of its Prepares (none
    // set: always). Off: the game's four rows, as the game built them
    void set_row_query(std::function<bool()> f) { row_query_ = std::move(f); }
    int timer_seconds() const { return timer_seconds_; }
    // The race HUD's colours the countdown takes (CB3HUDPos 0x3356A0 / 0x335AB0: the HUD manager 0x0066A4C0's CGtV4d
    // +0x30, +0x40 the labels', +0x50 the last place's warning, +0x60 the digits'; floats 0..1) as RGBA bytes, red low
    // ... then the HUD captions' style 1 ramp (CB3StylisedText2dObject func_003CE290: D_004CA500..D_004CA530, the gold of
    // the race's messages, set at boot by func_004DB0E0)
    static void hud_colours(std::uint32_t out[8]) {
        const b3host::gaddr at[8] = {0x0066A4F0u, 0x0066A500u, 0x0066A510u, 0x0066A520u,
                                     0x004CA500u, 0x004CA510u, 0x004CA520u, 0x004CA530u};
        for (int k = 0; k < 8; ++k) {
            std::uint32_t c = 0;
            for (int j = 0; j < 4; ++j) {
                float f = b3host::load<float>(at[k] + 4u * unsigned(j));
                f = f < 0 ? 0 : f > 1 ? 1 : f;
                c |= std::uint32_t(f * 255.99f) << (8 * j);
            }
            out[k] = c;
        }
    }
    // The player's profile in guest RAM (CB3Profile, *0x0051BF9C, 0x49C bytes): taken when an Eliminator stage starts,
    // compared when it ends (native_play logs it: the Eliminator must leave it as it was)
    void take_profile() { profile_ = profile_bytes(); }
    int profile_changes() const {
        const std::vector<std::uint8_t> now = profile_bytes();
        if (now.size() != profile_.size()) return -1;
        int n = 0;
        for (std::size_t i = 0; i < now.size(); ++i) n += now[i] != profile_[i];
        return n;
    }

private:
    static SingleEliminator*& instance() { static SingleEliminator* p = nullptr; return p; }
    static void prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void action(const b3host::CallArgs& a, b3host::CallResult& r);
    static void request(const b3host::CallArgs& a, b3host::CallResult& r);
    static void records(const b3host::CallArgs& a, b3host::CallResult& r);
    static void eliminate(const b3host::CallArgs& a, b3host::CallResult& r);
    static void heading(const b3host::CallArgs& a, b3host::CallResult& r);
    static void heading_text(const b3host::CallArgs& a, b3host::CallResult& r);
    static void heading_call(void (*next)(const b3host::CallArgs&, b3host::CallResult&), const b3host::CallArgs& a,
                             b3host::CallResult& r);
    static void lookup(const b3host::CallArgs& a, b3host::CallResult& r);
    static void main_menu(const b3host::CallArgs& a, b3host::CallResult& r);
    static void profile_counts(const b3host::CallArgs& a, b3host::CallResult& r);
    static void logic_update(const b3host::CallArgs& a, b3host::CallResult& r);
    bool timed() const { return timed_query_ && timed_query_() && racing(); }
    void clear(const char* why);

    void (*prepare_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*action_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*request_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*records_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*eliminate_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*heading_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*heading_text_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*lookup_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*main_menu_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*profile_counts_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*logic_update_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    std::function<bool()> row_query_;   // extras
    std::function<bool()> timed_query_;
    TimedElimination timer_;
    int timer_seconds_ = -1;
    bool in_timer_ = false, lap_skip_logged_ = false;
    bool profile_logged_ = false;
    std::vector<std::uint8_t> profile_;
    static std::vector<std::uint8_t> profile_bytes() {
        const b3host::gaddr pr = b3host::load<std::uint32_t>(0x0051BF9Cu) & 0x01FFFFFFu;   // D_0051BF9C [C func_0042F0F0]
        std::vector<std::uint8_t> v;
        if (pr == 0) return v;
        for (b3host::gaddr i = 0; i < 0x49Cu; ++i) v.push_back(b3host::load<std::uint8_t>(pr + i));
        return v;
    }
    bool in_heading_ = false;
    bool selected_ = false;
    bool shown_logged_ = false;
};

namespace single_eliminator_detail {
constexpr b3host::gaddr kPage = 0x01E94E4Cu;                 // SELECT MODE [C func_003C9210]
constexpr b3host::gaddr kPrepare = 0x003C98D0u;              // CB3GenericVSelectPage::Prepare [L DNAT]
constexpr b3host::gaddr kAction = 0x003C9340u;               // the Single Event state's Action [C func_003C9340]
constexpr b3host::gaddr kRequest = 0x0030C2C0u;              // CB3MenuChoices::RequestOnePlayerGameMode(int)
constexpr b3host::gaddr kRecords = 0x003D1090u;              // CB3Records::Update(kinds, p1) [C func_0042F0F0's call]
constexpr b3host::gaddr kEliminate = 0x0038BB70u;            // CB3OfflineLapEliminatorLogic: a car out
constexpr b3host::gaddr kChoices = 0x01E900B0u;              // CB3MenuChoices
constexpr b3host::gaddr kStageRecord = 0x0051AD00u, kCurrentStage = 0x0051BA88u;
constexpr b3host::gaddr kEliminatorLogic = 0x0051B2F0u, kRaceLogic = 0x0051B258u;   // inside CB3Game 0x004EE040
constexpr b3host::gaddr kOnePlayerStage = 0x0051B920u;       // +0x1B8 of a Single Event stage
constexpr b3host::gaddr kCars = 0x0064D3B0u, kCarCount = 0x0064D37Cu;   // CB3RaceCar[] (0x27D0 each), their count
constexpr b3host::gaddr kCarPtrs = 0x0064D388u;   // CB3RaceCar* [6] (gWorld+0x12AD28), the order the logic walks [C 0x38BD30]
// (an Eliminator keeps the game's 6-car frame: MORE RIVALS takes a RIVALS above 5 as 5 there, so 6 entries are all)
constexpr std::uint32_t kCarSize = 0x27D0u;
constexpr unsigned kTopSingleEvent = 1;
constexpr b3host::gaddr kHeadingPrepare = 0x00319C80u;       // CB3HeadingComponent::Prepare(EGlobalStrings, EHeadingType)
constexpr b3host::gaddr kHeadingTextPrepare = 0x00319A90u;   // CB3HeadingComponent::Prepare(const unsigned short*, EHeadingType)
constexpr b3host::gaddr kLookup = 0x00271A00u;               // func_00271A00(string table, id) -> UTF-16 text
constexpr b3host::gaddr kGlobalStrings = 0x0051BA44u, kHeadingStrings = 0x0051BA48u;
constexpr unsigned kHeadingTitle = 14, kHeadingRace = 5;   // [C 0x319E80 type 14, func_0031ABC0]
constexpr b3host::gaddr kLogicUpdate = 0x0038BEB0u;           // CB3OfflineLapEliminatorLogic::Update [vtable 0x4DECF0]
constexpr b3host::gaddr kGameTimer = 0x00522660u + 0x20u;    // gWorld's CGtTimer: the game time, seconds (capture.h)
constexpr b3host::gaddr kProfileCounts = 0x00136F30u;        // whether the race counts for the profile [C func_0042F0F0]
constexpr b3host::gaddr kMainMenuPrepare = 0x00318340u;      // the main menu page's Prepare (pc_menu.h kMainMenu's)
inline u_long128 reg(std::uint32_t v) { u_long128 q{}; q.w[0] = v; return q; }
}  // namespace single_eliminator_detail

inline bool SingleEliminator::install(HookTable& hooks) {
    using namespace single_eliminator_detail;
    const b3host::gaddr need[] = {kPrepare, kAction, kRequest, kRecords, kEliminate, kHeadingPrepare, kHeadingTextPrepare, kLookup, kMainMenuPrepare, kProfileCounts, kLogicUpdate};
    for (b3host::gaddr a : need)
        if (!hooks.has_row(a)) {
            std::fprintf(stderr, "single_eliminator: %08X is not native: SELECT MODE left as retail\n", a);
            return false;
        }
    std::vector<Hook> set = {{kPrepare, HookLayer::kPort, &prepare, &prepare_},
                             {kAction, HookLayer::kPort, &action, &action_},
                             {kRequest, HookLayer::kPort, &request, &request_},
                             {kRecords, HookLayer::kPort, &records, &records_},
                             {kEliminate, HookLayer::kPort, &eliminate, &eliminate_},
                             {kHeadingPrepare, HookLayer::kPort, &heading, &heading_},
                             {kHeadingTextPrepare, HookLayer::kPort, &heading_text, &heading_text_},
                             {kLookup, HookLayer::kPort, &lookup, &lookup_},
                             {kMainMenuPrepare, HookLayer::kPort, &main_menu, &main_menu_},
                             {kProfileCounts, HookLayer::kPort, &profile_counts, &profile_counts_},
                             {kLogicUpdate, HookLayer::kPort, &logic_update, &logic_update_}};
    std::string error;
    if (!hooks.install("port:single_eliminator", set, &error)) {
        std::fprintf(stderr, "single_eliminator: %s: SELECT MODE left as retail\n", error.c_str());
        return false;
    }
    instance() = this;
    std::printf("single_eliminator: SINGLE EVENT > SELECT MODE row ELIMINATOR (B3NATIVE_ELIMINATOR=0: retail's four modes)\n");
    return true;
}

inline bool SingleEliminator::racing() {
    using namespace single_eliminator_detail;
    const b3host::gaddr s = b3host::load<std::uint32_t>(kCurrentStage) & 0x01FFFFFFu;
    return s != 0 && (b3host::load<std::uint32_t>(s + 0x1B4u) & 0x01FFFFFFu) == kEliminatorLogic &&
           (b3host::load<std::uint32_t>(s + 0x1B8u) & 0x01FFFFFFu) == kOnePlayerStage;
}

// func_003C98D0 (the generic page's Prepare): on SELECT MODE as the game built it, the ELIMINATOR row added
inline void SingleEliminator::prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace single_eliminator_detail;
    SingleEliminator* p = instance();
    if ((a.gpr[0].w[0] & 0x01FFFFFFu) == kPage) {
        std::uint32_t rows[EliminatorRows::kCount + 1] = {};
        for (unsigned i = 0; i < EliminatorRows::kGameCount; ++i) rows[i] = b3host::load<std::uint32_t>(kPage + 0x10u + 4u * i);
        const unsigned count = b3host::load<std::uint32_t>(kPage + 4u);
        const bool row_on = !p->row_query_ || p->row_query_();
        if (!row_on && p->selected_) {   // extras: ELIMINATOR switched off with it chosen last: the game's RACE (its mode)
            p->selected_ = false;
            std::printf("single_eliminator: ELIMINATOR off: SELECT MODE as the game's, the last choice RACE\n");
        }
        if (EliminatorRows::is_game_list(b3host::load<std::uint32_t>(kPage + 0xCu), count, rows) && row_on) {
            std::uint8_t fa[EliminatorRows::kCount + 1] = {}, fb[EliminatorRows::kCount + 1] = {};
            for (unsigned i = 0; i < EliminatorRows::kGameCount; ++i) {
                fa[i] = b3host::load<std::uint8_t>(kPage + 0x38u + i);
                fb[i] = b3host::load<std::uint8_t>(kPage + 0x42u + i);
            }
            EliminatorRows::build(rows, fa, fb);
            for (unsigned i = 0; i < EliminatorRows::kCount; ++i) {
                b3host::store<std::uint32_t>(kPage + 0x10u + 4u * i, rows[i]);
                b3host::store<std::uint8_t>(kPage + 0x38u + i, fa[i]);
                b3host::store<std::uint8_t>(kPage + 0x42u + i, fb[i]);
            }
            b3host::store<std::uint32_t>(kPage + 4u, EliminatorRows::kCount);
            const unsigned sel = b3host::load<std::uint32_t>(kPage + 8u);
            b3host::store<std::uint32_t>(kPage + 8u, EliminatorRows::row_of(sel, p->selected_));
            if (!p->shown_logged_) {
                std::printf("single_eliminator: SELECT MODE shown with ELIMINATOR (row %u of %u)\n", EliminatorRows::kRow + 1,
                            EliminatorRows::kCount);
                p->shown_logged_ = true;
            }
        }
    }
    p->prepare_(a, r);
}

// func_003C9340 (the Single Event state's Action, a1 the action, a3 the event): Confirm on SELECT MODE (event 5 / 0 with
// the state's +0x10 clear) is given the game's row; the rest is the game's
inline void SingleEliminator::action(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace single_eliminator_detail;
    SingleEliminator* p = instance();
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
    if (a.gpr[1].w[0] == 5u && a.gpr[3].w[0] == 0u && self != 0 && b3host::load<std::uint8_t>(self + 0x10u) == 0 &&
        b3host::load<std::uint32_t>(kPage + 4u) == EliminatorRows::kCount) {
        const unsigned row = b3host::load<std::uint32_t>(kPage + 8u);
        bool elim = false;
        const unsigned game_row = EliminatorRows::game_row_of(row, elim);
        p->selected_ = elim;
        b3host::store<std::uint32_t>(kPage + 8u, game_row);
        if (elim) std::printf("single_eliminator: SELECT MODE: ELIMINATOR (the game's mode: race, the Eliminator's logic)\n");
    } else if (a.gpr[1].w[0] == 5u && a.gpr[3].w[0] == 3u && self != 0 && b3host::load<std::uint8_t>(self + 0x10u) == 0) {
        p->clear("Back on SELECT MODE");   // (event 5 / 3 with +0x10 clear: SELECT MODE's Back, to the main menu)
    }
    // Continue on SELECT OPTIONS (event 5 / 0 with +0x10 set) copies RIVALS to CB3MenuChoices +0x18; the rivals' cars are
    // picked later for that count, so 0 is raised to 1 here (the Eliminator's laps are the cars less one)
    const bool options_continue = a.gpr[1].w[0] == 5u && a.gpr[3].w[0] == 0u && self != 0 &&
                                  b3host::load<std::uint8_t>(self + 0x10u) != 0;
    p->action_(a, r);
    if (options_continue && p->selected_ && b3host::load<std::uint32_t>(kChoices + 0x18u) == 0) {
        b3host::store<std::uint32_t>(kChoices + 0x18u, 1u);
        std::printf("single_eliminator: SELECT OPTIONS: RIVALS 0 raised to 1 (an Eliminator needs a rival)\n");
    }
    return;
}

// RequestOnePlayerGameMode: the game's race record, then the Eliminator's logic in it
inline void SingleEliminator::request(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace single_eliminator_detail;
    SingleEliminator* p = instance();
    p->request_(a, r);
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu, rec = r.v0.w[0] & 0x01FFFFFFu;
    if (!p->selected_ || self != kChoices || rec != kStageRecord) return;
    if (b3host::load<std::uint32_t>(kChoices + 4u) != kTopSingleEvent || b3host::load<std::uint32_t>(kChoices + 8u) != 0) return;
    if ((b3host::load<std::uint32_t>(rec + 0x1B4u) & 0x01FFFFFFu) != kRaceLogic ||
        (b3host::load<std::uint32_t>(rec + 0x1B8u) & 0x01FFFFFFu) != kOnePlayerStage) return;
    const unsigned chosen = b3host::load<std::uint32_t>(rec + 0xA4u), n = EliminatorRows::rivals(chosen);
    // the rivals' cars and colours as the game's race loop copies them (CB3MenuChoices +0x80 / +0xC0, from index 1)
    for (unsigned i = chosen; i < n; ++i)   // (the SELECT OPTIONS raise makes this rare: the game picked no car for it)
        if (b3host::load<std::uint64_t>(kChoices + 0x80u + 8u * (i + 1u)) == 0) {
            std::printf("single_eliminator: no rival car picked for an Eliminator (RIVALS 0): the race stays a race\n");
            return;
        }
    for (unsigned i = chosen; i < n; ++i) {
        const std::uint64_t car = b3host::load<std::uint64_t>(kChoices + 0x80u + 8u * (i + 1u));
        const std::uint32_t colour = b3host::load<std::uint32_t>(kChoices + 0xC0u + 4u * (i + 1u));
        b3host::store<std::uint64_t>(rec + 0x18u + 8u * i, car);
        b3host::store<std::uint32_t>(rec + 0x78u + 4u * i, colour);
        std::printf("single_eliminator: rival %u added: car id %016llX, colour %u (CB3MenuChoices +0x%X)\n", i + 1u,
                    static_cast<unsigned long long>(car), colour, 0x80u + 8u * (i + 1u));
    }
    b3host::store<std::uint32_t>(rec + 0xA4u, n);
    b3host::store<std::uint32_t>(rec + 0x1B4u, kEliminatorLogic);
    std::printf("single_eliminator: the race is an ELIMINATOR: logic %08X (CB3OfflineLapEliminatorLogic), %u rival%s%s, "
                "%u lap%s\n", kEliminatorLogic, n, n == 1 ? "" : "s", n != chosen ? " (RIVALS 0 raised to 1)" : "", n,
                n == 1 ? "" : "s");
}

// CB3Records::Update(kinds, p1): a Single Event Eliminator asks for no kinds (the game's tables are RACE's)
inline void SingleEliminator::records(const b3host::CallArgs& a, b3host::CallResult& r) {
    SingleEliminator* p = instance();
    if (!racing()) {
        p->records_(a, r);
        return;
    }
    // no kinds, the records left as after a race with no new record (no RECORD tags): records_guard.h
    const std::uint64_t kinds = records_without_kinds(p->records_, a, r);
    if (kinds == 0) return;   // (another guard took them first: raceopts')
    std::printf("single_eliminator: results: the record kinds %llu not written (no Eliminator table: the track's are "
                "RACE's)\n", static_cast<unsigned long long>(kinds));
}

// the Eliminator logic's car-out call (a1 the car): logged for a Single Event Eliminator, then the game's
inline void SingleEliminator::eliminate(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace single_eliminator_detail;
    SingleEliminator* p = instance();
    if (racing()) {
        if (!p->in_timer_ && p->timed()) {   // TIMED ELIMINATION: the lap rule's call (0x38BD30) is skipped
            if (!p->lap_skip_logged_) {
                std::printf("single_eliminator: TIMED ELIMINATION: the lap rule's elimination skipped (the timer's instead)\n");
                p->lap_skip_logged_ = true;
            }
            return;
        }
        const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu, car = a.gpr[1].w[0] & 0x01FFFFFFu;
        const int count = b3host::load<std::int32_t>(kCarCount);
        int idx = -1;
        for (int i = 0; i < count && i < 6; ++i)
            if ((b3host::load<std::uint32_t>(kCarPtrs + 4u * unsigned(i)) & 0x01FFFFFFu) == car) idx = i;
        const unsigned out = b3host::load<std::uint32_t>(self + 0x4Cu) + 1u;
        std::printf("single_eliminator: %s %u: car %d (%s, position %d) is out, %d of %d left\n", p->in_timer_ ? "timed elimination" : "lap",
                    out, idx, b3host::load<std::int32_t>(car + 0x1914u) == 0 ? "the player" : "a rival",
                    int(b3host::load<std::int16_t>(car + 0x10C0u)), count - int(out), count);
    }
    p->eliminate_(a, r);
}

// CB3OfflineLapEliminatorLogic::Update (each logic update): TIMED ELIMINATION's timer, then the game's Update (which ends
// the race when one car is left)
inline void SingleEliminator::logic_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace single_eliminator_detail;
    SingleEliminator* p = instance();
    if (!p->timed()) {
        p->timer_.reset();
        p->timer_seconds_ = -1;
        p->lap_skip_logged_ = false;
        p->logic_update_(a, r);
        return;
    }
    const int count = std::min(b3host::load<std::int32_t>(kCarCount), 6);
    int pos[6] = {}, player = -1, left = 0;
    bool in[6] = {};
    b3host::gaddr cars[6] = {};
    for (int i = 0; i < count; ++i) {
        const b3host::gaddr car = cars[i] = b3host::load<std::uint32_t>(kCarPtrs + 4u * unsigned(i)) & 0x01FFFFFFu;
        if (car == 0) continue;
        pos[i] = b3host::load<std::int16_t>(car + 0x10C0u);   // (s16: +0x10C2 is the elimination order, 0x38BB70)
        in[i] = b3host::load<std::uint8_t>(car + 0x1998u) != 0 && b3host::load<std::int32_t>(car + 0x10C8u) == 0;
        left += in[i] ? 1 : 0;
        if (b3host::load<std::int32_t>(car + 0x1914u) == 0 && player < 0) player = i;
    }
    if (player < 0) {   // (no player car found: nothing to time)
        p->timer_seconds_ = -1;
        p->logic_update_(a, r);
        return;
    }
    const b3host::gaddr score = cars[player] + 0x10C0u;
    const bool started = b3host::load<std::int32_t>(score + 0x27Cu) >= 2;
    const float elapsed = b3host::load<float>(kGameTimer) - b3host::load<float>(score + 0x280u);
    const TimedElimination::Step st = p->timer_.step(started, elapsed, left);
    if (st.eliminate) {
        const int last = TimedElimination::last_car(pos, in, count);
        if (last >= 0) {
            b3host::CallArgs c{};
            c.gpr[0] = a.gpr[0];
            c.gpr[1] = reg(cars[last]);
            c.n_gpr = 2;
            c.sp = a.sp;
            b3host::CallResult cr{};
            std::printf("single_eliminator: TIMED ELIMINATION at %.2f s of race time; positions (car: position, in):", double(elapsed));
            for (int i = 0; i < count; ++i) std::printf(" %d: %d%s", i, pos[i], in[i] ? "" : " out");
            std::printf("\n");
            p->in_timer_ = true;
            eliminate(c, cr);
            p->in_timer_ = false;
        }
    }
    // the countdown while the player is still in and not crashing (+0x18EA, as 0x38BB70 tests it: the game hides its race
    // HUD for the crash and its replay)
    p->timer_seconds_ = in[player] && b3host::load<std::uint8_t>(cars[player] + 0x18EAu) == 0 ? st.seconds_left : -1;
    p->logic_update_(a, r);
}

// the main menu page's Prepare: out of Single Event, the row's choice ends (the game's mode stays as it was: race)
inline void SingleEliminator::main_menu(const b3host::CallArgs& a, b3host::CallResult& r) {
    instance()->clear("the main menu");
    instance()->main_menu_(a, r);
}
// func_00136F30 (does the race count for the profile; the game's own says no for the split screen and some modes): no
// for a Single Event Eliminator. Counted, its results would add Burnout points (World Tour's event unlock thresholds,
// func_00138530), the race / slam / car totals and the unlocks' evaluation (Evaluate__10CB3ProfileFv); the game also asks
// it in CB3StageLogic::OnTakedown (signature takedowns). As raceopts does for its races.
inline void SingleEliminator::profile_counts(const b3host::CallArgs& a, b3host::CallResult& r) {
    SingleEliminator* p = instance();
    p->profile_counts_(a, r);
    if ((r.v0.w[0] & 0xFFu) == 0 || !racing()) return;
    if (!p->profile_logged_) {
        std::printf("single_eliminator: the Eliminator does not count for the profile (Burnout points, totals, unlocks left as they were)\n");
        p->profile_logged_ = true;
    }
    r.v0 = u_long128{};
}

inline void SingleEliminator::clear(const char* why) {
    if (!selected_) return;
    selected_ = false;
    std::printf("single_eliminator: %s: ELIMINATOR no longer chosen\n", why);
}

// CB3HeadingComponent::Prepare(id or text, type): a type 14 heading (the red title) while the ELIMINATOR row's race is set up
inline void SingleEliminator::heading_call(void (*next)(const b3host::CallArgs&, b3host::CallResult&), const b3host::CallArgs& a,
                                           b3host::CallResult& r) {
    using namespace single_eliminator_detail;
    SingleEliminator* p = instance();
    if (p->selected_ && a.gpr[2].w[0] == kHeadingTitle && b3host::load<std::uint32_t>(kChoices + 4u) == kTopSingleEvent &&
        b3host::load<std::uint32_t>(kChoices + 8u) == 0) {
        p->in_heading_ = true;
        next(a, r);
        p->in_heading_ = false;
        return;
    }
    next(a, r);
}
inline void SingleEliminator::heading(const b3host::CallArgs& a, b3host::CallResult& r) { heading_call(instance()->heading_, a, r); }
inline void SingleEliminator::heading_text(const b3host::CallArgs& a, b3host::CallResult& r) {
    heading_call(instance()->heading_text_, a, r);
}

// func_00271A00(table, id): in that heading, the heading table's RACE is the global table's ELIMINATOR
inline void SingleEliminator::lookup(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace single_eliminator_detail;
    SingleEliminator* p = instance();
    if (p->in_heading_ && (a.gpr[0].w[0] & 0x01FFFFFFu) == kHeadingStrings && a.gpr[1].w[0] == kHeadingRace) {
        b3host::CallArgs b = a;
        b.gpr[0] = reg(kGlobalStrings);
        b.gpr[1] = reg(EliminatorRows::kEliminator);
        p->lookup_(b, r);
        return;
    }
    p->lookup_(a, r);
}

}  // namespace b3native

#endif
