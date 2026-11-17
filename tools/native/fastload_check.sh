#!/bin/bash
# fastload_check.sh -- FAST LOADING (tools/native/fast_load.h) changes no VBlank of the game, only the wall time of
# its loading screens (fastload1). One race_check scenario is recorded (its pad reports), then replayed three times
# offscreen from that recording: unpaced (as every check runs), paced to real time with FAST LOADING off (what a window
# does without it), and paced with it on. All three must give the same game: the DMA, SIF and RW Audio logs line for
# line, guest RAM's per-MB hashes on a grid, the screenshots and the pad reports; the paced runs' wall times per
# loading screen give the saving.
#
#   tools/native/fastload_check.sh [native_play] [out dir]
#   Environment: SCENARIO (default crashresults: into a race, the crash, the results and back to the menus), RAM_GRID
#   (default 1-100000:100), B3_GAME_DIR, SAVES (as race_check.sh), VBLANKS (the scenario's own by default).
# Last line: "fastload_check: PASS ..." or "fastload_check: FAIL <what>". Exit 0 / 1. The paced runs take real time
# (crashresults: 33,000 VBlanks, 9 min with FAST LOADING off).
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/fastload_check}
case $NP in /*) ;; *) NP=$PWD/$NP ;; esac
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
. tools/local_env.sh; b3_local_env .
GAME=${B3_GAME_DIR:-${GAME:-data/game}}
SAVES=${SAVES:-}
SC=${SCENARIO:-crashresults}
RAM_GRID=${RAM_GRID:-1-100000:100}
[ -x "$NP" ] || { echo "fastload_check: FAIL no $NP"; exit 1; }
mkdir -p "$OUT"
SB=""   # line-buffered stdout (a RAM-hash line is never split by another thread's output)
for f in /usr/libexec/coreutils/libstdbuf.so /usr/lib/coreutils/libstdbuf.so; do [ -f "$f" ] && SB=$f && break; done

# 1. the recording: race_check's own run (unpaced, scripted keys)
rm -rf "$OUT/rec"
SCENARIO=$SC STRICT=1 tools/native/race_check.sh "$NP" "$OUT/rec" > "$OUT/rec.txt" 2>&1
verdict=$(tail -1 "$OUT/rec.txt")
case $verdict in "race_check: PASS"*) ;; *) echo "fastload_check: FAIL the recording run: ${verdict:0:200}"; exit 1 ;; esac
PADS=$OUT/rec/pads.txt
VBL=$(grep -o '^native_play: [0-9]* VBlanks in' "$OUT/rec/native_play.log" | awk '{print $2}' | tail -1)
VBL=${VBLANKS:-$VBL}
SHOTS=$(grep -o -- '--shots [0-9,]*' "$OUT/rec/native_play.log" | head -1 | awk '{print $2}')
[ -n "$VBL" ] && [ -s "$PADS" ] || { echo "fastload_check: FAIL no pad recording / VBlank count in $OUT/rec"; exit 1; }
echo "fastload_check: recorded $SC ($VBL VBlanks, shots $SHOTS)"

# 2. the replays
replay() {   # replay <name> <native_play options...>
  local n=$1 d=$OUT/$1; shift
  rm -rf "$d"; mkdir -p "$d/shots" "$d/card"
  rsync -a --delete "$SAVES/" "$d/card/"
  local t0; t0=$(date +%s.%N)
  env ${SB:+LD_PRELOAD=$SB _STDBUF_O=L} B3NATIVE_MAX_VBLANKS="$VBL" B3HOST_FATAL_ABORT=1 B3NATIVE_SAVES_ROOT="$d/card" \
    B3NATIVE_CARD_PRESENT_VBLANK=270 B3NATIVE_RAM_HASH="$RAM_GRID" B3NATIVE_SIF_LOG="$d/sif.log" B3NATIVE_RWA_LOG="$d/rwa.log" \
    B3NATIVE_DMA_LOG="$d/dma.log" prlimit --as=10737418240 "$NP" --game "$GAME" --offscreen --strict --replay-pad "$PADS" \
    --record-pad "$d/pads.txt" --render-args "--offscreen-live $d/shots --shots $SHOTS" "$@" > "$d/native_play.log" 2>&1
  echo "rc $?" > "$d/rc"
  awk -v a="$(date +%s.%N)" -v b="$t0" 'BEGIN { printf "%.2f\n", a - b }' > "$d/wall"
  grep -o '^native_play: VBlank [0-9]*: ram\( [0-9a-f]\{8\}\)*' "$d/native_play.log" > "$d/ram.txt"
  (cd "$d/shots" && sha256sum live_*_0.png 2>/dev/null) > "$d/shots.txt"
  grep '^native_play: VBlank [0-9]*: loading screen gone' "$d/native_play.log" > "$d/loads.txt"
  echo "fastload_check: $n: $(cat "$d/rc"), $(printf '%.1f' "$(cat "$d/wall")") s wall, $(wc -l < "$d/loads.txt") loading screens, $(wc -l < "$d/ram.txt") RAM hashes, $(wc -l < "$d/shots.txt") shots"
}
replay unpaced --no-pace
replay paced_off --fast-loading off
replay paced_on --fast-loading on

# 3. the comparison: everything the game did, the three runs equal
fail=""
for d in paced_off paced_on; do
  grep -q '^rc 0' "$OUT/$d/rc" || fail="$fail $d:rc"
  for f in ram.txt shots.txt dma.log sif.log rwa.log pads.txt; do
    if cmp -s "$OUT/unpaced/$f" "$OUT/$d/$f"; then :; else fail="$fail $d:$f"; fi
  done
  # the loading screens: the same VBlanks (the wall times differ)
  cmp -s <(sed 's/ in [0-9.]* s wall.*//' "$OUT/unpaced/loads.txt") <(sed 's/ in [0-9.]* s wall.*//' "$OUT/$d/loads.txt") || fail="$fail $d:loads"
done
grep -q '^rc 0' "$OUT/unpaced/rc" || fail="$fail unpaced:rc"
[ -s "$OUT/unpaced/ram.txt" ] || fail="$fail no-ram-hashes"
for f in ram.txt shots.txt dma.log sif.log rwa.log pads.txt; do
  echo "fastload_check: $f: $(wc -l < "$OUT/unpaced/$f") lines, $(cmp -s "$OUT/unpaced/$f" "$OUT/paced_off/$f" && echo equal || echo DIFFERENT) paced off, $(cmp -s "$OUT/unpaced/$f" "$OUT/paced_on/$f" && echo equal || echo DIFFERENT) paced on"
done
# the saving per loading screen (paced off against paced on)
paste -d'|' "$OUT/paced_off/loads.txt" "$OUT/paced_on/loads.txt" | while IFS='|' read -r a b; do
  v=$(echo "$a" | grep -o 'VBlank [0-9]*' | head -1); n=$(echo "$a" | grep -o 'after [0-9]* VBlanks' | awk '{print $2}')
  wa=$(echo "$a" | grep -o 'in [0-9.]* s wall' | awk '{print $2}'); wb=$(echo "$b" | grep -o 'in [0-9.]* s wall' | awk '{print $2}')
  echo "fastload_check: loading screen ending at $v: $n VBlanks; paced $wa s, fast loading $wb s"
done
wo=$(cat "$OUT/paced_off/wall"); wn=$(cat "$OUT/paced_on/wall")
for d in rec unpaced paced_off paced_on; do rm -rf "$OUT/$d/card" "$OUT/$d/shots"; gzip -f "$OUT/$d"/*.log 2>/dev/null; done
if [ -n "$fail" ]; then echo "fastload_check: FAIL$fail"; exit 1; fi
echo "fastload_check: PASS $SC: unpaced, paced and paced with FAST LOADING give the same game (DMA, SIF, RW Audio, RAM, shots, pads); wall $(printf '%.1f' "$wo") s -> $(printf '%.1f' "$wn") s"
exit 0
