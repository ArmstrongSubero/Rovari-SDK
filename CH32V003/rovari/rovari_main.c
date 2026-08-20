/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_main.c
 * @brief System bootstrap and hidden main() for CH32V003.
 *
 * Provides the real main(). Users implement app_init() and app_run()
 * in their .rova file.
 */

#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"

extern void app_init(void);
extern void app_run(void);
extern void rovari_tick_init(void);

/* Provided by newlib; iterates .preinit_array, .init, .init_array
 * to call C++ global constructors. Required because startup.S uses
 * -nostartfiles and jumps to main directly, skipping crt0. */
extern void __libc_init_array(void);

/**
 * @brief System entry point.
 * @req REQ-ROVARI-CORE-0010
 * @req REQ-ROVARI-CORE-0011
 */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    SEVS_INVARIANT(SystemCoreClock > 0U);

    rovari_tick_init();

    /* Run C++ global constructors (Gpio led(PC0, Output) etc.) */
    __libc_init_array();

    app_init();

    /* @sevs-bound: top-level scheduler loop */
    while (1) {
        app_run();
    }

    return 0;
}
