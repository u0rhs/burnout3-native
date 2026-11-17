// save_formats.h -- the port's own save file (.sav) and the PS2 / PCSX2 save formats it imports (remaster-features
// §8, §5 "PS2 save import"): a PS2 memory card image read through its own filesystem (PCSX2 .ps2, with or without the
// per-page ECC area; also the native card's image after the game wrote to it), EMS / uLaunchELF .psu, Action Replay
// MAX .max (LZARI), and PCSX2 folder memory cards. Standard library only, no guest or platform state (tests:
// tests/test_save_formats.cpp, on synthetic images built there).
//
// The .sav container (one per profile, little endian, version 1):
//   +0  "B3PSAVE1" magic (8 bytes)      +8  u32 version (1)      +12 u32 payload size
//   +16 payload: u32 directory count, then per directory
//         name (u8 length + bytes), u16 mode, created (8), modified (8), u32 file count, then per file
//         name (u8 length + bytes), u16 mode, created (8), modified (8), u32 size, the bytes
//       a time is the card's 8 bytes: 0, second, minute, hour, day, month, year (u16), JST as the console keeps it
//   end u32 CRC-32 (IEEE 802.3, as zlib's crc32) of everything before it
// It holds the card's save directories as the game left them (normally one, BASLUS-21050: icon.sys, view.ico and the
// 61,772-byte save), so loading it builds the same card image the game wrote.
#ifndef B3NATIVE_SAVE_FORMATS_H
#define B3NATIVE_SAVE_FORMATS_H

#include "save_card.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace b3native::save {

// The game's save directory (CMemoryCardFileBase::mgpacDirName, "BASLUS-21050" at 0x4CEB30) [C ELF / func_003FDF90].
inline constexpr const char* kGameSaveDirectory = "BASLUS-21050";

using Bytes = std::vector<std::uint8_t>;
using Directories = std::vector<CardSaveDirectory>;

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0);

// ---- .sav ----
Bytes write_sav(const Directories& dirs);
std::optional<Directories> read_sav(const Bytes& bytes, std::string* error = nullptr);
std::optional<Directories> read_sav_file(const std::filesystem::path& p, std::string* error = nullptr);
// Written beside as "<p>.tmp", then renamed over p (a crash leaves the old file).
bool write_sav_file(const std::filesystem::path& p, const Directories& dirs, std::string* error = nullptr);

// ---- a PS2 memory card's filesystem ----
// Every save directory of the root (files only; a directory inside a save is skipped). `bytes`: raw pages (512 a page,
// the native card's and the older folder form) or pages with their 16-byte spare area (528 a page, PCSX2's .ps2), told apart by
// the size against the superblock's geometry. nullopt (and *error) for no card filesystem or a broken chain.
std::optional<Directories> read_card_image(const std::uint8_t* bytes, std::size_t size, std::string* error = nullptr);

// ---- single-save exports ----
std::optional<CardSaveDirectory> read_psu(const Bytes& bytes, std::string* error = nullptr);
Bytes write_psu(const CardSaveDirectory& dir);
std::optional<CardSaveDirectory> read_max(const Bytes& bytes, std::string* error = nullptr);
// Okumura's LZARI as AR MAX uses it (N 4096, F 60, threshold 2): `out_size` bytes from `in`; nullopt when `in` ends
// too early (more than a few zero bits past its end would be needed).
std::optional<Bytes> lzari_decode(const std::uint8_t* in, std::size_t size, std::size_t out_size);

// ---- import ----
// The save directories a file or folder holds: .sav, .psu, .max, a card image (.ps2 / .mcd / .bin, any name: by its
// superblock), a PCSX2 folder memory card (a folder with _pcsx2_superblock or save folders), or one save folder; a
// folder card's _pcsx2_superblock (a save folder's _pcsx2_index) stands for its folder.
std::optional<Directories> read_any(const std::filesystem::path& p, std::string* error = nullptr);
// The game's save (kGameSaveDirectory) among them, alone; nullopt (and *error) when there is none.
std::optional<Directories> game_save(const Directories& dirs, std::string* error = nullptr);

}  // namespace b3native::save

#endif
