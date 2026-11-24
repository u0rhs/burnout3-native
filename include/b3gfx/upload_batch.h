// uploadbatch: whether the route's uploads may be batched (b3gfx_sdl.cpp Device::upload_batch; perf4), before the backend
// check (SDL 3.4.2's Vulkan backend only, see there). Pure, so the rule is tested without a GPU (tests/test_perf4.cpp).
#ifndef B3GFX_UPLOAD_BATCH_H
#define B3GFX_UPLOAD_BATCH_H

#include <cstring>

namespace b3gfx {

// `env`: $B3_UPLOAD_BATCH (null when unset), which wins: "0" off, "offscreen" on without a window too (timing only),
// anything else on (with a window). Unset: the PC setting `upload_batch` (pc_settings.ini, ini only; true by default).
// Without a window only "offscreen" batches: a device without a claimed window defragments at every submit (see the
// device's comment), so there each upload keeps its own submit.
inline bool upload_batch_wanted(const char* env, bool setting, bool window) {
    const bool set = env && *env;
    if (set && env[0] == '0') return false;
    if (set && std::strcmp(env, "offscreen") == 0) return true;
    if (!window) return false;
    return set ? true : setting;
}

}  // namespace b3gfx

#endif
