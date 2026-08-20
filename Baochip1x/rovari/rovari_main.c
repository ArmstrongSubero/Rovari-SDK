/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 */

/**
 * @file rovari_main.c
 * @brief System bootstrap and hidden main() for Baochip-1x.
 *
 * Provides the real main(). Users implement app_init() and app_run()
 * in their .rova file. The Baochip boot chain (boot0/boot1) handles
 * clock setup and low-level init before user code starts.
 */

#include <stdint.h>
#include "sevs_runtime.h"
#include "bao/stdlib.h"

extern void app_init(void);
extern void app_run(void);

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
    /* Initialize UART2 for printf and tick timer for delays */
    bao_init();

    /* Run C++ global constructors (Gpio led(PB, 1, Output) etc.) */
    __libc_init_array();

    app_init();

    /* @sevs-bound: top-level scheduler loop */
    while (1) {
        app_run();
    }

    return 0;
}
