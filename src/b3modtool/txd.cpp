// RenderWare PS2 rasters to PNG and back (docs/modding/b3modtool.md "txd").
//
// Writing a raster back: the raster's upload transfers are replayed into a GS memory model (as b3asset's decoder does),
// the new texel values (and CLUT, when it changes) are written into that memory at the addresses the texture is sampled
// from (TEX0 / MIPTBP / the CLUT layout), and then every byte of the upload's IMAGE data is read back from the memory
// at the place the transfer had put it. So the swizzled upload forms (PSMT8 sent as CT32, PSMT4 as CT16, palettes in
// the same transfer) are inverted by the same GS addressing that decodes them, and the file keeps its size and layout:
// only image bytes change. The result is decoded again and checked against what was meant to be written.
#include "b3modtool/modtool.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "b3asset/gs_memory.h"

using namespace b3asset;

namespace b3modtool {

namespace {

bool fail(std::string* why, std::string what) { if (why) *why = std::move(what); return false; }

struct Layout {
    int psm = 0, cpsm = 0;
    std::uint32_t tbp[7] = {}, tbw[7] = {};
    std::uint32_t cbp = 0, csa = 0;
    bool csm2 = false, indexed = false;
    int clut_n = 0;
};

Layout layout_of(const RasterHeader& h) {
    Layout L;
    L.psm = h.psm();
    L.cpsm = h.cpsm();
    L.indexed = L.psm == PSMT8 || L.psm == PSMT4 || L.psm == PSMT8H || L.psm == PSMT4HL || L.psm == PSMT4HH;
    L.clut_n = L.indexed ? ((L.psm == PSMT8 || L.psm == PSMT8H) ? 256 : 16) : 0;
    L.csa = (h.tex0 >> 56) & 0x1F;
    L.csm2 = (h.tex0 >> 55) & 1;
    L.cbp = h.pal_offset;
    L.tbp[0] = h.tex0 & 0x3FFF;
    L.tbw[0] = (h.tex0 >> 14) & 0x3F;
    for (int i = 0; i < 3; ++i) {
        L.tbp[1 + i] = (h.miptbp1 >> (20 * i)) & 0x3FFF;
        L.tbw[1 + i] = (h.miptbp1 >> (20 * i + 14)) & 0x3F;
        L.tbp[4 + i] = (h.miptbp2 >> (20 * i)) & 0x3FFF;
        L.tbw[4 + i] = (h.miptbp2 >> (20 * i + 14)) & 0x3F;
    }
    return L;
}

// where CLUT entry i lives (the decoder's rule: decode_raster_clut)
void clut_xy(const Layout& L, int i, std::uint32_t& bw, std::uint32_t& x, std::uint32_t& y) {
    if (L.csm2) { bw = 1 << 2; x = std::uint32_t(i); y = 0; return; }
    const std::uint32_t e = L.clut_n == 16 ? (L.csa & 0xF) * 16 + std::uint32_t(i) : std::uint32_t(i);
    const std::uint32_t j = (e & 0xE7) | ((e & 0x08) << 1) | ((e & 0x10) >> 1);
    bw = 1;
    x = j & 15;
    y = j >> 4;
}

// ---- the upload, walked as GsMemory replays it (raster.cpp run_gif, gs_memory.cpp transfer) ----
struct Pixel {
    int psm;
    std::uint32_t bp, bw, x, y;
    std::size_t pos[4];   // the file bytes that carry it (CT24 / CT16 / CT32: one per byte; 4-bit: pos[0], nibble)
    int nbytes;           // 0: a 4-bit pixel in the nibble `nibble` of pos[0]
    int nibble;
};
template <class F>
struct UploadWalk {
    Bytes f;
    F& on_pixel;
    std::uint32_t dbp = 0, dbw = 0, dpsm = 0, dsax = 0, dsay = 0, rrw = 0, rrh = 0, x = 0, y = 0;
    bool active = false;
    std::size_t pend_pos[4] = {};
    int pend_n = 0;
    int transfer_no = 0;

    void put(Pixel p) {
        p.psm = int(dpsm); p.bp = dbp; p.bw = dbw; p.x = dsax + x; p.y = dsay + y;
        on_pixel(p, transfer_no);
        if (++x == rrw) { x = 0; if (++y == rrh) active = false; }
    }
    void image(std::size_t at, std::size_t bytes) {
        const int bits = dpsm == PSMCT24 ? 24 : dpsm == PSMT8H ? 8 : (dpsm == PSMT4HL || dpsm == PSMT4HH) ? 4 : psm_bits(int(dpsm));
        for (std::size_t i = 0; active && i < bytes; ++i) {
            if (bits == 4) {
                Pixel p{}; p.pos[0] = at + i; p.nbytes = 0; p.nibble = 0;
                put(p);
                if (active) { p.nibble = 1; put(p); }
                continue;
            }
            pend_pos[pend_n++] = at + i;
            if (pend_n * 8 == bits) {
                Pixel p{};
                for (int k = 0; k < pend_n; ++k) p.pos[k] = pend_pos[k];
                p.nbytes = pend_n;
                pend_n = 0;
                put(p);
            }
        }
    }
    void gif(std::size_t at, std::size_t qwc) {
        std::size_t q = 0;
        while (q < qwc && f.has(at + q * 16, 16)) {
            const std::uint64_t lo = f.u64(at + q * 16), regs = f.u64(at + q * 16 + 8);
            ++q;
            const std::size_t nloop = lo & 0x7FFF;
            const int flg = int(lo >> 58) & 3;
            int nreg = int(lo >> 60) & 0xF;
            if (!nreg) nreg = 16;
            if (flg == 0) {
                for (std::size_t i = 0; i < nloop; ++i)
                    for (int r = 0; r < nreg && q < qwc; ++r, ++q) {
                        if (((regs >> (4 * r)) & 0xF) != 0xE) continue;
                        const std::uint64_t d = f.u64(at + q * 16);
                        switch (f.p[at + q * 16 + 8]) {
                        case 0x50: dbp = (d >> 32) & 0x3FFF; dbw = (d >> 48) & 0x3F; dpsm = (d >> 56) & 0x3F; break;
                        case 0x51: dsax = (d >> 32) & 0x7FF; dsay = (d >> 48) & 0x7FF; break;
                        case 0x52: rrw = d & 0xFFF; rrh = (d >> 32) & 0xFFF; break;
                        case 0x53:
                            if ((d & 3) == 0) { x = y = 0; pend_n = 0; active = rrw && rrh && psm_bits(int(dpsm)); ++transfer_no; }
                            break;
                        default: break;
                        }
                    }
            } else if (flg == 1) {
                q += (nloop * nreg + 1) / 2;
            } else {
                const std::size_t n = std::min(nloop, qwc - q);
                if (f.has(at + q * 16, n * 16)) image(at + q * 16, n * 16);
                q += n;
            }
        }
    }
    bool run(const RasterHeader& h, std::string* why) {
        const std::size_t up = h.offset + h.upload + 16;
        for (std::uint32_t k = 0; k < h.transfers; ++k) {
            const std::size_t e = up + 64 * k;
            const std::uint32_t cnt = f.u32(e), ref = f.u32(e + 48);
            if (((cnt >> 28) & 7) != 1 || ((ref >> 28) & 7) != 3) return fail(why, "upload chain is not DMAcnt + DMAref");
            gif(e + 16, cnt & 0xFFFF);
            const std::size_t src = h.offset + f.u32(e + 52);
            if (!f.has(src, std::size_t(ref & 0xFFFF) * 16)) return fail(why, "upload source outside the file");
            gif(src, ref & 0xFFFF);
        }
        return true;
    }
};

// the GS nibbles a stored pixel occupies (overlap check)
template <class F>
void pixel_nibbles(const Pixel& p, F&& fn) {
    const std::uint32_t a = GsMemory::address(p.psm, p.bp, p.bw, p.x, p.y);
    auto bytes = [&](std::uint32_t b, int n) { for (int k = 0; k < n; ++k) { fn((2 * (b + k)) & 0x7FFFFF); fn((2 * (b + k) + 1) & 0x7FFFFF); } };
    switch (p.psm) {
    case PSMCT32: bytes(a, 4); break;
    case PSMCT24: bytes(a, 3); break;
    case PSMCT16: case PSMCT16S: bytes(a, 2); break;
    case PSMT8: bytes(a, 1); break;
    case PSMT4: fn(a & 0x7FFFFF); break;
    case PSMT8H: bytes(a + 3, 1); break;
    case PSMT4HL: fn((2 * (a + 3)) & 0x7FFFFF); break;
    case PSMT4HH: fn((2 * (a + 3) + 1) & 0x7FFFFF); break;
    default: break;
    }
}

struct Context {
    GsMemory gs;
    std::vector<std::uint32_t> stamp = std::vector<std::uint32_t>(8u << 20, 0);   // per GS nibble: the last transfer's stamp
    std::uint32_t next = 1;
};
Context& ctx() { static Context c; return c; }

bool view_into(const Buf& file, std::size_t o, Context& c, RasterView& v, Layout& L, std::string* why) {
    v = RasterView{};
    Bytes f{file.data(), file.size()};
    if (!decode_raster(f, o, c.gs, v.dec, 0, why)) return false;
    const RasterHeader& h = v.dec.hdr;
    L = layout_of(h);
    v.indexed = L.indexed;
    for (std::uint32_t l = 0; l < h.levels; ++l) {
        const Image& im = v.dec.levels[l];
        std::vector<std::uint32_t> r(std::size_t(im.w) * im.h);
        for (int y = 0; y < im.h; ++y)
            for (int x = 0; x < im.w; ++x) {
                std::uint32_t val = c.gs.read(L.psm, L.tbp[l], L.tbw[l], std::uint32_t(x), std::uint32_t(y));
                if (L.indexed) val &= 0xFF;
                r[std::size_t(y) * im.w + x] = val;
            }
        v.raw.push_back(std::move(r));
    }
    for (int i = 0; i < L.clut_n; ++i) {
        std::uint32_t bw, x, y;
        clut_xy(L, i, bw, x, y);
        const std::uint32_t raw = c.gs.read(L.cpsm, L.cbp, bw, x, y);
        v.clut_raw.push_back(raw);
        v.clut_rgba.push_back(gs_color_to_rgba8(L.cpsm, raw, h.tcc()));
    }
    // two transfers writing one GS location: a write-back would give both the later bytes
    if (c.next > 0xFFFF0000u) { std::fill(c.stamp.begin(), c.stamp.end(), 0u); c.next = 1; }
    const std::uint32_t base = c.next;
    std::uint32_t cur = base;
    auto on = [&](const Pixel& p, int t) {
        cur = base + std::uint32_t(t);
        pixel_nibbles(p, [&](std::uint32_t n) {
            std::uint32_t& s = c.stamp[n];
            if (s >= base && s != cur && v.overlap.empty()) {
                char m[96];
                std::snprintf(m, sizeof m, "uploads %u and %u write the same GS memory", unsigned(s - base), unsigned(t));
                v.overlap = m;
            }
            s = cur;
        });
    };
    UploadWalk<decltype(on)> w{f, on};
    if (!w.run(h, why)) return false;
    c.next = base + std::uint32_t(w.transfer_no) + 1;
    return true;
}

std::uint32_t rgba_u32(const std::uint8_t* p) { return std::uint32_t(p[0]) | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24; }
std::uint32_t alpha_from8(std::uint32_t a8) { return std::min<std::uint32_t>(0x80, (a8 + 1) / 2); }

// a colour in storage format `psm` (CT32 / CT24 / CT16); `keep`: the stored value whose unused bits stay (no TCC)
std::uint32_t encode_color(int psm, std::uint32_t rgba, std::uint32_t keep, bool tcc) {
    const std::uint32_t r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF, a8 = rgba >> 24;
    switch (psm) {
    case PSMCT32: return r | g << 8 | b << 16 | (tcc ? alpha_from8(a8) : keep >> 24) << 24;
    case PSMCT24: return r | g << 8 | b << 16;
    default:   // CT16, CT16S
        return (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 | (tcc ? (a8 >= 0x80 ? 0x8000u : 0u) : (keep & 0x8000u));
    }
}

int dist2(std::uint32_t a, std::uint32_t b) {
    int d = 0;
    for (int k = 0; k < 32; k += 8) { const int e = int((a >> k) & 0xFF) - int((b >> k) & 0xFF); d += e * e; }
    return d;
}
int nearest(const std::vector<std::uint32_t>& pal, std::uint32_t c) {
    int best = 0, bd = -1;
    for (std::size_t i = 0; i < pal.size(); ++i) {
        const int d = dist2(pal[i], c);
        if (bd < 0 || d < bd) { bd = d; best = int(i); }
    }
    return best;
}

// median cut: at most n colours for the histogram `hist` (colour, count)
std::vector<std::uint32_t> median_cut(std::vector<std::pair<std::uint32_t, std::uint64_t>> hist, int n) {
    struct Box { std::size_t b, e; };
    std::vector<Box> boxes{{0, hist.size()}};
    auto range = [&](const Box& bx, int& ch) {
        int best = -1;
        for (int k = 0; k < 4; ++k) {
            std::uint32_t lo = 255, hi = 0;
            for (std::size_t i = bx.b; i < bx.e; ++i) { const std::uint32_t v = (hist[i].first >> (8 * k)) & 0xFF; lo = std::min(lo, v); hi = std::max(hi, v); }
            if (int(hi - lo) > best) { best = int(hi - lo); ch = k; }
        }
        return best;
    };
    while (int(boxes.size()) < n) {
        int pick = -1, pick_ch = 0, pick_r = 0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].e - boxes[i].b < 2) continue;
            int ch = 0;
            const int r = range(boxes[i], ch);
            if (r > pick_r) { pick = int(i); pick_r = r; pick_ch = ch; }
        }
        if (pick < 0) break;
        Box bx = boxes[std::size_t(pick)];
        std::sort(hist.begin() + std::ptrdiff_t(bx.b), hist.begin() + std::ptrdiff_t(bx.e), [&](const auto& a, const auto& b) {
            return ((a.first >> (8 * pick_ch)) & 0xFF) < ((b.first >> (8 * pick_ch)) & 0xFF);
        });
        std::uint64_t total = 0, acc = 0;
        for (std::size_t i = bx.b; i < bx.e; ++i) total += hist[i].second;
        std::size_t m = bx.b;
        while (m < bx.e - 1 && acc + hist[m].second <= total / 2) acc += hist[m++].second;
        if (m == bx.b) m = bx.b + 1;
        boxes[std::size_t(pick)] = {bx.b, m};
        boxes.push_back({m, bx.e});
    }
    std::vector<std::uint32_t> pal;
    for (const Box& bx : boxes) {
        std::uint64_t s[4] = {}, cnt = 0;
        for (std::size_t i = bx.b; i < bx.e; ++i) {
            for (int k = 0; k < 4; ++k) s[k] += ((hist[i].first >> (8 * k)) & 0xFF) * hist[i].second;
            cnt += hist[i].second;
        }
        std::uint32_t c = 0;
        for (int k = 0; k < 4; ++k) c |= std::uint32_t((s[k] + cnt / 2) / std::max<std::uint64_t>(1, cnt)) << (8 * k);
        pal.push_back(c);
    }
    return pal;
}

Image box_down(const Image& src, int w, int h) {
    Image o;
    o.w = w;
    o.h = h;
    o.rgba.resize(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < 4; ++k) {
                unsigned s = 0, n = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const int sx = std::min(src.w - 1, 2 * x + dx), sy = std::min(src.h - 1, 2 * y + dy);
                        s += src.rgba[(std::size_t(sy) * src.w + sx) * 4 + k];
                        ++n;
                    }
                o.rgba[(std::size_t(y) * w + x) * 4 + k] = std::uint8_t((s + n / 2) / n);
            }
    return o;
}

}  // namespace

bool view_raster(const Buf& file, std::size_t offset, RasterView& out, std::string* why) {
    Layout L;
    return view_into(file, offset, ctx(), out, L, why);
}

bool encode_raster(Buf& file, std::size_t o, const std::vector<Image>& levels, const EncodeOptions& opt, EncodeResult* res_out,
                   std::string* why) {
    EncodeResult res;
    Context& c = ctx();
    RasterView v;
    Layout L;
    if (!view_into(file, o, c, v, L, why)) return false;
    const RasterHeader& h = v.dec.hdr;
    const std::size_t nl = h.levels;
    // what each level should hold
    std::vector<Image> want(nl);
    std::vector<bool> edited(nl, false);
    for (std::size_t l = 0; l < nl; ++l) {
        const Image& st = v.dec.levels[l];
        if (l < levels.size() && levels[l].w) {
            if (levels[l].w != st.w || levels[l].h != st.h) {
                char m[160];
                std::snprintf(m, sizeof m, "%s level %zu is %dx%d; the image given is %dx%d (the size cannot change)", h.name.c_str(), l, st.w,
                              st.h, levels[l].w, levels[l].h);
                return fail(why, m);
            }
            want[l] = levels[l];
            edited[l] = want[l].rgba != st.rgba;
        } else {
            want[l] = st;
        }
    }
    if (edited[0] && !opt.keep_mips)
        for (std::size_t l = 1; l < nl; ++l)
            if (!edited[l]) {
                want[l] = box_down(want[l - 1], v.dec.levels[l].w, v.dec.levels[l].h);
                edited[l] = want[l].rgba != v.dec.levels[l].rgba;
                if (l == 1) res.notes.push_back(h.name + ": mip levels rebuilt from level 0");
            }
    const bool any = std::find(edited.begin(), edited.end(), true) != edited.end();
    if (!any && !opt.force_write && !opt.strict) { if (res_out) *res_out = res; return true; }
    if (!v.overlap.empty()) return fail(why, h.name + ": " + v.overlap + "; it cannot be written back (export only)");

    // the new stored values
    std::vector<std::vector<std::uint32_t>> raw(nl);
    std::vector<std::uint32_t> clut = v.clut_raw, clut_rgba = v.clut_rgba;
    bool clut_changed = false;
    if (!L.indexed) {
        for (std::size_t l = 0; l < nl; ++l) {
            raw[l].resize(v.raw[l].size());
            for (std::size_t i = 0; i < raw[l].size(); ++i) {
                const std::uint32_t t = rgba_u32(&want[l].rgba[4 * i]), s = rgba_u32(&v.dec.levels[l].rgba[4 * i]);
                raw[l][i] = (!opt.strict && t == s) ? v.raw[l][i] : encode_color(L.psm, t, v.raw[l][i], h.tcc());
            }
        }
    } else {
        std::unordered_map<std::uint32_t, int> exact;
        for (int i = int(clut_rgba.size()) - 1; i >= 0; --i) exact[clut_rgba[std::size_t(i)]] = i;   // the first entry of a colour
        std::size_t missing = 0;
        for (std::size_t l = 0; l < nl; ++l) {
            raw[l].assign(v.raw[l].size(), 0xFFFFFFFFu);
            for (std::size_t i = 0; i < raw[l].size(); ++i) {
                const std::uint32_t t = rgba_u32(&want[l].rgba[4 * i]), s = rgba_u32(&v.dec.levels[l].rgba[4 * i]);
                if (!opt.strict && t == s) { raw[l][i] = v.raw[l][i]; continue; }
                auto it = exact.find(t);
                if (it != exact.end()) raw[l][i] = std::uint32_t(it->second);
                else ++missing;
            }
        }
        if (missing) {
            if (opt.keep_palette || h.palettes > 1) {
                std::unordered_map<std::uint32_t, int> memo;
                for (std::size_t l = 0; l < nl; ++l)
                    for (std::size_t i = 0; i < raw[l].size(); ++i)
                        if (raw[l][i] == 0xFFFFFFFFu) {
                            const std::uint32_t t = rgba_u32(&want[l].rgba[4 * i]);
                            auto it = memo.find(t);
                            const int k = it != memo.end() ? it->second : (memo[t] = nearest(clut_rgba, t));
                            raw[l][i] = std::uint32_t(k);
                            ++res.nearest;
                        }
                if (h.palettes > 1 && !opt.keep_palette)
                    res.notes.push_back(h.name + ": has " + std::to_string(h.palettes) + " palettes, so its CLUT is kept (colours not in it: the nearest)");
            } else {
                // a new CLUT from level 0's colours (the mips, often box-filtered, take the nearest of them)
                std::unordered_map<std::uint32_t, std::uint64_t> count;
                std::vector<std::uint32_t> order;
                for (std::size_t l = 0; l < 1; ++l)
                    for (std::size_t i = 0; i < want[l].rgba.size() / 4; ++i) {
                        const std::uint32_t t = rgba_u32(&want[l].rgba[4 * i]);
                        if (!count[t]++) order.push_back(t);
                    }
                std::vector<std::uint32_t> pal;
                if (int(order.size()) <= L.clut_n) pal = order;
                else {
                    std::vector<std::pair<std::uint32_t, std::uint64_t>> hist;
                    for (std::uint32_t col : order) hist.push_back({col, count[col]});
                    pal = median_cut(std::move(hist), L.clut_n);
                    res.notes.push_back(h.name + ": " + std::to_string(order.size()) + " colours reduced to " + std::to_string(pal.size()));
                }
                // the palette's colours as the CLUT format stores them (and back, so the mapping sees what is drawn)
                clut.assign(std::size_t(L.clut_n), 0);
                clut_rgba.assign(std::size_t(L.clut_n), 0);
                for (std::size_t i = 0; i < pal.size(); ++i) {
                    clut[i] = encode_color(L.cpsm, pal[i], 0x80000000u, h.tcc());
                    clut_rgba[i] = gs_color_to_rgba8(L.cpsm, clut[i], h.tcc());
                }
                clut_changed = true;
                res.new_palette = true;
                std::vector<std::uint32_t> used(clut_rgba.begin(), clut_rgba.begin() + std::ptrdiff_t(std::max<std::size_t>(1, pal.size())));
                std::unordered_map<std::uint32_t, int> memo;
                for (std::size_t l = 0; l < nl; ++l)
                    for (std::size_t i = 0; i < raw[l].size(); ++i) {
                        const std::uint32_t t = rgba_u32(&want[l].rgba[4 * i]);
                        auto it = memo.find(t);
                        raw[l][i] = std::uint32_t(it != memo.end() ? it->second : (memo[t] = nearest(used, t)));
                    }
            }
        }
    }

    // into GS memory, then the upload's bytes back out of it
    for (std::size_t l = 0; l < nl; ++l) {
        const int w = v.dec.levels[l].w, hh = v.dec.levels[l].h;
        for (int y = 0; y < hh; ++y)
            for (int x = 0; x < w; ++x)
                c.gs.write(L.psm, L.tbp[l], L.tbw[l], std::uint32_t(x), std::uint32_t(y), raw[l][std::size_t(y) * w + x]);
    }
    if (clut_changed)
        for (int i = 0; i < L.clut_n; ++i) {
            std::uint32_t bw, x, y;
            clut_xy(L, i, bw, x, y);
            c.gs.write(L.cpsm, L.cbp, bw, x, y, clut[std::size_t(i)]);
        }
    Buf before;
    std::size_t changed_bytes = 0;
    auto put = [&](const Pixel& p, int) {
        const std::uint32_t val = c.gs.read(p.psm, p.bp, p.bw, p.x, p.y);
        if (!p.nbytes) {
            std::uint8_t& b = file[p.pos[0]];
            const std::uint8_t nb = p.nibble ? std::uint8_t((b & 0x0F) | (val & 0xF) << 4) : std::uint8_t((b & 0xF0) | (val & 0xF));
            changed_bytes += nb != b;
            b = nb;
            return;
        }
        for (int k = 0; k < p.nbytes; ++k) {
            const std::uint8_t nb = std::uint8_t(val >> (8 * k));
            changed_bytes += file[p.pos[k]] != nb;
            file[p.pos[k]] = nb;
        }
    };
    Bytes f{file.data(), file.size()};
    UploadWalk<decltype(put)> w{f, put};
    if (!w.run(h, why)) return false;

    // decode what was written: every texel and CLUT entry must be what was meant
    RasterView after;
    Layout L2;
    if (!view_into(file, o, c, after, L2, why)) return false;
    for (std::size_t l = 0; l < nl; ++l)
        if (after.raw[l] != raw[l]) return fail(why, h.name + ": level " + std::to_string(l) + " does not read back as written (the levels share GS memory)");
    if (L.indexed && after.clut_raw != clut) return fail(why, h.name + ": the CLUT does not read back as written");
    res.changed = changed_bytes > 0;
    if (res_out) *res_out = res;
    return true;
}

namespace {

// median cut over tuples of `dim` bytes (several paints' RGBA side by side): at most n boxes; `box_of` gives each
// tuple's box, the result each box's count-weighted mean
std::vector<std::vector<std::uint8_t>> median_cut_tuples(const std::vector<std::vector<std::uint8_t>>& t, const std::vector<std::uint64_t>& cnt,
                                                         std::size_t dim, int n, std::vector<int>& box_of) {
    std::vector<std::size_t> ord(t.size());
    for (std::size_t i = 0; i < ord.size(); ++i) ord[i] = i;
    struct Box { std::size_t b, e; };
    std::vector<Box> boxes{{0, ord.size()}};
    while (int(boxes.size()) < n) {
        int pick = -1, best = 0;
        std::size_t pick_ch = 0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].e - boxes[i].b < 2) continue;
            for (std::size_t ch = 0; ch < dim; ++ch) {
                int lo = 255, hi = 0;
                for (std::size_t k = boxes[i].b; k < boxes[i].e; ++k) { lo = std::min<int>(lo, t[ord[k]][ch]); hi = std::max<int>(hi, t[ord[k]][ch]); }
                if (hi - lo > best) { best = hi - lo; pick = int(i); pick_ch = ch; }
            }
        }
        if (pick < 0) break;
        const Box bx = boxes[std::size_t(pick)];
        std::sort(ord.begin() + std::ptrdiff_t(bx.b), ord.begin() + std::ptrdiff_t(bx.e),
                  [&](std::size_t a, std::size_t b) { return t[a][pick_ch] < t[b][pick_ch]; });
        std::uint64_t total = 0, acc = 0;
        for (std::size_t k = bx.b; k < bx.e; ++k) total += cnt[ord[k]];
        std::size_t m = bx.b;
        while (m < bx.e - 1 && acc + cnt[ord[m]] <= total / 2) acc += cnt[ord[m++]];
        if (m == bx.b) m = bx.b + 1;
        boxes[std::size_t(pick)] = {bx.b, m};
        boxes.push_back({m, bx.e});
    }
    std::vector<std::vector<std::uint8_t>> pal;
    box_of.assign(t.size(), 0);
    for (std::size_t bi = 0; bi < boxes.size(); ++bi) {
        std::vector<std::uint64_t> s(dim, 0);
        std::uint64_t c = 0;
        for (std::size_t k = boxes[bi].b; k < boxes[bi].e; ++k) {
            const std::size_t i = ord[k];
            for (std::size_t ch = 0; ch < dim; ++ch) s[ch] += std::uint64_t(t[i][ch]) * cnt[i];
            c += cnt[i];
            box_of[i] = int(bi);
        }
        std::vector<std::uint8_t> m(dim);
        for (std::size_t ch = 0; ch < dim; ++ch) m[ch] = std::uint8_t((s[ch] + c / 2) / std::max<std::uint64_t>(1, c));
        pal.push_back(std::move(m));
    }
    return pal;
}

}  // namespace

bool encode_raster_paints(Buf& file, std::size_t o, const std::vector<Image>& paints, PaintEncodeResult* res_out, std::string* why) {
    PaintEncodeResult res;
    const Bytes f0{file.data(), file.size()};
    RasterHeader h;
    if (!read_raster_header(f0, o, h, why)) return false;
    const int np = std::max(1, int(h.palettes));
    if (int(paints.size()) > np) return fail(why, h.name + ": " + std::to_string(paints.size()) + " pictures for " + std::to_string(np) + " paints");
    // the pictures as the export wrote them
    std::vector<DecodedRaster> dec(static_cast<std::size_t>(np));
    {
        GsMemory g;
        for (int k = 0; k < np; ++k)
            if (!decode_raster(f0, o, g, dec[std::size_t(k)], k, why)) return false;
    }
    Context& c = ctx();
    RasterView v;
    Layout L;
    if (!view_into(file, o, c, v, L, why)) return false;
    if (!L.indexed) return fail(why, h.name + ": not an indexed raster (the paint pictures need CLUTs)");
    const std::size_t nl = h.levels;
    const int W = dec[0].levels[0].w, H = dec[0].levels[0].h;
    const std::size_t npx = std::size_t(W) * std::size_t(H);
    std::vector<Image> want(static_cast<std::size_t>(np));
    std::vector<bool> edited(std::size_t(np), false);
    for (int k = 0; k < np; ++k) {
        const Image& st = dec[std::size_t(k)].levels[0];
        if (std::size_t(k) < paints.size() && paints[std::size_t(k)].w) {
            const Image& p = paints[std::size_t(k)];
            if (p.w != W || p.h != H || p.rgba.size() != npx * 4) {
                char m[160];
                std::snprintf(m, sizeof m, "%s paint %d is %dx%d; the picture given is %dx%d (the size cannot change)", h.name.c_str(), k, W, H, p.w, p.h);
                return fail(why, m);
            }
            want[std::size_t(k)] = p;
            bool opaque = true, stored_opaque = true;
            for (std::size_t i = 0; i < npx; ++i) { opaque = opaque && p.rgba[4 * i + 3] == 255; stored_opaque = stored_opaque && st.rgba[4 * i + 3] == 255; }
            if (opaque && !stored_opaque) {
                for (std::size_t i = 0; i < npx; ++i) want[std::size_t(k)].rgba[4 * i + 3] = st.rgba[4 * i + 3];
                res.notes.push_back("paint " + std::to_string(k) + ": the picture has no alpha; the stored alpha (a mask, not coverage) is kept");
            }
            edited[std::size_t(k)] = want[std::size_t(k)].rgba != st.rgba;
        } else {
            want[std::size_t(k)] = st;
        }
        res.changed += edited[std::size_t(k)];
    }
    if (!res.changed) { if (res_out) *res_out = res; return true; }
    if (!v.overlap.empty()) return fail(why, h.name + ": " + v.overlap + "; it cannot be written back");

    // every paint's CLUT as stored
    std::vector<std::uint32_t> cbp(static_cast<std::size_t>(np));
    std::vector<std::vector<std::uint32_t>> clut(static_cast<std::size_t>(np)), clut_rgba(static_cast<std::size_t>(np));
    for (int k = 0; k < np; ++k) {
        cbp[std::size_t(k)] = L.cbp + palette_cbp_add(f0, o, k);
        for (int i = 0; i < L.clut_n; ++i) {
            std::uint32_t bw, x, y;
            clut_xy(L, i, bw, x, y);
            const std::uint32_t raw = c.gs.read(L.cpsm, cbp[std::size_t(k)], bw, x, y);
            clut[std::size_t(k)].push_back(raw);
            clut_rgba[std::size_t(k)].push_back(gs_color_to_rgba8(L.cpsm, raw, h.tcc()));
        }
    }
    std::vector<std::vector<std::uint32_t>> raw = v.raw;
    std::vector<std::vector<std::uint32_t>> nclut = clut;
    // a recolour: each stored index of level 0 shows one colour in the edited picture
    bool recolour = true;
    for (int k = 0; k < np && recolour; ++k) {
        if (!edited[std::size_t(k)]) continue;
        std::vector<std::int64_t> col(std::size_t(L.clut_n), -1);
        for (std::size_t i = 0; i < npx && recolour; ++i) {
            const std::uint32_t t = rgba_u32(&want[std::size_t(k)].rgba[4 * i]);
            std::int64_t& cc = col[raw[0][i] & std::uint32_t(L.clut_n - 1)];
            if (cc < 0) cc = t;
            else if (std::uint32_t(cc) != t) recolour = false;
        }
        for (int i = 0; recolour && i < L.clut_n; ++i)
            if (col[std::size_t(i)] >= 0 && std::uint32_t(col[std::size_t(i)]) != clut_rgba[std::size_t(k)][std::size_t(i)])
                nclut[std::size_t(k)][std::size_t(i)] = encode_color(L.cpsm, std::uint32_t(col[std::size_t(i)]), clut[std::size_t(k)][std::size_t(i)], h.tcc());
    }
    // two paints on one CLUT must agree
    for (int a = 0; a < np && recolour; ++a)
        for (int b = a + 1; b < np; ++b)
            if (cbp[std::size_t(a)] == cbp[std::size_t(b)] && nclut[std::size_t(a)] != nclut[std::size_t(b)])
                return fail(why, h.name + ": paints " + std::to_string(a) + " and " + std::to_string(b) + " share one CLUT in the file; edit them alike");
    if (recolour) {
        res.notes.push_back(h.name + ": a recolour (each palette entry one colour): only the edited paints' CLUTs change");
    } else {
        // re-quantise: each texel's colours in every paint, side by side
        res.new_indices = true;
        const std::size_t dim = 4 * std::size_t(np);
        std::unordered_map<std::string, std::size_t> id;
        std::vector<std::vector<std::uint8_t>> tup;
        std::vector<std::uint64_t> cnt;
        std::vector<std::size_t> px_tuple(npx);
        std::string key(dim, '\0');
        for (std::size_t i = 0; i < npx; ++i) {
            for (int k = 0; k < np; ++k)
                for (int ch = 0; ch < 4; ++ch) key[std::size_t(k) * 4 + std::size_t(ch)] = char(want[std::size_t(k)].rgba[4 * i + std::size_t(ch)]);
            auto it = id.find(key);
            if (it == id.end()) {
                it = id.emplace(key, tup.size()).first;
                tup.emplace_back(key.begin(), key.end());
                cnt.push_back(0);
            }
            ++cnt[it->second];
            px_tuple[i] = it->second;
        }
        std::vector<int> box_of;
        std::vector<std::vector<std::uint8_t>> pal;
        if (int(tup.size()) <= L.clut_n) {
            pal = tup;
            box_of.resize(tup.size());
            for (std::size_t i = 0; i < tup.size(); ++i) box_of[i] = int(i);
        } else {
            pal = median_cut_tuples(tup, cnt, dim, L.clut_n, box_of);
            res.notes.push_back(h.name + ": " + std::to_string(tup.size()) + " colour combinations over " + std::to_string(np) +
                                " paints reduced to " + std::to_string(pal.size()));
        }
        for (int k = 0; k < np; ++k) {
            nclut[std::size_t(k)].assign(std::size_t(L.clut_n), 0);
            for (std::size_t i = 0; i < pal.size(); ++i) {
                const std::uint8_t* p = &pal[i][4 * std::size_t(k)];
                nclut[std::size_t(k)][i] = encode_color(L.cpsm, rgba_u32(p), 0x80000000u, h.tcc());
            }
        }
        for (std::size_t i = 0; i < npx; ++i) raw[0][i] = std::uint32_t(box_of[px_tuple[i]]);
        // the mip levels: each paint box-filtered from the level above, then the entry nearest in every paint at once
        std::vector<Image> prev = want;
        for (std::size_t l = 1; l < nl; ++l) {
            const int w = v.dec.levels[l].w, hh = v.dec.levels[l].h;
            std::vector<Image> cur;
            for (int k = 0; k < np; ++k) cur.push_back(box_down(prev[std::size_t(k)], w, hh));
            std::unordered_map<std::string, std::uint32_t> memo;
            for (std::size_t i = 0; i < std::size_t(w) * std::size_t(hh); ++i) {
                for (int k = 0; k < np; ++k)
                    for (int ch = 0; ch < 4; ++ch) key[std::size_t(k) * 4 + std::size_t(ch)] = char(cur[std::size_t(k)].rgba[4 * i + std::size_t(ch)]);
                auto it = memo.find(key);
                if (it == memo.end()) {
                    std::uint32_t best = 0;
                    std::int64_t bd = -1;
                    for (std::size_t e = 0; e < pal.size(); ++e) {
                        std::int64_t d = 0;
                        for (std::size_t ch = 0; ch < dim; ++ch) { const int x = int(pal[e][ch]) - int(std::uint8_t(key[ch])); d += x * x; }
                        if (bd < 0 || d < bd) { bd = d; best = std::uint32_t(e); }
                    }
                    it = memo.emplace(key, best).first;
                }
                raw[l][i] = it->second;
            }
            prev = std::move(cur);
        }
        if (nl > 1) res.notes.push_back(h.name + ": mip levels rebuilt from level 0");
    }

    // into GS memory, then the upload's bytes back out of it
    for (std::size_t l = 0; l < nl; ++l) {
        const int w = v.dec.levels[l].w, hh = v.dec.levels[l].h;
        for (int y = 0; y < hh; ++y)
            for (int x = 0; x < w; ++x)
                c.gs.write(L.psm, L.tbp[l], L.tbw[l], std::uint32_t(x), std::uint32_t(y), raw[l][std::size_t(y) * std::size_t(w) + std::size_t(x)]);
    }
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < L.clut_n; ++i) {
            std::uint32_t bw, x, y;
            clut_xy(L, i, bw, x, y);
            c.gs.write(L.cpsm, cbp[std::size_t(k)], bw, x, y, nclut[std::size_t(k)][std::size_t(i)]);
        }
    auto put = [&](const Pixel& p, int) {
        const std::uint32_t val = c.gs.read(p.psm, p.bp, p.bw, p.x, p.y);
        if (!p.nbytes) {
            std::uint8_t& b = file[p.pos[0]];
            b = p.nibble ? std::uint8_t((b & 0x0F) | (val & 0xF) << 4) : std::uint8_t((b & 0xF0) | (val & 0xF));
            return;
        }
        for (int k = 0; k < p.nbytes; ++k) file[p.pos[k]] = std::uint8_t(val >> (8 * k));
    };
    Bytes f{file.data(), file.size()};
    UploadWalk<decltype(put)> walk{f, put};
    if (!walk.run(h, why)) return false;
    // decode what was written: every texel and every paint's CLUT must be what was meant
    RasterView after;
    Layout L2;
    if (!view_into(file, o, c, after, L2, why)) return false;
    for (std::size_t l = 0; l < nl; ++l)
        if (after.raw[l] != raw[l]) return fail(why, h.name + ": level " + std::to_string(l) + " does not read back as written");
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < L.clut_n; ++i) {
            std::uint32_t bw, x, y;
            clut_xy(L, i, bw, x, y);
            if (c.gs.read(L.cpsm, cbp[std::size_t(k)], bw, x, y) != nclut[std::size_t(k)][std::size_t(i)])
                return fail(why, h.name + ": paint " + std::to_string(k) + "'s CLUT does not read back as written");
        }
    if (res_out) *res_out = res;
    return true;
}

bool txd_offsets(const Buf& file, std::vector<std::size_t>& out, std::string* why) {
    out.clear();
    std::vector<TxdEntry> es;
    if (!read_txd(Bytes{file.data(), file.size()}, es, why)) return false;
    for (const TxdEntry& e : es) out.push_back(e.offset);
    return true;
}

std::string raster_file_stem(std::size_t index, const std::string& name) {
    char num[16];
    std::snprintf(num, sizeof num, "%03zu", index);
    std::string n;
    for (char ch : name) n += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') ? ch : '_';
    if (n.empty()) n = "raster";
    return std::string(num) + "_" + n;
}

bool txd_export(const Buf& txd, const std::string& source, FileSet& out, std::string* why) {
    std::vector<std::size_t> offs;
    if (!txd_offsets(txd, offs, why)) return false;
    std::string index = "# b3modtool txd export: " + source + ", " + std::to_string(offs.size()) + " rasters\n"
                        "# <file stem> <width>x<height> <format> levels <n> [clut <format>] [palettes <n>] offset <hex>\n"
                        "# Edit the PNGs (same size), then: b3modtool txd import <the original TXD> <this folder> <new TXD>\n";
    for (std::size_t i = 0; i < offs.size(); ++i) {
        RasterView v;
        std::string e;
        if (!view_raster(txd, offs[i], v, &e)) { index += "# " + std::to_string(i) + ": not decoded (" + e + ")\n"; continue; }
        const RasterHeader& h = v.dec.hdr;
        const std::string stem = raster_file_stem(i, h.name);
        for (std::size_t l = 0; l < v.dec.levels.size(); ++l) {
            const Image& im = v.dec.levels[l];
            out[stem + (l ? ".mip" + std::to_string(l) : std::string()) + ".png"] = encode_png(im.w, im.h, im.rgba.data());
        }
        char line[256];
        std::snprintf(line, sizeof line, "%s %ux%u %s levels %u", stem.c_str(), h.width, h.height, psm_name(h.psm()), h.levels);
        index += line;
        if (v.indexed) index += std::string(" clut ") + psm_name(h.cpsm());
        if (h.palettes > 1) index += " palettes " + std::to_string(h.palettes);
        std::snprintf(line, sizeof line, " offset 0x%zx%s\n", offs[i], v.overlap.empty() ? "" : " (export only: its uploads overlap)");
        index += line;
    }
    out["txd.txt"] = Buf(index.begin(), index.end());
    return true;
}

bool txd_import(const Buf& txd, const FileSet& in, const EncodeOptions& opt, Buf& out, ImportReport* rep, std::string* why) {
    ImportReport r;
    std::vector<std::size_t> offs;
    if (!txd_offsets(txd, offs, why)) return false;
    // "<index>_<anything>[.mip<k>].png" -> (index, level)
    std::map<std::size_t, std::map<std::size_t, const Buf*>> files;
    for (const auto& [name, bytes] : in) {
        const std::string up = to_upper_ascii(name);
        if (up == "TXD.TXT") continue;
        std::size_t i = 0, k = 0;
        while (k < name.size() && std::isdigit(static_cast<unsigned char>(name[k]))) i = i * 10 + std::size_t(name[k++] - '0');
        if (!k || k >= name.size() || name[k] != '_' || up.size() < 4 || up.substr(up.size() - 4) != ".PNG") {
            r.notes.push_back(name + ": not a raster PNG (<index>_<name>.png), ignored");
            continue;
        }
        std::size_t level = 0;
        const std::string stem = up.substr(0, up.size() - 4);
        const std::size_t m = stem.rfind(".MIP");
        if (m != std::string::npos && m + 4 < stem.size() && std::all_of(stem.begin() + std::ptrdiff_t(m + 4), stem.end(), ::isdigit))
            level = std::size_t(std::stoul(stem.substr(m + 4)));
        if (i >= offs.size()) { r.notes.push_back(name + ": the dictionary has no raster " + std::to_string(i) + ", ignored"); continue; }
        if (files[i].count(level)) return fail(why, name + ": a second file for raster " + std::to_string(i) + " level " + std::to_string(level));
        files[i][level] = &bytes;
    }
    out = txd;
    for (const auto& [i, lv] : files) {
        std::vector<Image> levels;
        for (const auto& [l, bytes] : lv) {
            if (levels.size() <= l) levels.resize(l + 1);
            std::string e;
            if (!decode_png(*bytes, levels[l].w, levels[l].h, levels[l].rgba, &e))
                return fail(why, raster_file_stem(i, "") + "* level " + std::to_string(l) + ": " + e);
        }
        EncodeResult er;
        std::string e;
        if (!encode_raster(out, offs[i], levels, opt, &er, &e)) return fail(why, "raster " + std::to_string(i) + ": " + e);
        ++r.rasters;
        r.changed += er.changed;
        r.new_palettes += er.new_palette;
        r.nearest += er.nearest;
        for (auto& n : er.notes) r.notes.push_back(std::move(n));
    }
    if (rep) *rep = std::move(r);
    return true;
}

}  // namespace b3modtool
