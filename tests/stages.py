#!/usr/bin/env python3
# mpegv.library's tests: tests/prof.py's functions put in the decoder's
# stages, each one's share of the cycles and its cycles a picture:
#   IDCT          the inverse DCT, and its adding to (or putting in) the
#                 picture
#   motion comp.  the prediction: blocks copied or averaged from the
#                 reference pictures
#   VLC/blocks    the bitstream: macroblock headers, motion vectors, the
#                 coefficients' codes, dequantising
#   demux/copy    the Program Stream's packets, libmpeg2's start codes and
#                 copying, the rest of libmpeg2 (headers, its parsing)
#   other         vamos, the program, the C library
# GPL-3.0-or-later, (c) 2026 Brian King.
#
#   stages.py prof.txt [pictures]
import re
import sys


def stage(n):
    if "idct" in n or n.startswith("mv_cols"):
        return "IDCT"
    if n.startswith("MC_") or n.startswith("motion_") or n.startswith("mpeg2_mc") or n.startswith("mc_"):
        return "motion comp."
    if n.startswith("get_") or n.startswith("slice_") or n == "mpeg2_slice" or n.startswith("decode_"):
        return "VLC/blocks"
    if n.startswith("mpeg2_") or n in ("step", "packet", "queue", "give", "more", "run", "begin", "shown",
                                        "copy_chunk", "skip_chunk", "seek_chunk", "memcpy", "memmove"):
        return "demux/copy"
    return "other"


def main():
    tot, st, pics = None, {}, None
    for line in open(sys.argv[1]):
        m = re.match(r"total (\d+) cycles", line)
        if m:
            tot = int(m.group(1))
            continue
        m = re.match(r"(\d+) pictures", line)
        if m:
            pics = int(m.group(1))
            continue
        m = re.match(r"\s*([\d.]+)%\s+(\d+)\s+(.+)$", line)
        if m:
            k = stage(m.group(3).strip())
            st[k] = st.get(k, 0) + int(m.group(2))
    if len(sys.argv) > 2:
        pics = int(sys.argv[2])
    if not tot:
        sys.exit("no total in " + sys.argv[1])
    per = (" %.2fM cycles a picture" % (tot / pics / 1e6)) if pics else ""
    print("total %d cycles%s" % (tot, per))
    for k, v in sorted(st.items(), key=lambda x: -x[1]):
        print("  %-13s %5.1f%%%s" % (k, 100.0 * v / tot, (" %7.2fM" % (v / pics / 1e6)) if pics else ""))


if __name__ == "__main__":
    main()
