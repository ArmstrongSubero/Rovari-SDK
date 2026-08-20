/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_capture.c — Timer input capture implementation
 *
 * Uses WCH-recommended PWM Input Mode with Slave Reset:
 *   - TIM_PWMIConfig() sets up CH1 (rising=period) + CH2 (falling=pulse)
 *   - Slave mode resets counter on each rising edge
 *   - CC1 capture value = period directly (no subtraction needed)
 *   - CC2 capture value = pulse width (high time)
 *
 * Supported pins (CH1 recommended for best results):
 *   TIM1: PA8 (CH1)   — APB2, uses TIM1_CC_IRQn
 *   TIM2: PA0 (CH1)   — APB1, uses TIM2_IRQn
 *   TIM3: PA6 (CH1)   — APB1, uses TIM3_IRQn
 *   TIM4: PB6 (CH1)   — APB1, uses TIM4_IRQn
  */

#include "rovari_capture.h"
#include "debug.h"

/* ── Pin-to-timer-channel mapping ───────────────────────────────────── */
typedef struct {
    pin_t        pin;
    TIM_TypeDef* timer;
    uint8_t      channel;
    uint8_t      apb_bus;     /* 1 or 2 */
    uint32_t     rcc_tim;
    uint32_t     rcc_gpio;
    GPIO_TypeDef* gpio_port;
    uint16_t     gpio_pin;
} CapPinDef;

static const CapPinDef cap_pins[] = {
    /* TIM1: APB2 */
    { PA8,  TIM1, 1, 2, RCC_APB2Periph_TIM1, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_8 },

    /* TIM2: APB1 */
    { PA0,  TIM2, 1, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_0 },
    { PA1,  TIM2, 2, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_1 },

    /* TIM3: APB1 */
    { PA6,  TIM3, 1, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_6 },
    { PA7,  TIM3, 2, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_7 },

    /* TIM4: APB1 */
    { PB6,  TIM4, 1, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_6 },
    { PB7,  TIM4, 2, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_7 },

};

#define CAP_PIN_COUNT (sizeof(cap_pins) / sizeof(cap_pins[0]))

static const CapPinDef* find_cap_pin(pin_t pin)
{
    for (uint32_t i = 0; i < CAP_PIN_COUNT; i++) {
        if (cap_pins[i].pin == pin) return &cap_pins[i];
    }
    return 0;
}

/* ── Per-timer capture state ────────────────────────────────────────── */
typedef struct {
    volatile uint32_t period_us;
    volatile uint32_t pulse_us;
    volatile uint8_t  got_capture;
    CaptureCallback   callback;
} CaptureState;

/* Indexed by timer number: [1]=TIM1 .. [4]=TIM4 */
static CaptureState cap_state[5] = {{0}};

static uint8_t timer_num(TIM_TypeDef* tim)
{
    if (tim == TIM1) return 1;
    if (tim == TIM2) return 2;
    if (tim == TIM3) return 3;
    if (tim == TIM4) return 4;
    return 0;
}

/* ── CC ISR dispatch — shared logic ─────────────────────────────────── */
static void capture_cc_isr(TIM_TypeDef* tim)
{
    uint8_t tn = timer_num(tim);
    if (tn == 0) return;

    CaptureState* s = &cap_state[tn];

    /* CC1 = period (counter resets on each rising edge in slave mode) */
    if (TIM_GetITStatus(tim, TIM_IT_CC1) != RESET) {
        TIM_ClearITPendingBit(tim, TIM_IT_CC1);
        uint16_t val = TIM_GetCapture1(tim);
        if (val > 0) {
            s->period_us = (uint32_t)val;
            s->got_capture = 1;
            if (s->callback) s->callback(s->period_us);
        }
    }

    /* CC2 = pulse width (high time) */
    if (TIM_GetITStatus(tim, TIM_IT_CC2) != RESET) {
        TIM_ClearITPendingBit(tim, TIM_IT_CC2);
        s->pulse_us = (uint32_t)TIM_GetCapture2(tim);
    }
}

/* ── ISR handlers ───────────────────────────────────────────────────
 *
 * TIM1 has a DEDICATED Capture/Compare IRQs separate from
 * their Update IRQs. These must be defined here.
 *
 * TIM2/3/4 share a single IRQ for both update and CC events.
 * Their ISR is defined in rovari_timer.c and calls
 * rovari_capture_dispatch() (weak override) for CC events.
 * ──────────────────────────────────────────────────────────────────── */

/* TIM1 Capture Compare — dedicated IRQ */
void TIM1_CC_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM1_CC_IRQHandler(void)
{
    capture_cc_isr(TIM1);
}

/* No TIM8 on CH32V203 */

/* TIM2/3/4: called from rovari_timer.c ISR via weak override */
void rovari_capture_dispatch(TIM_TypeDef* tim)
{
    capture_cc_isr(tim);
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

void capture_init(pin_t pin, CaptureEdge edge)
{
    capture_init_cb(pin, edge, 0);
}

void capture_init_cb(pin_t pin, CaptureEdge edge, CaptureCallback callback)
{
    const CapPinDef* def = find_cap_pin(pin);
    if (!def) return;

    uint8_t tn = timer_num(def->timer);
    CaptureState* s = &cap_state[tn];
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

    /* Calculate prescaler for 1 MHz tick */
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    uint32_t pclk = (def->apb_bus == 2) ? clk.PCLK2_Frequency
                                        : clk.PCLK1_Frequency;
    uint32_t timer_clk = (pclk == clk.HCLK_Frequency) ? pclk : pclk * 2;
    uint16_t psc = (uint16_t)(timer_clk / 1000000 - 1);

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

    /* Clear all pending flags before enabling NVIC */
    def->timer->INTFR = 0;

    /* Select correct IRQ for this timer */
    IRQn_Type irqn;
    if (def->timer == TIM1) {
        irqn = TIM1_CC_IRQn;
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

    /* Start timer */
    TIM_Cmd(def->timer, ENABLE);
}

uint32_t capture_read_period_us(pin_t pin)
{
    const CapPinDef* def = find_cap_pin(pin);
    if (!def) return 0;
    return cap_state[timer_num(def->timer)].period_us;
}

uint32_t capture_read_pulse_us(pin_t pin)
{
    const CapPinDef* def = find_cap_pin(pin);
    if (!def) return 0;
    return cap_state[timer_num(def->timer)].pulse_us;
}

uint32_t capture_read_freq(pin_t pin)
{
    const CapPinDef* def = find_cap_pin(pin);
    if (!def) return 0;
    uint32_t period = cap_state[timer_num(def->timer)].period_us;
    if (period == 0) return 0;
    return 1000000 / period;
}

void capture_stop(pin_t pin)
{
    const CapPinDef* def = find_cap_pin(pin);
    if (!def) return;

    TIM_ITConfig(def->timer, TIM_IT_CC1 | TIM_IT_CC2, DISABLE);
    cap_state[timer_num(def->timer)].callback = 0;
}
