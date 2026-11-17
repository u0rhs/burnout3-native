#!/usr/bin/env python3
"""VU micro-mode disassembler for the game's VU1 microprogram images (render3, docs/native-renderer.md §11).

Input: a `*.textbin.bin` image from the decomp (`assembly/assets/vu/`), i.e. a DMA chain whose VIF stream holds MPG
commands, or a raw microcode blob (--raw). Output: one line per instruction pair, `addr  upper | lower`, addresses in
instruction units (the unit MSCAL/B/JR use; byte address = 8 * addr). Branch targets and XGKICKs are marked.
Encodings: VU User's Manual ch. 5/6 and the VU Instruction Manual (research/ext/PS2Docs in the decomp).
Run: python3 -I tools/render/vudis.py <image> [--raw] [--load ADDR]. Listings are game-derived: keep them out of git.
"""
import struct
import sys

BC = "xyzw"


def dest(w):
    return "".join(c for i, c in enumerate("xyzw") if (w >> (24 - i)) & 1)


UP = {0x1C: "mulq", 0x1D: "maxi", 0x1E: "muli", 0x1F: "minii", 0x20: "addq", 0x21: "maddq", 0x22: "addi",
      0x23: "maddi", 0x24: "subq", 0x25: "msubq", 0x26: "subi", 0x27: "msubi", 0x28: "add", 0x29: "madd",
      0x2A: "mul", 0x2B: "max", 0x2C: "sub", 0x2D: "msub", 0x2E: "opmsub", 0x2F: "mini"}
UPBC = ["add", "sub", "madd", "msub", "max", "mini", "mul"]
UP2 = {0x10: "itof0", 0x11: "itof4", 0x12: "itof12", 0x13: "itof15", 0x14: "ftoi0", 0x15: "ftoi4", 0x16: "ftoi12",
       0x17: "ftoi15", 0x1C: "mulaq", 0x1D: "abs", 0x1E: "mulai", 0x1F: "clip", 0x20: "addaq", 0x21: "maddaq",
       0x22: "addai", 0x23: "maddai", 0x24: "subaq", 0x25: "msubaq", 0x26: "subai", 0x27: "msubai", 0x28: "adda",
       0x29: "madda", 0x2A: "mula", 0x2C: "suba", 0x2D: "msuba", 0x2E: "opmula", 0x2F: "nop"}
UP2BC = {0: "adda", 1: "suba", 2: "madda", 3: "msuba", 6: "mula"}


def upper(w):
    d = dest(w)
    ft, fs, fd = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31
    f = w & 0x3F
    if f < 0x1C:
        return f"{UPBC[f >> 2]}{BC[f & 3]}.{d} vf{fd:02}, vf{fs:02}, vf{ft:02}{BC[f & 3]}"
    if f < 0x3C:
        n = UP.get(f)
        if n is None:
            return f"?up{f:02x}"
        if n.endswith("q"):
            return f"{n}.{d} vf{fd:02}, vf{fs:02}, Q"
        if n.endswith("i"):
            return f"{n}.{d} vf{fd:02}, vf{fs:02}, I"
        return f"{n}.{d} vf{fd:02}, vf{fs:02}, vf{ft:02}"
    f2 = ((w >> 6) & 0x1F) << 2 | (w & 3)
    if f2 < 0x10 or 0x18 <= f2 <= 0x1B:
        k = f2 >> 2 if f2 < 0x10 else 6
        return f"{UP2BC[k]}{BC[f2 & 3]}.{d} ACC, vf{fs:02}, vf{ft:02}{BC[f2 & 3]}"
    n = UP2.get(f2)
    if n is None:
        return f"?up2_{f2:02x}"
    if n == "nop":
        return "nop"
    if n == "clip":
        return f"clipw.xyz vf{fs:02}, vf{ft:02}w"
    if n.startswith(("itof", "ftoi", "abs")):
        return f"{n}.{d} vf{ft:02}, vf{fs:02}"
    if n.endswith("q"):
        return f"{n}.{d} ACC, vf{fs:02}, Q"
    if n.endswith("i"):
        return f"{n}.{d} ACC, vf{fs:02}, I"
    return f"{n}.{d} ACC, vf{fs:02}, vf{ft:02}"


LO1 = {0x30: "iadd", 0x31: "isub", 0x32: "iaddi", 0x34: "iand", 0x35: "ior"}
LO2 = {0x30: "move", 0x31: "mr32", 0x34: "lqi", 0x35: "sqi", 0x36: "lqd", 0x37: "sqd", 0x38: "div", 0x39: "sqrt",
       0x3A: "rsqrt", 0x3B: "waitq", 0x3C: "mtir", 0x3D: "mfir", 0x3E: "ilwr", 0x3F: "iswr", 0x40: "rnext",
       0x41: "rget", 0x42: "rinit", 0x43: "rxor", 0x64: "mfp", 0x68: "xtop", 0x69: "xitop", 0x6C: "xgkick",
       0x70: "esadd", 0x71: "ersadd", 0x72: "eleng", 0x73: "erleng", 0x74: "eatanxy", 0x75: "eatanxz", 0x76: "esum",
       0x78: "esqrt", 0x79: "ersqrt", 0x7A: "ercpr", 0x7B: "waitp", 0x7C: "esin", 0x7D: "eatan", 0x7E: "eexp"}
LO0 = {0x00: "lq", 0x01: "sq", 0x04: "ilw", 0x05: "isw", 0x08: "iaddiu", 0x09: "isubiu", 0x10: "fceq",
       0x11: "fcset", 0x12: "fcand", 0x13: "fcor", 0x14: "fseq", 0x15: "fsset", 0x16: "fsand", 0x17: "fsor",
       0x18: "fmeq", 0x1A: "fmand", 0x1B: "fmor", 0x1C: "fcget", 0x20: "b", 0x21: "bal", 0x24: "jr", 0x25: "jalr",
       0x28: "ibeq", 0x29: "ibne", 0x2C: "ibltz", 0x2D: "ibgtz", 0x2E: "iblez", 0x2F: "ibgez"}


def s11(w):
    v = w & 0x7FF
    return v - 0x800 if v & 0x400 else v


def lower(w, pc):
    """Returns (text, branch_target or None)."""
    d = dest(w)
    it, is_, id_ = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31
    fsf, ftf = BC[(w >> 21) & 3], BC[(w >> 23) & 3]
    if w >> 25 == 0x40:
        f = w & 0x3F
        if f in LO1:
            n = LO1[f]
            if n == "iaddi":
                imm = (w >> 6) & 31
                imm = imm - 32 if imm & 16 else imm
                return f"iaddi vi{it:02}, vi{is_:02}, {imm}", None
            return f"{n} vi{id_:02}, vi{is_:02}, vi{it:02}", None
        if f < 0x3C:
            return f"?lo1_{f:02x}", None
        f2 = ((w >> 6) & 0x1F) << 2 | (w & 3)
        n = LO2.get(f2)
        if n is None:
            return f"?lo2_{f2:02x}", None
        if n in ("move", "mr32"):
            return f"{n}.{d} vf{it:02}, vf{is_:02}", None
        if n in ("lqi", "lqd"):
            return (f"lqi.{d} vf{it:02}, (vi{is_:02}++)" if n == "lqi" else f"lqd.{d} vf{it:02}, (--vi{is_:02})"), None
        if n in ("sqi", "sqd"):
            return (f"sqi.{d} vf{is_:02}, (vi{it:02}++)" if n == "sqi" else f"sqd.{d} vf{is_:02}, (--vi{it:02})"), None
        if n == "div":
            return f"div Q, vf{is_:02}{fsf}, vf{it:02}{ftf}", None
        if n == "sqrt":
            return f"sqrt Q, vf{it:02}{ftf}", None
        if n == "rsqrt":
            return f"rsqrt Q, vf{is_:02}{fsf}, vf{it:02}{ftf}", None
        if n in ("waitq", "waitp"):
            return n, None
        if n == "mtir":
            return f"mtir vi{it:02}, vf{is_:02}{fsf}", None
        if n == "mfir":
            return f"mfir.{d} vf{it:02}, vi{is_:02}", None
        if n in ("ilwr", "iswr"):
            return f"{n}.{d} vi{it:02}, (vi{is_:02})", None
        if n in ("rnext", "rget"):
            return f"{n}.{d} vf{it:02}, R", None
        if n in ("rinit", "rxor"):
            return f"{n} R, vf{is_:02}{fsf}", None
        if n == "mfp":
            return f"mfp.{d} vf{it:02}, P", None
        if n in ("xtop", "xitop"):
            return f"{n} vi{it:02}", None
        if n == "xgkick":
            return f"xgkick vi{is_:02}", None
        if n in ("esadd", "ersadd", "eleng", "erleng", "esum"):
            return f"{n} P, vf{is_:02}", None
        return f"{n} P, vf{is_:02}{fsf}", None
    op = w >> 25
    n = LO0.get(op)
    if n is None:
        return f"?lo0_{op:02x}", None
    if n == "lq":
        return f"lq.{d} vf{it:02}, {s11(w)}(vi{is_:02})", None
    if n == "sq":
        return f"sq.{d} vf{is_:02}, {s11(w)}(vi{it:02})", None
    if n in ("ilw", "isw"):
        return f"{n}.{d} vi{it:02}, {s11(w)}(vi{is_:02})", None
    if n in ("iaddiu", "isubiu"):
        imm = ((w >> 10) & 0x7800) | (w & 0x7FF)
        return f"{n} vi{it:02}, vi{is_:02}, 0x{imm:x}", None
    if n in ("fceq", "fcset", "fcand", "fcor"):
        return f"{n} vi01, 0x{w & 0xFFFFFF:06x}", None
    if n in ("fseq", "fsset", "fsand", "fsor"):
        imm = ((w >> 10) & 0x800) | (w & 0x7FF)
        return f"{n} vi{it:02}, 0x{imm:03x}", None
    if n in ("fmeq", "fmand", "fmor"):
        return f"{n} vi{it:02}, vi{is_:02}", None
    if n == "fcget":
        return f"fcget vi{it:02}", None
    if n in ("jr", "jalr"):
        return (f"jr vi{is_:02}" if n == "jr" else f"jalr vi{it:02}, vi{is_:02}"), None
    tgt = pc + 1 + s11(w)
    if n == "b":
        return f"b {tgt:04x}", tgt
    if n == "bal":
        return f"bal vi{it:02}, {tgt:04x}", tgt
    if n in ("ibeq", "ibne"):
        return f"{n} vi{it:02}, vi{is_:02}, {tgt:04x}", tgt
    return f"{n} vi{is_:02}, {tgt:04x}", tgt


def load_image(data, raw=False, load=0):
    """Returns {instruction address: (lower word, upper word)} from MPG commands (or a raw blob at `load`)."""
    code = {}
    if raw:
        for i in range(0, len(data) - 7, 8):
            lo, up = struct.unpack_from("<II", data, i)
            code[load + i // 8] = (lo, up)
        return code
    o = 0
    while o + 16 <= len(data):
        tag = struct.unpack_from("<I", data, o)[0]
        qwc = tag & 0xFFFF
        tid = (tag >> 28) & 7
        end = o + 16 + qwc * 16
        # VIF codes: the tag's upper 64 bits then the payload.
        vo = o + 8
        while vo + 4 <= min(end, len(data)):
            vc = struct.unpack_from("<I", data, vo)[0]
            vo += 4
            cmd = (vc >> 24) & 0x7F
            if cmd == 0x4A:  # MPG
                num = (vc >> 16) & 0xFF or 256
                addr = vc & 0xFFFF
                vo = (vo + 7) & ~7
                for k in range(num):
                    lo, up = struct.unpack_from("<II", data, vo + 8 * k)
                    code[addr + k] = (lo, up)
                vo += 8 * num
            elif cmd in (0, 0x10, 0x11, 0x13, 0x14, 0x15, 0x17, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07):
                if cmd in (0x14, 0x15, 0x17):
                    raise SystemExit(f"unexpected kick in image at {vo - 4:#x}")
            else:
                raise SystemExit(f"unexpected VIF code {vc:08x} at {vo - 4:#x}")
        o = end
        if tid in (6, 7):  # ret / end
            break
    return code


def disasm(code):
    targets = set()
    rows = []
    for pc in sorted(code):
        lo, up = code[pc]
        u = upper(up)
        flags = "".join(c for c, b in (("I", 31), ("E", 30), ("M", 29), ("D", 28), ("T", 27)) if (up >> b) & 1)
        if up >> 31 & 1:
            f = struct.unpack("<f", struct.pack("<I", lo))[0]
            l, t = f"loi {f:.8g}  (0x{lo:08x})", None
        else:
            l, t = lower(lo, pc)
        if t is not None:
            targets.add(t)
        rows.append((pc, flags, u, l))
    out = []
    for pc, flags, u, l in rows:
        mark = f"L{pc:04x}:" if pc in targets else ""
        out.append(f"{mark:7}{pc:04x} {flags:3} {u:44} {l}")
    return "\n".join(out)


def main():
    args = sys.argv[1:]
    raw = "--raw" in args
    load = 0
    if "--load" in args:
        load = int(args[args.index("--load") + 1], 0)
    path = [a for a in args if not a.startswith("--") and not a.startswith("0x") and not a.isdigit()][0]
    data = open(path, "rb").read()
    print(disasm(load_image(data, raw, load)))


if __name__ == "__main__":
    main()
