// xboxglow (src/b3replay/xboxglow.h, docs/remaster-features.md §35.5): TUNNEL LIGHTS' value from the environment, the
// lamps outside the tunnels (their glow masks), and which textures' alpha is a glow mask (a lamp's lit part) -- not a cut-out, not opaque,
// not a broad gloss mask.
#include "test.h"

#include "xboxglow.h"

#include <cstdint>
#include <vector>

namespace {

// a w x h texture: `lit` texels (the first ones) at colour `lit_c`, alpha `lit_a`; the rest colour `rest_c`, alpha `rest_a`
b3asset::Texture tex(int w, int h, std::size_t lit, std::uint8_t lit_c, std::uint8_t lit_a, std::uint8_t rest_c, std::uint8_t rest_a) {
    b3asset::Texture t;
    b3asset::Image im;
    im.w = w;
    im.h = h;
    im.rgba.resize(std::size_t(w) * std::size_t(h) * 4);
    for (std::size_t i = 0; i < std::size_t(w) * std::size_t(h); ++i) {
        const bool l = i < lit;
        im.rgba[4 * i] = im.rgba[4 * i + 1] = im.rgba[4 * i + 2] = l ? lit_c : rest_c;
        im.rgba[4 * i + 3] = l ? lit_a : rest_a;
    }
    t.levels.push_back(im);
    return t;
}

TEST(env_values) {
    CHECK(b3replay::tunnel_lights_env("game") == 0);
    CHECK(b3replay::tunnel_lights_env("glow") == 1);
    CHECK(b3replay::tunnel_lights_env("xbox") == 2);
    CHECK(b3replay::tunnel_lights_env("xboxall") == 2 && b3replay::tunnel_lights_env("3") == 2);   // (all lamps withdrawn)
    CHECK(b3replay::tunnel_lights_env("2") == 2 && b3replay::tunnel_lights_env("0") == 0);
    CHECK(b3replay::tunnel_lights_env("9") == 0 && b3replay::tunnel_lights_env("") == 0 && b3replay::tunnel_lights_env(nullptr) == 0);
}

TEST(lamps) {
    CHECK(b3replay::xbox_lamp("ATB_LampostG") && b3replay::xbox_lamp("R_furnit3") && b3replay::xbox_lamp("Alleway7"));
    CHECK(!b3replay::xbox_lamp("ATB_Snow_Heaped") && !b3replay::xbox_lamp("pavement2b") && !b3replay::xbox_lamp("ATB_TunnelB"));
}

TEST(glow_masks) {
    // a wall with a lamp tube: 3 % of it masked, the tube white, the wall grey and unmasked (ATB_TunnelB)
    CHECK(b3replay::glow_mask_of(tex(64, 64, 120, 250, 255, 150, 4)));
    // a cut-out: the masked part (the rig's wires) darker than the rest (HK_INS_LightRig)
    CHECK(!b3replay::glow_mask_of(tex(64, 64, 1800, 140, 255, 230, 0)));
    // opaque: every texel at alpha 255
    CHECK(!b3replay::glow_mask_of(tex(64, 64, 0, 0, 0, 200, 255)));
    // broad: 60 % masked (a gloss mask: ATB_BridgeStone)
    CHECK(!b3replay::glow_mask_of(tex(64, 64, 2458, 220, 255, 100, 0)));
    // a dim mask: the masked texels not bright (ATB_TunnelRnd's)
    CHECK(!b3replay::glow_mask_of(tex(64, 64, 120, 130, 255, 90, 4)));
    // no pixels
    CHECK(!b3replay::glow_mask_of(b3asset::Texture{}));
    // an HD pack's replacement (opaque here) of a masked texture: not a mask (the GPU samples the replacement's alpha)
    b3asset::Texture hd = tex(256, 256, 0, 0, 0, 200, 255);
    hd.original = tex(64, 64, 120, 250, 255, 150, 4).levels;
    CHECK(!b3replay::glow_mask_of(hd));
}

}  // namespace

TEST_MAIN("TUNNEL LIGHTS' Xbox looks: the setting's value, the lamps, the glow masks (xboxglow)")
