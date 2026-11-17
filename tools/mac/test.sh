#!/bin/bash
# macOS: run the self-contained tests, a headless b3view render on Metal, and write build/mac/report.txt to send back.
# Run tools/mac/build.sh [disc folder] first. docs/mac.md.
#
#   tools/mac/test.sh [AREA/ROUTE]      (the route for the headless render, default AS/C1_V1)
set -u
cd "$(dirname "$0")/../.." || exit 1
OUT=build/mac
[ -f "$OUT/.defs" ] || { echo "run tools/mac/build.sh first"; exit 1; }
ROUTE=${1:-AS/C1_V1}
CXX=${CXX:-clang++}
if [ -z "${MAKE:-}" ]; then if command -v gmake >/dev/null 2>&1; then MAKE=gmake; else MAKE=make; fi; fi
IFS='|' read -r _ ELF GAMEDIR SDLC < "$OUT/.defs"
REPORT=$OUT/report.txt
{
  echo "== b3 mac report $(date '+%Y-%m-%d %H:%M:%S')"
  echo "repo: $(git rev-parse --short HEAD 2>/dev/null) $(git rev-parse --abbrev-ref HEAD 2>/dev/null)$(git diff --quiet 2>/dev/null || echo ' (local changes)')"
  echo "host: $(uname -sm) $(sw_vers -productVersion 2>/dev/null) $(sysctl -n machdep.cpu.brand_string 2>/dev/null)"
  echo "compiler: $("$CXX" --version 2>&1 | head -1) ($("$CXX" -dumpmachine 2>&1))"
  echo "SDL3: $(pkg-config --modversion sdl3 2>/dev/null || echo '?')"
  echo "retail ELF: ${ELF:-none}   game files: ${GAMEDIR:-none}"
  echo
  echo "== unit tests (make mac-tests)"
} > "$REPORT"

# The tests: the same make line as build.sh (nothing rebuilds unless a source changed)
MK=("$MAKE" --no-print-directory BUILD="$OUT" CXX="$CXX" GAME="${GAMEDIR:-/nonexistent}" RETAIL_ELF="${ELF:-/nonexistent}")
[ -n "$SDLC" ] && command -v pkg-config >/dev/null 2>&1 && MK+=(SDL3_CFLAGS="$(pkg-config --cflags sdl3)" SDL3_LIBS="$(pkg-config --libs sdl3)")
# each test under a 15-minute alarm (macOS has no timeout(1); perl is part of the base system)
"${MK[@]}" RUN="perl -e 'alarm 900; exec @ARGV'" mac-tests > "$OUT/mac-tests.out" 2>&1
TRC=$?
cat "$OUT/mac-tests/summary.txt" >> "$REPORT" 2>/dev/null || echo "(no summary: see $OUT/mac-tests.out)" >> "$REPORT"
# mac3: why a test did not build -- each one rebuilt alone, its first compiler / linker errors into the report
for t in $(grep 'NOT BUILT' "$OUT/mac-tests/summary.txt" 2>/dev/null | awk '{print $2}'); do
  { echo; echo "== $t: build errors"
    "${MK[@]}" "$OUT/$t" 2>&1 | grep -E -m 15 'error|undefined|ld:' || echo "(no error lines: see $OUT/mac-tests.out)"; } >> "$REPORT"
done
# the bit-exactness lines of the retail comparisons (no game data in them: counts and verdicts only)
{
  echo
  echo "== retail comparisons (b3libm vs the retail code, run in the R5900 interpreter)"
  grep -h -E 'vs retail|bit-exact|differ' "$OUT"/mac-tests/test_libm*.log "$OUT"/mac-tests/test_overrides.log 2>/dev/null | head -120
  echo
  echo "== Ps2FloatEnv (FPCR round-toward-zero + flush-to-zero) vs b3fp::ref"
  grep -h -E 'operand pairs' "$OUT"/mac-tests/test_fenv.log 2>/dev/null
} >> "$REPORT"

# Headless b3view on Metal: one PNG of the route (game-derived: stays in build/mac, git-ignored)
BRC=skipped
if [ -x "$OUT/b3view" ] && [ -n "$GAMEDIR" ] && [ -d "$GAMEDIR/TRACKS/$ROUTE" ]; then
  PNG=$OUT/b3view-$(echo "$ROUTE" | tr / _).png
  perl -e 'alarm 300; exec @ARGV or die "exec: $!"' "$OUT/b3view" "$GAMEDIR/TRACKS/$ROUTE" --offscreen "$PNG" --size 1280x720 \
    > "$OUT/b3view-offscreen.log" 2>&1
  BRC=$?
  { echo; echo "== b3view --offscreen $ROUTE (exit $BRC)"; head -20 "$OUT/b3view-offscreen.log"
    [ -f "$PNG" ] && echo "png: $PNG ($(wc -c < "$PNG" | tr -d ' ') bytes, sha256 $(shasum -a 256 "$PNG" 2>/dev/null | cut -c1-16))"; } >> "$REPORT"
else
  { echo; echo "== b3view --offscreen: skipped (no build/mac/b3view, or no $ROUTE in the disc folder)"; } >> "$REPORT"
fi

cat "$REPORT"
echo
echo "Send back: $REPORT$( [ "$BRC" = 0 ] && echo " and the PNG named above")."
echo "Then run the window: tools/mac/b3view.sh $ROUTE   (screenshot the title bar: it shows the fps and 'metal')."
[ "$TRC" = 0 ]
