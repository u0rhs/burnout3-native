#!/usr/bin/env python3
"""host_check: does a readable file build for the host exactly like the original? (docs/names-layer.md)

    python3 -I tools/names/host_check.py <decomp path> [--rev HEAD] [--scopes field,struct]

Runs the port's own host pipeline twice on the unit's selected functions (config/game_overrides.txt rows whose source is
the file, via gen/decomp/manifest.tsv): tools/decomp2host on the decomp snapshot, and on a copy of it where the file
(and the headers readable.py rewrote) are the readable versions. Both generated units are compiled with the Makefile's
generated-unit flags (PS2-faithful floats, -O2, no debug info), then disassembled with relocations (objdump -d -r -C);
with the aliases mapped back to their placeholders the two listings must be identical.
Default scopes field,struct: function and global aliases change the linkage names decomp2host resolves to guest
addresses (T4, call stubs), which it cannot map back yet (docs/names-layer.md, open questions).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
import readable  # noqa: E402

PORT = common.PORT
FLAGS = ["-Iinclude", "-Iinclude/gtmath", "-std=c++20", "-ffp-contract=off", "-fno-fast-math", "-msse2", "-O2", "-g0",
         "-fno-strict-aliasing", "-fno-aggressive-loop-optimizations", "-fstrict-flex-arrays=0", "-fno-access-control", "-fpermissive", "-w",
         "-DB3_PS2_FLOAT=1"]


def unalias(text, back):
    """Map alias names back to placeholders in a listing: plain identifiers, and the <length><name> form of mangled
    section names (objdump -C demangles symbols, not section names)."""
    for alias, p in sorted(back.items(), key=lambda x: -len(x[0])):
        text = text.replace("%d%s" % (len(alias), alias), "%d%s" % (len(p), p))
        text = re.sub(r"\b%s\b" % re.escape(alias), p, text)
    return text


def sections(listing):
    """A listing as its sorted per-section blocks: COMDAT sections (inline functions, templates) may come out in
    another order when names change; their contents may not."""
    return sorted(listing.split("\nDisassembly of section "))


def run(cmd, **kw):
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=PORT, **kw)
    if p.returncode:
        raise SystemExit("host_check: %s failed:\n%s%s" % (" ".join(cmd[:4]), p.stdout[-2000:], p.stderr[-4000:]))
    return p.stdout


def copy_tree(src, dst):
    """Hard-link copy (cheap); files that will be replaced are unlinked first so the snapshot is never written."""
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst, copy_function=os.link)


def tree_check(snap, rd, jobs):
    """Every unit of config/game_overrides.txt, original vs a tree where readable.py --tree output (rd) replaces files."""
    import concurrent.futures as cf
    import json
    work = os.path.join(PORT, "build/names/hostcheck_tree")
    os.makedirs(work, exist_ok=True)
    tree = os.path.join(work, "tree")
    copy_tree(snap, tree)
    n = 0
    for dp, _, fs in os.walk(rd):
        for f in fs:
            rel = os.path.relpath(os.path.join(dp, f), rd)
            if rel == "struct_aliases.json":
                continue
            os.remove(os.path.join(tree, rel))
            shutil.copyfile(os.path.join(dp, f), os.path.join(tree, rel))
            n += 1
    used = json.load(open(os.path.join(rd, "struct_aliases.json")))
    sel = os.path.join(work, "select.txt")
    with open(sel, "w") as f:   # gen/decomp's functions whose source exists at this snapshot
        for l in open(os.path.join(PORT, "gen/decomp/manifest.tsv")):
            c = l.split("\t")
            if len(c) > 3 and c[0] != "address" and os.path.exists(os.path.join(snap, c[3])):
                f.write("%s %s\n" % (c[0], c[1]))
    outs = {}
    for tag, root in (("original", snap), ("readable", tree)):
        out = os.path.join(work, "gen_" + tag)
        if os.path.exists(out):
            shutil.rmtree(out)
        run(["python3", "-I", "tools/decomp2host/decomp2host.py", "--decomp", root, "--out", out,
             "--select", sel, "--keep-going"])
        outs[tag] = out
    src_of = {}
    for l in open(os.path.join(outs["original"], "manifest.tsv")):
        c = l.rstrip("\n").split("\t")
        if len(c) > 3 and c[0] != "address":
            src_of[os.path.splitext(os.path.basename(c[3]))[0]] = c[3]
    units = sorted(os.listdir(os.path.join(outs["original"], "units")))
    jobs_list = [(tag, outs[tag], u, work) for u in units for tag in outs]
    with cf.ProcessPoolExecutor(jobs) as ex:
        dis = dict(zip([(j[0], j[2]) for j in jobs_list], ex.map(disassemble, jobs_list)))
    same, differ, renamed = 0, [], 0
    for u in units:
        back = used.get(src_of.get(u[:-4], ""), {})
        a, b = dis[("original", u)], dis[("readable", u)]
        if back:
            renamed += 1
        b = unalias(b, back)
        if sections(a) == sections(b):
            same += 1
        else:
            differ.append(u)
    print("host_check --tree: %d readable files in the tree; %d generated units: %d identical host code, %d differ" % (
        n, len(units), same, len(differ)))
    for u in differ[:20]:
        print("  DIFF " + u)
    return 1 if differ else 0


def disassemble(job):
    tag, out, unit, work = job
    obj = os.path.join(work, "%s_%s.o" % (tag, unit[:-4]))
    run(["g++"] + FLAGS + ["-I" + os.path.join(out, "include"), "-c", os.path.join(out, "units", unit), "-o", obj])
    dis = run(["objdump", "-d", "-r", "-C", "--no-show-raw-insn", obj])
    return re.sub(r"^.*file format.*$", "", dis, flags=re.M)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file", nargs="?")
    ap.add_argument("--tree", help="a readable.py --tree output directory (field,struct scopes): check every unit")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--rev", default="HEAD")
    ap.add_argument("--decomp")
    ap.add_argument("--scopes", default="field,struct")
    a = ap.parse_args()
    snap = a.decomp or common.snapshot(a.rev)[0]
    if a.tree:
        return tree_check(snap, a.tree, a.jobs)
    commit = open(os.path.join(snap, ".complete")).read().strip()
    sel = [l.split("\t")[0] for l in open(os.path.join(PORT, "gen/decomp/manifest.tsv"))
           if l.count("\t") > 3 and l.split("\t")[3] == a.file]
    if not sel:
        raise SystemExit("host_check: %s has no function in gen/decomp/manifest.tsv" % a.file)
    work = os.path.join(PORT, "build/names/hostcheck")
    os.makedirs(work, exist_ok=True)
    open(os.path.join(work, "select.txt"), "w").write("".join("%s matched\n" % s for s in sel))

    scopes = set(a.scopes.split(","))
    u = readable.Unit(snap, a.file, common.read_aliases(), scopes)
    per_file = u.edits(scopes)
    rd = os.path.join(work, "readable_out")
    if os.path.exists(rd):
        shutil.rmtree(rd)
    written = u.write(rd, per_file, commit)
    tree = os.path.join(work, "tree")
    copy_tree(snap, tree)
    for w in written:
        rel = os.path.relpath(w, rd)
        os.remove(os.path.join(tree, rel))
        shutil.copyfile(w, os.path.join(tree, rel))

    listings = {}
    for tag, root in (("original", snap), ("readable", tree)):
        out = os.path.join(work, "gen_" + tag)
        if os.path.exists(out):
            shutil.rmtree(out)
        run(["python3", "-I", "tools/decomp2host/decomp2host.py", "--decomp", root, "--out", out,
             "--select", os.path.join(work, "select.txt")])
        units = sorted(os.listdir(os.path.join(out, "units")))
        text = []
        for unit in units:
            obj = os.path.join(work, "%s_%s.o" % (tag, unit[:-4]))
            run(["g++"] + FLAGS + ["-I" + os.path.join(out, "include"), "-c", os.path.join(out, "units", unit),
                                   "-o", obj])
            dis = run(["objdump", "-d", "-r", "-C", "--no-show-raw-insn", obj])
            text.append(re.sub(r"^.*file format.*$", "", dis, flags=re.M))
        listings[tag] = "\n".join(text)
    back = sorted(((r["alias"], p) for (s, c, p), r in u.used.items()), key=lambda x: -len(x[0]))
    norm = unalias(listings["readable"], dict(back))
    same = sections(norm) == sections(listings["original"])
    for tag, t in (("original", listings["original"]), ("readable", norm)):
        open(os.path.join(work, tag + ".dis"), "w").write(t)
    n_insn = sum(1 for l in listings["original"].splitlines() if re.match(r"^\s+[0-9a-f]+:\t", l))
    print("host_check: %s (%d function(s) %s): %d aliases (%s), host code %s (%d instructions; listings in %s)" % (
        a.file, len(sel), " ".join(sel), len(u.used), ",".join(sorted(scopes)),
        "IDENTICAL" if same else "DIFFERS", n_insn, os.path.relpath(work, PORT)))
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())
