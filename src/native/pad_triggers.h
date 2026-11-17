// pad_triggers.h -- DualSense adaptive triggers (pad1, 2026-11-13; remaster-features §5 "Modern controller features",
// docs/pc-settings.md §7.12). Pure (no SDL, no guest memory): src/b3replay/nativepad.cpp sends the blocks this makes
// with SDL_SendGamepadEffect to PS5 pads, the launcher's Controllers page its trigger test, tests/test_pad_motion.cpp
// checks the bytes.
//
// PRESENTATION ONLY: the effects are made from what the renderer already reads of the game (the latest tick of the
// live feed, the same capture the HUD and the post effects are drawn from: whether a race is on screen, the view's boost
// radial blur amount, its race car's crashed flag +0x18EA) and the window's own pad context (driving / menus / paused,
// b3r_pad_context). Nothing is written to the game and nothing the game reads changes: a trigger's resistance changes
// how hard the player's finger pushes, not the report (the trigger's travel is read as before).
//
// The effect block (SDL_SendGamepadEffect's data for a PS5 controller) is SDL's DS5EffectsState_t, the output report's
// effect section that SDL's HIDAPI PS5 driver merges into its own state (src/joystick/hidapi/SDL_hidapi_ps5.c in SDL 3;
// SDL's test/testcontroller.c keeps a copy of the struct and cycles three trigger effects with it). The SDL we ship
// (ext/sdl3-3.4.2) has the public headers only, which say no more than "a gamepad specific effect packet"; the layout
// below is that struct as SDL 3's sources define it:
//   +0   ucEnableBits1   0x04: the right trigger's effect is set, 0x08: the left's (other bits: rumble, see SDL)
//   +1   ucEnableBits2
//   +2   ucRumbleRight, +3 ucRumbleLeft, +4 headphone, +5 speaker, +6 microphone volumes, +7 audio enable bits,
//   +8   mic light, +9 audio mute bits
//   +10  rgucRightTriggerEffect[11]
//   +21  rgucLeftTriggerEffect[11]
//   +32  6 unknown, +38 ucEnableBits3, +39 2 unknown, +41 LED animation, +42 LED brightness, +43 pad lights,
//   +44  LED red, green, blue (+46)                                                     47 bytes in all
// Only the enable bits for the triggers and the two trigger blocks are set here (the rest zero: SDL leaves what a zero
// enable bit names as it is). A trigger block is a mode byte and its parameters; the modes used are the controller's
// simple ones, the same as testcontroller's:
//   0x05  off: no effect (the trigger moves freely)                     testcontroller "Clear trigger effect"
//   0x01  resistance: [1] start position 0-255 of the pull, [2] force 0-255   ("Constant resistance": 0x01, 0, 110)
//   0x06  vibration: [1] frequency (Hz), [2] amplitude, [3] start position    ("Resistance and vibration": 0x06, 15, 63, 128)
// The values chosen (start positions, forces, frequencies) are first guesses to be tuned with a pad.
#ifndef B3NATIVE_PAD_TRIGGERS_H
#define B3NATIVE_PAD_TRIGGERS_H

#include <algorithm>
#include <array>
#include <cstdint>

namespace b3native::pad {

constexpr int kDs5EffectSize = 47;            // sizeof(DS5EffectsState_t)
constexpr int kDs5EnableBits1 = 0;
constexpr std::uint8_t kDs5RightTriggerBit = 0x04, kDs5LeftTriggerBit = 0x08;
constexpr int kDs5RightTrigger = 10, kDs5LeftTrigger = 21, kDs5TriggerBytes = 11;
constexpr std::uint8_t kModeOff = 0x05, kModeResist = 0x01, kModeVibrate = 0x06;

using TriggerEffect = std::array<std::uint8_t, kDs5TriggerBytes>;
using Ds5Block = std::array<std::uint8_t, kDs5EffectSize>;

inline TriggerEffect trigger_off() { return TriggerEffect{kModeOff}; }
inline TriggerEffect trigger_resist(std::uint8_t start, std::uint8_t force) { return TriggerEffect{kModeResist, start, force}; }
inline TriggerEffect trigger_vibrate(std::uint8_t hz, std::uint8_t amplitude, std::uint8_t start) {
    return TriggerEffect{kModeVibrate, hz, amplitude, start};
}

// The effect block that sets both triggers
inline Ds5Block ds5_trigger_block(const TriggerEffect& left, const TriggerEffect& right) {
    Ds5Block b{};
    b[kDs5EnableBits1] = kDs5RightTriggerBit | kDs5LeftTriggerBit;
    std::copy(right.begin(), right.end(), b.begin() + kDs5RightTrigger);
    std::copy(left.begin(), left.end(), b.begin() + kDs5LeftTrigger);
    return b;
}
// Both triggers back to free (the setting off, a menu, pause, the window closing, a pad leaving the game)
inline Ds5Block ds5_triggers_off() { return ds5_trigger_block(trigger_off(), trigger_off()); }

// What the game is doing for one player, as the renderer reads it (above)
struct TriggerGame {
    bool active = false;   // a race on screen, the player driving (not paused, no menu, no PC page / photo mode)
    float boost = 0;       // the view's boost radial blur amount, 0..1 (lerp(0, 1, boost) [capture.h RadialBlurIn])
    bool crashed = false;  // the view's race car +0x18EA (crashed: the game skips the boost blur then)
    bool operator==(const TriggerGame&) const = default;
};
// What a physical trigger drives, from the player's bindings: the brake (Square) or the accelerator (Cross); else none
enum class TriggerRole : std::uint8_t { None, Brake, Accel };

struct TriggerPair {
    TriggerEffect left = trigger_off(), right = trigger_off();
    bool operator==(const TriggerPair&) const = default;
};

// The tuning (first guesses; strength scales the forces and amplitudes, percent)
constexpr std::uint8_t kBrakeStart = 40, kBrakeForce = 180;     // the brake: firm from ~15 % of the pull
constexpr std::uint8_t kAccelForce = 50;                        // the accelerator: a light spring
constexpr std::uint8_t kBoostHz = 30, kBoostAmpMin = 60, kBoostAmpMax = 200;   // boosting: the accelerator buzzes
constexpr std::uint8_t kKickHz = 12, kKickAmp = 255;            // a crash: both triggers kick
constexpr std::uint64_t kKickMs = 300;
constexpr float kBoostOn = 0.05f;

inline std::uint8_t scaled(std::uint8_t v, int strength_percent) {
    const int pct = std::clamp(strength_percent, 0, 100);
    if (v == 0 || pct == 0) return 0;
    return static_cast<std::uint8_t>(std::max(1, int(v) * pct / 100));
}

// One player's effects over time (window thread): step() with the game's state and the time (ms, any monotonic clock)
// gives both triggers' effects. A crash (the crashed flag rising while active) kicks both triggers for kKickMs, then
// they are free until the car is back; while not active everything is off and no kick is pending.
class TriggerFx {
public:
    TriggerPair step(const TriggerGame& g, TriggerRole left, TriggerRole right, int strength_percent, std::uint64_t now_ms) {
        TriggerPair out;
        const bool crash_edge = g.crashed && !was_crashed_;
        was_crashed_ = g.crashed;
        if (!g.active) {
            kick_until_ = 0;
            return out;
        }
        if (crash_edge) kick_until_ = now_ms + kKickMs;
        const auto one = [&](TriggerRole r) {
            if (r == TriggerRole::None) return trigger_off();
            if (now_ms < kick_until_) return trigger_vibrate(kKickHz, scaled(kKickAmp, strength_percent), 0);
            if (g.crashed) return trigger_off();
            if (r == TriggerRole::Brake) return trigger_resist(kBrakeStart, scaled(kBrakeForce, strength_percent));
            if (g.boost > kBoostOn) {
                const float b = std::clamp(g.boost, 0.0f, 1.0f);
                const int amp = kBoostAmpMin + int(float(kBoostAmpMax - kBoostAmpMin) * b + 0.5f);
                return trigger_vibrate(kBoostHz, scaled(std::uint8_t(amp), strength_percent), 0);
            }
            return trigger_resist(0, scaled(kAccelForce, strength_percent));
        };
        out.left = one(left);
        out.right = one(right);
        return out;
    }
    bool kicking(std::uint64_t now_ms) const { return now_ms < kick_until_; }

private:
    bool was_crashed_ = false;
    std::uint64_t kick_until_ = 0;
};

}  // namespace b3native::pad

#endif
