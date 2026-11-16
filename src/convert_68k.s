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

| ---- void mv_row16_68k (UWORD * dst, const UBYTE * y, const UBYTE * terms,
|                         ULONG pairs, const struct mv_t16 * t)
| One source row made 16-bit pixels and written out scaled across: for
| each pair of source pixels (one chroma sample) its sums (terms: red's,
| green's negated, blue's, words, then how many times each of the two is
| written, a byte each), each pixel's colour three look-ups OR'd together,
| written that many times. t: the red, green and blue tables (UWORD, at
| their 0) and the Y table (WORD). mv_row16pc_68k: the same, each pixel's
| bytes swapped (little-endian formats).
	.macro	ROW16 name, swap
	.globl	\name
\name:
	movem.l	d2-d7/a2-a6,-(sp)
	movem.l	48(sp),a0-a1/a6		| dst, y, terms
	move.l	60(sp),d7		| pairs
	move.l	64(sp),a5		| t
	movem.l	(a5),a2-a5		| red, green, blue, Y tables
	moveq	#0,d6			| (an index: its upper bytes stay 0)
	subq.l	#1,d7
	bmi	9f
1:
	movem.w	(a6)+,d3-d5		| red's, green's (negated), blue's
	| the first pixel of the pair
	move.b	(a1)+,d6
	move.w	(a5,d6.w*2),d2
	move.w	d2,d0
	add.w	d3,d0
	move.w	(a2,d0.w*2),d1
	move.w	d2,d0
	add.w	d4,d0
	or.w	(a3,d0.w*2),d1
	add.w	d5,d2
	or.w	(a4,d2.w*2),d1
	.if	\swap
	ror.w	#8,d1
	.endif
	moveq	#0,d0
	move.b	(a6)+,d0
	subq.w	#1,d0
	bmi.s	3f
2:
	move.w	d1,(a0)+
	dbra	d0,2b
3:
	| the second
	move.b	(a1)+,d6
	move.w	(a5,d6.w*2),d2
	move.w	d2,d0
	add.w	d3,d0
	move.w	(a2,d0.w*2),d1
	move.w	d2,d0
	add.w	d4,d0
	or.w	(a3,d0.w*2),d1
	add.w	d5,d2
	or.w	(a4,d2.w*2),d1
	.if	\swap
	ror.w	#8,d1
	.endif
	moveq	#0,d0
	move.b	(a6)+,d0
	subq.w	#1,d0
	bmi.s	5f
4:
	move.w	d1,(a0)+
	dbra	d0,4b
5:
	dbra	d7,1b
	clr.w	d7
	subq.l	#1,d7
	bcc	1b
9:
	movem.l	(sp)+,d2-d7/a2-a6
	rts
	.endm

	ROW16	_mv_row16_68k, 0
	ROW16	_mv_row16pc_68k, 1

| ---- void mv_copy_68k (UBYTE * dst, const ULONG * src, ULONG bytes)
| A line of 16-bit pixels (bytes even; src longword aligned) to dst
| (word aligned), a longword at a time, eight to a turn of the loop.
	.globl	_mv_copy_68k
_mv_copy_68k:
	movem.l	4(sp),a0-a1		| dst, src
	move.l	12(sp),d0		| bytes
	move.l	d0,d1
	lsr.l	#5,d1			| 32 bytes at a time
	beq.s	2f
1:	move.l	(a1)+,(a0)+
	move.l	(a1)+,(a0)+
	move.l	(a1)+,(a0)+
	move.l	(a1)+,(a0)+
	move.l	(a1)+,(a0)+
	move.l	(a1)+,(a0)+
	move.l	(a1)+,(a0)+
	move.l	(a1)+,(a0)+
	subq.l	#1,d1
	bne.s	1b
2:	moveq	#28,d1
	and.l	d0,d1
	lsr.l	#2,d1			| the longwords left
	bra.s	4f
3:	move.l	(a1)+,(a0)+
4:	dbra	d1,3b
	btst	#1,d0
	beq.s	5f
	move.w	(a1),(a0)		| the last pixel
5:	rts
