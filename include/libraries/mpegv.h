/*
 * mpegv.library: MPEG-1 video decoded by the CPU, for any program, as
 * mpega.library decodes MPEG audio. A Program Stream (.mpg, a VCD's .dat)
 * is demultiplexed as it is read, its video decoded, its other packets
 * (MPEG audio, private stream 1) handed to the caller; a bare video
 * stream (.m1v) is decoded as it is. Pictures come out in display order
 * as the decoder's own Y, Cb and Cr planes (4:2:0), with helpers that
 * make them packed YUV for a graphics card's overlay, RGB to draw, or
 * grey (8-bit chunky) for an AGA screen.
 *
 * MIT licence: (c) 2026 Brian King; the decoder is pl_mpeg, MIT,
 * (c) Dominic Szablewski (see the library's LICENSE).
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
#define MPEGVVERSION    1

/* ---- MpvOpen's tags */
#define MPVA_Dummy      (TAG_USER + 0x4D500000)
#define MPVA_ReadHook   (MPVA_Dummy + 1)    /* struct Hook *: the stream's bytes (struct MpvRead) */
#define MPVA_PacketHook (MPVA_Dummy + 2)    /* struct Hook *: its packets not video (struct MpvPacket) */
#define MPVA_Stream     (MPVA_Dummy + 3)    /* MPVS_: what the bytes are (default MPVS_AUTO) */
#define MPVA_ReadSize   (MPVA_Dummy + 4)    /* bytes asked of the read hook at once (default 16384) */
#define MPVA_SkipB      (MPVA_Dummy + 5)    /* BOOL: B pictures passed over from the start */

/* MPVA_Stream */
#define MPVS_AUTO       0                   /* from its first bytes: a pack header or a sequence header */
#define MPVS_PROGRAM    1                   /* an MPEG-1 Program Stream (packs) */
#define MPVS_VIDEO      2                   /* a bare MPEG-1 video stream */

/* The read hook: called with the hook (A0), the decoder (A2) and this
 * (A1); returns the bytes it put at mr_Buffer (up to mr_Length), 0 at the
 * stream's end, or -1 if it could not be read. Without a read hook the
 * caller gives the bytes with MpvWrite. */
struct MpvRead {
    UBYTE *mr_Buffer;
    LONG   mr_Length;
};

/* The packet hook: each packet of a Program Stream that is not its video,
 * as it is demultiplexed: its stream id (0xC0-0xDF MPEG audio, 0xBD
 * private stream 1: its first byte the substream), its time if it has
 * one, and its bytes (valid only during the call). Returns nothing. */
struct MpvPacket {
    ULONG  mp_Stream;                       /* its stream id (0xBD, 0xC0...) */
    ULONG  mp_Flags;                        /* MPVPF_ */
    ULONG  mp_PTS;                          /* its time, 90 kHz (the low 32 bits of 33) */
    UBYTE *mp_Data;
    ULONG  mp_Length;
};
#define MPVPF_PTS       (1UL << 0)          /* mp_PTS is there */

/* ---- a picture: the decoder's own, valid until the next MpvDecode (or
 * MpvClose). Its planes are whole macroblocks (16 pixels) wide and high;
 * mpic_Width x mpic_Height of them is the picture. Y is 0-255 studio
 * range (16-235), Cb and Cr half as wide and half as high (4:2:0). */
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
    ULONG  mpic_Time;                       /* 90 kHz from the first picture's */
    ULONG  mpic_PTS;                        /* 90 kHz: the stream's first video time + mpic_Time */
    ULONG  mpic_Flags;                      /* MPVPF_PTS: the stream had a video time (a Program Stream's) */
};
#define MPVT_I          1
#define MPVT_P          2
#define MPVT_B          3

/* ---- the stream, once its sequence header has been read (MpvGetInfo) */
struct MpvInfo {
    UWORD mi_Width, mi_Height;
    UWORD mi_RateCode;                      /* as stored: 1-8 */
    UWORD mi_AspectCode;                    /* as stored: 1-14 (a pixel's shape) */
    ULONG mi_RateNum, mi_RateDen;           /* pictures a second: mi_RateNum / mi_RateDen (30000/1001) */
    ULONG mi_Period;                        /* one picture's time, 90 kHz (3003 at 29.97) */
    ULONG mi_ShownWidth;                    /* its width for its height as it is meant to be seen */
    ULONG mi_BitRate;                       /* bits a second, as the header says (0 not given) */
    ULONG mi_Kind;                          /* MPVS_PROGRAM or MPVS_VIDEO: what it turned out to be */
};

/* ---- MpvDecode's *error */
#define MPVE_OK         0                   /* a picture */
#define MPVE_END        1                   /* the stream's end: no more pictures */
#define MPVE_MORE       2                   /* (no read hook) more bytes wanted: MpvWrite them */
#define MPVE_READ       3                   /* the read hook said it could not read */
#define MPVE_NOMEM      4
#define MPVE_STREAM     5                   /* not an MPEG-1 stream mpegv decodes */

/* ---- MpvControl's commands */
#define MPVC_SKIPB      1                   /* value: B pictures passed over (TRUE) or not; returns the last */
#define MPVC_SKIPPEDB   2                   /* returns how many B pictures were passed over */
#define MPVC_ENDED      3                   /* returns TRUE once the stream's end was reached */
#define MPVC_FIRSTPTS   4                   /* returns the first video time (90 kHz), or ~0 if none yet */
#define MPVC_END        5                   /* (no read hook) no more bytes: what is held is decoded */
#define MPVC_HELD       6                   /* returns bytes read but not yet decoded or handed on */

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
