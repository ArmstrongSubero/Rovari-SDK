/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_timer.c - Hardware timer interrupt implementation (CH32H417)
 *
 * Timer architecture (CH32H417):
 *   - TIM1, TIM8, TIM9, TIM10, TIM11, TIM12 are on HB2 (APB2-equivalent)
 *   - TIM2, TIM3, TIM4, TIM5, TIM6, TIM7 are on HB1 (APB1-equivalent)
 *   - All timers run at HCLK (150 MHz from default PLL config)
 *   - No APB prescaler doubling (unlike STM32/CH32V307)
 *   - Interrupt frequency = HCLK / ((PSC+1) x (ARR+1))
 *
 * ISR handlers:
 *   TIM1 uses TIM1_UP_IRQHandler (dedicated update interrupt)
 *   TIM2-6 use TIMx_IRQHandler (global interrupt)
 *   TIM8 uses TIM8_UP_IRQHandler (dedicated update interrupt)
 *   TIM9-12 use TIMx_IRQHandler (global interrupt)
 *   TIM7 is reserved for system tick (millis/micros)
 */

#include "rovari_timer.h"
#include "debug.h"

/* -- Instance lookup table ------------------------------------------------ */
typedef struct {
    TIM_TypeDef* periph;
    uint32_t     rcc;
    uint8_t      bus;         /* 1 = HB1, 2 = HB2 */
    IRQn_Type    irqn;
} TimerDef;

static const TimerDef timer_defs[] = {
    [ 0] = {0},
    [ 1] = { TIM1,  RCC_HB2Periph_TIM1,   2, TIM1_UP_IRQn },
    [ 2] = { TIM2,  RCC_HB1Periph_TIM2,   1, TIM2_IRQn    },
    [ 3] = { TIM3,  RCC_HB1Periph_TIM3,   1, TIM3_IRQn    },
    [ 4] = { TIM4,  RCC_HB1Periph_TIM4,   1, TIM4_IRQn    },
    [ 5] = { TIM5,  RCC_HB1Periph_TIM5,   1, TIM5_IRQn    },
    [ 6] = { TIM6,  RCC_HB1Periph_TIM6,   1, TIM6_IRQn    },
    [ 7] = { TIM7,  RCC_HB1Periph_TIM7,   1, TIM7_IRQn    },
    [ 8] = { TIM8,  RCC_HB2Periph_TIM8,   2, TIM8_UP_IRQn },
    [ 9] = { TIM9,  RCC_HB2Periph_TIM9,   2, TIM9_IRQn    },
    [10] = { TIM10, RCC_HB2Periph_TIM10,  2, TIM10_IRQn   },
    [11] = { TIM11, RCC_HB2Periph_TIM11,  2, TIM11_IRQn   },
    [12] = { TIM12, RCC_HB2Periph_TIM12,  2, TIM12_IRQn   },
};

#define TIMER_DEF_COUNT (sizeof(timer_defs) / sizeof(timer_defs[0]))

static volatile TimerCallback timer_callbacks[13] = {0};

static inline const TimerDef* get_def(TimerInstance inst)
{
    if (inst == 0 || inst >= TIMER_DEF_COUNT) return &timer_defs[2];
    return &timer_defs[inst];
}

static uint32_t get_timer_clock(void)
{
    /* CH32H417: all timers run at HCLK.
     * No APB prescaler doubling like STM32/CH32V307. */
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    return clk.HCLK_Frequency;
}

/* -- Prescaler/period calculation ----------------------------------------- */
static void calc_prescaler_period(uint32_t timer_clk, uint32_t freq_hz,
                                  uint16_t* out_psc, uint16_t* out_arr)
{
    /* We need: (PSC+1) x (ARR+1) = timer_clk / freq_hz
     * Try increasing prescaler until ARR fits in 16 bits. */
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

    /* Fallback: max prescaler, max period */
    *out_psc = 65535;
    *out_arr = 65535;
}

/* =========================================================================
 *  Public API
 * ========================================================================= */

void timer_start(TimerInstance inst, uint32_t freq_hz, TimerCallback callback)
{
    const TimerDef* def = get_def(inst);

    timer_callbacks[inst] = callback;

    /* Enable timer clock */
    if (def->bus == 2) {
        RCC_HB2PeriphClockCmd(def->rcc, ENABLE);
    } else {
        RCC_HB1PeriphClockCmd(def->rcc, ENABLE);
    }

    /* Calculate prescaler and period */
    uint32_t timer_clk = get_timer_clock();
    uint16_t psc, arr;
    calc_prescaler_period(timer_clk, freq_hz, &psc, &arr);

    /* Configure time base */
    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler         = psc;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_Period            = arr;
    tb.TIM_ClockDivision     = 0;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(def->periph, &tb);

    TIM_ClearITPendingBit(def->periph, TIM_IT_Update);
    TIM_ITConfig(def->periph, TIM_IT_Update, ENABLE);

    NVIC_SetPriority(def->irqn, 1);
    NVIC_EnableIRQ(def->irqn);

    TIM_Cmd(def->periph, ENABLE);
}

void timer_start_ms(TimerInstance inst, uint32_t period_ms, TimerCallback callback)
{
    const TimerDef* def = get_def(inst);

    timer_callbacks[inst] = callback;

    if (def->bus == 2) {
        RCC_HB2PeriphClockCmd(def->rcc, ENABLE);
    } else {
        RCC_HB1PeriphClockCmd(def->rcc, ENABLE);
    }

    /* At 150 MHz, a 1 kHz prescaler (150000-1) overflows 16 bits.
     * Use 10 kHz tick: PSC = timer_clk/10000 - 1 = 14999, fits fine.
     * ARR = period_ms * 10 - 1. Max period: 65536/10 = 6553 ms.
     * For longer periods, fall back to the general prescaler solver.
     */
    uint32_t timer_clk = get_timer_clock();
    uint16_t psc, arr;

    if (period_ms <= 6553) {
        /* 10 kHz tick: each count = 0.1 ms */
        psc = (uint16_t)(timer_clk / 10000 - 1);
        arr = (uint16_t)(period_ms * 10 - 1);
    } else {
        /* Long periods: convert to Hz, use general solver */
        uint32_t freq_hz = 1000 / period_ms;
        if (freq_hz == 0) freq_hz = 1;
        calc_prescaler_period(timer_clk, freq_hz, &psc, &arr);
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

    NVIC_SetPriority(def->irqn, 1);
    NVIC_EnableIRQ(def->irqn);

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
    uint32_t timer_clk = get_timer_clock();
    uint16_t psc, arr;
    calc_prescaler_period(timer_clk, freq_hz, &psc, &arr);

    TIM_PrescalerConfig(def->periph, psc, TIM_PSCReloadMode_Immediate);
    TIM_SetAutoreload(def->periph, arr);
}

/* =========================================================================
 *  ISR handlers - dispatch to user callbacks
 *  Using interrupt("machine") for upstream GCC compatibility.
 *
 *  Each ISR also calls rovari_capture_dispatch() (weak) so that
 *  input capture and periodic interrupts can share the same timer IRQ.
 * ========================================================================= */

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

void TIM5_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM5_IRQHandler(void) { timer_dispatch(TIMER5); }

void TIM6_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM6_IRQHandler(void) { timer_dispatch(TIMER6); }

/* TIM7 is reserved for system tick (millis/micros) - see rovari_tick.c */

void TIM8_UP_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM8_UP_IRQHandler(void) { timer_dispatch(TIMER8); }

void TIM9_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM9_IRQHandler(void) { timer_dispatch(TIMER9); }

void TIM10_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM10_IRQHandler(void) { timer_dispatch(TIMER10); }

void TIM11_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM11_IRQHandler(void) { timer_dispatch(TIMER11); }

void TIM12_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM12_IRQHandler(void) { timer_dispatch(TIMER12); }
