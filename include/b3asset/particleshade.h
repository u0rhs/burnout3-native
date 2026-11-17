// The particle arrays (CB3ParticleArray x 24, tyre / exhaust smoke, dust, gravel, snow, glass shards, fire, explosions;
// docs/effects-survey.md §3.5, docs/native-renderer.md §19.10): the game's own VU1 program 0x46D750 run on the game's own
// per-frame data, in the b3asset VU interpreter, and its GS output read back as world-space quads.
//
// There is no hand-written model of the program: every motion, size, colour and rotation step is the program's. The
// inputs are what CB3ParticleRenderer sends [C: EndRender 0x254580 (the camera rows 1017..1023, the program call),
// DispatchArray 0x254340 (per bank: the array's rows 1000..1016 and its pre-built chain), CB3ParticleBank::Construct
// 0x2558A0 (the chain: per <= 60 particles three UNPACKs V4-32 / V4-16 / V4-8 into stride-3 rows, ITOP = count | 0x100 on
// the last chunk, MSCNT)]; the output is the program's GIF packets (SPRITE or TRISTRIP, ST, RGBA, XYZ in screen space).
// The screen space is the game camera's: each vertex is mapped back to the world through the program's own world-to-clip
// rows (VU memory 1011..1014 after its set-up, vf05 and the camera row 1022) exactly as the spark vertices are
// (src/b3replay/sparks.cpp), so the quads can be drawn by any camera, at any resolution, depth-tested against the scene.
#ifndef B3ASSET_PARTICLESHADE_H
#define B3ASSET_PARTICLESHADE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace b3asset {

constexpr int kParticleArrays = 24;
constexpr std::size_t kParticleArrayBytes = 0x150;   // CB3ParticleArray
constexpr std::size_t kParticleCamBytes = 0x230;     // the camera record (graphics manager +0x470 + 0x230 idx)
constexpr std::size_t kParticleProgramBytes = 0x1980;   // vu_game_0046D750 (the image up to the next program)

// A bank's record allocation as CB3ParticleBank::Construct lays it out: capacity x 16 (position xyz, spawn time w),
// capacity x 8 (V4-16), capacity x 4 (V4-8), then (capacity + 59) / 60 x 4 (newest spawn time per 60-block).
inline std::size_t particle_bank_bytes(std::uint32_t capacity) { return capacity ? std::size_t(capacity) * 28 + ((capacity + 59) / 60) * 4 : 0; }

struct ParticleBankIn {
    const std::uint8_t* rec = nullptr;   // particle_bank_bytes(capacity) bytes
    std::uint32_t capacity = 0;
    bool dispatch = false;               // RenderParticleArray's decision for this bank
};
struct ParticleArrayIn {
    const std::uint8_t* head = nullptr;  // the CB3ParticleArray, kParticleArrayBytes
    ParticleBankIn bank[2];              // normal, crash
};

struct ParticleVertex {
    float pos[3] = {};                   // world
    float s = 0, t = 0;
    std::uint8_t rgba[4] = {};
    bool adc = false;
};
// One GIF packet's primitives: `prim` the GS PRIM value in force (type in bits 0..2: 4 tristrip, 6 sprite, 3 triangle),
// `alpha` / `test` the A+D ALPHA_1 / TEST_1 values the program sends with it.
struct ParticlePacket {
    int array = 0, bank = 0;
    std::uint32_t prim = 0;
    bool sprite = false;                 // SPRITE: `v` holds four corners (TL, TR, BL, BR) per sprite, else the GIF's vertices (with adc)
    std::uint64_t alpha = 0, test = 0;
    bool has_alpha = false, has_test = false;
    std::vector<ParticleVertex> v;
};

struct ParticleStats {
    std::uint64_t kicks = 0, batches = 0, vertices = 0, pairs = 0, unprojected_bad = 0;
};

// CB3ParticleRenderer::RenderParticleArray 0x254970's decision for one bank: `normal_live` / `crash_live` are the banks'
// live masks (+0x28 of each bank, array +0x110 / +0x13C), `params_5d` the params' +0x5D byte, `skip_flag` D_004E29E4,
// `crash` the BeginRender argument.
bool particle_bank_dispatched(const std::uint8_t* head, int bank, bool crash, bool skip_flag, std::uint8_t params_5d);

// The program image (its DMA chain with the VIF MPG commands, as in RAM at 0x46D750).
class ParticleVu {
public:
    bool load(const std::uint8_t* image, std::size_t bytes, std::string* why = nullptr);
    bool loaded() const { return loaded_; }
    // Runs one render: `cam` is the camera record, `arrays` the 24 arrays in draw order (head null: skipped). `keep`, when
    // set, lets the caller drop individual particles: it is called as keep(array, bank, slot) for every record with a
    // spawn time; false kills that record in the copy the program reads (effect levels). Appends to `out`.
    using KeepFn = bool (*)(void* user, int array, int bank, int slot);
    bool run(const std::uint8_t cam[kParticleCamBytes], const ParticleArrayIn arrays[kParticleArrays], std::vector<ParticlePacket>& out,
             ParticleStats* stats = nullptr, std::string* why = nullptr, KeepFn keep = nullptr, void* user = nullptr,
             float time_override = -1e30f);

private:
    bool loaded_ = false;
    struct Prog;
    std::shared_ptr<Prog> prog_;
};

}  // namespace b3asset

#endif
