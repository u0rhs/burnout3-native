#!/usr/bin/env python3
# lane_trace.py -- the register extension of stack_slot_check.py (vu0 lane; the writer-proof condition for
# config/compare_mask.tsv, 2026-11-07): where does one 32-bit lane that a retail function stores come from? Backward
# data flow over the function's instruction-level control-flow graph (spimdisasm assembly/asm, delay slots included),
# following the lane through
#   VF w lanes   (VU ops whose dest field lacks w pass it; qmtc2 rt -> vf takes it from rt's top word; lqc2 from memory)
#   GPR top words (bits 96-127: qmfc2 vf -> rt from vf.w; lq; por rd, rs, $zero; 64-bit ops leave them, r5900_mini.h)
#   stack words  ($sp-relative, including pointers $rX = $sp + C known on every path: a small value-set analysis)
# Every backward path ends in one of
#   WRITTEN  <pc> <insn>    the lane is (re)defined there (a VU op writing w, a store of another value, a load from
#                           memory that is not this frame, a call that may change it): the lane is defined -> not a mask
#   ENTRY    <location>     the function's entry still holds it: the caller's register lane (class "retail carries its
#                           caller's stale VF lane") or, for a stack word, the caller's stack below its sp
#   PROLOGUE <sp+X>         a stack word of this frame never written before the use: uninitialised stack
#   UNKNOWN  <pc> <why>     the analysis cannot follow (a store through a pointer that may alias the slot, a copy loop)
# A lane qualifies for a mask row only when EVERY path ends in ENTRY (register class) or PROLOGUE / ENTRY-stack (stack
# class). The verdict is printed with each path's end.
#
#   python3 -I tools/native/lane_trace.py [--decomp DIR] [--args <callee>=<n> ...] [--clobbers <callee>=<regs> ...]
#       <function> <pc> <location>
#     location (the lane just BEFORE pc executes): vfN (its w lane) | gpr:<reg> (its top word) | sp:<hex offset>
#     e.g. the w of a `sq $a1, 0x20F0($sp)` at 0x223AEC:  BuildCustomCameraData__18CB3GraphicsManagerFi 00223AEC gpr:a1
import subprocess
import argparse
import glob
import os
import re
import sys

LINE = re.compile(r'/\*\s*[0-9A-F]+\s+([0-9A-F]{8})\s+[0-9A-F]{8}\s*\*/\s+(\S+)\s*(.*)$')
LABEL = re.compile(r'^\s*\.(L[0-9A-F]{8}):')
CALLEE_SAVED = {'s0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'fp', 'sp', 'gp'}
BRANCHES = {'b', 'beq', 'bne', 'beqz', 'bnez', 'blez', 'bgtz', 'bltz', 'bgez', 'bc1t', 'bc1f', 'bc0t', 'bc0f', 'bc2t',
            'bc2f', 'bal', 'bltzal', 'bgezal'}
LIKELY = {'beql', 'bnel', 'beqzl', 'bnezl', 'blezl', 'bgtzl', 'bltzl', 'bgezl', 'bc1tl', 'bc1fl', 'bc0tl', 'bc0fl'}
ARGS = {}   # --args <callee>=<n>: the argument registers a callee takes (its declaration); default all 8
CLOBBERS = {}   # --clobbers <callee>=<r1,r2,..>: the only GPRs the callee writes (its body read, cited); no VF registers
STORES = {'sb': 1, 'sh': 2, 'sw': 4, 'swc1': 4, 'sd': 8, 'sdc1': 8, 'sq': 16, 'sqc2': 16, 'swl': 4, 'swr': 4, 'sdl': 8,
          'sdr': 8}


def load(decomp, name):
    for path in glob.glob(os.path.join(decomp, 'assembly', 'asm', '**', '*.s'), recursive=True):
        text = open(path, encoding='utf-8', errors='replace').read().split('\n')
        for i, l in enumerate(text):
            if l.strip() == 'glabel ' + name:
                ins, labels = [], {}
                pending = []
                for m in text[i + 1:]:
                    if m.startswith('glabel ') or m.startswith('endlabel '):
                        break
                    lm = LABEL.match(m)
                    if lm:
                        pending.append(lm.group(1))
                        continue
                    x = LINE.search(m)
                    if x:
                        pc = int(x.group(1), 16)
                        for lab in pending:
                            labels[lab] = pc
                        pending = []
                        ops = [o.strip() for o in x.group(3).split('#')[0].split(',')] if x.group(3).strip() else []
                        ins.append((pc, x.group(2), ops))
                return path, ins, labels
    sys.exit('lane_trace: no glabel %s' % name)


def reg(o):
    return o.lstrip('$') if o.startswith('$') else None


def num(s):
    s = s.strip()
    neg = s.startswith('-')
    s = s.lstrip('-')
    v = int(s, 16) if s.lower().startswith('0x') else int(s)
    return -v if neg else v


def mem(o):
    m = re.match(r'(-?(?:0x)?[0-9A-Fa-f]*)\(\$(\w+)\)$', o)
    if not m:
        return None, None
    return (num(m.group(1)) if m.group(1) else 0), m.group(2)


def cfg(ins, labels):
    idx = {pc: k for k, (pc, _, _) in enumerate(ins)}
    succ = {k: [] for k in range(len(ins))}
    for k, (pc, op, ops) in enumerate(ins):
        nxt = k + 1 if k + 1 < len(ins) else None
        tgt = None
        for o in ops:
            if o.startswith('.L') and o[1:] in labels:
                tgt = idx.get(labels[o[1:]])
        is_br = op in BRANCHES or op in LIKELY or op in ('j', 'jal', 'jr', 'jalr')
        if not is_br:
            # the delay slot of the previous instruction is handled there
            prev = ins[k - 1][1] if k else None
            if prev and (prev in BRANCHES or prev in LIKELY or prev in ('j', 'jal', 'jr', 'jalr')):
                continue
            if nxt is not None:
                succ[k].append(nxt)
            continue
        d = k + 1   # delay slot
        if d >= len(ins):
            continue
        after = d + 1 if d + 1 < len(ins) else None
        if op in ('jal', 'jalr', 'bal', 'bltzal', 'bgezal'):
            succ[k].append(d)
            if after is not None:
                succ[d].append(after)
        elif op == 'jr' or op == 'j':
            succ[k].append(d)   # function exit (or tail jump): no successor after the slot
        elif op == 'b':
            succ[k].append(d)
            if tgt is not None:
                succ[d].append(tgt)
        elif op in LIKELY:
            succ[k].append(d)
            if tgt is not None:
                succ[d].append(tgt)
            if after is not None:
                succ[k].append(after)
        else:
            succ[k].append(d)
            if tgt is not None:
                succ[d].append(tgt)
            if after is not None:
                succ[d].append(after)
    pred = {k: [] for k in range(len(ins))}
    for k, ss in succ.items():
        for s in ss:
            pred[s].append(k)
    return idx, succ, pred


def counted_loops(ins, labels):
    """loop head index -> trip count, for `addiu rc, $zero, N` ... L: ... `addiu rc, rc, -1` ... `bgtz rc, L` (the copy loops)"""
    idx = {pc: k for k, (pc, _, _) in enumerate(ins)}
    loops = {}
    for k, (pc, op, ops) in enumerate(ins):
        if op not in ('bgtz', 'bnez') or len(ops) != 2 or not ops[1].startswith('.L') or ops[1][1:] not in labels:
            continue
        h = idx.get(labels[ops[1][1:]])
        if h is None or h > k:
            continue
        rc = reg(ops[0])
        if not any(o2 in ('addiu', 'daddiu') and len(p2) == 3 and reg(p2[0]) == rc and reg(p2[1]) == rc and p2[2] == '-0x1'
                   for _, o2, p2 in ins[h:k + 2]):
            continue
        for j in range(h - 1, max(h - 200, -1), -1):
            _, o2, p2 = ins[j]
            if p2 and reg(p2[0]) == rc:
                if o2 in ('addiu', 'daddiu') and len(p2) == 3 and reg(p2[1]) == 'zero':
                    loops[h] = num(p2[2])
                break
    return loops


def sp_values(ins, succ, loops=None):
    """per instruction (before it): reg -> frozenset of $sp offsets, or 'TOP' (may point into the frame, unknown)"""
    TOP = 'TOP'
    loops = loops or {}
    first = {}   # loop head -> the state the forward entry brought (the induction start values)
    state = {0: {}}
    work = [0]
    out = {}
    def apply(k, st):
        pc, op, ops = ins[k]
        st = dict(st)
        rd = reg(ops[0]) if ops else None
        if op in ('addiu', 'daddiu', 'addu', 'daddu') and len(ops) == 3 and rd:
            a, b = reg(ops[1]), ops[2]
            base = {0} if a == 'sp' else st.get(a)
            if a == 'sp' and rd == 'sp':
                return st   # the prologue / epilogue: offsets are post-prologue
            if base is not None and base != TOP:
                try:
                    c = num(b)
                    st[rd] = frozenset(x + c for x in base) if len(base) < 40 else TOP
                except ValueError:
                    rb = reg(b)
                    st[rd] = base if rb == 'zero' else TOP
                return st
            if base == TOP:
                st[rd] = TOP
                return st
        if op in ('jal', 'jalr'):
            for r in list(st):
                if r not in CALLEE_SAVED:
                    del st[r]
            return st
        if rd and op not in STORES and not op.startswith('b') and op not in ('j', 'jr', 'sqc2', 'swc1', 'mtc1', 'ctc2',
                                                                             'qmtc2', 'qmtc2.ni', 'qmtc2.i') \
                and not op.startswith('v') and not op.startswith('c.'):
            st.pop(rd, None)
        return st
    while work:
        k = work.pop()
        st = state[k]
        res = apply(k, st)
        out[k] = st
        for s in succ[k]:
            old = state.get(s)
            if old is None:
                state[s] = res
                work.append(s)
                continue
            merged = {}
            if s in loops:
                first.setdefault(s, old)
            for r in set(old) | set(res):
                a, b = old.get(r), res.get(r)
                if a is None or b is None:
                    if (a is not None and a != frozenset()) or (b is not None and b != frozenset()):
                        merged[r] = TOP   # a pointer on one path only: may alias
                    continue
                if a == TOP or b == TOP:
                    merged[r] = TOP
                else:
                    u = a | b
                    if s in loops and r in first.get(s, {}) and first[s][r] not in (TOP, None) and len(first[s][r]) == 1:
                        # a counted loop: the induction pointer takes trip + 1 values from its entry value on
                        start = next(iter(first[s][r]))
                        u = frozenset(sorted(u, key=lambda v: abs(v - start))[:loops[s] + 1])
                    merged[r] = u if len(u) < 40 else TOP
            if merged != old:
                state[s] = merged
                work.append(s)
    return out


def vu_dest(op, ops):
    """(dest vf name or None, fields) for a COP2 macro op"""
    if not op.startswith('v') or not ops:
        return None, ''
    base, _, fields = op.partition('.')
    if base in ('vclipw', 'vdiv', 'vsqrt', 'vrsqrt', 'vwaitq', 'vnop', 'vcallms', 'vcallmsr', 'vrinit', 'vrxor',
                'vsqi', 'vsqd', 'viswr', 'vmtir', 'vilwr', 'viadd', 'viaddi', 'visub', 'viand', 'vior', 'vlqi', 'vlqd'):
        if base in ('vlqi', 'vlqd'):
            return reg(ops[0]), fields or 'xyzw'
        return None, ''
    d = reg(ops[0])
    if d is None or d == 'ACC' or not d.startswith('vf'):
        return None, ''
    return d, fields or 'xyzw'


def step_back(k, ins, spv, loc):
    """the lane's location before ins[k], given it after; or ('END', kind, detail)"""
    pc, op, ops = ins[k]
    kind, name = loc
    sv = spv.get(k, {})
    if kind == 'vf':
        d, f = vu_dest(op, ops)
        if d == name:
            if 'w' in f:
                return ('END', 'WRITTEN', '%08X %s %s' % (pc, op, ', '.join(ops)))
            return loc
        if op.startswith('qmtc2') and len(ops) == 2 and reg(ops[1]) == name:
            r = reg(ops[0])
            return ('END', 'WRITTEN', '%08X %s %s (from $zero)' % (pc, op, ', '.join(ops))) if r == 'zero' else ('gpr', r)
        if op == 'lqc2' and reg(ops[0]) == name:
            off, base = mem(ops[1])
            if base == 'sp':
                return ('sp', off + 12)
            vals = sv.get(base)
            if vals not in (None, 'TOP') and len(vals) == 1:
                return ('sp', next(iter(vals)) + off + 12)
            return ('END', 'WRITTEN', '%08X %s %s (memory%s)' % (pc, op, ', '.join(ops), ', may be the frame' if vals == 'TOP' else ''))
        if op in ('jal', 'jalr'):
            if ops and ops[0] in CLOBBERS:
                return loc
            return ('END', 'UNKNOWN', '%08X %s %s (a call may change %s)' % (pc, op, ', '.join(ops), name))
        return loc
    if kind == 'gpr':
        if not ops:
            return loc
        rd = reg(ops[0])
        if op.startswith('qmfc2') and rd == name:
            return ('vf', reg(ops[1]))
        if op == 'lq' and rd == name:
            off, base = mem(ops[1])
            if base == 'sp':
                return ('sp', off + 12)
            vals = sv.get(base)
            if vals not in (None, 'TOP') and len(vals) == 1:
                return ('sp', next(iter(vals)) + off + 12)
            return ('END', 'WRITTEN', '%08X %s %s (memory)' % (pc, op, ', '.join(ops)))
        if op == 'por' and rd == name and len(ops) == 3:
            a, b = reg(ops[1]), reg(ops[2])
            if b == 'zero':
                return ('gpr', a)
            if a == 'zero':
                return ('gpr', b)
            return ('END', 'WRITTEN', '%08X %s %s' % (pc, op, ', '.join(ops)))
        if op == 'pcpyud' and rd == name and len(ops) == 3:
            return ('gpr', reg(ops[2]))   # rd[127:64] = rt[127:64]
        if rd == name and (op.startswith('p') or op in ('lq', 'qmfc2', 'mfsa', 'pmfhl', 'pmflo', 'pmfhi', 'lqc2')):
            return ('END', 'WRITTEN', '%08X %s %s' % (pc, op, ', '.join(ops)))
        if op in ('jal', 'jalr') and ops and ops[0] in CLOBBERS:
            if name in CLOBBERS[ops[0]]:
                return ('END', 'WRITTEN', '%08X %s %s (the callee writes $%s)' % (pc, op, ', '.join(ops), name))
            return loc
        if op in ('jal', 'jalr') and name not in CALLEE_SAVED:
            return ('END', 'UNKNOWN', '%08X %s %s (a call may change $%s)' % (pc, op, ', '.join(ops), name))
        if name == 'sp' and op in ('addiu', 'daddiu') and reg(ops[0]) == 'sp':
            return loc
        return loc
    if kind == 'sp':
        x = name
        if op in ('addiu', 'daddiu') and len(ops) == 3 and reg(ops[0]) == 'sp' and reg(ops[1]) == 'sp':
            c = num(ops[2])
            if c < 0:
                return ('END', 'PROLOGUE', 'sp+%#x (frame %#x: never written in the function)' % (x, -c))
        if op in STORES and len(ops) == 2:
            off, base = mem(ops[1])
            if base is None:
                return loc
            cands = None
            if base == 'sp':
                cands = {off}
            else:
                vals = sv.get(base)
                if vals == 'TOP':
                    return ('END', 'UNKNOWN', '%08X %s %s (through a pointer that may point into the frame)' % (pc, op, ', '.join(ops)))
                if vals is not None:
                    cands = {v + off for v in vals}
            if cands is not None:
                size = STORES[op]
                hits = [c for c in cands if c <= x < c + size]
                if hits:
                    if len(cands) > 1:
                        # a copy loop (the CGtV3d copy: lb / sb pairs, both pointers stepping): the store's value is the load
                        # just before it from the source pointer at the same relative offset -> follow the source word
                        src = reg(ops[0])
                        for j in range(k - 1, max(k - 7, -1), -1):
                            _, op2, ops2 = ins[j]
                            if ops2 and reg(ops2[0]) == src and op2 in ('lb', 'lbu', 'lh', 'lhu', 'lw', 'ld', 'lq'):
                                off2, base2 = mem(ops2[1])
                                svals = sv.get(base2) if base2 != 'sp' else None
                                dvals = sv.get(base)
                                if svals not in (None, 'TOP') and dvals not in (None, 'TOP') and len(svals) == len(dvals):
                                    shift = min(svals) + off2 - (min(dvals) + off)
                                    return ('sp', x + shift)
                                break
                        return ('END', 'UNKNOWN', '%08X %s %s (one of %d frame addresses: a loop)' % (pc, op, ', '.join(ops), len(cands)))
                    c = hits[0]
                    src = reg(ops[0])
                    lane = (x - c) // 4
                    if op == 'sqc2' and lane == 3:
                        return ('vf', src)
                    if op == 'sq' and lane == 3:
                        return ('END', 'WRITTEN', '%08X %s %s (from $zero)' % (pc, op, ', '.join(ops))) if src == 'zero' else ('gpr', src)
                    return ('END', 'WRITTEN', '%08X %s %s' % (pc, op, ', '.join(ops)))
        if op in ('jal', 'jalr'):
            nargs = ARGS.get(ops[0] if ops else '', 8)
            for r in ('a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3')[:nargs]:
                vals = sv.get(r)
                if vals == 'TOP' or (vals and any(v <= x < v + 0x100 for v in vals)):
                    return ('END', 'UNKNOWN', '%08X %s %s (the callee gets a pointer near sp+%#x in $%s)' % (pc, op, ', '.join(ops), x, r))
        return loc
    return loc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--decomp', default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(subprocess.run(['git', '-C', os.path.dirname(os.path.abspath(__file__)), 'rev-parse', '--path-format=absolute', '--git-common-dir'], capture_output=True, text=True).stdout.strip()))), 'burnout3-retail-decomp'))
    ap.add_argument('--args', action='append', default=[], help='<callee>=<n>: the callee takes n argument registers')
    ap.add_argument('--clobbers', action='append', default=[], help='<callee>=<r1,r2>: the only GPRs the callee writes')
    ap.add_argument('function')
    ap.add_argument('pc')
    ap.add_argument('location')
    a = ap.parse_args()
    for spec in a.args:
        n, _, c = spec.partition('=')
        ARGS[n] = int(c)
    for spec in a.clobbers:
        n, _, c = spec.partition('=')
        CLOBBERS[n] = set(x.strip().lstrip('$') for x in c.split(',') if x.strip())
    path, ins, labels = load(a.decomp, a.function)
    idx, succ, pred = cfg(ins, labels)
    spv = sp_values(ins, succ, counted_loops(ins, labels))
    pc = int(a.pc, 16)
    if pc not in idx:
        sys.exit('lane_trace: %08X is not in %s' % (pc, a.function))
    k0 = idx[pc]
    kind, _, name = a.location.partition(':')
    if a.location.startswith('vf'):
        loc = ('vf', a.location)
    elif kind == 'gpr':
        loc = ('gpr', name)
    elif kind == 'sp':
        loc = ('sp', int(name, 16))
    else:
        sys.exit('lane_trace: location vfN | gpr:<reg> | sp:<hex>')
    print('%s (%s), lane %s before %08X' % (a.function, os.path.relpath(path, a.decomp), a.location, pc))
    ends = {}
    seen = set()
    work = [(p, loc) for p in pred[k0]] or [(None, loc)]
    if not pred[k0]:
        ends.setdefault(('ENTRY', fmt(loc)), 1)
    while work:
        k, l = work.pop()
        if k is None:
            continue
        if (k, l) in seen:
            continue
        seen.add((k, l))
        r = step_back(k, ins, spv, l)
        if r[0] == 'END':
            ends[(r[1], r[2])] = ends.get((r[1], r[2]), 0) + 1
            continue
        if not pred[k]:
            if k != 0:   # no predecessor and not the entry: dead code (after an unconditional jump, no label) -- not a path
                ends[('DEAD', '%08X (no predecessor: unreachable)' % ins[k][0])] = 1
                continue
            ends[('ENTRY', fmt(r))] = ends.get(('ENTRY', fmt(r)), 0) + 1
            continue
        for p in pred[k]:
            work.append((p, r))
    kinds = {e[0] for e in ends} - {'DEAD'}
    for (kd, det), n in sorted(ends.items()):
        print('  %-8s %s%s' % (kd, det, '' if n == 1 else '   (%d paths)' % n))
    if kinds <= {'ENTRY'}:
        stack = all(det.startswith('sp+') for kd, det in ends if kd != 'DEAD')
        print('verdict: every path reaches the entry -> %s [C]' % ('the caller\'s stack (uninitialised)' if stack else 'the caller\'s register lane'))
        return 0
    if kinds <= {'ENTRY', 'PROLOGUE'}:
        print('verdict: every path ends at the entry or the prologue -> never written in the function [C]')
        return 0
    print('verdict: NOT proven (%s)' % ', '.join(sorted(kinds - {'ENTRY', 'PROLOGUE'})))
    return 1


def fmt(loc):
    kind, name = loc
    if kind == 'sp':
        return 'sp+%#x (the caller\'s stack below its sp)' % name if False else 'sp+%#x' % name
    if kind == 'gpr':
        return '$%s top word' % name
    return '$%s.w' % name


if __name__ == '__main__':
    sys.exit(main())
