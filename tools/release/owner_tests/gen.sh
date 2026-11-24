#!/bin/bash
# gen.sh -- ownertests: the owner's unattended test runner's scenarios (run-all-tests.ps1 reads scenarios.json), made from
# tools/native/race_check.sh's own scripts so they cannot drift from the checked ones. Each base scenario is run through
# race_check with a capture shim in place of native_play (nothing is played): its --keys, its environment and its
# pc_settings.ini are recorded; gen.py then writes the runs (README.txt: the list) with the window's changes (paced from
# the race, no offscreen, a scratch card and user folder per run).
#
#   tools/release/owner_tests/gen.sh [out dir]      default build/owner_tests; needs B3_GAME_DIR and SAVES (race_check's)
set -euo pipefail
cd "$(dirname "$0")/../../.."
OUT=${1:-build/owner_tests}
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
. tools/local_env.sh; b3_local_env .
mkdir -p "$OUT/capture"
SHIM=$OUT/capture/native_play
cat > "$SHIM" <<'SH'
#!/bin/bash
# the capture shim: the arguments (one a line) and the environment race_check gives native_play
printf '%s\n' "$@" > "$CAPTURE_DIR/args.txt"
env > "$CAPTURE_DIR/env.txt"
echo "native_play: capture shim (nothing played)"
exit 0
SH
chmod +x "$SHIM"
env > "$OUT/capture/env.outer"   # this shell's: what race_check did not set is left out
for s in race replaycams morerivals crashresults; do
  d=$OUT/capture/$s; rm -rf "$d"; mkdir -p "$d"
  env CAPTURE_DIR="$d" SCENARIO=$s tools/native/race_check.sh "$SHIM" "$d/rc" > "$d/race_check.txt" 2>&1 || true
  [ -s "$d/args.txt" ] || { echo "gen: $s: race_check did not start native_play (see $d/race_check.txt)"; exit 1; }
  [ -f "$d/rc/pc_settings.ini" ] && cp "$d/rc/pc_settings.ini" "$d/pc_settings.ini"
done
python3 -I tools/release/owner_tests/gen.py "$OUT/capture" "$OUT/scenarios.json"
echo "gen: $OUT/scenarios.json"
