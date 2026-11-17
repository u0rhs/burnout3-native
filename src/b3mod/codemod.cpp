// codemod.cpp -- see include/b3mod/codemod.h
#include "b3mod/codemod.h"

#include "../native/sha256.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace b3mod {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}
std::string lower(std::string_view s) {
    std::string o(s);
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return o;
}
bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream o;
    o << f.rdbuf();
    out = o.str();
    return true;
}
bool write_text(const fs::path& path, const std::string& text, std::string* why) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !(f << text)) {
            if (why) *why = "cannot write " + path_utf8(tmp);
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        if (why) *why = "cannot replace " + path_utf8(path) + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}
template <class F>
void for_lines(std::string_view text, F&& f) {
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        f(text.substr(at, end - at));
        at = end + 1;
    }
}
bool parse_uint(std::string_view v, std::uint64_t& out) {
    v = trim(v);
    if (v.empty() || v.size() > 18) return false;
    std::uint64_t n = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + std::uint64_t(c - '0');
    }
    out = n;
    return true;
}
std::string esc(std::string_view s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '|') o += "\\p";
        else if (c == ';') o += "\\s";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') continue;
        else o += c;
    }
    return o;
}
std::string unesc(std::string_view s) {
    std::string o;
    for (std::size_t k = 0; k < s.size(); ++k) {
        if (s[k] != '\\' || k + 1 == s.size()) { o += s[k]; continue; }
        const char n = s[++k];
        o += n == 'p' ? '|' : n == 's' ? ';' : n == 'n' ? '\n' : n;
    }
    return o;
}
// splits on `sep` outside escapes (the escapes stay in the parts)
std::vector<std::string_view> split_raw(std::string_view s, char sep) {
    std::vector<std::string_view> v;
    std::size_t a = 0;
    for (std::size_t k = 0; k <= s.size(); ++k) {
        if (k < s.size() && s[k] == '\\') { ++k; continue; }
        if (k == s.size() || s[k] == sep) { v.push_back(s.substr(a, k - a)); a = k + 1; }
    }
    return v;
}

}  // namespace

// ================================================================================================ rows
bool valid_setting_key(std::string_view key) {
    if (key.empty() || key.size() > 32) return false;
    for (char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

std::string check_row(const SettingRow& r) {
    if (!valid_setting_key(r.key)) return "key \"" + r.key + "\": a-z, 0-9, '_', '-', '.' (1 to 32)";
    if (r.label.empty() || r.label.size() > 40) return r.key + ": a label of 1 to 40 characters";
    if (r.description.size() > 400) return r.key + ": a description of up to 400 characters";
    switch (r.kind) {
    case SettingKind::kToggle:
        if (r.count != 2 || r.values.size() != 2) return r.key + ": a toggle has two values";
        break;
    case SettingKind::kChoice:
        if (r.count < 2 || r.count > 16 || r.values.size() != r.count) return r.key + ": a choice has 2 to 16 named values";
        for (const std::string& v : r.values)
            if (v.empty() || v.size() > 24) return r.key + ": a choice's value names have 1 to 24 characters";
        break;
    case SettingKind::kSlider:
        if (r.count < 2 || r.count > 101) return r.key + ": a slider has 2 to 101 levels";
        break;
    default: return r.key + ": not a toggle, a choice or a slider";
    }
    if (r.def >= r.count) return r.key + ": the default " + std::to_string(r.def) + " is not one of its " + std::to_string(r.count) + " values";
    return {};
}

std::string value_text(const SettingRow& r, int index) {
    if (index < 0 || unsigned(index) >= r.count) return {};
    if (r.kind == SettingKind::kSlider) return std::to_string(index);
    return std::size_t(index) < r.values.size() ? r.values[std::size_t(index)] : std::to_string(index);
}

int value_from_text(const SettingRow& r, std::string_view text) {
    text = trim(text);
    if (r.kind != SettingKind::kSlider)
        for (std::size_t k = 0; k < r.values.size(); ++k)
            if (lower(r.values[k]) == lower(text)) return int(k);
    std::uint64_t n = 0;
    if (parse_uint(text, n) && n < r.count) return int(n);
    return -1;
}

std::string row_to_line(const SettingRow& r) {
    std::string vals;
    for (std::size_t k = 0; k < r.values.size(); ++k) vals += (k ? ";" : "") + esc(r.values[k]);
    return esc(r.mod) + "|" + esc(r.key) + "|" + std::to_string(int(r.kind)) + "|" + std::to_string(r.count) + "|" + std::to_string(r.def) + "|" +
           std::to_string(r.value) + "|" + esc(r.label) + "|" + esc(r.description) + "|" + vals;
}

bool row_from_line(std::string_view line, SettingRow& r) {
    const std::vector<std::string_view> f = split_raw(trim(line), '|');
    if (f.size() != 9) return false;
    std::uint64_t kind = 0, count = 0, def = 0, value = 0;
    if (!parse_uint(f[2], kind) || kind > 2 || !parse_uint(f[3], count) || !parse_uint(f[4], def) || !parse_uint(f[5], value)) return false;
    r = SettingRow{};
    r.mod = unesc(f[0]);
    r.key = unesc(f[1]);
    r.kind = static_cast<SettingKind>(kind);
    r.count = unsigned(count);
    r.def = unsigned(def);
    r.value = int(value);
    r.label = unesc(f[6]);
    r.description = unesc(f[7]);
    if (!f[8].empty())
        for (std::string_view v : split_raw(f[8], ';')) r.values.push_back(unesc(v));
    return valid_id(r.mod) && check_row(r).empty() && r.value >= 0 && unsigned(r.value) < r.count;
}

std::string rows_to_text(const std::vector<SettingRow>& rows) {
    std::string t;
    for (const SettingRow& r : rows) t += row_to_line(r) + "\n";
    return t;
}

std::vector<SettingRow> rows_from_text(std::string_view text) {
    std::vector<SettingRow> v;
    for_lines(text, [&](std::string_view l) {
        SettingRow r;
        if (!trim(l).empty() && row_from_line(l, r)) v.push_back(std::move(r));
    });
    return v;
}

// ================================================================================================ values files
fs::path settings_folder(const fs::path& pref) { return pref / "mod-settings"; }
fs::path settings_file(const fs::path& pref, const std::string& id) { return settings_folder(pref) / path_from_utf8(id + ".ini"); }

std::map<std::string, std::string> load_values(const fs::path& file) {
    std::map<std::string, std::string> m;
    std::string text;
    if (!read_text(file, text)) return m;
    for_lines(text, [&](std::string_view raw) {
        std::string_view s = trim(raw);
        if (s.empty() || s[0] == '#' || s[0] == ';' || s[0] == '[') return;
        const std::size_t eq = s.find('=');
        if (eq == std::string_view::npos) return;
        const std::string key = lower(trim(s.substr(0, eq)));
        std::string_view v = trim(s.substr(eq + 1));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        if (valid_setting_key(key)) m.emplace(key, std::string(v));
    });
    return m;
}

bool save_values(const fs::path& file, const std::string& id, const std::vector<SettingRow>& rows, std::string* why) {
    std::string t = "# Burnout 3 port: the settings of the code mod " + id +
                    " (PC SETTINGS > MODS in the game, or the launcher's Mods page, write this file).\n[settings]\n";
    for (const SettingRow& r : rows)
        if (r.mod == id) t += r.key + " = " + value_text(r, r.value) + "\n";
    return write_text(file, t, why);
}

// ================================================================================================ live settings
bool parse_set_spec(std::string_view spec, std::vector<SettingSet>& out, std::string* why) {
    for (std::string_view item : split_raw(spec, ',')) {
        item = trim(item);
        if (item.empty()) continue;
        SettingSet s;
        const std::size_t colon = item.find(':'), eq = item.find('=');
        if (colon != std::string_view::npos && colon < eq) {
            if (!parse_uint(item.substr(0, colon), s.vblank)) { if (why) *why = "\"" + std::string(item) + "\": the VBlank is not a number"; return false; }
            item.remove_prefix(colon + 1);
        }
        const std::size_t eq2 = item.find('=');
        if (eq2 == std::string_view::npos) { if (why) *why = "\"" + std::string(item) + "\": <mod>.<key>=<value>"; return false; }
        const std::string_view name = trim(item.substr(0, eq2));
        const std::size_t dot = name.find('.');
        if (dot == std::string_view::npos || !valid_id(name.substr(0, dot)) || !valid_setting_key(name.substr(dot + 1))) {
            if (why) *why = "\"" + std::string(name) + "\": not <mod id>.<setting key>";
            return false;
        }
        s.mod = std::string(name.substr(0, dot));
        s.key = std::string(name.substr(dot + 1));
        s.value = std::string(trim(item.substr(eq2 + 1)));
        out.push_back(std::move(s));
    }
    return true;
}

std::string set_line(const SettingSet& s) {
    return "set " + std::to_string(s.vblank) + " " + s.mod + "." + s.key + " " + s.value;
}

bool set_from_line(std::string_view line, SettingSet& s) {
    line = trim(line);
    if (line.substr(0, 4) != "set ") return false;
    line.remove_prefix(4);
    const std::size_t a = line.find(' ');
    if (a == std::string_view::npos || !parse_uint(line.substr(0, a), s.vblank)) return false;
    line.remove_prefix(a + 1);
    const std::size_t b = line.find(' ');
    const std::string_view name = line.substr(0, b);
    const std::size_t dot = name.find('.');
    if (dot == std::string_view::npos || !valid_id(name.substr(0, dot)) || !valid_setting_key(name.substr(dot + 1))) return false;
    s.mod = std::string(name.substr(0, dot));
    s.key = std::string(name.substr(dot + 1));
    s.value = b == std::string_view::npos ? std::string() : std::string(line.substr(b + 1));
    return true;
}

int LiveSettings::add(const SettingRow& row) {
    if (find(row.mod, row.key) >= 0) return -1;
    rows_.push_back(row);
    return int(rows_.size()) - 1;
}

void LiveSettings::remove_mod(const std::string& mod) {
    rows_.erase(std::remove_if(rows_.begin(), rows_.end(), [&](const SettingRow& r) { return r.mod == mod; }), rows_.end());
}

int LiveSettings::find(std::string_view mod, std::string_view key) const {
    for (std::size_t k = 0; k < rows_.size(); ++k)
        if (rows_[k].mod == mod && rows_[k].key == key) return int(k);
    return -1;
}

void LiveSettings::play(std::vector<SettingSet> events) {
    playing_ = true;
    requests_.clear();
    timed_ = std::move(events);
    std::stable_sort(timed_.begin(), timed_.end(), [](const SettingSet& a, const SettingSet& b) { return a.vblank < b.vblank; });
    timed_at_ = 0;
}

bool LiveSettings::request(std::string_view mod, std::string_view key, std::string_view text) {
    if (playing_) return false;
    const int i = find(mod, key);
    if (i < 0 || value_from_text(rows_[std::size_t(i)], text) < 0) return false;
    requests_.push_back({0, std::string(mod), std::string(key), std::string(text)});
    return true;
}

void LiveSettings::schedule(std::vector<SettingSet> sets) {
    if (playing_) return;
    std::vector<SettingSet> rest(timed_.begin() + std::ptrdiff_t(timed_at_), timed_.end());
    rest.insert(rest.end(), sets.begin(), sets.end());
    std::stable_sort(rest.begin(), rest.end(), [](const SettingSet& a, const SettingSet& b) { return a.vblank < b.vblank; });
    timed_ = std::move(rest);
    timed_at_ = 0;
}

bool LiveSettings::apply(const SettingSet& s, std::vector<SettingSet>& out) {
    const int i = find(s.mod, s.key);
    if (i < 0) return false;
    SettingRow& r = rows_[std::size_t(i)];
    const int v = value_from_text(r, s.value);
    if (v < 0 || v == r.value) return false;
    r.value = v;
    SettingSet done = s;
    done.value = value_text(r, v);   // the canonical text (a recording names the value as files do)
    out.push_back(std::move(done));
    return true;
}

std::vector<SettingSet> LiveSettings::boundary(std::uint64_t v) {
    std::vector<SettingSet> out;
    for (SettingSet& s : requests_) {
        s.vblank = v;
        apply(s, out);
    }
    requests_.clear();
    for (; timed_at_ < timed_.size() && timed_[timed_at_].vblank <= v; ++timed_at_) {
        SettingSet s = timed_[timed_at_];
        s.vblank = v;
        apply(s, out);
    }
    return out;
}

std::vector<SettingSet> LiveSettings::snapshot(std::uint64_t v) const {
    std::vector<SettingSet> out;
    for (const SettingRow& r : rows_) out.push_back({v, r.mod, r.key, value_text(r, r.value)});
    return out;
}

// ================================================================================================ trust
fs::path trust_file(const fs::path& pref) { return pref / "mods-trusted.ini"; }

std::string library_digest(const fs::path& library) {
    std::string bytes;
    if (!read_text(library, bytes)) return {};
    return b3native::sha256_hex(b3native::sha256(bytes.data(), bytes.size()));
}

bool trusted(const fs::path& pref, const std::string& digest) {
    if (digest.size() != 64) return false;
    std::string text;
    if (!read_text(trust_file(pref), text)) return false;
    bool found = false;
    for_lines(text, [&](std::string_view raw) {
        const std::string_view s = trim(raw);
        const std::size_t eq = s.find('=');
        if (eq != std::string_view::npos && lower(trim(s.substr(0, eq))) == digest) found = true;
    });
    return found;
}

bool add_trust(const fs::path& pref, const std::string& digest, const std::string& id, const std::string& version, std::string* why) {
    if (digest.size() != 64) { if (why) *why = "no library digest"; return false; }
    if (trusted(pref, digest)) return true;
    std::string text;
    if (!read_text(trust_file(pref), text) || text.empty())
        text = "# Burnout 3 port: the code mods' libraries you allowed to run (the launcher asks once per library).\n"
               "# A library that changes (a new version of the mod) is asked about again. Delete a line to be asked again.\n[trusted]\n";
    if (text.back() != '\n') text += '\n';
    text += digest + " = " + id + " " + version + "\n";
    return write_text(trust_file(pref), text, why);
}

// ================================================================================================ state
fs::path state_file(const fs::path& pref) { return pref / "mods-state.ini"; }

std::string state_to_ini(const std::vector<ModState>& s) {
    std::string t = "# Burnout 3 port: what the last start did with each code mod (written by the game; the launcher shows it).\n";
    for (const ModState& m : s) {
        t += "[mod " + m.id + "]\nstatus = " + (m.loaded ? "loaded" : "not loaded") + "\n";
        if (!m.reason.empty()) t += "reason = " + esc(m.reason) + "\n";
        if (!m.digest.empty()) t += "library = " + m.digest + "\n";
        for (const SettingRow& r : m.rows) t += "row = " + row_to_line(r) + "\n";
    }
    return t;
}

std::vector<ModState> state_from_ini(std::string_view text) {
    std::vector<ModState> v;
    for_lines(text, [&](std::string_view raw) {
        const std::string_view s = trim(raw);
        if (s.empty() || s[0] == '#') return;
        if (s.size() > 6 && s.substr(0, 5) == "[mod " && s.back() == ']') {
            ModState m;
            m.id = std::string(trim(s.substr(5, s.size() - 6)));
            if (valid_id(m.id)) v.push_back(std::move(m));
            return;
        }
        if (v.empty()) return;
        const std::size_t eq = s.find('=');
        if (eq == std::string_view::npos) return;
        const std::string key(trim(s.substr(0, eq)));
        const std::string_view val = trim(s.substr(eq + 1));
        ModState& m = v.back();
        if (key == "status") m.loaded = val == "loaded";
        else if (key == "reason") m.reason = unesc(val);
        else if (key == "library") m.digest = std::string(val);
        else if (key == "row") {
            SettingRow r;
            if (row_from_line(val, r) && r.mod == m.id) m.rows.push_back(std::move(r));
        }
    });
    return v;
}

bool save_state(const fs::path& pref, const std::vector<ModState>& s, std::string* why) { return write_text(state_file(pref), state_to_ini(s), why); }

std::vector<ModState> load_state(const fs::path& pref) {
    std::string text;
    if (!read_text(state_file(pref), text)) return {};
    return state_from_ini(text);
}

}  // namespace b3mod
