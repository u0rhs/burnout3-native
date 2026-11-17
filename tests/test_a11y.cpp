// Accessibility, the drawing side (src/b3replay/a11y.h): HUD COLOUR FILTER's daltonize matrices and SCREEN FLASHES' cap.
#include "a11y.h"
#include "test.h"

#include <cmath>
#include <cstdlib>
#include <vector>

using namespace b3replay::a11y;

namespace {
std::uint32_t rgba(int r, int g, int b, int a = 0x80) { return std::uint32_t(r) | std::uint32_t(g) << 8 | std::uint32_t(b) << 16 | std::uint32_t(a) << 24; }
int ch(std::uint32_t c, int k) { return int(c >> (8 * k) & 0xFFu); }
// distance of two colours as a dichromat of filter f sees them (0..255 units)
double seen_apart(ColourFilter f, std::uint32_t a, std::uint32_t b) {   // (kOff: as they are)
    const Matrix3 sim = simulate_matrix(f);
    const std::uint32_t sa = remap_rgba(sim, a), sb = remap_rgba(sim, b);
    double d = 0;
    for (int k = 0; k < 3; ++k) d += double(ch(sa, k) - ch(sb, k)) * double(ch(sa, k) - ch(sb, k));
    return std::sqrt(d);
}
}  // namespace

TEST(filter_values) {
    CHECK(colour_filter(0) == ColourFilter::kOff && colour_filter(1) == ColourFilter::kDeuteranopia &&
          colour_filter(2) == ColourFilter::kProtanopia && colour_filter(3) == ColourFilter::kTritanopia);
    CHECK(colour_filter(4) == ColourFilter::kOff && colour_filter(-1) == ColourFilter::kOff);
    // off: the identity, every colour as it is
    const Matrix3 id = daltonize_matrix(ColourFilter::kOff);
    for (std::uint32_t c : {rgba(0, 0, 0), rgba(255, 128, 7, 3), rgba(12, 200, 99, 255)}) CHECK(remap_rgba(id, c) == c);
}

TEST(greys_and_alpha_kept) {
    for (ColourFilter f : {ColourFilter::kDeuteranopia, ColourFilter::kProtanopia, ColourFilter::kTritanopia})
        for (int g = 0; g <= 255; g += 15)
            for (int a : {0, 0x40, 0x80, 0xFF}) {
                const std::uint32_t c = remap_rgba(daltonize_matrix(f), rgba(g, g, g, a));
                CHECK(ch(c, 3) == a);
                for (int k = 0; k < 3; ++k) CHECK(std::abs(ch(c, k) - g) <= 3);   // grey stays grey (white text stays white)
            }
}

TEST(confused_colours_pulled_apart) {
    // every pair of a 6 x 6 x 6 colour grid that the dichromat confuses (at least 100 apart, at most 30 apart as the
    // dichromat sees them -- Machado 2009's simulation): after the filter, every one of them is seen further apart, and
    // on average by at least 3x. (Red / green for the deutan and protan, blue / green and yellow / pink for the tritan.)
    std::vector<std::uint32_t> grid;
    for (int r = 0; r <= 255; r += 51)
        for (int g = 0; g <= 255; g += 51)
            for (int b = 0; b <= 255; b += 51) grid.push_back(rgba(r, g, b));
    for (ColourFilter f : {ColourFilter::kDeuteranopia, ColourFilter::kProtanopia, ColourFilter::kTritanopia}) {
        const Matrix3 m = daltonize_matrix(f);
        double before = 0, after = 0;
        int pairs = 0, better = 0;
        for (std::size_t i = 0; i < grid.size(); ++i)
            for (std::size_t j = i + 1; j < grid.size(); ++j) {
                const double truth = seen_apart(ColourFilter::kOff, grid[i], grid[j]), b0 = seen_apart(f, grid[i], grid[j]);
                if (truth < 100 || b0 > 30) continue;
                const double a0 = seen_apart(f, remap_rgba(m, grid[i]), remap_rgba(m, grid[j]));
                before += b0;
                after += a0;
                ++pairs;
                better += a0 > b0;
            }
        CHECK(pairs >= 20 && better == pairs && after > 3.0 * before);
    }
    // the tritan's case at HUD colours: the boost bar's blue against a green, a yellow against a pink
    const Matrix3 t = daltonize_matrix(ColourFilter::kTritanopia);
    CHECK(seen_apart(ColourFilter::kTritanopia, remap_rgba(t, rgba(40, 80, 230)), remap_rgba(t, rgba(40, 170, 120))) >
          1.3 * seen_apart(ColourFilter::kTritanopia, rgba(40, 80, 230), rgba(40, 170, 120)));
    // the deutan's: a red against a green
    const Matrix3 d = daltonize_matrix(ColourFilter::kDeuteranopia);
    CHECK(seen_apart(ColourFilter::kDeuteranopia, remap_rgba(d, rgba(220, 40, 30)), remap_rgba(d, rgba(60, 170, 40))) >
          1.3 * seen_apart(ColourFilter::kDeuteranopia, rgba(220, 40, 30), rgba(60, 170, 40)));
}

TEST(pixels) {
    std::uint8_t px[8] = {220, 40, 30, 7, 128, 128, 128, 255};
    const Matrix3 m = daltonize_matrix(ColourFilter::kDeuteranopia);
    const std::uint32_t want = remap_rgba(m, rgba(220, 40, 30, 7));
    remap_pixels(m, px, 2);
    CHECK(px[0] == ch(want, 0) && px[1] == ch(want, 1) && px[2] == ch(want, 2) && px[3] == 7 && px[7] == 255);
    CHECK(std::abs(px[4] - 128) <= 2 && std::abs(px[5] - 128) <= 2 && std::abs(px[6] - 128) <= 2);
}

TEST(flash_cap) {
    auto fill = [](float cover, float light, float alpha, std::uint8_t gs, bool abe = true) {
        FlashDraw d;
        d.coverage = cover;
        d.light = d.white = light;   // grey / white
        d.alpha = alpha;
        d.gs_alpha = gs;
        d.blend = abe;
        return d;
    };
    // a full-screen white fade at full alpha (0x44): its alpha down to the cap
    FlashCut c = flash_cut(fill(1.0f, 1.0f, 1.0f, 0x44));
    CHECK(c.scale_alpha && std::fabs(c.factor - kFlashCap) < 1e-6f);
    c = flash_cut(fill(0.9f, 1.0f, 0.5f, 0x44));
    CHECK(c.scale_alpha && std::fabs(c.factor - kFlashCap / 0.5f) < 1e-6f);
    // additive (0x48 Cs As + Cd, 0x58 Cs Ad + Cd): its colour scaled
    c = flash_cut(fill(1.0f, 1.0f, 1.0f, 0x48));
    CHECK(!c.scale_alpha && std::fabs(c.factor - kFlashCap) < 1e-6f);
    c = flash_cut(fill(1.0f, 0.8f, 0.1f, 0x58));
    CHECK(!c.scale_alpha && std::fabs(c.factor - kFlashCap / 0.8f) < 1e-6f);
    // opaque (PRIM ABE off): its colour
    c = flash_cut(fill(1.0f, 1.0f, 1.0f, 0x44, false));
    CHECK(!c.scale_alpha && std::fabs(c.factor - kFlashCap) < 1e-6f);
    // not flashes: small, dark (the pause shade, the letterbox), grey, already faint, subtractive, alpha only
    CHECK(flash_cut(fill(0.3f, 1.0f, 1.0f, 0x44)).factor == 1.0f);
    CHECK(flash_cut(fill(1.0f, 0.0f, 0.6f, 0x44)).factor == 1.0f);
    CHECK(flash_cut(fill(1.0f, 0.5f, 1.0f, 0x44)).factor == 1.0f);
    CHECK(flash_cut(fill(1.0f, 1.0f, 0.2f, 0x44)).factor == 1.0f);
    CHECK(flash_cut(fill(1.0f, 1.0f, 1.0f, 0x42)).factor == 1.0f);
    CHECK(flash_cut(fill(1.0f, 1.0f, 1.0f, 0x6A)).factor == 1.0f);
    // a coloured panel (the boot dialog's blue box: one light channel, the others dark) is not a flash
    FlashDraw box = fill(0.52f, 0.75f, 1.0f, 0x44);
    box.white = 0.25f;
    CHECK(flash_cut(box).factor == 1.0f);
    // a picture (a menu's backdrop, a loading screen) is left alone
    FlashDraw pic = fill(1.0f, 1.0f, 1.0f, 0x44);
    pic.textured = true;
    CHECK(flash_cut(pic).factor == 1.0f);
}

TEST(static_frames) {
    // the TV static's frames 2..5 are drawn as its first; nothing else is
    for (const char* n : {"Camchange2", "Camchange3", "Camchange4", "Camchange5"}) CHECK(static_frame(n));
    for (const char* n : {"Camchange", "Camchange6", "Camchange1", "Camchange22", "GlobalFont", "camchange2", ""}) CHECK(!static_frame(n));
    CHECK(std::string_view(kStaticFirstFrame) == "Camchange");
}

namespace {
Box box(float x0, float y0, float x1, float y1) {
    Box b;
    b.add(x0, y0);
    b.add(x1, y1);
    return b;
}
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool inside(const Box& b, const Box& in) { return b.x0 >= in.x0 - 1e-3f && b.y0 >= in.y0 - 1e-3f && b.x1 <= in.x1 + 1e-3f && b.y1 <= in.y1 + 1e-3f; }
}  // namespace

TEST(hud_size_levels) {
    CHECK(hud_size_scale(0) == 1.0f && near(hud_size_scale(1), 1.15f) && near(hud_size_scale(2), 1.3f) && near(hud_size_scale(3), 1.5f));
    CHECK(hud_size_scale(-1) == 1.0f && hud_size_scale(4) == 1.0f);
    CHECK(hud_size_percent(0) == 100 && hud_size_percent(1) == 115 && hud_size_percent(2) == 130 && hud_size_percent(3) == 150);
    CHECK(hud_size_percent(9) == 100);
}

TEST(hud_size_anchors) {
    // the HUD rect of a 640 x 448 raster with a 32 / 24 unit inset; the pivots are its corners, edge middles and centre
    const Box rect = box(32, 24, 608, 424);
    const Pivot tl = anchor_pivot(0, 0, rect, 0), br = anchor_pivot(1, 1, rect, 0), c = anchor_pivot(0.5f, 0.5f, rect, 0);
    CHECK(near(tl.x, 32) && near(tl.y, 24) && near(br.x, 608) && near(br.y, 424) && near(c.x, 320) && near(c.y, 224));
    // the wide layout's shift moves the pivot with the component: -ext for a left one, +ext for a right one
    const float ext = 106.67f;
    CHECK(near(anchor_pivot(0, 0, rect, -ext).x, 32 - ext) && near(anchor_pivot(1, 0, rect, ext).x, 608 + ext));
    // a box at its pivot keeps that corner and grows away from it (toward the centre)
    const Box pos = box(32, 24, 132, 64);   // a top-left component at the rect's corner
    const Box p15 = scaled(pos, tl, 1.5f);
    CHECK(near(p15.x0, 32) && near(p15.y0, 24) && near(p15.x1, 182) && near(p15.y1, 84));
    const Box speedo = box(508, 344, 608, 424);   // bottom-right
    const Box s15 = scaled(speedo, br, 1.5f);
    CHECK(near(s15.x1, 608) && near(s15.y1, 424) && near(s15.x0, 458) && near(s15.y0, 304));
    // a centred message grows about the centre, both ways
    const Box msg = box(220, 200, 420, 248);
    const Box m13 = scaled(msg, c, 1.3f);
    CHECK(near(m13.x0, 190) && near(m13.x1, 450) && near(m13.y0, 192.8f) && near(m13.y1, 255.2f));
    // scale 1 is the identity
    const Box id = scaled(msg, c, 1.0f);
    CHECK(id.x0 == msg.x0 && id.x1 == msg.x1 && id.y0 == msg.y0 && id.y1 == msg.y1);
}

TEST(hud_size_clamp) {
    const Box screen = box(0, 0, 640, 448);
    const Box rect = box(32, 24, 608, 424);
    // a corner component away from the far sides takes the whole scale
    CHECK(near(fit_scale(box(32, 24, 132, 64), anchor_pivot(0, 0, rect, 0), 1.5f, screen), 1.5f));
    // a wide centred bar at the bottom (a boost bar 500 units wide about x 320): 1.5 would put it past both sides;
    // it is capped where it meets them (320 / 250 = 1.28)
    const Pivot bc = anchor_pivot(0.5f, 1, rect, 0);
    const Box bar = box(70, 400, 570, 424);
    const float f = fit_scale(bar, bc, 1.5f, screen);
    CHECK(near(f, 1.28f));
    CHECK(inside(scaled(bar, bc, f), screen));
    // a component standing out past its pivot (a box below the rect's bottom edge) is capped by the screen's bottom
    const Box low = box(300, 400, 340, 440);   // pivot y 424: 16 below it, 24 to the screen's edge -> 1.5
    CHECK(near(fit_scale(low, bc, 2.0f, screen), 1.5f));
    // never below 1, never above the asked scale
    CHECK(fit_scale(bar, bc, 1.0f, screen) == 1.0f && fit_scale(Box{}, bc, 1.5f, screen) == 1.0f);
    // a message sliding in from off screen right: the side already past the screen does not limit it
    const Pivot c = anchor_pivot(0.5f, 0.5f, rect, 0);
    CHECK(near(fit_scale(box(500, 200, 900, 248), c, 1.5f, screen), 1.5f));
    // in the wide layout the bounds are the window (-ext .. 640 + ext): a right component moved there fits the same way
    const float ext = 106.67f;
    const Box wide = box(-ext, 0, 640 + ext, 448);
    const Pivot tr = anchor_pivot(1, 0, rect, ext);
    const Box lap = box(508 + ext, 24, 608 + ext, 64);
    CHECK(near(fit_scale(lap, tr, 1.5f, wide), 1.5f));
    CHECK(inside(scaled(lap, tr, 1.5f), wide));
    // every level, every anchor: whatever fits at 100 % stays on screen
    for (int lvl = 0; lvl < kHudSizeLevels; ++lvl)
        for (float fx : {0.0f, 0.5f, 1.0f})
            for (float fy : {0.0f, 0.5f, 1.0f}) {
                const Pivot p = anchor_pivot(fx, fy, rect, 0);
                for (const Box& b : {box(0, 0, 640, 448), box(32, 24, 300, 100), box(400, 300, 640, 448), bar, box(220, 200, 420, 248)}) {
                    const float s = fit_scale(b, p, hud_size_scale(lvl), screen);
                    CHECK(s >= 1.0f && s <= hud_size_scale(lvl) + 1e-6f);
                    CHECK(inside(scaled(b, p, s), screen));
                }
            }
}

TEST(hud_size_overlaps) {
    const Box rect = box(32, 24, 608, 424);
    // two top-left components stacked (position above lap): both scale about the same corner, so they stay apart
    {
        std::vector<Part> parts(2);
        parts[0] = Part{box(32, 24, 132, 64), anchor_pivot(0, 0, rect, 0), 1.5f, false};
        parts[1] = Part{box(32, 70, 132, 100), anchor_pivot(0, 0, rect, 0), 1.5f, false};
        resolve_overlaps(parts);
        CHECK(near(parts[0].scale, 1.5f) && near(parts[1].scale, 1.5f));
    }
    // a left and a right component in one row that meet at 150 %: one common cap, the largest at which they stay apart
    {
        std::vector<Part> parts(2);
        parts[0] = Part{box(32, 24, 300, 64), anchor_pivot(0, 0, rect, 0), 1.5f, false};
        parts[1] = Part{box(340, 24, 608, 64), anchor_pivot(1, 0, rect, 0), 1.5f, false};
        resolve_overlaps(parts);
        CHECK(near(parts[0].scale, parts[1].scale, 1e-4f) && parts[0].scale > 1.0f && parts[0].scale < 1.5f);
        // 32 + 268 s = 608 - 268 s -> s = 576 / 536
        CHECK(near(parts[0].scale, 576.0f / 536.0f, 1e-3f));
        CHECK(!scaled(parts[0].box, parts[0].pivot, parts[0].scale).overlaps(scaled(parts[1].box, parts[1].pivot, parts[1].scale)));
    }
    // a centred message that would run into a steady top component: the message gives way, the HUD keeps its size
    {
        std::vector<Part> parts(2);
        parts[0] = Part{box(220, 24, 420, 80), anchor_pivot(0.5f, 0, rect, 0), 1.5f, false};
        parts[1] = Part{box(200, 130, 440, 200), anchor_pivot(0.5f, 0.5f, rect, 0), 1.5f, true};
        resolve_overlaps(parts);
        CHECK(near(parts[0].scale, 1.5f));
        CHECK(parts[1].scale >= 1.0f && parts[1].scale < 1.5f);
        CHECK(!scaled(parts[0].box, parts[0].pivot, parts[0].scale).overlaps(scaled(parts[1].box, parts[1].pivot, parts[1].scale)));
    }
    // ... unless the steady part would grow into the message where it stands at 100 %: then one common cap
    {
        std::vector<Part> parts(2);
        parts[0] = Part{box(220, 24, 420, 80), anchor_pivot(0.5f, 0, rect, 0), 1.5f, false};
        parts[1] = Part{box(200, 100, 440, 200), anchor_pivot(0.5f, 0.5f, rect, 0), 1.5f, true};
        resolve_overlaps(parts);
        CHECK(parts[0].scale > 1.0f && parts[0].scale < 1.5f && near(parts[0].scale, parts[1].scale, 1e-4f));
        CHECK(!scaled(parts[0].box, parts[0].pivot, parts[0].scale).overlaps(scaled(parts[1].box, parts[1].pivot, parts[1].scale)));
    }
    // the gap: parts kept that far apart
    {
        std::vector<Part> parts(2);
        parts[0] = Part{box(32, 24, 300, 64), anchor_pivot(0, 0, rect, 0), 1.5f, false};
        parts[1] = Part{box(340, 24, 608, 64), anchor_pivot(1, 0, rect, 0), 1.5f, false};
        resolve_overlaps(parts, 2.0f);
        CHECK(!scaled(parts[0].box, parts[0].pivot, parts[0].scale).overlaps(scaled(parts[1].box, parts[1].pivot, parts[1].scale), 2.0f));
        CHECK(near(parts[0].scale, 574.0f / 536.0f, 1e-3f));
    }
    // parts that overlap at 100 % (a component drawn over another) are left as the game has them
    {
        std::vector<Part> parts(2);
        parts[0] = Part{box(32, 24, 200, 80), anchor_pivot(0, 0, rect, 0), 1.5f, false};
        parts[1] = Part{box(100, 40, 300, 90), anchor_pivot(0.5f, 0, rect, 0), 1.3f, false};
        resolve_overlaps(parts);
        CHECK(near(parts[0].scale, 1.5f) && near(parts[1].scale, 1.3f));
    }
    // many parts: every pair that was apart at 100 % is apart afterwards; scales stay in [1, asked]
    {
        std::vector<Part> parts;
        const float fxs[3] = {0, 0.5f, 1}, fys[3] = {0, 0.5f, 1};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                const Pivot p = anchor_pivot(fxs[i], fys[j], rect, 0);
                const float w = 150, h = 90;   // a big component at each anchor, grown toward the centre
                const float x0 = fxs[i] == 0 ? 32 : fxs[i] == 1 ? 608 - w : 320 - w / 2;
                const float y0 = fys[j] == 0 ? 24 : fys[j] == 1 ? 424 - h : 224 - h / 2;
                parts.push_back(Part{box(x0, y0, x0 + w, y0 + h), p, 1.5f, i == 1 && j == 1});
            }
        resolve_overlaps(parts);
        for (std::size_t a = 0; a < parts.size(); ++a) {
            CHECK(parts[a].scale >= 1.0f && parts[a].scale <= 1.5f);
            for (std::size_t b = a + 1; b < parts.size(); ++b)
                if (!parts[a].box.overlaps(parts[b].box))
                    CHECK(!scaled(parts[a].box, parts[a].pivot, parts[a].scale).overlaps(scaled(parts[b].box, parts[b].pivot, parts[b].scale)));
        }
    }
}

TEST(explosion_flash_curve) {
    // before 0.025 s and from 0.145 s on: nothing; in between the colour of RenderFullScreenFlash, alpha t^2 x 128.9
    CHECK(!explosion_flash(-1.0f, false).on && !explosion_flash(0.02f, false).on && !explosion_flash(0.145f, false).on &&
          !explosion_flash(5.0f, false).on);
    const ExplosionFlash first = explosion_flash(0.025f, false);
    CHECK(first.on && first.r == 255 && first.g == 204 && first.b == 127 && first.a == 128);
    const ExplosionFlash mid = explosion_flash(0.085f, false);   // t = 0.5
    CHECK(mid.on && mid.a == 32);
    float prev = 1000;
    for (float e = 0.025f; e < 0.144f; e += 0.004f) {
        const ExplosionFlash f = explosion_flash(e, false);
        CHECK(f.on && f.a <= prev);
        prev = f.a;
    }
}

TEST(explosion_flash_reduced) {
    // Reduced: never more than kFlashCap of white (Cs As / 128, the brightest channel 255), the same window and colour
    for (float e = 0.025f; e < 0.144f; e += 0.002f) {
        const ExplosionFlash full = explosion_flash(e, false), red = explosion_flash(e, true);
        CHECK(full.on == red.on && red.r == full.r && red.g == full.g && red.b == full.b);
        CHECK(red.a <= full.a);
        CHECK(double(red.r) * double(red.a) / 128.0 / 255.0 <= double(kFlashCap) + 1e-9);
    }
    CHECK(explosion_flash(0.025f, true).a >= 31 && explosion_flash(0.025f, true).a <= 32);
}

TEST_MAIN("accessibility: HUD colour filter, screen flashes, HUD size (src/b3replay/a11y)")
