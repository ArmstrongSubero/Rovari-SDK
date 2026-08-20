/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_lptim.c - Low-Power Timer implementation (CH32H417)
 *
 * The CH32H417 has two low-power timers (LPTIM1, LPTIM2) on HB1.
 * They can run from LSI (40 kHz), LSE (32.768 kHz), HSI, or PCLK1.
 * LPTIM continues running in Sleep and Stop modes, making it useful
 * for periodic wake-up without keeping the main clocks active.
 *
 * LSI clock: ~40 kHz (uncalibrated, can vary 30-60 kHz)
 * With /128 prescaler: 40000/128 = 312.5 Hz tick (~3.2 ms per count)
 * Max period: 65535 * 3.2 ms = ~210 seconds
 *
 * LPTIM uses EXTI lines for wake-up:
 *   LPTIM1 -> EXTI_Line23
 *   LPTIM2 -> EXTI_Line24
 */

#include "rovari_lptim.h"
#include "debug.h"

/* -- Callback storage ----------------------------------------------------- */
static volatile LptimCallback lptim_callbacks[3] = {0};  /* index 1, 2 */

/* =========================================================================
 *  Public API
 * ========================================================================= */

void lptim_start(uint8_t instance, uint32_t period_ms, LptimCallback callback)
{
    LPTIM_TypeDef *lptim;
    uint32_t rcc_periph;
    IRQn_Type irqn;

    if (instance == 1) {
        lptim = LPTIM1;
        rcc_periph = RCC_HB1Periph_LPTIM1;
        irqn = LPTIM1_IRQn;
    } else if (instance == 2) {
        lptim = LPTIM2;
        rcc_periph = RCC_HB1Periph_LPTIM2;
        irqn = LPTIM2_IRQn;
    } else {
        return;
    }

    lptim_callbacks[instance] = callback;

    /* Enable LPTIM and PWR/BKP clocks */
    RCC_HB1PeriphClockCmd(rcc_periph | RCC_HB1Periph_PWR | RCC_HB1Periph_BKP, ENABLE);

    /* Enable LSI oscillator */
    RCC_LSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET);

    /* Enable LPTIM before configuring */
    LPTIM_Cmd(lptim, ENABLE);

    /* Calculate period register value.
     * LSI = 40 kHz, prescaler /128 = 312.5 Hz
     * period_counts = period_ms * 312.5 / 1000 = period_ms * 5 / 16 */
    uint32_t counts = (period_ms * 5) / 16;
    if (counts == 0) counts = 1;
    if (counts > 65535) counts = 65535;

    /* Configure LPTIM */
    LPTIM_TimeBaseInitTypeDef tb = {0};
    tb.LPTIM_ClockSource = LPTIM_ClockSource_In;
    tb.LPTIM_CountSource = LPTIM_CountSource_Internal;
    tb.LPTIM_ClockPrescaler = LPTIM_TClockPrescaler_DIV128;
    tb.LPTIM_InClockSource = LPTIM_InClockSource_LSI;
    tb.LPTIM_ClockPolarity = LPTIM_ClockPolarity_Falling;
    tb.LPTIM_ClockSampleTime = LPTIM_ClockSampleTime_0T;
    tb.LPTIM_TriggerSampleTime = LPTIM_TriggerSampleTime_0T;
    tb.LPTIM_ExTriggerPolarity = LPTIM_ExTriggerPolarity_Disable;
    tb.LPTIM_TimeOut = ENABLE;
    tb.LPTIM_OutputPolarity = LPTIM_OutputPolarity_High;
    tb.LPTIM_UpdateMode = LPTIM_UpdateMode0;
    tb.LPTIM_Encoder = DISABLE;
    tb.LPTIM_ForceOutHigh = DISABLE;
    tb.LPTIM_SingleMode = DISABLE;
    tb.LPTIM_ContinuousMode = ENABLE;
    tb.LPTIM_PWMOut = DISABLE;
    tb.LPTIM_CounterDirIndicat = DISABLE;
    tb.LPTIM_Pulse = 0;
    tb.LPTIM_Period = (uint16_t)counts;

    LPTIM_TimeBaseInit(lptim, &tb);

    /* Enable auto-reload match interrupt */
    LPTIM_ITConfig(lptim, LPTIM_IT_ARRM, ENABLE);

    /* Enable IRQ */
    NVIC_SetPriority(irqn, 1);
    NVIC_EnableIRQ(irqn);
}

void lptim_stop(uint8_t instance)
{
    LPTIM_TypeDef *lptim;

    if (instance == 1) lptim = LPTIM1;
    else if (instance == 2) lptim = LPTIM2;
    else return;

    LPTIM_ITConfig(lptim, LPTIM_IT_ARRM, DISABLE);
    LPTIM_Cmd(lptim, DISABLE);
    lptim_callbacks[instance] = 0;
}

uint16_t lptim_get_counter(uint8_t instance)
{
    if (instance == 1) return LPTIM_GetCounter(LPTIM1);
    if (instance == 2) return LPTIM_GetCounter(LPTIM2);
    return 0;
}

/* =========================================================================
 *  ISR handlers
 * ========================================================================= */

void LPTIM1_IRQHandler(void) __attribute__((interrupt("machine")));
void LPTIM1_IRQHandler(void)
{
    if (LPTIM_GetFlagStatus(LPTIM1, LPTIM_FLAG_ARRM) == SET) {
        LPTIM_ClearFlag(LPTIM1, LPTIM_FLAG_ARRM);
        if (lptim_callbacks[1]) {
            lptim_callbacks[1]();
        }
    }
}

void LPTIM2_IRQHandler(void) __attribute__((interrupt("machine")));
void LPTIM2_IRQHandler(void)
{
    if (LPTIM_GetFlagStatus(LPTIM2, LPTIM_FLAG_ARRM) == SET) {
        LPTIM_ClearFlag(LPTIM2, LPTIM_FLAG_ARRM);
        if (lptim_callbacks[2]) {
            lptim_callbacks[2]();
        }
    }
}
