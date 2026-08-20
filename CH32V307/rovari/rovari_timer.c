/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_timer.c
 * @brief Hardware timer periodic-interrupt driver for CH32V307.
 *
 * @sevs-callbacks  Implements a TimerCallback function-pointer dispatch
 *                  table; JPL Rule 9 suppressed per SEVS Section 2.10.
 *
 * TIM1/8 on APB2, TIM2-6 on APB1. Interrupt freq = timer_clk /
 * ((PSC+1)*(ARR+1)). TIM7 is reserved for the system tick (rovari_tick.c).
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_timer.h"

#define TIMER_PSC_MAX  65536U
#define TIMER_ARR_MAX  65535U

/* Instance lookup table */
typedef struct {
    TIM_TypeDef* periph;
    uint32_t     rcc;
    uint8_t      apb_bus;       /* 1 = APB1, 2 = APB2 */
    IRQn_Type    irqn;
} timer_def_t;

static const timer_def_t timer_defs[] = {
    [0] = {0},
    [1] = { TIM1,  RCC_APB2Periph_TIM1,  2, TIM1_UP_IRQn },
    [2] = { TIM2,  RCC_APB1Periph_TIM2,  1, TIM2_IRQn    },
    [3] = { TIM3,  RCC_APB1Periph_TIM3,  1, TIM3_IRQn    },
    [4] = { TIM4,  RCC_APB1Periph_TIM4,  1, TIM4_IRQn    },
    [5] = { TIM5,  RCC_APB1Periph_TIM5,  1, TIM5_IRQn    },
    [6] = { TIM6,  RCC_APB1Periph_TIM6,  1, TIM6_IRQn    },
    [7] = { TIM7,  RCC_APB1Periph_TIM7,  1, TIM7_IRQn    },
    [8] = { TIM8,  RCC_APB2Periph_TIM8,  2, TIM8_UP_IRQn },
};

#define TIMER_DEF_COUNT (sizeof(timer_defs) / sizeof(timer_defs[0]))

static volatile TimerCallback timer_callbacks[9] = {0};

/**
 * @brief Resolve a timer instance to its definition, bounded.
 */
static const timer_def_t* get_def(TimerInstance inst)
{
    if (inst == 0 || inst >= TIMER_DEF_COUNT) {
        return &timer_defs[2];
    }
    return &timer_defs[inst];
}

/**
 * @brief Return the timer input clock, applying the APB 2x rule.
 * @req REQ-ROVARI-TIMER-WORKAROUND-001
 */
static uint32_t get_timer_clock(uint8_t apb_bus)
{
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);

    if (apb_bus == 2) {
        return (clk.PCLK2_Frequency == clk.HCLK_Frequency)
                 ? clk.PCLK2_Frequency
                 : clk.PCLK2_Frequency * 2U;
    }
    return (clk.PCLK1_Frequency == clk.HCLK_Frequency)
             ? clk.PCLK1_Frequency
             : clk.PCLK1_Frequency * 2U;
}

/**
 * @brief Compute prescaler and period for a target frequency.
 * @req REQ-ROVARI-TIMER-0010
 */
static void calc_prescaler_period(uint32_t timer_clk, uint32_t freq_hz,
                                  uint16_t* out_psc, uint16_t* out_arr)
{
    SEVS_REQUIRE_NOT_NULL(out_psc);
    SEVS_REQUIRE_NOT_NULL(out_arr);
    uint32_t total = (freq_hz != 0U) ? (timer_clk / freq_hz) : 1U;
    if (total == 0U) {
        total = 1U;
    }

    for (uint32_t psc = 0U; psc < TIMER_PSC_MAX; psc++) {
        uint32_t arr = total / (psc + 1U) - 1U;
        if (arr <= TIMER_ARR_MAX) {
            *out_psc = (uint16_t)psc;
            *out_arr = (uint16_t)arr;
            return;
        }
    }

    *out_psc = (uint16_t)TIMER_ARR_MAX;
    *out_arr = (uint16_t)TIMER_ARR_MAX;
}

/**
 * @brief Enable a timer's clock and configure its update-interrupt NVIC line.
 */
static void timer_enable_clock_nvic(const timer_def_t* def)
{
    SEVS_REQUIRE_NOT_NULL(def);
    if (def->apb_bus == 2) {
        RCC_APB2PeriphClockCmd(def->rcc, ENABLE);
    } else {
        RCC_APB1PeriphClockCmd(def->rcc, ENABLE);
    }
}

/**
 * @brief Apply a time base and enable the update interrupt and the timer.
 */
static void timer_apply_base(const timer_def_t* def, uint16_t psc, uint16_t arr)
{
    SEVS_REQUIRE_NOT_NULL(def);
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

/* -----------------------------------------------------------------------
 *  Public API
 * ----------------------------------------------------------------------- */

/**
 * @brief Start a timer firing a callback at a given frequency.
 * @param[in] inst     Timer instance (TIMER1-6, TIMER8).
 * @param[in] freq_hz  Interrupt frequency in Hz.
 * @param[in] callback Function invoked from ISR context each period.
 * @req REQ-ROVARI-TIMER-0010
 */
void timer_start(TimerInstance inst, uint32_t freq_hz, TimerCallback callback)
{
    const timer_def_t* def = get_def(inst);
    SEVS_INVARIANT(inst < TIMER_DEF_COUNT);

    timer_callbacks[inst] = callback;
    timer_enable_clock_nvic(def);

    uint32_t timer_clk = get_timer_clock(def->apb_bus);
    uint16_t psc;
    uint16_t arr;
    calc_prescaler_period(timer_clk, freq_hz, &psc, &arr);
    timer_apply_base(def, psc, arr);
}

/**
 * @brief Start a timer firing a callback every period_ms milliseconds.
 * @param[in] inst      Timer instance.
 * @param[in] period_ms Period in milliseconds.
 * @param[in] callback  Function invoked from ISR context each period.
 * @req REQ-ROVARI-TIMER-0011
 */
void timer_start_ms(TimerInstance inst, uint32_t period_ms, TimerCallback callback)
{
    const timer_def_t* def = get_def(inst);
    SEVS_INVARIANT(inst < TIMER_DEF_COUNT);

    timer_callbacks[inst] = callback;
    timer_enable_clock_nvic(def);

    uint32_t timer_clk = get_timer_clock(def->apb_bus);
    uint16_t psc;
    uint16_t arr;

    if (period_ms <= TIMER_ARR_MAX) {
        /* 1 kHz tick: each count = 1 ms */
        psc = (uint16_t)(timer_clk / 1000U - 1U);
        arr = (uint16_t)(period_ms - 1U);
    } else {
        /* 100 Hz tick: each count = 10 ms, max ~655 seconds */
        psc = (uint16_t)(timer_clk / 100U - 1U);
        arr = (uint16_t)(period_ms / 10U - 1U);
    }
    timer_apply_base(def, psc, arr);
}

/**
 * @brief Stop a timer and clear its callback.
 * @param[in] inst Timer instance.
 * @req REQ-ROVARI-TIMER-0012
 */
void timer_stop(TimerInstance inst)
{
    const timer_def_t* def = get_def(inst);
    SEVS_INVARIANT(inst < TIMER_DEF_COUNT);

    TIM_Cmd(def->periph, DISABLE);
    TIM_ITConfig(def->periph, TIM_IT_Update, DISABLE);
    timer_callbacks[inst] = 0;
}

/**
 * @brief Change an active timer's frequency.
 * @param[in] inst    Timer instance.
 * @param[in] freq_hz New interrupt frequency in Hz.
 * @req REQ-ROVARI-TIMER-0013
 */
void timer_set_freq(TimerInstance inst, uint32_t freq_hz)
{
    const timer_def_t* def = get_def(inst);
    uint32_t timer_clk = get_timer_clock(def->apb_bus);
    uint16_t psc;
    uint16_t arr;
    calc_prescaler_period(timer_clk, freq_hz, &psc, &arr);

    TIM_PrescalerConfig(def->periph, psc, TIM_PSCReloadMode_Immediate);
    TIM_SetAutoreload(def->periph, arr);
}

/* -----------------------------------------------------------------------
 *  ISR handlers: dispatch to user callbacks
 * ----------------------------------------------------------------------- */

/**
 * @brief Weak capture-dispatch hook, overridden by rovari_capture.c.
 * @param[in] tim Timer whose IRQ fired.
 */
void __attribute__((weak)) rovari_capture_dispatch(TIM_TypeDef* tim) { (void)tim; }

/**
 * @brief Clear the update flag and dispatch the callback for a timer.
 * @param[in] inst Timer instance whose IRQ fired.
 * @req REQ-ROVARI-TIMER-0014
 */
static inline void timer_dispatch(TimerInstance inst)
{
    SEVS_INVARIANT(inst < TIMER_DEF_COUNT);
    TIM_TypeDef* periph = timer_defs[inst].periph;
    if (TIM_GetITStatus(periph, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(periph, TIM_IT_Update);
        if (timer_callbacks[inst] != NULL) {
            timer_callbacks[inst]();
        }
    }
    rovari_capture_dispatch(periph);
}

/** @brief TIM1 update ISR. @req REQ-ROVARI-TIMER-0014 */
void __attribute__((interrupt("machine"))) TIM1_UP_IRQHandler(void) { timer_dispatch(TIMER1); }
/** @brief TIM2 ISR. @req REQ-ROVARI-TIMER-0014 */
void __attribute__((interrupt("machine"))) TIM2_IRQHandler(void) { timer_dispatch(TIMER2); }
/** @brief TIM3 ISR. @req REQ-ROVARI-TIMER-0014 */
void __attribute__((interrupt("machine"))) TIM3_IRQHandler(void) { timer_dispatch(TIMER3); }
/** @brief TIM4 ISR. @req REQ-ROVARI-TIMER-0014 */
void __attribute__((interrupt("machine"))) TIM4_IRQHandler(void) { timer_dispatch(TIMER4); }
/** @brief TIM5 ISR. @req REQ-ROVARI-TIMER-0014 */
void __attribute__((interrupt("machine"))) TIM5_IRQHandler(void) { timer_dispatch(TIMER5); }
/** @brief TIM6 ISR. @req REQ-ROVARI-TIMER-0014 */
void __attribute__((interrupt("machine"))) TIM6_IRQHandler(void) { timer_dispatch(TIMER6); }
/* TIM7 is reserved for the system tick (rovari_tick.c). */
/** @brief TIM8 update ISR. @req REQ-ROVARI-TIMER-0014 */
void __attribute__((interrupt("machine"))) TIM8_UP_IRQHandler(void) { timer_dispatch(TIMER8); }
