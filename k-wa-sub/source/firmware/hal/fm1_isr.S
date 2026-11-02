/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* IRQ entry wrappers: save what a C call may clobber, call the C body,
 * restore, csync, rti. clang does not emit the csync needed before rti,
 * so ISRs are not written in C directly. */
	.macro FM1_ISR name, body
	.section .text.\name,"ax",@progbits
	.globl \name
\name:
	[--sp] = {psr, rets, reti}
	[--sp] = {r3-r0}
	call \body
	{r3-r0} = [sp++]
	{psr, rets, reti} = [sp++]
	csync
	rti
	.endm

	FM1_ISR isr_timer5, fm1_timer5_irq
	FM1_ISR isr_alnk0, fm1_alnk0_irq

/* core 1's entry (fm1_cpu1.h, as JieLi's startup.S cpu1_start): its own stacks, then fm1_cpu1_main */
	.section .text.fm1_cpu1_entry,"ax",@progbits
	.globl fm1_cpu1_entry
fm1_cpu1_entry:
	usp = fm1_cpu1_ustk + 3072
	sp = fm1_cpu1_sstk + 1024
	reti = fm1_cpu1_main
	rti
