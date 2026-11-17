#!/usr/bin/env python3
"""census: static coverage of the native backend from boot (docs/standalone.md §8, deliverable 3 of s0native).

Starting at the retail entry _start (0x100008), walks every function reachable through
  - direct calls and tail jumps (jal, and j / b to an address outside the function),
  - code addresses formed in code (lui + addiu/ori pairs that equal a function entry: function pointers),
  - data the code addresses (lui + addiu/ori pairs into the ELF image's data): from that address, consecutive words
    that are function entries or zero are taken as a table (vtables, callback tables, MWCC's ctor table) [L: a
    heuristic; it over-approximates, never under-approximates a table that starts at the referenced address],
and marks each function native (in the table: gen/decomp, libm, platform tier) or not, with its kind and C status.
Breadth-first, so `depth` orders the to-do by how early boot needs it. jalr through registers loaded from objects
is covered only through the tables the code addresses.

  census.py --decomp SNAP --elf SLUS_210.50 --gen gen/decomp --out DIR
Writes DIR/reach.tsv (every reachable function), DIR/missing_frontier.tsv (non-native functions all of whose path
from boot is native: the next ones to provide), DIR/summary.txt.
"""
import argparse
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_table  # noqa: E402

PORT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def native_set(gen):
    nat = {}
    for line in open(os.path.join(gen, "manifest.tsv")):
        p = line.rstrip("\n").split("\t")
        if len(p) > 2 and re.fullmatch(r"[0-9A-F]{8}", p[0]):
            nat[int(p[0], 16)] = "game:" + p[1]
    for path, tag in ((os.path.join(PORT, "include/b3port/libm_overrides.h"), "libm"),
                      (os.path.join(PORT, "src/native/platform.h"), "platform")):
        for m in re.finditer(r'X\("[^"]+", 0x([0-9A-Fa-f]{8})u', open(path).read()):
            nat.setdefault(int(m.group(1), 16), tag)
    return nat


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--gen", default=os.path.join(PORT, "gen/decomp"))
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    rows = gen_table.load(a.decomp)
    fn = {r[0]: r for r in rows}           # addr -> (addr, size, kind, c, name)
    starts = sorted(fn)
    nat = native_set(a.gen)

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

    text_end = max(f + fn[f][1] for f in starts if fn[f][2] != "data")

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
        """[(how, target)] in code order: how = call / ptr / table."""
        _, size, kind, _, _ = fn[addr]
        out = []
        hi = {}   # reg -> lui value
        for k in range(size // 4):
            pc = addr + 4 * k
            w = word(pc)
            if w is None:
                break
            op = w >> 26
            rs, rt = (w >> 21) & 31, (w >> 16) & 31
            if op == 3 or op == 2:                                   # jal / j
                t = (pc & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                if (op == 3 or not (addr <= t < addr + size)) and t in fn:
                    out.append(("call", t))
            elif op == 0x0F:                                         # lui
                hi[rt] = (w & 0xFFFF) << 16
            elif op in (0x09, 0x0D) and rs in hi and addr != 0x100008:  # addiu / ori (_start: gp/stack/heap constants)
                imm = w & 0xFFFF
                v = (hi[rs] + (imm - 0x10000 if (op == 0x09 and imm & 0x8000) else imm)) & 0xFFFFFFFF
                if v in fn:
                    out.append(("ptr", v))
                elif text_end <= v < 0x02000000 or (0x100000 <= v < text_end and v not in fn):
                    out.extend(("table", t) for t in table_at(v))
        return out

    edge_cache = {}

    def edges_of(f):
        # a library function is replaced as a whole (platform tier / S5), so its own callees are not to-do items
        if fn[f][2] not in ("game", "crt0"):
            return []
        if f not in edge_cache:
            edge_cache[f] = edges(f)
        return edge_cache[f]

    root = 0x100008
    depth = {root: 0}
    via = {root: ("entry", None)}
    q = collections.deque([root])
    while q:
        f = q.popleft()
        for how, t in edges_of(f):
            if t not in depth:
                depth[t] = depth[f] + 1
                via[t] = (how, f)
                q.append(t)

    # boot order: depth-first, callees in code order (an approximation of the order execution first needs them)
    boot = []
    seen = {root}
    stack = [(root, iter(edges_of(root)))]
    boot.append((root, 0))
    while stack:
        f, it = stack[-1]
        nxt = next(it, None)
        if nxt is None:
            stack.pop()
            continue
        t = nxt[1]
        if t in seen:
            continue
        seen.add(t)
        boot.append((t, len(stack)))
        stack.append((t, iter(edges_of(t))))

    # frontier: not native, and every function on its BFS path from boot (excluding itself) is native
    # (crt0 counts as native: its host replacement is the native boot)
    def path_native(f):
        g = via[f][1]
        while g is not None:
            if g not in nat and fn[g][2] != "crt0":
                return False
            g = via[g][1]
        return True

    os.makedirs(a.out, exist_ok=True)
    order = sorted(depth, key=lambda f: (depth[f], f))
    with open(os.path.join(a.out, "reach.tsv"), "w") as t:
        t.write("depth\taddress\tname\tkind\tc\tnative\treached_by\tfrom\n")
        for f in order:
            _, size, kind, c, name = fn[f]
            how, frm = via[f]
            t.write("%d\t%08X\t%s\t%s\t%s\t%s\t%s\t%s\n" % (depth[f], f, name, kind, c, nat.get(f, "-"), how,
                                                         "%08X %s" % (frm, fn[frm][4]) if frm else ""))
    frontier = [f for f in order if f not in nat and fn[f][2] != "crt0" and path_native(f)]
    with open(os.path.join(a.out, "missing_frontier.tsv"), "w") as t:
        t.write("depth\taddress\tname\tkind\tc\treached_by\tfrom\n")
        for f in frontier:
            _, size, kind, c, name = fn[f]
            how, frm = via[f]
            t.write("%d\t%08X\t%s\t%s\t%s\t%s\t%08X %s\n" % (depth[f], f, name, kind, c, how, frm, fn[frm][4]))
    with open(os.path.join(a.out, "boot_order.tsv"), "w") as t:
        t.write("# depth-first from _start, callees in code order; todo = not native (game with C: generate; game "
                "without C: decomp S1; library: platform tier / S5)\n")
        t.write("order\tdfs_depth\taddress\tname\tkind\tc\tnative\n")
        for i, (f, d) in enumerate(boot):
            _, size, kind, c, name = fn[f]
            t.write("%d\t%d\t%08X\t%s\t%s\t%s\t%s\n" % (i, d, f, name, kind, c, nat.get(f, "-")))
    # the to-do in boot order, by who provides it
    with open(os.path.join(a.out, "todo_boot_order.tsv"), "w") as t:
        t.write("# non-native functions in boot order (boot_order.tsv); provider: generate = game code with C "
                "(decomp2host, lane native2), decomp = game code without C (S1), platform = library entry point "
                "(platform tier / S5)\n")
        t.write("order\taddress\tname\tkind\tc\tprovider\n")
        k = 0
        for f, d in boot:
            _, size, kind, c, name = fn[f]
            if f in nat or kind == "crt0":
                continue
            prov = ("generate" if c != "none" else "decomp") if kind == "game" else "platform"
            t.write("%d\t%08X\t%s\t%s\t%s\t%s\n" % (k, f, name, kind, c, prov))
            k += 1
    cnt = collections.Counter()
    for f in depth:
        _, _, kind, c, _ = fn[f]
        cnt[(kind, c, f in nat)] += 1
    total = collections.Counter()
    for r in rows:
        total[(r[2], r[3])] += 1
    with open(os.path.join(a.out, "summary.txt"), "w") as t:
        t.write("functions reachable from _start: %d of %d\n" % (len(depth), len(rows)))
        t.write("native in the table: %d (reachable and native: %d)\n" % (len(nat), sum(1 for f in depth if f in nat)))
        t.write("frontier (not native, path from boot all native): %d\n\n" % len(frontier))
        t.write("%-8s %-8s %8s %8s %8s\n" % ("kind", "c", "all", "reached", "native"))
        for (kind, c) in sorted(total):
            r = cnt[(kind, c, True)] + cnt[(kind, c, False)]
            t.write("%-8s %-8s %8d %8d %8d\n" % (kind, c, total[(kind, c)], r, cnt[(kind, c, True)]))
    print(open(os.path.join(a.out, "summary.txt")).read())
    first = [f for f in order if f not in nat and fn[f][2] != "crt0"][:1]
    for f in first:
        print("first non-native function on the way from boot: %08X %s (depth %d, via %s from %s)" % (
            f, fn[f][4], depth[f], via[f][0], fn[via[f][1]][4] if via[f][1] else "-"))


if __name__ == "__main__":
    main()
