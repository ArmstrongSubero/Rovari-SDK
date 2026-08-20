/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 * rovari_exti.h - External interrupt abstraction for CH32V003
 * CH32V003 has a single combined EXTI7_0_IRQHandler for lines 0-7.
 */
#ifndef ROVARI_EXTI_H
#define ROVARI_EXTI_H
#include "rovari_defs.h"
typedef enum { Rising = 0, Falling = 1, Change = 2 } EdgeMode;
typedef void (*ExtiCallback)(void);
#ifdef __cplusplus
extern "C" {
#endif
void attach_interrupt(pin_t pin, EdgeMode edge, ExtiCallback callback);
void detach_interrupt(pin_t pin);
uint32_t interrupts_disable(void);
void interrupts_restore(uint32_t state);
#ifdef __cplusplus
}
#endif
#endif
