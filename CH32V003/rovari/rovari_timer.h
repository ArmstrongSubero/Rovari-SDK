/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 * rovari_timer.h - Hardware timer interrupts for CH32V003
 * Only TIM1 available for user code (TIM2 reserved for millis/micros).
 */
#ifndef ROVARI_TIMER_H
#define ROVARI_TIMER_H
#include "rovari_defs.h"
typedef enum {
    TIMER1 = 1,
    TIMER2 = 2,  /* Reserved for millis()/micros(); do not use */
} TimerInstance;
typedef void (*TimerCallback)(void);
#ifdef __cplusplus
extern "C" {
#endif
void timer_start(TimerInstance inst, uint32_t freq_hz, TimerCallback callback);
void timer_start_ms(TimerInstance inst, uint32_t period_ms, TimerCallback callback);
void timer_stop(TimerInstance inst);
#ifdef __cplusplus
}
#endif
#endif
