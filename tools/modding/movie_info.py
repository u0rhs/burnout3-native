#!/usr/bin/env python3
"""Prints the video parameters of the game's movies (docs/modding/file-formats/movies.md).

    python3 -I tools/modding/movie_info.py <disc folder | file.M2V> [...] [--count]

Per file: the MPEG-2 sequence header (size, aspect code, frame rate, bit rate, VBV buffer), the sequence extension
(profile / level, progressive, chroma format) and, with --count, the start codes of the whole file (pictures, GOPs,
sequence ends; any other start code is listed, so a file that is not a bare video elementary stream shows up).
Read only; prints numbers, no picture data.
"""
import collections
import os
import sys

RATES = {1: '23.976', 2: '24', 3: '25', 4: '29.97', 5: '30', 6: '50', 7: '59.94', 8: '60'}
ASPECT = {1: '1:1 pixels', 2: '4:3', 3: '16:9', 4: '2.21:1'}


def header(b):
    if b[:4] != b'\0\0\1\xb3':
        return None
    r = {'width': b[4] << 4 | b[5] >> 4, 'height': (b[5] & 15) << 8 | b[6], 'aspect': ASPECT.get(b[7] >> 4, b[7] >> 4),
         'rate': RATES.get(b[7] & 15, b[7] & 15), 'bitrate': (b[8] << 10 | b[9] << 2 | b[10] >> 6) * 400,
         'vbv_kb': ((b[10] & 0x1F) << 5 | b[11] >> 3) * 2}
    e = b.find(b'\0\0\1\xb5')
    if e >= 0 and b[e + 4] >> 4 == 1:
        r['profile_level'] = '0x%02X' % ((b[e + 4] & 15) << 4 | b[e + 5] >> 4)
        r['progressive'] = (b[e + 5] >> 3) & 1
        r['chroma'] = {1: '4:2:0', 2: '4:2:2', 3: '4:4:4'}.get((b[e + 5] >> 1) & 3)
    return r


def count(path):
    c = collections.Counter()
    with open(path, 'rb') as f:
        b = f.read()
    i = b.find(b'\0\0\1')
    while i >= 0 and i + 3 < len(b):
        code = b[i + 3]
        if code == 0:
            c['picture'] += 1
        elif code == 0xB8:
            c['gop'] += 1
        elif code == 0xB3:
            c['sequence'] += 1
        elif code == 0xB7:
            c['sequence end'] += 1
        elif not (1 <= code <= 0xAF or code in (0xB2, 0xB5)):
            c['other 0x%02X' % code] += 1
        i = b.find(b'\0\0\1', i + 4)
    return c


def main(a):
    do_count = '--count' in a
    paths = []
    for p in (x for x in a if x != '--count'):
        if os.path.isdir(p):
            for root, ds, fs in os.walk(p):
                ds.sort()
                paths += [os.path.join(root, f) for f in sorted(fs) if f.upper().endswith('.M2V')]
        else:
            paths.append(p)
    if not paths:
        print(__doc__)
        return 2
    base = a[0] if os.path.isdir(a[0]) else None
    total = collections.Counter()
    for p in paths:
        with open(p, 'rb') as f:
            h = header(f.read(4096))
        rel = os.path.relpath(p, base) if base else p
        if not h:
            print('%s: not an MPEG video elementary stream (no sequence header at the start)' % rel)
            continue
        line = '%s: %dx%d %s, %s fps, %d bit/s, VBV %d KB, profile/level %s, progressive %s, %s, %d bytes' % (
            rel, h['width'], h['height'], h['aspect'], h['rate'], h['bitrate'], h['vbv_kb'], h.get('profile_level'),
            h.get('progressive'), h.get('chroma'), os.path.getsize(p))
        if do_count:
            c = count(p)
            total.update(c)
            line += '; ' + ', '.join('%s %d' % kv for kv in sorted(c.items()))
        print(line)
    if do_count:
        print('%d files; %s' % (len(paths), ', '.join('%s %d' % kv for kv in sorted(total.items()))))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
