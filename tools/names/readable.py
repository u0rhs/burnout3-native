#!/usr/bin/env python3
"""readable: one decomp source file with the alias layer applied (docs/names-layer.md).

    python3 -I tools/names/readable.py <decomp path, e.g. src/b3/.../X.cpp> [--rev HEAD | --decomp SNAPDIR]
            [--aliases config/aliases.tsv] [--out gen/readable] [--scopes field,struct,function,global]

Writes OUT/<same path> for the file and OUT/<header path> for each decomp header it includes that changes. Only
identifiers are renamed (never comments, string or char literals), and each written file starts with a comment listing
every alias it uses with its tag.

Renames:
  struct / function / global  every token of the placeholder in the file and its headers (the alias's home file must be
                              one of them for a view type)
  field                       member placeholders are resolved per aggregate by the compiler itself: a probe copy has
                              every declaration of the name made unique per aggregate (field_X -> field_X__R<k>), and
                              g++'s diagnostics for the uses ("'struct S' has no member named 'field_X'; did you mean
                              'field_X__R3'?") say which aggregate each use belongs to. Resolved uses are fed back and
                              the probe recompiled until nothing new resolves (chains like a->field_8->field_4).
Safety: an alias that is already an identifier anywhere the file sees is not used (no shadowing, no clash); a member
name with any use the probe could not resolve is left alone in the whole file; GtMath headers (the port's shadow
headers replace them) are never rewritten. A pre-existing g++ error in the original is fine (only new diagnostics count).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

SHADOW = {"include/b3/GtMath.h", "include/b3/GtMathPs2.h", "include/b3/GtMathPs2Quat.h", "include/b3/GtMathPs2Trig.h"}
GXX = ["g++", "-std=c++20", "-fsyntax-only", "-fpermissive", "-w", "-fmax-errors=0", "-fdiagnostics-column-unit=byte",
       "-fno-diagnostics-show-caret", "-fno-diagnostics-color"]
DIAG = re.compile(r"^(.*?):(\d+):(\d+): error: (.*)$")
MARK = "__R"


def lines_index(text):
    starts = [0]
    for i, c in enumerate(text):
        if c == "\n":
            starts.append(i + 1)
    return starts


def rewrite(text, edits):
    """edits {pos: (old, new)} -> (new text, probe->original position map as sorted [(new_pos, orig_pos)])."""
    out, anchors, last, delta = [], [], 0, 0
    for p in sorted(edits):
        old, new = edits[p]
        out.append(text[last:p])
        anchors.append((p + delta, p))
        out.append(new)
        last = p + len(old)
        delta += len(new) - len(old)
        anchors.append((p + delta + len(old), last))  # after the token
    out.append(text[last:])
    return "".join(out), anchors


def to_orig(anchors, pos):
    """A probe position -> the original position (exact at token starts and outside edited tokens)."""
    best = (0, 0)
    for a in anchors:
        if a[0] <= pos:
            best = a
        else:
            break
    return best[1] + (pos - best[0])


class Unit:
    def __init__(self, snap, rel, rows, scopes, exclude=frozenset()):
        """exclude: keys never applied here, ('t', placeholder) for token aliases and ('m', aggregate, placeholder) for
        members (tree mode: what another unit cannot rename in a shared header)."""
        self.snap, self.rel = snap, rel
        self.keys = {}      # (file, pos) -> the key of the alias applied there
        self.files = [f for f in common.include_closure(snap, rel)]
        self.text = {f: open(os.path.join(snap, f), errors="replace").read() for f in self.files}
        self.masked = {f: common.mask(t) for f, t in self.text.items()}
        self.toks = {f: common.tokens(m) for f, m in self.masked.items()}
        self.idents = {t for f in self.files for _, t in self.toks[f]}
        self.skipped = []   # (placeholder, alias, reason)
        self.used = {}      # (scope, container, placeholder) -> row
        files = set(self.files)
        self.token_alias = {}
        claimed = defaultdict(list)
        for r in rows:
            if r["scope"] not in scopes or r["placeholder"] not in self.idents:
                continue
            if r["scope"] == "field":
                continue
            if r["scope"] == "struct" and r["file"] not in files:
                continue
            if ("t", r["placeholder"]) in exclude:
                continue
            claimed[r["alias"]].append(r)
        for alias, rs in claimed.items():
            if alias in self.idents:
                for r in rs:
                    self.skipped.append((r["placeholder"], alias, "alias already an identifier here"))
            elif len(rs) > 1:
                for r in rs:
                    self.skipped.append((r["placeholder"], alias, "alias claimed by %d placeholders here" % len(rs)))
            else:
                self.token_alias[rs[0]["placeholder"]] = rs[0]
        # members
        self.aggs = []   # (file, name, body_start, body_end, members)
        for f in self.files:
            for name, b, e, members in common.aggregates(self.masked[f]):
                self.aggs.append((f, name, b, e, members))
        self.member_rows = {}   # (aggregate name, placeholder) -> row
        if "field" in scopes:
            names = defaultdict(int)
            for a in self.aggs:
                names[a[1]] += 1
            for r in rows:
                if r["scope"] != "field" or r["placeholder"] not in self.idents:
                    continue
                if r["file"] != "*" and r["file"] not in files:
                    continue
                if names.get(r["container"], 0) == 0 or ("m", r["container"], r["placeholder"]) in exclude:
                    continue
                if names[r["container"]] > 1:
                    self.skipped.append((r["placeholder"], r["alias"], "%s defined %d times here" % (
                        r["container"], names[r["container"]])))
                    continue
                if r["alias"] in self.idents:
                    self.skipped.append((r["placeholder"], r["alias"], "alias already an identifier here"))
                    continue
                self.member_rows[(r["container"], r["placeholder"])] = r
            self.override_guard()

    def override_guard(self):
        """A virtual slot (vfunc_XX) declared by a class and by one of its bases or derived classes here must get one
        alias in both or in neither: renaming one side only would turn an override into a new virtual."""
        bases = defaultdict(set)
        for f in self.files:
            for n, bs in common.bases(self.masked[f]).items():
                bases[n] |= bs
        declares = defaultdict(set)
        for f, n, b, e, members in self.aggs:
            for t in members:
                if t.startswith("vfunc_"):
                    declares[t].add(n)
        related = defaultdict(set)
        for d, bs in bases.items():
            for b in bs:
                related[d].add(b)
                related[b].add(d)
        changed = True
        while changed:
            changed = False
            for (c, p), r in list(self.member_rows.items()):
                if not p.startswith("vfunc_"):
                    continue
                seen, todo = {c}, [c]
                while todo:
                    for n in related[todo.pop()]:
                        if n not in seen:
                            seen.add(n)
                            todo.append(n)
                for o in seen & declares[p]:
                    other = self.member_rows.get((o, p))
                    if other is None or other["alias"] != r["alias"]:
                        for k in [(x, p) for x in seen if (x, p) in self.member_rows]:
                            self.skipped.append((p, self.member_rows.pop(k)["alias"], "override in %s without the same "
                                                 "alias" % o))
                        changed = True
                        break

    # ------------------------------------------------------------ member resolution through g++
    def resolve_members(self):
        names = {p for _, p in self.member_rows}
        if not names:
            return {}, {}, {}, {}
        decl = {}    # (file, pos) -> aggregate index, for every declaration of a candidate name
        for k, (f, n, b, e, members) in enumerate(self.aggs):
            for tok, poss in members.items():
                if tok in names:
                    for p in poss:
                        decl[(f, p)] = k
        uses = {}    # (file, pos) -> name, every non-declaration token of a candidate name
        for f in self.files:
            for p, t in self.toks[f]:
                if t in names and (f, p) not in decl:
                    uses[(f, p)] = t
        resolved = {}   # (file, pos) -> aggregate index
        tmp = tempfile.mkdtemp(prefix="readable-", dir=os.path.join(common.PORT, "build", "names"))
        try:
            # errors the unchanged original already has are not evidence (a member whose declaration failed, ...)
            os.makedirs(os.path.join(tmp, "base"), exist_ok=True)
            for f in self.files:
                os.makedirs(os.path.dirname(os.path.join(tmp, "base", f)), exist_ok=True)
                shutil.copyfile(os.path.join(self.snap, f), os.path.join(tmp, "base", f))
            baseline = set()
            for path, line, col, msg in self.compile(os.path.join(tmp, "base")):
                f = os.path.relpath(os.path.realpath(path), os.path.realpath(os.path.join(tmp, "base")))
                if f in self.text:
                    baseline.add((f, lines_index(self.text[f])[line - 1] + col - 1))
            self.baseline_errors = len(baseline)
            for rnd in range(10):
                anchors = {}
                for f in self.files:
                    edits = {}
                    for (g, p), k in decl.items():
                        if g == f:
                            tok = re.match(r"\w+", self.text[f][p:]).group(0)
                            edits[p] = (tok, "%s%s%d" % (tok, MARK, k))
                    for (g, p), k in resolved.items():
                        if g == f:
                            tok = uses[(g, p)]
                            edits[p] = (tok, "%s%s%d" % (tok, MARK, k))
                    new, anchors[f] = rewrite(self.text[f], edits)
                    os.makedirs(os.path.dirname(os.path.join(tmp, "probe", f)), exist_ok=True)
                    open(os.path.join(tmp, "probe", f), "w").write(new)
                    anchors[f] = (anchors[f], lines_index(new))
                errs = self.compile(os.path.join(tmp, "probe"))
                new_res = 0
                for path, line, col, msg in errs:
                    f = os.path.relpath(os.path.realpath(path), os.path.realpath(os.path.join(tmp, "probe")))
                    if f not in anchors:
                        continue
                    anc, starts = anchors[f]
                    if line - 1 >= len(starts):
                        continue
                    pos = to_orig(anc, starts[line - 1] + col - 1)
                    if (f, pos) not in uses or (f, pos) in resolved or (f, pos) in baseline:
                        continue
                    k = self.agg_of(msg, uses[(f, pos)], f, pos)
                    if k is not None:
                        resolved[(f, pos)] = k
                        new_res += 1
                if new_res == 0:
                    break
        finally:
            if os.environ.get("READABLE_KEEP_PROBE"):
                sys.stderr.write("readable: probe kept in %s\n" % tmp)
            else:
                shutil.rmtree(tmp, ignore_errors=True)
        unresolved = defaultdict(int)
        for key, t in uses.items():
            if key not in resolved:
                unresolved[t] += 1
        return decl, resolved, uses, unresolved

    def agg_of(self, msg, name, f, pos):
        m = re.search(r"did you mean ‘%s%s(\d+)’" % (re.escape(name), MARK), msg)
        if m:
            return int(m.group(1))
        m = re.search(r"‘(?:const |volatile )*(?:struct |class |union )?([\w:]+)’(?: \{aka ‘(?:const |volatile )*"
                      r"(?:struct |class |union )?([\w:]+)’\})? has no member named ‘%s’" % re.escape(name), msg)
        if m:
            tname = (m.group(2) or m.group(1)).split("::")[-1]
            ks = [k for k, a in enumerate(self.aggs) if a[1] == tname and name in a[4]]
            return ks[0] if len(ks) == 1 else None
        if "was not declared in this scope" in msg:
            # a bare member name: inside an inline method of an aggregate, or an out-of-line `Class::Method` body
            ks = [k for k, a in enumerate(self.aggs) if a[0] == f and a[2] < pos < a[3] and name in a[4]]
            if ks:
                return ks[-1]
            for cls, b, e in self.methods(f):
                if b < pos < e:
                    ks = [k for k, a in enumerate(self.aggs) if a[1] == cls and name in a[4]]
                    return ks[0] if len(ks) == 1 else None
        return None

    METHOD = re.compile(r"\b([A-Za-z_]\w*)\s*::\s*~?[A-Za-z_]\w*\s*\([^;{}]*\)\s*(?:const\s*)?\{")

    def methods(self, f):
        """Out-of-line member function bodies at file scope: [(class, body start, body end)]."""
        if not hasattr(self, "_methods"):
            self._methods = {}
        if f not in self._methods:
            m = self.masked[f]
            out = []
            for x in self.METHOD.finditer(m):
                b = x.end() - 1
                if m.count("{", 0, b) - m.count("}", 0, b) == 0:
                    out.append((x.group(1), b, common.match_brace(m, b)))
            self._methods[f] = out
        return self._methods[f]

    def compile(self, root):
        cmd = GXX + ["-I", os.path.join(root, "include"), "-I", os.path.join(self.snap, "include"),
                     os.path.join(root, self.rel)]
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=root)
        out = []
        for line in p.stderr.splitlines():
            m = DIAG.match(line)
            if m:
                out.append((os.path.join(root, m.group(1)) if not os.path.isabs(m.group(1)) else m.group(1),
                            int(m.group(2)), int(m.group(3)), m.group(4)))
        return out

    # ------------------------------------------------------------ output
    def edits(self, scopes):
        per_file = defaultdict(dict)
        for f in self.files:
            for p, t in self.toks[f]:
                r = self.token_alias.get(t)
                if r:
                    per_file[f][p] = (t, r["alias"])
                    self.keys[(f, p)] = ("t", t)
                    self.used[(r["scope"], r["container"], t)] = r
        if self.member_rows:
            decl, resolved, uses, unresolved = self.resolve_members()
            unsafe = {t for t, n in unresolved.items() if n}
            for t in sorted(unsafe):
                for (c, p), r in self.member_rows.items():
                    if p == t:
                        self.skipped.append((t, r["alias"], "%d use(s) of %s the probe could not resolve" % (
                            unresolved[t], t)))
            for (f, p), k in list(decl.items()) + list(resolved.items()):
                name = self.aggs[k][1]
                tok = re.match(r"\w+", self.text[f][p:]).group(0)
                r = self.member_rows.get((name, tok))
                if r and tok not in unsafe:
                    per_file[f][p] = (tok, r["alias"])
                    self.keys[(f, p)] = ("m", name, tok)
                    self.used[("field", name, tok)] = r
        for f in list(per_file):
            if f in SHADOW:
                raise SystemExit("readable: %s would change a GtMath header (port shadow header): %s" % (
                    self.rel, sorted(set(n for _, n in per_file[f].values()))))
        return per_file

    def write(self, out, per_file, commit):
        written = []
        for f in self.files:
            if f != self.rel and f not in per_file:
                continue
            new, _ = rewrite(self.text[f], per_file.get(f, {}))
            here = [r for (s, c, p), r in sorted(self.used.items())
                    if any(o == p for o, _ in per_file.get(f, {}).values())]
            head = ["// READABLE VIEW generated by tools/names/readable.py from burnout3-retail-decomp %s:%s" % (
                        commit[:10], f),
                    "// Port-only names (docs/names-layer.md); evidence per row in config/aliases.tsv. Do not edit.",
                    "// Aliases used here (%d):" % len(here)]
            for r in here:
                where = r["container"] if r["scope"] == "field" else r["scope"]
                head.append("//   %-22s -> %-30s %s %-12s %s" % (r["placeholder"], r["alias"], r["tag"], r["source"],
                                                               "" if where == "-" else "(%s)" % where))
            dst = os.path.join(out, f)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, "w").write("\n".join(head) + "\n\n" + new)
            written.append(dst)
        return written


def unit_pass(args):
    snap, rel, rows, scopes, exclude = args
    u = Unit(snap, rel, rows, scopes, exclude)
    per_file = u.edits(scopes)
    headers = {f: frozenset((p, n, u.keys[(f, p)]) for p, (o, n) in e.items()) for f, e in per_file.items() if f != rel}
    return rel, u, per_file, headers, [f for f in u.files if f != rel]


def tree(snap, files, rows, scopes, out, commit, jobs):
    """Every file in `files`, with shared headers rewritten once: a header edit is kept only if every unit that sees
    the header makes it; the aliases behind the others are excluded everywhere and the units redone (until stable)."""
    import concurrent.futures as cf
    exclude = frozenset()
    for rnd in range(4):
        with cf.ProcessPoolExecutor(jobs) as ex:
            res = list(ex.map(unit_pass, [(snap, f, rows, scopes, exclude) for f in files]))
        seen = defaultdict(list)   # header -> [edit set per unit that includes it]
        for rel, u, per_file, headers, incs in res:
            for h in incs:
                seen[h].append(headers.get(h, frozenset()))
        bad = set()
        for h, sets in seen.items():
            agreed = frozenset.intersection(*sets)
            for s in sets:
                bad |= {k for p, n, k in s - agreed}
        if not bad:
            break
        exclude = exclude | frozenset(bad)
    else:
        raise SystemExit("readable: tree mode did not settle")
    written, used, header_done = [], {}, set()
    for rel, u, per_file, headers, incs in res:
        mine = {f: e for f, e in per_file.items() if f == rel or f not in header_done}
        written += u.write(out, mine, commit)
        header_done |= set(per_file) - {rel}
        # names that can reach the object code: view types (mangled names) and virtual slots (vfunc_XX: mangled names
        # of a devirtualised or defined override)
        used[rel] = {r["alias"]: p for (s, c, p), r in u.used.items() if s == "struct" or p.startswith("vfunc_")}
    return res, written, exclude, used


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file", nargs="?")
    ap.add_argument("--tree", help="a file listing decomp paths: convert them all (shared headers once)")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--rev", default="HEAD")
    ap.add_argument("--decomp", help="an existing snapshot directory (default: snapshot --rev)")
    ap.add_argument("--aliases", default=common.ALIASES)
    ap.add_argument("--out", default=os.path.join(common.PORT, "gen", "readable"))
    ap.add_argument("--scopes", default="field,struct,function,global")
    a = ap.parse_args()
    snap = a.decomp or common.snapshot(a.rev)[0]
    commit = open(os.path.join(snap, ".complete")).read().strip()
    scopes = set(a.scopes.split(","))
    rows = common.read_aliases(a.aliases)
    if a.tree:
        files = [l.strip() for l in open(a.tree) if l.strip() and not l.startswith("#")]
        res, written, exclude, used = tree(snap, files, rows, scopes, a.out, commit, a.jobs)
        import json
        json.dump(used, open(os.path.join(a.out, "struct_aliases.json"), "w"), indent=0, sort_keys=True)
        n_edits = sum(len(e) for r in res for e in r[2].values())
        print("readable: %d files -> %d written under %s, %d identifier edits; %d aliases excluded for shared-header "
              "consistency" % (len(files), len(written), os.path.relpath(a.out, common.PORT), n_edits, len(exclude)))
        return
    u = Unit(snap, a.file, rows, scopes)
    per_file = u.edits(scopes)
    written = u.write(a.out, per_file, commit)
    by = defaultdict(int)
    for (s, c, p), r in u.used.items():
        by[(s, r["tag"])] += 1
    n_edits = sum(len(e) for e in per_file.values())
    print("readable: %s -> %d file(s), %d identifier edits, %d aliases (%s); %d skipped" % (
        a.file, len(written), n_edits, len(u.used), ", ".join("%s %s %d" % (s, t, n) for (s, t), n in sorted(by.items())),
        len(u.skipped)))
    for w in written:
        print("  " + os.path.relpath(w, common.PORT))
    for s in u.skipped:
        print("  skipped %s -> %s: %s" % s)


if __name__ == "__main__":
    main()
