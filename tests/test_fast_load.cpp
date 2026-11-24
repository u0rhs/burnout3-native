// fastload1: FAST LOADING (tools/native/fast_load.h): the loading detector, the per-VBlank decision, the pacer with a
// fake clock (a fast load saves its wall time, the VBlanks after it keep their real-time spacing: no catch-up burst),
// and the muted sound with its fades.
#include "test.h"

#include "fast_load.h"
#include "save_screens.h"
#include "../src/b3replay/game_behind.h"   // slowcpu
#include "../src/b3replay/fastforward.h"   // scenewait

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
    // stutter: the first paced VBlank is due now (base a period before it), the next a period after it
    CHECK(p.vblank(7, false, true, none) == Pacer::kNow && p.paced == 1 && p.base == 7 - kPeriod && p.stamp_ns() == 7);
    CHECK(p.vblank(8, false, false, none) == 7 + kPeriod);
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

// stutter: the polls' latch stamps are the schedule (Pacer::stamp_ns), not the late wake-ups: exactly one period apart
// whatever the wake-up; 0 unpaced (before --pace-from, a fast load); re-anchored once after a load
TEST(stamps_are_the_schedule) {
    Pacer p;
    p.period = kPeriod;
    const FastLoad::Step none;
    CHECK(p.vblank(5, true, true, none) == Pacer::kNow && p.stamp_ns() == 0);   // waiting: unpaced
    std::int64_t now = 1000000000;
    p.vblank(now, false, true, none);
    std::int64_t prev = p.stamp_ns();
    CHECK(prev == now);   // the first paced VBlank is due now
    std::uint32_t lcg = 1;
    for (int v = 0; v < 300; ++v) {
        const std::int64_t due = p.base + kPeriod * std::int64_t(p.paced + 1);
        lcg = lcg * 1664525u + 1013904223u;
        now = std::max(now, due) + std::int64_t(lcg >> 10) % 5000000;   // woken 0..5 ms late
        p.vblank(now, false, false, none);
        CHECK(p.stamp_ns() - prev == kPeriod);
        CHECK(p.behind == now - due && !p.late());   // how late it started; 0..5 ms is not catching up (not marked)
        prev = p.stamp_ns();
    }
    const std::int64_t due = p.base + kPeriod * std::int64_t(p.paced + 1);
    p.vblank(due + kPeriod + 1, false, false, none);   // more than a period late: catching up, marked
    CHECK(p.late() && p.behind == kPeriod + 1);
    now = due + kPeriod + 1;
    FastLoad::Step fast;
    fast.fast = true;
    p.vblank(now + kPeriod, false, false, fast);
    CHECK(p.stamp_ns() == 0);   // a fast load: unstamped
    FastLoad::Step end;
    end.ended = true;
    const std::int64_t after = now + 5 * kPeriod;
    p.vblank(after, false, false, end);
    CHECK(p.stamp_ns() == after);   // re-anchored at the load's end
    p.vblank(after + kPeriod + 1000000, false, false, none);
    CHECK(p.stamp_ns() == after + kPeriod);
}

// trafmem (Fable): a wait for the shot runner's full hold that ran out is not started again for that hold (a stalled
// renderer would hold the game 2 minutes a VBlank); waiting again once the hold moves
TEST(feed_wait_not_again_after_running_out) {
    b3native::FeedWait w;
    CHECK(!w.should_wait(false, 100));          // not full: no wait
    CHECK(w.should_wait(true, 100));            // full: wait
    w.ran_out(100);                             // ... it ran out on hold 100
    CHECK(!w.should_wait(true, 100));           // the next VBlanks: the same hold, no wait
    CHECK(!w.should_wait(true, 100));
    CHECK(w.should_wait(true, 140));            // the hold moved (a shot taken): wait again
    w.ran_out(140);
    CHECK(!w.should_wait(true, 140));
    CHECK(!w.should_wait(false, 180));          // moved and not full: no wait, the give-up forgotten
    CHECK(w.should_wait(true, 140));            // (back on 140 later: a different stretch, allowed)
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

// slowcpu (issue #15, Fable): the behind state: on only for sustained lateness (3 VBlanks more than two VBlanks late
// within 1 s, or 2 resyncs within 5 s), never one hitch; off after the hold on time (3 s), longer when it comes back
// within 10 s of a release (10 s, 30 s, then the rest of the race); reset() at a new race; the renderer's limit meanwhile
TEST(behind_real_time) {
    using b3native::BehindWatch;
    const std::int64_t T = 16683350, L = 2 * T + 1;   // L: late enough to count
    BehindWatch w;
    std::uint64_t v = 1000;
    CHECK(!w.step(v++, 10 * L, 0) && !w.behind);           // one hitch, however long: not behind
    for (int k = 0; k < 30; ++k) CHECK(!w.step(v++, 0, 0));
    CHECK(!w.step(v++, L, 0) && !w.behind);                // a second late VBlank within 1 s: still not
    CHECK(w.step(v++, L + 1, 0) && w.behind && w.hold() == 180);   // the third (a new delay) within 1 s: behind, 3 s hold
    for (int k = 0; k < 179; ++k) CHECK(!w.step(v++, 0, 0));
    CHECK(w.step(v++, 0, 0) && !w.behind);                 // 3 s on time: released
    // back within 10 s: the next hold is 10 s, then 30 s, then the rest of the race
    const std::uint64_t holds[] = {600, 1800, BehindWatch::kForever};
    for (std::uint64_t want : holds) {
        for (int k = 0; k < 100; ++k) w.step(v++, 0, 0);
        w.step(v++, L, 0);
        w.step(v++, L + 1, 0);
        CHECK(w.step(v++, L + 2, 0) && w.behind && w.hold() == want);
        if (want == BehindWatch::kForever) break;
        for (std::uint64_t k = 0; k + 1 < want; ++k) CHECK(!w.step(v++, 0, 0));
        CHECK(w.step(v++, 0, 0) && !w.behind);
    }
    for (int k = 0; k < 10000; ++k) CHECK(!w.step(v++, 0, 0));   // the rest of the race: held
    CHECK(w.behind && w.reset() && !w.behind && w.hold() == 180);   // a new race: from 3 s again
    // resyncs: two within 5 s turn it on; spread wider, they do not
    BehindWatch r;
    r.resyncs_seen = 7;
    v = 50000;
    CHECK(!r.step(v++, 0, 8));
    for (int k = 0; k < 400; ++k) CHECK(!r.step(v++, 0, 8));
    CHECK(!r.step(v++, 0, 9) && !r.behind);                // 401 VBlanks later: not within 5 s
    for (int k = 0; k < 100; ++k) r.step(v++, 0, 9);
    CHECK(r.step(v++, 0, 10) && r.behind);                 // the third, 101 after the second: on
    // (Fable) one stall of 100 ms: the catch-up VBlanks after it are each less late (100, 83, 67, 50 ...): one delay,
    // no hold; lateness that grows (a game slower than real time) holds within 3
    BehindWatch st;
    v = 70000;
    for (int k = 0; k < 10; ++k) st.step(v++, 0, 0);
    for (std::int64_t ms = 100; ms > 0; ms -= 17) CHECK(!st.step(v++, ms * 1000000, 0));
    for (int k = 0; k < 120; ++k) CHECK(!st.step(v++, 0, 0) && !st.behind);
    BehindWatch gr;
    v = 80000;
    CHECK(!gr.step(v++, 35000000, 0) && !gr.step(v++, 40000000, 0) && gr.step(v++, 45000000, 0) && gr.behind);
    // (Fable) two minutes without a trigger: one step back (a 30 s hold early in a race does not stay)
    BehindWatch dk;
    v = 90000;
    auto trigger = [&](BehindWatch& b) { b.step(v++, 40000000, 0); b.step(v++, 45000000, 0); return b.step(v++, 50000000, 0); };
    CHECK(trigger(dk) && dk.hold() == 180);
    for (int k = 0; k < 180; ++k) dk.step(v++, 0, 0);
    CHECK(!dk.behind && trigger(dk) && dk.hold() == 600);          // back within 10 s: 10 s
    for (int k = 0; k < 600; ++k) dk.step(v++, 0, 0);
    CHECK(!dk.behind && trigger(dk) && dk.hold() == 1800);         // again: 30 s
    for (int k = 0; k < 1800; ++k) dk.step(v++, 0, 0);
    CHECK(!dk.behind);
    for (int k = 0; k < 7200; ++k) dk.step(v++, 0, 0);             // two minutes quiet
    CHECK(dk.level == 1 && trigger(dk) && dk.hold() == 600);       // one step back: 10 s (not the rest of the race)
    // a late VBlank that is not late enough (one VBlank) never counts
    BehindWatch q;
    for (int k = 0; k < 100; ++k) CHECK(!q.step(k, T + 5, 0));
    using b3replay::behind_limit;
    CHECK(behind_limit(0, false) == 0 && behind_limit(144, false) == 144);   // the player's
    CHECK(behind_limit(0, true) == 60 && behind_limit(144, true) == 60 && behind_limit(30, true) == 30 && behind_limit(60, true) == 60);
}

TEST(scene_wait_goes_on_only_while_the_scene_is_being_built) {
    using b3replay::scene_wait_more;
    CHECK(scene_wait_more(100, 3000, false) && scene_wait_more(100, 3000, true));   // within the cap: as before
    CHECK(!scene_wait_more(3000, 3000, false));                                    // the cap, the scene ready: no more
    CHECK(scene_wait_more(3000, 3000, true) && scene_wait_more(9999, 3000, true));   // the route / preload still coming
    CHECK(!scene_wait_more(b3replay::kSceneBuildMaxMs, 3000, true));                // never past 10 s
    CHECK(scene_wait_more(500, 0, true) && !scene_wait_more(500, 0, false));        // a load slower than real time
}

TEST_MAIN("test_fast_load (tools/native/fast_load.h)")
