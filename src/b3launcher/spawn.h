// spawn.h -- the game started from the launcher (Windows: CreateProcessW; Linux / macOS: posix_spawn), its output to a
// log file, and a look at whether it ended at once (a missing file, a bad argument).
#ifndef B3LAUNCHER_SPAWN_H
#define B3LAUNCHER_SPAWN_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace b3launcher {

struct Child {
    std::intptr_t handle = 0;   // Windows: the process HANDLE; else the pid
    bool valid() const { return handle != 0; }
};

// Starts `program` with `args` in `cwd`, stdout and stderr to `log` (created, truncated; empty: inherited). The child
// outlives the launcher. nullopt (and *error) when it cannot be started.
std::optional<Child> spawn(const std::filesystem::path& program, const std::vector<std::string>& args,
                           const std::filesystem::path& cwd, const std::filesystem::path& log, std::string* error = nullptr);
// The exit code once it ended, else nullopt (still running). Does not wait.
std::optional<int> poll_exit(Child& c);
// Lets go of the child (it keeps running).
void release(Child& c);
// launchskip1: a Shift key held now, as the system sees it without a window of ours focused (Windows:
// GetAsyncKeyState; macOS: [NSEvent modifierFlags], looked up at run time). false where the system has no such
// query (Linux: the launcher then reads SDL's keyboard state once its window has the focus).
bool shift_key_held();

}  // namespace b3launcher

#endif
