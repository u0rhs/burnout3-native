#!/bin/bash
# sdk_check.sh -- the code-mod SDK's examples (docs/modding/DESIGN.md §2, tutorials 03 and 05) built from sdk/ alone and
# run in the game, as the tutorials do it, without a person:
#   1. copies examples/mods/hello-overlay and menu-label-setting to <out>/mods and builds each with `make -C sdk MOD=...`
#      (this system's library; the copies keep the build out of the repo);
#   2. hello-overlay: a race_check run (MODS / MODS_DIR) with menu shots MENU_SHOTS: it must PASS, the log must say the
#      code loaded and the overlay was drawn in every shot (main menu and race);
#   3. menu-label-setting: two race_check runs, the row Off (its default) and On from VBlank SET_AT (native_play
#      --mod-set through B3NATIVE_MOD_SET, a change while the game runs): both PASS; the On run's log has the change at
#      SET_AT and its pad recording the "set" lines; its main-menu shots differ from the Off run's (QUICK RACE), the
#      title shot before SET_AT is equal.
# The replays of such runs: tools/native/replay_check.sh with the same MODS / MODS_DIR / B3NATIVE_MOD_SET.
#
#   tools/modding/sdk_check.sh [native_play] [out dir]      B3_GAME_DIR, SAVES (as race_check.sh); one native_play at a time
# Last line "sdk_check: PASS ..." or "sdk_check: FAIL <why>".
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/sdk_check}
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
MENU_SHOTS=${MENU_SHOTS:-8850,9000}
TITLE_SHOT=${TITLE_SHOT:-4200}   # the title screen (before the change; tutorial_check.sh's)
SET_AT=${SET_AT:-16000}
why=""
fail() { why="${why:+$why; }$1"; }
rm -rf "$OUT"
mkdir -p "$OUT/mods"
for m in hello-overlay menu-label-setting; do
  cp -r "examples/mods/$m" "$OUT/mods/"
  rm -rf "$OUT/mods/$m/bin" "$OUT/mods/$m/build"
  make -s -C sdk MOD="$OUT/mods/$m" > "$OUT/build-$m.txt" 2>&1 || fail "$m did not build ($(tail -1 "$OUT/build-$m.txt"))"
  echo "sdk_check: $(grep -m1 'b3mod: built' "$OUT/build-$m.txt")"
done
run() {   # run <name> [env...]
  local name=$1; shift
  env "$@" MODS_DIR="$OUT/mods" STRICT=1 tools/native/race_check.sh "$NP" "$OUT/$name" > "$OUT/$name.txt" 2>&1
  echo "sdk_check: $name: $(tail -1 "$OUT/$name.txt" | cut -c1-120)"
  tail -1 "$OUT/$name.txt" | grep -q '^race_check: PASS' || fail "$name: race_check did not PASS"
}
shot() { sha256sum "$OUT/$1/shots/live_$2_0.png" 2>/dev/null | cut -c1-16; }
if [ -z "$why" ]; then
  run overlay MODS=hello-overlay SHOTS="$MENU_SHOTS"
  L=$OUT/overlay/native_play.log
  grep -q 'native_play: mods: hello-overlay: code loaded' "$L" || fail "overlay: no 'code loaded' line"
  for q in ${MENU_SHOTS//,/ } 10700 11500; do
    grep -q "shot poll $q: mod overlay drawn (4 of 4 items" "$L" || fail "overlay: shot $q without the overlay"
  done
  run label-off MODS=menu-label-setting SHOTS="$TITLE_SHOT,$MENU_SHOTS"
  run label-on MODS=menu-label-setting B3NATIVE_MOD_SET="$SET_AT:menu-label-setting.quick_race=On" SHOTS="$TITLE_SHOT,$MENU_SHOTS"
  grep -q "native_play: VBlank $SET_AT: mods: setting menu-label-setting.quick_race = On" "$OUT/label-on/native_play.log" || fail "label-on: no setting change at $SET_AT"
  grep -q "^set 0 menu-label-setting.quick_race Off$" "$OUT/label-on/pads.txt" && grep -q "^set $SET_AT menu-label-setting.quick_race On$" "$OUT/label-on/pads.txt" ||
    fail "label-on: the pad recording lacks the set lines"
  [ "$(shot label-off "$TITLE_SHOT")" = "$(shot label-on "$TITLE_SHOT")" ] || fail "label: title shot $TITLE_SHOT differs (before the change)"
  for q in ${MENU_SHOTS//,/ }; do
    a=$(shot label-off "$q"); b=$(shot label-on "$q")
    [ -n "$a" ] && [ "$a" != "$b" ] || fail "label: main-menu shot $q is the same with the row On"
    echo "sdk_check: main menu shot $q: Off $a, On $b"
  done
fi
if [ -n "$why" ]; then echo "sdk_check: FAIL $why"; exit 1; fi
echo "sdk_check: PASS both examples built from sdk/; hello-overlay drawn in every shot; menu-label-setting's row changed at VBlank $SET_AT (recorded), the main menu's shots differ (logs in $OUT)"
