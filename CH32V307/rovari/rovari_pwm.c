/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_pwm.c
 * @brief PWM output for CH32V307 on TIM1-4/8 channels.
 *
 * Fixed pin-to-timer-channel map (no AFIO remap). Frequency configuration
 * accounts for the APB 2x timer-clock rule. Integer interfaces only.
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_pwm.h"

#define PWM_DUTY_MAX_8BIT  255U
#define PWM_PCT_MAX        100U
#define PWM_PSC_MAX        65536U
#define PWM_ARR_MAX        65535U

/* Pin-to-timer-channel mapping */
typedef struct {
    pin_t        pin;
    TIM_TypeDef* timer;
    uint8_t      channel;   /* 1-4 */
    uint8_t      apb_bus;   /* 1 or 2 */
    uint32_t     rcc_tim;
    uint32_t     rcc_gpio;
    GPIO_TypeDef* gpio_port;
    uint16_t     gpio_pin;
} pwm_pin_def_t;

static const pwm_pin_def_t pwm_pins[] = {
    { PA0, TIM2, 1, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_0 },
    { PA1, TIM2, 2, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_1 },
    { PA2, TIM2, 3, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_2 },
    { PA3, TIM2, 4, 1, RCC_APB1Periph_TIM2, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_3 },
    { PA6, TIM3, 1, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_6 },
    { PA7, TIM3, 2, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_7 },
    { PB0, TIM3, 3, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_0 },
    { PB1, TIM3, 4, 1, RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_1 },
    { PB6, TIM4, 1, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_6 },
    { PB7, TIM4, 2, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_7 },
    { PB8, TIM4, 3, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_8 },
    { PB9, TIM4, 4, 1, RCC_APB1Periph_TIM4, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_9 },
    { PA8,  TIM1, 1, 2, RCC_APB2Periph_TIM1, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_8 },
    { PA9,  TIM1, 2, 2, RCC_APB2Periph_TIM1, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_9 },
    { PA10, TIM1, 3, 2, RCC_APB2Periph_TIM1, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_10 },
    { PA11, TIM1, 4, 2, RCC_APB2Periph_TIM1, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_11 },
    { PC6, TIM8, 1, 2, RCC_APB2Periph_TIM8, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_6 },
    { PC7, TIM8, 2, 2, RCC_APB2Periph_TIM8, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_7 },
    { PC8, TIM8, 3, 2, RCC_APB2Periph_TIM8, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_8 },
    { PC9, TIM8, 4, 2, RCC_APB2Periph_TIM8, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_9 },
};

#define PWM_PIN_COUNT (sizeof(pwm_pins) / sizeof(pwm_pins[0]))

/* Store the period (ARR) per timer so duty functions scale correctly. */
static uint16_t s_timer_period[9] = {0};  /* indexed by timer number 1-8 */

/**
 * @brief Find the PWM pin definition for a pin, or NULL if not PWM-capable.
 */
static const pwm_pin_def_t* find_pin(pin_t pin)
{
    for (uint32_t i = 0; i < PWM_PIN_COUNT; i++) {
        if (pwm_pins[i].pin == pin) {
            return &pwm_pins[i];
        }
    }
    return NULL;
}

/**
 * @brief Map a timer peripheral to its number 1-8, or 0 if unknown.
 */
static uint8_t timer_number(TIM_TypeDef* tim)
{
    if (tim == TIM1) { return 1; }
    if (tim == TIM2) { return 2; }
    if (tim == TIM3) { return 3; }
    if (tim == TIM4) { return 4; }
    if (tim == TIM5) { return 5; }
    if (tim == TIM6) { return 6; }
    if (tim == TIM7) { return 7; }
    if (tim == TIM8) { return 8; }
    return 0;
}

/**
 * @brief Compute prescaler and period for a target frequency.
 * @req REQ-ROVARI-PWM-0010
 */
static void calc_psc_arr(uint32_t timer_clk, uint32_t freq_hz,
                         uint16_t* out_psc, uint16_t* out_arr)
{
    SEVS_REQUIRE_NOT_NULL(out_psc);
    SEVS_REQUIRE_NOT_NULL(out_arr);
    uint32_t total = (freq_hz != 0U) ? (timer_clk / freq_hz) : 1U;
    if (total == 0U) {
        total = 1U;
    }

    for (uint32_t psc = 0U; psc < PWM_PSC_MAX; psc++) {
        uint32_t arr = total / (psc + 1U) - 1U;
        if (arr <= PWM_ARR_MAX) {
            *out_psc = (uint16_t)psc;
            *out_arr = (uint16_t)arr;
            return;
        }
    }
    *out_psc = (uint16_t)PWM_ARR_MAX;
    *out_arr = (uint16_t)PWM_ARR_MAX;
}

/**
 * @brief Set the compare register for a timer channel.
 */
static void set_compare(TIM_TypeDef* tim, uint8_t ch, uint16_t val)
{
    SEVS_INVARIANT(ch >= 1U && ch <= 4U);
    switch (ch) {
        case 1: TIM_SetCompare1(tim, val); break;
        case 2: TIM_SetCompare2(tim, val); break;
        case 3: TIM_SetCompare3(tim, val); break;
        case 4: TIM_SetCompare4(tim, val); break;
        default: break;
    }
}

/**
 * @brief Configure output compare for PWM mode on a channel.
 */
static void init_oc(TIM_TypeDef* tim, uint8_t ch)
{
    SEVS_INVARIANT(ch >= 1U && ch <= 4U);
    TIM_OCInitTypeDef oc = {0};
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;
    oc.TIM_Pulse       = 0;

    switch (ch) {
        case 1: TIM_OC1Init(tim, &oc); TIM_OC1PreloadConfig(tim, TIM_OCPreload_Enable); break;
        case 2: TIM_OC2Init(tim, &oc); TIM_OC2PreloadConfig(tim, TIM_OCPreload_Enable); break;
        case 3: TIM_OC3Init(tim, &oc); TIM_OC3PreloadConfig(tim, TIM_OCPreload_Enable); break;
        case 4: TIM_OC4Init(tim, &oc); TIM_OC4PreloadConfig(tim, TIM_OCPreload_Enable); break;
        default: break;
    }
}

/**
 * @brief Return the timer input clock, applying the APB 2x rule.
 * @req REQ-ROVARI-PWM-WORKAROUND-001
 */
static uint32_t pwm_timer_clock(uint8_t apb_bus)
{
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    uint32_t pclk = (apb_bus == 2) ? clk.PCLK2_Frequency : clk.PCLK1_Frequency;
    return (pclk == clk.HCLK_Frequency) ? pclk : pclk * 2U;
}

/* -----------------------------------------------------------------------
 *  Public API
 * ----------------------------------------------------------------------- */

/**
 * @brief Initialize PWM output on a pin at a given frequency.
 * @param[in] pin     PWM-capable pin; non-PWM pins are ignored.
 * @param[in] freq_hz PWM frequency in Hz.
 * @req REQ-ROVARI-PWM-0010
 * @req REQ-ROVARI-PWM-0020
 */
void pwm_init(pin_t pin, uint32_t freq_hz)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return;
    }
    SEVS_INVARIANT(def->channel >= 1U && def->channel <= 4U);

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

    uint32_t timer_clk = pwm_timer_clock(def->apb_bus);
    uint16_t psc;
    uint16_t arr;
    calc_psc_arr(timer_clk, freq_hz, &psc, &arr);

    s_timer_period[timer_number(def->timer)] = arr;

    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler         = psc;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_Period            = arr;
    tb.TIM_ClockDivision     = 0;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(def->timer, &tb);

    init_oc(def->timer, def->channel);
    TIM_ARRPreloadConfig(def->timer, ENABLE);
    TIM_Cmd(def->timer, ENABLE);

    /* TIM1/TIM8 advanced timers need the main output enabled. */
    if (def->timer == TIM1 || def->timer == TIM8) {
        TIM_CtrlPWMOutputs(def->timer, ENABLE);
    }
}

/**
 * @brief Set duty from an 8-bit value (0-255).
 * @param[in] pin  PWM-capable pin.
 * @param[in] duty 0 (0%) to 255 (100%).
 * @req REQ-ROVARI-PWM-0011
 */
void pwm_write(pin_t pin, uint8_t duty)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return;
    }
    uint16_t arr = s_timer_period[timer_number(def->timer)];
    uint16_t compare = (uint16_t)(((uint32_t)duty * arr) / PWM_DUTY_MAX_8BIT);
    set_compare(def->timer, def->channel, compare);
}

/**
 * @brief Set duty from an integer percentage (0-100).
 * @param[in] pin     PWM-capable pin.
 * @param[in] percent 0-100; values above 100 are clamped.
 * @req REQ-ROVARI-PWM-0012
 */
void pwm_write_pct(pin_t pin, uint8_t percent)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return;
    }
    if (percent > PWM_PCT_MAX) {
        percent = (uint8_t)PWM_PCT_MAX;
    }
    uint16_t arr = s_timer_period[timer_number(def->timer)];
    uint16_t compare = (uint16_t)(((uint32_t)percent * arr) / PWM_PCT_MAX);
    set_compare(def->timer, def->channel, compare);
}

/**
 * @brief Set the pulse width in microseconds, clamped to the period.
 * @param[in] pin      PWM-capable pin.
 * @param[in] pulse_us Pulse width in microseconds.
 * @req REQ-ROVARI-PWM-0013
 */
void pwm_write_us(pin_t pin, uint32_t pulse_us)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return;
    }

    uint32_t timer_clk = pwm_timer_clock(def->apb_bus);
    uint8_t tn = timer_number(def->timer);
    uint16_t arr = s_timer_period[tn];

    uint16_t psc = def->timer->PSC;
    uint32_t tick_rate = timer_clk / (psc + 1U);

    uint32_t ticks = pulse_us * (tick_rate / 1000000U);
    if (ticks > arr) {
        ticks = arr;
    }

    set_compare(def->timer, def->channel, (uint16_t)ticks);
}

/**
 * @brief Set the channel compare register directly.
 * @param[in] pin     PWM-capable pin.
 * @param[in] compare Raw compare value (0..period).
 * @req REQ-ROVARI-PWM-0014
 */
void pwm_write_raw(pin_t pin, uint16_t compare)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return;
    }
    set_compare(def->timer, def->channel, compare);
}

/**
 * @brief Stop PWM and return the pin to GPIO output low.
 * @param[in] pin PWM-capable pin.
 * @req REQ-ROVARI-PWM-0015
 */
void pwm_stop(pin_t pin)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return;
    }

    set_compare(def->timer, def->channel, 0);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = def->gpio_pin;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(def->gpio_port, &gpio);
    GPIO_ResetBits(def->gpio_port, def->gpio_pin);
}
