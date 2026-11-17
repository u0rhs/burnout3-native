#!/bin/bash
# blender_check.sh -- the Blender add-on (tools/blender/b3_car_tools; docs/modding/tutorials/09-car-mods-in-blender.md)
# and a route through Blender's own glTF importer and exporter (tutorial 10), in a real Blender, headless; then the mods
# Blender made in offscreen races. Skips (exit 0) when there is no Blender.
#
# Run with Blender 4.2.23 LTS (the official linux-x64 build), headless: NO_RACE=1 under several PYTHONHASHSEED values
# (Blender's glTF importer matches a mesh's "_" attributes with their data in a set's order, which the string hashing
# of each run decides; b3modtool writes one float SCALAR per component so that no order matters), and once with the
# races. The parts without Blender are tested by tools/blender/test_b3_car_tools.py (make test); build/model_import_check
# ("through Blender") models what the importer and exporter do to every car.
#
#   tools/modding/blender_check.sh [native_play] [out dir]
#     native_play   default build/native_play (the race part; NO_RACE=1 leaves it out)
#     out dir       default build/blender_check (the add-on's exports, the round-trip files, the mods, the runs; game
#                   data: delete it after)
#   Environment: BLENDER (the blender program; default: blender on PATH), B3_GAME_DIR (a folder of the disc's files),
#   MODTOOL (default build/b3modtool), CARS (the cars to import, "|" between them; default three of different classes),
#   TRACK (the route for the track part; default the race's route, with NO_RACE=1 US/C2_V2; NO_TRACK=1 leaves it out),
#   SAVES (as race_check.sh), REF (a no-mod race_check out dir of the same build: that run is not repeated),
#   PYTHONHASHSEED (passed on to Blender: a fixed attribute order)
#
# What it does:
#   1. `blender --command extension validate` on the add-on's zip (make_addon_zip.py): the manifest is one Blender takes;
#   2. Blender -b with its own user folders under <out dir> (your Blender's settings are not touched), running
#      tools/blender/blender_check.py: installs and enables the add-on, imports the cars with File > Import > Burnout 3
#      Car, checks each, exports each unedited to a car file and compares it with the disc's (byte for byte), then
#      scales the first car's LOD 3 body and panels 1.3x in object mode and exports that as a mod folder;
#   3. the route: `b3modtool track export`, then tools/blender/blender_track_check.py in Blender (its glTF importer and
#      exporter, no add-on): exported unedited -- `track import` of it writes nothing (the route's STATIC.DAT and
#      STREAMED.DAT byte for byte) -- and with the green taken out of every high-detail section's colours and the
#      first section lifted 0.5 m -- `track import` of it into a mod folder: every mesh unchanged or edited in place,
#      the files' sizes kept, verify OK;
#   4. race_check.sh without a mod, with the car mod and with the track mod: all PASS, the game read the car / the
#      route's files from the mods, and the race shots changed (the first car is Compact Type 1, one of the race's
#      cars with the saves the checks use).
# PASS: the last line "blender_check: PASS ...".
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/blender_check}
MT=${MODTOOL:-build/b3modtool}
. tools/local_env.sh; b3_local_env .
GAME=${B3_GAME_DIR:-data/game}
CARS=${CARS:-Compact Type 1|Classic Hotrod|Fire Truck}
MOD_NAME=blender-car
TRACK_MOD=blender-track
RACE_SHOTS="10700 11500"
fail() { echo "blender_check: FAIL $*"; exit 1; }

[ -z "${BLENDER:-}" ] || [ -x "$BLENDER" ] || fail "BLENDER=$BLENDER is not a program"
BL=${BLENDER:-$(command -v blender 2>/dev/null || true)}
if [ -z "$BL" ]; then
  echo "blender_check: SKIP no Blender (BLENDER=<the blender program>, or blender on PATH)"
  exit 0
fi
[ -x "$MT" ] || fail "no $MT (make modtool)"
[ -d "$GAME" ] || fail "B3_GAME_DIR ($GAME) is not a folder of the disc's files"
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
case $GAME in /*) ;; *) GAME=$PWD/$GAME ;; esac
case $MT in /*) ;; *) MT=$PWD/$MT ;; esac
rm -rf "$OUT"
mkdir -p "$OUT"
echo "blender_check: $("$BL" --version 2>/dev/null | head -1) ($BL)${PYTHONHASHSEED:+, PYTHONHASHSEED=$PYTHONHASHSEED}"

# ---- 1. the zip, and Blender's own check of its manifest ----
python3 -I tools/blender/make_addon_zip.py "$OUT/b3_car_tools.zip" || fail "make_addon_zip"
if "$BL" --help 2>/dev/null | grep -q -- '--command'; then
  "$BL" --factory-startup --command extension validate "$OUT/b3_car_tools.zip" > "$OUT/validate.txt" 2>&1 \
    || fail "the extension manifest: $(tail -3 "$OUT/validate.txt" | tr '\n' ' ')"
  echo "blender_check: extension validate: $(tail -1 "$OUT/validate.txt")"
else
  echo "blender_check: this Blender has no --command (older than 4.2): the manifest is not validated"
fi

# ---- 2. the add-on in Blender (its own user folders) ----
export BLENDER_USER_CONFIG=$OUT/blender-user/config BLENDER_USER_SCRIPTS=$OUT/blender-user/scripts
export BLENDER_USER_DATAFILES=$OUT/blender-user/datafiles BLENDER_USER_EXTENSIONS=$OUT/blender-user/extensions
mkdir -p "$BLENDER_USER_CONFIG" "$BLENDER_USER_SCRIPTS" "$BLENDER_USER_DATAFILES" "$BLENDER_USER_EXTENSIONS"
"$BL" -b --factory-startup --python-exit-code 1 --python tools/blender/blender_check.py -- \
    --zip "$OUT/b3_car_tools.zip" --modtool "$MT" --disc "$GAME" --out "$OUT" --cars "$CARS" --mod-name "$MOD_NAME" \
    > "$OUT/blender.txt" 2>&1
grep '^blender_check' "$OUT/blender.txt"
grep '^blender_check(blender): \(PASS\|FAIL\)' "$OUT/blender.txt" | tail -1 | grep -q 'PASS$' || fail "in Blender: $(grep '^blender_check(blender)' "$OUT/blender.txt" | tail -1) (log $OUT/blender.txt)"
MODS_DIR=$OUT/mods
"$MT" verify "$MODS_DIR/$MOD_NAME" --game "$GAME" > "$OUT/verify.txt" 2>&1 || fail "verify: $(tail -1 "$OUT/verify.txt")"
ROUND=$(grep -c 'unedited round trip through Blender: the disc' "$OUT/blender.txt")
SUMMARY="the add-on in Blender ($ROUND cars imported and round-tripped byte for byte; Compact Type 1 scaled 1.3x as a mod, verify OK)"

run() {   # run <out dir> [MODS]
  env ${2:+MODS=$2 MODS_DIR=$MODS_DIR} tools/native/race_check.sh "$NP" "$1" > "$1.verdict.txt" 2>&1
  tail -1 "$1.verdict.txt"
}
shots_changed() {   # shots_changed <plain run> <modded run>: the race shots that differ
  local c="" q a b
  for q in $RACE_SHOTS; do
    a=$1/shots/live_${q}_0.png b=$2/shots/live_${q}_0.png
    [ -f "$a" ] && [ -f "$b" ] || { echo "missing"; return; }
    cmp -s "$a" "$b" || c="$c $q"
  done
  echo "$c"
}
R=""
if [ "${NO_RACE:-0}" != 1 ]; then
  [ -x "$NP" ] || fail "no $NP (make play; NO_RACE=1 to leave the race out)"
  if [ -n "${REF:-}" ]; then R=$REF; V2="race_check: PASS (REF $REF)"
  else echo "blender_check: the plain run ..."; R=$OUT/rc_plain; V2=$(run "$R"); fi
  case $V2 in "race_check: PASS"*) ;; *) fail "plain run: $V2" ;; esac
fi

# ---- 3. a route through Blender's glTF importer and exporter ----
if [ "${NO_TRACK:-0}" != 1 ]; then
  ROUTE=${TRACK:-}
  if [ -z "$ROUTE" ] && [ -n "$R" ]; then
    ROUTE=$(grep -o '^\[b3live\] route [^ ]* from the game' "$R/native_play.log" | head -1 | sed 's/^\[b3live\] route //; s/ from the game$//')
    [ -n "$ROUTE" ] || fail "no route line in $R/native_play.log"
  fi
  ROUTE=${ROUTE:-US/C2_V2}
  KEY=$(echo "$ROUTE" | tr '\\' '/' | awk -F/ '{print toupper($(NF-1)) "/" toupper($NF)}')
  T=$OUT/track
  mkdir -p "$T"
  "$MT" track export "$GAME" "$KEY" "$T/export" > "$T/export.txt" 2>&1 || fail "track export $KEY: $(tail -1 "$T/export.txt")"
  MODEL=$(ls "$T"/export/*.gltf)
  SIDE=$(ls "$T"/export/*.b3track.json)
  "$BL" -b --factory-startup --python-exit-code 1 --python tools/blender/blender_track_check.py -- \
      --model "$MODEL" --out "$T" > "$T/blender.txt" 2>&1
  grep '^blender_check(track)' "$T/blender.txt"
  grep '^blender_check(track): \(PASS\|FAIL\)' "$T/blender.txt" | tail -1 | grep -q 'PASS$' || fail "the route in Blender: $(grep '^blender_check(track)' "$T/blender.txt" | tail -1) (log $T/blender.txt)"
  # unedited: nothing to write (the import's files are the disc's)
  "$MT" track import "$GAME" "$T/track_unedited.glb" "$T/unedited_mod" --sidecar "$SIDE" > "$T/import_unedited.txt" 2>&1 \
    || fail "track import of the unedited round trip: $(tail -1 "$T/import_unedited.txt")"
  L=$(grep '^track import:' "$T/import_unedited.txt")
  echo "blender_check: unedited: $L"
  case $L in *"nothing changed: no patch written"*) ;; *) fail "the unedited round trip of $KEY through Blender is not the disc's files" ;; esac
  N=$(echo "$L" | sed -n 's/.*: \([0-9]*\) meshes in the model (\([0-9]*\) unchanged.*/\1 \2/p')
  set -- $N
  [ "${1:-x}" = "${2:-y}" ] || fail "the unedited round trip: not every mesh unchanged ($L)"
  # the edit: in place, the sizes kept
  mkdir -p "$MODS_DIR/$TRACK_MOD"
  cat > "$MODS_DIR/$TRACK_MOD/mod.ini" <<INI
[mod]
id = $TRACK_MOD
name = Blender check: the race's route without green, a section lifted
version = 1
author = blender_check.sh
description = The route edited in Blender (a test; made from your own disc, never shared).
api = 1
INI
  "$MT" track import "$GAME" "$T/track_edit.glb" "$MODS_DIR/$TRACK_MOD" --sidecar "$SIDE" > "$T/import_edit.txt" 2>&1 \
    || fail "track import of the Blender edit: $(tail -1 "$T/import_edit.txt")"
  L=$(grep '^track import:' "$T/import_edit.txt")
  echo "blender_check: edited: $L"
  case $L in *" 0 with new triangles;"*" 0 packets moved, 0 blocks grown,"*) ;; *) fail "the Blender edit was not imported in place" ;; esac
  "$MT" verify "$MODS_DIR/$TRACK_MOD" --game "$GAME" > "$T/verify.txt" 2>&1 || fail "verify the track mod: $(tail -1 "$T/verify.txt")"
  SUMMARY="$SUMMARY; $KEY through Blender's glTF import / export: unedited the disc's files, edited in place (verify OK)"
fi

if [ "${NO_RACE:-0}" = 1 ]; then
  echo "blender_check: PASS $SUMMARY; races left out (NO_RACE=1)"
  exit 0
fi

# ---- 4. the mods in races ----
echo "blender_check: the run with the car mod ..."
V1=$(run "$OUT/rc_mod" "$MOD_NAME")
case $V1 in "race_check: PASS"*) ;; *) fail "the run with the car mod: $V1" ;; esac
L1=$OUT/rc_mod/native_play.log
grep -q "^mods: PVEH/COMP/CAR1\.BGV from $MOD_NAME" "$L1" || fail "the game did not read PVEH/COMP/CAR1.BGV from the mod ($L1)"
changed=$(shots_changed "$R" "$OUT/rc_mod")
[ "$changed" != missing ] || fail "no race shots"
[ -n "$changed" ] || fail "no race shot changed with the scaled car"
SUMMARY="$SUMMARY; race_check PASS with and without the car mod, the game read PVEH/COMP/CAR1.BGV from it, race shots changed:$changed"
if [ "${NO_TRACK:-0}" != 1 ]; then
  echo "blender_check: the run with the track mod ..."
  V3=$(run "$OUT/rc_track" "$TRACK_MOD")
  case $V3 in "race_check: PASS"*) ;; *) fail "the run with the track mod: $V3" ;; esac
  L3=$OUT/rc_track/native_play.log
  grep -q "^mods: TRACKS/$KEY/STREAMED.DAT from $TRACK_MOD" "$L3" || fail "the game did not read TRACKS/$KEY/STREAMED.DAT from the track mod ($L3)"
  changed=$(shots_changed "$R" "$OUT/rc_track")
  [ "$changed" != missing ] || fail "no race shots (track mod)"
  [ -n "$changed" ] || fail "no race shot changed with the track mod"
  SUMMARY="$SUMMARY; race_check PASS with the track mod, the game read TRACKS/$KEY/STREAMED.DAT from it, race shots changed:$changed"
fi
echo "blender_check: PASS $SUMMARY"
