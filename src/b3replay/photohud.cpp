// Photo mode's on-screen UI: src/b3replay/photohud.h.
#include "photohud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "a11y.h"
#include "glyphdraw.h"

namespace b3replay {

using glyph::Canvas;
using glyph::Line;
using glyph::Pt;

PhotoUi photo_ui_next(PhotoUi u) {
    switch (u) {
    case PhotoUi::kFull: return PhotoUi::kReadouts;
    case PhotoUi::kReadouts: return PhotoUi::kHidden;
    default: return PhotoUi::kFull;
    }
}

bool PhotoHudData::same_picture(const PhotoHudData& o) const {
    return std::lround(fov_deg) == std::lround(o.fov_deg) && std::lround(roll_deg) == std::lround(o.roll_deg) && dof_on == o.dof_on &&
           dof_level == o.dof_level && dof_levels == o.dof_levels && focus_car == o.focus_car &&
           std::lround(focus_stops * 20.0f) == std::lround(o.focus_stops * 20.0f);
}

namespace {

// ---- the look ----
constexpr float kAmber[4] = {1.0f, 0.80f, 0.25f, 1.0f};        // the notices' heading
constexpr float kOrange[4] = {0.98f, 0.50f, 0.12f, 1.0f};
constexpr float kWhite[4] = {0.95f, 0.95f, 0.95f, 1.0f};
constexpr float kSoft[4] = {0.70f, 0.72f, 0.76f, 1.0f};
constexpr float kFaint[4] = {0.45f, 0.47f, 0.52f, 1.0f};
constexpr float kPlate[4] = {0.02f, 0.022f, 0.032f, 0.88f};
constexpr float kEdge[4] = {0.95f, 0.72f, 0.20f, 0.88f};
constexpr float kCell[4] = {1.0f, 1.0f, 1.0f, 0.13f};          // an unlit gauge cell
constexpr float kCapFill[4] = {0.13f, 0.14f, 0.17f, 0.97f}, kCapEdge[4] = {0.84f, 0.86f, 0.90f, 0.92f};

// The size unit and the layout's measures, all in pixels
struct M {
    float s;
    float pad() const { return 12 * s; }
    float tok() const { return 25 * s; }          // a control's height
    float gap() const { return 3 * s; }           // between controls of a group
    float sep() const { return 15 * s; }          // between groups
    float row_gap() const { return 7 * s; }
    float label_cap() const { return 8.4f * s; }
    float label_stroke() const { return std::max(1.1f, 1.25f * s); }
    float cap_cap() const { return 9.4f * s; }    // a key cap's text
    float cap_stroke() const { return std::max(1.1f, 1.45f * s); }
    float chamfer() const { return 9 * s; }
    float edge() const { return std::max(1.0f, 1.5f * s); }
};

float put(Canvas& c, std::string_view t, float x, float top, float cap, float st, const float (&col)[4]) {
    std::vector<Line> l;
    const float w = glyph::text(t, x, top, cap, st, l);
    c.strokes(l, st, col);
    return w;
}

// ---- the controls: groups of key caps / button glyphs ----
struct Tok {
    enum Kind { kKey, kPad, kChip, kDpad } kind = kKey;
    std::uint16_t sc = 0;
    PadButton b = PadButton::kCross;
    std::string text;
    int arms = 0;   // kDpad: the arms that act (1 up, 2 down, 4 left, 8 right)
};
struct Group {
    const char* label;
    int section;    // 0 camera, 1 lens, 2 session (a thin divider between sections on a row)
    int prio;       // the lower, the longer it stays when the strip must be shortened
    std::vector<Tok> toks;
};

Tok key(std::uint16_t sc) { return sc ? Tok{Tok::kKey, sc, PadButton::kCross, {}, 0} : Tok{Tok::kChip, 0, PadButton::kCross, "-", 0}; }   // (rebind2: unbound: a dash)
Tok pad(PadButton b) { return Tok{Tok::kPad, 0, b, {}, 0}; }
Tok chip(const char* t) { return Tok{Tok::kChip, 0, PadButton::kCross, t, 0}; }
Tok pk(const PhotoPadKey& k) { return k.text.empty() ? Tok{Tok::kPad, 0, k.b, {}, 0} : Tok{Tok::kChip, 0, PadButton::kCross, k.text, 0}; }
Tok dpad(int arms) { return Tok{Tok::kDpad, 0, PadButton::kCross, {}, arms}; }

std::vector<Group> make_groups(bool keyboard, const PhotoKeys& K) {
    if (keyboard)
        return {
            {"MOVE", 0, 1, {key(K.fwd), key(K.left), key(K.back), key(K.right)}},
            {"UP/DOWN", 0, 4, {key(K.up), key(K.down)}},
            {"LOOK", 0, 2, {chip("MOUSE"), chip("ARROWS")}},
            {"FAST/SLOW", 0, 7, {key(K.fast), key(K.slow)}},
            {"ZOOM", 1, 3, {chip("WHEEL"), key(K.zoom_out), key(K.zoom_in)}},
            {"ROLL", 1, 3, {key(K.roll_ccw), key(K.roll_cw)}},
            {"DOF", 1, 8, {key(K.dof)}},
            {"BLUR", 1, 9, {key(K.blur_less), key(K.blur_more)}},
            {"FOCUS", 1, 10, {key(K.focus_near), key(K.focus_far)}},
            {"FOCUS ON", 1, 11, {key(K.target)}},
            {"RESET", 2, 6, {key(K.reset)}},
            {"HIDE UI", 2, 5, {key(K.hide_ui)}},
            {"PHOTO", 2, 5, {key(K.shot)}},
            {"EXIT", 2, 1, {key(K.exit_key), key(K.exit_alt)}},
        };
    return {
        {"MOVE", 0, 1, {chip("L STICK")}},
        {"UP/DOWN", 0, 4, {pk(K.pad_up), pk(K.pad_down)}},
        {"LOOK", 0, 2, {chip("R STICK")}},
        {"FAST", 0, 7, {pk(K.pad_fast)}},
        {"ZOOM", 1, 3, {dpad(1 | 2)}},
        {"ROLL", 1, 3, {pk(K.pad_roll_ccw), pk(K.pad_roll_cw)}},
        {"DOF", 1, 8, {pk(K.pad_dof)}},
        {"BLUR", 1, 9, {pk(K.pad_blur)}},
        {"FOCUS", 1, 10, {dpad(4 | 8)}},
        {"FOCUS ON", 1, 11, {pk(K.pad_target)}},
        {"RESET", 2, 6, {pk(K.pad_reset)}},
        {"HIDE UI", 2, 5, {pk(K.pad_hide)}},
        {"EXIT", 2, 1, {pk(K.pad_exit)}},
    };
}

float pad_glyph_w(PadButton b, float h) {   // prompts.cpp cell_units: shoulders 35 x 15, Start / Select 33 x 21, the rest square
    if (b == PadButton::kL1 || b == PadButton::kR1 || b == PadButton::kL2 || b == PadButton::kR2) return h * 35.0f / 15.0f;
    if (b == PadButton::kStart || b == PadButton::kSelect) return h * 33.0f / 21.0f;
    return h;
}

float tok_w(const Tok& t, const M& m) {
    switch (t.kind) {
    case Tok::kPad: return pad_glyph_w(t.b, m.tok());
    case Tok::kDpad: return m.tok();
    case Tok::kChip: return glyph::text_width(t.text, m.cap_cap(), m.cap_stroke()) + 2 * 8 * m.s;
    case Tok::kKey: {
        const std::string n = key_cap_name(t.sc);
        const float cap = n.size() == 1 ? 1.15f * m.cap_cap() : m.cap_cap();
        return std::max(m.tok(), glyph::text_width(n, cap, m.cap_stroke()) + 2 * 7 * m.s);
    }
    }
    return m.tok();
}

float group_w(const Group& g, const M& m) {
    float w = glyph::text_width(g.label, m.label_cap(), m.label_stroke()) + 6 * m.s;
    for (std::size_t i = 0; i < g.toks.size(); ++i) w += tok_w(g.toks[i], m) + (i ? m.gap() : 0.0f);
    return w;
}

struct Placed { std::size_t group; float x, y; };
struct Flow {
    std::vector<Placed> at;
    float w = 0, h = 0;
    int rows = 0;
};

// The groups laid out in rows no wider than `max_w`, each row centred in the plate
Flow flow(const std::vector<const Group*>& g, const M& m, float max_w) {
    Flow f;
    if (g.empty()) return f;
    std::vector<float> gw;
    for (const Group* p : g) gw.push_back(group_w(*p, m));
    std::vector<std::vector<std::size_t>> rows(1);
    float x = m.pad(), widest = 0;
    for (std::size_t i = 0; i < g.size(); ++i) {
        if (!rows.back().empty() && x + gw[i] + m.pad() > max_w) {
            widest = std::max(widest, x - m.sep() + m.pad());
            rows.emplace_back();
            x = m.pad();
        }
        rows.back().push_back(i);
        x += gw[i] + m.sep();
    }
    widest = std::max(widest, x - m.sep() + m.pad());
    f.w = std::ceil(widest);
    f.rows = int(rows.size());
    f.h = std::ceil(2 * m.pad() + float(rows.size()) * m.tok() + float(rows.size() - 1) * m.row_gap());
    for (std::size_t r = 0; r < rows.size(); ++r) {
        float rw = 0;
        for (std::size_t k = 0; k < rows[r].size(); ++k) rw += gw[rows[r][k]] + (k ? m.sep() : 0.0f);
        float rx = std::floor(0.5f * (f.w - rw));
        const float ry = m.pad() + float(r) * (m.tok() + m.row_gap());
        for (std::size_t k = 0; k < rows[r].size(); ++k) {
            f.at.push_back({rows[r][k], rx, ry});
            rx += gw[rows[r][k]] + m.sep();
        }
    }
    return f;
}

// The groups kept when `keep` of them stay: the `keep` lowest priorities, in display order
std::vector<const Group*> kept(const std::vector<Group>& all, int keep) {
    std::vector<int> prios;
    for (const Group& g : all) prios.push_back(g.prio);
    std::sort(prios.begin(), prios.end());
    const int limit = prios[std::size_t(std::clamp(keep, 1, int(prios.size())) - 1)];
    std::vector<const Group*> out;
    for (const Group& g : all)
        if (g.prio <= limit) out.push_back(&g);
    return out;
}

// ---- the readouts' measures (units of `s`) ----
struct Ro {
    float pad, head_h, row_h, row_gap, label_w, gauge_w, val_w, gap, w, h;
};
constexpr int kRoRows = 4;
Ro ro_of(const M& m) {
    Ro r{};
    r.pad = m.pad();
    r.head_h = 29 * m.s;
    r.row_h = 23 * m.s;
    r.row_gap = 5 * m.s;
    r.label_w = 54 * m.s;
    r.gauge_w = 124 * m.s;
    r.val_w = 70 * m.s;
    r.gap = 8 * m.s;
    r.w = std::ceil(2 * r.pad + r.label_w + r.gap + r.gauge_w + r.gap + r.val_w);
    r.h = std::ceil(r.pad + r.head_h + 8 * m.s + kRoRows * r.row_h + (kRoRows - 1) * r.row_gap + r.pad);
    return r;
}

// the shared base of both plates: a dark plate with two cut corners and an amber edge, and a short amber bar on its top
// edge (the HUD's panel accent)
void plate(Canvas& c, const M& m) {
    const float w = float(c.w), h = float(c.h), k = m.chamfer(), e = m.edge();
    const std::vector<Pt> p{{k, 0}, {w, 0}, {w, h - k}, {w - k, h}, {0, h}, {0, k}};
    c.polygon(p, kPlate);
    Line l = {{k, e * 0.5f}, {w - e * 0.5f, e * 0.5f}, {w - e * 0.5f, h - k}, {w - k, h - e * 0.5f}, {e * 0.5f, h - e * 0.5f}, {e * 0.5f, k}, {k, e * 0.5f}};
    c.strokes({l}, e, kEdge);
    // the accent: a thicker amber / orange bar along the top edge from the chamfer
    const float bar = std::min(w * 0.30f, 70 * m.s), t = std::max(2.0f, 3.0f * m.s);
    c.strokes({{{k + e, t * 0.5f}, {k + bar, t * 0.5f}}}, t, kAmber);
    c.strokes({{{k + bar + 2 * m.s, t * 0.5f}, {k + bar + 14 * m.s, t * 0.5f}}}, t, kOrange);
}

// a gauge: `cells` slanted cells in a box (x, y, w, h); lit from `from` to `to` (cell indices, [from, to)); cell
// `mark` (>= 0) drawn as a white marker
void gauge(Canvas& c, const M& m, float x, float y, float w, float h, int cells, int from, int to, int mark, const float (&lit)[4]) {
    const float pitch = w / float(cells), cw = pitch - 1.6f * m.s, skew = 0.22f * h;
    for (int i = 0; i < cells; ++i) {
        const float x0 = x + float(i) * pitch + skew * 0.5f;
        const std::vector<Pt> p{{x0 + skew, y}, {x0 + skew + cw, y}, {x0 + cw, y + h}, {x0, y + h}};
        const bool on = i >= from && i < to;
        c.polygon(p, i == mark ? kWhite : on ? lit : kCell);
    }
}

void camera_icon(Canvas& c, const M& m, float x, float cy) {
    const float s = m.s, w = 20 * s, h = 13 * s;
    c.box(x + w * 0.5f, cy + 1 * s, w * 0.5f, h * 0.5f, 2.6f * s, kAmber);
    c.box(x + 6 * s, cy - 6.5f * s, 3.4f * s, 1.8f * s, 0.8f * s, kAmber);   // the viewfinder bump
    const float dark[4] = {0.02f, 0.02f, 0.03f, 1};
    c.disc(x + w * 0.5f + 0.5f * s, cy + 1 * s, 4.4f * s, dark);
    c.ring(x + w * 0.5f + 0.5f * s, cy + 1 * s, 2.6f * s, std::max(1.0f, 1.2f * s), kAmber);
}

// A key cap / chip / button / d-pad of the strip at (x, y)
void paint_tok(Canvas& c, const M& m, const Tok& t, float x, float y, PromptDevice dev) {
    const float h = m.tok(), w = tok_w(t, m), st = m.cap_stroke();
    switch (t.kind) {
    case Tok::kKey: {
        c.box(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, 4.5f * m.s, kCapFill);
        c.box_outline(x + w * 0.5f, y + h * 0.5f, w * 0.5f - 0.5f * st, h * 0.5f - 0.5f * st, 4.0f * m.s, std::max(1.0f, 1.3f * m.s), kCapEdge);
        const float lip[4] = {1, 1, 1, 0.22f};
        c.strokes({{{x + 5 * m.s, y + h - 4 * m.s}, {x + w - 5 * m.s, y + h - 4 * m.s}}}, std::max(1.0f, 1.2f * m.s), lip);
        const std::string n = key_cap_name(t.sc);
        const float cap = n.size() == 1 ? 1.15f * m.cap_cap() : m.cap_cap();
        const float tw = glyph::text_width(n, cap, st);
        put(c, n, x + 0.5f * (w - tw), y + 0.5f * (h - 3 * m.s) - 0.5f * cap, cap, st, kWhite);
        break;
    }
    case Tok::kChip: {
        c.box(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, h * 0.5f, kCapFill);
        c.box_outline(x + w * 0.5f, y + h * 0.5f, w * 0.5f - 0.5f * st, h * 0.5f - 0.5f * st, h * 0.5f - 0.5f * st, std::max(1.0f, 1.3f * m.s), kCapEdge);
        put(c, t.text, x + 8 * m.s, y + 0.5f * (h - m.cap_cap()), m.cap_cap(), st, kWhite);
        break;
    }
    case Tok::kPad: {
        const float back[4] = {0.0f, 0.0f, 0.0f, 0.35f};
        if (t.b == PadButton::kCross || t.b == PadButton::kCircle || t.b == PadButton::kSquare || t.b == PadButton::kTriangle ||
            t.b == PadButton::kL3 || t.b == PadButton::kR3)
            c.disc(x + w * 0.5f, y + h * 0.5f, h * 0.5f, back);
        paint_prompt_glyph(c, x, y, h, dev, t.b);   // the physical button's position: photo mode reads the pad, not its bindings
        break;
    }
    case Tok::kDpad: {
        const float cx = x + w * 0.5f, cy = y + h * 0.5f, arm = 0.5f * h, half = 0.17f * h, r = 1.6f * m.s;
        auto one = [&](int bit, float dx, float dy) {
            const bool on = (t.arms & bit) != 0;
            const float along = 0.5f * (arm - half - 0.5f * m.s), at = half + along;   // the arm's half length, its centre from the middle
            c.box(cx + dx * at, cy + dy * at, dx != 0 ? along : half, dy != 0 ? along : half, r, on ? kAmber : kFaint);
        };
        c.box(cx, cy, half, half, r, kFaint);
        one(1, 0, -1);
        one(2, 0, 1);
        one(4, -1, 0);
        one(8, 1, 0);
        break;
    }
    }
}

}  // namespace

// ---- layout ----

PhotoHud::Layout PhotoHud::layout(int w, int h, int hud_size, PromptDevice device, const PhotoKeys& keys, PhotoUi ui) {
    Layout L;
    if (w <= 0 || h <= 0 || ui == PhotoUi::kHidden) return L;
    const float base = std::max(0.75f, float(h) / 720.0f) * a11y::hud_size_scale(hud_size);
    const std::vector<Group> all = make_groups(device == PromptDevice::kKeyboard, keys);
    const float margin = std::round(0.045f * float(h));
    L.margin = margin;
    const float avail_w = float(w) - 2 * margin, avail_h = float(h) - 2 * margin;
    for (float s = base; s >= 0.3f; s *= 0.92f) {
        const M m{s};
        const Ro ro = ro_of(m);
        if (ro.w > avail_w || ro.h > avail_h) continue;
        L.scale = s;
        L.readouts = {float(w) - margin - ro.w, margin, ro.w, ro.h};
        L.strip = {};
        L.groups = L.rows = 0;
        if (ui != PhotoUi::kFull) return L;
        const float room = avail_h - ro.h - 8 * s;   // under the readouts, the plates apart
        const int total = int(all.size());
        for (int keep = total; keep >= 2; --keep) {
            const std::vector<const Group*> g = kept(all, keep);
            const Flow f = flow(g, m, std::min(avail_w, 1100.0f * s));
            if (f.w <= avail_w && f.h <= room) {
                L.strip = {std::floor(0.5f * (float(w) - f.w)), float(h) - margin - f.h, f.w, f.h};
                L.groups = int(g.size());
                L.rows = f.rows;
                return L;
            }
        }
    }
    // (nothing fits down to the smallest size: the readouts alone if they do)
    return L;
}

std::vector<std::string> PhotoHud::strip_labels(PromptDevice device, const PhotoKeys& keys) {
    std::vector<std::string> out;
    for (const Group& g : make_groups(device == PromptDevice::kKeyboard, keys))
        for (const Tok& t : g.toks) {
            if (t.kind == Tok::kKey) out.push_back(key_cap_name(t.sc));
            else if (t.kind == Tok::kChip) out.push_back(t.text);
            else if (t.kind == Tok::kPad) out.push_back(prompt_label(device, t.b));
            else out.push_back("DPAD");
        }
    return out;
}

// ---- pictures ----

void PhotoHud::picture_strip(const Layout& l, PromptDevice device, const PhotoKeys& keys, int& w, int& h, std::vector<std::uint8_t>& rgba) {
    w = int(l.strip.w);
    h = int(l.strip.h);
    rgba.clear();
    if (w <= 0 || h <= 0) return;
    const M m{l.scale};
    const std::vector<Group> all = make_groups(device == PromptDevice::kKeyboard, keys);
    // the groups the layout kept (the lowest priorities, `l.groups` of them)
    std::vector<const Group*> use;
    for (int keep = 1; keep <= int(all.size()); ++keep) {
        use = kept(all, keep);
        if (int(use.size()) >= l.groups) break;
    }
    const Flow f = flow(use, m, float(w));
    Canvas c(w, h);
    plate(c, m);
    const float sep_col[4] = {0.95f, 0.72f, 0.20f, 0.35f};
    for (std::size_t i = 0; i < f.at.size(); ++i) {
        const Group& gr = *use[f.at[i].group];
        float x = f.at[i].x;
        const float y = f.at[i].y, mid = y + 0.5f * m.tok();
        if (i > 0 && f.at[i - 1].y == y && use[f.at[i - 1].group]->section != gr.section) {   // a divider between sections
            const float dx = x - 0.5f * m.sep();
            c.strokes({{{dx, y + 3 * m.s}, {dx - 3 * m.s, y + m.tok() - 3 * m.s}}}, std::max(1.0f, 1.3f * m.s), sep_col);
        }
        x += put(c, gr.label, x, mid - 0.5f * m.label_cap(), m.label_cap(), m.label_stroke(), kAmber) + 6 * m.s;
        for (const Tok& t : gr.toks) {
            paint_tok(c, m, t, x, y, device);
            x += tok_w(t, m) + m.gap();
        }
    }
    rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    c.store(rgba.data(), w, 0, 0);
}

namespace {

void degree_sign(Canvas& c, const M& m, float x, float top, const float (&col)[4]) {
    c.ring(x + 2.0f * m.s, top + 1.6f * m.s, 1.7f * m.s, std::max(1.0f, 1.2f * m.s), col);
}

// a value at the row's right: text, left aligned at `x`
float value_text(Canvas& c, const M& m, std::string_view t, float x, float row_mid, const float (&col)[4]) {
    const float cap = 11.0f * m.s, st = std::max(1.3f, 1.8f * m.s);
    return put(c, t, x, row_mid - 0.5f * cap, cap, st, col);
}

}  // namespace

void PhotoHud::picture_readouts(const Layout& l, const PhotoHudData& d, PromptDevice device, const PhotoKeys& keys, int& w, int& h,
                                std::vector<std::uint8_t>& rgba) {
    w = int(l.readouts.w);
    h = int(l.readouts.h);
    rgba.clear();
    if (w <= 0 || h <= 0) return;
    const M m{l.scale};
    const Ro r = ro_of(m);
    Canvas c(w, h);
    plate(c, m);
    // the header: the mode (a shutter icon, PHOTO MODE) and the key that hides the UI
    const float hy = r.pad + 3 * m.s + 0.5f * r.head_h;
    camera_icon(c, m, r.pad, hy);
    put(c, "PHOTO MODE", r.pad + 30 * m.s, hy - 7.5f * m.s, 15 * m.s, std::max(1.7f, 2.3f * m.s), kAmber);
    {   // HIDE <key>, at the right
        const Tok t = device == PromptDevice::kKeyboard ? key(keys.hide_ui) : pk(keys.pad_hide);
        const float th = 0.8f * m.tok();
        const M mm{m.s * 0.8f};
        const float tw = tok_w(t, mm), lw = glyph::text_width("HIDE", m.label_cap(), m.label_stroke());
        const float x0 = float(w) - r.pad - tw;
        paint_tok(c, mm, t, x0, hy - 0.5f * th, device);
        put(c, "HIDE", x0 - 5 * m.s - lw, hy - 0.5f * m.label_cap(), m.label_cap(), m.label_stroke(), kSoft);
    }
    // a thin rule under the header
    const float rule[4] = {0.95f, 0.72f, 0.20f, 0.28f};
    const float ry = r.pad + r.head_h + 2 * m.s;
    c.strokes({{{r.pad, ry}, {float(w) - r.pad, ry}}}, std::max(1.0f, 1.2f * m.s), rule);
    // the gauges
    const float gx = r.pad + r.label_w + r.gap, vx = gx + r.gauge_w + r.gap, gh = 9.5f * m.s;
    float y = r.pad + r.head_h + 8 * m.s;
    auto label = [&](const char* t, float row_mid, const float (&col)[4]) {
        put(c, t, r.pad, row_mid - 0.5f * 9.2f * m.s, 9.2f * m.s, std::max(1.2f, 1.5f * m.s), col);
    };
    char buf[32];
    {   // FOV: the cells lit up to the field of view on a log scale (5 .. 120 degrees)
        const float mid = y + 0.5f * r.row_h;
        label("FOV", mid, kSoft);
        const float f = std::clamp(std::log(std::max(d.fov_deg, 5.0f) / 5.0f) / std::log(120.0f / 5.0f), 0.0f, 1.0f);
        constexpr int kCells = 22;
        gauge(c, m, gx, mid - 0.5f * gh, r.gauge_w, gh, kCells, 0, int(std::lround(f * float(kCells))), -1, kAmber);
        std::snprintf(buf, sizeof buf, "%ld", std::lround(d.fov_deg));
        const float tw = value_text(c, m, buf, vx, mid, kWhite);
        degree_sign(c, m, vx + tw + 2 * m.s, mid - 0.5f * 11.0f * m.s, kWhite);
        y += r.row_h + r.row_gap;
    }
    {   // ROLL: from the middle cell, to the side it leans
        const float mid = y + 0.5f * r.row_h;
        label("ROLL", mid, kSoft);
        constexpr int kCells = 23;   // odd: a middle cell
        const float f = std::clamp(d.roll_deg / 180.0f, -1.0f, 1.0f);
        const int n = int(std::lround(std::fabs(f) * float(kCells / 2)));
        const int centre = kCells / 2;
        gauge(c, m, gx, mid - 0.5f * gh, r.gauge_w, gh, kCells, f >= 0 ? centre + 1 : centre - n, f >= 0 ? centre + 1 + n : centre, centre, kAmber);
        const long v = std::lround(d.roll_deg);
        std::snprintf(buf, sizeof buf, "%s%ld", v > 0 ? "+" : "", v);
        const float tw = value_text(c, m, buf, vx, mid, v == 0 ? kSoft : kWhite);
        degree_sign(c, m, vx + tw + 2 * m.s, mid - 0.5f * 11.0f * m.s, v == 0 ? kSoft : kWhite);
        y += r.row_h + r.row_gap;
    }
    {   // DOF: one cell per blur level
        const float mid = y + 0.5f * r.row_h;
        label("DOF", mid, d.dof_on ? kAmber : kSoft);
        const int levels = std::max(1, d.dof_levels);
        const float cell_w = r.gauge_w / float(levels);
        for (int i = 0; i < levels; ++i) {
            const float x0 = gx + float(i) * cell_w + 1 * m.s, cw = cell_w - 3 * m.s;
            const float skew = 0.22f * 11 * m.s, top = mid - 5.5f * m.s, bot = mid + 5.5f * m.s;
            const std::vector<Pt> p{{x0 + skew, top}, {x0 + skew + cw, top}, {x0 + cw, bot}, {x0, bot}};
            c.polygon(p, d.dof_on && i < d.dof_level ? kAmber : kCell);
        }
        if (d.dof_on) {
            std::snprintf(buf, sizeof buf, "BLUR %d", d.dof_level);
            value_text(c, m, buf, vx, mid, kWhite);
        } else {
            value_text(c, m, "OFF", vx, mid, kFaint);
        }
        y += r.row_h + r.row_gap;
    }
    {   // FOCUS: where, and the offset in stops (from the middle cell); dim while the depth of field is off
        const float mid = y + 0.5f * r.row_h;
        const float (&col)[4] = d.dof_on ? kWhite : kFaint;
        label("FOCUS", mid, d.dof_on ? kSoft : kFaint);
        constexpr int kCells = 23;
        const float f = std::clamp(d.focus_stops / 4.0f, -1.0f, 1.0f);
        const int n = int(std::lround(std::fabs(f) * float(kCells / 2)));
        const int centre = kCells / 2;
        gauge(c, m, gx, mid - 0.5f * gh, r.gauge_w, gh, kCells, d.dof_on ? (f >= 0 ? centre + 1 : centre - n) : 0,
              d.dof_on ? (f >= 0 ? centre + 1 + n : centre) : 0, centre, kOrange);
        if (!d.dof_on) value_text(c, m, "--", vx, mid, kFaint);
        else value_text(c, m, d.focus_car ? "CAR" : "CENTRE", vx, mid, col);
    }
    rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    c.store(rgba.data(), w, 0, 0);
}

// ---- drawing ----

void PhotoHud::quad(b3gfx::Device& dev, Plate& p, int w, int h, float x0, float y0, float op, std::vector<b3gfx::DrawItem>& out) {
    if (!(op > 0) || p.tex == b3gfx::kNone) return;
    const float x1 = x0 + float(p.w), y1 = y0 + float(p.h);
    // the 2D stage's half-intensity GS frame, as the notices: RGB x 64, alpha x 128 = 1
    const std::uint32_t rgba = 64u | 64u << 8 | 64u << 16 | std::uint32_t(std::lround(std::clamp(op, 0.0f, 1.0f) * 128.0f)) << 24;
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
    dev.update_vertex_buffer(p.vb, vs, 4);
    dev.update_index_buffer(p.ib, is, 6);
    b3gfx::DrawItem it;
    it.overlay = true;
    it.shading = b3gfx::Shading::kTrack;
    it.track_type = 0;
    it.textured = true;
    it.texture = p.tex;
    it.gs_alpha = 0x44;
    it.overlay_blend = true;
    it.point_sample = true;
    const float m[16] = {2.0f / float(w), 0, 0, 0, 0, -2.0f / float(h), 0, 0, 0, 0, 1, 0, -1, 1, 0.5f, 1};
    std::memcpy(it.model, m, sizeof m);
    it.vertices = p.vb;
    it.indices = p.ib;
    it.first_index = 0;
    it.index_count = 6;
    out.push_back(it);
}

void PhotoHud::draw_list(b3gfx::Device& dev, int w, int h, float dt, PhotoUi ui, const PhotoHudData& data, PromptDevice device,
                         const PhotoKeys& keys, int hud_size, std::vector<b3gfx::DrawItem>& out) {
    if (epoch_.changed(dev)) {   // a track load released every id (the game's own took them): make the pictures again
        readouts_.tex = strip_.tex = b3gfx::kNone;
        readouts_.vb = readouts_.ib = strip_.vb = strip_.ib = b3gfx::kNone;
        built_readouts_ = built_strip_ = false;
    }
    if (w <= 0 || h <= 0) return;
    // the fades: 0.2 s each way
    auto ease = [dt](float& v, float target) {
        const float step = std::max(0.0f, dt) / 0.2f;
        v = v < target ? std::min(target, v + step) : std::max(target, v - step);
    };
    ease(readouts_.shown, ui != PhotoUi::kHidden ? 1.0f : 0.0f);
    ease(strip_.shown, ui == PhotoUi::kFull ? 1.0f : 0.0f);
    // the layout of the whole UI (the readouts' size does not change as the strip fades in and out)
    if (layout_w_ != w || layout_h_ != h || layout_size_ != hud_size || layout_device_ != device || !(layout_keys_ == keys)) {
        layout_ = layout(w, h, hud_size, device, keys, PhotoUi::kFull);
        layout_w_ = w;
        layout_h_ = h;
        layout_size_ = hud_size;
        layout_device_ = device;
        layout_keys_ = keys;
    }
    auto ease_out = [](float t) { return t * t * (3.0f - 2.0f * t); };
    if (!layout_.readouts.empty() && readouts_.shown > 0) {
        if (!built_readouts_ || !built_data_.same_picture(data) || !(built_layout_ == layout_) || built_device_ != device || !(built_keys_ == keys)) {
            std::vector<std::uint8_t> px;
            picture_readouts(layout_, data, device, keys, readouts_.w, readouts_.h, px);
            dev.update_texture(readouts_.tex, readouts_.w, readouts_.h, px.data());
            built_data_ = data;
            built_layout_ = layout_;
            built_device_ = device;
            built_keys_ = keys;
            built_readouts_ = true;
        }
        const float e = ease_out(readouts_.shown), s = layout_.scale;
        quad(dev, readouts_, w, h, layout_.readouts.x + (1 - e) * 18 * s, layout_.readouts.y, e, out);
    }
    if (!layout_.strip.empty() && strip_.shown > 0) {
        if (!built_strip_ || !(built_strip_layout_ == layout_) || built_strip_device_ != device || !(built_strip_keys_ == keys)) {
            std::vector<std::uint8_t> px;
            picture_strip(layout_, device, keys, strip_.w, strip_.h, px);
            dev.update_texture(strip_.tex, strip_.w, strip_.h, px.data());
            built_strip_layout_ = layout_;
            built_strip_device_ = device;
            built_strip_keys_ = keys;
            built_strip_ = true;
        }
        const float e = ease_out(strip_.shown), s = layout_.scale;
        quad(dev, strip_, w, h, layout_.strip.x, layout_.strip.y + (1 - e) * 14 * s, e, out);
    }
}

}  // namespace b3replay
