// Button prompts for the device in use (mac5): src/b3replay/prompts.h.
#include "prompts.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "glyphdraw.h"

namespace b3replay {

using b3native::input::Ds2;
using b3native::input::KeyBinding;
using b3native::input::KeyCtx;
namespace sc = b3native::input::sc;
using glyph::Canvas;
using glyph::Line;
using glyph::Pt;

const char* prompt_device_name(PromptDevice d) {
    switch (d) {
    case PromptDevice::kKeyboard: return "keyboard";
    case PromptDevice::kPlayStation: return "PlayStation";
    case PromptDevice::kXbox: return "Xbox";
    case PromptDevice::kNintendo: return "Nintendo";
    case PromptDevice::kGeneric: return "generic";
    case PromptDevice::kPlayStationModern: return "PS5";
    }
    return "?";
}

bool prompt_device_parse(std::string_view s, PromptDevice& d) {
    auto same = [&](std::string_view n) {
        if (n.size() != s.size()) return false;
        for (std::size_t i = 0; i < n.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(n[i])) != std::tolower(static_cast<unsigned char>(s[i]))) return false;
        return true;
    };
    for (PromptDevice c : {PromptDevice::kKeyboard, PromptDevice::kPlayStation, PromptDevice::kXbox, PromptDevice::kNintendo,
                           PromptDevice::kGeneric, PromptDevice::kPlayStationModern})
        if (same(prompt_device_name(c))) { d = c; return true; }
    if (same("ps4") || same("playstation5") || same("playstation4")) { d = PromptDevice::kPlayStationModern; return true; }
    if (same("ps2") || same("ps3")) { d = PromptDevice::kPlayStation; return true; }
    return false;
}

PromptDevice prompt_device_for_pad(int type, int label) {
    if (type == kSdlTypePS4 || type == kSdlTypePS5) return PromptDevice::kPlayStationModern;
    if (type == kSdlTypePS3) return PromptDevice::kPlayStation;
    switch (label) {
    case kSdlLabelCross: return PromptDevice::kPlayStation;   // a PlayStation-style pad of no known model: the game's own
    case kSdlLabelB: return PromptDevice::kNintendo;
    case kSdlLabelA: return PromptDevice::kXbox;
    default: return PromptDevice::kGeneric;
    }
}

PromptDevice prompt_resolve(int mode, PromptDevice tracked) {
    switch (PromptMode(std::clamp(mode, 0, kPromptModes - 1))) {
    case PromptMode::kPlayStation: return PromptDevice::kPlayStation;
    case PromptMode::kXbox: return PromptDevice::kXbox;
    case PromptMode::kNintendo: return PromptDevice::kNintendo;
    case PromptMode::kGeneric: return PromptDevice::kGeneric;
    case PromptMode::kKeyboard: return PromptDevice::kKeyboard;
    case PromptMode::kPlayStationModern: return PromptDevice::kPlayStationModern;
    case PromptMode::kAuto: break;
    }
    return tracked;
}

const char* pad_button_name(PadButton b) {
    static const char* const n[] = {"cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3", "start", "select"};
    return std::size_t(b) < sizeof n / sizeof n[0] ? n[std::size_t(b)] : "?";
}

namespace {

Ds2 ds2_of(PadButton b) {
    switch (b) {
    case PadButton::kCross: return Ds2::Cross;
    case PadButton::kCircle: return Ds2::Circle;
    case PadButton::kSquare: return Ds2::Square;
    case PadButton::kTriangle: return Ds2::Triangle;
    case PadButton::kL1: return Ds2::L1;
    case PadButton::kR1: return Ds2::R1;
    case PadButton::kL2: return Ds2::L2;
    case PadButton::kR2: return Ds2::R2;
    case PadButton::kL3: return Ds2::L3;
    case PadButton::kR3: return Ds2::R3;
    case PadButton::kStart: return Ds2::Start;
    case PadButton::kSelect: return Ds2::Select;
    default: return Ds2::Count;
    }
}

// The keys a player reaches for first [G: the project's choice, docs/mac.md §5's layout]: the named keys, then WASD, then
// the rest; a key not listed comes after these, in the map's order.
constexpr std::uint16_t kKeyRank[] = {sc::Return, sc::KpEnter, sc::Escape, sc::Backspace, sc::Tab, sc::W, sc::S, sc::A, sc::D,
                                      sc::Space, sc::Q, sc::E, sc::R, sc::C, sc::X, sc::Z, sc::H, sc::N1, sc::N3,
                                      sc::Up, sc::Down, sc::Left, sc::Right, sc::LShift, sc::RShift};
int rank_of(std::uint16_t k) {
    for (std::size_t i = 0; i < sizeof kKeyRank / sizeof kKeyRank[0]; ++i)
        if (kKeyRank[i] == k) return int(i);
    return 1000;
}

}  // namespace

PromptKeys prompt_keys(const std::vector<KeyBinding>& map, bool driving) {
    PromptKeys k;
    const KeyCtx now = driving ? KeyCtx::Drive : KeyCtx::Menu;
    for (int b = 0; b < kPadButtons; ++b) {
        const Ds2 c = ds2_of(PadButton(b));
        int best = -1, best_score = 1 << 30;
        for (std::size_t i = 0; i < map.size(); ++i) {
            const KeyBinding& kb = map[i];
            if (kb.control != c) continue;
            // a binding of this context first (driving: W accelerates, not Enter), then any context; a binding of the
            // other context only when nothing else drives the button (Square has no menu key in the default map)
            const int ctx = kb.ctx == now ? (driving ? 0 : 1) : kb.ctx == KeyCtx::Any ? 1 : 2;
            const int score = ctx * 100000 + rank_of(kb.scancode) * 100 + int(std::min<std::size_t>(i, 99));
            if (score < best_score) { best_score = score; best = int(i); }
        }
        k.scancode[std::size_t(b)] = best >= 0 ? map[std::size_t(best)].scancode : 0;
    }
    return k;
}

PromptKeys prompt_pad_glyphs(const std::array<Ds2, 16>& glyphs) {
    PromptKeys k;
    for (int b = 0; b < kPadButtons; ++b) {
        const Ds2 at = glyphs[std::size_t(ds2_of(PadButton(b)))];
        for (int g = 0; g < kPadButtons; ++g)
            if (ds2_of(PadButton(g)) == at && g != b) k.pad[std::size_t(b)] = std::uint8_t(g + 1);
    }
    return k;
}

bool prompts_substituted(PromptDevice d, const PromptKeys& keys) { return d != PromptDevice::kPlayStation || keys.pad_remapped(); }

std::string key_cap_name(std::uint16_t s) {
    if (s >= 4 && s <= 29) return std::string(1, char('A' + (s - 4)));
    if (s >= 30 && s <= 38) return std::string(1, char('1' + (s - 30)));
    if (s == 39) return "0";
    if (s >= 58 && s <= 69) return "F" + std::to_string(s - 57);
    if (s >= 89 && s <= 97) return "NUM" + std::to_string(s - 88);
    switch (s) {
    case 0: return "";
    case 40: case 88: return "ENTER";
    case 41: return "ESC";
    case 42: return "BKSP";
    case 43: return "TAB";
    case 44: return "SPACE";
    case 45: return "-";
    case 46: return "=";
    case 54: return ",";
    case 55: return ".";
    case 56: return "/";
    case 57: return "CAPS";
    case 73: return "INS";
    case 74: return "HOME";
    case 75: return "PGUP";
    case 76: return "DEL";
    case 77: return "END";
    case 78: return "PGDN";
    case 79: return "RIGHT";
    case 80: return "LEFT";
    case 81: return "DOWN";
    case 82: return "UP";
    case 98: return "NUM0";
    case 224: case 228: return "CTRL";
    case 225: case 229: return "SHIFT";
    case 226: case 230: return "ALT";
    default: return "?";
    }
}

std::string prompt_label(PromptDevice d, PadButton b, const PromptKeys& keys) {
    static const char* const ps[] = {"CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "L1", "R1", "L2", "R2", "L3", "R3", "START", "SELECT"};
    // Xbox: SDL's positions (the pad's South = Cross) carry the Xbox labels A / B / X / Y
    static const char* const xb[] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "MENU", "VIEW"};
    // Nintendo: the same positions carry B / A / Y / X (South is B)
    static const char* const nt[] = {"B", "A", "Y", "X", "L", "R", "ZL", "ZR", "LS", "RS", "+", "-"};
    static const char* const gn[] = {"SOUTH", "EAST", "WEST", "NORTH", "L1", "R1", "L2", "R2", "L3", "R3", "START", "SELECT"};
    // PS4 / PS5: Start became OPTIONS, Select CREATE (PS5; SHARE on the PS4: never drawn by the game, prompts.h)
    static const char* const pm[] = {"CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "L1", "R1", "L2", "R2", "L3", "R3", "OPTIONS", "CREATE"};
    if (std::size_t(b) >= std::size_t(kPadButtons)) return "?";
    // remap1: a pad's label is the one on the position bound to the button
    const std::size_t i = d == PromptDevice::kKeyboard ? std::size_t(b) : std::size_t(keys.shown(b));
    switch (d) {
    case PromptDevice::kPlayStation: return ps[i];
    case PromptDevice::kXbox: return xb[i];
    case PromptDevice::kNintendo: return nt[i];
    case PromptDevice::kGeneric: return gn[i];
    case PromptDevice::kPlayStationModern: return pm[i];
    case PromptDevice::kKeyboard: return keys.scancode[i] ? key_cap_name(keys.scancode[i]) : "?";
    }
    return "?";
}

// ---- automatic switching ----

void PromptDeviceTracker::set(PromptDevice d, double t) {
    if (!known_ || active_ != d) switched_at_ = t;
    active_ = d;
    known_ = true;
    has_pending_ = false;
}

void PromptDeviceTracker::press(PromptDevice d, double t) { set(d, t); }

void PromptDeviceTracker::axis(PromptDevice d, bool past_half, double t) {
    if (known_ && d == active_) {
        if (has_pending_ && pending_ == d) has_pending_ = false;
        return;
    }
    if (!past_half) {
        if (has_pending_ && pending_ == d) has_pending_ = false;
        return;
    }
    if (!has_pending_ || pending_ != d) {
        has_pending_ = true;
        pending_ = d;
        pending_since_ = t;
        return;
    }
    if (t - pending_since_ >= kAxisHold && t - switched_at_ >= kAxisSettle) set(d, t);
}

void PromptDeviceTracker::pads_gone(bool keyboard_used, double t) {
    has_pending_ = false;
    if (!known_ || active_ == PromptDevice::kKeyboard) return;
    if (keyboard_used) set(PromptDevice::kKeyboard, t);
    else known_ = false;   // back to the game's own prompts
}

// ---- the glyphs ----

namespace {

constexpr float kWhite[4] = {0.95f, 0.95f, 0.95f, 1.0f};
constexpr float kDim[4] = {0.72f, 0.74f, 0.78f, 1.0f};
constexpr float kShade[4] = {0.0f, 0.0f, 0.0f, 0.45f};

// A cell: its pixel box and the game's unit there (the cell is `units_w` x `units_h` of the game's 2D units)
struct Box {
    float x0, y0, w, h, u;
    float cx() const { return x0 + 0.5f * w; }
    float cy() const { return y0 + 0.5f * h; }
};

// `s` centred at (cx, cy), at most `max_w` wide and `cap` high
void centred_text(Canvas& c, std::string_view s, float cx, float cy, float max_w, float cap, float t_ratio, const float (&rgba)[4]) {
    float t = std::max(1.0f, cap * t_ratio);
    float w = glyph::text_width(s, cap, t);
    if (w > max_w && w > 0) {
        cap *= max_w / w;
        t = std::max(1.0f, cap * t_ratio);
        w = glyph::text_width(s, cap, t);
    }
    std::vector<Line> l;
    glyph::text(s, cx - 0.5f * w, cy - 0.5f * cap, cap, t, l);
    c.strokes(l, t, rgba);
}

// An arrow head pointing along (dx, dy) with its tip at (x, y), `len` long
void arrow_head(Canvas& c, float x, float y, float dx, float dy, float len, const float (&rgba)[4]) {
    const float nx = -dy, ny = dx;
    c.polygon({{x, y}, {x - dx * len + nx * len * 0.62f, y - dy * len + ny * len * 0.62f}, {x - dx * len - nx * len * 0.62f, y - dy * len - ny * len * 0.62f}}, rgba);
}

// A key's name on its cap: arrows, Enter, Backspace, Space and Shift as symbols, the rest as text
void key_label(Canvas& c, const Box& b, std::uint16_t s, float max_cap) {
    const float u = b.u, cx = b.cx(), cy = b.cy(), t = std::max(1.0f, 1.7f * u);
    std::vector<Line> l;
    switch (s) {
    case sc::Up: case sc::Down: case sc::Left: case sc::Right: {
        const float dx = s == sc::Left ? -1.0f : s == sc::Right ? 1.0f : 0.0f, dy = s == sc::Up ? -1.0f : s == sc::Down ? 1.0f : 0.0f;
        const float len = std::min(4.6f * u, 0.3f * std::min(b.w, b.h));
        c.polygon({{cx + dx * len, cy + dy * len}, {cx - dx * len * 0.55f - dy * len, cy - dy * len * 0.55f + dx * len},
                   {cx - dx * len * 0.55f + dy * len, cy - dy * len * 0.55f - dx * len}}, kWhite);
        return;
    }
    case sc::Return: case sc::KpEnter: {   // the return arrow: down from the top right, then left
        const float r = std::min(4.6f * u, 0.3f * b.h);
        l.push_back({{cx + r, cy - r}, {cx + r, cy + 0.3f * r}, {cx - 0.45f * r, cy + 0.3f * r}});
        c.strokes(l, t, kWhite);
        arrow_head(c, cx - r * 1.05f, cy + 0.3f * r, -1, 0, 0.75f * r, kWhite);
        return;
    }
    case sc::Backspace: {   // a long arrow to the left
        const float r = std::min(4.8f * u, 0.32f * b.w);
        l.push_back({{cx + r, cy}, {cx - 0.4f * r, cy}});
        c.strokes(l, t, kWhite);
        arrow_head(c, cx - r, cy, -1, 0, 0.7f * r, kWhite);
        return;
    }
    case sc::Space: {   // the space bar's open box
        const float r = std::min(5.0f * u, 0.34f * b.w);
        l.push_back({{cx - r, cy - 0.3f * r}, {cx - r, cy + 0.35f * r}, {cx + r, cy + 0.35f * r}, {cx + r, cy - 0.3f * r}});
        c.strokes(l, t, kWhite);
        return;
    }
    case sc::LShift: case sc::RShift: {   // the hollow up arrow
        const float r = std::min(4.6f * u, 0.3f * b.h);
        l.push_back({{cx, cy - r}, {cx + r, cy}, {cx + 0.45f * r, cy}, {cx + 0.45f * r, cy + r}, {cx - 0.45f * r, cy + r},
                     {cx - 0.45f * r, cy}, {cx - r, cy}, {cx, cy - r}});
        c.strokes(l, std::max(1.0f, 1.4f * u), kWhite);
        return;
    }
    default: break;
    }
    const std::string name = s ? key_cap_name(s) : "?";
    centred_text(c, name, cx, cy, b.w - 7.0f * u, max_cap, 0.19f, kWhite);
}

// The base of a round button (a face button, a stick): a dark disc in a white ring, as the game's ring
void round_base(Canvas& c, float cx, float cy, float u, float r_max = 1e30f) {
    // 1.2.1: at most r_max (a stick's ring drawn in a shoulder picture's lower cell: R2's Crashbreaker on R3)
    const float r = std::min(9.4f * u, r_max);
    c.disc(cx, cy, r, kShade);
    c.ring(cx, cy, r, std::max(1.0f, 1.7f * u * r / (9.4f * u)), kWhite);
}

// A key cap filling the box (a little in from its edges)
void key_base(Canvas& c, const Box& b) {
    const float u = b.u, hx = 0.5f * b.w - 1.3f * u, hy = 0.5f * b.h - 1.3f * u, r = std::min(3.6f * u, hy);
    c.box(b.cx(), b.cy(), hx, hy, r, kShade);
    c.box_outline(b.cx(), b.cy(), hx, hy, r, std::max(1.0f, 1.6f * u), kWhite);
    // the cap's lower lip
    std::vector<Line> l{{{b.cx() - hx + r, b.cy() + hy - 2.6f * u}, {b.cx() + hx - r, b.cy() + hy - 2.6f * u}}};
    const float lip[4] = {0.95f, 0.95f, 0.95f, 0.35f};
    c.strokes(l, std::max(1.0f, 0.9f * u), lip);
}

// A pill (shoulder buttons, the generic Start / Select): `r` its corner radius in units
void pill_base(Canvas& c, const Box& b, float r_units) {
    const float u = b.u, hx = 0.5f * b.w - 1.3f * u, hy = 0.5f * b.h - 1.3f * u, r = std::min(r_units * u, hy);
    c.box(b.cx(), b.cy(), hx, hy, r, kShade);
    c.box_outline(b.cx(), b.cy(), hx, hy, r, std::max(1.0f, 1.6f * u), kWhite);
}

bool face(PadButton b) { return b == PadButton::kCross || b == PadButton::kCircle || b == PadButton::kSquare || b == PadButton::kTriangle; }
bool shoulder(PadButton b) { return b == PadButton::kL1 || b == PadButton::kR1 || b == PadButton::kL2 || b == PadButton::kR2; }
bool wide(PadButton b) { return b == PadButton::kStart || b == PadButton::kSelect; }

void paint_base(Canvas& c, const Box& b, PromptDevice d, PadButton pb) {
    if (d == PromptDevice::kKeyboard) { key_base(c, b); return; }
    if (shoulder(pb)) { pill_base(c, b, pb == PadButton::kL2 || pb == PadButton::kR2 ? 2.6f : 6.5f); return; }
    if (wide(pb) && d == PromptDevice::kGeneric) { pill_base(c, b, 9.0f); return; }
    if (wide(pb) && d == PromptDevice::kPlayStationModern) {   // OPTIONS / CREATE: the pill button filling the box
        // (2026-11-09: no name under it -- the word made the glyph too small)
        const float u = b.u, hx = 0.46f * b.w, hy = std::min(0.40f * b.h, 0.62f * hx);
        c.box(b.cx(), b.cy(), hx, hy, hy, kShade);
        c.box_outline(b.cx(), b.cy(), hx, hy, hy, std::max(1.0f, 1.5f * u), kWhite);
        return;
    }
    round_base(c, b.cx(), b.cy(), b.u, 0.5f * std::min(b.w, b.h) - 0.6f * b.u);
}

void paint_symbol(Canvas& c, const Box& b, PromptDevice d, PadButton pb, const PromptKeys& keys) {
    const float u = b.u, cx = b.cx(), cy = b.cy();
    if (d == PromptDevice::kKeyboard) {
        key_label(c, b, keys.scancode[std::size_t(pb)], (wide(pb) || shoulder(pb) ? 7.5f : 8.0f) * u);
        return;
    }
    const std::string label = prompt_label(d, pb);
    if (wide(pb) && d == PromptDevice::kPlayStationModern) {   // the pill's three bars (OPTIONS; CREATE: a spark), no name
        const float hx = 0.46f * b.w, hy = std::min(0.40f * b.h, 0.62f * hx);
        const float bx = 0.42f * hx, gap = 0.36f * hy, t = std::max(1.0f, 0.17f * hy);
        std::vector<Line> l;
        if (pb == PadButton::kStart)
            for (int i = -1; i <= 1; ++i) l.push_back({{cx - bx, cy + float(i) * gap}, {cx + bx, cy + float(i) * gap}});
        else
            for (int i = 0; i < 3; ++i) {
                const float a = float(i) * 3.14159265f / 3.0f, dx = std::cos(a) * 0.55f * hy, dy = std::sin(a) * 0.55f * hy;
                l.push_back({{cx - dx, cy - dy}, {cx + dx, cy + dy}});
            }
        c.strokes(l, t, kWhite);
        (void)label;
        return;
    }
    if (face(pb) && (d == PromptDevice::kPlayStation || d == PromptDevice::kPlayStationModern)) {   // our own drawing of the four symbols (the skip hint's; the
        const float t = std::max(1.2f, 1.9f * u);          // game's prompts keep the game's own pictures)
        static const float col[4][4] = {{0.55f, 0.71f, 0.95f, 1}, {0.95f, 0.45f, 0.52f, 1}, {0.93f, 0.55f, 0.85f, 1}, {0.35f, 0.80f, 0.72f, 1}};
        const float(&k)[4] = col[int(pb)];
        std::vector<Line> l;
        const float r = 4.2f * u;
        if (pb == PadButton::kCross) { l.push_back({{cx - r, cy - r}, {cx + r, cy + r}}); l.push_back({{cx - r, cy + r}, {cx + r, cy - r}}); }
        else if (pb == PadButton::kSquare) l.push_back({{cx - r, cy - r}, {cx + r, cy - r}, {cx + r, cy + r}, {cx - r, cy + r}, {cx - r, cy - r}});
        else if (pb == PadButton::kTriangle) l.push_back({{cx, cy - 1.15f * r}, {cx + 1.1f * r, cy + 0.75f * r}, {cx - 1.1f * r, cy + 0.75f * r}, {cx, cy - 1.15f * r}});
        if (pb == PadButton::kCircle) c.ring(cx, cy, 1.05f * r, t, k);
        else c.strokes(l, t, k);
        return;
    }
    if (face(pb)) {
        if (d == PromptDevice::kGeneric) {   // the four positions, the one meant filled
            static const float off[4][2] = {{0, 1}, {1, 0}, {-1, 0}, {0, -1}};   // South, East, West, North
            const int k = int(pb);
            for (int i = 0; i < 4; ++i) {
                const float x = cx + off[i][0] * 4.3f * u, y = cy + off[i][1] * 4.3f * u;
                if (i == k) c.disc(x, y, 2.3f * u, kWhite);
                else c.ring(x, y, 1.8f * u, std::max(1.0f, 0.9f * u), kDim);
            }
            return;
        }
        // Xbox: A green, B red, X blue, Y yellow; Nintendo: white letters
        static const float xb[4][4] = {{0.45f, 0.80f, 0.28f, 1}, {0.93f, 0.30f, 0.25f, 1}, {0.30f, 0.58f, 0.97f, 1}, {0.98f, 0.80f, 0.20f, 1}};
        const float(&col)[4] = d == PromptDevice::kXbox ? xb[int(pb)] : kWhite;
        centred_text(c, label, cx, cy, 13.0f * u, 9.6f * u, 0.22f, col);
        return;
    }
    std::vector<Line> l;
    const float t = std::max(1.0f, 1.6f * u);
    if (wide(pb) && d == PromptDevice::kXbox) {
        if (pb == PadButton::kStart) {   // Menu: three bars
            for (int i = -1; i <= 1; ++i) l.push_back({{cx - 3.6f * u, cy + float(i) * 2.9f * u}, {cx + 3.6f * u, cy + float(i) * 2.9f * u}});
            c.strokes(l, t, kWhite);
        } else {                         // View: two overlapping windows
            c.box_outline(cx - 1.3f * u, cy - 1.1f * u, 2.8f * u, 2.2f * u, 0.8f * u, std::max(1.0f, 1.2f * u), kWhite);
            c.box(cx + 1.3f * u, cy + 1.1f * u, 2.8f * u, 2.2f * u, 0.8f * u, kWhite);
        }
        return;
    }
    if (wide(pb) && d == PromptDevice::kNintendo) {   // + / -
        l.push_back({{cx - 4.0f * u, cy}, {cx + 4.0f * u, cy}});
        if (pb == PadButton::kStart) l.push_back({{cx, cy - 4.0f * u}, {cx, cy + 4.0f * u}});
        c.strokes(l, std::max(1.0f, 2.1f * u), kWhite);
        return;
    }
    // text: shoulders, the sticks, the generic Start / Select
    const float max_w = (wide(pb) || shoulder(pb) ? b.w - 8.0f * u : 13.0f * u);
    centred_text(c, label, cx, cy, max_w, (shoulder(pb) ? 7.0f : wide(pb) ? 6.5f : 8.0f) * u, 0.2f, kWhite);
}

// A d-pad arm in the game's cell orientation: its inner end (the pad's centre) at v 0 (up / down arm) or u 0 (left /
// right arm), its outer end at 1. Pads: one arm of a plus-shaped pad's outline (the four make the plus); keyboard: an
// arrow key's cap on the outer part, its arrow pointing out.
void paint_arm(Canvas& c, const Box& b, PromptDevice d, bool lr) {
    const float u = b.u;
    // in arm space: a = along the arm from the centre (0) outward, s = across it (0 at the arm's middle)
    const float len = lr ? b.w : b.h, across_c = lr ? b.cy() : b.cx();
    auto P = [&](float a, float s) { return lr ? Pt{b.x0 + a, across_c + s} : Pt{across_c + s, b.y0 + a}; };
    if (d == PromptDevice::kKeyboard) {
        const float a0 = 0.30f * len, a1 = len - 0.8f * u, half = std::min(5.0f * u, 0.5f * (lr ? b.h : b.w) - 0.8f * u);
        const Pt p0 = P(a0, -half), p1 = P(a1, half);
        Box cap{std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::fabs(p1.x - p0.x), std::fabs(p1.y - p0.y), u * 0.75f};
        key_base(c, cap);
        const float mid = 0.5f * (a0 + a1), r = 0.24f * (a1 - a0);
        c.polygon({P(mid + r, 0), P(mid - 0.6f * r, -r), P(mid - 0.6f * r, r)}, kWhite);
        return;
    }
    const float half = 4.2f * u, end = len - 0.9f * u, r = 1.6f * u;
    // the dark fill of the arm's outer part (the centre square is left to the plus's look: it stays clear)
    std::vector<Pt> fillp{P(half, -half), P(end - r, -half), P(end, -half + r), P(end, half - r), P(end - r, half), P(half, half)};
    c.polygon(fillp, kShade);
    std::vector<Line> l{{P(half, -half), P(end - r, -half), P(end, -half + r), P(end, half - r), P(end - r, half), P(half, half)}};
    c.strokes(l, std::max(1.0f, 1.6f * u), kWhite);
    // a small direction mark
    const float m = 0.5f * (half + end), k = 1.7f * u;
    c.polygon({P(m + k, 0), P(m - 0.7f * k, -k), P(m - 0.7f * k, k)}, kDim);
}

// The cells' sizes in the game's units [C the button sizes: D_004C94A0 (21 x 21, Start 33 x 21, the shoulders 35 x 15);
// the d-pad's arm quads 14 x 14.9 and 16 x 13.1 (capture)]
void cell_units(PadButton b, float& w, float& h) {
    if (shoulder(b)) { w = 35; h = 15; return; }
    if (wide(b)) { w = 33; h = 21; return; }
    w = 21; h = 21;
}

}  // namespace

float paint_prompt_glyph(glyph::Canvas& c, float x0, float y0, float h, PromptDevice d, PadButton bound, const PromptKeys& keys) {
    const PadButton b = d == PromptDevice::kKeyboard ? bound : keys.shown(bound);   // remap1: the glyph of the position bound
    float uw = 0, uh = 0;
    cell_units(b, uw, uh);
    const float w = h * uw / uh;
    Canvas layer(int(std::ceil(w)), int(std::ceil(h)));
    const Box box{0, 0, w, h, h / uh};
    paint_base(layer, box, d, b);
    paint_symbol(layer, box, d, b, keys);
    // the glyph's canvas over `c` at (x0, y0) (whole pixels; premultiplied "over")
    const int ox = int(std::lround(x0)), oy = int(std::lround(y0));
    for (int y = 0; y < layer.h; ++y)
        for (int x = 0; x < layer.w; ++x) {
            const int X = ox + x, Y = oy + y;
            if (X < 0 || Y < 0 || X >= c.w || Y >= c.h) continue;
            const auto& s = layer.px[std::size_t(y) * std::size_t(layer.w) + std::size_t(x)];
            auto& o = c.px[std::size_t(Y) * std::size_t(c.w) + std::size_t(X)];
            for (std::size_t k = 0; k < 3; ++k) o[k] = s[k] + o[k] * (1 - s[3]);
            o[3] = s[3] + o[3] * (1 - s[3]);
        }
    return w;
}

void PromptAtlas::build(PromptDevice d, const PromptKeys& keys, int window_h) {
    device_ = d;
    keys_ = keys;
    window_h_ = window_h;
    // a unit in pixels: the window's 2D scale (448 units high), at least 2 (the glyphs stay legible when minified)
    const float u = std::max(2.0f, 1.15f * float(window_h) / 448.0f);
    struct Want { float uw, uh; int kind; };   // kind: button * 2 + layer, or -1 / -2 for the arms
    std::vector<Want> want;
    for (int b = 0; b < kPadButtons; ++b) {
        float uw = 0, uh = 0;
        cell_units(PadButton(b), uw, uh);
        want.push_back({uw, uh, b * 2});
        want.push_back({uw, uh, b * 2 + 1});
    }
    want.push_back({14.0f, 14.9f, -1});
    want.push_back({16.0f, 13.1f, -2});
    // shelf packing, 2-pixel gaps (no bleeding under bilinear sampling)
    const int gap = 2, row_w = int(std::ceil(4.0f * 35.0f * u)) + 5 * gap;
    struct Placed { int x, y, w, h; };
    std::vector<Placed> at(want.size());
    int x = gap, y = gap, row_h = 0, max_w = 0;
    for (std::size_t i = 0; i < want.size(); ++i) {
        const int cw = int(std::ceil(want[i].uw * u)), ch = int(std::ceil(want[i].uh * u));
        if (x + cw + gap > row_w) { x = gap; y += row_h + gap; row_h = 0; }
        at[i] = {x, y, cw, ch};
        x += cw + gap;
        row_h = std::max(row_h, ch);
        max_w = std::max(max_w, x);
    }
    w_ = max_w;
    h_ = y + row_h + gap;
    rgba_.assign(std::size_t(w_) * std::size_t(h_) * 4, 0);
    for (std::size_t i = 0; i < want.size(); ++i) {
        const Placed& p = at[i];
        Canvas c(p.w, p.h);
        const Box box{0, 0, float(p.w), float(p.h), float(p.h) / want[i].uh};
        const Rect r{float(p.x) / float(w_), float(p.y) / float(h_), float(p.x + p.w) / float(w_), float(p.y + p.h) / float(h_)};
        if (want[i].kind < 0) {
            const bool lr = want[i].kind == -2;
            paint_arm(c, box, d, lr);
            arms_[lr ? 1 : 0] = r;
        } else {
            const PadButton b = PadButton(want[i].kind / 2);
            // 1.2.1: the base too is the bound position's (a shoulder picture bound to a stick click draws a ring, not a pill)
            if (want[i].kind % 2 == 0) paint_base(c, box, d, d == PromptDevice::kKeyboard ? b : keys.shown(b));
            else paint_symbol(c, box, d, d == PromptDevice::kKeyboard ? b : keys.shown(b), keys);   // remap1: the bound position's symbol
            cells_[std::size_t(want[i].kind)] = r;
        }
        c.store(rgba_.data(), w_, p.x, p.y);
        if (want[i].kind >= 0) {   // the cell's ink
            int ix0 = p.w, ix1 = -1, iy0 = p.h, iy1 = -1;
            for (int yy = 0; yy < p.h; ++yy)
                for (int xx = 0; xx < p.w; ++xx)
                    if (rgba_[4 * (std::size_t(p.y + yy) * std::size_t(w_) + std::size_t(p.x + xx)) + 3] > 32) {
                        ix0 = std::min(ix0, xx); ix1 = std::max(ix1, xx); iy0 = std::min(iy0, yy); iy1 = std::max(iy1, yy);
                    }
            ink_[std::size_t(want[i].kind)] = ix1 < 0 ? r
                : Rect{float(p.x + ix0) / float(w_), float(p.y + iy0) / float(h_), float(p.x + ix1 + 1) / float(w_), float(p.y + iy1 + 1) / float(h_)};
        }
    }
}

// ---- the substitution ----

namespace {

// A piece of a game texture: its texel rectangle, what it shows, and how much its quad grows to the button's box
struct Piece {
    float x, y, w, h;
    int button;        // PadButton, or -1 / -2: the d-pad's up-down / left-right arm, -3: base or symbol by quad parity
    GlyphLayer layer;
    float grow;
};

// "Buttons" (128 x 64) [C the 0x4D9B10 tables, prompts.h]
const Piece kButtons[] = {
    {1, 33, 21, 21, int(PadButton::kCross), GlyphLayer::kBase, 1.0f},              // the face buttons' ring
    {15, 2, 13, 13, int(PadButton::kTriangle), GlyphLayer::kSymbol, 21.0f / 13.0f},
    {2, 3, 11, 11, int(PadButton::kCross), GlyphLayer::kSymbol, 21.0f / 11.0f},
    {16, 17, 11, 11, int(PadButton::kSquare), GlyphLayer::kSymbol, 21.0f / 11.0f},
    {1, 16, 13, 13, int(PadButton::kCircle), GlyphLayer::kSymbol, 21.0f / 13.0f},
    {67, 1, 58, 36, int(PadButton::kStart), GlyphLayer::kSymbol, 1.0f},
    {67, 39, 58, 36, int(PadButton::kStart), GlyphLayer::kBase, 1.0f},
};
// "dpad" (32 x 16)
const Piece kDpad[] = {
    {1, 0, 14, 16, -1, GlyphLayer::kBase, 1.0f},
    {15, 1, 16, 14, -2, GlyphLayer::kBase, 1.0f},
};

// The shoulder pictures (64 x 32, GLOBAL.TXD "L1Button" "L2Button" "R1Button" "R2Button"): the button they show, -1 for
// another raster. The program looks them up by name case-insensitively [C strcasecmp: the front end's "l1button" /
// "l2button", the HUD's "R2Button" (CB3HUDCrashStyle 0x3A3880), "R1Button" (Impact Time), "L2Button" (0x435AB0)]; the
// draws carry the raster's own name.
int shoulder_picture(std::string_view n) {
    if (n.size() != 8) return -1;
    std::string l(n);
    for (char& ch : l) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (l == "l1button") return int(PadButton::kL1);
    if (l == "l2button") return int(PadButton::kL2);
    if (l == "r1button") return int(PadButton::kR1);
    if (l == "r2button") return int(PadButton::kR2);
    return -1;
}

}  // namespace

int prompt_piece_button(std::string_view tex_name, int tex_w, int tex_h, const std::vector<HudVertex>& tri, std::size_t piece) {
    const std::size_t q = piece * 6;
    if (tex_w <= 0 || tex_h <= 0 || tri.size() % 6 != 0 || q + 6 > tri.size()) return -1;
    switch (shoulder_picture(tex_name)) {   // libpad2 indices (menu_input.h pad::)
    case int(PadButton::kL1): return 10;
    case int(PadButton::kR1): return 11;
    case int(PadButton::kL2): return 8;
    case int(PadButton::kR2): return 9;
    default: break;
    }
    if (tex_name != "Buttons") return -1;
    float u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
    for (std::size_t k = q; k < q + 6; ++k) {
        u0 = std::min(u0, tri[k].u); u1 = std::max(u1, tri[k].u); v0 = std::min(v0, tri[k].v); v1 = std::max(v1, tri[k].v);
    }
    const float sx = float(tex_w) / 128.0f, sy = float(tex_h) / 64.0f;
    const float tx0 = u0 * float(tex_w) / sx, tx1 = u1 * float(tex_w) / sx, ty0 = v0 * float(tex_h) / sy, ty1 = v1 * float(tex_h) / sy;
    for (const Piece& p : kButtons) {
        if (std::fabs(tx0 - p.x) >= 1.0f || std::fabs(tx1 - (p.x + p.w)) >= 1.0f || std::fabs(ty0 - p.y) >= 1.0f || std::fabs(ty1 - (p.y + p.h)) >= 1.0f)
            continue;
        if (p.layer == GlyphLayer::kBase && p.button == int(PadButton::kCross)) return -2;   // the face buttons' shared ring
        switch (PadButton(p.button)) {
        case PadButton::kCross: return 14;
        case PadButton::kCircle: return 13;
        case PadButton::kSquare: return 15;
        case PadButton::kTriangle: return 12;
        case PadButton::kStart: return 3;
        default: return -1;
        }
    }
    return -1;
}

bool prompt_texture(std::string_view n) { return n == "Buttons" || n == "dpad" || shoulder_picture(n) >= 0; }

bool prompt_substitute(std::string_view tex_name, int tex_w, int tex_h, const std::vector<HudVertex>& tri, const PromptAtlas& atlas,
                       std::vector<HudVertex>& out) {
    if (!prompts_substituted(atlas.device(), atlas.keys()) || atlas.width() <= 0 || tri.empty() || tri.size() % 6 != 0) return false;
    if (tex_w <= 0 || tex_h <= 0) return false;
    bool all_start = true, moved = false;   // remap1: the PlayStation's own pictures stay but for buttons the bindings moved
    const Piece* pieces = nullptr;
    std::size_t n = 0;
    float ref_w = 0, ref_h = 0;   // the texture size the table is in
    const int shoulder = shoulder_picture(tex_name);
    // the shoulder pictures (64 x 32): the front end's button object draws base and symbol from the button's rectangle
    // [C D_004C95C0 / D_004C9680 [5], [8]]; the HUD's image object draws the whole raster in one quad [C 0x3A3250:
    // CGt2dObjectImage::Prepare, uv (0, 0)..(1, 1), 45 x 25; the capture gives texels 1,1..64,32: the crash run's
    // "R1Button", Impact Time], whose picture covers texels 5,4 54 x 24 [D GLOBAL.TXD, alpha over 1/4, the same in all
    // four]: -4 (0..64 or 1..64 within the 1-texel tolerance), redrawn as base then symbol over that part of the quad
    const Piece shoulder_pieces[] = {{7, 5, 50, 21, -3, GlyphLayer::kBase, 1.0f}, {0.5f, 0.5f, 63.5f, 31.5f, -4, GlyphLayer::kBase, 1.0f}};
    if (tex_name == "Buttons") { pieces = kButtons; n = sizeof kButtons / sizeof kButtons[0]; ref_w = 128; ref_h = 64; }
    else if (tex_name == "dpad") { pieces = kDpad; n = 2; ref_w = 32; ref_h = 16; }
    else if (shoulder >= 0) { pieces = shoulder_pieces; n = 2; ref_w = 64; ref_h = 32; }
    else return false;
    const float sx = float(tex_w) / ref_w, sy = float(tex_h) / ref_h;   // the raster's texels per table texel
    std::vector<HudVertex> res;
    res.reserve(tri.size());
    for (std::size_t q = 0; q < tri.size(); q += 6) {
        float u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f, x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
        for (std::size_t k = q; k < q + 6; ++k) {
            const HudVertex& v = tri[k];
            u0 = std::min(u0, v.u); u1 = std::max(u1, v.u); v0 = std::min(v0, v.v); v1 = std::max(v1, v.v);
            x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
        }
        // the quad's texel rectangle, in the table's texels
        const float tx0 = u0 * float(tex_w) / sx, tx1 = u1 * float(tex_w) / sx, ty0 = v0 * float(tex_h) / sy, ty1 = v1 * float(tex_h) / sy;
        const Piece* hit = nullptr;
        for (std::size_t i = 0; i < n && !hit; ++i) {
            const Piece& p = pieces[i];
            if (std::fabs(tx0 - p.x) < 1.0f && std::fabs(tx1 - (p.x + p.w)) < 1.0f && std::fabs(ty0 - p.y) < 1.0f && std::fabs(ty1 - (p.y + p.h)) < 1.0f) hit = &p;
        }
        if (!hit || u1 <= u0 || v1 <= v0) return false;   // not a piece we know: the draw stays the game's
        // PS4 / PS5: the game's own pictures but its Start (the button object's index 2), which becomes OPTIONS
        all_start = all_start && hit->button == int(PadButton::kStart);
        if ((hit->button >= 0 && hit->layer == GlyphLayer::kSymbol && atlas.keys().pad_moved(PadButton(hit->button))) ||
            ((hit->button == -3 || hit->button == -4) && atlas.keys().pad_moved(PadButton(shoulder))))
            moved = true;
        if (hit->button == -4) {   // the whole picture: base, then symbol, over the picture's part of the quad
            // the picture's texels as fractions of the quad's texel range
            const float px0 = (5.0f - tx0) / (tx1 - tx0), px1 = (59.0f - tx0) / (tx1 - tx0);
            const float py0 = (4.0f - ty0) / (ty1 - ty0), py1 = (28.0f - ty0) / (ty1 - ty0);
            for (GlyphLayer layer : {GlyphLayer::kBase, GlyphLayer::kSymbol}) {
                const PromptAtlas::Rect r = atlas.cell(PadButton(shoulder), layer);
                for (std::size_t k = q; k < q + 6; ++k) {
                    HudVertex v = tri[k];
                    const float nu = (v.u - u0) / (u1 - u0), nv = (v.v - v0) / (v1 - v0);
                    if (std::fabs(nu - std::round(nu)) > 0.25f || std::fabs(nv - std::round(nv)) > 0.25f) return false;
                    // this corner moves along the quad's edges to the picture's corner (flips and mirrors carry over)
                    const float fu = std::round(nu) == 0 ? px0 : px1, fv = std::round(nv) == 0 ? py0 : py1;
                    const float ex = std::round(nu) == 0 ? 1.0f : -1.0f, ey = std::round(nv) == 0 ? 1.0f : -1.0f;
                    float ax = 0, ay = 0, bx = 0, by = 0;   // the quad's u and v edges from this corner
                    for (std::size_t m = q; m < q + 6; ++m) {
                        const float mu = std::round((tri[m].u - u0) / (u1 - u0)), mv = std::round((tri[m].v - v0) / (v1 - v0));
                        if (mu != std::round(nu) && mv == std::round(nv)) { ax = tri[m].x - v.x; ay = tri[m].y - v.y; }
                        if (mu == std::round(nu) && mv != std::round(nv)) { bx = tri[m].x - v.x; by = tri[m].y - v.y; }
                    }
                    const float du = ex > 0 ? fu : 1.0f - fu, dv = ey > 0 ? fv : 1.0f - fv;
                    v.x += ax * du + bx * dv;
                    v.y += ay * du + by * dv;
                    v.u = r.u0 + std::round(nu) * (r.u1 - r.u0);
                    v.v = r.v0 + std::round(nv) * (r.v1 - r.v0);
                    res.push_back(v);
                }
            }
            continue;
        }
        PromptAtlas::Rect r;
        if (hit->button == -1 || hit->button == -2) r = atlas.dpad_arm(hit->button == -2);
        else if (hit->button == -3) r = atlas.cell(PadButton(shoulder), (q / 6) % 2 == 0 ? GlyphLayer::kBase : GlyphLayer::kSymbol);
        else r = atlas.cell(PadButton(hit->button), hit->layer);
        const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
        for (std::size_t k = q; k < q + 6; ++k) {
            HudVertex v = tri[k];
            // where the vertex is in its piece (0 or 1 each way: flips and mirrors carry over), into our cell
            const float nu = (v.u - u0) / (u1 - u0), nv = (v.v - v0) / (v1 - v0);
            if (std::fabs(nu - std::round(nu)) > 0.25f || std::fabs(nv - std::round(nv)) > 0.25f) return false;
            v.u = r.u0 + std::round(nu) * (r.u1 - r.u0);
            v.v = r.v0 + std::round(nv) * (r.v1 - r.v0);
            v.x = cx + (v.x - cx) * hit->grow;
            v.y = cy + (v.y - cy) * hit->grow;
            res.push_back(v);
        }
    }
    if (atlas.device() == PromptDevice::kPlayStation && !moved) return false;
    if (atlas.device() == PromptDevice::kPlayStationModern && !all_start && !moved) return false;
    out = std::move(res);
    return true;
}

// ---- the title's "Press START button to continue" (2026-11-10) ----

namespace {

struct QuadBox { float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f; };

QuadBox quad_box(const std::vector<HudVertex>& tri, std::size_t q) {
    QuadBox b;
    for (std::size_t k = q * 6; k < q * 6 + 6; ++k) {
        b.x0 = std::min(b.x0, tri[k].x); b.x1 = std::max(b.x1, tri[k].x);
        b.y0 = std::min(b.y0, tri[k].y); b.y1 = std::max(b.y1, tri[k].y);
    }
    return b;
}

// `b` follows `a` on its row: right of it, overlapping it vertically, nearer than two letter heights
bool same_row_after(const QuadBox& a, const QuadBox& b) {
    const float h = std::max(a.y1 - a.y0, b.y1 - b.y0);
    return b.x0 > a.x0 && b.y0 < a.y1 && a.y0 < b.y1 && b.x0 - a.x1 < 2.0f * h;
}

}  // namespace

std::u16string font_quad_chars(const HostFont& f, const HudTexture& t, const std::vector<HudVertex>& tri) {
    std::u16string out;
    if (tri.empty() || tri.size() % 6 != 0 || t.w <= 0 || t.h <= 0 || t.tw <= 0 || t.th <= 0) return out;
    const float nu = float(t.tw) / float(t.w), nv = float(t.th) / float(t.h);   // a record's uv -> a vertex's
    const float tol_u = 1.0f / float(t.w), tol_v = 1.0f / float(t.h);          // a texel
    out.reserve(tri.size() / 6);
    for (std::size_t q = 0; q < tri.size() / 6; ++q) {
        float u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
        for (std::size_t k = q * 6; k < q * 6 + 6; ++k) {
            u0 = std::min(u0, tri[k].u); u1 = std::max(u1, tri[k].u); v0 = std::min(v0, tri[k].v); v1 = std::max(v1, tri[k].v);
        }
        char16_t c = 0;
        float best = 1e30f;
        for (const HostFontGlyph& g : f.glyphs) {
            if (g.u < 0.0f || g.w <= 0.0f || g.h <= 0.0f) continue;   // a space: no quad [C RenderString]
            const float du0 = std::fabs(u0 - g.u * nu), du1 = std::fabs(u1 - (g.u + g.w) * nu);
            const float dv0 = std::fabs(v0 - g.v * nv), dv1 = std::fabs(v1 - (g.v + g.h) * nv);
            if (du0 > tol_u || du1 > tol_u || dv0 > tol_v || dv1 > tol_v) continue;
            const float e = du0 + du1 + dv0 + dv1;
            if (e < best) { best = e; c = char16_t(g.code); }
        }
        out.push_back(c);
    }
    return out;
}

std::vector<TextLineMatch> find_text_line(const std::vector<HudVertex>& tri, std::u16string_view chars, std::u16string_view line,
                                          std::u16string_view word) {
    std::vector<TextLineMatch> found;
    // the line as drawn (no spaces), whether a space follows drawn letter k, and the word's place in it
    std::u16string drawn;
    std::vector<bool> space_after;
    for (char16_t c : line) {
        if (c == u' ') { if (!space_after.empty()) space_after.back() = true; continue; }
        drawn.push_back(c);
        space_after.push_back(false);
    }
    const std::size_t n = drawn.size();
    std::size_t word_at = n;
    for (std::size_t i = 0; !word.empty() && i + word.size() <= n; ++i) {
        const bool starts = i == 0 || space_after[i - 1], ends = space_after[i + word.size() - 1] || i + word.size() == n;
        if (starts && ends && std::u16string_view(drawn).substr(i, word.size()) == word) { word_at = i; break; }
    }
    if (n == 0 || word_at == n || tri.size() % 6 != 0 || chars.size() != tri.size() / 6) return found;
    for (std::size_t s = 0; s + n <= chars.size(); ++s) {
        if (chars.substr(s, n) != drawn) continue;
        std::vector<QuadBox> b(n);
        for (std::size_t k = 0; k < n; ++k) b[k] = quad_box(tri, s + k);
        bool row = true;
        float space_min = 1e30f, letter_max = -1e30f;
        for (std::size_t k = 0; k + 1 < n && row; ++k) {
            row = same_row_after(b[k], b[k + 1]);
            const float gap = b[k + 1].x0 - b[k].x1;
            if (space_after[k]) space_min = std::min(space_min, gap);
            else letter_max = std::max(letter_max, gap);
        }
        if (!row || (space_min < 1e30f && letter_max > -1e30f && !(space_min > letter_max))) continue;
        // the whole line: no glyph of its row joined on either end
        if (s > 0 && same_row_after(quad_box(tri, s - 1), b[0])) continue;
        if (s + n < chars.size() && same_row_after(b[n - 1], quad_box(tri, s + n))) continue;
        found.push_back({s, n, s + word_at, word.size()});
    }
    return found;
}

bool title_start_swap(const std::vector<HudVertex>& tri, std::u16string_view chars, const PromptAtlas& atlas, std::vector<HudVertex>& text,
                      std::vector<HudVertex>& glyph) {
    if ((atlas.device() == PromptDevice::kPlayStation && !atlas.keys().pad_moved(PadButton::kStart)) || atlas.width() <= 0) return false;
    const std::vector<TextLineMatch> m = find_text_line(tri, chars, kTitleLine, kTitleWord);
    if (m.empty()) return false;
    std::vector<HudVertex> moved = tri;
    std::vector<bool> drop(tri.size() / 6, false);
    glyph.clear();
    float cw = 0, ch = 0;
    cell_units(PadButton::kStart, cw, ch);
    // the button's drawn part across its cell (base and symbol), 0..1: the words are laid out by it
    const PromptAtlas::Rect cell = atlas.cell(PadButton::kStart, GlyphLayer::kBase);
    const PromptAtlas::Rect ib = atlas.ink(PadButton::kStart, GlyphLayer::kBase), is = atlas.ink(PadButton::kStart, GlyphLayer::kSymbol);
    const PromptAtlas::Rect cs = atlas.cell(PadButton::kStart, GlyphLayer::kSymbol);
    const float cu = cell.u1 - cell.u0, su = cs.u1 - cs.u0;
    const float f0 = cu > 0 && su > 0 ? std::clamp(std::min((ib.u0 - cell.u0) / cu, (is.u0 - cs.u0) / su), 0.0f, 1.0f) : 0.0f;
    const float f1 = cu > 0 && su > 0 ? std::clamp(std::max((ib.u1 - cell.u0) / cu, (is.u1 - cs.u0) / su), f0, 1.0f) : 1.0f;
    for (const TextLineMatch& r : m) {
        QuadBox w;
        for (std::size_t q = r.word_first; q < r.word_first + r.word_quads; ++q) {
            const QuadBox b = quad_box(tri, q);
            w.x0 = std::min(w.x0, b.x0); w.x1 = std::max(w.x1, b.x1); w.y0 = std::min(w.y0, b.y0); w.y1 = std::max(w.y1, b.y1);
            drop[q] = true;
        }
        const float gh = (w.y1 - w.y0) * kTitleGlyphScale, gw = gh * cw / ch, vw = gw * (f1 - f0);
        const float half = 0.5f * (vw - (w.x1 - w.x0));   // each side of the word moves out by this (in, when < 0)
        for (std::size_t q = r.line_first; q < r.line_first + r.line_quads; ++q) {
            if (drop[q]) continue;
            const float dx = q < r.word_first ? -half : half;
            for (std::size_t k = q * 6; k < q * 6 + 6; ++k) moved[k].x += dx;
        }
        // the drawn part centred on the word
        const float cx = 0.5f * (w.x0 + w.x1) + (0.5f - 0.5f * (f0 + f1)) * gw, cy = 0.5f * (w.y0 + w.y1);
        const HudVertex& like = tri[r.word_first * 6];   // the word's colour (it fades / pulses with the line) and depth
        for (GlyphLayer l : {GlyphLayer::kBase, GlyphLayer::kSymbol}) {
            const PromptAtlas::Rect c = atlas.cell(PadButton::kStart, l);
            const float xs[4] = {cx - 0.5f * gw, cx + 0.5f * gw, cx + 0.5f * gw, cx - 0.5f * gw};
            const float ys[4] = {cy - 0.5f * gh, cy - 0.5f * gh, cy + 0.5f * gh, cy + 0.5f * gh};
            const float us[4] = {c.u0, c.u1, c.u1, c.u0}, vs[4] = {c.v0, c.v0, c.v1, c.v1};
            for (int k : {0, 1, 2, 0, 2, 3}) {
                HudVertex v = like;
                v.x = xs[k]; v.y = ys[k]; v.u = us[k]; v.v = vs[k];
                glyph.push_back(v);
            }
        }
    }
    text.clear();
    for (std::size_t q = 0; q < drop.size(); ++q)
        if (!drop[q]) text.insert(text.end(), moved.begin() + std::ptrdiff_t(q * 6), moved.begin() + std::ptrdiff_t(q * 6 + 6));
    return true;
}

bool series_glyphs(const std::vector<const std::vector<HudVertex>*>& arrows, const PromptAtlas& atlas,
                   std::vector<std::vector<HudVertex>>& out) {
    out.assign(arrows.size(), {});
    if (atlas.width() <= 0) return false;
    struct Q { std::size_t draw, quad; QuadBox b; };
    std::vector<Q> qs;
    for (std::size_t d = 0; d < arrows.size(); ++d)
        if (arrows[d])
            for (std::size_t q = 0; q < arrows[d]->size() / 6; ++q) qs.push_back({d, q, quad_box(*arrows[d], q)});
    const Q* best[2] = {nullptr, nullptr};
    float best_y = 1e30f;
    for (const Q& a : qs)
        for (const Q& b : qs) {
            const float ha = a.b.y1 - a.b.y0, hb = b.b.y1 - b.b.y0, wa = a.b.x1 - a.b.x0, wb = b.b.x1 - b.b.x0;
            const float cya = 0.5f * (a.b.y0 + a.b.y1), cyb = 0.5f * (b.b.y0 + b.b.y1);
            if (!(ha > 0 && hb > 0 && wa > 0 && wb > 0) || std::fabs(cya - cyb) > 2.0f || std::fabs(ha - hb) > 2.0f) continue;
            if (!(b.b.x0 - a.b.x1 > std::max(wa, wb))) continue;   // a left of b, the name between them
            if (cya < best_y) { best_y = cya; best[0] = &a; best[1] = &b; }
        }
    if (!best[0]) return false;
    float cw = 0, ch = 0;
    cell_units(PadButton::kL1, cw, ch);
    const float gh = kSeriesGlyphH, gw = gh * cw / ch;
    for (int side = 0; side < 2; ++side) {
        const Q& a = *best[side];
        const PadButton pb = side ? PadButton::kR1 : PadButton::kL1;
        const float cy = 0.5f * (a.b.y0 + a.b.y1);
        const float x0 = side ? a.b.x1 + kSeriesGlyphGap : a.b.x0 - kSeriesGlyphGap - gw;
        HudVertex like = (*arrows[a.draw])[a.quad * 6];   // its depth and alpha (the page's fade); white
        like.rgba[0] = like.rgba[1] = like.rgba[2] = 0x80;
        std::vector<HudVertex>& g = out[a.draw];
        for (GlyphLayer l : {GlyphLayer::kBase, GlyphLayer::kSymbol}) {
            const PromptAtlas::Rect c = atlas.cell(pb, l);
            const float xs[4] = {x0, x0 + gw, x0 + gw, x0};
            const float ys[4] = {cy - 0.5f * gh, cy - 0.5f * gh, cy + 0.5f * gh, cy + 0.5f * gh};
            const float us[4] = {c.u0, c.u1, c.u1, c.u0}, vs[4] = {c.v0, c.v0, c.v1, c.v1};
            for (int k : {0, 1, 2, 0, 2, 3}) {
                HudVertex v = like;
                v.x = xs[k]; v.y = ys[k]; v.u = us[k]; v.v = vs[k];
                g.push_back(v);
            }
        }
    }
    return true;
}

}  // namespace b3replay
