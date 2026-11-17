// menu_mouse.h -- native_play's side of the mouse and typed text in the game's own menus (mac5, 2026-11-09;
// docs/remaster-features.md §8; the shared parts: include/b3live/menu_input.h).
//
// Two wrappers on the hook table (hook_table.h, port layer, as tools/native/pc_menu.h's; install after it):
//   - Update__19CB3VSelectComponentFf (0x31FF80 [C name]): the vertical selection every front-end list and the pause
//     menu use. Each call records `this`, so the menus on screen are the ones updated in the last few VBlanks. Their
//     state is read from the component [C B3VSelectComponent.h layout, Prepare 0x3210D0 / Update]: +0xC the selection
//     data (u16 current row, u16 rows: CB3MenuComponentSelectionData, which the page's Update steps with
//     GetMenuUp / GetMenuDown), +0x14 the 9 row text objects, +0xC4 the lead-in / lead-out timer (< 0: none); the menu
//     manager's state D_01E90424 (5 = idle [C Update's switch]).
//   - Update__28CB3OnScreenKeyboardComponentFf (0x351410): the on-screen keyboard (the profile name, D_01E899C0). Its
//     page registers KeyboardCharacterCallback (0x34E2A0) at the PS2 keyboard device D_004F5518 (+0x8 the character
//     callback, +0xC its context: RegisterKeyboardCallbacks 0x34D8D0; the CB3MyBurnoutProfileCreateNewPage's Prepare
//     0x3F55E0 calls it); Show (0x350F80) clears D_01E7AA38 while the keyboard is up. While both hold, the typed
//     characters go to that callback, one per keyboard update, before the update -- the game's own path for a USB
//     keyboard: Enter presses its Done key, Backspace its Backspace key, space its Space key, any other character the
//     key that shows it (PressKey) or, without one, AddCharToString, both after its check against the page's allowed
//     characters. So the name's text, length, cursor and sounds stay the game's. PressKey lights the key it pressed
//     (HighlightKey(key, true)); on the PS2 the key's release (KeyboardEventCallback 0x34E3D0, type 1) puts them out
//     again with UnHighlightKeys (0x34D3A0: every key but the cursor's). The window gives characters, not releases, so
//     the release is ours: UnHighlightKeys kKeyFlash VBlanks after a typed character, or before the next one.
// The front end's dialog box D_01E8F360 (the profile pages' "LOAD PROFILE / NEW PROFILE", "Would you like to save your
// profile? YES / NO"; the pages hide it through its vfunc +0xC when +0xA is set [C func_003F55E0]) is read each VBlank, no
// wrapper: shown while +0xA is set, +0xC u16 the current choice, +0xE u16 the choices (1..2), +0x38 / +0x3C their text
// objects [C a memory scan and guest reads on the fresh-card profile flow, mac5: items 01E923A0 / 01E924E0 drawn as the
// choices, +0x4C the selection bar, +0x50 the prepared choice].
// Per VBlank (vblank()): the record of the menus for the renderer (gameview_native set_menu_record), the text mode for
// the window (b3r_text_mode: printable keys then type instead of pressing pad buttons), and the renderer's actions turned
// into presses on the pad report (MenuPresser), never while driving (the caller passes its driving context: the race's
// GetAccelerator reads), so the mouse reaches the game only in menus and the pause menu.
// B3NATIVE_MENU_MOUSE=0 leaves both off.
#ifndef B3NATIVE_MENU_MOUSE_H
#define B3NATIVE_MENU_MOUSE_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "b3live/menu_input.h"
#include "hook_table.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace b3native {

class MenuMouse {
public:
    struct Renderer {   // b3render's C ABI (any may be null: an older renderer)
        int (*actions)(std::uint32_t* out, int max) = nullptr;   // b3r_menu_actions
        void (*text_mode)(int on) = nullptr;                     // b3r_text_mode
        int (*text_take)(char* out, int max) = nullptr;          // b3r_text_take
    };
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_MENU_MOUSE");
        return !(e && e[0] == '0' && e[1] == 0);
    }
    bool install(HookTable& hooks, const Renderer& r);
    // Once per VBlank, game thread: `driving` as the key map's context; `report` the pad report about to be set
    // (presses are added to it; null: none, e.g. a pad replay). Returns the record to publish when it changed (else null).
    const std::vector<std::uint8_t>* vblank(std::uint64_t v, bool driving, bool page_open, std::uint8_t* report);
    std::uint64_t presses() const { return presses_; }
    std::uint64_t typed() const { return typed_; }
    // replay1 (src/native/replay_file.h): the characters the window gave on the last vblank() (a replay records them),
    // and a replay's own instead of the window's (replay_text(true): the window's typing is then dropped; feed_text
    // before each vblank() with that VBlank's characters)
    const std::string& taken() const { return taken_; }
    void replay_text(bool on) { text_replay_ = on; }
    void feed_text(const std::string& t) { text_feed_ = t; }

private:
    static MenuMouse*& instance() { static MenuMouse* p = nullptr; return p; }
    static void select_update(const b3host::CallArgs& a, b3host::CallResult& r);
    static void seen(std::uint32_t self, int kind);
    std::uint32_t row_object(std::uint32_t addr, int kind, unsigned i) const;
    static void keyboard_update(const b3host::CallArgs& a, b3host::CallResult& r);
    bool live(std::uint32_t select, b3live::MenuSelectState& s) const;
    bool jump(std::uint32_t select, std::uint32_t row);
    bool text_entry_now() const;

    void (*select_update_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    void (*keyboard_update_)(const b3host::CallArgs&, b3host::CallResult&) = nullptr;
    Renderer r_;
    enum Kind { kVSelect = 0, kDialog = 1, kMessage = 2 };   // kMessage: the message box (choices at +0x20 / +0x24)
    struct Seen { std::uint32_t addr; std::uint64_t vblank; int kind; };
    std::vector<Seen> seen_;           // lists updated, with the VBlank of their last update
    std::uint64_t vblank_ = 0, keyboard_seen_ = 0;
    std::uint64_t release_at_ = 0;     // the typed key's release (UnHighlightKeys) due at this VBlank (0: none)
    b3live::MenuRecord record_, published_;
    std::vector<std::uint8_t> record_bytes_;
    b3live::MenuPresser presser_;
    bool text_on_ = false, allowed_known_ = false;
    std::u16string allowed_;
    std::deque<char> text_;
    std::uint64_t presses_ = 0, typed_ = 0;
    std::uint32_t held_ = 0;
    std::string taken_, text_feed_;   // replay1
    bool text_replay_ = false;
};

// ---- implementation (header-only: native_play.cpp is its one includer) ----
namespace menu_mouse_detail {
constexpr b3host::gaddr kSelectUpdate = 0x0031FF80u, kKeyboardUpdate = 0x00351410u;
constexpr b3host::gaddr kDialogBox = 0x01E8F360u;   // the front end's dialog box (see above)
// The message box every other page uses (the pause menu's QUIT RACE / END SESSION / RESTART confirmations, exit game, delete
// save, ... : func_0032D080 shows it, func_0032B2C0 is its Update, the same class: +0xA shown, +0xC the choice (u16), +0xE
// the choices, +0x20 / +0x24 their text objects [read from the pause menu's QUIT confirmation, mac5])
constexpr b3host::gaddr kMessageBox = 0x01E898E8u;
constexpr b3host::gaddr kMenuState = 0x01E90424u;   // CB3MenuPageManager D_01E90240 + 0x1E4: 5 = idle [C VSelect Update]
constexpr b3host::gaddr kKeyboard = 0x01E899C0u, kKeyboardDevice = 0x004F5518u, kKeyboardHidden = 0x01E7AA38u;
constexpr b3host::gaddr kCharCallback = 0x0034E2A0u;   // CB3OnScreenKeyboardComponent::KeyboardCharacterCallback(char, void*)
constexpr b3host::gaddr kUnHighlightKeys = 0x0034D3A0u;   // CB3OnScreenKeyboardComponent::UnHighlightKeys() [C 0x34E3D0, key up]
constexpr b3host::gaddr kKeysBlocked = 0x01E8E8AAu;       // u8: both keyboard callbacks return at once while set [C 0x34E2A0 / 0x34E3D0]
constexpr unsigned kKeyFlash = 6;   // VBlanks a typed key stays lit (~0.1 s: a key press's length)
constexpr b3host::gaddr kGlobalString = 0x00271A00u, kStrings = 0x0051BA44u;   // func_00271A00(&D_0051BA44, id) [C 0x34E2A0]
constexpr unsigned kSeenVblanks = 6;   // a select not updated for this long is off screen
inline u_long128 reg(std::uint32_t v) { u_long128 q{}; q.w[0] = v; return q; }
inline std::uint32_t ptr(b3host::gaddr a) { return b3host::load<std::uint32_t>(a) & 0x01FFFFFFu; }
}  // namespace menu_mouse_detail

inline bool MenuMouse::install(HookTable& hooks, const Renderer& r) {
    using namespace menu_mouse_detail;
    const bool su = hooks.has_row(kSelectUpdate), ku = hooks.has_row(kKeyboardUpdate);
    if (!su && !ku) {
        std::fprintf(stderr, "menu_mouse: neither the selection's nor the keyboard's Update is native: off\n");
        return false;
    }
    std::vector<Hook> set;
    if (su) set.push_back({kSelectUpdate, HookLayer::kPort, &select_update, &select_update_});
    if (ku) set.push_back({kKeyboardUpdate, HookLayer::kPort, &keyboard_update, &keyboard_update_});
    std::string error;
    if (!hooks.install("port:menu_mouse", set, &error)) {
        std::fprintf(stderr, "menu_mouse: %s: off\n", error.c_str());
        return false;
    }
    r_ = r;
    instance() = this;
    std::printf("menu_mouse: the mouse in the game's menus%s, typed text%s (B3NATIVE_MENU_MOUSE=0: off)\n", su ? "" : " (lists off: no native VSelect Update)",
                ku && r.text_take ? "" : " (off)");
    return true;
}

inline void MenuMouse::seen(std::uint32_t self, int kind) {
    MenuMouse* p = instance();
    bool found = false;
    for (Seen& s : p->seen_) if (s.addr == self) { s.vblank = p->vblank_; s.kind = kind; found = true; }
    if (!found && p->seen_.size() < 16) {
        p->seen_.push_back({self, p->vblank_, kind});
        std::printf("menu_mouse: VBlank %llu: a %s at %08X\n", static_cast<unsigned long long>(p->vblank_),
                    kind == kVSelect ? "selection list" : kind == kMessage ? "message box" : "dialog box", self);
        std::fflush(stdout);
    }
}

inline void MenuMouse::select_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    seen(a.gpr[0].w[0] & 0x01FFFFFFu, kVSelect);
    instance()->select_update_(a, r);
}

inline std::uint32_t MenuMouse::row_object(std::uint32_t addr, int kind, unsigned i) const {
    return menu_mouse_detail::ptr(addr + (kind == kVSelect ? 0x14u : kind == kMessage ? 0x20u : 0x38u) + 4u * i);
}

inline bool MenuMouse::text_entry_now() const {
    using namespace menu_mouse_detail;
    return keyboard_update_ && keyboard_seen_ + kSeenVblanks >= vblank_ && keyboard_seen_ != 0 &&
           b3host::load<std::uint32_t>(kKeyboardDevice + 8u) == kCharCallback && ptr(kKeyboardDevice + 0xCu) == kKeyboard &&
           b3host::load<std::uint8_t>(kKeyboardHidden) == 0;
}

inline void MenuMouse::keyboard_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace menu_mouse_detail;
    MenuMouse* p = instance();
    const std::uint32_t self = a.gpr[0].w[0] & 0x01FFFFFFu;
    if (self == kKeyboard) p->keyboard_seen_ = p->vblank_;
    auto call = [&](b3host::gaddr fn, std::uint32_t a0, std::uint32_t a1) {
        b3host::CallArgs c{};
        c.gpr[0] = reg(a0);
        c.gpr[1] = reg(a1);
        c.n_gpr = 2;
        c.sp = a.sp;
        b3host::CallResult cr{};
        b3host::detail::dispatch(fn, c, cr);
        return cr.v0.w[0];
    };
    const bool typing = self == kKeyboard && p->text_on_ && b3host::load<std::uint32_t>(kMenuState) == 5u && !p->text_.empty();
    // the last typed key released (the PS2's key-up: KeyboardEventCallback type 1), when due or before the next one
    if (self == kKeyboard && p->release_at_ && (typing || p->vblank_ >= p->release_at_) && b3host::load<std::uint8_t>(kKeysBlocked) == 0) {
        p->release_at_ = 0;
        call(kUnHighlightKeys, kKeyboard, 0);
    }
    if (typing) {
        if (!p->allowed_known_) {   // the page's allowed characters (once per keyboard shown)
            p->allowed_known_ = true;
            p->allowed_.clear();
            const std::uint32_t s = call(kGlobalString, kStrings, b3host::load<std::uint32_t>(kKeyboard + 0x3208u)) & 0x01FFFFFFu;
            for (std::uint32_t i = 0; s && i < 512; ++i) {
                const std::uint16_t c = b3host::load<std::uint16_t>(s + 2 * i);
                if (!c) break;
                p->allowed_.push_back(char16_t(c));
            }
            std::printf("menu_mouse: the keyboard takes %zu characters\n", p->allowed_.size());
            std::fflush(stdout);
        }
        const char c = b3live::text_char_for(p->text_.front(), p->allowed_);
        p->text_.pop_front();
        if (c) {
            call(kCharCallback, std::uint32_t(std::int32_t(static_cast<signed char>(c))), kKeyboard);
            ++p->typed_;
            p->release_at_ = p->vblank_ + kKeyFlash;
        }
    }
    p->keyboard_update_(a, r);
}

inline bool MenuMouse::live(std::uint32_t select, b3live::MenuSelectState& s) const {
    using namespace menu_mouse_detail;
    int kind = -1;
    for (const Seen& x : seen_) if (x.addr == select && x.vblank + kSeenVblanks >= vblank_) kind = x.kind;
    if (kind < 0) return false;
    if (kind == kDialog || kind == kMessage) {
        if (b3host::load<std::uint8_t>(select + 0xAu) == 0) return false;
        s.index = b3host::load<std::uint16_t>(select + 0xCu);
        s.count = b3host::load<std::uint16_t>(select + 0xEu);
        s.idle = b3host::load<std::uint32_t>(kMenuState) == 5u;
        return s.count > 0 && s.count <= 2;
    }
    const std::uint32_t sel = ptr(select + 0xCu);
    if (!sel) return false;
    s.index = b3host::load<std::uint16_t>(sel);
    s.count = b3host::load<std::uint16_t>(sel + 2u);
    float lead;
    const std::uint32_t lw = b3host::load<std::uint32_t>(select + 0xC4u);
    std::memcpy(&lead, &lw, 4);
    s.idle = b3host::load<std::uint32_t>(kMenuState) == 5u && lead < 0.0f;
    return s.count > 0 && s.count <= b3live::kMenuRows;
}

// Hover: the current row set in the selection data (the game's Update sees it change: the highlight moves and the move
// sound plays once, the same as for an Up / Down press, which only steps this value by one without wrap)
inline bool MenuMouse::jump(std::uint32_t select, std::uint32_t row) {
    using namespace menu_mouse_detail;
    b3live::MenuSelectState s;
    if (!live(select, s) || row >= s.count) return false;
    int kind = -1;
    for (const Seen& x : seen_) if (x.addr == select) kind = x.kind;
    const std::uint32_t at = kind == kDialog || kind == kMessage ? select + 0xCu : ptr(select + 0xCu);
    if (!at) return false;
    b3host::store<std::uint16_t>(at, std::uint16_t(row));
    if (std::getenv("B3NATIVE_MENU_TRACE"))
        std::printf("menu_mouse: VBlank %llu: hover jump %08X row %u -> %u\n", static_cast<unsigned long long>(vblank_), select, s.index, row);
    return true;
}

inline const std::vector<std::uint8_t>* MenuMouse::vblank(std::uint64_t v, bool driving, bool page_open, std::uint8_t* report) {
    vblank_ = v;
    {   // the dialog box: on screen while shown with one or two choices
        using namespace menu_mouse_detail;
        const unsigned n = b3host::load<std::uint16_t>(kDialogBox + 0xEu);
        if (b3host::load<std::uint8_t>(kDialogBox + 0xAu) != 0 && n >= 1 && n <= 2 && ptr(kDialogBox + 0x38u) != 0) seen(kDialogBox, kDialog);
        const unsigned m = b3host::load<std::uint16_t>(kMessageBox + 0xEu);
        if (b3host::load<std::uint8_t>(kMessageBox + 0xAu) != 0 && m >= 1 && m <= 2 && ptr(kMessageBox + 0x20u) != 0) seen(kMessageBox, kMessage);
    }
    // the selects on screen
    record_.selects.clear();
    bool modal = false;   // a dialog box up: the lists beneath it are not clickable
    for (const Seen& x : seen_) {
        b3live::MenuSelectState s;
        if (x.kind != kVSelect && live(x.addr, s)) modal = true;
    }
    for (const Seen& x : seen_) {
        b3live::MenuSelectState s;
        if (record_.selects.size() >= b3live::kMenuSelects || (modal && x.kind == kVSelect) || !live(x.addr, s)) continue;
        b3live::MenuSelect m;
        m.addr = x.addr;
        m.index = s.index;
        m.count = s.count;
        m.idle = s.idle;
        for (unsigned i = 0; i < b3live::kMenuRows; ++i) m.rows[i] = i < s.count ? row_object(x.addr, x.kind, i) : 0u;
        record_.selects.push_back(m);
    }
    // typed text: the window types while the keyboard is up
    const bool text = text_entry_now();
    if (text != text_on_) {
        text_on_ = text;
        allowed_known_ = false;
        text_.clear();
        if (r_.text_mode) r_.text_mode(text ? 1 : 0);
        std::printf("native_play: VBlank %llu: the on-screen keyboard %s typed text\n", static_cast<unsigned long long>(v), text ? "takes" : "no longer takes");
        std::fflush(stdout);
    }
    if (r_.text_take || text_replay_) {
        char buf[64];
        int n = r_.text_take ? r_.text_take(buf, int(sizeof buf)) : 0;
        if (text_replay_) {   // replay1: the recorded characters of this VBlank, the window's dropped
            n = int(std::min(text_feed_.size(), sizeof buf));
            std::memcpy(buf, text_feed_.data(), std::size_t(n));
            text_feed_.clear();
        }
        taken_.assign(buf, std::size_t(std::max(n, 0)));
        for (int i = 0; i < n; ++i) if (text_on_ && text_.size() < 256) text_.push_back(buf[i]);
    }
    record_.text_entry = text_on_;
    // the renderer's actions -> presses (menus only: never while the race is driven, nor under the PC SETTINGS page)
    if (r_.actions) {
        std::uint32_t w[3 * 16];
        const int n = r_.actions(w, 16);
        for (int i = 0; i < n; ++i) if (!driving && !page_open) presser_.push(b3live::menu_action_get(w + 3 * i));
    }
    if (driving || page_open) presser_.reset();
    const std::uint32_t mask = presser_.step([this](std::uint32_t s, b3live::MenuSelectState& st) { return live(s, st); },
                                          [this](std::uint32_t s, std::uint32_t row) { return jump(s, row); });
    if (mask && report) {
        for (std::uint8_t b = 0; b < 16; ++b) if (mask & (1u << b)) b3live::pad_press(report, b);
        if (mask != held_) {
            ++presses_;
            std::printf("native_play: VBlank %llu: menu mouse press %08X\n", static_cast<unsigned long long>(v), mask);
            std::fflush(stdout);
        }
    }
    held_ = mask;
    if (record_.same_content(published_) && published_.serial != 0) return nullptr;
    record_.serial = published_.serial + 1;
    published_ = record_;
    record_bytes_ = b3live::menu_record_bytes(record_);
    static const bool trace = std::getenv("B3NATIVE_MENU_TRACE") != nullptr;
    if (trace) {
        std::printf("menu_mouse: VBlank %llu: record %u: %zu lists%s", static_cast<unsigned long long>(v), record_.serial, record_.selects.size(),
                    record_.text_entry ? ", text entry" : "");
        for (const b3live::MenuSelect& s : record_.selects) std::printf(" [%08X row %u of %u%s]", s.addr, s.index, s.count, s.idle ? " idle" : "");
        std::printf("\n");
    }
    return &record_bytes_;
}

}  // namespace b3native

#endif
