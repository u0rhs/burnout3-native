#!/usr/bin/env python3
"""Census of the serialized PS2 rasters (CGtTexture) on a Burnout 3 disc: storage formats, CLUT formats, upload
flags, mip levels and palette counts, per file kind. Prints counts only (no game data).

    python3 -I tools/modding/asset_raster_census.py <game dir>

Rasters are found as b3assetdump's census finds them: the two texture dictionaries by their directory, other files
by a validated header scan on 16-byte steps (cType 4 at +0x20, TEX0 TW/TH equal to log2 of the size, upload block
inside the file). Layout: docs/modding/file-formats/txd.md.
"""
import collections
import os
import struct
import sys

PSM = {0x00: "CT32", 0x01: "CT24", 0x02: "CT16", 0x0A: "CT16S", 0x13: "T8", 0x14: "T4", 0x1B: "T8H", 0x24: "T4HL",
       0x2C: "T4HH"}


def log2c(v):
    l = 0
    while (1 << l) < v:
        l += 1
    return l


def header(d, o):
    if o + 0xC8 > len(d) or d[o + 0x20] != 4:
        return None
    w, h, depth = struct.unpack_from("<III", d, o + 0x0C)
    if not w or not h or w > 2048 or h > 2048:
        return None
    tex0, = struct.unpack_from("<Q", d, o + 0x3C)
    psm = (tex0 >> 20) & 0x3F
    if psm not in PSM:
        return None
    if ((tex0 >> 26) & 0xF) != log2c(w) or ((tex0 >> 30) & 0xF) != log2c(h):
        return None
    levels = (d[o + 0x4A] >> 2) + 1
    if levels > 7:
        return None
    up, = struct.unpack_from("<I", d, o + 0x24)
    if not up or o + up + 16 > len(d):
        return None
    n, = struct.unpack_from("<I", d, o + up)
    if not n or n > 16 or o + up + 16 + 64 * n > len(d):
        return None
    return dict(w=w, h=h, depth=depth, psm=PSM[psm], cpsm=PSM.get((tex0 >> 51) & 0xF, "?"), csm2=(tex0 >> 55) & 1,
                tcc=(tex0 >> 34) & 1, levels=levels, flags=d[o + 0x6A], pals=d[o + 0xA0], transfers=n,
                transfers2=struct.unpack_from("<I", d, o + up + 8)[0])


def kind_of(path):
    n = os.path.basename(path).upper()
    ext = os.path.splitext(n)[1]
    if ext == ".TXD":
        return "TXD"
    if n in ("STATIC.DAT", "ENVIRO.DAT"):
        return n
    if ext in (".BGV", ".BTV"):
        return "car " + ext[1:]
    if ext == ".BIN" and os.path.basename(os.path.dirname(path)).upper() == "DATA":
        return "DATA/*.BIN"
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    game = sys.argv[1]
    stats = collections.defaultdict(collections.Counter)
    for root, _, files in os.walk(game):
        for fn in sorted(files):
            p = os.path.join(root, fn)
            k = kind_of(p)
            if not k:
                continue
            d = open(p, "rb").read()
            s = stats[k]
            s["files"] += 1
            offs = []
            if k == "TXD":
                n, dr = struct.unpack_from("<II", d, 8)
                offs = [struct.unpack_from("<I", d, dr + 16 * i + 8)[0] for i in range(n)]
            else:
                offs = range(0, len(d) - 0xC8, 16)
            for o in offs:
                h = header(d, o)
                if not h:
                    continue
                s["rasters"] += 1
                s["psm " + h["psm"] + ("" if h["psm"] in ("CT32", "CT24", "CT16", "CT16S") else
                                       " clut " + h["cpsm"] + (" csm2" if h["csm2"] else ""))] += 1
                s["levels %d" % h["levels"]] += 1
                s["upload flags 0x%X" % h["flags"]] += 1
                s["palettes %d" % h["pals"]] += 1
                s["transfers %d/%d" % (h["transfers"], h["transfers2"])] += 1
                s["tcc %d" % h["tcc"]] += 1
                s["max side %d" % max(h["w"], h["h"])] += 1
    for k in sorted(stats):
        print(k)
        for key, v in sorted(stats[k].items()):
            print("  %-28s %d" % (key, v))


if __name__ == "__main__":
    main()
