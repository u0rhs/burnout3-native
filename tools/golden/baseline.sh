#!/bin/bash
# baseline.sh -- the golden recording (docs/specs/): everything one native_play
# build does that a platform change must keep, recorded so that tools/golden/compare.sh can compare two builds.
#
#   tools/golden/baseline.sh <native_play> <out dir>
#
# Per scenario (tools/native/race_check.sh SCENARIO=<s> STRICT=1, libb3render.so beside native_play), into <out>/<s>:
#   verdict       race_check's last line (must be PASS)
#   shots.txt     race_check's shot hashes (SHA-256 of each screenshot); photo.txt the polls shot in photo mode (the
#                 free camera moves by host time: never compared)
#   trace.txt     B3NATIVE_TIME_TRACE over the EE kernel rows (docs/specs/ee-kernel.md §12), digested: per window
#                 of TRACE_WINDOW VBlanks the number of calls and the SHA-1 of their lines (VBlank, virtual cycles at
#                 entry and exit, arguments, result), then per address the number of calls. The raw trace (tens of
#                 millions of lines in a race) is kept gzipped (trace.raw.gz, a few MB) for compare.sh's flake net,
#                 which deletes it where the logs are equal; KEEP_TRACE=0: deleted after digesting.
#   dma.log       B3NATIVE_DMA_LOG (per VBlank the DMA completions and DMAC callbacks, hashed with their clock)
#   sif.log       B3NATIVE_SIF_LOG (the EE<->IOP traffic, stamped with the VBlank and the EE cycles), gzipped
#   rwa.log       B3NATIVE_RWA_LOG (RW Audio's commands and replies), gzipped
#   ram.txt       B3NATIVE_RAM_HASH=$RAM_GRID: an FNV-1a hash of each MB of guest RAM at those VBlanks
#   tail.log      the last 40 lines of native_play's log
#   native_play.log.gz   determ1: the whole log (each 600 VBlanks' wall rate, the loads: a run that differs from the
#                 reference can be told apart by its timing). The shots (rc/) are deleted unless the verdict is not
#                 PASS; a recording compare.sh fails is kept apart by compare.sh (KEEP_FAILED). KEEP_SHOTS=1: a passing
#                 run's shot PNGs are kept in shot_png/ (replay_check: for compare.sh's SHOT_TOL)
# and <out>/rate.txt: RATE_RUNS unpaced runs of the plain race scenario (no trace, no logs) -- race_check's "last
# windows N VBlanks/s", one per line; <out>/build.txt: the binary's path, SHA-256 and the tree's git description.
#
# The reference look: rec_det was recorded before CAR REFLECTIONS existed, i.e. with the game's own car env pass. The
# renderer's default is now Enhanced, so this script exports B3_CAR_REFLECTIONS=original (the PS2 ORIGINAL preset's value,
# `car_reflections = 1` in the settings ini) unless it is already set; tools/golden/compare.sh against the reference then
# needs nothing else. Run it with B3_CAR_REFLECTIONS=enhanced to see which items the new default changes.
#
# Environment:
#   SCENARIOS     default: the scenarios and tours of CONTRIBUTING.md ("Checks a change must pass") plus layer_check's "movie" (the race with the movie skip)
#   TRACE         the traced row addresses, comma separated (default: the 41 kernel rows of ee-kernel.md §12). A build
#                 whose time_trace.h has fewer slots than addresses (32 in older builds) is traced in groups:
#                 TRACE_SLOTS=32 runs one extra trace-only run per further group (trace2.txt, ...)
#   TRACE_SLOTS   default 64
#   TRACE_WINDOW  default 100 (VBlanks per digest line)
#   RAM_GRID      default 1-100000:100
#   RATE_RUNS     default 3 (0: none)
#   JOBS          native_play runs at once, default 2 (at most 2 for this tool on a typical desktop)
#   B3_GAME_DIR, SAVES   as race_check.sh (local.env)
#   B3_PHOTO_ROW=0 is exported (photorow1: no PHOTO MODE row in the pause menu, so the pause menu frames stay the game's own),
#   and B3_RACE_TIME_LABEL=0 (todrace: no "Time of day" prompt on the track pages; the choice itself still works),
#   B3NATIVE_CAR_STATS=0 (carstats1: SELECT CAR's own two stats, the hooks not installed), and
#   B3NATIVE_RACE_OPTS=0 unless set (raceopts: SELECT OPTIONS without the port's rows: the game drawing two more rows
#   costs EE time, which moves every later VBlank's work; the record is the game's own page; to be re-recorded with the
#   rows on at the next release, then this pin goes)
# Last line: "baseline: PASS recorded <n> scenarios in <out>" or "baseline: FAIL <scenarios>" (a run that did not PASS
# is still recorded: compare.sh then reports it).
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:?usage: baseline.sh <native_play> <out dir>}
OUT=${2:?usage: baseline.sh <native_play> <out dir>}
case $NP in /*) ;; *) NP=$PWD/$NP ;; esac
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
[ -x "$NP" ] || { echo "baseline: FAIL no $NP"; exit 1; }
. tools/local_env.sh; b3_local_env .
export B3_GAME_DIR=${B3_GAME_DIR:-${GAME:-data/game}}
export SAVES=${SAVES:-}
# photorow1: the pause menu without the PHOTO MODE row (the golden frames are the game's own menu; the setting is on by default)
export B3_PHOTO_ROW=0
export B3NATIVE_TRAFFIC_CHECK=${B3NATIVE_TRAFFIC_CHECK:-0}   # trafficcheck: TRAFFIC CHECKING off whatever the settings say
# settingsback: Back on the game's SETTINGS pages cancels, as the game's (the tours leave pages with Back; Keep changes is
# the setting's default)
export B3NATIVE_SETTINGS_BACK=${B3NATIVE_SETTINGS_BACK:-0}
# raceopts (tools/native/race_options.h): SELECT OPTIONS as the game's own (the rows are on by default); replay_check sets 1
export B3NATIVE_RACE_OPTS=${B3NATIVE_RACE_OPTS:-0}
export B3NATIVE_GAME_OPTIONS=${B3NATIVE_GAME_OPTIONS:-0}   # gameopts: GAMEPLAY's MIRROR MODE / CRASHBREAKER off whatever the settings say (replay_check sets them)
# titlefast (tools/native/title_fast.h): the title's two-second wait before PRESS START, as the record has it (the prompt
# drawn earlier costs EE time and moves every later VBlank); removed at the next re-record
export B3NATIVE_TITLE_FAST=${B3NATIVE_TITLE_FAST:-0}
export B3_CAR_REFLECTIONS=${B3_CAR_REFLECTIONS:-original}   # the reference look (see the header)
export B3_CAR_ENV=${B3_CAR_ENV:-sky}                          # reflect-tunnel: the car env map without the track (the record predates it)
export B3_PARTICLES=${B3_PARTICLES:-off}                      # effects3: the particle layer (smoke, dust, glass, fire) not drawn: the record predates it
export B3_BLOOM_LEVEL=${B3_BLOOM_LEVEL:-standard}            # bloom2: the game's own bloom (the PC default is Soft)
export B3_BLOOM_SPLIT=${B3_BLOOM_SPLIT:-1}                   # bloom3: the original bloom pass form (the lean MSAA form differs by rounding)
export B3NATIVE_SAVE_SCREENS=${B3NATIVE_SAVE_SCREENS:-show}   # the reference flow: the autosave page is drawn (SAVE SCREENS defaults to Hide; only crashresults live_17200 differs)
export B3_SUB_CLAMP=${B3_SUB_CLAMP:-0}                       # subtractive blends not clamped at 0 (the record predates the clamp: the race shots' wire edges differ)
export B3_TUNNEL_LIGHTS=${B3_TUNNEL_LIGHTS:-game}             # nightfix: TUNNEL LIGHTS Game (the record predates the Glow default)
export B3_RACE_TIME_LABEL=${B3_RACE_TIME_LABEL:-0}           # todrace: the track pages without the "Time of day" prompt (the record predates it)
export B3NATIVE_CAR_STATS=${B3NATIVE_CAR_STATS:-0}           # carstats1: SELECT CAR's two stats only (the record predates GRIP / ACCELERATION)
SCEN=${SCENARIOS:-race pause photo music takedown timeattack roadrage crash worldtour movie multiplayer partycrash online ddrewards ddrecords ddsettings crashnav carselect crashresults nfsdemo retail}
KERNEL_ROWS=10BB00,10BB20,10BB30,10BB40,10BB50,10CA38,10C9D0,10C968,10C900,10BA20,10C160,10BE20,10BE30,10BE40,10BE50,10BE60,10BE70,10BE80,10BC20,10BC30,10BC40,10BC50,10BC60,10BC70,10BC80,10BCB0,10BD10,10BD20,10BD30,10BD40,10BD50,10CCD0,10BBA0,10BBB0,10BC00,10BC10,114E60,114EB8,10BED0,10C130,2372F0
TRACE=${TRACE:-$KERNEL_ROWS}
TRACE_SLOTS=${TRACE_SLOTS:-64}
TRACE_WINDOW=${TRACE_WINDOW:-100}
RAM_GRID=${RAM_GRID:-1-100000:100}
RATE_RUNS=${RATE_RUNS:-3}
JOBS=${JOBS:-2}
mkdir -p "$OUT"
# the trace groups: TRACE split into runs of at most TRACE_SLOTS addresses
mapfile -t TGROUPS < <(echo "$TRACE" | tr ',' '\n' | grep -v '^$' | awk -v n="$TRACE_SLOTS" '{g = int((NR - 1) / n); a[g] = a[g] (a[g] == "" ? "" : ",") $0} END {for (k = 0; k in a; ++k) print a[k]}')
SB=""   # native_play's stdout line-buffered (a RAM-hash line is never split by another thread's output)
for f in /usr/libexec/coreutils/libstdbuf.so /usr/lib/coreutils/libstdbuf.so; do [ -f "$f" ] && SB=$f && break; done
{
  echo "native_play $NP"
  echo "sha256 $(sha256sum "$NP" | cut -c1-64)"
  echo "tree $(git -C "$(dirname "$NP")" describe --always --dirty 2>/dev/null || echo '?')"
  echo "recorded $(date -u +%Y-%m-%dT%H:%M:%SZ) scenarios: $SCEN; trace: $TRACE; ram grid $RAM_GRID"
} > "$OUT/build.txt"

digest() {   # digest <raw trace> <out>: per window the call count and SHA-1, then per address the count
  python3 -I - "$1" "$2" "$TRACE_WINDOW" <<'PY'
import hashlib, sys
raw, out, win = sys.argv[1], sys.argv[2], int(sys.argv[3])
wins, per, total = {}, {}, 0
try:
    f = open(raw, 'rb')
except OSError:
    f = []
for line in f:
    p = line.split(b' ', 2)
    if len(p) < 3 or not p[1].startswith(b'v'):
        continue
    w = int(p[1][1:]) // win
    e = wins.get(w)
    if e is None:
        e = wins[w] = [0, hashlib.sha1()]
    e[0] += 1
    e[1].update(line)
    per[p[0]] = per.get(p[0], 0) + 1
    total += 1
with open(out, 'w') as o:
    o.write('calls %d\n' % total)
    for w in sorted(wins):
        o.write('window v%d-%d %d %s\n' % (w * win, w * win + win - 1, wins[w][0], wins[w][1].hexdigest()))
    for a in sorted(per):
        o.write('address %s %d\n' % (a.decode(), per[a]))
PY
}

record() {   # record <scenario>
  local s=$1 d=$OUT/$1 sc=$1 skip="" t0 g k
  [ "$s" = movie ] && { sc=race; skip=${MOVIE_SKIP_AT:-3000}; }
  t0=$(date +%s)
  rm -rf "$d"; mkdir -p "$d"
  env ${SB:+LD_PRELOAD=$SB _STDBUF_O=L} SCENARIO="$sc" ${skip:+SKIP_MOVIE_AT=$skip} STRICT=1 \
    B3NATIVE_RAM_HASH="$RAM_GRID" B3NATIVE_SIF_LOG="$d/sif.log" B3NATIVE_RWA_LOG="$d/rwa.log" B3NATIVE_DMA_LOG="$d/dma.log" \
    ${TGROUPS[0]:+B3NATIVE_TIME_TRACE=${TGROUPS[0]} B3NATIVE_TIME_TRACE_FILE=$d/trace.raw} \
    tools/native/race_check.sh "$NP" "$d/rc" > "$d/race_check.txt" 2>&1
  tail -1 "$d/race_check.txt" > "$d/verdict"
  grep '^race_check: shot ' "$d/race_check.txt" | awk '{print $3, $4}' > "$d/shots.txt"
  grep -o 'b3live\] shot poll [0-9]* .* race (photo mode)' "$d/rc/native_play.log" | awk '{print $4}' | sort -u > "$d/photo.txt"
  grep -o '^native_play: VBlank [0-9]*: ram\( [0-9a-f]\{8\}\)*' "$d/rc/native_play.log" | sed 's/^native_play: //' > "$d/ram.txt"
  grep -m1 -o 'time_trace: [0-9]* functions traced\|time_trace: .*' "$d/rc/native_play.log" > "$d/trace_status.txt"
  tail -40 "$d/rc/native_play.log" > "$d/tail.log"
  if [ -n "${TGROUPS[0]:-}" ]; then
    digest "$d/trace.raw" "$d/trace.txt"
    if [ "${KEEP_TRACE:-1}" != 0 ]; then gzip -f "$d/trace.raw"; else rm -f "$d/trace.raw"; fi
  fi
  gzip -f "$d/sif.log" "$d/rwa.log" 2>/dev/null
  gzip -c "$d/rc/native_play.log" > "$d/native_play.log.gz" 2>/dev/null
  case $(cat "$d/verdict") in "race_check: PASS"*)   # determ1: a failing run keeps its shots
    if [ "${KEEP_SHOTS:-0}" = 1 ] && [ -d "$d/rc/shots" ]; then mv "$d/rc/shots" "$d/shot_png"; fi
    rm -rf "$d/rc" ;;
  esac
  # further trace groups: trace-only runs (a build with fewer time_trace slots than addresses)
  for ((g = 1; g < ${#TGROUPS[@]}; ++g)); do
    k=$((g + 1))
    env SCENARIO="$sc" ${skip:+SKIP_MOVIE_AT=$skip} STRICT=1 B3NATIVE_TIME_TRACE="${TGROUPS[$g]}" B3NATIVE_TIME_TRACE_FILE="$d/trace$k.raw" \
      tools/native/race_check.sh "$NP" "$d/rc$k" > "$d/race_check$k.txt" 2>&1
    tail -1 "$d/race_check$k.txt" > "$d/verdict$k"
    grep -m1 -o 'time_trace: .*' "$d/rc$k/native_play.log" > "$d/trace${k}_status.txt"
    digest "$d/trace$k.raw" "$d/trace$k.txt"
    if [ "${KEEP_TRACE:-1}" != 0 ]; then gzip -f "$d/trace$k.raw"; else rm -f "$d/trace$k.raw"; fi
    rm -rf "$d/rc$k"
  done
  echo "baseline: $s ($(( $(date +%s) - t0 )) s): $(cut -c1-100 "$d/verdict"); $(head -1 "$d/trace.txt" 2>/dev/null) traced ($(cat "$d/trace_status.txt")), $(wc -l < "$d/ram.txt") RAM hashes, $(wc -l < "$d/dma.log" 2>/dev/null || echo 0) DMA lines, $(wc -l < "$d/shots.txt") shots"
}

rate() {   # rate <k>: one plain race run, its VBlanks/s
  local d=$OUT/rate_$1
  rm -rf "$d"
  STRICT=1 tools/native/race_check.sh "$NP" "$d" > "$d.txt" 2>&1
  local r
  r=$(grep -o 'last windows [0-9.]* VBlanks/s' "$d.txt" | awk '{print $3}')
  echo "${r:-FAIL}" >> "$OUT/rate.txt"
  echo "baseline: rate run $1: ${r:-FAIL ($(tail -1 "$d.txt" | cut -c1-120))} VBlanks/s"
  rm -rf "$d" "$d.txt"
}

for s in $SCEN; do
  while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
  record "$s" &
done
wait
# the rate runs one at a time (another native_play beside them would slow them)
rm -f "$OUT/rate.txt"
for ((k = 1; k <= RATE_RUNS; ++k)); do rate "$k"; done
fail=""
for s in $SCEN; do
  case $(cat "$OUT/$s/verdict" 2>/dev/null) in "race_check: PASS"*) ;; *) fail="$fail $s" ;; esac
done
if [ -n "$fail" ]; then echo "baseline: FAIL (recorded anyway; these did not PASS):$fail"; exit 1; fi
rates=""; [ "$RATE_RUNS" -gt 0 ] && rates="; rates (VBlanks/s): $(tr '\n' ' ' < "$OUT/rate.txt")"
echo "baseline: PASS recorded $(echo $SCEN | wc -w) scenarios in $OUT$rates"
exit 0
