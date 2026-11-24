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
#include "driverlabels.h"
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

// elim1 (tools/native/single_eliminator.h): TIMED ELIMINATION's countdown in a race, in the lettering of the race's own
// captions ("6th PLACE ELIMINATED!": the HUD's primary message, func_003D56D0 -> CB3StylisedText2dObject func_003D0710,
// its layout func_003CDB80 and _RenderNormal 0x3CFE40 / func_003CE290): the graphics manager's font (D_00665ED0.pFont,
// the captured fonts' first), 1.5 wide, the tightened advance, sheared 0.4, the black outline ring and drop shadow, the
// four-stop ramp (0, 0.48, 0.51, 1) of the caption's style 1 (the elimination caption's gold); a small lead-in (the "\2"
// size, 0.625) and the big seconds (1.0) on one baseline, as "6th PLACE" and "ELIMINATED!". kElimH high (the real caption
// is 44), centred under the HUD's top row, above where the caption shows. In the last kElimWarnSeconds the seconds take
// the game's warning captions' style (4: the HUD's +0x50 to +0x60). In `language` (FR GE IT SP DU SW; else English).
constexpr float kElimMid = 320.0f, kElimTop = 50.0f, kElimH = 34.0f;
constexpr int kElimWarnSeconds = 5;
std::string elim_countdown_text(int seconds, const std::string& language);   // the lead-in (the seconds drawn after it)
class ElimCountdown {
public:
    // The countdown as host-UI quads (View::host_ui_list), kept while nothing changes; null while none (`seconds` < 0) or
    // before the game's fonts are captured
    std::shared_ptr<const HostUi> ui(const std::shared_ptr<const HostFonts>& fonts,
                                     const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures,
                                     const HostElimTimer& t, const std::string& language);

private:
    const HostFonts* fonts_ = nullptr;
    std::uint32_t font_serial_ = 0;
    int seconds_ = -2;
    std::uint32_t colours_[4] = {};
    std::string language_;
    std::shared_ptr<const HostUi> built_;
    std::uint32_t serial_ = 0;
};

// gearbox (tools/native/gearbox.h): the manual gearbox's gear on the race HUD, drawn as the HUD draws its POS (CB3HUDPos
// 0x335AB0 / 0x3356A0: the graphics manager's font; the label as its labels, 27 units in its label colour +0x40; the gear
// as its position digits, 56 units, 1.5 wide, the blend +0x60 -> +0x60 / +0x30 at 0.75; the four-way black outline):
// "GEAR" and 1..6, N, R, right-aligned above the speedometer
constexpr float kGearRight = 606.0f, kGearTop = 328.0f;
std::string gear_label(const std::string& language);
std::string gear_text(int gear);   // "R", "N", "1".."9"
class GearIndicator {
public:
    std::shared_ptr<const HostUi> ui(const std::shared_ptr<const HostFonts>& fonts,
                                     const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures, const HostGear& g,
                                     const std::string& language);

private:
    const HostFonts* fonts_ = nullptr;
    std::uint32_t font_serial_ = 0, colours_[4] = {}, serial_ = 0;
    int gear_ = -3;
    std::string language_;
    std::shared_ptr<const HostUi> built_;
};

// options3: DRIVER LABELS (driverlabels.h has the layout)
class DriverLabels {
public:
    // The labels as host-UI quads (View::host_ui_list slot 1), cover 4 (over the wide picture); null for none, the
    // setting Off or before the game's fonts are captured
    std::shared_ptr<const HostUi> ui(const std::shared_ptr<const HostFonts>& fonts,
                                     const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures,
                                     const std::vector<DriverLabel>& labels, int mode, int speed_units);

private:
    std::uint32_t serial_ = 0;
};

// replayview (tools/native/replay_view.h): the REPLAY prompt on a race's results page (the record 'HRV1' state 1): the
// REPLAY action's button as the prompts draw it for the device in use (Square / X; the keyboard's key cap, F6) and
// "Replay" in the game's font, in the bar's blue, right-aligned at kReplayPromptRight, left of the page's own Continue
constexpr float kReplayPromptRight = 500.0f, kReplayPromptMid = 418.5f, kReplayPromptH = 25.0f, kReplayPromptGlyphH = 19.0f;
class ReplayPrompt {
public:
    // null while hidden (`shown` false) or before the game's fonts are captured; `key`: the REPLAY key (0 none)
    std::shared_ptr<const HostUi> ui(const std::shared_ptr<const HostFonts>& fonts,
                                     const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures, bool shown,
                                     PromptDevice d, std::uint16_t key, const std::string& language);

private:
    const HostFonts* fonts_ = nullptr;
    std::uint32_t font_serial_ = 0;
    PromptDevice device_ = PromptDevice::kPlayStation;
    std::uint16_t key_ = 0;
    std::string language_;
    std::shared_ptr<const HostUi> built_;
    std::shared_ptr<const HostUi::Image> glyph_;
    float glyph_aspect_ = 0;
    std::uint32_t serial_ = 0;
};

}  // namespace b3replay

#endif
