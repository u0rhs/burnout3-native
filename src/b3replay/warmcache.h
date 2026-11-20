// perf4: the warm-up list (the next start's pipelines and effect textures, made before the race needs them).
//   The 2D stage's pipelines are keyed by the GS ALPHA byte and flags (b3gfx overlay_pipeline): the keys a run made are saved
//   here and made at the next start (Device::warm_overlay_keys). The effect and corona textures (GLOBAL.TXD by name,
//   Effects::texture) a run used are loaded with each route (Effects::preload) instead of at their first draw.
// The file: warm_cache.txt beside pc_settings.ini (the window), or $B3_WARM_CACHE; lines "overlay <hex>" and "texture <name>",
// at most kMaxEntries of each (read and written; a texture name that no longer loads is dropped by Effects::preload, so the
// file holds what the last run used, not every name ever seen). Read through b3io (UTF-8 names), written by its safe replace.
// Presentation only: what is made early is what would have been made at its first use.
#ifndef B3REPLAY_WARMCACHE_H
#define B3REPLAY_WARMCACHE_H

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "b3port/fileio.h"

namespace b3replay::warm {

constexpr std::size_t kMaxEntries = 256;   // per kind

struct Cache {
    std::set<int> overlay;
    std::set<std::string> textures;
};

inline std::string to_text(const Cache& c) {
    std::string s = "# b3 warm cache 1 (pipelines and effect textures made at the start; safe to delete)\n";
    char buf[32];
    std::size_t n = 0;
    for (int k : c.overlay) {
        if (n++ == kMaxEntries) break;
        std::snprintf(buf, sizeof buf, "overlay %03x\n", unsigned(k));
        s += buf;
    }
    n = 0;
    for (const std::string& t : c.textures) {
        if (n++ == kMaxEntries) break;
        s += "texture " + t + "\n";
    }
    return s;
}

// Lines it does not know (a newer or damaged file) are left out; a key outside 0..0xFFF or a name over 64 characters too.
inline Cache from_text(const std::string& text) {
    Cache c;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("overlay ", 0) == 0) {
            char* end = nullptr;
            const long k = std::strtol(line.c_str() + 8, &end, 16);
            if (end && end != line.c_str() + 8 && *end == '\0' && k >= 0 && k < 0x1000 && c.overlay.size() < kMaxEntries) c.overlay.insert(int(k));
        } else if (line.rfind("texture ", 0) == 0) {
            const std::string n = line.substr(8);
            if (!n.empty() && n.size() <= 64 && c.textures.size() < kMaxEntries &&
                std::all_of(n.begin(), n.end(), [](char ch) { return ch > ' ' && ch < 127; }))
                c.textures.insert(n);
        }
    }
    return c;
}

inline bool load(const std::string& path, Cache& c) {
    FILE* f = path.empty() ? nullptr : b3io::fopen(b3io::u8path(path), "rb");
    if (!f) return false;
    std::string s;
    char buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0 && s.size() < (1u << 20);) s.append(buf, n);
    std::fclose(f);
    c = from_text(s);
    return true;
}

// (not durable: a cache; a lost write only makes the next start's warm-up smaller)
inline bool save(const std::string& path, const Cache& c) {
    return !path.empty() && b3io::replace_file(b3io::u8path(path), to_text(c), nullptr, false);
}

}  // namespace b3replay::warm

#endif
