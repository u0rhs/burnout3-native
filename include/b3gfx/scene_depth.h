// scenedepth (remaster-features §41): whether the passes that read the frame's depth (night's lights, the AO, the depth of
// field, the motion blur) take the scene pass's own depth (b3gfx_sdl.cpp g_sd) or draw the depth pre-pass. Pure, so the
// rule is tested without a GPU (tests/test_perf4.cpp).
#ifndef B3GFX_SCENE_DEPTH_H
#define B3GFX_SCENE_DEPTH_H

#include <cstring>

namespace b3gfx {

// The default per backend (SDL_GetGPUDeviceDriver's name; Options::scene_depth -1). Measured: on an RTX 4080 SUPER
// (Vulkan) about even with the pre-pass at 1080p and up to a few ms slower at 4K in split screen, a large saving where the
// second geometry pass is the cost (the software rasteriser). On tile-based GPUs (Apple's, "metal") a second colour target
// costs little and a second geometry pass does not: the candidate for on there, once measured. Not on for every frame on
// any backend for now (nightdepth: the default is night's frames, scene_depth_mode).
inline bool scene_depth_default(const char* driver) {
    (void)driver;
    return false;
}

// Whether R32F render targets blend (the MAX the AO's break draws the post-fog draws' depth with). SDL 3.4 has no query
// for blending, so by backend and GPU name: Direct3D 12 (feature level 11_0 and up: R32_FLOAT blendable is required)
// and Vulkan on desktop GPUs (NVIDIA, AMD, Intel, llvmpipe: all report COLOR_ATTACHMENT_BLEND for R32_SFLOAT) yes;
// mobile-class Vulkan GPUs (Mali, Adreno, PowerVR, Broadcom V3D, Immortalis) and Metal (Apple GPUs do not blend
// 32-bit float targets) no -- there a frame with the AO keeps the pre-pass and the other readers take the scene depth.
inline bool scene_depth_blend(const char* driver, const char* gpu) {
    if (!driver) return false;
    if (std::strcmp(driver, "direct3d12") == 0) return true;
    if (std::strcmp(driver, "vulkan") != 0) return false;
    if (gpu)
        for (const char* m : {"Mali", "Adreno", "PowerVR", "V3D", "Immortalis"})
            if (std::strstr(gpu, m)) return false;
    return true;
}

// `env`: $B3_SCENE_DEPTH (null when unset), which wins when "0" or "1"; else the PC setting `scene_depth` (pc_settings.ini,
// ini only: 0 off, 1 on, -1 the default). nightdepth (1.4.0, the owner: RTX 4080 SUPER at night 288 fps with the
// pre-pass, 389 with the scene depth, +35 %; by day about even): the default is kNight -- the scene depth for night's
// frames (the night lights' spot pass: CONDITIONS / TIME OF DAY Night, World Tour's night races), the pre-pass by day
// (as before: the golden and day play unchanged) -- unless the backend's default is on for every frame.
enum class SceneDepthMode { kOff, kOn, kNight };
inline SceneDepthMode scene_depth_mode(const char* env, int setting, const char* driver) {
    if (env && (env[0] == '0' || env[0] == '1') && env[1] == '\0') return env[0] == '1' ? SceneDepthMode::kOn : SceneDepthMode::kOff;
    if (setting == 0 || setting == 1) return setting == 1 ? SceneDepthMode::kOn : SceneDepthMode::kOff;
    return scene_depth_default(driver) ? SceneDepthMode::kOn : SceneDepthMode::kNight;
}
// Whether the two-target pipelines are made at start (Fable): for every frame's scene depth, or night's when night can
// be on from the start (EXTRAS > NIGHT, $B3_NIGHT); else at the first night race's load (Device::prepare_scene_depth)
inline bool scene_depth_forms_at_start(SceneDepthMode mode, bool night_from_start) {
    return mode == SceneDepthMode::kOn || (mode == SceneDepthMode::kNight && night_from_start);
}
// Whether a frame takes the scene depth in `mode` (with a pass that reads the depth; `night`: night's lights need it)
inline bool scene_depth_frame(SceneDepthMode mode, bool night) {
    return mode == SceneDepthMode::kOn || (mode == SceneDepthMode::kNight && night);
}

}  // namespace b3gfx

#endif
