#include "b3asset/raster.h"

#include "b3asset/hdpack.h"

#include <algorithm>
#include <cstring>

namespace b3asset {

std::uint32_t Bytes::u32(std::size_t off) const { std::uint32_t v = 0; if (has(off, 4)) std::memcpy(&v, p + off, 4); return v; }
std::uint64_t Bytes::u64(std::size_t off) const { std::uint64_t v = 0; if (has(off, 8)) std::memcpy(&v, p + off, 8); return v; }
std::uint16_t Bytes::u16(std::size_t off) const { std::uint16_t v = 0; if (has(off, 2)) std::memcpy(&v, p + off, 2); return v; }
float Bytes::f32(std::size_t off) const { float v = 0; if (has(off, 4)) std::memcpy(&v, p + off, 4); return v; }

namespace {
bool fail(std::string* why, const char* what) { if (why) *why = what; return false; }
int log2_ceil(std::uint32_t v) { int l = 0; while ((1u << l) < v) ++l; return l; }
}  // namespace

bool read_raster_header(Bytes f, std::size_t o, RasterHeader& h, std::string* why) {
    if (!f.has(o, 0xC8)) return fail(why, "raster header outside the file");
    h = RasterHeader{};
    h.offset = o;
    h.width = f.u32(o + 0x0C);
    h.height = f.u32(o + 0x10);
    h.depth = f.u32(o + 0x14);
    h.ctype = f.p[o + 0x20];
    h.cformat = f.p[o + 0x23];
    h.tex0 = f.u64(o + 0x3C);
    h.pal_offset = f.u32(o + 0x44);
    h.levels = (f.p[o + 0x4A] >> 2) + 1;
    h.miptbp1 = f.u64(o + 0x4C);
    h.miptbp2 = f.u64(o + 0x54);
    h.flags = f.p[o + 0x6A];
    h.palettes = f.p[o + 0xA0];
    h.upload = f.u32(o + 0x24);
    for (std::size_t i = 0; i < 32 && f.p[o + 0xA8 + i]; ++i) {
        const char c = char(f.p[o + 0xA8 + i]);
        if (c < 0x20 || c > 0x7E) { h.name.clear(); break; }
        h.name += c;
    }
    if (h.ctype != 4) return fail(why, "cType is not 4 (texture)");
    if (!h.width || !h.height || h.width > 2048 || h.height > 2048) return fail(why, "bad raster size");
    const int psm = h.psm();
    if (!psm_bits(psm)) return fail(why, "unknown TEX0 PSM");
    const int tw = int(h.tex0 >> 26) & 0xF, th = int(h.tex0 >> 30) & 0xF;
    if (tw != log2_ceil(h.width) || th != log2_ceil(h.height)) return fail(why, "TEX0 TW/TH disagree with the raster size");
    if (h.levels > 7) return fail(why, "more than 7 mip levels");
    const std::size_t up = o + h.upload;
    if (!h.upload || !f.has(up, 16)) return fail(why, "no upload block");
    // The first count: when the palette rides in the last level's transfer, the second count is one higher and its
    // extra chain entry holds a stale pointer [C GLOBAL.TXD "ramp"; librw rasterCreateTexture's numTransfers].
    h.transfers = f.u32(up);
    if (!h.transfers || h.transfers > 16 || !f.has(up + 16, 64 * std::size_t(h.transfers)))
        return fail(why, "bad upload transfer count");
    return true;
}

void run_gif(Bytes f, std::size_t at, std::size_t qwc, GsMemory& gs) {
    std::size_t q = 0;
    while (q < qwc && f.has(at + q * 16, 16)) {
        const std::uint64_t lo = f.u64(at + q * 16), regs = f.u64(at + q * 16 + 8);
        ++q;
        const std::size_t nloop = lo & 0x7FFF;
        const int flg = int(lo >> 58) & 3;
        int nreg = int(lo >> 60) & 0xF;
        if (!nreg) nreg = 16;
        if (flg == 0) {  // PACKED
            for (std::size_t i = 0; i < nloop; ++i)
                for (int r = 0; r < nreg && q < qwc; ++r, ++q) {
                    if (((regs >> (4 * r)) & 0xF) != 0xE) continue;  // only A+D matters for transfers
                    const std::uint64_t data = f.u64(at + q * 16);
                    switch (f.p[at + q * 16 + 8]) {
                    case 0x50: gs.set_bitbltbuf(data); break;
                    case 0x51: gs.set_trxpos(data); break;
                    case 0x52: gs.set_trxreg(data); break;
                    case 0x53: if ((data & 3) == 0) gs.start_transfer(); break;
                    default: break;
                    }
                }
        } else if (flg == 1) {  // REGLIST: 64-bit words, padded to a quadword
            q += (nloop * nreg + 1) / 2;
        } else {  // IMAGE (3 = disabled, treated as IMAGE by the GS)
            const std::size_t n = std::min(nloop, qwc - q);
            if (f.has(at + q * 16, n * 16)) gs.transfer(f.p + at + q * 16, n * 16);
            q += n;
        }
    }
}

std::uint32_t gs_color_to_rgba8(int psm, std::uint32_t v, bool tcc) {
    std::uint32_t r, g, b, a;
    if (psm == PSMCT16 || psm == PSMCT16S) {
        r = (v & 0x1F) << 3; g = ((v >> 5) & 0x1F) << 3; b = ((v >> 10) & 0x1F) << 3;
        r |= r >> 5; g |= g >> 5; b |= b >> 5;
        a = (v & 0x8000) ? 0x80 : 0x00;  // TEXA TA1 = 0x80, TA0 = 0 [L: RW sky2 defaults]
    } else {
        r = v & 0xFF; g = (v >> 8) & 0xFF; b = (v >> 16) & 0xFF;
        a = psm == PSMCT24 ? 0x80 : v >> 24;
    }
    if (!tcc) a = 0x80;
    const std::uint32_t a8 = a >= 0x80 ? 0xFF : a * 2;
    return r | g << 8 | b << 16 | a8 << 24;
}

std::uint32_t palette_cbp_add(Bytes f, std::size_t o, int palette) {
    if (palette <= 0 || !f.has(o + 0x90, 0x11) || palette >= f.p[o + 0xA0] || palette >= 8) return 0;
    return f.u16(o + 0x90 + 2 * std::size_t(palette));
}

bool decode_raster(Bytes f, std::size_t o, GsMemory& gs, DecodedRaster& out, int palette, std::string* why) {
    return decode_raster_clut(f, o, gs, out, palette_cbp_add(f, o, palette), why);
}

bool decode_raster_clut(Bytes f, std::size_t o, GsMemory& gs, DecodedRaster& out, std::uint32_t cbp_add, std::string* why) {
    out = DecodedRaster{};
    if (!read_raster_header(f, o, out.hdr, why)) return false;
    const RasterHeader& h = out.hdr;
    gs.clear();
    const std::size_t up = o + h.upload + 16;
    for (std::uint32_t k = 0; k < h.transfers; ++k) {
        const std::size_t e = up + 64 * k;
        const std::uint32_t cnt = f.u32(e), ref = f.u32(e + 48);
        if (((cnt >> 28) & 7) != 1 || ((ref >> 28) & 7) != 3) return fail(why, "upload chain is not DMAcnt + DMAref");
        run_gif(f, e + 16, cnt & 0xFFFF, gs);
        const std::size_t src = o + f.u32(e + 52);
        if (!f.has(src, std::size_t(ref & 0xFFFF) * 16)) return fail(why, "upload source outside the file");
        run_gif(f, src, ref & 0xFFFF, gs);
    }

    const int psm = h.psm();
    const bool indexed = psm == PSMT8 || psm == PSMT4 || psm == PSMT8H || psm == PSMT4HL || psm == PSMT4HH;
    const int cpsm = h.cpsm();
    const std::uint32_t csa = (h.tex0 >> 56) & 0x1F;
    const bool csm2 = (h.tex0 >> 55) & 1;
    const std::uint32_t cbp = h.pal_offset + cbp_add;
    std::uint32_t clut[256] = {};
    std::uint8_t clut_alpha[256] = {};
    if (indexed) {
        const int n = (psm == PSMT8 || psm == PSMT8H) ? 256 : 16;
        for (int i = 0; i < n; ++i) {
            std::uint32_t v;
            if (csm2) {  // CSM2: a linear row (TEXCLUT offsets assumed 0)
                v = gs.read(cpsm, cbp, 1 << 2, i, 0);
            } else {
                // CSM1: 16x16 (8-bit) or 8x2 (4-bit, CSA picks the 16-entry group) with index bits 3 and 4 swapped.
                const std::uint32_t e = n == 16 ? (csa & 0xF) * 16 + i : i;
                const std::uint32_t j = (e & 0xE7) | ((e & 0x08) << 1) | ((e & 0x10) >> 1);
                v = gs.read(cpsm, cbp, 1, j & 15, j >> 4);
            }
            clut[i] = gs_color_to_rgba8(cpsm, v, h.tcc());
            clut_alpha[i] = (cpsm == PSMCT16 || cpsm == PSMCT16S) ? ((v & 0x8000) ? 0x80 : 0) : std::uint8_t(v >> 24);
        }
    }

    std::uint32_t tbp[7], tbw[7];
    tbp[0] = h.tex0 & 0x3FFF;
    tbw[0] = (h.tex0 >> 14) & 0x3F;
    for (int i = 0; i < 3; ++i) {
        tbp[1 + i] = (h.miptbp1 >> (20 * i)) & 0x3FFF;
        tbw[1 + i] = (h.miptbp1 >> (20 * i + 14)) & 0x3F;
        tbp[4 + i] = (h.miptbp2 >> (20 * i)) & 0x3FFF;
        tbw[4 + i] = (h.miptbp2 >> (20 * i + 14)) & 0x3F;
    }
    for (std::uint32_t l = 0; l < h.levels; ++l) {
        Image img;
        img.w = int(std::max(1u, h.width >> l));
        img.h = int(std::max(1u, h.height >> l));
        img.rgba.resize(std::size_t(img.w) * img.h * 4);
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x) {
                const std::uint32_t v = gs.read(psm, tbp[l], tbw[l], x, y);
                std::uint32_t c;
                std::uint8_t raw_a;
                if (indexed) {
                    c = clut[v & 0xFF];
                    raw_a = clut_alpha[v & 0xFF];
                } else {
                    c = gs_color_to_rgba8(psm, v, h.tcc());
                    raw_a = psm == PSMCT32 ? std::uint8_t(v >> 24) : psm == PSMCT24 ? 0x80 : ((v & 0x8000) ? 0x80 : 0);
                }
                if (!h.tcc()) raw_a = 0x80;
                if (l == 0) {
                    out.max_alpha = std::max(out.max_alpha, raw_a);
                    if (raw_a < 0x80) out.translucent = true;
                }
                std::memcpy(&img.rgba[(std::size_t(y) * img.w + x) * 4], &c, 4);
            }
        out.levels.push_back(std::move(img));
    }
    if (HdTextures* hd = hd_textures()) hd->apply(gs, out, cbp_add);
    return true;
}

bool read_txd(Bytes f, std::vector<TxdEntry>& out, std::string* why) {
    out.clear();
    if (f.u32(0) != 0x543C0000u || f.u32(4) != 0xBCDEED81u) return fail(why, "not a texture dictionary");
    const std::uint32_t n = f.u32(8), dir = f.u32(12);
    if (!f.has(dir, std::size_t(n) * 16)) return fail(why, "directory outside the file");
    for (std::uint32_t i = 0; i < n; ++i) {
        TxdEntry e;
        e.key = f.u32(dir + 16 * i);
        e.offset = f.u32(dir + 16 * i + 8);
        out.push_back(e);
    }
    return true;
}

}  // namespace b3asset
