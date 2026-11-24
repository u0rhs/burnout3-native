#!/usr/bin/env python3
"""split_census.py -- every access in the generated units to the game's per-car arrays and pools (MORE RIVALS; the
census behind decomp2host's split_arrays.py pass, and a report of its own).

The game's car count (6) is designed into fixed per-car arrays inline in its objects (the world, each race car, its
score, the physics manager, the traffic system ...). A race with more cars needs every access to them found. The
generated units (gen/native/units) declare each object as a struct whose fields are named by their byte offset
(field_<HEX>), so an access is found by the struct's class and the field's offset:

  1. the classes: a unit's struct is a class when it is the `self` of a mangled member (Name__<n><Class>F...), when it
     is named as the class, when it is the type of a guest global at a known object's address (a global inside the
     world is the world at that offset), or when it is the type of a field of a struct of known class at an offset
     the seed table below names (the world's car list holds race cars, a race car's score sits at +0x10C0 ...);
  2. the targets (config/split_arrays.tsv): (class, offset, element size, count) per array;
  3. each struct field that overlaps a target is a site: its declaration (an array, an open array, a scalar element),
     how the unit uses it (subscripts, decays), the confidence of the class (H: self / name / global, M: through a
     field, L: offset and shape only);
  4. raw byte arithmetic (`D_00522660 + 0x12FCF0 + i * 0x2460`, `pcast<char>(self) + 0x1894`) on a known object, and
     constant loop bounds `< 6` / `< 5` in the units with a site.

Usage: split_census.py [--units gen/native/units] [--targets config/split_arrays.tsv] [--discover] [--out FILE]
  --discover  also list every array of 5 / 6 / 8 elements in a struct of known class (to find targets).
"""
import argparse
import collections
import os
import re
import sys

SEED_GLOBALS = {   # guest address -> (class, size)
    0x00522660: ("CB3World", 0x143860),
    0x00665EC0: ("CB3GraphicsManager", 0x10000),
    0x0051AD00: ("StageRecord", 0x1C0),
    0x01E900B0: ("CB3MenuChoices", 0x200),
    0x0066A3D0: ("CB3HUDManager", 0x3000),
    0x01D9D200: ("CB3EffectsManager", 0x8000),
    0x01E77550: ("CB3SoundRaceCarManager", 0x1000),
    0x01E77540: ("CB3SoundGameModeManager", 0x10),
    0x004EE040: ("CB3CameraSystem", 0x2B000),
    0x0066DEB0: ("AIPreRaceRpmSpikes", 0xA8),   # _grSpikeGen: CB3AiCarPreRaceRpmSpike[6] (0x1C), by car index
}
# (class, offset) -> (member class, inline?, size)  inline: the member object sits at that offset; else a pointer
SEED_MEMBERS = {
    ("CB3World", 0x12AD28): ("CB3RaceCar", False, 4),
    ("CB3World", 0x12AD50): ("CB3RaceCar", True, 0x27D0 * 2),    # player cars x2, stride 0x27D0
    ("CB3World", 0x12FCF0): ("CB3RaceCar", True, 0x2460 * 5),    # AI cars x5, stride 0x2460 (CB3AICar)
    ("CB3World", 0x13B2D0): ("CB3RaceCar", True, 0x1AA0 * 5),    # network cars x5, stride 0x1AA0
    ("CB3World", 0x3C910): ("CB3PhysicsManager", True, 0x40000),  # [G] size
    ("CB3World", 0x4DE0): ("CB3TrafficSystem", True, 0x37B30),
    ("CB3World", 0x125B40): ("CB3GameData", True, 0x51DC),
    ("CB3World", 0x3C888): ("CB3ReplaySystem", True, 0x88),
    ("CB3World", 0x1260D0): ("CB3RacePosition", True, 0x2124),
    ("CB3World", 0x3C2E8): ("CB3CrashActionDirector", True, 0x5A0),
    ("CB3World", 0x3C2E0): ("CB3CrashPresentationDirector", True, 0x8),
    ("CB3RaceCar", 0x10C0): ("CB3Score", True, 0x6B0),            # [G] size
    ("CB3Score", 0xC8): ("CB3RaceCar", False, 4),
    ("CB3Score", 0x124): ("CB3ScoreBonus", True, 0x158),
}
POOLS = {   # (class, offset) -> stride: an element index beyond the count lands on the next object
    ("CB3World", 0x12AD50): 0x27D0,
    ("CB3World", 0x12FCF0): 0x2460,
    ("CB3World", 0x13B2D0): 0x1AA0,
}

SIZES = {"unsigned char": 1, "signed char": 1, "char": 1, "bool": 1, "short": 2, "unsigned short": 2,
         "int": 4, "unsigned int": 4, "ps2f": 4, "float": 4, "b3host::gaddr": 4, "b3host::ps2_ulong": 8,
         "long long": 8, "CGtV3d": 16, "CGtV4d": 16}

STRUCT_RE = re.compile(r"\b(struct|class)\s+(\w+)\s*(?::\s*(?:public\s+)?(\w+))?\s*\{")
FIELD_RE = re.compile(r"([A-Za-z_][\w:<>,\s\*]*?)\s*\b(field|pad)_([0-9A-Fa-f]+)\s*((?:\[[^\]]*\]\s*)*)\s*;")
GLOBAL_RE = re.compile(r"#define\s+([A-Za-z_]\w*)\s+\((&?)b3host::guest_global<(.+?)>\(0x([0-9A-Fa-f]+)u\)\)")
SELF_ANY_RE = re.compile(r"\b(func_[0-9A-Fa-f]{8}|\w+?__\d+\w+?F\w*)\s*\(\s*(?:const\s+)?(\w+)\s*\*\s*self\b")
SELF_RE = re.compile(r"(?:extern \"C\"\s+)?[\w:<>\*\s]+?\b(\w+?)__(\d+)(\w+?)F\w*\s*\(\s*(?:const\s+)?(\w+)\s*\*\s*self\b")
METHOD_RE = re.compile(r"^[\w:<>\*\s]+?\b(\w+)::(\w+)\s*\(", re.M)


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", " ", t, flags=re.S)
    return re.sub(r"//[^\n]*", "", t)


def elem_type(tdecl):
    t = " ".join(tdecl.split())
    m = re.match(r"b3host::open_array<(.*)>$", t)
    open_ = bool(m)
    if m:
        t = m.group(1).strip()
    m = re.match(r"b3host::gp<(.*)>$", t)
    if m:
        return "ptr", m.group(1).strip(), 4, open_
    t = t.replace("const ", "").replace("volatile ", "")
    return "val", t, SIZES.get(t), open_


INCLUDE_RE = re.compile(r'#include\s+"(b3/[^"]+\.h)"')
_HEADERS = {}


def parse_structs(text, into):
    """the struct / class bodies of (comment-stripped) text: name -> {"base", "fields"}"""
    for m in STRUCT_RE.finditer(text):
            i = m.end()
            depth = 1
            while i < len(text) and depth:
                c = text[i]
                depth += (c == "{") - (c == "}")
                i += 1
            body = text[m.end():i - 1]
            fields = []
            end = 0   # the struct's size as far as its fields and pads tell (0: unknown)
            for f in FIELD_RE.finditer(body):
                dims_txt = re.findall(r"\[([^\]]*)\]", f.group(4) or "")
                try:
                    dims = [int(eval(d, {"__builtins__": {}}, {})) for d in dims_txt]   # 0x12AD28 - 0x27D0 too
                except Exception:
                    dims = []
                if f.group(2) == "pad":
                    if dims:
                        end = max(end, int(f.group(3), 16) + dims[0])
                    continue
                tdecl = f.group(1).strip().split(";")[-1].strip()
                tdecl = re.sub(r"^(public|private|protected)\s*:\s*", "", tdecl)
                kind, et, es, open_ = elem_type(tdecl)
                cnt = dims[0] if dims else None
                for d in dims[1:]:   # a [6][254] array: 6 elements of 254
                    es = es * d if es else None
                if es:
                    end = max(end, int(f.group(3), 16) + es * (cnt or 1))
                fields.append((int(f.group(3), 16), kind, et, es, cnt, open_, " ".join(f.group(0).split()), dims))
            st = into.setdefault(m.group(2), {"base": m.group(3), "fields": [], "size": 0})
            st["fields"] += fields
            st["size"] = max(st.get("size", 0), end)


def header_structs(inc_dir, rel, seen=None):
    """a generated header's structs, with the headers it includes (cached)"""
    if rel in _HEADERS:
        return _HEADERS[rel]
    _HEADERS[rel] = {}
    path = os.path.join(inc_dir, rel)
    if not os.path.exists(path):
        return {}
    raw = open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for inc in INCLUDE_RE.findall(raw):
        for k, v in header_structs(inc_dir, inc).items():
            out.setdefault(k, v)
    parse_structs(strip_comments(raw), out)
    _HEADERS[rel] = out
    return out


class Unit:
    def __init__(self, path, inc_dir):
        self.path = path
        self.name = os.path.basename(path)[:-4]
        raw = open(path, encoding="utf-8", errors="replace").read()
        self.raw = raw
        self.text = strip_comments(raw)
        self.structs = {}   # name -> {"base": parent, "fields": [(off, kind, etype, esize, count, open, decl)]}
        parse_structs(self.text, self.structs)
        self.header = set()
        for inc in INCLUDE_RE.findall(raw):
            for k, v in header_structs(inc_dir, inc).items():
                if k not in self.structs:
                    self.structs[k] = v
                    self.header.add(k)
        self.globals = {}   # D_xxx -> (struct, address); an array global (`&guest_global<T>`) as ("[]" + T)
        for g in GLOBAL_RE.finditer(self.raw):
            t = g.group(3).strip()
            self.globals[g.group(1)] = (("[]" + t) if g.group(2) else t, int(g.group(4), 16))


def class_of_address(addr):
    """the smallest seeded global holding addr (a table inside another's [G] size: _grSpikeGen in the graphics manager's)"""
    best = None
    for base, (cls, size) in SEED_GLOBALS.items():
        if base <= addr < base + size and (best is None or size < best[2]):
            best = (cls, addr - base, size)
    return (best[0], best[1]) if best else (None, None)


def resolve_member(cls, off):
    """the innermost seeded inline member holding (cls, off): (class, offset inside it)"""
    while True:
        for (c, o), (mc, inline, size) in SEED_MEMBERS.items():
            if c == cls and inline and o <= off < o + size:
                cls, off = mc, (off - o) % POOLS.get((c, o), size)
                break
        else:
            return cls, off


def classify(units):
    """struct (unit, name) -> (class, base offset, confidence, how)"""
    cls = {}
    for u in units:
        for g, (s, addr) in u.globals.items():
            c, off = class_of_address(addr)
            if c and not s.startswith("[]") and s in u.structs:
                c2, off2 = resolve_member(c, off)
                cls[(u.name, s)] = (c2, off2, "H", "global %s" % g)
        for s in u.structs:
            if re.fullmatch(r"CB3\w+|CGt\w+", s):
                cls.setdefault((u.name, s), (s, 0, "H", "named"))
        for m in SELF_RE.finditer(u.text):
            c, s = m.group(3), m.group(4)
            if c.startswith("CB3") or c.startswith("CGt"):
                cls[(u.name, s)] = (c, 0, "H", "self of " + m.group(1))
        # a func_ definition: the class from the comment that names it (the decomp's research names), else the unit's
        # source file (B3World_0019A950: CB3World)
        for m in SELF_ANY_RE.finditer(u.text):
            s = m.group(2)
            if (u.name, s) in cls or s not in u.structs:
                continue
            fa = re.search(r"func_([0-9A-Fa-f]{8})", m.group(1))
            c = None
            if fa:
                cm = re.search(r"(?:0x)?%s[^\n]*?\b(CB3\w+)::" % fa.group(1), u.raw, re.I) or \
                     re.search(r"\b(CB3\w+)::\w+\([^\n]*\n(?:[^\n]*\n){0,3}[^\n]*%s\s*\(" % re.escape(m.group(1)), u.raw)
                if cm:
                    c = cm.group(1)
            fm = re.match(r"B3(\w+?)_[0-9A-Fa-f]{8}$", u.name)
            if not c and fm:
                c = "CB3" + fm.group(1)
            if c:
                cls[(u.name, s)] = (c, 0, "M", "func comment / unit name")
    changed = True
    while changed:
        changed = False
        for u in units:
            for s, st in u.structs.items():
                k = (u.name, s)
                if k not in cls:
                    continue
                c, base, conf, _ = cls[k]
                for off, kind, et, es, cnt, open_, decl, dims in st["fields"]:
                    o = base + off
                    tgt = None
                    if (c, o) in SEED_MEMBERS:
                        mc, inline, _ = SEED_MEMBERS[(c, o)]
                        if (kind == "ptr") != inline:
                            tgt = (mc, 0)
                    elif kind == "val" and et in u.structs and es is None:
                        rc, ro = resolve_member(c, o)
                        if rc != c:
                            tgt = (rc, ro)
                    if tgt and et in u.structs and (u.name, et) not in cls:
                        cls[(u.name, et)] = (tgt[0], tgt[1], "M" if conf == "H" else "L", "%s.field_%X" % (s, off))
                        changed = True
    return cls


def load_targets(path):
    t = []
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        p = line.split()
        t.append({"cls": p[0], "off": int(p[1], 16), "esize": int(p[2], 0), "count": int(p[3]), "item": p[4],
                  "how": p[5], "what": " ".join(p[6:])})
    return t


def uses(u, field):
    sub = len(re.findall(r"\b%s\s*\[" % field, u.text))
    allu = len(re.findall(r"\b%s\b" % field, u.text))
    return sub, allu


def find_sites(units, cls, targets):
    """item -> [(unit, struct, field, shape, conf, how, subscripts, other uses, header?, declared count, offset)]"""
    sites = collections.defaultdict(list)
    for u in units:
        for s, st in u.structs.items():
            k = (u.name, s)
            for off, kind, et, es, cnt, open_, decl, dims in st["fields"]:
                if k in cls:
                    c, base, conf, how = cls[k]
                    c0, o0 = c, base + off
                    c, o = resolve_member(c, base + off)
                else:
                    c0, o0 = None, None
                    c, o, conf, how = None, off, "L", "shape"
                if es is None and kind == "val" and et in u.structs and u.structs[et].get("size"):
                    es = u.structs[et]["size"]   # a struct element: its size from its fields and pads
                    for d in dims[1:]:
                        es *= d
                for t in targets:
                    lo, hi = t["off"], t["off"] + t["esize"] * t["count"]
                    span = (es or 0) * (cnt or 1)
                    if c is None:
                        # unknown struct: the same offset, element size and an array (or open array) only
                        if not (o == t["off"] and (cnt == t["count"] or open_) and (es == t["esize"])):
                            continue
                        if t["off"] < 0x400:   # too common an offset for a guess
                            continue
                    elif c0 == t["cls"] and (o0 < hi and o0 + max(span, 1) > lo):
                        o = o0
                    elif c != t["cls"] or not (o < hi and o + max(span, 1) > lo):
                        continue
                    whole = o == lo and (cnt is not None and cnt >= t["count"] or open_)
                    elem = None if whole else ((o - lo) // t["esize"] if o >= lo else None)
                    sub, allu = uses(u, "field_%X" % off)
                    shape = ("open" if open_ else "[%s]" % cnt if cnt else ("elem %s" % elem))
                    sites[t["item"]].append((u.name, s, "field_%X" % off, shape, conf, how, sub, allu - 1 - sub,
                                             s in u.header, cnt, o - lo, es))
    return sites


RAW_RE = re.compile(r"\+\s*0x([0-9A-Fa-f]{3,6})\b")


def raw_sites(units, cls, targets):
    """raw byte arithmetic on the world (D_00522660) or on a unit's self of known class: an offset constant that
    resolves (through the seeded members) into a target array, or a pool's start: (item, unit, line no, line)"""
    out = []
    for u in units:
        selfc = {cls[(u.name, s)][0] for s in u.structs if (u.name, s) in cls and cls[(u.name, s)][3].startswith("self")}
        for ln, line in enumerate(u.text.split("\n"), 1):
            bases = []
            if "D_00522660" in line:
                bases.append(("CB3World", 0))
            if re.search(r"\b(self|this)\b", line):
                bases += [(c, 0) for c in selfc]
            if not bases:
                continue
            for m in RAW_RE.finditer(line):
                v = int(m.group(1), 16)
                for bc, _ in bases:
                    rcl, ro = resolve_member(bc, v)
                    for t in targets:
                        hit = (rcl == t["cls"] and t["off"] <= ro < t["off"] + t["esize"] * t["count"]) or \
                              (bc == t["cls"] and t["off"] <= v < t["off"] + t["esize"] * t["count"])
                        if hit:
                            out.append((t["item"], u.name, ln, line.strip()[:160]))
                    if (bc, v) in POOLS:
                        out.append(("pool+0x%X" % v, u.name, ln, line.strip()[:160]))
    return sorted(set(out))


def load_units(units_dir):
    inc_dir = os.path.join(os.path.dirname(os.path.abspath(units_dir)), "include")
    return [Unit(os.path.join(units_dir, f), inc_dir) for f in sorted(os.listdir(units_dir)) if f.endswith(".cpp")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--units", default="gen/native/units")
    ap.add_argument("--targets", default="config/split_arrays.tsv")
    ap.add_argument("--discover", action="store_true")
    ap.add_argument("--out")
    a = ap.parse_args()
    units = load_units(a.units)
    cls = classify(units)
    out = open(a.out, "w") if a.out else sys.stdout
    known = collections.Counter(c for c, *_ in cls.values())
    print("# units %d, structs %d, classified %d (%s)" % (
        len(units), sum(len(u.structs) for u in units), len(cls),
        ", ".join("%s %d" % kv for kv in known.most_common(12))), file=out)
    if a.discover:
        agg = collections.defaultdict(list)
        for u in units:
            for s, st in u.structs.items():
                if (u.name, s) not in cls:
                    continue
                c, base, conf, _ = cls[(u.name, s)]
                for off, kind, et, es, cnt, open_, decl, dims in st["fields"]:
                    if cnt in (5, 6, 8) or open_:
                        rc, ro = resolve_member(c, base + off)
                        agg[(rc, ro, cnt, es)].append(u.name)
        print("# discover: class offset count esize units", file=out)
        for (c, o, cnt, es), us in sorted(agg.items(), key=lambda kv: (kv[0][0], kv[0][1])):
            if c in ("CB3World", "CB3RaceCar", "CB3Score", "CB3ScoreBonus", "CB3PhysicsManager", "CB3TrafficSystem",
                     "CB3TrafficVehicle", "CB3RacePosition", "CB3GameData", "CB3AICar", "CB3HUDScoreList",
                     "CB3CrashActionDirector", "CB3OfflineRoadRageLogic", "CB3SoundRaceCarManager", "CB3ReplaySystem",
                     "CB3HUDManager", "CB3EffectsManager", "CB3CameraSystem", "CB3SoundGameModeManager", "CB3HUDScoreList",
                     "CB3AIDriver", "CB3AIControlData", "CB3StageLogic", "CB3OfflineStageLogic", "CB3OnePlayerStage"):
                print("D\t%s\t+0x%X\t%s\t%s\t%d\t%s" % (c, o, "open" if cnt is None else cnt, es, len(us),
                                                     " ".join(sorted(set(us))[:6])), file=out)
    targets = load_targets(a.targets)
    sites = find_sites(units, cls, targets)
    site_units = {x[0] for ss in sites.values() for x in ss}
    print("# sites: item unit struct field shape conf how subscripts other-uses", file=out)
    for t in targets:
        ss = sites.get(t["item"], [])
        confs = collections.Counter(x[4] for x in ss)
        print("T\t%s\t%s +0x%X %dx%d\t%s\tunits %d\t%s" % (
            t["item"], t["cls"], t["off"], t["count"], t["esize"], t["what"], len({x[0] for x in ss}),
            " ".join("%s %d" % kv for kv in sorted(confs.items()))), file=out)
        for x in sorted(ss):
            print("S\t%s\t%s" % (t["item"], "\t".join(str(v) for v in x[:8])), file=out)
    # raw byte arithmetic on a known object: an offset constant inside a target array's span or a pool's span
    print("# raw: item unit line", file=out)
    for item, unit, ln, line in raw_sites(units, cls, targets):
        print("R\t%s\t%s:%d\t%s" % (item, unit, ln, line), file=out)
    world_t = [t for t in targets if t["cls"] == "CB3World"]
    for u in units:
        for g, (s, addr) in u.globals.items():
            c, off = class_of_address(addr)
            for t in world_t:
                if c == "CB3World" and t["off"] <= off < t["off"] + t["esize"] * t["count"]:
                    sub, allu = uses(u, g)
                    print("G\t%s\t%s\t%s at world +0x%X (%s)\tsubscripts %d\tother %d" % (
                        t["item"], u.name, g, off, s, sub, allu - 1 - sub), file=out)
    print("# loops: unit count-of(< 6) count-of(< 5)  (units with a site)", file=out)
    for u in units:
        if u.name in site_units:
            n6 = len(re.findall(r"<\s*6\b|<=\s*5\b", u.text))
            n5 = len(re.findall(r"<\s*5\b|<=\s*4\b", u.text))
            if n6 or n5:
                print("L\t%s\t%d\t%d" % (u.name, n6, n5), file=out)


if __name__ == "__main__":
    main()
