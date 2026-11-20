// settings1: our PC settings applied to the view (pcsettings.h).
#include "pcsettings.h"

#include "view.h"
#include "b3gfx/b3gfx.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace b3replay {

namespace {
// SUN SHADOWS (the row; $B3_SUN_SHADOWS wins) and its map's side: 2048 texels from ANTI-ALIASING 4x (the High preset),
// 1024 below (Medium); $B3_SUN_SHADOW_SIZE wins
bool sun_shadows_on(const b3settings::Settings& s) {
    if (const char* e = std::getenv("B3_SUN_SHADOWS"); e && *e) return *e != '0';
    return s.sun_shadows;
}
int sun_shadow_side(const b3settings::Settings& s) {
    if (const char* e = std::getenv("B3_SUN_SHADOW_SIZE"); e && *e) return std::clamp(std::atoi(e), 256, 4096);
    return s.msaa >= 4 ? 2048 : 1024;
}
}  // namespace

void apply_view(View& v, const b3settings::Settings& s) {
    // split2p: both views' effects (split screen keeps player 2's in View::view2_state, swapped in for its view only: it
    // kept the defaults, so SHADOWS, SKID MARKS, SPARKS, SCRAPES and CRASH DEBRIS changed only player 1's view)
    for (Effects* e : {&v.effects, &v.view2_state.effects}) {
        e->shadows = s.shadows;
        e->trails = s.skid_marks;
        e->sparks = s.sparks;
        e->scrapes = s.scrapes;
        e->popcorn = s.popcorn;
    }
    v.show_sun = s.sun;
    v.show_coronas = s.coronas;
    v.show_blur = s.motion_blur;
    v.radial_scale = float(s.radial_blur) / 100.0f;
    v.show_bloom = s.bloom;
    v.bloom_level = b3replay::bloom_level_env(s.bloom_level == 2 ? 2 : 1);   // bloom2: the golden record's look wins
    if (v.post_game == s.post_pc) { v.post_game = !s.post_pc; v.bloom_last = -1; }   // as key Z: the bloom's kept picture restarts
    v.hud_wide = s.hud_wide;
    v.fe_layout = s.fe_layout;   // fe3 (the HUD lists rebuild when it changes: hud_built_fe_layout)
    v.movie_fill = s.movie_fill;
    v.split_layout = s.split_layout;   // splitlayout1 (splitlayout.h): the split screen's layout, drawn only
    v.whole_track = s.whole_track;   // mac5: DRAW DISTANCE
    v.car_lod = s.car_detail_highest ? 3 : -1;
    v.car_reflect = s.car_reflections;   // reflect1 (View::apply_car_reflections), drawn only
    if (const char* e = std::getenv("B3_CAR_REFLECTIONS")) {   // the golden record's reference look (tools/golden/baseline.sh) wins
        if (!std::strcmp(e, "off")) v.car_reflect = 0;
        else if (!std::strcmp(e, "original")) v.car_reflect = 1;
        else if (!std::strcmp(e, "enhanced")) v.car_reflect = 2;
    }
    // reflect-tunnel: the track in the car env map goes with the track's own reflection pass, SPECULAR (the full-screen
    // specular): the LOW preset is the one preset that turns it off, so weak hardware gets the sky-only map (no env pass
    // draws); $B3_CAR_ENV wins (the golden record pins sky)
    v.car_env_world = s.specular;
    if (const int o = car_env_override(); o >= 0) v.car_env_world = o == 1;
    v.deform = s.damage;
    v.glass_damage = s.glass_damage;
    v.fov_scale = b3settings::fov_tangent_scale(s.fov);
    v.fov_correction = s.fov_correction;
    v.chase_distance = float(std::clamp(s.camera_distance, 90, 150)) / 100.0f;   // 1.1
    v.chase_height = float(std::clamp(s.camera_height, 0, 100)) / 100.0f;
    {   // carpaint (view.h CarPaint): the settings for player 1, the environment's paint over them (B3_PAINT, B3_DECALS)
        CarPaint p1 = v.car_paint[0];
        p1.custom = s.car_paint == 1;
        p1.rgb = std::uint32_t(std::clamp(s.paint_red, 0, 255)) << 16 | std::uint32_t(std::clamp(s.paint_green, 0, 255)) << 8 |
                 std::uint32_t(std::clamp(s.paint_blue, 0, 255));
        p1.decals = s.car_decals;
        p1.finish = std::clamp(s.paint_finish, 0, 2);
        v.car_paint[0] = car_paint_env(0, p1);
        CarPaint p2 = v.car_paint[1];   // padpaint: player 2's rows (B3_PAINT_P2 over them)
        p2.custom = s.p2_car_paint == 1;
        p2.rgb = std::uint32_t(std::clamp(s.p2_paint_red, 0, 255)) << 16 | std::uint32_t(std::clamp(s.p2_paint_green, 0, 255)) << 8 |
                 std::uint32_t(std::clamp(s.p2_paint_blue, 0, 255));
        p2.decals = s.p2_car_decals;
        p2.finish = p1.finish;
        v.car_paint[1] = car_paint_env(1, p2);
    }
    // night (view.h NightLook; remaster-features §17 / §18), drawn only: $B3_NIGHT / $B3_MOON win over the rows
    if (!night_env_set()) v.night.on = false;   // 1.3.0: night mode held back (TIME OF DAY hidden); $B3_NIGHT still turns it on
    if (const char* e = std::getenv("B3_MOON"); !e || !*e) v.night.moon = s.moon;
    // nightlights (§17.2): the projected headlight beams by the preset the values match: HIGH, ULTRA and custom values 8
    // cars, MEDIUM 4, LOW and PS2 ORIGINAL the ground quads (no depth pre-pass); $B3_NIGHT_LIGHTS wins
    if (const char* e = std::getenv("B3_NIGHT_LIGHTS"); !e || !*e) {
        const b3settings::Preset p = b3settings::matching_preset(s);
        v.night.lights = p == b3settings::Preset::kLow || p == b3settings::Preset::kPs2 ? 0 : p == b3settings::Preset::kMedium ? 4 : 8;
    }
    v.surface_detail = s.surface_detail;
    v.ambient_occlusion = false;   // 1.3.0: AMBIENT OCCLUSION held back (noise lines over the picture)
    // SUN SHADOWS (view.h; remaster-features §20), drawn only: 2048 texels from ANTI-ALIASING 4x (the High preset), the
    // softer 5 x 5 kernel at 8x (Ultra), 1024 below (Medium); $B3_SUN_SHADOWS / _SIZE / _FILTER win
    v.sun_shadows = sun_shadows_on(s);
    v.sun_shadow_size = sun_shadow_side(s);
    v.sun_shadow_filter = s.msaa >= 8 ? 1 : 0;
    if (const char* e = std::getenv("B3_SUN_SHADOW_FILTER"); e && *e) v.sun_shadow_filter = *e == '1' ? 1 : 0;
    v.hud_colour_filter = s.hud_colour_filter;   // accessibility (a11y.h): the 2D layer only, drawn only
    v.reduce_flashes = s.screen_flashes == 1;
    v.hud_size = s.hud_size;   // textsize1 (a11y.h): the race HUD scaled per component, drawn only
    v.ghost_mode = s.ghost;   // ghost1 (ghost.h): Time Attack ghosts, drawn only
    v.series_glyphs = s.series_shoulders == 1;   // carsel1: the car select's L1 / R1 glyphs (with the shoulders' series change)
}

void scale_options(const b3settings::Settings& s, b3gfx::Options& o) {
    o.render_scale = float(std::clamp(s.render_scale, 50, 200)) / 100.0f;
    // finalfix: what is on at start has its pipelines made with the fixed set; the map's size counts in the supersampling budget
    o.warm_sun_shadows = sun_shadows_on(s);
    o.sun_shadow_side = o.warm_sun_shadows ? sun_shadow_side(s) : 0;
    const char* surf = std::getenv("B3_SURFACE");   // (view.cpp surface_from_env: the prototype's switch, AO included)
    o.warm_ambient_occlusion = surf && *surf && *surf != '0';   // (the AO row is held back in 1.3.0)
}

void filter_options(b3settings::Filter f, b3gfx::Options& o) {
    using b3settings::Filter;
    o.point = f == Filter::kPoint;
    o.mip_nearest = f == Filter::kBilinear;
    o.anisotropy = f == Filter::kAniso2 ? 2.0f : f == Filter::kAniso4 ? 4.0f : f == Filter::kAniso8 ? 8.0f : f == Filter::kAniso16 ? 16.0f : 1.0f;
}

}  // namespace b3replay
