#!/usr/bin/env python3
# gen1: the constant-fold scan.
"""gen1: the constant-fold class (vu0, func_001D1130: MWCC folds `1.0f - 0.03f` round-to-nearest; the host
chops it at run time). Per function of a generation:
  retail constants: every float the function loads as a constant (lwc1 of a symbol / %lo address, read from the ELF;
  lui [+ ori] moved into an FPR / VU register by mtc1 / qmtc2);
  source literals: every float literal of the function's generated unit (its _inlN helpers included);
  a fold: a retail constant that is not a source literal but is nearest(a op b) for two source literals (+ - * /);
  it matters when nearest(a op b) != chop(a op b) (the host computes the chop at run time).
Rows with host_differs = DIFFERS are candidates for config/fold_sites.tsv: check each against the source (a plain literal
pair `1.0f / 255.0f` is folded by C++ too and needs no entry; only constants that reach the op as ps2f values do).
Usage: fold_scan.py --decomp SNAPSHOT --elf SLUS_210.50 OUT.tsv GEN [GEN ...]   (GEN: a decomp2host output directory)"""
import glob
import os
import re
import struct
import sys
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decomp2host as d  # noqa: E402

DECOMP = None
ELF = None
PORT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../.."))
LINE = re.compile(r"^\s*/\* [0-9A-F]+ ([0-9A-F]{8}) [0-9A-F]{8} \*/\s+(\S+)\s*(.*)$")
FLIT = re.compile(r"(?<![\w.])(\d+\.\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?|\d+[eE][-+]?\d+)[fF]\b")


def f32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def val(b):
    return struct.unpack("<f", struct.pack("<I", b))[0]


def chop(exact):
    """float32 bits of an exact Fraction, rounded toward zero (finite, normal range)."""
    if exact == 0:
        return 0
    n = f32(float(exact))
    v = Fraction(val(n))
    if abs(v) > abs(exact):
        n -= 1   # one ulp toward zero (same sign: magnitude down)
    return n


class Elf:
    def __init__(self, path):
        d = open(path, "rb").read()
        self.d = d
        phoff, = struct.unpack_from("<I", d, 0x1C)
        phnum, = struct.unpack_from("<H", d, 0x2C)
        self.seg = []
        for i in range(phnum):
            t, off, va, pa, fs, ms = struct.unpack_from("<IIIIII", d, phoff + 32 * i)
            if t == 1:
                self.seg.append((va, off, fs))

    def word(self, a):
        for va, off, fs in self.seg:
            if va <= a < va + fs - 3:
                return struct.unpack_from("<I", self.d, off + a - va)[0]
        return None


def symbols():
    s = {}
    for fn in ("config/symbol_addrs_b3names.txt", "config/symbol_addrs.txt"):
        for l in open(os.path.join(DECOMP, fn), errors="replace"):
            m = re.match(r"\s*(\S+)\s*=\s*0x([0-9A-Fa-f]+);", l)
            if m:
                s.setdefault(m.group(1), int(m.group(2), 16))
    return s


def load_asm():
    out, cur = {}, None
    for f in glob.glob(os.path.join(DECOMP, "assembly/asm/**/*.s"), recursive=True):
        for line in open(f, errors="replace"):
            if line.startswith(("nonmatching ", "glabel ")):
                cur = None
            m = LINE.match(line)
            if not m:
                continue
            pc = int(m.group(1), 16)
            if cur is None:
                cur = pc
                out[cur] = []
            out[cur].append((pc, m.group(2), m.group(3).strip()))
    return out


def retail_constants(ins, elf, syms):
    """[(pc, bits)] of float constants the function loads."""
    res = []
    hi = {}   # gpr -> (pc, value so far)
    for pc, mn, ops in ins:
        if mn == "lwc1":
            m = re.search(r"%(?:gp_rel|lo)\(([A-Za-z_]\w*)(?:\s*\+\s*(0x[0-9A-Fa-f]+|\d+))?\)", ops)
            base = None
            if m:
                base = syms.get(m.group(1))
                if base is None and re.fullmatch(r"D_[0-9A-Fa-f]{8}", m.group(1)):
                    base = int(m.group(1)[2:], 16)
            if base is not None:
                a = base + (int(m.group(2), 0) if m.group(2) else 0)
                w = elf.word(a)
                if w is not None:
                    res.append((pc, w, m.group(1)))
            continue
        m = re.match(r"(\$\w+),\s*\((0x[0-9A-Fa-f]+)\s*>>\s*16\)", ops) if mn == "lui" else None
        if m:
            hi[m.group(1)] = (pc, int(m.group(2), 16) & 0xFFFF0000)
            continue
        if mn == "lui":
            m = re.match(r"(\$\w+),\s*(0x[0-9A-Fa-f]+|\d+)$", ops)
            if m:
                hi[m.group(1)] = (pc, (int(m.group(2), 0) << 16) & 0xFFFFFFFF)
                continue
        if mn == "ori":
            m = re.match(r"(\$\w+),\s*(\$\w+),\s*\(?(0x[0-9A-Fa-f]+|\d+)", ops)
            if m and m.group(2) in hi:
                p0, v = hi[m.group(2)]
                hi[m.group(1)] = (p0, v | (int(m.group(3), 0) & 0xFFFF))
                continue
        if mn in ("mtc1", "qmtc2", "qmtc2.ni", "qmtc2.i"):
            r = ops.split(",")[0].strip()
            if r in hi:
                res.append((hi[r][0], hi[r][1], None))
            continue
        # a write to a tracked register ends its constant
        dst = ops.split(",")[0].strip() if ops else ""
        if dst in hi and mn not in ("sw", "sh", "sb", "sd", "sq", "swc1"):
            del hi[dst]
    return res


def main():
    global DECOMP, ELF
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("out")
    ap.add_argument("gens", nargs="+")
    a = ap.parse_args()
    DECOMP, ELF, dst, gens = a.decomp, a.elf, a.out, a.gens
    elf, syms, asm = Elf(ELF), symbols(), load_asm()
    hdr_lits = set()
    for h in glob.glob(os.path.join(PORT, "include/gtmath/b3/*.h")):
        for m in FLIT.finditer(open(h, errors="replace").read()):
            b = f32(float(m.group(1))); hdr_lits.add(b); hdr_lits.add(b ^ 0x80000000)
    rows, seen = [], set()
    for g in gens:
        man = {}
        for l in open(os.path.join(g, "manifest.tsv")):
            p = l.rstrip("\n").split("\t")
            if len(p) > 3 and p[0] != "address":
                man[int(p[0], 16)] = (p[1], p[2], p[3])
        unit_lits, unit_text = {}, {}
        for a, (track, name, src) in sorted(man.items()):
            if a in seen or a not in asm:
                continue
            seen.add(a)
            unit = os.path.join(g, "units", d.unit_name(src) + ".cpp")
            if not os.path.exists(unit):
                continue
            if unit not in unit_lits:
                txt = open(unit, errors="replace").read()
                unit_text[unit] = txt
                lits = {}
                for m in FLIT.finditer(txt):
                    b = f32(float(m.group(1)))
                    lits[b] = m.group(0)
                    lits[b ^ 0x80000000] = "-" + m.group(0)
                unit_lits[unit] = lits
            lits = unit_lits[unit]
            consts = retail_constants(asm[a], elf, syms)
            vals = sorted({b for b in lits if not (b & 0x80000000) or True})
            for pc, c, sym in consts:
                if c in lits or c in hdr_lits or c == 0 or c == 0x80000000:
                    continue   # a literal of the unit or of the shadow GtMath headers its inlines come from
                if sym and re.search(r"\b%s\b" % re.escape(sym), unit_text[unit]):
                    continue   # the unit reads retail's own constant (a guest global): the same bits
                expl = None
                for x in vals:
                    fx = Fraction(val(x))
                    for y in vals:
                        fy = Fraction(val(y))
                        for op in "+-*/":
                            if op == "/" and fy == 0:
                                continue
                            e = {"+": fx + fy, "-": fx - fy, "*": fx * fy, "/": fx / fy if fy else None}[op]
                            if e is None or e == 0:
                                continue
                            try:
                                near = f32(float(e))
                            except OverflowError:
                                continue
                            if near == c:
                                expl = (lits[x], op, lits[y], near, chop(e))
                                break
                        if expl:
                            break
                    if expl:
                        break
                if expl:
                    rows.append((track, "%08X" % a, name, "%08X" % pc, "%08X" % c, "%s %s %s" % expl[:3],
                                 "%08X" % expl[3], "%08X" % expl[4], "DIFFERS" if expl[3] != expl[4] else "same",
                                 os.path.basename(unit)))
    with open(dst, "w") as o:
        o.write("track\taddress\tname\tpc\tretail_const\texpression\tnearest\tchop\thost_differs\tunit\n")
        for r in sorted(rows, key=lambda r: (r[8] != "DIFFERS", r[1], r[3])):
            o.write("\t".join(r) + "\n")
    n = sum(1 for r in rows if r[8] == "DIFFERS")
    print(dst, len(rows), "folded constants;", n, "where the host's chop differs, in",
          len({r[1] for r in rows if r[8] == "DIFFERS"}), "functions")


if __name__ == "__main__":
    main()
