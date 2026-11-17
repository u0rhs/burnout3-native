// draw.cpp -- see draw.h
#include "draw.h"

#include <algorithm>
#include <cmath>

namespace b3launcher::draw {

namespace {

SDL_Renderer* g_r = nullptr;
float g_px = 1;
float g_round = 1;
constexpr float kPi = 3.14159265358979f;

SDL_Vertex vtx(P p, SDL_FColor c) { return SDL_Vertex{{p.x, p.y}, c, {0, 0}}; }

void submit(const std::vector<SDL_Vertex>& v, const std::vector<int>& idx) {
    if (!v.empty() && !idx.empty()) SDL_RenderGeometry(g_r, nullptr, v.data(), int(v.size()), idx.data(), int(idx.size()));
}

// the unit normals at each vertex (a miter of its two edges, limited), for a closed or open polyline
std::vector<P> normals(const std::vector<P>& p, bool closed) {
    const std::size_t n = p.size();
    std::vector<P> out(n, P{0, 0});
    auto edge_n = [&](std::size_t a, std::size_t b) {
        float dx = p[b].x - p[a].x, dy = p[b].y - p[a].y;
        const float l = std::sqrt(dx * dx + dy * dy);
        if (l < 1e-6f) return P{0, 0};
        return P{dy / l, -dx / l};
    };
    for (std::size_t i = 0; i < n; ++i) {
        P a{0, 0}, b{0, 0};
        const bool has_prev = closed || i > 0, has_next = closed || i + 1 < n;
        if (has_prev) a = edge_n((i + n - 1) % n, i);
        if (has_next) b = edge_n(i, (i + 1) % n);
        if (!has_prev) a = b;
        if (!has_next) b = a;
        P m{a.x + b.x, a.y + b.y};
        const float l = std::sqrt(m.x * m.x + m.y * m.y);
        if (l < 1e-6f) { out[i] = a; continue; }
        m = {m.x / l, m.y / l};
        const float cosh = m.x * a.x + m.y * a.y;   // cos of half the turn
        const float k = 1.0f / std::max(cosh, 0.5f);
        out[i] = {m.x * k, m.y * k};
    }
    return out;
}

float signed_area(const std::vector<P>& p) {
    float a = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const P& u = p[i];
        const P& w = p[(i + 1) % p.size()];
        a += u.x * w.y - w.x * u.y;
    }
    return a * 0.5f;
}

void arc(std::vector<P>& out, float cx, float cy, float r, float a0, float a1, int steps) {
    for (int k = 0; k <= steps; ++k) {
        const float a = (a0 + (a1 - a0) * float(k) / float(steps)) * kPi / 180.0f;
        out.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
}

std::vector<P> round_rect_pts(const SDL_FRect& r, float rad) {
    rad *= g_round;
    rad = std::max(0.0f, std::min(rad, std::min(r.w, r.h) * 0.5f));
    std::vector<P> p;
    const int steps = std::clamp(int(rad / g_px / 1.5f), 3, 16);
    if (rad <= 0.01f) return {{r.x, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}};
    arc(p, r.x + r.w - rad, r.y + rad, rad, -90, 0, steps);
    arc(p, r.x + r.w - rad, r.y + r.h - rad, rad, 0, 90, steps);
    arc(p, r.x + rad, r.y + r.h - rad, rad, 90, 180, steps);
    arc(p, r.x + rad, r.y + rad, rad, 180, 270, steps);
    return p;
}

std::vector<P> circle_pts(float cx, float cy, float r) {
    std::vector<P> p;
    const int steps = std::clamp(int(r / g_px * 1.2f), 12, 96);
    for (int k = 0; k < steps; ++k) {
        const float a = 2.0f * kPi * float(k) / float(steps);
        p.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
    return p;
}

}  // namespace

void set_round(float k) { g_round = k; }

void set_renderer(SDL_Renderer* r, float px) {
    g_r = r;
    g_px = px;
}

SDL_FColor rgba(unsigned hex, float a) {
    return SDL_FColor{float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f, a};
}
SDL_FColor mix(SDL_FColor a, SDL_FColor b, float t) {
    return SDL_FColor{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
SDL_FColor alpha(SDL_FColor c, float a) { return SDL_FColor{c.r, c.g, c.b, c.a * a}; }

void rect(const SDL_FRect& r, SDL_FColor c) {
    SDL_SetRenderDrawColorFloat(g_r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(g_r, &r);
}

void gradient(const SDL_FRect& r, SDL_FColor lt, SDL_FColor rt, SDL_FColor lb, SDL_FColor rb) {
    std::vector<SDL_Vertex> v = {vtx({r.x, r.y}, lt), vtx({r.x + r.w, r.y}, rt), vtx({r.x + r.w, r.y + r.h}, rb), vtx({r.x, r.y + r.h}, lb)};
    submit(v, {0, 1, 2, 0, 2, 3});
}

void convex(const std::vector<P>& in, SDL_FColor c) {
    if (in.size() < 3) return;
    std::vector<P> p = in;
    if (signed_area(p) < 0) std::reverse(p.begin(), p.end());   // y down: positive area = clockwise on screen
    const std::vector<P> nrm = normals(p, true);
    // with y down and positive area, the edge normal (dy, -dx) points outward
    const float h = g_px * 0.5f;
    const SDL_FColor clear = alpha(c, 0);
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    const int n = int(p.size());
    for (int i = 0; i < n; ++i) v.push_back(vtx({p[i].x - nrm[i].x * h, p[i].y - nrm[i].y * h}, c));
    for (int i = 0; i < n; ++i) v.push_back(vtx({p[i].x + nrm[i].x * h, p[i].y + nrm[i].y * h}, clear));
    for (int i = 1; i + 1 < n; ++i) idx.insert(idx.end(), {0, i, i + 1});
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        idx.insert(idx.end(), {i, j, n + j, i, n + j, n + i});
    }
    submit(v, idx);
}

void stroke(const std::vector<P>& p, float w, SDL_FColor c, bool closed) {
    const int n = int(p.size());
    if (n < 2) return;
    const std::vector<P> nrm = normals(p, closed);
    float half = w * 0.5f;
    SDL_FColor core = c;
    if (w < g_px) {   // thinner than a pixel: one pixel, fainter
        core = alpha(c, w / g_px);
        half = g_px * 0.5f;
    }
    const float f = g_px * 0.5f;
    const float off[4] = {-half - f, -half + f, half - f, half + f};
    const SDL_FColor col[4] = {alpha(core, 0), core, core, alpha(core, 0)};
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 4; ++k) {
            const float o = (k == 1 || k == 2) && half < f ? 0.0f : off[k];
            v.push_back(vtx({p[i].x + nrm[i].x * o, p[i].y + nrm[i].y * o}, col[k]));
        }
    const int segs = closed ? n : n - 1;
    for (int s = 0; s < segs; ++s) {
        const int a = s * 4, b = ((s + 1) % n) * 4;
        for (int k = 0; k < 3; ++k) idx.insert(idx.end(), {a + k, b + k, b + k + 1, a + k, b + k + 1, a + k + 1});
    }
    submit(v, idx);
}

void round_rect(const SDL_FRect& r, float radius, SDL_FColor c) { convex(round_rect_pts(r, radius), c); }

void round_rect_outline(const SDL_FRect& r, float radius, float w, SDL_FColor c) {
    const SDL_FRect in{r.x + w * 0.5f, r.y + w * 0.5f, r.w - w, r.h - w};
    stroke(round_rect_pts(in, std::max(0.0f, radius - w * 0.5f)), w, c, true);
}

void disc(float cx, float cy, float r, SDL_FColor c) { convex(circle_pts(cx, cy, r), c); }

void ring(float cx, float cy, float r, float w, SDL_FColor c) { stroke(circle_pts(cx, cy, r - w * 0.5f), w, c, true); }

void line(float x0, float y0, float x1, float y1, float w, SDL_FColor c) { stroke({{x0, y0}, {x1, y1}}, w, c, false); }

void check(float cx, float cy, float s, float w, SDL_FColor c) {
    stroke({{cx - s * 0.42f, cy + s * 0.02f}, {cx - s * 0.12f, cy + s * 0.32f}, {cx + s * 0.44f, cy - s * 0.30f}}, w, c, false);
}

void cross(float cx, float cy, float s, float w, SDL_FColor c) {
    const float h = s * 0.34f;
    line(cx - h, cy - h, cx + h, cy + h, w, c);
    line(cx - h, cy + h, cx + h, cy - h, w, c);
}

void chevron(float cx, float cy, float s, int dir, float w, SDL_FColor c) {
    const float h = s * 0.5f, q = s * 0.25f;
    if (dir == -1) stroke({{cx + q, cy - h}, {cx - q, cy}, {cx + q, cy + h}}, w, c, false);
    else if (dir == 1) stroke({{cx - q, cy - h}, {cx + q, cy}, {cx - q, cy + h}}, w, c, false);
    else if (dir == -2) stroke({{cx - h, cy + q}, {cx, cy - q}, {cx + h, cy + q}}, w, c, false);
    else stroke({{cx - h, cy - q}, {cx, cy + q}, {cx + h, cy - q}}, w, c, false);
}

void triangle(float cx, float cy, float s, SDL_FColor c) {
    const float h = s * 0.42f;
    stroke({{cx, cy - h}, {cx + h * 1.05f, cy + h * 0.75f}, {cx - h * 1.05f, cy + h * 0.75f}}, s * 0.11f, c, true);
}

void square(float cx, float cy, float s, SDL_FColor c) {
    const float h = s * 0.32f;
    stroke({{cx - h, cy - h}, {cx + h, cy - h}, {cx + h, cy + h}, {cx - h, cy + h}}, s * 0.11f, c, true);
}

}  // namespace b3launcher::draw
