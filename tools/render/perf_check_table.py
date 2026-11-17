#!/usr/bin/env python3
"""One row of tools/render/perf_check.sh: perf_check_table.py <variant> <b3perf log> [budget_ms]

Prints the CPU ms per frame of the render thread's stages and the per-frame draw / pass / submit / upload counts, steady
seconds only. Exit 1 when the log has no usable second, or (variant "base", budget_ms > 0) the CPU stages' sum is over the budget.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "native"))
import perf_summary as ps  # noqa: E402

CPU = ("drawlist", "cmd_scene", "cmd_post", "cmd_overlay", "upload")
HEAD_DONE = [False]


def main():
    variant, log = sys.argv[1], sys.argv[2]
    budget = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
    _, names, rows = ps.load(log, 20, 1)
    if not names or not rows:
        print("  %-15s no usable seconds" % variant)
        return 1
    stages, out, counters, fps, frames = ps.summarise(names, rows)
    cpu = sum(out.get(s, (0, 0, 0))[0] for s in CPU)
    print("  %-15s fps %7.1f  cpu %6.3f ms  [%s]  draws %6.0f passes %4.1f submits %4.1f uploads %5.1f up_KB %7.1f" % (
        variant, fps, cpu, " ".join("%s %.3f" % (s, out.get(s, (0, 0, 0))[0]) for s in CPU),
        counters.get("draws", 0), counters.get("passes", 0), counters.get("submits", 0), counters.get("uploads", 0),
        counters.get("upload_bytes", 0) / 1024.0))
    if variant == "base" and budget > 0 and cpu > budget:
        print("  base: render CPU %.3f ms over the budget %.3f ms" % (cpu, budget))
        return 1
    return 0


sys.exit(main())
