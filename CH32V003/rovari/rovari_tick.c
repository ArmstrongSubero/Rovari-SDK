/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_tick.c
 * @brief millis()/micros() system tick on TIM2 for CH32V003.
 *
 * CH32V003 has only TIM1 (advanced, APB2) and TIM2 (general, APB1).
 * TIM2 is used for the system tick, leaving TIM1 for the user
 * (PWM, capture, timer interrupts).
 *
 * TIM2 runs at 1 MHz with a 1 ms update interrupt that increments
 * a millisecond counter. At 48 MHz HCLK the APB1 prescaler is /1,
 * so timer clock = PCLK1 = 48 MHz. PSC = 47 gives 1 MHz tick.
 */

#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"

static volatile uint32_t s_ms_counter = 0;

/**
 * @brief TIM2 update ISR; increments the millisecond counter every 1 ms.
 * @req REQ-ROVARI-TICK-0011
 */
void __attribute__((interrupt("machine"))) TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        s_ms_counter++;
    }
}

/**
 * @brief Initialize TIM2 as the 1 kHz system tick.
 *
 * At 48 MHz with APB1 prescaler /1, the timer clock is 48 MHz.
 * PSC = 47 gives a 1 MHz counter. Period = 999 gives 1 kHz overflow.
 *
 * @req REQ-ROVARI-TICK-0010
 */
void rovari_tick_init(void)
{
    /* Enable TIM2 clock (APB1) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* Derive timer clock.
     * CH32V003: single APB domain. If HPRE divides, timer clk = HCLK.
     * At default 48 MHz with no APB prescaler: timer_clk = 48 MHz. */
    uint32_t timer_clk = SystemCoreClock;
    SEVS_INVARIANT(timer_clk >= 1000000U);

    /* Configure: timer_clk / (PSC+1) = 1 MHz, overflow every 1000 = 1 ms */
    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler     = (uint16_t)(timer_clk / 1000000U - 1U);
    tb.TIM_CounterMode   = TIM_CounterMode_Up;
    tb.TIM_Period        = 999;      /* 1 MHz / 1000 = 1 kHz overflow */
    tb.TIM_ClockDivision = 0;
    TIM_TimeBaseInit(TIM2, &tb);

    /* Clear pending UIF from TimeBaseInit, enable update interrupt */
    TIM2->INTFR = 0;
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    /* Enable in PFIC */
    NVIC_EnableIRQ(TIM2_IRQn);

    /* Start */
    TIM_Cmd(TIM2, ENABLE);
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
 * Reads the millisecond counter and the TIM2 counter atomically
 * by retrying if a 1 ms rollover occurs between the two reads.
 * @return Microsecond count (wraps after ~71 minutes).
 * @req REQ-ROVARI-TICK-0012
 */
uint32_t micros(void)
{
    uint32_t ms1;
    uint32_t ms2;
    uint32_t cnt;
    for (uint32_t i = 0U; i < 4U; i++) {
        ms1 = s_ms_counter;
        cnt = TIM_GetCounter(TIM2);
        ms2 = s_ms_counter;
        if (ms1 == ms2) {
            break;
        }
    }
    return ms1 * 1000U + cnt;
}
