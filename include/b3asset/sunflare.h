// The sun corona and lens flare (water1, docs/native-renderer.md §21): CB3EnvironmentManager::SunOcclusionTest 0x2BBFA0
// -> CB3SunRenderer::SetupCamera 0x24D110 + OcclusionTest 0x24C7E0, and RenderSunCorona 0x2BBF50 -> CB3SunRenderer::
// RenderCorona 0x24BF60 [C matched callers; L nonmatching drafts of the callees, every constant read from the asm].
// Tags: [C] seen in code/data, [L] likely, [G] guess. Field names are descriptive, not Criterion's.
//
// What the game does, per view, after the 3D (fog, post-fog effects, B3Im3d objects) and before bloom:
//  - SetupCamera: the point camera + 50 x unit(to-sun) through the camera record's +0xE0 matrix; behind (no corona) when
//    its view z <= 0.01; screen position = (x / z, y / z) x the screen size in pixels.
//  - OcclusionTest: no corona unless that position lies within 6.5 px of the screen. A box of +-7 px (0x70 in 1/16 px)
//    around it, clamped to the screen, gives fade = clamped area / 256 / 196 (1 when whole) on the corona colour
//    (sun colour x its w x 128, in GS bytes). Then the GS: the box's frame alpha is set to 0, then 0x40 where Z <= 1 and
//    0x80 where Z == 0 (Z GEQUAL sprites at Z = 1 / 0: pixels no Z-writing draw covered, the sky), and that alpha is
//    reduced by bilinear copies (box -> 8x8 -> 4x4 -> 2x2 px at the frame's origin).
//  - RenderCorona: one sprite copies that reduced alpha into the corona texture's CLUT (FRAME = CBP, FBMSK 0x00FFFFFF:
//    alpha only, all 256 entries), then the corona sprite (radius R = ENVIRO +0x90 x graphics manager +0, R x aspect
//    tall, the texture's upper half) and 12 lens-flare sprites along the line from the screen centre through the sun
//    (table D_004BB610: position, size, colour scale, quadrant of the lower half; dimmed by a pseudo-random flicker from
//    the world clock) are drawn with ALPHA 0x48 = Cs x As + Cd, As = that CLUT alpha: additive, scaled by the share of
//    the box where the sky shows. A GS renderer that does not keep the frame's alpha for this CLUT-from-frame read
//    shows "sun glare through tunnels".
#ifndef B3ASSET_SUNFLARE_H
#define B3ASSET_SUNFLARE_H

#include <cstdint>
#include <string>
#include <vector>

namespace b3asset {

// D_004BB610[12] (0x18 each) and Revenge's `static bool lbSecondaries` (D_004E1978, initial 1) [C ELF data].
struct SunSecondary {
    float pos = 0;               // +0x00: along the centre -> sun line (1 = at the sun, negative = past the centre)
    float size = 0;              // +0x04: x the corona radius
    float rgb[3] = {};           // +0x08..+0x10: colour scale
    std::uint32_t quadrant = 0;  // +0x14: bit 0 right half, bit 1 lower quarter of the texture's lower half
};
struct SunFlareTable {
    SunSecondary sec[12];
    bool secondaries = true;
    bool valid = false;
};
// Reads the table from the game's ELF (`<game>/SLUS_210.50`); false (table invalid) if it cannot.
bool read_sun_flare_table(const std::string& elf_path, SunFlareTable& out, std::string* why = nullptr);

// CB3EnvironmentManager::Prepare 0x2BCA20 skips the sun renderer (no blob, no corona) for the track IDs
// 0xC4BDA62031BD4000 / ..31E45000 / 0x67E63A1B0EBD4000 / ..0EE45000 [C]; read as packed route names they are US/P2_V1,
// US/P2_V2, EU/M2_V1, EU/M2_V2 (the ID Prepare falls back to, 0xC4BD5916359D4000, reads US/C5_V1, a real route) [L].
// `route_dir` ends in <area>/<route>.
bool route_has_sun(const std::string& route_dir);

// Inputs, in the game's screen pixels (x right, y down; 640 x 448 for the PS2 frame; a wider window is wider).
struct SunFlareInput {
    float sun_px[2] = {};        // mSunPos x mScreenSize
    float screen[2] = {640, 448};
    float radius = 0;            // mrCoronaRadius = ENVIRO +0x90 x graphics manager +0
    float aspect = 1;            // mrAspectRatio (y radius = radius x aspect)
    float colour[3] = {};        // D_01D9B970 = sun colour x its w x 128 (GS bytes, float)
    float now = 0;               // world time gWorld+0x20 (D_00522680): the flicker
};
struct SunSprite {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // screen pixels
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // texture coordinates 0..1
    int rgb[3] = {};                       // vertex colour, GS bytes (MODULATE: Ct x Cv / 128)
};
struct SunFlare {
    bool drawn = false;          // false: behind or off screen (mbBehind)
    float box[4] = {};           // the occlusion box (pixels, clamped to the screen; the GS sprite's whole pixels)
    float fade = 0;              // clamped area / 256 / 196
    std::vector<SunSprite> sprites;   // the corona, then the 12 secondaries
};
// OcclusionTest's screen test, box and fade, and RenderCorona's sprites (integer arithmetic as the game's, in 1/16 px;
// the flicker with the EE's float rounding). `tex_w` / `tex_h`: the corona texture (128 x 256 on every route).
SunFlare sun_flare(const SunFlareInput& in, const SunFlareTable& table, int tex_w, int tex_h);

}  // namespace b3asset

#endif
