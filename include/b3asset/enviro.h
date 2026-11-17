// Per-route environment: TRACKS/<area>/<route>/ENVIRO.DAT (sky, sun, fog, specular), and the sky as the game draws it
// (docs/sky-enviro.md; merged into docs/native-renderer.md §11).
//
// ENVIRO.DAT is one serialized CB3EnvironmentData image (0xB0-byte header + four CGtTextures at file offsets), loaded
// by CB3EnvironmentManager::Prepare 0x2BCA20 (`<route path>` + "enviro.dat", rodata 0x4C1CB8) and fixed up by
// CB3EnvironmentData::FixUp 0x2B7110 (only the four texture offsets) [C matched func_002BCA20.cpp, func_002B7110.cpp].
// Tags: [C] seen in code/data, [L] likely, [G] guess. Field names are descriptive, not Criterion's.
#ifndef B3ASSET_ENVIRO_H
#define B3ASSET_ENVIRO_H

#include <cstdint>
#include <string>
#include <vector>

#include "b3asset/raster.h"

namespace b3asset {

// One 0x30-byte parameter block. Prepare copies block 0 into both per-light entries of the environment manager
// (env +0xB0 + 0x40 k, k = 0/1: player/viewport light index D_01E85A30) at entry +0x10..+0x3F [C func_002BCA20.cpp].
struct EnviroBlock {
    float fog_colour[4] = {};   // +0x00 -> env +0xC0: CB3Fog::Render colour, RGB 0..1 [C B3WorldPS2_00276300.cpp:165]
    float near_scale = 0;       // +0x10 -> env +0xD0 -> graphics manager +0x260 (+0xA0 per viewport) [L nonmatching
                                //   func_002BC460.cpp:120]; near plane = clamp(0.0003 * this, 0.003, 0.4), far 10000
                                //   [C CB3Fog::CalculateClipPlanes 0x240EF0 + OpenViewport func_00227360.cpp:78-97]
    float fog_max = 0;          // +0x14 -> env +0xD4: fog strength of the far CLUT entries (GenerateClut p0) [C]
    float fog_sky = 0;          // +0x18 -> env +0xD8: fog of CLUT index 0 = depth 0 (sky pixels); 0 in every file [C]
    float specular_scale = 0;   // +0x1C -> env +0xDC: RenderFullScreenSpecular intensity [C B3WorldPS2_00276300.cpp:160]
    float sun_w = 0;            // +0x20 -> env +0xE0: w of the sun-blob colour (1.0 in all files; RenderBlob ignores it) [C/G]
    float unknown[3] = {};      // +0x24..+0x2F: 0, 0, 1 in all files [G]
};

struct Enviro {
    EnviroBlock block[2];       // +0x00 used; +0x30 a second block (colours, 5/80/100 at +0x40) that Prepare does not
                                //   copy into the light entries; purpose unknown [G]
    float specular_colour[4] = {};  // +0x60 = env +0x60 = D_01E85960: RenderFullScreenSpecular colour [C]
    float sun_colour[4] = {};       // +0x70 = env +0x70: sun blob colour (RenderSunBlob) [C B3EnvironmentManager_002BC2B0.cpp]
    float sun_travel[3] = {};       // +0x80 as stored; Prepare negates it [C]: to_sun = -sun_travel is env +0xB0, the
                                    //   track's light vector (renderer +0x30 = -to_sun) and the sky's sun position
    float to_sun[3] = {};           // derived: -sun_travel (normalised as stored, |v| = 1 in all files [C data])
    float corona_scale = 0;         // +0x90: SunRenderer corona radius; Prepare multiplies it by graphics manager +0 [C/G]
    std::uint32_t word94 = 0, word_a8 = 0, file_size = 0;  // +0x94 (0), +0xA8 (0x45440002) [G], +0xAC = file size [C data]
    // +0x98..+0xA4: offsets (from the file start) of four serialized rasters [C FixUp]:
    DecodedRaster gradient;     // +0x98 32x32 CT32 "bk_sky"/"skygrad1": sky colour gradient + sun glow mask (sky dome source)
    DecodedRaster clouds;       // +0x9C 1024x256/128 PSMT8 cloud panorama (sky component 2), also the sun renderer's 3rd texture
    DecodedRaster sky_envmap;   // +0xA0 64/128 px PSMT8 sky for the car environment map (components 4/5)
    DecodedRaster sun_corona;   // +0xA4 128x256 PSMT8 corona / lens flare (CB3SunRenderer). Prepare skips the whole sun
                                //   (no blob, no corona) for four track IDs (0xC4BDA62031BD4000/..31E45000,
                                //   0x67E63A1B0EBD4000/..0EE45000) or when this offset is 0 [C func_002BCA20.cpp]
};

// Decodes ENVIRO.DAT (`file` = whole image). Rasters with a zero offset stay empty.
bool read_enviro(Bytes file, GsMemory& gs, Enviro& out, std::string* why = nullptr);

// Finds a raster by name in a TXD (e.g. "sunblob" in DATA/GLOBAL.TXD, which the sun renderer uses [C Prepare 0x2BCA20,
// rodata 0x4C1CC8]).
bool find_txd_raster(Bytes txd, const std::string& name, GsMemory& gs, DecodedRaster& out);

// ---------------------------------------------------------------------------------------------------------------
// Sky dome (CB3SkyDomeRenderer, VU1 program 0x46A590; geometry checked against that program in the VU1 interpreter:
// tools/render/b3envdump.cpp --vu).
//
// The program generates the dome itself (no mesh data): per azimuth segment j (0..segs) and ring i (0..rings),
//   e = e0 + (e1 - e0) i / rings,  height h = 2e - e^2,  radius r = 1 - e^2,  azimuth a = j / segs (turns)
//   direction = (r cos 2pi a, sign * h, r sin 2pi a) in world axes (camera-centred, no translation), Y up
//   u = u_off + u_scale * a,  v = v0 + v_k * h
// drawn as one strip per segment; CLAMP_1 = 4 (u repeats, v clamps). Component table maDefinitions 0x4B9CA0 [C rodata],
// derived constants as CB3SkyDomeHemisphereParams::Construct 0x21E490 [C matched].
struct SkyVertex {
    float dir[3];
    float uv[2];
};
struct SkyLayer {
    int component = 0;           // 0 below the horizon, 1 above (both textured with the backdrop), 2 clouds
    std::vector<SkyVertex> verts;
    std::vector<std::uint32_t> indices;  // triangle list
    std::uint8_t rgba[4] = {};   // vertex colour as sent (0x80 = 1.0): sky 0x80,0x80,0x80 (alpha 0x20 below / 0 above),
                                 //   clouds 0x40 (half: the frame buffer holds half intensity, docs) with alpha 0x80
    bool blend = false;          // clouds: ALPHA (Cs-Cd)*As+Cd with the texel alpha, FBMSK keeps the frame alpha [C]
    bool clouds_texture = false; // true: Enviro::clouds; false: the 64x32 backdrop texture (build_backdrop)
};
// `cloud_u_offset`: u start of the clouds (component 2), see cloud_u_offset().
SkyLayer build_sky_layer(int component, float cloud_u_offset = 0.0f);

// Cloud scroll: the environment manager's clock t (env +0x134) advances by dt / 480 per update and wraps
// (CB3EnvironmentManager::Update 0x2BC460: t = frac(t + 1 + 0.0020833 dt) [L nonmatching]); SetupCamera 0x21DF80 sets
// the clouds' u start to 2t - (int(2t) + 1) [L nonmatching]. RenderSky adds 0.25 to t (mod 1) when a per-player race
// flag (0x8 or 0x10) is set [C matched B3EnvironmentManager_002BC380.cpp; meaning G: reversed route].
float cloud_u_offset(float t);

// The 64x32 "backdrop" texture the sky components 0/1 sample, composed each frame by
// CB3EnvironmentManager::RenderBackdropTexture 0x2BBB90 [L nonmatching, blend modes C Gt2dRendererPS2_0021A290.cpp]
// from the gradient raster with three sprite passes:
//   1. copy (ALPHA 0x2A): every column = gradient column (15 * column_blend) (left half: base sky by height);
//   2. alpha only (0x6A, alpha written): glow mask = gradient alpha sampled with u = 0.5 - bearing + x/64 (repeat) and,
//      for target rows y < 24, v from rows h(y) = min(24, 24 (0.5 - (1 - sqrt(y/24) - alt))) (sun-relative elevation;
//      D_004E0F08 = 1/24), rows 24..32 from gradient rows 24..32;
//   3. ALPHA 0x54 (Cs-Cd)*Ad+Cd: lerp to gradient column 16 + 15 * column_blend (bright sky) by the glow mask.
// bearing / alt: CB3EnvironmentManager::SunPos2BearingAndAltitude 0x2BB830 of `to_sun`: bearing = atan2(z, x) in turns
// (0..1), alt = 1 - sqrt(1 - y) (y >= 0) or sqrt(1 + y) - 1, i.e. the dome's e of the sun.
// `column_blend` (0..1): (D_0064E470 +0x348 + +0x34C) / (D_0064D344 -> +8) when that pointer is set, else 0 [G meaning].
// Output in display units (the PS2 composes at half intensity and the frame is doubled at present; docs), RGBA8,
// alpha = the glow mask (0x80 = 1.0 mapped to 0xFF).
Image build_backdrop(const Enviro& env, const float to_sun[3], float column_blend = 0.0f);

// ---------------------------------------------------------------------------------------------------------------
// Fog (CB3Fog::Render 0x2419C0, after the after-specular track pass; docs/sky-enviro.md):
// the 24-bit Z the VU1 programs write, Z = z_shift + z_scale / w (RW camera zScale/zShift for near..far mapped to
// 16777215..0 less 0.01 % margins [C RW 3.1 bacamera.c CameraUpdateZShiftScale; RpSkySelectDeepZBuffer(1) C
// B3GraphicsManagerPS2.cpp:115]), is reduced to an 8-bit index = Z >> 8 for Z < 0xFFFF (ZGreen2FBAlpha + a full-screen
// sprite at Z 0xFFFF with ZTST GREATER [C RenderIndexedFog 0x241040]); nearer pixels get no fog. The index selects
// alpha from GenerateClut 0x2413E0 [L nonmatching, formula C]:
//   i = 0: fog_sky;  1..11: fog_max;  12..255: fog_max * g(t), t = (1.4 / (0.4 + (i - 12) / 243) - 1) / 2.5,
//   g = t < 0.75 ? 4/3 t^2 : 1 - 4 (1 - t)^2,   alpha byte = int(0.5 + 128 * value)
// and the frame is blended (Cs-Cd)*As+Cd towards the fog colour (RGB * 64 modulated by a white CLUT = half intensity,
// i.e. fog_colour in display units).
struct FogModel {
    float near = 0.4f, far = 10000.0f;
    float z_scale = 0, z_shift = 0;
    float fog_max = 0, fog_sky = 0;
    float colour[3] = {};
    std::uint8_t alpha[256] = {};  // per index, 0x80 = full fog
};
FogModel fog_model(const Enviro& env);
// Fog amount (0..1, the game's quantised value) for a pixel at view depth w (metres along the view axis).
float fog_amount(const FogModel& f, float w);

// ---------------------------------------------------------------------------------------------------------------
// Sun blob (CB3SunRenderer::RenderBlob 0x24CDF0 via CB3EnvironmentManager::RenderSunBlob 0x2BC2B0): a screen sprite of
// GLOBAL.TXD "sunblob" centred on the projection of camera + 50 * to_sun, half size 12 / viewWindow.x pixels and the
// same angular size vertically, i.e. tan(half angle) = 24 / frame width (640: 0.0375) [L nonmatching]; colour
// sun_colour * 128 (MODULATE: twice the texel in display units, clamped), texel-alpha blend, no Z test or write, drawn
// after the sky components 0/1 and before the clouds [C frame order B3WorldPS2_00276300.cpp:121-125].
constexpr float kSunBlobTanHalf = 24.0f / 640.0f;

}  // namespace b3asset

#endif
