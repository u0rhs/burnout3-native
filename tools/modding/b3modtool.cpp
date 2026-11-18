// b3modtool: the mod kit's asset tool (docs/modding/b3modtool.md; design docs/modding/DESIGN.md §3).
//   b3modtool list <disc> [pattern]                    every file: size, kind, path
//   b3modtool extract <disc> <pattern> <dir>           copy files out (for editing, never for sharing)
//   b3modtool txd list <file.TXD>                      the rasters of a texture dictionary
//   b3modtool txd export <file.TXD> <dir>              each raster's levels as PNG, and txd.txt
//   b3modtool txd import <orig.TXD> <dir> <out.TXD>    the PNGs of <dir> back into a copy of the dictionary
//   b3modtool strings export <file.BIN> <out.txt>      a string table as UTF-8 text
//   b3modtool strings import <orig.BIN> <in.txt> <out.BIN>
//   b3modtool diff <orig> <modded> <out.b3patch>       a binary delta (docs/modding/b3patch.md)
//   b3modtool apply <orig> <patch.b3patch> <out>
//   b3modtool pack <mod dir> <out.b3mod> [--game <disc>] [--allow-exports] [--force]
//   b3modtool verify <mod dir | file.b3mod> [--game <disc>] [--allow-exports]
//   b3modtool vdb export <VDB.XML> <out.txt> [--names <table>]     the tuning defaults as `name = value` text
//   b3modtool vdb import <in.txt> <out VDB.XML> [--base <VDB.XML>] [--names <table>]
//   b3modtool audio list <file>                        an RWS stream's segments / a bank's waves
//   b3modtool audio export <file> <dir>                each segment / wave as a WAV, and audio.txt
//   b3modtool audio import <orig> <dir> <out>          the WAVs of <dir> back into a copy of the file (PS-ADPCM)
//   b3modtool roundtrip <disc> [txd | strings | vdb | audio | all]   export -> import over every such file: the census
//   b3modtool roundtrip <disc> rasters                 the write-back over the rasters inside tracks / cars (report only)
//   b3modtool car list <disc>                          the cars, in the game's order with the game's names
//   b3modtool car export <disc> <car | all> <dir> [--lod N|all] [--format gltf|obj] [--paint K]
//   b3modtool car import <disc> <model.gltf|.glb|export dir> <mod dir | out.BGV> [--sidecar <file.b3car.json>]
//   b3modtool car check <disc> <model.gltf|.glb|export dir> [--sidecar <file.b3car.json>]   the import, nothing written
//   b3modtool track list <disc>                        the routes and their sections
//   b3modtool track export <disc> <route | all> <dir> [--section N|all] [--format gltf|obj]
//   b3modtool track import <disc> <model.gltf|.glb|export dir> <mod dir | out.b3mod> [--sidecar <file.b3track.json>] [--restrip]
//   b3modtool track check <disc> <model.gltf|.glb|export dir> [--sidecar <file.b3track.json>]   the import, nothing written
//   b3modtool preview <file.gltf> <out.png> [--yaw D] [--pitch D] [--skip NAME,...]   a picture of an export, offscreen
// <disc> is a folder of the disc's files or the disc image (.iso / .bin / .chd); a file argument may be a path inside an
// image (<image>/DATA/GLOBAL.TXD). Nothing is ever written to the disc: only to the output paths given.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "b3asset/discfs.h"
#include "b3asset/gs_memory.h"
#include "b3asset/preview.h"
#include "b3mod/mods.h"
#include "b3modtool/export3d.h"
#include "b3modtool/gltf.h"
#include "b3modtool/modtool.h"

using namespace b3modtool;
namespace fs = std::filesystem;
namespace disc = b3asset::disc;

namespace {

const char* kUsage =
    "b3modtool: the Burnout 3 port's mod asset tool (docs/modding/b3modtool.md)\n"
    "  list <disc> [pattern]                     every file: size, kind, path\n"
    "  extract <disc> <pattern> <dir>            copy files out for editing (never share them)\n"
    "  txd list <file.TXD>                       the rasters of a texture dictionary\n"
    "  txd export <file.TXD> <dir>               each raster as PNG (+ .mip<k>.png levels) and txd.txt\n"
    "  txd import <orig.TXD> <dir> <out.TXD> [--keep-palette] [--keep-mips]\n"
    "  strings export <file.BIN> <out.txt>       a string table (GLOBALUS.BIN, HEADUS.BIN) as UTF-8 text\n"
    "  strings import <orig.BIN> <in.txt> <out.BIN>\n"
    "  diff <orig> <modded> <out.b3patch>        a binary delta against the original file\n"
    "  apply <orig> <patch.b3patch> <out>\n"
    "  pack <mod dir> <out.b3mod> [--game <disc>] [--allow-exports] [--force]\n"
    "  verify <mod dir | file.b3mod> [--game <disc>] [--allow-exports]\n"
    "  vdb export <VDB.XML> <out.txt> [--names <table>]   the tuning defaults (DATA/VDB.XML) as name = value text\n"
    "  vdb import <in.txt> <out VDB.XML> [--base <VDB.XML>] [--names <table>]\n"
    "  audio list <file>                         an RWS stream's segments or a bank's (.AWD .HWD .LWD) waves\n"
    "  audio export <file> <dir>                 each segment / wave as a WAV, and audio.txt\n"
    "  audio import <orig> <dir> <out>           the WAVs of <dir> back into a copy of the file (PS-ADPCM)\n"
    "  roundtrip <disc> [txd | strings | vdb | audio | all]   export -> import of every such file: the same bytes\n"
    "  roundtrip <disc> rasters                  the same write-back over the rasters in tracks and cars (report only)\n"
    "  car list <disc>                           the cars: the game's order and names, and the traffic vehicles\n"
    "  car export <disc> <car | all> <dir> [--lod N|all] [--format gltf|obj] [--paint K]\n"
    "                                            a car as glTF 2.0 (+ .bin, PNGs, a .b3car.json sidecar) or OBJ / MTL\n"
    "  car import <disc> <model.gltf | model.glb | export dir> <mod dir | out.BGV> [--sidecar <file.b3car.json>]\n"
    "                                            an exported car, edited (vertices, UVs, triangles, the paint PNGs), back\n"
    "                                            into the game's file: a mod dir gets patches/PVEH/../CARn.BGV.b3patch\n"
    "  car check <disc> <model.gltf | model.glb | export dir> [--sidecar <file.b3car.json>]\n"
    "                                            the same import and its checks, nothing written\n"
    "  track list <disc>                         the routes and their sections (streamed units)\n"
    "  track export <disc> <route | all> <dir> [--section N|all] [--format gltf|obj]\n"
    "                                            a route's meshes, materials and textures by section (+ a report)\n"
    "  track import <disc> <model.gltf | model.glb | export dir> <mod dir | out.b3mod> [--sidecar <file.b3track.json>] [--restrip]\n"
    "                                            an exported route, edited (vertices, UVs, colours, triangles, materials,\n"
    "                                            the texture PNGs), back as patches of its STATIC.DAT / STREAMED.DAT\n"
    "  track check <disc> <model.gltf | model.glb | export dir> [--sidecar <file.b3track.json>]\n"
    "                                            the same import and its checks, nothing written\n"
    "  preview <file.gltf> <out.png> [--yaw D] [--pitch D] [--skip NAME,NAME]   a quick picture of an export (no window)\n"
    "<disc>: a folder of the disc's files or the disc image (.iso / .bin / .chd). Nothing is written to it.\n";

int usage() { std::fputs(kUsage, stderr); return 2; }
int fail(const std::string& m) { std::fflush(stdout); std::fprintf(stderr, "b3modtool: %s\n", m.c_str()); return 1; }

bool load(const std::string& p, Buf& b) { return read_host_file(p, b); }

// the output must not be one of the inputs (or the disc)
bool same_file(const std::string& a, const std::string& b) {
    std::error_code ec;
    return fs::exists(a, ec) && fs::exists(b, ec) && fs::equivalent(a, b, ec);
}

int cmd_list(const std::string& d, const std::string& pat) {
    if (!disc::is_dir(d)) return fail(d + ": not a disc folder or image");
    std::uint64_t n = 0, bytes = 0;
    for (const DiscFile& f : disc_files(d)) {
        if (!pat.empty() && !glob_match(pat, f.key)) continue;
        std::uint8_t head[16] = {};
        disc::File df;
        std::size_t got = 0;
        if (df.open(f.path)) { const std::int64_t r = df.read_at(0, head, sizeof head); got = r > 0 ? std::size_t(r) : 0; }
        const std::string kind = file_kind(f.key, head, got);
        std::printf("%12llu  %-18s %s\n", (unsigned long long)f.size, kind.empty() ? "-" : kind.c_str(), f.key.c_str());
        ++n;
        bytes += f.size;
    }
    std::printf("%llu files, %llu bytes\n", (unsigned long long)n, (unsigned long long)bytes);
    return 0;
}

int cmd_extract(const std::string& d, const std::string& pat, const std::string& out) {
    if (!disc::is_dir(d)) return fail(d + ": not a disc folder or image");
    std::uint64_t n = 0, bytes = 0;
    for (const DiscFile& f : disc_files(d)) {
        if (!glob_match(pat, f.key)) continue;
        Buf b;
        if (!load(f.path, b)) return fail("cannot read " + f.key);
        const std::string dst = b3mod::path_utf8(fs::path(out) / b3mod::path_from_utf8(f.key));
        if (same_file(dst, f.path)) return fail(dst + " is the disc's own file");
        std::string why;
        if (!write_host_file(dst, b, &why)) return fail(why);
        ++n;
        bytes += b.size();
    }
    std::printf("extract: %llu files, %llu bytes into %s (the game's files: for editing on this PC, never for sharing)\n",
                (unsigned long long)n, (unsigned long long)bytes, out.c_str());
    return n ? 0 : fail("no file matches " + pat);
}

int cmd_txd_list(const std::string& p) {
    Buf b;
    if (!load(p, b)) return fail("cannot read " + p);
    std::vector<std::size_t> offs;
    std::string why;
    if (!txd_offsets(b, offs, &why)) return fail(p + ": " + why);
    int bad = 0;
    for (std::size_t i = 0; i < offs.size(); ++i) {
        RasterView v;
        if (!view_raster(b, offs[i], v, &why)) { std::printf("%3zu  (not decoded: %s)\n", i, why.c_str()); ++bad; continue; }
        const b3asset::RasterHeader& h = v.dec.hdr;
        std::printf("%3zu  %-28s %4ux%-4u %-8s levels %u%s%s%s%s\n", i, h.name.c_str(), h.width, h.height, b3asset::psm_name(h.psm()), h.levels,
                    v.indexed ? (std::string(" clut ") + b3asset::psm_name(h.cpsm())).c_str() : "",
                    h.palettes > 1 ? (" palettes " + std::to_string(h.palettes)).c_str() : "", v.dec.translucent ? " translucent" : "",
                    v.overlap.empty() ? "" : " EXPORT ONLY (uploads overlap)");
    }
    std::printf("%s: %zu rasters%s\n", p.c_str(), offs.size(), bad ? (", " + std::to_string(bad) + " not decoded").c_str() : "");
    return bad ? 1 : 0;
}

int cmd_txd_export(const std::string& p, const std::string& out) {
    Buf b;
    if (!load(p, b)) return fail("cannot read " + p);
    FileSet fsx;
    std::string why;
    if (!txd_export(b, fs::path(p).filename().string(), fsx, &why)) return fail(p + ": " + why);
    for (const auto& [name, bytes] : fsx)
        if (!write_host_file(b3mod::path_utf8(fs::path(out) / name), bytes, &why)) return fail(why);
    std::printf("txd export: %zu files into %s (edit the PNGs at their size; then txd import)\n", fsx.size(), out.c_str());
    return 0;
}

int cmd_txd_import(const std::string& orig, const std::string& dir, const std::string& out, const EncodeOptions& opt) {
    if (same_file(orig, out)) return fail("the output would replace the original: name another file");
    Buf b;
    if (!load(orig, b)) return fail("cannot read " + orig);
    FileSet in;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string name = b3mod::path_utf8(e.path().filename());
        if (to_upper_ascii(name).size() < 4 || to_upper_ascii(name).substr(name.size() - 4) != ".PNG") continue;
        Buf png;
        if (!load(b3mod::path_utf8(e.path()), png)) return fail("cannot read " + name);
        in[name] = std::move(png);
    }
    if (ec) return fail("cannot list " + dir);
    Buf res;
    ImportReport rep;
    std::string why;
    if (!txd_import(b, in, opt, res, &rep, &why)) return fail(why);
    for (const std::string& n : rep.notes) std::printf("  note: %s\n", n.c_str());
    if (!write_host_file(out, res, &why)) return fail(why);
    std::printf("txd import: %zu PNGs, %zu rasters changed%s%s -> %s (%zu bytes, the original's size)\n", in.size(), rep.changed,
                rep.new_palettes ? (", " + std::to_string(rep.new_palettes) + " with a new palette").c_str() : "",
                rep.nearest ? (", " + std::to_string(rep.nearest) + " texels took the nearest palette colour").c_str() : "", out.c_str(), res.size());
    return 0;
}

int cmd_strings_export(const std::string& p, const std::string& out) {
    Buf b;
    if (!load(p, b)) return fail("cannot read " + p);
    StringTable t;
    std::string why;
    if (!read_strings(b, t, &why)) return fail(p + ": " + why);
    const std::string text = strings_to_text(t, fs::path(p).filename().string());
    if (!write_host_file(out, Buf(text.begin(), text.end()), &why)) return fail(why);
    std::printf("strings export: %zu strings -> %s\n", t.s.size(), out.c_str());
    return 0;
}

int cmd_strings_import(const std::string& orig, const std::string& txt, const std::string& out) {
    if (same_file(orig, out)) return fail("the output would replace the original: name another file");
    Buf b, tb;
    if (!load(orig, b)) return fail("cannot read " + orig);
    if (!load(txt, tb)) return fail("cannot read " + txt);
    StringTable t;
    std::string why;
    if (!read_strings(b, t, &why)) return fail(orig + ": " + why);
    const std::size_t before = t.s.size();
    std::size_t changed = 0;
    if (!strings_from_text(std::string_view(reinterpret_cast<const char*>(tb.data()), tb.size()), t, &why, &changed)) return fail(txt + ": " + why);
    const Buf res = write_strings(t);
    if (!write_host_file(out, res, &why)) return fail(why);
    std::printf("strings import: %zu strings changed%s -> %s (%zu bytes)\n", changed,
                t.s.size() > before ? (", " + std::to_string(t.s.size() - before) + " added").c_str() : "", out.c_str(), res.size());
    return 0;
}

int cmd_diff(const std::string& a, const std::string& b, const std::string& out) {
    Buf x, y;
    if (!load(a, x)) return fail("cannot read " + a);
    if (!load(b, y)) return fail("cannot read " + b);
    PatchInfo pi;
    const Buf p = make_patch(x, y, &pi);
    std::string why;
    if (!write_host_file(out, p, &why)) return fail(why);
    std::printf("diff: %s -> %s: %zu ops, %llu bytes copied from the original, %llu new; patch %zu bytes\n", a.c_str(), b.c_str(), pi.ops,
                (unsigned long long)pi.copied, (unsigned long long)pi.inserted, p.size());
    return 0;
}

int cmd_apply(const std::string& a, const std::string& patch, const std::string& out) {
    if (same_file(a, out)) return fail("the output would replace the original: name another file");
    Buf x, p, y;
    if (!load(a, x)) return fail("cannot read " + a);
    if (!load(patch, p)) return fail("cannot read " + patch);
    std::string why;
    if (!apply_patch(x, p, y, &why)) return fail(patch + ": " + why);
    if (!write_host_file(out, y, &why)) return fail(why);
    std::printf("apply: %s (%zu bytes, the patch's target hash checked)\n", out.c_str(), y.size());
    return 0;
}

int print_verify(const VerifyReport& r, const std::string& what) {
    for (const std::string& e : r.errors) std::printf("  ERROR    %s\n", e.c_str());
    for (const std::string& w : r.warnings) std::printf("  warning  %s\n", w.c_str());
    for (const std::string& n : r.notes) std::printf("  note     %s\n", n.c_str());
    std::printf("verify: %s %s: %s %s, %zu files (%zu disc paths, %zu patches), %zu errors, %zu warnings\n", r.errors.empty() ? "OK" : "FAIL",
                what.c_str(), r.id.empty() ? "?" : r.id.c_str(), r.version.c_str(), r.files, r.overlay_files, r.patches, r.errors.size(),
                r.warnings.size());
    return r.errors.empty() ? 0 : 1;
}

void game_hint(const std::string& game) {
    if (game.empty()) std::printf("  (add --game <disc> to check that no file is a copy of the game's own)\n");
}

int cmd_verify(const std::string& mod, const std::string& game, const VerifyOptions& opt) {
    VerifyReport r;
    verify_mod(mod, game, r, opt);
    game_hint(game);
    return print_verify(r, mod);
}

int cmd_pack(const std::string& dir, const std::string& out, const std::string& game, const VerifyOptions& opt, bool force) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return fail(dir + ": not a folder");
    VerifyReport r;
    verify_mod(dir, game, r, opt);
    game_hint(game);
    print_verify(r, dir);
    std::string why;
    if (!pack_allowed(r, force, &why)) return fail(why);   // errors; a whole game file with a small change (ship a patch)
    std::size_t n = 0;
    if (!pack_mod(dir, out, &n, &why)) return fail(why);
    std::printf("pack: %s (%zu files)\n", out.c_str(), n);
    return 0;
}

// ---- vdb ----
bool load_names(const std::string& extra, VdbNames& n) {
    n = vdb_builtin_names();
    if (extra.empty()) return true;
    Buf t;
    std::string why;
    if (!load(extra, t)) { fail("cannot read " + extra); return false; }
    if (!vdb_names_add(n, std::string_view(reinterpret_cast<const char*>(t.data()), t.size()), &why)) { fail(extra + ": " + why); return false; }
    return true;
}

int cmd_vdb_export(const std::string& p, const std::string& out, const std::string& names_file) {
    VdbNames names;
    if (!load_names(names_file, names)) return 1;
    Buf b;
    if (!load(p, b)) return fail("cannot read " + p);
    Vdb v;
    std::string why;
    if (!read_vdb(b, v, &why)) return fail(p + ": " + why);
    VdbTextStats st;
    const std::string text = vdb_to_text(v, names, fs::path(p).filename().string(), &st);
    Buf check;
    Vdb back;
    if (!vdb_from_text(text, names, back, &why) || !write_vdb(back, check, &why) || check != b)
        std::printf("  note: this file does not come back byte for byte from the text (%s): its layout is not the disc's\n",
                    why.empty() ? "the rebuilt bytes differ" : why.c_str());
    if (!write_host_file(out, Buf(text.begin(), text.end()), &why)) return fail(why);
    std::printf("vdb export: %zu values (%zu named, %zu by key only), %zu config files (%zu named) -> %s\n", v.values.size(), st.named,
                st.unnamed, v.files.size(), st.files_named, out.c_str());
    return 0;
}

int cmd_vdb_import(const std::string& txt, const std::string& out, const std::string& base, const std::string& names_file) {
    if (!base.empty() && same_file(base, out)) return fail("the output would replace the original: name another file");
    VdbNames names;
    if (!load_names(names_file, names)) return 1;
    Buf tb;
    if (!load(txt, tb)) return fail("cannot read " + txt);
    Vdb v;
    std::string why;
    if (!base.empty()) {
        Buf bb;
        if (!load(base, bb)) return fail("cannot read " + base);
        if (!read_vdb(bb, v, &why)) return fail(base + ": " + why);
    }
    const std::size_t before = v.values.size();
    std::size_t changed = 0;
    if (!vdb_from_text(std::string_view(reinterpret_cast<const char*>(tb.data()), tb.size()), names, v, &why, &changed)) return fail(txt + ": " + why);
    Buf res;
    if (!write_vdb(v, res, &why)) return fail(why);
    if (!write_host_file(out, res, &why)) return fail(why);
    std::printf("vdb import: %zu values (%zu set by the text%s), %zu config files -> %s (%zu bytes of at most %zu)\n", v.values.size(), changed,
                base.empty() ? "" : (", " + std::to_string(v.values.size() - before) + " added to the base").c_str(), v.files.size(), out.c_str(),
                res.size(), kVdbMaxSize);
    return 0;
}

// ---- audio ----
bool load_audio(const std::string& p, Buf& b, AudioFile& af) {
    if (!load(p, b)) { fail("cannot read " + p); return false; }
    std::string why;
    if (!audio_read(b, af, &why)) { fail(p + ": " + why); return false; }
    return true;
}
double seconds(const AudioItem& it) { return it.rate ? double(it.bytes / std::size_t(it.channels) / 16 * 28) / it.rate : 0; }

std::string audio_index(const AudioFile& af, const std::string& src) {
    std::string o = "# b3modtool audio: " + src + ": " + (af.kind == AudioFile::Rws ? "RWS stream" : "wave bank") + " \"" + af.name + "\", " +
                    std::to_string(af.items.size()) + (af.kind == AudioFile::Rws ? " segment x sub-stream items" : " waves") + "\n";
    o += "# <file> <rate> Hz <channels> ch <PS-ADPCM bytes> <seconds> [loops] [flags]; import matches a WAV by its leading number" +
         std::string(af.kind == AudioFile::Rws && af.nsub > 1 ? " and .sub<k>" : "") + "\n";
    for (const AudioItem& it : af.items) {
        char t[200];
        std::snprintf(t, sizeof t, "%s.wav %d Hz %d ch %zu bytes %.3f s%s", audio_item_stem(af, it).c_str(), it.rate, it.channels, it.bytes, seconds(it),
                      it.loops ? " loops" : "");
        o += t;
        if (af.kind == AudioFile::Awd) { std::snprintf(t, sizeof t, " flags 0x%X", it.flags); o += t; }
        o += "\n";
    }
    return o;
}

int cmd_audio_list(const std::string& p) {
    Buf b;
    AudioFile af;
    if (!load_audio(p, b, af)) return 1;
    std::fputs(audio_index(af, fs::path(p).filename().string()).c_str(), stdout);
    return 0;
}

int cmd_audio_export(const std::string& p, const std::string& out) {
    Buf b;
    AudioFile af;
    if (!load_audio(p, b, af)) return 1;
    std::string why;
    for (const AudioItem& it : af.items)
        if (!write_host_file(b3mod::path_utf8(fs::path(out) / (audio_item_stem(af, it) + ".wav")), write_wav(audio_decode(b, af, it)), &why)) return fail(why);
    const std::string idx = audio_index(af, fs::path(p).filename().string());
    if (!write_host_file(b3mod::path_utf8(fs::path(out) / "audio.txt"), Buf(idx.begin(), idx.end()), &why)) return fail(why);
    std::printf("audio export: %zu WAV files and audio.txt into %s (the game's audio: for editing on this PC, never for sharing)\n", af.items.size(),
                out.c_str());
    return 0;
}

// "012_name.wav" -> 12, "012_name.sub1.wav" -> 12 / 1
bool wav_key(const std::string& name, std::size_t& index, std::size_t& sub) {
    const std::string up = to_upper_ascii(name);
    if (up.size() < 5 || up.substr(up.size() - 4) != ".WAV" || !std::isdigit(static_cast<unsigned char>(up[0]))) return false;
    index = std::strtoul(up.c_str(), nullptr, 10);
    sub = 0;
    const std::size_t s = up.rfind(".SUB");
    if (s != std::string::npos && s + 4 < up.size() - 4) sub = std::strtoul(up.c_str() + s + 4, nullptr, 10);
    return true;
}

int cmd_audio_import(const std::string& orig, const std::string& dir, const std::string& out) {
    if (same_file(orig, out)) return fail("the output would replace the original: name another file");
    Buf b;
    AudioFile af;
    if (!load_audio(orig, b, af)) return 1;
    std::map<std::pair<std::size_t, std::size_t>, std::string> wavs;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        std::size_t i = 0, s = 0;
        const std::string name = b3mod::path_utf8(e.path().filename());
        if (!wav_key(name, i, s)) continue;
        if (wavs.count({i, s})) return fail("two WAV files for item " + std::to_string(i) + ": " + wavs[{i, s}] + " and " + name);
        wavs[{i, s}] = b3mod::path_utf8(e.path());
    }
    if (ec) return fail("cannot list " + dir);
    std::string err;
    const AudioSource get = [&](const AudioItem& it, Pcm& p) {
        auto w = wavs.find({it.index, it.sub});
        if (w == wavs.end() || !err.empty()) return false;
        Buf wb;
        std::string why;
        if (!load(w->second, wb)) { err = "cannot read " + w->second; return false; }
        if (!read_wav(wb, p, &why)) { err = w->second + ": " + why; return false; }
        return true;
    };
    Buf res;
    AudioImportReport rep;
    std::string why;
    if (!audio_import(b, get, res, &rep, &why)) return fail(why);
    if (!err.empty()) return fail(err);
    for (const std::string& n : rep.notes) std::printf("  note: %s\n", n.c_str());
    if (!write_host_file(out, res, &why)) return fail(why);
    std::printf("audio import: %zu WAVs for %zu items: %zu unchanged (the original PS-ADPCM kept), %zu re-encoded (%zu with a new length); "
                "%zu frames kept, %zu encoded -> %s (%zu bytes%s)\n", wavs.size(), rep.items, rep.unchanged, rep.reencoded, rep.length_changed,
                rep.frames_reused, rep.frames_encoded, out.c_str(), res.size(), res.size() == b.size() ? ", the original's size" : "");
    return 0;
}

// ---- roundtrip: the census behind "importable" ----
struct Census {
    std::size_t files = 0, files_ok = 0, items = 0, items_ok = 0, strict_ok = 0;
    std::size_t strict_dup = 0, strict_alpha = 0, strict_other = 0, edit = 0, edit_ok = 0;
};

// why a raster does not re-encode from its pixels alone: CLUT entries of one colour (the index is ambiguous), or stored
// alpha above 0x80 (the PNG's 255 stands for 0x80 and above)
void strict_reason(const RasterView& v, Census& c) {
    bool dup = false, hi = v.dec.max_alpha > 0x80;
    if (v.indexed) {
        std::vector<std::uint32_t> seen;
        std::vector<bool> used(v.clut_rgba.size(), false);
        for (const auto& lv : v.raw)
            for (std::uint32_t i : lv) used[i % used.size()] = true;
        for (std::size_t i = 0; i < v.clut_rgba.size(); ++i) {
            if (!used[i]) continue;
            if (std::find(seen.begin(), seen.end(), v.clut_rgba[i]) != seen.end()) dup = true;
            seen.push_back(v.clut_rgba[i]);
        }
    }
    if (dup) ++c.strict_dup;
    else if (hi) ++c.strict_alpha;
    else ++c.strict_other;
}

// a real edit, the mips rebuilt; the written raster must decode to exactly that image. Level 0 with its RGB inverted (as
// many colours as before, so a new CLUT holds them exactly), or for a raster with several palettes (its CLUT is kept)
// mirrored left to right (only colours the CLUT has)
bool edit_check(const Buf& orig, std::size_t off, const RasterView& v, std::string& why) {
    std::vector<b3asset::Image> lv(1, v.dec.levels[0]);
    b3asset::Image& im = lv[0];
    if (v.dec.hdr.palettes > 1) {
        for (int y = 0; y < im.h; ++y)
            for (int x = 0; x < im.w; ++x)
                std::memcpy(&im.rgba[(std::size_t(y) * im.w + x) * 4], &v.dec.levels[0].rgba[(std::size_t(y) * im.w + (im.w - 1 - x)) * 4], 4);
    } else {
        for (std::size_t i = 0; i < im.rgba.size(); i += 4)
            for (int k = 0; k < 3; ++k) im.rgba[i + std::size_t(k)] = std::uint8_t(255 - im.rgba[i + std::size_t(k)]);
    }
    Buf work = orig;
    EncodeOptions opt;
    if (!encode_raster(work, off, lv, opt, nullptr, &why)) return false;
    if (work.size() != orig.size()) { why = "size changed"; return false; }
    RasterView after;
    if (!view_raster(work, off, after, &why)) return false;
    if (after.dec.levels[0].rgba != lv[0].rgba) { why = "the edited level does not read back exactly"; return false; }
    return true;
}

bool levels_from(const FileSet& fsx, const std::string& stem, std::size_t nlev, std::vector<b3asset::Image>& lv, std::string& why) {
    lv.assign(nlev, {});
    for (std::size_t l = 0; l < nlev; ++l) {
        auto it = fsx.find(stem + (l ? ".mip" + std::to_string(l) : std::string()) + ".png");
        if (it == fsx.end()) { why = "no PNG for level " + std::to_string(l); return false; }
        if (!decode_png(it->second, lv[l].w, lv[l].h, lv[l].rgba, &why)) return false;
    }
    return true;
}

void roundtrip_txd(const DiscFile& f, Census& c) {
    Buf orig;
    ++c.files;
    if (!load(f.path, orig)) { std::printf("  %s: cannot read\n", f.key.c_str()); return; }
    FileSet fsx;
    std::string why;
    std::vector<std::size_t> offs;
    if (!txd_export(orig, f.key, fsx, &why) || !txd_offsets(orig, offs, &why)) { std::printf("  %s: %s\n", f.key.c_str(), why.c_str()); return; }
    Buf work = orig;
    std::size_t ok = 0, strict_ok = 0, shown = 0;
    for (std::size_t i = 0; i < offs.size(); ++i) {
        ++c.items;
        RasterView v;
        if (!view_raster(orig, offs[i], v, &why)) { if (shown++ < 8) std::printf("    raster %zu: not decoded: %s\n", i, why.c_str()); continue; }
        std::vector<b3asset::Image> lv;
        const std::string stem = raster_file_stem(i, v.dec.hdr.name);
        if (!levels_from(fsx, stem, v.dec.levels.size(), lv, why)) { if (shown++ < 8) std::printf("    %s: %s\n", stem.c_str(), why.c_str()); continue; }
        for (int strict = 0; strict < 2; ++strict) {
            EncodeOptions opt;
            opt.force_write = true;
            opt.strict = strict;
            std::string e;
            const bool enc = encode_raster(work, offs[i], lv, opt, nullptr, &e);
            const bool same = enc && work == orig;
            if (same) (strict ? strict_ok : ok)++;
            else if (strict) strict_reason(v, c);
            else if (!strict && shown++ < 8) {
                std::size_t at = 0;
                while (enc && at < work.size() && work[at] == orig[at]) ++at;
                std::printf("    %s: %s\n", stem.c_str(), enc ? ("bytes differ from offset 0x" + b3mod::hex64(at).substr(8)).c_str() : e.c_str());
            }
            if (!same) work = orig;
        }
        ++c.edit;
        std::string e;
        if (edit_check(orig, offs[i], v, e)) ++c.edit_ok;
        else if (shown++ < 8) std::printf("    %s: edit check: %s\n", stem.c_str(), e.c_str());
    }
    // and the whole file through txd import, as the CLI does it
    EncodeOptions all;
    all.force_write = true;
    Buf res;
    const bool whole = txd_import(orig, fsx, all, res, nullptr, &why) && res == orig;
    if (whole && ok == offs.size()) ++c.files_ok;
    c.items_ok += ok;
    c.strict_ok += strict_ok;
    std::printf("  %-22s %4zu rasters: %4zu identical after export -> PNG -> import%s; file %s; from pixels alone (strict) %zu\n", f.key.c_str(),
                offs.size(), ok, ok == offs.size() ? "" : " (FAIL)", whole ? "identical" : "DIFFERS", strict_ok);
}

void roundtrip_strings(const DiscFile& f, Census& c) {
    Buf orig;
    ++c.files;
    if (!load(f.path, orig)) { std::printf("  %s: cannot read\n", f.key.c_str()); return; }
    StringTable t;
    std::string why;
    if (!read_strings(orig, t, &why)) { std::printf("  %-22s %s\n", f.key.c_str(), why.c_str()); return; }
    const std::string text = strings_to_text(t, f.key);
    StringTable fresh;   // every string from the text: nothing kept from the file but its header
    std::memcpy(fresh.magic, t.magic, 8);
    fresh.table_offset = t.table_offset;
    const bool parsed = strings_from_text(text, fresh, &why);
    std::size_t same_strings = 0;
    for (std::size_t i = 0; parsed && i < t.s.size() && i < fresh.s.size(); ++i) same_strings += t.s[i] == fresh.s[i];
    c.items += t.s.size();
    c.items_ok += same_strings;
    const bool same = parsed && write_strings(fresh) == orig;
    c.files_ok += same;
    std::printf("  %-22s %5zu strings: %5zu identical after export -> text -> import; file %s\n", f.key.c_str(), t.s.size(), same_strings,
                same ? "identical" : (parsed ? "DIFFERS" : ("not parsed: " + why).c_str()));
}

// Rasters inside other files (tracks, cars, DATA/*.BIN), found by a validated header scan as b3assetdump's census does:
// written back through GS memory as an import would (forced; and strict). Report only: these files have no import
// command yet (DESIGN.md §3, part 3c), so this is no acceptance check.
struct EmbeddedCensus { std::size_t files = 0, rasters = 0, ok = 0, strict_ok = 0, failed = 0; };
void roundtrip_embedded(const DiscFile& f, std::map<std::string, EmbeddedCensus>& by_kind) {
    const std::string base = to_upper_ascii(f.key.substr(f.key.find_last_of('/') == std::string::npos ? 0 : f.key.find_last_of('/') + 1));
    const std::string ext = base.size() > 4 ? base.substr(base.size() - 4) : "";
    const std::string kind = (base == "STATIC.DAT" || base == "ENVIRO.DAT") ? base
                             : (ext == ".BGV" || ext == ".BTV") ? std::string("car ") + ext.substr(1)
                             : (ext == ".BIN" && to_upper_ascii(f.key).rfind("DATA/", 0) == 0) ? std::string("DATA/*.BIN") : "";
    if (kind.empty()) return;
    Buf orig;
    if (!load(f.path, orig)) return;
    EmbeddedCensus& c = by_kind[kind];
    ++c.files;
    const b3asset::Bytes b{orig.data(), orig.size()};
    Buf work = orig;
    for (std::size_t o = 0; o + 0xC8 <= orig.size(); o += 16) {
        if (orig[o + 0x20] != 4) continue;
        b3asset::RasterHeader h;
        if (!b3asset::read_raster_header(b, o, h)) continue;
        RasterView v;
        if (!view_raster(orig, o, v)) continue;
        ++c.rasters;
        for (int strict = 0; strict < 2; ++strict) {
            EncodeOptions opt;
            opt.force_write = true;
            opt.strict = strict;
            EncodeResult er;
            std::string e;
            const bool enc = encode_raster(work, o, v.dec.levels, opt, &er, &e);
            if (enc && !er.changed) (strict ? c.strict_ok : c.ok)++;
            else {
                if (!strict) {
                    ++c.failed;
                    if (c.failed <= 3) std::printf("    %s raster @0x%zx %s: %s\n", f.key.c_str(), o, h.name.c_str(), enc ? "bytes differ" : e.c_str());
                }
                work = orig;
            }
        }
    }
}

// VDB.XML: binary -> text -> a new file from the text alone; then an edit (one named float + 1) and an added record
void roundtrip_vdb(const DiscFile& f, const VdbNames& names, Census& c) {
    Buf orig;
    ++c.files;
    if (!load(f.path, orig)) { std::printf("  %s: cannot read\n", f.key.c_str()); return; }
    Vdb v, fresh;
    std::string why;
    if (!read_vdb(orig, v, &why)) { std::printf("  %-22s %s\n", f.key.c_str(), why.c_str()); return; }
    VdbTextStats st;
    const std::string text = vdb_to_text(v, names, f.key, &st);
    Buf res;
    const bool parsed = vdb_from_text(text, names, fresh, &why) && write_vdb(fresh, res, &why);
    std::size_t same = 0;
    for (const auto& [k, x] : v.values) {
        auto it = fresh.values.find(k);
        same += it != fresh.values.end() && it->second.kind == x.kind && it->second.word == x.word && it->second.block == x.block;
    }
    c.items += v.values.size();
    c.items_ok += same;
    const bool ok = parsed && res == orig;
    c.files_ok += ok;
    std::printf("  %-22s %5zu values (%zu named, %zu by key), %zu config files (%zu named): %zu identical after export -> text -> import; "
                "file %s\n", f.key.c_str(), v.values.size(), st.named, st.unnamed, v.files.size(), st.files_named, same,
                ok ? "identical" : (parsed ? "DIFFERS" : ("not parsed: " + why).c_str()));
    // the edit check: the first named float word + 1.0 through a one-line text over the original, and a new record
    std::size_t at = text.find("\n[");
    std::string file, line;
    while (at != std::string::npos && line.empty()) {
        const std::size_t e = text.find(']', at);
        file = text.substr(at + 2, e - at - 2);
        std::size_t l = e + 2;
        while (l < text.size() && text[l] != '[' && text[l] != '\n') {
            const std::size_t le = text.find('\n', l);
            const std::string cand = text.substr(l, le - l);
            const std::size_t eq = cand.rfind(" = ");
            const std::string val = eq == std::string::npos ? std::string() : cand.substr(eq + 3);
            if (!val.empty() && val[0] != '(' && val[0] != '[' && val.rfind("0x", 0) != 0 && val.find('.') != std::string::npos) { line = cand; break; }
            l = le + 1;
        }
        at = text.find("\n[", e);
    }
    ++c.edit;
    if (line.empty()) { std::printf("    edit check: no named float found\n"); return; }
    const std::size_t eq = line.rfind(" = ");
    const float nv = std::strtof(line.c_str() + eq + 3, nullptr) + 1.0f;
    char num[48];
    std::snprintf(num, sizeof num, "%.9g", double(nv));
    std::string edit = "[" + file + "]\n" + line.substr(0, eq) + " = " + num + (std::strchr(num, '.') || std::strchr(num, 'e') ? "" : ".0") + "\n";
    std::int32_t newkey = 0x12345678;
    while (v.values.count(newkey)) ++newkey;
    char add[64];
    std::snprintf(add, sizeof add, "0x%08X = 0x3FC00000\n", std::uint32_t(newkey));
    Vdb ed = v;
    std::size_t changed = 0;
    Buf eb;
    Vdb back;
    if (!vdb_from_text(edit + add, names, ed, &why, &changed) || !write_vdb(ed, eb, &why) || !read_vdb(eb, back, &why)) {
        std::printf("    edit check: %s\n", why.c_str());
        return;
    }
    std::size_t diff = 0;
    for (const auto& [k, x] : v.values) {
        const VdbValue& y = back.values.at(k);
        diff += !(y.kind == x.kind && y.word == x.word && y.block == x.block);
    }
    const bool eok = changed == 2 && diff == 1 && back.values.size() == v.values.size() + 1 && back.values.count(newkey) &&
                     eb.size() <= orig.size() + 16 && back.files == v.files;
    c.edit_ok += eok;
    std::printf("    edit check (\"%s\" + 1.0, and one record added): %s, %zu bytes\n", line.substr(0, eq).c_str(),
                eok ? "only those changed" : "FAIL", eb.size());
}

// audio: every item through WAV and back (the original frames must be kept), the encoder's quality on the decoded
// audio (decode -> encode -> decode), and an edit check (item 0 made longer: the rest of the file must be unchanged)
struct AudioCensus {
    std::size_t files = 0, files_ok = 0, items = 0, items_ok = 0, dummy = 0, failed = 0, edit = 0, edit_ok = 0;
    double sig[2] = {}, noise[2] = {};
    std::vector<double> item_snr[2];
    std::size_t lossless = 0;
    std::uint64_t samples = 0;
};
// the encoder's quality on an item's decoded audio: [0] as decoded (decode -> encode -> decode), [1] at gain 0.9 (the
// samples then lie off the original frames' grid, as new audio does)
void snr_of(const Pcm& p, std::size_t max_frames, AudioCensus& c) {
    double s[2] = {}, n[2] = {};
    for (int ch = 0; ch < p.channels; ++ch) {
        std::vector<std::int16_t> x[2];
        for (std::size_t i = 0; i < p.frames() && i < max_frames; ++i) {
            const std::int16_t v = p.s[i * std::size_t(p.channels) + std::size_t(ch)];
            x[0].push_back(v);
            x[1].push_back(std::int16_t(std::lround(v * 0.9)));
        }
        if (x[0].empty()) continue;
        for (int m = 0; m < 2; ++m) {
            const Buf enc = adpcm_encode(x[m].data(), x[m].size(), nullptr, 0);
            const std::vector<std::int16_t> y = adpcm_decode(enc.data(), enc.size());
            for (std::size_t i = 0; i < x[m].size(); ++i) {
                s[m] += double(x[m][i]) * x[m][i];
                n[m] += double(x[m][i] - y[i]) * (x[m][i] - y[i]);
            }
        }
        c.samples += x[0].size();
    }
    for (int m = 0; m < 2; ++m) {
        c.sig[m] += s[m];
        c.noise[m] += n[m];
        if (s[m] > 0 && n[m] > 0) c.item_snr[m].push_back(10 * std::log10(s[m] / n[m]));
    }
    c.lossless += s[0] > 0 && n[0] == 0;
}
void roundtrip_audio(const DiscFile& f, AudioCensus& cs, AudioCensus& cb) {
    Buf orig;
    if (!load(f.path, orig)) { std::printf("  %s: cannot read\n", f.key.c_str()); return; }
    AudioFile af;
    std::string why;
    const bool bank = is_audio_bank(orig.data(), orig.size());
    AudioCensus& c = bank ? cb : cs;
    if (!audio_read(orig, af, &why)) {
        if (!bank && orig.size() >= 4 && orig[0] == 0x09 && orig[1] == 0x08) { ++c.dummy; return; }   // MUSIC.RWS: an empty wave dictionary
        ++c.files;
        ++c.failed;
        std::printf("  %s: %s\n", f.key.c_str(), why.c_str());
        return;
    }
    ++c.files;
    std::size_t kept = 0;
    const AudioSource get = [&](const AudioItem& it, Pcm& p) {
        const Pcm dec = audio_decode(orig, af, it);
        std::string e;
        if (!read_wav(write_wav(dec), p, &e) || p.s != dec.s) return false;
        snr_of(dec, bank ? SIZE_MAX : std::size_t(it.rate) * 10, c);
        return true;
    };
    Buf res;
    AudioImportReport rep;
    if (!audio_import(orig, get, res, &rep, &why)) { std::printf("  %s: import: %s\n", f.key.c_str(), why.c_str()); return; }
    kept = rep.unchanged;
    c.items += rep.items;
    c.items_ok += kept;
    const bool ok = res == orig && kept == rep.items;
    c.files_ok += ok;
    if (!ok) std::printf("  %s: %zu of %zu items kept, file %s\n", f.key.c_str(), kept, rep.items, res == orig ? "identical" : "DIFFERS");
    // the edit check: item 0 with a quarter second of silence added at its end
    if (af.items.empty()) return;
    ++c.edit;
    const AudioItem it0 = af.items[0];
    Pcm longer = audio_decode(orig, af, it0);
    longer.s.resize(longer.s.size() + std::size_t(it0.rate / 4) * std::size_t(it0.channels), 0);
    const AudioSource get1 = [&](const AudioItem& it, Pcm& p) { if (it.index || it.sub) return false; p = longer; return true; };
    Buf eb;
    AudioFile ea;
    if (!audio_import(orig, get1, eb, nullptr, &why) || !audio_read(eb, ea, &why) || ea.items.size() != af.items.size()) {
        std::printf("  %s: edit check: %s\n", f.key.c_str(), why.c_str());
        return;
    }
    bool eok = true;
    for (std::size_t i = 0; i < af.items.size() && eok; ++i) {
        const std::vector<Buf> a = audio_channels(orig, af, af.items[i]), b = audio_channels(eb, ea, ea.items[i]);
        if (i == 0) {   // what the import meant to write: the encoder's frames for the longer audio
            const Pcm& p = longer;
            for (int ch = 0; ch < p.channels && eok; ++ch) {
                std::vector<std::int16_t> x(p.frames());
                for (std::size_t k = 0; k < x.size(); ++k) x[k] = p.s[k * std::size_t(p.channels) + std::size_t(ch)];
                eok = adpcm_encode(x.data(), x.size(), a[std::size_t(ch)].data(), a[std::size_t(ch)].size()) == b[std::size_t(ch)] &&
                      b[std::size_t(ch)].size() > a[std::size_t(ch)].size();
            }
        } else {
            eok = a == b && ea.items[i].rate == af.items[i].rate && ea.items[i].flags == af.items[i].flags && ea.items[i].loops == af.items[i].loops;
        }
    }
    c.edit_ok += eok;
    if (!eok) std::printf("  %s: edit check FAIL\n", f.key.c_str());
}

int cmd_roundtrip(const std::string& d, const std::string& kind) {
    if (!disc::is_dir(d)) return fail(d + ": not a disc folder or image");
    if (kind == "rasters") {
        std::map<std::string, EmbeddedCensus> by_kind;
        for (const DiscFile& f : disc_files(d)) roundtrip_embedded(f, by_kind);
        EmbeddedCensus all;
        for (const auto& [k, c] : by_kind) {
            std::printf("  %-12s %4zu files: %6zu rasters, %6zu identical written back, %6zu from pixels alone (strict)\n", k.c_str(), c.files,
                        c.rasters, c.ok, c.strict_ok);
            all.rasters += c.rasters;
            all.ok += c.ok;
            all.strict_ok += c.strict_ok;
        }
        std::printf("roundtrip rasters (report only: no import for these files yet): %zu of %zu identical (strict %zu)\n", all.ok, all.rasters,
                    all.strict_ok);
        return all.ok == all.rasters ? 0 : 1;
    }
    const bool do_txd = kind == "all" || kind == "txd", do_str = kind == "all" || kind == "strings";
    const bool do_vdb = kind == "all" || kind == "vdb", do_audio = kind == "all" || kind == "audio";
    if (!do_txd && !do_str && !do_vdb && !do_audio) return fail("kind is txd, strings, vdb, audio, all or rasters");
    Census ct, cs, cv;
    AudioCensus as, ab;
    VdbNames names;
    if (do_vdb) names = vdb_builtin_names();
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<DiscFile> files = disc_files(d);
    for (const DiscFile& f : files) {
        std::uint8_t head[16] = {};
        disc::File df;
        std::size_t got = 0;
        if (df.open(f.path)) { const std::int64_t r = df.read_at(0, head, sizeof head); got = r > 0 ? std::size_t(r) : 0; }
        const std::string k = file_kind(f.key, head, got);
        if (k == "txd" && do_txd) roundtrip_txd(f, ct);
        if (k == "strings" && do_str) roundtrip_strings(f, cs);
        if (k == "vdb tuning" && do_vdb) roundtrip_vdb(f, names, cv);
        if ((k == "rws audio stream" || k == "awd sound bank" || k == "car sound") && do_audio) roundtrip_audio(f, as, ab);
    }
    bool pass = true;
    if (do_txd) {
        std::printf("roundtrip txd: %zu of %zu files identical, %zu of %zu rasters (strict, from pixels alone: %zu)\n", ct.files_ok, ct.files,
                    ct.items_ok, ct.items, ct.strict_ok);
        std::printf("  strict: of the %zu rasters that differ, %zu have CLUT entries of one colour, %zu stored alpha above 0x80, %zu other\n",
                    ct.items - ct.strict_ok, ct.strict_dup, ct.strict_alpha, ct.strict_other);
        std::printf("  edit check (level 0 RGB inverted, or mirrored with several palettes; mips rebuilt): %zu of %zu rasters read back exactly\n", ct.edit_ok, ct.edit);
        pass = pass && ct.files && ct.files_ok == ct.files && ct.edit_ok == ct.edit;
    }
    if (do_str) {
        std::printf("roundtrip strings: %zu of %zu files identical, %zu of %zu strings\n", cs.files_ok, cs.files, cs.items_ok, cs.items);
        pass = pass && cs.files && cs.files_ok == cs.files;
    }
    if (do_vdb) {
        std::printf("roundtrip vdb: %zu of %zu files identical, %zu of %zu values; edit check %zu of %zu\n", cv.files_ok, cv.files, cv.items_ok,
                    cv.items, cv.edit_ok, cv.edit);
        pass = pass && cv.files && cv.files_ok == cv.files && cv.edit_ok == cv.edit;
    }
    if (do_audio) {
        auto median = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        auto lo = [](const std::vector<double>& v) { return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end()); };
        for (int k = 0; k < 2; ++k) {
            const AudioCensus& a = k ? ab : as;
            std::printf("roundtrip audio %s: %zu of %zu files identical, %zu of %zu %s kept byte for byte through WAV%s; edit check (item 0 "
                        "longer) %zu of %zu\n", k ? "banks (.AWD .HWD .LWD)" : "streams (.RWS)", a.files_ok, a.files, a.items_ok, a.items,
                        k ? "waves" : "segment x sub-stream items",
                        a.dummy ? (" (" + std::to_string(a.dummy) + " MUSIC.RWS dummies, not streams, skipped)").c_str() : "", a.edit_ok, a.edit);
            auto db = [](double sg, double nz) { char t[32]; std::snprintf(t, sizeof t, nz > 0 ? "%.1f dB SNR" : "no error at all", 10 * std::log10(sg / (nz > 0 ? nz : 1))); return std::string(t); };
            auto rest = [&](const std::vector<double>& v) {
                if (v.empty()) return std::string("none differ");
                char t[96];
                std::snprintf(t, sizeof t, "of the others median %.1f dB, lowest %.1f dB", median(v), lo(v));
                return std::string(t);
            };
            std::printf("  encoder quality over %.1f M samples (%s):\n", double(a.samples) / 1e6, k ? "every wave whole" : "the first 10 s of each item");
            std::printf("    decode -> encode -> decode: %s; %zu items come back exactly; %s\n", db(a.sig[0], a.noise[0]).c_str(), a.lossless,
                        rest(a.item_snr[0]).c_str());
            std::printf("    the same at gain 0.9 (off the original frames' grid, like new audio): %s; per item median %.1f dB, lowest %.1f dB\n",
                        db(a.sig[1], a.noise[1]).c_str(), median(a.item_snr[1]), lo(a.item_snr[1]));
            pass = pass && a.files && a.files_ok == a.files && a.edit_ok == a.edit && !a.failed;
        }
    }
    std::printf("  (%.0f s)\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    std::printf("roundtrip: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// ---- car / track export (part 3c) ----

// Writes a FileSet into `dir`; the .gltf (if any) checked from memory first
bool write_set(const FileSet& fsx, const std::string& dir, std::string& check, std::string* why) {
    check.clear();
    for (const auto& [name, bytes] : fsx) {
        if (name.size() > 5 && name.substr(name.size() - 5) == ".gltf") {
            Json doc;
            std::string err;
            const std::string stem = name.substr(0, name.size() - 5);
            auto bin = fsx.find(stem + ".bin");
            std::vector<std::string> errors;
            GltfCheckStats st;
            if (!json_parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), doc, &err)) check = "JSON: " + err;
            else if (bin == fsx.end()) check = "no .bin";
            else if (!gltf_check(doc, bin->second, errors, &st)) check = errors.front() + (errors.size() > 1 ? " (+" + std::to_string(errors.size() - 1) + " more)" : "");
            else check = "ok";
        }
        if (!write_host_file(b3mod::path_utf8(fs::path(dir) / name), bytes, why)) return false;
    }
    return true;
}

std::string bounds_text(const ExportReport& r) {
    char t[128];
    std::snprintf(t, sizeof t, "(%.3f %.3f %.3f) .. (%.3f %.3f %.3f)", r.bmin[0], r.bmin[1], r.bmin[2], r.bmax[0], r.bmax[1], r.bmax[2]);
    return t;
}

int cmd_car_list(const std::string& d) {
    if (!disc::is_dir(d)) return fail(d + ": not a disc folder or image");
    std::string note;
    const auto cars = car_list(d, true, &note);
    std::size_t player = 0, traffic = 0;
    for (const CarListEntry& c : cars) {
        if (c.index >= 0) { std::printf("%3d  %-20s %-12s %s\n", c.index, c.key.c_str(), c.kind.c_str(), c.name.c_str()); ++player; }
        else ++traffic;
    }
    if (traffic) {
        std::printf("traffic and other vehicles (export them by path):\n");
        for (const CarListEntry& c : cars)
            if (c.index < 0) std::printf("     %s\n", c.key.c_str());
    }
    if (!note.empty()) std::printf("note: %s\n", note.c_str());
    std::printf("%zu player cars, %zu traffic vehicles\n", player, traffic);
    return player ? 0 : 1;
}

int cmd_car_export(const std::string& d, const std::string& car, const std::string& out, int lod, bool obj, int paint) {
    if (!disc::is_dir(d)) return fail(d + ": not a disc folder or image");
    std::string note;
    const auto cars = car_list(d, true, &note);
    std::vector<CarListEntry> pick;
    if (to_upper_ascii(car) == "ALL") {
        for (const CarListEntry& c : cars) if (c.index >= 0) pick.push_back(c);
    } else {
        std::string k = to_upper_ascii(car);
        for (char& ch : k) if (ch == '\\') ch = '/';
        if (k.rfind("PVEH/", 0) != 0 && k.find('/') != std::string::npos) k = "PVEH/" + k;
        if (k.find('.') == std::string::npos) k += ".BGV";
        for (const CarListEntry& c : cars)
            if (c.key == k || (!c.name.empty() && to_upper_ascii(c.name) == to_upper_ascii(car)) || (c.index >= 0 && std::to_string(c.index) == car)) { pick.push_back(c); break; }
        if (pick.empty()) return fail("no car \"" + car + "\" (a name from `car list`, its number, or a path such as PVEH/COMP/CAR1.BGV)");
    }
    std::size_t ok = 0, valid = 0;
    for (const CarListEntry& c : pick) {
        Buf b;
        if (!load(c.path, b)) return fail("cannot read " + c.key);
        std::string stem = c.key.substr(5);
        stem = stem.substr(0, stem.find('.'));
        for (char& ch : stem) if (ch == '/') ch = '_';
        CarExportOptions o;
        o.lod = lod;
        o.obj = obj;
        o.paint = paint;
        o.name = c.name;
        o.source = c.key;
        FileSet fsx;
        ExportReport r;
        std::string why, check;
        if (!car_export(b, stem, o, fsx, &r, &why)) { std::printf("%-20s FAILED: %s\n", c.key.c_str(), why.c_str()); continue; }
        const std::string dir = pick.size() > 1 ? b3mod::path_utf8(fs::path(out) / stem) : out;
        if (!write_set(fsx, dir, check, &why)) return fail(why);
        ++ok;
        valid += obj || check == "ok";
        std::printf("%-20s %-22s %zu files, %zu meshes, %zu nodes, %llu vertices, %llu triangles, %zu paints; bounds %s%s\n", c.key.c_str(),
                    c.name.empty() ? "-" : c.name.c_str(), r.files, r.meshes, r.nodes, (unsigned long long)r.vertices,
                    (unsigned long long)r.triangles, r.textures, bounds_text(r).c_str(), obj ? "" : ("; glTF check " + check).c_str());
        if (pick.size() == 1)
            for (const std::string& nt : r.notes) std::printf("  note: %s\n", nt.c_str());
    }
    std::printf("car export: %zu of %zu cars written into %s%s (the game's data: for your own editing, never for sharing)\n", ok, pick.size(),
                out.c_str(), obj ? "" : (", " + std::to_string(valid) + " glTF checked valid").c_str());
    return ok == pick.size() && (obj || valid == ok) ? 0 : 1;
}

// car import <disc> <model.gltf | model.glb | export folder> <mod folder | out.BGV> [--sidecar <file>] (part 3c);
// car check: the same with `out` empty, nothing written (the Blender add-on's Check button, tools/blender/b3_car_tools)
int cmd_car_import(const std::string& d, const std::string& model_arg, const std::string& out, std::string sidecar) {
    if (!disc::is_dir(d)) return fail(d + ": not a disc folder or image");
    std::error_code ec;
    auto ends_with = [](std::string s, const char* e) {
        s = to_upper_ascii(s);
        const std::string t = to_upper_ascii(e);
        return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
    };
    // the sidecar: given, or the one *.b3car.json beside the model (in the export folder)
    std::string model = model_arg;
    const fs::path mp = b3mod::path_from_utf8(model_arg);
    const fs::path folder = fs::is_directory(mp, ec) ? mp : mp.parent_path();
    if (sidecar.empty()) {
        std::vector<fs::path> found;
        for (const auto& e : fs::directory_iterator(folder.empty() ? fs::path(".") : folder, ec))
            if (ends_with(b3mod::path_utf8(e.path().filename()), ".b3car.json")) found.push_back(e.path());
        if (found.size() != 1)
            return fail(found.empty() ? "no .b3car.json beside " + model_arg + " (the export's sidecar: keep it in the export folder, or name it with --sidecar)"
                                      : "several .b3car.json in " + b3mod::path_utf8(folder) + ": name the one to use with --sidecar");
        sidecar = b3mod::path_utf8(found[0]);
    }
    if (fs::is_directory(mp, ec)) {   // an export folder: its <stem>.gltf (or .glb)
        std::string stem = b3mod::path_utf8(b3mod::path_from_utf8(sidecar).filename());
        stem = stem.substr(0, stem.size() - std::string(".b3car.json").size());
        model = b3mod::path_utf8(mp / (stem + ".gltf"));
        if (!fs::exists(b3mod::path_from_utf8(model), ec)) model = b3mod::path_utf8(mp / (stem + ".glb"));
    }
    Buf sj;
    Json side;
    if (!load(sidecar, sj) || !json_parse(std::string_view(reinterpret_cast<const char*>(sj.data()), sj.size()), side))
        return fail("cannot read the sidecar " + sidecar);
    const Json* src = side.get("source");
    const std::string key = src && src->get("path") ? to_upper_ascii(src->get("path")->s) : "";
    if (key.rfind("PVEH/", 0) != 0) return fail(sidecar + ": the sidecar names no car file of the disc");
    // the disc's own file (its own spelling of the path: found case-insensitively, without listing the whole disc)
    DiscFile file_on_disc;
    {
        fs::path at = b3mod::path_from_utf8(d);
        std::string spelled;
        for (std::size_t a = 0; a <= key.size();) {
            const std::size_t b = std::min(key.find('/', a), key.size());
            at = disc::resolve_ci(at, key.substr(a, b - a));
            if (at.empty()) break;
            spelled += (spelled.empty() ? "" : "/") + b3mod::path_utf8(at.filename());
            a = b + 1;
        }
        if (at.empty() || !disc::is_file(at)) return fail(d + " has no " + key);
        file_on_disc.key = spelled;
        file_on_disc.path = b3mod::path_utf8(at);
    }
    const DiscFile* df = &file_on_disc;
    Buf orig, outb;
    if (!load(df->path, orig)) return fail("cannot read " + key);
    CarImportReport r;
    std::string why;
    const FileLoader loader = [](const std::string& p, Buf& b) { return read_host_file(p, b); };
    if (!car_import(orig, loader, model, sidecar, {}, outb, &r, &why)) return fail("car import: " + why + " (nothing written)");
    for (const std::string& n : r.notes) std::printf("  note: %s\n", n.c_str());
    if (out.empty()) {
        std::printf("car check: %s: %zu meshes (%zu unchanged, %zu edited in place, %zu with new triangles, %zu not in the model: kept), "
                    "%zu packets moved, %zu paints changed%s; %zu -> %zu bytes; ok, nothing written\n",
                    key.c_str(), r.meshes, r.unchanged, r.edited, r.restripped, r.missing, r.moved, r.paints_changed,
                    r.new_palette_indices ? " (re-quantised together)" : "", r.size_before, r.size_after);
        return 0;
    }
    std::string where;
    if (ends_with(out, ".BGV") || ends_with(out, ".BTV")) {
        if (same_file(out, df->path)) return fail("the output would replace the disc's own file: name another file");
        if (!write_host_file(out, outb, &why)) return fail(why);
        where = out;
    } else {   // a mod folder: patches/<disc path>.b3patch, and a mod.ini when it has none
        const fs::path mod = b3mod::path_from_utf8(out);
        if (fs::exists(mod / "data" / b3mod::path_from_utf8(df->key), ec))
            return fail(b3mod::path_utf8(mod / "data" / b3mod::path_from_utf8(df->key)) + " is there too: a mod has a file or its patch, not both");
        const std::string pp = b3mod::path_utf8(mod / "patches" / b3mod::path_from_utf8(df->key + ".b3patch"));
        PatchInfo pi;
        if (!write_host_file(pp, make_patch(orig, outb, &pi), &why)) return fail(why);
        where = pp;
        if (!fs::exists(mod / "mod.ini", ec)) {
            std::string id;
            for (char ch : b3mod::path_utf8(mod.filename().empty() ? mod.parent_path().filename() : mod.filename()))
                id += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '_' || ch == '-') ? char(std::tolower(static_cast<unsigned char>(ch))) : '-';
            while (!id.empty() && id[0] == '.') id.erase(0, 1);
            if (!b3mod::valid_id(id)) id = "edited-car";
            const std::string name = side.get("name") && !side.get("name")->s.empty() ? side.get("name")->s : key;
            const std::string ini = "[mod]\nid = " + id + "\nname = " + name + " (edited)\nversion = 1.0\nauthor = you\ndescription = An edited " + name +
                                    " (b3modtool car import).\napi = 1\n";
            if (!write_host_file(b3mod::path_utf8(mod / "mod.ini"), Buf(ini.begin(), ini.end()), &why)) return fail(why);
            std::printf("  note: %s written (id %s): change the name, author and description before you share the mod\n",
                        b3mod::path_utf8(mod / "mod.ini").c_str(), id.c_str());
        }
    }
    std::printf("car import: %s: %zu meshes (%zu unchanged, %zu edited in place, %zu with new triangles, %zu not in the model: kept), "
                "%zu packets moved, %zu paints changed%s; %zu -> %zu bytes -> %s\n",
                key.c_str(), r.meshes, r.unchanged, r.edited, r.restripped, r.missing, r.moved, r.paints_changed,
                r.new_palette_indices ? " (re-quantised together)" : "", r.size_before, r.size_after, where.c_str());
    return 0;
}

int cmd_track_list(const std::string& d) {
    if (!disc::is_dir(d)) return fail(d + ": not a disc folder or image");
    const auto routes = route_list(d);
    for (const RouteEntry& r : routes) {
        std::uint8_t h[0x60] = {};
        disc::File f;
        int units = 0;
        if (f.open(r.static_path) && f.read_at(0, h, sizeof h) == std::int64_t(sizeof h)) units = std::int16_t(h[0x54] | h[0x55] << 8);
        const bool streamed = !r.streamed_path.empty() && disc::file_size(r.streamed_path) > 0;
        std::printf("%-18s %4d sections%s%s\n", r.key.c_str(), streamed ? units : 0, streamed ? "" : r.streamed_path.empty() ? " (no STREAMED.DAT)" : " (STREAMED.DAT is empty)",
                    r.enviro_path.empty() ? " (no ENVIRO.DAT)" : "");
    }
    std::printf("%zu routes\n", routes.size());
    return routes.empty() ? 1 : 0;
}

int cmd_track_export(const std::string& d, const std::string& route, const std::string& out, int section, bool obj) {
    if (!disc::is_dir(d)) return fail(d + ": not a disc folder or image");
    const auto routes = route_list(d);
    std::vector<RouteEntry> pick;
    std::string k = to_upper_ascii(route);
    for (char& ch : k) if (ch == '\\') ch = '/';
    if (k.rfind("TRACKS/", 0) != 0) k = "TRACKS/" + k;
    while (!k.empty() && k.back() == '/') k.pop_back();
    for (const RouteEntry& r : routes)
        if (to_upper_ascii(route) == "ALL" || r.key == k) pick.push_back(r);
    if (pick.empty()) return fail("no route \"" + route + "\" (see `track list`, e.g. US/C1_V1)");
    if (pick.size() > 1 && section >= 0) return fail("--section needs one route");
    std::size_t ok = 0, valid = 0;
    for (const RouteEntry& rt : pick) {
        Buf st, sm, en;
        if (!load(rt.static_path, st)) return fail("cannot read " + rt.key + "/STATIC.DAT");
        if (!rt.streamed_path.empty() && !load(rt.streamed_path, sm)) return fail("cannot read " + rt.key + "/STREAMED.DAT");
        if (!rt.enviro_path.empty() && !load(rt.enviro_path, en)) return fail("cannot read " + rt.key + "/ENVIRO.DAT");
        std::string stem = rt.key.substr(7);
        for (char& ch : stem) if (ch == '/') ch = '_';
        if (section >= 0) { char t[16]; std::snprintf(t, sizeof t, "_s%03d", section); stem += t; }
        TrackExportOptions o;
        o.section = section;
        o.obj = obj;
        o.source = rt.key;
        FileSet fsx;
        ExportReport r;
        std::string why, check;
        const auto t0 = std::chrono::steady_clock::now();
        if (!track_export(st, sm, en, stem, o, fsx, &r, &why)) { std::printf("%-18s FAILED: %s\n", rt.key.c_str(), why.c_str()); continue; }
        const std::string dir = pick.size() > 1 ? b3mod::path_utf8(fs::path(out) / stem) : out;
        if (!write_set(fsx, dir, check, &why)) return fail(why);
        ++ok;
        valid += obj || check == "ok";
        std::printf("%-18s %zu files, %zu meshes, %zu nodes, %zu primitives, %llu vertices, %llu triangles, %zu textures; bounds %s; %.1f s%s\n",
                    rt.key.c_str(), r.files, r.meshes, r.nodes, r.primitives, (unsigned long long)r.vertices, (unsigned long long)r.triangles,
                    r.textures, bounds_text(r).c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                    obj ? "" : ("; glTF check " + check).c_str());
    }
    std::printf("track export: %zu of %zu routes written into %s%s; each folder's *_report.txt says what is not exported (the game's "
                "data: for your own editing, never for sharing)\n", ok, pick.size(), out.c_str(), obj ? "" : (", " + std::to_string(valid) + " glTF checked valid").c_str());
    return ok == pick.size() && (obj || valid == ok) ? 0 : 1;
}


// track import <disc> <model.gltf | model.glb | export folder> <mod folder | out.b3mod> [--sidecar <file>] (part 3c);
// track check: the same with `out` empty, nothing written
int cmd_track_import(const std::string& d, const std::string& model_arg, const std::string& out, std::string sidecar, bool restrip) {
    if (!disc::is_dir(d)) return fail(d + ": not a disc folder or image");
    std::error_code ec;
    auto ends_with = [](std::string s, const char* e) {
        s = to_upper_ascii(s);
        const std::string t = to_upper_ascii(e);
        return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
    };
    // the sidecar: given, or the one *.b3track.json beside the model (in the export folder)
    std::string model = model_arg;
    const fs::path mp = b3mod::path_from_utf8(model_arg);
    const fs::path folder = fs::is_directory(mp, ec) ? mp : mp.parent_path();
    if (sidecar.empty()) {
        std::vector<fs::path> found;
        for (const auto& e : fs::directory_iterator(folder.empty() ? fs::path(".") : folder, ec))
            if (ends_with(b3mod::path_utf8(e.path().filename()), ".b3track.json")) found.push_back(e.path());
        if (found.size() != 1)
            return fail(found.empty() ? "no .b3track.json beside " + model_arg + " (the export's sidecar: keep it in the export folder, or name it with --sidecar)"
                                      : "several .b3track.json in " + b3mod::path_utf8(folder) + ": name the one to use with --sidecar");
        sidecar = b3mod::path_utf8(found[0]);
    }
    if (fs::is_directory(mp, ec)) {   // an export folder: its <stem>.gltf (or .glb)
        std::string stem = b3mod::path_utf8(b3mod::path_from_utf8(sidecar).filename());
        stem = stem.substr(0, stem.size() - std::string(".b3track.json").size());
        model = b3mod::path_utf8(mp / (stem + ".gltf"));
        if (!fs::exists(b3mod::path_from_utf8(model), ec)) model = b3mod::path_utf8(mp / (stem + ".glb"));
    }
    Buf sj;
    Json side;
    if (!load(sidecar, sj) || !json_parse(std::string_view(reinterpret_cast<const char*>(sj.data()), sj.size()), side))
        return fail("cannot read the sidecar " + sidecar);
    const Json* src = side.get("source");
    const std::string key = src && src->get("path") ? to_upper_ascii(src->get("path")->s) : "";
    if (key.rfind("TRACKS/", 0) != 0) return fail(sidecar + ": the sidecar names no route of the disc");
    const auto routes = route_list(d);
    const RouteEntry* rt = nullptr;
    for (const RouteEntry& r : routes)
        if (r.key == key) rt = &r;
    if (!rt) return fail(d + " has no " + key);
    Buf st, sm;
    if (!load(rt->static_path, st)) return fail("cannot read " + key + "/STATIC.DAT");
    if (!rt->streamed_path.empty() && !load(rt->streamed_path, sm)) return fail("cannot read " + key + "/STREAMED.DAT");
    TrackImportReport r;
    std::string why;
    Buf st2, sm2;
    const FileLoader loader = [](const std::string& p, Buf& b) { return read_host_file(p, b); };
    TrackImportOptions io;
    io.restrip = restrip;
    if (!track_import(st, sm, loader, model, sidecar, io, st2, sm2, &r, &why)) return fail("track import: " + why + " (nothing written)");
    for (const std::string& n : r.notes) std::printf("  note: %s\n", n.c_str());
    char summary[700];
    std::snprintf(summary, sizeof summary,
                  "%s: %zu meshes in the model (%zu unchanged, %zu edited in place, %zu with new triangles; %zu not in the model: kept), %zu packets moved, "
                  "%zu blocks grown, %zu boxes grown, %zu materials and %zu textures swapped, %zu texture pictures changed; STATIC.DAT %zu -> %zu, STREAMED.DAT %zu -> %zu bytes",
                  key.c_str(), r.meshes, r.unchanged, r.edited, r.restripped, r.missing, r.moved, r.blocks_grown, r.boxes_grown, r.materials_changed, r.textures_swapped,
                  r.textures_changed, r.static_before, r.static_after, r.streamed_before, r.streamed_after);
    if (out.empty()) {
        std::printf("track check: %s; ok, nothing written\n", summary);
        return 0;
    }
    if (st2 == st && sm2 == sm) {
        std::printf("track import: %s; nothing changed: no patch written\n", summary);
        return 0;
    }
    // the disc's own spelling of the files' paths (the patches are named after them)
    std::vector<DiscFile> files = disc_files(d);
    auto spelled = [&](const std::string& file_path) {
        const std::string k = key + "/" + to_upper_ascii(b3mod::path_utf8(b3mod::path_from_utf8(file_path).filename()));
        for (const DiscFile& f : files)
            if (to_upper_ascii(f.key) == k) return f.key;
        return k;
    };
    const bool packed = ends_with(out, ".b3mod");
    fs::path mod = b3mod::path_from_utf8(out);
    if (packed) mod.replace_extension();   // the mod folder beside it, then packed
    std::vector<std::string> where;
    for (int f = 0; f < 2; ++f) {
        const Buf& a = f ? sm : st;
        const Buf& b = f ? sm2 : st2;
        if (a == b) continue;
        const std::string dk = spelled(f ? rt->streamed_path : rt->static_path);
        if (fs::exists(mod / "data" / b3mod::path_from_utf8(dk), ec))
            return fail(b3mod::path_utf8(mod / "data" / b3mod::path_from_utf8(dk)) + " is there too: a mod has a file or its patch, not both");
        const std::string pp = b3mod::path_utf8(mod / "patches" / b3mod::path_from_utf8(dk + ".b3patch"));
        PatchInfo pi;
        if (!write_host_file(pp, make_patch(a, b, &pi), &why)) return fail(why);
        where.push_back(pp);
    }
    if (!fs::exists(mod / "mod.ini", ec)) {
        std::string id;
        for (char ch : b3mod::path_utf8(mod.filename().empty() ? mod.parent_path().filename() : mod.filename()))
            id += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '_' || ch == '-') ? char(std::tolower(static_cast<unsigned char>(ch))) : '-';
        while (!id.empty() && id[0] == '.') id.erase(0, 1);
        if (!b3mod::valid_id(id)) id = "edited-track";
        const std::string ini = "[mod]\nid = " + id + "\nname = " + key.substr(7) + " (edited)\nversion = 1.0\nauthor = you\ndescription = An edited " + key.substr(7) +
                                " (b3modtool track import).\napi = 1\n";
        if (!write_host_file(b3mod::path_utf8(mod / "mod.ini"), Buf(ini.begin(), ini.end()), &why)) return fail(why);
        std::printf("  note: %s written (id %s): change the name, author and description before you share the mod\n",
                    b3mod::path_utf8(mod / "mod.ini").c_str(), id.c_str());
    }
    std::string list;
    for (const std::string& w : where) list += (list.empty() ? "" : ", ") + w;
    if (packed) {
        VerifyReport vr;
        verify_mod(b3mod::path_utf8(mod), d, vr, VerifyOptions{});
        if (!pack_allowed(vr, false, &why)) return fail(why);
        std::size_t n = 0;
        if (!pack_mod(b3mod::path_utf8(mod), out, &n, &why)) return fail(why);
        list += "; packed into " + out + " (" + std::to_string(n) + " files)";
    }
    std::printf("track import: %s -> %s\n", summary, list.c_str());
    return 0;
}


int cmd_preview(const std::string& gltf, const std::string& png, float yaw, float pitch, const std::string& skip) {
    std::vector<std::string> sk;
    for (std::size_t a = 0; a <= skip.size();) {
        const std::size_t b = std::min(skip.find(',', a), skip.size());
        if (b > a) sk.push_back(skip.substr(a, b - a));
        a = b + 1;
    }
    b3asset::Model m;
    std::string why;
    if (!gltf_load_model(gltf, m, sk, &why)) return fail(gltf + ": " + why);
    b3asset::PreviewView v;
    v.yaw_deg = yaw;
    v.pitch_deg = pitch;
    const b3asset::Image im = b3asset::render_preview(m, 960, 600, v);
    if (!write_host_file(png, encode_png(im.w, im.h, im.rgba.data()), &why)) return fail(why);
    std::printf("preview: %zu vertices, %zu triangles, %zu draws, bounds (%.3f %.3f %.3f) .. (%.3f %.3f %.3f) -> %s\n", m.vertices.size(),
                m.indices.size() / 3, m.draws.size(), m.bmin[0], m.bmin[1], m.bmin[2], m.bmax[0], m.bmax[1], m.bmax[2], png.c_str());
    return 0;
}


}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> a, flags;
    std::string game, names, base, sidecar, lod_s = "all", format = "gltf", section_s = "all", paint_s = "0", yaw_s = "35", pitch_s = "20", skip_s;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--game" && i + 1 < argc) game = argv[++i];
        else if (s == "--lod" && i + 1 < argc) lod_s = argv[++i];
        else if (s == "--format" && i + 1 < argc) format = argv[++i];
        else if (s == "--section" && i + 1 < argc) section_s = argv[++i];
        else if (s == "--paint" && i + 1 < argc) paint_s = argv[++i];
        else if (s == "--yaw" && i + 1 < argc) yaw_s = argv[++i];
        else if (s == "--pitch" && i + 1 < argc) pitch_s = argv[++i];
        else if (s == "--skip" && i + 1 < argc) skip_s = argv[++i];
        else if (s == "--names" && i + 1 < argc) names = argv[++i];
        else if (s == "--base" && i + 1 < argc) base = argv[++i];
        else if (s == "--sidecar" && i + 1 < argc) sidecar = argv[++i];
        else if (s.rfind("--", 0) == 0 && s.size() > 2) flags.push_back(s);
        else a.push_back(s);
    }
    auto flag = [&](const char* f) { return std::find(flags.begin(), flags.end(), f) != flags.end(); };
    if (flag("--help") || a.empty()) { std::fputs(kUsage, a.empty() && !flag("--help") ? stderr : stdout); return a.empty() && !flag("--help") ? 2 : 0; }
    for (const std::string& f : flags)
        if (f != "--keep-palette" && f != "--restrip" && f != "--keep-mips" && f != "--allow-exports" && f != "--force" && f != "--help")
            return fail("unknown option " + f);
    VerifyOptions vopt;
    vopt.allow_exports = flag("--allow-exports");
    const std::string& c = a[0];
    const std::size_t n = a.size();
    if (c == "list" && (n == 2 || n == 3)) return cmd_list(a[1], n == 3 ? a[2] : "");
    if (c == "extract" && n == 4) return cmd_extract(a[1], a[2], a[3]);
    if (c == "txd" && n >= 2) {
        if (a[1] == "list" && n == 3) return cmd_txd_list(a[2]);
        if (a[1] == "export" && n == 4) return cmd_txd_export(a[2], a[3]);
        if (a[1] == "import" && n == 5) {
            EncodeOptions o;
            o.keep_palette = flag("--keep-palette");
            o.keep_mips = flag("--keep-mips");
            return cmd_txd_import(a[2], a[3], a[4], o);
        }
    }
    if (c == "strings" && n >= 2) {
        if (a[1] == "export" && n == 4) return cmd_strings_export(a[2], a[3]);
        if (a[1] == "import" && n == 5) return cmd_strings_import(a[2], a[3], a[4]);
    }
    if (c == "vdb" && n == 4) {
        if (a[1] == "export") return cmd_vdb_export(a[2], a[3], names);
        if (a[1] == "import") return cmd_vdb_import(a[2], a[3], base, names);
    }
    if (c == "audio" && n >= 3) {
        if (a[1] == "list" && n == 3) return cmd_audio_list(a[2]);
        if (a[1] == "export" && n == 4) return cmd_audio_export(a[2], a[3]);
        if (a[1] == "import" && n == 5) return cmd_audio_import(a[2], a[3], a[4]);
    }
    if ((c == "car" || c == "track") && n >= 3) {
        auto number = [](const std::string& v, int& out) {
            if (v == "all") { out = -1; return true; }
            char* end = nullptr;
            const long x = std::strtol(v.c_str(), &end, 10);
            if (v.empty() || *end || x < 0 || x > 100000) return false;
            out = int(x);
            return true;
        };
        if (format != "gltf" && format != "obj") return fail("--format is gltf or obj");
        if (c == "car" && a[1] == "list" && n == 3) return cmd_car_list(a[2]);
        if (c == "track" && a[1] == "list" && n == 3) return cmd_track_list(a[2]);
        if (c == "car" && a[1] == "export" && n == 5) {
            int lod = -1, paint = 0;
            if (!number(lod_s, lod) || lod > 4) return fail("--lod is 0..4 or all");
            if (!number(paint_s, paint) || paint < 0) return fail("--paint is a number");
            return cmd_car_export(a[2], a[3], a[4], lod, format == "obj", paint);
        }
        if (c == "car" && a[1] == "import" && n == 5) return cmd_car_import(a[2], a[3], a[4], sidecar);
        if (c == "car" && a[1] == "check" && n == 4) return cmd_car_import(a[2], a[3], "", sidecar);
        if (c == "track" && a[1] == "import" && n == 5) return cmd_track_import(a[2], a[3], a[4], sidecar, flag("--restrip"));
        if (c == "track" && a[1] == "check" && n == 4) return cmd_track_import(a[2], a[3], "", sidecar, flag("--restrip"));
        if (c == "track" && a[1] == "export" && n == 5) {
            int section = -1;
            if (!number(section_s, section)) return fail("--section is a number or all");
            return cmd_track_export(a[2], a[3], a[4], section, format == "obj");
        }
    }
    if (c == "preview" && n == 3) return cmd_preview(a[1], a[2], float(std::atof(yaw_s.c_str())), float(std::atof(pitch_s.c_str())), skip_s);
    if (c == "diff" && n == 4) return cmd_diff(a[1], a[2], a[3]);
    if (c == "apply" && n == 4) return cmd_apply(a[1], a[2], a[3]);
    if (c == "pack" && n == 3) return cmd_pack(a[1], a[2], game, vopt, flag("--force"));
    if (c == "verify" && n == 2) return cmd_verify(a[1], game, vopt);
    if (c == "roundtrip" && (n == 2 || n == 3)) return cmd_roundtrip(a[1], n == 3 ? a[2] : "all");
    return usage();
}
