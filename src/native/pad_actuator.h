// pad_actuator.h -- the pads' rumble motors (rumble1, 2026-11-12; remaster-features §3 "Modern controller features"):
// what the game asks of a DualShock 2's actuators, how the device manager answers it, and what a PC pad is given.
// Pure (no guest memory, no SDL): platform_dbc.cpp (sceDbcSendData2), src/b3replay/nativepad.cpp and the unit tests
// (tests/test_pad_actuator.cpp) share it.
//
// What the game sends [C decomp, assembly/asm]:
//   - CB3InputManager's per-frame update (func_00213F00) sets each player's motors on its CGtInputDevicePS2DualShock:
//     the large motor (func_00217200) to (u8)(255 * clamp(3 * x, 0, 0.9999)) -- 0..254, a value of 1.0 or more is
//     ignored -- and the small motor (func_002171D0) on while clamp(y, 0, 0.9999) != 0, only while the player's
//     vibration is allowed (func_0022C340: the profile's VIBRATION option for that pad, among others).
//   - CGtInputDevicePS2DualShock::UpdateVibration (0x217070) sends them only when they change (and zeroes when paused:
//     SetPause, 0x217060): sceVibSetActParam(socket, 1, &profile, 2, &data) with profile 3 (actuators 0 and 1) and the
//     16-bit data (large << 1) + small, little-endian. It ignores the result, and records the values as sent either way.
//   - libvib's sceVibSetActParam (0x217520) packs a block for sceDbcSendData2 (descriptor 0x0103400B, size 0x28):
//     {u32 profile bytes, u32 data bytes, the data bytes, then the profile bytes}. The data is a bit stream, least
//     significant bit first, one field per actuator the profile names in order: actuator 0 (the small motor) 1 bit,
//     actuator 1 (the large motor) 8 bits.
//   - libdbc's sceDbcSendData2 (0x216A58) copies the request into its buffer and, unless the previous request's RPC to
//     the IOP's device manager is still in flight (sceSifCheckStatRpc 1: "busy", the request dropped, return 0), sends
//     it as a no-wait RPC (sceSifCallRpc mode 1, function 0x8000131B; return 1).
// The acknowledgement: the device manager's RPC reply. Until it comes the channel is busy; once it has come the next
// request goes through. There is nothing else (no completion callback, no semaphore).
#ifndef B3NATIVE_PAD_ACTUATOR_H
#define B3NATIVE_PAD_ACTUATOR_H

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace b3native::pad {

// A DualShock 2's motors: the small one on / off, the large one's speed 0-255.
struct Motors {
    std::uint8_t small = 0;
    std::uint8_t large = 0;
    bool operator==(const Motors&) const = default;
    bool any() const { return small != 0 || large != 0; }
};

// libvib's request block (sceDbcSendData2's data, `size` bytes) -> the motors it sets. The actuators the profile does
// not name keep their value from `out`. False when the block is not one libvib makes for a DualShock 2 (a profile
// byte naming other actuators, too few data bits, sizes past the block).
inline bool decode_actuator_request(const std::uint8_t* block, std::uint32_t size, Motors& out) {
    if (!block || size < 8) return false;
    auto u32 = [block](unsigned at) {
        std::uint32_t v;
        std::memcpy(&v, block + at, 4);
        return v;   // (little-endian: the EE's order and every host's the port builds for)
    };
    const std::uint32_t profile_bytes = u32(0), data_bytes = u32(4);
    if (profile_bytes == 0 || profile_bytes > 32 || data_bytes > 32 || 8u + data_bytes + profile_bytes > size) return false;
    const std::uint8_t* data = block + 8;
    const std::uint8_t* profile = data + data_bytes;
    if ((profile[0] & ~3u) != 0) return false;
    for (std::uint32_t k = 1; k < profile_bytes; ++k)
        if (profile[k] != 0) return false;
    unsigned bit = 0;
    auto take = [&](unsigned n) {
        std::uint32_t v = 0;
        for (unsigned i = 0; i < n; ++i, ++bit) v |= std::uint32_t((data[bit / 8] >> (bit % 8)) & 1u) << i;
        return v;
    };
    const unsigned need = ((profile[0] & 1u) ? 1u : 0u) + ((profile[0] & 2u) ? 8u : 0u);
    if (need > data_bytes * 8u) return false;
    Motors m = out;
    if (profile[0] & 1u) m.small = static_cast<std::uint8_t>(take(1));
    if (profile[0] & 2u) m.large = static_cast<std::uint8_t>(take(8));
    out = m;
    return true;
}

// The block sceVibSetActParam makes for the game's call (tests): profile 3, data (large << 1) + small.
inline void encode_game_request(Motors m, std::uint8_t block[0x28]) {
    std::memset(block, 0, 0x28);
    const std::uint32_t profile_bytes = 1, data_bytes = 2;
    std::memcpy(block, &profile_bytes, 4);
    std::memcpy(block + 4, &data_bytes, 4);
    const std::uint16_t d = static_cast<std::uint16_t>((unsigned(m.large) << 1) + (m.small ? 1u : 0u));
    block[8] = static_cast<std::uint8_t>(d & 0xFF);
    block[9] = static_cast<std::uint8_t>(d >> 8);
    block[10] = 3;
}

// libdbc's one RPC client for actuator requests, as sceDbcSendData2 sees it (sceSifCheckStatRpc).
//   Pending mode (the default, the port before rumble1): no device manager answers, so the first request taken stays
//   in flight for good and every later one finds the channel busy.
//   Acknowledging mode: the device manager answers each request `ack_cycles` virtual EE cycles after it was sent.
// Switching modes leaves a request in flight as it is, except that a request left pending is answered when
// acknowledging begins (the device manager takes it up then).
class ActuatorChannel {
public:
    static constexpr std::uint64_t kNever = ~0ull;
    void set_ack(bool on, std::uint64_t now, std::uint64_t ack_cycles) {
        if (on && !ack_ && done_ == kNever && sent_) done_ = now + ack_cycles;
        ack_ = on;
        ack_cycles_ = ack_cycles;
    }
    bool ack() const { return ack_; }
    bool busy(std::uint64_t now) const { return sent_ && now < done_; }
    // A request taken (the caller checked busy() first)
    void send(std::uint64_t now) {
        sent_ = true;
        done_ = ack_ ? now + ack_cycles_ : kNever;
    }
    bool sent() const { return sent_; }

private:
    bool ack_ = false, sent_ = false;
    std::uint64_t ack_cycles_ = 0, done_ = kNever;
};

// What a PC pad is given (SDL_RumbleGamepad's low / high frequency motors, SDL_RumbleGamepadTriggers' two triggers,
// 0-0xFFFF): the large motor (the DualShock 2's heavy, low frequency one) as the low frequency motor, its speed scaled
// to the full range; the small motor (on / off, a light fast buzz) as the high frequency motor at full; both scaled by
// the strength (percent). With `triggers` the triggers' own motors (Xbox One / Series impulse triggers) follow the
// large motor at half its strength, the right one with the small motor's buzz added.
struct RumbleOut {
    std::uint16_t low = 0, high = 0, left_trigger = 0, right_trigger = 0;
    bool operator==(const RumbleOut&) const = default;
    bool any() const { return low || high || left_trigger || right_trigger; }
};
inline RumbleOut rumble_out(Motors m, int strength_percent, bool triggers) {
    const std::uint32_t pct = static_cast<std::uint32_t>(std::clamp(strength_percent, 0, 100));
    auto scale = [pct](std::uint32_t v) { return static_cast<std::uint16_t>(v * pct / 100u); };
    RumbleOut r;
    r.low = scale(std::uint32_t(m.large) * 257u);
    r.high = scale(m.small ? 0xFFFFu : 0u);
    if (triggers) {
        r.left_trigger = scale(std::uint32_t(m.large) * 257u / 2u);
        r.right_trigger = scale(std::min<std::uint32_t>(std::uint32_t(m.large) * 257u / 2u + (m.small ? 0x4000u : 0u), 0xFFFFu));
    }
    return r;
}

}  // namespace b3native::pad

#endif
