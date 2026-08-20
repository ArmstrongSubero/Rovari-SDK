/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */
/**
 * @file rovari_wdg.c
 * @brief IWDG + WWDG for CH32V003.
 */
#include <stdint.h>
#include "debug.h"
#include "rovari_wdg.h"

void iwdg_start(uint32_t timeout_ms)
{
    /* LSI ~40 kHz. Find prescaler + reload for the requested timeout. */
    static const uint8_t psc_values[]  = {4, 8, 16, 32, 64, 128, 256};
    static const uint16_t psc_enums[] = {
        IWDG_Prescaler_4, IWDG_Prescaler_8, IWDG_Prescaler_16,
        IWDG_Prescaler_32, IWDG_Prescaler_64, IWDG_Prescaler_128,
        IWDG_Prescaler_256
    };
    uint16_t psc_enum = IWDG_Prescaler_256;
    uint16_t reload   = 0xFFF;

    for (uint32_t i = 0; i < 7; i++) {
        uint32_t ticks = (40U * timeout_ms) / psc_values[i];
        if (ticks > 0 && ticks <= 0xFFF) {
            psc_enum = psc_enums[i];
            reload   = (uint16_t)(ticks > 0 ? ticks - 1 : 0);
            break;
        }
    }

    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(psc_enum);
    IWDG_SetReload(reload);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

void iwdg_feed(void) { IWDG_ReloadCounter(); }

void wwdg_start(uint8_t counter, uint8_t window)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_WWDG, ENABLE);
    WWDG_SetPrescaler(WWDG_Prescaler_8);
    WWDG_SetWindowValue(window);
    WWDG_Enable(counter);
}

void wwdg_feed(void) { WWDG_SetCounter(0x7F); }
