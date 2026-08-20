/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_tick.c
 * @brief millis()/micros() system tick on TIM7 for CH32V307.
 *
 * TIM7 (basic timer, APB1) runs at 1 MHz with a 1 ms update interrupt that
 * increments a millisecond counter. This reserves TIM7 for the system tick;
 * user code uses TIMER1-6 and TIMER8.
 */

#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"

static volatile uint32_t s_ms_counter = 0;

/**
 * @brief TIM7 update ISR; increments the millisecond counter every 1 ms.
 * @req REQ-ROVARI-TICK-0011
 */
void __attribute__((interrupt("machine"))) TIM7_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM7, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM7, TIM_IT_Update);
        s_ms_counter++;
    }
}

/**
 * @brief Initialize TIM7 as the 1 kHz system tick.
 *
 * Derives the prescaler from the actual APB1 timer clock (which is 2x PCLK1
 * when the APB1 prescaler is not 1). Called from rovari_main before app_init.
 * @req REQ-ROVARI-TICK-0010
 */
void rovari_tick_init(void)
{
    /* Enable TIM7 clock (APB1) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);

    /* TIM7 is on APB1. If APB1 prescaler != 1, timer clk = 2 x PCLK1. */
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    uint32_t timer_clk = (clk.PCLK1_Frequency == clk.HCLK_Frequency)
                          ? clk.PCLK1_Frequency
                          : clk.PCLK1_Frequency * 2U;
    SEVS_INVARIANT(timer_clk >= 1000000U);

    /* Configure: timer_clk / (PSC+1) = 1 MHz, overflow every 1000 ticks = 1 ms */
    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler     = (uint16_t)(timer_clk / 1000000U - 1U);
    tb.TIM_CounterMode   = TIM_CounterMode_Up;
    tb.TIM_Period        = 999;      /* 1 MHz / 1000 = 1 kHz overflow */
    tb.TIM_ClockDivision = 0;
    TIM_TimeBaseInit(TIM7, &tb);

    /* Clear pending, enable update interrupt */
    TIM_ClearITPendingBit(TIM7, TIM_IT_Update);
    TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);

    /* Enable in PFIC */
    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel                   = TIM7_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;  /* Highest priority */
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    /* Start */
    TIM_Cmd(TIM7, ENABLE);
}

/**
 * @brief Return milliseconds elapsed since tick init.
 * @return Millisecond count (wraps after ~49 days).
 * @req REQ-ROVARI-TICK-0011
 */
uint32_t millis(void)
{
    return s_ms_counter;
}

/**
 * @brief Return microseconds elapsed since tick init.
 *
 * Reads the millisecond counter and the TIM7 counter atomically by retrying
 * if a 1 ms rollover occurs between the two reads.
 * @return Microsecond count (wraps after ~71 minutes).
 * @req REQ-ROVARI-TICK-0012
 */
uint32_t micros(void)
{
    uint32_t ms1;
    uint32_t ms2;
    uint32_t cnt;
    /* Bounded retry: at most a handful of iterations even under contention. */
    for (uint32_t i = 0U; i < 4U; i++) {
        ms1 = s_ms_counter;
        cnt = TIM_GetCounter(TIM7);
        ms2 = s_ms_counter;
        if (ms1 == ms2) {
            break;
        }
    }
    return ms1 * 1000U + cnt;
}
