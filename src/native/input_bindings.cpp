// Control remapping (src/native/input_bindings.h).
#include "input_bindings.h"
#include "b3port/fileio.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace b3native::input {

namespace {

std::size_t idx(Ds2 a) { return static_cast<std::size_t>(a); }

std::string lower(std::string_view s) {
    std::string o(s);
    for (char& c : o) c = char(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
std::string upper(std::string_view s) {
    std::string o(s);
    for (char& c : o) c = char(std::toupper(static_cast<unsigned char>(c)));
    return o;
}
std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// ---- keys: name for the ini, label for the pages ----
struct KeyName { std::uint16_t sc; const char* name; const char* label; };
const KeyName kKeys[] = {
    {40, "Return", "ENTER"}, {41, "Escape", "ESC"}, {42, "Backspace", "BACKSPACE"}, {43, "Tab", "TAB"},
    {44, "Space", "SPACE"}, {45, "Minus", "-"}, {46, "Equals", "="}, {47, "LeftBracket", "["}, {48, "RightBracket", "]"},
    {49, "Backslash", "\\"}, {50, "NonUSHash", "#"}, {51, "Semicolon", ";"}, {52, "Apostrophe", "'"}, {53, "Grave", "`"},
    {54, "Comma", ","}, {55, "Period", "."}, {56, "Slash", "/"}, {57, "CapsLock", "CAPS LOCK"},
    {70, "PrintScreen", "PRINT SCREEN"}, {71, "ScrollLock", "SCROLL LOCK"}, {72, "Pause", "PAUSE"}, {73, "Insert", "INSERT"},
    {74, "Home", "HOME"}, {75, "PageUp", "PAGE UP"}, {76, "Delete", "DELETE"}, {77, "End", "END"}, {78, "PageDown", "PAGE DOWN"},
    {79, "Right", "RIGHT"}, {80, "Left", "LEFT"}, {81, "Down", "DOWN"}, {82, "Up", "UP"}, {83, "Numlock", "NUM LOCK"},
    {84, "Keypad /", "NUM /"}, {85, "Keypad *", "NUM *"}, {86, "Keypad -", "NUM -"}, {87, "Keypad +", "NUM +"},
    {88, "Keypad Enter", "NUM ENTER"}, {98, "Keypad 0", "NUM 0"}, {99, "Keypad .", "NUM ."}, {100, "NonUSBackslash", "<>"},
    {101, "Application", "MENU"}, {224, "Left Ctrl", "L CTRL"}, {225, "Left Shift", "L SHIFT"}, {226, "Left Alt", "L ALT"},
    {227, "Left GUI", "L GUI"}, {228, "Right Ctrl", "R CTRL"}, {229, "Right Shift", "R SHIFT"}, {230, "Right Alt", "R ALT"},
    {231, "Right GUI", "R GUI"},
};

// ---- gamepad inputs ----
struct PadName { const char* key; const char* generic; const char* ps; const char* xbox; const char* nintendo; };
const PadName kPad[] = {
    {"none", "-", "-", "-", "-"},
    {"south", "SOUTH", "CROSS", "A", "B"},
    {"east", "EAST", "CIRCLE", "B", "A"},
    {"west", "WEST", "SQUARE", "X", "Y"},
    {"north", "NORTH", "TRIANGLE", "Y", "X"},
    {"back", "BACK", "SELECT", "VIEW", "-"},
    {"guide", "GUIDE", "PS", "GUIDE", "HOME"},
    {"start", "START", "START", "MENU", "+"},
    {"left_stick", "L3", "L3", "LS", "LS"},
    {"right_stick", "R3", "R3", "RS", "RS"},
    {"left_shoulder", "L1", "L1", "LB", "L"},
    {"right_shoulder", "R1", "R1", "RB", "R"},
    {"dpad_up", "D-PAD UP", "D-PAD UP", "D-PAD UP", "D-PAD UP"},
    {"dpad_down", "D-PAD DOWN", "D-PAD DOWN", "D-PAD DOWN", "D-PAD DOWN"},
    {"dpad_left", "D-PAD LEFT", "D-PAD LEFT", "D-PAD LEFT", "D-PAD LEFT"},
    {"dpad_right", "D-PAD RIGHT", "D-PAD RIGHT", "D-PAD RIGHT", "D-PAD RIGHT"},
    {"misc1", "MISC", "MIC", "SHARE", "CAPTURE"},
    {"right_paddle1", "PADDLE R1", "PADDLE R1", "PADDLE P1", "PADDLE R1"},
    {"left_paddle1", "PADDLE L1", "PADDLE L1", "PADDLE P3", "PADDLE L1"},
    {"right_paddle2", "PADDLE R2", "PADDLE R2", "PADDLE P2", "PADDLE R2"},
    {"left_paddle2", "PADDLE L2", "PADDLE L2", "PADDLE P4", "PADDLE L2"},
    {"touchpad", "TOUCHPAD", "TOUCHPAD", "TOUCHPAD", "TOUCHPAD"},
    {"left_x-", "L STICK LEFT", "L STICK LEFT", "L STICK LEFT", "L STICK LEFT"},
    {"left_x+", "L STICK RIGHT", "L STICK RIGHT", "L STICK RIGHT", "L STICK RIGHT"},
    {"left_y-", "L STICK UP", "L STICK UP", "L STICK UP", "L STICK UP"},
    {"left_y+", "L STICK DOWN", "L STICK DOWN", "L STICK DOWN", "L STICK DOWN"},
    {"right_x-", "R STICK LEFT", "R STICK LEFT", "R STICK LEFT", "R STICK LEFT"},
    {"right_x+", "R STICK RIGHT", "R STICK RIGHT", "R STICK RIGHT", "R STICK RIGHT"},
    {"right_y-", "R STICK UP", "R STICK UP", "R STICK UP", "R STICK UP"},
    {"right_y+", "R STICK DOWN", "R STICK DOWN", "R STICK DOWN", "R STICK DOWN"},
    {"left_trigger", "L2", "L2", "LT", "ZL"},
    {"right_trigger", "R2", "R2", "RT", "ZR"},
};
static_assert(sizeof kPad / sizeof kPad[0] == std::size_t(PadInput::Count));

// input.md §3: one stick reading's half axes past the dead zone z, rescaled (gamepad_sample's stick_axis, split)
float half_axis(std::int16_t raw, float z, bool positive) {
    const float n = raw < 0 ? static_cast<float>(raw) / 32768.0f : static_cast<float>(raw) / 32767.0f;
    return positive ? std::clamp((n - z) / (1.0f - z), 0.0f, 1.0f) : std::clamp((-n - z) / (1.0f - z), 0.0f, 1.0f);
}
float trigger_travel(std::int16_t raw) { return std::clamp(static_cast<float>(std::max<int>(raw, 0)) / 32767.0f, 0.0f, 1.0f); }

void press(std::array<std::uint8_t, 32>& s, int b) {
    s[std::size_t(b >> 3)] = static_cast<std::uint8_t>(s[std::size_t(b >> 3)] & ~(1u << (b & 7)));
}

bool overlap(KeyCtx a, KeyCtx b) { return a == KeyCtx::Any || b == KeyCtx::Any || a == b; }
KeyCtx other(KeyCtx v) { return v == KeyCtx::Menu ? KeyCtx::Drive : KeyCtx::Menu; }

// a binding's physical input (a key's scancode, a gamepad input), for the helpers both kinds share
std::uint16_t phys(const KeyBind& b) { return b.scancode; }
std::uint16_t phys(const PadBind& b) { return std::uint16_t(b.in); }

// an input and its contexts merged: Menu + Drive of one input -> Any; duplicates dropped; the first place kept
template <class B>
void normalize(std::vector<B>& l) {
    std::vector<B> o;
    for (const B& b : l) {
        auto it = std::find_if(o.begin(), o.end(), [&](const B& x) { return phys(x) == phys(b); });
        if (it == o.end()) { o.push_back(b); continue; }
        if (it->ctx != b.ctx) it->ctx = KeyCtx::Any;
    }
    l = std::move(o);
}

// `in` out of the view: a binding of the view goes, one of both contexts keeps the other context only
template <class B>
void remove_from_view(std::vector<B>& l, std::uint16_t in, KeyCtx view) {
    std::vector<B> o;
    for (B b : l) {
        if (phys(b) != in) { o.push_back(b); continue; }
        if (view == KeyCtx::Any || b.ctx == view) continue;
        if (b.ctx == KeyCtx::Any) b.ctx = other(view);
        o.push_back(b);
    }
    l = std::move(o);
}

// one action's bindings back to `def`'s in the view (the other context's stay); both views back: `def`'s list itself
// (its order is part of what the file compares against)
template <class B>
void reset_in_view(std::vector<B>& l, const std::vector<B>& def, KeyCtx view) {
    if (view == KeyCtx::Any) {
        l = def;
        return;
    }
    for (const B& k : std::vector<B>(l)) remove_from_view(l, phys(k), view);
    for (B k : def)
        if (overlap(k.ctx, view)) {
            if (k.ctx == KeyCtx::Any) k.ctx = view;
            l.push_back(k);
        }
    normalize(l);
    const auto less = [](const B& a, const B& b) { return phys(a) != phys(b) ? phys(a) < phys(b) : a.ctx < b.ctx; };
    std::vector<B> a = l, b = def;
    std::sort(a.begin(), a.end(), less);
    std::sort(b.begin(), b.end(), less);
    if (a == b) l = def;
}

PlayerControls make_default(int player) {
    PlayerControls p;
    if (player == 0)
        for (const KeyBinding& k : default_key_map()) p.keys[idx(k.control)].push_back({k.scancode, k.ctx});
    // gamepad_sample's layout (input_report.cpp, docs/specs/input.md §3)
    using P = PadInput;
    p.pad[idx(Ds2::Cross)] = {P::South, P::RightTrigger};
    p.pad[idx(Ds2::Square)] = {P::West, P::LeftTrigger};
    p.pad[idx(Ds2::Circle)] = {P::East};
    p.pad[idx(Ds2::Triangle)] = {P::North};
    p.pad[idx(Ds2::L1)] = {P::LeftShoulder};
    p.pad[idx(Ds2::R1)] = {P::RightShoulder};
    // padr2: the triggers are the pedals, so the stick clicks give the two driving controls a pad had no way to reach:
    // R2 the Crashbreaker (0x12F610), L2 the music track skip (0x12F460) (input_keys.h)
    p.pad[idx(Ds2::L2)] = {P::LeftStick};
    p.pad[idx(Ds2::R2)] = {P::RightStick};
    p.pad[idx(Ds2::Start)] = {P::Start};
    p.pad[idx(Ds2::Select)] = {P::Back};
    p.pad[idx(Ds2::Up)] = {P::DpadUp};
    p.pad[idx(Ds2::Down)] = {P::DpadDown};
    p.pad[idx(Ds2::Left)] = {P::DpadLeft};
    p.pad[idx(Ds2::Right)] = {P::DpadRight};
    p.pad[idx(Ds2::SteerLeft)] = {P::LeftXNeg};
    p.pad[idx(Ds2::SteerRight)] = {P::LeftXPos};
    p.pad[idx(Ds2::StickUp)] = {P::LeftYNeg};
    p.pad[idx(Ds2::StickDown)] = {P::LeftYPos};
    return p;
}

// padlayout (1.2.1): the console layouts (input_bindings.h PadLayout; docs/PLAYING.md "Controllers")
PadBindings common_moves() {
    using P = PadInput;
    PadBindings b;
    b[idx(Ds2::Start)] = {P::Start};
    b[idx(Ds2::Up)] = {P::DpadUp};
    b[idx(Ds2::Down)] = {P::DpadDown};
    b[idx(Ds2::Left)] = {P::DpadLeft};
    b[idx(Ds2::Right)] = {P::DpadRight};
    b[idx(Ds2::SteerLeft)] = {P::LeftXNeg};
    b[idx(Ds2::SteerRight)] = {P::LeftXPos};
    b[idx(Ds2::StickUp)] = {P::LeftYNeg};
    b[idx(Ds2::StickDown)] = {P::LeftYPos};
    return b;
}

// The original Xbox game's: RT accelerate, LT brake, A boost, B Crashbreaker, X look back, Y camera, Back music while
// driving; A confirm, B back in menus. The triggers stay the pedals in menus too: the race's intro and countdown are
// the menus' context (native_play: the game reads the accelerator from the start), where the throttle revs.
PadBindings make_xbox() {
    using P = PadInput;
    constexpr KeyCtx D = KeyCtx::Drive, M = KeyCtx::Menu;
    PadBindings b = common_moves();
    b[idx(Ds2::Cross)] = {P::RightTrigger, PadBind{P::South, M}};
    b[idx(Ds2::Square)] = {P::LeftTrigger, PadBind{P::West, M}};
    b[idx(Ds2::Triangle)] = {PadBind{P::North, D}, PadBind{P::East, M}};
    b[idx(Ds2::Circle)] = {PadBind{P::LeftStick, D}, PadBind{P::North, M}};   // the horn on LS; Y in menus
    b[idx(Ds2::L1)] = {PadBind{P::West, D}, P::LeftShoulder};                 // look back on X, and LB in both
    b[idx(Ds2::R1)] = {PadBind{P::South, D}, P::RightShoulder};               // boost on A, and RB in both
    b[idx(Ds2::R2)] = {PadBind{P::East, D}};                                  // the Crashbreaker on B
    b[idx(Ds2::L2)] = {PadBind{P::Back, D}};                                  // the music track skip on View / Back
    b[idx(Ds2::Select)] = {PadBind{P::Back, M}};
    return b;
}

// Nintendo (2026-11-19): the Xbox layout mirrored by label -- the face buttons' positions swapped across each diagonal
// (South <-> East, West <-> North), so the button labelled A confirms and boosts, B goes back and is the Crashbreaker,
// X (North) looks back / Square in menus and Y (West) the camera / Circle in menus, as the Xbox pad's A / B / X / Y
PadBindings make_nintendo() {
    PadBindings b = make_xbox();
    for (std::vector<PadBind>& l : b)
        for (PadBind& x : l) {
            switch (x.in) {
            case PadInput::South: x.in = PadInput::East; break;
            case PadInput::East: x.in = PadInput::South; break;
            case PadInput::West: x.in = PadInput::North; break;
            case PadInput::North: x.in = PadInput::West; break;
            default: break;
            }
        }
    return b;
}

// The PS2 game's, with the triggers as the pedals (padlayout, 2026-11-19): Cross / R2 accelerate and Square / L2 brake
// (R2 / L2 analog, as the Xbox layout's triggers), the Crashbreaker on R3 and the music track skip on L3 (the PS2's R2
// / L2 moved to the stick clicks), the rest button for button; the right stick as the game's (up accelerates, down
// brakes). Both contexts.
PadBindings make_playstation() {
    using P = PadInput;
    PadBindings b = common_moves();
    b[idx(Ds2::Cross)] = {P::South, P::RightTrigger};
    b[idx(Ds2::Square)] = {P::West, P::LeftTrigger};
    b[idx(Ds2::Circle)] = {P::East};
    b[idx(Ds2::Triangle)] = {P::North};
    b[idx(Ds2::L1)] = {P::LeftShoulder};
    b[idx(Ds2::R1)] = {P::RightShoulder};
    b[idx(Ds2::L2)] = {P::LeftStick};
    b[idx(Ds2::R2)] = {P::RightStick};
    b[idx(Ds2::Select)] = {P::Back};
    b[idx(Ds2::RStickLeft)] = {P::RightXNeg};
    b[idx(Ds2::RStickRight)] = {P::RightXPos};
    b[idx(Ds2::RStickUp)] = {P::RightYNeg};
    b[idx(Ds2::RStickDown)] = {P::RightYPos};
    return b;
}

// padlayout: an edit of the bindings a player's page shows (the layout for the page's pad family): applied to a copy;
// when it changed anything, the copy becomes the player's own bindings and the layout Custom
template <class F>
void edit_pad(Controls& c, int player, F f) {
    PlayerControls& p = c.player[std::size_t(player & 1)];
    const PadBindings& now = pad_bindings(p, c.family[std::size_t(player & 1)]);
    PadBindings b = now;
    f(b);
    if (b == now) return;
    p.pad = std::move(b);
    p.layout = PadLayout::Custom;
}

std::string join_keys(const std::vector<KeyBind>& l) {
    if (l.empty()) return "none";
    std::string o;
    for (const KeyBind& b : l) {
        if (!o.empty()) o += ", ";
        o += key_name(b.scancode);
        if (b.ctx == KeyCtx::Menu) o += "@menus";
        if (b.ctx == KeyCtx::Drive) o += "@driving";
    }
    return o;
}
std::string join_pad(const std::vector<PadBind>& l) {
    if (l.empty()) return "none";
    std::string o;
    for (const PadBind& b : l) {
        if (!o.empty()) o += ", ";
        o += pad_input_key(b.in);
        if (b.ctx == KeyCtx::Menu) o += "@menus";
        if (b.ctx == KeyCtx::Drive) o += "@driving";
    }
    return o;
}

}  // namespace

// ---- actions ----

const std::array<Ds2, kActions>& action_order() {
    static const std::array<Ds2, kActions> o = {
        Ds2::Cross, Ds2::Circle, Ds2::Square, Ds2::Triangle, Ds2::L1, Ds2::R1, Ds2::L2, Ds2::R2, Ds2::Start, Ds2::Select,
        Ds2::Up, Ds2::Down, Ds2::Left, Ds2::Right, Ds2::SteerLeft, Ds2::SteerRight, Ds2::StickUp, Ds2::StickDown,
        Ds2::RStickLeft, Ds2::RStickRight, Ds2::RStickUp, Ds2::RStickDown, Ds2::L3, Ds2::R3};
    return o;
}

const char* action_key(Ds2 a) {
    static const char* const n[kActions] = {"select", "l3", "r3", "start", "dpad_up", "dpad_right", "dpad_down", "dpad_left",
                                            "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square",
                                            "lstick_left", "lstick_right", "lstick_up", "lstick_down",
                                            "rstick_left", "rstick_right", "rstick_up", "rstick_down"};
    return idx(a) < std::size_t(kActions) ? n[idx(a)] : "?";
}

const char* action_label(Ds2 a) {
    static const char* const n[kActions] = {"SELECT", "L3", "R3", "START", "D-PAD UP", "D-PAD RIGHT", "D-PAD DOWN", "D-PAD LEFT",
                                            "L2", "R2", "L1", "R1", "TRIANGLE", "CIRCLE", "CROSS", "SQUARE",
                                            "LEFT STICK LEFT", "LEFT STICK RIGHT", "LEFT STICK UP", "LEFT STICK DOWN",
                                            "RIGHT STICK LEFT", "RIGHT STICK RIGHT", "RIGHT STICK UP", "RIGHT STICK DOWN"};
    return idx(a) < std::size_t(kActions) ? n[idx(a)] : "?";
}

const char* action_use(Ds2 a, bool driving) {
    // input_keys.h: what the game reads while driving [C, the asm cited there]; in menus the game's prompts
    if (driving) {
        switch (a) {
        case Ds2::Cross: return "accelerate";
        case Ds2::Square: return "brake / reverse";
        case Ds2::Circle: return "horn";
        case Ds2::Triangle: return "change camera";
        case Ds2::L1: return "look back";
        case Ds2::R1: return "boost; held in a crash: Impact Time";
        case Ds2::R2: return "Crashbreaker";
        case Ds2::L2: return "music track skip";
        case Ds2::Start: return "pause";
        case Ds2::Left: case Ds2::Right: case Ds2::SteerLeft: case Ds2::SteerRight: return "steer";
        case Ds2::Up: case Ds2::Down: case Ds2::StickUp: case Ds2::StickDown: return "Aftertouch in a crash";
        default: return "";
        }
    }
    switch (a) {
    case Ds2::Cross: return "confirm";
    case Ds2::Triangle: return "back";
    case Ds2::Start: return "start / resume";
    case Ds2::Square: return "car colour, change names";
    case Ds2::Up: case Ds2::Down: case Ds2::Left: case Ds2::Right: return "move";
    case Ds2::SteerLeft: case Ds2::SteerRight: case Ds2::StickUp: case Ds2::StickDown: return "move";
    default: return "";
    }
}

std::optional<Ds2> parse_action(std::string_view s) {
    const std::string l = lower(trim(s));
    for (int a = 0; a < kActions; ++a)
        if (l == action_key(Ds2(a))) return Ds2(a);
    return std::nullopt;
}

// ---- gamepad inputs ----

bool pad_input_analog(PadInput p) { return p >= PadInput::LeftXNeg && p < PadInput::Count; }
const char* pad_input_key(PadInput p) { return std::size_t(p) < std::size_t(PadInput::Count) ? kPad[std::size_t(p)].key : "?"; }

std::optional<PadInput> parse_pad_input(std::string_view s) {
    const std::string l = lower(trim(s));
    for (std::size_t k = 1; k < std::size_t(PadInput::Count); ++k)
        if (l == kPad[k].key) return PadInput(k);
    return std::nullopt;
}

std::vector<PadInput> parse_pad_chord(std::string_view text, std::vector<std::string>* unknown) {
    const std::string l = lower(std::string(text));
    const auto space = [&](std::size_t i) { return i < l.size() && (l[i] == ' ' || l[i] == '\t'); };
    const auto sep = [&](std::size_t i) { return i < l.size() && (l[i] == '+' || l[i] == ','); };
    std::vector<PadInput> out;
    std::size_t at = 0;
    while (at < l.size()) {
        while (space(at) || sep(at)) ++at;
        if (at >= l.size()) break;
        std::size_t best = 0, best_len = 0;
        for (std::size_t k = 1; k < std::size_t(PadInput::Count); ++k) {
            const std::size_t n = std::strlen(kPad[k].key);
            if (n <= best_len || l.compare(at, n, kPad[k].key) != 0) continue;
            std::size_t e = at + n;
            while (space(e)) ++e;
            if (e == l.size() || sep(e)) { best = k; best_len = n; }   // a whole input, then a separator or the end
        }
        if (best_len) {
            if (std::find(out.begin(), out.end(), PadInput(best)) == out.end()) out.push_back(PadInput(best));
            at += best_len;
            while (space(at)) ++at;
            if (sep(at)) ++at;
        } else {   // not an input: up to the next separator
            std::size_t e = at;
            while (e < l.size() && !sep(e)) ++e;
            if (unknown) unknown->push_back(std::string(trim(std::string_view(text).substr(at, e - at))));
            at = e;
        }
    }
    return out;
}

std::string pad_input_label(PadInput p, PadStyle style) {
    if (std::size_t(p) >= std::size_t(PadInput::Count)) return "?";
    const PadName& n = kPad[std::size_t(p)];
    switch (style) {
    case PadStyle::PlayStation: return n.ps;
    case PadStyle::Xbox: return n.xbox;
    case PadStyle::Nintendo: return n.nintendo;
    case PadStyle::Generic: break;
    }
    return n.generic;
}

float pad_input_amount(const GamepadState& g, PadInput p, float z) {
    auto b = [](bool v) { return v ? 1.0f : 0.0f; };
    switch (p) {
    case PadInput::South: return b(g.south);
    case PadInput::East: return b(g.east);
    case PadInput::West: return b(g.west);
    case PadInput::North: return b(g.north);
    case PadInput::Back: return b(g.back);
    case PadInput::Guide: return b(g.guide);
    case PadInput::Start: return b(g.start);
    case PadInput::LeftStick: return b(g.left_stick);
    case PadInput::RightStick: return b(g.right_stick);
    case PadInput::LeftShoulder: return b(g.left_shoulder);
    case PadInput::RightShoulder: return b(g.right_shoulder);
    case PadInput::DpadUp: return b(g.dpad_up);
    case PadInput::DpadDown: return b(g.dpad_down);
    case PadInput::DpadLeft: return b(g.dpad_left);
    case PadInput::DpadRight: return b(g.dpad_right);
    case PadInput::Misc1: return b(g.misc1);
    case PadInput::RightPaddle1: return b(g.paddles[0]);
    case PadInput::LeftPaddle1: return b(g.paddles[1]);
    case PadInput::RightPaddle2: return b(g.paddles[2]);
    case PadInput::LeftPaddle2: return b(g.paddles[3]);
    case PadInput::Touchpad: return b(g.touchpad);
    case PadInput::LeftXNeg: return half_axis(g.left_x, z, false);
    case PadInput::LeftXPos: return half_axis(g.left_x, z, true);
    case PadInput::LeftYNeg: return half_axis(g.left_y, z, false);
    case PadInput::LeftYPos: return half_axis(g.left_y, z, true);
    case PadInput::RightXNeg: return half_axis(g.right_x, z, false);
    case PadInput::RightXPos: return half_axis(g.right_x, z, true);
    case PadInput::RightYNeg: return half_axis(g.right_y, z, false);
    case PadInput::RightYPos: return half_axis(g.right_y, z, true);
    case PadInput::LeftTrigger: return trigger_travel(g.left_trigger);
    case PadInput::RightTrigger: return trigger_travel(g.right_trigger);
    default: return 0.0f;
    }
}

// ---- keys ----

std::string key_name(std::uint16_t s) {
    if (s >= 4 && s <= 29) return std::string(1, char('A' + (s - 4)));
    if (s >= 30 && s <= 38) return std::string(1, char('1' + (s - 30)));
    if (s == 39) return "0";
    if (s >= 58 && s <= 69) return "F" + std::to_string(s - 57);
    if (s >= 104 && s <= 115) return "F" + std::to_string(s - 91);   // F13 .. F24
    if (s >= 89 && s <= 97) return "Keypad " + std::to_string(s - 88);
    for (const KeyName& k : kKeys)
        if (k.sc == s) return k.name;
    return "sc" + std::to_string(s);
}

std::optional<std::uint16_t> parse_key(std::string_view text) {
    const std::string l = lower(trim(text));
    if (l.empty()) return std::nullopt;
    if (l.size() > 2 && l.compare(0, 2, "sc") == 0 && std::all_of(l.begin() + 2, l.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        const long v = std::strtol(l.c_str() + 2, nullptr, 10);
        if (v > 0 && v < 512) return std::uint16_t(v);
        return std::nullopt;
    }
    for (std::uint16_t s = 1; s < 512; ++s) {
        const std::string n = key_name(s);
        if (n.rfind("sc", 0) != 0 && lower(n) == l) return s;
    }
    return std::nullopt;
}

std::string key_label(std::uint16_t s) {
    if (s >= 89 && s <= 98) return "NUM " + std::to_string(s == 98 ? 0 : s - 88);
    for (const KeyName& k : kKeys)
        if (k.sc == s) return k.label;
    return upper(key_name(s));
}

bool key_reserved(std::uint16_t s) {
    // rebind2: Escape cancels a capture (the only key the pages keep for themselves); 0 and 512+ are no key
    return s == 41 || s == 0 || s >= 512;
}

// ---- port actions (rebind2) ----

namespace {
struct PortInfo {
    const char* key;
    const char* label;
    const char* use;
    bool photo, pad, analog;
};
const PortInfo kPort[kPortActions] = {
    {"restart", "INSTANT RESTART", "Restart the event while you drive (needs INSTANT RESTART on).", false, true, false},
    {"fullscreen", "FULLSCREEN", "Switch between fullscreen and a window (Alt + Enter always does too).", false, true, false},
    {"screenshot", "SCREENSHOT", "Save a picture of the game.", false, true, false},
    {"bug_report", "BUG REPORT", "Save a bug report with the log and the settings.", false, true, false},
    {"tune_overlay", "TUNING OVERLAY", "Open or close the tuning console in a race (needs DEVELOPER MODS on).", false, false, false},
    {"photo_mode", "PHOTO MODE", "Enter photo mode from the race's pause menu; leave it again.", false, true, false},
    {"section_prev", "SETTINGS: PREVIOUS SECTION", "PC SETTINGS: jump to the previous section of a long tab.", false, false, false},
    {"section_next", "SETTINGS: NEXT SECTION", "PC SETTINGS: jump to the next section of a long tab.", false, false, false},
    {"fps_view", "FPS COUNTER VIEW", "Cycle the FPS counter: minimal, standard, detailed, off.", false, false, false},
    {"photo_forward", "PHOTO: MOVE FORWARD", "Photo mode: fly along the view.", true, true, true},
    {"photo_backward", "PHOTO: MOVE BACK", "Photo mode: fly backwards.", true, true, true},
    {"photo_left", "PHOTO: MOVE LEFT", "Photo mode: move sideways to the left.", true, true, true},
    {"photo_right", "PHOTO: MOVE RIGHT", "Photo mode: move sideways to the right.", true, true, true},
    {"photo_up", "PHOTO: MOVE UP", "Photo mode: rise.", true, true, true},
    {"photo_down", "PHOTO: MOVE DOWN", "Photo mode: sink.", true, true, true},
    {"photo_look_left", "PHOTO: LOOK LEFT", "Photo mode: turn the camera left (the mouse looks too).", true, true, true},
    {"photo_look_right", "PHOTO: LOOK RIGHT", "Photo mode: turn the camera right.", true, true, true},
    {"photo_look_up", "PHOTO: LOOK UP", "Photo mode: tilt the camera up.", true, true, true},
    {"photo_look_down", "PHOTO: LOOK DOWN", "Photo mode: tilt the camera down.", true, true, true},
    {"photo_roll_left", "PHOTO: ROLL LEFT", "Photo mode: tilt the picture anticlockwise.", true, true, true},
    {"photo_roll_right", "PHOTO: ROLL RIGHT", "Photo mode: tilt the picture clockwise.", true, true, true},
    {"photo_wider", "PHOTO: WIDER VIEW", "Photo mode: widen the field of view (the wheel zooms too).", true, true, true},
    {"photo_narrower", "PHOTO: NARROWER VIEW", "Photo mode: narrow the field of view.", true, true, true},
    {"photo_focus_near", "PHOTO: FOCUS NEARER", "Photo mode: focus nearer (depth of field).", true, true, true},
    {"photo_focus_far", "PHOTO: FOCUS FARTHER", "Photo mode: focus farther (depth of field).", true, true, true},
    {"photo_fast", "PHOTO: FASTER", "Photo mode: hold for faster moves and turns.", true, true, false},
    {"photo_slow", "PHOTO: FINER", "Photo mode: hold for slower, finer moves and turns.", true, true, false},
    {"photo_dof", "PHOTO: DEPTH OF FIELD", "Photo mode: depth of field on or off.", true, true, false},
    {"photo_blur_less", "PHOTO: LESS BLUR", "Photo mode: a weaker depth-of-field blur.", true, true, false},
    {"photo_blur_more", "PHOTO: MORE BLUR", "Photo mode: a stronger depth-of-field blur.", true, true, false},
    {"photo_blur_cycle", "PHOTO: BLUR LEVELS", "Photo mode: the next blur level, round to the first.", true, true, false},
    {"photo_target", "PHOTO: FOCUS ON CAR / CENTRE", "Photo mode: focus on the car or the middle of the screen.", true, true, false},
    {"photo_motion", "PHOTO: MOTION BLUR", "Photo mode: the motion blur's next mode: off, tracking, panning, static.", true, true, false},
    {"photo_shutter_faster", "PHOTO: FASTER SHUTTER", "Photo mode: a shorter exposure, less motion blur.", true, true, false},
    {"photo_shutter_slower", "PHOTO: SLOWER SHUTTER", "Photo mode: a longer exposure, more motion blur.", true, true, false},
    {"photo_reset", "PHOTO: GAME CAMERA", "Photo mode: back to the game's camera view.", true, true, false},
    {"photo_help", "PHOTO: HELP", "Photo mode: hide or show the help box.", true, true, false},
    {"photo_exit", "PHOTO: LEAVE", "Photo mode: leave it (Escape and the photo mode key leave too).", true, true, false},
    {"car_look", "CAR LOOK PANEL", "Open or close the live car reflection tuner (needs DEVELOPER MODS on).", false, false, false},
    {"hd_textures", "HD TEXTURES ON / OFF", "Switch between the HD texture pack and the original textures at once, to compare (this run only).", false, true, false},
    {"race_time", "TIME OF DAY PER RACE", "The track pages (Single Event, Multiplayer): Default, Day or Night for the next race.", false, true, false},
    {"paint_panel", "CAR PAINT PANEL", "SELECT CAR: open or close the car paint panel (a controller: the menus' Circle button, Y on the Xbox and Nintendo layouts).", false, false, false},
    {"shift_up", "SHIFT UP", "A race with TRANSMISSION Manual (SELECT OPTIONS): the next gear up.", false, true, false},
    {"shift_down", "SHIFT DOWN", "A race with TRANSMISSION Manual (SELECT OPTIONS): the next gear down.", false, true, false},
    {"party_moon_jump", "MOON JUMP", "Held in a race: lifts your car into the air (GAME > PARTY MODES > MOON JUMP on).", false, true, false},
    {"party_crash", "CRASH", "In a race: crashes your car at once (GAME > PARTY MODES > CRASH ON DEMAND on).", false, true, false},
    {"party_ai_driver", "AI DRIVER", "In a race: the game's AI drives your car, and you again on the next press (GAME > PARTY MODES > AI DRIVER BUTTON on).", false, true, false},
    {"replay", "REPLAY", "A race's results page, with RACE REPLAY on (GAME > MODES): watch the race again; while it plays: back to the results.", false, true, false},
    {"replay_camera_prev", "REPLAY: PREVIOUS CAMERA", "While a race replay plays: the previous camera angle.", false, true, false},
    {"replay_camera_next", "REPLAY: NEXT CAMERA", "While a race replay plays: the next camera angle.", false, true, false},
    {"replay_car_prev", "REPLAY: PREVIOUS CAR", "While a race replay plays: watch the previous car.", false, true, false},
    {"replay_car_next", "REPLAY: NEXT CAR", "While a race replay plays: watch the next car.", false, true, false},
    {"replay_free_camera", "REPLAY: FREE CAMERA", "While a race replay plays: a free camera (photo mode's controls), and back.", false, true, false},
};

PortControls make_default_port() {
    using P = PadInput;
    PortControls d;
    const auto set = [&](PortAction a, std::vector<std::uint16_t> keys, std::vector<PadInput> pad) {
        d.keys[std::size_t(a)] = std::move(keys);
        d.pad[std::size_t(a)] = std::move(pad);
    };
    set(PortAction::Restart, {62}, {P::Back, P::Start});          // F5; Select + Start
    set(PortAction::Fullscreen, {68}, {});                         // F11
    set(PortAction::Screenshot, {69}, {});                         // F12
    set(PortAction::BugReport, {67}, {});                          // F10
    set(PortAction::TuneOverlay, {66}, {});                        // F9
    set(PortAction::CarLook, {65}, {});                            // F8
    set(PortAction::HdTextures, {61}, {});                         // F4 (hdkey; a gamepad binding can be added)
    set(PortAction::RaceTime, {23}, {P::LeftShoulder});            // T; LB / L1 (todrace: the track page reads neither)
    set(PortAction::PaintPanel, {19}, {});                         // P (paintsel; photo mode's P too: never on SELECT CAR)
    set(PortAction::ShiftUp, {24}, {P::RightPaddle1});             // U; the right paddle (gearbox: no layout binds the paddles)
    set(PortAction::ShiftDown, {28}, {P::LeftPaddle1});            // Y; the left paddle
    set(PortAction::PartyMoonJump, {33}, {});                      // 4 (party1: no controller button of its own: bindable)
    set(PortAction::PartyCrash, {34}, {});                         // 5
    set(PortAction::PartyAiDriver, {35}, {});                      // 6
    set(PortAction::Replay, {63}, {P::West});                      // F6; Square / X (replayview: the results pages read neither)
    set(PortAction::ReplayCameraPrev, {47}, {P::DpadDown});        // [ ; replaycams (the replay only)
    set(PortAction::ReplayCameraNext, {48}, {P::DpadUp});          // ]
    set(PortAction::ReplayCarPrev, {54}, {P::DpadLeft});           // ,
    set(PortAction::ReplayCarNext, {55}, {P::DpadRight});          // .
    set(PortAction::ReplayFreeCamera, {12}, {P::Start});           // I
    set(PortAction::PhotoMode, {19}, {P::LeftStick, P::RightStick});   // P; L3 + R3
    set(PortAction::SectionPrev, {75}, {});                        // Page Up
    set(PortAction::SectionNext, {78}, {});                        // Page Down
    set(PortAction::FpsView, {60}, {});                            // F3
    set(PortAction::PhotoForward, {26}, {P::LeftYNeg});            // W; left stick up
    set(PortAction::PhotoBackward, {22}, {P::LeftYPos});           // S
    set(PortAction::PhotoLeft, {4}, {P::LeftXNeg});                // A
    set(PortAction::PhotoRight, {7}, {P::LeftXPos});               // D
    set(PortAction::PhotoUp, {8}, {P::RightTrigger});              // E; RT
    set(PortAction::PhotoDown, {20}, {P::LeftTrigger});            // Q; LT
    set(PortAction::PhotoLookLeft, {80}, {P::RightXNeg});          // arrows; right stick
    set(PortAction::PhotoLookRight, {79}, {P::RightXPos});
    set(PortAction::PhotoLookUp, {82}, {P::RightYNeg});
    set(PortAction::PhotoLookDown, {81}, {P::RightYPos});
    set(PortAction::PhotoRollLeft, {29}, {P::LeftShoulder});       // Z; LB
    set(PortAction::PhotoRollRight, {6}, {P::RightShoulder});      // C; RB
    set(PortAction::PhotoWider, {46, 87}, {P::DpadDown});          // = and keypad +; D-pad down
    set(PortAction::PhotoNarrower, {45, 86}, {P::DpadUp});         // - and keypad -; D-pad up
    set(PortAction::PhotoFocusNear, {17}, {P::DpadLeft});          // N
    set(PortAction::PhotoFocusFar, {16}, {P::DpadRight});          // M
    set(PortAction::PhotoFast, {225, 229}, {P::South});            // Shift; A / Cross
    set(PortAction::PhotoSlow, {226, 230}, {});                    // Alt
    set(PortAction::PhotoDof, {9}, {P::Back});                     // F; Back / Select
    set(PortAction::PhotoBlurLess, {25}, {});                      // V
    set(PortAction::PhotoBlurMore, {5}, {});                       // B
    set(PortAction::PhotoBlurCycle, {}, {P::Start});               // Start
    set(PortAction::PhotoTarget, {10}, {P::LeftStick});            // G; L3 alone
    set(PortAction::PhotoMotion, {13}, {P::RightStick});           // J; R3 alone (photoblur)
    set(PortAction::PhotoShutterFaster, {14}, {P::RightStick, P::DpadLeft});    // K; R3 + D-pad left
    set(PortAction::PhotoShutterSlower, {15}, {P::RightStick, P::DpadRight});   // L; R3 + D-pad right
    set(PortAction::PhotoReset, {21}, {P::West});                  // R; X / Square
    set(PortAction::PhotoHelp, {11}, {P::North});                  // H; Y / Triangle
    set(PortAction::PhotoExit, {}, {P::East});                     // B / Circle
    return d;
}

std::size_t pidx(PortAction a) { return std::size_t(a); }
bool has_input(const std::vector<PadBind>& l, PadInput in, KeyCtx view = KeyCtx::Any) {
    return std::any_of(l.begin(), l.end(), [&](const PadBind& b) { return b.in == in && overlap(b.ctx, view); });
}
// A port action is global when it works whatever the game is doing: it conflicts with the game's own keys and buttons
bool port_global(PortAction a) { return !port_photo(a) && !port_page(a); }
}  // namespace

const char* port_key(PortAction a) { return std::size_t(a) < std::size_t(kPortActions) ? kPort[std::size_t(a)].key : "?"; }
const char* port_label(PortAction a) { return std::size_t(a) < std::size_t(kPortActions) ? kPort[std::size_t(a)].label : "?"; }
const char* port_use(PortAction a) { return std::size_t(a) < std::size_t(kPortActions) ? kPort[std::size_t(a)].use : ""; }
bool port_photo(PortAction a) { return std::size_t(a) < std::size_t(kPortActions) && kPort[std::size_t(a)].photo; }
bool port_replay(PortAction a) { return a >= PortAction::ReplayCameraPrev && a <= PortAction::ReplayFreeCamera; }
bool port_page(PortAction a) {
    return a == PortAction::RaceTime || a == PortAction::PaintPanel || a == PortAction::Replay || port_replay(a);
}
bool port_has_pad(PortAction a) { return std::size_t(a) < std::size_t(kPortActions) && kPort[std::size_t(a)].pad; }
bool port_analog(PortAction a) { return std::size_t(a) < std::size_t(kPortActions) && kPort[std::size_t(a)].analog; }

std::optional<PortAction> parse_port_action(std::string_view s) {
    const std::string l = lower(trim(s));
    for (int a = 0; a < kPortActions; ++a)
        if (l == kPort[a].key) return PortAction(a);
    return std::nullopt;
}

const PortControls& default_port() {
    static const PortControls d = make_default_port();
    return d;
}

std::uint64_t port_actions_of_key(const PortControls& p, std::uint16_t scancode) {
    std::uint64_t m = 0;
    for (int a = 0; a < kPortActions; ++a) {
        const auto& l = p.keys[std::size_t(a)];
        if (std::find(l.begin(), l.end(), scancode) != l.end()) m |= std::uint64_t(1) << a;
    }
    return m;
}

float port_pad_amount(const GamepadState& g, const PortControls& p, PortAction a, float dead_zone) {
    const auto& l = p.pad[std::size_t(a)];
    if (l.empty()) return 0.0f;
    float v = 1.0f;
    for (PadInput in : l) v = std::min(v, pad_input_amount(g, in, dead_zone));
    return v;
}

bool take_port_chord(GamepadState& g, const PortControls& p, PortAction a, float dead_zone) {
    if (port_pad_amount(g, p, a, dead_zone) < 0.5f) return false;
    for (PadInput in : p.pad[std::size_t(a)]) {
        switch (in) {
        case PadInput::South: g.south = false; break;
        case PadInput::East: g.east = false; break;
        case PadInput::West: g.west = false; break;
        case PadInput::North: g.north = false; break;
        case PadInput::Back: g.back = false; break;
        case PadInput::Guide: g.guide = false; break;
        case PadInput::Start: g.start = false; break;
        case PadInput::LeftStick: g.left_stick = false; break;
        case PadInput::RightStick: g.right_stick = false; break;
        case PadInput::LeftShoulder: g.left_shoulder = false; break;
        case PadInput::RightShoulder: g.right_shoulder = false; break;
        case PadInput::DpadUp: g.dpad_up = false; break;
        case PadInput::DpadDown: g.dpad_down = false; break;
        case PadInput::DpadLeft: g.dpad_left = false; break;
        case PadInput::DpadRight: g.dpad_right = false; break;
        case PadInput::Misc1: g.misc1 = false; break;
        case PadInput::RightPaddle1: g.paddles[0] = false; break;
        case PadInput::LeftPaddle1: g.paddles[1] = false; break;
        case PadInput::RightPaddle2: g.paddles[2] = false; break;
        case PadInput::LeftPaddle2: g.paddles[3] = false; break;
        case PadInput::Touchpad: g.touchpad = false; break;
        case PadInput::LeftXNeg: case PadInput::LeftXPos: g.left_x = 0; break;
        case PadInput::LeftYNeg: case PadInput::LeftYPos: g.left_y = 0; break;
        case PadInput::RightXNeg: case PadInput::RightXPos: g.right_x = 0; break;
        case PadInput::RightYNeg: case PadInput::RightYPos: g.right_y = 0; break;
        case PadInput::LeftTrigger: g.left_trigger = 0; break;
        case PadInput::RightTrigger: g.right_trigger = 0; break;
        default: break;
        }
    }
    return true;
}

bool page_reads_input(const Controls& c, const PhysInput& in) { return page_reads_input(c, in, PortAction::RaceTime); }

bool page_reads_input(const Controls& c, const PhysInput& in, PortAction a) {
    if (port_replay(a)) return false;   // replaycams: the game reads no input while its replay plays
    static constexpr Ds2 kRead[] = {Ds2::Cross, Ds2::Triangle, Ds2::Up, Ds2::Down, Ds2::Left, Ds2::Right,
                                    Ds2::SteerLeft, Ds2::SteerRight, Ds2::StickUp, Ds2::StickDown, Ds2::Square};
    // paintsel: SELECT CAR reads Square too (the colour: car_select.h), the track pages do not
    const std::size_t n = std::size(kRead) - (a == PortAction::PaintPanel ? 0 : 1);
    for (int p = 0; p < kPlayers; ++p) {
        const PlayerControls& pc = c.player[std::size_t(p)];
        for (std::size_t gi = 0; gi < n; ++gi) {
            const Ds2 g = kRead[gi];
            if (in.device == Device::Keyboard) {
                for (const KeyBind& k : pc.keys[idx(g)])
                    if (k.scancode == in.key && overlap(k.ctx, KeyCtx::Menu)) return true;
            } else {
                for (PadStyle f : {PadStyle::Generic, PadStyle::PlayStation, PadStyle::Xbox, PadStyle::Nintendo})
                    if (has_input(pad_bindings(pc, f)[idx(g)], in.pad, KeyCtx::Menu)) return true;
            }
        }
    }
    return false;
}

bool port_page_safe(const Controls& c, PortAction a, Device d) {
    for (const PhysInput& in : bound_port(c, a, d))
        if (page_reads_input(c, in, a)) return false;
    return true;
}

std::vector<PhysInput> bound_port(const Controls& c, PortAction a, Device d) {
    std::vector<PhysInput> o;
    if (d == Device::Keyboard) {
        for (std::uint16_t k : c.port.keys[pidx(a)]) o.push_back({Device::Keyboard, k, PadInput::None});
    } else {
        for (PadInput in : c.port.pad[pidx(a)]) o.push_back({Device::Gamepad, 0, in});
    }
    return o;
}

std::string bound_port_text(const Controls& c, PortAction a, Device d, PadStyle style) {
    std::string o;
    for (const PhysInput& in : bound_port(c, a, d)) {
        if (!o.empty()) o += d == Device::Gamepad ? " + " : ", ";
        o += input_label(in, style);
    }
    return o.empty() ? std::string(kUnbound) : o;   // settingsui: a word, not "-"
}

std::vector<Use> uses_port(const Controls& c, PortAction a, Device d, const PhysInput& in) {
    std::vector<Use> o;
    for (int b = 0; b < kPortActions; ++b) {
        if (PortAction(b) == a) continue;
        // todrace: a page's action and photo mode's are never active together
        if ((port_page(a) && port_photo(PortAction(b))) || (port_photo(a) && port_page(PortAction(b)))) continue;
        // paintsel: photo mode is entered from the race's pause menu only, never on a page action's page (P is both)
        if ((port_page(a) && PortAction(b) == PortAction::PhotoMode) || (a == PortAction::PhotoMode && port_page(PortAction(b)))) continue;
        if (d == Device::Keyboard) {
            const auto& l = c.port.keys[std::size_t(b)];
            if (std::find(l.begin(), l.end(), in.key) != l.end()) o.push_back({0, Ds2::Cross, true, PortAction(b)});
        } else {
            const auto& l = c.port.pad[std::size_t(b)];
            if (l.size() == 1 && l[0] == in.pad) o.push_back({0, Ds2::Cross, true, PortAction(b)});   // a chord shares its buttons
        }
    }
    if (port_global(a))
        for (int p = 0; p < kPlayers; ++p)
            for (Ds2 g : action_order()) {
                const PlayerControls& pc = c.player[std::size_t(p)];
                bool hit = false;
                if (d == Device::Keyboard) {
                    for (const KeyBind& k : pc.keys[idx(g)]) hit = hit || k.scancode == in.key;
                } else {
                    hit = has_input(pad_bindings(pc, c.family[std::size_t(p)])[idx(g)], in.pad);
                }
                if (hit) o.push_back({p, g});
            }
    return o;
}

void rebind_port(Controls& c, PortAction a, Device d, const PhysInput& in, Resolve r, bool add) {
    const std::vector<PhysInput> before = bound_port(c, a, d);
    std::optional<PhysInput> old_first;
    for (const PhysInput& b : before)
        if (!(b == in)) { old_first = b; break; }
    const std::vector<Use> others = uses_port(c, a, d, in);
    if (d == Device::Keyboard) {
        auto& l = c.port.keys[pidx(a)];
        if (!add) l.clear();
        if (std::find(l.begin(), l.end(), in.key) == l.end()) l.push_back(in.key);
    } else {
        auto& l = c.port.pad[pidx(a)];
        if (!add) l.clear();
        if (std::find(l.begin(), l.end(), in.pad) == l.end()) l.push_back(in.pad);
    }
    if (r == Resolve::Keep) return;
    for (const Use& u : others) {
        if (d == Device::Keyboard) {
            if (u.port) {
                auto& o = c.port.keys[pidx(u.port_action)];
                o.erase(std::remove(o.begin(), o.end(), in.key), o.end());
                if (r == Resolve::Swap && !add && old_first && std::find(o.begin(), o.end(), old_first->key) == o.end()) o.push_back(old_first->key);
            } else {
                auto& o = c.player[std::size_t(u.player)].keys[idx(u.action)];
                remove_from_view(o, in.key, KeyCtx::Any);
                if (r == Resolve::Swap && !add && old_first) {
                    o.push_back({old_first->key, KeyCtx::Any});
                    normalize(o);
                }
            }
        } else if (u.port) {
            auto& o = c.port.pad[pidx(u.port_action)];
            o.erase(std::remove(o.begin(), o.end(), in.pad), o.end());
            if (r == Resolve::Swap && !add && old_first && std::find(o.begin(), o.end(), old_first->pad) == o.end()) o.push_back(old_first->pad);
        } else {   // padlayout: a game action's (the shown layout becomes the player's own)
            edit_pad(c, u.player, [&](PadBindings& b) {
                auto& o = b[idx(u.action)];
                std::erase_if(o, [&](const PadBind& x) { return x.in == in.pad; });
                if (r == Resolve::Swap && !add && old_first && !has_input(o, old_first->pad)) o.push_back(old_first->pad);
            });
        }
    }
}

void remove_last_port(Controls& c, PortAction a, Device d) {
    if (d == Device::Keyboard) {
        auto& l = c.port.keys[pidx(a)];
        if (!l.empty()) l.pop_back();
    } else {
        auto& l = c.port.pad[pidx(a)];
        if (!l.empty()) l.pop_back();
    }
}

void reset_port(Controls& c, PortAction a, Device d) {
    if (d == Device::Keyboard) c.port.keys[pidx(a)] = default_port().keys[pidx(a)];
    else c.port.pad[pidx(a)] = default_port().pad[pidx(a)];
}

void reset_action(Controls& c, int player, Device d, Ds2 action, KeyCtx view) {
    player &= 1;
    if (d == Device::Gamepad) {   // padlayout: the binding of the layout the player has (Custom: Auto's for the pad)
        const PadBindings& def = pad_default(c.player[std::size_t(player)], c.family[std::size_t(player)]);
        edit_pad(c, player, [&](PadBindings& b) { reset_in_view(b[idx(action)], def[idx(action)], view); });
        return;
    }
    reset_in_view(c.player[std::size_t(player)].keys[idx(action)], default_player(player).keys[idx(action)], view);
}

void reset_all(Controls& c) {
    const auto family = c.family;
    c = default_controls();
    c.family = family;
}

std::string use_label(const Use& u) {
    if (u.port) return port_label(u.port_action);
    return std::string(u.player ? "PLAYER 2 " : "PLAYER 1 ") + action_label(u.action);
}

// ---- the bindings ----

bool PlayerControls::has_keys() const {
    for (const auto& l : keys)
        if (!l.empty()) return true;
    return false;
}

const PlayerControls& default_player(int player) {
    static const PlayerControls d[2] = {make_default(0), make_default(1)};
    return d[player == 1 ? 1 : 0];
}

Controls default_controls() {
    Controls c;
    c.player[0] = default_player(0);
    c.player[1] = default_player(1);
    c.port = default_port();
    return c;
}

// ---- padlayout: the controller layouts ----

namespace {
const char* const kLayoutKeys[kPadLayouts] = {"auto", "xbox", "playstation", "nintendo", "custom"};
}

const char* pad_layout_key(PadLayout l) { return std::size_t(l) < kPadLayouts ? kLayoutKeys[std::size_t(l)] : "?"; }

std::optional<PadLayout> parse_pad_layout(std::string_view s) {
    const std::string l = lower(trim(s));
    for (std::size_t k = 0; k < kPadLayouts; ++k)
        if (l == kLayoutKeys[k]) return PadLayout(k);
    if (l == "ps" || l == "ps2") return PadLayout::PlayStation;
    if (l == "switch") return PadLayout::Nintendo;
    return std::nullopt;
}

PadStyle pad_family(int t) {
    switch (t) {
    case kSdlPadPS3: case kSdlPadPS4: case kSdlPadPS5: return PadStyle::PlayStation;
    case kSdlPadXbox360: case kSdlPadXboxOne: return PadStyle::Xbox;
    default: break;
    }
    if (t >= kSdlPadSwitchPro && t <= kSdlPadGameCube) return PadStyle::Nintendo;   // Switch Pro, Joy-Cons, GameCube
    return PadStyle::Generic;
}

PadLayout resolved_layout(PadLayout l, PadStyle family) {
    if (l != PadLayout::Auto) return l;
    return family == PadStyle::PlayStation ? PadLayout::PlayStation : PadLayout::Xbox;
}

const PadBindings& layout_bindings(PadLayout l) {
    static const PadBindings xbox = make_xbox(), ps = make_playstation(), nintendo = make_nintendo();
    switch (l) {
    case PadLayout::PlayStation: return ps;
    case PadLayout::Nintendo: return nintendo;
    case PadLayout::Custom: return default_player(0).pad;
    default: return xbox;
    }
}

const PadBindings& pad_bindings(const PlayerControls& p, PadStyle family) {
    const PadLayout l = resolved_layout(p.layout, family);
    return l == PadLayout::Custom ? p.pad : layout_bindings(l);
}

const PadBindings& pad_default(const PlayerControls& p, PadStyle family) {
    return layout_bindings(resolved_layout(p.layout == PadLayout::Custom ? PadLayout::Auto : p.layout, family));
}

bool right_stick_bound(const PadBindings& b, KeyCtx ctx, const PortControls* port) {
    const auto axis = [](PadInput in) { return in == PadInput::RightXNeg || in == PadInput::RightXPos || in == PadInput::RightYNeg || in == PadInput::RightYPos; };
    for (const auto& binds : b)
        for (const PadBind& k : binds)
            if (axis(k.in) && (k.ctx == KeyCtx::Any || ctx == KeyCtx::Any || k.ctx == ctx)) return true;
    if (port)
        for (int a = 0; a < kPortActions; ++a) {
            const auto pa = PortAction(a);
            if ((port_photo(pa) && pa != PortAction::PhotoMode) || port_page(pa)) continue;
            for (PadInput in : port->pad[std::size_t(a)]) if (axis(in)) return true;
        }
    return false;
}

void set_page_family(Controls& c, int player, PadStyle family) { c.family[std::size_t(player & 1)] = family; }
PadStyle page_family(const Controls& c, int player) { return c.family[std::size_t(player & 1)]; }

void set_layout(Controls& c, int player, PadLayout l) { c.player[std::size_t(player & 1)].layout = l; }

std::string layout_text(const PlayerControls& p, PadStyle family, bool connected) {
    switch (p.layout) {
    case PadLayout::Xbox: return "Xbox";
    case PadLayout::PlayStation: return "PlayStation";
    case PadLayout::Nintendo: return "Nintendo";
    case PadLayout::Custom: return "Custom";
    case PadLayout::Auto: break;
    }
    return auto_text(family, connected);
}

std::string auto_text(PadStyle family, bool connected) {
    if (!connected) return "Auto: no controller, Xbox layout";
    switch (family) {
    case PadStyle::PlayStation: return "Auto: PlayStation controller";
    case PadStyle::Xbox: return "Auto: Xbox controller";
    case PadStyle::Nintendo: return "Auto: Nintendo, Xbox layout";
    case PadStyle::Generic: break;
    }
    return "Auto: controller, Xbox layout";
}

std::vector<KeyBinding> key_map(const PlayerControls& p) {
    std::vector<KeyBinding> m;
    for (int a = 0; a < kActions; ++a)
        for (const KeyBind& k : p.keys[std::size_t(a)]) m.push_back({k.scancode, Ds2(a), k.ctx});
    return m;
}

std::array<std::uint8_t, 32> gamepad_sample(const GamepadState& g, const PlayerControls& p, const GamepadTuning& t, KeyCtx now,
                                            PadStyle family) {
    return gamepad_sample(g, pad_bindings(p, family), t, now);
}

std::array<std::uint8_t, 32> gamepad_sample(const GamepadState& g, const PadBindings& pb, const GamepadTuning& t, KeyCtx now) {
    // padlayout: the bindings of this context (and of both)
    std::array<std::vector<PadInput>, kActions> pads;
    for (int a = 0; a < kActions; ++a)
        for (const PadBind& b : pb[std::size_t(a)])
            if (overlap(b.ctx, now)) pads[std::size_t(a)].push_back(b.in);
    std::array<std::uint8_t, 32> s = neutral_sample();
    const float z = std::clamp(t.stick_dead_zone, 0.0f, 0.95f);
    const float theta = std::clamp(t.trigger_threshold, 0.01f, 1.0f);
    // 1.0.1 ANALOG PEDALS (input_keys.h): the analog inputs on Cross (accelerate) and Square (brake) as pedals on the
    // right stick, unless both are past the threshold (a powerslide: both buttons, as 1.0.0)
    const auto analog_travel = [&](Ds2 c) {
        float v = 0;
        for (PadInput in : pads[idx(c)]) if (pad_input_analog(in)) v = std::max(v, pad_input_amount(g, in, z));
        return v;
    };
    const float acc = analog_travel(Ds2::Cross), brk = analog_travel(Ds2::Square);
    const bool pedals = t.analog_pedals && !(acc >= theta && brk >= theta);
    for (int b = 0; b < 16; ++b) {
        const bool pedal = pedals && (Ds2(b) == Ds2::Cross || Ds2(b) == Ds2::Square);
        bool held = false;
        float amount = 0;
        for (PadInput in : pads[std::size_t(b)]) {
            const float a = pad_input_amount(g, in, z);
            if (!pad_input_analog(in)) {
                if (a > 0) { held = true; amount = 1.0f; }
            } else if (!pedal) {
                amount = std::max(amount, a);   // the pressure follows the travel (below the threshold too)
                if (a >= theta) held = true;
            }
        }
        if (held) press(s, b);
        if (const int pb = pressure_byte(Ds2(b)); pb >= 0) s[std::size_t(pb)] = static_cast<std::uint8_t>(std::lround(255.0f * amount));
    }
    const auto half = [&](Ds2 a) {
        float v = 0;
        for (PadInput in : pads[idx(a)]) v = std::max(v, pad_input_amount(g, in, z));
        return v;
    };
    float steer = half(Ds2::SteerRight) - half(Ds2::SteerLeft);
    if (g.gyro_use == 1) steer = std::clamp(steer + g.gyro_steer, -1.0f, 1.0f);   // pad1: gyro steering, mixed
    else if (g.gyro_use == 2) steer = std::clamp(g.gyro_steer, -1.0f, 1.0f);      // in the stick's place
    s[4] = stick_x_byte(steer);
    s[5] = stick_y_byte(half(Ds2::StickDown) - half(Ds2::StickUp));   // SDL's y: down positive, as the report's
    s[2] = stick_x_byte(half(Ds2::RStickRight) - half(Ds2::RStickLeft));
    const float up = std::max(half(Ds2::RStickUp), pedals ? pedal_stick(acc) : 0.0f);
    const float down = std::max(half(Ds2::RStickDown), pedals ? pedal_stick(brk) : 0.0f);
    s[3] = stick_y_byte(down - up);
    return s;
}

const std::array<PadInput, 8>& gyro_recentre_choices() {
    static const std::array<PadInput, 8> c = {PadInput::None, PadInput::Touchpad, PadInput::Back, PadInput::LeftStick,
                                               PadInput::RightStick, PadInput::Misc1, PadInput::LeftPaddle1, PadInput::RightPaddle1};
    return c;
}

bool apply_gyro(GamepadState& s, b3native::pad::GyroSteer& steer, const GyroControls& g, bool driving, bool driving_started,
                bool& recentre_held) {
    if (g.mode == 0) {
        recentre_held = false;
        return false;
    }
    const bool held = g.recentre != PadInput::None && !pad_input_analog(g.recentre) && pad_input_amount(s, g.recentre, 0.24f) > 0;
    const bool recentred = held && !recentre_held;
    recentre_held = held;
    if (recentred) steer.recentre();
    if (driving_started) steer.zero_yaw();   // yaw has no reference: straight where driving starts
    if (driving && steer.has_data()) {
        s.gyro_use = std::uint8_t(std::min<int>(g.mode, 2));
        s.gyro_steer = steer.steer(b3native::pad::GyroAxis(g.axis & 1), g.sensitivity, g.dead_zone);
    }
    return recentred;
}

std::uint8_t trigger_role(const PadBindings& b, bool right) {
    const PadInput t = right ? PadInput::RightTrigger : PadInput::LeftTrigger;
    const auto has = [&](Ds2 a) { return has_input(b[idx(a)], t, KeyCtx::Drive); };
    return has(Ds2::Square) ? 1 : has(Ds2::Cross) ? 2 : 0;
}

namespace {
const char* const kGyroModes[] = {"off", "mix", "replace"};
const char* const kGyroAxes[] = {"roll", "yaw"};
}  // namespace

// ---- controls.ini ----

std::string to_ini(const Controls& c) {
    std::string o = "# Burnout 3 controls (docs/pc-settings.md 7.9): one line per action that differs from its default.\n"
                    "# Keys: names, @menus / @driving for one context only. Gamepad: layout = auto / xbox / playstation /\n"
                    "# nintendo / custom; the custom layout's inputs: south east west north, left_x- ..., left_trigger right_trigger\n"
                    "# (@menus / @driving too). none: unbound.\n";
    o += "version = " + std::to_string(kControlsVersion) + "\n";
    for (int p = 0; p < kPlayers; ++p) {
        const PlayerControls& d = default_player(p);
        const PlayerControls& now = c.player[std::size_t(p)];
        for (int dev = 0; dev < 2; ++dev) {
            std::string lines;
            // padlayout: the layout line when it is not what a file without one reads as (Auto with the 1.x pad defaults)
            if (dev == 1 && (now.layout != PadLayout::Auto || now.pad != d.pad)) lines += std::string("layout = ") + pad_layout_key(now.layout) + "\n";
            for (Ds2 a : action_order()) {
                if (dev == 0 && now.keys[idx(a)] != d.keys[idx(a)]) lines += std::string(action_key(a)) + " = " + join_keys(now.keys[idx(a)]) + "\n";
                if (dev == 1 && now.pad[idx(a)] != d.pad[idx(a)]) lines += std::string(action_key(a)) + " = " + join_pad(now.pad[idx(a)]) + "\n";
            }
            if (!lines.empty()) o += "\n[player" + std::to_string(p + 1) + (dev == 0 ? ".keyboard]\n" : ".gamepad]\n") + lines;
        }
        // pad1: gyro steering, the lines that differ from the defaults
        const GyroControls& gd = d.gyro;
        const GyroControls& gn = now.gyro;
        std::string lines;
        if (gn.mode != gd.mode) lines += std::string("mode = ") + kGyroModes[std::min<int>(gn.mode, 2)] + "\n";
        if (gn.axis != gd.axis) lines += std::string("axis = ") + kGyroAxes[std::min<int>(gn.axis, 1)] + "\n";
        if (gn.sensitivity != gd.sensitivity) lines += "sensitivity = " + std::to_string(gn.sensitivity) + "\n";
        if (gn.dead_zone != gd.dead_zone) lines += "dead_zone = " + std::to_string(gn.dead_zone) + "\n";
        if (gn.recentre != gd.recentre) lines += std::string("recentre = ") + (gn.recentre == PadInput::None ? "none" : pad_input_key(gn.recentre)) + "\n";
        if (!lines.empty()) o += "\n[player" + std::to_string(p + 1) + ".gyro]\n" + lines;
    }
    // rebind2: the port actions (one set), the lines that differ from the defaults; a gamepad chord joins its inputs with '+'
    for (int dev = 0; dev < 2; ++dev) {
        std::string lines;
        for (int a = 0; a < kPortActions; ++a) {
            if (dev == 0 && c.port.keys[std::size_t(a)] != default_port().keys[std::size_t(a)]) {
                std::string v;
                for (std::uint16_t k : c.port.keys[std::size_t(a)]) v += (v.empty() ? "" : ", ") + key_name(k);
                lines += std::string(port_key(PortAction(a))) + " = " + (v.empty() ? "none" : v) + "\n";
            }
            if (dev == 1 && c.port.pad[std::size_t(a)] != default_port().pad[std::size_t(a)]) {
                std::string v;
                for (PadInput p : c.port.pad[std::size_t(a)]) v += (v.empty() ? "" : "+") + std::string(pad_input_key(p));
                lines += std::string(port_key(PortAction(a))) + " = " + (v.empty() ? "none" : v) + "\n";
            }
        }
        if (!lines.empty()) o += std::string("\n[port.") + (dev == 0 ? "keyboard" : "gamepad") + "]\n" + lines;
    }
    return o;
}

Controls controls_from_ini(std::string_view text, std::vector<std::string>* warnings) {
    Controls c = default_controls();
    auto warn = [&](int line, const std::string& w) {
        if (warnings) warnings->push_back("controls.ini line " + std::to_string(line) + ": " + w);
    };
    int player = -1, device = -1, n = 0, port_dev = -1;
    bool section_bad = false, saw_version = false;
    int version = 1;                                   // a file without a version line is a version 1 file
    std::array<std::array<bool, kActions>, kPlayers> pad_given{};   // the gamepad actions the file sets
    std::array<bool, kPlayers> layout_given{};                        // padlayout: a layout line
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t e = text.find('\n', at);
        if (e == std::string_view::npos) e = text.size();
        std::string_view line = text.substr(at, e - at);
        at = e + 1;
        ++n;
        if (const std::size_t h = line.find_first_of("#;"); h != std::string_view::npos) line = line.substr(0, h);
        line = trim(line);
        if (line.empty()) {
            if (e == text.size()) break;
            continue;
        }
        if (line.front() == '[') {
            const std::string name = lower(trim(line.substr(1, line.find(']') == std::string_view::npos ? line.size() - 1 : line.find(']') - 1)));
            player = device = -1;
            port_dev = -1;
            section_bad = true;
            if (name == "port.keyboard" || name == "port.gamepad") {   // rebind2: the port actions
                port_dev = name == "port.keyboard" ? 0 : 1;
                section_bad = false;
                continue;
            }
            if (name.size() > 7 && name.compare(0, 6, "player") == 0 && (name[6] == '1' || name[6] == '2') && name[7] == '.') {
                const std::string dev = name.substr(8);
                if (dev == "keyboard" || dev == "gamepad" || dev == "gyro") {
                    player = name[6] - '1';
                    device = dev == "keyboard" ? 0 : dev == "gamepad" ? 1 : 2;   // pad1: 2 the gyro
                    section_bad = false;
                }
            }
            if (section_bad) warn(n, "unknown section [" + name + "] (skipped)");
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) { warn(n, "not key = value"); continue; }
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string_view value = trim(line.substr(eq + 1));
        if (key == "version") {
            saw_version = true;
            const int v = std::atoi(std::string(value).c_str());
            if (v < 1) warn(n, "bad version");
            else if (v > kControlsVersion) warn(n, "version " + std::to_string(v) + " is newer than this game's " + std::to_string(kControlsVersion) + ": unknown entries are skipped");
            if (v >= 1) version = v;
            continue;
        }
        if (section_bad) continue;
        if (port_dev >= 0) {   // rebind2: [port.keyboard] key list; [port.gamepad] a chord, inputs joined with '+'
            const std::optional<PortAction> pa = parse_port_action(key);
            if (!pa) { warn(n, "unknown port action '" + key + "'"); continue; }
            const std::string v = lower(std::string(value));
            if (v.empty() || v == "none") {
                (port_dev == 0 ? c.port.keys[std::size_t(*pa)].clear() : c.port.pad[std::size_t(*pa)].clear());
                continue;
            }
            std::vector<std::uint16_t> ks;
            std::vector<PadInput> ps;
            if (port_dev == 1) {   // robust2: read input by input (an axis input ends in '+' itself)
                std::vector<std::string> unknown;
                ps = parse_pad_chord(value, &unknown);
                for (const std::string& u : unknown) warn(n, "unknown gamepad input '" + u + "'");
            }
            for (std::size_t a = 0; port_dev == 0 && a <= value.size();) {   // [port.keyboard]: keys joined with ','
                std::size_t b = value.find(',', a);
                if (b == std::string_view::npos) b = value.size();
                const std::string_view it = trim(value.substr(a, b - a));
                if (!it.empty()) {
                    const std::optional<std::uint16_t> k = parse_key(it);
                    if (!k) warn(n, "unknown key '" + std::string(it) + "'");
                    else if (std::find(ks.begin(), ks.end(), *k) == ks.end()) ks.push_back(*k);
                }
                a = b + 1;
            }
            if (port_dev == 0 ? ks.empty() : ps.empty()) { warn(n, std::string(port_key(*pa)) + ": no valid input, the default kept"); continue; }
            if (port_dev == 0) c.port.keys[std::size_t(*pa)] = std::move(ks);
            else c.port.pad[std::size_t(*pa)] = std::move(ps);
            continue;
        }
        if (player < 0) { warn(n, "'" + key + "' outside a [playerN.keyboard] / [playerN.gamepad] section"); continue; }
        if (device == 2) {   // pad1: [playerN.gyro]
            GyroControls& g = c.player[std::size_t(player)].gyro;
            const std::string v = lower(std::string(value));
            const auto number = [&](int lo, int hi, int step, int& out) {
                char* end = nullptr;
                const long x = std::strtol(v.c_str(), &end, 10);
                if (v.empty() || !end || *end != '\0' || x < lo || x > hi || (x - lo) % step != 0) {
                    warn(n, key + ": '" + std::string(value) + "' is not " + std::to_string(lo) + ".." + std::to_string(hi) +
                                (step > 1 ? " in steps of " + std::to_string(step) : "") + " (the default kept)");
                    return;
                }
                out = int(x);
            };
            if (key == "mode") {
                const auto* it = std::find_if(std::begin(kGyroModes), std::end(kGyroModes), [&](const char* m) { return v == m; });
                if (it == std::end(kGyroModes)) warn(n, "mode: '" + std::string(value) + "' (off, mix, replace)");
                else g.mode = std::uint8_t(it - std::begin(kGyroModes));
            } else if (key == "axis") {
                const auto* it = std::find_if(std::begin(kGyroAxes), std::end(kGyroAxes), [&](const char* m) { return v == m; });
                if (it == std::end(kGyroAxes)) warn(n, "axis: '" + std::string(value) + "' (roll, yaw)");
                else g.axis = std::uint8_t(it - std::begin(kGyroAxes));
            } else if (key == "sensitivity") {
                number(25, 300, 25, g.sensitivity);
            } else if (key == "dead_zone") {
                number(0, 10, 1, g.dead_zone);
            } else if (key == "recentre") {
                const std::optional<PadInput> p = v == "none" ? std::optional<PadInput>(PadInput::None) : parse_pad_input(v);
                const auto& ch = gyro_recentre_choices();
                if (!p || std::find(ch.begin(), ch.end(), *p) == ch.end())
                    warn(n, "recentre: '" + std::string(value) + "' (none, touchpad, back, left_stick, right_stick, misc1, left_paddle1, right_paddle1)");
                else g.recentre = *p;
            } else {
                warn(n, "unknown gyro entry '" + key + "'");
            }
            continue;
        }
        if (device == 1 && key == "layout") {   // padlayout (version 3)
            const std::optional<PadLayout> l = parse_pad_layout(value);
            if (!l) { warn(n, "layout: '" + std::string(value) + "' (auto, xbox, playstation, custom)"); continue; }
            c.player[std::size_t(player)].layout = *l;
            layout_given[std::size_t(player)] = true;
            continue;
        }
        const std::optional<Ds2> act = parse_action(key);
        if (!act) { warn(n, "unknown action '" + key + "'"); continue; }
        // the inputs
        std::vector<std::string_view> items;
        for (std::size_t a = 0; a <= value.size();) {
            std::size_t b = value.find(',', a);
            if (b == std::string_view::npos) b = value.size();
            const std::string_view it = trim(value.substr(a, b - a));
            if (!it.empty()) items.push_back(it);
            a = b + 1;
        }
        const bool none = items.empty() || (items.size() == 1 && lower(items[0]) == "none");
        PlayerControls& pc = c.player[std::size_t(player)];
        // an item's context suffix (@menus / @driving): false when it is not one
        const auto split_ctx = [&](std::string_view it, std::string_view& nm, KeyCtx& ctx) {
            nm = it;
            ctx = KeyCtx::Any;
            if (const std::size_t q = it.rfind('@'); q != std::string_view::npos && q > 0) {
                const std::string cx = lower(trim(it.substr(q + 1)));
                nm = trim(it.substr(0, q));
                if (cx == "menus" || cx == "menu") ctx = KeyCtx::Menu;
                else if (cx == "driving" || cx == "drive") ctx = KeyCtx::Drive;
                else if (cx != "any" && cx != "both") { warn(n, "unknown context '@" + cx + "' (menus, driving)"); return false; }
            }
            return true;
        };
        if (device == 0) {
            std::vector<KeyBind> l;
            if (!none)
                for (std::string_view it : items) {
                    std::string_view nm;
                    KeyCtx ctx;
                    if (!split_ctx(it, nm, ctx)) continue;
                    const std::optional<std::uint16_t> k = parse_key(nm);
                    if (!k) { warn(n, "unknown key '" + std::string(nm) + "'"); continue; }
                    l.push_back({*k, ctx});
                }
            if (!none && l.empty()) { warn(n, std::string(action_key(*act)) + ": no valid key, the default kept"); continue; }
            normalize(l);
            pc.keys[idx(*act)] = std::move(l);
        } else {
            std::vector<PadBind> l;
            if (!none)
                for (std::string_view it : items) {
                    std::string_view nm;
                    KeyCtx ctx;
                    if (!split_ctx(it, nm, ctx)) continue;
                    const std::optional<PadInput> p = parse_pad_input(nm);
                    if (!p) { warn(n, "unknown gamepad input '" + std::string(nm) + "'"); continue; }
                    l.push_back({*p, ctx});
                }
            if (!none && l.empty()) { warn(n, std::string(action_key(*act)) + ": no valid input, the default kept"); continue; }
            normalize(l);
            pc.pad[idx(*act)] = std::move(l);
            pad_given[std::size_t(player)][idx(*act)] = true;
        }
    }
    // padr2 (version 2): the stick clicks became L2 / R2's defaults. A version 1 file was written when they drove nothing
    // by default, so a click it binds to another action stays that action's only (not L2 / R2's too), as it was then.
    if (version < 2)
        for (int p = 0; p < kPlayers; ++p) {
            PlayerControls& pc = c.player[std::size_t(p)];
            for (const auto& [act, click] : {std::pair{Ds2::L2, PadInput::LeftStick}, std::pair{Ds2::R2, PadInput::RightStick}}) {
                if (pad_given[std::size_t(p)][idx(act)]) continue;
                bool elsewhere = false;
                for (int a = 0; a < kActions; ++a)
                    if (Ds2(a) != act && has_input(pc.pad[std::size_t(a)], click)) elsewhere = true;
                if (elsewhere) std::erase_if(pc.pad[idx(act)], [&](const PadBind& b) { return b.in == click; });
            }
        }
    // padlayout: no layout line (every file before 1.2.1): a gamepad remap stays the player's (Custom), exactly as it
    // was; a file with the 1.x pad defaults gets Auto
    for (int p = 0; p < kPlayers; ++p) {
        PlayerControls& pc = c.player[std::size_t(p)];
        if (!layout_given[std::size_t(p)]) pc.layout = pc.pad != default_player(p).pad ? PadLayout::Custom : PadLayout::Auto;
    }
    if (!saw_version && warnings && c != default_controls()) warnings->push_back("controls.ini: no version line (read as version 1)");
    return c;
}

bool load_controls(const std::string& path, Controls& c, std::vector<std::string>* warnings) {
    std::ifstream f(b3io::u8path(path), std::ios::binary);   // fsrobust: UTF-8 path (Windows: opened wide)
    if (!f) { c = default_controls(); return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    c = controls_from_ini(ss.str(), warnings);
    return true;
}

bool save_controls(const std::string& path, const Controls& c, std::string* why, bool durable) {
    return b3io::replace_file(b3io::u8path(path), to_ini(c), why, durable);   // fsrobust: path.tmp, flushed, then one replace
}

// ---- editing ----

std::vector<PhysInput> bound(const Controls& c, int player, Device d, Ds2 action, KeyCtx view) {
    std::vector<PhysInput> o;
    const PlayerControls& p = c.player[std::size_t(player & 1)];
    if (d == Device::Keyboard) {
        for (const KeyBind& k : p.keys[idx(action)])
            if (overlap(k.ctx, view)) o.push_back({Device::Keyboard, k.scancode, PadInput::None});
    } else {   // padlayout: the bindings shown (the layout for the page's pad family), in the view
        for (const PadBind& b : pad_bindings(p, c.family[std::size_t(player & 1)])[idx(action)])
            if (overlap(b.ctx, view)) o.push_back({Device::Gamepad, 0, b.in});
    }
    return o;
}

std::string input_label(const PhysInput& in, PadStyle style) {
    return in.device == Device::Keyboard ? key_label(in.key) : pad_input_label(in.pad, style);
}

std::string bound_text(const Controls& c, int player, Device d, Ds2 action, KeyCtx view, PadStyle style) {
    std::string o;
    for (const PhysInput& in : bound(c, player, d, action, view)) {
        if (!o.empty()) o += ", ";
        o += input_label(in, style);
    }
    return o.empty() ? std::string(kUnbound) : o;   // settingsui: a word, not "-"
}

bool moved(const Controls& c, int player, Device d, Ds2 action, KeyCtx view) {
    Controls def = c;
    reset_action(def, player, d, action, view);
    return bound(def, player, d, action, view) != bound(c, player, d, action, view);
}

std::vector<Use> uses(const Controls& c, int player, Device d, Ds2 action, const PhysInput& in, KeyCtx view) {
    std::vector<Use> o;
    for (int p = 0; p < kPlayers; ++p) {
        if (d == Device::Gamepad && p != (player & 1)) continue;   // each player's pads are their own
        const PlayerControls& pc = c.player[std::size_t(p)];
        for (Ds2 a : action_order()) {
            if (p == (player & 1) && a == action) continue;
            bool hit = false;
            if (d == Device::Keyboard) {
                for (const KeyBind& k : pc.keys[idx(a)]) hit = hit || (k.scancode == in.key && overlap(k.ctx, view));
            } else {
                hit = has_input(pad_bindings(pc, c.family[std::size_t(p)])[idx(a)], in.pad, view);
            }
            if (hit) o.push_back({p, a});
        }
    }
    // rebind2: the port actions that are global (the window's shortcuts) take their key whatever the game is doing
    for (int b = 0; b < kPortActions; ++b) {
        if (!port_global(PortAction(b))) continue;
        if (d == Device::Keyboard) {
            const auto& l = c.port.keys[std::size_t(b)];
            if (std::find(l.begin(), l.end(), in.key) != l.end()) o.push_back({0, Ds2::Cross, true, PortAction(b)});
        } else {
            const auto& l = c.port.pad[std::size_t(b)];
            if (l.size() == 1 && l[0] == in.pad) o.push_back({0, Ds2::Cross, true, PortAction(b)});
        }
    }
    return o;
}

void rebind(Controls& c, int player, Device d, Ds2 action, const PhysInput& in, KeyCtx view, Resolve r, bool add) {
    player &= 1;
    const std::vector<PhysInput> before = bound(c, player, d, action, view);
    std::optional<PhysInput> old_first;
    for (const PhysInput& b : before)
        if (!(b == in)) { old_first = b; break; }
    const std::vector<Use> others = uses(c, player, d, action, in, view);
    if (d == Device::Keyboard) {
        std::vector<KeyBind>& l = c.player[std::size_t(player)].keys[idx(action)];
        if (!add)
            for (const PhysInput& b : before) remove_from_view(l, b.key, view);
        const bool there = std::any_of(l.begin(), l.end(), [&](const KeyBind& k) { return k.scancode == in.key && (k.ctx == KeyCtx::Any || k.ctx == view); });
        if (!there) l.push_back({in.key, view});
        normalize(l);
        if (r == Resolve::Keep) return;
        for (const Use& u : others) {
            if (u.port) {   // rebind2: a port action's key
                auto& po = c.port.keys[std::size_t(u.port_action)];
                po.erase(std::remove(po.begin(), po.end(), in.key), po.end());
                if (r == Resolve::Swap && !add && old_first && std::find(po.begin(), po.end(), old_first->key) == po.end()) po.push_back(old_first->key);
                continue;
            }
            std::vector<KeyBind>& o = c.player[std::size_t(u.player)].keys[idx(u.action)];
            remove_from_view(o, in.key, view);
            if (r == Resolve::Swap && !add && old_first) {
                o.push_back({old_first->key, view});
                normalize(o);
            }
        }
    } else {   // padlayout: as the keyboard's, in the view; the shown layout becomes the player's own (Custom)
        const std::uint16_t pin = std::uint16_t(in.pad);
        for (const Use& u : others)
            if (u.port && r != Resolve::Keep) {
                auto& o = c.port.pad[std::size_t(u.port_action)];
                o.erase(std::remove(o.begin(), o.end(), in.pad), o.end());
                if (r == Resolve::Swap && !add && old_first && std::find(o.begin(), o.end(), old_first->pad) == o.end()) o.push_back(old_first->pad);
            }
        edit_pad(c, player, [&](PadBindings& b) {
            std::vector<PadBind>& l = b[idx(action)];
            if (!add)
                for (const PhysInput& x : before) remove_from_view(l, std::uint16_t(x.pad), view);
            const bool there = std::any_of(l.begin(), l.end(), [&](const PadBind& k) { return k.in == in.pad && (k.ctx == KeyCtx::Any || k.ctx == view); });
            if (!there) l.push_back({in.pad, view});
            normalize(l);
            if (r == Resolve::Keep) return;
            for (const Use& u : others) {
                if (u.port) continue;
                std::vector<PadBind>& o = b[idx(u.action)];
                remove_from_view(o, pin, view);
                if (r == Resolve::Swap && !add && old_first) {
                    o.push_back({old_first->pad, view});
                    normalize(o);
                }
            }
        });
    }
}

Removed remove_last(Controls& c, int player, Device d, Ds2 action, KeyCtx view) {
    player &= 1;
    const std::vector<PhysInput> shown = bound(c, player, d, action, view);
    if (shown.empty()) return Removed::kNothing;
    Controls after = c;
    if (d == Device::Keyboard) remove_from_view(after.player[std::size_t(player)].keys[idx(action)], shown.back().key, view);
    else edit_pad(after, player, [&](PadBindings& b) { remove_from_view(b[idx(action)], std::uint16_t(shown.back().pad), view); });
    if ((action == Ds2::Cross || action == Ds2::Triangle) && bound(after, player, d, action, KeyCtx::Menu).empty())
        return Removed::kKeptForMenus;   // settingsui: the menus' last confirm / back on this device stays
    c = std::move(after);
    return Removed::kDone;
}

void reset(Controls& c, int player, Device d) {
    player &= 1;
    if (d == Device::Keyboard) c.player[std::size_t(player)].keys = default_player(player).keys;
    else {
        c.player[std::size_t(player)].pad = default_player(player).pad;
        c.player[std::size_t(player)].layout = default_player(player).layout;   // padlayout: Auto
        c.player[std::size_t(player)].gyro = default_player(player).gyro;   // pad1
    }
}

// ---- the rebind flow ----

void RebindFlow::begin(int player, Device d, Ds2 action, KeyCtx view, bool add) {
    state_ = State::Waiting;
    player_ = player & 1;
    device_ = d;
    action_ = action;
    view_ = view;   // padlayout: a gamepad's view too
    add_ = add;
    port_ = false;
    waited_ = 0;
    choice_ = 0;
    pressed_ = {};
    refused_.reset();
    conflicts_.clear();
}

void RebindFlow::begin_port(PortAction action, Device d, bool add) {
    begin(0, d, Ds2::Cross, KeyCtx::Any, add);
    port_ = true;
    port_action_ = action;
}

RebindFlow::State RebindFlow::tick(float dt) {
    if (state_ == State::Waiting) {
        waited_ += std::max(0.0f, dt);
        if (waited_ >= kTimeout) state_ = State::Cancelled;
    }
    return state_;
}

RebindFlow::State RebindFlow::input(Controls& c, const PhysInput& in) {
    if (state_ != State::Waiting || in.device != device_) return state_;
    if (in.device == Device::Keyboard && key_reserved(in.key)) return state_;
    if (in.device == Device::Gamepad && (in.pad == PadInput::None || in.pad >= PadInput::Count)) return state_;
    if (port_ && port_page(port_action_) && page_reads_input(c, in, port_action_)) {   // todrace: the page would lose it: wait for another
        refused_ = in;
        waited_ = 0;
        return state_;
    }
    pressed_ = in;
    conflicts_ = port_ ? uses_port(c, port_action_, device_, in) : uses(c, player_, device_, action_, in, view_);
    swap_.reset();
    if (!add_)
        for (const PhysInput& b : port_ ? bound_port(c, port_action_, device_) : bound(c, player_, device_, action_, view_))
            if (!(b == in)) { swap_ = b; break; }
    if (conflicts_.empty()) {
        if (port_) rebind_port(c, port_action_, device_, in, Resolve::Keep, add_);
        else rebind(c, player_, device_, action_, in, view_, Resolve::Keep, add_);
        state_ = State::Done;
    } else {
        choice_ = 0;
        state_ = State::Conflict;
    }
    return state_;
}

void RebindFlow::cancel() {
    if (active()) state_ = State::Cancelled;
}

void RebindFlow::step(int dir) {
    if (state_ != State::Conflict || dir == 0) return;
    choice_ = (choice_ + (dir > 0 ? 1 : 2)) % 3;
}

RebindFlow::State RebindFlow::choose(Controls& c) {
    if (state_ != State::Conflict) return state_;
    const Resolve r = choice_ == 0 ? Resolve::Swap : choice_ == 1 ? Resolve::Clear : Resolve::Keep;
    if (port_) rebind_port(c, port_action_, device_, pressed_, r, add_);
    else rebind(c, player_, device_, action_, pressed_, view_, r, add_);
    state_ = State::Done;
    return state_;
}

std::string RebindFlow::message(PadStyle style) const {
    const std::string act = port_ ? port_label(port_action_) : action_label(action_);
    const std::string where = port_ ? "" : view_ == KeyCtx::Drive ? " (WHILE DRIVING)" : view_ == KeyCtx::Menu ? " (IN MENUS)" : "";
    const std::string who = port_ ? std::string() : "PLAYER " + std::to_string(player_ + 1) + " ";
    if (state_ == State::Waiting) {
        const int left = std::max(1, int(std::ceil(remaining())));
        const std::string refused = !refused_ ? std::string()
                                    : port_action_ == PortAction::PaintPanel   // paintsel
                                        ? input_label(*refused_, style) + " IS ONE SELECT CAR USES ITSELF (CONFIRM, BACK, COLOUR, THE D-PAD): CHOOSE ANOTHER.  "
                                        : input_label(*refused_, style) + " IS ONE THE TRACK PAGES USE THEMSELVES (CONFIRM, BACK, "
                                                                          "THE D-PAD): CHOOSE ANOTHER.  ";
        return refused + std::string(device_ == Device::Keyboard ? (add_ ? "PRESS ANOTHER KEY FOR " : "PRESS A KEY FOR ")
                                                       : (add_ ? "PRESS ANOTHER CONTROLLER INPUT FOR " : "PRESS A BUTTON, STICK OR TRIGGER FOR ")) +
               who + act + where + ".  ESCAPE CANCELS (" + std::to_string(left) + ")";
    }
    if (state_ == State::Conflict) {
        std::string list;
        for (const Use& u : conflicts_) {
            if (!list.empty()) list += ", ";
            if (u.port) list += port_label(u.port_action);
            else {
                if (port_ || u.player != player_) list += "PLAYER " + std::to_string(u.player + 1) + " ";
                list += action_label(u.action);
            }
        }
        const std::string in = input_label(pressed_, style);
        const char* const opt[3] = {"SWAP", "CLEAR", "KEEP BOTH"};
        std::string choices;
        for (int k = 0; k < 3; ++k) {
            if (!choices.empty()) choices += "   ";
            choices += k == choice_ ? std::string("> ") + opt[k] + " <" : std::string(opt[k]);
        }
        // Swap gives them the action's first input in the view before this rebind (none, or adding: they lose it)
        const std::string swap = swap_ ? "SWAP: THEY GET " + input_label(*swap_, style) : "SWAP: THEY LOSE " + in;
        return in + " ALSO DRIVES " + list + where + ".  " + swap + ".  CLEAR: THEY LOSE " + in + ".  KEEP BOTH: " + in +
               " DRIVES ALL OF THEM.      " + choices + "      (LEFT / RIGHT, CONFIRM; BACK CANCELS)";
    }
    return "";
}

std::array<Ds2, 16> pad_glyphs(const PadBindings& pb, KeyCtx now) {
    std::array<Ds2, 16> g{};
    for (int b = 0; b < 16; ++b) {
        g[std::size_t(b)] = Ds2(b);
        // padlayout: this context's bindings first, then both contexts' (Any: all, in their order)
        std::vector<PadInput> order;
        for (int pass = 0; pass < 2; ++pass)
            for (const PadBind& x : pb[std::size_t(b)])
                if (now == KeyCtx::Any ? pass == 0 : pass == 0 ? x.ctx == now : x.ctx == KeyCtx::Any) order.push_back(x.in);
        for (PadInput in : order) {
            std::optional<Ds2> at;
            switch (in) {
            case PadInput::South: at = Ds2::Cross; break;
            case PadInput::East: at = Ds2::Circle; break;
            case PadInput::West: at = Ds2::Square; break;
            case PadInput::North: at = Ds2::Triangle; break;
            case PadInput::LeftShoulder: at = Ds2::L1; break;
            case PadInput::RightShoulder: at = Ds2::R1; break;
            case PadInput::LeftTrigger: at = Ds2::L2; break;
            case PadInput::RightTrigger: at = Ds2::R2; break;
            case PadInput::LeftStick: at = Ds2::L3; break;
            case PadInput::RightStick: at = Ds2::R3; break;
            case PadInput::Start: at = Ds2::Start; break;
            case PadInput::Back: at = Ds2::Select; break;
            default: break;
            }
            if (at) { g[std::size_t(b)] = *at; break; }
        }
    }
    return g;
}

}  // namespace b3native::input
