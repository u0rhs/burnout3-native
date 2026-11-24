// todrace: the DAY / NIGHT label on the track pages (racetime.h).
#include "racetime.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <vector>

#include "b3settings/page.h"   // translate_text / translate_format
#include "glyphdraw.h"
#include "pcpage.h"            // host_font_text

namespace b3replay {

std::string race_time_text(int choice, bool setting_night, const std::string& language) {
    const std::string day = b3settings::translate_text("Day", language), night = b3settings::translate_text("Night", language);
    const std::string value = choice == 1 ? day : choice == 2 ? night
                                                : b3settings::translate_format("Default ({})", setting_night ? night : day, language);
    return b3settings::translate_format("Time of day: {}", value, language);
}

bool race_time_pad_glyph(b3native::input::PadInput in, PadButton& out) {
    using P = b3native::input::PadInput;
    switch (in) {
    case P::South: out = PadButton::kCross; return true;
    case P::East: out = PadButton::kCircle; return true;
    case P::West: out = PadButton::kSquare; return true;
    case P::North: out = PadButton::kTriangle; return true;
    case P::LeftShoulder: out = PadButton::kL1; return true;
    case P::RightShoulder: out = PadButton::kR1; return true;
    case P::LeftTrigger: out = PadButton::kL2; return true;
    case P::RightTrigger: out = PadButton::kR2; return true;
    case P::LeftStick: out = PadButton::kL3; return true;
    case P::RightStick: out = PadButton::kR3; return true;
    case P::Start: out = PadButton::kStart; return true;
    case P::Back: out = PadButton::kSelect; return true;
    default: return false;
    }
}

std::shared_ptr<const HostUi> RaceTimeLabel::ui(const std::shared_ptr<const HostFonts>& fonts,
                                                const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures, int choice,
                                                std::uint8_t opacity, bool setting_night, PromptDevice d, std::uint16_t key,
                                                b3native::input::PadInput pad, const std::string& language) {
    if (opacity == 0 || !fonts || fonts->fonts.empty()) return nullptr;
    const Key k{fonts.get(), fonts->serial, choice, opacity, setting_night, d, key, pad, language};
    if (built_ && k == built_key_) return built_;
    // the button: the keyboard's key cap (the bound key, drawn as the L1 cell's), else the pad input's DualShock position
    const bool kb = d == PromptDevice::kKeyboard;
    PadButton button = PadButton::kL1;
    const bool has_glyph = kb ? key != 0 : race_time_pad_glyph(pad, button);
    if (has_glyph && (!glyph_ || built_key_.device != d || built_key_.key != key || built_key_.pad != pad)) {
        PromptKeys keys;
        if (kb) keys.scancode[std::size_t(button)] = key;
        constexpr float H = 64;   // pixels (drawn kRaceTimeH units high)
        glyph::Canvas probe(1, 1);
        const float w = paint_prompt_glyph(probe, 0, 0, H, d, button, keys);
        glyph::Canvas c(std::max(1, int(std::ceil(w))), int(H));
        paint_prompt_glyph(c, 0, 0, H, d, button, keys);
        auto im = std::make_shared<HostUi::Image>();
        im->name = "racetime-" + std::to_string(++serial_);
        im->w = c.w;
        im->h = c.h;
        im->rgba.resize(std::size_t(c.w) * std::size_t(c.h) * 4);
        c.store(im->rgba.data(), c.w, 0, 0);
        glyph_ = std::move(im);
        glyph_aspect_ = w / H;
    }
    built_key_ = k;
    // the font: the prompt bar's (GlobalFont), else the first captured one
    const HostFont* font = nullptr;
    const HudTexture* tex = nullptr;
    bool global = false;
    for (const HostFont& f : fonts->fonts) {
        if (f.glyphs.empty() || global) continue;
        for (const auto& [id, t] : textures)
            if (t && t->ok && t->raster == f.raster) {
                global = t->name == "GlobalFont";
                if (global || !font) { font = &f; tex = t.get(); }
                break;
            }
    }
    if (!font || !tex || tex->w <= 0 || tex->h <= 0) { built_ = nullptr; return nullptr; }
    auto ui = std::make_shared<HostUi>();
    ui->serial = ++serial_;
    ui->paths.push_back(std::string());
    std::uint32_t glyph_path = 0;
    if (has_glyph && glyph_) {
        ui->images.push_back(glyph_);
        glyph_path = std::uint32_t(ui->paths.size());
        ui->paths.push_back("img:" + glyph_->name);
    }
    char name[32];
    std::snprintf(name, sizeof name, "hudtex:%08x", font->raster);
    const std::uint32_t font_path = std::uint32_t(ui->paths.size());
    ui->paths.push_back(name);
    const auto add = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, std::uint32_t c, std::uint32_t path) {
        HostUiQuad q;
        const float qx[4] = {x0, x1, x1, x0}, qy[4] = {y0, y0, y1, y1}, qu[4] = {u0, u1, u1, u0}, qv[4] = {v0, v0, v1, v1};
        for (int i = 0; i < 4; ++i) { q.x[i] = qx[i]; q.y[i] = qy[i]; q.u[i] = qu[i]; q.v[i] = qv[i]; }
        q.rgba = c;
        q.path = path;
        ui->quads.push_back(q);
    };
    const float a = float(opacity) / 255.0f;
    const auto alpha = [&](float base) { return std::uint32_t(std::lround(std::clamp(base * a, 0.0f, 1.0f) * 255.0f)) << 24; };
    // the text: the font's line kRaceTimeH units high (as the PC SETTINGS page sizes it), a soft drop shadow under it
    const std::u16string s = host_font_text(*font, race_time_text(choice, setting_night, language));
    float line = 0;
    for (const HostFontGlyph& g : font->glyphs) line = std::max(line, g.oy + g.h);
    const float size = line > 0 && font->scale_y > 0 ? kRaceTimeH / (line * font->scale_y) : 0.0f;
    const float gw = has_glyph ? glyph_aspect_ * kRaceTimeGlyphH : 0.0f, gap = has_glyph ? 5.0f : 0.0f;
    const float top = kRaceTimeMid - 0.5f * kRaceTimeH;
    const float tw = font->width(s, size);
    const float x_text = kRaceTimeRight - gw - gap - tw;
    const float sx = font->scale_x * size, sy = font->scale_y * size;
    const float nu = float(tex->tw) / float(tex->w), nv = float(tex->th) / float(tex->h);
    const auto run = [&](float x, float y, std::uint32_t c) {
        if (s.empty()) return;
        const HostFontGlyph* g = font->find(s[0]);
        float pen = x - g->ox * sx;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (i > 0) {
                pen += g->advance * sx;
                g = font->find(s[i]);
            }
            if (g->u < 0.0f || g->w <= 0.0f) continue;   // a space
            const float x0 = pen + g->ox * sx, y0 = y + g->oy * sy;
            add(x0, y0, x0 + g->w * sx, y0 + g->h * sy, g->u * nu, g->v * nv, (g->u + g->w) * nu, (g->v + g->h) * nv, c, font_path);
        }
    };
    const float d_shadow = std::max(0.75f, kRaceTimeH * 0.07f);
    run(x_text + d_shadow, top + d_shadow, alpha(0.7f));
    run(x_text, top, alpha(1.0f) | kRaceTimeText);
    if (has_glyph && glyph_) {   // the button, centred on the text's line
        const float x0 = kRaceTimeRight - gw, y0 = kRaceTimeMid - 0.5f * kRaceTimeGlyphH;
        add(x0, y0, kRaceTimeRight, y0 + kRaceTimeGlyphH, 0, 0, 1, 1, alpha(1.0f) | 0x00FFFFFFu, glyph_path);
    }
    built_ = std::move(ui);
    return built_;
}

std::string elim_countdown_text(int /*seconds*/, const std::string& language) {   // elim1: the lead-in
    if (language == "FR") return "\xC3\x89LIMINATION DANS";
    if (language == "GE") return "ELIMINIERUNG IN";
    if (language == "IT") return "ELIMINAZIONE TRA";
    if (language == "SP") return "ELIMINACI\xC3\x93N EN";
    if (language == "DU") return "ELIMINATIE OVER";
    if (language == "SW") return "ELIMINERING OM";
    return "ELIMINATION IN";
}

std::shared_ptr<const HostUi> ElimCountdown::ui(const std::shared_ptr<const HostFonts>& fonts,
                                                const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures,
                                                const HostElimTimer& t, const std::string& language) {
    const int seconds = t.seconds;
    if (seconds < 0 || !fonts || fonts->fonts.empty() || fonts->fonts[0].glyphs.empty()) return nullptr;
    const std::uint32_t cols[4] = {t.gold[0], t.gold[1], t.gold[2], t.gold[3]};
    if (built_ && fonts.get() == fonts_ && fonts->serial == font_serial_ && seconds == seconds_ && language == language_ &&
        std::equal(cols, cols + 4, colours_))
        return built_;
    // the captions' font: the graphics manager's first (D_00665ED0.pFont), the captured fonts' first
    const HostFont& font = fonts->fonts[0];
    const HudTexture* tex = nullptr;
    for (const auto& [id, tx] : textures)
        if (tx && tx->ok && tx->raster == font.raster && tx->w > 0 && tx->h > 0) { tex = tx.get(); break; }
    if (!tex) return nullptr;
    fonts_ = fonts.get();
    font_serial_ = fonts->serial;
    seconds_ = seconds;
    language_ = language;
    std::copy(cols, cols + 4, colours_);
    auto ui = std::make_shared<HostUi>();
    ui->serial = ++serial_;
    ui->paths.push_back(std::string());
    char name[32];
    std::snprintf(name, sizeof name, "hudtex:%08x", font.raster);
    const std::uint32_t path = std::uint32_t(ui->paths.size());
    ui->paths.push_back(name);
    const float nu = float(tex->tw) / float(tex->w), nv = float(tex->th) / float(tex->h);
    // the caption's four-stop ramp over the object's height (0, 0.48, 0.51, 1: func_003CE290), RGBA bytes
    const auto mix = [](std::uint32_t a, std::uint32_t b, float k) {
        k = std::clamp(k, 0.0f, 1.0f);
        std::uint32_t o = 0;
        for (int j = 0; j < 4; ++j) {
            const float x = float(a >> (8 * j) & 0xFFu), y = float(b >> (8 * j) & 0xFFu);
            o |= std::uint32_t(std::lround(x + (y - x) * k)) << (8 * j);
        }
        return o;
    };
    const bool warn = seconds <= kElimWarnSeconds;
    const std::uint32_t ramp4[4] = {t.gold[0], t.gold[1], t.gold[2], t.gold[3]};
    const auto ramp = [&](bool w, float u) {
        if (w) return mix(t.c50, t.c60, u);   // style 4 (func_003CE0A0): the HUD's warning +0x50 to +0x60
        if (u < 0.48f) return mix(ramp4[0], ramp4[1], u / 0.48f);
        if (u < 0.51f) return mix(ramp4[1], ramp4[2], (u - 0.48f) / 0.03f);
        return mix(ramp4[2], ramp4[3], (u - 0.51f) / 0.49f);
    };
    // the layout (func_003CDB80): glyph boxes at sc = (1.5 x the font's x scale, its y scale) x H x the size factor, the
    // advance less 0.2 of a space's, a smaller part dropped to the baseline; x sheared by 0.4 x (H/2 - y)
    struct Glyph { const HostFontGlyph* g; float x, y, sx, sy; bool big; };
    std::vector<Glyph> glyphs;
    const HostFontGlyph* space = font.find(u' ');
    const float spadv = -0.2f * (space ? space->advance : 0.0f), H = kElimH;
    float x = 0;
    bool first = true;
    const auto lay = [&](const std::u16string& s, float f, bool big) {
        const float sx = 1.5f * font.scale_x * H * f, sy = font.scale_y * H * f;
        const float yoff = H * ((f < 0.7f ? -0.175f : 0.0f) + (1.0f - f) * font.baseline);
        for (const char16_t c : s) {
            const HostFontGlyph* g = font.find(c);
            if (first) { x = -g->ox * sx; first = false; }
            glyphs.push_back({g, x, yoff, sx, sy, big});
            x += (g->advance + spadv) * sx;
        }
    };
    lay(host_font_text(font, elim_countdown_text(seconds, language) + " "), 0.625f, false);
    lay(host_font_text(font, std::to_string(seconds)), 1.0f, true);
    const float left = kElimMid - 0.5f * x, top = kElimTop;
    // one glyph's parallelogram, cut in horizontal bands with the ramp's colour at each band's middle
    const auto quad = [&](const Glyph& q, float dx, float dy, bool shadow, std::uint32_t shadow_rgba) {
        const HostFontGlyph* g = q.g;
        if (g->u < 0.0f || g->w <= 0.0f) return;
        const float x0 = q.x + g->ox * q.sx, y0 = q.y + g->oy * q.sy, x1 = x0 + g->w * q.sx, y1 = y0 + g->h * q.sy;
        const float u0 = g->u * nu, u1 = (g->u + g->w) * nu;
        constexpr float kCuts[] = {0.0f, 0.16f, 0.32f, 0.48f, 0.51f, 0.67f, 0.84f, 1.0f};
        std::vector<float> ys{y0};
        if (!shadow)
            for (const float c : kCuts)
                if (c * H > y0 && c * H < y1) ys.push_back(c * H);
        ys.push_back(y1);
        for (std::size_t k = 0; k + 1 < ys.size(); ++k) {
            const float ya = ys[k], yb = ys[k + 1];
            const float va = (g->v + g->h * (ya - y0) / (y1 - y0)) * nv, vb = (g->v + g->h * (yb - y0) / (y1 - y0)) * nv;
            HostUiQuad o;
            const float sa = 0.4f * (0.5f * H - ya), sb = 0.4f * (0.5f * H - yb);
            const float qx[4] = {x0 + sa, x1 + sa, x1 + sb, x0 + sb}, qy[4] = {ya, ya, yb, yb};
            const float qu[4] = {u0, u1, u1, u0}, qv[4] = {va, va, vb, vb};
            for (int m = 0; m < 4; ++m) {
                o.x[m] = left + qx[m] + dx;
                o.y[m] = top + qy[m] + dy;
                o.u[m] = qu[m];
                o.v[m] = qv[m];
            }
            o.rgba = shadow ? shadow_rgba : (ramp(warn && q.big, (0.5f * (ya + yb)) / H) | 0xFF000000u);
            o.path = path;
            ui->quads.push_back(o);
        }
    };
    // the outline ring and the drop shadow (_RenderNormal): offsets 0.036 H, the ring's x sheared by -0.4 of its y
    const float oh = 0.036f * H, ow = 0.036f * H, sh = -0.4f * oh;
    for (const Glyph& q : glyphs) quad(q, ow * 2.78f, oh * 2.78f, true, 0x80000000u);
    const float ring[4][2] = {{sh + ow, oh}, {sh - ow, oh}, {-(sh + ow), -oh}, {-(sh - ow), -oh}};
    for (const auto& r : ring)
        for (const Glyph& q : glyphs) quad(q, r[0], r[1], true, 0xFF000000u);
    for (const Glyph& q : glyphs) quad(q, 0, 0, false, 0);
    built_ = std::move(ui);
    return built_;
}

std::string gear_label(const std::string& language) {   // gearbox
    if (language == "FR") return "RAPPORT";
    if (language == "GE") return "GANG";
    if (language == "IT") return "MARCIA";
    if (language == "SP") return "MARCHA";
    if (language == "DU") return "VERSNELLING";
    if (language == "SW") return "V\xC3\x84XEL";
    return "GEAR";
}
std::string gear_text(int gear) { return gear < 0 ? "R" : gear == 0 ? "N" : std::to_string(gear); }

std::shared_ptr<const HostUi> GearIndicator::ui(const std::shared_ptr<const HostFonts>& fonts,
                                                const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures,
                                                const HostGear& t, const std::string& language) {
    if (t.gear < -1 || !fonts || fonts->fonts.empty() || fonts->fonts[0].glyphs.empty()) return nullptr;
    const std::uint32_t cols[4] = {t.c30, t.c40, t.c50, t.c60};
    if (built_ && fonts.get() == fonts_ && fonts->serial == font_serial_ && t.gear == gear_ && language == language_ &&
        std::equal(cols, cols + 4, colours_))
        return built_;
    // the race HUD's font: the graphics manager's (D_00665ED0.pFont, the captured fonts' first)
    const HostFont& font = fonts->fonts[0];
    const HudTexture* tex = nullptr;
    for (const auto& [id, tx] : textures)
        if (tx && tx->ok && tx->raster == font.raster && tx->w > 0 && tx->h > 0) { tex = tx.get(); break; }
    if (!tex) return nullptr;
    float line = 0;
    for (const HostFontGlyph& g : font.glyphs) line = std::max(line, g.oy + g.h);
    if (line <= 0 || font.scale_y <= 0) return nullptr;
    fonts_ = fonts.get();
    font_serial_ = fonts->serial;
    gear_ = t.gear;
    language_ = language;
    std::copy(cols, cols + 4, colours_);
    auto ui = std::make_shared<HostUi>();
    ui->serial = ++serial_;
    ui->paths.push_back(std::string());
    char name[32];
    std::snprintf(name, sizeof name, "hudtex:%08x", font.raster);
    const std::uint32_t path = std::uint32_t(ui->paths.size());
    ui->paths.push_back(name);
    const float nu = float(tex->tw) / float(tex->w), nv = float(tex->th) / float(tex->h);
    const auto mix = [](std::uint32_t a, std::uint32_t b, float k) {
        k = std::clamp(k, 0.0f, 1.0f);
        std::uint32_t o = 0;
        for (int j = 0; j < 4; ++j) {
            const float x = float(a >> (8 * j) & 0xFFu), y = float(b >> (8 * j) & 0xFFu);
            o |= std::uint32_t(std::lround(x + (y - x) * k)) << (8 * j);
        }
        return o;
    };
    // one text right-aligned at `right`, `h` units high (the object's), `wide` its widths' factor, its colour from `top` to
    // `bottom` over its height in bands, the four-way black outline first; returns its left edge
    const auto text = [&](const std::u16string& s, float h, float wide, float right, float y, std::uint32_t top, std::uint32_t bottom) {
        const float size = h / (line * font.scale_y), sx = font.scale_x * size * wide, sy = font.scale_y * size;
        float tw = 0;
        for (const char16_t c : s) tw += font.find(c)->advance * sx;
        const float x = right - tw;
        const auto run = [&](float dx, float dy, bool shadow) {
            float pen = x + dx;
            constexpr int kBands = 6;
            for (const char16_t c : s) {
                const HostFontGlyph* g = font.find(c);
                if (g->u >= 0.0f && g->w > 0.0f) {
                    const float x0 = pen + g->ox * sx, x1 = x0 + g->w * sx, y0 = y + dy + g->oy * sy, gh = g->h * sy;
                    const int n = shadow ? 1 : kBands;
                    for (int k = 0; k < n; ++k) {
                        const float a = float(k) / float(n), b = float(k + 1) / float(n);
                        const float ya = y0 + gh * a, yb = y0 + gh * b;
                        HostUiQuad q;
                        const float qx[4] = {x0, x1, x1, x0}, qy[4] = {ya, ya, yb, yb};
                        const float u0 = g->u * nu, u1 = (g->u + g->w) * nu, va = (g->v + g->h * a) * nv, vb = (g->v + g->h * b) * nv;
                        const float qu[4] = {u0, u1, u1, u0}, qv[4] = {va, va, vb, vb};
                        for (int m = 0; m < 4; ++m) { q.x[m] = qx[m]; q.y[m] = qy[m]; q.u[m] = qu[m]; q.v[m] = qv[m]; }
                        q.rgba = shadow ? 0xFF000000u : (mix(top, bottom, ((ya + yb) * 0.5f - y) / h) | 0xFF000000u);
                        q.path = path;
                        ui->quads.push_back(q);
                    }
                }
                pen += g->advance * sx;
            }
        };
        const float o = std::max(1.0f, h * 0.035f);
        run(o, o, true);
        run(-o, -o, true);
        run(-o, o, true);
        run(o, -o, true);
        run(0, 0, false);
        return x;
    };
    const float left = text(host_font_text(font, gear_text(t.gear)), 56.0f, 1.5f, kGearRight, kGearTop, t.c60, mix(t.c60, t.c30, 0.75f));
    text(host_font_text(font, gear_label(language)), 27.0f, 1.0f, left - 4.0f, kGearTop + 4.0f, t.c40, t.c40);
    built_ = std::move(ui);
    return built_;
}

// options3: DRIVER LABELS (racetime.h; the layout in driverlabels.h)
std::shared_ptr<const HostUi> DriverLabels::ui(const std::shared_ptr<const HostFonts>& fonts,
                                               const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures,
                                               const std::vector<DriverLabel>& labels, int mode, int speed_units) {
    if (mode <= kDriverLabelsOff || mode > kDriverLabelsDistance || labels.empty() || !fonts || fonts->fonts.empty()) return nullptr;
    // the font: the prompt bar's (GlobalFont), else the first captured one (as the DAY / NIGHT label)
    const HostFont* font = nullptr;
    const HudTexture* tex = nullptr;
    bool global = false;
    for (const HostFont& f : fonts->fonts) {
        if (f.glyphs.empty() || global) continue;
        for (const auto& [id, t] : textures)
            if (t && t->ok && t->raster == f.raster) {
                global = t->name == "GlobalFont";
                if (global || !font) { font = &f; tex = t.get(); }
                break;
            }
    }
    if (!font || !tex || tex->w <= 0 || tex->h <= 0) return nullptr;
    auto ui = std::make_shared<HostUi>();
    ui->serial = ++serial_;
    ui->paths.push_back(std::string());
    char path[32];
    std::snprintf(path, sizeof path, "hudtex:%08x", font->raster);
    ui->paths.push_back(path);
    float line = 0;
    for (const HostFontGlyph& g : font->glyphs) line = std::max(line, g.oy + g.h);
    const float size = line > 0 && font->scale_y > 0 ? kDriverLabelH / (line * font->scale_y) : 0.0f;
    const float sx = font->scale_x * size, sy = font->scale_y * size;
    const float nu = float(tex->tw) / float(tex->w), nv = float(tex->th) / float(tex->h);
    const float shadow = std::max(0.75f, kDriverLabelH * 0.08f);
    const auto run = [&](const std::u16string& s, float x, float y, std::uint32_t c) {
        if (s.empty()) return;
        const HostFontGlyph* g = font->find(s[0]);
        float pen = x - g->ox * sx;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (i > 0) {
                pen += g->advance * sx;
                g = font->find(s[i]);
            }
            if (g->u < 0.0f || g->w <= 0.0f) continue;   // a space
            const float x0 = pen + g->ox * sx, y0 = y + g->oy * sy, x1 = x0 + g->w * sx, y1 = y0 + g->h * sy;
            const float u0 = g->u * nu, v0 = g->v * nv, u1 = (g->u + g->w) * nu, v1 = (g->v + g->h) * nv;
            HostUiQuad q;
            const float qx[4] = {x0, x1, x1, x0}, qy[4] = {y0, y0, y1, y1}, qu[4] = {u0, u1, u1, u0}, qv[4] = {v0, v0, v1, v1};
            for (int k = 0; k < 4; ++k) { q.x[k] = qx[k]; q.y[k] = qy[k]; q.u[k] = qu[k]; q.v[k] = qv[k]; }
            q.rgba = c;
            q.path = 1;
            q.cover = 4;   // over the wide picture's sides too
            ui->quads.push_back(q);
        }
    };
    // the texts and their boxes; the nearest (the list's last) placed first, a label over a placed one left out
    struct Placed { std::u16string s; float x, y, w; const DriverLabel* l; };
    std::vector<Placed> placed;
    for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
        const DriverLabel& l = *it;
        std::u16string s;
        if (mode != kDriverLabelsDistance) s = l.name;
        if (mode != kDriverLabelsNames && l.metres >= 0) {
            if (!s.empty()) s += u"  ";
            for (const char c : driver_distance_text(l.metres, speed_units)) s.push_back(char16_t(c));
        }
        if (s.empty()) continue;
        const float tw = font->width(s, size), x = l.x - 0.5f * tw, y = l.y - kDriverLabelH;
        bool covers = false;
        for (const Placed& p : placed)
            if (x < p.x + p.w + 2.0f && p.x < x + tw + 2.0f && y < p.y + kDriverLabelH && p.y < y + kDriverLabelH) { covers = true; break; }
        if (!covers) placed.push_back({std::move(s), x, y, tw, &l});
    }
    for (auto it = placed.rbegin(); it != placed.rend(); ++it) {
        const DriverLabel& l = *it->l;
        const std::u16string& s = it->s;
        const float x = it->x, y = it->y;
        const auto a = [&](float base) { return std::uint32_t(std::lround(std::clamp(base * l.alpha, 0.0f, 1.0f) * 255.0f)) << 24; };
        run(s, x + shadow, y + shadow, a(0.75f));
        run(s, x, y, a(1.0f) | 0x00FFFFFFu);
    }
    if (ui->quads.empty()) return nullptr;
    return ui;
}

// replayview: the REPLAY prompt (racetime.h ReplayPrompt), built as the DAY / NIGHT label
std::shared_ptr<const HostUi> ReplayPrompt::ui(const std::shared_ptr<const HostFonts>& fonts,
                                               const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures, bool shown,
                                               PromptDevice d, std::uint16_t key, const std::string& language) {
    if (!shown || !fonts || fonts->fonts.empty()) return nullptr;
    if (built_ && fonts.get() == fonts_ && fonts->serial == font_serial_ && d == device_ && key == key_ && language == language_)
        return built_;
    const bool kb = d == PromptDevice::kKeyboard;
    if (!glyph_ || d != device_ || key != key_) {
        PromptKeys keys;
        if (kb) keys.scancode[std::size_t(PadButton::kSquare)] = key;
        constexpr float H = 64;
        glyph::Canvas probe(1, 1);
        const float w = paint_prompt_glyph(probe, 0, 0, H, d, PadButton::kSquare, keys);
        glyph::Canvas c(std::max(1, int(std::ceil(w))), int(H));
        paint_prompt_glyph(c, 0, 0, H, d, PadButton::kSquare, keys);
        auto im = std::make_shared<HostUi::Image>();
        im->name = "replayprompt-" + std::to_string(++serial_);
        im->w = c.w;
        im->h = c.h;
        im->rgba.resize(std::size_t(c.w) * std::size_t(c.h) * 4);
        c.store(im->rgba.data(), c.w, 0, 0);
        glyph_ = std::move(im);
        glyph_aspect_ = w / H;
    }
    fonts_ = fonts.get(), font_serial_ = fonts->serial, device_ = d, key_ = key, language_ = language;
    const HostFont* font = nullptr;
    const HudTexture* tex = nullptr;
    bool global = false;
    for (const HostFont& f : fonts->fonts) {
        if (f.glyphs.empty() || global) continue;
        for (const auto& [id, t] : textures)
            if (t && t->ok && t->raster == f.raster) {
                global = t->name == "GlobalFont";
                if (global || !font) { font = &f; tex = t.get(); }
                break;
            }
    }
    if (!font || !tex || tex->w <= 0 || tex->h <= 0) { built_ = nullptr; return nullptr; }
    auto ui = std::make_shared<HostUi>();
    ui->serial = ++serial_;
    ui->paths.push_back(std::string());
    ui->images.push_back(glyph_);
    const std::uint32_t glyph_path = std::uint32_t(ui->paths.size());
    ui->paths.push_back("img:" + glyph_->name);
    char name[32];
    std::snprintf(name, sizeof name, "hudtex:%08x", font->raster);
    const std::uint32_t font_path = std::uint32_t(ui->paths.size());
    ui->paths.push_back(name);
    const auto add = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, std::uint32_t c, std::uint32_t path) {
        HostUiQuad q;
        const float qx[4] = {x0, x1, x1, x0}, qy[4] = {y0, y0, y1, y1}, qu[4] = {u0, u1, u1, u0}, qv[4] = {v0, v0, v1, v1};
        for (int i = 0; i < 4; ++i) { q.x[i] = qx[i]; q.y[i] = qy[i]; q.u[i] = qu[i]; q.v[i] = qv[i]; }
        q.rgba = c;
        q.path = path;
        ui->quads.push_back(q);
    };
    // the glyph first, then the text after it (as the bar's own prompts: "Continue" then its button sits the other way,
    // the game's order is text then glyph: "Replay" then the button, ending at kReplayPromptRight)
    const std::u16string s = host_font_text(*font, b3settings::translate_text("Replay", language));
    float line = 0;
    for (const HostFontGlyph& g : font->glyphs) line = std::max(line, g.oy + g.h);
    const float size = line > 0 && font->scale_y > 0 ? kReplayPromptH / (line * font->scale_y) : 0.0f;
    const float gw = glyph_aspect_ * kReplayPromptGlyphH, gap = 5.0f;
    const float tw = font->width(s, size), x_text = kReplayPromptRight - gw - gap - tw, top = kReplayPromptMid - 0.5f * kReplayPromptH;
    const float sx = font->scale_x * size, sy = font->scale_y * size;
    const float nu = float(tex->tw) / float(tex->w), nv = float(tex->th) / float(tex->h);
    if (!s.empty()) {
        const HostFontGlyph* g = font->find(s[0]);
        float pen = x_text - g->ox * sx;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (i > 0) { pen += g->advance * sx; g = font->find(s[i]); }
            if (g->u < 0.0f || g->w <= 0.0f) continue;
            const float x0 = pen + g->ox * sx, y0 = top + g->oy * sy;
            add(x0, y0, x0 + g->w * sx, y0 + g->h * sy, g->u * nu, g->v * nv, (g->u + g->w) * nu, (g->v + g->h) * nv,
                0xFF000000u | kRaceTimeText, font_path);
        }
    }
    const float x0 = kReplayPromptRight - gw, y0 = kReplayPromptMid - 0.5f * kReplayPromptGlyphH;
    add(x0, y0, kReplayPromptRight, y0 + kReplayPromptGlyphH, 0, 0, 1, 1, 0xFFFFFFFFu, glyph_path);
    built_ = std::move(ui);
    return built_;
}

}  // namespace b3replay
