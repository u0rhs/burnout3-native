#!/usr/bin/env bash
# run_capped.sh [--as SIZE] -- CMD...: the user's standing rule (2026-11-06) for test tools, native_boot
# runs, checkers and long-running programs: under an address-space cap (prlimit --as, default 6G, never above 10G) and
# only while `free -g` shows at least 6 GB available.
set -euo pipefail
cap=6G
if [ "${1:-}" = "--as" ]; then cap="$2"; shift 2; fi
[ "${1:-}" = "--" ] && shift
n=${cap%G}
if ! [[ "$n" =~ ^[0-9]+$ ]] || [ "$n" -gt 10 ]; then echo "run_capped: cap $cap: at most 10G, in whole G" >&2; exit 2; fi
avail=$(free -g | awk '/^Mem:/ {print $7}')
if [ "$avail" -lt 6 ]; then echo "run_capped: only ${avail} GB available (< 6): not starting $1" >&2; exit 3; fi
exec prlimit --as=$((n * 1024 * 1024 * 1024)) -- "$@"
