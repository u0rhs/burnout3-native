// Keyboard -> DS2 report (src/native/input_keys.h), without a window: the default map, the report bytes, the steering
// ramp, focus loss and the combination with a real pad's report.
#include "input_keys.h"
#include "test.h"

#include <set>

using namespace b3native::input;

namespace {
bool pressed(const std::array<std::uint8_t, 32>& s, int button) { return !(s[button >> 3] & (1u << (button & 7))); }
constexpr int kSelect = 0, kStart = 3, kUp = 4, kRight = 5, kDown = 6, kLeft = 7, kL2 = 8, kR2 = 9, kL1 = 10, kR1 = 11,
              kTriangle = 12, kCircle = 13, kCross = 14, kSquare = 15;
}  // namespace

TEST(neutral_matches_platform_sif) {
    // platform_sif.cpp kNeutralPad [docs/specs/input.md §1, the neutral report]
    const std::uint8_t want[32] = {0xFF, 0xFF, 0x80, 0x7F, 0x80, 0x7F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0,    0,    0x80, 0x80, 0x80, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const auto n = neutral_sample();
    CHECK(std::memcmp(n.data(), want, 32) == 0);
    KeyPad k;
    k.step(1.0f / 60);
    CHECK(k.sample() == n);
}

TEST(default_map_covers_every_button) {
    std::set<Ds2> bound;
    for (const KeyBinding& b : default_key_map()) bound.insert(b.control);
    for (Ds2 c : {Ds2::Select, Ds2::Start, Ds2::Up, Ds2::Right, Ds2::Down, Ds2::Left, Ds2::L2, Ds2::R2, Ds2::L1, Ds2::R1,
                  Ds2::Triangle, Ds2::Circle, Ds2::Cross, Ds2::Square, Ds2::SteerLeft, Ds2::SteerRight, Ds2::StickUp,
                  Ds2::StickDown})
        CHECK(bound.count(c) == 1);
    CHECK(std::string(ds2_name(Ds2::Cross)) == "cross");
    CHECK(std::string(ds2_name(Ds2::SteerLeft)) == "steer_left");
}

TEST(buttons_digital_and_pressure) {
    struct Row { std::uint16_t key; int bit; int pressure; };
    // menus (the default context)
    const Row rows[] = {{sc::Z, kCross, 12},      {sc::Space, kCross, 12},    {sc::Return, kCross, 12},
                        {sc::KpEnter, kCross, 12}, {sc::X, kCircle, 11},      {sc::H, kCircle, 11},
                        {sc::C, kTriangle, 10},   {sc::Backspace, kTriangle, 10}, {sc::Escape, kStart, -1},
                        {sc::Q, kL1, 14},         {sc::E, kR2, 17},           {sc::R, kR1, 15},
                        {sc::LShift, kR1, 15},    {sc::N1, kL2, 16},          {sc::N3, kR2, 17},
                        {sc::Tab, kSelect, -1},   {sc::Up, kUp, 8},           {sc::W, kUp, 8},
                        {sc::Down, kDown, 9},     {sc::S, kDown, 9}};
    for (const Row& r : rows) {
        KeyPad k;
        k.key(r.key, true);
        k.step(1.0f / 60);
        const auto s = k.sample();
        CHECK(pressed(s, r.bit));
        int others = 0;
        for (int b = 0; b < 16; ++b) others += b != r.bit && pressed(s, b);
        CHECK(others == 0);
        if (r.pressure >= 0) CHECK(s[r.pressure] == 0xFF);
        k.key(r.key, false);
        CHECK(!pressed(k.sample(), r.bit));
    }
}

TEST(steering_ramps_up_and_back) {
    KeyPad k;   // ramp_up 0.12 s, ramp_down 0.06 s
    k.key(sc::D, true);
    k.step(1.0f / 60);
    CHECK_NEAR(k.steer(), (1.0f / 60) / 0.12f, 1e-5);
    CHECK(k.sample()[4] > 0x80 && k.sample()[4] < 0xFF);   // partly right, not full
    for (int i = 0; i < 6; ++i) k.step(1.0f / 60);
    CHECK(k.steer() < 1.0f);   // 7 / 60 s of 0.12 s
    k.step(1.0f / 60);
    CHECK(k.steer() == 1.0f);
    CHECK(k.sample()[4] == 0xFF);
    k.key(sc::D, false);
    k.step(1.0f / 60);
    CHECK(k.steer() > 0 && k.steer() < 1);
    for (int i = 0; i < 4; ++i) k.step(1.0f / 60);
    CHECK(k.steer() == 0.0f);
    CHECK(k.sample()[4] == 0x80);
}

TEST(steering_reversal_passes_centre) {
    KeyPad k;
    k.key(sc::A, true);
    for (int i = 0; i < 10; ++i) k.step(1.0f / 60);
    CHECK(k.steer() == -1.0f);
    CHECK(k.sample()[4] == 0x00);   // full left [input.md §2: X(-1) = 0]
    k.key(sc::A, false);
    k.key(sc::D, true);
    float prev = k.steer();
    bool crossed = false;
    for (int i = 0; i < 20; ++i) {
        k.step(1.0f / 60);
        CHECK(k.steer() >= prev);   // monotonic toward the new side
        crossed |= k.steer() == 0.0f;
        prev = k.steer();
    }
    CHECK(crossed);
    CHECK(k.steer() == 1.0f);
    k.key(sc::A, true);   // both held: centre
    for (int i = 0; i < 10; ++i) k.step(1.0f / 60);
    CHECK(k.steer() == 0.0f);
}

TEST(arrow_keys_dpad_digital_at_once_pressure_ramped) {
    KeyPad k;
    k.key(sc::Left, true);
    CHECK(pressed(k.sample(), kLeft));   // menus see the D-pad before any step
    k.step(1.0f / 60);
    const auto s1 = k.sample();
    CHECK(s1[7] > 0 && s1[7] < 0xFF);   // left pressure ramping (GetSteering reads ANALOG_LEFT)
    CHECK(s1[4] < 0x80);                // and the stick
    for (int i = 0; i < 10; ++i) k.step(1.0f / 60);
    CHECK(k.sample()[7] == 0xFF);
    CHECK(k.sample()[4] == 0x00);
    k.key(sc::Left, false);
    CHECK(!pressed(k.sample(), kLeft));
    for (int i = 0; i < 10; ++i) k.step(1.0f / 60);
    CHECK(k.sample()[7] == 0);
    k.key(sc::Right, true);
    for (int i = 0; i < 10; ++i) k.step(1.0f / 60);
    CHECK(pressed(k.sample(), kRight) && k.sample()[6] == 0xFF && k.sample()[4] == 0xFF);
}

// mac4: the driving context (native_play: the race simulation runs): W / Up accelerate (Cross) with the stick up,
// S / Down brake (Square) with the stick down, Space boost (R1); Enter / Escape / A / D as in menus
TEST(driving_context) {
    struct Row { std::uint16_t key; int bit; int pressure; int stick_y; };
    const Row rows[] = {{sc::W, kCross, 12, 0x00},   {sc::Up, kCross, 12, 0x00},   {sc::S, kSquare, 13, 0xFF},
                        {sc::Down, kSquare, 13, 0xFF}, {sc::Space, kR1, 15, 0x7F}, {sc::Return, kCross, 12, 0x7F},
                        {sc::Escape, kStart, -1, 0x7F}, {sc::C, kTriangle, 10, 0x7F}, {sc::E, kR2, 17, 0x7F}};
    for (const Row& r : rows) {
        KeyPad k;
        k.set_driving(true);
        CHECK(k.driving());
        k.key(r.key, true);
        k.step(1.0f / 60);
        const auto s = k.sample();
        CHECK(pressed(s, r.bit));
        int others = 0;
        for (int b = 0; b < 16; ++b) others += b != r.bit && pressed(s, b);
        CHECK(others == 0);
        if (r.pressure >= 0) CHECK(s[r.pressure] == 0xFF);
        CHECK(s[5] == r.stick_y);
    }
    KeyPad k;   // steering works in both contexts
    k.set_driving(true);
    k.key(sc::Left, true);
    for (int i = 0; i < 10; ++i) k.step(1.0f / 60);
    CHECK(k.sample()[4] == 0x00 && k.sample()[7] == 0xFF);
}

// mac4: in menus Up / W move up and never press Cross (confirm); the context can change while a key is held
TEST(menu_up_never_confirms) {
    KeyPad k;
    for (std::uint16_t key : {sc::Up, sc::W}) {
        k.key(key, true);
        const auto s = k.sample();
        CHECK(pressed(s, kUp) && !pressed(s, kCross) && s[12] == 0 && s[5] == 0x7F);
        k.key(key, false);
    }
    k.key(sc::W, true);
    k.set_driving(true);    // the race starts with W held: it accelerates
    CHECK(pressed(k.sample(), kCross) && !pressed(k.sample(), kUp));
    k.set_driving(false);   // paused: W is up again
    CHECK(!pressed(k.sample(), kCross) && pressed(k.sample(), kUp));
}

TEST(stick_y_and_bytes) {
    KeyPad k;
    k.set_driving(true);
    k.key(sc::W, true);
    CHECK(k.sample()[5] == 0x00);   // up [input.md §2: Y(-1) = 0]
    k.key(sc::W, false);
    k.key(sc::S, true);
    CHECK(k.sample()[5] == 0xFF);
    CHECK(stick_x_byte(0) == 0x80 && stick_y_byte(0) == 0x7F);   // the neutral bytes [input.md §2]
    CHECK(stick_x_byte(1) == 0xFF && stick_x_byte(-1) == 0x00);
}

TEST(focus_loss_releases_everything) {
    KeyPad k;
    k.key(sc::Z, true);
    k.key(sc::D, true);
    for (int i = 0; i < 10; ++i) k.step(1.0f / 60);
    k.release_all();
    CHECK(k.sample() == neutral_sample());
    k.step(1.0f / 60);
    CHECK(k.sample() == neutral_sample());
}

TEST(compose_with_a_real_pad) {
    std::array<std::uint8_t, 32> pad = neutral_sample();
    pad[1] = static_cast<std::uint8_t>(pad[1] & ~(1u << (kSquare - 8)));   // the pad holds square (brake)
    pad[13] = 0x90;
    pad[4] = 0x60;                                                          // and steers a little left
    KeyPad k;
    CHECK(k.compose(pad.data()) == pad);   // no keys: the pad's report unchanged
    k.key(sc::Z, true);                    // keys add cross
    auto s = k.compose(pad.data());
    CHECK(pressed(s, kCross) && pressed(s, kSquare));
    CHECK(s[12] == 0xFF && s[13] == 0x90);
    CHECK(s[4] == 0x60);                   // no steer key: the pad's stick
    k.key(sc::D, true);
    for (int i = 0; i < 10; ++i) k.step(1.0f / 60);
    s = k.compose(pad.data());
    CHECK(s[4] == 0xFF);                   // full right is further from centre than the pad's 0x60
    CHECK(k.compose(nullptr) == k.sample());
}

TEST(remap) {
    KeyPad k({{sc::LShift, Ds2::Cross}});
    k.key(sc::Z, true);
    CHECK(!pressed(k.sample(), kCross));
    k.key(sc::LShift, true);
    CHECK(pressed(k.sample(), kCross));
    k.set_map(default_key_map());
    CHECK(pressed(k.sample(), kCross));    // Z is still down and is cross again
    k.key(500, true);                      // unbound / out-of-range scancodes are harmless
    k.key(9999, true);
}

TEST(gamepad_default_bindings) {
    GamepadState g;
    CHECK(gamepad_sample(g) == neutral_sample());
    g.south = true;
    auto s = gamepad_sample(g);
    CHECK(pressed(s, kCross) && s[12] == 0xFF);
    g = {};
    g.right_trigger = 16384;   // half throttle: cross pressed (>= 0.25), pressure 128 [input.md §3: 255 * 0.5000153 -> 128]
    s = gamepad_sample(g);
    CHECK(pressed(s, kCross) && s[12] == 128);
    g.right_trigger = 6000;    // 0.18: below the threshold, pressure still reported
    s = gamepad_sample(g);
    CHECK(!pressed(s, kCross) && s[12] == 47);
    g = {};
    g.left_trigger = 32767;    // full brake on square
    s = gamepad_sample(g);
    CHECK(pressed(s, kSquare) && s[13] == 0xFF);
    g = {};
    g.east = g.north = g.left_shoulder = g.right_shoulder = g.start = g.back = g.dpad_up = true;
    s = gamepad_sample(g);
    CHECK(pressed(s, kCircle) && pressed(s, kTriangle) && pressed(s, kL1) && pressed(s, kR1) && pressed(s, kStart) &&
          pressed(s, kSelect) && pressed(s, kUp));
    CHECK(!pressed(s, kL2) && !pressed(s, kR2) && !pressed(s, kCross));   // no pad binding for L2 / R2 [input.md §3]
    CHECK(s[11] == 0xFF && s[10] == 0xFF && s[8] == 0xFF);
}

TEST(gamepad_stick_dead_zone) {
    GamepadState g;
    g.left_x = 7000;   // 0.21: inside the 0.24 dead zone
    CHECK(gamepad_sample(g)[4] == 0x80);
    g.left_x = 32767;
    CHECK(gamepad_sample(g)[4] == 0xFF);
    g.left_x = -32768;
    CHECK(gamepad_sample(g)[4] == 0x00);
    g.left_x = 0;
    g.left_y = -32768;   // SDL up -> 0x00, as the keyboard's W
    CHECK(gamepad_sample(g)[5] == 0x00);
    g.left_y = 0;
    g.left_x = 20000;    // (0.6104 - 0.24) / 0.76 = 0.4874 -> axis 15970 -> byte (15970*127+16383)/32767 + 128
    CHECK(gamepad_sample(g)[4] == static_cast<std::uint8_t>((15970 * 127 + 16383) / 32767 + 128));
}

TEST(keys_and_gamepad_together) {
    GamepadState g;
    g.right_trigger = 32767;   // throttle on the pad
    KeyPad k;
    k.key(sc::A, true);        // steer on the keys
    for (int i = 0; i < 10; ++i) k.step(1.0f / 60);
    const auto pad = gamepad_sample(g);
    const auto s = k.compose(pad.data());
    CHECK(pressed(s, kCross) && s[12] == 0xFF && s[4] == 0x00);
}

TEST(boost_toggle_latch) {
    // accessibility, BOOST: Toggle (opt-in, input-changing). Off: every report untouched, whatever R1 does
    const auto with_r1 = [](bool down, bool start = false) {
        auto s = neutral_sample();
        if (down) { s[kR1 >> 3] = std::uint8_t(s[kR1 >> 3] & ~(1u << (kR1 & 7))); s[15] = 0xFF; }
        if (start) s[kStart >> 3] = std::uint8_t(s[kStart >> 3] & ~(1u << (kStart & 7)));
        s[12] = 0x40;   // something else in the report (a half-pressed Cross) stays as it is
        return s;
    };
    {
        BoostLatch hold;
        for (bool d : {false, true, true, false, true, false}) {
            for (bool driving : {false, true}) {
                auto r = with_r1(d);
                const auto before = r;
                hold.apply(r, false, driving);
                CHECK(r == before && !hold.latched());
            }
        }
    }
    BoostLatch t;
    auto step = [&](bool down, bool driving = true, bool start = false) {
        auto r = with_r1(down, start);
        t.apply(r, true, driving);
        CHECK(r[12] == 0x40);
        return r;
    };
    // a tap latches: R1 stays held (bit and full pressure) after the button is let go
    CHECK(!pressed(step(false), kR1));
    auto r = step(true);
    CHECK(pressed(r, kR1) && r[15] == 0xFF && t.latched());
    for (int k = 0; k < 5; ++k) { r = step(false); CHECK(pressed(r, kR1) && r[15] == 0xFF); }
    r = step(true);   // held longer: still latched (one toggle per press)
    CHECK(!pressed(r, kR1) && r[15] == 0 && !t.latched());   // the second press releases it, while still held
    r = step(true);
    CHECK(!pressed(r, kR1) && !t.latched());
    r = step(false);
    CHECK(!pressed(r, kR1));
    // pause (Start) drops the latch and passes the report as it is
    step(true);
    step(false);
    CHECK(t.latched());
    r = step(false, true, true);
    CHECK(!t.latched() && !pressed(r, kR1) && pressed(r, kStart));
    CHECK(!pressed(step(false), kR1));
    // the driving context ending drops it too; a button held across the change does not toggle by itself
    step(true);
    CHECK(t.latched());
    r = step(true, false);
    CHECK(!t.latched() && pressed(r, kR1));   // menus: R1 as it is (held here)
    r = step(true, true);
    CHECK(!t.latched() && !pressed(r, kR1));  // still held from before: no new press, no latch
    r = step(false);
    r = step(true);
    CHECK(t.latched() && pressed(r, kR1));
}

TEST(shoulder_series) {
    // carsel1: on the car select page, L1 / R1 presses reach the game as Left / Right; elsewhere (or off) nothing changes
    const auto with = [](std::initializer_list<int> buttons) {
        auto r = neutral_sample();
        for (int b : buttons) {
            r[std::size_t(b >> 3)] = static_cast<std::uint8_t>(r[std::size_t(b >> 3)] & ~(1u << (b & 7)));
            if (const int p = pressure_byte(Ds2(b)); p >= 0) r[std::size_t(p)] = 0xC0;
        }
        return r;
    };
    {   // off, or off the page: every report as it is, byte for byte
        for (bool on : {false, true}) {
            ShoulderSeries s;
            for (int k = 0; k < 4; ++k)
                for (auto r : {with({kL1}), with({kR1}), with({}), with({kL1, kR1, kCross}), with({kLeft})}) {
                    const auto before = r;
                    CHECK(!s.apply(r, on, !on && k % 2 == 0) && r == before);
                }
            CHECK(s.presses() == 0);
        }
    }
    ShoulderSeries s;
    auto r = with({});
    CHECK(!s.apply(r, true, true) && r == with({}));
    // L1 pressed on the page: Left (bit and full pressure), L1 cleared, held as long as L1 is
    for (int k = 0; k < 3; ++k) {
        r = with({kL1});
        CHECK(s.apply(r, true, true));
        CHECK(pressed(r, kLeft) && !pressed(r, kL1) && r[std::size_t(pressure_byte(Ds2::Left))] == 0xFF &&
              r[std::size_t(pressure_byte(Ds2::L1))] == 0 && s.translating(0));
    }
    r = with({});
    CHECK(!s.apply(r, true, true) && r == with({}) && !s.translating(0));
    // R1 -> Right; the other buttons pass
    r = with({kR1, kCross});
    CHECK(s.apply(r, true, true) && pressed(r, kRight) && !pressed(r, kR1) && pressed(r, kCross) && !pressed(r, kLeft));
    CHECK(s.presses() == 2);
    // the page going away mid-press: R1 passes as it is (no Right held into the next page), and stays so on return
    r = with({kR1});
    CHECK(!s.apply(r, true, false) && r == with({kR1}));
    r = with({kR1});
    CHECK(!s.apply(r, true, true) && r == with({kR1}));
    r = with({});
    s.apply(r, true, true);
    // a shoulder held into the page is not translated until pressed again there
    ShoulderSeries h;
    r = with({kL1});
    CHECK(!h.apply(r, true, false));
    r = with({kL1});
    CHECK(!h.apply(r, true, true) && r == with({kL1}));
    r = with({});
    h.apply(r, true, true);
    r = with({kL1});
    CHECK(h.apply(r, true, true) && pressed(r, kLeft));
    // switched off mid-press: as it is
    r = with({kL1});
    CHECK(!h.apply(r, false, true) && r == with({kL1}));
}

TEST(pad_seats_one_pad_is_player_1_only) {
    // pad2: no pad, or one pad: player 1 only (port 2 absent) -- the behaviour before pad2
    PadSeats p;
    CHECK(!p.port2_connected() && !p.any_player1_pad() && p.pads() == 0);
    p.added(7);
    CHECK(p.role(7) == PadSeats::Role::Player1 && p.drives_player1(7) && !p.port2_connected() && p.any_player1_pad());
    p.added(7);   // a repeated connect changes nothing
    CHECK(p.pads() == 1 && !p.port2_connected());
    p.removed(7);
    CHECK(p.role(7) == PadSeats::Role::None && !p.any_player1_pad() && !p.port2_connected() && p.pads() == 0);
    p.removed(7);   // unknown: ignored
    p.added(0);     // SDL's invalid id
    CHECK(p.pads() == 0);
}

TEST(pad_seats_connection_order) {
    PadSeats p;
    p.added(12);
    p.added(5);   // the second connected is player 2, whatever its id
    CHECK(p.seat(0) == 12 && p.seat(1) == 5 && p.port2_connected());
    CHECK(p.drives_player1(12) && !p.drives_player1(5) && p.role(5) == PadSeats::Role::Player2);
    // player 2's pad out: port 2 unplugged; back in: player 2 again
    p.removed(5);
    CHECK(!p.port2_connected() && p.seat(0) == 12);
    p.added(9);
    CHECK(p.port2_connected() && p.seat(1) == 9);
    // player 1's pad out: player 2 keeps its seat (as a PS2 pad stays in its port); the next pad fills player 1's
    p.removed(12);
    CHECK(p.seat(0) == 0 && p.seat(1) == 9 && p.port2_connected() && !p.any_player1_pad());
    p.added(13);
    CHECK(p.seat(0) == 13 && p.seat(1) == 9);
}

TEST(pad_seats_extra_pads_wait) {
    PadSeats p;
    p.added(1);
    p.added(2);
    p.added(3);
    p.added(4);
    CHECK(p.pads() == 4 && p.role(3) == PadSeats::Role::Waiting && p.role(4) == PadSeats::Role::Waiting);
    CHECK(p.drives_player1(3) && p.drives_player1(4) && !p.drives_player1(2));   // waiting pads compose into player 1
    p.removed(2);   // the oldest waiting pad takes the empty seat
    CHECK(p.seat(1) == 3 && p.port2_connected() && p.role(4) == PadSeats::Role::Waiting);
    p.removed(4);
    CHECK(p.role(4) == PadSeats::Role::None && p.pads() == 2);
    p.removed(1);
    CHECK(p.seat(0) == 0 && p.seat(1) == 3);
    p.removed(3);
    CHECK(p.pads() == 0 && !p.port2_connected());
}

// ---- the report encodings (src/native/input_report.cpp, docs/specs/input.md §1-§3) ----
// The tabled bytes below were computed apart from the C++ by a few lines of Python over exact rationals (python3 -I):
//   axis(v) = round_half_away(f32(v * (32768 if v < 0 else 32767)))   (v clamped to -1..1; the product rounded to a
//             float first, as the baseline forms it: the rows below are the same with the exact product, except the
//             near-half rows of stick_bytes_rules)
//   X(w) = 128 + floor(127 w / 32767 + 1/2)  (w >= 0),   128 - floor(|w| / 256 + 1/2)          (w < 0)
//   Y(w) = 127 + floor(128 w / 32767 + 1/2)  (w >= 0),   127 - floor(127 |w| / 32768 + 1/2)    (w < 0)
//   stick(raw, z): n = raw / (32768 if raw < 0 else 32767); clamp01((n - z) / (1 - z)) - clamp01((-n - z) / (1 - z))
//   pressure(raw) = floor(255 raw / 32767 + 1/2)

TEST(pressure_byte_layout) {
    // input.md §1: right, left, up, down, triangle, circle, cross, square, L1, R1, L2, R2 at bytes 6..17
    const std::pair<Ds2, int> rows[] = {{Ds2::Right, 6},     {Ds2::Left, 7},    {Ds2::Up, 8},     {Ds2::Down, 9},
                                        {Ds2::Triangle, 10}, {Ds2::Circle, 11}, {Ds2::Cross, 12}, {Ds2::Square, 13},
                                        {Ds2::L1, 14},       {Ds2::R1, 15},     {Ds2::L2, 16},    {Ds2::R2, 17}};
    for (const auto& [c, b] : rows) CHECK(pressure_byte(c) == b);
    for (Ds2 c : {Ds2::Select, Ds2::L3, Ds2::R3, Ds2::Start, Ds2::SteerLeft, Ds2::SteerRight, Ds2::StickUp,
                  Ds2::StickDown, Ds2::Count})
        CHECK(pressure_byte(c) == -1);
    std::set<int> used;   // twelve distinct bytes
    for (int i = 0; i < 16; ++i) {
        const int p = pressure_byte(static_cast<Ds2>(i));
        if (p >= 0) CHECK(used.insert(p).second && p >= 6 && p <= 17);
    }
    CHECK(used.size() == 12);
}

TEST(stick_bytes_table) {
    struct Row { float v; int x, y; };
    const Row rows[] = {
        {-1.0f, 0, 0},           {-0.75f, 32, 32},        {-0.5f, 64, 63},          {-0.25f, 96, 95},
        {-0.1f, 115, 114},       {-0.004f, 127, 126},     {-127.5f / 32768, 127, 127}, {-129.5f / 32768, 127, 126},
        {-1.0f / 32768, 128, 127}, {0.0f, 128, 127},      {1.0f / 32767, 128, 127}, {130.0f / 32767, 129, 128},
        {0.1f, 141, 140},        {0.25f, 160, 159},       {0.3f, 166, 165},         {0.487329215f, 190, 189},
        {0.5f, 192, 191},        {0.6f, 204, 204},        {0.75f, 223, 223},        {0.9f, 242, 242},
        {0.999f, 255, 255},      {1.0f, 255, 255},
    };
    for (const Row& r : rows) {
        CHECK(stick_x_byte(r.v) == r.x);
        CHECK(stick_y_byte(r.v) == r.y);
    }
}

TEST(stick_bytes_rules) {
    // the ends and the centre [input.md §2 checks]
    CHECK(stick_x_byte(-1) == 0 && stick_x_byte(0) == 128 && stick_x_byte(1) == 255);
    CHECK(stick_y_byte(-1) == 0 && stick_y_byte(0) == 127 && stick_y_byte(1) == 255);
    CHECK(stick_x_byte(-0.0f) == 128 && stick_y_byte(-0.0f) == 127);
    // clamped outside -1..1
    for (float v : {1.0001f, 2.0f, 1e30f, INFINITY}) CHECK(stick_x_byte(v) == 255 && stick_y_byte(v) == 255);
    for (float v : {-1.0001f, -2.0f, -1e30f, -INFINITY}) CHECK(stick_x_byte(v) == 0 && stick_y_byte(v) == 0);
    // v -> w rounds halves away from zero: 0.5 * 32767 = 16383.5 -> 16384 -> X 192 (16383 would give 191)
    CHECK(stick_x_byte(0.5f) == 192);
    // ... and on the negative side: -127.5 -> -128 -> X 127 (|w| / 256 = 0.5, halves up); -127 -> 128
    CHECK(stick_x_byte(-127.5f / 32768) == 127 && stick_x_byte(-127.0f / 32768) == 128);
    // Y's negative half: -129.5 -> -130 -> 127 * 130 / 32768 = 0.504 -> 126; -129 -> 0.49997 -> 127
    CHECK(stick_y_byte(-129.5f / 32768) == 126 && stick_y_byte(-129.0f / 32768) == 127);
    // an exact half in the negative Y rule: |w| = 16384 -> 127 * 16384 / 32768 = 63.5 -> up -> 127 - 64
    CHECK(stick_y_byte(-0.5f) == 63);
    // the first step off centre on the positive side: X needs w >= 130 (127 w / 32767 >= 1/2), Y w >= 128
    CHECK(stick_x_byte(129.0f / 32767) == 128 && stick_x_byte(130.0f / 32767) == 129);
    CHECK(stick_y_byte(127.0f / 32767) == 127 && stick_y_byte(128.0f / 32767) == 128);
    // ... and on the negative side: X |w| >= 128, Y |w| >= 130
    CHECK(stick_x_byte(-128.0f / 32768) == 127 && stick_y_byte(-130.0f / 32768) == 126);
    // v * 32767 is formed in single precision before the rounding (the baseline's behaviour, verifier report): near a
    // half the float product rounds up where the exact product would round down
    struct Near { std::uint32_t bits; int x, y; };
    const Near near_half[] = {{0x3b818103, 129, 128}, {0x3d113122, 133, 132}, {0x3d51b1a3, 135, 134},
                              {0x3d891912, 137, 136}, {0x3dc99993, 141, 140}, {0x3b7f01fe, 128, 128},
                              {0x3f010002, 192, 192}, {0x3f030006, 193, 193}, {0x3f07000e, 195, 195}};
    for (const Near& n : near_half) {
        CHECK(stick_x_byte(t::bitsf(n.bits)) == n.x);
        CHECK(stick_y_byte(t::bitsf(n.bits)) == n.y);
    }
    // every byte is monotonic in v over every ±k/32768 (and the positive half's k/32767)
    int px = 0, py = 0;
    for (int k = -32768; k <= 32767; ++k) {
        const float v = k < 0 ? float(k) / 32768.0f : float(k) / 32767.0f;
        const int x = stick_x_byte(v), y = stick_y_byte(v);
        CHECK(x >= px && y >= py);
        px = x;
        py = y;
    }
    CHECK(px == 255 && py == 255);
}

TEST(stick_bytes_exhaustive_axis) {
    // every SDL axis value w (v = w / 32768 or w / 32767, so axis(v) = w) against the §2 integer rules
    long bad = 0;
    for (int w = -32768; w <= 32767; ++w) {
        const float v = w < 0 ? float(w) / 32768.0f : float(w) / 32767.0f;
        int x, y;
        if (w >= 0) {
            x = 128 + int(std::floor(127.0 * w / 32767 + 0.5));
            y = 127 + int(std::floor(128.0 * w / 32767 + 0.5));
        } else {
            x = 128 - int(std::floor(-w / 256.0 + 0.5));
            y = 127 - int(std::floor(127.0 * -w / 32768 + 0.5));
        }
        bad += stick_x_byte(v) != x || stick_y_byte(v) != y;
    }
    CHECK(bad == 0);
}

TEST(gamepad_buttons_each) {
    struct Row { bool GamepadState::*b; int bit; int pressure; };
    const Row rows[] = {{&GamepadState::south, kCross, 12},          {&GamepadState::east, kCircle, 11},
                        {&GamepadState::west, kSquare, 13},          {&GamepadState::north, kTriangle, 10},
                        {&GamepadState::left_shoulder, kL1, 14},     {&GamepadState::right_shoulder, kR1, 15},
                        {&GamepadState::start, kStart, -1},          {&GamepadState::back, kSelect, -1},
                        {&GamepadState::dpad_up, kUp, 8},            {&GamepadState::dpad_down, kDown, 9},
                        {&GamepadState::dpad_left, kLeft, 7},        {&GamepadState::dpad_right, kRight, 6}};
    for (const Row& r : rows) {
        GamepadState g;
        g.*r.b = true;
        const auto s = gamepad_sample(g);
        auto want = neutral_sample();
        want[std::size_t(r.bit >> 3)] = std::uint8_t(want[std::size_t(r.bit >> 3)] & ~(1u << (r.bit & 7)));
        if (r.pressure >= 0) want[std::size_t(r.pressure)] = 0xFF;
        CHECK(s == want);   // its bit and pressure only
    }
    GamepadState all;   // everything held: L2, R2, L3, R3 and the right stick stay untouched
    for (const Row& r : rows) all.*r.b = true;
    const auto s = gamepad_sample(all);
    CHECK(s[0] == 0x06 && s[1] == 0x03);   // only L3 / R3 (bits 1, 2) and L2 / R2 (bits 8, 9) released
    CHECK(s[16] == 0 && s[17] == 0 && s[2] == 0x80 && s[3] == 0x7F && s[4] == 0x80 && s[5] == 0x7F);
    for (int i = 6; i < 16; ++i) CHECK(s[i] == 0xFF);
    for (int i = 18; i < 22; ++i) CHECK(s[i] == 0x80);
    for (int i = 22; i < 32; ++i) CHECK(s[i] == 0);
}

TEST(gamepad_triggers_are_pedals) {
    struct Row { std::int16_t raw; int pressure; };
    const Row rows[] = {{0, 0},       {1, 0},       {128, 1},     {129, 1},       {327, 3},       {328, 3},     {6000, 47},
                        {8191, 64},   {8192, 64},   {16383, 127}, {16384, 128},   {24000, 187},   {32766, 255}, {32767, 255}};
    for (const Row& r : rows) {
        GamepadState g;
        g.right_trigger = r.raw;
        auto s = gamepad_sample(g);
        CHECK(s[12] == r.pressure && pressed(s, kCross) == (r.raw >= 8192));   // default threshold 0.25
        CHECK(s[13] == 0 && !pressed(s, kSquare));
        g = {};
        g.left_trigger = r.raw;
        s = gamepad_sample(g);
        CHECK(s[13] == r.pressure && pressed(s, kSquare) == (r.raw >= 8192));
        CHECK(s[12] == 0 && !pressed(s, kCross));
    }
    // the face button wins over a light trigger, and is pressed whatever the trigger
    GamepadState g;
    g.south = true;
    g.right_trigger = 6000;
    auto s = gamepad_sample(g);
    CHECK(pressed(s, kCross) && s[12] == 0xFF);
    g = {};
    g.west = true;
    g.left_trigger = 100;
    s = gamepad_sample(g);
    CHECK(pressed(s, kSquare) && s[13] == 0xFF);
    // a negative reading counts as released
    g = {};
    g.right_trigger = -5;
    g.left_trigger = -32768;
    CHECK(gamepad_sample(g) == neutral_sample());
    // the threshold, clamped to 0.01..1: 0 -> 0.01 (328 / 32767 = 0.01001 pressed, 327 not); 5 -> 1 (only full)
    GamepadTuning lo{0.24f, 0.0f}, hi{0.24f, 5.0f}, one{0.24f, 0.01f}, full{0.24f, 1.0f};
    for (const GamepadTuning* t : {&lo, &one}) {
        g = {};
        g.right_trigger = 327;
        CHECK(!pressed(gamepad_sample(g, *t), kCross));
        g.right_trigger = 328;
        CHECK(pressed(gamepad_sample(g, *t), kCross));
    }
    for (const GamepadTuning* t : {&hi, &full}) {
        g = {};
        g.left_trigger = 32766;
        s = gamepad_sample(g, *t);
        CHECK(!pressed(s, kSquare) && s[13] == 255);
        g.left_trigger = 32767;
        CHECK(pressed(gamepad_sample(g, *t), kSquare));
    }
}

TEST(gamepad_stick_table) {
    // dead zone 3 clamps to 0.95 and -1 to 0 [input.md §3]
    struct Row { std::int16_t raw; float z; int x, y; };
    const Row rows[] = {
        {-32768, 0.00f, 0, 0}, {-30000, 0.00f, 11, 11}, {-20000, 0.00f, 50, 49}, {-12000, 0.00f, 81, 80},
        {-7865, 0.00f, 97, 97}, {-7864, 0.00f, 97, 97}, {-100, 0.00f, 128, 127}, {0, 0.00f, 128, 127},
        {100, 0.00f, 128, 127}, {7864, 0.00f, 158, 158}, {7865, 0.00f, 158, 158}, {9000, 0.00f, 163, 162},
        {16384, 0.00f, 192, 191}, {20000, 0.00f, 206, 205}, {31456, 0.00f, 250, 250}, {32000, 0.00f, 252, 252},
        {32767, 0.00f, 255, 255},
        {-32768, 0.24f, 0, 0}, {-30000, 0.24f, 14, 14}, {-20000, 0.24f, 66, 65}, {-12000, 0.24f, 107, 106},
        {-7865, 0.24f, 128, 127}, {-7864, 0.24f, 128, 127}, {-100, 0.24f, 128, 127}, {0, 0.24f, 128, 127},
        {100, 0.24f, 128, 127}, {7864, 0.24f, 128, 127}, {7865, 0.24f, 128, 127}, {9000, 0.24f, 134, 133},
        {16384, 0.24f, 171, 171}, {20000, 0.24f, 190, 189}, {31456, 0.24f, 248, 248}, {32000, 0.24f, 251, 251},
        {32767, 0.24f, 255, 255},
        {-32768, 0.50f, 0, 0}, {-30000, 0.50f, 22, 21}, {-20000, 0.50f, 100, 99}, {-12000, 0.50f, 128, 127},
        {-7865, 0.50f, 128, 127}, {-7864, 0.50f, 128, 127}, {-100, 0.50f, 128, 127}, {0, 0.50f, 128, 127},
        {100, 0.50f, 128, 127}, {7864, 0.50f, 128, 127}, {7865, 0.50f, 128, 127}, {9000, 0.50f, 128, 127},
        {16384, 0.50f, 128, 127}, {20000, 0.50f, 156, 155}, {31456, 0.50f, 245, 245}, {32000, 0.50f, 249, 249},
        {32767, 0.50f, 255, 255},
        {-32768, 0.95f, 0, 0}, {-30000, 0.95f, 128, 127}, {-20000, 0.95f, 128, 127}, {-12000, 0.95f, 128, 127},
        {-7865, 0.95f, 128, 127}, {-7864, 0.95f, 128, 127}, {-100, 0.95f, 128, 127}, {0, 0.95f, 128, 127},
        {100, 0.95f, 128, 127}, {7864, 0.95f, 128, 127}, {7865, 0.95f, 128, 127}, {9000, 0.95f, 128, 127},
        {16384, 0.95f, 128, 127}, {20000, 0.95f, 128, 127}, {31456, 0.95f, 153, 153}, {32000, 0.95f, 196, 195},
        {32767, 0.95f, 255, 255},
    };
    for (const Row& r : rows) {
        std::vector<float> zs{r.z};
        if (r.z == 0.95f) zs.push_back(3.0f);
        if (r.z == 0.0f) zs.push_back(-1.0f);
        for (float z : zs) {
            const GamepadTuning t{z, 0.25f};
            GamepadState g;
            g.left_x = r.raw;
            auto s = gamepad_sample(g, t);
            CHECK(s[4] == r.x && s[5] == 0x7F && s[2] == 0x80 && s[3] == 0x7F);
            g = {};
            g.left_y = r.raw;   // SDL's y: down positive, as the report's
            s = gamepad_sample(g, t);
            CHECK(s[5] == r.y && s[4] == 0x80 && s[2] == 0x80 && s[3] == 0x7F);
            g.left_x = r.raw;   // both axes at once: independent
            s = gamepad_sample(g, t);
            CHECK(s[4] == r.x && s[5] == r.y);
        }
    }
    // the default tuning is 0.24 / 0.25 (user-visible settings)
    const GamepadTuning d;
    CHECK(d.stick_dead_zone == 0.24f && d.trigger_threshold == 0.25f);
    // with no dead zone the stick passes straight through: every raw value gives the §2 bytes of its own axis value
    long bad = 0;
    for (int raw = -32768; raw <= 32767; ++raw) {
        GamepadState g;
        g.left_x = g.left_y = static_cast<std::int16_t>(raw);
        const float v = raw < 0 ? float(raw) / 32768.0f : float(raw) / 32767.0f;
        const auto s = gamepad_sample(g, GamepadTuning{0.0f, 0.25f});
        bad += s[4] != stick_x_byte(v) || s[5] != stick_y_byte(v);
    }
    CHECK(bad == 0);
}

TEST(restart_macro) {
    // speedrun1, INSTANT RESTART (opt-in, input-injecting): idle, the report is untouched byte for byte; triggered, the
    // macro's presses alone on the VBlank grid -- Start, Down, Cross, Up, Cross, each kHold samples -- then the
    // player's reports again
    RestartMacro m;
    auto r = neutral_sample();
    r[12] = 0x40;   // a half-pressed Cross of the player's
    const auto before = r;
    CHECK(!m.apply(r) && r == before && !m.active());
    CHECK(m.trigger() && m.active() && !m.trigger());
    std::vector<std::array<std::uint8_t, 32>> out;
    while (m.active()) {
        auto p = before;
        p[kR1 >> 3] = std::uint8_t(p[kR1 >> 3] & ~(1u << (kR1 & 7)));   // the player holding boost: not passed on
        CHECK(m.apply(p));
        out.push_back(p);
    }
    CHECK(int(out.size()) == RestartMacro::length());
    const auto& steps = RestartMacro::steps();
    CHECK(steps.size() == 5 && steps[0].button == Ds2::Start && steps[1].button == Ds2::Down && steps[2].button == Ds2::Cross &&
          steps[3].button == Ds2::Up && steps[4].button == Ds2::Cross);
    for (std::size_t v = 0; v < out.size(); ++v) {
        int down = 0;
        for (int b = 0; b < 16; ++b) down += pressed(out[v], b);
        const RestartMacro::Step* in = nullptr;
        for (const auto& s : steps) if (int(v) >= s.at && int(v) < s.at + RestartMacro::kHold) in = &s;
        CHECK(!pressed(out[v], kR1));
        if (in) {
            CHECK(down == 1 && pressed(out[v], static_cast<int>(in->button)));
            if (in->button == Ds2::Cross) CHECK(out[v][12] == 0xFF);
        } else {
            CHECK(out[v] == neutral_sample());
        }
    }
    for (std::size_t k = 1; k < steps.size(); ++k) CHECK(steps[k].at >= steps[k - 1].at + 2 * RestartMacro::kHold);   // a release between
    auto after = before;
    CHECK(!m.apply(after) && after == before);   // the player's again
    CHECK(m.trigger());
    m.cancel();
    CHECK(!m.active());
}

TEST_MAIN("keyboard -> DS2 report (src/native/input_keys)")
