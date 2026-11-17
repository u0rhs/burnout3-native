#!/usr/bin/env python3
"""Direct-link census of the game view's hook points (pic1, docs/native-renderer.md §32.3).

The native backend's watch hook (b3host::traced_entry) sees a call only when it goes through dispatch: a call_guest stub
or a guest_fn macro in the caller's unit. A C++ callee from a shared decomp header that another selected unit defines is
linked directly (decomp2host call_stubs, "n_direct"), and that call never reaches traced_entry. This lists, per watched
address, the generated units whose objects reference the defining unit's C++ symbol directly (an undefined symbol in
their object, or a call relocation inside the defining object), and the units that reach it through dispatch. A
definition with decomp2host's watch prologue (config/native_watch.txt) reports itself, so its direct links are covered.

    python3 -I tools/native/watch_census.py --gen build/gen_native --obj build/gen_native_ps2 [--entries FILE]

--entries: one address a line (default: `build/gameview_replay --list-entries`, the HUD capture's list) plus the poll
point 0x00214610. Reads the generated units, game_overrides.h and the objects (nm); writes nothing.
"""
import argparse
import glob
import os
import re
import subprocess
import sys
from collections import defaultdict


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", required=True, help="the native generation (units/, include/b3port/game_overrides.h)")
    ap.add_argument("--obj", required=True, help="the units' objects (<unit>.o)")
    ap.add_argument("--entries", help="watched addresses, one a line")
    ap.add_argument("--replay", default="build/gameview_replay", help="gameview_replay, for the default list")
    a = ap.parse_args()

    if a.entries:
        text = open(a.entries).read()
    else:
        text = subprocess.run([a.replay, "--list-entries"], check=True, capture_output=True, text=True).stdout
    watched = [int(x, 16) for x in text.split() if x.strip()]
    watched.append(0x00214610)   # the poll point (UpdatePort)

    over = open(os.path.join(a.gen, "include", "b3port", "game_overrides.h")).read()
    xs = {}
    for m in re.finditer(r'X\("([^"]+)",\s*0x([0-9A-Fa-f]+)u,[^)]*?,\s*(b3gen_\w+),\s*(\w+)\)', over):
        xs[int(m.group(2), 16)] = (m.group(1), m.group(3), m.group(4))

    units = sorted(glob.glob(os.path.join(a.gen, "units", "*.cpp")))
    texts = {os.path.basename(u)[:-4]: open(u, errors="replace").read() for u in units}

    # every object's undefined symbols (one nm run over all of them)
    objs = sorted(glob.glob(os.path.join(a.obj, "*.o")))
    undefined = defaultdict(set)   # symbol -> objects
    for k in range(0, len(objs), 400):
        out = subprocess.run(["nm", "-A", "-u"] + objs[k:k + 400], capture_output=True, text=True).stdout
        for line in out.splitlines():
            m = re.match(r"(.+?\.o):\s+U (\S+)$", line)
            if m:
                undefined[m.group(2)].add(os.path.basename(m.group(1))[:-2])

    rows, gaps = [], 0
    for addr in watched:
        if addr not in xs:
            rows.append((addr, "-", "not generated (no native C yet)", 0, []))
            continue
        name, wrapper, track = xs[addr]
        # the defining unit: the one that defines the wrapper, and the C++ function its body calls
        unit = next((u for u, t in texts.items() if re.search(r"\b%s\s*\([^;{]*\)\s*\{" % re.escape(wrapper), t)), None)
        if unit is None:
            rows.append((addr, name, "wrapper %s not found" % wrapper, 0, []))
            continue
        body = re.search(r"\b%s\s*\([^;{]*\)\s*\{(.*?)\}" % re.escape(wrapper), texts[unit], re.S).group(1)
        obj = os.path.join(a.obj, unit + ".o")
        defined = subprocess.run(["nm", "--defined-only", obj], capture_output=True, text=True).stdout.split("\n") if os.path.exists(obj) else []
        syms = [l.split()[-1] for l in defined if re.match(r"^[0-9a-f]+ [TW] ", l)]
        dem = dict(zip(syms, subprocess.run(["c++filt"], input="\n".join(syms), capture_output=True, text=True).stdout.split("\n")))
        called = re.search(r"([\w:~]+)\s*\(", body.replace("return", ""))
        callee = called.group(1) if called else ""
        target = [s for s, d in dem.items() if d.split("(")[0].endswith(callee.split("::")[-1]) and callee and
                  (("::" not in callee) or d.split("(")[0].endswith(callee))]
        direct = sorted({o for s in target for o in undefined.get(s, ()) if o != unit})
        # calls inside the defining object itself (other functions of the same unit): not undefined anywhere, but each
        # carries a relocation against the (global) symbol; the wrapper's own call (the dispatch path) is not counted
        if target and os.path.exists(obj):
            dis = subprocess.run(["objdump", "-dr", "--no-show-raw-insn", obj], capture_output=True, text=True).stdout
            fn, inner, callers, insn = None, 0, set(), ""
            for line in dis.splitlines():
                h = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
                if h:
                    fn = h.group(1)
                    continue
                c = re.search(r"R_X86_64_(?:PLT32|PC32)\s+([^\s+-]+)", line)
                if not c:
                    if line.strip():
                        insn = line
                    continue
                # a call or tail jump (taking the address, e.g. a callback registration, is not a call)
                if not re.search(r"\s(call|jmp)\s", insn):
                    continue
                if c.group(1) in target and fn not in target and wrapper not in (fn or "") and "native_entry" not in (fn or ""):
                    inner += 1
                    callers.add(fn)
            if inner and os.environ.get("WATCH_CENSUS_DEBUG"):
                print("  %s: %s" % (unit, ", ".join(sorted(callers))), file=sys.stderr)
            if inner:
                direct.append("%s itself (%d call%s from its other functions)" % (unit, inner, "" if inner == 1 else "s"))
        hexa = "0x%08Xu" % addr
        dispatched = sum(1 for u, t in texts.items() if u != unit and (("call_guest<" in t and ">(%s" % hexa in t) or (", %s>" % hexa) in t))
        selfrep = ("watch_self<%s>" % hexa) in texts[unit]   # decomp2host's prologue (config/native_watch.txt)
        if direct and not selfrep:
            gaps += 1
        rows.append((addr, name, "%s in %s (%s)%s" % (callee or "?", unit, track, "; self-reporting (prologue)" if selfrep else ""),
                     dispatched, direct))

    print("# watched address, name, definition, units through dispatch, units linking directly (not seen by the watch hook)")
    for addr, name, where, nd, direct in rows:
        print("0x%08X  %-60s  %s  dispatch %d  direct %d%s" % (addr, name[:60], where, nd, len(direct),
                                                               (": " + ", ".join(direct[:8]) + (" ..." if len(direct) > 8 else "")) if direct else ""))
    print("# %d watched, %d generated, %d with direct links not covered by a prologue" % (len(rows), sum(1 for r in rows if r[1] != "-"), gaps))
    return 0


if __name__ == "__main__":
    sys.exit(main())
