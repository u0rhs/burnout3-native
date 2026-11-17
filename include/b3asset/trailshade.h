// Skid-mark (trail) shading model: the math of the game's trail VU1 microprogram 0x4771B0 (E map `B3TrailRenderer.dsm`
// [L]) as C++, i.e. what the native renderer must draw for one CB3TrailEmitter. Every operation follows the program's
// float order through the PS2 float model (b3fp::ref), so the results are bit-exact; tools/render/b3vucheck_trail.cpp
// runs the real program in the VU1 interpreter (include/b3asset/vu1.h) and compares.
//
// Inputs, as the EE side uploads them [C CB3TrailRenderer::BeginRender 0x2F38B0, Render 0x2F3600]:
//   VU1 1008..1010  per trail type: CB3TrailRenderer::maDMAParams[type] (D_01E865E0, 5 x 0x30 [L name]), built once by
//                   CB3TrailRenderer::Construct 0x2F3A60 from CB3TrailSystem::mgaDefaultParams (0x49FFF0, 5 x 0x50)
//   VU1 1011.w      "now" (gWorld +0x20 = D_00522680, the clock AddTrailSegment stamps the points with)
//   VU1 1012        GIF tag words 1..3: PRE, PRIM 0x5C (TRISTRIP, IIP, TME, ABE; FST 0 = ST/Q; FGE 0), NREG 3:
//                   ST, RGBAQ, XYZF2
//   VU1 1013        (0xA, 0, 0, 3): set-up request (w != 0) and flags (unused by the trail math)
//   VU1 1014..1023  camera record +0x120 (graphics manager 0x665EC0 + 0x470 + k x 0x230): view rows R0..R3 (lanes x, y,
//                   w; x/w and y/w in [0,1] across the window, w = view depth), 1018 scale (width, height, zScale,
//                   fog scale), 1019 shift (2048-centred window x/y, zShift, fog far), 1020/1021 clip-frame constants
//                   (1022/1023 are not read by this program) [C program 0x0006..0x0016, 0x0063..0x006D]
//   per emitter     TOPS + 2k: point k (xyz world, w time), TOPS + 2k + 1: its s8 x3 side vector (V3-8 signed),
//                   ITOP = point count (emitter +0x10) [C 0x2F3600]
// Tags: [C] seen in code (VU address = pair index of `vudis.py game_004771B0.textbin.bin`) or data; [L] likely; [G].
#ifndef B3ASSET_TRAILSHADE_H
#define B3ASSET_TRAILSHADE_H

#include <cstdint>
#include <vector>

namespace b3asset {

// VU1 1008..1010 (raw words, lane order x y z w):
//   1008 = (w0 / 2, (w1 - w0) / life / 2, 1 / life, 0)   half width at age 0, half-width rate, 1 / lifetime
//   1009 = colour at age 0 (r, g, b, a; GS units, 0x80 = 1.0)
//   1010 = colour at the end of the lifetime - colour at age 0
// [C CB3TrailRenderer::Construct 0x2F3A60; mgaDefaultParams +0x24 w0, +0x28 w1, +0x2C..+0x38 start colour,
//  +0x3C..+0x48 end colour, +0x4C lifetime in seconds].
// Built from one raw mgaDefaultParams record (0x50 bytes, little-endian) with EE FPU arithmetic (b3fp::ref; FPU div.s
// as the float model has it (D4), host round-to-nearest -- capture the table instead when bit-exactness matters).
void trail_vu_params(const std::uint8_t record[0x50], std::uint32_t out[12]);
// GS ALPHA_1 for the type: low byte of record +0x20 = A/B/C/D selectors, FIX = its top byte [C 0x2F3600].
std::uint64_t trail_alpha_reg(std::uint32_t record_word20);

// One emitter as the program reads it: CB3TrailEmitter +0x10 (point count) and the data at +0x8 (16 x V4-32 points:
// world xyz + time; +0x100: 16 x 3 s8 side vectors = unit side x 127.5 truncated, CB3TrailEmitter::AddTrailSegment
// 0x2BCF60) [C 0x2F3600 / 0x2BCF60].
struct TrailEmitterInput {
    int count = 0;                    // s8 at emitter +0x10; the program draws nothing below 2
    std::uint32_t point[16][4] = {};  // float bits: x, y, z, time
    std::int8_t side[16][3] = {};
};
// From guest bytes: the s8 at emitter +0x10 and the 0x130 bytes its +0x8 pointer addresses (the DMA always sends all
// 16 points and 48 side bytes; the program reads `count` of them).
TrailEmitterInput trail_emitter_from_guest(std::int8_t count, const std::uint8_t data[0x130]);

// One ribbon vertex in world space, before projection.
struct TrailVertex {
    float pos[3] = {};          // world position = point +- side x half width
    float s = 0, t = 0;         // texture coordinate: s = point index & 1 (0/1 alternating, one texture repeat per
                                // segment), t = 0 on the "+side" edge, 1 on the "-side" edge; the program sends
                                // (s q, t q, q), i.e. the GPU interpolates s, t perspective-correctly
    std::int32_t colour[4] = {};  // the program's integers (FTOI0 of the colour lerp); the GS takes the low 8 bits
    std::uint8_t rgba[4] = {};
    bool kick = false;          // closes a triangle (strip vertex >= 2); the program never sets ADC in the strip
    int point = 0, edge = 0;    // source point index; 0 = +side edge (first of the pair), 1 = -side edge
};
struct TrailRibbon {
    int points = 0;             // points used (count, minus trailing points not newer than point 0), 0 = nothing
    std::vector<TrailVertex> verts;  // one triangle strip: +edge0, -edge0, +edge1, -edge1, ... (2 x points vertices)
};
// The program's ribbon for one emitter [C 0x002B..0x0057 and 0x0210]. View-independent: no camera input.
TrailRibbon trail_ribbon(const std::uint32_t params[12], float now, const TrailEmitterInput& e);

// ---- projection, as the program does it (for the reference check; the GPU does its own) ----
// The camera block VU1 1014..1023 as raw words (camera record +0x120, 10 qw).
struct TrailCamera {
    std::uint32_t row[10][4] = {};
};
// One vertex as the program sends it to the GS (fast path) or hands it to its clipper.
struct TrailGsVertex {
    float clip[4] = {};         // homogeneous clip coordinates (x, y, w, w): x, y in pixels from the window centre x w
    std::uint32_t clip_flags = 0;  // CLIP judgement of the clip-frame test vector (bit 0 +x ... bit 5 -z)
    std::uint32_t x = 0, y = 0;    // XYZF2 x, y (12.4 fixed point, 2048-centred)
    std::uint32_t z = 0;           // 24-bit Z
    std::uint8_t f = 0;            // fog byte (FGE is off for trails, so the GS ignores it)
    float s = 0, t = 0, q = 0;     // ST register (s q, t q) and Q = 1 / w
    std::uint8_t rgba[4] = {};
    bool adc = false;
};
enum class TrailDraw { kNothing, kCulled, kDirect, kClipped };
// Projects the ribbon. kDirect: every vertex inside the clip frame -> `out` is exactly the program's GIF strip (one
// XGKICK, NLOOP = 2 x points). kCulled: all vertices outside one common plane -> no output. kClipped: the program
// clips each strip triangle (its TSETrueClipper) and sends new triangles; `out` holds the clipper's input (every
// strip vertex projected, with its clip flags), not the GS output.
TrailDraw trail_project(const TrailCamera& cam, const TrailRibbon& r, std::vector<TrailGsVertex>& out);

}  // namespace b3asset

#endif
