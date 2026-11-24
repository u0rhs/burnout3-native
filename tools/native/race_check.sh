#!/bin/bash
# race_check.sh -- one native_play run through a race start and ~30 s of driving (rt1; part of every head's
# checks). Offscreen, unpaced, scripted keys; the Single Event path: title -> Load profile (the saves-100 card, a
# scratch copy) -> main menu -> Single Event -> Race -> Select options -> Select Car -> the race (accelerate held).
#
#   tools/native/race_check.sh [native_play] [out dir]
#     native_play   default build/native_play (libb3render.so beside it)
#     out dir       default build/race_check (log, shots, the scratch card)
#   Environment: B3_GAME_DIR (the disc folder or the disc image, default data/game; discread: most players' game data is
#   an image (.iso / .bin / .chd) and a file read that only works on a folder passes every folder run, so run `race` and
#   `trafficseries` with B3_GAME_DIR the image too), SAVES (the card to copy, default the perf
#   saves-100), EXPLORE_RETAIL (functions run as retail, empty by default; needs a NATIVE_FALLBACK=1 native_play, as
#   MOVIES=standin does), SHOTS / RENDER_EXTRA (more screenshots /
#   renderer options), MOVIES=standin (the exploration movie
#   stand-ins; default: native movies), VBLANKS (the end, default 23400).
#
# Verdict (last line): "race_check: PASS ..." when native_play exits 0 at the VBlank limit, one track was loaded, and
# the race shots (polls 10700 and 11500) show the race with a route and vehicles; else "race_check: FAIL <why>" (a
# crash prints native_play's crash report -- the native chain and backtrace -- in the log). Exit 0 / 1.
# Timing [C rt1 runs, boot2f + gen1d, 2026-11-07]: the race's countdown begins near VBlank 20300 (poll ~10150), the
# driving at ~20500; the end at 23400 is ~48 s of race, ~45 s of driving.
set -u
cd "$(dirname "$0")/../.." || exit 1
NP=${1:-build/native_play}
OUT=${2:-build/race_check}
GAME=${B3_GAME_DIR:-data/game}
. tools/local_env.sh; b3_local_env .   # the machine's paths (local.env: SAVES, B3_GAME_DIR)
SAVES=${SAVES:-}
# blinkers1 / rrparts1 (tools/native/turn_signals.h, road_rage_parts.h): two scenarios on top of others.
# SCENARIO=blinkers: the race with TURN SIGNALS on (BLINK_INI, default `turn_signals = 1`; the renderer's --settings) and
# B3_TURN_SIGNALS_TEST_PLAYER=1 (player 1's car takes them too, to see up close; B3_TURN_SIGNALS_LOG=1: its sides by
# VBlank). Shots 11493 / 11535 (US/C2_V2's right-hand bend: the right indicators), 11830 / 11871 (the left-hand one).
# The log must show a racer's indicator (none with turn_signals = 0).
# SCENARIO=rrparts: the roadrage scenario (to VBlank 27000) with ROAD RAGE PARTS on (RR_INI, default
# `road_rage_parts = 1`). The log must show the Road Rage taken with its records kept apart (the records word's bit 25,
# "-loose-parts"; none of it with road_rage_parts = 0).
XROWS=""
if [ "${SCENARIO:-race}" = blinkers ]; then XROWS=blinkers; SCENARIO=race; export B3_TURN_SIGNALS_TEST_PLAYER=1 B3_TURN_SIGNALS_LOG=1; fi
if [ "${SCENARIO:-race}" = rrparts ]; then XROWS=rrparts; SCENARIO=roadrage; VBLANKS=${VBLANKS:-27000}; fi
# SCENARIO=trafficcheck (checkfix, tools/native/traffic_check.h): raceopts' path with TRAFFIC Extreme (Down x3, Right x3),
# TRAFFIC CHECK TC_MODE (default 2 Extreme; 1 Revenge) through $B3NATIVE_TRAFFIC_CHECK, the party modes' AI DRIVER pressed
# at 20600 with the test-only B3_TRAFFIC_CHECK_TEST_AI=1 (the AI drives player 1's car through the traffic, and its hits
# check), to VBlank 27000 (TRAFFIC Extreme's memory growth reaches the 10 GB cap below near 27650), the trace on. The log must show checks (both modes); Extreme: no crash of the player's on traffic or a wreck
# (the run's summary: "player crashes on traffic 0, on wrecks 0"; walls still crash it)
if [ "${SCENARIO:-race}" = trafficcheck ]; then
  XROWS=trafficcheck; SCENARIO=raceopts; VBLANKS=${VBLANKS:-27000}
  RACEOPTS_KEYS=${RACEOPTS_KEYS:-19500:81 19550:81 19600:81 19650:79 19700:79 19750:79}; RACEOPTS_WANT=${RACEOPTS_WANT:-t5 l0 r0 d0}
  EXTRA_KEYS="20600:35:1,20610:35:0${EXTRA_KEYS:+,$EXTRA_KEYS}"
  export B3NATIVE_GAME_OPTIONS=${B3NATIVE_GAME_OPTIONS:-1920} B3_TRAFFIC_CHECK_TEST_AI=1 B3_TRAFFIC_CHECK_TRACE=1
  export B3NATIVE_TRAFFIC_CHECK=${TC_MODE:-2}
fi
# SCENARIO=profilename (mac5): a fresh card (an empty folder under OUT), see below
if [ "${SCENARIO:-race}" = profilename ]; then SAVES=$OUT/emptycard; rm -rf "$SAVES"; mkdir -p "$SAVES"; fi
VBL=${VBLANKS:-23400}
mkdir -p "$OUT/shots" "$OUT/card"
rm -f "$OUT"/shots/live_*.png   # vu0n: this run's shots only (their hashes are printed)
[ -x "$NP" ] || { echo "race_check: FAIL no $NP"; exit 1; }
# iso1: GAME may be the disc image itself (.iso / .bin / .chd, read in place: include/b3asset/discfs.h)
[ -f "$GAME/SLUS_210.50" ] || [ -f "$GAME" ] || { echo "race_check: FAIL no disc folder or image ($GAME)"; exit 1; }
[ -n "$SAVES" ] && [ -d "$SAVES" ] || { echo "race_check: FAIL no card folder (SAVES=$SAVES: a memory card folder with a profile, e.g. the 100 % save; set it in local.env)"; exit 1; }
rsync -a --delete "$SAVES/" "$OUT/card/"   # a scratch copy: the game writes to it
# Start at 9000 (title), Cross at 15500 and 16300 (LOAD PROFILE, PROFILE 1; save1: its "Load successful" is answered
# by tools/native/card_silence.h, so no CONTINUE), Down + Cross (Single Event),
# Cross every 800 (Race, Select options, Select Car), then Cross held from 20400 (accelerate). Keys (mac4's layout,
# docs/mac.md §5): Escape (41) = Start, Z (29) = Cross everywhere, Down (81) = D-pad down in menus.
K="9000:41:1,9010:41:0"
for v in $(seq 15500 800 16300); do K="$K,$v:29:1,$((v+10)):29:0"; done
K="$K,17900:81:1,17910:81:0,18300:29:1,18310:29:0"
for v in $(seq 19100 800 19900); do K="$K,$v:29:1,$((v+10)):29:0"; done
K="$K,20400:29:1"
# TITLE_START_AT=<VBlank> (titlefast, tools/native/title_fast.h): the title's Start there instead of at 9000 (the title
# takes input at VBlank 4680; the game's own wait would ignore Start until 4746). Its race then starts that much sooner:
# VBLANKS short of the race only (the verdict then fails on the race shots: read the log's title lines)
if [ -n "${TITLE_START_AT:-}" ]; then K="$TITLE_START_AT:41:1,$((TITLE_START_AT+10)):41:0${K#9000:41:1,9010:41:0}"; fi
# SKIP_MOVIE_AT=<VBlank> (mac4): Space (Cross in menus) there, during the front-end movie (Titles30 plays from ~2100 to
# 5522): the port's movie skip (tools/native/movie_skip.h) must end it and the rest of the script still reach the race
# (the title then comes ~100 VBlanks later, and its attract replay of Titles30 ~5500 after that: so the title's Start
# moves to SKIP_MOVIE_AT + 400)
if [ -n "${SKIP_MOVIE_AT:-}" ]; then
  K="$SKIP_MOVIE_AT:44:1,$((SKIP_MOVIE_AT+10)):44:0,$((SKIP_MOVIE_AT+400)):41:1,$((SKIP_MOVIE_AT+410)):41:0${K#9000:41:1,9010:41:0}"
fi
# SCENARIO=takedown (mac4): also boost (Left Shift = R1) held from 20500 and a weave left / right every 4 s, to ram
# rivals into the scenery for a takedown (the Mac crash of 2026-11-08 came at the first takedown); VBLANKS default 27000
if [ "${SCENARIO:-race}" = takedown ]; then
  VBL=${VBLANKS:-27000}
  K="$K,20500:225:1"
  for v in $(seq 20700 240 $((VBL - 200))); do K="$K,$v:80:1,$((v+50)):80:0,$((v+120)):79:1,$((v+170)):79:0"; done
fi
if [ "$XROWS" = blinkers ]; then
  SHOTS=${SHOTS:-11493,11535,11830,11871}
  printf '%s\n' "${BLINK_INI:-turn_signals = 1}" > "$OUT/pc_settings.ini"
  RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
  PACE_FROM=${PACE_FROM:-21300}
fi
if [ "$XROWS" = rrparts ]; then
  printf '%s\n' "${RR_INI:-road_rage_parts = 1}" > "$OUT/pc_settings.ini"
  RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
fi
# SCENARIO=crashbreaker (gameopts, tools/native/game_options.h): the takedown scenario's driving with CRASHBREAKER IN EVERY
# MODE on ($B3NATIVE_GAME_OPTIONS bit 1) and E (the keyboard's Crashbreaker, R2) tapped every 20 VBlanks: a crash of the player's
# car is armed and fired in a RACE (the verdict: the log's "Crashbreaker fired" and its races kept apart).
# SCENARIO=mirror: the race with MIRROR MODE on (bit 0) and the takedown's weave (the steering negated;
# the verdict: the race latched MIRROR MODE, records kept apart)
# (MIRROR_INI=1: the row through the settings file instead of $B3NATIVE_GAME_OPTIONS, below)
if [ "${SCENARIO:-race}" = crashbreaker ] || [ "${SCENARIO:-race}" = mirror ]; then
  VBL=${VBLANKS:-27000}
  # (the scenario's option added to any asked for: the golden's baseline.sh exports 0)
  # (CB_PLAIN=1: the crashbreaker drive without the option, to compare with)
  if [ "$SCENARIO" = crashbreaker ] && [ "${CB_PLAIN:-0}" = 1 ]; then export B3NATIVE_GAME_OPTIONS=0
  # (CB_CARS=0..3: CRASHBREAKER AFTER, the vehicles the crash must hit first; default 0, ready at once)
  elif [ "$SCENARIO" = crashbreaker ]; then export B3NATIVE_GAME_OPTIONS=$(( ${B3NATIVE_GAME_OPTIONS:-0} | 2 | ${CB_CARS:-0} << 2 )); else export B3NATIVE_GAME_OPTIONS=$(( ${B3NATIVE_GAME_OPTIONS:-0} | 1 )); fi
  # gameoptsfix (pre4.2): MIRROR_INI=1 takes MIRROR MODE (and MIRROR_INI_ROWS, e.g. "party_kamikaze_traffic = true") from
  # the renderer's --settings, as a player's run does, with no $B3NATIVE_GAME_OPTIONS: the path every build from gameopts
  # to pre4.2 left at 0 (native_play's g_game_options copied before its lookup) while the env path passed every check
  if [ "$SCENARIO" = mirror ] && [ "${MIRROR_INI:-0}" = 1 ]; then
    unset B3NATIVE_GAME_OPTIONS
    printf 'mirror_mode = true\n%s\n' "${MIRROR_INI_ROWS:-}" > "$OUT/pc_settings.ini"
    RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
  fi
  E=""
  # (CB_NOBOOST=1: no boost held: boost held in a crash is Impact Time's slow motion, so the crash never ends)
  # (CB_BOOST_UNTIL=<VBlank>: boost released there, so a crash under way can end)
  [ "${CB_NOBOOST:-0}" = 1 ] || K="$K,20500:225:1"
  [ -n "${CB_BOOST_UNTIL:-}" ] && E="$E ${CB_BOOST_UNTIL}:225:0"
  for v in $(seq 20700 240 $((VBL - 200))); do E="$E $v:80:1 $((v+50)):80:0 $((v+120)):79:1 $((v+170)):79:0"; done
  if [ "$SCENARIO" = crashbreaker ]; then for v in $(seq 20705 20 $((VBL - 100))); do E="$E $v:8:1 $((v+6)):8:0"; done; fi
  # CB_MODE=timeattack|roadrage: that mode instead of RACE (SELECT MODE's Downs, as those scenarios do)
  if [ "$SCENARIO" = crashbreaker ] && [ -n "${CB_MODE:-}" ]; then
    n=1; [ "$CB_MODE" = roadrage ] && n=2
    D=""; for k in $(seq 1 $n); do v=$((18700 + 100 * k)); D="$D,$v:81:1,$((v+10)):81:0"; done
    K="${K/,19100:29:1/$D,19100:29:1}"
  fi
  K="$K,$(echo $E | tr ' ' '\n' | sort -t: -k1,1n | paste -sd, -)"   # (the keys in VBlank order)
fi
# SCENARIO=party (party1, tools/native/party_modes.h): the race with GAME > PARTY MODES' buttons on ($B3NATIVE_GAME_OPTIONS,
# default 1920: MOON JUMP High, CRASH ON DEMAND, AI DRIVER BUTTON; PARTY_OPTS adds others, e.g. 2048 KAMIKAZE TRAFFIC): MOON JUMP
# (4) held 21100-21160, CRASH (5) at 21500, AI DRIVER (6) at 22200 and 22800. The verdict: each acted (the log's lines).
# The finished car (the AI keeps it): B3_RACEOPTS_TEST_FINISH=400 PARTY_AI_AT="21000 22400" VBLANKS=24000 (the laps done
# by 22268; the second press refused)
if [ "${SCENARIO:-race}" = party ]; then
  VBL=${VBLANKS:-23400}
  export B3NATIVE_GAME_OPTIONS=${B3NATIVE_GAME_OPTIONS:-$(( 1920 | ${PARTY_OPTS:-0} ))}
  # (PARTY_AI_AT="<VBlank> <VBlank> ...": the AI DRIVER presses there instead; with B3_RACEOPTS_TEST_FINISH a press after the
  # finish must be refused: the verdict then wants the log's "has finished: it stays the AI's")
  AI=""; for v in ${PARTY_AI_AT:-22200 22800}; do AI="$AI,$v:35:1,$((v+10)):35:0"; done
  K="$K,21100:33:1,21160:33:0,21500:34:1,21510:34:0$AI"
fi
# SCENARIO=steer (gameopts' proof that MIRROR MODE steers by the screen): the race with one steering input held from 21000 to
# 21300, STEER=left|right, STEER_DEV=key (the arrow keys: the D-pad and the stick together, as the keyboard steers) | dpad |
# stick (a scripted Xbox pad, B3NATIVE_TEST_PAD as padlayout's: the D-pad alone, or the left stick alone: codes 38 / 39),
# and the RAM
# hashed every 200 VBlanks from 21000 (B3NATIVE_RAM_HASH). Mirrored + right must leave the game exactly as plain + left
# (the same hashes), and plain left / right must differ.
if [ "${SCENARIO:-race}" = steer ]; then
  VBL=${VBLANKS:-22400}
  export B3NATIVE_RAM_HASH=${B3NATIVE_RAM_HASH:-21000-22400:200}
  if [ "${STEER_DEV:-key}" = key ]; then
    c=80; [ "${STEER:-left}" = right ] && c=79
    K="$K,21000:$c:1,21300:$c:0"
  else
    export B3NATIVE_TEST_PAD=xbox
    P=1024; S=$P; ST=$((P+6)); DD=$((P+12)); RT=$((P+33))
    if [ "$STEER_DEV" = dpad ]; then c=$((P+13)); [ "${STEER:-left}" = right ] && c=$((P+14)); else c=$((P+38)); [ "${STEER:-left}" = right ] && c=$((P+39)); fi
    K="9000:$ST:1,9010:$ST:0"
    for v in $(seq 15500 800 16300); do K="$K,$v:$S:1,$((v+10)):$S:0"; done
    K="$K,17900:$DD:1,17910:$DD:0,18300:$S:1,18310:$S:0"
    for v in $(seq 19100 800 19900); do K="$K,$v:$S:1,$((v+10)):$S:0"; done
    K="$K,20400:$S:1,20410:$S:0,20410:$RT:1,21000:$c:1,21300:$c:0"
  fi
fi
# SCENARIO=orbitcam (orbitcam1, src/b3replay/orbitcam.h): the race with a scripted Xbox pad (B3NATIVE_TEST_PAD=$PAD,
# default xbox; none of its buttons used, the keys drive) and the mouse, once the race is driven (VBlank ~21071; in the
# race a poll a VBlank, poll = VBlank - 10108): the game's view (shot 10990); a right-button mouse drag to the right
# 21105-21125 (11020: looking right; back by ~21200); the stick left 21220-21250 (11145: looking left), left again
# 21270-21300 (11195, 11230: behind, held), then at rest (11252: easing back, 11285: the game's view again); the stick
# down 21440-21460 (11355: the eye up, looking down); back by the race shot 11500. That is ORBIT STYLE Swing
# (ORBIT_INI='orbit_style = 1'); with Look (orbitcam2, the default) the stick's direction is where the view looks: left
# 21220-21250, down 21270-21300 (behind), right 21440-21460, each eased back after the stick is let go. Paced from VBlank 21000 (PACE_FROM;
# the run takes ~40 s more: unpaced, the shots this close together hold the renderer back past the feed it keeps). ORBIT_INI: rows for the renderer's --settings (e.g. orbit_camera = 0: no orbit). The log must
# show the view taken by the stick and by the mouse, eased back after each (none of it with orbit_camera = 0; PAD=ps:
# the mouse's only)
if [ "${SCENARIO:-race}" = orbitcam ]; then
  export B3NATIVE_TEST_PAD=${PAD:-xbox}
  if printf '%s' "${ORBIT_INI:-}" | grep -q 'orbit_style *= *1'; then   # SWING (orbitcam1's): left, left again (behind), down
    K="$K,21220:1059:1,21250:1059:0,21270:1059:1,21300:1059:0,21440:1061:1,21460:1061:0"
  else   # LOOK (orbitcam2, the default): left, down (behind), right
    K="$K,21220:1059:1,21250:1059:0,21270:1061:1,21300:1061:0,21440:1060:1,21460:1060:0"
  fi
  MOUSE=${MOUSE:-21100:move:0.5:0.5,21105:down:3,21110:move:0.55:0.5,21115:move:0.6:0.5,21120:move:0.65:0.5,21125:up:3}
  SHOTS=${SHOTS:-10990,11020,11145,11195,11230,11252,11285,11355}
  PACE_FROM=${PACE_FROM:-21000}
  if [ -n "${ORBIT_INI:-}" ]; then
    printf '%s\n' "$ORBIT_INI" > "$OUT/pc_settings.ini"
    RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
  fi
fi
# SCENARIO=replaycams (replaycams, src/b3replay/replaycams.h; RACE REPLAY: tools/native/replay_view.h): the race with
# RACE REPLAY on ($B3_REPLAYS=1), boost and a weave as the takedown scenario's, the laps completed by the test
# (B3_RACEOPTS_TEST_FINISH=600: the race over by VBlank ~22870), then REPLAY (F6, 63) on the results at 23600: the
# replay plays from ~24000 (poll 13618) to ~25800. In it the viewer's keys: ] (48) five times (GAME -> CHASE, FAR
# CHASE, BUMPER, ORBIT, TV), . (55: the next car, a rival: CHASE), ] again (TV), I (12) twice (the free camera on and
# off). Shots: each angle, the rival's two, the free camera, and the results after it (the cameras off). REPLAYCAMS_INI:
# rows for the renderer's --settings (e.g. replay_camera_stay = 1). The log must show each angle, the rival, the free
# camera on and off, and the cameras off once the replay is over
if [ "${SCENARIO:-race}" = replaycams ]; then
  export B3_REPLAYS=1 B3_RACEOPTS_TEST_FINISH=${B3_RACEOPTS_TEST_FINISH:-600}
  VBL=${VBLANKS:-26600}
  K="$K,20500:225:1"
  for v in $(seq 20700 240 22380); do K="$K,$v:80:1,$((v+50)):80:0,$((v+120)):79:1,$((v+170)):79:0"; done
  K="$K,22700:225:0,22800:29:0,23600:63:1,23610:63:0"
  for v in 24300 24500 24700 24850 25000; do K="$K,$v:48:1,$((v+10)):48:0"; done
  K="$K,25150:55:1,25160:55:0,25300:48:1,25310:48:0,25450:12:1,25460:12:0,25600:12:1,25610:12:0"
  # a scripted Xbox pad: D-pad up in the replay (25700: the viewer's, the game gets nothing), D-pad down on the results
  # page just after the replay leaves (~25980; 26050: the game's)
  export B3NATIVE_TEST_PAD=${B3NATIVE_TEST_PAD:-xbox}
  K="$K,25700:1035:1,25720:1035:0,26050:1036:1,26070:1036:0"
  SHOTS=${SHOTS:-14100,14300,14500,14700,14850,15000,15150,15300,15450,16000}
  if [ -n "${REPLAYCAMS_INI:-}" ]; then
    printf '%s\n' "$REPLAYCAMS_INI" > "$OUT/pc_settings.ini"
    RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
  fi
fi
# SCENARIO=camroll (camroll1, src/b3replay/orbitcam.h ChaseRoll): the race as it is (accelerator held: US/C2_V2 from the
# saves-100 card), with CAMERA ROLL from ROLL_INI (default `camera_roll = 2`, Strong; `camera_roll = 0`: off) through the
# renderer's --settings. Shots: 11300 (a straight: level), 11517, 11667 (the right-hand bend: about +5 and +7 degrees),
# 11917 (the left-hand one: about -5). The log must show the chase camera lean into a corner (none of it with
# camera_roll = 0); B3_ORBITCAM_LOG=1 logs the roll every tick.
if [ "${SCENARIO:-race}" = camroll ]; then
  SHOTS=${SHOTS:-11300,11517,11667,11917}
  printf '%s\n' "${ROLL_INI:-camera_roll = 2}" > "$OUT/pc_settings.ini"
  RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
fi
# SCENARIO=worldtour (mac5): World Tour (the main menu's first item) instead of Single Event, then Cross every 400 to
# 21600 (the globe, the event, its intro) and accelerate (the Mac crash of 2026-11-08 entering a World Tour race)
if [ "${SCENARIO:-race}" = worldtour ]; then
  K="9000:41:1,9010:41:0"
  for v in $(seq 15500 800 16300); do K="$K,$v:29:1,$((v+10)):29:0"; done
  for v in $(seq 17900 400 21600); do K="$K,$v:29:1,$((v+10)):29:0"; done
  K="$K,22000:29:1"
fi
# SCENARIO=timeattack / roadrage / eliminator / crash (mac5; elim1): SINGLE EVENT > SELECT MODE (RACE, TIME ATTACK, ROAD RAGE,
# ELIMINATOR, CRASH) with one / two / three / four Downs before the mode's Cross at 19100 (four: 80 VBlanks apart from
# 18760), then the same Crosses and the accelerator (the other modes under STRICT=1)
# SCENARIO=crashtraffic (crashramfix): the crash scenario's Crash junction (AS/C2_V2), steered left (VBlank 22060-22140)
# into its traffic instead of the wall: an articulated lorry (cab 8000 kg, trailer 10000 kg), a 5000 kg lorry and cars
# wreck, the lorry and a car among those the junction sends to ram the player. Passes with crash_ram_fix's log of a
# vehicle without a trailer that rammed (its roll kick left armed as retail: tools/native/crash_ram_fix.h); with
# B3NATIVE_CRASH_RAM_FIX=0 the scenario runs without that check. To VBlank 23400 (the junction's first attempt).
if [ "${SCENARIO:-race}" = roadrage ] || [ "${SCENARIO:-race}" = crash ] || [ "${SCENARIO:-race}" = crashtraffic ] || [ "${SCENARIO:-race}" = timeattack ] || [ "${SCENARIO:-race}" = eliminator ]; then
  n=2; [ "$SCENARIO" = crash ] && n=4; [ "$SCENARIO" = crashtraffic ] && n=4; [ "$SCENARIO" = timeattack ] && n=1; [ "$SCENARIO" = eliminator ] && n=3
  D=""; for k in $(seq 1 $n); do v=$((18700 + 100 * k)); [ "$n" -gt 3 ] && v=$((18680 + 80 * k)); D="$D,$v:81:1,$((v+10)):81:0"; done
  K="${K/,19100:29:1/$D,19100:29:1}"
  # crash mode: its intro takes a fresh Cross (Continue) while the accelerator is held: release / press again a few
  # times, and run longer (the junction, the crash, the score)
  # elim1: the Eliminator held at full throttle: the player hits the scenery and is out at the end of lap 1 (~VBlank
  # 27000, when the last rival starts lap 2), then ELIMINATOR RESULTS; Cross released and Continue at 28000
  # ELIM_RIVALS0=1: on SELECT OPTIONS two Downs to RIVALS (TRACK is the second row) and Right (5 wraps to 0: raised to 1, one lap). The results' two
  # Continues (28000, 28700) lead to the autosave (the card's profile written: the scenario's profile check)
  if [ "$SCENARIO" = eliminator ]; then
    VBL=${VBLANKS:-30000}
    [ "${ELIM_RIVALS0:-0}" = 1 ] && K="${K/,19900:29:1/,19550:81:1,19560:81:0,19650:81:1,19660:81:0,19780:79:1,19790:79:0,19900:29:1}"
    K="$K,27800:29:0,28000:29:1,28010:29:0,28700:29:1,28710:29:0"
    # ELIM_EXIT=1: then Back to the main menu (29400) and SINGLE EVENT again (29900): SELECT MODE opens on RACE
    [ "${ELIM_EXIT:-0}" = 1 ] && { VBL=${VBLANKS:-31000}; K="$K,29400:42:1,29410:42:0,29900:29:1,29910:29:0"; }
  fi
  if [ "$SCENARIO" = crash ]; then
    VBL=${VBLANKS:-27000}
    for v in 21000 21600 22200; do K="$K,$v:29:0,$((v+10)):29:1"; done
  fi
  if [ "$SCENARIO" = crashtraffic ]; then
    VBL=${VBLANKS:-23400}
    for v in 21000 21600; do K="$K,$v:29:0,$((v+10)):29:1"; done
    K="$K,22060:80:1,22140:80:0"
    SHOTS=${SHOTS:-12100,12450,12600,12700}   # (the pile-up; 10700 and 11500 are every scenario's)
  fi
fi
# SCENARIO=raceopts (raceopts, tools/native/race_options.h): SELECT OPTIONS' own rows set before Continue. RACEOPTS_MODE
# (0 RACE, 1 TIME ATTACK, 2 ROAD RAGE, 3 ELIMINATOR (elim1: no LAPS row); the Downs before the mode's Cross) and RACEOPTS_KEYS ("<VBlank>:<scancode> ..."
# between 19400 and 19880, the page's; default RACE: Down x3 to TRAFFIC, Left x2 = Off, Down, Left x2 = LAPS 1).
# (gearbox: RACE's TRANSMISSION is the sixth row: RACEOPTS_KEYS="19450:81 19500:81 19550:81 19600:81 19650:81 19750:79",
# RACEOPTS_WANT="t2 l0 r0 d0 m1"; SHIFT UP is U, scancode 24, e.g. EXTRA_KEYS="21000:24:1,21010:24:0")
# RACEOPTS_WANT: the options the race must take (default "t0 l1 r0 d0"); the verdict also needs the stage's lap count
# given, the profile and records left alone, and (Traffic Off) the race shots with the six race cars only.
if [ "${SCENARIO:-race}" = raceopts ]; then
  n=${RACEOPTS_MODE:-0}
  D=""; for k in $(seq 1 $n); do [ "$n" = 0 ] && break; v=$((18700 + 100 * k)); D="$D,$v:81:1,$((v+10)):81:0"; done
  P=""; for t in ${RACEOPTS_KEYS:-19500:81 19550:81 19600:81 19650:80 19700:80 19750:81 19800:80 19850:80}; do
    v=${t%%:*}; c=${t#*:}; P="$P,$v:$c:1,$((v+10)):$c:0"; done
  K="${K/,19100:29:1/$D,19100:29:1}"
  K="${K/,19900:29:1/$P,19900:29:1}"
fi
# SCENARIO=morerivals (MORE RIVALS, tools/native/more_rivals.h): on SELECT OPTIONS two Downs to the game's own RIVALS row,
# then Right (MR_RIVALS - 5) times (5 -> 6 -> 7; default 7) before Continue: the race with MR_RIVALS + 1 cars, its
# records apart (options-rivals-N), the start grid checked (the TEST grid check: every car on the road and apart);
# MR_TRAFFIC_OFF=1 below: the renderer's race shots hold all of the race cars
if [ "${SCENARIO:-race}" = morerivals ]; then
  P=",19500:81:1,19510:81:0,19560:81:1,19570:81:0"
  for k in $(seq 1 $(( ${MR_RIVALS:-7} - 5 ))); do v=$((19620 + 60 * k)); P="$P,$v:79:1,$((v+10)):79:0"; done
  # MR_TRAFFIC_OFF=1: then Down to TRAFFIC and Left x2 (Normal -> Light -> Off): the race shots hold the race cars only,
  # so the renderer's vehicle count is the cars it was given (all of them: the capture's cars 7 and 8)
  [ "${MR_TRAFFIC_OFF:-0}" = 1 ] && P="$P,19780:81:1,19790:81:0,19820:80:1,19830:80:0,19860:80:1,19870:80:0"
  K="${K/,19900:29:1/$P,19900:29:1}"
  export B3NATIVE_MORE_RIVALS_TEST_GRID=1
fi
# SCENARIO=morerivalselim (MORE RIVALS with elim1): RACE's SELECT OPTIONS with RIVALS 7 (Down x2, Right x2), Back to SELECT
# MODE, ELIMINATOR (Down x3), its SELECT OPTIONS' Continue, SELECT CAR, the race: an Eliminator keeps the game's frame
# (no AI car past the game's 5: more_rivals.h takes a stale RIVALS above 5 as 5)
if [ "${SCENARIO:-race}" = morerivalselim ]; then
  VBL=${VBLANKS:-27000}
  K="${K%%,19100:29:1*},19100:29:1,19110:29:0,19500:81:1,19510:81:0,19560:81:1,19570:81:0,19680:79:1,19690:79:0,19740:79:1,19750:79:0"
  K="$K,19950:42:1,19960:42:0,20400:81:1,20410:81:0,20500:81:1,20510:81:0,20600:81:1,20610:81:0,20800:29:1,20810:29:0"
  K="$K,21600:29:1,21610:29:0,22400:29:1,22410:29:0,22900:29:1"
  SHOTS=${SHOTS:-12000,12400}
fi
# SCENARIO=finishcam (finishcam1, src/b3replay/finishcam.h): a race of one lap to its finish line: SELECT OPTIONS as
# raceopts' default (TRAFFIC Off, LAPS 1), the accelerator held; US/C2_V2 from the saves-100 card finishes near VBlank
# 28030 (poll 17923: the camera manager enters the finish state), the set pieces from there; to VBlank 31000. FINISH
# CAMERA from FINISH_INI (default `finish_camera = 1`; `finish_camera = 0`: the game's shot) through the renderer's
# --settings. Shots through set pieces 1-9: 17960 18100 18250 18360 18460 18560 18700 18850 19000 19150 19450 19750
# 20050 20350. The log must show the set pieces start at the finish (none of it with finish_camera = 0).
if [ "${SCENARIO:-race}" = finishcam ]; then
  P=""; for t in 19500:81 19550:81 19600:81 19650:80 19700:80 19750:81 19800:80 19850:80; do
    v=${t%%:*}; c=${t#*:}; P="$P,$v:$c:1,$((v+10)):$c:0"; done
  K="${K/,19900:29:1/$P,19900:29:1}"
  VBL=${VBLANKS:-31000}
  SHOTS=${SHOTS:-17960,18100,18250,18360,18460,18560,18700,18850,19000,19150,19450,19750,20050,20350}
  printf '%s\n' "${FINISH_INI:-finish_camera = 1}" > "$OUT/pc_settings.ini"
  RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
fi
# SCENARIO=pause (mac5): Start (Escape) at 21600 opens the pause menu, Start again at 22200 resumes (the Mac report of
# 2026-11-08: a FOV blip and the options sliding in on open, a clunky resume, the music looping after it); shots with
# SHOTS around 10800 / 11100
if [ "${SCENARIO:-race}" = pause ]; then
  K="$K,21600:41:1,21610:41:0,22200:41:1,22210:41:0"
fi
# SCENARIO=pausesettings (mac5, 2026-11-09: settings in a race): the pause menu at 21600, Cross released, Down x4
# to its SETTINGS row (RESUME RESTART AUDIO CONTROLLERS SETTINGS QUIT), Cross: the log must show the page opened
if [ "${SCENARIO:-race}" = pausesettings ]; then
  VBL=${VBLANKS:-22800}
  K="$K,21600:41:1,21610:41:0,21900:29:0"
  for v in 22000 22100 22200 22300; do K="$K,$v:81:1,$((v+10)):81:0"; done
  K="$K,22400:29:1,22410:29:0"
fi
# fe1 (2026-11-10, goal: every front-end path under STRICT): the front-end tours. Each starts at the title as the race
# scenario does (Start at 9000, LOAD PROFILE / PROFILE 1 at 15500 / 16300: the main menu from ~17900) and drives one part
# of the menu tree with a fixed key timeline, a shot on each page (FE_SHOTS, polls; front-end polls run at ~VBlank / 2):
#   multiplayer   MULTIPLAYER (with a second controller, native_play --keys2): the setup pages of ROAD RAGE, TEAM CRUSH,
#                 DOUBLE IMPACT and PARTY CRASH, CHANGE NAMES, then RACE: options, G1 / G2 SELECT CAR (each player's
#                 Cross), the split-screen race start (both accelerating)
#   partycrash    MULTIPLAYER > PARTY CRASH (one controller, passed round): setup, car, the junction, a crash
#   online        ONLINE: the no-adapter message box (string 105; native: no PS2 network adapter, platform_sif.cpp
#                 sceSifLoadModule) with the port's "Online play is coming soon." (online_message.h), OK; then the main
#                 menu's SETTINGS row: the PC SETTINGS page opened and closed
#   ddrewards     DRIVER DETAILS > PROGRESS (4 views), REWARDS (TROPHIES with an award presentation, YOUR GARAGE's car
#                 pages, SIGNATURE TAKEDOWNS, SPECIAL EVENTS, CRASH HEADLINES)
#   ddrecords     DRIVER DETAILS > RECORDS (RACING / CRASH / TEAM CRUSH tables), PROFILE (LOAD PROFILE from the card,
#                 SAVE PROFILE to it)
#   ddsettings    DRIVER DETAILS > SETTINGS (GAMEPLAY, AUDIO, VIDEO, CONTROLLERS, EA GAMES TRAX), TRAINING (a video),
#                 EXTRAS (a series video, the credits)
#   eatrax        eatrax1 (remaster-features §6.3): DRIVER DETAILS > SETTINGS > EA GAMES TRAX with a music folder of
#                 synthetic tones made here ($OUT/music: 4 tagged by file name, a long one and a non-Latin-1 one; or
#                 EATRAX_FILL=<n> more short ones, the cap of 200), PLAYLIST Mixed: down to the first custom row (45),
#                 its mode to RACE, down three, OFF, confirm; the page again, down to row 45, a change and Back. The log
#                 must show the list (44 + n), a custom row's preview played, the modes saved ($OUT/music_modes.ini:
#                 races / off lines) and Back's restore
#   crashnav      WORLD TOUR's Crash Nav: the regions, a location, its event list, an ELIMINATOR event driven to its
#                 end (crashes, eliminated), the results, the BURNOUT POINTS page
#   carselect     SINGLE EVENT > RACE: the car select's series / car pages and the paint (Square: Color), then the race
#   garagecam     garagecam1 (src/b3replay/garagecam.h): the carselect path's SELECT CAR with a scripted pad
#                 (B3NATIVE_TEST_PAD=$PAD, default xbox; no buttons of it used: the keys drive the menus): the game's
#                 spin (shots 10560, 10600), the right stick left 21100-21140 (10670: the view turning), at rest (10690,
#                 10700: the view held), the spin back a second after (10740, 10780); the stick down 21500-21560 (the
#                 view up to its height cap: 10896), then left for ~90 degrees three times (10936, 10986, 11036: the
#                 highest view at four angles round the car); a mouse drag on the car 22300-22350 (11300: turned,
#                 nothing pressed; GARAGECAM_MOUSE=0: no mouse, so the shots keep the pad's prompts); then the race.
#                 (Front-end polls here: VBlank / 2 + 106.) The log must show no orbit before the stick, the view taken
#                 by the stick (and by the mouse), the spin resumed after each, no mouse press; the shots at rest within
#                 the second the same picture, the spin's shots before and after different
#   carseries     carsel1: the carselect path with PC SETTINGS > CAR SERIES ON L1 / R1 On ($OUT/pc_settings.ini
#                 series_shoulders = 1): on the car select R1 (R) twice, L1 (Q) once, then Right (the D-pad, as before), a
#                 car down, the race. The log must show each shoulder press given to the game as Right / Right / Left and
#                 the carousel's series following them (1, 2, 1, 2); shots of the page with the L1 / R1 glyphs
#   racetime      todrace (tools/native/race_time.h): SINGLE EVENT > RACE with PC SETTINGS > TIME OF DAY Day ($OUT/
#                 pc_settings.ini): on SELECT OPTIONS T (the TIME OF DAY action) twice (Default > Day > Night), the race
#                 (at night), the pause menu's QUIT (B3_PHOTO_ROW=0: six rows), YES, Back to the main menu (the choice
#                 back to Default), SINGLE EVENT > RACE again: the label reads Default, the race by day. The log must show
#                 the two presses, the choice Night, the race taking Night, the main menu's reset, the next race taking
#                 Default; shots of the page with the label (RACETIME_DEVICE: the prompts' device, default keyboard)
#                 RACETIME_ROWS=night | rain | both (B3_RACE_TIME_ROW=1: night): the choice through SELECT OPTIONS' TIME OF
#                 DAY / WEATHER rows (EXTRAS' NIGHT / RAIN alone on), the race must take Night / Rain / Night rain and its
#                 shots be drawn so ("drawn at night", "drawn in the rain"), the next race Default and by day
#   trafficseries trafficseries (tools/native/traffic_cars.h): heavyseries' path to TRAFFIC (TRAFFIC_RIGHTS=7; 8:
#                 TRAFFIC SPECIAL), TRAFFIC_CAR_DOWNS (default 7: the tuk tuk), TRAFFIC_MODE (as HEAVY_MODE). The log must show
#                 SELECT CAR on TRAFFIC and the race a traffic vehicle's (records kept apart)
#   customseries  customseries (tools/native/extra_cars.h): trafficseries' path on to CUSTOM (CUSTOM_RIGHTS=9 from the first
#                 series: TRAFFIC, TRAFFIC SPECIAL, CUSTOM), CUSTOM_CAR_DOWNS (default 1: the second car), B3_CUSTOM_CARS (the
#                 cars folder, needed: e.g. COMP/CAR2.BGV). The log must show SELECT CAR on CUSTOM and an extra car's race
#                 (records kept apart, its tuning its donor's)
#   heavyseries   heavyseries (tools/native/heavy_race.h): SINGLE EVENT > RACE (HEAVY_MODE=timeattack / roadrage / eliminator:
#                 one / two / three Downs on SELECT MODE) with Right x7 on SELECT CAR (to the last series, HEAVYWEIGHTS: the 100 % card
#                 has them unlocked), HEAVY_CAR_DOWNS (default 10: the City Bus) Downs to a car, the race driven with boost
#                 and a weave (rivals near, to HEAVY_WEAVE_END, default the end); ROAD RAGE runs to its end and its results. The log must show the series
#                 offered, SELECT CAR on it, the race a heavyweight race (records kept apart)
#   paintsel      paintsel (tools/native/paint_select.h): SINGLE EVENT > RACE's SELECT CAR with the PAINT prompt: P (CAR
#                 PAINT PANEL) opens the panel, Right (CAR PAINT Custom), Down to COLOUR and Left six times (Blue), Down four
#                 times to FINISH and Right (Metallic), Back closes it (saved to $OUT/pc_settings.ini); then Square (the
#                 game's own colour): CAR PAINT back to Game; then the race. The log must show the panel opened and closed
#                 with CAR PAINT Custom, the colour change setting it back to Game, the prompt and the panel drawn; shots of
#                 each step (PAINTSEL_DEVICE: the prompts' device, default keyboard; xbox, playstation, nintendo).
#                 PAINTSEL_P2=1: MULTIPLAYER's path (the multiplayer tour's) to player 2's SELECT CAR (G2): P opens player 2's
#                 panel there (the keyboard drives it), Right (P2 CAR PAINT Custom), COLOUR Left four times (Pink), Back;
#                 then the split-screen race (player 2's car pink)
#   crashresults  SINGLE EVENT > CRASH (the 5th row, elim1): the junction, the crash, CRASH RESULTS, CRASH $ and back to SELECT MODE
#   nfsdemo       DRIVER DETAILS > EXTRAS > PLAY NFS:UNDERGROUND 2 DEMO, YES: the game shuts down and asks for the demo
#                 (LoadExecPS2); the port ends there (platform.cpp load_exec_ps2), exit 0
# Keys: Down 81, Up 82, Left 80, Right 79, Cross Z 29, Triangle (back) Backspace 42, Square V 25. The verdict (below):
# native_play's end (the VBlank limit; nfsdemo: the hand-over), no crash, the VU0 counts, every FE_SHOTS shot taken, and
# each tour's own log evidence.
fe_keys() {   # fe_keys <vblank>:<scancode> (a 10-VBlank tap) or <vblank>:<scancode>:<0|1> (down / up) ...
  local t v c
  for t in "$@"; do
    v=${t%%:*}; c=${t#*:}
    case $c in *:*) K="$K,$t" ;; *) K="$K,$v:$c:1,$((v + 10)):$c:0" ;; esac
  done
}
FE=""
case "${SCENARIO:-race}" in
  restart|multiplayer|partycrash|online|ddrewards|ddrecords|ddsettings|eatrax|crashnav|carselect|thumbsweep|carseries|garagecam|crashresults|nfsdemo|racetime|paintsel|heavyseries|trafficseries|customseries|retail) FE=$SCENARIO ;;
esac
if [ -n "$FE" ]; then
  K="9000:41:1,9010:41:0,15500:29:1,15510:29:0,16300:29:1,16310:29:0"
  DN=81 UP=82 RT=79 X=29 BK=42 SQ=25
  DD="17900:$DN 18000:$DN 18100:$DN 18200:$DN 18500:$X"   # the main menu's DRIVER DETAILS
  case $FE in
  multiplayer)
    VBL=${VBLANKS:-34000}
    fe_keys 17900:$DN 18000:$DN 18300:$X \
      18800:$DN 19200:$X 20000:$BK 20400:$DN 20800:$X 21600:$BK 22000:$DN 22400:$X 23200:$BK 23600:$DN 24000:$X 24800:$BK \
      25200:$SQ 26000:$BK 26400:$UP 26500:$UP 26600:$UP 26700:$UP 27100:$X 27900:$X 28700:$X 30500:$X 31000:$X:1
    KEYS2=${KEYS2:-29500:29:1,29510:29:0,31000:29:1}
    FE_SHOTS=${FE_SHOTS:-9400,9800,10600,11400,12200,12750,13750,14250,14500,15500,16000,17000}
    ;;
  partycrash)
    VBL=${VBLANKS:-30000}
    fe_keys 17900:$DN 18000:$DN 18300:$X 18800:$DN 18900:$DN 19000:$DN 19100:$DN 19500:$X 20300:$X 21100:$X 21900:$X \
      22700:$X 23500:$X 24000:$X:1
    FE_SHOTS=${FE_SHOTS:-9900,10400,11150,11650,13900,14900}
    ;;
  restart)   # nextstart (tools/native/relaunch.h): the main menu's SETTINGS (five Downs), GRAPHICS (R: R1), Down to
    # ANTI-ALIASING, Right (a next-start row changed), Back: the question box (KEEP focused), Down to APPLY AND RESTART,
    # Cross: the run ends as QUIT GAME's and the game starts again (the same arguments but the keys: the second run plays
    # no script and stays on the title; START AT at least the title). The log must show the box, the answer and a second start.
    VBL=${VBLANKS:-22500}
    RS_ANSWER=${RS_ANSWER-20800:$DN}   # (RS_ANSWER= : KEEP, no restart: the box in the shots)
    fe_keys 17900:$DN 18000:$DN 18100:$DN 18200:$DN 18300:$DN 18700:$X 19200:21 19600:$DN 20000:$RT 20400:$BK $RS_ANSWER 21200:$X
    FE_SHOTS=${FE_SHOTS:-9800,10100,10300}
    ;;
  online)
    VBL=${VBLANKS:-21600}
    fe_keys 17900:$DN 18000:$DN 18100:$DN 18300:$X 19100:$X 19500:$DN 19600:$DN 20000:$X 20400:$DN 21000:$BK
    FE_SHOTS=${FE_SHOTS:-9200,9400,10400,10800}
    ;;
  ddrewards)
    VBL=${VBLANKS:-33000}
    fe_keys $DD 19000:$X 19500:$DN 19900:$DN 20300:$DN 20700:$BK \
      21100:$DN 21500:$X 22000:$X 22500:$X 23300:$X 24100:$BK \
      24900:$DN 25300:$X 25800:$RT 26100:$RT 26400:$DN 26800:$BK \
      27200:$DN 27600:$X 28400:$BK 28800:$DN 29200:$X 30000:$BK 30400:$DN 30800:$X 31600:$BK 32000:$BK
    FE_SHOTS=${FE_SHOTS:-9600,9800,10000,10300,10900,11200,11500,13000,13300,14200,14800,15700}
    ;;
  ddrecords)
    VBL=${VBLANKS:-35500}
    fe_keys $DD 19000:$DN 19100:$DN 19500:$X 20000:$X 20500:$X 21000:$RT 21500:$BK 21900:$BK \
      22300:$DN 22700:$X 23200:$X 23700:$BK 24100:$BK 24500:$DN 24900:$X 25400:$X 25900:$BK 26300:$BK 26700:$BK \
      27100:$DN 27500:$X 28000:$X 28400:$UP 28800:$X 29400:$X 30200:$DN 30600:$X 31100:$X 31500:$UP 31900:$X 32700:$X
    FE_SHOTS=${FE_SHOTS:-10600,11800,13000,14500,14800,16000,16300,17200}
    ;;
  ddsettings)
    VBL=${VBLANKS:-38500}
    fe_keys $DD 19000:$DN 19100:$DN 19200:$DN 19300:$DN 19700:$X \
      20200:$X 21000:$BK 21400:$DN 21800:$X 22600:$BK 23000:$DN 23100:$DN 23500:$X 24300:$BK \
      24700:$DN 24800:$DN 24900:$DN 25300:$X 26100:$BK 26500:$DN 26600:$DN 26700:$DN 26800:$DN 27200:$X 28000:$BK 28400:$BK \
      28800:$DN 29200:$X 29700:$X 31100:$X 31600:$BK \
      32000:$DN 32400:$X 32900:$DN 33300:$X 34100:$X 34600:$DN 34700:$DN 34800:$DN 34900:$DN 35000:$DN 35100:$DN 35200:$DN \
      35600:$X
    FE_SHOTS=${FE_SHOTS:-10300,10900,11800,12700,14000,15200,16400,17000,18200}
    ;;
  eatrax)   # eatrax1: SETTINGS (4 down), EA GAMES TRAX (4 down), then the page's rows (a Down every 40 VBlanks)
    VBL=${VBLANKS:-32600}
    fe_keys $DD 19000:$DN 19100:$DN 19200:$DN 19300:$DN 19700:$X 20200:$DN 20300:$DN 20400:$DN 20500:$DN 20900:$X
    for k in $(seq 0 43); do fe_keys $((21500 + 40 * k)):$DN; done
    fe_keys 24000:$RT 24500:$DN 24900:$DN 25300:$DN 25600:$RT 25800:$RT 26000:$RT 26600:$X   # (back on SETTINGS' first row)
    fe_keys 27400:$DN 27500:$DN 27600:$DN 27700:$DN 28000:$X
    for k in $(seq 0 43); do fe_keys $((28500 + 40 * k)):$DN; done
    fe_keys 30600:$RT 31000:$BK
    FE_SHOTS=${FE_SHOTS:-10600,11800,12100,12350,12750,13250,14100,15200,15400,16200}
    ;;
  crashnav)
    VBL=${VBLANKS:-37000}
    fe_keys 17900:$X 18800:$DN 19000:$DN 19400:$X 20000:$X 20500:$DN 20700:$DN 20900:$DN 21300:$X 22300:$X 22900:$X \
      23500:$X 23600:$X:1 29000:$X:0 29500:$X 30200:$X 30900:$X 31600:$X 32300:$X 33000:$X 33700:$X 34400:$X
    FE_SHOTS=${FE_SHOTS:-9500,9800,10100,10700,11000,12500,13100,17300,18800,19700}
    ;;
  thumbsweep)   # paint3: every series' carousel scrolled (SWEEP_DOWNS rows each), one shot at the end; with B3_THUMB_SEARCH=1
    # B3_THUMB_SEARCH_MIN=60 (and paint on) the thumbnails' view measured against all the game's car pictures (docs/pc-settings.md §7.31.1)
    VBL=${VBLANKS:-28500}
    KS="17900:$DN 18300:$X 19100:$X 19900:$X"; v=20900
    for sr in 0 1 2 3 4 5 6; do for k in $(seq 1 ${SWEEP_DOWNS:-11}); do KS="$KS $v:$DN"; v=$((v + 60)); done; KS="$KS $v:$RT"; v=$((v + 400)); done
    fe_keys $KS
    FE_SHOTS=${FE_SHOTS:-14000}
    ;;
  retail)   # extras: the game as it shipped (B3NATIVE_EXTRAS=0: every EXTRAS row off, ELIMINATOR too): SINGLE EVENT's SELECT MODE
    # (shot), RACE's SELECT OPTIONS (shot: the game's own page), SELECT CAR, Right eight times (the game's series only)
    VBL=${VBLANKS:-24000}
    fe_keys 17900:$DN 18300:$X 19100:$X 19900:$X 20500:$RT 20800:$RT 21100:$RT 21400:$RT 21700:$RT 22000:$RT 22300:$RT 22600:$RT
    FE_SHOTS=${FE_SHOTS:-9500,9800,10200,11500}
    ;;
  carselect)
    VBL=${VBLANKS:-27000}
    fe_keys 17900:$DN 18300:$X 19100:$X 19900:$X 20900:$RT 21300:$RT 21700:$DN 22100:$DN 22500:$SQ 22900:$SQ 23300:$UP \
      23700:$X 24500:$X 24600:$X:1
    FE_SHOTS=${FE_SHOTS:-9800,10150,10650,10900,11400,11650,11900,12400,13150}
    ;;
  garagecam)   # garagecam1: the scripted pad's right stick (nativepad.h kTestPadCode + 35 left, + 37 down)
    VBL=${VBLANKS:-27000}
    export B3NATIVE_TEST_PAD=${PAD:-xbox}
    fe_keys 17900:$DN 18300:$X 19100:$X 19900:$X 21100:1059:1 21140:1059:0 21500:1061:1 21560:1061:0 \
      21600:1059:1 21643:1059:0 21700:1059:1 21743:1059:0 21800:1059:1 21843:1059:0 23700:$X 24500:$X 24600:$X:1
    [ "${GARAGECAM_MOUSE:-1}" = 1 ] && MOUSE=${MOUSE:-22290:move:0.5:0.5,22300:down:1,22310:move:0.53:0.5,22320:move:0.57:0.51,22330:move:0.62:0.52,22350:up:1}
    FE_SHOTS=${FE_SHOTS:-10560,10600,10670,10690,10700,10740,10780,10896,10936,10986,11036,11300,13150}
    ;;
  carseries)   # carsel1: R = R1, Q = L1 (docs/mac.md §5); the row on through the renderer's --settings
    VBL=${VBLANKS:-27000}
    printf 'series_shoulders = 1\n%s' "${CARSERIES_INI:-}" > "$OUT/pc_settings.ini"   # (CARSERIES_INI: more rows, e.g. button_prompts = 2)
    RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
    fe_keys 17900:$DN 18300:$X 19100:$X 19900:$X 20900:21 21300:21 21700:20 22100:$RT 22500:$DN 23300:$UP \
      23700:$X 24500:$X 24600:$X:1
    FE_SHOTS=${FE_SHOTS:-10150,10650,10850,11050,11250,11400,13150}
    ;;
  racetime)   # todrace: T = TIME OF DAY (scancode 23); the race's Cross held 20400-23000; the pause menu's QUIT is row 6
    VBL=${VBLANKS:-${RACETIME_VBL:-34000}}
    printf 'time_of_day = 0\n%s' "${RACETIME_INI:-}" > "$OUT/pc_settings.ini"
    RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
    export B3_PHOTO_ROW=0 B3_PROMPT_DEVICE=${RACETIME_DEVICE:-keyboard} B3_RACE_TIME_LABEL=1   # (the label on, golden pin or not)
    # 1.3.x: the per-race label is held back (input_bindings.h kRaceTimeThisRelease): the T presses must be ignored and
    # nothing drawn at night. RACETIME_ROWS=night | rain | both (condrows; B3_RACE_TIME_ROW=1 is night): the choice through
    # SELECT OPTIONS' rows instead, with EXTRAS' ELIMINATOR and only those EXPERIMENTAL rows on (B3NATIVE_EXTRAS), so the
    # page is REGION, TRACK, RIVALS, then TIME OF DAY (Day / Night) and / or WEATHER (Clear / Rain) whatever the other
    # rows' layout (racetimefix: the Downs to the old TIME OF DAY row went stale with the two pages): Down x3, Right
    # (Night, or Rain); both: Down x3, Right (Night), Down, Right (Night rain). The verdict needs the race to take it.
    RTR=${RACETIME_ROWS:-}; [ -z "$RTR" ] && [ "${B3_RACE_TIME_ROW:-0}" = 1 ] && RTR=night
    case "$RTR" in
      night) B3NATIVE_EXTRAS=0x810000; RTK="19400:$DN 19450:$DN 19500:$DN 19700:$RT" ;;
      rain)  B3NATIVE_EXTRAS=0x1010000; RTK="19400:$DN 19450:$DN 19500:$DN 19700:$RT" ;;
      both)  B3NATIVE_EXTRAS=0x1810000; RTK="19400:$DN 19450:$DN 19500:$DN 19600:$RT 19700:$DN 19800:$RT" ;;
      "")    RTK="19400:23 19600:23" ;;
      *)     echo "race_check: FAIL RACETIME_ROWS=$RTR (night, rain or both)"; exit 1 ;;
    esac
    fe_keys 17900:$DN 18300:$X 19100:$X $RTK 19900:$X 20400:$X:1 23000:$X:0 \
      ${RACETIME_KEYS:-23200:41 23500:$DN 23600:$DN 23700:$DN 23800:$DN 23900:$DN 24200:$X 24600:$UP 24900:$X 26000:$BK 27600:$X 28400:$X 29200:$X 30000:$X:1}
    # shots: the page before the presses (Default (Day)), after them (Night), the race (night), the page again after the
    # main menu (Default (Day)), the second race (day). (Polls: VBlank / 2 + 106 in the first front end, VBlank - 10108 in
    # the first race, VBlank / 2 + 2437 after it, VBlank - 12577 in the second race.)
    # 17300: SELECT CAR's showroom after the night race (the race's value is still Night until the next load: drawn by day)
    FE_SHOTS=${FE_SHOTS:-9800,9956,10700,11500,17000,17300,18300,18900}
    ;;
  heavyseries|trafficseries|customseries)
    VBL=${VBLANKS:-30000}
    # customseries: Rights on past the traffic series to CUSTOM, the car Downs after the last step's move
    CDB=22500
    if [ "$FE" = customseries ]; then
      [ -n "${B3_CUSTOM_CARS:-}" ] || { echo "race_check: FAIL SCENARIO=customseries needs B3_CUSTOM_CARS (a cars folder)"; exit 1; }
      FE=trafficseries; CUSTOM=1; TRAFFIC_RIGHTS=${CUSTOM_RIGHTS:-9}; TRAFFIC_CAR_DOWNS=${CUSTOM_CAR_DOWNS:-1}
      CDB=$((20700 + 200 * TRAFFIC_RIGHTS + 300))   # (carpacks: CUSTOM 2, 3, ... are more Rights)
    fi
    # trafficseries: Rights to TRAFFIC (TRAFFIC_RIGHTS, default 7 from the first series; 8: TRAFFIC SPECIAL) and TRAFFIC_CAR_DOWNS (default 7: the tuk tuk)
    if [ "$FE" = trafficseries ]; then HEAVY_MODE=${TRAFFIC_MODE:-race}; HEAVY_CAR_DOWNS=${TRAFFIC_CAR_DOWNS:-7}; fi
    # (heavyseries: six Rights from the first series to HEAVYWEIGHTS, the last of the game's: a seventh goes on to TRAFFIC)
    RTS=""; for k in $(seq 1 "$([ "$FE" = trafficseries ] && echo "${TRAFFIC_RIGHTS:-7}" || echo 6)"); do RTS="$RTS $((20700 + 200 * k)):$RT"; done
    MD=""; [ "${HEAVY_MODE:-race}" = timeattack ] && MD="18800:$DN"; [ "${HEAVY_MODE:-race}" = roadrage ] && MD="18800:$DN 18900:$DN"
    [ "${HEAVY_MODE:-race}" = eliminator ] && MD="18800:$DN 18900:$DN 19000:$DN"
    CD=""; for k in $(seq 1 "${HEAVY_CAR_DOWNS:-10}"); do CD="$CD $((CDB + ${CUSTOM_DOWN_GAP:-60} * k)):$DN"; done   # (carpacks: CUSTOM_DOWN_GAP, down a page of 20)
    # HEAVY_OPTS_KEYS: SELECT OPTIONS keys before its Continue at 19900 (raceopts' rows), e.g. TRAFFIC Off:
    # "19500:81 19550:81 19600:81 19650:80 19700:80"
    fe_keys 17900:$DN 18300:$X $MD 19100:$X ${HEAVY_OPTS_KEYS:-} 19900:$X $RTS \
      $CD 23700:$X 24500:$X 24600:$X:1 25200:225:1
    for v in $(seq 25400 300 $((${HEAVY_WEAVE_END:-$VBL} - 300))); do K="$K,$v:80:1,$((v+40)):80:0,$((v+150)):79:1,$((v+190)):79:0"; done
    FE_SHOTS=${FE_SHOTS:-11000,11250,11800,13000,14000}
    ;;
  paintsel)   # paintsel: P = CAR PAINT PANEL (scancode 19); Backspace closes the panel (Back); V = Square (the game's colour)
    VBL=${VBLANKS:-34000}
    printf 'car_paint = 0\n%s' "${PAINTSEL_INI:-}" > "$OUT/pc_settings.ini"
    RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
    export B3_PROMPT_DEVICE=${PAINTSEL_DEVICE:-keyboard}
    # the panel takes the pad's reports on the VBlank grid (NativePad::panel_feed); its picture and the car's paint in a
    # shot are as the renderer has them when it takes the shot (as the PC SETTINGS page's), so each step has a long gap
    # (the offscreen renderer runs hundreds of VBlanks behind around a shot)
    # paint2: the panel's rows: LAYER, COLOUR (Game, ready colours, Custom), COLOUR PICKER, RED, GREEN, BLUE, FINISH,
    # DECALS, FACTORY COLOURS. R = R1, Q = L1 (the picker's hue); Left Shift = boost (R1) in the race
    if [ "${PAINTSEL_P2:-0}" = 1 ]; then   # player 2's: the multiplayer tour's path to G2 SELECT CAR (from VBlank 28755)
      VBL=${VBLANKS:-40000}
      printf 'p2_car_paint = 0\n%s' "${PAINTSEL_INI:-}" > "$OUT/pc_settings.ini"
      # COLOUR Right x7 (Pink), LAYER Wheels, COLOUR Right (Red), Back
      fe_keys 17900:$DN 18000:$DN 18300:$X \
        18800:$DN 19200:$X 20000:$BK 20400:$DN 20800:$X 21600:$BK 22000:$DN 22400:$X 23200:$BK 23600:$DN 24000:$X 24800:$BK \
        25200:$SQ 26000:$BK 26400:$UP 26500:$UP 26600:$UP 26700:$UP 27100:$X 27900:$X 28700:$X \
        30000:19 30800:$DN 30900:$RT 31000:$RT 31100:$RT 31200:$RT 31300:$RT 31400:$RT 31500:$RT 31700:$UP 31800:$RT 31900:$RT \
        32000:$DN 32100:$RT 33000:$BK 36000:$X 36500:$X:1
      KEYS2=${KEYS2:-35000:29:1,35010:29:0,36500:29:1}
      # shots: player 2's page with the prompt, the panel (Pink, red wheels), closed (the car and its thumbnails), the race
      FE_SHOTS=${FE_SHOTS:-14600,16300,17300,${PAINTSEL_RACE_SHOT:-22000}}
    else
    VBL=${VBLANKS:-38000}
    # P opens the panel; COLOUR Right x5 (Blue); COLOUR PICKER: Cross, R1 held (the hue on), Down held (darker), Cross;
    # LAYER Decals, COLOUR Right x3 (Yellow); LAYER Wheels, COLOUR Right (Red); LAYER Boost glow, COLOUR Right x4 (Green);
    # Back (saved). Then the race with the boost held
    K1="17900:$DN 18300:$X 19100:$X 19900:$X 21500:19 22400:$DN 22600:$RT 22700:$RT 22800:$RT 22900:$RT 23000:$RT 23300:$DN 23500:$X \
      23800:21:1 23860:21:0 24000:$DN:1 24030:$DN:0 25000:$X 25300:$UP 25400:$UP 25600:$RT 25800:$DN 26000:$RT 26100:$RT 26200:$RT \
      26500:$UP 26700:$RT 26900:$DN 27100:$RT 27400:$UP 27600:$RT 27800:$DN 28000:$RT 28100:$RT 28200:$RT 28300:$RT 28700:$BK"
    if [ "${PAINTSEL_RESET:-0}" = 1 ]; then   # then Square (the body back to Game), the panel again: FACTORY COLOURS (all Game)
      fe_keys $K1 31000:$SQ 32500:19 33000:$DN 33100:$DN 33200:$DN 33300:$DN 33400:$DN 33500:$DN 33600:$DN 33700:$DN 34800:$X 35800:$BK \
        37500:$X 38300:$X 38400:$X:1
      VBL=${VBLANKS:-43000}
      # shots: the car with every layer (thumbnails too), after Square (body Game, the rest kept), after FACTORY (all Game)
      FE_SHOTS=${FE_SHOTS:-15056,16056,18356,${PAINTSEL_RACE_SHOT:-22000}}
    else
      fe_keys $K1 31000:$X 31800:$X 31900:$X:1 33000:225:1
      # shots (polls: VBlank / 2 + 106 here): the page with the prompt, the panel (Game), the picker, the car with every
      # layer (and the carousel's thumbnails), then the race (boosting)
      FE_SHOTS=${FE_SHOTS:-10110,11256,12456,15056,${PAINTSEL_RACE_SHOT:-19000}}
    fi
    fi
    ;;
  crashresults)
    VBL=${VBLANKS:-33000}
    fe_keys 17900:$DN 18300:$X 18760:$DN 18840:$DN 18920:$DN 19000:$DN 19100:$X 19900:$X 20400:$X:1 21000:$X:0 21010:$X:1 \
      21600:$X:0 21610:$X:1 22200:$X:0 22210:$X:1 27000:$X:0 27500:$X 28200:$X 28900:$X 29600:$X
    FE_SHOTS=${FE_SHOTS:-9800,13300,17200,17500,17800}
    ;;
  nfsdemo)
    VBL=${VBLANKS:-24000}
    fe_keys $DD 19000:$DN 19100:$DN 19200:$DN 19300:$DN 19400:$DN 19500:$DN 19900:$X 20400:$X 21000:$UP 21400:$X
    FE_SHOTS=${FE_SHOTS:-10200,10400}
    ;;
  esac
  SHOTS=${SHOTS:+$SHOTS,}$FE_SHOTS
  [ "$FE" = online ] && export B3NATIVE_STRING_LOG=${B3NATIVE_STRING_LOG:-105}
fi
# EXTRA_KEYS=<vblank>:<scancode>:<0|1>,... (save1): more keys after the scenario's (later VBlanks than its last), e.g. a
# results page's Continue to reach the autosave
[ -n "${EXTRA_KEYS:-}" ] && K="$K,$EXTRA_KEYS"
# SCENARIO=pausemouse (mac5, remaster-features §8): the pause menu at 21600 (Cross released at 21700) driven by the mouse
# (tools/native/menu_mouse.h): hover AUDIO, the wheel down to CONTROLLERS, a click on SETTINGS (the PC SETTINGS page opens),
# a right click (it closes), a click on the prompt bar's START Resume (the race goes on); positions for the wide layout
# at RENDER_EXTRA's size (1280 x 720 or any 16:9; pinned here: at 4:3 the START prompt is at x 0.25-0.40, so the click
# at 0.5 misses it) with the PHOTO MODE row on (pinned: without it the menu has six rows and they move); each point is
# its row's centre (AUDIO 0.40-0.47, SETTINGS 0.54-0.60). The log must show the presses, the page opened and closed,
# and the race driven again.
if [ "${SCENARIO:-race}" = pausemouse ]; then
  VBL=${VBLANKS:-24000}
  K="$K,21600:41:1,21610:41:0,21700:29:0"
  export B3_PHOTO_ROW=1
  RENDER_EXTRA="${RENDER_EXTRA:---size 1280x720}"
  MOUSE=${MOUSE:-21800:move:0.15:0.44,21900:wheel:-1,22000:move:0.15:0.57,22010:click:1,22400:move:0.5:0.5,22600:click:3,23300:move:0.5:0.93,23310:click:1}
fi
# SCENARIO=pausemouseq (mac5): the pause menu's confirmation popup by mouse. Hover jumps straight to the row (no stepping
# presses): hover QUIT and click it ("Are you sure you want to quit?", the game's message box), hover YES (the highlight
# moves from NO to YES with no press) and click it. The log must show the message box as a mouse target, its row 0 (YES)
# hovered and two presses (the two clicks' Cross).
if [ "${SCENARIO:-race}" = pausemouseq ]; then
  VBL=${VBLANKS:-24000}
  K="$K,21600:41:1,21610:41:0,21700:29:0"
  export B3NATIVE_MENU_TRACE=1
  RENDER_EXTRA="${RENDER_EXTRA:---size 1280x720}"
  MOUSE=${MOUSE:-21800:move:0.13:0.70,21900:click:1,22800:move:0.51:0.60,23000:click:1}
fi
# SCENARIO=profilename (mac5, remaster-features §8): a fresh card; after the title the game asks for a profile (the
# dialog LOAD PROFILE / NEW PROFILE): hover NEW PROFILE and click it, type "Rus 3", Backspace, "0" and Enter on the name
# keyboard (the window's text, menu_mouse.h), hover YES on "save your profile?" and click it, click on the slot page
# (Cross: PROFILE 1). The log must show the keyboard taking text, the 8 characters typed, and the save written.
if [ "${SCENARIO:-race}" = profilename ]; then
  VBL=${VBLANKS:-19600}
  K="9000:41:1,9010:41:0"
  MOUSE=${MOUSE:-15200:move:0.5:0.635,15400:click:1,17100:move:0.5:0.503,17300:click:1,18300:click:1}
  TEXT=${TEXT:-16000:Rus 3<BS>0;16300:<ENTER>}
fi
# SCENARIO=photo (photo mode, src/b3replay/photomode.h): after the two race shots, the pause menu at 23400 (Cross
# released at 23500), P at 23900 (photo mode on), then Right 24200-24230 (turn), W 24200-24290 (forward), E
# 24250-24280 (up), C 24700-24730 (roll), - 24730-24800 (narrower), H at 24850 (the help box off), Escape at 25100
# (photo mode off). Shots (polls; poll = VBlank - 10108 in the race and its pause menu, B3NATIVE_POLL_LOG): A 13592
# paused, B 14050 photo mode as entered, C 14492 moved, D 14842 rolled and zoomed, E 15292 after it. The verdict: B, C,
# D drawn from the free camera (the log), A and E not, E the same picture as A (the game's camera and the pause menu
# back exactly; the keys withheld: W / Escape would have moved / closed the menu), B differs from A (no HUD), C from B
# (the camera moved). The free camera follows the game's polls, not the renderer's (photomode.h "Game time", 2026-11-11):
# entry at poll 13792, exit at 14992, and shots B-D the same pictures in every run however far the game runs ahead.
# SCENARIO=photodof (photo2: photo mode's depth of field, photomode.h / b3gfx FrameParams::Dof): the photo scenario
# with F (the depth of field on: focus on the car, blur 4) at 23950 and F again (off) at 24170, before the camera
# moves; two more shots, B0 13820 (photo mode, the depth of field not on yet) and B1 14080 (off again, the same camera).
# The verdict also: shot B (14050) drawn with the depth of field (the log) and different from B0, B1 = B0. With
# PHOTO_GOLDEN=<the photo scenario's shots folder>: B0 equals its shot B, and A, C, D, E its A, C, D, E (the effect off:
# the frames as before).
# SCENARIO=photorow (photorow1, tools/native/pc_menu.h): the photo scenario, entered THROUGH the pause menu's PHOTO MODE row
# (B3_PHOTO_ROW=1 whatever the setting): Down x$PHOTO_ROW_DOWNS (default 5: RESUME, RESTART, AUDIO, CONTROLLERS, SETTINGS,
# then PHOTO MODE) at 23720.. and Enter at 23900 instead of P; after Escape leaves photo mode, Up x5 at 25150.. back to RESUME,
# so shot E (25400) equals shot A (taken with the cursor on RESUME before the Downs): the pause menu is back as it was.
# The log must show the row confirmed and photo mode on from the pause menu, never from P.
if [ "${SCENARIO:-race}" = photo ] || [ "${SCENARIO:-race}" = photodof ] || [ "${SCENARIO:-race}" = photorow ]; then
  VBL=${VBLANKS:-25700}
  K="$K,23400:41:1,23410:41:0,23500:29:0"
  if [ "${SCENARIO:-race}" = photorow ]; then
    export B3_PHOTO_ROW=1
    for i in $(seq 0 $(( ${PHOTO_ROW_DOWNS:-5} - 1 ))); do K="$K,$((23720 + 30 * i)):81:1,$((23730 + 30 * i)):81:0"; done
    K="$K,23900:40:1,23910:40:0"
  else
    K="$K,23900:19:1,23910:19:0"
  fi
  # (native_play takes the script in its order: the keys go in VBlank order)
  [ "${SCENARIO:-race}" = photodof ] && K="$K,23950:9:1,23960:9:0,24170:9:1,24180:9:0"
  K="$K,24200:79:1,24200:26:1,24230:79:0,24250:8:1,24280:8:0,24290:26:0,24700:6:1,24730:6:0,24730:45:1,24800:45:0"
  K="$K,24850:11:1,24860:11:0,25100:41:1,25110:41:0"
  if [ "${SCENARIO:-race}" = photorow ]; then
    for i in $(seq 0 $(( ${PHOTO_ROW_DOWNS:-5} - 1 ))); do K="$K,$((25150 + 30 * i)):82:1,$((25160 + 30 * i)):82:0"; done
  fi
  PHOTO_SHOTS=${PHOTO_SHOTS:-13592,14050,14492,14842,15292}
  SHOTS=${SHOTS:+$SHOTS,}$PHOTO_SHOTS
  if [ "${SCENARIO:-race}" = photodof ]; then
    DOF_SHOTS=${DOF_SHOTS:-13820,14080}
    SHOTS=$SHOTS,$DOF_SHOTS
  fi
fi
# SCENARIO=boosttoggle (accessibility: PC SETTINGS > CONTROLS > DRIVING > BOOST Toggle; opt-in, it changes the
# input): the race scenario with $OUT/pc_settings.ini `boost_mode = 1` (the renderer's --settings), Left Shift (R1, boost)
# tapped for 10 VBlanks at 21400 and again at 22800 (the driving context begins near 21070; a tap in the countdown is
# passed as it is). The log must show the latch taken and dropped, and the pad recording R1 held from the first tap to
# the second (~1400 VBlanks; Hold would give 2 x 10)
if [ "${SCENARIO:-race}" = boosttoggle ]; then
  printf 'boost_mode = 1\n' > "$OUT/pc_settings.ini"
  RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
  K="$K,21400:225:1,21410:225:0,22800:225:1,22810:225:0"
fi
# SCENARIO=padlayout (1.2.1 CONTROLLER LAYOUT, docs/pc-settings.md §7.25): the race scenario driven by a scripted pad
# of PAD=xbox (default) or ps (B3NATIVE_TEST_PAD: player 1's pad, through the bindings as a window pad; no keys): its
# Start on the title, its A / Cross (South) for every confirm, its D-pad down; the throttle held from 20400 (Xbox: A
# tapped there, the confirm the keyboard's held Cross gives, then the right trigger, the analog pedal, which drives the
# right stick and so confirms nothing; PS: Cross), then while driving (from ~21070) boost held 21200-21400 (Xbox: A; PS:
# R1), B / Circle (East) tapped at 21450 (PS: R3 at 21500 too), Start at 21600 (the pause menu) and the back button at 21800 (Xbox: B; PS:
# Triangle; the throttle let go at 21700 as the keyboard scenarios do), A / Cross at 22000 (RESUME if still paused; PS:
# held on as the throttle; Xbox: the right trigger again). The verdict reads the pad recording (the reports the game
# read): Cross from South in the menus; R1 (not Cross) from A while driving with the Xbox layout; R2 from B while
# driving, Triangle from B in the pause menu (Xbox); Circle while driving, R2 from R3, Triangle in the pause menu (PS).
# PAD=switch (padpaint): a Switch Pro pad with CONTROLLER LAYOUT Nintendo ($OUT/controls.ini `layout = nintendo`, the
# renderer's --controls): the Xbox script with South and East swapped (A, the right button, confirms and boosts; B, the
# bottom one, is the Crashbreaker and goes back) and the same verdict, plus the log's "controller layout: player 1 nintendo"
if [ "${SCENARIO:-race}" = padlayout ]; then
  PADT=${PAD:-xbox}
  export B3NATIVE_TEST_PAD=$PADT
  P=1024; S=$P; E=$((P+1)); N=$((P+3)); ST=$((P+6)); R3=$((P+8)); RB=$((P+10)); DD=$((P+12)); RT=$((P+33))
  if [ "$PADT" = switch ]; then
    printf 'version = 3\n[player1.gamepad]\nlayout = nintendo\n' > "$OUT/controls.ini"
    RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--controls $OUT/controls.ini"
    S=$((P+1)); E=$P   # the labels: A (the confirm and boost) is East, B South
  fi
  K="9000:$ST:1,9010:$ST:0"
  for v in $(seq 15500 800 16300); do K="$K,$v:$S:1,$((v+10)):$S:0"; done
  K="$K,17900:$DD:1,17910:$DD:0,18300:$S:1,18310:$S:0"
  for v in $(seq 19100 800 19900); do K="$K,$v:$S:1,$((v+10)):$S:0"; done
  if [ "$PADT" = ps ]; then
    K="$K,20400:$S:1,21200:$RB:1,21400:$RB:0,21450:$E:1,21460:$E:0,21500:$R3:1,21510:$R3:0,21600:$ST:1,21610:$ST:0,21700:$S:0,21800:$N:1,21810:$N:0,22000:$S:1"
  else
    K="$K,20400:$S:1,20410:$S:0,20410:$RT:1,21200:$S:1,21400:$S:0,21450:$E:1,21460:$E:0,21600:$ST:1,21610:$ST:0,21700:$RT:0,21800:$E:1,21810:$E:0,22000:$S:1,22010:$S:0,22020:$RT:1"
  fi
fi
# RUMBLE=1 (rumble1, src/native/pad_actuator.h; any scenario): PC SETTINGS > RUMBLE On ($OUT/pc_settings.ini `rumble = 1`,
# the renderer's --settings) and B3NATIVE_RUMBLE_LOG=1 (each acknowledged request's motors in the log). The verdict also
# needs rumble on in the log and native_play's end-of-run rumble line; the PASS line gives the requests and the motors'
# peaks (no physical pad needed: the values are the ones the game sent)
if [ "${RUMBLE:-0}" = 1 ]; then
  [ "${SCENARIO:-race}" = boosttoggle ] || [ "${SCENARIO:-race}" = carseries ] || : > "$OUT/pc_settings.ini"
  printf 'rumble = 1\n' >> "$OUT/pc_settings.ini"
  case "${RENDER_EXTRA:-}" in *--settings*) ;; *) RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini" ;; esac
  export B3NATIVE_RUMBLE_LOG=1
fi
# MOUSE / TEXT (mac5, tools/native/menu_mouse.h): scripted mouse events / typing (native_play --mouse / --text);
# KEYS replaces the key script above
K=${KEYS:-$K}
# STRICT=1 (mac5): native_play --strict, a fallback to retail code halts the run (vu0n: so does a VU0 launch that no
# generated program covers; the end-of-run vu0 line is checked below).
# SHOTS=<poll,...> (mac5): extra screenshots besides the two race shots; RENDER_EXTRA: more renderer options (e.g. --size 1600x900)
ARGS=(--game "$GAME" --offscreen --no-pace --keys "$K" --render-args "--offscreen-live $OUT/shots --shots 10700,11500${SHOTS:+,$SHOTS}${RENDER_EXTRA:+ $RENDER_EXTRA}")
# PACE_FROM=<VBlank> (autosave1): real time from there instead of unpaced (the pacing and FAST LOADING / SAVE SCREENS skips are
# then in play; the run takes (VBLANKS - PACE_FROM) / 59.94 s)
if [ -n "${PACE_FROM:-}" ]; then
  for i in "${!ARGS[@]}"; do [ "${ARGS[$i]}" = "--no-pace" ] && ARGS[$i]="--pace-from" && ARGS=("${ARGS[@]:0:$((i+1))}" "$PACE_FROM" "${ARGS[@]:$((i+1))}") && break; done
fi
[ -n "${EXPLORE_RETAIL:-}" ] && ARGS+=(--explore-retail "$EXPLORE_RETAIL")
ARGS+=(--record-pad "$OUT/pads.txt")   # mac4: the reports given (replayable: native_play --replay-pad)
[ "${STRICT:-0}" = 1 ] && ARGS+=(--strict)   # mac5: STRICT=1 -- a retail fallback halts the run (the 100%-native check)
[ -n "${MOUSE:-}" ] && ARGS+=(--mouse "$MOUSE")   # mac5
[ -n "${TEXT:-}" ] && ARGS+=(--text "$TEXT")
[ -n "${KEYS2:-}" ] && ARGS+=(--keys2 "$KEYS2")   # fe1: a second controller's scripted keys (native_play --keys2)
# MODS=<ids> / MODS_DIR=<folder> (modkit1, docs/modding/DESIGN.md §1): native_play --mods / --mods-dir. Unset: no mods
# (an offscreen run never reads the user's mods.ini)
[ -n "${MODS:-}" ] && ARGS+=(--mods "$MODS")
[ -n "${MODS_DIR:-}" ] && ARGS+=(--mods-dir "$MODS_DIR")
# replay1 (src/native/replay_file.h): RECORD_REPLAY=<file> writes the run as a replay file (native_play --record-replay);
# REPLAY=<file> plays one (native_play --replay: its pads, card and options replace the scenario's; the keys are ignored).
# tools/native/replay_check.sh runs a scenario both ways and compares the two runs' logs
[ -n "${RECORD_REPLAY:-}" ] && ARGS+=(--record-replay "$RECORD_REPLAY")
[ -n "${REPLAY:-}" ] && ARGS+=(--replay "$REPLAY")
# tune4: REPLAY_PAD=<pads.txt> plays a pad recording (native_play --replay-pad: its reports, and its tuning edits, replace
# the scenario's keys); the run still records its own pads.txt
[ -n "${REPLAY_PAD:-}" ] && ARGS+=(--replay-pad "$REPLAY_PAD")
ENVS=(B3NATIVE_MAX_VBLANKS="$VBL" B3HOST_FATAL_ABORT=1 B3NATIVE_SAVES_ROOT="$OUT/card" B3NATIVE_CARD_PRESENT_VBLANK=270)
# extras (PC SETTINGS > EXTRAS): the scenarios walk the port's rows, modes and series, so every extra is on unless the
# caller says otherwise (B3NATIVE_EXTRAS=all by default: every extra but the EXPERIMENTAL one; set but empty: the player's
# defaults, ELIMINATOR alone; SCENARIO=retail: 0, every extra off, ELIMINATOR too: the game as it shipped).
# While a replay is recorded or played (RECORD_REPLAY / REPLAY) only B3NATIVE_EXTRAS and the settings count: a feature's
# own switch (B3NATIVE_ELIMINATOR=1 and the like) is ignored then
if [ "${SCENARIO:-race}" = retail ]; then ENVS+=(B3NATIVE_EXTRAS=0); else ENVS+=(B3NATIVE_EXTRAS="${B3NATIVE_EXTRAS-all}"); fi
if [ "${MOVIES:-native}" = standin ]; then ARGS+=(--explore-movies); ENVS+=(B3NATIVE_IPU_STANDIN=1); fi
# SCENARIO=music (custom soundtrack, remaster-features §6; tools/native/custom_soundtrack.h): the race scenario with a
# music folder of synthetic tones made here ($OUT/music: two WAVs, 40 s and 25 s, tagged by their file names) or
# MUSIC_DIR (read only), in MUSIC_MODE (default 1 = custom only; 2 = mixed). The log must show a custom song decoded and
# played through the music stream, its popup text given to the game's EA TRAX display, and samples mixed (SHOTS for the
# popup: it shows ~5 s after a song starts, the log's "popup:" VBlank / 2 = the poll)
if [ "${SCENARIO:-race}" = music ]; then
  MDIR=${MUSIC_DIR:-$OUT/music}
  if [ -z "${MUSIC_DIR:-}" ]; then
    rm -rf "$MDIR"; mkdir -p "$MDIR/Test Album"
    python3 -I - "$MDIR" <<'PY'
import math, struct, sys, wave
def tone(path, hz, seconds, rate=44100):
    with wave.open(path, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
        n = int(rate * seconds)
        w.writeframes(b''.join(struct.pack('<hh', int(8000 * math.sin(2 * math.pi * hz * k / rate)),
                                           int(8000 * math.sin(2 * math.pi * hz * 1.5 * k / rate))) for k in range(n)))
tone(sys.argv[1] + '/Test Album/01 Synthetic Band - First Tone.wav', 440.0, 40.0)
tone(sys.argv[1] + '/Test Album/02 Synthetic Band - Second Tone.wav', 330.0, 25.0)
PY
  fi
  export B3NATIVE_MUSIC_DIR="$MDIR" B3NATIVE_MUSIC_MODE="${MUSIC_MODE:-1}" B3NATIVE_MUSIC_MODES_FILE="$OUT/music_modes.ini"
  export B3NATIVE_MUSIC_LOUDNESS_FILE="$OUT/music_loudness.txt"   # musicloud: the measured songs, never the user's cache
  rm -f "$OUT/music_loudness.txt"
fi
if [ "${SCENARIO:-race}" = eatrax ]; then   # eatrax1: the tour's music folder (synthetic tones, tagged by file name)
  MDIR=$OUT/music
  rm -rf "$MDIR" "$OUT/music_modes.ini"; mkdir -p "$MDIR/Test Album"
  python3 -I - "$MDIR" "${EATRAX_FILL:-0}" <<'PY'
import math, struct, sys, wave
def tone(path, hz, seconds, rate=22050):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(b''.join(struct.pack('<h', int(6000 * math.sin(2 * math.pi * hz * k / rate))) for k in range(int(rate * seconds))))
d = sys.argv[1] + '/Test Album/'
tone(d + '01 Synthetic Band - First Tone.wav', 440.0, 30.0)
tone(d + '02 Synthetic Band - Second Tone.wav', 330.0, 20.0)
tone(d + '03 An Artist Name Long Enough To Test The Rows - A Title Much Longer Than Any Of The Game\'s Own.wav', 550.0, 10.0)
tone(d + '04 \u03a9mega \u00dcn\u00efcode - Caf\u00e9 \u00d1o\u00f1o \u266a.wav', 660.0, 10.0)
import os
if int(sys.argv[2]): os.makedirs(d + 'zz Fill', exist_ok=True)
for k in range(int(sys.argv[2])):   # (after the four by path)
    tone(d + 'zz Fill/%03d Fill Band - Fill Song %03d.wav' % (k + 5, k + 5), 220.0 + k, 0.5)
PY
  export B3NATIVE_MUSIC_DIR="$MDIR" B3NATIVE_MUSIC_MODE="${MUSIC_MODE:-2}" B3NATIVE_MUSIC_MODES_FILE="$OUT/music_modes.ini"
  export B3NATIVE_MUSIC_LOUDNESS_FILE="$OUT/music_loudness.txt"   # musicloud
  rm -f "$OUT/music_loudness.txt"
fi
# SCENARIO=nowplaying (nowplaying1, remaster-features §6.2; tools/native/now_playing_popup.h): the race scenario with
# OTHER MUSIC APPS on and a file in place of the system's reader ($OUT/now_playing.txt: a song, latched from VBlank
# NP_AT (default 16900: the main menu, the popup in SHOTS=8600), then a second one -- unicode, no album -- written once
# the log shows the first popup). The log must show both popups given to the game's EA TRAX display (SHOTS for them: the
# log's "popup:" VBlank / 2 = the poll; the text fades in once the panel has slid in)
NP_WRITER=""
if [ "${SCENARIO:-race}" = nowplaying ]; then
  NPF=$OUT/now_playing.txt
  rm -f "$OUT/native_play.log"   # the writer below waits for this run's first popup
  printf 'status=playing\ntitle=First Song\nartist=Some Band\nalbum=Some Album\napp=Test Player\n' > "$NPF"
  ( for _ in $(seq 600); do grep -q 'now_playing: VBlank [0-9]*: popup: "First Song"' "$OUT/native_play.log" 2>/dev/null && break; sleep 1; done
    printf 'status=playing\ntitle=Caf\303\251 \342\200\224 Second Song\nartist=Another Band\napp=Test Player\n' > "$NPF.tmp" && mv "$NPF.tmp" "$NPF" ) &
  NP_WRITER=$!
  ENVS+=(B3NATIVE_NOW_PLAYING=1 B3NATIVE_NOW_PLAYING_FILE="$NPF" B3NATIVE_NOW_PLAYING_AT="${NP_AT:-16900}")
fi
LOG=$OUT/native_play.log
# (a 10 GB address-space cap where prlimit exists, Linux; macOS has none: tools/release/package_mac.sh's smoke test)
LIMIT=(prlimit --as=10737418240)
command -v prlimit > /dev/null 2>&1 || LIMIT=()
env "${ENVS[@]}" ${LIMIT[@]+"${LIMIT[@]}"} "$NP" "${ARGS[@]}" > "$LOG" 2>&1
RC=$?
[ -n "$NP_WRITER" ] && kill "$NP_WRITER" 2>/dev/null; [ -n "$NP_WRITER" ] && wait "$NP_WRITER" 2>/dev/null
why=""
[ "$RC" = 0 ] || why="native_play exit $RC"
grep -q '^crash:' "$LOG" && why="${why:+$why; }crash: $(grep -m1 '^crash: \(signal\|exception\|uncaught\)' "$LOG" | cut -c1-200)"
# nofallback1: a function with no native version (no fallback in this build, or STRICT=1) stopped the game
grep -q '^native_play: STOPPED:' "$LOG" && why="${why:+$why; }stopped: no native code for $(sed -n 's/^native_play: STOPPED: the game called \(.*\), which has no native code.*/\1/p' "$LOG" | head -1 | cut -c1-160)"
grep -q 'ended after .* virtual VBlanks\|VBlank limit' "$LOG" || why="${why:+$why; }no VBlank limit reached"
# vu0n (2026-11-10): VU0 fully native -- native_play's end-of-run line "native_play: vu0: N launches: G generated,
# I interpreted, U without a generated program; aot on|check|off, ...". Always: no launch without a generated program,
# no difference in B3_VU0_AOT=check; with STRICT=1 also: the line is there, and with aot on nothing was interpreted.
VU0=$(grep -m1 -o 'native_play: vu0: .*' "$LOG" | sed 's/^native_play: //')
if [ -n "$VU0" ]; then
  read -r V_N V_G V_I V_U <<< "$(echo "$VU0" | sed -E 's/^vu0: ([0-9]+) launches: ([0-9]+) generated, ([0-9]+) interpreted, ([0-9]+) without.*/\1 \2 \3 \4/')"
  V_D=$(echo "$VU0" | sed -nE 's/.* ([0-9]+) differences.*/\1/p')
  [ "${V_U:-x}" = 0 ] || why="${why:+$why; }vu0: ${V_U:-?} launches without a generated program"
  [ "${V_D:-0}" = 0 ] || why="${why:+$why; }vu0: $V_D differences between the generated programs and the interpreter"
  if [ "${STRICT:-0}" = 1 ] && echo "$VU0" | grep -q 'aot on' && [ "${V_I:-x}" != 0 ]; then why="${why:+$why; }vu0: ${V_I:-?} launches interpreted"; fi
  if [ "${STRICT:-0}" = 1 ] && [ "${V_N:-0}" = 0 ]; then why="${why:+$why; }vu0: no launch at all"; fi
elif [ "${STRICT:-0}" = 1 ]; then
  why="${why:+$why; }no vu0 line in the log (native_play: vu0: ...)"
fi
# the shots' SHA-256 (the race shots first; compare runs of two builds with these)
for f in "$OUT"/shots/live_*_0.png; do
  [ -f "$f" ] && echo "race_check: shot $(basename "$f") $({ sha256sum "$f" 2>/dev/null || shasum -a 256 "$f"; } | cut -c1-64)"
done | sort -t_ -k2,2n
GOPT=""
if [ "${SCENARIO:-race}" = party ]; then   # party1: each button acted
  W=$B3NATIVE_GAME_OPTIONS
  if [ $(( W >> 7 & 3 )) != 0 ]; then grep -q 'party_modes: MOON JUMP: player 1.s car lifted' "$LOG" || why="${why:+$why; }MOON JUMP did not lift the car"; fi
  if [ $(( W >> 9 & 1 )) = 1 ]; then grep -q 'party_modes: CRASH ON DEMAND: player 1.s car crashed' "$LOG" || why="${why:+$why; }no crash on demand"; fi
  if [ $(( W >> 10 & 1 )) = 1 ]; then
    grep -q 'party_modes: AI DRIVER: player 1.s car driven by the AI' "$LOG" || why="${why:+$why; }the AI never took the car"
    if [ -n "${B3_RACEOPTS_TEST_FINISH:-}" ]; then
      grep -q 'party_modes: AI DRIVER: player 1.s car has finished: it stays the AI.s' "$LOG" || why="${why:+$why; }a finished car's AI DRIVER press not refused"
      ! grep -q 'party_modes: AI DRIVER: player 1.s car back to the player' "$LOG" || why="${why:+$why; }a car came back to the player"
    else
      grep -q 'party_modes: AI DRIVER: player 1.s car back to the player' "$LOG" || why="${why:+$why; }the car never came back"
    fi
  fi
  GOPT="; $(grep -m1 -o "native_play: party_modes: .*" "$LOG" | sed "s/^native_play: //")"
fi
if [ "${SCENARIO:-race}" = crashbreaker ] || [ "${SCENARIO:-race}" = mirror ]; then   # gameopts (game_options.h)
  want="MIRROR MODE"; [ "$SCENARIO" = crashbreaker ] && want="CRASHBREAKER IN EVERY MODE"
  [ "${CB_PLAIN:-0}" = 1 ] || grep -q "native_play: VBlank [0-9]*: game_options: $want\( (\(traffic\|Full\), after [0-9] vehicles*)\)*\( + PARTY MODES ([^)]*)\)* (records kept apart)" "$LOG" || why="${why:+$why; }the race did not take $want"
  FIRED=$(grep -c 'game_options: Crashbreaker fired outside Crash mode' "$LOG")
  ARMED=$(grep -c 'game_options: Crashbreaker armed outside Crash mode' "$LOG")
  if [ "$SCENARIO" = crashbreaker ] && [ "${CB_PLAIN:-0}" = 1 ]; then
    [ "$FIRED$ARMED" = 00 ] || why="${why:+$why; }a Crashbreaker without its option"
  elif [ "$SCENARIO" = crashbreaker ]; then
    [ "$FIRED" -ge 1 ] || why="${why:+$why; }no Crashbreaker fired (armed: $ARMED)"
    GOPT="; Crashbreaker armed $ARMED, fired $FIRED"
  else
    [ "$FIRED$ARMED" = 00 ] || why="${why:+$why; }a Crashbreaker without its option"
    GOPT="; MIRROR MODE latched ($(grep -m1 -o 'VBlank [0-9]*: game_options: MIRROR MODE' "$LOG"))"
    if [ "${MIRROR_INI:-0}" = 1 ]; then   # gameoptsfix: from the settings file; a PARTY MODES row there must act too
      GOPT="$GOPT, from the settings file"
      case "${MIRROR_INI_ROWS:-}" in *party_kamikaze_traffic*true*)
        grep -q 'game_options: MIRROR MODE + PARTY MODES ([^)]*KAMIKAZE' "$LOG" || why="${why:+$why; }the race did not latch KAMIKAZE TRAFFIC"
        KC=$(grep -o 'kamikaze traffic calls [0-9]*' "$LOG" | tail -1 | awk '{print $4}')
        [ "${KC:-0}" -gt 0 ] || why="${why:+$why; }KAMIKAZE TRAFFIC never acted (calls ${KC:-none})"
        GOPT="$GOPT; KAMIKAZE TRAFFIC calls $KC" ;;
      esac
    fi
  fi
fi
RUMB=""
if [ "${RUMBLE:-0}" = 1 ]; then   # rumble1: acknowledging from the start, the requests counted at the end
  grep -q 'native_play: VBlank [0-9]*: rumble on' "$LOG" || why="${why:+$why; }rumble not on"
  RL=$(grep -m1 -o 'native_play: rumble: [0-9]* actuator requests acknowledged, [0-9]* found the channel busy' "$LOG")
  [ -n "$RL" ] || why="${why:+$why; }no rumble line at the end"
  PEAK=$(grep -o 'platform: rumble: VBlank [0-9]* cycles [0-9]* port [12] small [01] large [0-9]*' "$LOG" | awk '{s[$8] += $10; if ($12 > l[$8]) l[$8] = $12} END {for (p in l) printf "%sport %s large peak %d, small on in %d requests", (n++ ? "; " : ""), p, l[p], s[p]}')
  RUMB="; rumble: ${RL#native_play: rumble: }${PEAK:+ ($PEAK)}"
fi
if [ -n "$FE" ]; then   # fe1: the front-end tours' verdict (no race needed)
  if [ "$FE" = nfsdemo ]; then   # the run ends at the hand-over, not at the VBlank limit
    why=${why//no VBlank limit reached/}; why=${why#; }; why=${why%; }; why=${why//; ; /; }
    grep -q 'platform: LoadExecPS2 "cdrom0:\\NFSUNDER\\NFSUNDER.ELF;1"' "$LOG" || why="${why:+$why; }no LoadExecPS2 of the demo"
    grep -q 'native_play: main: program ended (exit(0))' "$LOG" || why="${why:+$why; }the program did not end with exit(0)"
  fi
  why=${why//vu0: no launch at all/}; why=${why#; }; why=${why%; }   # (a front-end tour may launch no VU0 program)
  IFS=, read -r -a FS <<< "$FE_SHOTS"
  for q in "${FS[@]}"; do [ -f "$OUT/shots/live_${q}_0.png" ] || why="${why:+$why; }no shot $q"; done
  TL=$(grep -m1 -o ' [0-9]* track loads' "$LOG" | tr -dc 0-9)
  RACE=$(grep -cE 'b3live\] shot poll [0-9]+ .* race, (split screen, )?route .*[1-9][0-9]* vehicles' "$LOG")
  SPLIT=$(grep -c 'b3live\] shot poll [0-9]* .* race, split screen, route' "$LOG")
  case $FE in
  multiplayer)
    grep -q 'a second controller (port 2)' "$LOG" || why="${why:+$why; }no second controller"
    [ "${TL:-0}" = 1 ] || why="${why:+$why; }track loads ${TL:-0}, not 1"
    [ "$SPLIT" -ge 2 ] || why="${why:+$why; }split-screen race shots $SPLIT, not 2 or more" ;;
  trafficseries)   # trafficseries: SELECT CAR on TRAFFIC, a traffic vehicle's race with its records kept apart
    if [ "${CUSTOM:-0}" = 1 ]; then   # customseries: SELECT CAR on CUSTOM, an extra car's race (records apart, the donor's tuning)
    [ "${TL:-0}" -ge 1 ] || why="${why:+$why; }no track load"
    [ "$RACE" -ge 1 ] || why="${why:+$why; }no race shot"
    grep -Eq 'extra_cars: SELECT CAR shows (CUSTOM|BETA)' "$LOG" || why="${why:+$why; }SELECT CAR never on CUSTOM"
    grep -q 'extra_cars: an extra car.s race: records kept apart' "$LOG" || why="${why:+$why; }no extra car's race"
    grep -Eq 'extra_cars: ../Export/ValueDB/VehiclePhysics/X(CUS|BET)CAR[0-9]*.cfg registers as' "$LOG" || why="${why:+$why; }its tuning not its donor's"
    EATX="; $(grep -Eo 'SELECT CAR shows (CUSTOM|BETA), car [0-9]* \([^)]*\)' "$LOG" | tail -1), $(grep -m1 -Eo 'car 0 of the race: X(CUS|BET)/CAR[0-9]*, [^ ]*' "$LOG")"
    else
    [ "${TL:-0}" -ge 1 ] || why="${why:+$why; }no track load"
    [ "$RACE" -ge 1 ] || why="${why:+$why; }no race shot"
    grep -q 'traffic_cars: SELECT CAR shows TRAFFIC' "$LOG" || why="${why:+$why; }SELECT CAR never on TRAFFIC"
    grep -q 'traffic_cars: a traffic vehicle.s race: records kept apart' "$LOG" || why="${why:+$why; }no traffic vehicle's race"
    EATX="; $(grep -o 'SELECT CAR shows TRAFFIC[A-Z ]*, car [0-9]* ([A-Z0-9]*)' "$LOG" | tail -1), $(grep -m1 -o 'car 0 of the race: [A-Z0-9]* ([A-Z0-9]*.s' "$LOG")"
    fi ;;
  heavyseries)   # heavyseries: the series offered, SELECT CAR on it, a heavyweight race with its records kept apart
    [ "${TL:-0}" -ge 1 ] || why="${why:+$why; }no track load"
    [ "$RACE" -ge 1 ] || why="${why:+$why; }no race shot"
    grep -q 'heavy_series: SELECT CAR (mode [0-2]): HEAVYWEIGHTS offered' "$LOG" || why="${why:+$why; }the series not offered"
    grep -q 'heavy_series: SELECT CAR shows HEAVYWEIGHTS' "$LOG" || why="${why:+$why; }SELECT CAR never on HEAVYWEIGHTS"
    grep -q 'heavy_series: a heavyweight race: records kept apart' "$LOG" || why="${why:+$why; }no heavyweight race"
    EATX="; $(grep -m1 -o 'HEAVYWEIGHTS offered ([0-9]* of [0-9]* unlocked[^)]*)' "$LOG"), $(grep -m1 -o 'a heavyweight race: records kept apart (car [0-9a-f]*, mode [0-9]*, [0-9]* AI cars)' "$LOG")" ;;
  partycrash|carselect|crashnav|crashresults)   # (the garage showroom's backdrop counts as a load too)
    [ "${TL:-0}" -ge 1 ] || why="${why:+$why; }no track load"
    [ "$RACE" -ge 1 ] || why="${why:+$why; }no race shot" ;;
  paintsel)   # paintsel / paint2: the panel opened, the layers chosen and saved at Back (the reset variant: Square, FACTORY)
    [ "${TL:-0}" -ge 1 ] || why="${why:+$why; }no track load"
    if [ "${PAINTSEL_P2:-0}" = 1 ]; then
    [ "$SPLIT" -ge 1 ] || why="${why:+$why; }no split-screen race shot"
    grep -q 'native_play: VBlank [0-9]*: SELECT CAR takes input (player 2.s)' "$LOG" || why="${why:+$why; }player 2's SELECT CAR not seen"
    grep -q '\[b3settings\] car paint panel opened (player 2)' "$LOG" || why="${why:+$why; }player 2's panel did not open"
    grep -q '\[b3settings\] car paint panel closed (Back; player 2, [1-9][0-9]* edits): CAR PAINT Game, P2 CAR PAINT Custom, FINISH 0, saved' "$LOG" ||
      why="${why:+$why; }player 2's panel not closed by Back with P2 CAR PAINT Custom, saved"
    grep -q '^p2_car_paint = 1' "$OUT/pc_settings.ini" || why="${why:+$why; }the ini lacks player 2's custom paint"
    grep -q '^p2_wheel_paint = 1' "$OUT/pc_settings.ini" || why="${why:+$why; }the ini lacks player 2's wheel colour"
    grep -q "shot poll 14600: PAINT prompt drawn (player 2" "$LOG" || why="${why:+$why; }no PAINT prompt at 14600"
    grep -q "shot poll 16300: car paint panel drawn" "$LOG" || why="${why:+$why; }no panel at 16300"
    EATX="; player 2's car paint panel: opened, Pink and red wheels saved"
    else
    [ "$RACE" -ge 1 ] || why="${why:+$why; }no race shot"
    grep -q 'native_play: VBlank [0-9]*: SELECT CAR takes input (player 1.s)' "$LOG" || why="${why:+$why; }SELECT CAR not seen taking input"
    grep -q '\[b3pad\] car paint panel: a press (key (script)) on SELECT CAR' "$LOG" || why="${why:+$why; }no CAR PAINT PANEL press"
    grep -q '\[b3settings\] car paint panel opened (player 1)' "$LOG" || why="${why:+$why; }the panel did not open"
    grep -q '\[b3settings\] car paint panel: colour picker opened (player 1, layer 0)' "$LOG" || why="${why:+$why; }the picker did not open"
    grep -q '\[b3settings\] car paint panel: colour picker kept (player 1, layer 0' "$LOG" || why="${why:+$why; }the picker's colour not kept"
    grep -q '\[b3settings\] car paint panel closed (Back; player 1, [1-9][0-9]* edits): CAR PAINT Custom, P2 CAR PAINT Game, FINISH 0, saved' "$LOG" ||
      why="${why:+$why; }the panel not closed by Back with CAR PAINT Custom, saved"
    for k in decal_paint wheel_paint flame_paint; do
      if [ "${PAINTSEL_RESET:-0}" = 1 ]; then grep -q "^$k = 1" "$OUT/pc_settings.ini" && why="${why:+$why; }$k still custom after FACTORY COLOURS"
      else grep -q "^$k = 1" "$OUT/pc_settings.ini" || why="${why:+$why; }the ini lacks $k = 1"; fi
    done
    grep -q '^decal_red = 255' "$OUT/pc_settings.ini" && grep -q '^decal_green = 210' "$OUT/pc_settings.ini" || why="${why:+$why; }the decals not Yellow"
    grep -q "shot poll 15056: PAINT prompt drawn (player 1" "$LOG" || why="${why:+$why; }no PAINT prompt at 15056"
    if [ "${PAINTSEL_RESET:-0}" = 1 ]; then
      grep -q '\[b3settings\] SELECT CAR: the game.s colour changed (player 1): CAR PAINT back to Game (saved' "$LOG" ||
        why="${why:+$why; }CAR PAINT not set back to Game (and saved)"
      grep -q '\[b3settings\] car paint panel: factory colours (player 1)' "$LOG" || why="${why:+$why; }FACTORY COLOURS not chosen"
      grep -q '^car_paint = 1' "$OUT/pc_settings.ini" && why="${why:+$why; }the body still custom"
      EATX="; car paint panel: every layer, then Square (body Game) and FACTORY COLOURS (all Game)"
    else
      grep -q "shot poll 12456: car paint panel drawn" "$LOG" || why="${why:+$why; }no picker at 12456"
      EATX="; car paint panel: body Blue (picker), decals Yellow, wheels Red, boost glow Green, saved"
    fi
    fi ;;
  garagecam)   # garagecam1: the view taken by the stick (not before it) and by a drag, the spin back after each, no press
    [ "${TL:-0}" -ge 1 ] || why="${why:+$why; }no track load"
    [ "$RACE" -ge 1 ] || why="${why:+$why; }no race shot"
    GT=$(grep -m1 -o 'garagecam\] poll [0-9]*: the view taken (right stick)' "$LOG" | awk '{print $3}' | tr -dc 0-9)
    [ -n "$GT" ] || why="${why:+$why; }the right stick did not take the view"
    [ -n "$GT" ] && [ "$GT" -lt 10500 ] && why="${why:+$why; }the view taken at poll $GT, before the stick (VBlank 21100)"
    [ "${GARAGECAM_MOUSE:-1}" = 1 ] && ! grep -q 'garagecam\] poll [0-9]*: the view taken (mouse)' "$LOG" && why="${why:+$why; }the mouse drag did not take the view"
    GR=$(grep -c 'garagecam\] poll [0-9]*: the stick at rest .* the game.s spin resumes' "$LOG")
    [ "$GR" -ge 2 ] || why="${why:+$why; }the spin resumed $GR times, not 2 or more"
    grep -q 'menu mouse press' "$LOG" && why="${why:+$why; }the drag pressed a button"
    # the share of pixels two shots differ in (held: rounding only, under 1 %; turning: most of the car and the room)
    pxdiff() {
      python3 -I - "$OUT/shots/live_$1_0.png" "$OUT/shots/live_$2_0.png" <<'PY'
import struct, sys, zlib
def png(p):
    b = open(p, 'rb').read(); i = 8; idat = b''
    while i < len(b):
        n = struct.unpack('>I', b[i:i + 4])[0]; t = b[i + 4:i + 8]; d = b[i + 8:i + 8 + n]
        if t == b'IHDR': w, h, _, ct = struct.unpack('>IIBB', d[:10])
        if t == b'IDAT': idat += d
        i += 12 + n
    raw = zlib.decompress(idat); bpp = {2: 3, 6: 4}[ct]; st = w * bpp; out = []; prev = bytearray(st); q = 0
    for _ in range(h):
        f = raw[q]; ln = bytearray(raw[q + 1:q + 1 + st]); q += 1 + st
        for x in range(st):
            a = ln[x - bpp] if x >= bpp else 0; c = prev[x - bpp] if x >= bpp else 0; u = prev[x]
            if f == 1: ln[x] = (ln[x] + a) & 255
            elif f == 2: ln[x] = (ln[x] + u) & 255
            elif f == 3: ln[x] = (ln[x] + (a + u) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(u - c), abs(a - c), abs(a + u - 2 * c)
                ln[x] = (ln[x] + (a if pa <= pb and pa <= pc else u if pb <= pc else c)) & 255
        out.append(bytes(ln[k] for k in range(st) if k % bpp < 3)); prev = ln
    return out
try:
    A, B = png(sys.argv[1]), png(sys.argv[2])
    n = sum(1 for ra, rb in zip(A, B) for k in range(0, len(ra), 3) if ra[k:k + 3] != rb[k:k + 3])
    print(int(1000 * n / (len(A) * len(A[0]) // 3)))
except Exception:
    print(-1)
PY
    }
    D_HELD=$(pxdiff 10690 10700); D_BACK=$(pxdiff 10740 10780); D_SPIN=$(pxdiff 10560 10600)
    { [ "$D_HELD" -ge 0 ] && [ "$D_HELD" -lt 10 ]; } || why="${why:+$why; }the view moved at rest within the second (10690 / 10700: ${D_HELD} per mille differ)"
    [ "$D_BACK" -ge 100 ] || why="${why:+$why; }the spin did not resume (10740 / 10780: ${D_BACK} per mille differ)"
    [ "$D_SPIN" -ge 100 ] || why="${why:+$why; }no spin before the stick (10560 / 10600: ${D_SPIN} per mille differ)"
    EATX="; the view taken at poll $GT, the spin resumed $GR times; shots differ (per mille): spinning $D_SPIN, held $D_HELD, resumed $D_BACK" ;;
  racetime)   # todrace: held back (1.3.x): no label, the presses ignored, the races by day; the row (B3_RACE_TIME_ROW=1):
    # Night on SELECT OPTIONS, the race at night, the main menu's reset, the next race by day -- no label either way
    [ "${TL:-0}" -ge 2 ] || why="${why:+$why; }track loads ${TL:-0}, not 2"
    [ "$RACE" -ge 4 ] || why="${why:+$why; }race shots $RACE, not 4"
    TP=$(grep -c '\[b3pad\] time of day: a press' "$LOG")
    [ "$TP" = 0 ] || why="${why:+$why; }$TP time of day presses taken, not 0"
    TL_N=$(grep -c 'time of day label drawn' "$LOG")
    [ "$TL_N" = 0 ] || why="${why:+$why; }$TL_N time of day labels drawn, not 0"
    TSEQ=$(grep -o 'native_play: VBlank [0-9]*: time of day: .*' "$LOG" | sed 's/^native_play: VBlank [0-9]*: time of day: //' |
           tr '\n' '|')
    RTR=${RACETIME_ROWS:-}; [ -z "$RTR" ] && [ "${B3_RACE_TIME_ROW:-0}" = 1 ] && RTR=night
    if [ -n "$RTR" ]; then
      # (the page's values are reset on the way back -- SELECT MODE -- or on the main menu, whichever comes first)
      # (a replay played: the choice is the recording's, "the replay's")
      ROW="(SELECT OPTIONS' row|the replay's)"
      case "$RTR" in
        night) WANT=Night; STEPS="Night \\($ROW\\)\\|" ;;
        rain)  WANT=Rain; STEPS="Rain \\($ROW\\)\\|" ;;
        both)  WANT="Night rain"; STEPS="Night \\($ROW\\)\\|Night rain \\($ROW\\)\\|" ;;
      esac
      echo "$TSEQ" | grep -qE "^${STEPS}the race takes ${WANT}\|Default \((reset with SELECT OPTIONS|the main menu: back to the setting|the replay's)\)\|the race takes Default\|$" ||
        why="${why:+$why; }the choice's steps '${TSEQ}' (wanted ${WANT} taken by the race, then Default)"
      for q in 10700 11500; do
        case "$RTR" in rain) grep -q "shot poll $q: drawn at night" "$LOG" && why="${why:+$why; }race shot $q at night" ;;
          *) grep -q "shot poll $q: drawn at night (the race's own choice)" "$LOG" || why="${why:+$why; }race shot $q not at night" ;; esac
        case "$RTR" in night) grep -q "shot poll $q: drawn in the rain" "$LOG" && why="${why:+$why; }race shot $q in the rain" ;;
          *) grep -q "shot poll $q: drawn in the rain (the race's own choice)" "$LOG" || why="${why:+$why; }race shot $q not in the rain" ;; esac
      done
      EATX="; SELECT OPTIONS' rows ($RTR): $WANT taken by the race, Default after leaving the page, no label"
    else
      [ -z "$TSEQ" ] || why="${why:+$why; }the choice moved while held back: '${TSEQ}'"
      for q in 10700 11500; do grep -q "shot poll $q: drawn at night" "$LOG" && why="${why:+$why; }race shot $q at night"; done
      EATX="; time of day held back: no label, the presses ignored, the races by day"
    fi
    for q in 18300 18900; do
      grep -q "shot poll $q: drawn at night" "$LOG" && why="${why:+$why; }race shot $q at night"
      grep -q "shot poll $q: drawn in the rain" "$LOG" && why="${why:+$why; }race shot $q in the rain"
    done
    grep -q 'shot poll 17300 .* front end' "$LOG" || why="${why:+$why; }17300 not the front end's showroom"
    grep -q 'keys: driving' "$LOG" || why="${why:+$why; }never driving" ;;
  retail)   # extras: with every EXTRAS row off the front end is the game's: no ELIMINATOR, SELECT OPTIONS its own, no extra series
    grep -q '^native_play: extras 00000000 ' "$LOG" || why="${why:+$why; }extras not all off"
    grep -q 'single_eliminator: SELECT MODE shown with ELIMINATOR' "$LOG" && why="${why:+$why; }SELECT MODE had ELIMINATOR"
    # (the golden runs leave the rows out altogether, B3NATIVE_RACE_OPTS=0: then the page is the game's without them)
    if grep -q 'race_options: SELECT OPTIONS rows ' "$LOG"; then
      grep -q 'race_options: SELECT OPTIONS (mode 0) as the game.s own' "$LOG" || why="${why:+$why; }SELECT OPTIONS not the game's own"
    fi
    grep -q 'race_options: SELECT OPTIONS (mode [0-9]) with' "$LOG" && why="${why:+$why; }SELECT OPTIONS had rows of ours"
    grep -q 'heavy_series: SELECT CAR shows\|traffic_series: SELECT CAR' "$LOG" && why="${why:+$why; }an extra series on SELECT CAR"
    SMAX=$(grep -o 'car series: series [0-9]*' "$LOG" | awk '{print $4}' | sort -n | tail -1)
    [ -n "$SMAX" ] && [ "$SMAX" -le 5 ] || why="${why:+$why; }SELECT CAR's last series ${SMAX:-none}, not the game's (5 at most)"
    EATX="; the game's own front end: SELECT MODE 4 rows, SELECT OPTIONS its own, SELECT CAR series 0..$SMAX" ;;
  carseries)   # carsel1: three shoulder presses translated, the series after each (and the D-pad's Right after them)
    [ "${TL:-0}" -ge 1 ] || why="${why:+$why; }no track load"
    [ "$RACE" -ge 1 ] || why="${why:+$why; }no race shot"
    grep -q 'native_play: VBlank [0-9]*: car series on L1 / R1 on' "$LOG" || why="${why:+$why; }the row not on"
    SEQ=$(grep -o 'car series: port 1 [LR]1 given to the game as [A-Za-z]*' "$LOG" | awk '{print $5 ">" $11}' | tr '\n' ' ')
    [ "$SEQ" = "R1>Right R1>Right L1>Left " ] || why="${why:+$why; }shoulder presses translated: '${SEQ}', not R1 R1 L1"
    SER=$(grep -o 'car series: series [0-9]* car [0-9]*' "$LOG" | awk '{print $4}' | uniq | tr '\n' ' ')
    # (the page opens on the profile's last series s: then s+1, s+2 (R1 R1), s+1 (L1), s+2 (Right); the 100 % card: 0)
    echo "$SER" | awk '{ok = 0; for (i = 1; i + 4 <= NF; ++i) if ($(i+1) == $i + 1 && $(i+2) == $i + 2 && $(i+3) == $i + 1 && $(i+4) == $i + 2) ok = 1; exit !ok}' ||
      why="${why:+$why; }the carousel's series '${SER}', not s s+1 s+2 s+1 s+2"
    grep -q '\[prompts\] car series: .* L1 / R1 drawn beside the series arrows' "$LOG" || why="${why:+$why; }no glyphs drawn" ;;
  online)
    grep -q 'string_log: VBlank [0-9]*: string 105 ' "$LOG" || why="${why:+$why; }the no-adapter message (string 105) not shown"
    [ "${B3NATIVE_ONLINE_MESSAGE:-1}" = 0 ] || grep -q "online_message: string 105 (no network adapter) answered with the port's text" "$LOG" ||
      why="${why:+$why; }the ONLINE dialog not given the port's text"
    grep -q 'native_play: VBlank [0-9]*: SETTINGS opened' "$LOG" || why="${why:+$why; }SETTINGS not opened"
    grep -q 'PC SETTINGS closed' "$LOG" || why="${why:+$why; }SETTINGS not closed" ;;
  ddrecords)
    grep -q '\[save\] profile [0-9]* saved to' "$LOG" || why="${why:+$why; }SAVE PROFILE wrote nothing" ;;
  eatrax)   # eatrax1: the list, a custom row previewed, the modes kept on confirm and put back on Back
    NL=$(grep -m1 -o 'custom_music: VBlank [0-9]*: EA TRAX lists [0-9]* songs' "$LOG" | awk '{print $7}')
    WANT=$((44 + 4 + ${EATRAX_FILL:-0})); [ "$WANT" -gt 244 ] && WANT=244
    [ "${NL:-0}" = "$WANT" ] || why="${why:+$why; }EA TRAX lists ${NL:-no} songs, not $WANT"
    grep -q 'custom_music: VBlank [0-9]*: song 44 is custom 1 of' "$LOG" || why="${why:+$why; }row 45 not previewed as custom 1"
    grep -q 'custom_music: VBlank [0-9]*: stream [0-9A-F]* plays .*First Tone' "$LOG" || why="${why:+$why; }the first custom song not played"
    grep -q 'custom_music: VBlank [0-9]*: the custom songs. modes saved' "$LOG" || why="${why:+$why; }the modes not saved"
    grep -q '^races=Test Album/01 Synthetic Band - First Tone.wav$' "$OUT/music_modes.ini" 2>/dev/null || why="${why:+$why; }no races line for row 45"
    grep -q '^off=Test Album/04 ' "$OUT/music_modes.ini" 2>/dev/null || why="${why:+$why; }no off line for row 48"
    grep -q 'custom_music: VBlank [0-9]*: the custom songs. modes put back' "$LOG" || why="${why:+$why; }Back did not put the modes back"
    EATX="; $(grep -m1 -o 'EA TRAX lists [0-9]* songs' "$LOG"), modes saved and put back" ;;
  esac
  FB=$(grep -m1 -o 'fallback: [0-9]* functions ran retail in the mini ([0-9]* calls)\|fallback: none (compiled out)' "$LOG")   # nofallback1: the default build's line
  if [ -z "$why" ]; then
    echo "race_check: PASS front-end tour $FE: ${#FS[@]} pages shot, $RACE race shots, ${TL:-0} track loads${EATX:-}${FB:+; $FB}$RUMB${VU0:+; $VU0} (log $LOG)"
    exit 0
  fi
  echo "race_check: FAIL $why (log $LOG)"; exit 1
fi
if [ "${SCENARIO:-race}" = profilename ]; then   # mac5: no race in this one
  why=${why//vu0: no launch at all/}; why=${why#; }; why=${why%; }   # (the front end only: no VU0 program, as the tours)
  grep -q 'the on-screen keyboard takes typed text' "$LOG" || why="${why:+$why; }the name keyboard did not come up"
  grep -q 'menu mouse: .* 8 characters typed' "$LOG" || why="${why:+$why; }not 8 characters typed ($(grep -o '[0-9]* characters typed' "$LOG"))"
  # (a fresh card: written to its image; save1: the profile's .sav written back)
  grep -q "\[save-card\] write pages\|\[save\] profile [0-9]* saved to" "$LOG" || why="${why:+$why; }nothing saved to the card"
  if [ -z "$why" ]; then echo "race_check: PASS profile created on a fresh card by mouse and typing${VU0:+; $VU0} (log $LOG)"; exit 0; fi
  echo "race_check: FAIL $why (log $LOG)"; exit 1
fi
grep -q ' 1 track loads' "$LOG" || why="${why:+$why; }no track load"
RACE=$(grep -cE 'b3live\] shot poll (10700|11500) .* race, route .*[1-9][0-9]* vehicles' "$LOG")   # mac5: the two race shots only (SHOTS adds others)
# morerivalselim: its race starts ~2600 VBlanks later (RACE's page, Back, ELIMINATOR's page): its own shots (MR_SHOTS)
[ "${SCENARIO:-race}" = morerivalselim ] && RACE=$(grep -cE 'b3live\] shot poll (12000|12400) .* race, route .*[1-9][0-9]* vehicles' "$LOG")
[ "$RACE" = 2 ] || why="${why:+$why; }race shots $RACE of 2"
RATE=$(grep -E 'native_play: VBlank [0-9]+:' "$LOG" | tail -3 | awk '{s+=$4} END {if (NR) printf "%.1f", s/NR}')
ROUTE=$(grep -m1 -o 'race, route [^,]*' "$LOG" | sed 's|.*TRACKS/||')
FB=$(grep -m1 -o 'fallback: [0-9]* functions ran retail in the mini ([0-9]* calls)\|fallback: none (compiled out)' "$LOG")   # nofallback1: the default build's line
if [ -n "${SKIP_MOVIE_AT:-}" ] && ! grep -q 'movie skipped' "$LOG"; then why="${why:+$why; }no movie skipped at $SKIP_MOVIE_AT"; fi
if [ "${SCENARIO:-race}" = pausemouseq ]; then   # mac5: QUIT clicked, the popup's YES hovered (direct) and clicked
  grep -q 'a message box at 01E898E8' "$LOG" || why="${why:+$why; }the confirmation popup was not a mouse target"
  grep -q 'menu_mouse: VBlank [0-9]*: record [0-9]*: 1 lists \[01E898E8 row 0 of 2' "$LOG" || why="${why:+$why; }YES not hovered in the popup"
  [ "$(grep -c 'menu mouse press' "$LOG")" -ge 2 ] || why="${why:+$why; }fewer than two clicks reached the game"
  grep -qE 'menu mouse press 0000(0040|0010)' "$LOG" && why="${why:+$why; }stepping presses (Down / Up) were used for a hover"
fi
if [ "${SCENARIO:-race}" = pausemouse ]; then   # mac5: the mouse opened SETTINGS from the pause menu, closed it, resumed
  grep -q 'native_play: VBlank [0-9]*: SETTINGS opened' "$LOG" || why="${why:+$why; }SETTINGS not opened by the mouse"
  grep -q 'PC SETTINGS closed' "$LOG" || why="${why:+$why; }SETTINGS not closed by the right click"
  grep -E 'native_play: VBlank [0-9]+: keys: driving' "$LOG" | awk -F'[ :]+' '$3 > 23310 {f=1} END {exit !f}' || why="${why:+$why; }the race did not resume (START prompt click)"
fi
if [ "${SCENARIO:-race}" = restart ]; then   # nextstart: the question box, APPLY AND RESTART, the game started again
  grep -q 'PC SETTINGS: settings for the next start changed (ANTI-ALIASING' "$LOG" || why="${why:+$why; }no question box for ANTI-ALIASING"
  grep -q 'PC SETTINGS: the next-start settings: applied: the game starts again' "$LOG" || why="${why:+$why; }APPLY AND RESTART not chosen"
  grep -q 'native_play: VBlank [0-9]*: APPLY AND RESTART: the game ends' "$LOG" || why="${why:+$why; }native_play did not take the restart"
  grep -q 'native_play: restart: the game started again' "$LOG" || why="${why:+$why; }the game not started again"
  [ "$(grep -c '^native_play: started ' "$LOG")" -ge 2 ] || why="${why:+$why; }no second run in the log"
fi
if [ "${SCENARIO:-race}" = eliminator ]; then   # elim1 (tools/native/single_eliminator.h): the row, the logic, a car out, no records
  grep -q 'single_eliminator: SELECT MODE: ELIMINATOR' "$LOG" || why="${why:+$why; }ELIMINATOR not confirmed on SELECT MODE"
  grep -q 'single_eliminator: the race is an ELIMINATOR: logic 0051B2F0' "$LOG" || why="${why:+$why; }the race did not get the Eliminator's logic"
  grep -q 'single_eliminator: lap [0-9]*: car [0-9]* (.*) is out' "$LOG" || why="${why:+$why; }no car eliminated"
  grep -q 'single_eliminator: results: the record kinds' "$LOG" || why="${why:+$why; }the results did not reach the records"
elif grep -q 'single_eliminator: the race is an ELIMINATOR' "$LOG" && ! { [ "${SCENARIO:-race}" = raceopts ] && [ "${RACEOPTS_MODE:-0}" = 3 ]; } &&
     [ "${SCENARIO:-race}" != morerivalselim ]; then
  # every other mode stays the game's own (raceopts with RACEOPTS_MODE=3 is an Eliminator with race options)
  why="${why:+$why; }an Eliminator race in scenario ${SCENARIO:-race}"
fi
if [ "${SCENARIO:-race}" = photo ] || [ "${SCENARIO:-race}" = photodof ] || [ "${SCENARIO:-race}" = photorow ]; then   # photo mode: entered and left from the pause menu, the scene photographed
  IFS=, read -r PA PB PC PD PE <<< "$PHOTO_SHOTS"
  grep -q 'native_play: VBlank [0-9]*: pause menu up' "$LOG" || why="${why:+$why; }the pause menu was not seen"
  if [ "${SCENARIO:-race}" = photorow ]; then
    grep -q '\[b3photo\] photo mode on (pause menu)' "$LOG" || why="${why:+$why; }photo mode not entered from the pause menu's row"
    grep -q 'PHOTO MODE row confirmed: photo mode on' "$LOG" || why="${why:+$why; }the PHOTO MODE row was not confirmed"
    grep -q '\[b3photo\] photo mode on (P)' "$LOG" && why="${why:+$why; }photo mode entered with P"
  else
    grep -q '\[b3photo\] photo mode on (P)' "$LOG" || why="${why:+$why; }photo mode not entered with P"
  fi
  grep -q '\[b3photo\] photo mode off (Escape)' "$LOG" || why="${why:+$why; }photo mode not left with Escape"
  for q in $PB $PC $PD; do
    grep -q "b3live\] shot poll $q .* race (photo mode), " "$LOG" || why="${why:+$why; }shot $q not from the free camera"
  done
  for q in $PA $PE; do
    grep -q "b3live\] shot poll $q .* race, route" "$LOG" || why="${why:+$why; }shot $q not the game's race picture"
  done
  S=$OUT/shots
  cmp -s "$S/live_${PA}_0.png" "$S/live_${PE}_0.png" || why="${why:+$why; }the picture after photo mode ($PE) differs from the one before ($PA)"
  cmp -s "$S/live_${PA}_0.png" "$S/live_${PB}_0.png" && why="${why:+$why; }photo mode's first picture ($PB) is the paused one (HUD not hidden?)"
  cmp -s "$S/live_${PB}_0.png" "$S/live_${PC}_0.png" && why="${why:+$why; }the free camera did not move ($PB = $PC)"
  DOF=""
  if [ "${SCENARIO:-race}" = photodof ]; then   # photo2: the depth of field on for shot B only
    IFS=, read -r P0 P1 <<< "$DOF_SHOTS"
    grep -q "\[b3photo\] shot poll $PB: depth of field" "$LOG" || why="${why:+$why; }shot $PB not drawn with the depth of field"
    for q in $P0 $P1; do
      grep -q "b3live\] shot poll $q .* race (photo mode), " "$LOG" || why="${why:+$why; }shot $q not from the free camera"
      grep -q "\[b3photo\] shot poll $q: depth of field" "$LOG" && why="${why:+$why; }shot $q drawn with the depth of field"
    done
    cmp -s "$S/live_${P0}_0.png" "$S/live_${P1}_0.png" || why="${why:+$why; }the depth of field off again ($P1) differs from before it was on ($P0)"
    cmp -s "$S/live_${P0}_0.png" "$S/live_${PB}_0.png" && why="${why:+$why; }the depth of field changed nothing ($PB = $P0)"
    if [ -n "${PHOTO_GOLDEN:-}" ]; then
      cmp -s "$PHOTO_GOLDEN/live_${PB}_0.png" "$S/live_${P0}_0.png" || why="${why:+$why; }shot $P0 differs from the photo scenario's $PB ($PHOTO_GOLDEN)"
      for q in $PA $PC $PD $PE; do
        cmp -s "$PHOTO_GOLDEN/live_${q}_0.png" "$S/live_${q}_0.png" || why="${why:+$why; }shot $q differs from the photo scenario's ($PHOTO_GOLDEN)"
      done
    fi
    DOF="; the depth of field: shot $PB blurred, $P0 = $P1 without it"
    [ -n "${PHOTO_GOLDEN:-}" ] && DOF="$DOF = shot $PB of the photo scenario, and $PA $PC $PD $PE equal to its own"
  fi
  if [ -z "$why" ]; then
    echo "race_check: PASS photo mode entered from the pause menu, the camera moved (shots $PB $PC $PD), left: shot $PE = shot $PA$DOF${VU0:+; $VU0} (log $LOG)"
    exit 0
  fi
fi
BOOST=""
if [ "${SCENARIO:-race}" = orbitcam ]; then   # orbitcam1: the stick and the mouse took the view, it came back after each
  if printf '%s' "${ORBIT_INI:-}" | grep -q 'orbit_camera *= *0'; then
    grep -q '\[orbitcam\] poll [0-9]*: player 1: the view taken' "$LOG" && why="${why:+$why; }the view was taken with ORBIT CAMERA off"
  elif [ "${PAD:-xbox}" = ps ]; then   # the PlayStation layout: the right stick is the game's (the throttle), the mouse still turns it
    grep -q '\[orbitcam\] poll [0-9]*: player 1: the view taken (right stick)' "$LOG" && why="${why:+$why; }the stick took the view on the PlayStation layout"
    grep -q '\[orbitcam\] poll [0-9]*: player 1: the view taken (mouse)' "$LOG" || why="${why:+$why; }the mouse did not take the view"
  else
    grep -q '\[orbitcam\] poll [0-9]*: player 1: the view taken (right stick)' "$LOG" || why="${why:+$why; }the right stick did not take the view"
    grep -q '\[orbitcam\] poll [0-9]*: player 1: the view taken (mouse)' "$LOG" || why="${why:+$why; }the mouse did not take the view"
    [ "$(grep -c '\[orbitcam\] poll [0-9]*: player 1: back to the game.s chase view' "$LOG")" -ge 3 ] || why="${why:+$why; }the view did not come back after each look (stick, mouse, stick)"
  fi
fi
if [ "${SCENARIO:-race}" = replaycams ]; then   # replaycams: each angle, a rival, the free camera, off after the replay
  grep -q 'replay_view: the replay starts' "$LOG" || why="${why:+$why; }the replay did not start"
  grep -q '\[replaycams\] poll [0-9]*: the replay plays' "$LOG" || why="${why:+$why; }the cameras never came on"
  for a in CHASE "FAR CHASE" BUMPER ORBIT TV; do
    grep -q "\[replaycams\] poll [0-9]*: $a, .* (car [0-9A-F]*, 1 of " "$LOG" || why="${why:+$why; }no $a on the player's car"
  done
  grep -q '\[replaycams\] poll [0-9]*: TV, .* (car [0-9A-F]*, 2 of ' "$LOG" || why="${why:+$why; }no rival's TV angle"
  grep -q '\[b3photo\] photo mode on (REPLAY FREE CAMERA)' "$LOG" || why="${why:+$why; }the free camera did not open"
  grep -q '\[b3photo\] photo mode off (I)' "$LOG" || why="${why:+$why; }the free camera did not close on its key"
  grep -q '\[replaycams\] poll [0-9]*: the replay stopped' "$LOG" || why="${why:+$why; }the cameras did not stop after the replay"
  grep -q '\[replaycams\] camera next (test pad)' "$LOG" || why="${why:+$why; }the pad's D-pad up did not change the camera in the replay"
  grep -q '\[replaycams\] camera previous (test pad)' "$LOG" && why="${why:+$why; }the pad's D-pad down after the replay went to the viewer"
  awk '$1 >= 25700 && $1 <= 25720' "$OUT/pads.txt" | grep -q . && why="${why:+$why; }the game got the D-pad in the replay"
  awk '$1 == 26050' "$OUT/pads.txt" | grep -q . || why="${why:+$why; }the game did not get the D-pad on the results page after the replay"
  GOPT="; $(grep -c '\[replaycams\] poll [0-9]*: [A-Z ]*, ' "$LOG") camera changes"
fi
if [ "$XROWS" = blinkers ]; then   # blinkers1: a racer's indicator before a bend (not with the row off)
  if printf '%s' "${BLINK_INI:-turn_signals = 1}" | grep -q 'turn_signals *= *0'; then
    grep -q '\[*turn_signals: a racer' "$LOG" && why="${why:+$why; }indicators with TURN SIGNALS off"
  else
    grep -q "turn_signals: a racer's indicator before a bend" "$LOG" || why="${why:+$why; }no racer's indicator"
  fi
fi
if [ "$XROWS" = rrparts ]; then   # rrparts1: the Road Rage taken, its records apart (not with the row off)
  if printf '%s' "${RR_INI:-road_rage_parts = 1}" | grep -q 'road_rage_parts *= *0'; then
    grep -q 'road_rage_parts: an offline Road Rage' "$LOG" && why="${why:+$why; }the Road Rage taken with ROAD RAGE PARTS off"
  else
    grep -q 'road_rage_parts: an offline Road Rage with every loose part let go' "$LOG" || why="${why:+$why; }the Road Rage not taken"
    grep -q 'records word [0-9A-F]* (options[a-z0-9-]*-loose-parts' "$LOG" || why="${why:+$why; }its records not kept apart (bit 25)"
  fi
fi
if [ "$XROWS" = trafficcheck ]; then   # checkfix: checks made; Extreme: no player crash on traffic or a wreck
  TCS=$(grep -o 'native_play: traffic_check: [0-9]* checks.*' "$LOG" | tail -1)
  [ -n "$TCS" ] || TCS="native_play: traffic_check: 0 checks (0 of them wrecks, 0 shunts), player crashes on traffic ?, on wrecks ?"
  grep -q 'traffic_check: TRAFFIC CHECKING in this race' "$LOG" || why="${why:+$why; }the option not taken"
  grep -q 'party_modes: AI DRIVER: player 1.s car driven by the AI' "$LOG" || why="${why:+$why; }the AI never drove"
  printf '%s' "$TCS" | grep -q 'traffic_check: [1-9][0-9]* checks' || why="${why:+$why; }no check"
  if [ "${TC_MODE:-2}" = 2 ]; then
    printf '%s' "$TCS" | grep -q 'player crashes on traffic 0, on wrecks 0' || why="${why:+$why; }the player crashed on traffic (Extreme): $(printf '%s' "$TCS" | grep -o 'player crashes.*')"
  fi
  GOPT="; $(printf '%s' "$TCS" | grep -o '[0-9]* checks ([^)]*)'), $(printf '%s' "$TCS" | grep -o '[0-9]* wrecks of checked cars. chains'), $(grep -c 'TRACE spared' "$LOG") own / chain touches spared, $(printf '%s' "$TCS" | grep -o 'player crashes.*')"
fi
if [ "${SCENARIO:-race}" = camroll ]; then   # camroll1: the chase camera leaned into the turn (not with the row off)
  if printf '%s' "${ROLL_INI:-camera_roll = 2}" | grep -q 'camera_roll *= *0'; then
    grep -q '\[camroll\]' "$LOG" && why="${why:+$why; }the camera rolled with CAMERA ROLL off"
  else
    grep -q '\[camroll\] poll [0-9]*: player 1: the chase camera leans into a corner' "$LOG" || why="${why:+$why; }the chase camera did not lean into the turns"
  fi
fi
if [ "${SCENARIO:-race}" = finishcam ]; then   # finishcam1: the set pieces from the finish (not with the row on Game)
  if printf '%s' "${FINISH_INI:-finish_camera = 1}" | grep -q 'finish_camera *= *0'; then
    grep -q '\[finishcam\] poll [0-9]*: set piece' "$LOG" && why="${why:+$why; }the set pieces ran with FINISH CAMERA on Game"
  else
    grep -q '\[finishcam\] VDB.XML: 36 of 36 set-piece shots' "$LOG" || why="${why:+$why; }the set-piece shots not read from VDB.XML"
    grep -q '\[finishcam\] poll [0-9]*: set piece 1 shot 1 ' "$LOG" || why="${why:+$why; }no set piece at the finish"
    grep -q '\[finishcam\] poll [0-9]*: set piece 2 shot 3 ' "$LOG" || why="${why:+$why; }the set pieces did not move on"
  fi
fi
if [ "${SCENARIO:-race}" = boosttoggle ]; then   # the latch held R1 between the two taps (the reports the game got)
  grep -q '\[b3pad\] boost toggle: latched' "$LOG" || why="${why:+$why; }no boost latch"
  grep -q '\[b3pad\] boost toggle: released' "$LOG" || why="${why:+$why; }the boost latch not released"
  HELD=$(python3 -I - "$OUT/pads.txt" <<'PY'
import sys
held, since, total = False, 0, 0
for line in open(sys.argv[1]).read().split('\n')[1:]:
    f = line.split()
    if len(f) < 2 or not f[0].isdigit():
        continue
    v, b1 = int(f[0]), int(f[1][2:4], 16)
    if held:
        total += v - since
    held, since = (b1 & 8) == 0, v
print(total)
PY
)
  [ "${HELD:-0}" -ge 1300 ] && [ "${HELD:-0}" -le 1500 ] || why="${why:+$why; }R1 held for ${HELD:-?} VBlanks, not ~1400"
  BOOST="; boost toggle: R1 held $HELD VBlanks from two 10-VBlank taps"
fi
PADL=""
if [ "${SCENARIO:-race}" = padlayout ]; then   # the reports the game read at each of the pad's presses
  grep -q "\[b3pad\] test pad: ${PAD:-xbox} " "$LOG" || why="${why:+$why; }no test pad"
  if [ "${PAD:-xbox}" = switch ]; then
    grep -q "\[b3controls\] .*(controller layout: player 1 nintendo," "$LOG" || why="${why:+$why; }no Nintendo layout in the log"
  fi
  PADV=$(python3 -I - "$OUT/pads.txt" "${PAD:-xbox}" <<'PY'
import sys
rows = []
for line in open(sys.argv[1]).read().split('\n')[1:]:
    f = line.split()
    if len(f) >= 2 and f[0].isdigit():
        rows.append((int(f[0]), bytes.fromhex(f[1])))
def at(v):
    r = None
    for w, b in rows:
        if w <= v: r = b
    return r
NAMES = ["SELECT", "L3", "R3", "START", "UP", "RIGHT", "DOWN", "LEFT", "L2", "R2", "L1", "R1", "TRIANGLE", "CIRCLE", "CROSS", "SQUARE"]
def held(v):
    b = at(v)
    if b is None: return set(), 0x7F
    w = ~(b[0] | b[1] << 8) & 0xFFFF
    return {NAMES[i] for i in range(16) if w >> i & 1}, b[3]
xbox = sys.argv[2] != 'ps'
nin = sys.argv[2] == 'switch'   # the Nintendo layout: A (East) and B (South) by label
A, B = ("A (East)", "B (South)") if nin else ("A", "B")
want = [(15505, {"CROSS"}, "menus: East (A) confirms" if nin else "menus: South confirms")]
if xbox:
    want += [(21300, {"R1"}, "driving: %s boosts (not Cross; the throttle on the right stick)" % A),
             (21455, {"R2"}, "driving: %s is the Crashbreaker" % B),
             (21805, {"TRIANGLE"}, "pause menu: %s goes back" % B)]
else:
    want += [(21300, {"R1", "CROSS"}, "driving: R1 boosts, Cross accelerates"),
             (21455, {"CIRCLE", "CROSS"}, "driving: Circle the horn"),
             (21505, {"R2", "CROSS"}, "driving: R3 is the Crashbreaker"),
             (21805, {"TRIANGLE"}, "pause menu: Triangle goes back")]
bad, ok = [], []
for v, w, what in want:
    h, ry = held(v)
    if h != w: bad.append("%s at %d: %s, not %s" % (what, v, " ".join(sorted(h)) or "nothing", " ".join(sorted(w))))
    else: ok.append("%s (%d: %s)" % (what, v, " ".join(sorted(h))))
h, ry = held(21300)
if xbox and not ry < 0x7F: bad.append("driving: no throttle from the right trigger at 21300 (right stick y %02X)" % ry)
print(("FAIL " + "; ".join(bad)) if bad else ("PASS " + "; ".join(ok) + ("; RT throttle %02X" % ry if xbox else "")))
PY
)
  case "$PADV" in PASS*) PADL="; controller layout ${PAD:-xbox}: ${PADV#PASS }" ;; *) why="${why:+$why; }pad layout: ${PADV#FAIL }" ;; esac
fi
MUSIC=""
if [ "${SCENARIO:-race}" = music ]; then   # a custom song decoded, played by the music stream, its popup, samples mixed
  grep -q 'custom_music: VBlank [0-9]*: stream [0-9A-F]* plays' "$LOG" || why="${why:+$why; }no custom song played"
  grep -q 'custom_music: VBlank [0-9]*: popup: "' "$LOG" || why="${why:+$why; }no custom popup"
  S=$(grep -m1 -o 'custom_music: [0-9]* custom songs started, [0-9]* popups, [0-9]* frames mixed ([0-9.]* s), peak [0-9]*' "$LOG")
  echo "$S" | grep -q ' [1-9][0-9]* frames mixed .*peak [1-9]' || why="${why:+$why; }no custom samples mixed ($S)"
  MUSIC="; ${S#custom_music: }"
fi
if [ "${SCENARIO:-race}" = crashtraffic ] && [ "${B3NATIVE_CRASH_RAM_FIX:-1}" != 0 ]; then   # crashramfix
  grep -q 'crash_ram_fix: VBlank [0-9]*: vehicle [0-9A-F]* ([0-9]* kg) rammed on its own' "$LOG" ||
    why="${why:+$why; }no vehicle without a trailer rammed in the Crash junction (crash_ram_fix)"
fi
ROPT=""
if [ "${SCENARIO:-race}" = raceopts ]; then   # raceopts: the page's options taken by the race
  W=${RACEOPTS_WANT:-t0 l1 r0 d0}
  # (RACEOPTS_WANT may give the code's first fields only: options3's longer codes after them)
  grep -q "native_play: VBlank [0-9]*: race options: the race takes $W[ a-z0-9]*: records kept apart" "$LOG" || why="${why:+$why; }the race did not take $W"
  SE=$(grep -m1 -o 'race options: a SINGLE EVENT stage (mode [0-9], [0-9]* AI cars, options [^)]*)' "$LOG")
  [ -n "$SE" ] || why="${why:+$why; }no Single Event stage logged"
  L=$(echo "$W" | sed 's/.* l\([0-9]*\) .*/\1/')
  if [ "$L" != 0 ]; then
    grep -q "race_options: the race's laps $L (the track's [0-9]*)" "$LOG" || why="${why:+$why; }the stage's laps not $L"
  fi
  case "$W" in t0*)   # Traffic Off: the race cars only (the player and the stage's AI cars)
    NC=$(( $(echo "$SE" | sed -n 's/.*, \([0-9]*\) AI cars,.*/\1/p') + 1 ))
    for q in 10700 11500; do
      grep -q "shot poll $q .* race, route .*, $NC vehicles" "$LOG" || why="${why:+$why; }traffic in race shot $q (Off: not $NC vehicles)"
    done ;;
  esac
  case "$W" in *m1*)   # gearbox: TRANSMISSION Manual: the player's car in manual; a SHIFT key press reached the game
    grep -q "gearbox: TRANSMISSION Manual: the player's car [0-9A-F]* in manual" "$LOG" || why="${why:+$why; }the player's car not in manual"
    if grep -q '\[b3pad\] shift up' "$LOG"; then
      grep -q 'gearbox: shift up (gear' "$LOG" || why="${why:+$why; }a SHIFT UP press did not reach the game"
    fi ;;
  esac
  case "$W" in *e1*)   # elim1: TIMED ELIMINATION On (RACEOPTS_MODE=3): the timer put the last car out at 24 s
    grep -q 'single_eliminator: TIMED ELIMINATION at 24.00 s of race time' "$LOG" || why="${why:+$why; }no timed elimination at 24 s"
    grep -q 'single_eliminator: timed elimination 1: car' "$LOG" || why="${why:+$why; }no car put out by the timer" ;;
  esac
  ROPT="; race options $W (${SE#race options: })$(grep -m1 -o "the race's laps [0-9]* (the track's [0-9]*)" "$LOG" | sed 's/^/; /')"
fi
if [ "${SCENARIO:-race}" = morerivalselim ]; then   # an Eliminator after RACE's RIVALS 7: the game's frame
  grep -q 'single_eliminator: the race is an ELIMINATOR' "$LOG" || why="${why:+$why; }no ELIMINATOR race"
  grep -q 'more_rivals: AI car [0-9]*: car id' "$LOG" && why="${why:+$why; }AI cars past the game's 5 in an Eliminator"
  ROPT="; an ELIMINATOR after RIVALS 7: $(grep -m1 -o 'more_rivals: an ELIMINATOR keeps[^(]*' "$LOG" || echo 'RIVALS reset by the page')"
fi
if [ "${SCENARIO:-race}" = morerivals ]; then   # MORE RIVALS: the cars, the records word, the grid
  R=${MR_RIVALS:-7}
  [ "$(grep -c 'more_rivals: AI car [0-9]*: car id' "$LOG")" = $((R - 5)) ] || why="${why:+$why; }not $((R - 5)) AI cars past the game's"
  grep -qE "records word [0-9A-F]* \(options(-[a-z0-9]+)*-rivals-$R\)" "$LOG" || why="${why:+$why; }records word not options-...-rivals-$R"
  G=$(grep -m1 'more_rivals: grid check' "$LOG")
  echo "$G" | grep -q ": PASS ($((R + 1)) cars)" || why="${why:+$why; }grid check: ${G:-none}"
  if [ "${MR_TRAFFIC_OFF:-0}" = 1 ]; then   # the renderer drew every race car (and no traffic)
    for q in 10700 11500; do
      grep -q "shot poll $q .* race, route .*, $((R + 1)) vehicles" "$LOG" || why="${why:+$why; }race shot $q: not $((R + 1)) vehicles ($(grep -o "shot poll $q [^)]*vehicles" "$LOG" | head -1))"
    done
  fi
  ROPT="; $((R + 1)) cars (RIVALS $R), ${G#more_rivals: }"
fi
NOWP=""
if [ "${SCENARIO:-race}" = nowplaying ]; then   # both songs' popups given to the display, in order
  grep -q 'now_playing: VBlank [0-9]*: popup: "First Song" / "Some Band" / "Some Album"' "$LOG" || why="${why:+$why; }no popup of the first song"
  grep -q 'now_playing: VBlank [0-9]*: popup: "Caf.* Second Song" / "Another Band" / "Test Player"' "$LOG" || why="${why:+$why; }no popup of the second song"
  NOWP="; other music apps: $(grep -c 'now_playing: VBlank [0-9]*: popup:' "$LOG") popups (VBlanks $(grep -o 'now_playing: VBlank [0-9]*: popup' "$LOG" | grep -o '[0-9]*' | paste -sd, -))"
fi
if [ -z "$why" ]; then
  echo "race_check: PASS race $ROUTE reached and driven to VBlank $VBL; last windows ${RATE} VBlanks/s; $FB$MUSIC$NOWP$ROPT$BOOST$PADL$RUMB$GOPT${VU0:+; $VU0} (log $LOG)"
  exit 0
fi
echo "race_check: FAIL $why (log $LOG)"
exit 1
