// carpaint (b3asset/carpaint.h, docs/remaster-features.md §15): the paint / livery map of a car atlas on synthetic
// palettes (no game data): main paint, a second colour, decals enclosed by paint, parts the meshes keep, the shade.
#include <algorithm>

#include "b3asset/carpaint.h"
#include "test.h"

using namespace b3asset;

namespace {

constexpr int kW = 32, kH = 32;

// Paint k's body colour, a second colour that does not follow it, the fixed livery (white) and a fixed part (grey).
const std::uint8_t kBody[3][3] = {{120, 10, 10}, {20, 20, 60}, {110, 110, 105}};
const std::uint8_t kSecond[3][3] = {{30, 30, 30}, {90, 90, 20}, {10, 60, 10}};

// The atlas in paint k: the left 8 columns a fixed part; the rest body paint with a vertical shade (rows darker going
// down: 1.0 .. 0.5), a white 4 x 4 logo at (16..19, 8..11), a second-colour band in rows 24..27 and a grey 3 x 3 "lamp"
// at (26..28, 2..4) inside the paint.
std::vector<Image> atlas() {
    std::vector<Image> out;
    for (int k = 0; k < 3; ++k) {
        Image im;
        im.w = kW;
        im.h = kH;
        im.rgba.assign(std::size_t(kW) * kH * 4, 255);
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                std::uint8_t* p = &im.rgba[(std::size_t(y) * kW + x) * 4];
                const float shade = 1.0f - 0.5f * float(y) / float(kH - 1);
                for (int c = 0; c < 3; ++c) p[c] = std::uint8_t(float(kBody[k][c]) * shade);
                if (x < 8) for (int c = 0; c < 3; ++c) p[c] = 70;
                if (x >= 16 && x < 20 && y >= 8 && y < 12) for (int c = 0; c < 3; ++c) p[c] = 250;
                if (x >= 8 && y >= 24 && y < 28) for (int c = 0; c < 3; ++c) p[c] = kSecond[k][c];
                if (x >= 26 && x < 29 && y >= 2 && y < 5) for (int c = 0; c < 3; ++c) p[c] = 128;
            }
        out.push_back(std::move(im));
    }
    return out;
}

const std::uint8_t* mask_at(const CarPaintMap& m, int x, int y) { return &m.mask.rgba[(std::size_t(y) * m.w + x) * 4]; }

AtlasTriangle tri(float x0, float y0, float x1, float y1, float x2, float y2, AtlasTriangle::Kind kind) {
    AtlasTriangle t;
    const float p[3][2] = {{x0, y0}, {x1, y1}, {x2, y2}};
    for (int k = 0; k < 3; ++k) { t.uv[k][0] = p[k][0] / kW; t.uv[k][1] = p[k][1] / kH; }
    t.kind = kind;
    return t;
}

}  // namespace

TEST(classes_without_meshes) {
    CarPaintMap m;
    std::string why;
    CHECK(car_paint_map_from(atlas(), m, &why));
    CHECK(m.w == kW && m.h == kH && m.palettes == 3);
    CHECK(mask_at(m, 12, 4)[0] == 255 && mask_at(m, 12, 4)[1] == 0);    // body paint
    CHECK(mask_at(m, 2, 2)[0] == 0 && mask_at(m, 2, 2)[1] == 0 && mask_at(m, 2, 2)[2] == 0);   // the fixed part
    CHECK(mask_at(m, 12, 25)[1] == 255 && mask_at(m, 12, 25)[0] == 0);  // second colour
    CHECK(mask_at(m, 12, 23)[2] == 255 && mask_at(m, 12, 23)[0] == 255);  // the paint at its edge goes with it (decals off)
    CHECK(mask_at(m, 12, 21)[2] == 0);
    CHECK(mask_at(m, 17, 9)[2] == 255);                                  // the logo: a decal
    CHECK(mask_at(m, 27, 3)[2] == 255);                                  // the lamp too, without the meshes
    CHECK(m.decal_islands == 2 && m.parts_kept == 0);
    // the decal's edge: paint texels next to it belong to it (anti-aliased logos)
    CHECK(mask_at(m, 15, 9)[2] == 255 && mask_at(m, 15, 9)[0] == 255);
    CHECK(mask_at(m, 12, 9)[2] == 0);
}

TEST(reference_and_shade) {
    CarPaintMap m;
    CHECK(car_paint_map_from(atlas(), m));
    CHECK(m.ref == 0);   // paint 0's body (brightest channel 120) beats paint 2's (110)
    CHECK_NEAR(m.main[1][2], 60.0 * 0.75 / 255.0, 0.03);   // paint 1's mean body blue (shade 1 .. 0.5)
    // the shade is the texel's brightest channel over the reference's mean: higher at the top
    CHECK(mask_at(m, 12, 0)[3] > mask_at(m, 12, 20)[3]);
    // under the logo the shade is inpainted from the paint around it (about the row's own)
    const float row9 = (1.0f - 0.5f * 9.0f / 31.0f) * 120.0f / (m.ref_level * 255.0f);
    CHECK_NEAR(mask_at(m, 17, 9)[3] / 255.0 * 2.0, row9, 0.08);
}

TEST(meshes_keep_parts) {
    // the logo sits on a body quad that covers paint around it; the lamp has a quad of its own (a part, not printed)
    std::vector<AtlasTriangle> t = {tri(12, 4, 24, 4, 24, 16, AtlasTriangle::kBody), tri(12, 4, 24, 16, 12, 16, AtlasTriangle::kBody),
                                    tri(26, 2, 29, 2, 29, 5, AtlasTriangle::kBody), tri(26, 2, 29, 5, 26, 5, AtlasTriangle::kBody)};
    CarPaintMap m;
    CHECK(car_paint_map_from(atlas(), m, nullptr, &t));
    CHECK(mask_at(m, 17, 9)[2] == 255);
    CHECK(mask_at(m, 27, 3)[2] == 0 && mask_at(m, 27, 3)[0] == 0);
    CHECK(m.decal_islands == 1 && m.parts_kept == 1);
    // a light entry sampling an island keeps it whatever surrounds it
    t.push_back(tri(14, 6, 22, 6, 22, 14, AtlasTriangle::kLight));
    CHECK(car_paint_map_from(atlas(), m, nullptr, &t));
    CHECK(mask_at(m, 17, 9)[2] == 0 && m.decal_islands == 0 && m.parts_kept == 2);
}

TEST(paint_through_a_logo) {
    // a black box with the paint showing through its "letters" (a 2 x 2 hole): the hole goes with the decal (filled when
    // decals are off) and stays paint (recoloured with a custom paint)
    std::vector<Image> pal = atlas();
    for (Image& im : pal)
        for (int y = 14; y < 20; ++y)
            for (int x = 10; x < 16; ++x) {
                if (x >= 12 && x < 14 && y >= 16 && y < 18) continue;
                std::uint8_t* p = &im.rgba[(std::size_t(y) * kW + x) * 4];
                p[0] = p[1] = p[2] = 5;
            }
    CarPaintMap m;
    CHECK(car_paint_map_from(pal, m));
    CHECK(mask_at(m, 10, 14)[2] == 255 && mask_at(m, 10, 14)[0] == 0);   // the box: a decal
    CHECK(mask_at(m, 12, 16)[2] == 255 && mask_at(m, 12, 16)[0] == 255); // the hole: decal and paint
    CHECK(m.decal_holes == 1 && m.decal_islands == 3);
    CHECK(mask_at(m, 22, 20)[2] == 0);                                   // the body around stays plain paint
}

TEST(one_paint_has_no_map) {
    std::vector<Image> one = atlas();
    one.resize(1);
    CarPaintMap m;
    std::string why;
    CHECK(!car_paint_map_from(one, m, &why) && !why.empty());
    std::vector<Image> same = {one[0], one[0]};
    CHECK(!car_paint_map_from(same, m, &why));
}

TEST(rgb_text) {
    std::uint32_t v = 0;
    CHECK(parse_rgb("2080FF", v) && v == 0x2080FFu);
    CHECK(parse_rgb("#c01818", v) && v == 0xC01818u);
    CHECK(!parse_rgb("2080F", v) && !parse_rgb("2080FG", v) && !parse_rgb("", v));
    float t[3];
    custom_paint_texel(0xFFFFFF, t);
    CHECK_NEAR(t[0], kCustomWhite, 1e-6);
    custom_paint_texel(0x00FF00, t);
    CHECK(t[0] == 0.0f && t[2] == 0.0f && t[1] > 0.4f);
}

TEST_MAIN("carpaint")
