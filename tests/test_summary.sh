#!/bin/bash
# test_summary.sh <log dir> -- make test's summary of the test binaries' logs (tests/test.h: each case ok, SKIPPED or
# FAILED; the last line "N tests, C checks, F failures, S skipped"). Skipped cases do not fail the run (exit 0); they
# are counted and listed.
set -u
dir=$1
bins=0; cases=0; checks=0; skipped=0
list=""
for log in "$dir"/*.log; do
  [ -e "$log" ] || continue
  bins=$((bins + 1))
  last=$(tail -1 "$log")
  if [[ $last =~ ^([0-9]+)\ tests,\ ([0-9]+)\ checks,\ [0-9]+\ failures(,\ ([0-9]+)\ skipped)? ]]; then
    cases=$((cases + BASH_REMATCH[1])); checks=$((checks + BASH_REMATCH[2])); skipped=$((skipped + ${BASH_REMATCH[4]:-0}))
  fi
  name=$(basename "$log" .log)
  for c in $(awk '$2 == "SKIPPED" { print $1 }' "$log"); do list="$list $name:$c"; done
done
echo "== summary: $bins test binaries passed: $cases cases ($checks checks), $((cases - skipped)) ran, $skipped SKIPPED"
if [ -n "$list" ]; then echo "   SKIPPED (no checks ran, or t::skip):$list" | fold -s -w 160 | sed '2,$s/^/     /'; fi
exit 0
