// Corona shading model: the math of the game's corona VU1 microprogram 0x478260 (E map `Corona.dsm` [L]) as C++, i.e.
// what the GS receives for the corona slots CB3EffectsManager writes (car lights, traffic lights, the boost glow;
// docs/native-renderer.md §16). Every operation follows the program's float order through the PS2 float model
// (b3fp::ref), so the results are bit-exact; tools/render/b3vucheck_corona.cpp runs the real program in the VU1
// interpreter (include/b3asset/vu1.h) and compares.
//
// Inputs, as the EE side uploads them [C CB3CoronaRenderer::UploadCodeAndStaticData 0x306E80 (matched), EndRender 0x3070B0,
// CB3CoronaDynamicArray::ConstructDMAData 0x3076D0 (L draft)]:
//   VU1 1015..1018  camera record +0xE0 rows R0..R3 (graphics manager 0x665EC0 + 0x470 + 0x230 k), lanes x, y, z with
//                   w = z (CGtV3dPlus::SetPlus(ZAxisType)): the rows whose x/w, y/w run 0..1 across the view window
//   VU1 1019        (+0x218 + 64, +0x21C + 64, +0x198, +0x188): window width / height + 64 (a 32-pixel guard band each
//                   side), the clip-z scale and offset (near 0.3 / far 10000 for the race camera)
//   VU1 1020        (+0x170, +0x174, +0x178, +0x168): the 2048-centred window x / y, zShift, zScale (as the track's
//                   1019 / 1018)
//   VU1 1021        (+0x218, +0x21C, +0x200, +0x204): window width, height, the view window's half width / height at
//                   distance 1 (CGtViewport +0x48 / +0x4C)
//   VU1 1022        (8, -, +0x208, -): x = the colour threshold, z = K (1 / half width): a slot's fade distance scale
//   VU1 1023        GIF tag words 1..3: PRE, PRIM 0x56 (SPRITE, TME, ABE; FST 0 = ST/Q), NREG 5: ST RGBAQ XYZF2 ST XYZF2
//   per batch       up to 72 slots, 2 qw each (position xyz / fade distance w, colour x 64 xyz / size w) at TOPS,
//                   ITOP = the batch's slot count; the program draws them from VU 288 / 649 (double buffer)
// Per slot [C program, pair addresses of `vudis.py game_00478260.textbin.bin`]:
//   cull (0x0027..0x0059): the clip position c = (R1015..1018) p; kept when |c.x|, |c.y|, |c.z| <= |c.w| (the window
//     plus 32 pixels each side, 0.3 <= depth <= 10000) and K x p.w > |c.w| (the slot's fade distance; unused slots
//     have w = 0, EndUpdate 0x307440)
//   fade (0x005A..0x0087): colour' = colour - colour x (c.w / (K p.w)); dropped unless a channel of colour' is > 8;
//     depth' = c.w - size / 2 (the sprite's Z is taken half its size nearer)
//   sprite (0x0088..0x00A7): centre = shift + (c.x, c.y) / c.w x (width + 64, height + 64) / 2, half extent =
//     (width / 4 / half_w, height / 4 / half_h) x size / c.w, i.e. a square `size` metres across at the slot;
//     Z = zShift + zScale / depth' -+ size / c.w (the corners), ST (0, 0) -> (1, 1) with Q = half extent x / size
//     ratio, RGBA = FTOI0(colour') with alpha 0x80
// Tags: [C] seen in code (VU address = pair index) or data; [L] likely; [G].
#ifndef B3ASSET_CORONASHADE_H
#define B3ASSET_CORONASHADE_H

#include <cstdint>
#include <vector>

namespace b3asset {

// VU1 1015..1023 as raw words (lane order x y z w).
struct CoronaCamera {
    std::uint32_t row[9][4] = {};
};
// UploadCodeAndStaticData 0x306E80's block from one camera record (0x230 bytes, little endian) [C matched]. Lanes the
// function leaves as they were (1022.y / .w, 1023 x) are whatever its vector local held: 1022.y = 1021.y, 1022.w = 1021.w
// (the same local, only x / z set) and 1023 = (0, 0x502B4000, 0x42412, 0) (a constant quad).
CoronaCamera corona_camera(const std::uint8_t record[0x230]);

// One slot as raw words: position x y z, fade distance; colour x 64 r g b, size.
struct CoronaSlotWords {
    std::uint32_t w[8] = {};
};

// One sprite as the program sends it to the GS.
struct CoronaSprite {
    int slot = 0;                 // index of the slot in the array (batch order)
    std::uint32_t x[2] = {}, y[2] = {}, z[2] = {};   // the two XYZF2: x / y 12.4 (low 16 bits), z 24 bits
    std::uint32_t st[2][3] = {};  // the two ST writes (S, T, Q as float bits)
    std::int32_t col[3] = {};     // FTOI0 of colour' (the GS takes the low 8 bits)
    std::uint8_t rgba[4] = {};
    // the program's intermediate values, for the GPU path's check (float bits)
    std::uint32_t depth = 0;      // c.w (view depth)
    std::uint32_t pulled = 0;     // c.w - size / 2
};
// The program on one array's slots (all of them: the VU program runs over every slot the DMA sends, in batches of 72).
std::vector<CoronaSprite> corona_sprites(const CoronaCamera& cam, const std::vector<CoronaSlotWords>& slots);

}  // namespace b3asset

#endif
