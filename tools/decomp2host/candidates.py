#!/usr/bin/env python3
"""candidates: which decomp functions can run natively as overrides, by trying them (docs/decomp-on-host.md).

For every function with decomp source (a proven matched unit, or a src/nonmatching draft) that is a leaf in retail
(no jal/jalr, no jump out of its own range), or with --calls also every non-leaf (port7: generated code calls other
game functions through call stubs), and, with --vu0, whose retail code uses VU0 (a COP2 instruction, lqc2 or sqc2), it
runs decomp2host on that function alone (which compiles the unit once for its call stubs) and then compiles the
result in both float modes. Output (TSV on stdout): address, track, name, size, vu0, leaf, status, detail.

  candidates.py --decomp DIR [--vu0] [--calls [--nonleaf-only]] [--compile] [--jobs N] [--only ADDR,...]
"""
import argparse
import concurrent.futures
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decomp2host as d2h  # noqa: E402

PORT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def words(decomp, addr, size):
    code = decomp.code(addr, size)
    return [struct.unpack_from("<I", code, k)[0] for k in range(0, size - size % 4, 4)]


def classify(decomp, addr, size):
    """(leaf, uses_vu0) from the retail code."""
    leaf, vu0 = True, False
    for k, w in enumerate(words(decomp, addr, size)):
        op = w >> 26
        if op == 3 or (op == 0 and (w & 63) == 9):        # jal / jalr
            leaf = False
        if op == 2:                                         # j: a tail call if it leaves the function
            t = ((addr + 4 * k) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
            if not (addr <= t < addr + size):
                leaf = False
        if op == 0 and (w & 63) == 8:                       # jr: fine if jr ra
            if ((w >> 21) & 31) != 31:
                leaf = False
        if op == 0x12 or op == 0x36 or op == 0x3E:          # COP2 / lqc2 / sqc2
            vu0 = True
    return leaf, vu0


def try_one(decomp, addr, track, out):
    d2h.RAISE = True
    try:
        shutil.rmtree(out, ignore_errors=True)
        d2h.generate(decomp, [(addr, track)], d2h.Args(decomp.root, out))
        return "generated", ""
    except d2h.GenError as e:
        return "gen_fail", str(e).split("\n")[0][:200]
    except Exception as e:   # a transform crash is a finding too
        return "gen_crash", ("%s: %s" % (type(e).__name__, e))[:200]


_DECOMP = None


def _init(root):
    global _DECOMP
    _DECOMP = d2h.Decomp(root)
    os.environ["DECOMP2HOST_JOBS"] = "1"


def _work(job):
    addr, track, out, do_compile = job
    st, det = try_one(_DECOMP, addr, track, out)
    if st == "generated" and do_compile:
        st, det = compile_one(out)
    shutil.rmtree(out, ignore_errors=True)
    return addr, st, det


def compile_one(out):
    units = [os.path.join(out, "units", f) for f in os.listdir(os.path.join(out, "units"))]
    for mode in ("0", "1"):
        cmd = ["g++", "-std=c++20", "-ffp-contract=off", "-fno-fast-math", "-msse2", "-fsyntax-only",
               "-fno-strict-aliasing", "-fno-aggressive-loop-optimizations", "-fstrict-flex-arrays=0", "-fno-access-control", "-fpermissive", "-w", "-I" + os.path.join(PORT, "include"),
               "-I" + os.path.join(PORT, "include/gtmath"), "-I" + os.path.join(out, "include"),
               "-DB3_PS2_FLOAT=" + mode] + units
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            err = [l for l in r.stderr.split("\n") if "error" in l]
            return "compile_fail", (err[0] if err else r.stderr[:200])[:240]
    return "ok", ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--vu0", action="store_true", help="only functions whose retail code uses VU0")
    ap.add_argument("--compile", action="store_true")
    ap.add_argument("--calls", action="store_true", help="also non-leaf functions (call stubs)")
    ap.add_argument("--nonleaf-only", action="store_true")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    decomp = d2h.Decomp(a.decomp)
    only = {int(x, 16) for x in a.only.split(",") if x}
    cands = []
    for addr, (name, size) in sorted(decomp.funcs.items()):
        if only and addr not in only:
            continue
        if decomp.matched_file(addr):
            track = "matched"
        elif os.path.exists(os.path.join(a.decomp, decomp.nonmatching_file(name))):
            track = "nonmatching"
        else:
            continue
        leaf, vu0 = classify(decomp, addr, size)
        if (not leaf and not a.calls) or (leaf and a.nonleaf_only) or (a.vu0 and not vu0):
            continue
        cands.append((addr, track, name, size, vu0, leaf))
    tmp = tempfile.mkdtemp(prefix="b3cand_")
    results = {}
    jobs = [(c[0], c[1], os.path.join(tmp, "%08X" % c[0]), a.compile) for c in cands]
    with concurrent.futures.ProcessPoolExecutor(a.jobs, initializer=_init, initargs=(a.decomp,)) as ex:
        for addr, st, det in ex.map(_work, jobs, chunksize=4):
            results[addr] = (st, det)
    print("address\ttrack\tname\tsize\tvu0\tleaf\tstatus\tdetail")
    for addr, track, name, size, vu0, leaf in cands:
        st, det = results[addr]
        print("%08X\t%s\t%s\t%d\t%d\t%d\t%s\t%s" % (addr, track, name, size, vu0, int(leaf), st, det.replace("\t", " ")))
    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
