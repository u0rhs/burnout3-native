// settings1: our PC settings applied to the view (pcsettings.h).
#include "pcsettings.h"

#include "view.h"
#include "b3gfx/b3gfx.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace b3replay {

void apply_view(View& v, const b3settings::Settings& s) {
    v.effects.shadows = s.shadows;
    v.effects.trails = s.skid_marks;
    v.effects.sparks = s.sparks;
    v.effects.scrapes = s.scrapes;
    v.effects.popcorn = s.popcorn;
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
    v.deform = s.damage;
    v.glass_damage = s.glass_damage;
    v.fov_scale = b3settings::fov_tangent_scale(s.fov);
    v.fov_correction = s.fov_correction;
    v.chase_distance = float(std::clamp(s.camera_distance, 90, 150)) / 100.0f;   // 1.1
    v.chase_height = float(std::clamp(s.camera_height, 0, 100)) / 100.0f;
    v.hud_colour_filter = s.hud_colour_filter;   // accessibility (a11y.h): the 2D layer only, drawn only
    v.reduce_flashes = s.screen_flashes == 1;
    v.hud_size = s.hud_size;   // textsize1 (a11y.h): the race HUD scaled per component, drawn only
    v.ghost_mode = s.ghost;   // ghost1 (ghost.h): Time Attack ghosts, drawn only
    v.series_glyphs = s.series_shoulders == 1;   // carsel1: the car select's L1 / R1 glyphs (with the shoulders' series change)
}

void scale_options(const b3settings::Settings& s, b3gfx::Options& o) { o.render_scale = float(std::clamp(s.render_scale, 50, 100)) / 100.0f; }

void filter_options(b3settings::Filter f, b3gfx::Options& o) {
    using b3settings::Filter;
    o.point = f == Filter::kPoint;
    o.mip_nearest = f == Filter::kBilinear;
    o.anisotropy = f == Filter::kAniso2 ? 2.0f : f == Filter::kAniso4 ? 4.0f : f == Filter::kAniso8 ? 8.0f : f == Filter::kAniso16 ? 16.0f : 1.0f;
}

}  // namespace b3replay
