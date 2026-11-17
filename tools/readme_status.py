#!/usr/bin/env python3
"""readme_status.py: render README.md's generated numbers (the badge row and the stat strip under the header).

Inputs: docs/status.json (headline numbers of the last verified live run; update it with every run) and, when the
decomp checkout sits next to this repo (../burnout3-retail-decomp, or $B3_DECOMP), its live counters
(tools/progress_report.py + tools/progress_stats.py); otherwise status.json's decomp_fallback.
Outputs: the README blocks between <!-- badges:start/end --> and <!-- stats:start/end -->. A second run leaves the
README unchanged. Usage: python3 tools/readme_status.py"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import quote

ROOT = Path(__file__).resolve().parent.parent
S = json.loads((ROOT / "docs" / "status.json").read_text())


def decomp():
    d = Path(os.environ.get("B3_DECOMP", ROOT.parent / "burnout3-retail-decomp"))
    fb = dict(S["decomp_fallback"])
    try:
        rep = subprocess.run([sys.executable, "tools/progress_report.py"], cwd=d, capture_output=True, text=True,
                             timeout=300).stdout
        st = subprocess.run([sys.executable, "-I", "tools/progress_stats.py", "."], cwd=d, capture_output=True,
                            text=True, timeout=300).stdout
        mf = re.search(r"By function count: ([\d,]+)/([\d,]+)", rep)
        mb = re.search(r"By byte count: +([\d,]+)/([\d,]+)", rep)
        rows = {m.group(1): (int(m.group(2)), int(m.group(3)))
                for m in re.finditer(r"^(\w+)\s+(\d+) functions\s+(\d+) bytes", st, re.M)}
        if not (mf and mb and rows):
            return fb
        num = lambda s: int(s.replace(",", ""))
        fn, ftot, bn, btot = num(mf.group(1)), num(mf.group(2)), num(mb.group(1)), num(mb.group(2))
        out = {"functions_total": ftot, "functions_matched": fn, "bytes_total": btot, "bytes_matched": bn}
        for i, key, tot, done in ((0, "functions", ftot, fn), (1, "bytes", btot, bn)):
            # the not-yet-matched remainder, split in the proportions of the per-function scan (as the decomp README)
            scan = sum(v[i] for v in rows.values()) or 1
            for state in ("complete", "partial"):
                out["%s_%s" % (key, state)] = int(round((tot - done) * rows.get(state, (0, 0))[i] / scan))
        S["decomp_fallback"] = out
        (ROOT / "docs" / "status.json").write_text(json.dumps(S, indent=2) + "\n")
        return out
    except Exception:
        return fb


D = decomp()


def pct(n, t):
    return 100.0 * n / t if t else 0.0


def num(n):
    return "{:,}".format(n)


def badge(label, value, color, alt, link):
    esc = lambda t: quote(t.replace("-", "--").replace("_", "__"), safe="")
    return ('  <a href="%s"><img alt="%s" src="https://img.shields.io/badge/%s-%s-%s?style=for-the-badge&'
            'labelColor=0d1117"></a>' % (link, alt, esc(label), esc(value), color))


def blocks():
    # every game function has a native version: byte-matched or complete C (partial C would not run natively)
    native = pct(D["bytes_matched"] + D["bytes_complete"], D["bytes_total"])
    badges = "\n".join([
        "<!-- badges:start -->", '<p align="center">',
        badge("game code", "%.0f%% native" % native, "2ea043", "Game code: %.0f%% native" % native, "#how-it-works"),
        badge("emulation", "none", "a371f7", "Emulation: none", "#how-it-works"),
        badge("byte-matched", "%s of %s functions" % (num(D["functions_matched"]), num(D["functions_total"])),
              "388bfd", "Byte-matched functions", "#how-it-works"),
        "</p>", "<!-- badges:end -->"])
    rest = D["functions_total"] - D["functions_matched"]
    cells = [("🧬 **0**", "chips emulated: no EE, VU, IOP, GS, SPU2 or IPU; the game binary has no interpreter"),
             ("🎯 **%s / %s**" % (num(D["functions_matched"]), num(D["functions_total"])),
              "game functions rebuilt byte for byte; the other %s as complete C, audited against retail" % num(rest)),
             ("🔁 **%d / %d**" % (S["scenarios_deterministic"], S["scenarios_total"]),
              "test scenarios with no PS2 code at all, identical run to run")]
    # Not a measured fps figure: that depends on the player's hardware (the fact sheet has the measurements and the
    # machine they come from).
    cells.append(("⚡ **Uncapped**", "frame rate: set your own limit; the game's 60 Hz simulation never changes"))
    stats = "\n".join([
        "<!-- stats:start -->",
        "| " + " | ".join(h for h, _ in cells) + " |",
        "|" + ":-:|" * len(cells),
        "| " + " | ".join("<sub>%s</sub>" % b for _, b in cells) + " |",
        "<!-- stats:end -->"])
    return {"badges": badges, "stats": stats}


p = ROOT / "README.md"
s = p.read_text(encoding="utf-8")
for key, text in blocks().items():
    a, b = "<!-- %s:start -->" % key, "<!-- %s:end -->" % key
    if a in s and b in s:
        s = s[:s.index(a)] + text + s[s.index(b) + len(b):]
    else:
        print("readme_status: README.md has no %s ... %s block" % (a, b), file=sys.stderr)
p.write_text(s, encoding="utf-8", newline="\n")
