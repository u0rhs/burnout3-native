// tuneconsole.h -- the tuning console in the native renderer (tune4, mod kit part 4: docs/modding/tuning.md). The
// game's side (the registry, the writes, the recording) is native_play's (tools/native/tuning_console.h); it hands the
// renderer its functions once (b3r_set_tune_hooks, include/b3tune/tune_abi.h). Two views of the same variables, both
// browsing the same tree (tunenav1, include/b3tune/browse.h): category tabs (YOUR CAR, MOST USED, HANDLING, ENGINE &
// SPEED ... OTHER), then sections and subsections, then the values; a page is a few dozen rows at most.
//
//   - the F9 overlay (any time in the window, a race included): a box at the left with the tab strip, where you are
//     (breadcrumbs), the page's rows around a cursor, the focused row's help and the keys. Keyboard: Tab / Shift+Tab or
//     Q / E the tabs; Up / Down (Page Up / Page Down a page); Right or Enter opens a section, Left or Backspace goes
//     back up; on a value Left / Right change it (Shift x10, Ctrl or Alt a tenth), Home or R puts back its default,
//     F pins / unpins it as a favourite, Enter on a value shown away from its place (YOUR CAR, CHANGED, MOST USED, a
//     search) goes there; Shift+R (Shift+Home) resets the page's changed values (RESET SECTION), Ctrl+R (Ctrl+Home)
//     every changed value (RESET ALL); both ask first when they would reset more than one value (Enter: yes, any
//     other key: no); / or Ctrl+F searches by name across all tabs; F9 or Escape closes. A controller: the D-pad
//     (held: repeats), L1 / R1 the tabs, Cross opens / goes there, Circle back (at the top: closes), Triangle pins,
//     Square resets, R2 + Square the section, Select all (Cross: yes, any other button: no), R2 held x10, L2 held a
//     tenth, Start closes. The key lines follow the device last used. While it is open the game sees a neutral pad (as
//     for PC SETTINGS: b3r_settings_active), so the keys never reach the race.
//   - tunenav2: the CHANGED tab (after YOUR CAR) lists every value that differs from its default now (native_play's
//     B3TuneHooks::changes: the game's value after the mods' presets), each with its default, and the car page's
//     FREEZE DAMAGE, INFINITE BOOST and boost use rate while the console's; a changed row is marked `*` (in the
//     accent colour, with "(default X)"), and tabs, sections and subsections show how many changed values they hold.
//   - the DEVELOPER tab of PC SETTINGS (pcpage.cpp), shown while PC SETTINGS > INTERFACE > DEVELOPER MODS is on and
//     the console is in this run: SAVE AS PRESET, a status line, then the same tree one page at a time (a category
//     list, then BACK and the page's rows); CONFIRM or Right opens, Left on a section or BACK goes up, on a value
//     Left / Right step it and CONFIRM puts back its default. RESET ALL (and RESET THIS PAGE in a page) ask for a
//     second CONFIRM; the same markers and counts as the overlay.
//
// Every edit goes to native_play (B3TuneHooks::edit / car_edit / reset), which writes it at the next VBlank and records
// it (a reset as an edit with the default's word).
// Nothing here touches the game. With DEVELOPER MODS off, or no console in the run, neither view exists.
#ifndef B3REPLAY_TUNECONSOLE_H
#define B3REPLAY_TUNECONSOLE_H

#include "b3settings/page.h"
#include "b3tune/browse.h"
#include "b3tune/tune_abi.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "b3gfx/b3gfx.h"
#include "tunelayout.h"

namespace b3replay {

class TuneConsole {
public:
    static TuneConsole& instance();
    // any thread (native_play, before the game starts): the console's functions (null: none in this run)
    void set_hooks(const B3TuneHooks* h);
    bool available() const;
    // tunenav1: the folder of the user's settings: the favourites and recently changed values are kept there
    // (tuning_console.ini; B3TUNE_FAVOURITES: that file instead)
    void set_user_dir(const std::string& dir);

    // PC SETTINGS (render thread): the DEVELOPER tab (none when `developer_mods` is off or there is no console), and an
    // edit of one of its rows (true: it was one, handled)
    std::vector<b3settings::Tab> tabs(bool developer_mods);
    bool edit(const b3settings::Edit& e);

    // The F9 overlay. handle(): one SDL event from the window thread, true when taken (F9 opens / closes it while
    // DEVELOPER MODS is on; while open, its keys). pad(): the window's DS2 report, once per frame.
    void set_enabled(bool developer_mods) { std::lock_guard<std::mutex> l(mu_); enabled_ = developer_mods; if (!enabled_) close(); }
    bool handle(const SDL_Event& e);
    void pad(const std::array<std::uint8_t, 32>& report);
    bool open() const { std::lock_guard<std::mutex> l(mu_); return open_; }
    // render thread: the overlay's draw item in window pixels, appended to `out`
    void draw_list(b3gfx::Device& dev, int w, int h, std::vector<b3gfx::DrawItem>& out);

    // tunenav1, offscreen runs (tools/modding/tuning_check.sh, the docs' shots): B3TUNE_OVERLAY_SCRIPT
    // "<poll>:<action>;..." drives the overlay as keys would (actions: open, close, key:<SDL key name>[+shift|+ctrl],
    // type:<text>), each once the game's newest poll reaches its poll; true when one acted
    bool script_step(std::uint32_t poll);
    bool scripted() const;
    // a view for a shot (B3TUNE_OVERLAY_SHOTS, comma separated): "tab:<n>" (n a tab, from 0), "tab:<n>/<row>[/<row>...]"
    // (that tab's rows opened in turn), "search:<text>"; tunenav2: "+pad" after one: the controller's key lines, "+ask"
    // the RESET ALL question; false: not one
    bool shot_state(const std::string& state);
    // the shot views are drawn without opening the overlay for the game (b3r_settings_active stays as it was); the
    // overlay is left as it was before them
    void end_shots();

    // the overlay's text (tests and the drawing): lines with a label and a value column
    struct Line {
        std::string text, value;
        bool cursor = false, edited = false, heading = false;
        bool prompt = false;   // tunenav2: the RESET question (the accent colour)
        // tunenav3: the gutter marks ("*" changed, "+" a favourite), the default column, the role of a heading line, and (on
        // the tab strip) the list's scroll position: `scroll_total` rows, the window's first and its length
        std::string mark, def;
        enum Role : std::uint8_t { kPlain, kStrip, kCrumbs, kRow, kDetail, kHelp, kKeys } role = kPlain;
        std::size_t scroll_total = 0, scroll_first = 0, scroll_vis = 0;
    };
    std::vector<Line> lines(std::size_t rows);   // the overlay's lines now (mu_ not held)
    // tunenav3: the box as drawn for a window `w` x `h`: the lines with their layout (cut labels, the help over two lines;
    // `tag` maps each laid-out item to its line, the help's second to -(line) - 1); tests/test_tune_layout.cpp
    struct Frame {
        std::vector<Line> lines;
        std::vector<int> tag;                 // each laid-out item's line
        std::vector<Line::Role> role;         // and its role (the focused row's block has detail lines, then help lines)
        tunelayout::Fit fit;
    };
    Frame frame(int w, int h);   // (mu_ not held)
    static std::string element_value(const B3TuneVar& v, std::uint32_t element);

private:
    TuneConsole() = default;
    std::vector<B3TuneVar> live_vars() const;   // the variables registered now, in registration order
    void sync();                                // mu_ held: the trees again when the live set changed; the car's values
    bool var(std::uint32_t index, B3TuneVar& out) const;
    void step(const B3TuneVar& v, std::uint32_t element, int dir, int mult);
    bool reset_var(std::uint32_t index, std::uint32_t element);   // tunenav2: false: not changed
    bool reset_car(int field);
    void reset_changes(const std::vector<b3tune::browse::Change>& cs);
    void car_change(int field, int dir, int mult);
    void read_changes();                        // mu_ held: native_play's changed values into both views
    // the overlay's actions (mu_ held)
    void change(int dir, int mult);   // Left / Right on the focused row
    void confirm();                   // Enter / Cross
    void go_back();                   // Backspace / Circle (at the top: closes)
    void favourite();
    void reset_focused();
    enum class Ask : std::uint8_t { kNone, kSection, kAll };
    void ask(Ask what);                         // RESET SECTION / RESET ALL (asks when more than one)
    void answer(bool yes);
    void close();
    void save_usage(bool now);
    std::vector<Line> lines_locked(std::size_t rows);
    Frame frame_locked(int w, int h);
    std::string row_value(const b3tune::browse::Row& r);
    std::string row_help(const b3tune::browse::Row& r);
    std::string row_label(const b3tune::browse::Browser& b, const b3tune::browse::Row& r);   // the markers and counts
    bool row_changed(const b3tune::browse::Browser& b, const b3tune::browse::Row& r) const;

    mutable std::mutex mu_;
    const B3TuneHooks* hooks_ = nullptr;
    bool enabled_ = false, open_ = false, said_ = false, shot_ = false;
    std::string status_;          // the last SAVE AS PRESET's result
    // tunenav2: what differs from its default (native_play's list, by (variable, element)), the questions
    std::map<std::pair<std::uint32_t, std::uint32_t>, B3TuneChange> change_of_;
    std::vector<B3TuneChange> change_buf_;
    Ask ask_ = Ask::kNone;        // the overlay's question
    std::vector<b3tune::browse::Change> ask_items_;
    std::string note_;            // the overlay's last reset, until the next key
    bool pad_hints_ = false;      // the key lines for a controller (the last input was the pad's)
    Ask pg_ask_ = Ask::kNone;     // the DEVELOPER tab's (armed by the first CONFIRM, for 6 s)
    std::uint64_t pg_ask_ns_ = 0;
    std::string pg_note_;
    b3tune::browse::Browser shot_keep_{nullptr};   // the overlay before the shot views
    Ask shot_ask_keep_ = Ask::kNone;
    bool shot_pad_keep_ = false;
    // tunenav1: the trees (built when the live set changes), the two views' places, the favourites
    b3tune::browse::Usage usage_;
    std::string usage_path_;
    std::uint64_t usage_saved_ns_ = 0;
    b3tune::browse::Browser ov_{&usage_}, pg_{&usage_};
    bool pg_in_ = false;          // the DEVELOPER tab: in a category (false: the category list)
    std::uint32_t gen_seen_ = 0;
    std::uint64_t scan_ns_ = 0;   // (no generation hook: the full scan at most every 0.25 s)
    B3TuneCar car_{};
    std::uint64_t car_ns_ = 0;    // the car's values read at most every 0.1 s (the box is drawn again when its text changes)
    // the overlay's pad
    std::array<std::uint8_t, 32> last_pad_{};
    bool pad_seen_ = false;
    // devnav1: a held D-pad button repeats (as PC SETTINGS: after 0.35 s every 0.07 s; up / down faster after 1.5 s)
    struct Hold {
        bool held = false;
        float time = 0, next = 0;
        bool step(bool now, float dt, bool fast);
    };
    Hold hold_[4];                // up, down, left, right
    std::uint64_t pad_ns_ = 0;
    // the offscreen script
    struct Act { std::uint32_t poll; std::string what; };
    std::vector<Act> script_;
    std::size_t script_i_ = 0;
    bool script_read_ = false;
    // the box
    std::string built_text_;
    b3gfx::TextureId tex_ = b3gfx::kNone;
    b3gfx::BufferId vb_ = b3gfx::kNone, ib_ = b3gfx::kNone;
    b3gfx::ResourceEpoch epoch_;   // a track load clears the ids above (Device::resource_epoch)
    int built_h_ = -1, tex_w_ = 0, tex_h_ = 0;
    // tunenav3: where the box and its list were drawn (window pixels), for the mouse
    struct Geo {
        bool valid = false;
        float x = 0, y = 0, w = 0, h = 0, pitch = 1, list_y = 0, strip_y1 = 0;
        std::size_t list_first = 0, list_n = 0;
    } geo_;
    bool mouse(const SDL_Event& e);   // mu_ held: wheel, click on a row, a tab, right click back
};

}  // namespace b3replay

#endif
