// custom_music.cpp -- the custom soundtrack's host side (custom_music.h): tags, the folder, resampling, the player.
#include "custom_music.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace b3music {
namespace {

std::filesystem::path upath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string ustr(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

void put_utf8(std::string& o, std::uint32_t c) {
    if (c < 0x80) o.push_back(char(c));
    else if (c < 0x800) { o.push_back(char(0xC0 | (c >> 6))); o.push_back(char(0x80 | (c & 0x3F))); }
    else if (c < 0x10000) { o.push_back(char(0xE0 | (c >> 12))); o.push_back(char(0x80 | ((c >> 6) & 0x3F))); o.push_back(char(0x80 | (c & 0x3F))); }
    else { o.push_back(char(0xF0 | (c >> 18))); o.push_back(char(0x80 | ((c >> 12) & 0x3F))); o.push_back(char(0x80 | ((c >> 6) & 0x3F))); o.push_back(char(0x80 | (c & 0x3F))); }
}

// trims spaces and NULs at both ends
std::string trim(std::string s) {
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\0' || c == '\r' || c == '\n'; };
    while (!s.empty() && sp(s.back())) s.pop_back();
    std::size_t k = 0;
    while (k < s.size() && sp(s[k])) ++k;
    return s.substr(k);
}

std::uint32_t be32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }
std::uint32_t le32(const std::uint8_t* p) { return std::uint32_t(p[3]) << 24 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[1]) << 8 | p[0]; }
std::uint32_t syncsafe(const std::uint8_t* p) {
    return std::uint32_t(p[0] & 0x7F) << 21 | std::uint32_t(p[1] & 0x7F) << 14 | std::uint32_t(p[2] & 0x7F) << 7 | (p[3] & 0x7F);
}
std::vector<std::uint8_t> unsync(const std::uint8_t* d, std::size_t n) {   // FF 00 -> FF
    std::vector<std::uint8_t> o;
    o.reserve(n);
    for (std::size_t k = 0; k < n; ++k) {
        o.push_back(d[k]);
        if (d[k] == 0xFF && k + 1 < n && d[k + 1] == 0x00) ++k;
    }
    return o;
}

// An ID3v2 text frame's value (its encoding byte first): the first of its values, as UTF-8.
std::string id3_text(const std::uint8_t* d, std::size_t n) {
    if (n < 1) return {};
    const std::uint8_t enc = d[0];
    ++d, --n;
    std::string s;
    switch (enc) {
    case 0: {   // ISO-8859-1, NUL separated
        std::size_t e = 0;
        while (e < n && d[e]) ++e;
        s = latin1_to_utf8(d, e);
        break;
    }
    case 1:     // UTF-16 with a BOM (each value its own BOM in v2.4)
    case 2: {   // UTF-16BE
        bool be = enc == 2;
        if (enc == 1 && n >= 2) {
            if (d[0] == 0xFE && d[1] == 0xFF) { be = true; d += 2; n -= 2; }
            else if (d[0] == 0xFF && d[1] == 0xFE) { be = false; d += 2; n -= 2; }
        }
        std::size_t e = 0;
        while (e + 1 < n && (d[e] || d[e + 1])) e += 2;
        s = utf16_to_utf8(d, e, be);
        break;
    }
    case 3: {   // UTF-8
        std::size_t e = 0;
        while (e < n && d[e]) ++e;
        s.assign(reinterpret_cast<const char*>(d), e);
        break;
    }
    default:
        return {};
    }
    return trim(s);
}

bool iequal(const std::string& a, const char* b) {
    const std::size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (std::size_t k = 0; k < n; ++k)
        if (std::toupper(static_cast<unsigned char>(a[k])) != std::toupper(static_cast<unsigned char>(b[k]))) return false;
    return true;
}

std::vector<std::uint8_t> read_bytes(std::FILE* f, std::size_t n) {
    std::vector<std::uint8_t> v(n);
    v.resize(std::fread(v.data(), 1, n, f));
    return v;
}

}  // namespace

std::string latin1_to_utf8(const std::uint8_t* d, std::size_t n) {
    std::string o;
    for (std::size_t k = 0; k < n; ++k) put_utf8(o, d[k]);
    return o;
}

std::string utf16_to_utf8(const std::uint8_t* d, std::size_t n, bool big_endian) {
    std::string o;
    for (std::size_t k = 0; k + 1 < n; k += 2) {
        std::uint32_t c = big_endian ? (std::uint32_t(d[k]) << 8 | d[k + 1]) : (std::uint32_t(d[k + 1]) << 8 | d[k]);
        if (c >= 0xD800 && c < 0xDC00 && k + 3 < n) {
            const std::uint32_t lo = big_endian ? (std::uint32_t(d[k + 2]) << 8 | d[k + 3]) : (std::uint32_t(d[k + 3]) << 8 | d[k + 2]);
            if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); k += 2; }
        }
        put_utf8(o, c);
    }
    return o;
}

bool parse_id3v2(const std::uint8_t* d, std::size_t n, Tags& t, std::size_t* tag_bytes) {
    if (n < 10 || std::memcmp(d, "ID3", 3) != 0 || d[3] < 2 || d[3] > 4 || (d[6] | d[7] | d[8] | d[9]) & 0x80) return false;
    const unsigned ver = d[3];
    const std::uint8_t flags = d[5];
    const std::size_t size = syncsafe(d + 6);
    if (tag_bytes) *tag_bytes = 10 + size + ((ver == 4 && (flags & 0x10)) ? 10 : 0);
    std::vector<std::uint8_t> body(d + 10, d + 10 + std::min(size, n - 10));
    if (ver < 4 && (flags & 0x80)) body = unsync(body.data(), body.size());   // v2.2 / 2.3: the whole tag
    std::size_t p = 0;
    if (ver >= 3 && (flags & 0x40) && body.size() >= 4) {   // the extended header
        const std::size_t ext = ver == 3 ? be32(body.data()) + 4 : syncsafe(body.data());
        p = std::min(ext, body.size());
    }
    const std::size_t head = ver == 2 ? 6 : 10;
    while (p + head <= body.size()) {
        const std::uint8_t* f = body.data() + p;
        if (f[0] == 0) break;   // padding
        std::string id(reinterpret_cast<const char*>(f), ver == 2 ? 3 : 4);
        std::size_t len = ver == 2 ? (std::size_t(f[3]) << 16 | std::size_t(f[4]) << 8 | f[5]) : ver == 3 ? be32(f + 4) : syncsafe(f + 4);
        const std::uint16_t ff = ver == 2 ? 0 : std::uint16_t(f[8] << 8 | f[9]);
        p += head;
        if (len > body.size() - p) break;
        const std::uint8_t* v = body.data() + p;
        std::size_t vn = len;
        p += len;
        bool skip = false;
        std::vector<std::uint8_t> un;
        if (ver == 3) {
            if (ff & 0x00C0) skip = true;                  // compressed / encrypted
            if (ff & 0x0020) { if (vn < 1) continue; ++v; --vn; }   // grouping id
        } else if (ver == 4) {
            if (ff & 0x000C) skip = true;                  // compressed / encrypted
            if (ff & 0x0040) { if (vn < 1) continue; ++v; --vn; }   // grouping id
            if (ff & 0x0001) { if (vn < 4) continue; v += 4; vn -= 4; }   // data length indicator
            if ((ff & 0x0002) || (flags & 0x80)) { un = unsync(v, vn); v = un.data(); vn = un.size(); }
        }
        if (skip) continue;
        std::string* to = nullptr;
        if (id == "TIT2" || id == "TT2") to = &t.title;
        else if (id == "TPE1" || id == "TP1") to = &t.artist;
        else if (id == "TALB" || id == "TAL") to = &t.album;
        if (to && to->empty()) *to = id3_text(v, vn);
    }
    return true;
}

bool parse_id3v1(const std::uint8_t* d, std::size_t n, Tags& t) {
    if (n < 128 || std::memcmp(d, "TAG", 3) != 0) return false;
    auto field = [&](std::size_t at, std::size_t len) {
        std::size_t e = 0;
        while (e < len && d[at + e]) ++e;
        return trim(latin1_to_utf8(d + at, e));
    };
    if (t.title.empty()) t.title = field(3, 30);
    if (t.artist.empty()) t.artist = field(33, 30);
    if (t.album.empty()) t.album = field(63, 30);
    return true;
}

void apply_comment(const std::string& entry, Tags& t) {
    const std::size_t eq = entry.find('=');
    if (eq == std::string::npos) return;
    const std::string key = entry.substr(0, eq), val = trim(entry.substr(eq + 1));
    if (val.empty()) return;
    if (iequal(key, "TITLE") && t.title.empty()) t.title = val;
    else if (iequal(key, "ARTIST") && t.artist.empty()) t.artist = val;
    else if (iequal(key, "ALBUMARTIST") && t.artist.empty()) t.artist = val;
    else if (iequal(key, "ALBUM") && t.album.empty()) t.album = val;
}

bool parse_vorbis_comment(const std::uint8_t* d, std::size_t n, Tags& t) {
    if (n < 8) return false;
    std::size_t p = 0;
    const std::uint32_t vendor = le32(d);
    if (vendor > n - 8) return false;
    p = 4 + vendor;
    const std::uint32_t count = le32(d + p);
    p += 4;
    // an ARTIST seen after an ALBUMARTIST wins: two passes, ARTIST first
    std::vector<std::string> entries;
    for (std::uint32_t k = 0; k < count && p + 4 <= n; ++k) {
        const std::uint32_t len = le32(d + p);
        p += 4;
        if (len > n - p) return false;
        entries.emplace_back(reinterpret_cast<const char*>(d + p), len);
        p += len;
    }
    for (const auto& e : entries)
        if (e.size() < 12 || !iequal(e.substr(0, 12), "ALBUMARTIST=")) apply_comment(e, t);
    for (const auto& e : entries) apply_comment(e, t);
    return true;
}

bool parse_flac_tags(const std::uint8_t* d, std::size_t n, Tags& t) {
    if (n < 4 || std::memcmp(d, "fLaC", 4) != 0) return false;
    std::size_t p = 4;
    while (p + 4 <= n) {
        const bool last = (d[p] & 0x80) != 0;
        const unsigned type = d[p] & 0x7F;
        const std::size_t len = std::size_t(d[p + 1]) << 16 | std::size_t(d[p + 2]) << 8 | d[p + 3];
        p += 4;
        if (len > n - p) return false;
        if (type == 4) return parse_vorbis_comment(d + p, len, t);
        p += len;
        if (last) break;
    }
    return false;
}

void tags_from_filename(const std::string& path, Tags& t) {
    std::string name = ustr(upath(path).stem());
    // a leading track number ("07 ", "07. ", "07 - ", "1-07 ")
    std::size_t k = 0;
    while (k < name.size() && (std::isdigit(static_cast<unsigned char>(name[k])) || name[k] == '-') && k < 6) ++k;
    if (k > 0 && k < name.size() && std::isdigit(static_cast<unsigned char>(name[0]))) {
        std::size_t j = k;
        while (j < name.size() && (name[j] == ' ' || name[j] == '.' || name[j] == '_' || name[j] == '-')) ++j;
        if (j > k || name[k] == ' ') name = name.substr(j);
    }
    name = trim(name);
    const std::size_t dash = name.find(" - ");
    if (dash != std::string::npos) {
        if (t.artist.empty()) t.artist = trim(name.substr(0, dash));
        if (t.title.empty()) t.title = trim(name.substr(dash + 3));
    } else if (t.title.empty()) {
        t.title = name;
    }
}

std::u16string popup_text(const std::string& s, std::size_t max_units) {
    std::u16string o;
    for (std::size_t k = 0; k < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[k]);
        std::uint32_t c = b;
        std::size_t len = 1;
        if (b >= 0xF0 && k + 3 < s.size()) { c = (b & 0x07u) << 18 | (s[k + 1] & 0x3Fu) << 12 | (s[k + 2] & 0x3Fu) << 6 | (s[k + 3] & 0x3Fu); len = 4; }
        else if (b >= 0xE0 && k + 2 < s.size()) { c = (b & 0x0Fu) << 12 | (s[k + 1] & 0x3Fu) << 6 | (s[k + 2] & 0x3Fu); len = 3; }
        else if (b >= 0xC0 && k + 1 < s.size()) { c = (b & 0x1Fu) << 6 | (s[k + 1] & 0x3Fu); len = 2; }
        else if (b >= 0x80) c = '?';
        k += len;
        switch (c) {   // typographic marks the fonts lack [G]
        case 0x2018: case 0x2019: case 0x201B: case 0x2032: c = '\''; break;
        case 0x201C: case 0x201D: case 0x201F: case 0x2033: c = '"'; break;
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: c = '-'; break;
        case 0x2026: o += u"..."; continue;
        case 0x00A0: case 0x2002: case 0x2003: case 0x2009: c = ' '; break;
        default: break;
        }
        if (c < 0x20 || c > 0xFF) c = c == '\t' ? ' ' : '?';
        o.push_back(char16_t(c));
    }
    if (max_units >= 4 && o.size() > max_units) {
        o.resize(max_units - 3);
        while (!o.empty() && o.back() == u' ') o.pop_back();
        o += u"...";
    }
    return o;
}

bool is_audio_file(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = path.substr(dot);
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e == ".mp3" || e == ".flac" || e == ".ogg" || e == ".oga" || e == ".wav";
}

Tags read_tags(const std::string& path) {
    Tags t;
    std::string e = path.substr(std::min(path.size(), path.find_last_of('.')));
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (e == ".ogg" || e == ".oga") {
        read_ogg_comments(path, t);
    } else if (std::FILE* f = open_file(path)) {
        std::vector<std::uint8_t> head = read_bytes(f, 10);
        if (head.size() == 10 && std::memcmp(head.data(), "ID3", 3) == 0) {   // ID3v2 (MP3, also seen before FLAC / WAV data)
            std::size_t whole = 10 + syncsafe(head.data() + 6);
            whole = std::min<std::size_t>(whole, 4u << 20);   // cover art can be large; the text frames come first
            std::vector<std::uint8_t> tag = head;
            const std::vector<std::uint8_t> rest = read_bytes(f, whole - 10);
            tag.insert(tag.end(), rest.begin(), rest.end());
            parse_id3v2(tag.data(), tag.size(), t);
        } else if (head.size() >= 4 && std::memcmp(head.data(), "fLaC", 4) == 0) {
            std::fseek(f, 0, SEEK_SET);
            const std::vector<std::uint8_t> meta = read_bytes(f, 1u << 20);
            parse_flac_tags(meta.data(), meta.size(), t);
        }
        if ((t.title.empty() || t.artist.empty()) && e == ".mp3" && std::fseek(f, -128, SEEK_END) == 0) {
            const std::vector<std::uint8_t> v1 = read_bytes(f, 128);
            parse_id3v1(v1.data(), v1.size(), t);
        }
        std::fclose(f);
    }
    tags_from_filename(path, t);
    if (t.album.empty()) t.album = ustr(upath(path).parent_path().filename());
    return t;
}

std::vector<std::string> list_audio_files(const std::string& dir, std::string* why) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<std::string> files;
    std::size_t seen = 0;
    for (fs::recursive_directory_iterator it(upath(dir), fs::directory_options::skip_permission_denied | fs::directory_options::follow_directory_symlink, ec), end;
         !ec && it != end && seen < 200000; it.increment(ec), ++seen) {
        if (!it->is_regular_file(ec)) continue;
        const std::string p = ustr(it->path());
        if (is_audio_file(p)) files.push_back(p);
    }
    if (ec && why) *why = ec.message();
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<Track> scan_folder(const std::string& dir, std::string* why) {
    const std::vector<std::string> files = list_audio_files(dir, why);
    std::vector<Track> out;
    out.reserve(files.size());
    for (const std::string& p : files) out.push_back(Track{p, read_tags(p)});
    return out;
}

// ---- the resampler ----

void Resampler::reset(std::uint64_t base) {
    win_.clear();
    win_start_ = 0;
    end_ = 0;
    t_ = 0.0;
    base_ = base;
    ended_ = false;
}

float Resampler::at(std::int64_t k, int c) const {
    const std::int64_t i = k - win_start_;
    if (i < 0 || std::size_t(i) * 2 >= win_.size()) return 0.0f;
    return win_[std::size_t(i) * 2 + std::size_t(c)];
}

std::size_t Resampler::run(Decoder& src, double ratio, float* out, std::size_t frames) {
    std::size_t n = 0;
    float chunk[2 * 1024];
    while (n < frames) {
        if (ended_ && t_ >= double(end_)) break;
        const std::int64_t i = std::int64_t(std::floor(t_));
        while (!ended_ && win_start_ + std::int64_t(win_.size() / 2) <= i + 2) {   // x[i-1] .. x[i+2]
            const std::size_t got = src.read(chunk, 1024);
            if (got == 0) { ended_ = true; end_ = win_start_ + std::int64_t(win_.size() / 2); break; }
            win_.insert(win_.end(), chunk, chunk + 2 * got);
        }
        if (ended_ && t_ >= double(end_)) break;
        const float f = float(t_ - double(i));
        for (int c = 0; c < 2; ++c) {   // Catmull-Rom
            const float xm = at(i - 1, c), x0 = at(i, c), x1 = at(i + 1, c), x2 = at(i + 2, c);
            const float a = -0.5f * xm + 1.5f * x0 - 1.5f * x1 + 0.5f * x2;
            const float b = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float cc = -0.5f * xm + 0.5f * x1;
            out[2 * n + std::size_t(c)] = ((a * f + b) * f + cc) * f + x0;
        }
        ++n;
        t_ += ratio;
        // drop what lies before x[i-1]
        const std::int64_t keep = std::int64_t(std::floor(t_)) - 1;
        if (keep - win_start_ > 4096) {
            const std::size_t drop = std::size_t(keep - win_start_);
            win_.erase(win_.begin(), win_.begin() + std::ptrdiff_t(std::min(drop * 2, win_.size())));
            win_start_ += std::int64_t(drop);
        }
    }
    return n;
}

// ---- the player ----

bool Player::start(const Track& t, std::uint64_t at_frame, std::string* why) {
    close();
    dec_ = Decoder::open(t.path, why);
    if (!dec_) return false;
    if (at_frame && !dec_->seek(at_frame)) at_frame = 0, dec_->seek(0);
    rs_.reset(at_frame);
    track_ = t;
    finished_ = false;
    playing_ = paused_ = loop_ = false;
    gain_ = -1.0f;
    return true;
}

void Player::close() {
    dec_.reset();
    playing_ = paused_ = false;
    finished_ = false;
}

std::uint64_t Player::source_frame() const {
    const double p = rs_.source_position();
    return p > 0.0 ? std::uint64_t(p) : 0u;
}

std::uint32_t Player::position_ms() const {
    if (!dec_ || dec_->rate() == 0) return 0;
    return std::uint32_t(rs_.source_position() * 1000.0 / double(dec_->rate()));
}

void Player::mix(std::int16_t* lr, std::size_t frames) {
    if (!dec_ || !playing_ || paused_ || finished_ || frames == 0) return;
    tmp_.resize(frames * 2);
    const double ratio = double(dec_->rate()) / double(kOutputRate) * double(pitch_);
    std::size_t got = rs_.run(*dec_, ratio, tmp_.data(), frames);
    if (got < frames && loop_) {   // the game's looped segment (play mode "repeat")
        dec_->seek(0);
        rs_.reset(0);
        got += rs_.run(*dec_, ratio, tmp_.data() + 2 * got, frames - got);
    }
    if (got < frames && !loop_ && rs_.ended()) finished_ = true;
    // the gain moves to its target over this block (no zipper steps when the game fades or ducks the music)
    const float g0 = gain_ < 0.0f ? gain_target_ : gain_, g1 = gain_target_;
    for (std::size_t k = 0; k < got; ++k) {
        const float g = g0 + (g1 - g0) * (float(k + 1) / float(got));
        for (int c = 0; c < 2; ++c) {
            const float v = tmp_[2 * k + std::size_t(c)] * g * 32767.0f;
            const long add = std::lround(std::max(-65536.0f, std::min(65536.0f, v)));
            peak_ = std::max<std::uint64_t>(peak_, std::uint64_t(add < 0 ? -add : add));
            const long s = long(lr[2 * k + std::size_t(c)]) + add;
            lr[2 * k + std::size_t(c)] = std::int16_t(s < -32768 ? -32768 : (s > 32767 ? 32767 : s));
        }
    }
    gain_ = g1;
    mixed_ += got;
}

}  // namespace b3music
