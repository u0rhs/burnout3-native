#!/bin/bash
# more_rivals_grid_check.sh -- MORE RIVALS: every RACE route's start grid with 8 cars (the player + 7 rivals).
#
#   tools/native/more_rivals_grid_check.sh [native_play] [out dir] [first track] [last track]
#
# One race_check run per route of the game's track list (0..35: US, EU, AS; B3NATIVE_MORE_RIVALS_TEST_TRACK picks it
# in RequestOnePlayerGameMode), driven to the race's start; tools/native/more_rivals.h's TEST grid check
# (B3NATIVE_MORE_RIVALS_TEST_GRID=1) then tests every car: on a drivable lane segment (within 2.5 m of its height) and
# heading along it, 3 m from every other car (and, for information, 300 racing updates in, the cars off the road). A shot
# of the race (race_check's poll 11500) per route in <out>/shots/<n>.png.
# Verdict (last line): "more_rivals_grid_check: PASS 36 routes" or "... FAIL <routes>". Needs the env of race_check.
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/mr_grid}
FIRST=${3:-0}
LAST=${4:-35}
RIVALS=${RIVALS:-7}
mkdir -p "$OUT/shots"
fail=""
n=0
for t in $(seq "$FIRST" "$LAST"); do
  d="$OUT/t$t"
  B3NATIVE_MORE_RIVALS=$RIVALS B3NATIVE_MORE_RIVALS_TEST_TRACK=$t B3NATIVE_MORE_RIVALS_TEST_GRID=1 VBLANKS=${VBLANKS:-22000} \
    tools/native/race_check.sh "$NP" "$d" > "$d.log" 2>&1
  line=$(grep -a "more_rivals: grid check" "$d/native_play.log" 2>/dev/null | tail -1)
  spots=$(grep -a "more_rivals: grid:" "$d/native_play.log" 2>/dev/null | tail -1 | sed 's/.*more_rivals: grid: //')
  crash=$(grep -a -m1 "^crash: \(signal\|exception\|uncaught\)\|^fatal" "$d/native_play.log" 2>/dev/null | cut -c1-160)
  [ -f "$d/shots/live_11500_0.png" ] && cp "$d/shots/live_11500_0.png" "$OUT/shots/$t.png"
  n=$((n + 1))
  if [ -n "$crash" ] || [ -z "$line" ] || ! echo "$line" | grep -q ": PASS"; then
    fail="$fail $t"
    echo "track $t: FAIL ${line:-no grid check} ${crash:+[$crash]} | $spots"
  else
    echo "track $t: ${line#*grid check } | $spots"
  fi
  rm -rf "$d/card"   # the scratch memory card copy
done
if [ -z "$fail" ]; then echo "more_rivals_grid_check: PASS $n routes"; else echo "more_rivals_grid_check: FAIL$fail"; exit 1; fi
