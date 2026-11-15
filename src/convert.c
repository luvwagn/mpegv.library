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

/* on a 68020 or later, the YUV and grey loops are src/convert_68k.s's */
#if defined(__mc68020__) && !defined(MPEGV_NO_ASM)
#define MV_CONVERT68K
void mv_yuv_y0uy1v68k(ULONG *dst, const UBYTE *y, const UBYTE *cb, const UBYTE *cr, ULONG n);
void mv_grey68k(ULONG *dst, const UBYTE *y, const UBYTE *lut, ULONG n);
#endif

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
#ifdef MV_CONVERT68K
    if (order == MPVY_Y0UY1V && n >= 2) {
        /* four pixels at a time (src/convert_68k.s), the last two (if
         * any) as below */
        for (r = 0; r < pic->mpic_Height; r++) {
            const UBYTE *y = pic->mpic_Y + r * pic->mpic_YModulo;
            const UBYTE *cb = pic->mpic_Cb + (r >> 1) * pic->mpic_CModulo;
            const UBYTE *cr = pic->mpic_Cr + (r >> 1) * pic->mpic_CModulo;
            ULONG *d = (ULONG *)(dst + r * modulo);
            mv_yuv_y0uy1v68k(d, y, cb, cr, n >> 1);
            if (n & 1) {
                x = n - 1;
                d[x] = ((ULONG)y[2 * x] << 24) | ((ULONG)cb[x] << 16) | ((ULONG)y[2 * x + 1] << 8) | cr[x];
            }
        }
        return;
    }
#endif
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

/* pl_mpeg's sums, as tables made once (the same values: each product's
 * shift is taken before the sum only where pl_mpeg takes it):
 *   y = ((Y - 16) * 76309) >> 16             mv_y
 *   r = (cr * 104597) >> 16                  mv_r
 *   g = (cb * 25674 + cr * 53278) >> 16      (mv_gb + mv_gr) >> 16
 *   b = (cb * 132201) >> 16                  mv_b
 * and each colour's clamping (and its place in a 16-bit pixel) a table
 * indexed by y + r, y - g, y + b (-320 to 639: all they can be). */
#define MV_LO 320
#define MV_SPAN 960
static WORD mv_y[256], mv_r[256], mv_b[256];
static LONG mv_gb[256], mv_gr[256];
static UBYTE mv_clamp[MV_SPAN];
static UWORD mv_pack[2][3][MV_SPAN];      /* RGB565, RGB555: red, green, blue */
static volatile BOOL mv_tables;

static void make_tables(void)
{
    int i;
    if (mv_tables)
        return;
    for (i = 0; i < 256; i++) {
        mv_y[i] = (WORD)(((i - 16) * 76309) >> 16);
        mv_r[i] = (WORD)(((i - 128) * 104597) >> 16);
        mv_b[i] = (WORD)(((i - 128) * 132201) >> 16);
        mv_gb[i] = (i - 128) * 25674;
        mv_gr[i] = (i - 128) * 53278;
    }
    for (i = 0; i < MV_SPAN; i++) {
        UBYTE c = clamp(i - MV_LO);
        mv_clamp[i] = c;
        mv_pack[0][0][i] = (UWORD)((c & 0xF8) << 8);
        mv_pack[0][1][i] = (UWORD)((c & 0xFC) << 3);
        mv_pack[0][2][i] = (UWORD)(c >> 3);
        mv_pack[1][0][i] = (UWORD)((c & 0xF8) << 7);
        mv_pack[1][1][i] = (UWORD)((c & 0xF8) << 2);
        mv_pack[1][2][i] = (UWORD)(c >> 3);
    }
    mv_tables = TRUE;                       /* (last: another task sees it only when made) */
}

/* 2 x 2 pixels at a time (one Cb and Cr): 3 or 4 bytes a pixel, red,
 * green and blue at ri, gi, bi */
static inline __attribute__((always_inline)) void to_rgb(const struct MpvPicture *pic, UBYTE *dst, int stride, int bpp, int ri, int gi,
                   int bi)
{
    int cols = pic->mpic_Width >> 1, rows = pic->mpic_Height >> 1;
    int yw = pic->mpic_YModulo, cw = pic->mpic_CModulo, row, col;
    const UBYTE *cl = mv_clamp + MV_LO;
    make_tables();
    for (row = 0; row < rows; row++) {
        const UBYTE *y0 = pic->mpic_Y + row * 2 * yw, *y1 = y0 + yw;
        const UBYTE *cbp = pic->mpic_Cb + row * cw, *crp = pic->mpic_Cr + row * cw;
        UBYTE *d0 = dst + row * 2 * stride, *d1 = d0 + stride;
        for (col = 0; col < cols; col++) {
            int cr = *crp++, cb = *cbp++, yv;
            int rv = mv_r[cr], gv = (mv_gb[cb] + mv_gr[cr]) >> 16, bv = mv_b[cb];
#define MV_RGB(yp, dp)                \
    yv = mv_y[*yp++];                 \
    dp[ri] = cl[yv + rv];             \
    dp[gi] = cl[yv - gv];             \
    dp[bi] = cl[yv + bv];             \
    dp += bpp;
            MV_RGB(y0, d0);
            MV_RGB(y0, d0);
            MV_RGB(y1, d1);
            MV_RGB(y1, d1);
#undef MV_RGB
        }
    }
}

/* the same, 16 bits a pixel: pk the format's packing tables */
static inline __attribute__((always_inline)) void to_rgb16(const struct MpvPicture *pic, UBYTE *dst, int stride, ULONG fmt)
{
    int cols = pic->mpic_Width >> 1, rows = pic->mpic_Height >> 1;
    int yw = pic->mpic_YModulo, cw = pic->mpic_CModulo, row, col;
    const UWORD *pr, *pg, *pb;
    make_tables();
    pr = mv_pack[fmt == MPVR_RGB555][0] + MV_LO;
    pg = mv_pack[fmt == MPVR_RGB555][1] + MV_LO;
    pb = mv_pack[fmt == MPVR_RGB555][2] + MV_LO;
    for (row = 0; row < rows; row++) {
        const UBYTE *y0 = pic->mpic_Y + row * 2 * yw, *y1 = y0 + yw;
        const UBYTE *cbp = pic->mpic_Cb + row * cw, *crp = pic->mpic_Cr + row * cw;
        UBYTE *d0 = dst + row * 2 * stride, *d1 = d0 + stride;
        for (col = 0; col < cols; col++) {
            int cr = *crp++, cb = *cbp++, yv;
            const UWORD *r = pr + mv_r[cr], *g = pg - ((mv_gb[cb] + mv_gr[cr]) >> 16), *b = pb + mv_b[cb];
            UWORD v;
#define MV_RGB16(yp, dp)                                  \
    yv = mv_y[*yp++];                                     \
    v = r[yv] | g[yv] | b[yv];                            \
    if (fmt == MPVR_RGB565PC)                             \
        v = (UWORD)((v << 8) | (v >> 8));                 \
    put16(dp, v);                                         \
    dp += 2;
            MV_RGB16(y0, d0);
            MV_RGB16(y0, d0);
            MV_RGB16(y1, d1);
            MV_RGB16(y1, d1);
#undef MV_RGB16
        }
    }
}

API VOID MpvToRGB(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG format)
{
    if (!pic || !dst)
        return;
    switch (format) {
    case MPVR_RGB24:
        to_rgb(pic, dst, (int)modulo, 3, 0, 1, 2);
        break;
    case MPVR_BGR24:
        to_rgb(pic, dst, (int)modulo, 3, 2, 1, 0);
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
            to_rgb(pic, dst, (int)modulo, 4, 1, 2, 3);
        else if (format == MPVR_RGBA32)
            to_rgb(pic, dst, (int)modulo, 4, 0, 1, 2);
        else
            to_rgb(pic, dst, (int)modulo, 4, 2, 1, 0);
        break;
    }
    case MPVR_RGB565:
    case MPVR_RGB565PC:
    case MPVR_RGB555:
        if (format == MPVR_RGB565)
            to_rgb16(pic, dst, (int)modulo, MPVR_RGB565);
        else if (format == MPVR_RGB565PC)
            to_rgb16(pic, dst, (int)modulo, MPVR_RGB565PC);
        else
            to_rgb16(pic, dst, (int)modulo, MPVR_RGB555);
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
        x = 0;
#ifdef MV_CONVERT68K
        if (pic->mpic_Width >= 4 && !((ULONG)d & 1)) {
            /* four pixels a longword (src/convert_68k.s) */
            x = pic->mpic_Width & ~3UL;
            mv_grey68k((ULONG *)d, y, lut, x >> 2);
        }
#endif
        for (; x < pic->mpic_Width; x++)
            d[x] = lut[y[x]];
    }
}
