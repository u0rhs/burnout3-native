// steam_art.h -- ADD TO STEAM's own pictures (steam.h): the port's logo (docs/logo.png, built into the launcher as
// include/b3port/steam_logo_data.h by tools/release/make_steam_logo.py) on a background drawn here (a dark ground, a
// flame-orange glow, speed streaks at the logo's slant). Nothing of the game's disc or box. No SDL; the PNG files are
// encoded with zlib. tests/test_launcher_steam.cpp.
#ifndef B3LAUNCHER_STEAM_ART_H
#define B3LAUNCHER_STEAM_ART_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace b3launcher::steam_art {

struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;   // straight alpha, rows top to bottom
};

// An 8-bit RGB or RGBA, non-interlaced PNG (what the launcher's own pictures are); nullopt otherwise
std::optional<Image> decode_png(const std::uint8_t* data, std::size_t n, std::string* why = nullptr);
// PNG bytes (zlib-compressed, a filter per row); `alpha` false writes RGB (the alpha channel dropped)
std::vector<std::uint8_t> encode_png(const Image& im, bool alpha);

// The sizes Steam's library asks for
inline constexpr int kCoverW = 600, kCoverH = 900;    // the library's cover (portrait capsule)
inline constexpr int kWideW = 920, kWideH = 430;      // the wide capsule (recent games)
inline constexpr int kHeroW = 1920, kHeroH = 620;     // the game page's background (no logo: Steam draws the logo file on it)

// The port's logo as built into the launcher (920 x 459, transparent)
const Image& logo();
const std::uint8_t* logo_png(std::size_t* n);   // its PNG file's bytes

Image cover();
Image wide();
Image hero();

}  // namespace b3launcher::steam_art

#endif
