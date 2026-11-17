#!/bin/bash
# tutorial_check.sh -- the mod kit's tutorials 01 and 02 (docs/modding/tutorials/) repeated without a person: every
# b3modtool step the tutorials show, with the picture edits done by a script, then the two mods in the game through
# race_check runs, their shots checked against a run without mods. So the tutorials stay true as the tools change.
#
#   tools/modding/tutorial_check.sh [native_play] [out dir]
#     native_play   default build/native_play (libb3render.so beside it)
#     out dir       default build/tutorial_check: the work folders, the test mods, the runs' logs and shots.
#                   They hold the game's data (made from your disc): delete the folder after; never share it.
#   Environment: B3_GAME_DIR (the disc folder; tex-logo's key needs a folder), SAVES (as race_check.sh), B3MODTOOL
#   (default build/b3modtool: `make modtool`), B3HDKEYS (default build/b3hdkeys: `make build/b3hdkeys`), REF (a no-mod race_check out dir of the same build, taken with SHOTS=$TITLE_SHOTS,$MENU_SHOTS: that
#   run is not repeated), NO_GAME=1 (the tool steps only, no game runs).
#
# Tutorial 01 (blue-logo): txd list finds titlescreen_logo (FRONTEND.TXD raster 203, 512x256); txd export; its PNG's
# red and blue swapped (tools/modding/tutorial_png.py: the tutorial's "make the red letters blue"); txd import (1 raster
# changed, the original's size); mod.ini; verify --game OK; the same for B3Logo (GLOBAL.TXD raster 4, the menus' logo);
# the patch route: two diffs, the share copy verifies and packs, `pack` refuses the step-8 folder (whole TXDs) unless
# --force, `verify` flags an export folder inside a mod unless --allow-exports; the .b3mod unpacked as a player would:
# only mod.ini and the two .b3patch files (the game applies them: patches3); `apply` by hand equals the import. Two
# test mods beside it: bad-logo (a patch made from the blue FRONTEND.TXD, so it does not fit the disc) and tex-logo (a
# [textures] mod: the exported title logo with red and green swapped, twice its size, as a DDS named by its PCSX2 key
# from b3hdkeys). Tutorial 02 (menu-words): strings export has 230 = SINGLE EVENT and
# 234 = SET UP YOUR OWN RACE OR CRASH EVENTS; a two-line text imported (2 strings changed); mod.ini; verify --game OK.
# In the game (race_check runs: none, blue-logo as the player installed it, menu-words, tex-logo, each must PASS; and
# bad-logo, which must stop with its message): the logs name the mod and the files it served. blue-logo: both files
# served from their patches, the cache files equal the imported TXDs; in the title shots (TITLE_SHOTS) the big logo's
# area and in the main-menu shots (MENU_SHOTS) the corner logo's area turn from strongly red to strongly blue pixels
# (tutorial_png.py colours); race shots 10700 / 11500 equal the plain run's. menu-words: the title shots equal the
# plain run's, the main-menu shots differ (the text) while their logo area keeps its colours, race shot 11500 (driving)
# equal. (Its race shot 10700, the race's opening fly-by, is not compared: with this text the fly-by's camera is a
# different one, a repeatable difference; the same-length change of text_mod_check.sh keeps it equal.) tex-logo: the
# title shots' logo turns from red to green, the menu and race shots equal the plain run's. bad-logo: native_play
# exits 2 with "bad-logo: patches/DATA/FRONTEND.TXD.b3patch was made from another version of DATA/FRONTEND.TXD".
# Last line "tutorial_check: PASS ..." or "tutorial_check: FAIL <why>".
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/tutorial_check}
. tools/local_env.sh; b3_local_env .
GAME=${B3_GAME_DIR:-data/game}
MT=${B3MODTOOL:-build/b3modtool}
HDK=${B3HDKEYS:-build/b3hdkeys}
TITLE_SHOTS=${TITLE_SHOTS:-3800,4200}       # polls on the title screen ("Press START button to continue")
MENU_SHOTS=${MENU_SHOTS:-8850,9000,9150}    # the main menu: WORLD TOUR selected, then (9150) SINGLE EVENT
TITLE_BOX="0.15 0.38 0.85 0.68"             # the big logo on the title screen (fractions of the shot)
MENU_BOX="0.03 0.03 0.47 0.22"              # the menus' corner logo
fail() { echo "tutorial_check: FAIL $*"; exit 1; }
[ -x "$MT" ] || fail "no $MT (make modtool)"
[ -x "$HDK" ] || fail "no $HDK (make build/b3hdkeys)"
[ -d "$GAME" ] || [ -f "$GAME" ] || fail "B3_GAME_DIR ($GAME) is not the disc folder or image"
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
case $MT in /*) ;; *) MT=$PWD/$MT ;; esac
case $HDK in /*) ;; *) HDK=$PWD/$HDK ;; esac
PNGPY=$PWD/tools/modding/tutorial_png.py
rm -rf "$OUT/work" "$OUT/mods" "$OUT/share" "$OUT/player" "$OUT"/*.b3mod
mkdir -p "$OUT/work" "$OUT/mods" "$OUT/share"
cd "$OUT" || exit 1
# a disc file by its disc path: in a folder, matched without regard to case; in an image, the path inside it
disc() {
  local p
  if [ -d "$GAME" ]; then
    p=$(find "$GAME" -maxdepth 2 -ipath "$GAME/$1" -print -quit 2>/dev/null)
    [ -n "$p" ] && { echo "$p"; return; }
  fi
  echo "$GAME/$1"
}
TXD=$(disc DATA/FRONTEND.TXD)
GTXD=$(disc DATA/GLOBAL.TXD)
STR=$(disc DATA/GLOBALUS.BIN)
# step <log name> <expected regex in the output> <command...>: run it, keep its output, check rc 0 and the text
step() {
  local name=$1 want=$2 rc=0; shift 2
  "$@" > "work/$name.txt" 2>&1 || rc=$?
  [ $rc = 0 ] || { tail -20 "work/$name.txt"; fail "$name: exit $rc ($*)"; }
  grep -qE -- "$want" "work/$name.txt" || { tail -20 "work/$name.txt"; fail "$name: no \"$want\" in its output"; }
  echo "  $name: $(grep -E -- "$want" "work/$name.txt" | head -1)"
}
# step_fails: the same for a command that must refuse (exit 1) with that text
step_fails() {
  local name=$1 want=$2 rc=0; shift 2
  "$@" > "work/$name.txt" 2>&1 || rc=$?
  [ $rc = 1 ] || { tail -20 "work/$name.txt"; fail "$name: exit $rc, expected 1 ($*)"; }
  grep -qE -- "$want" "work/$name.txt" || { tail -20 "work/$name.txt"; fail "$name: no \"$want\" in its output"; }
  echo "  $name (refused, as it should): $(grep -E -- "$want" "work/$name.txt" | head -1 | cut -c1-160)"
}

echo "tutorial_check: tutorial 01 (replace a texture) ..."
step t1_list '^203  titlescreen_logo +512x256 +T8' "$MT" txd list "$TXD"
step t1_export '^txd export: 222 files into work/fe' "$MT" txd export "$TXD" work/fe
[ -f work/fe/203_titlescreen_logo.png ] || fail "no work/fe/203_titlescreen_logo.png"
cp work/fe/203_titlescreen_logo.png work/titlescreen_orig.png   # (tex-logo's picture, before the tutorial's edit)
step t1_edit 'red and blue swapped' python3 -I "$PNGPY" swap-rb work/fe/203_titlescreen_logo.png work/fe/203_titlescreen_logo.png
mkdir -p mods/blue-logo/data/DATA
step t1_import '^txd import: 221 PNGs, 1 rasters changed, .* the original.s size\)' \
  "$MT" txd import "$TXD" work/fe mods/blue-logo/data/DATA/FRONTEND.TXD
cat > mods/blue-logo/mod.ini <<'INI'
[mod]
id = blue-logo
name = Blue title logo
version = 1.0
author = you
description = The title screen's logo in blue.
api = 1
INI
step t1_verify '^verify: OK .*blue-logo 1.0, 2 files \(1 disc paths, 0 patches\), 0 errors' "$MT" verify mods/blue-logo --game "$GAME"
# step 8: the menus' logo
step t1_list_global '^  4  B3Logo +256x64 +T8' "$MT" txd list "$GTXD"
step t1_export_global '^txd export: [0-9]+ files into work/global' "$MT" txd export "$GTXD" work/global
step t1_edit_global 'red and blue swapped' python3 -I "$PNGPY" swap-rb work/global/004_B3Logo.png work/global/004_B3Logo.png
step t1_import_global '^txd import: [0-9]+ PNGs, 1 rasters changed, .* the original.s size\)' \
  "$MT" txd import "$GTXD" work/global mods/blue-logo/data/DATA/GLOBAL.TXD
step t1_verify2 '^verify: OK .*blue-logo 1.0, 3 files \(2 disc paths, 0 patches\), 0 errors' "$MT" verify mods/blue-logo --game "$GAME"
# step 9: patches, packed; then the player's side: the .b3mod unpacked into a mods folder as it is -- only the two
# .b3patch files: the game applies them (patches3)
mkdir -p share/blue-logo/patches/DATA
cp mods/blue-logo/mod.ini share/blue-logo/
step t1_diff '^diff: .* patch [0-9]+ bytes' "$MT" diff "$TXD" mods/blue-logo/data/DATA/FRONTEND.TXD share/blue-logo/patches/DATA/FRONTEND.TXD.b3patch
step t1_diff_global '^diff: .* patch [0-9]+ bytes' "$MT" diff "$GTXD" mods/blue-logo/data/DATA/GLOBAL.TXD share/blue-logo/patches/DATA/GLOBAL.TXD.b3patch
step t1_verify_share '^verify: OK .*\(0 disc paths, 2 patches\), 0 errors, 0 warnings' "$MT" verify share/blue-logo --game "$GAME"
step t1_pack '^pack: blue-logo-1.0.b3mod \(3 files\)' "$MT" pack share/blue-logo blue-logo-1.0.b3mod --game "$GAME"
# pack refuses the step-8 folder (whole TXDs that are mostly the disc's) unless --force, and says to ship patches
step_fails t1_pack_refused 'not packed: data/DATA/FRONTEND.TXD, data/DATA/GLOBAL.TXD are mostly the disc.s own file.*b3modtool diff.*--force' \
  "$MT" pack mods/blue-logo work/refused.b3mod --game "$GAME"
step t1_pack_forced '^pack: work/forced.b3mod \(3 files\)' "$MT" pack mods/blue-logo work/forced.b3mod --game "$GAME" --force
rm -f work/refused.b3mod work/forced.b3mod
# an export folder inside a mod is an error (unless --allow-exports)
mkdir -p work/exmod && cp mods/blue-logo/mod.ini work/exmod/ && cp -r work/fe work/exmod/fe
step_fails t1_verify_export 'ERROR +fe/: a txd export \(fe/txd.txt and 221 PNGs\)' "$MT" verify work/exmod
step t1_verify_export_allowed '^verify: OK .*0 errors' "$MT" verify work/exmod --allow-exports
rm -rf work/exmod
P=player/blue-logo
mkdir -p "$P"
step t1_unpack 'mod.ini' python3 -I -c 'import sys, zipfile; z = zipfile.ZipFile(sys.argv[1]); z.extractall(sys.argv[2]); print(" ".join(z.namelist()))' blue-logo-1.0.b3mod "$P"
[ -z "$(find "$P" -type f ! -name '*.b3patch' ! -name mod.ini)" ] || fail "the player's copy holds more than mod.ini and the patches"
step t1_verify_player '^verify: OK .*\(0 disc paths, 2 patches\), 0 errors' "$MT" verify "$P" --game "$GAME"
# b3modtool apply (by hand) gives the imported files back; the game's own application is checked after the run
step t1_apply '^apply: .* the patch.s target hash checked' "$MT" apply "$TXD" "$P/patches/DATA/FRONTEND.TXD.b3patch" work/apply_FRONTEND.TXD
cmp -s work/apply_FRONTEND.TXD mods/blue-logo/data/DATA/FRONTEND.TXD || fail "apply did not give back the imported FRONTEND.TXD"
rm -f work/apply_FRONTEND.TXD
# a patch that does not fit the disc: made from the blue FRONTEND.TXD (another version of the file than the disc's)
B=player/bad-logo
mkdir -p "$B/patches/DATA"
sed -e 's/^id = blue-logo/id = bad-logo/' mods/blue-logo/mod.ini > "$B/mod.ini"
step t1_diff_bad '^diff: .* patch [0-9]+ bytes' "$MT" diff mods/blue-logo/data/DATA/FRONTEND.TXD "$TXD" "$B/patches/DATA/FRONTEND.TXD.b3patch"
step_fails t1_verify_bad 'FRONTEND.TXD.b3patch: made from another version of DATA/FRONTEND.TXD' "$MT" verify "$B" --game "$GAME"
# a [textures] mod (patches3) made from the exported picture: the title logo's PCSX2 key (b3hdkeys), the picture with
# red and green swapped at twice its size as a DDS of that name
X=player/tex-logo
mkdir -p "$X/textures/UI"
printf '[mod]\nid = tex-logo\nname = Green title logo (HD texture)\nversion = 1.0\napi = 1\n[textures]\npack = textures\n' > "$X/mod.ini"
OFF=$(sed -nE 's/^203_titlescreen_logo .* offset (0x[0-9a-f]+).*/\1/p' work/fe/txd.txt)
[ -n "$OFF" ] || fail "no offset for raster 203 in work/fe/txd.txt"
step t1_hdkey '^  levels 1: [0-9a-f]+-[0-9a-f]+-[0-9a-f]+$' "$HDK" key "$TXD" "$OFF"
HDNAME=$(sed -nE 's/^  levels 1: ([0-9a-f-]+)$/\1/p' work/t1_hdkey.txt)
step t1_hddds 'red and green swapped' python3 -I "$PNGPY" hd-dds work/titlescreen_orig.png "$X/textures/UI/$HDNAME.dds" 2
step t1_verify_tex '^verify: OK .*0 errors' "$MT" verify "$X" --game "$GAME"
grep -q '1 replacement textures' work/t1_verify_tex.txt || fail "verify does not count the [textures] mod's texture"
echo "tutorial_check: tutorial 02 (change menu text) ..."
step t2_export '^strings export: 4006 strings' "$MT" strings export "$STR" work/strings.txt
grep -qx '230 = SINGLE EVENT' work/strings.txt || fail "the export has no line '230 = SINGLE EVENT'"
grep -qx '234 = SET UP YOUR OWN RACE OR CRASH EVENTS' work/strings.txt || fail "the export has no line '234 = SET UP YOUR OWN RACE OR CRASH EVENTS'"
printf '# my changes to the main menu\n230 = QUICK RACE\n234 = PICK A TRACK AND A CAR, THEN GO\n' > work/my-strings.txt
mkdir -p mods/menu-words/data/DATA
step t2_import '^strings import: 2 strings changed' "$MT" strings import "$STR" work/my-strings.txt mods/menu-words/data/DATA/GLOBALUS.BIN
cat > mods/menu-words/mod.ini <<'INI'
[mod]
id = menu-words
name = Menu words
version = 1.0
author = you
description = SINGLE EVENT on the main menu reads QUICK RACE.
api = 1
INI
step t2_verify '^verify: OK .*menu-words 1.0, 2 files \(1 disc paths, 0 patches\), 0 errors' "$MT" verify mods/menu-words --game "$GAME"
rm -rf work/fe work/global work/strings.txt work/titlescreen_orig.png   # the game's pictures and text: not needed past here
[ -n "${NO_GAME:-}" ] && { echo "tutorial_check: PASS the tool steps of tutorials 01 and 02 (NO_GAME: not run in the game)"; exit 0; }

cd - > /dev/null || exit 1
[ -x "$NP" ] || fail "no $NP"
SHOTS_ALL="$TITLE_SHOTS,$MENU_SHOTS"
run() {   # run <out dir> [MODS MODS_DIR]
  local dir=$1
  mkdir -p "$dir"
  env SHOTS="$SHOTS_ALL" ${2:+MODS=$2 MODS_DIR=$3} tools/native/race_check.sh "$NP" "$dir" > "$dir.verdict.txt" 2>&1
  tail -1 "$dir.verdict.txt"
}
why=""
verdict() { case $2 in "race_check: PASS"*) ;; *) why="$why; $1 run: $2" ;; esac; }
if [ -n "${REF:-}" ]; then R=$REF
else
  echo "tutorial_check: the run without mods ..."
  R=$OUT/rc_plain; verdict plain "$(run "$R")"
fi
echo "tutorial_check: the run with blue-logo (the player's copy: only its two patches) ..."
rm -rf "$OUT/player/.cache"
verdict blue-logo "$(run "$OUT/rc_blue-logo" blue-logo "$OUT/player")"
echo "tutorial_check: the run with menu-words ..."
verdict menu-words "$(run "$OUT/rc_menu-words" menu-words "$OUT/mods")"
echo "tutorial_check: the run with tex-logo (a [textures] mod) ..."
verdict tex-logo "$(run "$OUT/rc_tex-logo" tex-logo "$OUT/player")"
echo "tutorial_check: the run with bad-logo (a patch that does not fit the disc: must stop) ..."
BV=$(run "$OUT/rc_bad-logo" bad-logo "$OUT/player")
case $BV in *"native_play exit 2"*) ;; *) why="$why; bad-logo run: not stopped with exit 2 ($BV)" ;; esac
L1=$OUT/rc_blue-logo/native_play.log L2=$OUT/rc_menu-words/native_play.log L3=$OUT/rc_tex-logo/native_play.log L4=$OUT/rc_bad-logo/native_play.log
grep -q '^native_play: mods: none' "$R/native_play.log" || why="$why; the plain run's log has no 'mods: none'"
grep -q '^native_play: mods: active (--mods; the first wins): blue-logo 1.0 (2 files)' "$L1" || why="$why; no blue-logo set line"
grep -q '^native_play: mods: blue-logo 1.0 .*: 2 files (2 from patches)' "$L1" || why="$why; blue-logo's files are not from its patches"
grep -q "^mods: DATA/FRONTEND.TXD from blue-logo (8517568 bytes, its patch applied to your disc's file)" "$L1" || why="$why; FRONTEND.TXD not served from blue-logo's patch"
grep -q "^mods: DATA/GLOBAL.TXD from blue-logo ([0-9]* bytes, its patch applied to your disc's file)" "$L1" || why="$why; GLOBAL.TXD not served from blue-logo's patch"
grep -q '^\[b3render\] mods: blue-logo 1.0 (2 files); set hash' "$L1" || why="$why; the renderer did not build blue-logo's set"
grep -q 'WARNING: set hash' "$L1" && why="$why; the renderer's set hash differs from the game's"
# the game's results of the patches, in the cache: the imported files byte for byte
cached=0
for f in "$OUT"/player/.cache/*.bin; do
  [ -f "$f" ] || continue
  cmp -s "$f" "$OUT/mods/blue-logo/data/DATA/FRONTEND.TXD" || cmp -s "$f" "$OUT/mods/blue-logo/data/DATA/GLOBAL.TXD" || why="$why; a cache file ($f) is not one of the imported TXDs"
  cached=$((cached + 1))
done
[ "$cached" = 2 ] || why="$why; $cached cache files after the blue-logo run (expected 2)"
grep -q '^native_play: mods: active (--mods; the first wins): menu-words 1.0 (1 file)' "$L2" || why="$why; no menu-words set line"
grep -q '^mods: DATA/GLOBALUS.BIN from menu-words' "$L2" || why="$why; GLOBALUS.BIN not served from menu-words"
grep -q '^native_play: mods: tex-logo 1.0 .*: 0 files and a texture pack' "$L3" || why="$why; no tex-logo line with its texture pack"
grep -q '^hd pack .*tex-logo/textures: 1 replacements indexed' "$L3" || why="$why; the renderer did not index tex-logo's texture"
grep -q "^native_play: mods: bad-logo: patches/DATA/FRONTEND.TXD.b3patch was made from another version of DATA/FRONTEND.TXD than your disc's .*: the mod is off for this run" "$L4" ||
  why="$why; bad-logo's log has no mismatch message"
shot() { echo "$1/shots/live_${2}_0.png"; }
same() { [ -f "$(shot "$1" "$3")" ] && [ -f "$(shot "$2" "$3")" ] && cmp -s "$(shot "$1" "$3")" "$(shot "$2" "$3")"; }
report=""
# turned <what> <poll> <box> <min red> [run dir, colour field]: the box's strongly red pixels in the plain shot turn
# strongly blue (or green: field 8) in the modded run
turned() {
  local a b pr pb mr mb dir=${5:-$OUT/rc_blue-logo} f=${6:-4} name=${7:-blue}
  a=$(shot "$R" "$2") b=$(shot "$dir" "$2")
  [ -f "$a" ] && [ -f "$b" ] || { why="$why; no $1 shot $2"; return; }
  pr=$(python3 -I "$PNGPY" colours "$a" $3 | cut -d' ' -f2)
  pb=$(python3 -I "$PNGPY" colours "$a" $3 | cut -d' ' -f"$f")
  mr=$(python3 -I "$PNGPY" colours "$b" $3 | cut -d' ' -f2)
  mb=$(python3 -I "$PNGPY" colours "$b" $3 | cut -d' ' -f"$f")
  report="$report $1 $2: red $pr $name $pb -> red $mr $name $mb;"
  [ "$pr" -ge "$4" ] || why="$why; $1 shot $2 of the plain run: only $pr strongly red pixels in the logo's area (not that screen? the shot polls)"
  [ "$mb" -ge $((pr * 2 / 3)) ] && [ "$mr" -le $((pr / 3)) ] || why="$why; $1 shot $2: the logo did not turn $name (red $pr -> $mr, $name $pb -> $mb)"
}
for q in ${TITLE_SHOTS//,/ }; do
  turned title "$q" "$TITLE_BOX" 10000
  turned title-hd "$q" "$TITLE_BOX" 10000 "$OUT/rc_tex-logo" 8 green
  same "$R" "$OUT/rc_menu-words" "$q" || why="$why; title shot $q differs with menu-words (expected the same)"
done
for q in ${MENU_SHOTS//,/ }; do
  turned menu "$q" "$MENU_BOX" 3000
  same "$R" "$OUT/rc_tex-logo" "$q" || why="$why; menu shot $q differs with tex-logo (only the title logo has a replacement)"
  [ -f "$(shot "$OUT/rc_menu-words" "$q")" ] && [ -f "$(shot "$R" "$q")" ] || { why="$why; no menu shot $q"; continue; }
  same "$R" "$OUT/rc_menu-words" "$q" && why="$why; menu shot $q equal with menu-words (the text did not change)"
  a=$(python3 -I "$PNGPY" colours "$(shot "$R" "$q")" $MENU_BOX)
  b=$(python3 -I "$PNGPY" colours "$(shot "$OUT/rc_menu-words" "$q")" $MENU_BOX)
  [ "$a" = "$b" ] || why="$why; menu shot $q: the logo's area changed with menu-words ($a -> $b)"
done
for q in 10700 11500; do
  same "$R" "$OUT/rc_blue-logo" "$q" || why="$why; race shot $q differs with blue-logo"
  same "$R" "$OUT/rc_tex-logo" "$q" || why="$why; race shot $q differs with tex-logo"
done
same "$R" "$OUT/rc_menu-words" 11500 || why="$why; race shot 11500 differs with menu-words"
echo "tutorial_check:$report"
if [ -z "$why" ]; then
  echo "tutorial_check: PASS tutorials 01 and 02: every tool step as written; in the game, from the two patches alone, the title logo (shots $TITLE_SHOTS) and the menus' logo (shots $MENU_SHOTS) turned blue, the main menu's text changed (shots $MENU_SHOTS), a [textures] mod turned the title logo green, a patch that does not fit the disc stopped the run with its message, race shots equal, the logs name the mods and their files ($OUT)"
else
  echo "tutorial_check: FAIL ${why#; }"
  exit 1
fi
