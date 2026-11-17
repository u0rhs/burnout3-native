// Track shading model: the per-vertex math of the game's track VU1 microprograms, as C++ (docs/native-renderer.md
// §11). The GPU shaders (src/b3gfx/shaders/mesh.vert/.frag) implement the same formulas; tools/render/b3vucheck.cpp
// runs the microprograms in the VU1 interpreter (include/b3asset/vu1.h) on real batches and compares them with this.
#ifndef B3ASSET_TRACKSHADE_H
#define B3ASSET_TRACKSHADE_H

#include <cstdint>
#include <map>
#include <vector>

#include "b3asset/track.h"
#include "b3asset/vif.h"

namespace b3asset {

// What the EE side knows when it draws the track: the camera (as the RW view matrix the programs read: x/z and y/z in
// [0,1] across the view window), the GS window mapping, the sun direction and the material animation state.
struct ShadeScene {
    float view[4][3] = {};      // rows R0..R3, row-vector convention: (vx, vy, vz) = p * R + R3 [C CB3CameraData +0x120]
    // VU1 1018 = vu1DataXYZScale (width, height, zScale) with w = xMaxYMax128.z = -255 / (far - fogPlane); 1019 =
    // vu1DataXYZShift + vu1DataOffset3D (2048-centred window, zShift) with w = xMaxYMax128.w = far
    // [C CB3CameraData::Construct 0x225B30 draft; RW 3.1 sky2 _rwSkyPipeSetupForCamera, skyinst.c:2314-2333].
    float scale[4] = {640.0f, 448.0f, 4194304.0f, -255.0f / (1000.0f - 100.0f)};
    float shift[4] = {2048.0f, 2048.0f, 0.0f, 1000.0f};
    float guard[4][4] = {{325.0f, 1.0f / 325.0f, -1.0f, 1000.0f}, {229.0f, 1.0f / 229.0f, 1.001f, 0.5f},
                         {1727.9374f, 1.0f / 1727.9374f, -1.0f, 1000.0f}, {1823.9374f, 1.0f / 1823.9374f, 1.001f, 0.5f}};
    float eye[3] = {}, right[3] = {1, 0, 0};
    float sun[3] = {0, 1, 0};   // environment manager +0xB0[light]: direction towards the sun [L, from the spec math]
    bool uv_scroll_on = false;  // material flag 0x200: u += 1 - frac(t) [C ReallySetMaterial; program 0x0038-0x0054]
    float uv_scroll = 0.0f;
    bool no_specular = false;   // RenderMesh's last argument (VU1 1013.z): type 1 alpha 0, type 2 always texgen
};

// The material constant block ReallySetMaterial sends (VU1 1007-1009).
struct ShadeVectors {
    float c1007[4], c1008[4], c1009[2];
    std::uint32_t c1009_flag;
};
ShadeVectors shade_vectors(const ShadeScene& sc, const TrackMaterialInfo& m);

// One track vertex as a batch carries it (layouts: docs/track-format.md §3.3).
struct TrackVertexIn {
    float pos[3] = {};
    float uv[2] = {};           // V2-16 / 4096
    std::uint8_t rgba[4] = {};  // GS colour bytes (0x80 = 1.0); type 1/2 carry no alpha byte (0)
    std::int8_t n[4] = {};      // type 1: normal x/y/z (s8, |n| ~ 127) + specular intensity; type 2: normal + w
    bool adc = false;           // position w bit 15: this vertex does not close a triangle
};
// What the program sends to the GS for it.
struct TrackVertexOut {
    float u = 0, v = 0;         // texture coordinate (S/Q, T/Q)
    std::uint8_t rgba[4] = {};
    float q = 0;                // 1 / view depth
    float x = 0, y = 0;         // GS window position (pixels, before the 12.4 conversion)
    int fog = -1;               // GS fog byte F for type 3 (lines are the only track primitive with FGE); -1 = unused
    bool drawn = false;         // closes a triangle (vertex >= 2, no ADC from the data or from back-face culling)
};

// Reads a batch's vertices from VU memory (after the VIF unpacks) for material type `type`.
std::vector<TrackVertexIn> track_batch_inputs(const VuMemory& vu, std::uint32_t n, std::uint32_t type);

// The program's math for one batch (fast path: no guard-band clipping), in float.
void shade_track_batch(const ShadeScene& sc, const TrackMaterialInfo& m, const std::vector<TrackVertexIn>& in,
                       std::vector<TrackVertexOut>& out);

// Specular alpha of a type-1 vertex: what the shader computes per vertex (GS alpha units, 0x80 = 1.0).
float track_specular(const float pos[3], const std::int8_t n[4], const float eye_up[3], const float to_sun[3],
                     float strength /* material +0x04 */, float exponent /* material +0x08 */);

// The VU1 microprogram ReallySetMaterial 0x26B2F0 picks for a material (flag 0x20: the variant without back-face culling).
std::uint32_t track_program(const TrackMaterialInfo& m);
// Writes the constants the EE side sends: ReallySetMaterial (1005-1009), RenderMesh (1013: clip flags, no-specular,
// set-up request) and ReallySetupCameraData (1014-1023) for `sc` and material `m`.
void track_vu_constants(const ShadeScene& sc, const TrackMaterialInfo& m, std::uint32_t clip_flags, VuMemory& mem);
// A scene with the camera at `eye` looking at `target`, 60 degrees across a 640x448 window, the game's handedness.
ShadeScene shade_scene_look_at(const float eye[3], const float target[3], const float to_sun[3]);

// Line meshes (material type 3, docs/track-format.md §3.5) on the GPU (docs/native-renderer.md §11.8). The GS draws them
// 1 pixel wide (PRIM LINE with AA1) in a frame of 448 rows; the GPU draws each segment as a quad (track_line_batch)
// widened in screen space, the same rule in every quality preset. mesh.vert / mesh.frag implement these; the test
// checks them here.
constexpr float kLineRows = 448.0f;      // the GS frame's rows: one GS pixel = output rows / 448
constexpr float kLineFogPlane = 5.0f;    // RwCamera fogPlane: RwCameraCreate's default, the game never sets it [L]
// The line's width in output pixels for an output `rows` high (a split-screen view: the whole output's rows).
float track_line_width(float rows);
// Coverage of an output pixel whose centre lies `d` pixels from the centre line of a line `width` pixels wide (box filter;
// the GS's AA1 uses its coverage as As).
float track_line_coverage(float d, float width);
// The program's RW linear fog F (0x476130 0x0038-0x0057, as trackshade.cpp's type 3), as 0..1: 1 up to the fog plane, 0
// at `far`.
float track_line_fog(float w, float far, float fog_plane = kLineFogPlane);
// The screen-space expansion of one track_line_batch vertex. `self` / `other`: the clip-space positions of this vertex's
// end and of the other end; `side` -1 / +1 and `end` 0 / 1 as the vertex carries them; a `vw` x `vh` pixel target. An end
// behind the eye (w < kLineNearW) is moved along the segment onto w = kLineNearW first; when both are, the vertex
// collapses to (0, 0, 0, 1) and false is returned. `out`: the clip position, offset from the centre line by
// width / 2 + 1 pixels across the segment's screen direction (1 pixel for the coverage ramp); `edge`: that signed offset
// in pixels, the value the fragment's coverage reads (interpolated across the quad).
constexpr float kLineNearW = 1e-3f;
bool track_line_expand(const float self[4], const float other[4], float side, float end, float vw, float vh, float width,
                       float out[4], float* edge);

}  // namespace b3asset

#endif
