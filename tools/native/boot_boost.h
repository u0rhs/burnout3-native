// boot_boost.h -- gamemods1 (2026-11-21): START AT (PC SETTINGS > GAME > STARTING AND LOADING; ini `boot_to`, default 0 =
// the game's start): after the game has loaded, go straight to the title screen, or to the main menu with a profile
// loaded, without the intro films.
//
// What the game does at the boot [C decomp, retail asm; checked in runs, 2026-11-21]:
// - The loading page's Prepare func_00321780, on the first load, hands the front end CB3FrontEnd 0x0051A6A8 its first
//   menu with func_00130C20(front end, id): the intro sequence 0x94413E2EEC056247: the films englis30, cri_rw30 and
//   Titles30 (VBlanks 383-4650 of an unpaced run), then the title (its TRACKS background movie loops).
// - The title's Update func_00438840: while the page manager takes input (D_01E90424 == 5) and the pad was used in the
//   last 90 s, Start pressed more than 2 s after the title came up calls StartLeadOut__18CB3MenuPageManager (event 1);
//   after 90 s without input it leaves for the attract mode.
// - That Start reaches func_0030E930's flow event 3: with the boot flag D_0051A998 set (the first Start since the
//   boot) it goes to the profile choice (LOAD PROFILE / NEW PROFILE); with the flag clear, to the main menu.
// - CB3MyBurnoutProfileLoadPage::Update (func_003ED460) loads a profile with func_00421000(memory card controller
//   0x01ECD520, slot 0..2) when the player confirms one.
// (Handing func_00130C20 the title's id 0x60D6B07214065F27 at the boot does show the title at once, but in the port the title's Start then leads nowhere: the title leaves and no page follows. So the port keeps
// the game's intro sequence and ends its films instead.)
//
// The port's choices (BootBoostRule::Target):
// - kTitle: each film of the intro ends as soon as it starts (MovieSkip::abort_now: the game's own CGtVideoDecoder
//   Abort, as a skip press does) until the title's Update first runs: the game goes its own way to the title, about
//   4000 VBlanks sooner.
// - kProfile1..3: the same, then the port presses Start in player 1's report (native_play: press_start()) as soon as
//   the title would take it, until the title answers (at most kPressVBlanks); at flow event 3 with the boot flag set the
//   profile is loaded (func_00421000, as the profile page does) and the flag cleared, so the event goes on to the main
//   menu. A run lands on the main menu with that profile, as after LOAD PROFILE > PROFILE n. Only a profile the save
//   holds is loaded (native_play reads the save file: src/native/save_stats.h); for an empty slot, or no save, the
//   event goes the game's way, to its profile choice (the game would load an empty slot as a blank profile and save it).
// Hooks on the port layer; with the default (kGame) every call goes through unchanged and no film is ended (the setting
// is read once, at the first film: a change applies at the next start). B3NATIVE_BOOT_TO=0..4 overrides it (tests);
// B3NATIVE_BOOT_LOG=1 logs the hooked calls. Replays record the setting (`boot_to`) and play their start the same way.
#ifndef B3NATIVE_BOOT_BOOST_H
#define B3NATIVE_BOOT_BOOST_H

#include <cstdint>

namespace b3native {

// The pure rules (tests/test_boot_boost.cpp)
struct BootBoostRule {
    enum Target : int { kGame = 0, kTitle = 1, kProfile1 = 2, kProfile2 = 3, kProfile3 = 4 };
    static constexpr float kTitleWait = 2.0f;   // the title's own wait before it takes Start (func_00438840)
    static int clamp(int t) { return t < kGame || t > kProfile3 ? kGame : t; }
    static bool profile(int t) { return t >= kProfile1 && t <= kProfile3; }
    static int slot(int t) { return t - kProfile1; }   // func_00421000's profile slot 0..2
    // Each VBlank while a film may play: end the intro's films (not after the title has come up)
    bool skip_films(int target) const { return target != kGame && !title_seen_; }
    // The title's Update ran (the intro is over): from now on films are the game's
    void title_up(int target) {
        if (title_seen_) return;
        title_seen_ = true;
        start_ = profile(target);
    }
    static constexpr int kPressVBlanks = 30;   // the press's longest hold
    // The title's Update: the title would take Start now (the page manager takes input, the title's wait is over)
    void title_ready(int page_state, float now, float title_since) {
        if (start_ && page_state == 5 && now > title_since + kTitleWait) ready_ = true;
    }
    // Each VBlank (native_play, player 1's report): press Start now
    bool press() {
        if (!start_ || !ready_) return false;
        if (++held_ > kPressVBlanks) { start_ = false; return false; }
        return true;
    }
    // Flow event 3 with the boot flag set: load the profile (true: the caller loads slot(target) and clears the flag)
    bool autoload(int target, int event, bool boot_flag) {
        if (event == 3) start_ = false;   // the title took the Start: release it
        if (!profile(target) || event != 3 || !boot_flag || loaded_) return false;
        loaded_ = true;
        return true;
    }
    bool start_pending() const { return start_; }
    bool loaded() const { return loaded_; }

private:
    bool title_seen_ = false, start_ = false, ready_ = false, loaded_ = false;
    int held_ = 0;
};

}  // namespace b3native

#ifndef B3NATIVE_BOOT_RULE_ONLY
#include "hook_table.h"

#include "b3host/call.h"
#include "b3host/host.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace b3native {

class BootBoost {
public:
    using TargetFn = int (*)();
    using ProfileFn = bool (*)(int slot);   // the save holds profile `slot` (0..2)
    static constexpr b3host::gaddr kFlowEvent = 0x0030E930u, kTitleUpdate = 0x00438840u,
                                   kLoadProfile = 0x00421000u;
    static constexpr b3host::gaddr kPageState = 0x01E90424u, kClock = 0x004F50A0u,
                                   kMemCard = 0x01ECD520u, kBootFlag = 0x0051A998u;

    bool install(HookTable& hooks, TargetFn target, ProfileFn exists) {
        exists_ = exists;
        for (b3host::gaddr a : {kFlowEvent, kTitleUpdate})
            if (!hooks.has_row(a)) {
                std::fprintf(stderr, "boot_boost: %08X is not native: the game's start\n", a);
                return false;
            }
        target_fn_ = target;
        forced_ = forced();
        const char* l = std::getenv("B3NATIVE_BOOT_LOG");
        log_ = l && l[0] == '1';
        std::string error;
        std::vector<Hook> specs = {{kFlowEvent, HookLayer::kPort, &flow, &next_flow_},
                                   {kTitleUpdate, HookLayer::kPort, &title, &next_title_}};
        if (!hooks.install("port:boot_boost", specs, &error)) {
            std::fprintf(stderr, "boot_boost: %s\n", error.c_str());
            return false;
        }
        instance() = this;
        return true;
    }
    static int forced() {
        const char* e = std::getenv("B3NATIVE_BOOT_TO");
        return e && e[0] >= '0' && e[0] <= '4' && !e[1] ? e[0] - '0' : -1;
    }
    // native_play, each VBlank before player 1's report goes to the game (not while a replay drives the pad): Start held
    // for the title (START AT main menu)
    bool press_start() {
        const bool p = rule_.press();
        if (p && !pressing_) { std::printf("boot_boost: START AT main menu: Start pressed on the title\n"); std::fflush(stdout); }
        pressing_ = p;
        return p;
    }
    // native_play, each VBlank: end the film playing now (the intro's, START AT not the game's start)
    bool skip_films() { return rule_.skip_films(target()); }
    int target() {
        if (target_ < 0) target_ = BootBoostRule::clamp(forced_ >= 0 ? forced_ : target_fn_ ? target_fn_() : 0);
        return target_;
    }

private:
    static BootBoost*& instance() { static BootBoost* p = nullptr; return p; }
    static void title(const b3host::CallArgs& c, b3host::CallResult& r) {
        BootBoost& m = *instance();
        m.rule_.title_up(m.target());
        if (m.rule_.start_pending()) {
            float now, since;
            const std::uint32_t wn = b3host::load<std::uint32_t>(kClock), ws = b3host::load<std::uint32_t>(c.gpr[0].w[0] + 0x14u);
            std::memcpy(&now, &wn, 4);
            std::memcpy(&since, &ws, 4);
            m.rule_.title_ready(int(b3host::load<std::uint32_t>(kPageState)), now, since);
        }
        m.next_title_(c, r);
    }
    static void flow(const b3host::CallArgs& c, b3host::CallResult& r) {
        BootBoost& m = *instance();
        const int event = int(c.gpr[1].w[0]);
        const bool flag = event == 3 && b3host::load<std::uint8_t>(kBootFlag) != 0;
        if (m.log_ && event == 3) std::printf("boot_boost: flow event 3, boot flag %d\n", flag ? 1 : 0);
        if (m.rule_.autoload(m.target(), event, flag)) {
            const int slot = BootBoostRule::slot(m.target());
            if (m.exists_ && !m.exists_(slot)) {
                std::printf("boot_boost: START AT main menu: the save has no profile %d: the game's profile choice\n", slot + 1);
                std::fflush(stdout);
                m.next_flow_(c, r);
                return;
            }
            std::printf("boot_boost: START AT main menu: profile %d loaded (func_00421000, as LOAD PROFILE does)\n", slot + 1);
            std::fflush(stdout);
            b3host::call_guest<void>(kLoadProfile, kMemCard, slot);
            b3host::store<std::uint8_t>(kBootFlag, 0);
        }
        m.next_flow_(c, r);
    }

    HookEntry next_flow_ = nullptr, next_title_ = nullptr;
    TargetFn target_fn_ = nullptr;
    ProfileFn exists_ = nullptr;
    int forced_ = -1, target_ = -1;
    bool log_ = false, pressing_ = false;
    BootBoostRule rule_;
};

}  // namespace b3native
#endif
#endif
