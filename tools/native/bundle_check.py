#!/usr/bin/env python3
"""bundle_check -- the no-disc-bytes check of the committed native source bundle (gen/native; mac4, 2026-11-07).

gen/native holds decomp-derived C++ only: the units decomp2host generated from the decomp's C sources and the port
tracks, their headers, the manifest, and the function table (names, addresses, sizes, code hashes). This checks that
nothing in it is a copy of disc data:

  1. every file is text (UTF-8, no NUL byte) with a source / table extension;
  2. no ELF magic, no `.incbin`, no `#embed`;
  3. `.word` directives (raw instruction words in asm): counted; a run of more than kWordRun lines is an error;
  4. string literals with more than kEscRun `\\x` escapes in a row, and hex digit runs longer than kHexRun: errors;
  5. brace initialisers of kListMin or more numeric literals: counted, the longest reported; with --elf, each one is
     packed as bytes (all values < 256), 32-bit words and 32-bit floats and searched in the retail executable: a
     match of kElfMatch or more elements is an error (data copied out of the ELF), shorter matches are listed.

  python3 -I tools/native/bundle_check.py [--elf <disc>/SLUS_210.50] [gen/native]
Exit 0: clean; 1: a finding; 2: usage.
"""
import argparse
import os
import re
import struct
import sys

kExt = {".cpp", ".h", ".tsv", ".txt", ".md"}
kNames = {"SNAPSHOT"}   # boot2: the bundle's provenance line (native-bundle writes it; one text line, checked below like the rest)
kWordRun = 8
kEscRun = 32
kHexRun = 64
kListMin = 16
kElfMatch = 64

NUM = r"[-+]?(?:0[xX][0-9a-fA-F]+|\d+\.\d*(?:[eE][-+]?\d+)?f?|\d*\.\d+(?:[eE][-+]?\d+)?f?|\d+(?:[eE][-+]?\d+)?f?)[uUlL]*"
LIST_RE = re.compile(r"\{\s*(" + NUM + r"(?:\s*,\s*" + NUM + r")*)\s*,?\s*\}")
NUM_RE = re.compile(NUM)
ESC_RE = re.compile(r"(?:\\x[0-9a-fA-F]{1,2}){%d,}" % (kEscRun + 1))
HEX_RE = re.compile(r"[0-9a-fA-F]{%d,}" % (kHexRun + 1))


def value(tok):
    t = tok.rstrip("uUlL")
    if re.fullmatch(r"[-+]?0[xX][0-9a-fA-F]+", t) or re.fullmatch(r"[-+]?\d+", t):
        return int(t, 0)
    return float(t.rstrip("fF"))


def packings(vals):
    out = []
    if all(isinstance(v, int) for v in vals):
        if all(0 <= v < 256 for v in vals):
            out.append(("u8", bytes(vals)))
        if all(-0x80000000 <= v <= 0xFFFFFFFF for v in vals):
            out.append(("u32", b"".join(struct.pack("<I", v & 0xFFFFFFFF) for v in vals)))
    try:
        out.append(("f32", b"".join(struct.pack("<f", float(v)) for v in vals)))
    except (OverflowError, struct.error):
        pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default="gen/native")
    ap.add_argument("--elf", default="")
    a = ap.parse_args()
    if not os.path.isdir(a.root):
        print("bundle_check: no directory %s" % a.root)
        return 2
    elf = open(a.elf, "rb").read() if a.elf else None
    errors, notes = [], []
    files = nbytes = words = lists = 0
    longest = (0, "")
    elf_hits = []
    for dp, _, fns in os.walk(a.root):
        for fn in sorted(fns):
            p = os.path.join(dp, fn)
            rel = os.path.relpath(p, a.root)
            files += 1
            raw = open(p, "rb").read()
            nbytes += len(raw)
            if os.path.splitext(fn)[1] not in kExt and fn not in kNames:   # boot2: the recipe's own SNAPSHOT
                errors.append("%s: not a source / table file" % rel)
            if b"\0" in raw:
                errors.append("%s: NUL byte (binary)" % rel)
                continue
            try:
                s = raw.decode("utf-8")
            except UnicodeDecodeError:
                errors.append("%s: not UTF-8" % rel)
                continue
            if "\x7fELF" in s or "\\x7fELF" in s or "\\177ELF" in s:
                errors.append("%s: ELF magic" % rel)
            if ".incbin" in s or "#embed" in s:
                errors.append("%s: .incbin / #embed" % rel)
            run = 0
            for line in s.split("\n"):
                if ".word" in line:
                    words += 1
                    run += 1
                    if run == kWordRun + 1:
                        errors.append("%s: more than %d .word lines in a row" % (rel, kWordRun))
                else:
                    run = 0
            for m in ESC_RE.finditer(s):
                errors.append("%s: a string of %d+ \\x escapes" % (rel, kEscRun + 1))
                break
            for m in HEX_RE.finditer(s):
                errors.append("%s: a hex run of %d digits" % (rel, len(m.group(0))))
                break
            for m in LIST_RE.finditer(s):
                toks = NUM_RE.findall(m.group(1))
                if len(toks) < kListMin:
                    continue
                lists += 1
                if len(toks) > longest[0]:
                    longest = (len(toks), rel)
                if elf is None:
                    continue
                vals = [value(t) for t in toks]
                for kind, blob in packings(vals):
                    if blob in elf:
                        line = s.count("\n", 0, m.start()) + 1
                        elf_hits.append((len(toks), kind, "%s:%d" % (rel, line), elf.find(blob)))
                        break
    for n, kind, where, off in sorted(elf_hits, reverse=True):
        (errors if n >= kElfMatch else notes).append(
            "%s: a %d-element list found in the ELF as %s (file offset 0x%X)" % (where, n, kind, off))
    print("bundle_check %s: %d files, %.1f MB" % (a.root, files, nbytes / 1e6))
    print("  .word lines: %d (no run over %d)" % (words, kWordRun) if not any(".word" in e for e in errors) else
          "  .word lines: %d" % words)
    print("  numeric initialisers of %d+ literals: %d, the longest %d (%s)" % (kListMin, lists, longest[0], longest[1]))
    print("  searched in the ELF: %s; matches: %d (error from %d elements)" %
          ("yes" if elf is not None else "no (no --elf)", len(elf_hits), kElfMatch))
    for n in notes:
        print("  note: " + n)
    for e in errors:
        print("  ERROR: " + e)
    print("  verdict: %s" % ("CLEAN" if not errors else "%d finding(s)" % len(errors)))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
