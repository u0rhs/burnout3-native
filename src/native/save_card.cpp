// The PS2 memory card the game's libmc2 talks to (src/native/save_card.h; sif-lane rewrite, 2026-11-10).
//
// libmc2 (the retail EE library) keeps its own FAT filesystem on the card and reaches the card through libdbc's page
// commands (platform_dbc.cpp). So the card here is what the hardware is: 16384 pages of 512 bytes, erased to 0xFF,
// written and erased by the game. When a profile is attached the card holds a freshly formatted filesystem with the
// profile's save directories in it, laid out the way a card that was formatted and then had those directories copied
// onto it looks:
//
//   cluster = 2 pages (1 KiB), block = 16 pages. Clusters 0..40 are the format's: page 0 the superblock (page 1 left
//   erased), cluster 8 the indirect FAT (the 32 FAT cluster numbers 9..40), clusters 9..40 the FAT (one word per
//   cluster of the card: 0x7FFFFFFF free, 0x80000000 | next in a chain, 0xFFFFFFFF the chain's end; the words past
//   the allocatable clusters read as ends). Allocatable clusters are counted from cluster 41 and there are 8135 of
//   them; the last two blocks (1022 / 1023) are the format's backup blocks and stay erased.
//   Superblock: "Sony PS2 Memory Card Format 1.2.0.0", page size 512, 2 pages a cluster, 16 a block, 0xFF00, 8192
//   clusters, allocatable from 41 for 8135, root directory at (relative) cluster 0, backup blocks 1023 / 1022, the
//   indirect FAT cluster list {8}, a bad block list of 32 x 0xFFFFFFFF, card type 2, flags 0x52.
//   Directory entries: 512 bytes, two to a cluster: +0 mode, +4 length (a file's bytes; a directory's entry count in
//   its own "." and in its parent's entry), +8 created, +0x10 first cluster, +0x14 for a "." the entry's index in its
//   parent, +0x18 modified, +0x40 the name. A time is {0, second, minute, hour, day, month, year (16 bits)} in Japan
//   time (the console's).
//   Contents: the root directory first ("." and "..", then one entry per save directory); then for each save directory
//   in turn its entry clusters ("." and "..", then its files) followed by its files' data, each in consecutive clusters
//   from the lowest free one (zero-filled to the cluster's end); an empty file has no cluster (0xFFFFFFFF). Modes:
//   directories 0x8427 (the root's ".." 0xA426), files 0x8497, entries in a directory listing take the stored mode with
//   the exists and kind bits set. The root's times are the first save directory's creation time (2000-01-01 00:00 on
//   an empty card).
//
// The profile folder (saves/profile-<slot>/<save directory>/<files>) is
// read as: every sub-folder whose name does not start with '.', in name order; in each, every regular file except
// _pcsx2_index, _pcsx2_superblock and *.writing (a ".ps2" suffix dropped from the card name), icon.sys first and the
// rest in name order, each with its file's modification time (whole seconds), the directory with the folder's; or one
// fixed time for all (B3NATIVE_CARD_FILE_TIME). PCSX2's _pcsx2_index is not read.
#include "save_card.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace b3native::save {
namespace {

namespace fs = std::filesystem;

// ---- the format
constexpr std::size_t kSuperblockClusters = 41;          // superblock .. FAT
constexpr std::uint32_t kAllocatable = 8135;
constexpr std::uint32_t kIndirectFat = 8, kFirstFat = 9, kFatClusters = 32;
constexpr std::uint32_t kClusters = kPageCount / kPagesPerCluster;   // 8192
constexpr std::uint32_t kFree = 0x7FFFFFFFu, kLinked = 0x80000000u, kEnd = 0xFFFFFFFFu;
constexpr std::size_t kEntryBytes = 512, kEntriesInCluster = kClusterSize / kEntryBytes, kNameRoom = 32;
constexpr std::uint16_t kExists = 0x8000, kIsFile = 0x0010, kIsDir = 0x0020, kRootParentMode = 0xA426;
constexpr std::int64_t kJapan = 9 * 3600;

void le16(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
void le32(std::uint8_t* p, std::uint32_t v) {
    le16(p, v & 0xFFFFu);
    le16(p + 2, v >> 16);
}
void card_time(std::uint8_t* p, const CardTimestamp& t) {
    p[0] = 0;
    p[1] = t.second;
    p[2] = t.minute;
    p[3] = t.hour;
    p[4] = t.day;
    p[5] = t.month;
    le16(p + 6, t.year);
}

struct Entry {
    std::uint16_t mode;
    std::uint32_t length;
    CardTimestamp created, modified;
    std::uint32_t cluster, index_in_parent;
    const std::string& name;
};

// The card while it is being written: all erased, clusters handed out lowest first
class NewCard {
public:
    NewCard() : bytes_(kImageSize, 0xFF), fat_(kAllocatable, kFree) {}

    // n clusters in a row (relative numbers), chained and zeroed; false when they do not fit
    bool take(std::size_t n, std::uint32_t& first) {
        if (n == 0 || n > kAllocatable - used_) return false;
        first = used_;
        for (std::uint32_t k = 0; k < n; ++k) fat_[first + k] = k + 1 < n ? (kLinked | (first + k + 1)) : kEnd;
        used_ += static_cast<std::uint32_t>(n);
        std::memset(cluster(first), 0, n * kClusterSize);
        return true;
    }
    std::uint8_t* cluster(std::uint32_t relative) { return bytes_.data() + (kSuperblockClusters + relative) * kClusterSize; }
    void put(std::uint32_t dir_first, std::size_t index, const Entry& e) {
        std::uint8_t* p = cluster(dir_first + static_cast<std::uint32_t>(index / kEntriesInCluster)) + (index % kEntriesInCluster) * kEntryBytes;
        std::memset(p, 0, kEntryBytes);
        le16(p, e.mode);
        le32(p + 4, e.length);
        card_time(p + 8, e.created);
        le32(p + 0x10, e.cluster);
        le32(p + 0x14, e.index_in_parent);
        card_time(p + 0x18, e.modified);
        std::memcpy(p + 0x40, e.name.data(), e.name.size());
    }
    std::vector<std::uint8_t> done() {
        std::uint8_t* sb = bytes_.data();
        std::memset(sb, 0, kPageSize);
        static const char kTitle[] = "Sony PS2 Memory Card Format 1.2.0.0";
        std::memcpy(sb, kTitle, sizeof kTitle - 1);
        le16(sb + 0x28, kPageSize);
        le16(sb + 0x2A, kPagesPerCluster);
        le16(sb + 0x2C, kPagesPerBlock);
        le16(sb + 0x2E, 0xFF00);
        le32(sb + 0x30, kClusters);
        le32(sb + 0x34, kSuperblockClusters);
        le32(sb + 0x38, kAllocatable);
        le32(sb + 0x3C, 0);                     // the root directory
        le32(sb + 0x40, kBlockCount - 1);       // backup blocks
        le32(sb + 0x44, kBlockCount - 2);
        le32(sb + 0x50, kIndirectFat);
        for (std::size_t k = 0; k < 32; ++k) le32(sb + 0xD0 + 4 * k, 0xFFFFFFFFu);   // no bad blocks
        sb[0x150] = 2;
        sb[0x151] = 0x52;
        std::uint8_t* ind = bytes_.data() + kIndirectFat * kClusterSize;
        for (std::uint32_t k = 0; k < kFatClusters; ++k) le32(ind + 4 * k, kFirstFat + k);
        std::uint8_t* fat = bytes_.data() + kFirstFat * kClusterSize;
        for (std::uint32_t c = 0; c < kClusters; ++c) le32(fat + 4 * c, c < kAllocatable ? fat_[c] : kEnd);
        return std::move(bytes_);
    }

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<std::uint32_t> fat_;
    std::uint32_t used_ = 0;
};

bool name_fits(const std::string& n) {
    return !n.empty() && n.size() < kNameRoom && n != "." && n != ".." && n.find('\0') == std::string::npos &&
           n.find('/') == std::string::npos;
}
std::vector<std::uint8_t> refuse(std::string* error, std::string why) {
    if (error) *error = std::move(why);
    return {};
}
std::size_t clusters_for(std::size_t bytes) { return (bytes + kClusterSize - 1) / kClusterSize; }

// ---- the profile folder
std::optional<std::vector<std::uint8_t>> slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::uint8_t> v((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return std::nullopt;
    return v;
}
std::int64_t mtime(const fs::path& p, std::optional<std::int64_t> fixed) {
    if (fixed) return *fixed;
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::file_clock::to_sys(t).time_since_epoch()).count();
}
bool ends_with(const std::string& s, const char* tail) {
    const std::size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}
bool is_icon_sys(const std::string& name) {
    if (name.size() != 8) return false;
    for (std::size_t k = 0; k < 8; ++k)
        if (std::tolower(static_cast<unsigned char>(name[k])) != "icon.sys"[k]) return false;
    return true;
}

std::optional<CardSaveDirectory> read_folder(const fs::path& dir, std::optional<std::int64_t> fixed, std::string* error) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec) || ec) {
        if (error) *error = "not a save folder: " + dir.string();
        return std::nullopt;
    }
    CardSaveDirectory d;
    d.name = dir.filename().string();
    d.created = d.modified = card_timestamp_from_unix(mtime(dir, fixed));
    fs::directory_iterator it(dir, ec);
    if (ec) {
        if (error) *error = "cannot list " + dir.string();
        return std::nullopt;
    }
    for (const fs::directory_entry& e : it) {
        std::error_code fec;
        if (!e.is_regular_file(fec) || fec) continue;
        const std::string on_disk = e.path().filename().string();
        if (on_disk == "_pcsx2_index" || on_disk == "_pcsx2_superblock" || ends_with(on_disk, ".writing")) continue;
        auto bytes = slurp(e.path());
        if (!bytes) {
            if (error) *error = "cannot read " + e.path().string();
            return std::nullopt;
        }
        CardFile f;
        f.name = ends_with(on_disk, ".ps2") ? on_disk.substr(0, on_disk.size() - 4) : on_disk;
        f.content = std::move(*bytes);
        f.created = f.modified = card_timestamp_from_unix(mtime(e.path(), fixed));
        d.files.push_back(std::move(f));
    }
    std::sort(d.files.begin(), d.files.end(), [](const CardFile& a, const CardFile& b) {
        const bool ia = is_icon_sys(a.name), ib = is_icon_sys(b.name);
        if (ia != ib) return ia;
        return a.name < b.name;
    });
    return d;
}

constexpr int kSlots = 5;

// days since 1970-01-01 <-> civil dates (H. Hinnant's algorithms, proleptic Gregorian)
void civil_from_days(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
}
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}
std::optional<std::int64_t> whole_number(const std::string& s) {
    if (s.empty()) return std::nullopt;
    std::size_t k = 0;
    bool neg = false;
    if (s[0] == '-' || s[0] == '+') { neg = s[0] == '-'; k = 1; }
    if (k == s.size()) return std::nullopt;
    std::int64_t v = 0;
    for (; k < s.size(); ++k) {
        if (s[k] < '0' || s[k] > '9' || v > (INT64_MAX - 9) / 10) return std::nullopt;
        v = v * 10 + (s[k] - '0');
    }
    return neg ? -v : v;
}

}  // namespace

// ---- times
CardTimestamp card_timestamp_from_unix(std::int64_t unix_seconds) {
    const std::int64_t t = unix_seconds + kJapan;
    std::int64_t days = t / 86400, secs = t % 86400;
    if (secs < 0) {
        secs += 86400;
        --days;
    }
    std::int64_t y = 0;
    unsigned m = 0, d = 0;
    civil_from_days(days, y, m, d);
    CardTimestamp c;
    c.year = static_cast<std::uint16_t>(std::clamp<std::int64_t>(y, 0, 65535));
    c.month = static_cast<std::uint8_t>(m);
    c.day = static_cast<std::uint8_t>(d);
    c.hour = static_cast<std::uint8_t>(secs / 3600);
    c.minute = static_cast<std::uint8_t>(secs / 60 % 60);
    c.second = static_cast<std::uint8_t>(secs % 60);
    return c;
}
std::int64_t unix_from_card_timestamp(const CardTimestamp& t) {
    // a card can hold zero months / days: read as the first
    const unsigned m = std::clamp<unsigned>(t.month, 1, 12), d = std::clamp<unsigned>(t.day, 1, 31);
    return days_from_civil(t.year, m, d) * 86400 + std::int64_t{t.hour} * 3600 + std::int64_t{t.minute} * 60 + t.second - kJapan;
}

// ---- a formatted card holding the directories
std::vector<std::uint8_t> build_card_image(const std::vector<CardSaveDirectory>& dirs, std::string* error) {
    NewCard card;
    static const std::string kDot = ".", kDotDot = "..";
    const std::size_t root_n = dirs.size() + 2;
    std::uint32_t root = 0;
    if (!card.take(clusters_for(root_n * kEntryBytes), root)) return refuse(error, "card full (root directory)");
    const CardTimestamp formatted = dirs.empty() ? CardTimestamp{} : dirs.front().created;
    card.put(root, 0, {kModeDirectory, static_cast<std::uint32_t>(root_n), formatted, formatted, 0, 0, kDot});
    card.put(root, 1, {kRootParentMode, 0, formatted, formatted, 0, 0, kDotDot});
    for (std::size_t k = 0; k < dirs.size(); ++k) {
        const CardSaveDirectory& d = dirs[k];
        if (!name_fits(d.name)) return refuse(error, "save directory name not storable: " + d.name);
        const std::size_t n = d.files.size() + 2;
        std::uint32_t at = 0;
        if (!card.take(clusters_for(n * kEntryBytes), at)) return refuse(error, "card full (directory " + d.name + ")");
        card.put(root, k + 2, {static_cast<std::uint16_t>(d.mode | kExists | kIsDir), static_cast<std::uint32_t>(n), d.created,
                               d.modified, at, 0, d.name});
        card.put(at, 0, {kModeDirectory, 0, d.created, d.modified, root, static_cast<std::uint32_t>(k + 2), kDot});
        card.put(at, 1, {kModeDirectory, 0, d.created, d.modified, 0, 0, kDotDot});
        for (std::size_t i = 0; i < d.files.size(); ++i) {
            const CardFile& f = d.files[i];
            if (!name_fits(f.name) || f.content.size() > 0xFFFFFFFFull) return refuse(error, "file not storable: " + d.name + "/" + f.name);
            std::uint32_t data = kEnd;
            if (!f.content.empty()) {
                if (!card.take(clusters_for(f.content.size()), data)) return refuse(error, "card full (file " + d.name + "/" + f.name + ")");
                std::memcpy(card.cluster(data), f.content.data(), f.content.size());
            }
            card.put(at, i + 2, {static_cast<std::uint16_t>(f.mode | kExists | kIsFile), static_cast<std::uint32_t>(f.content.size()),
                                 f.created, f.modified, data, 0, f.name});
        }
    }
    return card.done();
}

// ---- the profile folders
std::optional<std::vector<CardSaveDirectory>> load_profile_slot(const fs::path& saves_root, int slot, std::string* error,
                                                                std::optional<std::int64_t> fixed_file_time) {
    if (slot < 1 || slot > kSlots) {
        if (error) *error = "invalid slot " + std::to_string(slot);
        return std::nullopt;
    }
    std::vector<fs::path> folders;
    std::error_code ec;
    for (fs::directory_iterator it(saves_root / ("profile-" + std::to_string(slot)), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code dec;
        const std::string name = it->path().filename().string();
        if (it->is_directory(dec) && !dec && !name.empty() && name[0] != '.') folders.push_back(it->path());
    }
    std::sort(folders.begin(), folders.end());
    std::vector<CardSaveDirectory> out;
    for (const fs::path& f : folders) {
        auto d = read_folder(f, fixed_file_time, error);
        if (!d) return std::nullopt;
        out.push_back(std::move(*d));
    }
    return out;
}
std::optional<CardSaveDirectory> load_save_folder(const fs::path& dir, std::string* error, std::optional<std::int64_t> fixed_file_time) {
    return read_folder(dir, fixed_file_time, error);
}

int read_active_slot(const fs::path& saves_root) {
    const auto text = slurp(saves_root / "active-slot.txt");
    if (!text) return 1;
    std::size_t k = 0;
    while (k < text->size() && std::isspace((*text)[k])) ++k;
    if (k < text->size() && (*text)[k] == '+') ++k;
    int v = 0, digits = 0;
    for (; k < text->size() && (*text)[k] >= '0' && (*text)[k] <= '9' && digits < 9; ++k, ++digits) v = v * 10 + ((*text)[k] - '0');
    if (k < text->size() && (*text)[k] >= '0' && (*text)[k] <= '9') return 1;   // a longer number: not a slot
    return digits > 0 && v >= 1 && v <= kSlots ? v : 1;
}

// ---- the clock
bool parse_clock(const char* text, ClockBcd& out) {
    if (!text || std::strlen(text) != 16) return false;
    ClockBcd v{};
    for (std::size_t k = 0; k < 16; ++k) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[k])));
        const int n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (n < 0) return false;
        v[k / 2] = static_cast<std::uint8_t>(v[k / 2] << 4 | n);
    }
    out = v;
    return true;
}
bool parse_rtc(const char* text, RtcBase& out) {
    if (!text) return false;
    const std::string s(text);
    const std::size_t comma = s.find(',');
    if (comma == std::string::npos) return false;
    const auto when = whole_number(s.substr(0, comma)), offset = whole_number(s.substr(comma + 1));
    if (!when || !offset || *when < 0 || *offset < -14 * 3600 || *offset > 14 * 3600) return false;
    out.unix = *when;
    out.utc_offset = static_cast<std::int32_t>(*offset);
    return true;
}
ClockBcd rtc_bcd(const RtcBase& base, std::uint64_t vblanks) {
    // VBlank 0's moment plus the run's virtual time (NTSC: 60000 / 1001 VBlanks a second), the player's local time
    // shown as the console's Japan time
    const std::int64_t t = base.unix + static_cast<std::int64_t>(vblanks * 1001ull / 60000ull) + base.utc_offset + kJapan;
    std::int64_t days = t / 86400, secs = t % 86400;
    if (secs < 0) {
        secs += 86400;
        --days;
    }
    std::int64_t y = 0;
    unsigned m = 0, d = 0;
    civil_from_days(days, y, m, d);
    const auto bcd = [](std::int64_t v) { return static_cast<std::uint8_t>((v / 10 % 10) << 4 | v % 10); };
    return ClockBcd{0, bcd(secs % 60), bcd(secs / 60 % 60), bcd(secs / 3600), 0, bcd(d), bcd(m), bcd((y % 100 + 100) % 100)};
}
ClockBcd rtc_now(std::uint64_t vblanks) {
    static const std::optional<RtcBase> base = []() -> std::optional<RtcBase> {
        const char* e = std::getenv("B3NATIVE_RTC");
        if (!e || !*e) return std::nullopt;
        RtcBase b;
        if (!parse_rtc(e, b)) throw std::runtime_error(std::string("B3NATIVE_RTC: expected <unix seconds>,<UTC offset seconds>, got '") + e + "'");
        return b;
    }();
    return base ? rtc_bcd(*base, vblanks) : kDefaultClock;
}

// ---- the card in the slot
SaveCard::SaveCard(fs::path saves_root) : saves_root_(std::move(saves_root)) {
    if (const char* c = std::getenv("B3NATIVE_CARD_CLOCK"); c && *c) {
        if (!parse_clock(c, clock_))
            throw std::runtime_error(std::string("B3NATIVE_CARD_CLOCK: expected 16 hex digits (8 reply bytes), got '") + c + "'");
        clock_fixed_ = true;
    }
    if (const char* t = std::getenv("B3NATIVE_CARD_FILE_TIME"); t && *t) {
        file_time_ = whole_number(t);
        if (!file_time_) throw std::runtime_error(std::string("B3NATIVE_CARD_FILE_TIME: expected Unix seconds, got '") + t + "'");
    }
}
bool SaveCard::attach(int slot, std::string* error) {
    detach_quietly();
    const auto dirs = load_profile_slot(saves_root_, slot, error, file_time_);
    return dirs && attach_directories(slot, *dirs, error);
}
bool SaveCard::attach_directories(int slot, const std::vector<CardSaveDirectory>& dirs, std::string* error) {
    detach_quietly();
    std::vector<std::uint8_t> img = build_card_image(dirs, error);
    if (img.empty()) return false;
    image_ = std::move(img);
    slot_ = slot;
    new_card_pending_ = true;   // the next probe says "a different card"
    return true;
}
void SaveCard::detach_quietly() {
    slot_.reset();
    image_.clear();
    dirty_ = false;
}
void SaveCard::detach() {
    if (dirty_) std::fprintf(stderr, "[save-card] detach: slot %d's writes left in the image\n", slot_.value_or(0));
    detach_quietly();
}
std::uint32_t SaveCard::probe() {
    ++stats_.probes;
    if (!slot_) return kProbeAbsent;
    const bool fresh = new_card_pending_;
    new_card_pending_ = false;
    return fresh ? kProbeNewCard : kProbeSameCard;
}
bool SaveCard::read_pages(std::uint32_t first_page, std::uint32_t page_count, std::uint8_t* out, std::size_t out_size) {
    const std::size_t n = std::size_t{page_count} * kPageSize;
    if (!slot_ || first_page >= kPageCount || page_count > kPageCount - first_page || out_size < n) return false;
    std::memcpy(out, image_.data() + std::size_t{first_page} * kPageSize, n);
    stats_.page_reads += page_count;
    return true;
}
bool SaveCard::write_pages(std::uint32_t first_page, const std::uint8_t* data, std::size_t size) {
    if (!slot_ || size % kPageSize || first_page >= kPageCount || size / kPageSize > kPageCount - first_page) return false;
    std::memcpy(image_.data() + std::size_t{first_page} * kPageSize, data, size);
    stats_.page_writes += size / kPageSize;
    dirty_ = true;
    if (std::getenv("B3NATIVE_CARD_TRACE"))
        std::fprintf(stderr, "[save-card] write pages %u..%u (slot %d)\n", first_page,
                     first_page + static_cast<std::uint32_t>(size / kPageSize) - 1, *slot_);
    return true;
}
bool SaveCard::erase_block(std::uint32_t block) {
    if (!slot_ || block >= kBlockCount) return false;
    std::memset(image_.data() + std::size_t{block} * kPagesPerBlock * kPageSize, 0xFF, kPagesPerBlock * kPageSize);
    ++stats_.block_erases;
    dirty_ = true;
    if (std::getenv("B3NATIVE_CARD_TRACE")) std::fprintf(stderr, "[save-card] erase block %u (slot %d)\n", block, *slot_);
    return true;
}

}  // namespace b3native::save
