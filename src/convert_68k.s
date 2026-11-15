| mpegv.library's helpers' inner loops for the 68020 and later, written by
| hand (src/convert.c says the same in C, which the build machine's tests
| use): the same bytes out.
|
| (c) 2026 Brian King. GPL-3.0-or-later (LICENSE).

| ---- void mv_yuv_y0uy1v68k (ULONG * dst, const UBYTE * y,
|                             const UBYTE * cb, const UBYTE * cr, ULONG n)
| n groups of four pixels: Y a longword, Cb and Cr a word each, two
| longwords out (Y0 Cb Y1 Cr, Y2 Cb Y3 Cr), put together a byte or a word
| at a time in the registers (no masks, no long shifts), written a
| longword at a time (a graphics card's memory, across the bus).
	.globl	_mv_yuv_y0uy1v68k
_mv_yuv_y0uy1v68k:
	movem.l	d2-d5/a2-a3,-(sp)
	movem.l	28(sp),a0-a3		| dst, y, cb, cr
	move.l	44(sp),d5		| n
1:
	move.l	(a1)+,d0		| y0 y1 y2 y3
	move.w	(a2)+,d1		| cb0 cb1
	move.w	(a3)+,d2		| cr0 cr1
	move.w	d0,d3
	lsl.w	#8,d3
	move.b	d2,d3			| y3 cr1
	move.w	d0,d4
	move.b	d1,d4			| y2 cb1
	swap	d4
	move.w	d3,d4			| y2 cb1 y3 cr1
	swap	d0			| y0 y1
	lsr.w	#8,d1			| cb0
	lsr.w	#8,d2			| cr0
	move.w	d0,d3
	lsl.w	#8,d3
	move.b	d2,d3			| y1 cr0
	move.b	d1,d0			| y0 cb0
	swap	d0
	move.w	d3,d0			| y0 cb0 y1 cr0
	move.l	d0,(a0)+
	move.l	d4,(a0)+
	subq.l	#1,d5
	bne.s	1b
	movem.l	(sp)+,d2-d5/a2-a3
	rts

| ---- void mv_grey68k (ULONG * dst, const UBYTE * y, const UBYTE * lut,
|                       ULONG n)
| n groups of four pixels: each through lut, a word of two at a time, a
| longword out.
	.globl	_mv_grey68k
_mv_grey68k:
	movem.l	d2-d3/a2,-(sp)
	movem.l	16(sp),a0-a2		| dst, y, lut
	move.l	28(sp),d3		| n
	moveq	#0,d1			| (an index: its upper bytes stay 0)
1:
	move.b	(a1)+,d1
	move.b	(a2,d1.w),d0
	lsl.w	#8,d0
	move.b	(a1)+,d1
	move.b	(a2,d1.w),d0
	swap	d0
	move.b	(a1)+,d1
	move.b	(a2,d1.w),d2
	lsl.w	#8,d2
	move.b	(a1)+,d1
	move.b	(a2,d1.w),d2
	move.w	d2,d0
	move.l	d0,(a0)+
	subq.l	#1,d3
	bne.s	1b
	movem.l	(sp)+,d2-d3/a2
	rts
