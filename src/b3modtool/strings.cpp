// The game's string tables (DATA/GLOBALUS.BIN, HEADUS.BIN) to UTF-8 text and back (docs/modding/b3modtool.md
// "strings").
#include "b3modtool/modtool.h"

#include <cstdio>
#include <cstring>

namespace b3modtool {

namespace {

constexpr std::uint8_t kMagic[8] = {0x08, 0x03, 0xF6, 0x69, 0xFA, 0x22, 0x36, 0xB9};

bool fail(std::string* why, std::string what) { if (why) *why = std::move(what); return false; }
std::uint32_t get32(const std::uint8_t* p) { return std::uint32_t(p[0] | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24); }
std::string hex32(std::uint32_t v) { char b[12]; std::snprintf(b, sizeof b, "%X", v); return b; }
void put32(Buf& o, std::size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) o[at + i] = std::uint8_t(v >> (8 * i)); }

void utf8_put(std::string& o, std::uint32_t c) {
    if (c < 0x80) o += char(c);
    else if (c < 0x800) { o += char(0xC0 | c >> 6); o += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += char(0xE0 | c >> 12); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
    else { o += char(0xF0 | c >> 18); o += char(0x80 | ((c >> 12) & 0x3F)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
}
void hex4(std::string& o, unsigned u) {
    static const char* d = "0123456789ABCDEF";
    o += "\\u";
    for (int k = 12; k >= 0; k -= 4) o += d[(u >> k) & 0xF];
}

std::string escape(const std::u16string& s) {
    std::string o;
    std::size_t end = s.size();
    while (end > 0 && s[end - 1] == u' ') --end;   // trailing spaces: escaped (editors strip them)
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char16_t u = s[i];
        if (i >= end) { hex4(o, u); continue; }
        switch (u) {
        case u'\\': o += "\\\\"; continue;
        case u'\n': o += "\\n"; continue;
        case u'\r': o += "\\r"; continue;
        case u'\t': o += "\\t"; continue;
        default: break;
        }
        if (u < 0x20 || u == 0x7F || (u >= 0xFFFE)) { hex4(o, u); continue; }
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF && i + 1 < end) {
            utf8_put(o, 0x10000 + ((std::uint32_t(u) - 0xD800) << 10) + (std::uint32_t(s[i + 1]) - 0xDC00));
            ++i;
            continue;
        }
        if (u >= 0xD800 && u <= 0xDFFF) { hex4(o, u); continue; }   // an unpaired surrogate
        utf8_put(o, u);
    }
    return o;
}

// One line's text after "= " -> UTF-16. false: why names the problem.
bool unescape(std::string_view t, std::u16string& out, std::string& why) {
    out.clear();
    for (std::size_t i = 0; i < t.size();) {
        const unsigned char c = static_cast<unsigned char>(t[i]);
        if (c == '\\') {
            if (i + 1 >= t.size()) { why = "a '\\' at the end of the line (write \\\\ for a backslash)"; return false; }
            const char e = t[i + 1];
            if (e == '\\') out += u'\\';
            else if (e == 'n') out += u'\n';
            else if (e == 'r') out += u'\r';
            else if (e == 't') out += u'\t';
            else if (e == 'u') {
                if (i + 6 > t.size()) { why = "\\u needs four hex digits"; return false; }
                unsigned v = 0;
                for (std::size_t k = i + 2; k < i + 6; ++k) {
                    const char h = t[k];
                    const int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                    if (d < 0) { why = "\\u needs four hex digits"; return false; }
                    v = v << 4 | unsigned(d);
                }
                if (!v) { why = "\\u0000 cannot be in a string (it ends it)"; return false; }
                out += char16_t(v);
                i += 6;
                continue;
            } else { why = std::string("unknown escape \\") + e + " (write \\\\ for a backslash)"; return false; }
            i += 2;
            continue;
        }
        std::uint32_t cp;
        std::size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { why = "not UTF-8 text (save the file as UTF-8)"; return false; }
        if (i + len > t.size()) { why = "not UTF-8 text (save the file as UTF-8)"; return false; }
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char d = static_cast<unsigned char>(t[i + k]);
            if ((d & 0xC0) != 0x80) { why = "not UTF-8 text (save the file as UTF-8)"; return false; }
            cp = cp << 6 | (d & 0x3F);
        }
        const std::uint32_t min = len == 1 ? 0 : len == 2 ? 0x80 : len == 3 ? 0x800 : 0x10000;
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { why = "not UTF-8 text (save the file as UTF-8)"; return false; }
        if (cp == 0) { why = "a NUL character cannot be in a string"; return false; }
        if (cp >= 0x10000) {
            out += char16_t(0xD800 + ((cp - 0x10000) >> 10));
            out += char16_t(0xDC00 + ((cp - 0x10000) & 0x3FF));
        } else {
            out += char16_t(cp);
        }
        i += len;
    }
    return true;
}

}  // namespace

bool is_string_table(const std::uint8_t* p, std::size_t n) { return n >= 16 && std::memcmp(p, kMagic, 8) == 0; }

bool read_strings(const Buf& f, StringTable& out, std::string* why) {
    out = StringTable{};
    if (!is_string_table(f.data(), f.size())) return fail(why, "not a string table (no 08 03 F6 69 FA 22 36 B9 header)");
    std::memcpy(out.magic, f.data(), 8);
    const std::uint32_t n = get32(&f[8]), to = get32(&f[12]);
    out.table_offset = to;
    if (to < 16 || to > f.size() || n > (f.size() - to) / 4) return fail(why, "the offset table is outside the file");
    std::size_t expect = to + std::size_t(n) * 4;
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t o = get32(&f[to + 4 * i]);
        // the packing import writes: each string right after the one before (checked: then write_strings gives the
        // file's own bytes back)
        if (o != expect) return fail(why, "string " + std::to_string(i) + " is not where the table packing puts it (offset 0x" +
                                              hex32(o) + "); this table cannot be rebuilt byte for byte");
        std::u16string s;
        std::size_t p = o;
        for (;; p += 2) {
            if (p + 2 > f.size()) return fail(why, "string " + std::to_string(i) + " runs past the end of the file");
            const char16_t u = char16_t(f[p] | f[p + 1] << 8);
            if (!u) break;
            s += u;
        }
        expect = p + 2;
        out.s.push_back(std::move(s));
    }
    if (expect != f.size()) return fail(why, std::to_string(f.size() - expect) + " bytes after the last string; this table cannot be rebuilt byte for byte");
    return true;
}

Buf write_strings(const StringTable& t) {
    const std::size_t to = t.table_offset < 16 ? 16 : t.table_offset;
    Buf o(to + 4 * t.s.size(), 0);
    std::memcpy(o.data(), t.magic, 8);
    put32(o, 8, std::uint32_t(t.s.size()));
    put32(o, 12, std::uint32_t(to));
    for (std::size_t i = 0; i < t.s.size(); ++i) {
        put32(o, to + 4 * i, std::uint32_t(o.size()));
        for (char16_t u : t.s[i]) { o.push_back(std::uint8_t(u)); o.push_back(std::uint8_t(u >> 8)); }
        o.push_back(0);
        o.push_back(0);
    }
    return o;
}

std::string strings_to_text(const StringTable& t, const std::string& source) {
    std::string o;
    o += "# b3modtool strings: " + source + ", " + std::to_string(t.s.size()) + " strings\n";
    o += "# One string per line: <id> = <text> (UTF-8). Escapes: \\n new line, \\r, \\t, \\\\ a backslash, \\uXXXX a\n";
    o += "# character by its UTF-16 code (trailing spaces are written \\u0020). Lines starting with # are comments.\n";
    o += "# To import, keep only the lines you change: the ids you leave out keep the game's text.\n";
    for (std::size_t i = 0; i < t.s.size(); ++i) o += std::to_string(i) + " = " + escape(t.s[i]) + "\n";
    return o;
}

bool strings_from_text(std::string_view text, StringTable& base, std::string* why, std::size_t* changed) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);   // a UTF-8 byte order mark
    std::vector<std::u16string> s = base.s;
    std::vector<bool> seen(s.size(), false);
    std::size_t line_no = 0, diff = 0;
    const std::size_t old_n = base.s.size();
    while (!text.empty()) {
        ++line_no;
        const std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);   // CRLF files
        std::size_t k = 0;
        while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
        if (k == line.size() || line[k] == '#') continue;
        const auto where = [&](const std::string& what) { return fail(why, "line " + std::to_string(line_no) + ": " + what); };
        std::size_t id = 0, digits = 0;
        while (k < line.size() && line[k] >= '0' && line[k] <= '9' && digits < 9) { id = id * 10 + std::size_t(line[k] - '0'); ++k; ++digits; }
        if (!digits) return where("expected \"<id> = <text>\"");
        while (k < line.size() && line[k] == ' ') ++k;
        if (k >= line.size() || line[k] != '=') return where("expected \"<id> = <text>\"");
        ++k;
        if (k < line.size() && line[k] == ' ') ++k;
        std::u16string u;
        std::string e;
        if (!unescape(line.substr(k), u, e)) return where(e);
        if (id >= s.size()) {
            if (id != s.size()) return where("id " + std::to_string(id) + " leaves a gap (the table has " + std::to_string(s.size()) + " strings; new ids continue it)");
            s.emplace_back();
            seen.push_back(false);
        }
        if (seen[id]) return where("id " + std::to_string(id) + " given twice");
        seen[id] = true;
        if (id >= old_n || s[id] != u) ++diff;
        s[id] = std::move(u);
    }
    base.s = std::move(s);
    if (changed) *changed = diff;
    return true;
}

}  // namespace b3modtool
