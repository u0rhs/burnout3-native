// The video skip hint of the native window (mac5): src/b3replay/skiphint.h.
#include "skiphint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

#include "glyphdraw.h"

namespace b3replay {

using glyph::Canvas;
using glyph::Line;
using glyph::text;
using glyph::text_width;

void SkipHint::picture(PromptDevice d, int window_h, int& w, int& h, std::vector<std::uint8_t>& rgba) {
    // Sizes in pixels of a 720-line window, scaled with the height [G: the look]: a dark pill 34 high, the button 24
    // across (or a key cap 24 high), "SKIP" with an 11-pixel cap.
    const float s = std::max(0.75f, float(window_h) / 720.0f);
    const float ph = std::round(34 * s), pad = (ph - std::round(24 * s)) * 0.5f;
    const float cap = 11 * s, stroke = std::max(1.5f, 2.0f * s), gap = 9 * s, right = 14 * s;
    const float key_cap = 8.5f * s, key_stroke = std::max(1.2f, 1.6f * s), key_pad = 7 * s;
    const bool key = d == PromptDevice::kKeyboard;
    const float bw = key ? text_width("ENTER", key_cap, key_stroke) + 2 * key_pad : ph - 2 * pad;   // the button's width
    const float tw = text_width("SKIP", cap, stroke);
    const float left = key ? pad + 5 * s : pad;   // a key cap clear of the pill's round end
    w = int(std::ceil(left + bw + gap + tw + right));
    h = int(ph);
    Canvas c(w, h);
    const float bg[4] = {0.0f, 0.0f, 0.0f, 0.5f}, white[4] = {0.95f, 0.95f, 0.95f, 1.0f};
    c.box(float(w) * 0.5f, ph * 0.5f, float(w) * 0.5f, ph * 0.5f, ph * 0.5f, bg);
    const float by = ph * 0.5f;
    if (key) {   // a key cap with the key's name
        const float kh = ph - 2 * pad, kx = left + bw * 0.5f;
        const float fill[4] = {0.16f, 0.17f, 0.19f, 0.95f}, edge[4] = {0.80f, 0.82f, 0.86f, 0.9f};
        c.box(kx, by, bw * 0.5f, kh * 0.5f, 4.5f * s, fill);
        c.box_outline(kx, by, bw * 0.5f - 0.5f * key_stroke, kh * 0.5f - 0.5f * key_stroke, 4.0f * s, std::max(1.0f, 1.3f * s), edge);
        std::vector<Line> l;
        text("ENTER", left + key_pad, by - key_cap * 0.5f, key_cap, key_stroke, l);
        c.strokes(l, key_stroke, white);
    } else {     // the bottom face button (the pad's Cross) as the menus' prompts draw it (prompts.h)
        const float fill[4] = {0.10f, 0.105f, 0.12f, 0.95f};
        c.disc(left + 0.5f * bw, by, 0.5f * bw, fill);
        paint_prompt_glyph(c, left, pad, bw, d, PadButton::kCross);
    }
    std::vector<Line> l;
    text("SKIP", left + bw + gap, by - cap * 0.5f, cap, stroke, l);
    c.strokes(l, stroke, white);
    rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    c.store(rgba.data(), w, 0, 0);
}


void SkipHint::place(int w, int h, int pw, int ph, int& x, int& y) {
    const float safe_w = std::min(float(w), float(h) * 16.0f / 9.0f), margin = std::round(0.05f * float(h));
    x = int(std::lround(0.5f * (float(w) + safe_w) - margin)) - pw;
    y = h - int(margin) - ph;
}

void SkipHint::draw_list(b3gfx::Device& dev, int w, int h, float opacity, PromptDevice d, std::vector<b3gfx::DrawItem>& out) {
    if (epoch_.changed(dev)) tex_ = vb_ = ib_ = b3gfx::kNone;   // a track load released every id (the game's own took them): make them again
    if (w <= 0 || h <= 0 || !(opacity > 0)) return;
    if (built_h_ != h || built_d_ != d || tex_ == b3gfx::kNone) {
        std::vector<std::uint8_t> px;
        picture(d, h, tex_w_, tex_h_, px);
        dev.update_texture(tex_, tex_w_, tex_h_, px.data());
        built_h_ = h;
        built_d_ = d;
    }
    int x0 = 0, y0 = 0;
    place(w, h, tex_w_, tex_h_, x0, y0);
    const float x1 = float(x0 + tex_w_), y1 = float(y0 + tex_h_);
    // Colours as FpsPanel's: the 2D stage draws into the half-intensity GS frame (RGB x 64, alpha x 128 = 1); eased
    const float e = std::clamp(opacity, 0.0f, 1.0f), ease = e * e * (3 - 2 * e);
    const std::uint32_t rgba = 64u | 64u << 8 | 64u << 16 | std::uint32_t(std::lround(ease * 128.0f)) << 24;
    const float xs[4] = {float(x0), x1, x1, float(x0)}, ys[4] = {float(y0), float(y0), y1, y1};
    const float us[4] = {0, 1, 1, 0}, vs_[4] = {0, 0, 1, 1};
    b3gfx::Vertex vs[4];
    for (int k = 0; k < 4; ++k) {
        vs[k] = b3gfx::Vertex{};
        vs[k].pos[0] = xs[k];
        vs[k].pos[1] = ys[k];
        vs[k].uv[0] = us[k];
        vs[k].uv[1] = vs_[k];
        vs[k].rgba = rgba;
    }
    const std::uint32_t is[6] = {0, 1, 2, 0, 2, 3};
    dev.update_vertex_buffer(vb_, vs, 4);
    dev.update_index_buffer(ib_, is, 6);
    b3gfx::DrawItem it;
    it.overlay = true;
    it.shading = b3gfx::Shading::kTrack;
    it.track_type = 0;
    it.textured = true;
    it.texture = tex_;
    it.gs_alpha = 0x44;
    it.overlay_blend = true;
    it.point_sample = true;   // the picture is made at the window's pixel size
    const float m[16] = {2.0f / float(w), 0, 0, 0, 0, -2.0f / float(h), 0, 0, 0, 0, 1, 0, -1, 1, 0.5f, 1};
    std::memcpy(it.model, m, sizeof m);
    it.vertices = vb_;
    it.indices = ib_;
    it.first_index = 0;
    it.index_count = 6;
    out.push_back(it);
}

}  // namespace b3replay
