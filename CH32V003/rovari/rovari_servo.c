/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_servo.c
 * @brief Servo motor control for CH32V003.
 *
 * TIM1 at 50 Hz, PSC=47 for exact 1us tick resolution.
 * Supports PD2 (CH1) and PC4 (CH4) simultaneously.
 * S-curve uses quintic smoothstep in Q15 fixed point.
 *
 * @req REQ-ROVARI-SERVO-0010
 */

#include <stdint.h>
#include "debug.h"
#include "rovari_servo.h"
#include "rovari_gpio.h"

/* -----------------------------------------------------------------------
 *  Timer configuration
 * ----------------------------------------------------------------------- */
#define TIM1_PSC_1US    47              /* 48MHz/(47+1) = 1MHz = 1us tick */
#define TIM1_ARR_50HZ   (20000U - 1U)   /* 20ms period */

/* -----------------------------------------------------------------------
 *  Pin to channel mapping
 * ----------------------------------------------------------------------- */
typedef struct {
    pin_t    pin;
    uint8_t  channel;     /* 1 or 4 */
    uint32_t rcc;
    GPIO_TypeDef* port;
    uint16_t gpio_pin;
} servo_pin_def_t;

static const servo_pin_def_t servo_pins[] = {
    { PD2, 1, RCC_APB2Periph_GPIOD, GPIOD, GPIO_Pin_2 },
    { PC4, 4, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_4 },
};

#define SERVO_PIN_COUNT (sizeof(servo_pins) / sizeof(servo_pins[0]))

static uint8_t s_tim1_inited = 0;

static const servo_pin_def_t* find_servo(pin_t pin)
{
    for (uint8_t i = 0; i < SERVO_PIN_COUNT; i++)
    {
        if (servo_pins[i].pin == pin)
        {
            return &servo_pins[i];
        }
    }
    return 0;
}

static void set_compare(uint8_t ch, uint16_t val)
{
    switch (ch)
    {
        case 1: TIM1->CH1CVR = val; break;
        case 4: TIM1->CH4CVR = val; break;
        default: break;
    }
}

static uint16_t get_compare(uint8_t ch)
{
    switch (ch)
    {
        case 1: return (uint16_t)TIM1->CH1CVR;
        case 4: return (uint16_t)TIM1->CH4CVR;
        default: return SERVO_CENTER_US;
    }
}

/* -----------------------------------------------------------------------
 *  Q15 fixed-point math for S-curve
 * ----------------------------------------------------------------------- */
#define Q15_ONE  32768U

static uint32_t q15_mul(uint32_t a, uint32_t b)
{
    return (uint32_t)(((uint64_t)a * (uint64_t)b + (1U << 14)) >> 15);
}

/**
 * Quintic smoothstep: s(t) = 6t^5 - 15t^4 + 10t^3
 * Input and output in Q15 [0..32768].
 * Zero velocity and zero acceleration at both endpoints.
 */
static uint32_t scurve5_q15(uint32_t t)
{
    uint32_t t2 = q15_mul(t, t);
    uint32_t t3 = q15_mul(t2, t);
    uint32_t t4 = q15_mul(t3, t);
    uint32_t t5 = q15_mul(t4, t);

    int64_t s = (int64_t)6 * (int64_t)t5
              - (int64_t)15 * (int64_t)t4
              + (int64_t)10 * (int64_t)t3;

    if (s < 0)               { s = 0; }
    if (s > (int64_t)Q15_ONE) { s = (int64_t)Q15_ONE; }

    return (uint32_t)s;
}

/* -----------------------------------------------------------------------
 *  Angle / microsecond conversion
 * ----------------------------------------------------------------------- */
static uint16_t deg_to_us(uint16_t deg)
{
    if (deg < SERVO_SAFE_MIN_DEG) { deg = SERVO_SAFE_MIN_DEG; }
    if (deg > SERVO_SAFE_MAX_DEG) { deg = SERVO_SAFE_MAX_DEG; }

    uint32_t span = (uint32_t)(SERVO_MAX_US - SERVO_MIN_US);
    return (uint16_t)(SERVO_MIN_US + (span * deg) / 180U);
}

static uint16_t us_to_deg(uint16_t us)
{
    if (us < SERVO_MIN_US) { us = SERVO_MIN_US; }
    if (us > SERVO_MAX_US) { us = SERVO_MAX_US; }

    int32_t span = SERVO_MAX_US - SERVO_MIN_US;
    return (uint16_t)(((us - SERVO_MIN_US) * 180U) / span);
}

/* -----------------------------------------------------------------------
 *  TIM1 setup (called once on first servo_init)
 * ----------------------------------------------------------------------- */
static void tim1_servo_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler         = TIM1_PSC_1US;
    tb.TIM_Period            = TIM1_ARR_50HZ;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tb);

    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_Cmd(TIM1, ENABLE);

    s_tim1_inited = 1;
}

static void init_oc(uint8_t ch, uint16_t pulse)
{
    TIM_OCInitTypeDef oc = {0};
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;
    oc.TIM_Pulse       = pulse;

    switch (ch)
    {
        case 1:
            TIM_OC1Init(TIM1, &oc);
            TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
            break;
        case 4:
            TIM_OC4Init(TIM1, &oc);
            TIM_OC4PreloadConfig(TIM1, TIM_OCPreload_Enable);
            break;
        default:
            break;
    }
}

/* -----------------------------------------------------------------------
 *  Public C API
 * ----------------------------------------------------------------------- */

/**
 * @brief Initialize a servo on the given pin.
 * @req REQ-ROVARI-SERVO-0010
 */
void servo_init(pin_t pin)
{
    const servo_pin_def_t* def = find_servo(pin);
    if (def == 0) { return; }

    if (!s_tim1_inited)
    {
        tim1_servo_init();
    }

    RCC_APB2PeriphClockCmd(def->rcc, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = def->gpio_pin;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_Init(def->port, &gpio);

    init_oc(def->channel, SERVO_CENTER_US);
}

/**
 * @brief Write pulse width in microseconds.
 * @req REQ-ROVARI-SERVO-0011
 */
void servo_write_us(pin_t pin, uint16_t us)
{
    const servo_pin_def_t* def = find_servo(pin);
    if (def == 0) { return; }

    if (us < SERVO_MIN_US) { us = SERVO_MIN_US; }
    if (us > SERVO_MAX_US) { us = SERVO_MAX_US; }

    set_compare(def->channel, us);
}

/**
 * @brief Write angle in degrees (0 to 180).
 * @req REQ-ROVARI-SERVO-0012
 */
void servo_write_deg(pin_t pin, uint16_t deg)
{
    if (deg > 180) { deg = 180; }
    servo_write_us(pin, deg_to_us(deg));
}

/**
 * @brief Read current angle from timer register.
 * @req REQ-ROVARI-SERVO-0013
 */
uint16_t servo_read_deg(pin_t pin)
{
    const servo_pin_def_t* def = find_servo(pin);
    if (def == 0) { return 90; }
    return us_to_deg(get_compare(def->channel));
}

/**
 * @brief Simple linear sweep.
 * @sevs-bound: max 180 iterations.
 * @req REQ-ROVARI-SERVO-0020
 */
void servo_sweep(pin_t pin, uint16_t start_deg, uint16_t end_deg,
                 uint32_t duration_ms)
{
    if (start_deg > 180) { start_deg = 180; }
    if (end_deg > 180)   { end_deg = 180; }

    int32_t diff = (int32_t)end_deg - (int32_t)start_deg;
    uint32_t steps = (uint32_t)(diff >= 0 ? diff : -diff);
    if (steps == 0) { servo_write_deg(pin, end_deg); return; }

    uint32_t per_step = duration_ms / steps;
    if (per_step == 0) { per_step = 1; }

    for (uint32_t i = 0; i <= steps; i++)
    {
        int32_t a;
        if (diff > 0)
        {
            a = (int32_t)start_deg + (int32_t)i;
        }
        else
        {
            a = (int32_t)start_deg - (int32_t)i;
        }
        servo_write_deg(pin, (uint16_t)a);
        Delay_Ms(per_step);
    }

    servo_write_deg(pin, end_deg);
}

/**
 * @brief Trapezoidal accel/coast/decel sweep.
 * @sevs-bound: max 180 iterations.
 * @req REQ-ROVARI-SERVO-0021
 */
void servo_sweep_trapezoid(pin_t pin, uint16_t start_deg, uint16_t end_deg,
                           uint32_t duration_ms)
{
    if (start_deg > 180) { start_deg = 180; }
    if (end_deg > 180)   { end_deg = 180; }

    int32_t diff = (int32_t)end_deg - (int32_t)start_deg;
    uint32_t steps = (uint32_t)(diff >= 0 ? diff : -diff);
    if (steps == 0) { servo_write_deg(pin, end_deg); return; }

    uint32_t per_step = duration_ms / steps;
    if (per_step == 0) { per_step = 1; }

    uint32_t accel = steps / 4;
    uint32_t mid   = steps / 2;
    uint32_t decel = steps - accel - mid;

    uint32_t pos = 0;

    /* Accelerate (2x delay = slow start) */
    for (uint32_t i = 0; i < accel; i++)
    {
        int32_t a = (diff > 0) ? (int32_t)start_deg + (int32_t)pos
                               : (int32_t)start_deg - (int32_t)pos;
        servo_write_deg(pin, (uint16_t)a);
        Delay_Ms(per_step * 2);
        pos++;
    }

    /* Coast (1x delay = full speed) */
    for (uint32_t i = 0; i < mid; i++)
    {
        int32_t a = (diff > 0) ? (int32_t)start_deg + (int32_t)pos
                               : (int32_t)start_deg - (int32_t)pos;
        servo_write_deg(pin, (uint16_t)a);
        Delay_Ms(per_step);
        pos++;
    }

    /* Decelerate (2x delay = slow stop) */
    for (uint32_t i = 0; i < decel; i++)
    {
        int32_t a = (diff > 0) ? (int32_t)start_deg + (int32_t)pos
                               : (int32_t)start_deg - (int32_t)pos;
        servo_write_deg(pin, (uint16_t)a);
        Delay_Ms(per_step * 2);
        pos++;
    }

    servo_write_deg(pin, end_deg);
}

/**
 * @brief S-curve quintic sweep between two angles.
 * @sevs-bound: max duration_ms/tick_ms iterations, clamped to 1000.
 * @req REQ-ROVARI-SERVO-0022
 */
void servo_sweep_scurve(pin_t pin, uint16_t start_deg, uint16_t end_deg,
                        uint32_t duration_ms, uint16_t tick_ms)
{
    if (start_deg > 180) { start_deg = 180; }
    if (end_deg > 180)   { end_deg = 180; }
    if (duration_ms < 5) { duration_ms = 5; }
    if (tick_ms < 2)     { tick_ms = 2; }

    int32_t diff = (int32_t)end_deg - (int32_t)start_deg;

    uint32_t steps = duration_ms / tick_ms;
    if (steps < 1)    { steps = 1; }
    if (steps > 1000) { steps = 1000; }  /* @sevs-bound */

    for (uint32_t k = 0; k <= steps; k++)
    {
        uint32_t t_q15 = (uint32_t)((uint64_t)Q15_ONE * k / steps);
        uint32_t s_q15 = scurve5_q15(t_q15);

        int32_t delta = (int32_t)(((int64_t)diff * (int64_t)s_q15
                        + (Q15_ONE >> 1)) >> 15);
        int32_t a = (int32_t)start_deg + delta;

        if (a < 0)   { a = 0; }
        if (a > 180) { a = 180; }

        servo_write_deg(pin, (uint16_t)a);
        Delay_Ms(tick_ms);
    }

    servo_write_deg(pin, end_deg);
}

/**
 * @brief S-curve from current position to target.
 * Reads current angle from timer register.
 * @req REQ-ROVARI-SERVO-0023
 */
void servo_move_to(pin_t pin, uint16_t target_deg,
                   uint32_t duration_ms, uint16_t tick_ms)
{
    if (target_deg > 180) { target_deg = 180; }

    uint16_t current = servo_read_deg(pin);
    servo_sweep_scurve(pin, current, target_deg, duration_ms, tick_ms);
}
