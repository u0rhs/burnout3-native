#!/bin/bash
# carlook_check.sh -- renders a frame with CAR REFLECTIONS original and enhanced and checks that the paint kept its colour
# (tools/render/carlook_check.py: against the ORIGINAL render's paint, hue within 8 degrees, saturation not down more than
# 10 %, a neutral paint stays neutral), and that the gloss strength 0 gives Original's picture exactly.
#   tools/render/carlook_check.sh <scene.b3scene> <poll> <x0> <y0> <x1> <y1> [extra b3replay arguments, e.g. --car-look f.ini]
# The box (fractions of the 1280 x 720 picture) is a patch of paint, no windows. Needs build/b3replay, B3_GAME_DIR (the
# install's game folder) and LD_LIBRARY_PATH for SDL3; the scene is game-derived data (a capture of your own session).
set -u
cd "$(dirname "$0")/../.." || exit 1
SCENE=${1:?usage: carlook_check.sh <scene> <poll> <x0> <y0> <x1> <y1>}
POLL=${2:?}; BOX="$3 $4 $5 $6"
shift 6
GAME=${B3_GAME_DIR:?set B3_GAME_DIR}
T=$(mktemp -d "${TMPDIR:-/tmp}/carlook_check.XXXXXX") || exit 1
trap 'rm -f "$T"/o.png "$T"/e.png "$T"/z.png "$T"/z.ini; rmdir "$T"' EXIT
for m in original enhanced; do
    f=$T/${m:0:1}.png
    build/b3replay "$SCENE" --game "$GAME" --msaa 4 --size 1280x720 --offscreen "$f" --poll "$POLL" --car-reflections "$m" "$@" > /dev/null 2>&1 || { echo "carlook_check: FAIL render $m"; exit 1; }
done
# with the gloss strength 0 Enhanced must be Original's picture, pixel for pixel
printf 'strength = 0\n' > "$T/z.ini"
build/b3replay "$SCENE" --game "$GAME" --msaa 4 --size 1280x720 --offscreen "$T/z.png" --poll "$POLL" --car-reflections enhanced "$@" --car-look "$T/z.ini" > /dev/null 2>&1 || { echo "carlook_check: FAIL render strength 0"; exit 1; }
cmp -s "$T/o.png" "$T/z.png" && echo "carlook_check: PASS strength 0 equals Original" || { echo "carlook_check: FAIL strength 0 differs from Original"; exit 1; }
# shellcheck disable=SC2086
python3 -I tools/render/carlook_check.py "$T/o.png" "$T/e.png" $BOX
