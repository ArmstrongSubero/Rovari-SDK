/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari
 *
 * rovari_tick.c - millis() and micros() for CH32V203
 *
 * Reads SysTick->CNT directly (64-bit free-running counter at HCLK/8).
 * SysTick is already configured and running via Delay_Init() in debug.c.
 * No additional interrupt or timer is consumed.
 *
 * At 144 MHz: SysTick clk = 18 MHz, so 1 us = 18 ticks.
 */

#include "debug.h"

/* rovari_tick_init: nothing to do, SysTick already runs from Delay_Init */
void rovari_tick_init(void)
{
    /* SysTick is configured by Delay_Init() called in main().
     * CNT is a 64-bit free-running up-counter at HCLK/8.
     * We just read it for millis()/micros(). */
}

uint32_t millis(void)
{
    uint64_t cnt = SysTick->CNT;
    uint32_t ticks_per_ms = SystemCoreClock / 8 / 1000;
    return (uint32_t)(cnt / ticks_per_ms);
}

uint32_t micros(void)
{
    uint64_t cnt = SysTick->CNT;
    uint32_t ticks_per_us = SystemCoreClock / 8 / 1000000;
    return (uint32_t)(cnt / ticks_per_us);
}
