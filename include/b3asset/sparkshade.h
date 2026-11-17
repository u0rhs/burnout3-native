// Spark shading model: the math of the game's spark VU1 microprogram 0x46D090 (1728 B; E map `Spark.dsm` [L]) as C++,
// i.e. what the GS receives for the six CB3SparkArray (gEffectsManager 0x1D9D200 + 0x21F0 + 0xA8 a). Every operation
// follows the program's float order through the PS2 float model (b3fp::ref), so the results are bit-exact;
// tools/render/b3vucheck_spark.cpp runs the real program in the VU1 interpreter (include/b3asset/vu1.h) and compares.
// Pair addresses below are those of `python3 -I tools/render/vudis.py game_0046D090.textbin.bin` [C listing].
//
// What a spark is [C program + EE code]: a ballistic point (spawn position p, velocity v, gravity g on y only; one ground
// bounce solved on the CPU at spawn, CB3SparkArray::SpawnBouncySpark 0x263C40) drawn as a motion-blurred streak: the
// renderer keeps a history of 18 camera frames (CSparkFrameData, CB3SparkRenderer::BeginRender 0x2638C0), and for each
// pair of consecutive frames k, k+1 (k = 0 .. i-1, frame 0 = now) the program draws one camera-facing quad from the
// spark's position at t_k projected with frame k's camera to its position at t_k+1 projected with frame k+1's camera.
// So a streak is up to i (<= 17) quads, each in its own pair of views (that is the motion blur: camera motion streaks
// too). The quad's half width is a world-space distance (params +0x58: 0.017 m for array 0, 0.125 m for 1-5) applied
// along the normal of the plane through the eye and both ends, in view space. Texture s runs with the age of the frame
// (0 at now, 1 at params +0x5C = 0.049 s ago, CLAMP beyond), t is 0 / 0.875 across. Colour: one flat RGBAQ per ring.
//
// Inputs, as the EE side uploads them [C asm / matched source unless noted]:
//   VU1 912        (+0x6C, +0x70, +0x78, (+0x74 - +0x6C) / +0x5C) of the array's params (CB3SparkArrayParams,
//                  CB3SparkArrayBase::mgaArrayParams 0x4C7710, 6 x 0x7C): s at age 0, t edge 0, t edge 1, ds/dt
//                  [UploadStaticDataAndCallCode 0x263420, nonmatching but every store checked against unit_00263420.s]
//   VU1 913        (i (int), array +0xA0, params +0x58, params +0x50): the frame-interval count, the lifetime unit
//                  A0 = max lifetime / 1023 (CB3SparkArray::Construct 0x264040, D_004E1160 = 0.00097751711), the half
//                  width, gravity (-29.4 for all six)
//   VU1 914        camera record (graphics manager 0x665EC0 + 0x470 + 0x230 x D_00667050) (+0x170, +0x174, +0x178,
//                  +0x168): the 2048-centred window x / y, zShift, zScale (as the track's 1019 / 1018)
//   VU1 915        D_01E31A20 = GIF tag (lo 0x802A400000000000, hi 0x42424242: PRE, PRIM 0x54 = TRISTRIP, TME, ABE,
//                  IIP 0 (flat), FST 0 (ST/Q), FGE 0; NREG 8: ST XYZF2 x 4), CB3SparkRenderer::Construct 0x263C10
//                  (matched); the program copies words 1..3 to its two output buffers (198, 551) and sets NLOOP
//   VU1 916..1023  D_01E31A30[mnRenderPlayer] (2 x 18 x 0x60 bss): 18 CSparkFrameData of 6 qw: rows 0..3 = a clip
//                  matrix (clip = p R0..R2 + R3, |x|,|y|,|z| <= w = inside), row 4 = (half_w / sx, half_h / sy,
//                  W / 2 / half_w, H / 2 / half_h), row 5.x = the frame's time; written by CSparkFrameData::Set 0x263120
//                  (nonmatching; see spark_frame_set) from camera record +0xE0 rows, +0x18C (far), +0x19C (near, min
//                  0.012), +0x200 / +0x204 (view window half width / height), +0x208 / +0x20C, +0x218 / +0x21C (window
//                  width / height), at BeginRender 0x2638C0 (matched) once per render: f[17..1] = f[16..0] when
//                  now - f[1].t >= 1/96 s, then f[0] = Set(current camera, now) (other paths: replay D_0051BA94 != -1
//                  rebuilds all 18 from the current camera at now - k x D_0051BAC8 when the player struct
//                  D_004EE040 + 0xBB0 p +0x29980 bit 0 is set, else leaves them; player +0x299A6 != 0 sets all 18 to
//                  (current camera, now); func_00131F60 true (crash mode [G]) moves f[1..17].t back by D_0051BAC8 / 2)
//                  The whole block 912..1023 is one UNPACK V4-32 x 112 to 0x390 (the bool argument is always true:
//                  EndRender 0x2636B0, matched; false would send 912/913 only)
//   per batch      (the bank's static DMA chain, CSparkBank::Construct 0x264170, nonmatching [L every word accounted
//                  for]; called by a DMA CALL from the upload): per ring a FLUSHE + DIRECT GIF A+D RGBAQ = the ring's
//                  colour (r x 64 + 0.5, g x 64 + 0.5, b x 64 + 0.5, a x 128 + 0.5 truncated, Q 1.0; params +0x10 r),
//                  then per batch of <= 11 blocks: STCYCL 1,1; UNPACK V4-32 9n to TOPS (a DMA REF to the ring's blocks);
//                  ITOP = n | packed lifetime (int(1023.999 / max life x ring life) & ~0xF); MSCALF 0 for the normal
//                  bank's first batch (the program's set-up), MSCNT otherwise
//   per block      0x90 = 9 qw: qw 0 = the four sparks' spawn times (lane j = spark j; -9999 when never used,
//                  CSparkBank::Prepare 0x2640E0), then per spark (position, velocity) [C SpawnBouncySpark 0x263C40]:
//                  position (x, y, z, w = the y of the post-bounce parabola at the spawn time), velocity (x, y, z,
//                  w = the post-bounce y velocity as if from the spawn time); no bounce: w = y in both
// Which arrays / banks [C RenderSparkArray 0x2637F0, EndRender 0x2636B0, matched]: an array is drawn when its normal
// bank's last spawn time (+0x44) > now - max lifetime, or mbIsCrash (= the view player's crash flag, gWorld + 0x27D0 p
// + 0x12C63A, RenderPostFogEffects 0x261E00) and the crash bank's (+0x90) is; it then always sends its normal bank and,
// when the second test held (+0xA4), the crash bank. Every block of every ring is sent (the chain is static); the
// program drops the dead ones.
// Program, per batch and per interval k (frames k, k+1, t_k > t_k+1) [C 0x000B..0x0046]:
//   L = itof(ITOP) x A0 (the ring lifetime, quantised: 1019 x 2/1023 = 1.992 s for array 0 ring 0's full batches);
//   a spark is used iff ts > t_k - L and ts < t_k+1 (both strictly, as MAC Z|S flags of float differences): alive at
//   the newer end, born before the older end; head = p + v a + (0, g/2, 0, g/2) a^2 at a = t_k - ts, tail the same at
//   t_k+1 (with the linear term as v (a - (t_k - t_k+1))), y = max(y, w) at both ends (the bounce) [0x0026, 0x004F];
// then [C 0x0047..0x0071] clip both ends (head with frame k, tail with frame k+1): dropped if either is outside;
// then [C 0x0072..0x00CA] view-space ends A = (c.x row4.x, c.y row4.y, c.w) (frame k / k+1), n = (B - A) x B scaled to
//   length +0x58 (RSQRT), corners A + n, A - n, B + n, B - n; pixel = shift + corner.xy x row4.zw / corner.z, Z =
//   zShift + zScale / corner.z (ERCPR for corners 0 and 3, DIV for 1 and 2); ST (s_k, t0) (s_k, t1) (s_k+1, t0)
//   (s_k+1, t1) with s_k = +0x6C + (t_0 - t_k) x rate, Q = 1.0; ADC on corners 0 and 1 (each spark its own two
//   triangles in the one strip); one XGKICK per (batch, interval) with at least one quad.
// GS state [C SetRenderstates 0x2633A0, matched]: TEST_1 0x71001 (alpha test NEVER with AFAIL FB_ONLY: RGBA written,
// no Z write; ZTST GREATER), ALPHA_1 0x48 (Cs x As + Cd: additive weighted by alpha), CLAMP_1 5 (CLAMP s and t);
// RestoreRenderstates 0x263350 sends an empty A+D packet. TEX0/TEX1: RpSkyTexCacheAccessRaster(D_01E327B0[0] +0x9C, 0)
// in EndRender: the raster of the FIRST renderable array (all six use "fxspark" [C params +0x68]), RW's TEX0 from the
// raster extension (TFX MODULATE [L RW 3.1 basky.c]; TCC per raster). PRIM 0x54 from the GIF tag. RGBAQ per ring as above.
// Tags: [C] seen in code (asm / matched source / the program) or data; [L] likely; [G] guess.
#ifndef B3ASSET_SPARKSHADE_H
#define B3ASSET_SPARKSHADE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace b3asset {

// ---- EE side ----
// CB3SparkArrayParams (0x7C bytes, little-endian words) [C offsets from 0x263C40, 0x264170, 0x264040, 0x263420]:
//   +0x00..+0x3F four RGBA colours (floats, 1.0 = 0x40 in RGB, 0x80 in A), +0x40..+0x4C four ring lifetimes (s),
//   +0x50 gravity, +0x54 bounce factor, +0x58 half width (m), +0x5C motion-blur time (s), +0x60 / +0x64 normal / crash
//   bank capacity, +0x68 texture name (pointer), +0x6C s at age 0, +0x70 t edge 0, +0x74 s at the motion-blur time,
//   +0x78 t edge 1.
struct SparkParams {
    std::uint32_t w[31] = {};
    float f(int off) const;
};
SparkParams spark_params_from_guest(const std::uint8_t p[0x7C]);

// One CSparkFrameData (0x60 bytes): row[0..3] clip rows, row[4] screen constants, row[5][0] = time.
struct SparkFrame {
    std::uint32_t row[6][4] = {};
};
SparkFrame spark_frame_from_guest(const std::uint8_t p[0x60]);
// CSparkFrameData::Set 0x263120 from a camera record (0x230 bytes) and a time, in the asm's operation order (EE FPU
// for the scalars: div.s as b3fp::ref models it, host round-to-nearest; VU0 macro ops for the rows) [L: the function is
// nonmatching; the order is read from unit_00263120.s]. `prev` = the record's previous contents (lanes Set does not
// write: row[5][1..3]).
SparkFrame spark_frame_set(const std::uint8_t cam[0x230], float t, const SparkFrame& prev = SparkFrame());

// UploadStaticDataAndCallCode's frame-interval count i [C asm 0x263434..0x2634B0]: i = 1; while i < 17 and
// f[i].t - f[0].t < blur (always, as times fall with i) and f[i].t != f[i+1].t: ++i. So i = 17 once the history is
// full, fewer while it still holds copies of one time (Prepare 0x263B80 sets all 18 times to now).
int spark_intervals(const SparkFrame hist[18], std::uint32_t blur_time);

// VU1 912..1023 as uploaded (q[0] = VU 912).
struct SparkVuStatic {
    std::uint32_t q[112][4] = {};
    const std::uint32_t* frame_row(int k, int r) const { return q[4 + 6 * k + r]; }
};
// `a0` = array +0xA0; `cam914` = camera record (+0x170, +0x174, +0x178, +0x168); `tag` = D_01E31A20 words.
SparkVuStatic spark_vu_static(const SparkParams& p, std::uint32_t a0, int intervals, const std::uint32_t cam914[4],
                              const std::uint32_t tag[4], const SparkFrame hist[18]);
void spark_cam914_from_record(const std::uint8_t cam[0x230], std::uint32_t out[4]);

// One batch of the bank chain: `blocks` blocks of ring `ring` from block `first`.
struct SparkBatch {
    int ring = 0, first = 0, blocks = 0;
    std::uint32_t itop = 0;      // n | packed lifetime (10 bits as XITOP reads it)
    bool mscalf = false;         // MSCALF 0 (program set-up) instead of MSCNT
    std::uint64_t rgbaq = 0;     // the ring's RGBAQ (sent by DIRECT before its first batch)
};
// The chain CSparkBank::Construct 0x264170 builds, rebuilt from the params (EE FPU arithmetic, b3fp::ref) [L: nonmatching
// source, every word accounted for]. `ring_capacity` (bank slot +4) may be given to use the game's own capacities.
std::vector<SparkBatch> spark_bank_batches(const SparkParams& p, bool crash_bank, const int* ring_capacity = nullptr);
// The same, parsed from the captured chain head [bank +0x48, ring 0 base) and the four ring bases (bank slot +0).
// Returns false when it does not have Construct's shape.
bool spark_bank_batches_from_chain(const std::uint8_t* chain, std::size_t bytes, const std::uint32_t ring_base[4],
                                   std::vector<SparkBatch>& out);
std::uint64_t spark_rgbaq(const SparkParams& p, int ring);   // _SparkSetRGBAQ [C E map name, asm 0x264170]

// RenderSparkArray 0x2637F0's test [C matched]: normal bank drawn (returned), `crash_bank` = the +0xA4 flag.
bool spark_array_renderable(const SparkParams& p, std::uint32_t now, std::uint32_t last_normal, std::uint32_t last_crash,
                            bool is_crash, bool* crash_bank);

// GS registers [C SetRenderstates 0x2633A0, Construct 0x263C10].
constexpr std::uint64_t kSparkTest1 = 0x71001, kSparkAlpha1 = 0x48, kSparkClamp1 = 5;
constexpr std::uint32_t kSparkPrim = 0x54;

// ---- the program ----
// One spark in one interval, view-independent: world positions of the two ends (lanes x y z, w = the post-bounce y;
// y already max(y, w)).
struct SparkSegment {
    int spark = 0;               // index within the batch: block x 4 + lane
    std::uint32_t head[4] = {};  // at t_k (newer end)
    std::uint32_t tail[4] = {};  // at t_k+1
};
// 0x000B..0x0046: the used sparks of one batch in interval k (blocks = 9 qw each, raw words).
void spark_segments(const SparkVuStatic& st, int interval, std::uint32_t itop, const std::uint32_t* blocks, int nblocks,
                    std::vector<SparkSegment>& out);
// The texture coordinates of interval k's corners: s at the head (frame k) and tail (frame k+1), t on the two edges.
void spark_st(const SparkVuStatic& st, int interval, std::uint32_t s[2], std::uint32_t t[2]);

// One GS vertex as the program sends it (PACKED ST + XYZF2).
struct SparkGsVertex {
    std::uint32_t xyz[3] = {};   // FTOI4 words of x, y, z: GS X = xyz[0] & 0xFFFF, Y = xyz[1] & 0xFFFF, Z = (xyz[2] >> 4)
                                 // & 0xFFFFFF (PACKED XYZF2), F = 0
    std::uint32_t s = 0, t = 0, q = 0x3F800000u;  // float bits
    bool adc = false;
};
struct SparkQuad {
    int spark = 0;
    std::uint32_t clip_head[4] = {}, clip_tail[4] = {};   // clip coordinates (frame k, frame k+1)
    std::uint32_t corner[4][3] = {};   // view-space corners (x, y, depth) before the divide: A + n, A - n, B + n, B - n
    SparkGsVertex v[4];
};
// 0x0047..0x00CA: clip, the quads and their GS vertices for one interval (in the order the program packs them).
// `screen` false (perf1: the native renderer, which uses only the corners and ST): the GS vertices' XYZ (four divides
// per quad) are left at zero; everything else is the same.
void spark_project(const SparkVuStatic& st, int interval, const std::vector<SparkSegment>& segs, std::vector<SparkQuad>& out,
                   std::vector<int>* culled = nullptr, bool screen = true);

// ---- VIF streams the EE side sends (DMA tags dropped), for the interpreter check ----
std::vector<std::uint8_t> spark_vif_static(const SparkVuStatic& st);
// One batch: STCYCL 1,1; UNPACK V4-32 9n -> TOPS; ITOP; MSCALF 0 / MSCNT. (STMOD 0, FLUSHE and the DIRECT RGBAQ are left
// out: the interpreter skips DIRECT; the colour is SparkBatch::rgbaq.)
std::vector<std::uint8_t> spark_vif_batch(const SparkBatch& b, const std::uint32_t* blocks);

}  // namespace b3asset

#endif
