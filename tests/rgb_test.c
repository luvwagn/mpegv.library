/*
 * mpegv.library's tests: MpvToRGB and MpvToRGBScaled (src/convert.c),
 * every format, against a plain conversion a pixel at a time (the same
 * BT.601 sums: pl_mpeg's), on random pictures: the same size, larger
 * (720 x 576 to 1024 x 576, as a DVD in a window; 352 x 240 to 1024 x 768,
 * a VCD on the whole screen), smaller, odd sizes.
 * The same bytes to the bit.
 * GPL-3.0-or-later, (c) 2026 Brian King.
 *
 *   rgb_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "convert.c"

/* (the library's memory: mpegv.c's, here malloc's) */
static int no_mem;                          /* (to test the way without it) */

void *mpv_malloc(unsigned long n)
{
    return no_mem ? NULL : malloc(n);
}

void mpv_free(void *p)
{
    free(p);
}

static unsigned long seed = 11;

static int rnd(int n)
{
    seed = seed * 1103515245 + 12345;
    return (int)((seed >> 8) % (unsigned long)n);
}

static int cl(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static const int bpp[13] = { 3, 3, 4, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2 };

/* one pixel of the picture in format f at d */
static void pixel(UBYTE *d, int f, int Y, int cb, int cr)
{
    int y = ((Y - 16) * 76309) >> 16;
    int r = cl(y + (((cr - 128) * 104597) >> 16));
    int g = cl(y - (((cb - 128) * 25674 + (cr - 128) * 53278) >> 16));
    int b = cl(y + (((cb - 128) * 132201) >> 16));
    unsigned v = 0;
    int t;
    switch (f) {
    case MPVR_RGB24: d[0] = r, d[1] = g, d[2] = b; return;
    case MPVR_BGR24: d[0] = b, d[1] = g, d[2] = r; return;
    case MPVR_ARGB32: d[0] = 0xFF, d[1] = r, d[2] = g, d[3] = b; return;
    case MPVR_RGBA32: d[0] = r, d[1] = g, d[2] = b, d[3] = 0xFF; return;
    case MPVR_BGRA32: d[0] = b, d[1] = g, d[2] = r, d[3] = 0xFF; return;
    }
    if (f == MPVR_BGR565 || f == MPVR_BGR565PC || f == MPVR_BGR555 || f == MPVR_BGR555PC)
        t = r, r = b, b = t;
    if (f == MPVR_RGB565 || f == MPVR_RGB565PC || f == MPVR_BGR565 || f == MPVR_BGR565PC)
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    else
        v = ((r & 0xF8) << 7) | ((g & 0xF8) << 2) | (b >> 3);
    if (f == MPVR_RGB565PC || f == MPVR_RGB555PC || f == MPVR_BGR565PC || f == MPVR_BGR555PC)
        d[0] = (UBYTE)v, d[1] = (UBYTE)(v >> 8);
    else
        d[0] = (UBYTE)(v >> 8), d[1] = (UBYTE)v;
}

int main(void)
{
    static const int sizes[][4] = {
        { 720, 576, 720, 576 }, { 720, 576, 1024, 576 }, { 720, 576, 920, 517 }, { 352, 240, 640, 480 },
        { 720, 480, 320, 200 }, { 101, 57, 203, 99 }, { 64, 48, 63, 47 }, { 16, 16, 1, 1 }, { 720, 576, 1280, 720 },
        { 352, 240, 1024, 768 }, { 64, 48, 133, 97 } };
    int s, f, bad = 0, n = 0;
    for (s = 0; s < (int)(sizeof(sizes) / sizeof(sizes[0])); s++) {
        int sw = sizes[s][0], sh = sizes[s][1], dw = sizes[s][2], dh = sizes[s][3];
        int ym = sw + 8, cm = (sw + 1) / 2 + 4, i, j;
        UBYTE *Y = malloc(ym * sh), *U = malloc(cm * ((sh + 1) / 2)), *V = malloc(cm * ((sh + 1) / 2));
        UBYTE *a = malloc((dw * 4 + 8) * dh + 64), *b = malloc((dw * 4 + 8) * dh + 64);
        struct MpvPicture p;
        if (!Y || !U || !V || !a || !b) {
            printf("no memory\n");
            return 1;
        }
        for (i = 0; i < ym * sh; i++)
            Y[i] = (UBYTE)rnd(256);
        for (i = 0; i < cm * ((sh + 1) / 2); i++)
            U[i] = (UBYTE)rnd(256), V[i] = (UBYTE)rnd(256);
        memset(&p, 0, sizeof(p));
        p.mpic_Y = Y, p.mpic_Cb = U, p.mpic_Cr = V;
        p.mpic_Width = (UWORD)sw, p.mpic_Height = (UWORD)sh;
        p.mpic_YModulo = (UWORD)ym, p.mpic_CModulo = (UWORD)cm;
        for (f = 0; f < 13; f++) {
            int stride = dw * bpp[f] + 6, k;
            memset(a, 0x5A, (dw * 4 + 8) * dh + 64);
            memset(b, 0x5A, (dw * 4 + 8) * dh + 64);
            for (j = 0; j < dh; j++) {
                int sy = (int)((long)j * sh / dh);
                for (i = 0; i < dw; i++) {
                    int sx = (int)((long)i * sw / dw);
                    pixel(b + j * stride + i * bpp[f], f, Y[sy * ym + sx], U[(sy >> 1) * cm + (sx >> 1)],
                          V[(sy >> 1) * cm + (sx >> 1)]);
                }
            }
            MpvToRGBScaled(&p, a, (ULONG)stride, (ULONG)f, (ULONG)dw, (ULONG)dh);
            for (k = 0; k < stride * dh; k++)
                if (a[k] != b[k]) {
                    if (bad++ < 5)
                        printf("%d x %d to %d x %d, format %d: byte %d is %02x, not %02x\n", sw, sh, dw, dh, f, k,
                               a[k], b[k]);
                    break;
                }
            n++;
            if (sw != dw || sh != dh) {
                /* without memory for a row: a pixel at a time, the same */
                memset(a, 0x5A, (dw * 4 + 8) * dh + 64);
                no_mem = 1;
                MpvToRGBScaled(&p, a, (ULONG)stride, (ULONG)f, (ULONG)dw, (ULONG)dh);
                no_mem = 0;
                if (memcmp(a, b, (size_t)stride * dh) && bad++ < 5)
                    printf("%d x %d to %d x %d, format %d, no memory: not the same\n", sw, sh, dw, dh, f);
                n++;
            }
            if (sw == dw && sh == dh) {
                /* MpvToRGB itself, the same */
                memset(a, 0x5A, (dw * 4 + 8) * dh + 64);
                MpvToRGB(&p, a, (ULONG)stride, (ULONG)f);
                if (memcmp(a, b, (size_t)stride * dh) && bad++ < 5)
                    printf("MpvToRGB %d x %d, format %d: not the same\n", sw, sh, f);
                n++;
            }
        }
        free(Y), free(U), free(V), free(a), free(b);
    }
    printf("RGB: %d conversions (13 formats, %d sizes), %d not as a pixel at a time\n", n,
           (int)(sizeof(sizes) / sizeof(sizes[0])), bad);
    return bad != 0;
}
