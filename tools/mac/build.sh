#!/bin/bash
# macOS (Apple Silicon) build: the self-contained unit tests and b3view / b3replay on SDL3 (Metal). docs/mac.md.
#
#   tools/mac/build.sh [disc folder]
#
# disc folder: the copied disc contents (SLUS_210.50, DATA, TRACKS, PVEH, ...); default data/game when it exists.
# It is only read: the tests compare against the retail executable in it (SLUS_210.50) and decode real files from it.
# The path is remembered in build/mac/disc (a symlink) for tools/mac/test.sh and tools/mac/b3view.sh.
# Everything lands in build/mac/ (git-ignored); the log is build/mac/build.log.
# Environment: JOBS (default: all cores), CXX (default clang++), MAKE (default gmake if installed, else make).
set -u
cd "$(dirname "$0")/../.." || exit 1
ROOT=$PWD
OUT=build/mac
mkdir -p "$OUT"
LOG=$OUT/build.log
: > "$LOG"
say() { printf '%s\n' "$*" | tee -a "$LOG"; }
die() { say "ERROR: $*"; say "(full log: $LOG)"; exit 1; }

say "== b3 mac build $(date '+%Y-%m-%d %H:%M:%S')  (repo $(git rev-parse --short HEAD 2>/dev/null || echo '?'))"
OS=$(uname -s); ARCH=$(uname -m)
say "host: $OS $ARCH$( [ "$OS" = Darwin ] && echo " macOS $(sw_vers -productVersion 2>/dev/null)")"
[ "$OS" = Darwin ] || say "note: not macOS -- continuing (the script also runs on Linux for checking)"
[ "$OS" = Darwin ] && [ "$ARCH" != arm64 ] && say "note: not arm64 (Rosetta shell? run 'arch -arm64 bash tools/mac/build.sh')"

# ---- tools ----
if [ "$OS" = Darwin ] && ! xcode-select -p >/dev/null 2>&1; then die "Xcode command line tools missing: xcode-select --install"; fi
CXX=${CXX:-clang++}
command -v "$CXX" >/dev/null 2>&1 || die "no C++ compiler '$CXX' (Xcode command line tools: xcode-select --install)"
say "compiler: $("$CXX" --version 2>&1 | head -1) -> $("$CXX" -dumpmachine 2>&1)"
if [ -z "${MAKE:-}" ]; then
  if command -v gmake >/dev/null 2>&1; then MAKE=gmake; else MAKE=make; fi
fi
say "make: $("$MAKE" --version 2>&1 | head -1)"
case "$("$MAKE" --version 2>&1 | head -1)" in
  *"GNU Make 3."*) say "note: GNU Make 3.x (Apple's) -- if the build misbehaves: brew install make (then it uses gmake)";;
esac
command -v python3 >/dev/null 2>&1 || die "python3 missing (it comes with the Xcode command line tools)"
say "python3: $(python3 --version 2>&1)"
if [ -z "${JOBS:-}" ]; then JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4); fi

# ---- SDL3 (Homebrew) ----
SDL_OK=0
if [ -n "${SDL3_CFLAGS:-}" ] && [ -n "${SDL3_LIBS:-}" ]; then
  SDL_OK=1; say "SDL3: from SDL3_CFLAGS / SDL3_LIBS"
elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl3 2>/dev/null; then
  SDL3_CFLAGS=$(pkg-config --cflags sdl3); SDL3_LIBS=$(pkg-config --libs sdl3); SDL_OK=1
  say "SDL3: $(pkg-config --modversion sdl3) (pkg-config)"
elif command -v brew >/dev/null 2>&1 && P=$(brew --prefix sdl3 2>/dev/null) && [ -f "$P/include/SDL3/SDL.h" ]; then
  SDL3_CFLAGS="-I$P/include"; SDL3_LIBS="-L$P/lib -lSDL3"; SDL_OK=1
  say "SDL3: $P (brew --prefix; pkg-config not found)"
else
  say "SDL3: NOT FOUND -- brew install sdl3 pkg-config (b3view and test_inproc need it; the other tests build without)"
  SDL3_CFLAGS=""; SDL3_LIBS=""
fi

# ---- disc ----
DISC=${1:-}
if [ -z "$DISC" ] && [ -e data/game ]; then DISC=data/game; fi
ELF=""; GAMEDIR=""
if [ -n "$DISC" ]; then
  [ -d "$DISC" ] || die "disc folder not found: $DISC"
  DISC_ABS=$(cd "$DISC" && pwd)
  ln -sfn "$DISC_ABS" "$OUT/disc"
  # the retail executable (APFS is case-insensitive by default; an ISO copy may keep the ';1' version suffix)
  for f in "$OUT/disc/SLUS_210.50" "$OUT/disc/slus_210.50" "$OUT/disc/SLUS_210.50;1"; do [ -f "$f" ] && { ELF=$f; break; }; done
  if [ -n "$ELF" ]; then
    SUM=$(shasum -a 256 "$ELF" 2>/dev/null || sha256sum "$ELF"); SUM=${SUM%% *}
    if [ "$SUM" = e118dba952bf9fde6a04ee284bb1f06fa0bb49c9e04bbe762af1039dcc443c66 ]; then
      say "disc: $DISC_ABS; SLUS_210.50 sha256 matches the retail executable"
    else
      say "disc: $DISC_ABS; WARNING SLUS_210.50 sha256 $SUM is not the expected e118dba9... (other region / version?)"
    fi
    case "$ELF" in *";1") ln -sfn "$ELF" "$OUT/SLUS_210.50"; ELF=$OUT/SLUS_210.50;; esac
  else
    say "disc: $DISC_ABS; no SLUS_210.50 in it (the retail-code comparisons will be skipped)"
  fi
  [ -f "$OUT/disc/DATA/GLOBAL.TXD" ] && GAMEDIR=$OUT/disc
  [ -n "$GAMEDIR" ] || say "note: no DATA/GLOBAL.TXD in the disc folder (test_b3asset's real-file checks skip; b3view needs it)"
else
  say "disc: none given (tools/mac/build.sh <disc folder>): retail-code and real-file checks will be skipped"
fi

# ---- tests (BUILD=build/mac). Compile-time defines carry the disc paths, so a changed disc rebuilds the tests. ----
MK=("$MAKE" --no-print-directory -j"$JOBS" BUILD="$OUT" CXX="$CXX" GAME="${GAMEDIR:-/nonexistent}" RETAIL_ELF="${ELF:-/nonexistent}")
[ "$SDL_OK" = 1 ] && MK+=(SDL3_CFLAGS="$SDL3_CFLAGS" SDL3_LIBS="$SDL3_LIBS")
STAMP="$CXX|$ELF|$GAMEDIR|$SDL3_CFLAGS"
FORCE=()
[ "$(cat "$OUT/.defs" 2>/dev/null)" = "$STAMP" ] || FORCE=(-B)
say "== building the self-contained tests (make mac-tests-build${FORCE[0]:+ -B}, -j$JOBS)"
# (bash 3.2, macOS's: an empty array under set -u is "unbound", hence the ${FORCE[@]+...} form)
"${MK[@]}" ${FORCE[@]+"${FORCE[@]}"} mac-tests-build >> "$LOG" 2>&1
printf '%s' "$STAMP" > "$OUT/.defs"
missing=""
for t in $("$MAKE" --no-print-directory -s print-mac-tests 2>/dev/null); do [ -x "$OUT/$t" ] || missing="$missing $t"; done
if [ -n "$missing" ]; then say "tests NOT built:$missing  (see $LOG: grep -n error: $LOG)"; else say "tests: all built"; fi

# ---- viewers ----
if [ "$SDL_OK" = 1 ]; then
  say "== building b3view and b3replay (SDL3, Metal at run time)"
  if "${MK[@]}" "$OUT/b3view" "$OUT/b3replay" >> "$LOG" 2>&1; then say "viewers: $OUT/b3view, $OUT/b3replay"
  else say "viewers: BUILD FAILED (grep -n error: $LOG)"; missing="$missing b3view"; fi
else
  missing="$missing b3view"
fi

say "== done: next  tools/mac/test.sh   (then: tools/mac/b3view.sh AS/C1_V1)"
[ -z "$missing" ]
