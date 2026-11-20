// race_time.h -- todrace (1.3.0, 2026-11-20): DAY / NIGHT chosen per race on the Single Event track page (and
// multiplayer's). Read only: the game's current menu page and its page manager's state, compared with page objects. The
// choice is the host's.
//
// What the game does on the track pages [C: the generated units' code, the ELF's vtables and its __sinit func_004D7150]:
// - SINGLE EVENT's track page ("SELECT OPTIONS": REGION / TRACK / RIVALS, for RACE, TIME ATTACK and ROAD RAGE) is one
//   page object, 0x01E96F78 (vtable 0x4E0110: Update 0x432A50, Release 0x432550; Prepare 0x433230, called by the Single
//   Event flow 0x3C9210). Its Update returns at once unless the page manager's state (D_01E90424, below) is 5 (the page
//   on screen and taking input); then, by its state +0x6C4: 0 the rows (0x4328C0), 1 an option's change alone
//   (0x432560). The rows read, for player 0 only: Confirm (Cross), Back (Triangle), Up / Down (the row,
//   CB3VOptionsSelectComponent +0x24) and Left / Right (the row's value: 0x432560, which reads Left / Right again).
// - SINGLE EVENT > CRASH's "JUNCTION SELECTION" is another object, 0x01E97F38 (vtable 0x4E0510: Update 0x454BF0), which
//   MULTIPLAYER's crash modes (TEAM CRUSH, ...) show too: Confirm, Back, Up / Down only.
// - MULTIPLAYER's track page for its racing modes (ROAD RAGE, ...: REGION / TRACK) is 0x01E97640 (vtable 0x4E0120:
//   Update 0x433C20, by +0x6C4 0x433A70 / 0x433720, SELECT OPTIONS' twin): Confirm, Back, Up / Down, Left / Right.
// - So L1 / R1, L2 / R2, Square, Select and Start do nothing on these pages (the menus' shoulder reads, car_select.h,
//   are on other pages). The port's TIME OF DAY action (input_bindings.h PortAction::RaceTime: L1 on a controller --
//   LB, L, the PlayStation's L1 -- and T on the keyboard) is taken there only, and the window withholds its inputs from
//   the game's report while such a page is up (nativepad.cpp), so the game sees no press at all. In multiplayer it is
//   player 1's (the port actions'), and the race's two views share the choice.
// - The main menu is pc_menu.h's kMainMenu (0x01E94C40). World Tour shows none of these pages (its events are the
//   globe's), so a choice never reaches a World Tour race: it resets on the main menu, before any World Tour.
//
// The choice: Default (PC SETTINGS > GRAPHICS > LIGHTING > TIME OF DAY), Day or Night, stepped by each press while the
// page takes input. Held by the host, never saved. The race takes it when the loading screen comes up (each load: a
// restart of the event keeps it), and it goes back to Default on the main menu. Drawn only: the renderer gets the
// race's value and the label's state in a host record (gameview_native.h set_race_time); a replay records the choice
// ("race_time", native_play's live settings) and plays it back on the same VBlanks.
#ifndef B3NATIVE_RACE_TIME_H
#define B3NATIVE_RACE_TIME_H

#include <cstdint>

namespace b3native {

constexpr std::uint32_t kSelectOptionsPage = 0x01E96F78u;   // [C] SINGLE EVENT's SELECT OPTIONS (vtable 0x4E0110)
constexpr std::uint32_t kJunctionPage = 0x01E97F38u;        // [C] CRASH's JUNCTION SELECTION (vtable 0x4E0510)
constexpr std::uint32_t kMultiTrackPage = 0x01E97640u;      // [C] MULTIPLAYER's REGION / TRACK page (vtable 0x4E0120)
constexpr std::uint32_t kRaceTimeMainMenu = 0x01E94C40u;    // [C] the main menu page (pc_menu.h kMainMenu)
constexpr std::uint32_t kPageState = 0x01E90424u;           // [C] D_01E90424: the page manager's state (5: the page takes input)
constexpr std::uint32_t kPageStateInput = 5u;

// One of the track pages is the menu's current page (the word at fast_load.h kMenuCurrentPage)
inline bool race_time_page(std::uint32_t current_page) {
    const std::uint32_t p = current_page & 0x01FFFFFFu;
    return p == kSelectOptionsPage || p == kJunctionPage || p == kMultiTrackPage;
}

// The choice's values (also the replay's "race_time" and the host record's)
enum : std::uint8_t { kRaceTimeDefault = 0, kRaceTimeDay = 1, kRaceTimeNight = 2 };
inline const char* race_time_name(int c) { return c == kRaceTimeDay ? "Day" : c == kRaceTimeNight ? "Night" : "Default"; }

class RaceTime {
public:
    static constexpr int kFadeSteps = 8;   // VBlanks to fade the label in or out
    struct Step {
        bool choice_changed = false;   // a press stepped it, or the main menu reset it
        bool race_changed = false;     // a load took a different value
        bool label_changed = false;    // the label's opacity or its text
    };
    // Once per VBlank. `page`: the page manager's current page word; `state`: its state word; `loading`: the loading
    // screen is up (fast_load.h loading_page_up); `presses`: the action's presses since the last VBlank (live play;
    // 0 while a replay plays, which sets the recorded choice with set_choice first).
    Step step(std::uint32_t page, std::uint32_t state, bool loading, int presses) {
        Step s;
        const std::uint32_t p = page & 0x01FFFFFFu;
        const bool input = race_time_page(page) && state == kPageStateInput;
        const std::uint8_t was_choice = choice_, was_op = opacity_;
        if (p == kRaceTimeMainMenu) choice_ = kRaceTimeDefault;
        if (input)
            for (int k = 0; k < presses; ++k) choice_ = std::uint8_t((choice_ + 1) % 3);
        if (input) presses_ += presses > 0 ? presses : 0;
        if (loading && !loading_ && race_ != choice_) {
            race_ = choice_;
            s.race_changed = true;
        }
        loading_ = loading;
        const int target = input ? 255 : 0, step = 255 / kFadeSteps + 1;
        opacity_ = std::uint8_t(opacity_ < target ? (target - opacity_ < step ? target : opacity_ + step)
                                                  : (opacity_ - target < step ? target : opacity_ - step));
        s.choice_changed = choice_ != was_choice;
        s.label_changed = s.choice_changed || opacity_ != was_op;
        return s;
    }
    // Playback (or a test): the recorded choice, from this VBlank on
    void set_choice(int c) { choice_ = std::uint8_t(c == kRaceTimeDay || c == kRaceTimeNight ? c : kRaceTimeDefault); }
    int choice() const { return choice_; }        // the page's choice (the label shows it)
    int race() const { return race_; }            // what the race on screen uses (taken at the last load)
    std::uint8_t opacity() const { return opacity_; }   // the label's, 0..255 (0: not drawn)
    int presses() const { return presses_; }      // presses taken on the page so far

private:
    std::uint8_t choice_ = kRaceTimeDefault, race_ = kRaceTimeDefault, opacity_ = 0;
    bool loading_ = false;
    int presses_ = 0;
};

}  // namespace b3native

#endif
