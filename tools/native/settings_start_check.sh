#!/bin/bash
# settings_start_check.sh -- extrastart (pre4): the PC settings a run starts with are the settings file's, not the
# defaults. native_play reads some of them at its setup and its first VBlanks (the EXTRAS word, whose CUSTOM / BETA cars
# are read once; RACE REPLAY's hooks; START AT) -- before the renderer has loaded the file on its own thread. pre4 took
# the defaults there in every window run (ELIMINATOR alone: no CUSTOM / BETA cars), and the scenario checks never saw it:
# they set B3NATIVE_EXTRAS. This run sets nothing but a settings file (--settings, the same path rule as a window run's
# user pc_settings.ini: include/b3settings/settings_path.h, unit-tested in test_settings):
#   - "native_play: extras 003D0002": LAPS, ELIMINATOR, HEAVYWEIGHTS, TRAFFIC SERIES, CUSTOM CARS, BETA CARS (the ini's);
#   - a CUSTOM and a BETA car loaded (extra_cars: XCUS/CAR1, XBET/CAR1; the cars folders made here from the disc's own
#     COMP/CAR7 and HEVY/CAR4: copies, nothing changed);
#   - RACE REPLAY's hooks installed ("native_play: RACE REPLAY on");
#   - START AT the title (boot_to = 1): "START AT: the intro film ended";
#   - gameoptsfix (pre4.2): GAMEPLAY's / PARTY MODES' word the races latch, "native_play: game options 2097" (MIRROR MODE,
#     CRAZY AI PHYSICS Mayhem, KAMIKAZE TRAFFIC: the ini's; every build from gameopts to pre4.2 gave 0 here), and TRAFFIC
#     CHECK (2 PLAYERS) 2 (its other checks set $B3NATIVE_TRAFFIC_CHECK: the settings' path proven here).
#
#   tools/native/settings_start_check.sh [native_play] [out dir]      (default build/native_play, build/settings_start)
#   Environment: B3_GAME_DIR (the disc folder, default data/game), STRICT=1 (no retail fallback).
# Verdict (last line): "settings_start_check: PASS ..." or "settings_start_check: FAIL <why>"; exit 0 / 1.
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/settings_start}
GAME=${B3_GAME_DIR:-data/game}
[ -x "$NP" ] || { echo "settings_start_check: FAIL no $NP"; exit 1; }
[ -f "$GAME/SLUS_210.50" ] || { echo "settings_start_check: FAIL no disc folder ($GAME)"; exit 1; }
[ -f "$GAME/PVEH/COMP/CAR7.BGV" ] && [ -f "$GAME/PVEH/HEVY/CAR4.BGV" ] || { echo "settings_start_check: FAIL no PVEH cars in $GAME"; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT/card" "$OUT/shots" "$OUT/custom/COMP" "$OUT/beta/01-check-hevy-car4"
cp "$GAME/PVEH/COMP/CAR7.BGV" "$OUT/custom/COMP/CAR7.BGV"
cp "$GAME/PVEH/HEVY/CAR4.BGV" "$OUT/beta/01-check-hevy-car4/car.bgv"
printf 'name = Check Heavy\ndonor = HEVY/CAR4\nmodel = car.bgv\nkey = check-hevy-car4\n' > "$OUT/beta/01-check-hevy-car4/car.ini"
cat > "$OUT/pc_settings.ini" <<'EOF'
version = 4
extra_laps = true
extra_heavyweights = true
extra_traffic_series = true
extra_custom_cars = 1
extra_beta_cars = 1
replays = 1
boot_to = 1
mirror_mode = true
party_crazy_ai = 3
party_kamikaze_traffic = true
traffic_check = 2
EOF
STRICTARG=(); [ "${STRICT:-0}" = 1 ] && STRICTARG=(--strict)
env -u B3NATIVE_EXTRAS -u B3NATIVE_EXTRAS_AT -u B3NATIVE_GAME_OPTIONS -u B3NATIVE_TRAFFIC_CHECK -u B3_REPLAYS -u B3NATIVE_CUSTOM_SERIES -u B3NATIVE_BETA_SERIES \
  B3HOST_FATAL_ABORT=1 B3NATIVE_SAVES_ROOT="$OUT/card" B3NATIVE_CARD_PRESENT_VBLANK=0 B3NATIVE_MAX_VBLANKS=1500 \
  B3_CUSTOM_CARS="$OUT/custom" B3_BETA_CARS="$OUT/beta" \
  prlimit --as=10737418240 "$NP" --game "$GAME" --offscreen --no-pace "${STRICTARG[@]}" \
  --render-args "--offscreen-live $OUT/shots --settings $OUT/pc_settings.ini" > "$OUT/log.txt" 2>&1
rc=$?
L="$OUT/log.txt"
why=""
[ $rc -eq 0 ] || why="native_play exit $rc"
grep -q '^crash:' "$L" && why="${why:+$why; }it crashed"
word=$(grep -m1 -o 'native_play: extras [0-9A-F]*' "$L" | awk '{print $3}')
[ "$word" = 003D0002 ] || why="${why:+$why; }the start's EXTRAS word is ${word:-missing}, not 003D0002 (the settings file's)"
grep -q 'extra_cars: XCUS/CAR1' "$L" || why="${why:+$why; }no CUSTOM car loaded"
grep -q 'extra_cars: XBET/CAR1' "$L" || why="${why:+$why; }no BETA car loaded"
grep -q 'native_play: RACE REPLAY on' "$L" || why="${why:+$why; }RACE REPLAY not installed"
grep -q 'START AT: the intro film ended' "$L" || why="${why:+$why; }START AT (the title) not taken"
gopt=$(grep -m1 -o 'native_play: game options [0-9]*' "$L" | awk '{print $4}')
tchk=$(grep -m1 -o 'traffic check (2 players) [0-9]*' "$L" | awk '{print $5}')
[ "$tchk" = 2 ] || why="${why:+$why; }the start's TRAFFIC CHECK (2 PLAYERS) is ${tchk:-missing}, not 2 (Extreme, the ini's; its checks set B3NATIVE_TRAFFIC_CHECK)"
[ "$gopt" = 2097 ] || why="${why:+$why; }the start's game options are ${gopt:-missing}, not 2097 (MIRROR MODE + CRAZY AI Mayhem + KAMIKAZE TRAFFIC, the ini's)"
if [ -n "$why" ]; then echo "settings_start_check: FAIL $why (log: $L)"; exit 1; fi
echo "settings_start_check: PASS the start took the settings file's: extras $word, game options $gopt, traffic check $tchk, a CUSTOM and a BETA car, RACE REPLAY, START AT"
