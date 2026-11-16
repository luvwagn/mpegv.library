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
 * green and blue at ri, gi, bi, and (4 bytes) the alpha byte, 0xFF, at
 * ai (-1: none) */
static inline __attribute__((always_inline)) void to_rgb(const struct MpvPicture *pic, UBYTE *dst, int stride, int bpp, int ri, int gi,
                   int bi, int ai)
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
    if (ai >= 0)                      \
        dp[ai] = 0xFF;                \
    dp += bpp;
            MV_RGB(y0, d0);
            MV_RGB(y0, d0);
            MV_RGB(y1, d1);
            MV_RGB(y1, d1);
#undef MV_RGB
        }
    }
}

/* A 16-bit format's packing: 555 (else 565), blue at the top (BGR), its
 * bytes the other way round (PC: little-endian). Red's and blue's tables
 * are the top five bits' and the bottom five's: BGR takes them the other
 * way round. */
#define MV_PK(five55, bgr, pc) (((five55) ? 1 : 0) | ((bgr) ? 2 : 0) | ((pc) ? 4 : 0))

/* the same, 16 bits a pixel */
static inline __attribute__((always_inline)) void to_rgb16(const struct MpvPicture *pic, UBYTE *dst, int stride, int pk)
{
    int cols = pic->mpic_Width >> 1, rows = pic->mpic_Height >> 1;
    int yw = pic->mpic_YModulo, cw = pic->mpic_CModulo, row, col;
    const UWORD *pr, *pg, *pb;
    make_tables();
    pr = mv_pack[pk & 1][pk & 2 ? 2 : 0] + MV_LO;
    pg = mv_pack[pk & 1][1] + MV_LO;
    pb = mv_pack[pk & 1][pk & 2 ? 0 : 2] + MV_LO;
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
    if (pk & 4)                                           \
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

/* Scaled to dw x dh, the nearest pixel: output column i shows source
 * column i * w / dw, row j row j * h / dh (stepped, no division a pixel);
 * a chroma sample's sums made once for the run of pixels it covers. */
#define MV_SCALE_ROWS(BPP, SETUP, PUT)                                                         \
    do {                                                                                       \
        ULONG sw = pic->mpic_Width, sh = pic->mpic_Height, j, i;                               \
        ULONG qs = sw / dw, rs = sw % dw;                                                      \
        make_tables();                                                                         \
        for (j = 0; j < dh; j++) {                                                             \
            ULONG sy = j * sh / dh, sx = 0, rem = 0, lastc = ~0UL;                             \
            const UBYTE *py = pic->mpic_Y + sy * pic->mpic_YModulo;                            \
            const UBYTE *pu = pic->mpic_Cb + (sy >> 1) * pic->mpic_CModulo;                    \
            const UBYTE *pv = pic->mpic_Cr + (sy >> 1) * pic->mpic_CModulo;                    \
            UBYTE *d = dst + j * stride;                                                       \
            SETUP;                                                                             \
            for (i = 0; i < dw; i++) {                                                         \
                ULONG c = sx >> 1;                                                             \
                int yv;                                                                        \
                if (c != lastc) {                                                              \
                    int cr = pv[c], cb = pu[c];                                                \
                    lastc = c;                                                                 \
                    rv = mv_r[cr];                                                             \
                    gv = (mv_gb[cb] + mv_gr[cr]) >> 16;                                        \
                    bv = mv_b[cb];                                                             \
                }                                                                              \
                yv = mv_y[py[sx]];                                                             \
                PUT;                                                                           \
                d += BPP;                                                                      \
                sx += qs;                                                                      \
                if ((rem += rs) >= dw) {                                                       \
                    rem -= dw;                                                                 \
                    sx++;                                                                      \
                }                                                                              \
            }                                                                                  \
        }                                                                                      \
    } while (0)

/* (mpegv.c's: exec's memory in the library) */
void *mpv_malloc(unsigned long n);
void mpv_free(void *p);

/* Scaled through a row of the picture's own size: each source row the
 * output shows converted once into it (fast memory: a source row shown
 * twice, converted once), then its pixels copied out to the output's
 * columns (xs: each one's source column), the output written in order. */
#define MV_LINE_ROWS(T, ROW)                                                                   \
    do {                                                                                       \
        ULONG sw = pic->mpic_Width, sh = pic->mpic_Height, j, i, x, rem, last = ~0UL;          \
        ULONG qs = sw / dw, rs = sw % dw;                                                      \
        T *line = (T *)buf, *o;                                                                \
        for (i = 0, x = 0, rem = 0; i < dw; i++) {                                             \
            xs[i] = (UWORD)x;                                                                  \
            x += qs;                                                                           \
            if ((rem += rs) >= dw) {                                                           \
                rem -= dw;                                                                     \
                x++;                                                                           \
            }                                                                                  \
        }                                                                                      \
        for (j = 0; j < dh; j++) {                                                             \
            ULONG sy = j * sh / dh;                                                            \
            const UWORD *xp = xs;                                                              \
            if (sy != last) {                                                                  \
                ROW;                                                                           \
                last = sy;                                                                     \
            }                                                                                  \
            o = (T *)(dst + j * stride);                                                       \
            for (i = dw; i; i--)                                                               \
                *o++ = line[*xp++];                                                            \
        }                                                                                      \
    } while (0)

/* One source row, 16 bits a pixel, into line (the output's byte order) */
static inline __attribute__((always_inline)) void row16(const struct MpvPicture *pic, ULONG sy, UWORD *line, int pk)
{
    const UWORD *pr, *pg, *pb;
    const UBYTE *py = pic->mpic_Y + sy * pic->mpic_YModulo;
    const UBYTE *pu = pic->mpic_Cb + (sy >> 1) * pic->mpic_CModulo;
    const UBYTE *pv = pic->mpic_Cr + (sy >> 1) * pic->mpic_CModulo;
    ULONG n = pic->mpic_Width, c;
    UBYTE *d = (UBYTE *)line;
    pr = mv_pack[pk & 1][pk & 2 ? 2 : 0] + MV_LO;
    pg = mv_pack[pk & 1][1] + MV_LO;
    pb = mv_pack[pk & 1][pk & 2 ? 0 : 2] + MV_LO;
    for (c = 0; c < (n + 1) >> 1; c++) {
        int cr = *pv++, cb = *pu++, yv;
        const UWORD *r = pr + mv_r[cr], *g = pg - ((mv_gb[cb] + mv_gr[cr]) >> 16), *b = pb + mv_b[cb];
        UWORD v;
#define MV_PX16                                                                                \
    yv = mv_y[*py++];                                                                          \
    v = r[yv] | g[yv] | b[yv];                                                                 \
    if (pk & 4)                                                                                \
        v = (UWORD)((v << 8) | (v >> 8));                                                      \
    put16(d, v);                                                                               \
    d += 2;
        MV_PX16;
        if (2 * c + 1 < n) {
            MV_PX16;
        }
#undef MV_PX16
    }
}

/* One source row, 3 or 4 bytes a pixel (as to_rgb), into line */
static inline __attribute__((always_inline)) void row_rgb(const struct MpvPicture *pic, ULONG sy, UBYTE *d, int bpp, int ri,
                    int gi, int bi, int ai)
{
    const UBYTE *cl = mv_clamp + MV_LO;
    const UBYTE *py = pic->mpic_Y + sy * pic->mpic_YModulo;
    const UBYTE *pu = pic->mpic_Cb + (sy >> 1) * pic->mpic_CModulo;
    const UBYTE *pv = pic->mpic_Cr + (sy >> 1) * pic->mpic_CModulo;
    ULONG n = pic->mpic_Width, x;
    int rv = 0, gv = 0, bv = 0;
    for (x = 0; x < n; x++) {
        int yv;
        if (!(x & 1)) {
            int cr = *pv++, cb = *pu++;
            rv = mv_r[cr];
            gv = (mv_gb[cb] + mv_gr[cr]) >> 16;
            bv = mv_b[cb];
        }
        yv = mv_y[*py++];
        d[ri] = cl[yv + rv];
        d[gi] = cl[yv - gv];
        d[bi] = cl[yv + bv];
        if (ai >= 0)
            d[ai] = 0xFF;
        d += bpp;
    }
}

/* 3 bytes: copied a byte at a time */
typedef struct {
    UBYTE b[3];
} mv_px3;

static inline __attribute__((always_inline)) void scale_rgb(const struct MpvPicture *pic, UBYTE *dst, ULONG stride, ULONG dw,
                    ULONG dh, int bpp, int ri, int gi, int bi, int ai)
{
    ULONG sw = pic->mpic_Width;
    UBYTE *buf = mpv_malloc(sw * 4 + 4 + dw * 2);
    UWORD *xs = (UWORD *)(buf + ((sw * 4 + 4) & ~3UL));
    make_tables();
    if (buf) {
        if (bpp == 3)
            MV_LINE_ROWS(mv_px3, row_rgb(pic, sy, buf, 3, ri, gi, bi, ai));
        else
            MV_LINE_ROWS(ULONG, row_rgb(pic, sy, buf, 4, ri, gi, bi, ai));
        mpv_free(buf);
    } else {
        /* (no memory: a pixel at a time, straight) */
        const UBYTE *cl = mv_clamp + MV_LO;
        int rv = 0, gv = 0, bv = 0;
        MV_SCALE_ROWS(bpp, (void)0, (d[ri] = cl[yv + rv], d[gi] = cl[yv - gv], d[bi] = cl[yv + bv],
                                     ai >= 0 ? (void)(d[ai] = 0xFF) : (void)0));
    }
}

static inline __attribute__((always_inline)) void scale_rgb16(const struct MpvPicture *pic, UBYTE *dst, ULONG stride, ULONG dw,
                      ULONG dh, int pk)
{
    ULONG sw = pic->mpic_Width;
    UBYTE *buf = mpv_malloc(sw * 2 + 4 + dw * 2);
    UWORD *xs = (UWORD *)(buf + ((sw * 2 + 4) & ~3UL));
    make_tables();
    if (buf) {
        MV_LINE_ROWS(UWORD, row16(pic, sy, (UWORD *)buf, pk));
        mpv_free(buf);
    } else {
        const UWORD *pr, *pg, *pb;
        int rv = 0, gv = 0, bv = 0;
        pr = mv_pack[pk & 1][pk & 2 ? 2 : 0] + MV_LO;
        pg = mv_pack[pk & 1][1] + MV_LO;
        pb = mv_pack[pk & 1][pk & 2 ? 0 : 2] + MV_LO;
        MV_SCALE_ROWS(2, (void)0, do {
            UWORD v = pr[yv + rv] | pg[yv - gv] | pb[yv + bv];
            if (pk & 4)
                v = (UWORD)((v << 8) | (v >> 8));
            put16(d, v);
        } while (0));
    }
}

/* Bytes a pixel of an MPVR_ format (0: not one). */
static ULONG rgb_bytes(ULONG format)
{
    return format <= MPVR_BGR24 ? 3 : format <= MPVR_BGRA32 ? 4 : format <= MPVR_BGR555PC ? 2 : 0;
}

API VOID MpvToRGB(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG format)
{
    if (!pic || !dst)
        return;
    switch (format) {
    case MPVR_RGB24:    to_rgb(pic, dst, (int)modulo, 3, 0, 1, 2, -1); break;
    case MPVR_BGR24:    to_rgb(pic, dst, (int)modulo, 3, 2, 1, 0, -1); break;
    case MPVR_ARGB32:   to_rgb(pic, dst, (int)modulo, 4, 1, 2, 3, 0); break;
    case MPVR_RGBA32:   to_rgb(pic, dst, (int)modulo, 4, 0, 1, 2, 3); break;
    case MPVR_BGRA32:   to_rgb(pic, dst, (int)modulo, 4, 2, 1, 0, 3); break;
    case MPVR_RGB565:   to_rgb16(pic, dst, (int)modulo, MV_PK(0, 0, 0)); break;
    case MPVR_RGB565PC: to_rgb16(pic, dst, (int)modulo, MV_PK(0, 0, 1)); break;
    case MPVR_RGB555:   to_rgb16(pic, dst, (int)modulo, MV_PK(1, 0, 0)); break;
    case MPVR_RGB555PC: to_rgb16(pic, dst, (int)modulo, MV_PK(1, 0, 1)); break;
    case MPVR_BGR565:   to_rgb16(pic, dst, (int)modulo, MV_PK(0, 1, 0)); break;
    case MPVR_BGR565PC: to_rgb16(pic, dst, (int)modulo, MV_PK(0, 1, 1)); break;
    case MPVR_BGR555:   to_rgb16(pic, dst, (int)modulo, MV_PK(1, 1, 0)); break;
    case MPVR_BGR555PC: to_rgb16(pic, dst, (int)modulo, MV_PK(1, 1, 1)); break;
    }
}

API VOID MpvToRGBScaled(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG format, ULONG width,
                        ULONG height)
{
    if (!pic || !dst || !width || !height || !pic->mpic_Width || !pic->mpic_Height || !rgb_bytes(format))
        return;
    if (width == pic->mpic_Width && height == pic->mpic_Height && !(width & 1) && !(height & 1)) {
        MpvToRGB(pic, dst, modulo, format);     /* (2 x 2 at a time) */
        return;
    }
    switch (format) {
    case MPVR_RGB24:    scale_rgb(pic, dst, modulo, width, height, 3, 0, 1, 2, -1); break;
    case MPVR_BGR24:    scale_rgb(pic, dst, modulo, width, height, 3, 2, 1, 0, -1); break;
    case MPVR_ARGB32:   scale_rgb(pic, dst, modulo, width, height, 4, 1, 2, 3, 0); break;
    case MPVR_RGBA32:   scale_rgb(pic, dst, modulo, width, height, 4, 0, 1, 2, 3); break;
    case MPVR_BGRA32:   scale_rgb(pic, dst, modulo, width, height, 4, 2, 1, 0, 3); break;
    case MPVR_RGB565:   scale_rgb16(pic, dst, modulo, width, height, MV_PK(0, 0, 0)); break;
    case MPVR_RGB565PC: scale_rgb16(pic, dst, modulo, width, height, MV_PK(0, 0, 1)); break;
    case MPVR_RGB555:   scale_rgb16(pic, dst, modulo, width, height, MV_PK(1, 0, 0)); break;
    case MPVR_RGB555PC: scale_rgb16(pic, dst, modulo, width, height, MV_PK(1, 0, 1)); break;
    case MPVR_BGR565:   scale_rgb16(pic, dst, modulo, width, height, MV_PK(0, 1, 0)); break;
    case MPVR_BGR565PC: scale_rgb16(pic, dst, modulo, width, height, MV_PK(0, 1, 1)); break;
    case MPVR_BGR555:   scale_rgb16(pic, dst, modulo, width, height, MV_PK(1, 1, 0)); break;
    case MPVR_BGR555PC: scale_rgb16(pic, dst, modulo, width, height, MV_PK(1, 1, 1)); break;
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
