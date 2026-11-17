#!/bin/bash
# vdb_mod_check.sh -- does the game read a VDB.XML made by b3modtool? (mod kit part 3b, docs/modding/b3modtool.md "vdb"):
# a test mod made here from the user's own DATA/VDB.XML (b3modtool vdb export, one kind of value changed in the text:
# every car's follow camera twice as far behind the car, "Camera/Follow/ | Camera Offset" z x 2; b3modtool vdb import
# --base), loaded by native_play through --mods in an offscreen race_check run, against the same run without it.
#
#   tools/modding/vdb_mod_check.sh [native_play] [out dir]
#     native_play   default build/native_play
#     out dir       default build/vdb_mod_check (the test mod, both runs' logs and shots; game data: delete it after)
#   Environment: B3_GAME_DIR (a folder of the disc's files), SAVES (as race_check.sh), B3MODTOOL (default
#   build/b3modtool), REF (a no-mod race_check out dir of the same build and shots: that run is not repeated)
#
# PASS (last line "vdb_mod_check: PASS ...") when both runs PASS race_check, the modded run's log has DATA/VDB.XML
# served from the mod, the shot of the event's intro (10700, its own camera) is equal and the race shot 11500 (driving,
# the follow camera) differs. The test mod never leaves OUT.
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/vdb_mod_check}
TOOL=${B3MODTOOL:-build/b3modtool}
. tools/local_env.sh; b3_local_env .
GAME=${B3_GAME_DIR:-data/game}
fail() { echo "vdb_mod_check: FAIL $*"; exit 1; }
[ -x "$NP" ] || fail "no $NP"
[ -x "$TOOL" ] || fail "no $TOOL (make modtool)"
[ -d "$GAME" ] || fail "B3_GAME_DIR ($GAME) is not a folder of the disc's files"
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
MODS_DIR=$OUT/mods
SRC=$(find "$GAME" -maxdepth 2 -iname vdb.xml | head -1)
[ -n "$SRC" ] || fail "no DATA/VDB.XML in $GAME"
rm -rf "$MODS_DIR"
mkdir -p "$MODS_DIR/proof-vdb/data/DATA" "$OUT"
cat > "$MODS_DIR/proof-vdb/mod.ini" <<'INI'
[mod]
id = proof-vdb
name = Proof: the follow camera further back
version = 1
author = vdb_mod_check.sh
description = Every car's follow camera twice as far behind (a test; made from your own disc, never shared).
api = 1
INI
"$TOOL" vdb export "$SRC" "$OUT/vdb.txt" > /dev/null || fail "vdb export"
python3 -I - "$OUT/vdb.txt" "$OUT/edit.txt" <<'PY' || fail "cannot make the edit"
import re, sys
out, n, sec = [], 0, None
for line in open(sys.argv[1]):
    if line.startswith('['):
        sec = line
    m = re.match(r'(Camera/Follow/ \| Camera Offset = )\(([^,]+), ([^,]+), ([^,]+), (.+)\)$', line.rstrip('\n'))
    if m:
        x, y, z, w = m.group(2, 3, 4, 5)
        out += [sec, '%s(%s, %s, %r, %s)\n' % (m.group(1), x, y, float(z) * 2, w)]
        n += 1
if not n:
    sys.exit('no "Camera/Follow/ | Camera Offset" values')
open(sys.argv[2], 'w').write(''.join(out))
print('vdb_mod_check: %d follow-camera offsets changed' % n)
PY
"$TOOL" vdb import "$OUT/edit.txt" "$MODS_DIR/proof-vdb/data/DATA/VDB.XML" --base "$SRC" || fail "vdb import"

run() {   # run <out dir> [MODS]
  local dir=$1
  env ${2:+MODS=$2 MODS_DIR=$MODS_DIR} tools/native/race_check.sh "$NP" "$dir" > "$dir.verdict.txt" 2>&1
  tail -1 "$dir.verdict.txt"
}
echo "vdb_mod_check: the modded run ..."
V1=$(run "$OUT/rc_mod" proof-vdb)
if [ -n "${REF:-}" ]; then R=$REF; V2="race_check: PASS (REF $REF, not checked here)"
else
  echo "vdb_mod_check: the plain run ..."
  R=$OUT/rc_plain
  V2=$(run "$R")
fi
why=""
case $V1 in "race_check: PASS"*) ;; *) why="$why; modded run: $V1" ;; esac
case $V2 in "race_check: PASS"*) ;; *) why="$why; plain run: $V2" ;; esac
L1=$OUT/rc_mod/native_play.log
grep -qi '^mods: DATA/VDB.XML from proof-vdb' "$L1" || why="$why; VDB.XML not served from the mod"
same=0; diff=0
for f in "$OUT"/rc_mod/shots/live_*_0.png; do
  [ -f "$f" ] || continue
  q=$(basename "$f"); q=${q#live_}; q=${q%_0.png}
  b=$R/shots/live_${q}_0.png
  [ -f "$b" ] || continue
  if cmp -s "$f" "$b"; then same=$((same + 1)); else diff=$((diff + 1)); fi
done
for q in 10700 11500; do
  a=$OUT/rc_mod/shots/live_${q}_0.png; b=$R/shots/live_${q}_0.png
  [ -f "$a" ] && [ -f "$b" ] || { why="$why; no shot $q"; continue; }
done
# 10700: the event's intro (its own camera, not the follow camera): equal; 11500: driving, seen by the follow camera
cmp -s "$OUT/rc_mod/shots/live_10700_0.png" "$R/shots/live_10700_0.png" || why="$why; intro shot 10700 differs (only the follow camera should)"
cmp -s "$OUT/rc_mod/shots/live_11500_0.png" "$R/shots/live_11500_0.png" && why="$why; race shot 11500 equal (the camera did not move)"
if [ -z "$why" ]; then
  echo "vdb_mod_check: PASS the mod's VDB.XML served; the race shot 11500 differs from the plain run (the follow camera further back), the intro shot 10700 is equal ($diff shots differ, $same equal; logs $L1, $R/native_play.log)"
else
  echo "vdb_mod_check: FAIL ${why#; }"
  exit 1
fi
