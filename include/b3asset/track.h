// Track render data TRACKS/<area>/<route>/{STATIC.DAT, STREAMED.DAT}: reader into a b3asset::Model.
//
// Layout: docs/native-renderer.md §10.3 (spec from CB3StaticTrackData::FixUp 0x15E8E0, CB3StreamedTrack{High,
// LODAndCollision}Data::FixUp 0x15F200/0x15F0D0, CB3TrackMaterial::FixUp 0x26A170, the render functions, and a
// structural prober over all 37 routes). In short:
//   STATIC.DAT: materials (0x28 B: type, texture list, GS TEST_1/ALPHA_1), texture table (serialized rasters), four
//     backdrop tables of CB3StaticTrackObject (world-space CB3TrackMesh + s16 material per mesh), instance types and
//     prop types (object-space models + materials), prop placements (0x40-byte matrices), the unit table.
//   STREAMED.DAT: per unit a high block and a LOD block (mesh array at +0x50); the high block's render table (+0x60)
//     maps material slots to meshes (row with delta 0); the LOD block holds the unit's instance placements.
//   CB3TrackMesh (0x40): DMA chain 1 at +0x0C, optional chain 2 at +0x1C (relative to the mesh), each a DMA RET packet
//     of VIF batches (STCYCL 3/1; V4-32 position + ADC flag @0, UV V2-16 /4096 or V2-32 float @1, colour V4-8/V3-8 @2,
//     type-1 normals V4-8 signed transposed @204; ITOP = vertex count; MSCNT between chain-2 batches).
#ifndef B3ASSET_TRACK_H
#define B3ASSET_TRACK_H

#include <functional>
#include <map>
#include <string>

#include "b3asset/model.h"
#include "b3asset/vif.h"

namespace b3asset {

struct TrackOptions {
    bool streamed = true;    // the units' high meshes (or LOD meshes with `lod`)
    bool lod = false;
    bool backdrops = true;   // STATIC +0x24 (backdrops), +0x2C/+0x30 (water); +0x28 only with `extra_backdrops`
    bool extra_backdrops = false;
    bool instances = true;   // instance types placed by the units' LOD blocks
    bool props = true;       // prop types placed by STATIC +0x48
    bool lines = false;      // material type 3 (the GS's anti-aliased lines: wires, cables): skipped by default; with it,
                             // each segment becomes a quad for the GPU's screen-space expansion (track_line_batch)
    int first_unit = 0, unit_count = -1;  // a window of units (-1 = all)
    // Optional: every decoded VU batch as the VU1 program would see it (VU memory after the batch's unpacks, ITOP,
    // unpack address -> format, STATIC material index, Model::transforms index or -1). Shared instance models are
    // decoded once. Used by the offline VU1 reference check (tools/render/b3vucheck.cpp).
    std::function<void(const VuMemory&, std::uint32_t itop, const std::map<std::uint32_t, int>& unpacks, int material,
                       int transform)> on_batch;
    // Optional: after each mesh is added (b3modtool's track export, docs/modding/b3modtool.md): where it came from.
    struct MeshRef {
        bool in_static = false;          // the chains are in STATIC.DAT (else STREAMED.DAT)
        std::size_t chain1 = 0, chain2 = 0;  // file offsets of the DMA chains (chain2 0: none)
        int material = -1;               // STATIC material index
        int unit = -1;                   // the unit being read (streamed meshes, instances, props), -1 backdrops / water
        int draw = -1;                   // index of its draw in Model::draws, -1 when it drew nothing (or was skipped)
        bool shared = false;             // the same chains and material were decoded before (draw reuses those vertices)
    };
    std::function<void(const MeshRef&)> on_mesh;
};

struct TrackMaterialInfo {
    std::uint32_t type = 0, test = 0;
    float param4 = 0, param8 = 0;  // +0x04 / +0x08: per-type parameters sent to VU1 (types 1/2, docs/native-renderer.md §11)
    std::uint16_t flags = 0;
    std::uint8_t alpha = 0, alpha_fix = 0;
    int texture = -1;  // index into Model::textures
};

// The runtime fields of an animated material (CB3TrackMaterial +0x10..+0x1F).
struct TrackAnimState {
    std::int8_t cur = 0;         // +0x10 current frame (the texture drawn: frames[cur])
    std::int8_t p12 = 0, p13 = 0;  // +0x12 frame number, +0x13 direction
    float period = 0;            // +0x18
    float t = 0;                 // +0x1C: the last frame change (cycling) or the scroll position (flag 0x200)
};
// The state after loading: the file's values with CB3TrackMaterial::Prepare 0x26A160's +0x10 = +0x1C = 0 [C matched].
TrackAnimState track_anim_prepare(const TrackAnim& a);
// One tick of CB3StaticTrack::Update 0x3329A0 for one material at world time `now` (gWorld+0x20) [C asm].
void track_anim_update(const TrackAnim& a, TrackAnimState& s, float now);
// The state at world time `now` of a race whose clock started at 0 with the material just prepared: Update on every
// 1/60 s tick up to `now`, then at `now` itself (a continuous scroll is then exact; cycling frames as with no slow
// motion or pause) [L: the game's clock restarts at 0 with each race, docs/native-renderer.md §15].
TrackAnimState track_anim_at(const TrackAnim& a, float now);
// What CB3TrackRenderer::ReallySetMaterial 0x26B2F0 sends for flag 0x200 (VU1 qw 1005 y, added to every u):
// 1 - (t - floor(t)) [C nonmatching ReallySetMaterial, program 0x0038-0x0054].
float track_uv_scroll(float t);

// `streamed` may be empty (static-only route, e.g. US/C5_V1). Material infos are appended to `materials` if given.
bool read_track(Bytes static_dat, Bytes streamed_dat, const TrackOptions& opt, GsMemory& gs, Model& out,
                std::string* why = nullptr, std::vector<TrackMaterialInfo>* materials = nullptr);

// One VU batch of a line mesh (material type 3, docs/track-format.md §3.5): V4-32 positions at 3i (w bit 15 = ADC) and
// V4-8 colours at 3i+2. Program 0x476130 sends every vertex as is under PRIM 0xE9 (LINE, Gouraud, fog, alpha blend,
// AA1), so the GS draws segment (2k, 2k+1) unless vertex 2k+1 carries ADC [C program 0x0058-0x0079, GS LINE rule].
// Each drawn segment is appended as a quad of four vertices that the GPU widens in screen space (b3gfx mesh.vert,
// track_line_expand below): pos = this end, normal = the other end, uv = (side -1 / +1, end 0 / 1), rgba = this end's
// colour (the raw GS bytes); indices (0 1 2) (2 1 3). Returns the segments added.
std::uint32_t track_line_batch(const VuMemory& vu, std::uint32_t n, std::vector<Vertex>& vertices,
                               std::vector<std::uint32_t>& indices);

}  // namespace b3asset

#endif
