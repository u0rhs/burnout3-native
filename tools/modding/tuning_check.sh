#!/bin/bash
# tuning_check.sh -- the tuning console in the game (mod kit part 4, docs/modding/tuning.md): offscreen runs of
# race_check.sh's race scenario, one at a time.
#
#   tools/modding/tuning_check.sh [native_play] [out dir]
#     native_play   default build/native_play
#     out dir       default build/tuning_check (logs, shots, the test mod; game data: delete it after)
#   Environment: B3_GAME_DIR, SAVES (as race_check.sh); REF: a golden baseline of the same disc
#   (tools/golden/baseline.sh; its race/ram.txt and race/shots.txt), default build/golden_ref; TUNE_AT: the VBlank of the
#   scripted edit (default 21000, while driving)
#
# The runs (each STRICT=1 with B3NATIVE_RAM_HASH=1-100000:100, as the golden baseline):
#   reg     --tuning on and B3NATIVE_TUNE_LOG=1: the registry recorded (every registration logged, the count at the end).
#           Read only: its RAM hashes and shots must equal REF's race run (the console on changes nothing by itself)
#   preset  a test mod made here (under the out dir only) with a [tuning] preset: every car's follow camera ("Camera/Follow/
#           | Camera Offset", z) twice as far as reg's log shows it, per config file. The log must show the mod flagged as
#           changing gameplay and the preset written as the cameras register; the intro shot 10700 (its own camera) equal
#           to reg's, the race shot 11500 (the follow camera) different
#   rec     --tune "<TUNE_AT>:Camera/Follow/ | Camera Offset[2]=<that z x 2>" with --record-pad and --record-replay: the
#           edit logged, a "tune" line in the pad recording; RAM equal to reg's before TUNE_AT, different after
#   pad     --replay-pad of rec's pad recording (no --tune): the edit from the recording
#   file    --replay of rec's replay file (no --tune): the edit from the replay file
#   ui      tunenav1: a session through the F9 overlay's own keys (B3TUNE_OVERLAY_SCRIPT): a HANDLING value stepped, then
#           the YOUR CAR page's boost, FILL BOOST and FREEZE DAMAGE, an ENGINE & SPEED value stepped; tunenav2: then the
#           CHANGED tab, the HANDLING value reset from there (R), RESET ALL asked and answered no (Escape), asked again
#           and answered yes (Enter); recorded (pad recording and replay file); the overlay's pages as extra shots
#           (B3TUNE_OVERLAY_SHOTS: live_<poll>_<alpha>_tune<k>.png; at 10700 the edits are in effect, at 11500 reset)
#   uipad / uifile  ui's pad recording / replay file played back
# PASS (last line "tuning_check: PASS ...") when every run PASSes race_check and: pad and file have every RAM hash, shot
# hash and tuning edit line equal to rec's, rec parts from reg exactly at the edit, uipad and uifile equal ui the same
# way (the resets included), and the rest above holds.
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/tuning_check}
REF=${REF:-build/golden_ref}
AT=${TUNE_AT:-21000}
GRID=1-100000:100
case $NP in /*) ;; *) NP=$PWD/$NP ;; esac
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
fail() { echo "tuning_check: FAIL $*"; exit 1; }
[ -x "$NP" ] || fail "no $NP"
mkdir -p "$OUT"
why=""
run() {   # run <name> [VAR=value ...]: race_check into $OUT/<name>; its RAM hashes, shots and tuning lines beside it
  local n=$1; shift
  rm -rf "$OUT/$n"
  env "$@" STRICT=1 B3NATIVE_RAM_HASH=$GRID tools/native/race_check.sh "$NP" "$OUT/$n" > "$OUT/$n.verdict.txt" 2>&1
  local v; v=$(tail -1 "$OUT/$n.verdict.txt")
  echo "tuning_check: $n: ${v:0:150}"
  case $v in "race_check: PASS"*) ;; *) why="$why; $n: race_check did not PASS" ;; esac
  grep -o '^native_play: VBlank [0-9]*: ram\( [0-9a-f]\{8\}\)*' "$OUT/$n/native_play.log" | sed 's/^native_play: //' > "$OUT/$n.ram.txt"
  grep '^race_check: shot ' "$OUT/$n.verdict.txt" | awk '{print $3, $4}' > "$OUT/$n.shots.txt"
  grep -o -e 'tuning: .* on [0-9]* variables\?' -e "tuning: car: .* on the player's car" "$OUT/$n/native_play.log" | sed 's/ (from the recording)//' > "$OUT/$n.edits.txt"
}
same() { cmp -s "$1" "$2"; }

# reg: the registry, read only
run reg B3NATIVE_TUNING=1 B3NATIVE_TUNE_LOG=1
L=$OUT/reg/native_play.log
COUNT=$(grep -o 'native_play: tuning: [0-9]* AddEntry calls.*' "$L" | head -1)
echo "tuning_check: reg: ${COUNT:-no count line}"
[ -n "$COUNT" ] || why="$why; reg: no registry count"
if [ -f "$REF/race/ram.txt" ]; then
  same "$OUT/reg.ram.txt" "$REF/race/ram.txt" && echo "tuning_check: reg: RAM hashes equal to $REF (the console only reads)" || why="$why; reg: RAM differs from $REF/race"
  same "$OUT/reg.shots.txt" "$REF/race/shots.txt" && echo "tuning_check: reg: shots equal to $REF" || why="$why; reg: shots differ from $REF/race"
else
  echo "tuning_check: reg: no $REF/race (a golden baseline): the read-only comparison is skipped"
fi

# the test mod: every registered follow-camera offset with z doubled, per config file
MODS_DIR=$OUT/mods
rm -rf "$MODS_DIR"; mkdir -p "$MODS_DIR/proof-tuning/tuning"
cat > "$MODS_DIR/proof-tuning/mod.ini" <<'INI'
[mod]
id = proof-tuning
name = Proof: the follow camera further back (a tuning preset)
version = 1
author = tuning_check.sh
description = Every car's follow camera twice as far behind (a test made from the values of your own disc; never shared).
api = 1

[tuning]
preset = tuning/camera.ini
INI
python3 -I - "$L" "$MODS_DIR/proof-tuning/tuning/camera.ini" "$OUT/tune_value.txt" <<'PY' || fail "no follow-camera registration in the reg log"
import re, sys
seen, out, first = set(), [], None
pat = re.compile(r'tuning: #\d+ vector Camera/Follow/ \| Camera Offset \((.*)\) = \(([^,]+), ([^,]+), ([^,]+), [^)]*\) key')
for line in open(sys.argv[1], errors='replace'):
    m = pat.search(line)
    if not m or m.group(1) in seen:
        continue
    seen.add(m.group(1))
    z = float(m.group(4)) * 2
    out += ['[%s]\n' % m.group(1), 'Camera/Follow/ | Camera Offset[2] = %r\n' % z]
    first = first if first is not None else z
if not out:
    sys.exit(1)
open(sys.argv[2], 'w').write('# tuning_check.sh: the follow camera twice as far behind\n' + ''.join(out))
open(sys.argv[3], 'w').write('%r\n' % first)
print('tuning_check: preset of %d follow-camera offsets' % len(seen))
PY
Z=$(cat "$OUT/tune_value.txt")

# preset: the mod in a race
run preset MODS=proof-tuning MODS_DIR="$MODS_DIR"
P=$OUT/preset/native_play.log
grep -q 'native_play: tuning: preset of proof-tuning (tuning/camera.ini): [0-9]* lines\?; this mod changes gameplay' "$P" || why="$why; preset: not flagged as changing gameplay in the log"
W=$(grep -c 'tuning: proof-tuning registered: Camera/Follow/ | Camera Offset z' "$P")
echo "tuning_check: preset: $W follow-camera offsets written as they registered; e.g. $(grep -m1 -o 'tuning: proof-tuning registered: Camera/Follow/ | Camera Offset z.*' "$P")"
[ "$W" -ge 1 ] || why="$why; preset: nothing written"
same "$OUT/preset/shots/live_10700_0.png" "$OUT/reg/shots/live_10700_0.png" || why="$why; preset: the intro shot 10700 differs (only the follow camera should)"
same "$OUT/preset/shots/live_11500_0.png" "$OUT/reg/shots/live_11500_0.png" && why="$why; preset: the race shot 11500 is equal (the camera did not move)"

# rec / pad / file: a scripted edit, recorded, played back two ways
TUNE="$AT:Camera/Follow/ | Camera Offset[2]=$Z"
run rec B3NATIVE_TUNE="$TUNE" RECORD_REPLAY="$OUT/rec.b3replay"
grep -q '^tune [0-9]* 0x' "$OUT/rec/pads.txt" || why="$why; rec: no tune line in the pad recording"
echo "tuning_check: rec: $(grep -c '^tune ' "$OUT/rec/pads.txt") tune lines in the pad recording, e.g. $(grep -m1 '^tune ' "$OUT/rec/pads.txt")"
[ -s "$OUT/rec.edits.txt" ] || why="$why; rec: no edit in the log"
before=$(awk -v at="$AT" '{split($2, a, ":"); if (a[1] + 0 < at) print}' "$OUT/rec.ram.txt" | md5sum)
before_reg=$(awk -v at="$AT" '{split($2, a, ":"); if (a[1] + 0 < at) print}' "$OUT/reg.ram.txt" | md5sum)
[ "$before" = "$before_reg" ] || why="$why; rec: RAM differs from reg before the edit"
same "$OUT/rec.ram.txt" "$OUT/reg.ram.txt" && why="$why; rec: RAM equal to reg after the edit (it did nothing)"
cp "$OUT/rec/pads.txt" "$OUT/rec.pads.txt"
run pad REPLAY_PAD="$OUT/rec.pads.txt"
run file REPLAY="$OUT/rec.b3replay"
for n in pad file; do
  r=""; s=""; e=""
  same "$OUT/$n.ram.txt" "$OUT/rec.ram.txt" && r=equal || { r=DIFFERENT; why="$why; $n: RAM differs from rec"; }
  same "$OUT/$n.shots.txt" "$OUT/rec.shots.txt" && s=equal || { s=DIFFERENT; why="$why; $n: shots differ from rec"; }
  same "$OUT/$n.edits.txt" "$OUT/rec.edits.txt" && e=equal || { e=DIFFERENT; why="$why; $n: tuning edits differ from rec"; }
  echo "tuning_check: $n: $(wc -l < "$OUT/$n.ram.txt") RAM hashes $r, $(wc -l < "$OUT/$n.shots.txt") shots $s, $(wc -l < "$OUT/$n.edits.txt") tuning edits $e to rec's"
done
grep -h -o 'replay: [0-9]* of [0-9]* checkpoints matched; .*' "$OUT/file/native_play.log" | sed 's/^/tuning_check: file: /'

# tunenav1 (the console's tabs and its car page): ui, a session driven through the F9 overlay itself (offscreen:
# B3TUNE_OVERLAY_SCRIPT, the overlay's own key handling; polls are about VBlank / 2, and the race cars' physics register
# at VBlank ~20800, poll ~10570): open it; E E E to the HANDLING tab (past CHANGED and MOST USED); Right Right into its
# first section and the player's car; Right steps the first value; Q Q Q to YOUR CAR; Down, Left (the boost one step
# down), Down x5 and Enter (FILL BOOST), Down and Enter (FREEZE DAMAGE on); E x4 to ENGINE & SPEED, Right Right Right
# (its first value stepped). tunenav2, after the shot at 10700 (the overlay's pages with the edits in effect): Q Q Q to
# CHANGED (RESET ALL, YOUR CAR's FREEZE DAMAGE, the HANDLING value, the ENGINE & SPEED one), Down Down to the HANDLING
# value, R (back to its default); Ctrl+R (RESET ALL asks), Escape (no: nothing reset); Ctrl+R, Enter (yes: the ENGINE &
# SPEED value back, FREEZE DAMAGE off); close. The edits and resets reach native_play at its next VBlank (whichever
# that is) and are recorded; uipad and uifile play them back and must give the same game. B3TUNE_OVERLAY_SHOTS: the
# overlay's pages over the race shots (live_<poll>_<alpha>_tune<k>.png): k 0-14 the tabs, 15 HANDLING's first section,
# 16 its player's car (the markers), 17 CHANGED with the RESET ALL question, 18 CHANGED with a controller's keys, 19
# HANDLING's first section with a controller's keys, 20 a search
UI="10590:open;10593:key:E;10596:key:E;10599:key:E;10602:key:Right;10605:key:Right;10608:key:Right"
UI="$UI;10611:key:Q;10614:key:Q;10617:key:Q;10620:key:Down;10623:key:Left;10626:key:Down;10628:key:Down;10630:key:Down;10632:key:Down"
UI="$UI;10634:key:Down;10637:key:Return;10640:key:Down;10643:key:Return"
UI="$UI;10646:key:E;10649:key:E;10652:key:E;10655:key:E;10658:key:Right;10661:key:Right;10664:key:Right"
UI="$UI;10720:key:Q;10723:key:Q;10726:key:Q;10730:key:Down;10733:key:Down;10736:key:R"
UI="$UI;10750:key:R+ctrl;10754:key:Escape;10762:key:R+ctrl;10766:key:Return;10780:close"
UISHOTS="tab:0,tab:1,tab:2,tab:3,tab:4,tab:5,tab:6,tab:7,tab:8,tab:9,tab:10,tab:11,tab:12,tab:13,tab:14,tab:3/0,tab:3/0/0,tab:1+ask,tab:1+pad,tab:3/0+pad,search:mass"
run ui B3NATIVE_TUNING=1 B3TUNE_OVERLAY_SCRIPT="$UI" B3TUNE_OVERLAY_SHOTS="$UISHOTS" B3TUNE_FAVOURITES="$OUT/ui.favourites.ini" RECORD_REPLAY="$OUT/ui.b3replay"
U=$OUT/ui/native_play.log
grep -q '^tune [0-9]* 0xB3CA' "$OUT/ui/pads.txt" || why="$why; ui: no car edit in the pad recording"
grep -q 'tuning: Physics/Race Car/Misc | ' "$OUT/ui.edits.txt" || why="$why; ui: no HANDLING edit (the overlay's first value) in the log"
grep -q "tuning: car: fill boost = .* on the player's car" "$OUT/ui.edits.txt" || why="$why; ui: no FILL BOOST in the log"
grep -q "tuning: car: freeze damage on on the player's car" "$OUT/ui.edits.txt" || why="$why; ui: no FREEZE DAMAGE in the log"
# tunenav2: two values edited (not defaults), the one reset from CHANGED, RESET ALL refused once and done once (the
# other value and FREEZE DAMAGE), every reset an edit to the default in the recording
EDITED=$(grep -v '(the default)' "$OUT/ui.edits.txt" | grep -c ' on [0-9]* variables\?$')
DEFAULTS=$(grep -c '(the default) (.*) on [0-9]* variables\?$' "$OUT/ui.edits.txt")
[ "$EDITED" -ge 2 ] || why="$why; ui: fewer than two values edited ($EDITED)"
[ "$DEFAULTS" -eq "$EDITED" ] || why="$why; ui: $DEFAULTS values put back to their default, not the $EDITED edited"
grep -q '^\[b3tune\] reset: .* back to its default' "$U" || why="$why; ui: no single reset from the CHANGED page"
[ "$(grep -c '^\[b3tune\] reset all: asked' "$U")" -eq 2 ] || why="$why; ui: RESET ALL not asked twice"
[ "$(grep -c '^\[b3tune\] reset all: cancelled' "$U")" -eq 1 ] || why="$why; ui: RESET ALL not refused once"
[ "$(grep -c '^\[b3tune\] reset all: [0-9]* values back to their defaults' "$U")" -eq 1 ] || why="$why; ui: RESET ALL not done once"
[ "$(grep -c 'tuning: reset all: [0-9]* values\? back to the default' "$U")" -eq 1 ] || why="$why; ui: native_play did not reset all once"
grep -q "tuning: car: freeze damage off on the player's car" "$OUT/ui.edits.txt" || why="$why; ui: RESET ALL did not turn FREEZE DAMAGE off"
echo "tuning_check: ui: $(grep -c '^tune ' "$OUT/ui/pads.txt") tune lines in the pad recording; $EDITED values edited, $DEFAULTS put back; the edits: $(tr '\n' ';' < "$OUT/ui.edits.txt" | cut -c1-600)"
echo "tuning_check: ui: $(grep -h -e '^\[b3tune\] reset' "$U" | tr '\n' ';' | cut -c1-400)"
echo "tuning_check: ui: $(ls "$OUT/ui/shots"/live_*_tune*.png 2>/dev/null | wc -l) overlay shots in $OUT/ui/shots"
cp "$OUT/ui/pads.txt" "$OUT/ui.pads.txt"
# (the replays draw the same overlay shots: the shots' draws are then the same sequence as ui's)
run uipad REPLAY_PAD="$OUT/ui.pads.txt" B3NATIVE_TUNING=1 B3TUNE_OVERLAY_SHOTS="$UISHOTS"
run uifile REPLAY="$OUT/ui.b3replay" B3NATIVE_TUNING=1 B3TUNE_OVERLAY_SHOTS="$UISHOTS"
for n in uipad uifile; do
  r=""; s=""; e=""
  same "$OUT/$n.ram.txt" "$OUT/ui.ram.txt" && r=equal || { r=DIFFERENT; why="$why; $n: RAM differs from ui"; }
  same "$OUT/$n.shots.txt" "$OUT/ui.shots.txt" && s=equal || { s=DIFFERENT; why="$why; $n: shots differ from ui"; }
  same "$OUT/$n.edits.txt" "$OUT/ui.edits.txt" && e=equal || { e=DIFFERENT; why="$why; $n: tuning edits differ from ui"; }
  echo "tuning_check: $n: $(wc -l < "$OUT/$n.ram.txt") RAM hashes $r, $(wc -l < "$OUT/$n.shots.txt") shots $s, $(wc -l < "$OUT/$n.edits.txt") edits $e to ui's"
done
if [ -z "$why" ]; then
  echo "tuning_check: PASS registry recorded ($(echo "$COUNT" | grep -o '[0-9]* AddEntry calls')), read only; the preset mod flagged and written ($W writes), the race shot changed; the edit at VBlank $AT replayed from the pad recording and the replay file with equal RAM, shots and edits; the overlay session (a HANDLING value, the car page's boost, FILL BOOST, FREEZE DAMAGE, an ENGINE & SPEED value; one reset from CHANGED, RESET ALL refused then done) replayed the same (in $OUT)"
else
  echo "tuning_check: FAIL ${why#; }"
  exit 1
fi
