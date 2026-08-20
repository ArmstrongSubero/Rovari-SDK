/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_capture.c
 * @brief Timer input-capture (frequency/pulse) for CH32V307.
 *
 * @sevs-callbacks  Implements a CaptureCallback function-pointer table;
 *                  JPL Rule 9 suppressed per SEVS Section 2.10.
 *
 * Uses PWM Input Mode with slave reset: CH1 captures period (rising),
 * CH2 captures pulse width (falling), counter resets each rising edge so
 * CC1 is the period directly. 1 MHz tick gives microsecond resolution.
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_capture.h"

/* Pin-to-timer-channel mapping */
typedef struct {
    pin_t        pin;
    TIM_TypeDef* timer;
    uint8_t      channel;
    uint8_t      apb_bus;     /* 1 or 2 */
    uint32_t     rcc_tim;
    uint32_t     rcc_gpio;
    GPIO_TypeDef* gpio_port;
    uint16_t     gpio_pin;
} cap_pin_def_t;

static const cap_pin_def_t cap_pins[] = {
    { PA8,  TIM1, 1, 2, RCC_APB2Periph_TIM1, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_8 },
    { PA0,  TIM2, 1, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_0 },
    { PA1,  TIM2, 2, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_1 },
    { PA6,  TIM3, 1, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_6 },
    { PA7,  TIM3, 2, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_7 },
    { PB6,  TIM4, 1, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_6 },
    { PB7,  TIM4, 2, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_7 },
    { PC6,  TIM8, 1, 2, RCC_APB2Periph_TIM8, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_6 },
};

#define CAP_PIN_COUNT (sizeof(cap_pins) / sizeof(cap_pins[0]))

/**
 * @brief Find the capture pin definition, or NULL if not capture-capable.
 */
static const cap_pin_def_t* find_cap_pin(pin_t pin)
{
    for (uint32_t i = 0; i < CAP_PIN_COUNT; i++) {
        if (cap_pins[i].pin == pin) {
            return &cap_pins[i];
        }
    }
    return NULL;
}

/* Per-timer capture state */
typedef struct {
    volatile uint32_t period_us;
    volatile uint32_t pulse_us;
    volatile uint8_t  got_capture;
    CaptureCallback   callback;
} capture_state_t;

static capture_state_t cap_state[9] = {{0}};  /* indexed by timer number 1-8 */

/**
 * @brief Map a timer peripheral to its capture-table index, or 0 if unknown.
 */
static uint8_t timer_num(TIM_TypeDef* tim)
{
    if (tim == TIM1) { return 1; }
    if (tim == TIM2) { return 2; }
    if (tim == TIM3) { return 3; }
    if (tim == TIM4) { return 4; }
    if (tim == TIM8) { return 8; }
    return 0;
}

/**
 * @brief Shared CC interrupt logic: update period/pulse and fire callback.
 * @param[in] tim Timer whose CC interrupt fired.
 * @req REQ-ROVARI-CAPTURE-0015
 */
static void capture_cc_isr(TIM_TypeDef* tim)
{
    uint8_t tn = timer_num(tim);
    if (tn == 0) {
        return;
    }
    SEVS_INVARIANT(tn < 9U);

    capture_state_t* s = &cap_state[tn];

    /* CC1 = period (counter resets on each rising edge in slave mode) */
    if (TIM_GetITStatus(tim, TIM_IT_CC1) != RESET) {
        TIM_ClearITPendingBit(tim, TIM_IT_CC1);
        uint16_t val = TIM_GetCapture1(tim);
        if (val > 0) {
            s->period_us = (uint32_t)val;
            s->got_capture = 1;
            if (s->callback != NULL) {
                s->callback(s->period_us);
            }
        }
    }

    /* CC2 = pulse width (high time) */
    if (TIM_GetITStatus(tim, TIM_IT_CC2) != RESET) {
        TIM_ClearITPendingBit(tim, TIM_IT_CC2);
        s->pulse_us = (uint32_t)TIM_GetCapture2(tim);
    }
}

/* ISR handlers */

/** @brief TIM1 dedicated capture/compare ISR. @req REQ-ROVARI-CAPTURE-WORKAROUND-001 */
void __attribute__((interrupt("machine"))) TIM1_CC_IRQHandler(void) { capture_cc_isr(TIM1); }

/** @brief TIM8 dedicated capture/compare ISR. @req REQ-ROVARI-CAPTURE-WORKAROUND-001 */
void __attribute__((interrupt("machine"))) TIM8_CC_IRQHandler(void) { capture_cc_isr(TIM8); }

/**
 * @brief Capture dispatch hook called by the shared TIM2-4 ISR.
 * @param[in] tim Timer whose IRQ fired.
 * @req REQ-ROVARI-CAPTURE-0015
 */
void rovari_capture_dispatch(TIM_TypeDef* tim)
{
    capture_cc_isr(tim);
}

/* -----------------------------------------------------------------------
 *  Public API
 * ----------------------------------------------------------------------- */

/**
 * @brief Initialize input capture on a pin (no callback).
 * @param[in] pin  Capture-capable pin.
 * @param[in] edge Capture edge polarity.
 * @req REQ-ROVARI-CAPTURE-0010
 */
void capture_init(pin_t pin, CaptureEdge edge)
{
    capture_init_cb(pin, edge, NULL);
}

/**
 * @brief Initialize input capture on a pin with a callback.
 * @param[in] pin      Capture-capable pin; non-capture pins are ignored.
 * @param[in] edge     Capture edge polarity.
 * @param[in] callback Called with the period (us) on each new period.
 * @req REQ-ROVARI-CAPTURE-0010
 * @req REQ-ROVARI-CAPTURE-WORKAROUND-001
 */
void capture_init_cb(pin_t pin, CaptureEdge edge, CaptureCallback callback)
{
    const cap_pin_def_t* def = find_cap_pin(pin);
    if (def == NULL) {
        return;
    }

    uint8_t tn = timer_num(def->timer);
    SEVS_INVARIANT(tn > 0U && tn < 9U);
    capture_state_t* s = &cap_state[tn];
    s->period_us = 0;
    s->pulse_us = 0;
    s->got_capture = 0;
    s->callback = callback;

    /* Enable clocks */
    RCC_APB2PeriphClockCmd(def->rcc_gpio, ENABLE);
    if (def->apb_bus == 2) {
        RCC_APB2PeriphClockCmd(def->rcc_tim, ENABLE);
    } else {
        RCC_APB1PeriphClockCmd(def->rcc_tim, ENABLE);
    }

    /* Configure pin as floating input */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin  = def->gpio_pin;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(def->gpio_port, &gpio);
    GPIO_ResetBits(def->gpio_port, def->gpio_pin);

    /* Calculate prescaler for 1 MHz tick (APB 2x rule). */
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    uint32_t pclk = (def->apb_bus == 2) ? clk.PCLK2_Frequency : clk.PCLK1_Frequency;
    uint32_t timer_clk = (pclk == clk.HCLK_Frequency) ? pclk : pclk * 2U;
    uint16_t psc = (uint16_t)(timer_clk / 1000000U - 1U);

    /* Time base: free-running, max period */
    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler         = psc;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_Period            = 0xFFFF;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(def->timer, &tb);

    /* PWM Input Mode: CH1=period (rising), CH2=pulse (falling) */
    TIM_ICInitTypeDef ic = {0};
    ic.TIM_Channel     = (def->channel == 1) ? TIM_Channel_1 : TIM_Channel_2;
    ic.TIM_ICPolarity  = (edge == CaptureFalling) ? TIM_ICPolarity_Falling
                                                  : TIM_ICPolarity_Rising;
    ic.TIM_ICSelection = TIM_ICSelection_DirectTI;
    ic.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    ic.TIM_ICFilter    = 0x00;
    TIM_PWMIConfig(def->timer, &ic);

    /* Slave mode: reset counter on trigger edge */
    uint16_t trigger = (def->channel == 1) ? TIM_TS_TI1FP1 : TIM_TS_TI2FP2;
    TIM_SelectInputTrigger(def->timer, trigger);
    TIM_SelectSlaveMode(def->timer, TIM_SlaveMode_Reset);
    TIM_SelectMasterSlaveMode(def->timer, TIM_MasterSlaveMode_Enable);

    /* Enable CC1 + CC2 interrupts */
    TIM_ITConfig(def->timer, TIM_IT_CC1 | TIM_IT_CC2, ENABLE);

    /* Clear all pending flags before enabling NVIC (TIM_TimeBaseInit sets UIF). */
    def->timer->INTFR = 0;

    /* Select correct IRQ for this timer */
    IRQn_Type irqn;
    if (def->timer == TIM1) {
        irqn = TIM1_CC_IRQn;
    } else if (def->timer == TIM8) {
        irqn = TIM8_CC_IRQn;
    } else if (def->timer == TIM2) {
        irqn = TIM2_IRQn;
    } else if (def->timer == TIM3) {
        irqn = TIM3_IRQn;
    } else {
        irqn = TIM4_IRQn;
    }

    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel                   = irqn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(def->timer, ENABLE);
}

/**
 * @brief Read the most recent measured period.
 * @param[in] pin Capture-capable pin.
 * @return Period in microseconds, or 0 if none/invalid pin.
 * @req REQ-ROVARI-CAPTURE-0011
 */
uint32_t capture_read_period_us(pin_t pin)
{
    const cap_pin_def_t* def = find_cap_pin(pin);
    if (def == NULL) {
        return 0;
    }
    uint8_t tn = timer_num(def->timer);
    SEVS_INVARIANT(tn > 0U && tn < 9U);
    return cap_state[tn].period_us;
}

/**
 * @brief Read the most recent measured high-pulse width.
 * @param[in] pin Capture-capable pin.
 * @return Pulse width in microseconds, or 0 if none/invalid pin.
 * @req REQ-ROVARI-CAPTURE-0012
 */
uint32_t capture_read_pulse_us(pin_t pin)
{
    const cap_pin_def_t* def = find_cap_pin(pin);
    if (def == NULL) {
        return 0;
    }
    uint8_t tn = timer_num(def->timer);
    SEVS_INVARIANT(tn > 0U && tn < 9U);
    return cap_state[tn].pulse_us;
}

/**
 * @brief Read the measured frequency derived from the period.
 * @param[in] pin Capture-capable pin.
 * @return Frequency in Hz, or 0 if no period measured.
 * @req REQ-ROVARI-CAPTURE-0013
 */
uint32_t capture_read_freq(pin_t pin)
{
    const cap_pin_def_t* def = find_cap_pin(pin);
    if (def == NULL) {
        return 0;
    }
    uint8_t tn = timer_num(def->timer);
    SEVS_INVARIANT(tn > 0U && tn < 9U);
    uint32_t period = cap_state[tn].period_us;
    if (period == 0) {
        return 0;
    }
    return 1000000U / period;
}

/**
 * @brief Stop capture on a pin and clear its callback.
 * @param[in] pin Capture-capable pin.
 * @req REQ-ROVARI-CAPTURE-0014
 */
void capture_stop(pin_t pin)
{
    const cap_pin_def_t* def = find_cap_pin(pin);
    if (def == NULL) {
        return;
    }
    TIM_ITConfig(def->timer, TIM_IT_CC1 | TIM_IT_CC2, DISABLE);
    cap_state[timer_num(def->timer)].callback = 0;
}
