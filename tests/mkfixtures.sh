#!/bin/sh
# mpegv.library's tests: how tests/fixtures were made (ffmpeg's encoders).
# Not run by the tests; tests/expected.txt is what mpv_test -all gave for
# these files.
#   ps.mpg    MPEG-1 Program Stream, B pictures, MP2 sound
#   es.m1v    MPEG-1 video stream, 29.97, 4:3 pixels
#   pal.m2v   MPEG-2 video stream as a PAL DVD's: 720x576, 25, interlaced
#             (top field first, interlaced DCT and prediction, the
#             alternate scan, intra VLC table B.15), 4:3
#   ntsc.m2v  MPEG-2 video stream as an NTSC DVD stores film: 720x480,
#             progressive frames marked 29.97 with 3:2 pulldown (tests/
#             telecine.py), 16:9
#   dvd.vob   a DVD's VOB: MPEG-2 packs, 720x576 MPEG-2 video, AC-3 sound
#             in private stream 1, navigation packets (private stream 2)
# GPL-3.0-or-later, (c) 2026 Brian King.
set -e
cd "$(dirname "$0")/fixtures"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc=size=160x112:rate=25:duration=2" \
    -f lavfi -i "sine=frequency=440:duration=2" -c:v mpeg1video -b:v 300k -bf 2 -g 12 \
    -c:a mp2 -b:a 64k -ar 44100 -f mpeg ps.mpg
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "smptebars=size=176x144:rate=30000/1001:duration=1.5" \
    -c:v mpeg1video -b:v 400k -bf 2 -g 15 -aspect 4:3 -f mpeg1video es.m1v
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=720x576:rate=50,tinterlace=mode=interleave_top" \
    -frames:v 12 -c:v mpeg2video -b:v 3M -maxrate 8M -bufsize 1835k -g 12 -bf 2 -flags +ilme+ildct \
    -top 1 -alternate_scan 1 -intra_vlc 1 -aspect 4:3 -f mpeg2video pal.m2v
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=720x480:rate=24000/1001" -frames:v 12 \
    -c:v mpeg2video -b:v 4M -maxrate 8M -bufsize 1835k -g 12 -bf 2 -aspect 16:9 -f mpeg2video film.m2v
python3 ../telecine.py film.m2v ntsc.m2v
rm film.m2v
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=720x576:rate=25" \
    -f lavfi -i "sine=frequency=440:sample_rate=48000" -frames:v 12 -t 0.48 -c:v mpeg2video -b:v 4M \
    -maxrate 8M -bufsize 1835k -g 12 -bf 2 -aspect 16:9 -c:a ac3 -b:a 192k -f dvd dvd.vob
