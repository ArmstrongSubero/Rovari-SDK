/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */
/**
 * @file rovari_timer.c
 * @brief Hardware timer interrupts for CH32V003.
 * Only TIM1 is available for user code. TIM2 is reserved for millis/micros.
 * TIM1 is on APB2; timer clock = SystemCoreClock.
 */
#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_timer.h"

static TimerCallback s_tim1_callback = NULL;

void __attribute__((interrupt("machine"))) TIM1_UP_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
        if (s_tim1_callback != NULL) {
            s_tim1_callback();
        }
    }
}

void timer_start(TimerInstance inst, uint32_t freq_hz, TimerCallback callback)
{
    if (inst != TIMER1) return;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    uint32_t timer_clk = SystemCoreClock;
    uint32_t total = (freq_hz != 0U) ? (timer_clk / freq_hz) : 1U;
    if (total == 0U) total = 1U;

    uint16_t psc = 0;
    uint16_t arr = 0;
    for (uint32_t p = 0; p < 65536U; p++) {
        uint32_t a = total / (p + 1U) - 1U;
        if (a <= 65535U) {
            psc = (uint16_t)p;
            arr = (uint16_t)a;
            break;
        }
    }

    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler     = psc;
    tb.TIM_CounterMode   = TIM_CounterMode_Up;
    tb.TIM_Period        = arr;
    tb.TIM_ClockDivision = 0;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tb);

    TIM1->INTFR = 0;
    TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);

    s_tim1_callback = callback;
    NVIC_EnableIRQ(TIM1_UP_IRQn);
    TIM_Cmd(TIM1, ENABLE);
}

void timer_start_ms(TimerInstance inst, uint32_t period_ms, TimerCallback callback)
{
    if (period_ms == 0) return;
    uint32_t freq = 1000U / period_ms;
    if (freq == 0) freq = 1;
    timer_start(inst, freq, callback);
}

void timer_stop(TimerInstance inst)
{
    if (inst != TIMER1) return;
    TIM_Cmd(TIM1, DISABLE);
    TIM_ITConfig(TIM1, TIM_IT_Update, DISABLE);
    s_tim1_callback = NULL;
}
