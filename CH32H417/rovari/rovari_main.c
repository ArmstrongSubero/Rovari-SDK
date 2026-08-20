/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_main.c - Hidden main() that bootstraps the system (CH32H417)
 *
 * Boot sequence:
 *   1. startup.S -> main()
 *   2. main() -> system init (clocks, delay)
 *   3. main() -> rovari_tick_init() (millis/micros)
 *   4. If ROVARI_DUAL_CORE: wake V5F core
 *   5. main() -> app_init()    (user's one-time setup)
 *   6. main() -> app_run()     (user's loop, called forever)
 *
 * When ROVARI_DUAL_CORE is defined (by the build system when Dual Core
 * is enabled), V3F wakes V5F via NVIC_WakeUp_V5F() before entering the
 * user's app_init(). V5F runs its own main() from rovari_main_v5f.c.
 */

#include "debug.h"

/* Declared in rovari.h, implemented by user in their .rova file */
extern void app_init(void);
extern void app_run(void);

/* System tick (millis/micros) - implemented in rovari_tick.c */
extern void rovari_tick_init(void);

int main(void)
{
    /* -- System bootstrap ------------------------------------------- */
    SystemInit();
    SystemAndCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    rovari_tick_init();        /* Start millis()/micros() tick */

#ifdef ROVARI_DUAL_CORE
    /* Wake the V5F core. It will run its own main() from
     * rovari_main_v5f.c, which calls the user's app_v5f.rova code. */
    NVIC_WakeUp_V5F(Core_V5F_StartAddr);
#endif

    /* -- User initialization ---------------------------------------- */
    app_init();

    /* -- Main loop -------------------------------------------------- */
    while (1) {
        app_run();
    }

    return 0;  /* never reached */
}
