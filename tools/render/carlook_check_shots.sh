#!/bin/bash
# carlook_check_shots.sh -- the same colour check on native_play's race_check shots (the under-cover case: SCENARIO=photo's
# shot 13592 is the paused race under the elevated rail; 10700 / 11500 are the race in the sun).
#   tools/render/carlook_check_shots.sh <orig shots dir> <enh shots dir> <poll> <x0> <y0> <x1> <y1>
# Both folders come from tools/native/race_check.sh runs of one scenario: B3_CAR_REFLECTIONS=original and =enhanced
# (the renderer's start value for CAR REFLECTIONS; native_play has no flag of its own); the box (fractions of the picture) a patch of paint.
# The enhanced shot must keep the original's hue and saturation (tools/render/carlook_check.py).
set -u
cd "$(dirname "$0")/../.." || exit 1
O=${1:?usage: carlook_check_shots.sh <orig dir> <enh dir> <poll> <x0> <y0> <x1> <y1>}; E=${2:?}; POLL=${3:?}
exec python3 -I tools/render/carlook_check.py "$O/live_${POLL}_0.png" "$E/live_${POLL}_0.png" "$4" "$5" "$6" "$7"
