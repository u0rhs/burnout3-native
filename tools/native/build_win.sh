#!/bin/bash
# Builds the native backend's programs for Windows (mingw-w64, the verified MSYS2 toolchain: tools/toolchain/toolchain.sha256)
# from WSL through Windows interop, into $OUT (default $WIN/s0native; no default exe is touched):
#   native_boot.exe   (as `make native`'s build/native_boot)
#   native_play.exe   (as `make play`'s build/native_play; with RENDER=1 also b3render.dll + SDL3.dll beside it, from
#                      tools/render/build_win.sh -- native_play needs them next to the exe)
#
# One definition of what is compiled and how (win1): the source lists and every compile flag come from the Makefile
# (CPPFLAGS NATIVE_FLAGS REQFLAGS NGENFLAGS, NATIVE_SRC HOST_SRC GAMEVIEW_SRC), so the Windows objects are built as
# the Linux ones (zero-initialised locals, no access control, -ffp-contract=off, ...). -O2 without -g (the same
# code). The tree is staged under $OUT/stage at the repo's relative layout and compiled from there.
# The generated game code: NATIVE_FROM=bundle (or no build/native/functions.h: a bundle build's build/ has none) the
# committed gen/native (its table/ and units/, as the Makefile's NATIVE_FROM=bundle); else build/native/functions.h and
# build/gen_native from a decomp-mode `make native` or `make play`.
#   tools/native/build_win.sh        JOBS=4 compiles at a time (default); FORCE=1 rebuilds every object
#   1.3.1 (tools/toolchain/wincc.sh): the tree is staged by content (an unchanged file keeps its time, so only the units
#   whose own -MMD list -- source or a header it read -- changed are compiled again), and each compile goes through
#   ccache when WSL has it (its own cache, ~/.cache/ccache-mingw: a respin into a new OUT is mostly cache hits; CCACHE=0:
#   none)
#   OUT=/mnt/c/.../play1 RENDER=1 tools/native/build_win.sh
#   STRIP=1: the programs linked with -s (no symbols: a release, tools/release/package_win.sh; a crash report's
#   offsets then need the unstripped build of the same commit)
#   DEBUG_INFO=1 (debuginfo): every unit compiled with -g1 as well (here and in tools/render/build_win.sh): the line
#   tables and function names, so an unstripped build resolves a crash report's offsets to source lines, inlined frames
#   included (addr2line -f -i); DEBUG_INFO=2: full -g (locals and types too: native_play.exe ~2.1 GB, against ~0.5 GB).
#   Debug information does not change the code GCC generates: such a build, stripped (package_win.sh strips its copies),
#   has the same code and data as a STRIP=1 build of the commit (checked section by section for 1.4.0's test bundles).
#   Changing it recompiles every object (the flags differ).
#   NATIVE_FALLBACK=1: native_play.exe with the retail fallback (the Makefile's NATIVE_FALLBACK: developer / diagnostic
#   builds); the default 0 has no R5900 interpreter in it, as every release. native_boot.exe always has it.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../.." && pwd)
. "$PORT/tools/local_env.sh"; b3_local_env "$PORT"   # the machine's paths (local.env)
. "$PORT/tools/toolchain/wincc.sh"   # 1.3.1: staging by content, ccache
WIN=${WIN:-$PORT/build-win}   # local.env: where the Windows toolchain (toolchain/mingw64) and outputs live
TC=$WIN/toolchain/mingw64
OUT=${OUT:-$WIN/s0native}
(cd "$TC" && sha256sum --quiet -c "$PORT/tools/toolchain/toolchain.sha256") || { echo "toolchain does not match toolchain.sha256" >&2; exit 1; }
if [ -z "${NATIVE_FROM:-}" ]; then NATIVE_FROM=$([ -f "$PORT/build/native/functions.h" ] && echo decomp || echo bundle); fi
case "$NATIVE_FROM" in
bundle)
    GEN_DIRS="gen/native"; UNITS=gen/native/units
    [ -f "$PORT/gen/native/table/functions.h" ] && [ -f "$PORT/gen/native/manifest.tsv" ] || { echo "no committed gen/native (table/functions.h, manifest.tsv)" >&2; exit 2; } ;;
decomp)
    GEN_DIRS="build/native build/gen_native gen/native/vu0"; UNITS=build/gen_native/units
    [ -f "$PORT/build/native/functions.h" ] || { echo "run: make native (build/native/functions.h)" >&2; exit 2; }
    [ -f "$PORT/build/gen_native/manifest.tsv" ] || { echo "run: make native (build/gen_native)" >&2; exit 2; } ;;
*) echo "NATIVE_FROM=$NATIVE_FROM: bundle or decomp" >&2; exit 2 ;;
esac
echo "build_win: generated code from $([ "$NATIVE_FROM" = bundle ] && echo "the committed gen/native" || echo "build/gen_native (decomp)")" >&2

# the Makefile's variables (never `make -p`: that runs the default goal), for the same NATIVE_FROM (its -I paths)
mkvar() { make -s -C "$PORT" --no-print-directory NATIVE_FROM="$NATIVE_FROM" CXX=g++ CXXFLAGS=-O2 --eval="b3_print_var: ; @echo \$($1)" b3_print_var; }
# mac5: -mno-ms-bitfields -- mingw-w64 lays bitfields out as MSVC does by default (a new unit at each change of the
# declared type), the PS2 / SysV ABI packs them: the game's DMA / GIF tag bitfields came out wrong (VIF1 streams the
# DMA model misparsed from VBlank 93 on: the race-start hang)
read -r -a FLAGS <<< "$(mkvar CPPFLAGS) $(mkvar NATIVE_FLAGS) $(mkvar REQFLAGS) -O2 $(mkvar NGENFLAGS) -DB3_PS2_FLOAT=1 -mno-ms-bitfields"
DEBUG_G=$(case "${DEBUG_INFO:-0}" in 1) echo -g1 ;; 2) echo -g ;; esac)   # debuginfo: on every compile (the header)
[ -n "$DEBUG_G" ] && FLAGS+=("$DEBUG_G")
read -r -a SRCS <<< "$(mkvar NATIVE_SRC) $(mkvar HOST_SRC) $(mkvar GAMEVIEW_SRC) tools/native/gameview_native.cpp"
CHD_SRC=$(mkvar CHD_SRC)   # chd1: libchdr's C unit (b3asset/discfs.h's CHD images), compiled as C

S=$OUT/stage
mkdir -p "$S" "$OUT/obj"
# vu0n: gen/native/vu0 -- the committed VU0 microprograms as C++ (no VU0 interpreter in these programs: Makefile
# VU0_INTERP stays 0 here; the bundle's gen/native holds it)
# 1.3.1: by content (wincc_stage): an unchanged file keeps its time in the stage
staged=$(wincc_stage "$PORT" "$S" include src tests tools/native $GEN_DIRS sdk/include)   # sdk2b: the mod API header
echo "build_win: $staged files staged (new or changed)" >&2
mkdir -p "$S/build"   # version_info.cpp (a bundle build stages nothing else under build/)
# test builds: the version stamp (include/b3report/version.h), a generated source rewritten only when it changes
"$PORT/tools/release/version_info.sh" "$S/build/version_info.cpp"
wincc_init "$TC" "$PORT" "$OUT"
tc() { wincc_run "$S" "$1" "$2"; }   # cwd: the stage (relative -I); $2: the rsp, relative to the stage (../obj/...)
m() { wslpath -m "$1"; }

# 1.3.1: an object is stale when any file of its own dependency list (the compiler's -MMD list: its source and every
# header it read, as staged) is newer than it (was: any header newer than the last run's stamp rebuilt everything, and an
# interrupted run left objects built against the old headers that the next run took). Changed flags rebuild every object;
# .flags is written only once every object compiled with them, so an interrupted run does it again (through ccache).
printf '%s\n' "${FLAGS[@]}" > "$OUT/obj/.flags.new"
full=${FORCE:-0}
cmp -s "$OUT/obj/.flags.new" "$OUT/obj/.flags" 2>/dev/null || full=1
rm -f "$OUT/obj/.stamp"   # (the retired stamp)
# nofallback1: native_play.o is compiled with -DB3_NATIVE_FALLBACK=$NATIVE_FALLBACK; a change of it recompiles that one
NATIVE_FALLBACK=${NATIVE_FALLBACK:-0}
[ "$(cat "$OUT/obj/.native_fallback" 2>/dev/null)" = "$NATIVE_FALLBACK" ] || rm -f "$OUT/obj/native_play.o"
echo "$NATIVE_FALLBACK" > "$OUT/obj/.native_fallback"

# 1.3.1: the objects as <source relative to the stage> <object name>; one pass (python: a stat each over /mnt/c, not a
# process each) says which are stale: all (full), else those missing, failed last time, without a dependency list, or
# older than a file of it
units=()
for s in "${SRCS[@]}"; do x=${s%.cpp}; units+=("$s" "${x//\//_}.o"); done
x=${CHD_SRC%.c}; units+=("$CHD_SRC" "${x//\//_}.o")
units+=(tools/native/native_boot.cpp native_boot.o tools/native/native_play.cpp native_play.o build/version_info.cpp version_info.o)
n_common=${#units[@]}
for s in "$S/$UNITS"/*.cpp; do n=${s##*/}; units+=("$UNITS/$n" "gen_${n%.cpp}.o"); done
printf '%s\n' "${units[@]}" > "$OUT/obj/.units"
declare -A stale=()
while IFS= read -r o; do stale[$o]=1; done < <(python3 -I - "$S" "$OUT/obj" "$full" << 'EOF'
import os, sys
stage, obj, full = sys.argv[1], sys.argv[2], sys.argv[3] == '1'
import json
u = open(os.path.join(obj, '.units')).read().split('\n')
# .depcache: per object its time and dependency list when last read good (an object is deleted before it is compiled
# again, so a time seen before is the object that list belongs to): reads only the .status / .d of objects made since
cache_path = os.path.join(obj, '.depcache')
try:
    cache = json.load(open(cache_path))
except (OSError, ValueError):
    cache = {}
try:   # the stage's record (wincc_stage): each staged file's time as written, without a stat over /mnt/c
    rec = json.load(open(os.path.join(stage, '.wincc_stage')))
except (OSError, ValueError):
    rec = {}
mt = {}
def mtime(rel):   # a dependency's time in the stage (its paths are relative to it, or C:/... ones); missing: newer than anything
    r = rec.get(os.path.normpath(rel))
    if r:
        return r[1]
    if rel not in mt:
        try:
            q = '/mnt/' + rel[0].lower() + rel[2:] if len(rel) > 2 and rel[1] == ':' and rel[2] == '/' else os.path.join(stage, rel)
            mt[rel] = os.stat(q).st_mtime_ns
        except OSError:
            mt[rel] = 1 << 62
    return mt[rel]
new_cache = {}
newest = 0
for src, o in zip(u[0::2], u[1::2]):
    p = os.path.join(obj, o)
    try:
        if full:
            raise OSError
        t = os.stat(p).st_mtime_ns
        newest = max(newest, t)
        c = cache.get(o)
        if c and c[0] == t:
            deps = c[1]
        else:
            if open(p + '.status').read().strip() != 'ok':
                raise OSError
            d = open(p + '.d').read().replace('\r', '').replace('\\\n', ' ')
            deps = d.split(':', 1)[1].split()
            if not deps or deps[0] != src:
                raise OSError
        new_cache[o] = [t, deps]
        ok = all(mtime(x) < t for x in deps)
    except (OSError, IndexError):
        ok = False
    if not ok:
        print(o)
with open(cache_path + '.new', 'w') as f:
    json.dump(new_cache, f)
os.replace(cache_path + '.new', cache_path)
with open(os.path.join(obj, '.newest'), 'w') as f:   # the newest object before this run's compiles (the links' check)
    f.write(str(newest))
EOF
)
running=0
compiled=0
done_list=()
compile() {   # compile <source relative to the stage> <object name>
    [ -n "${stale[$2]+x}" ] || return 0
    local extra=()   # rt1: the units on either side of the VU0 float-environment switch (Makefile VU0_FP_ENV)
    case "$1" in *native/vu0_aot_gen.cpp|*native/vu0_unit.cpp) extra=(-frounding-math);; *native/script_lua.cpp) extra=(-w);;   # lua6: the vendored Lua unit
                 tools/native/native_play.cpp) extra=(-DB3_NATIVE_FALLBACK="$NATIVE_FALLBACK");; esac
    local cc=g++.exe flags=("${FLAGS[@]}")
    case "$1" in *.c) cc=gcc.exe; flags=(-std=gnu11 -O2 -DNDEBUG -w $DEBUG_G);; esac   # chd1: the C unit (libchdr); NDEBUG: no assert() file paths (the build folder) in a release
    # 1.3.1: every path relative to the stage (the object beside it in ../obj), as ccache's depend mode needs (-MMD)
    local rel=../obj/$2 o=$OUT/obj/$2
    rm -f "$o" "$o.d"   # (a failed compile leaves no object, so no old one passes as made from this source)
    printf '%s\n' "${flags[@]}" "${extra[@]}" -c "$1" -o "$rel" -MMD -MF "$rel.d" > "$o.rsp"
    # WSL interop can fail to start a Windows process ("UtilAcceptVsock ... failed"): retried in wincc_run
    ( if tc "$cc" "$rel.rsp" > "$o.log" 2>&1; then echo ok > "$o.status"; else echo failed > "$o.status"; fi ) &
    running=$((running + 1)); compiled=$((compiled + 1)); done_list+=("$2")
    if [ "$running" -ge "${JOBS:-4}" ]; then wait -n || true; running=$((running - 1)); fi
}
for ((i = 0; i < ${#units[@]}; i += 2)); do compile "${units[i]}" "${units[i + 1]}"; done   # the platform tier and the programs first: their errors show in minutes
wait || true
echo "compiled $compiled objects ($(( (${#units[@]} - n_common) / 2 )) generated units in all)" >&2
# the objects compiled now: each there, its status ok and no error in its log (the others were checked when they were made)
printf '%s\n' "${done_list[@]}" > "$OUT/obj/.compiled"
[ "$compiled" -gt 0 ] && rm -f "$OUT/native_boot.linked" "$OUT/native_play.linked"   # (also below: a run cut short here)
python3 -I - "$OUT/obj" << 'EOF' || exit 1
import os, sys
obj, bad = sys.argv[1], 0
for o in filter(None, open(os.path.join(obj, '.compiled')).read().split('\n')):
    p = os.path.join(obj, o)
    try:
        log = open(p + '.log', errors='replace').read()
    except OSError:
        log = ''
    try:
        st = open(p + '.status').read().strip()
    except OSError:
        st = ''
    if not os.path.isfile(p) or st != 'ok' or ' error: ' in log:
        bad = 1
        print('compile failed: ' + p, file=sys.stderr)
        for l in [l for l in log.split('\n') if 'error' in l][:5]:
            print(l, file=sys.stderr)
sys.exit(bad)
EOF
mv "$OUT/obj/.flags.new" "$OUT/obj/.flags"   # every object is now compiled with these flags
# the programs' icon (assets/icon/b3-icon.ico, made by tools/release/make_icons.py): a resource object linked into the .exe
printf '1 ICON "%s"\n' "$(m "$PORT/assets/icon/b3-icon.ico")" > "$OUT/obj/b3icon.rc"
printf '%s\n' -O coff -i "$(m "$OUT/obj/b3icon.rc")" -o "$(m "$OUT/obj/b3icon.res.o")" > "$OUT/obj/b3icon.rsp"
[ -f "$OUT/obj/b3icon.res.o" ] && [ "$OUT/obj/b3icon.res.o" -nt "$PORT/assets/icon/b3-icon.ico" ] ||
    { rm -f "$OUT/native_play.linked"; tc windres.exe ../obj/b3icon.rsp > "$OUT/obj/b3icon.log" 2>&1; } || { cat "$OUT/obj/b3icon.log" >&2; exit 1; }
for t in native_boot native_play; do
    # 16 MB stacks: the main thread's reserve (Linux: 8 MB by ulimit), and the default for threads that ask for none
    # 1.3.1: the objects relative to the stage, the link's cwd. Relinked when anything compiled now, or unless <exe>.linked
    # (written after its last good link) is newer than every object (.newest, from the stale check) and the list is the
    # same: a run cut short after compiling, or a failed link of the other program, relinks it next time
    { printf '%s\n' -static -O2 -Wl,--stack,16777216 $([ "${STRIP:-0}" = 1 ] && echo -s) -o "../$t.exe" "../obj/$t.o"
      for ((i = 0; i < ${#units[@]}; i += 2)); do case "${units[i + 1]}" in native_boot.o|native_play.o) ;; *) echo "../obj/${units[i + 1]}" ;; esac; done
      [ "$t" = native_play ] && echo ../obj/b3icon.res.o   # the B3 icon
      printf '%s\n' -lz -lwinmm -pthread   # zlib: was the retired snapshot restore's gzipped SIF log; unused now (harmless)
    } > "$OUT/$t.link.rsp.new"
    if [ "$compiled" -gt 0 ] || [ ! -f "$OUT/$t.exe" ] || ! cmp -s "$OUT/$t.link.rsp.new" "$OUT/$t.link.rsp" ||
       ! python3 -I -c 'import os, sys; sys.exit(int(open(sys.argv[2]).read() or 0) >= os.stat(sys.argv[1]).st_mtime_ns)' \
           "$OUT/$t.linked" "$OUT/obj/.newest" 2> /dev/null
    then
        rm -f "$OUT/$t.linked" "$OUT/$t.exe"
        mv "$OUT/$t.link.rsp.new" "$OUT/$t.link.rsp"
        tc g++.exe "../$t.link.rsp" > "$OUT/$t.link.log" 2>&1 || { cat "$OUT/$t.link.log" >&2; exit 1; }
        touch "$OUT/$t.linked"
        echo "built $OUT/$t.exe" >&2
    else
        rm -f "$OUT/$t.link.rsp.new"
        echo "up to date: $OUT/$t.exe" >&2
    fi
done
if [ "${RENDER:-0}" = 1 ]; then   # native_play's renderer: b3render.dll and SDL3.dll beside it
    OUT="$OUT" "$PORT/tools/render/build_win.sh"
fi
