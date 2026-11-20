// todrace: the DAY / NIGHT label on the track pages (racetime.h).
#include "racetime.h"

#include <algorithm>
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

}  // namespace b3replay
