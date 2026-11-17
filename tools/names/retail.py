"""Retail ELF access for the names layer (docs/names-layer.md): the snapshot's orig/SLUS_210.50 bytes, function starts
from the snapshot's assembly/asm glabels, and a small MIPS register tracker for decoding call arguments."""
import os
import re
import struct

GLABEL = re.compile(r"^glabel (\S+)\s*\n\s*/\* [0-9A-F]+ ([0-9A-F]{8}) ", re.M)
DLABEL = re.compile(r"^dlabel (\S+)\s*\n\s*/\* [0-9A-F]+ ([0-9A-F]{8}) ", re.M)
REG = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
       "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
SAVED = {16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30}


class Elf:
    def __init__(self, snap):
        self.d = open(os.path.join(snap, "orig/SLUS_210.50"), "rb").read()
        phoff, = struct.unpack_from("<I", self.d, 0x1C)
        n, = struct.unpack_from("<H", self.d, 0x2C)
        self.segs = []
        for i in range(n):
            _, off, va, _, fs, ms = struct.unpack_from("<6I", self.d, phoff + i * 32)
            if fs:
                self.segs.append((va, va + fs, off))
        self.gp = self.reginfo_gp()

    def reginfo_gp(self):
        """ri_gp_value, the last word of the .reginfo section (0x4E8670 for retail)."""
        shoff, = struct.unpack_from("<I", self.d, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<3H", self.d, 0x2E)
        sh = [struct.unpack_from("<10I", self.d, shoff + i * shentsize) for i in range(shnum)]
        strtab = sh[shstrndx][4]
        for h in sh:
            if self.d[strtab + h[0]:].split(b"\0", 1)[0] == b".reginfo":
                return struct.unpack_from("<I", self.d, h[4] + 20)[0]
        raise SystemExit("retail: no .reginfo in the ELF")

    def off(self, a):
        for lo, hi, o in self.segs:
            if lo <= a < hi:
                return o + a - lo
        return None

    def word(self, a):
        o = self.off(a)
        return None if o is None else struct.unpack_from("<I", self.d, o)[0]

    def cstr(self, a):
        o = self.off(a)
        if o is None:
            return None
        e = self.d.find(b"\0", o)
        s = self.d[o:e]
        try:
            t = s.decode("ascii")
        except UnicodeDecodeError:
            return None
        return t if all(32 <= ord(c) < 127 for c in t) else None


def labels(snap):
    """{address: name} of every glabel (functions) and dlabel (data) in the snapshot's assembly/asm."""
    funcs, data = {}, {}
    for dp, _, fs in os.walk(os.path.join(snap, "assembly/asm")):
        for f in fs:
            if f.endswith(".s"):
                t = open(os.path.join(dp, f), errors="replace").read()
                for m in GLABEL.finditer(t):
                    funcs[int(m.group(2), 16)] = m.group(1)
                for m in DLABEL.finditer(t):
                    data[int(m.group(2), 16)] = m.group(1)
    return funcs, data


def sext(v):
    return v - 0x10000 if v & 0x8000 else v


def calls(elf, start, end, targets):
    """Forward over [start, end): register values at each jal to one of `targets` (address -> kind). Values: ('k', n)
    a constant, ('ld', n, addr) the initial value n of the word at a constant address, ('this', off) the function's a0 at entry plus off, ('gp', off) $gp-relative address, None unknown.
    Straight-line tracking (branches ignored); a call clobbers the caller-saved registers. Yields (site, target,
    regs) with regs as seen by the callee (the delay slot applied)."""
    r = [None] * 32
    r[0] = ("k", 0)
    r[4] = ("this", 0)
    r[28] = ("k", elf.gp)
    pend = None
    a = start
    while a < end:
        w = elf.word(a)
        op, rs, rt, rd = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
        imm, fn = w & 0xFFFF, w & 63
        val, dst = None, None
        if op == 0x0F:                               # lui
            dst, val = rt, ("k", imm << 16)
        elif op in (0x09, 0x19, 0x08, 0x18):         # addiu daddiu addi daddi
            dst = rt
            b = r[rs]
            if b and b[0] in ("k", "this"):
                val = (b[0], (b[1] + sext(imm)) & 0xFFFFFFFF if b[0] == "k" else b[1] + sext(imm))
        elif op == 0x0D:                             # ori
            dst = rt
            b = r[rs]
            if b and b[0] == "k":
                val = ("k", b[1] | imm)
        elif op == 0 and fn in (0x21, 0x2D, 0x25) and (rs == 0 or rt == 0):   # addu daddu or: a move
            dst, val = rd, r[rt if rs == 0 else rs]
        elif op == 0 and fn in (0x21, 0x2D):
            dst = rd
        elif op == 0 and fn == 9:                    # jalr
            dst = rd
        elif op == 0x23 and r[rs] and r[rs][0] == "k":   # lw from a known address: its initial value
            dst = rt
            w2 = elf.word((r[rs][1] + sext(imm)) & 0xFFFFFFFF)
            val = ("ld", w2, (r[rs][1] + sext(imm)) & 0xFFFFFFFF) if w2 is not None else None
        elif op in (0x23, 0x20, 0x21, 0x24, 0x25, 0x37, 0x1E, 0x27, 0x0A, 0x0B, 0x0C, 0x0E, 0x1A, 0x1B):
            dst = rt                                 # loads, slti, andi, xori, ldl/ldr: unknown result
        elif op == 0 and fn not in (0x08, 0x09, 0x0C, 0x0D, 0x0F, 0x18, 0x19, 0x1A, 0x1B, 0x10, 0x12, 0x11, 0x13):
            dst = rd                                 # other ALU ops
        elif op == 0x1C:                             # MMI: rd
            dst = rd
        if pend is not None:
            # this is the delay slot of a call: apply it, then the call happens
            if dst:
                r[dst] = val
            site, tgt = pend
            if tgt in targets:
                yield site, tgt, list(r)
            for i in range(32):
                if i not in SAVED and i:
                    r[i] = None
            pend = None
        else:
            if op == 3:
                pend = (a, ((a & 0xF0000000) | ((w & 0x3FFFFFF) << 2)))
            elif op == 0 and fn == 9:
                pend = (a, None)
            elif dst:
                r[dst] = val
        a += 4
