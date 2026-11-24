// game_limits.h -- gamemods1 (2026-11-21): two of the game's limits as opt-in rows (PC SETTINGS > GAME > GAMEPLAY):
//
// IDLE PAUSE (ini `idle_pause`, default on = the game's). CB3OfflineStageLogic::Update 0x395490 [C decomp] reads
// GetDeadInputTime__15CB3InputManagerFSc 0x22BAE0 for pads 0 and 1; when both have been idle more than 300 s (and no
// pause, no menu page is up) it opens the pause menu (D_0051BA98 = 0). Off: inside that Update only, the dead input time
// reads 0, so the race never pauses itself; every other reader (the title's attract timeout, the menus) is the game's.
// The race is unchanged otherwise (nothing but the pause it would have opened).
//
// END VERY LONG EVENTS (ini `long_event_end`, default on = the game's). Two places end an event that has run too long
// [C decomp]:
// - func_0019A550 (the world's update, called by func_00132600): at its end, unless the world is a replay (+0x1437F3),
//   once the world time +0x20 passes 1800 s (2400 s with D_01E65499 set) every race car not yet finished is finished
//   (CB3RaceCar::SetFinished, state 2, its place 6 - the cars still running, its time the world's).
// - func_0038B160: outside World Tour (CB3Game::IsInChampionship), once func_00199750 (1800 s - the world time) is under
//   300 s, every car not finished is finished (state 1).
// Off: func_0038B160 does nothing, and func_0019A550 runs with the replay flag set for that call only (its end check is
//   the flag's one reader in the update; the flag is put back) once the time is past the limit. The event then goes on
//   until it ends the normal way.
//   Only in Single Event and the two-player game (game_option_rules.h game_options_stage, as GAMEPLAY's other options):
//   World Tour, whose results count, and online keep the game's limits.
//   Gameplay: an event kept going past the game's limit keeps no game records (records_guard.h on CB3Records::Update
//   0x3D1090) and does not count for the profile (0x136F30, its totals and unlocks: answered no), from the moment a limit
//   was passed until the next race starts. Below the limits nothing changes.
//
// Both: hooks on the port layer; with the rows on (the defaults) every call goes through unchanged. The settings are
// read at each call (live); replays record them (native_play: `idle_pause`, `long_event_end` when off).
// B3NATIVE_LIMITS_LOG=1 logs what the hooks change.
#ifndef B3NATIVE_GAME_LIMITS_H
#define B3NATIVE_GAME_LIMITS_H

#include <cstdint>

namespace b3native {

// The pure rules (tests/test_game_limits.cpp)
struct GameLimitsRule {
    static constexpr float kIdleSeconds = 300.0f, kLongSeconds = 1800.0f, kLongerSeconds = 2400.0f, kNearEnd = 300.0f;
    static float limit(bool longer) { return longer ? kLongerSeconds : kLongSeconds; }
    // func_0019A550's end check would finish the cars now
    static bool world_end_due(float world_time, bool longer, bool replay) { return !replay && world_time > limit(longer); }
    // func_0038B160 would finish the cars now
    static bool near_end_due(float world_time, bool world_tour) { return !world_tour && (kLongSeconds - world_time) < kNearEnd; }
};

}  // namespace b3native

#ifndef B3NATIVE_LIMITS_RULE_ONLY
#include "game_option_rules.h"
#include "hook_table.h"
#include "records_guard.h"

#include "b3host/call.h"
#include "b3host/host.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace b3native {

class GameLimits {
public:
    using OnFn = int (*)();   // 1: the game's limit is on (the default), 0: off
    static constexpr b3host::gaddr kStageUpdate = 0x00395490u, kDeadInputTime = 0x0022BAE0u, kWorldUpdate = 0x0019A550u,
                                   kNearEnd = 0x0038B160u, kProfileCounts = 0x00136F30u;
    static constexpr b3host::gaddr kWorld = 0x00522660u, kLonger = 0x01E65499u, kMode = 0x0051BA88u, kChampionship = 0x0051B928u;
    static constexpr std::uint32_t kWorldTime = 0x20u, kReplayFlag = 0x1437F3u;

    bool install(HookTable& hooks, OnFn idle_pause, OnFn long_end) {
        for (b3host::gaddr a : {kStageUpdate, kDeadInputTime, kWorldUpdate, kNearEnd, kRecordsUpdate, kProfileCounts})
            if (!hooks.has_row(a)) {
                std::fprintf(stderr, "game_limits: %08X is not native: the game's limits\n", a);
                return false;
            }
        idle_on_ = idle_pause;
        long_on_ = long_end;
        const char* l = std::getenv("B3NATIVE_LIMITS_LOG");
        log_ = l && l[0] == '1';
        std::string error;
        std::vector<Hook> specs = {{kStageUpdate, HookLayer::kPort, &stage_update, &next_stage_},
                                   {kDeadInputTime, HookLayer::kPort, &dead_input, &next_dead_},
                                   {kWorldUpdate, HookLayer::kPort, &world_update, &next_world_},
                                   {kNearEnd, HookLayer::kPort, &near_end, &next_near_},
                                   {kRecordsUpdate, HookLayer::kPort, &records, &next_records_},
                                   {kProfileCounts, HookLayer::kPort, &profile_counts, &next_profile_}};
        if (!hooks.install("port:game_limits", specs, &error)) {
            std::fprintf(stderr, "game_limits: %s\n", error.c_str());
            return false;
        }
        instance() = this;
        return true;
    }
    std::uint64_t idle_skipped() const { return idle_skipped_; }
    std::uint64_t ends_held() const { return ends_held_; }

private:
    static GameLimits*& instance() { static GameLimits* p = nullptr; return p; }
    static float f32(b3host::gaddr a) {
        const std::uint32_t w = b3host::load<std::uint32_t>(a);
        float f;
        std::memcpy(&f, &w, 4);
        return f;
    }
    bool idle_off() const { return idle_on_ && idle_on_() == 0; }
    // END VERY LONG EVENTS Off, in an offline Single Event / two-player stage only (never World Tour or online)
    bool long_off() const {
        if (!(long_on_ && long_on_() == 0)) return false;
        const std::uint32_t cur = b3host::load<std::uint32_t>(kGoCurrentMode) & 0x01FFFFFFu;
        return cur != 0 && game_options_stage(cur, b3host::load<std::uint32_t>(cur + 0x1B8u) & 0x01FFFFFFu);
    }

    static void stage_update(const b3host::CallArgs& c, b3host::CallResult& r) {
        GameLimits& m = *instance();
        if (!m.idle_off() && !m.log_) { m.next_stage_(c, r); return; }
        m.in_stage_ = true;   // (with the row on only to log: the answers are the game's)
        m.next_stage_(c, r);
        m.in_stage_ = false;
        if (m.log_ && m.idle_seen_ && m.idle_watch_ < 600) {   // B3NATIVE_LIMITS_LOG: what the game does with its request
            const std::int32_t paused = b3host::load<std::int32_t>(0x004EE040u + 0x2DA54u), req = b3host::load<std::int32_t>(0x004EE040u + 0x2DA58u);
            const int gate = b3host::load<std::uint8_t>(0x004EB1E0u);
            if (m.idle_watch_++ % 60 == 0 || paused != m.last_paused_)
                std::printf("game_limits: after the idle check: pause request %d, paused player %d, D_004EB1E0 %d; player 2 idle %.0f s, "
                            "D_00665E53 %d, menu page %08x\n", req, paused, gate, double(m.dead1_), b3host::load<std::uint8_t>(0x00665E53u),
                            b3host::load<std::uint32_t>(0x01E90430u));
            m.last_paused_ = paused;
        }
    }
    static void dead_input(const b3host::CallArgs& c, b3host::CallResult& r) {
        GameLimits& m = *instance();
        if (!m.in_stage_) { m.next_dead_(c, r); return; }
        m.next_dead_(c, r);   // (the game's own call: the same work, only its answer is changed)
        float t;
        std::memcpy(&t, &r.f0, 4);
        if (c.gpr[1].w[0] == 1) m.dead1_ = t;
        if (m.log_ && c.gpr[1].w[0] == 1 && m.calls_++ % 1200 == 0)   // the two players' idle times through the race
            std::printf("game_limits: idle times: player 1 %.0f s, player 2 %.0f s\n", double(m.dead0_), double(t));
        if (c.gpr[1].w[0] == 0) m.dead0_ = t;
        if (!m.idle_off()) {   // B3NATIVE_LIMITS_LOG=1 with the row on: when the game's own test passes
            if (t > GameLimitsRule::kIdleSeconds && c.gpr[1].w[0] == 0 && !m.idle_seen_) {
                m.idle_seen_ = true;
                std::printf("game_limits: player 1 idle %.0f s: the first half of the game's idle check passes (IDLE PAUSE on; the "
                            "pause once player 2's idle time passes it too)\n", double(t));
                std::fflush(stdout);
            }
            return;
        }
        if (t > GameLimitsRule::kIdleSeconds && c.gpr[1].w[0] == 0) {
            // the game asks player 2's only after player 1's passes (an empty port 2 keeps the stamp of the menus' last
            // reset, from before the race clock restarted): the same query, to tell when the game's pause would open
            b3host::CallArgs c1 = c;
            c1.gpr[1].w[0] = 1;
            b3host::CallResult r1;
            m.next_dead_(c1, r1);
            float t1;
            std::memcpy(&t1, &r1.f0, 4);
            if (t1 > GameLimitsRule::kIdleSeconds && m.idle_skipped_++ == 0) {
                std::printf("game_limits: IDLE PAUSE off: both players idle past %.0f s (player 1 %.0f s, player 2 %.0f s): the "
                            "game's idle pause not opened\n", double(GameLimitsRule::kIdleSeconds), double(t), double(t1));
                std::fflush(stdout);
            }
        }
        r.f0 = 0;   // 0.0f: never idle inside the stage logic's check
    }
    static void world_update(const b3host::CallArgs& c, b3host::CallResult& r) {
        GameLimits& m = *instance();
        const b3host::gaddr w = c.gpr[0].w[0];
        const float t = f32(w + kWorldTime);
        if (t < 60.0f) m.held_ = false;   // a new race
        if (!m.long_off()) { m.next_world_(c, r); return; }
        const bool replay = b3host::load<std::uint8_t>(w + kReplayFlag) != 0;
        if (!GameLimitsRule::world_end_due(t, b3host::load<std::uint8_t>(kLonger) != 0, replay)) { m.next_world_(c, r); return; }
        m.hold(t);
        b3host::store<std::uint8_t>(w + kReplayFlag, 1);   // the end check's only reader in this update
        m.next_world_(c, r);
        if (b3host::load<std::uint8_t>(w + kReplayFlag) == 1) b3host::store<std::uint8_t>(w + kReplayFlag, 0);
    }
    static void near_end(const b3host::CallArgs& c, b3host::CallResult& r) {
        GameLimits& m = *instance();
        if (!m.long_off()) { m.next_near_(c, r); return; }
        const b3host::gaddr mode = b3host::load<std::uint32_t>(kMode);
        const bool wt = mode != 0 && b3host::host_pointer(mode + 0x1B8u, 4) && b3host::load<std::uint32_t>(mode + 0x1B8u) == kChampionship;
        const float t = f32(kWorld + kWorldTime);
        if (GameLimitsRule::near_end_due(t, wt)) m.hold(t);
        // nothing else: the event is not ended
    }
    static void records(const b3host::CallArgs& c, b3host::CallResult& r) {
        GameLimits& m = *instance();
        if (!m.held_) { m.next_records_(c, r); return; }
        records_without_kinds(m.next_records_, c, r);
        std::printf("game_limits: END VERY LONG EVENTS off: this event ran past the game's limit, its records are not kept\n");
        std::fflush(stdout);
    }
    static void profile_counts(const b3host::CallArgs& c, b3host::CallResult& r) {   // 0x136F30: the race counts for the profile
        GameLimits& m = *instance();
        m.next_profile_(c, r);
        if (!m.held_ || (r.v0.w[0] & 0xFFu) == 0) return;
        r.v0 = u_long128{};
        std::printf("game_limits: END VERY LONG EVENTS off: this event ran past the game's limit, it does not count for the profile\n");
        std::fflush(stdout);
    }
    void hold(float t) {
        if (!held_) {
            std::printf("game_limits: END VERY LONG EVENTS off: the event goes on past the game's limit (%.0f s)\n", double(t));
            std::fflush(stdout);
        }
        held_ = true;
        ++ends_held_;
    }

    HookEntry next_stage_ = nullptr, next_dead_ = nullptr, next_world_ = nullptr, next_near_ = nullptr, next_records_ = nullptr, next_profile_ = nullptr;
    OnFn idle_on_ = nullptr, long_on_ = nullptr;
    bool in_stage_ = false, held_ = false, log_ = false, idle_seen_ = false;
    int idle_watch_ = 0;
    float dead1_ = 0, dead0_ = 0;
    std::uint64_t calls_ = 0;
    std::int32_t last_paused_ = -2;
    std::uint64_t idle_skipped_ = 0, ends_held_ = 0;
};

}  // namespace b3native
#endif

#endif
