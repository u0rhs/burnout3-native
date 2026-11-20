// gameart.h -- the game's own front-end art for the launcher, read at run time from the user's game data folder (the
// disc files; nothing of the game's is in git or in the launcher's binary): its three 2D fonts and a few front-end
// textures. No SDL here (tests/test_launcher.cpp); main.cpp turns them into SDL textures.
//
// The fonts are linked into the game program (SLUS_210.50), not in a TXD [C decomp B3GraphicsManager_0019EE40.cpp
// CB3GraphicsManagerBase::Construct, data unit_0015F270.sdata]: three pointers each to a CGt2dFont definition and to a
// one-texture dictionary in the ELF's data, the glyph texture found by name ("GlobalFont", "HeadFont", "SmallFont"):
//   GlobalFont  def *0x4E1584, dictionary *0x4E1588     (the menus' text; 166 glyphs, 256 x 256)
//   HeadFont    def *0x4E1594, dictionary *0x4E1598     (headings; 30 glyphs, capitals only, 128 x 256)
//   SmallFont   def *0x4E158C, dictionary *0x4E1590     (small text; 166 glyphs, 128 x 128)
// A definition as stored (before CGt2dFont::Prepare fixes it up): +0 "3rev" tag, +4 texture (0), +8 / +0xC cell scale
// x / y, +0x10 baseline, +0x1C the last glyph's offset, +0x20 128 bucket offsets (from the definition), the glyphs from
// the lowest of them, 0x20 bytes each: u v w h (texture ST), offset x y, advance (f32), u16 code
// (gameview_native.cpp publish_fonts reads the same records from the running game). A dictionary is the TXD layout
// (b3asset/raster.h read_txd) with its offsets relative to its own start.
// The textures (b3asset decode_raster): GLOBAL.TXD "B3Logo", "grid" (the menus' grid cell), "big_curve", "FE_Element01";
// FRONTEND.TXD "titlescreen_logo", "Halo" (the blue arc).
#ifndef B3LAUNCHER_GAMEART_H
#define B3LAUNCHER_GAMEART_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b3mod { class Overlay; }

namespace b3launcher {

struct ArtImage {
    int w = 0, h = 0;                  // the decoded picture
    int tw = 0, th = 0;                // the GS texture's size (TEX0 TW / TH): what glyph ST coordinates are relative to
    std::vector<std::uint8_t> rgba;    // straight RGBA8
};

// settingsui: the box of a picture's visible pixels (alpha above `threshold`), grown by `margin` and kept inside the
// picture; x1 / y1 one past the last. A picture with none visible: the whole picture.
struct PixelBox { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
PixelBox visible_box(const ArtImage& im, int margin = 0, int threshold = 8);

struct FontGlyph {
    float u = 0, v = 0, w = 0, h = 0, ox = 0, oy = 0, advance = 0;
    std::uint16_t code = 0;
};

struct GameFont {
    float scale_x = 0, scale_y = 0, baseline = 0;
    std::vector<FontGlyph> glyphs;
    ArtImage tex;
    bool has(std::uint16_t c) const;
    const FontGlyph* find(std::uint16_t c) const;   // the game's lookup: the last glyph when there is none
    float line() const;                             // the tallest glyph's bottom (offset y + h)
    // CGt2dFont::GetStringWidth x size (0x1D8150): from the first glyph's ink to the last one's
    float width(std::u16string_view s, float size) const;
    // The `size` that makes the font's line `height` units tall (the PC SETTINGS page's rule)
    float size_for(float height) const;
};

enum FontId { kGlobalFont = 0, kHeadFont = 1, kSmallFont = 2 };

// The loadable segments of an ELF32 (the game program), for its data by virtual address.
struct ElfFile {
    std::vector<std::uint8_t> bytes;
    struct Segment { std::uint32_t va = 0, offset = 0, size = 0; };
    std::vector<Segment> segments;
    bool parse(std::string* why = nullptr);   // bytes -> segments (PT_LOAD with file bytes)
    // The file offset of [va, va + len) when one segment holds it
    std::optional<std::size_t> offset_of(std::uint32_t va, std::size_t len = 1) const;
    std::optional<std::uint32_t> u32(std::uint32_t va) const;
};

// A font definition at `p` (`n` bytes from its start); false with a reason when it is not one.
bool parse_font_def(const std::uint8_t* p, std::size_t n, GameFont& out, std::string* why = nullptr);
// Texture `name` (any case) of a texture dictionary at `p` (a TXD file, or a dictionary in the ELF), level 0 decoded.
std::optional<ArtImage> txd_texture(const std::uint8_t* p, std::size_t n, std::string_view name, std::string* why = nullptr);
// The three fonts from the game program's bytes (fonts[kGlobalFont] ...); false with a reason.
bool load_game_fonts(const ElfFile& elf, GameFont (&fonts)[3], std::string* why = nullptr);

// A file below `dir` (a folder, or a disc image: b3asset/discfs.h) by a relative path in any case ("DATA/GLOBAL.TXD"
// finds Data/Global.txd); empty when none.
std::filesystem::path find_ci(const std::filesystem::path& dir, std::string_view rel);
// UTF-8 to the game's 16-bit codes (a bad byte: '?')
std::u16string to_u16(std::string_view utf8);

struct GameArt {
    bool fonts = false;                        // the three fonts loaded
    GameFont font[3];
    std::map<std::string, ArtImage> images;    // by texture name, those found
    std::string why;                           // what was missing
    bool ok() const { return fonts; }
    const ArtImage* image(const std::string& name) const;
};
// Everything above from the disc folder or disc image `game_dir` (SLUS_210.50, DATA/GLOBAL.TXD, DATA/FRONTEND.TXD).
// modkit1b: `mods` (the mods that are on, b3mod::Overlay) answer first for the files they have, as in the game.
GameArt load_game_art(const std::filesystem::path& game_dir, const b3mod::Overlay* mods = nullptr);

}  // namespace b3launcher

#endif
