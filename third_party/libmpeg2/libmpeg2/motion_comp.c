/*
 * motion_comp.c
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

#include <inttypes.h>

#include "mpeg2.h"
#include "attributes.h"
#include "mpeg2_internal.h"

mpeg2_mc_t mpeg2_mc;

void mpeg2_mc_init (uint32_t accel)
{
#ifdef ARCH_X86
    if (accel & MPEG2_ACCEL_X86_MMXEXT)
	mpeg2_mc = mpeg2_mc_mmxext;
    else if (accel & MPEG2_ACCEL_X86_3DNOW)
	mpeg2_mc = mpeg2_mc_3dnow;
    else if (accel & MPEG2_ACCEL_X86_MMX)
	mpeg2_mc = mpeg2_mc_mmx;
    else
#endif
#ifdef ARCH_PPC
    if (accel & MPEG2_ACCEL_PPC_ALTIVEC)
	mpeg2_mc = mpeg2_mc_altivec;
    else
#endif
#ifdef ARCH_ALPHA
    if (accel & MPEG2_ACCEL_ALPHA)
	mpeg2_mc = mpeg2_mc_alpha;
    else
#endif
#ifdef ARCH_SPARC
    if (accel & MPEG2_ACCEL_SPARC_VIS)
	mpeg2_mc = mpeg2_mc_vis;
    else
#endif
#ifdef ARCH_ARM
    if (accel & MPEG2_ACCEL_ARM) {
	mpeg2_mc = mpeg2_mc_arm;
    } else
#endif
#if defined (MPEGV) && !defined (MPEGV_PLAIN)
	mpeg2_mc = mpeg2_mc_mpegv;	/* mpegv: four pixels at a time */
#else
	mpeg2_mc = mpeg2_mc_c;
#endif
}

#define avg2(a,b) ((a+b+1)>>1)
#define avg4(a,b,c,d) ((a+b+c+d+2)>>2)

#define predict_o(i) (ref[i])
#define predict_x(i) (avg2 (ref[i], ref[i+1]))
#define predict_y(i) (avg2 (ref[i], (ref+stride)[i]))
#define predict_xy(i) (avg4 (ref[i], ref[i+1], \
			     (ref+stride)[i], (ref+stride)[i+1]))

#define put(predictor,i) dest[i] = predictor (i)
#define avg(predictor,i) dest[i] = avg2 (predictor (i), dest[i])

/* mc function template */

#define MC_FUNC(op,xy)							\
static void MC_##op##_##xy##_16_c (uint8_t * dest, const uint8_t * ref,	\
				   const int stride, int height)	\
{									\
    do {								\
	op (predict_##xy, 0);						\
	op (predict_##xy, 1);						\
	op (predict_##xy, 2);						\
	op (predict_##xy, 3);						\
	op (predict_##xy, 4);						\
	op (predict_##xy, 5);						\
	op (predict_##xy, 6);						\
	op (predict_##xy, 7);						\
	op (predict_##xy, 8);						\
	op (predict_##xy, 9);						\
	op (predict_##xy, 10);						\
	op (predict_##xy, 11);						\
	op (predict_##xy, 12);						\
	op (predict_##xy, 13);						\
	op (predict_##xy, 14);						\
	op (predict_##xy, 15);						\
	ref += stride;							\
	dest += stride;							\
    } while (--height);							\
}									\
static void MC_##op##_##xy##_8_c (uint8_t * dest, const uint8_t * ref,	\
				  const int stride, int height)		\
{									\
    do {								\
	op (predict_##xy, 0);						\
	op (predict_##xy, 1);						\
	op (predict_##xy, 2);						\
	op (predict_##xy, 3);						\
	op (predict_##xy, 4);						\
	op (predict_##xy, 5);						\
	op (predict_##xy, 6);						\
	op (predict_##xy, 7);						\
	ref += stride;							\
	dest += stride;							\
    } while (--height);							\
}

/* definitions of the actual mc functions */

MC_FUNC (put,o)
MC_FUNC (avg,o)
MC_FUNC (put,x)
MC_FUNC (avg,x)
MC_FUNC (put,y)
MC_FUNC (avg,y)
MC_FUNC (put,xy)
MC_FUNC (avg,xy)

MPEG2_MC_EXTERN (c)

#if defined (MPEGV) && !defined (MPEGV_PLAIN)
/*
 * mpegv: the same prediction four pixels at a time, a longword each (any
 * 68020 or later reads a longword at any address), each byte's sum and
 * rounding as above, to the bit:
 * - avg2: (a + b + 1) >> 1 of each byte is (a | b) - (((a ^ b) & 0xfe) >> 1)
 * - avg4: (a + b + c + d + 2) >> 2 of each byte is the sum of their top six
 *   bits plus that of their low two bits (and 2), shifted: neither carries
 *   into the next byte
 */
#if defined (__mc68020__)
/* (a plain longword move at any address: gcc would read a longword it is
 * told may be unaligned a byte at a time) */
typedef uint32_t __attribute__ ((__may_alias__)) mv_long;
#define MV_LD(p) (*(const mv_long *)(p))
#else
typedef uint32_t __attribute__ ((__may_alias__, __aligned__ (1))) mv_long;
#define MV_LD(p) (*(const mv_long *)(p))
#endif
#define MV_AVG2(a,b) (((a) | (b)) - ((((a) ^ (b)) & 0xfefefefeu) >> 1))

static inline uint32_t mv_avg4 (uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t high = (((a >> 2) & 0x3f3f3f3fu) + ((b >> 2) & 0x3f3f3f3fu) +
		     ((c >> 2) & 0x3f3f3f3fu) + ((d >> 2) & 0x3f3f3f3fu));
    uint32_t low = ((a & 0x03030303u) + (b & 0x03030303u) +
		    (c & 0x03030303u) + (d & 0x03030303u) + 0x02020202u);

    return high + ((low >> 2) & 0x03030303u);
}

#define mv_predict_o(i) MV_LD (ref + (i))
#define mv_predict_x(i) MV_AVG2 (MV_LD (ref + (i)), MV_LD (ref + (i) + 1))
#define mv_predict_y(i) MV_AVG2 (MV_LD (ref + (i)), MV_LD (ref + stride + (i)))
#define mv_predict_xy(i) mv_avg4 (MV_LD (ref + (i)), MV_LD (ref + (i) + 1), \
				  MV_LD (ref + stride + (i)),		    \
				  MV_LD (ref + stride + (i) + 1))

#define mv_put(predictor,i) (*(mv_long *)(dest + (i)) = predictor (i))
#define mv_avg(predictor,i) do {					\
    uint32_t p = predictor (i), q = MV_LD (dest + (i));			\
    *(mv_long *)(dest + (i)) = MV_AVG2 (p, q);				\
} while (0)

#define MV_MC_FUNC(op,xy)						\
static void MC_##op##_##xy##_16_mpegv (uint8_t * dest, const uint8_t * ref, \
				       const int stride, int height)	\
{									\
    do {								\
	mv_##op (mv_predict_##xy, 0);					\
	mv_##op (mv_predict_##xy, 4);					\
	mv_##op (mv_predict_##xy, 8);					\
	mv_##op (mv_predict_##xy, 12);					\
	ref += stride;							\
	dest += stride;							\
    } while (--height);							\
}									\
static void MC_##op##_##xy##_8_mpegv (uint8_t * dest, const uint8_t * ref, \
				      const int stride, int height)	\
{									\
    do {								\
	mv_##op (mv_predict_##xy, 0);					\
	mv_##op (mv_predict_##xy, 4);					\
	ref += stride;							\
	dest += stride;							\
    } while (--height);							\
}

MV_MC_FUNC (put,o)
MV_MC_FUNC (avg,o)
MV_MC_FUNC (put,x)
MV_MC_FUNC (avg,x)
MV_MC_FUNC (put,y)
MV_MC_FUNC (avg,y)
MV_MC_FUNC (put,xy)
MV_MC_FUNC (avg,xy)

MPEG2_MC_EXTERN (mpegv)
#endif
