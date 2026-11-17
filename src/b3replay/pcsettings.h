// settings1 (docs/pc-settings.md): our PC settings applied to the native renderer's view. Every value is drawn-only.
#ifndef B3REPLAY_PCSETTINGS_H
#define B3REPLAY_PCSETTINGS_H

#include "b3settings/settings.h"

namespace b3replay {

class View;

// The view-level rows (effects, post, HUD layout, car detail, damage, camera). The window / device / loop rows (MSAA,
// filter, VSync, fullscreen, frame cap, interpolation, road shine, FPS counter) are applied by the caller.
void apply_view(View& v, const b3settings::Settings& s);
}  // namespace b3replay
namespace b3gfx { struct Options; }
namespace b3replay {
// The texture filter row into the device options (applies when the device is made).
void filter_options(b3settings::Filter f, b3gfx::Options& o);
// effects2: RENDER SCALE into the device options (the percent of the settings; 100: the picture as before)
void scale_options(const b3settings::Settings& s, b3gfx::Options& o);

}  // namespace b3replay

#endif
