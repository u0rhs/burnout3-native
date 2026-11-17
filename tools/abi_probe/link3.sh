#!/bin/bash
# link3.sh: probe3.cpp compiled by the decomp's MWCC (cc.sh) and linked at fixed addresses (link3.ld) into a flat
# image, probe3.bin, plus its symbol addresses, probe3.sym (both committed: tests/test_calls.cpp runs them in
# tests/r5900_mini.h, so `make test` needs neither MWCC nor its licence).
set -e
here=$(cd "$(dirname "$0")" && pwd)
"$here/cc.sh" "$here/probe3.cpp"
obj="${TMPDIR:-/tmp}/probe3.o"
mips-linux-gnu-ld -m elf32ltsmip -T "$here/link3.ld" "$obj" -o "${TMPDIR:-/tmp}/probe3.elf"
mips-linux-gnu-objcopy -O binary -j .text "${TMPDIR:-/tmp}/probe3.elf" "$here/probe3.bin"
mips-linux-gnu-nm "${TMPDIR:-/tmp}/probe3.elf" | awk '$2 ~ /[TtARrDd]/ {print $1, $3}' | sort > "$here/probe3.sym"
