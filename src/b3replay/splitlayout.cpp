// splitlayout1: the split screen's layout maths (splitlayout.h).
#include "splitlayout.h"

#include <algorithm>
#include <cmath>

namespace b3replay {

SplitLayout split_layout_of(int setting) { return setting == 1 ? SplitLayout::kSideBySide : SplitLayout::kGame; }

GameViewport game_split_viewport(bool side_by_side, bool wide_flag) {
    // CB3GraphicsManagerBase::SetViewportsSplitScreen 0x19E760 [C matched]: (aspect, scale) per branch
    if (side_by_side) return wide_flag ? GameViewport{0.8888889f, 0.75f} : GameViewport{0.6666667f, 0.55f};
    return wide_flag ? GameViewport{3.5555556f, 1.62f} : GameViewport{2.6666667f, 1.35f};
}

bool game_split_wide_flag(float half_w, float half_h) {
    if (!(half_h > 0)) return false;
    const float r = half_w / half_h;
    return std::fabs(r - 3.5555556f) < std::fabs(r - 2.6666667f);
}

float game_split_tangent(float half_w, float half_h) {
    return half_w / game_split_viewport(false, game_split_wide_flag(half_w, half_h)).scale;
}

void split_views(SplitLayout layout, float out_aspect, SplitView views[2]) {
    for (int k = 0; k < 2; ++k) {
        SplitView& v = views[k];
        if (layout == SplitLayout::kSideBySide) {
            v.rect[0] = 0.5f * float(k);
            v.rect[1] = 0.0f;
            v.rect[2] = 0.5f * float(k + 1);
            v.rect[3] = 1.0f;
            v.aspect = 0.5f * out_aspect;
        } else {   // the game's: player 1 on top (View::split_frame before splitlayout1)
            v.rect[0] = 0.0f;
            v.rect[1] = 0.5f * float(k);
            v.rect[2] = 1.0f;
            v.rect[3] = 0.5f * float(k + 1);
            v.aspect = 2.0f * out_aspect;
        }
    }
}

float split_view_half_h(SplitLayout layout, float half_w, float half_h) {
    if (layout != SplitLayout::kSideBySide) return half_h;
    const GameViewport g = game_split_viewport(true, game_split_wide_flag(half_w, half_h));
    return g.scale * game_split_tangent(half_w, half_h) / g.aspect;
}

HudPlace split_hud_place(int k, const SplitView& view, int out_w, int out_h, int width, int height) {
    HudPlace p;
    const float bh = 0.5f * float(height);
    p.src[0] = 0.0f;
    p.src[1] = bh * float(k);
    p.src[2] = float(width);
    p.src[3] = bh * float(k + 1);
    p.dst[0] = view.rect[0] * float(out_w);
    p.dst[1] = view.rect[1] * float(out_h);
    p.dst[2] = view.rect[2] * float(out_w);
    p.dst[3] = view.rect[3] * float(out_h);
    const float dw = p.dst[2] - p.dst[0], dh = p.dst[3] - p.dst[1];
    p.scale = std::min({float(out_h) / float(height), dw / float(width), dh / bh});
    return p;
}

HudPlace frame_hud_place(int out_w, int out_h, int width, int height) {
    HudPlace p;
    p.src[0] = 0.0f;
    p.src[1] = 0.0f;
    p.src[2] = float(width);
    p.src[3] = float(height);
    p.dst[0] = 0.0f;
    p.dst[1] = 0.0f;
    p.dst[2] = float(out_w);
    p.dst[3] = float(out_h);
    p.scale = std::min(float(out_h) / float(height), float(out_w) / float(width));
    return p;
}

HudAffine split_hud_affine(const HudPlace& p, float ax, float ay, bool stretch_x) {
    HudAffine m;
    const float sw = p.src[2] - p.src[0], sh = p.src[3] - p.src[1];
    const float dw = p.dst[2] - p.dst[0], dh = p.dst[3] - p.dst[1];
    if (stretch_x && sw > 0) {
        m.a = dw / sw;
        m.b = p.dst[0] - m.a * p.src[0];
    } else {
        m.a = p.scale;
        m.b = p.dst[0] + ax * dw - p.scale * (p.src[0] + ax * sw);
    }
    m.c = p.scale;
    m.d = p.dst[1] + ay * dh - p.scale * (p.src[1] + ay * sh);
    return m;
}

void split_hud_anchor(const HudPlace& p, const float box[4], float* ax, float* ay) {
    const auto third = [](float rel) { return rel < 1.0f / 3.0f ? 0.0f : rel > 2.0f / 3.0f ? 1.0f : 0.5f; };
    const float sw = p.src[2] - p.src[0], sh = p.src[3] - p.src[1];
    *ax = sw > 0 ? third((0.5f * (box[0] + box[2]) - p.src[0]) / sw) : 0.5f;
    *ay = sh > 0 ? third((0.5f * (box[1] + box[3]) - p.src[1]) / sh) : 0.5f;
}

void split_hud_model(const HudAffine& m, int width, int height, int out_w, int out_h, float model[16]) {
    // the 4:3 item: x_ndc = sx x - 1 + hx -> x = (x_ndc + 1 - hx) / sx; moved: x' = a x + b; x_ndc' = 2 x' / out_w - 1 + hx
    const float ow = float(out_w), oh = float(out_h);
    const float sx = 2.0f / float(width), sy = 2.0f / float(height), hx = 1.0f / ow, hy = 1.0f / oh;
    const float alpha = 2.0f * m.a / (ow * sx), beta = alpha * (1.0f - hx) + 2.0f * m.b / ow - 1.0f + hx;
    // y_ndc = 1 - hy - sy y -> y = (1 - hy - y_ndc) / sy; y' = c y + d; y_ndc' = 1 - hy - 2 y' / out_h
    const float gamma = 2.0f * m.c / (oh * sy), delta = 1.0f - hy - gamma * (1.0f - hy) - 2.0f * m.d / oh;
    for (int col = 0; col < 4; ++col) {
        float* c = model + 4 * col;
        const float x = c[0], y = c[1], w = c[3];
        c[0] = alpha * x + beta * w;
        c[1] = gamma * y + delta * w;
    }
}

bool split_hud_scissor(const HudAffine& m, int width, int height, int out_w, int out_h, const float clip[4], float scissor[4]) {
    const float W = float(width), H = float(height), ow = float(out_w), oh = float(out_h);
    const float s0 = (m.a * scissor[0] * W + m.b) / ow, s2 = (m.a * scissor[2] * W + m.b) / ow;
    const float s1 = (m.c * scissor[1] * H + m.d) / oh, s3 = (m.c * scissor[3] * H + m.d) / oh;
    scissor[0] = std::max(std::min(s0, s2), clip[0] / ow);
    scissor[2] = std::min(std::max(s0, s2), clip[2] / ow);
    scissor[1] = std::max(std::min(s1, s3), clip[1] / oh);
    scissor[3] = std::min(std::max(s1, s3), clip[3] / oh);
    return scissor[2] > scissor[0] && scissor[3] > scissor[1];
}

}  // namespace b3replay
