#!/bin/sh
# mpegv.library's tests: how tests/fixtures were made (ffmpeg's encoder):
# a Program Stream with B pictures and MP2 sound, and a bare video stream
# at 29.97 with 4:3 pixels. Not run by the tests; tests/expected.txt is
# what mpv_test -all gave for these files. MIT licence, (c) 2026 Brian King.
set -e
cd "$(dirname "$0")/fixtures"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc=size=160x112:rate=25:duration=2" \
    -f lavfi -i "sine=frequency=440:duration=2" -c:v mpeg1video -b:v 300k -bf 2 -g 12 \
    -c:a mp2 -b:a 64k -ar 44100 -f mpeg ps.mpg
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "smptebars=size=176x144:rate=30000/1001:duration=1.5" \
    -c:v mpeg1video -b:v 400k -bf 2 -g 15 -aspect 4:3 -f mpeg1video es.m1v
