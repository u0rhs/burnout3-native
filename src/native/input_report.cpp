// src/native/input_report.cpp -- the DualShock 2 report encodings (docs/specs/input.md): the pressure byte of each
// button, the stick bytes, and a gamepad's report with the default bindings (docs/specs/input.md).
//
// Report layout (input.md §1, the public libpad padButtonStatus): bytes 0-1 the 16 buttons active low, 2-5 the sticks
// (right X, right Y, left X, left Y), 6-17 the twelve pressures, 18-21 0x80.
#include "input_keys.h"

#include <algorithm>
#include <cmath>

namespace b3native::input {

namespace {
// The pressure byte (6..17) of each libpad2 button index (input.md §1: right, left, up, down, triangle, circle, cross,
// square, L1, R1, L2, R2); -1: no pressure (select, L3, R3, start).
constexpr signed char kPressureByte[16] = {
    -1,   // Select
    -1,   // L3
    -1,   // R3
    -1,   // Start
    8,    // Up
    6,    // Right
    9,    // Down
    7,    // Left
    16,   // L2
    17,   // R2
    14,   // L1
    15,   // R1
    10,   // Triangle
    11,   // Circle
    12,   // Cross
    13,   // Square
};

// input.md §2: -1..1 to SDL's axis value -32768..32767 (the negative half scaled by 32768, the positive by 32767),
// rounded to the nearest, halves away from zero. The product is formed in single precision (float x float, then that
// float rounded), as the baseline does: near a half this can differ from rounding the exact product.
int to_axis(float v) {
    if (!(v == v)) return 0;   // NaN: centre (outside the spec's domain)
    const float c = std::clamp(v, -1.0f, 1.0f);
    return static_cast<int>(std::lround(c < 0 ? c * 32768.0f : c * 32767.0f));
}

// input.md §2: the axis value to the report bytes, in integers (w in -32768..32767).
std::uint8_t x_byte(int w) {
    if (w >= 0) return static_cast<std::uint8_t>(128 + (127 * w + 16383) / 32767);   // nearest; no exact halves
    return static_cast<std::uint8_t>(128 - ((-w + 128) >> 8));                       // |w| / 256, halves up
}
std::uint8_t y_byte(int w) {
    if (w >= 0) return static_cast<std::uint8_t>(127 + (128 * w + 16383) / 32767);   // nearest; no exact halves
    return static_cast<std::uint8_t>(127 - ((127 * -w + 16384) >> 15));              // 127 |w| / 32768, halves up
}

// input.md §3: one stick axis past the dead zone z, rescaled so the rest of the travel spans 0..1.
float stick_axis(std::int16_t raw, float z) {
    const float n = raw < 0 ? static_cast<float>(raw) / 32768.0f : static_cast<float>(raw) / 32767.0f;
    const float pos = std::clamp((n - z) / (1.0f - z), 0.0f, 1.0f);
    const float neg = std::clamp((-n - z) / (1.0f - z), 0.0f, 1.0f);
    return pos - neg;
}

// input.md §3: a trigger's travel 0..1 (negative readings count as released).
float trigger_amount(std::int16_t raw) {
    return std::clamp(static_cast<float>(std::max<int>(raw, 0)) / 32767.0f, 0.0f, 1.0f);
}

void press(std::array<std::uint8_t, 32>& s, Ds2 c) {
    const int b = static_cast<int>(c);
    s[std::size_t(b >> 3)] = static_cast<std::uint8_t>(s[std::size_t(b >> 3)] & ~(1u << (b & 7)));
}
}  // namespace

int pressure_byte(Ds2 c) {
    const auto i = static_cast<std::size_t>(c);
    return i < 16 ? kPressureByte[i] : -1;
}

std::uint8_t stick_x_byte(float v) { return x_byte(to_axis(v)); }

std::uint8_t stick_y_byte(float v) { return y_byte(to_axis(v)); }

float pedal_stick(float travel) {
    if (!(travel > 0.0f)) return 0.0f;
    return std::clamp(0.1f + 0.8f * std::pow(std::min(travel, 1.0f), 0.25f), 0.0f, 1.0f);
}

std::array<std::uint8_t, 32> gamepad_sample(const GamepadState& g, const GamepadTuning& t) {
    std::array<std::uint8_t, 32> s = neutral_sample();
    const float z = std::clamp(t.stick_dead_zone, 0.0f, 0.95f);
    const float theta = std::clamp(t.trigger_threshold, 0.01f, 1.0f);

    // plain buttons: the digital bit and, where the button has one, full pressure while held
    const std::pair<bool, Ds2> buttons[] = {
        {g.east, Ds2::Circle},       {g.north, Ds2::Triangle},     {g.left_shoulder, Ds2::L1},
        {g.right_shoulder, Ds2::R1}, {g.start, Ds2::Start},        {g.back, Ds2::Select},
        {g.dpad_up, Ds2::Up},        {g.dpad_down, Ds2::Down},     {g.dpad_left, Ds2::Left},
        {g.dpad_right, Ds2::Right},
        // padr2: the stick clicks are L2 / R2 (music track skip, Crashbreaker): full pressure while held, as the
        // bindings path gives any digital input on a button with a pressure byte (docs/specs/input.md §3)
        {g.left_stick, Ds2::L2},     {g.right_stick, Ds2::R2},
    };
    for (const auto& [held, c] : buttons) {
        if (!held) continue;
        press(s, c);
        const int p = pressure_byte(c);
        if (p >= 0) s[std::size_t(p)] = 0xFF;
    }

    // Cross / Square: the face button, and the trigger as a pedal. 1.0.1 ANALOG PEDALS: a trigger alone drives the right
    // stick (pedal_stick: the game's analog throttle / brake); both triggers past the threshold press both buttons (a
    // powerslide). Off: the trigger's pressure on the button (pressed from the threshold), as 1.0.0.
    const float acc = trigger_amount(g.right_trigger), brk = trigger_amount(g.left_trigger);
    const bool both = acc >= theta && brk >= theta;
    float stick_y = 0;   // the report's y: down positive
    const auto pedal = [&](bool button, float tr, Ds2 c, float dir) {
        if (t.analog_pedals && !both) {
            if (button) { press(s, c); s[std::size_t(pressure_byte(c))] = 0xFF; }
            stick_y += dir * pedal_stick(tr);
            return;
        }
        const float amount = std::max(button ? 1.0f : 0.0f, tr);
        s[std::size_t(pressure_byte(c))] = static_cast<std::uint8_t>(std::lround(255.0f * amount));
        if (button || tr >= theta) press(s, c);
    };
    pedal(g.south, acc, Ds2::Cross, -1.0f);
    pedal(g.west, brk, Ds2::Square, 1.0f);
    if (stick_y != 0.0f) s[3] = stick_y_byte(std::clamp(stick_y, -1.0f, 1.0f));

    // the left stick (SDL's y: down positive, as the report's)
    s[4] = stick_x_byte(stick_axis(g.left_x, z));
    s[5] = stick_y_byte(stick_axis(g.left_y, z));
    return s;
}

}  // namespace b3native::input
