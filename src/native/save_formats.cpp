// The port's .sav and the PS2 / PCSX2 save formats (src/native/save_formats.h). Our own code; the formats as the PS2
// memory card filesystem and the common PS2 save tools define them [C: read back from images built in
// tests/test_save_formats.cpp; locally, not in the repo, on a PCSX2 Mcd001.ps2 and a uLaunchELF .psu of the same save].
#include "save_formats.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace b3native::save {
namespace {

namespace fs = std::filesystem;

constexpr char kSavMagic[8] = {'B', '3', 'P', 'S', 'A', 'V', 'E', '1'};
constexpr std::uint32_t kSavVersion = 1;

constexpr std::uint16_t kExists = 0x8000, kFile = 0x0010, kDir = 0x0020;
constexpr std::size_t kEntry = 512;

std::uint16_t g16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }
std::uint32_t g32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | std::uint32_t{p[1]} << 8 | std::uint32_t{p[2]} << 16 | std::uint32_t{p[3]} << 24;
}
void p16(Bytes& b, std::uint16_t v) { b.push_back(static_cast<std::uint8_t>(v)); b.push_back(static_cast<std::uint8_t>(v >> 8)); }
void p32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }

CardTimestamp get_time(const std::uint8_t* p) {
    CardTimestamp t;
    t.second = p[1];
    t.minute = p[2];
    t.hour = p[3];
    t.day = p[4];
    t.month = p[5];
    t.year = g16(p + 6);
    return t;
}
void put_time(Bytes& b, const CardTimestamp& t) {
    b.push_back(0);
    b.push_back(t.second);
    b.push_back(t.minute);
    b.push_back(t.hour);
    b.push_back(t.day);
    b.push_back(t.month);
    p16(b, t.year);
}

// A directory entry's name: up to 32 bytes, NUL-terminated.
std::string entry_name(const std::uint8_t* e) {
    std::string n;
    for (std::size_t i = 64; i < 96 && e[i]; ++i) n.push_back(static_cast<char>(e[i]));
    return n;
}

bool usable_name(const std::string& n) {
    return !n.empty() && n.size() <= 31 && n != "." && n != ".." && n.find('/') == std::string::npos &&
           n.find('\\') == std::string::npos && n.find('\0') == std::string::npos;
}

template <class T>
std::optional<T> fail(std::string* error, const std::string& what) {
    if (error) *error = what;
    return std::nullopt;
}

std::optional<Bytes> read_file(const fs::path& p, std::string* error) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return fail<Bytes>(error, "cannot open " + p.string());
    Bytes out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return fail<Bytes>(error, "cannot read " + p.string());
    return out;
}

// A cursor over the .sav payload.
struct In {
    const std::uint8_t* p;
    std::size_t n, i = 0;
    bool ok = true;
    bool need(std::size_t k) { if (n - i < k) ok = false; return ok; }
    std::uint16_t u16() { if (!need(2)) return 0; const auto v = g16(p + i); i += 2; return v; }
    std::uint32_t u32() { if (!need(4)) return 0; const auto v = g32(p + i); i += 4; return v; }
    CardTimestamp time() { if (!need(8)) return {}; const auto t = get_time(p + i); i += 8; return t; }
    std::string name() {
        if (!need(1)) return {};
        const std::size_t len = p[i++];
        if (!need(len)) return {};
        std::string s(reinterpret_cast<const char*>(p + i), len);
        i += len;
        return s;
    }
};

void put_name(Bytes& b, const std::string& n) {
    b.push_back(static_cast<std::uint8_t>(n.size()));
    b.insert(b.end(), n.begin(), n.end());
}

// ---- the card filesystem reader ----
class CardReader {
public:
    CardReader(const std::uint8_t* b, std::size_t n) : b_(b), n_(n) {}

    std::optional<Directories> read(std::string* error) {
        static constexpr char kMagic[] = "Sony PS2 Memory Card Format ";
        if (n_ < 512 || std::memcmp(b_, kMagic, sizeof kMagic - 1) != 0) return fail<Directories>(error, "no PS2 memory card superblock");
        page_ = g16(b_ + 40);
        ppc_ = g16(b_ + 42);
        clusters_ = g32(b_ + 48);
        alloc_off_ = g32(b_ + 52);
        alloc_end_ = g32(b_ + 56);
        root_ = g32(b_ + 60);
        if (page_ != 512 || ppc_ == 0 || ppc_ > 16 || clusters_ == 0 || clusters_ > (1u << 22))
            return fail<Directories>(error, "unsupported card geometry");
        const std::size_t pages = std::size_t{clusters_} * ppc_;
        // the per-page spare area (16 bytes for 512-byte pages) present when the image is that much larger
        if (n_ >= pages * (page_ + 16)) stride_ = page_ + 16;
        else if (n_ >= pages * page_) stride_ = page_;
        else return fail<Directories>(error, "card image shorter than its superblock's " + std::to_string(pages) + " pages");
        csize_ = std::size_t{page_} * ppc_;
        for (int i = 0; i < 32; ++i) ifc_[i] = g32(b_ + 80 + 4 * i);

        std::vector<Bytes> root_entries;
        if (!read_dir(root_, root_entries, error)) return std::nullopt;
        Directories out;
        for (std::size_t i = 2; i < root_entries.size(); ++i) {
            const std::uint8_t* e = root_entries[i].data();
            const std::uint16_t mode = g16(e);
            if (!(mode & kExists) || !(mode & kDir)) continue;
            CardSaveDirectory d;
            d.name = entry_name(e);
            if (!usable_name(d.name)) continue;
            d.mode = mode;
            d.created = get_time(e + 8);
            d.modified = get_time(e + 24);
            std::vector<Bytes> entries;
            if (!read_dir(g32(e + 16), entries, error, g32(e + 4))) return std::nullopt;
            for (std::size_t k = 2; k < entries.size(); ++k) {
                const std::uint8_t* f = entries[k].data();
                const std::uint16_t fm = g16(f);
                if (!(fm & kExists) || !(fm & kFile)) continue;
                CardFile file;
                file.name = entry_name(f);
                if (!usable_name(file.name)) continue;
                file.mode = fm;
                file.created = get_time(f + 8);
                file.modified = get_time(f + 24);
                const std::uint32_t len = g32(f + 4);
                if (len) {
                    auto data = read_chain(g32(f + 16), len, error);
                    if (!data) return std::nullopt;
                    file.content = std::move(*data);
                }
                d.files.push_back(std::move(file));
            }
            out.push_back(std::move(d));
        }
        return out;
    }

private:
    // Absolute cluster c's bytes; false when outside the image.
    bool cluster(std::uint32_t c, std::uint8_t* out) const {
        for (std::uint32_t k = 0; k < ppc_; ++k) {
            const std::size_t o = (std::size_t{c} * ppc_ + k) * stride_;
            if (c >= clusters_ || o + page_ > n_) return false;
            std::memcpy(out + std::size_t{k} * page_, b_ + o, page_);
        }
        return true;
    }
    // FAT entry of allocatable cluster n (through the indirect FAT list).
    std::optional<std::uint32_t> fat(std::uint32_t n) const {
        const std::size_t epc = csize_ / 4;
        const std::size_t fat_index = n / epc, ind = fat_index / epc;
        if (ind >= 32) return std::nullopt;
        Bytes buf(csize_);
        if (!cluster(ifc_[ind], buf.data())) return std::nullopt;
        const std::uint32_t fat_cluster = g32(buf.data() + 4 * (fat_index % epc));
        if (!cluster(fat_cluster, buf.data())) return std::nullopt;
        return g32(buf.data() + 4 * (n % epc));
    }
    // `len` bytes along the chain from allocatable cluster `first`.
    std::optional<Bytes> read_chain(std::uint32_t first, std::size_t len, std::string* error) const {
        Bytes out;
        out.reserve(len + csize_);
        Bytes buf(csize_);
        std::uint32_t c = first;
        for (std::size_t steps = 0; out.size() < len; ++steps) {
            if (steps > clusters_ || c >= clusters_ || !cluster(c + alloc_off_, buf.data()))
                return fail<Bytes>(error, "card image: a cluster chain leaves the card");
            out.insert(out.end(), buf.begin(), buf.end());
            if (out.size() >= len) break;
            const auto next = fat(c);
            if (!next || *next == 0xFFFFFFFFu || !(*next & 0x80000000u))
                return fail<Bytes>(error, "card image: a cluster chain ends before its file");
            c = *next & 0x7FFFFFFFu;
        }
        out.resize(len);
        return out;
    }
    // A directory's entries (the count from its "." entry, unless given by the parent's entry).
    bool read_dir(std::uint32_t first, std::vector<Bytes>& out, std::string* error, std::uint32_t count = 0) const {
        auto head = read_chain(first, kEntry, error);
        if (!head) return false;
        if (!count) count = g32(head->data() + 4);
        if (count < 2 || count > 4096) {
            if (error) *error = "card image: a directory of " + std::to_string(count) + " entries";
            return false;
        }
        auto all = read_chain(first, std::size_t{count} * kEntry, error);
        if (!all) return false;
        for (std::uint32_t i = 0; i < count; ++i) out.emplace_back(all->begin() + i * kEntry, all->begin() + (i + 1) * kEntry);
        return true;
    }

    const std::uint8_t* b_;
    std::size_t n_;
    std::uint32_t page_ = 0, ppc_ = 0, clusters_ = 0, alloc_off_ = 0, alloc_end_ = 0, root_ = 0;
    std::size_t stride_ = 0, csize_ = 0;
    std::uint32_t ifc_[32] = {};
};

// ---- LZARI (Okumura, 1989), the decoder ----
class Lzari {
public:
    static constexpr int N = 4096, F = 60, THRESHOLD = 2, N_CHAR = 256 - THRESHOLD + F;
    static constexpr std::uint32_t M = 15, Q1 = 1u << M, Q2 = 2 * Q1, Q3 = 3 * Q1, Q4 = 4 * Q1, MAX_CUM = Q1 - 1;

    Lzari(const std::uint8_t* in, std::size_t size) : in_(in), size_(size) {
        sym_cum_[N_CHAR] = 0;
        for (int sym = N_CHAR; sym >= 1; --sym) {
            sym_to_char_[sym] = sym - 1;
            sym_freq_[sym] = 1;
            sym_cum_[sym - 1] = sym_cum_[sym] + sym_freq_[sym];
        }
        sym_freq_[0] = 0;
        pos_cum_[N] = 0;
        for (int i = N; i >= 1; --i) pos_cum_[i - 1] = pos_cum_[i] + 10000 / (i + 200);
        for (std::uint32_t i = 0; i < M + 2; ++i) value_ = 2 * value_ + bit();
    }

    std::optional<Bytes> decode(std::size_t out_size) {
        Bytes out;
        out.reserve(out_size);
        std::array<std::uint8_t, N> text{};
        std::fill(text.begin(), text.begin() + (N - F), static_cast<std::uint8_t>(' '));
        int r = N - F;
        while (out.size() < out_size) {
            const int c = decode_char();
            if (c < 256) {
                out.push_back(static_cast<std::uint8_t>(c));
                text[r++] = static_cast<std::uint8_t>(c);
                r &= N - 1;
            } else {
                const int i = (r - decode_position() - 1) & (N - 1);
                const int j = c - 255 + THRESHOLD;
                for (int k = 0; k < j && out.size() < out_size; ++k) {
                    const std::uint8_t b = text[(i + k) & (N - 1)];
                    out.push_back(b);
                    text[r++] = b;
                    r &= N - 1;
                }
            }
            if (overrun_ > 64) return std::nullopt;   // far past the input: not this format
        }
        return out;
    }

private:
    int bit() {
        if (byte_ >= size_) { ++overrun_; return 0; }
        const int b = (in_[byte_] >> (7 - bitpos_)) & 1;
        if (++bitpos_ == 8) { bitpos_ = 0; ++byte_; }
        return b;
    }
    void update(int sym) {
        if (sym_cum_[0] >= MAX_CUM) {
            std::uint32_t c = 0;
            for (int i = N_CHAR; i > 0; --i) {
                sym_cum_[i] = c;
                c += (sym_freq_[i] = (sym_freq_[i] + 1) >> 1);
            }
            sym_cum_[0] = c;
        }
        int i = sym;
        while (sym_freq_[i] == sym_freq_[i - 1]) --i;
        if (i < sym) std::swap(sym_to_char_[i], sym_to_char_[sym]);
        ++sym_freq_[i];
        while (--i >= 0) ++sym_cum_[i];
    }
    void normalise() {
        for (;;) {
            if (low_ >= Q2) { value_ -= Q2; low_ -= Q2; high_ -= Q2; }
            else if (low_ >= Q1 && high_ <= Q3) { value_ -= Q1; low_ -= Q1; high_ -= Q1; }
            else if (high_ > Q2) break;
            low_ += low_;
            high_ += high_;
            value_ = 2 * value_ + bit();
        }
    }
    int decode_char() {
        const std::uint64_t range = high_ - low_;
        const std::uint64_t x = ((std::uint64_t{value_} - low_ + 1) * sym_cum_[0] - 1) / range;
        int i = 1, j = N_CHAR;
        while (i < j) {
            const int k = (i + j) / 2;
            if (sym_cum_[k] > x) i = k + 1; else j = k;
        }
        const int sym = i;
        high_ = static_cast<std::uint32_t>(low_ + range * sym_cum_[sym - 1] / sym_cum_[0]);
        low_ += static_cast<std::uint32_t>(range * sym_cum_[sym] / sym_cum_[0]);
        normalise();
        const int ch = sym_to_char_[sym];
        update(sym);
        return ch;
    }
    int decode_position() {
        const std::uint64_t range = high_ - low_;
        const std::uint64_t x = ((std::uint64_t{value_} - low_ + 1) * pos_cum_[0] - 1) / range;
        int i = 1, j = N;
        while (i < j) {
            const int k = (i + j) / 2;
            if (pos_cum_[k] > x) i = k + 1; else j = k;
        }
        const int pos = i - 1;
        high_ = static_cast<std::uint32_t>(low_ + range * pos_cum_[pos] / pos_cum_[0]);
        low_ += static_cast<std::uint32_t>(range * pos_cum_[pos + 1] / pos_cum_[0]);
        normalise();
        return pos;
    }

    const std::uint8_t* in_;
    std::size_t size_, byte_ = 0, overrun_ = 0;
    int bitpos_ = 0;
    std::uint32_t low_ = 0, high_ = Q4, value_ = 0;
    std::array<int, N_CHAR + 1> sym_to_char_{};
    std::array<std::uint32_t, N_CHAR + 1> sym_freq_{}, sym_cum_{};
    std::array<std::uint32_t, N + 1> pos_cum_{};
};

std::string lower_ext(const fs::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// ---- .sav ----
Bytes write_sav(const Directories& dirs) {
    Bytes payload;
    p32(payload, static_cast<std::uint32_t>(dirs.size()));
    for (const auto& d : dirs) {
        put_name(payload, d.name);
        p16(payload, d.mode);
        put_time(payload, d.created);
        put_time(payload, d.modified);
        p32(payload, static_cast<std::uint32_t>(d.files.size()));
        for (const auto& f : d.files) {
            put_name(payload, f.name);
            p16(payload, f.mode);
            put_time(payload, f.created);
            put_time(payload, f.modified);
            p32(payload, static_cast<std::uint32_t>(f.content.size()));
            payload.insert(payload.end(), f.content.begin(), f.content.end());
        }
    }
    Bytes out(kSavMagic, kSavMagic + 8);
    p32(out, kSavVersion);
    p32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    p32(out, crc32(out.data(), out.size()));
    return out;
}

std::optional<Directories> read_sav(const Bytes& b, std::string* error) {
    if (b.size() < 20 || std::memcmp(b.data(), kSavMagic, 8) != 0) return fail<Directories>(error, "not a .sav file");
    const std::uint32_t version = g32(b.data() + 8), size = g32(b.data() + 12);
    if (version != kSavVersion) return fail<Directories>(error, ".sav version " + std::to_string(version) + " (this build reads 1)");
    if (size > b.size() - 20) return fail<Directories>(error, ".sav truncated");
    if (crc32(b.data(), 16 + std::size_t{size}) != g32(b.data() + 16 + size)) return fail<Directories>(error, ".sav checksum mismatch");
    In in{b.data() + 16, size};
    Directories out;
    const std::uint32_t nd = in.u32();
    for (std::uint32_t d = 0; in.ok && d < nd && d < 1024; ++d) {
        CardSaveDirectory dir;
        dir.name = in.name();
        dir.mode = in.u16();
        dir.created = in.time();
        dir.modified = in.time();
        const std::uint32_t nf = in.u32();
        for (std::uint32_t f = 0; in.ok && f < nf && f < 4096; ++f) {
            CardFile file;
            file.name = in.name();
            file.mode = in.u16();
            file.created = in.time();
            file.modified = in.time();
            const std::uint32_t len = in.u32();
            if (!in.need(len)) break;
            file.content.assign(in.p + in.i, in.p + in.i + len);
            in.i += len;
            if (!usable_name(file.name)) return fail<Directories>(error, ".sav: a bad file name");
            dir.files.push_back(std::move(file));
        }
        if (!usable_name(dir.name)) return fail<Directories>(error, ".sav: a bad directory name");
        out.push_back(std::move(dir));
    }
    if (!in.ok || in.i != size) return fail<Directories>(error, ".sav payload malformed");
    return out;
}

std::optional<Directories> read_sav_file(const fs::path& p, std::string* error) {
    auto b = read_file(p, error);
    if (!b) return std::nullopt;
    std::string why;
    auto d = read_sav(*b, &why);
    if (!d) return fail<Directories>(error, p.string() + ": " + why);
    return d;
}

bool write_sav_file(const fs::path& p, const Directories& dirs, std::string* error) {
    const Bytes bytes = write_sav(dirs);
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            if (error) *error = "cannot write " + tmp.string();
            return false;
        }
    }
    fs::rename(tmp, p, ec);   // replaces p (std::filesystem::rename does on Windows too)
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
        if (ec) {
            if (error) *error = "cannot rename " + tmp.string() + " to " + p.string() + ": " + ec.message();
            return false;
        }
    }
    return true;
}

// ---- card images ----
std::optional<Directories> read_card_image(const std::uint8_t* bytes, std::size_t size, std::string* error) {
    return CardReader(bytes, size).read(error);
}

// ---- .psu: the directory's entry, ".", "..", then each file's entry and its bytes padded to 1024 ----
std::optional<CardSaveDirectory> read_psu(const Bytes& b, std::string* error) {
    if (b.size() < 3 * kEntry) return fail<CardSaveDirectory>(error, "not a .psu (too short)");
    const std::uint8_t* d = b.data();
    const std::uint16_t mode = g16(d);
    if (!(mode & kDir) || !(g16(d + kEntry) & kDir) || !(g16(d + 2 * kEntry) & kDir) || entry_name(d + kEntry) != "." ||
        entry_name(d + 2 * kEntry) != "..")
        return fail<CardSaveDirectory>(error, "not a .psu (no directory header)");
    const std::uint32_t count = g32(d + 4);
    if (count < 2 || count > 4096) return fail<CardSaveDirectory>(error, ".psu: a directory of " + std::to_string(count) + " entries");
    CardSaveDirectory out;
    out.name = entry_name(d);
    if (!usable_name(out.name)) return fail<CardSaveDirectory>(error, ".psu: a bad directory name");
    out.mode = mode;
    out.created = get_time(d + 8);
    out.modified = get_time(d + 24);
    std::size_t o = 3 * kEntry;
    for (std::uint32_t i = 2; i < count; ++i) {
        if (b.size() - o < kEntry) return fail<CardSaveDirectory>(error, ".psu truncated");
        const std::uint8_t* e = b.data() + o;
        o += kEntry;
        const std::uint16_t fm = g16(e);
        if (!(fm & kFile)) return fail<CardSaveDirectory>(error, ".psu: a subdirectory (not supported)");
        const std::uint32_t len = g32(e + 4);
        const std::size_t padded = (std::size_t{len} + 1023) / 1024 * 1024;
        if (b.size() - o < len) return fail<CardSaveDirectory>(error, ".psu truncated");
        CardFile f;
        f.name = entry_name(e);
        if (!usable_name(f.name)) return fail<CardSaveDirectory>(error, ".psu: a bad file name");
        f.mode = fm;
        f.created = get_time(e + 8);
        f.modified = get_time(e + 24);
        f.content.assign(b.begin() + o, b.begin() + o + len);
        o += std::min(padded, b.size() - o);
        out.files.push_back(std::move(f));
    }
    return out;
}

Bytes write_psu(const CardSaveDirectory& dir) {
    auto entry = [](Bytes& b, std::uint16_t mode, std::uint32_t len, const CardTimestamp& c, const CardTimestamp& m,
                    const std::string& name) {
        const std::size_t at = b.size();
        b.resize(at + kEntry, 0);
        std::uint8_t* e = b.data() + at;
        e[0] = static_cast<std::uint8_t>(mode);
        e[1] = static_cast<std::uint8_t>(mode >> 8);
        for (int i = 0; i < 4; ++i) e[4 + i] = static_cast<std::uint8_t>(len >> (8 * i));
        Bytes t;
        put_time(t, c);
        put_time(t, m);
        std::memcpy(e + 8, t.data(), 8);
        std::memcpy(e + 24, t.data() + 8, 8);
        std::memcpy(e + 64, name.data(), std::min<std::size_t>(name.size(), 31));
    };
    Bytes b;
    const std::uint16_t dmode = static_cast<std::uint16_t>(dir.mode | kExists | kDir);
    entry(b, dmode, static_cast<std::uint32_t>(dir.files.size() + 2), dir.created, dir.modified, dir.name);
    entry(b, dmode, 0, dir.created, dir.modified, ".");
    entry(b, dmode, 0, dir.created, dir.modified, "..");
    for (const auto& f : dir.files) {
        entry(b, static_cast<std::uint16_t>(f.mode | kExists | kFile), static_cast<std::uint32_t>(f.content.size()), f.created,
              f.modified, f.name);
        b.insert(b.end(), f.content.begin(), f.content.end());
        b.resize(b.size() + ((f.content.size() + 1023) / 1024 * 1024 - f.content.size()), 0);
    }
    return b;
}

// ---- .max: "Ps2PowerSave", CRC, directory name (32), title (32), compressed size + 4, file count, unpacked size;
// the LZARI stream unpacks to per file {u32 size, name (32), bytes, padded so the next record + 8 is 16-aligned} ----
std::optional<Bytes> lzari_decode(const std::uint8_t* in, std::size_t size, std::size_t out_size) {
    return Lzari(in, size).decode(out_size);
}

std::optional<CardSaveDirectory> read_max(const Bytes& b, std::string* error) {
    if (b.size() < 0x5C || std::memcmp(b.data(), "Ps2PowerSave", 12) != 0) return fail<CardSaveDirectory>(error, "not a .max");
    const std::uint32_t clen = g32(b.data() + 80), count = g32(b.data() + 84), length = g32(b.data() + 88);
    if (count > 4096 || length > (64u << 20)) return fail<CardSaveDirectory>(error, ".max: implausible header");
    // some writers store the unpacked size where the packed one belongs: then the stream is the rest of the file
    const std::size_t avail = b.size() - 0x5C;
    const std::size_t packed = clen == length || clen < 4 ? avail : std::min<std::size_t>(clen - 4, avail);
    auto s = lzari_decode(b.data() + 0x5C, packed, length);
    if (!s) return fail<CardSaveDirectory>(error, ".max: the compressed data ends early");
    CardSaveDirectory out;
    out.name = std::string(reinterpret_cast<const char*>(b.data() + 16), strnlen(reinterpret_cast<const char*>(b.data() + 16), 32));
    if (!usable_name(out.name)) return fail<CardSaveDirectory>(error, ".max: a bad directory name");
    out.mode = 0x8427;   // exists, 0x0400, directory, rwx (as mymc+ gives a .max's directory)
    std::size_t o = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (s->size() - o < 36) return fail<CardSaveDirectory>(error, ".max: the unpacked data ends early");
        const std::uint32_t len = g32(s->data() + o);
        CardFile f;
        f.name = std::string(reinterpret_cast<const char*>(s->data() + o + 4), strnlen(reinterpret_cast<const char*>(s->data() + o + 4), 32));
        o += 36;
        if (s->size() - o < len) return fail<CardSaveDirectory>(error, ".max: the unpacked data ends early");
        if (!usable_name(f.name)) return fail<CardSaveDirectory>(error, ".max: a bad file name");
        f.mode = 0x8417;   // exists, 0x0400, file, rwx
        f.content.assign(s->begin() + o, s->begin() + o + len);
        o += len;
        o = (o + 8 + 15) / 16 * 16 - 8;
        out.files.push_back(std::move(f));
    }
    return out;
}

// ---- import ----
std::optional<Directories> read_any(const fs::path& given, std::string* error) {
    std::error_code ec;
    // a file dialog picks files: PCSX2's own files stand for their folder (the folder card, or one save folder)
    const std::string leaf = given.filename().string();
    const fs::path p = leaf == "_pcsx2_superblock" || leaf == "_pcsx2_index" ? given.parent_path() : given;
    if (fs::is_directory(p, ec)) {
        // a PCSX2 folder memory card (save folders inside) or one save folder (files only)
        bool has_subdirs = false;
        for (const auto& e : fs::directory_iterator(p, ec)) {
            std::error_code eec;
            const std::string n = e.path().filename().string();
            if (e.is_directory(eec) && !n.empty() && n.front() != '.') has_subdirs = true;
        }
        if (!has_subdirs) {
            auto d = load_save_folder(p, error);
            if (!d) return std::nullopt;
            return Directories{std::move(*d)};
        }
        std::vector<fs::path> folders;
        for (const auto& e : fs::directory_iterator(p, ec)) {
            std::error_code eec;
            const std::string n = e.path().filename().string();
            if (e.is_directory(eec) && !n.empty() && n.front() != '.') folders.push_back(e.path());
        }
        std::sort(folders.begin(), folders.end());
        Directories out;
        for (const auto& f : folders) {
            auto d = load_save_folder(f, error);
            if (!d) return std::nullopt;
            out.push_back(std::move(*d));
        }
        return out;
    }
    auto b = read_file(p, error);
    if (!b) return std::nullopt;
    std::string why;
    if (b->size() >= 8 && std::memcmp(b->data(), kSavMagic, 8) == 0) {
        auto d = read_sav(*b, &why);
        if (!d) return fail<Directories>(error, p.string() + ": " + why);
        return d;
    }
    if (b->size() >= 12 && std::memcmp(b->data(), "Ps2PowerSave", 12) == 0) {
        auto d = read_max(*b, &why);
        if (!d) return fail<Directories>(error, p.string() + ": " + why);
        return Directories{std::move(*d)};
    }
    if (b->size() >= 28 && std::memcmp(b->data(), "Sony PS2 Memory Card Format ", 28) == 0) {
        auto d = read_card_image(b->data(), b->size(), &why);
        if (!d) return fail<Directories>(error, p.string() + ": " + why);
        return d;
    }
    if (auto d = read_psu(*b, &why)) return Directories{std::move(*d)};
    const std::string ext = lower_ext(p);
    return fail<Directories>(error, p.string() + ": not a save this build reads (" +
                                        (ext == ".psu" ? why : std::string(".sav, .psu, .max, .ps2 or a folder card")) + ")");
}

std::optional<Directories> game_save(const Directories& dirs, std::string* error) {
    for (const auto& d : dirs)
        if (d.name == kGameSaveDirectory) return Directories{d};
    std::string names;
    for (const auto& d : dirs) names += (names.empty() ? "" : ", ") + d.name;
    return fail<Directories>(error, std::string("no Burnout 3 save (") + kGameSaveDirectory + ") in it" +
                                        (names.empty() ? std::string(" (empty)") : " (found: " + names + ")"));
}

}  // namespace b3native::save
