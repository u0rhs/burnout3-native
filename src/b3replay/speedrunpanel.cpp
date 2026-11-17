// speedrun1: the speedrun timer's panel (speedrunpanel.h).
#include "speedrunpanel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

#include "glyphdraw.h"

namespace b3replay {

namespace {

constexpr std::string_view kChars = " 0123456789:+-/.ABCDEFGHIJKLMNOPQRSTUVWXYZ";

// Sizes in pixels of a 720-line window, scaled with the height [G: the look]
struct Metrics {
    float s, big_cap, big_t, small_cap, small_t, pad, gap, row_gap, margin, top;
};
Metrics metrics(int h) {
    const float s = std::max(0.75f, float(h) / 720.0f);
    return {s, std::round(18 * s), std::max(1.8f, 2.6f * s), std::round(11 * s), std::max(1.3f, 1.8f * s),
            std::round(9 * s), std::round(14 * s), std::round(4 * s), std::round(0.03f * float(h)), std::round(0.19f * float(h))};
}

float glyph_w(char c, float cap, float t) { return c == ' ' ? cap * 0.5f : b3replay::glyph::text_width(std::string_view(&c, 1), cap, t); }

// the advance of `c`: digits share the widest digit's, so a running time keeps its place
float advance(char c, float cap, float t) {
    float dw = 0;
    for (char d = '0'; d <= '9'; ++d) dw = std::max(dw, glyph_w(d, cap, t));
    const float w = (c >= '0' && c <= '9') ? dw : glyph_w(c, cap, t);
    return w + std::max(1.0f, 0.22f * cap);
}

float width(std::string_view s, float cap, float t) {
    float x = 0;
    for (char c : s) x += advance(c, cap, t);
    return x;
}

void colour(speedrun::Row::Colour c, float (&rgb)[3]) {
    switch (c) {
    case speedrun::Row::kGrey: rgb[0] = rgb[1] = rgb[2] = 0.72f; break;
    case speedrun::Row::kGreen: rgb[0] = 0.40f; rgb[1] = 1.00f; rgb[2] = 0.48f; break;
    case speedrun::Row::kRed: rgb[0] = 1.00f; rgb[1] = 0.42f; rgb[2] = 0.36f; break;
    case speedrun::Row::kGold: rgb[0] = 1.00f; rgb[1] = 0.82f; rgb[2] = 0.30f; break;
    default: rgb[0] = rgb[1] = rgb[2] = 0.96f; break;
    }
}

}  // namespace

std::array<float, 4> SpeedrunPanel::box(int w, int h, const std::vector<speedrun::Row>& rows) {
    const Metrics m = metrics(h);
    float inner = 0, height = 0;
    for (std::size_t k = 0; k < rows.size(); ++k) {
        const bool big = k == 0;
        const float lw = width(rows[k].label, m.small_cap, m.small_t);
        const float vw = width(rows[k].value, big ? m.big_cap : m.small_cap, big ? m.big_t : m.small_t);
        inner = std::max(inner, lw + (lw > 0 && vw > 0 ? m.gap : 0) + vw);
        height += (big ? m.big_cap : m.small_cap) + 2 * m.row_gap;
    }
    const float bw = std::round(inner + 2 * m.pad), bh = std::round(height + 2 * m.pad - 2 * m.row_gap);
    const float safe_w = std::min(float(w), float(h) * 16.0f / 9.0f);
    const float right = std::round(0.5f * (float(w) + safe_w) - m.margin);
    return {right - bw, m.top, bw, bh};
}

void SpeedrunPanel::build(int h, std::vector<std::uint8_t>& px) {
    const Metrics m = metrics(h);
    // two rows of cells (the big font, then the small one), plus a white cell for the box
    auto cell_w = [&](char c, float cap, float t) { return std::ceil(glyph_w(c, cap, t) + 2 * t + 2); };
    float wb = 0, ws = 0;
    for (char c : kChars) { wb += cell_w(c, m.big_cap, m.big_t); ws += cell_w(c, m.small_cap, m.small_t); }
    const float hb = std::ceil(m.big_cap + 2 * m.big_t + 2), hs = std::ceil(m.small_cap + 2 * m.small_t + 2);
    tex_w_ = int(std::max(wb, ws) + 8);
    tex_h_ = int(hb + hs + 8);
    glyph::Canvas cv(tex_w_, tex_h_);
    const float white[4] = {1, 1, 1, 1};
    auto row = [&](float y0, float cap, float t, float ch, std::array<Glyph, 128>& out) {
        float x = 0;
        for (char c : kChars) {
            const float cw = cell_w(c, cap, t);
            std::vector<glyph::Line> l;
            if (c != ' ') glyph::text(std::string_view(&c, 1), x + t + 1, y0 + t + 1, cap, t, l);
            if (!l.empty()) cv.strokes(l, t, white);
            Glyph& g = out[std::size_t(c)];
            g.u0 = x / float(tex_w_);
            g.v0 = y0 / float(tex_h_);
            g.u1 = (x + cw) / float(tex_w_);
            g.v1 = (y0 + ch) / float(tex_h_);
            g.w = cw;
            g.adv = advance(c, cap, t);
            x += cw;
        }
    };
    row(0, m.big_cap, m.big_t, hb, big_);
    row(hb, m.small_cap, m.small_t, hs, small_);
    cv.box(float(tex_w_) - 4, float(tex_h_) - 4, 3, 3, 0, white);
    white_u_ = (float(tex_w_) - 4) / float(tex_w_);
    white_v_ = (float(tex_h_) - 4) / float(tex_h_);
    big_h_ = hb;
    small_h_ = hs;
    built_h_ = h;
    px.assign(std::size_t(tex_w_) * std::size_t(tex_h_) * 4, 0);
    cv.store(px.data(), tex_w_, 0, 0);
}

void SpeedrunPanel::draw_list(b3gfx::Device& dev, int w, int h, const std::vector<speedrun::Row>& rows, std::vector<b3gfx::DrawItem>& out) {
    if (epoch_.changed(dev)) tex_ = vb_ = ib_ = b3gfx::kNone;   // a track load released every id (the game's own took them): make them again
    if (w <= 0 || h <= 0 || rows.empty()) return;
    if (built_h_ != h || tex_ == b3gfx::kNone) {
        std::vector<std::uint8_t> px;
        build(h, px);
        dev.update_texture(tex_, tex_w_, tex_h_, px.data());
    }
    const Metrics m = metrics(h);
    const std::array<float, 4> b = box(w, h, rows);
    std::vector<b3gfx::Vertex> vs;
    std::vector<std::uint32_t> is;
    // colours as FpsPanel's: the 2D stage draws into the half-intensity GS frame (RGB x 64 / 255, alpha x 128 / 255)
    auto rgba = [](float r, float g, float bl, float a) {
        const auto c = [](float v) { return std::uint32_t(std::clamp(v, 0.0f, 1.0f) * 64.0f + 0.5f); };
        return c(r) | c(g) << 8 | c(bl) << 16 | std::uint32_t(std::clamp(a, 0.0f, 1.0f) * 128.0f + 0.5f) << 24;
    };
    auto quad = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, std::uint32_t col) {
        const std::uint32_t base = std::uint32_t(vs.size());
        const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1}, us[4] = {u0, u1, u1, u0}, vv[4] = {v0, v0, v1, v1};
        for (int k = 0; k < 4; ++k) {
            b3gfx::Vertex v{};
            v.pos[0] = xs[k];
            v.pos[1] = ys[k];
            v.uv[0] = us[k];
            v.uv[1] = vv[k];
            v.rgba = col;
            vs.push_back(v);
        }
        for (std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) is.push_back(base + k);
    };
    quad(b[0], b[1], b[0] + b[2], b[1] + b[3], white_u_, white_v_, white_u_, white_v_, rgba(0, 0, 0, 0.55f));
    auto text = [&](std::string_view s, float x, float top, bool big, std::uint32_t col) {
        const std::array<Glyph, 128>& gl = big ? big_ : small_;
        const float t = big ? m.big_t : m.small_t, ch = big ? big_h_ : small_h_;
        const float cap = big ? m.big_cap : m.small_cap;
        float dw = 0;
        for (char d = '0'; d <= '9'; ++d) dw = std::max(dw, glyph_w(d, cap, t));
        for (char c : s) {
            const Glyph& g = gl[std::size_t(static_cast<unsigned char>(c) & 127)];
            if (g.w > 0 && c != ' ') {
                const float off = (c >= '0' && c <= '9') ? std::round(0.5f * (dw - glyph_w(c, cap, t))) : 0.0f;
                const float gx = std::round(x + off - t - 1), gy = std::round(top - t - 1);
                quad(gx, gy, gx + g.w, gy + ch, g.u0, g.v0, g.u1, g.v1, col);
            }
            x += g.adv > 0 ? g.adv : cap * 0.5f;
        }
    };
    float y = b[1] + m.pad;
    for (std::size_t k = 0; k < rows.size(); ++k) {
        const bool big = k == 0;
        const float cap = big ? m.big_cap : m.small_cap;
        float c3[3];
        colour(rows[k].colour, c3);
        const std::uint32_t col = rgba(c3[0], c3[1], c3[2], 1), lab = rgba(0.78f, 0.80f, 0.84f, 1);
        // the label at the left (small, on the value's baseline), the value at the right
        text(rows[k].label, b[0] + m.pad, y + cap - m.small_cap, false, big ? lab : col);
        const float vw = width(rows[k].value, cap, big ? m.big_t : m.small_t);
        text(rows[k].value, b[0] + b[2] - m.pad - vw, y, big, col);
        y += cap + 2 * m.row_gap;
    }
    dev.update_vertex_buffer(vb_, vs.data(), vs.size());
    dev.update_index_buffer(ib_, is.data(), is.size());
    b3gfx::DrawItem it;
    it.overlay = true;
    it.shading = b3gfx::Shading::kTrack;
    it.track_type = 0;
    it.textured = true;
    it.texture = tex_;
    it.gs_alpha = 0x44;
    it.overlay_blend = true;
    it.point_sample = true;   // the atlas is made at the window's pixel size
    const float mm[16] = {2.0f / float(w), 0, 0, 0, 0, -2.0f / float(h), 0, 0, 0, 0, 1, 0, -1, 1, 0.5f, 1};
    std::memcpy(it.model, mm, sizeof mm);
    it.vertices = vb_;
    it.indices = ib_;
    it.first_index = 0;
    it.index_count = std::uint32_t(is.size());
    out.push_back(it);
}

}  // namespace b3replay
