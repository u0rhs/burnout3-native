// The accessibility rows' drawing side: src/b3replay/a11y.h.
#include "a11y.h"

#include <algorithm>
#include <cmath>

namespace b3replay::a11y {

namespace {

Matrix3 mul(const Matrix3& a, const Matrix3& b) {
    Matrix3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0;
            for (int k = 0; k < 3; ++k) s += a[std::size_t(3 * i + k)] * b[std::size_t(3 * k + j)];
            r[std::size_t(3 * i + j)] = s;
        }
    return r;
}

constexpr Matrix3 kIdentity = {1, 0, 0, 0, 1, 0, 0, 0, 1};
// The dichromat's view of an RGB colour: Machado, Oliveira and Fernandes 2009, severity 1.0 (their published matrices; on
// the 8-bit values as games use them, not on linear light)
constexpr Matrix3 kProtan = {0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f, -0.003882f, -0.048116f, 1.051998f};
constexpr Matrix3 kDeutan = {0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f, -0.011820f, 0.042940f, 0.968881f};
constexpr Matrix3 kTritan = {1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f, 0.004733f, 0.691367f, 0.303900f};
// where the lost difference goes (Fidaner, Lin and Ozguven 2005): a red-green loss into green and blue; a blue-yellow
// loss into red and green
constexpr Matrix3 kShiftRedGreen = {0, 0, 0, 0.7f, 1, 0, 0.7f, 0, 1};
constexpr Matrix3 kShiftBlue = {1, 0, 0.7f, 0, 1, 0.7f, 0, 0, 0};

std::uint8_t to_byte(float v) { return std::uint8_t(std::lround(std::clamp(v, 0.0f, 255.0f))); }

}  // namespace

ColourFilter colour_filter(int setting) {
    return setting >= 1 && setting <= 3 ? ColourFilter(setting) : ColourFilter::kOff;
}

Matrix3 simulate_matrix(ColourFilter f) {
    return f == ColourFilter::kDeuteranopia ? kDeutan : f == ColourFilter::kProtanopia ? kProtan
         : f == ColourFilter::kTritanopia   ? kTritan : kIdentity;
}

Matrix3 daltonize_matrix(ColourFilter f) {
    if (f == ColourFilter::kOff) return kIdentity;
    const Matrix3 sim = simulate_matrix(f);
    Matrix3 err{};
    for (std::size_t i = 0; i < 9; ++i) err[i] = kIdentity[i] - sim[i];
    const Matrix3 moved = mul(f == ColourFilter::kTritanopia ? kShiftBlue : kShiftRedGreen, err);
    Matrix3 m{};
    for (std::size_t i = 0; i < 9; ++i) m[i] = kIdentity[i] + moved[i];
    return m;
}

std::uint32_t remap_rgba(const Matrix3& m, std::uint32_t rgba) {
    const float r = float(rgba & 0xFFu), g = float(rgba >> 8 & 0xFFu), b = float(rgba >> 16 & 0xFFu);
    return std::uint32_t(to_byte(m[0] * r + m[1] * g + m[2] * b)) | std::uint32_t(to_byte(m[3] * r + m[4] * g + m[5] * b)) << 8 |
           std::uint32_t(to_byte(m[6] * r + m[7] * g + m[8] * b)) << 16 | (rgba & 0xFF000000u);
}

void remap_pixels(const Matrix3& m, std::uint8_t* rgba, std::size_t pixels) {
    for (std::size_t i = 0; i < pixels; ++i) {
        std::uint8_t* p = rgba + 4 * i;
        const std::uint32_t c = remap_rgba(m, std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24);
        p[0] = std::uint8_t(c & 0xFFu);
        p[1] = std::uint8_t(c >> 8 & 0xFFu);
        p[2] = std::uint8_t(c >> 16 & 0xFFu);
    }
}

FlashCut flash_cut(const FlashDraw& d) {
    FlashCut cut;
    if (d.coverage < kFlashCoverage) return cut;
    if (d.textured) return cut;   // a picture (a backdrop, a loading screen): never dimmed
    if (d.white < kFlashWhite) return cut;   // a coloured or dark fill (a dialog box, the pause shade, the letterbox)
    // GS ALPHA: (A - B) x C + D; A / B / D: 0 Cs, 1 Cd, 2 zero; C: 0 As, 1 Ad, 2 FIX
    const int a = d.gs_alpha & 3, b = d.gs_alpha >> 2 & 3, c = d.gs_alpha >> 4 & 3, dd = d.gs_alpha >> 6 & 3;
    float lift = 0;   // how far the draw can lift a black picture, of white
    if (!d.blend || (a == 0 && dd != 1)) {   // drawn as is: the colour replaces the picture
        lift = d.light;
    } else if (a == 0 && b == 1 && dd == 1) {   // (Cs - Cd) C + Cd: a translucent fill
        if (c == 0) {
            lift = d.light * std::min(1.0f, d.alpha);
            cut.scale_alpha = true;
        } else {
            lift = d.light;   // by the frame alpha or FIX: taken as covering
        }
    } else if (a == 0 && b == 2 && dd == 1) {   // Cs C + Cd: additive
        lift = d.light * (c == 0 ? std::min(1.0f, d.alpha) : 1.0f);
    } else {
        return cut;   // subtractive, alpha-only, nothing drawn
    }
    if (lift > kFlashCap) cut.factor = kFlashCap / lift;
    return cut;
}

ExplosionFlash explosion_flash(float elapsed, bool reduce) {
    ExplosionFlash f;
    const float x = elapsed - 0.025f;   // D_004E0DB0
    if (x < 0.0f || !(x < 0.12f)) return f;   // D_004E0BAC
    const float t = 8.333334f * (0.12f - x);   // D_004E1004
    const float a = std::min(t * t * 128.9f, 255.9f);
    f.r = 255;   // min(2.0 x 127.95, 255.9)
    f.g = 204;   // 1.6 x 127.95 (D_004E0F7C = 1.6)
    f.b = 127;   // 1.0 x 127.95
    float av = a;
    if (reduce) {
        FlashDraw d;
        d.coverage = 1.0f;
        d.light = 1.0f;   // the additive fill reaches white: Cs 255 x As
        d.white = 1.0f;
        d.alpha = a / 128.0f;
        d.gs_alpha = 0x48;
        av = a * flash_cut(d).factor;
    }
    f.a = std::uint8_t(int(av));
    f.on = true;
    return f;
}

bool static_frame(std::string_view n) {
    return n.size() == 10 && n.substr(0, 9) == "Camchange" && n[9] >= '2' && n[9] <= '5';
}

float hud_size_scale(int level) {
    static const float k[kHudSizeLevels] = {1.0f, 1.15f, 1.3f, 1.5f};
    return level >= 0 && level < kHudSizeLevels ? k[level] : 1.0f;
}

int hud_size_percent(int level) {
    static const int k[kHudSizeLevels] = {100, 115, 130, 150};
    return level >= 0 && level < kHudSizeLevels ? k[level] : 100;
}

Pivot anchor_pivot(float fx, float fy, const Box& rect, float shift) {
    Pivot p;
    p.x = rect.x0 + fx * (rect.x1 - rect.x0) + shift;
    p.y = rect.y0 + fy * (rect.y1 - rect.y0);
    return p;
}

Box scaled(const Box& b, Pivot p, float s) {
    if (b.empty()) return b;
    Box o;
    o.add(scale_about(b.x0, p.x, s), scale_about(b.y0, p.y, s));
    o.add(scale_about(b.x1, p.x, s), scale_about(b.y1, p.y, s));
    return o;
}

float fit_scale(const Box& b, Pivot p, float s, const Box& bounds) {
    if (b.empty() || !(s > 1.0f)) return 1.0f;
    float m = s;
    // a side on the far side of the pivot moves out as s grows, up to the bounds' side; one on the pivot's own side
    // moves in (never limits), one already past the bounds at 100 % was off screen already (not limiting)
    const auto lim = [&](float edge, float piv, float bound, bool high) {
        const float d = high ? edge - piv : piv - edge;   // > 0: the side lies beyond the pivot, outward
        const float room = high ? bound - piv : piv - bound;
        if (!(d > 0.0f) || (high ? edge > bound : edge < bound)) return;
        m = std::min(m, std::max(room, 0.0f) / d);
    };
    lim(b.x1, p.x, bounds.x1, true);
    lim(b.x0, p.x, bounds.x0, false);
    lim(b.y1, p.y, bounds.y1, true);
    lim(b.y0, p.y, bounds.y0, false);
    return std::clamp(m, 1.0f, s);
}

void resolve_overlaps(std::vector<Part>& parts, float gap) {
    const std::size_t n = parts.size();
    for (Part& p : parts) p.scale = std::max(1.0f, p.scale);
    // the largest t in [lo, hi] with ok(t), ok(lo) holding: bisection (24 steps: far under a 2D unit)
    const auto largest = [](float lo, float hi, const auto& ok) {
        if (ok(hi)) return hi;
        for (int k = 0; k < 24; ++k) {
            const float mid = 0.5f * (lo + hi);
            (ok(mid) ? lo : hi) = mid;
        }
        return lo;
    };
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = i + 1; j < n; ++j) {
                Part& a = parts[i];
                Part& b = parts[j];
                if (a.box.empty() || b.box.empty() || a.box.overlaps(b.box, gap)) continue;   // touching at 100 %: as the game has them
                if (!scaled(a.box, a.pivot, a.scale).overlaps(scaled(b.box, b.pivot, b.scale), gap)) continue;
                Part& m = a.message ? a : b;
                const Part& f = a.message ? b : a;
                const Box fb = scaled(f.box, f.pivot, f.scale);
                if (a.message != b.message && !m.box.overlaps(fb, gap)) {   // the message gives way (enough when it fits at 100 %)
                    m.scale = largest(1.0f, m.scale, [&](float t) { return !scaled(m.box, m.pivot, t).overlaps(fb, gap); });
                } else {   // one common cap
                    const float t = largest(1.0f, std::max(a.scale, b.scale), [&](float c) {
                        return !scaled(a.box, a.pivot, std::min(a.scale, c)).overlaps(scaled(b.box, b.pivot, std::min(b.scale, c)), gap);
                    });
                    a.scale = std::min(a.scale, t);
                    b.scale = std::min(b.scale, t);
                }
                changed = true;
            }
        if (!changed) break;
    }
}

}  // namespace b3replay::a11y
