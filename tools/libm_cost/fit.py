#!/usr/bin/env python3
"""Fits the per-block costs of the PS2 libm port (B3LIBM_B(id, n), include/b3libm/ps2_core.h "Exact cost").

  python3 -I tools/libm_cost/fit.py build/libm_cost_rows.tsv [--apply]

Input: the rows of build/libm_cost_dump (tests/libm_cost_dump.cpp): per distinct block-hit vector of one entry, the
retail code's retired instruction count. Every row is one linear equation sum(hits[id] * n[id]) = retired. The system
is solved exactly (rational Gauss-Jordan elimination); blocks the rows do not separate (always hit together) are free,
set to 0 in pivot order, so their sum lands on one block. The fit fails on an inconsistent system (a block mark is
missing in the port) or a non-integer solution. Prints n per block and, per entry, the largest retired count (the
ceiling). --apply rewrites B3LIBM_B(id, old) to B3LIBM_B(id, n) in include/b3libm/*.h.
"""
import glob
import os
import re
import sys
from fractions import Fraction


HAND = {
    # __ieee754_powf's final scale: bgtz not taken + slot, jal scalbnf + slot, b + slot (scalbnf's own blocks count
    # it) vs bgtz taken + slot, mtc1: 4 - 1 [C asm 0x11B040-0x11B058]. Needs |y*log2 x| == 126 exactly with the
    # rounding residue on the far side; no sweep or search input reached it.
    301: (302, 3, 'asm 0x11B040-0x11B058'),
}


def main():
    path = sys.argv[1]
    rows, lines, maxima, conflicts = [], [], {}, 0
    for line in open(path):
        if line.startswith('#'):
            continue
        if line.startswith('CONFLICT'):
            conflicts += 1
            print(line.rstrip())
            continue
        name, retired, _inputs, _first, key = line.rstrip('\n').split('\t')
        hits = {int(a): int(b) for a, b in (p.split(':') for p in key.split(','))} if key else {}
        rows.append((name, hits, int(retired)))
        lines.append(_first)
        maxima[name] = max(maxima.get(name, 0), int(retired))
    if conflicts:
        sys.exit(f'{conflicts} conflict rows: a retail path difference has no block mark')
    ids = sorted({i for _, h, _ in rows for i in h})
    # Gauss-Jordan over sparse dict rows; pivoting on the largest id first, so an always-together group's cost lands on
    # its outermost (lowest-id, usually the function entry) block.
    basis = {}  # pivot id -> (row dict, rhs, provenance: {row index: coefficient})
    for ri, (name, hits, rhs) in enumerate(rows):
        r = {i: Fraction(v) for i, v in hits.items()}
        b = Fraction(rhs)
        prov = {ri: Fraction(1)}
        for p in sorted(basis, reverse=True):
            if p in r:
                f = r[p]
                pr, pb, pp = basis[p]
                for i, v in pr.items():
                    r[i] = r.get(i, 0) - f * v
                    if r[i] == 0:
                        del r[i]
                b -= f * pb
                for k, v in pp.items():
                    prov[k] = prov.get(k, 0) - f * v
                    if prov[k] == 0:
                        del prov[k]
        if not r:
            if b != 0:
                print(f'inconsistent: {name} retired {rhs} (residual {b}); the rows that contradict each other:')
                for k, v in sorted(prov.items()):
                    print(f'  {str(v):>6} x  {rows[k][0]} retired {rows[k][2]}  {lines[k]}  {rows[k][1]}')
                sys.exit(1)
            continue
        # pivot: a unit coefficient if there is one (keeps the costs integral when a loop block always runs a fixed
        # number of times next to its entry block), the largest id among those
        units = [i for i, v in r.items() if abs(v) == 1]
        p = max(units) if units else min(r, key=lambda i: (abs(r[i]), -i))
        f = r[p]
        r = {i: v / f for i, v in r.items()}
        b /= f
        prov = {k: v / f for k, v in prov.items()}
        for q in list(basis):  # keep the basis reduced
            qr, qb, qp = basis[q]
            if p in qr:
                g = qr[p]
                for i, v in r.items():
                    qr[i] = qr.get(i, 0) - g * v
                    if qr[i] == 0:
                        del qr[i]
                for k, v in prov.items():
                    qp[k] = qp.get(k, 0) - g * v
                    if qp[k] == 0:
                        del qp[k]
                basis[q] = (qr, qb - g * b, qp)
        basis[p] = (r, b, prov)
    n = {i: Fraction(0) for i in ids}
    for p, (r, b, _) in basis.items():
        n[p] = b - sum(v * n[i] for i, v in r.items() if i != p and i not in basis)
    # Blocks the sweeps cannot reach (searched for: build a probe with -DB3LIBM_TRACE_BLOCKS), counted by hand from the
    # retail asm relative to a fitted block: id -> (other id, difference, evidence).
    for i, (j, d, why) in HAND.items():
        if i not in n and j in n:
            n[i] = n[j] + d
            print(f'block {i} by hand: block {j} {d:+d} ({why})')
    free = [i for i in ids if i not in basis]
    bad = [i for i in ids if n[i].denominator != 1]
    if bad:
        sys.exit(f'non-integer block costs: {[(i, str(n[i])) for i in bad]}')
    # verify every row
    for name, hits, rhs in rows:
        assert sum(n[i] * v for i, v in hits.items()) == rhs, (name, hits, rhs)
    print(f'{len(rows)} rows, {len(ids)} blocks hit, {len(free)} free (set to 0): {free}')
    neg = [i for i in ids if n[i] < 0]
    if neg:
        print(f'negative block costs (fine for the sums, odd to read): {[(i, int(n[i])) for i in neg]}')
    print('block costs: ' + ' '.join(f'{i}:{int(n[i])}' for i in ids))
    print('max retired per entry:')
    for name, m in maxima.items():
        print(f'  {name}\t{m}')
    if '--apply' in sys.argv:
        here = os.path.dirname(os.path.abspath(__file__))
        for h in glob.glob(os.path.join(here, '..', '..', 'include', 'b3libm', '*.h')):
            s = open(h).read()
            t = re.sub(r'B3LIBM_B\((\d+), -?\d+\)', lambda m: f'B3LIBM_B({m.group(1)}, {int(n.get(int(m.group(1)), 0))})', s)
            if t != s:
                open(h, 'w').write(t)
                print(f'updated {os.path.relpath(h)}')
    marks = set()
    for h in glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'include', 'b3libm', '*.h')):
        marks |= {int(x) for x in re.findall(r'B3LIBM_B\((\d+), -?\d+\)', open(h).read())}
    unseen = sorted(marks - set(ids) - set(HAND))
    if unseen:
        print(f'blocks never hit by the sweeps (cost unknown, left at 0): {unseen}')


if __name__ == '__main__':
    main()
