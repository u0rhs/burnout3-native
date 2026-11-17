#!/bin/bash
# replay_check.sh -- replay1 (remaster-features §5 "Replay files", src/native/replay_file.h): a session recorded as a
# replay file plays back exactly. Per scenario, two runs of tools/golden/baseline.sh (its logs: the EE kernel time
# trace, the DMA / SIF / RW Audio logs, guest RAM hashes per MB every 100 VBlanks, the shots):
#   rec    the scenario as race_check runs it (scripted keys), with RECORD_REPLAY: native_play --record-replay
#   play   the same scenario run with REPLAY: native_play --replay of that file (its pads, card and options replace the
#          scenario's own; the scripted keys go to the window's pad, which a replay ignores)
# then tools/golden/compare.sh rec play (every item equal) and native_play's own checkpoint line of the playback
# ("replay: N of N checkpoints matched; it played as recorded").
#
#   tools/native/replay_check.sh [native_play] [out dir]      SCENARIOS (default "race crash"), B3_GAME_DIR, SAVES
#
# One native_play run at a time. Last line: "replay_check: PASS <scenarios> ..." or "replay_check: FAIL <why>".
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/replay_check}
case $NP in /*) ;; *) NP=$PWD/$NP ;; esac
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
SCEN=${SCENARIOS:-race crash}
fail=""
summary=""
cat_of() { if [ "${1%.gz}" != "$1" ]; then zcat "$1" 2>/dev/null; else cat "$1" 2>/dev/null; fi; }
hash_of() { cat_of "$1" | sha256sum | cut -c1-16; }
lines_of() { cat_of "$1" | wc -l; }
for s in $SCEN; do
  d=$OUT/$s
  rm -rf "$d"; mkdir -p "$d"
  RECORD_REPLAY=$d/session.b3replay SCENARIOS=$s JOBS=1 RATE_RUNS=0 tools/golden/baseline.sh "$NP" "$d/rec" > "$d/rec.txt" 2>&1
  echo "replay_check: $s recorded: $(tail -1 "$d/rec.txt" | cut -c1-160)"
  if [ ! -s "$d/session.b3replay" ]; then echo "replay_check: $s: FAIL no replay file written"; fail="$fail $s:file"; continue; fi
  REPLAY=$d/session.b3replay SCENARIOS=$s JOBS=1 RATE_RUNS=0 tools/golden/baseline.sh "$NP" "$d/play" > "$d/play.txt" 2>&1
  echo "replay_check: $s played: $(tail -1 "$d/play.txt" | cut -c1-160)"
  grep -h -o 'native_play: replay: .* written to .*' "$d/rec/$s/tail.log" | sed "s|^|replay_check: $s |"
  CHK=$(grep -h -o 'native_play: replay: [0-9]* of [0-9]* checkpoints matched; .*' "$d/play/$s/tail.log")
  echo "replay_check: $s ${CHK:-no checkpoint line in the playback log}"
  case $CHK in *" 0 of 0 "*|"") fail="$fail $s:checkpoints" ;; *"it played as recorded"*) ;; *) fail="$fail $s:checkpoints" ;; esac
  # the logs' hashes, side by side (equal hashes: the same timeline)
  for f in trace.txt dma.log sif.log.gz rwa.log.gz ram.txt shots.txt; do
    a=$(hash_of "$d/rec/$s/$f"); b=$(hash_of "$d/play/$s/$f"); n=$(lines_of "$d/rec/$s/$f")
    if [ "$a" = "$b" ]; then same=equal; else same=DIFFERENT; fi
    echo "replay_check: $s ${f%.gz} ($n lines): recorded $a, replayed $b $same"
  done
  tools/golden/compare.sh "$d/rec" "$d/play" > "$d/compare.txt" 2>&1
  sed "s|^|replay_check: $s |" "$d/compare.txt"
  tail -1 "$d/compare.txt" | grep -q '^compare: PASS' || fail="$fail $s:compare"
  summary="$summary $s"
done
if [ -n "$fail" ]; then echo "replay_check: FAIL$fail"; exit 1; fi
echo "replay_check: PASS$summary: each replay played back with every compared log equal to its recording's (in $OUT)"
exit 0
