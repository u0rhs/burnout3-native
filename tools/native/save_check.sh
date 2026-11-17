#!/bin/bash
# save_check.sh -- save1 (remaster-features §8): the memory card as the port's own profile file, end to end. Two
# offscreen native_play runs on one fresh saves folder (OUT/card), scripted keys:
#   1. new profile: title -> NEW PROFILE -> name "BURNOUT" (Done) -> PROFILE 1 -> main menu. The card messages
#      ("Checking / Saving ... memory card", "Would you like to save your profile?", "Save successful") must not be
#      shown (tools/native/card_silence.h answers them) and profile-1.sav must hold the game's save afterwards.
#   2. restart on that folder: title -> LOAD PROFILE -> PROFILE 1 -> main menu ("Load successful" answered), then
#      SETTINGS > SAVES > EXPORT SAVE (the file dialog answered by $B3NATIVE_SAVE_DIALOG): the export must equal the
#      profile, and loading must not have changed profile-1.sav.
#
#   tools/native/save_check.sh [native_play] [out dir]      (default build/native_play, build/save_check)
#   Environment: B3_GAME_DIR (the disc folder, default data/game), STRICT=1 (no retail fallback), SHOTS (more polls).
# Verdict (last line): "save_check: PASS ..." or "save_check: FAIL <why>"; exit 0 / 1. Shots: OUT/run1, OUT/run2.
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/save_check}
GAME=${B3_GAME_DIR:-data/game}
[ -x "$NP" ] || { echo "save_check: FAIL no $NP"; exit 1; }
[ -f "$GAME/SLUS_210.50" ] || { echo "save_check: FAIL no disc folder ($GAME)"; exit 1; }
make -s build/save_tool >/dev/null || { echo "save_check: FAIL cannot build save_tool"; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT/card" "$OUT/run1" "$OUT/run2"
ENVS=(B3HOST_FATAL_ABORT=1 B3NATIVE_SAVES_ROOT="$OUT/card" B3NATIVE_CARD_PRESENT_VBLANK=0 B3NATIVE_SAVE_DIALOG="$OUT/export.sav")
STRICTARG=(); [ "${STRICT:-0}" = 1 ] && STRICTARG=(--strict)
run() {   # run <name> <vblanks> <keys> <shots>
  env "${ENVS[@]}" B3NATIVE_MAX_VBLANKS="$2" prlimit --as=10737418240 "$NP" --game "$GAME" --offscreen --no-pace "${STRICTARG[@]}" \
    --keys "$3" --record-pad "$OUT/$1/pads.txt" --render-args "--offscreen-live $OUT/$1 --shots $4${SHOTS:+,$SHOTS}" > "$OUT/$1/log.txt" 2>&1
}
press() { echo "$1:$2:1,$(($1 + ${3:-10})):$2:0"; }   # VBlank, SDL scancode[, VBlanks held]
# keys (mac4's layout): Escape (41) Start, Z (29) Cross, Down (81), R (21) R1 (the page's next tab)
K1="$(press 9000 41),$(press 9500 81),$(press 9700 29),$(press 10200 29),$(press 11400 29)"
run run1 13000 "$K1" 4700,5000,6400
why=""
grep -q '^crash:' "$OUT/run1/log.txt" && why="run 1 crashed"
[ -f "$OUT/card/profile-1.sav" ] || why="${why:+$why; }run 1 wrote no profile-1.sav"
build/save_tool info "$OUT/card/profile-1.sav" > "$OUT/run1/sav.txt" 2>&1 || why="${why:+$why; }profile-1.sav unreadable"
grep -q "BASLUS-21050 .* 61772 bytes" "$OUT/run1/sav.txt" || why="${why:+$why; }profile-1.sav lacks the game's save"
for m in "message 2257 (type 1) not shown, answered YES" "message 171 (type 0) not shown" "message 2256 (type 2) not shown, answered OK"; do
  grep -qF "card_silence: $m" "$OUT/run1/log.txt" || why="${why:+$why; }run 1: no \"$m\""
done
# a card message the game showed after all (its text from GLOBALUS.BIN: any mentioning the memory card)
card_shown() {
  grep -o 'card_silence: message [0-9]* (type [0-9]*) shown' "$1" | awk '{print $3}' | sort -u | python3 -I -c '
import struct, sys
d = open(sys.argv[1], "rb").read()
n = struct.unpack_from("<I", d, 8)[0]
for line in sys.stdin:
    i = int(line)
    o = struct.unpack_from("<I", d, 0x10 + 4 * i)[0] if i < n else 0
    e = o
    while e + 1 < len(d) and (d[e] or d[e + 1]): e += 2
    s = d[o:e].decode("utf-16-le", "replace").replace("\xa0", " ")
    if "memory card" in s.lower(): print(i, s[:60])
' "$GAME/DATA/GLOBALUS.BIN"
}
shown1=$(card_shown "$OUT/run1/log.txt")
[ -z "$shown1" ] || why="${why:+$why; }run 1 showed card messages: $shown1"
cp "$OUT/card/profile-1.sav" "$OUT/run1/profile-1.sav"
# run 2: load it, then SETTINGS (main menu row 6: Down x5) > SAVES (the 6th tab: R1 x5) > EXPORT SAVE (Down x3)
# (the page reads the pad on the renderer's frames, which an unpaced run may space out: page keys are held 40 VBlanks)
K2="$(press 9000 41),$(press 9600 29),$(press 10200 29)"
for v in 11600 11660 11720 11780 11840; do K2="$K2,$(press $v 81)"; done
K2="$K2,$(press 12000 29)"
for v in 12300 12400 12500 12600 12700; do K2="$K2,$(press $v 21 40)"; done
K2="$K2,$(press 12800 81 40),$(press 12900 81 40),$(press 13000 81 40),$(press 13100 29 40),$(press 13500 41 40)"
run run2 14200 "$K2" 4800,5300,6200,6540
grep -q '^crash:' "$OUT/run2/log.txt" && why="${why:+$why; }run 2 crashed"
grep -qF "card_silence: message 205 (type 2) not shown, answered OK" "$OUT/run2/log.txt" || why="${why:+$why; }run 2: the profile did not load (no \"Load successful\")"
shown2=$(card_shown "$OUT/run2/log.txt")
[ -z "$shown2" ] || why="${why:+$why; }run 2 showed card messages: $shown2"
cmp -s "$OUT/card/profile-1.sav" "$OUT/run1/profile-1.sav" || why="${why:+$why; }loading changed profile-1.sav"
grep -q "\[save\] EXPORTED TO" "$OUT/run2/log.txt" || why="${why:+$why; }run 2: no export"
if [ -f "$OUT/export.sav" ]; then
  build/save_tool info "$OUT/export.sav" | grep -v "^BASLUS-21050  (" > "$OUT/run2/export.txt"
  grep -v "^BASLUS-21050  (" "$OUT/run1/sav.txt" | cmp -s - "$OUT/run2/export.txt" || why="${why:+$why; }the export differs from the profile"
fi
if [ -z "$why" ]; then
  echo "save_check: PASS new profile saved silently to profile-1.sav, loaded on restart, exported from SETTINGS (logs $OUT/run1, $OUT/run2)"
  exit 0
fi
echo "save_check: FAIL $why"
exit 1
