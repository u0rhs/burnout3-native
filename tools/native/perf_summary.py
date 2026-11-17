#!/usr/bin/env python3
"""Summarise a B3_PERF_LOG file (include/b3gfx/perf.h): one table over the whole capture.

  tools/native/perf_summary.py <log> [<log2>]     (two logs: before / after side by side)

Per stage: the frame-weighted mean of the per-second averages, the worst second's p95 and the worst single sample, in ms.
Seconds with fewer than MIN_FRAMES frames (loading screens, the start) are left out: --min-frames N (default 20); --skip N leaves out the first N usable seconds.
"""
import sys


def load(path, min_frames, skip=0):
    names, rows, info = None, [], ""
    for line in open(path):
        if line.startswith("# b3perf"):
            info = line[2:].strip()
        elif line.startswith("# t_s"):
            names = line[2:].split()
        elif line.startswith("#") or not line.strip():
            continue
        else:
            v = line.split()
            if int(v[1]) >= min_frames:
                rows.append(v)
    return info, names, rows[skip:]


def summarise(names, rows):
    out = {}
    frames = sum(int(r[1]) for r in rows) or 1
    cols = {n: i for i, n in enumerate(names)}
    stages = sorted({n[:-4] for n in names if n.endswith("_avg")}, key=lambda s: cols[s + "_avg"])
    for s in stages:
        w = n = 0.0
        p95 = mx = 0.0
        for r in rows:
            weight = int(r[1]) if not s.startswith("game_") else 60.0
            w += float(r[cols[s + "_avg"]]) * weight
            n += weight
            p95 = max(p95, float(r[cols[s + "_p95"]]))
            mx = max(mx, float(r[cols[s + "_max"]]))
        out[s] = (w / n if n else 0.0, p95, mx)
    counters = {}
    for c in ("draws", "passes", "submits", "uploads", "upload_bytes"):
        if c in cols:
            counters[c] = sum(float(r[cols[c]]) for r in rows) / frames
    fps = frames / sum(int(r[1]) / float(r[cols["fps"]]) for r in rows) if rows else 0
    return stages, out, counters, fps, frames


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    min_frames = 20
    skip = 0
    if "--skip" in sys.argv:   # the first N usable seconds left out (loading)
        skip = int(sys.argv[sys.argv.index("--skip") + 1])
        args = [a for a in args if a != str(skip)]
    if "--min-frames" in sys.argv:
        min_frames = int(sys.argv[sys.argv.index("--min-frames") + 1])
        args = [a for a in args if a != str(min_frames)]
    if not args:
        print(__doc__)
        return 1
    res = []
    for p in args[:2]:
        info, names, rows = load(p, min_frames, skip)
        if not names or not rows:
            print(p, ": no usable seconds")
            return 1
        print(p, "-", info)
        res.append(summarise(names, rows))
    stages = res[0][0]
    head = "%-16s" % "stage (ms)" + "".join("  %8s %8s %8s" % ("mean", "p95max", "max") for _ in res)
    print(head)
    for s in stages:
        print("%-16s" % s + "".join("  %8.3f %8.3f %8.3f" % r[1].get(s, (0, 0, 0)) for r in res))
    print("%-16s" % "fps" + "".join("  %8.1f %8s %8s" % (r[3], "", "") for r in res))
    for c in ("draws", "passes", "submits", "uploads", "upload_bytes"):
        print("%-16s" % (c + "/frame") + "".join("  %8.1f %8s %8s" % (r[2].get(c, 0), "", "") for r in res))
    return 0


if __name__ == "__main__":
    sys.exit(main())
