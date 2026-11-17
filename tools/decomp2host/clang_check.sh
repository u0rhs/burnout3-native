#!/usr/bin/env bash
# clang_check.sh GEN [JOBS]: every unit of a decomp2host generation through clang's front end (-fsyntax-only), as the
# Mac / Windows builds compile them (gen1, 2026-11-07: generation must be clang-clean; run it with every
# identity check). The compiler: $CLANG_CHECK_CXX, else the aarch64 cross clang (tools/mac/xc/a64-clang++, the Mac's
# target), else the x64 libc++ one, else clang++. Flags: the Makefile's for generated units under clang (GENFLAGS with
# GEN_CC_FLAGS = -Wno-address-of-temporary; NGENFLAGS' -DB3_BOUNCE_CANARY=1), PS2-faithful floats. Each tool run under
# prlimit --as=6G (the memory rule). Prints the failing units with their first error; exit 1 when any fails.
set -uo pipefail
gen=${1:?usage: clang_check.sh GEN [JOBS]}
jobs=${2:-4}
root=$(cd "$(dirname "$0")/../.." && pwd)
cxx=${CLANG_CHECK_CXX:-}
if [ -z "$cxx" ]; then
    for c in "$root/tools/mac/xc/a64-clang++" "$root/tools/mac/xc/x64-clang++-libcxx" clang++; do
        if echo 'int main(){}' | "$c" -x c++ -std=c++20 -fsyntax-only - 2>/dev/null; then cxx=$c; break; fi
    done
fi
[ -n "$cxx" ] || { echo "clang_check: no working clang (set CLANG_CHECK_CXX)" >&2; exit 2; }
out=$(mktemp -d "${TMPDIR:-$root/build/tmp}/clang_check.XXXXXX")
export cxx gen root out
ls "$gen"/units/*.cpp | xargs -P "$jobs" -I{} sh -c '
    u=$(basename {} .cpp)
    prlimit --as=6442450944 $cxx -std=c++20 -fsyntax-only -I"$root/include" -I"$root/include/gtmath" -I"$gen/include" \
        -DB3_PS2_FLOAT=1 -DB3_BOUNCE_CANARY=1 -ffp-contract=off -fno-fast-math -fno-strict-aliasing -fstrict-flex-arrays=0 \
        -fno-access-control -fpermissive -Wno-address-of-temporary -w {} > "$out/$u.log" 2>&1 || echo "$u" >> "$out/failed"'
n=$(ls "$gen"/units/*.cpp | wc -l)
if [ -s "$out/failed" ]; then
    sort "$out/failed" | while read -r u; do printf '%s\t%s\n' "$u" "$(grep -m1 'error' "$out/$u.log" | cut -c1-240)"; done
    echo "clang_check: $(wc -l < "$out/failed") of $n units fail ($cxx)"
    exit 1
fi
echo "clang_check: $n units clean ($cxx)"
