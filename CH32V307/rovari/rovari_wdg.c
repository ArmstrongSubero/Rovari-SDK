/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_wdg.c
 * @brief Independent (IWDG) and window (WWDG) watchdog driver.
 *
 * IWDG: timeout_ms = (reload * prescaler) / 40 (LSI ~= 40 kHz), reload 0-4095.
 * WWDG: with prescaler /8 and counter 127, the total window is ~29 ms.
 */

#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_wdg.h"

#define IWDG_RELOAD_MAX  4095U
#define IWDG_PSC_COUNT   7

/**
 * @brief Start the IWDG with the smallest prescaler meeting the timeout.
 * @param[in] timeout_ms Desired timeout in milliseconds.
 * @req REQ-ROVARI-WDG-0010
 */
void iwdg_start(uint32_t timeout_ms)
{
    static const struct {
        uint8_t  reg;       /* IWDG_Prescaler_xxx value */
        uint16_t divisor;   /* Actual divider */
    } psc[] = {
        { IWDG_Prescaler_4,   4   },
        { IWDG_Prescaler_8,   8   },
        { IWDG_Prescaler_16,  16  },
        { IWDG_Prescaler_32,  32  },
        { IWDG_Prescaler_64,  64  },
        { IWDG_Prescaler_128, 128 },
        { IWDG_Prescaler_256, 256 },
    };

    uint8_t  prescaler_reg = IWDG_Prescaler_256;
    uint16_t reload = IWDG_RELOAD_MAX;  /* Fallback: maximum timeout */

    for (int i = 0; i < IWDG_PSC_COUNT; i++) {
        uint32_t r = (timeout_ms * 40U) / psc[i].divisor;
        if (r <= IWDG_RELOAD_MAX) {
            prescaler_reg = psc[i].reg;
            reload = (uint16_t)r;
            break;
        }
    }
    SEVS_INVARIANT(reload <= IWDG_RELOAD_MAX);

    /* Unlock IWDG registers */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);

    /* Set prescaler and reload */
    IWDG_SetPrescaler(prescaler_reg);
    IWDG_SetReload(reload);

    /* Load the reload value into the counter and start */
    IWDG_ReloadCounter();
    IWDG_Enable();
}

/**
 * @brief Reload (feed) the IWDG counter.
 * @req REQ-ROVARI-WDG-0011
 */
void iwdg_feed(void)
{
    IWDG_ReloadCounter();
}

/**
 * @brief Enable and configure the WWDG.
 * @param[in] counter Initial down-counter value (bit 6 must be set to run).
 * @param[in] window  Window value below which a refresh is allowed.
 * @req REQ-ROVARI-WDG-0012
 */
void wwdg_start(uint8_t counter, uint8_t window)
{
    /* Enable WWDG clock (it's on APB1) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_WWDG, ENABLE);

    /* Set prescaler to /8 for a reasonable window duration */
    WWDG_SetPrescaler(WWDG_Prescaler_8);

    /* Set the window value */
    WWDG_SetWindowValue(window);

    /* Enable WWDG with initial counter value (HAL sets WDGA). */
    WWDG_Enable(counter);
}

/**
 * @brief Refresh the WWDG counter within its window.
 * @param[in] counter New counter value.
 * @req REQ-ROVARI-WDG-0013
 */
void wwdg_feed(uint8_t counter)
{
    WWDG_SetCounter(counter);
}

/**
 * @brief Report whether the last reset was caused by the IWDG.
 * @return 1 if the IWDG reset flag is set, 0 otherwise.
 * @req REQ-ROVARI-WDG-0014
 */
uint8_t iwdg_was_reset(void)
{
    return (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) ? 1 : 0;
}

/**
 * @brief Report whether the last reset was caused by the WWDG.
 * @return 1 if the WWDG reset flag is set, 0 otherwise.
 * @req REQ-ROVARI-WDG-0014
 */
uint8_t wwdg_was_reset(void)
{
    return (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) != RESET) ? 1 : 0;
}

/**
 * @brief Clear the RCC reset-cause flags.
 * @req REQ-ROVARI-WDG-0014
 */
void wdg_clear_reset_flags(void)
{
    RCC_ClearFlag();
}
