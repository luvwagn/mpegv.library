| mpegv: libmpeg2's IDCT columns for the 68020 and later, written by hand
| (idct.c's mv_cols, which the build machine's tests use, says the same in
| C): the same sums and shifts, so the same pixels to the bit.
|
| Each column of the block (the rows' results, int16) is transformed and
| put straight into the picture, clipped through mpeg2_clip (copy), or
| added to it (add). gcc's own runs out of data registers and spills; here
| the four even results live in a4-a6 and d2, the odd ones in d1, d3, d4,
| d5, and every product is one MULS.W (16 x 16 bits: the rows' results are
| int16, the constants 12 bits); only the two multiplies by 181 need
| MULS.L (their operand can exceed 16 bits).
|
| Which rows can be other than zero chooses the routine (idct.c's MV_LOW,
| MV_LOW7, MV_ALL): rows 0, 1, 4, 5; those and row 7 (MPEG-2's mismatch
| coefficient); or all eight.
|
|   void mv_cols68k_<copy|add>_<low|low7|all> (const int16_t * block,
|                                              uint8_t * dest, int stride)
|
| (c) 2026 Brian King. GPL-3.0-or-later (mpegv.library's LICENSE).

W1	= 2841
W2	= 2676
W3	= 2408
W5	= 1609
W6	= 1108
W7	= 565

| locals on the stack: the block's end (its first row's), 8 * stride
END	= 0
STR8	= 4
| the arguments, after the locals (8) and the saved registers (44)
ARGS	= 8 + 44 + 4

| one pixel: a + b (or a - b, op sub), >> 17, clipped (add: to the
| picture's pixel), stored; the next row
	.macro	PIXEL a, b, op, add
	move.l	\a,d0
	\op\().l	\b,d0
	asr.l	d7,d0
	.if \add
	move.b	(a1),d6
	add.l	d6,d0
	.endif
	move.b	(a2,d0.l),(a1)
	adda.l	a3,a1
	.endm

	.macro	COLS name, add, even, o6, o7
	.globl	_\name
_\name:
	movem.l	d2-d7/a2-a6,-(sp)
	subq.l	#8,sp
	movea.l	ARGS(sp),a0		| the block (its first row)
	movea.l	ARGS+4(sp),a1		| the picture
	movea.l	ARGS+8(sp),a3		| its stride
	lea	16(a0),a2
	move.l	a2,END(sp)
	move.l	a3,d0
	lsl.l	#3,d0
	move.l	d0,STR8(sp)
	lea	_mpeg2_clip+3840,a2
	moveq	#0,d6			| (a pixel's byte: the rest stays 0)
1:
	| ---- even: rows 0 and 2 (<< 11, +65536), rows 1 and 3
	move.w	(a0),d0
	ext.l	d0
	moveq	#32,d7
	add.l	d7,d0			| (x0 + 32) << 11 = (x0 << 11) + 65536
	moveq	#11,d7
	asl.l	d7,d0
	.if \even
	move.w	32(a0),d1
	ext.l	d1
	asl.l	d7,d1
	move.l	d0,d2
	add.l	d1,d2			| t0
	sub.l	d1,d0			| t1
	.else
	move.l	d0,d2			| t0 = t1
	.endif
	move.w	16(a0),d1		| x1
	move.w	d1,d4
	muls.w	#W2,d4			| W2 x1
	muls.w	#W6,d1			| W6 x1
	.if \even
	move.w	48(a0),d3		| x3
	move.w	d3,d5
	muls.w	#W6,d5
	add.l	d5,d4			| t2 = W6 x3 + W2 x1
	muls.w	#W2,d3
	sub.l	d3,d1			| t3 = W6 x1 - W2 x3
	.endif
	movea.l	d2,a4
	adda.l	d4,a4			| a0 = t0 + t2
	sub.l	d4,d2			| a3 = t0 - t2
	movea.l	d0,a5
	adda.l	d1,a5			| a1 = t1 + t3
	sub.l	d1,d0
	movea.l	d0,a6			| a2 = t1 - t3

	| ---- odd: rows 4, 5, 6, 7
	move.w	64(a0),d0		| x4
	move.w	80(a0),d1		| x5
	move.w	d0,d3
	muls.w	#W1,d3			| p0 = W1 x4 (+ W7 x7)
	muls.w	#W7,d0			| p1 = W7 x4 (- W1 x7)
	.if \o7
	move.w	112(a0),d4		| x7
	move.w	d4,d5
	muls.w	#W7,d5
	add.l	d5,d3
	muls.w	#W1,d4
	sub.l	d4,d0
	.endif
	move.w	d1,d4
	muls.w	#W3,d4			| p2 = W3 x5 (+ W5 x6)
	muls.w	#W5,d1			| W5 x5
	.if \o6
	move.w	96(a0),d5		| x6
	move.w	d5,d7
	muls.w	#W5,d7
	add.l	d7,d4
	muls.w	#W3,d5
	sub.l	d1,d5
	move.l	d5,d1			| p3 = W3 x6 - W5 x5
	.else
	neg.l	d1			| p3 = - W5 x5
	.endif
	move.l	d3,d5
	add.l	d4,d5			| b0 = p0 + p2
	sub.l	d4,d3			| q0 = p0 - p2
	move.l	d0,d4
	add.l	d1,d4			| b3 = p1 + p3
	sub.l	d1,d0			| q1 = p1 - p3
	move.l	d3,d1
	add.l	d0,d1
	asr.l	#8,d1
	muls.l	#181,d1			| b1 = ((q0 + q1) >> 8) * 181
	sub.l	d0,d3
	asr.l	#8,d3
	muls.l	#181,d3			| b2 = ((q0 - q1) >> 8) * 181

	| ---- the eight pixels, rows 0 to 7
	moveq	#17,d7
	PIXEL	a4, d5, add, \add
	PIXEL	a5, d1, add, \add
	PIXEL	a6, d3, add, \add
	PIXEL	d2, d4, add, \add
	PIXEL	d2, d4, sub, \add
	PIXEL	a6, d3, sub, \add
	PIXEL	a5, d1, sub, \add
	PIXEL	a4, d5, sub, \add

	suba.l	STR8(sp),a1
	addq.l	#1,a1			| the next column
	addq.l	#2,a0
	cmpa.l	END(sp),a0
	bne	1b
	addq.l	#8,sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts
	.endm

	.text
	COLS	mv_cols68k_copy_low, 0, 0, 0, 0
	COLS	mv_cols68k_copy_low7, 0, 0, 0, 1
	COLS	mv_cols68k_copy_all, 0, 1, 1, 1
	COLS	mv_cols68k_add_low, 1, 0, 0, 0
	COLS	mv_cols68k_add_low7, 1, 0, 0, 1
	COLS	mv_cols68k_add_all, 1, 1, 1, 1

