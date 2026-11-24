// paintsel (tools/native/paint_select.h): SELECT CAR's player for the car paint panel, and the colour-row rule -- the
// game's colour counter of the row on show changing with the series and the row unchanged is the player's colour choice
// (that player's paint back to Game); a series or row change, another page or player, or the page not taking input is
// not. The PAINT prompt's fade.
#include "test.h"

#include "paint_select.h"

#include <cstdint>

namespace {

using b3native::PaintSelect;
constexpr std::uint32_t kSel = b3native::kCarSelectPage, kModes = 0x01E94E4Cu;
constexpr std::uint32_t kInput = b3native::kPageStateInput, kLeadOut = 3;

TEST(addresses) {
    CHECK(b3native::kCarSelectPlayer == 0x01E94F08u);
    CHECK(b3native::carousel_colour_addr(0) == 0x01E8EC44u);   // CB3CarouselComponent +0xC, the entry's byte +8
    CHECK(b3native::carousel_colour_addr(2) == 0x01E8EC44u + 0x18u);
}

TEST(page_and_player) {
    PaintSelect p;
    auto s = p.step(kModes, kInput, 0, 0, 0, 0);
    CHECK(s.page == -1 && !s.page_changed);
    s = p.step(kSel, kLeadOut, 0, 0, 0, 0);   // leading in: not yet
    CHECK(s.page == -1);
    s = p.step(kSel, kInput, 0, 0, 0, 0);
    CHECK(s.page == 0 && s.page_changed);
    s = p.step(kSel, kInput, 0, 0, 0, 0);
    CHECK(s.page == 0 && !s.page_changed);
    s = p.step(kSel | 0x20000000u, kInput, 1, 0, 0, 0);   // the page word's high bits ignored; player 2's page
    CHECK(s.page == 1 && s.page_changed);
    s = p.step(kSel, kLeadOut, 1, 0, 0, 0);
    CHECK(s.page == -1 && s.page_changed);
}

TEST(colour_row_rule) {
    PaintSelect p;
    auto s = p.step(kSel, kInput, 0, 1, 2, 0);   // the baseline: no change on the first VBlank
    CHECK(s.colour_player == -1);
    s = p.step(kSel, kInput, 0, 1, 2, 1);        // Square: the row's colour 0 -> 1
    CHECK(s.colour_player == 0 && s.colour_from == 0 && s.colour_to == 1);
    s = p.step(kSel, kInput, 0, 1, 2, 1);        // held: once
    CHECK(s.colour_player == -1);
    s = p.step(kSel, kInput, 0, 1, 3, 4);        // Down: another row (its own colour): not a choice
    CHECK(s.colour_player == -1);
    s = p.step(kSel, kInput, 0, 2, 0, 0);        // Right: another series, its rows rebuilt: not a choice
    CHECK(s.colour_player == -1);
    s = p.step(kSel, kInput, 0, 2, 0, 1);        // then Square there: a choice
    CHECK(s.colour_player == 0);
    s = p.step(kSel, kInput, 0, 2, 0, 0);        // round to the first colour again: a choice too
    CHECK(s.colour_player == 0 && s.colour_from == 1 && s.colour_to == 0);
}

TEST(colour_rule_off_the_page) {
    PaintSelect p;
    p.step(kSel, kInput, 0, 1, 2, 0);
    auto s = p.step(kSel, kLeadOut, 0, 1, 2, 3);   // leading out (Confirm): the page's own rebuild is no choice
    CHECK(s.colour_player == -1);
    s = p.step(kSel, kInput, 0, 1, 2, 3);          // back: a new baseline
    CHECK(s.colour_player == -1);
    s = p.step(kModes, kInput, 0, 1, 2, 5);        // another page
    CHECK(s.colour_player == -1);
    // G1 -> G2 (multiplayer): the same page object, the other player; player 2's choices are player 2's
    PaintSelect m;
    m.step(kSel, kInput, 0, 0, 0, 0);
    s = m.step(kSel, kInput, 1, 0, 0, 2);          // player 2's page with player 1's colour left: a baseline, no choice
    CHECK(s.page == 1 && s.colour_player == -1);
    s = m.step(kSel, kInput, 1, 0, 0, 3);
    CHECK(s.colour_player == 1);
}

TEST(prompt_fades) {
    PaintSelect p;
    CHECK(p.opacity() == 0);
    auto s = p.step(kSel, kInput, 0, 0, 0, 0);
    CHECK(s.prompt_changed && p.opacity() > 0 && p.opacity() < 255);
    for (int k = 0; k < PaintSelect::kFadeSteps; ++k) p.step(kSel, kInput, 0, 0, 0, 0);
    CHECK(p.opacity() == 255);
    s = p.step(kSel, kInput, 0, 0, 0, 0);
    CHECK(!s.prompt_changed);
    s = p.step(kSel, kInput, 1, 0, 0, 0);          // player 2's page: the record changes (its player)
    CHECK(s.prompt_changed && p.player() == 1);
    for (int k = 0; k <= PaintSelect::kFadeSteps; ++k) p.step(kModes, kInput, 0, 0, 0, 0);
    CHECK(p.opacity() == 0 && p.player() == 1);    // faded out, the player kept
}

}  // namespace

TEST_MAIN("the car paint panel on SELECT CAR, the game's colour choice (paintsel)")
