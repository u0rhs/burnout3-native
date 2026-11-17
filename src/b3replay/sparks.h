// b3replay sparks (fx2, docs/native-renderer.md §19.2): the six CB3SparkArray drawn the game's way from the captured
// state (ContactFx: array heads, params, ring blocks, the static DMA chains and the 18-frame camera history). The quads
// are the spark VU1 program's (b3asset/sparkshade.h, bit-exact against the program in the interpreter: b3vucheck_spark):
// per spark and history interval k a camera-facing quad from its position at t_k (seen by frame k's camera) to t_k+1
// (frame k+1's), the motion-blurred streak. The program's view-space corners are taken back to the world through frame
// 0's clip rows (the render's own camera), so the native camera draws them where the game did at the tick, at any
// resolution, depth-tested against the scene.
#ifndef B3REPLAY_SPARKS_H
#define B3REPLAY_SPARKS_H

#include <cstdint>
#include <string>
#include <vector>

#include "b3asset/particleshade.h"
#include "b3gfx/b3gfx.h"
#include "capture.h"

namespace b3replay {

struct SparkStats {
    int arrays = 0, banks = 0, segments = 0, quads = 0, culled = 0, intervals = 0;
};

// Builds the quads RenderPostFogEffects 0x261E00 -> CB3SparkRenderer draws for the tick (none unless the capture holds
// the whole camera history this tick). `cam914` = camera record (+0x170, +0x174, +0x178, +0x168). Appends 4 vertices
// and 6 indices per quad (corners A + n, A - n, B + n, B - n; ST and the ring's RGBA as the program / chain send them).
// Returns false when nothing is drawn.
// effects2: `level` (b3settings kLevel*: 3 High = every spark, the default; 2 Medium; 1 Low; 0 none) draws a share of them,
// by particle_kept below. Drawn only: the captured state is read, never changed.
bool build_sparks(const ContactFx& c, const std::uint32_t cam914[4], std::vector<b3gfx::Vertex>& verts, std::vector<std::uint32_t>& idx,
                  SparkStats* stats = nullptr, int level = 3);

// effects2: the particle effects' levels. A particle's identity (its array, bank and slot in the game's ring: the same number
// for as long as the particle lives, from tick to tick and frame to frame) is mixed into 2 bits; High keeps every one, Medium
// two in four, Low one in four, Off none. The shares nest (Low is part of Medium), so raising a level only adds particles.
std::uint32_t particle_id(std::uint32_t array, std::uint32_t bank, std::uint32_t slot);
bool particle_kept(int level, std::uint32_t id);
// effects2: so that a thinned effect still reads as bright, the particles a level keeps are drawn a little larger: the spark
// streaks' width (what the 'spark' program calls half width) and the debris sprites' size are scaled by this (1 at High,
// exactly: nothing is multiplied there). Low 1.6 for sparks / 1.3 for debris, Medium 1.25 / 1.12.
float spark_width_scale(int level);
float debris_size_scale(int level);

// Popcorn (fx2, §19.3): CB3PopcornArray::Render 0x365920 per array (RenderPostFogEffects 0x261E00 draws the four
// only while the player's crash flag gWorld + 0x12C63A is set): one GS SPRITE per live particle at `now` (the displayed
// time: the particles are closed-form in time), from b3asset/popcornshade.h (bit-exact against the program:
// b3vucheck_popcorn). The set-up (size, colours, fade, clip rows for the ST order) uses the tick's game camera `game`;
// the quads face the displayed camera `view` and fade by its depth. One draw per array (its ALPHA_1 and texture).
struct PopcornDraw {
    int array = 0;
    std::uint32_t alpha = 0;     // array +0x0 low word: ALPHA_1 (0x48 array 0, 0x44 arrays 1-3)
    const char* texture = "";
    std::uint32_t first = 0, count = 0;   // indices
};
// effects2: `level` as build_sparks (the sprites of a thinned particle are left out; the others are drawn exactly as at High).
int build_popcorn(const ContactFx& c, const Camera& game, const std::uint32_t trail_cam[10][4], float now, const Camera& view,
                  std::vector<b3gfx::Vertex>& verts, std::vector<std::uint32_t>& idx, std::vector<PopcornDraw>& draws, int level = 3);

// Particles (effects3, docs/native-renderer.md §19.10): the 24 CB3ParticleArray (tyre / exhaust smoke, dust, gravel, snow, glass,
// fire, explosions), CB3ParticleRenderer::EndRender 0x254580 / DispatchArray 0x254340 per array and bank, drawn from the
// GIF the game's own program 0x46D750 produces (b3asset/particleshade.h, run in the VU interpreter on the captured data),
// mapped back to the world. One draw per run of packets with the same texture, ALPHA_1 and TEST_1.
struct ParticleDraw {
    int array = 0, bank = 0;
    std::string texture;
    std::uint64_t alpha = 0, test = 0;
    std::uint32_t first = 0, count = 0;   // indices
};
struct ParticleBuildStats {
    int arrays = 0, banks = 0, quads = 0, triangles = 0;
    b3asset::ParticleStats vu;
};
// `crash` = the player's crash flag (the crash banks are drawn only then), `debris_level` / `smoke_level` as the effect
// levels (arrays 0..5 and every crash bank follow CRASH DEBRIS, the smoke / fire / explosion arrays SPARKS; 3 = every
// particle, the game's whole set). false when the capture holds no particles or nothing is drawn.
// The particle layer's share for a SPARKS / CRASH DEBRIS level: the sparks and the popcorn bits keep their own levels, the
// 24 CB3ParticleArray (smoke, dust, glass, fire) are drawn one step lighter at the default (High -> Medium: a half), Medium ->
// Low, Low -> Low; $B3_PARTICLES=full draws the whole set at High.
int particle_layer_level(int setting, bool full);

bool build_particles(const ContactFx& c, bool crash, int smoke_level, int debris_level, b3asset::ParticleVu& vu,
                     std::vector<b3gfx::Vertex>& verts, std::vector<std::uint32_t>& idx, std::vector<ParticleDraw>& draws,
                     ParticleBuildStats* stats = nullptr);

// The array whose raster EndRender 0x2636B0 binds (the first renderable one's; all six name "fxspark" [C params +0x68]).
const char* spark_texture_name();

}  // namespace b3replay

#endif
