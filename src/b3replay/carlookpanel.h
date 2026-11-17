// carlookpanel.h -- the live tuner of the enhanced car reflections' look (reflect2; docs/car-look.md). A small overlay at
// the right of the window, shown while PC SETTINGS > INTERFACE > DEVELOPER MODS is on (never otherwise). The values are
// carlook.h's (port renderer values, not the game's variables: the tuning console's registry is not touched); a change is
// drawn on the next frame on every car, in a race, paused or in photo mode.
//
//   Keyboard (the key is the port action CAR LOOK, F8 by default, rebindable on the controls page): the key opens and
//     closes the panel; Up / Down choose a value; Left / Right change it (Shift x10, Ctrl or Alt a tenth); R or Home puts
//     back its default; Shift+R (Ctrl+R) every value; S saves car_look.ini; C copies the values as code (the log, the
//     screen, car_look_values.txt and the clipboard); Escape closes. The mouse: the wheel and a click choose a row.
//   Controller: D-pad up / down choose, left / right change (R2 held x10, L2 held a tenth), Square resets the value,
//     Select every value, Cross saves, Triangle copies as code, Circle or Start closes. While it is open the game sees
//     a neutral pad (b3r_settings_active).
#ifndef B3REPLAY_CARLOOKPANEL_H
#define B3REPLAY_CARLOOKPANEL_H

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "b3gfx/b3gfx.h"

namespace b3replay {

class CarLookPanel {
public:
    static CarLookPanel& instance();
    void set_enabled(bool developer_mods);          // off: closed and its key does nothing
    bool enabled() const { std::lock_guard<std::mutex> l(mu_); return enabled_; }
    bool open() const { std::lock_guard<std::mutex> l(mu_); return open_; }
    bool handle(const SDL_Event& e);                // window thread: true when taken
    void pad(const std::array<std::uint8_t, 32>& report);
    void draw_list(b3gfx::Device& dev, int w, int h, std::vector<b3gfx::DrawItem>& out);

    // the panel's lines (tests and the drawing)
    struct Line { std::string label, value, def; bool cursor = false, changed = false, heading = false; };
    std::vector<Line> lines();
    // the actions (the keys' and the pad's), also for tests; `mult` 1, 10, -10 (a tenth)
    void move(int dir);
    void change(int dir, int mult);
    void reset_value();
    void reset_all();
    std::string save();           // the status text
    std::string copy_as_code();   // likewise
    std::string status();
    void open_now(bool on) { std::lock_guard<std::mutex> l(mu_); open_ = on && enabled_; }

private:
    CarLookPanel() = default;
    mutable std::mutex mu_;
    bool enabled_ = false, open_ = false, pad_seen_ = false;
    int cursor_ = 0;
    std::string status_;
    std::array<std::uint8_t, 32> last_pad_{};
    struct Hold { bool held = false; float time = 0, next = 0; bool step(bool now, float dt); } hold_[4];
    std::uint64_t pad_ns_ = 0;
    // the box
    std::string built_;
    b3gfx::TextureId tex_ = b3gfx::kNone;
    b3gfx::BufferId vb_ = b3gfx::kNone, ib_ = b3gfx::kNone;
    b3gfx::ResourceEpoch epoch_;   // a track load clears the ids above (Device::resource_epoch)
    int tex_w_ = 0, tex_h_ = 0;
    struct Geo { bool valid = false; float x = 0, y = 0, w = 0, h = 0, pitch = 1, list_y = 0; } geo_;
    void change_locked(int dir, int mult);
    void reset_value_locked();
    void reset_all_locked();
    std::string save_locked();
    std::string copy_locked();
    std::vector<Line> lines_locked();
};

}  // namespace b3replay

#endif
