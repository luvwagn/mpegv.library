# mpegv.library

MPEG-1 video decoded by the CPU, for any Amiga program (68020 or better,
no FPU needed), as `mpega.library` decodes MPEG audio. Made for
AmigaMCE, a media centre for the Amiga, which opens it at run time to play
MPEG-1 where there is no ZZ9000 card.

- A Program Stream (`.mpg`, a VCD's `.dat`) is demultiplexed as it is
  read; its video decoded, its other packets (MPEG audio, private
  stream 1) handed to the program, with their times, for `mpega.library`
  or `ac3.library`. A bare video stream (`.m1v`) is decoded as it is.
- Pictures come out in display order as the decoder's own Y, Cb and Cr
  planes (not copied), with integer 90 kHz times.
- B pictures can be passed over when the CPU is behind (a VCD's are two in
  three; nothing is predicted from them).
- Helpers make a picture what a screen wants: packed YUV 4:2:2 for a
  graphics card's overlay (Picasso96's PIP, which scales it), RGB in eight
  formats for `WritePixelArray`, or 8-bit greys for an AGA screen.

The decoder is [pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic
Szablewski, made about 2.6 times faster on a 68020 than it was (each change
marked `AmigaMCE:` or `mpegv:` in `third_party/pl_mpeg/pl_mpeg.h`); its
pictures are the same, bit for bit. MIT licence throughout (`LICENSE`).

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
        /* show it at p->mpic_Time (90 kHz from the first picture) */
    }
MpvClose(mpv);
```

`mpegv.doc` describes every function; `examples/mpvplay.c` plays a file in a
window on an RTG screen, or in greys on AGA.

## AGA output

`MpvToGrey` gives 8-bit chunky greys for `WriteChunkyPixels` or a program's
own chunky-to-planar routine. Colour on AGA (HAM8) is not in the library:
RiVA's AGA renderers (Stephen Fellner, GPL or MIT) were looked at, but their
HAM8 lookup tables are built by RiVA's decoder, which is GPL only, and the
renderers patch their own code and keep their state in RiVA's globals; a
HAM8 helper here would have to be written afresh from the HAM8 encoding
itself. Wanted for version 2.

## Building

```
make            build/mpegv.library, build/mpvplay
make test       its pictures against pl_mpeg's own, bit for bit, on this
                computer and on a 68020 under vamos (amitools)
make prof MPG=file.mpg N=60
                a 68020's cycles under vamos: pl_mpeg built in, and the
                same through the library
make lha        build/mpegv.lha for Aminet
```

Needs bebbo's m68k-amigaos-gcc (libnix), and for the tests a C compiler for
the build computer, ffmpeg-made fixtures (in `tests/fixtures`) and vamos.

## Speed

60 pictures of a VCD (352 x 240), as vamos counts a 68020's cycles:

| | cycles |
|---|---|
| pl_mpeg built into the program | 688,791,031 |
| through mpegv.library | 688,914,798 (+0.02%) |

About 11.5 million cycles a picture. An emulated 68060 decoded one in
about 4 ms (a picture lasts 33 ms); a 68030 or 68020 needs B pictures passed
over, and still shows fewer than there are. Not yet measured on real
hardware.
