// rumble1: the pads' actuator requests (src/native/pad_actuator.h) -- libvib's block decoded, the device manager's
// answer (the channel's busy / done states in both modes), the PC pad's rumble values, and the pad recording's rumble
// switches (tools/native/pad_record.h).
#include "test.h"

#include "../src/native/pad_actuator.h"
#include "../tools/native/pad_record.h"

#include <cstdio>
#include <filesystem>
#include <string>

using b3native::pad::ActuatorChannel;
using b3native::pad::Motors;

TEST(decode_the_games_request) {
    // UpdateVibration's call: profile 3, data (large << 1) + small as a little-endian 16-bit value
    for (int large : {0, 1, 2, 127, 128, 200, 254, 255})
        for (int small : {0, 1}) {
            std::uint8_t b[0x28];
            b3native::pad::encode_game_request(Motors{std::uint8_t(small), std::uint8_t(large)}, b);
            Motors m{1, 99};
            CHECK(b3native::pad::decode_actuator_request(b, 0x28, m));
            CHECK(m.small == small && m.large == large);
        }
    // the bytes libvib makes for large 200, small on: {1, 2, 0x91, 0x01, 3}
    std::uint8_t b[0x28] = {1, 0, 0, 0, 2, 0, 0, 0, 0x91, 0x01, 3};
    Motors m;
    CHECK(b3native::pad::decode_actuator_request(b, 0x28, m) && m.small == 1 && m.large == 200);
}

TEST(decode_partial_profiles_and_bad_blocks) {
    // profile 1: the small motor only (1 bit); profile 2: the large one only (8 bits from bit 0); the other keeps its value
    std::uint8_t small_only[0x28] = {1, 0, 0, 0, 1, 0, 0, 0, 0x01, 1};
    Motors m{0, 77};
    CHECK(b3native::pad::decode_actuator_request(small_only, 0x28, m) && m.small == 1 && m.large == 77);
    std::uint8_t large_only[0x28] = {1, 0, 0, 0, 1, 0, 0, 0, 0xC8, 2};
    m = Motors{1, 0};
    CHECK(b3native::pad::decode_actuator_request(large_only, 0x28, m) && m.small == 1 && m.large == 200);
    // not a DualShock 2's: actuators past the two motors, too few data bits, sizes past the block, no profile
    std::uint8_t other[0x28] = {1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 7};
    std::uint8_t short_data[0x28] = {1, 0, 0, 0, 1, 0, 0, 0, 0xFF, 3};
    std::uint8_t too_big[0x28] = {1, 0, 0, 0, 40, 0, 0, 0};
    std::uint8_t no_profile[0x28] = {0, 0, 0, 0, 2, 0, 0, 0};
    for (const std::uint8_t* b : {other, short_data, too_big, no_profile}) {
        Motors k{1, 5};
        CHECK(!b3native::pad::decode_actuator_request(b, 0x28, k));
        CHECK(k.small == 1 && k.large == 5);   // untouched
    }
    std::uint8_t ok[0x28];
    b3native::pad::encode_game_request(Motors{1, 1}, ok);
    Motors k;
    CHECK(!b3native::pad::decode_actuator_request(ok, 10, k));   // the profile byte past the block
    CHECK(!b3native::pad::decode_actuator_request(nullptr, 0x28, k));
}

TEST(channel_pending_mode_is_the_old_behaviour) {
    // off (the default): the first request is taken, then the channel is busy for good
    ActuatorChannel c;
    CHECK(!c.ack() && !c.busy(0));
    c.send(100);
    for (std::uint64_t t : {100ull, 101ull, 1000000ull, 1ull << 50}) CHECK(c.busy(t));
}

TEST(channel_acknowledges_after_the_rpc) {
    ActuatorChannel c;
    c.set_ack(true, 0, 500);
    CHECK(c.ack() && !c.busy(0));
    c.send(1000);
    CHECK(c.busy(1000) && c.busy(1499) && !c.busy(1500));
    c.send(1600);   // the game's next change: taken
    CHECK(c.busy(1700) && !c.busy(2100));
    // switched off while one is in flight: that one is still answered, the next one stays pending
    c.send(3000);
    c.set_ack(false, 3100, 500);
    CHECK(c.busy(3200) && !c.busy(3500));
    c.send(4000);
    CHECK(c.busy(1ull << 40));
    // switched on with a request left pending: the device manager answers it then
    c.set_ack(true, 5000, 500);
    CHECK(c.busy(5499) && !c.busy(5500));
}

TEST(pc_rumble_values) {
    using b3native::pad::rumble_out;
    auto r = rumble_out(Motors{0, 0}, 100, true);
    CHECK(!r.any());
    r = rumble_out(Motors{0, 255}, 100, false);
    CHECK(r.low == 0xFFFF && r.high == 0 && r.left_trigger == 0 && r.right_trigger == 0);
    r = rumble_out(Motors{1, 0}, 100, false);
    CHECK(r.low == 0 && r.high == 0xFFFF);
    r = rumble_out(Motors{1, 128}, 50, false);
    CHECK(r.low == 128 * 257 / 2 && r.high == 0xFFFF / 2);
    r = rumble_out(Motors{1, 254}, 100, true);
    CHECK(r.left_trigger == 254 * 257 / 2 && r.right_trigger == 254 * 257 / 2 + 0x4000);
    r = rumble_out(Motors{0, 100}, 250, false);   // the strength clamps to 100 %
    CHECK(r.low == 100 * 257);
}

TEST(pad_recording_keeps_the_rumble_switches) {
    const std::string path = (std::filesystem::temp_directory_path() / "test_pad_actuator.pads.txt").string();
    {
        b3native::PadRecorder rec;
        CHECK(rec.open(path));
        b3native::PadReport s{};
        s.fill(0x11);
        rec.put(10, s);
        rec.note_rumble(500, true);
        rec.note_rumble(900, false);
        rec.close(1000);
    }
    b3native::PadReplay rp;
    b3native::PadReport neutral{};
    CHECK(rp.load(path, neutral));
    CHECK(rp.changes() == 1 && rp.end() == 1000);   // the rumble lines are not reports
    CHECK(!rp.rumble_at(0) && !rp.rumble_at(499) && rp.rumble_at(500) && rp.rumble_at(899) && !rp.rumble_at(900));
    std::remove(path.c_str());
}

TEST_MAIN("pad actuators (rumble1)")
