// paintsel.h -- paintsel (2026-11-21): the PAINT prompt on SELECT CAR (tools/native/paint_select.h has the page, its
// inputs and the rule for the game's own colour). While the page takes input, native_play's host record ('HPS1', hud.h
// HostPaintSel) carries the prompt's opacity; the renderer draws, in the page's prompt bar between the game's "Change"
// and "Select", one more prompt in the bar's style: the CAR PAINT PANEL button as the prompts draw it for the device in
// use (prompts.h paint_prompt_glyph: the PlayStation's Circle; the Xbox and Nintendo Y, where their layouts put the
// menus' Circle; the keyboard's key cap with the bound key, P) and "Paint" in the game's own font, in the PC SETTINGS
// language (b3settings i18n). Drawn only; hidden while the paint panel (PcPage::panel_open) or PC SETTINGS is open.
#ifndef B3REPLAY_PAINTSEL_H
#define B3REPLAY_PAINTSEL_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "hud.h"
#include "prompts.h"

namespace b3replay {

// Where the prompt goes (640 x 448 picture units): the glyph's left edge, the middle of the bar's line, the text's line
// height and the glyph's -- the bar's own prompts' sizes, measured on the page (English: "Back" capitals 412-424, its
// glyph 18 units high centred at 417.5, the text 6 units after it; "Change" ends at 394, "Select" begins at 533); the
// text in the bar's blue. The text is narrowed to fit before kPaintSelMaxRight.
constexpr float kPaintSelLeft = 430.0f, kPaintSelMid = 418.5f, kPaintSelH = 25.0f, kPaintSelGlyphH = 19.0f, kPaintSelMaxRight = 524.0f;
constexpr std::uint32_t kPaintSelText = 0x00C0985Au;   // RGB 90, 152, 192 (as HostUiQuad::rgba: red in the low byte)

class PaintSelPrompt {
public:
    // The prompt as host-UI quads (View::host_ui_list), kept while nothing changes; null while hidden (`opacity` 0) or
    // before the game's fonts are captured. `keys`: the device's prompt keys (a pad: its glyph positions, so the menus'
    // Circle shows where the player's layout puts it); `key`: the CAR PAINT PANEL key (the keyboard's cap; 0: none).
    std::shared_ptr<const HostUi> ui(const std::shared_ptr<const HostFonts>& fonts,
                                     const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures, std::uint8_t opacity,
                                     PromptDevice d, const PromptKeys& keys, std::uint16_t key, const std::string& language);
    // The text (tests): "Paint" in `language` (a disc code; else English)
    static std::string text(const std::string& language);

private:
    struct Key {
        const HostFonts* fonts = nullptr;
        std::uint32_t font_serial = 0;
        std::uint8_t opacity = 0;
        PromptDevice device = PromptDevice::kPlayStation;
        PromptKeys keys{};
        std::uint16_t key = 0;
        std::string language;
        bool operator==(const Key&) const = default;
    } built_key_;
    std::shared_ptr<const HostUi> built_;
    std::shared_ptr<const HostUi::Image> glyph_;
    float glyph_aspect_ = 0;
    std::uint32_t serial_ = 0;
};

}  // namespace b3replay

#endif
