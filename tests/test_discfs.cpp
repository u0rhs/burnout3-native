// test_discfs -- the disc's files from a folder or a disc image (include/b3asset/discfs.h, iso1). Synthetic ISO 9660
// images written here under build/ (no disc data): nested and mixed-case names, ";1" versions, a file without an
// extension, a directory over two sectors, a multi-extent file, a file past 4 GB in a sparse image, the same volume as
// raw 2352-byte sectors (.bin), concurrent reads; and the host folder path the extracted disc uses.
//
// Optional real check (not part of `make test`: it reads the whole disc): B3_TEST_ISO=<the retail .iso> and
// B3_TEST_ISO_DIR=<the extracted disc folder>: every file of the folder is in the image with the same bytes.
#include "b3asset/discfs.h"
#include "b3asset/language.h"   // 1.0.1: language packs on a disc folder
#include "test.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

namespace fs = std::filesystem;
namespace disc = b3asset::disc;

namespace {

// A folder on a case-sensitive disk (Linux) or not (macOS / Windows defaults: two names that differ only in case are
// one file there)
bool case_sensitive_dir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    { std::ofstream(dir / "cs_probe") << "1"; }
    const bool sensitive = !fs::exists(dir / "CS_PROBE", ec);
    fs::remove(dir / "cs_probe", ec);
    return sensitive;
}


const fs::path kDir = "build/test_discfs.d";

// the bytes of a test file: a pattern of its seed and offset (every byte position distinct enough)
std::uint8_t pat(std::uint32_t seed, std::uint64_t k) { return std::uint8_t((k * 131 + seed * 7 + (k >> 11) * 17 + (k >> 24)) & 0xFF); }
std::vector<std::uint8_t> pattern(std::uint32_t seed, std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t k = 0; k < n; ++k) v[k] = pat(seed, k);
    return v;
}

// ---- a minimal ISO 9660 writer (cooked 2048-byte sectors) ----
struct Iso {
    std::map<std::uint64_t, std::vector<std::uint8_t>> sectors;   // LBA -> 2048 bytes (the rest of the image: zeros)
    std::uint32_t next = 24;                                      // the next free LBA
    std::uint32_t alloc(std::uint64_t bytes) {
        const std::uint32_t at = next;
        next += std::uint32_t((bytes + 2047) / 2048);
        return at;
    }
    void put(std::uint64_t lba, const std::uint8_t* d, std::size_t n) {
        for (std::size_t o = 0; o < n; o += 2048) {
            auto& s = sectors[lba + o / 2048];
            s.resize(2048);
            std::memcpy(s.data(), d + o, std::min<std::size_t>(2048, n - o));
        }
    }
};
void le32(std::uint8_t* p, std::uint32_t v) { for (int k = 0; k < 4; ++k) p[k] = std::uint8_t(v >> (8 * k)); }
void be32(std::uint8_t* p, std::uint32_t v) { for (int k = 0; k < 4; ++k) p[3 - k] = std::uint8_t(v >> (8 * k)); }
std::vector<std::uint8_t> record(const std::string& name, std::uint32_t lba, std::uint32_t size, std::uint8_t flags) {
    const std::size_t nlen = name.size(), len = 33 + nlen + (nlen % 2 == 0 ? 1 : 0);
    std::vector<std::uint8_t> r(len, 0);
    r[0] = std::uint8_t(len);
    le32(&r[2], lba);
    be32(&r[6], lba);
    le32(&r[10], size);
    be32(&r[14], size);
    r[25] = flags;
    r[28] = 1;   // volume sequence number
    r[31] = 1;
    r[32] = std::uint8_t(nlen);
    std::memcpy(&r[33], name.data(), nlen);
    return r;
}
// a directory's extent from its records ("." and ".." first); records never cross a sector
std::vector<std::uint8_t> dir_bytes(const std::vector<std::vector<std::uint8_t>>& recs) {
    std::vector<std::uint8_t> d;
    for (const auto& r : recs) {
        if (d.size() % 2048 + r.size() > 2048) d.resize((d.size() / 2048 + 1) * 2048, 0);
        d.insert(d.end(), r.begin(), r.end());
    }
    d.resize((d.size() + 2047) / 2048 * 2048, 0);
    return d;
}

struct Spec {
    std::map<std::string, std::vector<std::uint8_t>> files;   // expected contents by path ("DATA/GLOBAL.TXD")
    std::uint64_t far_lba = 0;                                // the far file's LBA (sparse image), 0: none
};

// The test volume: / {SYSTEM.CNF, SLUS_210.50, Mixed.Txt, NOEXT., DATA/ (two sectors), TRACKS/EU/C1_V1/
// {STATIC.DAT, STREAMED.DAT}, MULTI.BIN (three extents)} (DATA: 52 files), and with `far` FAR.DAT at LBA 0x240000 (byte offset 4.5 GB)
Iso build(Spec& spec, bool far) {
    Iso iso;
    const std::uint32_t root = 18, data = 19 /* 2 sectors */, tracks = 21, eu = 22, c1 = 23;
    auto file = [&](const std::string& path, std::uint32_t seed, std::size_t n) {
        auto bytes = pattern(seed, n);
        const std::uint32_t lba = iso.alloc(n);
        iso.put(lba, bytes.data(), bytes.size());
        spec.files[path] = bytes;
        return std::pair<std::uint32_t, std::uint32_t>(lba, std::uint32_t(n));
    };
    const auto cnf = file("SYSTEM.CNF", 1, 60), elf = file("SLUS_210.50", 2, 70000), mixed = file("Mixed.Txt", 3, 5),
               noext = file("NOEXT", 4, 2048), st = file("TRACKS/EU/C1_V1/STATIC.DAT", 5, 3000),
               sm = file("TRACKS/EU/C1_V1/STREAMED.DAT", 6, 9000), empty = file("DATA/EMPTY.BIN", 7, 0);
    // a multi-extent file: three extents of 4096, 2048 and 1000 bytes, not contiguous
    std::vector<std::uint8_t> multi = pattern(8, 4096 + 2048 + 1000);
    const std::uint32_t m1 = iso.alloc(4096);
    iso.alloc(2048);   // a gap
    const std::uint32_t m2 = iso.alloc(2048), m3 = iso.alloc(1000);
    iso.put(m1, multi.data(), 4096);
    iso.put(m2, multi.data() + 4096, 2048);
    iso.put(m3, multi.data() + 6144, 1000);
    spec.files["MULTI.BIN"] = multi;
    std::vector<std::vector<std::uint8_t>> rr = {record(std::string(1, '\0'), root, 2048, 2), record(std::string(1, '\1'), root, 2048, 2),
        record("SYSTEM.CNF;1", cnf.first, cnf.second, 0), record("SLUS_210.50;1", elf.first, elf.second, 0),
        record("Mixed.Txt;1", mixed.first, mixed.second, 0), record("NOEXT.;1", noext.first, noext.second, 0),
        record("DATA", data, 4096, 2), record("TRACKS", tracks, 2048, 2),
        record("MULTI.BIN;1", m1, 4096, 0x80), record("MULTI.BIN;1", m2, 2048, 0x80), record("MULTI.BIN;1", m3, 1000, 0)};
    if (far) {
        spec.far_lba = 0x240000;
        auto bytes = pattern(9, 100000);
        iso.put(spec.far_lba, bytes.data(), bytes.size());
        spec.files["FAR.DAT"] = bytes;
        rr.push_back(record("FAR.DAT;1", std::uint32_t(spec.far_lba), 100000, 0));
    }
    std::vector<std::vector<std::uint8_t>> dr = {record(std::string(1, '\0'), data, 4096, 2), record(std::string(1, '\1'), root, 2048, 2),
                                                 record("EMPTY.BIN;1", empty.first, 0, 0)};
    for (int k = 0; k < 50; ++k) {   // long names: the directory needs two sectors
        char n[40];
        std::snprintf(n, sizeof n, "FILE_WITH_A_LONG_NAME_%02d.DAT", k);
        const auto f = file(std::string("DATA/") + n, 100 + k, 10 + k);
        dr.push_back(record(std::string(n) + ";1", f.first, f.second, 0));
    }
    const auto gt = file("DATA/GLOBAL.TXD", 10, 5000);
    dr.push_back(record("GLOBAL.TXD;1", gt.first, gt.second, 0));
    const auto rd = dir_bytes(rr), dd = dir_bytes(dr);
    CHECK(dd.size() == 4096);
    if (dd.size() != 4096) std::printf("  (DATA's directory is %zu bytes, not 2 sectors)\n", dd.size());
    iso.put(root, rd.data(), rd.size());
    iso.put(data, dd.data(), dd.size());
    const auto td = dir_bytes({record(std::string(1, '\0'), tracks, 2048, 2), record(std::string(1, '\1'), root, 2048, 2), record("EU", eu, 2048, 2)});
    const auto ed = dir_bytes({record(std::string(1, '\0'), eu, 2048, 2), record(std::string(1, '\1'), tracks, 2048, 2), record("C1_V1", c1, 2048, 2)});
    const auto cd = dir_bytes({record(std::string(1, '\0'), c1, 2048, 2), record(std::string(1, '\1'), eu, 2048, 2),
                               record("STATIC.DAT;1", st.first, st.second, 0), record("STREAMED.DAT;1", sm.first, sm.second, 0)});
    iso.put(tracks, td.data(), td.size());
    iso.put(eu, ed.data(), ed.size());
    iso.put(c1, cd.data(), cd.size());
    // the volume descriptors: the primary one (logical block size 2048, the root's record), then the terminator
    std::vector<std::uint8_t> pvd(2048, 0), term(2048, 0);
    pvd[0] = 1;
    std::memcpy(&pvd[1], "CD001", 5);
    pvd[6] = 1;
    std::memcpy(&pvd[40], "B3TEST", 6);
    le32(&pvd[80], far ? 0x240000 + 64 : iso.next);
    pvd[128] = 0x00; pvd[129] = 0x08; pvd[130] = 0x08; pvd[131] = 0x00;
    const auto rootrec = record(std::string(1, '\0'), root, 2048, 2);
    std::memcpy(&pvd[156], rootrec.data(), 34);
    term[0] = 255;
    std::memcpy(&term[1], "CD001", 5);
    term[6] = 1;
    iso.put(16, pvd.data(), 2048);
    iso.put(17, term.data(), 2048);
    return iso;
}

// the image as a file: cooked (2048) or raw (2352, mode 1 or mode 2 form 1); sparse where nothing is written
bool write_image(const Iso& iso, const fs::path& path, int raw_mode) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const std::uint64_t stride = raw_mode ? 2352 : 2048, off = raw_mode == 1 ? 16 : raw_mode == 2 ? 24 : 0;
    std::uint64_t last = 0;
    for (const auto& [lba, s] : iso.sectors) {
        f.seekp(std::streamoff(lba * stride));
        if (raw_mode) {
            std::uint8_t hdr[24] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
            hdr[15] = std::uint8_t(raw_mode);
            f.write(reinterpret_cast<const char*>(hdr), std::streamsize(off));
        }
        f.write(reinterpret_cast<const char*>(s.data()), 2048);
        if (raw_mode) {
            const std::vector<char> tail(stride - off - 2048, 0);   // EDC / ECC (not checked by readers)
            f.write(tail.data(), std::streamsize(tail.size()));
        }
        last = std::max(last, lba);
    }
    // the raw images need a sync at sector 16 only; pad the cooked image to its last sector
    return bool(f);
}

void check_volume(const fs::path& img, const Spec& spec, const char* what) {
    std::printf("  [%s] %s\n", what, img.string().c_str());
    CHECK(disc::is_image(img));
    CHECK(disc::is_dir(img));
    CHECK(disc::is_dir(img / "DATA"));
    CHECK(disc::is_dir(img / "data"));
    CHECK(disc::is_dir(img / "Tracks" / "eu" / "C1_v1"));
    CHECK(!disc::is_dir(img / "SYSTEM.CNF"));
    CHECK(disc::is_file(img / "SYSTEM.CNF"));
    CHECK(disc::is_file(img / "system.cnf"));
    CHECK(disc::exists(img / "SYSTEM.CNF;1") == false);   // ";1" is not part of the name
    CHECK(disc::is_file(img / "mixed.txt"));
    CHECK(disc::is_file(img / "NOEXT"));
    CHECK(!disc::exists(img / "NOPE"));
    CHECK(!disc::exists(img / "DATA" / "NOPE" / "X"));
    for (const auto& [path, bytes] : spec.files) {
        std::vector<std::uint8_t> got;
        const bool ok = disc::read_file(img / path, got);
        CHECK(ok);
        CHECK(got == bytes);
        CHECK(disc::file_size(img / path) == std::int64_t(bytes.size()));
        if (!ok || got != bytes) std::printf("    %s: %s, %zu bytes of %zu\n", path.c_str(), ok ? "differs" : "not read", got.size(), bytes.size());
    }
    CHECK(disc::file_size(img / "DATA") == -1);
    // a backslash below the image, as a Windows path
    std::vector<std::uint8_t> g;
    CHECK(disc::read_file(img.string() + "/data\\global.txd", g) && g == spec.files.at("DATA/GLOBAL.TXD"));
    // listings: the names as on the disc
    std::vector<std::string> names;
    for (const auto& e : disc::list_dir(img)) names.push_back(e.name + (e.dir ? "/" : ""));
    std::vector<std::string> want = {"SYSTEM.CNF", "SLUS_210.50", "Mixed.Txt", "NOEXT", "DATA/", "TRACKS/", "MULTI.BIN"};
    if (spec.far_lba) want.push_back("FAR.DAT");
    CHECK(names == want);
    CHECK(disc::list_dir(img / "data").size() == 52);
    CHECK(disc::list_dir(img / "tracks/eu/c1_v1").size() == 2);
    CHECK(disc::list_dir(img / "SYSTEM.CNF").empty());
    for (const auto& e : disc::list_dir(img / "tracks" / "eu")) CHECK(e.path == img / "tracks" / "eu" / "C1_V1");
    CHECK(disc::resolve_ci(img, "tracks/eu/c1_v1/static.dat") == img / "TRACKS" / "EU" / "C1_V1" / "STATIC.DAT");
    CHECK(disc::resolve_ci(img, "\\DATA\\Global.Txd") == img / "DATA" / "GLOBAL.TXD");
    CHECK(disc::resolve_ci(img, "DATA/../SYSTEM.CNF").empty());
    CHECK(disc::resolve_ci(img, "DATA/MISSING").empty());
    // File: reads at offsets, across the multi-extent file's extents, past the end
    disc::File f;
    CHECK(f.open(img / "multi.bin"));
    CHECK(f.size() == 7144);
    const auto& m = spec.files.at("MULTI.BIN");
    for (std::uint64_t at : {0ull, 1ull, 4095ull, 4096ull, 6000ull, 6143ull, 6144ull, 7143ull}) {
        std::uint8_t buf[3000];
        const std::int64_t n = f.read_at(at, buf, sizeof buf);
        const std::uint64_t want_n = std::min<std::uint64_t>(sizeof buf, 7144 - at);
        CHECK(n == std::int64_t(want_n));
        CHECK(n > 0 && std::memcmp(buf, m.data() + at, std::size_t(n)) == 0);
    }
    std::uint8_t one;
    CHECK(f.read_at(7144, &one, 1) == 0);
    CHECK(!disc::File().open(img / "DATA"));
    // concurrent readers on one File and on others (the game thread and the renderer's)
    std::atomic<int> bad{0};
    std::vector<std::thread> th;
    for (int t = 0; t < 8; ++t)
        th.emplace_back([&, t] {
            disc::File own;
            if (!own.open(img / "SLUS_210.50")) { ++bad; return; }
            const auto& e = spec.files.at("SLUS_210.50");
            std::uint64_t x = 0x9E3779B97F4A7C15ull * std::uint64_t(t + 1);
            for (int k = 0; k < 400; ++k) {
                x ^= x << 13; x ^= x >> 7; x ^= x << 17;
                const std::uint64_t at = x % e.size();
                std::uint8_t buf[5000];
                const disc::File& use = (k & 1) ? own : f;
                if (&use == &f) {
                    const std::int64_t n = f.read_at(at % 7144, buf, 1000);
                    if (n <= 0 || std::memcmp(buf, m.data() + at % 7144, std::size_t(n)) != 0) ++bad;
                } else {
                    const std::int64_t n = own.read_at(at, buf, sizeof buf);
                    if (n <= 0 || std::memcmp(buf, e.data() + at, std::size_t(n)) != 0) ++bad;
                }
            }
        });
    for (auto& x : th) x.join();
    CHECK(bad == 0);
}

}  // namespace

TEST(cooked_iso) {
    fs::create_directories(kDir);
    Spec spec;
    const Iso iso = build(spec, false);
    CHECK(write_image(iso, kDir / "small.iso", 0));
    check_volume(kDir / "small.iso", spec, "2048-byte sectors");
}

TEST(raw_bin_mode1_and_mode2) {
    Spec spec;
    const Iso iso = build(spec, false);
    CHECK(write_image(iso, kDir / "small_m1.bin", 1));
    check_volume(kDir / "small_m1.bin", spec, "2352-byte sectors, mode 1");
    CHECK(write_image(iso, kDir / "small_m2.bin", 2));
    check_volume(kDir / "small_m2.bin", spec, "2352-byte sectors, mode 2 form 1");
}

TEST(file_past_4gb) {
    Spec spec;
    const Iso iso = build(spec, true);
    const fs::path p = kDir / "sparse.iso";
    CHECK(write_image(iso, p, 0));
    std::error_code ec;
    CHECK(fs::file_size(p, ec) > (4ull << 30));
    check_volume(p, spec, "a file at 4.5 GB (sparse)");
    fs::remove(p, ec);
}

TEST(not_an_image) {
    const fs::path p = kDir / "plain.bin";
    { std::ofstream f(p, std::ios::binary); const auto b = pattern(3, 100000); f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size())); }
    CHECK(!disc::is_image(p));
    CHECK(!disc::is_dir(p));
    CHECK(disc::is_file(p));
    CHECK(!disc::exists(p / "SYSTEM.CNF"));
    CHECK(disc::list_dir(p).empty());
    std::vector<std::uint8_t> d;
    CHECK(disc::read_file(p, d) && d == pattern(3, 100000));
}

TEST(host_folder) {
    const fs::path d = kDir / "folder";
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d / "Data");
    fs::create_directories(d / "TRACKS" / "EU");
    { std::ofstream(d / "Data" / "Global.txd", std::ios::binary) << "txd"; }
    { std::ofstream(d / "SYSTEM.CNF", std::ios::binary) << "BOOT2 = cdrom0:\\SLUS_210.50;1\n"; }
    CHECK(disc::is_dir(d));
    CHECK(!disc::is_image(d));
    CHECK(disc::is_file(d / "SYSTEM.CNF"));
    CHECK(disc::file_size(d / "SYSTEM.CNF") == 30);
    CHECK(disc::resolve_ci(d, "DATA/GLOBAL.TXD") == d / "Data" / "Global.txd");
    CHECK(disc::resolve_ci(d, "data\\global.TXD") == d / "Data" / "Global.txd");
    CHECK(disc::resolve_ci(d, "DATA/NONE").empty());
    std::vector<std::uint8_t> b;
    CHECK(disc::read_file(d / "Data" / "Global.txd", b) && std::string(b.begin(), b.end()) == "txd");
    CHECK(disc::list_dir(d).size() == 3);
    // two names that differ only in case: ambiguous, not found (as the platform's resolver always had it)
    if (case_sensitive_dir(d)) {
        { std::ofstream(d / "a.txt") << "1"; std::ofstream(d / "A.TXT") << "2"; }
        CHECK(disc::resolve_ci(d, "a.txt").empty());
        fs::remove(d / "a.txt");
        fs::remove(d / "A.TXT");
    }
    // a disc image inside the folder: resolved through it
    Spec spec;
    CHECK(write_image(build(spec, false), d / "game.iso", 0));
    CHECK(disc::resolve_ci(d, "GAME.ISO/data/global.txd") == d / "game.iso" / "DATA" / "GLOBAL.TXD");
    disc::File f;
    CHECK(f.open(d / "Data" / "Global.txd") && f.size() == 3);
    char c3[8] = {};
    CHECK(f.read_at(1, c3, 8) == 2 && std::string(c3) == "xd");
}

// B3_TEST_ISO + B3_TEST_ISO_DIR: the real disc image against the extracted folder, file by file
TEST(real_disc_image) {
    const char* iso = std::getenv("B3_TEST_ISO");
    const char* dir = std::getenv("B3_TEST_ISO_DIR");
    if (!iso || !dir || !*iso || !*dir) { std::printf("  (B3_TEST_ISO / B3_TEST_ISO_DIR not set: skipped)\n"); return; }
    CHECK(disc::is_image(iso));
    std::size_t files = 0, bytes = 0;
    for (fs::recursive_directory_iterator it(dir), end; it != end; ++it) {
        if (!it->is_regular_file()) continue;
        const std::string rel = fs::relative(it->path(), dir).generic_string();
        std::vector<std::uint8_t> a, b;
        const bool ra = disc::read_file(it->path(), a), rb = disc::read_file(fs::path(iso) / rel, b);
        CHECK(ra && rb && a == b);
        if (!(ra && rb && a == b)) std::printf("    %s: folder %zu bytes, image %s %zu bytes\n", rel.c_str(), a.size(), rb ? "read" : "missing", b.size());
        ++files;
        bytes += a.size();
    }
    std::printf("  %zu files, %.1f MB compared\n", files, bytes / 1048576.0);
    CHECK(files > 0);
}

// 1.0.1 (b3asset/language.h): the languages on a disc folder and their one-time import. Synthetic files only (dummy
// bytes, no game text): GLOBAL<code>.BIN needs its HEAD<code>.BIN; the US pair is the game's own (not offered); JA is
// listed but not supported (not imported).
TEST(language_packs_on_a_disc_folder) {
    const fs::path root = fs::path("build") / "test_language";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path disc_dir = root / "disc", pref = root / "pref";
    fs::create_directories(disc_dir / "DATA");
    auto put = [&](const char* name, const std::string& bytes) { std::ofstream(disc_dir / "DATA" / name, std::ios::binary) << bytes; };
    put("GLOBALUS.BIN", "us");  put("HEADUS.BIN", "us");
    put("GLOBALFR.BIN", "fr-global"); put("HEADFR.BIN", "fr-head");
    put("globalit.bin", "it-global"); put("HEADIT.BIN", "it-head");   // any case, as the disc layer reads names
    put("GLOBALGE.BIN", "ge-global");                                  // no HEADGE.BIN: not a language
    put("GLOBALJA.BIN", "ja"); put("HEADJA.BIN", "ja");
    put("GLOBAL.TXD", "x"); put("GLOBALUSX.BIN", "x");                 // not GLOBAL<two letters>.BIN
    namespace lang = b3asset::language;
    const auto found = lang::on_disc(disc_dir);
    CHECK(found.size() == 3);
    CHECK(found.size() == 3 && found[0].code == "FR" && found[1].code == "IT" && found[2].code == "JA");
    CHECK(found.size() == 3 && found[0].supported && found[1].supported && !found[2].supported);
    CHECK(found.size() == 3 && found[1].name == "Italiano (Italian)");
    CHECK(lang::file(disc_dir, "it", false).filename() == "globalit.bin");
    CHECK(lang::file(disc_dir, "GE", true).empty() && lang::file(disc_dir, "xyz", false).empty());
    std::string why;
    const auto done = lang::import_from_disc(disc_dir, pref, &why);
    CHECK(done.size() == 2 && done[0].code == "FR" && done[1].code == "IT");
    std::vector<std::uint8_t> b;
    CHECK(disc::read_file(lang::imported_root(pref) / "DATA" / "GLOBALIT.BIN", b) && std::string(b.begin(), b.end()) == "it-global");
    CHECK(disc::read_file(lang::imported_root(pref) / "DATA" / "HEADFR.BIN", b) && std::string(b.begin(), b.end()) == "fr-head");
    CHECK(!fs::exists(lang::imported_root(pref) / "DATA" / "GLOBALJA.BIN"));
    // the imported folder reads as a disc: the game's redirect and the settings rows use it
    const auto again = lang::on_disc(lang::imported_root(pref));
    CHECK(again.size() == 2 && again[0].code == "FR" && again[1].code == "IT");
    // nothing to import: a reason, no files
    const auto none = lang::import_from_disc(root / "nowhere", root / "pref2", &why);
    CHECK(none.empty() && !why.empty());
    CHECK(lang::normalise("it") == "IT" && lang::normalise("ITA").empty() && lang::normalise("1t").empty());
    fs::remove_all(root, ec);
}

TEST_MAIN("test_discfs")
