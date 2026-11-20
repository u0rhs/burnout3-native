// racetime.h -- todrace (1.3.0, 2026-11-20): the DAY / NIGHT label on the track pages (Single Event's SELECT OPTIONS and
// JUNCTION SELECTION, Multiplayer's REGION / TRACK; tools/native/race_time.h has the pages, their inputs and the choice's
// lifetime). While a page takes input, native_play's
// host record ('HTD1', hud.h HostRaceTime) carries the page's choice and the label's opacity; the renderer draws, above
// the page's prompt bar and right-aligned with its last prompt ("Continue" and its button), one more prompt in the bar's
// style: "Time of day: Night" in the game's own font and the TIME OF DAY action's button (input_bindings.h
// PortAction::RaceTime) as the prompts draw it for the device in use (prompts.h paint_prompt_glyph: the PlayStation's
// L1, the Xbox LB, the Nintendo L; the keyboard's key cap with the bound key). Default reads "Default (Day)" / "Default
// (Night)": what TIME OF DAY gives. In the PC SETTINGS language (b3settings i18n). Drawn only.
#ifndef B3REPLAY_RACETIME_H
#define B3REPLAY_RACETIME_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "../native/input_bindings.h"
#include "hud.h"
#include "prompts.h"

namespace b3replay {

// The label's text: "Time of day: Day", "Time of day: Night", "Time of day: Default (Day)" (`setting_night`: what Default
// gives), in `language` (a disc code: FR GE IT SP DU SW; else English)
std::string race_time_text(int choice, bool setting_night, const std::string& language);
// The prompt glyph of a gamepad input (the DualShock position it is at: South Cross, LB L1, the triggers L2 / R2, the
// stick clicks L3 / R3, Back Select ...); false for one the glyphs have no picture of (the D-pad, a stick's direction, a
// paddle)
bool race_time_pad_glyph(b3native::input::PadInput in, PadButton& out);

// Where the label goes (640 x 448 picture units): its right end, the middle of its line, the text's line height (its
// capitals as tall as the bar's, ~12 units), the button's height (the bar's glyphs'); the text in the bar's blue
constexpr float kRaceTimeRight = 603.0f, kRaceTimeMid = 370.0f, kRaceTimeH = 19.0f, kRaceTimeGlyphH = 16.0f;
constexpr std::uint32_t kRaceTimeText = 0x00C0985Au;   // RGB 90, 152, 192 (as HostUiQuad::rgba: red in the low byte)

class RaceTimeLabel {
public:
    // The label as host-UI quads (View::host_ui_list), kept while nothing changes (the view keeps its build); null while
    // hidden (`opacity` 0) or before the game's fonts are captured. `key`: the action's first key (0 none), `pad`: its
    // first gamepad input (None: none).
    std::shared_ptr<const HostUi> ui(const std::shared_ptr<const HostFonts>& fonts,
                                     const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures, int choice,
                                     std::uint8_t opacity, bool setting_night, PromptDevice d, std::uint16_t key,
                                     b3native::input::PadInput pad, const std::string& language);

private:
    struct Key {
        const HostFonts* fonts = nullptr;
        std::uint32_t font_serial = 0;
        int choice = -1;
        std::uint8_t opacity = 0;
        bool night = false;
        PromptDevice device = PromptDevice::kPlayStation;
        std::uint16_t key = 0;
        b3native::input::PadInput pad = b3native::input::PadInput::None;
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
