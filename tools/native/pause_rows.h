// pause_rows.h -- the pure part of the pause menu's PHOTO MODE row (photorow1, 2026-11-15; tools/native/pc_menu.h applies it
// to guest memory; tests/test_pause_rows.cpp).
//
// The game's pause page holds seven row slots, full once SETTINGS is added (pc_menu.h), so the selection is handed a private
// array: the page's rows up to and including SETTINGS, PHOTO MODE, then the page's last row. The page's own tables (the
// action codes, the enabled flags) keep the page's indices, so for the rows after PHOTO MODE the game's Update is shown the
// index one lower.
#ifndef B3NATIVE_PAUSE_ROWS_H
#define B3NATIVE_PAUSE_ROWS_H

#include <cstdint>

namespace b3native {

struct PauseRow {
    std::uint32_t label = 0;   // the guest address of the row's UTF-16 text
    std::uint32_t flags = 0;   // +4: the row disabled (byte 4), byte 5 0
};

struct PhotoRows {
    static constexpr unsigned kMaxPageRows = 7;   // the page's slots
    static constexpr unsigned kMinPageRows = 3;   // RESUME-like row, SETTINGS, the last row
    // `page`: the page's rows (count `n`, SETTINGS second to last). Writes `out` (n + 1 rows) and the PHOTO MODE row's index
    // in `photo_idx`; false (nothing written) when the page is not in the expected shape.
    static bool build(const PauseRow* page, unsigned n, std::uint32_t settings_label, std::uint32_t photo_label, PauseRow* out,
                      unsigned& photo_idx) {
        if (n < kMinPageRows || n > kMaxPageRows || page[n - 2].label != settings_label) return false;
        for (unsigned i = 0; i + 1 < n; ++i) out[i] = page[i];
        out[n - 1] = PauseRow{photo_label, 0};
        out[n] = page[n - 1];
        photo_idx = n - 1;
        return true;
    }
    // What Confirm on selection `sel` means with the PHOTO MODE row at `photo_idx`: the row itself (-1), or the page's own
    // index to show the game (the rows after it are one up).
    static int page_index(unsigned sel, unsigned photo_idx) {
        if (sel == photo_idx) return -1;
        return int(sel > photo_idx ? sel - 1u : sel);
    }
};

}  // namespace b3native

#endif
