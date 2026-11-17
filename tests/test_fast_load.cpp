// fastload1: FAST LOADING (tools/native/fast_load.h): the loading detector, the per-VBlank decision, the pacer with a
// fake clock (a fast load saves its wall time, the VBlanks after it keep their real-time spacing: no catch-up burst),
// and the muted sound with its fades.
#include "test.h"

#include "fast_load.h"
#include "save_screens.h"

#include <cstdint>
#include <vector>

namespace {

using b3native::AudioGate;
using b3native::FastLoad;
using b3native::Pacer;

constexpr std::int64_t kPeriod = 16683350;   // 1 / 59.94 Hz

// A run of `n` VBlanks with the loading screen up on [load_lo, load_hi); each VBlank costs `work` ns of host time
// (`fast_work` while loading). The pacer's waits advance the fake clock. Returns each VBlank's wall time (ns) after its
// wait, the time the game goes on with it.
struct Run {
    std::vector<std::int64_t> at;
    std::uint64_t resyncs = 0, fast = 0, spans = 0;
};
Run run(bool enabled, int n, int load_lo, int load_hi, std::int64_t work, std::int64_t fast_work) {
    FastLoad fl;
    Pacer p;
    p.period = kPeriod;
    std::int64_t now = 1000000000;
    p.base = now;
    Run r;
    for (int v = 1; v <= n; ++v) {
        const bool loading = v >= load_lo && v < load_hi;
        const FastLoad::Step s = fl.step(enabled, loading);
        const std::int64_t due = p.vblank(now, false, false, s);
        if (due != Pacer::kNow && due > now) now = due;
        r.at.push_back(now);
        now += loading ? fast_work : work;
    }
    r.resyncs = p.resyncs;
    r.fast = fl.vblanks();
    r.spans = fl.spans();
    return r;
}

}  // namespace

TEST(detector_is_the_loading_page) {
    CHECK(b3native::loading_page_up(0x01E9583Cu));
    CHECK(b3native::loading_page_up(0x21E9583Cu));   // a KSEG / uncached alias of the same object
    CHECK(!b3native::loading_page_up(0));
    CHECK(!b3native::loading_page_up(0x01E95840u));   // another page object of the menus
    CHECK(b3native::kMenuCurrentPage == 0x01E90240u + 0x1F0u);
}

TEST(step_spans_and_edges) {
    FastLoad f;
    const bool loading[] = {false, true, true, true, false, false, true, false};
    std::vector<FastLoad::Step> s;
    for (bool l : loading) s.push_back(f.step(true, l));
    CHECK(!s[0].fast && !s[0].began && !s[0].ended);
    CHECK(s[1].fast && s[1].began && !s[1].ended);
    CHECK(s[2].fast && !s[2].began);
    CHECK(!s[4].fast && s[4].ended && !s[4].began);
    CHECK(!s[5].ended);
    CHECK(s[6].began && s[7].ended);
    CHECK(f.spans() == 2 && f.vblanks() == 4 && f.span_vblanks() == 1);
}

TEST(sound_tail_plays_paced_first) {
    // a load whose first 5 VBlanks still carry the sound before it: paced through them, fast from the first quiet one
    FastLoad f;
    CHECK(!f.step(true, false, false).fast);
    for (int k = 0; k < 5; ++k) CHECK(!f.step(true, true, false).fast);
    const FastLoad::Step s = f.step(true, true, true);
    CHECK(s.fast && s.began);
    CHECK(f.step(true, true, false).fast);   // a sound inside the load does not pace it again
    CHECK(f.step(true, false, false).ended);
}

TEST(sound_tail_capped) {
    // a load that is never quiet goes fast after kMaxTail paced VBlanks
    FastLoad f;
    std::uint64_t paced = 0;
    for (int k = 0; k < 100; ++k) if (!f.step(true, true, false).fast) ++paced;
    CHECK(paced == b3native::kMaxTail);
    CHECK(f.spans() == 1 && f.vblanks() == 100 - b3native::kMaxTail);
    f.step(true, false, false);
    // the next load counts its own tail
    paced = 0;
    for (int k = 0; k < 40; ++k) if (!f.step(true, true, false).fast) ++paced;
    CHECK(paced == b3native::kMaxTail);
}

TEST(off_or_unpaced_never_fast) {
    FastLoad f;
    for (int k = 0; k < 10; ++k) {
        const FastLoad::Step s = f.step(false, true);
        CHECK(!s.fast && !s.began && !s.ended);
    }
    CHECK(f.spans() == 0 && f.vblanks() == 0);
}

TEST(setting_turned_off_mid_load_ends_span) {
    FastLoad f;
    f.step(true, true);
    const FastLoad::Step s = f.step(false, true);
    CHECK(!s.fast && s.ended);
}

TEST(pacer_paces_to_real_time) {
    const Run r = run(false, 600, 100, 400, 2000000, 2000000);
    CHECK(r.resyncs == 0 && r.fast == 0);
    for (int v = 1; v < 600; ++v) CHECK(r.at[std::size_t(v)] - r.at[std::size_t(v - 1)] == kPeriod);
}

TEST(fast_load_saves_its_wall_time) {
    const Run off = run(false, 600, 100, 400, 2000000, 2000000);
    const Run on = run(true, 600, 100, 400, 2000000, 2000000);
    CHECK(on.spans == 1 && on.fast == 300);
    // 300 VBlanks at 2 ms each instead of 300 periods
    const std::int64_t saved = (off.at.back() - off.at.front()) - (on.at.back() - on.at.front());
    CHECK_NEAR(double(saved), 300.0 * double(kPeriod - 2000000), double(kPeriod));
    CHECK(on.resyncs == 0);
}

TEST(no_catch_up_burst_after_a_load) {
    const Run on = run(true, 600, 100, 400, 2000000, 2000000);
    // the load's VBlanks go on at once (2 ms apart); the first one after it at once too, then one period apart each
    for (int v = 100; v < 399; ++v) CHECK(on.at[std::size_t(v)] - on.at[std::size_t(v - 1)] == 2000000);
    for (int v = 400; v < 600; ++v) CHECK(on.at[std::size_t(v)] - on.at[std::size_t(v - 1)] == kPeriod);
}

TEST(slow_load_host_still_resumes_cleanly) {
    // a host slower than real time in the load (30 ms a VBlank): nothing to gain, but no resync and no burst after it
    const Run on = run(true, 600, 100, 400, 2000000, 30000000);
    CHECK(on.resyncs == 0);
    for (int v = 400; v < 600; ++v) CHECK(on.at[std::size_t(v)] - on.at[std::size_t(v - 1)] == kPeriod);
}

TEST(pace_from_first_vblank_unchanged) {
    // --pace-from: unpaced while waiting, then base = now at the first paced VBlank (play1's rule)
    Pacer p;
    p.period = kPeriod;
    const FastLoad::Step none;
    CHECK(p.vblank(5, true, true, none) == Pacer::kNow && p.paced == 0);
    CHECK(p.vblank(7, false, true, none) == Pacer::kNow && p.paced == 1 && p.base == 7);
    CHECK(p.vblank(8, false, false, none) == 7 + 2 * kPeriod);
}

TEST(behind_resyncs) {
    Pacer p;
    p.period = kPeriod;
    const FastLoad::Step none;
    CHECK(p.vblank(0, false, false, none) == kPeriod);
    CHECK(p.vblank(kPeriod + 300000000, false, false, none) == Pacer::kNow);   // 300 ms late
    CHECK(p.resyncs == 1);
    CHECK(p.vblank(kPeriod + 300000000, false, false, none) == kPeriod + 300000000 + kPeriod);
}

TEST(audio_gate_mutes_with_fades) {
    AudioGate g;
    std::vector<std::int16_t> one(800 * 2, 10000);
    CHECK(g.apply(one.data(), 800) == one.data());   // open: passed through untouched
    g.set_muted(true);
    const std::int16_t* out = g.apply(one.data(), 800);   // the muting VBlank: faded out
    CHECK(out != nullptr && out != one.data());
    CHECK(out[0] == 10000 && out[1] == 10000);
    CHECK(out[2 * 799] == 0 && out[2 * 799 + 1] == 0);
    for (int k = 1; k < 800; ++k) CHECK(out[2 * k] <= out[2 * (k - 1)]);
    CHECK(g.apply(one.data(), 800) == nullptr);   // then dropped
    CHECK(g.apply(one.data(), 800) == nullptr);
    CHECK(g.dropped() == 1600);
    g.set_muted(false);
    out = g.apply(one.data(), 800);   // the first VBlank after: faded in
    CHECK(out != nullptr && out[0] == 0 && out[2 * 799] == 10000);
    for (int k = 1; k < 800; ++k) CHECK(out[2 * k] >= out[2 * (k - 1)]);
    CHECK(g.apply(one.data(), 800) == one.data());
}

TEST(audio_gate_repeated_set_is_no_edge) {
    AudioGate g;
    std::vector<std::int16_t> one(800 * 2, -5000);
    g.set_muted(false);
    CHECK(g.apply(one.data(), 800) == one.data());
    g.set_muted(true);
    g.set_muted(true);
    CHECK(g.apply(one.data(), 800) != nullptr);   // one fade
    CHECK(g.apply(one.data(), 800) == nullptr);
}

// loadlag1: the wait for the race's scene at a fast load's end never makes the load slower than at real time
TEST(scene_hold_within_the_real_time_load) {
    CHECK(b3native::scene_hold_ms(6.22, 1.16) == b3native::kSceneHoldMaxMs);   // the Single Event load: 5 s saved, the cap
    CHECK(b3native::scene_hold_ms(2.5, 2.25) == 250);                 // 0.25 s left of the real-time load
    CHECK(b3native::scene_hold_ms(2.27, 2.27) == 0);
    CHECK(b3native::scene_hold_ms(2.27, 2.5) == 0);                   // a load slower than real time: no wait
    CHECK(b3native::scene_hold_ms(0.0, 0.0) == 0);
}

// autosave1 (tools/native/save_screens.h): the AUTOSAVE page is the card flow's page object and no other page; its span runs
// as a fast load does (the FastLoad rule with `loading` the page's)
TEST(autosave_page_span) {
    CHECK(b3native::autosave_page_up(0x01E97DACu) && b3native::autosave_page_up(0x21E97DACu));   // (a cached alias of the same RAM word)
    CHECK(!b3native::autosave_page_up(0x01E96424u));   // DRIVER DETAILS > PROFILE > SAVE PROFILE
    CHECK(!b3native::autosave_page_up(0x01E96418u));   // LOAD PROFILE
    CHECK(!b3native::autosave_page_up(b3native::kLoadingScreenPage) && !b3native::autosave_page_up(0));
    // the loading screen, then the AUTOSAVE page, then the next page: one fast span to the page's end
    FastLoad fl;
    std::uint64_t unpaced = 0;
    for (int v = 0; v < 60; ++v) {
        const bool loading = v < 20, autosave = v >= 20 && v < 50;
        const FastLoad::Step s = fl.step(true, loading || autosave, true);
        unpaced += s.fast;
        CHECK(s.began == (v == 0) && s.ended == (v == 50));
    }
    CHECK(unpaced == 50 && fl.spans() == 1);
}

TEST_MAIN("test_fast_load (tools/native/fast_load.h)")
