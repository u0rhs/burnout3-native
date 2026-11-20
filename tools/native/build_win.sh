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
#   OUT=/mnt/c/.../play1 RENDER=1 tools/native/build_win.sh
#   STRIP=1: the programs linked with -s (no symbols: a release, tools/release/package_win.sh; a crash report's
#   offsets then need the unstripped build of the same commit)
#   NATIVE_FALLBACK=1: native_play.exe with the retail fallback (the Makefile's NATIVE_FALLBACK: developer / diagnostic
#   builds); the default 0 has no R5900 interpreter in it, as every release. native_boot.exe always has it.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../.." && pwd)
. "$PORT/tools/local_env.sh"; b3_local_env "$PORT"   # the machine's paths (local.env)
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
read -r -a SRCS <<< "$(mkvar NATIVE_SRC) $(mkvar HOST_SRC) $(mkvar GAMEVIEW_SRC) tools/native/gameview_native.cpp"
CHD_SRC=$(mkvar CHD_SRC)   # chd1: libchdr's C unit (b3asset/discfs.h's CHD images), compiled as C

S=$OUT/stage
mkdir -p "$S" "$OUT/obj"
# vu0n: gen/native/vu0 -- the committed VU0 microprograms as C++ (no VU0 interpreter in these programs: Makefile
# VU0_INTERP stays 0 here; the bundle's gen/native holds it)
for d in include src tests tools/native $GEN_DIRS sdk/include; do   # sdk2b: the mod API header
    mkdir -p "$S/$d"
    rsync -a --delete "$PORT/$d/" "$S/$d/"
done
mkdir -p "$S/build"   # version_info.cpp (a bundle build stages nothing else under build/)
# test builds: the version stamp (include/b3report/version.h), a generated source rewritten only when it changes
"$PORT/tools/release/version_info.sh" "$S/build/version_info.cpp"
WTC=$(wslpath -w "$TC")
printf '@echo off\r\nset "PATH=%s\\bin;%%SystemRoot%%\\system32;%%SystemRoot%%"\r\n"%s\\bin\\%%1" @%%2\r\n' "$WTC" "$WTC" > "$OUT/tc.cmd"
tc() { (cd "$S" && cmd.exe /c "$(wslpath -w "$OUT/tc.cmd")" "$1" "$2" < /dev/null); }   # cwd: the stage (relative -I)
m() { wslpath -m "$1"; }

# a changed header (or flag) rebuilds every object; else only sources newer than their object
STAMP=$OUT/obj/.stamp
printf '%s\n' "${FLAGS[@]}" > "$OUT/obj/.flags.new"
full=${FORCE:-0}
if [ ! -f "$STAMP" ] || ! cmp -s "$OUT/obj/.flags.new" "$OUT/obj/.flags" 2>/dev/null; then full=1
elif [ -n "$(find "$S/include" "$S/src" "$S/tests" "$S/tools/native" "$S/build" "$S/gen" "$S/sdk" -name '*.h' -newer "$STAMP" -print -quit 2>/dev/null)" ]; then full=1; fi
mv "$OUT/obj/.flags.new" "$OUT/obj/.flags"
touch "$STAMP"   # the staging time: a failed run's finished objects stay valid for the next run
# nofallback1: native_play.o is compiled with -DB3_NATIVE_FALLBACK=$NATIVE_FALLBACK; a change of it recompiles that one
NATIVE_FALLBACK=${NATIVE_FALLBACK:-0}
[ "$(cat "$OUT/obj/.native_fallback" 2>/dev/null)" = "$NATIVE_FALLBACK" ] || rm -f "$OUT/obj/native_play.o"
echo "$NATIVE_FALLBACK" > "$OUT/obj/.native_fallback"

running=0
compiled=0
compile() {   # compile <source relative to the stage> <object>
    if [ "$full" = 0 ] && [ -f "$2" ] && [ "$2" -nt "$S/$1" ] && [ "$(cat "$2.status" 2>/dev/null)" = ok ]; then return; fi
    local extra=()   # rt1: the units on either side of the VU0 float-environment switch (Makefile VU0_FP_ENV)
    case "$1" in *native/vu0_aot_gen.cpp|*native/vu0_unit.cpp) extra=(-frounding-math);; *native/script_lua.cpp) extra=(-w);;   # lua6: the vendored Lua unit
                 tools/native/native_play.cpp) extra=(-DB3_NATIVE_FALLBACK="$NATIVE_FALLBACK");; esac
    local cc=g++.exe flags=("${FLAGS[@]}")
    case "$1" in *.c) cc=gcc.exe; flags=(-std=gnu11 -O2 -DNDEBUG -w);; esac   # chd1: the C unit (libchdr); NDEBUG: no assert() file paths (the build folder) in a release
    printf '%s\n' "${flags[@]}" "${extra[@]}" -c "$1" -o "$(m "$2")" > "$2.rsp"
    # WSL interop can fail to start a Windows process ("UtilAcceptVsock ... failed"): retried, unlike a compile error
    ( for try in 1 2 3; do
          if tc "$cc" "$(m "$2.rsp")" > "$2.log" 2>&1; then echo ok > "$2.status"; exit 0; fi
          grep -q 'UtilAcceptVsock\|WSL .*ERROR' "$2.log" || break
      done; echo failed > "$2.status" ) &
    running=$((running + 1)); compiled=$((compiled + 1))
    if [ "$running" -ge "${JOBS:-4}" ]; then wait -n || true; running=$((running - 1)); fi
}
common=()   # the platform tier and the programs first: their errors show in minutes
for s in "${SRCS[@]}"; do o=$OUT/obj/$(echo "${s%.cpp}" | tr / _).o; compile "$s" "$o"; common+=("$o"); done
o=$OUT/obj/$(echo "${CHD_SRC%.c}" | tr / _).o; compile "$CHD_SRC" "$o"; common+=("$o")
for t in native_boot native_play; do compile "tools/native/$t.cpp" "$OUT/obj/$t.o"; done
compile build/version_info.cpp "$OUT/obj/version_info.o"; common+=("$OUT/obj/version_info.o")
objs=()
for s in "$S/$UNITS"/*.cpp; do o=$OUT/obj/gen_$(basename "$s" .cpp).o; compile "$UNITS/$(basename "$s")" "$o"; objs+=("$o"); done
wait || true
echo "compiled $compiled objects (${#objs[@]} generated units in all)" >&2
bad=0
for o in "${objs[@]}" "${common[@]}" "$OUT/obj/native_boot.o" "$OUT/obj/native_play.o"; do
    if [ ! -f "$o" ] || [ "$(cat "$o.status" 2>/dev/null)" = failed ] || grep -q ' error: ' "$o.log" 2>/dev/null; then echo "compile failed: $o" >&2; grep -m5 'error' "$o.log" >&2 || true; bad=1; fi
done
[ "$bad" = 0 ] || exit 1
# the programs' icon (assets/icon/b3-icon.ico, made by tools/release/make_icons.py): a resource object linked into the .exe
printf '1 ICON "%s"\n' "$(m "$PORT/assets/icon/b3-icon.ico")" > "$OUT/obj/b3icon.rc"
printf '%s\n' -O coff -i "$(m "$OUT/obj/b3icon.rc")" -o "$(m "$OUT/obj/b3icon.res.o")" > "$OUT/obj/b3icon.rsp"
tc windres.exe "$(m "$OUT/obj/b3icon.rsp")" > "$OUT/obj/b3icon.log" 2>&1 || { cat "$OUT/obj/b3icon.log" >&2; exit 1; }
for t in native_boot native_play; do
    # 16 MB stacks: the main thread's reserve (Linux: 8 MB by ulimit), and the default for threads that ask for none
    printf '%s\n' -static -O2 -Wl,--stack,16777216 $([ "${STRIP:-0}" = 1 ] && echo -s) -o "$(m "$OUT/$t.exe")" "$(m "$OUT/obj/$t.o")" > "$OUT/$t.link.rsp"
    for o in "${common[@]}" "${objs[@]}"; do m "$o" >> "$OUT/$t.link.rsp"; done
    [ "$t" = native_play ] && m "$OUT/obj/b3icon.res.o" >> "$OUT/$t.link.rsp"   # the B3 icon
    printf '%s\n' -lz -lwinmm -pthread >> "$OUT/$t.link.rsp"   # zlib: was the retired snapshot restore's gzipped SIF log; unused now (harmless)
    tc g++.exe "$(m "$OUT/$t.link.rsp")" > "$OUT/$t.link.log" 2>&1 || { cat "$OUT/$t.link.log" >&2; exit 1; }
    echo "built $OUT/$t.exe" >&2
done
if [ "${RENDER:-0}" = 1 ]; then   # native_play's renderer: b3render.dll and SDL3.dll beside it
    OUT="$OUT" "$PORT/tools/render/build_win.sh"
fi
