#!/bin/bash
# compare.sh -- two recordings of tools/golden/baseline.sh compared item by item (docs/specs/).
#
#   tools/golden/compare.sh <old recording> <new recording>
#
# One line per scenario and item, "PASS" or "FAIL" with what differs (behaviour only: VBlank windows, counts, MB
# indices, shot names -- never code), then the rate, then the verdict. Items:
#   verdict   the new run's race_check verdict is PASS
#   shots     every shot hash equal, except a shot taken in photo mode in either run (photo.txt: the free camera moves
#             by host time). SHOT_TOL=<pixels>:<levels>[:<specks>] (default off; replay_check sets it): a shot whose PNGs
#             (the runs' shot_png/ or rc/shots/) differ in at most <pixels> pixels by at most <levels> per channel, plus
#             at most <specks> pixels by more (default 0), passes, reported "within tolerance (N px, max d; K specks)"
#             -- lavapipe, the WSL test machines' software Vulkan driver, draws a crash's spark burst slightly
#             differently from identical input (faint edge pixels and a few 1-pixel specks, 1 run in 5)
#   trace     the time-trace digest equal: every window's call count and hash, every address's count (a FAIL names
#             the first differing VBlank window and the addresses whose counts differ); trace2.txt ... likewise
#   dma sif rwa   the logs equal line for line (a FAIL gives the first differing line number and the line counts)
#   ram       every MB hash at every grid VBlank equal, except host noise: a cell that differs once and is equal
#             to the old recording again at one of the next 4 grid VBlanks while everything else is equal (b3host's
#             pointer-argument bounce windows leave host bytes below the guest stack pointer; see
#             tools/native/layer_check.sh) -- reported as transient, not a failure; RAM_STRICT=1 fails on it too
#   rate      the median of the new rates within RATE_TOL % (default 2) of the old median (lower only: faster passes)
# determ1: a scenario with any failing item is copied, as recorded, to $KEEP_FAILED/<scenario>.<UTC time> (default
# <new recording>.failed; KEEP_FAILED=none: not kept), so that a re-run into the same folder (baseline.sh starts each
# scenario afresh) does not erase the evidence of a run that differed.
# flakenet (the standing net for the rare nondeterminism): a scenario whose LOG items differ (trace, dma, sif, rwa, ram:
# the EE's own timeline) is kept in $FLAKE_DIR/<UTC time>-<scenario>[-$FLAKE_TAG] instead (default
# none: off; local.env's FLAKE_DIR names the machine's net), with its raw time trace (trace.raw.gz,
# which baseline.sh now keeps until this compare), compare.txt (this compare's lines for it), build.txt, and with
# FLAKE_WITH_OLD=1 the old recording's scenario too (old/; replay_check: the recording a playback parted from) and the
# files in FLAKE_EXTRA (space separated; replay_check: the .b3replay). Only when at most FLAKE_MAX_SCEN (default 3)
# scenarios differ in their logs: more is a change of the build, not a flake (kept as determ1 keeps it). Nothing is
# copied once the net holds FLAKE_CAP_MB (default 3000) MB; the output says so. The raw traces of scenarios whose
# logs are equal are deleted (KEEP_TRACE=1: kept).
# Last line: "compare: PASS ..." or "compare: FAIL <scenario:item ...>"; exit 0 / 1.
set -u
OLD=${1:?usage: compare.sh <old> <new>}
NEW=${2:?usage: compare.sh <old> <new>}
RATE_TOL=${RATE_TOL:-2}
KEEP_FAILED=${KEEP_FAILED:-${NEW%/}.failed}
. "$(dirname "$0")/../local_env.sh"; b3_local_env "$(dirname "$0")/../.."
FLAKE_DIR=${FLAKE_DIR:-none}
FLAKE_MAX_SCEN=${FLAKE_MAX_SCEN:-3}
FLAKE_CAP_MB=${FLAKE_CAP_MB:-3000}
fail=""
failed=""      # the scenarios with any failing item
logfailed=""   # flakenet: those whose log items (trace dma sif rwa ram) differ
fl() { fail="$fail $1"; }
keep() {   # keep <scenario>: determ1: the failing recording copied apart (once per scenario)
  [ "$KEEP_FAILED" = none ] || [ ! -d "$NEW/$1" ] && return
  local k
  k=$KEEP_FAILED/$1.$(date -u +%Y%m%dT%H%M%SZ)
  mkdir -p "$KEEP_FAILED" && cp -a "$NEW/$1" "$k" && cp -a "$NEW/build.txt" "$k/build.txt" 2>/dev/null
  [ "${KEEP_TRACE:-0}" = 1 ] || rm -f "$k"/trace*.raw.gz
  echo "compare: $1: the failing recording kept in $k"
}
net() {   # net <scenario>: flakenet: kept in the flake net (true), or not (false: determ1's copy then)
  [ "$FLAKE_DIR" = none ] || [ ! -d "$NEW/$1" ] && return 1
  mkdir -p "$FLAKE_DIR" || return 1
  local used k
  used=$(du -sm "$FLAKE_DIR" 2>/dev/null | cut -f1)
  if [ "${used:-0}" -ge "$FLAKE_CAP_MB" ]; then
    echo "compare: $1: NOT kept in the flake net: $FLAKE_DIR holds ${used} MB (FLAKE_CAP_MB $FLAKE_CAP_MB); clear it"
    return 1
  fi
  k=$FLAKE_DIR/$(date -u +%Y%m%dT%H%M%SZ)-$1${FLAKE_TAG:+-$FLAKE_TAG}
  mkdir -p "$k" && cp -a "$NEW/$1/." "$k/" || return 1
  cp -a "$NEW/build.txt" "$k/build.txt" 2>/dev/null
  grep "^compare: $1[ :]" "$OUT_LINES" > "$k/compare.txt"
  { echo "old: $OLD"; echo "new: $NEW"; echo "kept: $(date -u +%Y-%m-%dT%H:%M:%SZ)"; } > "$k/where.txt"
  [ "${FLAKE_WITH_OLD:-0}" = 1 ] && [ -d "$OLD/$1" ] && mkdir -p "$k/old" && cp -a "$OLD/$1/." "$k/old/"
  for f in ${FLAKE_EXTRA:-}; do [ -f "$f" ] && cp -a "$f" "$k/"; done
  echo "compare: $1: its logs differ: kept in the flake net, $k ($(du -sm "$k" | cut -f1) MB$([ -f "$k/trace.raw.gz" ] && echo ", raw trace included"))"
  return 0
}
OUT_LINES=$(mktemp)
trap 'rm -f "$OUT_LINES"' EXIT
say() { echo "$*"; echo "$*" >> "$OUT_LINES"; }   # flakenet: this compare's lines, for a kept scenario's compare.txt
for d in "$OLD"/*/; do
  s=$(basename "$d")
  fail_before=$fail
  [ -f "$d/verdict" ] || continue
  n=$NEW/$s
  if [ ! -d "$n" ]; then say "compare: $s: FAIL not recorded in $NEW"; fl "$s"; continue; fi
  v=$(cat "$n/verdict" 2>/dev/null)
  case $v in "race_check: PASS"*) say "compare: $s verdict: PASS" ;; *) say "compare: $s verdict: FAIL ${v:0:160}"; fl "$s:verdict" ;; esac
  # shots
  r=$(python3 -I - "$d/shots.txt" "$n/shots.txt" "$d/photo.txt" "$n/photo.txt" "${SHOT_TOL:-}" "$d" "$n" \
      "$(dirname "$0")/pngdiff.py" <<'PY'
import os, subprocess, sys
def shots(p):
    try:
        return dict(l.split()[:2] for l in open(p) if len(l.split()) >= 2)
    except OSError:
        return None
def polls(p):
    try:
        return set(open(p).read().split())
    except OSError:
        return set()
a, b = shots(sys.argv[1]), shots(sys.argv[2])
if a is None or b is None:
    print('FAIL shots.txt missing'); sys.exit()
photo = polls(sys.argv[3]) | polls(sys.argv[4])
def poll(name):
    p = name.split('_')
    return p[1] if len(p) > 1 else ''
missing = sorted(set(a) ^ set(b), key=lambda x: int(poll(x) or 0))
diff = [k for k in a if k in b and a[k] != b[k] and poll(k) not in photo]
skipped = sum(1 for k in a if poll(k) in photo)
# SHOT_TOL: a differing shot within <pixels>:<levels>[:<specks>] of the other run's passes (lavapipe's own pixels)
tol, within = sys.argv[5], []
if tol and diff:
    tv = [int(x) for x in tol.split(':')]
    tp, tl, ts = tv[0], tv[1], (tv[2] if len(tv) > 2 else 0)
    def png(run, name):
        for sub in ('shot_png', os.path.join('rc', 'shots')):
            p = os.path.join(run, sub, name)
            if os.path.isfile(p):
                return p
        return None
    left = []
    for k in diff:
        pa, pb = png(sys.argv[6], k), png(sys.argv[7], k)
        m = subprocess.run([sys.executable, '-I', sys.argv[8], pa, pb, str(tl)], capture_output=True, text=True) if pa and pb else None
        if m is not None and m.returncode == 0 and len(m.stdout.split()) == 3:
            px, dmax, specks = (int(x) for x in m.stdout.split())
            what = '%d px, max %d; %d speck%s' % (px, dmax, specks, '' if specks == 1 else 's')
            if px - specks <= tp and specks <= ts:
                within.append('%s within tolerance (%s)' % (k, what)); continue
            left.append('%s (%s)' % (k, what)); continue
        left.append('%s (no PNGs to measure)' % k)
    diff = left
key = lambda x: int(poll(x.split()[0]) or 0)
if missing or diff:
    print('FAIL %s%s' % ('differ: ' + ' '.join(sorted(diff, key=key)) if diff else '',
                         ('; only in one run: ' + ' '.join(missing)) if missing else ''))
else:
    print('PASS %d equal%s%s' % (len(a) - skipped - len(within), ', %d photo-mode shots not compared' % skipped if skipped else '',
                                 ''.join(', ' + w for w in sorted(within, key=key))))
PY
)
  say "compare: $s shots: $r"; case $r in PASS*) ;; *) fl "$s:shots" ;; esac
  # time traces
  for t in "$d"/trace*.txt; do
    tb=$(basename "$t")
    case $tb in *_status.txt) continue ;; esac
    r=$(python3 -I - "$t" "$n/$tb" <<'PY'
import sys
def load(p):
    try:
        lines = open(p).read().split('\n')
    except OSError:
        return None
    calls = next((l for l in lines if l.startswith('calls ')), 'calls ?')
    win = [l.split()[1:] for l in lines if l.startswith('window ')]
    adr = dict(l.split()[1:3] for l in lines if l.startswith('address '))
    return calls, win, adr
a, b = load(sys.argv[1]), load(sys.argv[2])
if b is None:
    print('FAIL not recorded'); sys.exit()
if a == b:
    print('PASS %s, %d windows equal' % (a[0], len(a[1]))); sys.exit()
first = next((x[0] for x, y in zip(a[1], b[1]) if x != y), None)
if first is None:
    first = 'after %s (%d vs %d windows)' % (a[1][-1][0] if a[1] else 'start', len(a[1]), len(b[1]))
counts = ['%s %s->%s' % (k, a[2].get(k, 0), b[2].get(k, 0)) for k in sorted(set(a[2]) | set(b[2])) if a[2].get(k) != b[2].get(k)]
print('FAIL %s vs %s; first differing window %s%s' % (a[0], b[0], first, '; call counts: ' + ', '.join(counts[:12]) if counts else ''))
PY
)
    say "compare: $s ${tb%.txt}: $r"; case $r in PASS*) ;; *) fl "$s:${tb%.txt}" ;; esac
  done
  # logs
  for f in dma.log sif.log.gz rwa.log.gz; do
    item=${f%%.*}
    if [ ! -f "$d/$f" ]; then say "compare: $s $item: FAIL no old $f"; fl "$s:$item"; continue; fi
    if [ ! -f "$n/$f" ]; then say "compare: $s $item: FAIL no new $f"; fl "$s:$item"; continue; fi
    if cmp -s <(zcat -f "$d/$f") <(zcat -f "$n/$f"); then
      say "compare: $s $item: PASS $(zcat -f "$d/$f" | wc -l) lines equal"
    else
      first=$(cmp <(zcat -f "$d/$f") <(zcat -f "$n/$f") 2>&1 | grep -o 'line [0-9]*' | head -1)
      say "compare: $s $item: FAIL $(zcat -f "$d/$f" | wc -l) vs $(zcat -f "$n/$f" | wc -l) lines; first difference at ${first:-the end of the shorter}"
      fl "$s:$item"
    fi
  done
  # RAM
  r=$(python3 -I - "$d/ram.txt" "$n/ram.txt" "${RAM_STRICT:-0}" <<'PY'
import sys
try:
    A = [l.split() for l in open(sys.argv[1]).read().split('\n') if l.strip()]
    C = [l.split() for l in open(sys.argv[2]).read().split('\n') if l.strip()]
except OSError:
    print('FAIL ram.txt missing'); sys.exit()
strict = sys.argv[3] == '1'
if len(A) != len(C):
    print('FAIL %d vs %d grid VBlanks' % (len(A), len(C))); sys.exit()
real = transient = 0
first = ''
for i, (xs, ys) in enumerate(zip(A, C)):
    if xs == ys:
        continue
    if len(xs) != len(ys) or xs[:3] != ys[:3]:
        real += 1; first = first or 'line %d' % (i + 1); continue
    for k in range(3, len(xs)):
        if xs[k] == ys[k]:
            continue
        if not strict and any(j < len(A) and len(A[j]) > k and len(C[j]) > k and A[j][k] == C[j][k] for j in range(i + 1, i + 5)):
            transient += 1; continue
        real += 1; first = first or '%s MB %d' % (xs[1].rstrip(':'), k - 3)
if real:
    print('FAIL %d cells differ (first: VBlank %s)%s' % (real, first, ', %d transient' % transient if transient else ''))
else:
    print('PASS %d grid VBlanks x 32 MB equal%s' % (len(A), ', %d transient host-noise cells' % transient if transient else ''))
PY
)
  say "compare: $s ram: $r"; case $r in PASS*) ;; *) fl "$s:ram" ;; esac
  if [ "$fail" != "$fail_before" ]; then
    failed="$failed $s"
    case "${fail#"$fail_before"}" in *":trace"*|*":dma"*|*":sif"*|*":rwa"*|*":ram"*) logfailed="$logfailed $s" ;; esac
  elif [ "${KEEP_TRACE:-0}" != 1 ]; then
    rm -f "$n"/trace*.raw.gz   # flakenet: equal logs: the raw trace is not needed
  fi
done
nlog=$(echo $logfailed | wc -w)
if [ "$nlog" -gt "$FLAKE_MAX_SCEN" ] && [ "$FLAKE_DIR" != none ]; then
  say "compare: $nlog scenarios' logs differ (more than FLAKE_MAX_SCEN $FLAKE_MAX_SCEN): a change of the build, not kept in the flake net"
fi
for s in $failed; do
  if [ "$nlog" -le "$FLAKE_MAX_SCEN" ] && [[ " $logfailed " == *" $s "* ]] && net "$s"; then continue; fi
  keep "$s"
done
for s in $failed; do [ "${KEEP_TRACE:-0}" = 1 ] || rm -f "$NEW/$s"/trace*.raw.gz; done
# rate
if [ -s "$OLD/rate.txt" ]; then
  r=$(python3 -I - "$OLD/rate.txt" "$NEW/rate.txt" "$RATE_TOL" <<'PY'
import statistics, sys
def rates(p):
    try:
        return [float(x) for x in open(p).read().split() if x.replace('.', '', 1).isdigit()]
    except OSError:
        return []
a, b, tol = rates(sys.argv[1]), rates(sys.argv[2]), float(sys.argv[3])
if not a or not b:
    print('FAIL no rates (%d old, %d new)' % (len(a), len(b))); sys.exit()
ma, mb = statistics.median(a), statistics.median(b)
d = (mb - ma) / ma * 100
print('%s median %.1f -> %.1f VBlanks/s (%+.1f %%, tolerance -%g %%; old %s, new %s)' % ('PASS' if d >= -tol else 'FAIL', ma, mb, d, tol,
      ' '.join('%g' % x for x in a), ' '.join('%g' % x for x in b)))
PY
)
  say "compare: rate: $r"; case $r in PASS*) ;; *) fl "rate" ;; esac
fi
if [ -n "$fail" ]; then say "compare: FAIL$fail"; exit 1; fi
say "compare: PASS every recorded item of $OLD equal in $NEW"
exit 0
