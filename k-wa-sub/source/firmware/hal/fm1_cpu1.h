/* SPDX-License-Identifier: GPL-3.0-only */
/* The second core of the FM-1's chip (JieLi AC79 / WL82: two pi32v2 cores).
 *
 * The start is JieLi's own (AC79 SDK, system.a: port.c EnableOtherCpu, and cpu.a: startup.S cpu1_start):
 *   the address core 1 starts at goes to 0x01C7FFF8 (inside the write-protected top of RAM: opened for it),
 *   bit 3 of 0x10008 is set while it starts (and given back after), C1_CON bit 3 on and bit 1 (its reset) off,
 *   then core 1 says it runs. Core 1 gets its own stacks (fm1_isr.S: fm1_cpu1_entry). Its interrupts, stack
 *   limits and write limits are its own registers (q32DSP(1), C1_*): none is set, so it takes no interrupt.
 *
 * Core 1 runs from the XIP flash like core 0: before the flash is erased, written or read by command, it has to
 * wait in RAM (fm1_cpu1_park, .ram_text). fm1_cpu1_stop puts it back into reset (before an update, a crash).
 *
 *   fm1_cpu1_start(fn)   0: core 1 runs fn (never returns); -1: it did not answer within 50 ms (back in reset)
 *   fm1_cpu1_stop()      core 1 back into reset
 *   fm1_cpu1_park(&req, &parked)   (core 1, in RAM) parked = 1 while req, then 0 */
#pragma once
#include <stdint.h>
#include "fm1_guard.h"
#include "fm1_time.h"

#define FM1_C1_CON (*(volatile uint32_t *)0x1EEE004u)        /* corex2 C1_CON */
#define FM1_SYS_DIV (*(volatile uint32_t *)0x00010008u)       /* (EnableOtherCpu's "bkp_sys_div") */
#define FM1_CPU1_VEC (*(volatile uint32_t *)0x01C7FFF8u)      /* where core 1 starts */
#define FM1_CPU1_STACK 3072u

static inline void fm1_csync(void) { __asm__ volatile("csync" ::: "memory"); }   /* between the cores */
extern void fm1_cpu1_entry(void);               /* fm1_isr.S */
uint32_t fm1_cpu1_ustk[FM1_CPU1_STACK / 4u] __attribute__((aligned(8)));
uint32_t fm1_cpu1_sstk[256] __attribute__((aligned(8)));
static void (*volatile fm1_cpu1_fn)(void);
static volatile uint32_t fm1_cpu1_alive;

void fm1_cpu1_main(void)                        /* core 1, from fm1_cpu1_entry */
{
    fm1_cpu1_alive = 1u;
    for (;;)
        if (fm1_cpu1_fn) fm1_cpu1_fn();
}

static int fm1_cpu1_start(void (*fn)(void))
{
    uint32_t div, t0;
    fm1_cpu1_fn = fn;
    fm1_cpu1_alive = 0;
    fm1_guard_unlock_top();
    FM1_CPU1_VEC = (uint32_t)(uintptr_t)fm1_cpu1_entry;
    fm1_guard_lock_top();
    fm1_csync();
    div = FM1_SYS_DIV;
    FM1_SYS_DIV = div | 8u;
    FM1_C1_CON |= 8u;
    FM1_C1_CON &= ~2u;
    t0 = fm1_ticks();
    while (!fm1_cpu1_alive && fm1_ticks() - t0 < 50000u * FM1_TICKS_PER_US) {}
    FM1_SYS_DIV = div;
    if (!fm1_cpu1_alive) {
        FM1_C1_CON |= 2u;
        return -1;
    }
    return 0;
}

static void fm1_cpu1_stop(void) { FM1_C1_CON |= 2u; }

__attribute__((section(".ram_text"), noinline)) void fm1_cpu1_park(volatile uint32_t *req, volatile uint32_t *parked)
{
    *parked = 1u;                               /* (said from RAM: core 1 is off the flash from here) */
    while (*req) {}
    *parked = 0u;
}
