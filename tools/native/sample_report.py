#!/usr/bin/env python3
"""Summarise a B3_SAMPLE file (src/native/sampler.h): the game's work by host function.

    tools/native/sample_report.py <sample file> [--top N] [--binary MODULE=PATH ...]

The file's leaf section is "<module>\\t<offset hex>\\t<count>" per sampled instruction address. Each module's addresses
are named with addr2line (binutils; for a Windows build the MinGW one: `x86_64-w64-mingw32-addr2line`, or --addr2line
<toolchain>/bin/addr2line.exe, which from WSL gets the module's Windows path) on
the module itself, or on the PATH given with --binary (the same build of it, e.g. the unstripped copy of a reporter's
native_play.exe). Prints the file's own header and guest tables, then the host functions by self time (% of the
running samples).
"""
import argparse
import collections
import os
import shutil
import struct
import subprocess
import sys


def image_base(path):
    """The address addr2line wants added to a module offset: a PE's ImageBase, an ELF executable's first load address
    (0 for a position-independent one)."""
    with open(path, 'rb') as f:
        head = f.read(4096)
    if head[:2] == b'MZ':
        pe = struct.unpack_from('<I', head, 0x3C)[0]
        magic = struct.unpack_from('<H', head, pe + 24)[0]
        return struct.unpack_from('<Q', head, pe + 24 + 24)[0] if magic == 0x20B else struct.unpack_from('<I', head, pe + 24 + 28)[0]
    if head[:4] == b'\x7fELF' and head[4] == 2:
        e_type = struct.unpack_from('<H', head, 16)[0]
        if e_type != 2:   # ET_DYN: offsets as they are
            return 0
        phoff = struct.unpack_from('<Q', head, 32)[0]
        phentsize, phnum = struct.unpack_from('<HH', head, 54)
        for i in range(phnum):
            p_type, _, _, p_vaddr = struct.unpack_from('<IIQQ', head, phoff + i * phentsize)
            if p_type == 1:
                return p_vaddr
    return 0


TOOL = None   # --addr2line


def addr2line_for(path):
    if TOOL:
        return TOOL
    with open(path, 'rb') as f:
        pe = f.read(2) == b'MZ'
    for tool in (['x86_64-w64-mingw32-addr2line', 'addr2line'] if pe else ['addr2line']):
        if shutil.which(tool):
            return tool
    return None


def nm_for(tool):
    """nm beside the addr2line used (the same toolchain), else the system's"""
    if tool:
        d, b = os.path.split(tool)
        cand = os.path.join(d, b.replace('addr2line', 'nm'))
        if cand != tool and (os.path.exists(cand) or shutil.which(cand)):
            return cand
    return shutil.which('nm')


def symbol_names(path, offsets, tool):
    """A build without debug information (a Windows build: no -g): the nearest symbol at or below each address, from
    the symbol table (nm -n -C)"""
    nm = nm_for(tool)
    if not nm:
        return {}
    target = path
    if nm.lower().endswith('.exe') and path.startswith('/') and shutil.which('wslpath'):
        target = subprocess.run(['wslpath', '-w', path], capture_output=True, text=True).stdout.strip() or path
    syms = []
    for line in subprocess.run([nm, '-n', '-C', '--defined-only', target], capture_output=True, text=True).stdout.splitlines():
        parts = line.split(' ', 2)
        if len(parts) == 3 and parts[1].lower() in ('t', 'w') and parts[0]:
            try:
                syms.append((int(parts[0], 16), parts[2]))
            except ValueError:
                pass
    import bisect
    keys = [a for a, _ in syms]
    base = image_base(path)
    res = {}
    for o in offsets:
        i = bisect.bisect_right(keys, base + o) - 1
        if i >= 0:
            res[o] = syms[i][1]
    return res


def names(path, offsets):
    tool = addr2line_for(path)
    if not tool:
        return {o: '?' for o in offsets}
    base = image_base(path)
    addrs = ['%x' % (base + o) for o in offsets]
    target = path
    if tool.lower().endswith('.exe') and path.startswith('/') and shutil.which('wslpath'):   # a Windows tool from WSL
        target = subprocess.run(['wslpath', '-w', path], capture_output=True, text=True).stdout.strip() or path
    out = subprocess.run([tool, '-f', '-C', '-e', target], input='\n'.join(addrs) + '\n', capture_output=True, text=True).stdout.splitlines()
    res = {}
    for i, o in enumerate(offsets):
        fn = out[2 * i] if 2 * i < len(out) else '?'
        res[o] = fn if fn and fn != '??' else '?'
    if all(v == '?' for v in res.values()):   # no debug information: the symbol table
        res.update(symbol_names(path, offsets, tool))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file')
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--binary', action='append', default=[], help='MODULE=PATH: name MODULE\'s addresses from PATH')
    ap.add_argument('--addr2line', help='the addr2line to use (e.g. the MinGW toolchain\'s addr2line.exe for a Windows build)')
    a = ap.parse_args()
    global TOOL
    TOOL = a.addr2line
    subst = dict(b.split('=', 1) for b in a.binary)
    head, leaf, section = [], collections.defaultdict(dict), None
    running = 0
    for line in open(a.file, encoding='utf-8', errors='replace'):
        line = line.rstrip('\n')
        if line.startswith('## leaf'):
            section = 'leaf'
            continue
        if section == 'leaf' and line:
            mod, off, n = line.split('\t')
            leaf[mod][int(off, 16)] = int(n)
            continue
        head.append(line)
        if line.startswith('# samples'):
            running = int(line.split('running ')[1].split()[0])
    print('\n'.join(head))
    total = collections.Counter()
    for mod, offs in leaf.items():
        path = subst.get(mod, subst.get(os.path.basename(mod), mod))
        named = names(path, sorted(offs)) if os.path.exists(path) else {o: '?' for o in offs}
        short = os.path.basename(mod)
        for o, n in offs.items():
            fn = named[o]
            total[(fn if fn != '?' else '?') + '  [' + short + ']'] += n
    print('## host function, self (%% of the running samples, %d)' % running)
    for fn, n in total.most_common(a.top):
        print('%6.2f %8d %s' % (100.0 * n / max(running, 1), n, fn))


if __name__ == '__main__':
    sys.exit(main())
