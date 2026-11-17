// src/b3settings/menu.cpp -- the PC SETTINGS page's menu model (include/b3settings/page.h class Menu), written from
// docs/specs/settings-page.md: one poll of input -> edits (§2), the page as UI items in the game's
// 640 x 448 menu picture (§3). Time comes only from Input::seconds, so a replay draws the same page.
#include "b3settings/page.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace b3settings {

namespace {

// ---- the hold repeat (§2.3)
constexpr float kRepeatDelay = 0.35f, kRepeatInterval = 0.07f;
constexpr float kFastAfter = 1.5f, kFasterAfter = 3.0f;   // devnav1: up / down held: x2 from 1.5 s, ~60 rows a second from 3 s
constexpr float kFastestInterval = 0.016f;                // (every poll at 60 Hz, every other at 120 Hz)

// ---- the page's geometry, picture units (shared by the pointer rules and the items)
constexpr float kPictureW = 640.0f, kPictureH = 448.0f;
constexpr float kFrameX0 = 32.0f, kFrameX1 = 608.0f;      // the page's panels
constexpr float kTitleY = 14.0f, kTitleH = 30.0f;
constexpr float kTabY = 56.0f, kTabH = 24.0f;
constexpr float kPromptW = 38.0f;                         // the L1 / R1 prompts left and right of the tab slots
constexpr float kTabX0 = kFrameX0 + kPromptW + 8.0f, kTabX1 = kFrameX1 - kPromptW - 8.0f;
constexpr float kListY = 92.0f, kRowH = 24.0f;
constexpr std::size_t kVisible = 10;                      // rows in the window
constexpr float kListX0 = 40.0f, kListX1 = 584.0f;        // a row's box (the scroll bar right of it)
constexpr float kLabelX = 58.0f;
constexpr float kCtrlX0 = 312.0f, kCtrlX1 = 572.0f;       // a row's control
constexpr float kArrowW = 20.0f;                          // an arrow's hit box (the arrow drawn inside it)
constexpr float kBarX0 = kCtrlX0 + kArrowW + 4.0f, kBarX1 = kCtrlX1 - kArrowW - 4.0f;
constexpr float kScrollX = 590.0f, kScrollW = 14.0f;      // the scroll bar: arrows at both ends
constexpr float kDescY = kListY + kRowH * float(kVisible) + 10.0f, kDescH = 52.0f;
constexpr float kFooterY = kDescY + kDescH + 10.0f, kFooterH = 22.0f;
constexpr float kToggleW = 40.0f, kButtonW = 128.0f;

struct Box {
    float x = 0, y = 0, w = 0, h = 0;
    bool in(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
Box tab_box(std::size_t k, std::size_t n) {
    const float w = (kTabX1 - kTabX0) / float(std::max<std::size_t>(n, 1));
    return {kTabX0 + w * float(k), kTabY, w, kTabH};
}
Box prompt_box(bool right) { return {right ? kTabX1 + 8.0f : kFrameX0, kTabY, kPromptW, kTabH}; }
Box row_box(std::size_t slot) { return {kListX0, kListY + kRowH * float(slot), kListX1 - kListX0, kRowH}; }
Box scroll_up_box() { return {kScrollX, kListY, kScrollW, 18.0f}; }
Box scroll_down_box() { return {kScrollX, kListY + kRowH * float(kVisible) - 18.0f, kScrollW, 18.0f}; }
Box arrow_box(float y, bool right) { return {right ? kCtrlX1 - kArrowW : kCtrlX0, y, kArrowW, kRowH}; }
Box toggle_box(float y) { return {kCtrlX1 - kToggleW, y + 5.0f, kToggleW, kRowH - 10.0f}; }
Box button_box(float y) { return {kCtrlX1 - kButtonW, y + 3.0f, kButtonW, kRowH - 6.0f}; }

std::size_t value_count(const Row& r) {
    switch (r.kind) {
    case RowKind::kToggle: return 2;
    case RowKind::kChoice: return r.values.size();
    case RowKind::kSlider: return r.levels;
    default: return 0;
    }
}

}  // namespace

// ---- input (§2)

bool Menu::Repeat::step(bool now, float seconds, bool block, bool fast) {
    if (!now) {
        held = blocked = false;
        return false;
    }
    if (block) blocked = true;   // a popup: this press never acts, not even when the popup has gone
    if (blocked) {
        held = true;
        return false;
    }
    if (!held) {   // the press acts at once
        held = true;
        time = 0;
        next = kRepeatDelay;
        return true;
    }
    time += seconds;
    if (time < next) return false;
    // devnav1: a long list (DEVELOPER: ~1,100 rows in a race) goes faster the longer the key is held
    const float interval = !fast || time < kFastAfter ? kRepeatInterval : time < kFasterAfter ? 0.5f * kRepeatInterval : kFastestInterval;
    next = std::max(next + interval, time);   // at most one step per poll, no backlog after a long poll
    return true;
}

float fallback_measure(std::string_view text, float height, std::uint8_t flags) {
    float em = 0;
    for (unsigned char c : text) {
        if (c == ' ') em += 0.34f;
        else if (c == 'I' || c == 'i' || c == 'l' || c == 'j' || c == '1' || c == '!' || c == '|' || c == '.' || c == ',' ||
                 c == ':' || c == ';' || c == '\'' || c == '(' || c == ')')
            em += 0.32f;
        else if (c == 'M' || c == 'W' || c == 'm' || c == 'w' || c == '%') em += 0.86f;
        else if (c >= 'a' && c <= 'z') em += 0.54f;
        else em += 0.64f;
    }
    const float w = em * height;
    return (flags & kTextSizeMask) == kItalic ? w * kItalicWidth : w;
}

void Menu::set_clock(float seconds) { clock_ = std::fmod(std::max(seconds, 0.0f), 3600.0f); }

void Menu::open(std::size_t tab) {
    tab_ = tab;
    focus_ = scroll_ = 0;
    focus_id_ = 0;
    focus_known_ = false;
    memory_.clear();
    remembered_.clear();
    // a button held as the page opens (the press that opened it) acts only after it is released
    was_confirm_ = was_back_ = was_previous_ = was_next_ = was_reset_ = true;
    up_ = down_ = left_ = right_ = section_up_ = section_down_ = Repeat{};
    dragging_ = false;
    drag_id_ = 0;
    open_time_ = tab_time_ = clock_ = 0;
    tab_from_ = 0;
}

const std::vector<Row>* Menu::tab_rows() const {
    return tab_ < content_.tabs.size() ? &content_.tabs[tab_].rows : nullptr;
}

std::size_t Menu::first_selectable() const {
    const std::vector<Row>* rows = tab_rows();
    if (!rows) return 0;
    for (std::size_t i = 0; i < rows->size(); ++i)
        if ((*rows)[i].kind != RowKind::kSection) return i;
    return rows->size();
}

void Menu::focus_row(std::size_t index, bool header) {
    const std::vector<Row>* rows = tab_rows();
    if (!rows || index >= rows->size()) {
        focus_ = 0;
        focus_known_ = false;
        return;
    }
    if (dragging_ && (*rows)[index].id != drag_id_) dragging_ = false;
    focus_ = index;
    focus_id_ = (*rows)[index].id;
    focus_known_ = true;
    show_focus(header);
}

// The window of rows keeps the focused row inside; with `header`, its section header just above it is shown too.
void Menu::show_focus(bool header) {
    const std::vector<Row>* rows = tab_rows();
    const std::size_t n = rows ? rows->size() : 0;
    if (n <= kVisible) {
        scroll_ = 0;
        return;
    }
    if (focus_ < scroll_) scroll_ = focus_;
    if (focus_ >= scroll_ + kVisible) scroll_ = focus_ + 1 - kVisible;
    if (header && focus_ > 0 && focus_ - 1 < scroll_ && (*rows)[focus_ - 1].kind == RowKind::kSection) scroll_ = focus_ - 1;
    scroll_ = std::min(scroll_, n - kVisible);
}

void Menu::switch_tab(std::size_t to) {
    if (to >= content_.tabs.size() || to == tab_) return;
    if (memory_.size() < content_.tabs.size()) {
        memory_.resize(content_.tabs.size(), 0);
        remembered_.resize(content_.tabs.size(), false);
    }
    if (tab_ < memory_.size() && focus_known_) {
        memory_[tab_] = focus_id_;
        remembered_[tab_] = true;
    }
    tab_from_ = to > tab_ ? 1 : -1;
    tab_ = to;
    dragging_ = false;
    tab_time_ = 0;
    scroll_ = 0;
    const std::vector<Row>& rows = content_.tabs[tab_].rows;
    std::size_t index = first_selectable();
    if (remembered_[tab_])
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].id == memory_[tab_] && rows[i].kind != RowKind::kSection) index = i;
    focus_row(index, true);
}

// devnav1: PageDown: the first selectable row after the next section heading (none after: the last row); PageUp: the
// first row of the focused row's section, or of the section before when already there (none before: the first row).
// A tab without sections moves by a window of rows.
void Menu::section_step(int direction) {
    const std::vector<Row>* rows = tab_rows();
    if (!rows || rows->empty() || !focus_known_) return;
    const std::vector<Row>& r = *rows;
    const std::size_t n = r.size();
    const auto selectable_from = [&](std::size_t i, int dir) {   // the first selectable row from i going dir; n: none
        for (long k = long(i); k >= 0 && k < long(n); k += dir)
            if (r[std::size_t(k)].kind != RowKind::kSection) return std::size_t(k);
        return n;
    };
    bool sections = false;
    for (const Row& x : r) sections = sections || x.kind == RowKind::kSection;
    std::size_t to = n;
    if (!sections) {
        const long t = std::clamp(long(focus_) + direction * long(kVisible), 0L, long(n) - 1);
        to = selectable_from(std::size_t(t), direction > 0 ? -1 : 1);
    } else if (direction > 0) {
        for (std::size_t i = focus_ + 1; i < n && to == n; ++i)
            if (r[i].kind == RowKind::kSection) to = selectable_from(i, 1);
        if (to == n) to = selectable_from(n - 1, -1);
    } else {
        std::size_t start = focus_;   // the first selectable row of the focused row's section
        while (start > 0 && r[start - 1].kind != RowKind::kSection) --start;
        start = selectable_from(start, 1);
        if (start < focus_) to = start;
        else {   // already at it: the first selectable row of the section before (the rows above this one's heading)
            std::size_t head = focus_;
            while (head > 0 && r[head].kind != RowKind::kSection) --head;   // this section's heading (0: none)
            std::size_t prev = head;
            while (prev > 0 && r[prev - 1].kind != RowKind::kSection) --prev;
            if (head > 0 && r[head].kind == RowKind::kSection) to = selectable_from(prev, 1);
            if (to >= head) to = n;
        }
        if (to == n) to = selectable_from(0, 1);
    }
    if (to < n && to != focus_) focus_row(to, true);
}

// The wheel and the scroll arrows: the window moves, the focus stays inside it.
void Menu::scroll(int by) {
    const std::vector<Row>* rows = tab_rows();
    const std::size_t n = rows ? rows->size() : 0;
    if (n <= kVisible || by == 0) return;
    const long top = std::clamp(long(scroll_) + by, 0L, long(n - kVisible));
    scroll_ = std::size_t(top);
    const std::size_t end = scroll_ + kVisible;
    if (focus_ >= scroll_ && focus_ < end) return;
    std::size_t pick = n;
    if (focus_ < scroll_) {
        for (std::size_t i = scroll_; i < end && pick == n; ++i)
            if ((*rows)[i].kind != RowKind::kSection) pick = i;
    } else {
        for (std::size_t i = end; i-- > scroll_ && pick == n;)
            if ((*rows)[i].kind != RowKind::kSection) pick = i;
    }
    if (pick == n) return;   // only section rows in view: the focus stays (and is out of view until the next move)
    if (dragging_) dragging_ = false;
    focus_ = pick;
    focus_id_ = (*rows)[pick].id;
}

// An edit of the focused row to `value` (only a change within range is an edit). The copy of the content takes it at
// once, so the rest of the poll and the items see it; the caller applies the edit to the settings.
void Menu::set_value(std::size_t value, Frame& out) {
    if (tab_ >= content_.tabs.size() || focus_ >= content_.tabs[tab_].rows.size()) return;
    Row& r = content_.tabs[tab_].rows[focus_];
    if (r.disabled || value >= value_count(r) || value == r.value) return;
    r.value = value;
    out.edits.push_back(Edit{r.id, value, 0, false});
}

void Menu::change(int direction, Frame& out) {
    if (tab_ >= content_.tabs.size() || focus_ >= content_.tabs[tab_].rows.size()) return;
    const Row& r = content_.tabs[tab_].rows[focus_];
    if (r.disabled || r.kind == RowKind::kSection) return;
    if (r.kind == RowKind::kAction) {
        out.edits.push_back(Edit{r.id, 0, direction, true});
        return;
    }
    if (direction < 0 && r.value == 0) return;
    const std::size_t before = out.edits.size();
    set_value(direction < 0 ? r.value - 1 : r.value + 1, out);
    if (out.edits.size() > before) out.edits.back().direction = direction;
}

void Menu::activate(Frame& out) {
    if (tab_ >= content_.tabs.size() || focus_ >= content_.tabs[tab_].rows.size()) return;
    const Row& r = content_.tabs[tab_].rows[focus_];
    if (r.disabled) return;
    if (r.kind == RowKind::kAction) out.edits.push_back(Edit{r.id, 0, 0, true});
    else if (r.kind == RowKind::kToggle) set_value(r.value ? 0 : 1, out);
}

void Menu::handle_pointer(const Input& in, Frame& out) {
    const bool click = in.click;
    if (!in.pointer) {
        dragging_ = false;
        return;
    }
    const float px = (*in.pointer)[0], py = (*in.pointer)[1];
    const std::vector<Row>* rows = tab_rows();
    const std::size_t n = rows ? rows->size() : 0;
    const auto slider_level = [&](const Row& r) {
        const float t = std::clamp((px - kBarX0) / (kBarX1 - kBarX0), 0.0f, 1.0f);
        return std::size_t(std::lround(t * float(r.levels > 0 ? r.levels - 1 : 0)));
    };
    if (dragging_) {   // a slider drag: the level under the pointer while the button stays down on that slider
        if (in.click_held && rows && focus_ < n && (*rows)[focus_].id == drag_id_ && (*rows)[focus_].kind == RowKind::kSlider) {
            set_value(slider_level((*rows)[focus_]), out);
            return;
        }
        dragging_ = false;
    }
    if (click) {
        for (std::size_t k = 0; k < content_.tabs.size(); ++k)
            if (tab_box(k, content_.tabs.size()).in(px, py)) {
                switch_tab(k);
                return;
            }
        if (prompt_box(false).in(px, py)) {
            if (tab_ > 0) switch_tab(tab_ - 1);
            return;
        }
        if (prompt_box(true).in(px, py)) {
            switch_tab(tab_ + 1);
            return;
        }
        if (n > kVisible && scroll_up_box().in(px, py)) {
            scroll(-1);
            return;
        }
        if (n > kVisible && scroll_down_box().in(px, py)) {
            scroll(1);
            return;
        }
    }
    if (in.wheel && n > kVisible) scroll(-in.wheel);   // wheel up (positive) shows the rows above
    if (!rows || (!click && !in.pointer_moved) || py < kListY || px < kListX0 || px >= kListX1) return;
    const std::size_t slot = std::size_t((py - kListY) / kRowH);
    const std::size_t index = scroll_ + slot;
    if (slot >= kVisible || index >= n) return;
    const Row& r = (*rows)[index];
    if (r.kind == RowKind::kSection) return;   // section rows ignore the pointer
    if (index != focus_) focus_row(index, false);
    if (!click || r.disabled) return;
    const float y = row_box(slot).y;
    switch (r.kind) {
    case RowKind::kToggle:
        if (px >= kCtrlX0 && px < kCtrlX1) activate(out);
        break;
    case RowKind::kChoice:
    case RowKind::kSlider:
        if (arrow_box(y, false).in(px, py)) change(-1, out);
        else if (arrow_box(y, true).in(px, py)) change(1, out);
        else if (px >= kBarX0 - 4.0f && px < kBarX1 + 4.0f) {
            if (r.kind == RowKind::kSlider) {
                set_value(slider_level(r), out);
                dragging_ = true;
                drag_id_ = r.id;
            } else if (py >= y + kRowH * 0.5f && !r.values.empty()) {   // the value bar under the value text
                const float t = std::clamp((px - kBarX0) / (kBarX1 - kBarX0), 0.0f, 0.9999f);
                set_value(std::size_t(t * float(r.values.size())), out);
            }
        }
        break;
    case RowKind::kAction:
        if (button_box(y).in(px, py)) activate(out);
        break;
    case RowKind::kSection:
        break;
    }
}

Frame Menu::update(const Content& content, const Input& in) {
    Frame out;
    content_ = content;
    const float dt = std::clamp(in.seconds, 0.0f, 1.0f);
    clock_ = std::fmod(clock_ + dt, 3600.0f);
    open_time_ = std::min(open_time_ + dt, 60.0f);
    tab_time_ = std::min(tab_time_ + dt, 60.0f);
    if (tab_ >= content_.tabs.size()) tab_ = 0;
    if (memory_.size() < content_.tabs.size()) {
        memory_.resize(content_.tabs.size(), 0);
        remembered_.resize(content_.tabs.size(), false);
    }
    // 1. the content is new each poll: the focus follows its row id
    {
        const std::vector<Row>* rows = tab_rows();
        std::size_t index = first_selectable();
        if (rows && focus_known_)
            for (std::size_t i = 0; i < rows->size(); ++i)
                if ((*rows)[i].id == focus_id_ && (*rows)[i].kind != RowKind::kSection) {
                    index = i;
                    break;
                }
        const bool moved = !focus_known_ || index != focus_;
        focus_row(index, moved);
        if (!focus_known_) focus_ = 0;
    }
    // 2. edges (released first after the opening press), 3. the hold repeat
    const bool confirm = in.confirm && !was_confirm_, back = in.back && !was_back_;
    const bool previous = in.previous_tab && !was_previous_, next = in.next_tab && !was_next_;
    was_confirm_ = in.confirm;
    const bool reset = in.reset && !was_reset_;
    was_reset_ = in.reset;
    was_back_ = in.back;
    was_previous_ = in.previous_tab;
    was_next_ = in.next_tab;
    const bool popup = !content_.popup.empty();
    const bool both_lr = in.left && in.right, both_ud = in.up && in.down;
    const bool up = up_.step(in.up && !both_ud, dt, popup, true), down = down_.step(in.down && !both_ud, dt, popup, true);
    const bool both_sec = in.previous_section && in.next_section;
    const bool sec_up = section_up_.step(in.previous_section && !both_sec, dt, popup, true);
    const bool sec_down = section_down_.step(in.next_section && !both_sec, dt, popup, true);
    const bool left = left_.step(in.left && !both_lr, dt, popup), right = right_.step(in.right && !both_lr, dt, popup);
    // 4. a popup: nothing acts
    if (popup) {
        dragging_ = false;
        return out;
    }
    // 5. back, else the tabs, else focus / change / activate, then the pointer
    if (back) {
        out.close = true;
        dragging_ = false;
        return out;
    }
    if (previous || next) {
        if (previous && !next && tab_ > 0) switch_tab(tab_ - 1);
        if (next && !previous) switch_tab(tab_ + 1);
        return out;
    }
    if (const std::vector<Row>* rows = tab_rows(); rows && focus_known_) {
        if (up)
            for (std::size_t i = focus_; i-- > 0;)
                if ((*rows)[i].kind != RowKind::kSection) {
                    focus_row(i, true);
                    break;
                }
        if (down)
            for (std::size_t i = focus_ + 1; i < rows->size(); ++i)
                if ((*rows)[i].kind != RowKind::kSection) {
                    focus_row(i, true);
                    break;
                }
        if (sec_up) section_step(-1);
        if (sec_down) section_step(1);
        if (left) change(-1, out);
        if (right) change(1, out);
        if (confirm) activate(out);
        if (reset && focus_ < (*rows).size() && (*rows)[focus_].kind == RowKind::kAction && !(*rows)[focus_].disabled &&
            (*rows)[focus_].id >= kActionCtlFirst && (*rows)[focus_].id <= kActionCtlLast)
            out.edits.push_back(Edit{(*rows)[focus_].id, 0, 2, true});
    }
    handle_pointer(in, out);
    return out;
}

// ---- the page as items (§3)

namespace {

constexpr std::uint32_t rgba(int r, int g, int b, int a = 255) {
    return std::uint32_t(r) | std::uint32_t(g) << 8 | std::uint32_t(b) << 16 | std::uint32_t(a) << 24;
}
std::uint32_t faded(std::uint32_t c, float alpha) {
    const float a = std::clamp(float(c >> 24) * alpha, 0.0f, 255.0f);
    return (c & 0x00FFFFFFu) | std::uint32_t(std::lround(a)) << 24;
}
float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The palette: the game's deep blue panels, white italic menu text, a warm accent for what is chosen.
constexpr std::uint32_t kBackdrop = rgba(6, 12, 32, 255);
constexpr std::uint32_t kGrid = rgba(70, 120, 210, 26);
constexpr std::uint32_t kPanel = rgba(16, 36, 84, 214);
constexpr std::uint32_t kPanelEdge = rgba(70, 130, 230, 150);
constexpr std::uint32_t kTitleBar = rgba(28, 84, 196, 255);
constexpr std::uint32_t kTabIdle = rgba(18, 40, 92, 230);
constexpr std::uint32_t kTabOn = rgba(44, 112, 232, 255);
constexpr std::uint32_t kFocusBar = rgba(38, 104, 224, 235);
constexpr std::uint32_t kAccent = rgba(255, 176, 32, 255);
constexpr std::uint32_t kText = rgba(255, 255, 255, 255);
constexpr std::uint32_t kTextSoft = rgba(176, 196, 232, 255);
constexpr std::uint32_t kTextDim = rgba(108, 124, 156, 255);
constexpr std::uint32_t kTrack = rgba(6, 14, 38, 220);
constexpr std::uint32_t kFill = rgba(255, 176, 32, 255);
constexpr std::uint32_t kFillIdle = rgba(150, 178, 226, 255);
constexpr std::uint32_t kShade = rgba(0, 0, 0, 150);

struct Builder {
    std::vector<Item>& out;
    const Measure& measure;
    std::string art;
    float alpha = 1.0f;

    void rect(float x, float y, float w, float h, std::uint32_t c, std::uint8_t flags = 0) {
        if (w <= 0 || h <= 0) return;
        Item i;
        i.kind = ItemKind::kRect;
        i.flags = flags;
        i.x = std::round(x);   // rects and images on whole pixels
        i.y = std::round(y);
        i.w = std::round(x + w) - i.x;
        i.h = std::round(y + h) - i.y;
        i.colour = faded(c, alpha);
        out.push_back(std::move(i));
    }
    void image(const char* name, float x, float y, float w, float h, std::uint32_t c) {
        Item i;
        i.kind = ItemKind::kImage;
        i.x = std::round(x);
        i.y = std::round(y);
        i.w = std::round(x + w) - i.x;
        i.h = std::round(y + h) - i.y;
        i.colour = faded(c, alpha);
        i.text = art.empty() ? std::string(name) : art + "/" + name;
        out.push_back(std::move(i));
    }
    float width(std::string_view s, float h, std::uint8_t flags) const { return measure(s, h, flags); }
    // A text in its box (the renderer clips it there), shrunk to fit when it is wider (down to 75 %).
    void text(std::string_view s, float x, float y, float w, float h, std::uint32_t c, std::uint8_t flags) {
        if (s.empty() || w <= 0) return;
        if (s.size() > 1024) s = s.substr(0, 1024);
        float size = h;
        const float slant = (flags & kTextSizeMask) == kItalic ? kItalicSlant * h : 0.0f;
        const float tw = width(s, h, flags) + slant;
        if (tw > w) size = std::max(h * 0.75f, h * w / tw);
        Item i;
        i.kind = ItemKind::kText;
        i.flags = flags;
        i.x = x;
        i.y = y + (h - size) * 0.5f;
        i.w = w;
        i.h = size;
        i.colour = faded(c, alpha);
        i.text = std::string(s);
        out.push_back(std::move(i));
    }
    // The words of `s` over at most `lines` lines of width `w` (the last line takes the rest; clipped if longer)
    std::vector<std::string> wrap(std::string_view s, float w, float h, std::uint8_t flags, std::size_t lines) const {
        std::vector<std::string> o;
        std::string line;
        std::size_t k = 0;
        while (k < s.size()) {
            while (k < s.size() && s[k] == ' ') ++k;
            std::size_t e = k;
            while (e < s.size() && s[e] != ' ') ++e;
            if (e == k) break;
            const std::string_view word = s.substr(k, e - k);
            std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
            if (!line.empty() && o.size() + 1 < lines && width(candidate, h, flags) > w) {
                o.push_back(line);
                line = std::string(word);
            } else {
                line = std::move(candidate);
            }
            k = e;
        }
        if (!line.empty()) o.push_back(line);
        return o;
    }
};

}  // namespace

std::vector<Item> Menu::items(const std::string& fonts, const std::string& art, const Measure& measure_in) const {
    std::vector<Item> out;
    out.reserve(256);
    {
        Item f;
        f.kind = ItemKind::kFontDirectory;
        f.text = fonts;
        out.push_back(std::move(f));
    }
    const Measure measure = measure_in ? measure_in : Measure(fallback_measure);
    const float open = smooth(open_time_ / 0.22f);
    Builder b{out, measure, art, open};

    // the backdrop over the whole picture (the menu behind it hidden): deep blue, a faint grid, a lighter wash at the top
    b.rect(0, 0, kPictureW, kPictureH, kBackdrop);
    for (float x = 16; x < kPictureW; x += 32) b.rect(x, 0, 1, kPictureH, kGrid);
    for (float y = 8; y < kPictureH; y += 32) b.rect(0, y, kPictureW, 1, kGrid);
    b.rect(0, 0, kPictureW, 84, rgba(20, 52, 128, 70), kRectShade);

    // the title: a slanted blue bar, the warm "PC" tag and SETTINGS in the heading face, two warm stripes after it
    {
        const float y = kTitleY;
        b.rect(kFrameX0, y, 300, kTitleH, kTitleBar, kRectSlant | kRectShade);
        b.rect(kFrameX0 + 302, y, 12, kTitleH, kAccent, kRectSlant);
        b.rect(kFrameX0 + 318, y, 7, kTitleH, rgba(255, 176, 32, 150), kRectSlant);
        b.text("PC", kFrameX0 + 12, y + 6, 34, 18, kAccent, kItalic);
        b.text("SETTINGS", kFrameX0 + 46, y + 3, 230, kTitleH - 4, kText, kHeadingFont);
    }

    // the tabs: the L1 / R1 prompts at both ends, the current tab a bright slanted panel
    const std::size_t ntabs = content_.tabs.size();
    {
        const float y = kTabY;
        for (int side = 0; side < 2; ++side) {
            const Box p = prompt_box(side == 1);
            const bool can = side == 0 ? tab_ > 0 : tab_ + 1 < ntabs;
            b.rect(p.x, y, p.w, kTabH, rgba(10, 22, 56, 230));
            b.rect(p.x, y + kTabH - 2, p.w, 2, can ? kAccent : kTextDim);
            b.image(side == 0 ? kImagePromptL1 : kImagePromptR1, p.x + 3, y + 4, p.w - 6, kTabH - 8, can ? kText : kTextDim);
        }
        for (std::size_t k = 0; k < ntabs; ++k) {
            const Box t = tab_box(k, ntabs);
            const bool on = k == tab_;
            b.rect(t.x + 2, y, t.w - 4, kTabH, on ? kTabOn : kTabIdle, kRectSlant | kRectShade);
            if (on) b.rect(t.x + 2, y + kTabH - 3, t.w - 6, 3, kAccent, kRectSlant);
            b.text(content_.tabs[k].label, t.x + 6, y + 5, t.w - 12, kTabH - 9, on ? kText : kTextSoft, kItalic | kAlignCentre);
        }
    }

    // the rows' panel
    const std::vector<Row>* rows = tab_rows();
    const std::size_t n = rows ? rows->size() : 0;
    const float list_y = kListY;
    b.rect(kFrameX0, list_y - 4, kFrameX1 - kFrameX0, kRowH * float(kVisible) + 8, kPanel);
    b.rect(kFrameX0, list_y - 4, kFrameX1 - kFrameX0, 1, kPanelEdge);
    b.rect(kFrameX0, list_y + kRowH * float(kVisible) + 3, kFrameX1 - kFrameX0, 1, kPanelEdge);

    const float switch_t = smooth(tab_time_ / 0.16f);
    const float shift = std::round(float(tab_from_) * 10.0f * (1.0f - switch_t));
    const float pulse = 0.5f + 0.5f * std::sin(clock_ * 4.0f);
    const std::size_t end = std::min(n, scroll_ + kVisible);
    for (std::size_t i = scroll_; i < end; ++i) {
        const Row& r = (*rows)[i];
        const std::size_t slot = i - scroll_;
        const float y = list_y + kRowH * float(slot);
        b.alpha = open * (0.5f + 0.5f * switch_t);
        const float x0 = kListX0 + shift;
        const bool focused = focus_known_ && i == focus_;
        if (r.kind == RowKind::kSection) {
            b.text(r.label, x0 + 6, y + 9, 220, 13, kAccent, kTextSmall);
            const float lw = std::min(220.0f, b.width(r.label, 13, kTextSmall));
            b.rect(x0 + 14 + lw, y + 15, kListX1 - (x0 + 14 + lw) - 6 + shift, 1, rgba(255, 176, 32, 110));
            continue;
        }
        if (focused) {
            b.rect(x0, y + 1, kListX1 - kListX0, kRowH - 2, faded(kFocusBar, 0.86f + 0.14f * pulse), kRectSlant | kRectShade);
            b.rect(x0, y + 1, 9, kRowH - 2, kAccent, kRectSlant);
        }
        const std::uint32_t label = r.disabled ? kTextDim : focused ? kText : kTextSoft;
        b.text(r.label, kLabelX + shift, y + 4, kCtrlX0 - kLabelX - 12, 15, label, kItalic);
        const float cx0 = kCtrlX0 + shift, cx1 = kCtrlX1 + shift;
        const float bx0 = kBarX0 + shift, bx1 = kBarX1 + shift;
        const std::uint32_t value_c = r.disabled ? kTextDim : kText;
        const std::uint32_t arrow_c = r.disabled ? kTextDim : focused ? kText : kTextSoft;
        switch (r.kind) {
        case RowKind::kToggle: {
            const bool on = r.value != 0;
            const Box t = toggle_box(y);
            const float tx = t.x + shift;
            const std::string shown = on ? (r.values.size() > 1 ? r.values[1] : "On") : (!r.values.empty() ? r.values[0] : "Off");
            b.text(shown, cx0, y + 5, tx - cx0 - 8, 14, value_c, kItalic | kAlignRight);
            b.rect(tx, t.y, t.w, t.h, on && !r.disabled ? rgba(255, 176, 32, 235) : kTrack);
            b.rect(tx, t.y, t.w, 1, rgba(255, 255, 255, 60));
            const float knob = t.h - 4;
            b.rect(on ? tx + t.w - knob - 2 : tx + 2, t.y + 2, knob, knob, r.disabled ? kTextDim : on ? kText : kFillIdle);
            break;
        }
        case RowKind::kChoice:
        case RowKind::kSlider: {
            const std::size_t count = value_count(r);
            const bool lo = !r.disabled && r.value > 0, hi = !r.disabled && r.value + 1 < count;
            b.image(kImageArrowLeft, cx0 + 6, y + 6, 8, 12, lo ? arrow_c : faded(arrow_c, 0.3f));
            b.image(kImageArrowRight, cx1 - 14, y + 6, 8, 12, hi ? arrow_c : faded(arrow_c, 0.3f));
            const std::string shown = r.kind == RowKind::kSlider ? r.level_text
                                    : r.value < r.values.size() ? r.values[r.value] : std::string();
            b.text(shown, bx0, y + 2, bx1 - bx0, 14, value_c, kItalic | kAlignCentre);
            const float by = y + kRowH - 6;
            if (r.kind == RowKind::kSlider) {
                b.rect(bx0, by, bx1 - bx0, 3, kTrack);
                const float t = count > 1 ? float(r.value) / float(count - 1) : 0.0f;
                b.rect(bx0, by, (bx1 - bx0) * t, 3, r.disabled ? kTextDim : focused ? kFill : kFillIdle);
                b.rect(bx0 + (bx1 - bx0) * t - 2, by - 2, 4, 7, r.disabled ? kTextDim : kText);
            } else if (count > 0) {
                const float gap = count > 12 ? 1.0f : 2.0f;
                const float seg = (bx1 - bx0 + gap) / float(count);
                for (std::size_t k = 0; k < count; ++k) {
                    const bool cur = k == r.value;
                    b.rect(bx0 + seg * float(k), by, seg - gap, 3, cur ? (r.disabled ? kTextDim : focused ? kFill : kFillIdle) : kTrack);
                }
            }
            break;
        }
        case RowKind::kAction: {
            const Box t = button_box(y);
            const float tx = t.x + shift;
            const std::string v = r.values.empty() ? std::string() : r.values[0];
            if (r.disabled) {
                b.text(v, cx0, y + 5, cx1 - cx0, 14, kTextDim, kItalic | kAlignRight);
            } else {
                b.rect(tx, t.y, t.w, t.h, focused ? kAccent : rgba(26, 60, 136, 240), kRectSlant | kRectShade);
                b.text(v, tx + 4, t.y + 3, t.w - 10, t.h - 6, focused ? rgba(16, 20, 40, 255) : kText, kItalic | kAlignCentre | kNoOutline);
            }
            break;
        }
        case RowKind::kSection:
            break;
        }
    }
    b.alpha = open;

    // the scroll bar (only when rows are hidden): an arrow towards hidden rows, the thumb for the window
    if (n > kVisible) {
        const Box up = scroll_up_box(), down = scroll_down_box();
        const bool above = scroll_ > 0, below = scroll_ + kVisible < n;
        const float track_y = up.y + up.h, track_h = down.y - (up.y + up.h) - 2;
        b.rect(up.x + up.w * 0.5f - 2, track_y, 4, track_h, kTrack);
        const float th = std::max(12.0f, track_h * float(kVisible) / float(n));
        const float ty = track_y + (track_h - th) * float(scroll_) / float(n - kVisible);
        b.rect(up.x + up.w * 0.5f - 2, ty, 4, th, kFillIdle);
        if (above) b.image(kImageArrowUp, up.x + 1, up.y + 4, up.w - 2, 9, kAccent);
        if (below) b.image(kImageArrowDown, down.x + 1, down.y + 4, down.w - 2, 9, kAccent);
    }

    // the focused row's description
    {
        const float y = kDescY;
        b.rect(kFrameX0, y, kFrameX1 - kFrameX0, kDescH, rgba(10, 24, 60, 220));
        b.rect(kFrameX0, y, 4, kDescH, kAccent);
        if (rows && focus_known_ && focus_ < n) {
            const std::string& d = (*rows)[focus_].description;
            const float lh = 13, w = kFrameX1 - kFrameX0 - 28;
            const std::vector<std::string> lines = b.wrap(d, w, lh, kTextSmall, 3);
            for (std::size_t k = 0; k < lines.size(); ++k)
                b.text(lines[k], kFrameX0 + 14, y + 4 + 16.0f * float(k), w, lh, kTextSoft, kTextSmall);
        }
    }

    // the footer: what the buttons do; Back closes and saves
    {
        const float y = kFooterY;
        b.rect(kFrameX0, y, kFrameX1 - kFrameX0, kFooterH, rgba(8, 18, 46, 220));
        b.text("UP / DOWN  SELECT     LEFT / RIGHT  CHANGE     CONFIRM  CHOOSE", kFrameX0 + 12, y + 5, 380, 12, kTextSoft, kTextSmall);
        b.text("BACK  CLOSE AND SAVE", kFrameX1 - 212, y + 5, 200, 12, kAccent, kTextSmall | kAlignRight);
    }

    // a popup over the page
    if (!content_.popup.empty()) {
        b.rect(0, 0, kPictureW, kPictureH, kShade);
        const float w = 420, x = (kPictureW - w) * 0.5f, lh = 13;
        const std::vector<std::string> lines = b.wrap(content_.popup, w - 40, lh, kTextSmall, 8);
        const float h = 36 + 17.0f * float(lines.size());
        const float y = std::round((kPictureH - h) * 0.5f);
        b.rect(x, y, w, h, rgba(20, 50, 120, 250), kRectShade);
        b.rect(x, y, w, 3, kAccent);
        for (std::size_t k = 0; k < lines.size(); ++k)
            b.text(lines[k], x + 20, y + 18 + 17.0f * float(k), w - 40, lh, kText, kTextSmall | kAlignCentre);
    }
    return out;
}

}  // namespace b3settings
