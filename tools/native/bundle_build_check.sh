#!/bin/bash
# bundle_build_check.sh -- the head checks' simulated Mac build (2026-11-07): native_play from the committed
# bundle (gen/native) with no decomp anywhere -- NATIVE_FROM=bundle, DECOMP / VU_DIR (the Makefile's only decomp
# fallbacks) pointing nowhere -- into its own build folder, with the compiler in CXX (for the Mac: clang + libc++,
# tools/mac/xc/x64-clang++-libcxx -- GCC's -fpermissive hides what clang refuses, mac1).
# vu0n (2026-11-10): the VU0 microprograms' C++ is committed too (gen/native/vu0/vu0_aot_gen.cpp), so one build: no
# disc, no decomp, and the program must link the four generated VU0 programs and no VU0 interpreter (b3asset::Vu1).
#
#   tools/native/bundle_build_check.sh [build dir]     (default build/bundle_check; JOBS=4, nice)
# Verdict (last line): "bundle_build_check: PASS ..." or "bundle_build_check: FAIL ..."; exit 0 / 1.
set -u
cd "$(dirname "$0")/../.." || exit 1
OUT=${1:-build/bundle_check}
mkdir -p "$OUT"
CXXBIN=${CXX:-g++}
case "$CXXBIN" in /*) ;; *) CXXBIN=$(command -v "$CXXBIN") ;; esac
SUMMARY_CXX=$(basename "${CXXBIN##* }")
log=$OUT/make_bundle.log
why=""
nice -n 5 \
    make -k -j"${JOBS:-4}" play NATIVE_FROM=bundle DECOMP=/nonexistent/decomp VU_DIR=/nonexistent/vu VU0_AOT_ELF= BUILD="$OUT/b" \
    CXX="$CXXBIN" > "$log" 2>&1
rc=$?
if [ "$rc" != 0 ]; then
    units=$(grep -oE '[^ :]+\.cpp:[0-9]+:[0-9]+: (fatal )?error' "$log" | sed 's|:.*||; s|.*/||' | sort -u)
    why="make exit $rc: $(grep -m1 -E ': error|undefined reference|No rule to make|Error [0-9]' "$log" | cut -c1-240)"
    [ -n "$units" ] && why="$why; units with errors: $(echo "$units" | wc -l) ($(echo $units | cut -c1-400))"
fi
[ -z "$why" ] && [ ! -x "$OUT/b/native_play" ] && why="no native_play"
if [ -z "$why" ]; then
    syms=$(nm -C "$OUT/b/native_play" 2>/dev/null)
    progs=$(echo "$syms" | grep 'vu0aot::(anonymous namespace)::run_00[0-9A-F]*(' | grep -vc '\[clone')
    interp=$(echo "$syms" | grep -c 'b3asset::Vu1::run(')
    [ "$progs" = 4 ] || why="${why:+$why; }$progs generated VU0 programs linked, not 4"
    [ "$interp" = 0 ] || why="${why:+$why; }the VU0 interpreter (b3asset::Vu1::run) is linked"
fi
if [ -z "$why" ]; then
    echo "bundle_build_check: PASS native_play from gen/native ($(cut -c1-20 gen/native/SNAPSHOT 2>/dev/null)) with no decomp and no disc, $SUMMARY_CXX; the 4 generated VU0 programs linked, no VU0 interpreter"
    exit 0
fi
echo "bundle_build_check: FAIL $why (log $log)"
exit 1
