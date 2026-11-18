#!/bin/bash
# race_check.sh -- one native_play run through a race start and ~30 s of driving (rt1; part of every head's
# checks). Offscreen, unpaced, scripted keys; the Single Event path: title -> Load profile (the saves-100 card, a
# scratch copy) -> main menu -> Single Event -> Race -> Select options -> Select Car -> the race (accelerate held).
#
#   tools/native/race_check.sh [native_play] [out dir]
#     native_play   default build/native_play (libb3render.so beside it)
#     out dir       default build/race_check (log, shots, the scratch card)
#   Environment: B3_GAME_DIR (the disc folder or the disc image, default data/game), SAVES (the card to copy, default the perf
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
# SCENARIO=worldtour (mac5): World Tour (the main menu's first item) instead of Single Event, then Cross every 400 to
# 21600 (the globe, the event, its intro) and accelerate (the Mac crash of 2026-11-08 entering a World Tour race)
if [ "${SCENARIO:-race}" = worldtour ]; then
  K="9000:41:1,9010:41:0"
  for v in $(seq 15500 800 16300); do K="$K,$v:29:1,$((v+10)):29:0"; done
  for v in $(seq 17900 400 21600); do K="$K,$v:29:1,$((v+10)):29:0"; done
  K="$K,22000:29:1"
fi
# SCENARIO=timeattack / roadrage / crash (mac5): SINGLE EVENT > SELECT MODE (RACE, TIME ATTACK, ROAD RAGE, CRASH) with one / two / three
# Downs before the mode's Cross at 19100, then the same Crosses and the accelerator (the other modes under STRICT=1)
if [ "${SCENARIO:-race}" = roadrage ] || [ "${SCENARIO:-race}" = crash ] || [ "${SCENARIO:-race}" = timeattack ]; then
  n=2; [ "$SCENARIO" = crash ] && n=3; [ "$SCENARIO" = timeattack ] && n=1
  D=""; for k in $(seq 1 $n); do v=$((18700 + 100 * k)); D="$D,$v:81:1,$((v+10)):81:0"; done
  K="${K/,19100:29:1/$D,19100:29:1}"
  # crash mode: its intro takes a fresh Cross (Continue) while the accelerator is held: release / press again a few
  # times, and run longer (the junction, the crash, the score)
  if [ "$SCENARIO" = crash ]; then
    VBL=${VBLANKS:-27000}
    for v in 21000 21600 22200; do K="$K,$v:29:0,$((v+10)):29:1"; done
  fi
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
#   carseries     carsel1: the carselect path with PC SETTINGS > CAR SERIES ON L1 / R1 On ($OUT/pc_settings.ini
#                 series_shoulders = 1): on the car select R1 (R) twice, L1 (Q) once, then Right (the D-pad, as before), a
#                 car down, the race. The log must show each shoulder press given to the game as Right / Right / Left and
#                 the carousel's series following them (1, 2, 1, 2); shots of the page with the L1 / R1 glyphs
#   crashresults  SINGLE EVENT > CRASH: the junction, the crash, CRASH RESULTS, CRASH $ and back to SELECT MODE
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
  multiplayer|partycrash|online|ddrewards|ddrecords|ddsettings|eatrax|crashnav|carselect|carseries|crashresults|nfsdemo) FE=$SCENARIO ;;
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
  carselect)
    VBL=${VBLANKS:-27000}
    fe_keys 17900:$DN 18300:$X 19100:$X 19900:$X 20900:$RT 21300:$RT 21700:$DN 22100:$DN 22500:$SQ 22900:$SQ 23300:$UP \
      23700:$X 24500:$X 24600:$X:1
    FE_SHOTS=${FE_SHOTS:-9800,10150,10650,10900,11400,11650,11900,12400,13150}
    ;;
  carseries)   # carsel1: R = R1, Q = L1 (docs/mac.md §5); the row on through the renderer's --settings
    VBL=${VBLANKS:-27000}
    printf 'series_shoulders = 1\n%s' "${CARSERIES_INI:-}" > "$OUT/pc_settings.ini"   # (CARSERIES_INI: more rows, e.g. button_prompts = 2)
    RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
    fe_keys 17900:$DN 18300:$X 19100:$X 19900:$X 20900:21 21300:21 21700:20 22100:$RT 22500:$DN 23300:$UP \
      23700:$X 24500:$X 24600:$X:1
    FE_SHOTS=${FE_SHOTS:-10150,10650,10850,11050,11250,11400,13150}
    ;;
  crashresults)
    VBL=${VBLANKS:-33000}
    fe_keys 17900:$DN 18300:$X 18800:$DN 18900:$DN 19000:$DN 19100:$X 19900:$X 20400:$X:1 21000:$X:0 21010:$X:1 \
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
# at RENDER_EXTRA's size (1280 x 720 or any 16:9). The log must show the presses, the page opened and closed, and the
# race driven again.
if [ "${SCENARIO:-race}" = pausemouse ]; then
  VBL=${VBLANKS:-24000}
  K="$K,21600:41:1,21610:41:0,21700:29:0"
  MOUSE=${MOUSE:-21800:move:0.15:0.47,21900:wheel:-1,22000:move:0.15:0.60,22010:click:1,22400:move:0.5:0.5,22600:click:3,23300:move:0.5:0.93,23310:click:1}
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
# SCENARIO=boosttoggle (accessibility: PC SETTINGS > INTERFACE > ACCESSIBILITY > BOOST Toggle; opt-in, it changes the
# input): the race scenario with $OUT/pc_settings.ini `boost_mode = 1` (the renderer's --settings), Left Shift (R1, boost)
# tapped for 10 VBlanks at 21400 and again at 22800 (the driving context begins near 21070; a tap in the countdown is
# passed as it is). The log must show the latch taken and dropped, and the pad recording R1 held from the first tap to
# the second (~1400 VBlanks; Hold would give 2 x 10)
if [ "${SCENARIO:-race}" = boosttoggle ]; then
  printf 'boost_mode = 1\n' > "$OUT/pc_settings.ini"
  RENDER_EXTRA="${RENDER_EXTRA:+$RENDER_EXTRA }--settings $OUT/pc_settings.ini"
  K="$K,21400:225:1,21410:225:0,22800:225:1,22810:225:0"
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
grep -q '^crash:' "$LOG" && why="${why:+$why; }crash: $(grep -m1 '^crash: signal' "$LOG" | cut -c1-200)"
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
  partycrash|carselect|crashnav|crashresults)   # (the garage showroom's backdrop counts as a load too)
    [ "${TL:-0}" -ge 1 ] || why="${why:+$why; }no track load"
    [ "$RACE" -ge 1 ] || why="${why:+$why; }no race shot" ;;
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
MUSIC=""
if [ "${SCENARIO:-race}" = music ]; then   # a custom song decoded, played by the music stream, its popup, samples mixed
  grep -q 'custom_music: VBlank [0-9]*: stream [0-9A-F]* plays' "$LOG" || why="${why:+$why; }no custom song played"
  grep -q 'custom_music: VBlank [0-9]*: popup: "' "$LOG" || why="${why:+$why; }no custom popup"
  S=$(grep -m1 -o 'custom_music: [0-9]* custom songs started, [0-9]* popups, [0-9]* frames mixed ([0-9.]* s), peak [0-9]*' "$LOG")
  echo "$S" | grep -q ' [1-9][0-9]* frames mixed .*peak [1-9]' || why="${why:+$why; }no custom samples mixed ($S)"
  MUSIC="; ${S#custom_music: }"
fi
NOWP=""
if [ "${SCENARIO:-race}" = nowplaying ]; then   # both songs' popups given to the display, in order
  grep -q 'now_playing: VBlank [0-9]*: popup: "First Song" / "Some Band" / "Some Album"' "$LOG" || why="${why:+$why; }no popup of the first song"
  grep -q 'now_playing: VBlank [0-9]*: popup: "Caf.* Second Song" / "Another Band" / "Test Player"' "$LOG" || why="${why:+$why; }no popup of the second song"
  NOWP="; other music apps: $(grep -c 'now_playing: VBlank [0-9]*: popup:' "$LOG") popups (VBlanks $(grep -o 'now_playing: VBlank [0-9]*: popup' "$LOG" | grep -o '[0-9]*' | paste -sd, -))"
fi
if [ -z "$why" ]; then
  echo "race_check: PASS race $ROUTE reached and driven to VBlank $VBL; last windows ${RATE} VBlanks/s; $FB$MUSIC$NOWP$BOOST$RUMB${VU0:+; $VU0} (log $LOG)"
  exit 0
fi
echo "race_check: FAIL $why (log $LOG)"
exit 1
