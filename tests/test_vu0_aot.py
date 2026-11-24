# tests/test_vu0_aot.py -- vu0flags (Fable): tools/native/vu0_aot.py keeps exactly the flag pushes that can be observed.
# Synthetic VU0 programs (no game data): a push nobody reads is dropped, the last push before the launch ends is kept
# (the settled MAC), a push a MAC read (FMAND) can see is kept, and an image that reads STATUS (FSAND) keeps every push.
# Run by `make test`: python3 -I tests/test_vu0_aot.py (exit 1 on a failure).
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "native"))
import vu0_aot as g  # noqa: E402

NOP_LO, NOP_UP = 0x8000033C, 0x000002FF
EBIT = 1 << 30
ADD = (0xF << 21) | (3 << 16) | (2 << 11) | (1 << 6) | 0x28   # ADD.xyzw vf1, vf2, vf3 (pushes the flags)
FMAND = (0x1A << 25) | (1 << 16) | (2 << 11)                  # FMAND vi1, vi2 (reads MAC)
FSAND = (0x16 << 25) | (1 << 16) | 0x3F                       # FSAND vi1, 0x3F (reads STATUS)

failures = 0


def check(what, ok):
    global failures
    print("  %-62s %s" % (what, "ok" if ok else "FAILED"))
    if not ok:
        failures += 1


def pushes(pairs):
    """the pairs (index) whose generated code pushes flags, for a program given as [(lower, upper)] (the last with E)"""
    lo = [p[0] for p in pairs] + [NOP_LO] * (512 - len(pairs))
    up = [p[1] for p in pairs] + [NOP_UP] * (512 - len(pairs))
    text = g.gen_image("TEST", lo, up, len(pairs))
    blocks = re.split(r"\nP(\d+): \{", text)
    out = set()
    for k in range(1, len(blocks), 2):
        if "rt.push_flags(" in blocks[k + 1].split("\nP")[0]:
            out.add(int(blocks[k]))
    return out, text


# 1. two pushes, no reader: the first is dropped, the second (the settled MAC at the end) kept
got, _ = pushes([(NOP_LO, ADD), (NOP_LO, ADD), (NOP_LO, NOP_UP | EBIT), (NOP_LO, NOP_UP)])
check("no reader: only the last push before the end is kept", got == {1})
# 2. a MAC read 4 pairs after the second push sees it: the second kept (and the first, as no: it is never the newest)
got, _ = pushes([(NOP_LO, ADD), (NOP_LO, ADD), (NOP_LO, NOP_UP), (NOP_LO, NOP_UP), (NOP_LO, NOP_UP), (FMAND, NOP_UP),
                 (NOP_LO, ADD), (NOP_LO, NOP_UP | EBIT), (NOP_LO, NOP_UP)])
check("MAC read: the push it sees and the last one are kept", got == {1, 6})
# 3. a STATUS read anywhere: every push is kept (STATUS's sticky bits come from all of them)
got, text = pushes([(NOP_LO, ADD), (NOP_LO, ADD), (NOP_LO, ADD), (FSAND, NOP_UP), (NOP_LO, NOP_UP | EBIT), (NOP_LO, NOP_UP)])
check("STATUS read: every push is kept", got == {0, 1, 2})
check("STATUS read: the stats line says so", "it reads STATUS, all kept" in text)

# 4. the EE census: a cfc2 from $vi16 / $vi17 / $vi18 anywhere in the decomp's sources stops the generation
import tempfile  # noqa: E402
with tempfile.TemporaryDirectory() as t:
    os.makedirs(os.path.join(t, "assembly", "asm"))
    os.makedirs(os.path.join(t, "src"))
    open(os.path.join(t, "assembly", "asm", "a.s"), "w").write("    cfc2.i      $t0, $vi1\n    cfc2.ni     $v0, $vi29\n")
    check("EE census: vi1 / vi29 reads are not flag reads", g.ee_flag_reads(t) == [])
    open(os.path.join(t, "src", "b.cpp"), "w").write('asm volatile("cfc2.i %0, $vi17" : "=r"(m));\n')
    got = g.ee_flag_reads(t)
    check("EE census: a MAC read (vi17) in a C source is found", len(got) == 1 and got[0].startswith("src/b.cpp:1:"))

print("test_vu0_aot: %s" % ("PASS" if failures == 0 else "%d FAILED" % failures))
sys.exit(1 if failures else 0)
