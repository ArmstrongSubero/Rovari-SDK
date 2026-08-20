/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_main.c
 * @brief System bootstrap and hidden main() for CH32V307.
 *
 * Provides the real main(). Users implement app_init() and app_run() in
 * their .rova file. Boot: startup.S -> main() -> system init -> app_init()
 * -> app_run() forever.
 */

#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"

/* Declared in rovari.h, implemented by user in their .rova file */
extern void app_init(void);
extern void app_run(void);

/* System tick (millis/micros), implemented in rovari_tick.c */
extern void rovari_tick_init(void);

/**
 * @brief System entry point: bootstrap, then run the user application.
 *
 * Initializes NVIC priority grouping, system clock, delay, and the
 * millisecond tick, calls app_init() once, then calls app_run() in the
 * top-level scheduler loop.
 *
 * @return Never returns under normal operation.
 * @req REQ-ROVARI-CORE-0010
 * @req REQ-ROVARI-CORE-0011
 */
int main(void)
{
    /* System bootstrap (the boilerplate users never need to see) */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    SEVS_INVARIANT(SystemCoreClock > 0U);   /* clock came up before we proceed */
    rovari_tick_init();        /* Start millis()/micros() tick */

    /* User initialization */
    app_init();

    /* Main loop */
    /* @sevs-bound: top-level scheduler loop; runs until power-off or
     *              watchdog reset. This is the embedded idle/scheduler
     *              loop, the one intentional unbounded loop per program. */
    while (1) {
        app_run();
    }

    return 0;  /* never reached */
}
