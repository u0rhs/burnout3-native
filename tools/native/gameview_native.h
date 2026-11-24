// gameview_native.h -- the game view on the native backend (pic1, docs/native-renderer.md §32): the scene / HUD capture
// (include/b3gameview/gameview.h) fed from the native arena, and the in-process renderer (b3render) opened beside it.
//
// native_boot --render offscreen|window [--render-lib <lib>] [--render-args "<b3replay live options>"] calls install()
// before the boot and finish() at its end. Hook points:
//   - every function in b3gv::entry_points() (the HUD's Gt2d / scene / object entries), at its entry, through b3host's
//     watch hook (traced_entry: calls that go through dispatch; definitions that carry the generator's watch prologue
//     report themselves, see §32.3);
//   - the poll: each return of UpdatePort__22CGtInputManagerPS2Pad2F15EGtPS2InputPort(port 0) 0x214610, the poll point
//     (the pad is read right after that call, 0x214A9C); vblank = the platform's virtual VBlank count.
// Records go to b3render through its C ABI (b3r_submit_at, stamped with the host steady clock at the poll), paced by the
// native frame scheduler: the renderer's display clock follows their arrival, one per retail poll.
#ifndef B3NATIVE_GAMEVIEW_NATIVE_H
#define B3NATIVE_GAMEVIEW_NATIVE_H

#include <cstdint>
#include <string>
#include <vector>

namespace b3gv_native {

struct Options {
    std::string mode;   // "offscreen" or "window"
    std::string lib;    // b3render library (default: next to the exe: libb3render.so / b3render.dll)
    std::string args;   // the renderer's options (b3replay live options; --offscreen-live <dir> is added for offscreen
                        // unless given)
    std::string game;   // the game install (--game), default B3_GAME_DIR or the port's default install
    std::uint64_t (*vblank)() = nullptr;   // the backend's VBlank count (native_boot: platform::vblank_count)
};

// Sets the capture's outputs (B3PORT_RENDER / _ARGS, unless already set), the host callbacks and the watch hook.
// Returns false (with a message) if the mode is unknown. Must run before the first guest call.
bool install(const Options& o, const std::string& exe_dir);
// The poll point: native_boot calls this at each return of UpdatePort with port 0.
void poll();
// The end of the boot (normal or not): closes the feed and lets the renderer finish (B3PORT_RENDER_WAIT_MS).
void finish();
bool active();
// mac4: one more watched function, `fn` called at each of its entries (an observer, as the HUD's; native_play: the
// player's GetAccelerator calls for the key map's context, CGtVideoDecoder::Prepare for the movie skip). Call after
// install(); up to 4.
// `args` are $a0..$t3's low words at the entry.
bool watch_extra(std::uint32_t address, void (*fn)(std::uint32_t address, const std::uint32_t* args));
// mac5: the video skip hint's opacity (0..255, 0 hidden; tools/native/movie_skip.h SkipHintFade), from the game thread;
// the next poll's record carries it to the renderer (src/b3replay/hud.h kHostSkipBase) when it changed.
void set_skip_hint(std::uint8_t opacity);
// todrace (tools/native/race_time.h): the race's DAY / NIGHT choice (0 Default, 1 Day, 2 Night: what the race's frames
// draw) and the track page's label (its choice, its opacity 0..255), from the game thread; the next poll's record carries
// them to the renderer (src/b3replay/hud.h kHostRaceTimeBase) when they changed. Nothing is published before the first call.
void set_race_time(std::uint8_t race, std::uint8_t label_choice, std::uint8_t label_opacity);
// gameopts (tools/native/game_options.h): the race's GAMEPLAY options (bit 0 MIRROR MODE, bit 1 CRASHBREAKER IN EVERY MODE)
// in the same record's race word (bits 9..10); the renderer draws the mirror. Their records go in set_race_options' word.
void set_game_options(std::uint32_t latched);
// raceopts (tools/native/race_options.h): the race's gameplay options as race_choice.h's record word (0: the game's own
// race), in the same record's label word (bits 16..31); the renderer keeps that race's records apart
void set_race_options(std::uint64_t word);   // options3: 64 bits (HTD1's race word 16..31 and word 4 for 32..63)
// replayview (tools/native/replay_view.h): the race replay's state (0 none, 1 REPLAY offered, 2 the replay playing), from
// the VBlank side; the next poll's record carries it to the renderer (src/b3replay/hud.h kHostReplayBase) when it changes
void set_replay_state(int state);
// paintsel (tools/native/paint_select.h): the PAINT prompt on SELECT CAR (its opacity 0..255, the page's player 0 / 1), from
// the game thread; the next poll's record carries it to the renderer (src/b3replay/hud.h kHostPaintSelBase) when it
// changed. Nothing is published before the prompt first shows.
void set_paint_prompt(std::uint8_t opacity, std::uint8_t player);
// paint3 (tools/native/carousel_models.h): the carousel slots whose model is not their picture's ('HCM1' bytes,
// carousel_record), from the game thread; the next poll's record carries them to the renderer (src/b3replay/hud.h
// kHostCarouselBase) when they changed. Nothing is published before the first non-empty set.
void set_carousel_models(const std::vector<std::uint8_t>& record);
// elim1 (single_eliminator.h): TIMED ELIMINATION's countdown, whole seconds to the next elimination (-1: none shown), and the
// colours it is drawn in (RGBA: the race HUD's table 0x0066A4C0 +0x30 / +0x40 / +0x50 / +0x60, then the HUD captions'
// style 1 ramp 0x004CA500..0x004CA530; null: as before), from the game thread; the next poll's record carries it to the
// renderer (src/b3replay/hud.h kHostElimTimerBase)
void set_elim_timer(int seconds, const std::uint32_t colours[8] = nullptr);
// gearbox (gearbox.h): the manual gearbox's gear (-1 reverse, 0 neutral, 1..; -2 none) and the HUD colours as above, from
// the game thread; the next poll's record carries it to the renderer (src/b3replay/hud.h kHostGearBase) when it changed
void set_gear(int gear, const std::uint32_t colours[8] = nullptr);
// 1.3.1 (tools/native/pc_menu.h): the text the renderer shows for the ticker whose text is at guest address `text`
// (empty: the game's own; include/b3gameview/gameview.h b3gv::set_ticker_text). From the game thread.
void set_ticker_text(std::uint32_t text, const std::u16string& display);
// autosave1 (tools/native/save_screens.h): `probe` is asked at each poll whether the display holds the previous picture (the
// AUTOSAVE page is up and SAVE SCREENS is Hide); the poll's record carries it to the renderer (src/b3replay/hud.h
// kHostHoldBase) when it changed. Called from the game thread (null: never).
void set_hold_probe(bool (*probe)());
// fastload1 (tools/native/fast_load.h): while on, the polls' records carry no latch stamp (0); from the game thread.
void set_unstamped(bool on);
// stutter: the current VBlank's place on the pacer's schedule (steady_clock ns; 0: unpaced, the poll's own time is the
// stamp) and whether it started late (include/b3live/sched_stamp.h); from the game thread, each VBlank and after a hold
void set_vblank_stamp(std::uint64_t ns, bool late = false);
// mac5: the menus on screen ('HMN1', include/b3live/menu_input.h; tools/native/menu_mouse.h), from the game thread when
// they change; the next poll's record carries them to the renderer (the mouse's rows).
void set_menu_record(const std::vector<std::uint8_t>& bytes);
// sdk2b (src/native/code_mods.h): called at each poll before its record is made (the code mods' on_frame; null: none),
// and the code mods' overlay record (include/b3mod/codemod.h), carried by the next poll's record when it changed
void set_frame_hook(void (*fn)());
void set_mod_overlay(const std::vector<std::uint8_t>& bytes);

}  // namespace b3gv_native

#endif
