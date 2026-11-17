// Crash-mode objects from a route's GAMEDATA.BGD (crash2, docs/native-renderer.md §12.9): the crash ramps and pickups
// that CB3GameData::Draw 0x14E8C0 draws after the fog pass through B3Im3d. Tags as in docs/track-format.md.
//
// GAMEDATA.BGD [C CB3GameData::Prepare 0x14F050, src/nonmatching/Prepare__11CB3GameDataFv.cpp, its 18-state loader;
// checked on all 36 routes by tests/test_b3asset.cpp]: a 0x800-byte directory {+0x08 u64 stage id[50], +0x198 s32
// header offset[50], +0x260 s32 count}; per stage a 0x800-byte header whose +0x3C0 / +0x3BC give the offset / size of
// its track data (CB3TrackDataStruct, Fixup'd relative to its own start). Track data [C Prepare's 0x17 state]:
// +0x000 6 x CB3StartPositionStruct (0x50; position +0x30 [L]), +0x3E0 off CB3CrashModePickupStruct[] (0x20: position
// +0x00, type +0x10 = texture index, mode mask +0x11), +0x3E4 off CB3CrashModeRampStruct[] (0x50: CGtMatrix3x4 rows X,
// Y, Z, position, then s32 type +0x40), +0x3F8 pickup count, +0x3FC ramp count. The stage the game loads is its run-time
// choice (CB3GameData +0x558, the u64 compared with the directory ids), so the capture names it (scene_capture.cpp).
#ifndef B3ASSET_CRASH_H
#define B3ASSET_CRASH_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "b3asset/raster.h"

namespace b3asset {

struct CrashRamp {
    int type = 0;                 // CB3CrashModeRampStruct +0x40: 0..3 (sizes below)
    float m[4][3] = {};           // rows X, Y, Z, position (row-vector convention, as the capture's Mat34)
    float corner[6][3] = {};      // CB3CrashRamp::Prepare's six points (crash_ramp_corners)
};

struct CrashPickup {
    float pos[3] = {};            // CB3CrashModePickupStruct +0x00
    int type = 0;                 // +0x10: index into kCrashPickupTextures (CB3CrashPickup::Prepare 0x3A17A0 [C matched])
    std::uint8_t modes = 0;       // +0x11: bit 0 normal, bit 2 option A8, bit 1 option A4 (Prepare's 0x17 state) [C]
};

struct GameStage {
    int index = 0;                // position in the directory
    std::uint64_t id = 0;
    std::vector<std::array<float, 3>> starts;   // nonzero start positions (+0x30 of each 0x50 entry) [L]
    std::vector<CrashRamp> ramps;
    std::vector<CrashPickup> pickups;
};

bool read_game_data(Bytes f, std::vector<GameStage>& out, std::string* why = nullptr);

// CB3CrashRamp::Prepare 0x3A1EB0 [L nonmatching, logic believed right; checked against the captured corners, §12.9]:
// half length b along Z, half width a along X, heights c / d of the -X / +X high corners per type:
// 0: b 4, a 3, c = d = 1.2; 1: b 6, a 4, c = d = 3; 2: b 7, a 2.55, c 3.2, d 1; 3: b 7, a 2.55, c 1, d 3.2.
// p0 = P + Zb - Xa, p1 = P + Zb + Xa, p2 = P - Zb - Xa, p3 = P - Zb + Xa, p4 = p0 + Yc, p5 = p1 + Yd.
void crash_ramp_corners(const float m[4][3], int type, float out[6][3]);

// CB3CrashRamp::Draw 0x3A1800 [C asm]: four B3Im3d tri lists over the corners, 18 vertices: the +X side (p5 p3 p1),
// the -X side (p4 p0 p2), the back (p5 p0 p1, p0 p4 p5) and the slope (type 2: p5 p2 p3, p5 p4 p2; else p5 p4 p3,
// p4 p2 p3), with the UVs the asm stores. No bottom face. Colour lWhite = (0.5, 0.5, 0.5, 1) x 128 -> GS (64, 64, 64,
// 128) (static init 0x4DA3A0 [C asm]).
struct ImVertex { float pos[3]; float uv[2]; };
constexpr int kCrashRampVertices = 18;
void crash_ramp_vertices(const float corner[6][3], int type, ImVertex out[kCrashRampVertices]);
constexpr std::uint32_t kCrashRampColour = 0x80404040u;   // RGBA bytes, GS units
// B3Im3d TEST_1 = 0x5000B | (128 x threshold) << 4 (SetAlphaTestThreshold 0x365230 [C matched]): ramps 0.5, pickups
// 0.078125 (CB3CrashPickup::Draw 0x3A0AA0).
constexpr std::uint32_t kCrashRampTest = 0x5000Bu | (64u << 4);
constexpr std::uint32_t kCrashPickupTest = 0x5000Bu | (10u << 4);

// CB3CrashPickup::Draw 0x3A0AA0 [L nonmatching draft, every block mapped; constants C static init 0x4DA310]: a diamond
// facing the camera, top / bottom = pos +- kPickupUp (0, 1.4, 0), "left" / "right" = pos -+ camera right x 1.4
// (D_004E0EE0), UVs kTopUV (0, 0), kLeftUV (1, 0), kBottomUV (1, 1), kRightUV (0, 1); triangles (top, left, bottom),
// (top, bottom, right). Colour D_004C9BF0 = (0.6, 0.6, 0.6, 1) x 128 -> (76, 76, 76, 128). Here in local
// coordinates: x = the camera right axis, y = world up, both in units of 1.4.
constexpr int kCrashPickupVertices = 6;
constexpr float kCrashPickupHalf = 1.4f;
void crash_pickup_vertices(ImVertex out[kCrashPickupVertices]);
constexpr std::uint32_t kCrashPickupColour = 0x804C4C4Cu;
// CB3GameData::Prepare's texture names (D_00488080[9], looked up in the global dictionary; all in DATA/GLOBAL.TXD [C]).
extern const char* const kCrashPickupTextures[9];
constexpr const char* kCrashRampTexture = "ramp";   // D_004B60F0 [C]

}  // namespace b3asset

#endif
