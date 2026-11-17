// modtool.h -- the mod kit's asset tools (mod kit part 3a, docs/modding/DESIGN.md §3; the CLI is
// tools/modding/b3modtool.cpp, the user guide docs/modding/b3modtool.md, tests tests/test_modtool.cpp).
//
//   glob       disc path patterns (`list`, `extract`)
//   b3patch    a binary delta of one file against the user's own (docs/modding/b3patch.md)
//   strings    the game's UTF-16 string tables (DATA/GLOBALUS.BIN, HEADUS.BIN) to UTF-8 text and back
//   png        PNG files in and out (zlib), for the texture tools
//   txd        RenderWare PS2 rasters (TXD dictionaries): to PNG and back into the same file, same size, same format
//   zip        reading a .b3mod (a ZIP) back, for `verify`
//   vdb        DATA/VDB.XML (the tuning defaults) to `name = value` text and back (part 3b)
//   audio      PS-ADPCM, WAV, RWS streams and AWD banks: to WAV and back (part 3b)
//
// Every function works on bytes in memory; nothing here writes the user's disc. Standard library, zlib and b3asset
// (the raster decoder, the disc reader) only.
#ifndef B3MODTOOL_MODTOOL_H
#define B3MODTOOL_MODTOOL_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "b3asset/raster.h"
#include "b3mod/mods.h"

namespace b3modtool {

using Buf = std::vector<std::uint8_t>;

// ---- glob ----
// `pattern` against a disc path ("DATA/GLOBAL.TXD"), ASCII-case-insensitively: `*` any run of characters except '/',
// `**` any run including '/', `?` one character. A pattern without '/' is matched against the file name alone (in any
// folder). '\' in either counts as '/'.
bool glob_match(std::string_view pattern, std::string_view path);

// What a file is, from its name and first bytes ("txd", "strings", "rws", "car", "track", "movie", ...; "" unknown)
std::string file_kind(std::string_view name, const std::uint8_t* head, std::size_t n);

// ---- b3patch (docs/modding/b3patch.md) ----
// The reader is the mod loader's (include/b3mod/mods.h: the game applies patches with the same code):
//   read_patch_info(patch, info, why)   the header and the op stream checked (no source needed)
//   apply_patch(src, patch, out, why)   `src` must be the file the patch was made from (size and FNV-1a 64); the result
//                                       is checked against the patch's target size and hash
using PatchInfo = b3mod::PatchInfo;
using b3mod::apply_patch;
using b3mod::read_patch_info;
constexpr std::uint32_t kPatchVersion = 1;
// A patch that turns `src` into `dst`.
Buf make_patch(const Buf& src, const Buf& dst, PatchInfo* info = nullptr);

// ---- string tables ----
// {u8 magic[8] = 08 03 F6 69 FA 22 36 B9, u32 count, u32 table offset (0x10)}, `count` u32 offsets, then the strings:
// UTF-16LE, each ended by a 0 unit, in id order (CB3Strings::GetString func_00271A00 reads table[id]).
struct StringTable {
    std::uint8_t magic[8] = {};
    std::uint32_t table_offset = 0x10;
    std::vector<std::u16string> s;
};
bool is_string_table(const std::uint8_t* p, std::size_t n);
bool read_strings(const Buf& file, StringTable& out, std::string* why = nullptr);
Buf write_strings(const StringTable& t);   // the strings packed in id order after the table
// The text form: "<id> = <text>" per line, UTF-8; escapes \n \r \t \\ \uXXXX (a UTF-16 unit); a trailing space as
//  ; '#' lines are comments.
std::string strings_to_text(const StringTable& t, const std::string& source);
// The lines of `text` replace the strings of `base` with their ids; ids it does not name keep base's text; ids from
// base.s.size() up may be added when they continue the table without a gap. `changed`: the strings whose text differs.
bool strings_from_text(std::string_view text, StringTable& base, std::string* why = nullptr, std::size_t* changed = nullptr);

// ---- PNG ----
Buf encode_png(int w, int h, const std::uint8_t* rgba);   // RGBA8, zlib-compressed
// Any non-interlaced PNG (grey, grey + alpha, RGB, RGBA, palette; 1-16 bits) -> RGBA8.
bool decode_png(const Buf& png, int& w, int& h, std::vector<std::uint8_t>& rgba, std::string* why = nullptr);

// ---- rasters (TXD) ----
// The raw texel values and CLUT of a raster as stored (no alpha mapping), beside the RGBA decode.
struct RasterView {
    b3asset::DecodedRaster dec;               // the RGBA levels (b3asset::decode_raster)
    std::vector<std::vector<std::uint32_t>> raw;   // per level, the stored value of each texel (an index or a colour)
    std::vector<std::uint32_t> clut_raw;      // indexed formats: the CLUT's stored colours (16 or 256)
    std::vector<std::uint32_t> clut_rgba;     // the same as RGBA8 (gs_color_to_rgba8)
    bool indexed = false;
    std::string overlap;                      // not empty: two uploads write one GS location (the raster cannot be written back)
};
bool view_raster(const Buf& file, std::size_t offset, RasterView& out, std::string* why = nullptr);

struct EncodeOptions {
    bool force_write = false;    // write the raster back through GS memory even when nothing changed (roundtrip)
    bool strict = false;         // every texel from its RGBA alone, the unedited ones too (the encoder's own census)
    bool keep_palette = false;   // indexed: never build a new CLUT; colours not in it take the nearest entry
    bool keep_mips = false;      // a mip level whose PNG was not edited stays as stored (default: rebuilt from level 0)
};
struct EncodeResult {
    bool changed = false;        // any texel or CLUT entry differs from the file's
    bool new_palette = false;    // a new CLUT was built
    std::size_t nearest = 0;     // texels that took the nearest CLUT colour (not exact)
    std::vector<std::string> notes;
};
// Replaces the raster at `offset` in `file` (in place; the file's size and layout never change) with `levels`: one
// RGBA image per stored level, level 0 first, each the level's own size. An empty image (w == 0) means "as stored".
bool encode_raster(Buf& file, std::size_t offset, const std::vector<b3asset::Image>& levels, const EncodeOptions& opt,
                   EncodeResult* res = nullptr, std::string* why = nullptr);

// A raster with several CLUTs (a car's paint atlas: paint k's CLUT at palOffset + b3asset::palette_cbp_add(k)).
// `paints[k]` is level 0 of paint k as b3asset::decode_raster(.., k) gives it (w == 0, or fewer pictures than paints:
// as stored). A paint whose picture is a recolour (each stored index shows one colour) changes only its own CLUT; any
// other edit re-quantises all paints together: one index picture whose 256 entries hold a colour per paint (median cut
// over the paints' colours side by side), the mip levels rebuilt from it. A picture with no alpha at all (every texel
// 255) keeps the stored alpha (the car atlases keep a mask there). The file's size and layout never change.
struct PaintEncodeResult {
    std::size_t changed = 0;          // paints whose picture differs from the stored one
    bool new_indices = false;         // the index picture was rebuilt (not only CLUTs)
    std::vector<std::string> notes;
};
bool encode_raster_paints(Buf& file, std::size_t offset, const std::vector<b3asset::Image>& paints,
                          PaintEncodeResult* res = nullptr, std::string* why = nullptr);

// A texture dictionary's rasters: TxdEntry offsets (b3asset::read_txd).
bool txd_offsets(const Buf& file, std::vector<std::size_t>& out, std::string* why = nullptr);

// Export / import through a set of named files (the CLI writes them to a folder; roundtrip keeps them in memory):
//   <NNN>_<name>.png         level 0 of raster NNN (its index in the dictionary, 3 digits or more)
//   <NNN>_<name>.mip<k>.png  its stored mip level k (when it has more than one)
//   txd.txt                  an index (what each file is; informative)
using FileSet = std::map<std::string, Buf>;
std::string raster_file_stem(std::size_t index, const std::string& name);
bool txd_export(const Buf& txd, const std::string& source, FileSet& out, std::string* why = nullptr);
struct ImportReport {
    std::size_t rasters = 0, changed = 0, new_palettes = 0, nearest = 0;
    std::vector<std::string> notes;
};
// `txd` (the original dictionary) with every raster whose PNG(s) `in` has replaced; files `in` lacks: as stored.
bool txd_import(const Buf& txd, const FileSet& in, const EncodeOptions& opt, Buf& out, ImportReport* rep = nullptr,
                std::string* why = nullptr);

// ---- ZIP (reading a .b3mod) ----
struct ZipEntry {
    std::string name;
    std::uint32_t csize = 0, usize = 0, crc = 0, offset = 0;
    std::uint16_t method = 0, flags = 0;
};
bool zip_list(const Buf& zip, std::vector<ZipEntry>& out, std::string* why = nullptr);
bool zip_read(const Buf& zip, const ZipEntry& e, Buf& out, std::string* why = nullptr);   // CRC checked

// ---- the disc ----
struct DiscFile {
    std::string key;    // its path on the disc, '/' separated, the disc's own case ("DATA/GLOBAL.TXD")
    std::string path;   // what read_host_file takes
    std::uint64_t size = 0;
};
// Every file of a disc folder or image (b3asset::disc), sorted by key.
std::vector<DiscFile> disc_files(const std::string& disc);

// ---- mods: verify and pack (docs/modding/b3modtool.md "verify", "pack") ----
struct VerifyReport {
    std::string id, version;
    std::size_t files = 0, overlay_files = 0, patches = 0, textures = 0;
    std::vector<std::string> errors, warnings, notes;
    // with a disc: files in the overlay of which half or more is the disc's file at that path (a warning each; `pack`
    // refuses them without --force)
    std::vector<std::string> mostly_disc;
};
struct VerifyOptions {
    // b3modtool's own exports inside the mod (a `txd export` / `audio export` folder, a whole `strings` / `vdb` text)
    // are the game's data: an error, unless this is set (then a note)
    bool allow_exports = false;
};
// A mod folder or a .b3mod archive. `game` (a disc folder or image, may be empty): also checks that no file is a copy
// of a disc file and how much of each replaced file is the disc's, and that each b3patch was made from the disc's file.
// false: the mod could not be read at all (errors says why); otherwise rep.errors empty = the mod passes.
bool verify_mod(const std::string& mod, const std::string& game, VerifyReport& rep, const VerifyOptions& opt = {});
// `pack`'s rule on top of verify's: no errors, and no file in the overlay that is mostly the disc's (a whole game file
// with a small change: ship a patch) unless `force`. false: `why` says what to do.
bool pack_allowed(const VerifyReport& rep, bool force, std::string* why = nullptr);
// The folder `dir` as a .b3mod (a ZIP: mod.ini at the top, every file but the file managers' own, names sorted).
bool pack_mod(const std::string& dir, const std::string& out, std::size_t* entries = nullptr, std::string* why = nullptr);

// ---- VDB.XML, the tuning defaults (docs/modding/file-formats/vdb.md; src/b3modtool/vdb.cpp) ----
// CGtHash::CalculateHash 0x0021BFA0: CRC-32's table, start -1, an arithmetic shift right, no final inversion. Plain
// ASCII only (retail reads outside its table for a byte >= 0x80): gt_hash_ok says whether `s` is.
bool gt_hash_ok(std::string_view s);
std::int32_t gt_hash(std::string_view s);
// The key of a registration: CGtHash(name + group + ("/" unless group ends in "/") + file)
std::int32_t vdb_key(std::string_view name, std::string_view group, std::string_view file);
constexpr std::size_t kVdbMaxSize = 0x12000;   // 73,728: the game's buffer for the file (vdb.md "Size limit")

struct VdbValue {
    enum Kind : std::uint8_t { Word, Vector, Array } kind = Word;
    std::uint32_t word = 0;                // Word: the raw 32 bits (a float or an int)
    std::vector<std::uint32_t> block;      // Vector: 4 words (x, y, z, w); Array: n words
};
struct Vdb {
    std::uint32_t version = 2;             // header +0x00 (the game skips it)
    std::string pad = "\xFD\xFD\xFD\xFD";  // the bytes that align the first block to 16 (the disc's: 0xFD)
    bool vector_word_set = false;          // header +0x08 is not the number of vectors: keep `vector_word`
    std::uint32_t vector_word = 0;
    std::map<std::int32_t, VdbValue> values;        // by key (std::map: signed order, the file's)
    std::map<std::int32_t, std::uint32_t> files;   // file records: key -> enabled
};
// The layout of vdb.md: records sorted by signed key; the blocks of the vectors (16 bytes, 16-aligned, in key order;
// their count is header +0x08), then the arrays (n x 4 bytes, packed, in key order); the file records.
bool read_vdb(const Buf& file, Vdb& out, std::string* why = nullptr);
// The file rebuilt by that layout; refuses one over kVdbMaxSize.
bool write_vdb(const Vdb& v, Buf& out, std::string* why = nullptr);

// The names: the tuning registry's (name, group, file, type, count) per key, built in (src/b3modtool/vdb_names.inc,
// generated by tools/modding/tuning_registry.py --names-table --cxx), plus any tables added from text in that format.
struct VdbName {
    std::string name, group, file;
    char type = 'f';                       // 'f' float, 'i' int, 'v' vector
    std::uint32_t count = 0;               // >= 2: an array of that many
};
struct VdbNames {
    std::map<std::int32_t, VdbName> values;
    std::map<std::int32_t, std::string> files;
    std::size_t rows = 0;
};
bool vdb_names_add(VdbNames& n, std::string_view table, std::string* why = nullptr);   // the text table format
VdbNames vdb_builtin_names();
// The text form (docs/modding/b3modtool.md "vdb"). `source` names the file in the first comment line.
struct VdbTextStats { std::size_t named = 0, unnamed = 0, files_named = 0, odd = 0; };
std::string vdb_to_text(const Vdb& v, const VdbNames& names, const std::string& source, VdbTextStats* st = nullptr);
// `text` over `v` (empty for a whole new file, or the original's records: lines replace and add). `changed`: the
// records whose value the text set to something new.
bool vdb_from_text(std::string_view text, const VdbNames& names, Vdb& v, std::string* why = nullptr,
                   std::size_t* changed = nullptr);

// ---- audio: PS-ADPCM, WAV, RWS streams and AWD banks (docs/modding/file-formats/audio.md; src/b3modtool/audio.cpp) ----
struct AdpcmState { std::int32_t h1 = 0, h2 = 0; };
// One 16-byte frame -> 28 samples, as the SPU2 decodes it (src/audio/spu2_mixer_impl.cpp): filters 5-15 as 0,
// shifts 13-15 as 9.
void adpcm_decode_frame(const std::uint8_t* frame, AdpcmState& st, std::int16_t out[28]);
std::vector<std::int16_t> adpcm_decode(const std::uint8_t* p, std::size_t n);
// 28 samples -> one frame (the filter and shift with the least error, quantised against the decoder's own output);
// `st` becomes the decoder's state after it.
void adpcm_encode_frame(const std::int16_t in[28], std::uint8_t flags, AdpcmState& st, std::uint8_t out[16]);
// `n` samples (padded with silence to whole frames) -> frames. `orig` (may be null): the frames it replaces; each
// original frame that decodes to exactly the wanted samples from the running state is kept as it is (so audio that
// was not edited gives the original bytes). Flags: frame i of the result keeps original frame i's flag byte, the last
// frame takes the original's last flag, new frames in between the original's flag without loop start; with no
// original, `def_flags` everywhere.
Buf adpcm_encode(const std::int16_t* pcm, std::size_t n, const std::uint8_t* orig, std::size_t orig_n,
                 std::uint8_t def_flags = 0x02, std::size_t* reused = nullptr);

struct Pcm {
    int rate = 0, channels = 0;
    std::vector<std::int16_t> s;           // interleaved
    std::size_t frames() const { return channels ? s.size() / std::size_t(channels) : 0; }
};
Buf write_wav(const Pcm& p);               // 16-bit PCM
// PCM 8 / 16 / 24 / 32-bit and 32-bit float WAV (also WAVE_FORMAT_EXTENSIBLE) -> 16-bit
bool read_wav(const Buf& wav, Pcm& out, std::string* why = nullptr);

struct AudioItem {
    std::size_t index = 0, sub = 0;        // segment (or wave) number; sub-stream
    std::string name;                      // the segment's or wave's name
    int rate = 0, channels = 0;
    std::size_t bytes = 0;                 // PS-ADPCM bytes, all channels (without a stream's padding)
    std::uint32_t flags = 0;               // a wave's +0x54 flags
    bool loops = false;                    // a wave whose first frame sets the loop start (it loops from its start)
};
struct AudioFile {
    enum Kind { Rws, Awd } kind = Rws;
    std::string name;                      // the stream's or bank's name
    std::vector<AudioItem> items;
    // RWS: the stream's layout
    std::size_t desc = 0, data_at = 0, block = 0, nseg = 0, nsub = 0;
    struct Seg { std::size_t at = 0, sizes_at = 0; std::uint32_t size = 0, offset = 0; };
    struct Sub { std::size_t at = 0, fmt_at = 0; std::uint32_t slice = 0, interleave = 0, bytes_per_block = 0; };
    std::vector<Seg> segs;
    std::vector<Sub> subs;
    // AWD: the bank's layout
    std::size_t header_size = 0;
    std::vector<std::size_t> wave_at;      // each wave's RwaWave in the file
};
bool is_audio_bank(const std::uint8_t* p, std::size_t n);   // an .AWD / .HWD / .LWD header
bool audio_read(const Buf& file, AudioFile& out, std::string* why = nullptr);   // an RWS stream or a bank
// One item's PS-ADPCM, per channel (a stream's channel runs gathered from its blocks, without the padding)
std::vector<Buf> audio_channels(const Buf& file, const AudioFile& af, const AudioItem& it);
Pcm audio_decode(const Buf& file, const AudioFile& af, const AudioItem& it);
std::string audio_item_stem(const AudioFile& af, const AudioItem& it);   // "003_name" (+ ".sub1" with sub-streams)
struct AudioImportReport {
    std::size_t items = 0, replaced = 0, unchanged = 0, reencoded = 0, length_changed = 0;
    std::size_t frames_reused = 0, frames_encoded = 0;
    std::vector<std::string> notes;
};
// `orig` with the items `get` returns new audio for (true and the PCM; false: keep). A WAV at another rate is
// resampled (linear) to the item's rate; mono <-> stereo is mixed / copied. The file keeps its structure; only the
// lengths of the changed items and what follows them move (docs/modding/b3modtool.md "audio").
using AudioSource = std::function<bool(const AudioItem& it, Pcm& pcm)>;
bool audio_import(const Buf& orig, const AudioSource& get, Buf& out, AudioImportReport* rep = nullptr,
                  std::string* why = nullptr);

// ---- helpers ----
bool read_host_file(const std::string& path, Buf& out);    // host files and paths in a disc image (b3asset::disc)
bool write_host_file(const std::string& path, const Buf& data, std::string* why = nullptr);   // parent folders made
std::string to_upper_ascii(std::string s);

}  // namespace b3modtool

#endif
