#!/usr/bin/env python3
# mpegv.library's tests (2.6): half size (mpv_test -half) against the whole
# picture averaged 2x2 (mpv_test, its pictures box-filtered), each
# picture's PSNR for Y, Cb and Cr; the worst and the mean, and the mean
# by a picture's place in its GOP (pictures since its I picture, in
# display order: the drift motion compensation at half size gathers), and
# whether the mean Y is above the threshold. Both are mpegv's own, so
# their pictures match one for one. Needs numpy.
# GPL-3.0-or-later, (c) 2026 Brian King.
#
#   halfpsnr.py build/host/mpv_test file [pictures]
import os
import subprocess
import sys
import tempfile

import numpy as np

THRESHOLD = 28.0
prog, path = sys.argv[1], sys.argv[2]
cap = int(sys.argv[3]) if len(sys.argv) > 3 else 0
tmp = tempfile.mkdtemp()


def decode(half):
    out = os.path.join(tmp, "half.yuv" if half else "whole.yuv")
    args = [prog, "-dump", out] + (["-half"] if half else []) + (["-n", str(cap)] if cap else []) + [path]
    lines = subprocess.run(args, capture_output=True, text=True, check=True).stdout.splitlines()
    pics = []
    for ln in lines:
        t = ln.split()
        if len(t) >= 3 and t[0].startswith("n"):
            w, h = (int(v) for v in t[1].split("x"))
            pics.append((int(t[0][1:]), w, h, t[2]))
    data = np.fromfile(out, np.uint8)
    os.remove(out)
    frames, o = [], 0
    for n, w, h, k in pics:
        cw, ch = w // 2, h // 2
        y = data[o:o + w * h].reshape(h, w)
        o += w * h
        cb = data[o:o + cw * ch].reshape(ch, cw)
        o += cw * ch
        cr = data[o:o + cw * ch].reshape(ch, cw)
        o += cw * ch
        frames.append((n, k, (y, cb, cr)))
    return frames


def box(p, h, w):
    p = p[:h * 2, :w * 2].astype(np.int32)
    return (p[0::2, 0::2] + p[1::2, 0::2] + p[0::2, 1::2] + p[1::2, 1::2] + 2) // 4


def psnr(x, y):
    d = x.astype(np.int32) - y
    mse = float((d * d).mean())
    return 99.0 if mse == 0 else 10 * np.log10(65025.0 / mse)


whole, half = decode(False), decode(True)
hw = {n: (k, pl) for n, k, pl in whole}
res, place = [], []
since = None
for n, k, pl in sorted(half, key=lambda f: f[0]):
    if n not in hw:
        continue
    if k == "I":
        since = 0
    elif since is not None:
        since += 1
    wk, wpl = hw[n]
    row = []
    for a, b in zip(pl, wpl):
        h, w = a.shape
        row.append(psnr(a, box(b, h, w)))
    res.append(row)
    place.append(since if since is not None else -1)
res = np.array(res)
worst, mean = res.min(axis=0), res.mean(axis=0)
ok = mean[0] >= THRESHOLD
w0 = half[0][2][0].shape
print("%-18s %4d pictures %dx%d  Y %5.1f/%5.1f  Cb %5.1f/%5.1f  Cr %5.1f/%5.1f dB (worst/mean)  %s"
      % (os.path.basename(path), len(res), w0[1], w0[0], worst[0], mean[0], worst[1], mean[1], worst[2],
         mean[2], "ok" if ok else "BELOW %.0f dB" % THRESHOLD))
# drift: mean Y by place in the GOP (display order from its I picture)
place = np.array(place)
cols = []
for s in sorted(set(place.tolist())):
    if s < 0:
        continue
    sel = res[place == s][:, 0]
    cols.append("%d:%.1f(%d)" % (s, sel.mean(), len(sel)))
print("  Y by pictures since the I picture: " + " ".join(cols[:16]) + (" ..." if len(cols) > 16 else ""))
sys.exit(0 if ok else 1)
