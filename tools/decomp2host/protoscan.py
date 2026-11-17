# protoscan.py <gen/decomp/units>: extern "C" declarations whose parameter kinds (P pointer, I integer, F float,
# L 64-bit) or void-ness differ from the definition of the same C name in another generated unit (port8). MWCC
# accepts these (both sides meet only in registers); natively linked they broke (docs/decomp-on-host.md, port8).
# Output: name, caller unit + kinds + return, defining unit + kinds + return.
import re, os, sys, collections
D = sys.argv[1]
decl = collections.defaultdict(list)   # name -> [(unit, params)]
defs = {}
pat = re.compile(r'extern\s*"C"\s+([^;{}()]*?)\b(\w+)\s*\(([^()]*(?:\([^()]*\)[^()]*)*)\)\s*([;{])', re.S)
def norm(p):
    p = re.sub(r'/\*.*?\*/', '', p).strip()
    if p in ('', 'void'): return []
    out = []
    for x in p.split(','):
        x = x.strip()
        x = re.sub(r'\b(const|volatile|struct|class|union|enum)\b', '', x)
        # drop param name
        m = re.match(r'^(.*?[\*&\s])(\w+)\s*$', x)
        t = m.group(1) if m and not re.fullmatch(r'(unsigned|signed|int|char|short|long|float|double|bool|ps2f|void)', m.group(2)) else x
        t = re.sub(r'\s+', '', t)
        kind = 'P' if ('*' in t or '&' in t or 'gp<' in t) else ('F' if t in ('float','ps2f','double') else ('L' if 'long' in t and 'ps2_' in t else 'I'))
        out.append(kind)
    return out
for f in sorted(os.listdir(D)):
    t = open(os.path.join(D, f)).read()
    for m in pat.finditer(t):
        ret, name, params, end = m.group(1), m.group(2), m.group(3), m.group(4)
        k = norm(params)
        rk = 'V' if re.search(r'\bvoid\s*$', ret.strip()) and '*' not in ret else ret.strip()
        if end == '{': defs[name] = (f, k, ret.strip())
        else: decl[name].append((f, k, ret.strip()))
n = 0
for name, ds in sorted(decl.items()):
    if name not in defs: continue
    df, dk, dr = defs[name]
    for f, k, r in ds:
        if f == df: continue
        if k != dk or (r.replace(' ','') != dr.replace(' ','') and ('void' in (r, dr))):
            n += 1
            print("%s\tcaller %s %s ret=%s\tdef %s %s ret=%s" % (name, f, ''.join(k) or '-', r, df, ''.join(dk) or '-', dr))
print(n, "mismatching declarations", file=sys.stderr)
