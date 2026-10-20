/* SPDX-License-Identifier: GPL-3.0-only */
/* The second core of the FM-1's chip (JieLi AC79 / WL82: two pi32v2 cores).
 *
 * NOT YET CHECKED ON AN FM-1. The JieLi SDK starts core 1 by giving it an entry and a stack and releasing
 * its reset (cpu/wl82 in the AC79 SDK); the registers have to be read there and tried on a real FM-1
 * before this returns 0. Until then it returns -1 and K-WA SUB renders the voices and the effects on core 0,
 * one after the other (ksub_dsp.c: app_audio): the same sound, one core.
 *
 * When it works: build with -DKS_DUAL=1; core 1 runs core1_main (the voices of the next block) while the
 * audio interrupt of core 0 puts the effects on the block before. */
#pragma once
#include <stdint.h>
static int fm1_cpu1_start(void (*entry)(void), uint32_t *stack_top)
{
    (void)entry;
    (void)stack_top;
    return -1;
}
