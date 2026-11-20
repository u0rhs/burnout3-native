// records.h -- media1: where a run's Time Attack ghosts (ghost.h) and speedrun best times (speedrun.h) are read and
// written. Two kinds of run do not make the player's own records:
//   - a replay playing (native_play --replay passes --replay-playback): its laps and finishes are the recording's (a
//     friend's, perhaps), so nothing is written; the player's ghost is still read and shown;
//   - a run whose mod set changes gameplay (b3mod::changes_gameplay_any: a tuning preset, a code mod marked
//     gameplay = yes, a script mod that may write the game): its ghosts and times belong to that set, kept in a folder
//     of its own, "<ghosts or speedrun folder>/modded-<hash>" (docs/PLAYING.md §8; the hash: b3mod::gameplay_set_hash,
//     the gameplay-changing mods alone, so a texture mod added or moved keeps the folder). The plain game reads only
//     its folder's own files, so it never sees them; another gameplay mod or version starts afresh.
#ifndef B3REPLAY_RECORDS_H
#define B3REPLAY_RECORDS_H

#include <filesystem>
#include <string>

namespace b3replay::records {

struct Run {
    bool playback = false;      // a replay playing
    std::string gameplay_set;   // b3mod::gameplay_set_hash (16 hex); "": no mod changes gameplay
};

// The folder the run reads from (and writes to, when it writes): `base` itself, or its set's folder. "" stays "".
inline std::string folder(const std::string& base, const Run& r) {
    if (base.empty() || r.gameplay_set.empty()) return base;
    return (std::filesystem::path(base) / ("modded-" + r.gameplay_set)).string();
}
// Whether the run writes its records at all (a folder given, and not a replay playing)
inline bool writes(const std::string& base, const Run& r) { return !base.empty() && !r.playback; }

}  // namespace b3replay::records

#endif
