#!/usr/bin/env python3
"""check_readable: convert decomp files with readable.py and check each still compiles the way the original does.

    python3 -I tools/names/check_readable.py [--rev HEAD] [--files LIST | --manifest gen/decomp/manifest.tsv] [--jobs 8]

For each file: readable.py into a scratch tree, then g++ -fsyntax-only (readable.GXX flags) on the original and on the
readable copy (its rewritten headers first on the include path). Pass: the readable copy produces exactly the original's
diagnostics (normally none), with the aliases mapped back and the header comment's line offset removed.
Default file list: the matched units in gen/decomp/manifest.tsv (the ones the port already compiles for the host).
"""
import argparse
import concurrent.futures as cf
import os
import re
import shutil
import subprocess
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
import readable  # noqa: E402


def diags(cmd, roots):
    """(file relative to the first root containing it, line, message) for every error."""
    p = subprocess.run(cmd, capture_output=True, text=True)
    out = []
    for line in p.stderr.splitlines():
        m = readable.DIAG.match(line)
        if m:
            f = os.path.realpath(m.group(1))
            for r in roots:
                if f.startswith(os.path.realpath(r) + os.sep):
                    f = os.path.relpath(f, os.path.realpath(r))
                    break
            out.append((f, int(m.group(2)), m.group(4)))
    return out


def check(snap, rel, rows, scratch):
    out = os.path.join(scratch, re.sub(r"\W", "_", rel))
    u = readable.Unit(snap, rel, rows, {"field", "struct", "function", "global"})
    per_file = u.edits(None)
    u.write(out, per_file, open(os.path.join(snap, ".complete")).read())
    orig = diags(readable.GXX + ["-I", os.path.join(snap, "include"), os.path.join(snap, rel)], [snap])
    new = diags(readable.GXX + ["-I", os.path.join(out, "include"), "-I", os.path.join(snap, "include"),
                                os.path.join(out, rel)], [out, snap])
    back = {r["alias"]: p for (s, c, p), r in u.used.items()}
    hdr = {f: (open(os.path.join(out, f)).read().split("\n\n", 1)[0].count("\n") + 2) for f in per_file}
    hdr.setdefault(rel, open(os.path.join(out, rel)).read().split("\n\n", 1)[0].count("\n") + 2)

    def norm(d, shift):
        f, line, msg = d
        for a, p in back.items():
            msg = re.sub(r"\b%s\b" % re.escape(a), p, msg)
        msg = re.sub(r"; did you mean ‘[^’]*’\?$", "", msg)   # spelling hints depend on the names around
        return f, line - (hdr.get(f, 0) if shift else 0), msg
    ok = Counter(norm(d, False) for d in orig) == Counter(norm(d, True) for d in new)
    shutil.rmtree(out, ignore_errors=True)
    by = Counter((s, r["tag"]) for (s, c, p), r in u.used.items())
    src = {(r["placeholder"], r["alias"]): r["source"] for r in u.used.values()}
    by_source = Counter(src.get(e, "?") for es in per_file.values() for e in es.values())
    return rel, ok, sum(len(e) for e in per_file.values()), by, len(u.skipped), len(orig), \
        [m for m in new if norm(m, True) not in set(norm(d, False) for d in orig)][:3], by_source


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rev", default="HEAD")
    ap.add_argument("--decomp")
    ap.add_argument("--files")
    ap.add_argument("--manifest", default=os.path.join(common.PORT, "gen/decomp/manifest.tsv"))
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--aliases", default=common.ALIASES)
    a = ap.parse_args()
    snap = a.decomp or common.snapshot(a.rev)[0]
    if a.files:
        files = [l.strip() for l in open(a.files) if l.strip()]
    else:
        files = sorted({l.split("\t")[3] for l in open(a.manifest)
                        if l.count("\t") > 3 and l.split("\t")[1] == "matched"})
    rows = common.read_aliases(a.aliases)
    scratch = os.path.join(common.PORT, "build/names/check")
    os.makedirs(scratch, exist_ok=True)
    results = []
    with cf.ProcessPoolExecutor(a.jobs) as ex:
        for r in ex.map(check, [snap] * len(files), files, [rows] * len(files), [scratch] * len(files)):
            results.append(r)
    tot, edits, skipped, per_source = Counter(), 0, 0, Counter()
    bad = [r for r in results if not r[1]]
    for r in results:
        tot.update(r[3])
        per_source.update(r[7])
        edits += r[2]
        skipped += r[4]
    with open(os.path.join(scratch, "results.tsv"), "w") as f:
        f.write("file\tsame_diagnostics\tedits\taliases\tskipped\toriginal_errors\n")
        for r in sorted(results, key=lambda r: -r[2]):
            f.write("%s\t%s\t%d\t%d\t%d\t%d\n" % (r[0], r[1], r[2], sum(r[3].values()), r[4], r[5]))
    print("check_readable: %d files, %d same diagnostics as the original, %d differ; %d identifier edits; %d skipped" % (
        len(results), len(results) - len(bad), len(bad), edits, skipped))
    print("  aliases applied (per file, summed): " + ", ".join("%s %s %d" % (k + (v,)) for k, v in sorted(tot.items())))
    print("  identifier edits per source: " + ", ".join("%s %d" % kv for kv in sorted(per_source.items())))
    for r in bad[:20]:
        print("  DIFF %s: %s" % (r[0], r[6]))
    print("  per file: build/names/check/results.tsv")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
