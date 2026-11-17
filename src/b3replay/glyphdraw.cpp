// The prompts' vector painter and stroke font (mac5): src/b3replay/glyphdraw.h.
#include "glyphdraw.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace b3replay::glyph {

namespace {

constexpr float kPi = 3.14159265358979f;

// The stroke font: each letter's polylines in cap-height units (x right, y down, the cap from 0 to 1) and its width.
struct Letter { std::vector<Line> lines; float width = 0; };

Line ellipse(float cx, float cy, float rx, float ry) {
    Line l;
    arc(l, cx, cy, rx, ry, 0, 360);
    return l;
}

Letter letter(char c) {
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    Letter g;
    auto line = [&](std::initializer_list<Pt> p) { g.lines.emplace_back(p); };
    switch (c) {
    case 'A': g.width = 0.68f; line({{0, 1}, {0.34f, 0}, {0.68f, 1}}); line({{0.12f, 0.66f}, {0.56f, 0.66f}}); break;
    case 'B': {
        g.width = 0.56f;
        Line l{{0, 0.5f}, {0, 0}, {0.27f, 0}};
        arc(l, 0.27f, 0.25f, 0.25f, 0.25f, -90, 90);
        l.push_back({0, 0.5f});
        l.push_back({0.29f, 0.5f});
        arc(l, 0.29f, 0.75f, 0.27f, 0.25f, -90, 90);
        l.push_back({0, 1});
        l.push_back({0, 0.5f});
        g.lines.push_back(l);
        break;
    }
    case 'C': { g.width = 0.6f; Line l; arc(l, 0.33f, 0.5f, 0.33f, 0.5f, -45, -315); g.lines.push_back(l); break; }
    case 'D': {
        g.width = 0.6f;
        Line l{{0, 1}, {0, 0}, {0.22f, 0}};
        arc(l, 0.22f, 0.5f, 0.38f, 0.5f, -90, 90);
        l.push_back({0, 1});
        g.lines.push_back(l);
        break;
    }
    case 'E': g.width = 0.52f; line({{0.52f, 0}, {0, 0}, {0, 1}, {0.52f, 1}}); line({{0, 0.5f}, {0.44f, 0.5f}}); break;
    case 'F': g.width = 0.5f; line({{0.5f, 0}, {0, 0}, {0, 1}}); line({{0, 0.48f}, {0.42f, 0.48f}}); break;
    case 'G': {
        g.width = 0.64f;
        Line l;
        arc(l, 0.33f, 0.5f, 0.33f, 0.5f, -45, -360);
        l.push_back({0.66f, 0.56f});
        l.push_back({0.38f, 0.56f});
        g.lines.push_back(l);
        break;
    }
    case 'H': g.width = 0.6f; line({{0, 0}, {0, 1}}); line({{0.6f, 0}, {0.6f, 1}}); line({{0, 0.5f}, {0.6f, 0.5f}}); break;
    case 'I': g.width = 0; line({{0, 0}, {0, 1}}); break;
    case 'J': {
        g.width = 0.48f;
        Line l{{0.48f, 0}, {0.48f, 0.72f}};
        arc(l, 0.24f, 0.72f, 0.24f, 0.28f, 0, 180);
        g.lines.push_back(l);
        break;
    }
    case 'K': g.width = 0.6f; line({{0, 0}, {0, 1}}); line({{0.6f, 0}, {0, 0.62f}}); line({{0.2f, 0.45f}, {0.62f, 1}}); break;
    case 'L': g.width = 0.48f; line({{0, 0}, {0, 1}, {0.48f, 1}}); break;
    case 'M': g.width = 0.76f; line({{0, 1}, {0, 0}, {0.38f, 0.62f}, {0.76f, 0}, {0.76f, 1}}); break;
    case 'N': g.width = 0.62f; line({{0, 1}, {0, 0}, {0.62f, 1}, {0.62f, 0}}); break;
    case 'O': g.width = 0.7f; g.lines.push_back(ellipse(0.35f, 0.5f, 0.35f, 0.5f)); break;
    case 'Q': g.width = 0.7f; g.lines.push_back(ellipse(0.35f, 0.5f, 0.35f, 0.5f)); line({{0.44f, 0.72f}, {0.74f, 1.02f}}); break;
    case 'P':
    case 'R': {
        g.width = c == 'P' ? 0.56f : 0.6f;
        Line l{{0, 1}, {0, 0}, {0.29f, 0}};
        arc(l, 0.29f, 0.27f, 0.27f, 0.27f, -90, 90);
        l.push_back({0, 0.54f});
        g.lines.push_back(l);
        if (c == 'R') line({{0.26f, 0.54f}, {0.6f, 1}});
        break;
    }
    case 'S': {   // two bowls, the top one drawn from its upper right round to the middle, the bottom one on from there
        g.width = 0.58f;
        Line l;
        arc(l, 0.29f, 0.255f, 0.27f, 0.245f, -30, -270);
        arc(l, 0.29f, 0.745f, 0.29f, 0.255f, -90, 150);
        g.lines.push_back(l);
        break;
    }
    case 'T': g.width = 0.6f; line({{0, 0}, {0.6f, 0}}); line({{0.3f, 0}, {0.3f, 1}}); break;
    case 'U': {
        g.width = 0.6f;
        Line l{{0, 0}, {0, 0.7f}};
        arc(l, 0.3f, 0.7f, 0.3f, 0.3f, 180, 0);
        l.push_back({0.6f, 0});
        g.lines.push_back(l);
        break;
    }
    case 'V': g.width = 0.66f; line({{0, 0}, {0.33f, 1}, {0.66f, 0}}); break;
    case 'W': g.width = 0.9f; line({{0, 0}, {0.22f, 1}, {0.45f, 0.3f}, {0.68f, 1}, {0.9f, 0}}); break;
    case 'X': g.width = 0.62f; line({{0, 0}, {0.62f, 1}}); line({{0, 1}, {0.62f, 0}}); break;
    case 'Y': g.width = 0.64f; line({{0, 0}, {0.32f, 0.5f}, {0.64f, 0}}); line({{0.32f, 0.5f}, {0.32f, 1}}); break;
    case 'Z': g.width = 0.58f; line({{0, 0}, {0.58f, 0}, {0, 1}, {0.58f, 1}}); break;
    case '0': g.width = 0.58f; g.lines.push_back(ellipse(0.29f, 0.5f, 0.29f, 0.5f)); break;
    case '1': g.width = 0.3f; line({{0, 0.2f}, {0.3f, 0}, {0.3f, 1}}); break;
    case '2': {
        g.width = 0.58f;
        Line l;
        arc(l, 0.29f, 0.28f, 0.28f, 0.28f, -165, 30);
        l.push_back({0, 1});
        l.push_back({0.58f, 1});
        g.lines.push_back(l);
        break;
    }
    case '3': {
        g.width = 0.56f;
        Line l;
        arc(l, 0.27f, 0.25f, 0.26f, 0.25f, -160, 90);
        arc(l, 0.27f, 0.75f, 0.29f, 0.25f, -90, 160);
        g.lines.push_back(l);
        break;
    }
    case '4': g.width = 0.62f; line({{0.46f, 1}, {0.46f, 0}, {0, 0.68f}, {0.62f, 0.68f}}); break;
    case '5': {
        g.width = 0.56f;
        Line l{{0.52f, 0}, {0.06f, 0}, {0.02f, 0.46f}};
        arc(l, 0.27f, 0.68f, 0.29f, 0.32f, -125, 145);
        g.lines.push_back(l);
        break;
    }
    case '6': {
        g.width = 0.58f;
        Line l;
        arc(l, 0.31f, 0.5f, 0.31f, 0.5f, -60, -180);
        g.lines.push_back(l);
        g.lines.push_back(ellipse(0.29f, 0.7f, 0.29f, 0.3f));
        break;
    }
    case '7': g.width = 0.56f; line({{0, 0}, {0.56f, 0}, {0.18f, 1}}); break;
    case '8': g.width = 0.58f; g.lines.push_back(ellipse(0.29f, 0.25f, 0.25f, 0.25f)); g.lines.push_back(ellipse(0.29f, 0.74f, 0.29f, 0.26f)); break;
    case '9': {
        g.width = 0.58f;
        g.lines.push_back(ellipse(0.29f, 0.3f, 0.29f, 0.3f));
        Line l;
        arc(l, 0.27f, 0.5f, 0.31f, 0.5f, 0, 120);
        g.lines.push_back(l);
        break;
    }
    case '+': g.width = 0.6f; line({{0, 0.5f}, {0.6f, 0.5f}}); line({{0.3f, 0.2f}, {0.3f, 0.8f}}); break;
    case '-': g.width = 0.5f; line({{0, 0.5f}, {0.5f, 0.5f}}); break;
    case '/': g.width = 0.5f; line({{0, 1}, {0.5f, 0}}); break;
    case '.': g.width = 0; line({{0, 1}, {0, 1}}); break;
    case ':': g.width = 0; line({{0, 0.35f}, {0, 0.35f}}); line({{0, 0.95f}, {0, 0.95f}}); break;
    case ' ': g.width = 0.35f; break;
    // tunenav1: the marks the tuning console's names and pages use (the game's variable names: "Mass (Kg)", "[2]")
    case '(': { g.width = 0.22f; Line l; arc(l, 0.42f, 0.5f, 0.42f, 0.62f, -125, -235); g.lines.push_back(l); break; }
    case ')': { g.width = 0.22f; Line l; arc(l, -0.2f, 0.5f, 0.42f, 0.62f, -55, 55); g.lines.push_back(l); break; }
    case '[': g.width = 0.22f; line({{0.22f, -0.08f}, {0, -0.08f}, {0, 1.08f}, {0.22f, 1.08f}}); break;
    case ']': g.width = 0.22f; line({{0, -0.08f}, {0.22f, -0.08f}, {0.22f, 1.08f}, {0, 1.08f}}); break;
    case '<': g.width = 0.5f; line({{0.5f, 0.15f}, {0, 0.5f}, {0.5f, 0.85f}}); break;
    case '>': g.width = 0.5f; line({{0, 0.15f}, {0.5f, 0.5f}, {0, 0.85f}}); break;
    case '|': g.width = 0; line({{0, -0.1f}, {0, 1.1f}}); break;
    case '*': g.width = 0.5f; line({{0.25f, 0.1f}, {0.25f, 0.6f}}); line({{0, 0.22f}, {0.5f, 0.48f}}); line({{0, 0.48f}, {0.5f, 0.22f}}); break;
    case ',': g.width = 0.1f; line({{0.1f, 0.9f}, {0, 1.15f}}); break;
    case '\'': g.width = 0; line({{0, 0}, {0, 0.3f}}); break;
    case '=': g.width = 0.5f; line({{0, 0.36f}, {0.5f, 0.36f}}); line({{0, 0.66f}, {0.5f, 0.66f}}); break;
    case '_': g.width = 0.5f; line({{0, 1}, {0.5f, 1}}); break;
    case '%': {
        g.width = 0.62f;
        line({{0, 1}, {0.62f, 0}});
        g.lines.push_back(ellipse(0.12f, 0.15f, 0.12f, 0.15f));
        g.lines.push_back(ellipse(0.5f, 0.85f, 0.12f, 0.15f));
        break;
    }
    case '&': g.width = 0.6f; line({{0.6f, 1}, {0.05f, 0.42f}, {0.05f, 0.15f}, {0.3f, 0}, {0.5f, 0.15f}, {0.45f, 0.35f}, {0, 0.7f}, {0.1f, 1}, {0.35f, 1}, {0.6f, 0.6f}}); break;
    case '#': g.width = 0.6f; line({{0.2f, 0}, {0.1f, 1}}); line({{0.5f, 0}, {0.4f, 1}}); line({{0, 0.33f}, {0.6f, 0.33f}}); line({{0, 0.67f}, {0.6f, 0.67f}}); break;
    case '!': g.width = 0; line({{0, 0}, {0, 0.68f}}); line({{0, 0.98f}, {0, 0.98f}}); break;
    case '?': {
        g.width = 0.5f;
        Line l;
        arc(l, 0.25f, 0.27f, 0.25f, 0.25f, -165, 60);
        l.push_back({0.25f, 0.68f});
        g.lines.push_back(l);
        line({{0.25f, 0.98f}, {0.25f, 0.98f}});
        break;
    }
    default: g.width = 0.5f; line({{0, 0}, {0.5f, 0}, {0.5f, 1}, {0, 1}, {0, 0}}); break;   // unknown: a box
    }
    return g;
}

}  // namespace

void arc(Line& l, float cx, float cy, float rx, float ry, float a0, float a1) {
    const int n = std::max(4, int(std::ceil(std::fabs(a1 - a0) / 7.5f)));
    for (int k = 0; k <= n; ++k) {
        const float a = (a0 + (a1 - a0) * float(k) / float(n)) * kPi / 180.0f;
        l.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
    }
}

Canvas::Canvas(int w_, int h_) : w(w_), h(h_), px(std::size_t(std::max(w_, 0)) * std::size_t(std::max(h_, 0)), {0, 0, 0, 0}) {}

namespace {

// Composites a shape given by its signed distance `sdf` (pixels, negative inside) over its bounds
template <class Sdf>
void fill(Canvas& c, float x0, float y0, float x1, float y1, const float (&rgba)[4], const Sdf& sdf) {
    const int ix0 = std::max(0, int(std::floor(x0)) - 1), iy0 = std::max(0, int(std::floor(y0)) - 1);
    const int ix1 = std::min(c.w, int(std::ceil(x1)) + 1), iy1 = std::min(c.h, int(std::ceil(y1)) + 1);
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            const float cov = std::clamp(0.5f - sdf(float(x) + 0.5f, float(y) + 0.5f), 0.0f, 1.0f);
            if (cov <= 0) continue;
            const float a = rgba[3] * cov;
            auto& d = c.px[std::size_t(y) * std::size_t(c.w) + std::size_t(x)];
            for (int k = 0; k < 3; ++k) d[k] = rgba[k] * a + d[k] * (1 - a);
            d[3] = a + d[3] * (1 - a);
        }
}

float seg_dist(float x, float y, Pt a, Pt b) {
    const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    const float u = len2 > 0 ? std::clamp(((x - a.x) * dx + (y - a.y) * dy) / len2, 0.0f, 1.0f) : 0.0f;
    return std::hypot(x - (a.x + u * dx), y - (a.y + u * dy));
}

float rbox_sdf(float x, float y, float cx, float cy, float hx, float hy, float r) {
    const float qx = std::fabs(x - cx) - (hx - r), qy = std::fabs(y - cy) - (hy - r);
    // hypot(a, 0) is |a| exactly (C Annex F): only the corners need it (devnav1: the same values, a box's area faster)
    const float outside = qx > 0 && qy > 0 ? std::hypot(qx, qy) : qx > 0 ? qx : qy > 0 ? qy : 0.0f;
    return outside + std::min(std::max(qx, qy), 0.0f) - r;
}

// std::lround of a value in [0, 255] (exact in double: the float + 0.5 rounds nothing), without the library call
std::uint8_t round_byte(float v) { return std::uint8_t(int(std::floor(double(v) + 0.5))); }

}  // namespace

void Canvas::box(float cx, float cy, float hx, float hy, float r, const float (&rgba)[4]) {
    fill(*this, cx - hx, cy - hy, cx + hx, cy + hy, rgba, [=](float x, float y) { return rbox_sdf(x, y, cx, cy, hx, hy, r); });
}

void Canvas::box_outline(float cx, float cy, float hx, float hy, float r, float t, const float (&rgba)[4]) {
    fill(*this, cx - hx - t, cy - hy - t, cx + hx + t, cy + hy + t, rgba,
         [=](float x, float y) { return std::fabs(rbox_sdf(x, y, cx, cy, hx, hy, r)) - 0.5f * t; });
}

void Canvas::disc(float cx, float cy, float r, const float (&rgba)[4]) {
    fill(*this, cx - r, cy - r, cx + r, cy + r, rgba, [=](float x, float y) { return std::hypot(x - cx, y - cy) - r; });
}

void Canvas::ring(float cx, float cy, float r, float t, const float (&rgba)[4]) {
    fill(*this, cx - r - t, cy - r - t, cx + r + t, cy + r + t, rgba,
         [=](float x, float y) { return std::fabs(std::hypot(x - cx, y - cy) - r) - 0.5f * t; });
}

void Canvas::polygon(const std::vector<Pt>& p, const float (&rgba)[4]) {
    if (p.size() < 3) return;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (const Pt& q : p) { x0 = std::min(x0, q.x); y0 = std::min(y0, q.y); x1 = std::max(x1, q.x); y1 = std::max(y1, q.y); }
    fill(*this, x0, y0, x1, y1, rgba, [&](float x, float y) {
        float best = 1e30f;
        bool inside = false;
        for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
            best = std::min(best, seg_dist(x, y, p[j], p[i]));
            if ((p[i].y > y) != (p[j].y > y) && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) inside = !inside;
        }
        return inside ? -best : best;
    });
}

void Canvas::strokes(const std::vector<Line>& lines, float t, const float (&rgba)[4]) {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (const Line& l : lines)
        for (const Pt& p : l) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    if (x1 < x0) return;
    // devnav1 (2026-11-13): the nearest segment per pixel, each segment visiting only the pixels it can cover (a
    // distance below 0.5 t + 0.5). Before, every pixel of the strokes' whole box measured every segment: a line of
    // text cost its box's area x its segment count (~1.2 s for photo mode's help box at a 1800-pixel window, ~1 s for
    // the F9 tuning overlay; both rebuilt as their text changed). The same distances, the same minimum where any
    // segment reaches: the same picture to the bit.
    const float bx0 = x0 - t, by0 = y0 - t, bx1 = x1 + t, by1 = y1 + t;   // fill()'s bounds below
    const int ix0 = std::max(0, int(std::floor(bx0)) - 1), iy0 = std::max(0, int(std::floor(by0)) - 1);
    const int ix1 = std::min(w, int(std::ceil(bx1)) + 1), iy1 = std::min(h, int(std::ceil(by1)) + 1);
    if (ix1 <= ix0 || iy1 <= iy0) return;
    const int bw = ix1 - ix0;
    std::vector<float> best(std::size_t(bw) * std::size_t(iy1 - iy0), 1e30f);
    const float reach = 0.5f * t + 1.0f;
    for (const Line& l : lines)
        for (std::size_t k = 0; k < l.size(); ++k) {
            const Pt a = l[k], b = k + 1 < l.size() ? l[k + 1] : l[k];
            // pixel centres (x + 0.5) within `reach` of the segment's box
            const int sx0 = std::max(ix0, int(std::floor(std::min(a.x, b.x) - reach - 0.5f)));
            const int sx1 = std::min(ix1, int(std::ceil(std::max(a.x, b.x) + reach - 0.5f)) + 1);
            const int sy0 = std::max(iy0, int(std::floor(std::min(a.y, b.y) - reach - 0.5f)));
            const int sy1 = std::min(iy1, int(std::ceil(std::max(a.y, b.y) + reach - 0.5f)) + 1);
            for (int y = sy0; y < sy1; ++y) {
                float* row = best.data() + std::size_t(y - iy0) * std::size_t(bw);
                for (int x = sx0; x < sx1; ++x) row[x - ix0] = std::min(row[x - ix0], seg_dist(float(x) + 0.5f, float(y) + 0.5f, a, b));
            }
        }
    fill(*this, bx0, by0, bx1, by1, rgba, [&](float x, float y) {
        const int px_x = int(x - 0.5f), px_y = int(y - 0.5f);   // fill() passes the pixel's centre
        return best[std::size_t(px_y - iy0) * std::size_t(bw) + std::size_t(px_x - ix0)] - 0.5f * t;
    });
}

void Canvas::strokes_rows(const std::vector<Line>& lines, float t, int& row0, int& row1) const {
    float y0 = 1e30f, y1 = -1e30f;
    for (const Line& l : lines)
        for (const Pt& p : l) { y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    row0 = row1 = 0;
    if (y1 < y0) return;
    row0 = std::max(0, int(std::floor(y0 - t)) - 1);   // fill()'s rows for strokes()' bounds
    row1 = std::max(row0, std::min(h, int(std::ceil(y1 + t)) + 1));
}

void Canvas::store(std::uint8_t* rgba, int stride_px, int x0, int y0, int row0, int row1) const {
    for (int y = std::max(row0, 0); y < (row1 < 0 ? h : std::min(row1, h)); ++y)
        for (int x = 0; x < w; ++x) {
            const auto& p = px[std::size_t(y) * std::size_t(w) + std::size_t(x)];
            std::uint8_t* o = rgba + 4 * (std::size_t(y0 + y) * std::size_t(stride_px) + std::size_t(x0 + x));
            if (p[3] <= 0) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            for (int k = 0; k < 3; ++k) o[k] = round_byte(std::clamp(p[std::size_t(k)] / p[3], 0.0f, 1.0f) * 255.0f);
            o[3] = round_byte(std::clamp(p[3], 0.0f, 1.0f) * 255.0f);
        }
}

float text(std::string_view s, float x, float top, float cap, float t, std::vector<Line>& out) {
    const float x_start = x;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const Letter g = letter(s[i]);
        for (const Line& l : g.lines) {
            Line p;
            for (const Pt& q : l) p.push_back({x + q.x * cap, top + q.y * cap});
            out.push_back(std::move(p));
        }
        x += g.width * cap + (i + 1 < s.size() ? t + 0.24f * cap : 0.0f);
    }
    return x - x_start;
}

float text_width(std::string_view s, float cap, float t) {
    std::vector<Line> scratch;
    return text(s, 0, 0, cap, t, scratch);
}

}  // namespace b3replay::glyph
