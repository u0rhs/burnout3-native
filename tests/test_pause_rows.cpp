// photorow1: the pause menu's PHOTO MODE row, the pure part (tools/native/pause_rows.h): the private row array the selection
// is given, and which row of the page the game's Update is shown.
#include "test.h"

#include "pause_rows.h"

using b3native::PauseRow;
using b3native::PhotoRows;

namespace {
constexpr std::uint32_t kSettings = 0x200, kPhoto = 0x240;
}

TEST(rows_with_photo_before_the_last) {
    // RESUME, RESTART, AUDIO, CONTROLLERS, SETTINGS, QUIT (the page's six, SETTINGS added by pc_menu)
    PauseRow page[6] = {{0x1000, 0}, {0x1010, 0}, {0x1020, 0}, {0x1030, 0}, {kSettings, 0}, {0x1050, 0}};
    PauseRow out[7] = {};
    unsigned idx = 99;
    CHECK(PhotoRows::build(page, 6, kSettings, kPhoto, out, idx));
    CHECK(idx == 5);
    for (unsigned i = 0; i < 5; ++i) CHECK(out[i].label == page[i].label);   // the rows up to SETTINGS keep their index
    CHECK(out[4].label == kSettings && out[5].label == kPhoto && out[5].flags == 0 && out[6].label == 0x1050);
}

TEST(a_disabled_last_row_stays_disabled) {
    PauseRow page[5] = {{0x1000, 0}, {0x1020, 0}, {0x1030, 0}, {kSettings, 0}, {0x1050, 0x100}};
    PauseRow out[6] = {};
    unsigned idx = 0;
    CHECK(PhotoRows::build(page, 5, kSettings, kPhoto, out, idx));
    CHECK(idx == 4 && out[5].label == 0x1050 && out[5].flags == 0x100);
}

TEST(the_largest_page_fits) {
    // seven slots full (six rows and SETTINGS): eight rows with PHOTO MODE
    PauseRow page[7], out[8] = {};
    for (unsigned i = 0; i < 7; ++i) page[i] = {0x1000u + 0x10u * i, 0};
    page[5].label = kSettings;
    unsigned idx = 0;
    CHECK(PhotoRows::build(page, 7, kSettings, kPhoto, out, idx));
    CHECK(idx == 6 && out[6].label == kPhoto && out[7].label == page[6].label);
}

TEST(an_unexpected_page_is_left_alone) {
    PauseRow page[8] = {}, out[9] = {};
    unsigned idx = 7;
    page[3].label = kSettings;
    CHECK(!PhotoRows::build(page, 2, kSettings, kPhoto, out, idx));    // too few rows
    CHECK(!PhotoRows::build(page, 8, kSettings, kPhoto, out, idx));    // more than the page's slots
    CHECK(!PhotoRows::build(page, 4, kSettings, kPhoto, out, idx));    // SETTINGS is not second to last (row 3 is the last)
    page[4].label = 0x1050;
    CHECK(PhotoRows::build(page, 5, kSettings, kPhoto, out, idx) && idx == 4);
    CHECK(idx == 4 && out[0].label == 0 && out[4].label == kPhoto);
    // nothing written on a refusal
    PauseRow o2[9] = {};
    CHECK(!PhotoRows::build(page, 2, kSettings, kPhoto, o2, idx) && o2[0].label == 0 && o2[1].label == 0);
}

TEST(confirm_maps_to_the_pages_rows) {
    // the PHOTO MODE row at 5: rows 0..4 are the page's, 5 is ours, 6 (QUIT) is the page's row 5
    for (unsigned sel = 0; sel < 5; ++sel) CHECK(PhotoRows::page_index(sel, 5) == int(sel));
    CHECK(PhotoRows::page_index(5, 5) == -1);
    CHECK(PhotoRows::page_index(6, 5) == 5);
    CHECK(PhotoRows::page_index(7, 6) == 6);
    CHECK(PhotoRows::page_index(0, 3) == 0 && PhotoRows::page_index(4, 3) == 3);
}

TEST_MAIN("pause menu PHOTO MODE row (photorow1)")
