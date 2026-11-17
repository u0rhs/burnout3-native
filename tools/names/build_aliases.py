#!/usr/bin/env python3
"""build_aliases: the port's alias table (placeholder -> readable name), generated from the decomp (docs/names-layer.md).

    python3 -I tools/names/build_aliases.py [--rev HEAD] [--decomp SNAPDIR] [--out config/aliases.tsv]

Reads a snapshot of the decomp's committed tree (never its working tree) and writes one row per alias:
  scope      field | struct | function | global
  file       '*' (applies in every file) or the decomp path the alias is local to (per-unit views)
  container  the aggregate the member belongs to (field), else '-'
  placeholder, alias, tag ([C]/[L]/[G], the weaker of the evidence chain), source, evidence (one line, citable)

Sources (best first; nothing else, no free-form guessing):
  symbols  a func_/D_ placeholder still in the source although config/symbol_addrs_b3names.txt names the address
           (the source lags the symbol file): the decomp's own name, its own tag
  dnat     research/proto/new_names.csv: B3's own prototype DNAT_200.45 aligned to retail by code (decomp N159; hold-out
           error: high 2/664, medium 5/259, low 8/51) -> [L] high/medium, [G] low
  emap     research/retail_data_names.csv: build E data names transferred by code reference (high/medium), function-local
           statics (`name$N`) and globals the symbol file left out; compiler temporaries (`@N`) skipped -> [L]
  revenge  research/names/revenge_member_map.tsv: Revenge (Jul 14 prototype, stabs) member at the same offset of the
           paired class (decomp tools/revenge_member_map.py) -> the row's tier
  view     research/names/unkstruct_bindings.tsv: an UnkStruct view bound to class C at base B (decomp
           tools/bind_unkstructs.py); the view type gets `<C>View[_<B>]`, its field_X the vdb (else Revenge) name of
           C+B+X -> weaker of the binding's and the member's tier
  vdb      retail's variable-debugger registrations (GtComms::CGtCommsDatabase::AddEntry; decomp before N298: vdbRegister*): each
           call pairs a variable (a global, or this+offset in a named method) with Criterion's label and group strings,
           decoded from the retail ELF at the call site -> [C]; mangled into an identifier by vdb_ident(). A vdb name
           replaces a Revenge name for the same member (the Revenge row goes to conflicts.tsv); a global keeps its emap
           name, the registration added to the evidence
  vtable   retail vtables (__vt__<Class> in the symbol file): vfunc_XX of class C (or of a view bound to C at base 0)
           is the method name of the function in C's vtable at +XX (slot 0 at +8) -> the slot function's name tag
           (symbol file, else DNAT); left out wherever a base and a derived class both declare the slot without the
           same alias (an override must keep overriding)
Rows that would give one container two equal names, or one placeholder two names, are left out (build/names/conflicts.tsv).
"""
import argparse
import csv
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
import retail  # noqa: E402

FIELD = re.compile(r"^field_([0-9A-Fa-f]+)$")


def cpp_ident(qualified):
    """'CB3Score::HandleBoostStart()' -> 'CB3Score_HandleBoostStart'; None for operators, templates, destructors."""
    q = qualified.split("(")[0].strip()
    if "<" in q or "operator" in q or "~" in q:
        return None
    q = q.replace("GtMathPs2::", "")
    s = q.replace("::", "_")
    return s if re.fullmatch(r"[A-Za-z_]\w*", s) else None


def member_ident(path):
    """A Revenge member path -> one identifier: 'mAvoidance.mrLeftRisk' -> 'mAvoidance_mrLeftRisk' (a member of an
    embedded struct the decomp declares flat), 'marTimes[2]' -> 'marTimes_2' (one element of an array member)."""
    s = re.sub(r"\[(\d+)\]", r"_\1", path).replace(".", "_")
    return s if re.fullmatch(r"[A-Za-z_]\w*", s) else None


def demangle_simple(mangled):
    """Name__<n>ClassF... -> Class_Name (the decomp's mangled method names); None otherwise."""
    m = re.match(r"^([A-Za-z_]\w*?)__(\d+)([A-Za-z_]\w*)", mangled)
    if m and int(m.group(2)) <= len(m.group(3)):
        return "%s_%s" % (m.group(3)[:int(m.group(2))], m.group(1))
    if re.fullmatch(r"[A-Za-z_]\w*", mangled) and "__" not in mangled:
        return mangled
    return None


class Decls:
    """Aggregates over the whole decomp: members[name] = identifiers declared in any definition of `name`, bases[name] =
    its direct base classes (every definition's base clause)."""
    def __init__(self):
        self.members, self.bases, self.arrays = defaultdict(set), defaultdict(set), defaultdict(set)

    def add(self, masked):
        for name, b, e, members in common.aggregates(masked):
            self.members[name] |= set(members)
            for tok, poss in members.items():
                if any(masked[p + len(tok):p + len(tok) + 40].lstrip().startswith("[") for p in poss):
                    self.arrays[name].add(tok)
        for name, bs in common.bases(masked).items():
            self.bases[name] |= bs

    def owner(self, cls, member, seen=()):
        """The class among cls and its bases that declares `member` (single inheritance: same offset), else None."""
        if member in self.members.get(cls, ()):
            return cls
        for b in sorted(self.bases.get(cls, ())):
            if b not in seen and len(self.bases[cls]) == 1:
                o = self.owner(b, member, seen + (cls,))
                if o:
                    return o
        return None

    def related(self, cls):
        """cls's ancestors and descendants (transitively)."""
        down = defaultdict(set)
        for d, bs in self.bases.items():
            for b in bs:
                down[b].add(d)
        out, todo = set(), [(cls, "up"), (cls, "down")]
        while todo:
            c, way = todo.pop()
            for n in (self.bases.get(c, ()) if way == "up" else down.get(c, ())):
                if n not in out:
                    out.add(n)
                    todo.append((n, way))
        out.discard(cls)
        return out


def placeholders_in_source(snap, decls=None):
    """{placeholder: uses} over src/ and include/ (outside comments and literals); fills `decls` on the way."""
    c = Counter()
    for root in ("src", "include"):
        for dp, _, fs in os.walk(os.path.join(snap, root)):
            for f in fs:
                if f.endswith((".c", ".cpp", ".h", ".hpp", ".inc")):
                    m = common.mask(open(os.path.join(dp, f), errors="replace").read())
                    if decls is not None:
                        decls.add(m)
                    for _, t in common.tokens(m):
                        if t.startswith(("func_", "D_", "UnkStruct_", "field_", "vfunc_")) and common.PLACEHOLDER.match(t):
                            c[t] += 1
    return c


# ------------------------------------------------------------------------------------------------ vdb registrations
# GtComms::CGtCommsDatabase::AddEntry overloads [C E map, symbol file comments]: (db, T *var, const char *label,
# const char *group, const char *cfgfile, ...). The entry key is CRC32(label + group + "/" + cfgfile) (CGtHash::CalculateHash).
VDB_ADD = {0x21B9B0: "Vector", 0x21BB60: "Float", 0x21BD80: "Int"}
TEXT_END = 0x469E00   # the last game function ends before the VU code (decomp N213)
WORD = re.compile(r"[A-Za-z0-9]+")


def vdb_words(s):
    """'Minimum Air Distance (Metres)' -> 'MinimumAirDistanceMetres': the alphanumeric runs, each with its first letter
    capitalised (the rest kept: 'LockUp', '3d', 'Kg')."""
    return "".join(w[0].upper() + w[1:] for w in WORD.findall(s))


def vdb_ident(label, group, depth=1):
    """A registration's identifier: the last `depth` components of the group path, then the label, joined by '_'
    ('Physics/Suspension/Front' + 'Spring Length' -> 'Front_SpringLength'; depth 2 -> 'Suspension_Front_SpringLength').
    Components and label are vdb_words(); empty components (trailing '/') are dropped; a leading digit gets 'n'."""
    parts = [vdb_words(c) for c in (group or "").split("/") if vdb_words(c)]
    s = "_".join(parts[len(parts) - depth:] + [vdb_words(label)]) if parts else vdb_words(label)
    return ("n" + s) if s[:1].isdigit() else s


def vdb_hash(elf, s):
    """CGtHash::CalculateHash (CalculateHash__7CGtHashFPCc 0x21BFA0): CRC32 over the bytes with retail's table, initial ~0, arithmetic shift, no final
    inversion."""
    table = [elf.word(0x49B110 + 4 * i) for i in range(256)]   # CGtHash::manHashTable [C symbol file, N298 name]
    a = 0xFFFFFFFF
    for ch in s.encode("latin1"):
        a = (((a >> 8) | (0xFF000000 if a & 0x80000000 else 0)) ^ table[(ch ^ a) & 0xFF]) & 0xFFFFFFFF
    return a


def vdb_xml_check(snap, path):
    """How many decoded registrations have their key in the game's DATA/VDB.XML (a binary key -> value table, no
    strings). Prints a statistic only: game data never feeds the table."""
    import struct
    elf = retail.Elf(snap)
    d = open(path, "rb").read()
    keys = set(struct.unpack_from("<%dI" % (len(d) // 4), d, 0))
    c = Counter()
    for x in vdb_calls(snap):
        if None in (x["label"], x["group"], x["cfg"]):
            c["cfg file not a constant (per-instance buffer): not checkable"] += 1
            continue
        key = x["label"] + x["group"] + ("" if x["group"].endswith("/") else "/") + x["cfg"]
        c["key in VDB.XML" if vdb_hash(elf, key) in keys else "key not in VDB.XML"] += 1
    print("  VDB.XML (%s, header %s): %s" % (os.path.basename(path), struct.unpack_from("<4I", d, 0),
                                              ", ".join("%s %d" % kv for kv in sorted(c.items()))))


def vdb_calls(snap):
    """Every AddEntry call in retail game code: dict(site, func, kind, var, label, group, cfg), var ('k', address) or
    ('this', offset) or None (through a loaded pointer)."""
    elf = retail.Elf(snap)
    funcs, _ = retail.labels(snap)
    starts = sorted(a for a in funcs if a < TEXT_END)
    out = []
    for i, s in enumerate(starts):
        e = starts[i + 1] if i + 1 < len(starts) else TEXT_END
        for site, tgt, r in retail.calls(elf, s, e, VDB_ADD):
            def string(v):
                return elf.cstr(v[1]) if v and v[0] in ("k", "ld") else None
            var = r[5] if r[5] and r[5][0] in ("k", "this") else None
            out.append(dict(site=site, func=funcs[s], kind=VDB_ADD[tgt], var=var, label=string(r[6]),
                            group=string(r[7]), cfg=string(r[8]),
                            group_ptr=r[7][2] if r[7] and r[7][0] == "ld" else None))
    return out


def method_class(mangled):
    m = re.match(r"^[A-Za-z_]\w*?__(\d+)([A-Za-z_]\w*)", mangled)
    if m and int(m.group(1)) <= len(m.group(2)):
        return m.group(2)[:int(m.group(1))]
    return None


def vdb_tables(snap, used, syms, decls, conflicts, stats):
    """-> (global rows, class field rows, {class: {offset: row}} naming every registered member offset, for views)."""
    globs, members = defaultdict(list), defaultdict(lambda: defaultdict(list))
    for c in vdb_calls(snap):
        stats["calls"] += 1
        if c["var"] is None or c["label"] is None:
            stats["call: variable through a loaded pointer"] += 1
            continue
        ev = "vdb AddEntry(%s) @0x%08X in %s: '%s' in '%s'%s" % (
            c["kind"], c["site"], c["func"], c["label"], c["group"],
            " (group: initial value of the char* at 0x%08X)" % c["group_ptr"] if c["group_ptr"] else "")
        if c["var"][0] == "k":
            globs[c["var"][1]].append((c, ev))
        else:
            cls = method_class(c["func"])
            if not cls:
                stats["call: this-relative outside a method"] += 1
                continue
            members[cls][c["var"][1]].append((c, ev))
    # globals: the D_ placeholders the source still uses
    grows = []
    for a, cs in sorted(globs.items()):
        p = "D_%08X" % a
        if a in syms:
            stats["global: already named in the decomp"] += len(cs)
        elif not used[p]:
            stats["global: placeholder not used in the source"] += len(cs)
        else:
            grows.append((p, cs))
    rows = assign_vdb(grows, "global", "*", "-", conflicts, "globals")
    # members: one name per registered offset of the class (unique within the class), then a row wherever the decomp
    # declares that field_X in the class (or its single-inheritance base)
    named = {}
    frows = []
    for cls, offs in sorted(members.items()):
        named[cls] = {int(r["placeholder"][6:], 16): r for r in assign_vdb(
            [("field_%X" % off, cs) for off, cs in sorted(offs.items())], "field", "*", cls, conflicts, "class " + cls)}
        for off, r in sorted(named[cls].items()):
            n = len(offs[off])
            owner = decls.owner(cls, r["placeholder"])
            if owner is None:
                stats["member: no field_X declared there (inside an array, named, or class not in the decomp)"] += n
                continue
            if r["placeholder"] in decls.arrays.get(owner, ()):
                stats["member: field_X is an array (the label names its first element only)"] += n
                continue
            frows.append(dict(r, container=owner))
            stats["member: field placeholder"] += n
    return rows, frows, named


def assign_vdb(items, scope, file, container, conflicts, where):
    """items [(placeholder, [(call, evidence)])] -> rows; one name per placeholder (several different registrations of
    one variable: left out), aliases unique within the scope (deeper group qualification, then the call site)."""
    first = []
    for p, cs in items:
        names = {(c["label"], c["group"]) for c, _ in cs}
        if len(names) > 1:
            conflicts.append((where, p, " / ".join(sorted("%s|%s" % n for n in names)), "vdb: registered under several labels"))
            continue
        first.append((p, cs[0][0], "; ".join(sorted({e for _, e in cs}))))
    depth = {p: 1 for p, _, _ in first}
    for _ in range(4):
        names = Counter(vdb_ident(c["label"], c["group"], depth[p]) for p, c, _ in first)
        clash = [p for p, c, _ in first if names[vdb_ident(c["label"], c["group"], depth[p])] > 1]
        if not clash:
            break
        for p in clash:
            depth[p] += 1
    names = Counter(vdb_ident(c["label"], c["group"], depth[p]) for p, c, _ in first)
    rows = []
    for p, c, ev in first:
        alias = vdb_ident(c["label"], c["group"], depth[p])
        if names[alias] > 1:
            alias = "%s_%X" % (alias, c["site"])
        rows.append(dict(scope=scope, file=file, container=container, placeholder=p, alias=alias, tag="[C]",
                         source="vdb", evidence=ev))
    return rows


# ------------------------------------------------------------------------------------------------ vtables
VT = re.compile(r"^__vt__(\d+)([A-Za-z_]\w*) = 0x([0-9A-Fa-f]+);.*size:0x([0-9A-Fa-f]+)(.*)$")


def method_name(mangled):
    """'Update__10CB3RaceCarFv' -> 'Update'; None for constructors, destructors, operators, non-method names."""
    m = re.match(r"^([A-Za-z][A-Za-z0-9_]*?)__(\d+|Q\d)", mangled)
    return m.group(1) if m and not m.group(1).startswith("operator") else None


def vtables(snap):
    """{class: (address, symbol size, evidence tag)} from the symbol files' __vt__ symbols (simple class names)."""
    out = {}
    for fn in ("config/symbol_addrs_b3names.txt", "config/symbol_addrs.txt"):
        for line in open(os.path.join(snap, fn)):
            m = VT.match(line.strip())
            if m and int(m.group(1)) == len(m.group(2)) and m.group(2) not in out:
                was = re.search(r"\[(C|L|G)\]", m.group(5))
                out[m.group(2)] = (int(m.group(3), 16), int(m.group(4), 16), "[%s]" % was.group(1) if was else "[C]")
    return out


def slot_names(snap, syms):
    """{function address: (method name, tag, evidence, class)}: the symbol file's names, else DNAT's (as the dnat
    source); class is the name's own class (mangled or DNAT-qualified)."""
    out = {}
    for a, (name, note) in syms.items():
        mn = method_name(name)
        if mn:
            was = re.search(r"\[(C|L|G)\]", note)
            out[a] = (mn, "[%s]" % was.group(1) if was else "[C]", "%s (symbol file%s)" % (
                name, ", " + note.strip("/ ")[:60] if note.strip("/ ") else ""), method_class(name))
    for r in csv.DictReader(open(os.path.join(snap, "research/proto/new_names.csv"))):
        a = int(r["retail_addr"], 16)
        if a in out or a in syms:
            continue
        q = r["dnat_name"].split("(")[0].strip()
        mn = q.split("::")[-1]
        if "::" not in q or "<" in q or "operator" in q or "~" in q or not re.fullmatch(r"[A-Za-z_]\w*", mn):
            continue
        out[a] = (mn, "[G]" if r["confidence"] == "low" else "[L]", "DNAT_200.45 %s (confidence %s; decomp N159)" % (
            r["dnat_name"], r["confidence"]), q.split("::")[-2])
    return out


def vfunc_rows(snap, syms, decls, conflicts, stats):
    """-> ([class rows], {class: {placeholder: (alias, tag, evidence)}} for views at base 0)."""
    elf = retail.Elf(snap)
    vts = vtables(snap)
    names = slot_names(snap, syms)
    per_class = {}
    starts = sorted(v[0] for v in vts.values())

    def extent(va):
        # the extent comes from the data, not the symbol's size (split by data labels: __vt__26CB3OnlineRoadRageRace
        # Logic says 0xEC, its slots run to +0x108): slots while the words point into game code, up to the next vtable
        nxt = next((a for a in starts if a > va), va + 0x1000)
        size = 8
        while va + size < nxt and 0x100000 <= (elf.word(va + size) or 0) < TEXT_END:
            size += 4
        return size
    # each vtable's confirmed classes: itself, the classes of its [C]-named slots (it inherits from them), and theirs
    family = {}
    for cls, (va, _, _) in vts.items():
        family[cls] = {cls} | {names[w][3] for w in (elf.word(va + o) for o in range(8, extent(va), 4))
                               if w in names and names[w][1] == "[C]"}
    for _ in range(8):
        for cls in family:
            for k in list(family[cls]):
                family[cls] |= family.get(k, set())
    # method names confirmed at each slot offset in some vtable (one slot of one base is one virtual everywhere)
    confirmed_at = defaultdict(set)
    for cls, (va, _, _) in vts.items():
        for o in range(8, extent(va), 4):
            w = elf.word(va + o)
            if w in names and names[w][1] == "[C]":
                confirmed_at[o].add(names[w][0])
    for cls, (va, size, vtag) in sorted(vts.items()):
        size = extent(va)
        slots = {}
        for off in range(8, size, 4):
            w = elf.word(va + off)
            if w in names:
                mn, tag, ev, owner = names[w]
                if tag != "[C]" and owner not in family[cls] and mn not in confirmed_at[off]:
                    stats["vfunc: [L]/[G] slot name whose class and name no confirmed slot supports"] += 1
                    conflicts.append(("__vt__" + cls, "vfunc_%X" % off, "%s::%s" % (owner, mn),
                                      "slot name unsupported: class not among the vtable's confirmed classes %s and no "
                                      "[C] slot at +0x%X has that name" % (sorted(family[cls]), off)))
                    continue
                if tag != "[C]" and owner not in family[cls]:
                    ev += "; a [C] slot at +0x%X in another vtable has the same name" % off
                slots["vfunc_%X" % off] = (mn, common.weaker(tag, vtag), "__vt__%d%s 0x%08X +0x%X = 0x%08X %s" % (
                    len(cls), cls, va, off, w, ev))
        per_class[cls] = (slots, size)
    rows = []
    for cls, (slots, size) in sorted(per_class.items()):
        declared = sorted(t for t in decls.members.get(cls, ()) if t.startswith("vfunc_") and common.PLACEHOLDER.match(t))
        cand = []
        for p in declared:
            if int(p[6:], 16) >= size:
                stats["vfunc: declared past the vtable's end"] += 1
                conflicts.append(("class " + cls, p, "-", "past __vt__ size 0x%X" % size))
            elif p not in slots:
                stats["vfunc: slot function has no name"] += 1
            else:
                mn, tag, ev = slots[p]
                cand.append(dict(scope="field", file="*", container=cls, placeholder=p, alias=mn, tag=tag,
                                 source="vtable", evidence=ev))
        rows += dedupe(cand, conflicts, "class %s" % cls)
    # an override must keep overriding: a slot declared in two related classes needs the same alias in both
    alias = {(r["container"], r["placeholder"]): r["alias"] for r in rows}
    bad = set()
    for (cls, p), a in alias.items():
        for o in decls.related(cls):
            if p in decls.members.get(o, ()) and alias.get((o, p)) != a:
                bad |= {(cls, p), (o, p)}
                conflicts.append(("class %s / %s" % (cls, o), p, "%s / %s" % (a, alias.get((o, p), "-")),
                                  "vtable: base and derived class declare the slot without one alias"))
    stats["vfunc: dropped for override consistency"] += sum(1 for r in rows if (r["container"], r["placeholder"]) in bad)
    rows = [r for r in rows if (r["container"], r["placeholder"]) not in bad]
    view_slots = {cls: {p: (mn, tag, ev) for p, (mn, tag, ev) in slots.items()} for cls, (slots, _) in per_class.items()}
    return rows, view_slots


def load_symbols(snap):
    by_addr = {}
    for line in open(os.path.join(snap, "config/symbol_addrs_b3names.txt")):
        m = re.match(r"(\S+) = 0x([0-9A-Fa-f]+);(.*)", line)
        if m:
            by_addr[int(m.group(2), 16)] = (m.group(1), m.group(3).strip())
    return by_addr


def global_rows(snap, used, syms):
    rows = []
    sym_names = {v[0] for v in syms.values()}
    func_ph = {int(p[5:], 16): p for p in used if p.startswith("func_")}
    data_ph = {int(p[2:], 16): p for p in used if p.startswith("D_")}
    # symbols: the source lags the symbol file
    for a, p in sorted(list(func_ph.items()) + list(data_ph.items())):
        if a in syms:
            name, note = syms[a]
            was = re.search(r"\[(C|L|G)\]", note)
            tag = "[%s]" % was.group(1) if was else "[C]"
            alias = demangle_simple(name)
            if alias:
                rows.append(dict(scope="function" if p.startswith("func_") else "global", file="*", container="-",
                                 placeholder=p, alias=alias, tag=tag, source="symbols",
                                 evidence="config/symbol_addrs_b3names.txt: %s = 0x%08X %s" % (name, a, note.strip("/ "))))
    named = {r["placeholder"] for r in rows}
    # dnat
    for r in csv.DictReader(open(os.path.join(snap, "research/proto/new_names.csv"))):
        a = int(r["retail_addr"], 16)
        p = func_ph.get(a)
        if not p or p in named or a in syms:
            continue
        alias = cpp_ident(r["dnat_name"])
        if not alias:
            continue
        tag = "[G]" if r["confidence"] == "low" else "[L]"
        rows.append(dict(scope="function", file="*", container="-", placeholder=p, alias=alias, tag=tag, source="dnat",
                         evidence="DNAT_200.45 %s @%s (%s, %s, similarity %s, confidence %s; decomp N159)" % (
                             r["dnat_name"], r["dnat_addr"], r["dnat_placement"], r["method"], r["similarity"],
                             r["confidence"])))
    # emap data names
    for r in csv.DictReader(open(os.path.join(snap, "research/retail_data_names.csv"))):
        a = int(r["elf_addr"], 16)
        p = data_ph.get(a)
        if not p or p in named or a in syms or r["confidence"] not in ("high", "medium"):
            continue
        n = r["name"]
        if n.startswith("@") or "::" in n:
            continue
        base, local = n.split("$")[0], "$" in n
        if not re.fullmatch(r"[A-Za-z_]\w*", base) or base in sym_names:
            continue
        rows.append(dict(scope="global", file="*", container="-", placeholder=p, alias=base, tag="[L]", source="emap",
                         evidence="build E %s %s in %s (%s, %s, confidence %s)%s" % (
                             r["section"], n, r["source"], r["method"], r["votes"] + " votes", r["confidence"],
                             "; function-local static" if local else "")))
    # one name per address: a base name shared by several addresses gets the address appended
    count = Counter(r["alias"] for r in rows)
    for r in rows:
        if count[r["alias"]] > 1:
            r["alias"] = "%s_%s" % (r["alias"], r["placeholder"].split("_")[1].lstrip("0") or "0")
    return rows


def member_map(snap):
    mm = defaultdict(dict)   # class -> offset -> row
    for r in csv.DictReader(open(os.path.join(snap, "research/names/revenge_member_map.tsv")), delimiter="\t"):
        mm[r["b3_class"]][int(r["b3_offset"], 16)] = r
    return mm


def field_rows(mm, conflicts, extra=()):
    """Revenge rows per class merged with `extra` (vdb and vtable rows: they win over a Revenge row for one member)."""
    over = defaultdict(dict)   # class -> placeholder -> row
    for r in extra:
        over[r["container"]][r["placeholder"]] = r
    rows = []
    for cls in sorted(set(mm) | set(over)):
        cand = list(over.get(cls, {}).values())
        for off, r in sorted(mm.get(cls, {}).items()):
            if not FIELD.match(r["b3_current_name"]):
                continue   # the decomp already names it: not an alias
            alias = member_ident(r["revenge_member"])
            if not alias:
                conflicts.append(("class " + cls, r["b3_current_name"], r["revenge_member"], "not an identifier path"))
                continue
            o = over.get(cls, {}).get(r["b3_current_name"])
            if o:
                conflicts.append(("class " + cls, r["b3_current_name"], "%s (Revenge %s) / %s (%s)" % (
                    alias, r["tier"], o["alias"], o["source"]), "Revenge member superseded by a %s row" % o["source"]))
                o["evidence"] += "; Revenge pairing had %s::%s %s" % (r["revenge_class"], r["revenge_member"], r["tier"])
                continue
            cand.append(dict(scope="field", file="*", container=cls, placeholder=r["b3_current_name"],
                             alias=alias, tag=r["tier"], source="revenge",
                             evidence="Revenge %s::%s (%s) at 0x%X; %s" % (r["revenge_class"], r["revenge_member"],
                                                                          r["revenge_type"], off, r["evidence"])))
        rows += dedupe(cand, conflicts, "class %s" % cls)
    return rows


def dedupe(cand, conflicts, where):
    names = Counter(r["alias"] for r in cand)
    out = []
    for r in cand:
        if names[r["alias"]] > 1:
            conflicts.append((where, r["placeholder"], r["alias"], "alias given to %d members" % names[r["alias"]]))
        else:
            out.append(r)
    return out


def view_rows(snap, mm, conflicts, vdb_named=None, view_slots=None, decls=None):
    """Per (file, view): one consistent binding over every function that binds it, else left out."""
    texts = {}

    def defs_in(rel):
        if rel not in texts:
            path = os.path.join(snap, rel)
            texts[rel] = common.aggregates(common.mask(open(path, errors="replace").read())) if os.path.exists(path) else []
        return texts[rel]
    closure = {}
    by_view = defaultdict(list)
    for r in csv.DictReader(open(os.path.join(snap, "research/names/unkstruct_bindings.tsv")), delimiter="\t"):
        if r["contradictions"].startswith("REJECTED"):
            continue
        f, view = r["file"], r["unkstruct"]
        if not os.path.exists(os.path.join(snap, f)):
            conflicts.append((f, view, "-", "file gone at the snapshot"))
            continue
        if f not in closure:
            closure[f] = common.include_closure(snap, f)
        home = next((h for h in closure[f] if any(a[0] == view for a in defs_in(h))), None)
        if home is None:
            conflicts.append((f, view, "-", "view not defined in the file or its headers at the snapshot"))
            continue
        by_view[(home, view)].append(r)
    rows = []
    type_alias = defaultdict(dict)   # home file -> view -> alias
    for (f, view), bs in sorted(by_view.items()):
        places = {(b["bound_class"], int(b["base_offset"], 16)) for b in bs}
        if len(places) > 1:
            conflicts.append((f, view, "-", "bound to %s" % sorted("%s+0x%X" % p for p in places)))
            continue
        cls, base = places.pop()
        best = min(bs, key=lambda b: common.TIER_RANK[b["tier"]])
        defs = [a for a in defs_in(f) if a[0] == view]
        bind_ev = "%s = %s+0x%X (%s %s in %s; decomp tools/bind_unkstructs.py)" % (
            view, cls, base, best["evidence"], best["tier"], best["function"])
        type_alias[f][view] = (cls, base, best["tier"], bind_ev)
        cand = []
        for tok in sorted(defs[0][3]):
            if tok.startswith("vfunc_") and base == 0 and tok in (view_slots or {}).get(cls, {}):
                mn, tag, ev = view_slots[cls][tok]
                cand.append(dict(scope="field", file=f, container=view, placeholder=tok, alias=mn,
                                 tag=common.weaker(best["tier"], tag), source="view+vtable",
                                 evidence="%s; %s" % (bind_ev, ev)))
                continue
            m = FIELD.match(tok)
            if not m:
                continue
            off = base + int(m.group(1), 16)
            v = (vdb_named or {}).get(cls, {}).get(off)
            if v and not (decls and tok in decls.arrays.get(view, ())):
                cand.append(dict(scope="field", file=f, container=view, placeholder=tok, alias=v["alias"],
                                 tag=best["tier"], source="view+vdb",
                                 evidence="%s; %s at %s+0x%X" % (bind_ev, v["evidence"], cls, off)))
                continue
            r = mm.get(cls, {}).get(off)
            if not r:
                continue
            alias = member_ident(r["revenge_member"])
            if not alias:
                conflicts.append((f + " " + view, tok, r["revenge_member"], "not an identifier path"))
                continue
            cand.append(dict(scope="field", file=f, container=view, placeholder=tok, alias=alias,
                             tag=common.weaker(best["tier"], r["tier"]), source="view+revenge",
                             evidence="%s; Revenge %s::%s (%s) at %s+0x%X (%s)" % (
                                 bind_ev, r["revenge_class"], r["revenge_member"], r["revenge_type"], cls, off,
                                 r["tier"])))
        rows += dedupe(cand, conflicts, "%s %s" % (f, view))
    for f, views in sorted(type_alias.items()):
        names = {}
        for view, (cls, base, tier, ev) in sorted(views.items()):
            stem = "%sView" % cls if base == 0 else "%sView_%X" % (cls, base)
            alias, k = stem, 2
            while alias in names.values():
                alias, k = "%s%d" % (stem, k), k + 1
            names[view] = alias
            rows.append(dict(scope="struct", file=f, container="-", placeholder=view, alias=alias, tag=tier,
                             source="view", evidence=ev + ("" if alias == stem else "; numbered: several views of one place")))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rev", default="HEAD")
    ap.add_argument("--decomp", help="an existing snapshot directory (default: snapshot --rev)")
    ap.add_argument("--out", default=common.ALIASES)
    ap.add_argument("--vdb-xml", help="the game's DATA/VDB.XML: print how many decoded registrations it has a key for")
    a = ap.parse_args()
    if a.decomp:
        snap, full = a.decomp, open(os.path.join(a.decomp, ".complete")).read().strip()
    else:
        snap, full = common.snapshot(a.rev)
    decls = Decls()
    used = placeholders_in_source(snap, decls)
    syms = load_symbols(snap)
    mm = member_map(snap)
    conflicts = []
    found = Counter()
    grows = global_rows(snap, used, syms)
    vg, vf, vdb_named = vdb_tables(snap, used, syms, decls, conflicts, found)
    have = {r["placeholder"]: r for r in grows}
    for r in vg:
        if r["placeholder"] in have:   # an E-map identifier: keep it, the registration confirms what it is
            have[r["placeholder"]]["evidence"] += "; %s" % r["evidence"]
            found["global: E-map name kept, registration added to its evidence"] += 1
        else:
            grows.append(r)
    vt, view_slots = vfunc_rows(snap, syms, decls, conflicts, found)
    rows = grows + field_rows(mm, conflicts, vf + vt) + view_rows(snap, mm, conflicts, vdb_named, view_slots, decls)
    # one placeholder, one name per scope key
    keyed = defaultdict(list)
    for r in rows:
        keyed[(r["scope"], r["file"], r["container"], r["placeholder"])].append(r)
    for k, rs in keyed.items():
        if len({r["alias"] for r in rs}) > 1:
            conflicts.append((k[1], k[3], "/".join(r["alias"] for r in rs), "several names (folded code?): left out"))
    rows = [rs[0] for rs in keyed.values() if len({r["alias"] for r in rs}) == 1]
    rows.sort(key=lambda r: (r["scope"], r["file"], r["container"], r["placeholder"]))
    common.write_aliases(rows, a.out, [
        "Generated by tools/names/build_aliases.py from burnout3-retail-decomp %s. Do not edit: regenerate." % full[:10],
        "Port-only readable names (docs/names-layer.md). Never feed these back into the decomp.",
    ])
    os.makedirs(os.path.join(common.PORT, "build/names"), exist_ok=True)
    with open(os.path.join(common.PORT, "build/names/conflicts.tsv"), "w") as f:
        f.write("where\tplaceholder\talias\treason\n")
        for c in conflicts:
            f.write("\t".join(c) + "\n")
    stats = Counter((r["source"], r["scope"], r["tag"]) for r in rows)
    uses = Counter()
    for r in rows:
        if r["scope"] in ("function", "global"):
            uses[r["source"]] += used[r["placeholder"]]
    print("build_aliases: %d rows from decomp %s -> %s (%d conflicts left out: build/names/conflicts.tsv)" % (
        len(rows), full[:10], os.path.relpath(a.out, common.PORT), len(conflicts)))
    for k, v in sorted(stats.items()):
        print("  %-13s %-9s %s %6d" % (k + (v,)))
    for k, v in sorted(uses.items()):
        print("  %s: %d source uses of the renamed function/global placeholders" % (k, v))
    for k, v in sorted(found.items()):
        print("  %-86s %5d" % (k, v))
    if a.vdb_xml:
        vdb_xml_check(snap, a.vdb_xml)


if __name__ == "__main__":
    main()
