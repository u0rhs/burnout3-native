#!/usr/bin/env python3
"""make_icons.py [--check]: builds every app-icon file from the B3 art (assets/icon/source/b3-red.png, the same picture
as assets/b3-icon.png, which the launcher's header draws).

  python3 -I tools/release/make_icons.py            write the files
  python3 -I tools/release/make_icons.py --check    exit 1 if a file on disk differs from what the art makes

The art is cropped to its visible part, resampled with a Lanczos filter (premultiplied alpha, standard library only) and
composited on a dark rounded tile that a small scanline rasteriser draws. Variants:
  mac    the macOS grid: an 824 px squircle in the 1024 canvas, soft drop shadow
  flat   a rounded square with a small transparent margin, no shadow (Windows, Linux, the in-game window)
  small  16 / 24 / 32 px: almost no margin, a bigger mark, no rim
  clear  the art alone on a transparent background (an alternative Windows icon)

Outputs (under assets/icon/ unless noted):
  png/b3-icon-{16,24,32,48,64,128,256,512,1024}.png (flat; <= 32 px use small), png/b3-icon-mac-1024.png,
  png/b3-icon-clear-256.png
  b3-icon.ico         16 24 32 48 64 128 256 (PNG-compressed entries), flat tile: the one the Windows builds link
  b3-icon-clear.ico   the same sizes on a transparent background (not linked; swap it in to try)
  b3-icon.icns        the macOS container: icp4..ic14 (16 .. 1024, @2x included), PNG payloads
  hicolor/<n>x<n>/apps/burnout3-native.png (16 .. 512)
  ../../include/b3port/app_icon_data.h (the 128 px flat icon as RGBA bytes, for SDL_SetWindowIcon)
"""
import math
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ICON = os.path.join(ROOT, "assets", "icon")
ART = os.path.join(ICON, "source", "b3-red.png")

TILE_HI, TILE_LO = (44, 45, 52), (5, 5, 7)    # near-black, lit from the top so the red reads
RIM_HI, RIM_LO = (96, 98, 108), (26, 27, 32)


# ---- PNG in / out ----------------------------------------------------------------------------------------------------
def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("make_icons: %s is not a PNG" % path)
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype != 6 or inter != 0:
                sys.exit("make_icons: needs an 8-bit RGBA non-interlaced PNG")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    raw = zlib.decompress(idat)
    stride = w * 4
    rows, prev, at = [], bytearray(stride), 0
    for _ in range(h):
        ftype, line = raw[at], bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 255
            elif ftype == 2:
                line[i] = (line[i] + b) & 255
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, rows


def png_bytes(rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", len(rows[0]) // 4, len(rows), 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# ---- the art: crop, Lanczos resample (premultiplied) -------------------------------------------------------------------
_ART = []


def art():
    """the art cropped to its visible part: (w, h, [R, G, B, A planes as lists of rows of premultiplied floats 0..255])"""
    if not _ART:
        w, h, rows = read_png(ART)
        x0, y0, x1, y1 = w, h, -1, -1
        for y in range(h):
            r = rows[y]
            for x in range(w):
                if r[4 * x + 3] > 8:
                    x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
        cw, ch = x1 - x0 + 1, y1 - y0 + 1
        planes = [[], [], [], []]
        for y in range(y0, y1 + 1):
            r = rows[y]
            line = [[], [], [], []]
            for x in range(x0, x1 + 1):
                a = r[4 * x + 3]
                line[0].append(r[4 * x] * a / 255.0)
                line[1].append(r[4 * x + 1] * a / 255.0)
                line[2].append(r[4 * x + 2] * a / 255.0)
                line[3].append(float(a))
            for c in range(4):
                planes[c].append(line[c])
        _ART.append((cw, ch, planes))
    return _ART[0]


def _lanczos(x):
    if x == 0.0:
        return 1.0
    if abs(x) >= 3.0:
        return 0.0
    px = math.pi * x
    return 3.0 * math.sin(px) * math.sin(px / 3.0) / (px * px)


def _weights(n_in, n_out):
    """per output sample: (first input index, [weights]) for a Lanczos-3 filter (support widens when shrinking)"""
    scale = n_in / n_out
    f = max(1.0, scale)
    out = []
    for o in range(n_out):
        c = (o + 0.5) * scale
        lo, hi = max(0, int(math.floor(c - 3 * f))), min(n_in - 1, int(math.ceil(c + 3 * f)))
        ws = [_lanczos((i + 0.5 - c) / f) for i in range(lo, hi + 1)]
        s = sum(ws)
        out.append((lo, [w / s for w in ws]))
    return out


def resample(planes, w, h, ow, oh):
    wx, wy = _weights(w, ow), _weights(h, oh)
    out = []
    for p in planes:
        tmp = []
        for row in p:   # horizontal
            line = []
            for lo, ws in wx:
                line.append(sum(v * k for v, k in zip(row[lo:lo + len(ws)], ws)))
            tmp.append(line)
        cols = []
        for lo, ws in wy:   # vertical
            line = [0.0] * ow
            for r, k in zip(tmp[lo:lo + len(ws)], ws):
                for x in range(ow):
                    line[x] += r[x] * k
            cols.append(line)
        out.append(cols)
    return out


# ---- the tile: scanline rasteriser -----------------------------------------------------------------------------------
def superellipse(cx, cy, half, n, steps=192):
    pts = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        c, s = math.cos(t), math.sin(t)
        pts.append((cx + half * math.copysign(abs(c) ** (2 / n), c), cy + half * math.copysign(abs(s) ** (2 / n), s)))
    return pts


def rounded_rect(cx, cy, half, radius, arc=14):
    pts = []
    for qx, qy, a0 in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
        for i in range(arc + 1):
            a = math.radians(a0 + 90 * i / arc)
            pts.append((cx + (half - radius) * qx + radius * math.cos(a), cy + (half - radius) * qy + radius * math.sin(a)))
    return pts


def grad(y0, y1, *stops):
    return ("g", y0, y1, [(t, c if len(c) == 4 else c + (255,)) for t, c in stops])


def solid(c):
    return ("s", c if len(c) == 4 else c + (255,))


def tile_shapes(kind):
    """(contours, fill) of the tile in the 1024 canvas, and the square (x0, y0, side) the art may use"""
    mac, small = kind == "mac", kind == "small"
    inset = 100 if mac else (5 if small else 26)
    half = 512 - inset
    cx = cy = 512.0
    top, bot = cy - half, cy + half
    shapes = []
    if mac:   # soft depth: stacked translucent tiles, growing, pushed down
        for i in range(12):
            shapes.append(([superellipse(cx, cy + 14 + i * 0.6, half + 2 + i * 2.6, 5)], solid((0, 0, 0, 7 if i < 9 else 6))))
    tile = (lambda h: [superellipse(cx, cy, h, 5)]) if mac else (lambda h: [rounded_rect(cx, cy, h, (0.22 if small else 0.2) * 2 * half)])
    if small:
        shapes.append((tile(half), grad(top, bot, (0, TILE_HI), (1, TILE_LO))))
    else:
        rim = 0.0085 * 2 * half
        shapes.append((tile(half), grad(top, bot, (0, RIM_HI), (0.5, RIM_LO), (1, (14, 14, 17)))))
        shapes.append((tile(half - rim), grad(top, bot, (0, TILE_HI), (1, TILE_LO))))
        shapes.append((tile(half - rim), grad(top, top + 0.5 * (bot - top), (0, (255, 255, 255, 22)), (1, (255, 255, 255, 0)))))
    return shapes, (half * 2)


def _color_at(fill, y):
    if fill[0] == "s":
        return fill[1]
    _, y0, y1, stops = fill
    t = min(1.0, max(0.0, (y - y0) / (y1 - y0)))
    for (ta, ca), (tb, cb) in zip(stops, stops[1:]):
        if t <= tb:
            u = 0.0 if tb == ta else (t - ta) / (tb - ta)
            return tuple(ca[i] + (cb[i] - ca[i]) * u for i in range(4))
    return stops[-1][1]


def render_tile(shapes, size):
    """premultiplied float planes [R, G, B, A] (rows of lists, 0..255 / alpha 0..1 in A) of the shapes at size x size"""
    k = size / 1024.0
    sub = 8 if size <= 256 else 4
    P = [[[0.0] * size for _ in range(size)] for _ in range(4)]
    R, G, B, A = P
    for contours, fill in shapes:
        rows = {}
        for c in contours:
            n = len(c)
            for i in range(n):
                x0, y0 = c[i][0] * k, c[i][1] * k
                x1, y1 = c[(i + 1) % n][0] * k, c[(i + 1) % n][1] * k
                if y0 == y1:
                    continue
                if y0 > y1:
                    x0, y0, x1, y1 = x1, y1, x0, y0
                slope = (x1 - x0) / (y1 - y0)
                j0 = max(0, math.ceil(y0 * sub - 0.5))
                j1 = min(size * sub - 1, math.ceil(y1 * sub - 0.5) - 1)
                for j in range(j0, j1 + 1):
                    rows.setdefault(j, []).append(x0 + ((j + 0.5) / sub - y0) * slope)
        if not rows:
            continue
        for py in range(max(0, min(rows) // sub), min(size - 1, max(rows) // sub) + 1):
            cov = [0.0] * (size + 2)
            dif = [0.0] * (size + 2)
            for j in range(py * sub, py * sub + sub):
                xs = rows.get(j)
                if not xs:
                    continue
                xs.sort()
                for a in range(0, len(xs) - 1, 2):
                    xa, xb = max(0.0, xs[a]), min(float(size), xs[a + 1])
                    if xb <= xa:
                        continue
                    ia, ib = int(xa), int(xb)
                    if ia == ib:
                        cov[ia] += xb - xa
                    else:
                        cov[ia] += ia + 1 - xa
                        cov[ib] += xb - ib
                        dif[ia + 1] += 1.0
                        dif[ib] -= 1.0
            r, g, b, a = _color_at(fill, (py + 0.5) / k)
            a /= 255.0
            run = 0.0
            Rr, Gr, Br, Ar = R[py], G[py], B[py], A[py]
            for x in range(size):
                run += dif[x]
                cv = (cov[x] + run) / sub
                if cv > 0.0:
                    sa = a * min(cv, 1.0)
                    inv = 1.0 - sa
                    Rr[x] = r * sa + Rr[x] * inv
                    Gr[x] = g * sa + Gr[x] * inv
                    Br[x] = b * sa + Br[x] * inv
                    Ar[x] = sa + Ar[x] * inv
    return P


# ---- the icon: tile + art -------------------------------------------------------------------------------------------
_SIZES = {"mac": 0.84, "flat": 0.84, "clear": 0.92, "small": 0.90}   # the art's longest side as a fraction of the tile


def icon_rows(kind, size):
    """RGBA rows (straight alpha) of one icon"""
    cw, ch, planes = art()
    if kind == "clear":
        P = [[[0.0] * size for _ in range(size)] for _ in range(4)]
        tile_side = size
        ox = oy = 0.0
    else:
        shapes, side = tile_shapes(kind)
        P = render_tile(shapes, size)
        tile_side = side * size / 1024.0
        ox = oy = (size - tile_side) / 2
    fit = _SIZES[kind] * tile_side / max(cw, ch)
    aw, ah = max(1, round(cw * fit)), max(1, round(ch * fit))
    small = resample(planes, cw, ch, aw, ah)
    px, py = round(ox + (tile_side - aw) / 2 - (0 if kind == "clear" else 0)), round(oy + (tile_side - ah) / 2)
    for y in range(ah):
        yy = py + y
        if not 0 <= yy < size:
            continue
        for x in range(aw):
            xx = px + x
            if not 0 <= xx < size:
                continue
            a = min(255.0, max(0.0, small[3][y][x])) / 255.0
            if a <= 0.0:
                continue
            inv = 1.0 - a
            for c in range(3):
                P[c][yy][xx] = min(255.0, max(0.0, small[c][y][x])) + P[c][yy][xx] * inv
            P[3][yy][xx] = a + P[3][yy][xx] * inv
    out = []
    for y in range(size):
        line = bytearray()
        for x in range(size):
            a = P[3][y][x]
            if a <= 0.0:
                line += b"\0\0\0\0"
            else:
                line += bytes((min(255, round(P[0][y][x] / a)), min(255, round(P[1][y][x] / a)), min(255, round(P[2][y][x] / a)), min(255, round(a * 255))))
        out.append(line)
    return out


# ---- containers ------------------------------------------------------------------------------------------------------
def ico_bytes(images):
    head = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, blobs = b"", b""
    for s, blob in images:
        entries += struct.pack("<BBBBHHII", s & 255, s & 255, 0, 0, 1, 32, len(blob), offset + len(blobs))
        blobs += blob
    return head + entries + blobs


ICNS_TYPES = [(b"icp4", 16), (b"icp5", 32), (b"icp6", 64), (b"ic07", 128), (b"ic08", 256), (b"ic09", 512), (b"ic10", 1024),
              (b"ic11", 32), (b"ic12", 64), (b"ic13", 256), (b"ic14", 512)]   # ic11..ic14: 16, 32, 128, 256 @2x


def icns_bytes(png_of):
    body = b"".join(t + struct.pack(">I", 8 + len(png_of(s))) + png_of(s) for t, s in ICNS_TYPES)
    return b"icns" + struct.pack(">I", 8 + len(body)) + body


def header_text(rows, size):
    flat = b"".join(bytes(r) for r in rows)
    lines = []
    for i in range(0, len(flat), 24):
        lines.append("    " + ",".join("0x%02x" % b for b in flat[i:i + 24]) + ",")
    return ("// generated by tools/release/make_icons.py; do not edit by hand.\n// The app icon, %dx%d RGBA (straight alpha), for SDL_SetWindowIcon (b3port/app_icon.h).\n"
            "#pragma once\nnamespace b3port {\ninline constexpr int kAppIconSize = %d;\ninline constexpr unsigned char kAppIconRgba[%d] = {\n%s\n};\n}  // namespace b3port\n"
            % (size, size, size, len(flat), "\n".join(lines))).encode()


FLAT_SIZES = (16, 24, 32, 48, 64, 128, 256, 512, 1024)
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
HICOLOR = (16, 24, 32, 48, 64, 128, 256, 512)
_ROWS = {}


def rows_of(kind, size):
    if (kind, size) not in _ROWS:
        _ROWS[(kind, size)] = icon_rows(kind, size)
    return _ROWS[(kind, size)]


def flat_kind(s):
    return "small" if s <= 32 else "flat"


def mac_kind(s):
    return "small" if s <= 32 else "mac"


def outputs():
    """{path: bytes} of every generated file"""
    out = {}
    flat_png = lambda s: png_bytes(rows_of(flat_kind(s), s))
    mac_png = lambda s: png_bytes(rows_of(mac_kind(s), s))
    clear_png = lambda s: png_bytes(rows_of("clear", s))
    for s in FLAT_SIZES:
        out[os.path.join(ICON, "png", "b3-icon-%d.png" % s)] = flat_png(s)
    out[os.path.join(ICON, "png", "b3-icon-mac-1024.png")] = mac_png(1024)
    out[os.path.join(ICON, "png", "b3-icon-clear-256.png")] = clear_png(256)
    out[os.path.join(ICON, "b3-icon.ico")] = ico_bytes([(s, flat_png(s)) for s in ICO_SIZES])
    out[os.path.join(ICON, "b3-icon-clear.ico")] = ico_bytes([(s, clear_png(s)) for s in ICO_SIZES])
    out[os.path.join(ICON, "b3-icon.icns")] = icns_bytes(mac_png)
    for s in HICOLOR:
        out[os.path.join(ICON, "hicolor", "%dx%d" % (s, s), "apps", "burnout3-native.png")] = flat_png(s)
    out[os.path.join(ROOT, "include", "b3port", "app_icon_data.h")] = header_text(rows_of("flat", 128), 128)
    return out


def main():
    check = sys.argv[1:] == ["--check"]
    if sys.argv[1:] and not check:
        sys.exit(__doc__)
    bad = 0
    if os.path.exists(os.path.join(ROOT, "assets", "b3-icon.png")) and open(ART, "rb").read() != open(os.path.join(ROOT, "assets", "b3-icon.png"), "rb").read():
        print("make_icons: assets/b3-icon.png differs from assets/icon/source/b3-red.png (they are the same art)")
        bad += 1
    for path, data in sorted(outputs().items()):
        rel = os.path.relpath(path, ROOT)
        if check:
            if not os.path.exists(path) or open(path, "rb").read() != data:
                print("make_icons: %s is out of date (run tools/release/make_icons.py)" % rel)
                bad += 1
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "wb").write(data)
            print("wrote %s (%d bytes)" % (rel, len(data)))
    if check:
        print("make_icons: %s" % ("FAIL, %d file(s) differ" % bad if bad else "PASS, every icon file matches the art"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
