// menu_input.h -- the mouse and typed text in the game's own menus (mac5, 2026-11-09; docs/remaster-features.md §8): what
// crosses between the renderer (b3render: the window's mouse and keyboard, the menus as drawn) and the game thread
// (native_play: the game's menu state, the pad report the game reads). Header-only, no dependencies (unit-tested in
// tests/test_menu_input.cpp).
//
// - The game side publishes the menus on screen as a host record ('HMN1' at kHostMenuBase, MenuRecord): each
//   CB3VSelectComponent [C name] updated in the last VBlanks (its row text objects, the current row) and whether the
//   on-screen keyboard takes typed text. The renderer finds the rows' draws by object address (HudFrame objects) and so
//   knows where each row is on screen, after the wide layout.
// - The renderer sends actions back (MenuAction, b3r_menu_actions): "move the selection to row R of select S" (hover;
//   a click adds a Cross after it), "press button B" (a prompt clicked, right click = Triangle, the wheel = Up / Down).
// - The game side turns them into pad presses (MenuPresser) in the report the game reads, so the game's own menu logic,
//   sounds and animations run: a row is reached by Up / Down presses one at a time, each checked against the game's
//   selection (hover jumps straight to the row where the game's selection data can be set: MenuPresser::Jump; closed loop: a press the game did not take is pressed again; a row it skips is never aimed at past).
// - Typed text: native_play hands each character to the on-screen keyboard's own KeyboardCharacterCallback (the path the
//   PS2's USB keyboard used); text_char_for picks the case the page allows.
#ifndef B3LIVE_MENU_INPUT_H
#define B3LIVE_MENU_INPUT_H

#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace b3live {

// ---- the game's menus on screen (game -> renderer) ----

constexpr std::uint32_t kHostMenuBase = 0x41113000U, kHostMenuMax = 0x400U;   // after the skip hint's record (hud.h)
constexpr std::uint32_t kMenuMagic = 0x314E4D48U;                             // 'HMN1'
constexpr unsigned kMenuRows = 9, kMenuSelects = 4;                           // CB3VSelectComponent +0x14: 9 row objects

struct MenuSelect {
    std::uint32_t addr = 0;              // the CB3VSelectComponent
    std::uint32_t index = 0, count = 0;  // its selection data (+0xC): the current row, the rows
    bool idle = false;                   // the menu idle (no lead-in / lead-out): presses act now
    std::uint32_t rows[kMenuRows] = {};  // the rows' text objects (+0x14), as HudObject::addr
};
struct MenuRecord {
    std::uint32_t serial = 0;
    bool text_entry = false;             // the on-screen keyboard is shown and takes typed characters
    std::vector<MenuSelect> selects;
    bool same_content(const MenuRecord& o) const {
        if (text_entry != o.text_entry || selects.size() != o.selects.size()) return false;
        for (std::size_t i = 0; i < selects.size(); ++i) {
            const MenuSelect &a = selects[i], &b = o.selects[i];
            if (a.addr != b.addr || a.index != b.index || a.count != b.count || a.idle != b.idle ||
                std::memcmp(a.rows, b.rows, sizeof a.rows) != 0)
                return false;
        }
        return true;
    }
};

// {u32 magic, u32 serial, u32 flags (bit 0 text entry), u32 n} then per select {addr, index, count, idle, rows[9]}
inline std::vector<std::uint8_t> menu_record_bytes(const MenuRecord& r) {
    std::vector<std::uint32_t> w = {kMenuMagic, r.serial, r.text_entry ? 1u : 0u, 0u};
    std::uint32_t n = 0;
    for (const MenuSelect& s : r.selects) {
        if (n == kMenuSelects) break;
        w.insert(w.end(), {s.addr, s.index, s.count, s.idle ? 1u : 0u});
        w.insert(w.end(), s.rows, s.rows + kMenuRows);
        ++n;
    }
    w[3] = n;
    std::vector<std::uint8_t> out(w.size() * 4);
    for (std::size_t i = 0; i < w.size(); ++i)
        for (int b = 0; b < 4; ++b) out[i * 4 + std::size_t(b)] = std::uint8_t(w[i] >> (8 * b));
    return out;
}
inline bool menu_record_parse(const std::uint8_t* p, std::size_t n, MenuRecord& r) {
    auto rd = [&](std::size_t i) {
        return std::uint32_t(p[i * 4]) | std::uint32_t(p[i * 4 + 1]) << 8 | std::uint32_t(p[i * 4 + 2]) << 16 | std::uint32_t(p[i * 4 + 3]) << 24;
    };
    if (n < 16 || rd(0) != kMenuMagic) return false;
    const std::uint32_t count = rd(3);
    if (count > kMenuSelects || n < 16 + std::size_t(count) * (4 + kMenuRows) * 4) return false;
    r = MenuRecord{};
    r.serial = rd(1);
    r.text_entry = (rd(2) & 1u) != 0;
    std::size_t at = 4;
    for (std::uint32_t k = 0; k < count; ++k) {
        MenuSelect s;
        s.addr = rd(at); s.index = rd(at + 1); s.count = rd(at + 2); s.idle = rd(at + 3) != 0;
        for (unsigned i = 0; i < kMenuRows; ++i) s.rows[i] = rd(at + 4 + i);
        at += 4 + kMenuRows;
        r.selects.push_back(s);
    }
    return true;
}

// ---- actions (renderer -> game) ----

// DualShock buttons by libpad2 index (src/native/input_keys.h Ds2: the report's bit, active low)
namespace pad {
constexpr std::uint8_t kSelect = 0, kStart = 3, kUp = 4, kRight = 5, kDown = 6, kLeft = 7, kL2 = 8, kR2 = 9, kL1 = 10, kR1 = 11,
                       kTriangle = 12, kCircle = 13, kCross = 14, kSquare = 15;
}

struct MenuAction {
    enum Kind : std::uint8_t { kNone = 0, kTarget = 1, kPress = 2, kCancel = 3 };
    std::uint8_t kind = kNone;
    std::uint8_t button = 0;     // kPress: a pad:: index
    bool after_target = false;   // kPress: only once the target row is reached (dropped with it)
    std::uint32_t select = 0;    // kTarget: the CB3VSelectComponent
    std::uint32_t row = 0;       // kTarget: the row to select
};
// three u32 per action across the C ABI
inline void menu_action_put(const MenuAction& a, std::uint32_t* w) {
    w[0] = std::uint32_t(a.kind) | std::uint32_t(a.button) << 8 | (a.after_target ? 1u << 16 : 0u);
    w[1] = a.select;
    w[2] = a.row;
}
inline MenuAction menu_action_get(const std::uint32_t* w) {
    MenuAction a;
    a.kind = std::uint8_t(w[0] & 0xFFu);
    a.button = std::uint8_t((w[0] >> 8) & 0xFFu);
    a.after_target = (w[0] >> 16 & 1u) != 0;
    a.select = w[1];
    a.row = w[2];
    return a;
}

// A button held in a pad report (bytes 0-1 active low by libpad2 index; the pressure bytes 6-17 full; input_keys.h)
inline void pad_press(std::uint8_t* report, std::uint8_t button) {
    if (button > 15) return;
    report[button / 8] = std::uint8_t(report[button / 8] & ~(1u << (button % 8)));
    int p = -1;
    switch (button) {
    case pad::kRight: p = 6; break;
    case pad::kLeft: p = 7; break;
    case pad::kUp: p = 8; break;
    case pad::kDown: p = 9; break;
    case pad::kTriangle: p = 10; break;
    case pad::kCircle: p = 11; break;
    case pad::kCross: p = 12; break;
    case pad::kSquare: p = 13; break;
    case pad::kL1: p = 14; break;
    case pad::kR1: p = 15; break;
    case pad::kL2: p = 16; break;
    case pad::kR2: p = 17; break;
    default: break;
    }
    if (p >= 0) report[p] = 0xFF;
}

// ---- the presses (game thread, once per VBlank) ----

// What the game holds for a select now (MenuPresser::step asks for the target's)
struct MenuSelectState {
    std::uint32_t index = 0, count = 0;
    bool idle = false;
};

class MenuPresser {
public:
    // The actions run in the order they came (a wheel turn before a hover steps first, then the hover's row). A press is
    // held kHold VBlanks and followed by kGap released ones (the game's menus take a new press on its edge and repeat
    // only after a longer hold: race_check's 10-VBlank key presses move one row). A target row is reached by steps: each
    // step's press waits after its release until the game's selection moved, up to kStepWait VBlanks; a target not
    // reached within kTargetTimeout VBlanks (a page that went away, a select that stays busy) is dropped with the presses
    // that waited for it. A new target replaces the queued one (the hover moved on) and the presses that waited for it.
    static constexpr int kHold = 3, kGap = 4, kStepWait = 12, kTargetTimeout = 120;
    using Live = std::function<bool(std::uint32_t select, MenuSelectState& s)>;

    void push(const MenuAction& a) {
        switch (a.kind) {
        case MenuAction::kTarget:
            drop_targets();
            if (queue_.size() < 16) queue_.push_back({true, 0, false, a.select, a.row, 0, 0});
            break;
        case MenuAction::kPress: {
            bool target = false;
            for (const Item& q : queue_) target = target || q.target;
            if (a.after_target && !target) break;   // its target is gone
            if (queue_.size() < 16) queue_.push_back({false, a.button, a.after_target, 0, 0, 0, 0});
            break;
        }
        case MenuAction::kCancel:
            drop_targets();
            break;
        default: break;
        }
    }
    void reset() { queue_.clear(); hold_ = gap_ = 0; stepping_ = false; }
    bool busy() const { return !queue_.empty() || hold_ > 0 || gap_ > 0; }

    // The buttons to hold this VBlank: bit i = pad index i.
    // `jump` (optional): sets a select's current row directly (the game's own Update then moves the highlight and plays the
    // move sound once, as for a press: a selection is only its data's current row); true when it did. Without it, or when it
    // returns false, the row is reached by presses.
    using Jump = std::function<bool(std::uint32_t select, std::uint32_t row)>;
    std::uint32_t step(const Live& live, const Jump& jump = nullptr) {
        if (hold_ > 0) {
            if (--hold_ == 0) gap_ = stepping_ ? kStepWait : kGap;
            return 1u << button_;
        }
        if (gap_ > 0) {
            --gap_;
            if (stepping_ && !queue_.empty() && queue_.front().target) {   // a step: done once the selection moved
                MenuSelectState s;
                const bool moved = live(queue_.front().select, s) && s.index != step_from_;
                if (moved && kStepWait - gap_ >= 2) gap_ = 0;   // (released 2 VBlanks)
            }
            if (gap_ > 0) return 0;
            stepping_ = false;
        }
        while (!queue_.empty()) {
            Item& q = queue_.front();
            if (!q.target) {
                const std::uint8_t b = q.button;
                queue_.pop_front();
                return start(b);
            }
            MenuSelectState s;
            const bool ok = live(q.select, s);
            if (++q.age > kTargetTimeout || (ok && q.row >= s.count) || q.steps > 2 * int(kMenuRows) + 4) {
                drop_targets();   // dropped, with what waited for it
                continue;
            }
            if (!ok || !s.idle) return 0;   // the select not on screen (yet) or busy: wait
            if (s.index == q.row) { queue_.pop_front(); continue; }   // reached: what waited for it follows
            if (jump && jump(q.select, q.row)) {   // straight to the row: what waited for it follows
                ++q.steps;
                if (q.steps <= 2 * int(kMenuRows) + 4) { queue_.pop_front(); continue; }
            }
            ++q.steps;
            stepping_ = true;
            step_from_ = s.index;
            return start(s.index < q.row ? pad::kDown : pad::kUp);
        }
        return 0;
    }

private:
    struct Item {
        bool target;
        std::uint8_t button;
        bool after_target;
        std::uint32_t select, row;
        int age, steps;
    };
    std::uint32_t start(std::uint8_t b) {
        button_ = b;
        hold_ = kHold - 1;
        if (hold_ == 0) gap_ = stepping_ ? kStepWait : kGap;
        return 1u << b;
    }
    void drop_targets() {   // the targets and the presses that wait for them
        std::deque<Item> keep;
        for (const Item& q : queue_) if (!q.target && !q.after_target) keep.push_back(q);
        queue_.swap(keep);
    }
    std::deque<Item> queue_;
    int hold_ = 0, gap_ = 0;
    std::uint8_t button_ = 0;
    bool stepping_ = false;
    std::uint32_t step_from_ = 0;
};

// ---- typed text ----

// The character to hand the on-screen keyboard for a typed one: Enter (10: its Done key), Backspace (8) and space as
// they are; another printable ASCII character as typed when the page allows it (`allowed`: the page's allowable
// characters, CB3OnScreenKeyboardComponent +0x3208's global string), else in the other case when that is allowed
// (a page of capitals takes "a" as "A"); 0 when neither (dropped). An empty `allowed` (not known) passes it as typed:
// the game's callback checks the set again.
inline char text_char_for(char c, const std::u16string& allowed) {
    if (c == 8 || c == 10 || c == ' ') return c;
    if (c < 0x21 || c > 0x7E) return 0;
    if (allowed.empty()) return c;
    auto ok = [&](char x) { return allowed.find(char16_t(static_cast<unsigned char>(x))) != std::u16string::npos; };
    if (ok(c)) return c;
    const char other = c >= 'a' && c <= 'z' ? char(c - 32) : c >= 'A' && c <= 'Z' ? char(c + 32) : 0;
    return other && ok(other) ? other : 0;
}

}  // namespace b3live

#endif
