#!/bin/bash
# text_mod_check.sh -- the mod loader's functional check (mod kit part 1, docs/modding/DESIGN.md §1 "Phase 1"): a test
# mod made here from a COPY of the user's own DATA/GLOBALUS.BIN (the game's US string table) with one main-menu label
# changed ("SINGLE EVENT" -> "MODDED EVENT", the same length, UTF-16), loaded by native_play through --mods in an
# offscreen race_check run, against the same run without it.
#
#   tools/modding/text_mod_check.sh [native_play] [out dir]
#     native_play   default build/native_play
#     out dir       default build/text_mod_check (the test mod, both runs' logs and shots; game data: delete it after)
#   Environment: B3_GAME_DIR (a folder of the disc's files), SAVES (as race_check.sh), MENU_SHOTS (the main-menu polls,
#   default 8850,9000), REF (a no-mod race_check out dir of the same build and shots: that run is not repeated)
#
# PASS (last line "text_mod_check: PASS ...") when both runs PASS race_check, the modded run's log names the mod set
# and DATA/GLOBALUS.BIN served from the mod, the plain run's log has "mods: none", every main-menu shot differs between
# the runs (the label) and the two race shots are equal (the change is text only). The test mod never leaves OUT.
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/text_mod_check}
. tools/local_env.sh; b3_local_env .
GAME=${B3_GAME_DIR:-data/game}
MENU_SHOTS=${MENU_SHOTS:-8850,9000}
fail() { echo "text_mod_check: FAIL $*"; exit 1; }
[ -x "$NP" ] || fail "no $NP"
[ -d "$GAME" ] || fail "B3_GAME_DIR ($GAME) is not a folder of the disc's files"
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
MODS_DIR=$OUT/mods
rm -rf "$MODS_DIR"
mkdir -p "$MODS_DIR/proof-text/data/DATA"
cat > "$MODS_DIR/proof-text/mod.ini" <<'INI'
[mod]
id = proof-text
name = Proof: one menu label
version = 1
author = text_mod_check.sh
description = SINGLE EVENT reads MODDED EVENT on the main menu (a test; made from your own disc, never shared).
api = 1
INI
python3 -I - "$GAME" "$MODS_DIR/proof-text/data/DATA/GLOBALUS.BIN" <<'PY' || fail "cannot make the test mod"
import os, sys
game, out = sys.argv[1], sys.argv[2]
src = None
for d in os.listdir(game):
    if d.upper() == 'DATA':
        for f in os.listdir(os.path.join(game, d)):
            if f.upper() == 'GLOBALUS.BIN':
                src = os.path.join(game, d, f)
if not src:
    sys.exit('no DATA/GLOBALUS.BIN in ' + game)
b = open(src, 'rb').read()
old, new = 'SINGLE EVENT'.encode('utf-16-le'), 'MODDED EVENT'.encode('utf-16-le')
if b.count(old) != 1:
    sys.exit('"SINGLE EVENT" found %d times (one expected)' % b.count(old))
open(out, 'wb').write(b.replace(old, new))
print('text_mod_check: test mod made: %s (%d bytes, one label changed at offset %d)' % (out, len(b), b.find(old)))
PY

run() {   # run <out dir> [MODS]
  local dir=$1
  env SHOTS="$MENU_SHOTS" ${2:+MODS=$2 MODS_DIR=$MODS_DIR} tools/native/race_check.sh "$NP" "$dir" > "$dir.verdict.txt" 2>&1
  tail -1 "$dir.verdict.txt"
}
echo "text_mod_check: the modded run ..."
mkdir -p "$OUT"
V1=$(run "$OUT/rc_mod" proof-text)
if [ -n "${REF:-}" ]; then R=$REF; V2="race_check: PASS (REF $REF, not checked here)"
else
  echo "text_mod_check: the plain run ..."
  R=$OUT/rc_plain
  V2=$(run "$R")
fi
why=""
case $V1 in "race_check: PASS"*) ;; *) why="$why; modded run: $V1" ;; esac
case $V2 in "race_check: PASS"*) ;; *) why="$why; plain run: $V2" ;; esac
L1=$OUT/rc_mod/native_play.log
grep -q '^native_play: mods: active (--mods; the first wins): proof-text 1 (1 file); set hash [0-9a-f]\{16\}' "$L1" || why="$why; no mod set line in the modded log"
grep -q '^mods: DATA/GLOBALUS.BIN from proof-text' "$L1" || why="$why; GLOBALUS.BIN not served from the mod"
grep -q '^native_play: mods: none' "$R/native_play.log" || why="$why; the plain run's log has no 'mods: none'"
for q in ${MENU_SHOTS//,/ }; do
  a=$OUT/rc_mod/shots/live_${q}_0.png; b=$R/shots/live_${q}_0.png
  [ -f "$a" ] && [ -f "$b" ] || { why="$why; no menu shot $q"; continue; }
  if cmp -s "$a" "$b"; then why="$why; menu shot $q equal (the label did not change)"; fi
done
for q in 10700 11500; do
  cmp -s "$OUT/rc_mod/shots/live_${q}_0.png" "$R/shots/live_${q}_0.png" || why="$why; race shot $q differs"
done
SET=$(grep -o 'set hash [0-9a-f]\{16\}' "$L1" | head -1)
if [ -z "$why" ]; then
  echo "text_mod_check: PASS the mod's GLOBALUS.BIN served (${SET:-?}); main-menu shots $MENU_SHOTS differ from the plain run, race shots 10700 / 11500 equal (logs $L1, $R/native_play.log)"
else
  echo "text_mod_check: FAIL ${why#; }"
  exit 1
fi
