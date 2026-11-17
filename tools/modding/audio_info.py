#!/usr/bin/env python3
"""Reads the game's audio files and prints their structure (docs/modding/file-formats/audio.md).

    python3 -I tools/modding/audio_info.py <disc folder or file> [...]      structure of every .RWS / .AWD / .HWD / .LWD
    python3 -I tools/modding/audio_info.py --census <disc folder>          one line per file kind, totals
    python3 -I tools/modding/audio_info.py --wav <file.RWS> <segment> <substream> <out.wav>
    python3 -I tools/modding/audio_info.py --wav <file.AWD> <wave index> <out.wav>

Read only; prints numbers and names, no audio bytes (--wav writes the decoded audio of one segment / wave to the file
you name, for listening on your own PC). Checks every PS-ADPCM frame header it decodes (filter 0-4, shift 0-12).
"""
import os
import struct
import sys

ALIGN = 0x10   # _rwaGblockAlign (RwaCoreOpen, rwabase.cpp)
COEF = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def cstr(b, o):
    e = b.index(b'\0', o)
    return b[o:e].decode('latin-1'), e


def align(n):
    return (n + ALIGN - 1) & ~(ALIGN - 1)


# ---------------------------------------------------------------------------------------------------------------- RWS

def chunks(b, o, end):
    while o + 12 <= end:
        t, n, v = struct.unpack_from('<III', b, o)
        yield o, t, n, v
        o += 12 + n


def wave_format(b, o):
    """A serialised RwaWaveFormat (0x1C bytes) and the data-type UUID after it (0x10)."""
    rate, _dtype, length = struct.unpack_from('<III', b, o)
    bits, chans = b[o + 0xC], b[o + 0xD]
    flags = b[o + 0x18]
    return {'rate': rate, 'length': length, 'bits': bits, 'channels': chans, 'flags': flags,
            'type': b[o + 0x1C:o + 0x2C].hex()}


def stream_desc(b, d, size):
    """The packed stream description (chunk 0x80E after its 0x14-byte header), as _rwaStreamFormatCreateFromPack
    0x2AD8E0 walks it."""
    nseg, nsub = u32(b, d + 0xC), u32(b, d + 0x14)
    r = {'segments': [], 'substreams': [], 'block': u32(b, d + 0x20), 'data_at': u32(b, d + 0x24),
         'word1c': u32(b, d + 0x1C)}
    cur = d + 0x2C
    if u32(b, d + 0):
        cur += 0x10
    if u32(b, d + 4):
        r['name'], e = cstr(b, cur)
        cur += align(e - cur + 1)
    seg0 = cur
    cur += nseg * 32
    segs = []
    for i in range(nseg):
        s = seg0 + 32 * i
        segs.append({'at': s, 'size': u32(b, s + 0x18), 'offset': u32(b, s + 0x1C), 'markers': u32(b, s + 0x10)})
    for s in segs:
        if u32(b, s['at'] + 0xC):
            s['sizes'] = [u32(b, cur + 4 * k) for k in range(nsub)]
            cur += 4 * nsub
    for s in segs:
        if u32(b, s['at'] + 0):
            cur += 0x10
    for s in segs:
        if u32(b, s['at'] + 4):
            s['name'], e = cstr(b, cur)
            cur += align(e - cur + 1)
    if any(s['markers'] for s in segs):   # markers (0x14 bytes each, then their UUIDs / names): none on the US disc
        raise ValueError('segment markers present: not handled by this reader')
    sub0 = cur
    cur += nsub * 40
    subs = []
    for i in range(nsub):
        f = sub0 + 40 * i
        subs.append({'at': f, 'spf': u32(b, f + 0xC), 'bytes_per_block': u32(b, f + 0x10),
                     'interleave': u16(b, f + 0x18), 'frame': u16(b, f + 0x1A), 'word1c': u32(b, f + 0x1C),
                     'offset_in_block': u32(b, f + 0x24)})
    for s in subs:
        if u32(b, s['at'] + 0x14):
            s.update(wave_format(b, cur))
            cur += align(0x2C)
    for s in subs:
        if u32(b, s['at'] + 0):
            cur += 0x10
    for s in subs:
        if u32(b, s['at'] + 4):
            s['name'], e = cstr(b, cur)
            cur += align(e - cur + 1)
    r['segments'], r['substreams'] = segs, subs
    r['used'] = cur - d
    return r


def read_rws(path):
    b = open(path, 'rb').read()
    out = []
    for o, t, n, v in chunks(b, 0, len(b)):
        item = {'at': o, 'type': t, 'size': n, 'version': v}
        if t == 0x80D:
            for o2, t2, n2, v2 in chunks(b, o + 12, o + 12 + n):
                if t2 == 0x80E:
                    h = struct.unpack_from('<5I', b, o2 + 12)
                    item['header'] = h
                    item['desc'] = stream_desc(b, o2 + 12 + 0x14, h[0])
                    item['desc_room'] = n2 - 0x14
                elif t2 == 0x80F:
                    item['data'] = (o2 + 12, n2)
        elif t == 0x809:
            item['children'] = [(t2, n2) for _, t2, n2, _ in chunks(b, o + 12, o + 12 + n)]
        out.append(item)
    return b, out


# ---------------------------------------------------------------------------------------------------------------- AWD

def read_awd(path):
    """An .AWD / .HWD / .LWD: the header RwaWaveDictReadAsync 0x29EB70 reads (0x800 bytes first), the RwaWaveDict
    dump inside it (offsets relative to the file start, as RwaWaveDictFixup 0x29E740 adds the load address), then the
    wave data."""
    b = open(path, 'rb').read()
    hsize, dict_at, data_size = u32(b, 8), u32(b, 0xC), u32(b, 0x14)
    d = dict_at
    name = cstr(b, u32(b, d + 4))[0] if u32(b, d + 4) else ''
    last, node = u32(b, d + 0xC), u32(b, d + 0x10)
    waves = []
    while node:
        w = u32(b, node + 8)
        wn = cstr(b, u32(b, w + 4))[0] if u32(b, w + 4) else ''
        f = w + 0x10
        waves.append({'name': wn, 'rate': u32(b, f), 'type_index': u32(b, f + 4), 'length': u32(b, f + 8),
                      'bits': b[f + 0xC], 'channels': b[f + 0xD], 'data': u32(b, w + 0x4C), 'flags': u32(b, w + 0x54)})
        if node == last:
            break
        node = u32(b, node + 4)
    return b, {'magic': u32(b, 0), 'swap': b[4], 'header_size': hsize, 'dict_at': dict_at, 'word10': u32(b, 0x10),
               'data_size': data_size, 'platform': b[0x18:0x28].hex(), 'name': name, 'waves': waves,
               'file_ok': hsize + data_size == len(b)}


# ------------------------------------------------------------------------------------------------------------ ADPCM

class BadFrame(Exception):
    pass


VALID = bytes(f << 4 | sh for f in range(5) for sh in range(13))


def check_frames(frames):
    """Every 16-byte frame's first byte: filter 0-4 (high nibble), shift 0-12 (low nibble). Returns the flag byte of
    the last frame (bit 0 end, bit 1 loop, bit 2 loop start)."""
    if len(frames) % 16:
        raise BadFrame('length 0x%X is not whole 16-byte frames' % len(frames))
    if bytes(frames[0::16]).translate(None, VALID):
        raise BadFrame('a frame header with filter > 4 or shift > 12')
    return frames[-15] if frames else 0


def adpcm(frames, hist=None):
    h1, h2 = hist or (0, 0)
    out = []
    for k in range(0, len(frames) - 15, 16):
        shift, filt = frames[k] & 0xF, frames[k] >> 4
        if filt > 4 or shift > 12:
            raise BadFrame('frame at +0x%X: filter %d shift %d' % (k, filt, shift))
        f0, f1 = COEF[filt]
        for i in range(28):
            nib = (frames[k + 2 + i // 2] >> (4 * (i & 1))) & 0xF
            t = ((nib << 12) - (0x10000 if nib & 8 else 0)) >> shift
            s = max(-32768, min(32767, t + ((h1 * f0 + h2 * f1 + 32) >> 6)))
            h2, h1 = h1, s
            out.append(s)
    return out, (h1, h2)


def write_wav(path, rate, chans):
    n = min(len(c) for c in chans)
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + n * 2 * len(chans)) + b'WAVEfmt ')
        f.write(struct.pack('<IHHIIHH', 16, 1, len(chans), rate, rate * 2 * len(chans), 2 * len(chans), 16))
        f.write(b'data' + struct.pack('<I', n * 2 * len(chans)))
        f.write(b''.join(struct.pack('<' + 'h' * len(chans), *(c[i] for c in chans)) for i in range(n)))
    return n


def rws_channels(b, item, seg, sub):
    """The channels of one segment / sub-stream: the data is cut into blocks (desc +0x20 bytes); each block holds
    every sub-stream's slice (+0x24 offset, +0x10 bytes), and a slice holds its channels one after another, `interleave`
    bytes each."""
    d = item['desc']
    s, f = d['segments'][seg], d['substreams'][sub]
    data0 = item['data'][0] + s['offset']
    nblocks = (s['size'] + d['block'] - 1) // d['block']
    chans = [bytearray() for _ in range(f['channels'])]
    for k in range(nblocks):
        sl = data0 + k * d['block'] + f['offset_in_block']
        for c in range(f['channels']):
            chans[c] += b[sl + c * f['interleave']: sl + (c + 1) * f['interleave']]
    total = s['sizes'][sub] // f['channels'] if 'sizes' in s else None
    return [bytes(c[:total]) if total else bytes(c) for c in chans]


# ------------------------------------------------------------------------------------------------------------- main

def kind(path):
    e = os.path.splitext(path)[1].upper()
    return 'rws' if e == '.RWS' else 'awd' if e in ('.AWD', '.HWD', '.LWD') else None


def files(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, ds, fs in os.walk(p):
                ds.sort()
                for f in sorted(fs):
                    if kind(f):
                        yield os.path.join(root, f)
        elif kind(p):
            yield p


def show(path, base):
    rel = os.path.relpath(path, base) if base else path
    if kind(path) == 'awd':
        _, a = read_awd(path)
        print('%s: wave dictionary "%s", header 0x%X, data 0x%X (%s), %d waves' % (
            rel, a['name'], a['header_size'], a['data_size'], 'sizes add up' if a['file_ok'] else 'SIZE MISMATCH',
            len(a['waves'])))
        for i, w in enumerate(a['waves']):
            print('  %3d %-24s %6d Hz %d ch %2d bit  0x%06X bytes at +0x%06X flags 0x%X' % (
                i, w['name'], w['rate'], w['channels'], w['bits'], w['length'], w['data'], w['flags']))
        return
    _, items = read_rws(path)
    for it in items:
        if it['type'] != 0x80D:
            print('%s: chunk 0x%X (%d bytes, version 0x%08X) %s' % (rel, it['type'], it['size'], it['version'],
                                                                   it.get('children', '')))
            continue
        d = it['desc']
        print('%s: stream "%s", block 0x%X, data at 0x%X (0x%X bytes), %d segments, %d sub-streams, description '
              '0x%X of 0x%X bytes' % (rel, d.get('name', ''), d['block'], it['data'][0], it['data'][1],
                                      len(d['segments']), len(d['substreams']), it['header'][0], it['desc_room']))
        for f in d['substreams']:
            print('    sub-stream %-14s %6d Hz %d ch %d bit, %d samples / 0x%X-byte frame, 0x%X bytes a block at +0x%X, '
                  'channel interleave 0x%X' % (f.get('name', ''), f['rate'], f['channels'], f['bits'], f['spf'],
                                               f['frame'], f['bytes_per_block'], f['offset_in_block'],
                                               f['interleave']))
        for i, s in enumerate(d['segments']):
            print('    segment %2d %-16s 0x%08X bytes at +0x%08X  per sub-stream %s' % (
                i, s.get('name', ''), s['size'], s['offset'], ['0x%X' % x for x in s.get('sizes', [])]))


def census(base):
    rows = {}
    bad = 0
    lastflags = {}
    for p in files([base]):
        rel = os.path.relpath(p, base)
        k = kind(p)
        try:
            if k == 'awd':
                b, a = read_awd(p)
                key = os.path.splitext(p)[1].upper()
                r = rows.setdefault(key, [0, 0, set(), set()])
                r[0] += 1
                r[1] += len(a['waves'])
                for w in a['waves']:
                    r[2].add(w['rate'])
                    r[3].add((w['channels'], w['bits'], w['type_index']))
                    fr = b[a['header_size'] + w['data']: a['header_size'] + w['data'] + w['length']]
                    fl = check_frames(fr)
                    lastflags[fl] = lastflags.get(fl, 0) + 1
                    if any(x & 4 for x in fr[1::16]):
                        lastflags['loop start'] = lastflags.get('loop start', 0) + 1
                if not a['file_ok']:
                    bad += 1
                    print('size mismatch:', rel)
            else:
                b, items = read_rws(p)
                key = 'RWS ' + ('stream' if any(i['type'] == 0x80D for i in items) else
                                'chunk 0x%X' % items[0]['type'])
                r = rows.setdefault(key, [0, 0, set(), set()])
                r[0] += 1
                for it in items:
                    if it['type'] != 0x80D:
                        continue
                    d = it['desc']
                    r[1] += len(d['segments'])
                    for f in d['substreams']:
                        r[2].add(f['rate'])
                        r[3].add((f['channels'], f['bits'], f['interleave'], d['block'], len(d['substreams'])))
                    for si in range(len(d['segments'])):
                        for fi in range(len(d['substreams'])):
                            for c in rws_channels(b, it, si, fi):
                                check_frames(c)
        except (BadFrame, ValueError, IndexError, struct.error) as e:
            bad += 1
            print('%s: %s' % (rel, e))
    for k, r in sorted(rows.items()):
        print('%-16s %4d files, %5d %s; rates %s; %s' % (k, r[0], r[1], 'waves' if 'RWS' not in k else 'segments',
                                                        sorted(r[2]), sorted(r[3])))
    print('AWD waves by the flag byte of their last frame: %s' % ', '.join(
        ('0x%X: %d' if isinstance(k, int) else '%s: %d') % (k, v) for k, v in sorted(lastflags.items(), key=str)))
    print('census: %s' % ('every file read, every ADPCM frame header valid' if not bad else '%d problems' % bad))
    return 1 if bad else 0


def main(a):
    if len(a) >= 2 and a[0] == '--census':
        return census(a[1])
    if len(a) >= 4 and a[0] == '--wav':
        if kind(a[1]) == 'awd':
            b, d = read_awd(a[1])
            w = d['waves'][int(a[2])]
            o = d['header_size'] + w['data']
            s, _ = adpcm(b[o:o + w['length']])
            print('%d samples -> %s' % (write_wav(a[3], w['rate'], [s]), a[3]))
            return 0
        b, items = read_rws(a[1])
        it = next(i for i in items if i['type'] == 0x80D)
        f = it['desc']['substreams'][int(a[3])]
        chans = [adpcm(c)[0] for c in rws_channels(b, it, int(a[2]), int(a[3]))]
        print('%d samples a channel -> %s' % (write_wav(a[4], f['rate'], chans), a[4]))
        return 0
    if not a:
        print(__doc__)
        return 2
    base = a[0] if len(a) == 1 and os.path.isdir(a[0]) else None
    for p in files(a):
        show(p, base)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
