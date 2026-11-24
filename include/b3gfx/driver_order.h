// d3d12: the SDL_GPU drivers to try, in order, for a chosen GRAPHICS API (b3gfx::Options::driver; b3settings
// graphics_api through b3replay::graphics_api_driver). Every GPU device the port makes goes through it: b3gfx's
// Device::init (the game's renderer, the launcher's previews, b3view, gameview_replay, the showroom, headless) and the
// bug report's GPU probe (b3report sysinfo), so they all name the same backend.
//
// SDL 3.4.2 tries Metal, then D3D12, then Vulkan (SDL_gpu.c `backends`) for an unnamed device: once DXIL is offered, an
// unnamed device on Windows would be D3D12. The port's default stays Vulkan, so on Windows Auto ("") names Vulkan first
// and D3D12 only after it; a named API that cannot start falls back to the other. Off Windows: the choice as given
// ("": SDL's own, Vulkan on Linux, Metal on macOS). Pure, so the rule is tested without a GPU (tests/test_perf4.cpp).
#ifndef B3GFX_DRIVER_ORDER_H
#define B3GFX_DRIVER_ORDER_H

#include <string>
#include <vector>

namespace b3gfx {

inline std::vector<std::string> driver_attempts(const std::string& chosen, bool windows) {
    if (!windows) return {chosen};
    if (chosen.empty() || chosen == "vulkan") return {"vulkan", "direct3d12"};
    if (chosen == "direct3d12") return {"direct3d12", "vulkan"};
    return {chosen};   // anything else as asked (SDL refuses what it does not have)
}

inline bool windows_build() {
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}

}  // namespace b3gfx

#endif
