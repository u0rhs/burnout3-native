// oneclick.h -- 1.1 one-click updates: getting a release's package for this system, checked, onto the disk.
//
//   1. SHA256SUMS and SHA256SUMS.sig from the release (small files, at most 64 KB each)
//   2. the signature: SHA256SUMS signed by the release key in the update namespace (sign.h, update_key.h), else stop
//   3. this system's package (update.h package_asset) must be listed there; downloaded (at most 2 GB)
//   4. its SHA-256 must be the listed one, else stop (nothing written)
//   5. written to `dir`/<its name> (a ".part" file renamed when complete)
// Installing it is installer.h's (Windows, Linux) or the player's (macOS: the dmg opened). Every request goes through
// `fetch`, so the steps are tested with canned files (tests/test_launcher_oneclick.cpp) and the launcher's --screenshot
// checks; the launcher's fetch is update_net.h's https_get.
#ifndef B3LAUNCHER_ONECLICK_H
#define B3LAUNCHER_ONECLICK_H

#include "update.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace b3launcher::oneclick {

namespace fs = std::filesystem;

// One GET: `download` a release file (else the API), at most `max_bytes`
using Fetch = std::function<update::Response(const std::string& url, bool download, std::size_t max_bytes)>;
// Where the work is, for the launcher's progress line
enum class Step : int { kSums = 0, kPackage, kCheck, kWrite };
using OnStep = std::function<void(Step)>;

struct Result {
    bool ok = false;
    std::string why;      // !ok: what failed, in a few words
    fs::path package;     // ok: the checked package on disk
};
// The release key as update_key.h gives it; false when there is none (one-click updates off) or it is not a key
bool release_key(std::array<std::uint8_t, 32>& key);
// Steps 1-5 for release `r` on platform `p`
Result fetch_package(const update::Release& r, update::Platform p, const std::array<std::uint8_t, 32>& key, const Fetch& fetch,
                     const fs::path& dir, const OnStep& on_step = {});

}  // namespace b3launcher::oneclick

#endif
