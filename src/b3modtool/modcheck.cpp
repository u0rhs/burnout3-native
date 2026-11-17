// b3modtool verify / pack: a mod folder or .b3mod checked against the loader's rules (include/b3mod/mods.h) and,
// with the user's disc, against the rule that a mod ships its own content (docs/modding/DESIGN.md §8).
#include "b3modtool/modtool.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <unordered_map>

#include "b3asset/discfs.h"
#include "b3asset/hdpack.h"
#include "b3mod/mods.h"
#include "b3tune/tune.h"   // tune4: a [tuning] preset checked
#include "b3report/zip.h"

namespace fs = std::filesystem;

namespace b3modtool {

namespace {

struct ModFile {
    std::string rel;   // '/' separated, as in the folder / archive
    std::uint64_t size = 0;
    std::function<bool(Buf&, std::string*)> read;
};

// the names the loader leaves out (mods.cpp): dot files and folders, the file managers' own files
bool ignored_name(const std::string& rel) {
    std::size_t at = 0;
    for (;;) {
        const std::size_t e = rel.find('/', at);
        const std::string c = rel.substr(at, e == std::string::npos ? std::string::npos : e - at);
        const std::string u = to_upper_ascii(c);
        if (!c.empty() && (c[0] == '.' || u == "THUMBS.DB" || u == "DESKTOP.INI")) return true;
        if (e == std::string::npos) return false;
        at = e + 1;
    }
}

bool folder_files(const fs::path& dir, std::vector<ModFile>& out, VerifyReport& rep) {
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(dir, ec);
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        const std::string rel = b3mod::path_utf8(fs::relative(p, dir, ec));
        std::string r = rel;
        std::replace(r.begin(), r.end(), '\\', '/');
        if (it->is_symlink(ec)) {
            const fs::path t = fs::weakly_canonical(p, ec);
            const std::string ts = b3mod::path_utf8(t), rs = b3mod::path_utf8(root);
            if (ec || ts.compare(0, rs.size(), rs) != 0 || (ts.size() > rs.size() && ts[rs.size()] != '/' && ts[rs.size()] != '\\')) {
                rep.errors.push_back(r + ": a link that leads out of the mod (the loader leaves it out; copy the file in)");
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
        }
        if (!it->is_regular_file(ec)) continue;
        ModFile m;
        m.rel = r;
        m.size = it->file_size(ec);
        m.read = [p](Buf& b, std::string* why) {
            if (read_host_file(b3mod::path_utf8(p), b)) return true;
            if (why) *why = "cannot read " + b3mod::path_utf8(p);
            return false;
        };
        out.push_back(std::move(m));
    }
    if (ec) { rep.errors.push_back("cannot list " + b3mod::path_utf8(dir) + ": " + ec.message()); return false; }
    return true;
}

bool zip_files(const std::shared_ptr<Buf>& z, std::vector<ModFile>& out, VerifyReport& rep) {
    std::vector<ZipEntry> es;
    std::string why;
    if (!zip_list(*z, es, &why)) { rep.errors.push_back(why); return false; }
    for (const ZipEntry& e : es) {
        const std::string& n = e.name;
        if (!n.empty() && n.back() == '/') continue;   // a folder
        bool bad = n.empty() || n[0] == '/' || n.find('\\') != std::string::npos || (n.size() > 1 && n[1] == ':');
        for (std::size_t at = 0; !bad;) {
            const std::size_t s = n.find('/', at);
            const std::string c = n.substr(at, s == std::string::npos ? std::string::npos : s - at);
            if (c.empty() || c == "." || c == "..") bad = true;
            if (s == std::string::npos) break;
            at = s + 1;
        }
        if (bad) { rep.errors.push_back("archive entry \"" + n + "\": a path that is absolute or leaves the mod's folder (refused)"); continue; }
        ModFile m;
        m.rel = n;
        m.size = e.usize;
        m.read = [z, e](Buf& b, std::string* w) { return zip_read(*z, e, b, w); };
        out.push_back(std::move(m));
    }
    return true;
}

std::string pct(std::uint64_t a, std::uint64_t b) { return std::to_string(b ? (a * 100 + b / 2) / b : 0) + "%"; }

// b3modtool's own exports of the game's data inside a mod (each export starts with a "# b3modtool <what>:" line):
//   txd.txt  "# b3modtool txd export: ..."  a `txd export` folder: the game's pictures as PNG
//   audio.txt "# b3modtool audio: ..."      an `audio export` folder: the game's sounds as WAV
//   any text "# b3modtool strings: <file>, <n> strings" with more than half of the n "<id> = " lines: the game's text
//   any text "# b3modtool vdb: <file>: <n> values ..." with more than half of the n "... = ..." lines: the tuning table
// (a partial text, only the lines a modder changed, is the mod's own and passes). An error each, unless allowed.
void check_exports(const std::vector<ModFile>& files, const VerifyOptions& opt, VerifyReport& rep) {
    auto flag = [&](const std::string& what) {
        if (opt.allow_exports) rep.notes.push_back(what + " (allowed: --allow-exports)");
        else rep.errors.push_back(what + ": the game's own data, never part of a mod (keep exports outside the mod folder; --allow-exports if this is meant)");
    };
    auto count_in = [&](const std::string& dir, const char* ext) {   // files directly in dir with that extension
        std::size_t n = 0;
        for (const ModFile& g : files) {
            const std::size_t s = g.rel.rfind('/');
            const std::string d = s == std::string::npos ? std::string() : g.rel.substr(0, s);
            const std::string u = to_upper_ascii(g.rel);
            if (d == dir && u.size() > 4 && u.compare(u.size() - 4, 4, ext) == 0) ++n;
        }
        return n;
    };
    auto number_after = [](const std::string& line, std::size_t at) {
        std::size_t n = 0;
        while (at < line.size() && line[at] == ' ') ++at;
        bool any = false;
        while (at < line.size() && line[at] >= '0' && line[at] <= '9') { n = n * 10 + std::size_t(line[at++] - '0'); any = true; }
        return any ? n : std::size_t(0);
    };
    for (const ModFile& f : files) {
        if (f.size < 16 || f.size > (std::uint64_t(8) << 20)) continue;
        Buf b;
        if (!f.read(b, nullptr) || b.size() < 12 || std::memcmp(b.data(), "# b3modtool ", 12) != 0) continue;
        const std::string text(b.begin(), b.end());
        const std::string first = text.substr(0, text.find('\n'));
        const std::size_t s = f.rel.rfind('/');
        const std::string dir = s == std::string::npos ? std::string() : f.rel.substr(0, s);
        const std::string base = to_upper_ascii(s == std::string::npos ? f.rel : f.rel.substr(s + 1));
        const std::string where = dir.empty() ? std::string("the mod's top folder") : dir + "/";
        if (base == "TXD.TXT" && first.rfind("# b3modtool txd export:", 0) == 0) {
            flag(where + ": a txd export (" + f.rel + " and " + std::to_string(count_in(dir, ".PNG")) + " PNGs)");
            continue;
        }
        if (base == "AUDIO.TXT" && first.rfind("# b3modtool audio:", 0) == 0) {
            flag(where + ": an audio export (" + f.rel + " and " + std::to_string(count_in(dir, ".WAV")) + " WAVs)");
            continue;
        }
        const bool strings = first.rfind("# b3modtool strings:", 0) == 0, vdb = first.rfind("# b3modtool vdb:", 0) == 0;
        if (!strings && !vdb) continue;
        // the table's size from the first line: "..., <n> strings" / ": <n> values"
        std::size_t total = 0;
        if (strings) {
            const std::size_t c = first.rfind(", ");
            if (c != std::string::npos) total = number_after(first, c + 2);
        } else {
            const std::size_t v = first.find(" values");
            const std::size_t c = v == std::string::npos ? std::string::npos : first.rfind(": ", v);
            if (c != std::string::npos) total = number_after(first, c + 2);
        }
        std::size_t lines = 0;
        for (std::size_t at = 0; at < text.size();) {
            std::size_t e = text.find('\n', at);
            if (e == std::string::npos) e = text.size();
            const std::string l = text.substr(at, e - at);
            at = e + 1;
            if (l.empty() || l[0] == '#') continue;
            if (strings ? (l[0] >= '0' && l[0] <= '9' && l.find(" = ") != std::string::npos) : l.find(" = ") != std::string::npos) ++lines;
        }
        if (total && lines * 2 > total)
            flag(f.rel + ": a " + (strings ? "strings" : "vdb") + " export (" + std::to_string(lines) + " of the table's " + std::to_string(total) +
                 (strings ? " strings" : " values") + "; a mod's text holds only the lines it changes)");
    }
}

}  // namespace

std::vector<DiscFile> disc_files(const std::string& disc) {
    std::vector<DiscFile> out;
    std::function<void(const fs::path&, const std::string&)> walk = [&](const fs::path& dir, const std::string& prefix) {
        for (const b3asset::disc::Entry& e : b3asset::disc::list_dir(dir)) {
            const std::string key = prefix.empty() ? e.name : prefix + "/" + e.name;
            if (e.dir) walk(e.path, key);
            else {
                // (a host folder's entries carry no size)
                const std::int64_t sz = e.size ? std::int64_t(e.size) : b3asset::disc::file_size(e.path);
                out.push_back({key, b3mod::path_utf8(e.path), std::uint64_t(sz < 0 ? 0 : sz)});
            }
        }
    };
    walk(fs::path(disc), "");
    std::sort(out.begin(), out.end(), [](const DiscFile& a, const DiscFile& b) { return a.key < b.key; });
    return out;
}

bool verify_mod(const std::string& mod, const std::string& game, VerifyReport& rep, const VerifyOptions& opt) {
    rep = VerifyReport{};
    std::vector<ModFile> files;
    std::error_code ec;
    if (fs::is_directory(mod, ec)) {
        if (!folder_files(mod, files, rep)) return false;
    } else {
        auto z = std::make_shared<Buf>();
        if (!read_host_file(mod, *z)) { rep.errors.push_back("cannot read " + mod); return false; }
        if (!zip_files(z, files, rep)) return false;
    }
    std::sort(files.begin(), files.end(), [](const ModFile& a, const ModFile& b) { return a.rel < b.rel; });

    // the manifest
    const ModFile* ini = nullptr;
    for (const ModFile& f : files)
        if (f.rel == b3mod::kManifestName) ini = &f;
    if (!ini) {
        std::string where;
        for (const ModFile& f : files) {
            const std::size_t s = f.rel.rfind('/');
            const std::string base = s == std::string::npos ? f.rel : f.rel.substr(s + 1);
            if (to_upper_ascii(base) == "MOD.INI") where = f.rel;
        }
        rep.errors.push_back(where.empty() ? "no mod.ini at the top of the mod"
                                           : "no mod.ini at the top of the mod (found " + where + ": it must be named mod.ini and be at the top)");
        return true;
    }
    Buf ini_bytes;
    std::string why;
    if (!ini->read(ini_bytes, &why)) { rep.errors.push_back(why); return true; }
    const b3mod::ParsedManifest pm = b3mod::parse_manifest(std::string_view(reinterpret_cast<const char*>(ini_bytes.data()), ini_bytes.size()));
    for (const std::string& e : pm.errors) rep.errors.push_back("mod.ini: " + e);
    for (const std::string& w : pm.warnings) rep.warnings.push_back("mod.ini: " + w);
    rep.id = pm.m.id;
    rep.version = pm.m.version;
    auto folder_of = [](std::string s) {
        std::replace(s.begin(), s.end(), '\\', '/');
        while (!s.empty() && s.back() == '/') s.pop_back();
        return s;
    };
    const std::string root = folder_of(pm.m.files_root), proot = folder_of(pm.m.files_patches), troot = folder_of(pm.m.textures);
    auto below = [](const std::string& rel, const std::string& dir) {   // rel inside dir ('/' separated): the rest, else ""
        if (dir.empty() || rel.size() <= dir.size() + 1 || rel.compare(0, dir.size(), dir) != 0 || rel[dir.size()] != '/') return std::string();
        return rel.substr(dir.size() + 1);
    };

    check_exports(files, opt, rep);
    bool readme = false, licence = false;
    std::unordered_map<std::string, std::string> keys;   // overlay key -> rel
    std::vector<std::pair<const ModFile*, std::string>> overlay;   // file, key
    std::vector<std::pair<const ModFile*, std::string>> patches;   // file, the disc key it patches
    std::unordered_map<std::string, std::string> patch_keys;      // patched key -> rel
    for (const ModFile& f : files) {
        ++rep.files;
        const std::string u = to_upper_ascii(f.rel);
        if (u.rfind("README", 0) == 0) readme = true;
        if (u.rfind("LICENSE", 0) == 0 || u.rfind("LICENCE", 0) == 0 || u.rfind("COPYING", 0) == 0) licence = true;
        if (ignored_name(f.rel)) { rep.notes.push_back(f.rel + ": left out by the loader (a dot file or the file manager's own)"); continue; }
        const bool is_patch = u.size() > 8 && u.compare(u.size() - 8, 8, ".B3PATCH") == 0;
        if (const std::string in = below(f.rel, proot); !in.empty()) {   // [files] patches
            if (!is_patch) { rep.warnings.push_back(f.rel + ": not a .b3patch file (the loader leaves it out)"); continue; }
            const std::string key = b3mod::disc_key(in.substr(0, in.size() - 8));
            if (key.empty() || key == "SLUS_210.50" || key == "SYSTEM.CNF") { rep.errors.push_back(f.rel + ": not a disc path the loader patches"); continue; }
            auto [it, fresh] = patch_keys.emplace(key, f.rel);
            if (!fresh) { rep.errors.push_back(f.rel + " and " + it->second + ": patches for the same disc path (names differ only in case)"); continue; }
            patches.push_back({&f, key});
            continue;
        }
        if (is_patch) {
            rep.warnings.push_back(f.rel + ": a b3patch outside " + (proot.empty() ? "patches" : proot) + "/ (the [files] patches folder): the loader does not apply it");
            continue;
        }
        if (!below(f.rel, troot).empty()) {   // [textures] pack: the renderer reads them by name
            b3asset::HdKey k;
            const std::size_t s = f.rel.rfind('/');
            const std::string name = f.rel.substr(s + 1);
            if (u.size() > 4 && u.compare(u.size() - 4, 4, ".DDS") == 0 && b3asset::parse_hd_name(name, k)) ++rep.textures;
            else rep.notes.push_back(f.rel + ": not a replacement texture (<hash>-<hash>-<bits>.dds): the renderer skips it");
            continue;
        }
        if (root.empty() || f.rel.size() <= root.size() + 1 || f.rel.compare(0, root.size(), root) != 0 || f.rel[root.size()] != '/') continue;
        const std::string key = b3mod::disc_key(f.rel.substr(root.size() + 1));
        if (key.empty()) { rep.errors.push_back(f.rel + ": not a disc path"); continue; }
        if (key == "SLUS_210.50" || key == "SYSTEM.CNF") {
            rep.errors.push_back(f.rel + ": the program and SYSTEM.CNF are never read from a mod (the game's code runs natively: use a code mod)");
            continue;
        }
        if (f.size >= (std::uint64_t(1) << 32)) { rep.errors.push_back(f.rel + ": larger than 4 GB"); continue; }
        auto [it, fresh] = keys.emplace(key, f.rel);
        if (!fresh) { rep.errors.push_back(f.rel + " and " + it->second + ": the same disc path (names differ only in case)"); continue; }
        overlay.push_back({&f, key});
        ++rep.overlay_files;
    }
    if (!readme) rep.notes.push_back("no README: say what the mod changes, how to install it and whose work it uses");
    if (!licence) rep.notes.push_back("no LICENSE: say what others may do with your work");
    if (!troot.empty() && rep.textures == 0) rep.warnings.push_back("[textures] pack " + troot + ": no replacement textures in it");
    if (rep.textures) rep.notes.push_back(troot + "/: " + std::to_string(rep.textures) + " replacement textures (the renderer's, above your own HD pack)");
    if (!pm.m.tuning_preset.empty()) {   // tune4: the tuning preset (docs/modding/tuning.md)
        std::string rel = pm.m.tuning_preset;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        const ModFile* pf = nullptr;
        for (const ModFile& f : files)
            if (f.rel == rel) pf = &f;
        Buf pb;
        if (!pf) rep.errors.push_back("[tuning] preset " + pm.m.tuning_preset + " is not in the mod");
        else if (!pf->read(pb, &why)) rep.errors.push_back(why);
        else {
            const b3tune::Preset pr = b3tune::parse_preset(std::string_view(reinterpret_cast<const char*>(pb.data()), pb.size()), pm.m.id, rel);
            for (const std::string& e : pr.errors) rep.errors.push_back(rel + ": " + e);
            if (pr.lines.empty()) rep.warnings.push_back(rel + ": no values: the preset changes nothing");
            rep.notes.push_back(rel + ": a tuning preset of " + std::to_string(pr.lines.size()) + (pr.lines.size() == 1 ? " line" : " lines") + ": the mod changes gameplay");
        }
    }
    if (rep.overlay_files == 0 && patches.empty() && rep.textures == 0 && pm.m.tuning_preset.empty() && !pm.m.code.present && !pm.m.script.present)
        rep.warnings.push_back("no files under " + (root.empty() ? std::string("data") : root) + "/ or " + (proot.empty() ? std::string("patches") : proot) +
                               "/: the mod changes nothing");

    for (auto& [f, key] : patches) {
        if (auto it = keys.find(key); it != keys.end()) {
            rep.errors.push_back(f->rel + " and " + it->second + ": a patch and a file for the same disc path (the loader refuses the mod: keep one)");
            continue;
        }
        Buf p;
        PatchInfo pi;
        if (!f->read(p, &why) || !read_patch_info(p, pi, &why)) { rep.errors.push_back(f->rel + ": " + why); continue; }
        ++rep.patches;
    }

    if (game.empty()) return true;

    // the disc: no file may be a copy of one of its files
    const std::vector<DiscFile> disc = disc_files(game);
    if (disc.empty()) { rep.errors.push_back(game + ": no files (not a disc folder or image)"); return true; }
    std::unordered_map<std::uint64_t, std::vector<const DiscFile*>> by_size;
    std::unordered_map<std::string, const DiscFile*> by_key;
    for (const DiscFile& d : disc) {
        by_size[d.size].push_back(&d);
        by_key[to_upper_ascii(d.key)] = &d;
    }
    std::unordered_map<std::string, std::uint64_t> disc_fnv;
    auto fnv_of = [&](const DiscFile& d) -> std::uint64_t {
        auto it = disc_fnv.find(d.path);
        if (it != disc_fnv.end()) return it->second;
        Buf b;
        const std::uint64_t h = read_host_file(d.path, b) ? b3mod::fnv1a64(b.data(), b.size()) : 0;
        return disc_fnv[d.path] = h;
    };
    std::set<const ModFile*> copies;
    for (const ModFile& f : files) {
        if (f.size < 16 || !by_size.count(f.size)) continue;
        Buf b;
        if (!f.read(b, &why)) { rep.errors.push_back(why); continue; }
        const std::uint64_t h = b3mod::fnv1a64(b.data(), b.size());
        for (const DiscFile* d : by_size[f.size])
            if (fnv_of(*d) == h) {
                rep.errors.push_back(f.rel + ": a copy of the disc's " + d->key + " (a mod must not ship the game's files)");
                copies.insert(&f);
                break;
            }
    }
    for (auto& [f, key] : overlay) {
        if (copies.count(f)) continue;
        auto it = by_key.find(key);
        if (it == by_key.end()) { rep.notes.push_back(f->rel + ": a new path (the disc has no " + key + ")"); continue; }
        if (f->size > (std::uint64_t(512) << 20)) continue;
        Buf a, b;
        if (!f->read(b, &why) || !read_host_file(it->second->path, a)) continue;
        PatchInfo pi;
        make_patch(a, b, &pi);
        if (pi.copied * 2 >= b.size() && b.size() >= 64) {
            rep.warnings.push_back(f->rel + ": " + pct(pi.copied, b.size()) + " of it is the disc's " + it->second->key +
                                   " (ship a b3patch, or only what you made: b3modtool diff; pack refuses it without --force)");
            rep.mostly_disc.push_back(f->rel);
        }
    }
    for (auto& [f, key] : patches) {
        Buf p;
        PatchInfo pi;
        if (!f->read(p, &why) || !read_patch_info(p, pi)) continue;
        auto it = by_key.find(key);
        if (it == by_key.end()) { rep.warnings.push_back(f->rel + ": the disc has no " + key + " to apply it to"); continue; }
        if (it->second->size != pi.src_size || fnv_of(*it->second) != pi.src_fnv)
            rep.errors.push_back(f->rel + ": made from another version of " + it->second->key + " than this disc's");
    }
    return true;
}

bool pack_allowed(const VerifyReport& rep, bool force, std::string* why) {
    if (!rep.errors.empty()) {
        if (why) *why = "not packed: fix the errors first";
        return false;
    }
    if (rep.mostly_disc.empty() || force) return true;
    std::string list;
    for (const std::string& f : rep.mostly_disc) list += (list.empty() ? "" : ", ") + f;
    if (why)
        *why = "not packed: " + list + (rep.mostly_disc.size() == 1 ? " is" : " are") +
               " mostly the disc's own file. Share your change as a patch: b3modtool diff <the disc's file> <your file> "
               "<mod>/patches/<its disc path>.b3patch, then remove your file from the mod (players' games apply the patch to "
               "their own disc). To pack it anyway: --force";
    return false;
}

bool pack_mod(const std::string& dir, const std::string& out, std::size_t* entries, std::string* why) {
    VerifyReport rep;
    std::vector<ModFile> files;
    if (!folder_files(dir, files, rep)) { if (why) *why = rep.errors.empty() ? "cannot list " + dir : rep.errors[0]; return false; }
    std::error_code ec;
    const fs::path outp = fs::weakly_canonical(out, ec);
    std::vector<std::pair<std::string, fs::path>> list;
    for (const ModFile& f : files) {
        if (ignored_name(f.rel)) continue;
        const fs::path p = fs::path(dir) / b3mod::path_from_utf8(f.rel);
        if (fs::weakly_canonical(p, ec) == outp) continue;   // the archive itself, when it is written into the folder
        list.push_back({f.rel, p});
    }
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        const bool am = a.first == b3mod::kManifestName, bm = b.first == b3mod::kManifestName;
        return am != bm ? am : a.first < b.first;
    });
    if (list.empty() || list[0].first != b3mod::kManifestName) { if (why) *why = dir + ": no mod.ini at the top"; return false; }
    b3port::ZipWriter z;
    if (!z.open(fs::path(out))) { if (why) *why = z.error(); return false; }
    for (const auto& [rel, p] : list)
        if (!z.add_file(rel, p)) { if (why) *why = z.error(); return false; }
    if (entries) *entries = z.entries();
    if (!z.close()) { if (why) *why = z.error(); return false; }
    return true;
}

}  // namespace b3modtool
