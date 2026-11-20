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
#include "chd_writer.h"   // chd1
#include "iso_writer.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
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
Iso build(Spec& spec, bool far, bool odd = false) {
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
    if (odd) {   // fsrobust: names a real disc never has: a byte >= 0x80 (read as Latin-1: CAFÉ) and a separator (left out)
        const auto cafe = file("CAF\xC3\x89.TXT", 11, 300);
        rr.push_back(record("CAF\xC9.TXT;1", cafe.first, cafe.second, 0));
        rr.push_back(record("A/B.TXT;1", cafe.first, cafe.second, 0));
        rr.push_back(record("A\\C.TXT;1", cafe.first, cafe.second, 0));
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

// ---- chd1: CHD images (tests/chd_writer.h) of the same volume ----
namespace {
std::vector<std::uint8_t> file_bytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
bool put_file(const fs::path& p, const std::vector<std::uint8_t>& b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return bool(f);
}
// the cooked .iso of the test volume (small.iso) and its files
const std::vector<std::uint8_t>& cooked(Spec& spec) {
    static Spec s;
    static std::vector<std::uint8_t> bytes;
    if (bytes.empty()) {
        fs::create_directories(kDir);
        CHECK(write_image(build(s, false), kDir / "chd_src.iso", 0));
        bytes = file_bytes(kDir / "chd_src.iso");
    }
    spec = s;
    return bytes;
}
// every file of the CHD has the bytes the .iso path gives
void same_as_iso(const fs::path& chd, const Spec& spec) {
    const fs::path iso = kDir / "chd_src.iso";
    int n = 0;
    std::function<void(const fs::path&)> walk = [&](const fs::path& rel) {
        for (const disc::Entry& e : disc::list_dir(rel.empty() ? iso : iso / rel)) {
            if (e.dir) { walk(rel / e.name); continue; }
            std::vector<std::uint8_t> a, b;
            CHECK(disc::read_file(iso / rel / e.name, a));
            const bool ok = disc::read_file(chd / rel / e.name, b);
            CHECK(ok && a == b);
            if (!ok || a != b) std::printf("    %s differs from the .iso's\n", (rel / e.name).generic_string().c_str());
            ++n;
        }
    };
    walk("");
    CHECK(n == int(spec.files.size()));
}
// a CD's data (frames of 2448 bytes): `before` frames of an audio track first (0: none), then the data track (its
// pregap frames stored first when `pregap`), each track padded to 4 frames as chdman writes it
std::vector<std::uint8_t> cd_data(const std::vector<std::uint8_t>& iso, const std::string& type, std::uint32_t before, std::uint32_t pregap,
                                  chdtest::Options& o, bool old_tags = false) {
    std::vector<std::uint8_t> d;
    o.unitbytes = 2448;
    o.hunkbytes = 8 * 2448;
    int track = 1;
    if (before) {
        d.resize(std::size_t((before + 3) / 4 * 4) * 2448, 0x11);
        o.metadata.push_back({"CHT2", "TRACK:1 TYPE:AUDIO SUBTYPE:NONE FRAMES:" + std::to_string(before) + " PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0"});
        ++track;
    }
    d.resize(d.size() + std::size_t(pregap) * 2448, 0x22);   // the pregap's frames (not the volume)
    const std::vector<std::uint8_t> frames = chdtest::cd_frames(iso, type);
    d.insert(d.end(), frames.begin(), frames.end());
    const std::uint32_t n = pregap + std::uint32_t(frames.size() / 2448);
    if (old_tags)
        o.metadata.push_back({"CHTR", "TRACK:" + std::to_string(track) + " TYPE:" + type + " SUBTYPE:NONE FRAMES:" + std::to_string(n)});
    else
        o.metadata.push_back({"CHT2", "TRACK:" + std::to_string(track) + " TYPE:" + type + " SUBTYPE:RW FRAMES:" + std::to_string(n) +
                                          " PREGAP:" + std::to_string(pregap) + " PGTYPE:" + (pregap ? "V" : "") + type + " PGSUB:RW POSTGAP:0"});
    d.resize(d.size() + std::size_t((4 - n % 4) % 4) * 2448, 0);
    return d;
}
}  // namespace

TEST(chd_dvd) {
    // chdman createdvd: the cooked image in hunks; a plain map (no compressor) and a compressed one (zlib hunks, stored
    // hunks, repeats of an earlier hunk)
    Spec spec;
    const std::vector<std::uint8_t>& iso = cooked(spec);
    chdtest::Options plain;
    plain.hunkbytes = 4096;
    CHECK(put_file(kDir / "dvd_plain.chd", chdtest::write(iso, plain).bytes));
    CHECK(disc::is_chd(kDir / "dvd_plain.chd") && !disc::is_chd(kDir / "chd_src.iso"));
    check_volume(kDir / "dvd_plain.chd", spec, "CHD, DVD, uncompressed");
    same_as_iso(kDir / "dvd_plain.chd", spec);
    chdtest::Options z;
    z.codec = chdtest::Codec::kZlib;
    z.hunkbytes = 8192;
    z.metadata.push_back({"DVD ", ""});
    const chdtest::Image zi = chdtest::write(iso, z);
    CHECK(zi.bytes.size() < iso.size() / 2);   // the pattern files and the zero sectors compress
    CHECK(put_file(kDir / "dvd_zlib.iso", zi.bytes));   // a CHD by its magic, whatever its name
    check_volume(kDir / "dvd_zlib.iso", spec, "CHD, DVD, zlib (named .iso)");
    same_as_iso(kDir / "dvd_zlib.iso", spec);
    CHECK(disc::image_error(kDir / "dvd_zlib.iso").empty());
}

TEST(chd_cd) {
    // chdman createcd: frames of 2352 + 96 bytes, the volume on the data track (MODE1 / MODE2 form 1 user data, as a
    // raw .bin's); cdzl hunks and a plain map; an audio track before it, a pregap stored in the file, the old track tag
    Spec spec;
    const std::vector<std::uint8_t>& iso = cooked(spec);
    struct Case { const char* name; const char* type; chdtest::Codec codec; std::uint32_t before, pregap; bool old_tags; };
    for (const Case& c : {Case{"cd_m2raw.chd", "MODE2_RAW", chdtest::Codec::kCdZlib, 0, 0, false},
                          Case{"cd_m1raw.chd", "MODE1_RAW", chdtest::Codec::kCdZlib, 0, 0, false},
                          Case{"cd_m1.chd", "MODE1", chdtest::Codec::kNone, 0, 0, true},
                          Case{"cd_m2.chd", "MODE2", chdtest::Codec::kCdZlib, 0, 0, false},
                          Case{"cd_audio_first.chd", "MODE2_RAW", chdtest::Codec::kNone, 5, 0, false},
                          Case{"cd_pregap.chd", "MODE1_RAW", chdtest::Codec::kCdZlib, 13, 150, false}}) {
        chdtest::Options o;
        o.codec = c.codec;
        const std::vector<std::uint8_t> data = cd_data(iso, c.type, c.before, c.pregap, o, c.old_tags);
        CHECK(put_file(kDir / c.name, chdtest::write(data, o).bytes));
        check_volume(kDir / c.name, spec, c.name);
        same_as_iso(kDir / c.name, spec);
    }
    // a CD with only an audio track: no data track, no disc image
    chdtest::Options a;
    a.unitbytes = 2448;
    a.hunkbytes = 8 * 2448;
    a.metadata.push_back({"CHT2", "TRACK:1 TYPE:AUDIO SUBTYPE:NONE FRAMES:40 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0"});
    CHECK(put_file(kDir / "cd_audio.chd", chdtest::write(std::vector<std::uint8_t>(40 * 2448, 3), a).bytes));
    CHECK(!disc::is_image(kDir / "cd_audio.chd"));
    CHECK(disc::image_error(kDir / "cd_audio.chd").find("data track") != std::string::npos);
}

TEST(chd_concurrent_reads) {
    // the hunk cache shared by threads, with the read-ahead workers: a cache much smaller than the files (B3_CHD_CACHE_KB:
    // hunks evicted and decoded again), sequential readers of two large files beside random readers; every byte right
    std::vector<std::uint8_t> big = pattern(21, 3u << 20), other = pattern(22, 1u << 20);
    for (std::size_t k = 0; k < big.size(); k += 4096) big[k] = std::uint8_t(k >> 12);   // no two hunks alike
    isotest::Writer w;
    w.file("BIG.DAT", big);
    w.file("DATA/OTHER.DAT", other);
    CHECK(w.write((kDir / "conc.iso").string()));
    chdtest::Options z;
    z.codec = chdtest::Codec::kZlib;
    z.hunkbytes = 4096;
    CHECK(put_file(kDir / "conc.chd", chdtest::write(file_bytes(kDir / "conc.iso"), z).bytes));
    setenv("B3_CHD_CACHE_KB", "160", 1);   // 40 hunks
    const bool image = disc::is_image(kDir / "conc.chd");
    unsetenv("B3_CHD_CACHE_KB");
    CHECK(image);
    std::atomic<int> bad{0};
    std::vector<std::thread> th;
    for (int t = 0; t < 8; ++t)
        th.emplace_back([&, t] {
            disc::File f;
            const bool which = t % 2 == 0;
            const std::vector<std::uint8_t>& want = which ? big : other;
            if (!f.open(kDir / "conc.chd" / (which ? "BIG.DAT" : "DATA/OTHER.DAT"))) { ++bad; return; }
            std::vector<std::uint8_t> buf(65536);
            if (t < 4) {   // sequential, 2 KB to 64 KB at a time (RW Audio's and a load's sizes), twice
                for (int pass = 0; pass < 2; ++pass)
                    for (std::uint64_t at = 0; at < want.size();) {
                        const std::size_t n = std::size_t(2048) << ((at / 65536 + std::uint64_t(t)) % 6);
                        const std::int64_t r = f.read_at(at, buf.data(), n);
                        if (r <= 0 || std::memcmp(buf.data(), want.data() + at, std::size_t(r)) != 0) { ++bad; return; }
                        at += std::uint64_t(r);
                    }
            } else {   // random
                std::uint64_t x = 0x9E3779B97F4A7C15ull * std::uint64_t(t + 1);
                for (int k = 0; k < 3000; ++k) {
                    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
                    const std::uint64_t at = x % want.size();
                    const std::int64_t r = f.read_at(at, buf.data(), 1 + std::size_t(x >> 40) % 9000);
                    if (r <= 0 || std::memcmp(buf.data(), want.data() + at, std::size_t(r)) != 0) { ++bad; return; }
                }
            }
        });
    for (auto& x : th) x.join();
    CHECK(bad == 0);
}

TEST(chd_damaged) {
    // a cut-short or damaged CHD: no disc image (or a file that cannot be read), a message that says so, no crash
    Spec spec;
    const std::vector<std::uint8_t>& iso = cooked(spec);
    chdtest::Options z;
    z.codec = chdtest::Codec::kZlib;
    z.hunkbytes = 8192;
    const chdtest::Image img = chdtest::write(iso, z);
    auto gone = [](const fs::path& p) {
        const std::string why = disc::image_error(p);
        std::printf("  %s: %s\n", p.filename().string().c_str(), why.c_str());
        CHECK(!disc::is_image(p) && !disc::is_dir(p) && disc::list_dir(p).empty() && !disc::exists(p / "SYSTEM.CNF"));
        CHECK(!why.empty() && why.find(p.string()) != std::string::npos);
        return why;
    };
    // the header only; the map without the hunks; a few bytes
    std::vector<std::uint8_t> cut(img.bytes.begin(), img.bytes.begin() + 124);
    CHECK(put_file(kDir / "cut_header.chd", cut));
    gone(kDir / "cut_header.chd");
    cut.assign(img.bytes.begin(), img.bytes.begin() + std::ptrdiff_t(img.first_hunk + 10));
    CHECK(put_file(kDir / "cut_map.chd", cut));
    CHECK(gone(kDir / "cut_map.chd").find("damaged or cut short") != std::string::npos);
    cut.assign(img.bytes.begin(), img.bytes.begin() + 12);
    CHECK(put_file(kDir / "cut_magic.chd", cut));
    gone(kDir / "cut_magic.chd");
    // the volume descriptor's hunk (sector 16: hunk 4) with a byte changed: its CRC fails
    std::vector<std::uint8_t> bad = img.bytes;
    CHECK(img.hunk_at[4] != 0);
    bad[std::size_t(img.hunk_at[4]) + 3] ^= 0x40;
    CHECK(put_file(kDir / "bad_pvd.chd", bad));
    CHECK(gone(kDir / "bad_pvd.chd").find("damaged") != std::string::npos);
    // the last stored hunk damaged: the volume mounts, the file there cannot be read (an error, not other bytes)
    std::size_t last = img.hunk_at.size();
    while (last > 0 && img.hunk_at[last - 1] == 0) --last;
    CHECK(last > 8);
    bad = img.bytes;
    bad[std::size_t(img.hunk_at[last - 1]) + 1] ^= 0x01;
    CHECK(put_file(kDir / "bad_tail.chd", bad));
    CHECK(disc::is_image(kDir / "bad_tail.chd"));
    int unreadable = 0;
    for (const auto& [path, bytes] : spec.files) {
        std::vector<std::uint8_t> got;
        if (!disc::read_file(kDir / "bad_tail.chd" / path, got)) ++unreadable;
        else CHECK(got == bytes);
    }
    CHECK(unreadable >= 1);
    // the end cut off: the same (the files there unreadable)
    cut.assign(img.bytes.begin(), img.bytes.begin() + std::ptrdiff_t(img.hunk_at[last - 1] + 1));
    CHECK(put_file(kDir / "cut_tail.chd", cut));
    CHECK(disc::is_image(kDir / "cut_tail.chd"));
    unreadable = 0;
    for (const auto& [path, bytes] : spec.files) {
        std::vector<std::uint8_t> got;
        if (!disc::read_file(kDir / "cut_tail.chd" / path, got)) ++unreadable;
    }
    CHECK(unreadable >= 1);
    // a CHD that needs its parent
    z.parent = true;
    CHECK(put_file(kDir / "child.chd", chdtest::write(iso, z).bytes));
    CHECK(gone(kDir / "child.chd").find("parent") != std::string::npos);
    // a CHD of something else (no ISO 9660 volume): not a disc image, as for an .iso, no CHD error
    chdtest::Options plain;
    CHECK(put_file(kDir / "other.chd", chdtest::write(pattern(3, 100000), plain).bytes));
    CHECK(!disc::is_image(kDir / "other.chd") && disc::is_file(kDir / "other.chd") && disc::image_error(kDir / "other.chd").empty());
    CHECK(disc::image_error(kDir / "chd_src.iso").empty() && disc::image_error(kDir / "missing.chd").empty());
}

TEST(odd_names_in_an_image) {
    // fsrobust: a name byte >= 0x80 became an fs::path that throws on Windows (a made image in the game folder ended
    // the launcher); now Latin-1 read as UTF-8, and a name with a separator is not listed
    fs::create_directories(kDir);
    Spec spec;
    const Iso iso = build(spec, false, true);
    const fs::path img = kDir / "odd.iso";
    CHECK(write_image(iso, img, 0));
    bool cafe = false, sep = false;
    for (const disc::Entry& e : disc::list_dir(img)) {
        cafe = cafe || e.name == "CAF\xC3\x89.TXT";
        sep = sep || e.name.find_first_of("/\\") != std::string::npos;
    }
    CHECK(cafe);
    CHECK(!sep);
    std::vector<std::uint8_t> d;
    CHECK(disc::read_file(img / fs::path(std::u8string(u8"CAF\u00C9.TXT")), d) && d == spec.files["CAF\xC3\x89.TXT"]);
    CHECK(!disc::resolve_ci(img, "caf\xC3\x89.txt").empty());   // (the case of the ASCII letters only)
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

// 1.3.0 (b3asset/language.h dj_*): the DJ voice. Synthetic stand-in banks only (dj_test_bank: headers and zero bytes,
// no game audio). The redirect table, the header checks, the missing-file fallback, and the import (a temporary
// folder renamed into place; cancelled: nothing kept; a bad bank refused).
namespace {
void put_bank(const fs::path& p, const std::vector<std::uint8_t>& b) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}
}  // namespace

TEST(dj_redirect_table) {
    namespace lang = b3asset::language;
    CHECK(lang::dj_letter("FR") == 'F' && lang::dj_letter("ge") == 'G' && lang::dj_letter("IT") == 'I' && lang::dj_letter("SP") == 'S');
    CHECK(!lang::dj_letter("EN") && !lang::dj_letter("DU") && !lang::dj_letter("SW") && !lang::dj_letter("") && !lang::dj_letter("JA"));
    CHECK(lang::disc_key("tracks\\eu\\c1_v1\\e_djrace.rws") == "TRACKS/EU/C1_V1/E_DJRACE.RWS");
    CHECK(lang::disc_key("\\TRACKS\\E_DJGEN.RWS") == "TRACKS/E_DJGEN.RWS");
    CHECK(lang::dj_counterpart("TRACKS/EU/C1_V1/E_DJRACE.RWS", 'G') == "TRACKS/EU/C1_V1/G_DJRACE.RWS");
    CHECK(lang::dj_counterpart("TRACKS/E_DJGEN.RWS", 'S') == "TRACKS/S_DJGEN.RWS");
    CHECK(lang::dj_counterpart("TRACKS/E_DJMBL.RWS", 'F') == "TRACKS/F_DJMBL.RWS");
    // not an English DJ bank, or no language: no redirect
    CHECK(lang::dj_counterpart("TRACKS/E_DJGEN.RWS", 0).empty());
    CHECK(lang::dj_counterpart("TRACKS/_EATRAX1.RWS", 'G').empty());
    CHECK(lang::dj_counterpart("TRACKS/G_DJGEN.RWS", 'G').empty());
    CHECK(lang::dj_counterpart("SOUND/E_DJGEN.RWS", 'G').empty());
    CHECK(lang::dj_counterpart("TRACKS/EU/C1_V1/STATIC.DAT", 'G').empty());
    CHECK(lang::dj_counterpart("TRACKS/E_DJGEN.AWD", 'G').empty());
    CHECK(lang::dj_counterpart("TRACKS/../E_DJGEN.RWS", 'G').empty());
}

TEST(dj_header_checks) {
    namespace lang = b3asset::language;
    const auto b = lang::dj_test_bank(5, 0x800, "DJMBL");
    lang::RwsHeader h, e;
    std::string why;
    CHECK(lang::rws_header(b.data(), b.size(), b.size(), h, &why) && lang::dj_format(h));
    CHECK(h.segments == 5 && h.layers == 1 && h.bank == "DJMBL" && h.largest_segment == 0x800 && h.data_size == 5 * 0x800);
    // the same bank with longer lines (a German bank): it stands in; another line count, name or codec does not
    const auto longer = lang::dj_test_bank(5, 0x1000, "DJMBL");
    CHECK(lang::rws_header(longer.data(), longer.size(), longer.size(), e) && lang::dj_matches(e, h, &why));
    const auto fewer = lang::dj_test_bank(4, 0x800, "DJMBL"), other = lang::dj_test_bank(5, 0x800, "DJMCR"), codec = lang::dj_test_bank(5, 0x800, "DJMBL", 9);
    CHECK(lang::rws_header(fewer.data(), fewer.size(), fewer.size(), e) && !lang::dj_matches(e, h, &why) && why.find("line count") != std::string::npos);
    CHECK(lang::rws_header(other.data(), other.size(), other.size(), e) && !lang::dj_matches(e, h, &why) && why.find("name") != std::string::npos);
    CHECK(lang::rws_header(codec.data(), codec.size(), codec.size(), e) && !lang::dj_matches(e, h, &why) && why.find("codec") != std::string::npos);
    // broken files: refused with a reason
    CHECK(!lang::rws_header(b.data(), b.size(), b.size() + 1, h, &why));              // truncated / padded
    auto bad = b;
    bad[0] = 0x0E;
    CHECK(!lang::rws_header(bad.data(), bad.size(), bad.size(), h, &why) && why.find("0x80D") != std::string::npos);
    bad = b;
    bad[0x18 + 0x60 + 0x1F] = 0x7F;   // the first line's offset past the data
    CHECK(!lang::rws_header(bad.data(), bad.size(), bad.size(), h, &why) && why.find("segment") != std::string::npos);
    bad = b;
    bad[0x18 + 0x28] = 2;   // two layers: not the DJ's layout
    CHECK(lang::rws_header(bad.data(), bad.size(), bad.size(), h) && !lang::dj_format(h, &why));
    const std::string junk(200, 'x');
    CHECK(!lang::rws_header(reinterpret_cast<const std::uint8_t*>(junk.data()), junk.size(), junk.size(), h, &why));
}

TEST(dj_redirect_and_fallback) {
    namespace lang = b3asset::language;
    const fs::path root = fs::path("build") / "test_dj_redirect";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path us = root / "us", dj = root / "pref" / "languages" / "DJ" / "GE";
    put_bank(us / "TRACKS" / "E_DJGEN.RWS", lang::dj_test_bank(18, 0x800, "DJGEN"));
    put_bank(us / "TRACKS" / "E_DJMBL.RWS", lang::dj_test_bank(5, 0x800, "DJMBL"));
    put_bank(us / "TRACKS" / "EU" / "C1_V1" / "E_DJRACE.RWS", lang::dj_test_bank(38, 0x800, "EU_C1"));
    put_bank(us / "TRACKS" / "US" / "C1_V1" / "E_DJRACE.RWS", lang::dj_test_bank(38, 0x800, "US_C1"));
    put_bank(dj / "TRACKS" / "G_DJGEN.RWS", lang::dj_test_bank(18, 0x1000, "DJGEN"));             // longer lines: fine
    put_bank(dj / "TRACKS" / "g_djmbl.rws", lang::dj_test_bank(4, 0x800, "DJMBL"));               // a line short: English
    put_bank(dj / "TRACKS" / "EU" / "C1_V1" / "G_DJRACE.RWS", lang::dj_test_bank(38, 0x800, "EU_C1"));
    // (no US/C1_V1/G_DJRACE.RWS: that track's DJ stays English)
    std::string why;
    const auto eng = [&](const std::string& k) { return disc::resolve_ci(us, k); };
    CHECK(lang::dj_redirect(dj, 'G', "TRACKS/E_DJGEN.RWS", eng("TRACKS/E_DJGEN.RWS"), &why).filename() == "G_DJGEN.RWS");
    CHECK(lang::dj_redirect(dj, 'G', "TRACKS/EU/C1_V1/E_DJRACE.RWS", eng("TRACKS/EU/C1_V1/E_DJRACE.RWS"), &why).filename() == "G_DJRACE.RWS");
    CHECK(lang::dj_redirect(dj, 'G', "TRACKS/US/C1_V1/E_DJRACE.RWS", eng("TRACKS/US/C1_V1/E_DJRACE.RWS"), &why).empty() && why == "not imported");
    CHECK(lang::dj_redirect(dj, 'G', "TRACKS/E_DJMBL.RWS", eng("TRACKS/E_DJMBL.RWS"), &why).empty() && why.find("line count") != std::string::npos);
    CHECK(lang::dj_redirect(dj, 'G', "TRACKS/_EATRAX1.RWS", eng("TRACKS/E_DJGEN.RWS"), &why).empty() && why.empty());
    CHECK(lang::dj_redirect(dj, 0, "TRACKS/E_DJGEN.RWS", eng("TRACKS/E_DJGEN.RWS"), &why).empty());
    CHECK(lang::dj_redirect({}, 'G', "TRACKS/E_DJGEN.RWS", eng("TRACKS/E_DJGEN.RWS"), &why).empty());
    // the imported folder lists as the disc does (the rows' sizes; the game's "is anything imported")
    const auto v = lang::dj_on_disc(dj);
    CHECK(v.size() == 1 && v[0].code == "GE" && v[0].files.size() == 3);
    fs::remove_all(root, ec);
}

TEST(dj_import_temp_rename_cancel) {
    namespace lang = b3asset::language;
    const fs::path root = fs::path("build") / "test_dj_import";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path eu = root / "eu", us = root / "us", pref = root / "pref";
    // a European disc with two DJ voices (and the English banks it does not need), a US disc to check against
    for (const char* l : {"G", "I"}) {
        put_bank(eu / "TRACKS" / (std::string(l) + "_DJGEN.RWS"), lang::dj_test_bank(18, 0x4000, "DJGEN"));
        put_bank(eu / "TRACKS" / "EU" / "C1_V1" / (std::string(l) + "_DJRACE.RWS"), lang::dj_test_bank(38, 0x4000, "EU_C1"));
    }
    put_bank(eu / "TRACKS" / "E_DJGEN.RWS", lang::dj_test_bank(18, 0x800, "DJGEN"));
    put_bank(eu / "TRACKS" / "EU" / "C1_V1" / "STATIC.DAT", {1, 2, 3});
    put_bank(us / "TRACKS" / "E_DJGEN.RWS", lang::dj_test_bank(18, 0x800, "DJGEN"));
    put_bank(us / "TRACKS" / "EU" / "C1_V1" / "E_DJRACE.RWS", lang::dj_test_bank(38, 0x800, "EU_C1"));
    const auto found = lang::dj_on_disc(eu);
    CHECK(found.size() == 2 && found[0].code == "GE" && found[1].code == "IT");
    CHECK(found.size() == 2 && found[0].files.size() == 2 && found[0].bytes == found[1].bytes && found[0].bytes > 38 * 0x4000);
    if (found.size() != 2) return;
    std::string why;
    // cancelled at once: nothing kept, not even the temporary folder
    std::atomic<bool> cancel{true};
    lang::DjProgress pr;
    pr.cancel = &cancel;
    CHECK(!lang::dj_import(found[0], pref, us, pr, &why) && why == "cancelled");
    CHECK(!fs::exists(lang::dj_root(pref, "GE")) && !fs::exists(fs::path(lang::dj_root(pref, "GE")).concat(".part")));
    CHECK(lang::dj_imported_bytes(pref, "GE") == 0);
    // cancelled half way (after the first file's first MB): the same
    cancel = false;
    std::uint64_t seen = 0, total = 0;
    pr.step = [&](std::uint64_t d, std::uint64_t t) { seen = d; total = t; if (d > 0) cancel = true; };
    CHECK(!lang::dj_import(found[0], pref, us, pr, &why) && why == "cancelled" && !fs::exists(lang::dj_root(pref, "GE")));
    // the whole import: progress to the end, the files in dj_root, no temporary folder left
    cancel = false;
    pr.step = [&](std::uint64_t d, std::uint64_t t) { seen = d; total = t; };
    CHECK(lang::dj_import(found[0], pref, us, pr, &why));
    CHECK(seen == total && total == found[0].bytes);
    CHECK(lang::dj_imported_bytes(pref, "GE") == found[0].bytes);
    CHECK(fs::exists(lang::dj_root(pref, "GE") / "TRACKS" / "EU" / "C1_V1" / "G_DJRACE.RWS"));
    CHECK(!fs::exists(fs::path(lang::dj_root(pref, "GE")).concat(".part")));
    std::vector<std::uint8_t> a, b;
    CHECK(disc::read_file(lang::dj_root(pref, "GE") / "TRACKS" / "G_DJGEN.RWS", a) && disc::read_file(eu / "TRACKS" / "G_DJGEN.RWS", b) && a == b);
    // imported again over it: replaced in one step, still one copy
    CHECK(lang::dj_import(found[0], pref, us, pr, &why) && lang::dj_imported_bytes(pref, "GE") == found[0].bytes);
    CHECK(!fs::exists(fs::path(lang::dj_root(pref, "GE")).concat(".old")));
    // a bank that does not match the US one (another line count): refused before anything is written
    put_bank(us / "TRACKS" / "EU" / "C1_V1" / "E_DJRACE.RWS", lang::dj_test_bank(37, 0x800, "EU_C1"));
    CHECK(!lang::dj_import(found[1], pref, us, pr, &why) && why.find("line count") != std::string::npos && !fs::exists(lang::dj_root(pref, "IT")));
    // without a US disc to compare: the format alone
    CHECK(lang::dj_import(found[1], pref, {}, pr, &why) && lang::dj_imported_bytes(pref, "IT") == found[1].bytes);
    // a damaged bank on the disc (cut short): refused
    {
        auto broken = lang::dj_test_bank(18, 0x4000, "DJGEN");
        broken.resize(broken.size() - 100);
        put_bank(eu / "TRACKS" / "I_DJGEN.RWS", broken);
        const auto f2 = lang::dj_on_disc(eu);
        CHECK(f2.size() == 2 && !lang::dj_import(f2[1], pref, {}, pr, &why) && why.find("I_DJGEN.RWS") != std::string::npos);
        CHECK(lang::dj_imported_bytes(pref, "IT") == found[1].bytes);   // the earlier import untouched
    }
    // not a language with a DJ of its own
    lang::DjVoice en = found[0];
    en.code = "DU";
    CHECK(!lang::dj_import(en, pref, us, pr, &why));
    // removed: gone, the other language stays
    CHECK(lang::dj_remove(pref, "GE", &why) && lang::dj_imported_bytes(pref, "GE") == 0 && !fs::exists(lang::dj_root(pref, "GE")));
    CHECK(lang::dj_imported_bytes(pref, "IT") == found[1].bytes);
    fs::remove_all(root, ec);
}

TEST(training_videos_redirect_and_import) {
    // 1.3.0: the race training's localized pictures (OVID/TRC_ / TRD_<FRA|GER|ITA|SPA>.M2V): the key mapping, and their
    // import as a pack of its own (stand-in videos: a sequence header and zeros)
    namespace lang = b3asset::language;
    std::string code, en;
    CHECK(lang::training_video("OVID/TRC_GER.M2V", &code, &en) && code == "GE" && en == "OVID/TRC_ENG.M2V");
    CHECK(lang::training_video("OVID/TRD_SPA.M2V", &code, &en) && code == "SP" && en == "OVID/TRD_ENG.M2V");
    CHECK(lang::training_video("OVID/TRD_FRA.M2V", &code) && code == "FR" && lang::training_video("OVID/TRC_ITA.M2V", &code) && code == "IT");
    CHECK(!lang::training_video("OVID/TRC_ENG.M2V") && !lang::training_video("OVID/TRC_DUT.M2V") && !lang::training_video("OVID/TRC_JAP.M2V"));
    CHECK(!lang::training_video("OVID/MOVIE.RWS") && !lang::training_video("DATA/TRC_GER.M2V") && !lang::training_video("OVID/GERMAN30.M2V"));
    const fs::path root = fs::path("build") / "test_training";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path eu = root / "eu", pref = root / "pref";
    put_bank(eu / "OVID" / "TRC_GER.M2V", lang::training_test_video(0x30000));
    put_bank(eu / "OVID" / "trd_ger.m2v", lang::training_test_video(0x20000));
    put_bank(eu / "OVID" / "TRC_FRA.M2V", lang::training_test_video(0x30000));   // no TRD_FRA: not offered
    put_bank(eu / "OVID" / "TRC_ITA.M2V", std::vector<std::uint8_t>(0x30000, 7)); // not an MPEG-2 video
    put_bank(eu / "OVID" / "TRD_ITA.M2V", lang::training_test_video(0x20000));
    const auto found = lang::training_on_disc(eu);
    CHECK(found.size() == 2 && found[0].code == "GE" && found[1].code == "IT");
    if (found.size() != 2) return;
    CHECK(found[0].files.size() == 2 && found[0].bytes == 0x30004 + 0x20004);
    lang::DjProgress pr;
    std::string why;
    CHECK(lang::pack_import(found[0], pref, {}, pr, lang::Pack::kTraining, &why));
    CHECK(lang::pack_imported_bytes(pref, "GE", lang::Pack::kTraining) == found[0].bytes);
    CHECK(fs::exists(lang::pack_root(pref, "GE", lang::Pack::kTraining) / "OVID" / "TRD_GER.M2V"));
    CHECK(lang::pack_imported_bytes(pref, "GE", lang::Pack::kDj) == 0);   // the DJ pack is another folder
    CHECK(!lang::pack_import(found[1], pref, {}, pr, lang::Pack::kTraining, &why) && why.find("TRC_ITA.M2V") != std::string::npos &&
          !fs::exists(lang::pack_root(pref, "IT", lang::Pack::kTraining)));
    // the folders go through the remover the caller gives (the launcher: b3mod::remove_tree): the pack and its leftovers
    std::vector<fs::path> removed;
    const lang::RemoveTree spy = [&](const fs::path& p) { removed.push_back(p); return lang::remove_all_tree(p); };
    CHECK(lang::pack_remove(pref, "GE", lang::Pack::kTraining, &why, spy) && lang::pack_imported_bytes(pref, "GE", lang::Pack::kTraining) == 0);
    CHECK(removed.size() == 3 && removed[0] == lang::pack_root(pref, "GE", lang::Pack::kTraining));
    removed.clear();
    std::atomic<bool> stop{true};
    pr.cancel = &stop;
    CHECK(!lang::pack_import(found[0], pref, {}, pr, lang::Pack::kTraining, &why, spy) && why == "cancelled" && !removed.empty());
    fs::remove_all(root, ec);
}

TEST_MAIN("test_discfs")
