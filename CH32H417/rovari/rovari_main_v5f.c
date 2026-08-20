/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_main_v5f.c - Hidden main() for the V5F core (CH32H417)
 *
 * Boot sequence (V5F is woken by V3F):
 *   1. V3F calls NVIC_WakeUp_V5F(0x00010000)
 *   2. V5F startup_ch32h417_v5f.S -> main()
 *   3. main() -> minimal system init (no SystemInit, V3F already did clocks)
 *   4. main() -> rovari_tick_init() (V5F gets its own millis/micros)
 *   5. main() -> app_init()
 *   6. main() -> app_run() loop
 *
 * V5F does NOT call SystemInit() because V3F has already configured
 * the PLL and system clocks. V5F only needs its own delay calibration
 * and core clock update.
 */

#include "debug.h"

/* Declared in the user's app_v5f.rova file */
extern void app_init(void);
extern void app_run(void);

/* System tick (millis/micros) - implemented in rovari_tick.c */
extern void rovari_tick_init(void);

int main(void)
{
    /* V5F does not call SystemInit() - V3F owns the clocks.
     * We only update the local clock variable and calibrate delays. */
    SystemAndCoreClockUpdate();
    Delay_Init();

    /* V5F does NOT call rovari_tick_init() because V3F owns TIM7.
     * V5F uses SysTick (configured by Delay_Init) for timing instead.
     * millis() and micros() are NOT available on V5F. */

    /* User initialization */
    app_init();

    /* Main loop */
    while (1) {
        app_run();
    }

    return 0;
}
