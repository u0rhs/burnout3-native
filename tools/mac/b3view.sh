#!/bin/bash
# macOS: open b3view on a route of the disc folder given to tools/mac/build.sh (or B3_DISC). docs/mac.md.
#
#   tools/mac/b3view.sh [AREA/ROUTE | file under the disc folder] [b3view options...]
#   e.g. tools/mac/b3view.sh AS/C1_V1     tools/mac/b3view.sh EU            (a whole region)
#        tools/mac/b3view.sh PVEH/COMP/CAR1.BGV
# The first lines of output name the GPU driver ("driver metal ..."); the window title shows fps, size, MSAA, driver.
# Log: build/mac/b3view.log. Controls: docs/mac.md (right mouse look, WASD/QE, Shift, wheel, Esc quits).
set -u
cd "$(dirname "$0")/../.." || exit 1
OUT=build/mac
DISC=${B3_DISC:-$OUT/disc}
[ -x "$OUT/b3view" ] || { echo "build/mac/b3view missing: run tools/mac/build.sh <disc folder> first"; exit 1; }
[ -d "$DISC" ] || { echo "no disc folder: tools/mac/build.sh <disc folder> (or B3_DISC=<folder>)"; exit 1; }
WHAT=${1:-AS/C1_V1}
[ $# -gt 0 ] && shift
case "$WHAT" in
  PVEH/*|TRACKS/*) P=$DISC/$WHAT;;
  *) P=$DISC/TRACKS/$WHAT;;
esac
[ -e "$P" ] || { echo "not found: $P"; exit 1; }
"$OUT/b3view" "$P" "$@" 2>&1 | tee "$OUT/b3view.log"
exit "${PIPESTATUS[0]}"
