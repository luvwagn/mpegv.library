#!/usr/bin/env python3
# mpegv.library's tests: a 23.976 progressive MPEG-2 video stream made as
# a DVD stores film for NTSC ("soft telecine"): its sequence marked 29.97
# and interlaced, each picture still a progressive frame, shown for 3
# fields or 2 in turn (repeat_first_field and top_field_first: 3:2
# pulldown). Nothing decoded changes, only how long each picture is shown.
# GPL-3.0-or-later, (c) 2026 Brian King.
#
#   telecine.py in.m2v out.m2v
import sys

d = bytearray(open(sys.argv[1], "rb").read())
i, tr = 0, 0
while True:
    i = d.find(b"\x00\x00\x01", i)
    if i < 0:
        break
    code, p = d[i + 3], i + 4
    if code == 0xB3:
        d[p + 3] = (d[p + 3] & 0xF0) | 4              # frame_rate_code 4: 29.97
    elif code == 0x00:
        tr = (d[p] << 2) | (d[p + 1] >> 6)          # temporal_reference
    elif code == 0xB5 and d[p] >> 4 == 1:
        d[p + 1] &= ~0x08                           # progressive_sequence 0
    elif code == 0xB5 and d[p] >> 4 == 8:
        tff, rff = [(1, 1), (0, 0), (0, 1), (1, 0)][tr % 4]
        d[p + 3] = (d[p + 3] & 0x7D) | (tff << 7) | (rff << 1)
        d[p + 4] |= 0x80                            # progressive_frame
    i = p
open(sys.argv[2], "wb").write(d)
