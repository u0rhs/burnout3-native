#!/usr/bin/env python3
"""Pixel diff of two PNGs written by b3gfx (8-bit RGB/RGBA, non-interlaced; stdlib only).

    python3 -I tools/render/pngdiff.py a.png b.png [--tol N]
Prints: size, pixels differing (any channel), pixels differing by more than N (default 2) in some channel, max channel
difference, and the mean absolute difference per channel. Exit 0 if identical, 1 if they differ, 2 on errors.
"""
import struct
import sys
import zlib


def read_png(path):
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(path + ': not a PNG')
    pos, idat, w = 8, [], None
    while pos < len(d):
        n, t = struct.unpack('>I4s', d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if t == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
            if depth != 8 or ctype not in (2, 6) or inter:
                raise ValueError(path + ': only 8-bit RGB/RGBA non-interlaced')
            bpp = 3 if ctype == 2 else 4
        elif t == b'IDAT':
            idat.append(body)
        pos += 12 + n
    raw = zlib.decompress(b''.join(idat))
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    # to RGB
    if bpp == 4:
        rgb = bytearray(w * h * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = out[0::4], out[1::4], out[2::4]
        out = rgb
    return w, h, bytes(out)


def main():
    args = sys.argv[1:]
    tol = 2
    if '--tol' in args:
        i = args.index('--tol'); tol = int(args[i + 1]); del args[i:i + 2]
    if len(args) != 2:
        print(__doc__); return 2
    try:
        wa, ha, a = read_png(args[0]); wb, hb, b = read_png(args[1])
    except (OSError, ValueError) as e:
        print(e); return 2
    if (wa, ha) != (wb, hb):
        print('size differs: %dx%d vs %dx%d' % (wa, ha, wb, hb)); return 1
    diff = any_px = tol_px = mx = 0
    sums = [0, 0, 0]
    for i in range(0, len(a), 3):
        da = [abs(a[i + k] - b[i + k]) for k in range(3)]
        m = max(da)
        if m:
            any_px += 1
            mx = max(mx, m)
            if m > tol: tol_px += 1
            for k in range(3): sums[k] += da[k]
    n = wa * ha
    print('%dx%d: %d of %d pixels differ (%.4f %%), %d by more than %d, max channel diff %d, mean abs diff r/g/b %.4f/%.4f/%.4f'
          % (wa, ha, any_px, n, 100.0 * any_px / n, tol_px, tol, mx, sums[0] / n, sums[1] / n, sums[2] / n))
    return 0 if any_px == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
