// save_tool -- the port's save files from the command line (save1, remaster-features §8; src/native/save_store.h):
//
//   save_tool info <file or folder>                 what a .sav / .psu / .max / .ps2 card / folder card holds
//   save_tool import <from> [--saves DIR] [--slot N] the Burnout 3 save in <from> becomes profile N's .sav (old: .bak)
//   save_tool export <to.sav> [--saves DIR] [--slot N] profile N (its .sav, or an older profile-N folder) as a .sav
//   save_tool stats [<file or folder>]              the game's own stats in a save and the local achievements from
//                                                   them (save_stats.h; read only); no file: profile N's
//   save_tool where                                 the user's saves folder
//
// --saves defaults to $B3NATIVE_SAVES_ROOT, else the folder chosen in the launcher (chosen_saves_root), else the user's
// folder (default_saves_root); --slot to active-slot.txt's.
#include "save_stats.h"
#include "save_store.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace b3native::save;
namespace fs = std::filesystem;

namespace {

int usage() {
    std::fprintf(stderr, "usage: save_tool info <file|folder> | import <from> [--saves DIR] [--slot N] | "
                         "export <to.sav> [--saves DIR] [--slot N] | stats [<file|folder>] [--saves DIR] [--slot N] | "
                         "music <file|folder> | where\n");
    return 2;
}

void print(const Directories& dirs) {
    for (const auto& d : dirs) {
        std::printf("%s  (%04u-%02u-%02u %02u:%02u:%02u, %zu files)\n", d.name.c_str(), d.modified.year, d.modified.month,
                    d.modified.day, d.modified.hour, d.modified.minute, d.modified.second, d.files.size());
        for (const auto& f : d.files)
            std::printf("  %-32s %8zu bytes  crc32 %08x\n", f.name.c_str(), f.content.size(), crc32(f.content.data(), f.content.size()));
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    fs::path saves;
    if (const char* e = std::getenv("B3NATIVE_SAVES_ROOT"); e && *e) saves = e;
    else if (const fs::path c = chosen_saves_root(user_data_dir()); !c.empty()) saves = c;   // 1.1: the launcher's choice
    else saves = default_saves_root();
    int slot = 0;
    std::string arg;
    for (int i = 2; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--saves" && i + 1 < argc) saves = argv[++i];
        else if (s == "--slot" && i + 1 < argc) slot = std::atoi(argv[++i]);
        else if (arg.empty()) arg = s;
        else return usage();
    }
    if (slot == 0) slot = read_active_slot(saves);
    std::string err;
    if (cmd == "where") {
        std::printf("%s\n", saves.string().c_str());
        return 0;
    }
    if (cmd == "info" && !arg.empty()) {
        auto d = read_any(arg, &err);
        if (!d) { std::fprintf(stderr, "save_tool: %s\n", err.c_str()); return 1; }
        print(*d);
        std::printf("%s\n", game_save(*d) ? "a Burnout 3 save" : "no Burnout 3 save");
        return 0;
    }
    if (cmd == "music" && !arg.empty()) {   // eatrax1: the save's song fields (each must be a disc song: 0..43, or 0xFF)
        auto d = read_any(arg, &err);
        auto game = d ? game_save(*d, &err) : std::nullopt;
        if (!game) { std::fprintf(stderr, "save_tool: %s\n", err.c_str()); return 1; }
        for (const CardSaveDirectory& dir : *game)
            for (const CardFile& f : dir.files) {
                if (f.name != kGameSaveDirectory || f.content.size() != kGameSaveBytes) continue;
                const std::uint8_t* b = f.content.data();
                std::printf("last song %u\nsong bits", unsigned(b[kLastSongOffset]));
                for (std::size_t k = 0; k < kSongBits; ++k) std::printf(" %u", unsigned(b[kOptionsOffset + k]));
                std::printf("\n");
                return 0;
            }
        std::fprintf(stderr, "save_tool: no game save in %s\n", arg.c_str());
        return 1;
    }
    if (cmd == "stats") {
        std::optional<std::vector<ProfileStats>> st;
        if (!arg.empty()) {
            st = read_profile_stats(arg, &err);
        } else {
            auto p = load_profile(saves, slot, &err);
            if (p && p->source == ProfileSource::kNone) { std::fprintf(stderr, "save_tool: profile %d is empty\n", slot); return 1; }
            if (p) st = decode_game_save(p->dirs, &err);
        }
        if (!st) { std::fprintf(stderr, "save_tool: %s\n", err.c_str()); return 1; }
        std::fputs(format_stats(*st).c_str(), stdout);
        return 0;
    }
    if (cmd == "import" && !arg.empty()) {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (!import_profile(saves, slot, arg, now, &err)) { std::fprintf(stderr, "save_tool: %s\n", err.c_str()); return 1; }
        std::printf("imported %s as %s\n", arg.c_str(), profile_file(saves, slot).string().c_str());
        return 0;
    }
    if (cmd == "export" && !arg.empty()) {
        auto p = load_profile(saves, slot, &err);
        if (!p) { std::fprintf(stderr, "save_tool: %s\n", err.c_str()); return 1; }
        auto game = game_save(p->dirs, &err);
        if (!game) { std::fprintf(stderr, "save_tool: profile %d: %s\n", slot, err.c_str()); return 1; }
        if (!write_sav_file(arg, *game, &err)) { std::fprintf(stderr, "save_tool: %s\n", err.c_str()); return 1; }
        std::printf("exported profile %d to %s\n", slot, arg.c_str());
        return 0;
    }
    return usage();
}
