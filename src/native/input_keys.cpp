// Keyboard -> DS2 report (src/native/input_keys.h).
#include "input_keys.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace b3native::input {

namespace {
float approach(float v, float target, float rate_up, float rate_down, float dt) {
    if (v == target) return v;
    if (target == 0 || (v != 0 && (v > 0) != (target > 0))) {   // back toward 0 first (release, or a reversal)
        const float r = rate_down * dt;
        return std::fabs(v) <= r ? 0.0f : v - std::copysign(r, v);   // a reversal continues from 0 next step
    }
    const float r = rate_up * dt;
    return std::fabs(target - v) <= r ? target : v + std::copysign(r, target - v);
}
}  // namespace

const char* ds2_name(Ds2 c) {
    static const char* const n[] = {"select", "l3", "r3", "start", "up", "right", "down", "left", "l2", "r2", "l1",
                                    "r1", "triangle", "circle", "cross", "square", "steer_left", "steer_right",
                                    "stick_up", "stick_down", "rstick_left", "rstick_right", "rstick_up", "rstick_down"};
    const auto i = static_cast<std::size_t>(c);
    return i < sizeof n / sizeof n[0] ? n[i] : "?";
}

const std::vector<KeyBinding>& default_key_map() {
    // docs/mac.md §5 (mac4, the default layout since 2026-11-08): WASD / arrows drive (W / Up accelerate, S / Down brake, A / D
    // and Left / Right steer) and navigate menus; Space boost (menus: confirm); Enter confirm; Escape start / pause;
    // Backspace back. The rest by the game's controls (input_keys.h); Z / X kept from the earlier keyboard layout.
    using C = KeyCtx;
    static const std::vector<KeyBinding> m = {
        // steering and the D-pad left / right, everywhere
        {sc::Left, Ds2::Left},             {sc::Left, Ds2::SteerLeft},          {sc::A, Ds2::Left},
        {sc::A, Ds2::SteerLeft},           {sc::Right, Ds2::Right},             {sc::Right, Ds2::SteerRight},
        {sc::D, Ds2::Right},               {sc::D, Ds2::SteerRight},
        // up / down: menus move, driving accelerates / brakes (the stick's Y too: Aftertouch while crashing)
        {sc::Up, Ds2::Up, C::Menu},        {sc::W, Ds2::Up, C::Menu},           {sc::Down, Ds2::Down, C::Menu},
        {sc::S, Ds2::Down, C::Menu},       {sc::Up, Ds2::Cross, C::Drive},      {sc::W, Ds2::Cross, C::Drive},
        {sc::Up, Ds2::StickUp, C::Drive},  {sc::W, Ds2::StickUp, C::Drive},     {sc::Down, Ds2::Square, C::Drive},
        {sc::S, Ds2::Square, C::Drive},    {sc::Down, Ds2::StickDown, C::Drive}, {sc::S, Ds2::StickDown, C::Drive},
        // Space: boost (R1; held while crashing: Impact Time + Aftertouch) / confirm in menus
        {sc::Space, Ds2::R1, C::Drive},    {sc::Space, Ds2::Cross, C::Menu},
        // everywhere
        {sc::Return, Ds2::Cross},          {sc::KpEnter, Ds2::Cross},           {sc::Escape, Ds2::Start},
        {sc::Backspace, Ds2::Triangle},    {sc::Tab, Ds2::Select},              {sc::Z, Ds2::Cross},
        {sc::X, Ds2::Circle},              {sc::H, Ds2::Circle},                {sc::C, Ds2::Triangle},
        {sc::Q, Ds2::L1},                  {sc::E, Ds2::R2},                    {sc::LShift, Ds2::R1},
        {sc::R, Ds2::R1},                  {sc::N1, Ds2::L2},                   {sc::N3, Ds2::R2},
        // fe1 (2026-11-10): Square in menus (the car select's Color, MULTIPLAYER's Change Names: the game's Square
        // prompts); Z X C V = Cross Circle Triangle Square. (Driving: S / Down, the brake.)
        {sc::V, Ds2::Square, C::Menu},
    };
    return m;
}

KeyPad::KeyPad(std::vector<KeyBinding> map, RampTimes ramp) : map_(std::move(map)), ramp_(ramp) {}

void KeyPad::key(std::uint16_t scancode, bool down) {
    if (scancode < down_.size()) down_[scancode] = down;
}
void KeyPad::release_all() {
    down_.fill(false);
    steer_ = 0;
    dpad_[0] = dpad_[1] = 0;
}
bool KeyPad::held(Ds2 c) const {
    const KeyCtx now = driving_ ? KeyCtx::Drive : KeyCtx::Menu;
    for (const KeyBinding& b : map_)
        if (b.control == c && (b.ctx == KeyCtx::Any || b.ctx == now) && b.scancode < down_.size() && down_[b.scancode]) return true;
    return false;
}

void KeyPad::step(float dt) {
    if (!(dt > 0)) return;
    const float up = ramp_.ramp_up > 0 ? 1.0f / ramp_.ramp_up : 1e9f, dn = ramp_.ramp_down > 0 ? 1.0f / ramp_.ramp_down : 1e9f;
    const bool l = held(Ds2::SteerLeft), r = held(Ds2::SteerRight);
    const float target = l == r ? 0.0f : (l ? -1.0f : 1.0f);   // both held: centre
    steer_ = std::clamp(approach(steer_, target, up, dn, dt), -1.0f, 1.0f);
    // the D-pad left / right pressures (GetSteering reads them as analog values): the same ramp, one per direction
    dpad_[0] = std::clamp(approach(dpad_[0], held(Ds2::Left) ? 1.0f : 0.0f, up, dn, dt), 0.0f, 1.0f);
    dpad_[1] = std::clamp(approach(dpad_[1], held(Ds2::Right) ? 1.0f : 0.0f, up, dn, dt), 0.0f, 1.0f);
}

std::array<std::uint8_t, 32> neutral_sample() {
    // the neutral DualShock 2 report (platform_dbc.cpp kNeutral)
    std::array<std::uint8_t, 32> s{};
    s[0] = s[1] = 0xFF;
    s[2] = 0x80; s[3] = 0x7F; s[4] = 0x80; s[5] = 0x7F;
    s[18] = s[19] = s[20] = s[21] = 0x80;
    return s;
}

std::array<std::uint8_t, 32> KeyPad::sample() const {
    std::array<std::uint8_t, 32> s = neutral_sample();
    for (int b = 0; b < 16; ++b) {
        const Ds2 c = static_cast<Ds2>(b);
        if (!held(c)) continue;
        s[b >> 3] = static_cast<std::uint8_t>(s[b >> 3] & ~(1u << (b & 7)));   // digital: at once
        const int p = pressure_byte(c);
        if (p >= 0) s[p] = 0xFF;
    }
    // ramped D-pad left / right pressures (the digital bit stays immediate, for menus)
    s[7] = static_cast<std::uint8_t>(std::lround(dpad_[0] * 255.0f));
    s[6] = static_cast<std::uint8_t>(std::lround(dpad_[1] * 255.0f));
    if (steer_ != 0) s[4] = stick_x_byte(steer_);
    const bool u = held(Ds2::StickUp), d = held(Ds2::StickDown);
    if (u != d) s[5] = stick_y_byte(u ? -1.0f : 1.0f);   // SDL's y: -1 up
    // remap1: the right stick's half axes (full at once; the default map binds none, so these bytes stay neutral)
    const bool rl = held(Ds2::RStickLeft), rr = held(Ds2::RStickRight), ru = held(Ds2::RStickUp), rd = held(Ds2::RStickDown);
    if (rl != rr) s[2] = stick_x_byte(rl ? -1.0f : 1.0f);
    if (ru != rd) s[3] = stick_y_byte(ru ? -1.0f : 1.0f);
    return s;
}

std::array<std::uint8_t, 32> KeyPad::compose(const std::uint8_t* pad) const {
    const std::array<std::uint8_t, 32> k = sample();
    return pad ? merge_samples(k.data(), pad) : k;
}

void BoostLatch::apply(std::array<std::uint8_t, 32>& r, bool toggle, bool driving) {
    auto down = [&r](Ds2 c) { const int b = static_cast<int>(c); return (r[std::size_t(b >> 3)] & (1u << (b & 7))) == 0; };
    const bool held = down(Ds2::R1);
    if (!toggle || !driving || down(Ds2::Start)) {   // Hold, out of the race context, or pausing: the report as it is
        latched_ = false;
        prev_ = held;
        return;
    }
    if (held && !prev_) latched_ = !latched_;
    prev_ = held;
    const int b = static_cast<int>(Ds2::R1), p = pressure_byte(Ds2::R1);
    if (latched_) {
        r[std::size_t(b >> 3)] = static_cast<std::uint8_t>(r[std::size_t(b >> 3)] & ~(1u << (b & 7)));
        r[std::size_t(p)] = 0xFF;
    } else {
        r[std::size_t(b >> 3)] = static_cast<std::uint8_t>(r[std::size_t(b >> 3)] | (1u << (b & 7)));
        r[std::size_t(p)] = 0;
    }
}

bool ShoulderSeries::apply(std::array<std::uint8_t, 32>& r, bool on, bool page) {
    auto down = [&r](Ds2 c) { const int b = static_cast<int>(c); return (r[std::size_t(b >> 3)] & (1u << (b & 7))) == 0; };
    auto set = [&r](Ds2 c, bool pressed) {
        const int b = static_cast<int>(c), p = pressure_byte(c);
        if (pressed) {
            r[std::size_t(b >> 3)] = static_cast<std::uint8_t>(r[std::size_t(b >> 3)] & ~(1u << (b & 7)));
            r[std::size_t(p)] = 0xFF;
        } else {
            r[std::size_t(b >> 3)] = static_cast<std::uint8_t>(r[std::size_t(b >> 3)] | (1u << (b & 7)));
            r[std::size_t(p)] = 0;
        }
    };
    bool changed = false;
    auto side = [&](Ds2 shoulder, Ds2 dpad, bool& active, bool& prev) {
        const bool held = down(shoulder);
        if (!held || !on || !page) active = false;
        else if (!prev) {   // a press that begins on the page
            active = true;
            ++presses_;
        }
        prev = held;
        if (!active) return;
        set(shoulder, false);
        set(dpad, true);
        changed = true;
    };
    side(Ds2::L1, Ds2::Left, left_, prev_l_);
    side(Ds2::R1, Ds2::Right, right_, prev_r_);
    return changed;
}

const std::vector<RestartMacro::Step>& RestartMacro::steps() {
    // the pause menu opens on its first row (RESUME); RESTART is the next; its dialog (string 0x136) opens on NO, YES
    // is above it [timings: speedrun1's runs, docs/pc-settings.md §7.3]
    static const std::vector<Step> s = {{0, Ds2::Start}, {60, Ds2::Down}, {90, Ds2::Cross}, {140, Ds2::Up}, {170, Ds2::Cross}};
    return s;
}

int RestartMacro::length() { return steps().back().at + kHold + 24; }

bool RestartMacro::trigger() {
    if (t_ >= 0) return false;
    t_ = 0;
    return true;
}

bool RestartMacro::apply(std::array<std::uint8_t, 32>& r) {
    if (t_ < 0) return false;
    r = neutral_sample();
    for (const Step& s : steps())
        if (t_ >= s.at && t_ < s.at + kHold) {
            const int b = static_cast<int>(s.button);
            r[std::size_t(b >> 3)] = static_cast<std::uint8_t>(r[std::size_t(b >> 3)] & ~(1u << (b & 7)));
            if (const int p = pressure_byte(s.button); p >= 0) r[std::size_t(p)] = 0xFF;
        }
    if (++t_ >= length()) t_ = -1;
    return true;
}

std::array<std::uint8_t, 32> merge_samples(const std::uint8_t* k, const std::uint8_t* pad) {
    std::array<std::uint8_t, 32> out{};
    out[0] = static_cast<std::uint8_t>(k[0] & pad[0]);   // active low: pressed on either
    out[1] = static_cast<std::uint8_t>(k[1] & pad[1]);
    const std::array<std::uint8_t, 32> n = neutral_sample();
    for (int i = 2; i < 6; ++i)   // the axis further from its centre
        out[i] = std::abs(int(pad[i]) - int(n[i])) > std::abs(int(k[i]) - int(n[i])) ? pad[i] : k[i];
    for (int i = 6; i < 18; ++i) out[i] = std::max(k[i], pad[i]);
    for (int i = 18; i < 32; ++i) out[i] = pad[i];
    return out;
}

void PadSeats::added(std::uint32_t id) {
    if (id == 0 || role(id) != Role::None) return;
    for (std::uint32_t& s : seats_)
        if (s == 0) { s = id; return; }
    waiting_.push_back(id);
}

void PadSeats::removed(std::uint32_t id) {
    if (id == 0) return;
    for (std::uint32_t& s : seats_)
        if (s == id) {
            s = 0;
            if (!waiting_.empty()) { s = waiting_.front(); waiting_.erase(waiting_.begin()); }
            return;
        }
    waiting_.erase(std::remove(waiting_.begin(), waiting_.end(), id), waiting_.end());
}

PadSeats::Role PadSeats::role(std::uint32_t id) const {
    if (id == 0) return Role::None;
    if (seats_[0] == id) return Role::Player1;
    if (seats_[1] == id) return Role::Player2;
    return std::find(waiting_.begin(), waiting_.end(), id) != waiting_.end() ? Role::Waiting : Role::None;
}

}  // namespace b3native::input
