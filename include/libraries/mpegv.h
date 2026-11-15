/*
 * mpegv.library: MPEG-1 and MPEG-2 video decoded by the CPU, for any
 * program, as mpega.library decodes MPEG audio. A Program Stream (.mpg, a
 * VCD's .dat, a DVD's .vob) is demultiplexed as it is read, its video
 * decoded, its other packets (MPEG audio, private stream 1: a DVD's AC-3,
 * LPCM and subpictures; a DVD's navigation packets) handed to the caller;
 * a bare video stream (.m1v, .m2v) is decoded as it is. Pictures come out
 * in display order as the decoder's own Y, Cb and Cr planes (4:2:0), with
 * helpers that make them packed YUV for a graphics card's overlay, RGB to
 * draw, or grey (8-bit chunky) for an AGA screen.
 *
 * This header, mpegv_lib.sfd and mpegv.doc are MIT-licensed, (c) 2026
 * Brian King, so that any program may use the library. The library itself
 * is GPL-3.0-or-later (its decoder is libmpeg2: see its LICENSE); a
 * program that opens it at run time is not bound by that.
 *
 * Times are 90 kHz ticks, as MPEG stores them: no floating point in the
 * interface.
 */
#ifndef LIBRARIES_MPEGV_H
#define LIBRARIES_MPEGV_H

#include <exec/types.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>

#define MPEGVNAME       "mpegv.library"
#define MPEGVVERSION    2                   /* 2: MPEG-2 (libmpeg2); 1: MPEG-1 (pl_mpeg) */

/* ---- MpvOpen's tags */
#define MPVA_Dummy      (TAG_USER + 0x4D500000)
#define MPVA_ReadHook   (MPVA_Dummy + 1)    /* struct Hook *: the stream's bytes (struct MpvRead) */
#define MPVA_PacketHook (MPVA_Dummy + 2)    /* struct Hook *: its packets not video (struct MpvPacket) */
#define MPVA_Stream     (MPVA_Dummy + 3)    /* MPVS_: what the bytes are (default MPVS_AUTO) */
#define MPVA_ReadSize   (MPVA_Dummy + 4)    /* bytes asked of the read hook at once (default 16384) */
#define MPVA_SkipB      (MPVA_Dummy + 5)    /* BOOL: B pictures passed over from the start */
#define MPVA_VideoStream (MPVA_Dummy + 6)   /* (2) the video's stream id, 0xE0-0xEF (default 0: the first) */

/* MPVA_Stream */
#define MPVS_AUTO       0                   /* from its first bytes: a pack header or a sequence header */
#define MPVS_PROGRAM    1                   /* a Program Stream (packs): MPEG-1, or MPEG-2 (a DVD's VOB) */
#define MPVS_VIDEO      2                   /* a bare MPEG-1 or MPEG-2 video stream */

/* The read hook: called with the hook (A0), the decoder (A2) and this
 * (A1); returns the bytes it put at mr_Buffer (up to mr_Length), 0 at the
 * stream's end, or -1 if it could not be read. Without a read hook the
 * caller gives the bytes with MpvWrite. */
struct MpvRead {
    UBYTE *mr_Buffer;
    LONG   mr_Length;
};

/* The packet hook: each packet of a Program Stream that is not its video,
 * as it is demultiplexed: its stream id, its time if it has one, and its
 * bytes (the packet's payload, after its PES header; valid only during
 * the call). Returns nothing.
 *   0xC0-0xDF  MPEG audio
 *   0xBD       private stream 1: its first byte the substream (also in
 *              mp_SubStream): a DVD's 0x80-0x87 AC-3 and 0x88-0x8F DTS
 *              (3 more bytes before the frames: their count and the first
 *              one's offset), 0xA0-0xA7 LPCM (6 more), 0x20-0x3F
 *              subpictures
 *   0xBF       private stream 2: a DVD's navigation packets (PCI 0x00,
 *              DSI 0x01: the first byte), whole
 *   0xE0-0xEF  a video stream other than the one decoded
 * Padding, system headers and stream maps are not passed on. */
struct MpvPacket {
    ULONG  mp_Stream;                       /* its stream id (0xBD, 0xC0...) */
    ULONG  mp_Flags;                        /* MPVPF_PTS */
    ULONG  mp_PTS;                          /* its time, 90 kHz (the low 32 bits of 33) */
    UBYTE *mp_Data;
    ULONG  mp_Length;
    /* 2 */
    ULONG  mp_SubStream;                    /* 0xBD's first byte (0 for the others) */
};
#define MPVPF_PTS       (1UL << 0)          /* mp_PTS (mpic_PTS) is there */

/* ---- a picture: the decoder's own, valid until the next MpvDecode (or
 * MpvClose, or MPVC_FLUSH). Its planes are whole macroblocks (16 pixels)
 * wide and high; mpic_Width x mpic_Height of them is the picture. Y is
 * 0-255 studio range (16-235), Cb and Cr half as wide and half as high
 * (4:2:0). An interlaced picture is its two fields woven, as a frame. */
struct MpvPicture {
    UBYTE *mpic_Y;
    UBYTE *mpic_Cb;
    UBYTE *mpic_Cr;
    UWORD  mpic_Width, mpic_Height;         /* the picture */
    UWORD  mpic_YModulo;                    /* Y's bytes a row */
    UWORD  mpic_CModulo;                    /* Cb's and Cr's bytes a row */
    UWORD  mpic_YRows;                      /* Y's rows (Cb's and Cr's: half) */
    UWORD  mpic_Type;                       /* MPVT_ */
    ULONG  mpic_Number;                     /* in display order from 0 (B pictures passed over counted) */
    ULONG  mpic_Time;                       /* 90 kHz from the first picture's (by fields shown) */
    ULONG  mpic_PTS;                        /* 90 kHz: its packet's time, or carried on from the last one
                                             * (2.2: never from before a flush; an open GOP's first B
                                             * pictures timed back from the picture after them) */
    ULONG  mpic_Flags;                      /* MPVPF_ */
    /* 2 */
    ULONG  mpic_Duration;                   /* how long it is shown, 90 kHz (3 fields for a repeated one) */
    UWORD  mpic_Fields;                     /* fields it is shown for: 2, or 3 with MPVPF_REPEAT */
    UWORD  mpic_Reserved;
};
#define MPVPF_TOPFIRST  (1UL << 1)          /* (2) interlaced: its top field is shown first */
#define MPVPF_REPEAT    (1UL << 2)          /* (2) its first field is shown again (3:2 pulldown) */
#define MPVPF_PROGRESSIVE (1UL << 3)        /* (2) a progressive frame (always, in MPEG-1) */
#define MPVPF_FIELDS    (1UL << 4)          /* (2) coded as two field pictures, not one frame */

#define MPVT_I          1
#define MPVT_P          2
#define MPVT_B          3

/* ---- the stream, once its sequence header has been read. MpvGetInfo
 * fills the fields to mi_Kind (mpegv 1's); MpvExamine (2) all of them. */
struct MpvInfo {
    UWORD mi_Width, mi_Height;
    UWORD mi_RateCode;                      /* as stored: 1-8 (4: 29.97, 1: 23.976) */
    UWORD mi_AspectCode;                    /* as stored: MPEG-1 1-14 (a pixel's shape); MPEG-2 1-4 (the picture's) */
    ULONG mi_RateNum, mi_RateDen;           /* pictures a second: mi_RateNum / mi_RateDen (30000/1001) */
    ULONG mi_Period;                        /* one picture's time, 90 kHz (3003 at 29.97) */
    ULONG mi_ShownWidth;                    /* its width for its height as it is meant to be seen */
    ULONG mi_BitRate;                       /* bits a second, as the header says (0 not given) */
    ULONG mi_Kind;                          /* MPVS_PROGRAM or MPVS_VIDEO: what it turned out to be */
    /* 2: MpvExamine only */
    ULONG mi_Flags;                         /* MPVIF_ */
    UWORD mi_Profile;                       /* MPEG-2: 5 Simple, 4 Main, 3 SNR, 2 Spatial, 1 High (0: MPEG-1) */
    UWORD mi_Level;                         /* MPEG-2: 10 Low, 8 Main, 6 High-1440, 4 High (0: MPEG-1) */
    UWORD mi_Chroma;                        /* MPVCF_420: the only one decoded */
    UWORD mi_Reserved;
    UWORD mi_AspectNum, mi_AspectDen;       /* the picture's shape as shown: 4/3, 16/9 */
    UWORD mi_DisplayWidth, mi_DisplayHeight; /* the part meant to be shown (MPEG-2's display extension) */
};
#define MPVIF_MPEG2     (1UL << 0)          /* MPEG-2 video */
#define MPVIF_PROGRESSIVE (1UL << 1)        /* a progressive sequence (MPEG-1: always); else interlaced */
#define MPVIF_LOWDELAY  (1UL << 2)          /* no B pictures, each shown as decoded */
#define MPVIF_PACKS2    (1UL << 3)          /* MPEG-2 pack headers (a DVD's VOB) */
#define MPVCF_420       1                   /* MPEG-2's chroma_format code */

/* ---- MpvDecode's *error */
#define MPVE_OK         0                   /* a picture */
#define MPVE_END        1                   /* the stream's end: no more pictures */
#define MPVE_MORE       2                   /* (no read hook) more bytes wanted: MpvWrite them */
#define MPVE_READ       3                   /* the read hook said it could not read */
#define MPVE_NOMEM      4
#define MPVE_STREAM     5                   /* not an MPEG stream mpegv decodes */
#define MPVE_FORMAT     6                   /* (2) MPEG-2 that is not 4:2:0 (4:2:2, 4:4:4): not decoded */

/* ---- MpvControl's commands */
#define MPVC_SKIPB      1                   /* value: B pictures passed over (TRUE) or not; returns the last */
#define MPVC_SKIPPEDB   2                   /* returns how many B pictures were passed over */
#define MPVC_ENDED      3                   /* returns TRUE once the stream's end was reached */
#define MPVC_FIRSTPTS   4                   /* returns the first video time (90 kHz), or ~0 if none yet */
#define MPVC_END        5                   /* (no read hook) no more bytes: what is held is decoded */
#define MPVC_HELD       6                   /* returns bytes read but not yet decoded or handed on */
#define MPVC_FLUSH      7                   /* (2) after a seek: what is held dropped, the decoder's
                                             * pictures too; it starts again at the next I picture
                                             * (with a read hook: from its next bytes) */

/* ---- MpvToYUV422's order: packed 4:2:2, two pixels a longword */
#define MPVY_Y0UY1V     0                   /* Y0 Cb Y1 Cr: Picasso96's RGBFB_YUV422CGX as the ZZ9000 shows it */
#define MPVY_UY0VY1     1                   /* Cb Y0 Cr Y1 (UYVY) */
#define MPVY_Y1UY0V     2                   /* Y1 Cb Y0 Cr: Picasso96's RGBFB_YUV422 */
#define MPVY_Y0VY1U     3                   /* Y0 Cr Y1 Cb */

/* ---- MpvToRGB's formats */
#define MPVR_RGB24      0                   /* R G B: WritePixelArray's RECTFMT_RGB */
#define MPVR_BGR24      1
#define MPVR_ARGB32     2                   /* A (0xFF) R G B: RECTFMT_ARGB */
#define MPVR_RGBA32     3
#define MPVR_BGRA32     4
#define MPVR_RGB565     5                   /* big-endian (Picasso96's RGBFB_R5G6B5) */
#define MPVR_RGB565PC   6                   /* little-endian (RGBFB_R5G6B5PC) */
#define MPVR_RGB555     7                   /* big-endian, 0RRRRRGG GGGBBBBB (RGBFB_R5G5B5) */

/* ---- MpvToGrey's levels: 0 or 256: Y made full range 0-255; 2-255: that
 * many greys, 0 to levels - 1 (pens for a screen's grey palette) */

#endif /* LIBRARIES_MPEGV_H */
