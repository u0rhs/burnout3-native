"""Shared helpers for the names layer (docs/names-layer.md): decomp snapshot, lexing, struct parsing, alias table I/O."""
import bisect
import csv
import os
import re
import subprocess

PORT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DECOMP_REPO = os.path.join(os.path.dirname(PORT), "burnout3-retail-decomp")
ALIASES = os.path.join(PORT, "config", "aliases.tsv")

# What a snapshot needs: sources + headers, the name tables, the symbol file (and, for the decomp2host check,
# assembly/asm, config/proven_units.txt and the retail ELF).
SNAP_PATHS = ["src", "include", "config", "research/names", "research/proto/new_names.csv",
              "research/retail_data_names.csv", "assembly/asm", "orig/SLUS_210.50"]

TIER_RANK = {"[C]": 0, "[L]": 1, "[G]": 2}
COLUMNS = ["scope", "file", "container", "placeholder", "alias", "tag", "source", "evidence"]

PLACEHOLDER = re.compile(r"^(func_[0-9A-Fa-f]{6,8}|UnkStruct_\w+|field_[0-9A-Fa-f]+|D_[0-9A-Fa-f]{8}|vfunc_[0-9A-Fa-f]+"
                         r"|UnkEnum_[0-9A-Fa-f]+)$")
IDENT = re.compile(r"[A-Za-z_]\w*")


def weaker(a, b):
    return a if TIER_RANK[a] >= TIER_RANK[b] else b


def snapshot(rev="HEAD", repo=DECOMP_REPO):
    """A read-only copy of the decomp's committed `rev` (never its working tree: other agents edit it), under
    build/names/snap-<rev>. Returns (dir, full commit hash)."""
    full = subprocess.run(["git", "-C", repo, "rev-parse", rev], capture_output=True, text=True, check=True).stdout.strip()
    d = os.path.join(PORT, "build", "names", "snap-" + full[:10])
    if not os.path.exists(os.path.join(d, ".complete")):
        os.makedirs(d, exist_ok=True)
        arch = subprocess.Popen(["git", "-C", repo, "archive", full] + SNAP_PATHS, stdout=subprocess.PIPE)
        subprocess.run(["tar", "-x", "-C", d], stdin=arch.stdout, check=True)
        if arch.wait():
            raise SystemExit("git archive failed")
        open(os.path.join(d, ".complete"), "w").write(full + "\n")
    return d, full


def mask(text):
    """Comments, string and char literals replaced by spaces (newlines kept): offsets stay valid, and nothing inside a
    comment or literal is ever seen as a token. Preprocessor lines are kept (an #include path is a string)."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif text[i] in "\"'":
            q, j = text[i], i + 1
            while j < n and text[j] != q and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j += 1
        else:
            i += 1
            continue
        for k in range(i, min(j, n)):
            if out[k] != "\n":
                out[k] = " "
        i = j
    return "".join(out)


def tokens(masked):
    """(start, identifier) for every identifier outside comments/literals."""
    return [(m.start(), m.group(0)) for m in IDENT.finditer(masked)
            if m.start() == 0 or not (masked[m.start() - 1].isalnum() or masked[m.start() - 1] == "_")]


AGG_HEAD = re.compile(r"\b(struct|class|union)\s+([A-Za-z_]\w*)\s*(?:final\s*)?(?::[^;{}()]*)?\{")


def match_brace(m, i):
    depth = 0
    for j in range(i, len(m)):
        if m[j] == "{":
            depth += 1
        elif m[j] == "}":
            depth -= 1
            if depth == 0:
                return j
    return len(m) - 1


def aggregates(masked):
    """Aggregate definitions: [(name, body_start, body_end, members)], members = {identifier: [offsets]} for the
    identifiers declared at the body's own depth (not inside inline method bodies or nested aggregates)."""
    depth, d = [0] * (len(masked) + 1), 0
    for i, c in enumerate(masked):
        if c == "{":
            d += 1
        depth[i] = d
        if c == "}":
            d -= 1
    toks = tokens(masked)
    starts = [p for p, _ in toks]
    out = []
    for m in AGG_HEAD.finditer(masked):
        b = m.end() - 1
        e = match_brace(masked, b)
        members = {}
        for p, tok in toks[bisect.bisect_right(starts, b):bisect.bisect_left(starts, e)]:
            if depth[p] == depth[b]:
                members.setdefault(tok, []).append(p)
        out.append((m.group(2), b, e, members))
    return out


def bases(masked):
    """{aggregate name: its direct base classes} from every definition's base clause ('class D : public B {')."""
    out = {}
    for m in AGG_HEAD.finditer(masked):
        head = masked[m.start():m.end() - 1]
        if ":" in head:
            out.setdefault(m.group(2), set()).update(
                x.split()[-1].split("::")[-1] for x in head.split(":", 1)[1].split(",") if x.split())
    return out


def read_aliases(path=ALIASES):
    with open(path, newline="") as f:
        rows = [r for r in csv.DictReader((l for l in f if not l.startswith("#")), delimiter="\t")]
    return rows


def write_aliases(rows, path, header_lines):
    with open(path, "w", newline="") as f:
        for h in header_lines:
            f.write("# " + h + "\n")
        w = csv.DictWriter(f, COLUMNS, delimiter="\t", lineterminator="\n")
        w.writeheader()
        for r in rows:
            w.writerow({k: r[k] for k in COLUMNS})


INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)


def include_closure(snap, rel):
    """The decomp files a source sees, in include order: [rel, headers...] (paths relative to the snapshot; quote
    includes resolved against the including file's folder, then include/)."""
    seen, order = set(), []

    def visit(r):
        if r in seen:
            return
        seen.add(r)
        order.append(r)
        text = open(os.path.join(snap, r), errors="replace").read()
        for inc in INCLUDE.findall(text):
            for cand in (os.path.normpath(os.path.join(os.path.dirname(r), inc)), os.path.join("include", inc)):
                if os.path.exists(os.path.join(snap, cand)):
                    visit(cand)
                    break
    visit(rel)
    return order
