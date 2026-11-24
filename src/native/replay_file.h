// replay_file.h -- replay files (remaster-features §5 "Replay files"; replay1, 2026-11-12): a whole session of the game,
// from its boot to its end, kept as everything the game's timeline depends on, so native_play plays it again exactly.
// The game runs on virtual time (VBlanks, IOP samples, the idle skip to the next event), so the same pad reports on
// the same VBlanks, the same memory card, the same disc and the same game-side options give the same session, VBlank
// for VBlank [L: tools/golden/baseline.sh / compare.sh; tools/native/replay_check.sh shows a recording and its
// replay with equal SIF / DMA / RW Audio logs and RAM hashes].
//
// A replay holds the inputs only: no pictures, no game data. The disc is named by its program's size and FNV-1a 64
// (the check the launcher and native_play make), the port by its version (include/b3report/version.h).
//
// The file (little endian; standard library only, plus save_formats.h's CRC-32 and .sav reader):
//   +0  "B3REPLAY" (8 bytes)   +8 u32 format (1)   +12 u32 section count
//   then the sections: u32 tag (four characters), u32 size, the payload; a reader skips tags it does not know
//   end u32 CRC-32 (save_formats.h crc32, zlib's) of everything before it
// Sections (all present in a file this port writes; HEAD, DISC, OPTS, CARD and PADS are required):
//   HEAD  u64 the session's last VBlank, i64 its start (Unix seconds), i32 the player's UTC offset (seconds), str the
//         port version that recorded it ("0.1.0-test+1a2b3c4d")
//   DISC  u64 the program's (SLUS_210.50) size, u64 its FNV-1a 64
//   INFO  str: "key=value" lines, for people (recorded, host, end, ...); never read for the replay itself
//   OPTS  str: "key=value" lines: the game-side options of the run (idle_skip, movie_skip, explore_movies), the
//         game-side environment (env.<NAME>, kGameEnv: the console clock, the card's insertion VBlank, ...) and the
//         custom music folders' fingerprints (music_fp.<folder>); a replay sets exactly these again
//   CARD  u8 1 a memory card in the slot (0 none), u8 its slot (1..5), then the card's contents at boot as a .sav
//         (save_formats.h write_sav: the profile file the game started with, its own CRC inside)
//   PADS  32 bytes the neutral report, u32 op count, then per op: varint VBlank delta, u8 (kind << 1 | port), and for
//         kSample a u32 mask of the bytes that changed from that port's previous sample (the neutral one at first)
//         and those bytes. The ops are what native_play did to the pad model before each VBlank's pad_vblank():
//         set the port's report (kSample; from then on every VBlank until the next op of the port), set no report
//         (kAbsent: the window gave none), plug the port in / pull it out (kConnect / kDisconnect; VBlank 0: before
//         the boot)
//   TEXT  u32 count, per entry: varint VBlank delta, str: the characters typed into the game's on-screen keyboard,
//         as taken on that VBlank (tools/native/menu_mouse.h)
//   SETS  u32 count, per entry: varint VBlank delta, str key, str value: the live settings the game reads while it
//         runs (date_format, speed_units (units1: from its first KM/H VBlank), music: "<mode>|<folder>"), latched
//         per VBlank; VBlank 0: the values at the boot. tune4:
//         also "tune" = "0x<key>[<element>] 0x<word>", a tuning console edit (include/b3tune/tune.h), each one kept
//   CHKS  u32 interval, u32 count, per entry: varint VBlank delta, u64 a hash of guest RAM (MB 0..30; MB 31 holds the
//         stack, which differs harmlessly between hosts) at that VBlank's hook: a replay checks it is still on the
//         recorded timeline
// (str: u16 length, UTF-8 bytes; varint: LEB128, at most 10 bytes.)
//
// Compatibility (compatible()): another disc program: refused. Another port release (the VERSION base before '+'):
// refused. The same release, another commit: played, with a warning (the checkpoints then tell whether it parted).
// A newer format than kFormat: refused by decode().
#ifndef B3NATIVE_REPLAY_FILE_H
#define B3NATIVE_REPLAY_FILE_H

#include "save_formats.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace b3native::replay {

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
using Report = std::array<std::uint8_t, 32>;

inline constexpr char kMagic[8] = {'B', '3', 'R', 'E', 'P', 'L', 'A', 'Y'};
inline constexpr std::uint32_t kFormat = 1;
inline constexpr const char* kExtension = ".b3replay";
inline constexpr std::uint32_t kCheckEvery = 600;   // VBlanks between RAM checkpoints (10 s of PS2 time)
inline constexpr int kKeepRecorded = 20;            // the newest automatic recordings kept (rotate())
inline constexpr double kVBlankHz = 59.94;          // [C PS2 NTSC field rate]

// The environment variables that change what the game does (read by the platform, the card, the stand-ins, the
// renderer-side wrappers), recorded and set again by a replay; everything else in the environment is host-only
// (logs, traces, the saves folder, pacing).
inline constexpr const char* kGameEnv[] = {
    "B3NATIVE_RTC", "B3NATIVE_CARD_CLOCK", "B3NATIVE_CARD_FILE_TIME", "B3NATIVE_CARD_PRESENT_VBLANK", "B3NATIVE_CARD_MESSAGES",
    "B3NATIVE_PC_ROW", "B3NATIVE_MENU_MOUSE", "B3NATIVE_MUSIC_OFF", "B3NATIVE_MUSIC_MODE", "B3NATIVE_MUSIC_DIR",
    "B3NATIVE_DATE_FORMAT", "B3NATIVE_SPEED_UNITS", "B3NATIVE_MOVIE_STANDIN", "B3NATIVE_IPU_STANDIN", "B3NATIVE_MOVIE_IPU", "B3NATIVE_EXPLORE_RETAIL",
    "B3NATIVE_EXPLORE_RETURN", "B3NATIVE_GTFS_FAIL", "B3NATIVE_OSD_CONFIG", "B3NATIVE_NET_ADAPTER", "B3NATIVE_MIXER",
    "B3NATIVE_DEBUG_HEALTH", "B3NATIVE_VFSET", "B3NATIVE_STACKSET", "B3_VU0_AOT", "B3_VU0_FLOAT", "B3_VU0_TIMING",
    "B3_RWA_INSTANT_UPLOAD", "B3NATIVE_MORE_RIVALS",
};

struct PadOp {
    enum Kind : std::uint8_t { kSample = 0, kAbsent = 1, kConnect = 2, kDisconnect = 3 };
    std::uint64_t vblank = 0;
    std::uint8_t port = 0;   // 0 or 1
    Kind kind = kSample;
    Report report{};         // kSample only
    bool operator==(const PadOp&) const = default;
};
struct TextOp {
    std::uint64_t vblank = 0;
    std::string text;
    bool operator==(const TextOp&) const = default;
};
struct SettingOp {
    std::uint64_t vblank = 0;
    std::string key, value;
    bool operator==(const SettingOp&) const = default;
};
struct Check {
    std::uint64_t vblank = 0, hash = 0;
    bool operator==(const Check&) const = default;
};
using KeyValues = std::vector<std::pair<std::string, std::string>>;

struct Replay {
    std::uint32_t format = kFormat;
    // HEAD
    std::uint64_t vblanks = 0;      // the session's last VBlank
    std::int64_t started = 0;       // Unix seconds
    std::int32_t utc_offset = 0;    // seconds east of UTC
    std::string version;            // the port that recorded it
    // DISC
    std::uint64_t program_size = 0, program_fnv = 0;
    KeyValues info, options;
    // CARD
    bool card = false;
    int card_slot = 1;
    Bytes card_sav;                 // write_sav bytes (empty without a card)
    // PADS, TEXT, SETS, CHKS
    Report neutral{};
    std::vector<PadOp> pads;
    std::vector<TextOp> text;
    std::vector<SettingOp> settings;
    std::uint32_t check_every = kCheckEvery;
    std::vector<Check> checks;
    bool operator==(const Replay&) const = default;

    const std::string* option(std::string_view key) const {
        for (const auto& [k, v] : options) if (k == key) return &v;
        return nullptr;
    }
    const std::string* info_value(std::string_view key) const {
        for (const auto& [k, v] : info) if (k == key) return &v;
        return nullptr;
    }
};

// ---- encoding ----
namespace detail {
inline void u8(Bytes& b, std::uint8_t v) { b.push_back(v); }
inline void u32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
inline void u64(Bytes& b, std::uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
inline void var(Bytes& b, std::uint64_t v) {
    while (v >= 0x80) { b.push_back(std::uint8_t(v | 0x80)); v >>= 7; }
    b.push_back(std::uint8_t(v));
}
inline void str(Bytes& b, std::string_view s) {
    const std::size_t n = std::min<std::size_t>(s.size(), 0xFFFF);
    b.push_back(std::uint8_t(n));
    b.push_back(std::uint8_t(n >> 8));
    b.insert(b.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
}
inline std::string lines(const KeyValues& kv) {
    std::string t;
    for (const auto& [k, v] : kv) {
        std::string vv = v;
        std::replace(vv.begin(), vv.end(), '\n', ' ');
        t += k + "=" + vv + "\n";
    }
    return t;
}
inline std::uint32_t tag(std::string_view t) {   // four characters
    return std::uint32_t(std::uint8_t(t[0])) | std::uint32_t(std::uint8_t(t[1])) << 8 | std::uint32_t(std::uint8_t(t[2])) << 16 |
           std::uint32_t(std::uint8_t(t[3])) << 24;
}

struct Reader {
    const std::uint8_t* p;
    std::size_t n, at = 0;
    bool ok = true;
    bool need(std::size_t k) {
        if (!ok || n - at < k) { ok = false; return false; }
        return true;
    }
    std::uint8_t u8() { return need(1) ? p[at++] : 0; }
    std::uint32_t u32() {
        if (!need(4)) return 0;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= std::uint32_t(p[at + std::size_t(i)]) << (8 * i);
        at += 4;
        return v;
    }
    std::uint64_t u64() {
        if (!need(8)) return 0;
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= std::uint64_t(p[at + std::size_t(i)]) << (8 * i);
        at += 8;
        return v;
    }
    std::uint64_t var() {
        std::uint64_t v = 0;
        for (int s = 0; s < 70; s += 7) {
            const std::uint8_t c = u8();
            if (!ok) return 0;
            v |= std::uint64_t(c & 0x7F) << s;
            if (!(c & 0x80)) return v;
        }
        ok = false;
        return 0;
    }
    std::string str() {
        const std::size_t len = std::size_t(u8()) | std::size_t(u8()) << 8;
        if (!need(len)) return {};
        std::string s(reinterpret_cast<const char*>(p + at), len);
        at += len;
        return s;
    }
    std::string rest() {
        std::string s(reinterpret_cast<const char*>(p + at), n - at);
        at = n;
        return s;
    }
};
inline KeyValues parse_lines(std::string_view t) {
    KeyValues kv;
    while (!t.empty()) {
        const std::size_t e = t.find('\n');
        const std::string_view line = t.substr(0, e);
        t = e == std::string_view::npos ? std::string_view() : t.substr(e + 1);
        const std::size_t eq = line.find('=');
        if (eq != std::string_view::npos) kv.emplace_back(std::string(line.substr(0, eq)), std::string(line.substr(eq + 1)));
    }
    return kv;
}
}  // namespace detail

inline Bytes encode(const Replay& r) {
    using namespace detail;
    Bytes out(kMagic, kMagic + 8);
    u32(out, kFormat);
    u32(out, 0);   // the section count, set at the end
    std::uint32_t sections = 0;
    auto section = [&](std::string_view t, const Bytes& payload) {
        ++sections;
        u32(out, tag(t));
        u32(out, std::uint32_t(payload.size()));
        out.insert(out.end(), payload.begin(), payload.end());
    };
    Bytes b;
    u64(b, r.vblanks);
    u64(b, std::uint64_t(r.started));
    u32(b, std::uint32_t(r.utc_offset));
    str(b, r.version);
    section("HEAD", b);
    b.clear();
    u64(b, r.program_size);
    u64(b, r.program_fnv);
    section("DISC", b);
    b.clear();
    const std::string info = lines(r.info);
    b.assign(info.begin(), info.end());
    section("INFO", b);
    const std::string opts = lines(r.options);
    b.assign(opts.begin(), opts.end());
    section("OPTS", b);
    b.clear();
    u8(b, r.card ? 1 : 0);
    u8(b, std::uint8_t(r.card_slot));
    if (r.card) b.insert(b.end(), r.card_sav.begin(), r.card_sav.end());
    section("CARD", b);
    b.assign(r.neutral.begin(), r.neutral.end());
    u32(b, std::uint32_t(r.pads.size()));
    {
        Report prev[2] = {r.neutral, r.neutral};
        std::uint64_t last = 0;
        for (const PadOp& op : r.pads) {
            var(b, op.vblank - std::min(op.vblank, last));
            last = std::max(last, op.vblank);
            u8(b, std::uint8_t(op.kind << 1 | (op.port & 1)));
            if (op.kind != PadOp::kSample) continue;
            Report& p = prev[op.port & 1];
            std::uint32_t mask = 0;
            for (int i = 0; i < 32; ++i) if (op.report[std::size_t(i)] != p[std::size_t(i)]) mask |= 1u << i;
            u32(b, mask);
            for (int i = 0; i < 32; ++i) if (mask & (1u << i)) b.push_back(op.report[std::size_t(i)]);
            p = op.report;
        }
    }
    section("PADS", b);
    b.clear();
    u32(b, std::uint32_t(r.text.size()));
    {
        std::uint64_t last = 0;
        for (const TextOp& t : r.text) {
            var(b, t.vblank - std::min(t.vblank, last));
            last = std::max(last, t.vblank);
            str(b, t.text);
        }
    }
    section("TEXT", b);
    b.clear();
    u32(b, std::uint32_t(r.settings.size()));
    {
        std::uint64_t last = 0;
        for (const SettingOp& s : r.settings) {
            var(b, s.vblank - std::min(s.vblank, last));
            last = std::max(last, s.vblank);
            str(b, s.key);
            str(b, s.value);
        }
    }
    section("SETS", b);
    b.clear();
    u32(b, r.check_every);
    u32(b, std::uint32_t(r.checks.size()));
    {
        std::uint64_t last = 0;
        for (const Check& c : r.checks) {
            var(b, c.vblank - std::min(c.vblank, last));
            last = std::max(last, c.vblank);
            u64(b, c.hash);
        }
    }
    section("CHKS", b);
    for (int i = 0; i < 4; ++i) out[12 + std::size_t(i)] = std::uint8_t(sections >> (8 * i));
    u32(out, b3native::save::crc32(out.data(), out.size()));
    return out;
}

// nullopt (and *error: "is damaged (...)" and the like, to follow the file's name) for anything that is not a whole replay this port can read.
inline std::optional<Replay> decode(const Bytes& bytes, std::string* error = nullptr) {
    using namespace detail;
    auto fail = [&](const std::string& why) -> std::optional<Replay> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (bytes.size() < 20 || !std::equal(kMagic, kMagic + 8, bytes.begin())) return fail("is not a replay file");
    Reader h{bytes.data(), bytes.size(), 8};
    const std::uint32_t format = h.u32();
    if (format == 0 || format > kFormat)
        return fail("was made by a newer version of the port (replay format " + std::to_string(format) + "; this one reads up to " +
                    std::to_string(kFormat) + ")");
    Reader c{bytes.data(), bytes.size(), bytes.size() - 4};
    if (c.u32() != b3native::save::crc32(bytes.data(), bytes.size() - 4)) return fail("is damaged (its checksum does not match)");
    const std::uint32_t count = h.u32();
    Replay r;
    r.format = format;
    std::map<std::uint32_t, std::pair<std::size_t, std::size_t>> sec;   // tag -> (offset, size)
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::uint32_t t = h.u32(), size = h.u32();
        if (!h.ok || h.at > bytes.size() - 4 || size > bytes.size() - 4 - h.at) return fail("is damaged (a section runs past the end)");
        sec.emplace(t, std::make_pair(h.at, std::size_t(size)));
        h.at += size;
    }
    if (h.at != bytes.size() - 4) return fail("is damaged (its length does not match its sections)");
    for (const char* need : {"HEAD", "DISC", "OPTS", "CARD", "PADS"})
        if (!sec.count(tag(need))) return fail(std::string("is incomplete (no ") + need + " section)");
    auto reader = [&](std::string_view t) -> std::optional<Reader> {
        auto it = sec.find(tag(t));
        if (it == sec.end()) return std::nullopt;
        return Reader{bytes.data() + it->second.first, it->second.second};
    };
    {
        Reader s = *reader("HEAD");
        r.vblanks = s.u64();
        r.started = std::int64_t(s.u64());
        r.utc_offset = std::int32_t(s.u32());
        r.version = s.str();
        if (!s.ok) return fail("is damaged (HEAD)");
    }
    {
        Reader s = *reader("DISC");
        r.program_size = s.u64();
        r.program_fnv = s.u64();
        if (!s.ok) return fail("is damaged (DISC)");
    }
    if (auto s = reader("INFO")) r.info = parse_lines(s->rest());
    r.options = parse_lines(reader("OPTS")->rest());
    {
        Reader s = *reader("CARD");
        r.card = s.u8() != 0;
        r.card_slot = s.u8();
        if (!s.ok || (r.card && (r.card_slot < 1 || r.card_slot > 5))) return fail("is damaged (CARD)");
        if (r.card) {
            r.card_sav.assign(s.p + s.at, s.p + s.n);
            std::string why;
            if (!b3native::save::read_sav(r.card_sav, &why)) return fail("is damaged (its memory card: " + why + ")");
        }
    }
    {
        Reader s = *reader("PADS");
        if (!s.need(32)) return fail("is damaged (PADS)");
        std::copy(s.p, s.p + 32, r.neutral.begin());
        s.at = 32;
        const std::uint32_t n = s.u32();
        if (!s.ok || n > s.n) return fail("is damaged (PADS)");
        Report prev[2] = {r.neutral, r.neutral};
        std::uint64_t v = 0;
        r.pads.reserve(n);
        for (std::uint32_t k = 0; k < n && s.ok; ++k) {
            PadOp op;
            v += s.var();
            op.vblank = v;
            const std::uint8_t kp = s.u8();
            op.port = kp & 1;
            op.kind = static_cast<PadOp::Kind>(kp >> 1);
            if (op.kind > PadOp::kDisconnect) return fail("is damaged (PADS: an unknown op)");
            if (op.kind == PadOp::kSample) {
                const std::uint32_t mask = s.u32();
                Report& p = prev[op.port];
                for (int i = 0; i < 32; ++i) if (mask & (1u << i)) p[std::size_t(i)] = s.u8();
                op.report = p;
            }
            r.pads.push_back(op);
        }
        if (!s.ok || s.at != s.n) return fail("is damaged (PADS)");
    }
    if (auto s = reader("TEXT")) {
        const std::uint32_t n = s->u32();
        std::uint64_t v = 0;
        for (std::uint32_t k = 0; k < n && s->ok; ++k) {
            v += s->var();
            std::string t = s->str();
            r.text.push_back({v, std::move(t)});
        }
        if (!s->ok) return fail("is damaged (TEXT)");
    }
    if (auto s = reader("SETS")) {
        const std::uint32_t n = s->u32();
        std::uint64_t v = 0;
        for (std::uint32_t k = 0; k < n && s->ok; ++k) {
            v += s->var();
            std::string key = s->str(), value = s->str();
            r.settings.push_back({v, std::move(key), std::move(value)});
        }
        if (!s->ok) return fail("is damaged (SETS)");
    }
    if (auto s = reader("CHKS")) {
        r.check_every = s->u32();
        const std::uint32_t n = s->u32();
        std::uint64_t v = 0;
        for (std::uint32_t k = 0; k < n && s->ok; ++k) {
            v += s->var();
            const std::uint64_t hash = s->u64();
            r.checks.push_back({v, hash});
        }
        if (!s->ok) return fail("is damaged (CHKS)");
    }
    return r;
}

inline std::optional<Replay> read_file(const fs::path& p, std::string* error = nullptr) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot be read";
        return std::nullopt;
    }
    const Bytes b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return decode(b, error);
}

// Written beside as "<p>.tmp", then renamed over p.
inline bool write_bytes(const fs::path& p, const Bytes& b, std::string* error = nullptr) {
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size())) || !f.flush()) {
            if (error) *error = "cannot write " + tmp.string();
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
    }
    if (ec) {
        if (error) *error = "cannot rename " + tmp.string() + ": " + ec.message();
        return false;
    }
    return true;
}
inline bool write_file(const fs::path& p, const Replay& r, std::string* error = nullptr) { return write_bytes(p, encode(r), error); }

// ---- compatibility ----
struct Host {
    std::uint64_t program_size = 0, program_fnv = 0;
    std::string version;   // b3port::version()
};
struct Compat {
    enum Verdict { kExact, kWarn, kRefuse } verdict = kExact;
    std::string text;      // empty for kExact
};
inline std::string_view version_base(std::string_view v) { return v.substr(0, v.find('+')); }
inline Compat compatible(const Replay& r, const Host& h) {
    char a[64], b[64];
    if (r.program_size != h.program_size || r.program_fnv != h.program_fnv) {
        std::snprintf(a, sizeof a, "%llu bytes, %016llx", static_cast<unsigned long long>(r.program_size), static_cast<unsigned long long>(r.program_fnv));
        std::snprintf(b, sizeof b, "%llu bytes, %016llx", static_cast<unsigned long long>(h.program_size), static_cast<unsigned long long>(h.program_fnv));
        return {Compat::kRefuse, std::string("It was recorded with another game disc (its program SLUS_210.50: ") + a + "; this one: " + b +
                                     "). A replay plays only with the disc that recorded it."};
    }
    if (version_base(r.version) != version_base(h.version))
        return {Compat::kRefuse, "It was recorded by version " + r.version + " of the port, and this is " + h.version +
                                     ". A replay plays only on the release that recorded it."};
    if (r.version != h.version)
        return {Compat::kWarn, "It was recorded by build " + r.version + " and this is " + h.version +
                                   ": the same release, so it should play the same; the checkpoints tell if it does not."};
    if (r.version.find("-dirty") != std::string::npos)   // two builds with local changes share one name
        return {Compat::kWarn, "It was recorded by a build with local changes (" + r.version +
                                   "), maybe not this one; the checkpoints tell if it does not play the same."};
    return {};
}

// ---- recording ----
// What native_play did to the pad model per VBlank, kept as the ops above (only the changes).
class Recorder {
public:
    explicit Recorder(const Report& neutral) { r_.neutral = neutral; }
    Replay& replay() { return r_; }
    // The report set for `port` on VBlank v (null: none set this VBlank)
    void sample(std::uint64_t v, unsigned port, const std::uint8_t* report) {
        port &= 1;
        Report s{};
        if (report) std::copy(report, report + 32, s.begin());
        if (seen_[port] && present_[port] == (report != nullptr) && (!report || s == last_[port])) return;
        if (!seen_[port] && !report) {   // the start is "no report" already
            seen_[port] = true;
            return;
        }
        seen_[port] = true;
        present_[port] = report != nullptr;
        last_[port] = s;
        r_.pads.push_back({v, std::uint8_t(port), report ? PadOp::kSample : PadOp::kAbsent, report ? s : Report{}});
    }
    void connect(std::uint64_t v, unsigned port, bool on) {
        r_.pads.push_back({v, std::uint8_t(port & 1), on ? PadOp::kConnect : PadOp::kDisconnect, {}});
    }
    void text(std::uint64_t v, std::string_view t) {
        if (!t.empty()) r_.text.push_back({v, std::string(t)});
    }
    // A live setting's value on VBlank v (kept when it changed)
    void setting(std::uint64_t v, const std::string& key, const std::string& value) {
        auto it = settings_.find(key);
        if (it != settings_.end() && it->second == value) return;
        settings_[key] = value;
        r_.settings.push_back({v, key, value});
    }
    // tune4: an event on VBlank v, kept every time (a tuning edit: "tune", b3tune::event_value; the same value twice is
    // two writes)
    void event(std::uint64_t v, const std::string& key, const std::string& value) { r_.settings.push_back({v, key, value}); }
    void check(std::uint64_t v, std::uint64_t hash) { r_.checks.push_back({v, hash}); }
    std::size_t changes() const { return r_.pads.size(); }

private:
    Replay r_;
    bool seen_[2] = {false, false}, present_[2] = {false, false};
    Report last_[2]{};
    std::map<std::string, std::string> settings_;
};

// ---- playback ----
class Player {
public:
    explicit Player(const Replay& r) : r_(r) { report_[0] = report_[1] = r.neutral; }
    // Takes the ops up to VBlank v (call with v increasing; v = 0: the ones before the boot). The port connections
    // changed on the way, in order: (port, plugged in).
    std::vector<std::pair<unsigned, bool>> step(std::uint64_t v) {
        std::vector<std::pair<unsigned, bool>> plugs;
        for (; pad_ < r_.pads.size() && r_.pads[pad_].vblank <= v; ++pad_) {
            const PadOp& op = r_.pads[pad_];
            switch (op.kind) {
            case PadOp::kSample: present_[op.port] = true; report_[op.port] = op.report; break;
            case PadOp::kAbsent: present_[op.port] = false; break;
            case PadOp::kConnect: plugs.emplace_back(op.port, true); break;
            case PadOp::kDisconnect: plugs.emplace_back(op.port, false); break;
            }
        }
        return plugs;
    }
    bool present(unsigned port) const { return present_[port & 1]; }
    const Report& report(unsigned port) const { return report_[port & 1]; }
    // The characters typed on VBlank v (call with v increasing)
    std::string text(std::uint64_t v) {
        std::string t;
        for (; text_ < r_.text.size() && r_.text[text_].vblank <= v; ++text_) t += r_.text[text_].text;
        return t;
    }
    // The live settings changed up to VBlank v, in order
    std::vector<const SettingOp*> settings(std::uint64_t v) {
        std::vector<const SettingOp*> out;
        for (; set_ < r_.settings.size() && r_.settings[set_].vblank <= v; ++set_) out.push_back(&r_.settings[set_]);
        return out;
    }
    // The recorded hash of VBlank v, when it has one
    std::optional<std::uint64_t> check(std::uint64_t v) {
        while (chk_ < r_.checks.size() && r_.checks[chk_].vblank < v) ++chk_;
        if (chk_ < r_.checks.size() && r_.checks[chk_].vblank == v) return r_.checks[chk_].hash;
        return std::nullopt;
    }

private:
    const Replay& r_;
    std::size_t pad_ = 0, text_ = 0, set_ = 0, chk_ = 0;
    bool present_[2] = {false, false};
    Report report_[2]{};
};

// ---- the folder (<user data folder>/replays) ----
inline fs::path replays_dir(const fs::path& user_data) { return user_data.empty() ? fs::path() : user_data / "replays"; }

inline std::tm local_tm(std::int64_t unix) {
    const std::time_t t = static_cast<std::time_t>(unix);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    return lt;
}
// "20261112-213005.b3replay" (local time); "-2", "-3" ... when taken
inline fs::path new_file(const fs::path& dir, std::int64_t unix) {
    const std::tm lt = local_tm(unix);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &lt);
    std::error_code ec;
    for (int k = 1;; ++k) {
        fs::path p = dir / (std::string(stamp) + (k > 1 ? "-" + std::to_string(k) : std::string()) + kExtension);
        if (!fs::exists(p, ec)) return p;
    }
}
// A name new_file() makes (rotate() deletes only those: a replay renamed by the player stays)
inline bool automatic_name(const std::string& name) {
    const std::string ext = kExtension;
    if (name.size() < 15 + ext.size() || name.compare(name.size() - ext.size(), ext.size(), ext) != 0) return false;
    for (int i = 0; i < 15; ++i) {
        const char c = name[std::size_t(i)];
        if (i == 8 ? c != '-' : (c < '0' || c > '9')) return false;
    }
    const std::string tail = name.substr(15, name.size() - 15 - ext.size());
    if (tail.empty()) return true;
    if (tail.size() < 2 || tail[0] != '-') return false;
    return std::all_of(tail.begin() + 1, tail.end(), [](char c) { return c >= '0' && c <= '9'; });
}
// Deletes the oldest automatic recordings in `dir` beyond the newest `keep`; returns how many went. media1: also an
// automatic recording's leftover "<name>.tmp" more than a minute old (a session killed while it wrote a checkpoint).
inline int rotate(const fs::path& dir, int keep = kKeepRecorded) {
    std::vector<std::string> names;
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = it->path().filename().string();
        if (automatic_name(name)) names.push_back(name);
        else if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0 && automatic_name(name.substr(0, name.size() - 4))) {
            std::error_code tec;
            const auto t = fs::last_write_time(it->path(), tec);
            if (!tec && now - t > std::chrono::minutes(1)) fs::remove(it->path(), tec);
        }
    }
    std::sort(names.begin(), names.end());   // the stamp sorts by time
    int gone = 0;
    for (std::size_t k = 0; k + std::size_t(std::max(keep, 0)) < names.size(); ++k)
        if (fs::remove(dir / names[k], ec)) ++gone;
    return gone;
}

// "1:05", "1:02:03" for a number of VBlanks
inline std::string format_length(std::uint64_t vblanks) {
    const std::uint64_t s = static_cast<std::uint64_t>(double(vblanks) / kVBlankHz);
    char b[32];
    if (s >= 3600) std::snprintf(b, sizeof b, "%llu:%02llu:%02llu", static_cast<unsigned long long>(s / 3600),
                                 static_cast<unsigned long long>(s / 60 % 60), static_cast<unsigned long long>(s % 60));
    else std::snprintf(b, sizeof b, "%llu:%02llu", static_cast<unsigned long long>(s / 60), static_cast<unsigned long long>(s % 60));
    return b;
}

// The replays in `dir`, newest first (by their start, then name); a file that does not read keeps its error.
struct Entry {
    fs::path path;
    std::uintmax_t bytes = 0;
    std::optional<Replay> replay;
    std::string error;
};
inline std::vector<Entry> list(const fs::path& dir) {
    std::vector<Entry> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != kExtension) continue;
        Entry e;
        e.path = it->path();
        e.bytes = it->file_size(ec);
        e.replay = read_file(e.path, &e.error);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        const std::int64_t ta = a.replay ? a.replay->started : 0, tb = b.replay ? b.replay->started : 0;
        if (ta != tb) return ta > tb;
        return a.path.filename() > b.path.filename();
    });
    return out;
}

// FNV-1a 64 over a list of (name, size): the custom music folder's fingerprint (its files as the game would play them)
inline std::uint64_t fingerprint(const std::vector<std::pair<std::string, std::uintmax_t>>& files) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    auto mix = [&](const void* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) h = (h ^ static_cast<const std::uint8_t*>(p)[i]) * 0x100000001B3ull;
    };
    for (const auto& [name, size] : files) {
        mix(name.data(), name.size());
        const std::uint64_t s = size;
        mix(&s, 8);
    }
    return h;
}

}  // namespace b3native::replay

#endif
