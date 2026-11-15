/*
 * mpegv.library's tests: its IDCT (third_party/libmpeg2/libmpeg2/idct.c,
 * the "mpegv:" one) against libmpeg2's own C, picture for picture: blocks
 * of every kind it treats apart (zeros, the DC coefficient alone, rows of
 * the lower frequencies, MPEG-2's mismatch coefficient, any), with
 * coefficients small, typical and as large as libmpeg2 lets them be,
 * put into a picture and added to one. Then the IEEE 1180-1990 accuracy
 * test of libmpeg2's (which mpegv's is to the bit): 10000 blocks for each
 * range and sign, its peak, mean square and mean errors against a
 * double-precision IDCT (on the build machine only: NO_IEEE leaves it
 * out, for the 68k's build, which has no floating point).
 * GPL-3.0-or-later, (c) 2026 Brian King.
 *
 *   idct_test [blocks]
 */
#ifndef NO_IEEE
#include <math.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "idct.c"

uint8_t mpeg2_scan_norm[64], mpeg2_scan_alt[64];

static unsigned long seed = 1;

/* the IEEE 1180 generator */
static long ieee_rand(long l, long h)
{
    long i, j;
    double x;
    seed = (seed * 1103515245) + 12345;
    i = seed & 0x7ffffffe;
    x = ((double)i) / ((double)0x7fffffff);
    x *= (l + h + 1);
    j = (long)x;
    return j - l;
}

static int rnd(int n)
{
    seed = seed * 1103515245 + 12345;
    return (int)((seed >> 8) % (unsigned long)n);
}

/* ---- mpegv's against libmpeg2's: the same pixels, the block zeros after */
static int same(int blocks)
{
    static const int kinds[] = { 0x00, 0x01, 0x33, 0xb3, 0xff };
    int16_t a[64] ATTR_ALIGN(8), b[64] ATTR_ALIGN(8), z[64] = { 0 };
    uint8_t pa[8 * 16], pb[8 * 16];
    int n, i, bad = 0;
    for (n = 0; n < blocks; n++) {
        int rows = kinds[rnd(5)], range = rnd(3), cols = rnd(5), add = rnd(2), dconly = !rnd(8);
        int big = range == 2 ? 32752 : range == 1 ? 2000 : 40;
        memset(a, 0, sizeof(a));
        for (i = 0; i < 64; i++) {
            int r = i >> 3, c = i & 7;
            if (!(rows & (1 << r)) || rnd(3))
                continue;
            if (cols == 0 && c) /* the DC position alone in its row */
                continue;
            if (cols == 1 && (c & 2)) /* positions 0, 1, 4, 5 */
                continue;
            if (cols == 4 && c != 7) /* the last position alone (MPEG-2's mismatch) */
                continue;
            a[i] = (int16_t)(rnd(2 * big + 1) - big);
        }
        if (dconly) {
            memset(a, 0, sizeof(a));
            a[0] = (int16_t)(rnd(2 * big + 1) - big);
        }
        if (!rnd(4))
            a[63] = rnd(2) ? 16 : -16; /* MPEG-2's mismatch control */
        memcpy(b, a, sizeof(a));
        for (i = 0; i < 8 * 16; i++)
            pa[i] = pb[i] = (uint8_t)rnd(256);
        if (add) {
            /* libmpeg2's 129: the DC coefficient alone (and the
             * mismatch one): its own shortcut then */
            int last = 129;
            for (i = 1; i < 63; i++)
                if (a[i])
                    last = 63;
            mpeg2_idct_add_c(last, a, pa, 16);
            (rnd(2) ? mpegv_idct_add : mpegv_idct_add_1)(last, b, pb, 16); /* MPEG-2's, MPEG-1's */
        } else {
            mpeg2_idct_copy_c(a, pa, 16);
            (rnd(2) ? mpegv_idct_copy : mpegv_idct_copy_1)(b, pb, 16);
        }
        if (memcmp(pa, pb, sizeof(pa)) || memcmp(b, z, sizeof(z)) || memcmp(a, z, sizeof(z))) {
            if (bad++ < 5)
                printf("block %d (rows %02x, %s) differs\n", n, rows, add ? "add" : "copy");
        }
    }
    printf("mpegv's IDCT against libmpeg2's C: %d blocks, %d differ\n", blocks, bad);
    return bad;
}

#ifndef NO_IEEE
/* ---- IEEE 1180: libmpeg2's rows and columns, its coefficients 16 times
 * the standard's (as libmpeg2 dequantises them) */
static double c8[8][8];

static void fdct(const double *in, double *out)
{
    int u, v, x, y;
    for (u = 0; u < 8; u++)
        for (v = 0; v < 8; v++) {
            double s = 0;
            for (x = 0; x < 8; x++)
                for (y = 0; y < 8; y++)
                    s += in[x * 8 + y] * c8[u][x] * c8[v][y];
            out[u * 8 + v] = s;
        }
}

static void idct_ref(const double *in, double *out)
{
    int u, v, x, y;
    for (x = 0; x < 8; x++)
        for (y = 0; y < 8; y++) {
            double s = 0;
            for (u = 0; u < 8; u++)
                for (v = 0; v < 8; v++)
                    s += in[u * 8 + v] * c8[u][x] * c8[v][y];
            out[x * 8 + y] = s;
        }
}

static int ieee(long l, long h, int sign, int blocks)
{
    double err[64] = { 0 }, sq[64] = { 0 }, pmse = 0, pme = 0, omse = 0, ome = 0;
    int peak = 0, n, i, ok;
    seed = 1;
    for (n = 0; n < blocks; n++) {
        double x[64], f[64], r[64];
        int16_t blk[64] ATTR_ALIGN(8);
        int coef[64];
        for (i = 0; i < 64; i++)
            x[i] = (double)(ieee_rand(l, h) * sign);
        fdct(x, f);
        for (i = 0; i < 64; i++) {
            double v = floor(f[i] + 0.5);
            coef[i] = v < -2048 ? -2048 : v > 2047 ? 2047 : (int)v;
            f[i] = coef[i];
        }
        idct_ref(f, r);
        /* libmpeg2's order: the row and column positions permuted as its
         * scan tables put them (mpeg2_idct_init) */
        for (i = 0; i < 64; i++) {
            int j = ((i & 0x36) >> 1) | ((i & 0x09) << 2);
            blk[j] = (int16_t)(coef[i] * 16);
        }
        for (i = 0; i < 8; i++)
            idct_row(blk + 8 * i);
        for (i = 0; i < 8; i++)
            idct_col(blk + i);
        for (i = 0; i < 64; i++) {
            double ref = floor(r[i] + 0.5);
            int t = blk[i], e;
            ref = ref < -256 ? -256 : ref > 255 ? 255 : ref;
            t = t < -256 ? -256 : t > 255 ? 255 : t;
            e = t - (int)ref;
            if (abs(e) > peak)
                peak = abs(e);
            err[i] += e;
            sq[i] += e * e;
        }
    }
    for (i = 0; i < 64; i++) {
        double me = err[i] / blocks, mse = sq[i] / blocks;
        if (fabs(me) > pme)
            pme = fabs(me);
        if (mse > pmse)
            pmse = mse;
        ome += err[i];
        omse += sq[i];
    }
    ome /= 64.0 * blocks;
    omse /= 64.0 * blocks;
    ok = peak <= 1 && pmse <= 0.06 && omse <= 0.02 && pme <= 0.015 && fabs(ome) <= 0.0015;
    printf("IEEE 1180 [-%ld,%ld] %s: peak %d, mse %.4f (pixel %.4f), mean %.5f (pixel %.4f): %s\n", l, h,
           sign > 0 ? "+" : "-", peak, omse, pmse, ome, pme, ok ? "ok" : "FAILS");
    return !ok;
}

#endif

int main(int argc, char **argv)
{
    int blocks = argc > 1 ? atoi(argv[1]) : 200000, i, j, bad = 0;
    mpeg2_idct_init(0);
    bad += same(blocks);
#ifndef NO_IEEE
    for (i = 0; i < 8; i++)
        for (j = 0; j < 8; j++)
            c8[i][j] = (i ? sqrt(0.25) : sqrt(0.125)) * cos((2 * j + 1) * i * M_PI / 16);
    bad += ieee(256, 255, 1, 10000) + ieee(256, 255, -1, 10000);
    bad += ieee(5, 5, 1, 10000) + ieee(5, 5, -1, 10000);
    bad += ieee(300, 300, 1, 10000) + ieee(300, 300, -1, 10000);
    {
        /* all zeros in, all zeros out */
        int16_t z[64] ATTR_ALIGN(8) = { 0 };
        for (i = 0; i < 8; i++)
            idct_row(z + 8 * i);
        for (i = 0; i < 8; i++)
            idct_col(z + i);
        for (i = 0; i < 64; i++)
            bad += z[i] != 0;
    }
#else
    (void)j;
#endif
    return bad != 0;
}
