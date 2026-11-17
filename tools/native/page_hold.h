// page_hold.h -- when native_play withholds the pad from the game for the PC SETTINGS page, on the game's VBlank grid
// (online-det, 2026-11-11).
//
// The page opens and closes on the render thread (src/b3replay/pcpage.cpp PcPage::frame): it takes the open request at
// its next frame, and closes on the keys it reads there. native_play used to show the game a neutral pad exactly while
// the render thread reported the page open (b3r_settings_active), so the VBlanks the game saw the window's pad around
// the page depended on the two threads' speeds: the `online` tour's Cross, still held when SETTINGS was confirmed
// (VBlank 20001), reached the game until the render thread's next frame: 1 to 4 VBlanks seen, each one 800 virtual EE
// cycles later on (the tour was not deterministic from VBlank 20002); and the Triangle that closed the page reached
// the game from the close until it was released.
//
// Now the game's side is a function of the VBlank-stamped pad samples alone: withheld from the VBlank the page is
// requested, until the render thread has shown it open, then closed, and the window's pad has since been seen
// released (no button, sticks within the page's dead zone; as photo mode withholds the buttons held at its exit). The
// render thread's timing can only change what the game sees if it is still behind when the next key after the close
// comes.
#ifndef B3NATIVE_PAGE_HOLD_H
#define B3NATIVE_PAGE_HOLD_H

#include <cstdint>

namespace b3native {

class PageHold {
public:
    // A DualShock 2 report (platform_dbc.cpp layout: buttons in bytes 0-1, active low; sticks in 2-5, 0x80 centre) with
    // no button down and the sticks within 0x40 of the centre (the page takes a stick past that as the D-pad).
    static bool released(const std::uint8_t* s) {
        if (s[0] != 0xFF || s[1] != 0xFF) return false;
        for (int k = 2; k < 6; ++k)
            if (s[k] < 0x80 - 0x40 || s[k] > 0x80 + 0x40) return false;
        return true;
    }
    // The page was requested this VBlank (b3r_settings_open returned 1).
    void opened() { state_ = kRequested; }
    // Once per VBlank, after opened(): `active` the render thread's b3r_settings_active, `window` this VBlank's pad
    // sample from the window (null: none). True while the game must see a neutral pad.
    bool step(bool active, const std::uint8_t* window) {
        if (state_ == kRequested && active) state_ = kShown;
        if (state_ == kShown && !active) state_ = kClosed;
        if (state_ == kClosed && (!window || released(window))) state_ = kIdle;
        return state_ != kIdle || active;
    }
    bool holding() const { return state_ != kIdle; }

private:
    enum State : std::uint8_t { kIdle, kRequested, kShown, kClosed };
    State state_ = kIdle;
};

}  // namespace b3native

#endif
