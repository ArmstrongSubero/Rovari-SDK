/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 */

/**
 * @file rovari_tick.c
 * @brief Rovari micros() for Baochip-1x.
 *
 * millis() and delay_ms()/delay_us() are provided by the vendor
 * layer (bao_delay.c). This file adds micros() which is Rovari
 * specific and not part of the Dabao SDK.
 */

#include <stdint.h>
#include "hardware/regs/addressmap.h"
#include "hardware/regs/timer.h"
#include "bao/platform.h"
#include "bao/stdlib.h"

void rovari_tick_init(void)
{
    /* Tick timer is initialized by bao_init() -> delay_ms(0).
     * Nothing additional needed here. */
}

/**
 * @brief Returns microseconds since boot (32-bit, wraps after ~71 min).
 *
 * The Baochip TickTimer counts in milliseconds, so microsecond
 * precision is estimated from the RISC-V cycle counter.
 *
 * @req REQ-ROVARI-TICK-0002
 */
uint32_t micros(void)
{
    uint32_t ms = (uint32_t)millis();
    uint32_t cycles;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(cycles));

    uint32_t sub_ms_cycles = cycles % (ACLK_HZ / 1000U);
    uint32_t sub_ms_us = sub_ms_cycles / (ACLK_HZ / 1000000U);

    return (ms * 1000U) + sub_ms_us;
}
