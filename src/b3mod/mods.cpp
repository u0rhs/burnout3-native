// mods.cpp -- see include/b3mod/mods.h
#include "b3mod/mods.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace b3mod {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}
char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }
std::string lower(std::string_view s) {
    std::string o(s);
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return o;
}
std::string utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}
fs::path from_utf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool parse_int(std::string_view v, int& out) {
    v = trim(v);
    if (v.empty()) return false;
    bool neg = false;
    std::size_t i = 0;
    if (v[0] == '-' || v[0] == '+') { neg = v[0] == '-'; i = 1; }
    if (i >= v.size()) return false;
    long long n = 0;
    for (; i < v.size(); ++i) {
        if (v[i] < '0' || v[i] > '9') return false;
        n = n * 10 + (v[i] - '0');
        if (n > 1000000000) return false;
    }
    out = static_cast<int>(neg ? -n : n);
    return true;
}

// One "key = value" line of a tiny ini: comments (# ;), [sections], a value in double quotes keeps its spaces.
struct IniLine {
    enum Kind { kNone, kSection, kPair, kBad } kind = kNone;
    std::string section, key, value;
};
IniLine ini_line(std::string_view raw) {
    IniLine l;
    std::string_view s = trim(raw);
    if (s.empty() || s[0] == '#' || s[0] == ';') return l;
    if (s[0] == '[') {
        const std::size_t close = s.find(']');
        if (close == std::string_view::npos) { l.kind = IniLine::kBad; return l; }
        l.kind = IniLine::kSection;
        l.section = lower(trim(s.substr(1, close - 1)));
        return l;
    }
    const std::size_t eq = s.find('=');
    if (eq == std::string_view::npos) { l.kind = IniLine::kBad; return l; }
    l.kind = IniLine::kPair;
    l.key = lower(trim(s.substr(0, eq)));
    std::string_view v = trim(s.substr(eq + 1));
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    l.value = std::string(v);
    return l;
}
template <class F>
void for_lines(std::string_view text, F&& f) {
    std::size_t at = 0;
    int n = 0;
    while (at <= text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        f(text.substr(at, end - at), ++n);
        at = end + 1;
    }
}

bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream o;
    o << f.rdbuf();
    out = o.str();
    return true;
}

// Names a mod's overlay never takes: the file managers' own files (so a mod hashes the same on every system)
bool ignored_name(const std::string& name) {
    if (name.empty() || name[0] == '.') return true;
    const std::string l = lower(name);
    return l == "thumbs.db" || l == "desktop.ini";
}

bool inside(const fs::path& base, const fs::path& p) {   // both canonical
    auto b = base.begin(), e = base.end();
    auto q = p.begin();
    for (; b != e; ++b, ++q) {
        if (q == p.end() || *q != *b) return false;
    }
    return true;
}

}  // namespace

// ================================================================================================ the manifest
bool valid_id(std::string_view id) {
    if (id.empty() || id.size() > 64 || id[0] == '.') return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

ParsedManifest parse_manifest(std::string_view text) {
    ParsedManifest r;
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);   // a UTF-8 BOM (Notepad's "UTF-8 with BOM")
    Manifest& m = r.m;
    std::string section;
    bool have_mod = false, have_api = false, have_version = false, have_name = false;
    std::set<std::string> seen;
    for_lines(text, [&](std::string_view raw, int n) {
        const IniLine l = ini_line(raw);
        const std::string where = "line " + std::to_string(n);
        if (l.kind == IniLine::kNone) return;
        if (l.kind == IniLine::kBad) { r.warnings.push_back(where + ": not \"key = value\" or [section]: ignored"); return; }
        if (l.kind == IniLine::kSection) {
            section = l.section;
            if (section == "mod") have_mod = true;
            else if (section == "code") m.code.present = true;   // sdk2b
            else if (section == "script") m.script.present = true;   // lua6
            else if (section != "files" && section != "requires" && section != "textures" && section != "tuning") r.warnings.push_back(where + ": section [" + section + "] is not used by this port (api " + std::to_string(kApiVersion) + ")");
            return;
        }
        const std::string full = section + "." + l.key;
        if (!seen.insert(full).second) { r.warnings.push_back(where + ": " + l.key + " given again: the first one counts"); return; }
        if (section == "mod") {
            if (l.key == "id") m.id = l.value;
            else if (l.key == "name") { m.name = l.value; have_name = true; }
            else if (l.key == "version") { m.version = l.value; have_version = true; }
            else if (l.key == "author") m.author = l.value;
            else if (l.key == "description") m.description = l.value;
            else if (l.key == "api") {
                have_api = true;
                if (!parse_int(l.value, m.api) || m.api < 1) r.errors.push_back(where + ": api \"" + l.value + "\" is not a version number (1, 2, ...)");
            } else if (l.key == "priority") {
                if (!parse_int(l.value, m.priority)) r.errors.push_back(where + ": priority \"" + l.value + "\" is not a whole number");
            } else r.warnings.push_back(where + ": [mod] " + l.key + " is not known: ignored");
        } else if (section == "files") {
            if (l.key == "root") m.files_root = l.value;
            else if (l.key == "patches") m.files_patches = l.value;
            else r.warnings.push_back(where + ": [files] " + l.key + " is not known: ignored");
        } else if (section == "textures") {
            if (l.key == "pack") m.textures = l.value;
            else r.warnings.push_back(where + ": [textures] " + l.key + " is not known: ignored");
        } else if (section == "tuning") {   // tune4: a tuning preset (docs/modding/tuning.md)
            if (l.key == "preset") m.tuning_preset = l.value;
            else r.warnings.push_back(where + ": [tuning] " + l.key + " is not known: ignored");
        } else if (section == "requires") {   // another mod's id = its least version ("" or "*": any)
            std::string v = l.value;
            if (v == "*") v.clear();
            if (!valid_id(l.key)) r.errors.push_back(where + ": [requires] \"" + l.key + "\" is not a mod id");
            else if (v.find_first_of(" \t") != std::string::npos) r.errors.push_back(where + ": [requires] " + l.key + ": version \"" + v + "\" has spaces");
            else m.requirements.push_back({l.key, v});
        } else if (section == "code") {   // sdk2b: the library per system, the author's statements
            auto yes_no = [&](bool& out) {
                const std::string v = lower(l.value);
                if (v == "yes" || v == "1" || v == "true" || v == "on") out = true;
                else if (v == "no" || v == "0" || v == "false" || v == "off" || v.empty()) out = false;
                else r.errors.push_back(where + ": [code] " + l.key + " = \"" + l.value + "\": yes or no");
            };
            if (l.key == "windows" || l.key == "linux" || l.key == "macos") {
                if (!safe_relative(l.value)) r.errors.push_back(where + ": [code] " + l.key + " \"" + l.value + "\" is not a file inside the mod (no '..', no absolute path)");
                else (l.key == "windows" ? m.code.windows_path : l.key == "linux" ? m.code.linux_path : m.code.macos_path) = l.value;
            } else if (l.key == "gameplay") yes_no(m.code.gameplay);
            else if (l.key == "platform") yes_no(m.code.platform);
            else r.warnings.push_back(where + ": [code] " + l.key + " is not known: ignored");
        } else if (section == "script") {   // lua6: the script, the author's statement, the limits
            if (l.key == "main") {
                if (!safe_relative(l.value)) r.errors.push_back(where + ": [script] main \"" + l.value + "\" is not a file inside the mod (no '..', no absolute path)");
                else if (l.value.find('\\') != std::string::npos) r.errors.push_back(where + ": [script] main \"" + l.value + "\": use '/' between folders");
                else if (l.value.find('/') == std::string::npos) r.errors.push_back(where + ": [script] main \"" + l.value + "\": put the script in a folder of the mod (scripts/main.lua)");
                else if (l.value.size() < 5 || lower(l.value.substr(l.value.size() - 4)) != ".lua") r.errors.push_back(where + ": [script] main \"" + l.value + "\" is not a .lua file");
                else m.script.main_file = l.value;
            } else if (l.key == "gameplay") {
                const std::string v = lower(l.value);
                if (v == "yes" || v == "1" || v == "true" || v == "on") m.script.gameplay = true;
                else if (v == "no" || v == "0" || v == "false" || v == "off") m.script.gameplay = false;
                else r.errors.push_back(where + ": [script] gameplay = \"" + l.value + "\": yes or no");
                m.script.gameplay_given = true;
            } else if (l.key == "budget") {
                int n = 0;
                if (!parse_int(l.value, n) || n < int(ScriptSpec::kMinBudget) || std::uint64_t(n) > ScriptSpec::kMaxBudget)
                    r.errors.push_back(where + ": [script] budget \"" + l.value + "\": Lua instructions per VBlank, " + std::to_string(ScriptSpec::kMinBudget) + " to " +
                                       std::to_string(ScriptSpec::kMaxBudget));
                else m.script.budget = std::uint64_t(n);
            } else if (l.key == "unsafe") {   // modsec: b3.call and b3.mem.write, with the user's permission
                const std::string v = lower(l.value);
                if (v == "yes" || v == "1" || v == "true" || v == "on") m.script.unsafe = true;
                else if (v == "no" || v == "0" || v == "false" || v == "off") m.script.unsafe = false;
                else r.errors.push_back(where + ": [script] unsafe = \"" + l.value + "\": yes or no");
            } else if (l.key == "calls") {   // modsec: the game functions b3.call may call, "0x00151780, 0x001519F0"
                std::string tok;
                bool bad = false;
                auto flush = [&] {
                    if (tok.empty()) return;
                    std::uint64_t v = 0;
                    const bool hexa = tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X');
                    std::size_t k = hexa ? 2 : 0;
                    for (; k < tok.size() && !bad; ++k) {
                        const char c = tok[k];
                        const int d = c >= '0' && c <= '9' ? c - '0' : hexa && c >= 'a' && c <= 'f' ? c - 'a' + 10 : hexa && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                        if (d < 0) bad = true;
                        v = v * (hexa ? 16 : 10) + std::uint64_t(d);
                        if (v > 0xFFFFFFFFull) bad = true;
                    }
                    if (!bad) m.script.calls.push_back(std::uint32_t(v));
                    tok.clear();
                };
                for (char c : l.value) {
                    if (c == ',' || c == ' ' || c == '\t') flush();
                    else tok += c;
                }
                flush();
                if (bad) r.errors.push_back(where + ": [script] calls \"" + l.value + "\": the game functions' addresses (0x00151780, ...), separated by commas");
                else if (m.script.calls.size() > ScriptSpec::kMaxCalls) r.errors.push_back(where + ": [script] calls: more than " + std::to_string(ScriptSpec::kMaxCalls) + " functions");
            } else if (l.key == "memory") {
                int n = 0;
                if (!parse_int(l.value, n) || n < 1 || unsigned(n) > ScriptSpec::kMaxMemoryMb)
                    r.errors.push_back(where + ": [script] memory \"" + l.value + "\": megabytes, 1 to " + std::to_string(ScriptSpec::kMaxMemoryMb));
                else m.script.memory_mb = unsigned(n);
            } else r.warnings.push_back(where + ": [script] " + l.key + " is not known: ignored");
        } else if (section.empty()) {
            r.warnings.push_back(where + ": " + l.key + " before any [section]: ignored");
        }
    });
    for (const Requirement& q : m.requirements)
        if (q.id == m.id) r.errors.push_back("[requires] names the mod itself");
    if (!have_mod) r.errors.push_back("no [mod] section");
    if (m.id.empty()) r.errors.push_back("no id (the [mod] section's id = ...)");
    else if (!valid_id(m.id)) r.errors.push_back("id \"" + m.id + "\": only a-z, 0-9, '.', '_' and '-' (up to 64, not starting with '.')");
    if (!have_api) {
        m.api = 1;
        r.warnings.push_back("no api version: read as api 1 (add api = " + std::to_string(kApiVersion) + ")");
    } else if (m.api > kApiVersion) {
        r.errors.push_back("made for mod api " + std::to_string(m.api) + "; this port has api " + std::to_string(kApiVersion) + ": a newer port is needed");
    }
    if (!have_version || m.version.empty()) {
        m.version = "0";
        r.warnings.push_back("no version: read as 0");
    } else if (m.version.find_first_of(" \t") != std::string::npos) {
        r.errors.push_back("version \"" + m.version + "\" has spaces");
    }
    if (m.code.present) {   // sdk2b: a code mod needs api 2 and a library for at least one system
        if (m.api < kCodeApi && m.api <= kApiVersion)
            r.errors.push_back("[code] needs api = " + std::to_string(kCodeApi) + " (the code-mod API; this mod says api " + std::to_string(m.api) + ")");
        if (m.code.windows_path.empty() && m.code.linux_path.empty() && m.code.macos_path.empty())
            r.errors.push_back("[code] names no library (windows = ..., linux = ..., macos = ...)");
    }
    if (m.script.present) {   // lua6: a script mod needs api 3 and its script; a mod is code or script, not both
        if (m.api < kScriptApi && m.api <= kApiVersion)
            r.errors.push_back("[script] needs api = " + std::to_string(kScriptApi) + " (the script API; this mod says api " + std::to_string(m.api) + ")");
        if (m.script.main_file.empty() && r.errors.empty()) r.errors.push_back("[script] names no script (main = scripts/main.lua)");
        if (m.code.present) r.errors.push_back("[code] and [script] in one mod: a mod is a library or a script (make two mods)");
        if (m.script.unsafe && !m.script.gameplay)
            r.errors.push_back("[script] unsafe = yes with gameplay = no: an unsafe script writes the game (say gameplay = yes, or drop unsafe)");
        if (!m.script.calls.empty() && !m.script.unsafe)
            r.warnings.push_back("[script] calls without unsafe = yes: b3.call is refused all the same (add unsafe = yes)");
        if (!m.script.gameplay_given)
            r.warnings.push_back("[script] says nothing about gameplay: read as gameplay = yes (it may change the game; say gameplay = no for a script that only reads and draws)");
    }
    if (!have_name || m.name.empty()) m.name = m.id;
    if (!safe_relative(m.files_root)) r.errors.push_back("[files] root \"" + m.files_root + "\" is not a folder inside the mod (no '..', no absolute path)");
    if (!safe_relative(m.files_patches)) r.errors.push_back("[files] patches \"" + m.files_patches + "\" is not a folder inside the mod (no '..', no absolute path)");
    if (seen.count("textures.pack") && !safe_relative(m.textures))
        r.errors.push_back("[textures] pack \"" + m.textures + "\" is not a folder inside the mod (no '..', no absolute path)");
    if (!m.tuning_preset.empty() && !safe_relative(m.tuning_preset))
        r.errors.push_back("[tuning] preset \"" + m.tuning_preset + "\" is not a file inside the mod (no '..', no absolute path)");
    return r;
}

int compare_versions(std::string_view a, std::string_view b) {
    auto part = [](std::string_view& s) {
        const std::size_t dot = s.find('.');
        const std::string_view p = s.substr(0, dot);
        s = dot == std::string_view::npos ? std::string_view() : s.substr(dot + 1);
        return p;
    };
    auto numeric = [](std::string_view p) {
        if (p.empty() || p.size() > 18) return false;
        for (char c : p)
            if (c < '0' || c > '9') return false;
        return true;
    };
    a = trim(a);
    b = trim(b);
    while (!a.empty() || !b.empty()) {
        std::string_view pa = part(a), pb = part(b);
        if (pa.empty()) pa = "0";
        if (pb.empty()) pb = "0";
        const bool na = numeric(pa), nb = numeric(pb);
        if (na && nb) {
            const long long x = std::stoll(std::string(pa)), y = std::stoll(std::string(pb));
            if (x != y) return x < y ? -1 : 1;
        } else if (na != nb) {
            return na ? 1 : -1;   // "1.1" > "1.beta"
        } else if (const int c = pa.compare(pb); c != 0) {
            return c < 0 ? -1 : 1;
        }
    }
    return 0;
}

// ================================================================================================ paths
namespace {
// '/' or '\' separated components; false on "." / ".." / an empty component in the middle
bool components(std::string_view p, std::vector<std::string_view>& out) {
    out.clear();
    std::size_t at = 0;
    while (at <= p.size()) {
        std::size_t end = p.find_first_of("/\\", at);
        if (end == std::string_view::npos) end = p.size();
        const std::string_view c = p.substr(at, end - at);
        if (c.empty()) {
            if (end != p.size()) return false;   // "a//b"
        } else {
            if (c == "." || c == "..") return false;
            out.push_back(c);
        }
        at = end + 1;
    }
    return !out.empty();
}
}  // namespace

std::string disc_key(std::string_view p) {
    if (const std::size_t colon = p.find(':'); colon != std::string_view::npos) {
        if (lower(p.substr(0, colon)) != "cdrom0") return {};   // another device, or a drive letter
        p.remove_prefix(colon + 1);
    }
    if (const std::size_t semi = p.rfind(';'); semi != std::string_view::npos) {
        bool digits = semi + 1 < p.size();
        for (std::size_t k = semi + 1; k < p.size(); ++k) digits = digits && p[k] >= '0' && p[k] <= '9';
        if (digits) p = p.substr(0, semi);
    }
    while (!p.empty() && (p.front() == '/' || p.front() == '\\')) p.remove_prefix(1);
    std::vector<std::string_view> comps;
    if (!components(p, comps)) return {};
    std::string key;
    for (const std::string_view c : comps) {
        if (!key.empty()) key += '/';
        for (char ch : c) {
            if (ch == '\0' || ch == ':') return {};
            key += upper(ch);
        }
    }
    return key;
}

bool safe_relative(std::string_view rel) {
    rel = trim(rel);
    if (rel.empty() || rel.front() == '/' || rel.front() == '\\') return false;
    if (rel.find(':') != std::string_view::npos) return false;   // a drive or a device
    std::vector<std::string_view> comps;
    if (!components(rel, comps)) return false;
    return true;
}

// ================================================================================================ installed mods
std::vector<Mod> discover(const fs::path& mods_root) {
    std::vector<Mod> out;
    std::error_code ec;
    if (!fs::is_directory(mods_root, ec)) return out;
    std::vector<fs::path> dirs;
    for (fs::directory_iterator it(mods_root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (utf8(it->path().filename()).rfind('.', 0) == 0) continue;   // hidden (an archive being installed)
        if (it->is_directory(e2) && fs::is_regular_file(it->path() / kManifestName, e2)) dirs.push_back(it->path());
    }
    std::sort(dirs.begin(), dirs.end());
    std::set<std::string> ids;
    for (const fs::path& d : dirs) {
        Mod m;
        m.dir = d;
        m.folder = utf8(d.filename());
        std::string text;
        if (!read_text(d / kManifestName, text)) {
            m.errors.push_back(std::string("cannot read ") + kManifestName);
        } else {
            ParsedManifest p = parse_manifest(text);
            m.manifest = std::move(p.m);
            m.errors = std::move(p.errors);
            m.warnings = std::move(p.warnings);
        }
        if (!m.manifest.id.empty() && !ids.insert(m.manifest.id).second)
            m.errors.push_back("id \"" + m.manifest.id + "\" is already taken by another folder");
        if (m.errors.empty()) {
            // the overlay root, the patches and the texture pack: folders inside the mod (a link out of it: refused)
            std::error_code e3;
            const fs::path cd = fs::weakly_canonical(d, e3);
            int present = 0;
            auto folder = [&](const std::string& rel, const char* what) {
                if (rel.empty()) return;
                const fs::path f = d / from_utf8(rel);
                std::error_code e4;
                if (!fs::is_directory(f, e4)) return;
                const fs::path cf = fs::weakly_canonical(f, e4);
                if (e4 || !inside(cd, cf)) m.errors.push_back(std::string(what) + " leads out of the mod's folder (a link)");
                else ++present;
            };
            folder(m.manifest.files_root, "[files] root");
            folder(m.manifest.files_patches, "[files] patches");
            folder(m.manifest.textures, "[textures] pack");
            if (!m.manifest.textures.empty() && !fs::is_directory(d / from_utf8(m.manifest.textures), e3))
                m.warnings.push_back("no " + m.manifest.textures + " folder ([textures] pack): the mod has no textures");
            if (!present && m.errors.empty())
                m.warnings.push_back("no " + m.manifest.files_root + " folder: the mod replaces no game files");
            if (!m.manifest.tuning_preset.empty()) {   // tune4: the preset must be a file inside the mod
                const fs::path p = d / from_utf8(m.manifest.tuning_preset);
                const fs::path cp = fs::weakly_canonical(p, e3);
                if (!fs::is_regular_file(p, e3)) m.errors.push_back("[tuning] preset " + m.manifest.tuning_preset + " is not there");
                else if (!inside(cd, cp)) m.errors.push_back("[tuning] preset leads out of the mod's folder (a link)");
            }
        }
        out.push_back(std::move(m));
    }
    return out;
}

const Mod* find_mod(const std::vector<Mod>& mods, std::string_view id) {
    for (const Mod& m : mods)
        if (m.manifest.id == id) return &m;
    return nullptr;
}

// ================================================================================================ mods.ini
fs::path mods_folder(const fs::path& pref) { return pref / "mods"; }
fs::path order_file(const fs::path& pref) { return pref / "mods.ini"; }

Order order_from_ini(std::string_view text) {
    Order o;
    std::set<std::string> seen;
    std::string section = "order";
    for_lines(text, [&](std::string_view raw, int) {
        const IniLine l = ini_line(raw);
        if (l.kind == IniLine::kSection) { section = l.section; return; }
        if (l.kind != IniLine::kPair) return;
        const std::string v = lower(l.value);
        const bool on = v == "on" || v == "1" || v == "true" || v == "yes";
        // (an [options] section, which sdk2b's test builds wrote for Developer mods, is ignored: the switch is
        // pc_settings.ini's developer_mods since the tune4 merge)
        if (section != "order" || !valid_id(l.key) || !seen.insert(l.key).second) return;
        o.entries.push_back({l.key, on});
    });
    return o;
}

std::string order_to_ini(const Order& o) {
    std::string s =
        "# Burnout 3 port: the installed mods in load order (the launcher's Mods page writes this file).\n"
        "# The first line is the highest priority: of the mods that are on, the first one that has a file supplies it.\n"
        "[order]\n";
    for (const OrderEntry& e : o.entries) s += e.id + " = " + (e.enabled ? "on" : "off") + "\n";
    return s;
}

bool load_order(const fs::path& path, Order& o) {
    std::string text;
    if (!read_text(path, text)) return false;
    o = order_from_ini(text);
    return true;
}

bool save_order(const fs::path& path, const Order& o, std::string* why) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (why) *why = "cannot write " + utf8(tmp); return false; }
        f << order_to_ini(o);
        if (!f) { if (why) *why = "cannot write " + utf8(tmp); return false; }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        if (why) *why = "cannot replace " + utf8(path) + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

Order merged_order(const Order& saved, const std::vector<Mod>& found) {
    Order o;
    std::set<std::string> placed;
    for (const OrderEntry& e : saved.entries)
        if (find_mod(found, e.id) && placed.insert(e.id).second) o.entries.push_back(e);
    std::vector<const Mod*> fresh;
    for (const Mod& m : found)
        if (!m.manifest.id.empty() && !placed.count(m.manifest.id)) fresh.push_back(&m);
    std::stable_sort(fresh.begin(), fresh.end(), [](const Mod* a, const Mod* b) {
        if (a->manifest.priority != b->manifest.priority) return a->manifest.priority > b->manifest.priority;
        return a->manifest.id < b->manifest.id;
    });
    for (const Mod* m : fresh)
        if (placed.insert(m->manifest.id).second) o.entries.push_back({m->manifest.id, false});
    return o;
}

std::vector<std::string> enabled_ids(const Order& o) {
    std::vector<std::string> v;
    for (const OrderEntry& e : o.entries)
        if (e.enabled) v.push_back(e.id);
    return v;
}

std::vector<std::string> split_ids(std::string_view list) {
    std::vector<std::string> v;
    if (trim(list) == "none") return v;
    std::size_t at = 0;
    while (at <= list.size()) {
        std::size_t end = list.find(',', at);
        if (end == std::string_view::npos) end = list.size();
        const std::string_view id = trim(list.substr(at, end - at));
        if (!id.empty()) v.emplace_back(id);
        at = end + 1;
    }
    return v;
}

Selection select(const std::vector<Mod>& found, const std::vector<std::string>& ids) {
    Selection s;
    std::set<std::string> seen;
    for (const std::string& id : ids) {
        if (!seen.insert(id).second) continue;
        const Mod* m = find_mod(found, id);
        if (!m) { s.errors.push_back(id + ": not installed (no mod with that id in the mods folder)"); continue; }
        if (!m->usable()) {
            s.errors.push_back(id + ": " + m->errors.front());
            continue;
        }
        s.mods.push_back(m);
    }
    // requirements: drop the mods whose requirement is not in the set, until none is dropped (a chain drops together)
    for (bool dropped = true; dropped;) {
        dropped = false;
        for (std::size_t k = 0; k < s.mods.size(); ++k) {
            const Mod* m = s.mods[k];
            std::string why;
            for (const Requirement& q : m->manifest.requirements) {
                const auto in = std::find_if(s.mods.begin(), s.mods.end(), [&](const Mod* o) { return o->manifest.id == q.id; });
                const std::string need = q.id + (q.min_version.empty() ? "" : " " + q.min_version + " or newer");
                if (in == s.mods.end()) {
                    const Mod* there = find_mod(found, q.id);
                    why = !there ? "it requires " + need + ", which is not installed"
                          : !there->usable() ? "it requires " + need + ", which cannot be used"
                                              : "it requires " + need + ", which is not on";
                } else if (!q.min_version.empty() && compare_versions((*in)->manifest.version, q.min_version) < 0) {
                    why = "it requires " + need + "; version " + (*in)->manifest.version + " is installed";
                }
                if (!why.empty()) break;
            }
            if (why.empty()) continue;
            s.errors.push_back(m->manifest.id + ": " + why + ": left out");
            s.mods.erase(s.mods.begin() + static_cast<std::ptrdiff_t>(k));
            dropped = true;
            break;
        }
    }
    for (std::size_t k = 0; k < s.mods.size(); ++k)
        for (const Requirement& q : s.mods[k]->manifest.requirements)
            for (std::size_t j = 0; j < k; ++j)
                if (s.mods[j]->manifest.id == q.id)
                    s.notes.push_back(s.mods[k]->manifest.id + " requires " + q.id + " and is below it: where both have a file, " + q.id +
                                      "'s wins (move " + s.mods[k]->manifest.id + " up if it is meant to change " + q.id + ")");
    return s;
}

// ================================================================================================ hashes
std::uint64_t fnv1a64(const void* data, std::size_t n, std::uint64_t h) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t k = 0; k < n; ++k) h = (h ^ p[k]) * 0x100000001B3ull;
    return h;
}

std::string path_utf8(const fs::path& p) { return utf8(p); }
fs::path path_from_utf8(const std::string& s) { return from_utf8(s); }

std::string hex64(std::uint64_t v) {
    char b[20];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

bool hash_file(const fs::path& p, std::uint64_t& fnv, std::uint64_t& size) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::vector<char> buf(1 << 20);
    std::uint64_t h = 0xCBF29CE484222325ull, n = 0;
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = f.gcount();
        if (got <= 0) break;
        h = fnv1a64(buf.data(), static_cast<std::size_t>(got), h);
        n += static_cast<std::uint64_t>(got);
    }
    if (f.bad()) return false;
    fnv = h;
    size = n;
    return true;
}

std::uint64_t mod_hash(const std::vector<OverlayFile>& files, const std::string& code_lines, const std::string& script_lines) {
    std::string t;
    for (const OverlayFile& f : files) {
        if (f.patch.empty()) t += "file " + f.key + " " + std::to_string(f.size) + " " + hex64(f.fnv) + "\n";
        else t += "patch " + f.key + " " + std::to_string(f.patch_size) + " " + hex64(f.patch_fnv) + "\n";
    }
    t += code_lines;
    t += script_lines;
    return fnv1a64(t.data(), t.size());
}

const char* host_system() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

// ================================================================================================ b3patch
namespace {
constexpr char kPatchMagic[8] = {'B', '3', 'P', 'A', 'T', 'C', 'H', 0};
constexpr std::size_t kPatchHeader = 48;
std::uint64_t get64(const std::uint8_t* p) { std::uint64_t v = 0; for (int i = 7; i >= 0; --i) v = v << 8 | p[i]; return v; }
std::uint32_t get32(const std::uint8_t* p) { return std::uint32_t(p[0] | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24); }
bool get_varint(const std::vector<std::uint8_t>& b, std::size_t& at, std::uint64_t& v) {
    v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (at >= b.size()) return false;
        const std::uint8_t c = b[at++];
        v |= std::uint64_t(c & 0x7F) << shift;
        if (!(c & 0x80)) return shift < 63 || (c & 0x7F) <= 1;
    }
    return false;
}
bool pfail(std::string* why, std::string what) { if (why) *why = std::move(what); return false; }
// the patch's header and ops; with src and out: the result (sizes and hashes checked)
bool walk_patch(const std::vector<std::uint8_t>& patch, PatchInfo& info, const std::vector<std::uint8_t>* src, std::vector<std::uint8_t>* out,
                std::string* why) {
    info = PatchInfo{};
    if (patch.size() < kPatchHeader + 1 || std::memcmp(patch.data(), kPatchMagic, 8) != 0) return pfail(why, "not a b3patch file");
    const std::uint32_t ver = get32(&patch[8]);
    if (ver != 1) return pfail(why, "b3patch version " + std::to_string(ver) + " (this port reads version 1)");
    if (get32(&patch[12]) != 0) return pfail(why, "b3patch flags not 0 (made by a newer tool)");
    info.src_size = get64(&patch[16]);
    info.src_fnv = get64(&patch[24]);
    info.dst_size = get64(&patch[32]);
    info.dst_fnv = get64(&patch[40]);
    if (out) {
        out->clear();
        if (info.dst_size > (std::uint64_t(1) << 32)) return pfail(why, "target larger than 4 GB");
        // fsrobust: reserved up to what an edit of the source can make (its bytes and the patch's), never the header's
        // claim alone (up to 4 GB from a 48-byte file); a longer result grows as its runs are added
        out->reserve(std::size_t(std::min<std::uint64_t>(info.dst_size, std::uint64_t(src ? src->size() : 0) + patch.size())));
    }
    std::size_t at = kPatchHeader;
    std::uint64_t produced = 0;
    for (;;) {
        if (at >= patch.size()) return pfail(why, "b3patch ends without its END op");
        const std::uint8_t op = patch[at++];
        if (op == 0) break;
        std::uint64_t a = 0, len = 0;
        if (op == 1) {
            if (!get_varint(patch, at, a) || !get_varint(patch, at, len)) return pfail(why, "b3patch: a COPY op is cut off");
            if (!len || a > info.src_size || len > info.src_size - a) return pfail(why, "b3patch: a COPY run outside the source");
            if (out) out->insert(out->end(), src->begin() + std::ptrdiff_t(a), src->begin() + std::ptrdiff_t(a + len));
            info.copied += len;
        } else if (op == 2) {
            if (!get_varint(patch, at, len) || !len || len > patch.size() - at) return pfail(why, "b3patch: an INSERT op is cut off");
            if (out) out->insert(out->end(), patch.begin() + std::ptrdiff_t(at), patch.begin() + std::ptrdiff_t(at + len));
            at += std::size_t(len);
            info.inserted += len;
        } else {
            return pfail(why, "b3patch: unknown op " + std::to_string(op));
        }
        ++info.ops;
        produced += len;
        if (produced > info.dst_size) return pfail(why, "b3patch: more bytes than its target size");
    }
    if (at != patch.size()) return pfail(why, "b3patch: bytes after the END op");
    if (produced != info.dst_size) return pfail(why, "b3patch: its runs do not add up to the target size");
    if (out && fnv1a64(out->data(), out->size()) != info.dst_fnv) return pfail(why, "b3patch: the result does not match the target hash");
    return true;
}
}  // namespace

bool read_patch_info(const std::vector<std::uint8_t>& patch, PatchInfo& info, std::string* why) {
    return walk_patch(patch, info, nullptr, nullptr, why);
}

bool apply_patch(const std::vector<std::uint8_t>& src, const std::vector<std::uint8_t>& patch, std::vector<std::uint8_t>& out, std::string* why) {
    PatchInfo info;
    if (!read_patch_info(patch, info, why)) return false;
    const std::uint64_t fnv = fnv1a64(src.data(), src.size());
    if (src.size() != info.src_size || fnv != info.src_fnv)
        return pfail(why, "the patch was made from another file: it expects " + std::to_string(info.src_size) + " bytes, FNV-1a " + hex64(info.src_fnv) +
                              "; this one is " + std::to_string(src.size()) + " bytes, " + hex64(fnv));
    return walk_patch(patch, info, &src, &out, why);
}

fs::path patch_cache(const fs::path& mods_root) { return mods_root / ".cache"; }

namespace {
bool read_bytes(const fs::path& p, std::vector<std::uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0);
    out.resize(static_cast<std::size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(out.data()), n);
    return static_cast<bool>(f);
}
// `bytes` into `dst` through a temporary file beside it and a rename (another process may write the same result at
// the same time: both write the same bytes, the last rename wins)
bool write_atomic(const fs::path& dst, const std::vector<std::uint8_t>& bytes, std::string* why) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    static std::uint64_t counter = 0;
    const std::uint64_t ticks = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::uint64_t tag = ticks * 0x9E3779B97F4A7C15ull ^ ++counter ^ reinterpret_cast<std::uintptr_t>(&bytes);
    fs::path tmp = dst;
    tmp += ".tmp-" + hex64(tag);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (f && !bytes.empty()) f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f) {
            f.close();
            fs::remove(tmp, ec);
            return pfail(why, "cannot write " + utf8(tmp) + " (the patch cache)");
        }
    }
    fs::rename(tmp, dst, ec);
    if (ec) {
        std::error_code e2;
        fs::remove(tmp, e2);
        std::uint64_t h = 0, n = 0;   // a rename refused while another process had just put the same file there
        if (hash_file(dst, h, n) && n == bytes.size() && h == fnv1a64(bytes.data(), bytes.size())) return true;
        return pfail(why, "cannot write " + utf8(dst) + " (the patch cache): " + ec.message());
    }
    return true;
}
}  // namespace

std::string set_hash_text(const std::vector<ActiveMod>& mods) {
    std::string t = "b3mods api 1\n";   // the hash text's version (sdk2b: api 2 did not change it)
    for (const ActiveMod& m : mods) t += "mod " + m.id + " " + m.version + " " + hex64(m.hash) + "\n";
    return t;
}

std::string gameplay_set_hash(const std::vector<ActiveMod>& mods) {
    std::string t = "b3mods gameplay 1\n";
    bool any = false;
    for (const ActiveMod& m : mods)
        if (changes_gameplay_any(m)) {
            t += "mod " + m.id + " " + m.version + " " + hex64(m.hash) + "\n";
            any = true;
        }
    return any ? hex64(fnv1a64(t.data(), t.size())) : std::string();
}

// ================================================================================================ the overlay
Overlay Overlay::build(const std::vector<const Mod*>& mods, bool hash_contents, const PatchEnv* env) {
    Overlay o;
    std::set<std::string> ids;
    std::map<std::string, const Mod*> by_id;
    // the files below `dir` by key: (path, "<label>/<path inside dir>"); `patches`: only *.b3patch, the suffix dropped
    auto scan = [](ActiveMod& a, const fs::path& dir, const std::string& label, bool patches,
                   std::map<std::string, std::vector<std::pair<fs::path, std::string>>>& by_key) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = utf8(it->path().filename());
            std::error_code e2;
            if (ignored_name(name)) {
                if (it->is_directory(e2)) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(e2)) continue;   // folders, and links to nothing
            std::string rel = utf8(it->path().lexically_relative(dir));
            std::replace(rel.begin(), rel.end(), '\\', '/');
            std::string disc = rel;
            if (patches) {
                const std::size_t n = std::string(kPatchSuffix).size();
                if (disc.size() <= n || lower(disc.substr(disc.size() - n)) != kPatchSuffix) {
                    a.notes.push_back(label + "/" + rel + ": not a " + kPatchSuffix + " file: left out");
                    continue;
                }
                disc.resize(disc.size() - n);
            }
            const std::string key = disc_key(disc);
            if (key.empty()) { a.notes.push_back(label + "/" + rel + ": not a disc path: left out"); continue; }
            if (key == "SLUS_210.50" || key == "SYSTEM.CNF") {   // the program runs as native code: never from a file
                a.notes.push_back(key + ": the game program is not replaced by files (code mods: the SDK, DESIGN.md §2): left out");
                continue;
            }
            by_key[key].push_back({it->path(), label + "/" + rel});
        }
    };
    for (const Mod* m : mods) {
        if (!m || !m->usable() || !ids.insert(m->manifest.id).second) continue;
        ActiveMod a;
        a.id = m->manifest.id;
        a.version = m->manifest.version;
        a.name = m->manifest.name;
        a.dir = m->dir;
        a.root = m->dir / from_utf8(m->manifest.files_root);
        std::error_code ec;
        const fs::path canon_dir = fs::weakly_canonical(m->dir, ec);
        std::map<std::string, std::vector<std::pair<fs::path, std::string>>> by_key, patch_keys;
        scan(a, a.root, m->manifest.files_root, false, by_key);
        for (auto& [key, paths] : by_key) {
            if (paths.size() > 1) {
                a.notes.push_back(key + ": " + std::to_string(paths.size()) + " files whose names differ only in case: all left out");
                continue;
            }
            const fs::path& p = paths.front().first;
            std::error_code e2;
            const fs::path canon = fs::weakly_canonical(p, e2);
            if (e2 || !inside(canon_dir, canon)) { a.notes.push_back(key + ": a link out of the mod's folder: left out"); continue; }
            OverlayFile f;
            f.key = key;
            f.host = p;
            if (hash_contents) {
                if (!hash_file(p, f.fnv, f.size)) { a.notes.push_back(key + ": cannot be read: left out"); continue; }
            } else {
                f.size = static_cast<std::uint64_t>(fs::file_size(p, e2));
            }
            if (f.size > 0xFFFFFFFFull) { a.notes.push_back(key + ": larger than 4 GB (no disc file is): left out"); continue; }
            a.files.push_back(std::move(f));
        }
        // [files] patches: each applied to the user's own file; any patch that does not apply leaves the whole mod out
        std::string refuse;
        scan(a, m->dir / from_utf8(m->manifest.files_patches), m->manifest.files_patches, true, patch_keys);
        for (auto& [key, paths] : patch_keys) {
            if (!refuse.empty()) break;
            const std::string& rel = paths.front().second;
            if (paths.size() > 1) { refuse = key + ": " + std::to_string(paths.size()) + " patches whose names differ only in case"; break; }
            if (by_key.count(key)) { refuse = rel + " and a file for " + key + " in " + m->manifest.files_root + "/: a mod has one or the other"; break; }
            const fs::path& p = paths.front().first;
            std::error_code e2;
            const fs::path canon = fs::weakly_canonical(p, e2);
            if (e2 || !inside(canon_dir, canon)) { refuse = rel + ": a link out of the mod's folder"; break; }
            std::vector<std::uint8_t> patch;
            PatchInfo pi;
            std::string why;
            if (!read_bytes(p, patch)) { refuse = rel + ": cannot be read"; break; }
            if (!read_patch_info(patch, pi, &why)) { refuse = rel + ": " + why; break; }
            OverlayFile f;
            f.key = key;
            f.patch = p;
            f.patch_size = patch.size();
            f.patch_fnv = fnv1a64(patch.data(), patch.size());
            f.size = pi.dst_size;
            f.fnv = pi.dst_fnv;
            if (env && env->read_disc) {
                std::vector<std::uint8_t> src;
                if (!env->read_disc(key, src)) { refuse = rel + ": your disc has no " + key + " to apply it to"; break; }
                const std::uint64_t src_fnv = fnv1a64(src.data(), src.size());
                if (src.size() != pi.src_size || src_fnv != pi.src_fnv) {
                    refuse = rel + " was made from another version of " + key + " than your disc's (it needs " + std::to_string(pi.src_size) +
                             " bytes, FNV-1a " + hex64(pi.src_fnv) + "; yours is " + std::to_string(src.size()) + " bytes, " + hex64(src_fnv) +
                             "): another region or version of the game, or a modified file";
                    break;
                }
                if (env->cache.empty()) { refuse = rel + ": no folder to keep the patched file in"; break; }
                const fs::path out = env->cache / (hex64(f.patch_fnv) + "-" + hex64(src_fnv) + ".bin");
                std::uint64_t have_fnv = 0, have_size = 0;
                if (!hash_file(out, have_fnv, have_size) || have_size != pi.dst_size || have_fnv != pi.dst_fnv) {
                    std::vector<std::uint8_t> result;
                    if (!apply_patch(src, patch, result, &why) || !write_atomic(out, result, &why)) { refuse = rel + ": " + why; break; }
                }
                f.host = out;
            } else if (hash_contents) {
                refuse = rel + ": patches are applied to the disc's files, and no disc was given";
                break;
            }   // else: listed by its header alone (the launcher's view without a disc)
            a.files.push_back(std::move(f));
            ++a.patched;
        }
        if (!refuse.empty()) {
            o.errors_.push_back(a.id + ": " + refuse + ": the mod is off for this run");
            o.left_out_.push_back(a.id);
            continue;
        }
        std::sort(a.files.begin(), a.files.end(), [](const OverlayFile& x, const OverlayFile& y) { return x.key < y.key; });
        if (!m->manifest.textures.empty()) {
            const fs::path t = m->dir / from_utf8(m->manifest.textures);
            if (fs::is_directory(t, ec)) a.textures = t;
        }
        if (m->manifest.code.present) {   // sdk2b: every library the manifest names, in the mod hash
            a.code = m->manifest.code;
            for (const char* sys : {"windows", "linux", "macos"}) {
                const std::string& rel = a.code.path_for(sys);
                if (rel.empty()) continue;
                const fs::path p = m->dir / from_utf8(rel);
                std::error_code e2;
                const fs::path canon = fs::weakly_canonical(p, e2);
                std::uint64_t fnv = 0, size = 0;
                const bool in = !e2 && inside(canon_dir, canon);
                if (in && fs::is_regular_file(p, e2) && (hash_contents ? hash_file(p, fnv, size) : true)) {
                    if (!hash_contents) size = static_cast<std::uint64_t>(fs::file_size(p, e2));
                    a.code_lines += std::string("code ") + sys + " " + rel + " " + std::to_string(size) + " " + hex64(fnv) + "\n";
                    if (std::string_view(sys) == host_system()) a.library = fs::absolute(p, e2);
                } else {
                    a.code_lines += std::string("code ") + sys + " " + rel + " missing\n";
                    if (std::string_view(sys) == host_system())
                        a.notes.push_back(std::string("[code] ") + sys + " = " + rel + (in ? ": no such file" : ": leads out of the mod's folder"));
                }
            }
        }
        if (m->manifest.script.present) {   // lua6: every file of the script's folder, in the mod hash
            a.script = m->manifest.script;
            const std::string folder = a.script.folder();
            const fs::path base = m->dir / from_utf8(folder);
            std::map<std::string, fs::path> by_rel;   // by path as written in the mod ('/' separators)
            std::error_code e2;
            if (!folder.empty() && fs::is_directory(base, e2)) {
                for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, e2), end; !e2 && it != end; it.increment(e2)) {
                    const std::string name = utf8(it->path().filename());
                    std::error_code e3;
                    if (ignored_name(name)) {
                        if (it->is_directory(e3)) it.disable_recursion_pending();
                        continue;
                    }
                    if (!it->is_regular_file(e3)) continue;
                    std::string rel = utf8(it->path().lexically_relative(m->dir));
                    std::replace(rel.begin(), rel.end(), '\\', '/');
                    by_rel[rel] = it->path();
                }
            }
            bool have_main = false;
            for (const auto& [rel, p] : by_rel) {
                std::error_code e3;
                const fs::path canon = fs::weakly_canonical(p, e3);
                if (e3 || !inside(canon_dir, canon)) { a.notes.push_back("[script] " + rel + ": a link out of the mod's folder: left out"); continue; }
                std::uint64_t fnv = 0, size = 0;
                if (hash_contents ? !hash_file(p, fnv, size) : false) { a.notes.push_back("[script] " + rel + ": cannot be read: left out"); continue; }
                if (!hash_contents) size = static_cast<std::uint64_t>(fs::file_size(p, e3));
                a.script_lines += "script " + rel + " " + std::to_string(size) + " " + hex64(fnv) + "\n";
                if (rel == a.script.main_file) have_main = true;
            }
            if (!have_main) {
                a.script_lines += "script " + a.script.main_file + " missing\n";
                a.notes.push_back("[script] main = " + a.script.main_file + ": no such file");
            }
        }
        a.hash = mod_hash(a.files, a.code_lines, a.script_lines);
        if (!m->manifest.tuning_preset.empty()) {   // tune4: the preset's bytes are part of the mod's hash
            a.tuning = m->dir / from_utf8(m->manifest.tuning_preset);
            a.tuning_rel = m->manifest.tuning_preset;
            std::error_code e2;
            if (hash_contents) hash_file(a.tuning, a.tuning_fnv, a.tuning_size);
            else a.tuning_size = static_cast<std::uint64_t>(fs::file_size(a.tuning, e2));
            const std::string t = "tuning " + a.tuning_rel + " " + std::to_string(a.tuning_size) + " " + hex64(a.tuning_fnv) + "\n";
            a.hash = fnv1a64(t.data(), t.size(), a.hash);   // = the hash of the file / patch / code / script lines followed by this one
        }
        by_id[a.id] = m;
        o.mods_.push_back(std::move(a));
    }
    // a mod that requires one left out above goes too (select() checked the requirements on the set before this)
    for (bool dropped = !o.left_out_.empty(); dropped;) {
        dropped = false;
        for (std::size_t k = 0; k < o.mods_.size() && !dropped; ++k)
            for (const Requirement& q : by_id[o.mods_[k].id]->manifest.requirements)
                if (std::find(o.left_out_.begin(), o.left_out_.end(), q.id) != o.left_out_.end()) {
                    o.errors_.push_back(o.mods_[k].id + ": it requires " + q.id + ", which is off for this run: left out");
                    o.left_out_.push_back(o.mods_[k].id);
                    o.mods_.erase(o.mods_.begin() + static_cast<std::ptrdiff_t>(k));
                    dropped = true;
                    break;
                }
    }
    for (std::size_t i = 0; i < o.mods_.size(); ++i)
        for (std::size_t j = 0; j < o.mods_[i].files.size(); ++j) o.index_.emplace(o.mods_[i].files[j].key, std::make_pair(i, j));
    if (!o.mods_.empty()) {
        const std::string t = set_hash_text(o.mods_);
        o.set_hash_ = fnv1a64(t.data(), t.size());
    }
    return o;
}

std::vector<fs::path> Overlay::texture_packs() const {
    std::vector<fs::path> v;
    for (const ActiveMod& m : mods_)
        if (!m.textures.empty()) v.push_back(m.textures);
    return v;
}


const OverlayFile* Overlay::find(std::string_view disc_path, const ActiveMod** who) const {
    if (index_.empty()) return nullptr;
    const std::string key = disc_key(disc_path);
    if (key.empty()) return nullptr;
    const auto it = index_.find(key);
    if (it == index_.end()) return nullptr;
    if (who) *who = &mods_[it->second.first];
    return &mods_[it->second.first].files[it->second.second];
}

std::vector<std::pair<std::string, fs::path>> Overlay::winners() const {
    std::vector<std::pair<std::string, fs::path>> v;
    v.reserve(index_.size());
    for (const auto& [key, at] : index_)   // (a patch listed without a disc has no file to read: left to the disc)
        if (!mods_[at.first].files[at.second].host.empty()) v.emplace_back(key, mods_[at.first].files[at.second].host);
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return v;
}

std::vector<FileConflict> conflicts(const Overlay& o) {
    std::map<std::string, FileConflict> by_key;
    for (const ActiveMod& m : o.mods())
        for (const OverlayFile& f : m.files) {
            FileConflict& c = by_key[f.key];
            c.key = f.key;
            if (c.winner.empty()) c.winner = m.id;
            else c.hidden.push_back(m.id);
        }
    std::vector<FileConflict> v;
    for (auto& [key, c] : by_key)
        if (!c.hidden.empty()) v.push_back(std::move(c));
    return v;
}

ModConflicts conflicts_of(const std::vector<FileConflict>& all, std::string_view id) {
    ModConflicts r;
    for (const FileConflict& c : all) {
        if (c.winner == id) r.wins.emplace_back(c.key, c.hidden.front());
        else if (std::find(c.hidden.begin(), c.hidden.end(), id) != c.hidden.end()) r.hidden.emplace_back(c.key, c.winner);
    }
    return r;
}

std::string renderer_args(const fs::path& mods_root, const Overlay& o, std::string* why) {
    if (o.empty()) return {};
    const std::string dir = utf8(mods_root);
    if (dir.find('"') != std::string::npos) {
        if (why) *why = "the mods folder's name has a '\"': the renderer cannot be given it";
        return {};
    }
    std::string ids;
    for (const ActiveMod& m : o.mods()) ids += (ids.empty() ? "" : ",") + m.id;
    return " --mods-dir \"" + dir + "\" --mods " + ids + " --mods-hash " + hex64(o.set_hash());
}

std::string describe(const Overlay& o) {
    if (o.empty()) return "none";
    std::string s;
    for (const ActiveMod& m : o.mods()) {
        if (!s.empty()) s += ", ";
        s += m.id + " " + m.version + " (" + std::to_string(m.files.size()) + (m.files.size() == 1 ? " file" : " files") +
             (m.code.present ? ", code)" : m.script.present ? ", script)" : ")");
    }
    return s + "; set hash " + hex64(o.set_hash());
}

}  // namespace b3mod
