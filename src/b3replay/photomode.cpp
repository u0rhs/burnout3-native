// Photo mode (remaster-features §5): src/b3replay/photomode.h.
#include "photomode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace b3replay {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kMaxPitch = 89.0f * kPi / 180.0f;
constexpr float kMinFov = 5.0f * kPi / 180.0f, kMaxFov = 120.0f * kPi / 180.0f;
constexpr float kSpeed = 10.0f;          // m/s; Shift / A x4, Alt x0.25
constexpr float kLookRate = 1.6f;        // rad/s: arrow keys, the right stick at full tilt
constexpr float kMouseRate = 0.0022f;    // rad per mouse count (at a 60 degree field of view; scaled with it)
// devnav1 (2026-11-13, a player report: zooming slow and stuttering on a Mac): the held options. The field of
// view zooms by a constant ratio per second (a doubling or halving in 0.6 s: 60 -> 120 degrees in 0.6 s, 60 -> 30
// likewise; it was 0.6 rad/s linear, 1.7 s for 60 -> 120 and too fast at the narrow end); roll 1.2 rad/s (was 0.8);
// the focus 1.5 stops/s (was 1). Each speeds up while held (x1 to x2 from 0.5 s to 1.5 s, hold_rate()); Shift / A
// x2, Alt x0.25 (fine), as for moving.
constexpr float kRollRate = 1.2f;        // rad/s: Z / C, LB / RB
constexpr float kFovZoom = 1.1552453f;   // ln 2 / 0.6 s: - / = and the D-pad up / down (the field of view x e^(rate t))
constexpr float kDeadZone = 0.15f;
constexpr float kFocusRate = 1.5f;       // stops/s: N / M and the D-pad left / right (photo2)
constexpr float kHoldFrom = 0.5f, kHoldRamp = 1.0f, kHoldMax = 2.0f;   // the hold's speed-up: x1 until 0.5 s, x2 from 1.5 s
// photo2: the blur strength levels' largest circle radius, fractions of the output height (1080 rows: 3 .. 26 pixels)
constexpr float kDofRadius[PhotoDof::kLevels] = {0.003f, 0.005f, 0.008f, 0.012f, 0.017f, 0.024f};

// photo2: a depth-of-field key going down (the window's state or the game-time replay's); true when it was one
bool dof_key(PhotoDof& d, std::uint16_t sc, const b3native::input::PortControls& b) {
    using b3native::input::PortAction;
    const std::uint64_t m = b3native::input::port_actions_of_key(b, sc);
    const auto has = [&](PortAction a) { return (m >> int(a) & 1) != 0; };
    bool used = false;
    if (has(PortAction::PhotoDof)) { d.toggle(); used = true; }
    if (has(PortAction::PhotoBlurLess)) { d.blur(-1); used = true; }
    if (has(PortAction::PhotoBlurMore)) { d.blur(+1); used = true; }
    if (has(PortAction::PhotoBlurCycle)) { d.blur(0); used = true; }
    if (has(PortAction::PhotoTarget)) { d.next_target(); used = true; }
    return used;
}

// One held option's speed factor for this step: x1 for the first kHoldFrom seconds held in one direction, then up to
// kHoldMax over kHoldRamp seconds; a release or the other direction starts again. Game time steps it a poll (1/60 s)
// at a time from the replayed keys: the same factors in every run.
float hold_rate(PhotoCam::Hold& h, float dir, float dt) {
    const int d = dir > 0 ? 1 : dir < 0 ? -1 : 0;
    if (d == 0 || d != h.dir) {
        h.dir = d;
        h.held = 0;
    }
    if (d == 0) return 0.0f;
    const float m = 1.0f + (kHoldMax - 1.0f) * std::clamp((h.held - kHoldFrom) / kHoldRamp, 0.0f, 1.0f);
    h.held = std::min(h.held + dt, 60.0f);
    return m;
}

float stick(float v) {   // a dead zone, then rescaled to full
    const float a = std::fabs(v);
    if (a <= kDeadZone) return 0.0f;
    return std::copysign(std::min(1.0f, (a - kDeadZone) / (1.0f - kDeadZone)), v);
}

// One step of the free camera: `dt` seconds of the held keys and the pad, plus the mouse / wheel moved since the last
// (the window's viewer, advance(); game time replays the keys alone, 1/60 s a poll)
void step(PhotoCam& cam, PhotoDof& dof, float dt, const std::array<bool, 512>& held, const PhotoPad& p, float mouse_dx, float mouse_dy,
          float wheel, const b3native::input::PortControls& b) {
    using b3native::input::PortAction;
    // an action's keys: 1 when any is held
    auto k = [&](PortAction a) {
        for (std::uint16_t sc : b.keys[std::size_t(a)])
            if (sc < held.size() && held[sc]) return 1.0f;
        return 0.0f;
    };
    // the held options' modifiers (Shift / A fast, Alt fine), as for moving
    float opt = 1.0f;
    if (k(PortAction::PhotoFast) > 0 || p.south) opt *= 2.0f;
    if (k(PortAction::PhotoSlow) > 0 || p.slow) opt *= 0.25f;
    // photo2: the focus nearer / farther (held)
    const float focus = k(PortAction::PhotoFocusFar) - k(PortAction::PhotoFocusNear) + (p.right ? 1.0f : 0.0f) - (p.left ? 1.0f : 0.0f);
    const float focus_m = hold_rate(cam.hold_focus, focus, dt);
    if (focus != 0) dof.focus(focus * kFocusRate * opt * focus_m * dt);
    // look: the mouse (scaled with the field of view, so a zoomed view turns as finely), the arrows, the right stick
    const float fs = cam.fov / (60.0f * kPi / 180.0f);
    cam.yaw -= mouse_dx * kMouseRate * fs;
    cam.pitch -= mouse_dy * kMouseRate * fs;
    cam.yaw += (k(PortAction::PhotoLookLeft) - k(PortAction::PhotoLookRight) - stick(p.rx)) * kLookRate * fs * dt;
    cam.pitch += (k(PortAction::PhotoLookUp) - k(PortAction::PhotoLookDown) - stick(p.ry)) * kLookRate * fs * dt;
    cam.pitch = std::clamp(cam.pitch, -kMaxPitch, kMaxPitch);
    cam.yaw = std::remainder(cam.yaw, 2.0f * kPi);
    // roll
    const float roll = k(PortAction::PhotoRollRight) - k(PortAction::PhotoRollLeft) + (p.r1 ? 1.0f : 0.0f) - (p.l1 ? 1.0f : 0.0f);
    const float roll_m = hold_rate(cam.hold_roll, roll, dt);
    if (roll != 0) {
        cam.roll += roll * kRollRate * opt * roll_m * dt;
        cam.roll = std::remainder(cam.roll, 2.0f * kPi);
    }
    // field of view: the wheel (a step of 10 %), - / = and the D-pad (up: narrower), by a ratio
    if (wheel != 0) cam.fov *= std::pow(0.9f, wheel);
    const float zoom = k(PortAction::PhotoWider) - k(PortAction::PhotoNarrower) +
                       (p.down ? 1.0f : 0.0f) - (p.up ? 1.0f : 0.0f);
    const float zoom_m = hold_rate(cam.hold_fov, zoom, dt);
    if (zoom != 0) cam.fov *= std::exp(zoom * kFovZoom * opt * zoom_m * dt);
    cam.fov = std::clamp(cam.fov, kMinFov, kMaxFov);
    // move: along the view (flying), level sideways, world up / down
    float speed = kSpeed;
    if (k(PortAction::PhotoFast) > 0 || p.south) speed *= 4.0f;
    if (k(PortAction::PhotoSlow) > 0 || p.slow) speed *= 0.25f;
    const float fwd = k(PortAction::PhotoForward) - k(PortAction::PhotoBackward) - stick(p.ly);
    const float side = k(PortAction::PhotoRight) - k(PortAction::PhotoLeft) + stick(p.lx);
    const float up = k(PortAction::PhotoUp) - k(PortAction::PhotoDown) + (p.rt > 0.1f ? p.rt : 0.0f) - (p.lt > 0.1f ? p.lt : 0.0f);
    float f[3], r[3];
    cam.forward(f);
    cam.right(r);
    for (int i = 0; i < 3; ++i) cam.pos[i] += (f[i] * fwd + r[i] * side) * speed * dt;
    cam.pos[1] += up * speed * dt;
}

}  // namespace

float PhotoDof::radius() const { return on ? kDofRadius[std::clamp(level, 1, kLevels) - 1] : 0.0f; }

void PhotoDof::blur(int dir) {
    if (dir == 0) level = level >= kLevels ? 1 : level + 1;
    else level = std::clamp(level + dir, 1, kLevels);
}

void PhotoDof::focus(float d) { stops = std::clamp(stops + d, -kMaxStops, kMaxStops); }

b3gfx::FrameParams::Dof PhotoDof::params(const Camera& cam, const float* car, const float* view_proj, int* subject) const {
    b3gfx::FrameParams::Dof d;
    if (subject) *subject = 0;
    if (!on) return d;
    d.on = true;
    d.radius = radius();
    d.focus_scale = std::exp2(stops);
    if (target == 0 && car) {
        float dist = 0;   // along the view (the camera's `at` row)
        for (int k = 0; k < 3; ++k) dist += (car[k] - cam.m.r[3][k]) * cam.m.r[2][k];
        if (dist > 0.3f && view_proj) {   // the autofocus on the car's middle as the frame shows it
            const float* m = view_proj;   // column-major
            const float cx = m[0] * car[0] + m[4] * car[1] + m[8] * car[2] + m[12];
            const float cy = m[1] * car[0] + m[5] * car[1] + m[9] * car[2] + m[13];
            const float cw = m[3] * car[0] + m[7] * car[1] + m[11] * car[2] + m[15];
            if (cw > 0.3f) {
                const float u = 0.5f + 0.5f * cx / cw, v = 0.5f - 0.5f * cy / cw;
                if (u > 0.03f && u < 0.97f && v > 0.03f && v < 0.97f) {
                    d.point[0] = u;
                    d.point[1] = v;
                    if (subject) *subject = 1;
                    return d;
                }
            }
        } else if (dist > 0.3f) {   // no projection: the car's distance
            d.focus = dist * d.focus_scale;
            if (subject) *subject = 1;
            return d;
        }
    }
    if (subject) *subject = 2;   // the screen centre
    return d;
}

std::string PhotoDof::status(int subject) const {
    if (!on) return "DOF OFF";
    const bool car = subject == 1 || (subject == 0 && target == 0);
    std::string s = "DOF " + std::to_string(level) + (car ? "   FOCUS CAR" : "   FOCUS CENTRE");
    if (std::fabs(stops) >= 0.005f) {
        char buf[24];
        std::snprintf(buf, sizeof buf, " X%.2f", double(std::exp2(stops)));
        s += buf;
    }
    return s;
}

void PhotoCam::forward(float f[3]) const {
    f[0] = std::sin(yaw) * std::cos(pitch);
    f[1] = std::sin(pitch);
    f[2] = std::cos(yaw) * std::cos(pitch);
}

void PhotoCam::right(float r[3]) const {
    r[0] = -std::cos(yaw);
    r[1] = 0;
    r[2] = std::sin(yaw);
}

void PhotoCam::from(const Camera& c) {
    for (int i = 0; i < 3; ++i) pos[i] = c.m.r[3][i];
    const float* a = c.m.r[2];
    yaw = std::atan2(a[0], a[2]);
    pitch = std::clamp(std::atan2(a[1], std::sqrt(a[0] * a[0] + a[2] * a[2])), -kMaxPitch, kMaxPitch);
    roll = 0;
    hold_fov = hold_roll = hold_focus = Hold{};
    fov = c.half_h > 0 ? std::clamp(2.0f * std::atan(c.half_h), kMinFov, kMaxFov) : 1.0f;
}

Camera PhotoCam::camera(const Camera& game) const {
    Camera c = game;
    float f[3];
    forward(f);
    float l[3] = {f[2], 0, -f[0]};   // (0, 1, 0) x f: screen left, level
    const float ll = std::sqrt(l[0] * l[0] + l[2] * l[2]);
    if (ll > 0) { l[0] /= ll; l[2] /= ll; } else { l[0] = 1; l[2] = 0; }
    const float u[3] = {f[1] * l[2] - f[2] * l[1], f[2] * l[0] - f[0] * l[2], f[0] * l[1] - f[1] * l[0]};   // f x l
    // roll about `at`: clockwise as the viewer sees it (up leans to screen right = -left)
    const float cr = std::cos(roll), sr = std::sin(roll);
    for (int k = 0; k < 3; ++k) {
        c.m.r[0][k] = l[k] * cr + u[k] * sr;
        c.m.r[1][k] = u[k] * cr - l[k] * sr;
        c.m.r[2][k] = f[k];
        c.m.r[3][k] = pos[k];
    }
    const float aspect = game.half_h > 0 && game.half_w > 0 ? game.half_w / game.half_h : 4.0f / 3.0f;
    c.half_h = std::tan(0.5f * fov);
    c.half_w = c.half_h * aspect;
    c.znear = game.znear > 0 ? std::min(game.znear, 0.1f) : 0.1f;
    c.valid = true;
    return c;
}

PhotoMode& PhotoMode::instance() {
    static PhotoMode p;
    return p;
}

void PhotoMode::set_game_paused(bool on) {
    std::lock_guard<std::mutex> l(mu_);
    if (paused_ == on) return;
    paused_ = on;
    if (!on && active_) leave_locked("the game is no longer paused");
}

bool PhotoMode::request_enter(const char* how) {
    std::lock_guard<std::mutex> l(mu_);
    if (active_ || !can_enter_locked()) return false;
    enter_locked(how);
    return true;
}

bool PhotoMode::game_paused() const {
    std::lock_guard<std::mutex> l(mu_);
    return paused_;
}

void PhotoMode::set_scene(bool on) {
    std::lock_guard<std::mutex> l(mu_);
    scene_ = on;
}

bool PhotoMode::can_enter_locked() const { return paused_ && scene_; }

void PhotoMode::enter_locked(const char* how) {
    active_ = true;
    started_ = false;
    held_.fill(false);
    mouse_dx_ = mouse_dy_ = wheel_ = 0;
    reset_ = false;
    ui_fresh_ = true;   // the UI slides in again
    ++serial_;
    record_locked(Event::kEnter);
    std::printf("[b3photo] photo mode on (%s), entry %llu\n", how, static_cast<unsigned long long>(serial_));
    std::fflush(stdout);
}

void PhotoMode::leave_locked(const char* how) {
    active_ = false;
    started_ = false;
    held_.fill(false);
    record_locked(Event::kLeave);
    std::printf("[b3photo] photo mode off (%s): the game's camera\n", how);
    std::fflush(stdout);
}

PhotoKeys photo_keys_from(const b3native::input::PortControls& b) {
    using b3native::input::PadInput;
    using b3native::input::PortAction;
    PhotoKeys K;
    const auto key = [&](PortAction a, std::size_t n, std::uint16_t fallback = 0) -> std::uint16_t {
        const auto& l = b.keys[std::size_t(a)];
        return n < l.size() ? l[n] : fallback;
    };
    K.fwd = key(PortAction::PhotoForward, 0);
    K.back = key(PortAction::PhotoBackward, 0);
    K.left = key(PortAction::PhotoLeft, 0);
    K.right = key(PortAction::PhotoRight, 0);
    K.up = key(PortAction::PhotoUp, 0);
    K.down = key(PortAction::PhotoDown, 0);
    K.fast = key(PortAction::PhotoFast, 0);
    K.fast_alt = key(PortAction::PhotoFast, 1);
    K.slow = key(PortAction::PhotoSlow, 0);
    K.slow_alt = key(PortAction::PhotoSlow, 1);
    K.look_left = key(PortAction::PhotoLookLeft, 0);
    K.look_right = key(PortAction::PhotoLookRight, 0);
    K.look_up = key(PortAction::PhotoLookUp, 0);
    K.look_down = key(PortAction::PhotoLookDown, 0);
    K.roll_cw = key(PortAction::PhotoRollRight, 0);
    K.roll_ccw = key(PortAction::PhotoRollLeft, 0);
    K.zoom_in = key(PortAction::PhotoWider, 0);
    K.zoom_in_alt = key(PortAction::PhotoWider, 1);
    K.zoom_out = key(PortAction::PhotoNarrower, 0);
    K.zoom_out_alt = key(PortAction::PhotoNarrower, 1);
    K.focus_far = key(PortAction::PhotoFocusFar, 0);
    K.focus_near = key(PortAction::PhotoFocusNear, 0);
    K.dof = key(PortAction::PhotoDof, 0);
    K.blur_less = key(PortAction::PhotoBlurLess, 0);
    K.blur_more = key(PortAction::PhotoBlurMore, 0);
    K.target = key(PortAction::PhotoTarget, 0);
    K.reset = key(PortAction::PhotoReset, 0);
    K.hide_ui = key(PortAction::PhotoHelp, 0);
    K.exit_key = key(PortAction::PhotoMode, 0);
    K.exit_alt = key(PortAction::PhotoExit, 0, SDL_SCANCODE_ESCAPE);   // (Escape always leaves too)
    K.shot = key(PortAction::Screenshot, 0);
    // a gamepad control: its glyph where the input sits on a PS2 button's place, else a chip with its name
    const auto padkey = [&](PortAction a, PhotoPadKey& out) {
        const auto& l = b.pad[std::size_t(a)];
        if (l.empty()) { out = {PadButton::kCross, "-"}; return; }
        if (l.size() == 1) {
            switch (l[0]) {
            case PadInput::South: out = {PadButton::kCross, {}}; return;
            case PadInput::East: out = {PadButton::kCircle, {}}; return;
            case PadInput::West: out = {PadButton::kSquare, {}}; return;
            case PadInput::North: out = {PadButton::kTriangle, {}}; return;
            case PadInput::LeftShoulder: out = {PadButton::kL1, {}}; return;
            case PadInput::RightShoulder: out = {PadButton::kR1, {}}; return;
            case PadInput::LeftTrigger: out = {PadButton::kL2, {}}; return;
            case PadInput::RightTrigger: out = {PadButton::kR2, {}}; return;
            case PadInput::LeftStick: out = {PadButton::kL3, {}}; return;
            case PadInput::RightStick: out = {PadButton::kR3, {}}; return;
            case PadInput::Start: out = {PadButton::kStart, {}}; return;
            case PadInput::Back: out = {PadButton::kSelect, {}}; return;
            default: break;
            }
        }
        std::string t;
        for (PadInput in : l) t += (t.empty() ? "" : "+") + b3native::input::pad_input_label(in, b3native::input::PadStyle::PlayStation);
        out = {PadButton::kCross, t};
    };
    padkey(PortAction::PhotoUp, K.pad_up);
    padkey(PortAction::PhotoDown, K.pad_down);
    padkey(PortAction::PhotoFast, K.pad_fast);
    padkey(PortAction::PhotoRollLeft, K.pad_roll_ccw);
    padkey(PortAction::PhotoRollRight, K.pad_roll_cw);
    padkey(PortAction::PhotoDof, K.pad_dof);
    padkey(PortAction::PhotoBlurCycle, K.pad_blur);
    padkey(PortAction::PhotoTarget, K.pad_target);
    padkey(PortAction::PhotoReset, K.pad_reset);
    padkey(PortAction::PhotoHelp, K.pad_hide);
    padkey(PortAction::PhotoExit, K.pad_exit);
    return K;
}

bool PhotoMode::key(std::uint16_t sc, bool down, bool repeat, unsigned mods) {
    using b3native::input::PortAction;
    std::lock_guard<std::mutex> l(mu_);
    // Ctrl / GUI held: the window's shortcuts (rebind2: unless that modifier is itself bound to a photo action)
    const auto mod_bound = [&](std::uint16_t a, std::uint16_t b) {
        return (b3native::input::port_actions_of_key(bind_, a) | b3native::input::port_actions_of_key(bind_, b)) != 0;
    };
    const bool viewer = ((mods & SDL_KMOD_CTRL) && !mod_bound(SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL)) ||
                        ((mods & SDL_KMOD_GUI) && !mod_bound(SDL_SCANCODE_LGUI, SDL_SCANCODE_RGUI));
    const std::uint64_t m = b3native::input::port_actions_of_key(bind_, sc);
    const auto has = [&](PortAction a) { return (m >> int(a) & 1) != 0; };
    if (!active_) {
        if (down && !repeat && !viewer && !(mods & SDL_KMOD_ALT) && has(PortAction::PhotoMode) && can_enter_locked()) {
            enter_locked(b3native::input::key_label(sc).c_str());
            return true;
        }
        return false;
    }
    if (sc < held_.size() && held_[sc] != (down && !viewer)) {
        held_[sc] = down && !viewer;
        record_locked(Event::kKey, sc, held_[sc]);
    }
    if (!down) return false;   // ups go on to the pad too (a key it saw go down before photo mode is released)
    if (viewer) return false;
    if (!repeat) {
        if (sc == SDL_SCANCODE_ESCAPE || has(PortAction::PhotoMode) || has(PortAction::PhotoExit))
            leave_locked(sc == SDL_SCANCODE_ESCAPE ? "Escape" : b3native::input::key_label(sc).c_str());
        else if (has(PortAction::PhotoHelp)) cycle_ui_locked();
        else if (dof_key(dof_, sc, bind_)) {
            std::printf("[b3photo] depth of field: %s\n", dof_.status(0).c_str());
            std::fflush(stdout);
        }
        else if (has(PortAction::PhotoReset)) {
            reset_ = true;
            record_locked(Event::kReset);
        }
    }
    return true;
}

void PhotoMode::set_bindings(const b3native::input::PortControls& b) {
    std::lock_guard<std::mutex> l(mu_);
    bind_ = b;
    keys_ = photo_keys_from(b);   // the controls strip shows what the bindings are
}

bool PhotoMode::handle(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return key(static_cast<std::uint16_t>(e.key.scancode), e.type == SDL_EVENT_KEY_DOWN, e.key.repeat, e.key.mod);
    case SDL_EVENT_MOUSE_MOTION: {
        std::lock_guard<std::mutex> l(mu_);
        if (!active_) return false;
        mouse_dx_ += e.motion.xrel;
        mouse_dy_ += e.motion.yrel;
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        std::lock_guard<std::mutex> l(mu_);
        if (!active_) return false;
        wheel_ += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        std::lock_guard<std::mutex> l(mu_);
        return active_;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST: {   // (not taken: the pad sees it too)
        std::lock_guard<std::mutex> l(mu_);
        held_.fill(false);
        return false;
    }
    default: return false;
    }
}

void PhotoMode::pad(const PhotoPad& p) {
    std::lock_guard<std::mutex> l(mu_);
    const bool both = p.toggle;   // rebind2: the photo mode chord (L3 + R3 by default)
    const bool combo = both && !pad_both_;
    pad_both_ = both;
    if (pad_latch_ && !p.any_button()) pad_latch_ = false;
    if (!active_) {
        if (combo && can_enter_locked()) enter_locked("L3 + R3");
        pad_prev_ = p;
        pad_ = p;
        return;
    }
    if (combo || (p.east && !pad_prev_.east)) {
        leave_locked(combo ? "L3 + R3" : "B / Circle");
        pad_latch_ = p.any_button();
        pad_prev_ = p;
        pad_ = PhotoPad{};
        return;
    }
    if (p.north && !pad_prev_.north) cycle_ui_locked();
    // photo2: the depth of field: Back on / off, Start the blur strength (cycling), L3 alone (released before the chord came)
    // the focus target
    if (p.back && !pad_prev_.back) dof_.toggle();
    if (p.start && !pad_prev_.start) dof_.blur(0);
    if (p.blur_less && !pad_prev_.blur_less) dof_.blur(-1);
    if (p.blur_more && !pad_prev_.blur_more) dof_.blur(+1);
    if (p.target && !pad_prev_.target) l3_solo_ = !p.toggle;
    if (p.toggle) l3_solo_ = false;
    if (!p.target && pad_prev_.target && l3_solo_) dof_.next_target();
    if (!p.target) l3_solo_ = false;
    if (p.west && !pad_prev_.west) {
        reset_ = true;
        record_locked(Event::kReset);
    }
    pad_prev_ = p;
    pad_ = p;
}

bool PhotoMode::active() const {
    std::lock_guard<std::mutex> l(mu_);
    return active_;
}

bool PhotoMode::holds_input() const {
    std::lock_guard<std::mutex> l(mu_);
    return active_ || pad_latch_;
}

void PhotoMode::advance(float dt, const Camera& game) {
    std::lock_guard<std::mutex> l(mu_);
    if (!active_ || !game.valid) return;
    if (!started_ || reset_) {
        cam_.from(game);
        const bool first = !started_;
        started_ = true;
        reset_ = false;
        mouse_dx_ = mouse_dy_ = wheel_ = 0;
        std::printf("[b3photo] camera %s the game's: (%.2f, %.2f, %.2f), field of view %.1f degrees\n", first ? "starts at" : "back to",
                    double(cam_.pos[0]), double(cam_.pos[1]), double(cam_.pos[2]), double(cam_.fov * 180.0f / kPi));
        std::fflush(stdout);
        return;
    }
    dt = std::clamp(dt, 0.0f, 0.25f);
    step(cam_, dof_, dt, held_, pad_, mouse_dx_, mouse_dy_, wheel_, bind_);
    mouse_dx_ = mouse_dy_ = wheel_ = 0;
}

bool PhotoMode::camera(const Camera& game, Camera& out) const {
    std::lock_guard<std::mutex> l(mu_);
    if (!active_ || !started_) return false;
    out = cam_.camera(game);
    return true;
}

void PhotoMode::record_locked(Event::Kind kind, std::uint16_t scancode, bool down) {
    if (game_time_) events_.push_back({game_poll_ + 1, kind, scancode, down});
}

void PhotoMode::set_game_poll(std::uint32_t poll) {
    std::lock_guard<std::mutex> l(mu_);
    game_poll_ = poll;
}

void PhotoMode::enable_game_time() {
    std::lock_guard<std::mutex> l(mu_);
    game_time_ = true;
}

std::uint32_t PhotoMode::game_time_poll() const {
    std::lock_guard<std::mutex> l(mu_);
    return replay_.poll;
}

void PhotoMode::advance_to(std::uint32_t poll, const std::function<bool(std::uint32_t, Camera&)>& game_cam) {
    std::lock_guard<std::mutex> l(mu_);
    if (!game_time_) return;
    Replay& r = replay_;
    const PhotoPad no_pad{};
    while (true) {
        // the events that apply from this poll on (all of them are here: an event stamped p was recorded before poll p
        // was handed over, and the caller has drained it)
        for (; !events_.empty() && events_.front().poll <= r.poll; events_.pop_front()) {
            const Event& e = events_.front();
            switch (e.kind) {
            case Event::kEnter:
                r.active = true;
                r.started = false;
                r.start_poll = e.poll;
                r.held.fill(false);
                std::printf("[b3photo] game time: photo mode on at poll %u\n", e.poll);
                break;
            case Event::kLeave:
                r.active = r.started = false;
                r.held.fill(false);
                std::printf("[b3photo] game time: photo mode off at poll %u\n", e.poll);
                break;
            case Event::kKey:
                if (r.active && e.scancode < r.held.size()) {
                    if (e.down && !r.held[e.scancode] && dof_key(r.dof, e.scancode, bind_))   // photo2: its keys at their poll
                        std::printf("[b3photo] game time: depth of field at poll %u: %s\n", e.poll, r.dof.status(0).c_str());
                    r.held[e.scancode] = e.down;
                }
                break;
            case Event::kReset:
                if (r.active) {
                    r.started = false;
                    r.start_poll = e.poll;
                }
                break;
            }
            std::fflush(stdout);
        }
        if (r.active && !r.started) {   // from the game's camera at the entry (R: at the reset)
            Camera g;
            if (game_cam(r.start_poll, g) && g.valid) {
                r.cam.from(g);
                r.started = true;
                std::printf("[b3photo] game time: the camera starts at the game's of poll %u: (%.2f, %.2f, %.2f), field of view %.1f degrees\n",
                            r.start_poll, double(r.cam.pos[0]), double(r.cam.pos[1]), double(r.cam.pos[2]), double(r.cam.fov * 180.0f / kPi));
                std::fflush(stdout);
            }
        }
        if (r.poll >= poll) break;
        if (r.active && r.started) {   // a poll of game time with the keys held in it
            step(r.cam, r.dof, 1.0f / 60.0f, r.held, no_pad, 0.0f, 0.0f, 0.0f, bind_);
            ++r.poll;
        } else {   // nothing moves: on to the next event
            r.poll = events_.empty() ? poll : std::min(poll, std::max(events_.front().poll, r.poll + 1));
        }
    }
}

bool PhotoMode::camera_at(std::uint32_t poll, const Camera& game, Camera& out) const {
    std::lock_guard<std::mutex> l(mu_);
    if (!game_time_ || replay_.poll != poll || !replay_.active || !replay_.started) return false;
    out = replay_.cam.camera(game);
    return true;
}

void PhotoMode::dof(const Camera& cam, const float* car, const float* view_proj, b3gfx::FrameParams::Dof& out) {
    std::lock_guard<std::mutex> l(mu_);
    if (!active_) return;
    out = dof_.params(cam, car, view_proj, &subject_);
}

bool PhotoMode::dof_at(std::uint32_t poll, const Camera& cam, const float* car, const float* view_proj, b3gfx::FrameParams::Dof& out) const {
    std::lock_guard<std::mutex> l(mu_);
    if (!game_time_ || replay_.poll != poll || !replay_.active || !replay_.started) return false;
    out = replay_.dof.params(cam, car, view_proj);
    return true;
}

PhotoDof PhotoMode::dof_state() const {
    std::lock_guard<std::mutex> l(mu_);
    return dof_;
}

std::vector<std::string> PhotoMode::help_lines(bool pad_prompts, int fov_deg, const PhotoDof& dof, int subject) {
    std::vector<std::string> lines{"PHOTO MODE   FOV " + std::to_string(fov_deg) + "   " + dof.status(subject)};
    if (pad_prompts) {
        lines.push_back("MOVE L STICK   UP/DOWN RT/LT   LOOK R STICK   FAST HOLD A/CROSS");
        lines.push_back("FOV D-PAD   ROLL LB/RB   RESET X/SQUARE   HIDE HELP Y/TRIANGLE   EXIT B/CIRCLE");
        lines.push_back("DEPTH OF FIELD BACK   BLUR START   FOCUS D-PAD LEFT/RIGHT   FOCUS ON CAR/CENTRE L3");
    } else {
        lines.push_back("MOVE WASD   UP/DOWN E/Q   LOOK MOUSE OR ARROWS   FAST SHIFT   SLOW ALT");
        lines.push_back("FOV WHEEL OR -/+   ROLL Z/C   RESET R   HIDE HELP H   SCREENSHOT F12   EXIT P OR ESC");
        lines.push_back("DEPTH OF FIELD F   BLUR V/B   FOCUS NEAR/FAR N/M   FOCUS ON CAR/CENTRE G");
    }
    return lines;
}

void PhotoMode::cycle_ui_locked() {
    ui_ = photo_ui_next(ui_);
    std::printf("[b3photo] photo UI: %s\n", ui_ == PhotoUi::kFull ? "full" : ui_ == PhotoUi::kReadouts ? "readouts" : "hidden");
    std::fflush(stdout);
}

PhotoUi PhotoMode::ui() const {
    std::lock_guard<std::mutex> l(mu_);
    return ui_;
}

void PhotoMode::draw_help(b3gfx::Device& dev, int w, int h, float dt, PromptDevice device, int hud_size, std::vector<b3gfx::DrawItem>& out) {
    PhotoHudData data;
    PhotoUi ui;
    PhotoKeys keys;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (!active_) return;
        if (ui_fresh_) {
            hud_.restart();
            ui_fresh_ = false;
        }
        ui = ui_;
        keys = keys_;
        data.fov_deg = float(std::lround(cam_.fov * 180.0f / kPi));
        data.roll_deg = cam_.roll * 180.0f / kPi;
        data.dof_on = dof_.on;
        data.dof_level = dof_.level;
        data.dof_levels = PhotoDof::kLevels;
        data.focus_car = subject_ == 1 || (subject_ == 0 && dof_.target == 0);
        data.focus_stops = dof_.stops;
    }
    hud_.draw_list(dev, w, h, dt, ui, data, device, keys, hud_size, out);
}

}  // namespace b3replay
