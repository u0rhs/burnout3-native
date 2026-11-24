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
#   REC_ENV="VAR=value ...": environment for the recording only (crashramfix: REC_ENV=B3NATIVE_CRASH_RAM_FIX=0 records as a
#   build before the fix did, so the playback must take the old path from the replay's missing entry)
#   PLAY_ENV="VAR=value ...": the same for the playback only (crashramfix: PLAY_ENV=B3NATIVE_CRASH_RAM_FIX=0 must not change a
#   replay that recorded the fix)
#
# sparkdet (2026-11-22): the shots are compared with SHOT_TOL (default 64:8:4: at most 64 pixels differing by at most 8
# per channel, plus at most 4 "specks" differing by more; SHOT_TOL= for exact), reported "within tolerance (N px, max d;
# K specks)". The test machines draw with lavapipe (WSL's software Vulkan driver), which draws a crash's spark burst
# slightly differently from identical input, 1 playback in 5: faint pixels (<= 7 levels) on the sparks' edges and a few
# 1-pixel specks present or absent (~50 levels). A full dump of every offscreen frame's input (every draw item's and
# frame parameter's field, the content of each texture and buffer drawn, the draw ranges) was equal in differing and
# equal runs; clearing the don't-care targets and LP_NUM_THREADS=0 did not change it. The logs (the game itself) are
# still compared exactly. The PNGs are kept for that compare (baseline.sh KEEP_SHOTS=1) and deleted after it.
# flakenet: a playback whose logs part from its recording's is kept in compare.sh's flake net (tag "replay", with the
# recording and the .b3replay); the raw traces are deleted where they are equal (KEEP_TRACE=1: kept).
# One native_play run at a time. Last line: "replay_check: PASS <scenarios> ..." or "replay_check: FAIL <why>".
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/replay_check}
case $NP in /*) ;; *) NP=$PWD/$NP ;; esac
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
SCEN=${SCENARIOS:-race crash}
# raceopts: the SELECT OPTIONS rows on, as a player has them (baseline.sh pins them off for the golden record)
export B3NATIVE_RACE_OPTS=${B3NATIVE_RACE_OPTS:-1}
fail=""
summary=""
cat_of() { if [ "${1%.gz}" != "$1" ]; then zcat "$1" 2>/dev/null; else cat "$1" 2>/dev/null; fi; }
hash_of() { cat_of "$1" | sha256sum | cut -c1-16; }
lines_of() { cat_of "$1" | wc -l; }
for s in $SCEN; do
  d=$OUT/$s
  rm -rf "$d"; mkdir -p "$d"
  env ${REC_ENV:-} KEEP_SHOTS=1 RECORD_REPLAY=$d/session.b3replay SCENARIOS=$s JOBS=1 RATE_RUNS=0 tools/golden/baseline.sh "$NP" "$d/rec" > "$d/rec.txt" 2>&1
  echo "replay_check: $s recorded: $(tail -1 "$d/rec.txt" | cut -c1-160)"
  if [ ! -s "$d/session.b3replay" ]; then echo "replay_check: $s: FAIL no replay file written"; fail="$fail $s:file"; continue; fi
  env ${PLAY_ENV:-} KEEP_SHOTS=1 REPLAY=$d/session.b3replay SCENARIOS=$s JOBS=1 RATE_RUNS=0 tools/golden/baseline.sh "$NP" "$d/play" > "$d/play.txt" 2>&1
  echo "replay_check: $s played: $(tail -1 "$d/play.txt" | cut -c1-160)"
  grep -h -o 'native_play: replay: .* written to .*' "$d/rec/$s/tail.log" | sed "s|^|replay_check: $s |"
  CHK=$(grep -h -o 'native_play: replay: [0-9]* of [0-9]* checkpoints matched; .*' "$d/play/$s/tail.log")
  # (the line can sit before the log's last 40: a scenario that logs on after the playback ends, e.g. finishcam's set
  # pieces; then the whole log)
  [ -z "$CHK" ] && CHK=$(cat_of "$d/play/$s/native_play.log.gz" | grep -o 'native_play: replay: [0-9]* of [0-9]* checkpoints matched; .*' | tail -1)
  echo "replay_check: $s ${CHK:-no checkpoint line in the playback log}"
  case $CHK in *" 0 of 0 "*|"") fail="$fail $s:checkpoints" ;; *"it played as recorded"*) ;; *) fail="$fail $s:checkpoints" ;; esac
  # the logs' hashes, side by side (equal hashes: the same timeline)
  for f in trace.txt dma.log sif.log.gz rwa.log.gz ram.txt shots.txt; do
    a=$(hash_of "$d/rec/$s/$f"); b=$(hash_of "$d/play/$s/$f"); n=$(lines_of "$d/rec/$s/$f")
    if [ "$a" = "$b" ]; then same=equal; else same=DIFFERENT; fi
    echo "replay_check: $s ${f%.gz} ($n lines): recorded $a, replayed $b $same"
  done
  SHOT_TOL=${SHOT_TOL-64:8:4} FLAKE_TAG=replay FLAKE_WITH_OLD=1 FLAKE_EXTRA="$d/session.b3replay" tools/golden/compare.sh "$d/rec" "$d/play" > "$d/compare.txt" 2>&1
  rm -rf "$d/rec/$s/shot_png" "$d/play/$s/shot_png"
  [ "${KEEP_TRACE:-0}" = 1 ] || rm -f "$d/rec/$s"/trace*.raw.gz
  sed "s|^|replay_check: $s |" "$d/compare.txt"
  tail -1 "$d/compare.txt" | grep -q '^compare: PASS' || fail="$fail $s:compare"
  summary="$summary $s"
done
if [ -n "$fail" ]; then echo "replay_check: FAIL$fail"; exit 1; fi
echo "replay_check: PASS$summary: each replay played back with every compared log equal to its recording's (in $OUT)"
exit 0
