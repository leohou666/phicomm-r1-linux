/* SPDX-License-Identifier: GPL-2.0-or-later */

	.syntax unified
	.arch armv7-a
	.arm
	.section .text.start, "ax"
	.global _start

/*
 * Rockchip BootROM request 0x472 loads and enters this image at 0x60000000.
 * The verified vendor DDR 0x471 stage has already configured UART2 for
 * 1500000 8N1, so only poll THRE and write the marker. Then stop forever.
 */
_start:
	ldr	r4, =0x11030000
	adr	r5, marker

1:	ldrb	r0, [r5], #1
	cmp	r0, #0
	beq	3f

2:	ldr	r1, [r4, #0x14]
	tst	r1, #0x20
	beq	2b
	str	r0, [r4]
	b	1b

3:	b	3b

marker:
	.asciz	"\r\nR1 472 ENTRY OK\r\n"
