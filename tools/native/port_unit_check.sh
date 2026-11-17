#!/bin/bash
# port_unit_check.sh -- generate and compile port-track C files on their own (movie2, 2026-11-07): every `// 0xADDR`
# marker of the given files (src/libs/<track>/*.cpp) through decomp2host into a scratch directory, then each unit
# compiled as the native build compiles it (-O1, the native generation's flags). Seconds instead of a native_boot
# rebuild; does not touch build/. Proof of behaviour stays the boot oracle (native_boot --oracle ADDR).
#
#   tools/native/port_unit_check.sh OUTDIR src/libs/sce/libmpeg_dec.cpp [more files of the same track]
#
# DECOMP: the decomp snapshot (default the Makefile's, ../decomp-snap/boot2c).
WS=${B3_WORKSPACE:-$(dirname "$(dirname "$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)")")}   # the folder that holds this repo (its main checkout)
set -u
out="$1"; shift
[ $# -ge 1 ] || { echo "usage: $0 OUTDIR FILE.cpp..." >&2; exit 2; }
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root" || exit 2
decomp="${DECOMP:-$WS/decomp-snap/boot2c}"
track="$(basename "$(dirname "$1")")"
mkdir -p "$out"
grep -hoE "^// 0x[0-9A-F]{8}$" "$@" | sed "s|// 0x||; s/\$/ $track/" > "$out/select.txt"
echo "port_unit_check: $(wc -l < "$out/select.txt") functions of track $track"
DECOMP2HOST_CXX="${CXX:-g++}" DECOMP2HOST_T31C=1 DECOMP2HOST_CACHE="$root/build/d2h_cache" \
    python3 -I tools/decomp2host/decomp2host.py --decomp "$decomp" --out "$out/gen" --select "$out/select.txt" \
    > "$out/gen.log" 2>&1
rc=$?
grep -E "functions from|rror" "$out/gen.log" | tail -5
[ $rc -eq 0 ] || { echo "port_unit_check: decomp2host failed (exit $rc), see $out/gen.log"; exit 1; }
fail=0
for u in "$out"/gen/units/*.cpp; do
    if ${CXX:-g++} -Iinclude -Iinclude/gtmath -std=c++20 -Wall -Wextra -Werror=return-type -ffp-contract=off \
        -fno-fast-math -O1 -ftrivial-auto-var-init=zero -fno-strict-aliasing -fno-aggressive-loop-optimizations -flifetime-dse=1 \
        -fstrict-flex-arrays=0 -fno-access-control -fpermissive -w -I"$out/gen/include" -DB3_BOUNCE_CANARY=1 \
        -DB3_PS2_FLOAT=1 -c "$u" -o "$out/$(basename "$u" .cpp).o" 2> "$out/$(basename "$u" .cpp).err"; then
        echo "  ok   $(basename "$u")"
    else
        echo "  FAIL $(basename "$u") (first errors:)"; grep -m5 "error" "$out/$(basename "$u" .cpp).err" | cut -c1-240
        fail=1
    fi
done
exit $fail
