// safemode.h -- safe mode of the native game's window (mac5, 2026-11-09; docs/remaster-features.md §8: when the
// settings chosen make the game unstable, it launches in a safe mode with the settings turned right down).
//
// A run state file beside the user's settings (`run_state.ini`) says whether the last window run is still marked as
// running and how many runs in a row did not end cleanly. A run that started and never reached its clean end (a crash, a
// hang the user killed, a GPU driver reset) leaves `running = 1`; the next start counts it. After two such runs in a
// row (or with --safe-mode / B3_SAFE_MODE=1, or the launcher's "start in safe mode") the game starts with safe settings
// for that run only: the user's file is not rewritten unless they change something on the SETTINGS page. A clean end
// clears the count.
//
// Safe settings touch the display and quality only (the Low preset, no MSAA, no HD pack, a window, vsync, the
// default latency), never the game: physics, AI and gameplay do not depend on any PC setting.
#ifndef B3SETTINGS_SAFEMODE_H
#define B3SETTINGS_SAFEMODE_H

#include <string>
#include <string_view>

#include "b3settings/settings.h"

namespace b3settings {

constexpr int kSafeModeAfter = 2;   // unclean runs in a row before safe mode

struct RunState {
    bool running = false;   // the last run started and has not ended cleanly (yet)
    int unclean = 0;        // runs in a row that did not end cleanly
    bool operator==(const RunState&) const = default;
};

std::string run_state_to_ini(const RunState& r);
RunState run_state_from_ini(std::string_view text);   // unknown keys ignored, bad values: the defaults
RunState load_run_state(const std::string& path);      // a missing file: the defaults
bool save_run_state(const std::string& path, const RunState& r);

// The decision at a start: the state to write while this run lasts (running, with the previous run counted when it
// never ended cleanly) and whether this run starts in safe mode.
struct StartDecision {
    RunState now;
    bool safe = false;
    bool forced = false;   // asked for (--safe-mode), not the count
};
StartDecision start_run(const RunState& previous, bool force_safe);
// The state a clean end writes
inline RunState clean_end() { return RunState{}; }

// hostfix: the guest RAM guards in safe mode. Safe mode asked for (START IN SAFE MODE: --safe-mode, B3_SAFE_MODE=1)
// starts with them off. The automatic one (kSafeModeAfter unclean runs) keeps them on, unless the previous run's log
// shows a "guest memory overrun" (the crash report's line, or that run's own guards-off line for this reason: a series
// of overrun crashes keeps them off). The reason NativeBackend logs ("guest RAM guards: off (<reason>)"); empty: on.
// (B3_GUEST_GUARDS=0 turns them off in any run: the backend reads it.)
constexpr const char* kGuardsOffAsked = "safe mode";
constexpr const char* kGuardsOffOverrun = "safe mode after a guest memory overrun";
std::string safe_mode_guards_off(const StartDecision& d, bool last_run_overrun);
// true when the run log at `path` (UTF-8) shows a guest memory overrun (see above); a missing file: false
bool log_shows_guard_overrun(const std::string& path);

// The user's settings lowered for a safe start (display and quality only)
Settings safe_settings(Settings s);

}  // namespace b3settings

#endif
