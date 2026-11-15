| mpegv 2.1: a block's coefficients, decoded and dequantised, for the
| 68020 and later, written by hand: slice.c's get_non_intra_block,
| get_intra_block_B14 and _B15, get_mpeg1_non_intra_block and
| get_mpeg1_intra_block, to the bit (the same coefficients, the same
| bitstream state after, the same i returned; corrupt streams included),
| with the bit buffer (d0), its count (d1) and its pointer (a0) kept in
| registers across the block.
|
| A code of 8 bits or fewer (all but the longest few) is one look-up in a
| table of 256 by the stream's next 8 bits (slice.c's mv_vlc_init builds
| them from vlc.h's): 4 words, the run (0x1000 + 65 an escape, 0x2000 a
| longer code, 129 the end of the block), the level (a non-intra block's
| tables: 2 * level + 1), the code's length
| and its sign's (the end of the block's: its own), and the code's own
| (with 0x100 if it is of 6 to 8 bits: past the block's end slice.c takes
| it for an escape). One
| LSL.L by that takes the code and its sign off together, the sign into
| the carry. Longer codes and escapes are slice.c's own sums, here.
|
|   int mv_vlc68k_<name> (uint32_t * bitstream, const uint16_t * quant,
|                         const uint8_t * scan, int16_t * dest,
|                         const mv_vlctabs * tabs)
|
| bitstream: the decoder's bitstream_buf, _bits and _ptr, in that order.
| tabs: the table of 256 (and the non-intra first code's), then the 10,
| 13, 15 and 16 bit tables (vlc.h's DCT_B14_10 or DCT_B15_10, DCT_13...).
| Returns i (the non-intra blocks' last, for the IDCT).
|
| (c) 2026 Brian King. GPL-3.0-or-later (mpegv.library's LICENSE).

| the arguments, after a local (4) and the saved registers (44)
VARGS	= 4 + 44 + 4

| at least 16 bits in d0 (slice.c's NEEDBITS); uses d6
	.macro	NEEDBITS
	tst.w	d1
	ble	.Lnb\@
	moveq	#0,d6
	move.w	(a0)+,d6
	lsl.l	d1,d6
	or.l	d6,d0
	sub.w	#16,d1
.Lnb\@:
	.endm

| d5 (signed) to -2048..2047, times 16, stored at dest[j] (j: d4); with
| MPEG-2 also into the mismatch (d3)
	.macro	STORESAT mpeg2
	cmp.l	a6,d5
	ble	.Ls1\@
	move.l	a6,d5
	bra	.Ls2\@
.Ls1\@:	cmp.l	#-2048,d5
	bge	.Ls2\@
	move.l	#-2048,d5
.Ls2\@:	lsl.w	#4,d5
	move.w	d5,(a1,d4.w*2)
	.if \mpeg2
	eor.w	d5,d3
	.endif
	.endm

| d5 (signed) divided by 2^n, towards zero (C's /)
	.macro	DIVPOW n
	tst.l	d5
	bpl	.Ld\@
	add.l	#(1<<\n)-1,d5
.Ld\@:	asr.l	#\n,d5
	.endm

	.macro	BLOCK name, mpeg2, intra
	.globl	_mv_vlc68k_\name
_mv_vlc68k_\name:
	movem.l	d2-d7/a2-a6,-(sp)
	subq.l	#4,sp
	move.l	VARGS(sp),a0
	move.l	(a0)+,d0
	move.l	(a0)+,d1
	move.l	(a0),a0
	move.l	VARGS+4(sp),a3
	move.l	VARGS+8(sp),a2
	move.l	VARGS+12(sp),a1
	move.l	VARGS+16(sp),a5
	move.l	(a5),a4
	movea.w	#2047,a6
	.if \intra
	moveq	#0,d2
	.if \mpeg2
	move.w	(a1),d3
	not.w	d3
	.endif
	NEEDBITS
	.else
	moveq	#-1,d2
	moveq	#-1,d3
	NEEDBITS
	| the first code: its own table (a 1 is run 1, level 1, not the end)
	move.l	4(a5),a5
	bfextu	d0{0:8},d4
	lea	(a5,d4.w*8),a5
	bra	.Llook_\name
	.endif

| the next code
.Lnext_\name:
	bfextu	d0{0:8},d4		| the next 8 bits
	lea	(a4,d4.w*8),a5
.Llook_\name:
	add.w	(a5)+,d2		| i += run
	cmp.w	#64,d2
	bhs	.Lspec_\name
| a coefficient: a5 at its level, then its length (with the sign's)
.Lnorm_\name:
	move.b	(a2,d2.w),d4		| j = scan[i]
	move.w	(a5)+,d5		| (non-intra: 2 * level + 1)
	mulu.w	(a3,d4.w*2),d5
	.if \intra
	lsr.l	#4,d5
	.else
	lsr.l	#5,d5
	.endif
	.if !\mpeg2
	subq.l	#1,d5			| MPEG-1's oddification: (val - 1) | 1
	bcs	.Lsatd_\name		| (0: -1)
	or.w	#1,d5
	.endif
	cmp.l	a6,d5
	bhi	.Lsat_\name
.Lsatd_\name:
	move.w	(a5),d6
	lsl.l	d6,d0			| the code and its sign, the sign in C
	bcc	.Lpos_\name
	neg.w	d5
.Lpos_\name:
	lsl.w	#4,d5
	move.w	d5,(a1,d4.w*2)
	.if \mpeg2
	eor.w	d5,d3
	.endif
	add.w	d6,d1
	ble	.Lnext_\name
	moveq	#0,d6
	move.w	(a0)+,d6
	lsl.l	d1,d6
	or.l	d6,d0
	sub.w	#16,d1
	bra	.Lnext_\name

| more than 2047 before its sign: 2047, or 2048 if it is to be negative
.Lsat_\name:
	move.l	a6,d5
	move.w	(a5),d6
	move.l	d0,d7
	lsl.l	d6,d7			| the sign, in C
	bcc	.Lsatd_\name
	addq.l	#1,d5
	bra	.Lsatd_\name

| the end of the block, an escape, or a longer code
.Lspec_\name:
	cmp.w	#0x1000,d2
	blo	.Lover_\name
	cmp.w	#0x1800,d2		| (0x2000 + i, i from -1)
	bhs	.Llong_\name
	sub.w	#0x1000,d2		| an escape: i has had 65 added
	moveq	#12,d6
| (from a code of 6 to 8 bits past the block's end too: slice.c takes it
| for an escape; d6 the length taken if that is not valid either)
.Lesc_\name:
	bfextu	d0{6:6},d4
	add.w	d4,d2
	sub.w	#64,d2
	cmp.w	#64,d2
	bhs	.Lexit6_\name		| not a valid one
	moveq	#0,d4
	move.b	(a2,d2.w),d4
	moveq	#12,d6
	lsl.l	d6,d0
	add.w	d6,d1
	NEEDBITS
	.if \mpeg2
	move.l	d0,d5
	moveq	#20,d6
	asr.l	d6,d5			| SBITS (bit_buf, 12)
	.if \intra
	muls.w	(a3,d4.w*2),d5
	DIVPOW	4
	.else
	tst.l	d0
	bpl	1f
	subq.l	#1,d5
1:	add.l	d5,d5
	addq.l	#1,d5
	muls.w	(a3,d4.w*2),d5
	DIVPOW	5
	.endif
	STORESAT 1
	moveq	#12,d6
	lsl.l	d6,d0
	add.w	d6,d1
	.else
	move.l	d0,d5
	moveq	#24,d6
	asr.l	d6,d5			| SBITS (bit_buf, 8)
	moveq	#127,d6
	and.l	d5,d6
	bne	2f
	lsl.l	#8,d0			| 16 bits of level
	addq.w	#8,d1
	add.l	d5,d5
	move.l	d0,d6
	moveq	#24,d7
	lsr.l	d7,d6
	add.l	d6,d5
2:
	.if !\intra
	tst.l	d5
	bpl	3f
	subq.l	#1,d5
3:	add.l	d5,d5
	addq.l	#1,d5
	muls.w	(a3,d4.w*2),d5
	DIVPOW	5
	.else
	muls.w	(a3,d4.w*2),d5
	DIVPOW	4
	.endif
	tst.l	d5			| oddification
	bmi	4f
	subq.l	#1,d5
4:	or.w	#1,d5
	STORESAT 0
	lsl.l	#8,d0
	addq.w	#8,d1
	.endif
	NEEDBITS
	bra	.Lnext_\name

| a code longer than 8 bits: vlc.h's tables, as slice.c does
.Llong_\name:
	sub.w	#0x2000,d2
	move.l	VARGS+16(sp),a5
	cmp.l	#0x02000000,d0
	blo	1f
	bfextu	d0{0:10},d4
	subq.w	#8,d4
	move.l	8(a5),a5
	bra	4f
1:	cmp.l	#0x00800000,d0
	blo	2f
	bfextu	d0{0:13},d4
	sub.w	#16,d4
	move.l	12(a5),a5
	bra	4f
2:	cmp.l	#0x00200000,d0
	blo	3f
	bfextu	d0{0:15},d4
	sub.w	#16,d4
	move.l	16(a5),a5
	bra	4f
3:	bfextu	d0{0:16},d4		| 16 bits: those taken, 16 more read
	move.l	20(a5),a5
	swap	d0
	clr.w	d0
	moveq	#0,d6
	move.w	(a0)+,d6
	moveq	#16,d7
	add.w	d1,d7
	lsl.l	d7,d6
	or.l	d6,d0
4:	lea	(a5,d4.w*2),a5		| a DCTtab: run, level, length
	adda.w	d4,a5
	moveq	#0,d5
	move.b	(a5)+,d5
	add.w	d5,d2
	moveq	#0,d6
	move.b	1(a5),d6
	cmp.w	#64,d2
	bhs	.Lexit6_\name		| the end, or not valid: its length
	move.b	(a5),d5
	.if !\intra
	add.w	d5,d5
	addq.w	#1,d5			| 2 * level + 1
	.endif
	addq.w	#1,d6
	move.w	d5,(sp)			| as the table of 256's: level, length
	move.w	d6,2(sp)
	move.l	sp,a5
	moveq	#0,d4
	bra	.Lnorm_\name

| the end of the block (i from 128), or a code past it: its length (with
| 0x100: of 6 to 8 bits, which slice.c takes for an escape)
.Lover_\name:
	cmp.w	#128,d2
	bhs	.Leob_\name
	move.w	4(a5),d6
	bclr	#8,d6
	bne	.Lesc_\name
	bra	.Lexit6_\name
.Leob_\name:
	move.w	2(a5),d6
| the end: the last code's length taken, the state back
.Lexit6_\name:
	lsl.l	d6,d0
	add.w	d6,d1
	.if \mpeg2
	and.w	#16,d3			| dest[63] ^= mismatch & 16
	eor.w	d3,126(a1)
	.endif
	move.l	VARGS(sp),a5
	move.l	d0,(a5)+
	ext.l	d1
	move.l	d1,(a5)+
	move.l	a0,(a5)
	move.w	d2,d0
	ext.l	d0
	addq.l	#4,sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts
	.endm

	.text
	BLOCK	ni2, 1, 0
	BLOCK	intra2, 1, 1
	BLOCK	ni1, 0, 0
	BLOCK	intra1, 0, 1
