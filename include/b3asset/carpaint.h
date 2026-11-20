// carpaint (docs/remaster-features.md §15): custom car paint and decals off for the player's car, drawn only.
//
// A player car's texture is one 256 x 256 8-bit atlas with up to 8 CLUTs, one per paint the car select offers
// (docs/modding/file-formats/car.md). The CLUTs index the same texels, so comparing the decodes tells the texels apart:
//   main paint    the texels whose colour changes with the paint, in step with the body's colour (the same colour
//                 "signature" across the palettes: a shade of each palette's body colour);
//   second colour texels that change too but not in step (two-tone stripes, boxes behind logos): a livery colour;
//   decal         texels that never change and sit in an island enclosed by paint (logos, numbers, sponsor stripes);
//   fixed         every other texel (lights, grilles, wheels, interior, the underside's parts of the atlas).
// The map is made from the PS2 decodes only (no HD pack), once per car file. The car shader (car.frag) applies it over
// whatever texture is bound, an HD pack's replacement too: a paint texel's shade is its own brightest channel over the
// reference palette's mean, so the HD detail stays; decals are filled with the paint at the shade inpainted from the
// paint around them. Nothing reaches game memory: the paint index the game keeps is untouched.
#ifndef B3ASSET_CARPAINT_H
#define B3ASSET_CARPAINT_H

#include <cstdint>
#include <string>
#include <vector>

#include "b3asset/raster.h"

namespace b3asset {

struct CarPaintMap {
    int w = 0, h = 0;              // the atlas's level 0
    int palettes = 0;              // the raster's paint count (+0xA0)
    int ref = 0;                   // the palette with the brightest main paint: custom colours are drawn over its decode
    float ref_level = 0;           // the main paint's mean brightest channel in `ref`, texel units (texel byte / 255)
    float main[8][3] = {};         // each palette's mean main paint colour, texel units (decals off with the game's paint)
    // RGBA8, the atlas's size: R main paint, G second colour, B decal (each 255 = all of the texel), A the paint's shade
    // at the texel / 2 (the decode's brightest channel over ref_level; inpainted under decals and second colours)
    Image mask;
    int main_texels = 0, second_texels = 0, decal_texels = 0, decal_islands = 0;
    int parts_kept = 0;            // islands enclosed by paint that the meshes show are parts (lamps, plates, wheels)
    int decal_holes = 0;           // paint islands inside decals (the paint through a logo's letters), taken into them
};

// The map from the car file's atlas (PVEH/<class>/CARn.BGV). False when the atlas does not decode or has one palette
// (nothing changes between paints, so nothing tells the paint from the livery: those cars keep their look).
bool car_paint_map(Bytes car_file, CarPaintMap& out, std::string* why = nullptr);
// A triangle of the car's meshes that samples the atlas (texture slot 0), in texture coordinates (1.0 = the atlas).
struct AtlasTriangle {
    float uv[3][2] = {};
    enum Kind : std::uint8_t { kBody, kLight, kWheel } kind = kBody;   // body / panel mesh, a light entry, a wheel list
};
// The car's atlas triangles over every LOD (read_car without the texture).
std::vector<AtlasTriangle> car_atlas_triangles(Bytes car_file);
// The same from the decoded palettes (level 0 of each, same size, at least 2). With `triangles`, a fixed island is a
// decal only where the meshes print it on paint: no light or wheel samples it, and the body triangles that sample it
// cover paint too (a licence plate, a lamp or an exhaust drawn on a paint-coloured part of the atlas has polygons of
// its own that cover only it). Without them (tests), the island test alone.
bool car_paint_map_from(const std::vector<Image>& palettes, CarPaintMap& out, std::string* why = nullptr,
                        const std::vector<AtlasTriangle>* triangles = nullptr);

// The texel colour a chosen RGB (0xRRGGBB, as on screen) is drawn at: the game's light paints sit near texel 120 / 255
// (MODULATE doubles a texel, so 128 is full brightness under white light); white is drawn at kCustomWhite.
constexpr float kCustomWhite = 120.0f / 255.0f;
inline void custom_paint_texel(std::uint32_t rgb, float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = float((rgb >> (16 - 8 * c)) & 0xFF) / 255.0f * kCustomWhite;
}

// "RRGGBB" (or "#RRGGBB") -> 0xRRGGBB; false for anything else.
bool parse_rgb(const std::string& text, std::uint32_t& out);

}  // namespace b3asset

#endif
