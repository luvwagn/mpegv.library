#!/usr/bin/env python3
# mpegv.library's tests: tests/bench.sh's results, a line a stream: its
# 68020 cycles a picture and each stage's (millions).
# GPL-3.0-or-later, (c) 2026 Brian King.
#
#   benchsum.py build/bench/*/prof.txt
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
order = ["IDCT", "motion comp.", "VLC/blocks", "demux/copy", "other"]
print("%-22s %8s  %s" % ("stream", "Mcyc/pic", "  ".join("%12s" % k for k in order)))
for f in sys.argv[1:]:
    tot = pics = skipped = None
    st = {}
    for line in open(f):
        m = re.match(r"total (\d+) cycles", line)
        if m:
            tot = int(m.group(1))
        m = re.match(r"(\d+) pictures, \S+, (\d+) B", line)
        if m:
            pics, skipped = int(m.group(1)), int(m.group(2))
        m = re.match(r"\s*([\d.]+)%\s+(\d+)\s+(.+)$", line)
        if m:
            import stages
            k = stages.stage(m.group(3).strip())
            st[k] = st.get(k, 0) + int(m.group(2))
    if not tot or not pics:
        print("%-22s (no result)" % os.path.basename(os.path.dirname(f)))
        continue
    print("%-22s %8.2f  %s" % (os.path.basename(os.path.dirname(f)), tot / pics / 1e6,
                               "  ".join("%12.2f" % (st.get(k, 0) / pics / 1e6) for k in order)))
