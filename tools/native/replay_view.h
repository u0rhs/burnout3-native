// replay_view.h -- replayview (2026-11-22): RACE REPLAY, the game's own replay of a finished race in every offline mode
// (remaster-features §31). Game side: the replay's recording kept whole on the host, the game's replay asked for, and its
// replay page shown; the cameras are the renderer's (src/b3replay/replaycam.h).
//
// What the game has [C the decomp]:
// - CB3ReplaySystem (gWorld + 0x3C888 = 0x0055EEE8) records the human players' driving controls: CB3PlayerCarPhysics's
//   GetDrivingControls 0x1BD020 / GetCrashingDrivingControls call NextFrame 0x155020(system, controls (car physics
//   +0x13FC, 32 bytes), crash). Recording (+0x84 0), NextFrame packs the controls into a 3-byte frame (8 in a crash:
//   with aftertouch and the camera) in its byte stream (+0x60, memory block 8: 16 KB), then gives the controls back as
//   that frame decodes, so the live race runs on them; once the stream is full it returns 0 and leaves the controls as
//   they are (the raw values). Playing (+0x84 1), it reads the frames back. Prepare 0x1551B0(system, play) rewinds the
//   stream (and, recording, resets its counts). AI cars and traffic are not recorded: the replay re-simulates them.
// - The replay re-runs the race: the stage's Update (CB3OnePlayerStage 0x135180, the two-player stage's 0x13CBC0) asks
//   the logic (vt+0x28; a race logic answers its +4) and sets the game state D_0051BACC 12; CB3Game::Update then has
//   the mode's StartReplay (the logic's StartReplay, D_00665E52 "replay requested") and prepares the world again (state
//   4), which copies it to D_00665E53 "in playback" and calls Prepare(system, 1). In retail only the Crash logics (after
//   the crash) and online ever ask; the race, Road Rage, Burning Lap and Eliminator logics have the same chain but
//   nothing sets their request.
// - When the world is ready again, func_0030EB10 picks the in-game menu's scene (D_01EA2970): in playback it enters the
//   replay page (0x944140403E74F2B0) only for a Crash event of game type 3 (CB3MenuChoices +0); otherwise it changes
//   nothing. A race's start: CB3OfflineStageLogic::Prepare 0x3956A0 sets the logic's +5, and the race logic's state 1
//   (its Update 0x389EE0) then runs the countdown (the countdown scene 0x94413E497BA7576B, the world timer, each car's
//   start in 4.5 s); StartReplay leaves the logic in state 1 without its +5, so the replay's race never starts.
// - The race's end (func_003960A0, from the logic's state 2 when every car is done) in playback does nothing: no
//   OnEndOfChallenge (World Tour's progress), no post-race setup 0x42F0F0 (the records, the profile), no results scene.
//   The results flow clears the request (D_00665E52) as it leads out; the world's reset (0x19B380) clears both flags.
//
// Ours (wrappers on the hook table's port layer, only with RACE REPLAY on: off, NextFrame and the rest are the game's):
// - NextFrame: recording, the game's own call, then the controls it leaves (its 3 / 8-byte frame decoded, or the raw
//   values once its 16 KB are full) and its result are copied to the host, per call: the live race is the game's, byte
//   for byte. Playing a replay we asked for, the host's copy gives the controls and the result back in the same order,
//   for any length (the game's own stream would stop after ~90 s).
// - Prepare: recording starts a new host copy; playing rewinds it.
// - request() (from the results page): the menu's scene then (the results page) noted, and, in the stage's next Update
//   (0x135180, before it), the logic's +4 set: the game's own replay chain follows.
// - func_0030EB10 in our replay's playback: the logic's +5 set, as its Prepare does for a race: the countdown runs and the
//   replay's race starts as the live one did (the controls then match the recording frame for frame).
// - leave(), or the replay's race over (the logic past its racing state) or its recording run out, 3 s after: in the
//   stage's Update (after it) the menu back on the noted page (the results, as they were: the post-race setup is not
//   run again), the request cleared; the playback flag stays until the world's reset, so the race's end stays as the
//   game's in playback (inert) and the HUD hidden.
#ifndef B3NATIVE_REPLAY_VIEW_H
#define B3NATIVE_REPLAY_VIEW_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "hook_table.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace b3native {

class ReplayView {
public:
    static constexpr b3host::gaddr kNextFrame = 0x00155020u, kPrepare = 0x001551B0u, kSceneOnReady = 0x0030EB10u,
                                   kStageUpdate = 0x00135180u,   // CB3OnePlayerStage::Update
                                   kPostRace = 0x0042F0F0u;      // the post-race setup (records, profile) at a race's end
    // The offline one-player race logics, gGame's own objects [C func_004D3E60, the gGame constructor]: the race (Single
    // Event and World Tour races), the Burning Lap (TIME ATTACK, World Tour's Burning Laps), the Eliminator, Road Rage.
    // The crash logics (the game's own replay), the 2P and the online ones are not offered (no guest call: a plain compare)
    static constexpr b3host::gaddr kRaceLogics[4] = {0x0051B258u, 0x0051B2A4u, 0x0051B2F0u, 0x0051B340u};
    static bool race_logic(b3host::gaddr l) { return l == kRaceLogics[0] || l == kRaceLogics[1] || l == kRaceLogics[2] || l == kRaceLogics[3]; }
    static constexpr b3host::gaddr kMenuFsm = 0x01EA2970u;       // the in-game menu's CGtFSM (+4 its state, +0x10 / +0x14 timing)
    static constexpr b3host::gaddr kSystem = 0x0055EEE8u;        // CB3ReplaySystem (gWorld + 0x3C888)
    static constexpr b3host::gaddr kRequested = 0x00665E52u, kPlaying = 0x00665E53u;   // the world's replay flags
    static constexpr b3host::gaddr kMenuChoices = 0x01E900B0u, kCurrentMode = 0x0051BA88u;
    static constexpr b3host::gaddr kOnePlayerStage = 0x0051AD00u;
    static constexpr b3host::gaddr kGameState = 0x0051BACCu;
    static constexpr b3host::gaddr kWorldHeld = 0x00665E50u, kWorld = 0x00522660u, kTimerStop = 0x001D3C10u,
                                   kTimerStart = 0x001D3C50u;   // CGtTimer::Stop / Start
    // the player's car (gWorld 0x00522660 + 0x12AD50) position, +0x30 (3 floats)
    static constexpr b3host::gaddr kPlayerCarPos = 0x00522660u + 0x12AD50u + 0x30u;
    static constexpr unsigned kControlsBytes = 32;

    bool install(HookTable& hooks) {
        const std::vector<Hook> set = {{kNextFrame, HookLayer::kPort, &next_frame, &next_frame_},
                                       {kPrepare, HookLayer::kPort, &prepare, &prepare_},
                                       {kSceneOnReady, HookLayer::kPort, &scene_on_ready, &scene_on_ready_},
                                       {kStageUpdate, HookLayer::kPort, &stage_update, &stage_update_},
                                       {kPostRace, HookLayer::kPort, &post_race, &post_race_}};
        for (const Hook& h : set)
            if (!hooks.has_row(h.address)) {
                std::fprintf(stderr, "replay_view: %08X is not native: RACE REPLAY off\n", h.address);
                return false;
            }
        std::string error;
        if (!hooks.install("port:replay_view", set, &error)) {
            std::fprintf(stderr, "replay_view: %s\n", error.c_str());
            return false;
        }
        instance() = this;
        return true;
    }
    // The race on now's logic (the one-player stage's +0x1B4; its Update is the one hooked), 0: none (split screen:
    // not offered)
    static b3host::gaddr logic_now() {
        const b3host::gaddr cur = b3host::load<std::uint32_t>(kCurrentMode) & 0x01FFFFFFu;
        if (cur != kOnePlayerStage) return 0;
        return b3host::load<std::uint32_t>(cur + 0x1B4u) & 0x01FFFFFFu;
    }
    static bool playing() { return b3host::load<std::uint8_t>(kPlaying) != 0; }
    // Ask for the replay of the race on now (VBlank side; done in the stage's next Update); false when there is no
    // recording or a replay is on
    bool request(bool any_time = false) {   // (any_time: checks only, a replay asked for mid-race)
        if (!(available() || (any_time && logic_now() && !frames_.empty() && !playing() && !pending_request_))) return false;
        pending_request_ = true;
        return true;
    }
    // Leave our replay (VBlank side; done in the stage's next Update)
    void leave() { if (ours_ && playing() && !left_) pending_leave_ = true; }
    // A replay can be asked for now: a race (not Crash mode: it has its own) ended since its recording began, its
    // stage still the game's current mode, a recording held, no replay on
    bool available() const {
        return ended_ && race_logic(logic_now()) && spoiled_.empty() && !frames_.empty() && logic_now() && (!playing() || (ours_ && left_)) &&
               !pending_request_ && settled() && chosen_on_ != (b3host::load<std::uint32_t>(kMenuFsm + 4u) & 0x01FFFFFFu);
    }
    // This race would not replay as it went (party1's buttons: not in the game's recording; START Standing: the game's
    // replay has no held start): no replay of it; `why` is logged once
    void spoil(const char* why) {
        if (playing() || !spoiled_.empty() || frames_.empty()) return;
        spoiled_ = why;
        std::printf("replay_view: no replay of this race: %s\n", why);
        std::fflush(stdout);
    }
    // Our replay plays now (the results page not yet back)
    bool showing() const { return ours_ && started_ && playing() && !left_; }
    bool ours() const { return ours_; }
    std::size_t recorded() const { return frames_.size(); }
    std::size_t played() const { return at_; }

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    struct Frame {
        std::array<std::uint8_t, kControlsBytes> controls;
        std::uint32_t result;
    };
    static ReplayView*& instance() { static ReplayView* p = nullptr; return p; }
    static b3host::gaddr a(const b3host::CallArgs& c, int i) { return c.gpr[i].w[0] & 0x01FFFFFFu; }

    static void next_frame(const b3host::CallArgs& c, b3host::CallResult& r) {
        ReplayView* p = instance();
        const b3host::gaddr sys = a(c, 0), ctl = a(c, 1);
        const bool play = b3host::load<std::uint8_t>(sys + 0x84u) != 0;
        if (!play) {   // recording: the game's own call, its outcome copied
            p->next_frame_(c, r);
            if (sys == kSystem) {
                Frame f;
                for (unsigned i = 0; i < kControlsBytes; ++i) f.controls[i] = b3host::load<std::uint8_t>(ctl + i);
                f.result = r.v0.w[0];
                p->frames_.push_back(f);
                for (unsigned i = 0; i < 3; ++i) p->last_pos_[i] = b3host::load<std::uint32_t>(kPlayerCarPos + 4u * i);
                race_cars(p->last_cars_);
            }
            return;
        }
        if (!p->ours_ || sys != kSystem) { p->next_frame_(c, r); return; }
        // our replay: the host's copy, in order (past its end: the game's own neutral, all 0, and "no frame")
        b3host::store<std::int32_t>(sys + 0x7Cu, b3host::load<std::int32_t>(sys + 0x7Cu) + 1);   // (frames played, as the game)
        r.v0 = u_long128{};
        if (p->at_ < p->frames_.size()) {
            const Frame& f = p->frames_[p->at_++];
            for (unsigned i = 0; i < kControlsBytes; ++i) b3host::store<std::uint8_t>(ctl + i, f.controls[i]);
            r.v0.w[0] = f.result;
            if (p->at_ == p->frames_.size()) {   // the self-check: the player's car where the race had it at its last frame
                std::uint32_t q[3];
                for (unsigned i = 0; i < 3; ++i) q[i] = b3host::load<std::uint32_t>(kPlayerCarPos + 4u * i);
                std::vector<std::uint32_t> cars;
                race_cars(cars);
                int cars_off = 0;   // the race cars (AI too) whose position is not the race's
                for (std::size_t k = 0; k < cars.size(); k += 3)
                    if (k + 2 >= p->last_cars_.size() || cars[k] != p->last_cars_[k] || cars[k + 1] != p->last_cars_[k + 1] ||
                        cars[k + 2] != p->last_cars_[k + 2])
                        ++cars_off;
                if (cars.size() != p->last_cars_.size()) ++cars_off;
                const bool same = q[0] == p->last_pos_[0] && q[1] == p->last_pos_[1] && q[2] == p->last_pos_[2] && cars_off == 0;
                float a[3], b[3];
                std::memcpy(a, q, 12);
                std::memcpy(b, p->last_pos_, 12);
                std::printf("replay_view: the replay's last frame (%zu): the player's car at %.3f %.3f %.3f, the race's %.3f %.3f %.3f; "
                            "%zu race cars, %d not where the race had them: %s\n",
                            p->at_, a[0], a[1], a[2], b[0], b[1], b[2], cars.size() / 3, cars_off, same ? "the same" : "DIFFERENT");
                std::fflush(stdout);
            }
        } else {
            for (unsigned i = 0; i < kControlsBytes; ++i) b3host::store<std::uint8_t>(ctl + i, 0);
            if (!p->told_end_) {
                p->told_end_ = true;
                std::printf("replay_view: the replay is past its recording (%zu frames): neutral controls\n", p->frames_.size());
                std::fflush(stdout);
            }
        }
    }
    static void prepare(const b3host::CallArgs& c, b3host::CallResult& r) {
        ReplayView* p = instance();
        if (a(c, 0) == kSystem) p->release_world("a world prepared");
        p->prepare_(c, r);
        if (a(c, 0) != kSystem) return;
        const bool play = (c.gpr[1].w[0] & 0xFFu) != 0;
        if (play) {
            p->at_ = 0;
            p->told_end_ = false;
            if (p->ours_) {
                p->started_ = true;
                std::printf("replay_view: the replay starts (%zu frames recorded)\n", p->frames_.size());
                std::fflush(stdout);
            }
        } else {
            if (p->ours_) {
                std::printf("replay_view: a new race after the replay: recording (%zu frames of the last one dropped)\n", p->frames_.size());
                std::fflush(stdout);
            }
            p->frames_.clear();
            p->at_ = 0;
            p->ours_ = false;
            p->started_ = false;
            p->ended_ = false;
            p->spoiled_.clear();
        }
    }
    // func_0030EB10 (the world ready): in our replay's playback, the logic's +5 set (as its Prepare does for a race)
    static void scene_on_ready(const b3host::CallArgs& c, b3host::CallResult& r) {
        ReplayView* p = instance();
        p->release_world("a world ready");
        p->scene_on_ready_(c, r);
        if (!p->ours_ || !playing()) return;
        if (const b3host::gaddr logic = logic_now()) b3host::store<std::uint8_t>(logic + 5u, 1);
        p->over_updates_ = 0;
        std::printf("replay_view: the replay's race starts with its countdown\n");
        std::fflush(stdout);
    }
    // the post-race setup: a race ended (not in playback: the game skips it there)
    static void post_race(const b3host::CallArgs& c, b3host::CallResult& r) {
        ReplayView* p = instance();
        p->post_race_(c, r);
        if (playing()) return;
        p->ended_ = true;
        std::printf("replay_view: the race is over (%zu frames recorded%s; the menu's scene %08X)\n", p->frames_.size(),
                    !race_logic(logic_now()) ? ", not a race logic: no replay" : !p->spoiled_.empty() ? ", no replay" : "",
                    b3host::load<std::uint32_t>(kMenuFsm + 4u) & 0x01FFFFFFu);
        std::fflush(stdout);
    }
    // CB3OnePlayerStage::Update: our requests and leaves, in the game's own frame
    // The game in its race (D_0051BACC 5: the race and its results pages; 7 leaving them, 8 a retry, 12 a replay asked
    // for, 4 / 6 loading): the stage's Update sets 12 over whatever is there, so a request is only made in 5
    // The report the game reads on port 0 this VBlank (keys, pads, the menu mouse's presses, a recording's): a new Cross
    // or Start press notes the menu's scene. A results page's choice (Continue, RETRY) plays its out-animation in game
    // state 5 before it leaves (7) or retries (8), so REPLAY waits for the scene to change (a choice always changes it)
    // A press that changed nothing (the scene still the same and the game still in its race state kChoiceHold VBlanks
    // later: a page with no choice to make yet) stops holding REPLAY back; how long a choice took to leave is logged
    void note_pad(const std::uint8_t* report) {
        const bool cross = (report[1] & 0x40u) == 0, start = (report[0] & 0x08u) == 0;   // (bytes 0-1, active low: movie_skip.h)
        const std::uint32_t scene = b3host::load<std::uint32_t>(kMenuFsm + 4u) & 0x01FFFFFFu;
        if ((cross && !cross_held_) || (start && !start_held_)) {
            chosen_on_ = scene;
            chosen_for_ = 0;
        } else if (chosen_on_) {
            ++chosen_for_;
            if (scene != chosen_on_ || !settled()) {
                if (ended_ && !playing())
                    std::printf("replay_view: a choice on the menu's scene %08X: %s after %u VBlanks\n", chosen_on_,
                                scene != chosen_on_ ? "the scene changed" : "the game left its race state", chosen_for_);
                chosen_on_ = 0;
            } else if (chosen_for_ >= kChoiceHold) {
                if (ended_ && !playing())
                    std::printf("replay_view: a press on the menu's scene %08X changed nothing in %u VBlanks: REPLAY offered again\n",
                                chosen_on_, chosen_for_);
                chosen_on_ = 0;
            }
            std::fflush(stdout);
        }
        cross_held_ = cross;
        start_held_ = start;
    }
    static bool settled() { return b3host::load<std::uint32_t>(kGameState) == 5u; }
    static void stage_update(const b3host::CallArgs& c, b3host::CallResult& r) {
        ReplayView* p = instance();
        if (p->pending_request_) {
            p->pending_request_ = false;
            const b3host::gaddr logic = logic_now();
            const std::uint32_t scene = b3host::load<std::uint32_t>(kMenuFsm + 4u) & 0x01FFFFFFu;
            if (!settled()) {   // the results page's Continue / RETRY went first (its 7 / 8 would be overwritten by our 12)
                std::printf("replay_view: no replay: the game is leaving the race (game state %u)\n", b3host::load<std::uint32_t>(kGameState));
                std::fflush(stdout);
            } else if (p->chosen_on_ == scene) {   // a Cross / Start press on the page in the request's own VBlank
                std::printf("replay_view: no replay: a choice was made on the menu's scene %08X\n", scene);
                std::fflush(stdout);
            } else if (logic && (!playing() || (p->ours_ && p->left_))) {
                p->tally_.resize(kTakedownTallyBytes);
                p->records_.resize(kRecordsBytes);
                b3host::copy_from_guest(p->tally_.data(), kTakedownTally, kTakedownTallyBytes);
                b3host::copy_from_guest(p->records_.data(), kRecords, kRecordsBytes);
                p->told_kept_ = false;
                p->began_ = false;
                p->undone_ = 0;
                p->release_world("a replay asked for");   // (running again, as the race left it for the first replay)
                p->return_to_ = b3host::load<std::uint32_t>(kMenuFsm + 4u);
                b3host::store<std::uint8_t>(logic + 4u, 1);
                p->ours_ = true;
                p->left_ = false;
                // (a replay watched before left the playback flag set until the world's reset: nothing of it counts
                // until this one's own prepare)
                p->started_ = false;
                p->continued_ = false;
                p->standing_ = -1;
                p->pending_leave_ = false;
                p->at_ = 0;
                p->over_updates_ = 0;
                std::printf("replay_view: the replay asked for (logic %08X, %zu frames recorded, the menu's scene %08X)\n", logic,
                            p->frames_.size(), p->return_to_);
                std::fflush(stdout);
            }
        }
        if (p->held_ && !settled()) p->release_world("the game left the race");
        // our replay's race, while it is watched (from the leave on, the writes are the game's own: left alone)
        const bool watch = p->ours_ && p->started_ && playing() && !p->left_;
        if (watch) p->before_update();
        p->stage_update_(c, r);
        if (watch) p->after_update();
        if (!p->ours_ || !p->started_ || !playing() || p->left_) return;
        p->continue_intro();
        // the end: the replay's race over (the logic past its racing state, 2) or the recording run out, 3 s after
        const b3host::gaddr logic = logic_now();
        const bool over = (logic && b3host::load<std::int32_t>(logic + 0x40u) > 2) || p->at_ >= p->frames_.size();
        p->over_updates_ = over ? p->over_updates_ + 1 : 0;
        const bool end = p->over_updates_ >= kEndUpdates;
        if (!p->pending_leave_ && !end) return;
        p->pending_leave_ = false;
        p->left_ = true;
        p->chosen_on_ = 0;
        p->check_race_state("at the leave");
        set_scene(p->return_to_);
        b3host::store<std::uint8_t>(kRequested, 0);
        // The world is held behind the results page, as the game holds it on a standing start's intro (D_00665E50 set,
        // the world's CGtTimer stopped [C func_00389EE0 state 1]): the replay's race does not race on there (its
        // recording, then neutral controls), and a held world is silent. Left before its end, the race offers no further
        // replay: held mid-race, the next replay of it started differently (DIFFERENT after a leave at 606 of 1,632
        // frames); what survives the re-prepare from there is not known. Played to its end it can be watched again
        p->hold_world();
        if (!end) {
            p->spoiled_ = "a replay of it was left before its end";
            std::printf("replay_view: no replay of this race from here: %s\n", p->spoiled_.c_str());
        }
        std::printf("replay_view: the replay left (%s, %zu of %zu frames played): back on the menu's scene %08X\n",
                    end ? "its end" : "asked", p->at_, p->frames_.size(), p->return_to_);
        std::fflush(stdout);
    }
    // A standing start (the logic's rolling-start query, vt+0xC0, 0: raceopts' START Standing) waits in state 1 on the
    // race's intro page (0x01E9584C, its +4 set: "wait for Continue") [C func_00389EE0 state 1, func_0043C760]. Its
    // Update (func_0043C470) takes the player's confirm from the input manager -- which a replay's playback does not
    // read -- and starts the page manager's lead-out with event 2, whose end the intro's scene answers with the logic's
    // start (vt+0x24). The replay continues it the same way, the page's own func_0043C570 (that lead-out without the
    // input), once the page is up (the page manager's state D_01E90424 5); the world's clock is stopped meanwhile, so the
    // moment does not change the race. (Calling the logic's start directly left the page's 2D frames behind: the
    // results page failed to build at the replay's end.)
    static constexpr b3host::gaddr kIntroPage = 0x01E9584Cu, kIntroContinue = 0x0043C570u, kPageState = 0x01E90424u;
    void continue_intro() {
        const b3host::gaddr l = logic_now();
        if (continued_) {   // the check: the race should be past its intro within a few seconds of the lead-out
            if (intro_updates_ >= 0 && ++intro_updates_ == kIntroCheck) {
                if (l && b3host::load<std::int32_t>(l + 0x40u) == 1)
                    std::printf("replay_view: a standing start: the race still waits on its intro %d Updates after the lead-out "
                                "(the replay will not start as the race did)\n", kIntroCheck);
                intro_updates_ = -1;
                std::fflush(stdout);
            }
            return;
        }
        if (!l || b3host::load<std::int32_t>(l + 0x40u) != 1 || b3host::load<std::uint8_t>(l + 5u) != 0) return;
        if (standing_ < 0) {
            const b3host::gaddr vt = b3host::load<std::uint32_t>(l) & 0x01FFFFFFu;
            standing_ = (b3host::call_guest<int>(b3host::load<std::uint32_t>(vt + 0xC0u), l) & 0xFF) == 0 ? 1 : 0;
        }
        if (standing_ != 1 || b3host::load<std::uint8_t>(kIntroPage + 4u) == 0 || b3host::load<std::int32_t>(kPageState) != 5) return;
        continued_ = true;
        intro_updates_ = 0;
        b3host::call_guest<void>(kIntroContinue, kIntroPage);
        std::printf("replay_view: a standing start: the race's intro continued as the player's Continue did\n");
        std::fflush(stdout);
    }
    bool continued_ = false;
    int intro_updates_ = -1;   // Updates since the intro's lead-out (-1: not counting)
    static constexpr int kIntroCheck = 300;   // 5 s
    int standing_ = -1;   // our replay's race starts standing (1) or rolling (0); -1 not asked yet
    void hold_world() {
        b3host::store<std::uint8_t>(kWorldHeld, 1);
        b3host::call_guest<void>(kTimerStop, kWorld);
        held_ = true;
    }
    // The hold given back: the flags cleared and the clock started, so whatever comes
    // next (a RETRY, Continue to the next event, a Crash event, a 2P race, the front end) starts as without a replay
    void release_world(const char* why) {
        if (!held_) return;
        held_ = false;
        b3host::store<std::uint8_t>(kWorldHeld, 0);
        b3host::call_guest<void>(kTimerStart, kWorld);
        std::printf("replay_view: the world given back (%s)\n", why);
        std::fflush(stdout);
    }
    // The takedown tally and the records around each Update of our replay's race: what that Update changed is put
    // back (only those bytes: whatever else writes them, a results page, is left alone)
    void before_update() {
        if (!began_) {   // the replay's first Update: what changed since the request is the replay's re-prepare (its
                         // world's prepare clears the takedown tally): put back as the request found it
            began_ = true;
            const auto back = [](b3host::gaddr a, const std::vector<std::uint8_t>& kept, const char* what) {
                const auto* now = static_cast<const std::uint8_t*>(b3host::host_pointer(a, std::uint32_t(kept.size())));
                if (kept.empty() || !now || std::memcmp(now, kept.data(), kept.size()) == 0) return;
                std::size_t n = 0;
                for (std::size_t i = 0; i < kept.size(); ++i)
                    if (now[i] != kept[i] && guarded(a, i)) { b3host::store<std::uint8_t>(a + std::uint32_t(i), kept[i]); ++n; }
                if (!n) return;   // (only the crash scratch differed)
                std::printf("replay_view: %s: %zu bytes the replay's prepare changed put back\n", what, n);
                std::fflush(stdout);
            };
            back(kTakedownTally, tally_, "the takedown tally");
            back(kRecords, records_, "the records");
        }
        const auto copy = [](b3host::gaddr a, std::uint32_t n, std::vector<std::uint8_t>& v) {
            v.resize(n);
            b3host::copy_from_guest(v.data(), a, n);
        };
        copy(kTakedownTally, kTakedownTallyBytes, pre_tally_);
        copy(kRecords, kRecordsBytes, pre_records_);
    }
    void after_update() {
        const auto undo = [&](b3host::gaddr a, std::vector<std::uint8_t>& pre, const char* what) {
            const auto* now = static_cast<const std::uint8_t*>(b3host::host_pointer(a, std::uint32_t(pre.size())));
            if (!now || std::memcmp(now, pre.data(), pre.size()) == 0) return;
            std::size_t n = 0;
            for (std::size_t i = 0; i < pre.size(); ++i)
                if (now[i] != pre[i] && guarded(a, i)) {
                    b3host::store<std::uint8_t>(a + std::uint32_t(i), pre[i]);
                    ++n;
                }
            if (!n) return;   // (only the crash scratch differed)
            undone_ += n;
            if (!told_kept_) std::printf("replay_view: %s: %zu bytes the replay's race changed put back\n", what, n);
            told_kept_ = true;
            std::fflush(stdout);
        };
        undo(kTakedownTally, pre_tally_, "the takedown tally");
        undo(kRecords, pre_records_, "the records");
    }
    // At the leave: the tally and the records against the request's copy (a check only, logged)
    void check_race_state(const char* when) {
        const auto same = [](b3host::gaddr a, const std::vector<std::uint8_t>& kept) {
            const auto* now = static_cast<const std::uint8_t*>(b3host::host_pointer(a, std::uint32_t(kept.size())));
            if (kept.empty()) return true;
            if (!now) return false;
            for (std::size_t i = 0; i < kept.size(); ++i)
                if (now[i] != kept[i] && guarded(a, i)) return false;
            return true;
        };
        std::printf("replay_view: %s: the takedown tally %s, the records %s as the race left them (%zu bytes put back so far)\n", when,
                    same(kTakedownTally, tally_) ? "is" : "is NOT", same(kRecords, records_) ? "are" : "are NOT", undone_);
        std::fflush(stdout);
    }
    // The menu's scene set as the game sets one (the FSM's leave / enter, its timing copied) [C the inline SetScene of
    // func_003960A0 and others]
    static void set_scene(b3host::gaddr state) {
        if (!state) return;
        const b3host::gaddr old = b3host::load<std::uint32_t>(kMenuFsm + 4u) & 0x01FFFFFFu;
        if (old == state) return;
        const b3host::gaddr vt = b3host::load<std::uint32_t>(kMenuFsm) & 0x01FFFFFFu;
        b3host::call_guest<void>(b3host::load<std::uint32_t>(vt + 0x18u), kMenuFsm, state, 0);
        b3host::store<std::uint32_t>(kMenuFsm + 4u, state);
        if (const b3host::gaddr t = b3host::load<std::uint32_t>(kMenuFsm + 0x10u) & 0x01FFFFFFu) {
            const std::uint32_t when = b3host::load<std::uint32_t>(t + 0x20u);
            b3host::store<std::uint32_t>(kMenuFsm + 0x14u, when);
            b3host::store<std::uint32_t>(state + 8u, when);
        }
        b3host::call_guest<void>(b3host::load<std::uint32_t>(vt + 0x1Cu), kMenuFsm, old, 0);
    }

    Entry next_frame_ = nullptr, prepare_ = nullptr, scene_on_ready_ = nullptr, stage_update_ = nullptr, post_race_ = nullptr;
    std::vector<Frame> frames_;
    static constexpr std::size_t kEndUpdates = 180;   // the stage's Updates (3 s) between the replay's end and the leave
    std::size_t at_ = 0, over_updates_ = 0;
    std::uint32_t return_to_ = 0, chosen_on_ = 0, chosen_for_ = 0;
    static constexpr std::uint32_t kChoiceHold = 120;   // 2 s: a results page's choice leaves its scene well within it
    // The takedown tally of the race (D_0051C610, 0x50 bytes: 20 signature-takedown flags, then counts) [C
    // OnTakedown__13CB3StageLogic, func_0013C170]: OnTakedown has no playback test, so the replay's takedowns would add to
    // it; it is kept at the request and put back at the leave
    static constexpr b3host::gaddr kTakedownTally = 0x0051C610u;
    static constexpr unsigned kTakedownTallyBytes = 0x50u;
    // The records (0x01EA7810, 0xF000 bytes: the tables the post-race setup wrote; a lap the replay completes writes there
    // too): kept with the tally
    static constexpr b3host::gaddr kRecords = 0x01EA7810u;
    static constexpr unsigned kRecordsBytes = 0xF000u;
    // recordsspan (2026-11-24): the span also covers D_01EB5B40, CGtV3d[22] (0x160 bytes at +0xE330), the deformed hull
    // vertices ImpactBody__16CB3VehicleDeformFv writes in every crash (DeformHullVerts) and reads back at once
    // (CreateDeformedHull): a scratch, never in the save. The replay's crashes rewrite it, so the leave's check read "the
    // records are NOT as the race left them (0 bytes put back)" with nothing real changed [C a diff at the leave: every
    // differing byte at +0xE330..+0xE42A]. It is neither checked nor put back; the rest of the span (the records, the
    // profile area D_01EAC6D0, the memory card flow's D_01EB5D00 / D_01EB5D38 / D_01EB5D60 after the scratch) as before
    static constexpr unsigned kScratchAt = 0x01EB5B40u - kRecords, kScratchBytes = 0x160u;
    static bool guarded(b3host::gaddr a, std::size_t i) { return a != kRecords || i < kScratchAt || i >= kScratchAt + kScratchBytes; }
    std::vector<std::uint8_t> tally_, records_;           // as the request found them (the leave's check)
    std::vector<std::uint8_t> pre_tally_, pre_records_;   // before the Update now running
    std::size_t undone_ = 0;
    bool began_ = true;   // our replay's first Update seen (false from the request)
    bool told_kept_ = false;
    std::uint32_t last_pos_[3] = {};   // the player's car at the race's last recorded frame (the replay's self-check)
    std::vector<std::uint32_t> last_cars_;   // every race car's position then (3 words each)
    // The race cars' positions (gWorld +0x12AD28: the race cars, D_0064D37C of them; each +0x30, 3 floats as words)
    static void race_cars(std::vector<std::uint32_t>& out) {
        out.clear();
        const std::uint32_t n = std::min<std::uint32_t>(b3host::load<std::uint32_t>(0x0064D37Cu), 16u);
        for (std::uint32_t i = 0; i < n; ++i) {
            const b3host::gaddr car = b3host::load<std::uint32_t>(0x00522660u + 0x12AD28u + 4u * i) & 0x01FFFFFFu;
            for (unsigned j = 0; j < 3; ++j) out.push_back(car ? b3host::load<std::uint32_t>(car + 0x30u + 4u * j) : 0u);
        }
    }
    bool cross_held_ = false, start_held_ = false;
    std::string spoiled_;
    bool held_ = false;   // the world held by our leave (until a replay asks for it again or the next race's prepare)
    bool ours_ = false, started_ = false, told_end_ = false, pending_request_ = false, pending_leave_ = false, left_ = false, ended_ = false;
};

}  // namespace b3native

#endif
