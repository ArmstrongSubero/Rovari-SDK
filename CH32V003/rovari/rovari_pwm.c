/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */
/**
 * @file rovari_pwm.c
 * @brief PWM output for CH32V003 on TIM1 channels.
 * TIM1 is on APB2 (48 MHz). TIM2 is reserved for millis()/micros().
 * Default channel pins: CH1=PD2, CH4=PC4
 */
#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_pwm.h"

#define PWM_DUTY_MAX_8BIT  255U
#define PWM_PCT_MAX        100U
#define PWM_ARR_MAX        65535U

typedef struct {
    pin_t        pin;
    uint8_t      channel;
    uint32_t     rcc_gpio;
    GPIO_TypeDef* gpio_port;
    uint16_t     gpio_pin;
} pwm_pin_def_t;

/* TIM1 default mapping (no remap), confirmed from WCH EVT examples.
 * Only CH1 and CH4 are available in the default config.
 * CH2/CH3 require GPIO_PartialRemap or GPIO_FullRemap_TIM1. */
static const pwm_pin_def_t pwm_pins[] = {
    { PD2, 1, RCC_APB2Periph_GPIOD, GPIOD, GPIO_Pin_2 },  /* TIM1_CH1 */
    { PC4, 4, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_4 },  /* TIM1_CH4 */
};

#define PWM_PIN_COUNT (sizeof(pwm_pins) / sizeof(pwm_pins[0]))
static uint16_t s_timer_period = 0;

static const pwm_pin_def_t* find_pin(pin_t pin)
{
    for (uint32_t i = 0; i < PWM_PIN_COUNT; i++) {
        if (pwm_pins[i].pin == pin) return &pwm_pins[i];
    }
    return NULL;
}

static void set_compare(uint8_t ch, uint16_t val)
{
    switch (ch) {
        case 1: TIM_SetCompare1(TIM1, val); break;
        case 2: TIM_SetCompare2(TIM1, val); break;
        case 3: TIM_SetCompare3(TIM1, val); break;
        case 4: TIM_SetCompare4(TIM1, val); break;
        default: break;
    }
}

static void init_oc(uint8_t ch)
{
    TIM_OCInitTypeDef oc = {0};
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;
    oc.TIM_Pulse       = 0;
    switch (ch) {
        case 1: TIM_OC1Init(TIM1, &oc); TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable); break;
        case 2: TIM_OC2Init(TIM1, &oc); TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable); break;
        case 3: TIM_OC3Init(TIM1, &oc); TIM_OC3PreloadConfig(TIM1, TIM_OCPreload_Enable); break;
        case 4: TIM_OC4Init(TIM1, &oc); TIM_OC4PreloadConfig(TIM1, TIM_OCPreload_Enable); break;
        default: break;
    }
}

void pwm_init(pin_t pin, uint32_t freq_hz)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) return;

    RCC_APB2PeriphClockCmd(def->rcc_gpio | RCC_APB2Periph_TIM1, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = def->gpio_pin;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(def->gpio_port, &gpio);

    /* TIM1 on APB2, timer clock = SystemCoreClock */
    uint32_t timer_clk = SystemCoreClock;
    uint32_t total = (freq_hz != 0U) ? (timer_clk / freq_hz) : 1U;
    if (total == 0U) total = 1U;

    uint16_t psc = 0;
    uint16_t arr = (uint16_t)(total - 1U);
    /* If total > 65536, find a prescaler */
    for (uint32_t p = 0; p < 65536U; p++) {
        uint32_t a = total / (p + 1U) - 1U;
        if (a <= PWM_ARR_MAX) {
            psc = (uint16_t)p;
            arr = (uint16_t)a;
            break;
        }
    }

    s_timer_period = arr;

    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler         = psc;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_Period            = arr;
    tb.TIM_ClockDivision     = 0;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tb);

    init_oc(def->channel);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_Cmd(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
}

void pwm_write(pin_t pin, uint8_t duty)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) return;
    uint16_t compare = (uint16_t)(((uint32_t)duty * s_timer_period) / PWM_DUTY_MAX_8BIT);
    set_compare(def->channel, compare);
}

void pwm_write_pct(pin_t pin, uint8_t percent)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) return;
    if (percent > PWM_PCT_MAX) percent = (uint8_t)PWM_PCT_MAX;
    uint16_t compare = (uint16_t)(((uint32_t)percent * s_timer_period) / PWM_PCT_MAX);
    set_compare(def->channel, compare);
}

void pwm_write_us(pin_t pin, uint32_t pulse_us)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) return;
    uint16_t psc = TIM1->PSC;
    uint32_t tick_rate = SystemCoreClock / (psc + 1U);
    /* Avoid integer truncation: compute ticks = pulse_us * tick_rate / 1e6
     * using (pulse_us * (tick_rate / 1000)) / 1000 to stay in 32-bit range */
    uint32_t ticks = (pulse_us * (tick_rate / 1000U) + 500U) / 1000U;
    if (ticks > s_timer_period) ticks = s_timer_period;
    set_compare(def->channel, (uint16_t)ticks);
}

void pwm_write_raw(pin_t pin, uint16_t compare)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) return;
    set_compare(def->channel, compare);
}

void pwm_stop(pin_t pin)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == NULL) return;
    set_compare(def->channel, 0);
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = def->gpio_pin;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(def->gpio_port, &gpio);
    GPIO_ResetBits(def->gpio_port, def->gpio_pin);
}
