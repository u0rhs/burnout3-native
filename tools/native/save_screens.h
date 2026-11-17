// save_screens.h -- PC SETTINGS > SAVES > SAVE SCREENS (autosave1, 2026-11-15; docs/remaster-features.md §8 "No card
// messages", docs/pc-settings.md): the AUTOSAVE page after a race is not shown.
//
// The port has no memory card; its card is the port's own save file (src/native/save_store.h) and its saves are silent
// (tools/native/card_silence.h hides the card's progress and "Save successful" boxes and answers them). What stayed was
// the page around them: after a race (and after the crash results) the game enters its AUTOSAVE page, whose title
// slides in, whose card controller checks the card, saves, and confirms (the hidden boxes), and which leads out to the
// next page, ~200 VBlanks in all [C observed, race_check crashresults / roadrage]. On the PS2 that is the "AUTOSAVE"
// banner the player has to sit through; on the PC it only delays the results.
//
// With SAVE SCREENS Hide (the default; B3NATIVE_SAVE_SCREENS=show or --save-screens show: as the PS2) two things change,
// both presentation only:
//   - the display holds the picture of the page before it (the loading screen, or the page that led here) from the
//     first frame of the AUTOSAVE page to its last: the poll's record carries a hold flag (gameview_native.h
//     set_hold_probe; src/b3replay/livefeed.cpp copies the previous tick's picture for a held tick);
//   - a paced run does not wait for real time during those VBlanks (the loading screens' FastLoad rule,
//     tools/native/fast_load.h: the game's virtual time is untouched, only the wall clock), and its sound is muted
//     there as a fast load's is.
// The game does not change: the same VBlanks, the same guest memory at each of them, the same save bytes (the card
// controller runs its own states exactly as before).
//
// What "the AUTOSAVE page" is, from the game's own state [C decomp, observed]: CB3MenuPageManager D_01E90240's current page
// word (+0x1F0, 0x01E90430, fast_load.h kMenuCurrentPage) is the memory card flow's page D_01E97DAC (func_00420A50
// prepares it, from func_0041F0C0 case 3: the card controller's states func_0041FE00 / func_004201F0, the title
// "AUTOSAVE" and, in the card controller, the check -> save -> "Save successful" run card_silence.h answers). DRIVER
// DETAILS > PROFILE > SAVE PROFILE is another page object (D_01E96424), LOAD PROFILE a third (D_01E96418): they stay as
// they are, they are the player's own actions.
#ifndef B3NATIVE_SAVE_SCREENS_H
#define B3NATIVE_SAVE_SCREENS_H

#include <cstdint>
#include <cstdlib>

namespace b3native {

constexpr std::uint32_t kAutosavePage = 0x01E97DACu;   // [C] the card flow's page object func_00420A50 prepares

// The AUTOSAVE page is up: the page manager's current page word.
inline bool autosave_page_up(std::uint32_t current_page) { return (current_page & 0x01FFFFFFu) == kAutosavePage; }

// B3NATIVE_SAVE_SCREENS=show | hide (0 / 1); -1: not set (the setting decides)
inline int save_screens_env() {
    const char* e = std::getenv("B3NATIVE_SAVE_SCREENS");
    if (!e || !*e) return -1;
    return (e[0] == 's' || e[0] == 'S' || e[0] == '0') ? 0 : 1;
}

}  // namespace b3native

#endif
