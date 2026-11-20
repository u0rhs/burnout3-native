// skiphint.h -- the video skip hint of the native window (mac5, 2026-11-09; docs/remaster-features.md §2, the open item
// "Video skip hint"): while a movie the port's skip would end is playing (native_play, tools/native/movie_skip.h), a
// small "SKIP" prompt in the bottom right corner with the button that skips it on the device in use: the PlayStation
// Cross, the Xbox A, the Nintendo B (the bottom face button, which the pad maps to Cross) or the keyboard's Enter key
// cap. Its opacity comes from the game (Tick::skip_hint, the 'HSK1' record: faded in game time); PC SETTINGS >
// GAME > INTERFACE > VIDEO SKIP HINT turns it off. Drawn only.
//
// The game's fonts have no button glyphs and none of its textures is decoded during the boot movies, so the hint is
// our own picture: shapes (discs, rounded boxes, a monoline stroke font for its few letters; glyphdraw.h, the pad's
// button as the prompts draw it, prompts.h) rasterised on the CPU with analytic anti-aliasing at the window's pixel
// size, one point-sampled textured quad in the 2D stage.
#ifndef B3REPLAY_SKIPHINT_H
#define B3REPLAY_SKIPHINT_H

#include <cstdint>
#include <vector>

#include "b3gfx/b3gfx.h"
#include "prompts.h"   // PromptDevice (nativepad.h NativePad::device: the last one used), the button glyphs

namespace b3replay {

class SkipHint {
public:
    // The hint's draw item in window pixels (`w` x `h`) at `opacity` 0..1 (nothing at 0), appended to `out`.
    void draw_list(b3gfx::Device& dev, int w, int h, float opacity, PromptDevice d, std::vector<b3gfx::DrawItem>& out);
    // The hint's picture for a window `window_h` pixels high: straight RGBA rows, `w` x `h`; for tests and draw_list.
    static void picture(PromptDevice d, int window_h, int& w, int& h, std::vector<std::uint8_t>& rgba);
    // Where draw_list puts a `pw` x `ph` picture in a `w` x `h` window: its top left pixel (the bottom right corner of
    // the window's centred 16:9 part, a margin of 5 % of the height inside it).
    static void place(int w, int h, int pw, int ph, int& x, int& y);

private:
    b3gfx::TextureId tex_ = b3gfx::kNone;
    b3gfx::BufferId vb_ = b3gfx::kNone, ib_ = b3gfx::kNone;
    b3gfx::ResourceEpoch epoch_;   // a track load clears the ids above (Device::resource_epoch)
    int built_h_ = -1, tex_w_ = 0, tex_h_ = 0;
    PromptDevice built_d_ = PromptDevice::kKeyboard;
};

}  // namespace b3replay

#endif
