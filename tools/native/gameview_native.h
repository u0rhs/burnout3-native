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
// autosave1 (tools/native/save_screens.h): `probe` is asked at each poll whether the display holds the previous picture (the
// AUTOSAVE page is up and SAVE SCREENS is Hide); the poll's record carries it to the renderer (src/b3replay/hud.h
// kHostHoldBase) when it changed. Called from the game thread (null: never).
void set_hold_probe(bool (*probe)());
// fastload1 (tools/native/fast_load.h): while on, the polls' records carry no latch stamp (0); from the game thread.
void set_unstamped(bool on);
// mac5: the menus on screen ('HMN1', include/b3live/menu_input.h; tools/native/menu_mouse.h), from the game thread when
// they change; the next poll's record carries them to the renderer (the mouse's rows).
void set_menu_record(const std::vector<std::uint8_t>& bytes);
// sdk2b (src/native/code_mods.h): called at each poll before its record is made (the code mods' on_frame; null: none),
// and the code mods' overlay record (include/b3mod/codemod.h), carried by the next poll's record when it changed
void set_frame_hook(void (*fn)());
void set_mod_overlay(const std::vector<std::uint8_t>& bytes);

}  // namespace b3gv_native

#endif
