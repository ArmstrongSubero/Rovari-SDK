/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari
 *
 * rovari_timer.c - Hardware timer interrupt implementation (CH32V203)
 *
 * Timer architecture (CH32V203):
 *   - TIM1 is on APB2 (144 MHz) - advanced timer
 *   - TIM2, TIM3, TIM4 are on APB1 (72 MHz) - general purpose
 *   - TIM5 has no IRQ on D6 variant - not available for timer interrupts
 *   - No TIM6, TIM7, TIM8 on CH32V203
 */

#include "rovari_timer.h"
#include "debug.h"

/* Instance lookup table */
typedef struct {
    TIM_TypeDef* periph;
    uint32_t     rcc;
    uint8_t      apb_bus;
    IRQn_Type    irqn;
} TimerDef;

static const TimerDef timer_defs[] = {
    [0] = {0},
    [1] = { TIM1,  RCC_APB2Periph_TIM1,  2, TIM1_UP_IRQn },
    [2] = { TIM2,  RCC_APB1Periph_TIM2,  1, TIM2_IRQn    },
    [3] = { TIM3,  RCC_APB1Periph_TIM3,  1, TIM3_IRQn    },
    [4] = { TIM4,  RCC_APB1Periph_TIM4,  1, TIM4_IRQn    },
    /* TIM5 has no IRQ on CH32V203 D6 - cannot use for interrupts */
};

#define TIMER_DEF_COUNT (sizeof(timer_defs) / sizeof(timer_defs[0]))

static volatile TimerCallback timer_callbacks[5] = {0};

static inline const TimerDef* get_def(TimerInstance inst)
{
    if (inst == 0 || inst >= TIMER_DEF_COUNT) return &timer_defs[2];
    return &timer_defs[inst];
}

static uint32_t get_timer_clock(uint8_t apb_bus)
{
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);

    if (apb_bus == 2) {
        return (clk.PCLK2_Frequency == clk.HCLK_Frequency)
                 ? clk.PCLK2_Frequency
                 : clk.PCLK2_Frequency * 2;
    } else {
        return (clk.PCLK1_Frequency == clk.HCLK_Frequency)
                 ? clk.PCLK1_Frequency
                 : clk.PCLK1_Frequency * 2;
    }
}

static void calc_prescaler_period(uint32_t timer_clk, uint32_t freq_hz,
                                  uint16_t* out_psc, uint16_t* out_arr)
{
    uint32_t total = timer_clk / freq_hz;
    if (total == 0) total = 1;

    for (uint32_t psc = 0; psc < 65536; psc++) {
        uint32_t arr = total / (psc + 1) - 1;
        if (arr <= 65535) {
            *out_psc = (uint16_t)psc;
            *out_arr = (uint16_t)arr;
            return;
        }
    }

    *out_psc = 65535;
    *out_arr = 65535;
}

void timer_start(TimerInstance inst, uint32_t freq_hz, TimerCallback callback)
{
    const TimerDef* def = get_def(inst);
    timer_callbacks[inst] = callback;

    if (def->apb_bus == 2)
        RCC_APB2PeriphClockCmd(def->rcc, ENABLE);
    else
        RCC_APB1PeriphClockCmd(def->rcc, ENABLE);

    uint32_t timer_clk = get_timer_clock(def->apb_bus);
    uint16_t psc, arr;
    calc_prescaler_period(timer_clk, freq_hz, &psc, &arr);

    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler         = psc;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_Period            = arr;
    tb.TIM_ClockDivision     = 0;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(def->periph, &tb);

    TIM_ClearITPendingBit(def->periph, TIM_IT_Update);
    TIM_ITConfig(def->periph, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel                   = def->irqn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(def->periph, ENABLE);
}

void timer_start_ms(TimerInstance inst, uint32_t period_ms, TimerCallback callback)
{
    const TimerDef* def = get_def(inst);
    timer_callbacks[inst] = callback;

    if (def->apb_bus == 2)
        RCC_APB2PeriphClockCmd(def->rcc, ENABLE);
    else
        RCC_APB1PeriphClockCmd(def->rcc, ENABLE);

    uint32_t timer_clk = get_timer_clock(def->apb_bus);
    uint16_t psc, arr;

    if (period_ms <= 65535) {
        psc = (uint16_t)(timer_clk / 1000 - 1);
        arr = (uint16_t)(period_ms - 1);
    } else {
        psc = (uint16_t)(timer_clk / 100 - 1);
        arr = (uint16_t)(period_ms / 10 - 1);
    }

    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler         = psc;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_Period            = arr;
    tb.TIM_ClockDivision     = 0;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(def->periph, &tb);

    TIM_ClearITPendingBit(def->periph, TIM_IT_Update);
    TIM_ITConfig(def->periph, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel                   = def->irqn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(def->periph, ENABLE);
}

void timer_stop(TimerInstance inst)
{
    const TimerDef* def = get_def(inst);
    TIM_Cmd(def->periph, DISABLE);
    TIM_ITConfig(def->periph, TIM_IT_Update, DISABLE);
    timer_callbacks[inst] = 0;
}

void timer_set_freq(TimerInstance inst, uint32_t freq_hz)
{
    const TimerDef* def = get_def(inst);
    uint32_t timer_clk = get_timer_clock(def->apb_bus);
    uint16_t psc, arr;
    calc_prescaler_period(timer_clk, freq_hz, &psc, &arr);

    TIM_PrescalerConfig(def->periph, psc, TIM_PSCReloadMode_Immediate);
    TIM_SetAutoreload(def->periph, arr);
}

/* Weak stub - overridden by rovari_capture.c when linked */
__attribute__((weak)) void rovari_capture_dispatch(TIM_TypeDef* tim) { (void)tim; }

static inline void timer_dispatch(TimerInstance inst)
{
    TIM_TypeDef* periph = timer_defs[inst].periph;
    if (TIM_GetITStatus(periph, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(periph, TIM_IT_Update);
        if (timer_callbacks[inst]) {
            timer_callbacks[inst]();
        }
    }
    rovari_capture_dispatch(periph);
}

void TIM1_UP_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM1_UP_IRQHandler(void) { timer_dispatch(TIMER1); }

void TIM2_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM2_IRQHandler(void) { timer_dispatch(TIMER2); }

void TIM3_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM3_IRQHandler(void) { timer_dispatch(TIMER3); }

void TIM4_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM4_IRQHandler(void) { timer_dispatch(TIMER4); }

/* TIM5 has no IRQ on D6 variant */
