/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 * rovari_wdg.h - Watchdog timer abstraction (IWDG + WWDG)
 */
#ifndef ROVARI_WDG_H
#define ROVARI_WDG_H
#include "rovari_defs.h"
#ifdef __cplusplus
extern "C" {
#endif
void iwdg_start(uint32_t timeout_ms);
void iwdg_feed(void);
void wwdg_start(uint8_t counter, uint8_t window);
void wwdg_feed(void);
#ifdef __cplusplus
}
#endif
#endif
