#!/usr/bin/env python3
"""decomp2host: generate host-compilable C++ from decomp game source, unchanged (docs/decomp-on-host.md).

The decomp source is never edited. For each selected function this script reads its translation unit (and the decomp
headers it includes), writes a transformed copy into OUT, and generates the override wrappers and table:

  OUT/include/b3gen/prelude.h          included first by every generated file
  OUT/include/b3/...                   transformed copies of the decomp headers a unit includes (not GtMath: the port's
                                       shadow headers in include/gtmath serve those)
  OUT/units/<unit>.cpp                 the unit, reduced to the selected functions and the helpers they use
  OUT/include/b3port/game_overrides.h  B3PORT_GAME_OVERRIDES(X): X(name, address, code_bytes, fnv1a64, wrapper, track)
  OUT/game_overrides.cpp               the wrappers (guest calling convention -> the decomp function)
  OUT/manifest.tsv                     what was generated, from where

Transforms (all mechanical, on code outside comments and string literals):
  T1 `float` -> `ps2f` (b3host/ps2f.h: COP1 semantics; 4 bytes, so layouts hold)
  T2 MWCC's 64-bit `long` / `unsigned long` -> b3host::ps2_long / ps2_ulong (an LLP64 host's long is 32-bit); integer
     literals' L / UL suffixes -> LL / ULL (mac5)
  T3 pointer members of struct/class/union bodies -> b3host::gp<T> (4-byte guest pointer); function-pointer members
     -> b3host::gaddr. Locals and parameters stay host pointers (into the guest RAM window)
  T4 `extern T name;` data declarations -> #define name (b3host::guest_global<T>(address)); the address comes from the
     D_XXXXXXXX name or, for a named global, from the decomp's config/symbol_addrs_b3names.txt
  T5 out-of-line function definitions other than the selected ones are dropped (the `// 0xADDR` marker the decomp puts
     before each definition identifies them); inline helpers are kept only if a kept function uses them
Anything the script cannot transform safely (inline asm in kept code, a data definition, a static data member) makes
it fail with the location, never guess.

Usage: decomp2host.py --decomp DIR --out DIR --select FILE
  FILE: lines `<address hex> <track>`; track = matched (src/b3, listed in config/proven_units.txt), nonmatching
  (src/nonmatching/<mangled>.cpp) or rwa (audio1: this repo's own C for a retail library, src/libs/rwa/*.cpp, found by
  the `// 0xADDR` marker before the definition; its headers in src/libs/rwa/include). The function's mangled name and size come from the decomp's assembly/asm
  `nonmatching <name>, <size>` lines; its FNV-1a 64 from orig/SLUS_210.50.
"""
import argparse
import glob
import functools
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import eeasm  # noqa: E402

# audio1: retail library code ported as C in this repo (not the decomp's game code): track name -> directory under
# PORT_ROOT. Sources are written in the decomp's style (raw pointers, float, named guest globals) and transformed the same way.
PORT_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../.."))
PORT_TRACKS = {"rwa": "src/libs/rwa",
               # boot1: RenderWare 3.6 core as C (the RW plan of 2026-11-05), the same mechanism
               "rw": "src/libs/rw",
               # boot2: Sony EE libraries (libgraph / libdma / libkernl pieces) the sky2 driver reaches, as C through b3hw
               "sce": "src/libs/sce",
               # boot2: the Logitech USB libraries (lgdev: wheels; lgcodec / lgkbm later) as C, the same way
               "lg": "src/libs/lg"}

SHADOW_HEADERS = {"b3/GtMath.h", "b3/GtMathPs2.h", "b3/GtMathPs2Quat.h", "b3/GtMathPs2Trig.h"}


class GenError(Exception):
    pass


RAISE = False   # set by tools that try many selections (candidates.py): die() raises GenError instead of exiting


def die(msg):
    if RAISE:
        raise GenError(msg)
    sys.stderr.write("decomp2host: " + msg + "\n")
    sys.exit(1)


# ---------------------------------------------------------------- lexing helpers
@functools.lru_cache(maxsize=256)   # boot2: a pure function of the text, called by every transform on unchanged text
def mask(text):
    """Comments, string and char literals replaced by spaces (newlines kept), so offsets stay valid.
    gen1 (speed): jumps to the next `//`, `/*` or quote with a regex instead of a Python step per character (the same
    output: mask_slow is the old loop, tests/test_decomp2host.py compares them)."""
    out, pos, i, n = [], 0, 0, len(text)
    while True:
        mm = _MASK_RE.search(text, i)
        if not mm:
            break
        p, tok = mm.start(), mm.group()
        out.append(text[pos:p])
        if tok == "//":
            j = text.find("\n", p)
            j = n if j < 0 else j
            out.append(" " * (j - p))
            pos = i = j
        elif tok == "/*":
            j = text.find("*/", p + 2)
            j = n if j < 0 else j + 2
            out.append(_NOT_NL.sub(" ", text[p:j]))
            pos = i = j
        else:
            j = p + 1
            while j < n and text[j] != tok:
                j += 2 if text[j] == "\\" else 1
            e = min(j, n)
            out.append(tok + " " * max(0, e - (p + 1)))
            pos, i = e, j + 1
    out.append(text[pos:])
    return "".join(out)


_MASK_RE = re.compile(r"//|/\*|[\"']")
_NOT_NL = re.compile(r"[^\n]")


def mask_slow(text):
    """The character-by-character mask (the reference for mask's test)."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            for k in range(i + 1, min(j, n)):
                out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


# boot2: mask() is pure and the same headers are masked again and again (is_static_member, is_namespace: 30 s of a
# 600-function generation's 170 s were mask); memoised.
mask = __import__("functools").lru_cache(maxsize=16384)(mask)


def preproc_mask(m):
    """Also blank preprocessor lines (they are copied unchanged, includes handled separately).
    gen1 (speed): one regex substitution (a line whose first non-blank is `#`, blanked to its length; str.lstrip's
    whitespace is \\s's); preproc_mask_slow is the old per-line form, compared in the tests."""
    return _PP_LINE.sub(lambda mm: " " * len(mm.group()), m)


_PP_LINE = re.compile(r"^[^\S\n]*#[^\n]*", re.M)


def preproc_mask_slow(m):
    lines = m.split("\n")
    return "\n".join((" " * len(l)) if l.lstrip().startswith("#") else l for l in lines)


_CLASS_KW = re.compile(r"\b(?:class|struct)\s")
_CLASS_BASE = re.compile(r"\b(?:class|struct)\s+([A-Za-z_]\w*)\s*:\s*([^{;]*)\{")
_CLASS_OPEN = re.compile(r"\b(class|struct)\s+([A-Za-z_]\w*)\b[^;{]*\{")


@functools.lru_cache(maxsize=8192)
def _class_base_index(m):
    r"""gen1 (speed): {class name: its base list} for `class|struct NAME : bases {` in masked text m, the first one per
    name -- what re.search(r"\b(?:class|struct)\s+NAME\s*:\s*([^{;]*)\{", m) finds for every NAME at once (matched
    at every keyword, so overlapping candidates count as they did; an identifier is captured whole)."""
    out = {}
    for km in _CLASS_KW.finditer(m):
        mm = _CLASS_BASE.match(m, km.start())
        if mm and mm.group(1) not in out:
            out[mm.group(1)] = mm.group(2)
    return out


@functools.lru_cache(maxsize=8192)
def _class_open_index(s):
    r"""gen1 (speed): {class name: end of `class|struct NAME ... {`} in s, the first one per name (re.search of
    r"\b(class|struct)\s+NAME\b[^;{]*\{" for every NAME at once)."""
    out = {}
    for km in _CLASS_KW.finditer(s):
        mm = _CLASS_OPEN.match(s, km.start())
        if mm and mm.group(2) not in out:
            out[mm.group(2)] = mm.end()
    return out


def preproc_mask_keep_defines(m):
    """boot2: preproc_mask, but a #define's body stays visible (only `#define NAME(params)` is blanked), for rewrites a
    macro body needs as much as code does (T12: `#define RB (const CGtV2d *)CGt2dObject::mgu8RenderBatchData, ...` in
    CB3FlashText2dObject::_Render)."""
    out = []
    for l in m.split("\n"):
        if l.lstrip().startswith("#"):
            d = re.match(r"\s*#\s*define\s+\w+(?:\([^)]*\))?", l)
            l = (" " * d.end() + l[d.end():]) if d else " " * len(l)
        out.append(l)
    return "\n".join(out)


def code_blocks(m):
    """(start, end) of every outermost brace block that is not a namespace or `extern "C"` block (boot1): a C name's
    declarations live outside them, so a match inside one is a use (a call in a class's inline member, which
    top_level_functions does not list), not a declaration the guest_fn #define may follow."""
    out = []

    def scan(lo, hi):
        i, last = lo, lo
        while i < hi:
            c = m[i]
            if c == "{":
                head = m[last:i]
                j = match_brace(m, i)
                if re.search(r"\bnamespace\b[^;{}()]*$", head) or re.search(r"\bextern\s*\"[^\"]*\"\s*$", head):   # masked "C"
                    scan(i + 1, j)
                else:
                    out.append((i, j))
                i = last = j + 1
                continue
            if c in ";}":
                last = i + 1
            i += 1

    scan(0, len(m))
    return out


def match_paren(m, i):
    """Index of the `)` matching the `(` at i, or None."""
    d = 0
    for j in range(i, len(m)):
        if m[j] == "(":
            d += 1
        elif m[j] == ")":
            d -= 1
            if d == 0:
                return j
    return None


def match_brace(m, i):
    d = 0
    for j in range(i, len(m)):
        if m[j] == "{":
            d += 1
        elif m[j] == "}":
            d -= 1
            if d == 0:
                return j
    die("unbalanced braces")


# ---------------------------------------------------------------- the decomp's tables
def manual_symbols(text):
    """gen1: name -> address from the decomp's config/undefined_syms_manual.txt (linker script syntax: `Name = func_X;`,
    `Name = 0xADDR;`, `PROVIDE(Name = 0xADDR);`; placeholder names themselves are skipped, they carry their address)."""
    out = {}
    for mm in re.finditer(r"^[ \t]*(?:PROVIDE\(\s*)?([A-Za-z_]\w*)\s*=\s*(?:(?:func|D)_([0-9A-Fa-f]{8})|0x([0-9A-Fa-f]+))"
                          r"\s*\)?\s*;", mask(text), re.M):
        if not re.fullmatch(r"(?:func|D)_[0-9A-Fa-f]{8}", mm.group(1)):
            out.setdefault(mm.group(1), int(mm.group(2) or mm.group(3), 16))
    return out


class Decomp:
    def __init__(self, root):
        self.root = root
        self.funcs = {}  # addr -> (name, size)
        self.by_name = {}
        # boot2: splat's alternate entries `alabel func_XXXXXXXX` inside a function (unit_0022E200.s: the network
        # threads' receive / send loops 0x22E300 / 0x22E560 behind func_0022E200) are functions of their own, defined in
        # the parent's unit file: alt_parent[addr] = the parent's address; sizes run to the next label or the end
        self.alt_parent = {}
        # 2026-11-06: an `alabel D_<addr>` is a function too when the decomp's C defines a function by that
        # name (func_0022F800.cpp's `extern "C" void D_0022F890(UnkStruct_0022F890 *self)`, a proven thread entry)
        d_defs = set()
        dre = re.compile(r"\bD_([0-9A-Fa-f]{8})\s*\([^;{}]*\)\s*\{")
        for sf in glob.glob(os.path.join(root, "src/**/*.c*"), recursive=True):
            txt = open(sf, errors="replace").read()
            if "D_" in txt:
                d_defs.update(int(x, 16) for x in dre.findall(txt))
        for f in glob.glob(os.path.join(root, "assembly/asm/**/*.s"), recursive=True):
            name = None
            parent = None   # (address, size) of the function being read
            alts = []
            for line in open(f, errors="replace"):
                mm = re.match(r"^nonmatching (\S+), (0x[0-9A-Fa-f]+)", line)
                if mm:
                    name, size = mm.group(1), int(mm.group(2), 16)
                    continue
                if name:
                    ma = re.match(r"\s*/\* \w+ ([0-9A-F]{8}) ", line)
                    if ma:
                        a = int(ma.group(1), 16)
                        self.funcs[a] = (name, size)
                        self.by_name[name] = a
                        parent = (a, size)
                        name = None
                    continue
                ml = re.match(r"^\s*alabel ((?:func|D)_([0-9A-Fa-f]{8}))\s*$", line)
                if ml and parent and parent[0] < int(ml.group(2), 16) < parent[0] + parent[1] and \
                        (ml.group(1).startswith("func_") or int(ml.group(2), 16) in d_defs):
                    alts.append((int(ml.group(2), 16), ml.group(1), parent))
            for k, (a, n, (pa, psz)) in enumerate(alts):
                if a in self.funcs:
                    continue
                later = [b for b, _, (qa, _) in alts if qa == pa and b > a]
                self.funcs[a] = (n, (min(later) if later else pa + psz) - a)
                self.by_name.setdefault(n, a)
                self.alt_parent[a] = pa
        # Names -> addresses: the decomp's name table first, then its splat symbol file (config/symbol_addrs.txt: the
        # matching build links against it, so its addresses are the retail ones). native2: N298 moved d4 globals such
        # as theMemMgr / mpToolBase__Q27GtComms16CGtCommsToolBase / _gpWholeMemoryBlock to the second file only.
        self.symbols = {}
        func_syms = {}
        for fn in ("config/symbol_addrs_b3names.txt", "config/symbol_addrs.txt"):
            path = os.path.join(root, fn)
            if not os.path.exists(path):
                continue
            for line in open(path):
                mm = re.match(r"^(\S+) = (0x[0-9A-Fa-f]+);", line)
                if mm and mm.group(1) not in self.symbols:
                    self.symbols[mm.group(1)] = int(mm.group(2), 16)
                    if "type:func" in line:
                        func_syms.setdefault(int(mm.group(2), 16), mm.group(1))
        # gen1: the decomp's hand-written linker aliases (config/undefined_syms_manual.txt: `GetString__10CB3StringsFi =
        # func_00271A00;` where C calls a function by its recovered name whose body is still asm, `PROVIDE(name =
        # 0x...)`, data such as `kan8HeadingAwardIconImages = 0x004E1D20;`) as a third name source
        path = os.path.join(root, "config/undefined_syms_manual.txt")
        if os.path.exists(path):
            for k, v in manual_symbols(open(path).read()).items():
                self.symbols.setdefault(k, v)
        self.proven = []
        for line in open(os.path.join(root, "config/proven_units.txt")):
            if line.startswith("#") or not line.strip():
                continue
            p = line.strip().split(",")
            self.proven.append((int(p[0], 16), int(p[1], 16), p[4]))
        # Functions inside proven multi-function units that have no `nonmatching` line in assembly/asm (native2: 9, e.g.
        # Construct__20CB3MenuComponentBaseFi 0x3193B0 in B3MenuComponentBase.cpp): from the symbol files, sized to the
        # next function start or the unit's end.
        for a, n in sorted(func_syms.items()):
            if a in self.funcs:
                continue
            unit = [(u, s) for u, s, _ in self.proven if u <= a < u + s]
            if not unit:
                continue
            end = unit[0][0] + unit[0][1]
            nxt = [b for b in list(self.funcs) + list(func_syms) if a < b < end]
            self.funcs[a] = (n, (min(nxt) if nxt else end) - a)
            self.by_name.setdefault(n, a)
        self.elf = open(os.path.join(root, "orig/SLUS_210.50"), "rb").read()
        self.segments = []
        e_phoff, = struct.unpack_from("<I", self.elf, 0x1C)
        e_phnum, = struct.unpack_from("<H", self.elf, 0x2C)
        for k in range(e_phnum):
            p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<IIIII", self.elf, e_phoff + 32 * k)
            if p_type == 1:
                self.segments.append((p_vaddr, p_offset, p_filesz))

    def code(self, addr, size):
        for v, o, s in self.segments:
            if v <= addr and addr + size <= v + s:
                return self.elf[o + addr - v:o + addr - v + size]
        die("address %08X not in a PT_LOAD segment" % addr)

    def nonmatching_file(self, name):
        """src/nonmatching/<name>.cpp, else the archive whose first line is `// NONMATCHING: <name>`: long mangled names
        are archived under a shortened file name (<prefix>_<hash>.cpp), and the decomp's N298 renamed 51 symbols without
        renaming their archives (memMgrInit.cpp holds Construct__19CB3MemoryManagerPS2Fv) [C decomp snapshot]."""
        f = "src/nonmatching/%s.cpp" % name
        if os.path.exists(os.path.join(self.root, f)):
            return f
        if not hasattr(self, "_nm_index"):
            self._nm_index = {}
            for g in sorted(glob.glob(os.path.join(self.root, "src/nonmatching/*.cpp"))):
                with open(g, errors="replace") as fh:
                    mm = re.match(r"// NONMATCHING: (\S+)", fh.readline())
                if mm:
                    self._nm_index.setdefault(mm.group(1), "src/nonmatching/" + os.path.basename(g))
        return self._nm_index.get(name, f)

    def port_file(self, track, addr):
        """audio1: the file under PORT_TRACKS[track] whose `// 0xADDR` marker names addr (None if none)."""
        if not hasattr(self, "_port_index"):
            self._port_index = {}
        if track not in self._port_index:
            idx = {}
            for g in sorted(glob.glob(os.path.join(PORT_ROOT, PORT_TRACKS[track], "*.cpp"))):
                # boot1: a marker is a line of its own (a comment line that starts with an address is not one)
                for mm in re.finditer(r"^//\s*0x([0-9A-Fa-f]{8})[ \t]*$", open(g).read(), re.M):
                    idx.setdefault(int(mm.group(1), 16), os.path.relpath(g, PORT_ROOT))
            self._port_index[track] = idx
        return self._port_index[track].get(addr)

    def matched_file(self, addr):
        for a, s, f in self.proven:
            if a <= addr < a + s:
                return f
        return None



# The decomp's comments name the runs and tools they were checked with; some of those belong to projects this repo does
# not mention. Generated units carry the comment text through this rewrite (comments only: the code is untouched).
_COMMENT_REWRITES = [
    (re.compile(r"\b(the )?Burn\d checker\b"), "the checker"), (re.compile(r"\bBurn\d capture\b"), "a capture"),
    (re.compile(r"\bthe hyb" r"rid restore\b"), "a restore"), (re.compile(r"\bthe hos" r"ted stage\b"), "an earlier stage"),
    (re.compile(r"\bvu0, hyb" r"rid$"), "vu0,"), (re.compile(r"\bBurn\d\b"), "an earlier build"),
]


def scrub_comment_text(text):
    out = []
    for line in text.split("\n"):
        k = line.find("//")
        if k >= 0:
            head, tail = line[:k], line[k:]
            for rx, rep in _COMMENT_REWRITES:
                tail = rx.sub(rep, tail)
            line = head + tail
        out.append(line)
    return "\n".join(out)

def fnv1a64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


# ---------------------------------------------------------------- token transforms (T1, T2)
def token_transforms(text):
    # mac5 (the Windows garage static): a #define's body too -- func_0042A4E0's `#define CARID(i) (*b3host::pcast<unsigned
    # long>(...))` read a 32-bit car id on Windows (the 64-bit wanted id never matched: the car never loaded)
    m = preproc_mask_keep_defines(mask(text))
    edits = []
    for mm in re.finditer(r"\bunsigned\s+long\s+long\b|\blong\s+long\b|\bunsigned\s+long\b|\blong\b|\bfloat\b", m):
        tok = re.sub(r"\s+", " ", mm.group(0))
        rep = {"unsigned long long": None, "long long": None, "unsigned long": "b3host::ps2_ulong",
               "long": "b3host::ps2_long", "float": "ps2f"}[tok]
        if rep:
            edits.append((mm.start(), mm.end(), rep))
    # mac5 (the Windows race-start hang): an integer literal's L / UL suffix is MWCC's 64-bit long, but an LLP64 host's
    # long is 32-bit -- `1UL << 32` (the sky dome's GIF tag) was 0 and the DMA model misparsed the VIF1 stream. L -> LL,
    # UL / LU -> ULL (a float's L, `1.5L`, is left: no digit or '.' before the number, none after the suffix)
    for mm in re.finditer(r"(?<![\w.])(0[xX][0-9A-Fa-f]+|[0-9]+)([uU][lL]|[lL][uU]|[lL])(?![\w.])", m):
        suf = mm.group(2)
        rep = "ULL" if "u" in suf.lower() else "LL"
        edits.append((mm.start(2), mm.end(2), rep))
    return apply(text, edits)


def apply(text, edits):
    edits.sort()
    out, pos = [], 0
    for s, e, r in edits:
        if s < pos:
            die("overlapping edits")
        out.append(text[pos:s])
        out.append(r)
        pos = e
    out.append(text[pos:])
    return "".join(out)


# ---------------------------------------------------------------- aggregate bodies (T3)
AGG_HEAD = re.compile(r"\b(struct|class|union)\b[^;{}()]*$")


def aggregate_bodies(m):
    """(start, end) of every struct/class/union body (inside the braces), outermost first."""
    bodies = []
    i = 0
    stack = []
    last = 0
    for i, c in enumerate(m):
        if c == "{":
            head = m[last:i]
            stack.append((i, bool(AGG_HEAD.search(head))))
            last = i + 1
        elif c == "}":
            s, agg = stack.pop()
            if agg:
                bodies.append((s + 1, i))
            last = i + 1
        elif c == ";":
            last = i + 1
    return bodies


DECL = re.compile(r"^(\s*)((?:(?:const|volatile|struct|class|union|enum|unsigned|signed)\s+)*[A-Za-z_][\w:]*(?:\s*<[^;<>]*>)?)"
                  r"((?:\s*(?:const\s+)?\s*(?:\*\s*)+(?:const\s+)?[A-Za-z_]\w*(?:\s*\[[^\]]*\])*\s*,?)+)\s*$", re.S)
FNPTR = re.compile(r"^(\s*)[^;]*\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\([^;]*\)\s*$", re.S)


def member_transforms(text):
    m = preproc_mask(mask(text))
    edits = []
    bodies = aggregate_bodies(m)
    nested = [(s, e) for (s, e) in bodies]
    for s, e in bodies:
        # statements at this body's own depth
        depth, last = 0, s
        i = s - 1
        while i + 1 < e:
            i += 1
            c = m[i]
            if c == "{" and depth == 0 and re.search(r"\)\s*(?:const\s*)?$", m[last:i]) and not AGG_HEAD.search(m[last:i]):
                # gen1: an inline member function's definition (`CB3StreamedTrackLODAndCollisionData *func_00301340_inl1(int
                # lnUnit) { ... }`, no `;` after it): skipped whole, so the next member starts a statement of its own
                i = match_brace(m, i)
                last = i + 1
                continue
            if c in "{(":
                depth += 1
            elif c in "})":
                depth -= 1
            elif c == ";" and depth == 0:
                stmt = m[last:i]
                st = stmt.strip()
                # access specifiers end with ':'
                st_nolabel = re.sub(r"^(public|private|protected)\s*:", "", st).strip()
                off = last + stmt.find(st_nolabel) if st_nolabel else last
                am_ = re.match(r"^(struct|class|union)\b([^{;]*)\{", st_nolabel)
                if am_:
                    # gen1: an aggregate defined in the member's own declaration (`struct { ... } *field_0;`,
                    # `struct UnkStruct_00152AF0_vp { ... } *field_60;`, `struct { ... } field_30[2];`): its members are a
                    # body of their own; a pointer declarator becomes gp of the aggregate (an anonymous one is named)
                    cb = match_brace(st_nolabel, am_.end() - 1)
                    rest = st_nolabel[cb + 1:]
                    if rest.strip() and "*" in rest:
                        name = am_.group(2).strip()
                        ins = []
                        if not name:
                            name = "b3anon_%x" % off
                            ins.append((off + am_.end(1), off + am_.end(1), " " + name))
                        decls = []
                        for d_ in split_top(rest):
                            dd = re.match(r"^\s*((?:\*\s*)+)([A-Za-z_]\w*)((?:\s*\[[^\]]*\])*)\s*$", d_)
                            if not dd:
                                die("pointer member not understood: " + st_nolabel)
                            ty = name
                            for _ in range(dd.group(1).count("*")):
                                ty = "b3host::gp<%s>" % ty
                            decls.append("%s %s%s" % (ty, dd.group(2), dd.group(3)))
                        edits.extend(ins)
                        edits.append((off + cb + 1, off + len(st_nolabel), "; " + "; ".join(decls)))
                    last = i + 1
                    continue
                if re.match(r"^(?:(?:static|virtual|inline|explicit|const|unsigned|signed|struct|class)\s+)*[A-Za-z_][\w:<>,\s]*?[\s*&]+"
                            r"(?:operator\s*\S+?|~?[A-Za-z_]\w*)\s*\((?:[^()]|\([^()]*\))*\)\s*(?:const\s*)?(?:=\s*0\s*)?$", st_nolabel) and \
                        not re.match(r"^[^(]*\(\s*\*", st_nolabel):
                    # gen1: a member function declaration whose parameters include a function pointer
                    # (`void ForEveryUnitInSafeArea(void (*)(CB3StreamedTrackLODAndCollisionData *, int, void *), void *);`)
                    last = i + 1
                    continue
                pp = re.match(r"^(.*?)\(\s*\*\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\(([^()]*)\)\s*$", st_nolabel, re.S)
                if pp and "(" not in pp.group(1):
                    # gen1: a pointer to a function pointer (`int (**field_4C)(int, void *, void *);`): a guest pointer to a
                    # stored guest code address
                    edits.append((off, off + len(st_nolabel), "b3host::gp<b3host::gfn<%s(%s)>> %s" % (
                        pp.group(1).strip(), pp.group(3).strip(), pp.group(2))))
                    last = i + 1
                    continue
                if "{" in st_nolabel and re.match(r"^(struct|class|union|enum)\b[^{;]*\{", st_nolabel) and st_nolabel.rstrip().endswith("}"):
                    # native2: a nested aggregate definition (`struct X { const unsigned short *field_0; ... };`);
                    # its own members are visited as a body of their own
                    last = i + 1
                    continue
                if st_nolabel and "*" in re.sub(r"\[[^\]]*\]", "", st_nolabel) and not st_nolabel.startswith(("typedef", "using", "friend")):
                    fm = FNPTR.match(st_nolabel)
                    if st_nolabel.startswith("static") and not fm:
                        # native2: a static pointer member is not part of the layout; T12 makes it a guest global
                        # (b3host::gp<T> at its symbol's address) or, with no address, it stays an undefined host
                        # declaration (a link error: loud)
                        last = i + 1
                        continue
                    dm = DECL.match(st_nolabel)
                    am = re.match(r"^((?:const\s+|unsigned\s+|signed\s+|struct\s+|class\s+)*[A-Za-z_][\w:]*)\s*\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*(\[[^\]]*\])\s*$", st_nolabel)
                    if am:
                        # a pointer to an array (`unsigned char (*field_C)[2];`): one guest pointer to T[N]
                        edits.append((off, off + len(st_nolabel), "b3host::gp<%s%s> %s" % (am.group(1).strip(), am.group(3), am.group(2))))
                    elif "(*" in re.sub(r"\s+", "", st_nolabel) and not fm:
                        die("pointer member not understood: " + st_nolabel)
                    elif fm:
                        # T3/T14: a function-pointer member is a 4-byte guest code address, callable through
                        # call_guest (an enabled native override at the target runs directly)
                        pm = re.match(r"^(.*?)\(\s*\*\s*%s\s*\)\s*\((.*)\)\s*$" % re.escape(fm.group(2)), st_nolabel, re.S)
                        if pm and "(" not in pm.group(1) and "(*" not in pm.group(2):
                            edits.append((off, off + len(st_nolabel), "b3host::gfn<%s(%s)> %s" % (pm.group(1).strip(), pm.group(2).strip(), fm.group(2))))
                        else:
                            edits.append((off, off + len(st_nolabel), "b3host::gaddr %s" % fm.group(2)))
                    elif "(" not in re.sub(r"\[[^\]]*\]", "", st_nolabel) and dm:
                        # gen1: also when an array size holds parentheses (`T *field_122A10[(0x122B1C - 0x122A10) / 4];`,
                        # CheckCrashingTraffic 0x13E920: the member had stayed an array of 8-byte host pointers)
                        base = dm.group(2).strip()
                        decls = []
                        for d in dm.group(3).split(","):
                            d = d.strip()
                            if not d:
                                continue
                            dd = re.match(r"((?:const\s+)?\s*(?:\*\s*)+(?:const\s+)?)([A-Za-z_]\w*)((?:\s*\[[^\]]*\])*)$", d)
                            if not dd:
                                die("pointer member not understood: " + st_nolabel)
                            stars = dd.group(1).count("*")
                            t = base
                            for _ in range(stars):
                                t = "b3host::gp<%s>" % t
                            decls.append("%s %s%s" % (t, dd.group(2), dd.group(3)))
                        edits.append((off, off + len(st_nolabel), "; ".join(decls)))
                    elif "(" not in re.sub(r"\[[^\]]*\]", "", st_nolabel):
                        die("pointer member not understood: " + st_nolabel)
                last = i + 1
            elif c == "{" or c == "}":
                pass
        # nested bodies are visited separately (their statements are at depth 0 of their own body)
    # drop edits inside nested bodies that a parent also saw (a parent never edits inside braces: depth > 0)
    return apply(text, edits)


OPEN1 = re.compile(r"^((?:(?:public|private|protected)\s*:\s*)*)([^;(),\[\]={}*&]+?)\s+([A-Za-z_]\w*)\s*\[\s*(?:1|0x1)\s*\]$", re.S)


def open_array_transforms(text, report):
    """T18 (port13): a data member declared with bound 1 (`T field_0[1];`, the decomp's form for an array of unknown
    length, indexed past it as MWCC allows) -> `b3host::open_array<T> field_0;` (b3host/gp.h: T[1]'s layout, indexing
    through a pointer GCC cannot bound). Interior ones were miscompiled (CAsyncLoadManager::Update, 0x13D250: GCC
    assumed every index was the one it had just stored through); trailing ones get the same form."""
    m = preproc_mask(mask(text))
    edits = []
    for s, e in aggregate_bodies(m):
        depth, last = 0, s
        for i in range(s, e):
            c = m[i]
            if c in "{(":
                depth += 1
            elif c in "})":
                depth -= 1
            elif c == ";" and depth == 0:
                stmt = m[last:i]
                st = stmt.strip()
                om = OPEN1.match(st)
                if om and not re.match(r"^(static|typedef|using|friend|extern|enum)\b", om.group(2).strip()):
                    off = last + stmt.find(st)
                    edits.append((off, off + len(st), "%sb3host::open_array<%s> %s" % (
                        text[off:off + len(om.group(1))], text[off + om.start(2):off + om.end(2)].strip(), om.group(3))))
                last = i + 1
    if edits:
        report.append("T18 open arrays %d" % len(edits))
    return apply(text, edits)


# ---------------------------------------------------------------- globals (T4)
# boot2: `long long` too (`extern unsigned long long skyFrame_1;`: a 64-bit GS register shadow)
EXTERN = re.compile(r"^[ \t]*extern\s+(?:\"[ C]\"\s+)?((?:const\s+|volatile\s+|struct\s+|class\s+|unsigned\s+|signed\s+|long\s+(?=long\b))*"
                    r"[A-Za-z_][\w:]*(?:\s*<[^;]*>)?)\s*(\**)\s*(const\s+)?([A-Za-z_]\w*)\s*((?:\[[^\]]*\]\s*)+)?;[^\n]*$", re.M)


EXTERN_MULTI = re.compile(r"^([ \t]*extern\s+(?:\"[ C]\"\s+)?(?:(?:const\s+|volatile\s+|struct\s+|class\s+|unsigned\s+|signed\s+|long\s+(?=long\b))*"
                          r"[A-Za-z_][\w:]*))\s+((?:\**\s*[A-Za-z_]\w*\s*(?:\[[^\]]*\])?\s*,\s*)+\**\s*[A-Za-z_]\w*\s*(?:\[[^\]]*\])?)\s*;",
                          re.M)


def split_multi_externs(text):
    """`extern T a, *b, c[4];` -> one declaration per line (native2: 82 functions declare several D_ globals in one
    line; T4 rewrites single declarations only). gen1: several `extern ...;` statements on one line are split onto
    lines of their own first (B3Im3DPS2_00365040's line 17 had kept only its first, losing farFogPlane / D_004E0E50)."""
    m0 = preproc_mask(mask(text))
    cuts = [mm.start() + 1 for mm in re.finditer(r";[ \t]+(?=extern\b)", m0)]
    if cuts:
        text = apply(text, [(c, c + len(re.match(r"[ \t]*", text[c:]).group(0)), "\n") for c in cuts])
    m = preproc_mask(mask(text))
    edits = []
    for mm in EXTERN_MULTI.finditer(m):
        if "(" in mm.group(0):
            continue
        base = text[mm.start(1):mm.end(1)]
        decls = [d.strip() for d in text[mm.start(2):mm.end(2)].split(",")]
        edits.append((mm.start(), mm.end(), "\n".join("%s %s;" % (base, d) for d in decls)))
    return apply(text, edits) if edits else text


def global_transforms(text, decomp, report):
    text = split_multi_externs(text)
    # gen1: `extern struct { unsigned char field_0; } D_004E2D3C;` (an anonymous struct type, func_002E7D20): the type is
    # named so T4 below can make it a guest global
    mm_ = preproc_mask(mask(text))
    ed_ = []
    for am in re.finditer(r"^([ \t]*)extern\s+(struct|union)\s*\{", mm_, re.M):
        cb = match_brace(mm_, am.end() - 1)
        dm_ = re.match(r"\s*([A-Za-z_]\w*)\s*((?:\[[^\]]*\]\s*)*);", mm_[cb + 1:])
        if dm_:
            nm_ = "b3anon_" + dm_.group(1)
            ed_.append((am.start(), cb + 1 + dm_.end(), "%s%s %s %s;\n%sextern %s %s%s;" % (
                am.group(1), am.group(2), nm_, text[am.end() - 1:cb + 1], am.group(1), nm_, dm_.group(1), dm_.group(2))))
    if ed_:
        text = apply(text, ed_)
    m = preproc_mask(mask(text))
    edits, arrays = [], []
    for mm in EXTERN.finditer(m):
        # gen1: `T *const X[]` (an array of const pointers) and arrays of arrays (`extern float D_004B71B0[][5];`,
        # `extern char D_01E7C8C0[4][64];`: the element is a row, T4's array a pointer to rows)
        t, stars, cptr, name, arr = mm.group(1).strip(), mm.group(2), mm.group(3), mm.group(4), mm.group(5)
        if cptr and not stars:
            # gen1: `T const X[]` (const elements: `CGt2dObject::UnkFn_RenderCallback const mgapfRenderFunctions__...[]`)
            t, cptr = "const " + t, None
        # D_XXXXXXXX, or a named prefix + address (vu_game_00478260); boot1: splat also names low addresses with fewer
        # digits (main 0x12EB30: D_10000 = 0x10000, D_1FF0000 = 0x1FF0000 from %hi/%lo pairs [C decomp asm])
        md = re.match(r"(?:D|\w+)_([0-9A-Fa-f]{8})$", name) or re.match(r"D_([0-9A-Fa-f]{1,7})$", name)
        if md:
            addr = int(md.group(1), 16)
        elif name in decomp.symbols:
            addr = decomp.symbols[name]
        else:
            die("extern %s: no address (not a D_ name, not in symbol_addrs_b3names.txt)" % name)
        # T1/T2 for the type here: the #define lines below are skipped by the token transforms (port7: a
        # `const unsigned long` global stayed a host 32-bit long on Windows, a `float` global used host float math)
        t = re.sub(r"\bfloat\b", "ps2f", t)
        t = re.sub(r"\bunsigned\s+long\b(?!\s+long)", "b3host::ps2_ulong", t)
        t = re.sub(r"(?<!long )(?<!unsigned )\blong\b(?!\s+long)", "b3host::ps2_long", t)
        for _ in range(len(stars)):
            t = "b3host::gp<%s>" % t
        if cptr:
            t = "const " + t
        if arr is not None and len(re.findall(r"\[", arr)) > 1:
            t += "".join("[%s]" % d.strip() for d in re.findall(r"\[([^\]]*)\]", arr)[1:])
        if arr is not None:
            rep = "#define %s (&b3host::guest_global<%s>(0x%08Xu))   // decomp2host T4: guest array" % (name, t, addr)
        else:
            rep = "#define %s (b3host::guest_global<%s>(0x%08Xu))   // decomp2host T4: guest global" % (name, t, addr)
        edits.append((mm.start(), mm.end(), rep))
        report.append("global %s -> 0x%08X" % (name, addr))
        if arr is not None:
            arrays.append(name)
    text = apply(text, edits)
    # boot1: `D_x = D_x;` with D_x an array: MWCC accepts it and it has no effect (the matched func_004D8D50 0x4D8D50
    # [C its asm has no copy]); on the host an array (T4: its address) is not assignable, so the statement is dropped
    for name in arrays:
        text, n = re.subn(r"(?<![\w.>])%s\s*=\s*%s\s*;" % (re.escape(name), re.escape(name)),
                          "/* decomp2host: %s = %s; (an array assigned to itself: no effect) */;" % (name, name), text)
        if n:
            report.append("array self-assignment %s dropped (%d)" % (name, n))
        # boot2: `&D_x` of an array (C: the array's address, the same value as the first element's; func_003999E0
        # `func_00398470(&D_01E9FC00)`): T4 already made the name that address
        mm = mask(text)   # gen1: macro bodies too (`#define SPF(off) (... (char *)&_grSpikeGen + (off) ...)`, 0x1D2210)
        own = [(a.start(), a.end()) for a in re.finditer(r"^[ \t]*#\s*define\s+%s\b[^\n]*" % re.escape(name), mm, re.M)]
        e3 = [(a.start(), a.end(), name) for a in re.finditer(r"(?<![&\w])&\s*%s\b(?!\s*\[)" % re.escape(name), mm)
              if not any(s_ <= a.start() < e_ for s_, e_ in own)]
        if e3:
            text = apply(text, e3)
            report.append("&%s of an array -> its address (%d)" % (name, len(e3)))
    return text


# ---------------------------------------------------------------- function selection (T5)
def preproc_mask_continued(m):
    """gen1: preproc_mask, with a directive's backslash-continued lines too (a multi-line #define's last line had been
    read as the head of the next function: func_003A9B70's ZOOM macro made its helper func_003A9B70_inl1 a
    "Prepare__16CGt2dObjectImage..." helper, dropped)."""
    lines = m.split("\n")
    out, cont = [], False
    for l in lines:
        if cont or l.lstrip().startswith("#"):
            out.append(" " * len(l))
            cont = l.rstrip().endswith("\\")
        else:
            out.append(l)
            cont = False
    return "\n".join(out)


def top_level_functions(text):
    """(start, body_start, body_end, marker_addr, head) of top-level function definitions, recursing into namespaces.
    gen1 (speed): memoised per text (a pure function of it; a new list each call, the callers edit theirs)."""
    return list(_top_level_functions(text))


_SCAN_STOP = re.compile(r"[{;}]")


@functools.lru_cache(maxsize=4096)
def _top_level_functions(text):
    m = preproc_mask_continued(mask(text))
    out = []

    def scan(lo, hi):
        i, last = lo, lo
        while i < hi:
            sm = _SCAN_STOP.search(m, i, hi)   # gen1 (speed): every other character only advanced i
            if not sm:
                break
            i = sm.start()
            c = m[i]
            if c == "{":
                head = m[last:i]
                j = match_brace(m, i)
                if re.search(r"\bnamespace\b[^;{}()]*$", head) or re.search(r"\bextern\s*\"[^\"]*\"\s*$", head):
                    scan(i + 1, j)   # gen1: also `extern "C" { ... }` (masked "C"): its definitions are top level
                elif ")" in head and not AGG_HEAD.search(head) and not re.search(r"\benum\b", head) and "=" not in head.split(")")[-1]:
                    st = last
                    while st < i and m[st].isspace():
                        st += 1
                    # the decomp's `// 0xADDR` marker in the comment lines just above
                    pre = text[max(0, st - 400):st]
                    ma = re.findall(r"//\s*0x([0-9A-Fa-f]{8})\s*\n", pre)
                    marker = None
                    if ma:
                        tail = pre[pre.rfind("// 0x" + ma[-1]) if ("// 0x" + ma[-1]) in pre else 0:]
                        if m[st - len(tail):st].strip() == "":
                            marker = int(ma[-1], 16)
                    out.append((st, i, j, marker, head.strip()))
                i = j + 1
                last = i
                continue
            if c == ";" or c == "}":
                last = i + 1
            i += 1

    scan(0, len(m))
    return tuple(out)


def t7_reopen(mo, declared=lambda n: False):
    """T7: for each namespace the unit reopens and also uses (`using namespace N; namespace N { helper }`): the lines
    opening the unit namespace with (keep the global N and the nested one visible) and, gen1, `namespace N {}` at global
    scope before it, so ::N exists even when no header declares it (func_003CD920 defines its own GtMathPs2 view of
    CGtV2d and includes no GtMath header: `using namespace ::GtMathPs2` was an error)."""
    reopen, glob_ns = "", ""
    for nm_ in sorted(set(re.findall(r"^[ \t]*namespace\s+([A-Za-z_]\w*)\s*\{", mo, re.M))):
        if re.search(r"\busing\s+namespace\s+%s\s*;" % re.escape(nm_), mo):
            reopen += "using namespace ::%s; namespace %s {} using namespace %s;   // decomp2host T7: reopened\n" % (nm_, nm_, nm_)
            if not declared(nm_):   # only when nothing the unit includes declares it (no change elsewhere)
                glob_ns += "namespace %s {}   // decomp2host T7: declared at global scope for the reopening below\n" % nm_
    return reopen, glob_ns


def t7_forward_declarations(text, at):
    """gen1 (T7): forward declarations above `at` (the T7 namespace's start, after the last #include) of a struct the
    unit defines below it (`struct UnkStruct_004617B0;` above `#include "b3/GtMathPs2.h"`, func_004617B0) leave the
    global scope: (text without them, the new `at`, the declarations to open the namespace with). Left global, the
    name was ambiguous after `using namespace` (an incomplete ::X and the namespace's X)."""
    mo = mask(text)
    fwd, pre = "", text[:at]
    for fm in reversed(list(re.finditer(r"^[ \t]*(?:struct|class|union)\s+([A-Za-z_]\w*)\s*;[ \t]*\n", pre, re.M))):
        if re.search(r"\b(?:struct|class|union)\s+%s\b[^;]*\{" % re.escape(fm.group(1)), mo[at:]) and \
                mo[fm.start():fm.end()] == pre[fm.start():fm.end()]:
            fwd = fm.group(0).lstrip() + fwd
            pre = pre[:fm.start()] + pre[fm.end():]
    return pre + text[at:], len(pre), fwd


FNPTR_PARAM = re.compile(r"^(.*?)\(\s*\*\s*([A-Za-z_]\w*)?\s*\)\s*\((.*)\)\s*$", re.S)


def fnptr_params(params):
    """gen1 (T36): a parameter list with each function-pointer parameter `R (*name)(Args)` (or unnamed) as
    `b3host::gfn<R(Args)> name`; None when it has none."""
    out, changed = [], False
    for prm in split_top(params):
        pm = FNPTR_PARAM.match(prm.strip())
        if pm and "(" not in pm.group(1):
            out.append(("b3host::gfn<%s(%s)> %s" % (pm.group(1).strip(), pm.group(3).strip(), pm.group(2) or "")).rstrip())
            changed = True
        else:
            out.append(prm.strip())
    return ", ".join(out) if changed else None


def fnptr_param_transforms(text, keep, report):
    """T36 (gen1): a kept definition's function-pointer parameters (CGtLobbyPS2DirtySock::ForAllLobbys' callback,
    IntersectLineWithUnit's result filter) become b3host::gfn<R(Args)>, in the definition and in the unit's declarations
    of that name (a class body's member, a prototype): the wrapper hands it the caller's guest code address and a call
    through it is a guest call (dispatch runs a native override); a host function passed from native code converts to
    its registered guest address. Before, the wrapper could not pass it (b3host::by_value of a function pointer)."""
    m = preproc_mask(mask(text))
    names = set()
    edits = []
    fns = top_level_functions(text)
    for f in fns:
        nm = re.search(r"([A-Za-z_]\w*)\s*\(", f[4])
        if not nm or not (f[3] in keep or (f[3] is None and nm.group(1) in keep.values())):
            continue
        head_m = m[f[0]:f[1]]
        po = f[0] + re.search(r"\b%s\s*\(" % re.escape(nm.group(1)), head_m).end() - 1
        pc = match_paren(m, po)
        rep = fnptr_params(text[po + 1:pc])
        if rep is None:
            continue
        edits.append((po + 1, pc, rep))
        names.add(nm.group(1))
    if not names:
        return text
    bodies = [(f[1], f[2]) for f in fns]
    for name in names:
        for dm in re.finditer(r"\b%s\s*\(" % re.escape(name), m):
            po = dm.end() - 1
            if any(a <= po <= b for a, b in bodies) or any(s <= po < e for s, e, _ in edits):
                continue
            pc = match_paren(m, po)
            if not re.match(r"\s*(?:const\s*)?(?:=\s*0\s*)?;", m[pc + 1:]):
                continue   # a declaration ends at its `;` (a call or a definition does not)
            rep = fnptr_params(text[po + 1:pc])
            if rep is not None:
                edits.append((po + 1, pc, rep))
    # and, in such a unit, the function-pointer parameters of its extern "C" prototypes (guest callees): a gfn received
    # by the kept definition is handed on (IntersectLineWithUnit -> IntersectLine__18CGtCollisionKDTree...); a host
    # function passed there converts to its registered guest address, as stub_arg's code_address did
    for pm_ in re.finditer(r'\bextern\s*"[ C]"\s*[^;{}()]*?\b([A-Za-z_]\w*)\s*\(', m):
        po = pm_.end() - 1
        if any(a <= po <= b for a, b in bodies) or any(s <= po < e for s, e, _ in edits):
            continue
        pc = match_paren(m, po)
        if not re.match(r"\s*;", m[pc + 1:]):
            continue
        rep = fnptr_params(text[po + 1:pc])
        if rep is not None:
            edits.append((po + 1, pc, rep))
    report.append("T36 function-pointer parameters: %s" % ", ".join(sorted(names)))
    return apply(text, edits)


DECOMP_ROOT = [None]


def t7_header_names(text, decomp_root):
    """gen1 (T7), compiler-guided (compile_for_stubs, a unit whose namespace's copy of a header name does not match the
    header's): names the unit's includes already declare at global scope keep meaning those.
    (a) A forward declaration inside the unit's namespace (`class CGtTexture;`, no definition in the unit) of a class an
        included header declares moves above the namespace: inside it, it was a second, incomplete CGtTexture that
        Gt2dRenderer::SetTexture(::CGtTexture *) did not take (func_003D62A0, the C2dObjectBoostBar renderers).
    (b) A namespace the unit opens that an included header also declares (`namespace Gt2dRenderer { ... }` beside
        Gt2dObject.h's) gets `using namespace ::X;` inside the unit's X: a qualified call to a function only the
        header's X declares (`Gt2dRenderer::SetTexture`, func_00381030) finds it; call_stubs then writes such stubs as
        `::X::f`.
    Compiler-guided because a global forward declaration shares template instantiations (PtrArg<T>) with units where T
    is complete: only units that do not compile otherwise change."""
    ns = re.search(r"^namespace (b3gen_\w+) \{\n", text, re.M)
    if not ns:
        return text
    at = ns.end()
    hdr = "\n".join(open(os.path.join(decomp_root, "include", r), errors="replace").read()
                    for r in sorted(include_closure(type("D", (), {"root": decomp_root}), text)))
    if not hdr:
        return text
    hm = preproc_mask(mask(hdr))
    m = preproc_mask(mask(text))
    end = text.rfind("}  // namespace %s" % ns.group(1))
    glob, edits, d, depth0 = "", [], 0, []
    for i in range(at, end):
        c = m[i]
        if c == "{":
            d += 1
        elif c == "}":
            d -= 1
        depth0.append(d == 0)
    for fm in re.finditer(r"^[ \t]*(?:class|struct)\s+([A-Za-z_]\w*)\s*;[ \t]*\n", m[at:end], re.M):
        s_, e_, name = at + fm.start(), at + fm.end(), fm.group(1)
        if not depth0[fm.start()] or re.search(r"\b(?:class|struct|union)\s+%s\b[^;(){}]*\{" % re.escape(name), m):
            continue
        if re.search(r"\b(?:class|struct)\s+%s\b" % re.escape(name), hm):
            glob += text[s_:e_].lstrip()
            edits.append((s_, e_, ""))
    # (c) gen1: a struct the unit DEFINES whose name an included header declares at global scope (UnkStruct_D_0055EFEC.h's
    #     `struct UnkStruct_00395A00_a1;`, its vfunc_14 parameter; B3StageLogic's ResetCarDamage 0x395A00): the
    #     definition completes the header's type, so it moves above the namespace -- when its body names none of the
    #     unit's own types -- and the unit's forward declarations of it go
    unit_types = set(re.findall(r"\b(?:class|struct|union|enum)\s+([A-Za-z_]\w*)", m[at:end])) | set(
        re.findall(r"\btypedef\b[^;]*?\b([A-Za-z_]\w*)\s*;", m[at:end]))
    for dm in re.finditer(r"^[ \t]*(?:class|struct)\s+([A-Za-z_]\w*)\s*(?::[^{;]*)?\{", m[at:end], re.M):
        name, s_ = dm.group(1), at + dm.start()
        if not depth0[dm.start()] or not re.search(r"\b(?:class|struct)\s+%s\s*;" % re.escape(name), hm):
            continue
        e_ = match_brace(m, at + dm.end() - 1)
        semi = m.find(";", e_)
        if e_ is None or semi < 0 or m[e_ + 1:semi].strip():
            continue
        body = m[at + dm.end():e_]
        if set(re.findall(r"[A-Za-z_]\w*", body)) & (unit_types - {name}):
            continue
        glob += text[s_:semi + 1].strip() + "   // decomp2host T7 (gen1): completes the headers' %s\n" % name
        edits.append((s_, semi + 1, ""))
        for fm in re.finditer(r"^[ \t]*(?:class|struct)\s+%s\s*;[ \t]*$" % re.escape(name), m[at:end], re.M):
            if not any(a_ <= at + fm.start() < b_ for a_, b_, _ in edits):
                edits.append((at + fm.start(), at + fm.end(), ""))
    reopen2 = ""
    for nm_ in sorted(set(re.findall(r"^[ \t]*namespace\s+([A-Za-z_]\w*)\s*\{", m[at:end], re.M))):
        if "namespace ::%s;" % nm_ in text[at:at + 2000] or nm_.startswith("b3gen_"):
            continue
        if re.search(r"\bnamespace\s+%s\s*\{" % re.escape(nm_), hm):
            reopen2 += "namespace %s { using namespace ::%s; }   // decomp2host T7 (gen1): the headers' %s too\n" % (nm_, nm_, nm_)
    if not glob and not reopen2:
        return text
    edits.append((at, at, reopen2))
    edits.append((ns.start(), ns.start(), glob))
    return apply(text, edits)


def include_closure(decomp, text, seen=None):
    """gen1: the decomp headers a text includes, transitively (relative paths under the decomp's include/)."""
    seen = set() if seen is None else seen
    for rel in re.findall(r'^[ \t]*#\s*include\s+"([^"]+)"', text, re.M):
        path = os.path.join(decomp.root, "include", rel)
        if rel in seen or not os.path.exists(path):
            continue
        seen.add(rel)
        include_closure(decomp, open(path, errors="replace").read(), seen)
    return seen


def header_c_linkage(text, decomp, keep_addrs, report):
    """gen1: a kept free definition without `extern "C"` whose name a header of the unit declares `extern "C"`
    (B3MenuComponentBase.h: `extern "C" bool func_00319330(CB3MenuComponentSelectionData *self);`, defined in
    B3MenuComponentBase_003192F0.cpp as `bool func_00319330(...) {`) gets the header's C linkage, as MWCC gives it
    (the earlier declaration's linkage holds). Inside the T7 namespace it was another, C++, function, and the
    wrapper's call was ambiguous between the two; extern "C" declarations in any namespace name one function."""
    hdr = "\n".join(open(os.path.join(decomp.root, "include", r), errors="replace").read()
                    for r in sorted(include_closure(decomp, text)))
    if 'extern' not in hdr:
        return text
    m = preproc_mask(mask(text))
    blocks = [(i, match_brace(m, i)) for i in (mm.end() - 1 for mm in re.finditer(r'\bextern\s*"[^"]*"\s*\{', m))]
    edits = []
    for f in top_level_functions(text):
        nm = re.search(r"([A-Za-z_]\w*)\s*\(", f[4])
        if not nm or not (f[3] in keep_addrs or (f[3] is None and nm.group(1) in keep_addrs.values())) or \
                "::" in f[4] or re.match(r'\s*extern\s*"', m[f[0]:f[1]]) or any(s < f[0] < e for s, e in blocks):
            continue
        if re.search(r'\bextern\s*"[ C]"[^;{}()]*\b%s\s*\(' % re.escape(nm.group(1)), mask(hdr)):   # masked "C"
            edits.append((f[0], f[0], 'extern "C" '))
            report.append("C linkage from the header: %s" % nm.group(1))
    return apply(text, edits) if edits else text


def cxx_name(mangled):
    """Class::Name for a simple MWCC member mangling Name__<len>Class..., else the name itself. boot2: a nested class
    (Name__Q<n><len>A<len>B...: Construct__Q216CB3ParticleArray15CB3ParticleBankFUi) -> A::B::Name."""
    mq = re.match(r"^(\w+?)__Q([2-9])(\d.*)$", mangled)
    if mq:
        rest, parts = mq.group(3), []
        for _ in range(int(mq.group(2))):
            ml = re.match(r"^(\d+)", rest)
            if not ml or len(rest) < len(ml.group(1)) + int(ml.group(1)):
                return mangled
            n = int(ml.group(1))
            parts.append(rest[len(ml.group(1)):len(ml.group(1)) + n])
            rest = rest[len(ml.group(1)) + n:]
        return "::".join(parts + [mq.group(1)])
    mm = re.match(r"^(\w+?)__(\d+)([A-Za-z_]\w*)", mangled)
    if mm and len(mm.group(3)) >= int(mm.group(2)):
        return "%s::%s" % (mm.group(3)[:int(mm.group(2))], mm.group(1))
    return mangled


T41_PAIR = re.compile(r"\n([ \t]*)u_long128 (\w+) = ([^;\n]+);\n[ \t]*u_long128 (\w+) = ([^;\n]+);")


def t41_box_pairs(text):
    """1.1.1 T41 (see its call): two adjacent u_long128 locals whose first is passed by address to a function taking a
    CGtAxisAlignedBox -> one array of two, the names kept as references. Returns (text, count)."""
    n = 0

    def repl(m):
        nonlocal n
        ind, a, ea, b, eb = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
        if not re.search(r"\w*AxisAlignedBox\w*\s*\([^;]*&\s*%s\b" % re.escape(a), text):
            return m.group(0)
        n += 1
        return ("\n%su_long128 b3_box_%s[2] = {%s, %s}; u_long128 &%s = b3_box_%s[0], &%s = b3_box_%s[1]; "
                "/* decomp2host T41: one array (max after min) */" % (ind, a, ea, eb, a, a, b, a))

    return T41_PAIR.sub(repl, text), n


# 1.2.0 T46: a local array a draft declares smaller than retail's frame holds, indexed by a count read at run time.
# MWCC's frame had room for every index, so retail never overran; on the host the extra elements land on whatever the
# compiler put next to the array (Apple clang: its stack-protector guard, __stack_chk_fail; GCC: other temporaries).
# Function address -> [(element type (a regex: the unit may hold it as `float` or T1's `ps2f`), name, the draft's size,
# retail's)]; the declaration is checked, so a decomp that changes it (or fixes it: then the entry is left over and stops
# the generation) is noticed.
T46_LOCAL_ARRAYS = {
    # CB3VehiclePhysics::UpdateSuspension: the bump impulses, laImpulse[lnWheel] for lnWheel < mnNumWheels (+0x1169: 6
    # for the six-wheeled vehicles, PVEH/HEVY CAR7 / CAR8 / CAR10.BGV and the traffic CAR22 / CAR23.BTV). Retail keeps them at sp+0x910 (sqc2 0x910(v1), v1 = sp + 16*wheel, 0x1CD508), six
    # slots to 0x96F (0x970 unused, the next local at sp+0x980, frame 0x990); Revenge's STABS: `CGtV3d laImpulse[6]`
    # (UpdateSuspension__17CB4VehiclePhysicsf).
    # The application loop stays `< 4`, as retail's (slti 4, 0x1CCF00). Mac crash 2026-11-18 (stack buffer overflow).
    0x001CC900: [("CGtV3d", "laImpulse", 4, 6)],
    # func_002B7DB0 (a convex hull's penetration along a direction, from func_002B7F70: the vehicle hull against the
    # world): d[i] for i < the hull's plane count (+0x19; CB3ConvexHullVehicle::Initialise 0x2B96A0 reserves 40, and 78
    # of the 107 PVEH hulls have 22..40). Retail: sw 0x70(a2), a2 = sp + 4*i (0x2B7E34), 44 floats to the next local at
    # sp+0x120 (frame 0x130), as its twin func_002B7BF0's d[44]. GCC (x86-64) keeps d[i] in a register (it is read only
    # in its own iteration: no d[] in the frame), so no build has been seen to overrun; still undefined past 20.
    0x002B7DB0: [("float|ps2f", "d", 20, 44)],
}


def t46_local_arrays(text, report=None):
    """1.2.0 T46 (see T46_LOCAL_ARRAYS): in the marked function of each entry, `T name[draft];` -> `T name[retail];`.
    Returns (text, count)."""
    n = 0
    for addr, arrays in T46_LOCAL_ARRAYS.items():
        mk = re.search(r"^//\s*0x%08X\b.*$" % addr, text, re.M | re.I)
        if not mk:
            continue
        end = re.search(r"^\}", text[mk.end():], re.M)
        if not end:
            die("T46: no end of the function marked 0x%08X" % addr)
        lo, hi = mk.end(), mk.end() + end.end()
        body = text[lo:hi]
        for typ, name, have, want in arrays:
            pat = re.compile(r"\b(%s)\s+%s\s*\[\s*%d\s*\]\s*;" % (typ, re.escape(name), have))
            if len(pat.findall(body)) != 1:
                die("T46: 0x%08X no longer declares `%s %s[%d];` once (decomp changed it? update T46_LOCAL_ARRAYS)" %
                    (addr, typ, name, have))
            body = pat.sub(lambda m: "%s %s[%d];   /* decomp2host T46: retail's frame holds %d (the draft: %d) */" %
                           (m.group(1), name, want, want, have), body)
            n += 1
            if report is not None:
                report.append("T46 %08X: %s[%d] -> [%d]" % (addr, name, have, want))
        text = text[:lo] + body + text[hi:]
    return text, n


# 1.2.1 T47: a statement a draft gets wrong against retail's code, written as retail has it. Function address ->
# [(a regex the draft's text matches exactly once inside the marked function, its replacement, retail's evidence)]. A
# pattern that no longer matches once (the decomp changed or fixed the draft) stops the generation, so the entry is
# looked at again.
T47_DRAFT_FIXES = {
    # CB3TabbedDisplayComponent::SetRecordsContentPanelContents (DRIVER DETAILS > RECORDS, the CAR column). Retail
    # places each car icon at y = 168 + D_004C9E88.y / 2 + 32 (i - 1), anchored at its left middle (0, 0.5), so the
    # icon's middle lies half an icon below the row's top, beside the name and time. The draft has 168 + 100 / 2 and
    # the anchor (0, 0): every icon 50 units (1.6 rows) below its row, the last ones below the panel.
    0x003A4A80: [
        (r"base = base \+ 100\.0f / 2\.0f;",
         "base = base + D_004C9E88.v.marValues[1] / 2.0f;   /* decomp2host T47: retail's y offset is the icon's half "
         "height (lwc1 D_004C9E8C, div.s 2.0, 0x3A4E20) */",
         "half height"),
        (r"(0\.0f \+ base \+ \((?:float|ps2f)\)\(i - 1\) \* 32\.0f\), )CGtV2d\(0\.0f, 0\.0f\)(, D_004C9E88\.v,)",
         r"\1CGtV2d(0.0f, 0.5f) /* decomp2host T47: retail's anchor (0, 0.5), sw 0x3F000000 0x114(sp), 0x3A4F38 */\2",
         "anchor"),
    ],
}


def t47_draft_fixes(text, report=None):
    """1.2.1 T47 (see T47_DRAFT_FIXES): in the marked function of each entry, each pattern (matched once) -> retail's
    form. Returns (text, count)."""
    n = 0
    for addr, fixes in T47_DRAFT_FIXES.items():
        mk = re.search(r"^//\s*0x%08X\b.*$" % addr, text, re.M | re.I)
        if not mk:
            continue
        end = re.search(r"^\}", text[mk.end():], re.M)
        if not end:
            die("T47: no end of the function marked 0x%08X" % addr)
        lo, hi = mk.end(), mk.end() + end.end()
        body = text[lo:hi]
        for pat, repl, what in fixes:
            if len(re.findall(pat, body)) != 1:
                die("T47: 0x%08X: the draft's %s (`%s`) is not there once (decomp changed it? update T47_DRAFT_FIXES)" %
                    (addr, what, pat))
            body = re.sub(pat, repl, body)
            n += 1
            if report is not None:
                report.append("T47 %08X: %s as retail" % (addr, what))
        text = text[:lo] + body + text[hi:]
    return text, n


def fix_markers(fns, names_by_addr, report=None):
    """A `// 0xADDR` marker sometimes sits above an inline helper that the decomp placed before the function itself
    (B3VehicleDeform_003E18E0.cpp: the marker, then func_003E18E0_inl1, then GetClosestBitToPoint). A marked definition
    must carry the selected function's name (its mangled name or the part before `__`); if it does not and exactly one
    unmarked definition does, the marker moves there."""
    if not names_by_addr:
        return fns
    fns = list(fns)
    for a, nm in names_by_addr.items():
        simple = nm.split("__")[0] if "__" in nm[1:] else nm
        pat = re.compile(r"\b(?:%s|%s)\s*\(" % (re.escape(nm), re.escape(simple)))
        marked = [k for k, f in enumerate(fns) if f[3] == a]
        if not marked:
            # boot2: a wrong address in the marker (B3AsyncDataLoader_0013CFA0.cpp marks QueueLoadRequest
            # `// 0x0013CF90`, the function and its proven unit are at 0x13CFA0): exactly one definition carrying the
            # selected function's name, under a marker that is not itself selected, takes the selected address
            named = [k for k, f in enumerate(fns) if f[3] is not None and f[3] not in names_by_addr and pat.search(f[4])]
            if len(named) == 1:
                k = named[0]
                if report is not None:
                    report.append("marker %08X corrected to %08X (%s)" % (fns[k][3], a, simple))
                fns[k] = fns[k][:3] + (a,) + fns[k][4:]
            continue
        if len(marked) != 1 or pat.search(fns[marked[0]][4]):
            continue
        cands = [k for k, f in enumerate(fns) if f[3] is None and pat.search(f[4])]
        if len(cands) == 1:
            k0, k1 = marked[0], cands[0]
            fns[k0] = fns[k0][:3] + (None,) + fns[k0][4:]
            fns[k1] = fns[k1][:3] + (a,) + fns[k1][4:]
            if report is not None:
                report.append("marker %08X moved to %s" % (a, simple))
    return fns


_SHADOW_OPS = None


def operator_sig(head):
    """(op, T1, T2) of a free binary operator definition head, types without const/&; else None."""
    mm = re.search(r"\boperator\s*([^\s(]+)\s*\(([^()]*)\)", re.sub(r"\s+", " ", head))
    if not mm:
        return None
    ps = [re.sub(r"\b(const|volatile)\b|&", "", p).strip() for p in split_params(mm.group(2))]
    ps = [re.sub(r"\s+[A-Za-z_]\w*$", "", p).strip() if len(p.split()) > 1 else p for p in ps]
    ps = [re.sub(r"^GtMathPs2\s*::\s*", "", p) for p in ps]
    return (mm.group(1),) + tuple(ps)


def shadow_operators():
    """Free operators the port's GtMathPs2 shadow header defines (include/gtmath/b3/GtMathPs2.h)."""
    global _SHADOW_OPS
    if _SHADOW_OPS is None:
        _SHADOW_OPS = set()
        for hdr in glob.glob(os.path.join(PORT_ROOT, "include/gtmath/b3/*.h")):
            for mm in re.finditer(r"^inline\s+[\w:]+\s+(operator\s*[^\s(]+\s*\([^()]*\))", open(hdr).read(), re.M):
                sig = operator_sig(mm.group(1))
                if sig:
                    _SHADOW_OPS.add(sig)
    return _SHADOW_OPS


def reduce_unit(text, keep_addrs, report, names_by_addr=None):
    fns = fix_markers(top_level_functions(text), {a: names_by_addr[a] for a in keep_addrs} if names_by_addr else None, report)
    if names_by_addr:
        # definitions without a `// 0xADDR` marker: found by their extern "C" mangled name, or Class::Name if unique
        have = {f[3] for f in fns}
        for a in keep_addrs - have:
            nm = names_by_addr[a]
            hits = [f for f in fns if f[3] is None and re.search(r"\b%s\s*\(" % re.escape(nm), f[4])]
            if not hits:
                hits = [f for f in fns if f[3] is None and re.search(r"\b%s\s*\(" % re.escape(cxx_name(nm)), f[4])]
            if len(hits) == 1:
                k = fns.index(hits[0])
                fns[k] = hits[0][:3] + (a,) + hits[0][4:]
                report.append("found %08X by name" % a)
        text_markers = []
        # gen1: a marker line with text after the address (`// 0x00319C80 (Target #32): Prepare(EGlobalStrings, ...)`,
        # B3HeadingComponent_00319C80.cpp) above an unmarked definition, only comments between: its marker
        for a in keep_addrs - {f[3] for f in fns}:
            mk = re.search(r"^[ \t]*//\s*0x%08X\b[^\n]*\n" % a, text, re.M | re.I)
            if not mk:
                continue
            after = [f for f in fns if f[3] is None and f[0] >= mk.end() and mask(text)[mk.end():f[0]].strip() == ""]
            if after:
                k = fns.index(after[0])
                fns[k] = after[0][:3] + (a,) + after[0][4:]
                text_markers.append((after[0][0], a))
                report.append("found %08X by a marker with text" % a)
    m = mask(text)
    kept = [f for f in fns if f[3] in keep_addrs]
    if len(kept) != len(keep_addrs):
        die("selected function(s) not found by marker: %s" % sorted(set(keep_addrs) - {f[3] for f in kept}))
    # helpers: inline functions without a marker, kept if referenced (transitively); gen1: also a marked `static`
    # definition (internal linkage, so a helper of this file whatever its marker says: `// 0x0032F550` above
    # `static inline bool func_0032F550_inl1(...)`, FindTarget 0x32FF80 -- the inlined copy of retail's 0x32F550) that
    # is not selected itself
    helpers = [f for f in fns if f[3] is None or (f[3] not in keep_addrs and re.match(r"\s*static\b", f[4]))]
    names = {}
    for f in helpers:
        hm = re.search(r"(operator\s*[^\s(]+|[A-Za-z_]\w*)\s*\(", f[4])
        names[f] = hm.group(1) if hm else None
    needed = set()
    work = [m[f[0]:f[2] + 1] for f in kept]
    # boot2: a helper used only through a macro (`#define SWAP(id) func_003FF540_inl1(...)`, func_003FF540.cpp)
    # gen1: with its backslash-continued lines (func_00424C40's STEP macro calls pabsw / pextlw0 on its 9th line)
    work += [mm.group(0) for mm in re.finditer(r"^[ \t]*#\s*define\b(?:[^\n]*\\\n)*[^\n]*", m, re.M)]
    # gen1: or from a class body's inline member (`CB3Im3dVertex() { func_0015E340_inl1(field_0, 0.0f); }`)
    pm_ = preproc_mask(m)
    work += [m[s:e + 1] for s, e in code_blocks(pm_) if not any(f[1] <= s <= f[2] for f in fns)
             and AGG_HEAD.search(pm_[max(0, s - 300):s].split(";")[-1].split("}")[-1])]
    changed = True
    while changed:
        changed = False
        blob = "\n".join(work)
        for f in helpers:
            if f in needed or not names[f]:
                continue
            nm = names[f]
            if nm.startswith("operator"):
                # an operator helper: keep it (cheap, and its use is not textual), unless the port's GtMathPs2 shadow
                # header defines the same operator (the decomp's [L] N185 stand-ins for lane operators its header
                # lacks, e.g. ZAxisType - ZAxisType in func_003D9360): two viable overloads would be ambiguous
                if operator_sig(f[4]) in shadow_operators():
                    report.append("operator %s: the shadow header's" % " ".join(operator_sig(f[4])))
                    continue
                needed.add(f)
                work.append(m[f[0]:f[2] + 1])
                changed = True
            elif re.search(r"\b%s\s*\(" % re.escape(nm), blob):
                needed.add(f)
                work.append(m[f[0]:f[2] + 1])
                changed = True
    edits = []
    # gen1: an unmarked definition named like a game function (`func_XXXXXXXX`, not static / inline / a member) that
    # kept code needs is the retail function itself, written into this unit as a helper (the draft of
    # Update__17CB3TrafficVehicleFP21CB3TrafficRaceCarInfoi 0x186850 carries func_00173D30, which the matched
    # B3TrafficVehicle.cpp defines): it is dropped like a marked one (below: its declaration stays when nothing else
    # declares it), so the call is a call stub and a combined generation links it once
    game_helpers = set()
    for f in list(needed):
        nm_ = names.get(f) or ""
        mf = re.fullmatch(r"func_([0-9A-Fa-f]{8})", nm_)
        if mf and int(mf.group(1), 16) not in keep_addrs and "::" not in m[f[0]:f[1]] and \
                not re.search(r"\b(static|inline)\b", m[f[0]:f[1]]):
            needed.discard(f)
            game_helpers.add(f)
            report.append("helper %s is a game function: a call stub" % nm_)
    live = "\n".join(m[f[0]:f[2] + 1] for f in fns if f in kept or f in needed)
    outside = list(m)
    for f in fns:
        outside[f[0]:f[2] + 1] = " " * (f[2] + 1 - f[0])
    outside = "".join(outside)
    for f in fns:
        if f in kept or f in needed:
            continue   # inline asm in kept code is translated afterwards (P6, eeasm.py)
        decl = ""
        hm = re.search(r"([A-Za-z_]\w*)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*(?:const\s*)?$", m[f[0]:f[1]])
        if (f[3] is not None or f in game_helpers) and hm and "::" not in m[f[0]:f[1]] and not re.search(r"\b(static|inline)\b", m[f[0]:f[1]]) \
                and re.search(r"\b%s\s*\(" % re.escape(hm.group(1)), live) \
                and not re.search(r"\b%s\b" % re.escape(hm.group(1)), outside):
            # gen1: another game function of the unit that kept code calls, and that nothing else declares
            # (Gt2dFrameHierarchy.cpp's func_001D4E70 for Clean / Sync): its declaration stays, so the call becomes a
            # call stub like any other callee
            decl = text[f[0]:f[1]].rstrip() + ";   // decomp2host T5: declaration kept, definition dropped\n"
        edits.append((f[0], f[2] + 1, decl + "/* decomp2host T5: %s dropped */" % ("0x%08X" % f[3] if f[3] else "helper")))
        report.append("dropped %s" % ("0x%08X" % f[3] if f[3] else (names.get(f) or "?")))
    for pos, a in (text_markers if names_by_addr else []):
        edits.append((pos, pos, "// 0x%08X\n" % a))   # gen1: a plain marker the later stages read
    return apply(text, edits), kept


# ---------------------------------------------------------------- float literals as ternary arms (T29)
# boot2: `(x > 1.0f) ? 1.0f : x` with x a ps2f (T1) has arms float and ps2f, which convert both ways: no common type
# (CGtSoundRealVoice::Update 0x385A00). Wrapping only the literal breaks `c ? 2.0f : w / 255.0f` (a plain float
# arm: rwavoicemanager.cpp), so both arms become ps2f(...): the same values in the type T1 gives every float.
FLOAT_LIT = r"-?(?:\d+\.\d*|\.\d+)(?:[eE][-+]?\d+)?[fF]"


def ternary_float_transforms(text):
    """A conditional with a float literal arm gets both arms as ps2f(...) (one type; ps2f converts from float, ps2f
    and the integers). Insertions only, so nested conditionals compose."""
    m = mask(text)
    lit = re.compile(r"\s*" + FLOAT_LIT + r"\s*$")
    ins = []   # (position, order, text)
    n = len(m)
    for q in range(n):
        if m[q] != "?":
            continue
        # arm 1: to the `:` at the same depth
        d, k = 0, q + 1
        while k < n:
            c = m[k]
            if c in "([{":
                d += 1
            elif c in ")]}":
                if d == 0:
                    break
                d -= 1
            elif c == "?" and d == 0:   # a nested conditional in arm 1: skip to its own `:`
                d2, k2 = 0, k + 1
                while k2 < n and not (m[k2] == ":" and d2 == 0 and m[k2 - 1:k2 + 1] != "::" and m[k2:k2 + 2] != "::"):
                    d2 += 1 if m[k2] in "([{" else -1 if m[k2] in ")]}" else 0
                    k2 += 1
                k = k2
            elif c == ":" and d == 0 and m[k - 1:k + 1] != "::" and m[k:k + 2] != "::":
                break
            elif c == ";":
                break
            k += 1
        if k >= n or m[k] != ":":
            continue
        colon = k
        # arm 2: to the first `)`, `]`, `}`, `,` or `;` at the same depth
        d, e = 0, colon + 1
        while e < n:
            c = m[e]
            if c in "([{":
                d += 1
            elif c in ")]}":
                if d == 0:
                    break
                d -= 1
            elif c in ",;" and d == 0:
                break
            e += 1
        a1, a2 = m[q + 1:colon], m[colon + 1:e]
        if not (lit.match(a1) or lit.match(a2)):
            continue
        s1 = q + 1 + (len(a1) - len(a1.lstrip()))
        e1 = colon - (len(a1) - len(a1.rstrip()))
        s2 = colon + 1 + (len(a2) - len(a2.lstrip()))
        e2 = e - (len(a2) - len(a2.rstrip()))
        ins += [(s1, 1, "ps2f("), (e1, 0, ")"), (s2, 1, "ps2f("), (e2, 0, ")")]
    if not ins:
        return text
    out, last = [], 0
    for pos, _, t in sorted(ins, key=lambda x: (x[0], x[1])):
        out.append(text[last:pos])
        out.append(t)
        last = pos
    out.append(text[last:])
    return "".join(out)


# ---------------------------------------------------------------- goto over initializations (T8)
# port7: the statement boundary is a lookbehind (two declarations in a row: the first match must not consume the `;`
# the second needs; func_002F6140, SpawnBouncyPopcorn), and `unsigned short x = e;`-style types are one type
GOTO_DECL = re.compile(r"(?:(?<=[;{}])|^)(\s*)((?:(?:unsigned|signed)\s+)?(?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<[^;<>]*>)?)(?:\s*\*+\s*|\s+))([A-Za-z_]\w*)\s*=(?!=)([^;]*);", re.M)
NOT_TYPES = {"return", "else", "case", "goto", "delete", "throw", "do", "new"}


def goto_transforms(text):
    """MWCC accepts a goto that jumps over `T x = e;` (the decomp keeps retail's block order with such gotos); ISO C++
    does not. In a function with a goto, each such declaration becomes `T x; x = e;`: the same for trivial types."""
    m = preproc_mask(mask(text))
    edits = []
    for f in top_level_functions(text):
        body = m[f[1]:f[2] + 1]
        if not re.search(r"\bgoto\b", body):
            continue
        for mm in GOTO_DECL.finditer(body):
            typ = mm.group(2).strip()
            if typ.split()[0] in NOT_TYPES:
                continue
            if re.match(r"(?:const\s+)?auto\b", typ):
                continue   # gen1: `auto x = e;` cannot be split (an asm function's `auto M = [](...) {...};`)
            s = f[1] + mm.start(2)
            e = f[1] + mm.end()
            orig = text[s:e]
            name = mm.group(3)
            init = text[f[1] + mm.start(4):f[1] + mm.end(4)]
            if len(split_top(mm.group(4))) > 1 or bracket_commas(mm.group(4)):
                continue   # `T a = x, b = y;`: several declarators (not split; jumping over one stops the compile)
                # gen1: also by brackets only (split_top takes the `>` of `->` for one: `int fb = *(int *)&nb->field_18,
                # fa = ...;` had been split as one declarator, leaving fa undeclared); T8b splits a crossed one
            tdecl = re.sub(r"^const\s+", "", text[s:f[1] + mm.start(3)])   # a const local is assigned once: same
            # native3 (2026-11-06): `__attribute__((uninitialized))` exempts the split local from
            # -ftrivial-auto-var-init=zero, which re-zeroes a goto-crossed local when a goto re-enters its scope
            # (DetectAndScoreNearMiss__8CB3ScoreFv read `*slot` as null after `goto chainback`)
            edits.append((s, e, "%s%s __attribute__((uninitialized)); %s =%s; /* decomp2host T8 */" % (tdecl, name, name, init)))
    return apply(text, edits)


# ---------------------------------------------------------------- data definitions check
# ---------------------------------------------------------------- T25 (boot1): compiler-generated static initialisers
SINIT_LO, SINIT_HI = 0x004D3E00, 0x004DD820   # MWCC's __sinit_* functions [C crt0 ctor table 0x4DD820-0x4DDA98, ELF]
SINIT_DEF = re.compile(r"^(const\s+)?((?:unsigned\s+|signed\s+)?[A-Za-z_][\w:]*(?:\s*<[^;=()]*>)?)\s+([A-Za-z_]\w*)\s*"
                       r"(\[[^\]]*\])?\s*(\(.*\)|=.*)$", re.S)


def sinit_unit(text, fns, decomp, report):
    """T25: a selected static initialiser (0x4D3E00-0x4DD81C) whose decomp unit is only namespace-scope definitions
    (`const CGtV2d k(136.0f, 176.0f);`, `CGtV4d D_x = f(...);`): MWCC generates the function from their dynamic
    initialisers, so the unit has no definition with the function's marker. Each such definition becomes a
    placement construction at the object's guest address (its D_ name or symbol), in the unit's order, inside
    `extern "C" void func_XXXXXXXX()` (marker added); each becomes an `extern` declaration (T4: a guest global).
    A brace-initialised definition (`const float D_x[12] = {...};`) is constant data already in the image (retail's
    initialiser never stores it [C func_004D8100 asm: no store to D_004C52E0 / D_004C5310]): dropped. Anything else
    at namespace scope keeps the normal path (and its errors). Proven per function like everything else
    (native_boot --sinit compares each with retail run in the mini)."""
    if len(fns) != 1 or not (SINIT_LO <= fns[0][0] < SINIT_HI) or top_level_functions(text):
        return text
    addr, name = fns[0][0], fns[0][2]
    m = preproc_mask(mask(text))
    stmts, start, d = [], 0, 0
    for i, c in enumerate(m):
        if c in "({[":
            d += 1
        elif c in ")}]":
            d -= 1
        elif c == ";" and d == 0:
            stmts.append((start, i))
            start = i + 1
    edits, body = [], []
    for a, b in stmts:
        ms = m[a:b].strip()
        if not ms or ms.startswith(("extern", "typedef", "using", "struct", "class", "union", "enum", "namespace",
                                    "template", "static_assert")):
            continue
        lead = len(m[a:b]) - len(m[a:b].lstrip())
        st = text[a + lead:b]
        mm = SINIT_DEF.match(st)
        if not mm or "{" in m[a:b]:
            if mm and "{" in m[a:b]:
                edits.append((a + lead, b + 1, "/* decomp2host T25: constant data in the image: %s */" % mm.group(3)))
                report.append("T25 %s: constant data, in the image" % mm.group(3))
            continue
        const, typ, var, arr, init = mm.groups()
        if arr:
            return text   # an array with a dynamic initialiser: not modelled (the normal path reports it)
        if not (re.match(r"(?:D|\w+)_([0-9A-Fa-f]{8})$", var) or re.match(r"D_([0-9A-Fa-f]{1,7})$", var) or var in decomp.symbols):
            return text
        edits.append((a + lead, b + 1, "extern %s%s %s;" % (const or "", typ, var)))
        args = init.strip()
        if args.startswith("="):
            args = "(%s)" % args[1:].strip()
        body.append("    ::new ((void *)&%s) %s%s;" % (var, typ, args))
        report.append("T25 %s: constructed in func_%08X" % (var, addr))
    if not body:
        return text
    text = apply(text, edits)
    text = "#include <new>\n" + text + ("\n// 0x%08X (decomp2host T25: MWCC's static initialiser of this unit's globals)\n"
                                       "extern \"C\" void %s()\n{\n%s\n}\n" % (addr, name, "\n".join(body)))
    return text


NUM_LIT = re.compile(r"0[xX][0-9a-fA-F]+[uUlL]*|(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?[fFuUlL]*")
ARITH = r"(?:unsigned\s+|signed\s+)?(?:char|short|int|long|float|double|bool)"


def literal_only(init):
    """True when an initializer is made of number literals, braces, commas and arithmetic signs only."""
    return re.fullmatch(r"[\s{},+\-*/()]*", NUM_LIT.sub(" ", init)) is not None


def top_statements(m):
    """(start, end) of the top-level `;`-statements of masked text; a brace block is part of the statement when its head
    has `=` (an aggregate initializer), else (a function, class or namespace body) the next statement starts after it."""
    out, start, d, head_eq = [], 0, 0, False
    for i, c in enumerate(m):
        if c == "{":
            if d == 0:
                head_eq = "=" in m[start:i]
            d += 1
        elif c == "}":
            d -= 1
            if d == 0 and not head_eq:
                start = i + 1
        elif c == ";" and d == 0:
            out.append((start, i))
            start = i + 1
    return out


def static_data_transforms(text, report):
    """boot1 (func_0019DD00, func_002F0640, ...): a top-level definition of guest data named by its address
    (`static float D_004E26BC;`, `static const signed char D_004C3FE8[3] = {...};`) is the object retail's image already
    holds, zero (.bss) or loader-initialised (.data / .rodata): it becomes `extern` (T4: a guest global), its
    initializer dropped -- allowed only for literal initializers, which the image holds as written. A dynamic
    initializer would be a static initialiser's (T25), so it keeps the refusal."""
    m = preproc_mask(mask(text))
    edits = []
    for a, b in top_statements(m):
        st = m[a:b].strip()
        mm = re.match(r"^(?:static\s+)?((?:const\s+|volatile\s+)*)(" + r"(?:unsigned\s+|signed\s+)?[A-Za-z_][\w:]*(?:\s*\*+)?" + r")\s+(D_[0-9A-Fa-f]{1,8})\s*(\[[^\]]*\])?\s*(?:=\s*(.*))?$", st, re.S)
        if not mm:
            continue
        quals, typ, name, arr, init = mm.groups()
        if init is not None and not literal_only(init):
            continue
        lead = len(m[a:b]) - len(m[a:b].lstrip())
        edits.append((a + lead, b + 1, "extern %s%s %s%s;   // decomp2host: guest data, the image holds it" % (quals, typ, name, arr or "")))
        report.append("static data %s -> guest global" % name)
    return apply(text, edits) if edits else text


def check_no_data(text, where):
    m = preproc_mask_continued(mask(text))   # gen1: a multi-line #define's continued lines are no statements
    # remove all brace blocks to see top-level statements
    flat, d, agg = [], 0, []
    last = 0
    for i, c in enumerate(m):
        if c == "{":
            if d == 0:
                # gen1: an aggregate body's `}` does not end the statement (`typedef struct { ... } T;`, `extern struct
                # { ... } D_x;`: the declarator after it had been read as a top-level data definition)
                agg.append(re.search(r"\b(?:struct|class|union|enum)\b[^;{}()]*$", m[last:i]) is not None)
            d += 1
            flat.append(" ")
        elif c == "}":
            d -= 1
            flat.append(" " if d == 0 and agg and agg.pop() else ";" if d == 0 else " ")
            if d == 0:
                last = i + 1
        elif d == 0:
            flat.append(c)
            if c == ";":
                last = i + 1
    for st in "".join(flat).split(";"):
        s = st.strip()
        if not s or s.startswith(("extern", "typedef", "using", "struct", "class", "union", "enum", "namespace", "template", "static_assert")):
            continue
        if "(" in s:
            continue   # function declarations
        if s.startswith("#define"):
            continue
        # boot1: a literal-initialised constant scalar (`static const float kOne = 1.0f;`) is a host constant of the same
        # value; only read (an address passed on would be bounced as a copy, still the value)
        mc = re.match(r"^(?:static\s+)?const\s+" + ARITH + r"\s+[A-Za-z_]\w*\s*=\s*(.*)$", s, re.S)
        if mc and literal_only(mc.group(1)):
            continue
        die("%s: top-level data definition not supported (would be host data, not guest data): %s" % (where, s[:80]))


# ---------------------------------------------------------------- the whole file
def is_value_name(m, name, pos):
    """gen1: `name` (at pos in the masked text m) names a value, not a type: a member (`->name`, `.name`), a guest
    global (`D_<addr>`), or a name the text declares as a variable or parameter (`T name =`, `(T *name,`) and never
    as a type (struct / class / union / enum / typedef / using)."""
    if re.search(r"(?:->|\.)\s*$", m[max(0, pos - 8):pos]) or re.fullmatch(r"D_[0-9A-Fa-f]+", name):
        return True
    if "::" in name or "<" in name:
        return False
    e = re.escape(name)
    if re.search(r"\b(?:struct|class|union|enum)\s+%s\b|\btypedef\b[^;]*\b%s\s*;|\busing\s+%s\s*=" % (e, e, e), m):
        return False
    return re.search(r"\b(?!(?:return|case|else|goto|throw|delete|new)\b)[A-Za-z_][\w:]*(?:\s*<[^;<>()]*>)?[\s*&]+%s"
                     r"\s*(?:=|;|,|\)|\[|\()" % e, m) is not None


def pointer_to_pointer_transforms(text):
    """T3b: outside aggregate bodies, `T **` (a pointer to a pointer, which in decomp code points at a pointer stored in
    guest memory: a member or array element, 4 bytes) becomes `b3host::gp<T> *`; `T ***` -> `b3host::gp<b3host::gp<T>> *`.
    A `T **` pointing at a host local would not fit; none was seen (it would fail to compile, not miscompile)."""
    m = preproc_mask(mask(text))
    edits = []
    for mm in re.finditer(r"\b((?:const\s+)?(?:(?:unsigned|signed)\s+)?[A-Za-z_][\w:]*(?:\s*<[^;<>()]*>)?)\s*((?:\*\s*){2,})", m):
        base = mm.group(1)
        if base in ("return", "case", "else") or base.startswith("b3host::gp"):
            continue
        if re.search(r"\*\s+\*", mm.group(2)) and is_value_name(m, base, mm.start()):
            continue   # gen1: `lScalingMat * *m`, `k * *pcast<...>(...)` is a product with a dereference, not a type
        n = mm.group(2).count("*")
        t = base
        for _ in range(n - 1):
            t = "b3host::gp<%s>" % t
        edits.append((mm.start(), mm.end(), t + " *"))
    # T35 (gen1), always: `T *const &` in an extern "C" prototype (a guest callee: func_00310180's `unsigned short
    # *const &arg`, whose definition reads the referenced pointer as a 4-byte guest word) -> `b3host::gp<T> const &`;
    # a const reference binds a temporary, so every caller still compiles. The rest of T35 is compiler-guided
    # (ref_pointer_transforms, from compile_for_stubs)
    taken = {(s, e) for s, e, _ in edits}
    for pm_ in re.finditer(r'\bextern\s*"[ C]"\s*[^;{}()]*\(([^;{}]*)\)\s*;', m):
        for mm in REF_PTR.finditer(m, pm_.start(1), pm_.end(1)):
            if re.search(r"\*\s*const\s*&", mm.group(0)) and not any(s <= mm.start() < e for s, e in taken) and \
                    mm.group(1) not in ("return", "case", "else") and not mm.group(1).startswith("b3host::gp"):
                edits.append((mm.start(), mm.end(), "b3host::gp<%s> const &" % mm.group(1)))
    return apply(text, edits)


REF_PTR = re.compile(r"\b((?:const\s+)?(?:(?:unsigned|signed)\s+)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<[^;<>()]*>)?(?:\s+const)?)"
                     r"\s*\*\s*(?:const\s*)?&(?!&)")


def ref_pointer_transforms(text):
    """T35 (gen1), compiler-guided: `T *&r` (a reference to a pointer: in decomp code bound to a member or array element,
    `CGt2dObject *&p = field_48[i];`, or a helper's `UnkStruct_X *&` parameter given one) -> `b3host::gp<T> &r`, as T3b
    does for `T **`. Applied by compile_for_stubs only to a unit that fails to bind such a reference (a gp member is an
    rvalue T*), and kept only if that error goes away: a helper whose `T *&` binds a host local (RW Audio's
    `RwUInt8 *&cur`, GtNATDataManager) compiles as it was. Casts `(T *&)x` are T9's ref_cast."""
    m = preproc_mask(mask(text))
    edits = []
    for mm in REF_PTR.finditer(m):
        base = mm.group(1)
        if base in ("return", "case", "else") or base.startswith("b3host::gp") or is_value_name(m, base, mm.start()):
            continue
        if re.search(r"\(\s*$", m[max(0, mm.start() - 8):mm.start()]) and re.match(r"\s*\)", m[mm.end():]):
            continue
        cq = " const" if re.search(r"\*\s*const\s*&", m[mm.start():mm.end()]) else ""
        edits.append((mm.start(), mm.end(), "b3host::gp<%s>%s &" % (base, cq)))
    return apply(text, edits) if edits else text


PTR_LOCAL_TYPE = r"(?:const\s+)?(?:(?:unsigned|signed)\s+)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<[^;<>()]*>)?(?:\s+const)?"


def address_taken_pointer_locals(text, report=None):
    """T32 (gen1): a local pointer whose address is taken (`CB3AILane *lane; ... Find(&lane, &seg)`) or a local array
    of pointers passed as a whole (`CB3ControllerMapping *local[2]; GetAvailableControllers(local, p1)`) is the
    4-byte guest pointer it is on the EE: `b3host::gp<T> lane`, `b3host::gp<T> local[2]`. The callee's `T **` is
    `b3host::gp<T> *` (T3b), which a host `T *` cannot bind to; through the gp the callee reads and writes the 4-byte
    guest address, natively or through a ptr_arg bounce window. Only single-declarator statements are rewritten."""
    m = preproc_mask(mask(text))
    edits = []
    for f in top_level_functions(text):
        b0, b1 = f[1], f[2]
        body = m[b0:b1]
        def scope_end(pos):
            """the end of the innermost block of the body that contains pos (a declaration's scope)"""
            d = 0
            for j in range(pos, len(body)):
                if body[j] == "{":
                    d += 1
                elif body[j] == "}":
                    if d == 0:
                        return j
                    d -= 1
            return len(body)
        for dm in re.finditer(r"(?:(?<=[;{}])|(?<=[^:]:))(\s*)(%s)\s*\*\s*([A-Za-z_]\w*)\s*(\[[^\]\[;]*\])?\s*(?=[=;]|__attribute__)" % PTR_LOCAL_TYPE, body):
            name, arr = dm.group(3), dm.group(4)
            base = dm.group(2).strip()
            if base in ("return", "case", "else", "goto", "delete", "throw") or base.startswith("b3host::gp") or \
                    re.fullmatch(r"(?:const\s+)?(?:struct|class|union|enum)", base):
                continue
            scope = body[dm.end():scope_end(dm.end())]   # where this declaration is visible
            taken = re.search(r"(?<![&\w])&\s*%s\b(?!\s*(?:\[|\.|->|\(|::))" % re.escape(name), scope)
            # fe1 (2026-11-10): also behind C-style casts (`f(..., (unsigned long)aD0)`, func_003A5040's TEAM CRUSH
            # records: the array's guest address as an integer argument, its elements read as 4-byte guest pointers)
            passed = re.search(r"[(,]\s*(?:\([^()]*\)\s*)*%s\s*(?=[,)])|(?<![&\w])&\s*%s\s*\[" % (re.escape(name), re.escape(name)), scope)
            if not ((not arr and taken) or (arr and passed)):
                continue
            # the statement must declare one name (no `T *a, *b` / `T *a = f(x), *b`): scan to its `;` at depth 0
            j, d = b0 + dm.end(), 0
            while j < b1 and not (m[j] == ";" and d == 0):
                if m[j] in "([{":
                    d += 1
                elif m[j] in ")]}":
                    d -= 1
                    if d < 0:
                        break
                elif m[j] == "," and d == 0:
                    break
                j += 1
            if j >= b1 or m[j] != ";":
                continue
            s = b0 + dm.start(2)
            e = b0 + dm.end(3)
            edits.append((s, e, "b3host::gp<%s> %s" % (base, name)))
            if report is not None:
                report.append("T32 guest-pointer local %s" % name)
    return apply(text, edits) if edits else text


def f32_address_transforms(text):
    """T34 (gen1): `&v.marValues[i]` (a GtMathPs2 float element: host `float` in the shadow headers) ->
    `b3host::f32_addr(&v.marValues[i])`, which converts to `ps2f *` (T1's type for the decomp's float) as well as to
    `float *` (b3host/ps2f.h); `ps2f *py = &pos.marValues[1];` did not compile (6 functions)."""
    m = preproc_mask(mask(text))
    edits = []
    # gen1: a reference bound to such an element (`ps2f &th = total.marValues[1];`, func_0033D890) -> the same ps2f
    # through the element's address
    for mm in re.finditer(r"\bps2f\s*&\s*[A-Za-z_]\w*\s*=\s*((?:[A-Za-z_]\w*)(?:\s*(?:\.|->)\s*[A-Za-z_]\w*|\s*\[[^\[\];]*\])*"
                          r"\s*\.\s*marValues\s*\[[^\[\];]*\])\s*;", m):
        edits.append((mm.start(1), mm.end(1), "*static_cast<ps2f *>(b3host::f32_addr(&%s))" % text[mm.start(1):mm.end(1)]))
    for mm in re.finditer(r"(?<![&\w])&\s*((?:[A-Za-z_]\w*)(?:\s*(?:\.|->)\s*[A-Za-z_]\w*|\s*\[[^\[\];]*\])*\s*\.\s*marValues\s*\[[^\[\];]*\])", m):
        if re.search(r"\bf32_addr\s*\(\s*$", m[max(0, mm.start() - 40):mm.start()]):
            continue   # already wrapped (a header transformed twice)
        edits.append((mm.start(), mm.end(), "b3host::f32_addr(%s)" % text[mm.start():mm.end()]))
    return apply(text, edits) if edits else text


def inline_asm_helpers(text, fns, decomp, report):
    """T41 (gen1, for vu0's VU binding): in a function the binding covers (one of its pcs in DECOMP2HOST_VUBIND's table;
    DECOMP2HOST_T41_ALL=1: every selected function, for tests), a call of a `static inline` helper that holds inline asm
    (itself or through another such helper) is expanded at the call site as an immediately-invoked lambda with the
    helper's parameters, result type and body. The resolver (eeasm.VuBinding) matches the k-th asm occurrence of a key
    inside the function's marker region with retail's k-th; a helper translated once was one textual occurrence for
    all its inlined copies (func_001AD380: vaddx 1 vs 6). The arguments are bound first, in order (an argument that
    itself calls a helper runs before the body, as MWCC's inlined code does), then the body:
    `[&]() -> R { auto&& b3a0 = (arg0); ...; return [](params) -> R { body }(static_cast<decltype(b3a0)&&>(b3a0), ...); }()`.
    The helper's own definition stays for other callers."""
    want = {x[0] for x in fns}
    if os.environ.get("DECOMP2HOST_T41_ALL") != "1":
        vb = vubind() if decomp is not None else None
        if vb is None:
            return text
        pcs = sorted(getattr(vb, "table", {}))
        import bisect
        bound = {a for a in want if a in decomp.funcs and
                 bisect.bisect_left(pcs, a) < bisect.bisect_left(pcs, a + decomp.funcs[a][1])}
    else:
        bound = set(want)
    if not bound:
        return text
    m = preproc_mask(mask(text))
    fl = top_level_functions(text)
    helpers = {}
    for f in fl:
        hm = re.match(r"^static\s+inline\s+(.+?)\s+([A-Za-z_]\w*)\s*\((.*)\)\s*$", f[4], re.S)
        if hm and f[3] is None or (hm and f[3] not in want):
            helpers[hm.group(2)] = (hm.group(1).strip(), hm.group(3).strip(), text[f[1] + 1:f[2]], m[f[1] + 1:f[2]])
    # those that hold asm, themselves or through another one
    asmy = {n for n, h in helpers.items() if re.search(r"\b(?:asm|__asm__)\b", h[3])}
    changed = True
    while changed:
        changed = False
        for n, h in helpers.items():
            if n not in asmy and any(re.search(r"\b%s\s*\(" % re.escape(x), h[3]) for x in asmy):
                asmy.add(n)
                changed = True
    if not asmy:
        return text
    call_re = re.compile(r"(?<![\w.>:])(%s)\s*\(" % "|".join(re.escape(x) for x in sorted(asmy, key=len, reverse=True)))
    counter = [0]

    def expand(code, depth):
        if depth > 8:
            die("T41: helpers nest deeper than 8")
        mc = mask(code)
        out, pos = [], 0
        while True:
            cm = call_re.search(mc, pos)
            if not cm:
                break
            o = cm.end() - 1
            dd, c = 0, None
            for k in range(o, len(mc)):
                if mc[k] in "([{":
                    dd += 1
                elif mc[k] in ")]}":
                    dd -= 1
                    if dd == 0:
                        c = k
                        break
            if c is None:
                break
            ret, plist, body, _ = helpers[cm.group(1)]
            args = [x for x in split_macro_args(code[o + 1:c])] if code[o + 1:c].strip() else []
            n = counter[0]
            counter[0] += 1
            binds = "".join("auto&& b3a%d_%d = (%s); " % (n, k, expand(a, depth + 1)) for k, a in enumerate(args))
            fwd = ", ".join("static_cast<decltype(b3a%d_%d)&&>(b3a%d_%d)" % (n, k, n, k) for k in range(len(args)))
            out.append(code[pos:cm.start()])
            out.append("[&]() -> %s { /* decomp2host T41: %s inlined */ %sreturn [](%s) -> %s {%s}(%s); }()" % (
                ret, cm.group(1), binds, plist, ret, expand(body, depth + 1), fwd))
            pos = c + 1
        out.append(code[pos:])
        return "".join(out)
    edits = []
    for f in fl:
        if f[3] in bound:
            b0 = text[f[1] + 1:f[2]]
            b1 = expand(b0, 0)
            if b1 != b0:
                edits.append((f[1] + 1, f[2], b1))
    if edits:
        report.append("T41 asm helpers inlined at %d call(s)" % counter[0])
    return apply(text, edits) if edits else text


def asm_function_transforms(text, fns, decomp, report):
    """gen1: a selected CodeWarrior whole-asm function (`asm void B3Im3d::RenderTristrip(const CB3Im3dVertex*, int)
    { ... }`, 0x365040 / 0x364E10: the hottest fallbacks after RenderBeforeVehicles) becomes an ordinary definition
    with the declared signature (unnamed parameters named p0..) whose body is vu0's eeasm.translate_asm_function: the
    retail instructions on a full register file (guest stack frame, jal as guest calls, VU ops on the shared VU0
    state). Arguments go in as their EE register images: pointers / references by guest address, 32-bit integers
    sign-extended, `this` first for a non-static member. Not done (refused as before): a non-void result, a float
    parameter, an eeasm without the translator."""
    try:
        import eeasm
    except ImportError:
        return text
    if not hasattr(eeasm, "translate_asm_function"):
        return text
    want = {x[0] for x in fns}
    edits = []
    for f in top_level_functions(text):
        head = text[f[0]:f[1]].strip()
        # gen1: `extern "C" asm void func_002FC530(void*, void*)` too (the prefix is kept)
        lm = re.match(r'^(extern\s*"C"\s*)', head)
        linkage = 'extern "C" ' if lm else ""
        head = head[lm.end():] if lm else head
        hm = re.match(r"^asm\s+(.+?)\s+((?:[A-Za-z_]\w*::)*[A-Za-z_~]\w*)\s*\((.*)\)\s*(const)?\s*$", head, re.S)
        if not hm or f[3] not in want:
            continue
        ret, qname, plist = hm.group(1).strip(), hm.group(2), hm.group(3).strip()
        if ret != "void":
            die("%08X: asm function returning %s: the translator returns no v0 / f0 yet" % (f[3], ret))
        params, exprs = [], []
        qual, _, nm = qname.rpartition("::")
        if qual and not is_static_member(qual, nm, text):
            exprs.append("b3host::guest_address(this)")
        for k, prm in enumerate(x for x in split_top(plist) if x.strip() and x.strip() != "void"):
            prm = prm.strip()
            pm = re.match(r"^(.*?[\s*&])([A-Za-z_]\w*)$", prm)
            if pm and pm.group(1).strip() and not re.fullmatch(r"(?:const\s+|volatile\s+|unsigned\s+|signed\s+)*(?:int|char|short|long|bool|float|double)", prm):
                ptype, pname = pm.group(1).strip(), pm.group(2)
            else:
                ptype, pname = prm, "p%d" % k
            params.append("%s %s" % (ptype, pname))
            if re.search(r"\b(?:float|double|ps2f)\b", ptype) and "*" not in ptype and "&" not in ptype:
                die("%08X: asm function with a float parameter: the translator takes GPR arguments only" % f[3])
            if ptype.endswith("&"):
                exprs.append("b3host::guest_address(&%s)" % pname)
            elif "*" in ptype:
                exprs.append("b3host::guest_address(%s)" % pname)
            else:
                exprs.append("b3asm::sx32(static_cast<std::uint32_t>(%s))" % pname)

        def symbol_address(name, _f=f[3]):
            a = decomp.symbols.get(name)
            if a is None:
                a = function_address(decomp, name)
            if a is None:
                dm = re.fullmatch(r"D_([0-9A-Fa-f]{8})", name)
                a = int(dm.group(1), 16) if dm else None
            if a is None:
                die("%08X: asm function: no guest address for %s" % (_f, name))
            return a
        body = text[f[1] + 1:f[2]]
        try:
            cbody = eeasm.translate_asm_function(body, exprs, symbol_address, f[3], where="%08X" % f[3])
        except eeasm.AsmError as e:
            die("%08X: asm function not translatable: %s" % (f[3], e))
        # a VU op's `do { ... } while (0)` that the translator leaves without its `;` (be98e58b): completed here
        cbody = re.sub(r"\bwhile \(0\)(?=\s*(?://[^\n]*)?$)", "while (0);", cbody, flags=re.M)
        edits.append((f[0], f[2] + 1, "%svoid %s(%s)%s\n{   // decomp2host (gen1): CodeWarrior asm function, translated by "
                      "eeasm.translate_asm_function\n%s\n}" % (linkage, qname, ", ".join(params), " const" if hm.group(4) else "", cbody)))
        report.append("asm function %08X translated" % f[3])
    if not edits:
        return text
    return "#include <cstring>\n" + apply(text, edits)


def expand_asm_macros(text, report):
    """port7: a function-like macro of the unit whose body is inline asm with a literal template (no # or ##: e.g.
    `#define ZERO_V3(v) { u_long128 r; asm("vadd.xyz %0, $vf0, $vf0" : "=j"(r)); (v).mData = r; }`,
    ResetData__13CB3CrashComboFv) is expanded at its uses, so the asm translator sees ordinary asm statements; its
    #define becomes a comment. Macros built with # or ## stay refused."""
    defs = {}
    edits = []
    for mm in re.finditer(r"^[ \t]*#\s*define\s+(\w+)\(([^)]*)\)((?:[^\n]*\\\n)*[^\n]*)", text, re.M):
        body = mm.group(3).replace("\\\n", "\n")
        if not re.search(r"\b(asm|__asm__)\b", mask(body)) or "#" in mask(body):
            continue
        defs[mm.group(1)] = ([p.strip() for p in mm.group(2).split(",") if p.strip()], body.strip())
        edits.append((mm.start(), mm.end(), "// decomp2host: asm macro %s expanded at its uses" % mm.group(1)))
    if not defs:
        return text
    text = apply(text, edits)
    for name, (params, body) in defs.items():
        while True:
            m = preproc_mask(mask(text))
            um = re.search(r"\b%s\s*\(" % re.escape(name), m)
            if not um:
                break
            d, j = 0, um.end() - 1
            for j in range(um.end() - 1, len(m)):
                if m[j] == "(":
                    d += 1
                elif m[j] == ")":
                    d -= 1
                    if d == 0:
                        break
            args = split_macro_args(text[um.end():j]) if params else []
            if len(args) != len(params):
                die("asm macro %s: %d arguments for %d parameters" % (name, len(args), len(params)))
            mb = mask(body)
            out, pos = [], 0
            for pm in re.finditer(r"\b\w+\b", mb):
                if pm.group(0) in params:
                    out.append(body[pos:pm.start()])
                    a_ = args[params.index(pm.group(0))]
                    # gen1: a string literal (an asm template) stays bare: `asm(("...") : ...)` is no asm statement;
                    # so does an identifier the body uses as a function name (OPN's `name(...)`: `(f)(...)` confuses the
                    # later passes); other identifiers keep their parentheses (the generated text as before)
                    bare = re.fullmatch(r'(?:"(?:[^"\\]|\\.)*"\s*)+', a_) or (
                        re.fullmatch(r"[A-Za-z_]\w*", a_) and re.match(r"\s*\(", mb[pm.end():]))
                    out.append(a_ if bare else "(%s)" % a_)
                    pos = pm.end()
            out.append(body[pos:])
            text = text[:um.start()] + "".join(out) + text[j + 1:]
        report.append("asm macro %s expanded" % name)
    return text


def asm_macros(text):
    """Names of #define macros whose body contains inline asm (their templates are macro-built: not translatable)."""
    out = set()
    for mm in re.finditer(r"^[ \t]*#\s*define\s+(\w+)((?:[^\n]*\\\n)*[^\n]*)", text, re.M):
        if re.search(r"\b(asm|__asm__)\b", mask(mm.group(2))):
            out.add(mm.group(1))
    return out


_VUBIND = None


def vubind():
    """native3: DECOMP2HOST_VUBIND=<table> (the Makefile's native generation only, never gen/decomp): the VU0
    register binding baked in (eeasm.VuBinding): at a bound COP2 site the native VU state mirrors retail's destination
    register, and a pure "=j" output under a partial mask takes its unwritten lanes from it (retail's stale lanes)."""
    global _VUBIND
    path = os.environ.get("DECOMP2HOST_VUBIND")
    if not path:
        return None
    if _VUBIND is None:
        _VUBIND = eeasm.VuBinding(path)
    return _VUBIND


def asm_transforms(text, where, report, macros=frozenset(), decomp=None):
    """P6: every inline asm statement in code (not in #define bodies) -> C++ on b3host/eeasm.h (tools/decomp2host/
    eeasm.py). An instruction or form the translator does not know stops the generation with its location."""
    m = preproc_mask(mask(text))
    for name in sorted(macros):
        mm = re.search(r"\b%s\b" % re.escape(name), m)
        if mm:
            die("%s:%d: uses macro %s, whose body is inline asm with a macro-built template (not translatable)" % (
                where, text.count("\n", 0, mm.start()) + 1, name))

    def fail(pos, msg, stmt):
        die("%s:%d: inline asm not translatable: %s\n    %s" % (where, text.count("\n", 0, pos) + 1, msg, stmt[:200]))
    resolver_at = None
    vb = vubind() if decomp is not None else None
    if vb is not None:
        # each `// 0xADDR` marker opens a function up to the next marker; its retail code from the ELF
        marks = [(mm.start(), int(mm.group(1), 16)) for mm in re.finditer(r"^// 0x([0-9A-Fa-f]{8})\b", text, re.M)]
        stmts = list(eeasm.find_asm(m))
        cache = {}

        def resolver_at(pos):
            k = max((i for i, (p0, _) in enumerate(marks) if p0 <= pos), default=None)
            if k is None or marks[k][1] not in decomp.funcs:
                return None
            if k not in cache:
                end = marks[k + 1][0] if k + 1 < len(marks) else len(text)
                keys = [key for s0, e0 in stmts if marks[k][0] <= s0 < end for key in eeasm.vu_keys_of(text[s0:e0])]
                addr = marks[k][1]
                cache[k] = vb.resolver(decomp.code(addr, decomp.funcs[addr][1]), addr, keys)
            return cache[k]
    before = vb.bound if vb is not None else 0
    edits = eeasm.translate_text(text, m, where, fail, resolver_at)
    if vb is not None and vb.bound != before:
        report.append("VU binding %d sites" % (vb.bound - before))
    if edits:
        report.append("asm %d" % len(edits))
    return apply(text, edits)


# ---------------------------------------------------------------- integer <-> pointer casts (T9)
INT_TYPES = {"int", "unsigned", "unsigned int", "signed", "signed int", "short", "unsigned short", "short int",
             "unsigned short int", "char", "unsigned char", "signed char", "b3host::ps2_long", "b3host::ps2_ulong",
             "long long", "unsigned long long", "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "u_char",
             "u_short", "u_int", "u_long", "uintptr_t", "intptr_t", "size_t", "uint8_t", "uint16_t", "uint32_t",
             "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t", "std::uint32_t", "std::int32_t", "std::uintptr_t",
             # boot2: RenderWare's integer typedefs (rwplcore.h; the port tracks' rw / rwa headers): `(RwUInt32)(p + 1)`
             # of a gp<T> was a C cast through operator T* -- the host address cut to 32 bits (setInitialRenderState's
             # farFogPlane cursor, after the single-declaration pass; malloc then wrote through it)
             "RwUInt8", "RwUInt16", "RwUInt32", "RwUInt64", "RwInt8", "RwInt16", "RwInt32", "RwInt64", "RwBool",
             "RwChar", "RwUInt", "RwInt"}
# boot2: integer typedefs a unit declares itself (`typedef unsigned int Foo;`) are integer cast targets too
INT_TYPEDEF = re.compile(r"\btypedef\s+((?:unsigned|signed|long|short|int|char|\s)+?|[A-Za-z_][\w:]*)\s+([A-Za-z_]\w*)\s*;")
UNIT_INT_TYPES = set()
CAST_OK_BEFORE = set("(,=!~-+*/%&|^<>?:;{}[")
CAST_KEYWORDS = {"return", "case", "else", "do", "throw"}
FNPTR_CAST = re.compile(r"^([A-Za-z_][\w:<> ]*?\**)\s*\(\s*\*\s*\)\s*\(([^()]*)\)$")
# boot2: `long long` too (`*(unsigned long long *)(flip + 0x3B0)` in rwDMAVSYNCHandler stayed a host cast)
PTR2_CAST = re.compile(r"^((?:const\s+|volatile\s+|struct\s+|class\s+|union\s+)*(?:unsigned\s+|signed\s+)?(?:long\s+(?=long\b))?"
                       r"[A-Za-z_][\w:]*(?:\s*<[^()]*>)?(?:\s+const)?)\s*\*\s*(const\s*)?\*$")
PTR_CAST = re.compile(r"^((?:const\s+|volatile\s+|struct\s+|class\s+|union\s+)*(?:unsigned\s+|signed\s+)?(?:long\s+(?=long\b))?"
                      r"[A-Za-z_][\w:]*(?:\s*<[^()]*>)?(?:\s+const)?)\s*\*$")


def cast_target(inner):
    """('i', type) for an integer cast, ('p', pointee) for a single-level pointer cast, else None."""
    t = re.sub(r"\s+", " ", inner.strip())
    t2 = re.sub(r"^(const|volatile) ", "", t)
    if t2 in INT_TYPES or t2 in UNIT_INT_TYPES:
        return ("i", t)
    # boot2: `(T *&)x`: the pointer object re-typed (b3host::ref_cast; CB3SoundDistortion::Construct)
    rm = re.match(r"^(.*\S)\s*&$", t)
    if rm and "(" not in t:
        pr = PTR_CAST.match(rm.group(1))
        if pr and pr.group(1).strip().split()[-1] not in ("return", "case", "else"):
            return ("r", pr.group(1).strip())
    pm = PTR_CAST.match(t)
    if pm and "(" not in t:
        base = pm.group(1).strip()
        if base.split()[-1] in ("return", "case", "else"):
            return None
        return ("p", base)
    # port8: `((void (*)(int, int))*fp)(16, 1)` calls a guest function whose address the game stored as data: a cast
    # to a function-pointer type becomes a callable gfn at that guest address (OpenViewport 0x227360 called the guest
    # address as a host function pointer and crashed)
    fm = FNPTR_CAST.match(t)
    if fm:
        return ("f", "%s(%s)" % (fm.group(1).strip(), fm.group(2).strip()))
    # gen1: `(void (**)(void *, int))p` points at a guest function pointer stored in guest memory (a vtable slot:
    # `(*(void (**)(void *, int))(*(char **)self + 0x28))(self, 0)`, GetCrashingDrivingControls 0x1BB6E0): a pointer to a
    # gfn, 4 bytes there, so the dereference is a callable guest address
    fm2 = re.match(r"^(.*?)\(\s*\*\s*\*\s*\)\s*\((.*)\)$", t)
    if fm2 and "(" not in fm2.group(1):
        return ("p", "b3host::gfn<%s(%s)>" % (fm2.group(1).strip(), fm2.group(2).strip()))
    # port8: `(T * const *)&field` / `(T **)p` reads a pointer stored in guest memory: there it is a 4-byte guest
    # address, i.e. a gp<T> (T3/T3b), never a host pointer (UpdateTransition 0x19F4B0 read 8 bytes of a gp member as a
    # host pointer and crashed)
    pm = PTR2_CAST.match(t)
    if pm and "(" not in t:
        base = pm.group(1).strip()
        if base.split()[-1] in ("return", "case", "else"):
            return None
        return ("p", "b3host::gp<%s>%s" % (base, " const" if pm.group(2) else ""))
    return None


def operand_end(m, i):
    """End of the unary-expression (a cast's operand) starting at or after m[i]; None if none starts there."""
    n = len(m)

    def ws(k):
        while k < n and m[k] in " \t\n":
            k += 1
        return k

    def close(k, o, c):
        d = 0
        for j in range(k, n):
            if m[j] == o:
                d += 1
            elif m[j] == c:
                d -= 1
                if d == 0:
                    return j
        return None
    i = ws(i)
    if i >= n:
        return None
    if m.startswith("++", i) or m.startswith("--", i):
        return operand_end(m, i + 2)
    if m[i] in "-+!~*&" and not m.startswith("->", i):
        return operand_end(m, i + 1)
    if m[i] == "(":
        j = close(i, "(", ")")
        if j is None:
            return None
        inner = re.sub(r"\s+", " ", m[i + 1:j].strip())
        if cast_target(inner) is not None or inner in ("ps2f", "float", "bool", "const ps2f", "void"):
            e = operand_end(m, j + 1)   # a nested cast: its operand follows
            if e is not None:
                return e
        k = j + 1
    elif re.match(r"[A-Za-z_]", m[i]):
        mm = re.match(r"[A-Za-z_]\w*(?:\s*::\s*~?[A-Za-z_]\w*)*", m[i:])
        word = mm.group(0)
        k = i + mm.end()
        if word in ("sizeof",):
            return operand_end(m, k)
        if word in ("static_cast", "reinterpret_cast", "const_cast", "dynamic_cast") or (m[ws(k):ws(k) + 1] == "<" and word.startswith("b3host::")):
            kk = ws(k)
            if kk < n and m[kk] == "<":
                j = close(kk, "<", ">")
                if j is None:
                    return None
                k = j + 1
    elif re.match(r"[0-9.]", m[i]):
        mm = re.match(r"(?:0[xX][0-9A-Fa-f]+|[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)[uUlLfF]*", m[i:])
        k = i + mm.end()
    elif m[i] in "\"'":
        j = m.find(m[i], i + 1)
        if j < 0:
            return None
        k = j + 1
    else:
        return None
    while True:
        kk = ws(k)
        if kk < n and m[kk] == "[":
            j = close(kk, "[", "]")
            if j is None:
                return None
            k = j + 1
        elif kk < n and m[kk] == "(":
            j = close(kk, "(", ")")
            if j is None:
                return None
            k = j + 1
        elif m.startswith("->", kk) or (kk < n and m[kk] == "." and kk + 1 < n and re.match(r"[A-Za-z_]", m[ws(kk + 1)])):
            k2 = ws(kk + (2 if m.startswith("->", kk) else 1))
            mm = re.match(r"~?[A-Za-z_]\w*", m[k2:])
            if not mm:
                return None
            k = k2 + mm.end()
        elif m.startswith("++", kk) or m.startswith("--", kk):
            k = kk + 2
        else:
            return k


def cast_transforms(text, report):
    """T9: `(int)x` -> b3host::icast<int>(x), `(T *)x` -> b3host::pcast<T>(x) (b3host/gp.h): on the EE a pointer is
    its 32-bit guest address, which neither a host pointer nor gp<T> converts to by a C cast."""
    m = preproc_mask(mask(text))
    out, pos, n_done, n_elem = [], 0, 0, 0
    UNIT_INT_TYPES.clear()
    for tm in INT_TYPEDEF.finditer(m):
        base = re.sub(r"\s+", " ", tm.group(1).strip())
        if base in INT_TYPES or base in UNIT_INT_TYPES:
            UNIT_INT_TYPES.add(tm.group(2))

    def rewrite(lo, hi, operand=False):
        """Rewrite casts inside text[lo:hi] (m gives the structure); returns the new text. `operand`: text[lo:hi] is a
        cast's operand, so a cast at its start is one even though a `)` precedes it (port8: `(unsigned long)((unsigned
        int)lpBatchAddr) << 32` in CSparkBank::Construct kept the inner cast as a C cast: a truncated host pointer)."""
        res, p = [], lo
        i = lo
        while i < hi:
            if m[i] != "(":
                i += 1
                continue
            j = i + 1
            d = 1
            while j < hi and d:
                if m[j] == "(":
                    d += 1
                elif m[j] == ")":
                    d -= 1
                j += 1
            if d:
                break
            j -= 1   # m[j] == ')'
            ct = cast_target(m[i + 1:j])
            if ct is None:
                i += 1
                continue
            k = i - 1
            while k >= 0 and m[k] in " \t\n":
                k -= 1
            prev_ok = k < 0 or m[k] in CAST_OK_BEFORE or (operand and m[lo:i].strip() == "")
            if not prev_ok and re.match(r"\w", m[k]):
                ws_ = k
                while ws_ >= 0 and re.match(r"\w", m[ws_]):
                    ws_ -= 1
                prev_ok = m[ws_ + 1:k + 1] in CAST_KEYWORDS
            if not prev_ok:
                i += 1
                continue
            e = operand_end(m, j + 1)
            if e is None or e > hi:
                i += 1
                continue
            nonlocal n_done
            n_done += 1
            res.append(text[p:i])
            inner = rewrite(j + 1, e, operand=True)
            if ct[0] == "i":
                res.append("b3host::icast<%s>(%s)" % (text[i + 1:j].strip(), inner.strip()))
            elif ct[0] == "r":
                res.append("b3host::ref_cast<%s>(%s)" % (ct[1], inner.strip()))
            elif ct[0] == "f":
                res.append("b3host::gfn<%s>::from_raw(b3host::icast<unsigned int>(%s))" % (ct[1], inner.strip()))
            else:
                ea = elem_address(inner.strip())
                if ea is not None:   # T19 (port14): `(T *)&p[i]` is address arithmetic, never a dereference
                    nonlocal n_elem
                    n_elem += 1
                    res.append("b3host::pcast_elem<%s>(%s, %s)" % (ct[1], ea[0], ea[1]))
                else:
                    res.append("b3host::pcast<%s>(%s)" % (ct[1], inner.strip()))
            p = e
            i = e
        res.append(text[p:hi])
        return "".join(res)
    new = rewrite(0, len(text))
    # port8: macro bodies too (preprocessor lines are masked above): `#define PLAY(id, ...) ...(const CGtSoundBank*)
    # self->field_78...` (func_00285C00) and `#define HW(a) (*(volatile unsigned int *)(a))` (viBufReset) cast an
    # integer to a host pointer. A body is rewritten as a text of its own (continuation lines included).
    def define_body(mm):
        head, body = mm.group(1), mm.group(2)
        sub = []
        nb = cast_transforms(body, sub)
        if sub:
            nonlocal n_done
            n_done += int(sub[0].split()[-1])
        return head + nb
    new = re.sub(r"^([ \t]*#[ \t]*define[ \t]+\w+(?:\([^)\n]*\))?)((?:[^\n]*\\\n)*[^\n]*)$", define_body, new,
                 flags=re.M) if "#" in new else new
    if n_done:
        report.append("T9 casts %d" % n_done)
    if n_elem:
        report.append("T19 element addresses %d" % n_elem)
    return new


def elem_address(operand):
    """T19 (port14): a pointer cast's operand `&X[i]` (X an lvalue path: `field_0`, `this->a.b`, `p->q`) -> (X, i), else
    None. Retail computes such an address without touching memory (Fixup__16CB3TriggerStructFUi 0x269D20: `lw; addu;
    sw` per relocated member); through gp<T>::operator[] it was a checked dereference, fatal when the sum leaves RAM."""
    mm = re.match(r"&\s*((?:this\s*->\s*)?\w+(?:\s*(?:->|\.)\s*\w+)*)\s*\[", operand)
    if not mm or not operand.endswith("]"):
        return None
    d, k = 0, mm.end() - 1
    for k in range(mm.end() - 1, len(operand)):
        if operand[k] == "[":
            d += 1
        elif operand[k] == "]":
            d -= 1
            if d == 0:
                break
    if k != len(operand) - 1 or d != 0:
        return None
    idx = operand[mm.end():-1].strip()
    if not idx or "[" in idx and idx.count("[") != idx.count("]"):
        return None
    return mm.group(1), idx


def hoist_late_includes(text, report):
    """gen1 (mac1, clang): T7 opens the unit namespace after the unit's last #include. When a unit defines a class above
    a later #include (two drafts in one file: B3AIArbitrator_0019D4D0's second draft includes b3/GtMathPs2.h after
    the first one's class CB3AIArbitrator) and defines its members below it, the class stays global while the member
    definitions go into the namespace: clang refuses ("namespace ... does not enclose"); GCC -fpermissive let it
    through. Then the includes after the first class definition move up to just before it (headers are
    self-contained)."""
    m = mask(text)
    first = re.search(r"^[ \t]*(?:class|struct|union)\s+[A-Za-z_]\w*[^;\n{]*\{", m, re.M)
    if not first:
        return text
    late = [x for x in re.finditer(r"^[ \t]*#\s*include[^\n]*\n", m, re.M) if x.start() > first.start()]
    if not late:
        return text
    above = m[first.start():late[-1].start()]
    below = m[late[-1].end():]
    classes = set(re.findall(r"^[ \t]*(?:class|struct|union)\s+([A-Za-z_]\w*)[^;\n{]*\{", above, re.M))
    if not any(re.search(r"\b%s\s*::\s*~?[A-Za-z_]\w*\s*\(" % re.escape(c), below) for c in classes):
        return text
    moved = "".join(text[x.start():x.end()] for x in late)
    for x in reversed(late):
        text = text[:x.start()] + text[x.end():]
    report.append("T7 late includes hoisted: %d" % len(late))
    return text[:first.start()] + moved + text[first.start():]


# ---------------------------------------------------------------- MWCC's constant folds (gen1)
FOLD_SITES_FILE = os.path.join(PORT_ROOT, "config", "fold_sites.tsv")
_FOLD_SITES = []


def fold_sites(path=None):
    """config/fold_sites.tsv: [(address, retail_pc, constant, find, replace)]."""
    if path is None and _FOLD_SITES:
        return _FOLD_SITES
    rows = []
    p = path or FOLD_SITES_FILE
    if os.path.exists(p):
        for line in open(p):
            if not line.strip() or line.startswith("#"):
                continue
            c = line.rstrip("\n").split("\t")
            if len(c) != 5:
                die("%s: not 5 tab-separated fields: %s" % (p, line.rstrip()))
            rows.append((int(c[0], 16), int(c[1], 16), int(c[2], 16), c[3], c[4]))
    if path is None:
        _FOLD_SITES[:] = rows
    return rows


def fold_site_transforms(text, addrs, where, report, sites=None):
    """gen1: a float expression MWCC folded at compile time (round to nearest) that the generated code would compute at
    run time in ps2f (PS2 chop), because its constants pass through an inline helper's ps2f parameters
    (func_001D1130's `inl7(0.03f, 1.0f, t)`: retail loads 1.0f - 0.03f = 0x3F7851EC, the host chopped 0x3F7851EB). Each
    config/fold_sites.tsv entry of a function in the unit replaces its exact text once, with retail's constant spelled
    out [C: the pc]; text not found, or found more than once, is fatal (the table no longer fits the source)."""
    for a, pc, const, find, rep in (sites if sites is not None else fold_sites()):
        if a not in addrs:
            continue
        n = text.count(find)
        if n != 1:
            die("%s: config/fold_sites.tsv entry for %08X found %d times (want 1): %s" % (where, a, n, find))
        text = text.replace(find, "%s /* gen1 [C] MWCC fold: retail 0x%08X at 0x%08X */" % (rep, const, pc))
        report.append("fold %08X at %08X" % (a, pc))
    return text


# ---------------------------------------------------------------- static data members (T12)
STATIC_MEMBERS = []   # (class, name, type, address, is_array, from_header) from every transformed header and unit (T12)


def static_member_transforms(text, decomp, report, apply_header_statics=False):
    """T12: a static data member of a class the unit declares (`static CGtV2d mSunPos;` in CB3SunRenderer) is guest data
    at the address of its mangled name (`mSunPos__14CB3SunRenderer` in the decomp's symbol_addrs_b3names.txt). The
    declaration is removed and every use (`mSunPos`, `CB3SunRenderer::mSunPos`) becomes a guest global. Members with no
    address stay (an undefined reference at link time: loud)."""
    m = preproc_mask(mask(text))
    edits, subst = [], []
    for mm in re.finditer(r"\b(class|struct)\s+([A-Za-z_]\w*)[^;{()]*\{", m):
        cls = mm.group(2)
        lo = mm.end()
        hi = match_brace(m, lo - 1)
        depth, last = 0, lo
        for i in range(lo, hi):
            c = m[i]
            if c in "{(":
                depth += 1
            elif c in "})":
                depth -= 1
            elif c == ";" and depth == 0:
                st = m[last:i]
                # boot2: `static const T name[];` too (no initialiser: retail's .rodata, e.g. CB3SkyDomeRenderer::
                # maDefinitions 0x4B9CA0); an in-class constant with an initialiser has `=` and stays a host constant
                sm = re.match(r"^(\s*(?:(?:public|private|protected)\s*:\s*)*)static\s+(?!constexpr\b|inline\b)([^;=(){}]*?)\s*\b([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*$", st)
                if sm and sm.group(2).strip() and re.sub(r"\bconst\b", "", sm.group(2)).strip():
                    name = sm.group(3)
                    sym = "%s__%d%s" % (name, len(cls), cls)
                    if sym in decomp.symbols:
                        t = re.sub(r"\bconst\b", "", sm.group(2)).strip()
                        stars = t.count("*")
                        t = t.replace("*", "").strip()
                        for _ in range(stars):
                            t = "b3host::gp<%s>" % t
                        if sm.group(4):
                            t = "%s%s" % (t, "")   # arrays: address of the first element
                        start = last + len(sm.group(1))
                        edits.append((start, i + 1, "/* decomp2host T12: static %s -> guest 0x%08X */" % (name, decomp.symbols[sym])))
                        subst.append((cls, name, t, decomp.symbols[sym], bool(sm.group(4))))
                last = i + 1
    STATIC_MEMBERS.extend(x + (not apply_header_statics,) for x in subst)   # the last field: from a header
    if not subst and not apply_header_statics:
        return text
    text = apply(text, edits)
    if apply_header_statics:
        mine = {(c, n) for c, n, _, _, _ in subst}
        m0 = preproc_mask_keep_defines(mask(text))
        # gen1 (speed): `\bNAME\b` for an identifier NAME matches exactly when a maximal \w+ run of the text is NAME, so
        # a set lookup gives the same list (one regex per static member of the whole generation per unit was ~60% of
        # the transform phase)
        words0 = set(re.findall(r"\w+", m0))
        # gen1: a static the unit does not declare takes the shared header's declaration over another unit's private
        # class view, whichever came first: B3CameraAttribs_002F8AA0's own CB3PlayerCameraManager has
        # `static UnkStruct_002F8AA0_1 *mgpCameraBuffer;` (its unit's type), the header `static int mgpCameraBuffer;`,
        # and B3CameraAttribs sorts first: B3PlayerCameraManager.cpp got a type it never declares ('not declared')
        other = {}
        for x in STATIC_MEMBERS:
            k = (x[0], x[1])
            if k in mine or x[1] not in words0:
                continue
            if k not in other or (x[5] and not other[k][5]):
                other[k] = x
        subst = subst + [x[:5] for x in other.values()]
    m = None
    for cls, name, t, addr, arr in subst:
        if m is None:   # gen1 (speed): re-masked only after an edit (the same text gives the same mask)
            m = preproc_mask_keep_defines(mask(text))
        rep = ("(&b3host::guest_global<%s>(0x%08Xu))" if arr else "(b3host::guest_global<%s>(0x%08Xu))") % (t, addr)
        e2 = []
        # gen1: not right after `::` unless qualified by this class: CB3SparkRenderer's static mnNumRenderableArrays
        # rewrote the name in `CB3PopcornRenderer::mnNumRenderableArrays` (B3PopcornRendererPS2's 'expected
        # unqualified-id', in a generation that has both)
        for um in re.finditer(r"(?<![\w.>:])(?:%s\s*::\s*)?%s\b" % (re.escape(cls), re.escape(name)), m):
            pre = m[:um.start()].rstrip()
            if pre.endswith(".") or pre.endswith("->"):
                continue
            e2.append((um.start(), um.end(), rep))
        if e2:
            text = apply(text, e2)
            m = None
        report.append("T12 static %s::%s -> 0x%08X" % (cls, name, addr))
    return text


# ---------------------------------------------------------------- function-pointer typedefs (T14)
def fnptr_typedef_transforms(text, report):
    """T14: `typedef R (*Name)(Args);` -> `typedef b3host::gfn<R(Args)> Name;`: a guest function pointer is 4 bytes
    (members of that type keep their offsets: UnkFn_RenderCallback) and calls through it reach the guest function
    (b3host/call.h gfn). A host function assigned to one stops the compile (loud)."""
    m = preproc_mask(mask(text))
    edits = []
    for mm in re.finditer(r"\btypedef\s+([^;{}()]*?)\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\(([^;{}]*)\)\s*;", m):
        if "(" in mm.group(3):
            continue
        edits.append((mm.start(), mm.end(), "typedef b3host::gfn<%s(%s)> %s; /* decomp2host T14 */" % (
            text[mm.start(1):mm.end(1)].strip(), text[mm.start(3):mm.end(3)].strip(), mm.group(2))))
        report.append("T14 %s" % mm.group(2))
    # the decomp's own portable form (include/b3/B3Ptr.h): B3_FNPTR(Name, Ret, (Args)), whose host branch is a bare
    # `unsigned int` (not callable); here a gfn like the plain typedef (Gt2dObject.h UnkFn_RenderCallback)
    for mm in re.finditer(r"\bB3_FNPTR\s*\(\s*([A-Za-z_]\w*)\s*,\s*([^,()]+?)\s*,\s*\(([^;{}]*)\)\s*\)", m):
        if "(" in mm.group(3) or re.match(r"^\s*#", text[text.rfind("\n", 0, mm.start()) + 1:mm.start()] + " "):
            continue
        edits2 = (mm.start(), mm.end(), "typedef b3host::gfn<%s(%s)> %s /* decomp2host T14 */" % (
            mm.group(2).strip(), text[mm.start(3):mm.end(3)].strip(), mm.group(1)))
        edits.append(edits2)
        report.append("T14 %s" % mm.group(1))
    return apply(text, edits)


# ---------------------------------------------------------------- virtual members (T13)
VIRTUALS = {}   # (class simple name, method name) -> vtable slot byte offset, for the call stubs


def virtual_transforms(text, report):
    """T13: MWCC puts a class's vptr where its first virtual function is declared (decomp docs/known.md N119), and the
    decomp declares the virtuals at that offset; the host compiler would put an 8-byte vptr at offset 0. So in every
    class body: the first `virtual` declaration becomes `b3host::gaddr b3_vptr;` followed by the declaration without
    `virtual`, the others lose `virtual` (and `= 0`), a virtual destructor becomes a comment (its slot still counts).
    A call to a de-virtualised method that the unit does not define gets a stub calling through the guest vtable
    (b3host::vcall_this, slot from VIRTUALS). Slot: the decomp's vfunc_XX name gives XX (the slot's byte offset,
    decomp convention); otherwise, in a class without bases, the declaration order (slot 0 at +8) [L: MWCC lays
    out a base-less class's vtable in declaration order]. A named virtual in a derived class stops the generation."""
    m = preproc_mask(mask(text))
    edits = []
    for mm in re.finditer(r"\b(class|struct)\s+([A-Za-z_]\w*)\s*(:[^;{()]*)?\{", m):
        cls, bases = mm.group(2), mm.group(3)
        lo = mm.end()
        hi = match_brace(m, lo - 1)
        stmts, depth, last = [], 0, lo
        for i in range(lo, hi):
            c = m[i]
            if c in "{(":
                depth += 1
            elif c in ")":
                depth -= 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    stmts.append((last, i + 1))
                    last = i + 1
            elif c == ";" and depth == 0:
                stmts.append((last, i))
                last = i + 1
        virt = []
        for a, b in stmts:
            st = m[a:b]
            vm = re.match(r"^(\s*(?:(?:public|private|protected)\s*:\s*)*)virtual\b", st)
            if vm:
                virt.append((a + len(vm.group(1)), b, st[len(vm.group(1)):]))
        if not virt:
            continue
        for k, (a, b, st) in enumerate(virt):
            nm_ = re.search(r"(~?[A-Za-z_]\w*)\s*\(", st)
            if not nm_:
                die("virtual member not understood in %s: %s" % (cls, st.strip()[:80]))
            name = nm_.group(1)
            vf = re.match(r"vfunc_([0-9A-Fa-f]+)$", name)
            if vf:
                slot = int(vf.group(1), 16)
            elif bases:
                die("virtual %s::%s in a derived class: vtable slot unknown" % (cls, name))
            else:
                slot = 8 + 4 * k
            new = re.sub(r"^virtual\s+", "", text[a:b])
            new = re.sub(r"\)\s*(const\s*)?=\s*0\s*$", r") \1", new)
            if name.startswith("~"):
                new = "/* decomp2host T13: virtual destructor (slot 0x%X) */" % slot
            elif "{" not in st:
                VIRTUALS[(cls, name)] = slot
            pre = "b3host::gaddr b3_vptr; /* decomp2host T13: MWCC vptr */ " if k == 0 else ""
            edits.append((a, b, pre + new))
        report.append("T13 %s: %d virtual(s)" % (cls, len(virt)))
        POLY.add(cls)
    return apply(text, edits)


POLY = set()   # boot1 T26: classes with a T13 vptr (own virtuals, or a base that has one), across headers and units


def ctor_vptr_transforms(text, decomp, report):
    """T26 (boot1): MWCC's constructor of a class with a vptr stores the class's vtable address in it (after the base
    constructors, before the body) [C __ct__12CUnk0028B700Fv 0x28B700: `lui/addiu v0, __vt__12CUnk0028B700; sw v0,
    0(a0)`]; T13 made the vptr a plain member (b3_vptr), so a generated constructor left it unset (native2 checker:
    `__ct__12CUnk0028B700Fv` value difference; native boot: the sound sinit func_004D5EE0's array kept zero vptrs).
    Every constructor definition of such a class (out of class `C::C(...) {`, or in its body) now starts with
    `b3_vptr = <__vt__<len><C>>;`, the address from the decomp's symbol tables; a polymorphic class without a
    `__vt__` symbol stops the generation (its constructor cannot be right)."""
    m = preproc_mask(mask(text))
    # classes deriving from a polymorphic base are polymorphic too (simple names; base lists as written)
    for mm in re.finditer(r"\b(?:class|struct)\s+([A-Za-z_]\w*)\s*:([^;{()]*)\{", m):
        if any(re.search(r"\b%s\b" % re.escape(b), mm.group(2)) for b in POLY):
            POLY.add(mm.group(1))
    edits = []
    words = set(re.findall(r"[A-Za-z_]\w*", m))   # boot2: only the classes the unit names (same edits, ~5 s less)
    for cls in sorted(POLY & words):
        for mm in re.finditer(r"\b%s\s*::\s*%s\s*\(|(?<![\w~:.>])%s\s*\(" % ((re.escape(cls),) * 3), m):
            # out of class needs `C::C(`; in class, the match must lie inside class C's body
            out_of_class = "::" in m[mm.start():mm.end()]
            if not out_of_class:
                inside = False
                for cm in re.finditer(r"\b(?:class|struct)\s+%s\s*(?::[^;{()]*)?\{" % re.escape(cls), m):
                    if cm.end() <= mm.start() <= match_brace(m, cm.end() - 1):
                        inside = True
                if not inside:
                    continue
            close = match_paren(m, mm.end() - 1)
            if close is None:
                continue
            j = close + 1
            while j < len(m) and m[j] in " \t\n":
                j += 1
            if m.startswith("const", j):
                continue
            if j < len(m) and m[j] == ":":   # a mem-initialiser list: up to the body's brace at depth 0
                d = 0
                while j < len(m) and not (m[j] == "{" and d == 0):
                    if m[j] in "(":
                        d += 1
                    elif m[j] == ")":
                        d -= 1
                    j += 1
            if j >= len(m) or m[j] != "{":
                continue   # a declaration, not a definition
            vt = "__vt__%d%s" % (len(cls), cls)
            addr = decomp.symbols.get(vt)
            if addr is None:
                die("constructor of %s (a class with a vptr): no %s in the decomp's symbol tables" % (cls, vt))
            edits.append((j + 1, j + 1, " b3_vptr = 0x%08Xu; /* decomp2host T26: MWCC stores %s */" % (addr, vt)))
            report.append("T26 %s: vptr = %s" % (cls, vt))
    return apply(text, edits) if edits else text


HOST_LIBC = {"memcpy", "memset", "memmove", "memcmp", "strlen", "strcmp", "strncmp", "strcpy", "strncpy", "strcat",
             "strchr", "strrchr", "strstr"}


def libm_transforms(text, decomp, report):
    """T16b: any other libm function a unit declares (`extern "C" float sinf(float);`) would be resolved by the host
    compiler to its builtin/host libm (no undefined symbol, so no call stub: ModulateRpm__11CB3SoundESM used host sinf,
    port7). It becomes a static inline that calls retail's function through call_guest (the native libm
    override when enabled, else the original), with the declared parameter types."""
    def rep_one(mm):
        name, params = mm.group(2), mm.group(3)
        addr = function_address(decomp, name)
        if addr is None:
            # the declaration would bind to the host's libm (other rounding; on Windows x64 another ABI for ps2f):
            # declare the decomp's name instead (acosf = func_0011E328) [C audio1 2026-11-05: 96 one-ulp DIFFs]
            sys.stderr.write("decomp2host: T16b: %s is not a decomp name; this declaration binds to the host libm\n"
                             % name)
            return mm.group(0)
        ps = [x.strip() for x in params.split(",") if x.strip() and x.strip() != "void"]
        decl, args = [], []
        for k, t in enumerate(ps):
            t = re.sub(r"\s+[A-Za-z_]\w*$", "", t) if len(t.split()) > 1 and t.split()[-1] not in ("int", "float", "double", "long", "char") else t
            decl.append("%s p%d" % (t, k))
            args.append("p%d" % k)
        report.append("T16b %s -> 0x%08X" % (name, addr))
        return ("static inline %s %s(%s) { return b3host::call_guest<%s>(0x%08Xu%s); }   /* decomp2host T16b: retail libm */"
                % (mm.group(1).strip(), name, ", ".join(decl), mm.group(1).strip(), addr, "".join(", " + x for x in args)))
    names = "|".join(sorted(LIBM_NAMES - {"sqrtf"}))
    for mm in re.finditer(r'(?m)^[ \t]*extern\s*"C"\s*(?!(?:const\s+)?(?:float|int|double)\s)[A-Za-z_]\w*\s+(%s)\s*\(' % names, text):
        sys.stderr.write("decomp2host: T16b: %s declared with a non-float type; it binds to the host libm\n" % mm.group(1))
    return re.sub(r'(?m)^[ \t]*extern\s*"C"\s*((?:const\s+)?(?:float|int|double))\s+(%s)\s*\(([^;(){}]*)\)\s*;' % names,
                  rep_one, text)


def stack_frame_transforms(text):
    """T27 (audio1): locals whose address goes into guest memory (list heads that heap entries point back to, retail's
    stack blocks passed to callees that keep them) must live on the guest stack, at retail's own addresses. The port C
    writes `B3_RETAIL_FRAME(bytes);` as the first statement of such a function (retail's frame size, addiu sp, sp,
    -bytes) and `B3_FRAME_LOCAL(T, offset)` for the local at retail's sp offset; on the host the frame is a
    b3host::GuestStackFrame (the guest sp moves down by `bytes` for the function's scope, so callees run on retail's sp)
    and the local a host pointer into it."""
    text = re.sub(r'\bB3_RETAIL_FRAME\s*\(\s*([^()]+?)\s*\)\s*;', r'b3host::GuestStackFrame b3_frame(\1);', text)
    return re.sub(r'\bB3_FRAME_LOCAL\s*\(\s*([^,()]+?)\s*,\s*([^()]+?)\s*\)',
                  r'b3host::gp<\1>::from_raw(b3_frame.addr() + (\2)).get()', text)


# ---------------------------------------------------------------- host locals escaping into guest memory (E1)
# boot2 (2026-11-06): natively a local lives on the HOST stack. A typed pointer to it passed to a guest call
# is safe (b3host::ptr_arg bounces it through a guest-stack window for the call), but two uses are not:
#   (a) its address turned into an integer word (b3host::icast<...>(&x), `(unsigned int)info`): guest code gets a host
#       address [C rwawavedict.cpp 0x29EF00: RwaWaveDictForAllWaves(dict, cb, (unsigned int)info) -> guest_address
#       fault in the boot];
#   (b) its address stored into guest memory (`obj->field = &x;`): the pointer outlives any call.
# Such a local must be a T27 guest-stack local at retail's offset (B3_RETAIL_FRAME / B3_FRAME_LOCAL). The check runs
# on the generated text, per function, over its value locals and by-value parameters (arrays, structs, scalars).
ESCAPE_DECL = re.compile(r"(?:(?<=[;{}(,])|^)\s*(?:const\s+|volatile\s+|struct\s+|unsigned\s+|signed\s+)*"
                         r"([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<[^;<>()]*>)?)\s+([A-Za-z_]\w*)\s*(\[[^\]\n]*\])?\s*(?=[;=,)\[(]|__attribute__)", re.M)
# gen1 (T31 audit): a type never ends in `:` (`default: a = 0x2C;` was a declaration of `a`, type `default:`)
# gen1 (T31 audit): also `T x(a, b);` (direct-initialised) and T8's `T x __attribute__((uninitialized)); x = e;`
ESCAPE_NOT_TYPES = {"return", "else", "case", "goto", "delete", "throw", "do", "new", "sizeof", "typedef", "using",
                    "namespace", "if", "while", "for", "switch", "static"}


ESCAPE_SCALARS = {"int", "unsigned", "char", "short", "long", "bool", "float", "double", "ps2f", "u_long128", "RwInt32",
                  "RwUInt32", "RwInt16", "RwUInt16", "RwInt8", "RwUInt8", "RwBool", "RwReal", "RwChar", "u8", "u16",
                  "u32", "u64", "s8", "s16", "s32", "s64", "b3host::ps2_long", "b3host::ps2_ulong", "size_t",
                  "std::uint32_t", "std::int32_t", "std::uint64_t", "std::int64_t", "std::uint8_t", "std::uint16_t"}


def comma_declarators(m, mm):
    """gen1 (T31 audit): the declarators of the statement an ESCAPE_DECL match starts, when it is a comma list
    (`T a, b = e, c[2];`): [(name, array suffix or '', start, end, initialiser text)], the first one included; the
    declarators with a `*` / `&` are left out (pointers, references); [] for an ordinary single declaration."""
    end = mm.end()
    d, j = 0, mm.end()
    while j < len(m):
        c = m[j]
        if c in "([{":
            d += 1
        elif c in ")]}":
            d -= 1
            if d < 0:
                return []
        elif c == ";" and d == 0:
            break
        j += 1
    stmt_start, stmt = mm.start(2), m[mm.start(2):j]
    parts, d, cur, pos = [], 0, "", stmt_start
    starts = [stmt_start]
    for k, c in enumerate(stmt):
        if c in "([{":
            d += 1
        elif c in ")]}":
            d -= 1
        if c == "," and d == 0:
            parts.append(cur)
            cur = ""
            starts.append(stmt_start + k + 1)
        else:
            cur += c
    parts.append(cur)
    if len(parts) < 2:
        return []
    out = []
    for p_, s_ in zip(parts, starts):
        dm = re.match(r"^\s*([A-Za-z_]\w*)\s*((?:\[[^\]]*\])?)\s*(=.*)?$", p_, re.S)
        if dm:
            out.append((dm.group(1), dm.group(2), s_, s_ + len(p_), (dm.group(3) or "").strip()))
    return out


def decl_in_scope(body, name, use):
    """gen1 (T31 audit): the start (in body) of the declaration of `name` that is in scope at `use`: the latest
    ESCAPE_DECL of that name before it whose enclosing block still contains `use` (an inner block's declaration of
    the same name shadows an outer one, a sibling block's has ended); None if none (a parameter)."""
    best = None
    for mm in ESCAPE_DECL.finditer(body[:use]):
        if mm.group(1) in ESCAPE_NOT_TYPES:
            continue
        if mm.group(2) != name and name not in [x[0] for x in comma_declarators(body, mm)]:
            continue
        d, k = 0, mm.end()
        ok = True
        while k < use:
            c = body[k]
            if c == "{":
                d += 1
            elif c == "}":
                d -= 1
                if d < 0:
                    ok = False   # its block closed before the use
                    break
            k += 1
        if ok:
            best = mm.start()
    return best


def escape_lint(text, guest_names=None, only_c=False):
    """[(function head, expression, local)] for every (a) / (b) / (c) escape of a value local or by-value parameter.
    (c) needs the unit's guest calls: `guest_names` (call_stubs knows them), else the `#define ... guest_fn` lines."""
    hits = []
    m = mask(text)
    if guest_names is None:
        guest_names = set(re.findall(r"#define\s+([A-Za-z_]\w*)\s+\(b3host::guest_fn<", text))
    for f in top_level_functions(text):
        head, body = f[4], m[f[1]:f[2] + 1]
        if re.match(r"\s*inline\s+auto\b", head) and re.search(r"\bb3host::(?:call_guest|vcall_this)\b", body):
            continue   # gen1 (T31 audit): a generated call stub (its parameters go by value through stub_arg)
        locals_ = {}
        param_types = {}
        # by-value parameters: the head's parenthesised list
        pm = re.search(r"\((.*)\)\s*(?:const)?\s*$", re.sub(r"\s+", " ", head))
        decls = [(pm.group(1) if pm else "")]
        for chunk in decls:
            for p in split_top(chunk):
                if "*" in p or "&" in p or not p.strip():
                    continue
                mm = re.match(r"^(?:const\s+)?([\w:<> ]+?)\s+([A-Za-z_]\w*)\s*(\[[^\]]*\])?$", p.strip())
                if mm:
                    locals_[mm.group(2)] = bool(mm.group(3))
                    param_types[mm.group(2)] = mm.group(1).split()[-1]
        locals_types = dict(param_types)
        for mm in ESCAPE_DECL.finditer(body):
            t, name, arr = mm.group(1), mm.group(2), mm.group(3)
            if t not in ESCAPE_NOT_TYPES and name not in ESCAPE_NOT_TYPES and \
                    not re.search(r"\bstatic\b[^;{}]*$", body[max(0, mm.start() - 40):mm.start(2)]):
                # gen1 (T31 audit): `T a, b = e, c[2];`: the later value declarators are locals of type T too
                for dn, darr, _ds, _de, _di in comma_declarators(body, mm)[1:]:
                    locals_types[dn] = t
                    locals_[dn] = bool(darr)
            locals_types[name] = t
            if t in ESCAPE_NOT_TYPES or name in ESCAPE_NOT_TYPES:
                continue
            # a pointer or reference declaration has * / & between the type and the name: the regex requires only
            # whitespace there, so those are excluded already
            if re.search(r"\bstatic\b[^;{}]*$", body[max(0, mm.start() - 40):mm.start(2)]):
                continue
            locals_[name] = bool(arr)
        if not locals_:
            continue
        names = "|".join(re.escape(n) for n in sorted(locals_, key=len, reverse=True))
        # &x, &x[k], &x.f are addresses; a local ARRAY's bare name decays to its address, but not when indexed /
        # accessed (`vals[0]`, `xyoffset[k]` are element values)
        arrays = "|".join(re.escape(n) for n in locals_ if locals_[n]) or "(?!x)x"
        addr_of = r"(?:&\s*(?:%s)\b|\b(?:%s)\b(?!\s*(?:\[|\.|->)))" % (names, arrays)
        # (a) icast of an address of a local (or a local array's decay)
        for mi in ([] if only_c else re.finditer(r"b3host::icast<[^<>()]*>\s*\(", body)):
            j, d = mi.end(), 1
            while j < len(body) and d:
                d += 1 if body[j] == "(" else -1 if body[j] == ")" else 0
                j += 1
            arg = body[mi.end():j - 1]
            ma = re.search(r"^\s*\(*\s*" + addr_of, arg)
            if ma:
                nm = re.search(r"[A-Za-z_]\w*", ma.group(0)).group(0)
                dp = decl_in_scope(body, nm, mi.start())
                hits.append((head.strip()[:90], "icast(" + text[f[1] + mi.end():f[1] + j - 1].strip()[:60] + ")", nm, (f[1], f[2]),
                             "param" if dp is None else f[1] + dp))
        # (c) decomp1 (2026-11-06): a local AGGREGATE (array, or a struct-typed local), or a pointer into it,
        # passed to a guest call: b3host::ptr_arg bounces only max(256, sizeof(pointee)) bytes from that pointer, so a
        # callee writing past it loses the writes (SoundESMHigh::Update passed outs.a, ESMStruct::Update filled 12
        # 0x50-byte outputs: 146 words differed) -- the whole object must live on the guest stack
        def scalar(t):
            return t in ESCAPE_SCALARS or t in INT_TYPES or t in UNIT_INT_TYPES or bool(re.match(r"^(?:E|UnkEnum_)\w+$", t))
        # behind DECOMP2HOST_T31C=1 (2026-11-06): the native build only (gen/decomp's units stay as they were)
        aggs = [] if os.environ.get("DECOMP2HOST_T31C") != "1" else \
            [n for n in locals_ if locals_[n] or (n in locals_types and not scalar(locals_types[n]))]
        if aggs:
            agg_re = r"^\s*(?:\([^()]*\)\s*)*&?\s*\(?\s*(%s)\b" % "|".join(re.escape(n) for n in sorted(aggs, key=len, reverse=True))
            callee_re = r"(?:\b(%s)|(?:->|\.)\s*(field_[0-9A-Fa-f]+))\s*\(" % ("|".join(re.escape(g) for g in guest_names) or "(?!x)x")
            for mc in re.finditer(callee_re, body):
                j, d = mc.end(), 1
                while j < len(body) and d:
                    d += 1 if body[j] == "(" else -1 if body[j] == ")" else 0
                    j += 1
                for arg in split_macro_args(body[mc.end():j - 1]):
                    ma = re.match(agg_re, arg)
                    if ma:
                        dp = decl_in_scope(body, ma.group(1), mc.start())
                        hits.append((head.strip()[:90], "guest call %s(... %s ...)" % (mc.group(1) or mc.group(2), arg.strip()[:40]), ma.group(1), (f[1], f[2]),
                                     "param" if dp is None else f[1] + dp))
        # (b) a store of a local's address into a member of a non-local object
        # gen1: also a member of an element of a local array (`stack[0].field_0 = &root;`, CGtCollisionKDTree::
        # IntersectLineNearest 0x3BF720: the root node's host address in a gp field) and an element of a local array
        # (`ptrs[i] = &x;`; a host-pointer array is a false positive that only costs a guest-stack local)
        for ms in ([] if only_c else re.finditer(r"([A-Za-z_][\w\.\[\]]*\s*->\s*[\w\.\[\]>-]+|\b[A-Za-z_]\w*(?:\s*\[[^\]\n]*\])*\s*\.\s*[\w\.\[\]]+|\b[A-Za-z_]\w*(?:\s*\[[^\]\n]*\])+)\s*=\s*(?!=)\s*\(*\s*" + addr_of, body)):
            # boot2 (2026-11-07): a member of a local is no exception -- a guest-pointer (gp<T>) field needs the
            # pointee's guest address whatever holds the field (GtSoundStream func_003865A0: `f.field_0 = &b;`, then
            # _rwaStreamSetStreamPos(&f): guest_address fault on b, a host local)
            tail = body[ms.end(1):ms.end()]
            nm = re.search(r"[A-Za-z_]\w*", tail[tail.index("=") + 1:]).group(0)
            dp = decl_in_scope(body, nm, ms.start())
            hits.append((head.strip()[:90], text[f[1] + ms.start():f[1] + ms.end()].strip()[:80], nm, (f[1], f[2]),
                         "param" if dp is None else f[1] + dp))
    return hits


# gen1: the writable slack after a T31 char array that a sprintf-family call fills (call.h GuestLocalArray's Slack)
SPRINTF_SLACK = 32
# the units (a name in their text) whose `unsigned short` strings a CGtUnicode Print* call fills get the same slack
USHORT_SLACK_UNITS = ("func_00458AE0",)


def escape_fix(text, names):
    """T31 (boot2): a decomp unit's escaping value locals (E1) live on the guest stack, declared where the local was.
    gen1 (audit): through b3host::GuestLocal<T> / GuestLocalArray<T, N> (call.h), which keep the declaration's
    lifetime: constructed there from its own initialiser (`T x;` default, `T x = e;` copy, `T x = {...};` brace,
    `T x(a, b);` direct, an initialised array element-wise from a host copy of its initialiser), destroyed when the
    scope ends (also per loop iteration and on early return), the guest sp restored after; `x` is a reference to it.
    Items: a name, (name, function span), or (name, function span, declaration offset) -- the declaration in scope at
    the escaping use (decl_in_scope: a shadowing inner declaration of the same name, a sibling block's)
    [L: not retail's offset -- the stack range is never compared; a T27 port file gives retail's offsets instead]."""
    m = mask(text)
    edits = []
    items = set()
    for it in names:
        if isinstance(it, str):
            items.add((it, None, None))
        else:
            it = tuple(it) + (None,) * (3 - len(it))
            items.add((it[0], tuple(it[1]) if it[1] is not None else None, it[2]))
    done = set()
    for nm, span, declpos in sorted(items, key=lambda x: (x[0], x[1] or (-1, -1), x[2] if isinstance(x[2], int) else -1)):
        if declpos == "param" and span is not None and ("param", nm, span) not in done:
            # gen1 (T31 audit): a by-value parameter whose address escapes: renamed, and a guest-stack copy under its
            # name made at the body's start (the callee's own copy of the argument, as MWCC's frame holds it)
            pc = m.rfind(")", 0, span[0])
            po = pc
            d_ = 0
            while po > 0:
                if m[po] == ")":
                    d_ += 1
                elif m[po] == "(":
                    d_ -= 1
                    if d_ == 0:
                        break
                po -= 1
            plist = text[po + 1:pc]
            ofs = po + 1
            for prm in split_top(plist):
                pm_ = re.match(r"^\s*((?:const\s+)?(?:struct\s+|class\s+)?[A-Za-z_][\w:]*(?:\s*<[^<>]*>)?(?:\s+[A-Za-z_]\w*)*?)\s+(%s)\s*$" % re.escape(nm), prm)
                if pm_ and "*" not in prm and "&" not in prm and "[" not in prm:
                    at = plist.find(prm) + ofs + pm_.start(2)
                    ty = re.sub(r"\s+", " ", pm_.group(1).strip())
                    edits.append((at, at + len(nm), "b3_arg_" + nm))
                    edits.append((span[0] + 1, span[0] + 1, " b3host::GuestLocal<%s> b3_esc_%s(b3host::copy_init, b3_arg_%s); %s &%s = *b3_esc_%s;"
                                  " /* decomp2host T31: a parameter escaping into guest memory */" % (ty, nm, nm, ty, nm, nm)))
                    done.add(("param", nm, span))
                    break
            continue
        if declpos == "param":
            continue
        for mm in ESCAPE_DECL.finditer(m):
            if mm.group(1) in ESCAPE_NOT_TYPES:
                continue
            cl = comma_declarators(m, mm)
            if mm.group(2) != nm and nm not in [x[0] for x in cl]:
                continue
            if isinstance(declpos, int) and mm.start() != declpos:
                continue
            if span is not None and not (span[0] <= mm.start() < span[1]):
                continue
            if cl and len(cl) == len(split_top(m[mm.start(2):m.find(";", mm.start(2))])) and mm.start() not in done:
                # gen1 (T31 audit): `T a, b = e;` with b escaping: one declaration per declarator, b's a holder
                k = mm.start() - 1
                while k >= 0 and m[k] in " \t\n":
                    k -= 1
                if k >= 0 and m[k] not in ";{}":
                    continue
                mw = m[mm.start():mm.start(2)]
                k0 = mm.start() + len(mw) - len(mw.lstrip())
                lead = text[mm.start():k0]
                t = re.sub(r"\s+", " ", mw.strip())
                pieces = []
                # every escaping declarator of this statement at once (`CGtV3d pos, at, vel;` with pos and at escaping)
                want = {x[0] for x in items if x[0] in [c_[0] for c_ in cl]
                        and (x[1] is None or x[1][0] <= mm.start() < x[1][1]) and x[2] != "param"}
                for dn, darr, ds, de, dinit in cl:
                    one = "%s %s%s%s;" % (t, dn, darr, (" " + dinit) if dinit else "")
                    pieces.append(escape_fix(one + "\n", [dn])[:-1] if dn in want else one)
                stmt_end = m.find(";", mm.start(2))
                edits.append((mm.start(), stmt_end + 1, lead + " ".join(pieces)))
                done.add(mm.start())
                break
            if mm.group(2) != nm:
                continue
            if isinstance(declpos, int) and mm.start() != declpos:
                continue
            if span is not None and not (span[0] <= mm.start() < span[1]):
                continue
            if mm.start() in done:
                break
            k = mm.start() - 1
            while k >= 0 and m[k] in " \t\n":
                k -= 1
            if k >= 0 and m[k] not in ";{}":
                continue   # only a statement-level declaration (not a for-init / parameter / comma list)
            end = m.find(";", mm.end())
            init = text[mm.end():end].strip()
            # gen1 (T31 audit): the type from the masked text (a comment above the declaration -- `// sp+0x20` in
            # func_004DB400 -- had become part of the type); `lead` keeps that comment
            mw = m[mm.start():mm.start(2)]
            k0 = mm.start() + len(mw) - len(mw.lstrip())
            lead = text[mm.start():k0]
            t = re.sub(r"\s+", " ", mw.strip())
            arr = mm.group(3)
            fr = "b3_esc_" + nm
            tag = " /* decomp2host T31: escapes into guest memory */"
            hoist = False
            if init.startswith("__attribute__((uninitialized))") or "/* decomp2host T20 */" in text[end:end + 30]:
                # T8 / T20 split a declaration a goto or a case label jumps across (a trivial type: the original jump was
                # valid only for one): the holder may not be crossed, so it goes to the function's start (constructed
                # there: the same for a trivial type) and the declaration goes; only when the function declares the
                # name once
                init = init[len("__attribute__((uninitialized))"):].strip() if init.startswith("__attribute__") else init
                if init or span is None or arr:
                    continue
                n_decl = sum(1 for x in ESCAPE_DECL.finditer(m[span[0]:span[1]]) if x.group(2) == nm)
                if n_decl != 1:
                    continue
                hoist = True
            if init.startswith(",") or (init and not init.startswith(("=", "("))):
                continue   # `T a, b;` and the like: not rewritten (the check reports it)
            if arr:
                dims = re.findall(r"\[([^\]]*)\]", arr)
                if len(dims) != 1 or not dims[0].strip():
                    continue   # several dimensions / `T x[] = {...}`: not rewritten
                # gen1: a char buffer a sprintf-family call writes (its first argument, in this function) gets slack
                # past it before the canary: retail's frames tolerate the overflow of a short buffer (func_003B78D0's
                # "%d/" into char[4]: the NUL lands in the next slot) [L: the neighbour byte retail clobbers is not]
                where_ = m[span[0]:span[1]] if span is not None else m
                slack = ""
                # (also the 16-bit string of the units in USHORT_SLACK_UNITS that a CGtUnicode Print* call fills:
                # func_00458AE0's `unsigned short str[4]` takes PrintUInt of a count that may have four digits -- the
                # NUL lands in the next slot, as in retail. Only the unit seen to overflow: another unit's frame
                # would change layout, and with it what an uninitialised read sees [golden])
                if (re.fullmatch(r"(?:(?:unsigned|signed)\s+)?char", t) and re.search(
                        r"\b\w*v?s(?:n)?printf\s*\(\s*(?:&\s*)?%s\b" % re.escape(nm), where_)) or (
                        re.fullmatch(r"unsigned\s+short", t) and any(u in text for u in USHORT_SLACK_UNITS) and re.search(
                            r"\bPrint\w*__10CGtUnicodeF\w*\s*\(\s*%s\b" % re.escape(nm), where_)):
                    slack = ", %d" % SPRINTF_SLACK
                    tag = " /* decomp2host T31: escapes into guest memory; a sprintf target: %d bytes of slack */" % SPRINTF_SLACK
                decl = "%sb3host::GuestLocalArray<%s, (%s)%s> %s; %s (&%s)%s = *%s;" % (lead, t, dims[0].strip(), slack, fr, t, nm, arr, fr)
                if init.startswith("="):
                    decl += " { %s b3_ini_%s%s %s; for (std::size_t b3_i = 0; b3_i < (%s); b3_i++) %s[b3_i] = b3_ini_%s[b3_i]; }" % (
                        re.sub(r"^const\s+", "", t), nm, arr, init, dims[0].strip(), nm, nm)
                elif init:
                    continue
            elif not init:
                decl = "%sb3host::GuestLocal<%s> %s; %s &%s = *%s;" % (lead, t, fr, t, nm, fr)
            elif init.startswith("("):
                inner = init[1:init.rfind(")")] if init.endswith(")") else None
                if inner is None or not inner.strip():
                    continue   # `T x();` declares a function
                decl = "%sb3host::GuestLocal<%s> %s(%s); %s &%s = *%s;" % (lead, t, fr, inner, t, nm, fr)
            else:
                rhs = init[1:].strip()
                if rhs.startswith("{") and rhs.endswith("}"):
                    decl = "%sb3host::GuestLocal<%s> %s(b3host::brace_init%s); %s &%s = *%s;" % (
                        lead, t, fr, (", " + rhs[1:-1].strip()) if rhs[1:-1].strip() else "", t, nm, fr)
                else:
                    decl = "%sb3host::GuestLocal<%s> %s(b3host::copy_init, %s); %s &%s = *%s;" % (lead, t, fr, rhs, t, nm, fr)
            if hoist:
                edits.append((mm.start(), end + 1, lead + "/* decomp2host T31: %s moved to the function's start (crossed by a jump) */" % nm))
                edits.append((span[0] + 1, span[0] + 1, " " + decl.strip() + tag))
            else:
                edits.append((mm.start(), end + 1, decl + tag))
            done.add(mm.start())
            break
    return apply(text, sorted(edits)) if edits else text


def sqrtf_transforms(text):
    """T16: the decomp's `extern "C" float sqrtf(float);` is MWCC's intrinsic: one sqrt.s [C retail 0x263D84 in
    SpawnBouncySpark]. On the host the declaration (after T1: ps2f sqrtf(ps2f)) would bind to the host libm's sqrtf
    with another ABI on Windows x64 (a 4-byte struct in RCX/RAX vs XMM0: fix1, sqrt(108.5) = 2493). It becomes an
    inline model of sqrt.s on b3fp (PS2-faithful)."""
    return re.sub(r'(?m)^[ \t]*extern\s*"C"\s*float\s+sqrtf\s*\(\s*float\s*\w*\s*\)\s*;',
                  'static inline float sqrtf(float x) { return b3fp::sqrt(x); }   /* decomp2host T16: sqrt.s */', text)


# T22 (native2): C library functions the decomp declares with signatures of its own that clash with the host's
# (`extern "C" void strcasecmp(int, int);` in GtLobbyDirtySock.cpp, `void strncat(char *, void *, int)` in B3DataLists.cpp:
# 34 functions). Every use is renamed b3guest_<name>; the call stub resolves that to retail's own function (newlib in the
# ELF), called through the guest ABI with the decomp's declaration, as MWCC does.
GUEST_LIBC = {"strncat", "strcasecmp", "strncasecmp", "atoi", "atol", "atof", "strtol", "strtoul", "strtod", "sprintf",
              "snprintf", "vsprintf", "vsnprintf", "sscanf", "printf", "puts", "rand", "srand", "qsort", "abs", "labs",
              "strtok", "strdup", "strspn", "strcspn", "strpbrk", "memchr", "toupper", "tolower", "isdigit", "isalpha",
              "isspace", "isupper", "islower", "isalnum", "isprint", "malloc", "free", "calloc", "realloc"}
GUEST_LIBC_PREFIX = "b3guest_"


def guest_libc_transforms(text, report):
    m = preproc_mask(mask(text))
    edits = []
    for mm in re.finditer(r"(?<![\w.>:])(%s)\b(?=\s*\()" % "|".join(sorted(GUEST_LIBC)), m):
        edits.append((mm.start(), mm.end(), GUEST_LIBC_PREFIX + mm.group(1)))
    if edits:
        report.append("T22 guest libc %s" % ",".join(sorted({text[a:b] for a, b, _ in edits})))
    return apply(text, edits) if edits else text


def port_libc_values(text, report):
    """audio1: in a port-track unit, a guest C library function used as a value (`funcs.field_4 = free;`, RWA's default
    memory interface) gets the T22 prefix too, so it names retail's function (its registered guest code address), not
    the host's. Port units only: decomp units generate as before."""
    m = preproc_mask(mask(text))
    edits = [(mm.start(), mm.end(), GUEST_LIBC_PREFIX + mm.group(1))
             for mm in re.finditer(r"(?<![\w.>:])(%s)\b(?!\s*\()" % "|".join(sorted(GUEST_LIBC)), m)]
    if edits:
        report.append("T22 guest libc values %s" % ",".join(sorted({text[a:b] for a, b, _ in edits})))
    return apply(text, edits) if edits else text


_LIBC_PATS = []


def _libc_patterns():
    if not _LIBC_PATS:
        names = "|".join(sorted(HOST_LIBC))
        _LIBC_PATS.append(re.compile(r"(?:\bextern\s*\"C\"\s*)?[^;{}()]*\b(%s)\s*\([^;{}]*\)\s*;" % names))
        _LIBC_PATS.append(re.compile(r"\b(?:%s)\s*\(" % names))
    return _LIBC_PATS[0], _LIBC_PATS[1]


def libc_decl_transforms(text):
    """T15: a unit's own declaration of a C library function the host declares too (`extern "C" void *memcpy(void *,
    const void *, unsigned int);`) conflicts with the host's (size_t); the host's is used: same semantics on host
    pointers into guest memory."""
    m = preproc_mask(mask(text))
    edits = []
    bodies = None
    # boot2 (2026-11-07): the same matches as finditer over the whole pattern, found without its quadratic scan (the
    # leading [^;{}()]* rescanned every delimiter-free stretch from each position: 13 s of 33 in a 600-unit profile).
    # A match holds one name hit per segment (the name's `(` is a delimiter) and starts right after the previous
    # ;{}() -- so each hit's segment start is tried with the anchored pattern, non-overlapping as finditer's.
    pat, hit = _libc_patterns()
    last_end = 0
    for hm in hit.finditer(m):
        seg = max(m.rfind(c, 0, hm.start()) for c in ";{}()") + 1
        if seg < last_end:
            continue
        mm = pat.match(m, seg)
        if not mm:
            continue
        last_end = mm.end()
        if "=" in mm.group(0) or "return" in mm.group(0):
            continue
        if bodies is None:
            bodies = [(f[1], f[2]) for f in top_level_functions(text)]
        # a declaration only: outside function bodies, with a type before the name (port7: a memset *call* in
        # ResetMemberVariables__17CB3TrafficVehicleFv was dropped, the checker caught the unzeroed fields)
        if any(b0 <= mm.start() <= b1 for b0, b1 in bodies):
            continue
        if not re.search(r"[A-Za-z_*&]\s*\b%s\s*\($" % mm.group(1), mm.group(0)[:mm.group(0).find(mm.group(1)) + len(mm.group(1))] + "("):
            continue
        st = mm.start() + len(mm.group(0)) - len(mm.group(0).lstrip())
        edits.append((st, mm.end(), "/* decomp2host T15: host %s */" % mm.group(1)))
    return apply(text, edits)


CASE_LABEL = re.compile(r"^\s*(?:(?:case\b(?:[^:;]|::)*?[^:]:(?!:)|default\s*:)\s*)+")
# gen1: the type is a name or a qualified name (A::B), never a goto label (`c10_join: *out = f(...)` in a case:
# func_00452ED0's store through `out` had become `c10_join:* out; out = f(...)`, which -fpermissive accepted)
CASE_DECL = re.compile(r"^((?:const\s+)?(?:(?:unsigned|signed|struct|class|enum)\s+)*[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<[^;<>{}]*>)?)"
                       r"((?:\s*\*)+\s*|\s+)([A-Za-z_]\w*)\s*=(?!=)([^;]*)$", re.S)


def top_level_comma(e):
    d = 0
    for c in e:
        if c in "([{":
            d += 1
        elif c in ")]}":
            d -= 1
        elif c == "," and d == 0:
            return True
    return False


def case_decl_transforms(text, report):
    """T20 (native2): MWCC accepts a jump to a case label past a declaration with an initializer at the switch body's
    own level (`case 4: int t0 = field_984; ... default:`); GCC refuses it even with -fpermissive (41 functions). Such a
    declaration becomes `T name; name = init;` (same value at the same point; a jump past an uninitialised trivial
    declaration is valid C++). A leading `const` is dropped (it needs an initialiser). Types that are not trivially
    default-constructible still fail to compile (loud)."""
    m = preproc_mask(mask(text))
    edits = []
    for sw in re.finditer(r"\bswitch\s*\(", m):
        po = sw.end() - 1
        d, j = 0, po
        while j < len(m):
            d += (m[j] == "(") - (m[j] == ")")
            if d == 0:
                break
            j += 1
        k = j + 1
        while k < len(m) and m[k].isspace():
            k += 1
        if k >= len(m) or m[k] != "{":
            continue
        end = match_brace(m, k)
        depth, last = 0, k + 1
        for i in range(k + 1, end):
            c = m[i]
            if c in "{(":
                depth += 1
            elif c in ")":
                depth -= 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    last = i + 1
            elif c == ";" and depth == 0:
                st = m[last:i]
                lm = CASE_LABEL.match(st)
                body = st[lm.end():] if lm else st
                off = last + (lm.end() if lm else 0)
                lead = len(body) - len(body.lstrip())
                body_s = body.strip()
                dm = CASE_DECL.match(body_s)
                if dm and not re.match(r"^(return|goto|break|continue|case|default|delete|throw|else|new|sizeof)\b", body_s) and \
                        not top_level_comma(dm.group(4)):
                    t = re.sub(r"^const\s+", "", dm.group(1)).strip() + dm.group(2).replace(" ", "").replace("\t", "").replace("\n", "")
                    a0 = off + lead
                    a1 = last + len(st)
                    init = text[a0:a1].split("=", 1)[1] if "=" in text[a0:a1] else None
                    if init is not None and text[a0:a1].count("=") >= 1:
                        orig = text[a0:a1]
                        name = dm.group(3)
                        eq = orig.index("=", orig.index(name, len(dm.group(1))) + len(name))
                        edits.append((a0, a1, "%s %s; /* decomp2host T20 */ %s =%s" % (t, name, name, orig[eq + 1:])))
                        report.append("T20 case declaration %s" % name)
                last = i + 1
    return apply(text, edits) if edits else text


def mmio_transforms(text, report):
    """T37 (boot2): `*(volatile T *)X`, X an address value (a parenthesised expression, a literal or a name -- not
    `&object`), is the decomp's direct hardware access: `b3host::mmio<T>(X)` (gp.h mmio_ref: RAM as memory, anything
    else through b3hw_read / b3hw_write, so the hardware model sees it). Also inside #define bodies (vibuf's HW(a))."""
    m = preproc_mask_keep_defines(mask(text))
    edits = []
    # gen1: a volatile typedef stands for its `volatile T` (`typedef volatile unsigned int vu32; #define R32(a)
    # (*(vu32 *)(a))`, func_002F34C0 / RenderBeforeVehicles 0x21C780's DMAC and toIPU registers: their R32(0x1000E010)
    # had stayed raw host loads and stores)
    vtd = {}
    for tm in re.finditer(r"\btypedef\s+(?:volatile\s+((?:const\s+)?[A-Za-z_][\w ]*?)|((?:const\s+)?[A-Za-z_][\w ]*?)\s+volatile)\s+([A-Za-z_]\w*)\s*;", m):
        vtd[tm.group(3)] = (tm.group(1) or tm.group(2)).strip()
    alts = r"volatile\s+((?:const\s+)?[A-Za-z_][\w ]*?)"
    if vtd:
        alts += r"|(%s)" % "|".join(re.escape(k) for k in sorted(vtd, key=len, reverse=True))
    for mm in re.finditer(r"\*\s*\(\s*(?:%s)\s*\*\s*\)\s*" % alts, m):
        t = (mm.group(1) or vtd.get(mm.group(2) or "", "")).strip()
        if t.startswith("const "):
            t = t[6:].strip()
        if "u_long128" in t or "struct" in t or "union" in t:
            continue
        j = mm.end()
        if j < len(m) and m[j] == "(":
            k = match_paren(m, j)
            if k is None:
                continue
            inner = m[j + 1:k].strip()
            if inner.startswith("&"):
                continue
            end = k + 1
            operand = text[j:end]
        else:
            om = re.match(r"(?:0[xX][0-9A-Fa-f]+|\d+|[A-Za-z_]\w*)[uUlL]*", m[j:])
            if not om:
                continue   # `&object` and anything else stays as it is
            end = j + om.end()
            operand = "(" + text[j:end] + ")"
        edits.append((mm.start(), end, "b3host::mmio<%s>%s" % (t, operand)))
        report.append("T37 hardware access %s" % text[mm.start():end].strip()[:60])
    return apply(text, edits) if edits else text


def pcast_offset_transforms(text, report):
    """gen1: `b3host::pcast<T>(E) + K` / `- K` (T9's `(T *)E` then a literal offset) -> `b3host::pcast_add<T>(E, K)`:
    for an integer E the guest address E + K * sizeof(T) (RenderBeforeVehicles 0x21C780's `(char *)s + 0x70002000`,
    the scratchpad: pointer arithmetic from E's host mapping had left the window, a wild pointer on the first race
    frame, mac1); a pointer E keeps host arithmetic (gp.h pcast_add). Not when the pcast is dereferenced or indexed
    first (`*pcast<T>(E) + K`), nor when K is an operand of * / % (precedence)."""
    m = mask(text)
    edits = []
    for mm in re.finditer(r"b3host::pcast<", m):
        s = mm.start()
        # its template argument and call
        d, j = 0, mm.end() - 1
        while j < len(m):
            if m[j] == "<":
                d += 1
            elif m[j] == ">":
                d -= 1
                if d == 0:
                    break
            j += 1
        if j >= len(m) or m[j + 1:j + 2] != "(":
            continue
        targ = text[mm.end():j]
        c = match_paren(m, j + 1)
        if c is None:
            continue
        km = re.match(r"\s*([+-])\s*(0[xX][0-9A-Fa-f]+|\d+)[uUlL]*\b(?!\s*[*/%\[(.])", m[c + 1:])
        if not km:
            continue
        q = s - 1
        while q >= 0 and m[q].isspace():
            q -= 1
        if q >= 0 and m[q] in "*&" and not (q >= 1 and m[q - 1] in "*&") and not re.search(r"[\w)\]]\s*[*&]$", m[:q + 1]):
            continue   # dereferenced / address-of first: (*p) + K is a value
        if any(a <= s < b for a, b, _ in edits):
            continue
        if int(km.group(2), 0) < 0x02000000:
            continue   # within main RAM's size: host arithmetic stays in the window it started in (no change)
        k = ("-" if km.group(1) == "-" else "") + km.group(2)
        edits.append((s, c + 1 + km.end(), "b3host::pcast_add<%s>(%s, %s)" % (targ, text[j + 2:c], k)))
    if edits:
        report.append("pcast offsets as guest arithmetic %d" % len(edits))
    return apply(text, edits) if edits else text


_T42_HEAD = re.compile(r"\b(class|struct)\s+([A-Za-z_]\w*)\s*:\s*(?:(?:public|private|protected)\s+)?(?:virtual\s+)?"
                       r"([A-Za-z_][\w:]*)\s*\{")
_T42_SKIP = re.compile(r"^\s*(?:public|private|protected)\s*:|^\s*(?:static|typedef|using|friend|virtual|enum|template)\b")


def derived_layout_transforms(text, report):
    """T42 (mac5, 2026-11-08): MWCC never places a derived class's members in its base's tail padding; the Itanium ABI
    (GCC / clang, x86-64 and arm64) does whenever the base is not POD for layout -- here every base with a b3host::gp
    member or a constructor-bearing math type. CB3RaceCarPhysics : CB3CollidingBody (0x208 bytes of data, 16-aligned:
    sizeof 0x210) put `pad_210` at 0x208 natively, so CB3CameraUtils::GetTrackMatrix read field_13F4 at 0x13EC and a
    takedown camera dereferenced 50.0f as a car [C gdb ptype/o; the Mac takedown crash of 2026-11-07/08, replayed].
    A zero-size member `alignas(alignof(Base)) unsigned char b3_t42_base_end[0];` before the derived class's first data
    member sits at round_up(dsize(Base), alignof(Base)) == sizeof(Base), MWCC's offset, so every member after it does too;
    it changes nothing where the base has no tail padding. (alignas on the member itself: clang refuses one weaker than
    the member type's own alignment.) Single non-template bases
    only; a class whose first data member cannot take alignas (a bit-field) is reported and left alone."""
    m = mask(text)
    edits = []
    for hm in _T42_HEAD.finditer(m):
        base = hm.group(3)
        if base in ("public", "private", "protected"):
            continue
        # the class body (depth 1 of this brace)
        i, depth, n = hm.end(), 1, len(m)
        j = i
        while j < n and depth:
            c = m[j]
            if c == "{": depth += 1
            elif c == "}": depth -= 1
            j += 1
        body_end = j - 1
        # walk depth-1 declarations (statements ending in ';' at depth 1; nested braces are methods / nested types)
        k, d, start = i, 0, i
        while k < body_end:
            c = m[k]
            if c == "{":
                d += 1
            elif c == "}":
                d -= 1
                if d == 0:
                    start = k + 1   # a nested definition or an inline method body ends: the next declaration starts
            elif c == ";" and d == 0:
                decl = m[start:k]
                stripped = decl.strip()
                if stripped:
                    lines = [ln for ln in stripped.split("\n") if ln.strip()]
                    first = lines[-1] if lines else ""
                    # access specifiers may lead the statement (`public: int x;`): look past them
                    st = re.sub(r"^(?:\s*(?:public|private|protected)\s*:)+", "", stripped).strip()
                    is_fn = "(" in st and not re.search(r"\(\s*\*", st)
                    if st and not is_fn and not _T42_SKIP.match(st) and not re.match(r"^(?:class|struct|union)\s+\w+\s*$", st):
                        if re.search(r":\s*\d+\s*$", st) or "alignas" in st:
                            report.append("T42: %s : %s first data member left alone (%s)" % (hm.group(2), base, st[:60]))
                        else:
                            off = start + (len(decl) - len(decl.lstrip())) + (len(stripped) - len(st))
                            indent = re.match(r"[ \t]*", text[text.rfind("\n", 0, off) + 1:off]).group()
                            edits.append((off, off, "alignas(alignof(%s)) unsigned char b3_t42_base_end[0];   // decomp2host T42: no tail-padding reuse\n%s" % (base, indent)))
                        break
                start = k + 1
            k += 1
    return apply(text, edits)


def transform_text(text, decomp, report, is_unit=False):
    # T6: MWCC pragmas (optimizer/scheduling controls for the matching build) mean nothing to the host compiler
    text = re.sub(r"^([ \t]*)#\s*pragma\b", r"\1// decomp2host T6: #pragma", text, flags=re.M)
    # T21 (native2): B3_PTR(T) (include/b3/B3Ptr.h: T* for MWCC, B3GuestPtr<T> on a host) -> b3host::gp<T>, the
    # adapter's 4-byte guest pointer, so the casts and calls below treat it as every other pointer member
    m0 = preproc_mask(mask(text))
    text = apply(text, [(mm.start(), mm.end(), "b3host::gp<%s>" % text[mm.start(1):mm.end(1)].strip())
                        for mm in re.finditer(r"\bB3_PTR\s*\(([^()]*)\)", m0)])
    # gen1 T33: MWCC's `__declspec(sdata)` (a small-data placement for the matching build) means nothing on the host;
    # dropped before T4 so the declaration is an ordinary extern (`extern __declspec(sdata) const float D_004E19C0;`)
    text = re.sub(r"\b__declspec\s*\(\s*sdata\s*\)\s*", "", text)
    text = mmio_transforms(text, report)   # boot2 T37: before T9 turns the cast into a pcast
    text = global_transforms(text, decomp, report)
    text = guest_libc_transforms(text, report)
    text = libc_decl_transforms(text)
    text = stack_frame_transforms(text)
    text = sqrtf_transforms(text)
    text = libm_transforms(text, decomp, report)
    text = goto_transforms(text)
    # boot2 T30: MWCC's inlined memcpy intrinsic (__memcpy: no call in retail, func_001364B0's 67-byte head copy) is
    # the host's memcpy over the guest bytes, as memcpy itself (HOST_LIBC)
    text = re.sub(r"\b__memcpy\s*\(", "memcpy(", text)
    text = ternary_float_transforms(text)   # boot2 T29
    text = virtual_transforms(text, report)
    text = ctor_vptr_transforms(text, decomp, report)   # boot1 T26
    text = fnptr_typedef_transforms(text, report)
    text = member_transforms(text)
    text = open_array_transforms(text, report)
    text = pointer_to_pointer_transforms(text)
    text = address_taken_pointer_locals(text, report)   # gen1 T32
    text = token_transforms(text)
    # T10: a unit's own `typedef unsigned int u_long128 __attribute__((mode(TI)));` (the EE's 128-bit integer) is the
    # prelude's u_long128 on the host (b3host/ps2types.h, a 16-byte struct)
    text = re.sub(r"^[ \t]*typedef\s+unsigned\s+int\s+u_long128\s+__attribute__\s*\(\(\s*mode\s*\(\s*TI\s*\)\s*\)\)\s*;",
                  "// decomp2host T10: u_long128 typedef (b3host/ps2types.h)", text, flags=re.M)
    # gen1 T10b: MWCC's own 128-bit integer type, `__int128` (`const __int128 *t1 = &_gau128CLAMP_1Regs[p0];`,
    # func_00217C40), is the same quadword: the prelude's u_long128 (arithmetic on it would stop the compile)
    text = re.sub(r"\b(?:(?:unsigned|signed)\s+)?__int128\b", "u_long128", text)
    # T11: `volatile` on a float (a matching device for MWCC's scheduling) means nothing for host locals; volatile
    # ps2f would have no operators
    text = re.sub(r"\bvolatile\s+ps2f\b|\bps2f\s+volatile\b", "ps2f /* decomp2host T11: volatile */", text)
    text = re.sub(r"\bvolatile\s+(union|struct)(\s*\{)", r"\1 /* decomp2host T11: volatile */\2", text)   # a punning local
    # gen1: a quadword read through a volatile pointer cast (`*(volatile const u_long128 *)&v.mData`, a scheduling
    # device for MWCC) is a plain read on the host: b3asm's operand loads and u_long128's copy take no volatile
    text = re.sub(r"\(\s*(const\s+)?volatile\s+(const\s+)?u_long128\s*\*\s*\)",
                  lambda mm: "(%su_long128 * /* decomp2host T11: volatile */)" % ("const " if (mm.group(1) or mm.group(2)) else ""), text)
    text = static_member_transforms(text, decomp, report, apply_header_statics=is_unit)
    text = f32_address_transforms(text)   # gen1 T34
    text = cast_transforms(text, report)
    text = case_decl_transforms(text, report)
    text = pcast_offset_transforms(text, report)   # gen1: `(T *)int + K` as guest-address arithmetic
    text = derived_layout_transforms(text, report)   # mac5 T42: MWCC's derived-class layout (no tail-padding reuse)
    return text


def mwerks_block_transforms(text, report):
    """T23 (native2): a decomp header's `#if defined(__MWERKS__)` block with no #else/#elif holds code for the matching
    build only because of its inline asm (CV3dRandomiser.h: RandomInterpolate / RandomiseXYZ, VU0 asm); the asm is
    translated here (P6), so the block is kept (7 functions called those inlines and had no definition)."""
    lines = text.split("\n")
    out = []
    i = 0
    while i < len(lines):
        l = lines[i]
        if re.match(r"^\s*#\s*if\s+defined\s*\(\s*__MWERKS__\s*\)\s*$", l) or re.match(r"^\s*#\s*ifdef\s+__MWERKS__\s*$", l):
            depth, j, has_else = 1, i + 1, False
            while j < len(lines) and depth:
                if re.match(r"^\s*#\s*if", lines[j]):
                    depth += 1
                elif re.match(r"^\s*#\s*endif", lines[j]):
                    depth -= 1
                elif depth == 1 and re.match(r"^\s*#\s*(else|elif)", lines[j]):
                    has_else = True
                j += 1
            if not has_else:
                out.append("#if 1 /* decomp2host T23: __MWERKS__-only block kept (asm translated) */")
                report.append("T23 __MWERKS__ block kept")
                i += 1
                continue
        out.append(l)
        i += 1
    return "\n".join(out)


def header_c_call_transforms(text, decomp, report):
    """T24 (native2): a decomp header's inline functions that call a C-linkage game function the header declares
    (GtFreeList.h: `void RemoveAllLinks() { RemoveAllLinks__11CGtFreeListFv(...); }`) call it through
    b3host::guest_fn (as a unit's own calls do, port8): the units' `#define Name (guest_fn...)` stubs come after the
    header, so the inline body kept the plain symbol, which nothing defines (undefined at link when 1,988 functions
    were generated together)."""
    m = preproc_mask(mask(text))
    decl_spans, names = [], {}
    for mm in re.finditer(r'\bextern\s*"[ C]"\s*[^;{}()]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*;', m):
        a = function_address(decomp, mm.group(1))
        decl_spans.append((mm.start(), mm.end()))
        if a is not None:
            names[mm.group(1)] = a
    if not names:
        return text
    edits = []
    for mm in re.finditer(r"(?<![\w.>:])(%s)\s*\(" % "|".join(re.escape(n) for n in sorted(names)), m):
        if any(a <= mm.start() < b for a, b in decl_spans):
            continue
        n = mm.group(1)
        edits.append((mm.start(1), mm.end(1), "b3host::guest_fn<decltype(%s), 0x%08Xu>::call" % (n, names[n])))
    if edits:
        report.append("T24 header C calls %d" % len(edits))
    return apply(text, edits) if edits else text


GTMATH_INSTANCES = "gen1/gtmath_instances.cpp"   # gen1: the synthetic unit's name (no such decomp file)


def gtmath_instance(name, addr):
    """gen1: a GtMathPs2 header inline the compiler emitted out of line (MWCC: the B3 build's operator= / copy
    constructor of a value type, a const member such as CGtV3d::GetSquaredLength), as an extern "C" definition over
    the port's shadow header (the same inline, PS2-faithful), or None. Their decomp files hold no definition (the
    header's inline is the source: Gt2dFrameHierarchy.cpp's CGtV4d / CGtV2d operator=, B3HeadingComponent_0031A570.cpp's
    CGtMatrix2x3 pair); the selection used to fail with "not found by marker"."""
    def qtype(s):
        mq = re.match(r"^Q29GtMathPs2(\d+)", s)
        if not mq or len(s) < mq.end() + int(mq.group(1)):
            return None, None
        return s[mq.end():mq.end() + int(mq.group(1))], s[mq.end() + int(mq.group(1)):]
    mm = re.match(r"^__(as|ct)__(.*)$", name)
    if mm:
        ty, rest = qtype(mm.group(2))
        if ty and rest == "FRCQ29GtMathPs2%d%s" % (len(ty), ty):
            if mm.group(1) == "as":
                return ('// 0x%08X\nextern "C" GtMathPs2::%s *b3gtm_%08X(GtMathPs2::%s *self, const GtMathPs2::%s &o)\n'
                        "{\n    *self = o;\n    return self;\n}\n" % (addr, ty, addr, ty, ty))
            return ('// 0x%08X\nextern "C" GtMathPs2::%s *b3gtm_%08X(GtMathPs2::%s *self, const GtMathPs2::%s &o)\n'
                    "{\n    ::new (static_cast<void *>(self)) GtMathPs2::%s(o);\n    return self;\n}\n" % (addr, ty, addr, ty, ty, ty))
    mm = re.match(r"^([A-Za-z]\w*?)__(Q29GtMathPs2\d+\w+)CFv$", name)
    if mm:
        ty, rest = qtype(mm.group(2))
        if ty and rest == "":
            # a struct result through the hidden pointer in a0, `this` in a1 [C docs/abi-mwcc-ee.md §4]
            return ('// 0x%08X\nextern "C" void b3gtm_%08X(decltype(std::declval<const GtMathPs2::%s &>().%s()) *ret, '
                    "const GtMathPs2::%s *self)\n{\n    static_assert(std::is_class_v<std::remove_pointer_t<decltype(ret)>>);\n"
                    "    *ret = self->%s();\n}\n" % (addr, addr, ty, mm.group(1), ty, mm.group(1)))
    return None


def gtmath_defined(decomp, a, addr, name):
    """gen1: the function's own decomp file defines it (its `// 0xADDR` marker), so it generates from there as before
    (CGtQuat's operator= in its unit); only header inlines without a definition go to the synthetic unit."""
    f = decomp.matched_file(addr) or decomp.nonmatching_file(name)
    p = os.path.join(a.decomp, f) if f else ""
    if not (p and os.path.exists(p)):
        return False
    src = open(p, errors="replace").read()
    if re.search(r"^\s*namespace\s+GtMathPs2\s*\{", src, re.M) and not re.search(r'#\s*include\s*"b3/GtMathPs2\.h"', src):
        return False   # gen1: an out-of-line copy over the file's own GtMathPs2 view (GetSquaredLength 0x2ED2C0): the shadow header's instance
    return re.search(r"//\s*0x%08X\s*\n" % addr, src, re.I) is not None


def src_path(root, f):
    """A unit's source: the decomp's (relative to its root) or, for a port track (audio1), this repo's."""
    if any(f.startswith(d + "/") for d in PORT_TRACKS.values()):
        return os.path.join(PORT_ROOT, f)
    return os.path.join(root, f)


def unit_name(f):
    """Generated unit name: the source's base name; a port track's units get the track as a prefix (audio1:
    src/libs/rwa/rwaintf.cpp -> rwa_rwaintf), so they never meet a decomp unit of the same name."""
    base = os.path.splitext(os.path.basename(f))[0]
    for t, d in PORT_TRACKS.items():
        if f.startswith(d + "/"):
            return t + "_" + base
    return base


# mac5 (T45): CV3dRandomiser.h's random state. Retail has one CGtV4d per source file (B3EffectsManager D_01D9D190,
# B3ExplosionManager D_01EA4BA0 [C asm: vmr32 + sqc2 back after every draw]), which every function of that file advances;
# the decomp splits the files into units, so the header's `static` became one private host copy per unit and the
# effects drafts' draws (sparks, debris) diverged from retail's. The state is the file's guest global instead: each
# user unit defines B3_RANDOMISER_STATE (RANDOMISER_STATES below, by the function's address).
RANDOMISER_HEADER = "b3/_unclassified/CV3dRandomiser.h"
RANDOMISER_STATES = [(0x00254000, 0x00262000, 0x01D9D190),   # B3EffectsManager (units 0x254C90 / 0x255EE0 / 0x257E10)
                     (0x00399000, 0x0039C000, 0x01EA4BA0)]   # B3ExplosionManager (units 0x399EC0 / 0x39AD90)


def randomiser_state_transform(text, report):
    pat = re.compile(r"^static GtMathPs2::CGtV4d gEffectsRandomiserScales\([^;]*\);[^\n]*$", re.M)
    if not pat.search(text):
        die("T45: %s no longer defines gEffectsRandomiserScales as expected" % RANDOMISER_HEADER)
    text = pat.sub("#ifndef B3_RANDOMISER_STATE\n#error decomp2host T45: the unit must define B3_RANDOMISER_STATE\n#endif\n"
                   "#define gEffectsRandomiserScales (b3host::guest_global<GtMathPs2::CGtV4d>(B3_RANDOMISER_STATE))"
                   "   // decomp2host T45: the source file's guest state", text)
    # mac5: the randomiser's inline members read the unit's state, so each unit needs its own copy: in an unnamed namespace
    # (internal linkage). As external inline functions the linker kept one out-of-line copy for every unit -- the effects
    # manager's state (0x1D9D190) in the explosion code too (func_00399EC0's draws differed from retail per call)
    if text.count("\nstruct CV3dRandomiser {") != 1 or not text.rstrip().endswith("#endif"):
        die("T45: %s no longer has the expected struct / include guard" % RANDOMISER_HEADER)
    text = text.replace("\nstruct CV3dRandomiser {",
                        "\nnamespace {   // decomp2host T45: one copy per unit (its own state)\nstruct CV3dRandomiser {", 1)
    end = text.rstrip().rfind("#endif")
    text = text[:end] + "}  // namespace (decomp2host T45)\n" + text[end:]
    report.append("T45 randomiser state: the file's guest global")
    return text


def randomiser_state_for(addresses):
    """T45: the B3_RANDOMISER_STATE of a unit that includes CV3dRandomiser.h, from its functions' addresses."""
    found = {st for a in addresses for lo, hi, st in RANDOMISER_STATES if lo <= a < hi}
    if len(found) != 1:
        die("T45: CV3dRandomiser.h user %s outside the known files (RANDOMISER_STATES)" %
            ", ".join("%08X" % a for a in addresses))
    return found.pop()


def transform_header(rel, decomp, out, done, report):
    if rel in done or rel in SHADOW_HEADERS:
        return
    if rel.startswith("b3hw/"):   # the host hardware hooks' header: included as it is (include/b3hw)
        return
    done.add(rel)
    src = os.path.join(decomp.root, "include", rel)
    if not os.path.exists(src):
        # audio1: a port track's own headers (src/libs/<lib>/include/<rel>)
        for d in PORT_TRACKS.values():
            alt = os.path.join(PORT_ROOT, d, "include", rel)
            if os.path.exists(alt):
                src = alt
                break
    if not os.path.exists(src):
        die("include %s not found in the decomp's include/" % rel)
    text = open(src).read()
    for inc in re.findall(r'#\s*include\s+"([^"]+)"', text):
        transform_header(inc, decomp, out, done, report)
    HEADER_ASM_MACROS.update(asm_macros(text))
    text = mwerks_block_transforms(text, report)
    text = asm_transforms(text, "include/" + rel, report)
    text = transform_text(text, decomp, report)
    if src.startswith(os.path.join(decomp.root, "include")):
        text = ee_division_transforms(text, report)   # gen1 T39: a decomp header's inline code (not a port track's)
    text = header_c_call_transforms(text, decomp, report)
    if rel == RANDOMISER_HEADER:
        text = randomiser_state_transform(text, report)   # mac5 T45
    HEADER_TEXT.append(text)
    if not src.startswith(os.path.join(decomp.root, "include")):
        # audio1: scalar typedefs of a port track's headers (RwUInt32, RwReal after T1) for the wrappers
        for mm in re.finditer(r"^\s*typedef\s+((?:const\s+)?(?:unsigned\s+|signed\s+)?(?:char|short|int|ps2f|[A-Z]\w*))\s+([A-Za-z_]\w*)\s*;",
                              text, re.M):
            PORT_TYPEDEFS[mm.group(2)] = mm.group(1)
    dst = os.path.join(out, "include", rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, "w").write("// GENERATED by tools/decomp2host/decomp2host.py from the decomp's include/%s. Do not edit.\n"
                         "#include \"b3gen/prelude.h\"\n" % rel + text)


PRELUDE = """// GENERATED by tools/decomp2host/decomp2host.py. Do not edit.
// Included first by every generated unit and header (docs/decomp-on-host.md).
#ifndef B3GEN_PRELUDE_H
#define B3GEN_PRELUDE_H
#include "b3host/call.h"
#include "b3host/ps2f.h"
#include "b3host/ps2types.h"
#include "b3host/eeasm.h"
using b3host::ps2f;
// The decomp's own portable pointer layer (include/b3/B3Ptr.h, its host branch): the two conversions it leaves to "the
// port layer" (native2; B3_PTR(T) members themselves become b3host::gp<T>, T21).
inline void* B3GuestToHost(unsigned int guest) { return b3host::detail::guest_to_host_checked(guest, 1); }
inline unsigned int B3HostToGuest(const void* host) { return b3host::guest_address(host); }
#endif
"""


# ---------------------------------------------------------------- wrappers
def split_params(s):
    out, d, cur = [], 0, ""
    for c in s:
        if c in "(<":
            d += 1
        elif c in ")>":
            d -= 1
        if c == "," and d == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
    if cur.strip():
        out.append(cur.strip())
    return [p for p in out if p != "void"]


HEADER_TEXT = []   # transformed decomp headers, for class bodies a unit only includes
HEADER_ASM_MACROS = set()   # asm macros defined in decomp headers a unit includes


GTMATH_VALUE_TYPES = {"CGtV2d", "CGtV3d", "CGtV4d", "CGtMatrix2x3", "CGtMatrix3x3", "CGtMatrix3x4", "CGtV3dPlus",
                      "CGtPlane", "CGtSphere", "CGtQuat", "CGtQuatPos", "XAxisType", "YAxisType", "ZAxisType",
                      "WAxisType"}


def gtmath_type(t):
    t = re.sub(r"\bconst\b", "", t).strip()
    t = re.sub(r"^GtMathPs2\s*::\s*", "", t)
    return t if t in GTMATH_VALUE_TYPES else None


PORT_TYPEDEFS = {}   # audio1: scalar typedefs declared by port-track headers (name -> type), resolved in wrappers only


def port_scalar(t):
    """audio1: a parameter / result type named by a port header's scalar typedef -> the scalar (RwBool -> RwInt32 ->
    int, RwReal -> ps2f); anything else unchanged. Limited to port tracks, so decomp units generate as before."""
    c = t.startswith("const ")
    b = t[6:].strip() if c else t.strip()
    seen = 0
    while b in PORT_TYPEDEFS and seen < 8:
        b, seen = PORT_TYPEDEFS[b], seen + 1
    return ("const " if c else "") + b if seen else t


def wrapper_for(head, unit_text, addr, ns=None):
    """Guest-ABI wrapper for the kept definition `head` (text before its body), after T1-T4."""
    h = re.sub(r"\s+", " ", head)
    is_c = 'extern "C"' in unit_text[:0] or h.startswith('extern "C"')
    h = re.sub(r"^extern\s*\"[^\"]*\"\s*", "", h)
    mm = re.match(r"^(.*?)\s*\b((?:[A-Za-z_]\w*::)*)([A-Za-z_]\w*)\s*\((.*)\)\s*(const)?\s*$", h)
    if not mm:
        die("cannot parse the definition head: " + h)
    ret, qual, fname, params, const = mm.group(1).strip(), mm.group(2), mm.group(3), mm.group(4), mm.group(5)
    ret = re.sub(r"\b(static|inline|virtual|__inline__|__inline)\b\s*", "", ret).strip()
    ret = port_scalar(ret)
    cls = qual[:-2] if qual else None
    static = False
    cls_body = ""
    if cls:
        simple = cls.split("::")[-1]
        # the class body: in the unit, or in a decomp header it includes (a static member declared there has no
        # `this`: LaneIsDriveable__9CB3AILaneFUcUsUs takes p0 in a0, retail 0x148CA0 `andi v1, a0, 0xFF`)
        for src in [unit_text] + HEADER_TEXT:
            um = re.search(r"\b(class|struct)\s+%s\b[^;{]*\{" % re.escape(simple), src)
            if um:
                body = src[um.end():match_brace(mask(src), um.end() - 1)]
                cls_body = body
                static = re.search(r"\bstatic\b[^;{}]*\b%s\s*\(" % re.escape(fname), mask(body)) is not None
                # gen1: an overload set mixing static and non-static members (CB3AILane::LaneIsDriveable: a static
                # (UcUsUs) beside a member (UsUs), 0x148D60): the declaration with this definition's parameter count
                decls_ = [(bool(re.search(r"\bstatic\b", dm_.group(1))), len(split_params(dm_.group(2))))
                          for dm_ in re.finditer(r"(?:^|[;{}])([^;{}]*?)\b%s\s*\(((?:[^()]|\([^()]*\))*)\)" % re.escape(fname), mask(body))]
                if len({s_ for s_, _ in decls_}) > 1:
                    same = {s_ for s_, n_ in decls_ if n_ == len(split_params(params))}
                    if len(same) == 1:
                        static = same.pop()
                break

    def scoped(t):
        """boot2: a type the definition names unqualified but its class declares (`enum EClutID {}` inside
        CB3GraphicsManager: Copy8BitTextureAsPSMT8H's parameter) is qualified for the namespace-scope wrapper."""
        bare = t.replace("const ", "").strip()
        if cls and "::" not in bare and re.search(r"\b(enum|struct|class|union|typedef)\s+(?:[^;{}]*\s)?%s\b" % re.escape(bare), mask(cls_body)):
            return cls + "::" + bare
        if cls and "::" not in bare and re.fullmatch(r"[A-Za-z_]\w*", bare) and bare not in (
                "void", "char", "short", "int", "long", "bool", "float", "double", "ps2f", "unsigned", "signed", "u_long128"):
            # gen1: a nested type of a base class (`const CGt2dObjectSpace &` in CGt2dObjectTable::_Render: CGt2dObject's)
            todo, seen = [cls.split("::")[-1]], set()
            while todo:
                c = todo.pop()
                if c in seen:
                    continue
                seen.add(c)
                for src in [unit_text] + HEADER_TEXT:
                    bases = _class_base_index(mask(src)).get(c)   # gen1 (speed): per-text index, same first match
                    if bases is None:
                        continue
                    for b in re.findall(r"(?:public|protected|private)?\s*([A-Za-z_][\w:]*)\s*(?:,|$)", bases.strip()):
                        b = b.split("::")[-1]
                        for src2 in [unit_text] + HEADER_TEXT:
                            j = _class_open_index(src2).get(b)
                            if j is not None and nested_decl(src2[j:match_brace(mask(src2), j - 1)], bare):
                                return b + "::" + bare
                        todo.append(b)
                    break
        return bare

    def nested_decl(body, bare):
        """gen1: `bare` is declared as a member type of the class body itself (depth 0: `struct X {`, `struct X;`,
        `enum X {`, `typedef ... X;`), not only named in an elaborated parameter type (`f(struct X *p)` declares X at
        namespace scope: CB3CrashActionDirector's CB3CollidingPair)"""
        b = mask(body)
        b = re.sub(r"\([^()]*\)", "()", re.sub(r"\([^()]*\)", "()", b))
        flat, d = [], 0
        for c in b:
            if c == "}":
                d -= 1
            if d == 0:
                flat.append(c)
            if c == "{":
                d += 1
        flat = "".join(flat)
        e = re.escape(bare)
        return re.search(r"\b(?:struct|class|union|enum)\s+%s\s*(?:\{|:(?!:)|;)|\btypedef\b[^;]*\b%s\s*;|\busing\s+%s\s*=" % (e, e, e),
                         flat) is not None

    def scoped_keep(t):
        """gen1: scoped() for a pointer / reference base, keeping its const (`const CLighting` declared inside
        CB3EffectsManager -> `const CB3EffectsManager::CLighting`)."""
        t = t.strip()
        c = re.match(r"^const\s+(.*)$", t)
        core = c.group(1) if c else t
        if not re.fullmatch(r"[A-Za-z_]\w*", core):
            return t
        if cls and nested_decl(cls_body, core):
            q = cls + "::" + core
        else:
            q = scoped(core) if not re.search(r"\b(enum|struct|class|union|typedef)\s+(?:[^;{}]*\s)?%s\b" % re.escape(core),
                                                mask(cls_body)) else core
        return ("const " + q) if c else q
    def gtq(name):
        """gen1: the qualified name of a GtMathPs2 value type for the wrapper: ::GtMathPs2::X, except in a unit that
        includes no GtMath header and declares the type itself, in its own `namespace GtMathPs2` (GetSquaredLength's
        out-of-line copy 0x2ED2C0) or plainly (`struct CGtSphere { u_long128 mData; };`, func_00301B00)"""
        if not re.search(r'#\s*include\s+"b3/GtMath', unit_text):
            um_ = mask(unit_text)
            if re.search(r"\bnamespace\s+GtMathPs2\s*\{", um_):
                return "GtMathPs2::" + name
            if re.search(r"\b(?:struct|class)\s+%s\b[^;]*\{" % re.escape(name), um_):
                return name
        return "::GtMathPs2::" + name
    # The wrapper's own signature uses only ABI-neutral types (b3host::gp<void>, float, integers), so the generated
    # table header needs no decomp class declarations; the casts to the decomp types happen inside.
    wparams, args, asserts = [], [], []
    rtypes = []   # native2: the parameter types as declared (code_registration)
    if cls and not static:
        wparams.append("b3host::gp<void> self")
    for k, p in enumerate(split_params(params)):
        if p == "...":
            # gen1: a C varargs definition (the decomp's empty debug-print stand-ins `void func_00447170(int, ...)
            # {}`): the wrapper passes the fixed parameters; one that reads its variable arguments (va_start) would
            # need the guest's, so it stops the generation
            if re.search(r"\b(va_start|__builtin_va_start)\b", mask(unit_text)):
                die("%08X: varargs definition reading its variable arguments (va_start) not supported" % addr)
            rtypes.append("...")
            continue
        pm = re.match(r"^(.*?)([A-Za-z_]\w*)?$", p)
        ptype = pm.group(1).strip() if pm.group(2) else p
        if pm.group(2) and re.fullmatch(r"(?:const\s+)?(?:unsigned|signed)", ptype) and \
                pm.group(2) not in ("int", "char", "short", "long"):
            ptype += " int"   # gen1: `unsigned n` is a named `unsigned int` (RpcAlloc(unsigned n))
        elif not ptype or ptype in ("const", "unsigned", "signed", "struct", "class") or ptype.endswith("::"):
            ptype = p   # an unnamed parameter: the last word is part of the type (`UploadCodeAndStaticData(float)`)
        pname = "a%d" % k
        rm = re.match(r"^(.*?)([\s*&]*)$", ptype)
        rtypes.append(scoped_keep(rm.group(1)) + rm.group(2))   # gen1: the registration's types as the wrapper's
        ptype = port_scalar(ptype)
        if ptype.endswith("&"):
            base = scoped_keep(ptype[:-1].strip())
            wparams.append("b3host::gp<void> %s" % pname)
            args.append("*%s.cast<%s>()" % (pname, base))
        elif ptype.endswith("*"):
            wparams.append("b3host::gp<void> %s" % pname)
            args.append("%s.cast<%s>()" % (pname, scoped_keep(ptype[:-1].strip())))
        elif re.match(r"^b3host::gfn<.*>$", ptype):
            # gen1 (T36): a function-pointer parameter: the caller's guest code address in one GPR
            wparams.append("unsigned int %s" % pname)
            args.append("%s::from_raw(%s)" % (ptype, pname))
        elif re.fullmatch(r"(?:const )?[A-Za-z_]\w*", ptype) and "::" in scoped_keep(ptype.replace("const ", "")) and \
                re.search(r"\btypedef\s+b3host::gfn<[^;]*>\s*%s\b" % re.escape(ptype.replace("const ", "")),
                          mask("\n".join([unit_text] + HEADER_TEXT))):
            # gen1: a function-pointer typedef nested in a class (T14's gfn; CGt2dObject::UnkFn_RenderCallback from
            # B3_FNPTR) by value: the caller's guest code address, the type qualified. A namespace-scope typedef keeps
            # by_value (its lone_float specialisation: RwaObjInterfaceAttach 0x2940B0, unchanged)
            wparams.append("unsigned int %s" % pname)
            args.append("%s::from_raw(%s)" % (scoped_keep(ptype.replace("const ", "")), pname))
        elif ptype in ("ps2f", "const ps2f"):
            wparams.append("float %s" % pname)
            args.append("ps2f(%s)" % pname)
        elif re.match(r"^(const )?(unsigned |signed )?(char|short|int|bool|b3host::ps2_long|b3host::ps2_ulong)$", ptype):
            wparams.append("%s %s" % (ptype, pname))
            args.append(pname)
        elif gtmath_type(ptype) == "CGtV2d":
            # 8 bytes, trivially copyable: one GPR holding its memory image [C docs/abi-mwcc-ee.md §2]
            wparams.append("%s %s" % (gtq("CGtV2d"), pname))
            args.append(pname)
        elif gtmath_type(ptype):
            # the other GtMathPs2 value types are 16 bytes or more: the address of the caller's copy [C §2]
            wparams.append("b3host::gp<void> %s" % pname)
            args.append("*%s.cast<%s>()" % (pname, gtq(gtmath_type(ptype))))
        elif re.match(r"^(const )?u_long128$", ptype):
            wparams.append("u_long128 %s" % pname)   # one 128-bit GPR [C docs/abi-mwcc-ee.md §2]
            args.append(pname)
        elif re.match(r"^(const )?(?:\w+::)*(?:E|UnkEnum_)\w+$", ptype):   # native2: nested (Class::EName) enums too
            wparams.append("int %s" % pname)   # an enum: int-sized, sign-extended [C docs/abi-mwcc-ee.md §2]
            args.append("static_cast<%s>(%s)" % (scoped(ptype), pname))
        elif re.search(r"\benum\s+%s\b" % re.escape(ptype.replace("const ", "").strip().split("::")[-1]),
                       "\n".join([unit_text] + HEADER_TEXT)):
            # native2: an enum whose name does not start with E (SFIODeviceE): int-sized, sign-extended
            wparams.append("int %s" % pname)
            args.append("static_cast<%s>(%s)" % (scoped(ptype), pname))
        elif lone_float_decision(ptype.replace("const ", "").strip(), [unit_text] + HEADER_TEXT) == 1:
            # native2: a 4-byte struct whose only member is a float: in an FPR [C docs/abi-mwcc-ee.md §2]
            wparams.append("float %s" % pname)
            args.append("std::bit_cast<%s>(%s)" % (ptype.replace("const ", ""), pname))
        else:
            # a struct by value, integer class either way [C docs/abi-mwcc-ee.md §2]: > 8 bytes as the address of the
            # caller's copy, <= 8 bytes as its memory image in the GPR (port7: HighlightKey's 8-byte cell); the
            # 4-byte case needs the lone-float decision, so b3host::by_value refuses it at compile time
            wparams.append("std::uint64_t %s" % pname)
            t = ptype.replace("const ", "")
            args.append("b3host::by_value<%s>(%s)" % (t, pname))
            BYVAL_TYPES.add(t.strip())
    # mac5: in a unit with its own namespace (T7) the class is the unit's own definition; qualified, so a global class of
    # the same name from a decomp header (CollideWithWorld's CB3CollidingBody) does not make the wrapper's use ambiguous
    cls_q = ("%s::%s" % (ns, cls)) if (ns and cls and not cls.startswith("::")) else cls
    callee = ("self.cast<%s%s>()->%s" % ("const " if const else "", cls_q, fname)) if (cls and not static) else ("%s%s" % (qual, fname))
    call = "%s(%s)" % (callee, ", ".join(args))
    is_ctor = bool(cls) and ret == "" and fname == cls.split("::")[-1]
    if is_ctor:
        # native2: a constructor, constructed in place in guest memory; MWCC returns `this` in v0 [C
        # docs/abi-mwcc-ee.md §6: __ct__Q29GtMathPs26CGtV4dF1 @ 0x130744]
        wret, body = "b3host::gp<void>", "::new (static_cast<void*>(self.cast<%s>().get())) %s(%s); return self;" % (
            cls_q, cls_q, ", ".join(args))
    elif ret == "void":
        wret, body = "void", "%s;" % call
    elif ret == "ps2f":
        wret, body = "float", "return static_cast<float>(%s);" % call
    elif ret.endswith("*"):
        wret, body = "b3host::gp<void>", "return b3host::gp<void>(%s);" % call
    elif re.match(r"^E\w+$", ret):
        wret, body = "int", "return static_cast<int>(%s);" % call
    elif gtmath_type(ret):
        # a struct result goes through a hidden pointer in a0, ahead of `this`; v0 is not written [C docs/abi-mwcc-ee.md
        # §4]: the wrapper takes that pointer as its first parameter and returns void (the same registers)
        wparams.insert(0, "b3host::gp<void> ret")
        wret, body = "void", "*ret.cast<%s>() = %s;" % (gtq(gtmath_type(ret)), call)
    elif re.match(r"^(const )?(unsigned |signed )?(char|short|int|bool|b3host::ps2_long|b3host::ps2_ulong|u_long128)$", ret):
        wret, body = ret, "return %s;" % call
    elif re.match(r"^(const )?(unsigned )?long long$", ret):   # MWCC's long long: 64 bits, as its long (native2)
        wret = "b3host::ps2_ulong" if "unsigned" in ret else "b3host::ps2_long"
        body = "return static_cast<%s>(%s);" % (wret, call)
    elif re.match(r"^(const )?UnkEnum_\w+$", ret):
        wret, body = "int", "return static_cast<int>(%s);" % call
    elif re.match(r"^(const )?(struct |class )?[A-Za-z_][\w:]*$", ret) and ret not in ("asm",) and not ret.startswith("asm "):
        # native2: any other struct/class result, through the hidden pointer in a0 as above [C docs/abi-mwcc-ee.md §4:
        # every struct result whatever its size]
        t = re.sub(r"^(const )?(struct |class )?", "", ret)
        wparams.insert(0, "b3host::gp<void> ret")
        wret, body = "void", "*ret.cast<%s>() = %s;" % (t, call)
    else:
        die("%08X: result type %s not supported by the wrapper generator" % (addr, ret))
    body = " ".join(asserts + [body])
    wname = "b3gen_%08X" % addr
    reg = ""
    if (not cls or static) and not is_ctor:
        # native2: a kept free / static definition handed to guest code as a callback maps to its own guest address
        reg = "\n" + code_registration("%s%s" % (qual, fname), rtypes, addr, 0)
    return wname, "%s %s(%s) { %s }%s" % (wret, wname, ", ".join(wparams), body, reg)


# ---------------------------------------------------------------- calls to other game functions (port7)
PORT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# Undefined symbols that are the host's own business (runtime, adapter, C library used on host pointers).
HOST_SYM = re.compile(r"^(_GLOBAL_OFFSET_TABLE_|__stack_chk_fail|_Unwind_\w+|__cxa_\w+|__gxx_\w+|__dso_handle|"
                      r"_ZN?K?S[tabiod]\w*|_ZNSt\w*|_ZSt\w*|_ZN6b3host\w*|_ZN5b3asm\w*|_ZN4b3fp\w*|_ZN\d*b3libm\w*|_Znw\w*|_Zna\w*|"
                      r"_Zdl\w*|_Zda\w*|__tls_get_addr|_ZTH\w*|_ZTW\w*|__assert_fail|abort|"
                      r"%s)$" % "|".join(sorted(HOST_LIBC)))
# C math names: the unit's own `extern "C" float sinf(float);` is retail's libm (a guest call: the native libm override
# when enabled); the same name undefined without such a declaration comes from host code (b3host headers).
LIBM_NAMES = {"sinf", "cosf", "tanf", "atanf", "atan2f", "acosf", "asinf", "logf", "log10f", "powf", "expf", "floorf",
              "ceilf", "fabsf", "fmodf", "sqrtf", "sqrt", "fabs", "floor", "ceil", "truncf", "lroundf", "ldexpf",
              "frexpf", "modff", "scalbnf", "copysignf"}

MW_BUILTIN = {"void": "v", "bool": "b", "char": "c", "signed char": "Sc", "unsigned char": "Uc", "short": "s",
              "unsigned short": "Us", "int": "i", "unsigned int": "Ui", "long": "l", "unsigned long": "Ul",
              "long long": "x", "unsigned long long": "Ux", "float": "f", "b3host::ps2f": "f", "double": "d",
              "wchar_t": "w", "u_long128": "1", "b3host::ps2_long": "l", "b3host::ps2_ulong": "Ul"}


def split_macro_args(s):
    """boot2: a macro call's arguments are expressions: only ( [ { nest (`MK(q0, 0x50000006UL << 32, 0x10000006)` in
    AccessTexture: `<<` is no template bracket)."""
    # gen1: brackets and commas inside string / char literals or comments do not count (`OPN(f, "vsuby.x %0, %1, %2y")`
    # in func_00429BE0 had read as 4 arguments)
    out, d, cur = [], 0, ""
    for c, mc in zip(s, mask(s)):
        if mc in "([{":
            d += 1
        elif mc in ")]}":
            d -= 1
        if mc == "," and d == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


def split_top(s, sep=","):
    out, d, cur = [], 0, ""
    for c in s:
        if c in "(<[":
            d += 1
        elif c in ")>]":
            d -= 1
        if c == sep and d == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


def strip_ns(q):
    return re.sub(r"\bb3gen_\w+::", "", q)


def mw_class(q):
    parts = [p for p in strip_ns(q).split("::") if p]
    if any(not re.match(r"^[A-Za-z_]\w*$", p) for p in parts):
        return None
    if len(parts) == 1:
        return "%d%s" % (len(parts[0]), parts[0])
    return "Q%d%s" % (len(parts), "".join("%d%s" % (len(p), p) for p in parts))


def mw_type(t):
    """MWCC (CodeWarrior) mangling of one parameter type, from GCC's demangled spelling (decomp symbols, e.g.
    GetFwdFrac__9CB3AILaneCFUsQ29GtMathPs26CGtV3d: no repeat compression, const as C, nested names as Q<n>)."""
    t = t.strip()
    fm = re.match(r"^(.*)\(\*\)\((.*)\)$", t)   # pointer to function: PF<params>_<ret>
    if fm:
        ps = [x for x in split_top(fm.group(2)) if x != "void"]
        inner = "".join(mw_type(x) or "?" for x in ps) or "v"
        r = mw_type(fm.group(1))
        return None if r is None or "?" in inner else "PF%s_%s" % (inner, r)
    if t.endswith("&"):
        r = mw_type(t[:-1])
        return None if r is None else "R" + r
    if t.endswith("*"):
        r = mw_type(t[:-1])
        return None if r is None else "P" + r
    if t.endswith(" const"):
        r = mw_type(t[:-6])
        return None if r is None else "C" + r
    if t.endswith(" volatile"):
        r = mw_type(t[:-9])
        return None if r is None else "V" + r
    t = strip_ns(t)
    if t in MW_BUILTIN:
        return MW_BUILTIN[t]
    return mw_class(t)


def mw_mangle(qual, name, params, const):
    ps = [x for x in params if x != "void"]
    pm = "".join(mw_type(x) or "?" for x in ps) or "v"
    if "?" in pm:
        return None
    if qual:
        c = mw_class(qual)
        if c is None:
            return None
        if name == qual.split("::")[-1]:
            return "__ct__%sF%s" % (c, pm)
        if name.replace(" ", "") == "operator=":
            name = "__as"   # gen1: MWCC's name for operator=
        return "%s__%s%sF%s" % (name, c, "C" if const else "", pm)
    return "%s__F%s" % (name, pm)


def function_address(decomp, name):
    if name.startswith(GUEST_LIBC_PREFIX):   # T22
        name = name[len(GUEST_LIBC_PREFIX):]
    mf = re.match(r"^func_([0-9A-Fa-f]{8})$", name)
    if mf:
        return int(mf.group(1), 16)
    # boot2: splat's alternate entry labels inside a function (`alabel D_0022F890` in func_0022F800, a thread entry the
    # decomp declares `extern "C" void D_0022F890(void *)`) are code addresses too
    md = re.match(r"^D_([0-9A-Fa-f]{8})$", name)
    if md:
        a = int(md.group(1), 16)
        if any(f <= a < f + sz for f, (_, sz) in decomp.funcs.items()):   # inside a function's code
            return a
    if name in decomp.by_name:
        return decomp.by_name[name]
    if name in decomp.symbols and decomp.symbols[name] in decomp.funcs:
        return decomp.symbols[name]
    return None


# gen1: no -w, so the diagnostics -fpermissive downgrades stay visible to compile_for_stubs (pointer <-> integer
# conversions: refused or made explicit); the objects are the same
# gen1 (mac1): the GCC-only -fno-aggressive-loop-optimizations is clang's -Wno-address-of-temporary under clang (the
# Makefile's CXX_IS_CLANG / GEN_CC_FLAGS rule: by `$DECOMP2HOST_CXX --version`); under clang every unit had failed its
# check compile with "unknown argument" (4949 of 4950). GCC's flags are unchanged (the same cache keys).
def _host_cxx_is_clang():
    try:
        r = subprocess.run(os.environ.get("DECOMP2HOST_CXX", "g++").split() + ["--version"], capture_output=True,
                           text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return False
    return "clang" in (r.stdout.split("\n")[0] if r.stdout else "")


HOST_COMPILE_FLAGS = ["-std=c++20", "-c", "-O0", "-fpermissive", "-fno-strict-aliasing",
                      "-Wno-address-of-temporary" if _host_cxx_is_clang() else "-fno-aggressive-loop-optimizations",
                      "-fstrict-flex-arrays=0", "-fno-access-control", "-ffp-contract=off"]


def host_compile(path, extra=()):
    obj = path + ".o"
    # boot2: DECOMP2HOST_CXX (e.g. ../ext/ccache/ccache-g++) for the check compiles; the cache key still names g++
    cmd = os.environ.get("DECOMP2HOST_CXX", "g++").split() + HOST_COMPILE_FLAGS + [
           "-I" + os.path.join(PORT_ROOT, "include"), "-I" + os.path.join(PORT_ROOT, "include/gtmath"),
           "-I" + os.path.join(os.path.dirname(os.path.dirname(path)), "include"), "-DB3_PS2_FLOAT=1", path, "-o", obj]
    r = subprocess.run(cmd + list(extra), capture_output=True, text=True)
    return obj, r


def undefined_symbols(obj):
    r = subprocess.run(["nm", "-u", obj], capture_output=True, text=True)
    syms = [l.split()[-1] for l in r.stdout.split("\n") if l.strip()]
    dem = subprocess.run(["c++filt"], input="\n".join(syms), capture_output=True, text=True).stdout.split("\n")
    return list(zip(syms, dem))


def is_namespace(qual, unit_text, fname=None):
    """native2: `namespace Gt2dRenderer { void SetTexture(CGtTexture*); }` (Gt2dObject.h): MWCC mangles a namespace
    function like a static member (SetTexture__12Gt2dRendererFP10CGtTexture), and it has no `this`. The decomp also
    has a `class Gt2dRenderer` (classes/Gt2dRenderer.h) declaring other statics, so the namespace counts when a
    namespace block of that name declares `fname(` and no class body of that name declares it."""
    simple = qual.split("::")[-1]
    srcs = [unit_text] + HEADER_TEXT
    in_ns = False
    for src in srcs:
        m = mask(src)
        for mm in re.finditer(r"\bnamespace\s+%s\s*\{" % re.escape(simple), m):
            body = m[mm.end():match_brace(m, mm.end() - 1)]
            if fname is None or re.search(r"\b%s\s*\(" % re.escape(fname), body):
                in_ns = True
    if not in_ns:
        return False
    # gen1: the unit's own namespace block declaring the function decides, unless the unit's own text has a class body
    # declaring it: HEADER_TEXT holds the headers of every unit of the generation, and classes/B3Im3d.h (other units'
    # header) declares a class B3Im3d whose SetBackfaceCull is a member, which made CB3Globe2dObject::_Render's
    # namespace call a `this` call in the full build only
    um = mask(unit_text)
    own_ns = fname is not None and any(re.search(r"\b%s\s*\(" % re.escape(fname), um[mm.end():match_brace(um, mm.end() - 1)])
                                       for mm in re.finditer(r"\bnamespace\s+%s\s*\{" % re.escape(simple), um))
    if own_ns:
        srcs = [unit_text]
    for src in srcs:
        m = mask(src)
        for mm in re.finditer(r"\b(class|struct)\s+%s\b[^;{]*\{" % re.escape(simple), m):
            body = m[mm.end():match_brace(m, mm.end() - 1)]
            if fname is None or re.search(r"\b%s\s*\(" % re.escape(fname), body):
                return False
    return True


def is_static_member(cls, fname, unit_text):
    simple = cls.split("::")[-1]
    for src in [unit_text] + HEADER_TEXT:
        # native2: every body of that name, not only the first (the decomp has several partial views of one class:
        # the first may not declare fname at all)
        for um in re.finditer(r"\b(class|struct)\s+%s\b[^;{]*\{" % re.escape(simple), src):
            body = src[um.end():match_brace(mask(src), um.end() - 1)]
            if re.search(r"\b%s\s*\(" % re.escape(fname), mask(body)):
                return re.search(r"\bstatic\b[^;{}]*\b%s\s*\(" % re.escape(fname), mask(body)) is not None
    return False


def portable_type(t):
    """A GCC-demangled type from the Linux generation compile, spelled for every host: MWCC's 64-bit long is
    b3host::ps2_long (int64_t: `long` on Linux, `long long` on LLP64 Windows), so a bare `long` must not survive."""
    t = re.sub(r"\bunsigned long\b(?! long)", "b3host::ps2_ulong", t)
    t = re.sub(r"(?<!long )(?<!unsigned )\blong\b(?! long)", "b3host::ps2_long", t)
    return t


BYVAL_TYPES = set()   # native2: struct types passed by value in this unit's wrappers (lone_float decisions)


def lone_float_decision(t, texts):
    """native2: b3host::abi::lone_float<T> for a struct T passed by value [C docs/abi-mwcc-ee.md §2: a 4-byte struct whose
    only member is a float goes in an FPR, every other small struct in a GPR]: 1 when T's body (in the unit or a decomp
    header) has exactly one data member and it is one float, 0 for any other body, None when no body is found (the
    compile then stops at b3host::by_value's static_assert, as before)."""
    simple = t.split("::")[-1].strip()
    if not re.fullmatch(r"[A-Za-z_]\w*", simple):
        return None
    for src in texts:
        m = mask(src)
        for mm in re.finditer(r"\b(struct|class|union)\s+%s\s*(?::[^;{]*)?\{" % re.escape(simple), m):
            if mm.group(1) == "union":
                return 0
            body = m[mm.end():match_brace(m, mm.end() - 1)]
            depth, last, members = 0, 0, []
            for i, c in enumerate(body):
                if c in "{(":
                    depth += 1
                elif c in "})":
                    depth -= 1
                elif c == ";" and depth == 0:
                    st = re.sub(r"^\s*(?:(?:public|private|protected)\s*:\s*)*", "", body[last:i]).strip()
                    last = i + 1
                    if not st or "(" in st or re.match(r"^(static|typedef|using|friend|enum|template)\b", st):
                        continue
                    members.append(st)
            if len(members) == 1 and re.fullmatch(r"(?:volatile\s+)?(?:float|ps2f)\s+[A-Za-z_]\w*", members[0]):
                return 1
            return 0
    return None


SCALAR_SIZES = {"char": 1, "unsigned char": 1, "signed char": 1, "bool": 1, "short": 2, "unsigned short": 2,
                "signed short": 2, "int": 4, "unsigned int": 4, "signed int": 4, "unsigned": 4, "float": 4, "ps2f": 4}


def small_struct_size(t, texts):
    """gen1: the size of struct T when its body holds only scalar members (`unsigned char red, green, blue, alpha;`,
    pointers and gp as 4 bytes, arrays of those), else None. Packed sizes: only used to recognise a 4-byte struct
    without padding (lone_float<T> is needed for exactly those)."""
    simple = t.split("::")[-1].strip()
    if not re.fullmatch(r"[A-Za-z_]\w*", simple):
        return None
    for src in texts:
        m = mask(src)
        mm = re.search(r"\b(?:struct|class)\s+%s\s*\{" % re.escape(simple), m)
        if not mm:
            continue
        body = m[mm.end():match_brace(m, mm.end() - 1)]
        if re.search(r"[{(]", body):
            return None
        size = 0
        for st in body.split(";"):
            st = re.sub(r"^\s*(?:(?:public|private|protected)\s*:\s*)*", "", st).strip()
            if not st:
                continue
            dm = re.match(r"^((?:const\s+|volatile\s+)*(?:(?:unsigned|signed)\s+)?(?:char|short|int|bool|float|ps2f)?)\s*(.*)$", st)
            base = re.sub(r"\b(?:const|volatile)\b", "", dm.group(1)).strip() if dm else ""
            decls = dm.group(2) if dm else ""
            if not base:
                gm = re.match(r"^b3host::gp<[^;]*>\s+(.*)$", st)
                if not gm:
                    return None
                base, decls = "int", gm.group(1)
            for d in split_top(decls):
                d = d.strip()
                n = 1
                for dim in re.findall(r"\[\s*(\d+|0x[0-9A-Fa-f]+)\s*\]", d):
                    n *= int(dim, 0)
                if re.search(r"\[\s*\]", d):
                    return None
                size += n * (4 if d.startswith("*") else SCALAR_SIZES.get(base, 0) or 99)
        return size
    return None


def lone_float_specialisations(types, texts):
    out = []
    for t in sorted(types):
        v = lone_float_decision(t, texts)
        if v is not None:
            out.append("template <> struct b3host::abi::lone_float<%s> { static constexpr int value = %d; };   "
                       "// native2: %s" % (t, v, "one float member" if v else "not a lone float"))
    return out


def code_registration(head, ptypes, addr, k):
    """native2: register a free / static stub's host address with its guest address (b3host::register_code), so the
    function can be handed to guest code as a value (a callback): b3host/call.h code_address."""
    ret = "decltype(%s(%s))" % (head, ", ".join("std::declval<%s>()" % t for t in ptypes if t != "..."))
    return ("static const bool b3reg_%d_%08X = b3host::register_code(reinterpret_cast<const void*>(static_cast<auto (*)(%s) -> %s>"
            "(&%s)), 0x%08Xu);   // native2: callback address" % (k, addr, ", ".join(ptypes), ret, head, addr))


def param_decl(t, name):
    fm = re.match(r"^(.*)\(\*\)\((.*)\)$", t.strip())
    if fm:
        return "%s(*%s)(%s)" % (fm.group(1), name, fm.group(2))
    return "%s %s" % (t, name)


def defined_symbols(obj):
    r = subprocess.run(["nm", "--defined-only", obj], capture_output=True, text=True)
    return {l.split()[-1] for l in r.stdout.split("\n") if len(l.split()) == 3 and l.split()[1] == "T"}


_HEADER_DIGESTS = {}
_HEADER_LOCK = __import__("threading").Lock()


# boot2 (2026-11-07): headers no unit includes (the native table's lists; they change with every select-list edit, and
# in the digest they made one added function invalidate every unit's cache: 746 s of regeneration). native_watch.h
# stays in: b3host/watch.h includes it.
# gen1: b3port/native_rows.h too (the list of the units' row arrays: only native_table.cpp includes it; it changes with
# every selection and stays in the reused gen_native.new, so it had invalidated every unit's entry)
_DIGEST_SKIP = frozenset({os.path.join("b3port", "game_overrides.h"), os.path.join("b3port", "native_rows.h")})


def _headers_digest(out_include):
    """boot2: one digest of every header a unit's host compile can see (this repo's include/ and OUT/include)."""
    import hashlib
    with _HEADER_LOCK:
        if out_include not in _HEADER_DIGESTS:
            h = hashlib.sha256()
            for root in (os.path.join(PORT_ROOT, "include"), out_include):
                for d, _, fs in sorted(os.walk(root)):
                    for f in sorted(fs):
                        q = os.path.join(d, f)
                        if os.path.relpath(q, root) in _DIGEST_SKIP:
                            continue
                        h.update(os.path.relpath(q, root).encode() + b"\0" + open(q, "rb").read())
            _HEADER_DIGESTS[out_include] = h.hexdigest()
        return _HEADER_DIGESTS[out_include]


CROSSED_DECL = re.compile(r"^(const\s+)?((?:(?:unsigned|signed)\s+)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<[^;<>]*>)?"
                          r"(?:\s*\*+\s*(?:const\s+)?|\s+))([A-Za-z_]\w*)\s*=(?!=)([^;]*);")


def bracket_commas(s):
    """gen1: whether s (an initialiser) has a comma outside brackets: several declarators"""
    d = 0
    for c in mask(s):
        d += 1 if c in "([{" else -1 if c in ")]}" else 0
        if c == "," and d == 0:
            return True
    return False


# gen1: the shadow header's GtMathPs2 value types whose default constructor is trivial (`= default`: a goto may cross
# such a declaration once it has no initialiser); tests/test_gtmath.cpp asserts it for CGtV3d / CGtV4d. Not CGtV2d:
# its user-provided constructor stays (made trivial, GCC's -O2 code of 10 live units changed)
TRIVIAL_GTMATH = ("CGtV3d", "CGtV4d", "XAxisType", "YAxisType", "ZAxisType", "WAxisType")


# ---------------------------------------------------------------- T40 (gen1): annotated function-local statics
_LSTATIC = re.compile(r"^(?P<lead>[ \t]*)static\s+(?P<const>const\s+)?(?P<type>[A-Za-z_][\w:]*(?:\s*<[^;<>]*>)?)\s+(?P<name>[A-Za-z_]\w*)\s*"
                      r"(?P<init>\([^;]*\)|=[^;]*|\{[^;]*\})?\s*;(?P<rest>[ \t]*//(?P<comment>[^\n]*))$", re.M)


_LOCAL_STATIC_SYMS = {}


def local_static_symbols(decomp):
    """gen1 T40: retail's function-local statics by name, from the decomp's symbol table comments
    (`// no symbol form @ 0x004E294C: lnRotate$36523 [medium]`): {name: [(address, confidence)]}; a `conflict` line is
    no entry."""
    root = getattr(decomp, "root", None)
    if root not in _LOCAL_STATIC_SYMS:
        out = {}
        try:
            for l in open(os.path.join(root, "config/symbol_addrs_b3names.txt"), errors="replace"):
                mm = re.match(r"^//\s*no symbol form @ 0x([0-9A-Fa-f]{8}):\s*([A-Za-z_]\w*)\$\d+\s*\[(\w+)\]", l)
                if mm:
                    out.setdefault(mm.group(2), []).append((int(mm.group(1), 16), mm.group(3)))
        except (OSError, TypeError):
            pass
        _LOCAL_STATIC_SYMS[root] = out
    return _LOCAL_STATIC_SYMS[root]


def local_static_transforms(text, report, decomp=None):
    """T40 (gen1): a function-local static the decomp annotates with its guest object, and for a dynamic initialiser
    its guard byte (`static CGtV2d sScale(1.0f, 0.9f); // D_004B7300 (guard D_004E26C0)`, or `// D_004B7308
    (D_004E26C4)`), is that guest object: natively it had been a host static, so retail code reading D_004B7300 saw
    0 (movie2's poll-1 mapping). With a guard: b3host::guest_static constructs it in place once and sets the guard
    as retail does; without one (a constant initialiser, in the image): the guest global itself. Another annotated
    form (an array, a dynamic initialiser without a guard) is left and reported."""
    fns = top_level_functions(text)
    edits, done, left = [], [], []
    syms = local_static_symbols(decomp) if decomp is not None else {}
    lstatic = re.compile(_LSTATIC.pattern.replace(r";(?P<rest>[ \t]*//(?P<comment>[^\n]*))$", r";(?P<rest>[ \t]*(?://(?P<comment>[^\n]*))?)$"), re.M)
    for mm in lstatic.finditer(text):
        if not any(f[1] < mm.start() < f[2] for f in fns):
            continue
        ds = re.findall(r"\bD_([0-9A-Fa-f]{8})\b", mm.group("comment") or "")
        if not ds and len(syms.get(mm.group("name"), [])) == 1:
            # vu0: retail's own name for it (`lnRotate$36523` @ 0x004E294C, CB3GraphicsManager::PresentDrawBuffer 0x2270B0:
            # the dither index a restore had started at 0), when the name is retail's only function-local static so called
            ds = ["%08X" % syms[mm.group("name")][0][0]]
        if not ds:
            continue
        typ, nm, init = mm.group("type"), mm.group("name"), (mm.group("init") or "").strip()
        cq = "const " if mm.group("const") else ""
        obj = int(ds[0], 16)
        if len(ds) >= 2:
            g = int(ds[1], 16)
            if init.startswith("("):
                mk = "%s%s" % (typ, init)
            elif init.startswith("{"):
                mk = "%s%s" % (typ, init)
            elif init.startswith("="):
                mk = "%s(%s)" % (typ, init[1:].strip())
            else:
                mk = "%s()" % typ
            rep = "%s%s%s &%s = b3host::guest_static<%s>(0x%08Xu, 0x%08Xu, [&]() { return %s; });" % (
                mm.group("lead"), cq, typ, nm, typ, obj, g, mk)
        elif not init or re.fullmatch(r"=\s*[-+]?[\w.]+|\{[^()]*\}|=\s*\{[^()]*\}", init):
            rep = "%s%s%s &%s = b3host::guest_global<%s>(0x%08Xu);" % (mm.group("lead"), cq, typ, nm, typ, obj)
        else:
            left.append(nm)
            continue
        edits.append((mm.start(), mm.start() + len(mm.group(0)) - len(mm.group("rest")),
                      rep + " /* decomp2host T40: the guest's static */"))
        done.append("%s 0x%08X" % (nm, obj))
    if done:
        report.append("T40 guest statics: %s" % ", ".join(done))
    if left:
        report.append("T40 left (no guard / form): %s" % ", ".join(left))
    return apply(text, edits) if edits else text


# ---------------------------------------------------------------- T39 (gen1): integer division with the EE's results
_T39_LIT = re.compile(r"^\(*\s*(?:0[xX][0-9A-Fa-f]+|\d+)[uUlL]*\s*\)*$|^\(*\s*(?:\d+\.\d*|\.\d+|\d+\.?\d*[eE][-+]?\d+)[fFlL]?\s*\)*$")
_OPERAND_END = re.compile(r"[\w)\]]")


_T39_TYPE_WORDS = {"int", "float", "double", "char", "short", "long", "unsigned", "signed", "bool", "void", "ps2f",
                   "u_long128", "size_t", "ptrdiff_t"}


def _is_cast_type(inner):
    """T39: the text inside `( ... )` names a type (a cast), not a parenthesised expression."""
    s = inner.strip()
    if not re.fullmatch(r"(?:(?:const|volatile|unsigned|signed|struct|class|enum)\s+)*[A-Za-z_][\w:]*(?:\s*<[^()]*>)?"
                        r"(?:\s+(?:int|long|short|char|double))*(?:\s*(?:const\s*)?[*&])*", s):
        return False
    words = re.findall(r"[A-Za-z_][\w:]*", s)
    if "*" in s or "&" in s or words[0] in ("const", "volatile", "unsigned", "signed", "struct", "class", "enum"):
        return True
    w = words[-1] if len(words) > 1 else words[0]
    return w in _T39_TYPE_WORDS or w in INT_TYPES or bool(re.match(
        r"(?:\w+::)*(?:UnkStruct_|UnkEnum_|C[A-Z]|E[A-Z]|Rw[A-Z]|Rp[A-Z]|b3host::)", s)) or "::" in s and s[s.rfind("::") + 2:s.rfind("::") + 3].isupper()


def _rhs_end(m, i):
    """End of the unary expression starting at m[i] (after skipping blanks): prefix operators, casts, a primary
    expression, its postfix parts."""
    n = len(m)
    while i < n and m[i].isspace():
        i += 1
    while True:   # prefix operators and casts
        if m.startswith(("++", "--"), i):
            i += 2
        elif i < n and m[i] in "-+!~*&":
            i += 1
        elif re.match(r"(?:sizeof|alignof)\b", m[i:]):
            i = re.match(r"(?:sizeof|alignof)\b\s*", m[i:]).end() + i
            if i < n and m[i] == "(":
                return _postfix_end(m, _close(m, i) + 1)
        elif i < n and m[i] == "(":
            j = _close(m, i)
            k = j + 1
            while k < n and m[k].isspace():
                k += 1
            # `(T)x`: a cast when the parenthesis names a type and an operand follows, else a parenthesised primary
            if k < n and (m[k].isalnum() or m[k] in "_(-~!*&.'\"") and _is_cast_type(m[i + 1:j]):
                i = k
                continue
            return _postfix_end(m, j + 1)
        else:
            break
        while i < n and m[i].isspace():
            i += 1
    mm = re.match(r"(?:\d[\w.]*(?:[eE][-+]?\d+)?[fFlLuU]*|'[^']*'|\"[^\"]*\"|(?:::)?[A-Za-z_]\w*(?:\s*::\s*[A-Za-z_~]\w*)*)", m[i:])
    if not mm:
        return None
    j = i + mm.end()
    if re.fullmatch(r"(?:static|reinterpret|const|dynamic)_cast|b3host::\w+|[A-Za-z_]\w*", mm.group(0).replace(" ", "")) and j < n and m[j] == "<":
        depth, k = 0, j   # a template-id followed by a call (`static_cast<int>(x)`, `b3host::icast<T>(x)`)
        while k < n:
            if m[k] == "<":
                depth += 1
            elif m[k] == ">":
                depth -= 1
                if depth == 0:
                    break
            elif m[k] in ";{}":
                break
            k += 1
        if depth == 0 and k < n and re.match(r"\s*\(", m[k + 1:]):
            j = k + 1
    return _postfix_end(m, j)


def _close(m, i):
    d = 0
    for k in range(i, len(m)):
        if m[k] in "([{":
            d += 1
        elif m[k] in ")]}":
            d -= 1
            if d == 0:
                return k
    raise ValueError("unbalanced")


def _postfix_end(m, j):
    n = len(m)
    while True:
        k = j
        while k < n and m[k].isspace():
            k += 1
        if k < n and m[k] in "([":
            j = _close(m, k) + 1
        elif m.startswith("->", k) or (k < n and m[k] == "." and not (k + 1 < n and m[k + 1].isdigit())):
            k += 2 if m.startswith("->", k) else 1
            mm = re.match(r"\s*(?:template\s+)?~?[A-Za-z_]\w*", m[k:])
            if not mm:
                return j
            j = k + mm.end()
        elif m.startswith(("++", "--"), k):
            j = k + 2
        else:
            return j


def _open(m, k):
    d = 0
    for q in range(k, -1, -1):
        if m[q] in ")]}":
            d += 1
        elif m[q] in "([{":
            d -= 1
            if d == 0:
                return q
    raise ValueError("unbalanced")


def _lhs_start(m, i):
    """Start of the multiplicative expression ending just before the operator at m[i] (left-associative `* / %`
    operands and their prefix operators / casts)."""
    while True:
        k = i - 1
        while k >= 0 and m[k].isspace():
            k -= 1
        if k < 0:
            return None
        # one postfix expression, backwards
        while True:
            if k >= 1 and m[k - 1:k + 1] in ("++", "--"):   # postfix increment / decrement
                k -= 2
                while k >= 0 and m[k].isspace():
                    k -= 1
                continue
            if m[k] in ")]":
                k = _open(m, k) - 1
                # a call of a template-id (`static_cast<unsigned>(a)`, `b3host::icast<int>(p)`): include `name<...>`
                tm = re.search(r"(?:\b(?:static|reinterpret|const|dynamic)_cast|\b[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)<[^<>()]*>$", m[:k + 1])
                if m[k + 1] == "(" and tm:
                    k = tm.start() - 1   # then on to what joins it on the left
                else:
                    q = k
                    while q >= 0 and m[q].isspace():
                        q -= 1
                    # a call / an index on what stands before the group (`f(1)`, `a[i](x)`), not a keyword's
                    # parenthesis (`return (c) / n`)
                    if q >= 0 and (m[q] in ")]" or ((m[q].isalnum() or m[q] == "_") and not re.search(
                            r"\b(?:return|case|throw|else|do|sizeof|alignof|if|while|for|switch)$", m[:q + 1]))):
                        k = q
                        continue
            elif m[k].isalnum() or m[k] == "_":
                while k >= 0 and (m[k].isalnum() or m[k] == "_"):
                    k -= 1
                if m[k + 1].isdigit() and k >= 0 and m[k] == ".":   # 1.5f
                    k -= 1
                    while k >= 0 and m[k].isdigit():
                        k -= 1
            elif m[k] == "'" or m[k] == '"':
                q = m.rfind(m[k], 0, k)
                k = q - 1
            else:
                return None
            # what joins it to the left: `.`, `->`, `::`, a template-id's `>` of a call `f<T>(x)`
            q = k
            while q >= 0 and m[q].isspace():
                q -= 1
            if q >= 0 and m[q] == "." or (q >= 1 and m[q - 1:q + 1] in ("->", "::")):
                k = q - (1 if m[q] == "." else 2)
                while k >= 0 and m[k].isspace():
                    k -= 1
                continue
            break
        start = k + 1
        # prefix operators and casts directly before it
        while True:
            q = start - 1
            while q >= 0 and m[q].isspace():
                q -= 1
            if q >= 1 and m[q] in "&|" and m[q - 1] == m[q]:
                break   # `&&` / `||`: a binary operator, the operand starts here
            if q >= 1 and m[q] in "+-" and m[q - 1] == m[q]:
                start = q - 1   # prefix `++` / `--`
                continue
            if q >= 0 and m[q] in "-+!~*&" and not (q >= 1 and m[q - 1] in "-+&|<>=!" and m[q - 1] != m[q]):
                p = q - 1
                while p >= 0 and m[p].isspace():
                    p -= 1
                if p < 0 or not _OPERAND_END.match(m[p]) or re.search(r"\b(?:return|case|throw|else|do)$", m[:p + 1]):
                    start = q   # unary
                    continue
            if q >= 0 and m[q] == ")":
                o = _open(m, q)
                inner = m[o + 1:q]
                pq = o - 1
                while pq >= 0 and m[pq].isspace():
                    pq -= 1
                if _is_cast_type(inner) and not (pq >= 0 and (m[pq].isalnum() or m[pq] == "_")):
                    start = o   # `(T)x`
                    continue
            break
        # extend over a left `*`, `/`, `%` (binary: the same precedence, left-associative)
        q = start - 1
        while q >= 0 and m[q].isspace():
            q -= 1
        if q >= 0 and m[q] in "*/%" and not (q >= 1 and m[q - 1] in "-") and not m.startswith("=", q + 1):
            p = q - 1
            while p >= 0 and m[p].isspace():
                p -= 1
            if p >= 0 and _OPERAND_END.match(m[p]) and not re.search(r"\b(?:return|case|throw|else|do)$", m[:p + 1]):
                i = q
                continue
        return start


def ee_division_transforms(text, report):
    """T39 (gen1): `a / b` and `a % b` (and `a /= b`, `a %= b`) whose divisor is not a non-zero literal become
    b3host::ee_div / ee_mod (/ _assign) (b3host/eediv.h): the EE's results for a zero divisor and INT_MIN / -1 where
    x86 traps; the plain operator for operands that are not both integral (floats, ps2f, GtMath types). Function
    bodies only (masked: comments, literals and preprocessor lines are not touched). A division the scanner cannot
    delimit stays as it was (counted in the report)."""
    n_done = n_left = 0
    pos = 0
    while True:
        m = preproc_mask(mask(text))
        mm = re.compile(r"(?<![/*])([/%])(=?)(?![/*])").search(m, pos)
        if not mm:
            break
        i, op, assign = mm.start(), mm.group(1), mm.group(2)
        pos = i + 1
        if re.search(r"\boperator\s*$", m[max(0, i - 12):i]) or (i > 0 and m[i - 1] == "%") or m.startswith("%>", i) or \
                m.startswith("%:", i) or m.startswith(">", i + 1) and op == "%":
            continue
        if not any(f[1] < i < f[2] for f in top_level_functions(text)):
            continue
        try:
            rs = i + 1 + len(assign)
            re_ = _rhs_end(m, rs)
            if assign:
                ls = _lhs_start(m, i)
            else:
                ls = _lhs_start(m, i)
        except (ValueError, IndexError):
            re_ = None
            ls = None
        if re_ is None or ls is None:
            n_left += 1
            continue
        rhs = text[rs:re_].strip()
        if _T39_LIT.match(mask(rhs)) and not re.fullmatch(r"\(*\s*0+[uUlL]*\s*\)*", rhs):
            continue   # a non-zero literal divisor (or a floating literal): no zero to meet, no INT_MIN / -1 but for -1
        lhs = text[ls:i].strip()
        call_re = r"\b(?!(?:sizeof|alignof|static_cast|reinterpret_cast|const_cast)\b)[A-Za-z_]\w*\s*(?:<[^<>()]*>)?\s*\("
        if not assign and re.search(call_re, mask(lhs)) and re.search(call_re, mask(rhs)):
            # both operands call something: as call arguments T17's order rule would apply (or refuse the unit);
            # the plain division keeps GCC's operand order as before
            n_left += 1
            continue
        if assign:
            # the assignment's right side runs to the expression's end: `a /= b + c` is `a /= (b + c)`
            d_, e_ = 0, rs
            while e_ < len(m):
                c_ = m[e_]
                if c_ in "([{":
                    d_ += 1
                elif c_ in ")]}":
                    if d_ == 0:
                        break
                    d_ -= 1
                elif c_ in ";," and d_ == 0:
                    break
                e_ += 1
            rhs, re_ = text[rs:e_].strip(), e_
            rep = "b3host::ee_%s_assign(%s, %s)" % ("div" if op == "/" else "mod", lhs, rhs)
        else:
            rep = "b3host::ee_%s(%s, %s)" % ("div" if op == "/" else "mod", lhs, rhs)
        text = text[:ls] + rep + text[re_:]
        pos = ls + len("b3host::ee_div(")
        n_done += 1
    if n_done or n_left:
        report.append("T39 EE division %d%s" % (n_done, (", %d left" % n_left) if n_left else ""))
    return text


# gen1: the compiler-guided rewrites in compile_for_stubs, part of its cache key (a new rule re-runs every unit once)
COMPILE_FIX_VERSION = "t38"   # (T39 changes unit text itself: no bump needed)


# gen1 T38: what only GCC's -fpermissive accepts in a unit that compiles (clang -- Apple's -- refuses each; mac2's list)
STRICT_JUMP = re.compile(r"([^\s:]+):(\d+):(\d+): note:\s+(?:crosses|skips) initialization of ‘([^’]*)’")
STRICT_VOLATILE = re.compile(r"([^\s:]+):(\d+):(\d+): warning: passing ‘volatile [^’]*’ as ‘this’ argument discards qualifiers")
STRICT_RVALUE = re.compile(r"([^\s:]+):(\d+):(\d+): warning: taking address of rvalue")


def strict_diagnostics(stderr, base):
    """gen1 T38: the unit's (line, col, kind, text) diagnostics that only -fpermissive lets through, kinds jump /
    volatile / rvalue."""
    out = []
    for kind, rx in (("jump", STRICT_JUMP), ("volatile", STRICT_VOLATILE), ("rvalue", STRICT_RVALUE)):
        for mm in rx.finditer(stderr):
            if os.path.basename(mm.group(1)) == base:
                out.append((int(mm.group(2)), int(mm.group(3)), kind, mm.group(4) if kind == "jump" else ""))
    return sorted(set(out))


def strict_fix(text, path, stderr):
    """gen1 T38 (compiler-guided, after a successful compile): standard C++ for what -fpermissive accepted.
    - A jump past a declaration's initialisation (GCC 15: "skips initialization of"): T8b's split in place
      (scalars, pointers -- `T *const p = e` loses the local's const --, GtMath value types); a local T8b cannot split
      in place (a class type T8 already split, as CGtV2d with its user-provided constructor; a for-init `for (int c =
      0; ...)` entered by a goto) is declared at the function's start, when the function declares the name once and
      does not name it before (no shadowing changes meaning): one object for the whole function, as MWCC's frame slot,
      so a value kept across the jump stays (GenerateInitialTraffic: `goto join` re-enters the loop).
    - A store to a member of a local `volatile` aggregate (B3OnScreenKeyboardComponent_0034D360's retail dead stores):
      the local loses `volatile` (a host stack local nothing else sees; -fpermissive had dropped it at each store).
    - `T *p = &f(...).m;`, the address of a member of a temporary (B3VehicleDeform_003E7110): the temporary is bound
      to `auto &&b3_tmp_p` first, so it lives as long as p (MWCC keeps it in the frame; on the host it had died at the
      end of the statement, p dangling).
    Lines are not added or removed (GCC's line numbers stay valid)."""
    base = os.path.basename(path)
    diags = strict_diagnostics(stderr, base)
    if not diags:
        return text
    t = crossed_declaration_fix(text, path, "\n".join(
        "%s:%d:%d: note:   crosses initialization of ‘%s’" % (base, ln, col, d) for ln, col, k, d in diags if k == "jump"))
    lines, orig_lines = t.split("\n"), text.split("\n")
    offs = [0]
    for l in lines:
        offs.append(offs[-1] + len(l) + 1)
    m = mask(t)
    fns = top_level_functions(t)
    edits, hoisted = [], set()
    for ln, col, kind, d in diags:
        if not (1 <= ln <= len(lines)):
            continue
        line, pos = lines[ln - 1], offs[ln - 1] + col - 1
        f = next((f for f in fns if f[1] <= pos <= f[2]), None)
        if f is None:
            continue
        body = m[f[1]:f[2] + 1]
        if kind == "jump":
            nm = d.split()[-1]
            if line[col - 1:col - 1 + len(nm)] != nm or (f[1], nm) in hoisted:
                continue
            pre = line[:col - 1]
            fm = re.search(r"\bfor\s*\(\s*((?:const\s+)?[A-Za-z_][\w:]*(?:\s*<[^;<>()]*>)?(?:\s*\*+)?)\s+$", pre)
            sm = re.search(r"((?:const\s+)?[A-Za-z_][\w:]*(?:\s*<[^;<>()]*>)?(?:\s*\*+)?)\s+$", pre)
            after = line[col - 1 + len(nm):]
            am = re.match(r"\s*__attribute__\(\(uninitialized\)\);", after)
            im = re.match(r"\s*=(?!=)", after)
            gm = re.match(r"\s*\(", after)
            if gm and sm and re.fullmatch(r"(?:const\s+)?(?:GtMathPs2::)?(?:%s|CGtV2d)" % "|".join(GTMATH_VALUE_TYPES), sm.group(1).strip()):
                # gen1: `CGtV2d r(a, b);` crossed (func_003A9250): `r = CGtV2d(a, b);` here, declared at the start
                o_ = pos + len(nm) + gm.end() - 1
                c_ = match_paren(m, o_)
                if c_ is None or not re.match(r"\s*;", m[c_ + 1:]):
                    continue
                tq = sm.group(1).strip()
                typ, drop_lo, drop_hi = tq, offs[ln - 1] + sm.start(1), c_ + 1
                e_ = re.compile(re.escape(nm))
                decls = re.findall(r"[\w>*&]\s+%s\s*(?:[;=,(\[)]|__attribute__)" % re.escape(nm), body)
                if len(decls) != 1 or re.search(r"(?<![\w.>])%s\b" % re.escape(nm), body[:max(0, drop_lo - f[1])]) or (f[1], nm) in hoisted:
                    continue
                hoisted.add((f[1], nm))
                edits.append((drop_lo, drop_hi, "%s = %s%s" % (nm, re.sub(r"^const\s+", "", tq), text[o_:c_ + 1])))
                edits.append((f[1] + 1, f[1] + 1, " %s %s __attribute__((uninitialized)); /* decomp2host T38: crossed by a jump */" % (
                    re.sub(r"^const\s+", "", tq), nm)))
                continue
            if fm:
                typ, drop_lo, drop_hi = fm.group(1), offs[ln - 1] + fm.start(1), pos
            elif sm and im and re.fullmatch(r"(?:const\s+)?(?:GtMathPs2::)?(?:%s|CGtV2d)" % "|".join(GTMATH_VALUE_TYPES), sm.group(1).strip()):
                # gen1: `CGtV2d v = e;` crossed (a user-provided constructor: a hard error; ReleaseExtraLeadIn 0x3A99E0,
                # func_003A9250): declared at the function's start, `v = e;` left (an empty constructor, then a copy)
                typ, drop_lo, drop_hi = sm.group(1), offs[ln - 1] + sm.start(1), pos
            elif sm and am and ln <= len(orig_lines) and re.match(r"\s*__attribute__\(\(uninitialized\)\);",
                                                                  orig_lines[ln - 1][col - 1 + len(nm):]):
                # split before this pass (T8's class-type form, CGtV2d): moved; one T8b just split in place is enough
                typ, drop_lo, drop_hi = sm.group(1), offs[ln - 1] + sm.start(1), pos + len(nm) + am.end()
            else:
                continue
            typ = re.sub(r"^const\s+", "", typ.strip())
            e = re.escape(nm)
            decls = re.findall(r"[\w>*&]\s+%s\s*(?:[;=,(\[)]|__attribute__)" % e, body)
            used_before = re.search(r"(?<![\w.>])%s\b" % e, body[:max(0, drop_lo - f[1])])
            if len(decls) != 1 or used_before:
                continue
            hoisted.add((f[1], nm))
            if fm or im and not am:
                edits.append((drop_lo, drop_hi, ""))   # `for (int c = 0;` -> `for (c = 0;`, `CGtV2d v = e;` -> `v = e;`
            else:
                # `T x __attribute__((uninitialized));` dropped: the assignment after it stays
                edits.append((drop_lo, drop_hi, "/* decomp2host T38: %s at the function's start */" % nm))
            edits.append((f[1] + 1, f[1] + 1, " %s %s __attribute__((uninitialized)); /* decomp2host T38: crossed by a jump */" % (typ, nm)))
        elif kind == "volatile":
            st = max(line.rfind(";", 0, col), line.rfind("{", 0, col)) + 1
            om = re.match(r"\s*([A-Za-z_]\w*)\s*(?:\.\s*[A-Za-z_]\w*\s*)+=", line[st:])
            if not om:
                continue
            nm = om.group(1)
            vd = list(re.finditer(r"\bvolatile\s+((?:struct\s+)?[A-Za-z_][\w:]*)\s+%s\s*;" % re.escape(nm), body))
            if len(vd) != 1:
                continue
            a = f[1] + vd[0].start()
            if any(s_ == a for s_, _, _ in edits):
                continue
            edits.append((a, a + len("volatile"), "/* decomp2host T38: volatile */"))
        elif kind == "rvalue":
            dm = re.match(r"^(\s*)((?:const\s+)?[A-Za-z_][\w:]*(?:\s*<[^;<>()]*>)?\s*\*\s*(?:const\s*)?)([A-Za-z_]\w*)\s*=\s*&(.+)\.([A-Za-z_]\w*)\s*;", line)
            if not dm or not dm.group(4).rstrip().endswith(")") or bracket_commas(dm.group(4)):
                continue
            ex, dd, first_close = mask(dm.group(4)).rstrip(), 0, None   # one call: its first `(` closes at the end
            for k_, ch_ in enumerate(ex):
                dd += 1 if ch_ == "(" else -1 if ch_ == ")" else 0
                if ch_ == ")" and dd == 0:
                    first_close = k_
                    break
            if first_close != len(ex) - 1:
                continue
            lo = offs[ln - 1] + dm.start(2)
            edits.append((lo, offs[ln - 1] + dm.end(), "auto &&b3_tmp_%s = %s; %s%s = &b3_tmp_%s.%s; /* decomp2host T38: the temporary lives as long as %s */" % (
                dm.group(3), dm.group(4).strip(), dm.group(2), dm.group(3), dm.group(3), dm.group(5), dm.group(3))))
    return apply(t, edits) if edits else t


def crossed_declaration_fix(text, path, stderr):
    """gen1 (T8b): the declarations GCC names in `jump to label ... crosses initialization of` notes, when T8's form
    did not catch them (`const int t1 = j + 1;`, `const T *const t13 = d->field_10;`, `const void *pr = &right;`):
    each becomes `T x __attribute__((uninitialized)); x = e;` with the local's own const dropped, as T8 does (a const
    local is assigned once: the same); a pointee's const stays. Guided by the compiler, so only units that do not compile change. Class types (a
    CGtV2d with a constructor) are left: splitting needs a default constructor, and the unit keeps failing."""
    lines = text.split("\n")
    base = os.path.basename(path)
    changed = False
    for mm in re.finditer(r"([^\s:]+):(\d+):(\d+): note:\s+(?:crosses|skips) initialization of", stderr):   # gen1: GCC 15 says "skips"
        if os.path.basename(mm.group(1)) != base:
            continue
        ln, col = int(mm.group(2)) - 1, int(mm.group(3)) - 1
        if not (0 <= ln < len(lines)):
            continue
        line = lines[ln]
        # the note's column is the declarator's name; the declaration starts after the previous `;`, `{` or `}`
        st = max(line.rfind(";", 0, col), line.rfind("{", 0, col), line.rfind("}", 0, col)) + 1
        while st < len(line) and line[st].isspace():
            st += 1
        dm = CROSSED_DECL.match(line[st:])
        if not dm:
            # gen1: a GtMathPs2 value type with a trivial default constructor (the shadow header's CGtV2d / CGtV3d /
            # CGtV4d / lane types), direct-initialised: `CGtV2d r(a, b);` -> `CGtV2d r; r = CGtV2d(a, b);`
            dd = re.match(r"^((?:GtMathPs2::)?(?:%s))\s+([A-Za-z_]\w*)\s*\(([^;]*)\)\s*;" % "|".join(TRIVIAL_GTMATH), line[st:])
            if dd and st + dd.start(2) == col:
                rep = "%s %s __attribute__((uninitialized)); %s = %s(%s); /* decomp2host T8b */" % (
                    dd.group(1), dd.group(2), dd.group(2), dd.group(1), dd.group(3))
                lines[ln] = line[:st] + rep + line[st + dd.end():]
                changed = True
            continue
        if dm.group(2).split()[0] in NOT_TYPES or st + dm.start(3) != col or "__attribute__" in dm.group(0):
            continue
        multi = bracket_commas(dm.group(4))   # several declarators? (split_top counts the `>` of `->` as a bracket)
        typ = re.sub(r"\*\s*const\s+", "*", dm.group(2))
        if multi:
            # gen1: `int fb = e1, fa = e2;` (scalars / pointers of one base type) -> each declared and assigned in order
            parts, d_, cur = [], 0, ""
            for ch_, mc_ in zip(dm.group(4), mask(dm.group(4))):
                d_ += 1 if mc_ in "([{" else -1 if mc_ in ")]}" else 0
                if mc_ == "," and d_ == 0:
                    parts.append(cur)
                    cur = ""
                else:
                    cur += ch_
            parts.append(cur)
            seq = ["%s%s __attribute__((uninitialized)); %s =%s;" % (typ, dm.group(3), dm.group(3), parts[0])]
            if "*" in typ or not re.fullmatch(r"\s*(?:(?:unsigned|signed)\s+)?(?:char|short|int|long|bool|ps2f)\s*", typ):
                continue   # pointers: `T *a = x, *b = y` repeat the star per declarator; not split here
            for q in parts[1:]:
                qm = re.match(r"^\s*([A-Za-z_]\w*)\s*(?:=(.*))?$", q, re.S)
                if not qm:
                    seq = None
                    break
                seq.append("%s%s __attribute__((uninitialized));%s" % (typ, qm.group(1), (" %s =%s;" % (qm.group(1), qm.group(2))) if qm.group(2) else ""))
            if not seq:
                continue
            lines[ln] = line[:st] + " ".join(seq) + " /* decomp2host T8b */" + line[st + dm.end():]
            changed = True
            continue
        if "*" in typ and dm.group(1):
            typ = dm.group(1) + typ   # `const T *p`: the pointee's const stays, only the local's own is dropped
        if not ("*" in typ or re.fullmatch(r"\s*(?:(?:unsigned|signed)\s+)?(?:char|short|int|long|bool|ps2f|b3host::ps2_long|b3host::ps2_ulong)\s*", typ)
                or re.fullmatch(r"\s*(?:GtMathPs2::)?(?:%s)\s*" % "|".join(TRIVIAL_GTMATH), typ)):
            continue   # another class type: maybe no trivial default constructor to split with
        rep = "%s%s __attribute__((uninitialized)); %s =%s; /* decomp2host T8b */" % (typ, dm.group(3), dm.group(3), dm.group(4))
        lines[ln] = line[:st] + rep + line[st + dm.end():]
        changed = True
    out = "\n".join(lines) if changed else text
    # gen1: a crossed local of a struct the unit declares with only empty-bodied constructor / destructor (a matching
    # device: `struct UnkStruct_00257C70_pad { int x[4]; UnkStruct_00257C70_pad() {} ~UnkStruct_00257C70_pad() {} };`)
    # loses them: the same struct, now trivial, so the jump is allowed
    for nm in set(re.findall(r"crosses initialization of ‘(?:[\w:]*::)?([A-Za-z_]\w*) [A-Za-z_]\w*’", stderr)):
        e = re.escape(nm)
        sm = re.search(r"\bstruct\s+%s\s*\{([^{}]*(?:\{\s*\}[^{}]*)*)\}" % e, out)
        if not sm:
            continue
        body = sm.group(1)
        nb = re.sub(r"\s*~?%s\s*\(\s*\)\s*\{\s*\}" % e, "", body)
        if nb != body and not re.search(r"\b%s\s*\(" % e, nb):
            out = out[:sm.start(1)] + nb + " /* decomp2host T8b: empty constructor / destructor dropped */ " + out[sm.end(1):]
    return out


NSQ_ERR = re.compile(r"([^\s:]+):(\d+):\d+: error: ‘([A-Za-z_]\w*)’ was not declared in this scope; did you mean ‘(b3gen_\w+)::\3’\?")


def ns_qualify_fix(text, path, stderr):
    """gen1 (T7), compiler-guided: a helper of the unit's namespace called from a member function of a header-declared
    class (CB3AIArbitrator::CalculateSpeedMatch 0x19D4D0 and func_0019D4D0_inl2): name lookup goes to the class's
    namespace, so the call is qualified with the unit's on the lines GCC names (its "did you mean" hint)."""
    lines = text.split("\n")
    for qm in NSQ_ERR.finditer(stderr):
        if os.path.basename(qm.group(1)) != os.path.basename(path):
            continue
        ln = int(qm.group(2)) - 1
        if 0 <= ln < len(lines):
            lines[ln] = re.sub(r"(?<![\w:])%s\s*\(" % re.escape(qm.group(3)), "%s::%s(" % (qm.group(4), qm.group(3)), lines[ln])
    return "\n".join(lines)


def const_ptr_ptr_fix(text, path, stderr):
    """gen1 (T3b), compiler-guided: `const CB3Wheel *const *wp = &wheels[i];` (a pointer to a const pointer stored in
    guest memory, func_0028E2E0; T3b's `T **` form takes no const between the stars) -> `b3host::gp<const CB3Wheel>
    const *wp` on the lines GCC names; units that compile keep their text."""
    lines = text.split("\n")
    base = os.path.basename(path)
    changed = False
    for mm in re.finditer(r"([^\s:]+):(\d+):\d+: error: cannot convert ‘b3host::gp<[^’]*>\*’ to ‘[^’]*\* const\*’", stderr):
        if os.path.basename(mm.group(1)) != base:
            continue
        ln = int(mm.group(2)) - 1
        if not (0 <= ln < len(lines)):
            continue
        new = re.sub(r"\b((?:const\s+)?(?:(?:unsigned|signed)\s+)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s+const)?)\s*\*\s*const\s*\*",
                     lambda x: "b3host::gp<%s> const *" % x.group(1), lines[ln])
        if new != lines[ln]:
            lines[ln] = new
            changed = True
    return "\n".join(lines) if changed else text


def host_fnptr_fix(text, path, stderr):
    """gen1 (T9), compiler-guided: a game function cast to a function-pointer type (`(void (*)(const
    CGt2dObjectSpace &, const void *))_Render__20CB3FlashText2dObject...`, T9: gfn::from_raw(icast(Name))) passed where
    the callee's declaration takes a host function pointer (CB3FlashText2dObject::Prepare 0x372010 ->
    PrepareCommonData__21CGt2dObjectItalicText...) becomes `reinterpret_cast<R (*)(Args)>(&Name)` on the lines GCC
    names: the callee's stub_arg turns the host function into its registered guest code address, the same value."""
    lines = text.split("\n")
    base = os.path.basename(path)
    changed = False
    for mm in re.finditer(r"([^\s:]+):(\d+):\d+: error: cannot convert ‘b3host::gfn<[^’]*>’ to ‘([^’]*\(\*\)\([^’]*\))’", stderr):
        if os.path.basename(mm.group(1)) != base:
            continue
        ln = int(mm.group(2)) - 1
        if not (0 <= ln < len(lines)):
            continue
        new = re.sub(r"b3host::gfn<[^;]*?>::from_raw\(b3host::icast<unsigned int>\(([A-Za-z_]\w*)\)\)",
                     lambda x: "reinterpret_cast<%s>(&%s)" % (mm.group(3), x.group(1)), lines[ln])
        if new != lines[ln]:
            lines[ln] = new
            changed = True
    return "\n".join(lines) if changed else text


PERMISSIVE_PI = re.compile(r"([^\s:]+):(\d+):(\d+): (?:warning|error): (?:invalid conversion from ‘([^’]*)’ to ‘([^’]*)’|"
                           r"cast from ‘([^’]*)’ to ‘([^’]*)’ loses precision) \[-fpermissive\]")
INT_TYPE_RX = r"(?:const |volatile )*(?:(?:unsigned|signed) )?(?:char|short|int|long|long long|long int|short int|b3host::ps2_u?long|RwU?Int(?:8|16|32))$|unsigned$"


def permissive_conversions(stderr, base):
    """gen1: the pointer <-> integer conversions -fpermissive accepted in a unit: (line, col, from, to, kind) with kind
    'p2i' (a pointer where an integer is declared), 'cast' (a C cast of a pointer to a narrower integer), 'i2p'."""
    out = []
    for mm in PERMISSIVE_PI.finditer(stderr):
        if os.path.basename(mm.group(1)) != base:
            continue
        ln, col = int(mm.group(2)), int(mm.group(3))
        if mm.group(6):
            out.append((ln, col, mm.group(6), mm.group(7), "cast"))
            continue
        fr, to = mm.group(4), mm.group(5)
        fp, tp = fr.rstrip().endswith("*") or "(*)" in fr, to.rstrip().endswith("*") or "(*)" in to
        if fp and not tp and re.match(INT_TYPE_RX, to.strip()):
            out.append((ln, col, fr, to, "p2i"))
        elif tp and not fp and re.match(INT_TYPE_RX, fr.strip()):
            out.append((ln, col, fr, to, "i2p"))
    return out


def permissive_fix(text, convs):
    """gen1: each pointer <-> integer conversion -fpermissive let through, made explicit as on the EE (a pointer is its
    32-bit guest address):
      p2i  `D_004E2D50 = this;` (an int guest global) -> `= b3host::icast<int>(this);` (the guest address; the host
           conversion truncated a 64-bit host pointer: B3CoronaRendererPS2)
      cast `(EB3MenuFlowFSMEvents)(int)p2` with p2 a pointer (T9 missed the inner cast) -> `b3host::icast<int>(p2)`
      i2p  an integer where a host pointer is required: T9's `icast<unsigned int>(p)` (the decomp cast a pointer for
           its own `memcpy(unsigned int, ...)` declaration, GtFileSystemPS2DVD 0x1D3280) -> `(p)`; any other integer is
           a guest address -> `b3host::pcast<T>(...)`, the object there.
    Edits at the diagnostics' line / column; one the rules do not cover is left (the caller refuses the unit)."""
    lines = text.split("\n")
    for ln, col, fr, to, kind in sorted(convs, key=lambda c: (c[0], -c[1])):
        if not (0 < ln <= len(lines)):
            continue
        s = lines[ln - 1]
        c = col - 1
        ms = mask(s)

        def operand_start_at(i):
            """the start of the argument / right-hand side containing column i (GCC points at a binary operator)"""
            d = 0
            k = i - 1
            while k >= 0:
                ch = ms[k]
                if ch in ")]}":
                    d += 1
                elif ch in "([{":
                    if d == 0:
                        break
                    d -= 1
                elif d == 0 and (ch == "," or (ch == "=" and ms[k - 1:k] not in "=!<>" and ms[k + 1:k + 2] != "=")):
                    break
                k -= 1
            k += 1
            while k < i and ms[k].isspace():
                k += 1
            return k

        def operand_end_at(i):
            d = 0
            while i < len(ms):
                ch = ms[i]
                if ch in "([{":
                    d += 1
                elif ch in ")]}":
                    if d == 0:
                        return i
                    d -= 1
                elif ch in ",;?:" and d == 0:
                    return i
                i += 1
            return i
        if kind == "p2i":
            c = operand_start_at(c)
            e = operand_end_at(c)
            expr = s[c:e].rstrip()
            if expr:
                lines[ln - 1] = s[:c] + "b3host::icast<%s>(%s)" % (to.strip(), expr) + s[c + len(expr):]
        elif kind == "cast":
            cm = re.match(r"\(\s*([^()]*?)\s*\)", s[c:])
            if cm:
                j = c + cm.end()
                om = re.match(r"\s*([A-Za-z_]\w*(?:\s*(?:->|\.)\s*[A-Za-z_]\w*)*|\((?:[^()]|\([^()]*\))*\))", s[j:])
                if om:
                    lines[ln - 1] = s[:c] + "b3host::icast<%s>(%s)" % (cm.group(1), om.group(1).strip()) + s[j + om.end():]
        elif kind == "i2p":
            pre = s[:c]
            im = re.search(r"b3host::icast<[^<>]*>\s*$", pre)
            if im and s[c:c + 1] == "(":
                k, d = c, 0
                while k < len(s):
                    d += (s[k] == "(") - (s[k] == ")")
                    if d == 0:
                        break
                    k += 1
                lines[ln - 1] = s[:im.start()] + s[c:k + 1] + s[k + 1:]
            else:
                c = operand_start_at(c)
                e = operand_end_at(c)
                expr = s[c:e].rstrip()
                pointee = re.sub(r"\s*\*\s*$", "", to.strip())
                if expr and "(*)" not in to:
                    lines[ln - 1] = s[:c] + "b3host::pcast<%s>(%s)" % (pointee, expr) + s[c + len(expr):]
    return "\n".join(lines)


_GXX_VERSION = []


def gxx_version():
    """gen1 (speed): `g++ --version`, run once per generation (it was once per unit, for the cache keys)."""
    if not _GXX_VERSION:
        _GXX_VERSION.append(subprocess.run(["g++", "--version"], capture_output=True, text=True).stdout)
    return _GXX_VERSION[0]


def compiles_cached(path):
    """gen1 (speed): whether the unit at `path` compiles for the host (host_compile, its object removed), cached under
    DECOMP2HOST_CACHE by the same key as compile_for_stubs' (the text, every visible header, the compiler, the flags):
    call_stubs' T31 (c) check compiled every rewritten unit again on every generation. Both outcomes are cached (the
    key holds everything the compile reads)."""
    cache = os.environ.get("DECOMP2HOST_CACHE")
    key = None
    if cache:
        import hashlib
        h = hashlib.sha256(open(path, "rb").read())
        h.update(_headers_digest(os.path.join(os.path.dirname(os.path.dirname(path)), "include")).encode())
        h.update(gxx_version().encode())
        h.update(repr(HOST_COMPILE_FLAGS).encode())
        key = os.path.join(cache, h.hexdigest() + ".ok")
        try:
            return open(key).read() == "1"
        except OSError:
            pass
    _, rc = host_compile(path)
    try:
        os.remove(path + ".o")
    except OSError:
        pass
    ok = rc.returncode == 0
    if key:
        tmp = "%s.%d.tmp" % (key, os.getpid())
        open(tmp, "w").write("1" if ok else "0")
        os.replace(tmp, key)
    return ok


def compile_for_stubs(path):
    """Phase 1 of the call stubs: the unit compiled for the host once; (object, undefined, strongly defined).
    boot2: with DECOMP2HOST_CACHE=<dir> the symbol lists are cached by the unit's text, every visible header and the
    compiler (a regeneration that changes a few units compiles only those); a unit that does not compile is never
    cached."""
    cache = os.environ.get("DECOMP2HOST_CACHE")
    key = None
    if cache:
        import hashlib
        import json
        h = hashlib.sha256(open(path, "rb").read())
        h.update(_headers_digest(os.path.join(os.path.dirname(os.path.dirname(path)), "include")).encode())
        h.update(gxx_version().encode())
        h.update(repr(HOST_COMPILE_FLAGS).encode())   # what the compile depends on besides the text and headers
        # gen1: and the compiler-guided rewrites' version -- a cached entry holds the unit as they left it, so a new
        # rule (T38) must not be skipped by a hit on the unchanged text; bump COMPILE_FIX_VERSION with each new rule
        h.update(COMPILE_FIX_VERSION.encode())
        key = os.path.join(cache, h.hexdigest() + ".json")
        if os.path.exists(key):
            d = json.load(open(key))
            if d.get("text") is not None:   # gen1: the unit as the crossed-declaration fix left it
                open(path, "w").write(d["text"])
            return path + ".o", [tuple(x) for x in d["undefined"]], set(d["defined"]) if isinstance(d["defined"], list) else d["defined"]
    obj, r = host_compile(path)
    fixed = None
    ref_err = re.compile(r"cannot bind non-const lvalue reference of type ‘[^’]*\*&’")
    tried_t35 = False
    # gen1 (T7): a unit's namespace copy of a header name (b3gen_X::CGtTexture, b3gen_X::Gt2dRenderer) in an error
    # gen1: also an ambiguity or a parse error in a template argument list (a wrapper's `a3.cast<CGtTexture>()` after
    # `using namespace b3gen_U;`, with the unit's own `struct CGtTexture;` beside Gt2dObject.h's: _DrawBezierSpline 0x41DF90)
    t7_err = re.compile(r"(?:no matching function for call to|cannot convert|is not a member of)[^\n]*‘b3gen_\w+::|"
                        r"no matching function for call to ‘[^’]*\(([^’]*b3gen_\w+::)|"
                        r"error: reference to ‘\w+’ is ambiguous|error: parse error in template argument list")
    tried_t7 = False
    fn_err = re.compile(r"error: cannot convert ‘b3host::gfn<[^’]*>’ to ‘[^’]*\(\*\)\([^’]*\)’")
    tried_fn = False
    cptr_err = re.compile(r"error: cannot convert ‘b3host::gp<[^’]*>\*’ to ‘[^’]*\* const\*’")
    tried_cptr = False
    nsq_err = NSQ_ERR
    tried_nsq = False
    for _ in range(5):
        if not r.returncode:
            break
        t0 = open(path).read()
        if "crosses initialization of" in r.stderr or "skips initialization of" in r.stderr:
            t1 = crossed_declaration_fix(t0, path, r.stderr)
            if t1 == t0:
                t1 = strict_fix(t0, path, r.stderr)   # gen1: a crossed GtMath value local -> the function's start
            if t1 != t0:
                open(path, "w").write(t1)
                fixed = t1
                obj, r = host_compile(path)
                continue
        if nsq_err.search(r.stderr) and not tried_nsq:
            # gen1 (T7): a helper of the unit's namespace named from a member function of a header-declared class
            # (CB3AIArbitrator::CalculateSpeedMatch 0x19D4D0 and func_0019D4D0_inl2): lookup goes to the class's
            # namespace, so the call is qualified with the unit's on the lines GCC names
            tried_nsq = True
            t1 = ns_qualify_fix(t0, path, r.stderr)
            if t1 != t0:
                open(path, "w").write(t1)
                obj, r2 = host_compile(path)
                if nsq_err.search(r2.stderr):
                    open(path, "w").write(t0)
                else:
                    fixed, r = t1, r2
                    continue
        if cptr_err.search(r.stderr) and not tried_cptr:
            tried_cptr = True
            t1 = const_ptr_ptr_fix(t0, path, r.stderr)
            if t1 != t0:
                open(path, "w").write(t1)
                obj, r2 = host_compile(path)
                if cptr_err.search(r2.stderr):
                    open(path, "w").write(t0)
                else:
                    fixed, r = t1, r2
                    continue
        if fn_err.search(r.stderr) and not tried_fn:
            tried_fn = True
            t1 = host_fnptr_fix(t0, path, r.stderr)
            if t1 != t0:
                open(path, "w").write(t1)
                obj, r2 = host_compile(path)
                if fn_err.search(r2.stderr):
                    open(path, "w").write(t0)
                else:
                    fixed, r = t1, r2
                    continue
        if t7_err.search(r.stderr) and not tried_t7 and DECOMP_ROOT[0]:
            tried_t7 = True
            t1 = t7_header_names(t0, DECOMP_ROOT[0])
            if t1 != t0:
                open(path, "w").write(t1)
                obj, r2 = host_compile(path)
                if t7_err.search(r2.stderr) and r2.returncode:
                    open(path, "w").write(t0)
                else:
                    fixed, r = t1, r2
                    continue
        if ref_err.search(r.stderr) and not tried_t35:
            tried_t35 = True
            t1 = ref_pointer_transforms(t0)
            if t1 != t0:
                open(path, "w").write(t1)
                obj, r2 = host_compile(path)
                if ref_err.search(r2.stderr):
                    open(path, "w").write(t0)   # T35 did not help: the unit as it was, with its own error
                else:
                    fixed, r = t1, r2
                    continue
        break
    if r.returncode:
        err = [l for l in r.stderr.split("\n") if "error" in l]
        die("%s: does not compile for the host: %s" % (os.path.basename(path), (err[0] if err else r.stderr[:300])[:300]))
    # gen1: a pointer <-> integer conversion that only -fpermissive accepts passes a truncated 64-bit host pointer, or an
    # integer as a host pointer (boot2's RwaObjIntGetParamPointer class): a hard error in a port-track unit (fix the
    # source); in a decomp unit made explicit (permissive_fix), else the unit is refused
    base = os.path.basename(path)
    convs = permissive_conversions(r.stderr, base)
    if convs:
        where = ", ".join("%d:%d %s -> %s" % (c[0], c[1], c[2], c[3]) for c in convs[:6])
        if any(base.startswith(tr + "_") for tr in PORT_TRACKS):
            die("%s: pointer <-> integer conversions only -fpermissive accepts (port track: fix the source): %s" % (base, where))
        t0 = open(path).read()
        t1 = permissive_fix(t0, convs)
        open(path, "w").write(t1)
        obj, r2 = host_compile(path)
        left = permissive_conversions(r2.stderr, base) if not r2.returncode else convs
        if r2.returncode or left:
            open(path, "w").write(t0)
            die("%s: pointer <-> integer conversions only -fpermissive accepts, not made explicit: %s" % (
                base, ", ".join("%d:%d %s -> %s" % (c[0], c[1], c[2], c[3]) for c in left[:6])))
        fixed, r = t1, r2
        sys.stderr.write("decomp2host: %s: %d pointer <-> integer conversion(s) made explicit\n" % (base, len(convs)))
    # gen1 T38: what only -fpermissive accepts (clang refuses it): rewritten while the unit keeps compiling and the
    # diagnostics get fewer; else the unit stays as it was (it still builds with GCC)
    sd = strict_diagnostics(r.stderr, base)
    for _ in range(3 if sd else 0):
        t0 = open(path).read()
        t1 = strict_fix(t0, path, r.stderr)
        if t1 == t0:
            break
        open(path, "w").write(t1)
        obj2, r2 = host_compile(path)
        sd2 = strict_diagnostics(r2.stderr, base) if not r2.returncode else sd
        if r2.returncode or len(sd2) >= len(sd):
            open(path, "w").write(t0)
            obj, r = host_compile(path)
            break
        obj, fixed, r, sd = obj2, t1, r2, sd2
        sys.stderr.write("decomp2host: %s: T38 rewrote what only -fpermissive accepts (%d left)\n" % (base, len(sd)))
    und, dfn = undefined_symbols(obj), defined_symbols(obj)
    if key:
        import json
        os.makedirs(cache, exist_ok=True)
        tmp = key + ".%d.tmp" % os.getpid()
        json.dump({"undefined": [list(x) for x in und], "defined": sorted(dfn) if isinstance(dfn, (set, frozenset)) else dfn,
                   **({"text": fixed} if fixed is not None else {})}, open(tmp, "w"))
        os.replace(tmp, key)
    return obj, und, dfn


SEQ_KEYWORDS = {"if", "while", "for", "switch", "return", "sizeof", "decltype", "alignof", "static_assert", "catch"}
SEQ_STMT_HEAD = re.compile(r"^(\s*(?:(?:case\b[^;{}?]*?|default\s*|[A-Za-z_]\w*\s*):(?!:)\s*)*(?:else\s+)?)(return\s+)?$")


def guest_arg_order_transforms(text, guest_names, report, where):
    """T17 (port13): C++ leaves the evaluation order of call arguments unspecified; MWCC evaluates the arguments that
    contain calls first, left to right, then the plain ones (retail func_00310200 at 0x3102C4 / 0x3102D4: jal
    func_0030CEC0, then jal func_0030CE80, then the D_01E900E0 index load for an earlier plain argument), while
    x86-64 GCC evaluated them right to left there (port12-b: ours called 0x30CE80 first). The order of guest
    calls is observable (each reaches the guest timeline in order: kernel calls, stores), so a call
    with two or more arguments that contain guest calls is rewritten at statement level: every argument that contains
    a call is bound in order (`auto&& __b3_seqN_K = (arg);`, forwarded unchanged into the call), the plain ones stay in
    place. Such a call anywhere else (a condition, an initialiser, a ternary, nested) stops the generation (loud)."""
    if not guest_names:
        return text
    m = preproc_mask(mask(text))
    guest_re = re.compile(r"\b(?:%s)\s*\(" % "|".join(re.escape(x) for x in sorted(guest_names)))
    call_re = re.compile(r"\b([A-Za-z_]\w*)\s*\(")

    # gen1 (speed): every opener's partner from one pass (a stack popped by any closer is the depth count the old
    # per-call scan did), and the guest-call starts found once: an argument holds a guest call exactly when one of
    # these starts lies inside it (a match never crosses the `,` / `)` that bounds an argument)
    pair, stack = {}, []
    for bm in re.finditer(r"[()\[\]{}]", m):
        if bm.group() in "([{":
            stack.append(bm.start())
        elif stack:
            pair[stack.pop()] = bm.start()
    gstarts = [g.start() for g in guest_re.finditer(m)]
    import bisect

    def n_guest(s, e):
        return bisect.bisect_left(gstarts, e) - bisect.bisect_left(gstarts, s)

    def close(i):
        if i in pair:
            return pair[i]
        die("%s: unbalanced parentheses" % where)

    def args_of(o, c):
        out, d, start = [], 0, o + 1
        for k in range(o + 1, c):
            if m[k] in "([{":
                d += 1
            elif m[k] in ")]}":
                d -= 1
            elif m[k] == "," and d == 0:
                out.append((start, k))
                start = k + 1
        out.append((start, c))
        return out

    def has_call(s, e):
        return any(x.group(1) not in SEQ_KEYWORDS for x in call_re.finditer(m, s, e))

    edits, n = [], 0
    for cm in call_re.finditer(m):
        if cm.group(1) in SEQ_KEYWORDS:
            continue
        o = cm.end() - 1
        c = close(o)
        if n_guest(o + 1, c) < 2:
            continue   # fewer than two guest calls inside: fewer than two arguments with one
        args = args_of(o, c)
        if sum(1 for s, t in args if n_guest(s, t)) < 2:
            continue
        b = max(m.rfind(";", 0, cm.start()), m.rfind("{", 0, cm.start()), m.rfind("}", 0, cm.start())) + 1
        e = c + 1
        while e < len(m) and m[e].isspace():
            e += 1
        hm = SEQ_STMT_HEAD.match(m[b:cm.start()])
        if not hm or e >= len(m) or m[e] != ";":
            die("%s: %s(...) has two or more arguments with guest calls, not at statement level (T17: sequence it by "
                "hand)" % (where, cm.group(1)))
        if any(s0 <= e and b < e0 for s0, e0, _ in edits):
            die("%s: nested calls with guest-call arguments (T17)" % where)
        prefix, ret = text[b:b + len(hm.group(1))], ("return " if hm.group(2) else "")
        binds, parts = [], []
        for k, (s, t) in enumerate(args):
            if has_call(s, t):
                v = "__b3_seq%d_%d" % (n, k)
                binds.append("auto&& %s = (%s);" % (v, text[s:t].strip()))
                parts.append("static_cast<decltype(%s)&&>(%s)" % (v, v))
            else:
                parts.append(text[s:t].strip())
        edits.append((b, e + 1, "%s{ /* decomp2host T17: call arguments in MWCC's order */ %s %s%s(%s); }" % (
            prefix, " ".join(binds), ret, text[cm.start():o], ", ".join(parts))))
        n += 1
    if n:
        report.append("T17 guest-call argument order %d" % n)
    return apply(text, edits) if edits else text


def destructor_stub(head, addr, name):
    """gen1: the stub of a destructor the unit declares but does not define: retail's __dt__<class>Fv with MWCC's
    hidden flag a1 = -1 (the object is not deleted) [C B3PropManager 0x2B7744]."""
    return "inline %s() { b3host::call_guest<void>(0x%08Xu, b3host::ptr_arg(this), -1); }   // %s" % (head, addr, name)


def place_guest_macros(text, macros, report):
    """The C names' `#define Name (b3host::guest_fn<decltype(Name), addr>::call)` (port7), after the last declaration
    outside any class / function body (boot1); gen1: after the first one, with later declarations wrapped in #undef /
    #define, when a call comes before the last declaration. Returns (text, number of macros)."""
    n_c = 0
    m = preproc_mask(mask(text))
    bodies = [(f[1], f[2]) for f in top_level_functions(text)]
    bodies += code_blocks(m)   # boot1: also class bodies (a call in an inline member is no declaration)
    edits = []
    incs = list(re.finditer(r"^[ \t]*#\s*include[^\n]*\n", text, re.M))
    for name, addr in sorted(macros.items()):
        at = None
        decls, first_use = [], None
        for dm in re.finditer(r"\b%s\s*\(" % re.escape(name), m):
            if any(a <= dm.start() <= b for a, b in bodies):
                if first_use is None:
                    first_use = dm.start()
                continue
            e = m.find(";", dm.end())
            if e > 0:
                at = e + 1
                decls.append((dm.start(), e + 1))
        if at is None:
            at = incs[-1].end() if incs else 0
        define = "\n#define %s (b3host::guest_fn<decltype(%s), 0x%08Xu>::call)   // decomp2host: guest call\n" % (name, name, addr)
        if first_use is not None and decls and first_use < decls[-1][0]:
            # gen1: a call before the last declaration (a class body's inline member, an earlier function:
            # rw_texcache.cpp's _rwDMAOpenVIFPkt) linked the C symbol directly. The macro follows the first
            # declaration; every later declaration is wrapped in #undef / #define so it still declares the function
            at = decls[0][1]
            for ds, de in decls[1:]:
                st_ = max(m.rfind(";", 0, ds), m.rfind("}", 0, ds), m.rfind("{", 0, ds)) + 1
                edits.append((st_, st_, "\n#undef %s   // decomp2host (gen1): a later declaration\n" % name))
                edits.append((de, de, define))
            report.append("guest call %s: macro after its first declaration (a call before the last)" % name)
        edits.append((at, at, define))
        n_c += 1
    return apply(text, edits), n_c


def call_stubs(path, decomp, report, compiled=None, defined_elsewhere=frozenset()):
    """Calls from generated code to other game functions (docs/decomp-on-host.md, "Calls"). The unit is compiled for
    the host once; every undefined symbol that is a game function gets a definition that calls the guest function
    through b3host::call_guest, i.e. its native override when one is enabled, else the original in the backend:
      - C++ members and functions (GCC-demangled): `inline auto Q::Name(T0 p0, ...) const -> decltype(Name(p0, ...))
        { return b3host::call_guest<...>(addr, b3host::ptr_arg(this), b3host::stub_arg<decltype(p0)>(p0), ...); }`
        (the trailing decltype gives the declared return type and picks the overload); the address from the MWCC
        mangling of the same signature (symbol tables), or for a T13 method a call through the guest vtable;
      - `extern "C"` names (the decomp declares most callees under their MWCC symbol: `extern "C" void
        Foo__8CB3ScoreFi(CB3Score *, int);`): `#define Foo__8CB3ScoreFi (b3host::guest_fn<decltype(Foo__8CB3ScoreFi),
        addr>::call)` after its last declaration (the parameter types stay the declared ones).
    A game symbol with no address, or data, stops the generation (loud). A C++ symbol that another selected unit
    defines (a member of a class from a shared decomp header, so not in a unit namespace) gets no stub: the call
    links to that native definition directly (an inline stub next to a strong definition is a multiple definition on
    PE/COFF; port7). Such a direct call bypasses the NativeOverride switch (docs/decomp-on-host.md, "Calls").
    A C name another selected unit defines gets the guest_fn macro all the same (port8): C linkage carries no
    parameter types, and the decomp's units declare one callee differently (func_00240A60 declares `func_00240E90()`,
    whose definition takes (self, p0): MWCC passes a0/a1 through untouched; func_0021B4E0 passes an `int` where the
    definition takes a pointer). Linked directly, the host call passes nothing or a 32-bit int where a 64-bit host
    pointer is read (port7: 29 checker crashes). Through the guest ABI each side marshals by its own declaration, as
    MWCC does, and dispatch still runs the native override."""
    obj, undefined, _ = compiled if compiled is not None else compile_for_stubs(path)
    text = open(path).read()
    stubs, macros, n_c, n_cpp, n_v, n_direct = [], {}, 0, 0, 0, 0
    stub_byval = set()   # gen1: struct types stubs take by value
    port_unit = any(os.path.basename(path).startswith(t + "_") for t in PORT_TRACKS)
    for sym, dem in undefined:
        if HOST_SYM.match(sym):
            continue
        # the hardware-hook decision (2026-11-05): b3hw_* are host functions (include/b3hw/hw.h) that port-track
        # C calls where the asm touches hardware; they link to the host implementation. Refused in decomp units.
        if dem.startswith("b3hw_"):
            # boot2 T37: a decomp unit's own hardware access (`*(volatile T *)addr` -> b3host::mmio<T>) reaches them
            # through gp.h's mmio_ref; any other b3hw_ use stays refused there
            if not port_unit and not (dem.startswith(("b3hw_read", "b3hw_write")) and "b3host::mmio<" in text):
                die("%s: calls %s: b3hw_ hardware hooks are for port-track C only" % (os.path.basename(path), dem))
            continue
        if sym in defined_elsewhere and ("(" in dem or function_address(decomp, dem) is None):
            n_direct += 1
            continue
        if "(" not in dem:   # a C name (also one another selected unit defines: port8, see above)
            if dem in HOST_LIBC:
                continue
            if dem in LIBM_NAMES and not re.search(r'extern\s*"C"[^;{}]*\b%s\s*\(' % re.escape(dem), mask(text)):
                continue
            addr = function_address(decomp, dem)
            if addr is None:
                die("%s: calls %s, which has no guest address (not func_XXXXXXXX, not in the decomp's tables)" % (
                    os.path.basename(path), dem))
            macros[dem] = addr
            continue
        mg = None
        mm = re.match(r"^(.*?)\((.*)\)\s*(const)?$", dem)
        head, params, const = mm.group(1), split_top(mm.group(2)), bool(mm.group(3))
        ptypes = [portable_type(x) for x in params]
        stub_byval.update(x.strip() for x in ptypes if re.fullmatch(r"\s*[A-Za-z_][\w:]*\s*", x) and
                          x.strip() not in SCALAR_SIZES and not re.match(r"\s*(?:b3host::|GtMathPs2::|u_long128)", x))
        hp = [p for p in head.split("::")]
        name = hp[-1]
        qual = "::".join(hp[:-1])
        cls_simple = strip_ns(qual).split("::")[-1] if qual and strip_ns(qual) else None
        pnames = ["p%d" % k for k in range(len([x for x in params if x != "void"]))]
        decls = ", ".join(param_decl(t, n) for t, n in zip([x for x in ptypes if x != "void"], pnames))
        args = ["b3host::stub_arg<decltype(%s)>(%s)" % (n, n) for n in pnames]
        if not qual or not strip_ns(qual):
            # a free C++ function
            addr = function_address(decomp, name)
            if addr is None:
                mg = mw_mangle(None, name, params, False)
                addr = function_address(decomp, mg) if mg else None
            if addr is None:
                die("%s: calls %s: no guest address" % (os.path.basename(path), dem))
            ret = "decltype(%s(%s))" % (name, ", ".join(pnames))
            stubs.append("inline auto %s(%s) -> %s { return b3host::call_guest<%s>(0x%08Xu%s); }   // %s" % (
                head, decls, ret, ret, addr, "".join(", " + a for a in args), mg or name))
            stubs.append(code_registration(head, [x for x in ptypes if x != "void"], addr, len(stubs)))
            n_cpp += 1
            continue
        # gen1: an assignment operator the unit declares but does not define (func_003CD920's GtMathPs2::CGtV2d view:
        # MWCC calls retail's out-of-line __as__Q29GtMathPs26CGtV2dFRCQ29GtMathPs26CGtV2d 0x1D4E90) is an ordinary
        # member stub (MWCC's __as__ name; a reference result is the object's guest address in v0)
        if name.startswith("~") and not [x for x in params if x != "void"]:
            # gen1: a destructor the unit declares but does not define (B3PropManager_002B7300's local CGtLinearMalloc):
            # retail's __dt__<class>Fv with MWCC's hidden flag a1 = -1 for an object that is not deleted [C caller
            # 0x2B7744: `addiu $a1, $zero, -0x1` in the jal's delay slot; the destructor reads it, 0x2F9DE8]
            c = mw_class(qual)
            addr = function_address(decomp, "__dt__%sFv" % c) if c else None
            if addr is None:
                die("%s: calls %s: no guest address (MWCC name __dt__%sFv not in the decomp's tables)" % (os.path.basename(path), dem, c))
            stubs.append(destructor_stub(head, addr, decomp.funcs[addr][0]))
            n_cpp += 1
            continue
        if name.startswith("~") or (name.startswith("operator") and name.replace(" ", "") != "operator="):
            die("%s: calls %s (destructor/operator stubs not supported)" % (os.path.basename(path), dem))
        is_ctor = name == cls_simple
        static = (not is_ctor) and (is_static_member(strip_ns(qual), name, text) or is_namespace(strip_ns(qual), text, name))
        this = [] if static else ["b3host::ptr_arg(this)"]
        if (cls_simple, name) in VIRTUALS and not static:
            slot = VIRTUALS[(cls_simple, name)]
            ret = "decltype(%s(%s))" % (name, ", ".join(pnames))
            stubs.append("inline auto %s(%s)%s -> %s { return b3host::vcall_this<%s>(this, offsetof(%s, b3_vptr), 0x%Xu%s); }"
                         "   // T13 virtual, slot 0x%X" % (head, decls, " const" if const else "", ret, ret, qual, slot,
                                                            "".join(", " + a for a in args), slot))
            n_v += 1
            continue
        mg = mw_mangle(strip_ns(qual), name, params, const)
        addr = function_address(decomp, mg) if mg else None
        if addr is None:
            # by name alone when unambiguous (Name__<class>[C]F...)
            c = mw_class(qual)
            pref = "%s__%s" % ("__ct" if is_ctor else name, c) if not is_ctor else "__ct__%sF" % c
            hits = sorted({a for n, a in decomp.by_name.items() if n.startswith(pref)})
            if len(hits) == 1:
                addr = hits[0]
                report.append("call %s: address by name only (%s)" % (dem, decomp.funcs[addr][0]))
        if addr is None:
            die("%s: calls %s: no guest address (MWCC name %s not in the decomp's tables)" % (os.path.basename(path), dem, mg))
        if is_ctor:
            stubs.append("inline %s(%s) { b3host::call_guest<void>(0x%08Xu%s); }   // %s" % (
                head, decls, addr, "".join(", " + a for a in this + args), decomp.funcs[addr][0]))
        else:
            ret = "decltype(%s(%s))" % (name, ", ".join(pnames))
            stubs.append("inline auto %s(%s)%s -> %s { return b3host::call_guest<%s>(0x%08Xu%s); }   // %s" % (
                head, decls, " const" if const else "", ret, ret, addr, "".join(", " + a for a in this + args),
                decomp.funcs[addr][0]))
            if static:
                stubs.append(code_registration(head, [x for x in ptypes if x != "void"], addr, len(stubs)))
        n_cpp += 1
    if macros:
        text, n_m = place_guest_macros(text, macros, report)
        n_c += n_m
        text = guest_arg_order_transforms(text, set(macros), report, os.path.basename(path))
    # gen1 (T7): a namespace the unit opens beside an included header's (t7_header_names) also exists inside the unit's
    # namespace, so at global scope, where the stubs are defined, `X::f` is ambiguous: the header's is `::X`
    for nx in re.findall(r"decomp2host T7 \(gen1\): the headers' ([A-Za-z_]\w*) too", text):
        stubs = [re.sub(r"(?<![\w:])%s::" % re.escape(nx), "::%s::" % nx, s_) for s_ in stubs]
    if stubs:
        # gen1: a 4-byte struct a stub passes by value (CB3Globe2dObject::DrawGlobeHalo's RwRGBA, 0x3B2EE0) needs the
        # lone-float decision call.h asks for, as wrappers' by_value types get it; only for scalar-only bodies of
        # exactly 4 bytes that the unit does not specialise already
        specs = []
        for bt in sorted(stub_byval):
            simple = bt.split("::")[-1]
            if re.search(r"lone_float<[^<>]*\b%s\s*>" % re.escape(simple), text):
                continue
            if small_struct_size(bt, [text] + HEADER_TEXT) == 4:
                specs += lone_float_specialisations({bt}, [text] + HEADER_TEXT)
        text += ("\n// ---- decomp2host: calls to other game functions (b3host::call_guest: native override if enabled, "
                 "else the original)\n#include <cstddef>\n#include <utility>\n" + "".join(s + "\n" for s in specs) +
                 "\n".join(stubs) + "\n")
    # 1.1.1 T41: an axis-aligned box written as two adjacent locals (`u_long128 b0 = min; u_long128 b1 = max;`) whose
    # first's address goes to a CGtAxisAlignedBox parameter: MWCC put them next to each other; a host compiler need not
    # (mingw-w64 did not: crash-mode pickups were never collected on Windows, Update__14CB3CrashPickupFf). The two
    # become one array, so max always follows min; the names stay (references), every use reads as before.
    text, n_t41 = t41_box_pairs(text)
    if n_t41:
        sys.stderr.write("decomp2host: T41 %s: %d box pair(s) made one array\n" % (os.path.basename(path), n_t41))
    # 1.2.0 T46: a local array the draft declares smaller than retail's frame (T46_LOCAL_ARRAYS)
    text, n_t46 = t46_local_arrays(text, report)
    if n_t46:
        sys.stderr.write("decomp2host: T46 %s: %d local array(s) sized as retail's frame\n" % (os.path.basename(path), n_t46))
    # 1.2.1 T47: a draft's statement written as retail has it (T47_DRAFT_FIXES)
    text, n_t47 = t47_draft_fixes(text, report)
    if n_t47:
        sys.stderr.write("decomp2host: T47 %s: %d statement(s) as retail\n" % (os.path.basename(path), n_t47))
    if (macros or stubs) and os.environ.get("DECOMP2HOST_T31C") == "1":
        # boot2 E1 (c): a local aggregate reaching a guest call (the bounce window covers max(256, sizeof(pointee))
        # from the pointer): the whole object onto the guest stack (T31); one count line per unit on stderr
        gnames = set(macros) | {re.search(r"(\w+)\s*\($", mm.group(1)).group(1)
                                for mm in (re.match(r"inline auto ([\w:~]+\s*\()", st) for st in stubs) if mm}
        found = escape_lint(text, guest_names=gnames, only_c=True)
        if found:
            original = text
            text = escape_fix(text, [(h[2], h[3], h[4]) for h in found])
            left = escape_lint(text, guest_names=gnames, only_c=True)
            why = {}
            if text != original:
                # the rewritten unit must compile (a reference crossed by a case label, a declaration in a macro
                # argument ...): else the unit keeps its original text and every site counts as left ("compile")
                open(path, "w").write(text)
                if not compiles_cached(path):
                    # gen1 (T31 audit): site by site instead of all or nothing -- each rewrite kept if the unit still
                    # compiles with it (one site that cannot be rewritten no longer keeps the unit's others on the host)
                    text = original
                    n_bad = 0
                    tried = set()
                    while True:
                        # offsets move with every kept rewrite: the sites are re-found on the current text each time
                        cur = [h for h in escape_lint(text, guest_names=gnames, only_c=True) if (h[0], h[1], h[2]) not in tried]
                        if not cur:
                            break
                        h = cur[0]
                        tried.add((h[0], h[1], h[2]))
                        cand = escape_fix(text, [(h[2], h[3], h[4])])
                        if cand == text:
                            continue
                        open(path, "w").write(cand)
                        if compiles_cached(path):
                            text = cand
                        else:
                            n_bad += 1
                    open(path, "w").write(text)
                    left = escape_lint(text, guest_names=gnames, only_c=True)
                    if n_bad:
                        why["compile"] = n_bad
            for h in ([] if "compile" in why and not left else left):
                ms = re.search(r"\b%s\b" % re.escape(h[2]), h[0])
                reason = "parameter" if ms else "declaration form"
                why[reason] = why.get(reason, 0) + 1
            sys.stderr.write("decomp2host: E1c %s: found %d, rewritten %d, left %d %s\n" % (
                os.path.basename(path), len(found), len(found) - len(left), len(left),
                " ".join("%s=%d" % kv for kv in sorted(why.items()))))
            for h in left:
                sys.stderr.write("decomp2host: E1c-left %s: %s (%s) in %s\n" % (os.path.basename(path), h[2], h[1], h[0]))
    # 1.2.0: a T41 / T46 (1.2.1: T47) rewrite is kept in a unit with no stub or macro too (func_002B7DB0 calls nothing:
    # its T46 was dropped)
    if macros or stubs or n_t41 or n_t46 or n_t47:
        open(path, "w").write(text)
    if macros or stubs:
        report.append("calls: %d extern C, %d C++, %d virtual" % (n_c, n_cpp, n_v))
    if n_direct:
        report.append("calls: %d direct to other selected units" % n_direct)
    for f in (obj,):
        try:
            os.remove(f)
        except OSError:
            pass
    return n_c + n_cpp + n_v + n_direct


# The decomp's comments name its maintainers' notes file (an upper-case .md at the decomp's root); the generated text drops
# that reference (comments only: no generated code changes).
NOTES_NAME = "".join(chr(c) for c in (67, 76, 65, 85, 68, 69)) + ".md"
NOTES_REF = re.compile(r" ?\(" + re.escape(NOTES_NAME) + r"\)")


def scrub_notes_refs(out_dir):
    for dirpath, _dirs, files in os.walk(out_dir):
        for fn in files:
            if not fn.endswith((".cpp", ".h", ".inc")):
                continue
            path = os.path.join(dirpath, fn)
            try:
                text = open(path, errors="surrogateescape").read()
            except OSError:
                continue
            if NOTES_NAME not in text:
                continue
            open(path, "w", errors="surrogateescape").write(NOTES_REF.sub("", text).replace(NOTES_NAME, "the decomp's notes"))


def main():
    _phase('main')
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--select", required=True)
    ap.add_argument("--keep-going", action="store_true", help="leave failing units out (OUT/failed.tsv)")
    ap.add_argument("--objects", default="", help="comma-separated object dirs whose <unit>.o files for units not "
                    "generated by this run are removed (OUT/units.txt)")
    a = ap.parse_args()
    decomp = Decomp(a.decomp)
    sel = []
    for line in open(a.select):
        line = line.split("#")[0].strip()
        if not line:
            continue
        p = line.split()
        sel.append((int(p[0], 16), p[1] if len(p) > 1 else "matched"))
    n_fn, n_units = generate(decomp, sel, a, keep_going=a.keep_going)
    print("decomp2host: %d functions from %d units -> %s" % (n_fn, n_units, a.out))


class Args:
    def __init__(self, decomp, out):
        self.decomp, self.out = decomp, out


# pic1 (docs/native-renderer.md §32.3, boot2's option A): game functions whose definitions report their own entry to
# b3host's watch hook (b3host/watch.h), because some generated unit links to them directly and so bypasses
# traced_entry. One guest address a line ("#" comments); DECOMP2HOST_WATCH=<file> instead of the default. An absent or
# empty list changes nothing in the output.
WATCH_LIST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../config/native_watch.txt")


def load_address_list(path):
    if not os.path.exists(path):
        return set()
    out = set()
    for line in open(path):
        line = line.split("#")[0].strip()
        if line:
            out.add(int(line.split()[0], 16))
    return out


def load_watch():
    return load_address_list(os.environ.get("DECOMP2HOST_WATCH") or WATCH_LIST)


# mod kit Phase 2a (docs/modding/hooks.md): game functions whose definitions route a call that did not come through
# dispatch (a direct C++ link from another generated unit) through dispatch while a hook is on them, so the hook sees
# every call (b3host/hook_route.h). One guest address a line ("#" comments); DECOMP2HOST_MOD_HOOKS=<file> instead of
# the default. An absent or empty list changes nothing in the output.
MOD_HOOKS_LIST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../config/mod_hooks.txt")


def load_mod_hooks():
    return load_address_list(os.environ.get("DECOMP2HOST_MOD_HOOKS") or MOD_HOOKS_LIST)


def watch_prologues(out_text, fns, tfns, watch, report, routes=frozenset()):
    """pic1: `b3host::watch_self<ADDR>(this, args...);` as the first statement of each watched definition in this unit
    (an observer: it reads the arguments, never writes guest memory, adds no cost), and the include it needs. Arguments
    are passed as the EE holds them: pointers, references (their address), integers, enums, floats; a struct by value
    is refused (the observer would need a guest copy). Returns (text, addresses).
    Mod kit 2a: for an address in `routes` (config/mod_hooks.txt) the definition first gets the route check of
    b3host/hook_route.h (`if (b3host::route_call<ADDR>(scope)) return b3host::call_guest<R>(ADDR, this, args...);`),
    ahead of any watch scope (so a routed call is reported once, on its dispatched entry); the same argument rules,
    and the definition must have a return type (no constructor / destructor). Routed addresses are not in the result
    (it lists the watched ones, for native_watch.h)."""
    edits, done, routed = [], [], []
    for addr, track, name, size in fns:
        if addr not in watch and addr not in routes:
            continue
        what = "native_watch" if addr in watch else "mod_hooks"
        tf = [x for x in tfns if x[3] == addr]
        if not tf:
            die("%08X %s: %s: definition not found" % (addr, name, what))
        _, brace, _, _, head = tf[0]
        h = re.sub(r"^extern\s*\"[^\"]*\"\s*", "", re.sub(r"\s+", " ", head))
        mm = re.match(r"^(.*?)\s*\b((?:[A-Za-z_]\w*::)*)([A-Za-z_]\w*)\s*\((.*)\)\s*(const)?\s*$", h)
        if not mm:
            die("%08X %s: %s: cannot parse the definition head: %s" % (addr, name, what, h))
        qual, fname, params = mm.group(2), mm.group(3), mm.group(4)
        cls = qual[:-2] if qual else None
        static = False
        if cls:
            simple = cls.split("::")[-1]
            for src in [out_text] + HEADER_TEXT:
                um = re.search(r"\b(class|struct)\s+%s\b[^;{]*\{" % re.escape(simple), src)
                if um:
                    body = src[um.end():match_brace(mask(src), um.end() - 1)]
                    static = re.search(r"\bstatic\b[^;{}]*\b%s\s*\(" % re.escape(fname), mask(body)) is not None
                    break
        args = ["this"] if cls and not static else []
        rargs = ["b3host::ptr_arg(this)"] if cls and not static else []
        for p in split_params(params):
            p = p.strip()
            if p in ("", "void"):
                continue
            pm = re.match(r"^(.*?[\s*&])([A-Za-z_]\w*)$", p)
            if not pm or pm.group(1).strip() in ("const", "unsigned", "signed", "struct", "class"):
                die("%08X %s: %s: unnamed parameter `%s`" % (addr, name, what, p))
            ptype, pname = pm.group(1).strip(), pm.group(2)
            rargs.append("b3host::stub_arg<decltype(%s)>(%s)" % (pname, pname))
            if ptype.endswith("&"):
                args.append("&" + pname)
            elif ptype.endswith("*"):
                args.append(pname)
            elif re.match(r"^(const )?(unsigned |signed )?(char|short|int|long|bool|float|ps2f|b3host::ps2f|b3host::ps2_long|"
                          r"b3host::ps2_ulong|u_int|u_short|u_char|u_long)$", ptype) or \
                    re.match(r"^(const )?(?:\w+::)*(?:E|UnkEnum_)\w+$", ptype) or re.match(r"^(const )?b3host::gp<.*>$", ptype):
                args.append(pname)
            else:
                die("%08X %s: %s: parameter `%s` is a struct by value (not %s)" % (
                    addr, name, what, p, "observed" if addr in watch else "routed"))
        text = ""
        if addr in routes:
            ret = re.sub(r"\b(inline|static|virtual|explicit|constexpr)\b\s*", "", mm.group(1)).strip()
            if not ret:
                die("%08X %s: mod_hooks: no return type (a constructor or destructor is not routed)" % (addr, name))
            text += ("\n    b3host::RouteScope b3_route_scope;   // decomp2host: config/mod_hooks.txt (mod kit 2a)"
                     "\n    if (b3host::route_call<0x%08Xu>(b3_route_scope)) return b3host::call_guest<%s>(0x%08Xu%s);"
                     % (addr, ret, addr, "".join(", " + x for x in rargs)))
            routed.append(addr)
        if addr in watch:
            text += ("\n    auto b3_watch_scope = b3host::watch_scope<0x%08Xu>(%s);   // decomp2host: config/native_watch.txt (pic1; boot2: entry and exit)"
                     % (addr, ", ".join(args)))
            done.append(addr)
        edits.append((brace + 1, text))
    if not edits:
        return out_text, done
    for at, s in sorted(edits, reverse=True):
        out_text = out_text[:at] + s + out_text[at:]
    if routed:
        report.append("mod_hooks route prologues: %s" % ", ".join("%08X" % x for x in routed))
        out_text = "#include \"b3host/hook_route.h\"   // decomp2host: config/mod_hooks.txt (mod kit 2a)\n" + out_text
    if done:
        report.append("native_watch prologues: %s" % ", ".join("%08X" % x for x in done))
        out_text = "#include \"b3host/watch.h\"   // decomp2host: config/native_watch.txt (pic1)\n" + out_text
    return out_text, done


_PHASE_T = [None]


def _phase(label):
    """boot2: DECOMP2HOST_TIMES=1 prints the time since the previous phase mark (stderr)."""
    import time
    if os.environ.get("DECOMP2HOST_TIMES") != "1":
        return
    now = time.time()
    if _PHASE_T[0] is not None:
        sys.stderr.write("decomp2host time: %-28s %7.1f s\n" % (label, now - _PHASE_T[0]))
    _PHASE_T[0] = now


_STUBS_CTX = []   # gen1: (paths, compiled, owners, decomp, keep_going) for _stubs_or_fail, set before the pool forks


def _stubs_or_fail(k):
    paths, compiled, owners, decomp, keep_going = _STUBS_CTX
    p, c = paths[k], compiled[k]
    if isinstance(c, GenError):
        return c
    try:
        elsewhere = {sym for sym, _ in c[1] if owners.get(sym, set()) - {p}}
        return call_stubs(p, decomp, [], compiled=c, defined_elsewhere=elsewhere)
    except GenError as e:
        if not keep_going:
            raise
        return e


def native_rows(out, units, entries):
    """gen1 (boot2's iteration time): with DECOMP2HOST_NATIVE_ROWS=1 (the Makefile's native generation), each unit ends
    with its game functions' native table rows (b3host/native_rows.h: B3_NATIVE_ROW, what native_table.cpp's B3N_GAME
    made of each B3PORT_GAME_OVERRIDES entry) and include/b3port/native_rows.h lists the arrays in the same order
    (X(array, count)). The table's traced_entry / native_entry instantiations then compile with their units: one
    changed function recompiles its unit and a small table, not one translation unit holding all ~6,600 entries.
    Without it nothing changes (gen/decomp and game_overrides.h are as before)."""
    path = os.path.join(out, "include/b3port/native_rows.h")
    if os.environ.get("DECOMP2HOST_NATIVE_ROWS") != "1":
        if os.path.exists(path):
            os.remove(path)
        return
    by_unit, order = {}, []
    for e in entries:
        if e[6] not in by_unit:
            order.append(e[6])
        by_unit.setdefault(e[6], []).append(e)
    # the rows of one unit are contiguous in entries (appended unit by unit): grouping keeps the overall order
    assert [e for f in order for e in by_unit[f]] == list(entries)
    syms = []
    for f in order:
        if f not in units:
            continue
        sym = "b3rows_" + re.sub(r"\W", "_", unit_name(f))
        if sym in [s for s, _ in syms]:
            die("%s: native row array name %s twice" % (f, sym))
        rows = by_unit[f]
        with open(os.path.join(out, "units", unit_name(f) + ".cpp"), "a") as o:
            o.write("\n// ---- decomp2host: the native backend's table rows (b3host/native_rows.h, b3port/native_rows.h)\n"
                    "#include \"b3host/native_rows.h\"\nextern const b3host::NativeRow %s[] = {\n" % sym)
            for name, addr, size, fnv, wname, track, _ in rows:
                o.write("    B3_NATIVE_ROW(\"%s\", 0x%08Xu, %du, %s, %s),\n" % (name, addr, size, wname, track))
            o.write("};\n")
        syms.append((sym, len(rows)))
    with open(path, "w") as o:
        o.write("// GENERATED by tools/decomp2host/decomp2host.py. Do not edit.\n"
                "// The generated units' native table rows (b3host::NativeRow arrays), in B3PORT_GAME_OVERRIDES order.\n"
                "#ifndef B3PORT_NATIVE_ROWS_H\n#define B3PORT_NATIVE_ROWS_H\n#define B3PORT_NATIVE_ROWS(X) \\\n")
        for sym, n in syms:
            o.write("    X(%s, %d) \\\n" % (sym, n))
        o.write("\n#endif\n")


def generate(decomp, sel, a, keep_going=False):
    """Generate host C++ for `sel` [(address, track)] into a.out (a.decomp: the decomp root). keep_going: a unit that
    fails is left out (OUT/failed.tsv) instead of stopping."""
    _phase('start')
    DECOMP_ROOT[0] = decomp.root   # gen1: for compile_for_stubs' compiler-guided T7 rule
    HEADER_TEXT.clear()
    PORT_TYPEDEFS.clear()
    HEADER_ASM_MACROS.clear()
    STATIC_MEMBERS.clear()
    VIRTUALS.clear()
    os.makedirs(os.path.join(a.out, "units"), exist_ok=True)
    os.makedirs(os.path.join(a.out, "include/b3gen"), exist_ok=True)
    os.makedirs(os.path.join(a.out, "include/b3port"), exist_ok=True)
    open(os.path.join(a.out, "include/b3gen/prelude.h"), "w").write(PRELUDE)
    units = {}
    seen_sel = set()
    for addr, track in sel:
        if addr in seen_sel:
            continue   # gen1: an address listed twice (mac1's 00365040): the first line wins (it had two wrappers)
        seen_sel.add(addr)
        if addr not in decomp.funcs:
            die("%08X: not a function start in assembly/asm" % addr)
        name, size = decomp.funcs[addr]
        # The selection's track follows the snapshot: a draft that has since byte-matched is generated from its proven
        # unit, and the reverse (a list written against an older snapshot).
        if track in PORT_TRACKS:
            f = decomp.port_file(track, addr)
            if not f:
                die("%08X %s: no `// 0x%08X` marker in %s/*.cpp" % (addr, name, addr, PORT_TRACKS[track]))
            units.setdefault(f, []).append((addr, track, name, size))
            continue
        if track in ("matched", "nonmatching") and gtmath_instance(name, addr) and not gtmath_defined(decomp, a, addr, name):
            units.setdefault(GTMATH_INSTANCES, []).append((addr, track, name, size))   # gen1
            continue
        if track == "nonmatching" and decomp.matched_file(addr):
            track = "matched"
        elif track == "matched" and not decomp.matched_file(addr) and \
                os.path.exists(os.path.join(a.decomp, decomp.nonmatching_file(name))):
            track = "nonmatching"
        if track == "matched":
            f = decomp.matched_file(addr)
            if not f:
                die("%08X %s: not in config/proven_units.txt" % (addr, name))
        else:
            # boot2: an alternate entry (alabel) is defined in its parent's archive
            f = decomp.nonmatching_file(decomp.funcs[decomp.alt_parent[addr]][0] if addr in decomp.alt_parent else name)
            if not os.path.exists(os.path.join(a.decomp, f)):
                die("%08X: %s missing" % (addr, f))
        units.setdefault(f, []).append((addr, track, name, size))
    done, manifest, entries, wrappers = set(), [], [], []
    failed = []   # keep_going: (unit, reason)
    watch, watched = load_watch(), []   # pic1
    routes = load_mod_hooks()   # mod kit 2a: config/mod_hooks.txt
    global RAISE
    raise_before = RAISE
    if keep_going:
        RAISE = True

    def drop(f, why):
        failed.append((f, why))
        manifest[:] = [l for l in manifest if ("\t%s\t" % f) not in l]
        entries[:] = [e for e in entries if e[6] != f]
        wrappers[:] = [w for w in wrappers if w[0] != f]
        try:
            os.remove(os.path.join(a.out, "units", unit_name(f) + ".cpp"))
        except OSError:
            pass
    for f, fns in sorted(units.items()):
      try:
        report = []
        if f == GTMATH_INSTANCES:   # gen1: header inlines emitted out of line, over the shadow header
            text = ('#include "b3/GtMathPs2.h"\n#include "b3/GtMathPs2Quat.h"\n#include "b3/GtMathPs2Trig.h"\n'
                    '#include <new>\n#include <type_traits>\n#include <utility>\n' +
                    "".join(gtmath_instance(x[2], x[0]) for x in sorted(fns)))
        else:
            text = open(src_path(a.decomp, f)).read()
        if any(f.startswith(d + "/") for d in PORT_TRACKS.values()):
            text = port_libc_values(text, report)
        for inc in re.findall(r'#\s*include\s+"([^"]+)"', text):
            transform_header(inc, decomp, a.out, done, report)
        text = sinit_unit(text, fns, decomp, report)   # boot1 T25
        reduced, kept = reduce_unit(text, {x[0] for x in fns}, report, {x[0]: x[2] for x in fns})
        reduced = header_c_linkage(reduced, decomp, {x[0]: x[2] for x in fns}, report)   # gen1
        reduced = fnptr_param_transforms(reduced, {x[0]: x[2] for x in fns}, report)   # gen1 T36
        reduced = expand_asm_macros(reduced, report)
        reduced = inline_asm_helpers(reduced, fns, decomp, report)   # gen1 T41 (vu0's VU binding)
        reduced = asm_function_transforms(reduced, fns, decomp, report)   # gen1 (vu0's translator)
        reduced = asm_transforms(reduced, f, report, asm_macros(reduced) | HEADER_ASM_MACROS, decomp)
        reduced = static_data_transforms(reduced, report)   # boot1
        check_no_data(reduced, f)
        out_text = transform_text(reduced, decomp, report, is_unit=True)
        if not any(f.startswith(d_ + "/") for d_ in PORT_TRACKS.values()):
            out_text = local_static_transforms(out_text, report, decomp)   # gen1 T40
            out_text = ee_division_transforms(out_text, report)   # gen1 T39 (decomp code only)
        out_text = fold_site_transforms(out_text, {x[0] for x in fns}, f, report)   # gen1: MWCC's constant folds
        # boot2 E1: host locals escaping into guest memory. A port file (src/libs/*) must use T27 at retail's offsets:
        # fatal; a decomp unit is rewritten (T31) and checked again. DECOMP2HOST_ESCAPE=warn only reports.
        port_unit = any(f.startswith(d_ + "/") for d_ in PORT_TRACKS.values())
        hits = escape_lint(out_text, guest_names=set())   # (a) / (b) here; (c) in call_stubs, which knows the guest calls
        # (a) / (b) in a port file need retail's offset (T27): fatal there; a decomp unit's are rewritten (T31)
        fixable = [h for h in hits if not port_unit]
        if fixable and os.environ.get("DECOMP2HOST_ESCAPE") != "warn":
            out_text = escape_fix(out_text, [(h[2], h[3], h[4]) for h in fixable])
            report.append("T31 guest-stack locals: %s" % ", ".join(sorted(set(h[2] for h in fixable))))
            hits = escape_lint(out_text)   # what T31 could not rewrite
        for fh, ex, nm, _span, _decl in hits:
            msg = "E1 host local escapes into guest memory: %s in %s" % (ex, fh)
            report.append(msg)
            sys.stderr.write("decomp2host: %s: %s\n" % (f, msg))
            if port_unit and not ex.startswith("guest call") and os.environ.get("DECOMP2HOST_ESCAPE") != "warn":
                die("%s: %s (make it a T27 guest-stack local: B3_RETAIL_FRAME / B3_FRAME_LOCAL at retail's offset)"
                    % (f, msg))
        uname = unit_name(f)
        # T7: the unit's own declarations go into a namespace of their own, so two units' partial layouts of one class
        # (each declares only what its function touches) never meet (ODR); not possible when the unit defines members
        # of a class declared in a decomp header (those headers are shared, so they are consistent anyway).
        ns = None
        mo = mask(out_text)
        quals = set()
        for tf in top_level_functions(out_text):
            q = re.findall(r"\b([A-Za-z_]\w*)::[A-Za-z_~]\w*\s*\(", tf[4])
            quals.update(q)
        local = all(re.search(r"\b(class|struct|union)\s+%s\b[^;]*\{" % re.escape(q), mo) for q in quals)
        if local:
            ns = "b3gen_" + re.sub(r"\W", "_", uname)
            out_text = hoist_late_includes(out_text, report)   # gen1 (mac1, clang)
            incs = list(re.finditer(r"^[ \t]*#\s*include[^\n]*\n", out_text, re.M))
            at = incs[-1].end() if incs else 0
            # a unit that reopens a namespace it also uses (`using namespace GtMathPs2; namespace GtMathPs2 { helper }`,
            # GtIntersection_002BF2E0) now reopens b3gen_X::N: keep both the global N and the nested one visible
            reopen, glob_ns = t7_reopen(mo, lambda n_: bool(
                (n_ == "GtMathPs2" and re.search(r'#\s*include\s*"b3/GtMath', out_text)) or
                any(re.search(r"\bnamespace\s+%s\b" % re.escape(n_), h_) for h_ in HEADER_TEXT)))
            # gen1: a forward declaration above the last #include (`struct UnkStruct_004617B0;`, func_004617B0) of a
            # struct the unit defines below it moves into the namespace with the definition: left global, the name
            # was ambiguous after `using namespace` (an incomplete ::X and the namespace's X)
            out_text, at, fwd = t7_forward_declarations(out_text, at)
            out_text = out_text[:at] + glob_ns + "namespace %s {\n" % ns + reopen + fwd + out_text[at:] + "\n}  // namespace %s\nusing namespace %s;\n" % (ns, ns)
            report.append("namespace " + ns)
        hdr = ("// GENERATED by tools/decomp2host/decomp2host.py from the decomp's %s. Do not edit.\n"
               "// Kept: %s\n#include \"b3gen/prelude.h\"\n" % (f, ", ".join("%08X %s" % (x[0], x[2]) for x in fns)))
        if re.search(r'#\s*include\s+"%s"' % re.escape(RANDOMISER_HEADER), out_text):   # mac5 T45
            hdr += "#define B3_RANDOMISER_STATE 0x%08Xu   // decomp2host T45: this source file's random state\n" % \
                randomiser_state_for([x[0] for x in fns])
        open(os.path.join(a.out, "units", uname + ".cpp"), "w").write(scrub_comment_text(hdr + out_text))
        # wrappers from the transformed definition heads
        tfns = fix_markers(top_level_functions(out_text), {x[0]: x[2] for x in fns})
        for x in fns:
            if not any(t[3] == x[0] for t in tfns):
                hits = [t for t in tfns if t[3] is None and (re.search(r"\b%s\s*\(" % re.escape(x[2]), t[4]) or
                                                             re.search(r"\b%s\s*\(" % re.escape(cxx_name(x[2])), t[4]))]
                if len(hits) == 1:
                    k = tfns.index(hits[0])
                    tfns[k] = hits[0][:3] + (x[0],) + hits[0][4:]
        decl_lines = []
        for addr, track, name, size in fns:
            tf = [t for t in tfns if t[3] == addr]
            if not tf:
                die("%08X: definition lost in the transform" % addr)
            wname, wdef = wrapper_for(tf[0][4], out_text, addr, ns)
            fnv = fnv1a64(decomp.code(addr, size))
            wrappers.append((f, wdef))
            entries.append((name, addr, size, fnv, wname, track, f))
            manifest.append("%08X\t%s\t%s\t%s\t%d\t%016x\t%s" % (addr, track, name, f, size, fnv, "; ".join(report)))
        if (watch | routes) & {x[0] for x in fns}:   # pic1: the watched definitions' prologues (2a: and the routed ones')
            out_text, wd = watch_prologues(out_text, fns, tfns, watch, report, routes)
            open(os.path.join(a.out, "units", uname + ".cpp"), "w").write(scrub_comment_text(hdr + out_text))
            watched.extend((f, x) for x in wd)
      except (GenError, Exception) as e:
        if not keep_going:
            raise
        drop(f, str(e).split("\n")[0])
    units = {f: v for f, v in units.items() if f not in {x[0] for x in failed}}
    _phase('units (transform)')
    # wrappers file: includes every unit's declarations by including the units themselves would define them twice,
    # so each unit gets its wrapper appended instead.
    for f in units:
        uname = unit_name(f)
        path = os.path.join(a.out, "units", uname + ".cpp")
        ws = [w for (ff, w) in wrappers if ff == f]
        bv = set()
        for w in ws:
            bv.update(re.findall(r"b3host::by_value<([^<>]+)>", w))
        specs = lone_float_specialisations(bv, [open(path).read()] + HEADER_TEXT) if bv else []
        with open(path, "a") as o:
            if specs:
                o.write("\n" + "\n".join(specs) + "\n")
            o.write("\n// ---- decomp2host: guest-ABI wrappers (b3host::native_entry reads them in the EE convention)\n")
            for w in ws:
                o.write(w + "\n")
    _phase('wrappers')
    # calls to other game functions: one host compile per unit (in parallel), then stubs
    import concurrent.futures
    paths = [os.path.join(a.out, "units", unit_name(f) + ".cpp") for f in units]
    def compile_or_fail(p):
        try:
            return compile_for_stubs(p)
        except GenError as e:
            if not keep_going:
                raise
            return e
    jobs = int(os.environ.get("DECOMP2HOST_JOBS", "8"))
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        compiled = list(ex.map(compile_or_fail, paths))
    _phase('compile_for_stubs')
    owners = {}
    for p, c in zip(paths, compiled):
        if not isinstance(c, GenError):
            for sym in c[2]:
                owners.setdefault(sym, set()).add(p)

    # gen1 (speed): call_stubs is Python-bound (the GIL kept 4 threads to one core: ~50 s); on Linux it runs in forked
    # processes, which inherit the state the transform phase built (call_stubs writes no module state but two memo
    # caches: checked). Each unit's result is its own file and a count, so the output is the same.
    _STUBS_CTX[:] = [paths, compiled, owners, decomp, keep_going]
    if sys.platform.startswith("linux") and jobs > 1 and os.environ.get("DECOMP2HOST_STUB_THREADS") != "1":
        import multiprocessing
        sys.stdout.flush()
        sys.stderr.flush()
        with concurrent.futures.ProcessPoolExecutor(jobs, mp_context=multiprocessing.get_context("fork")) as ex:
            n_calls = list(ex.map(_stubs_or_fail, range(len(paths)), chunksize=8))
    else:
        with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
            n_calls = list(ex.map(_stubs_or_fail, range(len(paths))))
    _STUBS_CTX[:] = []
    _phase('call_stubs (+T31c)')
    for f, n in list(zip(units, n_calls)):
        if isinstance(n, GenError):
            drop(f, str(n).split("\n")[0])
    n_calls = [n for n in n_calls if not isinstance(n, GenError)]
    units = {f: v for f, v in units.items() if f not in {x[0] for x in failed}}
    RAISE = raise_before
    for f, n in zip(units, n_calls):
        if n:
            for k, line in enumerate(manifest):
                if ("\t%s\t" % f) in line:
                    manifest[k] = line + "; calls %d" % n
    with open(os.path.join(a.out, "include/b3port/game_overrides.h"), "w") as o:
        o.write("// GENERATED by tools/decomp2host/decomp2host.py. Do not edit.\n"
                "// Decomp game functions as overrides: X(name, address, code_bytes, code_fnv1a64, wrapper, track).\n"
                "// Reported cost per call: the static instruction count (code_bytes / 4).\n"
                "#ifndef B3PORT_GAME_OVERRIDES_H\n#define B3PORT_GAME_OVERRIDES_H\n#include \"b3gen/prelude.h\"\n"
                "#include \"b3/GtMathPs2.h\"   // the port's shadow header: GtMath value types in wrapper signatures\n")
        for (f, w) in wrappers:
            o.write(w.split("{")[0].strip() + ";\n")
        o.write("\n#define B3PORT_GAME_OVERRIDES(X) \\\n")
        for name, addr, size, fnv, wname, track, _ in entries:
            o.write("    X(\"%s\", 0x%08Xu, %du, 0x%016xull, %s, %s) \\\n" % (name, addr, size, fnv, wname, track))
        o.write("\n#endif\n")
    native_rows(a.out, units, entries)
    # pic1: the self-reporting functions, for b3host::traced_entry (compile time); none: no file (output unchanged)
    wpath = os.path.join(a.out, "include/b3port/native_watch.h")
    wlist = sorted({x for f_, x in watched if f_ in units})
    if wlist:
        with open(wpath, "w") as o:
            o.write("// GENERATED by tools/decomp2host/decomp2host.py from config/native_watch.txt. Do not edit.\n"
                    "// Definitions that report their own entry (b3host::watch_self); traced_entry leaves them to it.\n"
                    "#ifndef B3PORT_NATIVE_WATCH_H\n#define B3PORT_NATIVE_WATCH_H\n#define B3PORT_NATIVE_WATCH(X) \\\n")
            for x in wlist:
                o.write("    X(0x%08Xu) \\\n" % x)
            o.write("\n#endif\n")
    elif os.path.exists(wpath):
        os.remove(wpath)
    _phase('tables')
    scrub_notes_refs(a.out)
    with open(os.path.join(a.out, "manifest.tsv"), "w") as o:
        o.write("address\ttrack\tname\tsource\tcode_bytes\tfnv1a64\ttransform notes\n")
        o.write("\n".join(manifest) + "\n")
    if keep_going:
        with open(os.path.join(a.out, "failed.tsv"), "w") as o:
            o.write("source\treason\n")
            for f, why in failed:
                o.write("%s\t%s\n" % (f, why.replace("\t", " ")))
    prune_units(a.out, {unit_name(f) for f in units}, getattr(a, "objects", None))
    _phase('manifest + prune')
    return len(entries), len(units)


def prune_units(out, names, object_dirs=None):
    """boot1 (2026-11-05): OUT/units.txt lists the units this run generated; every other unit in OUT/units
    (a renamed or dropped one from an earlier run) is removed, and with object_dirs (comma-separated) its compiled
    object `<dir>/<unit>.o` too, so a stale unit can never reach a link."""
    with open(os.path.join(out, "units.txt"), "w") as o:
        o.write("".join(n + "\n" for n in sorted(names)))
    removed = 0
    for g in glob.glob(os.path.join(out, "units", "*.cpp")):
        if os.path.splitext(os.path.basename(g))[0] not in names:
            os.remove(g)
            removed += 1
    for d in (object_dirs or "").split(","):
        if not d:
            continue
        for g in glob.glob(os.path.join(d, "*.o")):
            if os.path.splitext(os.path.basename(g))[0] not in names:
                os.remove(g)
                removed += 1
    if removed:
        sys.stderr.write("decomp2host: removed %d stale unit / object file(s)\n" % removed)


if __name__ == "__main__":
    main()
