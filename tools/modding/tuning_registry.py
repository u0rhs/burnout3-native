#!/usr/bin/env python3
"""The tuning registry: every variable the game registers with GtComms::CGtCommsDatabase::AddEntry.

Reads the decomp (its C sources for the call sites, its data listings for the strings) and prints a Markdown table:
one row per AddEntry call site, in the order of the registering functions' addresses and, within one, the source order
(retail's call order). Output committed as docs/modding/file-formats/tuning-registry.md (the table part).

    python3 -I tools/modding/tuning_registry.py <decomp> > table.md
    python3 -I tools/modding/tuning_registry.py <decomp> --vdb <disc>/DATA/VDB.XML --stats
        also reports which rows have a default in the user's VDB.XML (the key is CGtHash of name + group + "/" + file;
        docs/modding/file-formats/vdb.md). --values adds those defaults to the table: for your own use only, never
        commit that output (it is the game's data).
    python3 -I tools/modding/tuning_registry.py <decomp> --vdb <disc>/DATA/VDB.XML --names-table [--cxx]
        the names table of `b3modtool vdb export` (names, groups, config files, types; no values); --cxx wraps it
        as src/b3modtool/vdb_names.inc (committed). The VDB.XML only gives the per-vehicle / per-route file patterns
        and the config file names its file records match.

The three entry points [C config/symbol_addrs.txt]:
    0x0021BB60 AddEntry(float *, name, group, file, int flags, float min, float max, int count)
    0x0021BD80 AddEntry(int *,   name, group, file, int flags, int min,   int max,   int count)
    0x0021B9B0 AddEntry(CGtV3d *, name, group, file)
No game data is read from the disc unless --vdb is given; the strings come from the decomp's data listings.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vdb_read  # noqa: E402

FN = {'Pf': ('float', '0x0021BB60'), 'Pi': ('int', '0x0021BD80'), 'PQ': ('vector', '0x0021B9B0')}
CALL = re.compile(r'AddEntry__Q27GtComms16CGtCommsDatabaseF(Pf|Pi|PQ)\w*\s*\(')


def load_data(decomp):
    """Address -> byte, from the comments of assembly/asm/data/**/*.s; plus label -> address."""
    mem = {}
    labels = {}
    root = os.path.join(decomp, 'assembly', 'asm', 'data')
    head = re.compile(r'\s*/\* [0-9A-F]+ ([0-9A-F]{8})(?: ([0-9A-F]+))? \*/\s*\.(\w+)\s*(.*)')
    hexonly = re.compile(r'\s*/\* ([0-9A-F]+) \*/\s*$')
    for dp, _, fn in os.walk(root):
        for f in sorted(fn):
            if not f.endswith('.s'):
                continue
            pending = None
            for line in open(os.path.join(dp, f), errors='replace'):
                m = re.match(r'\s*dlabel (\S+)', line)
                if m:
                    pending = ('label', m.group(1))
                    continue
                m = head.match(line)
                if m:
                    va = int(m.group(1), 16)
                    if pending and pending[0] == 'label':
                        labels.setdefault(pending[1], va)
                    pending = None
                    hx, d, val = m.group(2), m.group(3), m.group(4)
                    if hx:
                        for i in range(0, len(hx), 2):
                            mem[va + i // 2] = int(hx[i:i + 2], 16)
                    elif d == 'byte':
                        mem[va] = int(val.split()[0], 0) & 0xFF
                    elif d == 'short':
                        v = int(val.split()[0], 0)
                        mem[va], mem[va + 1] = v & 0xFF, (v >> 8) & 0xFF
                    elif d in ('asciz', 'ascii'):
                        pending = ('str', va)
                    continue
                m = hexonly.match(line)
                if m and pending and pending[0] == 'str':
                    hx = m.group(1)
                    for i in range(0, len(hx), 2):
                        mem[pending[1] + i // 2] = int(hx[i:i + 2], 16)
                    pending = None
    return mem, labels


def load_symbols(decomp):
    sym = {}
    for f in ('symbol_addrs.txt', 'symbol_addrs_b3names.txt'):
        p = os.path.join(decomp, 'config', f)
        if not os.path.exists(p):
            continue
        for line in open(p, errors='replace'):
            m = re.match(r'\s*(\w+)\s*=\s*0x([0-9A-Fa-f]+)\s*;', line)
            if m:
                sym.setdefault(m.group(1), int(m.group(2), 16))
    return sym


class Img:
    def __init__(self, decomp):
        self.mem, self.labels = load_data(decomp)
        self.sym = load_symbols(decomp)
        self.addr_name = {}
        for k, v in self.sym.items():
            self.addr_name.setdefault(v, k)

    def addr(self, name):
        m = re.fullmatch(r'D_([0-9A-Fa-f]{8})', name)
        if m:
            return int(m.group(1), 16)
        return self.labels.get(name, self.sym.get(name))

    def cstr(self, a):
        out = bytearray()
        while a in self.mem and self.mem[a] != 0 and len(out) < 512:
            out.append(self.mem[a])
            a += 1
        if a not in self.mem or not out:
            return None
        if any(c < 0x20 or c > 0x7E for c in out):
            return None
        return out.decode('ascii')

    def word(self, a):
        if all(a + i in self.mem for i in range(4)):
            return struct.unpack('<I', bytes(self.mem[a + i] for i in range(4)))[0]
        return None

    def string_arg(self, expr):
        """A string argument -> (text, None) or (None, why)."""
        e = re.sub(r'\((?:const\s+)?(?:int|char|void|unsigned\s+char)\s*\*?\s*\)', '', expr).strip()
        e = e.strip('() ')
        m = re.fullmatch(r'&\s*(\w+)\s*\+\s*(0x[0-9A-Fa-f]+|\d+)', e)   # a string inside a labelled block
        if m and self.addr(m.group(1)) is not None:
            s = self.cstr(self.addr(m.group(1)) + int(m.group(2), 0))
            if s is not None:
                return s, None
        m = re.fullmatch(r'(&?)\s*(\w+)((?:\.field_0)|(?:->field_0))?', e)
        if not m:
            return None, 'runtime: `' + expr.strip() + '`'
        amp, name, deref = m.groups()
        a = self.addr(name)
        if a is None:
            return None, 'runtime: `' + expr.strip() + '`'
        if deref:
            p = self.word(a)
            s = self.cstr(p) if p else None
            return (s, None) if s is not None else (None, 'runtime: `' + expr.strip() + '`')
        s = self.cstr(a)
        if not amp:
            # a "const char *" variable (kpcGroupCar): its word points at a string. A char array's first bytes can
            # read as a pointer too, so the pointer wins only over a string of under four characters.
            p = self.word(a)
            s2 = self.cstr(p) if p and 0x00100000 <= p < 0x02000000 else None
            if s2 is not None and (s is None or len(s) < 4):
                return s2, None
        if s is not None:
            return s, None
        return None, 'runtime: `' + expr.strip() + '`'


def split_args(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    out.append(cur.strip())
    return out


def is_float_lit(t):
    return bool(re.fullmatch(r'-?\d+\.\d*(?:e-?\d+)?f?|-?\d+f|-?\.\d+f?', t.strip()))


def num(t):
    t = t.strip().rstrip('f')
    try:
        return int(t, 0)
    except ValueError:
        try:
            return float(t)
        except ValueError:
            return None


def fmt_num(v):
    if v is None:
        return '?'
    if isinstance(v, float):
        s = ('%.6g' % v)
        return s
    return str(v)


def scan(decomp):
    """Every call site in src/: dicts in (function address, source order)."""
    img = Img(decomp)
    rows = []
    src = os.path.join(decomp, 'src')
    seen_fn = {}
    for dp, _, fn in os.walk(src):
        for f in sorted(fn):
            if not f.endswith('.cpp') or f.startswith('vdbRegister'):
                continue
            path = os.path.join(dp, f)
            text = open(path, errors='replace').read()
            if not CALL.search(text):
                continue
            lines = text.split('\n')
            fn_addr = None
            stores = {}
            i = 0
            order = 0
            blocks = []          # per open brace: does it open a loop body
            loop_head = False
            while i < len(lines):
                line = lines[i]
                code = line.split('//')[0]
                if re.match(r'\s*(for|while|do)\b', code):
                    loop_head = True
                in_loop = any(blocks) or (loop_head and '{' not in code and CALL.search(code) is not None)
                for ch in code:
                    if ch == '{':
                        blocks.append(loop_head)
                        loop_head = False
                    elif ch == '}' and blocks:
                        blocks.pop()
                if code.strip().endswith(';') and '{' not in code and not CALL.search(code):
                    loop_head = False
                m = re.match(r'// 0x00([0-9A-F]{6})\s*$', line)
                if m:
                    fn_addr = int(m.group(1), 16)
                    stores = {}
                m = re.match(r'\s*([\w\->.\[\]&:]+)\s*=\s*(-?[\d.]+f?|-?0x[0-9A-Fa-f]+)\s*;', line)
                if m:
                    stores[m.group(1)] = m.group(2)
                m = CALL.search(line)
                if not m or line.lstrip().startswith(('extern', '//')) or 'extern "C"' in line:
                    i += 1
                    continue
                call = line[m.end():]
                j = i
                depth = 1 + call.count('(') - call.count(')')
                while depth > 0 and j + 1 < len(lines):
                    j += 1
                    call += ' ' + lines[j].strip()
                    depth += lines[j].count('(') - lines[j].count(')')
                k = 0
                depth = 1
                while k < len(call):
                    if call[k] == '(':
                        depth += 1
                    elif call[k] == ')':
                        depth -= 1
                        if depth == 0:
                            break
                    k += 1
                args = split_args(call[:k])
                kind = m.group(1)
                order += 1
                rows.append(make_row(img, kind, args, fn_addr, path[len(decomp) + 1:], order, stores,
                                     in_loop))
                i = j + 1
    for r in rows:
        r['fn_name'] = img.addr_name.get(r['fn_addr'], 'func_%08X' % r['fn_addr'] if r['fn_addr'] else '?')
    rows.sort(key=lambda r: (r['fn_addr'] or 0, r['order']))
    return rows


def make_row(img, kind, args, fn_addr, src, order, stores, in_loop):
    typ, entry = FN[kind]
    r = {'type': typ, 'entry': entry, 'fn_addr': fn_addr, 'src': src, 'order': order, 'loop': in_loop}
    var = args[1] if len(args) > 1 else '?'
    r['var'] = var
    strs = []
    for a in args[2:5]:
        s, why = img.string_arg(a)
        strs.append((s, why))
    r['name'], r['group'], r['file'] = strs + [(None, '?')] * (3 - len(strs))
    rest = args[5:]
    r['flags'] = r['min'] = r['max'] = r['count'] = None
    if kind == 'Pf':
        ints = [t for t in rest if not is_float_lit(t)]
        fl = [t for t in rest if is_float_lit(t)]
        if len(ints) == 2 and len(fl) == 2:
            r['flags'], r['count'] = num(ints[0]), num(ints[1])
            r['min'], r['max'] = num(fl[0]), num(fl[1])
        else:
            r['raw'] = ', '.join(rest)
    elif kind == 'Pi':
        if len(rest) == 4:
            r['flags'], r['min'], r['max'], r['count'] = (num(t) for t in rest)
        else:
            r['raw'] = ', '.join(rest)
    # the default from code: a literal stored to the variable earlier in the same function, or the variable's
    # initialiser in the executable's data (a global)
    lv = var.lstrip('&').strip()
    d = None
    if lv in stores:
        d = ('code', stores[lv])
    else:
        m = re.fullmatch(r'(?:\w+::)?(\w+)', lv)
        a = img.addr(m.group(1)) if m else None
        if a is not None and typ in ('float', 'int'):
            w = img.word(a)
            if w is not None:
                v = struct.unpack('<f', struct.pack('<I', w))[0] if typ == 'float' else struct.unpack('<i', struct.pack('<I', w))[0]
                d = ('data', fmt_num(float(v)) if typ == 'float' else str(v))
    r['default'] = d
    return r


def md_escape(s):
    return s.replace('|', '\\|')


def table(rows, vdb=None, values=False):
    out = []
    hdr = '| # | Group | Name | Type | Range | Flags / count | Default (code) | File | Variable | Registered by | In VDB.XML |'
    if values:
        hdr += ' VDB.XML value |'
    out.append(hdr)
    out.append('|' + '---|' * (hdr.count('|') - 1))
    for n, r in enumerate(rows, 1):
        def cell(x):
            s, why = x
            return md_escape(s) if s is not None else md_escape(why)
        rng = ''
        if r['type'] != 'vector' and r['min'] is not None and (r['min'] or r['max']):
            rng = '%s .. %s' % (fmt_num(r['min']), fmt_num(r['max']))
        fc = ''
        if r['type'] != 'vector':
            if 'raw' in r:
                fc = 'args: ' + md_escape(r['raw'])
            else:
                fc = '%s / %s' % (fmt_num(r['flags']), fmt_num(r['count']))
        dflt = ''
        if r['default']:
            dflt = r['default'][1].rstrip('f') + (' (store)' if r['default'][0] == 'code' else ' (data)')
        fn = '%s `0x%08X`' % (md_escape(r['fn_name']), r['fn_addr'] or 0)
        vd = ''
        val = ''
        filecell = cell(r['file'])
        if vdb is not None:
            hit = vdb.lookup_row(r)
            vd = hit[0]
            val = hit[1]
            if r.get('file_patterns'):
                filecell = '<br>'.join(md_escape(x) for x in r['file_patterns']) + ' (built at run time)'
        line = '| %d | %s | %s | %s | %s | %s | %s | %s | `%s` | %s | %s |' % (
            n, cell(r['group']), cell(r['name']), r['type'], rng, fc, dflt, filecell, md_escape(r['var']), fn, vd)
        if values:
            line += ' %s |' % val
        out.append(line)
    return '\n'.join(out) + '\n'


class VdbMatch:
    """Looks a registry row up in the user's VDB.XML."""

    def __init__(self, path, rows):
        self.v = vdb_read.Vdb(open(path, 'rb').read())
        # the files' names: every literal file of the rows, plus the templates' expansions found in the file table
        self.files = sorted({r['file'][0] for r in rows if r['file'][0]})

    def key(self, r, file):
        return vdb_read.gthash(r['name'][0] + r['group'][0] + ('' if r['group'][0].endswith('/') else '/') + file)

    def lookup_row(self, r):
        if r['name'][0] is None or r['group'][0] is None:
            return 'name not static', ''
        cands = [r['file'][0]] if r['file'][0] else self.v.known_files
        hits = [f for f in cands if self.key(r, f) in self.v.values]
        if not hits:
            return 'no', ''
        if r['file'][0]:
            r['vdb_hits'] = 1
            return 'yes', self.v.show(self.v.values[self.key(r, hits[0])], r['type'], r.get('count') or 0)
        r['vdb_hits'] = len(hits)
        pats = {}
        for f in hits:
            pats.setdefault(vdb_read.file_pattern(f), []).append(f)
        r['file_patterns'] = sorted(pats)
        return 'yes (%d files)' % len(hits), ''


def summary(rows, with_vdb):
    """One row per registering function: its entries by type, its config files, its top-level groups."""
    out = ['| Registered by | Address | Entries | float / int / vector | Config file(s) | Groups (first part) |' +
           (' In VDB.XML |' if with_vdb else '')]
    out.append('|' + '---|' * (out[0].count('|') - 1))
    fns = []
    for r in rows:
        if r['fn_name'] not in fns:
            fns.append(r['fn_name'])
    for f in fns:
        rs = [r for r in rows if r['fn_name'] == f]
        files = []
        for r in rs:
            for x in (r.get('file_patterns') or [r['file'][0] or r['file'][1]]):
                x = x.replace('../Export/ValueDB/', '').replace('../export/ValueDB/', '')
                if x not in files:
                    files.append(x)
        groups = []
        for r in rs:
            g = (r['group'][0] or '?').split('/')[0] or '?'
            if g not in groups:
                groups.append(g)
        n = lambda t: sum(1 for r in rs if r['type'] == t)
        line = '| %s | `0x%08X` | %d | %d / %d / %d | %s | %s |' % (
            md_escape(f), rs[0]['fn_addr'] or 0, len(rs), n('float'), n('int'), n('vector'),
            ', '.join(md_escape(x) for x in files[:6]) + (' ...' if len(files) > 6 else ''),
            ', '.join(md_escape(g) for g in groups[:6]) + (' ...' if len(groups) > 6 else ''))
        if with_vdb:
            line += ' %d of %d |' % (sum(1 for r in rs if r.get('vdb_hits')), len(rs))
        out.append(line)
    tot = '| **all** | | **%d** | %d / %d / %d | | |' % (
        len(rows), *(sum(1 for r in rows if r['type'] == t) for t in ('float', 'int', 'vector')))
    if with_vdb:
        tot += ' %d of %d |' % (sum(1 for r in rows if r.get('vdb_hits')), len(rows))
    out.append(tot)
    return '\n'.join(out) + '\n'


def names_table(rows, vdb):
    """The names table b3modtool's `vdb export` uses (src/b3modtool/vdb_names.inc): names, groups, config files and
    types, no values. Lines (tab-separated):
        v <type> <count> <group> <name> <file>     a registration; <file> may hold <vehicle> / <route> (vdb.md)
        f <file>                                    a config file name (VDB.XML's file records)"""
    out = ['# b3modtool VDB.XML names (tools/modding/tuning_registry.py --names-table): the tuning registry\'s',
           '# names, groups, config files and types, from the decomp\'s registrations; no game values.',
           '# v <type> <count> <group> <name> <file>   (tab-separated; <vehicle> / <route> are filled at run time)',
           '# f <file>']
    seen = set()
    for r in rows:
        if r['name'][0] is None or r['group'][0] is None:
            continue
        files = [r['file'][0]] if r['file'][0] else r.get('file_patterns', [])
        for f in files:
            line = 'v\t%s\t%d\t%s\t%s\t%s' % (r['type'], r['count'] or 0, r['group'][0], r['name'][0], f)
            for part in (r['group'][0], r['name'][0], f):
                if '\t' in part or part != part.strip():
                    raise ValueError('a name with a tab or an outer space: %r' % part)
            if line not in seen:
                seen.add(line)
                out.append(line)
    for f in sorted(set(vdb.v.file_names.values()) | {r['file'][0] for r in rows if r['file'][0]}):
        out.append('f\t' + f)
    return '\n'.join(out) + '\n'


def main(argv):
    if len(argv) < 2 or argv[1] in ('-h', '--help'):
        print(__doc__)
        return 2
    decomp = argv[1]
    vdbp = argv[argv.index('--vdb') + 1] if '--vdb' in argv else None
    rows = scan(decomp)
    vdb = None
    if vdbp:
        vdb = VdbMatch(vdbp, rows)
        vdb.v.find_files(vdb_read.file_candidates(decomp))
    if '--names-table' in argv:
        if vdb is None:
            sys.stderr.write('--names-table needs --vdb (the per-vehicle / per-route file patterns come from it)\n')
            return 2
        for r in rows:
            vdb.lookup_row(r)
        t = names_table(rows, vdb)
        if '--cxx' in argv:
            t = ('// Generated by tools/modding/tuning_registry.py <decomp> --vdb <VDB.XML> --names-table --cxx: the\n'
                 '// tuning registry\'s names (no game values) for b3modtool vdb (src/b3modtool/vdb.cpp).\n'
                 'R"B3NAMES(' + t + ')B3NAMES"\n')
        sys.stdout.write(t)
        return 0
    full = table(rows, vdb, '--values' in argv)
    sys.stdout.write(summary(rows, vdb is not None) + '\n## Every registration\n\n' +
                     'In the order of the registering functions\' addresses, then retail\'s call order.\n\n' + full)
    if '--stats' in argv:
        by = {}
        for r in rows:
            by.setdefault(r['fn_name'], 0)
            by[r['fn_name']] += 1
        st = sys.stderr
        st.write('%d call sites in %d functions\n' % (len(rows), len(by)))
        for k in ('float', 'int', 'vector'):
            st.write('  %s: %d\n' % (k, sum(1 for r in rows if r['type'] == k)))
        st.write('  static name+group: %d, static file: %d\n' % (
            sum(1 for r in rows if r['name'][0] and r['group'][0]), sum(1 for r in rows if r['file'][0])))
        if vdb:
            hit = [r for r in rows if r.get('vdb_hits')]
            st.write('  rows with a default in VDB.XML: %d (VDB.XML keys matched to names: %d of %d)\n' % (
                len(hit), vdb.v.matched_keys(rows, vdb), len(vdb.v.values)))
            st.write('  VDB.XML file entries named: %d of %d\n' % (len(vdb.v.file_names), len(vdb.v.files)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
