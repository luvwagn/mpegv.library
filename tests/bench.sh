#!/bin/sh
# mpegv.library's tests: a 68020's cycles a picture (vamos's count) for
# each stream given, and where they go, by stage (tests/stages.py): the
# decoder built into build/68k/mpv_prof (make build/68k/mpv_prof), each
# stream in its own directory under build/bench, two at a time.
# GPL-3.0-or-later, (c) 2026 Brian King.
#
#   bench.sh pictures [-skipb] file ...
set -e
top=$(cd "$(dirname "$0")/.." && pwd)
n=$1; shift
opt=
if [ "$1" = "-skipb" ]; then opt=-skipb; shift; fi
py=$(dirname "$(command -v vamos)")/python
run() {
    d=$top/build/bench/$(basename "$1")$opt
    rm -rf "$d"; mkdir -p "$d"
    ln -s "$top/build/68k/mpv_prof" "$d/mpv_prof"
    ln -s "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")" "$d/s"
    (cd "$d" && "$py" "$top/tests/prof.py" "$top/build/68k/mpv_prof.syms" -- -C 68020 -m 16384 \
        -H disable -s 64 --slice-cycles 700 -- mpv_prof -n "$n" -nohash $opt s > prof.txt 2>&1)
}
set -- "$@"
while [ $# -gt 0 ]; do
    run "$1" & a=$!
    if [ $# -gt 1 ]; then run "$2" & b=$!; wait $b; fi
    wait $a
    shift; [ $# -gt 0 ] && shift
done
