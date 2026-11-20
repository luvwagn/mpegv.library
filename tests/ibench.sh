#!/bin/sh
# mpegv.library's tests: instructions a picture (tests/prof_instr.py) for
# a stream, by function: the decoder built into build/68k/mpv_prof.
# GPL-3.0-or-later, (c) 2026 Brian King.
#
#   ibench.sh pictures file [-skipb] [-half]
set -e
top=$(cd "$(dirname "$0")/.." && pwd)
n=$1
f=$2
shift 2
opt="$*"
py=$(dirname "$(command -v vamos)")/python
d=$top/build/ibench/$(basename "$f")$(echo $opt | tr -d ' ')
rm -rf "$d"
mkdir -p "$d"
ln -s "$top/build/68k/mpv_prof" "$d/mpv_prof"
ln -s "$(cd "$(dirname "$f")" && pwd)/$(basename "$f")" "$d/s"
cd "$d"
"$py" "$top/tests/prof_instr.py" "$top/build/68k/mpv_prof.syms" -- -C 68020 -m 16384 \
    -H disable -s 64 -- mpv_prof -n "$n" -nohash $opt s > prof.txt 2>&1
grep -m1 "^total" prof.txt | awk -v n="$n" '{ printf "%s: %.3fM instructions a picture\n", "'"$(basename "$f")"'", $2 / n / 1e6 }'
