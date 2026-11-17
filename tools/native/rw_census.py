#!/usr/bin/env python3
"""rw_census: which RenderWare 3.6 functions the game reaches, core vs driver, with sizes (boot1, the RW plan).

RW in retail is one block, 0x1D8260-0x211C30 (the decomp's assembly/asm/rw/renderware.s; kind rw in
build/native/functions.tsv). For each RW function this tool reports:
  - its layer: the source file its name comes from in the RW 3.1 PS2 SDK tree (a READING AID only: names and paths,
    never code; RW names are RW's own symbols), mapped to core (src/, world/ outside the PS2 pipelines), plugin
    (plugin/), toolkit (tool/), os (os/), driver (driver/sky2, */pipe/p2/sky2, plugin/*/sky2: the PS2 device, DMA, VU1
    and GS; driver/common's image / palette conversion counts as core); an unnamed function takes its neighbours' file when both agree (one object file is contiguous in the link);
    `?` otherwise;
  - static reach: the call closure from boot (_start through game code, then into and within RW: jal / j, code
    pointers, and data tables the code addresses; an over-approximation through tables) -- `boot`;
  - dynamic reach (optional, --trace): entered by retail code from recorded captures of game functions (rw_trace TSVs
    of the boot1 capture sets, docs/results/boot1-2026-11-05/rw; a lower bound: RW code not beneath a captured game
    function is not seen).

  rw_census.py --decomp SNAP --elf SLUS_210.50 --rw31 RWSDK31_ROOT [--trace T1.tsv ...] --out DIR
"""
import argparse
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_table  # noqa: E402

RW_LO, RW_HI = 0x001D8260, 0x00211C30
KEYWORDS = {"if", "while", "for", "switch", "return", "sizeof", "defined", "else", "do"}
DEF = re.compile(r"^(?:[A-Za-z_][\w \t\*]*?[ \t\*])?([A-Za-z_]\w*)[ \t]*\(([^;]*)$")


def layer_of(path):
    p = path.replace("\\", "/")
    if p.startswith("os/"):
        return "os"
    if p.startswith("name:"):
        return p.split(":")[1]
    if "/sky2/" in p or p.startswith("driver/sky2") or "/sky/" in p:
        return "driver"
    if p.startswith("driver/common"):   # image / palette conversion, not PS2-specific
        return "core"
    if p.startswith("os/"):
        return "os"
    if p.startswith("tool/"):
        return "toolkit"
    if p.startswith("plugin"):
        return "plugin"
    if p.startswith(("src/", "world/")):
        return "core"
    return "?"


def rw31_files(root):
    """name -> set of relative .c paths that define it (RW 3.1 PS2 tree: reading aid, names only)."""
    out = collections.defaultdict(set)
    for d, _, files in os.walk(root):
        rel_d = os.path.relpath(d, root).replace("\\", "/")
        if rel_d.startswith(("plugin.archive", "buildtools")) or "/win" in rel_d or "/xbox" in rel_d or \
                "/d3d8" in rel_d or "/gcn" in rel_d or "/opengl" in rel_d or "/nullsky" in rel_d:
            continue
        for f in files:
            if not f.endswith(".c"):
                continue
            rel = (rel_d + "/" + f) if rel_d != "." else f
            try:
                text = open(os.path.join(d, f), encoding="latin-1").read()
            except OSError:
                continue
            for line in text.split("\n"):
                if not line or line[0] in " \t#/*{}":
                    continue
                m = DEF.match(line)
                if m and m.group(1) not in KEYWORDS:
                    out[m.group(1)].add(rel)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--rw31", required=True)
    ap.add_argument("--trace", nargs="*", default=[])
    ap.add_argument("--out", required=True)
    ap.add_argument("--roots", default="", help="hex addresses: also the closure from each (game + RW followed)")
    a = ap.parse_args()

    rows = gen_table.load(a.decomp)
    fn = {r[0]: r for r in rows}   # addr -> (addr, size, kind, c, name)
    rw = sorted(f for f in fn if RW_LO <= f < RW_HI)

    # ---- layer by source file (RW 3.1 names) ----
    files = rw31_files(a.rw31)
    src = {}
    for f in rw:
        nm = fn[f][4]
        hits = files.get(nm, set())
        if len(hits) == 1:
            src[f] = next(iter(hits))
        elif len(hits) > 1:
            lay = {layer_of(h) for h in hits}
            src[f] = sorted(hits)[0] if len(lay) == 1 else None
    # names with no RW 3.1 counterpart (3.5 / 3.6 additions, renamed driver code): by prefix [L]
    rules = [(r"^(_?rwDMA|_rwSky|RpSky|RwSky|[Ss]ky[A-Z])", "driver"),
             (r"^(_?rw|_?Rw|H[MRC]alloc|Leaf|Stats|Representative|TextureDefault)", "core")]
    for f in rw:
        if src.get(f) or fn[f][4].startswith("func_"):
            continue
        for pat, lay in rules:
            if re.match(pat, fn[f][4]):
                src[f] = "name:%s:%s" % (lay, pat)
                break
    for k, f in enumerate(rw):   # unnamed / unknown: the neighbours' object file when both agree
        if src.get(f):
            continue
        prev = next((src[g] for g in reversed(rw[:k]) if src.get(g)), None)
        nxt = next((src[g] for g in rw[k + 1:] if src.get(g)), None)
        if prev and prev == nxt:
            src[f] = prev + " (neighbours)"

    for k, f in enumerate(rw):   # still unknown: the neighbours' layer when both agree (layers are contiguous runs)
        if src.get(f):
            continue
        prev = next((layer_of(src[g].replace(" (neighbours)", "")) for g in reversed(rw[:k]) if src.get(g)), None)
        nxt = next((layer_of(src[g].replace(" (neighbours)", "")) for g in rw[k + 1:] if src.get(g)), None)
        if prev and prev == nxt:
            src[f] = "name:%s:neighbour layer" % prev

    # ---- static reach: boot closure through game code and into / within RW ----
    elf = open(a.elf, "rb").read()
    phoff, = struct.unpack_from("<I", elf, 0x1C)
    phnum, = struct.unpack_from("<H", elf, 0x2C)
    segs = []
    for k in range(phnum):
        t, off, va, _, fsz = struct.unpack_from("<IIIII", elf, phoff + 32 * k)
        if t == 1 and fsz:
            segs.append((va, off, fsz))

    def word(addr):
        for va, off, fsz in segs:
            if va <= addr and addr + 4 <= va + fsz:
                return struct.unpack_from("<I", elf, off + addr - va)[0]
        return None

    starts = sorted(fn)
    text_end = max(f + fn[f][1] for f in starts)

    def table_at(addr):
        out = []
        for k in range(1024):
            w = word(addr + 4 * k)
            if w is None:
                break
            if w in fn:
                out.append(w)
            elif w != 0:
                break
        return out

    def edges(addr):
        _, size, kind, _, _ = fn[addr]
        out, hi = [], {}
        for k in range(size // 4):
            pc = addr + 4 * k
            w = word(pc)
            if w is None:
                break
            op = w >> 26
            rs, rt = (w >> 21) & 31, (w >> 16) & 31
            if op in (2, 3):
                t = (pc & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                if (op == 3 or not (addr <= t < addr + size)) and t in fn:
                    out.append(t)
            elif op == 0x0F:
                hi[rt] = (w & 0xFFFF) << 16
            elif op in (0x09, 0x0D) and rs in hi and addr != 0x100008:
                imm = w & 0xFFFF
                v = (hi[rs] + (imm - 0x10000 if (op == 0x09 and imm & 0x8000) else imm)) & 0xFFFFFFFF
                if v in fn:
                    out.append(v)
                elif text_end <= v < 0x02000000 or (0x100000 <= v < text_end and v not in fn):
                    out.extend(table_at(v))
        return out

    follow = {"game", "crt0", "rw"}   # game code and RW itself; other libraries are leaves here
    seen, q = {0x100008}, collections.deque([0x100008])
    entry_from_game = collections.defaultdict(set)
    while q:
        f = q.popleft()
        if fn[f][2] not in follow:
            continue
        for t in edges(f):
            if fn[f][2] != "rw" and fn[t][2] == "rw":
                entry_from_game[t].add(f)
            if t not in seen:
                seen.add(t)
                q.append(t)

    for r in [int(x, 16) for x in a.roots.split(",") if x]:
        cl, q2 = {r}, collections.deque([r])
        while q2:
            f = q2.popleft()
            if fn[f][2] not in follow:
                continue
            for t in edges(f):
                if t not in cl:
                    cl.add(t)
                    q2.append(t)
        cnt = collections.Counter()
        with open(os.path.join(a.out, "closure_%08X.tsv" % r), "w") as o:
            o.write("# static closure from %08X %s (game and RW code followed; other libraries are leaves)\n" % (r, fn[r][4]))
            o.write("address\tsize\tkind\tname\tlayer\n")
            for f in sorted(cl):
                s_ = src.get(f) or ""
                lay = (layer_of(s_.replace(" (neighbours)", "")) if s_ else "?") if fn[f][2] == "rw" else ""
                cnt[(fn[f][2], lay)] += 1
                cnt[(fn[f][2], lay, "bytes")] += fn[f][1]
                o.write("%08X\t%d\t%s\t%s\t%s\n" % (f, fn[f][1], fn[f][2], fn[f][4], lay))
        print("closure from %08X %s: %s" % (r, fn[r][4], ", ".join(
            "%s%s %d fn / %d B" % (k[0], "/" + k[1] if k[1] else "", v, cnt[(k[0], k[1], "bytes")])
            for k, v in sorted(cnt.items()) if len(k) == 2)))

    # ---- dynamic reach (rw_trace) ----
    dyn = collections.Counter()
    dyn_caps = collections.Counter()
    root = {}
    for t in a.trace:
        for line in open(t):
            p = line.rstrip("\n").split("\t")
            if len(p) < 6 or not re.fullmatch(r"[0-9A-F]{8}", p[0]):
                continue
            ad = int(p[0], 16)
            dyn[ad] += int(p[3])
            dyn_caps[ad] += int(p[4])
            root.setdefault(ad, p[5])

    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "rw_functions.tsv"), "w") as o:
        o.write("# rw_census: every RW function (0x1D8260-0x211C30); layer from the RW 3.1 PS2 tree's file of its name "
                "(reading aid); boot = static closure; race = entered in the recorded captures (rw_trace)\n")
        o.write("address\tsize\tname\tlayer\tfile\tboot\tfrom_game\trace_entries\trace_captures\tfirst_root\n")
        for f in rw:
            s = src.get(f) or ""
            lay = layer_of(s.replace(" (neighbours)", "")) if s else "?"
            o.write("%08X\t%d\t%s\t%s\t%s\t%s\t%d\t%d\t%d\t%s\n" % (
                f, fn[f][1], fn[f][4], lay, s, "yes" if f in seen else "", len(entry_from_game.get(f, ())),
                dyn.get(f, 0), dyn_caps.get(f, 0), root.get(f, "")))
    # summary by layer
    agg = collections.defaultdict(lambda: [0, 0, 0, 0, 0, 0, 0, 0])
    for f in rw:
        s = src.get(f) or ""
        lay = layer_of(s.replace(" (neighbours)", "")) if s else "?"
        size = fn[f][1]
        r = agg[lay]
        r[0] += 1; r[1] += size
        if f in seen:
            r[2] += 1; r[3] += size
        if dyn.get(f):
            r[4] += 1; r[5] += size
        if f in seen or dyn.get(f):
            r[6] += 1; r[7] += size
    with open(os.path.join(a.out, "rw_summary.txt"), "w") as o:
        o.write("RW 3.6 in retail: %d functions, %d bytes (0x%X-0x%X)\n" % (len(rw), sum(fn[f][1] for f in rw), RW_LO, RW_HI))
        o.write("entry points called from game code (static): %d\n\n" % len(entry_from_game))
        o.write("%-8s %14s %16s %16s %16s\n" % ("layer", "all fn/bytes", "boot-static", "race-dynamic", "either"))
        for lay in ("core", "plugin", "toolkit", "os", "driver", "?"):
            if lay not in agg:
                continue
            r = agg[lay]
            o.write("%-8s %6d %7d %7d %8d %7d %8d %7d %8d\n" % (lay, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]))
        files_by = collections.defaultdict(lambda: [0, 0, 0])
        for f in rw:
            s = (src.get(f) or "?").replace(" (neighbours)", "")
            files_by[s][0] += 1
            files_by[s][1] += fn[f][1]
            if f in seen or dyn.get(f):
                files_by[s][2] += 1
        o.write("\nby source file (RW 3.1 path; functions, bytes, reached):\n")
        for s, (n, b, r) in sorted(files_by.items(), key=lambda kv: (layer_of(kv[0]), kv[0])):
            o.write("  %-8s %-48s %4d %7d %4d\n" % (layer_of(s) if s != "?" else "?", s, n, b, r))
    print(open(os.path.join(a.out, "rw_summary.txt")).read())


if __name__ == "__main__":
    main()
