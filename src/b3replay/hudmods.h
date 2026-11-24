// hudmods.h -- hudmods (2026-11-21; remaster-features §29): the race HUD's opt-in additions, drawn only.
//
// - RPM METER (PC SETTINGS > DISPLAY > RACE HUD; ini `rpm_meter`, default off): a thin bar of 20 segments under each
//   view's speedometer plate, filled with the engine revs from the low end to the rev limit: the plate's blue, the last
//   three red, unlit segments a faint navy, no panel behind (the HUD COLOUR filter applies, as to all the 2D). The
//   same for the automatic and the manual gearbox.
// - BOOST BAR SHAKE (the same section; ini `boost_shake`, default off): while a car boosts, its view's boost bar shakes
//   by up to 2 units across and 3 up and down, a new place every 3 frames, as in the game's April 2004 preview build.
//
// The values come with the HUD frame they belong to: the HUD capture's GAUGE events (src/gameview/hud_capture.cpp
// observe_gauge; hud.h HudGauge): each listed speedometer's engine revs and each boost bar's boosting flag, read where
// the game's own HUD reads them. Both additions are made on a copy of the frame before the HUD is laid out (View::
// hud_list): the meter as untextured draws of the speedometer's own object (so the wide layout, HUD SIZE and the split
// screen move it with the speedometer), the shake as an offset of the boost bar's draws. A frame without gauges (an
// older capture, the front end, Crash mode's HUD without a speedometer) is left as it is; with both rows off nothing
// is copied: the HUD is the game's.
#ifndef B3REPLAY_HUDMODS_H
#define B3REPLAY_HUDMODS_H

#include <cstdint>
#include <memory>

#include "hud.h"

namespace b3replay {

struct HudModOptions {
    bool rpm_meter = false;
    bool boost_shake = false;
    bool operator==(const HudModOptions&) const = default;
};

// The meter's look (2D units of the game's 640 x 448 HUD)
constexpr int kRpmSegments = 20, kRpmRedSegments = 3;
constexpr float kRpmGap = 1.0f, kRpmHeight = 3.0f, kRpmBelow = 2.0f;   // between segments; the bar's height; under the plate
constexpr float kRpmSlant = 1.5f;                                       // the segments' lean (the HUD plates' slant)
// The segments' colours as GS RGBA bytes (0x80 = 1.0; red in the low byte): lit in the speedometer plate's blue (RGB 70,
// 140, 230), the last kRpmRedSegments red (224, 44, 24) when lit, unlit a faint navy (no panel behind the bar)
constexpr std::uint32_t kRpmLit = 0x80734623U, kRpmRed = 0x800C1670U, kRpmUnlit = 0x48281408U;
constexpr float kShakeX = 2.0f, kShakeY = 3.0f;                         // the shake's reach either way
constexpr std::uint32_t kShakeFrames = 3;                               // frames per place

// The shake's offset for a HUD frame serial (deterministic: a replay shakes the same)
void hud_shake_offset(std::uint32_t serial, float& dx, float& dy);
// The lit segments for a fill 0..1 (a segment lights when the fill reaches its middle)
int hud_rpm_lit(float fraction);

// The frame with the additions `o` asks for; `in` itself when there is nothing to add. Kept while the frame and the
// options are the same (one copy per captured frame).
class HudMods {
public:
    std::shared_ptr<const HudFrame> apply(const std::shared_ptr<const HudFrame>& in, const HudModOptions& o);
    // The frame apply() gave for `in` (the split screen's placement reads the laid-out draws), else `in`
    const HudFrame& laid_out(const std::shared_ptr<const HudFrame>& in) const { return in == src_ && out_ ? *out_ : *in; }
    // tests: the additions on a frame
    static void add_rpm_meter(HudFrame& f);
    static void shake_boost_bars(HudFrame& f);

private:
    std::shared_ptr<const HudFrame> src_, out_;
    HudModOptions opts_;
};

}  // namespace b3replay

#endif
