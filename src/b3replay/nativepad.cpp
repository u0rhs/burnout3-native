// The live window as the native game's pad (play1): src/b3replay/nativepad.h.
#include "nativepad.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../native/input_keys_sdl.h"
#include "menumouse.h"
#include "photomode.h"

namespace b3replay {

namespace {

// prompts.h keeps SDL's values for its pure mapping
static_assert(kSdlTypePS3 == SDL_GAMEPAD_TYPE_PS3 && kSdlTypePS4 == SDL_GAMEPAD_TYPE_PS4 && kSdlTypePS5 == SDL_GAMEPAD_TYPE_PS5);
static_assert(kSdlLabelA == SDL_GAMEPAD_BUTTON_LABEL_A && kSdlLabelB == SDL_GAMEPAD_BUTTON_LABEL_B &&
              kSdlLabelCross == SDL_GAMEPAD_BUTTON_LABEL_CROSS);

// mac5: a gamepad's kind for the prompts, by its model (PS4 / PS5: the modern PlayStation) else its bottom face button's
// label (SDL3: positions are fixed, labels vary); prompts.h prompt_device_for_pad
PromptDevice device_of(SDL_JoystickID id) {
    SDL_GamepadButtonLabel label = SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
    SDL_GamepadType type = SDL_GAMEPAD_TYPE_UNKNOWN;
    if (SDL_Gamepad* g = SDL_GetGamepadFromID(id)) {
        label = SDL_GetGamepadButtonLabel(g, SDL_GAMEPAD_BUTTON_SOUTH);
        type = SDL_GetGamepadType(g);
    } else {
        type = SDL_GetGamepadTypeForID(id);
        label = SDL_GetGamepadButtonLabelForType(type, SDL_GAMEPAD_BUTTON_SOUTH);
    }
    return prompt_device_for_pad(int(type), int(label));
}

double now_s() { return double(SDL_GetTicks()) / 1000.0; }

// mac5: the keys that type on the on-screen keyboard (SDL scancodes = USB HID usages): letters, digits, Enter,
// Backspace, space and the punctuation keys, the keypad's digits, Shift; they do not press pad buttons meanwhile
bool typing_key(SDL_Scancode s) {
    return (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_0) || s == SDL_SCANCODE_RETURN || s == SDL_SCANCODE_BACKSPACE ||
           (s >= SDL_SCANCODE_SPACE && s <= SDL_SCANCODE_SLASH) || (s >= SDL_SCANCODE_KP_DIVIDE && s <= SDL_SCANCODE_KP_PERIOD) ||
           s == SDL_SCANCODE_LSHIFT || s == SDL_SCANCODE_RSHIFT || s == SDL_SCANCODE_NONUSBACKSLASH;
}

void window_fraction(SDL_WindowID id, float x, float y, float& fx, float& fy) {
    int w = 0, h = 0;
    if (SDL_Window* win = SDL_GetWindowFromID(id)) SDL_GetWindowSize(win, &w, &h);
    fx = w > 0 ? x / float(w) : 0.0f;
    fy = h > 0 ? y / float(h) : 0.0f;
}

}  // namespace

NativePad& NativePad::instance() {
    static NativePad p;
    return p;
}

void NativePad::enable() {
    if (enabled_.exchange(true)) return;
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) std::fprintf(stderr, "[b3pad] no gamepads: %s\n", SDL_GetError());
    std::printf("[b3pad] the window is the game's pad: keys (docs/mac.md) and SDL gamepads\n");
    std::fflush(stdout);
}

bool NativePad::handle(const SDL_Event& e) {
    if (!enabled_) return false;
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED: {
        if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) {
            std::lock_guard<std::mutex> l(mu_);
            open_.push_back(g);
            seats_.added(e.gdevice.which);   // pad2: player 1's, player 2's, or waiting (composed into player 1's)
            const bool p2 = seats_.role(e.gdevice.which) == b3native::input::PadSeats::Role::Player2;
            if (!p2) tracker_.press(device_of(e.gdevice.which), now_s());   // mac5: a pad just connected is the one about to be used
            std::printf("[b3pad] gamepad: %s, %s (prompts: %s)\n", SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "?",
                        p2 ? "player 2 (port 2 connected)" : "player 1", prompt_device_name(tracker_.active()));
            std::fflush(stdout);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {   // mac5: the prompts follow the device in use (only observed: the state is polled)
        std::lock_guard<std::mutex> l(mu_);
        if (capture_ == 2) {   // remap1: a rebind takes the button
            Capture c;
            c.in = {b3native::input::Device::Gamepad, 0, b3native::input::pad_input_of_button(e.gbutton.button)};
            if (c.in.pad != b3native::input::PadInput::None) capture_got(c);
            return true;
        }
        if (capture_ == 1 && (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST || e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) {
            Capture c;   // a keyboard rebind cancelled from a pad
            c.cancel = true;
            capture_got(c);
            return true;
        }
        if (seats_.drives_player1(e.gbutton.which)) tracker_.press(device_of(e.gbutton.which), now_s());   // pad2: player 1's
        MenuMouse::instance().other_device(now_s());
        return false;
    }
    case SDL_EVENT_MOUSE_MOTION: {   // mac5 (menumouse.h)
        float x, y;
        window_fraction(e.motion.windowID, e.motion.x, e.motion.y, x, y);
        MenuMouse::instance().move(x, y, now_s());
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        float x, y;
        window_fraction(e.button.windowID, e.button.x, e.button.y, x, y);
        MenuMouse::instance().move(x, y, now_s());
        MenuMouse::instance().button(e.button.button, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, now_s());
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            std::lock_guard<std::mutex> l(mu_);
            if (capture_ && e.button.button == SDL_BUTTON_RIGHT) {   // remap1: a right click cancels a rebind
                Capture c;
                c.cancel = true;
                capture_got(c);
            }
            tracker_.press(PromptDevice::kKeyboard, now_s());   // the mouse is the keyboard's companion: its caps stay
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        wheel_acc_ += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
        const int steps = int(wheel_acc_);
        wheel_acc_ -= float(steps);
        if (steps) {
            MenuMouse::instance().wheel(steps, now_s());
            std::lock_guard<std::mutex> l(mu_);
            tracker_.press(PromptDevice::kKeyboard, now_s());
        }
        return true;
    }
    case SDL_EVENT_TEXT_INPUT: {   // mac5: typed text while the game's keyboard is up
        if (text_mode_ && e.text.text) type(e.text.text);
        return true;
    }
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE: {   // pad1: gyro steering's samples (only for pads whose sensors it turned on)
        for (Motion& m : motion_)
            if (m.id == e.gsensor.which && m.sensors) b3native::input::feed_gyro_event(m.steer, e);
        return true;
    }
    case SDL_EVENT_GAMEPAD_REMOVED: {
        for (auto it = motion_.begin(); it != motion_.end(); ++it)   // pad1 (gone: nothing to put back)
            if (it->id == e.gdevice.which) { motion_.erase(it); break; }
        std::lock_guard<std::mutex> l(mu_);
        for (auto it = open_.begin(); it != open_.end(); ++it)
            if (SDL_GetGamepadID(*it) == e.gdevice.which) { SDL_CloseGamepad(*it); open_.erase(it); break; }
        const bool was_p2 = seats_.role(e.gdevice.which) == b3native::input::PadSeats::Role::Player2;
        const bool had_p1 = seats_.any_player1_pad();
        seats_.removed(e.gdevice.which);   // pad2: its seat empties (or the oldest waiting pad takes it)
        if (had_p1 && !seats_.any_player1_pad()) tracker_.pads_gone(keyboard_used_, now_s());   // mac5 (pad2: player 1's pads)
        for (auto it = rumbling_.begin(); it != rumbling_.end(); ++it)   // rumble1
            if (it->id == e.gdevice.which) { rumbling_.erase(it); break; }
        if (was_p2 || seats_.pads() != 0) {
            std::printf("[b3pad] gamepad removed (%s); port 2 %s\n", was_p2 ? "player 2" : "player 1",
                        seats_.port2_connected() ? "connected" : "empty");
            std::fflush(stdout);
        }
        return true;
    }
    default: {
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)
            page_key(std::uint16_t(e.key.scancode), e.type == SDL_EVENT_KEY_DOWN);   // devnav1 (and on to the key map)
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) page_keys_ = 0;
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) focused_ = false;
        if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) focused_ = true;
        std::lock_guard<std::mutex> l(mu_);
        if (capture_ && e.type == SDL_EVENT_KEY_DOWN) {   // remap1: a rebind takes the key (ups still reach the pad)
            if (e.key.repeat) return true;
            if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                Capture c;
                c.cancel = true;
                capture_got(c);
                return true;
            }
            // rebind2: any key, the modifier keys themselves too (a Ctrl + key is not one key)
            if (capture_ == 1 && (e.key.scancode >= SDL_SCANCODE_LCTRL || !(e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) &&
                !b3native::input::key_reserved(std::uint16_t(e.key.scancode))) {
                Capture c;
                c.in = {b3native::input::Device::Keyboard, std::uint16_t(e.key.scancode), b3native::input::PadInput::None};
                capture_got(c);
            }
            return true;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !shortcut_mods_locked(e.key.mod) &&
            (b3native::input::port_actions_of_key(controls_.port, std::uint16_t(e.key.scancode)) >> int(b3native::input::PortAction::Restart) & 1))
            ask_restart("INSTANT RESTART key");   // speedrun1: INSTANT RESTART (when on); rebind2: whatever key it is on
        if (e.type == SDL_EVENT_KEY_DOWN && text_mode_ && typing_key(e.key.scancode) && !shortcut_mods_locked(e.key.mod)) {
            // mac5: a typing key while the game's keyboard is up: text, not a pad button (key ups still reach the pad,
            // so a key held when the keyboard came up is released)
            if (!e.key.repeat || e.key.scancode == SDL_SCANCODE_BACKSPACE) {
                if (e.key.scancode == SDL_SCANCODE_BACKSPACE) text_.push_back(8);
                if (e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_KP_ENTER) text_.push_back(10);
            }
            if (!e.key.repeat) { tracker_.press(PromptDevice::kKeyboard, now_s()); keyboard_used_ = true; }
            MenuMouse::instance().other_device(now_s());
            return true;
        }
        const bool used = b3native::input::handle_sdl_event(keys_, e);
        b3native::input::handle_sdl_event(keys2_, e);   // remap1: player 2's keys (none by default)
        // rebind2: a shortcut's key (screenshot, fullscreen, ...) that is no game key does not make the prompts the keyboard's
        const std::uint16_t sc = std::uint16_t(e.key.scancode);
        const auto game_key = [&]() {
            for (const auto* km : {&keys_.map(), &keys2_.map()})
                for (const b3native::input::KeyBinding& k : *km)
                    if (k.scancode == sc) return true;
            return false;
        };
        const bool shortcut_only = e.type == SDL_EVENT_KEY_DOWN && b3native::input::port_actions_of_key(controls_.port, sc) != 0 && !game_key();
        if (used && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !shortcut_only) {   // mac5: a game key
            tracker_.press(PromptDevice::kKeyboard, now_s());
            keyboard_used_ = true;
            MenuMouse::instance().other_device(now_s());
        }
        return used;
    }
    }
}

void NativePad::poll_gamepads() {
    if (!enabled_) return;
    PhotoPad photo;   // photo mode (photomode.h): the sticks, triggers and buttons as they are, L3 / R3 too
    std::unique_lock<std::mutex> l(mu_);
    std::array<std::uint8_t, 32> all = b3native::input::neutral_sample();
    std::array<std::uint8_t, 32> two = b3native::input::neutral_sample();
    std::array<float, b3native::input::kPortActions> port_amt{};   // rebind2
    const double t = now_s();
    std::vector<std::pair<SDL_Gamepad*, int>> ports;   // rumble1: each pad's port (0 player 1's, 1 player 2's)
    for (SDL_Gamepad* g : open_) {
        const SDL_JoystickID id = SDL_GetGamepadID(g);
        const bool p1 = seats_.role(id) != b3native::input::PadSeats::Role::Player2;   // pad2: player 2's is its own report
        ports.emplace_back(g, p1 ? 0 : 1);
        // mac5: a stick or trigger past half way, held (prompts.h PromptDeviceTracker: the hysteresis); pad2: player 1's
        bool past_half = false;
        for (SDL_GamepadAxis a : {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
                                  SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}) {
            const int v = SDL_GetGamepadAxis(g, a);
            past_half = past_half || v > 16384 || v < -16384;
        }
        if (p1) tracker_.axis(device_of(id), past_half, t);
        b3native::input::GamepadState s = b3native::input::gamepad_state(g);
        {   // pad1: gyro steering (opt-in, per player; off: s untouched, the report as before)
            const b3native::input::GyroControls& gy = controls_.player[p1 ? 0 : 1].gyro;
            Motion& m = motion_of(id);
            const bool want = gy.mode != 0 && SDL_GamepadHasSensor(g, SDL_SENSOR_GYRO);
            if (want != m.sensors) {
                const bool accel = SDL_GamepadHasSensor(g, SDL_SENSOR_ACCEL);
                const bool ok = SDL_SetGamepadSensorEnabled(g, SDL_SENSOR_GYRO, want);
                if (accel) SDL_SetGamepadSensorEnabled(g, SDL_SENSOR_ACCEL, want);
                m.sensors = want;
                m.steer.reset();
                m.recentre_prev = false;
                const std::string why = want && !ok ? std::string(": the sensor did not start: ") + SDL_GetError() : std::string();
                std::printf("[b3pad] gyro steering %s: %s (player %d)%s%s\n", want ? "on" : "off", SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "?",
                            p1 ? 1 : 2, want ? (accel ? ", with its accelerometer" : ", gyroscope only") : "", why.c_str());
                std::fflush(stdout);
            }
            if (m.sensors) {
                if (b3native::input::apply_gyro(s, m.steer, gy, keys_.driving(), keys_.driving() && !drive_prev_, m.recentre_prev)) {
                    std::printf("[b3pad] gyro: recentred (player %d)\n", p1 ? 1 : 2);
                    std::fflush(stdout);
                }
                static const bool log = [] { const char* e = std::getenv("B3NATIVE_GYRO_LOG"); return e && *e && *e != '0'; }();
                const std::uint64_t now = SDL_GetTicks();
                if (log && now - m.log_ms >= 500) {
                    m.log_ms = now;
                    std::printf("[b3pad] gyro: player %d %s %.1f deg -> steer %.3f%s\n", p1 ? 1 : 2, gy.axis ? "yaw" : "roll",
                                double(m.steer.angle(b3native::pad::GyroAxis(gy.axis)) * 180.0f / b3native::pad::kPi),
                                double(m.steer.steer(b3native::pad::GyroAxis(gy.axis), gy.sensitivity, gy.dead_zone)),
                                keys_.driving() ? "" : " (not driving: unused)");
                    std::fflush(stdout);
                }
            }
        }
        if (capture_ == 2) {   // remap1: a stick or trigger pushed past half way, from rest, is a rebind's input
            auto it = std::find_if(capture_rest_.begin(), capture_rest_.end(), [id](const auto& r) { return r.first == id; });
            if (it == capture_rest_.end()) { capture_rest_.push_back({id, {}}); it = capture_rest_.end() - 1; }
            for (int k = 0; k < 10; ++k) {
                const auto in = b3native::input::PadInput(int(b3native::input::PadInput::LeftXNeg) + k);
                const float a = b3native::input::pad_input_amount(s, in, 0.24f);
                if (a < 0.3f) it->second[std::size_t(k)] = true;
                else if (a > 0.6f && it->second[std::size_t(k)]) {
                    Capture c;
                    c.in = {b3native::input::Device::Gamepad, 0, in};
                    capture_got(c);
                    break;
                }
            }
        }
        // remap1: the seat's bindings (player 1's for player 1's and the waiting pads; defaults: gamepad_sample's layout)
        const auto one = b3native::input::gamepad_sample(s, controls_.player[p1 ? 0 : 1]);
        if (!p1) {   // pad2: player 2's pad (port 2); not photo mode's (player 1's camera)
            two = one;
            continue;
        }
        all = b3native::input::merge_samples(all.data(), one.data());
        // rebind2: the port actions' gamepad bindings (a chord: its inputs held together), the largest over player 1's pads
        for (int a = 0; a < b3native::input::kPortActions; ++a) {
            const auto pa = b3native::input::PortAction(a);
            if (controls_.port.pad[std::size_t(a)].empty()) continue;
            port_amt[std::size_t(a)] = std::max(port_amt[std::size_t(a)],
                                                b3native::input::port_pad_amount(s, controls_.port, pa, b3native::input::port_analog(pa) ? 0.0f : 0.24f));
        }
    }
    pads_ = all;
    {
        using PA = b3native::input::PortAction;
        const auto amt = [&](PA a) { return port_amt[std::size_t(a)]; };
        const auto btn = [&](PA a) { return amt(a) >= 0.5f && capture_ == 0; };
        // photo mode (photomode.h): the camera's axes as the difference of the bound halves (its dead zone applies), the
        // rest as buttons
        photo.lx = amt(PA::PhotoRight) - amt(PA::PhotoLeft);
        photo.ly = amt(PA::PhotoBackward) - amt(PA::PhotoForward);
        photo.rx = amt(PA::PhotoLookRight) - amt(PA::PhotoLookLeft);
        photo.ry = amt(PA::PhotoLookDown) - amt(PA::PhotoLookUp);
        photo.rt = amt(PA::PhotoUp);
        photo.lt = amt(PA::PhotoDown);
        if (capture_ != 0) photo.lx = photo.ly = photo.rx = photo.ry = photo.rt = photo.lt = 0.0f;
        photo.south = btn(PA::PhotoFast);
        photo.slow = btn(PA::PhotoSlow);
        photo.west = btn(PA::PhotoReset);
        photo.north = btn(PA::PhotoHelp);
        photo.east = btn(PA::PhotoExit);
        photo.l1 = btn(PA::PhotoRollLeft);
        photo.r1 = btn(PA::PhotoRollRight);
        photo.up = btn(PA::PhotoNarrower);
        photo.down = btn(PA::PhotoWider);
        photo.left = btn(PA::PhotoFocusNear);
        photo.right = btn(PA::PhotoFocusFar);
        photo.back = btn(PA::PhotoDof);
        photo.start = btn(PA::PhotoBlurCycle);
        photo.blur_less = btn(PA::PhotoBlurLess);
        photo.blur_more = btn(PA::PhotoBlurMore);
        photo.target = btn(PA::PhotoTarget);
        photo.toggle = btn(PA::PhotoMode);
        // speedrun1: the INSTANT RESTART chord on player 1's pads (Select + Start; when on); the window's shortcuts: edges
        const bool combo = btn(PA::Restart);
        if (combo && !combo_prev_) ask_restart("INSTANT RESTART buttons");
        combo_prev_ = combo;
        std::uint64_t held = 0;
        for (PA a : {PA::Fullscreen, PA::Screenshot, PA::BugReport})
            if (btn(a)) held |= std::uint64_t(1) << int(a);
        port_pad_edges_ |= held & ~port_pad_prev_;
        port_pad_prev_ = held;
    }
    have_pad2_ = seats_.port2_connected();
    pad2_ = two;
    have_pads_ = seats_.any_player1_pad();   // (one pad or none: !open_.empty(), as before pad2)
    photo.connected = have_pads_;
    drive_prev_ = keys_.driving();
    l.unlock();
    PhotoMode::instance().pad(photo);
    give_rumble(ports);
    give_triggers(ports);
}

NativePad::Motion& NativePad::motion_of(SDL_JoystickID id) {
    for (Motion& m : motion_)
        if (m.id == id) return m;
    motion_.push_back(Motion{});
    motion_.back().id = id;
    return motion_.back();
}

void NativePad::set_trigger_game(int port, const b3native::pad::TriggerGame& g) {
    if (port < 0 || port > 1) return;
    std::lock_guard<std::mutex> l(mu_);
    trigger_game_[port] = g;
}

// pad1 (window thread): each DualSense given its port's trigger effects when they change (the controller keeps an
// effect until the next one). Off and never on: nothing is sent. B3NATIVE_TRIGGER_LOG=1: each change.
void NativePad::give_triggers(const std::vector<std::pair<SDL_Gamepad*, int>>& pads) {
    const bool on = triggers_on_.load();
    const int strength = triggers_strength_.load();
    const std::uint64_t now = SDL_GetTicks();
    b3native::pad::TriggerGame game[2];
    std::uint8_t roles[2][2] = {};
    {
        std::lock_guard<std::mutex> l(mu_);
        for (int p = 0; p < 2; ++p) {
            game[p] = trigger_game_[p];
            game[p].active = game[p].active && on && keys_.driving();
            roles[p][0] = b3native::input::trigger_role(controls_.player[std::size_t(p)], false);
            roles[p][1] = b3native::input::trigger_role(controls_.player[std::size_t(p)], true);
        }
    }
    static const bool log = [] { const char* e = std::getenv("B3NATIVE_TRIGGER_LOG"); return e && *e && *e != '0'; }();
    for (const auto& [g, port] : pads) {
        if (SDL_GetGamepadType(g) != SDL_GAMEPAD_TYPE_PS5) continue;
        Motion& m = motion_of(SDL_GetGamepadID(g));
        const b3native::pad::TriggerPair fx = m.fx.step(game[port], b3native::pad::TriggerRole(roles[port][0]),
                                                        b3native::pad::TriggerRole(roles[port][1]), strength, now);
        const b3native::pad::Ds5Block block = b3native::pad::ds5_trigger_block(fx.left, fx.right);
        if (!m.fx_sent && block == b3native::pad::ds5_triggers_off()) continue;   // never touched: leave the pad alone
        if (m.fx_sent && block == m.fx_last) continue;
        const bool ok = SDL_SendGamepadEffect(g, block.data(), int(block.size()));
        if (!m.fx_sent || log) {
            std::printf("[b3pad] adaptive triggers: %s (player %d): left %02X %u %u %u, right %02X %u %u %u%s%s\n",
                        SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "?", port + 1, fx.left[0], fx.left[1], fx.left[2], fx.left[3],
                        fx.right[0], fx.right[1], fx.right[2], fx.right[3], ok ? "" : ": not taken: ", ok ? "" : SDL_GetError());
            std::fflush(stdout);
        }
        m.fx_sent = true;
        m.fx_last = block;
    }
}

void NativePad::release_pads() {
    if (!enabled_) return;
    std::lock_guard<std::mutex> l(mu_);
    for (SDL_Gamepad* g : open_) {
        const SDL_JoystickID id = SDL_GetGamepadID(g);
        for (Motion& m : motion_) {
            if (m.id != id) continue;
            if (m.fx_sent && m.fx_last != b3native::pad::ds5_triggers_off()) {
                const b3native::pad::Ds5Block off = b3native::pad::ds5_triggers_off();
                SDL_SendGamepadEffect(g, off.data(), int(off.size()));
                m.fx_last = off;
            }
            if (m.sensors) {
                SDL_SetGamepadSensorEnabled(g, SDL_SENSOR_GYRO, false);
                if (SDL_GamepadHasSensor(g, SDL_SENSOR_ACCEL)) SDL_SetGamepadSensorEnabled(g, SDL_SENSOR_ACCEL, false);
                m.sensors = false;
            }
        }
    }
}

// rumble1: the game's motors (b3r_rumble, game thread). B3NATIVE_RUMBLE_LOG=1: each change, as a pad is given it.
void NativePad::set_motors(int port, std::uint8_t small, std::uint8_t large) {
    if (port < 0 || port > 1) return;
    static const bool log = [] {
        const char* e = std::getenv("B3NATIVE_RUMBLE_LOG");
        return e && *e && *e != '0';
    }();
    const b3native::pad::Motors m{small, large};
    bool changed = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        changed = !(motors_[port] == m);
        motors_[port] = m;
        motors_ms_[port] = SDL_GetTicks();
    }
    if (log && changed) {
        const auto o = b3native::pad::rumble_out(m, rumble_strength_.load(), rumble_triggers_.load());
        std::printf("[b3pad] rumble: port %d small %u large %u -> low %u high %u triggers %u %u\n", port + 1, unsigned(small),
                    unsigned(large), unsigned(o.low), unsigned(o.high), unsigned(o.left_trigger), unsigned(o.right_trigger));
        std::fflush(stdout);
    }
}

// rumble1 (window thread): each open pad given its port's motors -- when they change, and again every 500 ms while
// they run (each effect lasts 1 s, so a pad that missed nothing never stops between); stopped when they end, RUMBLE is
// Off, or the port's motors are stale (not handed for 250 ms: the game held, ended, or an older native_play).
void NativePad::give_rumble(const std::vector<std::pair<SDL_Gamepad*, int>>& pads) {
    if (pads.empty()) return;
    const bool on = rumble_on_.load(), triggers = rumble_triggers_.load();
    const int strength = rumble_strength_.load();
    const std::uint64_t now = SDL_GetTicks();
    b3native::pad::Motors m[2];
    {
        std::lock_guard<std::mutex> l(mu_);
        for (int p = 0; p < 2; ++p)
            if (on && now - motors_ms_[p] <= 250) m[p] = motors_[p];
    }
    for (const auto& [g, port] : pads) {
        const SDL_JoystickID id = SDL_GetGamepadID(g);
        Rumbling* r = nullptr;
        for (Rumbling& x : rumbling_) if (x.id == id) r = &x;
        if (!r) {
            rumbling_.push_back(Rumbling{id, {}, 0});
            r = &rumbling_.back();
        }
        const bool can_triggers = SDL_GetBooleanProperty(SDL_GetGamepadProperties(g), SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false);
        const b3native::pad::RumbleOut o = b3native::pad::rumble_out(m[port], strength, triggers && can_triggers);
        if (o == r->out && (!o.any() || now - r->at < 500)) continue;
        const std::uint32_t ms = o.any() ? 1000 : 0;
        const bool given = SDL_RumbleGamepad(g, o.low, o.high, ms);
        if (can_triggers && (o.left_trigger || o.right_trigger || r->out.left_trigger || r->out.right_trigger))
            SDL_RumbleGamepadTriggers(g, o.left_trigger, o.right_trigger, ms);
        if (o.any() && !r->out.any() && r->at == 0) {   // the first time this pad rumbles
            std::printf("[b3pad] rumble: %s (player %d)%s\n", SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "?", port + 1,
                        given ? (can_triggers && triggers ? ", triggers too" : "") : ": no rumble on this pad");
            std::fflush(stdout);
        }
        r->out = o;
        r->at = now;
    }
}

std::array<std::uint8_t, 32> NativePad::sample(float dt) {
    std::lock_guard<std::mutex> l(mu_);
    keys_.step(dt);
    keys2_.step(dt);   // remap1: player 2's keys (none by default: neutral)
    ++taken_;
    last_ = keys_.compose(have_pads_ ? pads_.data() : nullptr);
    if (restart_pending_) {   // speedrun1: INSTANT RESTART, at this VBlank (the pad's grid)
        restart_pending_ = false;
        const bool go = restart_on_.load() && keys_.driving() && restart_.trigger();
        std::printf("[b3pad] instant restart %s at sample %llu\n", go ? "started (the pause menu's RESTART, YES)" :
                    !restart_on_.load() ? "not on" : restart_.active() ? "already running" : "ignored: not driving",
                    static_cast<unsigned long long>(taken_.load()));
        std::fflush(stdout);
    }
    if (restart_.apply(last_)) {   // the macro's report alone while it runs
        if (!restart_.active()) {
            std::printf("[b3pad] instant restart done at sample %llu: the pad is the player's again\n", static_cast<unsigned long long>(taken_.load()));
            std::fflush(stdout);
        }
        return last_;
    }
    const bool was = boost_.latched();
    boost_.apply(last_, boost_toggle_.load(), keys_.driving());   // accessibility: BOOST Toggle (off: untouched)
    if (boost_.latched() != was) {
        std::printf("[b3pad] boost toggle: %s at sample %llu\n", boost_.latched() ? "latched" : "released",
                    static_cast<unsigned long long>(taken_.load()));
        std::fflush(stdout);
    }
    return last_;
}

bool NativePad::sample2(std::array<std::uint8_t, 32>& out) {
    std::lock_guard<std::mutex> l(mu_);
    if (!have_pad2_ && !keys2_on_) {
        auto n = b3native::input::neutral_sample();
        boost2_.apply(n, false, false);   // (resets the latch: a pad seated later starts unlatched)
        return false;
    }
    // remap1: player 2's keys, when they have any, with player 2's pad (else the pad's report as before)
    out = keys2_on_ ? keys2_.compose(have_pad2_ ? pad2_.data() : nullptr) : pad2_;
    boost2_.apply(out, boost_toggle_.load(), keys_.driving());   // accessibility: BOOST Toggle for player 2 too (off: untouched)
    return true;
}

std::array<std::uint8_t, 32> NativePad::last() const {
    std::lock_guard<std::mutex> l(mu_);
    return last_;
}

PromptDevice NativePad::device() const {
    static const int fixed = [] {
        const char* e = std::getenv("B3_PROMPT_DEVICE");
        PromptDevice d = PromptDevice::kPlayStation;
        return e && prompt_device_parse(e, d) ? int(d) : -1;
    }();
    if (fixed >= 0) return PromptDevice(fixed);
    std::lock_guard<std::mutex> l(mu_);
    return tracker_.active();
}

PromptKeys NativePad::prompt_keys(PromptDevice d) const {
    std::lock_guard<std::mutex> l(mu_);
    if (d != PromptDevice::kKeyboard) return prompt_pad_glyphs(b3native::input::pad_glyphs(controls_.player[0]));   // remap1
    return b3replay::prompt_keys(keys_.map(), keys_.driving());
}

void NativePad::set_controls(const b3native::input::Controls& c) {
    PhotoMode::instance().set_bindings(c.port);   // rebind2 (before mu_: it takes its own lock)
    std::lock_guard<std::mutex> l(mu_);
    controls_ = c;
    keys_.set_map(b3native::input::key_map(c.player[0]));
    keys2_.set_map(b3native::input::key_map(c.player[1]));
    keys2_on_ = c.player[1].has_keys();
}

b3native::input::Controls NativePad::controls() const {
    std::lock_guard<std::mutex> l(mu_);
    return controls_;
}

void NativePad::begin_capture(b3native::input::Device d) {
    std::lock_guard<std::mutex> l(mu_);
    capture_ = d == b3native::input::Device::Keyboard ? 1 : 2;
    capture_have_ = false;
    capture_out_ = {};
    capture_rest_.clear();
}

void NativePad::end_capture() {
    std::lock_guard<std::mutex> l(mu_);
    capture_ = 0;
    capture_have_ = false;
}

void NativePad::capture_got(const Capture& c) {
    if (!capture_ || capture_have_) return;
    capture_out_ = c;
    capture_have_ = true;
}

bool NativePad::take_capture(Capture& out) {
    std::lock_guard<std::mutex> l(mu_);
    if (!capture_have_) return false;
    out = capture_out_;
    capture_have_ = false;
    return true;
}

void NativePad::set_driving(bool on) {
    std::lock_guard<std::mutex> l(mu_);
    keys_.set_driving(on);
    keys2_.set_driving(on);
}

void NativePad::mouse(int kind, float x, float y, int n) {
    MenuMouse& m = MenuMouse::instance();
    const double t = now_s();
    if (kind == 0) m.move(x, y, t);
    else if (kind == 1 || kind == 2) m.button(n, kind == 1, t);
    else if (kind == 3) m.wheel(n, t);
    if (kind == 1 || kind == 3) {
        std::lock_guard<std::mutex> l(mu_);
        tracker_.press(PromptDevice::kKeyboard, t);
    }
}

void NativePad::sync_text_input(SDL_Window* w) {
    const bool want = text_mode_.load();
    if (!w || want == text_started_) return;
    text_started_ = want;
    if (want) SDL_StartTextInput(w);
    else SDL_StopTextInput(w);
    std::printf("[b3pad] the game's keyboard is %s: the keys %s\n", want ? "up" : "down", want ? "type" : "are the pad again");
    std::fflush(stdout);
}

void NativePad::type(const char* utf8) {
    std::lock_guard<std::mutex> l(mu_);
    for (const char* c = utf8; c && *c; ++c)
        if (static_cast<unsigned char>(*c) < 0x80 && text_.size() < 256) text_.push_back(*c);   // ASCII (the game's callback takes a char)
}

int NativePad::take_text(char* out, int max) {
    std::lock_guard<std::mutex> l(mu_);
    int n = 0;
    for (; n < max && std::size_t(n) < text_.size(); ++n) out[n] = text_[std::size_t(n)];
    text_.erase(text_.begin(), text_.begin() + n);
    return n;
}

void NativePad::page_key(std::uint16_t scancode, bool down) {
    using b3native::input::PortAction;
    std::uint64_t m;
    {
        std::lock_guard<std::mutex> l(mu_);
        m = b3native::input::port_actions_of_key(controls_.port, scancode);
    }
    const int bit = (m >> int(PortAction::SectionPrev) & 1 ? 1 : 0) | (m >> int(PortAction::SectionNext) & 1 ? 2 : 0);   // rebind2: any keys
    if (!bit) {
        if (!down) {   // a key that was a section key when it went down and has been rebound since: let go of what it held
            if (scancode == SDL_SCANCODE_PAGEUP || scancode == SDL_SCANCODE_PAGEDOWN) page_keys_ = 0;
        }
        return;
    }
    if (down) page_keys_ |= bit;
    else page_keys_ &= ~bit;
}

bool NativePad::shortcut_mods_locked(unsigned mod) const {
    const auto bound = [&](std::uint16_t a, std::uint16_t b) {
        for (const auto* km : {&keys_.map(), &keys2_.map()})
            for (const b3native::input::KeyBinding& k : *km)
                if (k.scancode == a || k.scancode == b) return true;
        for (const auto& l : controls_.port.keys)
            for (std::uint16_t k : l)
                if (k == a || k == b) return true;
        return false;
    };
    if ((mod & SDL_KMOD_CTRL) && !bound(SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL)) return true;
    if ((mod & SDL_KMOD_GUI) && !bound(SDL_SCANCODE_LGUI, SDL_SCANCODE_RGUI)) return true;
    return false;
}

bool NativePad::shortcut_mods(unsigned mod) const {
    std::lock_guard<std::mutex> l(mu_);
    return shortcut_mods_locked(mod);
}

bool NativePad::capturing() const {
    std::lock_guard<std::mutex> l(mu_);
    return capture_ != 0;
}

std::uint64_t NativePad::port_mask(std::uint16_t scancode, unsigned mod) const {
    std::lock_guard<std::mutex> l(mu_);
    if (capture_) return 0;
    if (!b3native::input::function_key_ignores_mods(scancode) && shortcut_mods_locked(mod)) return 0;
    return b3native::input::port_actions_of_key(controls_.port, scancode);
}

std::uint64_t NativePad::take_port_pad() { return port_pad_edges_.exchange(0); }

std::string NativePad::port_key_text(b3native::input::PortAction a) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto& k = controls_.port.keys[std::size_t(a)];
    return k.empty() ? "-" : b3native::input::key_label(k.front());
}

void NativePad::key(std::uint16_t scancode, bool down) {
    if (PhotoMode::instance().key(scancode, down)) return;   // photo mode's keys (a script's too); ups still come here
    page_key(scancode, down);   // devnav1
    std::lock_guard<std::mutex> l(mu_);
    if (capture_ && down) {   // remap1: a script's key during a rebind (as a window key)
        Capture c;
        if (scancode == SDL_SCANCODE_ESCAPE) c.cancel = true;
        else c.in = {b3native::input::Device::Keyboard, scancode, b3native::input::PadInput::None};
        if (c.cancel || (capture_ == 1 && !b3native::input::key_reserved(scancode))) capture_got(c);
        return;
    }
    if (b3native::input::port_actions_of_key(controls_.port, scancode) >> int(b3native::input::PortAction::Restart) & 1) {   // speedrun1: a script's restart key
        if (down) ask_restart("restart key (script)");
        return;
    }
    keys_.key(scancode, down);
    keys2_.key(scancode, down);
}

void NativePad::ask_restart(const char* by) {
    if (!restart_on_.load()) return;   // off: F5 / the combo do nothing (Start reaches the game as always)
    restart_pending_ = true;
    std::printf("[b3pad] instant restart asked (%s)\n", by);
    std::fflush(stdout);
}

bool NativePad::restarting() const {
    std::lock_guard<std::mutex> l(mu_);
    return restart_.active();
}

}  // namespace b3replay
