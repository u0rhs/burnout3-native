// The mouse in the game's own menus, the renderer's side: src/b3replay/menumouse.h.
#include "menumouse.h"
#include "garagecam.h"   // garagecam1: a drag on a car page

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace b3replay {

const MenuRowTarget* MenuTargets::row_at(float x, float y) const {
    for (const MenuRowTarget& r : rows) if (r.box.contains(x, y)) return &r;
    return nullptr;
}
const MenuPromptTarget* MenuTargets::prompt_at(float x, float y) const {
    for (const MenuPromptTarget& p : prompts) if (p.box.contains(x, y)) return &p;
    return nullptr;
}
std::uint32_t MenuTargets::index_of(std::uint32_t select) const {
    for (const auto& [s, i] : current) if (s == select) return i;
    return ~0u;
}

std::vector<MenuRowTarget> menu_row_bands(std::uint32_t select, const std::vector<ScreenBox>& rows, float pad) {
    std::vector<std::size_t> shown;
    ScreenBox col;
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (!rows[i].empty()) { shown.push_back(i); col.add(rows[i]); }
    std::vector<MenuRowTarget> out;
    if (shown.empty()) return out;
    std::sort(shown.begin(), shown.end(), [&](std::size_t a, std::size_t b) { return rows[a].cy() < rows[b].cy(); });
    // the usual spacing (the smallest gap between neighbouring rows' centres; one row: its own height)
    float step = 1e30f;
    for (std::size_t k = 1; k < shown.size(); ++k) step = std::min(step, rows[shown[k]].cy() - rows[shown[k - 1]].cy());
    if (shown.size() < 2 || !(step > 0.0f) || step > 1.0f) step = rows[shown[0]].y1 - rows[shown[0]].y0;
    for (std::size_t k = 0; k < shown.size(); ++k) {
        const ScreenBox& b = rows[shown[k]];
        MenuRowTarget t;
        t.select = select;
        t.row = std::uint32_t(shown[k]);
        t.box.x0 = col.x0 - pad;
        t.box.x1 = col.x1 + pad;
        t.box.y0 = k == 0 ? std::min(b.y0, b.cy() - 0.5f * step) : 0.5f * (rows[shown[k - 1]].cy() + b.cy());
        t.box.y1 = k + 1 == shown.size() ? std::max(b.y1, b.cy() + 0.5f * step) : 0.5f * (b.cy() + rows[shown[k + 1]].cy());
        out.push_back(t);
    }
    return out;
}

std::vector<MenuPromptTarget> menu_prompts(const HudFrame& f, const std::vector<std::vector<ScreenBox>>& pieces,
                                           const std::function<int(const HudDraw&, std::size_t piece)>& button_of,
                                           const std::function<bool(const HudDraw&)>& text) {
    std::vector<MenuPromptTarget> out;
    std::vector<ScreenBox> rings, glyphs;
    for (std::size_t di = 0; di < f.draws.size() && di < pieces.size(); ++di) {
        const HudDraw& d = f.draws[di];
        const bool is_text = text(d);
        for (std::size_t p = 0; p < pieces[di].size(); ++p) {
            const ScreenBox& b = pieces[di][p];
            if (b.empty()) continue;
            if (is_text) { glyphs.push_back(b); continue; }
            const int k = button_of(d, p);
            if (k >= 0) out.push_back({std::uint8_t(k), b});
            else if (k == -2) rings.push_back(b);
        }
    }
    for (MenuPromptTarget& t : out) {   // the button's ring around its symbol
        const ScreenBox sym = t.box;
        for (const ScreenBox& r : rings) if (r.contains(sym.cx(), sym.cy())) t.box.add(r);
    }
    for (std::size_t i = 0; i < out.size(); ++i)   // one button drawn in two pieces (Start: its outline and its picture)
        for (std::size_t j = i + 1; j < out.size();) {
            const ScreenBox &a = out[i].box, &b = out[j].box;
            if (out[j].button == out[i].button && a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1) {
                out[i].box.add(b);
                out.erase(out.begin() + std::ptrdiff_t(j));
            } else {
                ++j;
            }
        }
    // the labels: the glyphs on a button's row joined into words (gaps under a third of a button's height), each word
    // given to the nearest button on its row within a button's height ("Select (X)": left of it; "(T) Back": right)
    std::sort(glyphs.begin(), glyphs.end(), [](const ScreenBox& a, const ScreenBox& b) { return a.x0 < b.x0; });
    std::vector<ScreenBox> buttons;
    for (const MenuPromptTarget& t : out) buttons.push_back(t.box);
    std::vector<bool> used(glyphs.size(), false);
    for (std::size_t bi = 0; bi < buttons.size(); ++bi) {
        const ScreenBox& btn = buttons[bi];
        const float h = btn.y1 - btn.y0;
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            if (used[i] || glyphs[i].cy() < btn.y0 || glyphs[i].cy() > btn.y1) continue;
            // the word starting at glyph i (on this row)
            ScreenBox word = glyphs[i];
            std::vector<std::size_t> members = {i};
            for (std::size_t j = i + 1; j < glyphs.size(); ++j) {
                if (used[j] || glyphs[j].cy() < btn.y0 || glyphs[j].cy() > btn.y1) continue;
                if (glyphs[j].x0 > word.x1 + h / 3.0f) break;
                word.add(glyphs[j]);
                members.push_back(j);
            }
            auto dist = [&](const ScreenBox& b) { return word.x0 >= b.x1 ? word.x0 - b.x1 : b.x0 >= word.x1 ? b.x0 - word.x1 : 0.0f; };
            std::size_t best = bi;
            for (std::size_t k = 0; k < buttons.size(); ++k)
                if (buttons[k].cy() >= word.y0 && buttons[k].cy() <= word.y1 && dist(buttons[k]) < dist(buttons[best])) best = k;
            if (best == bi && dist(btn) <= h) {
                out[bi].box.add(word);
                for (std::size_t m : members) used[m] = true;
            }
            i = members.back();
        }
    }
    for (MenuPromptTarget& t : out) {   // a little slack round the whole prompt (the ring's edge, the gap before the label)
        const float h = t.box.y1 - t.box.y0;
        t.box.y0 -= 0.1f * h; t.box.y1 += 0.1f * h; t.box.x0 -= 0.1f * h; t.box.x1 += 0.1f * h;
    }
    return out;
}

// A row's text pieces as one box. The game batches text draws, so a choice's draw can also hold the lines drawn before it
// (a popup's question comes first, then YES; the draw carries the first object's address): the pieces are grouped into
// lines by height, and a gap of more than one and a half letter heights above the last line ends the row's text there.
ScreenBox last_text_run(std::vector<ScreenBox> p) {
    ScreenBox out;
    p.erase(std::remove_if(p.begin(), p.end(), [](const ScreenBox& b) { return b.empty(); }), p.end());
    if (p.empty()) return out;
    std::sort(p.begin(), p.end(), [](const ScreenBox& a, const ScreenBox& b) { return a.cy() > b.cy(); });   // bottom first
    float h = 0.0f;
    for (const ScreenBox& b : p) h = std::max(h, b.y1 - b.y0);
    float top = p[0].y0;   // the run's top so far
    for (const ScreenBox& b : p) {
        if (top - b.y1 > 1.5f * h) break;
        out.add(b);
        top = std::min(top, b.y0);
    }
    return out;
}

MenuTargets menu_targets(const HudFrame& f, const std::vector<std::vector<ScreenBox>>& pieces, const b3live::MenuRecord* rec,
                         bool prompt_bar, const std::function<int(const HudDraw&, std::size_t piece)>& button_of,
                         const std::function<bool(const HudDraw&)>& text) {
    MenuTargets t;
    if (rec) {
        t.text_entry = rec->text_entry;
        for (const b3live::MenuSelect& s : rec->selects) {
            t.current.emplace_back(s.addr, s.index);
            std::vector<ScreenBox> rows(std::min<std::uint32_t>(s.count, b3live::kMenuRows));
            std::vector<std::vector<ScreenBox>> parts(rows.size());
            for (std::size_t di = 0; di < f.draws.size() && di < pieces.size(); ++di) {
                const HudDraw& d = f.draws[di];
                // the row's text only: the selected row's object also owns the prompt draws after it (pause: START Resume)
                if (!text(d) || d.object < 0 || std::size_t(d.object) >= f.objects.size()) continue;
                const std::uint32_t o = f.objects[std::size_t(d.object)].addr & 0x01FFFFFFu;
                // the capture's object address is its render data ($a1) - 0x28, 8 bytes before the CGt2dObject* the
                // component holds (main menu: row 01E926C0, object event 01E926B8 [C mm3 trace])
                for (std::size_t i = 0; i < rows.size(); ++i)
                    if (s.rows[i] && (o + 8u == (s.rows[i] & 0x01FFFFFFu) || o == (s.rows[i] & 0x01FFFFFFu)))
                        for (const ScreenBox& b : pieces[di]) parts[i].push_back(b);
            }
            for (std::size_t i = 0; i < rows.size(); ++i) rows[i] = last_text_run(parts[i]);
            const auto bands = menu_row_bands(s.addr, rows, 0.02f);
            t.rows.insert(t.rows.end(), bands.begin(), bands.end());
        }
    }
    t.prompts = menu_prompts(f, pieces, button_of, text);
    t.menu = !t.rows.empty() || prompt_bar;
    return t;
}

MenuMouse& MenuMouse::instance() {
    static MenuMouse* m = new MenuMouse();   // never destroyed (the game thread may outlive static destruction)
    return *m;
}

void MenuMouse::move(float x, float y, double) {
    std::lock_guard<std::mutex> l(mu_);
    if (events_.size() < 256) events_.push_back({Event::kMove, x, y, 0});
}
void MenuMouse::button(int b, bool down, double) {
    std::lock_guard<std::mutex> l(mu_);
    if (events_.size() < 256) events_.push_back({down ? Event::kDown : Event::kUp, 0, 0, b});
}
void MenuMouse::wheel(int steps, double) {
    std::lock_guard<std::mutex> l(mu_);
    if (steps && events_.size() < 256) events_.push_back({Event::kWheel, 0, 0, steps});
}
void MenuMouse::other_device(double) {
    std::lock_guard<std::mutex> l(mu_);
    hidden_by_device_ = true;
    hover_select_ = 0;
    hover_row_ = ~0u;
}
bool MenuMouse::pending() const {
    std::lock_guard<std::mutex> l(mu_);
    return !events_.empty();
}
bool MenuMouse::cursor_wanted(double now) const {
    std::lock_guard<std::mutex> l(mu_);
    return !hidden_by_device_ && now - last_move_ < kHideAfter;
}

void MenuMouse::emit(const b3live::MenuAction& a) {
    if (out_.size() < 64) out_.push_back(a);
    ++sent_;
}

void MenuMouse::frame(const MenuTargets& t, bool page_open, double now) {
    std::lock_guard<std::mutex> l(mu_);
    using b3live::MenuAction;
    namespace pad = b3live::pad;
    auto press = [&](std::uint8_t b, bool after = false) { MenuAction a; a.kind = MenuAction::kPress; a.button = b; a.after_target = after; emit(a); };
    auto target = [&](const MenuRowTarget& r) { MenuAction a; a.kind = MenuAction::kTarget; a.select = r.select; a.row = r.row; emit(a); };
    // garagecam1: a held press on a car page belongs to that page: the page going away (no longer a car page, another
    // page's selects, or the PC SETTINGS page opened over it) drops it, so its release never sends a Cross to what
    // is on screen by then
    const auto car_page = [&] { return !page_open && t.menu && t.rows.empty() && !t.text_entry && t.car; };
    const auto selects = [&] {
        std::vector<std::uint32_t> v;
        for (const auto& c : t.current) v.push_back(c.first);
        return v;
    };
    if (drag_held_ && (!car_page() || selects() != drag_page_)) drag_held_ = drag_moved_ = false;
    bool moved = false;
    for (const Event& e : events_) {
        switch (e.kind) {
        case Event::kMove:
            moved = moved || !have_pos_ || e.x != x_ || e.y != y_;
            if (drag_held_ && have_pos_) {   // garagecam1: the held press on a car page
                const float dx = e.x - x_, dy = e.y - y_;
                if (drag_moved_) {
                    GarageCam::instance().add_drag(dx, dy);
                } else {
                    drag_dx_ += dx;
                    drag_dy_ += dy;
                    if (drag_dx_ * drag_dx_ + drag_dy_ * drag_dy_ > kDragStart * kDragStart) {
                        drag_moved_ = true;
                        GarageCam::instance().add_drag(drag_dx_, drag_dy_);
                    }
                }
            }
            x_ = e.x; y_ = e.y; have_pos_ = true;
            last_move_ = now;
            hidden_by_device_ = false;
            break;
        case Event::kDown:
            if (!have_pos_) break;
            last_move_ = now;   // a click shows the cursor again
            hidden_by_device_ = false;
            if (e.n == 1) {
                if (page_open) { page_.click = page_.held = true; break; }
                if (const MenuRowTarget* r = t.row_at(x_, y_)) {
                    target(*r);
                    press(pad::kCross, true);
                    hover_select_ = r->select; hover_row_ = r->row; hover_at_ = now;
                } else if (const MenuPromptTarget* p = t.prompt_at(x_, y_)) {
                    press(p->button);
                } else if (car_page()) {
                    drag_held_ = true;   // garagecam1: a click (Cross on the release) or a drag (the view)
                    drag_moved_ = false;
                    drag_page_ = selects();
                    drag_dx_ = drag_dy_ = 0;
                } else if (t.menu && t.rows.empty() && !t.text_entry) {
                    press(pad::kCross);   // a page without rows (a message, the car or track pages): continue
                }
            } else if (e.n == 3) {
                if (page_open) { page_.back = true; break; }
                if (t.menu) { MenuAction c; c.kind = MenuAction::kCancel; emit(c); press(pad::kTriangle); }
            }
            break;
        case Event::kUp:
            if (e.n == 1) page_.held = false;
            if (e.n == 1 && drag_held_) {   // garagecam1: released without a drag: the click's Cross (the same car page)
                if (!drag_moved_ && car_page() && selects() == drag_page_) press(pad::kCross);
                drag_held_ = drag_moved_ = false;
            }
            break;
        case Event::kWheel:
            if (page_open) { page_.wheel += e.n; break; }
            if (!t.menu) break;
            {
                MenuAction c;
                c.kind = MenuAction::kCancel;
                emit(c);
                const int n = std::min(4, std::abs(e.n));
                for (int k = 0; k < n; ++k) press(e.n > 0 ? pad::kUp : pad::kDown);
            }
            hover_select_ = 0; hover_row_ = ~0u;   // the wheel moved the selection: the next move hovers again
            break;
        }
    }
    events_.clear();
    if (moved && !page_open) {
        if (const MenuRowTarget* r = t.row_at(x_, y_)) {
            const bool same = r->select == hover_select_ && r->row == hover_row_;
            const std::uint32_t cur = t.index_of(r->select);
            // a new row under the cursor, or the selection moved away from it (keys) and the mouse moves again
            if (cur != r->row && (!same || now - hover_at_ > 0.5)) { target(*r); hover_at_ = now; }
            hover_select_ = r->select;
            hover_row_ = r->row;
        } else {
            hover_select_ = 0;
            hover_row_ = ~0u;
        }
    }
    page_.have = have_pos_;
    page_.x = x_;
    page_.y = y_;
    page_.moved = page_.moved || moved;
}

MenuMouse::PagePointer MenuMouse::page_pointer() {
    std::lock_guard<std::mutex> l(mu_);
    PagePointer p = page_;
    page_.moved = page_.click = page_.back = false;
    page_.wheel = 0;
    return p;
}

int MenuMouse::take(std::uint32_t* out, int max) {
    std::lock_guard<std::mutex> l(mu_);
    int n = 0;
    for (; n < max && std::size_t(n) < out_.size(); ++n) b3live::menu_action_put(out_[std::size_t(n)], out + 3 * n);
    out_.erase(out_.begin(), out_.begin() + n);
    return n;
}

MenuTargets menu_targets_of(const HudFrame& f, const std::vector<std::vector<ScreenBox>>& pieces, const b3live::MenuRecord* rec,
                            const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures) {
    constexpr std::uint32_t kPromptBarRender = 0x003D7B40U;   // CPromptBarPanel2dObject::_Render [C name] (hud.cpp)
    bool bar = false;
    for (const HudObject& o : f.objects) bar = bar || o.pc == kPromptBarRender;
    auto tex = [&](const HudDraw& d) -> const HudTexture* {
        if (d.texture == 0) return nullptr;
        const auto t = textures.find(d.texture);
        return t != textures.end() ? t->second.get() : nullptr;
    };
    const auto button_of = [&](const HudDraw& d, std::size_t piece) {
        const HudTexture* t = tex(d);
        return t ? prompt_piece_button(t->name, t->w, t->h, d.tri, piece) : -1;
    };
    const auto text = [&](const HudDraw& d) {
        const HudTexture* t = tex(d);
        return t && t->name.size() >= 4 && t->name.compare(t->name.size() - 4, 4, "Font") == 0;   // GlobalFont, SmallFont, HeadFont
    };
    return menu_targets(f, pieces, rec, bar, button_of, text);
}

}  // namespace b3replay
