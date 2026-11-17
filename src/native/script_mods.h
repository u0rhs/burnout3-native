// script_mods.h -- script mods: a Lua 5.4 script per mod with [script] in its mod.ini (mod kit Phase 6, lua6;
// docs/modding/DESIGN.md §10, the reference docs/modding/lua-reference.md).
//
// A script mod is a code mod whose entry point is in native_play: script_candidate() gives the code-mod loader
// (code_mods.h) a CodeMods::Candidate with a Builtin instead of a library, so a script mod loads at the same point, in
// the same load order, all or nothing, with the same API (sdk/include/b3mod_api.h) under it: its hooks are wraps /
// replaces on the hook table as "mod:<id>", its settings rows, overlay items, log lines and file reads are the code
// mods' own. The script never sees that API; it sees the `b3` table (lua-reference.md).
//
// One lua_State per mod, made with a memory cap (its own allocator) and only the libraries a script may have (base
// without dofile / loadfile, coroutine, math, string without dump, table, utf8; load() takes text only; require()
// reads only the script's folder). Every call into the state runs under an instruction budget per VBlank (a count
// hook): a script that goes past it, past its memory cap or into an error is stopped -- its callbacks are no longer
// called, its hooks pass every call to the game's function -- and the game goes on without it.
//
// Determinism: the state runs on the game thread only (the main chunk inside the loader, then the VBlank, frame and
// shutdown callbacks, and its hooks inside the game's calls); string hashing has a fixed seed (script_lua.cpp);
// math.random is seeded from the mod's id; there is no clock but b3.vblank(). Hot reload (a changed script loaded
// again at a VBlank) happens only with Developer mods on.
#ifndef B3NATIVE_SCRIPT_MODS_H
#define B3NATIVE_SCRIPT_MODS_H

#include "code_mods.h"

#include "b3mod/mods.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace b3native {

struct ScriptConfig {
    std::string id, version;
    std::filesystem::path dir;     // the mod's folder
    std::string main_file;         // [script] main, inside the mod ("scripts/main.lua")
    bool gameplay = true;          // [script] gameplay: false refuses every write to the game
    std::uint64_t budget = b3mod::ScriptSpec::kDefaultBudget;            // Lua instructions per VBlank
    std::uint64_t memory = std::uint64_t(b3mod::ScriptSpec::kDefaultMemoryMb) << 20;   // bytes
    bool developer = false;        // Developer mods: hot reload
    unsigned reload_every = 30;    // VBlanks between two looks at the script folder (hot reload)
};
// The main chunk and its on_load handlers run with this many times the per-VBlank budget.
constexpr std::uint64_t kScriptLoadBudgetFactor = 50;

std::shared_ptr<CodeMods::Builtin> make_script_mod(const ScriptConfig& c);
// The loader's candidate for an active mod with [script] (developer: the user's Developer mods)
CodeMods::Candidate script_candidate(const b3mod::ActiveMod& m, bool developer);

// The guest addresses behind the script API's read-only views and game events (lua-reference.md cites each).
namespace script_addr {
constexpr std::uint32_t kWorld = 0x00522660u;                 // gWorld [C src/b3replay/capture.cpp kWorld]
constexpr std::uint32_t kTimer = kWorld + 0x20;               // the game timer, seconds [C capture.h Tick::now]
constexpr std::uint32_t kRaceCarCount = kWorld + 0x12AD1C;    // race cars [C capture.cpp decode_tick]
constexpr std::uint32_t kRaceCarList = kWorld + 0x12AD28;     // their pointers, 6 [C capture.cpp decode_tick]
constexpr std::uint32_t kPlayerCar = kWorld + 0x12AD50;       // view 0's race car [C capture.h kPlayerRaceCar]
constexpr std::uint32_t kPlayerScore = kPlayerCar + 0x10C0;   // its CB3Score [C capture.h ScoreState]
constexpr std::uint32_t kGamePtr = 0x0051BA88u;               // the game; +0x1B4 its stage logic [C capture.cpp decode_lap]
constexpr std::uint32_t kSplitGame = 0x0051AEC0u;             // *kGamePtr in two-player split screen [C capture.h Tick::split]
constexpr std::uint32_t kTicksPerFrame = 0x0051BAC4u;         // [C capture.cpp decode_tick]
constexpr std::uint32_t kTimeAttackLogic = 0x004DEBC0u;       // the Time Attack logic's vtable [C capture.h]
// player 1's speedometer source: the W lane of a vector at D_0055EF70 + 0x1CB80 + 0xB0, shown as an integer times
// D_004E0798 (D_004E2918 <= 1) or D_004E0960 (D_004E2918 > 1) [L decomp src/nonmatching/func_001A3B30.cpp, DNAT
// CB3HUDSpeedo::Update; func_001A3C70 Prepare sets the pointer for player 0]
constexpr std::uint32_t kSpeedVector = 0x0055EF70u + 0x1CB80u + 0xB0u;
constexpr std::uint32_t kSpeedUnits = 0x004E2918u, kSpeedFactorLow = 0x004E0798u, kSpeedFactorHigh = 0x004E0960u;
// the wrapped game functions (decomp config/symbol_addrs_b3names.txt, untagged: [C])
constexpr std::uint32_t kSetFinished = 0x00151780u;     // SetFinished__10CB3RaceCarF19EB3ScoreFinishState
constexpr std::uint32_t kStartCrashing = 0x001519F0u;   // StartCrashing__10CB3RaceCarFP17CB3CrashStartInfo
constexpr std::uint32_t kOnTakeDown = 0x001553F0u;      // OnTakeDown__8CB3ScoreFP10CB3RaceCar
}  // namespace script_addr

}  // namespace b3native

#endif
