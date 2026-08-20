/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari
 *
 * rovari_main.c - Hidden main() for CH32V203 (bootloader mode)
 *
 * Boot sequence:
 *   1. Bootloader at 0x00000000 checks magic word / BOOT pin / app validity
 *   2. Bootloader jumps to 0x00005000 via SW_Handler trampoline
 *   3. startup.S runs: sp, gp, .data, .bss, __libc_init_array, main()
 *   4. main() below initializes system + USB CDC, then runs user code
 *
 * USB CDC is initialized here (not in user code) because it is MANDATORY
 * for bootloader re-entry.  Without USB CDC running, there is no way to
 * send the "ROVBOOT" command to re-enter the bootloader for reflash.
 * The board would be bricked until the user physically holds the BOOT button.
 *
 * The ROVBOOT listener lives in usb_endp.c (EP2_OUT_Callback).
 * It watches for the 7-byte "ROVBOOT" string on the bulk OUT endpoint,
 * writes 0xDEADBEEF to 0x20000400, and triggers NVIC_SystemReset().
 */

#include "debug.h"
#include "usb_lib.h"
#include "hw_config.h"
#include "usb_pwr.h"

/* Declared in rovari.h, implemented by user in their .rova file */
extern void app_init(void);
extern void app_run(void);

/* System tick (millis/micros) - implemented in rovari_tick.c */
extern void rovari_tick_init(void);

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    /* Re-enable GPIOA clock (bootloader disables it before jump).
     * MUST happen before USB init - USB uses PA11/PA12. */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* Initialize USB CDC.
     * This starts the USB peripheral and enables the ROVBOOT listener
     * on EP2 OUT so the board can always be reflashed via Rovari Studio. */
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();

    /* Start millis()/micros() (reads SysTick CNT) */
    rovari_tick_init();

    /* User initialization */
    app_init();

    /* Main loop */
    while (1) {
        app_run();
    }

    return 0;  /* never reached */
}
