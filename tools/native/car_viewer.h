// car_viewer.h -- gamemods1 (2026-11-21): the CAR VIEWER, the retail game's leftover debug game mode
// (CB3CarViewerGameMode, gCarViewerGameMode 0x0051A9E0), opened from YOUR GARAGE.
//
// The game's mode [C decomp names, retail asm]: one-player choice 4 of CB3MenuChoices (0x01E900B0 +0x04) with the car
// id at +0x80; RequestNewGameMode__14CB3MenuChoices 0x30D1B0 starts it. Its Prepare func_0024FC20 / Update 0x24DAB0 /
// Release func_0024D4D0 / Render 0x24D520 (the vtable at 0x4DE0C0). Update reads the player's controls: Select back to
// the menus, Start the colour, L1 / R1 / Cross / Square / Triangle / Circle the car's lights, L2 / R2 the wheels, L3 the
// debug drawing, R3 / D-pad down the deformation, D-pad up the hinged parts.
//
// What it needs before it runs in the retail game (the port's set-up around it):
// - The way in: on YOUR GARAGE (CB3CarRewardsGaragePage, Update func_003F03D0) while the page takes input
//   (D_01E90424 == 5): the menu state to come back to into CB3FrontEnd +0x300 (the FSM 0x01EA2970 +4's state +0), the
//   page released (ReleaseCurrentMenu__18CB3MenuPageManager 0x30DD70), the carousel's car (func_0042B700, carousel
//   0x01E8EC30 +0x244) as choice +0x80, choice 4, RequestNewGameMode. On Select (Tab on the keyboard), or L2 + R2 + Square (the game's own
//   combination test is func_0022C780; the port reads player 1's report instead, native_play: pad(), so the garage's frames make
//   no extra call: with the combo never pressed the game runs exactly as before).
// - Prepare: the player's race car 0x0064D3B0 is not set up for this mode: its body pointer (+0x2430 = the physics
//   manager's first player body 0x0057BAF0, whose +0x13F4 points back to the car), its lighting (+0x5C, +0x60..+0x7F:
//   the irradiance terms, here a plain soft light of our own) and the boost flame (+0x11A0 = 0). Shadows, bloom and
//   blur (graphics manager 0x00665EC0 +0x67 / +0x68 / +0x6A) are off while it runs and put back at Release.
// - Update: the sound game mode manager (0x01E77540 +4, its prepare state) is held at 0 while the mode runs (its updates
//   would read the race's sound objects); its whole value as the first Prepare found it is put back at Release, and so
//   are CB3MenuChoices +4 and +0x80 as the way in found them. Leaving runs nothing of the game's but the mode's Release
//   and the front end's own Prepare (no records, no profile counts, no autosave: nahelam1-shots profile diff).
// - Replays: a request to open it (the garage's Select / combination, from the pad) is an event of the recording
//   ("car_viewer", native_play); playback opens it on those events only (a recording without them never does).
// Two bindings of the port's (the D-pad's left and right are free there): D-pad left the background colour (drawn by the
// renderer: src/b3replay/view.cpp car_viewer_background; each frame b3gv::car_viewer_frame hands it the car), D-pad
// right puts the deformation back (CB3VehicleDeform::Reset 0x3DAEE0 on car +0x380 with the car and +0x80).
#ifndef B3NATIVE_CAR_VIEWER_H
#define B3NATIVE_CAR_VIEWER_H

#include "hook_table.h"

#include "b3gameview/gameview.h"

#include "b3host/call.h"
#include "b3host/host.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace b3native {

class CarViewer {
public:
    static constexpr b3host::gaddr kGarageUpdate = 0x003F03D0u, kPrepare = 0x0024FC20u, kUpdate = 0x0024DAB0u, kRelease = 0x0024D4D0u;
    static constexpr b3host::gaddr kReleaseMenu = 0x0030DD70u, kEntryCarId = 0x0042B700u,
                                   kRequestMode = 0x0030D1B0u, kDeformReset = 0x003DAEE0u, kPadControl = 0x0012FC30u;
    static constexpr b3host::gaddr kInput = 0x004F50C0u, kActivePad = 0x004F6061u, kPageManager = 0x01E90240u,
                                   kPageState = 0x01E90424u, kFsm = 0x01EA2970u, kFrontEnd = 0x0051A6A8u, kCarousel = 0x01E8EC30u,
                                   kChoices = 0x01E900B0u, kCar = 0x0064D3B0u, kBody = 0x0057BAF0u, kGraphics = 0x00665EC0u,
                                   kSoundModes = 0x01E77540u, kCurrentMode = 0x0051BA88u, kViewerMode = 0x0051A9E0u;

    using VblankFn = std::uint64_t (*)();
    bool install(HookTable& hooks, VblankFn vblank) {
        for (b3host::gaddr a : {kGarageUpdate, kPrepare, kUpdate, kRelease})
            if (!hooks.has_row(a)) {
                std::fprintf(stderr, "car_viewer: %08X is not native: no car viewer\n", a);
                return false;
            }
        vblank_ = vblank;
        if (const char* e = std::getenv("B3NATIVE_CAR_VIEWER_AT"); e && *e) open_at_ = std::strtoull(e, nullptr, 10);
        std::string error;
        std::vector<Hook> specs = {{kGarageUpdate, HookLayer::kPort, &garage, &next_garage_},
                                   {kPrepare, HookLayer::kPort, &prepare, &next_prepare_},
                                   {kUpdate, HookLayer::kPort, &update, &next_update_},
                                   {kRelease, HookLayer::kPort, &release, &next_release_}};
        if (!hooks.install("port:car_viewer", specs, &error)) {
            std::fprintf(stderr, "car_viewer: %s\n", error.c_str());
            return false;
        }
        instance() = this;
        return true;
    }
    bool running() const { return running_; }
    // native_play, each VBlank: player 1's report (bytes 0-1 buttons active low: L2 0x01, R2 0x02, Square 0x80 of byte 1;
    // Select 0x01 of byte 0, which YOUR GARAGE does not read: its Update func_003F03D0 asks Back and the D-pad only)
    // Returns true on a new request (native_play records it: "car_viewer"; playback calls step() with the recording's).
    bool pad(const unsigned char* s) {
        // L2 + R2 + Square when pressed; Select when let go (the viewer's own Select goes back, so it must not see this one
        // still held). Either is kept for a few VBlanks for the garage's next Update (it runs every frame), then dropped
        const bool down = s && !(s[1] & 0x01) && !(s[1] & 0x02) && !(s[1] & 0x80);
        const bool sel = s && !(s[0] & 0x01);
        const bool fresh = (down && !combo_down_) || (!sel && select_down_);
        combo_down_ = down;
        select_down_ = sel;
        step(fresh);
        return fresh;
    }
    void step(bool request) {
        if (request) combo_ttl_ = 4;
        else if (combo_ttl_ > 0) --combo_ttl_;
        combo_ = combo_ttl_ > 0;
    }
    int background() const { return background_; }

private:
    static CarViewer*& instance() { static CarViewer* p = nullptr; return p; }
    static void say(const char* what) { std::printf("car_viewer: %s\n", what); std::fflush(stdout); }
    static float control(int which) {   // CB3ControllerMapping::GetPadControl for the active pad
        const int pad = b3host::load<std::int8_t>(kActivePad);
        const float f = b3host::call_guest<float>(kPadControl, kInput + 300u * std::uint32_t(pad < 0 ? 0 : pad) + 0xD48u, which, 0);
        return f < 0 ? -f : f;
    }
    static void garage(const b3host::CallArgs& c, b3host::CallResult& r) {
        CarViewer& m = *instance();
        if (b3host::load<std::uint32_t>(kPageState) == 5) {
            const bool timed = m.open_at_ && m.vblank_ && m.vblank_() >= m.open_at_;
            if (timed || m.combo_) {
                m.open_at_ = 0;
                m.combo_ = false;
                m.combo_ttl_ = 0;
                m.enter();
                return;
            }
        }
        m.next_garage_(c, r);
    }
    void enter() {
        choice_ = b3host::load<std::uint32_t>(kChoices + 4u);   // (put back at Release)
        choice_car_ = b3host::load<std::uint64_t>(kChoices + 0x80u);
        entered_ = true;
        sound_saved_ = false;
        const b3host::gaddr state = b3host::load<std::uint32_t>(kFsm + 4u);
        if (state) b3host::store<std::uint64_t>(kFrontEnd + 0x300u, b3host::load<std::uint64_t>(state));   // where to come back
        b3host::call_guest<void>(kReleaseMenu, kPageManager);
        const std::uint32_t entry = b3host::load<std::uint32_t>(kCarousel + 0x244u);
        const std::uint64_t car = b3host::call_guest<std::uint64_t>(kEntryCarId, kCarousel, entry);
        b3host::store<std::uint64_t>(kChoices + 0x80u, car);
        b3host::store<std::uint32_t>(kChoices + 4u, 4u);   // the car viewer
        b3host::call_guest<void>(kRequestMode, kChoices);
        char t[96];
        std::snprintf(t, sizeof t, "opened from YOUR GARAGE (car %016llx)", static_cast<unsigned long long>(car));
        say(t);
    }
    static void prepare(const b3host::CallArgs& c, b3host::CallResult& r) {
        CarViewer& m = *instance();
        m.next_prepare_(c, r);
        if (m.entered_ && !m.sound_saved_) {   // the sound manager's state as the mode found it (put back at Release)
            m.sound_ = b3host::load<std::uint32_t>(kSoundModes + 4u);
            m.sound_saved_ = true;
        }
        if ((r.v0.w[0] & 0xFFu) == 0) return;   // not prepared yet (it is called again)
        // the car's lighting: a soft white light from above (the irradiance terms the car's shader reads), our own values
        const float k = 0.6f;
        const float terms[9] = {0.55f, 0.0f, 0.18f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.12f};
        std::uint32_t w;
        std::memcpy(&w, &k, 4);
        b3host::store<std::uint32_t>(kCar + 0x5Cu, w);
        for (int i = 0; i < 8; ++i) { std::memcpy(&w, &terms[i], 4); b3host::store<std::uint32_t>(kCar + 0x60u + 4u * i, w); }
        b3host::store<std::uint32_t>(kCar + 0x11A0u, 0);   // the boost flame
        b3host::store<std::uint32_t>(kCar + 0x2430u, kBody);
        b3host::store<std::uint32_t>(kBody + 0x13F4u, kCar);
        for (int i = 0; i < 3; ++i) {
            const std::uint32_t off = i == 0 ? 0x67u : i == 1 ? 0x68u : 0x6Au;
            m.gfx_[i] = b3host::load<std::uint8_t>(kGraphics + off);
            b3host::store<std::uint8_t>(kGraphics + off, 0);
        }
        m.running_ = true;
        m.background_ = 0;
        say("running (Select: back to YOUR GARAGE; D-pad left: background; D-pad right: deformation reset)");
    }
    static void update(const b3host::CallArgs& c, b3host::CallResult& r) {
        CarViewer& m = *instance();
        if (b3host::load<std::uint32_t>(kSoundModes + 4u) != 0) b3host::store<std::uint32_t>(kSoundModes + 4u, 0);
        m.next_update_(c, r);
        if (!m.running_) return;
        const bool right = control(7) > 0.5f, left = control(6) > 0.5f;
        if (right && !m.right_) {
            b3host::call_guest<void>(kDeformReset, kCar + 0x380u, kCar, kCar + 0x80u);
            say("deformation reset");
        }
        if (left && !m.left_) {
            m.background_ = (m.background_ + 1) % 15;
            char t[48];
            std::snprintf(t, sizeof t, "background %d", m.background_);
            say(t);
        }
        m.right_ = right;
        m.left_ = left;
        b3gv::car_viewer_frame(kCar, m.background_);   // the car and its background to the renderer (hud_capture.cpp)
    }
    static void release(const b3host::CallArgs& c, b3host::CallResult& r) {
        CarViewer& m = *instance();
        if (m.running_) {
            const std::uint32_t offs[3] = {0x67u, 0x68u, 0x6Au};
            for (int i = 0; i < 3; ++i) b3host::store<std::uint8_t>(kGraphics + offs[i], m.gfx_[i]);
            if (m.sound_saved_) b3host::store<std::uint32_t>(kSoundModes + 4u, m.sound_);
            if (m.entered_) {
                b3host::store<std::uint32_t>(kChoices + 4u, m.choice_);
                b3host::store<std::uint64_t>(kChoices + 0x80u, m.choice_car_);
            }
            m.running_ = false;
            char t[128];
            std::snprintf(t, sizeof t, "closed (the sound state %u, the menu choice %u and its car %016llx put back)", m.sound_, m.choice_,
                          static_cast<unsigned long long>(m.choice_car_));
            say(t);
            m.entered_ = m.sound_saved_ = false;
        }
        m.next_release_(c, r);
    }

    HookEntry next_garage_ = nullptr, next_prepare_ = nullptr, next_update_ = nullptr, next_release_ = nullptr;
    VblankFn vblank_ = nullptr;
    std::uint64_t open_at_ = 0;
    bool running_ = false, left_ = false, right_ = false, combo_ = false, combo_down_ = false, select_down_ = false;
    int combo_ttl_ = 0;
    bool entered_ = false, sound_saved_ = false;
    std::uint32_t sound_ = 0, choice_ = 0;
    std::uint64_t choice_car_ = 0;
    int background_ = 0;
    std::uint8_t gfx_[3] = {1, 1, 1};
};

}  // namespace b3native

#endif
