// mac5: the video skip hint (remaster-features §2): the skip's condition on a fake decoder (tools/native/movie_skip.h),
// the hint's opacity in game time (SkipHintFade), its record (src/b3replay/hud.h 'HSK1') and its picture
// (src/b3replay/skiphint.h).
#include "test.h"

#include "movie_skip.h"
#include "skiphint.h"

#include <array>
#include <cstring>
#include <map>

namespace {

using b3native::MovieSkip;
using b3native::SkipHintFade;

std::map<std::uint32_t, std::uint8_t> g_mem;   // the guest bytes the skip reads

std::array<unsigned char, 32> neutral() {
    std::array<unsigned char, 32> s{};
    s.fill(0);
    s[0] = s[1] = 0xFF;
    for (int k = 4; k < 8; ++k) s[k] = 0x80;
    return s;
}

MovieSkip skip_on_a_movie(const char* name) {
    g_mem.clear();
    for (std::uint32_t i = 0; name[i]; ++i) g_mem[0x1000u + i] = std::uint8_t(name[i]);
    g_mem[0x1000u + std::uint32_t(std::strlen(name))] = 0;
    MovieSkip m;
    m.load8 = [](std::uint32_t a) { auto it = g_mem.find(a); return it == g_mem.end() ? std::uint8_t(0) : it->second; };
    m.store8 = [](std::uint32_t a, std::uint8_t v) { g_mem[a] = v; };
    m.prepared(0x1000u);
    g_mem[MovieSkip::kDecoder + MovieSkip::kPlaying] = 1;
    return m;
}

}  // namespace

TEST(skippable_is_the_skips_condition) {
    MovieSkip m = skip_on_a_movie("MOVIES\\EALOGO.PSS");
    CHECK(m.skippable());
    m.enabled = false;
    CHECK(!m.skippable());
    m.enabled = true;
    g_mem[MovieSkip::kDecoder + MovieSkip::kLoop] = 1;   // a looping background
    CHECK(!m.skippable());
    g_mem[MovieSkip::kDecoder + MovieSkip::kLoop] = 0;
    MovieSkip t = skip_on_a_movie("TRACKS\\TITLES.PSS");   // the title's background: exempt
    CHECK(!t.skippable());
    // the press skips exactly when skippable, as before: Abort set, the press masked until released
    MovieSkip s = skip_on_a_movie("MOVIES\\EALOGO.PSS");
    auto r = neutral();
    r[1] = std::uint8_t(r[1] & ~0x40);   // Cross down
    CHECK(s.apply(r.data()) && s.skips() == 1 && g_mem[MovieSkip::kDecoder + MovieSkip::kAbort] == 1);
    CHECK(r[1] == 0xFF);   // masked
    CHECK(!s.skippable());   // ending: the hint goes
    auto up = neutral();
    CHECK(!s.apply(up.data()));
}

TEST(fade_in_game_time) {
    SkipHintFade f;
    const auto n = neutral();
    CHECK(f.step(false, n.data()) == 0);
    int first = -1, full = -1, gone = -1;
    for (int v = 0; v < SkipHintFade::kDelay + SkipHintFade::kHold + 200; ++v) {
        const std::uint8_t o = f.step(true, n.data());
        if (o && first < 0) first = v;
        if (o == 255 && full < 0) full = v;
        if (full >= 0 && o == 0 && gone < 0) gone = v;
    }
    CHECK(first == SkipHintFade::kDelay);                      // not from the movie's first frame
    CHECK(full > first && full < SkipHintFade::kDelay + SkipHintFade::kIn);   // in kIn VBlanks
    CHECK(gone > SkipHintFade::kDelay + SkipHintFade::kHold && gone < SkipHintFade::kDelay + SkipHintFade::kHold + SkipHintFade::kOut);
    // a press brings it back (and a held one does not keep it)
    auto p = neutral();
    p[0] = 0xFE;
    CHECK(f.step(true, p.data()) > 0);
    for (int v = 0; v < SkipHintFade::kHold + SkipHintFade::kOut; ++v) f.step(true, p.data());
    CHECK(f.step(true, p.data()) == 0);
    // a press early shows it at once; the movie's end fades it out
    SkipHintFade g;
    g.step(true, n.data());
    CHECK(g.step(true, p.data()) > 0);
    for (int v = 0; v < 20; ++v) g.step(true, n.data());
    std::uint8_t last = 255;
    for (int v = 0; v < SkipHintFade::kOut; ++v) last = g.step(false, n.data());
    CHECK(last == 0);
    // a stick past half way counts, a small offset (drift) does not
    SkipHintFade h;
    auto drift = neutral();
    drift[6] = 0x90;
    h.step(true, n.data());
    CHECK(h.step(true, drift.data()) == 0);
    auto stick = neutral();
    stick[6] = 0x10;
    CHECK(h.step(true, stick.data()) > 0);
}

TEST(picture_per_device) {
    using b3replay::PromptDevice;
    using b3replay::SkipHint;
    for (PromptDevice d : {PromptDevice::kKeyboard, PromptDevice::kPlayStation, PromptDevice::kXbox, PromptDevice::kNintendo}) {
        for (int wh : {448, 720, 1440}) {
            int w = 0, h = 0;
            std::vector<std::uint8_t> px;
            SkipHint::picture(d, wh, w, h, px);
            CHECK(h > 0 && w > h && w < 8 * h && px.size() == std::size_t(w) * std::size_t(h) * 4);
            CHECK(h <= wh / 12);   // small: under a twelfth of the window
            // the pill is translucent, its letters near-opaque white, the corners clear
            std::size_t opaque = 0;
            for (std::size_t i = 0; i < px.size(); i += 4) opaque += px[i + 3] >= 200 && px[i] > 160;
            CHECK(opaque > std::size_t(h));
            CHECK(px[3] == 0);
            int x = 0, y = 0;   // placed in the window's bottom right, inside its centred 16:9 part
            const int ww = wh * 32 / 9;
            SkipHint::place(ww, wh, w, h, x, y);
            CHECK(x > ww / 2 && x + w <= (ww + wh * 16 / 9) / 2 && y + h < wh && y > wh / 2);
        }
    }
    int wk = 0, wp = 0, h = 0;
    std::vector<std::uint8_t> px;
    SkipHint::picture(PromptDevice::kKeyboard, 720, wk, h, px);
    SkipHint::picture(PromptDevice::kPlayStation, 720, wp, h, px);
    CHECK(wk > wp);   // the key cap's name is wider than a face button
}

TEST_MAIN("video skip hint (mac5)")
