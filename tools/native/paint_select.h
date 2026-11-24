// paint_select.h -- paintsel (2026-11-21): the car paint panel on SELECT CAR, and the game's own colour choice there. Read
// only: the game's current menu page, its page manager's state, the car select page's player and the carousel's colour
// counters, compared VBlank by VBlank. The panel and the paint are the renderer's (drawn only).
//
// What the game does on SELECT CAR [C: the generated units' code; car_select.h has the page]:
// - The page's Update 0x318A80 reads, for its player (+0x18, the GetMenu* calls' signed char), Back (Triangle), Confirm
//   (Cross), Up / Down, Left / Right and Buddies (Square) -- nothing else: Circle, L2 / R2, Select and Start do nothing
//   there (L1 / R1 are CAR SERIES ON L1 / R1's, opt-in, given to the game as Left / Right). So the port's CAR PAINT PANEL
//   button is Circle in the menus (PlayStation Circle; Y on the Xbox and Nintendo layouts, which put the menus' Circle
//   there) and P on the keyboard (input_bindings.h PortAction::PaintPanel); the window keeps every Circle from the game
//   while the page takes input (src/b3replay/nativepad.cpp paint_circle).
// - Square is the colour: when the car has more than one (0x42C100: the car's colour count, at most 6, is 2 or more) it
//   steps the carousel's entry for the row on show (0x42BC90: CB3CarouselComponent 0x01E8EC30 +0xC + row x 0xC + 8, a
//   signed byte, + 1 modulo the count), then hands the colour to the car on show (0x42AFC0). Nothing else writes that
//   byte while the page shows the same series and row (Left / Right rebuild the series' rows; Up / Down move the row).
//   So a change of it with the series (+0x258) and the row (+0x244) unchanged is the player's colour choice.
// - The multiplayer car selects (G1 / G2 SELECT CAR) are the same page object, its player 0 or 1.
//
// The rule (owner, 2026-11-21): the game's colour choice on SELECT CAR is the paint the player wants: that player's CAR
// PAINT goes back to Game (b3r_paint_game: PC SETTINGS > CAR > CAR PAINT / P2 CAR PAINT, saved), so the small preview
// and the car always agree. A custom colour is used only when chosen in the panel (or in PC SETTINGS), and it stays
// saved until the player changes the colour on SELECT CAR again. A car with one livery has no colour choice: Square
// changes nothing there and the custom paint stays.
//
// The PAINT prompt (the renderer: src/b3replay/paintsel.h) shows while the page takes input, faded in and out like the
// track page's label (race_time.h); native_play hands its opacity and the page's player to the renderer in a host record
// (gameview_native.h set_paint_prompt). A replay plays what the game saw: the panel is not opened there and the rule does
// not touch the settings (the prompt still shows: it is the page's).
#ifndef B3NATIVE_PAINT_SELECT_H
#define B3NATIVE_PAINT_SELECT_H

#include <cstdint>

#include "car_select.h"
#include "race_time.h"   // kPageState, kPageStateInput

namespace b3native {

constexpr std::uint32_t kCarSelectPlayer = kCarSelectPage + 0x18u;      // [C] the page's player (a signed byte)
constexpr std::uint32_t kCarouselColour0 = kCarousel + 0xCu + 8u;       // [C] row 0's colour counter; rows 0xC apart
constexpr std::uint32_t kCarouselRows = 8u;                             // [C] the entries (field_C[8])
inline std::uint32_t carousel_colour_addr(std::uint32_t row) { return kCarouselColour0 + 0xCu * (row % kCarouselRows); }

class PaintSelect {
public:
    static constexpr int kFadeSteps = 8;   // VBlanks to fade the prompt in or out
    struct Step {
        int page = -1;                 // the player whose SELECT CAR takes input now (0 / 1), -1 none
        bool page_changed = false;     // `page` differs from the last VBlank's
        int colour_player = -1;        // the game's colour changed this VBlank, on this player's page; -1 none
        int colour_from = 0, colour_to = 0;
        bool prompt_changed = false;   // the prompt's opacity or player
    };
    // Once per VBlank. `page`: the page manager's current page word; `state`: its state word; `player`: the page's player
    // byte; `series`, `row`: the carousel's; `colour`: the row's colour counter (read at carousel_colour_addr(row)).
    Step step(std::uint32_t page, std::uint32_t state, int player, std::uint32_t series, std::uint32_t row, int colour) {
        Step s;
        const bool input = (page & 0x01FFFFFFu) == kCarSelectPage && state == kPageStateInput;
        s.page = input ? (player == 1 ? 1 : 0) : -1;
        s.page_changed = s.page != page_;
        if (s.page_changed) known_ = false;   // a new page (or player): the colour's baseline again
        if (input) {
            if (known_ && series == series_ && row == row_ && colour != colour_) {
                s.colour_player = s.page;
                s.colour_from = colour_;
                s.colour_to = colour;
            }
            known_ = true;
            series_ = series;
            row_ = row;
            colour_ = colour;
        }
        page_ = s.page;
        const std::uint8_t was_op = opacity_, was_player = player_;
        if (input) player_ = std::uint8_t(s.page);
        const int target = input ? 255 : 0, stp = 255 / kFadeSteps + 1;
        opacity_ = std::uint8_t(opacity_ < target ? (target - opacity_ < stp ? target : opacity_ + stp)
                                                  : (opacity_ - target < stp ? target : opacity_ - stp));
        s.prompt_changed = opacity_ != was_op || player_ != was_player;
        return s;
    }
    std::uint8_t opacity() const { return opacity_; }   // the prompt's, 0..255 (0: not drawn)
    std::uint8_t player() const { return player_; }     // the page's player the prompt was last shown for
    int page() const { return page_; }

private:
    int page_ = -1;
    bool known_ = false;
    std::uint32_t series_ = 0, row_ = 0;
    int colour_ = 0;
    std::uint8_t opacity_ = 0, player_ = 0;
};

}  // namespace b3native

#endif
