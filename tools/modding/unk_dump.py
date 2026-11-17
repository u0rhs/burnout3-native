#!/usr/bin/env python3
"""Structural summary of the disc files that have no decoder in the port yet (docs/modding/file-formats/: kfs.md,
bin-data.md, gamedata-bgd.md, lists.md). Reads the user's own disc folder; writes nothing; prints counts, IDs and
offsets so the layouts in those pages can be checked against every file.

    python3 -I tools/modding/unk_dump.py <disc folder> [lists|kfs|loadscrn|stagehed|prgdata|gamedata|all]

IDs are GtIDs: 12 characters packed base 40 into a u64 (GtIDUnCompress 0x21ADF0)."""
import glob
import os
import struct
import sys


def gtid(v):
    s = ''
    for _ in range(12):
        r = v % 40
        v //= 40
        s = ('_' if r == 39 else chr(r - 13 + ord('A')) if r >= 13 else chr(r - 3 + ord('0')) if r >= 3 else ' -/'[r]) + s
    return s.rstrip()


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def u64(b, o):
    return struct.unpack_from('<Q', b, o)[0]


def read(disc, rel):
    p = os.path.join(disc, rel)
    with open(p, 'rb') as f:
        return f.read()


def lists(disc):
    # CB3VehicleList / CB3TrackList (B3DataLists.cpp 0x251870..0x252000): +4 count, +8 s32[128], +0x208 s32[128],
    # +0x408 u64 id[]; the game reads exactly 0x1000 bytes (func_00133190 states 4..6)
    for rel, a, b in (('PVEH/VLIST.BIN', 'driveable', 'boost bar size'), ('TRACKS/TLIST.BIN', 'point to point', 'right-hand drive')):
        d = read(disc, rel)
        n = u32(d, 4)
        print('%s: %d bytes, +0 %d, %d entries' % (rel, len(d), u32(d, 0), n))
        for i in range(n):
            print('  %3d %-12s %s %d  %s %d' % (i, gtid(u64(d, 0x408 + 8 * i)), a, u32(d, 8 + 4 * i), b, u32(d, 0x208 + 4 * i)))


def directory(d, entry_base):
    # the 0x800-byte directory of KFS / LOADSCRN.BIN: +0 count, +8 rows {u64 id, u32 size, u32 offset}
    n = u32(d, 0)
    rows = [struct.unpack_from('<QII', d, 8 + 16 * i) for i in range(n)]
    for i, (k, size, off) in enumerate(rows):
        start = entry_base + off
        print('  %3d %-12s size 0x%06x at 0x%07x%s' % (i, gtid(k), size, start,
                                                     '' if start + size <= len(d) else ' (runs %d bytes past the end)' % (start + size - len(d))))
    return rows


def kfs(disc):
    for rel in ('DATA/INTRO.KFS', 'DATA/AFTER.KFS'):
        d = read(disc, rel)
        print('%s: %d bytes, %d entries' % (rel, len(d), u32(d, 0)))
        for k, size, off in directory(d, 0x800):
            e = 0x800 + off
            if e + 16 <= len(d):
                print('      +0 %d, keyframes %d, bytes 0x%x' % (u32(d, e), u32(d, e + 4), u32(d, e + 8)))


def loadscrn(disc):
    d = read(disc, 'DATA/LOADSCRN.BIN')
    print('DATA/LOADSCRN.BIN: %d bytes, %d entries' % (len(d), u32(d, 0)))
    for k, size, off in directory(d, 0x800):
        e = 0x800 + off
        print('      dictionary header %08x %08x, textures %d' % (u32(d, e), u32(d, e + 4), u32(d, e + 8)))


def stagehed(disc):
    d = read(disc, 'DATA/STAGEHED.BIN')
    n = u32(d, 0x260)
    print('DATA/STAGEHED.BIN: %d bytes, +0 %d, %d records of 0x3E0 at 0x278' % (len(d), u32(d, 0), n))
    for j in range(n):
        r = 0x278 + j * 0x3E0
        cars = ['%s %s' % (gtid(u64(d, r + 0x98 * s)), gtid(u64(d, r + 0x98 * s + 8))) for s in range(6)]
        print('  %3d %-10s %-12s index %d  slots: %s' % (j, gtid(u64(d, r + 0x390)), gtid(u64(d, r + 0x398)), u32(d, r + 0x3C0),
                                                       ', '.join(c.strip() or '-' for c in cars)))


def prgdata(disc):
    # CB3ProgressionDataStruct::Fixup 0x367E70: +4 off groups[+8] {off series[], count}; series 0x18 {.., +0x10 off
    # events[], +0x14 count}; event 0x40 {.., +0x34 off stage ref}; +0xC off of a second table
    d = read(disc, 'DATA/PRGDATA.BIN')
    print('DATA/PRGDATA.BIN: %d bytes, +0 %d, groups %d at 0x%x, table at 0x%x (%d rows), +0x14 0x%x' % (
        len(d), u32(d, 0), u32(d, 8), u32(d, 4), u32(d, 12), u32(d, 16), u32(d, 20)))
    g = u32(d, 4)
    for i in range(u32(d, 8)):
        so, sc = u32(d, g + 8 * i), u32(d, g + 8 * i + 4)
        print('  group %d: %d series' % (i, sc))
        for j in range(sc):
            s = so + 0x18 * j
            eo, ec = u32(d, s + 0x10), u32(d, s + 0x14)
            refs = []
            for k in range(ec):
                p = u32(d, eo + 0x40 * k + 0x34)
                refs.append('%s/%s' % (gtid(u64(d, p)), gtid(u64(d, p + 8))))
            print('    series %d %-6s %2d events: %s' % (j, gtid(u64(d, s)) or '-', ec, ' '.join(refs)))
    t = u32(d, 12)
    for k in range(u32(d, 16)):
        r = t + 24 * k
        print('  table %2d %-12s %d %d %d %d' % (k, gtid(u64(d, r)) or '-', u32(d, r + 8), u32(d, r + 12), u32(d, r + 16), u32(d, r + 20)))


def gamedata(disc):
    # CB3GameData::Prepare 0x14F050: 0x800 directory {+8 u64 id[50], +0x198 s32 header offset[50], +0x260 count}; per
    # stage a 0x800 header with (size, offset) pairs at +0x3BC..+0x3D8
    total = 0
    for p in sorted(glob.glob(os.path.join(disc, 'TRACKS', '*', '*', 'GAMEDATA.BGD'))):
        d = open(p, 'rb').read()
        n = u32(d, 0x260)
        total += n
        print('%s: %d bytes, +0 %d +4 0x%x, %d stages' % (os.path.relpath(p, disc), len(d), u32(d, 0), u32(d, 4), n))
        for i in range(n):
            h = u32(d, 0x198 + 4 * i)
            sz = [u32(d, h + x) for x in range(0x3BC, 0x3DC, 4)]
            t = sz[1]
            print('  %2d %-12s header 0x%06x track 0x%x+0x%x traffic 0x%x+0x%x lanes 0x%x+0x%x ai 0x%x+0x%x | +0x3B4 %d '
                  'lines %d+%d splits %d pickups %d ramps %d' % (
                      i, gtid(u64(d, 8 + 8 * i)), h, sz[1], sz[0], sz[3], sz[2], sz[5], sz[4], sz[7], sz[6], u32(d, h + 0x3B4),
                      u32(d, t + 0x3EC), u32(d, t + 0x3F0), u32(d, t + 0x3F4), u32(d, t + 0x3F8), u32(d, t + 0x3FC)))
    print('gamedata: %d stages in all' % total)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    disc, what = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'all'
    table = {'lists': lists, 'kfs': kfs, 'loadscrn': loadscrn, 'stagehed': stagehed, 'prgdata': prgdata, 'gamedata': gamedata}
    for name, fn in table.items():
        if what in (name, 'all'):
            fn(disc)
    return 0


if __name__ == '__main__':
    sys.exit(main())
