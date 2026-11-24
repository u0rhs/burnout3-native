#!/usr/bin/env python3
"""check.py -- ownertests: scenarios.json's runs played offscreen on Linux (unpaced), each checked against its `expect`
lines and the window runner's own reading of the log (the same rules as run-all-tests.ps1's), so the scripts are known
to reach their marks before the owner runs them in a window.

  check.py <native_play> <scenarios.json> <out dir> [run id ...]      B3_GAME_DIR, SAVES (the card copied per run)
"""
import json
import os
import re
import shutil
import subprocess
import sys
import time


def main():
    np_path, scen, out = sys.argv[1], sys.argv[2], sys.argv[3]
    only = set(sys.argv[4:])
    game, saves = os.environ["B3_GAME_DIR"], os.environ["SAVES"]
    runs = [r for r in json.load(open(scen))["runs"] if not only or r["id"] in only]
    bad = 0
    for r in runs:
        d = os.path.join(out, "%s-%s" % (r["id"], r["name"]))
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(os.path.join(d, "user"))
        os.makedirs(os.path.join(d, "shots"))
        shutil.copytree(saves, os.path.join(d, "card"))
        ini = os.path.join(d, "user", "pc_settings.ini")
        open(ini, "w").write("".join(l + "\n" for l in r["ini"]))
        env = dict(os.environ)
        env.update(r["env"])
        env.update({"B3NATIVE_SAVES_ROOT": os.path.join(d, "card"), "B3NATIVE_MAX_VBLANKS": str(r["end_vblanks"]),
                    "B3PORT_USER_DIR": os.path.join(d, "user"), "B3_PERF_LOG": os.path.join(d, "perf.txt")})
        args = [np_path, "--game", game, "--offscreen", "--no-pace", "--keys", r["keys"],
                "--record-pad", os.path.join(d, "pads.txt"),
                "--render-args", "--settings %s --offscreen-live %s --shots %s" % (ini, os.path.join(d, "shots"), ",".join(str(s) for s in r["shots"]))]
        if r["keys2"]:
            args += ["--keys2", r["keys2"]]
        t0 = time.time()
        with open(os.path.join(d, "log.txt"), "w") as f:
            rc = subprocess.run(args, env=env, stdout=f, stderr=subprocess.STDOUT).returncode
        secs = time.time() - t0
        log = open(os.path.join(d, "log.txt"), errors="replace").read()
        miss = []
        for e in r["expect"]:
            m = re.match(r"(\d+)\*(.*)", e)
            n, pat = (int(m.group(1)), m.group(2)) if m else (1, e)
            if len(re.findall(pat, log)) < n:
                miss.append(e)
        crash = re.search(r"^crash:", log, re.M)
        ok = rc == 0 and not miss and not crash
        bad += not ok
        print("%-4s %-20s %s  rc %d, %.0f s%s%s" % (r["id"], r["name"], "PASS" if ok else "FAIL", rc, secs,
                                                     "; missing: " + " | ".join(miss) if miss else "", "; a crash line" if crash else ""))
        sys.stdout.flush()
    print("check: %s (%d of %d runs)" % ("PASS" if not bad else "FAIL", len(runs) - bad, len(runs)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
