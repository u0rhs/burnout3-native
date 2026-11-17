// font.cpp -- see font.h
#include "font.h"

#include "gameart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "third_party/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace b3launcher {

namespace fs = std::filesystem;

namespace {

// The codepoints baked: ASCII, Latin-1 (paths, names), Latin Extended-A, Greek and Cyrillic (the Music page's song
// tags and file names) and a few marks
std::vector<int> codepoints() {
    std::vector<int> c;
    for (int k = 32; k < 127; ++k) c.push_back(k);
    for (int k = 160; k < 0x180; ++k) c.push_back(k);
    for (int k = 0x391; k < 0x3CA; ++k) c.push_back(k);
    for (int k = 0x400; k < 0x460; ++k) c.push_back(k);
    for (int k : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193, 0x00D7})
        c.push_back(k);
    std::sort(c.begin(), c.end());
    c.erase(std::unique(c.begin(), c.end()), c.end());
    return c;
}

// One codepoint from UTF-8 (a bad byte: '?')
int next_cp(std::string_view s, std::size_t& i) {
    const unsigned char b = static_cast<unsigned char>(s[i++]);
    if (b < 0x80) return b;
    int n = 0, cp = 0;
    if ((b & 0xE0) == 0xC0) { n = 1; cp = b & 0x1F; }
    else if ((b & 0xF0) == 0xE0) { n = 2; cp = b & 0x0F; }
    else if ((b & 0xF8) == 0xF0) { n = 3; cp = b & 0x07; }
    else return '?';
    for (int k = 0; k < n; ++k) {
        if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) return '?';
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    }
    return cp;
}

bool read_file(const std::string& p, std::vector<unsigned char>& out) {
    std::ifstream f(fs::path(std::u8string(p.begin(), p.end())), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return out.size() > 1024;
}

std::string env(const char* n) {
    const char* v = std::getenv(n);
    return v ? v : "";
}

}  // namespace

std::vector<std::pair<std::string, std::string>> font_candidates() {
    std::vector<std::pair<std::string, std::string>> c;
    if (!env("B3LAUNCHER_FONT").empty()) c.push_back({env("B3LAUNCHER_FONT"), env("B3LAUNCHER_FONT_BOLD")});
#if defined(_WIN32)
    std::string win = env("WINDIR");
    if (win.empty()) win = "C:\\Windows";
    c.push_back({win + "\\Fonts\\segoeui.ttf", win + "\\Fonts\\segoeuib.ttf"});   // mac5: Bold for the slanted labels
    c.push_back({win + "\\Fonts\\segoeui.ttf", win + "\\Fonts\\seguisb.ttf"});
    c.push_back({win + "\\Fonts\\arial.ttf", win + "\\Fonts\\arialbd.ttf"});
    c.push_back({win + "\\Fonts\\tahoma.ttf", win + "\\Fonts\\tahomabd.ttf"});
#elif defined(__APPLE__)
    c.push_back({"/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf"});
    c.push_back({"/Library/Fonts/Arial.ttf", "/Library/Fonts/Arial Bold.ttf"});
    c.push_back({"/System/Library/Fonts/Supplemental/Verdana.ttf", "/System/Library/Fonts/Supplemental/Verdana Bold.ttf"});
#else
    // the common families, wherever the distribution keeps them (searched below the font folders)
    static const std::pair<const char*, const char*> names[] = {
        {"NotoSans-Regular.ttf", "NotoSans-SemiBold.ttf"}, {"NotoSans-Regular.ttf", "NotoSans-Bold.ttf"},
        {"DejaVuSans.ttf", "DejaVuSans-Bold.ttf"},         {"LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf"},
        {"Ubuntu-R.ttf", "Ubuntu-B.ttf"},                  {"FreeSans.ttf", "FreeSansBold.ttf"},
        {"OpenSans-Regular.ttf", "OpenSans-SemiBold.ttf"}, {"Cantarell-Regular.ttf", "Cantarell-Bold.ttf"},
    };
    std::vector<fs::path> roots = {"/usr/share/fonts", "/usr/local/share/fonts"};
    if (!env("HOME").empty()) {
        roots.push_back(fs::path(env("HOME")) / ".local/share/fonts");
        roots.push_back(fs::path(env("HOME")) / ".fonts");
    }
    std::map<std::string, std::string> found;   // file name -> path
    for (const fs::path& r : roots) {
        std::error_code ec;
        std::size_t seen = 0;
        for (fs::recursive_directory_iterator it(r, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end && ++seen < 20000; it.increment(ec)) {
            const std::string n = it->path().filename().string();
            for (const auto& [a, b] : names)
                if ((n == a || n == b) && !found.count(n)) found[n] = it->path().string();
        }
    }
    for (const auto& [a, b] : names)
        if (found.count(a)) c.push_back({found[a], found.count(b) ? found[b] : std::string()});
    c.push_back({"/mnt/c/Windows/Fonts/segoeui.ttf", "/mnt/c/Windows/Fonts/seguisb.ttf"});   // WSL
#endif
    return c;
}

struct Text::FaceInfo {
    stbtt_fontinfo info{};
    float ascent = 0, descent = 0;   // per em unit (ascent > 0, descent < 0)
};

struct Text::Baked {
    SDL_Texture* tex = nullptr;
    int aw = 0, ah = 0;
    float px = 0;                    // pixels per em
    std::vector<stbtt_packedchar> chars;
    std::vector<int> cps;
    const FaceInfo* face = nullptr;
    int index(int cp) const {
        const auto it = std::lower_bound(cps.begin(), cps.end(), cp);
        if (it != cps.end() && *it == cp) return int(it - cps.begin());
        const auto q = std::lower_bound(cps.begin(), cps.end(), int('?'));
        return int(q - cps.begin());
    }
    ~Baked() {
        if (tex) SDL_DestroyTexture(tex);
    }
};

Text::Text(SDL_Renderer* r) : r_(r) {}
Text::~Text() = default;

bool Text::load() {
    for (const auto& [reg, bold] : font_candidates()) {
        std::vector<unsigned char> a, b;
        if (!read_file(reg, a)) continue;
        auto fr = std::make_unique<FaceInfo>();
        if (!stbtt_InitFont(&fr->info, a.data(), stbtt_GetFontOffsetForIndex(a.data(), 0))) continue;
        regular_ = std::move(a);
        // InitFont keeps a pointer into the data: initialise again on the moved buffer
        stbtt_InitFont(&fr->info, regular_.data(), stbtt_GetFontOffsetForIndex(regular_.data(), 0));
        regular_path_ = reg;
        face_regular_ = std::move(fr);
        if (!bold.empty() && read_file(bold, b)) {
            bold_ = std::move(b);
            auto fb = std::make_unique<FaceInfo>();
            if (stbtt_InitFont(&fb->info, bold_.data(), stbtt_GetFontOffsetForIndex(bold_.data(), 0))) face_bold_ = std::move(fb);
        }
        for (FaceInfo* f : {face_regular_.get(), face_bold_.get()}) {
            if (!f) continue;
            int asc = 0, desc = 0, gap = 0;
            stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
            const float em = stbtt_ScaleForMappingEmToPixels(&f->info, 1.0f);
            f->ascent = float(asc) * em;
            f->descent = float(desc) * em;
        }
        return true;
    }
    return false;
}

void Text::set_scale(float scale) {
    if (std::fabs(scale - scale_) < 1e-3f) return;
    scale_ = scale;
    cache_.clear();
}

Text::Baked* Text::baked(float size, bool bold) {
    if (regular_.empty()) return nullptr;
    const bool use_bold = bold && face_bold_;
    const int px = std::max(6, int(std::lround(size * scale_)));
    const auto key = std::make_pair(px, use_bold);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second.get();
    auto b = std::make_unique<Baked>();
    b->cps = codepoints();
    b->chars.resize(b->cps.size());
    b->px = float(px);
    b->face = use_bold ? face_bold_.get() : face_regular_.get();
    const std::vector<unsigned char>& data = use_bold ? bold_ : regular_;
    for (int dim = 256; dim <= 4096; dim *= 2) {
        std::vector<unsigned char> alpha(std::size_t(dim) * std::size_t(dim));
        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, alpha.data(), dim, dim, 0, 1, nullptr)) break;
        stbtt_PackSetOversampling(&pc, px < 24 ? 2 : 1, 1);   // small text: horizontal subpixel positions
        stbtt_pack_range range{};
        range.font_size = STBTT_POINT_SIZE(float(px));
        range.array_of_unicode_codepoints = b->cps.data();
        range.num_chars = int(b->cps.size());
        range.chardata_for_range = b->chars.data();
        const int ok = stbtt_PackFontRanges(&pc, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0), &range, 1);
        stbtt_PackEnd(&pc);
        if (!ok) continue;
        std::vector<unsigned char> rgba(alpha.size() * 4);
        for (std::size_t k = 0; k < alpha.size(); ++k) {
            rgba[k * 4 + 0] = rgba[k * 4 + 1] = rgba[k * 4 + 2] = 255;
            rgba[k * 4 + 3] = alpha[k];
        }
        b->tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, dim, dim);
        if (!b->tex) break;
        SDL_UpdateTexture(b->tex, nullptr, rgba.data(), dim * 4);
        SDL_SetTextureBlendMode(b->tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(b->tex, SDL_SCALEMODE_LINEAR);
        b->aw = b->ah = dim;
        break;
    }
    if (!b->tex) return nullptr;
    Baked* out = b.get();
    cache_[key] = std::move(b);
    return out;
}

// ---- the game's fonts ----

namespace {
constexpr float kGameLine = 1.28f;                        // the font's line (its tallest glyph) per TrueType size unit
constexpr float kSlant = 0.25f, kItalicWidth = 1.15f;     // the menus' italic (b3settings page.h kItalicSlant / kItalicWidth)
constexpr float kTrueSlant = 0.2f;                         // mac5: the TrueType bold's slant (the menus' italic look)
}  // namespace

void Text::set_game_fonts(const GameFont* menu, SDL_Texture* menu_tex, const GameFont* head, SDL_Texture* head_tex) {
    menu_ = menu && menu_tex ? menu : nullptr;
    menu_tex_ = menu_ ? menu_tex : nullptr;
    head_ = head && head_tex ? head : nullptr;
    head_tex_ = head_ ? head_tex : nullptr;
}

bool Text::game_usable(const GameFont* f, std::string_view s) const {
    if (!f) return false;
    for (char16_t c : to_u16(s))
        if (!f->has(std::uint16_t(c))) return false;
    return true;
}

// One string as CGt2dFont::RenderString lays it out (0x1D7B90; the PC SETTINGS page's glyphs()), its line kGameLine x
// size tall in the middle of the TrueType line box; outlined in black as the game's menu text is
float Text::game_text(const GameFont& f, SDL_Texture* tex, std::string_view s, float x, float y, float size, SDL_FColor c,
                      bool italic, bool measure_only) {
    const std::u16string u = to_u16(s);
    const float line = size * kGameLine, sz = f.size_for(line), stretch = italic ? kItalicWidth : 1.0f;
    const float w = f.width(u, sz) * stretch;
    if (measure_only || u.empty() || !tex || f.tex.w <= 0 || f.tex.h <= 0) return w;
    const float top = y + (line_height(size) - line) * 0.5f, bottom = top + line;
    const float sx = f.scale_x * sz * stretch, sy = f.scale_y * sz;
    const float nu = float(f.tex.tw) / float(f.tex.w), nv = float(f.tex.th) / float(f.tex.h);   // ST -> the picture
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    auto pass = [&](float dx, float dy, SDL_FColor col) {
        const FontGlyph* g = f.find(u[0]);
        float pen = x + dx - g->ox * sx;
        for (std::size_t k = 0;;) {
            if (g->u >= 0.0f && g->w > 0 && g->h > 0) {
                const float x0 = pen + g->ox * sx, y0 = top + dy + g->oy * sy, x1 = x0 + g->w * sx, y1 = y0 + g->h * sy;
                const float s0 = italic ? kSlant * (bottom - y0) : 0, s1 = italic ? kSlant * (bottom - y1) : 0;
                const int at = int(v.size());
                v.push_back(SDL_Vertex{{x0 + s0, y0}, col, {g->u * nu, g->v * nv}});
                v.push_back(SDL_Vertex{{x1 + s0, y0}, col, {(g->u + g->w) * nu, g->v * nv}});
                v.push_back(SDL_Vertex{{x1 + s1, y1}, col, {(g->u + g->w) * nu, (g->v + g->h) * nv}});
                v.push_back(SDL_Vertex{{x0 + s1, y1}, col, {g->u * nu, (g->v + g->h) * nv}});
                for (int q : {0, 1, 2, 0, 2, 3}) idx.push_back(at + q);
            }
            if (++k >= u.size()) break;
            pen += g->advance * sx;
            g = f.find(u[k]);
        }
    };
    const float o = std::max(1.0f, size * scale_ / 18.0f) / scale_;   // the outline: a pixel (more for big text)
    const SDL_FColor ink{0.0f, 0.02f, 0.06f, 0.9f * c.a};
    const bool light = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b > 0.35f;   // dark text on a light fill: no outline
    if (light)
        for (const auto& d : {std::pair{o, o}, std::pair{-o, -o}, std::pair{-o, o}, std::pair{o, -o}}) pass(d.first, d.second, ink);
    pass(0, 0, c);
    if (!v.empty()) SDL_RenderGeometry(r_, tex, v.data(), int(v.size()), idx.data(), int(idx.size()));
    return w;
}

// mac5 (2026-11-10, twice: the launcher's text looked pixelated): the game's bitmap fonts are small pictures for a
// 640 x 448 screen and stay soft or blocky however they are filtered at 2-4x, so every string is drawn in the system's
// TrueType face, baked at the window's pixel size; the menus' italic labels and headings are its bold, slanted as the
// game's italic (kTrueSlant) with a dark drop edge. (game_text stays for a later HD font.)
float Text::head(std::string_view s, float x, float y, float size, SDL_FColor c, bool measure_only) {
    return measure_only ? width(s, size, true) : draw(s, x, y, size, c, true);
}

float Text::width(std::string_view s, float size, bool bold) {
    Baked* b = baked(size, bold);
    if (!b) return float(s.size()) * 8.0f * (size / 10.0f);
    float x = 0;
    int prev = 0;
    for (std::size_t i = 0; i < s.size();) {
        const int cp = next_cp(s, i);
        const stbtt_packedchar& pc = b->chars[std::size_t(b->index(cp))];
        if (prev) x += float(stbtt_GetCodepointKernAdvance(&b->face->info, prev, cp)) * stbtt_ScaleForMappingEmToPixels(&b->face->info, b->px);
        x += pc.xadvance;
        prev = cp;
    }
    return x / scale_;
}

float Text::draw(std::string_view s, float x, float y, float size, SDL_FColor c, bool bold) {
    Baked* b = baked(size, bold);
    const float lh = line_height(size);
    if (!b) {   // SDL's debug font
        const float k = size / 10.0f;
        float sx = 1, sy = 1;
        SDL_GetRenderScale(r_, &sx, &sy);
        SDL_SetRenderScale(r_, sx * k, sy * k);
        SDL_SetRenderDrawColorFloat(r_, c.r, c.g, c.b, c.a);
        std::string ascii;
        for (std::size_t i = 0; i < s.size();) {
            const int cp = next_cp(s, i);
            ascii += cp < 127 ? char(cp) : '?';
        }
        SDL_RenderDebugText(r_, x / k, (y + (lh - 8.0f * k) * 0.5f) / k, ascii.c_str());
        SDL_SetRenderScale(r_, sx, sy);
        return float(ascii.size()) * 8.0f * k;
    }
    const float em = b->px;
    const float asc = b->face->ascent * em, desc = b->face->descent * em;
    const float lh_px = lh * scale_;
    float px = std::round(x * scale_);
    const float base = std::round(y * scale_ + (lh_px - (asc - desc)) * 0.5f + asc);
    const float x0 = px;
    std::vector<SDL_Vertex> v, sv;   // the glyphs, their shadow (drawn first)
    std::vector<int> idx, si;
    v.reserve(s.size() * 4);
    idx.reserve(s.size() * 6);
    int prev = 0;
    const float kern_scale = stbtt_ScaleForMappingEmToPixels(&b->face->info, em);
    for (std::size_t i = 0; i < s.size();) {
        const int cp = next_cp(s, i);
        if (prev) px += float(stbtt_GetCodepointKernAdvance(&b->face->info, prev, cp)) * kern_scale;
        prev = cp;
        float qx = px, qy = base;
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(b->chars.data(), b->aw, b->ah, b->index(cp), &qx, &qy, &q, 0);
        if (cp != ' ') {
            const float inv = 1.0f / scale_;
            // the slant (bold = the menus' italic): x moves right with the height above the baseline
            const float t0 = bold ? kTrueSlant * (base - q.y0) : 0.0f, t1 = bold ? kTrueSlant * (base - q.y1) : 0.0f;
            const auto quad = [&](float dx, float dy, SDL_FColor col, std::vector<SDL_Vertex>& out, std::vector<int>& oi) {
                const int at = int(out.size());
                out.push_back(SDL_Vertex{{(q.x0 + t0 + dx) * inv, (q.y0 + dy) * inv}, col, {q.s0, q.t0}});
                out.push_back(SDL_Vertex{{(q.x1 + t0 + dx) * inv, (q.y0 + dy) * inv}, col, {q.s1, q.t0}});
                out.push_back(SDL_Vertex{{(q.x1 + t1 + dx) * inv, (q.y1 + dy) * inv}, col, {q.s1, q.t1}});
                out.push_back(SDL_Vertex{{(q.x0 + t1 + dx) * inv, (q.y1 + dy) * inv}, col, {q.s0, q.t1}});
                for (int k : {0, 1, 2, 0, 2, 3}) oi.push_back(at + k);
            };
            const bool light = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b > 0.35f;
            if (bold && light) {   // the game's dark edge under light menu text, a pixel or two down-right
                const float d = std::max(1.0f, std::round(em / 22.0f));
                quad(d, d, SDL_FColor{0.0f, 0.02f, 0.06f, 0.75f * c.a}, sv, si);
            }
            quad(0, 0, c, v, idx);
        }
        px = qx;
    }
    if (!sv.empty()) SDL_RenderGeometry(r_, b->tex, sv.data(), int(sv.size()), si.data(), int(si.size()));
    if (!v.empty()) SDL_RenderGeometry(r_, b->tex, v.data(), int(v.size()), idx.data(), int(idx.size()));
    return (px - x0) / scale_;
}

std::vector<std::string> Text::wrap(std::string_view s, float size, float max_w, bool bold) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at <= s.size()) {
        std::size_t nl = s.find('\n', at);
        if (nl == std::string_view::npos) nl = s.size();
        const std::string_view para = s.substr(at, nl - at);
        std::string line;
        std::size_t p = 0;
        while (p < para.size()) {
            std::size_t sp = para.find(' ', p);
            if (sp == std::string_view::npos) sp = para.size();
            std::string word(para.substr(p, sp - p));
            const std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && width(trial, size, bold) > max_w) {
                lines.push_back(line);
                line.clear();
            } else if (line.empty() && width(word, size, bold) > max_w) {
                // the loop below breaks it
            } else {
                line = trial;
                word.clear();
            }
            // a word wider than the line (a long path): broken, after a slash where there is one
            while (!word.empty()) {
                if (width(word, size, bold) <= max_w) {
                    line = word;
                    break;
                }
                std::size_t cut = 0, slash = 0;
                for (std::size_t i = 0; i < word.size();) {
                    std::size_t j = i;
                    next_cp(word, j);
                    if (width(std::string_view(word).substr(0, j), size, bold) > max_w) break;
                    if (word[i] == '/' || word[i] == '\\') slash = j;
                    cut = j;
                    i = j;
                }
                if (slash > cut / 2) cut = slash;
                if (cut == 0) cut = 1;
                lines.push_back(word.substr(0, cut));
                word.erase(0, cut);
            }
            p = sp + 1;
        }
        lines.push_back(line);
        if (nl == s.size()) break;
        at = nl + 1;
    }
    return lines;
}

std::string Text::elide(std::string_view s, float size, float max_w, bool bold) {
    if (width(s, size, bold) <= max_w) return std::string(s);
    // keep the end of a path (its last folders say the most): "...rest"
    const bool path = s.find('/') != std::string_view::npos || s.find('\\') != std::string_view::npos;
    std::string t(s);
    while (!t.empty()) {
        if (path) t.erase(0, 1);
        else t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(path ? t.front() : t.back()) & 0xC0) == 0x80) {
            if (path) t.erase(0, 1);
            else t.pop_back();
        }
        const std::string cand = path ? "..." + t : t + "...";
        if (width(cand, size, bold) <= max_w) return cand;
    }
    return "...";
}

}  // namespace b3launcher
