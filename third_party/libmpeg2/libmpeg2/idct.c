/*
 * idct.c
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 * See http://libmpeg2.sourceforge.net/ for updates.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdlib.h>
#include <inttypes.h>

#include "mpeg2.h"
#include "attributes.h"
#include "mpeg2_internal.h"

#define W1 2841 /* 2048 * sqrt (2) * cos (1 * pi / 16) */
#define W2 2676 /* 2048 * sqrt (2) * cos (2 * pi / 16) */
#define W3 2408 /* 2048 * sqrt (2) * cos (3 * pi / 16) */
#define W5 1609 /* 2048 * sqrt (2) * cos (5 * pi / 16) */
#define W6 1108 /* 2048 * sqrt (2) * cos (6 * pi / 16) */
#define W7 565  /* 2048 * sqrt (2) * cos (7 * pi / 16) */

/* idct main entry point  */
void (* mpeg2_idct_copy) (int16_t * block, uint8_t * dest, int stride);
void (* mpeg2_idct_add) (int last, int16_t * block,
			 uint8_t * dest, int stride);

/*
 * In legal streams, the IDCT output should be between -384 and +384.
 * In corrupted streams, it is possible to force the IDCT output to go
 * to +-3826 - this is the worst case for a column IDCT where the
 * column inputs are 16-bit values.
 */
uint8_t mpeg2_clip[3840 * 2 + 256];
#define CLIP(i) ((mpeg2_clip + 3840)[i])

#if 0
#define BUTTERFLY(t0,t1,W0,W1,d0,d1)	\
do {					\
    t0 = W0 * d0 + W1 * d1;		\
    t1 = W0 * d1 - W1 * d0;		\
} while (0)
#else
#define BUTTERFLY(t0,t1,W0,W1,d0,d1)	\
do {					\
    int tmp = W0 * (d0 + d1);		\
    t0 = tmp + (W1 - W0) * d1;		\
    t1 = tmp - (W1 + W0) * d0;		\
} while (0)
#endif

static inline void idct_row (int16_t * const block)
{
    int d0, d1, d2, d3;
    int a0, a1, a2, a3, b0, b1, b2, b3;
    int t0, t1, t2, t3;

    /* shortcut */
    if (likely (!(block[1] | ((int32_t *)block)[1] | ((int32_t *)block)[2] |
		  ((int32_t *)block)[3]))) {
	uint32_t tmp = (uint16_t) (block[0] >> 1);
	tmp |= tmp << 16;
	((int32_t *)block)[0] = tmp;
	((int32_t *)block)[1] = tmp;
	((int32_t *)block)[2] = tmp;
	((int32_t *)block)[3] = tmp;
	return;
    }

    d0 = (block[0] << 11) + 2048;
    d1 = block[1];
    d2 = block[2] << 11;
    d3 = block[3];
    t0 = d0 + d2;
    t1 = d0 - d2;
    BUTTERFLY (t2, t3, W6, W2, d3, d1);
    a0 = t0 + t2;
    a1 = t1 + t3;
    a2 = t1 - t3;
    a3 = t0 - t2;

    d0 = block[4];
    d1 = block[5];
    d2 = block[6];
    d3 = block[7];
    BUTTERFLY (t0, t1, W7, W1, d3, d0);
    BUTTERFLY (t2, t3, W3, W5, d1, d2);
    b0 = t0 + t2;
    b3 = t1 + t3;
    t0 -= t2;
    t1 -= t3;
    b1 = ((t0 + t1) >> 8) * 181;
    b2 = ((t0 - t1) >> 8) * 181;

    block[0] = (a0 + b0) >> 12;
    block[1] = (a1 + b1) >> 12;
    block[2] = (a2 + b2) >> 12;
    block[3] = (a3 + b3) >> 12;
    block[4] = (a3 - b3) >> 12;
    block[5] = (a2 - b2) >> 12;
    block[6] = (a1 - b1) >> 12;
    block[7] = (a0 - b0) >> 12;
}

static inline void idct_col (int16_t * const block)
{
    int d0, d1, d2, d3;
    int a0, a1, a2, a3, b0, b1, b2, b3;
    int t0, t1, t2, t3;

    d0 = (block[8*0] << 11) + 65536;
    d1 = block[8*1];
    d2 = block[8*2] << 11;
    d3 = block[8*3];
    t0 = d0 + d2;
    t1 = d0 - d2;
    BUTTERFLY (t2, t3, W6, W2, d3, d1);
    a0 = t0 + t2;
    a1 = t1 + t3;
    a2 = t1 - t3;
    a3 = t0 - t2;

    d0 = block[8*4];
    d1 = block[8*5];
    d2 = block[8*6];
    d3 = block[8*7];
    BUTTERFLY (t0, t1, W7, W1, d3, d0);
    BUTTERFLY (t2, t3, W3, W5, d1, d2);
    b0 = t0 + t2;
    b3 = t1 + t3;
    t0 -= t2;
    t1 -= t3;
    b1 = ((t0 + t1) >> 8) * 181;
    b2 = ((t0 - t1) >> 8) * 181;

    block[8*0] = (a0 + b0) >> 17;
    block[8*1] = (a1 + b1) >> 17;
    block[8*2] = (a2 + b2) >> 17;
    block[8*3] = (a3 + b3) >> 17;
    block[8*4] = (a3 - b3) >> 17;
    block[8*5] = (a2 - b2) >> 17;
    block[8*6] = (a1 - b1) >> 17;
    block[8*7] = (a0 - b0) >> 17;
}

static void mpeg2_idct_copy_c (int16_t * block, uint8_t * dest,
			       const int stride)
{
    int i;

    for (i = 0; i < 8; i++)
	idct_row (block + 8 * i);
    for (i = 0; i < 8; i++)
	idct_col (block + i);
    do {
	dest[0] = CLIP (block[0]);
	dest[1] = CLIP (block[1]);
	dest[2] = CLIP (block[2]);
	dest[3] = CLIP (block[3]);
	dest[4] = CLIP (block[4]);
	dest[5] = CLIP (block[5]);
	dest[6] = CLIP (block[6]);
	dest[7] = CLIP (block[7]);

	((int32_t *)block)[0] = 0;	((int32_t *)block)[1] = 0;
	((int32_t *)block)[2] = 0;	((int32_t *)block)[3] = 0;

	dest += stride;
	block += 8;
    } while (--i);
}

static void mpeg2_idct_add_c (const int last, int16_t * block,
			      uint8_t * dest, const int stride)
{
    int i;

    if (last != 129 || (block[0] & (7 << 4)) == (4 << 4)) {
	for (i = 0; i < 8; i++)
	    idct_row (block + 8 * i);
	for (i = 0; i < 8; i++)
	    idct_col (block + i);
	do {
	    dest[0] = CLIP (block[0] + dest[0]);
	    dest[1] = CLIP (block[1] + dest[1]);
	    dest[2] = CLIP (block[2] + dest[2]);
	    dest[3] = CLIP (block[3] + dest[3]);
	    dest[4] = CLIP (block[4] + dest[4]);
	    dest[5] = CLIP (block[5] + dest[5]);
	    dest[6] = CLIP (block[6] + dest[6]);
	    dest[7] = CLIP (block[7] + dest[7]);

	    ((int32_t *)block)[0] = 0;	((int32_t *)block)[1] = 0;
	    ((int32_t *)block)[2] = 0;	((int32_t *)block)[3] = 0;

	    dest += stride;
	    block += 8;
	} while (--i);
    } else {
	int DC;

	DC = (block[0] + 64) >> 7;
	block[0] = block[63] = 0;
	i = 8;
	do {
	    dest[0] = CLIP (DC + dest[0]);
	    dest[1] = CLIP (DC + dest[1]);
	    dest[2] = CLIP (DC + dest[2]);
	    dest[3] = CLIP (DC + dest[3]);
	    dest[4] = CLIP (DC + dest[4]);
	    dest[5] = CLIP (DC + dest[5]);
	    dest[6] = CLIP (DC + dest[6]);
	    dest[7] = CLIP (DC + dest[7]);
	    dest += stride;
	} while (--i);
    }
}

#if defined (MPEGV) && !defined (MPEGV_PLAIN) && defined (MPEGV_EXACT_IDCT)
/*
 * mpegv (MPEGV_EXACT_IDCT): the same IDCT, each picture the same to the bit as libmpeg2's C
 * gives (its rows, then its columns, the same sums and shifts), made
 * quicker on a 68k by doing only what a block's coefficients need:
 *
 * - each row looked at first: a row of zeros is passed over (it stays
 *   zeros); a row with only its DC coefficient filled (libmpeg2's own
 *   shortcut); a row whose four higher frequencies are zero (positions
 *   2, 3, 6 and 7 here: libmpeg2's order puts the even frequencies
 *   first) needs half the products;
 * - the columns computed only from the rows that are not zeros: from the
 *   first alone (each column one value: no products), from rows 0, 1, 4
 *   and 5 (the four lower frequencies), from those and row 7 (MPEG-2's
 *   mismatch control puts a coefficient there in almost every block), or
 *   from all eight;
 * - each column clipped straight into the picture (or added to it), and
 *   only the rows that were used zeroed after;
 * - a block with only its DC coefficient: one value for all 64 pixels;
 * - every product 16 bits by 16 (a 68020's MULS.W, not MULS.L): each
 *   butterfly as its four products, W0 * d0 + W1 * d1 and W0 * d1 - W1 *
 *   d0, which is what libmpeg2's three give (W0 * (d0 + d1) needs 17
 *   bits).
 */
#if defined (__mc68020__) || defined (__mc68030__) || defined (__mc68040__) || defined (__mc68060__)
static inline int mv_m16 (int a, int b)
{
    int r = (int16_t) b;

    __asm__ ("muls.w %1,%0" : "+d" (r) : "dmi" ((int16_t) a));
    return r;
}
#define M16(a,b) mv_m16 (a, b)
#else
#define M16(a,b) ((int)(int16_t)(a) * (int)(int16_t)(b))
#endif

/* a row with coefficients in its higher frequencies */
static inline void mv_row_full (int16_t * const b)
{
    int d0, d1, d2, d3, a0, a1, a2, a3, b0, b1, b2, b3, t0, t1, t2, t3;

    d0 = (b[0] << 11) + 2048;
    d1 = b[1];
    d2 = b[2] << 11;
    d3 = b[3];
    t0 = d0 + d2;
    t1 = d0 - d2;
    t2 = M16 (W6, d3) + M16 (W2, d1);	/* BUTTERFLY (t2, t3, W6, W2, d3, d1) */
    t3 = M16 (W6, d1) - M16 (W2, d3);
    a0 = t0 + t2;
    a1 = t1 + t3;
    a2 = t1 - t3;
    a3 = t0 - t2;

    d0 = b[4];
    d1 = b[5];
    d2 = b[6];
    d3 = b[7];
    t0 = M16 (W7, d3) + M16 (W1, d0);	/* BUTTERFLY (t0, t1, W7, W1, d3, d0) */
    t1 = M16 (W7, d0) - M16 (W1, d3);
    t2 = M16 (W3, d1) + M16 (W5, d2);	/* BUTTERFLY (t2, t3, W3, W5, d1, d2) */
    t3 = M16 (W3, d2) - M16 (W5, d1);
    b0 = t0 + t2;
    b3 = t1 + t3;
    t0 -= t2;
    t1 -= t3;
    b1 = ((t0 + t1) >> 8) * 181;
    b2 = ((t0 - t1) >> 8) * 181;

    b[0] = (a0 + b0) >> 12;
    b[1] = (a1 + b1) >> 12;
    b[2] = (a2 + b2) >> 12;
    b[3] = (a3 + b3) >> 12;
    b[4] = (a3 - b3) >> 12;
    b[5] = (a2 - b2) >> 12;
    b[6] = (a1 - b1) >> 12;
    b[7] = (a0 - b0) >> 12;
}

/* a row with only its four lower frequencies (positions 0, 1, 4, 5) */
static inline void mv_row_low (int16_t * const b)
{
    int d0, d1, a0, a1, a2, a3, b0, b1, b2, b3, t0, t1, t2, t3;

    d0 = (b[0] << 11) + 2048;
    d1 = b[1];
    t2 = M16 (W2, d1);
    t3 = M16 (W6, d1);
    a0 = d0 + t2;
    a1 = d0 + t3;
    a2 = d0 - t3;
    a3 = d0 - t2;

    d0 = b[4];
    d1 = b[5];
    t0 = M16 (W1, d0);
    t1 = M16 (W7, d0);
    t2 = M16 (W3, d1);
    t3 = -M16 (W5, d1);
    b0 = t0 + t2;
    b3 = t1 + t3;
    t0 -= t2;
    t1 -= t3;
    b1 = ((t0 + t1) >> 8) * 181;
    b2 = ((t0 - t1) >> 8) * 181;

    b[0] = (a0 + b0) >> 12;
    b[1] = (a1 + b1) >> 12;
    b[2] = (a2 + b2) >> 12;
    b[3] = (a3 + b3) >> 12;
    b[4] = (a3 - b3) >> 12;
    b[5] = (a2 - b2) >> 12;
    b[6] = (a1 - b1) >> 12;
    b[7] = (a0 - b0) >> 12;
}

/* a row with only its highest frequency (position 7): in MPEG-2, the
 * mismatch control's coefficient, in almost every block's last row */
static inline void mv_row_7 (int16_t * const b)
{
    int t0, t1, b0, b1, b2, b3;

    t0 = M16 (W7, b[7]);
    t1 = -M16 (W1, b[7]);
    b0 = t0;
    b3 = t1;
    b1 = ((t0 + t1) >> 8) * 181;
    b2 = ((t0 - t1) >> 8) * 181;

    b[0] = (2048 + b0) >> 12;
    b[1] = (2048 + b1) >> 12;
    b[2] = (2048 + b2) >> 12;
    b[3] = (2048 + b3) >> 12;
    b[4] = (2048 - b3) >> 12;
    b[5] = (2048 - b2) >> 12;
    b[6] = (2048 - b1) >> 12;
    b[7] = (2048 - b0) >> 12;
}

/* on a 68020 or later the columns are idct_68k.s's, written by hand (the
 * same sums: mv_cols below is what they do) */
#if defined (__mc68020__) && !defined (MPEGV_NO_ASM)
#define MV_COLS68K
typedef void mv_cols_fct (const int16_t * block, uint8_t * dest, int stride);
extern mv_cols_fct mv_cols68k_copy_low, mv_cols68k_copy_low7, mv_cols68k_copy_all;
extern mv_cols_fct mv_cols68k_add_low, mv_cols68k_add_low7, mv_cols68k_add_all;
extern mv_cols_fct mv_cols68k_copy_low7m, mv_cols68k_add_low7m;
#endif

#define MV_ROW0 0	/* the columns from: row 0 only */
#define MV_LOW 1	/* rows 0, 1, 4, 5 */
#define MV_LOW7 2	/* rows 0, 1, 4, 5, 7 */
#define MV_LOW7M 4	/* rows 0, 1, 4, 5, and row 7 MPEG-2's mismatch
			 * coefficient alone: its products per column in
			 * rows 2 (W7 x7) and 3 (W1 x7) */

/* MPEG-2's mismatch control makes position 63 +16 or -16 in almost every
 * block, alone in its row: that row's transform (mv_row_7's), and its
 * products in each column, made once */
static int16_t mv_mm_row[2][8] ATTR_ALIGN(8);	/* +16, -16 */
static int16_t mv_mm_col[2][16] ATTR_ALIGN(8);	/* W7 x7 (8), W1 x7 (8) */
#define MV_ALL 3	/* any */

/* the columns, into the picture (add: added to it) */
static inline __attribute__ ((always_inline))
void mv_cols (const int16_t * b, uint8_t * dest, const int stride,
	      const int add, const int rows)
{
    int c;

#define MV_OUT(i,v) do {						\
    if (add)								\
	dest[(i) * stride] = CLIP ((v) + dest[(i) * stride]);		\
    else								\
	dest[(i) * stride] = CLIP (v);					\
} while (0)

    for (c = 0; c < 8; c++, b++, dest++) {
	int d0, d1, d2, d3, a0, a1, a2, a3, b0, b1, b2, b3, t0, t1, t2, t3;

	if (rows == MV_ROW0) {
	    /* ((b << 11) + 65536) >> 17, each of the eight */
	    int v = (b[0] + 32) >> 6;
	    MV_OUT (0, v); MV_OUT (1, v); MV_OUT (2, v); MV_OUT (3, v);
	    MV_OUT (4, v); MV_OUT (5, v); MV_OUT (6, v); MV_OUT (7, v);
	    continue;
	}
	d0 = (b[8*0] << 11) + 65536;
	d1 = b[8*1];
	if (rows == MV_ALL) {
	    d2 = b[8*2] << 11;
	    d3 = b[8*3];
	    t0 = d0 + d2;
	    t1 = d0 - d2;
	    t2 = M16 (W6, d3) + M16 (W2, d1);
	    t3 = M16 (W6, d1) - M16 (W2, d3);
	} else {
	    t0 = t1 = d0;
	    t2 = M16 (W2, d1);
	    t3 = M16 (W6, d1);
	}
	a0 = t0 + t2;
	a1 = t1 + t3;
	a2 = t1 - t3;
	a3 = t0 - t2;

	d0 = b[8*4];
	d1 = b[8*5];
	if (rows == MV_ALL) {
	    d2 = b[8*6];
	    d3 = b[8*7];
	    t0 = M16 (W7, d3) + M16 (W1, d0);
	    t1 = M16 (W7, d0) - M16 (W1, d3);
	    t2 = M16 (W3, d1) + M16 (W5, d2);
	    t3 = M16 (W3, d2) - M16 (W5, d1);
	} else if (rows == MV_LOW7) {
	    d3 = b[8*7];
	    t0 = M16 (W7, d3) + M16 (W1, d0);
	    t1 = M16 (W7, d0) - M16 (W1, d3);
	    t2 = M16 (W3, d1);
	    t3 = -M16 (W5, d1);
	} else if (rows == MV_LOW7M) {
	    t0 = b[8*2] + M16 (W1, d0);
	    t1 = M16 (W7, d0) - b[8*3];
	    t2 = M16 (W3, d1);
	    t3 = -M16 (W5, d1);
	} else {
	    t0 = M16 (W1, d0);
	    t1 = M16 (W7, d0);
	    t2 = M16 (W3, d1);
	    t3 = -M16 (W5, d1);
	}
	b0 = t0 + t2;
	b3 = t1 + t3;
	t0 -= t2;
	t1 -= t3;
	b1 = ((t0 + t1) >> 8) * 181;
	b2 = ((t0 - t1) >> 8) * 181;

	MV_OUT (0, (a0 + b0) >> 17);
	MV_OUT (1, (a1 + b1) >> 17);
	MV_OUT (2, (a2 + b2) >> 17);
	MV_OUT (3, (a3 + b3) >> 17);
	MV_OUT (4, (a3 - b3) >> 17);
	MV_OUT (5, (a2 - b2) >> 17);
	MV_OUT (6, (a1 - b1) >> 17);
	MV_OUT (7, (a0 - b0) >> 17);
    }
#undef MV_OUT
}

static inline __attribute__ ((always_inline))
void mv_idct (int16_t * const block, uint8_t * dest, const int stride,
	      const int add, const int mpeg2)
{
    int r, used = 0, mm = 0;

    /* the rows: each one's kind from its coefficients */
    for (r = 0; r < 8; r++) {
	int16_t * const b = block + 8 * r;
	int32_t high = ((int32_t *)b)[1] | ((int32_t *)b)[3];
	int32_t low = ((int32_t *)b)[2] | b[1];

	if (mpeg2 && r == 7 && !(low | ((int32_t *)b)[1] | b[0] | b[6]) &&
	    (b[7] == 16 || b[7] == -16)) {
	    /* the last row MPEG-2's mismatch coefficient alone, +16 or
	     * -16: below, not as a row */
	    mm = b[7] < 0 ? 2 : 1;
	    used |= 0x80;
	    break;
	}
	if (!(high | low)) {
	    uint32_t tmp;

	    if (!b[0])
		continue;		/* zeros: zeros */
	    used |= 1 << r;
	    tmp = (uint16_t) (b[0] >> 1);
	    tmp |= tmp << 16;
	    ((int32_t *)b)[0] = tmp;
	    ((int32_t *)b)[1] = tmp;
	    ((int32_t *)b)[2] = tmp;
	    ((int32_t *)b)[3] = tmp;
	    continue;
	}
	used |= 1 << r;
	if (!(low | ((int32_t *)b)[1] | b[0] | b[6]))
	    mv_row_7 (b);
	else if (high)
	    mv_row_full (b);
	else
	    mv_row_low (b);
    }

    if (mm) {
	int32_t * const w = (int32_t *)(block + 8 * 7);

	if (used & 0x4c || !(used & 0x7f)) {
	    /* other rows than 0, 1, 4, 5 (or none): the row as computed */
	    const int32_t * const m = (const int32_t *)mv_mm_row[mm - 1];

	    w[0] = m[0];
	    w[1] = m[1];
	    w[2] = m[2];
	    w[3] = m[3];
	    mm = 0;
	} else {
	    /* its products into rows 2 and 3 (zeros till now) */
	    const int32_t * const m = (const int32_t *)mv_mm_col[mm - 1];
	    int32_t * const w2 = (int32_t *)(block + 8 * 2);

	    w2[0] = m[0];
	    w2[1] = m[1];
	    w2[2] = m[2];
	    w2[3] = m[3];
	    w2[4] = m[4];
	    w2[5] = m[5];
	    w2[6] = m[6];
	    w2[7] = m[7];
	    w[3] = 0;		/* (the row's coefficient: zeroed after) */
	    used |= 0x0c;
	}
    }

    if (!used) {			/* (no coefficients at all) */
	if (!add)
	    for (r = 0; r < 8; r++, dest += stride)
		((uint32_t *)dest)[0] = ((uint32_t *)dest)[1] = CLIP (0) * 0x01010101u;
	return;
    }
    if (used == 1 && block[0] == block[1] && ((int32_t *)block)[1] ==
	((int32_t *)block)[0] && !(((int32_t *)block)[2] ^ ((int32_t *)block)[0]) &&
	!(((int32_t *)block)[3] ^ ((int32_t *)block)[0])) {
	/* the DC coefficient alone: one value (each column's) */
	int v = (block[0] + 32) >> 6;

	if (add)
	    for (r = 0; r < 8; r++, dest += stride) {
		dest[0] = CLIP (v + dest[0]);
		dest[1] = CLIP (v + dest[1]);
		dest[2] = CLIP (v + dest[2]);
		dest[3] = CLIP (v + dest[3]);
		dest[4] = CLIP (v + dest[4]);
		dest[5] = CLIP (v + dest[5]);
		dest[6] = CLIP (v + dest[6]);
		dest[7] = CLIP (v + dest[7]);
	    }
	else {
	    uint32_t q = CLIP (v) * 0x01010101u;

	    for (r = 0; r < 8; r++, dest += stride)
		((uint32_t *)dest)[0] = ((uint32_t *)dest)[1] = q;
	}
    } else if (!(used & ~0x01))
	mv_cols (block, dest, stride, add, MV_ROW0);
    else if (mm)
#ifdef MV_COLS68K
	(add ? mv_cols68k_add_low7m : mv_cols68k_copy_low7m) (block, dest, stride);
#else
	mv_cols (block, dest, stride, add, MV_LOW7M);
#endif
#ifdef MV_COLS68K
    else if (!(used & ~0x33))
	(add ? mv_cols68k_add_low : mv_cols68k_copy_low) (block, dest, stride);
    else if (!(used & ~0xb3))
	(add ? mv_cols68k_add_low7 : mv_cols68k_copy_low7) (block, dest, stride);
    else
	(add ? mv_cols68k_add_all : mv_cols68k_copy_all) (block, dest, stride);
#else
    else if (!(used & ~0x33))
	mv_cols (block, dest, stride, add, MV_LOW);
    else if (!(used & ~0xb3))
	mv_cols (block, dest, stride, add, MV_LOW7);
    else
	mv_cols (block, dest, stride, add, MV_ALL);
#endif

    /* the rows used made zeros again */
    for (r = 0; r < 8; r++)
	if (used & (1 << r)) {
	    int32_t * const w = (int32_t *)(block + 8 * r);

	    w[0] = w[1] = w[2] = w[3] = 0;
	}
}

static void mpegv_idct_copy (int16_t * block, uint8_t * dest,
			     const int stride)
{
    mv_idct (block, dest, stride, 0, 1);
}

/* MPEG-1's: no mismatch coefficient to look for */
static void mpegv_idct_copy_1 (int16_t * block, uint8_t * dest,
			       const int stride)
{
    mv_idct (block, dest, stride, 0, 0);
}

void (* mpegv_idct_copy1) (int16_t * block, uint8_t * dest, int stride);
void (* mpegv_idct_add1) (int last, int16_t * block, uint8_t * dest,
			  int stride);

static inline __attribute__ ((always_inline))
void mv_add (const int last, int16_t * block, uint8_t * dest,
	     const int stride, const int mpeg2)
{
    if (last != 129 || (block[0] & (7 << 4)) == (4 << 4))
	mv_idct (block, dest, stride, 1, mpeg2);
    else {
	/* libmpeg2's: the DC coefficient alone (and MPEG-2's mismatch
	 * control's in the last place, which changes nothing here) */
	int DC, i;

	DC = (block[0] + 64) >> 7;
	block[0] = block[63] = 0;
	i = 8;
	do {
	    dest[0] = CLIP (DC + dest[0]);
	    dest[1] = CLIP (DC + dest[1]);
	    dest[2] = CLIP (DC + dest[2]);
	    dest[3] = CLIP (DC + dest[3]);
	    dest[4] = CLIP (DC + dest[4]);
	    dest[5] = CLIP (DC + dest[5]);
	    dest[6] = CLIP (DC + dest[6]);
	    dest[7] = CLIP (DC + dest[7]);
	    dest += stride;
	} while (--i);
    }
}

static void mpegv_idct_add (const int last, int16_t * block,
			    uint8_t * dest, const int stride)
{
    mv_add (last, block, dest, stride, 1);
}

static void mpegv_idct_add_1 (const int last, int16_t * block,
			      uint8_t * dest, const int stride)
{
    mv_add (last, block, dest, stride, 0);
}
#endif

#if defined (MPEGV) && !defined (MPEGV_PLAIN) && !defined (MPEGV_EXACT_IDCT)
/*
 * mpegv 2.1: a faster IDCT, within IEEE 1180 (no longer libmpeg2's to the
 * bit: MPEGV_EXACT_IDCT builds the 2.0 one, which is).
 *
 * The rows are libmpeg2's (its sums and shifts), each with its constants
 * multiplied by the AAN prescale of the column frequency it holds:
 * s(0) = 1, s(u) = sqrt (2) cos (u pi / 16). So no coefficient is
 * multiplied by its scale: the rows' products carry it, at no cost. The
 * rows of the smallest scales are kept larger, so that their rounding
 * costs no more than the others': f5 and f6 twice, f7 four times. The
 * columns are then Arai, Agui and Nakajima's 8-point transform (five
 * multiplies against libmpeg2's eleven and two), in 32 bits: the column
 * values times 1024 (QB), its even part with f6 at twice its value and
 * 12-bit constants, its odd part at four times with 11-bit ones, each
 * product shifted down 3, the result's high word the pixel (so on a 68k
 * a SWAP, not a shift, takes it out).
 *
 * As before, each row is looked at first: zeros passed over, its DC
 * coefficient alone, its four lower frequencies (positions 0, 1, 4 and 5:
 * libmpeg2's order puts the even frequencies first), its highest alone;
 * MPEG-2's mismatch coefficient (+16 or -16 alone in the last row, in
 * almost every block) taken as a row made once. The columns are then
 * computed from rows 0, 1, 4, 5 (LOW), those and 7 (LOW7), or all
 * (ALL), by idct_aan_68k.s on a 68020 or later; mv_acols here is what
 * each of them computes, the rows not used being zeros.
 */
#if defined (__mc68020__) || defined (__mc68030__) || defined (__mc68040__) || defined (__mc68060__)
static inline int mv_m16 (int a, int b)
{
    int r = (int16_t) b;

    __asm__ ("muls.w %1,%0" : "+d" (r) : "dmi" ((int16_t) a));
    return r;
}
#define M16(a,b) mv_m16 (a, b)
#else
#define M16(a,b) ((int)(int16_t)(a) * (int)(int16_t)(b))
#endif

#define AAN_QB	10		/* the columns' values times 1024 */
#define AAN_RND	32767		/* rounding: a half, less one (IEEE 1180's mean) */
#define AAN_SQ2E 5793		/* sqrt (2), 12 bits (the even part) */
#define AAN_SQ2	2896		/* sqrt (2), 11 bits (the odd part) */
#define AAN_C1848 3784		/* 1.847759 (2 cos (pi / 8)) */
#define AAN_C1082 2217		/* 1.082392 (2 (cos (pi / 8) - cos (3 pi / 8))) */
#define AAN_C0613 1256		/* 0.613126: 2.613126 = 2 + this */
#define AAN_MUL(z,c) (((z) * (c)) >> 3)

/* each row's constants (its position r in the block: the column
 * frequency it holds, 0 2 4 6 1 3 5 7): 2048 s, then W1 to W7 times s,
 * rounded: (w * s15 + 16384) >> 15, s15 as in mv_aan_init (which checks
 * them). Constants, so that each row's code (inlined for it) has them as
 * immediates, and rows 0 and 2 (s = 1) shift for the first. */
static const int16_t mv_rw[8][8] ATTR_ALIGN(8) = {
    { 2048, 2841, 2676, 2408, 1609, 1108, 565 },
    { 2676, 3712, 3496, 3146, 2102, 1448, 738 },
    { 2048, 2841, 2676, 2408, 1609, 1108, 565 },
    { 2217, 3075, 2896, 2606, 1742, 1199, 612 },
    { 2841, 3941, 3712, 3340, 2232, 1537, 784 },
    { 2408, 3341, 3147, 2832, 1892, 1303, 664 },
    { 3218, 4464, 4205, 3784, 2528, 1741, 888 },
    { 2260, 3135, 2953, 2658, 1776, 1223, 624 }
};
/* the first constant's product: a shift where it is 2048 */
#define RK0M(r,x) (mv_rw[r][0] == 2048 ? (x) << 11 : M16 (mv_rw[r][0], x))
#define RK0 0
#define RW1 1
#define RW2 2
#define RW3 3
#define RW5 4
#define RW6 5
#define RW7 6

/* a row with coefficients in its higher frequencies */
static inline __attribute__ ((always_inline))
void mv_row_full (int16_t * const b, const int r)
{
    const int16_t * const W = mv_rw[r];
    int d0, d1, d2, d3, a0, a1, a2, a3, b0, b1, b2, b3, t0, t1, t2, t3;

    d0 = RK0M (r, b[0]) + 2048;
    d1 = b[1];
    d2 = RK0M (r, b[2]);
    d3 = b[3];
    t0 = d0 + d2;
    t1 = d0 - d2;
    t2 = M16 (W[RW6], d3) + M16 (W[RW2], d1);
    t3 = M16 (W[RW6], d1) - M16 (W[RW2], d3);
    a0 = t0 + t2;
    a1 = t1 + t3;
    a2 = t1 - t3;
    a3 = t0 - t2;

    d0 = b[4];
    d1 = b[5];
    d2 = b[6];
    d3 = b[7];
    t0 = M16 (W[RW7], d3) + M16 (W[RW1], d0);
    t1 = M16 (W[RW7], d0) - M16 (W[RW1], d3);
    t2 = M16 (W[RW3], d1) + M16 (W[RW5], d2);
    t3 = M16 (W[RW3], d2) - M16 (W[RW5], d1);
    b0 = t0 + t2;
    b3 = t1 + t3;
    t0 -= t2;
    t1 -= t3;
    b1 = ((t0 + t1) >> 8) * 181;
    b2 = ((t0 - t1) >> 8) * 181;

    b[0] = (a0 + b0) >> 12;
    b[1] = (a1 + b1) >> 12;
    b[2] = (a2 + b2) >> 12;
    b[3] = (a3 + b3) >> 12;
    b[4] = (a3 - b3) >> 12;
    b[5] = (a2 - b2) >> 12;
    b[6] = (a1 - b1) >> 12;
    b[7] = (a0 - b0) >> 12;
}

/* a row with only its four lower frequencies (positions 0, 1, 4, 5) */
static inline __attribute__ ((always_inline))
void mv_row_low (int16_t * const b, const int r)
{
    const int16_t * const W = mv_rw[r];
    int d0, d1, a0, a1, a2, a3, b0, b1, b2, b3, t0, t1, t2, t3;

    d0 = RK0M (r, b[0]) + 2048;
    d1 = b[1];
    t2 = M16 (W[RW2], d1);
    t3 = M16 (W[RW6], d1);
    a0 = d0 + t2;
    a1 = d0 + t3;
    a2 = d0 - t3;
    a3 = d0 - t2;

    d0 = b[4];
    d1 = b[5];
    t0 = M16 (W[RW1], d0);
    t1 = M16 (W[RW7], d0);
    t2 = M16 (W[RW3], d1);
    t3 = -M16 (W[RW5], d1);
    b0 = t0 + t2;
    b3 = t1 + t3;
    t0 -= t2;
    t1 -= t3;
    b1 = ((t0 + t1) >> 8) * 181;
    b2 = ((t0 - t1) >> 8) * 181;

    b[0] = (a0 + b0) >> 12;
    b[1] = (a1 + b1) >> 12;
    b[2] = (a2 + b2) >> 12;
    b[3] = (a3 + b3) >> 12;
    b[4] = (a3 - b3) >> 12;
    b[5] = (a2 - b2) >> 12;
    b[6] = (a1 - b1) >> 12;
    b[7] = (a0 - b0) >> 12;
}

/* a row with only its highest frequency (position 7) */
static inline __attribute__ ((always_inline))
void mv_row_7 (int16_t * const b, const int r)
{
    const int16_t * const W = mv_rw[r];
    int t0, t1, b0, b1, b2, b3;

    t0 = M16 (W[RW7], b[7]);
    t1 = -M16 (W[RW1], b[7]);
    b0 = t0;
    b3 = t1;
    b1 = ((t0 + t1) >> 8) * 181;
    b2 = ((t0 - t1) >> 8) * 181;

    b[0] = (2048 + b0) >> 12;
    b[1] = (2048 + b1) >> 12;
    b[2] = (2048 + b2) >> 12;
    b[3] = (2048 + b3) >> 12;
    b[4] = (2048 - b3) >> 12;
    b[5] = (2048 - b2) >> 12;
    b[6] = (2048 - b1) >> 12;
    b[7] = (2048 - b0) >> 12;
}

/* a row with only its DC coefficient: one value */
static inline __attribute__ ((always_inline))
void mv_row_dc (int16_t * const b, const int r)
{
    const int16_t * const W = mv_rw[r];
    uint32_t tmp = (uint16_t) ((RK0M (r, b[0]) + 2048) >> 12);

    tmp |= tmp << 16;
    ((int32_t *)b)[0] = tmp;
    ((int32_t *)b)[1] = tmp;
    ((int32_t *)b)[2] = tmp;
    ((int32_t *)b)[3] = tmp;
}

/* MPEG-2's mismatch row (+16, -16 alone at its end), made once */
static int16_t mv_mm_row[2][8] ATTR_ALIGN(8);

/* the columns: one pixel's value from a column (its 8 rows' values y) */
static inline void mv_acol (const int16_t * const b, int * const o)
{
    int t0, t1, t2, t3, t10, t11, t12, t13, e0, e1, e2, e3;
    int z10, z11, z12, z13, z5, o4, o5, o6, o7, o10, o11, o12;

    t0 = (b[8*0] << AAN_QB) + AAN_RND;
    t1 = b[8*2] << AAN_QB;
    t2 = b[8*1] << 1;		/* f2, at twice: as f6 is kept */
    t3 = b[8*3];
    t10 = t0 + t1;
    t11 = t0 - t1;
    t13 = (t2 + t3) << (AAN_QB - 1);
    t12 = AAN_MUL (t2 - t3, AAN_SQ2E) - t13;
    e0 = t10 + t13;
    e3 = t10 - t13;
    e1 = t11 + t12;
    e2 = t11 - t12;

    /* at four times: f1, f3 times 4, f5 (kept at twice) times 2, f7 (at
     * four times) as it is */
    z13 = (b[8*6] << 1) + (b[8*5] << 2);
    z10 = (b[8*6] << 1) - (b[8*5] << 2);
    z11 = (b[8*4] << 2) + b[8*7];
    z12 = (b[8*4] << 2) - b[8*7];
    o7 = (z11 + z13) << (AAN_QB - 2);
    o11 = AAN_MUL (z11 - z13, AAN_SQ2);
    z5 = AAN_MUL (z10 + z12, AAN_C1848);
    o10 = z5 - AAN_MUL (z12, AAN_C1082);
    o12 = z5 - (z10 << (AAN_QB - 1)) - AAN_MUL (z10, AAN_C0613);
    o6 = o12 - o7;
    o5 = o11 - o6;
    o4 = o10 - o5;

    o[0] = (e0 + o7) >> 16;
    o[1] = (e1 + o6) >> 16;
    o[2] = (e2 + o5) >> 16;
    o[3] = (e3 + o4) >> 16;
    o[4] = (e3 - o4) >> 16;
    o[5] = (e2 - o5) >> 16;
    o[6] = (e1 - o6) >> 16;
    o[7] = (e0 - o7) >> 16;
}

/* the columns, into the picture (add: added to it) */
static void mv_acols (const int16_t * b, uint8_t * dest, const int stride, const int add)
{
    int c, i, o[8];

    for (c = 0; c < 8; c++, b++, dest++) {
	mv_acol (b, o);
	for (i = 0; i < 8; i++)
	    if (add)
		dest[i * stride] = CLIP (o[i] + dest[i * stride]);
	    else
		dest[i * stride] = CLIP (o[i]);
    }
}

#ifdef MPEGV_IDCT_TEST
static void mv_row_full_any (int16_t * const b, const int r)
{
    switch (r) {
    case 0: mv_row_full (b, 0); break;
    case 1: mv_row_full (b, 1); break;
    case 2: mv_row_full (b, 2); break;
    case 3: mv_row_full (b, 3); break;
    case 4: mv_row_full (b, 4); break;
    case 5: mv_row_full (b, 5); break;
    case 6: mv_row_full (b, 6); break;
    default: mv_row_full (b, 7); break;
    }
}

/* (the tests: the transform's signed results, no picture) */
void mv_aan_signed (int16_t * const block, int * const out)
{
    int r, c, o[8];

    for (r = 0; r < 8; r++) {
	int16_t * const b = block + 8 * r;

	if (b[0] | b[1] | b[2] | b[3] | b[4] | b[5] | b[6] | b[7])
	    mv_row_full_any (b, r);
    }
    for (c = 0; c < 8; c++) {
	mv_acol (block + c, o);
	for (r = 0; r < 8; r++)
	    out[8 * r + c] = o[r];
    }
}
#endif

/* on a 68020 or later the columns are idct_aan_68k.s's (the same sums) */
#if defined (__mc68020__) && !defined (MPEGV_NO_ASM)
#define MV_ACOLS68K
typedef void mv_cols_fct (const int16_t * block, uint8_t * dest, int stride);
extern mv_cols_fct mv_aan68k_copy_low, mv_aan68k_copy_low7, mv_aan68k_copy_all;
extern mv_cols_fct mv_aan68k_add_low, mv_aan68k_add_low7, mv_aan68k_add_all;
#endif

static inline __attribute__ ((always_inline))
void mv_idct (int16_t * const block, uint8_t * dest, const int stride,
	      const int add, const int mpeg2)
{
    int r, used = 0;

    /* the rows: each one's kind from its coefficients (each row's code
     * its own, its constants immediates) */
#define MV_AROW(R) do {							\
	int16_t * const b = block + 8 * (R);				\
	int32_t high = ((int32_t *)b)[1] | ((int32_t *)b)[3];		\
	int32_t low = ((int32_t *)b)[2] | b[1];				\
									\
	if (!(high | low)) {						\
	    if (b[0]) {							\
		used |= 1 << (R);					\
		mv_row_dc (b, R);					\
	    }								\
	} else {							\
	    used |= 1 << (R);						\
	    if (!(low | ((int32_t *)b)[1] | b[0] | b[6]))		\
		mv_row_7 (b, R);					\
	    else if (high)						\
		mv_row_full (b, R);					\
	    else							\
		mv_row_low (b, R);					\
	}								\
    } while (0)
    MV_AROW (0);
    MV_AROW (1);
    MV_AROW (2);
    MV_AROW (3);
    MV_AROW (4);
    MV_AROW (5);
    MV_AROW (6);
    {
	int16_t * const b = block + 8 * 7;

	if (mpeg2 && !(((int32_t *)b)[2] | b[1] | ((int32_t *)b)[1] | b[0] | b[6]) &&
	    (b[7] == 16 || b[7] == -16)) {
	    /* the last row MPEG-2's mismatch coefficient alone: made once */
	    const int32_t * const m = (const int32_t *)mv_mm_row[b[7] < 0];
	    int32_t * const w = (int32_t *)b;

	    w[0] = m[0];
	    w[1] = m[1];
	    w[2] = m[2];
	    w[3] = m[3];
	    used |= 0x80;
	} else
	    MV_AROW (7);
    }
#undef MV_AROW

    if (!used) {			/* (no coefficients at all) */
	if (!add)
	    for (r = 0; r < 8; r++, dest += stride)
		((uint32_t *)dest)[0] = ((uint32_t *)dest)[1] = CLIP (0) * 0x01010101u;
	return;
    }
    if (!(used & ~0x01)) {
	/* row 0 alone: each column one value */
	int c;

	if (block[0] == block[1] && ((int32_t *)block)[1] == ((int32_t *)block)[0] &&
	    ((int32_t *)block)[2] == ((int32_t *)block)[0] &&
	    ((int32_t *)block)[3] == ((int32_t *)block)[0]) {
	    /* the DC coefficient alone: one value for the block */
	    int v = ((block[0] << AAN_QB) + AAN_RND) >> 16;

	    if (add)
		for (r = 0; r < 8; r++, dest += stride) {
		    dest[0] = CLIP (v + dest[0]);
		    dest[1] = CLIP (v + dest[1]);
		    dest[2] = CLIP (v + dest[2]);
		    dest[3] = CLIP (v + dest[3]);
		    dest[4] = CLIP (v + dest[4]);
		    dest[5] = CLIP (v + dest[5]);
		    dest[6] = CLIP (v + dest[6]);
		    dest[7] = CLIP (v + dest[7]);
		}
	    else {
		uint32_t q = CLIP (v) * 0x01010101u;

		for (r = 0; r < 8; r++, dest += stride)
		    ((uint32_t *)dest)[0] = ((uint32_t *)dest)[1] = q;
	    }
	} else
	    for (c = 0; c < 8; c++, dest++) {
		int v = ((block[c] << AAN_QB) + AAN_RND) >> 16;
		uint8_t * d = dest;

		for (r = 0; r < 8; r++, d += stride)
		    *d = add ? CLIP (v + *d) : CLIP (v);
	    }
    }
#ifdef MV_ACOLS68K
    else if (!(used & ~0x33))
	(add ? mv_aan68k_add_low : mv_aan68k_copy_low) (block, dest, stride);
    else if (!(used & ~0xb3))
	(add ? mv_aan68k_add_low7 : mv_aan68k_copy_low7) (block, dest, stride);
    else
	(add ? mv_aan68k_add_all : mv_aan68k_copy_all) (block, dest, stride);
#else
    else
	mv_acols (block, dest, stride, add);
#endif

    /* the rows used made zeros again */
    for (r = 0; r < 8; r++)
	if (used & (1 << r)) {
	    int32_t * const w = (int32_t *)(block + 8 * r);

	    w[0] = w[1] = w[2] = w[3] = 0;
	}
}

static void mpegv_idct_copy (int16_t * block, uint8_t * dest,
			     const int stride)
{
    mv_idct (block, dest, stride, 0, 1);
}

/* MPEG-1's: no mismatch coefficient to look for */
static void mpegv_idct_copy_1 (int16_t * block, uint8_t * dest,
			       const int stride)
{
    mv_idct (block, dest, stride, 0, 0);
}

void (* mpegv_idct_copy1) (int16_t * block, uint8_t * dest, int stride);
void (* mpegv_idct_add1) (int last, int16_t * block, uint8_t * dest,
			  int stride);

static inline __attribute__ ((always_inline))
void mv_add (const int last, int16_t * block, uint8_t * dest,
	     const int stride, const int mpeg2)
{
    if (last != 129 || (block[0] & (7 << 4)) == (4 << 4))
	mv_idct (block, dest, stride, 1, mpeg2);
    else {
	/* libmpeg2's: the DC coefficient alone (and MPEG-2's mismatch
	 * control's in the last place, which changes nothing here) */
	int DC, i;

	DC = (block[0] + 64) >> 7;
	block[0] = block[63] = 0;
	i = 8;
	do {
	    dest[0] = CLIP (DC + dest[0]);
	    dest[1] = CLIP (DC + dest[1]);
	    dest[2] = CLIP (DC + dest[2]);
	    dest[3] = CLIP (DC + dest[3]);
	    dest[4] = CLIP (DC + dest[4]);
	    dest[5] = CLIP (DC + dest[5]);
	    dest[6] = CLIP (DC + dest[6]);
	    dest[7] = CLIP (DC + dest[7]);
	    dest += stride;
	} while (--i);
    }
}

static void mpegv_idct_add (const int last, int16_t * block,
			    uint8_t * dest, const int stride)
{
    mv_add (last, block, dest, stride, 1);
}

static void mpegv_idct_add_1 (const int last, int16_t * block,
			      uint8_t * dest, const int stride)
{
    mv_add (last, block, dest, stride, 0);
}

static void mv_aan_init (void)
{
    int r, k;

#ifdef MPEGV_IDCT_TEST
    {
	static const int f[8] = { 0, 2, 4, 6, 1, 3, 5, 7 };
	/* s(u) for u = 0..7, 2^15: s(0) = 1, s(u) = sqrt (2) cos (u pi / 16);
	 * f5 and f6 twice, f7 four times (the columns know) */
	static const int32_t s15[8] = { 32768, 45450, 42813, 38531, 32768, 51492, 35468, 36164 };
	static const int w[7] = { 2048, W1, W2, W3, W5, W6, W7 };

	for (r = 0; r < 8; r++)
	    for (k = 0; k < 7; k++)
		if (mv_rw[r][k] != (int16_t) ((w[k] * s15[f[r]] + 16384) >> 15))
		    abort ();		/* (mv_rw's table, made by hand, is wrong) */
    }
#endif
    for (k = 0; k < 2; k++) {
	int16_t row[8] = { 0, 0, 0, 0, 0, 0, 0, k ? -16 : 16 };

	mv_row_7 (row, 7);
	for (r = 0; r < 8; r++)
	    mv_mm_row[k][r] = row[r];
    }
}
#endif


void mpeg2_idct_init (uint32_t accel)
{
#ifdef ARCH_X86
    if (accel & MPEG2_ACCEL_X86_SSE2) {
	mpeg2_idct_copy = mpeg2_idct_copy_sse2;
	mpeg2_idct_add = mpeg2_idct_add_sse2;
	mpeg2_idct_mmx_init ();
    } else if (accel & MPEG2_ACCEL_X86_MMXEXT) {
	mpeg2_idct_copy = mpeg2_idct_copy_mmxext;
	mpeg2_idct_add = mpeg2_idct_add_mmxext;
	mpeg2_idct_mmx_init ();
    } else if (accel & MPEG2_ACCEL_X86_MMX) {
	mpeg2_idct_copy = mpeg2_idct_copy_mmx;
	mpeg2_idct_add = mpeg2_idct_add_mmx;
	mpeg2_idct_mmx_init ();
    } else
#endif
#ifdef ARCH_PPC
    if (accel & MPEG2_ACCEL_PPC_ALTIVEC) {
	mpeg2_idct_copy = mpeg2_idct_copy_altivec;
	mpeg2_idct_add = mpeg2_idct_add_altivec;
	mpeg2_idct_altivec_init ();
    } else
#endif
#ifdef ARCH_ALPHA
    if (accel & MPEG2_ACCEL_ALPHA_MVI) {
	mpeg2_idct_copy = mpeg2_idct_copy_mvi;
	mpeg2_idct_add = mpeg2_idct_add_mvi;
	mpeg2_idct_alpha_init ();
    } else if (accel & MPEG2_ACCEL_ALPHA) {
	int i;

	mpeg2_idct_copy = mpeg2_idct_copy_alpha;
	mpeg2_idct_add = mpeg2_idct_add_alpha;
	mpeg2_idct_alpha_init ();
	for (i = -3840; i < 3840 + 256; i++)
	    CLIP(i) = (i < 0) ? 0 : ((i > 255) ? 255 : i);
    } else
#endif
    {
	int i, j;

#if defined (MPEGV) && !defined (MPEGV_PLAIN) && !defined (MPEGV_EXACT_IDCT)
	mv_aan_init ();
	mpeg2_idct_copy = mpegv_idct_copy;	/* mpegv 2.1: the faster one */
	mpegv_idct_copy1 = mpegv_idct_copy_1;	/* (MPEG-1's) */
	mpegv_idct_add1 = mpegv_idct_add_1;
	mpeg2_idct_add = mpegv_idct_add;
#elif defined (MPEGV) && !defined (MPEGV_PLAIN)
	for (i = 0; i < 2; i++) {
	    int16_t row[8] = { 0, 0, 0, 0, 0, 0, 0, i ? -16 : 16 };

	    mv_row_7 (row);
	    for (j = 0; j < 8; j++) {
		mv_mm_row[i][j] = row[j];
		mv_mm_col[i][j] = W7 * row[j];
		mv_mm_col[i][8 + j] = W1 * row[j];
	    }
	}
	mpeg2_idct_copy = mpegv_idct_copy;	/* mpegv: the quicker one */
	mpegv_idct_copy1 = mpegv_idct_copy_1;	/* (MPEG-1's) */
	mpegv_idct_add1 = mpegv_idct_add_1;
	mpeg2_idct_add = mpegv_idct_add;
#else
	mpeg2_idct_copy = mpeg2_idct_copy_c;
	mpeg2_idct_add = mpeg2_idct_add_c;
#endif
	for (i = -3840; i < 3840 + 256; i++)
	    CLIP(i) = (i < 0) ? 0 : ((i > 255) ? 255 : i);
	for (i = 0; i < 64; i++) {
	    j = mpeg2_scan_norm[i];
	    mpeg2_scan_norm[i] = ((j & 0x36) >> 1) | ((j & 0x09) << 2);
	    j = mpeg2_scan_alt[i];
	    mpeg2_scan_alt[i] = ((j & 0x36) >> 1) | ((j & 0x09) << 2);
	}
    }
}
