pl_mpeg: MPEG-1 video decoder, MP2 audio decoder and MPEG-PS demuxer, by
Dominic Szablewski (https://github.com/phoboslab/pl_mpeg), a single header,
at c871f2be022ece7ef4f64230b4fb8e1fb9eb6023 (30 December 2025), changed by
AmigaMCE for speed on a 68020 without an FPU (each change marked
"AmigaMCE:" in pl_mpeg.h), the pictures it decodes the same, bit for bit:

- bits read a longword at a time, and the video VLCs by tables of their
  first bits made from pl_mpeg's own trees; a DCT coefficient's code read
  with its sign from the same longword
- the IDCT only where a block has coefficients, by its even and odd halves
  (each rounding within itself; a half of zeros gives zeros), its rows
  straight into the picture, the rows' rounding carried in through the
  first column; block_data zeroed only where it was set
- x * 362 and x * 196 by shifts and adds, the dequantising by 16-bit
  multiplies (the same products)
- motion compensation and a block's one added value four pixels at a
  time, each byte's average or clamp as pl_mpeg's
- start codes looked for through what the buffer holds at once

And one addition: plm_video_set_skip_b(), B pictures passed over
undecoded (nothing is predicted from them) while AmigaMCE is behind, each
one's time still counted; the pictures it does decode are unchanged.

For mpegv.library (each marked "mpegv:"): a packet's time kept as stored
(90 kHz, plm_packet_t's pts90), the sequence header's rate and aspect
codes and bit rate kept (plm_video_t), and each frame's number in display
order and picture type (plm_frame_t's index and type). None changes a
picture.

Taken out for mpegv.library (lean: video and its demuxer only, and no
floating point, so no maths libraries): the MP2 audio decoder
(mpega.library is faster on a 68k, and does layers I-III), the plm_t
player and its callbacks, the demuxer's seek, start time and duration,
and every double (a packet's time in seconds, the video's frame rate,
pixel aspect and running time: the rate and aspect codes and 90 kHz times
are kept; a picture's time is worked out from its number by the caller).

Checked with tests/plm_ref.c and tests/mpv_test.c (each picture's hash,
pl_mpeg's own against the library's, built for this computer and for the
68020 under vamos: make test), and timed with tests/cycles.py (make prof).
These changes came from AmigaMCE, where tests/mpeg_bench.c checked them
against the original pl_mpeg's pictures.

Licence: MIT (LICENSE here, and in pl_mpeg.h), as mpegv.library's.
