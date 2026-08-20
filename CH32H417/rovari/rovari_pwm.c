/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_pwm.c - PWM output implementation (CH32H417)
 *
 * The CH32H417 uses explicit alternate function (AF) muxing via
 * GPIO_PinAFConfig(). Each pin-to-timer mapping requires the correct
 * AF number from the datasheet.
 *
 * Pin mapping prioritizes free 3.3V VDDIO-domain pins:
 *   TIM4: PD12(CH1), PD13(CH2), PD14(CH3), PD15(CH4)  - AF2
 *   TIM5: PD12(CH1), PD13(CH2), PD14(CH3), PD15(CH4)  - AF6
 *   TIM3: PD5(CH3)                                     - AF9
 *
 * All timers run at HCLK (150 MHz).
 */

#include "rovari_pwm.h"
#include "debug.h"

/* -- Pin-to-timer-channel mapping ----------------------------------------- */
typedef struct {
    pin_t         pin;
    TIM_TypeDef*  timer;
    uint8_t       channel;    /* 1-4 */
    uint8_t       bus;        /* 1 = HB1, 2 = HB2 */
    uint32_t      rcc_tim;
    uint32_t      rcc_gpio;
    GPIO_TypeDef* gpio_port;
    uint16_t      gpio_pin;
    uint8_t       gpio_pinsource;  /* GPIO_PinSource0..15 */
    uint8_t       af;              /* GPIO_AFx */
} PwmPinDef;

static const PwmPinDef pwm_pins[] = {
    /* TIM4 on PD12-PD15 (HB1, AF2, free 3.3V pins) */
    { PD12, TIM4, 1, 1, RCC_HB1Periph_TIM4, RCC_HB2Periph_GPIOD, GPIOD, GPIO_Pin_12, GPIO_PinSource12, GPIO_AF2 },
    { PD13, TIM4, 2, 1, RCC_HB1Periph_TIM4, RCC_HB2Periph_GPIOD, GPIOD, GPIO_Pin_13, GPIO_PinSource13, GPIO_AF2 },
    { PD14, TIM4, 3, 1, RCC_HB1Periph_TIM4, RCC_HB2Periph_GPIOD, GPIOD, GPIO_Pin_14, GPIO_PinSource14, GPIO_AF2 },
    { PD15, TIM4, 4, 1, RCC_HB1Periph_TIM4, RCC_HB2Periph_GPIOD, GPIOD, GPIO_Pin_15, GPIO_PinSource15, GPIO_AF2 },

    /* TIM5 on PD12-PD15 (HB1, AF6, alternate timer on same pins) */
    /* Commented out: conflicts with TIM4 above. Uncomment if TIM4 is
     * needed for timer interrupts and you want PWM on TIM5 instead.
     * { PD12, TIM5, 1, 1, RCC_HB1Periph_TIM5, RCC_HB2Periph_GPIOD, GPIOD, GPIO_Pin_12, GPIO_PinSource12, GPIO_AF6 },
     * { PD13, TIM5, 2, 1, RCC_HB1Periph_TIM5, RCC_HB2Periph_GPIOD, GPIOD, GPIO_Pin_13, GPIO_PinSource13, GPIO_AF6 },
     * { PD14, TIM5, 3, 1, RCC_HB1Periph_TIM5, RCC_HB2Periph_GPIOD, GPIOD, GPIO_Pin_14, GPIO_PinSource14, GPIO_AF6 },
     * { PD15, TIM5, 4, 1, RCC_HB1Periph_TIM5, RCC_HB2Periph_GPIOD, GPIOD, GPIO_Pin_15, GPIO_PinSource15, GPIO_AF6 },
     */

    /* TIM3 on PC6-PC7 (HB1, AF2, free 3.3V pins, not used by 4-bit SDMMC) */
    { PC6,  TIM3, 1, 1, RCC_HB1Periph_TIM3, RCC_HB2Periph_GPIOC, GPIOC, GPIO_Pin_6,  GPIO_PinSource6,  GPIO_AF2 },
    { PC7,  TIM3, 2, 1, RCC_HB1Periph_TIM3, RCC_HB2Periph_GPIOC, GPIOC, GPIO_Pin_7,  GPIO_PinSource7,  GPIO_AF2 },
};

#define PWM_PIN_COUNT (sizeof(pwm_pins) / sizeof(pwm_pins[0]))

static const PwmPinDef* find_pin(pin_t pin)
{
    for (uint32_t i = 0; i < PWM_PIN_COUNT; i++) {
        if (pwm_pins[i].pin == pin) return &pwm_pins[i];
    }
    return 0;
}

/* Store the period (ARR) for each timer so duty functions can scale */
static uint16_t timer_period[13] = {0};  /* indexed by timer number 1-12 */

static uint8_t timer_number(TIM_TypeDef* tim)
{
    if (tim == TIM1) return 1;
    if (tim == TIM2) return 2;
    if (tim == TIM3) return 3;
    if (tim == TIM4) return 4;
    if (tim == TIM5) return 5;
    if (tim == TIM6) return 6;
    if (tim == TIM7) return 7;
    if (tim == TIM8) return 8;
    if (tim == TIM9) return 9;
    if (tim == TIM10) return 10;
    if (tim == TIM11) return 11;
    if (tim == TIM12) return 12;
    return 0;
}

/* -- Prescaler/period calculation ----------------------------------------- */
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

/* -- Set compare value for a specific channel ----------------------------- */
static void set_compare(TIM_TypeDef* tim, uint8_t ch, uint16_t val)
{
    switch (ch) {
        case 1: TIM_SetCompare1(tim, val); break;
        case 2: TIM_SetCompare2(tim, val); break;
        case 3: TIM_SetCompare3(tim, val); break;
        case 4: TIM_SetCompare4(tim, val); break;
    }
}

/* -- Configure OC for a specific channel ---------------------------------- */
static void init_oc(TIM_TypeDef* tim, uint8_t ch)
{
    TIM_OCInitTypeDef oc = {0};
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState  = TIM_OutputState_Enable;
    oc.TIM_OCPolarity   = TIM_OCPolarity_High;
    oc.TIM_Pulse        = 0;

    switch (ch) {
        case 1: TIM_OC1Init(tim, &oc); TIM_OC1PreloadConfig(tim, TIM_OCPreload_Enable); break;
        case 2: TIM_OC2Init(tim, &oc); TIM_OC2PreloadConfig(tim, TIM_OCPreload_Enable); break;
        case 3: TIM_OC3Init(tim, &oc); TIM_OC3PreloadConfig(tim, TIM_OCPreload_Enable); break;
        case 4: TIM_OC4Init(tim, &oc); TIM_OC4PreloadConfig(tim, TIM_OCPreload_Enable); break;
    }
}

/* =========================================================================
 *  Public API
 * ========================================================================= */

void pwm_init(pin_t pin, uint32_t freq_hz)
{
    const PwmPinDef* def = find_pin(pin);
    if (!def) return;

    /* Enable GPIO, AFIO, and timer clocks */
    RCC_HB2PeriphClockCmd(def->rcc_gpio | RCC_HB2Periph_AFIO, ENABLE);
    if (def->bus == 2) {
        RCC_HB2PeriphClockCmd(def->rcc_tim, ENABLE);
    } else {
        RCC_HB1PeriphClockCmd(def->rcc_tim, ENABLE);
    }

    /* Configure AF mux (H417 requires explicit AF selection) */
    GPIO_PinAFConfig(def->gpio_port, def->gpio_pinsource, def->af);

    /* Configure pin as AF push-pull */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = def->gpio_pin;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(def->gpio_port, &gpio);

    /* All timers run at HCLK (150 MHz, no APB doubling) */
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    uint32_t timer_clk = clk.HCLK_Frequency;

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

    /* For TIM1 and TIM8 (advanced timers), must enable main output */
    if (def->timer == TIM1 || def->timer == TIM8) {
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

    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    uint32_t timer_clk = clk.HCLK_Frequency;

    uint8_t tn = timer_number(def->timer);
    uint16_t arr = timer_period[tn];

    uint16_t psc = def->timer->PSC;
    uint32_t tick_rate = timer_clk / (psc + 1);

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
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(def->gpio_port, &gpio);
    GPIO_ResetBits(def->gpio_port, def->gpio_pin);
}
