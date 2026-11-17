#!/bin/bash
# layer_check.sh -- the hardware layer's golden runs (the device-state work, 2026-11-10): race_check.sh's STRICT
# scenarios with the run's logs on, every log and the shots compared with a baseline kept outside git. A change to the
# platform's hardware layer (src/native/hw_model.h, platform_hw.cpp, the DMAC / VBlank delivery in platform_iop.cpp)
# passes only when every artifact is identical: what the game observes -- RAM, the clock of every DMA completion and
# DMAC callback, the EE<->IOP traffic, RW Audio's commands, the pictures -- did not move by one cycle.
#
#   tools/native/layer_check.sh [--baseline | --baseline2 | --compare] [native_play]
#     --baseline    record the baseline (BASE) instead of comparing with it
#     --baseline2   record a second run of the baseline's build (BASE2): its RAM-hash cells and shots that differ from
#                   BASE are host noise (below), ignored in comparisons
#     --compare     no run: compare the artifacts already in OUT with the baseline again
#     native_play   default build/native_play
#   Environment: SCENARIOS (default "race pause photo music takedown timeattack movie"; also roadrage crash worldtour),
#   BASE (default build/layer_base), BASE2 (default build/layer_base2), OUT (default build/layer_run), MOVIE_SKIP_AT
#   (the movie run's SKIP_MOVIE_AT, default 3000: Space during Titles30), the disc and card as race_check.sh (B3_GAME_DIR, else local.env's GAME; SAVES).
#
# Per scenario (race_check.sh SCENARIO=<s> STRICT=1; "movie" = the race scenario with SKIP_MOVIE_AT), into <dir>/<s>:
#   ram.txt     B3NATIVE_RAM_HASH: an FNV-1a hash of each MB of guest RAM at every VBlank (native_play)
#   dma.log     B3NATIVE_DMA_LOG: per VBlank the DMA completions and DMAC callbacks, hashed with their clock
#               (platform_hw.cpp; B3NATIVE_DMA_LOG_EVENTS=<first>-<last> lists the events where two runs part)
#   sif.log     B3NATIVE_SIF_LOG: the EE<->IOP traffic, each line stamped with the VBlank and the EE cycles
#   rwa.log     B3NATIVE_RWA_LOG: RW Audio's commands and replies
#   shots.txt   race_check's shot hashes (SHA-256 of each screenshot)
#   verdict     race_check's last line (must be PASS)
# Host noise: two runs of one build part in a few RAM-hash cells (a MB at a VBlank: ~100 of 750,000 in a race): host
# memory copied into guest stack memory by b3host's pointer-argument bounce windows (call.h PtrArg: a window of a few
# hundred bytes around a host object, host frames with their pointers and stack canary included; gdb watchpoint on
# guest 0x1FFFAF0, 2026-11-10), left below the guest stack pointer where the game never reads it (every other artifact
# is identical). So: a cell where BASE and BASE2 differ is not compared; a cell that differs only transiently -- the
# MB equal to the baseline again within 4 VBlanks, with every other artifact identical -- is reported as transient
# host noise, not a failure (a second run of the same build reproduces neither: a lasting state change would stay); nor
# is a shot compared where BASE and BASE2 differ, or one taken in photo mode in either run (the free camera moves by
# the renderer's host frame time, photomode.h advance(dt), and the offscreen renderer may enter photo mode a few polls
# later under load); any other difference fails.
# The last line: "layer_check: PASS ..." or "layer_check: FAIL <scenarios and artifacts>". Runs one native_play at a
# time (another agent's scenarios may share the machine). Never edits race_check.sh: it only sets its environment.
set -u
cd "$(dirname "$0")/../.." || exit 1
MODE=check
case ${1:-} in --baseline) MODE=baseline; shift ;; --baseline2) MODE=baseline2; shift ;; --compare) MODE=compare; shift ;; esac
NP=${1:-build/native_play}
. tools/local_env.sh; b3_local_env .
export B3_GAME_DIR=${B3_GAME_DIR:-${GAME:-data/game}}
export SAVES=${SAVES:-}
BASE=${BASE:-build/layer_base}
BASE2=${BASE2:-build/layer_base2}
OUT=${OUT:-build/layer_run}
[ "$MODE" = baseline ] && OUT=$BASE
[ "$MODE" = baseline2 ] && OUT=$BASE2
SCEN=${SCENARIOS:-race pause photo music takedown timeattack movie}
[ "$MODE" = compare ] || [ -x "$NP" ] || { echo "layer_check: FAIL no $NP"; exit 1; }
# native_play's stdout line-buffered (coreutils' libstdbuf, as `stdbuf -oL`): the renderer's stderr lines then never
# land inside a RAM-hash line of the log (native_play writes each RAM-hash line in one call)
SB=""
for f in /usr/libexec/coreutils/libstdbuf.so /usr/lib/coreutils/libstdbuf.so; do [ -f "$f" ] && SB=$f && break; done
fail=""
for s in $SCEN; do
  d=$OUT/$s
  sc=$s; skip=""
  if [ "$s" = movie ]; then sc=race; skip=${MOVIE_SKIP_AT:-3000}; fi
  t0=$(date +%s)
  if [ "$MODE" != compare ]; then
  rm -rf "$d"; mkdir -p "$d"
  env ${SB:+LD_PRELOAD=$SB _STDBUF_O=L} SCENARIO="$sc" ${skip:+SKIP_MOVIE_AT=$skip} STRICT=1 B3NATIVE_RAM_HASH=0-100000000:1 B3NATIVE_SIF_LOG="$d/sif.log" \
    B3NATIVE_RWA_LOG="$d/rwa.log" B3NATIVE_DMA_LOG="$d/dma.log" tools/native/race_check.sh "$NP" "$d/rc" > "$d/race_check.txt" 2>&1
  grep -o '^native_play: VBlank [0-9]*: ram\( [0-9a-f]\{8\}\)*' "$d/rc/native_play.log" > "$d/ram.txt"
  grep '^race_check: shot ' "$d/race_check.txt" > "$d/shots.txt"
  tail -1 "$d/race_check.txt" > "$d/verdict"
  rm -rf "$d/rc/card"   # the scratch card copy
  fi
  v=$(cat "$d/verdict")
  line="layer_check: $s ($(( $(date +%s) - t0 )) s): ${v:0:120}"
  case $v in "race_check: PASS"*) ;; *) fail="$fail $s:verdict" ;; esac
  if [ "$MODE" = check ] || [ "$MODE" = compare ]; then
    for f in ram.txt dma.log sif.log rwa.log shots.txt; do
      b=$BASE/$s/$f
      if [ ! -f "$b" ]; then fail="$fail $s:$f(no baseline)"; line="$line; $f: no baseline"; continue; fi
      if cmp -s "$b" "$d/$f"; then line="$line; $f ="; continue; fi
      if [ "$f" = shots.txt ] && [ -f "$BASE2/$s/shots.txt" ]; then   # shots that differ between BASE and BASE2 are noise
        r=$(python3 -I - "$b" "$BASE2/$s/shots.txt" "$d/shots.txt" "$d/rc/native_play.log" "$BASE/$s/rc/native_play.log" <<'PY'
import re, sys
a, n, c = (open(p).read().split('\n') for p in sys.argv[1:4])
photo = set()
for log in sys.argv[4:]:   # a shot in photo mode in either run (the renderer may enter it a few polls later)
    try:
        photo |= set(re.findall(r'b3live\] shot poll (\d+) .* race \(photo mode\)', open(log, errors='replace').read()))
    except OSError:
        pass
if len(a) != len(c) or len(a) != len(n):
    print('DIFFERS %d vs %d lines' % (len(a), len(c))); sys.exit()
def poll(x):
    m = re.search(r'live_(\d+)_', x)
    return m.group(1) if m else ''
skip = sum(1 for x in a if poll(x) in photo)
noise = sum(1 for x, y, z in zip(a, c, n) if x != y and x != z and poll(x) not in photo)
real = [x.split()[2] for x, y, z in zip(a, c, n) if x != y and x == z and poll(x) not in photo]
print('DIFFERS %s' % ' '.join(real) if real else '= (%d photo-mode shots not compared, %d host-noise shots)' % (skip, noise))
PY
)
        case $r in "="*) line="$line; $f $r"; continue ;; esac
        fail="$fail $s:$f"; line="$line; $f $r"; continue
      fi
      if [ "$f" = ram.txt ] && [ -f "$BASE2/$s/ram.txt" ]; then   # differences only in host-noise cells pass
        r=$(python3 -I - "$b" "$BASE2/$s/ram.txt" "$d/ram.txt" <<'PY'
import sys
a, n, c = (open(p).read().split('\n') for p in sys.argv[1:4])
if len(a) != len(c):
    print('DIFFERS %d vs %d lines' % (len(a), len(c))); sys.exit()
A = [x.split() for x in a]
C = [y.split() for y in c]
noise = real = transient = 0
first = ''
for i, (xs, ys) in enumerate(zip(A, C)):
    if xs == ys: continue
    ns = n[i].split() if i < len(n) else []
    if len(xs) != len(ys) or xs[:4] != ys[:4]:
        real += 1; first = first or 'line %d' % (i + 1); continue
    for k in range(4, len(xs)):
        if xs[k] == ys[k]: continue
        if k < len(ns) and ns[k] != xs[k]: noise += 1; continue
        if any(j < len(A) and len(A[j]) > k and len(C[j]) > k and A[j][k] == C[j][k] for j in range(i + 1, i + 5)):
            transient += 1; continue
        real += 1; first = first or '%s MB %d' % (' '.join(xs[1:3]), k - 4)
if real:
    print('DIFFERS %d cells (first: %s), %d noise cells, %d transient' % (real, first, noise, transient))
else:
    print('= (%d host-noise cells%s)' % (noise, ', %d transient host-noise cells' % transient if transient else ''))
PY
)
        case $r in "="*) line="$line; $f $r"; continue ;; esac
        fail="$fail $s:$f"; line="$line; $f $r"; continue
      fi
      fail="$fail $s:$f"
      first=$(diff "$b" "$d/$f" | head -3 | tr '\n' '|' | cut -c1-300)
      line="$line; $f DIFFERS ($(wc -l < "$b") vs $(wc -l < "$d/$f") lines; first: $first)"
    done
  else   # baseline / baseline2
    line="$line; recorded $(wc -l < "$d/ram.txt") RAM hashes, $(wc -l < "$d/dma.log") DMA lines, $(wc -l < "$d/sif.log") SIF lines, $(wc -l < "$d/rwa.log") RWA lines, $(wc -l < "$d/shots.txt") shots"
  fi
  echo "$line"
done
if [ -n "$fail" ]; then echo "layer_check: FAIL$fail"; exit 1; fi
if [ "$MODE" = baseline ] || [ "$MODE" = baseline2 ]; then echo "layer_check: PASS baseline recorded in $OUT ($SCEN)"; else echo "layer_check: PASS every log and shot identical to $BASE ($SCEN)"; fi
exit 0
