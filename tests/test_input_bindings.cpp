// Control remapping (remap1, src/native/input_bindings.h): the defaults give the reports of the mapping before remap1
// byte for byte (every key alone and in sequences, both contexts; gamepads over many states), rebinding in a view,
// conflicts (swap / clear / keep), the analog <-> digital conversions, controls.ini's round trip and its invalid entries,
// the rebind flow (timeout, cancel, conflict choice) and the prompts' glyph positions.
#include "input_bindings.h"
#include "test.h"

#include <random>

using namespace b3native::input;

namespace {
bool pressed(const std::array<std::uint8_t, 32>& s, int button) { return !(s[button >> 3] & (1u << (button & 7))); }
constexpr int kCross = 14, kCircle = 13, kSquare = 15, kL2 = 8, kR2 = 9, kUp = 4;

std::array<std::uint8_t, 32> run(KeyPad& k, std::initializer_list<std::uint16_t> keys, bool driving, int steps) {
    k.set_driving(driving);
    for (std::uint16_t s : keys) k.key(s, true);
    for (int i = 0; i < steps; ++i) k.step(1.0f / 60);
    return k.sample();
}

PhysInput key(std::uint16_t s) { return {Device::Keyboard, s, PadInput::None}; }
// padlayout: the 1.x controls (every player on the Custom layout, its bindings the 1.x defaults): the tests written
// before the layouts edit these
Controls classic_controls() {
    Controls c = default_controls();
    for (PlayerControls& p : c.player) p.layout = PadLayout::Custom;
    return c;
}
PlayerControls classic_player(int p) { return classic_controls().player[std::size_t(p)]; }
PhysInput pad(PadInput p) { return {Device::Gamepad, 0, p}; }

GamepadState random_state(std::mt19937& r) {
    auto axis = [&r]() -> std::int16_t {
        switch (r() % 6) {
        case 0: return 0;
        case 1: return -32768;
        case 2: return 32767;
        case 3: return std::int16_t(int(r() % 20001) - 10000);   // around the dead zone
        default: return std::int16_t(int(r() % 65536) - 32768);
        }
    };
    auto bit = [&r]() { return (r() & 3) == 0; };
    GamepadState g;
    g.south = bit(); g.east = bit(); g.west = bit(); g.north = bit(); g.back = bit(); g.start = bit();
    g.left_shoulder = bit(); g.right_shoulder = bit(); g.dpad_up = bit(); g.dpad_down = bit(); g.dpad_left = bit(); g.dpad_right = bit();
    g.guide = bit(); g.left_stick = bit(); g.right_stick = bit(); g.misc1 = bit(); g.touchpad = bit();
    g.paddles = {bit(), bit(), bit(), bit()};
    g.left_x = axis(); g.left_y = axis(); g.right_x = axis(); g.right_y = axis();
    g.left_trigger = axis(); g.right_trigger = axis();
    return g;
}
}  // namespace

// ---- the defaults are the mapping before remap1 ----

TEST(default_keyboard_byte_identical_every_key) {
    const std::vector<KeyBinding> mine = key_map(default_player(0));
    CHECK(mine.size() == default_key_map().size());
    for (int s = 0; s < 512; ++s)
        for (int driving = 0; driving < 2; ++driving)
            for (int steps : {0, 1, 3, 30}) {
                KeyPad a(default_key_map()), b(mine);
                const auto ra = run(a, {std::uint16_t(s)}, driving != 0, steps);
                const auto rb = run(b, {std::uint16_t(s)}, driving != 0, steps);
                CHECK(ra == rb);
                a.key(std::uint16_t(s), false);
                b.key(std::uint16_t(s), false);
                for (int i = 0; i < 5; ++i) { a.step(1.0f / 60); b.step(1.0f / 60); }
                CHECK(a.sample() == b.sample());
            }
}

TEST(default_keyboard_byte_identical_sequences) {
    std::mt19937 r(7);
    const std::uint16_t used[] = {sc::A, sc::C, sc::D, sc::E, sc::H, sc::Q, sc::R, sc::S, sc::V, sc::W, sc::X, sc::Z, sc::N1, sc::N3,
                                  sc::Return, sc::Escape, sc::Backspace, sc::Tab, sc::Space, sc::Right, sc::Left, sc::Down, sc::Up,
                                  sc::KpEnter, sc::LShift, sc::RShift, 14 /* K: unbound */};
    KeyPad a(default_key_map()), b(key_map(default_player(0)));
    int same = 0;
    for (int i = 0; i < 20000; ++i) {
        const std::uint16_t k = used[r() % (sizeof used / sizeof used[0])];
        const bool down = (r() & 1) != 0;
        a.key(k, down);
        b.key(k, down);
        if (r() % 50 == 0) { const bool d = (r() & 1) != 0; a.set_driving(d); b.set_driving(d); }
        if (r() % 300 == 0) { a.release_all(); b.release_all(); }
        const float dt = (r() % 4) / 60.0f;
        a.step(dt);
        b.step(dt);
        same += a.sample() == b.sample();
    }
    CHECK(same == 20000);
}

TEST(default_gamepad_byte_identical) {
    std::mt19937 r(11);
    int same = 0;
    const int n = 200000;
    for (int i = 0; i < n; ++i) {
        GamepadState g = random_state(r);
        // pad1: gyro steering's fields hold any steering while not in use (gyro_use 0): the report ignores them
        g.gyro_steer = float(int(r() % 4001) - 2000) / 1000.0f;
        GamepadTuning t;
        if (i % 3 == 1) { t.stick_dead_zone = float(r() % 100) / 100.0f; t.trigger_threshold = float(r() % 120) / 100.0f; }
        const auto want = gamepad_sample(g, t);
        same += gamepad_sample(g, classic_player(0), t) == want && gamepad_sample(g, classic_player(1), t) == want;
    }
    CHECK(same == n);
    // pad1: a player's gyro turned on in the controls changes nothing by itself (the window's pad layer sets gyro_use
    // only while driving with sensor data: tests/test_pad_motion.cpp), and mixing in a zero steering is the report too
    {
        std::mt19937 q(13);
        PlayerControls on = classic_player(0);
        on.gyro.mode = 1;
        on.gyro.axis = 1;
        on.gyro.sensitivity = 300;
        int eq = 0;
        for (int i = 0; i < n; ++i) {
            GamepadState g = random_state(q);
            const auto want = gamepad_sample(g);
            const bool a = gamepad_sample(g, on) == want;
            g.gyro_use = 1;
            g.gyro_steer = 0;
            eq += a && gamepad_sample(g, classic_player(0)) == want;
        }
        CHECK(eq == n);
    }
    // every button alone, and the extremes of each axis
    GamepadState g;
    CHECK(gamepad_sample(g, classic_player(0)) == gamepad_sample(g));
    CHECK(gamepad_sample(g) == neutral_sample());
}

TEST(default_controls_shape) {
    const Controls d = default_controls();
    CHECK(d.player[0].has_keys());
    CHECK(!d.player[1].has_keys());
    CHECK(d.player[0].pad == d.player[1].pad);
    // padr2: the stick clicks are L2 / R2; the right stick and L3 / R3 have no gamepad input by default (gamepad_sample's layout)
    CHECK(d.player[0].pad[std::size_t(Ds2::L2)] == std::vector<PadBind>{PadInput::LeftStick});
    CHECK(d.player[0].pad[std::size_t(Ds2::R2)] == std::vector<PadBind>{PadInput::RightStick});
    for (Ds2 a : {Ds2::L3, Ds2::R3, Ds2::RStickLeft, Ds2::RStickRight, Ds2::RStickUp, Ds2::RStickDown})
        CHECK(d.player[0].pad[std::size_t(a)].empty());
    // every action has a name and a label, parsed back
    for (Ds2 a : action_order()) {
        CHECK(parse_action(action_key(a)) == a);
        CHECK(std::string(action_label(a)).size() > 1);
    }
    CHECK(!parse_action("boost"));
    // the prompts: every button at its own position but L2 / R2, at the stick clicks' (L3 / R3)
    CHECK(pad_glyphs(d.player[0].pad)[std::size_t(Ds2::Cross)] == Ds2::Cross);
    for (int b = 0; b < 16; ++b)
        CHECK(pad_glyphs(d.player[0].pad)[std::size_t(b)] == (Ds2(b) == Ds2::L2 ? Ds2::L3 : Ds2(b) == Ds2::R2 ? Ds2::R3 : Ds2(b)));
}

// padr2: the default pad's stick clicks press L2 (music track skip) and R2 (Crashbreaker), digital bit and full
// pressure (as every button with a pressure byte), on the built-in sampler and the bindings alike; nothing else moves
TEST(default_stick_clicks_are_l2_r2) {
    for (int which = 0; which < 4; ++which) {
        GamepadState g;
        g.left_stick = which == 1 || which == 3;
        g.right_stick = which == 2 || which == 3;
        const auto built_in = gamepad_sample(g);
        for (const auto& s : {built_in, gamepad_sample(g, classic_player(0)), gamepad_sample(g, classic_player(1))}) {
            CHECK(s == built_in);
            CHECK(pressed(s, kL2) == g.left_stick && pressed(s, kR2) == g.right_stick);
            CHECK(s[16] == (g.left_stick ? 0xFF : 0) && s[17] == (g.right_stick ? 0xFF : 0));
            CHECK(!pressed(s, int(Ds2::L3)) && !pressed(s, int(Ds2::R3)));   // the PS2's own L3 / R3 stay unbound
            auto rest = s;   // the rest of the report is neutral
            rest[1] |= 0x03;
            rest[16] = rest[17] = 0;
            CHECK(rest == neutral_sample());
        }
    }
}

TEST(names_round_trip) {
    for (int s = 1; s < 512; ++s) {
        const std::string n = key_name(std::uint16_t(s));
        CHECK(n.find(',') == std::string::npos && n.find('@') == std::string::npos && n.find('#') == std::string::npos &&
              n.find(';') == std::string::npos && n.find('=') == std::string::npos);
        CHECK(parse_key(n) == std::uint16_t(s));
        CHECK(!key_label(std::uint16_t(s)).empty());
    }
    CHECK(parse_key("return") == sc::Return);
    CHECK(parse_key("LEFT SHIFT") == sc::LShift);
    CHECK(parse_key("sc9") == 9);
    CHECK(!parse_key("nokey"));
    CHECK(!parse_key("sc0"));
    CHECK(key_label(sc::Return) == "ENTER");
    CHECK(key_label(sc::Z) == "Z");
    for (int p = 1; p < int(PadInput::Count); ++p) {
        CHECK(parse_pad_input(pad_input_key(PadInput(p))) == PadInput(p));
        CHECK(pad_input_label(PadInput(p), PadStyle::Xbox) != "?");
    }
    CHECK(pad_input_label(PadInput::South, PadStyle::Xbox) == "A");
    CHECK(pad_input_label(PadInput::South, PadStyle::Nintendo) == "B");
    CHECK(pad_input_label(PadInput::South, PadStyle::PlayStation) == "CROSS");
    CHECK(!parse_pad_input("none"));
}

// ---- rebinding ----

TEST(rebind_keyboard_driving_view_keeps_menus) {
    Controls c = default_controls();
    // accelerate (Cross while driving) moved to K (14): the menus' Cross keys stay
    rebind(c, 0, Device::Keyboard, Ds2::Cross, key(14), KeyCtx::Drive, Resolve::Keep);
    std::vector<PhysInput> drive = bound(c, 0, Device::Keyboard, Ds2::Cross, KeyCtx::Drive);
    CHECK(drive.size() == 1 && drive[0] == key(14));
    const std::vector<PhysInput> menus = bound(c, 0, Device::Keyboard, Ds2::Cross, KeyCtx::Menu);
    CHECK(menus.size() == 4);   // Return, Keypad Enter, Z, Space
    KeyPad k(key_map(c.player[0]));
    CHECK(pressed(run(k, {14}, true, 1), kCross));
    CHECK(k.sample()[12] == 0xFF);
    k.key(14, false);
    CHECK(!pressed(run(k, {14}, false, 1), kCross));   // K is driving only
    k.key(14, false);
    CHECK(!pressed(run(k, {sc::W}, true, 1), kCross));   // W no longer accelerates ...
    CHECK(k.sample()[5] < 0x7F);                         // ... but still pushes the stick up (Aftertouch)
    k.key(sc::W, false);
    CHECK(pressed(run(k, {sc::Z}, false, 1), kCross));   // Z still confirms in menus
    k.key(sc::Z, false);
    CHECK(!pressed(run(k, {sc::Z}, true, 1), kCross));   // and no longer accelerates
    CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Cross, KeyCtx::Drive, PadStyle::Generic) == "K");
    // the same key bound in both views is one binding of both contexts
    rebind(c, 0, Device::Keyboard, Ds2::Start, key(19), KeyCtx::Drive, Resolve::Keep);
    rebind(c, 0, Device::Keyboard, Ds2::Start, key(19), KeyCtx::Menu, Resolve::Keep);
    CHECK(c.player[0].keys[std::size_t(Ds2::Start)].size() == 1);
    CHECK(c.player[0].keys[std::size_t(Ds2::Start)][0] == (KeyBind{19, KeyCtx::Any}));
}

TEST(rebind_add_and_remove_last) {
    Controls c = classic_controls();
    rebind(c, 0, Device::Keyboard, Ds2::Circle, key(15), KeyCtx::Drive, Resolve::Keep, true);   // L added for driving
    CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Circle, KeyCtx::Drive, PadStyle::Generic) == "X, H, L");
    CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Circle, KeyCtx::Menu, PadStyle::Generic) == "X, H");
    remove_last(c, 0, Device::Keyboard, Ds2::Circle, KeyCtx::Drive);
    CHECK(c.player[0] == classic_player(0));
    remove_last(c, 0, Device::Keyboard, Ds2::Circle, KeyCtx::Drive);   // H (both contexts) leaves driving only
    CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Circle, KeyCtx::Drive, PadStyle::Generic) == "X");
    CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Circle, KeyCtx::Menu, PadStyle::Generic) == "X, H");
    // gamepad: add, remove the last, reset
    rebind(c, 0, Device::Gamepad, Ds2::L2, pad(PadInput::LeftPaddle1), KeyCtx::Any, Resolve::Keep, true);
    CHECK(bound_text(c, 0, Device::Gamepad, Ds2::L2, KeyCtx::Any, PadStyle::Xbox) == "LS, PADDLE P3");
    remove_last(c, 0, Device::Gamepad, Ds2::L2, KeyCtx::Any);
    CHECK(c.player[0].pad == default_player(0).pad);
    reset(c, 0, Device::Keyboard);
    CHECK(c == classic_controls());
}

TEST(conflicts_swap_clear_keep_keyboard) {
    // X is Circle (both contexts); binding it to Cross in the driving view conflicts with Circle there
    const Controls d = default_controls();
    const std::vector<Use> u = uses(d, 0, Device::Keyboard, Ds2::Cross, key(sc::X), KeyCtx::Drive);
    CHECK(u.size() == 1 && u[0] == (Use{0, Ds2::Circle}));
    {   // swap: Circle takes Cross's first driving key (Up)
        Controls c = d;
        rebind(c, 0, Device::Keyboard, Ds2::Cross, key(sc::X), KeyCtx::Drive, Resolve::Swap);
        CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Circle, KeyCtx::Drive, PadStyle::Generic) == "H, UP");
        CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Circle, KeyCtx::Menu, PadStyle::Generic) == "X, H");
        CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Cross, KeyCtx::Drive, PadStyle::Generic) == "X");
        CHECK(uses(c, 0, Device::Keyboard, Ds2::Cross, key(sc::X), KeyCtx::Drive).empty());
    }
    {   // clear: Circle loses X while driving
        Controls c = d;
        rebind(c, 0, Device::Keyboard, Ds2::Cross, key(sc::X), KeyCtx::Drive, Resolve::Clear);
        CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Circle, KeyCtx::Drive, PadStyle::Generic) == "H");
        KeyPad k(key_map(c.player[0]));
        const auto s = run(k, {sc::X}, true, 1);
        CHECK(pressed(s, kCross) && !pressed(s, kCircle));
    }
    {   // keep: X drives both
        Controls c = d;
        rebind(c, 0, Device::Keyboard, Ds2::Cross, key(sc::X), KeyCtx::Drive, Resolve::Keep);
        KeyPad k(key_map(c.player[0]));
        const auto s = run(k, {sc::X}, true, 1);
        CHECK(pressed(s, kCross) && pressed(s, kCircle));
    }
    // a menus-only key does not conflict in the driving view (Up is D-pad up in menus, Cross while driving)
    CHECK(uses(d, 0, Device::Keyboard, Ds2::L1, key(sc::V), KeyCtx::Drive).empty());
    CHECK(uses(d, 0, Device::Keyboard, Ds2::L1, key(sc::V), KeyCtx::Menu).size() == 1);
    // the keyboard is shared: player 2's keys conflict with player 1's
    Controls c = d;
    rebind(c, 1, Device::Keyboard, Ds2::Cross, key(sc::Q), KeyCtx::Any, Resolve::Clear);
    CHECK(c.player[1].has_keys());
    CHECK(bound_text(c, 0, Device::Keyboard, Ds2::L1, KeyCtx::Drive, PadStyle::Generic) == "-");
}

TEST(conflicts_gamepad) {
    Controls c = classic_controls();
    const std::vector<Use> u = uses(c, 0, Device::Gamepad, Ds2::Cross, pad(PadInput::East), KeyCtx::Any);
    CHECK(u.size() == 1 && u[0] == (Use{0, Ds2::Circle}));
    CHECK(uses(c, 1, Device::Gamepad, Ds2::Cross, pad(PadInput::East), KeyCtx::Any).size() == 1);   // player 2's own
    rebind(c, 0, Device::Gamepad, Ds2::Cross, pad(PadInput::East), KeyCtx::Any, Resolve::Swap);   // Cross <-> Circle
    CHECK(c.player[0].pad[std::size_t(Ds2::Cross)] == std::vector<PadBind>{PadInput::East});
    CHECK(c.player[0].pad[std::size_t(Ds2::Circle)] == std::vector<PadBind>{PadInput::South});
    CHECK(c.player[1].pad == default_player(1).pad);   // player 2 untouched
    GamepadState g;
    g.east = true;
    auto s = gamepad_sample(g, c.player[0]);
    CHECK(pressed(s, kCross) && !pressed(s, kCircle) && s[12] == 0xFF && s[11] == 0);
    g = {};
    g.south = true;
    s = gamepad_sample(g, c.player[0]);
    CHECK(pressed(s, kCircle) && !pressed(s, kCross));
    // the prompts show the bound positions
    const auto glyphs = pad_glyphs(c.player[0].pad);
    CHECK(glyphs[std::size_t(Ds2::Cross)] == Ds2::Circle && glyphs[std::size_t(Ds2::Circle)] == Ds2::Cross);
    // the right trigger was Cross's second input: replaced, so it drives nothing now
    g = {};
    g.right_trigger = 32767;
    CHECK(gamepad_sample(g, c.player[0]) == neutral_sample());
}

TEST(analog_digital_conversions) {
    const GamepadTuning legacy{0.24f, 0.25f, false};   // 1.0.0's pedals (ANALOG PEDALS off)
    PlayerControls p = classic_player(0);
    // a button on a stick half axis: full deflection; a key likewise (the right stick, the keyboard)
    p.pad[std::size_t(Ds2::RStickLeft)] = {PadInput::North};
    p.pad[std::size_t(Ds2::RStickDown)] = {PadInput::LeftTrigger};
    GamepadState g;
    g.north = true;
    auto s = gamepad_sample(g, p, legacy);
    CHECK(s[2] == 0x00 && s[3] == 0x7F);
    CHECK(pressed(s, 12));   // North still Triangle too (both bound)
    // a trigger on a stick half axis: its travel
    g = {};
    g.left_trigger = 16384;
    s = gamepad_sample(g, p, legacy);
    CHECK(s[3] == stick_y_byte(16384.0f / 32767.0f));
    CHECK(s[3] > 0xBC && s[3] < 0xC2);
    CHECK(s[13] == std::uint8_t(std::lround(255.0f * 16384.0f / 32767.0f)));   // and Square's pressure (its default)
    // a trigger on a button keeps its pressure; pressed from the threshold
    p.pad[std::size_t(Ds2::L2)] = {PadInput::RightTrigger};
    g = {};
    g.right_trigger = 6000;   // below 25 %
    s = gamepad_sample(g, p, legacy);
    CHECK(!pressed(s, kL2) && s[16] == std::uint8_t(std::lround(255.0f * 6000.0f / 32767.0f)) && s[16] > 0);
    g.right_trigger = 20000;
    s = gamepad_sample(g, p, legacy);
    CHECK(pressed(s, kL2) && s[16] == std::uint8_t(std::lround(255.0f * 20000.0f / 32767.0f)));
    CHECK(pressed(s, kCross) && s[12] == s[16]);   // the trigger also stays Cross's
    // a stick half axis on a button: past the dead zone, rescaled, as pressure; pressed from the threshold
    p.pad[std::size_t(Ds2::Up)] = {PadInput::RightYNeg};
    g = {};
    g.right_y = -32768;
    s = gamepad_sample(g, p, legacy);
    CHECK(pressed(s, kUp) && s[8] == 0xFF);
    g.right_y = -6000;   // inside the 24 % dead zone
    s = gamepad_sample(g, p, legacy);
    CHECK(!pressed(s, kUp) && s[8] == 0);
    // two inputs on one stick half: the further one
    p.pad[std::size_t(Ds2::SteerLeft)] = {PadInput::LeftXNeg, PadInput::DpadLeft};
    g = {};
    g.left_x = -10000;
    g.dpad_left = true;
    s = gamepad_sample(g, p, legacy);
    CHECK(s[4] == 0x00);
    // the keyboard on the right stick: full at once
    PlayerControls k = default_player(0);
    k.keys[std::size_t(Ds2::RStickUp)] = {{sc::H, KeyCtx::Any}};
    KeyPad kp(key_map(k));
    s = run(kp, {sc::H}, true, 0);
    CHECK(s[3] == 0x00 && s[2] == 0x80);
    CHECK(pressed(s, kCircle));
}

// ---- controls.ini ----

TEST(ini_round_trip) {
    Controls d = default_controls();
    const std::string e = to_ini(d);
    CHECK(e.find('[') == std::string::npos);   // the defaults write no section
    CHECK(e.find("version = 3") != std::string::npos);
    std::vector<std::string> w;
    CHECK(controls_from_ini(e, &w) == d);
    CHECK(w.empty());
    Controls c = d;
    rebind(c, 0, Device::Keyboard, Ds2::Cross, key(14), KeyCtx::Drive, Resolve::Keep);
    rebind(c, 0, Device::Gamepad, Ds2::Cross, pad(PadInput::East), KeyCtx::Any, Resolve::Swap);
    rebind(c, 1, Device::Keyboard, Ds2::R1, key(sc::RShift), KeyCtx::Any, Resolve::Clear);
    rebind(c, 1, Device::Gamepad, Ds2::L3, pad(PadInput::RightPaddle1), KeyCtx::Any, Resolve::Keep);
    c.player[1].pad[std::size_t(Ds2::Start)].clear();   // unbound
    const std::string t = to_ini(c);
    CHECK(t.find("[player1.keyboard]") != std::string::npos && t.find("[player1.gamepad]") != std::string::npos);
    CHECK(t.find("[player2.keyboard]") != std::string::npos && t.find("[player2.gamepad]") != std::string::npos);
    CHECK(t.find("cross = Space@menus, Return@menus, Keypad Enter@menus, Z@menus, K@driving") != std::string::npos);
    CHECK(t.find("start = none") != std::string::npos);
    w.clear();
    CHECK(controls_from_ini(t, &w) == c);
    CHECK(w.empty());
    // the file
    const std::string path = "build/test_input_bindings_controls.ini";
    std::string why;
    if (save_controls(path, c, &why)) {
        Controls back;
        CHECK(load_controls(path, back));
        CHECK(back == c);
        std::remove(path.c_str());
    }
    Controls none;
    CHECK(!load_controls("build/no-such-dir/controls.ini", none));
    CHECK(none == d);
}

TEST(ini_invalid_entries) {
    std::vector<std::string> w;
    const Controls c = controls_from_ini(
        "version = 1\n"
        "cross = K\n"                                  // outside a section
        "[player3.keyboard]\ncross = K\n"              // unknown section: its lines skipped
        "[player1.keyboard]\n"
        "boost = K\n"                                  // unknown action
        "circle = NoSuchKey, L\n"                      // one bad key: the rest kept
        "square = NoSuchKey\n"                         // nothing valid: the default kept
        "triangle = C@sometimes, B@menus\n"            // a bad context
        "r1 = none\n"                                  // unbound
        "l1 =\n"                                       // unbound
        "start = Escape, escape\n"                     // duplicates dropped
        "[player1.gamepad]\n"
        "cross = south, warp_button\n"
        "square = jump\n"
        "junk line\n"
        "[player2.keyboard]\n"
        "cross = I # comment\n",
        &w);
    const PlayerControls& p = c.player[0];
    CHECK((p.keys[std::size_t(Ds2::Circle)] == std::vector<KeyBind>{{15, KeyCtx::Any}}));
    CHECK(p.keys[std::size_t(Ds2::Square)] == default_player(0).keys[std::size_t(Ds2::Square)]);
    CHECK((p.keys[std::size_t(Ds2::Triangle)] == std::vector<KeyBind>{{5, KeyCtx::Menu}}));
    CHECK(p.keys[std::size_t(Ds2::R1)].empty() && p.keys[std::size_t(Ds2::L1)].empty());
    CHECK(p.keys[std::size_t(Ds2::Start)].size() == 1);
    CHECK(p.keys[std::size_t(Ds2::Cross)] == default_player(0).keys[std::size_t(Ds2::Cross)]);
    CHECK(p.pad[std::size_t(Ds2::Cross)] == std::vector<PadBind>{PadInput::South});
    CHECK(p.pad[std::size_t(Ds2::Square)] == default_player(0).pad[std::size_t(Ds2::Square)]);
    CHECK((c.player[1].keys[std::size_t(Ds2::Cross)] == std::vector<KeyBind>{{12, KeyCtx::Any}}));
    int unknown_section = 0, unknown_action = 0, unknown_key = 0, context = 0, kept = 0, pad_bad = 0, outside = 0, not_kv = 0;
    for (const std::string& s : w) {
        unknown_section += s.find("unknown section") != std::string::npos;
        unknown_action += s.find("unknown action 'boost'") != std::string::npos;
        unknown_key += s.find("unknown key 'NoSuchKey'") != std::string::npos;
        context += s.find("unknown context") != std::string::npos;
        kept += s.find("the default kept") != std::string::npos;
        pad_bad += s.find("unknown gamepad input") != std::string::npos;
        outside += s.find("outside a [playerN") != std::string::npos;
        not_kv += s.find("not key = value") != std::string::npos;
    }
    CHECK(unknown_section == 1 && unknown_action == 1 && unknown_key == 2 && context == 1 && kept == 2 && pad_bad == 2 &&
          outside == 1 && not_kv == 1);
    // a newer file is read as far as understood, with a warning
    w.clear();
    const Controls n = controls_from_ini("version = 4\n[player1.gamepad]\nl2 = left_trigger\n", &w);
    CHECK(n.player[0].pad[std::size_t(Ds2::L2)] == std::vector<PadBind>{PadInput::LeftTrigger});
    CHECK(w.size() == 1 && w[0].find("newer") != std::string::npos);
}

// pad1: gyro steering in controls.ini ([playerN.gyro], only what differs from the defaults), its invalid values, reset
TEST(ini_gyro) {
    const Controls d = default_controls();
    CHECK(d.player[0].gyro == GyroControls{} && d.player[1].gyro == GyroControls{});
    CHECK(d.player[0].gyro.mode == 0 && d.player[0].gyro.recentre == PadInput::Touchpad && d.player[0].gyro.sensitivity == 100 &&
          d.player[0].gyro.dead_zone == 2 && d.player[0].gyro.axis == 0);
    CHECK(to_ini(d).find("gyro") == std::string::npos);
    Controls c = d;
    c.player[0].gyro.mode = 1;
    c.player[1].gyro = {2, 1, 175, 0, PadInput::None};
    const std::string t = to_ini(c);
    CHECK(t.find("[player1.gyro]\nmode = mix\n") != std::string::npos);
    CHECK(t.find("[player2.gyro]\nmode = replace\naxis = yaw\nsensitivity = 175\ndead_zone = 0\nrecentre = none\n") != std::string::npos);
    std::vector<std::string> w;
    CHECK(controls_from_ini(t, &w) == c);
    CHECK(w.empty());
    for (PadInput b : gyro_recentre_choices()) {
        Controls e = d;
        e.player[0].gyro.recentre = b;
        CHECK(controls_from_ini(to_ini(e), &w) == e);
    }
    CHECK(w.empty());
    // invalid values keep the defaults, each with a warning
    const Controls bad = controls_from_ini("version = 2\n[player1.gyro]\nmode = maybe\naxis = pitch\nsensitivity = 110\n"
                                           "sensitivity = 400\ndead_zone = -1\ndead_zone = 2x\nrecentre = south\nrecentre = left_x-\n"
                                           "speed = 3\n[player2.gyro]\nMODE = Mix\nrecentre = Back\n", &w);
    CHECK(bad.player[0].gyro == GyroControls{});
    CHECK(bad.player[1].gyro.mode == 1 && bad.player[1].gyro.recentre == PadInput::Back);
    CHECK(w.size() == 9);
    // the gamepad's reset turns the gyro off too; the keyboard's leaves it
    Controls r = c;
    reset(r, 1, Device::Keyboard);
    CHECK(r.player[1].gyro == c.player[1].gyro);
    reset(r, 1, Device::Gamepad);
    CHECK(r.player[1].gyro == GyroControls{} && r.player[0].gyro == c.player[0].gyro);
}

// padr2: a version 1 file (stick clicks unbound by default then) that binds a stick click to another action keeps it
// that action's only; a version 2 file, or one that sets L2 / R2 itself, is read as written
TEST(ini_version1_stick_clicks) {
    const Controls a = controls_from_ini("version = 1\n[player1.gamepad]\nl3 = left_stick\n[player2.gamepad]\ncross = south, right_stick\n");
    CHECK(a.player[0].pad[std::size_t(Ds2::L3)] == std::vector<PadBind>{PadInput::LeftStick});
    CHECK(a.player[0].pad[std::size_t(Ds2::L2)].empty());
    CHECK(a.player[0].pad[std::size_t(Ds2::R2)] == std::vector<PadBind>{PadInput::RightStick});   // not rebound: the new default
    CHECK(a.player[1].pad[std::size_t(Ds2::R2)].empty() && a.player[1].pad[std::size_t(Ds2::L2)] == std::vector<PadBind>{PadInput::LeftStick});
    GamepadState g;
    g.left_stick = true;
    CHECK(!pressed(gamepad_sample(g, a.player[0]), kL2) && pressed(gamepad_sample(g, a.player[0]), int(Ds2::L3)));
    const Controls nov = controls_from_ini("[player1.gamepad]\nl3 = left_stick\n");   // no version line: version 1
    CHECK(nov.player[0].pad[std::size_t(Ds2::L2)].empty());
    const Controls b = controls_from_ini("version = 2\n[player1.gamepad]\nl3 = left_stick\n");
    CHECK(b.player[0].pad[std::size_t(Ds2::L2)] == std::vector<PadBind>{PadInput::LeftStick});   // both (KEEP BOTH)
    const Controls k = controls_from_ini("version = 1\n[player1.gamepad]\nl3 = left_stick\nl2 = left_stick, left_paddle1\n");
    CHECK((k.player[0].pad[std::size_t(Ds2::L2)] == std::vector<PadBind>{PadInput::LeftStick, PadInput::LeftPaddle1}));
    // a file of the defaults reads back as them, whatever its version
    CHECK(controls_from_ini("version = 1\n") == default_controls());
    CHECK(controls_from_ini(to_ini(a)) == a);
}

// ---- the rebind flow ----

TEST(rebind_flow) {
    Controls c = classic_controls();
    RebindFlow f;
    CHECK(!f.active());
    f.begin(0, Device::Keyboard, Ds2::Cross, KeyCtx::Drive, false);
    CHECK(f.active() && f.state() == RebindFlow::State::Waiting);
    CHECK(f.message(PadStyle::Generic).find("PRESS A KEY FOR PLAYER 1 CROSS (WHILE DRIVING)") == 0);
    CHECK(f.input(c, pad(PadInput::South)) == RebindFlow::State::Waiting);   // a gamepad input: ignored
    CHECK(f.input(c, key(41)) == RebindFlow::State::Waiting);                 // Escape: reserved
    CHECK(key_reserved(41) && !key_reserved(68) && !key_reserved(62) && !key_reserved(224));   // only Escape (rebind2)
    CHECK(c == classic_controls());
    CHECK(f.input(c, key(14)) == RebindFlow::State::Done);                    // K: no other use
    CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Cross, KeyCtx::Drive, PadStyle::Generic) == "K");
    // timeout
    f.begin(0, Device::Keyboard, Ds2::Circle, KeyCtx::Drive, false);
    f.tick(RebindFlow::kTimeout - 0.5f);
    CHECK(f.state() == RebindFlow::State::Waiting);
    f.tick(1.0f);
    CHECK(f.state() == RebindFlow::State::Cancelled && !f.active());
    // cancel
    f.begin(0, Device::Gamepad, Ds2::Circle, KeyCtx::Drive, false);
    f.cancel();
    CHECK(f.state() == RebindFlow::State::Cancelled);
    // a conflict: the choice moves and is applied
    const Controls before = c;
    f.begin(0, Device::Gamepad, Ds2::Cross, KeyCtx::Any, false);
    CHECK(f.view() == KeyCtx::Any);
    CHECK(f.input(c, pad(PadInput::East)) == RebindFlow::State::Conflict);
    CHECK(c == before);   // nothing changed until chosen
    CHECK(f.conflicts().size() == 1);
    const std::string m = f.message(PadStyle::Xbox);
    CHECK(m.find("B ALSO DRIVES CIRCLE") == 0 && m.find("SWAP: THEY GET A") != std::string::npos && m.find("> SWAP <") != std::string::npos);
    f.step(1);
    CHECK(f.choice() == 1 && f.message(PadStyle::Xbox).find("> CLEAR <") != std::string::npos);
    f.step(1);
    f.step(1);
    CHECK(f.choice() == 0);
    f.step(-1);
    CHECK(f.choice() == 2);
    CHECK(f.choose(c) == RebindFlow::State::Done);   // keep both
    CHECK(c.player[0].pad[std::size_t(Ds2::Cross)] == std::vector<PadBind>{PadInput::East});
    CHECK(c.player[0].pad[std::size_t(Ds2::Circle)] == std::vector<PadBind>{PadInput::East});
    // a conflict cancelled: nothing changes
    const Controls mid = c;
    f.begin(0, Device::Gamepad, Ds2::Square, KeyCtx::Any, false);
    CHECK(f.input(c, pad(PadInput::East)) == RebindFlow::State::Conflict);
    f.cancel();
    CHECK(c == mid && f.state() == RebindFlow::State::Cancelled);
}

// ---- rebind2: the port actions ----

TEST(port_defaults_and_ini) {
    const Controls d = default_controls();
    CHECK(d.port == default_port());
    // the defaults are today's keys and combinations
    CHECK(d.port.keys[std::size_t(PortAction::Restart)] == std::vector<std::uint16_t>{62});
    CHECK(d.port.pad[std::size_t(PortAction::Restart)] == (std::vector<PadInput>{PadInput::Back, PadInput::Start}));
    CHECK(d.port.keys[std::size_t(PortAction::Screenshot)] == std::vector<std::uint16_t>{69});
    CHECK(d.port.keys[std::size_t(PortAction::PhotoMode)] == std::vector<std::uint16_t>{19});
    CHECK(d.port.pad[std::size_t(PortAction::PhotoMode)] == (std::vector<PadInput>{PadInput::LeftStick, PadInput::RightStick}));
    CHECK(to_ini(d).find("[port") == std::string::npos);   // nothing differs: nothing written
    for (int a = 0; a < kPortActions; ++a) {
        CHECK(parse_port_action(port_key(PortAction(a))) == PortAction(a));
        CHECK(std::string(port_label(PortAction(a))).size() > 3 && std::string(port_use(PortAction(a))).size() > 3);
    }
    CHECK(port_actions_of_key(d.port, 62) == (std::uint64_t(1) << int(PortAction::Restart)));
    CHECK(port_actions_of_key(d.port, 100) == 0);
    // a changed set round-trips, a chord with '+', "none" unbinds, odd keys by scancode
    Controls c = d;
    c.port.keys[std::size_t(PortAction::Screenshot)] = {226, 100, 70};
    c.port.pad[std::size_t(PortAction::Screenshot)] = {PadInput::Touchpad};
    c.port.pad[std::size_t(PortAction::Restart)] = {PadInput::Guide, PadInput::LeftShoulder, PadInput::RightShoulder};
    c.port.keys[std::size_t(PortAction::PhotoForward)].clear();
    const std::string ini = to_ini(c);
    CHECK(ini.find("[port.keyboard]") != std::string::npos && ini.find("screenshot = Left Alt, NonUSBackslash, PrintScreen") != std::string::npos);
    CHECK(ini.find("restart = guide+left_shoulder+right_shoulder") != std::string::npos && ini.find("photo_forward = none") != std::string::npos);
    std::vector<std::string> w;
    CHECK(controls_from_ini(ini, &w) == c && w.empty());
    // bad entries are reported and keep the default
    w.clear();
    const Controls bad = controls_from_ini("version = 2\n[port.keyboard]\nrestart = Nope\nbogus = F1\nscreenshot = F1, Nope\n[port.gamepad]\nfullscreen = zzz\n", &w);
    CHECK(w.size() == 6);
    CHECK(bad.port.keys[std::size_t(PortAction::Restart)] == d.port.keys[std::size_t(PortAction::Restart)]);
    CHECK(bad.port.keys[std::size_t(PortAction::Screenshot)] == std::vector<std::uint16_t>{58});
    CHECK(bad.port.pad[std::size_t(PortAction::Fullscreen)].empty());
}

TEST(fps_view_shortcut) {
    // fpsoverlay2: the FPS counter's view key: F3 by default, remappable like the other shortcuts, no gamepad chord
    const Controls d = default_controls();
    CHECK(d.port.keys[std::size_t(PortAction::FpsView)] == std::vector<std::uint16_t>{60});
    CHECK(d.port.pad[std::size_t(PortAction::FpsView)].empty() && !port_has_pad(PortAction::FpsView) && !port_photo(PortAction::FpsView));
    CHECK(parse_port_action("fps_view") == PortAction::FpsView && std::string(port_label(PortAction::FpsView)) == "FPS COUNTER VIEW");
    CHECK(port_actions_of_key(d.port, 60) == (std::uint64_t(1) << int(PortAction::FpsView)));
    // F3 is free: no game action of either player has it
    for (int pl = 0; pl < 2; ++pl)
        for (const KeyBinding& k : key_map(d.player[std::size_t(pl)])) CHECK(k.scancode != 60);
    Controls c = d;
    rebind_port(c, PortAction::FpsView, Device::Keyboard, PhysInput{Device::Keyboard, 61, PadInput::None}, Resolve::Swap);
    CHECK(c.port.keys[std::size_t(PortAction::FpsView)] == std::vector<std::uint16_t>{61});
    std::vector<std::string> w;
    CHECK(controls_from_ini(to_ini(c), &w) == c && w.empty() && to_ini(c).find("fps_view = F4") != std::string::npos);
    // F3 taken by another action: the rebind reports the conflict
    CHECK(!uses_port(d, PortAction::Screenshot, Device::Keyboard, PhysInput{Device::Keyboard, 60, PadInput::None}).empty());
}

TEST(hd_textures_shortcut) {
    // hdkey (issue #7): the HD TEXTURES key: F4 by default, keyboard only by default but a gamepad chord can be bound
    const Controls d = default_controls();
    constexpr PortAction a = PortAction::HdTextures;
    CHECK(d.port.keys[std::size_t(a)] == std::vector<std::uint16_t>{61});
    CHECK(d.port.pad[std::size_t(a)].empty() && port_has_pad(a) && !port_photo(a) && !port_analog(a));
    CHECK(parse_port_action("hd_textures") == a && std::string(port_label(a)) == "HD TEXTURES ON / OFF");
    CHECK(port_actions_of_key(d.port, 61) == (std::uint64_t(1) << int(a)));
    for (int pl = 0; pl < 2; ++pl)   // F4 is free: no game action of either player has it
        for (const KeyBinding& k : key_map(d.player[std::size_t(pl)])) CHECK(k.scancode != 61);
    Controls c = d;
    rebind_port(c, a, Device::Gamepad, PhysInput{Device::Gamepad, 0, PadInput::DpadUp}, Resolve::Swap);
    CHECK(c.port.pad[std::size_t(a)] == std::vector<PadInput>{PadInput::DpadUp});
    std::vector<std::string> w;
    CHECK(controls_from_ini(to_ini(c), &w) == c && w.empty() && to_ini(c).find("hd_textures") != std::string::npos);
}

TEST(port_chord_amount) {
    const Controls d = default_controls();
    GamepadState g;
    CHECK(port_pad_amount(g, d.port, PortAction::Restart, 0.24f) == 0.0f);
    g.back = true;
    CHECK(port_pad_amount(g, d.port, PortAction::Restart, 0.24f) == 0.0f);   // one of the two
    g.start = true;
    CHECK(port_pad_amount(g, d.port, PortAction::Restart, 0.24f) == 1.0f);
    CHECK(port_pad_amount(g, d.port, PortAction::Screenshot, 0.24f) == 0.0f);   // unbound on the pad
    g = GamepadState{};
    g.left_y = -32767;
    CHECK(port_pad_amount(g, d.port, PortAction::PhotoForward, 0.0f) > 0.99f);
    CHECK(port_pad_amount(g, d.port, PortAction::PhotoBackward, 0.0f) == 0.0f);
}

TEST(port_rebind_conflicts) {
    Controls c = default_controls();
    // F12 to FULLSCREEN: Screenshot has it
    CHECK(uses_port(c, PortAction::Fullscreen, Device::Keyboard, key(69)).size() == 1);
    rebind_port(c, PortAction::Fullscreen, Device::Keyboard, key(69), Resolve::Swap);   // Swap: Screenshot gets F11
    CHECK(c.port.keys[std::size_t(PortAction::Fullscreen)] == std::vector<std::uint16_t>{69});
    CHECK(c.port.keys[std::size_t(PortAction::Screenshot)] == std::vector<std::uint16_t>{68});
    rebind_port(c, PortAction::Screenshot, Device::Keyboard, key(69), Resolve::Clear);
    CHECK(c.port.keys[std::size_t(PortAction::Fullscreen)].empty());
    rebind_port(c, PortAction::Fullscreen, Device::Keyboard, key(69), Resolve::Keep);   // both
    CHECK(c.port.keys[std::size_t(PortAction::Fullscreen)] == std::vector<std::uint16_t>{69});
    CHECK(c.port.keys[std::size_t(PortAction::Screenshot)] == std::vector<std::uint16_t>{69});
    reset_port(c, PortAction::Fullscreen, Device::Keyboard);
    reset_port(c, PortAction::Screenshot, Device::Keyboard);
    CHECK(c == default_controls());
    // a global shortcut and a game key conflict (W drives CROSS while driving); photo mode's keys do not
    CHECK(!uses_port(c, PortAction::Screenshot, Device::Keyboard, key(26)).empty());
    CHECK(uses_port(c, PortAction::PhotoUp, Device::Keyboard, key(26)).size() == 1);   // only PHOTO: MOVE FORWARD
    CHECK(uses_port(c, PortAction::PhotoUp, Device::Keyboard, key(26))[0].port);
    // the other way: a game action taking F5 asks about INSTANT RESTART; Clear takes it from there
    const std::vector<Use> u = uses(c, 0, Device::Keyboard, Ds2::Cross, key(62), KeyCtx::Drive);
    CHECK(u.size() == 1 && u[0].port && u[0].port_action == PortAction::Restart);
    rebind(c, 0, Device::Keyboard, Ds2::Cross, key(62), KeyCtx::Drive, Resolve::Clear);
    CHECK(c.port.keys[std::size_t(PortAction::Restart)].empty());
    CHECK(bound(c, 0, Device::Keyboard, Ds2::Cross, KeyCtx::Drive).size() == 1);
    // any key can be taken: a modifier, one without a name
    RebindFlow f;
    f.begin_port(PortAction::BugReport, Device::Keyboard, false);
    CHECK(f.is_port() && f.message(PadStyle::Generic).find("PRESS A KEY FOR BUG REPORT.") == 0);
    CHECK(f.input(c, key(224)) == RebindFlow::State::Done);
    CHECK(c.port.keys[std::size_t(PortAction::BugReport)] == std::vector<std::uint16_t>{224});
    f.begin_port(PortAction::BugReport, Device::Keyboard, true);
    CHECK(f.input(c, key(100)) == RebindFlow::State::Done);
    CHECK(bound_port_text(c, PortAction::BugReport, Device::Keyboard, PadStyle::Generic) == "L CTRL, <>");
    // a flow with a conflict: message names the use, Keep both
    f.begin_port(PortAction::Fullscreen, Device::Keyboard, false);
    CHECK(f.input(c, key(69)) == RebindFlow::State::Conflict);   // F12 is SCREENSHOT's
    CHECK(f.message(PadStyle::Generic).find("F12 ALSO DRIVES SCREENSHOT.") == 0);
    CHECK(f.choose(c) == RebindFlow::State::Done);               // the choice at first: Swap
    CHECK(c.port.keys[std::size_t(PortAction::Screenshot)] == std::vector<std::uint16_t>{68});
}

TEST(port_chords_pad) {
    Controls c = default_controls();
    // replace a chord with one input, then add one more held with it
    rebind_port(c, PortAction::Restart, Device::Gamepad, pad(PadInput::Misc1), Resolve::Keep);
    CHECK(c.port.pad[std::size_t(PortAction::Restart)] == std::vector<PadInput>{PadInput::Misc1});
    rebind_port(c, PortAction::Restart, Device::Gamepad, pad(PadInput::RightPaddle1), Resolve::Keep, true);
    CHECK(bound_port_text(c, PortAction::Restart, Device::Gamepad, PadStyle::Generic) == "MISC + PADDLE R1");
    remove_last_port(c, PortAction::Restart, Device::Gamepad);
    CHECK(c.port.pad[std::size_t(PortAction::Restart)] == std::vector<PadInput>{PadInput::Misc1});
    // a single-input shortcut on a game button conflicts with the player's gamepad action
    const auto u = uses_port(c, PortAction::Screenshot, Device::Gamepad, pad(PadInput::West));
    CHECK(std::any_of(u.begin(), u.end(), [](const Use& x) { return !x.port; }));
    // the default chords never conflict (they share the game's Start / Select)
    CHECK(uses(default_controls(), 0, Device::Gamepad, Ds2::Cross, pad(PadInput::Start), KeyCtx::Any).size() == 1);   // START only
    reset_port(c, PortAction::Restart, Device::Gamepad);
    CHECK(c == default_controls());
}

TEST(reset_one_and_all) {
    Controls c = default_controls();
    rebind(c, 0, Device::Keyboard, Ds2::Cross, key(14), KeyCtx::Drive, Resolve::Keep);   // K while driving
    rebind(c, 0, Device::Keyboard, Ds2::Cross, key(15), KeyCtx::Menu, Resolve::Keep);    // L in menus
    CHECK(!(c == default_controls()));
    reset_action(c, 0, Device::Keyboard, Ds2::Cross, KeyCtx::Drive);                     // the driving view only
    CHECK(bound(c, 0, Device::Keyboard, Ds2::Cross, KeyCtx::Drive) == bound(default_controls(), 0, Device::Keyboard, Ds2::Cross, KeyCtx::Drive));
    CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Cross, KeyCtx::Menu, PadStyle::Generic) == "L");   // the menus' stayed
    reset_action(c, 0, Device::Keyboard, Ds2::Cross, KeyCtx::Menu);
    CHECK(c == default_controls());
    // padlayout: a gamepad edit makes the shown layout (Auto: the Xbox one without a pad) the player's Custom bindings;
    // back to the defaults one by one gives that layout's bindings again (still Custom: the player's own)
    rebind(c, 1, Device::Gamepad, Ds2::Cross, pad(PadInput::East), KeyCtx::Any, Resolve::Clear);
    CHECK(c.player[1].layout == PadLayout::Custom);
    for (Ds2 a : action_order()) reset_action(c, 1, Device::Gamepad, a, KeyCtx::Any);
    CHECK(c.player[1].pad == layout_bindings(PadLayout::Xbox) && c.player[1].layout == PadLayout::Custom);
    reset(c, 1, Device::Gamepad);
    CHECK(c == default_controls());
    c.port.keys[0].clear();
    c.player[1].gyro.mode = 1;
    reset_all(c);
    CHECK(c == default_controls());
}

TEST(analog_pedals_with_the_bindings) {
    // 1.0.1 ANALOG PEDALS on the bindings path: the default player's triggers (Cross / Square's analog inputs) drive the
    // right stick; a button bound to the right stick still adds its full deflection (the larger wins)
    PlayerControls p = classic_player(0);
    GamepadState g;
    g.right_trigger = 16384;
    auto s = gamepad_sample(g, p);
    CHECK(!pressed(s, kCross) && s[12] == 0 && s[3] == stick_y_byte(-pedal_stick(16384.0f / 32767.0f)));
    g = {};
    g.right_trigger = 32767;
    g.left_trigger = 32767;   // both: the buttons, as 1.0.0
    s = gamepad_sample(g, p);
    CHECK(pressed(s, kCross) && pressed(s, kSquare) && s[3] == stick_y_byte(0.0f));
    p.pad[std::size_t(Ds2::RStickUp)] = {PadInput::North};
    g = {};
    g.north = true;
    g.right_trigger = 4000;   // a light pedal under a full stick: the stick's full deflection
    s = gamepad_sample(g, p);
    CHECK(s[3] == stick_y_byte(-1.0f));
}

// ---- padlayout (1.2.1): CONTROLLER LAYOUT ----

namespace {
// SDL_GamepadType's values (SDL3: UNKNOWN 0, STANDARD 1, XBOX360 2, XBOXONE 3, PS3 4, PS4 5, PS5 6, SWITCH_PRO 7,
// JOYCON_LEFT 8, JOYCON_RIGHT 9, JOYCON_PAIR 10, GAMECUBE 11; nativepad.cpp static_asserts the header's constants)
constexpr int kUnknown = 0, kStandard = 1, kX360 = 2, kXOne = 3, kPs3 = 4, kPs4 = 5, kPs5 = 6, kSwitchPro = 7, kJoyL = 8,
              kJoyR = 9, kJoyPair = 10, kGameCube = 11;
constexpr int kL1 = 10, kR1 = 11, kTriangle = 12, kSelect = 0, kStart = 3;
// the buttons pressed in a report (bits of the 16)
unsigned held(const std::array<std::uint8_t, 32>& s) { return ~(unsigned(s[0]) | (unsigned(s[1]) << 8)) & 0xFFFFu; }
unsigned bits(std::initializer_list<int> b) {
    unsigned m = 0;
    for (int x : b) m |= 1u << x;
    return m;
}
}  // namespace

TEST(layout_selection_by_pad_type) {
    CHECK(pad_family(kPs3) == PadStyle::PlayStation && pad_family(kPs4) == PadStyle::PlayStation && pad_family(kPs5) == PadStyle::PlayStation);
    CHECK(pad_family(kX360) == PadStyle::Xbox && pad_family(kXOne) == PadStyle::Xbox);
    for (int t : {kSwitchPro, kJoyL, kJoyR, kJoyPair, kGameCube}) CHECK(pad_family(t) == PadStyle::Nintendo);
    for (int t : {kUnknown, kStandard, 12, 99, -1}) CHECK(pad_family(t) == PadStyle::Generic);
    // Auto: PlayStation pads the PlayStation layout, every other pad the Xbox one; an explicit choice whatever the pad
    for (int t = -1; t <= 12; ++t) {
        const PadStyle f = pad_family(t);
        CHECK(resolved_layout(PadLayout::Auto, f) == (f == PadStyle::PlayStation ? PadLayout::PlayStation : PadLayout::Xbox));
        CHECK(resolved_layout(PadLayout::Xbox, f) == PadLayout::Xbox);
        CHECK(resolved_layout(PadLayout::PlayStation, f) == PadLayout::PlayStation);
        CHECK(resolved_layout(PadLayout::Custom, f) == PadLayout::Custom);
    }
    // the defaults: Auto for both players, the Custom bindings the 1.x layout (the report of before for a Custom player)
    const Controls d = default_controls();
    CHECK(d.player[0].layout == PadLayout::Auto && d.player[1].layout == PadLayout::Auto);
    CHECK(&pad_bindings(d.player[0], PadStyle::PlayStation) == &layout_bindings(PadLayout::PlayStation));
    CHECK(&pad_bindings(d.player[0], PadStyle::Xbox) == &layout_bindings(PadLayout::Xbox));
    CHECK(&pad_bindings(d.player[0], PadStyle::Nintendo) == &layout_bindings(PadLayout::Xbox));
    CHECK(&pad_bindings(d.player[0], PadStyle::Generic) == &layout_bindings(PadLayout::Xbox));
    CHECK(pad_bindings(classic_player(0), PadStyle::PlayStation) == default_player(0).pad);
    // names: the ini's and the pages'
    for (PadLayout l : {PadLayout::Auto, PadLayout::Xbox, PadLayout::PlayStation, PadLayout::Custom}) CHECK(parse_pad_layout(pad_layout_key(l)) == l);
    CHECK(parse_pad_layout(" PlayStation ") == PadLayout::PlayStation && !parse_pad_layout("nintendo"));
    PlayerControls p = d.player[0];
    CHECK(layout_text(p, PadStyle::Xbox, true) == "Auto: Xbox controller");
    CHECK(layout_text(p, PadStyle::PlayStation, true) == "Auto: PlayStation controller");
    CHECK(layout_text(p, PadStyle::Nintendo, true) == "Auto: Nintendo, Xbox layout");
    CHECK(layout_text(p, PadStyle::Generic, false) == "Auto: no controller, Xbox layout");
    CHECK(layout_text(p, PadStyle::Generic, true) == "Auto: controller, Xbox layout");
    p.layout = PadLayout::PlayStation;
    CHECK(layout_text(p, PadStyle::Xbox, true) == "PlayStation");
    p.layout = PadLayout::Custom;
    CHECK(layout_text(p, PadStyle::Xbox, true) == "Custom");
}

// The Xbox layout: the original Xbox game's while driving, A / B confirm / back in menus; the triggers the pedals in both
TEST(xbox_layout_both_contexts) {
    const PadBindings& x = layout_bindings(PadLayout::Xbox);
    const GamepadTuning t;   // ANALOG TRIGGERS on (the default)
    const auto one = [&](auto set, KeyCtx now) {
        GamepadState g;
        set(g);
        return gamepad_sample(g, x, t, now);
    };
    struct Row { void (*set)(GamepadState&); unsigned drive, menus; };
    const Row rows[] = {
        {[](GamepadState& g) { g.south = true; }, bits({kR1}), bits({kCross})},             // A: boost / confirm
        {[](GamepadState& g) { g.east = true; }, bits({kR2}), bits({kTriangle})},           // B: Crashbreaker / back
        {[](GamepadState& g) { g.west = true; }, bits({kL1}), bits({kSquare})},             // X: look back / Square
        {[](GamepadState& g) { g.north = true; }, bits({kTriangle}), bits({kCircle})},      // Y: camera / Circle
        {[](GamepadState& g) { g.back = true; }, bits({kL2}), bits({kSelect})},             // View: music / Select
        {[](GamepadState& g) { g.left_stick = true; }, bits({kCircle}), 0},                 // LS: the horn
        {[](GamepadState& g) { g.right_stick = true; }, 0, 0},
        {[](GamepadState& g) { g.left_shoulder = true; }, bits({kL1}), bits({kL1})},        // LB / RB: L1 / R1 in both
        {[](GamepadState& g) { g.right_shoulder = true; }, bits({kR1}), bits({kR1})},
        {[](GamepadState& g) { g.start = true; }, bits({kStart}), bits({kStart})},
        {[](GamepadState& g) { g.dpad_up = true; }, bits({kUp}), bits({kUp})},
        {[](GamepadState& g) { g.right_trigger = 32767; g.left_trigger = 32767; }, bits({kCross, kSquare}), bits({kCross, kSquare})},
    };
    for (const Row& r : rows) {
        CHECK(held(one(r.set, KeyCtx::Drive)) == r.drive);
        CHECK(held(one(r.set, KeyCtx::Menu)) == r.menus);
    }
    // a face button's pressure is full in its context, 0 in the other's (R1 byte 15, Cross byte 12)
    CHECK(one(rows[0].set, KeyCtx::Drive)[15] == 0xFF && one(rows[0].set, KeyCtx::Drive)[12] == 0);
    CHECK(one(rows[0].set, KeyCtx::Menu)[12] == 0xFF && one(rows[0].set, KeyCtx::Menu)[15] == 0);
    // RT alone: the analog throttle (1.1's pedals: the right stick, not the button) in both contexts (the countdown
    // is the menus' context); LT the brake
    for (KeyCtx now : {KeyCtx::Drive, KeyCtx::Menu}) {
        GamepadState g;
        g.right_trigger = 16384;
        auto s = gamepad_sample(g, x, t, now);
        CHECK(!pressed(s, kCross) && s[3] == stick_y_byte(-pedal_stick(16384.0f / 32767.0f)));
        g = {};
        g.left_trigger = 32767;
        s = gamepad_sample(g, x, t, now);
        CHECK(!pressed(s, kSquare) && s[3] == stick_y_byte(pedal_stick(1.0f)));
        // ANALOG TRIGGERS off: the buttons with the trigger's pressure, as 1.0.0
        g = {};
        g.right_trigger = 32767;
        s = gamepad_sample(g, x, GamepadTuning{0.24f, 0.25f, false}, now);
        CHECK(pressed(s, kCross) && s[12] == 0xFF && s[3] == 0x7F);
    }
    // in a crash both work at once: A (R1, Impact Time / Aftertouch) and B (R2, the Crashbreaker), steering on the stick
    GamepadState g;
    g.south = g.east = true;
    g.left_x = -32768;
    const auto s = gamepad_sample(g, x, t, KeyCtx::Drive);
    CHECK(held(s) == bits({kR1, kR2}) && s[4] == 0x00 && s[15] == 0xFF && s[17] == 0xFF);
    // the adaptive triggers' roles: LT the brake, RT the accelerator
    CHECK(trigger_role(x, false) == 1 && trigger_role(x, true) == 2);
    // the prompts: A / B confirm / back in menus; A boost, B Crashbreaker, X look back, RT accelerate while driving
    const auto m = pad_glyphs(x, KeyCtx::Menu), d = pad_glyphs(x, KeyCtx::Drive);
    CHECK(m[std::size_t(Ds2::Cross)] == Ds2::Cross && m[std::size_t(Ds2::Triangle)] == Ds2::Circle && m[std::size_t(Ds2::Square)] == Ds2::Square);
    CHECK(d[std::size_t(Ds2::R1)] == Ds2::Cross && d[std::size_t(Ds2::R2)] == Ds2::Circle && d[std::size_t(Ds2::L1)] == Ds2::Square);
    CHECK(d[std::size_t(Ds2::Cross)] == Ds2::R2 && d[std::size_t(Ds2::Square)] == Ds2::L2 && d[std::size_t(Ds2::Triangle)] == Ds2::Triangle);
    CHECK(d[std::size_t(Ds2::L2)] == Ds2::Select && d[std::size_t(Ds2::Circle)] == Ds2::L3);
    // the pages' text per view
    Controls c = default_controls();
    set_page_family(c, 0, PadStyle::Xbox);
    CHECK(bound_text(c, 0, Device::Gamepad, Ds2::R1, KeyCtx::Drive, PadStyle::Xbox) == "A, RB");
    CHECK(bound_text(c, 0, Device::Gamepad, Ds2::R1, KeyCtx::Menu, PadStyle::Xbox) == "RB");
    CHECK(bound_text(c, 0, Device::Gamepad, Ds2::Cross, KeyCtx::Menu, PadStyle::Xbox) == "RT, A");
    CHECK(bound_text(c, 0, Device::Gamepad, Ds2::Cross, KeyCtx::Drive, PadStyle::Xbox) == "RT");
    CHECK(bound_text(c, 0, Device::Gamepad, Ds2::Triangle, KeyCtx::Menu, PadStyle::Xbox) == "B");
}

// The PlayStation layout: the PS2's, with the triggers as the pedals (Cross / R2 accelerate, Square / L2 brake), the
// Crashbreaker on R3 and the music on L3; the same in both contexts; the right stick the game's
TEST(playstation_layout_both_contexts) {
    const PadBindings& ps = layout_bindings(PadLayout::PlayStation);
    const GamepadTuning t;
    std::mt19937 r(29);
    int same = 0;
    for (int i = 0; i < 20000; ++i) {
        const GamepadState g = random_state(r);
        same += gamepad_sample(g, ps, t, KeyCtx::Drive) == gamepad_sample(g, ps, t, KeyCtx::Menu);
    }
    CHECK(same == 20000);
    struct Row { void (*set)(GamepadState&); unsigned want; };
    const Row rows[] = {
        {[](GamepadState& g) { g.south = true; }, bits({kCross})},
        {[](GamepadState& g) { g.east = true; }, bits({kCircle})},
        {[](GamepadState& g) { g.west = true; }, bits({kSquare})},
        {[](GamepadState& g) { g.north = true; }, bits({kTriangle})},
        {[](GamepadState& g) { g.left_shoulder = true; }, bits({kL1})},
        {[](GamepadState& g) { g.right_shoulder = true; }, bits({kR1})},
        {[](GamepadState& g) { g.left_stick = true; }, bits({kL2})},    // L3: the music track skip
        {[](GamepadState& g) { g.right_stick = true; }, bits({kR2})},   // R3: the Crashbreaker
        {[](GamepadState& g) { g.back = true; }, bits({kSelect})},
        {[](GamepadState& g) { g.start = true; }, bits({kStart})},
        {[](GamepadState& g) { g.right_trigger = 32767; g.left_trigger = 32767; }, bits({kCross, kSquare})},   // a powerslide
    };
    for (const Row& w : rows) {
        GamepadState g;
        w.set(g);
        const auto s = gamepad_sample(g, ps, t, KeyCtx::Drive);
        CHECK(held(s) == w.want);
        CHECK(s[2] == 0x80 && s[3] == 0x7F);
    }
    // R2 / L2: the analog pedals (1.1's ANALOG TRIGGERS: the right stick, not the button), in both contexts
    for (KeyCtx now : {KeyCtx::Drive, KeyCtx::Menu}) {
        GamepadState g;
        g.right_trigger = 16384;
        auto s = gamepad_sample(g, ps, t, now);
        CHECK(held(s) == 0 && s[3] == stick_y_byte(-pedal_stick(16384.0f / 32767.0f)));
        g = {};
        g.left_trigger = 32767;
        s = gamepad_sample(g, ps, t, now);
        CHECK(held(s) == 0 && s[3] == stick_y_byte(pedal_stick(1.0f)));
    }
    // the right stick: up accelerates, down brakes (the game's own right stick: control 20 / 21)
    GamepadState g;
    g.right_y = -32768;
    auto s = gamepad_sample(g, ps, t);
    CHECK(s[3] == 0x00 && held(s) == 0);
    g.right_y = 32767;
    g.right_x = -32768;
    s = gamepad_sample(g, ps, t);
    CHECK(s[3] == 0xFF && s[2] == 0x00);
    // in a crash: R1 (Impact Time, Aftertouch) and R3 (the Crashbreaker) together
    g = {};
    g.right_shoulder = g.right_stick = true;
    s = gamepad_sample(g, ps, t);
    CHECK(held(s) == bits({kR1, kR2}) && s[15] == 0xFF && s[17] == 0xFF);
    CHECK(trigger_role(ps, false) == 1 && trigger_role(ps, true) == 2);   // the adaptive triggers: L2 brake, R2 throttle
    // the prompts: every button its own but L2 / R2, at the stick clicks (L3 / R3)
    for (KeyCtx now : {KeyCtx::Menu, KeyCtx::Drive})
        for (int b = 0; b < 16; ++b)
            CHECK(pad_glyphs(ps, now)[std::size_t(b)] == (Ds2(b) == Ds2::L2 ? Ds2::L3 : Ds2(b) == Ds2::R2 ? Ds2::R3 : Ds2(b)));
    // the 1.x layout (Custom's starting bindings) with the right stick added: the rest the same
    PadBindings x = default_player(0).pad;
    for (Ds2 a : {Ds2::RStickLeft, Ds2::RStickRight, Ds2::RStickUp, Ds2::RStickDown}) x[std::size_t(a)] = ps[std::size_t(a)];
    CHECK(x == ps);
}

// Migration: a controls.ini from before 1.2.1 with a gamepad remap is Custom and stays exactly as it was; one with the
// 1.x pad defaults (keyboard or gyro changes only, or no gamepad lines) is Auto. The keyboard is not touched.
TEST(layout_migration) {
    // the 1.x defaults: Auto
    for (const char* f : {"", "version = 2\n", "version = 2\n[player1.keyboard]\ncross = K\n", "version = 2\n[player1.gyro]\nmode = mix\n",
                          "version = 2\n[player1.gamepad]\ncross = south, right_trigger\n", "[player2.keyboard]\ncross = L\n"}) {
        std::vector<std::string> w;
        const Controls c = controls_from_ini(f, &w);
        CHECK(c.player[0].layout == PadLayout::Auto && c.player[1].layout == PadLayout::Auto);
        CHECK(c.player[0].pad == default_player(0).pad);
    }
    {   // a remap: Custom, the bindings exactly the file's; the other player Auto; the keyboard as the file says
        const char* f = "version = 2\n[player1.keyboard]\ncross = K\n[player1.gamepad]\ncross = east\ncircle = south\n";
        const Controls c = controls_from_ini(f);
        CHECK(c.player[0].layout == PadLayout::Custom && c.player[1].layout == PadLayout::Auto);
        CHECK(c.player[0].pad[std::size_t(Ds2::Cross)] == std::vector<PadBind>{PadInput::East});
        CHECK(c.player[0].pad[std::size_t(Ds2::Circle)] == std::vector<PadBind>{PadInput::South});
        CHECK(bound_text(c, 0, Device::Keyboard, Ds2::Cross, KeyCtx::Drive, PadStyle::Generic) == "K");
        PlayerControls want = classic_player(0);
        want.keys = c.player[0].keys;
        want.pad[std::size_t(Ds2::Cross)] = {PadInput::East};
        want.pad[std::size_t(Ds2::Circle)] = {PadInput::South};
        CHECK(c.player[0] == want);
        // whatever the pad: the remap (a PlayStation pad and an Xbox one alike)
        GamepadState g;
        g.east = true;
        CHECK(pressed(gamepad_sample(g, c.player[0], {}, KeyCtx::Drive, PadStyle::PlayStation), kCross));
        CHECK(pressed(gamepad_sample(g, c.player[0], {}, KeyCtx::Menu, PadStyle::Xbox), kCross));
        // written back: an explicit layout line (version 3); read back the same
        const std::string e = to_ini(c);
        CHECK(e.find("version = 3") != std::string::npos && e.find("layout = custom") != std::string::npos);
        CHECK(controls_from_ini(e) == c);
    }
    {   // a version 1 file whose stick click moved (padr2's migration first): Custom
        const Controls c = controls_from_ini("[player2.gamepad]\nl3 = left_stick\n");
        CHECK(c.player[1].layout == PadLayout::Custom && c.player[0].layout == PadLayout::Auto);
        CHECK(c.player[1].pad[std::size_t(Ds2::L2)].empty());
    }
    // the defaults write no layout line; a layout line wins over the inference
    CHECK(to_ini(default_controls()).find("\nlayout") == std::string::npos);
    CHECK(controls_from_ini("version = 3\n[player1.gamepad]\nlayout = auto\ncross = east\n").player[0].layout == PadLayout::Auto);
    CHECK(controls_from_ini("version = 3\n[player1.gamepad]\nlayout = custom\n").player[0].layout == PadLayout::Custom);
    std::vector<std::string> w;
    const Controls bad = controls_from_ini("version = 3\n[player1.gamepad]\nlayout = nintendo\n", &w);
    CHECK(bad.player[0].layout == PadLayout::Auto && w.size() == 1 && w[0].find("layout") != std::string::npos);
}

// An explicit choice sets the layout (the Custom bindings kept for later); editing a gamepad binding makes the shown
// layout the player's own, changed, and Custom
TEST(layout_choice_then_edit_is_custom) {
    Controls c = controls_from_ini("version = 2\n[player1.gamepad]\ncross = east\ncircle = south\n");   // a 1.x remap
    const PadBindings mine = c.player[0].pad;
    set_page_family(c, 0, PadStyle::PlayStation);
    set_layout(c, 0, PadLayout::Xbox);
    CHECK(c.player[0].layout == PadLayout::Xbox && c.player[0].pad == mine);   // the remap kept
    CHECK(pad_bindings(c.player[0], PadStyle::PlayStation) == layout_bindings(PadLayout::Xbox));
    CHECK(bound_text(c, 0, Device::Gamepad, Ds2::R2, KeyCtx::Drive, PadStyle::Xbox) == "B");
    // round trip of every choice
    for (PadLayout l : {PadLayout::Auto, PadLayout::Xbox, PadLayout::PlayStation, PadLayout::Custom}) {
        Controls k = c;
        set_layout(k, 0, l);
        CHECK(controls_from_ini(to_ini(k)) == k);
    }
    set_layout(c, 0, PadLayout::Custom);
    CHECK(c.player[0].pad == mine);   // back to the remap
    // Xbox, then an edit in the driving view: Custom, the Xbox layout with that change
    set_layout(c, 0, PadLayout::Xbox);
    rebind(c, 0, Device::Gamepad, Ds2::R2, pad(PadInput::RightStick), KeyCtx::Drive, Resolve::Keep);   // Crashbreaker on RS
    CHECK(c.player[0].layout == PadLayout::Custom);
    PadBindings want = layout_bindings(PadLayout::Xbox);
    want[std::size_t(Ds2::R2)] = {PadBind{PadInput::RightStick, KeyCtx::Drive}};
    CHECK(c.player[0].pad == want);
    GamepadState g;
    g.right_stick = true;
    CHECK(held(gamepad_sample(g, c.player[0], {}, KeyCtx::Drive, PadStyle::PlayStation)) == bits({kR2}));
    CHECK(held(gamepad_sample(g, c.player[0], {}, KeyCtx::Menu, PadStyle::PlayStation)) == 0);
    {   // written with the contexts, read back the same
        const std::string e = to_ini(c);
        CHECK(e.find("r2 = right_stick@driving") != std::string::npos && e.find("r1 = south@driving, right_shoulder") != std::string::npos);
        CHECK(controls_from_ini(e) == c);
    }
    // Auto on a PlayStation pad, then a conflict swap in the menus view: Cross <-> Triangle in menus only
    Controls a = default_controls();
    set_page_family(a, 0, PadStyle::PlayStation);
    CHECK((uses(a, 0, Device::Gamepad, Ds2::Cross, pad(PadInput::North), KeyCtx::Menu) == std::vector<Use>{Use{0, Ds2::Triangle}}));
    rebind(a, 0, Device::Gamepad, Ds2::Cross, pad(PadInput::North), KeyCtx::Menu, Resolve::Swap);
    CHECK(a.player[0].layout == PadLayout::Custom);
    CHECK(bound_text(a, 0, Device::Gamepad, Ds2::Cross, KeyCtx::Menu, PadStyle::PlayStation) == "TRIANGLE");
    CHECK(bound_text(a, 0, Device::Gamepad, Ds2::Cross, KeyCtx::Drive, PadStyle::PlayStation) == "CROSS, R2");
    CHECK(bound_text(a, 0, Device::Gamepad, Ds2::Triangle, KeyCtx::Menu, PadStyle::PlayStation) == "CROSS");
    // nothing changed (a removal of nothing, a reset of what is already the default): the layout stays
    Controls b = default_controls();
    set_page_family(b, 0, PadStyle::Xbox);
    remove_last(b, 0, Device::Gamepad, Ds2::L3, KeyCtx::Any);
    reset_action(b, 0, Device::Gamepad, Ds2::Cross, KeyCtx::Drive);
    CHECK(b == default_controls());
    // the rebind flow in a gamepad view: the message names it; cancelled, nothing changes
    RebindFlow f;
    f.begin(0, Device::Gamepad, Ds2::R1, KeyCtx::Drive, false);
    CHECK(f.view() == KeyCtx::Drive && f.message(PadStyle::Xbox).find("FOR PLAYER 1 R1 (WHILE DRIVING)") != std::string::npos);
    CHECK(f.input(b, pad(PadInput::East)) == RebindFlow::State::Conflict);   // B is R2 while driving
    CHECK((f.conflicts() == std::vector<Use>{Use{0, Ds2::R2}}));
    f.cancel();
    CHECK(b == default_controls());
    // reset: the device back to Auto (and the 1.x Custom bindings)
    reset(c, 0, Device::Gamepad);
    CHECK(c.player[0] == default_controls().player[0]);
}

// Two players, two pads: each player's layout resolved by that player's pad
TEST(layout_two_players_two_pads) {
    const Controls c = default_controls();
    GamepadState g;
    g.south = true;
    // player 1 on a DualSense (PlayStation layout), player 2 on an Xbox pad
    const auto p1 = gamepad_sample(g, c.player[0], {}, KeyCtx::Drive, pad_family(kPs5));
    const auto p2 = gamepad_sample(g, c.player[1], {}, KeyCtx::Drive, pad_family(kXOne));
    CHECK(held(p1) == bits({kCross}) && held(p2) == bits({kR1}));
    // in menus both confirm with the bottom button
    CHECK(held(gamepad_sample(g, c.player[0], {}, KeyCtx::Menu, pad_family(kPs5))) == bits({kCross}));
    CHECK(held(gamepad_sample(g, c.player[1], {}, KeyCtx::Menu, pad_family(kXOne))) == bits({kCross}));
    // a Switch Pro (Nintendo: by position) is the Xbox layout: its bottom button (B) boosts and confirms, its right
    // one (A) is the Crashbreaker and goes back
    CHECK(held(gamepad_sample(g, c.player[1], {}, KeyCtx::Drive, pad_family(kSwitchPro))) == bits({kR1}));
    g = {};
    g.east = true;
    CHECK(held(gamepad_sample(g, c.player[1], {}, KeyCtx::Menu, pad_family(kSwitchPro))) == bits({kTriangle}));
    // player 2 explicitly PlayStation on the Xbox pad, player 1 Custom (the 1.x layout): each their own
    Controls k = c;
    set_layout(k, 1, PadLayout::PlayStation);
    set_layout(k, 0, PadLayout::Custom);
    g = {};
    g.right_stick = true;
    CHECK(held(gamepad_sample(g, k.player[1], {}, KeyCtx::Drive, pad_family(kXOne))) == bits({kR2}));   // R3: the Crashbreaker
    CHECK(held(gamepad_sample(g, k.player[0], {}, KeyCtx::Drive, pad_family(kXOne))) == bits({kR2}));   // 1.x: RS the Crashbreaker
    g = {};
    g.east = true;   // B / Circle: the Crashbreaker on the Xbox layout (player 1 here: 1.x, Circle), the horn on the PlayStation one
    CHECK(held(gamepad_sample(g, k.player[1], {}, KeyCtx::Drive, pad_family(kXOne))) == bits({kCircle}));
    CHECK(held(gamepad_sample(g, c.player[1], {}, KeyCtx::Drive, pad_family(kXOne))) == bits({kR2}));
    // an edit of player 2's pad leaves player 1's alone
    set_page_family(k, 1, PadStyle::Xbox);
    rebind(k, 1, Device::Gamepad, Ds2::Cross, pad(PadInput::LeftPaddle1), KeyCtx::Any, Resolve::Keep, true);
    CHECK(k.player[1].layout == PadLayout::Custom && k.player[0].layout == PadLayout::Custom && k.player[0].pad == default_player(0).pad);
}

TEST_MAIN("test_input_bindings: control remapping (src/native/input_bindings.h)")
