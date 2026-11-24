// title_fast.h -- titlefast (2026-11-21): the title screen's "PRESS START" prompt and its Start at once, not two seconds
// after the page is prepared.
//
// What the game does [C the decomp; addresses retail]:
// - The title page D_01E97E1C: its Prepare func_00438910 stores the game clock D_004F50A0 (a float, seconds) at +0x14 and
//   prepares the title component D_01E8D550 (CB3TitleScreenComponent, its Prepare func_00343A50 stores the clock at
//   +0x2C). The page's Update func_00438840, while the page manager takes input (D_01E90424 == 5): after 90 s without
//   input the attract mode (StartLeadOutFore, event 5); else Start (CB3InputManager::GetStartButton) leads out (event 1)
//   only when the clock is past +0x14 + 2.0. The component's Update func_00343800 in that state draws the prompt with
//   alpha 0 while the clock is within +0x2C + 2.0, then 0.5 + sin(pi x the clock's fraction) (the blink).
// - The page takes input when its lead-in is over, about a second after its Prepare: so for about a second the title
//   shows no prompt and ignores Start. That is the wait on the first load after the intro movies (and every time the
//   title comes back).
// Ours (wrappers on the hook table's port layer): while the page takes input and the clock is within the two seconds,
// the two Updates see their stamps two seconds further back for the call (the page's +0x14, the component's +0x2C;
// put back after it): the prompt blinks from the first input frame, and Start is taken there. Drawing the prompt in
// those frames costs EE time, so the later VBlanks' timing moves (remaster-features §26: the golden record). Nothing else of the page changes (its lead-in, the 90 s attract, the blink's phase).
// On unless B3NATIVE_TITLE_FAST=0; a replay plays with its recording's "title_fast" entry (none: off, as recorded).
#ifndef B3NATIVE_TITLE_FAST_H
#define B3NATIVE_TITLE_FAST_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "hook_table.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace b3native {

constexpr std::uint32_t kTitlePage = 0x01E97E1Cu;          // [C] the title page (Prepare 0x438910, Update 0x438840)
constexpr std::uint32_t kTitleComponent = 0x01E8D550u;     // [C] CB3TitleScreenComponent (Prepare 0x343A50, Update 0x343800)
constexpr std::uint32_t kGameClock = 0x004F50A0u;          // [C] D_004F50A0: the game's clock (float seconds)
constexpr float kTitleWait = 2.0f;                         // [C] the page's and the component's wait

// The stamp a call sees: the game's, or (fast, within the wait) one that is past it. Pure (tests/test_title_fast.cpp).
struct TitleStamp {
    float stamp = 0.0f;
    bool moved = false;
};
inline TitleStamp title_stamp(bool fast, bool input, float clock, float stamp) {
    TitleStamp t{stamp, false};
    if (fast && input && clock <= stamp + kTitleWait) {
        t.stamp = clock - 2.0f * kTitleWait;   // (well past the game's "clock <= stamp + 2.0")
        t.moved = true;
    }
    return t;
}
inline bool title_fast_entry(const char* v) { return v && v[0] == '1' && !v[1]; }

class TitleFast {
public:
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_TITLE_FAST");
        return !(e && e[0] == '0' && e[1] == 0);
    }
    void set_on_query(std::function<bool()> f) { on_ = std::move(f); }
    bool install(HookTable& hooks);
    struct Counts {
        std::uint64_t prompt_early = 0, page_early = 0, start_early = 0;
    };
    const Counts& counts() const { return counts_; }

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static TitleFast*& instance() { static TitleFast* p = nullptr; return p; }
    static void page_update(const b3host::CallArgs& a, b3host::CallResult& r);
    static void component_update(const b3host::CallArgs& a, b3host::CallResult& r);
    // calls `next` with the stamp at `at` moved when the rule says so (put back after); true when it was moved
    bool call_moved(Entry next, const b3host::CallArgs& a, b3host::CallResult& r, std::uint32_t at, float& waited);
    Entry page_update_ = nullptr, component_update_ = nullptr;
    std::function<bool()> on_;
    Counts counts_;
    float logged_stamp_ = -1.0f;   // the page visit (its stamp) last logged
};

inline bool TitleFast::install(HookTable& hooks) {
    const std::vector<Hook> set = {{0x00438840u, HookLayer::kPort, &page_update, &page_update_},
                                   {0x00343800u, HookLayer::kPort, &component_update, &component_update_}};
    for (const Hook& h : set)
        if (!hooks.has_row(h.address)) {
            std::fprintf(stderr, "title_fast: %08X is not native: the title's wait left as retail\n", h.address);
            return false;
        }
    std::string error;
    if (!hooks.install("port:title_fast", set, &error)) {
        std::fprintf(stderr, "title_fast: %s: the title's wait left as retail\n", error.c_str());
        return false;
    }
    instance() = this;
    std::printf("title_fast: the title's PRESS START prompt and Start from its first input frame (B3NATIVE_TITLE_FAST=0: "
                "the game's two-second wait)\n");
    std::fflush(stdout);
    return true;
}

inline bool TitleFast::call_moved(Entry next, const b3host::CallArgs& a, b3host::CallResult& r, std::uint32_t at, float& waited) {
    const bool input = b3host::load<std::uint32_t>(0x01E90424u) == 5u;   // the page manager takes input [C D_01E90424]
    const float clock = b3host::load<float>(kGameClock), stamp = b3host::load<float>(at);
    const TitleStamp t = title_stamp(on_ && on_(), input, clock, stamp);
    waited = clock - stamp;
    if (!t.moved) {
        next(a, r);
        return false;
    }
    b3host::store<float>(at, t.stamp);
    next(a, r);
    if (b3host::load<float>(at) == t.stamp) b3host::store<float>(at, stamp);   // (the game's stamp back)
    return true;
}

// func_00438840 (the title page's Update): Start taken within the game's wait
inline void TitleFast::page_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    TitleFast* p = instance();
    if ((a.gpr[0].w[0] & 0x01FFFFFFu) != kTitlePage) {
        p->page_update_(a, r);
        return;
    }
    float waited = 0.0f;
    const std::uint32_t before = b3host::load<std::uint32_t>(0x01E90424u);
    const float stamp = b3host::load<float>(kTitlePage + 0x14u);
    if (!p->call_moved(p->page_update_, a, r, kTitlePage + 0x14u, waited)) return;
    ++p->counts_.page_early;
    if (stamp != p->logged_stamp_) {
        p->logged_stamp_ = stamp;
        std::printf("title_fast: the title takes input %.3f s after its Prepare: the prompt and Start from now (the game's "
                    "wait: %.1f s)\n", double(waited), double(kTitleWait));
        std::fflush(stdout);
    }
    if (b3host::load<std::uint32_t>(0x01E90424u) != before) {   // Start led out
        ++p->counts_.start_early;
        std::printf("title_fast: Start taken %.3f s after the title's Prepare (the game would wait until %.1f s)\n",
                    double(waited), double(kTitleWait));
        std::fflush(stdout);
    }
}

// func_00343800 (the title component's Update): the prompt drawn within the game's wait
inline void TitleFast::component_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    TitleFast* p = instance();
    if ((a.gpr[0].w[0] & 0x01FFFFFFu) != kTitleComponent) {
        p->component_update_(a, r);
        return;
    }
    float waited = 0.0f;
    if (p->call_moved(p->component_update_, a, r, kTitleComponent + 0x2Cu, waited)) ++p->counts_.prompt_early;
}

}  // namespace b3native

#endif
