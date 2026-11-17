#!/usr/bin/env python3
# decl_check.py (boot2, 2026-11-07): every extern "C" function of the port tracks (src/libs/**) must be declared with
# the same parameter / return kinds in every file. Calls between generated port units link directly (decomp2host's
# call_stubs `n_direct`), so a declaration that passes a 32-bit guest word where the definition takes a pointer (or the
# reverse) passes a wrong value silently [C rwastream.cpp's RwaObjIntGetParamPointer(void *, RwUInt32, RwUInt32)
# against rwastreamnodety.cpp's (RwaObj *, RwaObjInterface *, RwUInt32): a segfault in the boot]. Kinds: P pointer /
# reference / function, I integer, F float, L 64/128-bit, V varargs, v void (return). Exit 1 and a list on a mismatch.
#
#   python3 -I tools/native/decl_check.py [--root src/libs]
import argparse, collections, glob, os, re, sys

RX = re.compile(r'extern\s+"C"\s+([^;{()]*?)\b([A-Za-z_]\w*)\s*\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*[;{]', re.S)


def kind(t):
    t = t.strip()
    if not t or t == "void":
        return None
    if "..." in t:
        return "V"
    # a b3host::gp<T> (generated code's guest pointer) is a 4-byte word, passed as a 32-bit integer is
    if re.search(r"\bgp\s*<", t) and not re.search(r">\s*[*&]", t):
        return "I"
    if "*" in t or "&" in t or "(" in t:
        return "P"
    if re.search(r"\b(float|ps2f|RwReal|double)\b", t):
        return "F"
    if re.search(r"\b(long\s+long|RwUInt64|RwInt64|u_long128|u64|s64|std::uint64_t|std::int64_t)\b", t):
        return "L"
    return "I"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="src/libs")
    a = ap.parse_args()
    decl = collections.defaultdict(list)
    for f in sorted(glob.glob(os.path.join(a.root, "**", "*.cpp"), recursive=True)):
        s = re.sub(r"//[^\n]*", "", open(f, errors="replace").read())
        for m in RX.finditer(s):
            ret, name, params = m.group(1), m.group(2), m.group(3)
            ps = [p for p in re.split(r",(?![^()]*\))", params) if p.strip() and p.strip() != "void"]
            sig = (kind(ret) or "v",) + tuple(kind(p) for p in ps)
            decl[name].append(("".join(sig), os.path.relpath(f, a.root), m.group(0).rstrip(";{").strip()))
    bad = 0
    for n, v in sorted(decl.items()):
        if len({s for s, _, _ in v}) > 1:
            bad += 1
            print("%s: %s" % (n, " | ".join("%s %s" % (s, f) for s, f, _ in sorted(set((s, f, "") for s, f, _ in v)))))
    print("decl_check: %d of %d extern \"C\" names declared with different kinds" % (bad, len(decl)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
