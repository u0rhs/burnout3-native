#!/bin/bash
# Linux-side check of the macOS build without a Mac (mac1): the self-contained tests (make mac-tests)
#   a64   built for aarch64 by clang 21 (tools/mac/xc/a64-clang++) and run under qemu-aarch64: the FPCR / NEON paths,
#         AArch64 float semantics (qemu's softfloat), with the retail-ELF oracle when RETAIL_ELF is given
#   lcx   built for x86-64 by clang 21 against LLVM libc++ (tools/mac/xc/x64-clang++-libcxx): Apple's C++ library
#         family (libstdc++ hides some bugs, e.g. the PtrArg snapshot order fixed in mac1)
#   lcx17 a -fsyntax-only pass of the Mac-built sources against libc++ 17 headers (closer to Xcode's library)
#   tools/mac/cross_check.sh [a64|lcx|lcx17|all] (default all); RETAIL_ELF=<disc>/SLUS_210.50 enables the oracle checks.
# Toolchain: tools/mac/toolchain-linux.sha256 (unpacked Ubuntu packages, nothing installed). Not covered: Apple's own
# clang / libc++ / ld64 / Mach-O, macOS ucontext, SDL3 on Cocoa + Metal: those only run on the Mac (docs/mac.md).
# The a64 build has no zlib, so test_hud / test_capture / test_livelap / test_inproc show NOT BUILT there.
WS=${B3_WORKSPACE:-$(dirname "$(dirname "$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)")")}   # the folder that holds this repo (its main checkout)
set -u
cd "$(dirname "$0")/../.." || exit 1
WHAT=${1:-all}
QEMU=${QEMU:-$WS/ext/qemu-user/usr/bin/qemu-aarch64}
LIM="prlimit --as=6442450944"
ELF=${RETAIL_ELF:-/nonexistent}
rc=0
if [ "$WHAT" = a64 ] || [ "$WHAT" = all ]; then
  echo "== a64: aarch64 clang 21 + qemu-aarch64 (BUILD=build/x-a64)"
  $LIM make -j4 BUILD=build/x-a64 DECOMP=/nonexistent GAME=/nonexistent RETAIL_ELF="$ELF" CXX="$PWD/tools/mac/xc/a64-clang++" \
    RUN="$LIM $QEMU" mac-tests 2>&1 | grep -E '^(exactness|other) |^== ' || true
  grep -q -E 'FAIL' build/x-a64/mac-tests/summary.txt && rc=1
fi
if [ "$WHAT" = lcx ] || [ "$WHAT" = all ]; then
  echo "== lcx: x86-64 clang 21 + libc++ 21 (BUILD=build/x-lcx)"
  $LIM make -j4 BUILD=build/x-lcx DECOMP=/nonexistent GAME=/nonexistent RETAIL_ELF="$ELF" CXX="$PWD/tools/mac/xc/x64-clang++-libcxx" \
    mac-tests 2>&1 | grep -E '^(exactness|other) |^== ' || true
  grep -q -E 'FAIL|NOT BUILT' build/x-lcx/mac-tests/summary.txt && rc=1
  $LIM make -j4 BUILD=build/x-lcx DECOMP=/nonexistent CXX="$PWD/tools/mac/xc/x64-clang++-libcxx" build/x-lcx/b3view build/x-lcx/b3replay \
    > build/x-lcx/viewers.log 2>&1 && echo "viewers: b3view, b3replay built (libc++)" || { echo "viewers: FAILED (build/x-lcx/viewers.log)"; rc=1; }
fi
if [ "$WHAT" = lcx17 ] || [ "$WHAT" = all ]; then
  echo "== lcx17: -fsyntax-only against libc++ 17 headers"
  n=0; bad=0
  for f in src/b3asset/*.cpp src/b3gfx/*.cpp src/b3view/*.cpp src/b3replay/*.cpp src/b3settings/*.cpp \
           src/b3host/host.cpp src/b3host/null_backend.cpp src/b3host/hw.cpp src/b3host/vubind.cpp src/b3host/native_backend.cpp \
           src/native/platform*.cpp src/native/save_card.cpp src/native/save_formats.cpp src/native/save_store.cpp src/native/save_stats.cpp src/audio/*.cpp src/port/*.cpp src/gameview/*.cpp \
           tools/native/gameview_native.cpp $(for t in $(make -s print-mac-tests); do ls tests/$t.cpp 2>/dev/null; done) tests/test_gtmath.cpp tests/test_eeasm.cpp; do
    [ -f "$f" ] || continue; n=$((n + 1))
    if ! out=$(LIBCXX_VER=17 $LIM tools/mac/xc/x64-clang++-libcxx -fsyntax-only -std=c++20 -w -Iinclude -Iinclude/gtmath -Isrc/native \
        -Isrc/b3gfx -Itests -Itools/native -Ibuild/x-lcx -I"$WS/ext/sdl3-3.4.2/usr/include" -DB3_PS2_FLOAT=1 "$f" 2>&1); then
      bad=$((bad + 1)); echo "  $f"; echo "$out" | grep error | head -3
    fi
  done
  echo "lcx17: $n files, $bad with errors"; [ "$bad" = 0 ] || rc=1
fi
exit $rc
