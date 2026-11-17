// carlookpanel.cpp -- see carlookpanel.h (reflect2).
#include "carlookpanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "carlook.h"
#include "glyphdraw.h"
#include "nativepad.h"

namespace b3replay {

namespace {
// the DS2 report: active-low buttons (pcpage.cpp's order)
bool pressed(const std::array<std::uint8_t, 32>& r, unsigned i) { return (r[i / 8] & (1u << (i % 8))) == 0; }
enum : unsigned { kSelect = 0, kStart = 3, kUp = 4, kRight = 5, kDown = 6, kLeft = 7, kL2 = 8, kR2 = 9, kTriangle = 12, kCircle = 13,
                  kCross = 14, kSquare = 15 };

std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", double(v));
    return b;
}
}  // namespace

CarLookPanel& CarLookPanel::instance() {
    static CarLookPanel p;
    return p;
}

void CarLookPanel::set_enabled(bool on) {
    std::lock_guard<std::mutex> l(mu_);
    enabled_ = on;
    if (!on) open_ = false;
}

bool CarLookPanel::Hold::step(bool now, float dt) {
    if (!now) { held = false; return false; }
    if (!held) { held = true; time = 0; next = 0.35f; return true; }
    time += dt;
    if (time < next) return false;
    next = time + 0.07f;
    return true;
}

// ---- actions ----
void CarLookPanel::move(int dir) {
    std::lock_guard<std::mutex> l(mu_);
    const int n = int(carlook::params().size());
    cursor_ = ((cursor_ + dir) % n + n) % n;
}
void CarLookPanel::change_locked(int dir, int mult) {
    const carlook::Param& p = carlook::params()[std::size_t(cursor_)];
    b3gfx::CarLook l = carlook::current();
    const float m = mult == 10 ? 10.0f : mult == -10 ? 0.1f : 1.0f;
    float v = l.*(p.field) + float(dir) * p.step * m;
    v = std::round(v * 1000.0f) / 1000.0f;
    l.*(p.field) = std::clamp(v, p.lo, p.hi);
    carlook::set_current(l);
    status_.clear();
}
void CarLookPanel::change(int dir, int mult) { std::lock_guard<std::mutex> l(mu_); change_locked(dir, mult); }
void CarLookPanel::reset_value_locked() {
    const carlook::Param& p = carlook::params()[std::size_t(cursor_)];
    b3gfx::CarLook l = carlook::current();
    l.*(p.field) = carlook::defaults().*(p.field);
    carlook::set_current(l);
    status_ = std::string(p.label) + " BACK TO ITS DEFAULT";
}
void CarLookPanel::reset_value() { std::lock_guard<std::mutex> l(mu_); reset_value_locked(); }
void CarLookPanel::reset_all_locked() {
    carlook::set_current(carlook::defaults());
    status_ = "EVERY VALUE BACK TO ITS DEFAULT";
}
void CarLookPanel::reset_all() { std::lock_guard<std::mutex> l(mu_); reset_all_locked(); }

std::string CarLookPanel::save_locked() {
    std::string why;
    const std::string path = carlook::ini_path();
    if (carlook::save_file(path, &why)) status_ = "SAVED car_look.ini";
    else status_ = "NOT SAVED: " + why;
    std::printf("[b3carlook] %s%s\n", status_.c_str(), path.empty() ? "" : (" (" + path + ")").c_str());
    std::fflush(stdout);
    return status_;
}
std::string CarLookPanel::save() { std::lock_guard<std::mutex> l(mu_); return save_locked(); }

std::string CarLookPanel::copy_locked() {
    const b3gfx::CarLook cur = carlook::current();
    const std::string text = carlook::values_text(cur);
    std::printf("[b3carlook] values as code:\n%s", text.c_str());
    std::fflush(stdout);
    status_ = "VALUES IN THE LOG AND THE CLIPBOARD";
    const std::string path = carlook::values_path();
    if (!path.empty()) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (f) { f << text; status_ = "WROTE car_look_values.txt, AND THE CLIPBOARD"; }
    }
    SDL_SetClipboardText(text.c_str());
    return status_;
}
std::string CarLookPanel::copy_as_code() { std::lock_guard<std::mutex> l(mu_); return copy_locked(); }
std::string CarLookPanel::status() { std::lock_guard<std::mutex> l(mu_); return status_; }

std::vector<CarLookPanel::Line> CarLookPanel::lines_locked() {
    const b3gfx::CarLook cur = carlook::current();
    std::vector<Line> out;
    Line head;
    head.heading = true;
    head.label = "CAR LOOK";
    out.push_back(head);
    int i = 0;
    for (const carlook::Param& p : carlook::params()) {
        Line l;
        l.label = p.label;
        l.value = fmt(cur.*(p.field));
        l.def = fmt(carlook::defaults().*(p.field));
        l.changed = !carlook::at_default(cur, p);
        l.cursor = i == cursor_;
        out.push_back(l);
        ++i;
    }
    return out;
}
std::vector<CarLookPanel::Line> CarLookPanel::lines() { std::lock_guard<std::mutex> l(mu_); return lines_locked(); }

// ---- input ----
bool CarLookPanel::handle(const SDL_Event& e) {
    std::lock_guard<std::mutex> l(mu_);
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        if (!open_) return false;
        const int n = int(carlook::params().size());
        cursor_ = ((cursor_ + (e.wheel.y > 0 ? -1 : 1)) % n + n) % n;
        return true;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (!open_ || !geo_.valid || e.button.x < geo_.x || e.button.x > geo_.x + geo_.w || e.button.y < geo_.y || e.button.y > geo_.y + geo_.h) return false;
        const int row = int(std::floor((e.button.y - geo_.list_y) / geo_.pitch));
        if (row >= 0 && row < int(carlook::params().size())) cursor_ = row;
        return true;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return false;
    const SDL_Keycode k = e.key.key;
    const bool toggle = e.key.scancode == SDL_SCANCODE_UNKNOWN
                            ? k == SDLK_F8
                            : (NativePad::instance().port_mask(std::uint16_t(e.key.scancode), e.key.mod) >> int(b3native::input::PortAction::CarLook) & 1) != 0;
    if (toggle && !e.key.repeat) {
        if (!enabled_) return false;
        open_ = !open_;
        pad_seen_ = false;
        std::printf("[b3carlook] the CAR LOOK panel %s\n", open_ ? "opened" : "closed");
        std::fflush(stdout);
        return true;
    }
    if (!open_) return false;
    const bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0, ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0, alt = (e.key.mod & SDL_KMOD_ALT) != 0;
    const int mult = shift ? 10 : (ctrl || alt) ? -10 : 1;
    const int n = int(carlook::params().size());
    switch (k) {
    case SDLK_UP: cursor_ = (cursor_ + n - 1) % n; break;
    case SDLK_DOWN: cursor_ = (cursor_ + 1) % n; break;
    case SDLK_PAGEUP: cursor_ = std::max(0, cursor_ - 6); break;
    case SDLK_PAGEDOWN: cursor_ = std::min(n - 1, cursor_ + 6); break;
    case SDLK_LEFT: change_locked(-1, mult); break;
    case SDLK_RIGHT: change_locked(+1, mult); break;
    case SDLK_HOME: reset_value_locked(); break;
    case SDLK_R: if (shift || ctrl) reset_all_locked(); else reset_value_locked(); break;
    case SDLK_S: if (!ctrl && !alt) save_locked(); break;
    case SDLK_C: if (!ctrl && !alt) copy_locked(); break;
    case SDLK_ESCAPE: open_ = false; break;
    default: break;   // (modifiers and other keys: taken while open, so they never reach the race)
    }
    return true;
}

void CarLookPanel::pad(const std::array<std::uint8_t, 32>& report) {
    std::lock_guard<std::mutex> l(mu_);
    const std::uint64_t now = SDL_GetTicksNS();
    const float dt = pad_ns_ ? std::clamp(float(double(now - pad_ns_) * 1e-9), 0.0f, 0.1f) : 0.0f;
    pad_ns_ = now;
    if (!open_) { pad_seen_ = false; return; }
    if (!pad_seen_) {
        last_pad_ = report;
        pad_seen_ = true;
        for (Hold& h : hold_) h = Hold{true, 0, 1e9f};
        return;
    }
    auto edge = [&](unsigned b) { return pressed(report, b) && !pressed(last_pad_, b); };
    const bool cross = edge(kCross), circle = edge(kCircle), tri = edge(kTriangle), square = edge(kSquare), start = edge(kStart), select = edge(kSelect);
    const bool up = hold_[0].step(pressed(report, kUp), dt), down = hold_[1].step(pressed(report, kDown), dt);
    const bool left = hold_[2].step(pressed(report, kLeft), dt), right = hold_[3].step(pressed(report, kRight), dt);
    const int mult = pressed(report, kR2) ? 10 : pressed(report, kL2) ? -10 : 1;
    last_pad_ = report;
    const int n = int(carlook::params().size());
    if (start || circle) { open_ = false; return; }
    if (up) cursor_ = (cursor_ + n - 1) % n;
    if (down) cursor_ = (cursor_ + 1) % n;
    if (left) change_locked(-1, mult);
    if (right) change_locked(+1, mult);
    if (square) reset_value_locked();
    if (select) reset_all_locked();
    if (cross) save_locked();
    if (tri) copy_locked();
}

// ---- drawing ----
void CarLookPanel::draw_list(b3gfx::Device& dev, int w, int h, std::vector<b3gfx::DrawItem>& out) {
    if (epoch_.changed(dev)) tex_ = vb_ = ib_ = b3gfx::kNone;   // a track load released every id (the game's own took them): make them again
    std::vector<Line> ls;
    std::string status, help, keys;
    int cursor = 0;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (!open_ || w <= 0 || h <= 0) return;
        ls = lines_locked();
        status = status_;
        cursor = cursor_;
    }
    help = carlook::params()[std::size_t(cursor)].help;
    const std::string key = NativePad::instance().port_key_text(b3native::input::PortAction::CarLook);
    keys = "UP/DOWN CHOOSE  LEFT/RIGHT CHANGE (SHIFT X10, CTRL /10)  R RESET  SHIFT+R ALL  S SAVE  C COPY AS CODE  " + key + " CLOSE";
    const float cap = std::max(9.0f, std::round(float(h) * 0.021f)), st = std::max(1.1f, cap * 0.13f), gap = cap * 0.55f, pad = cap * 1.0f, pitch = cap + gap;
    // the width: the longest label, the value and default columns, or the wrapped help / keys at a fixed share of the window
    float label_w = 0;
    for (const Line& l : ls) label_w = std::max(label_w, glyph::text_width(l.label, cap, st));
    const float vw = glyph::text_width("-0.00", cap, st);
    const float W = std::min(float(w) * 0.5f, pad * 2 + 0.9f * cap + label_w + cap + 2.0f * vw + 1.4f * cap);
    auto wrap = [&](const std::string& t, std::vector<std::string>& o) {
        std::string line, word;
        auto flush = [&]() { if (!line.empty()) { o.push_back(line); line.clear(); } };
        for (std::size_t i = 0; i <= t.size(); ++i) {
            if (i == t.size() || t[i] == ' ') {
                if (!word.empty()) {
                    const std::string trial = line.empty() ? word : line + " " + word;
                    if (glyph::text_width(trial, cap * 0.85f, st * 0.85f) > W - 2 * pad) { flush(); line = word; }
                    else line = trial;
                    word.clear();
                }
            } else word += t[i];
        }
        flush();
    };
    std::vector<std::string> help_l, key_l, stat_l;
    wrap(help, help_l);
    wrap(keys, key_l);
    if (!status.empty()) wrap(status, stat_l);
    const int rows = int(ls.size()) + 1 + int(help_l.size()) + int(key_l.size()) + (stat_l.empty() ? 0 : int(stat_l.size()) + 1);
    const float H = pad * 2 + float(rows) * pitch * 0.92f;
    tex_w_ = std::max(16, int(std::ceil(W)));
    tex_h_ = std::max(16, int(std::ceil(H)));
    std::string text = std::to_string(w) + "x" + std::to_string(h) + "\n";
    for (const Line& l : ls) text += (l.cursor ? "1" : "0") + std::string(l.changed ? "1" : "0") + l.label + "\t" + l.value + "\t" + l.def + "\n";
    text += help + "\n" + status + "\n" + keys;
    if (text != built_ || tex_ == b3gfx::kNone) {
        glyph::Canvas c(tex_w_, tex_h_);
        const float bg[4] = {0.025f, 0.03f, 0.045f, 0.94f}, edge[4] = {0.30f, 0.62f, 0.95f, 0.95f};
        const float headc[4] = {0.55f, 0.80f, 1.0f, 1.0f}, bodyc[4] = {0.95f, 0.95f, 0.95f, 1.0f}, dimc[4] = {0.66f, 0.70f, 0.76f, 1.0f},
                    cur[4] = {1.0f, 0.86f, 0.30f, 1.0f}, ed[4] = {0.50f, 1.0f, 0.55f, 1.0f}, bar[4] = {1.0f, 0.82f, 0.25f, 0.24f};
        const float Wf = float(tex_w_), Hf = float(tex_h_), s = cap / 12.0f;
        c.box(Wf * 0.5f, Hf * 0.5f, Wf * 0.5f, Hf * 0.5f, 6 * s, bg);
        c.box_outline(Wf * 0.5f, Hf * 0.5f, Wf * 0.5f - s, Hf * 0.5f - s, 5 * s, std::max(1.0f, 1.2f * s), edge);
        float y = pad;
        auto put = [&](const std::string& t, float x, const float (&col_)[4], float k = 1.0f) {
            std::vector<glyph::Line> pl;
            glyph::text(t, x, y, cap * k, st * k, pl);
            c.strokes(pl, st * k, col_);
        };
        auto put_right = [&](const std::string& t, float right, const float (&col_)[4]) { put(t, right - glyph::text_width(t, cap, st), col_); };
        const float def_right = Wf - pad, val_right = def_right - vw - 1.4f * cap;
        for (const Line& l : ls) {
            if (l.heading) {
                put(l.label, pad, headc);
                put_right("DEFAULT", def_right, dimc);
            } else {
                if (l.cursor) {
                    c.box(Wf * 0.5f, y + cap * 0.5f, Wf * 0.5f - 3 * s, cap * 0.5f + gap * 0.38f, 2 * s, bar);
                    c.box(pad * 0.5f, y + cap * 0.5f, 1.5f * s, cap * 0.5f + gap * 0.3f, 1 * s, cur);
                }
                const float(&cc)[4] = l.cursor ? cur : l.changed ? ed : bodyc;
                if (l.changed) put("*", pad * 0.4f, ed);
                put(l.label, pad + 0.9f * cap, cc);
                put_right(l.value, val_right, cc);
                put_right(l.def, def_right, l.cursor ? cur : dimc);
            }
            y += pitch * 0.92f;
        }
        y += pitch * 0.45f;
        for (const std::string& t : help_l) { put(t, pad, headc, 0.85f); y += pitch * 0.92f; }
        if (!stat_l.empty()) {
            for (const std::string& t : stat_l) { put(t, pad, cur, 0.85f); y += pitch * 0.92f; }
            y += pitch * 0.3f;
        }
        for (const std::string& t : key_l) { put(t, pad, dimc, 0.85f); y += pitch * 0.92f; }
        std::vector<std::uint8_t> px(std::size_t(tex_w_) * std::size_t(tex_h_) * 4, 0);
        c.store(px.data(), tex_w_, 0, 0);
        dev.update_texture(tex_, tex_w_, tex_h_, px.data());
        built_ = text;
    }
    const float x1 = std::round(float(w) * 0.985f), x0 = x1 - float(tex_w_), y0 = std::round(0.10f * float(h)), y1 = y0 + float(tex_h_);
    {
        std::lock_guard<std::mutex> l(mu_);
        geo_.valid = true;
        geo_.x = x0;
        geo_.y = y0;
        geo_.w = float(tex_w_);
        geo_.h = float(tex_h_);
        geo_.pitch = pitch * 0.92f;
        geo_.list_y = y0 + pad + pitch * 0.92f;   // (the heading line first)
    }
    const std::uint32_t rgba = 64u | 64u << 8 | 64u << 16 | 128u << 24;
    const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1}, us[4] = {0, 1, 1, 0}, vv[4] = {0, 0, 1, 1};
    b3gfx::Vertex vs[4];
    for (int k = 0; k < 4; ++k) {
        vs[k] = b3gfx::Vertex{};
        vs[k].pos[0] = xs[k];
        vs[k].pos[1] = ys[k];
        vs[k].uv[0] = us[k];
        vs[k].uv[1] = vv[k];
        vs[k].rgba = rgba;
    }
    const std::uint32_t is[6] = {0, 1, 2, 0, 2, 3};
    dev.update_vertex_buffer(vb_, vs, 4);
    dev.update_index_buffer(ib_, is, 6);
    b3gfx::DrawItem it;
    it.overlay = true;
    it.shading = b3gfx::Shading::kTrack;
    it.track_type = 0;
    it.textured = true;
    it.texture = tex_;
    it.gs_alpha = 0x44;
    it.overlay_blend = true;
    it.point_sample = true;
    const float mm[16] = {2.0f / float(w), 0, 0, 0, 0, -2.0f / float(h), 0, 0, 0, 0, 1, 0, -1, 1, 0.5f, 1};
    std::memcpy(it.model, mm, sizeof mm);
    it.vertices = vb_;
    it.indices = ib_;
    it.first_index = 0;
    it.index_count = 6;
    out.push_back(it);
}

}  // namespace b3replay
