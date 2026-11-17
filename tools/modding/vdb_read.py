#!/usr/bin/env python3
"""DATA/VDB.XML, the tuning defaults (docs/modding/file-formats/vdb.md). Binary despite its name.

    python3 -I tools/modding/vdb_read.py <disc>/DATA/VDB.XML                 the layout check and counts
    python3 -I tools/modding/vdb_read.py <disc>/DATA/VDB.XML --decomp <decomp>
        also names the keys: hashes every registry name (tools/modding/tuning_registry.py) and every config file
        name the decomp's strings give, and counts how many of the file's keys are matched
    ... --dump        one line per value record: key, raw word, as float / int, its name when known.
                      The output is the game's data: for your own use, never commit or share it.

Layout (all little-endian):
    0x00 u32 version (2)        0x04 u32 value records      0x08 u32 (unknown; read by nothing)
    0x0C u32 file records       0x10 u32 offset of the file records (from the start of the file)
    0x14 value records: { u32 value, s32 key } sorted by key (signed); key = CGtHash(name + group + "/" + file)
         then the blocks of the values that do not fit a word (vectors, arrays): a record's value is their offset
    file records: { u32 enabled, s32 key } sorted by key; key = CGtHash(file)
"""
import os
import re
import struct
import sys

_TAB = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = (_c >> 1) ^ 0xEDB88320 if _c & 1 else _c >> 1
    _TAB.append(_c)


def gthash(s):
    """CGtHash::CalculateHash 0x0021BFA0: CRC-32's table, start -1, an ARITHMETIC shift right, no final inversion.
    Returns the signed 32-bit value (the file's keys are compared as signed)."""
    crc = -1
    for ch in s.encode('latin-1'):
        c = ch - 256 if ch >= 0x80 else ch          # lb: sign-extended (retail then indexes before the table)
        idx = c ^ (crc & 0xFF)
        if idx < 0:
            raise ValueError('a character >= 0x80: retail reads outside its table')
        crc = (crc >> 8) ^ _TAB[idx]
        crc = (crc & 0xFFFFFFFF) - (1 << 32) if crc & 0x80000000 else crc & 0xFFFFFFFF
    return crc


class Vdb:
    def __init__(self, d):
        self.d = d
        if len(d) < 0x14:
            raise ValueError('too short for the header')
        self.version, self.n_values, self.unk8, self.n_files, self.files_off = struct.unpack_from('<5I', d, 0)
        end_values = 0x14 + 8 * self.n_values
        if end_values > len(d) or self.files_off + 8 * self.n_files > len(d) or self.files_off < end_values:
            raise ValueError('the tables are outside the file')
        self.values = {}
        prev = None
        for i in range(self.n_values):
            v, k = struct.unpack_from('<Ii', d, 0x14 + 8 * i)
            if prev is not None and k <= prev:
                raise ValueError('value records not sorted by key at %d' % i)
            prev = k
            self.values[k] = v
        self.files = {}
        prev = None
        for i in range(self.n_files):
            v, k = struct.unpack_from('<Ii', d, self.files_off + 8 * i)
            if prev is not None and k <= prev:
                raise ValueError('file records not sorted by key at %d' % i)
            prev = k
            self.files[k] = v
        self.blocks = (end_values, self.files_off)
        self.file_names = {}
        self.known_files = []

    def find_files(self, cands):
        for c in cands:
            try:
                k = gthash(c)
            except ValueError:
                continue
            if k in self.files and k not in self.file_names:
                self.file_names[k] = c
        self.known_files = sorted(self.file_names.values())

    def in_blocks(self, v):
        return self.blocks[0] <= v < self.blocks[1]

    def show(self, w, typ, count=0):
        if count >= 2 and self.in_blocks(w) and w + 4 * count <= len(self.d):
            f = '<%df' % count if typ == 'float' else '<%di' % count
            return '[%s]' % ', '.join(('%.6g' % x) if typ == 'float' else str(x) for x in struct.unpack_from(f, self.d, w))
        if typ == 'float':
            return '%.6g' % struct.unpack('<f', struct.pack('<I', w))[0]
        if typ == 'int':
            return str(struct.unpack('<i', struct.pack('<I', w))[0])
        if typ == 'vector' and self.in_blocks(w) and w + 16 <= len(self.d):
            return '(%s)' % ', '.join('%.6g' % x for x in struct.unpack_from('<4f', self.d, w))
        return '0x%08X' % w

    def matched_keys(self, rows, vm):
        """How many value keys a registry row names (with its own file, or any named file for a runtime file)."""
        got = set()
        for r in rows:
            if r['name'][0] is None or r['group'][0] is None:
                continue
            files = [r['file'][0]] if r['file'][0] else self.known_files
            for f in files:
                k = vm.key(r, f)
                if k in self.values:
                    got.add(k)
        return len(got)


def _strings(decomp):
    """Every string of the decomp's data listings (the .asciz lines and the bytes of .word runs)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import tuning_registry
    mem, _ = tuning_registry.load_data(decomp)
    out = set()
    cur = bytearray()
    last = None
    for a in sorted(mem):
        b = mem[a]
        if last is not None and a != last + 1:
            cur = bytearray()
        last = a
        if b == 0:
            if 1 <= len(cur) <= 200:
                out.add(cur.decode('ascii'))
            cur = bytearray()
        elif 0x20 <= b <= 0x7E:
            cur.append(b)
        else:
            cur = bytearray()
    return out


CAR_CLASSES = ['COMP', 'CUPE', 'HEVY', 'HSPC', 'MSCL', 'SPRT', 'SUPR', 'TSPC']   # the PVEH/<class> folders
AREAS = {'US': ['C1', 'C2', 'C3', 'C5', 'M1', 'P1', 'P2'], 'AS': ['C1', 'C2', 'C3', 'M1'],
         'EU': ['C1', 'C2', 'C3', 'C4', 'M1', 'M2', 'P1', 'P2']}                 # the TRACKS/<area>/<route> folders


def id_names():
    """The GtID strings the game builds file names from (GtIDConvertToString 0x0021AB40 of a vehicle's or route's
    id): vehicles "<class>CAR<n>" (PVEH/<class>/CAR<n>.*), routes "<area>_<route>_V<1|2>"."""
    out = set()
    for c in CAR_CLASSES:
        out.update('%sCAR%d' % (c, n) for n in range(1, 100))
    for a, rs in AREAS.items():
        for r in rs:
            out.update('%s_%s_V%d' % (a, r, v) for v in (1, 2))
            out.add('%s_%s' % (a, r))
    return out


def file_pattern(f):
    """A config file name with its vehicle / route id replaced by <vehicle> / <route>."""
    f = re.sub(r'/(%s)CAR\d+\.cfg$' % '|'.join(CAR_CLASSES), '/<vehicle>.cfg', f)
    f = re.sub(r'/(US|EU|AS)_[CMP]\d(_V[12])?\.cfg$', '/<route>.cfg', f)
    return f


def file_candidates(decomp):
    """Config file names: literal ones, and the templates ("dir/%s.cfg", "dir/" + x + ".cfg") filled with every
    identifier-like string of the decomp (car, track and surface names live there), in the case the strings have and
    upper / lower case."""
    ss = _strings(decomp)
    lit = [s for s in ss if s.lower().endswith('.cfg') and '%' not in s]
    tmpl = [s for s in ss if '%s' in s and s.lower().endswith('.cfg')]
    dirs = [s for s in ss if 'valuedb' in s.lower() and s.endswith('/')]
    toks = set()
    for s in ss:
        if re.fullmatch(r'[A-Za-z0-9_./ -]{1,40}', s):
            toks.update({s, s.upper(), s.lower()})
            if '.' in s:
                base = s.rsplit('.', 1)[0]
                toks.update({base, base.upper(), base.lower()})
            if '/' in s:
                tail = s.rstrip('/').rsplit('/', 1)[-1]
                toks.update({tail, tail.upper(), tail.lower()})
    toks.update(id_names())
    out = list(lit)
    for t in tmpl:
        out += [t.replace('%s', x, 1) for x in toks]
    for dd in dirs:
        out += [dd + x + '.cfg' for x in toks]
    return out


def main(argv):
    if len(argv) < 2 or argv[1] in ('-h', '--help'):
        print(__doc__)
        return 2
    v = Vdb(open(argv[1], 'rb').read())
    print('VDB.XML: %d bytes; version %d; %d value records; word 0x08 = %d; %d file records at 0x%X' % (
        len(v.d), v.version, v.n_values, v.unk8, v.n_files, v.files_off))
    print('  value records 0x14..0x%X, sorted by key (signed); blocks 0x%X..0x%X (%d bytes); file records end at 0x%X%s' % (
        v.blocks[0], v.blocks[0], v.blocks[1], v.blocks[1] - v.blocks[0], v.files_off + 8 * v.n_files,
        ' = the file size' if v.files_off + 8 * v.n_files == len(v.d) else ' (bytes follow)'))
    to_blocks = sum(1 for w in v.values.values() if v.in_blocks(w) and (w - v.blocks[0]) % 4 == 0)
    print('  values that point into the blocks (vector / array candidates): %d' % to_blocks)
    print('  file records: enabled values %s' % sorted(set(v.files.values())))
    if '--decomp' in argv:
        decomp = argv[argv.index('--decomp') + 1]
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import tuning_registry
        rows = tuning_registry.scan(decomp)
        v.find_files(file_candidates(decomp))
        vm = tuning_registry.VdbMatch.__new__(tuning_registry.VdbMatch)
        vm.v = v
        print('  file records named from the decomp\'s strings: %d of %d' % (len(v.file_names), v.n_files))
        print('  value keys named by the registry: %d of %d' % (v.matched_keys(rows, vm), v.n_values))
        if '--dump' in argv:
            names = {}
            for r in rows:
                if r['name'][0] is None or r['group'][0] is None:
                    continue
                for f in ([r['file'][0]] if r['file'][0] else v.known_files):
                    k = vm.key(r, f)
                    if k in v.values:
                        names[k] = (r, f)
            for k in sorted(v.values):
                w = v.values[k]
                n = names.get(k)
                desc = '%s%s/%s [%s]' % (n[0]['name'][0], n[0]['group'][0], n[1], n[0]['type']) if n else ''
                print('%08X %08X %-14s %s' % (k & 0xFFFFFFFF, w, v.show(w, n[0]['type'] if n else 'float'), desc))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
