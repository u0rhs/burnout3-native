#!/usr/bin/env python3
# vu0_aot.py -- retail's VU0 microprograms as C++ (vu0 lane, 2026-11-07; after mac1's race profile:
# VU0 micro mode was 43% of host time while driving).
#
# Reads the four VU0 images CGtVu0CodeManager uploads (the decomp's assembly/assets/vu/game_<addr>.textbin.bin: VIF0
# DMA chains with MPG) and writes one C++ function per image: every loaded pair as straight code with its decode done
# here, an entry switch, static gotos for the branches. The semantics are b3asset::Vu1::run's with pair_timing = true,
# op for op (src/b3asset/vu1.cpp; src/native/vu0_aot_rt.h has the pending-state runtime and the rules). Pairs whose
# lower or upper op the interpreter would refuse become a fatal at that pair (none in the four images' reachable code).
#
#   python3 -I tools/native/vu0_aot.py <vu dir> <out.cpp>
#   python3 -I tools/native/vu0_aot.py --elf <SLUS_210.50> <out.cpp>   (no decomp: the images from the game's ELF)
# Either way each image must match tools/native/vu0_aot_images.sha256, or nothing is written (exit 1); both sources
# give the same file. Its output is committed, as the rest of the generated game code: gen/native/vu0/vu0_aot_gen.cpp
# (`make vu0-aot` rewrites it; `make native-bundle` too). It holds the translated programs and each image's hash; no
# copy of the microcode words (the image is matched by its SHA-256 at run time, src/native/vu0_unit.cpp).
import os
import struct
import sys

IMAGES = ["004787D0", "00479610", "0047F6D0", "004839E0"]   # [C docs/vu0-micro.md: CGtVu0CodeManager's four images]


def load_textbin(path, data=None):
    """the VIF0 MPG payloads of a textbin image (a file, or `data`: the DMA chain's bytes from its start) -> (lower[512],
    upper[512], loaded pair count from 0); as VuMicroprogram::load_textbin, with VU0's 512-pair mask"""
    d = data if data is not None else open(path, "rb").read()
    lower, upper, loaded = [0] * 512, [0] * 512, set()
    o = 0
    while o + 16 <= len(d):
        tag = struct.unpack_from("<I", d, o)[0]
        end = min(len(d), o + 16 + (tag & 0xFFFF) * 16)
        idd = (tag >> 28) & 7
        v = o + 8
        while v + 4 <= end:
            code = struct.unpack_from("<I", d, v)[0]
            v += 4
            cmd = (code >> 24) & 0x7F
            if cmd == 0x4A:
                num = ((code >> 16) & 0xFF) or 256
                addr = code & 0xFFFF
                v = (v + 7) & ~7
                for k in range(num):
                    a = (addr + k) & 511
                    lower[a], upper[a] = struct.unpack_from("<II", d, v + 8 * k)
                    loaded.add(a)
                v += 8 * num
            elif cmd in (0, 0x10, 0x11, 0x13) or 1 <= cmd <= 7:
                pass
            else:
                sys.exit("vu0_aot: %s: unexpected VIF code %08X" % (path, code))
        o = o + 16 + (tag & 0xFFFF) * 16
        if idd in (6, 7):
            break
    n = len(loaded)
    if loaded != set(range(n)):
        sys.exit("vu0_aot: %s: the loaded pairs are not 0..%d" % (path, n - 1))
    return lower, upper, n


def elf_bytes_at(elf, va, length):
    """`length` bytes of the 32-bit little-endian ELF at virtual address va (from the PT_LOAD segment holding it)"""
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        sys.exit("vu0_aot: not a 32-bit little-endian ELF")
    phoff, = struct.unpack_from("<I", elf, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", elf, 0x2A)
    for k in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<5I", elf, phoff + k * phentsize)
        if p_type == 1 and p_vaddr <= va < p_vaddr + p_filesz:
            n = min(length, p_vaddr + p_filesz - va)
            return elf[p_offset + va - p_vaddr:p_offset + va - p_vaddr + n]
    sys.exit("vu0_aot: address %08X is in no loaded segment of the ELF" % va)


def check_hash(img, lower, upper, n):
    import hashlib
    known = {}
    for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "vu0_aot_images.sha256")):
        f = line.split()
        if f and not f[0].startswith("#"):
            known[f[0]] = (int(f[1]), f[2])
    words = b"".join(lower[p].to_bytes(4, "little") + upper[p].to_bytes(4, "little") for p in range(n))
    got = (n, hashlib.sha256(words).hexdigest())
    if known.get(img) != got:
        sys.exit("vu0_aot: image %s: %d pairs, sha256 %s; expected %s (tools/native/vu0_aot_images.sha256): not "
                 "retail's SLUS-21050 image" % (img, got[0], got[1], known.get(img)))
    return got[1]


def s11(w):
    v = w & 0x7FF
    return v - 0x800 if v & 0x400 else v


def has(dest, lane):
    return (dest >> (3 - lane)) & 1


class Unsupported(Exception):
    pass


def upper_slot(up):
    """-> (compute lines, writeback lines); compute reads the pre-pair state into u0..u3 (and the flags / CLIP)"""
    udest = (up >> 21) & 0xF
    uft, ufs, ufd = (up >> 16) & 31, (up >> 11) & 31, (up >> 6) & 31
    uop = up & 0x3F
    uop2 = (((up >> 6) & 0x1F) << 2) | (up & 3)
    vf = lambda r, l: "c.vf[%d][%d]" % (r, l)
    acc = lambda l: "c.acc[%d]" % l

    def fmac(kind, lane, a, b):
        return {0: "fr::add(%s, %s)" % (a, b), 1: "fr::sub(%s, %s)" % (a, b),
                2: "fr::madd(%s, %s, %s)" % (acc(lane), a, b), 3: "fr::msub(%s, %s, %s)" % (acc(lane), a, b),
                4: "fr::max(%s, %s)" % (a, b), 5: "fr::min(%s, %s)" % (a, b), 6: "fr::mul(%s, %s)" % (a, b)}[kind]

    comp, wb = [], []
    lanes = [l for l in range(4) if has(udest, l)]

    def arith(kind, scalar, bc, to_acc):
        # ures[l] for the dest lanes; the others are not written (VF) or keep ACC (all lanes written back = same)
        pre = []
        if scalar == "Q":
            pre.append("const std::uint32_t us = rt.qread();")
            sb = "us"
        elif scalar == "I":
            sb = "c.I"
        for l in lanes:
            b = sb if scalar else (vf(uft, bc) if bc is not None else vf(uft, l))
            pre.append("const std::uint32_t u%d = %s;" % (l, fmac(kind, l, vf(ufs, l), b)))
        w = []
        for l in lanes:
            w.append("%s = u%d;" % (acc(l) if to_acc else vf(ufd, l), l))
        if not to_acc and ufd == 0:
            w = []   # VF0 is never written (the flags still are)
        if kind not in (4, 5):
            if lanes:
                w.append("rt.push_flags(%s);" % " | ".join("lane_mac(u%d, %d)" % (l, l) for l in lanes))
            else:
                w.append("rt.push_flags(0);")
        return pre, w

    if uop < 0x3C:
        if uop < 0x1C:
            return arith(uop >> 2, None, uop & 3, False)
        tab = {0x1C: (6, "Q"), 0x1D: (4, "I"), 0x1E: (6, "I"), 0x1F: (5, "I"), 0x20: (0, "Q"), 0x21: (2, "Q"),
               0x22: (0, "I"), 0x23: (2, "I"), 0x24: (1, "Q"), 0x25: (3, "Q"), 0x26: (1, "I"), 0x27: (3, "I"),
               0x28: (0, None), 0x29: (2, None), 0x2A: (6, None), 0x2B: (4, None), 0x2C: (1, None), 0x2D: (3, None),
               0x2F: (5, None)}
        if uop in tab:
            k, s = tab[uop]
            return arith(k, s, None, False)
        if uop == 0x2E:   # OPMSUB fd.xyz = ACC - fs.yzx * ft.zxy; flags over every dest lane (w keeps fd.w)
            a1, a2 = [1, 2, 0], [2, 0, 1]
            for l in lanes:
                if l < 3:
                    comp.append("const std::uint32_t u%d = fr::msub(%s, %s, %s);" % (l, acc(l), vf(ufs, a1[l]), vf(uft, a2[l])))
                else:
                    comp.append("const std::uint32_t u3 = %s;" % vf(ufd, 3))
            if ufd:
                wb += ["%s = u%d;" % (vf(ufd, l), l) for l in lanes]
            wb.append("rt.push_flags(%s);" % (" | ".join("lane_mac(u%d, %d)" % (l, l) for l in lanes) or "0"))
            return comp, wb
        raise Unsupported("upper op %02X" % uop)
    if uop2 == 0x2F:   # NOP
        return [], []
    if uop2 < 0x10:
        return arith(uop2 >> 2, None, uop2 & 3, True)
    if 0x18 <= uop2 <= 0x1B:
        return arith(6, None, uop2 & 3, True)
    if 0x10 <= uop2 <= 0x17:   # ITOF / FTOI
        fb = [0, 4, 12, 15][uop2 & 3]
        fn = "itof" if uop2 < 0x14 else "ftoi"
        for l in lanes:
            comp.append("const std::uint32_t u%d = %s(%s, %d);" % (l, fn, vf(ufs, l), fb))
        if uft:
            wb += ["%s = u%d;" % (vf(uft, l), l) for l in lanes]
        return comp, wb
    if uop2 == 0x1D:   # ABS
        for l in lanes:
            comp.append("const std::uint32_t u%d = fr::clamp(%s) & 0x7FFFFFFFu;" % (l, vf(ufs, l)))
        if uft:
            wb += ["%s = u%d;" % (vf(uft, l), l) for l in lanes]
        return comp, wb
    if uop2 == 0x1F:   # CLIP
        comp.append("std::uint32_t ucl = 0; { const float aw = b3fp::flt(fr::clamp(%s) & 0x7FFFFFFFu);" % vf(uft, 3))
        for l in range(3):
            comp.append("  { const float x = b3fp::flt(fr::clamp(%s)); if (x > aw) ucl |= %du; if (x < -aw) ucl |= %du; }"
                        % (vf(ufs, l), 1 << (2 * l), 2 << (2 * l)))
        comp.append("}")
        wb.append("rt.push_clip(ucl);")
        return comp, wb
    tab2 = {0x1C: (6, "Q"), 0x1E: (6, "I"), 0x20: (0, "Q"), 0x21: (2, "Q"), 0x22: (0, "I"), 0x23: (2, "I"),
            0x24: (1, "Q"), 0x25: (3, "Q"), 0x26: (1, "I"), 0x27: (3, "I"), 0x28: (0, None), 0x29: (2, None),
            0x2A: (6, None), 0x2C: (1, None), 0x2D: (3, None)}
    if uop2 in tab2:
        k, s = tab2[uop2]
        return arith(k, s, None, True)
    if uop2 == 0x2E:   # OPMULA ACC.xyz = fs.yzx * ft.zxy
        a1, a2 = [1, 2, 0], [2, 0, 1]
        for l in lanes:
            if l < 3:
                comp.append("const std::uint32_t u%d = fr::mul(%s, %s);" % (l, vf(ufs, a1[l]), vf(uft, a2[l])))
            else:
                comp.append("const std::uint32_t u3 = %s;" % acc(3))
        wb += ["%s = u%d;" % (acc(l), l) for l in lanes]
        wb.append("rt.push_flags(%s);" % (" | ".join("lane_mac(u%d, %d)" % (l, l) for l in lanes) or "0"))
        return comp, wb
    raise Unsupported("upper op2 %02X" % uop2)


def lower_slot(pc, lo, up):
    """-> (lines run in the lower phase, writeback lines, branch: None or ('cond' | 'always', cond expr, target))"""
    ibit = (up >> 31) & 1
    if ibit:
        return ["c.I = 0x%08Xu;" % lo], [], None
    ltype1 = (lo >> 25) == 0x40
    lop = (lo & 0x3F) if ltype1 else (lo >> 25)
    lop2 = (((lo >> 6) & 0x1F) << 2) | (lo & 3)
    ldest = (lo >> 21) & 0xF
    lit, lis, lid = (lo >> 16) & 31, (lo >> 11) & 31, (lo >> 6) & 31
    lfsf, lftf = (lo >> 21) & 3, (lo >> 23) & 3
    lanes = [l for l in range(4) if has(ldest, l)]
    vf = lambda r, l: "c.vf[%d][%d]" % (r, l)
    vis = lambda r: "c.vi[%d]" % (r & 15)
    run, wb = [], []
    vi_writes = []   # (reg, value expr, eligible) in write order (lvi2 first, then lvi)

    def lres(src_of_lane, reg):
        for l in lanes:
            run.append("const std::uint32_t l%d = %s;" % (l, src_of_lane(l)))
        if reg:
            wb.extend("%s = l%d;" % (vf(reg, l), l) for l in lanes)

    def lvi(reg, expr, eligible=True):
        run.append("const std::uint16_t lv = static_cast<std::uint16_t>(%s);" % expr)
        vi_writes.append((reg, "lv", eligible))

    def lvi2(reg, expr):
        run.append("const std::uint16_t lv2 = static_cast<std::uint16_t>(%s);" % expr)
        vi_writes.insert(0, (reg, "lv2", True))

    branch = None
    if ltype1:
        if lop < 0x3C:
            if lop == 0x30:
                lvi(lid, "%s + %s" % (vis(lis), vis(lit)))
            elif lop == 0x31:
                lvi(lid, "%s - %s" % (vis(lis), vis(lit)))
            elif lop == 0x32:
                imm = (lo >> 6) & 31
                if imm & 16:
                    imm -= 32
                lvi(lit, "%s + (%d)" % (vis(lis), imm))
            elif lop == 0x34:
                lvi(lid, "%s & %s" % (vis(lis), vis(lit)))
            elif lop == 0x35:
                lvi(lid, "%s | %s" % (vis(lis), vis(lit)))
            else:
                raise Unsupported("lower op1 %02X" % lop)
        else:
            if lop2 == 0x30:     # MOVE (lower NOP: no dest lanes)
                lres(lambda l: vf(lis, l), lit)
            elif lop2 == 0x31:   # MR32
                lres(lambda l: vf(lis, (l + 1) & 3), lit)
            elif lop2 == 0x34:   # LQI
                run.append("const std::uint32_t* m = rt.qw(%s);" % vis(lis))
                lres(lambda l: "m[%d]" % l, lit)
                lvi2(lis, "%s + 1" % vis(lis))
            elif lop2 == 0x36:   # LQD
                run.append("const std::uint16_t la = static_cast<std::uint16_t>(%s - 1);" % vis(lis))
                run.append("const std::uint32_t* m = rt.qw(la);")
                lres(lambda l: "m[%d]" % l, lit)
                lvi2(lis, "la")
            elif lop2 == 0x35:   # SQI
                if lanes:
                    run.append("{ std::uint32_t* m = rt.qw(%s); %s }" % (vis(lit), " ".join("m[%d] = %s;" % (l, vf(lis, l)) for l in lanes)))
                lvi2(lit, "%s + 1" % vis(lit))
            elif lop2 == 0x37:   # SQD
                run.append("const std::uint16_t la = static_cast<std::uint16_t>(%s - 1);" % vis(lit))
                if lanes:
                    run.append("{ std::uint32_t* m = rt.qw(la); %s }" % " ".join("m[%d] = %s;" % (l, vf(lis, l)) for l in lanes))
                lvi2(lit, "la")
            elif lop2 in (0x38, 0x39, 0x3A):   # DIV, SQRT, RSQRT
                run.append("{ const std::uint32_t n = %s, d = %s; std::uint32_t st = 0; if (rt.qp) rt.commit_q();" % (vf(lis, lfsf), vf(lit, lftf)))
                if lop2 == 0x38:
                    run.append("  rt.qn = fr::vu_div(n, d); if ((fr::clamp(d) & 0x7FFFFFFFu) == 0) st = (fr::clamp(n) & 0x7FFFFFFFu) ? 0x20u : 0x10u;")
                elif lop2 == 0x39:
                    run.append("  (void)n; rt.qn = fr::vu_sqrt(d); if ((d & 0x80000000u) && (d & 0x7FFFFFFFu)) st = 0x10u;")
                else:
                    run.append("  rt.qn = fr::vu_rsqrt(n, d); if ((fr::clamp(d) & 0x7FFFFFFFu) == 0) st = 0x20u; else if (d & 0x80000000u) st = 0x10u;")
                run.append("  rt.qs = st; rt.qp = true; rt.qr = rt.now + %d; }" % (13 if lop2 == 0x3A else 7))
            elif lop2 == 0x3B:   # WAITQ
                run.append("if (rt.qp) rt.commit_q();")
            elif lop2 == 0x3C:   # MTIR
                lvi(lit, vf(lis, lfsf))
            elif lop2 == 0x3D:   # MFIR
                lres(lambda l: "static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(%s)))" % vis(lis), lit)
            elif lop2 == 0x3E:   # ILWR: the last dest lane's word
                if lanes:
                    run.append("const std::uint32_t* m = rt.qw(%s);" % vis(lis))
                    lvi(lit, "m[%d]" % lanes[-1])
            elif lop2 == 0x3F:   # ISWR
                if lanes:
                    run.append("{ std::uint32_t* m = rt.qw(%s); %s }" % (vis(lis), " ".join("m[%d] = %s;" % (l, vis(lit)) for l in lanes)))
            elif lop2 == 0x64:   # MFP
                run.append("if (rt.pp) ++c.stats.p_read_pending;")
                lres(lambda l: "c.P", lit)
            elif lop2 == 0x7B:   # WAITP
                pass
            elif lop2 in (0x78, 0x79, 0x7A):   # ESQRT, ERSQRT, ERCPR
                x = vf(lis, lfsf)
                expr, lat = {0x78: ("fr::vu_sqrt(%s)" % x, 12), 0x79: ("fr::vu_rsqrt(0x3F800000u, %s)" % x, 18),
                             0x7A: ("fr::vu_div(0x3F800000u, %s)" % x, 12)}[lop2]
                run.append("rt.pn = %s; rt.pr = rt.now + %d; rt.pp = true;" % (expr, lat))
            else:
                raise Unsupported("lower op2 %02X" % lop2)   # XTOP / XITOP / XGKICK are VU1's
    else:
        if lop == 0x00:   # LQ
            run.append("const std::uint32_t* m = rt.qw(static_cast<std::uint32_t>(%s + (%d)));" % (vis(lis), s11(lo)))
            lres(lambda l: "m[%d]" % l, lit)
        elif lop == 0x01:   # SQ
            if lanes:
                run.append("{ std::uint32_t* m = rt.qw(static_cast<std::uint32_t>(%s + (%d))); %s }" % (
                    vis(lit), s11(lo), " ".join("m[%d] = %s;" % (l, vf(lis, l)) for l in lanes)))
        elif lop == 0x04:   # ILW
            if lanes:
                run.append("const std::uint32_t* m = rt.qw(static_cast<std::uint32_t>(%s + (%d)));" % (vis(lis), s11(lo)))
                lvi(lit, "m[%d]" % lanes[-1])
        elif lop == 0x05:   # ISW
            if lanes:
                run.append("{ std::uint32_t* m = rt.qw(static_cast<std::uint32_t>(%s + (%d))); %s }" % (
                    vis(lis), s11(lo), " ".join("m[%d] = %s;" % (l, vis(lit)) for l in lanes)))
        elif lop in (0x08, 0x09):   # IADDIU, ISUBIU
            imm = ((lo >> 10) & 0x7800) | (lo & 0x7FF)
            lvi(lit, "%s %s %du" % (vis(lis), "+" if lop == 0x08 else "-", imm))
        elif lop == 0x10:
            lvi(1, "c.clip == 0x%06Xu" % (lo & 0xFFFFFF), False)
        elif lop == 0x11:
            run.append("c.clip = 0x%06Xu; rt.clear_clip();" % (lo & 0xFFFFFF))
        elif lop == 0x12:
            lvi(1, "(c.clip & 0x%06Xu) != 0" % (lo & 0xFFFFFF), False)
        elif lop == 0x13:
            lvi(1, "((c.clip | 0x%06Xu) & 0xFFFFFFu) == 0xFFFFFFu" % (lo & 0xFFFFFF), False)
        elif 0x14 <= lop <= 0x17:
            imm = ((lo >> 10) & 0x800) | (lo & 0x7FF)
            if lop == 0x15:
                run.append("c.status = (c.status & 0x3Fu) | 0x%Xu;" % (imm & 0xFC0))
            else:
                run.append("rt.flag_read(fb);")
                e = {0x14: "c.status == 0x%Xu", 0x16: "c.status & 0x%Xu", 0x17: "c.status | 0x%Xu"}[lop] % imm
                lvi(lit, e, False)
        elif lop in (0x18, 0x1A, 0x1B):
            run.append("rt.flag_read(fb);")
            e = {0x18: "c.mac == %s", 0x1A: "c.mac & %s", 0x1B: "c.mac | %s"}[lop] % vis(lis)
            lvi(lit, e, False)
        elif lop == 0x1C:
            lvi(lit, "c.clip & 0xFFFu", False)
        elif lop in (0x20, 0x21):   # B, BAL
            branch = ("always", None, (pc + 1 + s11(lo)) & 511)
            if lop == 0x21:
                lvi(lit, "%du" % ((pc + 2) & 0xFFFF))
        elif lop in (0x28, 0x29):   # IBEQ, IBNE
            run.append("taken = (rt.branch_vi(%d, bp) == rt.branch_vi(%d, bp)) == %s;" % (lit, lis, "true" if lop == 0x28 else "false"))
            branch = ("cond", "lc", (pc + 1 + s11(lo)) & 511)
        elif lop in (0x2C, 0x2D, 0x2E, 0x2F):
            rel = {0x2C: "< 0", 0x2D: "> 0", 0x2E: "<= 0", 0x2F: ">= 0"}[lop]
            run.append("taken = static_cast<std::int16_t>(rt.branch_vi(%d, bp)) %s;" % (lis, rel))
            branch = ("cond", "lc", (pc + 1 + s11(lo)) & 511)
        else:
            raise Unsupported("lower op0 %02X" % lop)   # JR / JALR: none in the four images
    # order: the lower VF result, then the VI writes (lvi2, lvi); the upper result follows in gen_image
    vf_wb = wb
    vi_wb = ["rt.write_vi(%d, %s, %s);" % (reg, val, "true" if el else "false") for reg, val, el in vi_writes]
    return run, vf_wb + vi_wb, branch


def gen_image(name, lower, upper, n):
    out = []
    W = out.append
    W("bool run_%s(Core& c, std::uint32_t* data, std::uint32_t entry) {" % name)
    W("    Rt rt(c, data);")
    W("    const std::uint64_t fb = rt.flag_commits; (void)fb;")
    W("    bool bp = false, ending = false;")
    W("    (void)bp; (void)ending;")
    W("    switch (entry & 511u) {")
    for p in range(n):
        W("    case %d: goto P%d;" % (p, p))
    W("    default: return false;")
    W("    }")
    branches = {}
    ebits = {}
    for p in range(n):
        lo, up = lower[p], upper[p]
        W("P%d: {" % p)
        try:
            uc, uw = upper_slot(up)
            lr, lw, br = lower_slot(p, lo, up)
        except Unsupported as ex:
            W('    b3host::fatal("vu0 aot: %s pair %d: %s (the interpreter refuses it too)");' % (name, p, ex))
            W("}")
            branches[p] = None
            ebits[p] = 0
            continue
        branches[p] = br
        ebits[p] = (up >> 30) & 1
        W("    rt.commit();")
        if br and br[0] == "cond":
            W("    bool taken = false;")
        for l in uc:
            W("    " + l)
        if lr or lw:
            W("    {")
            for l in lr:
                W("        " + l)
            for l in lw:
                W("        " + l)
            W("    }")
        for l in uw:
            W("    " + l)
        W("    ++rt.now;")
        # epilogue (Vu1::run's tail)
        nxt = (p + 1) & 511
        prev = branches.get(p - 1) if p > 0 else None
        if p > 0 and ebits.get(p - 1):
            tgt = ("bp ? %du : %du" % (prev[2], nxt)) if prev else ("%du" % nxt)
            W("    if (ending) { c.tpc = %s; rt.finish(); return true; }" % tgt)
        if ebits[p]:
            W("    ending = true;")
        if prev:
            W("    if (bp) { bp = false; if (rt.now > kMaxPairs) cycle_limit(); goto P%d; }" % prev[2])
        if br:
            if br[0] == "always":
                W("    bp = true;")
            else:
                W("    bp = taken;")
        if nxt >= n:
            W('    b3host::fatal("vu0 aot: %s ran past its last pair");' % name)
        W("}")
    W("}")
    text = "\n".join(out)
    return text


def main():
    args = sys.argv[1:]
    elf = None
    if len(args) == 3 and args[0] == "--elf":
        elf = open(args[1], "rb").read()
        args = args[1:]
    if len(args) != 2:
        sys.exit("usage: vu0_aot.py <vu dir> <out.cpp>   |   vu0_aot.py --elf <SLUS_210.50> <out.cpp>")
    vu_dir, out_path = args[0], args[1]
    parts = ["// GENERATED by tools/native/vu0_aot.py from retail's four VU0 images (tools/native/vu0_aot_images.sha256);",
             "// do not edit. Regenerate: make vu0-aot (from the decomp snapshot, or VU0_AOT_ELF=<SLUS_210.50>).",
             '#include "vu0_aot_rt.h"', "", "namespace b3native::vu0aot {", "namespace {",
             "constexpr std::uint64_t kMaxPairs = 4096u * 64u;   // Vu0Unit::call's limit",
             '[[noreturn]] void cycle_limit() { b3host::fatal("vu0 aot: cycle limit"); }']
    table = []
    for img in IMAGES:
        if elf is not None:   # the DMA chain at the image's address; load_textbin stops at its END / RET tag
            lower, upper, n = load_textbin(None, elf_bytes_at(elf, int(img, 16), 0x10000))
        else:
            lower, upper, n = load_textbin(os.path.join(vu_dir, "game_%s.textbin.bin" % img))
        digest = check_hash(img, lower, upper, n)
        parts.append(gen_image(img, lower, upper, n))
        table.append('{"%s", %du, "%s", run_%s}' % (img, n, digest, img))
    parts.append("const Image kImages[] = {%s};" % ", ".join(table))
    parts.append("}  // namespace")
    parts.append("const Image* images(unsigned* n) { *n = %d; return kImages; }" % len(table))
    parts.append("}  // namespace b3native::vu0aot")
    text = "\n".join(parts) + "\n"
    tmp = out_path + ".tmp"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, out_path)


if __name__ == "__main__":
    main()
