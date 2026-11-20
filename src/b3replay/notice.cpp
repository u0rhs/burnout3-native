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
    style_ = Style::kBanner;
    tone_ = Tone::kNeutral;
}

void Notice::show_bubble(std::vector<std::string> lines, Tone tone, float hold_s) {
    show(std::move(lines), hold_s);
    style_ = Style::kBubble;
    tone_ = tone;
}

Notice::Tone Notice::save_bubble_lines(const std::string& status, std::vector<std::string>& lines) {
    lines.clear();
    for (std::size_t a = 0;;) {
        const std::size_t b = status.find(" - ", a);
        lines.push_back(status.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 3;
    }
    auto starts = [&](const char* p) { return status.compare(0, std::strlen(p), p) == 0; };
    if (starts("SAVE WARNING")) return Tone::kWarn;
    if (starts("SAVE FAILED") || starts("NOT SAVED")) return Tone::kBad;
    return Tone::kOk;
}

float Notice::opacity(float age, float hold) {
    if (age < 0.0f) return 0.0f;
    if (age <= hold) return 1.0f;
    return std::clamp(1.0f - (age - hold), 0.0f, 1.0f);
}

namespace {

// The box's layout: sizes of a 720-line window, scaled with the height; the first line a 14-pixel cap (the heading),
// the others 10.5. saveflow: a bubble is ONE row (it fits the free strip along the window's top edge, above the menus'
// titles and the race HUD's boxes): after the tone's bar, the heading (a 7.5 cap) and the other parts (6.5) side by
// side on one baseline, a dot between them.
struct Layout {
    float s = 1, pad = 0, gap = 0, left = 0;
    bool bubble = false;
    std::vector<float> caps, strokes, widths, tops, lefts, dots;   // lefts: each part's x; dots: the dots' x (a bubble)
    float dot_y = 0;
    int w = 0, h = 0;
};

Layout layout(const std::vector<std::string>& lines, int window_h, Notice::Style style = Notice::Style::kBanner) {
    Layout L;
    L.bubble = style == Notice::Style::kBubble;
    L.s = std::max(0.75f, float(window_h) / 720.0f);
    L.pad = (L.bubble ? 7 : 14) * L.s;
    L.gap = (L.bubble ? 5 : 9) * L.s;
    L.left = L.bubble ? 6 * L.s : 0.0f;   // the tone's bar
    if (L.bubble) {
        const float pad_x = 7 * L.s, pad_y = 6 * L.s, sep = 13 * L.s, head = 7.5f * L.s, body = 6.5f * L.s;
        float x = pad_x + L.left;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const float cap = i == 0 ? head : body, st = std::max(1.0f, (i == 0 ? 1.3f : 1.1f) * L.s);
            if (i) {
                L.dots.push_back(x + 0.5f * sep);
                x += sep;
            }
            L.caps.push_back(cap);
            L.strokes.push_back(st);
            L.widths.push_back(glyph::text_width(lines[i], cap, st));
            L.lefts.push_back(x);
            L.tops.push_back(pad_y + head - cap);   // one baseline
            x += L.widths.back();
        }
        L.pad = pad_x;
        L.dot_y = pad_y + head - 0.5f * body;
        L.w = int(std::ceil(x + pad_x));
        L.h = int(std::ceil(head + 2 * pad_y));
        return L;
    }
    float tw = 0, th = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float cap = L.bubble ? (i == 0 ? 8.0f : 6.5f) * L.s : (i == 0 ? 14.0f : 10.5f) * L.s;
        const float st = L.bubble ? std::max(1.0f, (i == 0 ? 1.35f : 1.1f) * L.s) : std::max(1.3f, (i == 0 ? 2.2f : 1.7f) * L.s);
        L.caps.push_back(cap);
        L.strokes.push_back(st);
        L.widths.push_back(glyph::text_width(lines[i], cap, st));
        tw = std::max(tw, L.widths.back());
        th += cap + (i ? L.gap : 0.0f);
    }
    L.w = int(std::ceil(tw + 2 * L.pad + L.left));
    L.h = int(std::ceil(th + 2 * L.pad));
    float y = L.pad;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        L.tops.push_back(y);
        y += L.caps[i] + L.gap;
    }
    return L;
}

const float kHead[4] = {1.0f, 0.80f, 0.25f, 1.0f}, kBody[4] = {0.95f, 0.95f, 0.95f, 1.0f};
const float kBubbleBody[4] = {0.86f, 0.86f, 0.88f, 1.0f};

// saveflow: a bubble's tone colour (its bar and heading)
using Rgba = float[4];
const Rgba& tone_colour(Notice::Tone t) {
    static const float ok[4] = {0.45f, 0.88f, 0.50f, 1.0f}, warn[4] = {1.0f, 0.78f, 0.25f, 1.0f}, bad[4] = {1.0f, 0.42f, 0.36f, 1.0f},
                       neutral[4] = {0.92f, 0.92f, 0.94f, 1.0f};
    return t == Notice::Tone::kOk ? ok : t == Notice::Tone::kWarn ? warn : t == Notice::Tone::kBad ? bad : neutral;
}

void back(Canvas& c, const Layout& L, Notice::Tone tone = Notice::Tone::kNeutral) {
    const float w = float(L.w), h = float(L.h), s = L.s;
    if (L.bubble) {   // a quiet dark box, the tone's bar along its left edge
        const float bg[4] = {0.03f, 0.03f, 0.04f, 0.72f};
        const Rgba& t = tone_colour(tone);
        const float bar[4] = {t[0], t[1], t[2], 0.95f};
        const float dot[4] = {0.62f, 0.62f, 0.66f, 0.9f};
        c.box(w * 0.5f, h * 0.5f, w * 0.5f, h * 0.5f, 4 * s, bg);
        c.box(L.pad * 0.5f + 1.5f * s, h * 0.5f, 1.25f * s, h * 0.5f - 4.5f * s, 1.25f * s, bar);
        for (const float x : L.dots) c.disc(x, L.dot_y, 1.1f * s, dot);
        return;
    }
    const float bg[4] = {0.02f, 0.02f, 0.03f, 0.78f}, edge[4] = {0.95f, 0.72f, 0.20f, 0.9f};
    c.box(w * 0.5f, h * 0.5f, w * 0.5f, h * 0.5f, 8 * s, bg);
    c.box_outline(w * 0.5f, h * 0.5f, w * 0.5f - s, h * 0.5f - s, 7 * s, std::max(1.0f, 1.5f * s), edge);
}

const Rgba& line_colour(const Layout& L, std::size_t i, Notice::Tone tone) {
    if (L.bubble) return i == 0 ? tone_colour(tone) : kBubbleBody;
    return i == 0 ? kHead : kBody;
}

std::vector<Line> line_strokes(const std::vector<std::string>& lines, const Layout& L, std::size_t i) {
    std::vector<Line> l;
    const float x = L.bubble ? L.lefts[i] : 0.5f * (float(L.w) - L.widths[i]);
    glyph::text(lines[i], x, L.tops[i], L.caps[i], L.strokes[i], l);
    return l;
}

// a bubble's place: the window's top right corner, a small margin from both edges (the strip above the menus' titles and
// the race HUD's LAP / POS boxes, which begin ~35 lines down in a 720-line window, at every width and HUD layout)
float bubble_margin(int window_h) { return std::round(6.0f * std::max(0.75f, float(window_h) / 720.0f)); }

}  // namespace

void Notice::picture(const std::vector<std::string>& lines, int window_h, int& w, int& h, std::vector<std::uint8_t>& rgba, Style style,
                     Tone tone) {
    const Layout L = layout(lines, window_h, style);
    w = L.w;
    h = L.h;
    Canvas c(w, h);
    back(c, L, tone);
    for (std::size_t i = 0; i < lines.size(); ++i) c.strokes(line_strokes(lines, L, i), L.strokes[i], line_colour(L, i, tone));
    rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    c.store(rgba.data(), w, 0, 0);
}

const std::vector<std::uint8_t>& Notice::picture_kept(const std::vector<std::string>& lines, int window_h, int& w, int& h) {
    Kept& k = kept_;
    const Layout L = layout(lines, window_h, style_);
    const int look = int(style_) * 8 + int(tone_);
    w = L.w;
    h = L.h;
    if (k.window_h == window_h && k.w == L.w && k.h == L.h && k.look == look && k.lines.size() == lines.size()) {
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
                    k.pic.strokes(l, L.strokes[i], line_colour(L, i, tone_));
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
    k.look = look;
    k.back = Canvas(w, h);
    back(k.back, L, tone_);
    k.pic = k.back;
    k.rows.assign(lines.size(), {0, 0});
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::vector<Line> l = line_strokes(lines, L, i);
        k.pic.strokes_rows(l, L.strokes[i], k.rows[i][0], k.rows[i][1]);
        k.pic.strokes(l, L.strokes[i], line_colour(L, i, tone_));
    }
    k.rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    k.pic.store(k.rgba.data(), w, 0, 0);
    return k.rgba;
}

bool Notice::box(int w, int h, float (&xywh)[4]) const {
    if (!visible() || w <= 0 || h <= 0) return false;
    const Layout L = layout(lines_, h, style_);
    if (style_ == Style::kBubble) {
        xywh[0] = float(w - L.w) - 1.5f * bubble_margin(h);
        xywh[1] = bubble_margin(h);
        xywh[2] = float(L.w);
        xywh[3] = float(L.h);
        return true;
    }
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
    float x0 = std::round(0.5f * float(w - tex_w_)), y0 = std::round(bottom ? 0.96f * float(h) - float(tex_h_) : 0.06f * float(h));
    if (style_ == Style::kBubble) {   // saveflow: the top right corner
        x0 = float(w - tex_w_) - 1.5f * bubble_margin(h);
        y0 = bubble_margin(h);
    }
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
