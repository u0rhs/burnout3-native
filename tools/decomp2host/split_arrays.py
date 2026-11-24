#!/usr/bin/env python3
"""split_arrays.py -- decomp2host's last pass for the native generation: the game's per-car arrays as
b3host::split_array (include/b3host/split_array.h), so a race may have more than the game's 6 cars (MORE RIVALS).

decomp2host runs it on its output when DECOMP2HOST_SPLIT_ARRAYS names the targets (the Makefile's native rules:
config/split_arrays.tsv); `make native-bundle` keeps it. For every target marked `split`, the sites split_census.py
finds are rewritten in the generated code (a unit, or the shared header that declares the struct):
  T field_X[S]...;                  -> b3host::split_array<T..., N, S> field_X;     (S: the declared size)
  b3host::open_array<T> field_X;    -> b3host::split_open<T, N> field_X;            (T[1]'s layout kept)
  #define D_x (&b3host::guest_global<T>(0x...u))  (an array global at the array's address)
                                    -> #define D_x (b3host::split_global<T, N>{0x...u})
Sites that are one element (a constant index below N) or a smaller declaration are left and listed. The rewrite is
idempotent. With the split off the generated code behaves as before (split_array.h); the compiler then lists every
use that is not a subscript (no decay to T*), which this pass fixes by the rules in FIXUPS or reports.

Standalone (a check of a generated tree): split_arrays.py [--gen gen/native] [--targets config/split_arrays.tsv]
[--dry-run]. The census itself (a report): split_census.py.
"""
import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import split_census as rc  # noqa: E402

INCLUDE = '#include "b3host/split_array.h"   // decomp2host split_arrays.py: the per-car arrays\n'


def struct_span(text, name):
    m = re.search(r"\b(?:struct|class)\s+%s\b[^;{]*\{" % re.escape(name), text)
    if not m:
        return None
    i, depth = m.end(), 1
    while i < len(text) and depth:
        depth += (text[i] == "{") - (text[i] == "}")
        i += 1
    return m.end(), i - 1


def rewrite_flat(text, struct, field):
    """T field_X[S]...; -> b3host::flat_array<T..., S> field_X; (a pool / list the extra cars extend in place)"""
    span = struct_span(text, struct)
    if not span:
        return text, "no struct"
    a, b = span
    body = text[a:b]
    arr = re.compile(r"(^|[;{]|\*/)([ \t]*)((?![^;{}\n]*\b(?:return|=)\b)[^;{}\n/=()]*?)\s*\b%s\s*((?:\[[^\]]+\]\s*)+);" % field, re.M)
    m = arr.search(body)
    if not m:
        return text, "flat: no decl"
    dims = re.findall(r"\[([^\]]+)\]", m.group(4))
    inner = m.group(3).strip() + "".join("[%s]" % d for d in dims[1:])
    new = "%s%sb3host::flat_array<%s, %s> %s;" % (m.group(1), m.group(2), inner, dims[0].strip(), field)
    return text[:a] + body[:m.start()] + new + body[m.end():] + text[b:], "flat"


def rewrite_field(text, struct, field, n):
    span = struct_span(text, struct)
    if not span:
        return text, "no struct"
    a, b = span
    body = text[a:b]
    arr = re.compile(r"(^|[;{]|\*/)([ \t]*)((?![^;{}\n]*\b(?:return|=)\b)[^;{}\n/=()]*?)\s*\b%s\s*((?:\[[^\]]+\]\s*)+);" % field, re.M)
    opn = re.compile(r"b3host::open_array<(.+?)>\s*\b%s\s*;" % field)
    m = arr.search(body)
    if m:
        tdecl = m.group(3).strip()
        dims = re.findall(r"\[([^\]]+)\]", m.group(4))
        inner = tdecl + "".join("[%s]" % d for d in dims[1:])
        new = "%s%sb3host::split_array<%s, %d, %s> %s;" % (m.group(1), m.group(2), inner, n, dims[0].strip(), field)
        body = body[:m.start()] + new + body[m.end():]
        return text[:a] + body + text[b:], "array"
    m = opn.search(body)
    if m:
        new = "b3host::split_open<%s, %d> %s;" % (m.group(1), n, field)
        body = body[:m.start()] + new + body[m.end():]
        return text[:a] + body + text[b:], "open"
    return text, "no decl"


# The uses that are not a subscript, found by the compiler after the rewrite, and their fix (checked one by one):
FIXUPS = [
    # a local pointer to a split array global, then subscripted (RacePosition Update / Draw): the view itself
    (re.compile(r"(?:const\s+)?b3host::gp<[\w:]+>\s*\*\s*(\w+)\s*__attribute__\(\(uninitialized\)\)\s*;\s*\1\s*=\s*(D_0064D388)\s*;"),
     r"auto \1 = \2;   /* split_arrays: the split view */"),
    (re.compile(r"(?:const\s+)?b3host::gp<[\w:]+>\s*\*\s*(\w+)\s*=\s*(D_0064D388)\s*;"),
     r"auto \1 = \2;   /* split_arrays: the split view */"),
    # byte arithmetic on a split field by a car index (CB3TrafficVehicle +0xF0): the subscript it stands for
    (re.compile(r"b3host::pcast<ps2f>\(\(b3host::pcast<char>\((field_F0)\) \+ (\w+) \* 4\)\)"), r"&\1[\2]"),
    # the car list by raw byte arithmetic (CalcRaceDistFromRaceCars, RenderShadows, CalculateScoreForTakedowns, ...)
    (re.compile(r"\(\(D_00522660 \+ \(\(b3host::icast<unsigned>\((\w+)\)\) \* 4\)\) \+ 0x12AD28\)"),
     r"b3host::split::flat(0x0064D388u, 4, 6, \1)"),
    (re.compile(r"\(b3host::pcast<char>\(&D_00522660\) \+ (b3host::icast<unsigned int>\(\w+\)|\(\w+\)|\w+) \* 4 \+ 0x12AD28\)"),
     r"b3host::split::flat(0x0064D388u, 4, 6, \1)"),
    # the car's start point slot (CB3RaceCar::Prepare: &GameData +0x370[idx])
    (re.compile(r"field_1788 = D_00522660 \+ (\w+) \* 4 \+ 0x125EB0;"),
     r"field_1788 = b3host::split::flat(0x00648510u, 4, 6, \1);"),
    # host-stack per-car arrays (no guest memory: wider changes nothing for 6 cars); the census of host locals in the
    # units that walk the car list found this one ([6]) and four [8]s (RacePosition::Draw, crash escape, 0x1B3F90)
    (re.compile(r"(UnkStruct_002ED3E0_out \*outs)\[6\];"), r"\1[8];   /* split_arrays: up to 8 cars */"),
    # guest-stack per-car arrays: 6 with the split off (the game's frame), 8 with it on (RenderRaceCars' draw list)
    (re.compile(r"b3host::GuestLocalArray<(UnkStruct_00199A20_e), \(6\)> (\w+); \1 \(&(\w+)\)\[6\] = \*\2;"),
     r"b3host::split_local_array<\1, 6, 8> \2; \1 (&\3)[8] = *\2;   /* split_arrays */"),
    # the traffic system's race car infos (scratchpad 0x70000000, [6] x 0x70, its parameter list after them): handed
    # to every traffic vehicle as base + count, so moved whole while the split is on
    (re.compile(r"CB3TrafficRaceCarInfo \*info = &spr->field_0\[i\];"),
     r"CB3TrafficRaceCarInfo *info = &b3host::split::moved(spr->field_0, b3host::split::traffic_infos)[i];   /* split_arrays */"),
    (re.compile(r"spr->field_698\[i\]->Update\(spr->field_0, nCars\);"),
     r"spr->field_698[i]->Update(b3host::split::moved(spr->field_0, b3host::split::traffic_infos), nCars);   /* split_arrays */"),
    # CB3Score::Update's position bonus (gmanBestRacePositionBP 0x488140: 500 400 300 200 100 for places 1..5, then
    # padding): a place gained past 5th (only with more than 6 cars) gives no bonus and no event (the table's else
    # branch is the 1st place's event 0x7E); the time of the change kept (+0x48)
    # (the line before is part of the pattern, so the rule matches once: idempotent)
    (re.compile(r"(if \(D_0064D3A4\.field_0 > 0 \|\| D_0064D3A0\.field_0 >= 2\) \{\n)(\s*)(int bp = \(&b3host::guest_global<int>\(0x00488140u\)\)\[field_44 - 1\];)"),
     r"\1\2if (field_44 > 5) { field_48 = field_C; continue; }   /* split_arrays: places past 5th, no bonus */\n\2\3"),
    # the stage record's AI car ids and colours, read for every AI car (func_001346C0): the extra rivals' in the network
    # ones after them (unit-scoped: (unit, pattern, replacement))
    ("func_001346C0", re.compile(r"b3host::ps2_ulong field_18\[5\];"), r"b3host::flat_array<b3host::ps2_ulong, 5> field_18;"),
    ("func_001346C0", re.compile(r"int field_78\[5\];"), r"b3host::flat_array<int, 5> field_78;"),
    # CalculateScoreForTakedowns' profile flags (a player's +0x4EFD / +0x4F03, u8[6] by the victim's or the aggressor's
    # index): cars 7-8's in the shadow (not in the saved profile), not over the next table's first bytes
    ("func_001578E0", re.compile(r"pl->field_4EFD\[idx\] \|= 1;"),
     "*b3host::split::flat(b3host::guest_address(pl) + 0x4EFDu, 1, 6, std::size_t(idx)) |= 1;   /* split_arrays: profile flags */"),
    ("func_001578E0", re.compile(r"pl->field_4F03\[idx\] \|= 1;"),
     "*b3host::split::flat(b3host::guest_address(pl) + 0x4F03u, 1, 6, std::size_t(idx)) |= 1;   /* split_arrays: profile flags */"),
    # race car views of the score's per-opponent tables (the score at CB3RaceCar +0x10C0) and its distance table, indexed by
    # the other car's +0x19AC (split_uses.py "direct"): the body-body response's takedown time (+0x1598: sc.4D8), the AI
    # aggression's target distance (+0x1894: car.dist), the bonus' victim flag (+0x1679: sc.5B9)
    ("BodyBodyResponse__17CB3PhysicsManagerFP16CB3CollidingPair",
     re.compile(r"ps2f t = ca->field_1598\[\*\(b3host::pcast<signed char>\(cb\) \+ 0x19AC\)\];"),
     "ps2f t = *b3host::pcast<ps2f>(b3host::split::flat(b3host::guest_address(ca) + 0x1598u, 4, 6, "
     "std::size_t(*(b3host::pcast<signed char>(cb) + 0x19AC))));   /* split_arrays: sc.4D8 */"),
    ("FindTarget__24CB3AIAggressionBehaviourFv", re.compile(r"ps2f d = fabsf\(field_44->field_1894\[c->field_19AC\]\);"),
     "ps2f d = fabsf(*b3host::pcast<ps2f>(b3host::split::flat(b3host::guest_address(field_44) + 0x1894u, 4, 6, "
     "std::size_t(c->field_19AC))));   /* split_arrays: car.dist */"),
    ("OnTakedown__13CB3ScoreBonusFP10CB3RaceCarPC17CB3CrashStartInfobb",
     re.compile(r"self->field_6 = self->field_148->field_1679\[car->field_19AC\];"),
     "self->field_6 = *b3host::split::flat(b3host::guest_address(self->field_148) + 0x1679u, 1, 6, "
     "std::size_t(car->field_19AC));   /* split_arrays: sc.5B9 */"),
    # CB3CrashActionDirector's StopRaceCarCrashing twin (0x1AE630, the network car's): its +0x12C view of cad.car
    ("B3CrashActionDirector", re.compile(r"self->field_12C\[p0\]\.field_4 = -1\.0f;"),
     "*b3host::pcast<ps2f>(b3host::split::flat(b3host::guest_address(self) + 0x12Cu, 0x3C, 6, std::size_t(p0)) + 4) = -1.0f;"
     "   /* split_arrays: cad.car */"),
    # the near-miss score reads its car's traffic near-miss list (ts.362AE, World +0x3B08E, [6][8]) and its count (ts.3635C,
    # D_0055D79C, u8[6]) by the car's index as raw arithmetic
    ("B3Score_001598F0", re.compile(r"n = D_0055D79C\[idx\];"),
     "n = *b3host::split::flat(0x0055D79Cu, 1, 6, idx);   /* split_arrays: ts.3635C */"),
    ("B3Score_001598F0", re.compile(r"row = b3host::pcast<unsigned char>\(&D_00522660\) \+ idx \* 8;"),
     "row = b3host::pcast<unsigned char>(b3host::split::flat(0x0055D6EEu, 8, 6, idx));   /* split_arrays: ts.362AE */"),
    ("B3Score_001598F0", re.compile(r"id = row\[i \+ 0x3B08E\];"), "id = row[i];"),
    # views of the physics manager's active-body list head (pm.rcp, flat: cars 7-8 extend it in place) declared [6] but
    # indexed to the car count: flat_array (the same memory, no bound for the compiler)
    ("func_0019A550", re.compile(r"UnkStruct_0019A550_rcp field_3C98C\[6\];"),
     "b3host::flat_array<UnkStruct_0019A550_rcp, 6> field_3C98C;   /* split_arrays: pm.rcp */"),
    ("Update__16CB3TrafficSystemFv", re.compile(r"UnkStruct_00166160_pe field_3C98C\[6\];"),
     "b3host::flat_array<UnkStruct_00166160_pe, 6> field_3C98C;   /* split_arrays: pm.rcp */"),
    ("B3PhysicsManager_001B3EA0", re.compile(r"UnkStruct_001B3EA0_e field_70\[6\];"),
     "b3host::flat_array<UnkStruct_001B3EA0_e, 6> field_70;   /* split_arrays: pm.rcp */"),
    # the world collision's car-pair test reads the other car's score tables (CB3RaceCar +0x1618 / +0x161E: CB3Score +0x558
    # sc.558 / +0x55E sc.55E) by this car's index through a race car view
    ("DoDrivingWorldCollision__17CB3RaceCarPhysicsFv",
     re.compile(r"if \(!o->field_161E\[lnCar\] && !o->field_1618\[lnCar\]\) \{\}"),
     "if (!*b3host::split::flat(b3host::guest_address(o) + 0x161Eu, 1, 6, lnCar) && "
     "!*b3host::split::flat(b3host::guest_address(o) + 0x1618u, 1, 6, lnCar)) {}   /* split_arrays: sc.55E / sc.558 */"),
    # the AI's pre-race rev spikes (_grSpikeGen, 6 x 0x1C by car index; car 8's would reach mRPCArguments at 0x66DF80):
    # the unit's byte-offset macros through the split (the array's own subscripts are split_global)
    ("GetPreRaceDrivingControls__15CB3AICarPhysicsFv",
     re.compile(r"#define SPF\(off\) \(\(b3host::pcast<ps2f>\(\(b3host::pcast<char>\(_grSpikeGen\) \+ \(off\)\)\)\)\[idx \* 7\]\)\n"
                r"#define SPB\(off\) \(\(b3host::pcast<unsigned char>\(\(b3host::pcast<char>\(_grSpikeGen\) \+ \(off\)\)\)\)\[idx \* 28\]\)"),
     "#define SPF(off) (*b3host::pcast<ps2f>(b3host::split::flat(0x0066DEB0u, 28, 6, idx) + (off)))   /* split_arrays: ai.spike */\n"
     "#define SPB(off) (*b3host::pcast<unsigned char>(b3host::split::flat(0x0066DEB0u, 28, 6, idx) + (off)))"),
    # the sound traffic manager's bucket rows: indexed by a player's index (0 / 1), inside the first 6 rows
    (re.compile(r"(const unsigned char \*ids = base->field_36218)(;)"),
     r"\1.data()\2   /* split_arrays: player rows only */"),
]


def add_include(text):
    if INCLUDE in text:
        return text
    m = re.search(r'#include "b3gen/prelude.h"\n', text)
    if m:
        return text[:m.end()] + INCLUDE + text[m.end():]
    m = re.search(r"#(ifndef|pragma once)[^\n]*\n(#define[^\n]*\n)?", text)
    at = m.end() if m else 0
    return text[:at] + INCLUDE + text[at:]


def header_of(inc_dir, struct, cache={}):
    if not cache:
        for root, _, files in os.walk(inc_dir):
            for f in files:
                if f.endswith(".h"):
                    p = os.path.join(root, f)
                    t = rc.strip_comments(open(p, encoding="utf-8", errors="replace").read())
                    for m in rc.STRUCT_RE.finditer(t):
                        cache.setdefault(m.group(2), p)
    return cache.get(struct)


def view_straddles(u, struct, field, rel, esize):
    """a view of an array from inside an element (at `rel` from the element's start): why one of its fields is not in
    one element (None: each is)"""
    st = u.structs.get(struct)
    f = st and [x for x in st["fields"] if "field_%X" % x[0] == field]
    e = f and u.structs.get(f[0][2])
    if not e or not e["fields"]:
        return "its element type is unknown"
    for off, kind, et, es, cnt, open_, decl, dims in e["fields"]:
        if es is None or open_:
            return "field_%X's size is unknown" % off
        lo, hi = rel + off, rel + off + es * (cnt or 1)
        if lo < 0 or hi > esize:
            return "field_%X is bytes %+d..%+d of the element" % (off, lo, hi)
    return None


SPLIT_DECL = re.compile(r"\b(?:split_array|split_open|split_local_array)<[^;{}]*>\s+(\w+)\s*;|#define\s+(\w+)\s+\(b3host::split_global<")
BOUND_LOOP = re.compile(r"for \((?:int |signed char |unsigned int )?(\w+) = 0; (\(?)\1 < 6(\)?); (?:\1\+\+|\+\+\1)\) ?\{")


def scope_texts(text, inc_dir, processed):
    """the unit and the class headers it includes, transitively (as the pass has rewritten them)"""
    out, todo, seen = [("", text)], re.findall(r'#include "b3/classes/(\w+\.h)"', text), set()
    while todo:
        inc = todo.pop()
        if inc in seen:
            continue
        seen.add(inc)
        hp = os.path.join(inc_dir, "b3", "classes", inc)
        ht = processed.get(hp) or (open(hp, encoding="utf-8").read() if os.path.exists(hp) else "")
        out.append((inc, ht))
        todo += re.findall(r'#include "b3/classes/(\w+\.h)"', ht)
    return out


def member_decls(texts, name):
    """every struct / class in scope that declares a member `name`: [(struct, declaration, split?)]"""
    found = []
    decl = re.compile(r"^[^;{}()\n]*\b%s\b\s*(?:\[[^\]]*\]\s*)*;" % re.escape(name), re.M)
    for _, t in texts:
        for m in rc.STRUCT_RE.finditer(t):
            span = struct_span(t, m.group(2))
            if not span or span[0] != m.end():
                continue
            body = t[span[0]:span[1]]
            depth_free = re.sub(r"\{[^{}]*\}", "", body)   # not the members' own inline bodies
            for d in decl.finditer(depth_free):
                line = " ".join(d.group(0).split())
                found.append((m.group(2), line, bool(re.search(r"\b(?:split_array|split_open|split_local_array)<", line))))
    for _, t in texts:   # a global split by the pass (#define D_x (b3host::split_global<...>))
        for d in re.finditer(r"#define\s+%s\s+\((b3host::\w+)" % re.escape(name), t):
            found.append(("(global)", d.group(1), d.group(1) == "b3host::split_global"))
    return found


def owners(text, at):
    """the class of the function around `at` (Class::Name(...)) and the type of its `self` parameter"""
    heads = list(re.finditer(r"^[^\s#/][^;{}\n]*?\b(?:(\w+)::)?\w+\s*\(([^;{}]*)\)\s*(?:const\s*)?\{?\s*$", text[:at], re.M))
    if not heads:
        return None, None
    h = heads[-1]
    st = re.search(r"(\w+)\s*\*\s*(?:const\s+)?self\b", h.group(2))
    return h.group(1), st.group(1) if st else None


def global_type(texts, name):
    for _, t in texts:
        m = re.search(r"#define\s+%s\s+\(b3host::guest_global<(\w+)>" % re.escape(name), t)
        if m:
            return m.group(1)
    return None


BOUND_REPORT = []   # (unit, variable, [(subscript, [(struct, split?)])]) of each loop the rule bounded


def extend_bounds(text, path, inc_dir, processed):
    """the game's `< 6` loops over split arrays only (each subscript by the loop's variable is a member every struct in
    the unit's scope declares as a split array, the variable used for nothing else): bounded by split::bound(6), so cars
    7-8's elements get the same values (CB3Score::Prepare's 'none' -1.0 / -1000 for its per-opponent tables; with the
    split off it is 6). A member some struct in scope declares otherwise leaves the loop as it is (BOUND_REPORT lists
    each loop bounded with its subscripts' structs)"""
    texts = None
    out, last, k = [], 0, 0
    for m in BOUND_LOOP.finditer(text):
        v = m.group(1)
        a = j = m.end()
        d = 1
        while j < len(text) and d:
            d += (text[j] == "{") - (text[j] == "}")
            j += 1
        body = text[a:j - 1]
        subs = re.findall(r"([\w\.\->]+)\s*\[\s*%s\s*\]" % v, body)
        uses = len(re.findall(r"\b%s\b" % v, body))
        if not subs or uses != len(subs):
            continue
        texts = texts or scope_texts(text, inc_dir, processed)
        owner_this, owner_self = owners(text, m.start())
        why = []
        for x in sorted(set(subs)):
            parts = re.split(r"\.|->", x)
            ds = member_decls(texts, parts[-1])
            # the subscript's own struct where it can be told: a bare member is the function's class's, self-> is
            # self's type's, D_x. is the global's type's; else every struct in scope that declares the name
            own = owner_this if len(parts) == 1 else owner_self if parts[0] == "self" and len(parts) == 2 else \
                global_type(texts, parts[0]) if len(parts) == 2 else None
            if own and any(st == own for st, _, _ in ds):
                ds = [d for d in ds if d[0] == own]
            why.append((x, [(st, sp) for st, _, sp in ds]))
        if any(not ds or not all(sp for _, sp in ds) for _, ds in why):
            continue
        BOUND_REPORT.append((os.path.basename(path)[:-4], v, why))
        out.append(text[last:m.start()])
        out.append(m.group(0).replace("%s < 6" % v, "%s < b3host::split::bound(6)" % v, 1))
        last = m.end()
        k += 1
    out.append(text[last:])
    return "".join(out), k


def apply(gen, targets_path, dry_run=False, out=sys.stdout):
    """the pass over the generated tree `gen` (units/, include/); returns (stats, left)"""
    del BOUND_REPORT[:]
    units_dir, inc_dir = os.path.join(gen, "units"), os.path.join(gen, "include")
    units = rc.load_units(units_dir)
    cls = rc.classify(units)
    all_targets = rc.load_targets(targets_path)
    targets = [t for t in all_targets if t["how"] == "split"]
    flats = [t for t in all_targets if t["how"] in ("pool", "flat")]
    by_item = {t["item"]: t for t in targets}
    sites = rc.find_sites(units, cls, targets)
    by_unit = {u.name: u for u in units}
    edits = collections.defaultdict(list)   # path -> [(struct, field, n)]
    left = []
    for item, ss in sites.items():
        t = by_item[item]
        for (unit, struct, field, shape, conf, how, sub, other, header, cnt, rel, es) in ss:
            path = header_of(inc_dir, struct) if header else os.path.join(units_dir, unit + ".cpp")
            # a view of the array from inside an element (its fields at an offset within one element: the crash director's
            # +0xF0 / +0x12C views of its +0x110 table) splits as the array does: index i is the same element, its side
            # in the shadow at the same offset
            inside = rel != 0 and abs(rel) < t["esize"] and es == t["esize"] and (shape == "open" or cnt is not None)
            if inside:
                # each of the view's fields must fall in one real element (bytes rel + off .. + size of 0 .. esize): a view
                # that straddles two elements would be split wrong, so the pass refuses it (a human decides)
                bad = view_straddles(by_unit[unit], struct, field, rel, t["esize"])
                if bad:
                    raise SystemExit("split_arrays: %s %s::%s (%s, %+d in a 0x%X element): %s: refused" %
                                     (unit, struct, field, item, rel, t["esize"], bad))
            if (rel != 0 and not inside) or not path:
                why = ("no file" if not path else
                       "a view from inside an element, of another element size (%s)" % es if 0 < abs(rel) < t["esize"] else
                       "element %d" % (rel // t["esize"]) if rel > 0 else "a wider field covers it (a byte blob)")
                left.append((item, unit, struct, field, shape, why))
                continue
            # a declaration of any size (a smaller one indexes the real array flat up to N) of the same element size
            if (shape == "open" or cnt is not None) and (es is None or es == t["esize"]):
                if (struct, field, t["count"], item) not in edits[path]:   # a header: once for all its units
                    edits[path].append((struct, field, t["count"], item))
            else:
                left.append((item, unit, struct, field, shape, "another element size (%s): another view, left" % es))
    # the pools / lists the extra cars extend in place: their declared arrays as flat_array (no bound for the compiler)
    fedits = collections.defaultdict(list)
    for item, ss in rc.find_sites(units, cls, flats).items():
        for (unit, struct, field, shape, conf, how, sub, other, header, cnt, rel, es) in ss:
            if rel != 0 or cnt is None or shape == "open":
                continue
            path = header_of(inc_dir, struct) if header else os.path.join(units_dir, unit + ".cpp")
            if path and (struct, field) not in fedits[path]:
                fedits[path].append((struct, field))
    gl = collections.defaultdict(list)
    for u in units:
        for g, (s, addr) in u.globals.items():
            c, off = rc.class_of_address(addr)
            if c is None:
                continue
            for t in targets:
                k, r = divmod(off - t["off"], t["esize"])
                if c == t["cls"] and 0 <= k < t["count"] and r == 0 and s.startswith("[]"):
                    # an array global at element k: its own index i is the array's k + i
                    gl[os.path.join(units_dir, u.name + ".cpp")].append((g, s[2:], addr, t["count"], k, t["item"]))
                elif c == t["cls"] and t["off"] < off < t["off"] + t["esize"] * t["count"] and not s.startswith("[]"):
                    left.append((t["item"], u.name, "-", g, "global", "a struct global inside the array (+%d)" % (off - t["off"])))
    stats = collections.Counter()
    fix_paths = set()
    for u in units:   # the fixups go wherever their pattern is (raw arithmetic needs no struct site)
        if any(fx[-2].search(u.raw) and (len(fx) == 2 or fx[0] == u.name) for fx in FIXUPS):
            fix_paths.add(os.path.join(units_dir, u.name + ".cpp"))
    hits = collections.Counter()
    processed = {}
    bound_paths = {os.path.join(units_dir, u.name + ".cpp") for u in units if BOUND_LOOP.search(u.raw)}
    for path in sorted(set(edits) | set(gl) | fix_paths | set(fedits) | bound_paths):
        text = open(path, encoding="utf-8").read()
        orig = text
        for struct, field in fedits.get(path, []):
            text, how = rewrite_flat(text, struct, field)
            stats[how] += 1
        for struct, field, n, item in edits.get(path, []):
            text, how = rewrite_field(text, struct, field, n)
            stats[how] += 1
            if how.startswith("no"):
                left.append((item, os.path.basename(path), struct, field, "-", how))
        for g, t, addr, n, m, item in gl.get(path, []):
            pat = re.compile(r"\(&b3host::guest_global<%s>\(0x%08Xu\)\)" % (re.escape(t), addr), re.I)
            text, k = pat.subn("(b3host::split_global<%s, %d%s>{0x%08Xu})" % (t, n, ", %d" % m if m else "", addr), text)
            stats["global" if k else "global missed"] += 1
        for n, fx in enumerate(FIXUPS):
            pat, rep = fx[-2], fx[-1]
            if len(fx) == 3 and os.path.basename(path) != fx[0] + ".cpp":
                continue
            text, k = pat.subn(rep, text)
            stats["fixup"] += k
            hits[n] += k
        if path.endswith(".cpp"):
            text, k = extend_bounds(text, path, inc_dir, processed)
            stats["bound"] += k
        processed[path] = text
        if text != orig:
            text = add_include(text)
            stats["files"] += 1
            if not dry_run:
                open(path, "w", encoding="utf-8").write(text)
    # a unit's own rule that matched nothing: the generated code moved under it (a rename, a regenerated unit), so the
    # site is no longer covered; refused (a rule whose replacement is there already, a second run, passes)
    for n, fx in enumerate(FIXUPS):
        if len(fx) != 3 or hits[n]:
            continue
        p = os.path.join(units_dir, fx[0] + ".cpp")
        done = os.path.exists(p) and fx[2].split("(", 1)[0][:40] in open(p, encoding="utf-8").read() and \
            re.sub(r"\\[0-9]", "", fx[2])[:60] in open(p, encoding="utf-8").read()
        if not done:
            raise SystemExit("split_arrays: the rule for %s matched nothing (%s): refused" % (fx[0], fx[1].pattern[:80]))
    return stats, sorted(left)


def report_lines(left):
    """the pass' report as config/split_arrays_left.txt holds it (after its comment lines): the sites left as they are,
    then the < 6 loops bounded by split::bound(6) with each subscript's structs"""
    out = ["left\t" + "\t".join(str(v) for v in x) for x in left]
    for unit, v, why in BOUND_REPORT:
        out.append("bound\t%s\t%s\t%s" % (unit, v, "; ".join("%s: %s" % (x, ", ".join(st for st, _ in ds)) for x, ds in why)))
    return out


def check_report(left, path, out_dir):
    """the generation stops when the report differs from the committed one (path): a site newly left, one now split, a
    loop newly bounded (or no longer) is read before it is accepted. The new report is kept beside the output; after a
    review, copy it over the committed file (its comment lines kept). Returns the message (None: the same)"""
    new = report_lines(left)
    old = [l for l in open(path, encoding="utf-8").read().split("\n") if l and not l.startswith("#")] \
        if os.path.exists(path) else None
    if old == new:
        return None
    keep = os.path.join(out_dir, "split_arrays_left.new.txt")
    head = [l for l in open(path, encoding="utf-8").read().split("\n") if l.startswith("#")] if old is not None else []
    with open(keep, "w", encoding="utf-8") as f:
        f.write("\n".join(head + new) + "\n")
    if old is None:
        return "split_arrays: %s missing; the report is in %s" % (path, keep)
    gone = [l for l in old if l not in new]
    added = [l for l in new if l not in old]
    lines = ["split_arrays: the report differs from %s (%d lines gone, %d new); review, then copy %s over it:" %
             (path, len(gone), len(added), keep)]
    lines += ["  - " + l[:200] for l in gone[:20]] + ["  + " + l[:200] for l in added[:20]]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", default="gen/native")
    ap.add_argument("--targets", default="config/split_arrays.tsv")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--left", action="store_true", help="list the sites left as they are")
    a = ap.parse_args()
    stats, left = apply(a.gen, a.targets, a.dry_run)
    print("split_arrays: %s" % ", ".join("%s %d" % kv for kv in sorted(stats.items())))
    if a.left:
        for line in report_lines(left):
            print(line)


if __name__ == "__main__":
    main()
