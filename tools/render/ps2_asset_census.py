#!/usr/bin/env python3
"""Census of the PS2-native render data in Burnout 3's disc files (native-renderer study, docs/native-renderer.md).

Reads game files (never writes them, never copies them into the repo) and prints statistics only:

  textures  serialized RenderWare PS2 rasters (RwRaster + sky2 raster extension, offsets in place of pointers),
            found by signature: cType 4 (texture) at +0x20, a valid cFormat byte at +0x23, power-of-two width and
            height at +0x0C/+0x10, and a GS TEX0 at +0x3C whose PSM / TW / TH agree with them.
  vif       VIF UNPACK commands that follow an STCYCL (the shape of pre-instanced geometry packets), by format.

Usage: python3 -I tools/render/ps2_asset_census.py <file-or-dir> ...   (run with -I: the inputs are game data)
"""
import collections
import os
import re
import struct
import sys

P2 = rb"(?:[\x01\x02\x04\x08\x10\x20\x40\x80]\x00|\x00[\x01\x02\x04])\x00\x00"
RASTER_DIMS = re.compile(P2 + P2 + rb"[\x04\x08\x10\x20]\x00\x00\x00", re.S)
STCYCL_UNPACK = re.compile(rb"[\x01-\xff][\x01-\xff].\x01...[\x60-\x7f]", re.S)

PSM = {0x00: "CT32", 0x01: "CT24", 0x02: "CT16", 0x0A: "CT16S", 0x13: "T8", 0x14: "T4",
       0x1B: "T8H", 0x24: "T4HL", 0x2C: "T4HH"}
# RwRaster cFormat byte (format >> 8): pixel format in the low nibble, PAL4/PAL8/MIPMAP/AUTOMIPMAP flags above.
FMT_PIX = {1: "1555", 2: "565", 3: "4444", 4: "LUM8", 5: "8888", 6: "888", 0xA: "555"}
VIF_UNPACK = {0x0: "S-32", 0x1: "S-16", 0x2: "S-8", 0x4: "V2-32", 0x5: "V2-16", 0x6: "V2-8",
              0x8: "V3-32", 0x9: "V3-16", 0xA: "V3-8", 0xC: "V4-32", 0xD: "V4-16", 0xE: "V4-8", 0xF: "V4-5"}


def log2(v):
    return v.bit_length() - 1 if v and not v & (v - 1) else -1


def scan_textures(d):
    out = []
    for m in RASTER_DIMS.finditer(d):
        off = m.start() - 0x0C
        if off < 0 or off % 4 or off + 0x58 > len(d) or d[off + 0x20] != 4:
            continue
        w, h, depth = struct.unpack_from("<3I", d, off + 0x0C)
        lw, lh = log2(w), log2(h)
        if not (0 <= lw <= 10 and 0 <= lh <= 10 and depth in (4, 8, 16, 32)):
            continue
        cfmt = d[off + 0x23]
        tex0 = struct.unpack_from("<Q", d, off + 0x3C)[0]
        psm = (tex0 >> 20) & 0x3F
        tw, th = (tex0 >> 26) & 0xF, (tex0 >> 30) & 0xF
        if psm not in PSM or tw != lw or th != lh:
            continue
        pal = "PAL4" if cfmt & 0x40 else "PAL8" if cfmt & 0x20 else "-"
        mip = bool(cfmt & 0x80)
        mxl = d[off + 0x34 + 0x16] >> 2
        out.append((off, w, h, depth, PSM[psm], pal, FMT_PIX.get(cfmt & 0xF, hex(cfmt & 0xF)), mip, mxl,
                    (tex0 >> 34) & 1, (tex0 >> 35) & 3))
    return out


def scan_vif(d):
    c = collections.Counter()
    for m in STCYCL_UNPACK.finditer(d):
        off = m.start()
        if off % 4:
            continue
        w0, w1 = struct.unpack_from("<2I", d, off)
        cmd = w1 >> 24
        if cmd & 0x60 != 0x60:
            continue
        num = (w1 >> 16) & 0xFF
        if num == 0:
            continue
        c[(VIF_UNPACK.get(cmd & 0xF, hex(cmd & 0xF)), bool(cmd & 0x10))] += 1
    return c


def files(args):
    for a in args:
        if os.path.isdir(a):
            for root, _, names in os.walk(a):
                for n in sorted(names):
                    yield os.path.join(root, n)
        else:
            yield a


def main(argv):
    exts = (".TXD", ".BTV", ".BGV", ".DAT", ".BGD", ".BIN")
    tex_by_kind = collections.defaultdict(collections.Counter)
    vif_by_kind = collections.defaultdict(collections.Counter)
    nfiles = collections.Counter()
    sizes = collections.defaultdict(collections.Counter)
    for path in files(argv[1:]):
        ext = os.path.splitext(path)[1].upper()
        if ext not in exts:
            continue
        kind = ext if ext != ".DAT" else os.path.basename(path).upper()
        d = open(path, "rb").read()
        nfiles[kind] += 1
        for t in scan_textures(d):
            _, w, h, depth, psm, pal, pix, mip, mxl, tcc, tfx = t
            tex_by_kind[kind][(psm, pal, pix, "mip%d" % mxl if mip or mxl else "nomip")] += 1
            sizes[kind][max(w, h)] += 1
        vif_by_kind[kind].update(scan_vif(d))
    for kind in sorted(nfiles):
        n = sum(tex_by_kind[kind].values())
        print("== %s: %d files, %d rasters" % (kind, nfiles[kind], n))
        for k, v in tex_by_kind[kind].most_common(8):
            print("   tex  %-5s %-4s %-4s %-6s %6d" % (k + (v,)))
        if sizes[kind]:
            print("   max(w,h): " + ", ".join("%d:%d" % kv for kv in sorted(sizes[kind].items())))
        for (fmt, masked), v in vif_by_kind[kind].most_common(8):
            print("   vif  STCYCL+UNPACK %-6s%s %8d" % (fmt, " (mask)" if masked else "", v))


if __name__ == "__main__":
    main(sys.argv)
