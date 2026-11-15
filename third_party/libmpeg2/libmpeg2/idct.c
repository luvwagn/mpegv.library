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

#if defined (MPEGV) && !defined (MPEGV_PLAIN)
/*
 * mpegv: the same IDCT, each picture the same to the bit as libmpeg2's C
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

#define MV_ROW0 0	/* the columns from: row 0 only */
#define MV_LOW 1	/* rows 0, 1, 4, 5 */
#define MV_LOW7 2	/* rows 0, 1, 4, 5, 7 */
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
	      const int add)
{
    int r, used = 0;

    /* the rows: each one's kind from its coefficients */
    for (r = 0; r < 8; r++) {
	int16_t * const b = block + 8 * r;
	int32_t high = ((int32_t *)b)[1] | ((int32_t *)b)[3];
	int32_t low = ((int32_t *)b)[2] | b[1];

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
    else if (!(used & ~0x33))
	mv_cols (block, dest, stride, add, MV_LOW);
    else if (!(used & ~0xb3))
	mv_cols (block, dest, stride, add, MV_LOW7);
    else
	mv_cols (block, dest, stride, add, MV_ALL);

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
    mv_idct (block, dest, stride, 0);
}

static void mpegv_idct_add (const int last, int16_t * block,
			    uint8_t * dest, const int stride)
{
    if (last != 129 || (block[0] & (7 << 4)) == (4 << 4))
	mv_idct (block, dest, stride, 1);
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

#if defined (MPEGV) && !defined (MPEGV_PLAIN)
	mpeg2_idct_copy = mpegv_idct_copy;	/* mpegv: the quicker one */
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
