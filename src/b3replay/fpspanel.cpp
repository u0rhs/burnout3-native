// The FPS counter of the native window: src/b3replay/fpspanel.h. Built from solid rectangles only (a pixel font, a 1x1 white texture):
// no bitmap is drawn on the CPU and no texture is uploaded after the first.
#include "fpspanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "a11y.h"

namespace b3replay {

FpsView fps_view_from_setting(int v) { return v >= 1 && v <= 3 ? FpsView(v) : FpsView::kOff; }
FpsCorner fps_corner_from_setting(int v) { return v >= 1 && v <= 3 ? FpsCorner(v) : FpsCorner::kTopLeft; }

FpsView fps_view_next(FpsView v) {
    switch (v) {
    case FpsView::kMinimal: return FpsView::kStandard;
    case FpsView::kStandard: return FpsView::kDetailed;
    case FpsView::kDetailed: return FpsView::kOff;
    default: return FpsView::kMinimal;
    }
}

FpsState fps_state(float fps, float target_fps) {
    const float t = target_fps > 0 ? target_fps : 60.0f;
    if (fps >= 0.95f * t) return FpsState::kAtTarget;
    if (fps >= 0.75f * t) return FpsState::kBelow;
    return FpsState::kFarBelow;
}

FpsCorner fps_corner_in_photo(FpsCorner chosen, bool photo_ui_up) { return photo_ui_up ? FpsCorner::kTopLeft : chosen; }

// ---- the numbers ----

void FpsPanel::frame(std::uint64_t now) {
    if (!win_start_) win_start_ = now;
    if (last_ns_ && now > last_ns_) {
        hist_[head_] = {float(double(now - last_ns_) * 1e-6), now};
        head_ = (head_ + 1) % kHistory;
        count_ = std::min(count_ + 1, kHistory);
    }
    last_ns_ = now;
    ++win_frames_;
    const double e = double(now - win_start_) * 1e-9;
    if (e >= 0.5) {
        fps_ = float(win_frames_ / e);
        frame_ms_ = float(e * 1000.0 / win_frames_);
        win_frames_ = 0;
        win_start_ = now;
    }
}

FpsStats FpsPanel::stats(std::uint64_t now) const {
    FpsStats s;
    s.fps = fps_;
    s.frame_ms = frame_ms_;
    std::vector<float> ms;
    double total = 0;
    for (std::size_t i = 0; i < count_; ++i) {
        const Sample& sm = hist_[(head_ + kHistory - 1 - i) % kHistory];
        if (double(now - sm.ns) * 1e-9 > kWindowSeconds) break;
        ms.push_back(sm.ms);
        total += sm.ms;
        s.max_ms = std::max(s.max_ms, sm.ms);
    }
    s.frames = int(ms.size());
    if (ms.empty()) return s;
    s.avg = float(1000.0 * double(ms.size()) / total);
    const std::size_t worst = std::max<std::size_t>(1, ms.size() / 100);
    std::nth_element(ms.begin(), ms.begin() + std::ptrdiff_t(worst - 1), ms.end(), std::greater<float>());
    double w = 0;
    for (std::size_t i = 0; i < worst; ++i) w += ms[i];
    s.low1 = float(1000.0 * double(worst) / w);
    return s;
}

void FpsPanel::graph(std::uint64_t now, float (&ms)[kColumns]) const {
    std::fill(std::begin(ms), std::end(ms), 0.0f);
    for (std::size_t i = 0; i < count_; ++i) {
        const Sample& sm = hist_[(head_ + kHistory - 1 - i) % kHistory];
        const double age = double(now - sm.ns) * 1e-9;
        if (age >= kWindowSeconds) break;
        const int col = kColumns - 1 - std::min(kColumns - 1, int(age / kColumnSeconds));
        ms[col] = std::max(ms[col], sm.ms);
    }
}

float FpsPanel::graph_top_ms(float peak_ms, float target_ms) {
    const float t = target_ms > 0 ? target_ms : 1000.0f / 60.0f;
    const float want = std::ceil(std::max(peak_ms, 0.0f) * 1.1f / t) * t;
    return std::clamp(want, 2 * t, 4 * t);
}

float FpsPanel::spike_ms(float target_ms) {
    const float t = target_ms > 0 ? target_ms : 1000.0f / 60.0f;
    return std::max(2 * t, t + 8.0f);
}

std::vector<std::string> FpsPanel::detail_lines(const FpsInfo& i) {
    std::vector<std::string> out;
    char b[64];
    if (i.latency_ms >= 0) {
        std::snprintf(b, sizeof b, "LATENCY %.0f MS", double(i.latency_ms));
        out.emplace_back(b);
    }
    std::string mode;
    if (i.width > 0 && i.height > 0) mode = std::to_string(i.width) + "X" + std::to_string(i.height);
    if (i.msaa > 1) mode += (mode.empty() ? "" : "  ") + std::to_string(i.msaa) + "X MSAA";
    else if (i.msaa == 1) mode += (mode.empty() ? "" : "  ") + std::string("NO MSAA");
    if (!i.preset.empty()) mode += (mode.empty() ? "" : "  ") + i.preset;
    if (!mode.empty()) out.push_back(mode);
    if (!i.gpu.empty() || !i.backend.empty()) out.push_back(i.gpu.empty() ? i.backend : i.backend.empty() ? i.gpu : i.gpu + "  " + i.backend);
    return out;
}

// The number of lines detail_lines() gives, without building them
int FpsPanel::detail_row_count(const FpsInfo& i) {
    return (i.latency_ms >= 0 ? 1 : 0) + ((i.width > 0 && i.height > 0) || i.msaa >= 1 || !i.preset.empty() ? 1 : 0) + (!i.gpu.empty() || !i.backend.empty() ? 1 : 0);
}

namespace {

// ---- the look: photohud.cpp's plates, with the signal colours of a frame rate ----
using Col = std::array<float, 4>;
constexpr Col kAmber = {1.0f, 0.80f, 0.25f, 1.0f};
constexpr Col kBlue = {0.30f, 0.62f, 1.0f, 1.0f};
constexpr Col kWhite = {0.95f, 0.95f, 0.95f, 1.0f};
constexpr Col kSoft = {0.70f, 0.72f, 0.76f, 1.0f};
constexpr Col kFaint = {0.50f, 0.52f, 0.57f, 1.0f};
constexpr Col kPlate = {0.045f, 0.055f, 0.085f, 0.95f};   // near opaque, and a shade lighter than the race HUD's own dark plates
constexpr Col kEdge = {0.95f, 0.72f, 0.20f, 1.0f};
constexpr Col kCell = {1.0f, 1.0f, 1.0f, 0.14f};
constexpr Col kGood = {0.36f, 0.92f, 0.66f, 1.0f};    // at the target
constexpr Col kBad = {1.0f, 0.36f, 0.32f, 1.0f};      // far below it, and spikes
constexpr Col kWell = {1.0f, 1.0f, 1.0f, 0.07f};      // the graph's inset
constexpr Col kRule = {0.95f, 0.72f, 0.20f, 0.30f};

const Col& state_colour(FpsState s) { return s == FpsState::kAtTarget ? kGood : s == FpsState::kBelow ? kAmber : kBad; }

// The 5 x 7 pixel font (rows top to bottom, bit 4 = the left column): digits, A-Z and . - % : /
struct Glyph { char c; std::uint8_t rows[7]; };
constexpr Glyph kFont[] = {
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},     {'2', {14, 17, 1, 2, 4, 8, 31}},    {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 30, 1, 1, 17, 14}},  {'6', {6, 8, 16, 30, 17, 17, 14}},  {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 2, 12}},  {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {14, 4, 4, 4, 4, 4, 14}},     {'J', {7, 2, 2, 2, 2, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}}, {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}}, {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},      {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 27, 17}}, {'X', {17, 17, 10, 4, 10, 17, 17}},  {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'.', {0, 0, 0, 0, 0, 12, 12}},      {'-', {0, 0, 0, 31, 0, 0, 0}},       {'%', {25, 26, 2, 4, 8, 11, 19}},    {':', {0, 12, 12, 0, 12, 12, 0}},
    {'/', {1, 1, 2, 4, 8, 16, 16}},
};
const std::uint8_t* glyph_rows(char c) {
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    for (const Glyph& g : kFont)
        if (g.c == c) return g.rows;
    return nullptr;
}
// Width of `n` characters at dot size p: five dots and one apart
int text_w(std::size_t n, int p) { return n ? int(n) * 6 * p - p : 0; }

int R1(float v) { return std::max(1, int(std::lround(v))); }

// Measures in whole pixels at size unit `s`; every dot, gap and edge is a whole number of pixels
struct Sz {
    float s;
    bool compact = false;
    int pad() const { return R1((compact ? 6.0f : 9.0f) * s); }
    int chamfer() const { return R1((compact ? 5.0f : 7.0f) * s); }
    int edge() const { return std::max(2, R1(1.7f * s)); }
    int pn() const { return R1((compact ? 3.0f : 4.0f) * s); }        // the number's dot
    int num_h() const { return 7 * pn(); }
    int num_w() const { return text_w(3, pn()); }                      // (the plate keeps the room of three digits)
    int pf() const { return std::max(1, (2 * pn() + 2) / 5); }         // the word FPS
    int pl() const { return std::max(1, int(1.2f * s + 0.25f)); }      // labels and lines
    int pv() const { return R1(1.6f * s); }                            // the values under a label
    int gauge_w() const { return R1((compact ? 8.0f : 11.0f) * s); }
    int gauge_gap() const { return R1(1.5f * s); }
    int gap() const { return R1((compact ? 5.0f : 8.0f) * s); }        // gauge to number
    int gap2() const { return R1(4.0f * s); }                          // number to FPS
    int vgap() const { return R1((compact ? 4.0f : 8.0f) * s); }
    int cw() const { return R1(4.6f * s); }                            // a graph column
    int inset() const { return R1(3.0f * s); }
    int graph_h() const { return R1(46.0f * s); }
    int line_gap() const { return R1(3.0f * s); }
};

// What a plate holds, top to bottom (pixels)
struct Plan {
    int w = 0, h = 0;
    int head_h = 0;       // the number, gauge, FPS (and the 1 % low at the right in detailed)
    int low_y = 0;        // standard: the 1 % low line
    int graph_y = 0, graph_h = 0;
    int stats_y = 0, stats_h = 0;
    int lines_y = 0;      // the detailed lines' first row
};

Plan plan_for(const Sz& z, FpsView view, int rows) {
    Plan p;
    const int pad = z.pad();
    const int min_w = 2 * pad + z.gauge_w() + z.gap() + z.num_w() + z.gap2() + text_w(3, z.pf());
    if (view != FpsView::kDetailed) {
        p.head_h = z.num_h();
        p.h = 2 * pad + p.head_h;
        p.w = min_w;
        if (view == FpsView::kStandard) {
            p.low_y = pad + p.head_h + z.vgap();
            p.w = std::max(min_w, 2 * pad + text_w(10, z.pl()));
            p.h = p.low_y + 7 * z.pl() + pad;
        }
        return p;
    }
    p.w = 2 * pad + 2 * z.inset() + FpsPanel::kColumns * z.cw();
    p.head_h = std::max(z.num_h(), 7 * z.pl() + z.vgap() / 2 + 7 * z.pv());
    int y = pad + p.head_h + z.vgap();
    p.graph_y = y;
    p.graph_h = z.graph_h();
    y += p.graph_h + z.vgap();
    p.stats_y = y;
    p.stats_h = 7 * z.pl() + z.line_gap() + 7 * z.pv();
    y += p.stats_h;
    p.lines_y = y + z.vgap();
    if (rows > 0) y = p.lines_y + rows * (7 * z.pl() + z.line_gap()) - z.line_gap() + z.vgap() / 2;
    p.h = y + pad;
    return p;
}

struct Size { float w = 0, h = 0; };
Size plate_size(const Sz& z, FpsView view, int rows) {
    const Plan p = plan_for(z, view, rows);
    return {float(p.w), float(p.h)};
}

bool meets(const FpsPanel::Rect& a, const FpsPanel::Rect& b, float gap) {
    return a.x < b.x + b.w + gap && b.x - gap < a.x + a.w && a.y < b.y + b.h + gap && b.y - gap < a.y + a.h;
}

// ---- the quads ----
struct Builder {
    std::vector<b3gfx::Vertex>& vs;
    std::vector<std::uint32_t>& is;
    // The 2D stage draws into the half-intensity GS frame: RGB x 64, alpha x 128 = 1
    static std::uint32_t pack(const Col& c) {
        const auto f = [](float v, float k) { return std::uint32_t(std::clamp(v, 0.0f, 1.0f) * k + 0.5f); };
        return f(c[0], 64) | f(c[1], 64) << 8 | f(c[2], 64) << 16 | f(c[3], 128) << 24;
    }
    void quad(const float (&x)[4], const float (&y)[4], const Col& c) {
        const std::uint32_t base = std::uint32_t(vs.size()), col = pack(c);
        for (int k = 0; k < 4; ++k) {
            b3gfx::Vertex v{};
            v.pos[0] = x[k];
            v.pos[1] = y[k];
            v.uv[0] = v.uv[1] = 0.5f;
            v.rgba = col;
            vs.push_back(v);
        }
        for (std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) is.push_back(base + k);
    }
    void rect(int x, int y, int w, int h, const Col& c) {
        if (w <= 0 || h <= 0) return;
        quad({float(x), float(x + w), float(x + w), float(x)}, {float(y), float(y), float(y + h), float(y + h)}, c);
    }
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, const Col& c) { quad({x0, x1, x2, x2}, {y0, y1, y2, y2}, c); }
    // a line `t` wide between two points
    void line(float x0, float y0, float x1, float y1, float t, const Col& c) {
        const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        if (!(len > 0)) return;
        const float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
        quad({x0 + nx, x1 + nx, x1 - nx, x0 - nx}, {y0 + ny, y1 + ny, y1 - ny, y0 - ny}, c);
    }
    // text of dots p wide: each row's runs of lit dots are one rectangle; returns the width
    int text(std::string_view t, int x, int y, int p, const Col& c) {
        int cx = x;
        for (char ch : t) {
            if (const std::uint8_t* g = glyph_rows(ch))
                for (int r = 0; r < 7; ++r)
                    for (int k = 0; k < 5;) {
                        if (!(g[r] >> (4 - k) & 1)) { ++k; continue; }
                        int e = k;
                        while (e + 1 < 5 && g[r] >> (4 - e - 1) & 1) ++e;
                        rect(cx + k * p, y + r * p, (e - k + 1) * p, p, c);
                        k = e + 1;
                    }
            cx += 6 * p;
        }
        return text_w(t.size(), p);
    }
    int text_right(std::string_view t, int right, int y, int p, const Col& c) { return text(t, right - text_w(t.size(), p), y, p, c); }
};

std::string number_text(const FpsStats& st) {
    if (st.frames < 2 && st.fps <= 0) return "--";
    return std::to_string(int(std::lround(std::min(st.fps, 999.0f))));
}

}  // namespace

// ---- layout ----

// The race HUD's plates in a 448-line frame (the game's 640 x 448 layout): position and lap (with its big digit under the plate) from 4.9 % to 14 % of the height
// at the top, the boost bar and speed from 87.5 % to 95.5 % at the bottom, each grown about its own corner by HUD SIZE
// (the top: also under the second row of the time attack's stack (best time above, the current one below) and the takedown / score plates, which
// grow with HUD SIZE: 26 % of the height at 100 %)
float FpsPanel::game_hud_top(int h, int hud_size) { return std::round(float(h) * (0.049f + 0.21f * a11y::hud_size_scale(hud_size))); }
float FpsPanel::game_hud_bottom(int h, int hud_size) { return std::round(float(h) * (0.045f + 0.085f * a11y::hud_size_scale(hud_size) + 0.02f)); }

FpsPanel::Layout FpsPanel::layout(int w, int h, int hud_size, FpsView view, FpsCorner corner, int detail_rows, const Rect* avoid, bool game_hud) {
    Layout L;
    if (w <= 0 || h <= 0 || view == FpsView::kOff) return L;
    const float base = std::max(0.75f, float(h) / 720.0f) * a11y::hud_size_scale(hud_size);
    const float margin = std::round(0.045f * float(h));
    L.margin = margin;
    const bool right = corner == FpsCorner::kTopRight || corner == FpsCorner::kBottomRight;
    const bool bottom = corner == FpsCorner::kBottomLeft || corner == FpsCorner::kBottomRight;
    const float inset_top = game_hud && !bottom ? std::max(margin, game_hud_top(h, hud_size)) : margin;
    const float inset_bottom = game_hud && bottom ? std::max(margin, game_hud_bottom(h, hud_size)) : margin;
    L.top = inset_top;
    L.win_h = float(h);
    L.bottom = inset_bottom;
    const float avail_w = float(w) - 2 * margin;
    // a small plate on a big window: minimal and standard at most about 8 % of its width, detailed about 12 % (not under a 720-line size)
    const bool compact = view != FpsView::kDetailed;
    const float cap = (compact ? 0.08f : 0.12f) * float(w), lo = std::min(base, 1.0f);   // (not under a 720-line size)
    float start = base;
    while (start > lo && plate_size(Sz{start, compact}, view, detail_rows).w > cap) start *= 0.97f;
    start = std::max(start, std::min(base, lo));
    for (float s = start; s >= 0.3f; s *= 0.92f) {
        const Sz z{s, compact};
        const Size sz = plate_size(z, view, detail_rows);
        if (sz.w > avail_w || sz.h > float(h) - inset_top - inset_bottom) continue;
        Rect r{right ? float(w) - margin - sz.w : margin, bottom ? float(h) - inset_bottom - sz.h : inset_top, sz.w, sz.h};
        const float gap = 6 * s;
        if (avoid && !avoid->empty() && meets(r, *avoid, gap)) {
            const float moved = bottom ? avoid->y - gap - sz.h : avoid->y + avoid->h + gap;
            if (moved >= inset_top && moved + sz.h <= float(h) - inset_bottom) r.y = std::round(moved);   // (no room: it stays where it is)
        }
        L.plate = r;
        L.scale = s;
        return L;
    }
    return L;
}

// ---- the quads of a plate ----

void FpsPanel::build(const Layout& l, FpsView view, const FpsStats& st, const float (&graph_ms)[kColumns], const FpsInfo& info, std::vector<b3gfx::Vertex>& vs,
                     std::vector<std::uint32_t>& is) {
    vs.clear();
    is.clear();
    if (l.plate.empty() || view == FpsView::kOff) return;
    const bool compact = view != FpsView::kDetailed;
    const Sz z{l.scale, compact};
    const Plan P = plan_for(z, view, view == FpsView::kDetailed ? detail_row_count(info) : 0);
    const int w = int(l.plate.w), h = int(l.plate.h), pad = z.pad(), right = w - pad;
    Builder b{vs, is};

    // the plate: a body with two cut corners (boxes and two triangles, none overlapping), its edge, the accent bar
    const int k = z.chamfer(), e = z.edge();
    b.rect(k, 0, w - 2 * k, h, kPlate);
    b.rect(0, k, k, h - k, kPlate);
    b.rect(w - k, 0, k, h - k, kPlate);
    b.tri(float(k), 0, float(k), float(k), 0, float(k), kPlate);
    b.tri(float(w - k), float(h - k), float(w), float(h - k), float(w - k), float(h), kPlate);
    b.rect(k, 0, w - k, e, kEdge);
    b.rect(w - e, 0, e, h - k, kEdge);
    b.rect(0, h - e, w - k, e, kEdge);
    b.rect(0, k, e, h - k, kEdge);
    b.line(float(k), 0.5f * float(e), 0.5f * float(e), float(k), float(e), kEdge);
    b.line(float(w), float(h - k) + 0.5f * float(e), float(w - k) + 0.5f * float(e), float(h), float(e), kEdge);
    {
        const int bar = std::min(w * 3 / 10, R1(44.0f * z.s)), t = std::max(2, R1(2.6f * z.s));
        b.rect(k + e, e, bar, t, kAmber);
        b.rect(k + e + bar + R1(2.0f * z.s), e, R1(10.0f * z.s), t, kBlue);
    }

    // the head: the signal gauge, the number, FPS
    const bool none = st.frames < 2 && st.fps <= 0;
    const FpsState state = fps_state(st.fps, info.target_fps);
    const Col& col = none ? kFaint : state_colour(state);
    const int lit = none ? 0 : state == FpsState::kAtTarget ? 3 : state == FpsState::kBelow ? 2 : 1;
    const int gy = pad, gh = z.num_h(), gg = z.gauge_gap(), ch = std::max(1, (gh - 2 * gg) / 3), skew = std::max(1, z.gauge_w() / 4), cw = z.gauge_w() - skew;
    for (int i = 0; i < 3; ++i) {   // three slanted cells, the bottom one first
        const float cy = float(gy + gh - (i + 1) * ch - i * gg);
        b.quad({float(pad + skew), float(pad + skew + cw), float(pad + cw), float(pad)}, {cy, cy, cy + float(ch), cy + float(ch)}, i < lit ? col : kCell);
    }
    const int nx = pad + z.gauge_w() + z.gap();
    const std::string num = number_text(st);
    const int nw = b.text(num, nx, gy, z.pn(), col);
    b.text("FPS", nx + nw + z.gap2(), gy + gh - 7 * z.pf(), z.pf(), kAmber);

    char buf[32];
    if (view == FpsView::kStandard) {   // the 1 % low under the number
        if (st.frames >= 2) std::snprintf(buf, sizeof buf, "%d", int(std::lround(std::min(st.low1, 999.0f))));
        else std::snprintf(buf, sizeof buf, "--");
        b.text("1% LOW", pad, P.low_y, z.pl(), kSoft);
        b.text(buf, pad + 7 * 6 * z.pl(), P.low_y, z.pl(), st.frames >= 2 ? state_colour(fps_state(st.low1, info.target_fps)) : kFaint);
    }
    if (view != FpsView::kDetailed) return;

    {   // the 1 % low at the right of the head
        if (st.frames >= 2) std::snprintf(buf, sizeof buf, "%d", int(std::lround(std::min(st.low1, 999.0f))));
        else std::snprintf(buf, sizeof buf, "--");
        b.text_right("1% LOW", right, pad, z.pl(), kSoft);
        b.text_right(buf, right, pad + 7 * z.pl() + z.vgap() / 2, z.pv(), st.frames >= 2 ? state_colour(fps_state(st.low1, info.target_fps)) : kFaint);
    }

    // the graph: frame time in ms, a column each, up to its target line
    const float target_ms = 1000.0f / (info.target_fps > 0 ? info.target_fps : 60.0f);
    float peak = 0;
    for (float v : graph_ms) peak = std::max(peak, v);
    const float top_ms = graph_top_ms(peak, target_ms), spike = spike_ms(target_ms);
    const int gx = pad, gyy = P.graph_y, gw = w - 2 * pad, ghh = P.graph_h, inset = z.inset(), ed = std::max(1, int(z.s * 1.2f));
    b.rect(gx, gyy, gw, ghh, kWell);
    const int bx = gx + inset, by = gyy + inset / 2 + 1, bh = ghh - (by - gyy) - inset / 2 - 1, base_y = by + bh, colw = z.cw();
    const auto yof = [&](float ms) { return base_y - std::clamp(int(std::lround(std::min(ms / top_ms, 1.0f) * float(bh))), 1, bh); };
    const auto klass = [&](float ms) { return ms > spike ? 2 : ms > 1.05f * target_ms ? 1 : 0; };
    const Col* const tint[3] = {&kGood, &kAmber, &kBad};
    const float fill_a[3] = {0.26f, 0.32f, 0.55f};
    for (int i = 0; i < kColumns; ++i) {
        if (!(graph_ms[i] > 0)) continue;
        const int c = klass(graph_ms[i]), y = yof(graph_ms[i]), x = bx + i * colw;
        const Col fill = {(*tint[c])[0], (*tint[c])[1], (*tint[c])[2], fill_a[c]};
        b.rect(x, y, colw, base_y - y, fill);
        b.rect(x, y, colw, ed, *tint[c]);
        if (i > 0 && graph_ms[i - 1] > 0) {   // the riser between columns, in the colour of the higher one
            const int y0 = yof(graph_ms[i - 1]);
            b.rect(x, std::min(y, y0), ed, std::abs(y - y0) + ed, *tint[klass(std::max(graph_ms[i], graph_ms[i - 1]))]);
        }
    }
    const int ty = yof(target_ms);
    {   // the target line over them, dashed, amber
        const int on = R1(5.0f * z.s), off = R1(3.5f * z.s), x1 = bx + kColumns * colw;
        const Col dash = {kAmber[0], kAmber[1], kAmber[2], 0.9f};
        for (int x = bx; x < x1; x += on + off) b.rect(x, ty, std::min(on, x1 - x), ed, dash);
    }
    b.text("FRAME TIME 8 S", bx + 2 * z.pl(), by + z.pl(), z.pl(), kFaint);
    {
        std::snprintf(buf, sizeof buf, "%.1f MS", double(target_ms));
        const int lh = 7 * z.pl(), below = ty + 2 * z.pl() + ed, above = ty - lh - 2 * z.pl();
        b.text_right(buf, bx + kColumns * colw - z.pl(), below + lh <= base_y ? below : above, z.pl(), kAmber);
    }

    // detailed: the average, the current and the worst frame time, then the settings lines
    b.rect(pad, P.graph_y + P.graph_h + z.vgap() / 2 - ed / 2, w - 2 * pad, ed, kRule);
    {
        char a[24], f[24], m[24];
        if (st.frames >= 2) {
            std::snprintf(a, sizeof a, "%.1f", double(st.avg));
            std::snprintf(f, sizeof f, "%.1f", double(st.frame_ms));
            std::snprintf(m, sizeof m, "%.1f", double(st.max_ms));
        } else {
            std::snprintf(a, sizeof a, "--");
            std::snprintf(f, sizeof f, "--");
            std::snprintf(m, sizeof m, "--");
        }
        struct Cell { const char* label; const char* value; const char* unit; };
        const Cell cells[3] = {{"AVG FPS", a, ""}, {"FRAME MS", f, ""}, {"WORST MS", m, ""}};
        const int cwid = (w - 2 * pad) / 3;
        for (int i = 0; i < 3; ++i) {
            const int x = pad + i * cwid, vy = P.stats_y + 7 * z.pl() + z.line_gap();
            b.text(cells[i].label, x, P.stats_y, z.pl(), kSoft);
            b.text(cells[i].value, x, vy, z.pv(), kWhite);
        }
    }
    const std::vector<std::string> lines = detail_lines(info);
    if (!lines.empty()) {
        b.rect(pad, P.lines_y - z.vgap() / 2 - ed / 2, w - 2 * pad, ed, kRule);
        int y = P.lines_y;
        for (const std::string& ln : lines) {
            std::string t = ln;
            while (t.size() > 4 && text_w(t.size(), z.pl()) > right - pad) t.pop_back();   // a long GPU name
            b.text(t, pad, y, z.pl(), kSoft);
            y += 7 * z.pl() + z.line_gap();
        }
    }
    (void)h;
}

// ---- drawing ----

// The plate's rectangle for this frame: the natural place, moved by an eased shift toward the wanted one (a notice in its way).
// The shift starts again from nothing when the layout it was measured against is gone (a corner, the view, the window's size,
// the scale, the race HUD), and the result stays inside the safe area.
FpsPanel::Rect FpsPanel::place(const Layout& natural, const Layout& wanted, FpsView view, std::uint64_t now) {
    if (!(natural == shift_layout_) || view != shift_view_) {
        shift_ = 0;
        shift_layout_ = natural;
        shift_view_ = view;
        last_draw_ns_ = 0;
    }
    const float dt = last_draw_ns_ && now > last_draw_ns_ ? float(double(now - last_draw_ns_) * 1e-9) : 0.0f;
    last_draw_ns_ = now;
    const float want_shift = wanted.plate.y - natural.plate.y;
    const float step = dt / 0.15f * std::max(1.0f, std::fabs(want_shift - shift_));
    shift_ = shift_ < want_shift ? std::min(want_shift, shift_ + step) : std::max(want_shift, shift_ - step);
    Rect r = natural.plate;
    r.y = std::round(r.y + shift_);
    const float lo = natural.top, hi = std::max(lo, natural.win_h - natural.bottom - r.h);
    r.y = std::clamp(r.y, lo, hi);
    return r;
}

void FpsPanel::draw_list(b3gfx::Device& dev, int w, int h, std::uint64_t now, FpsView view, FpsCorner corner, int hud_size, const FpsInfo& info,
                         const Rect* avoid, bool game_hud, std::vector<b3gfx::DrawItem>& out) {
    if (w <= 0 || h <= 0 || view == FpsView::kOff) return;
    // A track load (View::clear -> clear_resources) released every buffer and texture and restarted the ids from 0: ours then
    // named the game's own (the plate drew a game texture, its quads went into a game buffer). Make them again.
    if (dev.resource_epoch() != epoch_) {
        epoch_ = dev.resource_epoch();
        vb_ = ib_ = b3gfx::kNone;
        white_ = b3gfx::kNone;
        built_.valid = false;
    }
    // per frame: the layout from a cache (it only changes with the window, the view, the corner, HUD SIZE, the race HUD or the lines of the detailed view),
    // the eased place, one draw item; the quads are built and uploaded when what they show changes, at most twice a second
    const int rows = view == FpsView::kDetailed ? detail_row_count(info) : 0;
    const NatKey key{w, h, hud_size, view, corner, rows, game_hud};
    if (!nat_valid_ || !(key == nat_key_)) {
        nat_ = layout(w, h, hud_size, view, corner, rows, nullptr, game_hud);
        nat_key_ = key;
        nat_valid_ = true;
    }
    if (nat_.plate.empty()) return;
    const Layout wanted = avoid && !avoid->empty() ? layout(w, h, hud_size, view, corner, rows, avoid, game_hud) : nat_;
    const Rect placed = place(nat_, wanted, view, now);
    const bool detailed = view == FpsView::kDetailed;
    const int lat = int(std::lround(info.latency_ms));   // (the line shows whole ms)
    const bool stale = !built_.valid || !(built_.layout == nat_) || built_.view != view || double(now - built_ns_) * 1e-9 >= 0.5 ||
                       built_.info.target_fps != info.target_fps ||
                       (detailed && (built_lat_ != lat || built_.info.msaa != info.msaa || built_.info.width != info.width || built_.info.height != info.height ||
                                     built_.info.preset != info.preset || built_.info.gpu != info.gpu || built_.info.backend != info.backend));
    if (stale || white_ == b3gfx::kNone) {
        if (white_ == b3gfx::kNone) {
            const std::uint8_t px[4] = {255, 255, 255, 255};
            white_ = dev.create_texture({b3gfx::TextureLevel{1, 1, px}});
        }
        const FpsStats st = stats(now);
        float g[kColumns];
        graph(now, g);
        build(nat_, view, st, g, info, vs_, is_);
        if (!vs_.empty()) {
            dev.update_vertex_buffer(vb_, vs_.data(), vs_.size());
            dev.update_index_buffer(ib_, is_.data(), is_.size());
        }
        built_ = {nat_, view, info, st, true};
        built_lat_ = lat;
        built_ns_ = now;
    }
    if (is_.empty()) return;
    b3gfx::DrawItem it;
    it.overlay = true;
    it.shading = b3gfx::Shading::kTrack;
    it.track_type = 0;
    it.textured = true;
    it.texture = white_;
    it.gs_alpha = 0x44;
    it.overlay_blend = true;
    it.point_sample = true;
    // plate pixels -> clip space (top left origin), the plate's place in the window in the matrix: moving it re-uploads nothing
    const float m[16] = {2.0f / float(w), 0, 0, 0, 0, -2.0f / float(h), 0, 0, 0, 0, 1, 0, -1 + 2.0f * placed.x / float(w), 1 - 2.0f * placed.y / float(h), 0.5f, 1};
    std::memcpy(it.model, m, sizeof m);
    it.vertices = vb_;
    it.indices = ib_;
    it.first_index = 0;
    it.index_count = std::uint32_t(is_.size());
    out.push_back(it);
}

}  // namespace b3replay
