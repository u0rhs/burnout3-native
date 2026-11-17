#!/usr/bin/env python3
"""carlook_check.py -- the enhanced car reflections must keep the paint's colour (reflect2, docs/car-look.md).

  python3 -I tools/render/carlook_check.py original.png enhanced.png x0 y0 x1 y1 [--max-hue 8] [--min-sat 0.90]

The region (fractions of the image, x0 y0 x1 y1) is a patch of paint, windows left out. Both pictures are renders of the
same frame (b3replay --car-reflections original / enhanced). Checks over the region: the hue of the mean colour moved by
at most --max-hue degrees (only when the paint is coloured: mean saturation of the original > 0.15), the mean saturation
did not drop below --min-sat of the original, and a neutral paint (black, grey, white) did not become coloured (mean
saturation up by less than 0.08). Prints one line and exits 0 (PASS) or 1 (FAIL). Standard library only.
"""
import colorsys
import struct
import sys
import zlib


def load(path):
    d = open(path, 'rb').read()
    pos, idat = 8, b''
    w = h = ct = 0
    while pos < len(d):
        n, t = struct.unpack('>I4s', d[pos:pos + 8])
        c = d[pos + 8:pos + 8 + n]
        if t == b'IHDR':
            w, h, _bd, ct = struct.unpack('>IIBB', c[:10])
        if t == b'IDAT':
            idat += c
        pos += 12 + n
    raw = zlib.decompress(idat)
    bpp = {2: 3, 6: 4}[ct]
    st = w * bpp
    rows, prev, i = [], bytearray(st), 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + st])
        i += 1 + st
        for x in range(st):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def region_stats(path, box):
    w, h, bpp, rows = load(path)
    x0, y0, x1, y1 = box
    n = 0
    rgb = [0.0, 0.0, 0.0]
    sat = 0.0
    for y in range(int(h * y0), int(h * y1), 2):
        for x in range(int(w * x0), int(w * x1), 2):
            r, g, b = rows[y][x * bpp:x * bpp + 3]
            rgb[0] += r
            rgb[1] += g
            rgb[2] += b
            sat += colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)[1]
            n += 1
    mean = [c / n / 255 for c in rgb]
    hue, s_mean, v = colorsys.rgb_to_hsv(*mean)
    return hue * 360.0, sat / n, v, mean


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    opt = {}
    argv = sys.argv[1:]
    for i, a in enumerate(argv):
        if a.startswith('--') and i + 1 < len(argv):
            opt[a[2:]] = float(argv[i + 1])
    a_png, b_png = args[0], args[1]
    box = tuple(float(v) for v in args[2:6])
    max_hue = opt.get('max-hue', 8.0)
    min_sat = opt.get('min-sat', 0.90)
    ho, so, vo, mo = region_stats(a_png, box)
    he, se, ve, me = region_stats(b_png, box)
    dh = abs((he - ho + 180.0) % 360.0 - 180.0)
    coloured = so > 0.15
    ok = True
    why = []
    if coloured:
        if dh > max_hue:
            ok = False
            why.append('hue moved %.1f deg' % dh)
        if se < min_sat * so:
            ok = False
            why.append('saturation fell %.3f -> %.3f' % (so, se))
    elif se - so > 0.08:
        ok = False
        why.append('a neutral paint became coloured (saturation %.3f -> %.3f)' % (so, se))
    print('carlook_check: %s original hue %.0f sat %.3f val %.3f, enhanced hue %.0f sat %.3f val %.3f%s' %
          ('PASS' if ok else 'FAIL', ho, so, vo, he, se, ve, ('; ' + '; '.join(why)) if why else ''))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
