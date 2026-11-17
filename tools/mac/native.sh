#!/bin/bash
# The native game on macOS (Apple Silicon): builds native_play and the in-process renderer (libb3render.so, Metal via
# SDL3) with Apple clang from the committed native source bundle gen/native (no decomp checkout needed), then runs it
# in a window with the keyboard / a gamepad as the pad and sound. docs/mac.md section 9.
#
#   tools/mac/native.sh <disc folder> [native_play options ...]
#
# disc folder: the copied disc contents (SLUS_210.50, DATA, TRACKS, PVEH, ...), only read; default: the one
# tools/mac/build.sh remembered (build/mac/disc). Options after it go to native_play (e.g. --quit-after 120, --no-audio,
# --offscreen). Environment:
#   EXPLORE_RETAIL=<hex,...>  functions run as the original PS2 code instead of natively (the exploration stand-in;
#                    default: none)
#   SAVES=<folder>   a memory card folder (the game's saves); without it: no card is inserted
#   BUILD_ONLY=1     build, do not run
#   LAUNCHER=1       also build the launcher (b3launcher, beside native_play in build/mac/) and open it instead of the
#                    game: its Game Data page takes the disc (the folder argument is then optional), START GAME runs
#                    native_play from the same folder
#   JOBS, CXX (default clang++), MAKE (default gmake if installed, else make)
#   REPLAY_PAD=<file> the pad from a recording (native_play --replay-pad) instead of the keys / controller
# Everything lands in build/mac/ (git-ignored): the build log build/mac/native-build.log, the run's log
# build/mac/native_play.log and the pad recording build/mac/pads.txt (send both back).
set -u
cd "$(dirname "$0")/../.." || exit 1
OUT=build/mac
mkdir -p "$OUT"
LOG=$OUT/native-build.log
: > "$LOG"
say() { printf '%s\n' "$*" | tee -a "$LOG"; }
die() { say "ERROR: $*"; say "(build log: $LOG)"; exit 1; }

say "== b3 native game, mac build $(date '+%Y-%m-%d %H:%M:%S')  (repo $(git rev-parse --short HEAD 2>/dev/null || echo '?'))"
OS=$(uname -s); ARCH=$(uname -m)
say "host: $OS $ARCH$( [ "$OS" = Darwin ] && echo " macOS $(sw_vers -productVersion 2>/dev/null)")"
[ "$OS" = Darwin ] && [ "$ARCH" != arm64 ] && say "note: not arm64 (Rosetta shell? run 'arch -arm64 bash tools/mac/native.sh ...')"
CXX=${CXX:-clang++}
command -v "$CXX" >/dev/null 2>&1 || die "no C++ compiler '$CXX' (xcode-select --install)"
say "compiler: $("$CXX" --version 2>&1 | head -1)"
if [ -z "${MAKE:-}" ]; then
  if command -v gmake >/dev/null 2>&1; then MAKE=gmake; else MAKE=make; fi
fi
if [ -z "${JOBS:-}" ]; then JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4); fi

# ---- SDL3 (Homebrew), for the renderer ----
if [ -z "${SDL3_CFLAGS:-}" ] || [ -z "${SDL3_LIBS:-}" ]; then
  if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl3 2>/dev/null; then
    SDL3_CFLAGS=$(pkg-config --cflags sdl3); SDL3_LIBS=$(pkg-config --libs sdl3)
  elif command -v brew >/dev/null 2>&1 && P=$(brew --prefix sdl3 2>/dev/null) && [ -f "$P/include/SDL3/SDL.h" ]; then
    SDL3_CFLAGS="-I$P/include"; SDL3_LIBS="-L$P/lib -lSDL3"
  else
    die "SDL3 not found: brew install sdl3 pkg-config"
  fi
fi
say "SDL3: $SDL3_LIBS"

# ---- disc ----
DISC=${1:-}
[ $# -gt 0 ] && shift
if [ -z "$DISC" ] && [ -e "$OUT/disc" ]; then DISC=$OUT/disc; fi
if [ "${LAUNCHER:-0}" = 1 ] && [ -z "$DISC" ]; then
  say "disc: none given (the launcher's Game Data page takes it)"
else
  [ -n "$DISC" ] || die "usage: tools/mac/native.sh <disc folder> [native_play options]  (or LAUNCHER=1 tools/mac/native.sh)"
  [ -f "$DISC/SLUS_210.50" ] || die "$DISC has no SLUS_210.50 (the copied disc contents are needed)"
  [ -d "$DISC/DATA" ] || die "$DISC has no DATA folder"
  DISC=$(cd "$DISC" && pwd)
  say "disc: $DISC"
fi

# ---- build: from gen/native (NATIVE_FROM=bundle: no decomp, no generation) ----
[ -f gen/native/manifest.tsv ] && [ -f gen/native/table/functions.h ] || die "gen/native is missing (git pull?)"
say "native bundle: $(ls gen/native/units | wc -l | tr -d ' ') units"
# vu0n: the VU0 microprograms' C++ is committed (gen/native/vu0/vu0_aot_gen.cpp), so nothing is generated from the disc
MK=("$MAKE" --no-print-directory -j"$JOBS" BUILD="$OUT" CXX="$CXX" NATIVE_FROM=bundle SDL3_CFLAGS="$SDL3_CFLAGS" SDL3_LIBS="$SDL3_LIBS")
# nofallback1: the exploration stand-ins below need the retail fallback, which only a NATIVE_FALLBACK=1 build has
if [ -n "${EXPLORE_RETAIL:-}" ] || ! grep -q 'sceMpegGetPicture' gen/native/manifest.tsv; then
  say "a NATIVE_FALLBACK=1 build (the exploration stand-ins run the original PS2 code)"
  MK+=(NATIVE_FALLBACK=1)
fi
say "building native_play + libb3render.so (the first build compiles ~4,900 units: 10-20 minutes) ..."
T0=$(date +%s)
TARGETS=("$OUT/native_play" "$OUT/libb3render.so")
[ "${LAUNCHER:-0}" = 1 ] && TARGETS+=("$OUT/b3launcher")
"${MK[@]}" "${TARGETS[@]}" >> "$LOG" 2>&1 || {
  grep -E -m 20 'error|Error|undefined|ld:' "$LOG" | sed 's/^/  /'
  die "build failed"
}
say "built in $(( $(date +%s) - T0 )) s: ${TARGETS[*]}"
[ "${BUILD_ONLY:-0}" = 1 ] && exit 0
if [ "${LAUNCHER:-0}" = 1 ]; then   # the launcher: its own pages, START GAME runs native_play beside it
  mkdir -p "$OUT/assets" && cp -f assets/b3-icon.png "$OUT/assets/" 2>/dev/null || true
  say "opening the launcher ($OUT/b3launcher; its log: $OUT/launcher.log)"
  "$OUT/b3launcher" > "$OUT/launcher.log" 2>&1
  RC=$?
  say "launcher exit $RC"
  exit $RC
fi

# ---- run ----
ENVS=()
EXTRA=()
# movies: native when the bundle has libmpeg (movie2's C); until then the exploration stand-ins (logged loudly)
if grep -q 'sceMpegGetPicture' gen/native/manifest.tsv; then
  say "movies: native"
else
  say "movies: not native in this bundle yet -- the exploration stand-ins (IPU + movie: every movie ends at once)"
  ENVS+=(B3NATIVE_IPU_STANDIN=1)
  EXTRA+=(--explore-movies)
fi
# No stand-ins by default (mac5, 2026-11-08): the three this list held (func_00427080 / FindWave's char[12] -> [13],
# CheckCrashingTraffic 0x13E920's guest-pointer array) are fixed in the bundle since boot2h; Linux races run them
# natively (race_check). EXPLORE_RETAIL=<hex,...> still runs the listed functions as the original PS2 code.
EXPLORE_RETAIL=${EXPLORE_RETAIL-}
if [ -n "$EXPLORE_RETAIL" ]; then
  say "exploration: these run as the original PS2 code until their fixes land: $EXPLORE_RETAIL"
  EXTRA+=(--explore-retail "$EXPLORE_RETAIL")
fi
if [ -n "${SAVES:-}" ]; then
  [ -d "$SAVES" ] || die "SAVES=$SAVES is not a folder"
  ENVS+=(B3NATIVE_SAVES_ROOT="$(cd "$SAVES" && pwd)" B3NATIVE_CARD_PRESENT_VBLANK=270)
  say "memory card: $SAVES"
else
  say "memory card: none (SAVES=<folder> inserts one)"
fi
RLOG=$OUT/native_play.log
# mac4: the pad is recorded (build/mac/pads.txt: the reports per VBlank, no game data), so a run can be replayed exactly
# elsewhere (native_play --replay-pad); REPLAY_PAD=<file> replays one here instead of reading the keys / controller
if [ -n "${REPLAY_PAD:-}" ]; then
  EXTRA+=(--replay-pad "$REPLAY_PAD")
  say "replaying the pad from $REPLAY_PAD"
else
  EXTRA+=(--record-pad "$OUT/pads.txt")
  say "recording the pad to $OUT/pads.txt (send it back with the log)"
fi
say "running (log: $RLOG); keys: docs/mac.md section 5; close the window to end"
# ${a[@]+"${a[@]}"}: an empty array under set -u (macOS bash 3.2 calls a plain "${a[@]}" unbound)
env ${ENVS[@]+"${ENVS[@]}"} "$OUT/native_play" --game "$DISC" --elf "$DISC/SLUS_210.50" --render-lib "$PWD/$OUT/libb3render.so" \
  ${EXTRA[@]+"${EXTRA[@]}"} "$@" 2>&1 | tee "$RLOG"
RC=${PIPESTATUS[0]}
say "native_play exit $RC (log: $RLOG)"
exit "$RC"
