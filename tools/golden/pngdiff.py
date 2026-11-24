#!/usr/bin/env python3
"""pngdiff.py -- two PNG screenshots of one size compared pixel by pixel (replay_check's shot tolerance).
Prints "<pixels that differ> <largest channel difference>" (0 0: identical); with a level, a third number: the pixels
that differ by more than it. Python standard library only: 8-bit RGB / RGBA PNGs (the port's shots), any filter, not
interlaced.
Usage: pngdiff.py a.png b.png [level]      (exit 2: unreadable, or the sizes differ)"""
import struct
import sys
import zlib


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, w = 8, [], None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _comp, _filt, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError("an 8-bit RGB / RGBA PNG, not interlaced, is needed")
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + n
    if w is None:
        raise ValueError("no IHDR")
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    i = 0
    for y in range(h):
        ft = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ft == 1:
                line[x] = (line[x] + a) & 255
            elif ft == 2:
                line[x] = (line[x] + b) & 255
            elif ft == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, bpp, bytes(out)


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__, file=sys.stderr)
        return 2
    try:
        wa, ha, ba, pa = read_png(sys.argv[1])
        wb, hb, bb, pb = read_png(sys.argv[2])
    except (OSError, ValueError, zlib.error, struct.error) as e:
        print("pngdiff: %s" % e, file=sys.stderr)
        return 2
    if (wa, ha) != (wb, hb):
        print("pngdiff: the sizes differ", file=sys.stderr)
        return 2
    level = int(sys.argv[3]) if len(sys.argv) == 4 else None
    pixels, worst, over = 0, 0, 0
    for k in range(wa * ha):
        d = 0
        for c in range(3):   # (the colour channels; a shot's alpha is not shown)
            d = max(d, abs(pa[k * ba + c] - pb[k * bb + c]))
        if d:
            pixels += 1
            worst = max(worst, d)
            if level is not None and d > level:
                over += 1
    print(pixels, worst, over) if level is not None else print(pixels, worst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
