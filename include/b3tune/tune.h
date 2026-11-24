// tune.h -- the tuning console's model (mod kit part 4, docs/modding/DESIGN.md §4, docs/modding/tuning.md): the
// registry of the game's tuning variables as the port records them, values and their ranges, `[tuning]` presets and
// the `tune` events that pad recordings and replay files carry. Header only, standard library only: native_play
// (tools/native/tuning_console.h, the guest side), the renderer (the DEVELOPER tab and the F9 overlay) and the tests
// (tests/test_tune.cpp) share it. No game values: every value here comes from guest RAM at run time or from a preset.
//
// The registry. The game registers each tuning variable with GtComms::CGtCommsDatabase::AddEntry (float 0x0021BB60,
// int 0x0021BD80, vector 0x0021B9B0; docs/modding/file-formats/tuning-registry.md): a guest address, a name, a group,
// a config file, a range for floats and ints, an element count. Registry::record keeps one Entry per (key, address) in
// first-registration order (the same order every run with the same inputs); a registration of the same variable
// again (the next race) counts on that entry.
//
// Names. A variable is named as b3modtool's VDB.XML text names it: "<group> | <name>", with its config file apart;
// one element of an array or a vector is "<group> | <name>[i]". The key is CGtHash(name + group + "/" + file)
// (vdb.md "The key"), written 0x<8 hex digits>. A per-vehicle variable (Physics, Camera ...) has one key per car model
// and the same "<group> | <name>" for all of them.
//
// Presets (a mod's `[tuning] preset = tuning/x.ini`). Lines `<group> | <name> = <value>`, optionally under a
// `[<config file>]` section as in b3modtool's text (`[]` or no section: any file), or `0x<key> = <value>`; either name
// may end in `[i]` (one element). Values: a float (`2.5`), an int (`3`), a vector `(x, y, z)` or `(x, y, z, w)`, an
// array `[a, b, ...]`, or `0x<raw 32-bit word>`. Comments: `#` or `;` at the start of a line, `#` after a value. Applied when a variable
// registers (after the game wrote its VDB.XML default) and once after the defaults are loaded
// (InitialiseDefaultValues 0x0024FE90). Precedence for one element: across mods the first mod in the load order that
// sets it wins; within one preset a key line beats a `[file]` line beats a plain name line, and of two equal ones the
// later line wins. A value outside the registered range is clamped (with a note).
//
// Edits (the console, or a script). An edit sets one element of every live variable with one key to one raw word at a
// VBlank boundary and stays for the session: a later registration of that key gets it too (after the preset). Each
// edit is an event `tune <vblank> 0x<key>[<element>] 0x<raw word>` (pad recordings: that line plus the name as a
// comment; replay files: a SETS entry with the key "tune" and the value "0x<key>[<element>] 0x<raw word>"), so a
// replay makes the same writes on the same VBlanks.
#ifndef B3TUNE_TUNE_H
#define B3TUNE_TUNE_H

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b3tune {

// ---- the game's side (the addresses and the registry entry's layout) ----
constexpr std::uint32_t kAddFloat = 0x0021BB60u, kAddInt = 0x0021BD80u, kAddVector = 0x0021B9B0u;
constexpr std::uint32_t kInitialiseDefaults = 0x0024FE90u;   // CGtCommsDatabaseGame::InitialiseDefaultValues
// CGtCommsDatabase (a0 of AddEntry): +4 the slot the last AddEntry took, +0xC the entries (32 bytes each), +0x10 the capacity
constexpr std::uint32_t kDbSlot = 0x4, kDbEntries = 0xC, kDbCapacity = 0x10;
// an entry: +0 variable, +4 max, +8 min, +0xC name, +0x10 group, +0x14 file, +0x18 key, +0x1C count, +0x1D flags, +0x1E type
constexpr std::uint32_t kEntrySize = 32;
enum class Type : std::uint8_t { kFree = 0, kInt = 1, kFloat = 2, kBool = 3, kVector = 5 };

// ---- CGtHash::CalculateHash 0x0021BFA0 (CRC-32's table, start -1, arithmetic shift, no final inversion) ----
inline std::int32_t gt_hash(std::string_view s) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::int32_t crc = -1;
    for (unsigned char c : s) {
        const std::uint32_t idx = (std::uint32_t(std::int32_t(std::int8_t(c))) ^ std::uint32_t(crc)) & 0xFFu;
        crc = std::int32_t(std::uint32_t(crc >> 8) ^ table[idx]);   // >> on a negative int: arithmetic (C++20)
    }
    return crc;
}
inline std::int32_t key_of(std::string_view name, std::string_view group, std::string_view file) {
    std::string s(name);
    s += group;
    if (group.empty() || group.back() != '/') s += '/';
    s += file;
    return gt_hash(s);
}

inline std::string hex8(std::uint32_t v) {
    char t[16];
    std::snprintf(t, sizeof t, "0x%08X", v);
    return t;
}
inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

// ---- one variable ----
struct Entry {
    std::size_t index = 0;            // first-registration order
    std::uint32_t entry_point = 0;    // kAddFloat / kAddInt / kAddVector
    std::uint32_t address = 0;        // the variable (guest)
    std::uint32_t db = 0, slot = 0;   // the game's registry and the slot of its latest registration
    Type type = Type::kFloat;
    std::uint32_t count = 0;          // the registration's count: >= 2 an array of that many words
    std::uint8_t flags = 0;           // bit 0: a range was given
    std::uint32_t min_word = 0, max_word = 0;   // raw (float or int bits)
    std::int32_t key = 0;
    std::string name, group, file;
    std::uint32_t registrations = 0;  // times registered
    bool dropped = false;             // its config file is disabled in VDB.XML (the game unregistered it at once)

    bool is_float() const { return type == Type::kFloat || type == Type::kVector; }
    bool ranged() const { return (flags & 1u) != 0 && type != Type::kVector; }
    // editable elements: an array's words, a vector's x y z (w is the authoring tool's fill), else 1
    std::uint32_t elements() const { return type == Type::kVector ? 3u : count >= 2 ? count : 1u; }
    std::uint32_t words() const { return type == Type::kVector ? 4u : count >= 2 ? count : 1u; }
    std::string label() const { return group + " | " + name; }
    std::string element_label(std::uint32_t e) const {
        if (type == Type::kVector) return label() + (e == 0 ? " x" : e == 1 ? " y" : e == 2 ? " z" : " w");
        if (count >= 2) return label() + "[" + std::to_string(e) + "]";
        return label();
    }
    const char* type_name() const {
        return type == Type::kInt ? "int" : type == Type::kFloat ? "float" : type == Type::kVector ? "vector" : type == Type::kBool ? "bool" : "?";
    }
};

// ---- values ----
inline float as_float(std::uint32_t w) { float f; std::memcpy(&f, &w, 4); return f; }
inline std::uint32_t float_word(float f) { std::uint32_t w; std::memcpy(&w, &f, 4); return w; }

// the shortest decimal that reads back to the same float (always with a '.' or an exponent); NaN / inf as the raw word
inline std::string format_float(std::uint32_t w) {
    const float f = as_float(w);
    if (!std::isfinite(f)) return hex8(w);
    char t[64];
    for (int p = 1; p <= 9; ++p) {
        std::snprintf(t, sizeof t, "%.*g", p, double(f));
        if (float_word(std::strtof(t, nullptr)) == w) break;
    }
    std::string s = t;
    const double a = std::fabs(double(f));
    if (s.find('e') != std::string::npos && a >= 1e-4 && a < 1e15)
        for (int d = 0; d <= 20; ++d) {
            std::snprintf(t, sizeof t, "%.*f", d, double(f));
            if (float_word(std::strtof(t, nullptr)) == w) { s = t; break; }
        }
    if (s.find_first_of(".en") == std::string::npos) s += ".0";
    return s;
}
inline std::string format_word(const Entry& e, std::uint32_t w) {
    return e.is_float() ? format_float(w) : std::to_string(std::int32_t(w));
}

// One number for an element of `e`: 0x<hex> the raw word, else a float (a float variable) or an int (an int variable;
// a float's text there is refused). false: *why says what is wrong.
inline bool parse_word(std::string_view tok, bool is_float, std::uint32_t& out, std::string* why = nullptr) {
    tok = trim(tok);
    if (tok.empty()) { if (why) *why = "a value is missing"; return false; }
    if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        if (tok.size() > 10) { if (why) *why = "'" + std::string(tok) + "' is more than 32 bits"; return false; }
        for (std::size_t i = 2; i < tok.size(); ++i)
            if (!std::isxdigit(static_cast<unsigned char>(tok[i]))) { if (why) *why = "'" + std::string(tok) + "' is not hex"; return false; }
        out = std::uint32_t(std::strtoul(std::string(tok.substr(2)).c_str(), nullptr, 16));
        return true;
    }
    const std::string s(tok);
    char* end = nullptr;
    if (is_float) {
        const float f = std::strtof(s.c_str(), &end);
        if (end != s.c_str() + s.size()) { if (why) *why = "'" + s + "' is not a number"; return false; }
        out = float_word(f);
        return true;
    }
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) { if (why) *why = "'" + s + "' is not a whole number (this variable is an int)"; return false; }
    if (v < -2147483648LL || v > 2147483647LL) { if (why) *why = "'" + s + "' does not fit 32 bits"; return false; }
    out = std::uint32_t(std::int32_t(v));
    return true;
}

// The registered range applied (floats and ints with flags bit 0; vectors have none). `clamped`: it moved.
inline std::uint32_t clamp_word(const Entry& e, std::uint32_t w, bool* clamped = nullptr) {
    if (clamped) *clamped = false;
    if (!e.ranged()) return w;
    if (e.type == Type::kFloat) {
        const float lo = as_float(e.min_word), hi = as_float(e.max_word), f = as_float(w);
        if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi) return w;
        if (std::isnan(f)) { if (clamped) *clamped = true; return e.min_word; }
        const float c = std::clamp(f, lo, hi);
        if (c != f && clamped) *clamped = true;
        return c != f ? float_word(c) : w;
    }
    const std::int32_t lo = std::int32_t(e.min_word), hi = std::int32_t(e.max_word), v = std::int32_t(w);
    if (lo > hi) return w;
    const std::int32_t c = std::clamp(v, lo, hi);
    if (c != v && clamped) *clamped = true;
    return std::uint32_t(c);
}

// The console's step from `w` (left / right: dir -1 / +1, times `mult` for a fast step): ints 1 (a range of more than
// 100: a hundredth of it); floats a hundredth of the range, or without one 1 % of the value's decade (0.01 at 0), so
// steps up and down retrace each other. The result is clamped.
inline std::uint32_t step_word(const Entry& e, std::uint32_t w, int dir, int mult = 1) {
    if (!dir) return w;
    if (!e.is_float()) {
        long long step = 1;
        if (e.ranged()) step = std::max<long long>(1, (static_cast<long long>(std::int32_t(e.max_word)) - std::int32_t(e.min_word)) / 100);
        const long long v = static_cast<long long>(std::int32_t(w)) + dir * step * mult;
        return clamp_word(e, std::uint32_t(std::int32_t(std::clamp<long long>(v, INT32_MIN, INT32_MAX))));
    }
    const float f = as_float(w);
    if (!std::isfinite(f)) return w;
    double step;
    if (e.ranged() && as_float(e.max_word) > as_float(e.min_word)) step = (double(as_float(e.max_word)) - as_float(e.min_word)) / 100.0;
    else {
        const double a = std::fabs(double(f));
        step = a < 1e-6 ? 0.01 : std::pow(10.0, std::floor(std::log10(a)) - 1.0);
    }
    double v = double(f) + dir * step * mult;
    v = std::round(v / step) * step;   // stay on the step's grid
    return clamp_word(e, float_word(float(v)));
}

// tunenav1: a fine step (the console's Ctrl / Alt, a controller's L2): a tenth of step_word's (ints: still 1)
inline std::uint32_t step_word_fine(const Entry& e, std::uint32_t w, int dir) {
    if (!dir) return w;
    if (!e.is_float()) return step_word(e, w, dir, 1);
    const float f = as_float(w);
    if (!std::isfinite(f)) return w;
    double step;
    if (e.ranged() && as_float(e.max_word) > as_float(e.min_word)) step = (double(as_float(e.max_word)) - as_float(e.min_word)) / 1000.0;
    else {
        const double a = std::fabs(double(f));
        step = a < 1e-6 ? 0.001 : std::pow(10.0, std::floor(std::log10(a)) - 2.0);
    }
    double v = double(f) + dir * step;
    v = std::round(v / step) * step;
    return clamp_word(e, float_word(float(v)));
}

// ---- the registry ----
// What one AddEntry left in the game's registry (read after it returned)
struct Registration {
    std::uint32_t entry_point = 0, db = 0, slot = 0, address = 0;
    Type type = Type::kFloat;
    std::uint32_t count = 0;
    std::uint8_t flags = 0;
    std::uint32_t min_word = 0, max_word = 0;
    std::int32_t key = 0;
    std::string name, group, file;
    bool dropped = false;
};
class Registry {
public:
    // One registration; returns the entry's index (new, or the existing (key, address) one: its slot and range updated)
    std::size_t record(const Registration& r) {
        ++calls_;
        if (r.dropped) ++dropped_calls_;
        const auto k = std::make_pair(r.key, r.address);
        auto it = by_var_.find(k);
        std::size_t i;
        if (it == by_var_.end()) {
            i = entries_.size();
            Entry e;
            e.index = i;
            e.key = r.key;
            e.address = r.address;
            e.name = r.name;
            e.group = r.group;
            e.file = r.file;
            entries_.push_back(std::move(e));
            by_var_.emplace(k, i);
            by_key_[r.key].push_back(i);
        } else {
            i = it->second;
        }
        Entry& e = entries_[i];
        e.entry_point = r.entry_point;
        e.db = r.db;
        e.slot = r.slot;
        e.type = r.type;
        e.count = r.count;
        e.flags = r.flags;
        e.min_word = r.min_word;
        e.max_word = r.max_word;
        e.dropped = r.dropped;
        ++e.registrations;
        return i;
    }
    const std::deque<Entry>& entries() const { return entries_; }   // a deque: an entry's strings never move
    const Entry& at(std::size_t i) const { return entries_[i]; }
    std::size_t size() const { return entries_.size(); }
    std::uint64_t calls() const { return calls_; }
    std::uint64_t dropped_calls() const { return dropped_calls_; }
    const std::vector<std::size_t>& with_key(std::int32_t key) const {
        static const std::vector<std::size_t> none;
        auto it = by_key_.find(key);
        return it == by_key_.end() ? none : it->second;
    }
    // every entry named "<group> | <name>" (any file), in registration order
    std::vector<std::size_t> with_label(std::string_view group, std::string_view name) const {
        std::vector<std::size_t> out;
        for (const Entry& e : entries_)
            if (e.group == group && e.name == name) out.push_back(e.index);
        return out;
    }

private:
    std::deque<Entry> entries_;
    std::map<std::pair<std::int32_t, std::uint32_t>, std::size_t> by_var_;
    std::map<std::int32_t, std::vector<std::size_t>> by_key_;
    std::uint64_t calls_ = 0, dropped_calls_ = 0;
};

// ---- names: "<group> | <name>[i]", "0x<key>[i]" ----
struct Target {
    bool by_key = false;
    std::int32_t key = 0;
    std::string group, name, file;   // file "": any
    int element = -1;                // -1: the whole variable
    bool matches(const Entry& e) const {
        if (by_key) return e.key == key;
        return e.group == group && e.name == name && (file.empty() || e.file == file);
    }
    int rank() const { return by_key ? 3 : !file.empty() ? 2 : 1; }
};
// `text` without a value; `file`: the section it is under ("" none). false: *why.
inline bool parse_target(std::string_view text, std::string_view file, Target& t, std::string* why = nullptr) {
    t = Target{};
    text = trim(text);
    if (!text.empty() && text.back() == ']') {   // an element
        const std::size_t open = text.rfind('[');
        if (open == std::string_view::npos) { if (why) *why = "']' without '['"; return false; }
        const std::string n(trim(text.substr(open + 1, text.size() - open - 2)));
        char* end = nullptr;
        const long v = std::strtol(n.c_str(), &end, 10);
        if (n.empty() || end != n.c_str() + n.size() || v < 0 || v > 255) { if (why) *why = "element [" + n + "] is not 0..255"; return false; }
        t.element = int(v);
        text = trim(text.substr(0, open));
    }
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') && text.find('|') == std::string_view::npos) {
        std::uint32_t w = 0;
        if (!parse_word(text, false, w, why)) return false;
        t.by_key = true;
        t.key = std::int32_t(w);
        return true;
    }
    const std::size_t bar = text.find(" | ");
    if (bar == std::string_view::npos) { if (why) *why = "'" + std::string(text) + "' is not \"<group> | <name>\" or 0x<key>"; return false; }
    t.group = std::string(trim(text.substr(0, bar)));
    t.name = std::string(trim(text.substr(bar + 3)));
    t.file = std::string(file);
    if (t.group.empty() || t.name.empty()) { if (why) *why = "an empty group or name"; return false; }
    for (unsigned char c : t.group + t.name + t.file)
        if (c >= 0x80) { if (why) *why = "a name with a character outside ASCII (the game's hash takes ASCII only)"; return false; }
    return true;
}

// A value text into words for `e` (all its words for a whole variable, one for an element): a scalar, (x, y, z[, w]),
// [a, b, ...]. A vector given as (x, y, z) keeps the variable's own w.
inline bool parse_value(std::string_view text, const Entry& e, int element, std::vector<std::pair<std::uint32_t, std::uint32_t>>& out,
                        std::string* why = nullptr) {
    out.clear();
    text = trim(text);
    const bool is_vec = e.type == Type::kVector, is_arr = e.count >= 2 && !is_vec;
    if (element >= 0) {
        if (std::uint32_t(element) >= e.elements()) { if (why) *why = "element [" + std::to_string(element) + "] past its " + std::to_string(e.elements()); return false; }
        std::uint32_t w = 0;
        if (!parse_word(text, e.is_float(), w, why)) return false;
        out.push_back({std::uint32_t(element), w});
        return true;
    }
    const char open = is_vec ? '(' : is_arr ? '[' : 0, close = is_vec ? ')' : ']';
    if (!open) {
        std::uint32_t w = 0;
        if (!parse_word(text, e.is_float(), w, why)) return false;
        out.push_back({0, w});
        return true;
    }
    if (text.size() < 2 || text.front() != open || text.back() != close) {
        if (why) *why = std::string(is_vec ? "a vector is written (x, y, z)" : "an array is written [a, b, ...]") + " (" + e.label() + ")";
        return false;
    }
    std::string_view body = text.substr(1, text.size() - 2);
    std::vector<std::uint32_t> ws;
    while (true) {
        const std::size_t c = body.find(',');
        std::uint32_t w = 0;
        if (!parse_word(body.substr(0, c), e.is_float(), w, why)) return false;
        ws.push_back(w);
        if (c == std::string_view::npos) break;
        body = body.substr(c + 1);
    }
    if (is_vec ? (ws.size() != 3 && ws.size() != 4) : ws.size() != e.count) {
        if (why) *why = e.label() + " has " + std::to_string(is_vec ? 3u : e.count) + " values, the text " + std::to_string(ws.size());
        return false;
    }
    for (std::size_t i = 0; i < ws.size(); ++i) out.push_back({std::uint32_t(i), ws[i]});
    return true;
}

// ---- presets ----
struct PresetLine {
    int line = 0;
    Target target;
    std::string value;   // the text after '=' (parsed for each variable it reaches: its type decides)
};
struct Preset {
    std::string mod;     // the mod's id
    std::string path;    // the file, as the manifest names it
    std::vector<PresetLine> lines;
    std::vector<std::string> errors;   // lines that are not usable (the rest is used)
};
inline Preset parse_preset(std::string_view text, std::string mod, std::string path = {}) {
    Preset p;
    p.mod = std::move(mod);
    p.path = std::move(path);
    std::string file;
    int n = 0;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view raw = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        ++n;
        if (n == 1 && raw.size() >= 3 && raw.substr(0, 3) == "\xEF\xBB\xBF") raw.remove_prefix(3);   // a UTF-8 BOM
        const std::string_view l = trim(raw);
        if (l.empty() || l[0] == '#' || l[0] == ';') continue;
        const std::string where = "line " + std::to_string(n) + ": ";
        if (l.front() == '[' && l.back() == ']' && l.find('=') == std::string_view::npos) {
            file = std::string(trim(l.substr(1, l.size() - 2)));
            continue;
        }
        if (l[0] == '@') continue;   // b3modtool's @version / @pad / @file lines: VDB.XML's own, not variables
        const std::size_t eq = l.find('=');
        if (eq == std::string_view::npos) { p.errors.push_back(where + "not \"<name> = <value>\""); continue; }
        PresetLine pl;
        pl.line = n;
        std::string why;
        if (!parse_target(l.substr(0, eq), file, pl.target, &why)) { p.errors.push_back(where + why); continue; }
        std::string_view val = l.substr(eq + 1);
        if (const std::size_t c = val.find('#'); c != std::string_view::npos) val = val.substr(0, c);   // a comment after the value
        pl.value = std::string(trim(val));
        if (pl.value.empty()) { p.errors.push_back(where + "no value"); continue; }
        p.lines.push_back(std::move(pl));
    }
    return p;
}

// What the presets set for one variable: per element the raw word (clamped), and where it came from
struct Applied {
    std::uint32_t element = 0;
    std::uint32_t word = 0;
    std::string mod;
    int line = 0;
    bool clamped = false;
};
// `presets` in load order (the first mod wins). `notes`: values that could not be used for this variable (the line's
// value does not fit its type) and clamps. The result is by element.
inline std::vector<Applied> preset_values(const std::vector<Preset>& presets, const Entry& e, std::vector<std::string>* notes = nullptr) {
    std::map<std::uint32_t, Applied> got;   // element -> the winner so far
    for (const Preset& p : presets) {
        std::map<std::uint32_t, std::pair<int, Applied>> mine;   // element -> (rank, value): this preset's best
        for (const PresetLine& pl : p.lines) {
            if (!pl.target.matches(e)) continue;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> ws;
            std::string why;
            if (!parse_value(pl.value, e, pl.target.element, ws, &why)) {
                if (notes) notes->push_back(p.mod + " " + p.path + " line " + std::to_string(pl.line) + ": " + why);
                continue;
            }
            for (const auto& [el, w] : ws) {
                if (el >= e.words()) continue;
                Applied a;
                a.element = el;
                a.mod = p.mod;
                a.line = pl.line;
                a.word = el < e.elements() || e.type != Type::kVector ? clamp_word(e, w, &a.clamped) : w;
                auto it = mine.find(el);
                if (it == mine.end() || pl.target.rank() >= it->second.first) mine[el] = {pl.target.rank(), a};
            }
        }
        for (const auto& [el, ra] : mine)
            if (!got.count(el)) {
                got[el] = ra.second;
                if (ra.second.clamped && notes)
                    notes->push_back(p.mod + " " + p.path + " line " + std::to_string(ra.second.line) + ": " + e.element_label(el) +
                                     " outside its range " + format_word(e, e.min_word) + " .. " + format_word(e, e.max_word) + ": clamped");
            }
    }
    std::vector<Applied> out;
    for (const auto& [el, a] : got) out.push_back(a);
    return out;
}

// ---- edits and their events ----
struct Event {
    std::uint64_t vblank = 0;
    std::int32_t key = 0;
    std::uint32_t element = 0;
    std::uint32_t word = 0;
    bool operator==(const Event&) const = default;
};
// "0x<key>[<element>] 0x<word>" (a replay file's SETS value)
inline std::string event_value(const Event& ev) {
    return hex8(std::uint32_t(ev.key)) + "[" + std::to_string(ev.element) + "] " + hex8(ev.word);
}
inline bool parse_event_value(std::string_view s, Event& ev) {
    s = trim(s);
    const std::size_t open = s.find('['), close = s.find(']'), sp = s.find(' ', close == std::string_view::npos ? 0 : close);
    if (open == std::string_view::npos || close == std::string_view::npos || close < open || sp == std::string_view::npos) return false;
    std::uint32_t k = 0, w = 0;
    if (!parse_word(s.substr(0, open), false, k) || !(s.size() > 2 && s[0] == '0')) return false;
    const std::string n(s.substr(open + 1, close - open - 1));
    char* end = nullptr;
    const unsigned long el = std::strtoul(n.c_str(), &end, 10);
    if (n.empty() || end != n.c_str() + n.size() || el > 255) return false;
    const std::string_view wt = trim(s.substr(sp + 1));
    const std::size_t wsp = wt.find(' ');
    if (!parse_word(wt.substr(0, wsp), false, w) || wt.substr(0, 2) != "0x") return false;
    ev.key = std::int32_t(k);
    ev.element = std::uint32_t(el);
    ev.word = w;
    return true;
}
// A pad recording's line: "tune <vblank> 0x<key>[<element>] 0x<word>  # <label> = <value>" (the comment for people)
inline std::string event_line(const Event& ev, std::string_view comment = {}) {
    std::string l = "tune " + std::to_string(ev.vblank) + " " + event_value(ev);
    if (!comment.empty()) l += "  # " + std::string(comment);
    return l;
}
inline bool parse_event_line(std::string_view l, Event& ev) {
    l = trim(l);
    if (l.substr(0, 5) != "tune ") return false;
    l.remove_prefix(5);
    const std::size_t sp = l.find(' ');
    if (sp == std::string_view::npos) return false;
    const std::string v(l.substr(0, sp));
    char* end = nullptr;
    const unsigned long long vb = std::strtoull(v.c_str(), &end, 10);
    if (v.empty() || end != v.c_str() + v.size()) return false;
    std::string_view rest = l.substr(sp + 1);
    if (const std::size_t hash = rest.find('#'); hash != std::string_view::npos) rest = rest.substr(0, hash);
    if (!parse_event_value(rest, ev)) return false;
    ev.vblank = vb;
    return true;
}

// ---- the player's car (tunenav1: the console's YOUR CAR page, docs/modding/tuning.md "The car page") ----
// Edits of the player's race car itself (not tuning variables): events like the others, with a key of their own
// (kCarKeyBase | field; element 0), so pad recordings and replay files carry them and a replay writes them on the
// same VBlanks. native_play (tools/native/tuning_console.h) writes them on the game thread at the VBlank, only while a
// race is on; they are never part of the session's overrides or a saved preset.
namespace car {
enum Field : std::uint32_t {
    kBoost = 1,           // the boost in the bar (float, clamped to the bar's size)
    kHealth = 2,          // the health (float, 0 .. 1)
    kBoostRate = 3,       // boost used per second (float, >= 0)
    kRepair = 16,         // the health to 1.0 (an action: the word is ignored)
    kFillBoost = 17,      // the boost to the bar's size (an action)
    kFreezeDamage = 18,   // 1 on / 0 off: the health is not let go down (held each VBlank)
    kInfiniteBoost = 19,  // 1 on / 0 off: the game's infinite boost flag held set each VBlank
};
constexpr std::uint32_t kKeyBase = 0xB3CA0000u;
inline bool is_field(std::uint32_t f) {
    return f == kBoost || f == kHealth || f == kBoostRate || f == kRepair || f == kFillBoost || f == kFreezeDamage || f == kInfiniteBoost;
}
inline bool is_key(std::int32_t key) { return (std::uint32_t(key) & 0xFFFF0000u) == kKeyBase && is_field(std::uint32_t(key) & 0xFFFFu); }
inline std::int32_t key_of(Field f) { return std::int32_t(kKeyBase | std::uint32_t(f)); }
inline Field field_of(std::int32_t key) { return Field(std::uint32_t(key) & 0xFFFFu); }
inline const char* field_name(Field f) {
    switch (f) {
    case kBoost: return "boost";
    case kHealth: return "health";
    case kBoostRate: return "boost use rate";
    case kRepair: return "repair";
    case kFillBoost: return "fill boost";
    case kFreezeDamage: return "freeze damage";
    case kInfiniteBoost: return "infinite boost";
    }
    return "?";
}
// The addresses [C]: gWorld 0x00522660; its race car count at +0x12AD1C and the cars' pointers at +0x12AD28 (a race
// is on: 1 to 6 cars, the first the player's: src/b3replay/capture.cpp decode_tick, src/native/script_mods.h); view 0's
// race car (CB3RaceCar) at +0x12AD50 = 0x0064D3B0 (capture.h kPlayerRaceCar).
constexpr std::uint32_t kWorld = 0x00522660u;
constexpr std::uint32_t kRaceCarCount = kWorld + 0x12AD1Cu, kRaceCarList = kWorld + 0x12AD28u, kPlayerCar = kWorld + 0x12AD50u;
// The car's CB3Burn at +0x118C (the boost: decomp B3OfflineSingleRaceLogic.cpp AddBurn(car + 0x118C), OnCrashStart's
// RemoveBurn(car + 0x118C, car->field_11C0)): +0x34 the bar's size, +0x38 the boost now, +0x40 the rate used per
// second (CB3Burn::Update: boost -= dt x rate while boosting), +0x50 the infinite boost flag (SetInfiniteBoost
// 0x0014DDE0; Update keeps the bar full while it is set), +0x52 boosting [L field names: CB3Burn::Update 0x0014E3A0,
// SetNumExtraBoostBarSegments 0x0014DFF0, StartBurn 0x0014DEF0].
constexpr std::uint32_t kBurn = 0x118Cu;
constexpr std::uint32_t kBoostMax = kBurn + 0x34u, kBoostNow = kBurn + 0x38u, kBoostRateOff = kBurn + 0x40u, kInfiniteFlag = kBurn + 0x50u,
                        kBoosting = kBurn + 0x52u;
// The health: the car's CB3Score (+0x10C0) +0x5F4 = car +0x16B4, 1.0 full (CB3Score::DecrementHealth 0x00156220 takes
// from it down to 0; CB3StageLogic::ResetCarDamage 0x00395A00 and the Road Rage logic write 1.0) [C decomp].
constexpr std::uint32_t kHealthOff = 0x16B4u;
// The speedometer's source for player 1: the W lane of a vector at 0x0055EF70 + 0x1CB80 + 0xB0 (m/s), shown times the
// factor at 0x004E0798 (units word 0x004E2918 <= 1: mph) or 0x004E0960 (km/h) [L src/native/script_mods.h kSpeedVector].
constexpr std::uint32_t kSpeedW = 0x0055EF70u + 0x1CB80u + 0xB0u + 0xCu;
constexpr std::uint32_t kSpeedUnits = 0x004E2918u, kSpeedFactorLow = 0x004E0798u, kSpeedFactorHigh = 0x004E0960u;

// The writes, on any memory `m` with load32 / store32 / load8 / store8 (native_play: guest RAM; tests: a toy).
// State: the toggles a session turned on (a replay turns them on and off on the same VBlanks).
struct State {
    bool freeze = false, infinite = false;
    float held = -1;   // FREEZE DAMAGE: the health held (-1: none yet / no race)
    // tunenav2 (the CHANGED page, reset): the boost use rate as the console set it, and the game's before it. Changed
    // while the car still has the console's rate (the game sets its own again when the bar changes size, and a new
    // race starts with its own: then it is the game's again). Read only: hold() writes nothing for it.
    bool rate_set = false;
    float rate_game = 0, rate_written = 0;
};
template <class Mem>
bool race_on(Mem& m) {
    const std::uint32_t n = m.load32(kRaceCarCount);
    return n >= 1 && n <= 8 && m.load32(kRaceCarList) == kPlayerCar;   // (8: MORE RIVALS' races)
}
// One event: what it did, for the log ("boost = 240.0"; `wrote` false: no race now, nothing written)
template <class Mem>
std::string apply(State& s, Mem& m, std::int32_t key, std::uint32_t word, bool& wrote) {
    const Field f = field_of(key);
    const bool on = race_on(m);
    const std::uint32_t c = kPlayerCar;
    auto get = [&](std::uint32_t a) { float v; const std::uint32_t w = m.load32(a); std::memcpy(&v, &w, 4); return v; };
    auto put = [&](std::uint32_t a, float v) { std::uint32_t w; std::memcpy(&w, &v, 4); m.store32(a, w); };
    auto text = [](float v) {
        char t[32];
        std::snprintf(t, sizeof t, "%.6g", double(v));
        return std::string(t);
    };
    float in;
    std::memcpy(&in, &word, 4);
    std::string what = field_name(f);
    wrote = false;
    switch (f) {
    case kBoost:
    case kFillBoost:
        if (on) {
            const float mx = std::max(get(c + kBoostMax), 0.0f), want = f == kFillBoost ? mx : in;
            const float v = std::isfinite(want) ? std::clamp(want, 0.0f, mx) : 0.0f;
            put(c + kBoostNow, v);
            what += " = " + text(v);
            wrote = true;
        }
        break;
    case kBoostRate:
        if (on) {
            const float v = std::isfinite(in) ? std::max(in, 0.0f) : 0.0f;
            if (!s.rate_set) s.rate_game = get(c + kBoostRateOff);   // the game's own, before the first change
            s.rate_written = v;
            std::uint32_t vw, gw;
            std::memcpy(&vw, &v, 4);
            std::memcpy(&gw, &s.rate_game, 4);
            s.rate_set = vw != gw;   // set back to the game's: not changed any more
            put(c + kBoostRateOff, v);
            what += " = " + text(v);
            wrote = true;
        }
        break;
    case kHealth:
    case kRepair:
        if (on) {
            const float want = f == kRepair ? 1.0f : in;
            const float v = std::isfinite(want) ? std::clamp(want, 0.0f, 1.0f) : 1.0f;
            put(c + kHealthOff, v);
            if (s.freeze) s.held = v;
            what += " = " + text(v);
            wrote = true;
        }
        break;
    case kFreezeDamage:
        s.freeze = word != 0;
        s.held = s.freeze && on ? get(c + kHealthOff) : -1.0f;
        what += s.freeze ? " on" : " off";
        wrote = true;
        break;
    case kInfiniteBoost:
        s.infinite = word != 0;
        if (on) m.store8(c + kInfiniteFlag, s.infinite ? 1 : 0);
        what += s.infinite ? " on" : " off";
        wrote = true;
        break;
    }
    return what;
}
// Each VBlank (after the edits): FREEZE DAMAGE keeps the health from going down (raised with the game's own repairs,
// a new race's full health), INFINITE BOOST keeps the game's flag set (the game clears it at a crash)
template <class Mem>
void hold(State& s, Mem& m) {
    if (!s.freeze && !s.infinite && !s.rate_set) return;
    if (!race_on(m)) { s.held = -1; s.rate_set = false; return; }
    const std::uint32_t c = kPlayerCar;
    if (s.rate_set) {   // tunenav2: the game set its own rate again (read only)
        std::uint32_t ww;
        std::memcpy(&ww, &s.rate_written, 4);
        if (m.load32(c + kBoostRateOff) != ww) s.rate_set = false;
    }
    if (s.freeze) {
        const std::uint32_t w = m.load32(c + kHealthOff);
        float h;
        std::memcpy(&h, &w, 4);
        if (s.held < 0 || h > s.held) s.held = h;
        else if (h < s.held) {
            std::uint32_t hw;
            std::memcpy(&hw, &s.held, 4);
            m.store32(c + kHealthOff, hw);
        }
    }
    if (s.infinite && m.load8(c + kInfiniteFlag) == 0) m.store8(c + kInfiniteFlag, 1);
}
}  // namespace car

// The events to play, by VBlank (a recording's, or a script's once resolved): take() hands out those at or before v
class EventQueue {
public:
    void add(const Event& ev) { ev_.push_back(ev); sorted_ = false; }
    std::vector<Event> take(std::uint64_t v) {
        if (!sorted_) { std::stable_sort(ev_.begin() + std::ptrdiff_t(i_), ev_.end(), [](const Event& a, const Event& b) { return a.vblank < b.vblank; }); sorted_ = true; }
        std::vector<Event> out;
        while (i_ < ev_.size() && ev_[i_].vblank <= v) out.push_back(ev_[i_++]);
        return out;
    }
    std::size_t size() const { return ev_.size(); }
    std::size_t pending() const { return ev_.size() - i_; }

private:
    std::vector<Event> ev_;
    std::size_t i_ = 0;
    bool sorted_ = true;
};

// A script's edit (native_play --tune / B3NATIVE_TUNE): "<vblank>:<name>=<value>;..." with the names of a preset
// (resolved against the variables registered at that VBlank)
struct ScriptEdit {
    std::uint64_t vblank = 0;
    Target target;
    std::string value;
};
inline std::vector<ScriptEdit> parse_script(std::string_view s, std::vector<std::string>* errors = nullptr) {
    std::vector<ScriptEdit> out;
    while (!s.empty()) {
        const std::size_t semi = s.find(';');
        const std::string_view item = trim(s.substr(0, semi));
        s = semi == std::string_view::npos ? std::string_view() : s.substr(semi + 1);
        if (item.empty()) continue;
        const std::size_t colon = item.find(':'), eq = item.rfind('=');
        ScriptEdit e;
        std::string why;
        if (colon == std::string_view::npos || eq == std::string_view::npos || eq < colon) {
            if (errors) errors->push_back("'" + std::string(item) + "' is not <vblank>:<name>=<value>");
            continue;
        }
        const std::string v(item.substr(0, colon));
        char* end = nullptr;
        e.vblank = std::strtoull(v.c_str(), &end, 10);
        if (v.empty() || end != v.c_str() + v.size()) { if (errors) errors->push_back("'" + v + "' is not a VBlank"); continue; }
        if (!parse_target(item.substr(colon + 1, eq - colon - 1), {}, e.target, &why)) { if (errors) errors->push_back(why); continue; }
        e.value = std::string(trim(item.substr(eq + 1)));
        out.push_back(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(), [](const ScriptEdit& a, const ScriptEdit& b) { return a.vblank < b.vblank; });
    return out;
}

// The session's edits: (key, element) -> word, the last one per element. Applied to a variable when it registers
// again (after its preset), so an edit holds for the rest of the session.
class Overrides {
public:
    void set(std::int32_t key, std::uint32_t element, std::uint32_t word) { m_[{key, element}] = word; }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> of(std::int32_t key) const {   // (element, word)
        std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
        for (auto it = m_.lower_bound({key, 0u}); it != m_.end() && it->first.first == key; ++it) out.push_back({it->first.second, it->second});
        return out;
    }
    const std::map<std::pair<std::int32_t, std::uint32_t>, std::uint32_t>& all() const { return m_; }
    bool empty() const { return m_.empty(); }
    // tunenav2: the session's word for one element, if it has one
    std::optional<std::uint32_t> get(std::int32_t key, std::uint32_t element) const {
        const auto it = m_.find({key, element});
        return it == m_.end() ? std::nullopt : std::optional<std::uint32_t>(it->second);
    }

private:
    std::map<std::pair<std::int32_t, std::uint32_t>, std::uint32_t> m_;
};

// tunenav2 (the console's CHANGED page and its resets): each variable's default, the value the game gave it after the
// active mods' presets, taken at its registration and after InitialiseDefaultValues, before the session's edits are
// written over it (native_play: tools/native/tuning_console.h). An element is changed while it has an edit and its
// value differs from its default (a value the game itself changes, never edited, is not); a variable not registered
// now counts with the edit's word, kept for when it registers again. A reset is ordinary edits with the defaults'
// words: recorded and replayed as any other.
class Defaults {
public:
    static constexpr std::size_t kAll = ~std::size_t(0);
    // entry i's words now (e.words() of them)
    void capture(std::size_t i, const Entry& e, const std::uint32_t* words) {
        if (d_.size() <= i) d_.resize(i + 1);
        Def& d = d_[i];
        d.words.assign(words, words + e.words());
        d.known = true;
    }
    bool known(std::size_t i) const { return i < d_.size() && d_[i].known; }
    std::uint32_t word(std::size_t i, std::uint32_t el) const { return known(i) && el < d_[i].words.size() ? d_[i].words[el] : 0u; }
    // element el of entry i; `now`: its value (nullopt: not registered now); *cur: what counts as its value
    bool changed(const Registry& reg, const Overrides& o, std::size_t i, std::uint32_t el, std::optional<std::uint32_t> now,
                 std::uint32_t* cur = nullptr) const {
        if (!known(i) || i >= reg.size() || el >= reg.at(i).elements() || el >= d_[i].words.size()) return false;
        const std::optional<std::uint32_t> ov = o.get(reg.at(i).key, el);
        if (!ov) return false;
        const std::uint32_t c = now ? *now : *ov;
        if (cur) *cur = c;
        return c != d_[i].words[el];
    }
    // The edits that put back element `element` of entry `index` (-1: each of its elements), or (index kAll) every
    // changed element: one event per key and element (an edit reaches every entry with the key). now(i, el): entry i's
    // value (nullopt: not registered now).
    template <class Now>
    std::vector<Event> resets(const Registry& reg, const Overrides& o, std::uint64_t v, std::size_t index, int element, Now now) const {
        std::vector<Event> out;
        if (index == kAll) {
            for (const auto& [ke, word] : o.all()) {
                (void)word;
                for (std::size_t i : reg.with_key(ke.first))
                    if (changed(reg, o, i, ke.second, now(i, ke.second))) {
                        out.push_back({v, ke.first, ke.second, d_[i].words[ke.second]});
                        break;
                    }
            }
            return out;
        }
        if (index >= reg.size()) return out;
        const Entry& e = reg.at(index);
        for (std::uint32_t el = 0; el < e.elements(); ++el)
            if ((element < 0 || el == std::uint32_t(element)) && changed(reg, o, index, el, now(index, el)))
                out.push_back({v, e.key, el, d_[index].words[el]});
        return out;
    }
    // the session's edits without those back at their default (SAVE AS PRESET writes what still changes something)
    Overrides kept(const Registry& reg, const Overrides& o) const {
        Overrides k;
        for (const auto& [ke, word] : o.all()) {
            bool at_default = false;
            for (std::size_t i : reg.with_key(ke.first))
                if (known(i)) { at_default = ke.second < d_[i].words.size() && d_[i].words[ke.second] == word; break; }
            if (!at_default) k.set(ke.first, ke.second, word);
        }
        return k;
    }

private:
    struct Def {
        bool known = false;
        std::vector<std::uint32_t> words;
    };
    std::vector<Def> d_;
};

// The edits as a preset file's text ("Save as preset"): one line per edited element, by its name and config file
// (a [file] section per config file), with the key in a comment
inline std::string overrides_to_preset(const Overrides& o, const Registry& reg, std::string_view title) {
    std::map<std::string, std::vector<std::string>> by_file;   // file -> lines
    for (const auto& [ke, word] : o.all()) {
        const auto& idx = reg.with_key(ke.first);
        if (idx.empty()) {
            by_file[""].push_back(hex8(std::uint32_t(ke.first)) + "[" + std::to_string(ke.second) + "] = " + hex8(word));
            continue;
        }
        const Entry& e = reg.at(idx.front());
        std::string name = e.label();
        if (e.elements() > 1 || e.type == Type::kVector) name += "[" + std::to_string(ke.second) + "]";
        by_file[e.file].push_back(name + " = " + format_word(e, word) + "   # " + hex8(std::uint32_t(e.key)));
    }
    std::string t = "# " + std::string(title) + "\n# Made by the tuning console (PC SETTINGS > DEVELOPER > SAVE AS PRESET).\n"
                    "# Lines: <group> | <name> = <value> under [<config file>]; docs/modding/tuning.md\n";
    for (const auto& [file, lines] : by_file) {
        t += "\n[" + file + "]\n";
        for (const std::string& l : lines) t += l + "\n";
    }
    return t;
}

}  // namespace b3tune

#endif
