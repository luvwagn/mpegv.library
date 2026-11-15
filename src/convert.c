/*
 * mpegv.library's helpers: a picture (the decoder's 4:2:0 planes) made
 * what a screen wants: packed YUV 4:2:2 for a graphics card's overlay,
 * RGB to draw, or grey for an AGA screen. They read only the picture they
 * are given, so any task may call them.
 *
 * The RGB conversion is pl_mpeg's (BT.601, integer; MIT, (c) Dominic
 * Szablewski: LICENSE), as mpegv 1 converted.
 *
 * (c) 2026 Brian King. GPL-3.0-or-later (LICENSE).
 */
#include <exec/types.h>
#include <libraries/mpegv.h>

#define API

/* (the 68k's order in memory: big-endian; the tests on this computer
 * store the same bytes a byte at a time) */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define MPV_BIG 0
static inline void put32(ULONG *d, ULONG v)
{
    UBYTE *b = (UBYTE *)d;
    b[0] = (UBYTE)(v >> 24);
    b[1] = (UBYTE)(v >> 16);
    b[2] = (UBYTE)(v >> 8);
    b[3] = (UBYTE)v;
}
static inline void put16(UBYTE *b, UWORD v)
{
    b[0] = (UBYTE)(v >> 8);
    b[1] = (UBYTE)v;
}
#else
#define MPV_BIG 1
#define put32(d, v) (*(d) = (v))
#define put16(b, v) (*(UWORD *)(b) = (v))
#endif

static inline UBYTE clamp(int n)
{
    if ((unsigned int)n > 255)
        n = n < 0 ? 0 : 255;
    return (UBYTE)n;
}

API VOID MpvToYUV422(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG order)
{
    ULONG r, x, n;
    if (!pic || !dst)
        return;
    n = pic->mpic_Width / 2;
    if (order == MPVY_Y0UY1V) {
        /* a longword at a time (a graphics card's memory, across the bus) */
        for (r = 0; r < pic->mpic_Height; r++) {
            const UWORD *y = (const UWORD *)(pic->mpic_Y + r * pic->mpic_YModulo);
            const UBYTE *cb = pic->mpic_Cb + (r >> 1) * pic->mpic_CModulo;
            const UBYTE *cr = pic->mpic_Cr + (r >> 1) * pic->mpic_CModulo;
            ULONG *d = (ULONG *)(dst + r * modulo);
            for (x = 0; x < n; x++) {
#if MPV_BIG
                ULONG yy = *y++;
                *d++ = ((yy & 0xFF00) << 16) | ((ULONG)*cb++ << 16) | ((yy & 0xFF) << 8) | *cr++;
#else
                const UBYTE *yb = (const UBYTE *)y++;
                put32(d++, ((ULONG)yb[0] << 24) | ((ULONG)*cb++ << 16) | ((ULONG)yb[1] << 8) | *cr++);
#endif
            }
        }
        return;
    }
    {
        /* the others: each byte's place (from the top) */
        static const UBYTE at[4][4] = { /* y0, cb, y1, cr */
            { 24, 16, 8, 0 }, { 16, 24, 0, 8 }, { 8, 16, 24, 0 }, { 24, 0, 8, 16 } };
        const UBYTE *s = at[order & 3];
        for (r = 0; r < pic->mpic_Height; r++) {
            const UBYTE *y = pic->mpic_Y + r * pic->mpic_YModulo;
            const UBYTE *cb = pic->mpic_Cb + (r >> 1) * pic->mpic_CModulo;
            const UBYTE *cr = pic->mpic_Cr + (r >> 1) * pic->mpic_CModulo;
            ULONG *d = (ULONG *)(dst + r * modulo);
            for (x = 0; x < n; x++, y += 2)
                put32(d++, ((ULONG)y[0] << s[0]) | ((ULONG)*cb++ << s[1]) | ((ULONG)y[1] << s[2]) |
                               ((ULONG)*cr++ << s[3]));
        }
    }
}

/* 2 x 2 pixels at a time (one Cb and Cr), pl_mpeg's sums: bpp bytes a
 * pixel, red, green and blue at ri, gi, bi; or 16 bits (bpp 2) */
#define PIXEL(YO, DO)                                                       \
    yv = ((yp[YO] - 16) * 76309) >> 16;                                     \
    if (bpp == 2) {                                                         \
        UBYTE rr = clamp(yv + rv), gg = clamp(yv - gv), bb = clamp(yv + bv); \
        UWORD v = fmt == MPVR_RGB555                                        \
                      ? (UWORD)(((rr & 0xF8) << 7) | ((gg & 0xF8) << 2) | (bb >> 3)) \
                      : (UWORD)(((rr & 0xF8) << 8) | ((gg & 0xFC) << 3) | (bb >> 3)); \
        if (fmt == MPVR_RGB565PC)                                           \
            v = (UWORD)((v << 8) | (v >> 8));                               \
        put16(dp + (DO), v);                                                \
    } else {                                                                \
        dp[(DO) + ri] = clamp(yv + rv);                                     \
        dp[(DO) + gi] = clamp(yv - gv);                                     \
        dp[(DO) + bi] = clamp(yv + bv);                                     \
    }

static void to_rgb(const struct MpvPicture *pic, UBYTE *dst, int stride, int bpp, int ri, int gi,
                   int bi, ULONG fmt)
{
    int cols = pic->mpic_Width >> 1, rows = pic->mpic_Height >> 1;
    int yw = pic->mpic_YModulo, cw = pic->mpic_CModulo, row, col;
    for (row = 0; row < rows; row++) {
        const UBYTE *yp = pic->mpic_Y + row * 2 * yw;
        const UBYTE *cbp = pic->mpic_Cb + row * cw, *crp = pic->mpic_Cr + row * cw;
        UBYTE *dp = dst + row * 2 * stride;
        for (col = 0; col < cols; col++) {
            int yv, cr = *crp++ - 128, cb = *cbp++ - 128;
            int rv = (cr * 104597) >> 16;
            int gv = (cb * 25674 + cr * 53278) >> 16;
            int bv = (cb * 132201) >> 16;
            PIXEL(0, 0);
            PIXEL(1, bpp);
            PIXEL(yw, stride);
            PIXEL(yw + 1, stride + bpp);
            yp += 2;
            dp += 2 * bpp;
        }
    }
}

API VOID MpvToRGB(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG format)
{
    if (!pic || !dst)
        return;
    switch (format) {
    case MPVR_RGB24:
        to_rgb(pic, dst, (int)modulo, 3, 0, 1, 2, format);
        break;
    case MPVR_BGR24:
        to_rgb(pic, dst, (int)modulo, 3, 2, 1, 0, format);
        break;
    case MPVR_ARGB32:
    case MPVR_RGBA32:
    case MPVR_BGRA32: {
        /* the colours leave the alpha byte as it was: 0xFF first */
        ULONG r, x;
        for (r = 0; r < pic->mpic_Height; r++) {
            ULONG *d = (ULONG *)(dst + r * modulo);
            for (x = 0; x < pic->mpic_Width; x++)
                put32(d + x, format == MPVR_ARGB32 ? 0xFF000000UL : 0x000000FFUL);
        }
        if (format == MPVR_ARGB32)
            to_rgb(pic, dst, (int)modulo, 4, 1, 2, 3, format);
        else if (format == MPVR_RGBA32)
            to_rgb(pic, dst, (int)modulo, 4, 0, 1, 2, format);
        else
            to_rgb(pic, dst, (int)modulo, 4, 2, 1, 0, format);
        break;
    }
    case MPVR_RGB565:
    case MPVR_RGB565PC:
    case MPVR_RGB555:
        to_rgb(pic, dst, (int)modulo, 2, 0, 0, 0, format);
        break;
    }
}

API VOID MpvToGrey(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG levels)
{
    UBYTE lut[256];
    ULONG i, r, x;
    if (!pic || !dst)
        return;
    if (!levels || levels > 256)
        levels = 256;
    for (i = 0; i < 256; i++) {
        LONG v = ((LONG)i - 16) * 255 / 219;    /* studio range made full */
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        lut[i] = (UBYTE)((ULONG)v * levels / 256);
    }
    for (r = 0; r < pic->mpic_Height; r++) {
        const UBYTE *y = pic->mpic_Y + r * pic->mpic_YModulo;
        UBYTE *d = dst + r * modulo;
        for (x = 0; x < pic->mpic_Width; x++)
            d[x] = lut[y[x]];
    }
}
