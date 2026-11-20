// Import Xbox textures (include/b3asset/xboximport.h).
#include "b3asset/xboximport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>

#include "b3asset/bc.h"
#include "b3asset/discfs.h"
#include "b3asset/hdpack.h"
#include "b3asset/xboxtex.h"
#include "b3asset/xdvdfs.h"
#include "b3port/fileio.h"

namespace b3asset::xboximport {

namespace fs = std::filesystem;
namespace {
constexpr int kHole = 8;   // alpha below it (of 255): a transparent texel, its colour unseen
}

const char* reason_name(int r) {
    static const char* const kNames[kReasons] = {"no Xbox file", "no Xbox texture of that name", "Xbox texture not larger",
                                                 "32 texels or less", "water placeholder", "several palettes",
                                                 "colour check failed", "Xbox format not handled", "car paint mapping not clean"};
    return r >= 0 && r < kReasons ? kNames[r] : "?";
}

// ---- the image steps ----

Image box_down(const Image& s, int w, int h) {
    Image o;
    o.w = w;
    o.h = h;
    o.rgba.assign(std::size_t(w) * h * 4, 0);
    if (w <= 0 || h <= 0 || s.w % w || s.h % h) return o;
    const int fx = s.w / w, fy = s.h / h, n = fx * fy;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int acc[4] = {};
            for (int j = 0; j < fy; ++j) {
                const std::uint8_t* r = &s.rgba[((std::size_t(y) * fy + j) * s.w + std::size_t(x) * fx) * 4];
                for (int i = 0; i < fx; ++i)
                    for (int k = 0; k < 4; ++k) acc[k] += r[i * 4 + k];
            }
            std::uint8_t* d = &o.rgba[(std::size_t(y) * w + x) * 4];
            for (int k = 0; k < 4; ++k) d[k] = std::uint8_t((acc[k] + n / 2) / n);
        }
    return o;
}

Image bilinear(const Image& s, int w, int h, bool wrap) {
    Image o;
    o.w = w;
    o.h = h;
    o.rgba.assign(std::size_t(w) * h * 4, 0);
    const auto idx = [wrap](int i, int n) { return wrap ? ((i % n) + n) % n : std::clamp(i, 0, n - 1); };
    struct Tap { int a, b; double t; };
    std::vector<Tap> xs(static_cast<std::size_t>(w));
    for (int x = 0; x < w; ++x) {
        const double f = (x + 0.5) * s.w / w - 0.5, f0 = std::floor(f);
        xs[std::size_t(x)] = {idx(int(f0), s.w) * 4, idx(int(f0) + 1, s.w) * 4, f - f0};
    }
    for (int y = 0; y < h; ++y) {
        const double f = (y + 0.5) * s.h / h - 0.5, f0 = std::floor(f), ty = f - f0;
        const std::uint8_t* r0 = &s.rgba[std::size_t(idx(int(f0), s.h)) * s.w * 4];
        const std::uint8_t* r1 = &s.rgba[std::size_t(idx(int(f0) + 1, s.h)) * s.w * 4];
        std::uint8_t* d = &o.rgba[std::size_t(y) * w * 4];
        for (const Tap& t : xs) {
            for (int k = 0; k < 4; ++k) {
                const double top = r0[t.a + k] * (1 - t.t) + r0[t.b + k] * t.t;
                const double bot = r1[t.a + k] * (1 - t.t) + r1[t.b + k] * t.t;
                d[k] = std::uint8_t(std::clamp(int(top * (1 - ty) + bot * ty + 0.5), 0, 255));
            }
            d += 4;
        }
    }
    return o;
}

Image bleed(const Image& im, bool wrap) {
    const int w = im.w, h = im.h;
    const std::size_t n = std::size_t(w) * h;
    std::vector<std::uint8_t> filled(n);
    double sum[3] = {};
    std::size_t vis = 0;
    for (std::size_t i = 0; i < n; ++i) {
        filled[i] = im.rgba[i * 4 + 3] >= kHole;
        if (!filled[i]) continue;
        ++vis;
        for (int k = 0; k < 3; ++k) sum[k] += im.rgba[i * 4 + std::size_t(k)];
    }
    if (vis == 0 || vis == n) return im;
    Image o = im;
    const auto at = [&](int x, int y) -> long {
        if (wrap) return long(((y % h) + h) % h) * w + ((x % w) + w) % w;
        if (x < 0 || y < 0 || x >= w || y >= h) return -1;
        return long(y) * w + x;
    };
    std::vector<std::pair<std::size_t, std::array<int, 3>>> next;
    for (int pass = 0; pass < 16; ++pass) {   // the visible colour, a texel a pass
        next.clear();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const std::size_t i = std::size_t(y) * w + x;
                if (filled[i]) continue;
                int acc[3] = {}, c = 0;
                for (const auto& d : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const long q = at(x + d.first, y + d.second);
                    if (q < 0 || !filled[std::size_t(q)]) continue;
                    for (int k = 0; k < 3; ++k) acc[k] += o.rgba[std::size_t(q) * 4 + std::size_t(k)];
                    ++c;
                }
                if (c) next.push_back({i, {acc[0] / c, acc[1] / c, acc[2] / c}});
            }
        if (next.empty()) break;
        for (const auto& [i, v] : next) {
            for (int k = 0; k < 3; ++k) o.rgba[i * 4 + std::size_t(k)] = std::uint8_t(v[std::size_t(k)]);
            filled[i] = 1;
        }
    }
    for (std::size_t i = 0; i < n; ++i)   // farther than 16 texels from anything visible: the mean visible colour
        if (!filled[i])
            for (int k = 0; k < 3; ++k) o.rgba[i * 4 + std::size_t(k)] = std::uint8_t(sum[k] / double(vis) + 0.5);
    return o;
}

Check colour_check(const Image& ps2, const Image& xbox) {
    Check c;
    if (ps2.w <= 0 || ps2.h <= 0 || xbox.w % ps2.w || xbox.h % ps2.h) return c;
    // (the Xbox picture as it is: an Xbox alpha that is not coverage has real colour under alpha 0, so no spreading here)
    const Image down = box_down(xbox, ps2.w, ps2.h);
    const std::size_t n = std::size_t(ps2.w) * ps2.h;
    std::vector<std::size_t> vis;   // the texels both show (alpha over 1/4)
    for (std::size_t i = 0; i < n; ++i)
        if (ps2.rgba[i * 4 + 3] > 64 && down.rgba[i * 4 + 3] > 64) vis.push_back(i);
    if (vis.empty())
        for (std::size_t i = 0; i < n; ++i) vis.push_back(i);
    for (int k = 0; k < 3; ++k) {
        double sp = 0, sx = 0;
        for (std::size_t i : vis) {
            sp += ps2.rgba[i * 4 + k];
            sx += down.rgba[i * 4 + k];
        }
        c.gain[k] = sp / std::max(1.0, sx);
    }
    double se = 0;
    for (std::size_t i : vis)
        for (int k = 0; k < 3; ++k) {
            const int adj = std::min(255, int(down.rgba[i * 4 + k] * c.gain[k] + 0.5));
            const double d = adj - int(ps2.rgba[i * 4 + k]);
            se += d * d;
        }
    c.psnr = se == 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 * 3.0 * double(vis.size()) / se);
    // alpha: does the Xbox alpha mean what the PS2 one does (the study: 339 same-size Xbox textures have an alpha of their
    // own)? The mean difference at the PS2 size; and is the PS2 texture opaque throughout (it stays so: BC1)
    double ad = 0;
    c.ps2_opaque = true;
    for (std::size_t i = 0; i < n; ++i) {
        ad += std::abs(int(down.rgba[i * 4 + 3]) - int(ps2.rgba[i * 4 + 3]));
        c.ps2_opaque = c.ps2_opaque && ps2.rgba[i * 4 + 3] == 255;
    }
    c.alpha_agrees = n && ad / double(n) < 16.0;
    c.ok = c.psnr >= 20.0 && *std::max_element(c.gain, c.gain + 3) <= 2.0 && *std::min_element(c.gain, c.gain + 3) >= 0.5;
    return c;
}

// (`wrap` only for the track textures, which the game repeats; a track texture the game clamps instead gets a
// half-texel line of the opposite edge at its border from the wrapped filter: accepted, it is under one PS2 texel)
Image detail_transfer(const Image& ps2, const Image& xbox, const Check& c, bool wrap) {
    // the visible colour spread into the transparent texels first (PS2 holes and DXT1 punch-through decode black): no
    // dark fringes along cut-outs from the up-scaling
    const Image p = bleed(ps2, wrap), x = bleed(xbox, wrap);
    const Image down = box_down(x, ps2.w, ps2.h);
    const Image up_p = bilinear(p, xbox.w, xbox.h, wrap), up_x = bilinear(down, xbox.w, xbox.h, wrap);
    Image o;
    o.w = xbox.w;
    o.h = xbox.h;
    o.rgba.resize(xbox.rgba.size());
    for (std::size_t i = 0; i < o.rgba.size(); i += 4) {
        // the colour detail only where the Xbox texel and its blurred copy are both mostly opaque
        const bool solid = xbox.rgba[i + 3] >= 128 && up_x.rgba[i + 3] >= 128;
        for (int k = 0; k < 3; ++k) {
            const double v = up_p.rgba[i + k] + (solid ? (int(x.rgba[i + k]) - int(up_x.rgba[i + k])) * c.gain[k] : 0.0);
            o.rgba[i + k] = std::uint8_t(std::clamp(int(std::floor(v + 0.5)), 0, 255));
        }
        // alpha: opaque stays opaque; the Xbox detail only where its alpha agrees with the PS2 one
        o.rgba[i + 3] = c.ps2_opaque ? 255
                        : c.alpha_agrees ? std::uint8_t(std::clamp(int(up_p.rgba[i + 3]) + int(xbox.rgba[i + 3]) - int(up_x.rgba[i + 3]), 0, 255))
                                         : up_p.rgba[i + 3];
    }
    return o;
}

Image car_map(const Image& ps2, const std::vector<std::uint8_t>& index, int w, int h, CarCheck& c) {
    c = CarCheck{};
    Image o;
    if (ps2.w <= 0 || ps2.h <= 0 || w % ps2.w || h % ps2.h || index.size() != std::size_t(w) * h) return o;
    const int fx = w / ps2.w, fy = h / ps2.h;
    const auto ps2_at = [&](int x, int y) { return &ps2.rgba[((std::size_t(y) / fy) * ps2.w + std::size_t(x) / fx) * 4]; };
    // votes: (index, PS2 RGBA) pairs, counted; ties go to the colour seen first (in texel order)
    const std::size_t n = index.size();
    std::vector<std::pair<std::uint64_t, std::uint32_t>> v(n);   // (index << 32 | colour, texel)
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            std::uint32_t col;
            std::memcpy(&col, ps2_at(x, y), 4);
            v[i] = {std::uint64_t(index[i]) << 32 | col, std::uint32_t(i)};
        }
    std::sort(v.begin(), v.end());
    struct Best { std::uint32_t col = 0, count = 0, first = 0xFFFFFFFFu; bool any = false; };
    std::array<Best, 256> best{};
    struct Run { std::uint32_t col, count; };
    std::array<std::vector<Run>, 256> runs;
    for (std::size_t a = 0; a < n;) {
        std::size_t b = a;
        while (b < n && v[b].first == v[a].first) ++b;
        const int ix = int(v[a].first >> 32);
        const std::uint32_t col = std::uint32_t(v[a].first), cnt = std::uint32_t(b - a), first = v[a].second;   // (sorted: the run's first texel)
        Best& bb = best[std::size_t(ix)];
        if (!bb.any || cnt > bb.count || (cnt == bb.count && first < bb.first)) bb = {col, cnt, first, true};
        runs[std::size_t(ix)].push_back({col, cnt});
        a = b;
    }
    std::array<std::uint8_t[4], 256> pal{};
    for (int i = 0; i < 256; ++i) std::memcpy(pal[std::size_t(i)], &best[std::size_t(i)].col, 4);
    std::size_t near = 0;
    for (int i = 0; i < 256; ++i) {
        const std::uint8_t* m = pal[std::size_t(i)];
        for (const Run& r : runs[std::size_t(i)]) {
            std::uint8_t q[4];
            std::memcpy(q, &r.col, 4);
            int d = 0;
            for (int k = 0; k < 3; ++k) d = std::max(d, std::abs(int(q[k]) - int(m[k])));
            if (d <= 24) near += r.count;
        }
    }
    c.consistency = double(near) / double(n);
    o.w = w;
    o.h = h;
    o.rgba.resize(n * 4);
    std::size_t fb = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            const std::uint8_t* m = pal[index[i]];
            const std::uint8_t* p = ps2_at(x, y);
            int d = 0;
            for (int k = 0; k < 4; ++k) d = std::max(d, std::abs(int(m[k]) - int(p[k])));
            const std::uint8_t* src = d > 48 ? p : m;   // differs from the PS2 texel: keep the PS2's
            fb += d > 48;
            std::memcpy(&o.rgba[i * 4], src, 4);
        }
    c.fallback = double(fb) / double(n);
    const Image down = box_down(o, ps2.w, ps2.h);
    double se = 0;
    for (std::size_t i = 0; i < down.rgba.size(); ++i) {
        const double d = int(down.rgba[i]) - int(ps2.rgba[i]);
        se += d * d;
    }
    c.psnr = se == 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 * double(down.rgba.size()) / se);
    c.ok = c.consistency >= 0.88 && c.psnr >= 26.0;
    return o;
}

Image extend(const Image& im, int w, int h) {
    if (w == im.w && h == im.h) return im;
    Image o;
    o.w = w;
    o.h = h;
    o.rgba.resize(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            std::memcpy(&o.rgba[(std::size_t(y) * w + x) * 4], &im.rgba[(std::size_t(std::min(y, im.h - 1)) * im.w + std::min(x, im.w - 1)) * 4], 4);
    return o;
}

std::vector<std::uint8_t> encode_dds(const Image& im, int* fmt_out) {
    std::vector<std::uint8_t> ps2(im.rgba);
    bool opaque = true;
    for (std::size_t i = 3; i < ps2.size(); i += 4) {
        ps2[i] = std::uint8_t((int(ps2[i]) + 1) / 2);   // PS2 units: 255 -> 0x80 (opaque), a -> a / 2
        opaque = opaque && ps2[i] == 0x80;
    }
    const int fmt = opaque ? kBC1 : kBC3;
    if (opaque)
        for (std::size_t i = 3; i < ps2.size(); i += 4) ps2[i] = 255;   // (BC1: opaque)
    std::vector<std::uint8_t> blocks;
    bc_encode(fmt, ps2.data(), im.w, im.h, blocks, 1);
    std::vector<std::uint8_t> d(128, 0);
    const auto put = [&](std::size_t at, std::uint32_t v) { std::memcpy(&d[at], &v, 4); };
    std::memcpy(&d[0], "DDS ", 4);
    put(4, 124);
    put(8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000);   // CAPS HEIGHT WIDTH PIXELFORMAT LINEARSIZE
    put(12, std::uint32_t(im.h));
    put(16, std::uint32_t(im.w));
    put(20, std::uint32_t(blocks.size()));
    put(76, 32);
    put(80, 0x4);   // FOURCC
    std::memcpy(&d[84], fmt == kBC1 ? "DXT1" : "DXT5", 4);
    put(108, 0x1000);   // TEXTURE
    d.insert(d.end(), blocks.begin(), blocks.end());
    if (fmt_out) *fmt_out = fmt;
    return d;
}

// ---- the import ----

namespace {

enum Group : int { kTracks, kCars, kFrontEnd };
const char* group_name(int g) { return g == kTracks ? "tracks" : g == kCars ? "cars" : "front end"; }

std::string upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return s;
}
std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
bool ends_with(const std::string& s, const char* t) {
    const std::size_t n = std::strlen(t);
    return s.size() >= n && s.compare(s.size() - n, n, t) == 0;
}

struct PsFile { std::string rel; fs::path path; int group; };

// The PS2 files with textures: TRACKS/<region>/<route>/STATIC.DAT and ENVIRO.DAT, PVEH/<class>/*.BGV and *.BTV,
// DATA/GLOBAL.TXD and DATA/FRONTEND.TXD (names as the disc has them; sorted for a stable order)
std::vector<PsFile> ps2_files(const fs::path& game) {
    std::vector<PsFile> out;
    const auto sub = [](const fs::path& dir) {
        std::vector<disc::Entry> es = disc::list_dir(dir);
        std::sort(es.begin(), es.end(), [](const disc::Entry& a, const disc::Entry& b) { return upper(a.name) < upper(b.name); });
        return es;
    };
    const fs::path tracks = disc::resolve_ci(game, "TRACKS"), pveh = disc::resolve_ci(game, "PVEH");
    if (!tracks.empty())
        for (const auto& region : sub(tracks))
            if (region.dir)
                for (const auto& route : sub(region.path))
                    if (route.dir)
                        for (const auto& f : sub(route.path)) {
                            const std::string u = upper(f.name);
                            if (!f.dir && (u == "STATIC.DAT" || u == "ENVIRO.DAT"))
                                out.push_back({"TRACKS/" + region.name + "/" + route.name + "/" + f.name, f.path, kTracks});
                        }
    if (!pveh.empty())
        for (const auto& cls : sub(pveh))
            if (cls.dir)
                for (const auto& f : sub(cls.path)) {
                    const std::string u = upper(f.name);
                    if (!f.dir && (ends_with(u, ".BGV") || ends_with(u, ".BTV"))) out.push_back({"PVEH/" + cls.name + "/" + f.name, f.path, kCars});
                }
    for (const char* rel : {"DATA/GLOBAL.TXD", "DATA/FRONTEND.TXD"}) {
        const fs::path p = disc::resolve_ci(game, rel);
        if (!p.empty() && disc::is_file(p)) out.push_back({rel, p, kFrontEnd});
    }
    return out;
}

// One occurrence of a key: a raster variant in a PS2 file and the Xbox texture of its name there (if any)
struct Occ {
    HdKey key;
    int file = 0;
    std::size_t offset = 0;
    int variant = 0;
    std::uint32_t cbp_add = 0;
    int pw = 0, ph = 0, tw = 0, th = 0;
    std::string name;
    bool xfile = false;                  // the Xbox file is there
    std::size_t xoff = SIZE_MAX;         // the Xbox texture (SIZE_MAX: none of that name)
    int xw = 0, xh = 0, xfmt = 0;
};

struct Task { HdKey key; Occ occ; bool car = false; };

// An exception's message for the result (out of memory: a damaged image's sizes, most likely)
std::string exception_text(const std::exception& e) {
    if (dynamic_cast<const std::bad_alloc*>(&e)) return "out of memory while reading the disc image (a damaged image?)";
    return std::string("the import stopped: ") + e.what();
}

// `fn` for 0..n-1 on `threads` threads. An exception in one (out of memory, a filesystem error) stops the rest and is
// returned as text (empty: none), so it never reaches std::thread (std::terminate)
std::string run_pool(int threads, std::size_t n, const std::function<void(std::size_t)>& fn) {
    std::atomic<std::size_t> next{0};
    std::atomic<bool> stop{false};
    std::mutex m;
    std::string error;
    std::vector<std::thread> ts;
    const int t = std::max(1, std::min<int>(threads, int(n)));
    for (int i = 0; i < t; ++i)
        ts.emplace_back([&]() {
            for (std::size_t j; !stop && (j = next.fetch_add(1)) < n;) {
                try {
                    fn(j);
                } catch (const std::exception& e) {
                    const std::lock_guard<std::mutex> lk(m);
                    if (error.empty()) error = exception_text(e);
                    stop = true;
                } catch (...) {
                    const std::lock_guard<std::mutex> lk(m);
                    if (error.empty()) error = "the import stopped: an unknown error";
                    stop = true;
                }
            }
        });
    for (auto& th : ts) th.join();
    return error;
}

bool write_file(const fs::path& p, const std::vector<std::uint8_t>& d) {
    std::FILE* f = b3io::fopen(p, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(d.data(), 1, d.size(), f) == d.size();
    return std::fclose(f) == 0 && ok;
}

std::int64_t unix_time(fs::file_time_type t) {
    const auto s = std::chrono::time_point_cast<std::chrono::seconds>(t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return s.time_since_epoch().count();
}

}  // namespace

std::uint64_t free_bytes(const fs::path& dir) {
    std::error_code ec;
    fs::path p = dir;
    while (!p.empty() && !fs::exists(p, ec)) {
        const fs::path up = p.parent_path();
        if (up == p) break;
        p = up;
    }
    const fs::space_info s = fs::space(p.empty() ? fs::path(".") : p, ec);
    return ec ? 0 : std::uint64_t(s.available);
}

bool remove_tree_plain(const fs::path& p) {
    std::error_code ec;
    fs::remove_all(p, ec);
    return !ec && !fs::exists(p, ec);
}

namespace {
Result run_import(const fs::path& image, const fs::path& game_dir, const fs::path& out_dir, const std::function<void(const Progress&)>& progress,
                  const std::atomic<bool>* cancel, const Options& opt) {
    const auto t0 = std::chrono::steady_clock::now();
    Result res;
    std::error_code ec;
    fs::path partial = out_dir;
    partial += ".partial";
    const RemoveTree rm = opt.remove_tree ? opt.remove_tree : RemoveTree(remove_tree_plain);   // modsec
    const auto cancelled = [&]() { return cancel && cancel->load(); };
    const auto finish = [&](Result& r) -> Result {
        if (!r.ok) rm(partial);
        r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return r;
    };
    const auto fail = [&](const std::string& why) {
        res.ok = false;
        res.error = why;
        return finish(res);
    };

    XdvdfsImage xiso;
    std::string why;
    if (!xiso.open(image, &why)) return fail(why == "cannot open the file" ? "cannot open the disc image" : why);
    if (!xiso.find("Data/Global.txd") || !xiso.find("Data/Frontend.txd") || !xiso.find("default.xbe"))
        return fail("this Xbox disc image is not Burnout 3: Takedown");
    const std::vector<PsFile> files = ps2_files(game_dir);
    if (files.empty()) return fail("the game's files were not found (choose the game's folder first)");
    if (opt.need_bytes) {
        const std::uint64_t avail = free_bytes(out_dir.parent_path());
        if (avail && avail < opt.need_bytes)
            return fail("not enough free space (about " + std::to_string(opt.need_bytes >> 20) + " MB needed)");
    }
    rm(partial);   // (a crashed import's)
    if (!fs::create_directories(partial, ec) || ec) return fail("cannot create the folder " + b3io::u8string(partial));

    const int threads = opt.threads > 0 ? opt.threads : int(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
    std::mutex m;   // progress, results
    Progress pr;
    const auto report = [&](int stage, int done, int total, int group) {   // (under m)
        pr.stage = stage;
        pr.done = done;
        pr.total = total;
        pr.group = group_name(group);
        pr.written = res.written;
        if (progress) progress(pr);
    };

    // 1. every raster variant's base key, and the Xbox texture of its name in the same file
    std::vector<std::vector<Occ>> occs(files.size());
    std::atomic<int> read{0};
    {
        const std::lock_guard<std::mutex> lk(m);
        report(0, 0, int(files.size()), files[0].group);
    }
    const auto fits_cap = [&](std::uint64_t n) { return n <= opt.max_file_bytes; };
    std::string pool_error = run_pool(threads, files.size(), [&](std::size_t fi) {
        if (cancelled()) return;
        const PsFile& pf = files[fi];
        std::vector<std::uint8_t> ps, xb;
        std::vector<xbox::Texture> xt;
        bool have_x = false;
        if (fits_cap(std::uint64_t(std::max<std::int64_t>(disc::file_size(pf.path), 0))) && disc::read_file(pf.path, ps)) {
            if (const XdvdfsImage::Entry* e = xiso.find(pf.rel); e && fits_cap(e->size)) {   // (larger: a damaged image's claim)
                have_x = xiso.read(*e, xb);
                if (have_x) xt = xbox::scan(xb.data(), xb.size());
            }
            std::unordered_multimap<std::string, std::size_t> by_name;
            for (std::size_t i = 0; i < xt.size(); ++i) by_name.emplace(lower(xt[i].name), i);
            GsMemory gs;
            hd_each_raster(Bytes{ps.data(), ps.size()}, gs, [&](const HdRasterVariant& rv) {
                if (cancelled()) return false;
                const HdCandidate* b = rv.base();
                if (!rv.decoded || !b || rv.raster.levels.empty()) return true;
                Occ o;
                o.key = b->key;
                o.file = int(fi);
                o.offset = rv.offset;
                o.variant = rv.variant;
                o.cbp_add = rv.cbp_add;
                o.pw = rv.raster.levels[0].w;
                o.ph = rv.raster.levels[0].h;
                o.tw = 1 << int(rv.raster.hdr.tex0 >> 26 & 0xF);
                o.th = 1 << int(rv.raster.hdr.tex0 >> 30 & 0xF);
                o.name = rv.raster.hdr.name;
                o.xfile = have_x;
                // the Xbox texture of that name (several: the largest whole multiple of the PS2 size, else the last)
                const auto range = by_name.equal_range(lower(o.name));
                const xbox::Texture* pick = nullptr;
                long best = -1;
                for (auto it = range.first; it != range.second; ++it) {
                    const xbox::Texture& t = xt[it->second];
                    const bool whole = o.pw > 0 && o.ph > 0 && t.width % o.pw == 0 && t.height % o.ph == 0;
                    const long score = whole ? long(t.width) * t.height : 0;
                    if (!pick || score > best || (score == best && it->second > std::size_t(pick - xt.data()))) { pick = &t; best = score; }
                }
                if (pick) {
                    o.xoff = pick->offset;
                    o.xw = pick->width;
                    o.xh = pick->height;
                    o.xfmt = pick->format;
                }
                occs[fi].push_back(std::move(o));
                return true;
            });
        }
        const std::lock_guard<std::mutex> lk(m);
        ++res.files;
        report(0, ++read, int(files.size()), pf.group);
    });
    if (!pool_error.empty()) return fail(pool_error);
    if (cancelled()) { res.cancelled = true; return finish(res); }

    // 2. per key, the source: the occurrences in file order; the largest Xbox texture wins (the first of equals)
    std::unordered_map<HdKey, std::vector<const Occ*>, HdKeyHash> by_key;
    std::vector<HdKey> order;
    for (const auto& v : occs)
        for (const Occ& o : v) {
            auto& list = by_key[o.key];
            if (list.empty()) order.push_back(o.key);
            list.push_back(&o);
        }
    res.keys = order.size();
    std::vector<std::vector<Task>> tasks(files.size());
    for (const HdKey& k : order) {
        const auto& list = by_key[k];
        const Occ& r0 = *list.front();
        const bool car = files[std::size_t(r0.file)].group == kCars;
        if (car && !opt.cars) continue;
        const auto skip = [&](int why_) { ++res.skipped[std::size_t(why_)]; };
        if (!car) {
            if (lower(r0.name).rfind("water", 0) == 0) { skip(kWater); continue; }
            if (std::any_of(list.begin(), list.end(), [](const Occ* o) { return o->variant > 0; })) { skip(kPaletteVariant); continue; }
            if (std::max(r0.pw, r0.ph) <= 32) { skip(kSmall); continue; }
        }
        const Occ* best = nullptr;
        bool any_x = false, any_file = false;
        for (const Occ* o : list) {
            any_file = any_file || o->xfile;
            if (o->xoff == SIZE_MAX) continue;
            any_x = true;
            if (o->xw % o->pw || o->xh % o->ph || long(o->xw) * o->xh <= long(o->pw) * o->ph) continue;
            if (!best || long(o->xw) * o->xh > long(best->xw) * best->xh) best = o;
        }
        if (!best) { skip(!any_file ? kNoXboxFile : !any_x ? kNoMatch : kNotLarger); continue; }
        if (car ? best->xfmt != xbox::kP8 : !(best->xfmt == xbox::kP8 || best->xfmt == xbox::kDXT1 || best->xfmt == xbox::kDXT3 || best->xfmt == xbox::kDXT5)) {
            skip(kFormat);
            continue;
        }
        tasks[std::size_t(best->file)].push_back({k, *best, car});
    }
    std::vector<std::size_t> work;
    for (std::size_t i = 0; i < tasks.size(); ++i)
        if (!tasks[i].empty()) work.push_back(i);

    // 3. decode, transfer or map, encode and write
    std::atomic<int> built{0};
    {
        const std::lock_guard<std::mutex> lk(m);
        report(1, 0, int(work.size()), work.empty() ? kTracks : files[work[0]].group);
    }
    bool write_failed = false;
    pool_error = run_pool(threads, work.size(), [&](std::size_t wi) {
        if (cancelled()) return;
        const std::size_t fi = work[wi];
        const PsFile& pf = files[fi];
        std::vector<std::uint8_t> ps, xb;
        std::array<std::size_t, kReasons> skipped{};
        std::size_t written = 0, cars = 0, alpha_ps2 = 0;
        std::uint64_t bytes = 0;
        bool failed = false;
        const XdvdfsImage::Entry* xe = xiso.find(pf.rel);
        if (disc::read_file(pf.path, ps) && xe && fits_cap(xe->size) && xiso.read(*xe, xb)) {   // (both under the cap in step 1)
            const Bytes f{ps.data(), ps.size()};
            GsMemory gs;
            const HdNoHookScope no_hook;
            for (const Task& t : tasks[fi]) {
                if (cancelled()) break;
                const Occ& o = t.occ;
                DecodedRaster r;
                xbox::Texture xt;
                if (!decode_raster_clut(f, o.offset, gs, r, o.cbp_add) || r.levels.empty() || r.levels[0].w != o.pw || r.levels[0].h != o.ph ||
                    !xbox::parse(xb.data(), xb.size(), o.xoff, xt)) {
                    ++skipped[kFormat];
                    continue;
                }
                const Image& p = r.levels[0];
                Image out;
                if (t.car) {
                    std::vector<std::uint8_t> idx;
                    if (!xbox::indices(xb.data(), xb.size(), xt, idx)) { ++skipped[kFormat]; continue; }
                    CarCheck cc;
                    out = car_map(p, idx, xt.width, xt.height, cc);
                    if (!cc.ok) { ++skipped[kCarMap]; continue; }
                } else {
                    Image x;
                    x.w = xt.width;
                    x.h = xt.height;
                    if (!xbox::decode(xb.data(), xb.size(), xt, x.rgba)) { ++skipped[kFormat]; continue; }
                    const Check c = colour_check(p, x);
                    if (!c.ok) { ++skipped[kCheck]; continue; }
                    out = detail_transfer(p, x, c, pf.group == kTracks);
                    alpha_ps2 += !c.ps2_opaque && !c.alpha_agrees;
                }
                // the replacement covers TEX0's 2^TW x 2^TH at the same scale
                out = extend(out, out.w / o.pw * o.tw, out.h / o.ph * o.th);
                const std::vector<std::uint8_t> dds = encode_dds(out);
                if (!write_file(partial / (hd_key_name(t.key) + ".dds"), dds)) { failed = true; break; }
                ++written;
                cars += t.car;
                bytes += dds.size();
            }
        } else {
            failed = true;
        }
        const std::lock_guard<std::mutex> lk(m);
        for (int i = 0; i < kReasons; ++i) res.skipped[std::size_t(i)] += skipped[std::size_t(i)];
        res.written += written;
        res.cars += cars;
        res.alpha_ps2 += alpha_ps2;
        res.bytes += bytes;
        write_failed = write_failed || failed;
        report(1, ++built, int(work.size()), pf.group);
    });
    if (!pool_error.empty()) return fail(pool_error);
    if (cancelled()) { res.cancelled = true; return finish(res); }
    if (write_failed) return fail("could not read the disc or write the textures (is the drive full?)");
    if (res.written == 0) return fail("no Xbox texture matched the game's (a different version of the game?)");

    // 4. import.ini, then the folder in place of an earlier import
    {
        std::string ini = "; Import Xbox textures: made by the launcher from the player's own Xbox disc\n[import]\n";
        const auto kv = [&](const char* k, const std::string& v) { ini += std::string(k) + " = " + v + "\n"; };
        kv("version", std::to_string(kVersion));
        kv("image", b3io::u8string(image.filename()));
        kv("image_size", std::to_string(xiso.image_size()));
        const auto mt = fs::last_write_time(image, ec);
        kv("image_time", std::to_string(ec ? 0 : unix_time(mt)));
        kv("date", std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
        kv("written", std::to_string(res.written));
        kv("cars", std::to_string(res.cars));
        kv("bytes", std::to_string(res.bytes));
        kv("keys", std::to_string(res.keys));
        kv("alpha_ps2", std::to_string(res.alpha_ps2));
        for (int i = 0; i < kReasons; ++i) ini += "; skipped, " + std::string(reason_name(i)) + ": " + std::to_string(res.skipped[std::size_t(i)]) + "\n";
        if (!write_file(partial / kInfoName, std::vector<std::uint8_t>(ini.begin(), ini.end()))) return fail("could not write " + std::string(kInfoName));
    }
    fs::path old = out_dir;
    old += ".old";
    rm(old);
    if (fs::exists(out_dir, ec)) {
        fs::rename(out_dir, old, ec);
        if (ec) return fail("could not replace the earlier import (a file in it is open?)");
    }
    fs::rename(partial, out_dir, ec);
    if (ec) {
        std::error_code ec2;
        if (fs::exists(old, ec2)) fs::rename(old, out_dir, ec2);
        return fail("could not move the textures into place");
    }
    rm(old);
    res.ok = true;
    return finish(res);
}

}  // namespace

Result run(const fs::path& image, const fs::path& game_dir, const fs::path& out_dir, const std::function<void(const Progress&)>& progress,
           const std::atomic<bool>* cancel, const Options& opt) {
    try {
        return run_import(image, game_dir, out_dir, progress, cancel, opt);
    } catch (const std::exception& e) {   // (the steps on this thread: the workers' are caught in run_pool)
        Result r;
        r.error = exception_text(e);
        fs::path partial = out_dir;
        partial += ".partial";
        try {
            (opt.remove_tree ? opt.remove_tree : RemoveTree(remove_tree_plain))(partial);
        } catch (...) {
        }
        return r;
    }
}

Status read_status(const fs::path& dir) {
    Status s;
    std::FILE* f = b3io::fopen(dir / kInfoName, "rb");
    if (!f) return s;
    std::string text;
    char buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (1u << 20);) text.append(buf, n);
    std::fclose(f);
    s.present = true;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t eq = line.find('=');
        if (line.empty() || line[0] == ';' || line[0] == '[' || eq == std::string::npos) continue;
        auto trim = [](std::string v) {
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
            std::size_t i = 0;
            while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) ++i;
            return v.substr(i);
        };
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        const auto num = [&]() { return std::strtoll(v.c_str(), nullptr, 10); };
        if (k == "version") s.version = int(num());
        else if (k == "image") s.image = v;
        else if (k == "image_size") s.image_size = std::uint64_t(num());
        else if (k == "image_time") s.image_time = num();
        else if (k == "date") s.date = num();
        else if (k == "written") s.written = std::size_t(num());
        else if (k == "cars") s.cars = std::size_t(num());
        else if (k == "bytes") s.bytes = std::uint64_t(num());
    }
    return s;
}

bool remove(const fs::path& dir, std::string* why, const RemoveTree& remove_tree) {
    const RemoveTree rm = remove_tree ? remove_tree : RemoveTree(remove_tree_plain);   // modsec: b3mod::remove_tree from the launcher
    fs::path partial = dir;
    partial += ".partial";
    rm(partial);
    if (!rm(dir)) {
        if (why) *why = "could not delete " + b3io::u8string(dir) + " (a file in it is open?)";
        return false;
    }
    return true;
}

}  // namespace b3asset::xboximport
