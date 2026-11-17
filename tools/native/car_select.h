// car_select.h -- carsel1 (2026-11-13; LB / RB change the car series): the car pages native_play's CAR SERIES ON L1 / R1 applies to (src/native/input_keys.h ShoulderSeries; PC
// SETTINGS > INTERFACE > CONTROLLER, opt-in). Read only: the game's current menu page, compared with two page objects.
//
// What the game does on these pages [C: the generated units' code, the ELF's vtables and its __sinit func_004D7150]:
// - The car select (SINGLE EVENT, WORLD TOUR, MULTIPLAYER: G1 / G2 SELECT CAR) is the CB3CarSelectPage object
//   0x01E94EF0 (vtable 0x4DE3F0: Update 0x318A80, Release 0x318A30; Prepare 0x318F50, called by the car select state
//   0x310200 / the flow 0x30ED90). Its Update reads, for the page's player (+0x18): Back (Triangle) when allowed, Confirm
//   (Cross), Up / Down, Left / Right and Buddies (Square: the paint) -- nothing else. Up / Down move the carousel's car
//   (CB3CarouselComponent 0x01E8EC30: 0x42C020 / 0x42C090, row +0x244), Left / Right its series, the car class
//   (0x42BD70 / 0x42BEA0: column +0x258, the row back to 0, the column's cars rebuilt), each with FEVerticalMove's sound.
//   Its prompts (0x318560) show the d-pad's arms where a move is possible.
// - DRIVER DETAILS > YOUR GARAGE is the page object 0x01E95CB8 (vtable 0x4DFE40: Update 0x3F03D0): the same carousel,
//   Back, Up / Down, Left / Right only.
// - The shoulders are read in the menus only by pages these are not [C: GetMenuPageUp / PageDown 0x22C940 / 0x22C8C0
//   (L1 / R1: gaeMenubuttons 0x4B9BC0 [16] = 10, [17] = 12, the DualShock controls L1 / R1) by the online rankings
//   0x3778D0; KeyboardShift / CapsLock (L1 / R1, [12] / [13]) by the on-screen keyboard 0x434550; GetMenuButton
//   0x22C810's callers (online pages) ask for 14 / 15 (L2 / R2) and 18 / 21 / 22 (Square / Circle / Square)]. On these
//   two pages L1 / R1 do nothing.
// - The series' arrows are two images of the front end's "CarArrow" texture either side of the series' name (the
//   carousel's Prepare 0x42D2B0, mode +0x25C = 9: the name right-aligned at x 594, y 76; the arrows centred at y 89);
//   the renderer draws the shoulders' glyphs beside them (src/b3replay/prompts.h series_glyphs).
#ifndef B3NATIVE_CAR_SELECT_H
#define B3NATIVE_CAR_SELECT_H

#include <cstdint>

namespace b3native {

constexpr std::uint32_t kCarSelectPage = 0x01E94EF0u;   // [C] CB3CarSelectPage (vtable 0x4DE3F0)
constexpr std::uint32_t kGaragePage = 0x01E95CB8u;      // [C] YOUR GARAGE (vtable 0x4DFE40)
constexpr std::uint32_t kCarousel = 0x01E8EC30u;        // [C] CB3CarouselComponent (both pages')
constexpr std::uint32_t kCarouselSeries = kCarousel + 0x258u, kCarouselCar = kCarousel + 0x244u;   // [L] column, row

// One of the two series pages is the menu's current page (the word at fast_load.h kMenuCurrentPage).
inline bool series_page_up(std::uint32_t current_page) {
    const std::uint32_t p = current_page & 0x01FFFFFFu;
    return p == kCarSelectPage || p == kGaragePage;
}

}  // namespace b3native

#endif
