// perf4: RENDER SCALE above 100% (supersampling, the PC settings' 125 / 150 / 200 %). The 3D picture is drawn at the scale and
// brought down to the window with an area (box) filter (post.frag mode 4, a.z 3). Such a target can pass what the GPU takes:
// capped_scale gives the largest scale up to the one asked for whose targets keep every side within `max_side` and their
// memory (the estimate below) within the budget (supersample_budget_mb: from the card's memory); 1 (no supersampling) when
// none above 100% fits.
#ifndef B3GFX_RENDER_SCALE_H
#define B3GFX_RENDER_SCALE_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace b3gfx {

// The scaled target set's bytes per pixel, everything that is made at its size: the scene colour (RGBA16F) and depth (32-bit)
// per MSAA sample, the resolved colour (MSAA only), the lean bloom's resolved copy (MSAA, B3_BLOOM_LEAN), the PC-quality zoom
// blur's two buffers, the depth of field / surface pre-pass (depth + its R8 colour), the depth of field's two half-size
// buffers, the surface's half-size AO buffer, night mode's copy of the frame (single-sample, with or without MSAA:
// with MSAA the scene pass resolves into it; nightlights) and, with `motion` (finalfix: photo mode's motion blur can run),
// its two full-size RGBA16F buffers (the velocity and the accumulation; its tile buffers are a 1/256 of a pixel's), and with
// `mirror` (gameopts: MIRROR MODE on in the frame) the picture's reversed RGBA16F copy.
inline double scaled_bytes_per_pixel(int msaa, bool lean = false, bool motion = false, bool mirror = false) {
    const int s = std::max(1, msaa);
    return double(s) * (8.0 + 4.0) + (s > 1 ? 8.0 : 0.0) + (s > 1 && lean ? 8.0 : 0.0) + 16.0 + 5.0 + 4.0 + 2.0 + 8.0 + (motion ? 16.0 : 0.0) +
           (mirror ? 8.0 : 0.0);
}

// finalfix: the sun-shadow map's MB, outside the per-pixel sum (its side is fixed: 1024 / 2048 texels): the R32F map and
// its 32-bit depth target. The supersampling budget keeps it aside while SUN SHADOWS is on.
inline double sun_shadow_map_mb(std::uint32_t side) { return double(side) * double(side) * (4.0 + 4.0) / 1048576.0; }

// The largest scale up to `want` (at most 2) whose set, `bytes_per_pixel` at w x h times the scale squared, fits `budget_mb`
// and keeps every side within `max_side`; whole percent; 1 when nothing above 100% fits (or want <= 1: want itself).
inline float capped_scale(float want, std::uint32_t w, std::uint32_t h, double bytes_per_pixel, double budget_mb, int max_side) {
    if (!(want > 1.0f) || w == 0 || h == 0) return want > 0.0f ? std::min(want, 1.0f) : 1.0f;
    double s = std::min<double>(want, 2.0);
    s = std::min(s, double(max_side) / double(w));
    s = std::min(s, double(max_side) / double(h));
    const double px = double(w) * double(h) * bytes_per_pixel;
    if (budget_mb > 0) s = std::min(s, std::sqrt(budget_mb * 1048576.0 / px));
    s = std::floor(s * 100.0) / 100.0;   // whole percent
    return s > 1.0 ? float(s) : 1.0f;
}

// The memory budget for the supersampled sets from the GPU's device-local memory (MB, 0: not known): a quarter of it, within
// 512 MB .. 4 GB; 1 GB when it is not known.
inline double supersample_budget_mb(double device_local_mb) {
    if (!(device_local_mb > 0)) return 1024.0;
    return std::clamp(device_local_mb / 4.0, 512.0, 4096.0);
}

// The scaled target's side for an output side (at least 1)
inline std::uint32_t scaled_side(std::uint32_t side, float scale) {
    return std::max<std::uint32_t>(1, std::uint32_t(std::floor(float(side) * scale + 0.5f)));
}

// The 2D stage under RENDER SCALE (b3gfx_sdl.cpp render_scaled): the depth-tested 2D items (DrawItem::overlay_ztest, the
// World Tour / Crash Nav globe) need the scene's depth, so they are drawn in the scaled pass; the rest goes over the upscaled
// picture. The list order is the game's draw order (the globe over the 2D backdrop drawn before it), so the scaled pass takes
// the whole prefix of the list up to and including the last depth-tested 2D item, and the output-size pass the items after
// it. Returns that prefix's length: 0 when no 2D item tests depth (then every 2D item is drawn at the output's size).
template <class Items>
std::size_t scaled_overlay_end(const Items& items) {
    std::size_t end = 0;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].overlay && items[i].overlay_ztest) end = i + 1;
    return end;
}

}  // namespace b3gfx

#endif
