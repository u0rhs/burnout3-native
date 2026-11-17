// The game's stats read back from its save (src/native/save_stats.h) and the local achievements worked out from them.
// The synthetic tests build a save here (no game data); the last one reads the kit's 100% profile when it is there:
// $B3_SAVES_ROOT (the Makefile passes local.env's SAVES; skipped without it; nothing is written there).
#include "save_stats.h"
#include "save_store.h"
#include "test.h"

#include <cstdlib>
#include <cstring>

using namespace b3native::save;
namespace fs = std::filesystem;

namespace {

void put32(Bytes& b, std::size_t o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[o + i] = std::uint8_t(v >> (8 * i));
}
void putf(Bytes& b, std::size_t o, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(b, o, v);
}
void seal(Bytes& b) {
    std::uint32_t sum = 0;
    for (std::size_t o = 0; o < kChecksumOffset; o += 4)
        sum += std::uint32_t{b[o]} | std::uint32_t{b[o + 1]} << 8 | std::uint32_t{b[o + 2]} << 16 | std::uint32_t{b[o + 3]} << 24;
    put32(b, kChecksumOffset, sum);
}

// A save as a new game leaves it (three slots, the first in use, medals -1), with profile 0 then filled in by `fill`.
Bytes synthetic_save() {
    Bytes b(kGameSaveBytes, 0);
    put32(b, 0, kGameSaveVersion);
    for (int i = 0; i < kProfilesPerSave; ++i) {
        const std::size_t p = kProfilesOffset + std::size_t(i) * kProfileBytes;
        const char16_t* name = u"RACER";
        for (int k = 0; name[k]; ++k) b[p + 8 + 2 * k] = std::uint8_t(name[k]);
        std::memset(&b[p + 0x386], 0xFF, kRaceEvents + kCrashEvents);
        b[p + 0x49B] = i == 0 ? 0 : 1;
    }
    seal(b);
    return b;
}

TEST(new_profile_is_empty) {
    const Bytes b = synthetic_save();
    std::string err;
    const auto s = decode_game_save(b.data(), b.size(), &err);
    CHECK(s.has_value());
    if (!s) return;
    CHECK(s->size() == 1);   // the two empty slots are left out
    const ProfileStats& p = s->front();
    CHECK(p.index == 0);
    CHECK(p.name == "RACER");
    CHECK(p.all().medals() == 0);
    CHECK(p.takedowns() == 0);
    CHECK(p.signature_takedowns == 0 && p.headlines == 0);
    int unlocked = 0;
    for (const Achievement& a : achievements(p)) unlocked += a.unlocked();
    CHECK(unlocked == 0);
}

TEST(fields_decode) {
    Bytes b = synthetic_save();
    const std::size_t p = kProfilesOffset + kProfileBytes;   // the second slot, now in use
    b[p + 0x49B] = 0;
    b[p + 8] = 0xE9; b[p + 9] = 0x00;   // U+00E9 then "x"
    b[p + 10] = 'x'; b[p + 11] = 0;
    b[p + 12] = 0; b[p + 13] = 0;
    put32(b, p + 0x40, 123456);
    put32(b, p + 0x44, 99000000);
    put32(b, p + 0x48 + 4 * 3, 5);
    put32(b, p + 0x48 + 4 * 66, 2);
    for (int k = 0; k < 7; ++k) put32(b, p + 0x154 + 4 * k, 20);   // 140 takedowns, 15 of them online
    put32(b, p + 0x200, 15);
    put32(b, p + 0x204, 300);
    put32(b, p + 0x208, 7);
    putf(b, p + 0x20C + 4 * 5, 812.5f);
    putf(b, p + 0x20C + 4 * 35, 1200.25f);
    putf(b, p + 0x2A0 + 4 * 0, 251.0f);
    put32(b, p + 0x334, 1);
    put32(b, p + 0x338, 2);
    put32(b, p + 0x33C, 40);
    put32(b, p + 0x340, 60);
    b[p + 0x386] = 3; b[p + 0x386 + 1] = 2; b[p + 0x386 + 72] = 1; b[p + 0x386 + 10] = 0;
    b[p + 0x3CF] = 3; b[p + 0x3CF + 99] = 3;
    for (int k = 0; k < kSignatureTakedowns; ++k) b[p + 0x433 + k] = 1;
    b[p + 0x447 + 4] = 1;
    seal(b);
    const auto s = decode_game_save(b.data(), b.size());
    CHECK(s && s->size() == 2);
    if (!s || s->size() != 2) return;
    const ProfileStats& q = (*s)[1];
    CHECK(q.index == 1);
    CHECK(q.name == "\xC3\xA9x");
    CHECK(q.burnout_points == 123456);
    CHECK(q.crash_damage == 99000000);
    CHECK(q.cars_driven == 2 && q.car_events == 7);
    CHECK(q.takedowns_offline == 125 && q.takedowns_online == 15 && q.takedowns() == 140);
    CHECK(q.slams_offline == 300 && q.slams_online == 7);
    CHECK_NEAR(q.longest_oncoming_m, 1200.25, 1e-3);
    CHECK_NEAR(q.longest_drift_m, 251.0, 1e-3);
    CHECK(q.race_events_offline == 40 && q.race_events_online == 1);
    CHECK(q.crash_events_offline == 60 && q.crash_events_online == 2);
    CHECK(q.race.gold == 1 && q.race.silver == 1 && q.race.bronze == 1);
    CHECK(q.crash.gold == 2 && q.crash.medals() == 2);
    CHECK(q.signature_takedowns == 20 && q.headlines == 1);
    for (const Achievement& a : achievements(q)) {
        const std::string id = a.id;
        if (id == "first_gold" || id == "signature" || id == "takedowns_100" || id == "oncoming" || id == "drift") CHECK(a.unlocked());
        else CHECK(!a.unlocked());
        if (id == "tour_medals") CHECK(a.progress == 5 && a.target == 173);
        if (id == "crash_damage") CHECK(a.progress == 99000000);
    }
    const std::string text = format_stats(*s);
    CHECK(text.find("profile 2:") != std::string::npos);
    CHECK(text.find("99,000,000") != std::string::npos);
}

TEST(rejects_other_files) {
    Bytes b = synthetic_save();
    std::string err;
    CHECK(!decode_game_save(b.data(), b.size() - 4, &err));
    CHECK(err.find("bytes") != std::string::npos);
    b[0x100] ^= 1;   // a changed byte: the checksum no longer matches
    CHECK(!decode_game_save(b.data(), b.size(), &err));
    CHECK(err.find("checksum") != std::string::npos);
    b[0x100] ^= 1;
    put32(b, 0, 0x11);
    seal(b);
    CHECK(!decode_game_save(b.data(), b.size(), &err));
}

TEST(from_card_directories) {
    CardSaveDirectory d;
    d.name = kGameSaveDirectory;
    CardFile f;
    f.name = kGameSaveDirectory;
    f.content = synthetic_save();
    d.files.push_back(f);
    CardSaveDirectory other;
    other.name = "BASLUS-99999";
    const auto s = decode_game_save(Directories{other, d});
    CHECK(s && s->size() == 1);
    std::string err;
    CHECK(!decode_game_save(Directories{other}, &err));
}

TEST(group_digits_) {
    CHECK(group_digits(0) == "0");
    CHECK(group_digits(999) == "999");
    CHECK(group_digits(1000) == "1,000");
    CHECK(group_digits(142084904) == "142,084,904");
}

// The kit's 100% profile (not in the repo): everything done.
TEST(complete_profile) {
    const char* r = std::getenv("B3_SAVES_ROOT");
    const fs::path root = r && *r ? fs::path(r) : fs::path();
    std::error_code ec;
    if (!fs::exists(profile_file(root, 1), ec) && !fs::is_directory(legacy_profile_folder(root, 1), ec)) {
        std::printf("  (skipped: no 100%% profile at %s)\n", root.string().c_str());
        return;
    }
    std::string err;
    const auto loaded = load_profile(root, 1, &err);
    CHECK(loaded.has_value());
    if (!loaded) return;
    const auto s = decode_game_save(loaded->dirs, &err);
    CHECK(s.has_value());
    if (!s) { std::printf("  %s\n", err.c_str()); return; }
    CHECK(s->size() == 1);
    if (s->empty()) return;
    const ProfileStats& p = s->front();
    CHECK(p.race.gold == kRaceEvents);
    CHECK(p.crash.gold == kCrashEvents);
    CHECK(p.signature_takedowns == kSignatureTakedowns);
    CHECK(p.headlines == kHeadlines);
    CHECK(p.takedowns() > 1000);
    CHECK(p.crash_damage > 100000000u);
    CHECK(p.longest_oncoming_m > 0 && p.longest_drift_m > 0);
    int unlocked = 0;
    const auto list = achievements(p);
    for (const Achievement& a : list) unlocked += a.unlocked();
    std::printf("  %s: %d / %zu achievements, %s takedowns, %d cars driven\n", p.name.c_str(), unlocked, list.size(),
                group_digits(p.takedowns()).c_str(), p.cars_driven);
    for (const Achievement& a : list)
        if (std::string(a.id) != "cars") CHECK(a.unlocked());
}

}  // namespace

TEST_MAIN("test_save_stats: the game's stats from its save, local achievements")
