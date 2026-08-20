/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_tick.c - millis() and micros() implementation (CH32H417)
 *
 * Uses TIM7 (basic timer) as a free-running 1 kHz tick.
 * This reserves TIM7 for the system tick.
 */

#include "debug.h"

static volatile uint32_t ms_counter = 0;

/*
 * TIM7 update interrupt - fires every 1 ms.
 */
void TIM7_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM7_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM7, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM7, TIM_IT_Update);
        ms_counter++;
    }
}

/*
 * Initialize TIM7 as 1 kHz system tick.
 * Called from rovari_main.c before app_init().
 */
void rovari_tick_init(void)
{
    /* Enable TIM7 clock */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_TIM7, ENABLE);

    /*
     * H417 RCC_ClocksTypeDef does not expose PCLK1.
     * Use HCLK as the timer clock base. If there is an APB prescaler,
     * the timer auto-doubles. For the default 400 MHz SYSCLK config,
     * HCLK and timer clock need to be determined from the actual
     * clock tree. Use HCLK_Frequency as a safe starting point.
     */
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    uint32_t timer_clk = clk.HCLK_Frequency;

    /* Configure: timer_clk / (PSC+1) = 1 MHz, overflow every 1000 ticks = 1 ms */
    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler     = (uint16_t)(timer_clk / 1000000 - 1);
    tb.TIM_CounterMode   = TIM_CounterMode_Up;
    tb.TIM_Period        = 999;      /* 1 MHz / 1000 = 1 kHz overflow */
    tb.TIM_ClockDivision = 0;
    TIM_TimeBaseInit(TIM7, &tb);

    /* Clear pending, enable update interrupt */
    TIM7->INTFR = 0;
    TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);

    /* Enable in PFIC */
    NVIC_EnableIRQ(TIM7_IRQn);

    /* Start */
    TIM_Cmd(TIM7, ENABLE);
}

uint32_t millis(void)
{
    return ms_counter;
}

uint32_t micros(void)
{
    uint32_t ms1, ms2, cnt;
    do {
        ms1 = ms_counter;
        cnt = TIM_GetCounter(TIM7);
        ms2 = ms_counter;
    } while (ms1 != ms2);

    return ms1 * 1000 + cnt;
}
