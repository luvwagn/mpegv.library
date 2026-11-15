# mpegv.library

MPEG-1 and MPEG-2 video decoded by the CPU, for any Amiga program (68020
or better, no FPU needed), as `mpega.library` decodes MPEG audio. Made for
AmigaMCE, a media centre for the Amiga, which opens it at run time to play
MPEG files and DVDs where there is no ZZ9000 card.

- A Program Stream is demultiplexed as it is read: MPEG-1's (`.mpg`, a
  VCD's `.dat`) and MPEG-2's (a DVD's `.vob`: MPEG-2 packs, private
  stream 1's AC-3, DTS, LPCM and subpictures with their substream,
  navigation packets). Its video is decoded, its other packets handed to
  the program with their times, for `mpega.library` or `ac3.library`. A
  bare video stream (`.m1v`, `.m2v`) is decoded as it is.
- MPEG-2 main profile, 4:2:0 (a DVD's): interlaced and progressive,
  frame and field pictures, 3:2 pulldown flags (each picture's fields and
  how long it is shown), 4:3 and 16:9, 23.976 to 60 pictures a second.
  4:2:2 and 4:4:4 are refused cleanly.
- Pictures come out in display order as the decoder's own Y, Cb and Cr
  planes (not copied), with integer 90 kHz times: each picture's own time
  stamp, and its time from the first.
- B pictures can be passed over when the CPU is behind (two in three of a
  DVD's or a VCD's; nothing is predicted from them). After a seek,
  `MPVC_FLUSH` starts again at the next I picture.
- Helpers make a picture what a screen wants: packed YUV 4:2:2 for a
  graphics card's overlay (Picasso96's PIP, which scales it), RGB in eight
  formats for `WritePixelArray`, or 8-bit greys for an AGA screen.

The decoder is [libmpeg2](https://github.com/cisco-open-source/libmpeg2)
0.5.1 (Aaron Holtzman, Michel Lespinasse and others), made faster for a
68k: its IDCT does only what each block needs, its columns Arai, Agui and
Nakajima's transform in 68k assembler (five multiplies to libmpeg2's
eleven and two), its motion compensation works four pixels at a time,
its coefficients and start codes are read in fewer instructions, and the
output conversions are 68k loops or tables.

What is exact and what is not: since 2.1 the IDCT is not libmpeg2's to
the bit, but within IEEE 1180 (peak error 1, mean square error 0.015,
mean error 0.0004; libmpeg2's own: 0.013 and 0.0003), and as close to an
exact (floating-point) decoder as libmpeg2's: 66 dB of luma PSNR on
average over a DVD's film or a whole VCD, against ffmpeg's floating-point
IDCT. Everything else (the bitstream, motion compensation, the
demultiplexer, times, the output conversions) is libmpeg2's, or
mpegv's, to the bit: `make test` builds the library's C with 2.0's
IDCT (`MPEGV_EXACT_IDCT`) and checks every picture against libmpeg2's
own C as released, and checks the faster IDCT block by block (its 68k
columns against its C) and for IEEE 1180's accuracy. The changes are
marked `mpegv:` in `third_party/libmpeg2` and listed in its `CHANGES`.
The demultiplexer is the library's own.

## Licence

GPL-3.0-or-later (`LICENSE`): libmpeg2 is GPL-2.0-or-later. The interface
(`include/`, `mpegv_lib.sfd`, `mpegv.doc`) and the example are MIT, so any
program may be built against it; a program that opens the library at run
time is not bound by its licence. The Aminet archive carries the library
as built; this repository is its source.

## Using it

```c
#include <proto/mpegv.h>
struct Library *MpegvBase = OpenLibrary(MPEGVNAME, MPEGVVERSION);

static ULONG reader(register struct Hook *h __asm("a0"), register APTR mpv __asm("a2"),
                    register struct MpvRead *r __asm("a1"))
{
    return Read(fh, r->mr_Buffer, r->mr_Length);
}

struct Hook rh = { .h_Entry = (HOOKFUNC)reader };
APTR mpv = MpvOpenTags(MPVA_ReadHook, (ULONG)&rh, TAG_DONE);
struct MpvPicture *p;
LONG err;
while ((p = MpvDecode(mpv, &err)) || err == MPVE_MORE)
    if (p) {
        MpvToRGB(p, rgb, p->mpic_Width * 3, MPVR_RGB24);    /* or MpvToYUV422, MpvToGrey */
        /* show it at p->mpic_Time (90 kHz from the first picture), for p->mpic_Duration */
    }
MpvClose(mpv);
```

`mpegv.doc` describes every function; `examples/mpvplay.c` plays a file in a
window on an RTG screen, or in greys on AGA, passing B pictures over when
it falls behind.

Version 2 is source-compatible with version 1: a program built for 1 runs
unchanged. New in 2: `MpvExamine` (MPEG-2's details: profile and level,
interlace, the picture's shape as shown), each picture's fields, duration
and flags, `mp_SubStream` for private stream 1, `MPVA_VideoStream`,
`MPVC_FLUSH` and `MPVE_FORMAT`.

## AGA output

`MpvToGrey` gives 8-bit chunky greys for `WriteChunkyPixels` or a program's
own chunky-to-planar routine. Colour on AGA (HAM8) is not in the library
yet: it would have to be written afresh from the HAM8 encoding (RiVA's
renderers depend on its GPL-only decoder's tables and globals).

## Building

```
make            build/mpegv.library, build/mpvplay
make test       each picture against libmpeg2's own C (as released) on
                this computer, built with 2.0's exact IDCT; the IDCT
                block by block against the whole transform's C (its 68k
                columns too), and its IEEE 1180 accuracy; motion
                compensation block by block against libmpeg2's; the
                68k's coefficient decoding against slice.c's C on random
                streams; times,
                flags, helpers, B-skipping, pushing and flushing as
                expected, here and through the library on a 68020 under
                vamos (amitools)
make psnr       each fixture against ffmpeg's decode (PSNR)
make prof MPG=file N=60
                a 68020's cycles under vamos for N pictures, by stage and
                function
make lha        build/mpegv.lha for Aminet
```

Needs bebbo's m68k-amigaos-gcc (libnix), and for the tests a C compiler for
the build computer, the ffmpeg-made fixtures in `tests/fixtures` (720 x 576
interlaced, 720 x 480 film with 3:2 pulldown, a VOB with AC-3; MPEG-1 too)
and vamos. `tests/bench.sh` times a set of streams.

## Size and memory

The library is 81 KB (77 KB of code). A decoder takes, from exec
(MEMF_ANY):

| | VCD (352 x 240) | DVD (720 x 576) |
|---|---|---|
| Three pictures (4:2:0) | 380 KB | 1.87 MB |
| libmpeg2's slice buffer and state | 274 KB | 274 KB |
| The stream's buffers (16 KB reads), packet queue | about 210 KB | about 210 KB |
| In all | about 0.85 MB | about 2.35 MB |

## Speed

A 68020's cycles a picture, as vamos (Musashi) counts them: no caches, no
wait states, and nothing extra for a longword read across an odd address.
Every picture decoded, not shown (`tests/bench.sh`: 60 pictures of each
MPEG-1 stream, 36 of each DVD one):

| Stream | Size | Cycles a picture | IDCT | Motion comp. | Bitstream | Rest |
|---|---|---|---|---|---|---|
| weezer.mpg (a VCD) | 352 x 240, 29.97 | 11.5 M | 8.5 M | 1.0 M | 2.0 M | 0.1 M |
| goodtime.mpg | 352 x 240, 29.97 | 9.9 M | 6.7 M | 0.9 M | 2.2 M | 0.1 M |
| HaleBopp.mpg | 320 x 240, 24 | 5.3 M | 2.0 M | 2.2 M | 0.7 M | 0.3 M |
| alien.vob (a PAL DVD's film) | 720 x 576, 25 interlaced, 16:9 | 79.1 M | 52.1 M | 15.3 M | 11.0 M | 0.6 M |
| pal_int.vob | 720 x 576, 25 interlaced, 4:3 | 82.2 M | 57.6 M | 12.6 M | 11.1 M | 0.8 M |
| pal_film.vob | 720 x 576, 25 progressive, 16:9 | 84.2 M | 58.4 M | 13.7 M | 11.3 M | 0.8 M |
| ntsc_int.vob | 720 x 480, 29.97 interlaced | 72.9 M | 50.8 M | 12.0 M | 9.4 M | 0.7 M |
| ntsc_film.vob | 720 x 480, 23.976 progressive | 74.7 M | 51.3 M | 12.4 M | 10.3 M | 0.7 M |

The DVD streams other than alien's are made like a DVD's (MP@ML, IBBP,
GOP 12 or 15, 5-6 Mbit/s, AC-3). With B pictures passed over, each I or P
picture shown costs 99 M (alien): a third of the stream's pictures for
1.25 times the cycles of one picture each.

Showing a picture costs more besides: `MpvToYUV422` 8.5 M a DVD picture
(1.8 M a VCD one), `MpvToGrey` 9.1 M, `MpvToRGB` (RGB565) 45 M.

What that means, given how many of these cycles a second each machine
manages (the assumption in each case):

| Machine (assumed 68020-model cycles a second) | VCD (weezer) | DVD (alien), every picture | DVD, B pictures passed over |
|---|---|---|---|
| Amiberry, JIT (about 3.0 G: mpegv 1 measured there, 3.8 ms for 11.4 M) | about 260 a second | about 38 a second (34 with YUV for the overlay): 25 with time to spare | at ease |
| PiStorm, Emu68 (1.5-5 G: not yet measured) | 130-430 | 19-63 | 25 shown, 8 of them new, at 1.5 G |
| 68060 at 50 MHz (about 150 M) | about 13 | under 2 | under 2 new a second |

So a DVD plays at its full rate under Amiberry's JIT and (very likely) on a
PiStorm. A real 68060 decodes about 13 of a VCD's 30 pictures a second (B
pictures must be passed over), and a DVD not at all. Not yet measured on
real hardware.
