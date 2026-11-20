// test_mods -- the mod kit's loader (include/b3mod/mods.h; docs/modding/DESIGN.md §1): manifests, mods.ini, the
// overlay's resolution (priority, case, paths that would leave a mod's folder) and the hashes; modkit1b: [requires],
// file conflicts, the renderer's arguments, the disc layer's overlay (b3asset/discfs.h) and installing from crafted
// zips (b3mod/archive.h: zip-slip, links, sizes, manifests); patches3: [files] patches applied (cache, mismatch, set
// hash) and [textures] packs in load order. The mods are folders the
// test writes under build/test_mods.scratch (no game data).
#include "test.h"

#include "b3mod/archive.h"
#include "b3mod/mods.h"
#include "b3asset/discfs.h"
#include "b3report/zip.h"

#include <zlib.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace b3mod;

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


const fs::path kRoot = fs::path("build") / "test_mods.scratch";   // (build/test_mods is the program)

void write(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << bytes;
}

bool has(const std::vector<std::string>& v, const std::string& part) {
    for (const std::string& s : v)
        if (s.find(part) != std::string::npos) return true;
    return false;
}

std::string manifest(const std::string& id, int priority = 0, const std::string& extra = "") {
    return "[mod]\nid = " + id + "\nname = The " + id + "\nversion = 1.0\napi = 1\npriority = " + std::to_string(priority) + "\n" + extra;
}

}  // namespace

TEST(manifest_full) {
    const ParsedManifest p = parse_manifest(
        "# a comment\n[mod]\nid = hello-text\nname = \"Hello  text\"\nversion = 1.2.3\nauthor = Someone\n"
        "description = One line.\napi = 1\npriority = -5\n\n[files]\nroot = files/disc\n");
    CHECK(p.errors.empty());
    CHECK(p.warnings.empty());
    CHECK(p.m.id == "hello-text");
    CHECK(p.m.name == "Hello  text");   // quotes keep the spaces
    CHECK(p.m.version == "1.2.3");
    CHECK(p.m.author == "Someone");
    CHECK(p.m.description == "One line.");
    CHECK(p.m.api == 1);
    CHECK(p.m.priority == -5);
    CHECK(p.m.files_root == "files/disc");
}

TEST(manifest_with_a_utf8_bom) {   // modsec: Notepad's "UTF-8 with BOM"
    const ParsedManifest p = parse_manifest("\xEF\xBB\xBF[mod]\r\nid = bom\r\nversion = 1\r\napi = 1\r\n");
    CHECK(p.errors.empty() && p.warnings.empty() && p.m.id == "bom" && p.m.version == "1");
    CHECK(!parse_manifest("\xEF\xBB[mod]\nid = bom\n").errors.empty());   // half a BOM is not skipped
}

TEST(manifest_defaults_and_warnings) {
    const ParsedManifest p = parse_manifest("[mod]\r\nid = x\r\n");   // CRLF too
    CHECK(p.errors.empty());
    CHECK(p.m.api == 1);
    CHECK(has(p.warnings, "no api version"));
    CHECK(has(p.warnings, "no version"));
    CHECK(p.m.version == "0");
    CHECK(p.m.name == "x");
    CHECK(p.m.files_root == "data");
    const ParsedManifest q = parse_manifest("[mod]\nid = x\napi = 1\nversion = 1\ncolour = red\nid = y\n[content]\ncar = a\n");   // (sdk2b: [code] is read now; tune4: [tuning]; lua6: [script])
    CHECK(q.errors.empty());
    CHECK(q.m.id == "x");                      // the first one counts
    CHECK(has(q.warnings, "colour"));
    CHECK(has(q.warnings, "given again"));
    CHECK(has(q.warnings, "[content]"));
}

TEST(manifest_errors) {
    CHECK(has(parse_manifest("id = x\n").errors, "no [mod] section"));
    CHECK(has(parse_manifest("[mod]\nname = x\n").errors, "no id"));
    CHECK(has(parse_manifest("[mod]\nid = Hello World\n").errors, "only a-z"));
    CHECK(has(parse_manifest("[mod]\nid = .hidden\n").errors, "only a-z"));
    CHECK(has(parse_manifest("[mod]\nid = x\napi = 99\n").errors, "a newer port"));
    CHECK(has(parse_manifest("[mod]\nid = x\napi = one\n").errors, "not a version number"));
    CHECK(has(parse_manifest("[mod]\nid = x\npriority = 1.5\n").errors, "priority"));
    CHECK(has(parse_manifest("[mod]\nid = x\nversion = 1 beta\n").errors, "spaces"));
    for (const char* root : {"../data", "data/../../x", "/etc", "\\abs", "C:\\Games", "C:data", "./data", "a//b", ""})
        CHECK(has(parse_manifest(std::string("[mod]\nid = x\n[files]\nroot = ") + root + "\n").errors, "[files] root"));
    CHECK(parse_manifest("[mod]\nid = x\n[files]\nroot = a/b\\c\n").errors.empty());
}

TEST(ids) {
    CHECK(valid_id("a"));
    CHECK(valid_id("my.mod_v2-x"));
    CHECK(!valid_id(""));
    CHECK(!valid_id("A"));
    CHECK(!valid_id("a b"));
    CHECK(!valid_id("a/b"));
    CHECK(!valid_id(std::string(65, 'a')));
    CHECK((split_ids(" a, b ,,c") == std::vector<std::string>{"a", "b", "c"}));
    CHECK(split_ids("none").empty());
    CHECK(split_ids("").empty());
}

TEST(disc_keys) {
    CHECK(disc_key("cdrom0:\\DATA\\GlobalUs.bin;1") == "DATA/GLOBALUS.BIN");
    CHECK(disc_key("CDROM0:/data/x.txd") == "DATA/X.TXD");
    CHECK(disc_key("Data\\vdb.xml") == "DATA/VDB.XML");
    CHECK(disc_key("\\\\tracks/US\\c2_v2/static.dat") == "TRACKS/US/C2_V2/STATIC.DAT");
    CHECK(disc_key("a;x") == "A;X");                // only a numeric version is dropped
    CHECK(disc_key("").empty());
    CHECK(disc_key("cdrom0:").empty());
    CHECK(disc_key("DATA\\..\\..\\etc\\passwd").empty());
    CHECK(disc_key("../x").empty());
    CHECK(disc_key("./x").empty());
    CHECK(disc_key("C:\\Windows\\win.ini").empty());   // a drive letter
    CHECK(disc_key("host:x").empty());                  // another device
    CHECK(disc_key("a//b").empty());
    CHECK(safe_relative("data"));
    CHECK(safe_relative("a/b"));
    CHECK(!safe_relative(".."));
    CHECK(!safe_relative("/data"));
    CHECK(!safe_relative("D:\\data"));
}

TEST(order_ini) {
    Order o;
    o.entries = {{"b", true}, {"a", false}, {"c", true}};
    const std::string t = order_to_ini(o);
    CHECK(order_from_ini(t) == o);
    const Order r = order_from_ini("[order]\nx = on\nBad Id = on\ny = 0\nx = off\nz = yes\n");
    CHECK(r.entries.size() == 3);
    CHECK((r.entries[0] == OrderEntry{"x", true}));
    CHECK((r.entries[1] == OrderEntry{"y", false}));
    CHECK((r.entries[2] == OrderEntry{"z", true}));
    CHECK((enabled_ids(r) == std::vector<std::string>{"x", "z"}));
    const fs::path f = kRoot / "pref" / "mods.ini";
    fs::remove_all(kRoot / "pref");
    Order none;
    CHECK(!load_order(f, none));
    std::string why;
    CHECK(save_order(f, o, &why));
    Order back;
    CHECK(load_order(f, back));
    CHECK(back == o);
    CHECK(order_file(kRoot / "pref") == f);
    CHECK(mods_folder(kRoot / "pref") == kRoot / "pref" / "mods");
}

TEST(discover_and_order) {
    const fs::path root = kRoot / "discover";
    fs::remove_all(root);
    write(root / "m1" / "mod.ini", manifest("low", 0));
    write(root / "m2" / "mod.ini", manifest("high", 10));
    write(root / "m3" / "mod.ini", manifest("low", 5));            // the id again
    write(root / "m4" / "mod.ini", "[mod]\nid = newer\napi = 7\n"); // a newer api
    write(root / "m5" / "readme.txt", "no manifest: not a mod");
    write(root / "m1" / "data" / "DATA" / "A.BIN", "a");
    const std::vector<Mod> found = discover(root);
    CHECK(found.size() == 4);
    const Mod* low = find_mod(found, "low");
    CHECK(low && low->folder == "m1" && low->usable());
    CHECK(find_mod(found, "high") && has(find_mod(found, "high")->warnings, "no data folder"));
    CHECK(!found[2].usable() && has(found[2].errors, "already taken"));
    CHECK(find_mod(found, "newer") && !find_mod(found, "newer")->usable());
    CHECK(discover(root / "missing").empty());
    // a saved order keeps its place; new mods follow, the higher priority first, all off
    Order saved;
    saved.entries = {{"gone", true}, {"low", true}};
    const Order m = merged_order(saved, found);
    CHECK(m.entries.size() == 3);
    CHECK((m.entries[0] == OrderEntry{"low", true}));
    CHECK((m.entries[1] == OrderEntry{"high", false}));
    CHECK((m.entries[2] == OrderEntry{"newer", false}));
    const Selection s = select(found, {"low", "low", "newer", "nothere"});
    CHECK(s.mods.size() == 1 && s.mods[0] == low);
    CHECK(s.errors.size() == 2 && has(s.errors, "newer: ") && has(s.errors, "nothere: not installed"));
}

TEST(overlay_priority_and_case) {
    const fs::path root = kRoot / "overlay";
    fs::remove_all(root);
    write(root / "a" / "mod.ini", manifest("a"));
    write(root / "a" / "data" / "DATA" / "GLOBALUS.BIN", "from a");
    write(root / "a" / "data" / "DATA" / "ONLY_A.BIN", "only a");
    write(root / "a" / "data" / ".DS_Store", "ignored");
    write(root / "a" / "data" / "Thumbs.db", "ignored");
    write(root / "b" / "mod.ini", manifest("b", 0, "[files]\nroot = disc\n"));
    write(root / "b" / "disc" / "data" / "globalus.bin", "from b");   // lower case on the host
    write(root / "b" / "disc" / "Tracks" / "New.dat", "new file");
    write(root / "b" / "disc" / "SLUS_210.50", "not the program");
    const std::vector<Mod> found = discover(root);
    const Mod *a = find_mod(found, "a"), *b = find_mod(found, "b");
    CHECK(a && b && a->usable() && b->usable());
    {   // a first
        const Overlay o = Overlay::build({a, b});
        CHECK(o.mods().size() == 2);
        const ActiveMod* who = nullptr;
        const OverlayFile* f = o.find("cdrom0:\\Data\\GlobalUs.bin;1", &who);
        CHECK(f && who && who->id == "a" && f->size == 6);
        CHECK(o.find("DATA/ONLY_A.BIN") != nullptr);
        f = o.find("tracks\\NEW.DAT", &who);
        CHECK(f && who->id == "b" && f->size == 8);
        CHECK(o.find("DATA/NOT_THERE.BIN") == nullptr);
        CHECK(o.find(".DS_Store") == nullptr);
        CHECK(o.find("THUMBS.DB") == nullptr);
        CHECK(o.find("SLUS_210.50") == nullptr);                 // never from a file
        CHECK(has(o.mods()[1].notes, "SLUS_210.50"));
        CHECK(o.find("DATA\\..\\DATA\\GLOBALUS.BIN") == nullptr);  // no escaping, even back in
        CHECK(o.find("C:\\x\\DATA\\GLOBALUS.BIN") == nullptr);
        CHECK(o.file_count() == 3);
    }
    {   // b first
        const Overlay o = Overlay::build({b, a});
        const ActiveMod* who = nullptr;
        CHECK(o.find("DATA/GLOBALUS.BIN", &who) && who->id == "b");
        CHECK(o.find("DATA/GLOBALUS.BIN")->size == 6);
    }
    {   // none: empty, hash 0
        const Overlay o = Overlay::build({});
        CHECK(o.empty());
        CHECK(o.set_hash() == 0);
        CHECK(o.find("DATA/GLOBALUS.BIN") == nullptr);
        CHECK(describe(o) == "none");
    }
}

TEST(overlay_refusals) {
    const fs::path root = kRoot / "refuse";
    fs::remove_all(root);
    write(root / "outside.bin", "secret");
    write(root / "m" / "mod.ini", manifest("m"));
    write(root / "m" / "data" / "DATA" / "X.BIN", "x upper");
    write(root / "m" / "data" / "DATA" / "x.bin", "x lower");   // differs only in case: both left out (a case-insensitive host has one)
    write(root / "m" / "data" / "DATA" / "OK.BIN", "ok");
#ifndef _WIN32
    std::error_code ec;
    fs::create_symlink(fs::absolute(root / "outside.bin"), root / "m" / "data" / "DATA" / "LINK.BIN", ec);
    fs::create_directory_symlink(fs::absolute(root), root / "m" / "data" / "UP", ec);   // a folder link: not followed
    write(root / "n" / "mod.ini", manifest("n", 0, "[files]\nroot = data\n"));
    fs::create_directory_symlink(fs::absolute(root), root / "n" / "data", ec);          // the root itself leads out
#endif
    const std::vector<Mod> found = discover(root);
    const Mod* m = find_mod(found, "m");
    CHECK(m && m->usable());
    const Overlay o = Overlay::build({m});
    CHECK(o.find("DATA/OK.BIN") != nullptr);
#ifndef _WIN32
    const bool case_sensitive_host = case_sensitive_dir(root / "probe");
    if (case_sensitive_host) {
        CHECK(o.find("DATA/X.BIN") == nullptr);
        CHECK(has(o.mods()[0].notes, "differ only in case"));
    }
    CHECK(o.find("DATA/LINK.BIN") == nullptr);
    CHECK(has(o.mods()[0].notes, "link out of the mod"));
    CHECK(o.find("UP/OUTSIDE.BIN") == nullptr);
    const Mod* n = find_mod(found, "n");
    CHECK(n && !n->usable() && has(n->errors, "leads out"));
#endif
}

TEST(hashes) {
    const fs::path root = kRoot / "hash";
    fs::remove_all(root);
    write(root / "one" / "m" / "mod.ini", manifest("m"));
    write(root / "one" / "m" / "data" / "DATA" / "A.BIN", "alpha");
    write(root / "one" / "m" / "data" / "DATA" / "B.BIN", "beta");
    write(root / "one" / "k" / "mod.ini", manifest("k"));
    write(root / "one" / "k" / "data" / "DATA" / "A.BIN", "kappa");
    // the same mod in another place, its files in another case: the same mod hash
    write(root / "two" / "m" / "mod.ini", manifest("m"));
    write(root / "two" / "m" / "data" / "data" / "a.bin", "alpha");
    write(root / "two" / "m" / "data" / "Data" / "b.BIN", "beta");
    const std::vector<Mod> one = discover(root / "one"), two = discover(root / "two");
    const Overlay o1 = Overlay::build({find_mod(one, "m")});
    const Overlay o2 = Overlay::build({find_mod(two, "m")});
    CHECK(o1.set_hash() != 0);
    CHECK(o1.mods()[0].hash == o2.mods()[0].hash);
    CHECK(o1.set_hash() == o2.set_hash());
    // the documented text formula
    std::uint64_t fa = 0, sa = 0;
    CHECK(hash_file(root / "one" / "m" / "data" / "DATA" / "A.BIN", fa, sa));
    CHECK(sa == 5 && fa == fnv1a64("alpha", 5));
    const std::string files = "file DATA/A.BIN 5 " + hex64(fnv1a64("alpha", 5)) + "\nfile DATA/B.BIN 4 " + hex64(fnv1a64("beta", 4)) + "\n";
    CHECK(o1.mods()[0].hash == fnv1a64(files.data(), files.size()));
    const std::string set = "b3mods api 1\nmod m 1.0 " + hex64(o1.mods()[0].hash) + "\n";
    CHECK(o1.set_hash() == fnv1a64(set.data(), set.size()));
    CHECK(describe(o1) == "m 1.0 (2 files); set hash " + hex64(o1.set_hash()));
    // order, contents and versions all count
    const Overlay mk = Overlay::build({find_mod(one, "m"), find_mod(one, "k")});
    const Overlay km = Overlay::build({find_mod(one, "k"), find_mod(one, "m")});
    CHECK(mk.set_hash() != km.set_hash());
    write(root / "two" / "m" / "data" / "data" / "a.bin", "alphA");
    CHECK(Overlay::build({find_mod(discover(root / "two"), "m")}).set_hash() != o1.set_hash());
    write(root / "two" / "m" / "data" / "data" / "a.bin", "alpha");
    write(root / "two" / "m" / "mod.ini", "[mod]\nid = m\nname = The m\nversion = 1.1\napi = 1\n");
    CHECK(Overlay::build({find_mod(discover(root / "two"), "m")}).set_hash() != o1.set_hash());
    CHECK(hex64(0x0123456789ABCDEFull) == "0123456789abcdef");
}

TEST(example_manifests) {   // examples/mods: each one's mod.ini stays valid (run from the repo root, as make test does)
    const std::vector<Mod> found = discover(fs::path("examples") / "mods");
    CHECK(!found.empty());
    for (const Mod& m : found) {
        CHECK(m.usable());
        CHECK(m.manifest.api >= 1 && m.manifest.api <= kApiVersion);   // (sdk2b: the code-mod examples say api 2)
        CHECK(m.manifest.id == m.folder);
        for (const std::string& w : m.warnings) CHECK(w.find("no data folder") != std::string::npos);   // they ship no game data
    }
    CHECK(find_mod(found, "hello-texture") != nullptr);
}

// ================================================================================================ modkit1b
TEST(requires_manifest_and_versions) {
    const ParsedManifest p = parse_manifest(manifest("patch", 0, "[requires]\nbase = 1.2\nother =\nany = *\n"));
    CHECK(p.errors.empty());
    CHECK(!has(p.warnings, "[requires]"));   // a known section now
    CHECK(p.m.requirements.size() == 3);
    CHECK(p.m.requirements[0].id == "base" && p.m.requirements[0].min_version == "1.2");
    CHECK(p.m.requirements[1].id == "other" && p.m.requirements[1].min_version.empty());
    CHECK(p.m.requirements[2].id == "any" && p.m.requirements[2].min_version.empty());
    CHECK(has(parse_manifest(manifest("x", 0, "[requires]\nBad Id = 1\n")).errors, "not a mod id"));
    CHECK(has(parse_manifest(manifest("x", 0, "[requires]\nbase = 1 beta\n")).errors, "has spaces"));
    CHECK(has(parse_manifest(manifest("x", 0, "[requires]\nx = 1\n")).errors, "the mod itself"));
    CHECK(compare_versions("1.2", "1.2.0") == 0);
    CHECK(compare_versions("1.10", "1.9") > 0);
    CHECK(compare_versions("1.2", "1.10") < 0);
    CHECK(compare_versions("2", "1.99.99") > 0);
    CHECK(compare_versions("1.0-beta", "1.0-alpha") > 0);
    CHECK(compare_versions("1.1", "1.beta") > 0);   // a number before text
    CHECK(compare_versions("0", "") == 0);
}

TEST(requires_select) {
    const fs::path root = kRoot / "requires";
    fs::remove_all(root);
    write(root / "base" / "mod.ini", "[mod]\nid = base\nversion = 1.4\napi = 1\n");
    write(root / "patch" / "mod.ini", manifest("patch", 0, "[requires]\nbase = 1.2\n"));
    write(root / "newer" / "mod.ini", manifest("newer", 0, "[requires]\nbase = 2.0\n"));
    write(root / "orphan" / "mod.ini", manifest("orphan", 0, "[requires]\nmissing-mod =\n"));
    write(root / "chain" / "mod.ini", manifest("chain", 0, "[requires]\norphan =\n"));
    write(root / "broken" / "mod.ini", "[mod]\nid = broken\napi = 99\n");
    write(root / "needs-broken" / "mod.ini", manifest("needs-broken", 0, "[requires]\nbroken =\n"));
    const std::vector<Mod> found = discover(root);
    {   // the requirement on and new enough: both in, in the order given
        const Selection s = select(found, {"patch", "base"});
        CHECK(s.errors.empty());
        CHECK(s.mods.size() == 2 && s.mods[0]->manifest.id == "patch");
        CHECK(s.notes.empty());
    }
    {   // the requirement off (not in the list): left out with a message
        const Selection s = select(found, {"patch"});
        CHECK(s.mods.empty());
        CHECK(has(s.errors, "patch: it requires base 1.2 or newer, which is not on: left out"));
    }
    {   // too old
        const Selection s = select(found, {"newer", "base"});
        CHECK(s.mods.size() == 1 && s.mods[0]->manifest.id == "base");
        CHECK(has(s.errors, "newer: it requires base 2.0 or newer; version 1.4 is installed"));
    }
    {   // not installed, and a chain: chain requires orphan, which is left out
        const Selection s = select(found, {"chain", "orphan", "base"});
        CHECK(s.mods.size() == 1 && s.mods[0]->manifest.id == "base");
        CHECK(has(s.errors, "orphan: it requires missing-mod, which is not installed"));
        CHECK(has(s.errors, "chain: it requires orphan, which is not on"));
    }
    {   // a requirement that cannot be used
        const Selection s = select(found, {"needs-broken", "broken"});
        CHECK(s.mods.empty());
        CHECK(has(s.errors, "needs-broken: it requires broken, which cannot be used"));
    }
    {   // below its requirement: kept, with a note (the list decides which file wins)
        const Selection s = select(found, {"base", "patch"});
        CHECK(s.errors.empty() && s.mods.size() == 2);
        CHECK(has(s.notes, "patch requires base and is below it"));
    }
}

TEST(file_conflicts) {
    const fs::path root = kRoot / "conflicts";
    fs::remove_all(root);
    write(root / "a" / "mod.ini", manifest("a"));
    write(root / "a" / "data" / "DATA" / "GLOBAL.TXD", "a");
    write(root / "a" / "data" / "DATA" / "ONLY_A.BIN", "a");
    write(root / "b" / "mod.ini", manifest("b"));
    write(root / "b" / "data" / "data" / "global.txd", "b");   // the same path in another case
    write(root / "b" / "data" / "TRACKS" / "X.DAT", "b");
    write(root / "c" / "mod.ini", manifest("c"));
    write(root / "c" / "data" / "DATA" / "GLOBAL.TXD", "c");
    write(root / "c" / "data" / "TRACKS" / "X.DAT", "c");
    const std::vector<Mod> found = discover(root);
    const Overlay o = Overlay::build({find_mod(found, "a"), find_mod(found, "b"), find_mod(found, "c")}, false);
    const std::vector<FileConflict> v = conflicts(o);
    CHECK(v.size() == 2);
    CHECK(v[0].key == "DATA/GLOBAL.TXD" && v[0].winner == "a" && (v[0].hidden == std::vector<std::string>{"b", "c"}));
    CHECK(v[1].key == "TRACKS/X.DAT" && v[1].winner == "b" && (v[1].hidden == std::vector<std::string>{"c"}));
    const ModConflicts ca = conflicts_of(v, "a"), cb = conflicts_of(v, "b"), cc = conflicts_of(v, "c");
    CHECK(ca.wins.size() == 1 && ca.wins[0].first == "DATA/GLOBAL.TXD" && ca.wins[0].second == "b" && ca.hidden.empty());
    CHECK(cb.wins.size() == 1 && cb.wins[0].first == "TRACKS/X.DAT" && cb.hidden.size() == 1 && cb.hidden[0].second == "a");
    CHECK(cc.wins.empty() && cc.hidden.size() == 2);
    // the winners: one file per path, the first mod's
    const auto w = o.winners();
    CHECK(w.size() == 3);
    CHECK(w[0].first == "DATA/GLOBAL.TXD" && w[0].second == root / "a" / "data" / "DATA" / "GLOBAL.TXD");
    CHECK(w[2].first == "TRACKS/X.DAT" && w[2].second == root / "b" / "data" / "TRACKS" / "X.DAT");
    // reordered: c first wins both
    const std::vector<FileConflict> v2 = conflicts(Overlay::build({find_mod(found, "c"), find_mod(found, "b"), find_mod(found, "a")}, false));
    CHECK(v2.size() == 2 && v2[0].winner == "c" && v2[1].winner == "c");
    CHECK(conflicts(Overlay::build({find_mod(found, "a")}, false)).empty());
    // the renderer's arguments name the same set
    const Overlay h = Overlay::build({find_mod(found, "b"), find_mod(found, "a")});
    CHECK(renderer_args(root, h) == " --mods-dir \"" + path_utf8(root) + "\" --mods b,a --mods-hash " + hex64(h.set_hash()));
    CHECK(renderer_args(root, Overlay::build({})).empty());
    std::string why;
    CHECK(renderer_args(fs::path("a\"b"), h, &why).empty() && !why.empty());
}

TEST(discover_skips_hidden_folders) {
    const fs::path root = kRoot / "hidden";
    fs::remove_all(root);
    write(root / ".install-x" / "mod.ini", manifest("x"));
    write(root / "y" / "mod.ini", manifest("y"));
    const std::vector<Mod> found = discover(root);
    CHECK(found.size() == 1 && found[0].manifest.id == "y");
}

// ---- the disc layer's overlay (b3asset/discfs.h set_overlay): what the renderer and the launcher read through
TEST(discfs_overlay) {
    namespace disc = b3asset::disc;
    const fs::path root = kRoot / "discfs";
    fs::remove_all(root);
    const fs::path game = root / "game";
    write(game / "DATA" / "GLOBAL.TXD", "disc txd");
    write(game / "TRACKS" / "US" / "C1" / "STATIC.DAT", "disc static");
    write(game / "TRACKS" / "US" / "C1" / "STREAMED.DAT", "disc streamed");
    write(root / "mod" / "STATIC.DAT", "mod static!");
    write(root / "mod" / "NEW.DAT", "new route");
    write(root / "mod" / "TXD", "mod txd");
    std::vector<std::uint8_t> d;
    auto text = [&d]() { return std::string(d.begin(), d.end()); };
    CHECK(!disc::has_overlay());
    CHECK(disc::read_file(game / "TRACKS/US/C1/STATIC.DAT", d) && text() == "disc static");
    disc::set_overlay(game, {{"DATA/GLOBAL.TXD", root / "mod" / "TXD"},
                             {"TRACKS/US/C1/STATIC.DAT", root / "mod" / "STATIC.DAT"},
                             {"TRACKS/US/C9/STATIC.DAT", root / "mod" / "NEW.DAT"}});
    CHECK(disc::has_overlay());
    // the mod's bytes, whatever the case or form of the path
    CHECK(disc::read_file(game / "TRACKS/US/C1/STATIC.DAT", d) && text() == "mod static!");
    CHECK(disc::read_file(game.string() + "/tracks/us/c1/static.dat", d) && text() == "mod static!");
    CHECK(disc::read_file(game / "TRACKS" / "US" / ".." / "US" / "C1" / "STATIC.DAT", d) && text() == "mod static!");
    CHECK(disc::file_size(game / "TRACKS/US/C1/STATIC.DAT") == 11);
    disc::File f;
    CHECK(f.open(game / "DATA/GLOBAL.TXD") && f.size() == 7);
    // the disc's other files as before
    CHECK(disc::read_file(game / "TRACKS/US/C1/STREAMED.DAT", d) && text() == "disc streamed");
    // a route only the mod has: in the folder listing, a folder, found by resolve_ci
    const std::vector<disc::Entry> us = disc::list_dir(game / "TRACKS" / "US");
    CHECK(us.size() == 2);
    bool c9 = false;
    for (const disc::Entry& e : us) c9 = c9 || (e.name == "C9" && e.dir && e.path == game / "TRACKS" / "US" / "C9");
    CHECK(c9);
    CHECK(disc::is_dir(game / "TRACKS/US/C9") && disc::exists(game / "TRACKS/US/C9/STATIC.DAT"));
    CHECK(disc::is_file(game / "TRACKS/US/C9/STATIC.DAT") && !disc::is_dir(game / "TRACKS/US/C9/STATIC.DAT"));
    CHECK(disc::list_dir(game / "TRACKS/US/C9").size() == 1);
    const fs::path r = disc::resolve_ci(game, "tracks/us/c9/static.dat");
    CHECK(!r.empty() && disc::read_file(r, d) && text() == "new route");
    CHECK(disc::resolve_ci(game, "TRACKS/US/C1/STATIC.DAT") == game / "TRACKS" / "US" / "C1" / "STATIC.DAT");
    CHECK(disc::resolve_ci(game, "TRACKS/US/C8/STATIC.DAT").empty());
    // a path outside the root: untouched
    write(root / "elsewhere" / "TRACKS" / "US" / "C1" / "STATIC.DAT", "elsewhere");
    CHECK(disc::read_file(root / "elsewhere" / "TRACKS/US/C1/STATIC.DAT", d) && text() == "elsewhere");
    CHECK(!disc::exists(root / "elsewhere" / "TRACKS/US/C9"));
    // cleared: the disc alone
    disc::set_overlay(game, {});
    CHECK(!disc::has_overlay());
    CHECK(disc::read_file(game / "TRACKS/US/C1/STATIC.DAT", d) && text() == "disc static");
    CHECK(!disc::exists(game / "TRACKS/US/C9"));
    CHECK(disc::list_dir(game / "TRACKS" / "US").size() == 1);
}

// ---- installing from an archive (b3mod/archive.h): crafted zips
namespace {

struct RawEntry {
    std::string name, data;
    bool deflate = false;
    std::uint16_t made_by = 20, flags = 0;   // 20: MS-DOS host, version 2.0
    std::uint32_t external = 0;
    long long usize = -1;                    // the header's size (-1: the data's)
    long long crc = -1;                      // the header's CRC (-1: the data's)
};

void put16(std::string& b, unsigned v) { b += char(v & 0xFF); b += char((v >> 8) & 0xFF); }
void put32(std::string& b, std::uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }

// A zip written byte by byte, so an entry can say anything a hostile archive could
void write_zip(const fs::path& p, const std::vector<RawEntry>& es, bool zip64_marker = false) {
    std::string out, cd;
    for (const RawEntry& e : es) {
        std::string body = e.data;
        if (e.deflate) {
            z_stream z{};
            deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            std::string o(deflateBound(&z, uLong(e.data.size())) + 16, '\0');
            z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(e.data.data()));
            z.avail_in = uInt(e.data.size());
            z.next_out = reinterpret_cast<Bytef*>(o.data());
            z.avail_out = uInt(o.size());
            deflate(&z, Z_FINISH);
            o.resize(z.total_out);
            deflateEnd(&z);
            body = o;
        }
        const std::uint32_t crc = e.crc >= 0 ? std::uint32_t(e.crc) : b3port::crc32_of(e.data.data(), e.data.size());
        const std::uint32_t us = e.usize >= 0 ? std::uint32_t(e.usize) : std::uint32_t(e.data.size());
        const std::uint32_t off = std::uint32_t(out.size());
        put32(out, 0x04034b50u); put16(out, 20); put16(out, e.flags); put16(out, e.deflate ? 8 : 0); put16(out, 0); put16(out, 0x21);
        put32(out, crc); put32(out, std::uint32_t(body.size())); put32(out, us); put16(out, unsigned(e.name.size())); put16(out, 0);
        out += e.name + body;
        put32(cd, 0x02014b50u); put16(cd, e.made_by); put16(cd, 20); put16(cd, e.flags); put16(cd, e.deflate ? 8 : 0); put16(cd, 0); put16(cd, 0x21);
        put32(cd, crc); put32(cd, std::uint32_t(body.size())); put32(cd, us); put16(cd, unsigned(e.name.size())); put16(cd, 0); put16(cd, 0);
        put16(cd, 0); put16(cd, 0); put32(cd, e.external); put32(cd, off);
        cd += e.name;
    }
    const std::uint32_t cd_off = std::uint32_t(out.size());
    out += cd;
    if (zip64_marker) { put32(out, 0x07064b50u); put32(out, 0); put32(out, 0); put32(out, 0); put32(out, 1); }
    put32(out, 0x06054b50u); put16(out, 0); put16(out, 0); put16(out, unsigned(es.size())); put16(out, unsigned(es.size()));
    put32(out, std::uint32_t(cd.size())); put32(out, cd_off); put16(out, 0);
    write(p, out);
}

const std::string kIni = "[mod]\nid = zipped\nname = Zipped\nversion = 1.0\napi = 1\n";

std::size_t count_entries(const fs::path& dir) {
    std::size_t n = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) ++n;
    return n;
}

}  // namespace

TEST(zip_reader) {
    const fs::path root = kRoot / "zipread";
    fs::remove_all(root);
    fs::create_directories(root);
    {   // the bug report's writer (b3report/zip.h) read back: stored and deflated entries
        b3port::ZipWriter w;
        CHECK(w.open(root / "w.zip"));
        const std::string big(100000, 'x');
        CHECK(w.add("a/b.txt", "hello", 0));
        CHECK(w.add("big.bin", big.data(), big.size(), 0));
        CHECK(w.close());
        ZipReader z;
        std::string why;
        CHECK(z.open(root / "w.zip", &why));
        CHECK(z.entries().size() == 2);
        if (z.entries().size() != 2) return;
        std::vector<std::uint8_t> d;
        CHECK(z.read_all(z.entries()[0], 1 << 20, d, &why) && std::string(d.begin(), d.end()) == "hello");
        CHECK(z.read_all(z.entries()[1], 1 << 20, d, &why) && d.size() == 100000 && z.entries()[1].method == 8);
        CHECK(!z.read_all(z.entries()[1], 1000, d, &why) && why.find("over the limit") != std::string::npos);
    }
    ZipReader z;
    std::string why;
    write(root / "junk.zip", "this is not a zip archive at all, just text");
    CHECK(!z.open(root / "junk.zip", &why) && why.find("not a zip") != std::string::npos);
    write_zip(root / "z64.zip", {{"mod.ini", kIni}}, true);
    CHECK(!z.open(root / "z64.zip", &why) && why.find("ZIP64") != std::string::npos);
    // more data than the header says (a decompression bomb's lie), a bad CRC
    write_zip(root / "lie.zip", {{"f.bin", std::string(5000, 'a'), true, 20, 0, 0, 100}});
    CHECK(z.open(root / "lie.zip", &why) && z.entries().size() == 1);
    if (z.entries().size() != 1) return;
    std::vector<std::uint8_t> d;
    CHECK(!z.read_all(z.entries()[0], 1 << 20, d, &why) && why.find("more data than its header") != std::string::npos);
    write_zip(root / "crc.zip", {{"f.bin", "abc", false, 20, 0, 0, -1, 0x12345678}});
    CHECK(z.open(root / "crc.zip", &why) && z.entries().size() == 1);
    if (z.entries().size() != 1) return;
    CHECK(!z.read_all(z.entries()[0], 1 << 20, d, &why) && why.find("CRC") != std::string::npos);
}

TEST(entry_names) {
    for (const char* ok : {"mod.ini", "data/DATA/GLOBAL.TXD", "data/", "a b/c-d_e.f", "data\\DATA\\X.BIN", "caf\xc3\xa9.txt"})
        CHECK(unsafe_entry_name(ok).empty());
    for (const char* bad : {"../evil.txt", "data/../../evil", "/etc/passwd", "\\windows\\x", "C:/x", "C:x", "a//b", "./mod.ini",
                            "a/./b", "a/..", "con.txt", "data/NUL", "LPT1", "trailing.", "space ", "a\x01" "b", "q?.txt",
                            "a|b", "bad\xff.txt", ""})
        CHECK(!unsafe_entry_name(bad).empty());
}

TEST(install_archive_good) {
    const fs::path root = kRoot / "install";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path mods = root / "mods";
    // mod.ini at the top, written by the bug report's writer
    {
        b3port::ZipWriter w;
        CHECK(w.open(root / "top.b3mod"));
        CHECK(w.add("mod.ini", kIni, 0));
        CHECK(w.add("data/DATA/GLOBAL.TXD", std::string(3000, 't'), 0));
        CHECK(w.add("README.txt", "read me", 0));
        CHECK(w.close());
    }
    InstallResult r = install_archive(root / "top.b3mod", mods);
    CHECK(r.ok && r.errors.empty());
    CHECK(r.id == "zipped" && r.version == "1.0" && r.files == 3 && !r.replaced);
    CHECK(r.dir == mods / "zipped");
    CHECK(fs::file_size(mods / "zipped" / "data" / "DATA" / "GLOBAL.TXD") == 3000);
    std::vector<Mod> found = discover(mods);
    CHECK(found.size() == 1 && found[0].usable() && found[0].manifest.id == "zipped");
    CHECK(Overlay::build({&found[0]}).find("DATA/GLOBAL.TXD") != nullptr);
    // the same id again: refused (offer Replace), the installed one untouched
    write_zip(root / "folder.zip", {{"My Mod/", ""},
                                    {"My Mod/mod.ini", "[mod]\nid = zipped\nversion = 2.0\napi = 1\n"},
                                    {"My Mod/data/DATA/FRONTEND.TXD", "v2", true},
                                    {"__MACOSX/My Mod/._mod.ini", "finder"}});
    r = install_archive(root / "folder.zip", mods);
    CHECK(!r.ok && r.exists && has(r.errors, "installed already"));
    CHECK(fs::exists(mods / "zipped" / "data" / "DATA" / "GLOBAL.TXD"));
    // Replace: the one top folder dropped, __MACOSX left out, the old files gone
    InstallOptions o;
    o.replace = true;
    r = install_archive(root / "folder.zip", mods, o);
    CHECK(r.ok && r.replaced && r.version == "2.0" && r.files == 2);
    CHECK(fs::exists(mods / "zipped" / "mod.ini"));
    CHECK(fs::exists(mods / "zipped" / "data" / "DATA" / "FRONTEND.TXD"));
    CHECK(!fs::exists(mods / "zipped" / "data" / "DATA" / "GLOBAL.TXD"));
    CHECK(!fs::exists(mods / "zipped" / "__MACOSX"));
    CHECK(!fs::exists(mods / ".install-zipped") && !fs::exists(mods / ".remove-zipped"));
    found = discover(mods);
    CHECK(found.size() == 1 && found[0].manifest.version == "2.0");
}

TEST(install_archive_top_folder_in_two_cases) {   // modsec: "My Mod/" and "my mod/" are one folder on Windows / macOS
    const fs::path root = kRoot / "installcase";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path mods = root / "mods";
    write_zip(root / "case.zip", {{"My Mod/mod.ini", "[mod]\nid = cased\nversion = 1\napi = 1\n"},
                                  {"my mod/data/DATA/A.TXD", "a", true},
                                  {"MY MOD/data/DATA/B.TXD", "b"}});
    const InstallResult r = install_archive(root / "case.zip", mods);
    CHECK(r.ok && r.files == 3);
    CHECK(fs::exists(mods / "cased" / "data" / "DATA" / "A.TXD") && fs::exists(mods / "cased" / "data" / "DATA" / "B.TXD"));
}

TEST(replace_does_not_follow_links) {   // modsec: Replace removes the old folder's own files only
    const fs::path root = fs::absolute(kRoot / "replacelink");
    fs::remove_all(root);
    const fs::path mods = root / "mods", outside = root / "outside", shared = root / "shared";
    write(outside / "mod.ini", "[mod]\nid = linked\nversion = 1\napi = 1\n");
    write(outside / "precious.txt", "the user's own files");
    write(shared / "keep.txt", "also not the mod's");
    fs::create_directories(mods);
    std::error_code ec;
    fs::create_directory_symlink(outside, mods / "linked", ec);   // the mod's folder is a link to elsewhere
    CHECK(!ec);
    fs::create_directory_symlink(shared, outside / "data", ec);    // and a link inside it
    CHECK(!ec);
    write_zip(root / "new.zip", {{"mod.ini", "[mod]\nid = linked\nversion = 2\napi = 1\n"}, {"data/DATA/X.BIN", "x"}});
    InstallOptions o;
    o.replace = true;
    const InstallResult r = install_archive(root / "new.zip", mods, o);
    CHECK(r.ok && r.replaced);
    CHECK(fs::exists(outside / "precious.txt") && fs::exists(outside / "mod.ini") && fs::exists(shared / "keep.txt"));
    CHECK(!fs::is_symlink(mods / "linked") && fs::exists(mods / "linked" / "data" / "DATA" / "X.BIN"));
    CHECK(!fs::exists(mods / ".remove-linked"));
    // remove_tree itself: links removed as links, real files removed
    const fs::path t = root / "tree";
    write(t / "a" / "f.txt", "f");
    fs::create_directory_symlink(shared, t / "a" / "l", ec);
    fs::create_symlink(shared / "keep.txt", t / "fl", ec);
    CHECK(remove_tree(t) && !fs::exists(t) && fs::exists(shared / "keep.txt"));
    CHECK(remove_tree(root / "not-there"));
}

TEST(install_says_when_it_cannot_clean_up) {   // modsec (review): remove_tree's result is not ignored
    const fs::path root = fs::absolute(kRoot / "cleanup");
    std::error_code e0;
    fs::permissions(root / "mods" / ".install-stuck" / "locked", fs::perms::owner_all, e0);   // (a run stopped half way)
    fs::remove_all(root, e0);
    const fs::path mods = root / "mods";
    write(mods / ".install-stuck" / "locked" / "file.txt", "x");
    fs::permissions(mods / ".install-stuck" / "locked", fs::perms::owner_read | fs::perms::owner_exec);   // its file cannot go
    std::error_code ec;
    fs::remove(mods / ".install-stuck" / "locked" / "file.txt", ec);
    const bool locked = fs::exists(mods / ".install-stuck" / "locked" / "file.txt");
    write_zip(root / "s.zip", {{"mod.ini", "[mod]\nid = stuck\nversion = 1\napi = 1\n"}, {"data/A.BIN", "a"}});
    const InstallResult r = install_archive(root / "s.zip", mods);
    if (locked) {   // (root removes it anyway)
        CHECK(!r.ok && has(r.errors, "cannot clear") && has(r.warnings, "could not remove all of"));
        CHECK(!fs::exists(mods / "stuck"));
    }
    fs::permissions(mods / ".install-stuck" / "locked", fs::perms::owner_all);
    const InstallResult r2 = install_archive(root / "s.zip", mods);
    CHECK(r2.ok && r2.warnings.empty() && fs::exists(mods / "stuck" / "data" / "A.BIN") && !fs::exists(mods / ".install-stuck"));
}

TEST(a_pack_of_game_files_without_mod_ini) {   // item 11: a community pack of replacement files (synthetic here)
    const fs::path root = fs::absolute(kRoot / "filespack");
    fs::remove_all(root);
    const fs::path mods = root / "mods";
    // the game's folders at the top, any case, and a readme: offered, then installed as a file-replacement mod
    write_zip(root / "Cool Cars #2.zip", {{"Pveh/Muscle/CAR1.BGV", "car bytes", true}, {"pveh/MUSCLE/CAR2.BGV", "car two"}, {"readme.txt", "by someone"}});
    InstallResult r = install_archive(root / "Cool Cars #2.zip", mods);
    CHECK(!r.ok && r.files_pack && has(r.errors, "file-replacement mod") && !fs::exists(mods / "cool-cars-2"));
    InstallOptions o;
    o.files_mod_name = "Cool Cars #2";
    r = install_archive(root / "Cool Cars #2.zip", mods, o);
    CHECK(r.ok && r.id == "cool-cars-2" && r.name == "Cool Cars  2" && r.files == 3);
    CHECK(fs::exists(mods / "cool-cars-2" / "data" / "Pveh" / "Muscle" / "CAR1.BGV") && fs::exists(mods / "cool-cars-2" / "readme.txt"));
    std::vector<Mod> found = discover(mods);
    CHECK(found.size() == 1 && found[0].usable() && found[0].warnings.empty() && found[0].manifest.api == 1 &&
          !found[0].manifest.code.present && !found[0].manifest.script.present && found[0].manifest.files_root == "data");
    if (!found.empty()) {
        const Overlay ov = Overlay::build({&found[0]});
        CHECK(ov.find("PVEH/MUSCLE/CAR1.BGV") != nullptr && ov.find("PVEH/MUSCLE/CAR2.BGV") != nullptr && ov.find("README.TXT") == nullptr);
    }
    // the same pack again: another id (never over the first)
    r = install_archive(root / "Cool Cars #2.zip", mods, o);
    CHECK(r.ok && r.id == "cool-cars-2-2");
    // one wrapper folder (a zipped folder), the game's folders inside it
    write_zip(root / "wrapped.zip", {{"Wrapper/", ""}, {"Wrapper/tracks/AREA/X.DAT", "t"}, {"Wrapper/DATA/GLOBAL.TXD", "d"}});
    o.files_mod_name = "wrapped";
    r = install_archive(root / "wrapped.zip", mods, o);
    CHECK(r.ok && r.id == "wrapped" && fs::exists(mods / "wrapped" / "data" / "tracks" / "AREA" / "X.DAT") && !fs::exists(mods / "wrapped" / "data" / "Wrapper"));
    // anything else without a mod.ini: refused as before, and not offered
    write_zip(root / "stuff.zip", {{"stuff/thing.txt", "x"}, {"other/x.bin", "y"}});
    r = install_archive(root / "stuff.zip", mods, o);
    CHECK(!r.ok && !r.files_pack && has(r.errors, "no game folders"));
    write_zip(root / "mixed.zip", {{"PVEH/A/CAR1.BGV", "x"}, {"Screenshots/a.png", "y"}});   // a folder that is not the game's
    r = install_archive(root / "mixed.zip", mods, o);
    CHECK(!r.ok && !r.files_pack);
    // a folder: the same rules (a pack, a mod with mod.ini), its files copied, the folder left as it was
    write(root / "My Pack" / "SOUND" / "X.AWD", "s");
    InstallOptions f;
    r = install_folder(root / "My Pack", mods, f);
    CHECK(!r.ok && r.files_pack);
    f.files_mod_name = "My Pack";
    r = install_folder(root / "My Pack", mods, f);
    CHECK(r.ok && r.id == "my-pack" && fs::exists(mods / "my-pack" / "data" / "SOUND" / "X.AWD") && fs::exists(root / "My Pack" / "SOUND" / "X.AWD"));
    write(root / "real" / "inner" / "mod.ini", "[mod]\nid = real\nversion = 1\napi = 1\n");
    write(root / "real" / "inner" / "data" / "DATA" / "A.BIN", "a");
    r = install_folder(root / "real", mods, {});
    CHECK(r.ok && r.id == "real" && fs::exists(mods / "real" / "data" / "DATA" / "A.BIN"));
    std::error_code ec;
    fs::create_directory_symlink(root / "real", root / "My Pack" / "DATA", ec);   // a link inside: refused
    r = install_folder(root / "My Pack", mods, f);
    CHECK(!r.ok && has(r.errors, "a link"));
    r = install_folder(mods / "real", mods, {});   // a folder of the mods folder itself
    CHECK(!r.ok && has(r.errors, "inside the mods folder"));
}

TEST(install_archive_refusals) {
    const fs::path root = kRoot / "refusezip";
    fs::remove_all(root);
    const fs::path mods = root / "mods";
    write(mods / "other" / "mod.ini", manifest("other"));
    const std::size_t before = count_entries(root);
    auto refused = [&](const std::vector<RawEntry>& es, const std::string& why, const InstallOptions& o = {}) {
        write_zip(root / "t.zip", es);
        const InstallResult r = install_archive(root / "t.zip", mods, o);
        const bool ok = !r.ok && has(r.errors, why);
        if (!ok) for (const std::string& e : r.errors) std::printf("    (got: %s)\n", e.c_str());
        return ok;
    };
    const RawEntry ini{"mod.ini", kIni};
    // zip-slip in every form
    CHECK(refused({ini, {"../evil.txt", "x"}}, "leads out"));
    CHECK(refused({ini, {"data/../../../evil.txt", "x"}}, "leads out"));
    CHECK(refused({ini, {"..\\evil.txt", "x"}}, "leads out"));
    CHECK(refused({ini, {"/tmp/evil.txt", "x"}}, "absolute path"));
    CHECK(refused({ini, {"\\evil.txt", "x"}}, "absolute path"));
    CHECK(refused({ini, {"C:/evil.txt", "x"}}, "drive letter"));
    CHECK(refused({ini, {"C:evil.txt", "x"}}, "drive letter"));
    // links (Unix st_mode S_IFLNK, a Windows reparse point)
    CHECK(refused({ini, {"data/link", "/etc/passwd", false, 0x031E, 0, 0120777u << 16}}, "a link"));
    CHECK(refused({ini, {"data/link2", "x", false, 0x0A14, 0, 0x400}}, "a link"));
    // sizes: one entry over the limit, too much in all, too many entries
    InstallOptions small;
    small.max_entry = 1000;
    CHECK(refused({ini, {"data/big.bin", std::string(2000, 'b'), true}}, "over the limit", small));
    InstallOptions total;
    total.max_total = 1500;
    CHECK(refused({ini, {"a.bin", std::string(1000, 'a')}, {"b.bin", std::string(1000, 'b')}}, "in all, over the limit", total));
    InstallOptions few;
    few.max_entries = 2;
    CHECK(refused({ini, {"a", "a"}, {"b", "b"}}, "entries: more than", few));
    // the header lies about the size: found while unpacking, nothing kept
    CHECK(refused({ini, {"data/lie.bin", std::string(5000, 'a'), true, 20, 0, 0, 10}}, "more data than its header"));
    // encrypted, another method, duplicates by case, a file and a folder of one name
    CHECK(refused({ini, {"data/x.bin", "x", false, 20, 1}}, "encrypted"));
    CHECK(refused({ini, {"data/A.BIN", "1"}, {"data/a.bin", "2"}}, "differ only in case"));
    CHECK(refused({ini, {"data/x", "1"}, {"data/x/y", "2"}}, "both a file and a folder"));
    // the manifest: none, not at the top, two top folders, broken, an api too new
    CHECK(refused({{"data/x.bin", "x"}}, "no mod.ini"));
    CHECK(refused({{"a/b/mod.ini", kIni}}, "no mod.ini"));
    CHECK(refused({{"a/mod.ini", kIni}, {"b/x", "x"}}, "no mod.ini"));
    CHECK(refused({{"mod.ini", "[mod]\nname = no id\n"}}, "mod.ini: no id"));
    CHECK(refused({{"mod.ini", "[mod]\nid = x\napi = 99\n"}}, "a newer port"));
    CHECK(refused({{"mod.ini", "[mod]\nid = x\napi = 1\n[files]\nroot = ../..\n"}}, "[files] root"));
    // a folder of that name that is not this mod
    write(mods / "zipped" / "notes.txt", "the user's own folder");
    CHECK(refused({ini}, "not this mod"));
    fs::remove_all(mods / "zipped");
    // nothing was written anywhere: no evil files, no half-installed mod, no leftovers
    CHECK(!fs::exists(root / "evil.txt") && !fs::exists(kRoot / "evil.txt") && !fs::exists(mods / "evil.txt"));
    CHECK(count_entries(root) == before + 1);   // + t.zip
    CHECK(discover(mods).size() == 1);
}

// ---- patches3: [files] patches applied at run time, [textures] packs ----
namespace {

std::string bytes_of(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
void put_le(std::string& b, std::uint64_t v, int n) { for (int i = 0; i < n; ++i) b += char((v >> (8 * i)) & 0xFF); }
void put_var(std::string& b, std::uint64_t v) {
    while (v >= 0x80) { b += char((v & 0x7F) | 0x80); v >>= 7; }
    b += char(v);
}
// A b3patch made by hand (docs/modding/b3patch.md): `src` -> its first `keep` bytes, then `insert`, then the rest of
// src after `skip` more bytes.
std::string hand_patch(const std::string& src, std::size_t keep, const std::string& insert, std::size_t skip) {
    const std::string dst = src.substr(0, keep) + insert + src.substr(keep + skip);
    std::string p("B3PATCH\0", 8);
    put_le(p, 1, 4);
    put_le(p, 0, 4);
    put_le(p, src.size(), 8);
    put_le(p, fnv1a64(src.data(), src.size()), 8);
    put_le(p, dst.size(), 8);
    put_le(p, fnv1a64(dst.data(), dst.size()), 8);
    p += char(1); put_var(p, 0); put_var(p, keep);
    p += char(2); put_var(p, insert.size()); p += insert;
    p += char(1); put_var(p, keep + skip); put_var(p, src.size() - keep - skip);
    p += char(0);
    return p;
}
// the patched result hand_patch describes
std::string patched(const std::string& src, std::size_t keep, const std::string& insert, std::size_t skip) {
    return src.substr(0, keep) + insert + src.substr(keep + skip);
}
// a PatchEnv over a disc folder (as native_play's over the platform's disc, without the case search)
PatchEnv disc_env(const fs::path& disc, const fs::path& cache, int* reads = nullptr) {
    PatchEnv e;
    e.cache = cache;
    e.read_disc = [disc, reads](const std::string& key, std::vector<std::uint8_t>& out) {
        if (reads) ++*reads;
        std::ifstream f(disc / key, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    };
    return e;
}
const std::string kDiscThing = "The quick brown fox jumps over the lazy dog; this is the disc's own THING.BIN, version one.";

}  // namespace

TEST(manifest_patches_and_textures) {
    ParsedManifest p = parse_manifest(manifest("m"));
    CHECK(p.errors.empty() && p.m.files_patches == "patches" && p.m.textures.empty());
    p = parse_manifest(manifest("m", 0, "[files]\npatches = deltas\n[textures]\npack = hd/replacements\n"));
    CHECK(p.errors.empty() && p.m.files_patches == "deltas" && p.m.textures == "hd/replacements");
    CHECK(!has(p.warnings, "[textures] is not used"));
    p = parse_manifest(manifest("m", 0, "[files]\npatches = ../elsewhere\n[textures]\npack = /abs\n"));
    CHECK(has(p.errors, "[files] patches \"../elsewhere\"") && has(p.errors, "[textures] pack \"/abs\""));
    p = parse_manifest(manifest("m", 0, "[textures]\nfolder = t\n"));
    CHECK(p.errors.empty() && has(p.warnings, "[textures] folder is not known"));
}

TEST(patches_applied_and_cached) {
    const fs::path root = kRoot / "patch", disc = root / "disc", mods = root / "mods", cache = patch_cache(mods);
    fs::remove_all(root);
    write(disc / "DATA" / "THING.BIN", kDiscThing);
    write(mods / "p" / "mod.ini", manifest("p"));
    const std::string pb = hand_patch(kDiscThing, 4, "QUICK", 5);
    write(mods / "p" / "patches" / "data" / "thing.bin.b3patch", pb);   // any case, as overlay paths
    write(mods / "p" / "patches" / "README.txt", "not a patch");
    const std::string want = patched(kDiscThing, 4, "QUICK", 5);
    std::vector<Mod> found = discover(mods);
    CHECK(found.size() == 1 && found[0].usable() && !has(found[0].warnings, "replaces no game files"));
    // read_patch_info / apply_patch, as b3modtool's
    PatchInfo pi;
    const std::vector<std::uint8_t> pv(pb.begin(), pb.end()), sv(kDiscThing.begin(), kDiscThing.end());
    CHECK(read_patch_info(pv, pi) && pi.src_size == kDiscThing.size() && pi.dst_size == want.size() && pi.ops == 3 && pi.inserted == 5);
    std::vector<std::uint8_t> out;
    CHECK(apply_patch(sv, pv, out) && std::string(out.begin(), out.end()) == want);

    int reads = 0;
    const PatchEnv env = disc_env(disc, cache, &reads);
    const Overlay o = Overlay::build({&found[0]}, true, &env);
    CHECK(o.errors().empty() && o.mods().size() == 1 && reads == 1);
    const ActiveMod& a = o.mods()[0];
    CHECK(a.patched == 1 && a.files.size() == 1 && has(a.notes, "patches/README.txt: not a .b3patch file"));
    const OverlayFile* f = o.find("cdrom0:\\DATA\\THING.BIN;1");
    CHECK(f && f->key == "DATA/THING.BIN" && !f->patch.empty());
    CHECK(f && f->size == want.size() && f->fnv == fnv1a64(want.data(), want.size()));
    CHECK(f && f->patch_size == pb.size() && f->patch_fnv == fnv1a64(pb.data(), pb.size()));
    // the result: in the cache, named by the patch's and the source's hashes, served like any overlay file
    const fs::path cached = cache / (hex64(fnv1a64(pb.data(), pb.size())) + "-" + hex64(fnv1a64(kDiscThing.data(), kDiscThing.size())) + ".bin");
    CHECK(f && f->host == cached && bytes_of(cached) == want);
    CHECK(o.winners().size() == 1 && o.winners()[0].second == cached);
    // the disc layer serves it to the renderer's readers (b3asset/discfs.h)
    b3asset::disc::set_overlay(disc, o.winners());
    std::vector<std::uint8_t> got;
    CHECK(b3asset::disc::read_file(disc / "data" / "thing.bin", got) && std::string(got.begin(), got.end()) == want);
    b3asset::disc::set_overlay(disc, {});
    CHECK(b3asset::disc::read_file(disc / "DATA" / "THING.BIN", got) && std::string(got.begin(), got.end()) == kDiscThing);
    // the next start reuses the cache file (not written again: its time stays)
    const auto t_old = fs::file_time_type::clock::now() - std::chrono::hours(48);
    fs::last_write_time(cached, t_old);
    const Overlay o2 = Overlay::build({&found[0]}, true, &env);
    CHECK(o2.errors().empty() && o2.mods()[0].files[0].host == cached && fs::last_write_time(cached) == t_old);
    // a damaged cache file is made again, a deleted cache too (the folder is safe to delete)
    write(cached, std::string(want.size(), 'x'));
    CHECK(Overlay::build({&found[0]}, true, &env).errors().empty() && bytes_of(cached) == want);
    fs::remove_all(cache);
    CHECK(Overlay::build({&found[0]}, true, &env).errors().empty() && bytes_of(cached) == want);
    // nothing written into the mod's folder or the disc's (the temporary file renamed away)
    std::size_t in_cache = 0;
    for (const auto& e : fs::directory_iterator(cache)) in_cache += e.is_regular_file();
    CHECK(in_cache == 1);
    CHECK(bytes_of(disc / "DATA" / "THING.BIN") == kDiscThing);
    // no disc to apply to: listed by the header only (the launcher without a game folder), or refused when hashing
    const Overlay listed = Overlay::build({&found[0]}, false);
    CHECK(listed.errors().empty() && listed.mods()[0].files[0].host.empty() && listed.mods()[0].files[0].size == want.size());
    CHECK(listed.winners().empty() && listed.file_count() == 1);
    const Overlay nodisc = Overlay::build({&found[0]});
    CHECK(nodisc.empty() && has(nodisc.errors(), "no disc was given"));
}

TEST(patch_mismatch_disables_the_mod) {
    const fs::path root = kRoot / "patchbad", disc = root / "disc", mods = root / "mods";
    fs::remove_all(root);
    write(disc / "DATA" / "THING.BIN", kDiscThing);
    write(mods / "p" / "mod.ini", manifest("p"));
    write(mods / "p" / "patches" / "DATA" / "THING.BIN.b3patch", hand_patch(kDiscThing, 4, "QUICK", 5));
    write(mods / "q" / "mod.ini", manifest("q", 0, "[requires]\np = 1\n"));
    write(mods / "q" / "data" / "DATA" / "Q.BIN", "q");
    write(mods / "r" / "mod.ini", manifest("r"));
    write(mods / "r" / "data" / "DATA" / "R.BIN", "r");
    const std::vector<Mod> found = discover(mods);
    const PatchEnv env = disc_env(disc, patch_cache(mods));
    const Selection sel = select(found, {"r", "q", "p"});
    CHECK(sel.errors.empty() && sel.mods.size() == 3);
    CHECK(Overlay::build(sel.mods, true, &env).mods().size() == 3);
    // another version of the file on this disc: p is left out with the reason, q (which requires it) too, r stays
    write(disc / "DATA" / "THING.BIN", kDiscThing + " (version two)");
    const Overlay o = Overlay::build(sel.mods, true, &env);
    CHECK(o.mods().size() == 1 && o.mods()[0].id == "r");
    CHECK(has(o.errors(), "p: patches/DATA/THING.BIN.b3patch was made from another version of DATA/THING.BIN than your disc's"));
    CHECK(has(o.errors(), "the mod is off for this run"));
    CHECK(has(o.errors(), "q: it requires p, which is off for this run"));
    CHECK(o.left_out().size() == 2 && !o.find("DATA/THING.BIN") && !o.find("DATA/Q.BIN") && o.find("DATA/R.BIN"));
    // the disc lacks the file; a damaged patch; a patch and a file for one path
    fs::remove(disc / "DATA" / "THING.BIN");
    CHECK(has(Overlay::build({find_mod(found, "p")}, true, &env).errors(), "your disc has no DATA/THING.BIN"));
    write(disc / "DATA" / "THING.BIN", kDiscThing);
    write(mods / "p" / "patches" / "DATA" / "THING.BIN.b3patch", "B3PATCH junk junk junk junk junk junk junk junk junk junk");
    CHECK(has(Overlay::build({find_mod(found, "p")}, true, &env).errors(), "p: patches/DATA/THING.BIN.b3patch: not a b3patch file"));
    write(mods / "p" / "patches" / "DATA" / "THING.BIN.b3patch", hand_patch(kDiscThing, 4, "QUICK", 5));
    write(mods / "p" / "data" / "DATA" / "THING.BIN", "whole");
    CHECK(has(Overlay::build({find_mod(discover(mods), "p")}, true, &env).errors(), "a mod has one or the other"));
}

TEST(patch_set_hash) {
    const fs::path root = kRoot / "patchhash", disc = root / "disc";
    fs::remove_all(root);
    write(disc / "DATA" / "THING.BIN", kDiscThing);
    const std::string pb = hand_patch(kDiscThing, 10, "abc", 0);
    // the same mod on two machines: other folders, other caches, its files in another case
    write(root / "one" / "p" / "mod.ini", manifest("p"));
    write(root / "one" / "p" / "patches" / "DATA" / "THING.BIN.b3patch", pb);
    write(root / "one" / "p" / "data" / "DATA" / "NEW.BIN", "new");
    write(root / "two" / "p" / "mod.ini", manifest("p"));
    write(root / "two" / "p" / "patches" / "data" / "Thing.bin.B3PATCH", pb);
    write(root / "two" / "p" / "data" / "Data" / "new.bin", "new");
    const PatchEnv e1 = disc_env(disc, root / "cache1"), e2 = disc_env(disc, root / "cache2");
    const std::vector<Mod> one = discover(root / "one"), two = discover(root / "two");
    const Overlay o1 = Overlay::build({find_mod(one, "p")}, true, &e1);
    const Overlay o2 = Overlay::build({find_mod(two, "p")}, true, &e2);
    CHECK(o1.errors().empty() && o2.errors().empty());
    CHECK(o1.set_hash() != 0 && o1.set_hash() == o2.set_hash());
    // the documented text: a "patch" line with the patch file's size and hash, by key among the plain files
    const std::string text = "file DATA/NEW.BIN 3 " + hex64(fnv1a64("new", 3)) + "\npatch DATA/THING.BIN " + std::to_string(pb.size()) + " " +
                             hex64(fnv1a64(pb.data(), pb.size())) + "\n";
    CHECK(o1.mods()[0].hash == fnv1a64(text.data(), text.size()));
    const std::string set = "b3mods api 1\nmod p 1.0 " + hex64(o1.mods()[0].hash) + "\n";
    CHECK(o1.set_hash() == fnv1a64(set.data(), set.size()));
    // the launcher's view (no hashing) names the same patch the same way
    CHECK(Overlay::build({find_mod(one, "p")}, false, &e1).mods()[0].files[1].patch_fnv == fnv1a64(pb.data(), pb.size()));
    // another patch (the same target made differently would count as another mod): another hash
    write(root / "two" / "p" / "patches" / "data" / "Thing.bin.B3PATCH", hand_patch(kDiscThing, 10, "abd", 0));
    CHECK(Overlay::build({find_mod(discover(root / "two"), "p")}, true, &e2).set_hash() != o1.set_hash());
}

TEST(texture_packs_in_load_order) {
    const fs::path root = kRoot / "textures";
    fs::remove_all(root);
    write(root / "a" / "mod.ini", manifest("a", 0, "[textures]\npack = textures\n"));
    write(root / "a" / "textures" / "x.dds", "a");
    write(root / "b" / "mod.ini", manifest("b", 0, "[textures]\npack = hd\n"));
    write(root / "b" / "hd" / "sub" / "x.dds", "b");
    write(root / "c" / "mod.ini", manifest("c"));
    write(root / "c" / "data" / "DATA" / "C.BIN", "c");
    write(root / "d" / "mod.ini", manifest("d", 0, "[textures]\npack = missing\n"));
    write(root / "d" / "data" / "DATA" / "D.BIN", "d");
    const std::vector<Mod> found = discover(root);
    CHECK(find_mod(found, "a")->usable() && !has(find_mod(found, "a")->warnings, "replaces no game files"));   // textures only
    CHECK(has(find_mod(found, "d")->warnings, "no missing folder ([textures] pack)"));
    const Selection s1 = select(found, {"b", "c", "a", "d"});
    const std::vector<fs::path> t1 = Overlay::build(s1.mods).texture_packs();
    CHECK(t1.size() == 2 && t1[0] == root / "b" / "hd" && t1[1] == root / "a" / "textures");
    const std::vector<fs::path> t2 = Overlay::build(select(found, {"a", "b"}).mods).texture_packs();
    CHECK(t2.size() == 2 && t2[0] == root / "a" / "textures");
    CHECK(Overlay::build(select(found, {"c"}).mods).texture_packs().empty());
    // the textures are the renderer's alone: not in the mod hash (the game never reads them)
    const std::uint64_t before = Overlay::build(select(found, {"a"}).mods).set_hash();
    write(root / "a" / "textures" / "x.dds", "a changed");
    write(root / "a" / "textures" / "y.dds", "another");
    CHECK(Overlay::build(select(discover(root), {"a"}).mods).set_hash() == before);
}

// media1: which mods keep their ghosts and best times apart (src/b3replay/records.h)
TEST(changes_gameplay_any_parts) {
    ActiveMod m;
    CHECK(!changes_gameplay_any(m));                         // files only (textures, sounds)
    m.code.present = true;
    CHECK(!changes_gameplay_any(m));                         // code, gameplay = no (the default)
    m.code.gameplay = true;
    CHECK(changes_gameplay_any(m));
    ActiveMod s;
    s.script.present = true;
    s.script.gameplay = false;
    CHECK(!changes_gameplay_any(s));                         // a script that only reads and draws
    s.script.gameplay = true;
    CHECK(changes_gameplay_any(s));
    ActiveMod t;
    t.tuning = "preset.ini";
    CHECK(changes_gameplay_any(t) && t.changes_gameplay());  // a tuning preset
}

// media1: the modded ghosts' folder is named by the gameplay-changing mods alone (b3mod::gameplay_set_hash)
TEST(gameplay_set_hash_ignores_other_mods) {
    ActiveMod gp, gp2, tex, tex2;
    gp.id = "faster-cars"; gp.version = "1.0"; gp.hash = 0x1111; gp.tuning = "preset.ini";
    gp2.id = "lua-ai"; gp2.version = "2"; gp2.hash = 0x2222; gp2.script.present = true; gp2.script.gameplay = true;
    tex.id = "hd-cars"; tex.version = "3"; tex.hash = 0x3333; tex.textures = "textures";
    tex2.id = "speedo"; tex2.version = "1"; tex2.hash = 0x4444; tex2.script.present = true; tex2.script.gameplay = false;
    CHECK(gameplay_set_hash({}).empty());
    CHECK(gameplay_set_hash({tex, tex2}).empty());   // nothing changes gameplay: the plain folder
    const std::string h = gameplay_set_hash({gp, gp2});
    CHECK(h.size() == 16);
    CHECK(gameplay_set_hash({tex, gp, gp2}) == h);          // a texture mod added
    CHECK(gameplay_set_hash({gp, tex, gp2, tex2}) == h);    // added and moved between them, a read-only script too
    CHECK(gameplay_set_hash({gp2, gp}) != h);               // the gameplay mods' own order counts
    CHECK(gameplay_set_hash({gp}) != h);                    // one of them off
    ActiveMod gp_new = gp;
    gp_new.hash = 0x1112;                                   // its content changed
    CHECK(gameplay_set_hash({gp_new, gp2}) != h);
    gp_new = gp;
    gp_new.version = "1.1";
    CHECK(gameplay_set_hash({gp_new, gp2}) != h);
    CHECK(set_hash_text({tex, gp, gp2}) != set_hash_text({gp, gp2}));   // (the whole set's hash does move)
}

TEST_MAIN("test_mods: the mod kit's loader (manifests, load order, overlay, hashes, requirements, conflicts, archives, patches, texture packs)")
