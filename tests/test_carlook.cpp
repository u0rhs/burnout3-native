// reflect2: the enhanced car look's values (src/b3replay/carlook.h): the table covers every field, the ranges hold the
// defaults, the ini round-trips, bad input is warned about and clamped, and the shipped defaults equal the hard-coded block.
#include "test.h"

#include "carlook.h"

#include <cmath>
#include <set>
#include <string>

using namespace b3replay;

TEST(table_covers_every_field_and_ranges_hold_the_defaults) {
    const b3gfx::CarLook d;
    CHECK(carlook::params().size() * sizeof(float) == sizeof(b3gfx::CarLook));   // one row per float field
    std::set<std::string> keys;
    for (const auto& p : carlook::params()) {
        CHECK(keys.insert(p.key).second);
        CHECK(p.lo < p.hi && p.step > 0 && (p.hi - p.lo) / p.step >= 4);
        CHECK(d.*(p.field) >= p.lo && d.*(p.field) <= p.hi);
        CHECK(std::string(p.label).size() > 3 && std::string(p.help).size() > 10);
    }
}

// the retuned look (reflect2): the values the shader was tuned with. A change here is a change of the default look
TEST(defaults_are_the_hard_coded_block) {
    const b3gfx::CarLook d = carlook::defaults();
    const b3gfx::CarLook block{
        .strength = 0.5f, .fresnel_power = 4.0f, .fresnel_floor = 0.10f, .paint_gain = 1.6f, .paint_darken = 0.0f,
        .glint_size = 1.0f, .glint_gain = 1.5f, .sky_gain = 0.9f, .horizon_gain = 0.8f, .ground_level = 0.05f, .haze = 0.0f,
        .glass_weight = 0.30f, .glass_fresnel = 0.65f, .glass_gain = 1.8f, .glass_darken = 0.45f, .rim = 0.20f,
        .colour_keep = 1.0f, .traffic = 0.55f};
    for (const auto& p : carlook::params()) CHECK(d.*(p.field) == block.*(p.field));
    CHECK(carlook::defaults().haze == 0.0f);   // no uniform veil by default
    CHECK(carlook::defaults().colour_keep == 1.0f);   // the reflection never tints the paint by default
    // the code block of the defaults parses back (the owner's file) to the same look
    const std::string c = carlook::code_block(d);
    CHECK(c.find(".paint_gain = 1.6f") != std::string::npos && c.find("b3gfx::CarLook{") == 0);
}

TEST(ini_round_trip_ranges_and_warnings) {
    b3gfx::CarLook l;
    l.paint_gain = 2.5f;
    l.glint_size = 0.5f;
    l.haze = 0.1f;
    const b3gfx::CarLook r = carlook::from_ini(carlook::to_ini(l));
    for (const auto& p : carlook::params()) CHECK(std::fabs(r.*(p.field) - l.*(p.field)) < 1e-3f);
    std::vector<std::string> w;
    const b3gfx::CarLook b = carlook::from_ini("paint_gain = 99\nnope = 1\nrim = abc\n# c\nglint_gain = 2 # note\nbroken line\n", &w);
    CHECK(b.paint_gain == 4.0f);                       // clamped to the range
    CHECK(b.rim == b3gfx::CarLook{}.rim);              // bad number: the default kept
    CHECK(b.glint_gain == 2.0f);
    CHECK(w.size() == 4);                              // clamped, unknown, bad number, no '='
    CHECK(carlook::from_ini("").strength == 0.5f);
    carlook::set_current(carlook::from_ini("strength = 7\n"));   // set_current clamps too
    CHECK(carlook::current().strength == 2.0f);
    carlook::set_current(carlook::defaults());
    CHECK(carlook::current().strength == 0.5f);
}

TEST(changed_values_only_and_the_values_file) {
    b3gfx::CarLook l;
    l.sky_gain = 1.3f;
    const std::string c = carlook::code_block(l, false);
    CHECK(c.find(".sky_gain = 1.3f") != std::string::npos && c.find("paint_gain") == std::string::npos);
    const std::string v = carlook::values_text(l);
    CHECK(v.find("sky_gain = 1.3") != std::string::npos && v.find("b3gfx::CarLook{") != std::string::npos);
    CHECK(carlook::at_default(b3gfx::CarLook{}, carlook::params()[0]));
    CHECK(!carlook::at_default(l, carlook::params()[7]));
}

TEST_MAIN("b3replay carlook (reflect2)")
