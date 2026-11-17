// Small CPU rasterizer for headless previews of decoded models (no GPU, no window): perspective view of the model's
// bounds from a yaw/pitch direction, depth-buffered, nearest-texel sampling, vertex colour, simple N.L shading.
// For checking decoders offline; not a reference for the game's look.
#ifndef B3ASSET_PREVIEW_H
#define B3ASSET_PREVIEW_H

#include <vector>

#include "b3asset/model.h"

namespace b3asset {

struct PreviewView {
    float yaw_deg = 35.0f, pitch_deg = 25.0f;  // camera direction around the bounds centre (y up)
    float fov_deg = 50.0f;
    float distance_scale = 1.0f;               // 1 = fit the bounding sphere
    bool textured = true, shaded = true;
    // Optional explicit camera (used when has_camera): eye position and look-at target.
    bool has_camera = false;
    float eye[3] = {0, 0, 0}, target[3] = {0, 0, 1};
};

Image render_preview(const Model& m, int w, int h, const PreviewView& view);

}  // namespace b3asset

#endif
