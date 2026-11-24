// xboxglow.h -- TUNNEL LIGHTS' Xbox looks (docs/remaster-features.md §35.5): which lamps, which textures carry a glow
// mask, and the setting's value from the environment. Pure (the tests' and view.cpp's).
#ifndef B3REPLAY_XBOXGLOW_H
#define B3REPLAY_XBOXGLOW_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "b3asset/model.h"

namespace b3replay {

// The Xbox looks' strength: the masked texels at this x their own colour (emissive.frag mode 8; a white tube 1.4 in frame
// units, over the routes' bloom thresholds 0.65-0.75: the bloom makes the glow). Set by eye against the bloom without Xbox
// footage; $B3_XBOX_GLOW_GAIN overrides it (to match the owner's footage).
constexpr float kXboxGlowGain = 1.5f;
inline float xbox_glow_gain() {
    static const float g = [] {
        const char* e = std::getenv("B3_XBOX_GLOW_GAIN");
        const float v = e && *e ? float(std::atof(e)) : kXboxGlowGain;
        return v > 0.0f && v < 8.0f ? v : kXboxGlowGain;
    }();
    return g;
}

// TUNNEL LIGHTS' value from $B3_TUNNEL_LIGHTS (nightfix §35.4, xboxglow §35.5): "game" / "glow" / "xbox", or the number
// 0-2. The withdrawn Xbox all lamps ("xboxall", 3) reads as Xbox
inline int tunnel_lights_env(const char* e) {
    if (!e || !*e) return 0;
    if (e[0] >= '0' && e[0] <= '3' && !e[1]) return e[0] == '3' ? 2 : e[0] - '0';
    const std::string v = e;
    return v == "glow" ? 1 : v == "xbox" || v == "xboxall" ? 2 : 0;
}

// xboxglow (§35.5): the lamps outside the tunnels whose textures carry a glow mask (DrawItem::day_glow 2: the mask is kept,
// no day glow since TUNNEL LIGHTS' Xbox all lamps was withdrawn): street
// lamps, a car park's and a restaurant's lamps, the toll plaza's, a lit window, traffic lights, an LED speed sign. From the
// census of the textures drawn opaque with a lamp-like alpha mask (a lit part on at most 30 % of the texture, 200+ bright),
// each looked at; snow (ATB_Snow_*), pavements and backdrops with broad masks are not lamps and are left out
inline bool xbox_lamp(const std::string& name) {
    static const char* const kLamps[] = {"ATB_Lamp", "ATB_LampostG", "ATB_Lamp_Toll", "ATB_TollBooth", "HK_post", "HK_ResLite",
                                         "HK_Midparking2", "HK_Midparking5", "Alleway7", "R_furnit3", "Freeway_info_speed"};
    for (const char* l : kLamps)
        if (name == l) return true;
    return false;
}

// xboxglow (§35.5): a texture whose alpha is a glow mask -- the lamp's lit part painted for the Xbox renderer, the rest ~0
// (the PS2 draws these surfaces opaque and ignores it): texels at alpha >= 128 on 0.2-50 % of the texture and at least
// 160 bright there (the brightest channel's mean), 30 brighter than the rest. A cut-out (the lamp rig's wires: the lit
// part darker than the rest) or an opaque texture is not one. An HD pack's replacement (`original` set: the levels are the
// pack's) is never taken for one: the GPU samples the replacement's alpha, which need not be the mask (an opaque one would
// light the whole fitting), so the fitting keeps the brightness ramp there.
inline bool glow_mask_of(const b3asset::Texture& t) {
    if (!t.original.empty()) return false;
    const b3asset::Image* im = !t.levels.empty() && !t.levels[0].rgba.empty() ? &t.levels[0] : nullptr;
    if (!im || im->w <= 0 || im->h <= 0 || im->rgba.size() < std::size_t(im->w) * std::size_t(im->h) * 4) return false;
    const std::size_t n = std::size_t(im->w) * std::size_t(im->h);
    double hi = 0, lo = 0;
    std::size_t nhi = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* p = &im->rgba[4 * i];
        const double m = std::max({p[0], p[1], p[2]});
        if (p[3] >= 128) { hi += m; ++nhi; } else lo += m;
    }
    if (nhi * 500 < n || nhi * 2 > n) return false;
    hi /= double(nhi);
    lo /= double(n - nhi);
    return hi >= 160.0 && hi >= lo + 30.0;
}


}  // namespace b3replay

#endif
