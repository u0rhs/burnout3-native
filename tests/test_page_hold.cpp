// online-det: the pad withheld from the game for the PC SETTINGS page (tools/native/page_hold.h). What the game sees must
// depend on the VBlank-stamped pad samples only, never on the VBlank at which the render thread opens or closes the page.
#include "test.h"

#include "page_hold.h"

#include <array>
#include <cstdint>
#include <vector>

namespace {

using b3native::PageHold;
using Sample = std::array<std::uint8_t, 32>;

Sample neutral() {
    Sample s{};
    s[0] = s[1] = 0xFF;
    s[2] = 0x80; s[3] = 0x7F; s[4] = 0x80; s[5] = 0x7F;
    return s;
}
Sample with_button(unsigned bit) {   // active low, bytes 0-1
    Sample s = neutral();
    s[bit / 8] = std::uint8_t(s[bit / 8] & ~(1u << (bit % 8)));
    return s;
}
constexpr unsigned kCross = 14, kTriangle = 12;

// The online tour's shape: Cross held 0..9 (SETTINGS confirmed at 0, requested at 1), Triangle held 1000..1009 (the
// page's Back). The render thread shows the page open `open_lag` VBlanks after the request and closed `close_lag`
// VBlanks after the Triangle went down. Returns per VBlank whether the game saw a neutral pad.
std::vector<bool> run(int open_lag, int close_lag) {
    PageHold h;
    std::vector<bool> held;
    bool active = false;
    for (int v = 0; v < 1100; ++v) {
        Sample s = neutral();
        if (v < 10) s = with_button(kCross);
        if (v >= 1000 && v < 1010) s = with_button(kTriangle);
        if (v == 1) h.opened();
        if (v == 1 + open_lag) active = true;
        if (v == 1000 + close_lag) active = false;
        held.push_back(h.step(active, s.data()));
    }
    return held;
}

}  // namespace

TEST(released_report) {
    CHECK(PageHold::released(neutral().data()));
    CHECK(!PageHold::released(with_button(kCross).data()));
    CHECK(!PageHold::released(with_button(3).data()));   // Start
    Sample s = neutral();
    s[5] = 0xFF;   // left stick fully down
    CHECK(!PageHold::released(s.data()));
    s[5] = 0x90;   // a drifting stick
    CHECK(PageHold::released(s.data()));
}

TEST(held_from_request_to_release) {
    const std::vector<bool> h = run(1, 1);
    CHECK(!h[0]);
    for (int v = 1; v < 1010; ++v) CHECK(h[v]);   // through the Triangle's release
    for (int v = 1010; v < 1100; ++v) CHECK(!h[v]);
}

TEST(independent_of_render_thread_lag) {
    const std::vector<bool> ref = run(1, 1);
    for (int open_lag : {0, 1, 2, 3, 4, 9, 10, 50})
        for (int close_lag : {0, 1, 2, 5, 9})
            CHECK(run(open_lag, close_lag) == ref);
}

TEST(late_close_holds_until_seen_closed) {
    // a render thread later than the Triangle: the game sees neutral until the page is closed
    const std::vector<bool> h = run(2, 30);
    for (int v = 1; v < 1030; ++v) CHECK(h[v]);
    for (int v = 1030; v < 1100; ++v) CHECK(!h[v]);
}

TEST(no_request_follows_active) {
    // photo mode (b3r_settings_active without a page request): the hold is whatever the renderer says
    PageHold h;
    const Sample n = neutral();
    CHECK(!h.step(false, n.data()));
    CHECK(h.step(true, n.data()));
    CHECK(!h.step(false, n.data()));
    CHECK(!h.holding());
}

TEST(no_window_sample_releases_after_close) {
    PageHold h;
    h.opened();
    CHECK(h.step(false, nullptr));   // requested, not yet open
    CHECK(h.step(true, nullptr));
    CHECK(!h.step(false, nullptr));
}

TEST_MAIN("PC SETTINGS pad hold (online-det)")
