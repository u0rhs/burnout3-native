// takedown_pitch.h -- hudmods (2026-11-21): TAKEDOWN STREAK PITCH (PC SETTINGS > AUDIO > SOUND; ini `takedown_pitch`,
// default off). Outside World Tour the takedown sound climbs in pitch once the player's takedowns since the last crash
// pass 10, the way World Tour's takedown-target events climb past their target. Sound only: the game's own sounds at
// the game's own pitches, chosen by the streak; the takedown count, the HUD and the score are the game's.
//
// What the game does [C decomp src/nonmatching/func_00285C00.cpp, retail asm 0x285C00..0x286944]:
// - func_00285C00 (self = the HUD sound manager D_01E7A800, p0 the popup's class, p1 its id, p2 a level) plays the HUD
//   popups' sounds. It is reached from CB3HUDPrimaryMessage::UpdateMessage (0x3D8280; each player's message, its +0x24
//   that player's CB3Score) and CB3HUDScore::UpdateSmallMessage (id 0 only) through dispatch (b3host::guest_fn), so a
//   port hook sees every call. Each player's takedown count is their CB3Score's +0x68 (player 1's: D_0064E4D8).
// - Class 4 is a takedown. Its id gives a pitch step n: 0x93 -> 0, 0x9F / 0xA3 -> 1, 0xA0 / 0xA4 -> 2, 0xA1 / 0xA5 -> 3,
//   0xA2 / 0xA6 -> 4 (the doubles, triples ... of a chain), played as the takedown sound 0xBCB1BCEECD400000 twice (full
//   and half volume) at pitch 1, 2^(2/12), 2^(3/12), 2^(4/12), 2^(6/12) (D_004E092C..0x938). Ids 0xAA..0xAE play the
//   bigger takedown sound 0xBCB1BD0D08820000 (func_00285770) at step id - 0xAA.
// - In World Tour (CB3Game::IsInChampionship 0x131CE0: the mode object D_0051BA88 non-null and its +0x1B8 = gGame +
//   0x2D8E8 = 0x0051B928) on an event whose +0x1B4 is 0x0051B340 (the takedown-target events) it compares the takedown
//   count D_0064E4D8 with the event's target (*D_0064E4F0 +8): d = count - target; 1..4 -> step max(n, d); 5 and more
//   -> the bigger sound at step min(d - 5, 4). Elsewhere: step n.
// - Class 3 ids 0x7F / 0x80 are the player's crash popups.
//
// The rule here (TakedownStreak, below), per player (split screen: the popup's player is the calling message's score, so
// player 2's crash leaves player 1's streak): the target is 10 more than that player's count at their last crash (0 at
// the race's start: a count lower than the last crash's is a new race). When a takedown's d is above 0, the call reaches the
// game with the id that makes the game play what World Tour would: step max(n, d) (id 0x93 / 0xA2 + step) for d 1..4,
// the bigger sound at step min(d - 5, 4) (id 0xAA + step) from 5. Every other call, World Tour, and the setting off:
// the call reaches the game unchanged. B3NATIVE_TAKEDOWN_PITCH=0 / 1 overrides the setting (tests);
// B3NATIVE_TAKEDOWN_PITCH_LOG=1 logs every takedown and crash the rule sees (with the setting on it logs the steps);
// B3NATIVE_TAKEDOWN_PITCH_TARGET=<0..99> sets another target (tests: a scripted race rarely reaches 11 takedowns).
#ifndef B3NATIVE_TAKEDOWN_PITCH_H
#define B3NATIVE_TAKEDOWN_PITCH_H

#include <algorithm>
#include <cstdint>

namespace b3native {

// The rule alone (no guest memory: tests/test_takedown_pitch.cpp).
class TakedownStreak {
public:
    static constexpr int kTarget = 10;   // takedowns after the last crash before the pitch climbs
    explicit TakedownStreak(int target = kTarget) : target_(target) {}
    static constexpr int kTakedown = 4, kCrash = 3;   // func_00285C00's classes
    // The game's pitch step for a takedown id (-1: not one of the takedown ids)
    static int step_of(int id) {
        switch (id) {
        case 0x93: return 0;
        case 0x9F: case 0xA3: return 1;
        case 0xA0: case 0xA4: return 2;
        case 0xA1: case 0xA5: return 3;
        case 0xA2: case 0xA6: return 4;
        default: return -1;
        }
    }
    struct Result {
        int id = -1;          // the id the call should carry (== the call's id when unchanged)
        int streak = 0;       // takedowns since the last crash
        int over = 0;         // streak - the target
        int game_step = -1;   // the game's own step (-1: not a takedown)
        int step = -1;        // the step played (0..4), -1 none
        bool big = false;     // the bigger takedown sound (func_00285770's)
        bool crash = false;   // a crash popup: the streak starts again
    };
    // One func_00285C00 call: its class and id, the takedown count now, and whether World Tour's own rule applies (the
    // game then keeps its own); `on` the setting (off: the streak is still followed, the id never changed).
    Result step(int cls, int id, int count, bool world_tour, bool on) {
        Result r;
        r.id = id;
        if (count < base_) base_ = 0;   // a new race (the count starts again at 0)
        if (cls == kCrash && (id == 0x7F || id == 0x80)) {
            base_ = count;
            r.crash = true;
            return r;
        }
        r.streak = count - base_;
        r.over = r.streak - target_;
        if (cls != kTakedown) return r;
        r.game_step = step_of(id);
        if (r.game_step < 0) return r;
        r.step = r.game_step;
        if (!on || world_tour || r.over <= 0) return r;
        if (r.over < 5) {
            r.step = std::max(r.game_step, r.over);
            r.id = r.step == 0 ? 0x93 : 0xA2 + r.step;
        } else {
            r.step = std::min(r.over - 5, 4);
            r.big = true;
            r.id = 0xAA + r.step;
        }
        return r;
    }
    int base() const { return base_; }
    int target() const { return target_; }

private:
    int target_ = kTarget;
    int base_ = 0;   // the count at the last crash
};

}  // namespace b3native

#ifndef B3NATIVE_TAKEDOWN_RULE_ONLY
#include "hook_table.h"

#include "b3host/host.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace b3native {

class TakedownPitch {
public:
    using OnFn = int (*)();                  // the setting: 1 on -- read at every call
    using VblankFn = std::uint64_t (*)();    // the VBlank count, for the log
    static constexpr b3host::gaddr kPopupSound = 0x00285C00u;   // func_00285C00
    // CB3HUDPrimaryMessage::UpdateMessage 0x3D8280: the popups' one caller with a takedown or crash id (CB3HUDScore's
    // UpdateSmallMessage passes id 0); its +0x24 is its player's CB3Score, whose +0x68 is that player's takedown count
    // (player 1's: D_0064E4D8 = race car 0x0064D3B0 + 0x10C0 + 0x68) [C the generated units]
    static constexpr b3host::gaddr kPrimaryMessage = 0x003D8280u;
    static constexpr std::uint32_t kMessageScore = 0x24u, kScoreTakedowns = 0x68u;
    static constexpr b3host::gaddr kMode = 0x0051BA88u, kChampionship = 0x0051B928u, kTargetEvent = 0x0051B340u;

    static int forced() {   // B3NATIVE_TAKEDOWN_PITCH=0 / 1 (-1: the setting)
        const char* e = std::getenv("B3NATIVE_TAKEDOWN_PITCH");
        return e && (e[0] == '0' || e[0] == '1') && !e[1] ? e[0] - '0' : -1;
    }
    bool install(HookTable& hooks, OnFn on, VblankFn vblank) {
        if (!hooks.has_row(kPopupSound) || !hooks.has_row(kPrimaryMessage)) {
            std::fprintf(stderr, "takedown_pitch: %08X is not native: the game's takedown sounds\n", kPopupSound);
            return false;
        }
        on_ = on;
        vblank_ = vblank;
        const char* l = std::getenv("B3NATIVE_TAKEDOWN_PITCH_LOG");
        log_ = l && l[0] == '1';
        if (const char* t = std::getenv("B3NATIVE_TAKEDOWN_PITCH_TARGET"); t && *t) {
            target_ = std::clamp(std::atoi(t), 0, 99);
            std::printf("takedown_pitch: the target is %d takedowns (B3NATIVE_TAKEDOWN_PITCH_TARGET)\n", target_);
        }
        std::string error;
        std::vector<Hook> specs = {{kPopupSound, HookLayer::kPort, &entry, &next_},
                                   {kPrimaryMessage, HookLayer::kPort, &message, &next_message_}};
        if (!hooks.install("port:takedown_pitch", specs, &error)) {
            std::fprintf(stderr, "takedown_pitch: %s\n", error.c_str());
            return false;
        }
        instance() = this;
        return true;
    }

private:
    static TakedownPitch*& instance() { static TakedownPitch* p = nullptr; return p; }
    // CB3Game::IsInChampionship 0x131CE0 by memory reads: World Tour keeps the game's own rule
    static bool in_world_tour() {
        const std::uint32_t mode = b3host::load<std::uint32_t>(kMode);
        if (mode == 0 || !b3host::host_pointer(mode + 0x1B4u, 8)) return false;
        return b3host::load<std::uint32_t>(mode + 0x1B8u) == kChampionship;   // in World Tour: the game's rule or none
    }
    // the popup's player: its message's score around the call (0: another caller)
    static void message(const b3host::CallArgs& c, b3host::CallResult& r) {
        TakedownPitch& m = *instance();
        const b3host::gaddr was = m.score_;
        m.score_ = b3host::load<std::uint32_t>(c.gpr[0].w[0] + kMessageScore);
        m.next_message_(c, r);
        m.score_ = was;
    }
    // the streak of that player (each player's own: a crash of player 2 leaves player 1's streak)
    TakedownStreak* rule_for(b3host::gaddr score) {
        if (score == 0) return nullptr;
        for (Slot& sl : slots_) if (sl.score == score) return &sl.rule;
        for (Slot& sl : slots_) if (sl.score == 0) { sl.score = score; sl.rule = TakedownStreak(target_); return &sl.rule; }
        return nullptr;
    }
    static void entry(const b3host::CallArgs& c, b3host::CallResult& r) {
        TakedownPitch& m = *instance();
        const int cls = int(c.gpr[1].w[0]), id = int(c.gpr[2].w[0]);
        if (cls != TakedownStreak::kTakedown && cls != TakedownStreak::kCrash) { m.next_(c, r); return; }
        TakedownStreak* rule = m.rule_for(m.score_);
        if (!rule) { m.next_(c, r); return; }   // (not a player's popup: the game's)
        const int f = forced();
        const bool on = f >= 0 ? f == 1 : m.on_ && m.on_() == 1;
        const TakedownStreak::Result s = rule->step(cls, id, b3host::load<std::int32_t>(m.score_ + kScoreTakedowns), in_world_tour(), on);
        if (m.log_ && (s.crash || s.game_step >= 0)) {
            const unsigned long long v = m.vblank_ ? static_cast<unsigned long long>(m.vblank_()) : 0ull;
            const int player = m.score_ == kPlayer1Score ? 1 : 2;
            if (s.crash)
                std::printf("takedown_pitch: VBlank %llu: player %d crash: the streak starts again at %d takedowns\n", v, player, rule->base());
            else
                std::printf("takedown_pitch: VBlank %llu: player %d takedown, streak %d (%+d over %d): the game's step %d, played %s step %d%s\n", v,
                            player, s.streak, s.over, rule->target(), s.game_step, s.big ? "the bigger sound at" : "the takedown sound at",
                            s.step, s.id != id ? "" : " (the game's)");
            std::fflush(stdout);
        }
        if (s.id == id) { m.next_(c, r); return; }
        b3host::CallArgs a = c;
        a.gpr[2].w[0] = std::uint32_t(s.id);
        a.gpr[2].w[1] = 0;
        m.next_(a, r);
    }

    static constexpr b3host::gaddr kPlayer1Score = 0x0064E470u;   // (the log's player number only)
    struct Slot { b3host::gaddr score = 0; TakedownStreak rule; };
    HookEntry next_ = nullptr, next_message_ = nullptr;
    OnFn on_ = nullptr;
    VblankFn vblank_ = nullptr;
    bool log_ = false;
    int target_ = TakedownStreak::kTarget;
    b3host::gaddr score_ = 0;
    Slot slots_[4];
};

}  // namespace b3native
#endif
#endif
