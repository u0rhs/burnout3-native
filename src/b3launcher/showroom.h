// showroom.h -- the launcher's SHOWROOM page (photo2, 2026-11-12; remaster-features §5 "Showroom / garage"): the player
// cars of the user's own disc in 3D on a turntable, with the game's names for them. These are the pure parts (the car
// list, the names from the game's string table, the turntable); the drawing (b3gfx offscreen with the renderer's car
// decoders, src/b3view/carscene.h) is showroom_view.h. Read only: nothing is written; the game and the save files never
// see it, so it unlocks nothing and changes nothing (every car on the disc shows, locked in the save or not).
#ifndef B3LAUNCHER_SHOWROOM_H
#define B3LAUNCHER_SHOWROOM_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace b3launcher::showroom {

// showroom2: the series, as SELECT CAR groups the cars (EB3MenuCarClasses): 0..4 the five classes, 5 the specials, 6 the
// heavyweights, 7 TRAFFIC, 8 TRAFFIC SPECIAL (trafficseries), 10 CUSTOM, 11 BETA (customseries: the player's cars)
enum : int { kSeriesSpecial = 5, kSeriesHeavy = 6, kSeriesTraffic = 7, kSeriesTrafficSpecial = 8, kSeriesCustom = 10, kSeriesBeta = 11 };

// One player car file: PVEH/<folder>/CARn.BGV
struct CarEntry {
    std::string folder;            // COMP, MSCL, CUPE, SPRT, SUPR, HSPC, HEVY
    int number = 0;                // n of CARn.BGV
    std::filesystem::path file;    // in the game folder (or the disc image)
    // "Compact", ..., "Heavyweight" (list_cars); list_more_cars' series exactly "Traffic", "Traffic special", "Custom" and
    // "Beta": the showroom's series selector (showroom2) keys on these four strings
    std::string kind;
    std::string name;              // the game's name, else "<kind> <n>"
    std::string label;             // where it is from, as the page shows it ("PVEH/COMP/CAR3.BGV", "cars/custom/COMP/CAR7.BGV")
    int page = 0;                  // showpages: a CUSTOM / BETA car's page (0-based: 19 cars each, as SELECT CAR's; extra_series.h)
};

// The game's string table (DATA/GLOBALUS.BIN): u32 count at +0x08, the offset table's position at +0x0C, then one u32
// file offset per string, each a UTF-16LE string ending in 0 [C the US disc's file: 4006 strings, table at 0x10, the
// first string right after it]. As UTF-8; empty when the file does not look like one.
std::vector<std::string> read_strings(const std::vector<std::uint8_t>& bin);

// The 67 player car names in the game's order (Compact x 10, Muscle, Coupe, Sports, Super, the 7 specials, the 10
// heavyweights): the block of the table that starts with "Compact Type 1" (title case, as the menus show the cars; the
// upper-case block before it otherwise), trailing spaces trimmed. Empty when there is no such block.
std::vector<std::string> car_names(const std::vector<std::string>& strings);

// The index in car_names of PVEH/<folder>/CAR<number>.BGV, -1 for a file the table does not name. Classes: CARn = the
// class's n-th; the specials are HSPC CAR20-25 and the fire truck HEVY CAR36; the heavyweights HEVY CAR1-10 [C the
// files on the US disc, the order checked against the drawn cars: the showroom proof's pictures].
int name_index(const std::string& folder, int number);

// The player cars of `game_dir` (a folder or a disc image: b3asset/discfs.h) in the game's order, named from its string
// table. `note`: what was missing (no PVEH folder, no string table), empty when all is there.
std::vector<CarEntry> list_cars(const std::filesystem::path& game_dir, std::string* note = nullptr,
                                std::vector<std::string>* strings = nullptr);   // (the string table, for series_name)

// showroom2: a series' name as SELECT CAR shows it: the game's string (GetClassString 0x42DA70: 0xBEF.. for 0..6, the
// specials 0xBF5, the heavyweights 0xBF4; trafficseries' 380 / 381), "CUSTOM" / "BETA" for the player's; a fallback
// when the table has none
// showpages: CUSTOM's and BETA's pages are series of their own, as in the game (carpacks: the series codes 10, 12, 14,
// ... CUSTOM's, 11, 13, ... BETA's: tools/native/extra_series.h extra_page_series), named as SELECT CAR names them:
// extra_words(`language`) (the launcher's language code: "" English, FR, GE, IT, SP, DU, SW), " 2", " 3", ... from the
// second page
std::string series_name(int series, const std::vector<std::string>& strings, const std::string& language = {});
// A car's series: the extra cars' by their kind ("Traffic", "Traffic special", "Custom", "Beta": customseries'
// list_more_cars; CUSTOM's and BETA's by their page too), the disc's by their folder (the specials and the fire truck:
// name_index 50-56)
int car_series(const CarEntry& c);
// A series and its cars (indices into a car list), in SELECT CAR's order: the series with cars only
struct SeriesEntry {
    int id = 0;
    std::string name;
    std::vector<std::size_t> cars;
};
std::vector<SeriesEntry> group_series(const std::vector<CarEntry>& cars, const std::vector<std::string>& strings,
                                      const std::string& language = {});   // (showpages: CUSTOM's and BETA's pages' names)

// trafficseries / customseries: the cars SELECT CAR offers beyond the disc's player cars, in its order after them: the
// TRAFFIC and TRAFFIC SPECIAL series (the traffic vehicles' .BTV files, named by the program's traffic table:
// tools/native/traffic_series.h), then the CUSTOM and BETA series (the player's car folders `custom` and `beta`, read
// as the game reads them: tools/native/extra_series.h extra_scan_all). `left_out`: each extra car file not shown, with
// why (as the game's log has it).
std::vector<CarEntry> list_more_cars(const std::filesystem::path& game_dir, const std::filesystem::path& custom,
                                     const std::filesystem::path& beta, std::vector<std::string>* left_out = nullptr);
// The traffic vehicles' names in the traffic table's order (traffic_series.h traffic_table): the program's string ids
// D_004CDD40[40] [C func_003EACC0 kind 1] in the game's string table; empty when the program or the table is not there
std::vector<std::string> traffic_names(const std::filesystem::path& game_dir);

// The turntable: the car turning by itself, turned with the mouse (drag) or the right stick, the camera's height and
// distance with them (vertical drag / stick, the wheel / triggers). A drag or stick stops the spin for a few seconds.
struct Turntable {
    static constexpr float kSpinRate = 0.35f;       // rad/s
    static constexpr float kMinPitch = 0.0f, kMaxPitch = 1.2f, kMinZoom = 0.55f, kMaxZoom = 2.5f;
    static constexpr float kHoldSeconds = 3.0f;     // after a drag before spinning again
    float yaw = 0.75f;      // the car's rotation about up (rad)
    float pitch = 0.22f;    // the camera's elevation (rad)
    float zoom = 1.0f;      // the camera's distance, x the framing distance
    bool spin = true;       // turning by itself
    float hold = 0;         // seconds before it spins again
    void drag(float dx, float dy);           // a mouse drag, in window points
    void stick(float x, float y, float dt);  // the right stick (-1..1, y down positive)
    void wheel(float steps);                 // the mouse wheel / the triggers: up = nearer
    void step(float dt);                     // the spin
    void reset() { *this = Turntable{}; }
};

}  // namespace b3launcher::showroom

#endif
