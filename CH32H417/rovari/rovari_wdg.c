/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_wdg.c - Watchdog timer implementation (CH32H417)
 *
 * IWDG timeout calculation:
 *   timeout_ms = (reload x prescaler) / 40   (LSI ~ 40 kHz)
 *   reload = (timeout_ms x 40) / prescaler
 *   reload must be 0-4095 (12-bit)
 *
 * WWDG timing (HCLK = 150 MHz):
 *   t_wwdg = (4096 x prescaler x (counter - 63)) / HCLK
 *   With prescaler /8 and counter 127:
 *     (4096 x 8 x 64) / 150,000,000 ~ 14 ms total window
 */

#include "rovari_wdg.h"
#include "debug.h"

/* =========================================================================
 *  IWDG - Independent Watchdog
 * ========================================================================= */

void iwdg_start(uint32_t timeout_ms)
{
    /* Find the smallest prescaler that allows the desired timeout.
     * reload = (timeout_ms x 40) / prescaler, must be <= 4095. */
    static const struct {
        uint8_t  reg;
        uint16_t divisor;
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
    uint16_t reload = 4095;

    for (int i = 0; i < 7; i++) {
        uint32_t r = (timeout_ms * 40) / psc[i].divisor;
        if (r <= 4095) {
            prescaler_reg = psc[i].reg;
            reload = (uint16_t)r;
            break;
        }
    }

    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(prescaler_reg);
    IWDG_SetReload(reload);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

void iwdg_feed(void)
{
    IWDG_ReloadCounter();
}

/* =========================================================================
 *  WWDG - Window Watchdog
 * ========================================================================= */

void wwdg_start(uint8_t counter, uint8_t window)
{
    /* Enable WWDG clock (HB1 bus on H417) */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_WWDG, ENABLE);

    WWDG_SetPrescaler(WWDG_Prescaler_8);
    WWDG_SetWindowValue(window);
    WWDG_Enable(counter);
}

void wwdg_feed(uint8_t counter)
{
    WWDG_SetCounter(counter);
}

/* =========================================================================
 *  Reset cause detection
 * ========================================================================= */

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
