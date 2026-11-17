// The native memory card (src/native/save_card.cpp): the card image of a profile slot read back through its own FAT
// and directory entries (an independent reader here), the device's page operations, the times, the profile folders.
//
// Profile: $B3_SAVES_ROOT (the Makefile passes local.env's SAVES: a 100% profile card; its tests skip
// without it). Nothing is written there. $B3_SAVE_CARD_DUMP=<file> writes slot 1's image (for byte comparisons).
#include "save_card.h"
#include "test.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>

#include <unistd.h>

using namespace b3native::save;
namespace fs = std::filesystem;

namespace {

fs::path saves_root() {
    const char* r = std::getenv("B3_SAVES_ROOT");
    return r && *r ? fs::path(r) : fs::path();
}
bool have_profile() {
    std::error_code ec;
    return fs::is_directory(saves_root() / "profile-1", ec);
}

std::uint16_t r16(const std::vector<std::uint8_t>& b, std::size_t o) { return static_cast<std::uint16_t>(b[o] | b[o + 1] << 8); }
std::uint32_t r32(const std::vector<std::uint8_t>& b, std::size_t o) {
    return std::uint32_t{b[o]} | std::uint32_t{b[o + 1]} << 8 | std::uint32_t{b[o + 2]} << 16 | std::uint32_t{b[o + 3]} << 24;
}
CardTimestamp rtime(const std::vector<std::uint8_t>& b, std::size_t o) {
    CardTimestamp t;
    t.second = b[o + 1];
    t.minute = b[o + 2];
    t.hour = b[o + 3];
    t.day = b[o + 4];
    t.month = b[o + 5];
    t.year = r16(b, o + 6);
    return t;
}

// An independent reader of the PS2 card filesystem, from the superblock's own fields.
struct Reader {
    const std::vector<std::uint8_t>& img;
    std::uint32_t alloc_off, alloc_end, ifat0;
    std::set<std::uint32_t> used;   // allocatable clusters seen in chains
    bool cycle = false;
    explicit Reader(const std::vector<std::uint8_t>& i) : img(i), alloc_off(r32(i, 52)), alloc_end(r32(i, 56)), ifat0(r32(i, 80)) {}
    std::uint32_t fat(std::uint32_t c) const {
        const std::uint32_t fat_cluster = r32(img, std::size_t{ifat0} * kClusterSize + 4 * (c / 256));
        return r32(img, std::size_t{fat_cluster} * kClusterSize + 4 * (c % 256));
    }
    // The chain from `first`, stopping at end of chain.
    std::vector<std::uint32_t> chain(std::uint32_t first) {
        std::vector<std::uint32_t> out;
        for (std::uint32_t c = first;;) {
            if (c >= alloc_end || out.size() > alloc_end) { cycle = true; break; }
            if (!used.insert(c).second) cycle = true;
            out.push_back(c);
            const std::uint32_t n = fat(c);
            if (n == 0xFFFFFFFFu) break;
            if (!(n & 0x80000000u)) { cycle = true; break; }
            c = n & 0x7FFFFFFFu;
        }
        return out;
    }
    std::vector<std::uint8_t> bytes(const std::vector<std::uint32_t>& ch, std::size_t len) const {
        std::vector<std::uint8_t> out;
        for (std::uint32_t c : ch) {
            const std::size_t o = std::size_t{c + alloc_off} * kClusterSize;
            out.insert(out.end(), img.begin() + o, img.begin() + o + kClusterSize);
        }
        out.resize(std::min(out.size(), len));
        return out;
    }
};

std::string entry_name(const std::vector<std::uint8_t>& e) {
    std::string n;
    for (std::size_t i = 64; i < 96 && e[i]; ++i) n.push_back(static_cast<char>(e[i]));
    return n;
}

std::vector<std::uint8_t> file_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_text(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary);
    out << s;
}

// A scratch saves root under the temp directory (small files only).
struct TempRoot {
    fs::path root;
    explicit TempRoot(const char* tag) {
        root = fs::temp_directory_path() / (std::string("b3_save_card_") + tag + "_" + std::to_string(::getpid()));
        fs::remove_all(root);
        fs::create_directories(root / "profile-1" / "BASLUS-21050");
    }
    ~TempRoot() { std::error_code ec; fs::remove_all(root, ec); }
    fs::path save() const { return root / "profile-1" / "BASLUS-21050"; }
};

}  // namespace

TEST(timestamps) {
    const CardTimestamp t0 = card_timestamp_from_unix(0);
    CHECK(t0.year == 1970 && t0.month == 1 && t0.day == 1 && t0.hour == 9 && t0.minute == 0 && t0.second == 0);
    const CardTimestamp t = card_timestamp_from_unix(1173398683);   // 2007-03-09 00:04:43 UTC
    CHECK(t.year == 2007 && t.month == 3 && t.day == 9 && t.hour == 9 && t.minute == 4 && t.second == 43);
    const CardTimestamp n = card_timestamp_from_unix(-1);           // 1970-01-01 08:59:59 JST
    CHECK(n.year == 1970 && n.month == 1 && n.day == 1 && n.hour == 8 && n.minute == 59 && n.second == 59);
    for (std::int64_t u : {std::int64_t{0}, std::int64_t{951782400}, std::int64_t{1173398683}, std::int64_t{1793762859},
                           std::int64_t{4102444799}})
        CHECK(unix_from_card_timestamp(card_timestamp_from_unix(u)) == u);
    CardTimestamp z{2004, 0, 0, 0, 0, 0};   // zero month / day read as 1
    CHECK(unix_from_card_timestamp(z) == unix_from_card_timestamp(CardTimestamp{2004, 1, 1, 0, 0, 0}));
}

TEST(rtc_player_clock) {
    // mac5: the player's clock as the PS2's RTC (local + 9 h, the game's GMT then reads local)
    RtcBase b;
    CHECK(parse_rtc("1794220200,39600", b) && b.unix == 1794220200 && b.utc_offset == 39600);   // 2026-11-09 10:30 UTC, +11 h
    RtcBase keep = b;
    CHECK(!parse_rtc("1794220200", keep) && keep == b);
    CHECK(!parse_rtc("x,0", keep) && !parse_rtc("1,999999", keep) && !parse_rtc(nullptr, keep));
    CHECK(parse_rtc("1794220200,-18000", keep) && keep.utc_offset == -18000);
    // local 21:30:00 on 2026-11-09 -> the RTC (JST) 06:30:00 on 2026-11-10
    CHECK((rtc_bcd(b, 0) == ClockBcd{0x00, 0x00, 0x30, 0x06, 0x00, 0x10, 0x11, 0x26}));
    // 60000 VBlanks = 1001 s later: 06:46:41
    CHECK((rtc_bcd(b, 60000) == ClockBcd{0x00, 0x41, 0x46, 0x06, 0x00, 0x10, 0x11, 0x26}));
    // New Year's Eve across the year end, and a leap day
    RtcBase ny{1798761599 - 9 * 3600, 0};   // 2026-12-31 23:59:59 local (UTC +0)
    CHECK((rtc_bcd(ny, 0) == ClockBcd{0x00, 0x59, 0x59, 0x23, 0x00, 0x31, 0x12, 0x26}));
    RtcBase leap{1709208000 - 9 * 3600, 0};   // 2024-02-29 12:00:00
    CHECK((rtc_bcd(leap, 0) == ClockBcd{0x00, 0x00, 0x00, 0x12, 0x00, 0x29, 0x02, 0x24}));
    // without $B3NATIVE_RTC: the fixed reply (tests and proofs)
    CHECK(rtc_now(12345) == kDefaultClock);
}

TEST(clock_parse) {
    ClockBcd c{};
    CHECK(parse_clock("0000001200070904", c) && c == kDefaultClock);
    CHECK(parse_clock("00305923003112ff", c) && c[1] == 0x30 && c[3] == 0x23 && c[7] == 0xFF);
    ClockBcd keep = kDefaultClock;
    CHECK(!parse_clock("000000120007090", keep) && keep == kDefaultClock);
    CHECK(!parse_clock("00000012000709g4", keep) && keep == kDefaultClock);
    CHECK(!parse_clock(nullptr, keep));
    unsetenv("B3NATIVE_CARD_CLOCK");
    CHECK(SaveCard("/nonexistent").clock_bcd() == kDefaultClock);
    setenv("B3NATIVE_CARD_CLOCK", "0001020300040506", 1);
    CHECK((SaveCard("/nonexistent").clock_bcd() == ClockBcd{0, 1, 2, 3, 0, 4, 5, 6}));
    setenv("B3NATIVE_CARD_CLOCK", "bad", 1);
    CHECK_THROWS(SaveCard("/nonexistent"));
    unsetenv("B3NATIVE_CARD_CLOCK");
}

TEST(active_slot) {
    TempRoot t("active");
    CHECK(read_active_slot(t.root) == 1);   // no file
    const std::pair<const char*, int> cases[] = {{"3\n", 3}, {"  5", 5}, {"2x", 2}, {"+4", 4}, {"6", 1}, {"0", 1},
                                                 {"-1", 1}, {"x", 1}, {"", 1}, {"99999999999999999999999", 1}};
    for (const auto& [text, want] : cases) {
        write_text(t.root / "active-slot.txt", text);
        CHECK(read_active_slot(t.root) == want);
    }
}

TEST(empty_card) {
    const std::vector<std::uint8_t> img = build_card_image({});
    CHECK(img.size() == kImageSize);
    CHECK(std::memcmp(img.data(), "Sony PS2 Memory Card Format 1.2.0.0", 35) == 0);
    CHECK(r32(img, 41 * kClusterSize + 4) == 2);   // root "." counts "." and ".."
    const CardTimestamp t = rtime(img, 41 * kClusterSize + 8);
    CHECK(t == CardTimestamp{});                   // 2000-01-01 00:00:00
    CHECK(r32(img, 9 * kClusterSize) == 0xFFFFFFFFu && r32(img, 9 * kClusterSize + 4) == 0x7FFFFFFFu);
    // A slot without a folder is an empty card, not an error.
    TempRoot tr("emptyslot");
    std::string err;
    const auto dirs = load_profile_slot(tr.root, 2, &err);
    CHECK(dirs && dirs->empty());
    CHECK(!load_profile_slot(tr.root, 0, &err) && !load_profile_slot(tr.root, 6, &err));
    CardSaveDirectory bad;
    bad.name = std::string(32, 'x');
    CHECK(build_card_image({bad}, &err).empty() && !err.empty());
}

// PCSX2's _pcsx2_index is not read: the files come icon.sys first, then in name order, timed by the files themselves
// (a fixed file time replaces them all).
TEST(index_not_read) {
    TempRoot t("index");
    write_text(t.save() / "icon.sys", "i");
    write_text(t.save() / "BASLUS-21050", "data");
    write_text(t.save() / "view.ico", "v");
    write_text(t.save() / "old.writing", "w");
    const auto mtime = [&](const char* n) {
        return card_timestamp_from_unix(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::file_clock::to_sys(fs::last_write_time(t.save() / n)).time_since_epoch()).count());
    };
    for (const char* index : {"{$ROOT: {timeCreated: 1173398683,timeModified: 1173398682},icon.sys: {order: 3},"
                              "view.ico: {order: 1,timeCreated: 1173398683},BASLUS-21050: {order: 2}}",
                              "$ROOT:\n  timeCreated: 1173398683\nview.ico:\n  order: 1\n  timeCreated: 5\n"}) {
        write_text(t.save() / "_pcsx2_index", index);
        auto dirs = load_profile_slot(t.root, 1);
        CHECK(dirs && dirs->size() == 1 && (*dirs)[0].files.size() == 3);
        if (dirs && dirs->size() == 1 && (*dirs)[0].files.size() == 3) {
            const auto& f = (*dirs)[0].files;
            CHECK(f[0].name == "icon.sys" && f[1].name == "BASLUS-21050" && f[2].name == "view.ico");
            CHECK(f[0].created == mtime("icon.sys") && f[1].modified == mtime("BASLUS-21050") && f[2].created == mtime("view.ico"));
            CHECK((*dirs)[0].created.year != 2007);
        }
    }
    const auto dirs = load_profile_slot(t.root, 1, nullptr, 1793762859);
    CHECK(dirs && (*dirs)[0].created == card_timestamp_from_unix(1793762859) &&
          (*dirs)[0].files[0].modified == card_timestamp_from_unix(1793762859));
}

TEST(profile_image) {
    if (!have_profile()) {
        std::printf("  (skip: no %s)\n", (saves_root() / "profile-1").c_str());
        return;
    }
    std::string err;
    const auto dirs = load_profile_slot(saves_root(), read_active_slot(saves_root()), &err);
    CHECK(dirs.has_value());
    if (!dirs) return;
    const std::vector<std::uint8_t> img = build_card_image(*dirs, &err);
    CHECK(img.size() == kImageSize);
    if (img.size() != kImageSize) return;
    if (const char* dump = std::getenv("B3_SAVE_CARD_DUMP"); dump && *dump) {
        // Through the device, as attached at start-up (honours B3NATIVE_CARD_FILE_TIME).
        SaveCard card(saves_root());
        CHECK(card.attach(read_active_slot(saves_root())));
        std::ofstream out(dump, std::ios::binary);
        out.write(reinterpret_cast<const char*>(card.image().data()), static_cast<std::streamsize>(card.image().size()));
        std::printf("  wrote %s (slot %d, file time %s)\n", dump, card.attached().value_or(0),
                    card.fixed_file_time() ? std::to_string(*card.fixed_file_time()).c_str() : "the files' own");
    }

    // Superblock.
    CHECK(std::memcmp(img.data(), "Sony PS2 Memory Card Format 1.2.0.0", 35) == 0);
    CHECK(r16(img, 40) == 512 && r16(img, 42) == 2 && r16(img, 44) == 16 && r32(img, 48) == 8192);
    CHECK(r32(img, 52) == 41 && r32(img, 56) == 8135 && r32(img, 60) == 0 && r32(img, 64) == 1023 && r32(img, 68) == 1022);
    CHECK(r32(img, 80) == 8 && r32(img, 84) == 0 && r32(img, 208) == 0xFFFFFFFFu && img[336] == 2 && img[337] == 0x52);
    for (std::uint32_t i = 0; i < 32; ++i) CHECK(r32(img, 8 * kClusterSize + 4 * i) == 9 + i);
    // Backup blocks 1022 / 1023 erased.
    bool erased = true;
    for (std::size_t o = 1022 * kPagesPerBlock * kPageSize; o < kImageSize; ++o) erased = erased && img[o] == 0xFF;
    CHECK(erased);

    // Root directory and every save directory / file, through the FAT.
    Reader rd(img);
    const auto root_chain = rd.chain(r32(img, 60));
    std::vector<std::uint8_t> root = rd.bytes(root_chain, root_chain.size() * kClusterSize);
    const std::uint32_t root_n = r32(root, 4);
    CHECK(root_n == dirs->size() + 2);
    CHECK(root_chain.size() == (root_n + 1) / 2);
    CHECK(entry_name(std::vector<std::uint8_t>(root.begin(), root.begin() + 512)) == ".");
    CHECK(r16(root, 512) == 0xA426);
    std::size_t files_checked = 0;
    for (std::size_t d = 0; d < dirs->size() && (d + 2) * 512 < root.size(); ++d) {
        const std::vector<std::uint8_t> e(root.begin() + (d + 2) * 512, root.begin() + (d + 3) * 512);
        const CardSaveDirectory& want = (*dirs)[d];
        CHECK(entry_name(e) == want.name && r16(e, 0) == 0x8427);
        CHECK(rtime(e, 8) == want.created && rtime(e, 24) == want.modified);
        const std::uint32_t n = r32(e, 4);
        CHECK(n == want.files.size() + 2);
        const auto ch = rd.chain(r32(e, 16));
        CHECK(ch.size() == (n + 1) / 2);
        const std::vector<std::uint8_t> ents = rd.bytes(ch, ch.size() * kClusterSize);
        CHECK(entry_name(std::vector<std::uint8_t>(ents.begin(), ents.begin() + 512)) == "." && r32(ents, 20) == d + 2);
        for (std::size_t i = 0; i < want.files.size() && (i + 3) * 512 <= ents.size(); ++i) {
            const std::vector<std::uint8_t> fe(ents.begin() + (i + 2) * 512, ents.begin() + (i + 3) * 512);
            const CardFile& wf = want.files[i];
            CHECK(entry_name(fe) == wf.name && r16(fe, 0) == 0x8497 && r32(fe, 4) == wf.content.size());
            const bool empty = r32(fe, 4) == 0;   // an empty file has no clusters: first cluster 0xFFFFFFFF
            CHECK(!empty || r32(fe, 16) == 0xFFFFFFFFu);
            const auto fch = empty ? std::vector<std::uint32_t>{} : rd.chain(r32(fe, 16));
            CHECK(fch.size() == (wf.content.size() + kClusterSize - 1) / kClusterSize);
            CHECK(rd.bytes(fch, r32(fe, 4)) == wf.content);
            // ... and equal to the file on disk (stored as "<name>.ps2" when that exists).
            const fs::path on_disk = saves_root() / "profile-1" / want.name / wf.name;
            CHECK(file_bytes(fs::exists(fs::path(on_disk) += ".ps2") ? fs::path(on_disk) += ".ps2" : on_disk) == wf.content);
            ++files_checked;
        }
    }
    CHECK(!rd.cycle);
    // Every allocated FAT word lies in some chain; the rest are free.
    std::size_t allocated = 0;
    for (std::uint32_t c = 0; c < 8135; ++c) allocated += rd.fat(c) != 0x7FFFFFFFu;
    CHECK(allocated == rd.used.size());
    for (std::uint32_t c = 8135; c < 8192; ++c) CHECK(rd.fat(c) == 0xFFFFFFFFu);
    std::printf("  %zu save directories, %zu files, %zu clusters\n", dirs->size(), files_checked, allocated);
}

TEST(device) {
    unsetenv("B3NATIVE_CARD_CLOCK");
    unsetenv("B3NATIVE_CARD_FILE_TIME");
    TempRoot t("device");
    write_text(t.save() / "icon.sys", std::string(964, 'i'));
    write_text(t.save() / "BASLUS-21050", std::string(3000, 'd'));
    SaveCard card(t.root);
    std::uint8_t page[16 * 512];
    CHECK(card.probe() == SaveCard::kProbeAbsent);
    CHECK(!card.read_pages(0, 1, page, sizeof page));
    CHECK(!card.attach(9));
    CHECK(!card.attached() && card.probe() == SaveCard::kProbeAbsent);
    CHECK(card.attach(1) && card.attached() == 1);
    CHECK(card.probe() == SaveCard::kProbeNewCard);
    CHECK(card.probe() == SaveCard::kProbeSameCard);
    CHECK(card.image() == build_card_image(*load_profile_slot(t.root, 1)));
    CHECK(card.read_pages(0, 16, page, sizeof page) && std::memcmp(page, card.image().data(), sizeof page) == 0);
    CHECK(card.read_pages(16383, 1, page, 512) && !card.read_pages(16383, 2, page, sizeof page));
    CHECK(!card.read_pages(16384, 1, page, sizeof page) && !card.read_pages(0, 2, page, 1023));
    CHECK(card.statistics().page_reads == 17);
    std::memset(page, 0x5A, 1024);
    CHECK(!card.dirty());
    CHECK(card.write_pages(100, page, 1024) && card.dirty());
    CHECK(card.image()[100 * 512] == 0x5A && card.image()[102 * 512 - 1] == 0x5A);
    CHECK(!card.write_pages(100, page, 1000) && !card.write_pages(16383, page, 1024) && !card.write_pages(16384, page, 512));
    CHECK(card.erase_block(6) && card.image()[100 * 512] == 0xFF && card.image()[96 * 512] == 0xFF);
    CHECK(!card.erase_block(1024));
    CHECK(card.statistics().page_writes == 2 && card.statistics().block_erases == 1);
    // Nothing reached the folder.
    CHECK(file_bytes(t.save() / "BASLUS-21050").size() == 3000 && !fs::exists(t.save() / "_pcsx2_index"));
    // Re-attach: a fresh image, a new card again.
    CHECK(card.attach(1) && !card.dirty() && card.probe() == SaveCard::kProbeNewCard);
    card.detach();
    CHECK(!card.attached() && card.probe() == SaveCard::kProbeAbsent);
    // Fixed file time from the environment.
    setenv("B3NATIVE_CARD_FILE_TIME", "1793762859", 1);
    SaveCard fixed(t.root);
    CHECK(fixed.fixed_file_time() == 1793762859 && fixed.attach(1));
    CHECK(fixed.image() == build_card_image(*load_profile_slot(t.root, 1, nullptr, 1793762859)));
    setenv("B3NATIVE_CARD_FILE_TIME", "12x", 1);
    CHECK_THROWS(SaveCard(t.root));
    unsetenv("B3NATIVE_CARD_FILE_TIME");
}

TEST_MAIN("test_save_card: the native memory card (src/native/save_card.cpp)")
