// The rain's sound (weather, remaster-features §38; src/b3replay/rain_audio.h): the game's samples untouched when off,
// a fade in and out with no step, quieter and duller in a tunnel, louder with speed, saturating at the s16 range.
#include "test.h"

#include "rain_audio.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {

using b3replay::RainAudio;
using b3replay::RainAudioState;

double rms(const std::vector<std::int16_t>& v, std::size_t from = 0) {
    double s = 0;
    for (std::size_t i = from; i < v.size(); ++i) s += double(v[i]) * double(v[i]);
    return std::sqrt(s / double(v.size() - from));
}

// `seconds` of rain into silence, in 800-frame blocks (one VBlank each); returns the last second's samples
std::vector<std::int16_t> run(RainAudio& r, const RainAudioState& s, double seconds) {
    std::vector<std::int16_t> out;
    const int blocks = int(seconds * 60.0);
    for (int b = 0; b < blocks; ++b) {
        std::vector<std::int16_t> blk(1600, 0);
        r.mix(blk.data(), 800, s);
        if (b >= blocks - 60) out.insert(out.end(), blk.begin(), blk.end());
    }
    return out;
}

}  // namespace

TEST(off_leaves_the_game_alone) {
    RainAudio r;
    std::vector<std::int16_t> v(1600);
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = std::int16_t(int(i * 37 % 2000) - 1000);
    const std::vector<std::int16_t> orig = v;
    r.mix(v.data(), 800, RainAudioState{});
    CHECK(v == orig);
}

TEST(rain_in_the_open_and_in_a_tunnel) {
    RainAudio open, tunnel;
    RainAudioState s;
    s.on = true;
    const double ro = rms(run(open, s, 3.0));
    s.enclosed = 1.0f;
    const double rt = rms(run(tunnel, s, 3.0));
    std::printf("  rain in the open: rms %.0f, in a tunnel %.0f\n", ro, rt);
    CHECK(ro > 150.0 && ro < 4000.0);   // audible, well under full scale (rainvol: kLevel 0.5)
    CHECK(rt > 25.0 && rt < 0.8 * ro);  // quieter inside
}

// rainvol: kLevel scales the whole sound: the state's volume 1 / kLevel gives what volume 1 gave before it (twice as
// loud now), the same noise
TEST(rain_level_is_one_factor) {
    RainAudio a, b;
    RainAudioState s;
    s.on = true;
    s.speed = 30.0f;
    const double r1 = rms(run(a, s, 3.0));
    s.volume = 1.0f / RainAudio::kLevel;
    const double r2 = rms(run(b, s, 3.0));
    CHECK(RainAudio::kLevel > 0.0f && RainAudio::kLevel <= 1.0f);
    const double want = 1.0 / double(RainAudio::kLevel);   // (the same noise, scaled: the ratio is the factor)
    CHECK(std::fabs(r2 / r1 - want) < 0.05 * want);
}

TEST(louder_with_speed_and_volume) {
    RainAudio still, fast, low;
    RainAudioState s;
    s.on = true;
    const double r0 = rms(run(still, s, 3.0));
    s.speed = 50.0f;
    const double r1 = rms(run(fast, s, 3.0));
    s.volume = 0.25f;
    const double r2 = rms(run(low, s, 3.0));
    CHECK(r1 > 1.2 * r0 && r2 < 0.4 * r1);
}

TEST(fades_without_a_step) {
    // on for 2 s then off: no sample-to-sample jump above the noise's own, and silence again after the fade
    RainAudio r;
    RainAudioState s;
    s.on = true;
    run(r, s, 2.0);
    s.on = false;
    std::vector<std::int16_t> all;
    for (int b = 0; b < 240; ++b) {
        std::vector<std::int16_t> blk(1600, 0);
        r.mix(blk.data(), 800, s);
        all.insert(all.end(), blk.begin(), blk.end());
    }
    CHECK(rms(all, all.size() - 1600) < 1.0);   // 4 s later: silent
    std::vector<std::int16_t> v(1600, 1234);
    r.mix(v.data(), 800, s);
    CHECK(v == std::vector<std::int16_t>(1600, 1234));   // and the game's samples untouched again
}

TEST(clean_stays_within_the_knee) {
    // the game's samples are never changed: the sum stays within the limiter's knee (29000), at the loudest volume, and
    // the rain adds nothing where the game is past it
    RainAudio r;
    RainAudioState s;
    s.on = true;
    s.speed = 50.0f;
    s.volume = 2.0f;   // RAIN VOLUME 200 % x EFFECTS 100 %
    run(r, s, 2.0);
    std::vector<std::int16_t> hot(1600, 28990), over(1600, 32000), neg(1600, -28995);
    r.mix(hot.data(), 800, s);
    r.mix(over.data(), 800, s);
    r.mix(neg.data(), 800, s);
    for (std::int16_t x : hot) CHECK(x <= 29000);
    for (std::int16_t x : neg) CHECK(x >= -29000);
    CHECK(over == std::vector<std::int16_t>(1600, 32000));
}

// rainfe: the renderer's rain state holds only while race frames keep setting it (the front end, a loading screen:
// none), so a rain race's sound stops in the menus; and once off, the mix fades to nothing
TEST(rain_state_expires_without_race_frames) {
    const std::uint64_t t = 5000000000ull;
    CHECK(!RainAudio::state_live(t, 0));                              // never set
    CHECK(RainAudio::state_live(t, t) && RainAudio::state_live(t + RainAudio::kHoldNs, t));
    CHECK(!RainAudio::state_live(t + RainAudio::kHoldNs + 1, t));     // no race frame for a second: off
    CHECK(RainAudio::state_live(t + 500000000ull, t));                // a half-second render hitch: still on
    CHECK(RainAudio::state_live(t - 1, t));                           // (set just after this read of the clock: live)
    RainAudio r;
    RainAudioState s;
    s.on = true;
    (void)run(r, s, 2.0);                                             // raining
    s.on = false;                                                     // the state expired: off
    const auto after = run(r, s, 4.0);
    CHECK(rms(after, after.size() - 9600) < 1.0);                     // silent once the fade is over
}

// rainpause: the rain follows the game's sound -- off under the pause menu and in photo mode, on in results and replays
TEST(rain_follows_the_game_sound) {
    CHECK(RainAudio::sounding(true, true, false, false));     // racing in the rain (results over the race, the replay: the same)
    CHECK(!RainAudio::sounding(true, true, true, false));     // the pause menu (its PC SETTINGS too)
    CHECK(!RainAudio::sounding(true, true, false, true));     // photo mode
    CHECK(!RainAudio::sounding(true, false, false, false));   // the race's clock stopped
    CHECK(!RainAudio::sounding(false, true, false, false));   // dry
}

TEST(non_finite_input_resets) {
    RainAudio r;
    RainAudioState s;
    s.on = true;
    s.speed = std::nanf("");
    std::vector<std::int16_t> v(1600, 0);
    r.mix(v.data(), 800, s);   // a NaN speed counts as 0
    for (std::int16_t x : v) CHECK(x > -2000 && x < 2000);
    s.speed = 10.0f;
    r.mix(v.data(), 800, s);
    const auto out = run(r, s, 1.0);
    CHECK(rms(out) > 10.0 && rms(out) < 6000.0);
}

TEST_MAIN("rain audio (weather)")
