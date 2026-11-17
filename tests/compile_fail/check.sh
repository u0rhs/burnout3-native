#!/bin/bash
# check.sh <compiler and flags...>: every tests/compile_fail/*.cpp must fail to compile with its "// EXPECT:" text in
# the diagnostics (the call marshalling's remaining compile-time rejects, docs/abi-mwcc-ee.md §7).
dir=$(dirname "$0"); fails=0; n=0
for f in "$dir"/*.cpp; do
    n=$((n + 1))
    want=$(sed -n 's#^// EXPECT: ##p' "$f" | head -1)
    if out=$("$@" -fsyntax-only "$f" 2>&1); then echo "  FAIL $f compiled"; fails=$((fails + 1));
    elif ! grep -qF "$want" <<<"$out"; then echo "  FAIL $f: no '$want' in the errors"; fails=$((fails + 1)); fi
done
echo "$n compile-fail checks, $fails failures"
[ $fails = 0 ]
