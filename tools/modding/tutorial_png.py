#!/usr/bin/env python3
"""The picture work of tools/modding/tutorial_check.sh (stdlib only; PNG reading from tools/render/pngdiff.py).

    python3 -I tools/modding/tutorial_png.py swap-rb <in.png> <out.png>
        tutorial 01's edit, done by a script: red and blue swapped in every pixel (alpha kept); RGBA out
    python3 -I tools/modding/tutorial_png.py colours <shot.png> [x0 y0 x1 y1]
        counts the strongly red, blue and green pixels (the channel >= 96 and over twice each other channel), in the
        whole picture or the box given as fractions of its size; prints "red <n> blue <n> pixels <n> green <n>"
    python3 -I tools/modding/tutorial_png.py hd-dds <in.png> <out.dds> <scale>
        a [textures] replacement made from an exported picture: red and green swapped, enlarged `scale` times (each
        pixel repeated), written as an uncompressed 32-bit RGBA DDS with PS2 alpha (0x80 = opaque), the format of a
        PCSX2 HD pack (docs/modding/mod-format.md "[textures]")
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'render'))
import pngdiff  # noqa: E402  read_png: 8-bit RGB / RGBA, non-interlaced -> RGB bytes


def read_rgba(path):
    """The PNG's pixels as RGBA (b3modtool export writes RGBA; RGB gets alpha 255)."""
    d = open(path, 'rb').read()
    ctype = d[25] if len(d) > 25 else 0
    w, h, rgb = pngdiff.read_png(path)
    if ctype != 6:
        out = bytearray(w * h * 4)
        out[0::4], out[1::4], out[2::4], out[3::4] = rgb[0::3], rgb[1::3], rgb[2::3], b'\xff' * (w * h)
        return w, h, out
    # pngdiff drops alpha: decode again keeping it (same filters)
    pos, idat = 8, []
    while pos < len(d):
        n, t = struct.unpack('>I4s', d[pos:pos + 8])
        if t == b'IDAT':
            idat.append(d[pos + 8:pos + 8 + n])
        pos += 12 + n
    raw = zlib.decompress(b''.join(idat))
    stride, bpp = w * 4, 4
    out, prev, p = bytearray(h * stride), bytearray(stride), 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if f:
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
    return w, h, out


def write_rgba(path, w, h, px):
    def chunk(t, body):
        return struct.pack('>I', len(body)) + t + body + struct.pack('>I', zlib.crc32(t + body) & 0xffffffff)
    raw = b''.join(b'\0' + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == 'swap-rb':
        w, h, px = read_rgba(a[1])
        px[0::4], px[2::4] = px[2::4], px[0::4]
        write_rgba(a[2], w, h, px)
        print('tutorial_png: %s -> %s (%dx%d, red and blue swapped)' % (a[1], a[2], w, h))
        return 0
    if len(a) == 4 and a[0] == 'hd-dds':
        w, h, px = read_rgba(a[1])
        k = int(a[3])
        px[0::4], px[1::4] = px[1::4], px[0::4]
        W, H = w * k, h * k
        out = bytearray(W * H * 4)
        for y in range(H):
            src = px[(y // k) * w * 4:(y // k + 1) * w * 4]
            row = bytearray(W * 4)
            for i in range(4):
                ch = src[i::4]
                if i == 3:
                    ch = bytes((v + 1) // 2 for v in ch)
                row[i::4] = bytes(c for c in ch for _ in range(k))
            out[y * W * 4:(y + 1) * W * 4] = row
        hdr = bytearray(128)
        hdr[0:4] = b'DDS '
        struct.pack_into('<IIII', hdr, 4, 124, 0x100F, H, W)
        struct.pack_into('<I', hdr, 20, W * 4)
        struct.pack_into('<IIIIIIII', hdr, 76, 32, 0x41, 0, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000)
        struct.pack_into('<I', hdr, 108, 0x1000)
        with open(a[2], 'wb') as f:
            f.write(bytes(hdr) + bytes(out))
        print('tutorial_png: %s -> %s (%dx%d, red and green swapped, PS2 alpha)' % (a[1], a[2], W, H))
        return 0
    if len(a) in (2, 6) and a[0] == 'colours':
        w, h, rgb = pngdiff.read_png(a[1])
        x0, y0, x1, y1 = (0.0, 0.0, 1.0, 1.0) if len(a) == 2 else map(float, a[2:])
        red = blue = green = n = 0
        for y in range(int(y0 * h), int(y1 * h)):
            row = rgb[y * w * 3:(y + 1) * w * 3]
            for x in range(int(x0 * w), int(x1 * w)):
                r, g, b = row[3 * x], row[3 * x + 1], row[3 * x + 2]
                n += 1
                if r >= 96 and r > 2 * g and r > 2 * b: red += 1
                elif b >= 96 and b > 2 * r and b > 2 * g: blue += 1
                elif g >= 96 and g > 2 * r and g > 2 * b: green += 1
        print('red %d blue %d pixels %d green %d' % (red, blue, n, green))
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
