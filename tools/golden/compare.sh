#!/bin/bash
# compare.sh -- two recordings of tools/golden/baseline.sh compared item by item (docs/specs/).
#
#   tools/golden/compare.sh <old recording> <new recording>
#
# One line per scenario and item, "PASS" or "FAIL" with what differs (behaviour only: VBlank windows, counts, MB
# indices, shot names -- never code), then the rate, then the verdict. Items:
#   verdict   the new run's race_check verdict is PASS
#   shots     every shot hash equal, except a shot taken in photo mode in either run (photo.txt: the free camera moves
#             by host time)
#   trace     the time-trace digest equal: every window's call count and hash, every address's count (a FAIL names
#             the first differing VBlank window and the addresses whose counts differ); trace2.txt ... likewise
#   dma sif rwa   the logs equal line for line (a FAIL gives the first differing line number and the line counts)
#   ram       every MB hash at every grid VBlank equal, except host noise: a cell that differs once and is equal
#             to the old recording again at one of the next 4 grid VBlanks while everything else is equal (b3host's
#             pointer-argument bounce windows leave host bytes below the guest stack pointer; see
#             tools/native/layer_check.sh) -- reported as transient, not a failure; RAM_STRICT=1 fails on it too
#   rate      the median of the new rates within RATE_TOL % (default 2) of the old median (lower only: faster passes)
# Last line: "compare: PASS ..." or "compare: FAIL <scenario:item ...>"; exit 0 / 1.
set -u
OLD=${1:?usage: compare.sh <old> <new>}
NEW=${2:?usage: compare.sh <old> <new>}
RATE_TOL=${RATE_TOL:-2}
fail=""
fl() { fail="$fail $1"; }
for d in "$OLD"/*/; do
  s=$(basename "$d")
  [ -f "$d/verdict" ] || continue
  n=$NEW/$s
  if [ ! -d "$n" ]; then echo "compare: $s: FAIL not recorded in $NEW"; fl "$s"; continue; fi
  v=$(cat "$n/verdict" 2>/dev/null)
  case $v in "race_check: PASS"*) echo "compare: $s verdict: PASS" ;; *) echo "compare: $s verdict: FAIL ${v:0:160}"; fl "$s:verdict" ;; esac
  # shots
  r=$(python3 -I - "$d/shots.txt" "$n/shots.txt" "$d/photo.txt" "$n/photo.txt" <<'PY'
import sys
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
if missing or diff:
    print('FAIL %s%s' % ('differ: ' + ' '.join(sorted(diff, key=lambda x: int(poll(x) or 0))) if diff else '',
                         ('; only in one run: ' + ' '.join(missing)) if missing else ''))
else:
    print('PASS %d equal%s' % (len(a) - skipped, ', %d photo-mode shots not compared' % skipped if skipped else ''))
PY
)
  echo "compare: $s shots: $r"; case $r in PASS*) ;; *) fl "$s:shots" ;; esac
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
    echo "compare: $s ${tb%.txt}: $r"; case $r in PASS*) ;; *) fl "$s:${tb%.txt}" ;; esac
  done
  # logs
  for f in dma.log sif.log.gz rwa.log.gz; do
    item=${f%%.*}
    if [ ! -f "$d/$f" ]; then echo "compare: $s $item: FAIL no old $f"; fl "$s:$item"; continue; fi
    if [ ! -f "$n/$f" ]; then echo "compare: $s $item: FAIL no new $f"; fl "$s:$item"; continue; fi
    if cmp -s <(zcat -f "$d/$f") <(zcat -f "$n/$f"); then
      echo "compare: $s $item: PASS $(zcat -f "$d/$f" | wc -l) lines equal"
    else
      first=$(cmp <(zcat -f "$d/$f") <(zcat -f "$n/$f") 2>&1 | grep -o 'line [0-9]*' | head -1)
      echo "compare: $s $item: FAIL $(zcat -f "$d/$f" | wc -l) vs $(zcat -f "$n/$f" | wc -l) lines; first difference at ${first:-the end of the shorter}"
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
  echo "compare: $s ram: $r"; case $r in PASS*) ;; *) fl "$s:ram" ;; esac
done
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
  echo "compare: rate: $r"; case $r in PASS*) ;; *) fl "rate" ;; esac
fi
if [ -n "$fail" ]; then echo "compare: FAIL$fail"; exit 1; fi
echo "compare: PASS every recorded item of $OLD equal in $NEW"
exit 0
