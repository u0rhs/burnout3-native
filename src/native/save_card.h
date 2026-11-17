// save_card.h -- the PS2 memory card in the game's slot (src/native/save_card.cpp): an 8 MiB card of 512-byte pages
// that the game's libmc2 formats, reads and writes through libdbc's page commands (platform_dbc.cpp), and a fresh
// card image holding a profile's save directories (save_store.h keeps a profile in its .sav).
//
// Self-contained (standard library only): no guest memory, no platform state.
#ifndef B3NATIVE_SAVE_CARD_H
#define B3NATIVE_SAVE_CARD_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace b3native::save {

// The standard 8 MiB card: 512-byte pages, 2 to a cluster, 16 to a block, 16384 pages. The image holds the pages'
// data only (no per-page ECC spare area).
inline constexpr std::size_t kPageSize = 512;
inline constexpr std::size_t kPagesPerCluster = 2;
inline constexpr std::size_t kPagesPerBlock = 16;
inline constexpr std::size_t kPageCount = 16384;
inline constexpr std::size_t kClusterSize = kPageSize * kPagesPerCluster;
inline constexpr std::size_t kBlockCount = kPageCount / kPagesPerBlock;
inline constexpr std::size_t kImageSize = kPageCount * kPageSize;

// Directory-entry modes of save directories and their files (read / write / execute, a PS2 entry).
inline constexpr std::uint16_t kModeDirectory = 0x8427;
inline constexpr std::uint16_t kModeFile = 0x8497;

// A directory-entry time: Japan Standard Time (UTC+9), as the console keeps it.
struct CardTimestamp {
    std::uint16_t year = 2000;
    std::uint8_t month = 1, day = 1, hour = 0, minute = 0, second = 0;
    bool operator==(const CardTimestamp&) const = default;
};
CardTimestamp card_timestamp_from_unix(std::int64_t unix_seconds);
std::int64_t unix_from_card_timestamp(const CardTimestamp& t);

struct CardFile {
    std::string name;
    std::vector<std::uint8_t> content;
    CardTimestamp created, modified;
    std::uint16_t mode = kModeFile;
    bool operator==(const CardFile&) const = default;
};

// One save directory of the card's root directory.
struct CardSaveDirectory {
    std::string name;
    CardTimestamp created, modified;
    std::uint16_t mode = kModeDirectory;
    std::vector<CardFile> files;
    bool operator==(const CardSaveDirectory&) const = default;
};

// A freshly formatted card image (kImageSize bytes) holding `directories` in order (the layout: save_card.cpp's
// header). Empty (and *error set) when a name cannot be stored or the directories do not fit.
std::vector<std::uint8_t> build_card_image(const std::vector<CardSaveDirectory>& directories,
                                           std::string* error = nullptr);

// A profile folder "<saves_root>/profile-<slot>" (slot 1..5): every
// sub-folder not starting with '.', in name order, each read as load_save_folder. An absent folder is an empty card.
// nullopt (and *error) for a bad slot number or an unreadable save folder / file.
std::optional<std::vector<CardSaveDirectory>> load_profile_slot(const std::filesystem::path& saves_root, int slot,
                                                                std::string* error = nullptr,
                                                                std::optional<std::int64_t> fixed_file_time = std::nullopt);

// One save folder as a card directory: its regular files but _pcsx2_index / _pcsx2_superblock / *.writing (a ".ps2"
// suffix dropped from the card name), icon.sys first and the rest in name order, each timed by its modification time
// (the directory by the folder's), or all by `fixed_file_time` (Unix seconds).
std::optional<CardSaveDirectory> load_save_folder(const std::filesystem::path& dir, std::string* error = nullptr,
                                                  std::optional<std::int64_t> fixed_file_time = std::nullopt);

// The active slot from "<saves_root>/active-slot.txt": its number when 1..5, else (or without the file) 1.
int read_active_slot(const std::filesystem::path& saves_root);

// The card's real-time clock reply (its command 10): status, second, minute, hour, 0, day, month, year % 100, BCD.
using ClockBcd = std::array<std::uint8_t, 8>;
// The fixed clock of test runs: 2004-09-07 12:00:00.
inline constexpr ClockBcd kDefaultClock = {0x00, 0x00, 0x00, 0x12, 0x00, 0x07, 0x09, 0x04};
// B3NATIVE_CARD_CLOCK: the 8 reply bytes as 16 hex digits in reply order, e.g. "0000001200070904" (the default).
// Returns false for anything else (out untouched).
bool parse_clock(const char* text, ClockBcd& out);

// The console's real-time clock (mac5, 2026-11-09: the date in the game follows the current
// date). The PS2 keeps its RTC in Japan time (libscf's sceScfGetGMTfromRTC subtracts 540 minutes [C decomp
// assembly/asm/sce/libscf.s 0x24B250]); the game reads it through libcdvd (func_00436350 / func_00436460: the
// profile's dates, CGtSoundUtils::mRandom's seed at 0x387240 -- sound choices, not physics or AI) and libmc2 (command
// 10: the card's file times). Natively the reply is fixed (kDefaultClock: test runs and proofs stay exact) unless
// $B3NATIVE_RTC = "<unix seconds>,<UTC offset seconds>" names the player's clock at VBlank 0: then the reply is that
// moment plus the run's virtual time (VBlanks at 59.94 Hz), shown as the player's local time (local + 9 h as the RTC's
// JST, so the game's GMT reads local). native_play sets it in a window run and writes it into its pad recordings, so a
// replay reads the same clock.
struct RtcBase {
    std::int64_t unix = 0;        // seconds since 1970 (UTC) at VBlank 0
    std::int32_t utc_offset = 0;  // the player's local offset, seconds east of UTC
    bool operator==(const RtcBase&) const = default;
};
bool parse_rtc(const char* text, RtcBase& out);           // "<unix>,<offset>"; false (out untouched) for anything else
ClockBcd rtc_bcd(const RtcBase& base, std::uint64_t vblanks);   // the reply at that VBlank
ClockBcd rtc_now(std::uint64_t vblanks);                  // $B3NATIVE_RTC's (read once), else kDefaultClock

// The card in the slot: page reads, page writes and block erases on its image. Writes stay in the image (ProfileCard in
// save_store.h writes the profile back to its .sav).
class SaveCard {
public:
    static constexpr std::uint32_t kProbeAbsent = 0, kProbeSameCard = 1, kProbeNewCard = 2;

    // The clock comes from $B3NATIVE_CARD_CLOCK, else kDefaultClock; $B3NATIVE_CARD_FILE_TIME (Unix seconds), when
    // set, replaces the profile folder's file times (load_profile_slot's fixed_file_time). Malformed: runtime_error.
    explicit SaveCard(std::filesystem::path saves_root);

    // Inserts profile folder `slot` as a freshly formatted card holding its save directories; the next probe reports a
    // new card. On failure the slot is empty (and *error says why).
    bool attach(int slot, std::string* error = nullptr);
    // The same with the directories given (save_store.h: a profile's .sav); `slot` is only reported.
    bool attach_directories(int slot, const std::vector<CardSaveDirectory>& directories, std::string* error = nullptr);
    void detach();
    std::optional<int> attached() const { return slot_; }
    std::optional<std::int64_t> fixed_file_time() const { return file_time_; }

    // libmc2's card commands: probe (4), read pages (1), write pages (2), erase a block (9), the clock (10).
    std::uint32_t probe();
    bool read_pages(std::uint32_t first_page, std::uint32_t page_count, std::uint8_t* out, std::size_t out_size);
    bool write_pages(std::uint32_t first_page, const std::uint8_t* data, std::size_t size);
    bool erase_block(std::uint32_t block);
    ClockBcd clock_bcd() const { return clock_; }
    bool clock_fixed() const { return clock_fixed_; }   // $B3NATIVE_CARD_CLOCK given (else the RTC answers, rtc_now)

    // The game wrote since attach; writes() counts page writes and block erases (save_store.h's ProfileCard flushes
    // the image into the profile's .sav once it stops changing).
    bool dirty() const { return dirty_; }
    std::uint64_t writes() const { return stats_.page_writes + stats_.block_erases; }
    const std::vector<std::uint8_t>& image() const { return image_; }
    const std::filesystem::path& saves_root() const { return saves_root_; }
    struct Statistics {
        std::uint64_t probes = 0, page_reads = 0, page_writes = 0, block_erases = 0;
    };
    const Statistics& statistics() const { return stats_; }

private:
    void detach_quietly();

    std::filesystem::path saves_root_;
    std::optional<int> slot_;
    std::vector<std::uint8_t> image_;
    bool new_card_pending_ = false;
    bool dirty_ = false;
    ClockBcd clock_ = kDefaultClock;
    bool clock_fixed_ = false;
    std::optional<std::int64_t> file_time_;
    Statistics stats_;
};

}  // namespace b3native::save

#endif
