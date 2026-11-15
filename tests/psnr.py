#!/usr/bin/env python3
# mpegv.library's tests: a stream's pictures as mpegv decodes them (built
# for this computer: mpv_test -dump) against ffmpeg's, each picture's PSNR
# for Y, Cb and Cr; the worst and the mean, and whether the worst Y is
# above the threshold (45 dB: nothing to be seen; libmpeg2 and ffmpeg
# differ only in their IDCTs' rounding, and that drifts a little along a
# GOP). Pictures are matched by their display numbers (mpegv passes over
# B pictures it cannot decode at a stream's start: ffmpeg may show them).
# Needs ffmpeg and numpy.
# GPL-3.0-or-later, (c) 2026 Brian King.
#
#   psnr.py build/host/mpv_test file [pictures]
import os
import subprocess
import sys
import tempfile

import numpy as np

THRESHOLD = 45.0
prog, path = sys.argv[1], sys.argv[2]
cap = int(sys.argv[3]) if len(sys.argv) > 3 else 0
tmp = tempfile.mkdtemp()
ours, theirs = os.path.join(tmp, "mpv.yuv"), os.path.join(tmp, "ff.yuv")
args = [prog, "-dump", ours] + (["-n", str(cap)] if cap else []) + [path]
lines = subprocess.run(args, capture_output=True, text=True, check=True).stdout.split()
nums = [int(t[1:]) for t in lines if t.startswith("n")]
w, h = (int(v) for v in lines[1].split("x"))
subprocess.run(["ffmpeg", "-v", "fatal", "-y", "-i", path, "-map", "0:v:0", "-fps_mode", "passthrough",
                "-f", "rawvideo", "-pix_fmt", "yuv420p", theirs], check=True)
fs = w * h * 3 // 2
a = np.fromfile(ours, np.uint8)
b = np.fromfile(theirs, np.uint8)
na, nb = len(a) // fs, len(b) // fs
a = a[:na * fs].reshape(na, fs).astype(np.int32)
b = b[:nb * fs].reshape(nb, fs).astype(np.int32)
planes = [(0, w * h), (w * h, w * h * 5 // 4), (w * h * 5 // 4, fs)]


def psnr(x, y):
    d = x - y
    mse = float((d * d).mean())
    return 99.0 if mse == 0 else 10 * np.log10(65025.0 / mse)


# ffmpeg's first picture: mpegv's number 0, or (if ffmpeg passed over the
# same B pictures) its first picture's: whichever matches better
cands = [o for o in sorted({0, nums[0]}) if 0 <= nums[0] - o < nb]
off = max(cands, key=lambda o: sum(psnr(a[i][:w * h], b[nums[i] - o][:w * h])
                                   for i in range(min(5, len(nums))) if nums[i] - o < nb))
res = []
for i, n in enumerate(nums):
    k = n - off
    if 0 <= k < nb:
        res.append([psnr(a[i][s:e], b[k][s:e]) for s, e in planes])
res = np.array(res)
worst, mean = res.min(axis=0), res.mean(axis=0)
ok = worst[0] >= THRESHOLD
print("%-18s %3d pictures  Y %5.1f/%5.1f  Cb %5.1f/%5.1f  Cr %5.1f/%5.1f dB (worst/mean)  %s"
      % (os.path.basename(path), len(res), worst[0], mean[0], worst[1], mean[1], worst[2], mean[2],
         "ok" if ok else "BELOW %.0f dB" % THRESHOLD))
sys.exit(0 if ok else 1)
