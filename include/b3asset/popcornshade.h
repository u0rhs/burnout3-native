// Popcorn shading model: the math of the game's popcorn VU1 microprogram 0x47C6F0 (the image's first 1728 B; E map
// `Popcorn.dsm` [L]) as C++, i.e. what the GS receives for the four CB3PopcornArray (gEffectsManager 0x1D9D200 + 0x20F0
// + 0x40 a), drawn only while the view player's crash flag (gWorld + 0x27D0 p + 0x12C63A) is set [C RenderPostFogEffects
// 0x261E00]. Every operation follows the program's float order through the PS2 float model (b3fp::ref), so the results
// are bit-exact; tools/render/b3vucheck_popcorn.cpp runs the real program in the VU1 interpreter and compares. Pair
// addresses are those of `python3 -I tools/render/vudis.py` on the image's first 1728 bytes [C listing].
//
// What a popcorn particle is [C program + EE code]: a ballistic point (the same 0x90 record blocks as the sparks, one
// ground bounce solved on the CPU at spawn, CB3PopcornArray::SpawnBouncyPopcorn 0x365E80) drawn as ONE GS SPRITE at
// "now" (no motion blur, no camera history): centre = p + v age + (0, g/2, 0, g/2) age^2 with y = max(y, w); half
// extent = size x sqrt(depth) in view-space metres (so 1 / sqrt(depth) on screen, not perspective); colour = (c0 + (c1
// - c0) age^2 / L^2) x (1 + (fade - 1) depth / far) per channel (fade = params +0x34: RGB 1, alpha 0.01: alpha fades
// to 1 % at the far clip); ST (0,0)-(1,1), mirrored on every other sprite (see PopcornStState for the batch seams).
//
// Inputs, as the EE side uploads them [C asm unit_00365920.s, CB3PopcornArray::Render 0x365920 (nonmatching, every
// store traced); src/nonmatching/Render__15CB3PopcornArrayFv.cpp]: one UNPACK V4-32 x 16 to VU 1008 (row S[k] = VU 1008+k):
//   S0..S3   the current RwCamera's (ourGlobals 0x670BD0 +0 ->) view matrix +0x20 rows right/up/at/pos as (m.x, m.y, z,
//            m.z): lanes x, y = window x / y times depth (0..1 across the window), w = depth; z = zB, zA, far', far'
//            (zA = (n + f') / (f' - n), zB = -2 n f' / (f' - n), n = camera +0x80, f' = min(camera +0x84, params +0x44))
//   S4..S6   vu1DataXYZScale 0x49AF40, vu1DataXYZShift 0x49AF50, vu1DataOffset3D 0x49AF60 (RW sky globals, raw)
//   S7       (0, array +0x10 (= params +0x48, the gravity term), 0, now = D_00522680)
//   S8       (camera +0x70, +0x74 (1 / view window), params +0x10 x (0.2 + camera +0x68), params +0xC = lifetime L)
//   S9, S10  params +0x14 / +0x24 colours x 128 (c0, c1)
//   S11, S12 params +0x34 row (the depth fade target), sent twice
//   S13      GIF tag (lo 0x402B400000000000, hi 0x4124): PRE, PRIM 0x56 (SPRITE, TME, ABE; FST 0), NREG 4: XYZF2, ST,
//            RGBAQ, XYZF2 (the first corner takes the previous sprite's ST: the program alternates ST (1,1) / (0,0))
//   S14      (0, 0, 1, 1); S15 (0, 0, D_0052267C (the frame step), 1): y = 0 passes after the first (vi03), so the
//            frame step is unused
//   then a DMA CALL of the array's static chain (CB3PopcornArray::Construct 0x366160, matched): BASE 0, OFFSET 0xB4,
//   per batch of <= 20 blocks: STCYCL 1,1; UNPACK V4-32 9n to TOPS; ITOP = n; MSCALF 0 (first batch: the set-up, which
//   also rewrites VU 1017 / 1018 in place) or MSCNT.
//   GS (DIRECT before the upload, A+D): ALPHA_1 = array +0x0 (_gu64PopcornBlendModes 0x4C7A20 [params +0x8] = {0x44, 0x48}:
//   0x48 additive for array 0 "fxpopcornspark", 0x44 (Cs - Cd) As + Cd for 1-3 "fxpopcorndebris"), TEST_1 0x53001 (alpha NEVER + AFAIL RGB_ONLY: no alpha / Z write;
//   ZTST GEQUAL), CLAMP_1 5, ST 0, RGBAQ Q 1.0; TEX0 RW's for array +0x2C (the raster named params +0x0); afterwards
//   TEST / ALPHA / CLAMP back to the sky globals skyTest_1 0x4E1840, skyAlpha_1 0x4E1868, skyClamp_1 0x4E1858.
// Program, per batch (pairs 0x003E..0x0089): particle j of a block is drawn iff ts - now < 0 (S flag) and ts - (now - L)
//   >= 0 (no S flag), and its clip position (S0..S3 rows, x and y made 2x - w, z = zA w + zB) is inside |x|,|y|,|z| <=
//   w. KNOWN PROGRAM BUG [C, run: popcorn_vu_check]: in the prologue pair 0x0046 loads block 0's particle-1 position
//   into vf06 (overwritten at 0x0049 by its velocity), so particle 1 of the FIRST block of every batch uses whatever
//   vf05 holds: the position the previous batch's last loop iteration prefetched from past its end (0x0061), i.e. VU
//   memory TOP_prev + 9 n_prev + 3. On the PS2 (TOPS double buffer 0 / 180) that is the next batch's own block 0
//   particle 1 when the previous batch was full, sat in buffer 0 and the VIF had already unpacked the next one (then
//   it is right), else stale input or a word of the output buffer at 360+ [L timing-dependent]. popcorn_particles
//   takes that vf05 value as an argument; a renderer should pass none (draw the particle where it is).
// Tags: [C] seen in code (asm / matched source / the program, checked by running it) or data; [L] likely; [G] guess.
#ifndef B3ASSET_POPCORNSHADE_H
#define B3ASSET_POPCORNSHADE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace b3asset {

// ---- EE side ----
// CB3PopcornArrayParams (maStandardParams 0x4A4DB0, 4 x 0x50) [C 0x366160, 0x365920, 0x365E80]: +0x0 texture name
// (pointer), +0x4 capacity, +0x8 blend mode index, +0xC lifetime L, +0x10 size, +0x14 / +0x24 colours (1.0 = 0x80 in
// all four lanes), +0x34 depth fade row, +0x44 draw distance (far clip), +0x48 gravity term (array +0x10), +0x4C bounce
// (array +0x18 = -it).
struct PopcornParams {
    std::uint32_t w[20] = {};
    float f(int off) const;
};
PopcornParams popcorn_params_from_guest(const std::uint8_t p[0x50]);

// The RwCamera fields Render reads (camera = *(u32*)ourGlobals 0x670BD0) [C asm].
struct PopcornCamera {
    std::uint32_t view[4][3] = {};   // +0x20 view matrix rows (right, up, at, pos), lanes x y z
    std::uint32_t window_x = 0;      // +0x68
    std::uint32_t recip_x = 0, recip_y = 0;   // +0x70, +0x74
    std::uint32_t near_ = 0, far_ = 0;        // +0x80, +0x84
};
PopcornCamera popcorn_camera_from_guest(const std::uint8_t cam[0x88]);

// VU1 1008..1023 as uploaded (q[0] = VU 1008).
struct PopcornVuStatic {
    std::uint32_t q[16][4] = {};
};
// `array_g` = array +0x10; scale/shift/offset = vu1DataXYZScale/Shift/Offset3D; `now` = D_00522680, `step` = D_0052267C.
PopcornVuStatic popcorn_vu_static(const PopcornParams& p, const PopcornCamera& c, std::uint32_t array_g,
                                  const std::uint32_t scale[4], const std::uint32_t shift[4], const std::uint32_t offset[4],
                                  std::uint32_t now, std::uint32_t step);

// One batch of the array's chain [C Construct 0x366160]: `blocks` blocks from block `first`; ITOP = blocks.
struct PopcornBatch {
    int first = 0, blocks = 0;
    bool mscalf = false;
};
std::vector<PopcornBatch> popcorn_batches(int capacity);
bool popcorn_batches_from_chain(const std::uint8_t* chain, std::size_t bytes, std::uint32_t rec_base, std::vector<PopcornBatch>& out);
std::uint64_t popcorn_alpha(const std::uint64_t blend_modes[], const PopcornParams& p);   // array +0x0 low word
constexpr std::uint64_t kPopcornTest1 = 0x53001, kPopcornClamp1 = 5;
constexpr std::uint32_t kPopcornPrim = 0x56;

// ---- the program ----
// The set-up's derived constants (pairs 0x0000..0x0034), shared by the two stages below.
struct PopcornSetup {
    std::uint32_t row[4][4] = {};   // clip rows: (2 m.x - m.z, 2 m.y - m.z, zA m.z (+ zB for the pos row), m.z)
    std::uint32_t half_scale[2] = {}, zscale = 0;   // vu1DataXYZScale x, y halved; z
    std::uint32_t shift[3] = {};                    // shift + offset (x, y), shift z
    std::uint32_t grav[4] = {};                     // (0, g/2, 0, g/2) from S7
    std::uint32_t now = 0, dead = 0;                // now, now - L
    std::uint32_t half_px[2] = {};                  // (recip_x, recip_y) x size x half_scale: half extent x depth^-1/2
    std::uint32_t size = 0;                         // S8.z: the view-space half extent at depth 1
    std::uint32_t c0[4] = {}, dc[4] = {};           // colour at age 0, (c1 - c0) / L^2
    std::uint32_t fade[4] = {};                     // (S11 - 1) / far'
    std::uint32_t st0[2] = {}, st_base[2] = {};     // vf21.xy at the set-up, vf20.xy (the flip: vf21 = base - vf21)
};
PopcornSetup popcorn_setup(const PopcornVuStatic& st);

// One particle at `now`, view-independent (world space; GS colour units).
struct PopcornParticle {
    int index = 0;                  // block x 4 + lane within the batch
    std::uint32_t pos[4] = {};      // world centre (y = max(y, w)); w = the post-bounce parabola's y
    std::uint32_t age2 = 0;         // (ts - now)^2
    std::uint32_t colour[4] = {};   // c0 + dc x age2 (before the depth fade), GS units (0x80 = 1.0)
};
// Pairs 0x003E..0x0058 (alive test and position) for one batch. `stale_p1`: the vf05 the program uses for particle 1
// of block 0 (see the header note), or nullptr for its own position.
void popcorn_particles(const PopcornSetup& s, const std::uint32_t* blocks, int nblocks, const std::uint32_t* stale_p1,
                       std::vector<PopcornParticle>& out);
// View-dependent helpers for a renderer (same float order): depth fade factor per channel at view depth d, and the
// half extent in view-space metres at depth d (size x sqrt(d)).
void popcorn_fade(const PopcornSetup& s, std::uint32_t depth, std::uint32_t out[4]);
// The sprite's half extent in view-space metres at view depth d (x and y alike): size x sqrt(d). The program builds it in
// pixels as half_px x sqrt(d) / d with half_px = (1 / view window) x size x scale / 2, i.e. exactly this once projected
// (host float: for drawing; the GS words come from popcorn_project).
float popcorn_view_half_extent(const PopcornSetup& s, float depth);

// One sprite as the GS receives it.
struct PopcornSprite {
    int index = 0;
    std::uint32_t clip[4] = {};
    std::uint32_t xyz[2][3] = {};   // FTOI4 words of the two corners (top-left, bottom-right): X, Y low 16 bits, Z >> 4
    std::uint32_t st[2][2] = {};    // float bits of each corner's ST (corner 0's is the previous sprite's)
    std::int32_t rgba[4] = {};      // FTOI0 words (the GS takes the low 8 bits)
};
// The ST state carried across the batches of one array: the program's vf21.xy and the GS ST register. Each batch with
// a sprite flips vf21 once more than it draws (0x009B in the prologue, then once after every stored sprite: the loop is
// pipelined) [C run], so the first sprite of every batch after the first has the same ST on both corners (one texel
// row/column stretched over it: a GS-visible quirk); within a batch the sprites alternate (0,0)-(1,1) / (1,1)-(0,0).
struct PopcornStState {
    std::uint32_t vf21[2] = {}, gs[2] = {};
};
PopcornStState popcorn_st_start(const PopcornSetup& s);   // per array: vf21 = S14.xy, GS ST = 0 (Render's A+D ST)
// Pairs 0x0054..0x00C9: clip test and sprites, in the order the program packs them.
void popcorn_project(const PopcornSetup& s, const std::vector<PopcornParticle>& parts, PopcornStState& st,
                     std::vector<PopcornSprite>& out, std::vector<int>* culled = nullptr);

// ---- VIF streams (DMA tags dropped), for the interpreter check ----
std::vector<std::uint8_t> popcorn_vif_static(const PopcornVuStatic& st);
std::vector<std::uint8_t> popcorn_vif_batch(const PopcornBatch& b, const std::uint32_t* blocks);

}  // namespace b3asset

#endif
