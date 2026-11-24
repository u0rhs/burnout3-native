// betaimport.h -- the BETA SERIES import: cars that were cut from the game, taken from the player's own disc image of a
// June 2004 prototype or demo and made into car files the game takes (b3asset/carconvert.h), with a manifest that the
// game's extra-cars code reads (the BETA series on SELECT CAR).
//
// The discs are known by their VEHICLES/VLIST.BIN (FNV-1a 64 of its bytes): the "Pre-Alpha" PS2 prototype of June 8 2004
// and the PS2 demo of June 21 2004, as an .iso, a raw .bin (MODE2/2352) or a CHD (b3asset/discfs.h reads all three).
// The cars offered are the ones the game no longer has (their models differ from every car of the game, by the
// prototype research), each known by its path and the FNV-1a 64 of its bytes: a file that differs (a damaged image) is
// not converted. Every car takes a game car as its donor: its engine-sound structures (put into the converted file),
// and for the extra-cars code its engine banks (.HWD / .LWD), handling and Crashbreaker.
//
// The output folder (<user data>/cars/beta) holds one folder per car, as the extra-cars code reads them
// (tools/native/extra_series.h extra_scan, the BETA series: in folder-name order, so "NN-<key>" in the table's order):
//
//   cars/beta/01-jun21-hevy-car4/
//     car.bgv                       the converted file (the game's layout, the donor's engine sound in it)
//     car.ini
//       name = SUV                  its SELECT CAR name (printable ASCII, at most 24; the discs only have placeholders:
//                                   "Heavyweight Car4 (No String)")
//       donor = HEVY/CAR4           the game car whose engine banks (.HWD / .LWD), handling, pictures and Crashbreaker it uses
//       model = car.bgv
//       key = jun21-hevy-car4       (for the launcher and the logs; the extra-cars code ignores the rest)
//       source = jun21              the disc: jun08 / jun21
//       from = VEHICLES/HEVY/CAR4.BGV
//       series = 6                  the game's series it races like
//       size = 448512
//       hash = 0123456789abcdef     FNV-1a 64 of car.bgv
//
// Each folder is written whole (a hidden ".<name>.partial" folder, then renamed into place).
#ifndef B3ASSET_BETAIMPORT_H
#define B3ASSET_BETAIMPORT_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace b3asset::beta {

struct Disc {
    const char* key;        // "jun08", "jun21"
    const char* label;      // "the June 8, 2004 PS2 prototype"
    std::uint64_t vlist;    // FNV-1a 64 of VEHICLES/VLIST.BIN
};
const std::vector<Disc>& discs();

struct Car {
    const char* disc;       // Disc::key
    const char* cls;        // "HEVY"
    const char* car;        // "CAR4"
    bool traffic;           // a .BTV on the disc
    std::uint64_t hash;     // FNV-1a 64 of the disc's file
    const char* donor_cls;  // the game car it takes its engine sound and handling from
    const char* donor_car;
    int series;             // the game's series it races like
    const char* name;       // its SELECT CAR name
    const char* replaced_by;   // "" or the key of the same car from a later disc (that one is imported instead)
    std::string key() const;            // "jun21-hevy-car4"
    std::string disc_path() const;      // "VEHICLES/HEVY/CAR4.BGV"
    std::string donor_path() const;     // "PVEH/HEVY/CAR4.BGV"
    std::string donor_id() const;       // "HEVYCAR4"
    std::string folder() const;         // "01-jun21-hevy-car4": its output folder (the table's order)
};
const std::vector<Car>& cars();

std::uint64_t fnv1a64(const std::uint8_t* p, std::size_t n);

// tests only: other discs and cars instead of the known ones (nullptr: the known ones again)
void set_test_tables(const std::vector<Disc>* discs, const std::vector<Car>* cars);

// The disc an image holds (nullptr: not one of them; why says what it is instead)
const Disc* identify(const std::filesystem::path& image, std::string* why = nullptr);

struct Result {
    const Disc* disc = nullptr;          // a disc image's (import_cars)
    std::vector<std::string> imported;   // "jun21-hevy-car4: SUV (448,512 bytes)"
    std::vector<std::string> skipped;    // "<key>: <why>"
    std::string error;                   // the whole import failed (not a known disc, the folder not writable ...)
    std::vector<std::filesystem::path> needs_donor;   // import_files / import_folder: prototype files whose car is not known
                                                      // (a renamed file, or a slot the game has no car file of): ask the
                                                      // player which car (donor_choices) and import them again with it
    std::size_t other_files = 0;         // import_folder: files that are not car files (not read)
    bool walk_stopped = false;           // import_folder: the folder held more than it looks through
    bool ok() const { return error.empty(); }
};
// Converts every car of the image's disc into its folder in `out_dir` (made if missing) with donors from `game` (the
// game's disc folder or image). `progress` (optional) gets "<n>/<total> <key>" before each car.
Result import_cars(const std::filesystem::path& image, const std::filesystem::path& game, const std::filesystem::path& out_dir,
                   void (*progress)(const std::string&, void*) = nullptr, void* ctx = nullptr);

// One car to write: its folder and car.ini's fields
struct Spec {
    std::string folder, key, name, donor;   // donor "HEVY/CAR4"
    int series = 0;
    std::string source, from;               // "jun21" / "file"; the path on the disc / the file's name
    std::string what;                       // for car.ini's comment: "disc image of the June 21, 2004 PS2 demo"
    std::string replaced_by;                // extrastart: the table's later version of this car ("" none)
};
Spec spec_of(const Car& car);
std::string car_ini(const Spec& car, std::size_t size, std::uint64_t hash);

// ---- the player's own prototype car files, one by one or a folder of them (any layout: an extracted VEHICLES tree, a
// loose folder) ----
// A file whose bytes are a known car's (the table's hash) is that car (its name, donor, folder). Another prototype file
// (0x14 / 0x15, the old layout) takes as its donor the game car its path names (".../HEVY/CAR4.BGV" -> HEVY/CAR4), named
// "<class> <n> (prototype)", in the folder "50-<class>-<carN>-<hash>"; a path that does not say (a renamed file) or a
// car the game has no file of: in needs_donor, for the player to choose (FileDonor). A file of the game's own layout
// is a mod car (refused, pointing to the CUSTOM series); anything else is refused with why. Size ceilings as above.
struct FileDonor {
    std::filesystem::path file;
    std::string donor;   // "HEVY/CAR4"
};
std::string donor_from_path(const std::filesystem::path& file);   // "HEVY/CAR4", or "" when the path does not say
// The game's race cars of a class one may choose as a donor ("HEVY/CAR1", ...: its PVEH/<cls>/CARn.BGV files, by number)
std::vector<std::string> donor_choices(const std::filesystem::path& game, const std::string& cls);
Result import_files(const std::vector<std::filesystem::path>& files, const std::filesystem::path& game, const std::filesystem::path& out_dir,
                    void (*progress)(const std::string&, void*) = nullptr, void* ctx = nullptr, const std::vector<FileDonor>& donors = {});
// betabytes: one car file's bytes from elsewhere (a car pack's zip or folder: the launcher's "Import car pack..."), as
// import_files takes a file. `rel`: its path in the pack ("HEVY/CAR35.BTV"; its folder and name say which car it is, as
// a file's path does), `donor`: the player's choice for a needs_donor car ("HEVY/CAR4"; empty: the table's or the
// path's). A traffic vehicle's .BTV at a slot the TRAFFIC series offers (COMP CAR11-19, HEVY CAR11-35) takes that
// series' donor race car (trafficseries' table) and is named by its slot ("Heavy 35 (prototype)"). needs_donor holds
// u8path(rel). Never throws; a file larger than any car file is refused
Result import_bytes(const std::string& rel, const std::vector<std::uint8_t>& bytes, const std::filesystem::path& game,
                    const std::filesystem::path& out_dir, const std::string& donor = {});
// Every .BGV / .BTV below `dir` (recursively, at most a few thousand entries; the output folder skipped), as import_files
Result import_folder(const std::filesystem::path& dir, const std::filesystem::path& game, const std::filesystem::path& out_dir,
                     void (*progress)(const std::string&, void*) = nullptr, void* ctx = nullptr, const std::vector<FileDonor>& donors = {});

// The cars in out_dir (folders with a car.ini and a car.bgv), in folder-name order. `pack`: car.ini's "pack = <name>"
// (betabytes: a table car written by a car pack's import, gameopts' carpacks, in the table car's own folder; empty for
// the cars written here). Importing that car from a disc or a file later writes the same folder: the pack's copy is
// replaced (each folder is swapped in whole), never kept beside it.
struct Imported {
    std::string folder, key, name, source, from, pack;
};
std::vector<Imported> imported(const std::filesystem::path& out_dir);
// extrastart: an earlier disc's copy of a car whose later version is imported too (its removal failed: a file in use on
// Windows) removed now; what was removed / could not be ("<folder>: <why>")
std::vector<std::string> tidy_replaced(const std::filesystem::path& out_dir);

}  // namespace b3asset::beta

#endif
