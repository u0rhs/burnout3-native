// Plain decoded geometry shared by the track and car readers: vertex/index buffers, draw ranges with a texture and
// material word, and the decoded textures. What the viewer (and later the native renderer) uploads to the GPU.
#ifndef B3ASSET_MODEL_H
#define B3ASSET_MODEL_H

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "b3asset/raster.h"

namespace b3asset {

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
    std::uint32_t rgba;       // vertex colour, 8-bit RGBA; scaled by Model::colour_scale (cars: 0x80 mapped to 0xFF;
                              // tracks: the raw GS bytes, 0x80 = 1.0); 0xFFFFFFFF if none
    std::uint16_t infl[2];    // cars: two deformation influences (weight << 8 | zone << 2); tracks: [0] low byte = the
                              // s8 specular intensity of type 1/2 vertices (docs/native-renderer.md §11)
};
static_assert(sizeof(Vertex) == 40, "GPU vertex layout");

struct DrawRange {
    std::uint32_t first_index = 0, index_count = 0;
    int texture = -1;            // index into Model::textures, -1 untextured
    std::uint32_t material = 0;  // source material word (meaning per file kind, see the reader)
    std::uint32_t colour = 0;    // source colour word (cars: entry colour)
    std::string part;            // e.g. "L0", "A3", "B0#2" (cars), "unit 12" (tracks)
    // How texture alpha is used: kOpaque ignores it (car atlases keep a non-coverage mask there), kAlphaTest cuts
    // below 0.5 [G threshold until the GS TEST state per material is read], kBlend draws in the translucent pass.
    enum Blend : std::uint8_t { kOpaque, kAlphaTest, kBlend } blend = kAlphaTest;
    std::uint32_t tint = 0xFFFFFFFFu;  // RGBA8 multiplied into the draw (cars: glass alpha from the colour word)
    float alpha_ref = 0.5f;            // kAlphaTest: discard below (tracks: from the material's GS TEST AREF)
    int transform = -1;                // index into Model::transforms (instanced draws), -1 = vertices are in world space
    float param[2] = {0, 0};           // tracks: material +0x04 / +0x08 (type 1/2 specular strength / exponent)
    std::int16_t entry = -1;           // cars: index of the mesh entry in its list
    std::int16_t glass_entry = -1;     // cars: the list's s8 at +1, the entry SetGlassState retextures (-1 none) [C]
    std::int16_t anim = -1;            // tracks: index into Model::track_anims when the material is animated (-1 none)
};

// An animated track material (anim1, docs/native-renderer.md §15): a material in the STATIC.DAT header's update list
// (+0x10, s16[+0x0E]), which CB3StaticTrack::Update 0x3329A0 advances every tick [C asm]. File values; the runtime
// fields live in TrackAnimState (b3asset/track.h).
struct TrackAnim {
    int material = -1;           // index into the STATIC.DAT material table
    std::uint16_t flags = 0;     // +0x24: 0x200 UV scroll (one texture), 0x100 frames back and forth
    std::vector<int> frames;     // +0x0C entries (+0x11 of them) as Model::textures indices; entry 0 is the draw's texture
    std::vector<int> number;     // per frame atoi(its name + strlen(frame 0's name)), as Update computes it (s8)
    std::int8_t p12 = 0, p13 = 0;  // +0x12 frame number, +0x13 direction
    float f14 = 0, f18 = 0;      // +0x14 frame unit (cycling) / speed (scroll); +0x18 first period (cycling) / step (scroll)
};

struct Texture {
    std::string name;
    std::vector<Image> levels;
    bool translucent = false;
    std::vector<Image> original;   // hdkey: `levels` are an HD replacement of these (DecodedRaster::original)
    bool hd = false;               // texpack: `levels` are an HD pack's replacement (DecodedRaster::hd; any pack, a mod's too)
};

struct Model {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;  // triangle list
    std::vector<DrawRange> draws;
    std::vector<Texture> textures;
    std::vector<std::array<float, 16>> transforms;  // column-major object -> world matrices for instanced draws
    std::vector<std::array<float, 4>> transform_tints;  // tracks: per transform the w lanes of rows X/Y/Z (instance RGB)
    std::vector<std::pair<int, std::uint32_t>> prop_transforms;  // tracks: (transform, STATIC +0x48 placement) per prop
    std::vector<TrackAnim> track_anims;  // tracks: the animated materials, in the header's update-list order
    float colour_scale = 1.0f;  // vertex colour byte / 255 * colour_scale = brightness on screen (tracks: GS 0x80 = 1.0
                                // times the x255/128 present blit = 255/64)
    std::vector<std::array<float, 3>> waypoints;    // tracks: unit centres in stream order (viewer start / jumps)
    float bmin[3] = {1e30f, 1e30f, 1e30f}, bmax[3] = {-1e30f, -1e30f, -1e30f};
    std::map<std::string, std::uint64_t> stats;  // counters for reports (batches, strips, unpack formats, ...)
    std::vector<std::string> notes;              // things the reader could not explain

    void grow_bounds(const float p[3]);
};

// Appends triangles for one VU batch of a strip with ADC flags: vertex i (i >= 2) closes triangle (i-2, i-1, i) unless
// its ADC flag is set; the winding alternates with the parity of i (as the track programs' back-face test does).
void strip_to_triangles(std::uint32_t base, const std::vector<bool>& adc, std::vector<std::uint32_t>& out);

}  // namespace b3asset

#endif
