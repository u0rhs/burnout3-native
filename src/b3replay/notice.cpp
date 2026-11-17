// The message banner of the native window (mac5): src/b3replay/notice.h.
#include "notice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "glyphdraw.h"

namespace b3replay {

using glyph::Canvas;
using glyph::Line;

void Notice::show(std::vector<std::string> lines, float hold_s) {
    lines_ = std::move(lines);
    hold_ = hold_s;
    age_ = 0.0f;
    dirty_ = true;
}

float Notice::opacity(float age, float hold) {
    if (age < 0.0f) return 0.0f;
    if (age <= hold) return 1.0f;
    return std::clamp(1.0f - (age - hold), 0.0f, 1.0f);
}

namespace {

// The box's layout: sizes of a 720-line window, scaled with the height; the first line a 14-pixel cap (the heading),
// the others 10.5
struct Layout {
    float s = 1, pad = 0, gap = 0;
    std::vector<float> caps, strokes, widths, tops;
    int w = 0, h = 0;
};

Layout layout(const std::vector<std::string>& lines, int window_h) {
    Layout L;
    L.s = std::max(0.75f, float(window_h) / 720.0f);
    L.pad = 14 * L.s;
    L.gap = 9 * L.s;
    float tw = 0, th = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float cap = (i == 0 ? 14.0f : 10.5f) * L.s, st = std::max(1.3f, (i == 0 ? 2.2f : 1.7f) * L.s);
        L.caps.push_back(cap);
        L.strokes.push_back(st);
        L.widths.push_back(glyph::text_width(lines[i], cap, st));
        tw = std::max(tw, L.widths.back());
        th += cap + (i ? L.gap : 0.0f);
    }
    L.w = int(std::ceil(tw + 2 * L.pad));
    L.h = int(std::ceil(th + 2 * L.pad));
    float y = L.pad;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        L.tops.push_back(y);
        y += L.caps[i] + L.gap;
    }
    return L;
}

const float kHead[4] = {1.0f, 0.80f, 0.25f, 1.0f}, kBody[4] = {0.95f, 0.95f, 0.95f, 1.0f};

void back(Canvas& c, const Layout& L) {
    const float bg[4] = {0.02f, 0.02f, 0.03f, 0.78f}, edge[4] = {0.95f, 0.72f, 0.20f, 0.9f};
    const float w = float(L.w), h = float(L.h), s = L.s;
    c.box(w * 0.5f, h * 0.5f, w * 0.5f, h * 0.5f, 8 * s, bg);
    c.box_outline(w * 0.5f, h * 0.5f, w * 0.5f - s, h * 0.5f - s, 7 * s, std::max(1.0f, 1.5f * s), edge);
}

std::vector<Line> line_strokes(const std::vector<std::string>& lines, const Layout& L, std::size_t i) {
    std::vector<Line> l;
    glyph::text(lines[i], 0.5f * (float(L.w) - L.widths[i]), L.tops[i], L.caps[i], L.strokes[i], l);
    return l;
}

}  // namespace

void Notice::picture(const std::vector<std::string>& lines, int window_h, int& w, int& h, std::vector<std::uint8_t>& rgba) {
    const Layout L = layout(lines, window_h);
    w = L.w;
    h = L.h;
    Canvas c(w, h);
    back(c, L);
    for (std::size_t i = 0; i < lines.size(); ++i) c.strokes(line_strokes(lines, L, i), L.strokes[i], i == 0 ? kHead : kBody);
    rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    c.store(rgba.data(), w, 0, 0);
}

const std::vector<std::uint8_t>& Notice::picture_kept(const std::vector<std::string>& lines, int window_h, int& w, int& h) {
    Kept& k = kept_;
    const Layout L = layout(lines, window_h);
    w = L.w;
    h = L.h;
    if (k.window_h == window_h && k.w == L.w && k.h == L.h && k.lines.size() == lines.size()) {
        std::size_t changed = lines.size(), n = 0;
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i] != k.lines[i]) {
                changed = i;
                ++n;
            }
        if (n == 0) return k.rgba;
        if (n == 1) {   // one line: its rows (old and new) back to the box alone, the line drawn again, if no other line's rows meet them
            const std::size_t i = changed;
            const std::vector<Line> l = line_strokes(lines, L, i);
            int r0 = 0, r1 = 0;
            k.pic.strokes_rows(l, L.strokes[i], r0, r1);
            const std::array<int, 2> now{r0, r1};
            int a = k.rows[i][0], b = k.rows[i][1];
            if (now[1] > now[0]) {
                a = a < b ? std::min(a, now[0]) : now[0];
                b = std::max(b, now[1]);
            }
            bool alone = true;
            for (std::size_t j = 0; j < k.rows.size(); ++j)
                if (j != i && k.rows[j][1] > k.rows[j][0] && k.rows[j][0] < b && a < k.rows[j][1]) alone = false;
            if (alone) {
                if (b > a) {
                    std::copy(k.back.px.begin() + std::ptrdiff_t(a) * k.back.w, k.back.px.begin() + std::ptrdiff_t(b) * k.back.w,
                              k.pic.px.begin() + std::ptrdiff_t(a) * k.pic.w);
                    k.pic.strokes(l, L.strokes[i], i == 0 ? kHead : kBody);
                    k.pic.store(k.rgba.data(), w, 0, 0, a, b);
                }
                k.rows[i] = now;
                k.lines[i] = lines[i];
                return k.rgba;
            }
        }
    }
    // the whole picture, kept
    k.lines = lines;
    k.window_h = window_h;
    k.w = L.w;
    k.h = L.h;
    k.back = Canvas(w, h);
    back(k.back, L);
    k.pic = k.back;
    k.rows.assign(lines.size(), {0, 0});
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::vector<Line> l = line_strokes(lines, L, i);
        k.pic.strokes_rows(l, L.strokes[i], k.rows[i][0], k.rows[i][1]);
        k.pic.strokes(l, L.strokes[i], i == 0 ? kHead : kBody);
    }
    k.rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    k.pic.store(k.rgba.data(), w, 0, 0);
    return k.rgba;
}

bool Notice::box(int w, int h, float (&xywh)[4]) const {
    if (!visible() || w <= 0 || h <= 0) return false;
    const Layout L = layout(lines_, h);
    xywh[0] = std::round(0.5f * float(w - L.w));
    xywh[1] = std::round(bottom ? 0.96f * float(h) - float(L.h) : 0.06f * float(h));
    xywh[2] = float(L.w);
    xywh[3] = float(L.h);
    return true;
}

void Notice::draw_list(b3gfx::Device& dev, int w, int h, float dt, std::vector<b3gfx::DrawItem>& out) {
    if (epoch_.changed(dev)) tex_ = vb_ = ib_ = b3gfx::kNone;   // a track load released every id (the game's own took them): make them again
    if (lines_.empty() || w <= 0 || h <= 0) return;
    age_ += std::max(0.0f, dt);
    const float op = opacity(age_, hold_);
    if (!(op > 0)) return;
    if (dirty_ || built_h_ != h || tex_ == b3gfx::kNone) {
        const std::vector<std::uint8_t>& px = picture_kept(lines_, h, tex_w_, tex_h_);
        dev.update_texture(tex_, tex_w_, tex_h_, px.data());
        built_h_ = h;
        dirty_ = false;
    }
    const float x0 = std::round(0.5f * float(w - tex_w_)), y0 = std::round(bottom ? 0.96f * float(h) - float(tex_h_) : 0.06f * float(h));
    const float x1 = x0 + float(tex_w_), y1 = y0 + float(tex_h_);
    // the 2D stage's half-intensity GS frame, as the skip hint: RGB x 64, alpha x 128 = 1
    const std::uint32_t rgba = 64u | 64u << 8 | 64u << 16 | std::uint32_t(std::lround(op * 128.0f)) << 24;
    const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
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
    it.point_sample = true;
    const float m[16] = {2.0f / float(w), 0, 0, 0, 0, -2.0f / float(h), 0, 0, 0, 0, 1, 0, -1, 1, 0.5f, 1};
    std::memcpy(it.model, m, sizeof m);
    it.vertices = vb_;
    it.indices = ib_;
    it.first_index = 0;
    it.index_count = 6;
    out.push_back(it);
}

}  // namespace b3replay
