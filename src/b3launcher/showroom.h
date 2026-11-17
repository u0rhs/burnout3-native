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

// One player car file: PVEH/<folder>/CARn.BGV
struct CarEntry {
    std::string folder;            // COMP, MSCL, CUPE, SPRT, SUPR, HSPC, HEVY
    int number = 0;                // n of CARn.BGV
    std::filesystem::path file;    // in the game folder (or the disc image)
    std::string kind;              // "Compact", ..., "Heavyweight"
    std::string name;              // the game's name, else "<kind> <n>"
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
std::vector<CarEntry> list_cars(const std::filesystem::path& game_dir, std::string* note = nullptr);

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
