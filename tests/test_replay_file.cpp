// replay1 (remaster-features §5 "Replay files"): the replay file (src/native/replay_file.h) -- its round trip, the
// checksum, the refusals (another disc, another release, a newer format, damage), the recorder's and the player's ops,
// the folder's names and rotation. Synthetic replays only.
#include "replay_file.h"
#include "test.h"

#include <cstdlib>
#include <fstream>

#include <unistd.h>

using namespace b3native::replay;
namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kSize = 4073816, kFnv = 0x790a7666929b62eaull;

Report neutral() {
    Report r{};
    r.fill(0xFF);
    for (int i = 4; i < 8; ++i) r[std::size_t(i)] = 0x80;   // sticks centred
    for (int i = 8; i < 20; ++i) r[std::size_t(i)] = 0;     // pressures
    return r;
}

b3native::save::CardSaveDirectory save_dir() {
    b3native::save::CardSaveDirectory d;
    d.name = b3native::save::kGameSaveDirectory;
    d.created = b3native::save::card_timestamp_from_unix(1173398683);
    d.modified = d.created;
    b3native::save::CardFile f;
    f.name = "BASLUS-21050";
    f.created = f.modified = d.created;
    for (int i = 0; i < 4000; ++i) f.content.push_back(std::uint8_t(i * 7));
    d.files.push_back(f);
    return d;
}

Replay sample_replay() {
    Recorder rec(neutral());
    Replay& r = rec.replay();
    r.vblanks = 23400;
    r.started = 1794500000;
    r.utc_offset = 3600;
    r.version = "0.1.0-test+1a2b3c4d";
    r.program_size = kSize;
    r.program_fnv = kFnv;
    r.info = {{"recorded", "2026-11-12 21:30:05"}, {"host", "linux"}};
    r.options = {{"idle_skip", "1"}, {"movie_skip", "1"}, {"env.B3NATIVE_RTC", "1794500000,3600"}};
    r.card = true;
    r.card_slot = 2;
    r.card_sav = b3native::save::write_sav({save_dir()});
    rec.connect(0, 1, true);   // port 2 before the boot
    Report s = neutral();
    rec.sample(1, 0, nullptr);   // no report yet: nothing recorded
    rec.sample(5, 0, s.data());
    rec.sample(6, 0, s.data());   // the same: nothing
    s[0] = 0xF7;                  // Start
    rec.sample(9000, 0, s.data());
    s[0] = 0xFF;
    s[5] = 0x10;                  // a stick
    rec.sample(9010, 0, s.data());
    rec.sample(9011, 0, nullptr);   // the window gave none
    rec.sample(9012, 0, s.data());
    rec.sample(100, 1, neutral().data());
    rec.connect(200, 1, false);
    rec.sample(200, 1, neutral().data());
    rec.sample(201, 1, nullptr);
    rec.text(15000, "ANNA");
    rec.text(15001, "");
    rec.setting(0, "date_format", "0");
    rec.setting(0, "music", "0|");
    rec.setting(12000, "date_format", "1");
    rec.setting(12001, "date_format", "1");   // unchanged: nothing
    rec.check(600, 0x1234567890ABCDEFull);
    rec.check(1200, 42);
    // sort as native_play's single pass would have made them (by VBlank; this test fed them out of order)
    std::stable_sort(r.pads.begin(), r.pads.end(), [](const PadOp& a, const PadOp& b) { return a.vblank < b.vblank; });
    return r;
}

fs::path scratch(const char* name) {
    const char* tmp = std::getenv("TMPDIR");
    fs::path d = fs::path(tmp && *tmp ? tmp : "/tmp") / ("b3_test_replay_" + std::to_string(getpid())) / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

}  // namespace

TEST(round_trip) {
    const Replay r = sample_replay();
    CHECK(r.pads.size() == 9);
    CHECK(r.text.size() == 1);
    CHECK(r.settings.size() == 3);
    const Bytes b = encode(r);
    std::string err;
    const auto back = decode(b, &err);
    CHECK(back.has_value());
    if (!back) { std::printf("    %s\n", err.c_str()); return; }
    CHECK(*back == r);
    CHECK(back->option("movie_skip") && *back->option("movie_skip") == "1");
    CHECK(back->info_value("host") && *back->info_value("host") == "linux");
    CHECK(!back->option("nothing"));
    // the card reads back as the save it was
    const auto dirs = b3native::save::read_sav(back->card_sav);
    CHECK(dirs && dirs->size() == 1 && (*dirs)[0] == save_dir());
    // encoding is stable
    CHECK(encode(*back) == b);
}

TEST(empty_and_no_card) {
    Replay r;
    r.version = "0.1.0-test";
    const auto back = decode(encode(r));
    CHECK(back && *back == r && !back->card && back->pads.empty());
}

TEST(checksum_and_damage) {
    const Bytes good = encode(sample_replay());
    std::string err;
    // every single byte flipped is caught (the CRC, or the magic / format before it)
    int caught = 0;
    for (std::size_t i = 0; i < good.size(); i += 7) {
        Bytes b = good;
        b[i] ^= 0x01;
        if (!decode(b, &err)) ++caught;
    }
    CHECK(caught == int((good.size() + 6) / 7));
    {
        Bytes b = good;
        b[good.size() / 2] ^= 0x40;
        CHECK(!decode(b, &err) && err.find("checksum") != std::string::npos);
    }
    CHECK(!decode(Bytes(good.begin(), good.end() - 9), &err));   // cut short
    CHECK(!decode(Bytes{}, &err) && err == "is not a replay file");
    Bytes text(good);
    text[0] = 'X';
    CHECK(!decode(text, &err) && err == "is not a replay file");
}

TEST(newer_format_refused) {
    Bytes b = encode(sample_replay());
    b[8] = std::uint8_t(kFormat + 1);   // the format, then a fresh CRC: a well-formed file of a newer port
    const std::uint32_t c = b3native::save::crc32(b.data(), b.size() - 4);
    for (int i = 0; i < 4; ++i) b[b.size() - 4 + std::size_t(i)] = std::uint8_t(c >> (8 * i));
    std::string err;
    CHECK(!decode(b, &err));
    CHECK(err.find("newer version") != std::string::npos);
}

TEST(missing_section_refused) {
    // a file with only HEAD: well formed, but incomplete
    Bytes out(kMagic, kMagic + 8);
    detail::u32(out, kFormat);
    detail::u32(out, 1);
    detail::u32(out, detail::tag("HEAD"));
    Bytes head;
    detail::u64(head, 1);
    detail::u64(head, 2);
    detail::u32(head, 0);
    detail::str(head, "0.1.0");
    detail::u32(out, std::uint32_t(head.size()));
    out.insert(out.end(), head.begin(), head.end());
    detail::u32(out, b3native::save::crc32(out.data(), out.size()));
    std::string err;
    CHECK(!decode(out, &err));
    CHECK(err.find("incomplete") != std::string::npos);
}

TEST(unknown_section_skipped) {
    // a later port's extra section (before the CRC) is skipped by this reader
    const Replay r = sample_replay();
    Bytes b = encode(r);
    b.resize(b.size() - 4);
    b[12] = std::uint8_t(b[12] + 1);   // one more section
    detail::u32(b, detail::tag("XTRA"));
    detail::u32(b, 3);
    b.insert(b.end(), {1, 2, 3});
    detail::u32(b, b3native::save::crc32(b.data(), b.size()));
    const auto back = decode(b);
    CHECK(back && *back == r);
}

TEST(compatibility) {
    const Replay r = sample_replay();
    Host h{kSize, kFnv, "0.1.0-test+1a2b3c4d"};
    CHECK(compatible(r, h).verdict == Compat::kExact);
    CHECK(compatible(r, h).text.empty());
    h.version = "0.1.0-test+99999999";
    CHECK(compatible(r, h).verdict == Compat::kWarn);
    h.version = "0.2.0+1a2b3c4d";
    const Compat other = compatible(r, h);
    CHECK(other.verdict == Compat::kRefuse && other.text.find("version 0.1.0-test+1a2b3c4d") != std::string::npos);
    h.version = r.version;
    h.program_fnv ^= 1;
    const Compat disc = compatible(r, h);
    CHECK(disc.verdict == Compat::kRefuse && disc.text.find("another game disc") != std::string::npos);
    h.program_fnv = kFnv;
    h.program_size = 1;
    CHECK(compatible(r, h).verdict == Compat::kRefuse);
    Replay dirty = r;
    dirty.version = "0.1.0-test+1a2b3c4d-dirty";
    CHECK(compatible(dirty, Host{kSize, kFnv, dirty.version}).verdict == Compat::kWarn);
}

TEST(player_replays_the_ops) {
    const Replay r = sample_replay();
    Player p(r);
    auto plugs = p.step(0);   // before the boot: port 2 plugged in
    CHECK(plugs.size() == 1 && plugs[0].first == 1 && plugs[0].second);
    CHECK(!p.present(0));
    for (std::uint64_t v = 1; v < 5; ++v) { CHECK(p.step(v).empty()); CHECK(!p.present(0)); }
    p.step(5);
    CHECK(p.present(0) && p.report(0) == neutral());
    p.step(100);
    CHECK(p.present(1) && p.report(1) == neutral());
    plugs = p.step(200);   // pulled out: then neutral, once
    CHECK(plugs.size() == 1 && plugs[0].first == 1 && !plugs[0].second);
    CHECK(p.present(1));
    p.step(201);
    CHECK(!p.present(1));
    p.step(8999);
    CHECK(p.report(0)[0] == 0xFF);
    p.step(9000);
    CHECK(p.report(0)[0] == 0xF7);
    p.step(9010);
    CHECK(p.report(0)[0] == 0xFF && p.report(0)[5] == 0x10 && p.present(0));
    p.step(9011);
    CHECK(!p.present(0));
    p.step(9012);
    CHECK(p.present(0) && p.report(0)[5] == 0x10);
    CHECK(p.text(14999).empty());
    CHECK(p.text(15000) == "ANNA");
    CHECK(p.text(15001).empty());
    auto s0 = p.settings(0);
    CHECK(s0.size() == 2 && s0[0]->key == "date_format" && s0[0]->value == "0" && s0[1]->key == "music");
    CHECK(p.settings(11999).empty());
    auto s1 = p.settings(12000);
    CHECK(s1.size() == 1 && s1[0]->value == "1");
    CHECK(!p.check(599));
    CHECK(p.check(600) && *p.check(600) == 0x1234567890ABCDEFull);
    CHECK(!p.check(601));
    CHECK(p.check(1200) && *p.check(1200) == 42);
    CHECK(!p.check(1800));
}

TEST(file_names_and_rotation) {
    CHECK(automatic_name("20261112-213005.b3replay"));
    CHECK(automatic_name("20261112-213005-2.b3replay"));
    CHECK(!automatic_name("my best crash.b3replay"));
    CHECK(!automatic_name("20261112-213005.b3replay.tmp"));
    CHECK(!automatic_name("20261112x213005.b3replay"));
    CHECK(!automatic_name("20261112-213005-.b3replay"));
    const fs::path d = scratch("rotate");
    const Replay r = sample_replay();
    for (int k = 0; k < 25; ++k) {
        char n[64];
        std::snprintf(n, sizeof n, "202611%02d-120000.b3replay", k + 1);
        CHECK(write_file(d / n, r));
    }
    CHECK(write_file(d / "keep me.b3replay", r));
    CHECK(rotate(d, 20) == 5);
    CHECK(!fs::exists(d / "20261101-120000.b3replay"));
    CHECK(!fs::exists(d / "20261105-120000.b3replay"));
    CHECK(fs::exists(d / "20261106-120000.b3replay"));
    CHECK(fs::exists(d / "keep me.b3replay"));
    const fs::path a = new_file(d, 1794500000), b = new_file(d, 1794500000);
    CHECK(a == b);   // nothing written in between
    std::ofstream(a) << "x";
    const fs::path c = new_file(d, 1794500000);
    CHECK(c != a && c.filename().string().find("-2.b3replay") != std::string::npos);
    CHECK(automatic_name(c.filename().string()));
    // the listing: every .b3replay, newest first, a bad file with its error
    const auto l = list(d);
    CHECK(l.size() == 22);
    int bad = 0;
    for (const Entry& e : l) if (!e.replay) { ++bad; CHECK(e.error == "is not a replay file"); }
    CHECK(bad == 1);
    fs::remove_all(d.parent_path());
}

TEST(write_read_file) {
    const fs::path d = scratch("file");
    const Replay r = sample_replay();
    std::string err;
    CHECK(write_file(d / "a.b3replay", r, &err));
    CHECK(!fs::exists(d / "a.b3replay.tmp"));
    const auto back = read_file(d / "a.b3replay", &err);
    CHECK(back && *back == r);
    CHECK(!read_file(d / "missing.b3replay", &err) && err == "cannot be read");
    fs::remove_all(d.parent_path());
}

TEST(lengths_and_fingerprint) {
    CHECK(format_length(0) == "0:00");
    CHECK(format_length(60) == "0:01");
    CHECK(format_length(23400) == "6:30");
    CHECK(format_length(std::uint64_t(3723 * kVBlankHz) + 1) == "1:02:03");
    const auto f1 = fingerprint({{"a.mp3", 100}, {"b.mp3", 200}});
    CHECK(f1 == fingerprint({{"a.mp3", 100}, {"b.mp3", 200}}));
    CHECK(f1 != fingerprint({{"a.mp3", 100}, {"b.mp3", 201}}));
    CHECK(f1 != fingerprint({{"a.mp3", 100}}));
}

TEST_MAIN("replay files (src/native/replay_file.h)")
