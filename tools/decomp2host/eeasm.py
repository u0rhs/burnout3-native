#!/usr/bin/env python3
"""eeasm: translate the decomp's EE / VU0 inline asm into C++ on the host model b3host/eeasm.h (docs/decomp-on-host.md).

Used by decomp2host.py (P6). Each `asm [volatile] ("template" : outputs : inputs : clobbers)` statement is parsed at
generation time and replaced by one C++ block: operand registers become locals, explicit registers ($vf1, $ACC, $Q,
$I, R, $viN, $N, $fN) the per-thread b3asm::State, and every instruction one statement on b3asm/b3vu/b3fp. Anything
not understood (an instruction, an operand form, a constraint, a branch, a macro-built template) raises AsmError with
the reason: never a silent skip.

Census mode (coverage driven by real use):
  eeasm.py --census DECOMP_ROOT [--list-unsupported]
translates every asm statement in the decomp's src/ and include/ and prints, per mnemonic, how often it occurs and
whether it is supported, plus the statements that fail and why.

Self-check: eeasm.py --selftest
"""
import collections
import glob
import os
import re
import sys


class AsmError(Exception):
    pass


# vu0flags (Fable): EE code that reads VU0's MAC / STATUS / CLIP flags (cfc2 from $vi16 / $vi17 / $vi18) would see what the
# generated VU0 programs no longer keep up (tools/native/vu0_aot.py: only the observable flag pushes are made). None does
# (a census of every function, 2026-11-23); if one ever appears the whole generation stops here -- never a dropped unit:
# a BaseException, so decomp2host's keep-going (except Exception) cannot swallow it
class VuFlagRead(BaseException):
    pass


# ---------------------------------------------------------------- statement location / parsing
ASM_KW = re.compile(r"\b(?:__asm__|asm)\b(\s*(?:volatile|__volatile__)\s*)?\s*\(")


def find_asm(masked):
    """(start, end) of every asm statement's `asm ... )` in already comment/string-masked text."""
    out = []
    for mm in ASM_KW.finditer(masked):
        i = mm.end() - 1
        d = 0
        for j in range(i, len(masked)):
            if masked[j] == "(":
                d += 1
            elif masked[j] == ")":
                d -= 1
                if d == 0:
                    out.append((mm.start(), j + 1))
                    break
        else:
            raise AsmError("unbalanced asm statement")
    return out


def split_top(s, sep):
    """Split on `sep` outside parentheses/brackets and string literals."""
    out, d, cur, i = [], 0, "", 0
    while i < len(s):
        c = s[i]
        if c == '"':
            j = i + 1
            while j < len(s) and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            cur += s[i:j + 1]
            i = j + 1
            continue
        if c in "([{":
            d += 1
        elif c in ")]}":
            d -= 1
        if c == sep and d == 0:
            out.append(cur)
            cur = ""
        else:
            cur += c
        i += 1
    out.append(cur)
    return out


class Operand:
    def __init__(self, idx, cons, expr, out):
        self.idx, self.cons, self.expr, self.out = idx, cons, expr.strip(), out
        c = cons.lstrip("=+&")
        self.rw = "+" if "+" in cons else ("=" if "=" in cons else "")
        self.tied = None
        if re.fullmatch(r"\d+", c):
            self.kind = "tied"
            self.tied = int(c)
        elif c in ("j", "r", "f", "m", "i", "n"):
            self.kind = {"n": "i"}.get(c, c)
        else:
            raise AsmError("constraint %r not supported" % cons)
        if out and not self.rw:
            raise AsmError("output operand without = or +: %r" % cons)


def parse_stmt(stmt):
    """`asm volatile ("..." : outs : ins : clobbers)` -> (template, [Operand], clobbers)."""
    mm = ASM_KW.match(stmt)
    if not mm:
        raise AsmError("not an asm statement")
    body = stmt[mm.end():-1]
    parts = split_top(body, ":")
    if len(parts) > 4:
        raise AsmError("asm goto / too many colons not supported")
    t = parts[0].strip()
    lits = re.fullmatch(r'((?:"(?:[^"\\]|\\.)*"\s*)+)', t)
    if not lits:
        raise AsmError("template is not a string literal (macro-built): %s" % t[:60])
    tmpl = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', t))
    tmpl = tmpl.replace("\\n", "\n").replace("\\t", "\t").replace('\\"', '"').replace("\\\\", "\\")
    ops = []
    for k, sec in enumerate(parts[1:3]):
        for o in split_top(sec, ","):
            o = o.strip()
            if not o:
                continue
            om = re.fullmatch(r'(?:\[\s*\w+\s*\]\s*)?"([^"]*)"\s*\((.*)\)', o, re.S)
            if not om:
                raise AsmError("operand not understood: %s" % o)
            ops.append(Operand(len(ops), om.group(1), om.group(2), k == 0))
    clob = [c.strip().strip('"') for c in (parts[3].split(",") if len(parts) > 3 else []) if c.strip()]
    for o in ops:
        if o.kind == "tied":
            if o.out or o.tied >= len(ops) or not ops[o.tied].out:
                raise AsmError("tied operand %d not supported" % o.idx)
    return tmpl, ops, clob


def split_insns(tmpl):
    out = []
    for line in tmpl.split("\n"):
        line = line.split("#")[0]
        for ins in line.split(";"):
            ins = ins.strip()
            if ins:
                out.append(ins)
    return out


# ---------------------------------------------------------------- registers
GPR_NAMES = {"zero": 0, "at": 1, "v0": 2, "v1": 3, "a0": 4, "a1": 5, "a2": 6, "a3": 7, "t0": 8, "t1": 9, "t2": 10,
             "t3": 11, "t4": 12, "t5": 13, "t6": 14, "t7": 15, "s0": 16, "s1": 17, "s2": 18, "s3": 19, "s4": 20,
             "s5": 21, "s6": 22, "s7": 23, "t8": 24, "t9": 25, "k0": 26, "k1": 27, "gp": 28, "sp": 29, "fp": 30,
             "s8": 30, "ra": 31}
LANE = {"x": 0, "y": 1, "z": 2, "w": 3}


def dest_mask(d):
    if not re.fullmatch(r"x?y?z?w?", d) or not d:
        raise AsmError("bad dest field .%s" % d)
    return sum(1 << LANE[c] for c in d)


# ---------------------------------------------------------------- native3: the baked VU binding (DECOMP2HOST_VUBIND)
# A COP2 macro instruction's key: (base mnemonic, dest mask), the same for the decomp's spelling and retail's word.
_SP1 = {0x1C: "vmulq", 0x1D: "vmaxi", 0x1E: "vmuli", 0x1F: "vminii", 0x20: "vaddq", 0x21: "vmaddq", 0x22: "vaddi",
        0x23: "vmaddi", 0x24: "vsubq", 0x25: "vmsubq", 0x26: "vsubi", 0x27: "vmsubi", 0x28: "vadd", 0x29: "vmadd",
        0x2A: "vmul", 0x2B: "vmax", 0x2C: "vsub", 0x2D: "vmsub", 0x2E: "vopmsub", 0x2F: "vmini"}
_SP1_BC = ["vadd", "vsub", "vmadd", "vmsub", "vmax", "vmini", "vmul"]   # funct 0x00-0x1B: <op><bc>, 4 each
_SP2_BC = {0: "vadda", 1: "vsuba", 2: "vmadda", 3: "vmsuba", 6: "vmula"}   # ext 0x00-0x0F, 0x18-0x1B
_SP2 = {0x10: "vitof0", 0x11: "vitof4", 0x12: "vitof12", 0x13: "vitof15", 0x14: "vftoi0", 0x15: "vftoi4",
        0x16: "vftoi12", 0x17: "vftoi15", 0x1C: "vmulaq", 0x1D: "vabs", 0x1E: "vmulai", 0x1F: "vclipw", 0x20: "vaddaq",
        0x21: "vmaddaq", 0x22: "vaddai", 0x23: "vmaddai", 0x24: "vsubaq", 0x25: "vmsubaq", 0x26: "vsubai",
        0x27: "vmsubai", 0x28: "vadda", 0x29: "vmadda", 0x2A: "vmula", 0x2C: "vsuba", 0x2D: "vmsuba", 0x2E: "vopmula",
        0x30: "vmove", 0x31: "vmr32"}
_SP2_FT_DST = ("vitof", "vftoi", "vabs", "vmove", "vmr32")   # special2 forms whose destination is the ft field


def vu_key(mn):
    """The key of a decomp-spelled COP2 vector instruction (`vaddw.x`, `vmove.xyzw`), None for anything else."""
    m = re.fullmatch(r"(v[a-z0-9]+)(?:\.([xyzw]+))?", mn)
    if not m or m.group(1) in ("vdiv", "vsqrt", "vrsqrt", "vwaitq", "vnop", "vrinit", "vrxor", "vrnext", "vrget",
                               "vcallms", "vcallmsr", "vlqi", "vsqi", "vlqd", "vsqd", "vmtir", "vmfir", "vilwr", "viswr"):
        return None
    return (m.group(1), dest_mask(m.group(2)) if m.group(2) else 15)


def retail_vu(word):
    """A retail COP2 macro word -> (key, destination vf register or None for ACC / none), or None."""
    if (word >> 26) != 0x12 or not (word >> 25) & 1:
        return None
    M = ((word >> 24) & 1) | (((word >> 23) & 1) << 1) | (((word >> 22) & 1) << 2) | (((word >> 21) & 1) << 3)
    ft, fd, funct = (word >> 16) & 31, (word >> 6) & 31, word & 63
    if funct < 0x1C:
        return ((_SP1_BC[funct >> 2] + "xyzw"[funct & 3], M), fd)
    if funct < 0x3C:
        n = _SP1.get(funct)
        return ((n, M), fd) if n else None
    ext = (fd << 2) | (funct & 3)
    if ext < 0x10 or 0x18 <= ext < 0x1C:
        n = _SP2_BC.get(ext >> 2)
        return ((n + "xyzw"[ext & 3], M), None) if n else None
    n = _SP2.get(ext)
    if not n:
        return None
    return ((n, M), ft if n.startswith(_SP2_FT_DST) else None)


def vu_keys_of(stmt):
    """The keys of an asm statement's COP2 vector instructions, in order."""
    try:
        tmpl, _, _ = parse_stmt(stmt)
    except AsmError:
        return []
    out = []
    for ins in split_insns(tmpl):
        k = vu_key(ins.split(None, 1)[0]) if ins.strip() else None
        if k is not None:
            out.append(k)
    return out


class VuBinding:
    """The table (retail pc -> retail destination register) and, per function, the match of the decomp's COP2 vector
    instructions with retail's: the k-th source occurrence of a key is the k-th retail one when both counts agree, and
    it is bound when the table names that pc with the same register (anything else: today's output)."""

    def __init__(self, path):
        self.table = {}
        for l in open(path):
            t = l.split("#")[0].split()
            if len(t) == 2:
                self.table[int(t[0], 16)] = int(t[1])
        self.bound = 0

    def resolver(self, code_bytes, func_addr, source_keys):
        retail = {}
        for k in range(0, len(code_bytes) - 3, 4):
            w = int.from_bytes(code_bytes[k:k + 4], "little")
            r = retail_vu(w)
            if r is not None:
                retail.setdefault(r[0], []).append((func_addr + k, r[1]))
        counts = {}
        for key in source_keys:
            counts[key] = counts.get(key, 0) + 1
        seen = {}
        binding = self

        def resolve(key):
            n = seen.get(key, 0)
            seen[key] = n + 1
            rs = retail.get(key, [])
            if counts.get(key) != len(rs) or n >= len(rs):
                return None
            pc, dst = rs[n]
            if dst is None or binding.table.get(pc) != dst:
                return None
            binding.bound += 1
            return dst
        return resolve


class Emitter:
    """Resolves operand tokens to C++ expressions; run twice (scan, then emit)."""

    def __init__(self, ops, scan, info, resolver=None):
        self.ops, self.scan, self.info = ops, scan, info
        self.written = set()
        self.resolver = resolver   # native3: the baked VU binding (VuBinding.resolver), None = off
        self.site_dst = None       # the current instruction's retail destination register when bound

    def mirror(self, d_tok, d):
        """native3: at a bound site whose destination is a "=j" / "+j" operand, the result also goes into the retail
        destination register of the native VU state, so later stale-lane reads see what retail's register holds."""
        if self.site_dst is None or self.site_dst == 0 or self.tok(d_tok)[0] != "op":
            return ""
        return " _S.vf[%d] = %s;" % (self.site_dst, d)

    # --- token classification
    def tok(self, t):
        t = t.strip()
        m = re.fullmatch(r"%(\d+)(?:\.?([xyzw]))?", t) or re.fullmatch(r"%\[(\w+)\](?:\.?([xyzw]))?", t)
        if m:
            k = int(m.group(1)) if m.group(1).isdigit() else None
            if k is None or k >= len(self.ops):
                raise AsmError("operand reference %s out of range" % t)
            op = self.ops[k]
            if op.kind == "tied":
                op = self.ops[op.tied]
            return ("op", op, m.group(2))
        m = re.fullmatch(r"\$?vf(\d+)(?:\.?([xyzw]))?", t)
        if m and int(m.group(1)) < 32:
            return ("vf", int(m.group(1)), m.group(2))
        if re.fullmatch(r"\$?ACC", t):
            return ("acc", None, None)
        if re.fullmatch(r"\$?Q", t):
            return ("q", None, None)
        if re.fullmatch(r"\$?I", t):
            return ("i", None, None)
        if re.fullmatch(r"\$?R", t):
            return ("r", None, None)
        m = re.fullmatch(r"\$vi(\d+)", t)
        if m:
            return ("vi", int(m.group(1)), None)
        m = re.fullmatch(r"\$f(\d+)", t)
        if m and int(m.group(1)) < 32:
            return ("fpr", int(m.group(1)), None)
        m = re.fullmatch(r"\$(\d+)", t)
        if m and int(m.group(1)) < 32:
            return ("gpr", int(m.group(1)), None)
        m = re.fullmatch(r"\$([a-z][a-z0-9]*)", t)
        if m and m.group(1) in GPR_NAMES:
            return ("gpr", GPR_NAMES[m.group(1)], None)
        m = re.fullmatch(r"(-?(?:0x[0-9A-Fa-f]+|\d+))?\s*\((.+)\)", t)
        if m:
            return ("mem", m.group(2), int(m.group(1), 0) if m.group(1) else 0)
        m = re.fullmatch(r"-?(?:0x[0-9A-Fa-f]+|\d+)", t)
        if m:
            return ("imm", int(t, 0), None)
        # boot2: a guest global named by its address as a memory operand (`lwc1 $f3, D_004E0E00`: the assembler's
        # absolute %hi / %lo pair, B3VehiclePhysicsAttribs.cpp Construct)
        m = re.fullmatch(r"D_([0-9A-Fa-f]{8})", t)
        if m:
            return ("absmem", int(m.group(1), 16), 0)
        raise AsmError("operand %r not understood" % t)

    def local(self, op):
        return "_o%d" % op.idx

    def note(self, key, what):
        if self.scan:
            self.info.setdefault(key, set()).add(what)

    # --- VU vector registers
    def vf(self, t, want_field=None):
        """Read a VF register (value expression). want_field: a broadcast lane letter the token may carry."""
        k, a, f = self.tok(t)
        if f and want_field and f != want_field:
            raise AsmError("broadcast field %s on %s does not match the instruction's .%s" % (f, t, want_field))
        if f and not want_field:
            raise AsmError("unexpected field on %s" % t)
        if k == "vf":
            return "b3asm::vf0()" if a == 0 else "_S.vf[%d]" % a
        if k == "op":
            if a.kind != "j":
                raise AsmError("%s is not a VU register operand (constraint %s)" % (t, a.cons))
            self.note(a.idx, "read")
            return self.local(a)
        raise AsmError("%s is not a VF register" % t)

    def vf_dst(self, t):
        k, a, f = self.tok(t)
        if f:
            raise AsmError("field on destination %s" % t)
        if k == "vf":
            if a == 0:
                return None   # writes to vf0 are discarded
            return "_S.vf[%d]" % a
        if k == "op":
            if a.kind != "j":
                raise AsmError("%s is not a VU register operand" % t)
            # An input register the template overwrites (the decomp does this, e.g. CreateDeformedHull_inl3): the local
            # copy changes, the C object does not [L: MWCC does not reload it either; the drafts never read it after].
            self.note(a.idx, "write")
            return self.local(a)
        raise AsmError("%s is not a VF register" % t)

    def base(self, dst_tok, dst, src_expr, mask):
        """The previous value of a destination written under `mask`. A pure output ("=j") not yet written has no
        defined previous value on the hardware (whatever its register held): its unwritten lanes are taken from the
        instruction's first source (the GtMathPs2 shadow headers' convention) [G]."""
        k, a, _ = self.tok(dst_tok)
        if k == "op" and a.rw == "=" and a.idx not in self.written and mask != 15 and self.site_dst is not None:
            # native3: a bound site: the unwritten lanes are what retail's destination register holds
            self.written.add(a.idx)
            return "b3asm::vf0()" if self.site_dst == 0 else "_S.vf[%d]" % self.site_dst
        if k == "op" and a.rw == "=" and a.idx not in self.written and mask != 15 and src_expr is not None:
            self.written.add(a.idx)
            return src_expr
        if k == "op":
            self.written.add(a.idx)
        return dst

    # --- GPRs (128-bit values)
    def gpr(self, t):
        k, a, f = self.tok(t)
        if f:
            raise AsmError("field on GPR %s" % t)
        if k == "gpr":
            return "b3asm::Q{}" if a == 0 else "_S.gpr[%d]" % a
        if k == "op":
            if a.kind == "r":
                self.note(a.idx, "value")
                return self.local(a)
            if a.kind == "i":
                return "b3asm::gpr_in(%s)" % a.expr
            raise AsmError("%s is not a GPR operand (constraint %s)" % (t, a.cons))
        if k == "imm" and a == 0:
            return "b3asm::Q{}"
        raise AsmError("%s is not a GPR" % t)

    def gpr_dst(self, t):
        k, a, f = self.tok(t)
        if k == "gpr":
            return None if a == 0 else "_S.gpr[%d]" % a
        if k == "op" and a.kind == "r":
            self.note(a.idx, "write")   # an input written: local copy only (see vf_dst)
            self.written.add(a.idx)
            return self.local(a)
        raise AsmError("%s is not a GPR destination" % t)

    def imm(self, t):
        k, a, f = self.tok(t)
        if k == "imm":
            return str(a)
        if k == "op" and a.kind == "i":
            return "(%s)" % a.expr
        raise AsmError("%s is not an immediate" % t)

    # --- FPRs (bits)
    def fpr(self, t):
        k, a, f = self.tok(t)
        if k == "fpr":
            return "_S.fpr[%d]" % a
        if k == "op" and a.kind == "f":
            self.note(a.idx, "read")
            return self.local(a)
        raise AsmError("%s is not an FPR" % t)

    def fpr_dst(self, t):
        k, a, f = self.tok(t)
        if k == "fpr":
            return "_S.fpr[%d]" % a
        if k == "op" and a.kind == "f":
            self.note(a.idx, "write")   # an input written: local copy only (see vf_dst)
            return self.local(a)
        raise AsmError("%s is not an FPR destination" % t)

    # --- memory
    def addr(self, t, length):
        k, a, off = self.tok(t)
        if k == "op" and a.kind == "m":
            return self.local(a)
        if k == "absmem":
            return "b3asm::mem(0x%08Xull, 0, %d)" % (a, length)
        if k != "mem":
            raise AsmError("%s is not a memory operand" % t)
        bk, ba, bf = self.tok(a)
        if bk == "gpr":
            return "b3asm::mem(b3asm::lo64(%s), %d, %d)" % ("b3asm::Q{}" if ba == 0 else "_S.gpr[%d]" % ba, off, length)
        if bk == "op" and ba.kind == "r":
            self.note(ba.idx, "base")
            if self.info.get(("hostbase", ba.idx)):
                return "(_b%d + %d)" % (ba.idx, off)
            return "b3asm::mem(b3asm::lo64(%s), %d, %d)" % (self.local(ba), off, length)
        raise AsmError("memory base %s not supported" % a)


# ---------------------------------------------------------------- instructions
VU_ARITH = re.compile(r"v(madd|msub|add|sub|mul|max|mini)(a?)([xyzwiq]?)\.([xyzw]+)$")
MMI3 = {"pcpyld", "pcpyud", "pextlw", "pextuw", "pextlh", "pextuh", "pextlb", "ppach", "ppacb", "ppacw", "pmaxw",
        "pminw", "paddw", "psubw", "paddh", "pand", "por", "pxor", "pnor"}
COP1_3 = {"add.s": "add_s", "sub.s": "sub_s", "mul.s": "mul_s", "div.s": "div_s", "max.s": "max_s", "min.s": "min_s",
          "rsqrt.s": "rsqrt_s"}
LOADS = {"lw": ("std::int32_t", "set64(%s, b3asm::sx32(static_cast<std::uint32_t>(%s)))", 4),
         "lwu": ("std::uint32_t", "set64(%s, %s)", 4),
         "lh": ("std::int16_t", "set64(%s, static_cast<std::uint64_t>(static_cast<std::int64_t>(%s)))", 2),
         "lhu": ("std::uint16_t", "set64(%s, %s)", 2),
         "lb": ("std::int8_t", "set64(%s, static_cast<std::uint64_t>(static_cast<std::int64_t>(%s)))", 1),
         "lbu": ("std::uint8_t", "set64(%s, %s)", 1),
         "ld": ("std::uint64_t", "set64(%s, %s)", 8)}
STORES = {"sw": ("std::uint32_t", 4), "sh": ("std::uint16_t", 2), "sb": ("std::uint8_t", 1), "sd": ("std::uint64_t", 8)}
NOPS = {"nop", "vnop", "vwaitq", "sync", "sync.l", "sync.p", "ssnop"}
REFUSED = {
    "mfc0": "privileged (COP0)", "mtc0": "privileged (COP0)",
    "bc0f": "COP0 branch", "bc0t": "COP0 branch", "syscall": "syscall", "break": "break",
}
BRANCHES = re.compile(r"^(b\w*|j|jal|jr|jalr|beq\w*|bne\w*|bgez\w*|bgtz\w*|blez\w*|bltz\w*|bc[12][ft]l?)$")


def lane_of(e, t, want=None):
    k, a, f = e.tok(t)
    if not f:
        raise AsmError("%s needs a lane field" % t)
    return LANE[f], f


def insn(e, ins):
    """One instruction -> one C++ statement (or "" for no-ops)."""
    parts = ins.split(None, 1)
    mn = parts[0]
    args = [x.strip() for x in split_top(parts[1], ",")] if len(parts) > 1 else []
    base_mn = re.sub(r"\.(i|ni)$", "", mn) if mn.startswith(("qmtc2", "qmfc2", "ctc2", "cfc2")) else mn

    def need(n):
        if len(args) != n:
            raise AsmError("%s: expected %d operands, got %d" % (mn, n, len(args)))

    if mn.startswith("."):
        if mn in (".set", ".p2align", ".align"):   # boot2: alignment has no effect on the host
            return ""
        # boot1: `.word 0x42000038` / `0x42000039` is how the decomp spells ei / di (no mnemonic in its assembler)
        if mn == ".word" and len(args) == 1 and args[0].lower() in ("0x42000038", "0x42000039"):
            mn = "ei" if args[0].lower() == "0x42000038" else "di"
        elif mn == ".word" and len(args) == 1 and re.fullmatch(r"0x[0-9a-fA-F]+", args[0]) and \
                (int(args[0], 16) & 0xFE00003F) == 0x4A000038:
            # vu0: `.word 0x4A00xxx8` is VCALLMS spelled as data (the decomp's GtIntersection soup functions, N219 [G]):
            # the immediate field (bits 20..6) is the pair index
            return "b3asm::vcallms(_S, %du);" % ((int(args[0], 16) >> 6) & 0x7FFF)
        else:
            raise AsmError("directive %s not supported" % mn)
    e.site_dst = e.resolver(vu_key(mn)) if e.resolver is not None and not e.scan and vu_key(mn) is not None else None
    if mn in ("ei", "di"):   # boot1: COP0 Status.EIE, through the backend (native only; fatal elsewhere)
        return "b3host::backend().set_interrupts_enabled(%s);" % ("true" if mn == "ei" else "false")
    if re.fullmatch(r"[\w$.]+:", mn):
        raise AsmError("label in asm (control flow not supported)")
    # boot2: `mfc0 $t, $12` (COP0 Status) in a DI loop: EIE (bit 16) as the backend keeps it, IE (bit 0) set
    if mn == "mfc0" and len(args) == 2 and args[1].strip() in ("$12", "$Status"):
        d = e.gpr_dst(args[0])
        return "" if d is None else ("b3asm::set64(%s, b3host::backend().interrupts_enabled() ? 0x10001ull : 0x1ull);" % d)
    # play1: `mfc0 $t, $9` (COP0 Count, CGtHiResSystemTimer::GetTime 0x375280): the backend's virtual EE clock, as the
    # fallback's mini and RW Audio's b3hw_cop0_count read it; sign-extended to 64 bits [C EE Core Instruction Set
    # manual MFC0: GPR[rt] <- sign_extend(CPR[0, rd])]. The native backend only (any other backend's cop0_count is fatal).
    if mn == "mfc0" and len(args) == 2 and args[1].strip() in ("$9", "$Count"):
        d = e.gpr_dst(args[0])
        return "" if d is None else ("b3asm::set64(%s, static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(b3host::backend().cop0_count()))));" % d)
    # vu0: VU0 micro mode: the microprogram runs on the State (b3asm::vcallms, a platform hook; include/b3host/eeasm.h);
    # the immediate is the program's byte address, the entry its pair index
    if mn == "vcallms":
        need(1)
        try:
            imm = int(args[0], 0)
        except ValueError:
            raise AsmError("vcallms: immediate %r not understood" % args[0])
        if imm % 8 or not 0 <= imm < 0x40000:
            raise AsmError("vcallms: %s is not a pair address" % args[0])
        return "b3asm::vcallms(_S, %du);" % (imm >> 3)
    if mn == "vcallmsr":
        need(1)
        if args[0].strip() != "$vi27":
            raise AsmError("vcallmsr: operand must be $vi27 (CMSAR0)")
        return "b3asm::vcallmsr(_S);"
    if mn in REFUSED:
        raise AsmError(REFUSED[mn])
    if BRANCHES.match(mn):
        raise AsmError("branch %s in asm (control flow not supported)" % mn)
    if mn in NOPS:
        return ""
    if mn == "pref":
        return ""

    m = VU_ARITH.match(mn)
    if m:
        op, acc, bcf, dest = m.groups()
        M = dest_mask(dest)
        if op in ("max", "mini") and (acc or bcf == "q"):
            raise AsmError("%s: no such form" % mn)
        need(3 if bcf not in ("q", "i") else 3)
        d_tok, s_tok, t_tok = args
        fs = e.vf(s_tok)
        if bcf in ("x", "y", "z", "w"):
            ft = "b3asm::bc<%d>(%s)" % (LANE[bcf], e.vf(t_tok, bcf))
        elif bcf == "q":
            if e.tok(t_tok)[0] != "q":
                raise AsmError("%s: third operand must be Q" % mn)
            ft = "b3asm::splat(_S.q)"
        elif bcf == "i":
            if e.tok(t_tok)[0] != "i":
                raise AsmError("%s: third operand must be I" % mn)
            ft = "b3asm::splat(_S.i)"
        else:
            ft = e.vf(t_tok)
        fn = {"add": "vadd", "sub": "vsub", "mul": "vmul", "max": "vmax", "mini": "vmini", "madd": "vmadd", "msub": "vmsub"}[op]
        if acc:
            if e.tok(d_tok)[0] != "acc":
                raise AsmError("%s: destination must be ACC" % mn)
            if op in ("madd", "msub"):
                return "_S.acc = b3asm::%s<%d>(_S.acc, _S.acc, %s, %s);" % (fn, M, fs, ft)
            return "_S.acc = b3asm::%s<%d>(_S.acc, %s, %s);" % (fn, M, fs, ft)
        d = e.vf_dst(d_tok)
        if d is None:
            return ""
        b = e.base(d_tok, d, fs, M)
        if op in ("madd", "msub"):
            return "%s = b3asm::%s<%d>(%s, _S.acc, %s, %s);%s" % (d, fn, M, b, fs, ft, e.mirror(d_tok, d))
        return "%s = b3asm::%s<%d>(%s, %s, %s);%s" % (d, fn, M, b, fs, ft, e.mirror(d_tok, d))

    m = re.fullmatch(r"vopm(ula|sub)\.([xyzw]+)", mn)
    if m:
        if m.group(2) != "xyz":
            raise AsmError("%s: only .xyz exists" % mn)
        need(3)
        fs, ft = e.vf(args[1]), e.vf(args[2])
        if m.group(1) == "ula":
            if e.tok(args[0])[0] != "acc":
                raise AsmError("vopmula: destination must be ACC")
            return "_S.acc = b3asm::vopmula(_S.acc, %s, %s);" % (fs, ft)
        d = e.vf_dst(args[0])
        if d is None:
            return ""
        return "%s = b3asm::vopmsub(%s, _S.acc, %s, %s);%s" % (d, e.base(args[0], d, fs, 7), fs, ft, e.mirror(args[0], d))

    m = re.fullmatch(r"v(abs|move|mr32|ftoi0|ftoi4|ftoi12|ftoi15|itof0|itof4|itof12|itof15)\.([xyzw]+)", mn)
    if m:
        need(2)
        M = dest_mask(m.group(2))
        fs = e.vf(args[1])
        d = e.vf_dst(args[0])
        if d is None:
            return ""
        b = e.base(args[0], d, fs, M)
        k = m.group(1)
        if k.startswith(("ftoi", "itof")):
            return "%s = b3asm::v%s<%d, %s>(%s, %s);%s" % (d, k[:4], M, k[4:], b, fs, e.mirror(args[0], d))
        return "%s = b3asm::v%s<%d>(%s, %s);%s" % (d, k, M, b, fs, e.mirror(args[0], d))

    if mn in ("vdiv", "vrsqrt"):
        need(3)
        if e.tok(args[0])[0] != "q":
            raise AsmError("%s: destination must be Q" % mn)
        ls, fs_ = lane_of(e, args[1])
        lt, ft_ = lane_of(e, args[2])
        return "_S.q = b3asm::%s(%s.w[%d], %s.w[%d]);" % (mn, e.vf(args[1], fs_), ls, e.vf(args[2], ft_), lt)
    if mn == "vsqrt":
        need(2)
        if e.tok(args[0])[0] != "q":
            raise AsmError("vsqrt: destination must be Q")
        lt, ft_ = lane_of(e, args[1])
        return "_S.q = b3asm::vsqrt(%s.w[%d]);" % (e.vf(args[1], ft_), lt)
    if mn in ("vrinit", "vrxor"):
        need(2)
        if e.tok(args[0])[0] != "r":
            raise AsmError("%s: destination must be R" % mn)
        ls, fs_ = lane_of(e, args[1])
        src = "%s.w[%d]" % (e.vf(args[1], fs_), ls)
        if mn == "vrxor":
            src = "(%s ^ _S.r)" % src
        return "_S.r = b3asm::r_init(%s);" % src
    m = re.fullmatch(r"vr(next|get)\.([xyzw]+)", mn)
    if m:
        need(2)
        if e.tok(args[1])[0] != "r":
            raise AsmError("%s: source must be R" % mn)
        M = dest_mask(m.group(2))
        d = e.vf_dst(args[0])
        pre = "_S.r = b3asm::r_next(_S.r); " if m.group(1) == "next" else ""
        if d is None:
            return pre
        return "%s%s = b3asm::fill<%d>(%s, _S.r);" % (pre, d, M, e.base(args[0], d, None, M))

    if base_mn == "qmtc2":
        need(2)
        g = e.gpr(args[0])
        d = e.vf_dst(args[1])
        if d is None:
            return ""
        e.base(args[1], d, None, 15)
        return "%s = %s;" % (d, g)
    if base_mn == "qmfc2":
        need(2)
        s = e.vf(args[1])
        d = e.gpr_dst(args[0])
        return "" if d is None else "%s = %s;" % (d, s)
    if base_mn == "ctc2":
        need(2)
        k, a, _ = e.tok(args[1])
        if k != "vi":
            raise AsmError("ctc2: destination must be $viN")
        g = "%s.w[0]" % e.gpr(args[0])
        if 1 <= a <= 15:
            return "b3asm::ctc2_vi(_S, %d, %s);" % (a, g)
        if a == 0:
            return ""
        if a == 20:
            return "_S.r = b3asm::r_init(%s);" % g
        if a == 21:
            return "_S.i = %s;" % g
        if a == 22:
            return "_S.q = %s;" % g
        if a == 27:   # vu0: CMSAR0
            return "_S.cmsar0 = static_cast<std::uint16_t>(%s);" % g
        raise AsmError("ctc2 to $vi%d (VU control register) not modelled" % a)
    if base_mn == "cfc2":
        need(2)
        k, a, _ = e.tok(args[1])
        if k != "vi":
            raise AsmError("cfc2: source must be $viN")
        if a <= 15:
            src = "b3asm::cfc2_vi(_S, %d)" % a
        elif a == 20:
            src = "(_S.r & 0x007FFFFFu)"
        elif a == 21:
            src = "_S.i"
        elif a == 22:
            src = "_S.q"
        elif a == 27:   # vu0: CMSAR0
            src = "static_cast<std::uint32_t>(_S.cmsar0)"
        elif a in (16, 17, 18):
            raise VuFlagRead("cfc2 from $vi%d (VU0 %s): the generated VU0 programs keep only the flags their own code reads "
                             "(tools/native/vu0_aot.py); keep every push there before allowing this" % (a, {16: "STATUS", 17: "MAC", 18: "CLIP"}[a]))
        else:
            raise AsmError("cfc2 from $vi%d (VU control register) not modelled" % a)
        d = e.gpr_dst(args[0])
        return "" if d is None else "b3asm::set32(%s, %s);" % (d, src)
    if mn in ("lqc2", "sqc2"):
        need(2)
        p = e.addr(args[1], 16)
        if mn == "lqc2":
            d = e.vf_dst(args[0])
            if d is None:
                return ""
            e.base(args[0], d, None, 15)
            return "%s = b3asm::ldq(%s);" % (d, p)
        return "b3asm::stq(%s, %s);" % (p, e.vf(args[0]))

    # ---- MMI
    if mn in MMI3:
        need(3)
        s, t = e.gpr(args[1]), e.gpr(args[2])
        d = e.gpr_dst(args[0])
        return "" if d is None else "%s = b3asm::%s(%s, %s);" % (d, mn, s, t)
    if mn in ("pabsw", "pcpyh"):
        need(2)
        t = e.gpr(args[1])
        d = e.gpr_dst(args[0])
        return "" if d is None else "%s = b3asm::%s(%s);" % (d, mn, t)
    if mn in ("psraw", "psrlw", "psllw"):
        need(3)
        t = e.gpr(args[1])
        d = e.gpr_dst(args[0])
        return "" if d is None else "%s = b3asm::%s(%s, %s);" % (d, mn, t, e.imm(args[2]))
    if mn == "pmulth":
        need(3)
        s, t = e.gpr(args[1]), e.gpr(args[2])
        d = e.gpr_dst(args[0])
        r = "b3asm::pmulth(_S, %s, %s)" % (s, t)
        return "%s;" % r if d is None else "%s = %s;" % (d, r)
    if mn == "qfsrv":
        need(3)
        s, t = e.gpr(args[1]), e.gpr(args[2])
        d = e.gpr_dst(args[0])
        return "" if d is None else "%s = b3asm::qfsrv(%s, %s, _S.sa);" % (d, s, t)
    if mn in ("mtsab", "mtsah"):
        need(2)
        g = "%s.w[0]" % e.gpr(args[0])
        i = e.imm(args[1])
        if mn == "mtsab":
            return "_S.sa = static_cast<std::uint8_t>((%s & 15u) ^ ((%s) & 15u));" % (g, i)
        return "_S.sa = static_cast<std::uint8_t>(((%s & 7u) ^ ((%s) & 7u)) << 1);" % (g, i)

    # ---- COP1
    if mn == "mtc1":
        need(2)
        g = e.gpr(args[0])
        return "%s = %s.w[0];" % (e.fpr_dst(args[1]), g)
    if mn == "mfc1":
        need(2)
        f = e.fpr(args[1])
        d = e.gpr_dst(args[0])
        return "" if d is None else "b3asm::set32(%s, %s);" % (d, f)
    if mn in COP1_3:
        need(3)
        a, b = e.fpr(args[1]), e.fpr(args[2])
        return "%s = b3asm::%s(%s, %s);" % (e.fpr_dst(args[0]), COP1_3[mn], a, b)
    if mn in ("abs.s", "neg.s", "mov.s", "sqrt.s", "cvt.s.w", "cvt.w.s"):
        need(2)
        a = e.fpr(args[1])
        expr = {"abs.s": "(%s & 0x7FFFFFFFu)", "neg.s": "(%s ^ 0x80000000u)", "mov.s": "%s",
                "sqrt.s": "b3asm::sqrt_s(%s)", "cvt.s.w": "b3asm::cvt_s_w(%s)", "cvt.w.s": "b3asm::cvt_w_s(%s)"}[mn] % a
        return "%s = %s;" % (e.fpr_dst(args[0]), expr)
    if mn in ("lwc1", "swc1"):
        need(2)
        p = e.addr(args[1], 4)
        if mn == "lwc1":
            return "%s = b3asm::ld<std::uint32_t>(%s);" % (e.fpr_dst(args[0]), p)
        return "b3asm::st<std::uint32_t>(%s, %s);" % (p, e.fpr(args[0]))

    # ---- EE core
    if mn in LOADS:
        need(2)
        ty, fmt, n = LOADS[mn]
        p = e.addr(args[1], n)
        d = e.gpr_dst(args[0])
        return "" if d is None else "b3asm::" + (fmt % (d, "b3asm::ld<%s>(%s)" % (ty, p))) + ";"
    if mn in STORES:
        need(2)
        ty, n = STORES[mn]
        p = e.addr(args[1], n)
        return "b3asm::st<%s>(%s, static_cast<%s>(b3asm::lo64(%s)));" % (ty, p, ty, e.gpr(args[0]))
    if mn == "lq":
        need(2)
        p = e.addr(args[1], 16)
        d = e.gpr_dst(args[0])
        return "" if d is None else "%s = b3asm::ldq(%s);" % (d, p)
    if mn == "sq":
        need(2)
        p = e.addr(args[1], 16)
        return "b3asm::stq(%s, %s);" % (p, e.gpr(args[0]))
    if mn in ("addiu", "addi", "daddiu", "daddi", "slti", "sltiu", "andi", "ori", "xori"):
        need(3)
        s = e.gpr(args[1])
        i = e.imm(args[2])
        d = e.gpr_dst(args[0])
        if d is None:
            return ""
        expr = {"addiu": "b3asm::set32(%(d)s, %(s)s.w[0] + static_cast<std::uint32_t>(%(i)s))",
                "addi": "b3asm::set32(%(d)s, %(s)s.w[0] + static_cast<std::uint32_t>(%(i)s))",
                "daddiu": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) + static_cast<std::uint64_t>(static_cast<std::int64_t>(%(i)s)))",
                "daddi": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) + static_cast<std::uint64_t>(static_cast<std::int64_t>(%(i)s)))",
                "slti": "b3asm::set64(%(d)s, static_cast<std::int64_t>(b3asm::lo64(%(s)s)) < static_cast<std::int64_t>(%(i)s) ? 1u : 0u)",
                "sltiu": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) < static_cast<std::uint64_t>(static_cast<std::int64_t>(%(i)s)) ? 1u : 0u)",
                "andi": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) & (static_cast<std::uint64_t>(%(i)s) & 0xFFFFu))",
                "ori": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) | (static_cast<std::uint64_t>(%(i)s) & 0xFFFFu))",
                "xori": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) ^ (static_cast<std::uint64_t>(%(i)s) & 0xFFFFu))"}[mn]
        return expr % {"d": d, "s": s, "i": i} + ";"
    if mn in ("addu", "add", "subu", "sub", "daddu", "dadd", "dsubu", "or", "and", "xor", "nor", "slt", "sltu"):
        need(3)
        s, t = e.gpr(args[1]), e.gpr(args[2])
        d = e.gpr_dst(args[0])
        if d is None:
            return ""
        expr = {"addu": "b3asm::set32(%(d)s, %(s)s.w[0] + %(t)s.w[0])", "add": "b3asm::set32(%(d)s, %(s)s.w[0] + %(t)s.w[0])",
                "subu": "b3asm::set32(%(d)s, %(s)s.w[0] - %(t)s.w[0])", "sub": "b3asm::set32(%(d)s, %(s)s.w[0] - %(t)s.w[0])",
                "daddu": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) + b3asm::lo64(%(t)s))",
                "dadd": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) + b3asm::lo64(%(t)s))",
                "dsubu": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) - b3asm::lo64(%(t)s))",
                "or": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) | b3asm::lo64(%(t)s))",
                "and": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) & b3asm::lo64(%(t)s))",
                "xor": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) ^ b3asm::lo64(%(t)s))",
                "nor": "b3asm::set64(%(d)s, ~(b3asm::lo64(%(s)s) | b3asm::lo64(%(t)s)))",
                "slt": "b3asm::set64(%(d)s, static_cast<std::int64_t>(b3asm::lo64(%(s)s)) < static_cast<std::int64_t>(b3asm::lo64(%(t)s)) ? 1u : 0u)",
                "sltu": "b3asm::set64(%(d)s, b3asm::lo64(%(s)s) < b3asm::lo64(%(t)s) ? 1u : 0u)"}[mn]
        return expr % {"d": d, "s": s, "t": t} + ";"
    if mn in ("move",):
        need(2)
        s = e.gpr(args[1])
        d = e.gpr_dst(args[0])
        return "" if d is None else "b3asm::set64(%s, b3asm::lo64(%s));" % (d, s)
    if mn in ("li",):
        need(2)
        i = e.imm(args[1])
        d = e.gpr_dst(args[0])
        return "" if d is None else "b3asm::set64(%s, static_cast<std::uint64_t>(static_cast<std::int64_t>(%s)));" % (d, i)
    if mn == "lui":
        need(2)
        i = e.imm(args[1])
        d = e.gpr_dst(args[0])
        return "" if d is None else "b3asm::set32(%s, static_cast<std::uint32_t>(%s) << 16);" % (d, i)
    if mn in ("sll", "srl", "sra", "dsll", "dsrl", "dsra", "dsll32", "dsrl32", "dsra32"):
        need(3)
        t = e.gpr(args[1])
        sa = e.imm(args[2])
        d = e.gpr_dst(args[0])
        if d is None:
            return ""
        expr = {"sll": "b3asm::set32(%(d)s, %(t)s.w[0] << ((%(a)s) & 31))",
                "srl": "b3asm::set32(%(d)s, %(t)s.w[0] >> ((%(a)s) & 31))",
                "sra": "b3asm::set32(%(d)s, static_cast<std::uint32_t>(static_cast<std::int32_t>(%(t)s.w[0]) >> ((%(a)s) & 31)))",
                "dsll": "b3asm::set64(%(d)s, b3asm::lo64(%(t)s) << ((%(a)s) & 31))",
                "dsrl": "b3asm::set64(%(d)s, b3asm::lo64(%(t)s) >> ((%(a)s) & 31))",
                "dsra": "b3asm::set64(%(d)s, static_cast<std::uint64_t>(static_cast<std::int64_t>(b3asm::lo64(%(t)s)) >> ((%(a)s) & 31)))",
                "dsll32": "b3asm::set64(%(d)s, b3asm::lo64(%(t)s) << (((%(a)s) & 31) + 32))",
                "dsrl32": "b3asm::set64(%(d)s, b3asm::lo64(%(t)s) >> (((%(a)s) & 31) + 32))",
                "dsra32": "b3asm::set64(%(d)s, static_cast<std::uint64_t>(static_cast<std::int64_t>(b3asm::lo64(%(t)s)) >> (((%(a)s) & 31) + 32)))"}[mn]
        return expr % {"d": d, "t": t, "a": sa} + ";"
    raise AsmError("instruction %s not supported" % mn)


def mnemonic(ins):
    return ins.split(None, 1)[0]


# ---------------------------------------------------------------- control flow (boot2)
# Labels and branches inside one asm statement (the decomp's hand-inlined bodies, e.g. CB3MenuChoices::Prepare
# 0x30D3B0: two `1:` / `2:` loops and its own `jr $ra`): labels become C++ labels, a branch a goto. Under `.set
# noreorder` the instruction after a branch is its delay slot: it runs after the condition is read and before the jump
# (a likely branch runs it only when taken); otherwise the assembler owns the slot and the next instruction is simply
# the next one. `jr $ra` ends the statement (the body is the whole function). Numeric labels resolve as GAS does
# (`1b` the nearest earlier `1:`, `1f` the next later one).
COND_BRANCH = {"beq": 2, "bne": 2, "beql": 2, "bnel": 2, "beqz": 1, "bnez": 1, "beqzl": 1, "bnezl": 1,
               "bgez": 1, "bgtz": 1, "blez": 1, "bltz": 1, "bgezl": 1, "bgtzl": 1, "blezl": 1, "bltzl": 1}


def is_label(ins):
    return re.fullmatch(r"[\w$.]+:", ins.strip()) is not None


def control_flow(insns):
    """None when the statement has no labels or branches; else {'noreorder': bool}."""
    if not any(is_label(i) or BRANCHES.match(mnemonic(i)) for i in insns):
        return None
    noreorder = any(re.fullmatch(r"\.set\s+noreorder", i.strip()) for i in insns)
    for i in insns:
        mn = mnemonic(i)
        if BRANCHES.match(mn) and mn not in COND_BRANCH and mn not in ("b", "jr"):
            raise AsmError("branch %s in asm not supported" % mn)
        if mn == "jr" and i.split(None, 1)[1].strip() not in ("$ra", "$31"):
            raise AsmError("jr to a register other than $ra in asm not supported")
    return {"noreorder": noreorder}


def branch_cond(e, ins):
    """The C++ condition of a branch (reads its operands); 'true' for b / jr."""
    mn = mnemonic(ins)
    args = [x.strip() for x in split_top(ins.split(None, 1)[1], ",")] if len(ins.split(None, 1)) > 1 else []
    if mn in ("b", "jr"):
        return "true"
    base = mn[:-1] if mn.endswith("l") and mn not in ("bnel", "beql") else mn
    base = {"bnel": "bne", "beql": "beq"}.get(mn, base)
    n = COND_BRANCH[mn]
    r = ["static_cast<std::int64_t>(b3asm::lo64(%s))" % e.gpr(a) for a in args[:n]]
    return {"beq": "%s == %s", "bne": "%s != %s", "beqz": "%s == 0", "bnez": "%s != 0", "bgez": "%s >= 0",
            "bgtz": "%s > 0", "blez": "%s <= 0", "bltz": "%s < 0"}[base] % tuple(r)


_FLOW_ID = [0]


def emit_flow(e, insns, flow):
    _FLOW_ID[0] += 1
    u = _FLOW_ID[0]   # labels unique per statement (a function may hold several)
    labels = [(k, i.strip()[:-1]) for k, i in enumerate(insns) if is_label(i)]

    def target(ref, at):
        m = re.fullmatch(r"(\d+)([bf])", ref)
        if m:
            cand = [k for k, nm in labels if nm == m.group(1) and (k < at if m.group(2) == "b" else k > at)]
            if not cand:
                raise AsmError("label %s not found" % ref)
            k = cand[-1] if m.group(2) == "b" else cand[0]
        else:
            cand = [k for k, nm in labels if nm == ref]
            if len(cand) != 1:
                raise AsmError("label %s not found" % ref)
            k = cand[0]
        return "_L%d_%d" % (u, k)

    body, k, n = [], 0, len(insns)
    while k < n:
        ins = insns[k]
        c = ins.replace("*/", "* /")
        if is_label(ins):
            body.append("_L%d_%d:;" % (u, k))
            k += 1
            continue
        mn = mnemonic(ins)
        if BRANCHES.match(mn):
            cond = branch_cond(e, ins)
            likely = mn.endswith("l") and mn in COND_BRANCH
            dest = ("_Lend_%d" % u) if mn == "jr" else target(ins.split(None, 1)[1].split(",")[-1].strip(), k)
            slot = ""
            if flow["noreorder"] and k + 1 < n and not is_label(insns[k + 1]):
                slot = insn(e, insns[k + 1])
                c += " ; slot: " + insns[k + 1].replace("*/", "* /")
                k += 1
            if likely:
                body.append("/* %s */ if (%s) { %s goto %s; }" % (c, cond, slot, dest))
            else:
                body.append("/* %s */ { const bool _c = %s; %s if (_c) goto %s; }" % (c, cond, slot, dest))
            k += 1
            continue
        s = insn(e, ins)
        body.append("/* %s */ %s" % (c, s) if s else "/* %s */" % c)
        k += 1
    body.append("_Lend_%d:;" % u)
    return body


def cpcond0_wait(insns, ops):
    """True / False: the statement is the idiom `[sync|sync.l|sync.p]* L: nop* bc0f|bc0t L` (the value bc0f / bc0t waits
    for: bc0f -> True); None otherwise"""
    if ops:
        return None
    seq = [i.strip() for i in insns if i.strip()]
    k = 0
    while k < len(seq) and mnemonic(seq[k]) in ("sync", "sync.l", "sync.p"):
        k += 1
    if k >= len(seq) or not is_label(seq[k]):
        return None
    label = seq[k].rstrip(":").strip()
    k += 1
    while k < len(seq) and mnemonic(seq[k]) == "nop":
        k += 1
    if k != len(seq) - 1:
        return None
    m = re.fullmatch(r"(bc0[ft])\s+(\S+)", seq[k])
    if not m or m.group(2) != label:
        return None
    return m.group(1) == "bc0f"


def translate(stmt, where="", resolver=None):
    """One asm statement (`asm ... )`, unmasked text) -> a C++ statement `do { ... } while (0)` (the source's `;`
    follows). Raises AsmError."""
    tmpl, ops, clob = parse_stmt(stmt)
    insns = split_insns(tmpl)
    # vu0 (mac1's request): the DMA-wait idiom `[sync] [sync.p] L: nop.. ; bc0f L` (or bc0t) with no operands -> a wait on
    # the DMAC's CPCOND0 (b3asm::wait_cpcond0, the platform's HwModel::cpcond0); bc0f loops while the condition is false
    w = cpcond0_wait(insns, ops)
    if w is not None:
        return "do { /* decomp2host P6: asm translated by eeasm.py (bc0%s wait) */ b3asm::wait_cpcond0(%s); } while (0)" % (
            "f" if w else "t", "true" if w else "false")
    # vu0: a microprogram reads and writes only the State's registers; a compiler-chosen "j" operand is a C++ local here
    # (on the PS2 it is some VF register the program may read or clobber), so such a statement is refused
    def _is_vcallms(i):
        m = re.fullmatch(r"\s*\.word\s+(0x[0-9a-fA-F]+)\s*", i)
        return mnemonic(i) in ("vcallms", "vcallmsr") or bool(m and (int(m.group(1), 16) & 0xFE00003F) == 0x4A000038)
    if any(_is_vcallms(i) for i in insns if not is_label(i)) and any(o.kind == "j" for o in ops):
        raise AsmError('vcallms with a compiler-chosen "j" operand: the microprogram sees explicit $vfN registers only')
    for c in clob:
        if c not in ("memory", "cc") and not re.fullmatch(r"\$?(vf\d+|\d+|[a-z]\w*|f\d+|vi\d+|ACC|Q|I)", c):
            raise AsmError("clobber %r not understood" % c)
    info = {}
    flow = control_flow(insns)
    # pass 1: operand use
    e = Emitter(ops, True, info)
    for ins in insns:
        if is_label(ins):
            continue
        if BRANCHES.match(mnemonic(ins)) and flow is not None:
            branch_cond(e, ins)
            continue
        insn(e, ins)
    # pointer-typed "r" inputs used only as memory bases keep their host pointer (they may point at host locals)
    for o in ops:
        u = info.get(o.idx, set())
        if o.kind == "r" and not o.out and u == {"base"}:   # never written, only a memory base
            info[("hostbase", o.idx)] = True
    # pass 2: emit
    e = Emitter(ops, False, info, resolver)
    body = []
    if flow is None:
        for ins in insns:
            s = insn(e, ins)
            body.append("/* %s */ %s" % (ins.replace("*/", "* /"), s) if s else "/* %s */" % ins.replace("*/", "* /"))
    else:
        body = emit_flow(e, insns, flow)
    pre, post = ["auto& _S = b3asm::state(); (void)_S;"], []
    for o in ops:
        u = info.get(o.idx, set())
        if o.kind == "tied":
            continue
        tied_in = [x for x in ops if x.kind == "tied" and x.tied == o.idx]
        if o.kind == "i":
            continue
        if o.kind == "m":
            pre.append("unsigned char* _o%d = reinterpret_cast<unsigned char*>(&(%s));" % (o.idx, o.expr))
            continue
        if info.get(("hostbase", o.idx)):
            pre.append("unsigned char* _b%d = b3asm::host_base(%s);" % (o.idx, o.expr))
            continue
        if o.kind in ("j", "r"):
            fin = "vf_in" if o.kind == "j" else "gpr_in"
            fout = "vf_out" if o.kind == "j" else "gpr_out"
            # boot2: `(u_long128)<integer literal>` (MWCC: the literal in the low 64 bits, B3SparkRendererPS2.cpp
            # Construct): the host's u_long128 is no scalar, so the literal goes in as a 64-bit value
            ml = re.fullmatch(r"\(*\s*\(\s*u_long128\s*\)\s*(0x[0-9A-Fa-f]+|\d+)[uUlL]*\s*\)*", o.expr.strip())
            if ml and o.kind == "r" and o.expr.count("(") == o.expr.count(")"):
                o.expr = "static_cast<std::uint64_t>(%sull)" % ml.group(1)
            if o.rw == "+" or (not o.out and u):
                pre.append("b3asm::Q _o%d = b3asm::%s(%s);" % (o.idx, fin, o.expr))
            elif o.out and tied_in:
                pre.append("b3asm::Q _o%d = b3asm::%s(%s);" % (o.idx, fin, tied_in[0].expr))
            elif o.out:
                pre.append("b3asm::Q _o%d = {};" % o.idx)
            if o.out:
                post.append("b3asm::%s(%s, _o%d);" % (fout, o.expr, o.idx))
        elif o.kind == "f":
            if o.rw == "+" or (not o.out and u):
                pre.append("std::uint32_t _o%d = b3asm::fpr_in(%s);" % (o.idx, o.expr))
            elif o.out and tied_in:
                pre.append("std::uint32_t _o%d = b3asm::fpr_in(%s);" % (o.idx, tied_in[0].expr))
            elif o.out:
                pre.append("std::uint32_t _o%d = 0;" % o.idx)
            if o.out:
                post.append("b3asm::fpr_out(%s, _o%d);" % (o.expr, o.idx))
    return "do { /* decomp2host P6: asm translated by eeasm.py */ %s } while (0)" % " ".join(pre + body + post)


def translate_text(text, masked, where, on_error, resolver_at=None):
    """Translate every asm statement found in text[lo:hi] ranges handled by the caller; returns edits
    [(start, end, replacement)]. on_error(start, msg) decides (raise or substitute). resolver_at(pos) (native3): the
    baked VU binding's resolver for the function holding pos, or None."""
    edits = []
    for s, e in find_asm(masked):
        stmt = text[s:e]
        try:
            edits.append((s, e, translate(stmt, where, resolver_at(s) if resolver_at is not None else None)))
        except AsmError as x:
            r = on_error(s, str(x), stmt)
            if r is not None:
                edits.append((s, e, r))
    return edits


# ---------------------------------------------------------------- census
def census(root, list_unsupported=False):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from decomp2host import mask
    files = sorted(set(f for pat in ("src/**/*.c", "src/**/*.cpp", "src/**/*.h", "include/**/*.h")
                       for f in glob.glob(os.path.join(root, pat), recursive=True)))
    use = collections.Counter()
    use_files = collections.defaultdict(set)
    bad_mn = collections.Counter()
    reasons = collections.Counter()
    examples = {}
    nstmt = ok = 0
    by_area = collections.Counter()
    ok_area = collections.Counter()
    for f in files:
        rel = os.path.relpath(f, root)
        area = "include" if rel.startswith("include/") else ("src/nonmatching" if rel.startswith("src/nonmatching") else "src/b3")
        t = open(f, errors="replace").read()
        m = mask(t)
        for s, e in find_asm(m):
            if re.match(r"\s*#\s*define", t[t.rfind("\n", 0, s) + 1:s]) or t[t.rfind("\n", 0, s) + 1:s].rstrip().endswith("\\"):
                continue   # inside a macro definition
            stmt = t[s:e]
            nstmt += 1
            by_area[area] += 1
            try:
                tmpl, ops, clob = parse_stmt(stmt)
                insns = split_insns(tmpl)
            except AsmError as x:
                reasons[str(x)[:70]] += 1
                examples.setdefault(str(x)[:70], "%s:%d" % (rel, t.count("\n", 0, s) + 1))
                continue
            for ins in insns:
                use[mnemonic(ins)] += 1
                use_files[mnemonic(ins)].add(rel)
            try:
                translate(stmt, rel)
                ok += 1
                ok_area[area] += 1
            except AsmError as x:
                msg = re.sub(r"%\d+|\$vf\d+|0x[0-9A-Fa-f]+|-?\d+", "N", str(x))[:90]
                reasons[msg] += 1
                examples.setdefault(msg, "%s:%d" % (rel, t.count("\n", 0, s) + 1))
                # which mnemonics are unsupported (translate each instruction alone)
                for ins in insns:
                    try:
                        insn(Emitter(ops, True, {}), ins)
                    except AsmError as y:
                        if "not supported" in str(y) or "not modelled" in str(y) or "microprogram" in str(y) or "privileged" in str(y) or "control flow" in str(y) or "COP0" in str(y):
                            bad_mn[mnemonic(ins)] += 1
    print("asm statements: %d, translated: %d (%.1f%%)" % (nstmt, ok, 100.0 * ok / max(1, nstmt)))
    for a in sorted(by_area):
        print("  %-16s %5d statements, %5d translated" % (a, by_area[a], ok_area[a]))
    print("distinct mnemonics used: %d; supported: %d; not supported: %d" % (
        len(use), len([k for k in use if k not in bad_mn]), len(bad_mn)))
    print("\n  uses files  supported  mnemonic")
    for k, v in use.most_common():
        print("%6d %5d  %-9s  %s" % (v, len(use_files[k]), "no" if k in bad_mn else "yes", k))
    print("\nstatements not translated, by reason:")
    for k, v in reasons.most_common():
        print("%6d  %s   (e.g. %s)" % (v, k, examples[k]))


# ---------------------------------------------------------------- self-test
def selftest():
    cases = [
        'asm("vsub.xyz %0, %1, %2" : "=j"(t) : "j"(a), "j"(b))',
        'asm("vaddx.y %0, $vf0, %1x" : "+j"(t) : "j"(z))',
        'asm("qmtc2.ni %1, %0" : "=j"(z) : "r"(0.0f))',
        'asm volatile("vopmula.xyz $ACC, %2, %1" "\\n\\t" "vopmsub.xyz %1, %1, %2" "\\n\\t" "qmfc2.i %0, %1" : "=r"(t), "+j"(b) : "j"(a.mData))',
        'asm("vrsqrt $Q, $vf0w, $vf1x" "\\n\\t" "vwaitq" "\\n\\t" "vmulq.xyz %1, %1, $Q" "\\n\\t" "qmfc2.i %0, %1" : "=r"(t), "+j"(v.mData))',
        'asm("lqc2 $vf1, 0x10(%0)" "\\n\\t" "sqc2 $vf1, 0(%1)" : : "r"(src), "r"(dst) : "memory")',
        'asm("pextlw %0,$0,%1" : "=r"(r) : "r"(x))',
        'asm("mtc1 %1, %0" : "=f"(f) : "r"(x))',
        'asm(".word 0x4A001C78")',
        'asm volatile("lqc2 $vf1, 0(%1)" "\\n\\t" "vcallms 0x3D8" "\\n\\t" "cfc2.i %0, $vi1" : "=r"(n) : "r"(p) : "memory")',
    ]
    for c in cases:
        print(translate(c))
    # play1: COP0 Count (GetTime 0x375280) through the backend's virtual clock, sign-extended; other COP0 registers refused
    t = translate('asm volatile ("mfc0 %0,$9" : "=r"(l))')
    if "cop0_count()" not in t or "static_cast<std::int32_t>" not in t:
        print("mfc0 Count: " + t)
        sys.exit(1)
    print(t)
    bad = ['asm("vcallms 0xA18" : : "j"(v))', 'asm(".word 0x4A001C78" : : "j"(v))', 'asm("vcallms 0xA1C")', 'asm("bgtz %0, 1b" : : "r"(n))', 'asm(".word 0x12345678")', 'asm("mfc0 %0, $10" : "=r"(x))',
           'asm("vfoo.xyz %0, %1, %2" : "=j"(t) : "j"(a), "j"(b))', 'asm(text : "=j"(r) : "j"(a))']
    for c in bad:
        try:
            translate(c)
            print("NOT REFUSED: " + c)
            sys.exit(1)
        except AsmError as x:
            print("refused: %s -> %s" % (c, x))
    for r in (16, 17, 18):   # vu0flags: a hard stop, not a refusal (AsmError: the unit dropped)
        c = 'asm volatile("cfc2.i %%0, $vi%d" : "=r"(x))' % r
        try:
            translate(c)
            print("NOT STOPPED: " + c)
            sys.exit(1)
        except AsmError:
            print("REFUSED ONLY (should stop the generation): " + c)
            sys.exit(1)
        except VuFlagRead as x:
            print("stopped: %s -> %s" % (c, x))


def emit_cases(src, dst):
    """tests/eeasm_cases.txt -> a header of TEST() blocks with every asm statement translated (tests/test_eeasm.cpp)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from decomp2host import mask
    out = ["// GENERATED by tools/decomp2host/eeasm.py --emit-cases from tests/eeasm_cases.txt. Do not edit."]
    cases, name, body = [], None, []
    for line in open(src):
        if line.startswith("#") and name is None:
            continue
        mm = re.match(r"@case\s+(\w+)", line)
        if mm:
            if name:
                cases.append((name, body))
            name, body = mm.group(1), []
            continue
        if name is not None and not line.startswith("#"):
            body.append(line)
    if name:
        cases.append((name, body))
    for name, body in cases:
        text = "".join(body)
        def fail(pos, msg, stmt):
            raise AsmError("case %s: %s" % (name, msg))
        edits = translate_text(text, mask(text), name, fail)
        res, pos = [], 0
        for s0, e0, r in sorted(edits):
            res.append(text[pos:s0])
            res.append(r)
            pos = e0
        res.append(text[pos:])
        out.append("TEST(asm_%s) {\n%s}" % (name, "".join(res)))
    open(dst, "w").write("\n".join(out) + "\n")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--emit-cases":
        emit_cases(sys.argv[2], sys.argv[3])
        sys.exit(0)
    if len(sys.argv) >= 3 and sys.argv[1] == "--census":
        census(sys.argv[2], "--list-unsupported" in sys.argv)
    elif len(sys.argv) >= 2 and sys.argv[1] == "--selftest":
        selftest()
    else:
        sys.stderr.write(__doc__)
        sys.exit(2)


# ---------------------------------------------------------------- whole asm functions (vu0, gen1's request)
# A CodeWarrior `asm <ret> F(<params>) { ... }` body is retail's own instruction stream. translate_asm_function turns it
# into a C++ body over a local 128-bit register file R[32] (the EE GPRs; 64-bit ops write the low 64 bits, the R5900
# way), the VU0 macro state b3asm::state() (VF / ACC / Q / I / VI, HI / LO / HI1 / LO1, SA: shared with every translated
# statement) and guest memory (b3asm::mem: RAM and the scratchpad). Each non-branch instruction goes through insn(),
# the inline-asm translator, its explicit-register form, with the function's operand spellings normalised first
# (immediate expressions evaluated, `sym` / `(expr)($r)` memory operands resolved); the mult / div / mfhi / mflo family
# is modelled here. Parameters enter a0.., the guest sp is the backend's, labels are C++ labels, a branch's condition
# is taken before its delay slot runs (`.set noreorder`), a likely branch runs its slot only when taken, `jal f` is a
# guest call (b3host::backend().call with a0-t3 and the current sp; v0 / v1 back), `jr $ra` returns. Anything
# unsupported is refused (AsmError).
_FN_BRANCH2 = {"beq": "==", "bne": "!=", "beql": "==", "bnel": "!="}
_FN_BRANCH1 = {"beqz": "== 0", "bnez": "!= 0", "bgtz": "> 0", "blez": "<= 0", "bltz": "< 0", "bgez": ">= 0"}
_FN_BRANCHES = set(_FN_BRANCH2) | set(_FN_BRANCH1) | {k + "l" for k in _FN_BRANCH1} | {"b", "j", "jal", "jr"}


def _fn_imm(tok):
    t = tok.strip()
    if not re.fullmatch(r"[\s0-9a-fA-FxX()<>&|+\-~*]+", t):
        raise AsmError("immediate %r not understood" % tok)
    return int(eval(t, {"__builtins__": {}}, {}))


def _fn_reg(tok):
    t = tok.strip()
    if t.startswith("$") and t[1:] in GPR_NAMES:
        return GPR_NAMES[t[1:]]
    m = re.fullmatch(r"\$(\d+)", t)
    if m and int(m.group(1)) < 32:
        return int(m.group(1))
    raise AsmError("GPR %r not understood" % tok)


def _fn_operand(op, symbol_address):
    """One operand in a form Emitter.tok accepts: registers as they are; `(expr)` immediates evaluated; `off($r)`
    with an expression offset evaluated; `sym` / `sym+off` (a guest global) -> D_XXXXXXXX (absmem)."""
    t = op.strip()
    if re.fullmatch(r"\$\w+", t):
        return t
    m = re.fullmatch(r"(.*)\((\$\w+)\)", t)
    if m:
        off = _fn_imm(m.group(1)) if m.group(1).strip() else 0
        return "%d(%s)" % (off, m.group(2))
    m = re.fullmatch(r"([A-Za-z_]\w*)\s*(?:\+\s*(.+))?", t)
    if m:
        return "D_%08X" % ((symbol_address(m.group(1)) + (_fn_imm(m.group(2)) if m.group(2) else 0)) & 0xFFFFFFFF)
    return str(_fn_imm(t))


def translate_asm_function(body, params, symbol_address, function_address, where=""):
    """body: the text between the braces; params: C++ expressions for a0, a1, ... (guest-ABI register images, uint64);
    symbol_address(name) -> int: a guest symbol's address (globals in memory operands, `jal sym`). Returns the C++ body."""
    items = []
    for raw in body.split("\n"):
        l = raw.split("#")[0].split("//")[0].strip()
        if not l or l.startswith("."):
            continue
        m = re.fullmatch(r"@?(\w+):", l)
        if m:
            items.append(("label", m.group(1)))
            continue
        parts = l.split(None, 1)
        ops = [o.strip() for o in split_top(parts[1], ",")] if len(parts) > 1 else []
        items.append(("insn", parts[0], ops, l))
    out = []
    W = out.append
    W("auto& _S = b3asm::state(); (void)_S;")
    W("b3asm::Q R[32] = {};")
    for k, p in enumerate(params):
        W("b3asm::set64(R[%d], static_cast<std::uint64_t>(%s));" % (4 + k, p))
    W("b3asm::set64(R[29], b3asm::sx32(b3host::backend().stack_pointer()));")
    e = Emitter([], False, {})

    def one(mn, ops, text):
        """one non-branch instruction -> C++ statement ('' for none)"""
        r = lambda k: _fn_reg(ops[k])
        g = lambda k: "b3asm::Q{}" if r(k) == 0 else "R[%d]" % r(k)
        if mn in ("mult", "multu", "mult1", "multu1"):
            unit = "1" if mn.endswith("1") else ""
            rs, rt = (g(1), g(2)) if len(ops) == 3 else (g(0), g(1))
            if mn.startswith("multu"):
                prod = "(static_cast<std::uint64_t>(static_cast<std::uint32_t>(b3asm::lo64(%s))) * static_cast<std::uint64_t>(static_cast<std::uint32_t>(b3asm::lo64(%s))))" % (rs, rt)
            else:
                prod = "static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(b3asm::lo64(%s))) * static_cast<std::int64_t>(static_cast<std::int32_t>(b3asm::lo64(%s))))" % (rs, rt)
            s = "{ const std::uint64_t p_ = %s; _S.lo%s = b3asm::sx32(static_cast<std::uint32_t>(p_)); _S.hi%s = b3asm::sx32(static_cast<std::uint32_t>(p_ >> 32));" % (prod, unit, unit)
            if len(ops) == 3 and r(0) != 0:
                s += " b3asm::set64(R[%d], _S.lo%s);" % (r(0), unit)
            return s + " }"
        if mn in ("div", "divu", "div1", "divu1"):
            unit = "1" if mn.endswith("1") else ""
            a, b = (g(1), g(2)) if len(ops) == 3 and ops[0].strip() == "$zero" else (g(0), g(1))
            if mn.startswith("divu"):
                return ("{ const std::uint32_t n_ = static_cast<std::uint32_t>(b3asm::lo64(%s)), d_ = static_cast<std::uint32_t>(b3asm::lo64(%s)); "
                        "if (d_) { _S.lo%s = b3asm::sx32(n_ / d_); _S.hi%s = b3asm::sx32(n_ %% d_); } }" % (a, b, unit, unit))
            return ("{ const std::int32_t n_ = static_cast<std::int32_t>(b3asm::lo64(%s)), d_ = static_cast<std::int32_t>(b3asm::lo64(%s)); "
                    "_S.lo%s = b3asm::sx32(static_cast<std::uint32_t>(b3host::ee_div(n_, d_))); _S.hi%s = b3asm::sx32(static_cast<std::uint32_t>(b3host::ee_mod(n_, d_))); }" % (a, b, unit, unit))
        if mn in ("mfhi", "mflo", "mfhi1", "mflo1"):
            return "" if r(0) == 0 else "b3asm::set64(R[%d], _S.%s);" % (r(0), mn[2:])
        if mn in ("mthi", "mtlo", "mthi1", "mtlo1"):
            return "_S.%s = b3asm::lo64(%s);" % (mn[2:], g(0))
        norm = [_fn_operand(o, symbol_address) for o in ops]
        s = insn(e, "%s %s" % (mn, ", ".join(norm)) if norm else mn)
        return s.replace("_S.gpr[", "R[")

    def cond(mn, ops):
        g = lambda k: "static_cast<std::int64_t>(b3asm::lo64(%s))" % ("b3asm::Q{}" if _fn_reg(ops[k]) == 0 else "R[%d]" % _fn_reg(ops[k]))
        if mn in _FN_BRANCH2:
            return "%s %s %s" % (g(0), _FN_BRANCH2[mn], g(1)), ops[2]
        return "%s %s" % (g(0), _FN_BRANCH1[mn if mn in _FN_BRANCH1 else mn[:-1]]), ops[1]

    lab = lambda t: "L_" + t.strip().lstrip("@")
    k, n = 0, len(items)
    while k < n:
        it = items[k]
        if it[0] == "label":
            W("L_%s:;" % it[1])
            k += 1
            continue
        _, mn, ops, text = it
        if mn not in _FN_BRANCHES:
            if BRANCHES.match(mn):
                raise AsmError("branch %r not supported in asm functions" % text)
            s = one(mn, ops, text)
            if s:
                W(s + "   // " + text)
            k += 1
            continue
        if k + 1 >= n or items[k + 1][0] != "insn":
            raise AsmError("branch %r without a delay-slot instruction" % text)
        _, dmn, dops, dtext = items[k + 1]
        if BRANCHES.match(dmn):
            raise AsmError("branch in a delay slot: %r" % dtext)
        slot = one(dmn, dops, dtext)
        if mn in ("b", "j"):
            W("{ %s goto %s; }   // %s / %s" % (slot, lab(ops[0]), text, dtext))
        elif mn == "jr":
            if ops[0].strip() not in ("$ra", "$31"):
                raise AsmError("jr %r: only jr $ra" % ops[0])
            W("{ %s return; }   // %s / %s" % (slot, text, dtext))
        elif mn == "jal":
            fa = symbol_address(ops[0].strip())
            W("{ %s b3host::CallArgs a_; for (unsigned i = 0; i < 8; ++i) a_.gpr[i] = R[4 + i]; a_.n_gpr = 8; a_.sp = static_cast<std::uint32_t>(b3asm::lo64(R[29])); b3host::CallResult r_; b3host::detail::dispatch(0x%08Xu, a_, r_); R[2] = r_.v0; b3asm::set64(R[3], r_.v1); }   // %s / %s" % (slot, fa, text, dtext))
        else:
            c, target = cond(mn, ops)
            likely = mn in ("beql", "bnel") or (mn.endswith("l") and mn[:-1] in _FN_BRANCH1)
            if likely:
                W("if (%s) { %s goto %s; }   // %s / %s (likely)" % (c, slot, lab(target), text, dtext))
            else:
                W("{ const bool c_ = %s; %s if (c_) goto %s; }   // %s / %s" % (c, slot, lab(target), text, dtext))
        k += 2
    W("return;")
    return "\n".join("    " + l for l in out)
