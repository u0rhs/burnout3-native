// b3view crash scene (crash2, docs/native-renderer.md §12.9): a route's crash-mode objects from GAMEDATA.BGD as b3gfx
// draws, shared by b3replay (the stage the capture names) and b3view's static route view (--crash-stage / K).
// CB3GameData::Draw 0x14E8C0 draws them through B3Im3d after the fog pass: CB3CrashRamp::Draw (the ramp wedges) and
// CB3CrashPickup::Draw (camera-facing diamonds); the geometry is b3asset/crash.h's.
#ifndef B3VIEW_CRASHSCENE_H
#define B3VIEW_CRASHSCENE_H

#include <string>
#include <vector>

#include "b3asset/crash.h"
#include "b3gfx/b3gfx.h"

namespace b3view {

// perf1: DrawCheckpoint's constants [C asm 0x15E340 / 0x15E720 / 0x15E5E0]: the vertex colour x 128 (Draw (0.5, 0.5, 0.5, 1),
// split checkpoints (0.375, 0.125, 0.125, 1)), the rise (0, 11, 0), B3Im3d TEST_1 at threshold 0.25 (AREF 32)
constexpr std::uint32_t kFinishColour = 0x80404040u, kSplitColour = 0x80101030u;
constexpr float kFinishRise = 11.0f;
constexpr std::uint32_t kFinishTest = 0x5000Bu | (32u << 4);

struct CrashScene {
    std::vector<b3asset::GameStage> stages;            // GAMEDATA.BGD's directory order
    std::vector<std::vector<b3gfx::DrawItem>> ramps;   // per stage, per ramp: Draw's 18 vertices from the file's matrix
    b3gfx::DrawItem pickup;                            // the unit diamond (texture and model matrix set per pickup)
    b3gfx::TextureId pickup_tex[9] = {b3gfx::kNone, b3gfx::kNone, b3gfx::kNone, b3gfx::kNone, b3gfx::kNone,
                                      b3gfx::kNone, b3gfx::kNone, b3gfx::kNone, b3gfx::kNone};
    // perf1 (docs/native-renderer.md §33): the start / finish line quad (CB3SplitTimeCheckpoint::DrawCheckpoint 0x15E340),
    // unit corners (+-1, +-1) in the local (side, up) plane with Draw's UVs; [0] colour (64, 64, 64, 128), [1] the split
    // checkpoints' (48, 16, 16, 128); textures "finish" and "check" (GLOBAL.TXD)
    b3gfx::DrawItem finish_quad[2];
    b3gfx::TextureId finish_tex[2] = {b3gfx::kNone, b3gfx::kNone};
};

// perf1: one start / finish line or split checkpoint: the quad centred at pos + (0, 11, 0), spanning side x half_w and
// up x half_h, side = normalise((0, 1, 0) x dir) [C asm 0x15E340]; texture 0 "finish", 1 "check"; rgba 0x80101030 = the
// split tint
void add_finish_line(const CrashScene& cs, const float pos[3], const float dir[3], float half_h, float half_w, int texture,
                     std::uint32_t rgba, std::vector<b3gfx::DrawItem>& out);

// Reads <route>/GAMEDATA.BGD and <game>/DATA/GLOBAL.TXD ("ramp", the nine pickup textures) and uploads the draws.
// False (and an empty scene) when the route has no GAMEDATA.BGD or it does not parse.
bool load_crash_scene(b3gfx::Device& dev, const std::string& route, const std::string& game, CrashScene& out, std::string* why);
// loadlag1: the same from the two files' bytes, read beforehand (on a worker: View::prepare_track); `txd` empty: not read
bool load_crash_scene(b3gfx::Device& dev, const std::vector<std::uint8_t>& gamedata_bgd, const std::vector<std::uint8_t>& global_txd,
                      CrashScene& out, std::string* why);

// One pickup of texture `type` at `pos`, facing a camera whose CGtMatrix3x4 right row is `right` (the game's camera
// frame: right points to screen left).
void add_crash_pickup(const CrashScene& cs, const float pos[3], int type, const float right[3], std::vector<b3gfx::DrawItem>& out);

// Stage `st`'s ramps and the pickups Prepare would place with no option set (mode bit 0) [L: the option virtuals'
// values are not known without a capture], facing `right`.
void add_crash_stage(const CrashScene& cs, int st, const float right[3], std::vector<b3gfx::DrawItem>& out);

}  // namespace b3view

#endif
