#include "sun.h"

#include <cmath>
#include <cstdio>

#include "scene.h"   // hdkey: upload_texture

namespace b3view {

Sun::Flare Sun::read_flare(const std::string& game) {
    Flare f;
    f.ok = b3asset::read_sun_flare_table(game + "/SLUS_210.50", f.table, &f.why);
    return f;
}

bool Sun::load(b3gfx::Device& dev, const b3asset::Enviro& env, const std::string& route, const std::string& game, std::string* why) {
    if (env.sun_corona.levels.empty() || !b3asset::route_has_sun(route)) { *this = Sun{}; return false; }   // (the table not read)
    return load(dev, env, route, read_flare(game), why);
}

bool Sun::load(b3gfx::Device& dev, const b3asset::Enviro& env, const std::string& route, const Flare& flare, std::string* why) {
    *this = Sun{};
    if (env.sun_corona.levels.empty() || !b3asset::route_has_sun(route)) return false;   // Prepare: field_13C = 0
    // hdkey: and its original (tex_w / tex_h stay the replacement's: the flare's half-texel UV insets)
    texture = upload_texture(dev, env.sun_corona.levels, env.sun_corona.original);
    tex_w = env.sun_corona.levels[0].w;
    tex_h = env.sun_corona.levels[0].h;
    // SunOcclusionTest: SetupCamera(dir, (env +0x70 xyz, w = light entry +0x30 = block 0 +0x20), 12, env +0x90);
    // D_01D9B970 = colour x colour.w x 128 [C caller matched, L SetupCamera draft]
    for (int c = 0; c < 3; ++c) colour[c] = env.sun_colour[c] * env.block[0].sun_w * 128.0f;
    corona_scale = env.corona_scale;
    table = flare.table;
    if (!flare.ok && why) *why = flare.why + " (corona without the lens flare)";
    on = texture != b3gfx::kNone;
    return on;
}

void Sun::frame(b3gfx::FrameParams& fp, float display_aspect, int gm0, float aspect, float now) const {
    fp.sun_sprites.clear();
    if (!on) return;
    // SetupCamera: the point camera + 50 x unit(to-sun); behind when its view depth <= 0.01
    float d[3] = {fp.to_sun[0], fp.to_sun[1], fp.to_sun[2]};
    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(l > 0)) return;
    const float p[4] = {fp.eye[0] + 50.0f * d[0] / l, fp.eye[1] + 50.0f * d[1] / l, fp.eye[2] + 50.0f * d[2] / l, 1.0f};
    float c[4] = {};
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k) c[r] += fp.view_proj[k * 4 + r] * p[k];
    if (c[3] <= 0.01f) return;
    const float D = display_aspect > 0 ? display_aspect : 4.0f / 3.0f;
    const float W = 480.0f * D, H = 448.0f;   // the game's pixel grid over the output
    b3asset::SunFlareInput in;
    in.sun_px[0] = (c[0] / c[3] + 1.0f) * 0.5f * W;
    in.sun_px[1] = (1.0f - c[1] / c[3]) * 0.5f * H;
    in.screen[0] = W;
    in.screen[1] = H;
    in.radius = corona_scale * float(gm0 > 0 ? gm0 : 640);
    in.aspect = aspect > 0 ? aspect : 448.0f / 480.0f;
    for (int k = 0; k < 3; ++k) in.colour[k] = colour[k];
    in.now = now;
    const b3asset::SunFlare sf = b3asset::sun_flare(in, table, tex_w, tex_h);
    if (!sf.drawn) return;
    const auto nx = [&](float x) { return x / W * 2.0f - 1.0f; };
    const auto ny = [&](float y) { return 1.0f - y / H * 2.0f; };
    // the box as the GS sprites cover it, clamped to the output (the game's clamp is to its screen)
    fp.sun_box[0] = std::fmax(-1.0f, nx(sf.box[0]));
    fp.sun_box[2] = std::fmin(1.0f, nx(sf.box[2]));
    fp.sun_box[1] = std::fmax(-1.0f, ny(sf.box[3]));
    fp.sun_box[3] = std::fmin(1.0f, ny(sf.box[1]));
    for (const auto& s : sf.sprites) {
        b3gfx::FrameParams::SunSprite o{};
        o.rect[0] = nx(s.x0); o.rect[2] = nx(s.x1);
        o.rect[1] = ny(s.y0); o.rect[3] = ny(s.y1);
        o.uv[0] = s.u0; o.uv[1] = s.v0; o.uv[2] = s.u1; o.uv[3] = s.v1;
        for (int k = 0; k < 3; ++k) o.rgb[k] = float(std::max(0, std::min(255, s.rgb[k]))) / 255.0f;
        fp.sun_sprites.push_back(o);
    }
    fp.sun_texture = texture;
}

}  // namespace b3view
