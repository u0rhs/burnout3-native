// mac5: the mouse and typed text in the game's own menus (remaster-features §8; include/b3live/menu_input.h,
// src/b3replay/menumouse.h): the menus record and the actions as they cross, the presses on the pad report, the closed
// loop that steps the selection to a row against a simulated menu, the text mapping by the page's allowed characters,
// the rows' hit bands and the prompts from draws, and the mouse's events turned into actions.
#include "test.h"

#include "b3live/menu_input.h"
#include "menumouse.h"
#include "prompts.h"

#include <string>
#include <vector>

namespace {

using b3live::MenuAction;
using b3live::MenuPresser;
using b3live::MenuRecord;
using b3live::MenuSelect;
using b3live::MenuSelectState;
using b3replay::HudDraw;
using b3replay::HudFrame;
using b3replay::HudObject;
using b3replay::HudVertex;
using b3replay::MenuMouse;
using b3replay::MenuTargets;
using b3replay::ScreenBox;
namespace pad = b3live::pad;

ScreenBox box(float x0, float y0, float x1, float y1) { ScreenBox b; b.add(x0, y0); b.add(x1, y1); return b; }

// A simulated menu page: a VSelect of `count` rows whose page takes Up / Down on the press's edge (one row each, no wrap),
// after `lag` VBlanks, and Cross as a confirm of the row it is on
struct FakeMenu {
    std::uint32_t index = 0, count = 5;
    int lag = 1;
    bool idle = true;
    std::uint32_t prev = 0;
    std::vector<std::pair<int, std::uint32_t>> pending;   // (VBlank due, buttons)
    std::vector<std::uint32_t> confirmed;
    int vblank = 0;
    void feed(std::uint32_t mask) {
        const std::uint32_t edge = mask & ~prev;
        prev = mask;
        if (edge) pending.push_back({vblank + lag, edge});
        for (auto it = pending.begin(); it != pending.end();) {
            if (it->first > vblank) { ++it; continue; }
            if ((it->second & (1u << pad::kDown)) && index + 1 < count) ++index;
            if ((it->second & (1u << pad::kUp)) && index > 0) --index;
            if (it->second & (1u << pad::kCross)) confirmed.push_back(index);
            it = pending.erase(it);
        }
        ++vblank;
    }
};

std::uint32_t run(MenuPresser& p, FakeMenu& m, int vblanks, std::uint32_t select = 0x1E89544) {
    std::uint32_t presses = 0, prev = 0;
    for (int k = 0; k < vblanks; ++k) {
        const std::uint32_t mask = p.step([&](std::uint32_t s, MenuSelectState& st) {
            if (s != select) return false;
            st.index = m.index; st.count = m.count; st.idle = m.idle;
            return true;
        });
        if (mask & ~prev) ++presses;
        prev = mask;
        m.feed(mask);
    }
    return presses;
}

MenuAction target(std::uint32_t s, std::uint32_t row) { MenuAction a; a.kind = MenuAction::kTarget; a.select = s; a.row = row; return a; }
MenuAction press(std::uint8_t b, bool after = false) { MenuAction a; a.kind = MenuAction::kPress; a.button = b; a.after_target = after; return a; }

}  // namespace

TEST(record_round_trip) {
    MenuRecord r;
    r.serial = 7;
    r.text_entry = true;
    MenuSelect s;
    s.addr = 0x1E89544; s.index = 2; s.count = 7; s.idle = true;
    for (unsigned i = 0; i < 7; ++i) s.rows[i] = 0x1F00000 + 0x100 * i;
    r.selects.push_back(s);
    const auto bytes = b3live::menu_record_bytes(r);
    CHECK(bytes.size() == 16 + (4 + b3live::kMenuRows) * 4);
    MenuRecord q;
    CHECK(b3live::menu_record_parse(bytes.data(), bytes.size(), q));
    CHECK(q.serial == 7 && q.text_entry && q.selects.size() == 1);
    CHECK(q.same_content(r));
    CHECK(q.selects[0].rows[6] == 0x1F00600 && q.selects[0].rows[7] == 0);
    CHECK(!b3live::menu_record_parse(bytes.data(), 15, q));
    auto bad = bytes;
    bad[12] = 9;   // more selects than kMenuSelects
    CHECK(!b3live::menu_record_parse(bad.data(), bad.size(), q));
}

TEST(action_encoding_and_pad_press) {
    MenuAction a = press(pad::kCross, true);
    std::uint32_t w[3];
    b3live::menu_action_put(a, w);
    const MenuAction b = b3live::menu_action_get(w);
    CHECK(b.kind == MenuAction::kPress && b.button == pad::kCross && b.after_target);
    b3live::menu_action_put(target(0x1E94C40, 6), w);
    const MenuAction c = b3live::menu_action_get(w);
    CHECK(c.kind == MenuAction::kTarget && c.select == 0x1E94C40 && c.row == 6 && !c.after_target);
    // Cross: byte 1 bit 6 cleared (active low), its pressure (byte 12) full; Start has no pressure
    std::uint8_t r[32] = {0xFF, 0xFF, 0x80, 0x7F, 0x80, 0x7F};
    b3live::pad_press(r, pad::kCross);
    CHECK(r[0] == 0xFF && r[1] == 0xBF && r[12] == 0xFF);
    b3live::pad_press(r, pad::kStart);
    CHECK(r[0] == 0xF7);
    b3live::pad_press(r, pad::kDown);
    CHECK(r[0] == 0xB7 && r[9] == 0xFF);
}

TEST(presser_steps_to_the_row_then_confirms) {
    MenuPresser p;
    FakeMenu m;
    p.push(target(0x1E89544, 3));
    p.push(press(pad::kCross, true));
    const std::uint32_t presses = run(p, m, 120);
    CHECK(m.index == 3);
    CHECK(presses == 4);   // Down x3, Cross
    CHECK(m.confirmed.size() == 1 && m.confirmed[0] == 3);
    CHECK(!p.busy());
    // up again, a slow page (3 VBlanks before it takes a press): still one row per press, no overshoot
    m.lag = 3;
    p.push(target(0x1E89544, 0));
    run(p, m, 200);
    CHECK(m.index == 0 && m.confirmed.size() == 1);
}

TEST(presser_jumps_straight_to_the_hovered_row) {
    MenuPresser p;
    FakeMenu m;
    m.count = 9;
    int jumps = 0;
    auto live = [&](std::uint32_t s, MenuSelectState& st) { if (s != 0x1E89544) return false; st.index = m.index; st.count = m.count; st.idle = m.idle; return true; };
    auto jump = [&](std::uint32_t s, std::uint32_t row) { if (s != 0x1E89544 || row >= m.count) return false; m.index = row; ++jumps; return true; };
    p.push(target(0x1E89544, 7));
    p.push(press(pad::kCross, true));
    std::uint32_t first_press = 0;
    for (int k = 0; k < 20; ++k) { const std::uint32_t mk = p.step(live, jump); if (mk && !first_press) first_press = std::uint32_t(k + 1); m.feed(mk); }
    CHECK(jumps == 1 && m.index == 7);   // one jump, no Down presses
    CHECK(first_press == 1);              // the confirm is held on the very next step: no stepping delay
    CHECK(m.confirmed.size() == 1 && m.confirmed[0] == 7);
    // upwards, and a jump the game refuses falls back to presses
    p.push(target(0x1E89544, 1));
    for (int k = 0; k < 20; ++k) m.feed(p.step(live, jump));
    CHECK(m.index == 1 && jumps == 2);
    p.push(target(0x1E89544, 3));
    for (int k = 0; k < 80; ++k) m.feed(p.step(live, [](std::uint32_t, std::uint32_t) { return false; }));
    CHECK(m.index == 3);
}

TEST(row_text_run_drops_the_lines_above) {
    // a popup's question (two lines) and YES batched into one draw: only YES is the row
    std::vector<ScreenBox> p = {box(0.23, 0.35, 0.70, 0.39), box(0.30, 0.41, 0.60, 0.45), box(0.48, 0.59, 0.53, 0.63)};
    const ScreenBox r = b3replay::last_text_run(p);
    CHECK(!r.empty() && r.y0 > 0.58f && r.y1 < 0.64f && r.x0 > 0.47f);
    // one row's pieces in one line (or a wrapped two-line label) stay whole
    const ScreenBox w = b3replay::last_text_run({box(0.1, 0.30, 0.2, 0.34), box(0.1, 0.35, 0.18, 0.39)});
    CHECK(w.y0 < 0.31f && w.y1 > 0.38f);
    CHECK(b3replay::last_text_run({}).empty());
}

TEST(presser_retargets_waits_and_drops) {
    MenuPresser p;
    FakeMenu m;
    // a new target replaces the old one and the Cross that waited for it
    p.push(target(0x1E89544, 4));
    p.push(press(pad::kCross, true));
    run(p, m, 3);
    p.push(target(0x1E89544, 1));
    run(p, m, 120);
    CHECK(m.index == 1 && m.confirmed.empty());
    // a busy menu (a lead-in) is waited for
    m.idle = false;
    p.push(target(0x1E89544, 2));
    run(p, m, 30);
    CHECK(m.index == 1);
    m.idle = true;
    run(p, m, 60);
    CHECK(m.index == 2);
    // a select that is not on screen: dropped after the timeout, with its Cross; a plain press still goes
    p.push(target(0x1234, 2));
    p.push(press(pad::kCross, true));
    p.push(press(pad::kTriangle));
    run(p, m, MenuPresser::kTargetTimeout + 40);
    CHECK(!p.busy() && m.confirmed.empty());
    // a row past the end is dropped at once
    p.push(target(0x1E89544, 9));
    run(p, m, 4);
    CHECK(!p.busy() && m.index == 2);
    // cancel drops the target; presses queue in order
    p.push(target(0x1E89544, 4));
    p.push(MenuAction{MenuAction::kCancel});
    p.push(press(pad::kUp));
    p.push(press(pad::kUp));
    run(p, m, 60);
    CHECK(m.index == 0);
    // in order (the pause menu run: a wheel turn, then the hover's row, then a click on it): the wheel's Down first,
    // then the steps to the row, then Cross there -- not the wheel's Down after the row was reached
    p.push(MenuAction{MenuAction::kCancel});
    p.push(press(pad::kDown));
    p.push(target(0x1E89544, 4));
    run(p, m, 2);
    p.push(target(0x1E89544, 4));   // the click: the same row again, and Cross after it
    p.push(press(pad::kCross, true));
    run(p, m, 120);
    CHECK(m.index == 4 && m.confirmed.size() == 1 && m.confirmed.back() == 4);
}

TEST(text_mapping) {
    const std::u16string caps = u"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";
    CHECK(b3live::text_char_for('a', caps) == 'A');
    CHECK(b3live::text_char_for('Q', caps) == 'Q');
    CHECK(b3live::text_char_for('7', caps) == '7');
    CHECK(b3live::text_char_for('!', caps) == 0);
    CHECK(b3live::text_char_for(8, caps) == 8);     // Backspace
    CHECK(b3live::text_char_for(10, caps) == 10);   // Enter: Done
    CHECK(b3live::text_char_for(' ', caps) == ' ');
    CHECK(b3live::text_char_for('\t', caps) == 0);
    const std::u16string lower = u"abc";
    CHECK(b3live::text_char_for('B', lower) == 'b');
    CHECK(b3live::text_char_for('x', u"") == 'x');   // not known: as typed (the game checks)
}

TEST(row_bands) {
    // three rows of different widths, 0.05 apart; row 1 not drawn
    std::vector<ScreenBox> rows = {box(0.10f, 0.40f, 0.30f, 0.43f), ScreenBox{}, box(0.10f, 0.50f, 0.40f, 0.54f), box(0.12f, 0.55f, 0.25f, 0.58f)};
    const auto b = b3replay::menu_row_bands(0x100, rows, 0.02f);
    CHECK(b.size() == 3);
    CHECK(b[0].row == 0 && b[1].row == 2 && b[2].row == 3);
    for (const auto& t : b) CHECK_NEAR(t.box.x0, 0.08, 1e-6);
    for (const auto& t : b) CHECK_NEAR(t.box.x1, 0.42, 1e-6);
    CHECK_NEAR(b[0].box.y1, b[1].box.y0, 1e-6);   // split at the midpoint between neighbours
    CHECK_NEAR(b[1].box.y1, 0.5 * (0.52 + 0.565), 1e-6);
    MenuTargets t;
    t.rows = b;
    CHECK(t.row_at(0.35f, 0.415f) && t.row_at(0.35f, 0.415f)->row == 0);   // right of a short label, in its band
    CHECK(t.row_at(0.2f, 0.56f)->row == 3);
    CHECK(t.row_at(0.6f, 0.52f) == nullptr);
    CHECK(b3replay::menu_row_bands(0x100, {ScreenBox{}}, 0.02f).empty());
}

namespace {
void quad(HudDraw& d, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float tw, float th) {
    const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1}, us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
    for (int k : {0, 1, 2, 0, 2, 3}) {
        HudVertex v;
        v.x = xs[k]; v.y = ys[k]; v.u = us[k] / tw; v.v = vs[k] / th;
        d.tri.push_back(v);
    }
}
// pieces as hud_list records them for a 640 x 448 2D on a 4:3 output (fractions = units / size)
std::vector<std::vector<ScreenBox>> pieces_of(const HudFrame& f) {
    std::vector<std::vector<ScreenBox>> p(f.draws.size());
    for (std::size_t i = 0; i < f.draws.size(); ++i)
        for (std::size_t v = 0; v < f.draws[i].tri.size(); ++v) {
            if (p[i].size() <= v / 6) p[i].resize(v / 6 + 1);
            p[i][v / 6].add(f.draws[i].tri[v].x / 640.0f, f.draws[i].tri[v].y / 448.0f);
        }
    return p;
}
}  // namespace

TEST(prompts_and_rows_from_draws) {
    // the prompt bar "(X) SELECT (T) BACK": a ring + Cross symbol, its label glyphs, a ring + Triangle symbol, its glyphs
    HudFrame f;
    f.width = 640; f.height = 448;
    HudDraw ring, syms, text;
    ring.texture = 1; syms.texture = 1; text.texture = 2;
    quad(ring, 400, 400, 421, 421, 1, 33, 22, 54, 128, 64);
    quad(syms, 405, 405, 416, 416, 2, 3, 13, 14, 128, 64);           // Cross
    for (int k = 0; k < 6; ++k) quad(text, 425.0f + 9 * k, 403, 433.0f + 9 * k, 418, 0, 0, 8, 8, 256, 256);   // SELECT
    quad(ring, 500, 400, 521, 421, 1, 33, 22, 54, 128, 64);
    quad(syms, 504, 404, 517, 417, 15, 2, 28, 15, 128, 64);          // Triangle
    for (int k = 0; k < 4; ++k) quad(text, 525.0f + 9 * k, 403, 533.0f + 9 * k, 418, 0, 0, 8, 8, 256, 256);   // BACK
    // two rows of a VSelect (objects 0x2000 / 0x2100) and the bar object
    HudDraw row0, row1;
    row0.texture = 2; row1.texture = 2;
    quad(row0, 80, 150, 200, 170, 0, 0, 8, 8, 256, 256); row0.object = 0;
    quad(row1, 80, 180, 160, 200, 0, 0, 8, 8, 256, 256); row1.object = 1;
    f.objects.push_back(HudObject{}); f.objects.back().addr = 0x2000;
    f.objects.push_back(HudObject{}); f.objects.back().addr = 0x20F8;   // the capture: 8 bytes before the component's pointer
    f.objects.push_back(HudObject{}); f.objects.back().pc = 0x003D7B40;   // the prompt bar
    f.draws = {row0, row1, ring, syms, text};
    const auto pieces = pieces_of(f);
    auto button_of = [&](const HudDraw& d, std::size_t piece) { return d.texture == 1 ? b3replay::prompt_piece_button("Buttons", 128, 64, d.tri, piece) : -1; };
    auto is_text = [](const HudDraw& d) { return d.texture == 2; };
    MenuRecord rec;
    MenuSelect s;
    s.addr = 0x1E89544; s.index = 1; s.count = 2; s.rows[0] = 0x2000; s.rows[1] = 0x2100;
    rec.selects.push_back(s);
    const MenuTargets t = b3replay::menu_targets(f, pieces, &rec, true, button_of, is_text);
    CHECK(t.menu && t.rows.size() == 2 && t.prompts.size() == 2);
    CHECK(t.index_of(0x1E89544) == 1 && t.index_of(1) == ~0u);
    const auto* cross = t.prompt_at(470 / 640.0f, 410 / 448.0f);    // on "SELECT"
    CHECK(cross && cross->button == pad::kCross);
    const auto* tri = t.prompt_at(510 / 640.0f, 410 / 448.0f);      // on the ring
    CHECK(tri && tri->button == pad::kTriangle);
    CHECK(t.prompt_at(490 / 640.0f, 410 / 448.0f) == nullptr);      // between the prompts
    CHECK(t.row_at(190 / 640.0f, 190 / 448.0f) && t.row_at(190 / 640.0f, 190 / 448.0f)->row == 1);
    // the pieces' buttons
    CHECK(b3replay::prompt_piece_button("Buttons", 128, 64, ring.tri, 0) == -2);
    CHECK(b3replay::prompt_piece_button("Buttons", 128, 64, syms.tri, 0) == pad::kCross);
    CHECK(b3replay::prompt_piece_button("Buttons", 128, 64, syms.tri, 1) == pad::kTriangle);
    CHECK(b3replay::prompt_piece_button("Other", 128, 64, syms.tri, 0) == -1);
}

TEST(mouse_events_to_actions) {
    MenuMouse m;   // (a private instance)
    MenuTargets t;
    t.menu = true;
    b3replay::MenuRowTarget r0, r1;
    r0.select = r1.select = 0x1E89544; r0.row = 0; r1.row = 1;
    r0.box = box(0.1f, 0.3f, 0.4f, 0.35f);
    r1.box = box(0.1f, 0.35f, 0.4f, 0.40f);
    t.rows = {r0, r1};
    t.current = {{0x1E89544, 0}};
    b3replay::MenuPromptTarget back;
    back.button = pad::kTriangle;
    back.box = box(0.7f, 0.9f, 0.8f, 0.95f);
    t.prompts = {back};
    std::uint32_t w[3 * 16];
    auto take = [&] { std::vector<MenuAction> v; const int n = m.take(w, 16); for (int i = 0; i < n; ++i) v.push_back(b3live::menu_action_get(w + 3 * i)); return v; };
    // hover the current row: nothing; the next: a target
    m.move(0.2f, 0.32f, 0.0);
    m.frame(t, false, 0.0);
    CHECK(take().empty());
    CHECK(m.cursor_wanted(0.1) && !m.cursor_wanted(MenuMouse::kHideAfter + 0.1));
    m.move(0.2f, 0.37f, 0.1);
    m.frame(t, false, 0.1);
    auto a = take();
    CHECK(a.size() == 1 && a[0].kind == MenuAction::kTarget && a[0].row == 1);
    m.move(0.25f, 0.38f, 0.2);   // moving within the row: no repeat
    m.frame(t, false, 0.2);
    CHECK(take().empty());
    // click on a row: target + Cross after it
    m.button(1, true, 0.3);
    m.button(1, false, 0.35);
    m.frame(t, false, 0.35);
    a = take();
    CHECK(a.size() == 2 && a[0].kind == MenuAction::kTarget && a[1].kind == MenuAction::kPress && a[1].button == pad::kCross && a[1].after_target);
    // click on a prompt: its button
    m.move(0.75f, 0.92f, 0.4);
    m.button(1, true, 0.4);
    m.frame(t, false, 0.4);
    a = take();
    CHECK(a.size() == 1 && a[0].kind == MenuAction::kPress && a[0].button == pad::kTriangle);
    // right click: cancel + Triangle; the wheel: cancel + Up / Down presses
    m.button(3, true, 0.5);
    m.wheel(-2, 0.5);
    m.frame(t, false, 0.5);
    a = take();
    CHECK(a.size() == 5 && a[1].button == pad::kTriangle && a[3].button == pad::kDown && a[4].button == pad::kDown);
    // a key hides the cursor
    m.other_device(0.6);
    CHECK(!m.cursor_wanted(0.6));
    // not a menu (a race): nothing at all
    MenuTargets race;
    m.move(0.5f, 0.5f, 0.7);
    m.button(1, true, 0.7);
    m.button(3, true, 0.7);
    m.wheel(1, 0.7);
    m.frame(race, false, 0.7);
    CHECK(take().empty());
    // a menu page without rows: a left click continues (Cross); not on the name keyboard
    MenuTargets page;
    page.menu = true;
    m.button(1, true, 0.8);
    m.frame(page, false, 0.8);
    a = take();
    CHECK(a.size() == 1 && a[0].button == pad::kCross);
    page.text_entry = true;
    m.button(1, true, 0.9);
    m.frame(page, false, 0.9);
    CHECK(take().empty());
    // the PC SETTINGS page takes the pointer instead
    m.move(0.3f, 0.3f, 1.0);
    m.button(1, true, 1.0);
    m.wheel(1, 1.0);
    m.button(3, true, 1.0);
    m.frame(t, true, 1.0);
    CHECK(take().empty());
    const auto pp = m.page_pointer();
    CHECK(pp.have && pp.moved && pp.click && pp.held && pp.back && pp.wheel == 1);
    const auto pp2 = m.page_pointer();
    CHECK(!pp2.click && !pp2.moved && !pp2.back && pp2.held && pp2.wheel == 0);
}

TEST_MAIN("menu mouse and typed text (mac5)")
