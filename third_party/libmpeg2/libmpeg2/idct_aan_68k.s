| mpegv 2.1: the IDCT's columns for the 68020 and later, written by hand
| (idct.c's mv_acol says the same in C, and the tests hold them to it, to
| the bit): Arai, Agui and Nakajima's 8-point transform over the rows'
| results (int16, each row already times its AAN prescale; f5 and f6 at
| twice, f7 at four times), in 32 bits: the values times 1024, the even
| part with f2 and f6 at twice and 12-bit constants, the odd part at four
| times with 11-bit ones, products shifted down 3, the pixel the result's
| high word (a SWAP takes it out). On the rows that are zeros the
| constants are folded (11586 = 2 x 5793, 11584 = 4 x 2896...), so a
| product of one row's value is one MULS.W.
|
| Which rows can be other than zero chooses the routine (idct.c's): rows
| 0, 1, 4 and 5 (LOW: the four lower frequencies; a product of one row's
| value is one MULS.W), those and row 7 (LOW7: MPEG-2's mismatch row, or
| any coefficient there), or all eight (ALL). The even results live in
| a4-a6 and d2, the odd in d1, d3, d4, d5.
|
|   void mv_aan68k_<copy|add>_<low|low7|all> (const int16_t * block,
|                                             uint8_t * dest, int stride)
|
| (c) 2026 Brian King. GPL-3.0-or-later (mpegv.library's LICENSE).

SQ2E	= 5793			| sqrt (2), 12 bits (the even part)
SQ2	= 2896			| sqrt (2), 11 bits (the odd part)
C1848	= 3784			| 1.847759
C1082	= 2217			| 1.082392
C0613	= 1256			| 0.613126 (2.613126 = 2 + this)
RND	= 32767			| a half (of 65536), less one

| locals on the stack: the block's end (its first row's), 8 * stride
END	= 0
STR8	= 4
| the arguments, after the locals (8) and the saved registers (44)
ARGS	= 8 + 44 + 4

| one pixel: a + b (or a - b, op sub), its high word, clipped (add: to
| the picture's pixel), stored; the next row
	.macro	PIXEL a, b, op, add
	move.l	\a,d0
	\op\().l	\b,d0
	swap	d0
	.if \add
	move.b	(a1),d6
	add.w	d6,d0
	.endif
	move.b	(a2,d0.w),(a1)
	adda.l	a3,a1
	.endm

| (2.5) a row of one value, its every pixel: a + b (or a - b), its high
| word, clipped (add: to the picture's pixel); the next row (a3: the
| stride less 8)
	.macro	ROWPIX a, b, op, add
	move.l	\a,d0
	\op\().l	\b,d0
	swap	d0
	.if \add
	lea	(a2,d0.w),a0		| (the table at v: a pixel its index)
	.rept	8
	move.b	(a1),d6
	move.b	(a0,d6.w),(a1)+
	.endr
	.else
	move.b	(a2,d0.w),d0
	and.l	#255,d0
	mulu.l	#0x01010101,d0		| (the byte four times)
	move.l	d0,(a1)+
	move.l	d0,(a1)+
	.endif
	adda.l	a3,a1
	.endm

| a value of the block's column, row r, into a data register, as a long
	.macro	ROWL r, d
	move.w	\r*16(a0),\d
	ext.l	\d
	.endm

	.macro	COLS name, add, kind, rows=0
	.globl	_\name
_\name:
	movem.l	d2-d7/a2-a6,-(sp)
	subq.l	#8,sp
	movea.l	ARGS(sp),a0		| the block (its first row)
	movea.l	ARGS+4(sp),a1		| the picture
	movea.l	ARGS+8(sp),a3		| its stride
	.if \rows
	subq.l	#8,a3			| (a row's end to the next one's start)
	.endif
	lea	16(a0),a2
	move.l	a2,END(sp)
	move.l	a3,d0
	lsl.l	#3,d0
	move.l	d0,STR8(sp)
	lea	_mpeg2_clip+3840,a2
	moveq	#0,d6			| (a pixel's byte: the rest stays 0)
	moveq	#10,d7			| (the values' scale: << 10)
1:
	| ---- even: rows 0 and 2 (f0, f4), rows 1 and 3 (f2, f6)
	ROWL	0, d0
	asl.l	d7,d0
	add.l	#RND,d0			| t0 = (y0 << 10) + RND
	.if \kind == 2
	ROWL	2, d1
	asl.l	d7,d1
	move.l	d0,d2
	add.l	d1,d2			| t10 = t0 + (y2 << 10)
	sub.l	d1,d0			| t11
	ROWL	1, d1
	add.l	d1,d1			| t2 = y1 << 1 (f2 at twice, as f6)
	ROWL	3, d3
	move.l	d1,d4
	add.l	d3,d4
	asl.l	#8,d4
	add.l	d4,d4			| t13 = (t2 + y3) << 9
	sub.l	d3,d1
	muls.l	#SQ2E,d1
	asr.l	#3,d1
	sub.l	d4,d1			| t12 = MUL (t2 - y3, SQ2E) - t13
	.else
	move.l	d0,d2			| t10 = t11 = t0
	ROWL	1, d1			| y1
	move.l	d1,d4
	asl.l	d7,d4			| t13 = y1 << 10
	muls.w	#2*SQ2E,d1
	asr.l	#3,d1
	sub.l	d4,d1			| t12 = MUL (y1 << 1, SQ2E) - t13
	.endif
	movea.l	d2,a4
	adda.l	d4,a4			| e0 = t10 + t13
	sub.l	d4,d2			| e3 = t10 - t13
	movea.l	d0,a5
	adda.l	d1,a5			| e1 = t11 + t12
	sub.l	d1,d0
	movea.l	d0,a6			| e2 = t11 - t12

	| ---- odd: rows 4 to 7 (f1, f3, f5, f7)
	.if \kind == 0
	| rows 6 and 7 zeros: z13 = 4 y5, z10 = -4 y5, z11 = z12 = 4 y4
	ROWL	4, d0			| y4
	ROWL	5, d1			| y5
	move.l	d0,d3
	add.l	d1,d3
	asl.l	d7,d3			| o7 = (z11 + z13) << 8 = (y4 + y5) << 10
	move.l	d0,d4
	sub.l	d1,d4			| y4 - y5: (z11 - z13) / 4, (z10 + z12) / 4
	move.l	d4,d5
	muls.l	#4*SQ2,d4
	asr.l	#3,d4			| o11
	muls.l	#4*C1848,d5
	asr.l	#3,d5			| z5
	muls.w	#4*C1082,d0
	asr.l	#3,d0
	neg.l	d0
	add.l	d5,d0			| o10 = z5 - MUL (4 y4, C1082)
	move.w	d1,d6
	muls.w	#-4*C0613,d6
	asr.l	#3,d6			| MUL (z10, C0613) = (-4 y5 C0613) >> 3
	asl.l	d7,d1
	add.l	d1,d1			| -(z10 << 9) = y5 << 11
	add.l	d1,d5
	sub.l	d6,d5			| o12
	.else
	| at four times: y4, y5 times 4, y6 (f5, at twice) times 2, y7 (f7,
	| at four times) as it is
	ROWL	4, d0
	asl.l	#2,d0			| 4 y4
	ROWL	7, d1			| y7
	move.l	d0,d3
	add.l	d1,d3			| z11 = 4 y4 + y7
	sub.l	d1,d0			| z12 = 4 y4 - y7
	ROWL	6, d1
	add.l	d1,d1			| 2 y6
	ROWL	5, d4
	asl.l	#2,d4			| 4 y5
	move.l	d1,d5
	add.l	d4,d5			| z13
	sub.l	d4,d1			| z10
	move.l	d3,d4
	sub.l	d5,d4
	muls.l	#SQ2,d4
	asr.l	#3,d4			| o11 = MUL (z11 - z13, SQ2)
	add.l	d5,d3
	asl.l	#8,d3			| o7 = (z11 + z13) << 8
	move.l	d1,d5
	add.l	d0,d5
	muls.l	#C1848,d5
	asr.l	#3,d5			| z5 = MUL (z10 + z12, C1848)
	muls.l	#C1082,d0
	asr.l	#3,d0
	neg.l	d0
	add.l	d5,d0			| o10 = z5 - MUL (z12, C1082)
	move.l	d1,d6
	muls.l	#C0613,d6
	asr.l	#3,d6			| MUL (z10, C0613)
	asl.l	#8,d1
	add.l	d1,d1			| z10 << 9
	sub.l	d1,d5
	sub.l	d6,d5			| o12 = z5 - (z10 << 9) - MUL (z10, C0613)
	.endif
	moveq	#0,d6
	sub.l	d3,d5			| o6 = o12 - o7
	sub.l	d5,d4			| o5 = o11 - o6
	sub.l	d4,d0			| o4 = o10 - o5
	move.l	d0,d1

	.if \rows
	| ---- (2.5) each row one value: the rows, 0 to 7, whole
	ROWPIX	a4, d3, add, \add
	ROWPIX	a5, d5, add, \add
	ROWPIX	a6, d4, add, \add
	ROWPIX	d2, d1, add, \add
	ROWPIX	d2, d1, sub, \add
	ROWPIX	a6, d4, sub, \add
	ROWPIX	a5, d5, sub, \add
	ROWPIX	a4, d3, sub, \add
	.else
	| ---- the eight pixels, rows 0 to 7
	PIXEL	a4, d3, add, \add
	PIXEL	a5, d5, add, \add
	PIXEL	a6, d4, add, \add
	PIXEL	d2, d1, add, \add
	PIXEL	d2, d1, sub, \add
	PIXEL	a6, d4, sub, \add
	PIXEL	a5, d5, sub, \add
	PIXEL	a4, d3, sub, \add
	.endif

	.if \rows == 0
	suba.l	STR8(sp),a1
	addq.l	#1,a1			| the next column
	addq.l	#2,a0
	cmpa.l	END(sp),a0
	bne	1b
	.endif
	addq.l	#8,sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts
	.endm

	.text
	COLS	mv_aan68k_copy_low, 0, 0
	COLS	mv_aan68k_copy_low7, 0, 1
	COLS	mv_aan68k_copy_all, 0, 2
	COLS	mv_aan68k_add_low, 1, 0
	COLS	mv_aan68k_add_low7, 1, 1
	COLS	mv_aan68k_add_all, 1, 2
| (2.5) every row used one value (only its DC coefficient: MPEG-1's
| vertical-only blocks): every column the same, one made, the rows filled
	COLS	mv_aan68k_copy_rows, 0, 2, 1
	COLS	mv_aan68k_add_rows, 1, 2, 1

| ---- mpegv 2.5: the blocks with no column transform to make, by hand
| (idct.c's C did them a pixel at a time through CLIP: about as much as a
| column kernel, for a block of one value). The same sums, to the bit.
|
|   void mv_fill68k_add (int v, uint8_t * dest, int stride)
|       each of the block's 64 pixels clipped (pixel + v): libmpeg2's DC
|       alone, and a block whose row 0 came out one value
|   void mv_fill68k_add_row0 (const int16_t * block, uint8_t * dest,
|                             int stride)
|   void mv_fill68k_copy_row0 (const int16_t * block, uint8_t * dest,
|                              int stride)
|       row 0 alone: each column one value, v = ((y << 10) + RND) >> 16
|       (the column kernels' sum over a column of zeros but its first),
|       added to the pixels or written, clipped. A pixel's value from
|       the clip table at v, indexed by the pixel (its high bytes 0).

	.globl	_mv_fill68k_add
_mv_fill68k_add:
	move.l	d2,-(sp)
	lea	_mpeg2_clip+3840,a1
	adda.l	8(sp),a1		| (the table at v)
	movea.l	12(sp),a0		| the picture
	move.l	16(sp),d1
	subq.l	#8,d1			| a row's end to the next row's start
	moveq	#0,d0			| (a pixel's byte: the rest stays 0)
	moveq	#7,d2
1:
	.rept	8
	move.b	(a0),d0
	move.b	(a1,d0.w),(a0)+
	.endr
	adda.l	d1,a0
	dbra	d2,1b
	move.l	(sp)+,d2
	rts

	.globl	_mv_fill68k_add_row0
_mv_fill68k_add_row0:
	movem.l	d2-d4/a2,-(sp)
	movea.l	20(sp),a2		| the block's row 0
	movea.l	24(sp),a0		| the picture
	move.l	28(sp),d3		| its stride
	move.l	d3,d4
	lsl.l	#3,d4
	subq.l	#1,d4			| a column's 8 rows back, and on one
	moveq	#7,d2
1:
	move.w	(a2)+,d1
	ext.l	d1
	moveq	#10,d0
	asl.l	d0,d1
	add.l	#RND,d1
	swap	d1			| v, the low word
	lea	_mpeg2_clip+3840,a1
	adda.w	d1,a1			| (the table at v)
	moveq	#0,d0
	.rept	8
	move.b	(a0),d0
	move.b	(a1,d0.w),(a0)
	adda.l	d3,a0
	.endr
	suba.l	d4,a0
	dbra	d2,1b
	movem.l	(sp)+,d2-d4/a2
	rts

| four columns' clipped values into a long, the first the high byte
	.macro	COL4 reg
	.rept	4
	move.w	(a1)+,d4
	ext.l	d4
	asl.l	d0,d4
	add.l	#RND,d4
	swap	d4
	lsl.l	#8,\reg
	move.b	(a2,d4.w),\reg
	.endr
	.endm

	.globl	_mv_fill68k_copy_row0
_mv_fill68k_copy_row0:
	movem.l	d2-d4/a2,-(sp)
	movea.l	20(sp),a1		| the block's row 0
	movea.l	24(sp),a0		| the picture
	move.l	28(sp),d1		| its stride
	lea	_mpeg2_clip+3840,a2
	moveq	#10,d0
	COL4	d2
	COL4	d3
	moveq	#7,d4
1:
	move.l	d2,(a0)
	move.l	d3,4(a0)
	adda.l	d1,a0
	dbra	d4,1b
	movem.l	(sp)+,d2-d4/a2
	rts
