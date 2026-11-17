// The user's save folder (src/native/save_store.h).
#include "save_store.h"

#include <algorithm>
#include <ctime>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <system_error>

namespace b3native::save {
namespace {
namespace fs = std::filesystem;

constexpr int kSlots = 5;   // as load_profile_slot / read_active_slot (five slots)

const char* env(const char* name) {
    const char* v = std::getenv(name);
    return v && *v ? v : nullptr;
}

}  // namespace

fs::path user_data_dir() {
#if defined(_WIN32)
    if (const char* a = env("APPDATA")) return fs::path(a) / "b3port" / "native";
    return {};
#elif defined(__APPLE__)
    if (const char* h = env("HOME")) return fs::path(h) / "Library" / "Application Support" / "b3port" / "native";
    return {};
#else
    if (const char* x = env("XDG_DATA_HOME")) return fs::path(x) / "b3port" / "native";
    if (const char* h = env("HOME")) return fs::path(h) / ".local" / "share" / "b3port" / "native";
    return {};
#endif
}

fs::path default_saves_root() {
    const fs::path d = user_data_dir();
    return d.empty() ? d : d / "saves";
}

fs::path profile_file(const fs::path& root, int slot) { return root / ("profile-" + std::to_string(slot) + ".sav"); }
fs::path legacy_profile_folder(const fs::path& root, int slot) { return root / ("profile-" + std::to_string(slot)); }

std::optional<LoadedProfile> load_profile(const fs::path& root, int slot, std::string* error,
                                          std::optional<std::int64_t> fixed_file_time) {
    if (slot < 1 || slot > kSlots) {
        if (error) *error = "invalid slot " + std::to_string(slot);
        return std::nullopt;
    }
    std::error_code ec;
    LoadedProfile out;
    if (fs::exists(profile_file(root, slot), ec)) {
        auto d = read_sav_file(profile_file(root, slot), error);
        if (!d) return std::nullopt;
        out.dirs = std::move(*d);
        out.source = ProfileSource::kSav;
        return out;
    }
    if (fs::is_directory(legacy_profile_folder(root, slot), ec)) {
        auto d = load_profile_slot(root, slot, error, fixed_file_time);
        if (!d) return std::nullopt;
        out.dirs = std::move(*d);
        out.source = ProfileSource::kFolder;
    }
    return out;
}

bool store_profile(const fs::path& root, int slot, const Directories& dirs, std::string* error) {
    if (slot < 1 || slot > kSlots) {
        if (error) *error = "invalid slot " + std::to_string(slot);
        return false;
    }
    backup_profile(root, slot, static_cast<std::int64_t>(std::time(nullptr)));   // mac5: the old file kept first
    return write_sav_file(profile_file(root, slot), dirs, error);
}

fs::path backup_profile(const fs::path& root, int slot, std::int64_t now_unix, int keep) {
    std::error_code ec;
    const fs::path cur = profile_file(root, slot);
    if (slot < 1 || slot > kSlots || !fs::is_regular_file(cur, ec)) return {};
    const fs::path dir = root / "backups";
    fs::create_directories(dir, ec);
    const std::time_t t = static_cast<std::time_t>(now_unix);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &lt);
    const std::string prefix = "profile-" + std::to_string(slot) + "-";
    fs::path out = dir / (prefix + stamp + ".sav");
    for (int n = 2; fs::exists(out, ec) && n < 100; ++n) out = dir / (prefix + stamp + "-" + std::to_string(n) + ".sav");
    if (!fs::copy_file(cur, out, fs::copy_options::overwrite_existing, ec)) return {};
    // the newest `keep` of this slot (the names sort by time)
    std::vector<fs::path> mine;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind(prefix, 0) == 0 && e.path().extension() == ".sav") mine.push_back(e.path());
    }
    std::sort(mine.begin(), mine.end());
    for (std::size_t i = 0; keep >= 0 && i + static_cast<std::size_t>(keep) < mine.size(); ++i) fs::remove(mine[i], ec);
    return out;
}

bool write_active_slot(const fs::path& root, int slot, std::string* error) {
    std::error_code ec;
    fs::create_directories(root, ec);
    std::ofstream out(root / "active-slot.txt", std::ios::binary | std::ios::trunc);
    out << slot << "\n";
    if (!out) {
        if (error) *error = "cannot write " + (root / "active-slot.txt").string();
        return false;
    }
    return true;
}

std::optional<Directories> import_game_save(const fs::path& from, std::int64_t now_unix, std::string* error) {
    auto all = read_any(from, error);
    if (!all) return std::nullopt;
    auto game = game_save(*all, error);
    if (!game) return std::nullopt;
    const CardTimestamp now = card_timestamp_from_unix(now_unix);
    const CardTimestamp unset{};
    for (auto& d : *game) {
        if (d.created == unset) d.created = now;
        if (d.modified == unset) d.modified = now;
        for (auto& f : d.files) {
            if (f.created == unset) f.created = now;
            if (f.modified == unset) f.modified = now;
        }
    }
    // the card must hold it (build_card_image refuses names / sizes it cannot store)
    std::string why;
    if (build_card_image(*game, &why).empty()) {
        if (error) *error = "the save does not fit a card: " + why;
        return std::nullopt;
    }
    return game;
}

bool import_profile(const fs::path& root, int slot, const fs::path& from, std::int64_t now_unix, std::string* error) {
    auto dirs = import_game_save(from, now_unix, error);
    if (!dirs) return false;
    std::error_code ec;
    const fs::path target = profile_file(root, slot);
    if (fs::exists(target, ec)) fs::copy_file(target, target.string() + ".bak", fs::copy_options::overwrite_existing, ec);
    return store_profile(root, slot, *dirs, error);
}

// ---- ProfileCard ----
struct ProfileCard::Shared {
    mutable std::mutex m;
    Request request = Request::kNone;
    fs::path path;
    Status status;
};

namespace {
std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}  // namespace

ProfileCard::ProfileCard(fs::path root) : root_(std::move(root)), card_(root_), shared_(new Shared) {
    const char* p = env("B3NATIVE_CARD_PERSIST");   // native_play sets 1; proof tools (native_boot ...) leave it off
    persist_ = p && p[0] == '1' && p[1] == 0;
}

ProfileCard::~ProfileCard() {
    std::string err;
    if (!flush(&err)) std::fprintf(stderr, "[save] %s\n", err.c_str());
    delete shared_;
}

bool ProfileCard::attach(int slot, std::string* error) {
    std::string why;
    auto p = load_profile(root_, slot, &why, card_.fixed_file_time());
    if (p && card_.attach_directories(slot, p->dirs, &why)) {
        slot_ = slot;
        source_ = p->source;
        seen_writes_ = flushed_writes_ = card_.writes();
        return true;
    }
    // an unreadable file: the game gets an empty card (no "no memory card" pages) and nothing is written over the file
    if (error) *error = why;
    slot_ = slot;
    source_ = ProfileSource::kNone;
    persist_ = false;
    card_.attach_directories(slot, {});
    seen_writes_ = flushed_writes_ = card_.writes();
    set_status(false, "SAVE FILE " + std::to_string(slot) + " UNREADABLE (" + why + "): NOTHING IS SAVED THIS SESSION");
    return false;
}

bool ProfileCard::flush(std::string* error) {
    if (!card_.attached() || card_.writes() == flushed_writes_) return true;
    if (!persist_) {
        flushed_writes_ = card_.writes();
        return true;
    }
    const auto& img = card_.image();
    std::string why;
    auto dirs = read_card_image(img.data(), img.size(), &why);
    if (!dirs) {
        if (error) *error = "the card's filesystem does not read back (" + why + "): profile-" + std::to_string(slot_) + ".sav not written";
        return false;
    }
    if (!store_profile(root_, slot_, *dirs, error)) return false;
    flushed_writes_ = card_.writes();
    std::printf("[save] profile %d saved to %s\n", slot_, profile_file(root_, slot_).string().c_str());
    std::fflush(stdout);
    return true;
}

void ProfileCard::tick(std::uint64_t vblank) {
    now_ = vblank;
    if (card_.writes() != seen_writes_) {
        seen_writes_ = card_.writes();
        last_change_ = vblank;
    }
    if (seen_writes_ != flushed_writes_ && vblank >= last_change_ + kSettleVBlanks && vblank >= retry_at_) {
        std::string err;
        if (!flush(&err)) {
            std::fprintf(stderr, "[save] %s (retrying)\n", err.c_str());
            retry_at_ = vblank + 10 * kSettleVBlanks;
        }
    }
    Request r;
    fs::path path;
    {
        std::lock_guard<std::mutex> lock(shared_->m);
        r = shared_->request;
        path = shared_->path;
        shared_->request = Request::kNone;
    }
    if (r != Request::kNone) run(r, path);
}

void ProfileCard::post(Request r, fs::path path) {
    std::lock_guard<std::mutex> lock(shared_->m);
    shared_->request = r;
    shared_->path = std::move(path);
}

ProfileCard::Status ProfileCard::status() const {
    std::lock_guard<std::mutex> lock(shared_->m);
    return shared_->status;
}

void ProfileCard::set_status(bool ok, std::string text) {
    std::printf("[save] %s\n", text.c_str());
    std::fflush(stdout);
    std::lock_guard<std::mutex> lock(shared_->m);
    ++shared_->status.serial;
    shared_->status.ok = ok;
    shared_->status.text = std::move(text);
}

void ProfileCard::run(Request r, const fs::path& path) {
    std::string err;
    if (r == Request::kExport) {
        // the card as it is now (a save not yet settled included)
        const auto& img = card_.image();
        auto dirs = card_.attached() ? read_card_image(img.data(), img.size(), &err) : std::nullopt;
        if (!dirs) return set_status(false, "EXPORT FAILED: " + (err.empty() ? std::string("no card") : err));
        auto game = game_save(*dirs, &err);
        if (!game) return set_status(false, "EXPORT FAILED: NO PROFILE SAVED YET");
        if (!write_sav_file(path, *game, &err)) return set_status(false, "EXPORT FAILED: " + err);
        return set_status(true, "EXPORTED TO " + path.string());
    }
    if (r == Request::kImport) {
        if (!import_profile(root_, slot_, path, unix_now(), &err)) return set_status(false, "IMPORT FAILED: " + err);
        // the game holds the old profile until it loads again: its later saves would overwrite the import
        persist_ = false;
        flushed_writes_ = card_.writes();
        return set_status(true, "IMPORTED. QUIT AND START THE GAME AGAIN TO PLAY IT.");
    }
}

}  // namespace b3native::save
