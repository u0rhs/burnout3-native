// font.h -- the launcher's text: a TrueType face of the system (stb_truetype, third_party/stb_truetype.h) baked per
// size into SDL textures at the window's pixel scale; SDL's 8 x 8 debug font when no face is found. Coordinates are
// the renderer's logical units (SDL_SetRenderScale = the UI scale).
#ifndef B3LAUNCHER_FONT_H
#define B3LAUNCHER_FONT_H

#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace b3launcher {

struct GameFont;

// settingsui: text that is the player's own (a profile's name, a folder or file name, a song title, a mod's own words):
// drawn and measured as it is, never looked up in the translation tables. verbatim() marks it (a leading U+0001); the
// mark is dropped when it is drawn, and wrap / elide keep it on the lines they return.
constexpr char kVerbatim = '\x01';
inline std::string verbatim(std::string_view s) { return std::string(1, kVerbatim) + std::string(s); }
inline bool is_verbatim(std::string_view s) { return !s.empty() && s.front() == kVerbatim; }

// The font files tried (regular, bold), the first that exists wins: $B3LAUNCHER_FONT / $B3LAUNCHER_FONT_BOLD, then the
// system's (Segoe UI, Arial, Noto Sans, DejaVu Sans, Liberation Sans, ...).
std::vector<std::pair<std::string, std::string>> font_candidates();

class Text {
public:
    explicit Text(SDL_Renderer* r);
    ~Text();
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;

    // The face (false: none found, the debug font draws). `scale`: pixels per logical unit (the atlases are rebuilt).
    bool load();
    void set_scale(float scale);
    bool have_face() const { return !regular_.empty(); }
    const std::string& face_path() const { return regular_path_; }

    // The game's own fonts (gameart.h; their textures made by the caller): text then draws in GlobalFont as the menus do
    // (outlined; `bold` = the menus' italic) whenever the font has every character of it, else in the TrueType face.
    // nullptr: off.
    void set_game_fonts(const GameFont* menu, SDL_Texture* menu_tex, const GameFont* head, SDL_Texture* head_tex);
    bool game() const { return menu_ != nullptr; }
    // A heading: HeadFont's italic capitals when it has every letter, else the menu font's italic
    float head(std::string_view s, float x, float y, float size, SDL_FColor c, bool measure_only = false);

    // 1.1: every text drawn or measured is the port's own words in this language (b3settings::translate_text: a label,
    // or a description sentence by sentence; what the tables lack, such as names and paths, stays). Empty: English.
    void set_language(std::string code) { language_ = std::move(code); }
    const std::string& language() const { return language_; }
    // `s` in the language (as draw would show it), for a caller that changes it first (capitals)
    std::string translated(std::string_view s) const {
        if (is_verbatim(s)) return std::string(s.substr(1));
        return language_.empty() ? std::string(s) : tr(s);
    }

    // settingsui: every text drawn from now on, as drawn (after its translation), appended to `into` (nullptr: off)
    void record_into(std::vector<std::string>* into) { record_ = into; }

    float width(std::string_view s, float size, bool bold = false);
    float line_height(float size) const { return size * 1.32f; }
    // Draws `s` with its top at y (the line box of line_height); returns its width.
    float draw(std::string_view s, float x, float y, float size, SDL_FColor c, bool bold = false);
    // Words wrapped to `max_w` (an explicit '\n' breaks too)
    std::vector<std::string> wrap(std::string_view s, float size, float max_w, bool bold = false);
    // `s` cut with "..." to fit `max_w`
    std::string elide(std::string_view s, float size, float max_w, bool bold = false);

private:
    struct Baked;
    Baked* baked(float size, bool bold);
    bool game_usable(const GameFont* f, std::string_view s) const;
    float game_text(const GameFont& f, SDL_Texture* tex, std::string_view s, float x, float y, float size, SDL_FColor c, bool italic,
                    bool measure_only);

    // 1.1: translation once per public call; `raw_` while a call works on text already translated (wrap's words)
    std::string language_;
    bool raw_ = false;
    struct Raw {
        Text& t;
        bool was;
        explicit Raw(Text& text) : t(text), was(text.raw_) { t.raw_ = true; }
        ~Raw() { t.raw_ = was; }
    };
    bool translating() const { return !raw_ && !language_.empty(); }
    std::string tr(std::string_view s) const;

    std::vector<std::string>* record_ = nullptr;

    const GameFont* menu_ = nullptr;
    const GameFont* head_ = nullptr;
    SDL_Texture* menu_tex_ = nullptr;
    SDL_Texture* head_tex_ = nullptr;

    SDL_Renderer* r_;
    float scale_ = 1;
    std::vector<unsigned char> regular_, bold_;
    std::string regular_path_;
    struct FaceInfo;
    std::unique_ptr<FaceInfo> face_regular_, face_bold_;
    std::map<std::pair<int, bool>, std::unique_ptr<Baked>> cache_;
};

}  // namespace b3launcher

#endif
