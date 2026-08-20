/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_pwm.c — PWM output implementation
 *
 * Pin-to-timer mapping for CH32V203 (TIM1-4, no TIM8).
 * Each entry maps a Rovari pin_t to a timer peripheral + channel.
 */

#include "rovari_pwm.h"
#include "debug.h"

/* ── Pin-to-timer-channel mapping ───────────────────────────────────── */
typedef struct {
    pin_t        pin;
    TIM_TypeDef* timer;
    uint8_t      channel;   /* 1–4 */
    uint8_t      apb_bus;   /* 1 or 2 */
    uint32_t     rcc_tim;   /* RCC periph clock for the timer */
    uint32_t     rcc_gpio;  /* RCC periph clock for the GPIO port */
    GPIO_TypeDef* gpio_port;
    uint16_t     gpio_pin;  /* GPIO_Pin_x */
} PwmPinDef;

/* Default pin mapping (no AFIO remap) */
static const PwmPinDef pwm_pins[] = {
    /* TIM2: APB1 (72 MHz) */
    { PA0, TIM2, 1, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_0 },
    { PA1, TIM2, 2, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_1 },
    { PA2, TIM2, 3, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_2 },
    { PA3, TIM2, 4, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_3 },

    /* TIM3: APB1 (72 MHz) */
    { PA6, TIM3, 1, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_6 },
    { PA7, TIM3, 2, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_7 },
    { PB0, TIM3, 3, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_0 },
    { PB1, TIM3, 4, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_1 },

    /* TIM4: APB1 (72 MHz) */
    { PB6, TIM4, 1, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_6 },
    { PB7, TIM4, 2, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_7 },
    { PB8, TIM4, 3, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_8 },
    { PB9, TIM4, 4, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_9 },

    /* TIM1: APB2 (144 MHz) — advanced timer */
    { PA8,  TIM1, 1, 2, RCC_APB2Periph_TIM1, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_8 },
    { PA9,  TIM1, 2, 2, RCC_APB2Periph_TIM1, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_9 },
    { PA10, TIM1, 3, 2, RCC_APB2Periph_TIM1, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_10 },
    { PA11, TIM1, 4, 2, RCC_APB2Periph_TIM1, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_11 },

};

#define PWM_PIN_COUNT (sizeof(pwm_pins) / sizeof(pwm_pins[0]))

static const PwmPinDef* find_pin(pin_t pin)
{
    for (uint32_t i = 0; i < PWM_PIN_COUNT; i++) {
        if (pwm_pins[i].pin == pin) return &pwm_pins[i];
    }
    return 0;  /* Not a PWM-capable pin */
}

/* Store the period (ARR) for each timer so duty functions can scale correctly */
static uint16_t timer_period[5] = {0};  /* indexed by timer number 1-4 */

static uint8_t timer_number(TIM_TypeDef* tim)
{
    if (tim == TIM1) return 1;
    if (tim == TIM2) return 2;
    if (tim == TIM3) return 3;
    if (tim == TIM4) return 4;
    return 0;
}

/* ── Prescaler/period calculation (same as rovari_timer.c) ──────────── */
static void calc_psc_arr(uint32_t timer_clk, uint32_t freq_hz,
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

/* ── Set compare value for a specific channel ───────────────────────── */
static void set_compare(TIM_TypeDef* tim, uint8_t ch, uint16_t val)
{
    switch (ch) {
        case 1: TIM_SetCompare1(tim, val); break;
        case 2: TIM_SetCompare2(tim, val); break;
        case 3: TIM_SetCompare3(tim, val); break;
        case 4: TIM_SetCompare4(tim, val); break;
    }
}

/* ── Configure OC for a specific channel ────────────────────────────── */
static void init_oc(TIM_TypeDef* tim, uint8_t ch)
{
    TIM_OCInitTypeDef oc = {0};
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState  = TIM_OutputState_Enable;
    oc.TIM_OCPolarity   = TIM_OCPolarity_High;
    oc.TIM_Pulse        = 0;  /* Start at 0% duty */

    switch (ch) {
        case 1: TIM_OC1Init(tim, &oc); TIM_OC1PreloadConfig(tim, TIM_OCPreload_Enable); break;
        case 2: TIM_OC2Init(tim, &oc); TIM_OC2PreloadConfig(tim, TIM_OCPreload_Enable); break;
        case 3: TIM_OC3Init(tim, &oc); TIM_OC3PreloadConfig(tim, TIM_OCPreload_Enable); break;
        case 4: TIM_OC4Init(tim, &oc); TIM_OC4PreloadConfig(tim, TIM_OCPreload_Enable); break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

void pwm_init(pin_t pin, uint32_t freq_hz)
{
    const PwmPinDef* def = find_pin(pin);
    if (!def) return;

    /* Enable GPIO and timer clocks */
    RCC_APB2PeriphClockCmd(def->rcc_gpio, ENABLE);
    if (def->apb_bus == 2) {
        RCC_APB2PeriphClockCmd(def->rcc_tim, ENABLE);
    } else {
        RCC_APB1PeriphClockCmd(def->rcc_tim, ENABLE);
    }

    /* Configure pin as AF push-pull */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = def->gpio_pin;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(def->gpio_port, &gpio);

    /* Calculate prescaler and period.
     * If APB prescaler != 1, timer clock = 2 × PCLK (STM32/WCH rule). */
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    uint32_t pclk = (def->apb_bus == 2) ? clk.PCLK2_Frequency : clk.PCLK1_Frequency;
    uint32_t timer_clk = (pclk == clk.HCLK_Frequency) ? pclk : pclk * 2;

    uint16_t psc, arr;
    calc_psc_arr(timer_clk, freq_hz, &psc, &arr);

    /* Store period for duty cycle scaling */
    timer_period[timer_number(def->timer)] = arr;

    /* Configure time base */
    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler         = psc;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_Period            = arr;
    tb.TIM_ClockDivision     = 0;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(def->timer, &tb);

    /* Configure output compare for PWM */
    init_oc(def->timer, def->channel);

    /* Enable auto-reload preload */
    TIM_ARRPreloadConfig(def->timer, ENABLE);

    /* Start timer */
    TIM_Cmd(def->timer, ENABLE);

    /* For TIM1 (advanced timer), must enable main output */
    if (def->timer == TIM1) {
        TIM_CtrlPWMOutputs(def->timer, ENABLE);
    }
}

void pwm_write(pin_t pin, uint8_t duty)
{
    const PwmPinDef* def = find_pin(pin);
    if (!def) return;

    uint16_t arr = timer_period[timer_number(def->timer)];
    uint16_t compare = (uint16_t)((uint32_t)duty * arr / 255);
    set_compare(def->timer, def->channel, compare);
}

void pwm_write_pct(pin_t pin, float percent)
{
    const PwmPinDef* def = find_pin(pin);
    if (!def) return;

    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    uint16_t arr = timer_period[timer_number(def->timer)];
    uint16_t compare = (uint16_t)(percent * (float)arr / 100.0f);
    set_compare(def->timer, def->channel, compare);
}

void pwm_write_us(pin_t pin, uint32_t pulse_us)
{
    const PwmPinDef* def = find_pin(pin);
    if (!def) return;

    /* Timer ticks per microsecond.
     * If APB prescaler != 1, timer clock = 2 × PCLK. */
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    uint32_t pclk = (def->apb_bus == 2) ? clk.PCLK2_Frequency : clk.PCLK1_Frequency;
    uint32_t timer_clk = (pclk == clk.HCLK_Frequency) ? pclk : pclk * 2;

    uint8_t tn = timer_number(def->timer);
    uint16_t arr = timer_period[tn];

    /* Reconstruct the actual tick rate from stored prescaler */
    uint16_t psc = def->timer->PSC;
    uint32_t tick_rate = timer_clk / (psc + 1);

    /* Convert microseconds to timer ticks */
    uint32_t ticks = (pulse_us * (tick_rate / 1000000));
    if (ticks > arr) ticks = arr;

    set_compare(def->timer, def->channel, (uint16_t)ticks);
}

void pwm_write_raw(pin_t pin, uint16_t compare)
{
    const PwmPinDef* def = find_pin(pin);
    if (!def) return;

    set_compare(def->timer, def->channel, compare);
}

void pwm_stop(pin_t pin)
{
    const PwmPinDef* def = find_pin(pin);
    if (!def) return;

    set_compare(def->timer, def->channel, 0);

    /* Reconfigure pin as regular GPIO output low */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = def->gpio_pin;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(def->gpio_port, &gpio);
    GPIO_ResetBits(def->gpio_port, def->gpio_pin);
}
