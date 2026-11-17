#!/usr/bin/env python3
# stack_slot_check.py -- the static half of config/compare_mask.tsv's "retail reads uninitialised stack" evidence (vu0
# lane): for one retail function (the decomp's assembly/asm, spimdisasm output) and a stack byte range, every store in the
# WHOLE function whose $sp-relative target overlaps the range, and every instruction that takes an address inside the
# frame (addiu/daddiu rX, $sp, off) covering it. No store anywhere and no address taken = the bytes are never written by
# the function, whatever its control flow, so a load of them reads the caller's stale stack [C]. An address taken needs a
# look at that pointer's uses (the script lists them; it does not follow registers).
#
#   python3 -I tools/native/stack_slot_check.py [--decomp DIR] [--before PC] <function name> <lo>[-<hi>] ...
# lo / hi are hex offsets from the function's $sp after its prologue (hi exclusive; default lo + 4). --before PC (the load):
# only stores and address-takings at lower pcs count, and every branch from PC or later back to below PC is listed (none:
# the load runs before any later store) -- for a slot the function writes only after reading it.
import subprocess
import argparse
import glob
import os
import re
import sys

STORES = {'sb': 1, 'sh': 2, 'sw': 4, 'swc1': 4, 'sd': 8, 'sdc1': 8, 'sq': 16, 'sqc2': 16, 'swl': 4, 'swr': 4,
          'sdl': 8, 'sdr': 8}
LINE = re.compile(r'/\*\s*[0-9A-F]+\s+([0-9A-F]{8})\s+[0-9A-F]{8}\s*\*/\s+(\S+)\s+(.*)$')


def function_lines(decomp, name):
    for path in glob.glob(os.path.join(decomp, 'assembly', 'asm', '**', '*.s'), recursive=True):
        text = open(path, encoding='utf-8', errors='replace').read().split('\n')
        for i, l in enumerate(text):
            if l.strip() == 'glabel ' + name:
                out = []
                for m in text[i + 1:]:
                    if m.startswith('glabel ') or m.startswith('endlabel '):
                        break
                    out.append(m)
                return path, out
    sys.exit('stack_slot_check: no glabel %s under %s/assembly/asm' % (name, decomp))


def num(s):
    s = s.strip()
    neg = s.startswith('-')
    v = int(s.lstrip('-'), 16 if 'x' in s.lower() else 10)
    return -v if neg else v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--decomp', default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(subprocess.run(['git', '-C', os.path.dirname(os.path.abspath(__file__)), 'rev-parse', '--path-format=absolute', '--git-common-dir'], capture_output=True, text=True).stdout.strip()))), 'burnout3-retail-decomp'))
    ap.add_argument('--before', default=None)
    ap.add_argument('function')
    ap.add_argument('ranges', nargs='+')
    a = ap.parse_args()
    path, lines = function_lines(a.decomp, a.function)
    ins = []
    for l in lines:
        m = LINE.search(l)
        if m:
            ins.append((m.group(1), m.group(2), m.group(3).split('#')[0].strip()))
    frame = None
    for pc, op, args in ins[:4]:
        if op == 'addiu' and args.replace(' ', '').startswith('$sp,$sp,-'):
            frame = num(args.split(',')[2])
    print('%s (%s): %d instructions, frame %s' % (a.function, os.path.relpath(path, a.decomp), len(ins),
                                                   hex(frame) if frame else '?'))
    status = 0
    before = int(a.before, 16) if a.before else None
    if before is not None:
        labels = {}
        for l in lines:
            m = re.match(r'\s*\.(L[0-9A-F]{8}):', l)
            if m:
                labels[m.group(1)] = int(m.group(1)[1:], 16)
        back = []
        for pc, op, args in ins:
            if int(pc, 16) >= before:
                for t in re.findall(r'\.(L[0-9A-F]{8})', args):
                    if t in labels and labels[t] < before:
                        back.append((pc, op, args))
        print('  --before %08X: %d branch(es) from it or later back to below it%s' % (before, len(back), '' if back else ' [C]'))
        for b in back:
            print('    branch %s %s %s' % b)
        if back:
            status = 1
        ins = [x for x in ins if int(x[0], 16) < before]
    for r in a.ranges:
        lo, _, hi = r.partition('-')
        lo = int(lo, 16)
        hi = int(hi, 16) if hi else lo + 4
        hits, addrs = [], []
        for pc, op, args in ins:
            ops = [x.strip() for x in args.split(',')]
            if op in STORES and len(ops) == 2:
                m = re.match(r'(-?(?:0x)?[0-9A-Fa-f]*)\(\$sp\)$', ops[1])
                if m:
                    off = num(m.group(1)) if m.group(1) else 0
                    if off < hi and off + STORES[op] > lo:
                        hits.append((pc, op, args))
            if op in ('addiu', 'daddiu', 'addu', 'daddu') and len(ops) == 3 and ops[1] == '$sp':
                try:
                    off = num(ops[2])
                except ValueError:
                    continue
                if ops[0] != '$sp' and off <= lo < off + 0x100:   # a pointer at or below the range (a copy loop / callee may reach it)
                    addrs.append((pc, op, args, off))
        verdict = 'NEVER WRITTEN (no store, no address taken) [C]' if not hits and not addrs else \
                  ('written by the function' if hits else 'no direct store; address taken: check those pointers')
        if hits:
            status = 1
        print('  sp+%#x..%#x: %s' % (lo, hi, verdict))
        for pc, op, args in hits:
            print('    store  %s %s %s' % (pc, op, args))
        for pc, op, args, off in addrs:
            print('    addr   %s %s %s   (slot at +%#x from it)' % (pc, op, args, lo - off))
    return status


if __name__ == '__main__':
    sys.exit(main())
