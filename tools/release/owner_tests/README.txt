owner_tests -- the owner's unattended test runner (ownertests, 2026-11-23)

What it is: run-all-tests.bat (Windows) plays scripted sessions in a real window on the PC's own graphics card, one
after another, with no input: the day race on Vulkan and on Direct3D 12, VSYNC on, night with scene depth off / on,
rain, night rain, the ULTRA reflections Track vs Track, props and cars (env timing), a RACE REPLAY with its cameras, an
8-car race and a Crash event confirmed with a controller's A. Each run has its own scratch user folder and card copy;
the player's own settings and saves are never read or written. At the end: one zip of every run's logs, frame timing
(B3_PERF_LOG), pad recording and screenshots, and a table (fps, 1 % low, motion, stalls, sound dropouts, env time).

Files:
  gen.sh / gen.py      scenarios.json from tools/native/race_check.sh's own scripts (a capture shim plays nothing),
                       so the runs cannot drift from the checked scripts: re-run after race_check's scripts change
  check.py             every run of scenarios.json offscreen on Linux, checked against its expected log lines
  run-all-tests.ps1    the runner (Windows PowerShell 5.1); run-all-tests.bat starts it
  scenarios.json       generated (not kept in git)

The card: card\ beside the runner when a bundle ships one (the owner's private bundles: race_check's 100 % save,
game-derived, never in git); else a read-only copy of the player's own saves (tester bundles, tools/release/make_rc.sh).
The zip leaves the card copies out and shortens the home folder to ~ in its text files.

  tools/release/owner_tests/gen.sh build/owner_tests                         (B3_GAME_DIR, SAVES as for race_check)
  tools/release/owner_tests/check.py build/native_play build/owner_tests/scenarios.json build/owner_tests/check
  run-all-tests.bat [list | <id or name> ...] [-Game <folder>] [-Exe <native_play.exe>] [-Offscreen]

Window-only pieces (ownertests): B3_WINDOW_SHOTS=<poll,...> (src/b3replay/live.cpp: F12's shot once the shown tick
reaches each poll; scripted keys never reach the window's own keys) and the perf log's frame_p99 column
(include/b3gfx/perf.h: each second's 99th percentile frame time, the table's 1 % low).
