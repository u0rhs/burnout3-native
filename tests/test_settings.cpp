// settings1: the PC settings model, ini, HSET record and the page's menu model (include/b3settings).
#include "test.h"

#include "b3settings/page.h"
#include "b3settings/safemode.h"
#include "b3settings/settings.h"

#include <algorithm>
#include <cstring>

using namespace b3settings;

TEST(defaults_are_main_and_the_high_preset) {
    const Settings d{};
    // main before settings1: MSAA 4, anisotropy 16, interpolation, wide HUD, every effect, radial 0.5, PC post, game LODs
    CHECK(d.msaa == 4 && d.filter == Filter::kAniso16 && d.interpolation && d.hud_wide && !d.vsync && d.frame_cap == 0);
    CHECK(d.motion_blur && d.bloom && d.post_pc && d.radial_blur == 50 && !d.car_detail_highest && d.damage && d.glass_damage);
    CHECK(d.fov == 0 && d.fps_view == 0 && d.fps_corner == 0 && !d.fullscreen);
    CHECK(d.fe_layout == 2 && d.movie_fill && d.latency == 0 && d.lead == -1 && d.frames_in_flight == -1);   // fe3 / lat1 defaults, auto
    CHECK(d.whole_track && d.pause_unfocused);   // mac5: the whole track drawn; the native game holds unfocused
    CHECK(d.skip_hint);   // mac5: the video skip hint shows
    CHECK(matching_preset(d) == Preset::kHigh);
    CHECK(with_preset(d, Preset::kHigh) == d);
    CHECK(!with_preset(d, Preset::kPs2).whole_track && with_preset(d, Preset::kLow).whole_track);   // PS2 streaming: PS2 only
}

TEST(car_reflections_default_enhanced_and_original_in_the_ps2_and_low_presets) {
    const Settings d{};
    CHECK(d.car_reflections == 2);   // reflect1: the one remaster feature on by default
    CHECK(with_preset(d, Preset::kPs2).car_reflections == 1 && with_preset(d, Preset::kLow).car_reflections == 1);
    CHECK(with_preset(d, Preset::kMedium).car_reflections == 2 && with_preset(d, Preset::kUltra).car_reflections == 2);
    CHECK(matching_preset(with_preset(d, Preset::kPs2)) == Preset::kPs2);
    Settings s = d;
    s.car_reflections = 0;
    CHECK(from_ini(to_ini(s)).car_reflections == 0);
    CHECK(from_ini("car_reflections = 1\n").car_reflections == 1);
    CHECK(from_ini("car_reflections = 7\n").car_reflections == 2);   // out of range: the default
    CHECK(matching_preset(s) == Preset::kCustom);
}

TEST(presets_round_trip_and_leave_the_other_rows) {
    Settings s;
    s.fov = 100;
    s.fps_view = 3; s.fps_corner = 3;
    s.frame_cap = 144;
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        const Settings q = with_preset(s, p);
        CHECK(matching_preset(q) == p);
        CHECK(q.fov == 100 && q.fps_view == 3 && q.fps_corner == 3 && q.frame_cap == 144);   // presets touch quality and effects only
    }
    Settings c = with_preset(s, Preset::kUltra);
    c.sparks = kLevelOff;
    CHECK(matching_preset(c) == Preset::kCustom);
    CHECK(with_preset(s, Preset::kPs2).msaa == 1 && with_preset(s, Preset::kPs2).filter == Filter::kBilinear);
}

TEST(ini_round_trip_and_bad_values) {
    Settings s = with_preset(Settings{}, Preset::kMedium);
    s.fov = 105;
    s.radial_blur = 30;
    s.vsync = true;
    s.filter = Filter::kPoint;
    CHECK(from_ini(to_ini(s)) == s);
    std::vector<std::string> w;
    const Settings b = from_ini("msaa = 3\nfov = 200\nbloom = maybe\nnope = 1\nradial_blur = 70\n[x]\n# c\n", &w);
    CHECK(b.msaa == 4 && b.fov == 0 && b.bloom && b.radial_blur == 70);
    CHECK(w.size() == 4);
    CHECK(from_ini("") == Settings{});
}

TEST(hset_record) {
    Settings s;
    s.bloom = false;
    const auto r = hset_encode(s, 7);
    CHECK(r.size() % 16 == 0 && r.size() <= kHsetMax);
    std::uint32_t n = 0;
    const auto serial = hset_serial(r.data(), &n);
    CHECK(serial && *serial == 7);
    CHECK(from_ini(std::string_view(reinterpret_cast<const char*>(r.data() + 16), n)) == s);
    std::uint8_t zero[16] = {};
    CHECK(!hset_serial(zero));
}

TEST(fov_scale) {
    CHECK(fov_tangent_scale(0) == 1.0f && fov_tangent_scale(90) == 1.0f);
    CHECK_NEAR(fov_tangent_scale(120), 1.7320508, 1e-5);
    CHECK(fov_tangent_scale(60) < 1.0f);
}

TEST(every_row_applies) {
    const Content c = build_content(Settings{}, Env{true});
    CHECK(c.tabs.size() == 5);
    int rows = 0;
    for (const Tab& t : c.tabs)
        for (const Row& r : t.rows) {
            if (r.kind == RowKind::kSection || r.disabled) continue;
            if (r.kind == RowKind::kAction) continue;   // mac5: the host's buttons (HD pack folder), not settings values
            if (r.id == kRowLanguage) continue;   // 1.0.1: the host's (language_edit: its values are the imported languages)
            ++rows;
            Settings s;
            const std::size_t n = r.kind == RowKind::kToggle ? 2 : r.kind == RowKind::kSlider ? r.levels : r.values.size();
            CHECK(n >= 2);
            CHECK(r.value < n);
            CHECK(apply_edit(s, Edit{r.id, r.value, 0, false}));
            CHECK(r.id == 1 || s == Settings{});   // the current value changes nothing (the preset row re-applies High)
        }
    CHECK(rows >= 25);
    // no HD row without a pack
    const Content n = build_content(Settings{}, Env{false});
    int hd = 0;
    for (const Tab& t : n.tabs) for (const Row& r : t.rows) hd += r.label == "HD TEXTURES";
    CHECK(hd == 0);
}

TEST(controls_section) {
    // remap1: the CONTROLS section (INTERFACE, last) only for a host that remaps; its rows are the host's
    const Content plain = build_content(Settings{}, Env{true});
    Env e{true};
    e.controls.on = true;
    e.controls.actions = {{"CROSS", "ENTER, Z", "Driving: accelerate."}, {"CIRCLE", "X, H", ""}};
    const Content c = build_content(Settings{}, e);
    CHECK(c.tabs.size() == plain.tabs.size());
    const Tab& t = c.tabs[4];
    CHECK(t.label == "INTERFACE");
    const std::size_t before = plain.tabs[4].rows.size();
    for (std::size_t k = 0; k < before; ++k) CHECK(t.rows[k].id == plain.tabs[4].rows[k].id && t.rows[k].label == plain.tabs[4].rows[k].label);
    // the section, PLAYER, DEVICE, KEYS FOR (the keyboard), the two actions, RESET
    CHECK(t.rows.size() == before + 8);   // (rebind2: and RESET ALL CONTROLS after RESET)
    CHECK(t.rows[before].kind == RowKind::kSection && t.rows[before].label == "CONTROLS");
    CHECK(t.rows[before + 1].id == kRowCtlPlayer && t.rows[before + 2].id == kRowCtlDevice && t.rows[before + 3].id == kRowCtlView);
    CHECK(t.rows[before + 4].id == kActionCtlFirst && t.rows[before + 4].values[0] == "ENTER, Z" && !t.rows[before + 4].disabled);
    CHECK(t.rows[before + 6].id == kActionCtlReset);
    for (std::size_t k = before; k < t.rows.size(); ++k) {
        Settings s;
        CHECK(!apply_edit(s, Edit{t.rows[k].id, t.rows[k].value, 0, t.rows[k].kind == RowKind::kAction}));
    }
    // a controller: no KEYS FOR row; a status row once there is one
    e.controls.device = 1;
    e.controls.status = "PLAYER 1 CROSS: B";
    const Content g = build_content(Settings{}, e);
    CHECK(g.tabs[4].rows.size() == before + 8);
    CHECK(g.tabs[4].rows.back().id == kRowCtlStatus);
}

TEST(controls_shortcuts_and_reset) {
    // rebind2: PLAYER's third value is the shortcuts (no KEYS FOR row, no gyro rows even with a controller), RESET ALL
    // CONTROLS follows RESET; SQUARE (Input::reset) on a control row asks the host for that one back (direction 2),
    // and only there
    Env e{true};
    e.controls.on = true;
    e.controls.player = 2;
    e.controls.actions = {{"INSTANT RESTART", "F5", ""}, {"FULLSCREEN", "F11", ""}};
    const Content c = build_content(Settings{}, e);
    const Tab* t = nullptr;
    for (const Tab& x : c.tabs) if (x.label == "INTERFACE") t = &x;
    CHECK(t != nullptr);
    if (!t) return;
    bool view = false, reset = false, all = false, player3 = false;
    std::size_t first = 0;
    for (std::size_t i = 0; i < t->rows.size(); ++i) {
        const Row& r = t->rows[i];
        if (r.id == kRowCtlView) view = true;
        if (r.id == kActionCtlReset) reset = true;
        if (r.id == kActionCtlResetAll) all = r.kind == RowKind::kAction && !r.disabled;
        if (r.id == kRowCtlPlayer) player3 = r.values.size() == 3 && r.values[2] == "Shortcuts" && r.value == 2;
        if (r.id == kActionCtlFirst) first = i;
    }
    CHECK(!view && reset && all && player3 && first > 0);
    // SQUARE on the first control row
    Menu m;
    m.open(std::size_t(t - c.tabs.data()));
    m.update(c, Input{});
    for (int guard = 0; guard < 600 && m.focus() != first; ++guard) {   // (the hold repeats walk there)
        Input d;
        d.down = true;
        m.update(c, d);
    }
    Input sq;
    sq.reset = true;
    Frame f = m.update(c, sq);
    CHECK(m.focus() == first);
    bool got = false;
    for (const Edit& ed : f.edits) got = got || (ed.id == kActionCtlFirst && ed.direction == 2 && ed.action);
    CHECK(got);
    // held: no second edit
    f = m.update(c, sq);
    CHECK(f.edits.empty());
}

TEST(saves_tab) {
    // save1 (remaster-features §8): the SAVES tab only with a card; its rows are the host's (apply_edit leaves them)
    for (const auto& t : build_content(Settings{}, Env{true}).tabs) CHECK(t.label != "SAVES");
    Env e;
    e.profile = 2;
    e.next_profile = 3;
    e.profile_used = {true, true, false, false, false};
    e.saves_folder = "/saves";
    const Content c = build_content(Settings{}, e);
    CHECK(c.tabs.size() == 6 && c.tabs.back().label == "SAVES");
    const Tab& t = c.tabs.back();
    bool file = false, ex = false, im = false, folder = false;
    for (const Row& r : t.rows) {
        Settings s;
        CHECK(apply_edit(s, Edit{r.id, r.value, 0, r.kind == RowKind::kAction}) == (r.id == kRowSaveScreens));
        if (r.id == kRowSaveFile) file = r.kind == RowKind::kChoice && r.values.size() == 5 && r.value == 2 && r.values[2] == "File 3 (new)" && r.values[1] == "File 2";
        if (r.id == kActionExportSave) ex = r.kind == RowKind::kAction && !r.disabled;
        if (r.id == kActionImportSave) im = r.kind == RowKind::kAction && !r.disabled;
        if (r.id == kActionOpenSaveFolder) folder = r.description == "/saves";
    }
    CHECK(file && ex && im && folder);
    // the last result shows as a row once there is one
    e.save_status = "IMPORT FAILED: no Burnout 3 save";
    bool status = false;
    const Content with_status = build_content(Settings{}, e);
    for (const Row& r : with_status.tabs.back().rows) status = status || (r.label == "LAST RESULT" && r.values[0] == "Failed");
    CHECK(status);
}

TEST(save_screens_row) {
    // autosave1 (tools/native/save_screens.h): SAVE SCREENS is Hide by default, in the file, a live choice on the SAVES tab, and
    // no preset or safe mode touches it
    CHECK(Settings{}.hide_save_screens);
    Settings s;
    s.hide_save_screens = false;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("hide_save_screens = false") != std::string::npos);
    CHECK(!from_ini("hide_save_screens = off\n").hide_save_screens && from_ini("").hide_save_screens);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(!with_preset(s, p).hide_save_screens);
    CHECK(!safe_settings(s).hide_save_screens && safe_settings(Settings{}).hide_save_screens);
    Env e;
    e.profile = 1;
    e.profile_used = {true, false, false, false, false};
    bool found = false;
    for (const auto& t : build_content(Settings{}, e).tabs)
        for (const auto& r : t.rows)
            if (r.label == "SAVE SCREENS") {
                found = t.label == "SAVES" && r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "Hide" && r.value == 0;
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && !x.hide_save_screens);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.hide_save_screens);
            }
    CHECK(found);
}

TEST(dither_row) {
    // perf1 (docs/native-renderer.md §33.8): DITHER is off by default, in the file, on the GRAPHICS tab, and no preset sets it
    CHECK(!Settings{}.dither);
    Settings s;
    s.dither = true;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("dither = true") != std::string::npos || to_ini(s).find("dither = 1") != std::string::npos);
    CHECK(with_preset(s, Preset::kPs2).dither && with_preset(s, Preset::kUltra).dither);
    CHECK(matching_preset(s) == Preset::kHigh);
    bool found = false;
    for (const auto& t : build_content(Settings{}, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "DITHER") {
                found = t.label == "GRAPHICS";
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.dither);
            }
    CHECK(found);
}

TEST(bloom_level_row) {
    // bloom2: BLOOM is Off / Soft / Standard (row 260), Soft by default; the PS2 preset is Standard (the game's own glow)
    const Settings d{};
    CHECK(d.bloom && d.bloom_level == 1);
    CHECK(with_preset(d, Preset::kPs2).bloom_level == 2 && with_preset(d, Preset::kHigh).bloom_level == 1);
    CHECK(matching_preset(with_preset(d, Preset::kPs2)) == Preset::kPs2);
    CHECK(from_ini("bloom_level = 2\n").bloom_level == 2 && from_ini("bloom_level = 9\n").bloom_level == 1);
    Settings s = d;
    s.bloom_level = 2;
    CHECK(from_ini(to_ini(s)) == s);
    bool found = false;
    for (const auto& t : build_content(d, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "BLOOM") {
                found = t.label == "EFFECTS" && r.id == 260 && r.value == 1 && r.values.size() == 3;
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 2, 0, false}) && x.bloom && x.bloom_level == 2);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && !x.bloom && x.bloom_level == 2);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.bloom && x.bloom_level == 1);
                Settings off = d;
                off.bloom = false;
                for (const auto& t2 : build_content(off, Env{true}).tabs)
                    for (const auto& r2 : t2.rows)
                        if (r2.id == 260) CHECK(r2.value == 0);
            }
    CHECK(found);
}

TEST(skip_hint_row) {
    // mac5 (remaster-features §2): VIDEO SKIP HINT is on by default, in the file, a live toggle on the INTERFACE tab, and
    // no preset touches it
    CHECK(Settings{}.skip_hint);
    Settings s;
    s.skip_hint = false;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("skip_hint = false") != std::string::npos);
    CHECK(!from_ini("skip_hint = off\n").skip_hint && from_ini("").skip_hint);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(!with_preset(s, p).skip_hint);
    bool found = false;
    for (const auto& t : build_content(Settings{}, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "VIDEO SKIP HINT") {
                found = t.label == "INTERFACE" && r.kind == RowKind::kToggle && !r.disabled && r.value == 1;
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && !x.skip_hint);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.skip_hint);
            }
    CHECK(found);
}

TEST(fast_loading_row) {
    // fastload1 (tools/native/fast_load.h): FAST LOADING is on by default, in the file, a live toggle on the DISPLAY tab
    // (WINDOW section, beside PAUSE WHEN UNFOCUSED), and no preset or safe mode touches it
    CHECK(Settings{}.fast_loading);
    Settings s;
    s.fast_loading = false;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("fast_loading = false") != std::string::npos);
    CHECK(!from_ini("fast_loading = off\n").fast_loading && from_ini("").fast_loading);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(!with_preset(s, p).fast_loading);
    CHECK(!safe_settings(s).fast_loading && safe_settings(Settings{}).fast_loading);
    bool found = false;
    for (const auto& t : build_content(Settings{}, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "FAST LOADING") {
                found = t.label == "DISPLAY" && r.kind == RowKind::kToggle && !r.disabled && r.value == 1;
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && !x.fast_loading);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.fast_loading);
            }
    CHECK(found);
}

TEST(button_prompts_row) {
    // mac5 (remaster-features §3): BUTTON PROMPTS is Auto by default, in the file (0-5), a live choice on the INTERFACE
    // tab, and no preset touches it
    CHECK(Settings{}.button_prompts == 0);
    Settings s;
    s.button_prompts = 2;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("button_prompts = 2") != std::string::npos);
    CHECK(from_ini("button_prompts = 9\n").button_prompts == 0 && from_ini("button_prompts = 5\n").button_prompts == 5);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).button_prompts == 2);
    bool found = false;
    for (const auto& t : build_content(Settings{}, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "BUTTON PROMPTS") {
                found = t.label == "INTERFACE" && r.kind == RowKind::kChoice && !r.disabled && r.value == 0 && r.values.size() == 7 &&
                        r.values[1] == "PlayStation" && r.values[2] == "PS4 / PS5" && r.values[3] == "Xbox" && r.values[6] == "Keyboard";
                // the page lists PS4 / PS5 beside PlayStation; the ini keeps its values (PS4 / PS5 = 6, Xbox = 2)
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 3, 0, false}) && x.button_prompts == 2);
                CHECK(apply_edit(x, Edit{r.id, 2, 0, false}) && x.button_prompts == 6);
            }
    CHECK(found);
    Settings m;
    m.button_prompts = 6;
    CHECK(from_ini(to_ini(m)).button_prompts == 6);
    for (const auto& t : build_content(m, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "BUTTON PROMPTS") CHECK(r.value == 2);
}

TEST(menu_edits_tabs_and_close) {
    Settings s;
    Menu m;
    m.open();
    Input idle;
    const Content c0 = build_content(s, Env{});
    CHECK(m.update(c0, idle).edits.empty());
    // the opening press is ignored until released: confirm held at open does nothing
    Input held;
    held.confirm = true;
    CHECK(m.update(c0, held).edits.empty());
    // right on the preset row (focused first): High -> Ultra
    Input right;
    right.right = true;
    Frame f = m.update(c0, right);
    CHECK(f.edits.size() == 1);
    for (const Edit& e : f.edits) apply_edit(s, e);
    CHECK(matching_preset(s) == Preset::kUltra);
    // held: the next step comes after the repeat delay, in the caller's time
    CHECK(m.update(build_content(s, Env{}), right).edits.empty());
    Input r2 = right;
    r2.seconds = 0.4f;
    CHECK(m.update(build_content(s, Env{}), r2).edits.size() == 1);   // Ultra -> Custom
    m.update(build_content(s, Env{}), idle);
    // next tab: GRAPHICS, focus on its first selectable row
    Input next;
    next.next_tab = true;
    m.update(build_content(s, Env{}), next);
    CHECK(m.tab() == 1);
    m.update(build_content(s, Env{}), idle);
    Input back;
    back.back = true;
    CHECK(m.update(build_content(s, Env{}), back).close);
}

// online-det follow-up: an offscreen shot sets the animation clock from the shot's poll, so the page it draws must
// not depend on how the host's frames split the time before it (the focused row's pulse)
TEST(menu_set_clock_makes_the_picture_independent_of_frame_timing) {
    const Content c = build_content(Settings{}, Env{});
    Menu a, b;
    a.open(1);
    b.open(1);
    Input in;
    in.seconds = 1.0f;
    a.update(c, in);
    in.seconds = 0.37f;
    b.update(c, in);
    in.seconds = 0.91f;
    b.update(c, in);
    auto same = [](const std::vector<Item>& x, const std::vector<Item>& y) {
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (x[i].kind != y[i].kind || x[i].flags != y[i].flags || x[i].x != y[i].x || x[i].y != y[i].y ||
                x[i].w != y[i].w || x[i].h != y[i].h || x[i].colour != y[i].colour || x[i].text != y[i].text)
                return false;
        return true;
    };
    CHECK(!same(a.items("f", "a", nullptr), b.items("f", "a", nullptr)));   // the pulse differs at 1.00 s and 1.28 s
    a.set_clock(10400.0f / 60.0f);
    b.set_clock(10400.0f / 60.0f);
    CHECK(same(a.items("f", "a", nullptr), b.items("f", "a", nullptr)));
}

TEST(menu_items_inside_the_picture) {
    Menu m;
    m.open(2);
    Input in;
    in.seconds = 1.0f;
    m.update(build_content(Settings{}, Env{true}), in);
    const auto items = m.items("fonts", "art", nullptr);
    CHECK(items.size() > 50);
    CHECK(items.front().kind == ItemKind::kFontDirectory);
    bool title = false;
    for (const Item& i : items) {
        if (i.kind == ItemKind::kFontDirectory) continue;
        CHECK(i.x >= -1 && i.y >= -1 && i.x + i.w <= 641 && i.y + i.h <= 449);
        if (i.kind == ItemKind::kText && i.text == "SETTINGS") title = true;
        if (i.kind == ItemKind::kImage) CHECK(i.text.rfind("art/", 0) == 0);
    }
    CHECK(title);
}

// ---- the menu model's rules (docs/specs/settings-page.md §2), positions found through items()

namespace {

Row mrow(std::uint32_t id, RowKind kind, std::string label, std::size_t n = 0, std::size_t value = 0, bool disabled = false) {
    Row r;
    r.id = id;
    r.kind = kind;
    r.label = std::move(label);
    if (kind == RowKind::kToggle) r.values = {"Off", "On"};
    if (kind == RowKind::kChoice)
        for (std::size_t k = 0; k < n; ++k) r.values.push_back("V" + std::to_string(k));
    if (kind == RowKind::kSlider) {
        r.levels = n;
        r.level_text = std::to_string(value);
    }
    if (kind == RowKind::kAction) r.values = {"Go"};
    r.value = value;
    r.disabled = disabled;
    r.description = r.label + " description";
    return r;
}
// Tab A: a section, a toggle (off), a choice (3 values, at 1), a slider (5 levels, at 2), an action, a disabled choice;
// tab B: 24 rows, a section before every 6th; tab C: one toggle.
Content mcontent() {
    Content c;
    Tab a{"TAB-A", {}};
    a.rows.push_back(mrow(900, RowKind::kSection, "SECTION A"));
    a.rows.push_back(mrow(1, RowKind::kToggle, "TOGGLE"));
    a.rows.push_back(mrow(2, RowKind::kChoice, "CHOICE", 3, 1));
    a.rows.push_back(mrow(3, RowKind::kSlider, "SLIDER", 5, 2));
    a.rows.push_back(mrow(4, RowKind::kAction, "ACTION"));
    a.rows.push_back(mrow(5, RowKind::kChoice, "DISABLED", 3, 1, true));
    c.tabs.push_back(a);
    Tab b{"TAB-B", {}};
    for (std::uint32_t k = 0; k < 24; ++k)
        b.rows.push_back(k % 6 == 0 ? mrow(1000 + k, RowKind::kSection, "HEAD " + std::to_string(k))
                                    : mrow(100 + k, RowKind::kToggle, "ROW " + std::to_string(k)));
    c.tabs.push_back(b);
    Tab t{"TAB-C", {}};
    t.rows.push_back(mrow(50, RowKind::kToggle, "ONLY"));
    c.tabs.push_back(t);
    return c;
}
const Row& focused(const Menu& m, const Content& c) { return c.tabs[m.tab()].rows[m.focus()]; }
Input pressed(bool Input::*b, float seconds = 1.0f / 60.0f) {
    Input in;
    in.*b = true;
    in.seconds = seconds;
    return in;
}
// a fresh menu on mcontent(), past the opening press, focus on `id` of tab `tab`
Menu mopen(const Content& c, std::size_t tab = 0) {
    Menu m;
    m.open(tab);
    m.update(c, Input{});
    return m;
}
const Item* text_item(const std::vector<Item>& items, const std::string& text) {
    for (const Item& i : items)
        if (i.kind == ItemKind::kText && i.text == text) return &i;
    return nullptr;
}
std::vector<std::string> shown_rows(const Menu& m, const Content& c) {
    std::vector<std::string> o;
    const auto items = m.items("", "", nullptr);
    for (const Row& r : c.tabs[m.tab()].rows)
        if (text_item(items, r.label)) o.push_back(r.label);
    return o;
}
// The arrow images on the row whose label is `label` (left, right)
std::pair<const Item*, const Item*> arrows(const std::vector<Item>& items, const std::string& label) {
    const Item* l = text_item(items, label);
    const Item* a = nullptr;
    const Item* b = nullptr;
    for (const Item& i : items)
        if (l && i.kind == ItemKind::kImage && i.y < l->y + l->h && i.y + i.h > l->y) {
            if (i.text.find("left") != std::string::npos) a = &i;
            if (i.text.find("right") != std::string::npos) b = &i;
        }
    return {a, b};
}
Input click_at(float x, float y) {
    Input in;
    in.pointer = std::array<float, 2>{x, y};
    in.pointer_moved = true;
    in.click = in.click_held = true;
    return in;
}

}  // namespace

TEST(menu_pointer) {
    // a click on the centre of the third tab's label (EFFECTS) selects it
    Menu m;
    m.open();
    const Content c = build_content(Settings{}, Env{});
    m.update(c, Input{});
    const auto items = m.items("", "", nullptr);
    const Item* t = text_item(items, "EFFECTS");
    CHECK(t != nullptr);
    if (!t) return;
    m.update(c, click_at(t->x + t->w * 0.5f, t->y + t->h * 0.5f));
    CHECK(m.tab() == 2);
}

TEST(menu_opening_press_and_edges) {
    const Content c = mcontent();
    Menu m;
    m.open();
    Input held = pressed(&Input::confirm);
    held.back = true;
    CHECK(m.update(c, held).edits.empty());   // the press that opened the page: not an edge
    CHECK(!m.update(c, held).close);
    CHECK(focused(m, c).id == 1);              // the first selectable row (the section skipped)
    CHECK(m.update(c, Input{}).edits.empty()); // released
    Frame f = m.update(c, pressed(&Input::confirm));
    CHECK(f.edits.size() == 1 && f.edits[0].id == 1 && f.edits[0].value == 1 && !f.edits[0].action);   // the toggle flips
    CHECK(m.update(c, pressed(&Input::confirm)).edits.empty());   // held: an edge only
    // a direction held as the page opens acts only after its release
    Menu d;
    d.open();
    CHECK(d.update(c, pressed(&Input::down)).edits.empty() && focused(d, c).id == 1);
    d.update(c, Input{});
    d.update(c, pressed(&Input::down));
    CHECK(focused(d, c).id == 2);
    // back closes, and nothing else happens in that poll
    Input bd = pressed(&Input::back);
    bd.down = true;
    d.update(c, Input{});
    Frame b = d.update(c, bd);
    CHECK(b.close && b.edits.empty() && focused(d, c).id == 2);
}

TEST(menu_hold_repeat) {
    const Content c = mcontent();
    Menu m = mopen(c);
    m.update(c, pressed(&Input::down));
    m.update(c, Input{});
    m.update(c, pressed(&Input::down));
    m.update(c, Input{});
    CHECK(focused(m, c).id == 3);   // the slider at 2 of 0..4
    // right: at once, then nothing before 0.35 s held, then one step every 0.07 s (the content stays at 2: the caller
    // applies no edit here, so each step is 2 -> 3)
    CHECK(m.update(c, pressed(&Input::right)).edits.size() == 1);
    CHECK(m.update(c, pressed(&Input::right, 0.30f)).edits.empty());
    CHECK(m.update(c, pressed(&Input::right, 0.04f)).edits.empty());   // 0.34 s held
    Frame f = m.update(c, pressed(&Input::right, 0.02f));             // 0.36
    CHECK(f.edits.size() == 1 && f.edits[0].direction == 1 && f.edits[0].value == 3);
    CHECK(m.update(c, pressed(&Input::right, 0.04f)).edits.empty());   // 0.40 (the next step at 0.42)
    CHECK(m.update(c, pressed(&Input::right, 0.03f)).edits.size() == 1);   // 0.43
    CHECK(m.update(c, pressed(&Input::right, 0.03f)).edits.empty());       // 0.46 (next at 0.49)
    CHECK(m.update(c, pressed(&Input::right, 0.04f)).edits.size() == 1);   // 0.50
    // a long poll is still one step at most
    Menu s = mopen(c);
    for (int k = 0; k < 2; ++k) { s.update(c, pressed(&Input::down)); s.update(c, Input{}); }
    CHECK(s.update(c, pressed(&Input::left)).edits.size() == 1);         // 2 -> 1
    Frame one = s.update(c, pressed(&Input::left, 1.0f));               // a whole second in one poll: one step
    CHECK(one.edits.size() == 1 && one.edits[0].value == 1);
    CHECK(s.update(c, pressed(&Input::left, 1.0f)).edits.size() == 1);
    Content zero = c;                                                    // at 0: nothing at the end (no wrap)
    zero.tabs[0].rows[3].value = 0;
    CHECK(s.update(zero, pressed(&Input::left, 1.0f)).edits.empty());
    // left and right together: neither
    Menu lr = mopen(c);
    Input both = pressed(&Input::left);
    both.right = true;
    for (int k = 0; k < 2; ++k) { lr.update(c, pressed(&Input::down)); lr.update(c, Input{}); }
    CHECK(lr.update(c, both).edits.empty());
    CHECK(lr.update(c, both).edits.empty());
}

// devnav1: PageUp / PageDown move by sections (a long tab: DEVELOPER); up / down held go faster after 1.5 s and 3 s
TEST(menu_sections_and_long_holds) {
    const Content c = mcontent();
    Menu m = mopen(c, 1);   // TAB-B: headings at 0, 6, 12, 18
    CHECK(focused(m, c).id == 101);
    auto tap = [&](bool Input::*b) {
        m.update(c, pressed(b));
        m.update(c, Input{});
        return focused(m, c).id;
    };
    CHECK(tap(&Input::next_section) == 107);
    CHECK(tap(&Input::next_section) == 113);
    CHECK(tap(&Input::down) == 114);
    CHECK(tap(&Input::previous_section) == 113);   // the section's first row
    CHECK(tap(&Input::previous_section) == 107);   // then the one before
    CHECK(tap(&Input::next_section) == 113);
    CHECK(tap(&Input::next_section) == 119);
    CHECK(tap(&Input::next_section) == 123);        // no section after: the last row
    CHECK(tap(&Input::next_section) == 123);
    CHECK(tap(&Input::previous_section) == 119);
    for (int k = 0; k < 4; ++k) tap(&Input::previous_section);
    CHECK(focused(m, c).id == 101);                 // the first row at most
    // a tab without sections: a window of rows
    Content flat;
    Tab t{"FLAT", {}};
    for (std::uint32_t k = 0; k < 40; ++k) t.rows.push_back(mrow(200 + k, RowKind::kToggle, "R" + std::to_string(k)));
    flat.tabs.push_back(t);
    Menu f = mopen(flat);
    f.update(flat, pressed(&Input::next_section));
    CHECK(focused(f, flat).id == 200 + 10);   // the window: 10 rows (menu.cpp kVisible)
    // down held 4 s at 60 polls a second: 1 + (1.5 - 0.35) / 0.07 steps by 1.5 s, then every 0.035 s, then every poll
    Content tall;
    Tab l{"TALL", {}};
    for (std::uint32_t k = 0; k < 1000; ++k) l.rows.push_back(mrow(k + 1, RowKind::kToggle, "R"));
    tall.tabs.push_back(l);
    Menu h = mopen(tall);
    int steps_at_1_5 = 0, steps_at_3 = 0;
    for (int p = 0; p < 240; ++p) {
        h.update(tall, pressed(&Input::down));
        const int at = int(h.focus());
        if (p == 89) steps_at_1_5 = at;
        if (p == 179) steps_at_3 = at;
    }
    const int end = int(h.focus());
    CHECK(steps_at_1_5 >= 16 && steps_at_1_5 <= 18);           // the normal rate first
    CHECK(steps_at_3 - steps_at_1_5 >= 40 && steps_at_3 - steps_at_1_5 <= 46);   // x2
    CHECK(end - steps_at_3 >= 59 && end - steps_at_3 <= 60);   // every poll
    // left / right (values) never speed up
    Menu v = mopen(c);
    for (int k = 0; k < 2; ++k) { v.update(c, pressed(&Input::down)); v.update(c, Input{}); }
    int edits = 0;
    for (int p = 0; p < 240; ++p) edits += int(v.update(c, pressed(&Input::right)).edits.size());
    CHECK(edits >= 52 && edits <= 54);   // 1 + (4 - 0.35) / 0.07
}

TEST(menu_popup) {
    Content c = mcontent();
    Content p = c;
    p.popup = "WORKING...";
    Menu m = mopen(c);
    CHECK(m.update(p, pressed(&Input::confirm)).edits.empty());   // nothing acts while the popup shows
    CHECK(!m.update(p, pressed(&Input::back)).close);
    // the popup is drawn over the page
    CHECK(text_item(m.items("", "", nullptr), "WORKING...") != nullptr);
    // a press held across the popup does not act when it closes
    Input r = pressed(&Input::down);
    m.update(p, r);
    CHECK(m.update(c, r).edits.empty() && focused(m, c).id == 1);
    Input cb = pressed(&Input::confirm);
    m.update(p, cb);
    CHECK(m.update(c, cb).edits.empty());
    m.update(c, Input{});
    CHECK(m.update(c, pressed(&Input::confirm)).edits.size() == 1);   // a new press does
}

TEST(menu_focus_tabs_and_ends) {
    const Content c = mcontent();
    Menu m = mopen(c);
    m.update(c, pressed(&Input::up));   // already the first selectable row: no wrap
    m.update(c, Input{});
    CHECK(focused(m, c).id == 1);
    for (int k = 0; k < 8; ++k) { m.update(c, pressed(&Input::down)); m.update(c, Input{}); }
    CHECK(focused(m, c).id == 5);       // the disabled row is selectable, and the last: no wrap
    // disabled: nothing changes it
    CHECK(m.update(c, pressed(&Input::right)).edits.empty());
    m.update(c, Input{});
    CHECK(m.update(c, pressed(&Input::confirm)).edits.empty());
    m.update(c, Input{});
    // tabs: no wrap at either end; a tab move does nothing else in that poll
    m.update(c, pressed(&Input::previous_tab));
    m.update(c, Input{});
    CHECK(m.tab() == 0);
    Input nt = pressed(&Input::next_tab);
    nt.confirm = true;
    CHECK(m.update(c, nt).edits.empty() && m.tab() == 1);
    CHECK(focused(m, c).id == 101);     // the first selectable row of a tab not visited
    m.update(c, Input{});
    m.update(c, pressed(&Input::down));
    m.update(c, Input{});
    CHECK(focused(m, c).id == 102);
    m.update(c, pressed(&Input::next_tab));
    m.update(c, Input{});
    m.update(c, pressed(&Input::next_tab));   // the last tab: stays
    m.update(c, Input{});
    CHECK(m.tab() == 2);
    // each tab remembers its row while the page is open
    m.update(c, pressed(&Input::previous_tab));
    m.update(c, Input{});
    CHECK(m.tab() == 1 && focused(m, c).id == 102);
    m.update(c, pressed(&Input::previous_tab));
    m.update(c, Input{});
    CHECK(m.tab() == 0 && focused(m, c).id == 5);
    // open forgets
    m.open(1);
    m.update(c, Input{});
    CHECK(m.tab() == 1 && focused(m, c).id == 101);
    // a tab index past the content resets to tab 0
    Menu z;
    z.open(9);
    z.update(c, Input{});
    CHECK(z.tab() == 0);
}

TEST(menu_actions_and_values) {
    const Content c = mcontent();
    Menu m = mopen(c);
    for (int k = 0; k < 3; ++k) { m.update(c, pressed(&Input::down)); m.update(c, Input{}); }
    CHECK(focused(m, c).id == 4);
    Frame f = m.update(c, pressed(&Input::left));
    CHECK(f.edits.size() == 1 && f.edits[0].action && f.edits[0].direction == -1 && f.edits[0].id == 4);
    m.update(c, Input{});
    f = m.update(c, pressed(&Input::confirm));
    CHECK(f.edits.size() == 1 && f.edits[0].action && f.edits[0].direction == 0);
    // the toggle: left at Off does nothing, right turns it on, right again (On) nothing
    Menu t = mopen(c);
    CHECK(t.update(c, pressed(&Input::left)).edits.empty());
    t.update(c, Input{});
    f = t.update(c, pressed(&Input::right));
    CHECK(f.edits.size() == 1 && f.edits[0].value == 1);
    Content on = c;
    on.tabs[0].rows[1].value = 1;
    t.update(on, Input{});
    CHECK(t.update(on, pressed(&Input::right)).edits.empty());
    // the choice: steps within 0..2, no wrap
    Menu ch = mopen(c);
    ch.update(c, pressed(&Input::down));
    ch.update(c, Input{});
    f = ch.update(c, pressed(&Input::right));
    CHECK(f.edits.size() == 1 && f.edits[0].id == 2 && f.edits[0].value == 2);
    Content at2 = c;
    at2.tabs[0].rows[2].value = 2;
    ch.update(at2, Input{});
    CHECK(ch.update(at2, pressed(&Input::right)).edits.empty());
}

TEST(menu_focus_kept_by_row_id) {
    Content c = mcontent();
    Menu m = mopen(c);
    for (int k = 0; k < 2; ++k) { m.update(c, pressed(&Input::down)); m.update(c, Input{}); }
    CHECK(focused(m, c).id == 3);
    Content more = c;   // a row appears above it: the focus stays on the slider
    more.tabs[0].rows.insert(more.tabs[0].rows.begin() + 1, mrow(7, RowKind::kToggle, "NEW"));
    m.update(more, Input{});
    CHECK(m.focus() == 4 && focused(m, more).id == 3);
    Content less = c;   // the focused row disappears: the first selectable row
    less.tabs[0].rows.erase(less.tabs[0].rows.begin() + 3);
    m.update(less, Input{});
    CHECK(focused(m, less).id == 1);
}

TEST(menu_scroll_window) {
    const Content c = mcontent();
    Menu m = mopen(c, 1);
    std::vector<std::string> s = shown_rows(m, c);
    CHECK(!s.empty() && s.front() == "HEAD 0" && s.size() < 24);
    const std::size_t window = s.size();
    const auto has = [](const std::vector<Item>& items, const char* name) {
        for (const Item& i : items)
            if (i.kind == ItemKind::kImage && i.text.find(name) != std::string::npos) return true;
        return false;
    };
    CHECK(has(m.items("", "", nullptr), "arrow-down") && !has(m.items("", "", nullptr), "arrow-up"));
    // down to the last row: the focus always in the window, which ends at the last row
    for (int k = 0; k < 30; ++k) {
        m.update(c, pressed(&Input::down));
        m.update(c, Input{});
        const std::vector<std::string> v = shown_rows(m, c);
        CHECK(std::find(v.begin(), v.end(), focused(m, c).label) != v.end());
    }
    CHECK(focused(m, c).id == 123);
    s = shown_rows(m, c);
    CHECK(s.size() == window && s.back() == "ROW 23");
    CHECK(has(m.items("", "", nullptr), "arrow-up") && !has(m.items("", "", nullptr), "arrow-down"));
    // up to the top: entering a row just below its section header shows the header too
    for (int k = 0; k < 30; ++k) {
        m.update(c, pressed(&Input::up));
        m.update(c, Input{});
        const Row& r = focused(m, c);
        const std::vector<std::string> v = shown_rows(m, c);
        CHECK(std::find(v.begin(), v.end(), r.label) != v.end());
        if (r.id == 113) CHECK(v.front() == "HEAD 12");
    }
    CHECK(focused(m, c).id == 101 && shown_rows(m, c).front() == "HEAD 0");
    // the wheel: down one step scrolls one row, clamped at the end; the focus follows into the window
    Input w;
    w.pointer = std::array<float, 2>{320.0f, 200.0f};
    w.wheel = -1;
    m.update(c, w);
    CHECK(shown_rows(m, c).front() == "ROW 1");
    w.wheel = -100;
    m.update(c, w);
    s = shown_rows(m, c);
    CHECK(s.back() == "ROW 23" && std::find(s.begin(), s.end(), focused(m, c).label) != s.end());
    w.wheel = 100;
    m.update(c, w);
    CHECK(shown_rows(m, c).front() == "HEAD 0");
    // a click on the down arrow scrolls one row
    const auto items = m.items("", "", nullptr);
    for (const Item& i : items)
        if (i.kind == ItemKind::kImage && i.text.find("arrow-down") != std::string::npos) m.update(c, click_at(i.x + i.w * 0.5f, i.y + i.h * 0.5f));
    CHECK(shown_rows(m, c).front() == "ROW 1");
}

TEST(menu_pointer_rows_and_controls) {
    const Content c = mcontent();
    Menu m = mopen(c);
    auto items = m.items("", "", nullptr);
    // hovering a row focuses it; a section row ignores the pointer
    const Item* choice = text_item(items, "CHOICE");
    const Item* section = text_item(items, "SECTION A");
    CHECK(choice && section);
    if (!choice || !section) return;
    Input hover;
    hover.pointer = std::array<float, 2>{choice->x + 4, choice->y + choice->h * 0.5f};
    hover.pointer_moved = true;
    CHECK(m.update(c, hover).edits.empty() && focused(m, c).id == 2);
    hover.pointer = std::array<float, 2>{section->x + 4, section->y + section->h * 0.5f};
    m.update(c, hover);
    CHECK(focused(m, c).id == 2);
    // the choice's arrows step it
    auto [l, r] = arrows(items, "CHOICE");
    CHECK(l && r);
    if (!l || !r) return;
    Frame f = m.update(c, click_at(r->x + r->w * 0.5f, r->y + r->h * 0.5f));
    CHECK(f.edits.size() == 1 && f.edits[0].id == 2 && f.edits[0].value == 2);
    m.update(c, Input{});
    f = m.update(c, click_at(l->x + l->w * 0.5f, l->y + l->h * 0.5f));
    CHECK(f.edits.size() == 1 && f.edits[0].value == 0);
    // the toggle's control flips it (a click on another row focuses that row first)
    const Item* toggle = text_item(items, "TOGGLE");
    CHECK(toggle != nullptr);
    m.update(c, Input{});
    f = m.update(c, click_at(r->x - 4, toggle->y + toggle->h * 0.5f));
    CHECK(focused(m, c).id == 1 && f.edits.size() == 1 && f.edits[0].id == 1 && f.edits[0].value == 1);
    // the disabled row: a click focuses it and changes nothing
    const Item* dis = text_item(items, "DISABLED");
    auto [dl, dr] = arrows(items, "DISABLED");
    CHECK(dis && dr);
    if (dr) {
        m.update(c, Input{});
        f = m.update(c, click_at(dr->x + dr->w * 0.5f, dr->y + dr->h * 0.5f));
        CHECK(f.edits.empty() && focused(m, c).id == 5);
    }
    // the action's button activates it
    const Item* act = text_item(items, "Go");
    CHECK(act != nullptr);
    if (act) {
        m.update(c, Input{});
        f = m.update(c, click_at(act->x + act->w * 0.5f, act->y + act->h * 0.5f));
        CHECK(f.edits.size() == 1 && f.edits[0].id == 4 && f.edits[0].action);
    }
}

TEST(menu_slider_drag) {
    const Content c = mcontent();
    Menu m = mopen(c);
    const auto items = m.items("", "", nullptr);
    auto [l, r] = arrows(items, "SLIDER");
    CHECK(l && r);
    if (!l || !r) return;
    // the bar lies between the arrows: a click at its left end sets level 0 and starts a drag
    const float y = l->y + l->h * 0.5f;
    const float x0 = l->x + l->w + 12, x1 = r->x - 12;
    Frame f = m.update(c, click_at(x0, y));
    CHECK(f.edits.size() == 1 && f.edits[0].id == 3 && f.edits[0].value == 0);
    // held and moved: the nearest level under the pointer, even off the row
    Input drag;
    drag.pointer = std::array<float, 2>{x1 + 40, y + 60};
    drag.pointer_moved = true;
    drag.click_held = true;
    f = m.update(c, drag);
    CHECK(f.edits.size() == 1 && f.edits[0].value == 4 && focused(m, c).id == 3);
    drag.pointer = std::array<float, 2>{x0 + 0.25f * (x1 - x0), y + 60};   // (the content stays at 2)
    f = m.update(c, drag);
    CHECK(f.edits.size() == 1 && f.edits[0].value == 1);
    // released: the drag ends (moving no longer sets it; it hovers instead)
    Input up;
    up.pointer = std::array<float, 2>{x1, y};
    up.pointer_moved = true;
    CHECK(m.update(c, up).edits.empty());
    drag.pointer = std::array<float, 2>{x0, y};
    CHECK(m.update(c, drag).edits.empty());
    // no pointer this poll ends a drag too
    m.update(c, click_at(x1, y));
    Input none;
    none.click_held = true;
    m.update(c, none);
    drag.pointer = std::array<float, 2>{x0, y};
    CHECK(m.update(c, drag).edits.empty());
}

TEST(menu_animation_is_a_function_of_the_polls) {
    // the same polls give the same items, and once settled every item stays in the picture
    const Content c = build_content(Settings{}, Env{true});
    Menu a, b;
    a.open(1);
    b.open(1);
    for (int k = 0; k < 20; ++k) {
        Input in;
        in.seconds = 0.013f * float(k % 3 + 1);
        in.next_tab = k == 10;
        a.update(c, in);
        b.update(c, in);
        const auto ia = a.items("", "", nullptr), ib = b.items("", "", nullptr);
        CHECK(ia.size() == ib.size());
        for (std::size_t i = 0; i < std::min(ia.size(), ib.size()); ++i)
            CHECK(ia[i].x == ib[i].x && ia[i].y == ib[i].y && ia[i].colour == ib[i].colour && ia[i].text == ib[i].text);
        for (const Item& i : ia)
            if (i.kind != ItemKind::kFontDirectory) CHECK(i.x >= -1 && i.y >= -1 && i.x + i.w <= 641 && i.y + i.h <= 449);
    }
}

TEST(safe_mode_after_two_unclean_runs) {
    // mac5 (remaster-features §8): a run that never reached its clean end counts; two in a row start in safe mode
    RunState st;                                   // first start ever
    StartDecision d = start_run(st, false);
    CHECK(d.now.running && d.now.unclean == 0 && !d.safe);
    d = start_run(d.now, false);                   // the run before crashed
    CHECK(d.now.unclean == 1 && !d.safe);
    d = start_run(d.now, false);                   // and the next one too
    CHECK(d.now.unclean == 2 && d.safe && !d.forced);
    d = start_run(clean_end(), false);             // a clean end clears the count
    CHECK(d.now.unclean == 0 && !d.safe);
    d = start_run(clean_end(), true);              // --safe-mode
    CHECK(d.safe && d.forced && d.now.unclean == 0);
    // the file round trip; junk keeps the defaults
    RunState r; r.running = true; r.unclean = 3;
    CHECK(run_state_from_ini(run_state_to_ini(r)) == r);
    CHECK(run_state_from_ini("running = 7\nunclean = x\n") == RunState{});
    const std::string path = "build/test_run_state.ini";
    CHECK(save_run_state(path, r) && load_run_state(path) == r);
    CHECK(save_run_state(path, clean_end()) && load_run_state(path) == RunState{});
    CHECK(load_run_state("build/no-such-run-state.ini") == RunState{});
}

TEST(safe_settings_lower_display_and_quality_only) {
    Settings u{};
    u.fullscreen = true; u.msaa = 8; u.hd_textures = true; u.fov = 100; u.button_prompts = 2; u.fps_view = 2; u.frame_cap = 240;
    const Settings s = safe_settings(u);
    CHECK(s.msaa == 1 && !s.hd_textures && !s.fullscreen && s.vsync && s.frame_cap == 0 && s.filter == Filter::kBilinear);
    CHECK(!s.motion_blur && !s.specular);                                 // the Low preset's effects
    CHECK(s.fov == 100 && s.button_prompts == 2 && s.fps_view == 2);         // camera and interface kept
    CHECK(s.latency == 0 && s.lead == -1 && s.frames_in_flight == -1);    // latency back to auto
}

TEST(hd_pack_folder_in_the_ini) {
    // mac5: the HD pack folder chosen on the page / in the launcher: a path with spaces survives the ini
    Settings s{};
    s.hd_pack_dir = "C:/Users/Some One/PCSX2/textures/SLUS-21050";
    const Settings b = from_ini(to_ini(s));
    CHECK(b.hd_pack_dir == s.hd_pack_dir && b == s);
    CHECK(from_ini(to_ini(Settings{})).hd_pack_dir.empty());
    // the QUALITY tab: choose, and remove only when one is chosen
    auto has = [](const Content& c, std::uint32_t id) {
        for (const Tab& t : c.tabs) for (const Row& r : t.rows) if (r.id == id) return true;
        return false;
    };
    CHECK(has(build_content(Settings{}, Env{}), kActionChooseHdPack) && !has(build_content(Settings{}, Env{}), kActionClearHdPack));
    CHECK(has(build_content(s, Env{}), kActionClearHdPack));
}

TEST(music_folder_and_playlist) {
    // the custom soundtrack (remaster-features §6): the folder survives the ini, the mode is 0..2, default EA Trax only
    CHECK(Settings{}.playlist_mode == 0 && Settings{}.music_folder.empty());
    Settings s{};
    s.music_folder = "D:/My Music/Punk = Rock";
    s.playlist_mode = 2;
    const Settings b = from_ini(to_ini(s));
    CHECK(b.music_folder == s.music_folder && b.playlist_mode == 2 && b == s);
    std::vector<std::string> w;
    CHECK(from_ini("playlist_mode = 3\n", &w).playlist_mode == 0 && !w.empty());
    // the MUSIC section (INTERFACE tab) only for a host that plays the folder (native_play; not the launcher), and no
    // seventh tab (the tab bar's width)
    auto tab = [](const Content& c) -> const Tab* {
        for (const Tab& t : c.tabs)
            for (const Row& r : t.rows)
                if (r.id == kActionChooseMusic) return &t;
        return nullptr;
    };
    CHECK(tab(build_content(Settings{}, Env{})) == nullptr);
    {
        Env both;
        both.music = true;
        both.profile = 1;
        CHECK(build_content(Settings{}, both).tabs.size() == 6);
    }
    Env e;
    e.music = true;
    const Content c = build_content(s, e);
    const Tab* t = tab(c);
    CHECK(t != nullptr);
    if (!t) return;
    bool pick = false, clear = false, mode = false;
    for (const Row& r : t->rows) {
        if (r.id == kActionChooseMusic) pick = r.kind == RowKind::kAction && !r.disabled && r.values[0] == "Punk = Rock";
        if (r.id == kActionClearMusic) clear = !r.disabled;
        if (r.label == "PLAYLIST") {
            mode = r.kind == RowKind::kChoice && r.values.size() == 3 && r.value == 2;
            Settings x;
            CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.playlist_mode == 1);
        }
    }
    CHECK(pick && clear && mode);
    for (const Row& r : tab(build_content(Settings{}, e))->rows) CHECK(r.id != kActionClearMusic);
}

TEST(accessibility_rows) {
    // remaster-features "Accessibility": SCREEN FLASHES, HUD COLOUR FILTER and BOOST in the INTERFACE tab's ACCESSIBILITY
    // section (no seventh tab); the defaults are the game's (Full, Off, Hold), in the file, untouched by the presets
    const Settings d{};
    CHECK(d.screen_flashes == 0 && d.hud_colour_filter == 0 && d.boost_mode == 0 && d.hud_size == 0);
    Settings s;
    s.screen_flashes = 1;
    s.hud_colour_filter = 3;
    s.boost_mode = 1;
    s.hud_size = 2;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("screen_flashes = 1") != std::string::npos && to_ini(s).find("hud_colour_filter = 3") != std::string::npos &&
          to_ini(s).find("boost_mode = 1") != std::string::npos && to_ini(s).find("hud_size = 2") != std::string::npos);
    std::vector<std::string> w;
    const Settings bad = from_ini("screen_flashes = 2\nhud_colour_filter = 4\nboost_mode = -1\nhud_size = 4\n", &w);
    CHECK(bad.screen_flashes == 0 && bad.hud_colour_filter == 0 && bad.boost_mode == 0 && bad.hud_size == 0 && w.size() == 4);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        const Settings q = with_preset(s, p);
        CHECK(q.screen_flashes == 1 && q.hud_colour_filter == 3 && q.boost_mode == 1 && q.hud_size == 2);
    }
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {   // (the launcher's Env too: no music, no card)
        const Content c = build_content(Settings{}, env);
        CHECK(c.tabs.size() == 5);
        const Tab* in = nullptr;
        for (const Tab& t : c.tabs) if (t.label == "INTERFACE") in = &t;
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t sec = 0, flashes = 0, colour = 0, size = 0, boost = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            if (r.kind == RowKind::kSection && r.label == "ACCESSIBILITY") sec = i;
            Settings x;
            if (r.label == "SCREEN FLASHES") {
                flashes = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 2 && r.value == 0 && !r.disabled);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.screen_flashes == 1);
            }
            if (r.label == "HUD COLOUR FILTER") {
                colour = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 4 && r.value == 0 && r.values[1] == "Deuteranopia" &&
                      r.values[2] == "Protanopia" && r.values[3] == "Tritanopia");
                for (std::size_t v = 0; v < 4; ++v) CHECK(apply_edit(x, Edit{r.id, v, 0, false}) && x.hud_colour_filter == int(v));
            }
            if (r.label == "HUD SIZE") {   // textsize1: the race HUD only (the row says so), 100 % = the game's
                size = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 4 && r.value == 0 && r.values[0] == "100%" && r.values[3] == "150%");
                for (std::size_t v = 0; v < 4; ++v) CHECK(apply_edit(x, Edit{r.id, v, 0, false}) && x.hud_size == int(v));
                CHECK(r.description.find("Menus keep their size") != std::string::npos);
            }
            if (r.label == "BOOST") {
                boost = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "Hold" && r.values[1] == "Toggle" && r.value == 0);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.boost_mode == 1);
                CHECK(r.description.find("input") != std::string::npos);   // says it changes the game's input
            }
        }
        CHECK(sec > 0 && flashes == sec + 1 && colour == sec + 2 && size == sec + 3 && boost == sec + 4);
        Settings set;   // shown as set
        set.hud_colour_filter = 2;
        for (const Tab& t : build_content(set, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "HUD COLOUR FILTER") CHECK(r.value == 2);
    }
}

TEST(ghost_row) {
    // ghost1 (Time Attack ghosts, src/b3replay/ghost.h): GHOST Off / Best lap in the INTERFACE tab's TIME ATTACK section,
    // before ACCESSIBILITY; Off by default (the frames byte-identical), in the file, untouched by the presets
    const Settings d{};
    CHECK(d.ghost == 0);
    Settings s;
    s.ghost = 1;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("ghost = 1") != std::string::npos && to_ini(d).find("ghost = 0") != std::string::npos);
    std::vector<std::string> w;
    CHECK(from_ini("ghost = 2\n", &w).ghost == 0 && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).ghost == 1);
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        const Tab* in = nullptr;
        for (const Tab& t : c.tabs) if (t.label == "INTERFACE") in = &t;
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t sec = 0, row = 0, access = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            if (r.kind == RowKind::kSection && r.label == "TIME ATTACK") sec = i;
            if (r.kind == RowKind::kSection && r.label == "ACCESSIBILITY") access = i;
            if (r.label == "GHOST") {
                row = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "Best lap" && r.value == 0 &&
                      !r.disabled);
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.ghost == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.ghost == 0);
            }
        }
        CHECK(sec > 0 && row == sec + 1 && access > row);
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "GHOST") CHECK(r.value == 1);
    }
}

TEST(speedrun_rows) {
    // speedrun1 (src/b3replay/speedrun.h): SPEEDRUN TIMER Off / Event / Event + splits and INSTANT RESTART Off / On in the
    // INTERFACE tab's SPEEDRUN section, after TIME ATTACK and before ACCESSIBILITY; both Off by default, in the file,
    // untouched by the presets
    const Settings d{};
    CHECK(d.speedrun_timer == 0 && d.instant_restart == 0);
    Settings s;
    s.speedrun_timer = 2;
    s.instant_restart = 1;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("speedrun_timer = 2") != std::string::npos && to_ini(d).find("instant_restart = 0") != std::string::npos);
    std::vector<std::string> w;
    CHECK(from_ini("speedrun_timer = 3\ninstant_restart = 2\n", &w).speedrun_timer == 0 && w.size() == 2);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        CHECK(with_preset(s, p).speedrun_timer == 2);
        CHECK(with_preset(s, p).instant_restart == 1);
    }
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        const Tab* in = nullptr;
        for (const Tab& t : c.tabs) if (t.label == "INTERFACE") in = &t;
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t ta = 0, sec = 0, timer = 0, restart = 0, access = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            if (r.kind == RowKind::kSection && r.label == "TIME ATTACK") ta = i;
            if (r.kind == RowKind::kSection && r.label == "SPEEDRUN") sec = i;
            if (r.kind == RowKind::kSection && r.label == "ACCESSIBILITY") access = i;
            Settings x;
            if (r.label == "SPEEDRUN TIMER") {
                timer = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 3 && r.values[0] == "Off" && r.values[2] == "Event + splits" && r.value == 0);
                CHECK(apply_edit(x, Edit{r.id, 2, 0, false}) && x.speedrun_timer == 2);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.speedrun_timer == 0);
            }
            if (r.label == "INSTANT RESTART") {
                restart = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[1] == "On" && r.value == 0);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.instant_restart == 1);
            }
        }
        CHECK(ta > 0 && sec > ta && timer == sec + 1 && restart == sec + 2 && access > restart);
    }
}

TEST(rumble_rows) {
    // rumble1 (src/native/pad_actuator.h): RUMBLE Off / On (default Off: the requests left pending, as before), RUMBLE
    // STRENGTH 10-100 % (default 100) and TRIGGER RUMBLE (default off) in the INTERFACE tab's CONTROLLER section; in the
    // file, untouched by the presets
    const Settings d{};
    CHECK(d.rumble == 0 && d.rumble_strength == 100 && !d.rumble_triggers);
    CHECK(to_ini(d).find("rumble = 0") != std::string::npos && to_ini(d).find("rumble_strength = 100") != std::string::npos &&
          to_ini(d).find("rumble_triggers = false") != std::string::npos);
    Settings s;
    s.rumble = 1;
    s.rumble_strength = 40;
    s.rumble_triggers = true;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(from_ini("rumble = 1\n").rumble == 1);
    std::vector<std::string> w;
    const Settings bad = from_ini("rumble = 2\nrumble_strength = 5\nrumble_strength = 101\nrumble_triggers = maybe\n", &w);
    CHECK(bad.rumble == 0 && bad.rumble_strength == 100 && !bad.rumble_triggers && w.size() == 4);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        const Settings q = with_preset(s, p);
        CHECK(q.rumble == 1 && q.rumble_strength == 40 && q.rumble_triggers);
    }
    CHECK(matching_preset(s) == Preset::kHigh);
    CHECK(hset_encode(s, 1).size() > 0);   // still fits the HSET record
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        const Tab* in = nullptr;
        for (const Tab& t : c.tabs) if (t.label == "INTERFACE") in = &t;
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t sec = 0, on = 0, strength = 0, triggers = 0, ghost = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            Settings x;
            if (r.kind == RowKind::kSection && r.label == "CONTROLLER") sec = i;
            if (r.kind == RowKind::kSection && r.label == "TIME ATTACK") ghost = i;
            if (r.label == "RUMBLE") {
                on = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "On" && r.value == 0 &&
                      !r.disabled);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.rumble == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.rumble == 0);
            }
            if (r.label == "RUMBLE STRENGTH") {
                strength = i;
                CHECK(r.kind == RowKind::kSlider && r.levels == 10 && r.value == 9 && r.level_text == "100%");
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.rumble_strength == 10);
                CHECK(apply_edit(x, Edit{r.id, 4, 0, false}) && x.rumble_strength == 50);
                CHECK(apply_edit(x, Edit{r.id, 20, 0, false}) && x.rumble_strength == 100);
            }
            if (r.label == "TRIGGER RUMBLE") {
                triggers = i;
                CHECK(r.kind == RowKind::kToggle && r.value == 0);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.rumble_triggers);
            }
        }
        CHECK(sec > 0 && on == sec + 1 && strength == sec + 2 && triggers == sec + 3 && ghost > triggers);
        for (const Tab& t : build_content(s, env).tabs)   // shown as set
            for (const Row& r : t.rows) {
                if (r.label == "RUMBLE") CHECK(r.value == 1);
                if (r.label == "RUMBLE STRENGTH") CHECK(r.value == 3 && r.level_text == "40%");
                if (r.label == "TRIGGER RUMBLE") CHECK(r.value == 1);
            }
    }
}

TEST(other_music_apps_row) {
    // nowplaying1 (tools/native/now_playing_popup.h): OTHER MUSIC APPS Off / Show now playing (default Off: no reader, no
    // system call) after PLAYLIST in the INTERFACE tab's MUSIC section (only with the host's music: native_play); in the
    // file, untouched by the presets
    const Settings d{};
    CHECK(d.other_music_apps == 0 && to_ini(d).find("other_music_apps = 0") != std::string::npos);
    Settings s;
    s.other_music_apps = 1;
    CHECK(from_ini(to_ini(s)) == s);
    std::vector<std::string> w;
    CHECK(from_ini("other_music_apps = 2\n", &w).other_music_apps == 0 && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).other_music_apps == 1);
    auto find = [](const Content& c, const std::string& label) -> const Row* {
        for (const Tab& t : c.tabs)
            for (const Row& r : t.rows)
                if (r.label == label) return &r;
        return nullptr;
    };
    Env env;
    CHECK(!find(build_content(s, env), "OTHER MUSIC APPS"));   // no MUSIC section without the host's music
    env.music = true;
    const Content c = build_content(Settings{}, env);
    const Row* r = find(c, "OTHER MUSIC APPS");
    CHECK(r && r->id == 91 && r->kind == RowKind::kChoice && r->value == 0);
    if (!r) return;
    CHECK((r->values == std::vector<std::string>{"Off", "Show now playing"}));
#if defined(__APPLE__)
    CHECK(r->disabled && r->description.find("macOS") != std::string::npos);
#else
    CHECK(!r->disabled && r->description.find("EA TRAX popup") != std::string::npos);
    CHECK(r->description.find("Now:") == std::string::npos);   // off: nothing detected is shown
    env.now_playing = "Spotify: Band - Song, playing";
    const Content c2 = build_content(s, env);
    const Row* on = find(c2, "OTHER MUSIC APPS");
    CHECK(on && on->value == 1 && on->description.find("Now: Spotify: Band - Song, playing.") != std::string::npos);
#endif
    Settings x;
    CHECK(apply_edit(x, Edit{r->id, 1, 0, false}) && x.other_music_apps == 1);
    CHECK(apply_edit(x, Edit{r->id, 0, 0, false}) && x.other_music_apps == 0);
    // right after PLAYLIST
    const Tab* in = nullptr;
    for (const Tab& t : c.tabs) if (t.label == "INTERFACE") in = &t;
    CHECK(in != nullptr);
    if (!in) return;
    for (std::size_t i = 0; i + 1 < in->rows.size(); ++i)
        if (in->rows[i].label == "PLAYLIST") CHECK(in->rows[i + 1].label == "OTHER MUSIC APPS");
}

TEST(adaptive_trigger_rows) {
    // pad1 (src/native/pad_triggers.h): ADAPTIVE TRIGGERS Off / On (default Off: no effect ever sent) and TRIGGER STRENGTH
    // 10-100 % (default 100) after TRIGGER RUMBLE in the INTERFACE tab's CONTROLLER section; in the file, untouched by
    // the presets
    const Settings d{};
    CHECK(d.adaptive_triggers == 0 && d.adaptive_triggers_strength == 100);
    CHECK(to_ini(d).find("adaptive_triggers = 0") != std::string::npos && to_ini(d).find("adaptive_triggers_strength = 100") != std::string::npos);
    Settings s;
    s.adaptive_triggers = 1;
    s.adaptive_triggers_strength = 30;
    CHECK(from_ini(to_ini(s)) == s);
    std::vector<std::string> w;
    const Settings bad = from_ini("adaptive_triggers = 2\nadaptive_triggers_strength = 5\nadaptive_triggers_strength = 101\n", &w);
    CHECK(bad.adaptive_triggers == 0 && bad.adaptive_triggers_strength == 100 && w.size() == 3);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        const Settings q = with_preset(s, p);
        CHECK(q.adaptive_triggers == 1 && q.adaptive_triggers_strength == 30);
    }
    CHECK(matching_preset(s) == Preset::kHigh);
    CHECK(hset_encode(s, 1).size() > 0);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Tab* in = nullptr;
        const Content c = build_content(Settings{}, env);
        for (const Tab& t : c.tabs) if (t.label == "INTERFACE") in = &t;
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t triggers = 0, on = 0, strength = 0, ghost = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            Settings x;
            if (r.label == "TRIGGER RUMBLE") triggers = i;
            if (r.kind == RowKind::kSection && r.label == "TIME ATTACK") ghost = i;
            if (r.label == "ADAPTIVE TRIGGERS") {
                on = i;
                CHECK(r.id == 190 && r.kind == RowKind::kChoice && r.values.size() == 2 && r.value == 0 && !r.disabled);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.adaptive_triggers == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.adaptive_triggers == 0);
            }
            if (r.label == "TRIGGER STRENGTH") {
                strength = i;
                CHECK(r.id == 191 && r.kind == RowKind::kSlider && r.levels == 10 && r.value == 9 && r.level_text == "100%");
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.adaptive_triggers_strength == 10);
                CHECK(apply_edit(x, Edit{r.id, 6, 0, false}) && x.adaptive_triggers_strength == 70);
                CHECK(apply_edit(x, Edit{r.id, 20, 0, false}) && x.adaptive_triggers_strength == 100);
            }
        }
        CHECK(triggers > 0 && on == triggers + 1 && strength == triggers + 2 && ghost > strength);   // (carsel1's row between)
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows) {
                if (r.label == "ADAPTIVE TRIGGERS") CHECK(r.value == 1);
                if (r.label == "TRIGGER STRENGTH") CHECK(r.value == 2 && r.level_text == "30%");
            }
    }
}

TEST(series_shoulders_row) {
    // carsel1 (src/native/input_keys.h ShoulderSeries): CAR SERIES ON L1 / R1 Off / On (default Off: the reports untouched,
    // nothing drawn), row 200, the CONTROLLER section's last row (after TRIGGER STRENGTH, before TIME ATTACK); in the file,
    // untouched by the presets
    const Settings d{};
    CHECK(d.series_shoulders == 0 && to_ini(d).find("series_shoulders = 0") != std::string::npos);
    Settings s;
    s.series_shoulders = 1;
    CHECK(from_ini(to_ini(s)) == s);
    std::vector<std::string> w;
    CHECK(from_ini("series_shoulders = 2\n", &w).series_shoulders == 0 && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).series_shoulders == 1);
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Tab* in = nullptr;
        const Content c = build_content(Settings{}, env);
        for (const Tab& t : c.tabs) if (t.label == "INTERFACE") in = &t;
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t strength = 0, row = 0, ghost = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            Settings x;
            if (r.label == "TRIGGER STRENGTH") strength = i;
            if (r.kind == RowKind::kSection && r.label == "TIME ATTACK") ghost = i;
            if (r.label == "CAR SERIES ON L1 / R1") {
                row = i;
                CHECK(r.id == 200 && r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "On" &&
                      r.value == 0 && !r.disabled);
                CHECK(r.description.find("input") != std::string::npos);   // says it changes the game's input
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.series_shoulders == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.series_shoulders == 0);
            }
        }
        // 1.0.1: ANALOG TRIGGERS between CAR SERIES and the TIME ATTACK section
        CHECK(strength > 0 && row == strength + 1 && ghost == row + 2 && in->rows[row + 1].label == "ANALOG TRIGGERS");
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "CAR SERIES ON L1 / R1") CHECK(r.value == 1);
    }
}

TEST(gyro_controls_rows) {
    // pad1: the CONTROLS section's gyro rows (ids 305-309) with the controller device only, after the action rows and
    // before RESET; the host applies them (apply_edit leaves them)
    Env env{true};
    env.controls.on = true;
    env.controls.device = 0;
    env.controls.actions.push_back({"CROSS", "Z", ""});
    env.controls.gyro = false;
    for (const Tab& t : build_content(Settings{}, env).tabs)
        for (const Row& r : t.rows) CHECK(r.label.find("GYRO") == std::string::npos);
    env.controls.device = 1;
    env.controls.gyro = true;
    env.controls.gyro_mode = 2;
    env.controls.gyro_axis = 1;
    env.controls.gyro_sensitivity = 7;   // 200 %
    env.controls.gyro_dead_zone = 4;
    env.controls.gyro_recentre = 3;
    env.controls.gyro_recentre_labels = {"NONE", "TOUCHPAD", "SELECT", "L3", "R3", "MIC", "PADDLE L1", "PADDLE R1"};
    const Tab* in = nullptr;
    const Content c = build_content(Settings{}, env);
    for (const Tab& t : c.tabs) if (t.label == "INTERFACE") in = &t;
    CHECK(in != nullptr);
    if (!in) return;
    std::size_t action = 0, reset = 0, first = 0, n = 0;
    for (std::size_t i = 0; i < in->rows.size(); ++i) {
        const Row& r = in->rows[i];
        if (r.id == kActionCtlFirst) action = i;
        if (r.id == kActionCtlReset) reset = i;
        if (r.id < kRowCtlGyroMode || r.id > kRowCtlGyroRecentre) continue;
        if (!first) first = i;
        ++n;
        CHECK(r.kind == RowKind::kChoice);
        Settings x;
        CHECK(!apply_edit(x, Edit{r.id, 1, 0, false}) && x == Settings{});
        if (r.id == kRowCtlGyroMode) CHECK(r.label == "GYRO STEERING" && r.values.size() == 3 && r.value == 2);
        if (r.id == kRowCtlGyroAxis) CHECK(r.values.size() == 2 && r.value == 1);
        if (r.id == kRowCtlGyroSensitivity) CHECK(r.values.size() == 12 && r.value == 7 && r.values[7] == "200%" && r.values[0] == "25%");
        if (r.id == kRowCtlGyroDeadZone) CHECK(r.values.size() == 11 && r.value == 4);
        if (r.id == kRowCtlGyroRecentre) CHECK(r.values.size() == 8 && r.value == 3 && r.values[3] == "L3");
    }
    CHECK(n == 5 && first == action + 1 && reset == first + 5);
}

TEST(record_replays_row) {
    // replay1 (replay files, src/native/replay_file.h): RECORD REPLAYS Off / On in the INTERFACE tab's REPLAYS section,
    // after TIME ATTACK; Off by default (nothing recorded), in the file, untouched by the presets
    const Settings d{};
    CHECK(d.record_replays == 0);
    Settings s;
    s.record_replays = 1;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("record_replays = 1") != std::string::npos && to_ini(d).find("record_replays = 0") != std::string::npos);
    std::vector<std::string> w;
    CHECK(from_ini("record_replays = 2\n", &w).record_replays == 0 && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).record_replays == 1);
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        const Tab* in = nullptr;
        for (const Tab& t : c.tabs) if (t.label == "INTERFACE") in = &t;
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t sec = 0, row = 0, ghost = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            if (r.kind == RowKind::kSection && r.label == "REPLAYS") sec = i;
            if (r.label == "GHOST") ghost = i;
            if (r.label == "RECORD REPLAYS") {
                row = i;
                CHECK(r.kind == RowKind::kToggle && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "On" && r.value == 0 && !r.disabled);
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.record_replays == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.record_replays == 0);
            }
        }
        CHECK(ghost > 0 && sec > ghost && row == sec + 1);
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "RECORD REPLAYS") CHECK(r.value == 1);
    }
}

TEST(launcher_at_start_row) {
    // launchskip1: OPEN THE LAUNCHER AT START On / Off (default On: the launcher shows), in the INTERFACE tab right
    // after DATE FORMAT (never on DISPLAY), row 210; ini key launcher_at_start; untouched by the presets; the launcher
    // reads it (src/b3launcher/core.h skip_launcher)
    const Settings d{};
    CHECK(d.launcher_at_start);
    CHECK(to_ini(d).find("launcher_at_start = true") != std::string::npos);
    Settings s;
    s.launcher_at_start = false;
    CHECK(to_ini(s).find("launcher_at_start = false") != std::string::npos);
    CHECK(from_ini(to_ini(s)) == s && !from_ini(to_ini(s)).launcher_at_start);
    CHECK(from_ini("launcher_at_start = false\n").launcher_at_start == false);
    CHECK(from_ini("launcher_at_start = true\n").launcher_at_start == true);
    CHECK(from_ini("").launcher_at_start == true);   // an older file: on
    std::vector<std::string> w;
    CHECK(from_ini("launcher_at_start = maybe\n", &w).launcher_at_start == true && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(!with_preset(s, p).launcher_at_start);
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        int found = 0;
        for (const Tab& t : c.tabs)
            for (std::size_t i = 0; i < t.rows.size(); ++i) {
                const Row& r = t.rows[i];
                if (r.label != "OPEN THE LAUNCHER AT START") continue;
                ++found;
                CHECK(t.label == "INTERFACE" && i > 0 && t.rows[i - 1].label == "DATE FORMAT");
                CHECK(r.id == 210 && r.kind == RowKind::kToggle && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "On" &&
                      r.value == 1 && !r.disabled);
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && !x.launcher_at_start);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.launcher_at_start);
            }
        CHECK(found == 1);
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "OPEN THE LAUNCHER AT START") CHECK(r.value == 0);
        // the DISPLAY tab (the golden's page shot) is as before
        CHECK(c.tabs[0].label == "DISPLAY");
        for (const Row& r : c.tabs[0].rows) CHECK(r.id != 210);
    }
}

TEST(speed_units_row) {
    // units1 (tools/native/speed_units.h): SPEED UNITS MPH / KM/H (default MPH: the US game's), in the INTERFACE tab right
    // before DATE FORMAT (DATE FORMAT stays right before OPEN THE LAUNCHER AT START), row 220; ini key speed_units 0-1;
    // untouched by the presets and safe mode
    const Settings d{};
    CHECK(d.speed_units == 0);
    CHECK(to_ini(d).find("speed_units = 0") != std::string::npos);
    Settings s;
    s.speed_units = 1;
    CHECK(to_ini(s).find("speed_units = 1") != std::string::npos);
    CHECK(from_ini(to_ini(s)) == s && from_ini(to_ini(s)).speed_units == 1);
    CHECK(from_ini("").speed_units == 0);   // an older file: MPH
    std::vector<std::string> w;
    CHECK(from_ini("speed_units = 7\n", &w).speed_units == 0 && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).speed_units == 1);
    CHECK(safe_settings(s).speed_units == 1);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        int found = 0;
        for (const Tab& t : c.tabs)
            for (std::size_t i = 0; i < t.rows.size(); ++i) {
                const Row& r = t.rows[i];
                if (r.label != "SPEED UNITS") continue;
                ++found;
                CHECK(t.label == "INTERFACE" && i + 2 < t.rows.size() && t.rows[i + 1].label == "DATE FORMAT" &&
                      t.rows[i + 2].label == "OPEN THE LAUNCHER AT START");
                CHECK(r.id == 220 && r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "MPH" && r.values[1] == "KM/H" &&
                      r.value == 0 && !r.disabled);
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.speed_units == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.speed_units == 0);
            }
        CHECK(found == 1);
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "SPEED UNITS") CHECK(r.value == 1);
        CHECK(c.tabs[0].label == "DISPLAY");   // the golden's page shot: DISPLAY as before
        for (const Row& r : c.tabs[0].rows) CHECK(r.id != 220);
    }
}

TEST(photo_row_setting) {
    // photorow1 (tools/native/pc_menu.h): PHOTO MODE IN PAUSE MENU On / Off (default On: the pause menu has the row), in the
    // INTERFACE tab after VIDEO SKIP HINT (never on DISPLAY), row 240; ini key photo_row; the presets (the PS2 ORIGINAL one
    // too: it only sets visuals) and safe mode leave it alone
    const Settings d{};
    CHECK(d.photo_row);
    CHECK(to_ini(d).find("photo_row = true") != std::string::npos);
    Settings s;
    s.photo_row = false;
    CHECK(to_ini(s).find("photo_row = false") != std::string::npos);
    CHECK(from_ini(to_ini(s)) == s && !from_ini(to_ini(s)).photo_row);
    CHECK(from_ini("photo_row = false\n").photo_row == false);
    CHECK(from_ini("photo_row = true\n").photo_row == true);
    CHECK(from_ini("").photo_row == true);   // an older file: on
    std::vector<std::string> w;
    CHECK(from_ini("photo_row = maybe\n", &w).photo_row == true && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        CHECK(!with_preset(s, p).photo_row);                   // off stays off
        CHECK(with_preset(Settings{}, p).photo_row);           // on stays on
    }
    CHECK(safe_settings(s).photo_row == s.photo_row);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        int found = 0;
        for (const Tab& t : c.tabs)
            for (std::size_t i = 0; i < t.rows.size(); ++i) {
                const Row& r = t.rows[i];
                if (r.label != "PHOTO MODE IN PAUSE MENU") continue;
                ++found;
                CHECK(t.label == "INTERFACE" && i > 0 && t.rows[i - 1].label == "VIDEO SKIP HINT");
                CHECK(r.id == 240 && r.kind == RowKind::kToggle && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "On" &&
                      r.value == 1 && !r.disabled);
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && !x.photo_row);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.photo_row);
            }
        CHECK(found == 1);
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "PHOTO MODE IN PAUSE MENU") CHECK(r.value == 0);
        CHECK(c.tabs[0].label == "DISPLAY");
        for (const Row& r : c.tabs[0].rows) CHECK(r.id != 240);
    }
}

TEST(fps_counter_rows_and_old_ini) {
    // fpsoverlay2: FPS COUNTER (Off / Minimal / Standard / Detailed, ini fps_view 0-3, row 255) and its corner (fps_corner 0-3,
    // row 256), first on INTERFACE; an ini from before the views holds fps_counter = true / false (on = Standard)
    const Settings d{};
    CHECK(d.fps_view == 0 && d.fps_corner == 0);
    CHECK(from_ini("fps_counter = true\n").fps_view == 2 && from_ini("fps_counter = on\n").fps_view == 2 && from_ini("fps_counter = 1\n").fps_view == 2);
    CHECK(from_ini("fps_counter = false\n").fps_view == 0 && from_ini("fps_counter = 0\n").fps_view == 0);
    CHECK(from_ini("fps_view = 3\n").fps_view == 3 && from_ini("fps_view = 1\nfps_corner = 2\n").fps_corner == 2);
    CHECK(from_ini("fps_counter = true\nfps_view = 3\n").fps_view == 3);   // the newer key wins, whatever the order
    CHECK(from_ini("fps_view = 1\nfps_counter = true\n").fps_view == 1);
    std::vector<std::string> w;
    CHECK(from_ini("fps_view = 9\nfps_corner = x\nfps_counter = maybe\n", &w).fps_view == 0 && w.size() == 3);
    Settings s;
    s.fps_view = 3;
    s.fps_corner = 1;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("fps_counter") == std::string::npos && to_ini(s).find("fps_view = 3") != std::string::npos);
    for (const Env& env : {Env{true}, Env{false}}) {
        int found = 0;
        for (const Tab& t : build_content(s, env).tabs)
            for (std::size_t i = 0; i < t.rows.size(); ++i) {
                const Row& r = t.rows[i];
                if (r.label != "FPS COUNTER") continue;
                ++found;
                CHECK(t.label == "INTERFACE" && i == 0 && r.id == 255 && r.kind == RowKind::kChoice && r.values.size() == 4 && r.value == 3);
                CHECK(r.values[0] == "Off" && r.values[1] == "Minimal" && r.values[2] == "Standard" && r.values[3] == "Detailed");
                CHECK(t.rows[i + 1].id == 256 && t.rows[i + 1].values.size() == 4 && t.rows[i + 1].value == 1);
                Settings x;
                CHECK(apply_edit(x, Edit{255, 2, 0, false}) && x.fps_view == 2);
                CHECK(apply_edit(x, Edit{256, 3, 0, false}) && x.fps_corner == 3);
            }
        CHECK(found == 1);
    }
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).fps_view == 3);

}
TEST(effect_levels_effects2) {
    // effects2: SPARKS and CRASH DEBRIS are Off / Low / Medium / High (0..3), High the default (the game's whole set);
    // the presets drive them: PS2 ORIGINAL, HIGH, ULTRA High, MEDIUM Medium, LOW Low; ini keys sparks / popcorn
    const Settings d{};
    CHECK(d.sparks == kLevelHigh && d.popcorn == kLevelHigh && !d.battery_saver);
    for (Preset p : {Preset::kPs2, Preset::kHigh, Preset::kUltra}) {
        CHECK(with_preset(d, p).sparks == kLevelHigh && with_preset(d, p).popcorn == kLevelHigh);
        CHECK(matching_preset(with_preset(d, p)) == p);
    }
    CHECK(with_preset(d, Preset::kMedium).sparks == kLevelMedium && with_preset(d, Preset::kMedium).popcorn == kLevelMedium);
    CHECK(with_preset(d, Preset::kLow).sparks == kLevelLow && with_preset(d, Preset::kLow).popcorn == kLevelLow);
    CHECK(matching_preset(with_preset(d, Preset::kLow)) == Preset::kLow && matching_preset(with_preset(d, Preset::kMedium)) == Preset::kMedium);
    CHECK(safe_settings(d).sparks == kLevelLow);
    Settings s = with_preset(d, Preset::kLow);
    s.sparks = kLevelOff;
    CHECK(matching_preset(s) == Preset::kCustom);   // one row changed: Custom
    // the ini round trip at every level, and the files from before the levels (true / false)
    for (int a = 0; a <= 3; ++a)
        for (int b = 0; b <= 3; ++b) {
            Settings x;
            x.sparks = a;
            x.popcorn = b;
            CHECK(from_ini(to_ini(x)) == x);
        }
    CHECK(from_ini("sparks = false\npopcorn = true\n").sparks == kLevelOff && from_ini("sparks = false\npopcorn = true\n").popcorn == kLevelHigh);
    CHECK(from_ini("sparks = on\n").sparks == kLevelHigh && from_ini("sparks = 2\n").sparks == kLevelMedium);
    std::vector<std::string> w;
    CHECK(from_ini("sparks = 4\npopcorn = loud\n", &w).sparks == kLevelHigh && w.size() == 2);   // out of range: the default
    CHECK(std::string(level_name(0)) == "Off" && std::string(level_name(1)) == "Low" && std::string(level_name(2)) == "Medium" &&
          std::string(level_name(3)) == "High");
    CHECK(level_percent(0) == 0 && level_percent(1) == 25 && level_percent(2) == 50 && level_percent(3) == 100);
    // the rows: EFFECTS tab, Off / Low / Medium / High, ids as before, the edit takes the index
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        int found = 0;
        for (const Tab& t : c.tabs)
            for (const Row& r : t.rows) {
                if (r.label != "SPARKS" && r.label != "CRASH DEBRIS") continue;
                ++found;
                CHECK(t.label == "EFFECTS" && r.kind == RowKind::kChoice && r.values.size() == 4 && r.values[0] == "Off" && r.values[3] == "High" && r.value == 3);
                Settings x;
                for (int v = 0; v <= 3; ++v) {
                    CHECK(apply_edit(x, Edit{r.id, std::size_t(v), 0, false}));
                    CHECK((r.label == "SPARKS" ? x.sparks : x.popcorn) == v);
                }
            }
        CHECK(found == 2);
        for (const Tab& t : build_content(with_preset(Settings{}, Preset::kLow), env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "SPARKS" || r.label == "CRASH DEBRIS") CHECK(r.value == 1);
        for (const Row& r : c.tabs[0].rows) CHECK(r.id != 245);   // not on DISPLAY (its scroll bar is in the golden record)
    }
}

TEST(battery_saver_effects2) {
    // BATTERY SAVER (EFFECTS > POWER, row 245, default off): the host's copy while on battery holds the frame rate to 60 and
    // the particle effects to at most Medium; off, or plugged in (the host does not call it), nothing changes
    Settings s;
    CHECK(!s.battery_saver && to_ini(s).find("battery_saver = false") != std::string::npos);
    CHECK(on_battery(s) == s);                    // off: nothing
    s.battery_saver = true;
    CHECK(from_ini(to_ini(s)) == s && from_ini(to_ini(s)).battery_saver);
    Settings b = on_battery(s);
    CHECK(b.frame_cap == 60 && b.sparks == kLevelMedium && b.popcorn == kLevelMedium);
    s.frame_cap = 30;
    s.sparks = kLevelLow;
    b = on_battery(s);
    CHECK(b.frame_cap == 30 && b.sparks == kLevelLow);   // never raised
    s.frame_cap = 144;
    CHECK(on_battery(s).frame_cap == 60);
    CHECK(with_preset(s, Preset::kUltra).battery_saver);   // the presets leave it
    int found = 0;
    for (const Tab& t : build_content(Settings{}, Env{true}).tabs)
        for (const Row& r : t.rows)
            if (r.label == "BATTERY SAVER") {
                ++found;
                CHECK(t.label == "EFFECTS" && r.id == 245 && r.kind == RowKind::kToggle && r.value == 0);
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.battery_saver);
            }
    CHECK(found == 1);
}

TEST(render_scale_effects2) {
    // RENDER SCALE (GRAPHICS tab, row 246, default 100 %): the 3D picture at a share of the window; the presets: LOW 75,
    // the others 100; ini `render_scale` 50..100
    const Settings d{};
    CHECK(d.render_scale == 100);
    for (Preset p : {Preset::kPs2, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(d, p).render_scale == 100);
    CHECK(with_preset(d, Preset::kLow).render_scale == 75 && matching_preset(with_preset(d, Preset::kLow)) == Preset::kLow);
    CHECK(with_preset(Settings{}, Preset::kHigh) == d);   // the default is still the HIGH preset
    Settings s = d;
    s.render_scale = 67;
    CHECK(from_ini(to_ini(s)) == s && matching_preset(s) == Preset::kCustom);
    CHECK(to_ini(d).find("render_scale = 100") != std::string::npos);
    std::vector<std::string> w;
    CHECK(from_ini("render_scale = 20\n", &w).render_scale == 100 && w.size() == 1);   // out of range: the default
    CHECK(from_ini("render_scale = 85\n").render_scale == 85);
    CHECK(from_ini("").render_scale == 100);   // an older file
    CHECK(render_scale_index(100) == 0 && render_scale_index(75) == 2 && render_scale_index(50) == 4 && render_scale_index(60) == 3 &&
          render_scale_index(90) == 1);
    int found = 0;
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        for (const Tab& t : c.tabs)
            for (const Row& r : t.rows)
                if (r.label == "RENDER SCALE") {
                    ++found;
                    CHECK(t.label == "GRAPHICS" && r.id == 246 && r.kind == RowKind::kChoice && r.values.size() == 5 && r.values[0] == "100%" &&
                          r.values[4] == "50%" && r.value == 0);
                    for (std::size_t v = 0; v < r.values.size(); ++v) {
                        Settings x;
                        CHECK(apply_edit(x, Edit{r.id, v, 0, false}) && x.render_scale == kRenderScales[v]);
                    }
                }
        for (const Row& r : c.tabs[0].rows) CHECK(r.id != 246);   // not on DISPLAY
        for (const Tab& t : build_content(with_preset(Settings{}, Preset::kLow), env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "RENDER SCALE") CHECK(r.value == 2);
    }
    CHECK(found == 2);
}

TEST(mods_tab_only_with_code_mod_rows) {   // sdk2b: PC SETTINGS > MODS (the code mods' rows; none: no tab, the page as before)
    for (const auto& t : build_content(Settings{}, Env{true}).tabs) CHECK(t.label != "MODS");
    Env e{true};
    Row sec;
    sec.id = kSectionModBase;
    sec.kind = RowKind::kSection;
    sec.label = "MENU-LABEL-SETTING";
    Row r;
    r.id = kModRowBase;
    r.kind = RowKind::kToggle;
    r.label = "QUICK RACE LABEL";
    r.values = {"Off", "On"};
    e.mod_rows = {sec, r};
    const Content c = build_content(Settings{}, e);
    CHECK(!c.tabs.empty() && c.tabs.back().label == "MODS" && c.tabs.back().rows.size() == 2 && c.tabs.back().rows[1].id == kModRowBase);
    Settings s;
    CHECK(!apply_edit(s, Edit{kModRowBase, 1, 1, false}));   // the host's row, not a setting
}

TEST(language_row_and_edit) {
    // 1.0.1 LANGUAGE (INTERFACE): English and the imported languages; language_edit maps the value to a code
    Env env{true};
    env.languages = {{"FR", "Fran\xC3\xA7" "ais (French)"}, {"IT", "Italiano (Italian)"}};
    Settings s;
    s.language = "IT";
    const Content content = build_content(s, env);   // kept: `row` points into it
    const Row* row = nullptr;
    for (const Tab& t : content.tabs)
        for (const Row& r : t.rows)
            if (r.id == kRowLanguage) row = &r;
    CHECK(row && row->label == "LANGUAGE" && row->values.size() == 3 && row->values[0] == "English" && row->value == 2);
    Settings e;
    CHECK(language_edit(e, env, Edit{kRowLanguage, 1, 0, false}) && e.language == "FR");
    CHECK(language_edit(e, env, Edit{kRowLanguage, 0, 0, false}) && e.language.empty());
    CHECK(language_edit(e, env, Edit{kRowLanguage, 9, 0, false}) && e.language.empty());   // out of range: English
    CHECK(!language_edit(e, env, Edit{kRowSaveScreens, 1, 0, false}));                    // not its row
    // a chosen language that is not imported shows English, and says so
    Settings gone;
    gone.language = "GE";
    for (const Tab& t : build_content(gone, env).tabs)
        for (const Row& r : t.rows)
            if (r.id == kRowLanguage) CHECK(r.value == 0 && r.description.find("not imported") != std::string::npos);
    // the ini keeps it (two letters, upper case), empty for English
    CHECK(from_ini(to_ini(s)).language == "IT");
    CHECK(from_ini("language = it\n").language == "IT");
    CHECK(from_ini("language = Italian\n").language.empty());
}

TEST(translations_cover_the_page) {
    // 1.1 (i18n.h): every label, value and description sentence of a full page has its translation in each language
    Env env{true};
    env.music = true;
    env.language_import = true;
    env.languages = {{"FR", "Fran\xC3\xA7" "ais (French)"}};
    env.profile = 1;
    env.next_profile = 2;
    env.controls.on = true;
    env.controls.gyro = true;
    Settings s;
    s.hd_pack_dir = "/packs/SLUS-21050";
    s.music_folder = "/music";
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"}) {
        for (std::size_t player = 0; player < 3; ++player)
            for (std::size_t device = 0; device < 2; ++device) {
                env.controls.player = player;
                env.controls.device = device;
                const std::vector<std::string> missing = untranslated(build_content(s, env), code);
                for (const std::string& m : missing)
                    // data, not words: the chosen folders' names and a language's own name stay as they are
                    if (m.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos &&
                        m.find('/') == std::string::npos && m != "music" && m != "SLUS-21050" && m != env.languages[0].second)
                    {
                        std::printf("  %s lacks: %s\n", code, m.c_str());
                        CHECK(false);
                    }
            }
    }
    CHECK(untranslated(build_content(s, env), "XX").empty());   // no table: English, nothing to check
}

TEST(translate_page) {
    Env env{true};
    env.languages = {{"FR", "Fran\xC3\xA7" "ais (French)"}, {"IT", "Italiano (Italian)"}};
    Settings s;
    const Content english = build_content(s, env);
    // English (and a language without a table) leaves the page as it is
    Content same = english;
    translate(same, "");
    translate(same, "XX");
    CHECK(same.tabs.size() == english.tabs.size() && same.title == "SETTINGS");
    for (std::size_t k = 0; k < same.tabs.size(); ++k) CHECK(same.tabs[k].label == english.tabs[k].label);
    // French: the same rows (ids, kinds, value counts), other words
    Content fr = english;
    translate(fr, "FR");
    CHECK(fr.title != "SETTINGS" && fr.footer_back != english.footer_back);
    for (std::size_t k = 0; k < fr.tabs.size(); ++k) {
        CHECK(fr.tabs[k].rows.size() == english.tabs[k].rows.size());
        for (std::size_t r = 0; r < fr.tabs[k].rows.size(); ++r) {
            const Row& a = fr.tabs[k].rows[r];
            const Row& b = english.tabs[k].rows[r];
            CHECK(a.id == b.id && a.kind == b.kind && a.value == b.value && a.values.size() == b.values.size());
        }
    }
    // a `{}` entry carries the text it stands for; an unknown sentence stays, the known ones around it change
    const std::string chosen = translate_text("Chosen: /my packs/SLUS-21050 (in use).", "FR");
    CHECK(chosen.find("/my packs/SLUS-21050") != std::string::npos && chosen.find("Chosen") == std::string::npos);
    const std::string mixed = translate_text("Smooths jagged edges. Something new here. Applies at the next start.", "FR");
    CHECK(mixed.find("Something new here.") != std::string::npos && mixed.find("Smooths") == std::string::npos &&
          mixed.find("Applies") == std::string::npos);
    // the page shows the chosen language only when it is imported
    s.language = "IT";
    CHECK(ui_language(s, env) == "IT");
    s.language = "GE";
    CHECK(ui_language(s, env).empty());
    s.language.clear();
    CHECK(ui_language(s, env).empty());
}

TEST(chase_camera_and_upscaling_rows) {
    // 1.1: CAMERA DISTANCE / HEIGHT (CAMERA tab, after FOV CORRECTION) and UPSCALING (GRAPHICS, after RENDER SCALE)
    Settings s;
    CHECK(s.camera_distance == 100 && s.camera_height == 0 && s.upscaling == 1);
    const Content c = build_content(s, Env{true});
    const Row* dist = nullptr;
    const Row* height = nullptr;
    const Row* up = nullptr;
    for (const Tab& t : c.tabs)
        for (std::size_t k = 0; k < t.rows.size(); ++k) {
            const Row& r = t.rows[k];
            if (r.label == "CAMERA DISTANCE") {
                dist = &r;
                CHECK(t.label == "CAMERA" && k > 0 && t.rows[k - 1].label == "FOV CORRECTION");
            }
            if (r.label == "CAMERA HEIGHT") height = &r;
            if (r.label == "UPSCALING") {
                up = &r;
                CHECK(k > 0 && t.rows[k - 1].label == "RENDER SCALE");
            }
        }
    CHECK(dist && dist->values[dist->value] == "Game" && height && height->values[height->value] == "Game");
    CHECK(up && up->values[up->value] == "Sharp");
    Settings e;
    CHECK(apply_edit(e, Edit{dist->id, 4, 0, false}) && e.camera_distance == 130);
    CHECK(apply_edit(e, Edit{dist->id, 0, 0, false}) && e.camera_distance == 90);
    CHECK(apply_edit(e, Edit{height->id, 4, 0, false}) && e.camera_height == 100);
    CHECK(apply_edit(e, Edit{up->id, 0, 0, false}) && e.upscaling == 0);
    const Settings back = from_ini(to_ini(e));
    CHECK(back.camera_distance == 90 && back.camera_height == 100 && back.upscaling == 0);
    CHECK(from_ini("camera_distance = 125\n").camera_distance == 100);   // not a step: the game's
    CHECK(from_ini("camera_height = 30\n").camera_height == 0);
    CHECK(from_ini("camera_distance = 200\n").camera_distance == 100);   // out of range
}

TEST(analog_triggers_row) {
    // 1.0.1 ANALOG TRIGGERS (INTERFACE, after CAR SERIES): on by default, a live toggle, kept in the ini
    Settings s;
    CHECK(s.analog_pedals);
    const Content c = build_content(s, Env{true});   // kept: `row` points into it
    const Row* row = nullptr;
    for (const Tab& t : c.tabs)
        for (const Row& r : t.rows)
            if (r.label == "ANALOG TRIGGERS") row = &r;
    CHECK(row && row->id == 272 && row->kind == RowKind::kToggle && row->value == 1);
    Settings x;
    CHECK(apply_edit(x, Edit{272, 0, 0, false}) && !x.analog_pedals);
    CHECK(apply_edit(x, Edit{272, 1, 0, false}) && x.analog_pedals);
    x.analog_pedals = false;
    CHECK(!from_ini(to_ini(x)).analog_pedals && from_ini(to_ini(Settings{})).analog_pedals);
}

TEST_MAIN("b3settings (settings1)")
