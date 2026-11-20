// menumouse.h -- the mouse in the game's own menus, the renderer's side (mac5, 2026-11-09; docs/remaster-features.md §8;
// what crosses to the game: include/b3live/menu_input.h).
//
// Where the menus are on screen comes from the frame as drawn: View::hud_list keeps every 2D draw's pieces (quads) in
// output fractions after the wide layout (hud_draw_pieces), and native_play's 'HMN1' record names each menu's row text
// objects (CB3VSelectComponent +0x14), which HudFrame::objects carry as the draws' objects. So a row's place is the union
// of its draws, whatever layout moved them. A row's hit area is its band across the menu column (the rows' x extent,
// split between neighbouring rows at their midpoints). The prompt bar's buttons are the draws of the "Buttons" texture
// whose symbol says which button (prompts.cpp's table), each with the label text beside it.
//
// MenuMouse turns the window's mouse into actions for the game thread (b3r_menu_actions): hovering a row (on a move)
// selects it, a left click selects it and presses Cross, a click on a prompt presses its button, a left click elsewhere
// on a menu page without rows presses Cross (continue screens; not on the name keyboard), a right click presses
// Triangle (the menus' Back), the wheel presses Up / Down. garagecam1 (garagecam.h): on a car page with GARAGE CAMERA on,
// that click elsewhere is taken on the release instead, and only when the pointer moved less than kDragStart while held;
// past it the press is a drag, which turns the view round the car (GarageCam::add_drag) and presses nothing. The cursor shows on a move and hides after kHideAfter seconds
// or when a key or a pad is used. While the PC SETTINGS page is open it gets the pointer instead (page_input).
#ifndef B3REPLAY_MENUMOUSE_H
#define B3REPLAY_MENUMOUSE_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "b3live/menu_input.h"
#include "hud.h"
#include "prompts.h"

namespace b3replay {

// A box in output fractions (0..1 across the window, y down)
struct ScreenBox {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    bool empty() const { return x1 < x0 || y1 < y0; }
    void add(float x, float y) { x0 = x < x0 ? x : x0; y0 = y < y0 ? y : y0; x1 = x > x1 ? x : x1; y1 = y > y1 ? y : y1; }
    void add(const ScreenBox& b) { if (!b.empty()) { add(b.x0, b.y0); add(b.x1, b.y1); } }
    bool contains(float x, float y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    float cx() const { return 0.5f * (x0 + x1); }
    float cy() const { return 0.5f * (y0 + y1); }
};

struct MenuRowTarget { std::uint32_t select = 0; std::uint32_t row = 0; ScreenBox box; };
struct MenuPromptTarget { std::uint8_t button = 0; ScreenBox box; };   // b3live::pad index
struct MenuTargets {
    std::vector<MenuRowTarget> rows;        // hit areas (bands across the column)
    std::vector<MenuPromptTarget> prompts;  // button + label
    bool menu = false;                      // a menu page (rows, or the front end's prompt bar)
    bool text_entry = false;                // the on-screen keyboard takes typed text
    bool car = false;                       // garagecam1: a car page with GARAGE CAMERA on (a left drag turns the view)
    const MenuRowTarget* row_at(float x, float y) const;
    const MenuPromptTarget* prompt_at(float x, float y) const;
    std::uint32_t index_of(std::uint32_t select) const;   // the select's current row (the record's), ~0u unknown
    std::vector<std::pair<std::uint32_t, std::uint32_t>> current;   // (select, row) from the record
};

// The rows' hit bands from their drawn boxes (one select's rows, by row index; an empty box: not drawn). `pad`: how far
// the column reaches past the rows' x extent (output fractions).
std::vector<MenuRowTarget> menu_row_bands(std::uint32_t select, const std::vector<ScreenBox>& rows, float pad);

// A row's text pieces as one box, dropping lines drawn above it in the same batched draw (a popup's question above YES)
ScreenBox last_text_run(std::vector<ScreenBox> pieces);

// The prompts from the frame's button draws: `button_of(draw, piece)` names the pad button a piece shows (-1 none; the
// ring is -2: part of a button, which one by its symbol), `text(draw)` says whether a draw is text (a font's glyphs).
// A label is the run of text pieces on the button's row starting right of it (gaps under a button's height).
std::vector<MenuPromptTarget> menu_prompts(const HudFrame& f, const std::vector<std::vector<ScreenBox>>& pieces,
                                           const std::function<int(const HudDraw&, std::size_t piece)>& button_of,
                                           const std::function<bool(const HudDraw&)>& text);

// Everything clickable in a displayed frame. `prompt_bar`: the frame shows the front end's prompt bar (a menu page).
MenuTargets menu_targets(const HudFrame& f, const std::vector<std::vector<ScreenBox>>& pieces, const b3live::MenuRecord* rec,
                         bool prompt_bar, const std::function<int(const HudDraw&, std::size_t piece)>& button_of,
                         const std::function<bool(const HudDraw&)>& text);

// The same for a frame laid out by View::hud_list (its hud_draw_pieces), the textures naming the button and font draws
MenuTargets menu_targets_of(const HudFrame& f, const std::vector<std::vector<ScreenBox>>& pieces, const b3live::MenuRecord* rec,
                            const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures);

class MenuMouse {
public:
    static MenuMouse& instance();
    static constexpr double kHideAfter = 3.0;   // seconds without a move
    static constexpr float kDragStart = 0.012f;  // garagecam1: a held left button moved this far (window fractions) drags

    // Any thread (the window's events, scripted ones): positions in output fractions; b 1 left, 3 right
    void move(float x, float y, double t);
    void button(int b, bool down, double t);
    void wheel(int steps, double t);   // > 0 away from the user (up)
    void other_device(double t);       // a key or a pad was used: the cursor hides, the hover rests

    // Render thread, once per displayed frame: the events since the last call against the frame on screen.
    // `page_open`: the PC SETTINGS page has the pointer (the game gets nothing).
    void frame(const MenuTargets& t, bool page_open, double now);
    bool cursor_wanted(double now) const;   // the window shows the system cursor
    // settingsui: the mouse was used after the last key or pad press (the PC SETTINGS page's prompt bar shows its words)
    bool used_last() const {
        std::lock_guard<std::mutex> l(mu_);
        return have_pos_ && !hidden_by_device_;
    }
    bool pending() const;                   // events not yet taken by frame()

    // The PC SETTINGS page's pointer (b3settings::Input; picture pixels from fractions by the caller's mapping)
    struct PagePointer { bool have = false; float x = 0, y = 0; bool moved = false, click = false, held = false, back = false; int wheel = 0; };
    PagePointer page_pointer();         // render thread, after frame(): this frame's (edges cleared on read)

    // Game thread: the actions for the game (3 u32 each, b3live::menu_action_put); returns how many
    int take(std::uint32_t* out, int max);

    std::uint64_t actions_sent() const { return sent_; }

private:
    struct Event { enum Kind { kMove, kDown, kUp, kWheel } kind; float x, y; int n; };
    void emit(const b3live::MenuAction& a);
    mutable std::mutex mu_;
    std::vector<Event> events_;
    std::vector<b3live::MenuAction> out_;
    float x_ = -1, y_ = -1;
    bool have_pos_ = false;
    double last_move_ = -1e9;
    bool hidden_by_device_ = true;
    std::uint32_t hover_select_ = 0, hover_row_ = ~0u;
    double hover_at_ = -1e9;
    PagePointer page_;
    std::uint64_t sent_ = 0;
    bool drag_held_ = false, drag_moved_ = false;   // garagecam1: a left press on a car page, held; moved past kDragStart
    std::vector<std::uint32_t> drag_page_;          // the press's page: its selects (MenuTargets::current)
    float drag_dx_ = 0, drag_dy_ = 0;               // its travel before it counted as a drag
};

}  // namespace b3replay

#endif
