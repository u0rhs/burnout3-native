#!/bin/bash
# perf_check.sh -- the render-cost guard: one fixed race scene and one crash scene drawn offscreen with the frame profiler on
# (B3_PERF_LOG, docs/native-renderer.md section 34), once with the defaults and once per feature switched off, so a feature that
# costs frames shows as a row against the baseline before it ships.
#
#   tools/render/perf_check.sh [native_play] [out dir]        (default build/native_play, build/perf_check)
#   Environment: SCENES="race crash" (race_check.sh scenarios), VARIANTS (default: all, below), SIZE=1280x360 (the bench size;
#   CPU numbers are per frame, the GPU's are not measurable offscreen), MSAA=8, plus race_check.sh's B3_GAME_DIR / SAVES /
#   LD_LIBRARY_PATH.
# Variants: base | no-bloom | no-reflections | no-particles | no-coronas | no-blur | bloom-split (the old MSAA form, B3_BLOOM_SPLIT=1)
# Per scene and variant a row: fps, the render thread's CPU ms per frame (drawlist + cmd_scene + cmd_post + cmd_overlay + upload,
# with each), and draws / passes / submits / uploads / upload KB per frame (steady seconds). Passes and upload KB are what the
# GPU pays for on a machine we cannot measure here. The last line is "perf_check: done"; with BUDGET_MS=<ms> set the base variant's
# CPU sum above it is "perf_check: FAIL" (exit 1). Offscreen only; one native_play at a time.
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/perf_check}
SCENES=${SCENES:-"race crash"}
SIZE=${SIZE:-1280x360}
MSAA=${MSAA:-8}
VARIANTS=${VARIANTS:-"base no-bloom no-reflections no-particles no-coronas no-blur bloom-split"}
mkdir -p "$OUT"
fail=0
for sc in $SCENES; do
  echo "== scene $sc ($SIZE, MSAA $MSAA) =="
  for v in $VARIANTS; do
    extra=""
    split=""
    case $v in
      base) ;;
      no-bloom) extra="--no-bloom" ;;
      no-reflections) extra="--car-reflections off" ;;
      no-particles) extra="--no-particles" ;;
      no-coronas) extra="--no-coronas" ;;
      no-blur) extra="--no-blur" ;;
      bloom-split) split=1 ;;
      *) echo "perf_check: unknown variant $v"; exit 1 ;;
    esac
    log=$OUT/${sc}_$v.perf.txt
    rm -f "$log"
    d=$OUT/${sc}_$v
    if ! env ${split:+B3_BLOOM_SPLIT=1} B3_PERF_LOG="$log" SCENARIO=$sc STRICT=1 \
         RENDER_EXTRA="--bench $SIZE --msaa $MSAA $extra" tools/native/race_check.sh "$NP" "$d" > "$d.log" 2>&1; then
      echo "  $v: race_check failed (see $d.log)"
      fail=1
      continue
    fi
    python3 tools/render/perf_check_table.py "$v" "$log" "${BUDGET_MS:-0}" || fail=1
  done
done
if [ $fail = 0 ]; then echo "perf_check: done"; else echo "perf_check: FAIL"; exit 1; fi
