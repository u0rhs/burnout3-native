// replay_card.h -- media1: the throwaway memory card a replay plays with (native_play --replay; src/native/replay_file.h
// CARD) lives in "<temp>/b3port-replay-card-<pid>-<stamp>" and is removed on every way out:
//   - any return from native_play (CardFolder's destructor: a refusal, a renderer that does not load, the normal end);
//   - Ctrl+C / a terminate request (install_card_signals: POSIX SIGINT / SIGTERM / SIGHUP remove the card's two known
//     files and the folder with unlink / rmdir, which are async-signal-safe, then die as before; Windows: the console
//     control handler, on its own thread, removes the folder);
//   - a kill, a crash, a power loss: the next native_play start removes every such folder whose process is gone
//     (sweep_stale_cards; a folder of the older name without a process id once it is a day old).
#ifndef B3NATIVE_REPLAY_CARD_H
#define B3NATIVE_REPLAY_CARD_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <utility>

namespace b3native::replay_card {

namespace fs = std::filesystem;
inline constexpr const char* kPrefix = "b3port-replay-card-";

inline std::string folder_name(std::uint64_t pid, std::uint64_t stamp) {
    return std::string(kPrefix) + std::to_string(pid) + "-" + std::to_string(stamp);
}
// A folder this port made: true, with *pid its process (0: the older name, "<prefix><stamp>", which had none)
inline bool parse_name(const std::string& name, std::uint64_t* pid) {
    const std::string p = kPrefix;
    if (name.size() <= p.size() || name.compare(0, p.size(), p) != 0) return false;
    const std::string rest = name.substr(p.size());
    const std::size_t dash = rest.find('-');
    auto digits = [](const std::string& s) {
        if (s.empty() || s.size() > 20) return false;
        for (char c : s) if (c < '0' || c > '9') return false;
        return true;
    };
    if (dash == std::string::npos) {
        if (!digits(rest)) return false;
        *pid = 0;
        return true;
    }
    if (!digits(rest.substr(0, dash)) || !digits(rest.substr(dash + 1))) return false;
    *pid = std::stoull(rest.substr(0, dash));
    return *pid != 0;
}
// Removes the leftover card folders in `temp` (a process that is no longer running; the older name: a day old); returns
// how many went. `alive(pid)`: whether that process still runs.
inline int sweep_stale_cards(const fs::path& temp, const std::function<bool(std::uint64_t)>& alive) {
    int gone = 0;
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    for (fs::directory_iterator it(temp, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        std::string name;
        try { name = it->path().filename().string(); } catch (...) { continue; }
        std::uint64_t pid = 0;
        if (!parse_name(name, &pid)) continue;
        if (pid != 0 && alive(pid)) continue;
        if (pid == 0) {
            const auto t = fs::last_write_time(it->path(), e2);
            if (e2 || now - t < std::chrono::hours(24)) continue;
        }
        if (fs::remove_all(it->path(), e2) > 0 && !e2) ++gone;
    }
    return gone;
}

// The folder, removed when it goes out of scope
class CardFolder {
public:
    CardFolder() = default;
    explicit CardFolder(fs::path p) : path_(std::move(p)) {}
    ~CardFolder() { remove(); }
    CardFolder(const CardFolder&) = delete;
    CardFolder& operator=(const CardFolder&) = delete;
    void set(fs::path p) { remove(); path_ = std::move(p); }
    void remove() {
        if (path_.empty()) return;
        std::error_code ec;
        fs::remove_all(path_, ec);
        path_.clear();
    }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

}  // namespace b3native::replay_card

#endif
