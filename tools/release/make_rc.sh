#!/bin/bash
# make_rc.sh -- a tester release candidate for Windows in one go (rcprep, 1.4.0): the release package of <commit> with
# VERSION = <version>, plus run-all-tests (tools/release/owner_tests, generic: the tester's own game data and save) and
# TESTERS.txt (tools/release/rc), as one zip for testers. Nothing is pushed or published: the owner hands the zip out.
#
#   tools/release/make_rc.sh <commit> <version> <where>      e.g. make_rc.sh dev 1.4.0-rc.1 "the Discord thread"
#     <where>   what TESTERS.txt says to send the zips to
#   DRY=1: print the steps only. KEEP_WORKTREE=1: keep the rc worktree. B3_GAME_DIR, SAVES: as for race_check (the
#   scenarios' capture and the checks); WIN (local.env): the Windows side (toolchain, outputs). B3_ISO: the US disc
#   image for the from-the-image checks (required; SKIP_ISO_CHECKS=1 skips them, with a warning). The game runs need the
#   environment the checks always need (B3_GAME_DIR, SAVES, LD_LIBRARY_PATH for the SDL the build uses)
#
# Steps:
#  1. a worktree on a new local branch rc-<version> at <commit>; VERSION = <version>, committed there only (never on dev)
#  2. make test there (Linux, NATIVE_FROM=bundle): the version gates' tests run against the candidate's VERSION too;
#     then CLAUDE.md's checks for a candidate, on its own Linux build: settings_start_check (the settings a run starts
#     with, no B3NATIVE_EXTRAS) and race_check race + trafficseries from the disc image (B3_ISO), STRICT. Each must
#     end "PASS"; the candidate is not packaged otherwise
#  3. tools/release/package_win.sh (build_win RENDER=1 STRIP=1, no DEBUG_INFO, no debug copies) into $WIN/testers-<version>
#  4. run-all-tests' scenarios.json from the candidate's race_check (tools/release/owner_tests/gen.sh)
#  5. the tester folder: the release package unpacked + run-all-tests\ (no card: the tester's own save) + TESTERS.txt
#     (@VERSION@ / @WHERE@ filled) + RELEASE-NOTES.txt (docs/release/notes-<base>.md), Windows line ends
#  6. a privacy check of every file in it (names, the home folder, WSL paths, this machine's folders): it fails on a hit
#  7. burnout3-native-<version>-testers-win64.zip of that folder, and the release package's own zip beside it
set -euo pipefail
cd "$(dirname "$0")/../.."
PORT=$PWD
. tools/local_env.sh; b3_local_env .
COMMIT=${1:?commit}; VERSION=${2:?version, e.g. 1.4.0-rc.1}; WHERE=${3:?where testers send their files}
WIN=${WIN:?WIN (local.env)}
BASE=${VERSION%%-*}
REL=$WIN/testers-$VERSION
WT=$(cd .. && pwd)/wt-rc-$VERSION
BR=rc-$VERSION
run() { echo "+ $*"; [ "${DRY:-0}" = 1 ] || "$@"; }
die() { echo "make_rc: FAIL $*"; exit 1; }

[ -f "docs/release/notes-$BASE.md" ] || die "no docs/release/notes-$BASE.md"
[ -n "${B3_GAME_DIR:-}" ] && [ -e "$B3_GAME_DIR" ] || die "B3_GAME_DIR (the game data for the checks and the scenarios) not set"
[ -n "${SAVES:-}" ] && [ -d "$SAVES" ] || die "SAVES (the test card for the checks) not set"
[ -e "$REL" ] && [ "${DRY:-0}" != 1 ] && die "$REL exists (move it away first)"
git rev-parse --verify --quiet "$COMMIT^{commit}" > /dev/null || die "no commit $COMMIT"
git rev-parse --verify --quiet "refs/heads/$BR" > /dev/null && die "branch $BR exists"

# 1. the candidate's tree
run git worktree add -b "$BR" "$WT" "$COMMIT"
if [ "${DRY:-0}" != 1 ]; then
  printf '%s\n' "$VERSION" > "$WT/VERSION"
  git -C "$WT" commit -q -m "rc: VERSION $VERSION (a tester release candidate; local only, never on dev)" -- VERSION
fi
echo "make_rc: $BR at $(git -C "$WT" log --oneline -1 2>/dev/null | cut -c1-60)"
# 2. tests and the candidate's checks
run make -C "$WT" NATIVE_FROM=bundle test
run make -C "$WT" -j8 NATIVE_FROM=bundle build/native_play build/libb3render.so
check() {   # check <name> <command...>: its last line must hold PASS
  local name=$1; shift
  echo "+ ($name) $*"
  [ "${DRY:-0}" = 1 ] && return 0
  local out; out=$("$@" 2>&1 | tail -1)
  echo "make_rc: $name: ${out:0:160}"
  case $out in *PASS*) ;; *) die "$name did not pass (above)" ;; esac
}
check settings_start env -C "$WT" tools/native/settings_start_check.sh build/native_play build/settings_start
if [ "${SKIP_ISO_CHECKS:-0}" = 1 ]; then
  echo "make_rc: WARNING: the from-the-disc-image checks skipped (SKIP_ISO_CHECKS=1)"
else
  [ -n "${B3_ISO:-}" ] && { [ "${DRY:-0}" = 1 ] || [ -f "$B3_ISO" ]; } || die "B3_ISO (the US disc image) not set or not a file (SKIP_ISO_CHECKS=1 skips the checks)"
  check race_iso env -C "$WT" B3_GAME_DIR="$B3_ISO" STRICT=1 tools/native/race_check.sh build/native_play build/rc_race_iso
  check trafficseries_iso env -C "$WT" B3_GAME_DIR="$B3_ISO" SCENARIO=trafficseries STRICT=1 tools/native/race_check.sh build/native_play build/rc_traffic_iso
fi
# 3. the release package (package_win builds first: build_win RENDER=1 STRIP=1 into $WIN/release-build)
run env REL="$REL" "$WT/tools/release/package_win.sh"
# 4. run-all-tests' scenarios from the candidate
run "$WT/tools/release/owner_tests/gen.sh" "$WT/build/owner_tests"
# 5. the tester folder
if [ "${DRY:-0}" != 1 ]; then
  PKG=$(ls -d "$REL"/stage/burnout3-native-*-win64 | head -1)
  [ -d "$PKG" ] || die "no package folder under $REL/stage"
  NAME=$(basename "$PKG")
  OUTD=$REL/$NAME
  mv "$PKG" "$OUTD" && rmdir "$REL/stage" 2>/dev/null || true
  mkdir -p "$OUTD/run-all-tests"
  cp "$WT/tools/release/owner_tests/run-all-tests.bat" "$WT/tools/release/owner_tests/run-all-tests.ps1" "$WT/build/owner_tests/scenarios.json" "$OUTD/run-all-tests/"
  sed -e "s|@VERSION@|$VERSION|g; s|@WHERE@|$WHERE|g" -e 's/$/\r/' "$WT/tools/release/rc/TESTERS.txt" > "$OUTD/TESTERS.txt"
  sed -e 's/$/\r/' "$WT/docs/release/notes-$BASE.md" > "$OUTD/RELEASE-NOTES.txt"
  # 6. privacy: this machine's user names, home folders and private folders, nowhere in the tester folder
  names=$(printf '%s\n' "$(whoami)" "$(cmd.exe /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r')" | sort -u | grep -v '^$' || true)
  bad=0
  for n in $names "/mnt/c" "$(basename "$WIN")" "port-notes" "$HOME"; do
    if grep -rIl --exclude='*.exe' --exclude='*.dll' -F "$n" "$OUTD" > /tmp/make_rc_hits.$$ 2>/dev/null; then
      echo "make_rc: '$n' found in:"; sed 's/^/  /' /tmp/make_rc_hits.$$; bad=1
    fi
  done
  for n in $names; do   # the programs: a user name in a compiled-in path (a debug build's) would show here
    if grep -l -a -F "\\Users\\$n\\" "$OUTD"/*.exe "$OUTD"/*.dll > /tmp/make_rc_hits.$$ 2>/dev/null; then
      echo "make_rc: a path with the user name '$n' compiled into:"; sed 's/^/  /' /tmp/make_rc_hits.$$; bad=1
    fi
  done
  rm -f /tmp/make_rc_hits.$$
  [ $bad = 0 ] || die "private text in the tester folder (above)"
  echo "make_rc: privacy check clean (the user names, /mnt/c, $(basename "$WIN"), port-notes, \$HOME)"
  # 7. the zip
  (cd "$REL" && python3 -I - "$NAME" "burnout3-native-$VERSION-testers-win64.zip" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, _, files in os.walk(src):
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, os.path.relpath(p, '.').replace(os.sep, '/'))
    n = len(z.namelist())
print('make_rc: %s (%d files)' % (dst, n))
PY
  )
fi
[ "${KEEP_WORKTREE:-0}" = 1 ] || run git worktree remove --force "$WT"
echo "make_rc: done: $REL (the tester zip, the release package zip, the unpacked folder); branch $BR kept (local only)"
