// platform_disc.h -- the game's disc as the platform reads it (S4 of the SIF lane, 2026-11-11; src/native/platform_disc.cpp).
//
// The disc is a folder of the disc's files or the disc image itself (b3asset/discfs.h: an .iso, raw .bin or .chd read in
// place), named by $B3_DISC_ROOT (native_play sets it from --game), else ./data/game. Two readers use it:
//   - the game's file system (platform_gtfs.cpp: CGtFileSystemPS2DVD / CGtFilePS2DVD answered at their own functions),
//     through File;
//   - RW Audio's IOP side (platform_rwa.cpp: RWA.IRX's remote file reads), through the handles below.
#ifndef B3NATIVE_PLATFORM_DISC_H
#define B3NATIVE_PLATFORM_DISC_H

#include "b3asset/discfs.h"
#include "b3mod/mods.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace b3native::platform::disc {

// The disc's root: a folder or a disc image. Fatal (naming it) when it is neither, at the first lookup.
const std::string& root();

// A path as the game names a file on the disc -> the host path (a file under root()), or "" when there is no such file.
// The game's paths are ISO 9660 paths on the PS2's CD/DVD device: "cdrom0:\DATA\GLOBALUS.BIN;1" (the device and the
// file version are optional, '/' works as a separator too). Each name is looked up ASCII-case-insensitively (the disc's
// names are upper case; an extracted folder may use any case); "." / ".." and other devices are not on the disc.
std::string resolve(const std::string& guest_path);

// The mod kit's data overlay (include/b3mod/mods.h, docs/modding/DESIGN.md §1): while set, resolve() first asks it, so
// the first active mod that has a path supplies that file (its size and bytes) to both readers below; every other
// path is the disc's. Set once before the game's first file read, never changed during a run; null (the default):
// no overlay, the disc exactly as before. The first time a mod supplies a path it is printed ("mods: <path> from
// <id>").
void set_overlay(const b3mod::Overlay* overlay);
// 1.0.1 (b3asset/language.h): the game's DATA/GLOBALUS.BIN and DATA/HEADUS.BIN from `disc` (the player's European disc,
// a folder or an image) as DATA/GLOBAL<code>.BIN / HEAD<code>.BIN. An empty code or disc: the game's own text. A mod's
// file for those paths still wins. Returns false (and leaves the game's text) when the disc has no such pair.
bool set_language(const std::string& disc, const std::string& code);
// 1.1: the code of the language whose GLOBAL table the game reads (set_language, and no mod's GLOBALUS.BIN over it);
// empty: the disc's own (or a mod's) text
std::string language_text();
// 1.3.0 (b3asset/language.h dj_*): the DJ's voice in `code`'s language (FR / GE / IT / SP), from `dj_root` (that
// language's imported banks, the disc's layout: <preferences>/languages/DJ/<code>). While set, resolve() gives each
// English DJ bank (TRACKS/E_DJ*.RWS, TRACKS/*/*/E_DJRACE.RWS) as the language's (<L>_...) when dj_root has it and its
// header matches the English bank's (b3asset::language::dj_matches); else the English bank, said once per bank. A
// mod's file for that path still wins. Empty code / root, or a language without a DJ of its own: the English DJ (the
// default). False when nothing would be redirected (no such language, or no banks in dj_root).
bool set_dj_voice(const std::string& dj_root, const std::string& code);
std::string dj_voice();   // the DJ's language set ("GE"), "" for the English DJ
// 1.3.0: the imported languages' folder (<preferences>/languages). The race training's localized pictures
// (OVID/TRC_<FRA|GER|ITA|SPA>.M2V, TRD_...: the game names them only when tools/native/training_voice.h picks a
// language's training movie) are its VIDEO/<code>/OVID files when imported, else the US disc's English pictures
// (OVID/TRC_ENG.M2V / TRD_ENG.M2V: the same length; the narration is in MOVIE.RWS). Said once per file.
void set_languages_root(const std::string& root);
// Replays (1.3.0): the run's DJ banks, decided at once ("<count>:<FNV-1a 64 of the redirected English banks' keys>";
// empty: the English DJ), and whether the imported training pictures are used (a run recorded with the English ones
// plays them: use_training_pictures(false)); training_pictures(code): that language's pictures are imported and read.
std::string dj_banks(const std::string& disc);   // `disc`: the game folder or image (UTF-8)
void use_training_pictures(bool imported);
bool training_pictures(const std::string& code);
// B3_DJ_LOG=1 (checks): each DJ bank opened (English or a language's) and OVID/MOVIE.RWS (the training narration) is traced: every line (segment) the stream
// source seeks to, with the VBlank, and how many of its bytes were read before the next seek or the close
// ("dj: VBlank <v>: <bank> line <k> (<n> bytes) starts", "dj: <bank> line <k>: <r> of <n> bytes read").

// One disc file open for reads at any offset.
struct File {
    b3asset::disc::File file;
    std::string host_path;
    std::uint64_t size = 0;

    // false: not on the disc (or larger than 4 GB: no PS2 file is)
    bool open(const std::string& guest_path);
    bool open_host(const std::string& host_path);   // a host path already resolved
    void close() { *this = File{}; }
    bool is_open() const { return file.is_open(); }
    // `n` bytes at `offset`, all inside the file. A short read is retried (a host file system under load returns them:
    // the 9P mount of /mnt/c); fatal, with what came back, when the bytes never come.
    void read(std::uint64_t offset, void* dst, std::size_t n) const;
};

// RW Audio's IOP file reads: handles >= 0 (-1: not on the disc); seek whence 0 start / 1 current / 2 end, the new
// position or -1; read from the position, the bytes read (0 at the end) or -1 for a bad handle.
int open(const std::string& guest_path);
std::int64_t size(int handle);
std::int64_t seek(int handle, std::int64_t offset, int whence);
std::int64_t read(int handle, void* dst, std::uint32_t bytes);
void close(int handle);

}  // namespace b3native::platform::disc

#endif
