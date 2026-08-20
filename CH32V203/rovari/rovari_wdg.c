/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_wdg.c — Watchdog timer implementation
 *
 * IWDG timeout calculation:
 *   timeout_ms = (reload × prescaler) / 40   (LSI ≈ 40 kHz)
 *   reload = (timeout_ms × 40) / prescaler
 *   reload must be 0–4095 (12-bit)
 *
 * WWDG timing (APB1 = 72 MHz):
 *   t_wwdg = (4096 × prescaler × (counter - 63)) / 72,000,000
 *   With prescaler /8 and counter 127:
 *     (4096 × 8 × 64) / 72,000,000 ≈ 29.1 ms total window
 */

#include "rovari_wdg.h"
#include "debug.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  IWDG — Independent Watchdog
 * ═══════════════════════════════════════════════════════════════════════ */

void iwdg_start(uint32_t timeout_ms)
{
    /* Find the smallest prescaler that allows the desired timeout.
     * reload = (timeout_ms × 40) / prescaler, must be ≤ 4095.
     */
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
    uint16_t reload = 4095;  /* Fallback: maximum timeout */

    for (int i = 0; i < 7; i++) {
        uint32_t r = (timeout_ms * 40) / psc[i].divisor;
        if (r <= 4095) {
            prescaler_reg = psc[i].reg;
            reload = (uint16_t)r;
            break;
        }
    }

    /* Unlock IWDG registers */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);

    /* Set prescaler and reload */
    IWDG_SetPrescaler(prescaler_reg);
    IWDG_SetReload(reload);

    /* Load the reload value into the counter and start */
    IWDG_ReloadCounter();
    IWDG_Enable();
}

void iwdg_feed(void)
{
    IWDG_ReloadCounter();
}

/* ═══════════════════════════════════════════════════════════════════════
 *  WWDG — Window Watchdog
 * ═══════════════════════════════════════════════════════════════════════ */

void wwdg_start(uint8_t counter, uint8_t window)
{
    /* Enable WWDG clock (it's on APB1) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_WWDG, ENABLE);

    /* Set prescaler to /8 for a reasonable window duration */
    WWDG_SetPrescaler(WWDG_Prescaler_8);

    /* Set the window value */
    WWDG_SetWindowValue(window);

    /* Enable WWDG with initial counter value.
     * Bit 7 (WDGA) must be set to enable — the HAL does this. */
    WWDG_Enable(counter);
}

void wwdg_feed(uint8_t counter)
{
    WWDG_SetCounter(counter);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Reset cause detection
 * ═══════════════════════════════════════════════════════════════════════ */

uint8_t iwdg_was_reset(void)
{
    return (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) ? 1 : 0;
}

uint8_t wwdg_was_reset(void)
{
    return (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) != RESET) ? 1 : 0;
}

void wdg_clear_reset_flags(void)
{
    RCC_ClearFlag();
}
